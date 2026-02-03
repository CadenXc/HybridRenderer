#include "pch.h"
#include "core/scene/Scene.h"
#include <tiny_obj_loader.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <unordered_set>
#include <unordered_map>
#include <iostream>

namespace Chimera
{
    // Helper functions for GLTF loading
    VkFilter GetVkFilter(cgltf_int filter) {
        switch (filter) {
        case 0x2600:
        case 0x2700:
        case 0x2701:
            return VK_FILTER_NEAREST;
        case 0x2601:
        case 0x2702:
        case 0x2703:
            return VK_FILTER_LINEAR;
        default:
            return VK_FILTER_LINEAR;
        }
    }

    VkSamplerAddressMode GetVkAddressMode(cgltf_int address_mode) {
        switch (address_mode) {
        case 0x812F:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case 0x812D:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case 0x2901:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case 0x8370:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        }
    }

    Scene::Scene(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager)
        : m_Context(context), m_ResourceManager(resourceManager)
    {
        m_Camera.view = glm::lookAt(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        m_Camera.proj = glm::perspective(glm::radians(45.0f), context->GetSwapChainExtent().width / (float)context->GetSwapChainExtent().height, 0.1f, 1000.0f);
        m_Camera.proj[1][1] *= -1; 
        
        m_Camera.viewInverse = glm::inverse(m_Camera.view);
        m_Camera.projInverse = glm::inverse(m_Camera.proj);

        m_Light.direction = glm::vec4(1.0f, -3.0f, 1.0f, 0.0f);
        m_Light.color = glm::vec4(1.0f, 1.0f, 1.0f, 5.0f);
    }

    Scene::~Scene()
    {
        auto device = m_Context->GetDevice();
        if (m_TopLevelAS != VK_NULL_HANDLE) 
        {
            vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
        }
        
        for (auto as : m_BLASHandles) 
        {
            vkDestroyAccelerationStructureKHR(device, as, nullptr);
        }
    }

    void Scene::LoadModel(const std::string& path)
    {
        std::string ext = path.substr(path.find_last_of(".") + 1);
        if (ext == "gltf" || ext == "glb") {
            LoadGLTF(path);
        }
        else {
            // OBJ Loading
            tinyobj::attrib_t attrib;
            std::vector<tinyobj::shape_t> shapes;
            std::vector<tinyobj::material_t> materials;
            std::string warn, err;
            
            std::string baseDir = path.substr(0, path.find_last_of("/\\"));

            if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), baseDir.c_str()))
            {
                CH_CORE_ERROR("Failed to load model: {} \nWarn: {} \nErr: {}", path, warn, err);
                throw std::runtime_error(err);
            }

            int materialOffset = (int)m_Materials.size();

            for (const auto& mat : materials)
            {
                Material newMat{};
                newMat.baseColor = glm::vec4(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1.0f);
                newMat.emission = glm::vec4(mat.emission[0], mat.emission[1], mat.emission[2], 1.0f);
                newMat.metallic = mat.metallic;
                newMat.roughness = mat.roughness;

                if (!mat.diffuse_texname.empty()) 
                {
                    std::string texturePath = baseDir + "/" + mat.diffuse_texname;
                    
                    if (m_TextureMap.find(texturePath) == m_TextureMap.end()) 
                    {
                        auto texture = m_ResourceManager->LoadTexture(texturePath);
                        if (texture) 
                        {
                            m_TextureMap[texturePath] = (int)m_LoadedTextures.size();
                            m_LoadedTextures.push_back(std::move(texture));
                        }
                    }
                    
                    if (m_TextureMap.count(texturePath)) 
                    {
                        newMat.base_color_texture = m_TextureMap[texturePath];
                    }
                }
                m_Materials.push_back(newMat);
            }

            if (materials.empty()) 
            {
                Material defaultMat{};
                m_Materials.push_back(defaultMat);
            }

            std::unordered_map<Vertex, uint32_t> uniqueVertices;

            for (const auto& shape : shapes)
            {
                Mesh mesh{};
                mesh.name = shape.name;
                mesh.indexOffset = (uint32_t)m_Indices.size();
                mesh.vertexOffset = 0; 

                if (!shape.mesh.material_ids.empty() && shape.mesh.material_ids[0] >= 0) 
                {
                    mesh.materialIndex = materialOffset + shape.mesh.material_ids[0];
                } 
                else 
                {
                    mesh.materialIndex = materialOffset > 0 ? 0 : 0;
                }

                std::vector<uint32_t> currentShapeIndices;

                for (const auto& index : shape.mesh.indices)
                {
                    Vertex vertex{};

                    vertex.pos = {
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 1],
                        attrib.vertices[3 * index.vertex_index + 2]
                    };

                    if (index.normal_index >= 0) 
                    {
                        vertex.normal = {
                            attrib.normals[3 * index.normal_index + 0],
                            attrib.normals[3 * index.normal_index + 1],
                            attrib.normals[3 * index.normal_index + 2]
                        };
                    } 
                    else 
                    {
                        vertex.normal = { 0.0f, 1.0f, 0.0f };
                    }

                    if (index.texcoord_index >= 0) 
                    {
                        vertex.texCoord = {
                            attrib.texcoords[2 * index.texcoord_index + 0],
                            1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                        };
                    }

                    if (uniqueVertices.count(vertex) == 0) 
                    {
                        uniqueVertices[vertex] = (uint32_t)m_Vertices.size();
                        m_Vertices.push_back(vertex);
                    }

                    uint32_t finalIndex = uniqueVertices[vertex];
                    m_Indices.push_back(finalIndex);
                    currentShapeIndices.push_back(finalIndex);
                }

                mesh.indexCount = (uint32_t)m_Indices.size() - mesh.indexOffset;
                mesh.transform = glm::mat4(1.0f);

                for (size_t i = 0; i < currentShapeIndices.size(); i += 3) 
                {
                    if (i + 2 >= currentShapeIndices.size()) break;

                    uint32_t idx0 = currentShapeIndices[i];
                    uint32_t idx1 = currentShapeIndices[i + 1];
                    uint32_t idx2 = currentShapeIndices[i + 2];

                    Vertex& v0 = m_Vertices[idx0];
                    Vertex& v1 = m_Vertices[idx1];
                    Vertex& v2 = m_Vertices[idx2];

                    glm::vec3 edge1 = v1.pos - v0.pos;
                    glm::vec3 edge2 = v2.pos - v0.pos;
                    glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
                    glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

                    float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
                    if (isinf(f) || isnan(f)) f = 0.0f;

                    glm::vec3 tangent;
                    tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
                    tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
                    tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

                    v0.tangent += glm::vec4(tangent, 0.0f);
                    v1.tangent += glm::vec4(tangent, 0.0f);
                    v2.tangent += glm::vec4(tangent, 0.0f);
                }

                m_Meshes.push_back(mesh);
            }

            for (auto& v : m_Vertices) 
            {
                if (glm::length(glm::vec3(v.tangent)) > 0.001f) 
                {
                    glm::vec3 t = glm::normalize(glm::vec3(v.tangent));
                    glm::vec3 n = glm::normalize(v.normal);
                    t = glm::normalize(t - n * glm::dot(n, t));
                    v.tangent = glm::vec4(t, 1.0f);
                } 
                else 
                {
                    v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                }
            }

            CreateVertexBuffer();
            CreateIndexBuffer();
            BuildBLAS();
            BuildTLAS();
        }
    }

    void Scene::LoadGLTF(const std::string& path) {
        cgltf_options options{};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
        if (result != cgltf_result_success) {
            CH_CORE_ERROR("Failed to parse GLTF file: {}", path);
            return;
        }

        result = cgltf_load_buffers(&options, data, path.c_str());
        if (result != cgltf_result_success) {
            CH_CORE_ERROR("Failed to load GLTF buffers: {}", path);
            cgltf_free(data);
            return;
        }

        std::string baseDir = path.substr(0, path.find_last_of("/\\")) + "/";

        for (size_t i = 0; i < data->nodes_count; ++i) {
            cgltf_node* node = &data->nodes[i];
            if (!node->mesh) continue;

            glm::mat4 transform = glm::mat4(1.0f);
            cgltf_node_transform_world(node, glm::value_ptr(transform));

            for (size_t j = 0; j < node->mesh->primitives_count; ++j) {
                cgltf_primitive* primitive = &node->mesh->primitives[j];
                
                Mesh mesh{};
                mesh.name = node->name ? node->name : "GLTF_Node";
                mesh.indexOffset = (uint32_t)m_Indices.size();
                mesh.vertexOffset = (uint32_t)m_Vertices.size(); // Use global offset
                mesh.transform = transform;

                // Material
                if (primitive->material) {
                    Material mat{};
                    if (primitive->material->has_pbr_metallic_roughness) {
                        mat.baseColor = glm::make_vec4(primitive->material->pbr_metallic_roughness.base_color_factor);
                        mat.metallic = primitive->material->pbr_metallic_roughness.metallic_factor;
                        mat.roughness = primitive->material->pbr_metallic_roughness.roughness_factor;
                        
                        if (primitive->material->pbr_metallic_roughness.base_color_texture.texture && primitive->material->pbr_metallic_roughness.base_color_texture.texture->image) {
                             const char* uri = primitive->material->pbr_metallic_roughness.base_color_texture.texture->image->uri;
                             if (uri) {
                                 std::string texPath = baseDir + uri;
                                 if (m_TextureMap.find(texPath) == m_TextureMap.end()) {
                                     auto texture = m_ResourceManager->LoadTexture(texPath);
                                     if (texture) {
                                         m_TextureMap[texPath] = (int)m_LoadedTextures.size();
                                         m_LoadedTextures.push_back(std::move(texture));
                                     }
                                 }
                                 if (m_TextureMap.count(texPath)) {
                                     mat.base_color_texture = m_TextureMap[texPath];
                                 }
                             }
                        }
                    }
                    mesh.materialIndex = (int)m_Materials.size();
                    m_Materials.push_back(mat);
                } else {
                    Material defaultMat{};
                    mesh.materialIndex = (int)m_Materials.size();
                    m_Materials.push_back(defaultMat);
                }

                // Attributes
                cgltf_accessor* posAccessor = nullptr;
                cgltf_accessor* normAccessor = nullptr;
                cgltf_accessor* tanAccessor = nullptr;
                cgltf_accessor* uvAccessor = nullptr;

                for (size_t k = 0; k < primitive->attributes_count; ++k) {
                    if (primitive->attributes[k].type == cgltf_attribute_type_position) posAccessor = primitive->attributes[k].data;
                    if (primitive->attributes[k].type == cgltf_attribute_type_normal) normAccessor = primitive->attributes[k].data;
                    if (primitive->attributes[k].type == cgltf_attribute_type_tangent) tanAccessor = primitive->attributes[k].data;
                    if (primitive->attributes[k].type == cgltf_attribute_type_texcoord) uvAccessor = primitive->attributes[k].data;
                }

                if (posAccessor) {
                    for (size_t k = 0; k < posAccessor->count; ++k) {
                        Vertex v{};
                        cgltf_accessor_read_float(posAccessor, k, glm::value_ptr(v.pos), 3);
                        if (normAccessor) cgltf_accessor_read_float(normAccessor, k, glm::value_ptr(v.normal), 3);
                        if (tanAccessor) cgltf_accessor_read_float(tanAccessor, k, glm::value_ptr(v.tangent), 4);
                        if (uvAccessor) cgltf_accessor_read_float(uvAccessor, k, glm::value_ptr(v.texCoord), 2);
                        
                        m_Vertices.push_back(v);
                    }
                }

                // Indices
                if (primitive->indices) {
                    for (size_t k = 0; k < primitive->indices->count; ++k) {
                        // We must offset the indices by the current vertex offset
                        // Wait, if we use vertexOffset in vkCmdDrawIndexed, then indices should be relative to 0.
                        // BUT if we merge everything into one big buffer, indices usually point to absolute vertices if we don't separate draw calls per mesh carefully.
                        // However, vkCmdDrawIndexed takes `vertexOffset` which is added to the index value.
                        // So if we set `vertexOffset` in Draw call, then indices in buffer should be relative to the mesh start.
                        // cgltf gives indices relative to the accessor (0).
                        // So we should NOT add `mesh.vertexOffset` here IF we use `vkCmdDrawIndexed(..., vertexOffset)`.
                        
                        // BUT, wait. `mesh.vertexOffset` in my code is `m_Vertices.size()` BEFORE adding vertices.
                        // So the vertices for this mesh are at `m_Vertices[mesh.vertexOffset]` to `m_Vertices[mesh.vertexOffset + count]`.
                        // If indices are 0, 1, 2...
                        // vkCmdDrawIndexed(..., firstIndex=indexOffset, vertexOffset=vertexOffset, ...)
                        // GPU calculates: actual_index = index_buffer[firstIndex + i] + vertexOffset.
                        // If index_buffer has 0, 1, 2.
                        // Result: vertexOffset + 0, vertexOffset + 1. Correct.
                        
                        // So, indices in m_Indices should be raw local indices.
                        m_Indices.push_back((uint32_t)cgltf_accessor_read_index(primitive->indices, k));
                    }
                    mesh.indexCount = (uint32_t)primitive->indices->count;
                }

                m_Meshes.push_back(mesh);
            }
        }

        cgltf_free(data);

        CreateVertexBuffer();
        CreateIndexBuffer();
        BuildBLAS();
        BuildTLAS();
    }

    void Scene::CreateVertexBuffer()
    {
        VkDeviceSize bufferSize = sizeof(Vertex) * m_Vertices.size();
        CH_CORE_INFO("Creating Vertex Buffer: Vertices={}, Size={}", m_Vertices.size(), bufferSize);
        
        Buffer stagingBuffer(m_Context->GetAllocator(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        stagingBuffer.UploadData(m_Vertices.data(), bufferSize);

        m_VertexBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(), 
            bufferSize, 
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        CopyBuffer(stagingBuffer.GetBuffer(), m_VertexBuffer->GetBuffer(), bufferSize);
    }

    void Scene::CreateIndexBuffer()
    {
        VkDeviceSize bufferSize = sizeof(uint32_t) * m_Indices.size();

        Buffer stagingBuffer(m_Context->GetAllocator(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        stagingBuffer.UploadData(m_Indices.data(), bufferSize);

        m_IndexBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(), 
            bufferSize, 
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        CopyBuffer(stagingBuffer.GetBuffer(), m_IndexBuffer->GetBuffer(), bufferSize);
    }

    void Scene::BuildBLAS()
    {
        m_BLASBuffers.clear();
        m_BLASHandles.clear();

        VkDeviceAddress vertexAddress = m_VertexBuffer->GetDeviceAddress();
        VkDeviceAddress indexAddress = m_IndexBuffer->GetDeviceAddress();

        for (const auto& mesh : m_Meshes)
        {
            VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            geometry.geometry.triangles.vertexData.deviceAddress = vertexAddress; 
            geometry.geometry.triangles.vertexStride = sizeof(Vertex);
            geometry.geometry.triangles.maxVertex = (uint32_t)m_Vertices.size();
            
            geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
            geometry.geometry.triangles.indexData.deviceAddress = indexAddress;
            geometry.geometry.triangles.transformData.deviceAddress = 0;

            VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
            buildRangeInfo.primitiveCount = mesh.indexCount / 3;
            buildRangeInfo.primitiveOffset = mesh.indexOffset * sizeof(uint32_t); 
            buildRangeInfo.firstVertex = 0; 
            buildRangeInfo.transformOffset = 0;

            VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
            buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geometry;

            VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
            vkGetAccelerationStructureBuildSizesKHR(m_Context->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildRangeInfo.primitiveCount, &sizeInfo);

            auto blasBuffer = std::make_unique<Buffer>(m_Context->GetAllocator(), sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
            
            VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
            createInfo.buffer = blasBuffer->GetBuffer();
            createInfo.size = sizeInfo.accelerationStructureSize;
            createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            
            VkAccelerationStructureKHR handle;
            vkCreateAccelerationStructureKHR(m_Context->GetDevice(), &createInfo, nullptr, &handle);

            Buffer scratchBuffer(m_Context->GetAllocator(), sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
            buildInfo.dstAccelerationStructure = handle;
            buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

            VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &buildRangeInfo; 
            
            VkCommandBuffer cmd = BeginSingleTimeCommands();
            vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
            EndSingleTimeCommands(cmd);

            m_BLASBuffers.push_back(std::move(blasBuffer));
            m_BLASHandles.push_back(handle);
        }
    }

    void Scene::BuildTLAS()
    {
        if (m_BLASHandles.empty()) return;

        std::vector<VkAccelerationStructureInstanceKHR> instances;
        instances.reserve(m_Meshes.size());

        for (size_t i = 0; i < m_Meshes.size(); ++i)
        {
            VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
            addressInfo.accelerationStructure = m_BLASHandles[i];
            VkDeviceAddress blasAddress = vkGetAccelerationStructureDeviceAddressKHR(m_Context->GetDevice(), &addressInfo);

            VkAccelerationStructureInstanceKHR instance{};
            
            // Identity matrix
            instance.transform = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f
            };
            
            instance.instanceCustomIndex = (uint32_t)i;
            instance.mask = 0xFF;
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = blasAddress;

            instances.push_back(instance);
        }

        VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * instances.size();
        Buffer instanceBuffer(m_Context->GetAllocator(), instanceBufferSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        
        Buffer stagingBuffer(m_Context->GetAllocator(), instanceBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        stagingBuffer.UploadData(instances.data(), instanceBufferSize);
        CopyBuffer(stagingBuffer.GetBuffer(), instanceBuffer.GetBuffer(), instanceBufferSize);

        VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers = VK_FALSE;
        geometry.geometry.instances.data.deviceAddress = instanceBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        uint32_t primitiveCount = (uint32_t)instances.size();
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        vkGetAccelerationStructureBuildSizesKHR(m_Context->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

        m_TLASBuffer = std::make_unique<Buffer>(m_Context->GetAllocator(), sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.buffer = m_TLASBuffer->GetBuffer();
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        vkCreateAccelerationStructureKHR(m_Context->GetDevice(), &createInfo, nullptr, &m_TopLevelAS);

        Buffer scratchBuffer(m_Context->GetAllocator(), sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        buildInfo.dstAccelerationStructure = m_TopLevelAS;
        buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
        buildRangeInfo.primitiveCount = primitiveCount;
        buildRangeInfo.primitiveOffset = 0;
        buildRangeInfo.firstVertex = 0;
        buildRangeInfo.transformOffset = 0;
        
        VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &buildRangeInfo;

        VkCommandBuffer cmd = BeginSingleTimeCommands();
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
        EndSingleTimeCommands(cmd);
    }

    void Scene::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        EndSingleTimeCommands(commandBuffer);
    }

    VkCommandBuffer Scene::BeginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_Context->GetCommandPool();
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_Context->GetDevice(), &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void Scene::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(m_Context->GetDevice(), m_Context->GetCommandPool(), 1, &commandBuffer);
    }
}
