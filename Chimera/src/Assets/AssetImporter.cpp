#include "pch.h"
#include "AssetImporter.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Core/Application.h"
#include "Core/TaskSystem.h"
#include <glm/gtc/type_ptr.hpp>

#include <cgltf.h>

namespace Chimera
{
std::shared_ptr<ImportedScene> AssetImporter::ImportScene(
    const std::string& path)
{
    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success)
    {
        return nullptr;
    }

    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success)
    {
        cgltf_free(data);
        return nullptr;
    }

    auto outScene = std::make_shared<ImportedScene>();

        // --- 1. Parallel Texture Preloading ---
    std::string baseDir = path.substr(0, path.find_last_of("/\\") + 1);
    std::vector<std::future<void>> textureFutures;
    std::set<std::string> uniquePaths;

    for (size_t i = 0; i < data->textures_count; ++i)
    {
        cgltf_texture& tex = data->textures[i];
        if (tex.image && tex.image->uri)
        {
            std::string relativeUri = tex.image->uri;
            std::replace(relativeUri.begin(), relativeUri.end(), '\\', '/');
            std::string fullPath = baseDir + relativeUri;

            if (uniquePaths.find(fullPath) == uniquePaths.end())
            {
                uniquePaths.insert(fullPath);
                // Decide if srgb based on usage (simplified: assume baseColor
                // is SRGB) In a real engine, you'd check which material slot
                // uses this texture. For now, we fire tasks that will populate
                // the cache in ResourceManager.
                textureFutures.push_back(
                    Application::Get().GetTaskSystem()->Enqueue(
                        [fullPath]()
                        {
                            // We load as SRGB by default, ResourceManager
                            // handles caching
                            ResourceManager::Get().LoadTexture(fullPath, true);
                        }));
            }
        }
    }

    // Wait for all decodes to finish (this happens in parallel)
    for (auto& f : textureFutures) f.wait();

    // 2. Load Materials (using now-cached textures)
    std::unordered_map<cgltf_image*, TextureHandle> textureCache;
    auto LoadTex = [&](cgltf_texture* tex, bool srgb)
    {
        if (!tex || !tex->image || !tex->image->uri)
        {
            return TextureHandle();
        }
        if (textureCache.count(tex->image))
        {
            return textureCache[tex->image];
        }

        std::string baseDir = path.substr(0, path.find_last_of("/\\") + 1);
        std::string relativeUri = tex->image->uri;
        std::replace(relativeUri.begin(), relativeUri.end(), '\\', '/');

        std::string texPath = baseDir + relativeUri;
        TextureHandle h = ResourceManager::Get().LoadTexture(texPath, srgb);
        textureCache[tex->image] = h;
        return h;
    };

    for (size_t i = 0; i < data->materials_count; ++i)
    {
        cgltf_material& gMat = data->materials[i];
        GpuMaterial mat{};
        mat.albedo = glm::vec4(1.0f);
        mat.emission = glm::vec4(0.0f);
        mat.roughness = 1.0f;
        mat.metallic = 0.0f;
        mat.albedoTex = -1;
        mat.normalTex = -1;
        mat.metalRoughTex = -1;
        mat.emissiveTex = -1;
        mat.aoTex = -1;

        if (gMat.has_pbr_metallic_roughness)
        {
            auto& pbr = gMat.pbr_metallic_roughness;
            mat.albedo = glm::make_vec4(pbr.base_color_factor);
            mat.metallic = pbr.metallic_factor;
            mat.roughness = pbr.roughness_factor;

            auto h = LoadTex(pbr.base_color_texture.texture, true);
            mat.albedoTex = h.IsValid() ? (int)h.id : -1;

            auto hm = LoadTex(pbr.metallic_roughness_texture.texture, false);
            mat.metalRoughTex = hm.IsValid() ? (int)hm.id : -1;
        }

        auto hn = LoadTex(gMat.normal_texture.texture, false);
        mat.normalTex = hn.IsValid() ? (int)hn.id : -1;

        // [NEW] Load Emissive Texture
        auto he = LoadTex(gMat.emissive_texture.texture, true);
        mat.emissiveTex = he.IsValid() ? (int)he.id : -1;

        // [NEW] Load Occlusion (AO) Texture
        auto hao = LoadTex(gMat.occlusion_texture.texture, false);
        mat.aoTex = hao.IsValid() ? (int)hao.id : -1;

        mat.emission = glm::vec4(glm::make_vec3(gMat.emissive_factor), 1.0f);
        outScene->Materials.push_back(mat);
    }

    if (outScene->Materials.empty())
    {
        outScene->Materials.push_back(GpuMaterial{});
    }

    // 2. Load Geometry
    for (size_t i = 0; i < data->nodes_count; ++i)
    {
        cgltf_node& node = data->nodes[i];
        if (!node.mesh)
        {
            continue;
        }

        glm::mat4 transform(1.0f);
        cgltf_node_transform_world(&node, glm::value_ptr(transform));

        for (size_t j = 0; j < node.mesh->primitives_count; ++j)
        {
            cgltf_primitive& prim = node.mesh->primitives[j];
            Mesh mesh{};
            mesh.name = node.name ? node.name : "Unnamed Mesh";
            mesh.transform = transform;
            mesh.materialIndex =
                prim.material ? (int)(prim.material - data->materials) : 0;
            mesh.vertexOffset = (uint32_t)outScene->Vertices.size();
            mesh.indexOffset = (uint32_t)outScene->Indices.size();

            cgltf_accessor *posAcc = nullptr, *normAcc = nullptr,
                           *uvAcc = nullptr, *tanAcc = nullptr;
            for (size_t k = 0; k < prim.attributes_count; ++k)
            {
                auto& attr = prim.attributes[k];
                if (attr.type == cgltf_attribute_type_position)
                    posAcc = attr.data;
                if (attr.type == cgltf_attribute_type_normal)
                    normAcc = attr.data;
                if (attr.type == cgltf_attribute_type_texcoord)
                    uvAcc = attr.data;
                if (attr.type == cgltf_attribute_type_tangent)
                    tanAcc = attr.data;
            }

            uint32_t startIdx = (uint32_t)outScene->Vertices.size();
            for (size_t k = 0; k < posAcc->count; ++k)
            {
                VertexInfo v{};
                cgltf_accessor_read_float(posAcc, k, glm::value_ptr(v.pos), 3);
                if (normAcc)
                    cgltf_accessor_read_float(normAcc, k,
                                              glm::value_ptr(v.normal), 3);
                if (uvAcc)
                    cgltf_accessor_read_float(uvAcc, k,
                                              glm::value_ptr(v.texCoord), 2);
                if (tanAcc)
                    cgltf_accessor_read_float(tanAcc, k,
                                              glm::value_ptr(v.tangent), 4);
                outScene->Vertices.push_back(v);
            }

            uint32_t indexCount = (uint32_t)prim.indices->count;
            std::vector<uint32_t> subIndices;
            for (size_t k = 0; k < indexCount; ++k)
            {
                uint32_t idx =
                    (uint32_t)cgltf_accessor_read_index(prim.indices, k);
                outScene->Indices.push_back(idx);
                subIndices.push_back(idx);
            }

            if (!tanAcc && uvAcc && normAcc)
            {
                for (size_t k = 0; k < indexCount; k += 3)
                {
                    uint32_t i0 = subIndices[k];
                    uint32_t i1 = subIndices[k + 1];
                    uint32_t i2 = subIndices[k + 2];

                    VertexInfo& v0 = outScene->Vertices[startIdx + i0];
                    VertexInfo& v1 = outScene->Vertices[startIdx + i1];
                    VertexInfo& v2 = outScene->Vertices[startIdx + i2];

                    glm::vec3 edge1 = v1.pos - v0.pos;
                    glm::vec3 edge2 = v2.pos - v0.pos;
                    glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
                    glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

                    float f = 1.0f / (deltaUV1.x * deltaUV2.y -
                                      deltaUV2.x * deltaUV1.y);
                    if (std::isinf(f)) f = 0.0f;

                    glm::vec3 tangent;
                    tangent.x =
                        f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
                    tangent.y =
                        f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
                    tangent.z =
                        f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

                    v0.tangent += glm::vec4(tangent, 0.0f);
                    v1.tangent += glm::vec4(tangent, 0.0f);
                    v2.tangent += glm::vec4(tangent, 0.0f);
                }
                // [FIX] Improved Tangent Calculation with correct Handedness
                // (w)
                for (size_t k = 0; k < posAcc->count; ++k)
                {
                    VertexInfo& v = outScene->Vertices[startIdx + k];
                    glm::vec3 t = glm::vec3(v.tangent);
                    glm::vec3 n = v.normal;
                    glm::vec3 orthoT = glm::normalize(t - n * glm::dot(n, t));

                    // Preserve original handedness if provided, otherwise
                    // default to 1.0
                    float w = (v.tangent.w != 0.0f) ? v.tangent.w : 1.0f;
                    v.tangent = glm::vec4(orthoT, w);
                }
            }

            mesh.indexCount = indexCount;

            // [NEW] Compute AABB
            if (posAcc->has_min && posAcc->has_max)
            {
                mesh.localBounds.min = glm::make_vec3(posAcc->min);
                mesh.localBounds.max = glm::make_vec3(posAcc->max);
            }
            else
            {
                // Fallback to manual computation if min/max not present in glTF
                for (size_t k = mesh.vertexOffset;
                     k < outScene->Vertices.size(); ++k)
                    mesh.localBounds.Merge(outScene->Vertices[k].pos);
            }

            outScene->Meshes.push_back(mesh);
        }
    }

    cgltf_free(data);
    return outScene;
}
} // namespace Chimera
