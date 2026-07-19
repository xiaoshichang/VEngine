#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] rhi::RhiTextureUsage MakeColorTargetUsage() noexcept
        {
            return static_cast<rhi::RhiTextureUsage>(static_cast<UInt32>(rhi::RhiTextureUsage::Sampled) |
                                                     static_cast<UInt32>(rhi::RhiTextureUsage::RenderTarget));
        }

        [[nodiscard]] FrameGraphTextureDesc MakeTextureDesc(const rhi::RhiTexture& texture, rhi::RhiTextureUsage usage) noexcept
        {
            FrameGraphTextureDesc desc = {};
            desc.dimension = texture.GetDimension();
            desc.width = texture.GetWidth();
            desc.height = texture.GetHeight();
            desc.depth = 1;
            desc.mipLevelCount = 1;
            desc.format = texture.GetFormat();
            desc.usage = usage;
            return desc;
        }
    } // namespace

    BaseRenderer::BaseRenderer(BaseRendererInitParam initParam)
        : target_(std::move(initParam.target))
        , frameRenderData_(initParam.frameData)
    {
        VE_ASSERT_RENDER_THREAD();
        rendererData_.scene = std::move(initParam.scene);
        rendererData_.resolvedCamera = std::move(initParam.camera);
    }

    ErrorCode BaseRenderer::RenderScene()
    {
        VE_ASSERT_RENDER_THREAD();
        if (frameRenderData_ == nullptr || frameRenderData_->device == nullptr || frameRenderData_->frameContext == nullptr ||
            frameRenderData_->mainSwapchain == nullptr || frameRenderData_->shaderManager == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        PrepareRenderTargetForClear();
        UpdateRenderWorld();
        BuildRenderQueues();

        FrameGraph frameGraph(FrameGraphExecuteContext{*frameRenderData_, rendererData_});
        RendererFrameGraphData graphData = {};

        const ErrorCode setupResult = frameGraph.Setup(
            [this, &graphData](FrameGraph& setupGraph)
            {
                // Setup step 1: import the renderer-owned output attachments into the graph namespace.
                const ErrorCode importResult = ImportRenderTargets(setupGraph, graphData);
                if (importResult != ErrorCode::None)
                {
                    return importResult;
                }

                // Setup step 2: register the renderer topology and let each pass declare its resource accesses.
                if (rendererData_.scene != nullptr && rendererData_.resolvedCamera != nullptr)
                {
                    BuildFrameGraph(setupGraph, graphData);
                }

                // Setup step 3: declare the final color version as an externally observable graph result.
                setupGraph.Export(graphData.color);
                return ErrorCode::None;
            });
        if (setupResult != ErrorCode::None)
        {
            return setupResult;
        }

        Error compileResult = frameGraph.Compile();
        if (!compileResult.IsOk())
        {
            VE_LOG_ERROR("Frame graph compile failed: %s", compileResult.GetMessage().c_str());
            return compileResult.GetCode();
        }
        return frameGraph.Execute();
    }

    const RendererData& BaseRenderer::GetRendererData() const noexcept
    {
        return rendererData_;
    }

    void BaseRenderer::PrepareRenderTargetForClear()
    {
        VE_ASSERT_RENDER_THREAD();
        if (target_.colorLoadAction != rhi::RhiLoadAction::Clear || target_.colorTexture == nullptr || rendererData_.scene == nullptr ||
            rendererData_.resolvedCamera == nullptr)
        {
            return;
        }

        const rhi::RhiColor& clearColor = rendererData_.resolvedCamera->GetClearColor();
        if (target_.colorTexture->IsInitialized() && target_.colorTexture->GetDesc().optimizedClearColor == clearColor)
        {
            return;
        }

        RenderTextureDesc desc = target_.colorTexture->GetDesc();
        desc.optimizedClearColor = clearColor;

        std::vector<std::unique_ptr<rhi::RhiObject>> retiredResources;
        target_.colorTexture->InitRenderResource(*frameRenderData_->device, std::move(desc), retiredResources);
        for (std::unique_ptr<rhi::RhiObject>& resource : retiredResources)
        {
            frameRenderData_->RetainTransientResource(std::move(resource));
        }
    }

    void BaseRenderer::UpdateRenderWorld()
    {
        VE_ASSERT_RENDER_THREAD();
    }

    void BaseRenderer::BuildRenderQueues()
    {
        VE_ASSERT_RENDER_THREAD();
        rendererData_.opaqueItems.clear();
        rendererData_.transparentItems.clear();
        if (rendererData_.scene == nullptr)
        {
            return;
        }

        for (SizeT itemIndex = 0; itemIndex < rendererData_.scene->GetRenderItemCount(); ++itemIndex)
        {
            const std::shared_ptr<RTRenderItem> item = rendererData_.scene->GetRenderItem(itemIndex);
            if (item == nullptr)
            {
                VE_LOG_WARN("Renderer queue build skipped null render item at scene index %zu.", itemIndex);
                continue;
            }

            const auto material = std::dynamic_pointer_cast<RTMaterialResource>(item->GetMaterialResource());
            if (material == nullptr)
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Renderer queue build requires a material resource.");
                VE_LOG_WARN("Renderer queue build skipped item at scene index %zu because its material is missing.", itemIndex);
                continue;
            }

            switch (material->GetDesc().renderQueue)
            {
            case RenderQueue::Opaque:
                rendererData_.opaqueItems.push_back(item);
                break;
            case RenderQueue::Transparent:
                rendererData_.transparentItems.push_back(item);
                break;
            default:
                VE_ASSERT_ALWAYS_MESSAGE(false, "Renderer queue build encountered an unsupported material queue.");
                break;
            }
        }

        if (rendererData_.resolvedCamera == nullptr)
        {
            return;
        }

        const Matrix44& cameraTransform = rendererData_.resolvedCamera->GetLocalToWorld();
        const Vector3 cameraPosition(cameraTransform.Get(0, 3), cameraTransform.Get(1, 3), cameraTransform.Get(2, 3));
        std::stable_sort(rendererData_.transparentItems.begin(),
                         rendererData_.transparentItems.end(),
                         [&cameraPosition](const std::shared_ptr<RTRenderItem>& left, const std::shared_ptr<RTRenderItem>& right)
                         {
                             const Vector3 leftCenter = left->GetLocalToWorld().TransformPoint(left->GetBoundsCenter());
                             const Vector3 rightCenter = right->GetLocalToWorld().TransformPoint(right->GetBoundsCenter());
                             return (leftCenter - cameraPosition).LengthSquared() > (rightCenter - cameraPosition).LengthSquared();
                         });
    }

    ErrorCode BaseRenderer::ImportRenderTargets(FrameGraph& frameGraph, RendererFrameGraphData& graphData) const
    {
        if (target_.colorTexture != nullptr)
        {
            rhi::RhiTexture* colorTexture = target_.colorTexture->GetTexture();
            if (colorTexture == nullptr)
            {
                return ErrorCode::InvalidState;
            }
            graphData.color = frameGraph.ImportTexture(
                "RendererColor", MakeTextureDesc(*colorTexture, MakeColorTargetUsage()), ImportedFrameGraphTexture{colorTexture, false});

            rhi::RhiTexture* depthTexture = target_.colorTexture->GetDepthTexture();
            if (depthTexture != nullptr)
            {
                graphData.depth = frameGraph.ImportTexture(
                    "RendererDepth", MakeTextureDesc(*depthTexture, rhi::RhiTextureUsage::DepthStencil), ImportedFrameGraphTexture{depthTexture, false});
            }
            return ErrorCode::None;
        }

        const rhi::RhiExtent2D extent = frameRenderData_->mainSwapchain->GetExtent();
        FrameGraphTextureDesc colorDesc = {};
        colorDesc.width = extent.width;
        colorDesc.height = extent.height;
        colorDesc.format = frameRenderData_->mainSwapchain->GetColorFormat();
        colorDesc.usage = rhi::RhiTextureUsage::RenderTarget;
        graphData.color = frameGraph.ImportTexture("MainSwapchainColor", colorDesc, ImportedFrameGraphTexture{nullptr, true});
        return ErrorCode::None;
    }
} // namespace ve
