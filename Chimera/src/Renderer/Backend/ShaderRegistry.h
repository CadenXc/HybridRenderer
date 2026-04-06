#pragma once
#include "ShaderManager.h"

namespace Chimera
{
class ShaderRegistry
{
public:
    static void RegisterAll()
    {
            // --- 1. Forward / Raster Passes ---
        ShaderManager::RegisterAlias("Forward_Vert", "forward/forward.vert");
        ShaderManager::RegisterAlias("Forward_Frag", "forward/forward.frag");

            // --- 2. Hybrid / G-Buffer Passes ---
        ShaderManager::RegisterAlias("GBuffer_Vert", "hybrid/gbuffer.vert");
        ShaderManager::RegisterAlias("GBuffer_Frag", "hybrid/gbuffer.frag");

            // --- 3. Ray Tracing Passes ---
        ShaderManager::RegisterAlias("Raytrace_Gen",
                                     "raytracing/raytrace.rgen");
        ShaderManager::RegisterAlias("Raygen_Gen", "raytracing/raygen.rgen");
        ShaderManager::RegisterAlias("RT_Shadow_Gen",
                                     "raytracing/rt_shadow.rgen");
        ShaderManager::RegisterAlias("RT_AO_Gen", "raytracing/rt_ao.rgen");
        ShaderManager::RegisterAlias("Raytrace_Hit",
                                     "raytracing/closesthit.rchit");
        ShaderManager::RegisterAlias("Raytrace_Miss", "raytracing/miss.rmiss");
        ShaderManager::RegisterAlias("Shadow_AnyHit",
                                     "raytracing/shadow.rahit");
        ShaderManager::RegisterAlias("Shadow_Miss", "raytracing/shadow.rmiss");
        ShaderManager::RegisterAlias("RayQuery_Frag",
                                     "raytracing/rayquery.frag");
        ShaderManager::RegisterAlias("DiffuseGI_Gen",
                                     "raytracing/diffuse_gi.rgen");
        ShaderManager::RegisterAlias("Reflection_Gen",
                                     "raytracing/reflection.rgen");

            // --- 4. Post-Processing & SVGF ---
        ShaderManager::RegisterAlias("Composition_Frag",
                                     "postprocess/composition.frag");
        ShaderManager::RegisterAlias("SVGF_Temporal",
                                     "postprocess/svgf/temporal.comp");
        ShaderManager::RegisterAlias("SVGF_FilterMoments",
                                     "postprocess/svgf/filter_moments.comp");
        CH_CORE_INFO("ShaderRegistry: Registered SVGF_FilterMoments alias");
        ShaderManager::RegisterAlias("SVGF_VarianceBlur",
                                     "postprocess/svgf/variance_blur.comp");
        ShaderManager::RegisterAlias("SVGF_Atrous",
                                     "postprocess/svgf/atrous.comp");
        ShaderManager::RegisterAlias("SVGF_Combine",
                                     "postprocess/svgf/combine.comp");
        ShaderManager::RegisterAlias("TAA_Comp", "postprocess/taa.comp");
        ShaderManager::RegisterAlias("PostProcess_Frag",
                                     "postprocess/postprocess.frag");
        ShaderManager::RegisterAlias("Skybox_Frag", "postprocess/skybox.frag");
        ShaderManager::RegisterAlias("Fullscreen_Vert",
                                     "common/fullscreen.vert");
    }
};
} // namespace Chimera
