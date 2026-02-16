#include "pch.h"
#include "AssetImporter.h"
#include "Renderer/Resources/ResourceManager.h"
#include <glm/gtc/type_ptr.hpp>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace Chimera
{
    std::shared_ptr<ImportedScene> AssetImporter::ImportScene(const std::string& path, ResourceManager* resourceManager)
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

        // 1. Load Materials
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
            // Unify separators
            std::replace(relativeUri.begin(), relativeUri.end(), '\\', '/');
            
            std::string texPath = baseDir + relativeUri;
            TextureHandle h = resourceManager->LoadTexture(texPath, srgb);
            textureCache[tex->image] = h;
            return h;
        };

        for (size_t i = 0; i < data->materials_count; ++i)
        {
            cgltf_material& gMat = data->materials[i];
            PBRMaterial mat{};
            mat.albedo = glm::vec4(1.0f);
            mat.emission = glm::vec4(0.0f);
            mat.roughness = 1.0f;
            mat.metallic = 0.0f;
            mat.albedoTex = -1;
            mat.normalTex = -1;
            mat.metalRoughTex = -1;

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
            
            mat.emission = glm::vec4(glm::make_vec3(gMat.emissive_factor), 1.0f);
            outScene->Materials.push_back(mat);
        }
        if (outScene->Materials.empty())
        {
            outScene->Materials.push_back(PBRMaterial{});
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
                mesh.materialIndex = prim.material ? (int)(prim.material - data->materials) : 0;
                mesh.vertexOffset = (uint32_t)outScene->Vertices.size();
                mesh.indexOffset = (uint32_t)outScene->Indices.size();

                cgltf_accessor *posAcc = nullptr, *normAcc = nullptr, *uvAcc = nullptr, *tanAcc = nullptr;
                for (size_t k = 0; k < prim.attributes_count; ++k)
                {
                    auto& attr = prim.attributes[k];
                    if (attr.type == cgltf_attribute_type_position) posAcc = attr.data;
                    if (attr.type == cgltf_attribute_type_normal) normAcc = attr.data;
                    if (attr.type == cgltf_attribute_type_texcoord) uvAcc = attr.data;
                    if (attr.type == cgltf_attribute_type_tangent) tanAcc = attr.data;
                }

                for (size_t k = 0; k < posAcc->count; ++k)
                {
                    VertexInfo v{};
                    cgltf_accessor_read_float(posAcc, k, glm::value_ptr(v.pos), 3);
                    if (normAcc) cgltf_accessor_read_float(normAcc, k, glm::value_ptr(v.normal), 3);
                    if (uvAcc) cgltf_accessor_read_float(uvAcc, k, glm::value_ptr(v.texCoord), 2);
                    if (tanAcc) cgltf_accessor_read_float(tanAcc, k, glm::value_ptr(v.tangent), 4);
                    outScene->Vertices.push_back(v);
                }

                for (size_t k = 0; k < prim.indices->count; ++k)
                {
                    outScene->Indices.push_back((uint32_t)cgltf_accessor_read_index(prim.indices, k));
                }

                mesh.indexCount = (uint32_t)prim.indices->count;
                outScene->Meshes.push_back(mesh);
            }
        }

        cgltf_free(data);
        return outScene;
    }
}
