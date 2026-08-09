#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector4.h"

#include <type_traits>

namespace ve::pbr
{
    /// Tone-mapping operators supported by the first HDR presentation path.
    enum class ToneMappingMode : UInt32
    {
        Reinhard = 0,
        Aces = 1,
    };

    /// HDR and presentation controls shared by the renderer and PBR shaders.
    struct alignas(16) HdrSettings
    {
        Float32 exposure = 0.0f;
        ToneMappingMode toneMapping = ToneMappingMode::Aces;
        Float32 whitePoint = 1.0f;
        Float32 padding = 0.0f;
    };

    /// Material factors for the metallic-roughness PBR workflow.
    struct alignas(16) MaterialFactors
    {
        Vector4 baseColor = Vector4::One();
        Float32 metallic = 0.0f;
        Float32 roughness = 0.5f;
        Float32 normalScale = 1.0f;
        Float32 padding = 0.0f;
        Vector4 emissive = Vector4::Zero();
    };

    /// Directional light data consumed by the direct-light PBR evaluator.
    struct alignas(16) DirectionalLight
    {
        Vector4 direction = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
        Vector4 colorAndIntensity = Vector4::One();
    };

    static_assert(sizeof(HdrSettings) == 16);
    static_assert(sizeof(MaterialFactors) == 48);
    static_assert(sizeof(DirectionalLight) == 32);
    static_assert(std::is_trivially_copyable_v<HdrSettings>);
    static_assert(std::is_trivially_copyable_v<MaterialFactors>);
    static_assert(std::is_trivially_copyable_v<DirectionalLight>);

    /// Returns the render-target format used for an HDR scene color buffer.
    [[nodiscard]] constexpr rhi::RhiFormat GetHdrColorFormat() noexcept
    {
        return rhi::RhiFormat::Rgba16Float;
    }
} // namespace ve::pbr
