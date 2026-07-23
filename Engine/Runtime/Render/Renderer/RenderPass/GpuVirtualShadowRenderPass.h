#pragma once

#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

namespace ve
{
    class FrameGraphPassResources;
    class RTShaderResource;
    struct ShaderID;

    /// GPU-driven directional virtual-shadow pipeline: receiver depth, page marking/allocation, page rendering, and finalization.
    class GpuVirtualShadowRenderPass final : public RenderPass
    {
    public:
        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

    private:
        [[nodiscard]] ErrorCode DrawReceiverDepth(RenderPassContext& context);
        [[nodiscard]] ErrorCode DispatchClear(const FrameGraphPassResources& resources,
                                              FrameGraphBufferHandle pageMarks,
                                              FrameGraphBufferHandle pageTable,
                                              FrameGraphBufferHandle requestCounts,
                                              FrameGraphBufferHandle physicalPages,
                                              RenderPassContext& context);
        [[nodiscard]] ErrorCode DispatchMark(const FrameGraphPassResources& resources,
                                             FrameGraphTextureHandle sceneDepth,
                                             FrameGraphBufferHandle pageMarks,
                                             RenderPassContext& context);
        [[nodiscard]] ErrorCode DispatchCompact(const FrameGraphPassResources& resources,
                                                FrameGraphBufferHandle pageMarks,
                                                FrameGraphBufferHandle requestList,
                                                FrameGraphBufferHandle requestCounts,
                                                RenderPassContext& context);
        [[nodiscard]] ErrorCode DispatchResolvePageHits(const FrameGraphPassResources& resources,
                                                        FrameGraphBufferHandle requestList,
                                                        FrameGraphBufferHandle requestCounts,
                                                        FrameGraphBufferHandle pageTable,
                                                        FrameGraphBufferHandle physicalPages,
                                                        UInt32 level,
                                                        RenderPassContext& context);
        [[nodiscard]] ErrorCode DispatchAllocate(const FrameGraphPassResources& resources,
                                                 FrameGraphBufferHandle requestList,
                                                 FrameGraphBufferHandle requestCounts,
                                                 FrameGraphBufferHandle pageTable,
                                                 FrameGraphBufferHandle physicalPages,
                                                 UInt32 level,
                                                 RenderPassContext& context);
        [[nodiscard]] ErrorCode DrawPages(const FrameGraphPassResources& resources, FrameGraphBufferHandle physicalPages, RenderPassContext& context);
        [[nodiscard]] ErrorCode DispatchFinalize(const FrameGraphPassResources& resources, FrameGraphBufferHandle physicalPages, RenderPassContext& context);

        [[nodiscard]] rhi::RhiPipelineState* GetReceiverDepthPipeline(RenderPassContext& context, RTShaderResource& shaderResource);
        [[nodiscard]] rhi::RhiPipelineState* GetPageClearPipeline(RenderPassContext& context);
        [[nodiscard]] rhi::RhiPipelineState* GetPageCasterPipeline(RenderPassContext& context);
        [[nodiscard]] rhi::RhiComputePipelineState* GetComputePipeline(
            RenderPassContext& context, const char* name, ShaderID shaderId, const char* source, const rhi::RhiPipelineResourceLayoutDesc& layout);
    };
} // namespace ve
