#include "pch.h"
#include "AssetImporter.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Core/Application.h"
#include "Core/TaskSystem.h"
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>

namespace Chimera
{
static glm::mat4 AssimpToGlmMatrix(const aiMatrix4x4& from)
{
    glm::mat4 to;
    to[0][0] = from.a1;
    to[1][0] = from.a2;
    to[2][0] = from.a3;
    to[3][0] = from.a4;
    to[0][1] = from.b1;
    to[1][1] = from.b2;
    to[2][1] = from.b3;
    to[3][1] = from.b4;
    to[0][2] = from.c1;
    to[1][2] = from.c2;
    to[2][2] = from.c3;
    to[3][2] = from.c4;
    to[0][3] = from.d1;
    to[1][3] = from.d2;
    to[2][3] = from.d3;
    to[3][3] = from.d4;
    return to;
}

static void TraverseNodes(aiNode* node, const aiScene* scene,
                          const glm::mat4& parentTransform,
                          ImportedScene& outScene, uint32_t parentIdx)
{
    glm::mat4 localTransform = AssimpToGlmMatrix(node->mTransformation);
    glm::mat4 worldTransform = parentTransform * localTransform;

    Node n{};
    n.name = node->mName.C_Str();
    n.transform = localTransform;

    uint32_t nodeIdx = (uint32_t)outScene.Nodes.size();
    outScene.Nodes.push_back(n);

    if (parentIdx != 0xFFFFFFFF)
    {
        outScene.Nodes[parentIdx].children.push_back((int)nodeIdx);
    }

    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* aMesh = scene->mMeshes[node->mMeshes[i]];
        Mesh mesh{};
        mesh.name = aMesh->mName.C_Str();
        mesh.materialIndex = aMesh->mMaterialIndex;
        mesh.transform = worldTransform;

        mesh.vertexOffset = (uint32_t)outScene.Vertices.size();
        mesh.indexOffset = (uint32_t)outScene.Indices.size();

        uint32_t meshVertexStart = (uint32_t)outScene.Vertices.size();

        for (unsigned int j = 0; j < aMesh->mNumVertices; j++)
        {
            VertexInfo v{};
            v.pos = {aMesh->mVertices[j].x, aMesh->mVertices[j].y,
                     aMesh->mVertices[j].z};
            if (aMesh->HasNormals())
                v.normal = {aMesh->mNormals[j].x, aMesh->mNormals[j].y,
                            aMesh->mNormals[j].z};
            if (aMesh->HasTextureCoords(0))
                v.texCoord = {aMesh->mTextureCoords[0][j].x,
                              aMesh->mTextureCoords[0][j].y};
            if (aMesh->HasTangentsAndBitangents())
                v.tangent = {aMesh->mTangents[j].x, aMesh->mTangents[j].y,
                             aMesh->mTangents[j].z, 1.0f};

            outScene.Vertices.push_back(v);
            mesh.localBounds.Merge(v.pos);
        }

        for (unsigned int j = 0; j < aMesh->mNumFaces; j++)
        {
            aiFace face = aMesh->mFaces[j];
            if (face.mNumIndices != 3) continue;

            uint32_t i0 = face.mIndices[0];
            uint32_t i1 = face.mIndices[1];
            uint32_t i2 = face.mIndices[2];

            outScene.Indices.push_back(i0);
            outScene.Indices.push_back(i1);
            outScene.Indices.push_back(i2);

            // Create GpuTriangle
            const VertexInfo& v0 = outScene.Vertices[meshVertexStart + i0];
            const VertexInfo& v1 = outScene.Vertices[meshVertexStart + i1];
            const VertexInfo& v2 = outScene.Vertices[meshVertexStart + i2];

            GpuTriangle tri{};
            tri.positionUvX0 = vec4(v0.pos, v0.texCoord.x);
            tri.positionUvX1 = vec4(v1.pos, v1.texCoord.x);
            tri.positionUvX2 = vec4(v2.pos, v2.texCoord.x);

            tri.normalUvY0 = vec4(v0.normal, v0.texCoord.y);
            tri.normalUvY1 = vec4(v1.normal, v1.texCoord.y);
            tri.normalUvY2 = vec4(v2.normal, v2.texCoord.y);

            tri.tangent0 = v0.tangent;
            tri.tangent1 = v1.tangent;
            tri.tangent2 = v2.tangent;

            tri.triCenter = (v0.pos + v1.pos + v2.pos) / 3.0f;

            outScene.Triangles.push_back(tri);
        }
        mesh.indexCount = (uint32_t)aMesh->mNumFaces * 3;
        outScene.Meshes.push_back(mesh);

        if (i == 0)
            outScene.Nodes[nodeIdx].meshIndex = (int)outScene.Meshes.size() - 1;
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        TraverseNodes(node->mChildren[i], scene, worldTransform, outScene,
                      nodeIdx);
    }
}

std::shared_ptr<ImportedScene> AssetImporter::ImportScene(
    const std::string& path)
{
    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, 65535);

    const aiScene* scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_FlipUVs |
                  aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals |
                  aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                  aiProcess_ImproveCacheLocality);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
        !scene->mRootNode)
    {
        CH_CORE_ERROR("Assimp Error: {0}", importer.GetErrorString());
        return nullptr;
    }

    auto outScene = std::make_shared<ImportedScene>();
    std::string baseDir =
        std::filesystem::path(path).parent_path().string() + "/";

    std::vector<std::future<void>> textureFutures;
    std::set<std::string> uniquePaths;

    auto QueueTexture = [&](const aiString& texPath, bool srgb)
    {
        if (texPath.length == 0) return;
        std::string fullPath = baseDir + texPath.C_Str();
        std::replace(fullPath.begin(), fullPath.end(), '\\', '/');

        if (uniquePaths.find(fullPath) == uniquePaths.end())
        {
            uniquePaths.insert(fullPath);
            textureFutures.push_back(
                Application::Get().GetTaskSystem()->Enqueue(
                    [fullPath, srgb]()
                    { ResourceManager::Get().LoadTexture(fullPath, srgb); }));
        }
    };

    for (unsigned int i = 0; i < scene->mNumMaterials; i++)
    {
        aiMaterial* mat = scene->mMaterials[i];
        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
            QueueTexture(texPath, true);
        if (mat->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS)
            QueueTexture(texPath, false);
        if (mat->GetTexture(aiTextureType_HEIGHT, 0, &texPath) == AI_SUCCESS)
            QueueTexture(texPath, false);
        if (mat->GetTexture(aiTextureType_METALNESS, 0, &texPath) == AI_SUCCESS)
            QueueTexture(texPath, false);
        if (mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texPath) ==
            AI_SUCCESS)
            QueueTexture(texPath, false);
        if (mat->GetTexture(aiTextureType_EMISSIVE, 0, &texPath) == AI_SUCCESS)
            QueueTexture(texPath, true);
        if (mat->GetTexture(aiTextureType_LIGHTMAP, 0, &texPath) == AI_SUCCESS)
            QueueTexture(texPath, false);
    }

    for (auto& f : textureFutures) f.wait();

    auto GetTexHandle = [&](aiMaterial* mat, aiTextureType type, bool srgb)
    {
        aiString texPath;
        if (mat->GetTexture(type, 0, &texPath) == AI_SUCCESS)
        {
            std::string fullPath = baseDir + texPath.C_Str();
            std::replace(fullPath.begin(), fullPath.end(), '\\', '/');
            return ResourceManager::Get().GetTextureIndex(fullPath);
        }
        return TextureHandle();
    };

    for (unsigned int i = 0; i < scene->mNumMaterials; i++)
    {
        aiMaterial* aMat = scene->mMaterials[i];
        GpuMaterial mat{};
        mat.colour = vec3(1.0f);
        mat.emission = vec3(0.0f);
        mat.roughness = 1.0f;
        mat.metallic = 0.0f;
        mat.materialType = (float)MaterialType::PBR;
        mat.opacity = 1.0f;
        mat.transmissionDepth = 0.01f;
        mat.scatteringColour = vec3(0.0f);

        aiColor4D color;
        if (aiGetMaterialColor(aMat, AI_MATKEY_COLOR_DIFFUSE, &color) ==
            AI_SUCCESS)
        {
            mat.colour = vec3(color.r, color.g, color.b);
            mat.opacity = color.a;
        }

        if (aiGetMaterialColor(aMat, AI_MATKEY_COLOR_EMISSIVE, &color) ==
            AI_SUCCESS)
            mat.emission = vec3(color.r, color.g, color.b);

        float value;
        if (aiGetMaterialFloat(aMat, AI_MATKEY_METALLIC_FACTOR, &value) ==
            AI_SUCCESS)
            mat.metallic = value;
        if (aiGetMaterialFloat(aMat, AI_MATKEY_ROUGHNESS_FACTOR, &value) ==
            AI_SUCCESS)
            mat.roughness = value;
        if (aiGetMaterialFloat(aMat, AI_MATKEY_OPACITY, &value) == AI_SUCCESS)
            mat.opacity = value;

        auto hAlbedo = GetTexHandle(aMat, aiTextureType_DIFFUSE, true);
        mat.colourTexture = hAlbedo.IsValid() ? (int)hAlbedo.id : -1;

        auto hNormal = GetTexHandle(aMat, aiTextureType_NORMALS, false);
        if (!hNormal.IsValid())
            hNormal = GetTexHandle(aMat, aiTextureType_HEIGHT, false);
        mat.normalTexture = hNormal.IsValid() ? (int)hNormal.id : -1;

        auto hMetal = GetTexHandle(aMat, aiTextureType_METALNESS, false);
        if (!hMetal.IsValid())
            hMetal = GetTexHandle(aMat, aiTextureType_UNKNOWN,
                                  false); // GLTF combined
        if (!hMetal.IsValid())
            hMetal = GetTexHandle(aMat, aiTextureType_DIFFUSE_ROUGHNESS, false);
        if (!hMetal.IsValid())
            hMetal = GetTexHandle(aMat, aiTextureType_SPECULAR,
                                  false); // OBJ fallback
        if (!hMetal.IsValid())
            hMetal = GetTexHandle(aMat, aiTextureType_SHININESS,
                                  false); // OBJ fallback

        mat.roughnessTexture = hMetal.IsValid() ? (int)hMetal.id : -1;

        auto hEmissive = GetTexHandle(aMat, aiTextureType_EMISSIVE, true);
        if (!hEmissive.IsValid())
            hEmissive = GetTexHandle(aMat, aiTextureType_EMISSION_COLOR, true);
        mat.emissionTexture = hEmissive.IsValid() ? (int)hEmissive.id : -1;

        outScene->Materials.push_back(mat);
    }

    if (outScene->Materials.empty())
        outScene->Materials.push_back(GpuMaterial{});

    TraverseNodes(scene->mRootNode, scene, glm::mat4(1.0f), *outScene,
                  0xFFFFFFFF);

    return outScene;
}

std::vector<AssetInfo> AssetImporter::GetAvailableModels(
    const std::string& rootDirectory)
{
    std::vector<AssetInfo> models;
    if (!std::filesystem::exists(rootDirectory)) return models;

    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(rootDirectory))
    {
        if (entry.is_regular_file())
        {
            auto ext = entry.path().extension();
            if (ext == ".gltf" || ext == ".glb" || ext == ".obj")
            {
                models.push_back(
                    {entry.path().filename().string(),
                     std::filesystem::relative(entry.path(), ".")
                         .generic_string()});
            }
        }
    }
    return models;
}

std::vector<AssetInfo> AssetImporter::GetAvailableHDRs(
    const std::string& rootDirectory)
{
    std::vector<AssetInfo> hdrs;
    if (!std::filesystem::exists(rootDirectory)) return hdrs;

    for (const auto& entry :
         std::filesystem::directory_iterator(rootDirectory))
    {
        if (entry.is_regular_file())
        {
            std::string ext = entry.path().extension().string();
            if (ext == ".hdr" || ext == ".exr" || ext == ".png" ||
                ext == ".jpg")
            {
                hdrs.push_back({entry.path().filename().string(),
                                entry.path().generic_string()});
            }
        }
    }
    return hdrs;
}
} // namespace Chimera
