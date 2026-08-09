#include "Engine/Render/PBR/HdrColorPipeline.h"

#include <algorithm>

namespace ve::pbr
{
    RenderTextureDesc BuildHdrSceneColorDesc(WindowExtent extent, const char* name)
    {
        RenderTextureDesc desc = {};
        desc.name = name != nullptr ? name : "HdrSceneColor";
        desc.extent = extent;
        desc.colorFormat = GetHdrColorFormat();
        return desc;
    }

    bool IsHdrFormat(rhi::RhiFormat format) noexcept
    {
        return format == rhi::RhiFormat::Rgba16Float || format == rhi::RhiFormat::Rgb32Float;
    }

    HdrSettings SanitizeHdrSettings(HdrSettings settings) noexcept
    {
        settings.exposure = std::clamp(settings.exposure, -16.0f, 16.0f);
        settings.whitePoint = std::max(settings.whitePoint, 0.001f);
        if (settings.toneMapping != ToneMappingMode::Reinhard && settings.toneMapping != ToneMappingMode::Aces)
        {
            settings.toneMapping = ToneMappingMode::Aces;
        }
        settings.padding = 0.0f;
        return settings;
    }
} // namespace ve::pbr
