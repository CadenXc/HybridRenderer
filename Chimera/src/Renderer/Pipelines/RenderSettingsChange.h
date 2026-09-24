#pragma once

#include "Renderer/Backend/ShaderCommon.h"

namespace Chimera
{
enum class RenderSettingsChangeImpact
{
    None,
    HistoryInvalidation,
    GraphRebuild
};

constexpr RenderSettingsChangeImpact
ClassifyRenderFlagChanges(RenderFlags changedFlags)
{
    constexpr RenderFlags graphTopologyFlags =
        RenderFlags_TAABit | RenderFlags_SVGFBit |
        RenderFlags_SVGFTemporalBit | RenderFlags_SVGFSpatialBit;

    if (changedFlags == RenderFlags_None)
        return RenderSettingsChangeImpact::None;

    if ((changedFlags & graphTopologyFlags) != 0)
        return RenderSettingsChangeImpact::GraphRebuild;

    return RenderSettingsChangeImpact::HistoryInvalidation;
}
} // namespace Chimera
