#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h"

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Math/Bounds.h"
#include "Engine/Runtime/Render/FrameContext.h"
#include "Engine/Runtime/Render/RenderCameraMath.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderTexture.h"
#include "Engine/Runtime/Render/RenderViewState.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/Renderer/RenderQueue.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowFrameGraph.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPasses.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ve
{
    namespace
    {
        void RequireVirtualShadow(bool condition, const char* message)
        {
            if (!condition)
            {
                FailVirtualShadow(message);
            }
        }

        [[nodiscard]] std::shared_ptr<RTLight> FindPrimaryDirectionalLight(const std::shared_ptr<RTScene>& scene)
        {
            RequireVirtualShadow(scene != nullptr, "VSM requires a render scene.");
            for (SizeT lightIndex = 0; lightIndex < scene->GetLightCount(); ++lightIndex)
            {
                const std::shared_ptr<RTLight> light = scene->GetLight(lightIndex);
                if (light != nullptr && light->GetType() == RTLightType::Directional)
                {
                    return light;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::shared_ptr<RTLight> GetRequiredShadowDirectionalLight(const std::shared_ptr<RTScene>& scene)
        {
            std::shared_ptr<RTLight> light = FindPrimaryDirectionalLight(scene);
            RequireVirtualShadow(light != nullptr, "VSM requires a directional light.");
            RequireVirtualShadow(light->CastShadows(), "VSM requires directional-light shadows to be enabled.");
            return light;
        }

        [[nodiscard]] VirtualShadowLightInput BuildRequiredVirtualShadowLightInput(const std::shared_ptr<RTLight>& light) noexcept
        {
            return {true, light->GetDirection(), light->GetShadowDistance(), light->GetDepthBias(), light->GetNormalBias()};
        }

        void ValidateVirtualShadowFrameGraphInputs(const FrameRenderPipelineData& frameData,
                                                   const RendererData& rendererData,
                                                   const RendererFrameGraphData& graphData)
        {
            RequireVirtualShadow(frameData.device != nullptr, "VSM requires an RHI device.");
            RequireVirtualShadow(frameData.pipelineManager != nullptr, "VSM requires an RHI pipeline manager.");
            RequireVirtualShadow(frameData.device->GetBackend() != rhi::RhiBackend::Metal, "Metal VSM is not implemented.");
            RequireVirtualShadow(frameData.mainSwapchain != nullptr, "VSM requires the frame swapchain.");
            RequireVirtualShadow(frameData.frameContext != nullptr, "VSM requires a frame context.");
            RequireVirtualShadow(rendererData.scene != nullptr, "VSM requires a render scene.");
            RequireVirtualShadow(graphData.views.size() == rendererData.views.size(), "VSM renderer/graph view counts differ.");
            RequireVirtualShadow(!rendererData.views.empty(), "VSM requires at least one renderer view.");
            RequireVirtualShadow(rendererData.views.size() <= static_cast<SizeT>(std::numeric_limits<UInt32>::max()),
                                 "VSM renderer view count exceeds its graph index range.");
        }

        [[nodiscard]] std::vector<VirtualShadowCasterSnapshot> BuildVirtualShadowCasterSnapshots(const RendererData& rendererData)
        {
            std::vector<VirtualShadowCasterSnapshot> casterSnapshots;
            casterSnapshots.reserve(rendererData.opaqueItems.size());
            for (const std::shared_ptr<RTRenderItem>& renderItem : rendererData.opaqueItems)
            {
                if (renderItem == nullptr)
                {
                    continue;
                }

                const Aabb localBounds = Aabb::FromCenterExtents(renderItem->GetBoundsCenter(), renderItem->GetBoundsExtents());
                casterSnapshots.push_back({renderItem->GetRenderItemID(),
                                           renderItem->GetRevision(),
                                           localBounds.Transformed(renderItem->GetLocalToWorld()),
                                           renderItem->CastShadows()});
            }
            return casterSnapshots;
        }

        [[nodiscard]] rhi::RhiExtent2D ResolveVirtualShadowViewExtent(const FrameRenderPipelineData& frameData, const RenderView& view)
        {
            rhi::RhiExtent2D extent = frameData.mainSwapchain->GetExtent();
            if (view.target.colorTexture != nullptr)
            {
                const WindowExtent targetExtent = view.target.colorTexture->GetDesc().extent;
                extent = {targetExtent.width, targetExtent.height};
            }
            RequireVirtualShadow(extent.width != 0 && extent.height != 0, "Every VSM renderer view requires a non-zero target extent.");
            return extent;
        }

        void PublishVirtualShadowFrameGraphResults(RendererData& rendererData,
                                                   RendererFrameGraphData& graphData,
                                                   const VirtualShadowPreparedFamilyGraphData& preparedFamily,
                                                   const VirtualShadowFrameGraphResources& resources)
        {
            graphData.virtualShadowAtlas = resources.atlas;
            graphData.virtualShadowPhysicalPages = resources.physicalPages;
            graphData.virtualShadowStatistics = resources.statistics;
            for (const VirtualShadowPreparedViewGraphData& preparedView : preparedFamily.views)
            {
                graphData.views[preparedView.viewIndex].virtualShadowPageTable = resources.pageTable;
            }
            for (SizeT viewIndex = 0; viewIndex < rendererData.views.size(); ++viewIndex)
            {
                graphData.views[viewIndex].virtualShadowSampling = rendererData.views[viewIndex].virtualShadowSampling;
            }
        }
    } // namespace

    struct VirtualShadowManager::Impl
    {
        struct SceneEntry
        {
            std::weak_ptr<RTScene> scene;
            VirtualShadowInvalidationTracker invalidationTracker;
            virtual_shadow_detail::SceneState state;
            UInt64 statisticsIdentity = 0;
            UInt64 dirtyCasterFrameIndex = std::numeric_limits<UInt64>::max();
            std::unordered_set<UInt64> dirtyCasterIDs;
        };

        struct ViewRegistration
        {
            std::weak_ptr<RTRenderViewState> view;
            RTScene* scene = nullptr;
            UInt32 viewID = InvalidVirtualShadowViewID;
            bool forceInvalidateViewPages = false;
        };

        struct ViewIDReservation
        {
            std::weak_ptr<const void> lifetimeToken;
        };

        struct CandidateView
        {
            RenderView* view = nullptr;
            VirtualShadowViewResult result;
            VirtualShadowPageTableSlice slice;
            rhi::RhiExtent2D extent;
            UInt32 viewID = InvalidVirtualShadowViewID;
            UInt32 viewIndex = 0;
        };

        struct FamilyPreparationResult
        {
            VirtualShadowPreparedFamilyGraphData family;
            UInt64 statisticsIdentity = 0;
        };

        explicit Impl(VirtualShadowSceneCacheDesc desc)
            : sceneCache(std::move(desc))
        {
        }

        [[nodiscard]] VirtualShadowSchedulingDiagnostics& BeginFrameGraphScheduling(UInt64 frameIndex, SizeT viewCount)
        {
            VirtualShadowSchedulingDiagnostics& diagnostics = GetSchedulingDiagnostics(frameIndex);
            ++diagnostics.graphRegistrarInvocationCount;
            if (diagnostics.graphRegistrarInvocationCount != 1)
            {
                FailVirtualShadow("One frame may have exactly one VSM scheduling owner.");
            }
            diagnostics.viewDraws.clear();
            diagnostics.viewDraws.reserve(viewCount);
            return diagnostics;
        }

        [[nodiscard]] SceneEntry& GetOrCreateSceneEntry(const std::shared_ptr<RTScene>& scene)
        {
            auto [iterator, inserted] = scenes.try_emplace(scene.get());
            if (!inserted)
            {
                const std::shared_ptr<RTScene> registeredScene = iterator->second.scene.lock();
                if (registeredScene == nullptr || registeredScene.get() != scene.get())
                {
                    for (ViewRegistration& registration : viewRegistrations)
                    {
                        if (registration.scene != scene.get())
                        {
                            continue;
                        }
                        sceneCache.GetSliceAllocator().Release(registration.viewID);
                        if (const std::shared_ptr<RTRenderViewState> view = registration.view.lock())
                        {
                            view->ClearVirtualShadowPageTableSlice();
                        }
                        registration.scene = nullptr;
                    }
                    scenes.erase(iterator);
                    std::tie(iterator, inserted) = scenes.try_emplace(scene.get());
                }
            }
            SceneEntry& entry = iterator->second;
            if (inserted)
            {
                entry.scene = scene;
                entry.statisticsIdentity = nextSceneIdentity++;
                if (nextSceneIdentity == 0)
                {
                    nextSceneIdentity = 1;
                }
            }
            return entry;
        }

        void ActivateScenePool(RTScene* scene)
        {
            if (activePoolScene == scene)
            {
                return;
            }

            for (ViewRegistration& registration : viewRegistrations)
            {
                if (registration.scene != activePoolScene)
                {
                    continue;
                }
                sceneCache.GetSliceAllocator().Release(registration.viewID);
                if (const std::shared_ptr<RTRenderViewState> view = registration.view.lock())
                {
                    view->ClearVirtualShadowPageTableSlice();
                }
                registration.scene = nullptr;
            }
            activePoolScene = scene;
            sceneCache.RequestSceneCacheReset();
        }

        struct StatisticsReadbackSlot
        {
            std::unique_ptr<rhi::RhiBuffer> buffer;
            UInt64 sourceFrameIndex = 0;
            UInt64 sceneIdentity = 0;
            UInt32 totalPhysicalPages = 0;
            rhi::RhiFence* completionFence = nullptr;
            UInt64 submittedFenceValue = 0;
            bool copyRecorded = false;
        };

        [[nodiscard]] StatisticsReadbackSlot& GetStatisticsSlot(UInt64 frameIndex) noexcept
        {
            return statisticsReadbackSlots[frameIndex % RenderFrameContextCount];
        }

        [[nodiscard]] const StatisticsReadbackSlot& GetStatisticsSlot(UInt64 frameIndex) const noexcept
        {
            return statisticsReadbackSlots[frameIndex % RenderFrameContextCount];
        }

        [[nodiscard]] VirtualShadowSchedulingDiagnostics& GetSchedulingDiagnostics(UInt64 frameIndex) noexcept
        {
            VirtualShadowSchedulingDiagnostics& diagnostics = schedulingDiagnostics[frameIndex % RenderFrameContextCount];
            if (diagnostics.frameIndex != frameIndex)
            {
                diagnostics = {};
                diagnostics.frameIndex = frameIndex;
            }
            return diagnostics;
        }

        [[nodiscard]] VirtualShadowSchedulingDiagnostics GetSchedulingDiagnostics(UInt64 frameIndex) const noexcept
        {
            const VirtualShadowSchedulingDiagnostics& diagnostics = schedulingDiagnostics[frameIndex % RenderFrameContextCount];
            if (diagnostics.frameIndex == frameIndex)
            {
                return diagnostics;
            }
            VirtualShadowSchedulingDiagnostics empty;
            empty.frameIndex = frameIndex;
            return empty;
        }

        void PrepareFrameStatistics(UInt64 frameIndex, UInt64 sceneIdentity)
        {
            VE_ASSERT_RENDER_THREAD();
            StatisticsReadbackSlot& slot = GetStatisticsSlot(frameIndex);
            if (slot.copyRecorded)
            {
                if (slot.completionFence == nullptr || slot.submittedFenceValue == 0)
                {
                    FailVirtualShadow("VSM statistics cannot reuse a readback slot whose copy has not been submitted.");
                }
                if (!slot.completionFence->IsComplete(slot.submittedFenceValue))
                {
                    FailVirtualShadow("VSM statistics cannot reuse a readback slot before its submission fence completes.");
                }
                FailVirtualShadow("VSM statistics must consume a completed readback slot before reusing it.");
            }

            slot.sourceFrameIndex = frameIndex;
            slot.sceneIdentity = sceneIdentity;
            slot.totalPhysicalPages = 0;
            slot.completionFence = nullptr;
            slot.submittedFenceValue = 0;
            slot.copyRecorded = true;
        }

        void EnsureStatisticsReadbackBuffer(rhi::RhiDevice& device, UInt64 frameIndex)
        {
            VE_ASSERT_RENDER_THREAD();
            if (statisticsReadbackDevice != &device)
            {
                for (StatisticsReadbackSlot& slot : statisticsReadbackSlots)
                {
                    if (slot.copyRecorded)
                    {
                        FailVirtualShadow("VSM cannot replace statistics readback resources while a slot is in flight.");
                    }
                    slot = {};
                }
                statisticsReadbackDevice = &device;
            }

            StatisticsReadbackSlot& slot = GetStatisticsSlot(frameIndex);
            if (slot.buffer != nullptr)
            {
                return;
            }

            rhi::RhiBufferDesc bufferDesc = {};
            bufferDesc.size = sizeof(VirtualShadowGpuStatistics);
            bufferDesc.usage = rhi::RhiBufferUsage::Readback;
            bufferDesc.memoryUsage = rhi::RhiBufferMemoryUsage::GpuToCpu;
            bufferDesc.debugName = "VirtualShadowStatisticsReadback";
            slot.buffer = device.CreateBuffer(bufferDesc);
            if (slot.buffer == nullptr)
            {
                FailVirtualShadow("VSM failed to create its mandatory statistics readback buffer.");
            }
        }

        [[nodiscard]] StatisticsReadbackSlot&
        PrepareStatisticsReadback(rhi::RhiDevice& device, UInt64 frameIndex, UInt64 sceneIdentity, VirtualShadowSchedulingDiagnostics& diagnostics)
        {
            EnsureStatisticsReadbackBuffer(device, frameIndex);
            PrepareFrameStatistics(frameIndex, sceneIdentity);
            ++diagnostics.statisticsReadbackPreparationCount;

            StatisticsReadbackSlot& slot = GetStatisticsSlot(frameIndex);
            if (slot.buffer == nullptr)
            {
                FailVirtualShadow("VSM graph preparation requires the current statistics readback slot.");
            }
            return slot;
        }

        [[nodiscard]] ViewRegistration* RegisterView(const std::shared_ptr<RTRenderViewState>& viewState, RTScene* scene, SceneEntry& sceneEntry)
        {
            const UInt32 viewID = viewState->GetVirtualShadowViewID();
            auto iterator = std::ranges::find_if(viewRegistrations, [viewID](const ViewRegistration& registration) { return registration.viewID == viewID; });
            if (iterator != viewRegistrations.end())
            {
                const std::shared_ptr<RTRenderViewState> registeredView = iterator->view.lock();
                if (registeredView != nullptr && registeredView.get() != viewState.get())
                {
                    return nullptr;
                }
                if (iterator->scene != scene)
                {
                    sceneCache.GetSliceAllocator().Release(viewID);
                    viewState->ClearVirtualShadowPageTableSlice();
                    iterator->scene = scene;
                    iterator->forceInvalidateViewPages |= sceneEntry.state.RegisterViewID(viewID);
                }
                iterator->view = viewState;
            }
            else
            {
                const bool forceInvalidateViewPages = sceneEntry.state.RegisterViewID(viewID);
                viewRegistrations.push_back({viewState, scene, viewID, forceInvalidateViewPages});
                iterator = std::prev(viewRegistrations.end());
            }

            const VirtualShadowPageTableSlice slice = sceneCache.GetSliceAllocator().Acquire(viewID);
            if (!slice.IsValid())
            {
                viewState->ClearVirtualShadowPageTableSlice();
                return &*iterator;
            }
            viewState->SetVirtualShadowPageTableSlice(slice);
            return &*iterator;
        }

        [[nodiscard]] ViewRegistration* FindViewRegistration(UInt32 viewID)
        {
            const auto iterator =
                std::ranges::find_if(viewRegistrations, [viewID](const ViewRegistration& registration) { return registration.viewID == viewID; });
            return iterator != viewRegistrations.end() ? &*iterator : nullptr;
        }

        void QueueSceneChangedBounds(RTScene* scene, UInt64 frameIndex, std::span<const Aabb> changedBounds)
        {
            for (ViewRegistration& registration : viewRegistrations)
            {
                if (registration.scene != scene)
                {
                    continue;
                }
                if (const std::shared_ptr<RTRenderViewState> view = registration.view.lock())
                {
                    virtual_shadow_detail::QueueChangedCasterBounds(view->GetVirtualShadowViewCache(), frameIndex, registration.viewID, changedBounds);
                }
            }
        }

        [[nodiscard]] FamilyPreparationResult PrepareFamilyGraphData(VirtualShadowManager& manager,
                                                                     const FrameRenderPipelineData& frameData,
                                                                     RendererData& rendererData,
                                                                     RendererFrameGraphData& graphData,
                                                                     const VirtualShadowLightInput& light,
                                                                     VirtualShadowSchedulingDiagnostics& diagnostics)
        {
            SceneEntry& sceneEntry = GetOrCreateSceneEntry(rendererData.scene);
            ++diagnostics.sceneStateAcquisitionCount;
            ++diagnostics.sceneFramePreparationCount;

            sceneCache.EnsureResources(*frameData.device);
            ActivateScenePool(rendererData.scene.get());

            const std::vector<VirtualShadowCasterSnapshot> casterSnapshots = BuildVirtualShadowCasterSnapshots(rendererData);
            const VirtualShadowSceneInvalidationResult sceneInvalidation =
                sceneEntry.invalidationTracker.UpdateScene(frameData.frameIndex, light.direction, casterSnapshots);
            if (sceneEntry.dirtyCasterFrameIndex != frameData.frameIndex)
            {
                sceneEntry.dirtyCasterFrameIndex = frameData.frameIndex;
                sceneEntry.dirtyCasterIDs.clear();
            }
            sceneEntry.dirtyCasterIDs.insert(sceneInvalidation.changedCasterIDs.begin(), sceneInvalidation.changedCasterIDs.end());
            rendererData.virtualShadowDirtyCasterIDs.clear();
            rendererData.virtualShadowDirtyCasterIDs.insert(sceneEntry.dirtyCasterIDs.begin(), sceneEntry.dirtyCasterIDs.end());
            if (sceneInvalidation.lightBasisChanged)
            {
                sceneEntry.state.RequestReset();
            }
            QueueSceneChangedBounds(rendererData.scene.get(), frameData.frameIndex, sceneInvalidation.changedBounds);

            virtual_shadow_detail::FamilyViewClaims familyViewClaims;
            std::unordered_set<UInt32> familySliceClaims;
            std::vector<CandidateView> candidateViews;
            candidateViews.reserve(rendererData.views.size());
            for (SizeT viewIndex = 0; viewIndex < rendererData.views.size(); ++viewIndex)
            {
                RenderView& view = rendererData.views[viewIndex].view;
                RequireVirtualShadow(view.viewState != nullptr, "Every VSM renderer view requires persistent view state.");

                const UInt32 viewID = manager.GetOrAssignViewID(*view.viewState);
                RequireVirtualShadow(viewID != InvalidVirtualShadowViewID, "Every VSM renderer view requires a valid stable view ID.");
                RequireVirtualShadow(familyViewClaims.TryClaim(viewID), "VSM renderer views must have distinct stable view IDs.");
                RequireVirtualShadow(view.camera != nullptr, "Every VSM renderer view requires a camera.");
                RequireVirtualShadow(graphData.views[viewIndex].depth.IsValid() && graphData.views[viewIndex].depth.version != 0,
                                     "Every VSM renderer view requires receiver depth produced by DepthPrePass.");

                const rhi::RhiExtent2D extent = ResolveVirtualShadowViewExtent(frameData, view);
                ViewRegistration* registration = RegisterView(view.viewState, rendererData.scene.get(), sceneEntry);
                RequireVirtualShadow(registration != nullptr, "VSM encountered a conflicting stable view registration.");
                const VirtualShadowPageTableSlice slice = view.viewState->GetVirtualShadowPageTableSlice();
                RequireVirtualShadow(slice.IsValid(), "Every VSM renderer view requires a valid page-table slice.");
                RequireVirtualShadow(familySliceClaims.insert(slice.index).second, "VSM renderer views must have distinct page-table slices.");

                candidateViews.push_back({&view, {}, slice, extent, viewID, static_cast<UInt32>(viewIndex)});
            }
            RequireVirtualShadow(candidateViews.size() == rendererData.views.size(), "VSM must prepare every renderer view.");

            const bool sceneCacheResetPending = sceneCache.IsSceneCacheResetPending();
            const bool resetSceneCache = sceneCacheResetPending || sceneEntry.state.IsResetPending();
            VirtualShadowPreparedFamilyGraphData preparedFamily;
            preparedFamily.sceneCache = &sceneCache;
            preparedFamily.views.reserve(candidateViews.size());
            for (CandidateView& candidate : candidateViews)
            {
                RenderView& view = *candidate.view;
                VirtualShadowPrepareInput prepareInput;
                prepareInput.frameIndex = frameData.frameIndex;
                prepareInput.viewID = candidate.viewID;
                prepareInput.screenWidth = candidate.extent.width;
                prepareInput.screenHeight = candidate.extent.height;
                prepareInput.viewProjection = BuildCameraViewProjection(*view.camera, candidate.extent);
                prepareInput.cameraLocalToWorld = view.camera->GetLocalToWorld();
                prepareInput.light = light;
                prepareInput.resetSceneCache = resetSceneCache;

                candidate.result.packet = view.viewState->GetVirtualShadowViewCache().PrepareFrame(prepareInput);
                candidate.result.packet.atlasExtent = sceneCache.GetDesc().atlasExtent;
                ViewRegistration* registration = FindViewRegistration(candidate.viewID);
                RequireVirtualShadow(registration != nullptr, "VSM lost a required view registration during frame preparation.");
                virtual_shadow_detail::ApplyForcedViewInvalidation(candidate.result.packet, registration->forceInvalidateViewPages);
                if (!candidate.result.packet.valid)
                {
                    FailVirtualShadow("VSM view preparation produced an invalid packet.");
                }

                candidate.result.atlas = sceneCache.GetAtlasTexture();
                candidate.result.pageTable = sceneCache.GetPageTableBuffer();
                candidate.result.pageTableOffset = candidate.slice.pageTableOffset;
                candidate.result.pageTableSize = VirtualShadowLogicalPageBufferSize;

                diagnostics.viewDraws.push_back({candidate.result.packet.viewID, 0});
                preparedFamily.views.push_back({candidate.viewIndex,
                                                candidate.result.packet,
                                                candidate.slice,
                                                graphData.views[candidate.viewIndex].depth,
                                                &diagnostics.viewDraws.back()});
                rendererData.views[candidate.viewIndex].virtualShadowSampling = BuildVirtualShadowSamplingSnapshot(candidate.result);
            }

            RequireVirtualShadow(preparedFamily.views.size() == rendererData.views.size(), "VSM failed to prepare every renderer view.");
            if (sceneCacheResetPending && !sceneCache.ConsumeSceneCacheReset())
            {
                FailVirtualShadow("VSM graph preparation failed to consume its scene-cache reset.");
            }
            sceneEntry.state.ConsumeReset();
            return {std::move(preparedFamily), sceneEntry.statisticsIdentity};
        }

        VirtualShadowSceneCache sceneCache;
        RTScene* activePoolScene = nullptr;
        UInt32 nextViewID = 1;
        UInt64 nextSceneIdentity = 1;
        std::unordered_map<UInt32, ViewIDReservation> viewIDReservations;
        std::unordered_map<RTScene*, SceneEntry> scenes;
        std::vector<ViewRegistration> viewRegistrations;
        std::array<StatisticsReadbackSlot, RenderFrameContextCount> statisticsReadbackSlots;
        std::array<VirtualShadowSchedulingDiagnostics, RenderFrameContextCount> schedulingDiagnostics;
        rhi::RhiDevice* statisticsReadbackDevice = nullptr;
    };

    VirtualShadowManager::VirtualShadowManager(VirtualShadowSceneCacheDesc desc)
        : impl_(std::make_unique<Impl>(std::move(desc)))
    {
    }

    VirtualShadowManager::~VirtualShadowManager() = default;

    UInt32 VirtualShadowManager::GetOrAssignViewID(RTRenderViewState& viewState) noexcept
    {
        const UInt32 existingID = viewState.GetVirtualShadowViewID();
        if (existingID != InvalidVirtualShadowViewID)
        {
            if (existingID > VirtualShadowMaximumViewID)
            {
                return InvalidVirtualShadowViewID;
            }

            auto reservation = impl_->viewIDReservations.find(existingID);
            if (reservation != impl_->viewIDReservations.end())
            {
                const std::shared_ptr<const void> reservedToken = reservation->second.lifetimeToken.lock();
                return reservedToken != nullptr && reservedToken == viewState.virtualShadowLifetimeToken_ ? existingID : InvalidVirtualShadowViewID;
            }

            try
            {
                impl_->viewIDReservations.emplace(existingID, Impl::ViewIDReservation{viewState.virtualShadowLifetimeToken_});
            }
            catch (...)
            {
                return InvalidVirtualShadowViewID;
            }
            if (existingID >= impl_->nextViewID)
            {
                impl_->nextViewID = existingID == VirtualShadowMaximumViewID ? VirtualShadowMaximumViewID + 1u : existingID + 1u;
            }
            return existingID;
        }

        while (impl_->nextViewID <= VirtualShadowMaximumViewID && impl_->viewIDReservations.contains(impl_->nextViewID))
        {
            ++impl_->nextViewID;
        }
        if (impl_->nextViewID > VirtualShadowMaximumViewID)
        {
            return InvalidVirtualShadowViewID;
        }

        const UInt32 assignedID = impl_->nextViewID;
        try
        {
            impl_->viewIDReservations.emplace(assignedID, Impl::ViewIDReservation{viewState.virtualShadowLifetimeToken_});
        }
        catch (...)
        {
            return InvalidVirtualShadowViewID;
        }
        ++impl_->nextViewID;
        if (!viewState.TryAssignVirtualShadowViewID(assignedID))
        {
            return InvalidVirtualShadowViewID;
        }
        return assignedID;
    }

    void VirtualShadowManager::AddToFrameGraph(const FrameRenderPipelineData& frameData,
                                               FrameGraph& frameGraph,
                                               RendererData& rendererData,
                                               RendererFrameGraphData& graphData)
    {
        VirtualShadowSchedulingDiagnostics& schedulingDiagnostics =
            impl_->BeginFrameGraphScheduling(frameData.frameIndex, rendererData.views.size());
        ValidateVirtualShadowFrameGraphInputs(frameData, rendererData, graphData);
        const std::shared_ptr<RTLight> selectedLight = GetRequiredShadowDirectionalLight(rendererData.scene);
        const VirtualShadowLightInput light = BuildRequiredVirtualShadowLightInput(selectedLight);

        CollectExpiredState();
        Impl::FamilyPreparationResult familyPreparation =
            impl_->PrepareFamilyGraphData(*this, frameData, rendererData, graphData, light, schedulingDiagnostics);
        Impl::StatisticsReadbackSlot& statisticsSlot =
            impl_->PrepareStatisticsReadback(*frameData.device, frameData.frameIndex, familyPreparation.statisticsIdentity, schedulingDiagnostics);

        VirtualShadowSceneCache& sceneCache = impl_->sceneCache;
        graphData.virtualShadowResources = ImportVirtualShadowFrameGraphResources(frameGraph, sceneCache, *statisticsSlot.buffer);
        familyPreparation.family.atlas = graphData.virtualShadowResources.atlas;
        familyPreparation.family.physicalPages = graphData.virtualShadowResources.physicalPages;
        familyPreparation.family.statistics = graphData.virtualShadowResources.statistics;
        graphData.virtualShadowPreparedFamily = std::move(familyPreparation.family);

        VirtualShadowPreparedFamilyGraphData& family = graphData.virtualShadowPreparedFamily;
        VirtualShadowFrameGraphResources& resources = graphData.virtualShadowResources;
        const bool familyRequiresUpdate =
            std::ranges::any_of(family.views, [](const VirtualShadowPreparedViewGraphData& view) { return view.packet.requiresRequestUpdate; });
        if (familyRequiresUpdate)
        {
            AddVirtualShadowStep1_ClearScenePass(frameGraph, family, resources);

            bool firstRequestClear = true;
            for (const VirtualShadowPreparedViewGraphData& view : family.views)
            {
                if (!view.packet.requiresRequestUpdate)
                {
                    continue;
                }
                AddVirtualShadowStep2_ClearViewRequestsPass(frameGraph, view, resources, firstRequestClear);
                firstRequestClear = false;
            }

            bool firstRequestUpdate = true;
            for (const VirtualShadowPreparedViewGraphData& view : family.views)
            {
                if (!view.packet.requiresRequestUpdate)
                {
                    continue;
                }
                AddVirtualShadowStep3_MarkRequestsPass(frameGraph, view, resources, firstRequestUpdate);
                firstRequestUpdate = false;
            }

            bool atlasWrittenThisFrame = false;
            for (SizeT viewIndex = 0; viewIndex < family.views.size(); ++viewIndex)
            {
                const VirtualShadowPreparedViewGraphData& view = family.views[viewIndex];
                AddVirtualShadowStep4_ResolvePageHitsPass(frameGraph, view, resources, true, !view.packet.requiresRequestUpdate);
                AddVirtualShadowStep5_AllocatePagesPass(frameGraph, view, resources, true);

                if (!view.packet.requiresPageRendering)
                {
                    continue;
                }

                AddVirtualShadowStep6_ClearPhysicalPagesPass(frameGraph, view, resources, atlasWrittenThisFrame);
                AddVirtualShadowStep7_RenderCastersPass(frameGraph, view, resources);
                AddVirtualShadowStep8_MarkRenderedPass(frameGraph, view, resources);
                atlasWrittenThisFrame = true;
            }
            family.physicalPages = resources.physicalPages;
            family.statistics = resources.statistics;
            AddVirtualShadowStep9_FinalizeScenePass(frameGraph, family, resources);
        }
        else
        {
            AddVirtualShadowStep10_ReuseStatisticsPass(frameGraph, resources);
        }
        AddVirtualShadowStep11_StatisticsReadbackPass(frameGraph, resources);

        PublishVirtualShadowFrameGraphResults(rendererData, graphData, family, resources);
        statisticsSlot.totalPhysicalPages = sceneCache.GetPhysicalPageCapacity();
    }

    VirtualShadowSchedulingDiagnostics VirtualShadowManager::GetSchedulingDiagnostics(UInt64 frameIndex) const noexcept
    {
        return impl_->GetSchedulingDiagnostics(frameIndex);
    }

    UInt64 VirtualShadowManager::GetRecordingSceneIdentity(UInt64 frameIndex) const noexcept
    {
        const Impl::StatisticsReadbackSlot& slot = impl_->GetStatisticsSlot(frameIndex);
        return slot.sourceFrameIndex == frameIndex && slot.copyRecorded ? slot.sceneIdentity : 0;
    }

    void VirtualShadowManager::NotifyFrameSubmitted(UInt64 frameIndex, rhi::RhiFence& completionFence, UInt64 fenceValue)
    {
        VE_ASSERT_RENDER_THREAD();
        Impl::StatisticsReadbackSlot& slot = impl_->GetStatisticsSlot(frameIndex);
        if (slot.sourceFrameIndex != frameIndex)
        {
            FailVirtualShadow("VSM statistics submission does not match the prepared frame.");
        }
        if (!slot.copyRecorded)
        {
            FailVirtualShadow("VSM statistics submission requires a recorded readback copy.");
        }
        if (slot.completionFence != nullptr || slot.submittedFenceValue != 0)
        {
            FailVirtualShadow("VSM statistics were submitted more than once for one frame.");
        }
        if (fenceValue == 0)
        {
            FailVirtualShadow("VSM statistics submission requires a non-zero fence value.");
        }
        slot.completionFence = &completionFence;
        slot.submittedFenceValue = fenceValue;
    }

    std::optional<RenderPerformanceStatistics> VirtualShadowManager::ConsumeCompletedFrameStatistics(rhi::RhiDevice& device, UInt64 frameIndex) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        Impl::StatisticsReadbackSlot& slot = impl_->GetStatisticsSlot(frameIndex);
        if (slot.sourceFrameIndex == 0)
        {
            if (slot.sceneIdentity != 0 || slot.totalPhysicalPages != 0 || slot.completionFence != nullptr || slot.submittedFenceValue != 0 ||
                slot.copyRecorded)
            {
                FailVirtualShadow("VSM statistics encountered an inconsistent cleared readback slot.");
            }
            return std::nullopt;
        }
        if (slot.sourceFrameIndex != frameIndex)
        {
            FailVirtualShadow("VSM statistics consumption does not match the recorded frame.");
        }
        if (!slot.copyRecorded)
        {
            FailVirtualShadow("VSM statistics consumption requires a recorded readback copy.");
        }
        if (impl_->statisticsReadbackDevice != &device)
        {
            FailVirtualShadow("VSM statistics consumption used a different RHI device than recording.");
        }
        if (slot.completionFence == nullptr || slot.submittedFenceValue == 0)
        {
            return std::nullopt;
        }
        if (slot.completionFence->GetCompletedValue() < slot.submittedFenceValue)
        {
            return std::nullopt;
        }
        if (slot.buffer == nullptr)
        {
            FailVirtualShadow("VSM completed statistics slot has no readback buffer.");
        }

        RenderPerformanceStatistics result;
        result.sourceFrameIndex = frameIndex;
        result.sceneIdentity = slot.sceneIdentity;
        VirtualShadowGpuStatistics gpuStatistics = {};
        if (!device.ReadBuffer(*slot.buffer, 0, &gpuStatistics, sizeof(gpuStatistics)))
        {
            FailVirtualShadow("VSM failed to map a completed statistics readback.");
        }
        result.virtualShadow.available = true;
        result.virtualShadow.totalPhysicalPages = slot.totalPhysicalPages;
        result.virtualShadow.allocatedPhysicalPages = gpuStatistics.allocatedPhysicalPages;
        result.virtualShadow.requestedPages = gpuStatistics.requestedPages;
        result.virtualShadow.cachedPages = gpuStatistics.cachedPages;
        result.virtualShadow.newlyAllocatedPages = gpuStatistics.newlyAllocatedPages;
        result.virtualShadow.redrawnPages = gpuStatistics.redrawnPages;
        result.virtualShadow.unmappedPages = gpuStatistics.unmappedPages;

        slot.sourceFrameIndex = 0;
        slot.sceneIdentity = 0;
        slot.totalPhysicalPages = 0;
        slot.completionFence = nullptr;
        slot.submittedFenceValue = 0;
        slot.copyRecorded = false;
        return result;
    }

    void VirtualShadowManager::CollectExpiredState()
    {
        for (auto iterator = impl_->viewRegistrations.begin(); iterator != impl_->viewRegistrations.end();)
        {
            if (!iterator->view.expired())
            {
                ++iterator;
                continue;
            }

            impl_->sceneCache.GetSliceAllocator().Release(iterator->viewID);
            iterator = impl_->viewRegistrations.erase(iterator);
        }

        for (auto iterator = impl_->scenes.begin(); iterator != impl_->scenes.end();)
        {
            if (!iterator->second.scene.expired())
            {
                ++iterator;
                continue;
            }

            RTScene* expiredScene = iterator->first;
            if (impl_->activePoolScene == expiredScene)
            {
                impl_->activePoolScene = nullptr;
            }
            for (Impl::ViewRegistration& registration : impl_->viewRegistrations)
            {
                if (registration.scene != expiredScene)
                {
                    continue;
                }
                impl_->sceneCache.GetSliceAllocator().Release(registration.viewID);
                if (const std::shared_ptr<RTRenderViewState> view = registration.view.lock())
                {
                    view->ClearVirtualShadowPageTableSlice();
                }
                registration.scene = nullptr;
            }
            iterator = impl_->scenes.erase(iterator);
        }
    }
} // namespace ve
