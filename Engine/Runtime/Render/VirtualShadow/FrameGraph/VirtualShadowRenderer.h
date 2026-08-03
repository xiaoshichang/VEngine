#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h"

#include <span>

namespace ve
{
    struct UniformBufferAllocation;
    namespace rhi
    {
        class RhiCommandList;
    }

    struct VirtualShadowSamplingSnapshot
    {
        VirtualShadowGpuConstants constants;
        rhi::RhiTexture* atlas = nullptr;
        rhi::RhiBuffer* pageTable = nullptr;
        UInt64 pageTableOffset = 0;
        UInt64 pageTableSize = 0;
    };

    namespace virtual_shadow_detail
    {
        [[nodiscard]] bool IsValidVirtualShadowUniformAllocation(const UniformBufferAllocation& allocation) noexcept;
    }

    [[nodiscard]] VirtualShadowSamplingSnapshot BuildVirtualShadowSamplingSnapshot(const VirtualShadowViewResult& result) noexcept;
    [[nodiscard]] rhi::RhiTexture* SelectVirtualShadowFamilyAtlas(std::span<const VirtualShadowSamplingSnapshot> snapshots) noexcept;
    void BindVirtualShadowSampling(rhi::RhiCommandList& commandList, const VirtualShadowSamplingSnapshot& snapshot, const UniformBufferAllocation& constants);
} // namespace ve
