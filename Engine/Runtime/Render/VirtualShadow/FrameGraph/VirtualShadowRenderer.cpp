#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.h"

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Render/RenderUniformBuffer.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"

namespace ve
{
    namespace
    {
        [[nodiscard]] bool IsValidBufferRange(const rhi::RhiBuffer* buffer, UInt64 offset, UInt64 size) noexcept
        {
            return buffer != nullptr && size != 0 && offset <= buffer->GetSize() && size <= buffer->GetSize() - offset;
        }

        [[nodiscard]] bool IsValidVirtualShadowAtlas(const rhi::RhiTexture* texture, UInt32 width, UInt32 height) noexcept
        {
            return texture != nullptr && texture->GetDimension() == rhi::RhiTextureDimension::Texture2D && texture->GetFormat() == rhi::RhiFormat::R32Uint &&
                   texture->GetWidth() == width && texture->GetHeight() == height;
        }

        void RequireVirtualShadowSampling(bool condition, const char* message)
        {
            if (!condition)
            {
                FailVirtualShadow(message);
            }
        }
    } // namespace

    VirtualShadowSamplingSnapshot BuildVirtualShadowSamplingSnapshot(const VirtualShadowViewResult& result) noexcept
    {
        RequireVirtualShadowSampling(result.packet.valid, "VSM sampling requires a valid prepared packet.");
        RequireVirtualShadowSampling(result.packet.clipmaps.valid, "VSM sampling requires valid clipmaps.");
        RequireVirtualShadowSampling(result.packet.atlasExtent != 0, "VSM sampling requires a non-zero atlas extent.");
        RequireVirtualShadowSampling(IsValidVirtualShadowAtlas(result.atlas, result.packet.atlasExtent, result.packet.atlasExtent),
                                     "VSM sampling requires the real scene atlas with the prepared extent.");
        RequireVirtualShadowSampling(result.pageTableOffset % sizeof(UInt32) == 0, "VSM sampling requires an aligned page-table slice.");
        RequireVirtualShadowSampling(result.pageTableSize == VirtualShadowLogicalPageBufferSize,
                                     "VSM sampling requires one complete logical page-table slice.");
        RequireVirtualShadowSampling(IsValidBufferRange(result.pageTable, result.pageTableOffset, result.pageTableSize),
                                     "VSM sampling page-table slice exceeds its real scene buffer.");

        VirtualShadowSamplingSnapshot snapshot;
        snapshot.constants = BuildVirtualShadowGpuConstants(result.packet);
        snapshot.atlas = result.atlas;
        snapshot.pageTable = result.pageTable;
        snapshot.pageTableOffset = result.pageTableOffset;
        snapshot.pageTableSize = result.pageTableSize;
        return snapshot;
    }

    rhi::RhiTexture* SelectVirtualShadowFamilyAtlas(std::span<const VirtualShadowSamplingSnapshot> snapshots) noexcept
    {
        RequireVirtualShadowSampling(!snapshots.empty(), "VSM family atlas selection requires at least one view.");

        rhi::RhiTexture* atlas = nullptr;
        for (const VirtualShadowSamplingSnapshot& snapshot : snapshots)
        {
            RequireVirtualShadowSampling(snapshot.atlas != nullptr && snapshot.pageTable != nullptr,
                                         "VSM family atlas selection requires complete real bindings for every view.");
            if (atlas == nullptr)
            {
                atlas = snapshot.atlas;
            }
            RequireVirtualShadowSampling(atlas == snapshot.atlas, "VSM family views must share one physical atlas.");
        }
        return atlas;
    }

    void BindVirtualShadowSampling(rhi::RhiCommandList& commandList, const VirtualShadowSamplingSnapshot& snapshot, const UniformBufferAllocation& constants)
    {
        RequireVirtualShadowSampling(snapshot.atlas != nullptr, "VSM binding requires the real scene atlas.");
        RequireVirtualShadowSampling(IsValidBufferRange(snapshot.pageTable, snapshot.pageTableOffset, snapshot.pageTableSize),
                                     "VSM binding requires a valid real page-table slice.");
        RequireVirtualShadowSampling(snapshot.pageTableSize == VirtualShadowLogicalPageBufferSize,
                                     "VSM binding requires one complete logical page-table slice.");
        RequireVirtualShadowSampling(virtual_shadow_detail::IsValidVirtualShadowUniformAllocation(constants), "VSM binding requires uploaded GPU constants.");

        commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 4, *constants.buffer, constants.offset, constants.size);
        commandList.SetTexture(rhi::RhiShaderStage::Fragment, 1, *snapshot.atlas);
        commandList.SetStorageBuffer(rhi::RhiShaderStage::Fragment, 5, *snapshot.pageTable, snapshot.pageTableOffset, snapshot.pageTableSize);
    }

    bool virtual_shadow_detail::IsValidVirtualShadowUniformAllocation(const UniformBufferAllocation& allocation) noexcept
    {
        constexpr UInt64 RequiredSize = sizeof(VirtualShadowGpuConstants);
        if (allocation.buffer == nullptr || allocation.size < RequiredSize)
        {
            return false;
        }
        const UInt64 bufferSize = allocation.buffer->GetSize();
        return allocation.offset <= bufferSize && RequiredSize <= bufferSize - allocation.offset && allocation.size <= bufferSize - allocation.offset;
    }
} // namespace ve
