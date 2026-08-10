#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderTexture.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
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

        [[nodiscard]] rhi::RhiTextureUsage MakeSampledDepthUsage() noexcept
        {
            return static_cast<rhi::RhiTextureUsage>(static_cast<UInt32>(rhi::RhiTextureUsage::DepthStencil) |
                                                     static_cast<UInt32>(rhi::RhiTextureUsage::Sampled));
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

        [[noreturn]] void FailRenderer(const std::string& message)
        {
            VE_LOG_ERROR("{}", message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message.c_str());
            std::terminate();
        }

        void RequireRenderer(bool condition, const std::string& message)
        {
            if (!condition)
            {
                FailRenderer(message);
            }
        }

        [[nodiscard]] std::string BuildIndexedResourceName(const char* baseName, UInt32 viewIndex)
        {
            return std::string(baseName) + "[" + std::to_string(viewIndex) + "]";
        }
    } // namespace

    void BuildRendererQueues(RendererData& rendererData)
    {
        VE_ASSERT_RENDER_THREAD();
        rendererData.opaqueItems.clear();
        for (RendererViewData& viewData : rendererData.views)
        {
            viewData.transparentItems.clear();
        }
        if (rendererData.scene == nullptr)
        {
            return;
        }

        std::vector<std::shared_ptr<RTRenderItem>> transparentCandidates;
        for (SizeT itemIndex = 0; itemIndex < rendererData.scene->GetRenderItemCount(); ++itemIndex)
        {
            const std::shared_ptr<RTRenderItem> item = rendererData.scene->GetRenderItem(itemIndex);
            if (item == nullptr)
            {
                FailRenderer("Renderer queue build encountered a null render item at scene index " + std::to_string(itemIndex) + ".");
            }

            if (item->GetMeshResource() == nullptr)
            {
                continue;
            }

            const auto material = std::dynamic_pointer_cast<RTMaterialResource>(item->GetMaterialResource());
            if (material == nullptr)
            {
                FailRenderer("Renderer queue build requires a material resource.");
            }

            switch (material->GetDesc().renderQueue)
            {
            case RenderQueue::Opaque:
                rendererData.opaqueItems.push_back(item);
                break;
            case RenderQueue::Transparent:
                transparentCandidates.push_back(item);
                break;
            default:
                FailRenderer("Renderer queue build encountered an unsupported material queue.");
            }
        }

        for (RendererViewData& viewData : rendererData.views)
        {
            const std::shared_ptr<RTCamera>& camera = viewData.view.camera;
            if (camera == nullptr)
            {
                continue;
            }

            viewData.transparentItems = transparentCandidates;
            const Matrix44& cameraTransform = camera->GetLocalToWorld();
            const Vector3 cameraPosition(cameraTransform.Get(0, 3), cameraTransform.Get(1, 3), cameraTransform.Get(2, 3));
            std::stable_sort(viewData.transparentItems.begin(),
                             viewData.transparentItems.end(),
                             [&cameraPosition](const std::shared_ptr<RTRenderItem>& left, const std::shared_ptr<RTRenderItem>& right)
                             {
                                 const Vector3 leftCenter = left->GetLocalToWorld().TransformPoint(left->GetBoundsCenter());
                                 const Vector3 rightCenter = right->GetLocalToWorld().TransformPoint(right->GetBoundsCenter());
                                 return (leftCenter - cameraPosition).LengthSquared() > (rightCenter - cameraPosition).LengthSquared();
                             });
        }
    }

    BaseRenderer::BaseRenderer(BaseRendererInitParam initParam)
        : outputPasses_(std::move(initParam.outputPasses))
        , frameRenderData_(initParam.frameData)
    {
        VE_ASSERT_RENDER_THREAD();
        rendererData_.scene = std::move(initParam.viewFamily.scene);
        rendererData_.views.reserve(initParam.viewFamily.views.size());
        for (RenderView& view : initParam.viewFamily.views)
        {
            RendererViewData viewData = {};
            viewData.view = std::move(view);
            rendererData_.views.push_back(std::move(viewData));
        }
    }

    void BaseRenderer::Render()
    {
        VE_ASSERT_RENDER_THREAD();
        lastFrameGraphPassDiagnostics_.clear();
        lastFrameGraphExecutionPassNames_.clear();
        RequireRenderer(frameRenderData_ != nullptr && frameRenderData_->device != nullptr && frameRenderData_->frameContext != nullptr &&
                            frameRenderData_->mainSwapchain != nullptr && frameRenderData_->pipelineManager != nullptr,
                        "Renderer family requires initialized frame services.");
        RequireRenderer(!rendererData_.views.empty() || !outputPasses_.empty(), "Renderer family requires at least one render view or output pass.");
        RequireRenderer(rendererData_.views.size() <= static_cast<SizeT>(std::numeric_limits<UInt32>::max()),
                        "Renderer family view count exceeds its frame-graph index range.");

        SizeT mainOutputViewCount = 0;
        std::optional<SizeT> mainOutputViewIndex;
        std::unordered_set<rhi::RhiTexture*> writableTargets;
        for (SizeT viewIndex = 0; viewIndex < rendererData_.views.size(); ++viewIndex)
        {
            const RendererViewData& viewData = rendererData_.views[viewIndex];
            if (rendererData_.scene != nullptr && viewData.view.camera != nullptr && viewData.view.viewState == nullptr)
            {
                FailRenderer("A scene renderer with an active camera requires a persistent render view state.");
            }
            if (viewData.view.target.colorTexture == nullptr)
            {
                ++mainOutputViewCount;
                mainOutputViewIndex = viewIndex;
                continue;
            }

            rhi::RhiTexture* colorTexture = viewData.view.target.colorTexture->GetTexture();
            if (colorTexture == nullptr)
            {
                FailRenderer("Renderer view has an uninitialized offscreen color target.");
            }
            if (!writableTargets.insert(colorTexture).second)
            {
                FailRenderer("Renderer family views must not alias the same writable offscreen color target.");
            }

            rhi::RhiTexture* depthTexture = viewData.view.target.colorTexture->GetDepthTexture();
            if (depthTexture != nullptr && !writableTargets.insert(depthTexture).second)
            {
                FailRenderer("Renderer family views must not alias the same writable offscreen depth target.");
            }
        }
        if (mainOutputViewCount > 1)
        {
            FailRenderer("A renderer family supports at most one main-output render view.");
        }

        UpdateRenderWorld();
        BuildRendererQueues(rendererData_);
        for (RendererViewData& viewData : rendererData_.views)
        {
            viewData.virtualShadowSampling = {};
        }

        FrameGraph frameGraph(FrameGraphExecuteContext{*frameRenderData_, rendererData_});
        RendererFrameGraphData graphData = {};
        graphData.views.resize(rendererData_.views.size());

        frameGraph.Setup(
            [this, &graphData, mainOutputViewIndex](FrameGraph& setupGraph)
            {
                for (SizeT viewIndex = 0; viewIndex < graphData.views.size(); ++viewIndex)
                {
                    ImportViewRenderTargets(setupGraph, static_cast<UInt32>(viewIndex), graphData.views[viewIndex]);
                }
                if (!mainOutputViewIndex.has_value())
                {
                    ImportMainSwapchainColor(setupGraph, graphData);
                }

                BuildFrameGraph(setupGraph, graphData);
                if (mainOutputViewIndex.has_value())
                {
                    graphData.swapchainColor = graphData.views[*mainOutputViewIndex].color;
                }
                for (std::unique_ptr<RenderPass>& pass : outputPasses_)
                {
                    if (pass == nullptr)
                    {
                        FailRenderer("Renderer output pass ownership contains a null pass.");
                    }
                    pass->AddToFrameGraph(setupGraph, graphData);
                }

                std::vector<FrameGraphTextureHandle> exportedColors;
                exportedColors.reserve(graphData.views.size() + 1);
                const auto exportColor = [&setupGraph, &exportedColors](FrameGraphTextureHandle handle)
                {
                    if (handle.IsValid() && std::ranges::find(exportedColors, handle) == exportedColors.end())
                    {
                        setupGraph.Export(handle);
                        exportedColors.push_back(handle);
                    }
                };

                for (SizeT viewIndex = 0; viewIndex < graphData.views.size(); ++viewIndex)
                {
                    if (!mainOutputViewIndex.has_value() || viewIndex != *mainOutputViewIndex)
                    {
                        exportColor(graphData.views[viewIndex].color);
                    }
                }
                exportColor(graphData.swapchainColor);
            });
        lastFrameGraphPassDiagnostics_ = frameGraph.GetPassDiagnostics();

        if (frameRenderData_->frameGraphDebugCapture != nullptr)
        {
            const Error debugPrepareResult = frameGraph.PrepareDebugCapture(*frameRenderData_->frameGraphDebugCapture);
            if (!debugPrepareResult.IsOk())
            {
                frameRenderData_->frameGraphDebugCapture->failureMessage = debugPrepareResult.GetMessage();
            }
        }

        Error compileResult = frameGraph.Compile();
        if (!compileResult.IsOk())
        {
            VE_LOG_ERROR("Renderer family frame graph compile failed: {}", compileResult.GetMessage());
            FailRenderer("Renderer family frame graph compilation failed.");
        }

        const ErrorCode executeResult = frameGraph.Execute();
        lastFrameGraphExecutionPassNames_ = frameGraph.GetLastExecutionPassNames();
        if (executeResult != ErrorCode::None)
        {
            FailRenderer("Renderer family frame graph execution failed.");
        }

    }

    const std::vector<FrameGraphPassDiagnostics>& BaseRenderer::GetLastFrameGraphPassDiagnostics() const noexcept
    {
        return lastFrameGraphPassDiagnostics_;
    }

    const std::vector<std::string>& BaseRenderer::GetLastFrameGraphExecutionPassNames() const noexcept
    {
        return lastFrameGraphExecutionPassNames_;
    }

    const RendererData& BaseRenderer::GetRendererData() const noexcept
    {
        return rendererData_;
    }

    RendererData& BaseRenderer::GetMutableRendererData() noexcept
    {
        return rendererData_;
    }

    const FrameRenderPipelineData& BaseRenderer::GetFrameData() const noexcept
    {
        if (frameRenderData_ == nullptr)
        {
            FailRenderer("Renderer family frame data is unavailable.");
        }
        return *frameRenderData_;
    }

    void BaseRenderer::UpdateRenderWorld()
    {
        VE_ASSERT_RENDER_THREAD();
    }

    void BaseRenderer::ImportViewRenderTargets(FrameGraph& frameGraph, UInt32 viewIndex, RendererViewFrameGraphData& graphData) const
    {
        if (viewIndex >= rendererData_.views.size())
        {
            FailRenderer("Renderer target import references an out-of-bounds view.");
        }
        const RendererRenderTarget& target = rendererData_.views[viewIndex].view.target;
        if (target.colorTexture != nullptr)
        {
            rhi::RhiTexture* colorTexture = target.colorTexture->GetTexture();
            if (colorTexture == nullptr)
            {
                FailRenderer("Renderer view has an uninitialized offscreen color target.");
            }
            graphData.color = frameGraph.ImportTexture(BuildIndexedResourceName("RendererColor", viewIndex),
                                                       MakeTextureDesc(*colorTexture, MakeColorTargetUsage()),
                                                       ImportedFrameGraphTexture{colorTexture, false});
            frameRenderData_->RetainInFlightGpuFrameObject(target.colorTexture->GetTextureShared());

            rhi::RhiTexture* depthTexture = target.colorTexture->GetDepthTexture();
            if (depthTexture != nullptr)
            {
                graphData.depth = frameGraph.ImportTexture(BuildIndexedResourceName("RendererDepth", viewIndex),
                                                           MakeTextureDesc(*depthTexture, MakeSampledDepthUsage()),
                                                           ImportedFrameGraphTexture{depthTexture, false});
                frameRenderData_->RetainInFlightGpuFrameObject(target.colorTexture->GetDepthTextureShared());
            }
            else
            {
                FrameGraphTextureDesc depthDesc = {};
                depthDesc.width = colorTexture->GetWidth();
                depthDesc.height = colorTexture->GetHeight();
                depthDesc.format = rhi::RhiFormat::Depth32Float;
                depthDesc.usage = MakeSampledDepthUsage();
                graphData.depth = frameGraph.CreateTexture(BuildIndexedResourceName("RendererDepth", viewIndex), depthDesc);
            }
            return;
        }

        const rhi::RhiExtent2D extent = frameRenderData_->mainSwapchain->GetExtent();
        FrameGraphTextureDesc colorDesc = {};
        colorDesc.width = extent.width;
        colorDesc.height = extent.height;
        colorDesc.format = frameRenderData_->mainSwapchain->GetColorFormat();
        colorDesc.usage = rhi::RhiTextureUsage::RenderTarget;
        graphData.color = frameGraph.ImportTexture("MainSwapchainColor", colorDesc, ImportedFrameGraphTexture{nullptr, true});
        FrameGraphTextureDesc depthDesc = {};
        depthDesc.width = extent.width;
        depthDesc.height = extent.height;
        depthDesc.format = rhi::RhiFormat::Depth32Float;
        depthDesc.usage = MakeSampledDepthUsage();
        graphData.depth = frameGraph.CreateTexture(BuildIndexedResourceName("RendererDepth", viewIndex), depthDesc);
    }

    void BaseRenderer::ImportMainSwapchainColor(FrameGraph& frameGraph, RendererFrameGraphData& graphData) const
    {
        if (frameRenderData_ == nullptr || frameRenderData_->mainSwapchain == nullptr)
        {
            FailRenderer("Main-swapchain import requires initialized frame data and a main swapchain.");
        }
        if (graphData.swapchainColor.IsValid())
        {
            FailRenderer("Main-swapchain color was imported more than once.");
        }

        const rhi::RhiExtent2D extent = frameRenderData_->mainSwapchain->GetExtent();
        FrameGraphTextureDesc colorDesc = {};
        colorDesc.width = extent.width;
        colorDesc.height = extent.height;
        colorDesc.format = frameRenderData_->mainSwapchain->GetColorFormat();
        colorDesc.usage = rhi::RhiTextureUsage::RenderTarget;
        graphData.swapchainColor = frameGraph.ImportTexture("MainSwapchainColor", colorDesc, ImportedFrameGraphTexture{nullptr, true});
    }

} // namespace ve
