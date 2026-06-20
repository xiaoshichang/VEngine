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

    } // namespace

    void BaseRenderer::RenderScene(const FrameRenderPipelineData& frameData)
    {
        VE_ASSERT_RENDER_THREAD();

        BeginSceneRender(frameData);
        ExecutePassesInOrder();
        EndSceneRender();
    }

    bool BaseRenderer::IsFrameActive() const noexcept
    {
        return rendererData_.active;
    }

    const RendererData& BaseRenderer::GetRendererData() const noexcept
    {
        return rendererData_;
    }

    void BaseRenderer::SetScene(std::shared_ptr<RTScene> scene) noexcept
    {
        scene_ = std::move(scene);
    }

    void BaseRenderer::SetOverrideCamera(std::shared_ptr<RTCamera> camera) noexcept
    {
        overrideCamera_ = std::move(camera);
    }

    void BaseRenderer::SetFillMode(rhi::RhiFillMode fillMode) noexcept
    {
        fillMode_ = fillMode;
    }

    std::shared_ptr<RTScene> BaseRenderer::GetScene() const noexcept
    {
        return scene_;
    }

    void BaseRenderer::AddRenderPass(std::unique_ptr<RenderPass> pass)
    {
        VE_ASSERT_MESSAGE(!rendererData_.active, "BaseRenderer::AddRenderPass requires no active renderer.");
        VE_ASSERT_MESSAGE(pass != nullptr, "BaseRenderer::AddRenderPass requires a valid pass.");
        passes_.push_back(std::move(pass));
    }

    void BaseRenderer::ClearRenderPasses() noexcept
    {
        VE_ASSERT_MESSAGE(!rendererData_.active, "BaseRenderer::ClearRenderPasses requires no active renderer.");
        passes_.clear();
    }

    void BaseRenderer::RefreshRendererSceneData() noexcept
    {
        rendererData_.scene = scene_;
        rendererData_.camera = overrideCamera_ != nullptr || scene_ == nullptr ? overrideCamera_ : FindFrameCamera(*scene_);
        rendererData_.clearColor = rendererData_.camera != nullptr ? rendererData_.camera->GetDesc().clearColor : ResolveFrameClearColor(scene_);
        rendererData_.activeRenderPassIndex = 0;
        rendererData_.active = false;
        rendererData_.renderPassOpen = false;
    }

    void BaseRenderer::UpdateRenderWorld()
    {
        // Render world updates will consume scene snapshots here once the render proxy layer grows.
    }

    void BaseRenderer::BuildVisibleDrawLists()
    {
        // Visibility and batching stay here so concrete renderers only choose their pass topology.
    }

    void BaseRenderer::BeginSceneRender(const FrameRenderPipelineData& frameData)
    {
        VE_ASSERT_MESSAGE(!rendererData_.active, "BaseRenderer::BeginSceneRender requires no active renderer.");
        VE_ASSERT(frameData.device != nullptr);
        VE_ASSERT(frameData.commandList != nullptr);
        VE_ASSERT(frameData.mainSwapchain != nullptr);
        VE_ASSERT(frameData.shaderManager != nullptr);

        frameRenderData_ = &frameData;
        RefreshRendererSceneData();
        UpdateRenderWorld();
        BuildVisibleDrawLists();

        rendererData_.activeRenderPassIndex = 0;
        rendererData_.active = true;
        rendererData_.renderPassOpen = false;
    }

    void BaseRenderer::ExecutePassesInOrder()
    {
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

            RenderPassContext passContext(*frameRenderData_, *this, passData);
            pass->Execute(passContext);

            frameRenderData_->commandList->EndRenderPass();
            rendererData_.renderPassOpen = false;
            ++rendererData_.activeRenderPassIndex;
        }
    }

    void BaseRenderer::EndSceneRender()
    {
        VE_ASSERT_MESSAGE(rendererData_.active, "BaseRenderer::EndSceneRender requires an active scene render.");
        VE_ASSERT(frameRenderData_ != nullptr);
        VE_ASSERT(frameRenderData_->commandList != nullptr);

        if (rendererData_.renderPassOpen)
        {
            frameRenderData_->commandList->EndRenderPass();
            rendererData_.renderPassOpen = false;
        }

        frameRenderData_ = nullptr;
        rendererData_.activeRenderPassIndex = 0;
        rendererData_.active = false;
    }

    RenderPassData BaseRenderer::BuildPassData(RenderPass& pass)
    {
        VE_ASSERT(frameRenderData_ != nullptr);
        RenderPassBuilder builder;
        builder.Reset(pass.GetName(), *frameRenderData_, rendererData_);
        pass.Setup(builder);

        RenderPassData passData = {};
        passData.renderPassDesc = builder.GetRenderPassDesc();
        passData.viewport = builder.GetViewport();
        passData.scissorRect = builder.GetScissor();
        return passData;
    }

    bool BaseRenderer::BeginPass(const RenderPassData& passData)
    {
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

    ForwardRenderer::ForwardRenderer(ForwardRendererDesc desc)
    {
        SetScene(std::move(desc.scene));
        SetOverrideCamera(std::move(desc.camera));
        SetFillMode(desc.fillMode);
        if (desc.addOpaquePass)
        {
            AddRenderPass(std::make_unique<OpaqueSceneRenderPass>(std::move(desc.target), desc.fillMode));
        }

        for (std::unique_ptr<RenderPass>& pass : desc.additionalPasses)
        {
            AddRenderPass(std::move(pass));
        }
    }
} // namespace ve
