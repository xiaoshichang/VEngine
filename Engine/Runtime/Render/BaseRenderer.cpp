#include "Engine/Runtime/Render/BaseRenderer.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderPass/OpaqueSceneRenderPass.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <utility>

namespace ve
{

    BaseRendererInitParam ForwardRendererInitParam::TakeBaseInitParam() &&
    {
        if (addOpaquePass)
        {
            OpaqueSceneRenderPassInitParam passInitParam = {};
            passInitParam.target = std::move(target);
            passInitParam.fillMode = fillMode;
            passes.insert(passes.begin(), std::make_unique<OpaqueSceneRenderPass>(std::move(passInitParam)));
        }

        BaseRendererInitParam baseInitParam = {};
        baseInitParam.frameData = frameData;
        baseInitParam.scene = std::move(scene);
        baseInitParam.camera = std::move(camera);
        baseInitParam.passes = std::move(passes);
        return baseInitParam;
    }

    BaseRenderer::BaseRenderer(BaseRendererInitParam initParam)
        : passes_(std::move(initParam.passes))
        , frameRenderData_(initParam.frameData)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT_MESSAGE(frameRenderData_ != nullptr, "BaseRenderer construction requires frame data.");
        VE_ASSERT(initParam.scene != nullptr);

        rendererData_.scene = std::move(initParam.scene);
        rendererData_.resolvedCamera = std::move(initParam.camera);
    }

    void BaseRenderer::RenderScene()
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(frameRenderData_ != nullptr);
        VE_ASSERT(frameRenderData_->device != nullptr);
        VE_ASSERT(frameRenderData_->frameContext != nullptr);
        VE_ASSERT(frameRenderData_->mainSwapchain != nullptr);
        VE_ASSERT(frameRenderData_->shaderManager != nullptr);
        UpdateRenderWorld();
        BuildVisibleDrawLists();
        ExecutePassesInOrder();
    }

    void BaseRenderer::UpdateRenderWorld()
    {
        VE_ASSERT_RENDER_THREAD();
        // Render world updates will consume scene snapshots here once the render proxy layer grows.
    }

    void BaseRenderer::BuildVisibleDrawLists()
    {
        VE_ASSERT_RENDER_THREAD();
        // Visibility and batching stay here so concrete renderers only choose their pass topology.
    }

    void BaseRenderer::ExecutePassesInOrder()
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT_MESSAGE(frameRenderData_ != nullptr, "BaseRenderer::ExecutePassesInOrder requires active frame data.");
        rhi::RhiCommandList& commandList = frameRenderData_->GetCommandList();

        for (SizeT passIndex = 0; passIndex < passes_.size(); ++passIndex)
        {
            std::unique_ptr<RenderPass>& pass = passes_[passIndex];
            VE_ASSERT(pass != nullptr);

            RenderPassData passData = BuildPassData(*pass);
            if (!BeginPass(passData))
            {
                return;
            }

            RenderPassContext passContext(RenderPassContextInitParam{*frameRenderData_, rendererData_, passData});
            pass->Execute(passContext);

            commandList.EndRenderPass();
        }
    }

    RenderPassData BaseRenderer::BuildPassData(RenderPass& pass)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(frameRenderData_ != nullptr);
        RenderPassBuilder builder(RenderPassBuilderInitParam{pass.GetName(), *frameRenderData_, rendererData_});
        pass.Setup(builder);
        return builder.Build();
    }

    bool BaseRenderer::BeginPass(const RenderPassData& passData)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(frameRenderData_ != nullptr);
        VE_ASSERT(frameRenderData_->mainSwapchain != nullptr);
        rhi::RhiCommandList& commandList = frameRenderData_->GetCommandList();

        const bool beganRenderPass = commandList.BeginRenderPass(*frameRenderData_->mainSwapchain, passData.renderPassDesc);
        VE_ASSERT_MESSAGE(beganRenderPass, "BaseRenderer failed to begin render pass.");
        if (!beganRenderPass)
        {
            return false;
        }

        commandList.SetViewport(passData.viewport);
        commandList.SetScissor(passData.scissorRect);
        return true;
    }

    ForwardRenderer::ForwardRenderer(ForwardRendererInitParam initParam)
        : BaseRenderer(std::move(initParam).TakeBaseInitParam())
    {
    }
} // namespace ve
