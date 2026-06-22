#include "Engine/Runtime/Render/BaseRenderer.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderPass/OpaqueSceneRenderPass.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] std::shared_ptr<RTCamera> FindFrameCamera(const RTScene& scene) noexcept
        {
            std::shared_ptr<RTCamera> fallback;
            for (SizeT cameraIndex = 0; cameraIndex < scene.GetCameraCount(); ++cameraIndex)
            {
                std::shared_ptr<RTCamera> camera = scene.GetCamera(cameraIndex);
                if (camera == nullptr)
                {
                    continue;
                }

                if (fallback == nullptr)
                {
                    fallback = camera;
                }

                if (camera->GetDesc().primary)
                {
                    return camera;
                }
            }

            return fallback;
        }

        [[nodiscard]] rhi::RhiColor ResolveFrameClearColor(const std::shared_ptr<RTScene>& scene) noexcept
        {
            if (scene == nullptr)
            {
                return rhi::RhiColor{0.05f, 0.07f, 0.10f, 1.0f};
            }

            const std::shared_ptr<RTCamera> camera = FindFrameCamera(*scene);
            return camera != nullptr ? camera->GetDesc().clearColor : rhi::RhiColor{0.05f, 0.07f, 0.10f, 1.0f};
        }

        void ValidateRenderPasses(const std::vector<std::unique_ptr<RenderPass>>& passes)
        {
            for (const std::unique_ptr<RenderPass>& pass : passes)
            {
                VE_ASSERT_MESSAGE(pass != nullptr, "Renderer construction requires valid render passes.");
            }
        }

    } // namespace

    BaseRendererInitParam ForwardRendererInitParam::TakeBaseInitParam() &&
    {
        if (addOpaquePass)
        {
            OpaqueSceneRenderPassInitParam passInitParam = {};
            passInitParam.target = std::move(target);
            passInitParam.fillMode = fillMode;
            passes.insert(passes.begin(), std::make_unique<OpaqueSceneRenderPass>(std::move(passInitParam)));
        }

        ValidateRenderPasses(passes);

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

        rendererData_.scene = std::move(initParam.scene);
        rendererData_.camera = initParam.camera != nullptr || rendererData_.scene == nullptr ? std::move(initParam.camera) : FindFrameCamera(*rendererData_.scene);
        rendererData_.clearColor =
            rendererData_.camera != nullptr ? rendererData_.camera->GetDesc().clearColor : ResolveFrameClearColor(rendererData_.scene);
        rendererData_.activeRenderPassIndex = 0;
        rendererData_.active = false;
        rendererData_.renderPassOpen = false;
    }

    void BaseRenderer::RenderScene()
    {
        VE_ASSERT_RENDER_THREAD();

        BeginSceneRender();
        ExecutePassesInOrder();
        EndSceneRender();
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

    void BaseRenderer::BeginSceneRender()
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT_MESSAGE(!rendererData_.active, "BaseRenderer::BeginSceneRender requires no active renderer.");
        VE_ASSERT(frameRenderData_ != nullptr);
        VE_ASSERT(frameRenderData_->device != nullptr);
        VE_ASSERT(frameRenderData_->commandList != nullptr);
        VE_ASSERT(frameRenderData_->mainSwapchain != nullptr);
        VE_ASSERT(frameRenderData_->shaderManager != nullptr);

        UpdateRenderWorld();
        BuildVisibleDrawLists();

        rendererData_.activeRenderPassIndex = 0;
        rendererData_.active = true;
        rendererData_.renderPassOpen = false;
    }

    void BaseRenderer::ExecutePassesInOrder()
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT_MESSAGE(rendererData_.active, "BaseRenderer::ExecutePassesInOrder requires an active renderer.");
        VE_ASSERT_MESSAGE(frameRenderData_ != nullptr, "BaseRenderer::ExecutePassesInOrder requires active frame data.");
        VE_ASSERT_MESSAGE(frameRenderData_->commandList != nullptr, "BaseRenderer::ExecutePassesInOrder requires an active command list.");

        while (rendererData_.activeRenderPassIndex < passes_.size())
        {
            std::unique_ptr<RenderPass>& pass = passes_[rendererData_.activeRenderPassIndex];
            VE_ASSERT(pass != nullptr);

            RenderPassData passData = BuildPassData(*pass);
            if (!BeginPass(passData))
            {
                return;
            }

            RenderPassContext passContext(RenderPassContextInitParam{*frameRenderData_, rendererData_, passData});
            pass->Execute(passContext);

            frameRenderData_->commandList->EndRenderPass();
            rendererData_.renderPassOpen = false;
            ++rendererData_.activeRenderPassIndex;
        }
    }

    void BaseRenderer::EndSceneRender()
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT_MESSAGE(rendererData_.active, "BaseRenderer::EndSceneRender requires an active scene render.");
        VE_ASSERT(frameRenderData_ != nullptr);
        VE_ASSERT(frameRenderData_->commandList != nullptr);

        if (rendererData_.renderPassOpen)
        {
            frameRenderData_->commandList->EndRenderPass();
            rendererData_.renderPassOpen = false;
        }

        rendererData_.activeRenderPassIndex = 0;
        rendererData_.active = false;
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
        VE_ASSERT(rendererData_.active);
        VE_ASSERT(frameRenderData_ != nullptr);
        VE_ASSERT(frameRenderData_->commandList != nullptr);
        VE_ASSERT(frameRenderData_->mainSwapchain != nullptr);
        VE_ASSERT(rendererData_.activeRenderPassIndex < passes_.size());
        VE_ASSERT(!rendererData_.renderPassOpen);

        const bool beganRenderPass = frameRenderData_->commandList->BeginRenderPass(*frameRenderData_->mainSwapchain, passData.renderPassDesc);
        VE_ASSERT_MESSAGE(beganRenderPass, "BaseRenderer failed to begin render pass.");
        if (!beganRenderPass)
        {
            return false;
        }

        frameRenderData_->commandList->SetViewport(passData.viewport);
        frameRenderData_->commandList->SetScissor(passData.scissorRect);
        rendererData_.renderPassOpen = true;
        return true;
    }

    ForwardRenderer::ForwardRenderer(ForwardRendererInitParam initParam)
        : BaseRenderer(std::move(initParam).TakeBaseInitParam())
    {
    }
} // namespace ve
