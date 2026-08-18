#include "Assets/AssetImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stb_image.h>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void TestTangentHandednessPreservesBitangentDirection()
{
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    const glm::vec3 tangent(1.0f, 0.0f, 0.0f);

    Require(Chimera::CalculateTangentHandedness(
                normal, tangent, glm::vec3(0.0f, 1.0f, 0.0f)) == 1.0f,
            "ordinary UV orientation must have positive tangent handedness");
    Require(Chimera::CalculateTangentHandedness(
                normal, tangent, glm::vec3(0.0f, -1.0f, 0.0f)) == -1.0f,
            "mirrored UV orientation must have negative tangent handedness");
}

void TestAssimpMatrixConversionPreservesPointTransform()
{
    const aiMatrix4x4 assimpMatrix(
        0.0f, -2.0f, 0.0f, 3.0f,
        1.0f,  0.0f, 0.0f, 4.0f,
        0.0f,  0.0f, 0.5f, 5.0f,
        0.0f,  0.0f, 0.0f, 1.0f);
    const aiVector3D assimpPoint(2.0f, 1.0f, 6.0f);
    const aiVector3D expected = assimpMatrix * assimpPoint;

    const glm::mat4 glmMatrix = Chimera::ConvertAssimpMatrix(assimpMatrix);
    const glm::vec4 actual =
        glmMatrix * glm::vec4(assimpPoint.x, assimpPoint.y, assimpPoint.z, 1.0f);

    constexpr float epsilon = 1e-6f;
    Require(std::abs(actual.x - expected.x) < epsilon &&
                std::abs(actual.y - expected.y) < epsilon &&
                std::abs(actual.z - expected.z) < epsilon &&
                std::abs(actual.w - 1.0f) < epsilon,
            "Assimp-to-GLM conversion must preserve affine point transforms");
}

void TestGlbEmbeddedTextureCanBeResolvedAndDecoded()
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(CHIMERA_EMBEDDED_GLTF_FIXTURE,
                                             aiProcess_Triangulate);

    Require(scene != nullptr, importer.GetErrorString());
    Require(scene->mNumMaterials > 0,
            "textured GLB fixture must contain a material");

    aiString textureReference;
    bool foundBaseColorTexture = false;
    for (unsigned int materialIndex = 0;
         materialIndex < scene->mNumMaterials; ++materialIndex)
    {
        if (scene->mMaterials[materialIndex]->GetTexture(
                aiTextureType_DIFFUSE, 0, &textureReference) == AI_SUCCESS)
        {
            foundBaseColorTexture = true;
            break;
        }
    }
    Require(foundBaseColorTexture,
            "textured GLB material must expose its base-color texture");
    Require(textureReference.length > 0 && textureReference.C_Str()[0] == '*',
            "GLB image must be represented as an embedded texture reference");

    const aiTexture* embedded =
        scene->GetEmbeddedTexture(textureReference.C_Str());
    Require(embedded != nullptr,
            "embedded texture reference must resolve to an aiTexture");
    Require(embedded->mHeight == 0 && embedded->mWidth > 0,
            "fixture must contain a compressed embedded image payload");

    int width = 0;
    int height = 0;
    int channels = 0;
    const auto* encoded =
        reinterpret_cast<const unsigned char*>(embedded->pcData);
    unsigned char* pixels = stbi_load_from_memory(
        encoded, static_cast<int>(embedded->mWidth), &width, &height,
        &channels, 4);
    Require(pixels != nullptr,
            "embedded image payload must decode from memory as RGBA pixels");
    stbi_image_free(pixels);
    Require(width > 0 && height > 0,
            "decoded embedded image must have a non-zero extent");
}

struct ImportedFixtureSummary
{
    aiVector3D min{std::numeric_limits<float>::max()};
    aiVector3D max{std::numeric_limits<float>::lowest()};
    aiVector2D uvMin{std::numeric_limits<float>::max()};
    aiVector2D uvMax{std::numeric_limits<float>::lowest()};
    unsigned int meshCount = 0;
    unsigned int texturedMeshCount = 0;
    bool hasUV = false;
};

void AccumulateImportedGeometry(const aiScene* scene, const aiNode* node,
                                const aiMatrix4x4& parentTransform,
                                ImportedFixtureSummary& summary)
{
    const aiMatrix4x4 worldTransform = parentTransform * node->mTransformation;
    for (unsigned int nodeMeshIndex = 0; nodeMeshIndex < node->mNumMeshes;
         ++nodeMeshIndex)
    {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[nodeMeshIndex]];
        ++summary.meshCount;

        aiString textureReference;
        const bool hasBaseColorTexture =
            scene->mMaterials[mesh->mMaterialIndex]->GetTexture(
                aiTextureType_DIFFUSE, 0, &textureReference) == AI_SUCCESS;
        if (hasBaseColorTexture) ++summary.texturedMeshCount;

        for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices;
             ++vertexIndex)
        {
            const aiVector3D position =
                worldTransform * mesh->mVertices[vertexIndex];
            summary.min.x = std::min(summary.min.x, position.x);
            summary.min.y = std::min(summary.min.y, position.y);
            summary.min.z = std::min(summary.min.z, position.z);
            summary.max.x = std::max(summary.max.x, position.x);
            summary.max.y = std::max(summary.max.y, position.y);
            summary.max.z = std::max(summary.max.z, position.z);

            if (hasBaseColorTexture && mesh->HasTextureCoords(0))
            {
                const aiVector3D uv = mesh->mTextureCoords[0][vertexIndex];
                summary.uvMin.x = std::min(summary.uvMin.x, uv.x);
                summary.uvMin.y = std::min(summary.uvMin.y, uv.y);
                summary.uvMax.x = std::max(summary.uvMax.x, uv.x);
                summary.uvMax.y = std::max(summary.uvMax.y, uv.y);
                summary.hasUV = true;
            }
        }
    }

    for (unsigned int childIndex = 0; childIndex < node->mNumChildren;
         ++childIndex)
    {
        AccumulateImportedGeometry(scene, node->mChildren[childIndex],
                                   worldTransform, summary);
    }
}

void TestTextureCoordinateFixtureKeepsExpectedGeometryAndUVs()
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        CHIMERA_EMBEDDED_GLTF_FIXTURE,
        aiProcess_Triangulate | aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals |
            aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
            aiProcess_ImproveCacheLocality);

    Require(scene != nullptr, importer.GetErrorString());
    Require(scene->mRootNode != nullptr,
            "textured GLB fixture must have a root node");

    ImportedFixtureSummary summary;
    AccumulateImportedGeometry(scene, scene->mRootNode, aiMatrix4x4(), summary);

    const aiVector3D extent = summary.max - summary.min;
    Require(summary.meshCount == 5,
            "TextureCoordinateTest must import exactly five meshes");
    Require(summary.texturedMeshCount == 4,
            "TextureCoordinateTest must keep four textured meshes");
    Require(extent.x > 2.3f && extent.y > 2.3f,
            "imported fixture must remain a two-dimensional XY test card");
    Require(extent.z < 0.1f,
            "imported fixture must not be rotated into a deep Z extent");
    Require(summary.hasUV, "textured meshes must retain TEXCOORD_0");
    Require(summary.uvMax.x - summary.uvMin.x > 0.5f &&
                summary.uvMax.y - summary.uvMin.y > 0.5f,
            "imported texture coordinates must not collapse to one value");
}

void TestFixtureMeshIndicesStayInsideLocalVertexRanges()
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        CHIMERA_EMBEDDED_GLTF_FIXTURE,
        aiProcess_Triangulate | aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals |
            aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
            aiProcess_ImproveCacheLocality);

    Require(scene != nullptr, importer.GetErrorString());
    Require(scene->mNumMeshes > 0, "imported fixture must contain meshes");

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes;
         ++meshIndex)
    {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        Require(mesh->mNumVertices > 0,
                "each imported mesh must record a non-zero vertex count");

        for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces;
             ++faceIndex)
        {
            const aiFace& face = mesh->mFaces[faceIndex];
            Require(face.mNumIndices == 3,
                    "triangulated fixture must contain only triangle faces");
            for (unsigned int index = 0; index < face.mNumIndices; ++index)
            {
                Require(face.mIndices[index] < mesh->mNumVertices,
                        "mesh-local index must stay below BLAS maxVertex + 1");
            }
        }
    }
}
} // namespace

int main()
{
    try
    {
        TestTangentHandednessPreservesBitangentDirection();
        std::cout << "[PASS] tangent handedness preserves imported bitangent direction\n";
        TestAssimpMatrixConversionPreservesPointTransform();
        std::cout << "[PASS] Assimp-to-GLM matrix conversion preserves point transforms\n";
        TestGlbEmbeddedTextureCanBeResolvedAndDecoded();
        std::cout << "[PASS] GLB embedded texture resolves and decodes from memory\n";
        TestTextureCoordinateFixtureKeepsExpectedGeometryAndUVs();
        std::cout << "[PASS] GLB fixture preserves its XY geometry and UV range\n";
        TestFixtureMeshIndicesStayInsideLocalVertexRanges();
        std::cout << "[PASS] imported mesh indices stay inside local vertex ranges\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
