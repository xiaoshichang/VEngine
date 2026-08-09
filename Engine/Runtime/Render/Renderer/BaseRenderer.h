#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderViewFamily.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <memory>
#include <string>
#include <vector>

namespace ve
{
    class FrameGraph;

    struct BaseRendererInitParam
    {
        const FrameRenderPipelineData* frameData = nullptr;
        RenderViewFamily viewFamily;
        std::vector<std::unique_ptr<RenderPass>> outputPasses;
    };

    struct RendererViewPassExtension
    {
        UInt32 viewIndex = 0;
        std::vector<std::unique_ptr<ViewRenderPass>> passes;
    };

    /// Rebuilds the shared opaque queue and per-view transparent queues for one renderer family.
    void BuildRendererQueues(RendererData& rendererData);

    /// Builds one render-view family's queues and executes the concrete renderer topology through one compiled frame graph.
    class BaseRenderer : public NonCopyable
    {
    public:
        virtual ~BaseRenderer() = default;

        void Render();
        [[nodiscard]] const std::vector<FrameGraphPassDiagnostics>& GetLastFrameGraphPassDiagnostics() const noexcept;
        [[nodiscard]] const std::vector<std::string>& GetLastFrameGraphExecutionPassNames() const noexcept;

    protected:
        explicit BaseRenderer(BaseRendererInitParam initParam);

        virtual void BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) = 0;
        [[nodiscard]] const RendererData& GetRendererData() const noexcept;
        /// Mutable family data is exposed only so graph registrars can publish immutable frame-local sampling snapshots.
        [[nodiscard]] RendererData& GetMutableRendererData() noexcept;
        [[nodiscard]] const FrameRenderPipelineData& GetFrameData() const noexcept;

    private:
        void UpdateRenderWorld();
        void RetainInFlightGpuFrameObjects() const;
        void ImportViewRenderTargets(FrameGraph& frameGraph, UInt32 viewIndex, RendererViewFrameGraphData& graphData) const;
        void ImportMainSwapchainColor(FrameGraph& frameGraph, RendererFrameGraphData& graphData) const;

        RendererData rendererData_ = {};
        std::vector<std::unique_ptr<RenderPass>> outputPasses_;
        std::vector<FrameGraphPassDiagnostics> lastFrameGraphPassDiagnostics_;
        std::vector<std::string> lastFrameGraphExecutionPassNames_;
        const FrameRenderPipelineData* frameRenderData_ = nullptr;
    };
} // namespace ve
