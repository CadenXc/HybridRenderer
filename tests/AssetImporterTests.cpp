#include "Assets/AssetImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <iostream>
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
} // namespace

int main()
{
    try
    {
        TestTangentHandednessPreservesBitangentDirection();
        std::cout << "[PASS] tangent handedness preserves imported bitangent direction\n";
        TestGlbEmbeddedTextureCanBeResolvedAndDecoded();
        std::cout << "[PASS] GLB embedded texture resolves and decodes from memory\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
