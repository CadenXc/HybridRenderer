#include "pch.h"
#include "ShaderRegistry.h"
#include "ShaderMetadata.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera {

    void ShaderRegistry::Init() {
        // --- RT_Standard: Absolute Match for RT Shaders ---
        ShaderLayout rtLayout;
        rtLayout.name = "RT_Standard";
        rtLayout.resources[RS::SCENE_AS]         = { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR };
        rtLayout.resources[RS::RT_SHADOW_AO]     = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE }; 
        rtLayout.resources[RS::RT_OUTPUT]        = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE }; 
        rtLayout.resources[RS::FINAL_COLOR]      = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE }; 
        rtLayout.resources[RS::MATERIAL_BUFFER]  = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
        rtLayout.resources["InstanceDataBuffer"] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
        rtLayout.resources[RS::TEXTURE_ARRAY]    = { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 }; 
        rtLayout.resources[RS::RT_REFLECTIONS]   = { 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        rtLayout.resources[RS::NORMAL]           = { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        rtLayout.resources[RS::DEPTH]            = { 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        rtLayout.resources[RS::MATERIAL]         = { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        ShaderLibrary::RegisterLayout("RT_Standard", rtLayout);

        // --- GBuffer_Standard: Matches gbuffer.frag ---
        ShaderLayout gbLayout;
        gbLayout.name = "GBuffer_Standard";
        gbLayout.resources[RS::MATERIAL_BUFFER] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
        gbLayout.resources[RS::TEXTURE_ARRAY]   = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 };
        ShaderLibrary::RegisterLayout("GBuffer_Standard", gbLayout);

        // --- Deferred_Standard: Matches deferred_lighting.frag ---
        ShaderLayout defLayout;
        defLayout.name = "Deferred_Standard";
        defLayout.resources[RS::ALBEDO]         = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        defLayout.resources[RS::NORMAL]         = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        defLayout.resources[RS::MATERIAL]       = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        defLayout.resources[RS::DEPTH]          = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        defLayout.resources[RS::RT_SHADOW_AO]   = { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        defLayout.resources[RS::RT_REFLECTIONS] = { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        ShaderLibrary::RegisterLayout("Deferred_Standard", defLayout);

        // --- SVGF_Standard: Matches svgf.comp ---
        ShaderLayout svgfLayout;
        svgfLayout.name = "SVGF_Standard";
        svgfLayout.resources[RS::NORMAL]          = { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        svgfLayout.resources[RS::MOTION]          = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        svgfLayout.resources[RS::DEPTH]           = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        svgfLayout.resources[RS::RT_SHADOW_AO]    = { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        svgfLayout.resources[RS::SVGF_OUTPUT]     = { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        svgfLayout.resources[RS::PREV_NORMAL]     = { 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        svgfLayout.resources[RS::PREV_DEPTH]      = { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        svgfLayout.resources[RS::SHADOW_AO_HIST]  = { 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        svgfLayout.resources[RS::MOMENTS_HIST]    = { 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        ShaderLibrary::RegisterLayout("SVGF_Standard", svgfLayout);

        // --- Atrous_Standard: Matches svgf_atrous.comp ---
        ShaderLayout atrousLayout;
        atrousLayout.name = "Atrous_Standard";
        atrousLayout.resources[RS::NORMAL]          = { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        atrousLayout.resources[RS::DEPTH]           = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        atrousLayout.resources[RS::ATROUS_PING]     = { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        atrousLayout.resources[RS::ATROUS_PONG]     = { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        ShaderLibrary::RegisterLayout("Atrous_Standard", atrousLayout);

        ShaderLayout bloomLayout;
        bloomLayout.resources[RS::BLOOM_BRIGHT]   = { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        bloomLayout.resources[RS::BLOOM_BLUR_TMP] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        bloomLayout.resources[RS::BLOOM_FINAL]    = { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        ShaderLibrary::RegisterLayout("Bloom_Standard", bloomLayout);
    }

}
