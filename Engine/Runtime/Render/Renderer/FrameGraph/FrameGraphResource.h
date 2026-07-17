#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"

#include <limits>

namespace ve
{
    inline constexpr UInt32 InvalidFrameGraphResourceIndex = std::numeric_limits<UInt32>::max();

    /// Identifies one exact logical version of a typed frame-graph resource.
    template<typename T>
    struct FrameGraphResource
    {
        UInt32 index = InvalidFrameGraphResourceIndex;
        UInt32 version = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return index != InvalidFrameGraphResourceIndex;
        }

        [[nodiscard]] bool operator==(const FrameGraphResource&) const noexcept = default;
    };

    struct FrameGraphTexture
    {
    };

    using FrameGraphTextureHandle = FrameGraphResource<FrameGraphTexture>;

    /// Describes how one pass accesses a logical texture version.
    enum class FrameGraphTextureAccess
    {
        ColorAttachment,
        DepthAttachment,
        ShaderRead,
    };

    /// Physical properties used to create and reuse graph-owned textures.
    struct FrameGraphTextureDesc
    {
        rhi::RhiTextureDimension dimension = rhi::RhiTextureDimension::Texture2D;
        UInt32 width = 0;
        UInt32 height = 0;
        UInt32 depth = 1;
        UInt32 mipLevelCount = 1;
        rhi::RhiFormat format = rhi::RhiFormat::Rgba8Unorm;
        rhi::RhiTextureUsage usage = rhi::RhiTextureUsage::Sampled;

        [[nodiscard]] bool operator==(const FrameGraphTextureDesc&) const noexcept = default;
    };

    struct FrameGraphTextureDescHash
    {
        [[nodiscard]] SizeT operator()(const FrameGraphTextureDesc& desc) const noexcept;
    };

    /// Describes an externally owned texture. A swapchain color target has no RhiTexture pointer in the common RHI.
    struct ImportedFrameGraphTexture
    {
        rhi::RhiTexture* texture = nullptr;
        bool isSwapchain = false;
    };

    /// Converts graph-owned physical properties to the common RHI descriptor.
    [[nodiscard]] rhi::RhiTextureDesc BuildRhiTextureDesc(const FrameGraphTextureDesc& desc, const char* debugName) noexcept;
} // namespace ve
