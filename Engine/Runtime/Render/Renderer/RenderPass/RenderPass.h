#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderViewFamily.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowFrameGraph.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.h"

#include <memory>
#include <unordered_set>
#include <vector>

namespace ve
{
    class FrameGraph;
    class RTCamera;
    class RTRenderItem;
    class RTRenderViewState;
    class RTScene;

    struct RendererViewData
    {
        RenderView view;
        VirtualShadowSamplingSnapshot virtualShadowSampling;
        std::vector<std::shared_ptr<RTRenderItem>> transparentItems;
    };

    /// Renderer-owned scene choices and queue lists for one render view family.
    struct RendererData
    {
        std::shared_ptr<RTScene> scene;
        std::vector<std::shared_ptr<RTRenderItem>> opaqueItems;
        std::unordered_set<UInt64> virtualShadowDirtyCasterIDs;
        std::vector<RendererViewData> views;
    };

    struct RendererViewFrameGraphData
    {
        FrameGraphTextureHandle color;
        FrameGraphTextureHandle depth;
        FrameGraphBufferHandle virtualShadowPageTable;
        VirtualShadowSamplingSnapshot virtualShadowSampling;
    };

    /// The current logical attachment versions shared while renderer passes register themselves.
    struct RendererFrameGraphData
    {
        std::vector<RendererViewFrameGraphData> views;
        FrameGraphTextureHandle virtualShadowAtlas;
        FrameGraphBufferHandle virtualShadowPhysicalPages;
        FrameGraphBufferHandle virtualShadowStatistics;
        VirtualShadowPreparedFamilyGraphData virtualShadowPreparedFamily;
        VirtualShadowFrameGraphResources virtualShadowResources;
        FrameGraphTextureHandle swapchainColor;
    };

    /// Logical raster state exposed to draw code after FrameGraph has resolved the native attachments.
    struct RenderPassExecutionInfo
    {
        rhi::RhiRenderArea renderArea = {};
        rhi::RhiFormat colorFormat = rhi::RhiFormat::Unknown;
        UInt32 colorAttachmentCount = 0;
        bool depthEnabled = false;
        bool depthReadOnly = false;
    };

    struct RenderPassContextInitParam
    {
        const FrameRenderPipelineData& frameData;
        const RendererData& rendererData;
        const RenderPassExecutionInfo& executionInfo;
    };

    /// Draw-time facade supplied after the frame graph has resolved and begun the native render pass.
    class RenderPassContext final : public NonCopyable
    {
    public:
        explicit RenderPassContext(RenderPassContextInitParam initParam) noexcept;

        const FrameRenderPipelineData& frameData;
        const RendererData& rendererData;
        const RenderPassExecutionInfo& executionInfo;
        rhi::RhiDevice& device;
        rhi::RhiCommandList& commandList;

        [[nodiscard]] const RendererViewData& GetView(UInt32 viewIndex) const noexcept;
    };

    /// Long-lived renderer pass that declares one or more nodes in a frame graph.
    class RenderPass : public NonCopyable
    {
    public:
        RenderPass() = default;
        virtual ~RenderPass() = default;

        virtual void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) = 0;
    };

    class ViewRenderPass : public NonCopyable
    {
    public:
        virtual ~ViewRenderPass() = default;

        virtual void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex) = 0;
    };

} // namespace ve
