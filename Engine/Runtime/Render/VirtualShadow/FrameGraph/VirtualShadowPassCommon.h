#pragma once

#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Render/RenderUniformBuffer.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowFrameGraph.h"

#include <memory>
#include <span>

namespace ve
{
    struct FrameRenderPipelineData;
    class RTRenderItem;
    class RenderPassContext;
    class FrameGraphPassResources;

    namespace rhi
    {
        class RhiBuffer;
        class RhiComputePipelineState;
        class RhiTexture;
        struct RhiPipelineResourceBindingDesc;
    } // namespace rhi

    struct VirtualShadowRequestRecordingContext
    {
        const FrameRenderPipelineData& frameData;
        const VirtualShadowFramePacket& packet;
        VirtualShadowPageTableSlice slice;
        UInt32 physicalPageCapacity = 0;
    };

    struct VirtualShadowPageRecordingContext
    {
        const FrameRenderPipelineData& frameData;
        const VirtualShadowFramePacket& packet;
        VirtualShadowPageTableSlice slice;
        UInt32 physicalPageCapacity = 0;
        std::span<const std::shared_ptr<RTRenderItem>> opaqueItems;
        VirtualShadowViewDrawDiagnostics* drawDiagnostics = nullptr;
    };

    namespace virtual_shadow_detail
    {
        struct VirtualShadowViewRequestPassData
        {
            UInt32 viewIndex = 0;
            VirtualShadowFramePacket packet;
            VirtualShadowPageTableSlice slice;
            UInt32 physicalPageCapacity = 0;
            FrameGraphTextureHandle receiverDepth;
            FrameGraphBufferHandle pageMarks;
            FrameGraphBufferHandle pageTable;
            FrameGraphBufferHandle requestList;
            FrameGraphBufferHandle requestCounts;
            FrameGraphBufferHandle physicalPages;
            FrameGraphBufferHandle statistics;
        };

        struct VirtualShadowResidencyPassData
        {
            UInt32 viewIndex = 0;
            UInt32 physicalPageCapacity = 0;
            bool countPersistedRequests = false;
            VirtualShadowFramePacket packet;
            VirtualShadowPageTableSlice slice;
            FrameGraphBufferHandle pageTable;
            FrameGraphBufferHandle requestList;
            FrameGraphBufferHandle requestCounts;
            FrameGraphBufferHandle physicalPages;
            FrameGraphBufferHandle statistics;
        };

        struct VirtualShadowViewPagePassData
        {
            UInt32 viewIndex = 0;
            UInt32 physicalPageCapacity = 0;
            VirtualShadowFramePacket packet;
            VirtualShadowPageTableSlice slice;
            FrameGraphTextureHandle atlas;
            FrameGraphBufferHandle pageTable;
            FrameGraphBufferHandle physicalPages;
            FrameGraphBufferHandle statistics;
            VirtualShadowViewDrawDiagnostics* drawDiagnostics = nullptr;
        };

        [[nodiscard]] rhi::RhiComputePipelineState* GetVirtualShadowComputePipeline(const FrameRenderPipelineData& frameData,
                                                                                    const char* name,
                                                                                    const rhi::RhiPipelineResourceBindingDesc* bindings,
                                                                                    UInt32 bindingCount);

        [[nodiscard]] UniformBufferAllocation UploadVirtualShadowPassConstants(const FrameRenderPipelineData& frameData,
                                                                               const VirtualShadowGpuConstants& constants);

        [[nodiscard]] VirtualShadowRequestRecordingContext BuildVirtualShadowRequestRecordingContext(const FrameRenderPipelineData& frameData,
                                                                                                     const VirtualShadowFramePacket& packet,
                                                                                                     VirtualShadowPageTableSlice slice,
                                                                                                     UInt32 physicalPageCapacity) noexcept;

        [[nodiscard]] VirtualShadowGpuConstants BuildVirtualShadowRequestGpuConstants(const VirtualShadowRequestRecordingContext& context,
                                                                                      UInt32 passLevel = 0) noexcept;

        [[nodiscard]] VirtualShadowGpuConstants BuildVirtualShadowPageGpuConstants(const VirtualShadowPageRecordingContext& context) noexcept;

        [[nodiscard]] VirtualShadowRequestRecordingContext BuildVirtualShadowRequestPassContext(const VirtualShadowViewRequestPassData& data,
                                                                                                const RenderPassContext& context);

        [[nodiscard]] VirtualShadowRequestRecordingContext BuildVirtualShadowResidencyPassContext(const VirtualShadowResidencyPassData& data,
                                                                                                  const RenderPassContext& context);

        [[nodiscard]] VirtualShadowPageRecordingContext BuildVirtualShadowPagePassContext(const VirtualShadowViewPagePassData& data,
                                                                                          const RenderPassContext& context);

        [[nodiscard]] UInt32 GetVirtualShadowPhysicalPageDispatchGroupCount(UInt32 physicalPageCapacity) noexcept;

        [[nodiscard]] rhi::RhiBuffer& RequireVirtualShadowBuffer(const FrameGraphPassResources& resources, FrameGraphBufferHandle handle);

        [[nodiscard]] rhi::RhiTexture& RequireVirtualShadowTexture(const FrameGraphPassResources& resources, FrameGraphTextureHandle handle);

        void ValidateVirtualShadowRequestView(const VirtualShadowPreparedViewGraphData& view, bool requireReceiverDepth);
        void ValidateVirtualShadowResidencyView(const VirtualShadowPreparedViewGraphData& view);
        void ValidateVirtualShadowPageView(const VirtualShadowPreparedViewGraphData& view, const VirtualShadowFrameGraphResources& resources);
        void ValidateVirtualShadowPhysicalCapacity(UInt32 physicalPageCapacity);
    } // namespace virtual_shadow_detail
} // namespace ve
