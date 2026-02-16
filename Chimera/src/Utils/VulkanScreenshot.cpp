#include "pch.h"
#include "VulkanScreenshot.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/Buffer.h"
#include <fstream>
#include <vector>
#include <algorithm>

namespace Chimera
{
    void VulkanScreenshot::SaveToPPM(VkImage sourceImage, 
                                     VkFormat sourceImageFormat, 
                                     VkExtent2D extent, 
                                     VkImageLayout currentLayout, 
                                     const std::string& filename)
    {
        VkDevice device = VulkanContext::Get().GetDevice();
        
        // 1. Determine size per pixel
        bool isRGBA16F = (sourceImageFormat == VK_FORMAT_R16G16B16A16_SFLOAT);
        uint32_t bytesPerPixel = isRGBA16F ? 8 : 4;
        
        // 2. Create Staging Buffer
        VkDeviceSize imageSize = extent.width * extent.height * bytesPerPixel;
        Buffer stagingBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);

        // 3. Execute Copy Command
        {
            ScopedCommandBuffer cmd;

            // Transition to TRANSFER_SRC_OPTIMAL
            VkImageMemoryBarrier barrier = VulkanUtils::CreateImageBarrier(
                sourceImage, 
                currentLayout, 
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, 
                VK_ACCESS_TRANSFER_READ_BIT
            );
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Copy
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { extent.width, extent.height, 1 };

            vkCmdCopyImageToBuffer(cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, (VkBuffer)(void*)(uintptr_t)stagingBuffer.GetBuffer(), 1, &region);

            // Transition back
            VkImageMemoryBarrier barrier2 = VulkanUtils::CreateImageBarrier(
                sourceImage, 
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                currentLayout, 
                VK_ACCESS_TRANSFER_READ_BIT, 
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT
            );
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier2);
        }

        // 4. Read Memory and Write PPM
        void* mappedData = stagingBuffer.Map();
        std::ofstream file(filename, std::ios::out | std::ios::binary);
        
        if (file.is_open())
        {
            file << "P6\n" << extent.width << " " << extent.height << "\n255\n";
            std::vector<unsigned char> pixelData(extent.width * extent.height * 3);

            if (isRGBA16F)
            {
                // Handle R16G16B16A16_SFLOAT (Half-float)
                unsigned short* hdata = (unsigned short*)mappedData;
                for (uint32_t i = 0; i < extent.width * extent.height; i++)
                {
                    for (int c = 0; c < 3; c++)
                    {
                        // Half float parts: sign:1, exponent:5, mantissa:10
                        unsigned short h = hdata[i * 4 + c];
                        
                        // Bit manipulation to extract float value (basic)
                        int e = (h >> 10) & 0x1f;
                        int m = h & 0x3ff;
                        float f = (e > 0) ? (1.0f + m / 1024.0f) * pow(2.0f, e - 15.0f) : (m / 1024.0f) * pow(2.0f, -14.0f);
                        if ((h >> 15) > 0)
                        {
                            f *= -1.0f;
                        }
                        
                        pixelData[i * 3 + c] = (unsigned char)(std::clamp(f, 0.0f, 1.0f) * 255.0f);
                    }
                }
            }
            else
            {
                // Handle 8-bit formats (RGBA/BGRA)
                unsigned char* data = (unsigned char*)mappedData;
                bool isBGR = (sourceImageFormat == VK_FORMAT_B8G8R8A8_SRGB || sourceImageFormat == VK_FORMAT_B8G8R8A8_UNORM);
                
                for (uint32_t i = 0; i < extent.width * extent.height; i++)
                {
                    if (isBGR)
                    {
                        pixelData[i * 3 + 0] = data[i * 4 + 2]; // R
                        pixelData[i * 3 + 1] = data[i * 4 + 1]; // G
                        pixelData[i * 3 + 2] = data[i * 4 + 0]; // B
                    }
                    else
                    {
                        pixelData[i * 3 + 0] = data[i * 4 + 0]; // R
                        pixelData[i * 3 + 1] = data[i * 4 + 1]; // G
                        pixelData[i * 3 + 2] = data[i * 4 + 2]; // B
                    }
                }
            }
            
            file.write((const char*)pixelData.data(), pixelData.size());
            file.close();
            CH_CORE_INFO("VulkanScreenshot: Saved screenshot to {}", filename);
        }
        
        stagingBuffer.Unmap();
    }
}