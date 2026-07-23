#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"

#include <memory>
#include <vector>

namespace ve
{
    class FrameGraph;
    class RTCamera;
    class RTRenderItem;
    class RTRenderViewState;
    class RTScene;
    struct VirtualShadowFramePacket;

    /// Renderer-owned scene choices and the queue lists built once for one view.
    struct RendererData
    {
        std::shared_ptr<RTScene> scene;
        std::shared_ptr<RTCamera> resolvedCamera;
        std::shared_ptr<RTRenderViewState> viewState;
        std::shared_ptr<VirtualShadowFramePacket> virtualShadowPacket;
        std::vector<std::shared_ptr<RTRenderItem>> opaqueItems;
        std::vector<std::shared_ptr<RTRenderItem>> transparentItems;
    };

    /// The current logical attachment versions shared while renderer passes register themselves.
    struct RendererFrameGraphData
    {
        FrameGraphTextureHandle color;
        FrameGraphTextureHandle depth;
        FrameGraphTextureHandle virtualShadowAtlas;
        FrameGraphBufferHandle virtualShadowPageMarks;
        FrameGraphBufferHandle virtualShadowPageTable;
        FrameGraphBufferHandle virtualShadowRequestList;
        FrameGraphBufferHandle virtualShadowRequestCounts;
        FrameGraphBufferHandle virtualShadowPhysicalPages;
        bool depthPrepassReady = false;
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
    };

    /// Long-lived renderer pass that declares one or more nodes in a frame graph.
    class RenderPass : public NonCopyable
    {
    public:
        RenderPass() = default;
        virtual ~RenderPass() = default;

        virtual void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) = 0;
    };

} // namespace ve
