#include "pch.h"
#include "core/scene/Scene.h"
#include "tiny_obj_loader.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace Chimera {

    Scene::Scene(std::shared_ptr<VulkanContext> context)
        : m_Context(context), m_BottomLevelAS(VK_NULL_HANDLE), m_TopLevelAS(VK_NULL_HANDLE)
    {
        // Initialize Default Camera
        m_Camera.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        m_Camera.proj = glm::perspective(glm::radians(45.0f), context->GetSwapChainExtent().width / (float)context->GetSwapChainExtent().height, 0.1f, 10.0f);
        m_Camera.proj[1][1] *= -1;
        m_Camera.viewInverse = glm::inverse(m_Camera.view);
        m_Camera.projInverse = glm::inverse(m_Camera.proj);

        // Initialize Default Light
        m_Light.direction = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        m_Light.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        m_Light.position = glm::vec4(2.0f, 4.0f, 2.0f, 0.0f);
    }

    Scene::~Scene()
    {
        if (m_BottomLevelAS != VK_NULL_HANDLE)
        {
            vkDestroyAccelerationStructureKHR(m_Context->GetDevice(), m_BottomLevelAS, nullptr);
        }
        if (m_TopLevelAS != VK_NULL_HANDLE)
        {
            vkDestroyAccelerationStructureKHR(m_Context->GetDevice(), m_TopLevelAS, nullptr);
        }
    }

    void Scene::LoadModel(const std::string& path)
    {
        m_Vertices.clear();
        m_Indices.clear();

        std::string extension = path.substr(path.find_last_of(".") + 1);
        
        if (extension == "obj") {
            LoadObj(path);
        } else if (extension == "glb" || extension == "gltf") {
            LoadGLTF(path);
        } else {
            throw std::runtime_error("Unsupported model format: " + extension);
        }

        CreateVertexBuffer();
        CreateIndexBuffer();
        BuildBLAS();
        BuildTLAS();
    }

    void Scene::LoadObj(const std::string& path)
    {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str()))
        {
            throw std::runtime_error(warn + err);
        }

        std::unordered_map<Vertex, uint32_t> uniqueVertices{};

        for (const auto& shape : shapes)
        {
            for (const auto& index : shape.mesh.indices)
            {
                Vertex vertex{};
                vertex.pos = { 
                    attrib.vertices[3 * index.vertex_index + 0], 
                    attrib.vertices[3 * index.vertex_index + 1], 
                    attrib.vertices[3 * index.vertex_index + 2] 
                };

                if (index.normal_index >= 0) {
                    vertex.normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    };
                } else {
                    vertex.normal = { 0.0f, 0.0f, 1.0f };
                }

                if (index.texcoord_index >= 0) {
                    vertex.texCoord = { 
                        attrib.texcoords[2 * index.texcoord_index + 0], 
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1] 
                    };
                } else {
                    vertex.texCoord = { 0.0f, 0.0f };
                }

                vertex.tangent = { 0.0f, 0.0f, 0.0f, 1.0f }; 

                if (uniqueVertices.count(vertex) == 0)
                {
                    uniqueVertices[vertex] = static_cast<uint32_t>(m_Vertices.size());
                    m_Vertices.push_back(vertex);
                }
                m_Indices.push_back(uniqueVertices[vertex]);
            }
        }
    }

    void Scene::CreateVertexBuffer()
    {
        VkDeviceSize bufferSize = sizeof(Vertex) * m_Vertices.size();

        Buffer stagingBuffer(
            m_Context->GetAllocator(),
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        stagingBuffer.UploadData(m_Vertices.data(), bufferSize);

        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        
        m_VertexBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            bufferSize,
            usageFlags,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        CopyBuffer(stagingBuffer.GetBuffer(), m_VertexBuffer->GetBuffer(), bufferSize);
    }

    void Scene::CreateIndexBuffer()
    {
        VkDeviceSize bufferSize = sizeof(uint32_t) * m_Indices.size();

        Buffer stagingBuffer(
            m_Context->GetAllocator(),
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        stagingBuffer.UploadData(m_Indices.data(), bufferSize);

        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        
        m_IndexBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            bufferSize,
            usageFlags,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        CopyBuffer(stagingBuffer.GetBuffer(), m_IndexBuffer->GetBuffer(), bufferSize);
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

    void Scene::BuildBLAS()
    {
        if (m_BottomLevelAS != VK_NULL_HANDLE)
        {
            vkDestroyAccelerationStructureKHR(m_Context->GetDevice(), m_BottomLevelAS, nullptr);
            m_BottomLevelAS = VK_NULL_HANDLE;
        }

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.vertexData.deviceAddress = m_VertexBuffer->GetDeviceAddress();
        geometry.geometry.triangles.vertexStride = sizeof(Vertex);
        geometry.geometry.triangles.maxVertex = static_cast<uint32_t>(m_Vertices.size());
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geometry.geometry.triangles.indexData.deviceAddress = m_IndexBuffer->GetDeviceAddress();

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        const uint32_t primitiveCount = static_cast<uint32_t>(m_Indices.size() / 3);
        VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
        buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(m_Context->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &buildSizesInfo);

        m_BLASBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            buildSizesInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = m_BLASBuffer->GetBuffer();
        createInfo.size = buildSizesInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        vkCreateAccelerationStructureKHR(m_Context->GetDevice(), &createInfo, nullptr, &m_BottomLevelAS);

        Buffer scratchBuffer(
            m_Context->GetAllocator(),
            buildSizesInfo.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );
        
        buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();
        buildInfo.dstAccelerationStructure = m_BottomLevelAS;

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = primitiveCount;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &pRangeInfo);
        EndSingleTimeCommands(commandBuffer);
    }
    
    void Scene::BuildTLAS()
    {
        if (m_TopLevelAS != VK_NULL_HANDLE)
        {
            vkDestroyAccelerationStructureKHR(m_Context->GetDevice(), m_TopLevelAS, nullptr);
            m_TopLevelAS = VK_NULL_HANDLE;
        }

        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.accelerationStructure = m_BottomLevelAS;
        VkDeviceAddress blasAddress = vkGetAccelerationStructureDeviceAddressKHR(m_Context->GetDevice(), &addressInfo);

        VkAccelerationStructureInstanceKHR instance{};
        instance.transform = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f
        };
        instance.instanceCustomIndex = 0;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = blasAddress;

        Buffer instanceBuffer(
            m_Context->GetAllocator(),
            sizeof(VkAccelerationStructureInstanceKHR),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        instanceBuffer.UploadData(&instance, sizeof(VkAccelerationStructureInstanceKHR));

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers = VK_FALSE;
        geometry.geometry.instances.data.deviceAddress = instanceBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        const uint32_t primitiveCount = 1; 
        VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
        buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(m_Context->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &buildSizesInfo);

        m_TLASBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            buildSizesInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = m_TLASBuffer->GetBuffer();
        createInfo.size = buildSizesInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        vkCreateAccelerationStructureKHR(m_Context->GetDevice(), &createInfo, nullptr, &m_TopLevelAS);

        Buffer scratchBuffer(
            m_Context->GetAllocator(),
            buildSizesInfo.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );
        
        buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();
        buildInfo.dstAccelerationStructure = m_TopLevelAS;

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = 1;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &pRangeInfo);
        EndSingleTimeCommands(commandBuffer);
    }

    void Scene::LoadGLTF(const std::string& path)
    {
        cgltf_options options = {};
        cgltf_data* data = NULL;
        cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);

        if (result != cgltf_result_success) {
            throw std::runtime_error("Failed to parse GLTF file: " + path);
        }

        result = cgltf_load_buffers(&options, data, path.c_str());
        if (result != cgltf_result_success) {
            cgltf_free(data);
            throw std::runtime_error("Failed to load GLTF buffers: " + path);
        }

        std::unordered_map<Vertex, uint32_t> uniqueVertices{};

        for (size_t i = 0; i < data->meshes_count; ++i) {
            cgltf_mesh* mesh = &data->meshes[i];
            for (size_t j = 0; j < mesh->primitives_count; ++j) {
                cgltf_primitive* primitive = &mesh->primitives[j];
                
                size_t vertexCount = primitive->attributes[0].data->count; 
                
                std::vector<glm::vec3> positions(vertexCount);
                std::vector<glm::vec3> normals(vertexCount);
                std::vector<glm::vec2> texCoords(vertexCount);

                for (size_t k = 0; k < primitive->attributes_count; ++k) {
                    cgltf_attribute* attribute = &primitive->attributes[k];
                    cgltf_accessor* accessor = attribute->data;
                    
                    if (attribute->type == cgltf_attribute_type_position) {
                        for (size_t v = 0; v < accessor->count; ++v) {
                            cgltf_accessor_read_float(accessor, v, &positions[v].x, 3);
                        }
                    } else if (attribute->type == cgltf_attribute_type_normal) {
                        for (size_t v = 0; v < accessor->count; ++v) {
                            cgltf_accessor_read_float(accessor, v, &normals[v].x, 3);
                        }
                    } else if (attribute->type == cgltf_attribute_type_texcoord) {
                        for (size_t v = 0; v < accessor->count; ++v) {
                            cgltf_accessor_read_float(accessor, v, &texCoords[v].x, 2);
                        }
                    }
                }

                if (primitive->indices) {
                    cgltf_accessor* indexAccessor = primitive->indices;
                    for (size_t k = 0; k < indexAccessor->count; ++k) {
                        uint32_t index = static_cast<uint32_t>(cgltf_accessor_read_index(indexAccessor, k));
                        
                        Vertex vertex{};
                        vertex.pos = positions[index];
                        vertex.normal = normals[index];
                        vertex.texCoord = texCoords[index];
                        vertex.tangent = { 0.0f, 0.0f, 0.0f, 1.0f };

                        if (uniqueVertices.count(vertex) == 0) {
                            uniqueVertices[vertex] = static_cast<uint32_t>(m_Vertices.size());
                            m_Vertices.push_back(vertex);
                        }
                        m_Indices.push_back(uniqueVertices[vertex]);
                    }
                }
            }
        }

        cgltf_free(data);
    }
}
