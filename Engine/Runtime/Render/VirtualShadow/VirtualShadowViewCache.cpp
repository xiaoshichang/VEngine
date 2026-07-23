#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Render/RenderCameraMath.h"
#include "Engine/Runtime/Render/RenderScene.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace ve
{
    VirtualShadowViewCache::~VirtualShadowViewCache() = default;

    namespace
    {
        bool TryInvertMatrix(const Matrix44& matrix, Matrix44& inverse) noexcept
        {
            Float32 augmented[4][8] = {};
            for (SizeT row = 0; row < 4; ++row)
            {
                for (SizeT column = 0; column < 4; ++column)
                {
                    augmented[row][column] = matrix.Get(row, column);
                    augmented[row][column + 4] = row == column ? 1.0f : 0.0f;
                }
            }

            for (SizeT pivotColumn = 0; pivotColumn < 4; ++pivotColumn)
            {
                SizeT pivotRow = pivotColumn;
                for (SizeT row = pivotColumn + 1; row < 4; ++row)
                {
                    if (std::abs(augmented[row][pivotColumn]) > std::abs(augmented[pivotRow][pivotColumn]))
                    {
                        pivotRow = row;
                    }
                }
                if (std::abs(augmented[pivotRow][pivotColumn]) < 1.0e-8f)
                {
                    return false;
                }
                if (pivotRow != pivotColumn)
                {
                    for (SizeT column = 0; column < 8; ++column)
                    {
                        std::swap(augmented[pivotRow][column], augmented[pivotColumn][column]);
                    }
                }

                const Float32 divisor = augmented[pivotColumn][pivotColumn];
                for (SizeT column = 0; column < 8; ++column)
                {
                    augmented[pivotColumn][column] /= divisor;
                }
                for (SizeT row = 0; row < 4; ++row)
                {
                    if (row == pivotColumn)
                    {
                        continue;
                    }
                    const Float32 factor = augmented[row][pivotColumn];
                    for (SizeT column = 0; column < 8; ++column)
                    {
                        augmented[row][column] -= factor * augmented[pivotColumn][column];
                    }
                }
            }

            inverse = Matrix44::Zero();
            for (SizeT row = 0; row < 4; ++row)
            {
                for (SizeT column = 0; column < 4; ++column)
                {
                    inverse.Set(row, column, augmented[row][column + 4]);
                }
            }
            return true;
        }

        Aabb TransformBoundsToLightSpace(const VirtualShadowLightBasis& basis, const Aabb& worldBounds)
        {
            const Vector3& minimum = worldBounds.GetMinimum();
            const Vector3& maximum = worldBounds.GetMaximum();
            Vector3 lightMinimum(std::numeric_limits<Float32>::max(), std::numeric_limits<Float32>::max(), std::numeric_limits<Float32>::max());
            Vector3 lightMaximum(std::numeric_limits<Float32>::lowest(), std::numeric_limits<Float32>::lowest(), std::numeric_limits<Float32>::lowest());
            for (UInt32 cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
            {
                const Vector3 corner((cornerIndex & 1u) != 0u ? maximum.GetX() : minimum.GetX(),
                                     (cornerIndex & 2u) != 0u ? maximum.GetY() : minimum.GetY(),
                                     (cornerIndex & 4u) != 0u ? maximum.GetZ() : minimum.GetZ());
                const Vector3 lightCorner = basis.TransformPoint(corner);
                lightMinimum.SetX(std::min(lightMinimum.GetX(), lightCorner.GetX()));
                lightMinimum.SetY(std::min(lightMinimum.GetY(), lightCorner.GetY()));
                lightMinimum.SetZ(std::min(lightMinimum.GetZ(), lightCorner.GetZ()));
                lightMaximum.SetX(std::max(lightMaximum.GetX(), lightCorner.GetX()));
                lightMaximum.SetY(std::max(lightMaximum.GetY(), lightCorner.GetY()));
                lightMaximum.SetZ(std::max(lightMaximum.GetZ(), lightCorner.GetZ()));
            }
            return Aabb(lightMinimum, lightMaximum);
        }

        Matrix44 BuildPageViewProjection(const VirtualShadowLightBasis& basis, const Aabb& pageBounds) noexcept
        {
            const Vector3& minimum = pageBounds.GetMinimum();
            const Vector3& maximum = pageBounds.GetMaximum();
            Matrix44 worldToLight = Matrix44::Identity();
            worldToLight.Set(0, 0, basis.right.GetX());
            worldToLight.Set(0, 1, basis.right.GetY());
            worldToLight.Set(0, 2, basis.right.GetZ());
            worldToLight.Set(1, 0, basis.up.GetX());
            worldToLight.Set(1, 1, basis.up.GetY());
            worldToLight.Set(1, 2, basis.up.GetZ());
            worldToLight.Set(2, 0, basis.forward.GetX());
            worldToLight.Set(2, 1, basis.forward.GetY());
            worldToLight.Set(2, 2, basis.forward.GetZ());

            const Float32 width = maximum.GetX() - minimum.GetX();
            const Float32 height = maximum.GetY() - minimum.GetY();
            const Float32 depth = maximum.GetZ() - minimum.GetZ();
            Matrix44 projection = Matrix44::Zero();
            projection.Set(0, 0, 2.0f / width);
            projection.Set(0, 3, -(maximum.GetX() + minimum.GetX()) / width);
            projection.Set(1, 1, 2.0f / height);
            projection.Set(1, 3, -(maximum.GetY() + minimum.GetY()) / height);
            projection.Set(2, 2, 1.0f / depth);
            projection.Set(2, 3, -minimum.GetZ() / depth);
            projection.Set(3, 3, 1.0f);
            return projection * worldToLight;
        }
    } // namespace

    VirtualShadowViewCache::VirtualShadowViewCache(UInt32 atlasExtent)
        : atlasExtent_(atlasExtent)
        , pageCache_(std::min(GetVirtualShadowPhysicalPageCapacity(atlasExtent), VirtualShadowPageTableCapacity))
    {
    }

    VirtualShadowFramePacket VirtualShadowViewCache::PrepareFrame(const VirtualShadowPrepareInput& input)
    {
        VirtualShadowFramePacket packet;
        packet.atlasExtent = atlasExtent_;
        pageCache_.BeginFrame(input.frameIndex);

        if (!hasCameraCutRevision_)
        {
            lastCameraCutRevision_ = input.cameraCutRevision;
            hasCameraCutRevision_ = true;
        }
        else if (lastCameraCutRevision_ != input.cameraCutRevision)
        {
            pageCache_.ClearRequestHistory();
            lastCameraCutRevision_ = input.cameraCutRevision;
        }

        if (pageCache_.GetCapacity() == 0)
        {
            packet.valid = false;
            return packet;
        }

        if (!input.light.enabled)
        {
            return packet;
        }

        packet.clipmaps = BuildVirtualShadowClipmaps(input.cameraLocalToWorld, input.light.direction, input.light.shadowDistance);
        if (!packet.clipmaps.valid)
        {
            packet.valid = false;
            return packet;
        }

        if (!hasShadowDistance_)
        {
            lastShadowDistance_ = input.light.shadowDistance;
            hasShadowDistance_ = true;
        }
        else if (!NearlyEqual(lastShadowDistance_, input.light.shadowDistance))
        {
            pageCache_.InvalidateAll();
            lastShadowDistance_ = input.light.shadowDistance;
        }

        packet.enabled = true;
        packet.depthBias = input.light.depthBias;
        packet.normalBias = input.light.normalBias;

        std::vector<VirtualShadowReceiver> receivers;
        std::vector<VirtualShadowCasterSnapshot> casters;
        receivers.reserve(input.items.size());
        casters.reserve(input.items.size());
        for (const VirtualShadowSceneItem& item : input.items)
        {
            if (!item.worldBounds.IsFiniteAndValid())
            {
                continue;
            }
            if (item.receiveShadows)
            {
                receivers.push_back({item.renderItemID, item.worldBounds, true});
            }
            if (item.opaque && item.castShadows)
            {
                casters.push_back({item.renderItemID, item.revision, item.worldBounds, true});
            }
        }

        const VirtualShadowInvalidationResult invalidation = invalidationTracker_.Update(input.frameIndex, packet.clipmaps, input.light.direction, casters);
        if (invalidation.fullInvalidation)
        {
            pageCache_.InvalidateAll();
        }
        for (VirtualShadowPageKey key : invalidation.invalidatedKeys)
        {
            (void)pageCache_.Invalidate(key);
        }

        const VirtualShadowRequestBuildInput requestInput{input.viewProjection, input.cameraLocalToWorld, packet.clipmaps, receivers};
        const std::vector<VirtualShadowPageRequest> requests = BuildVirtualShadowPageRequests(requestInput);
        const VirtualShadowRequestResolution resolution = pageCache_.ResolveRequests(requests);

        const std::vector<UInt32> dirtyPageIndices = pageCache_.GetDirtyPhysicalPageIndices();
        const std::span<const VirtualShadowPhysicalPage> physicalPages = pageCache_.GetPhysicalPages();
        packet.dirtyPages.reserve(dirtyPageIndices.size());
        for (UInt32 physicalPageIndex : dirtyPageIndices)
        {
            const VirtualShadowPhysicalPage& physicalPage = physicalPages[physicalPageIndex];
            const VirtualShadowClipmapLevel& level = packet.clipmaps.levels[physicalPage.key.GetClipmapLevel()];
            const Float32 gutterWorldSize =
                level.pageWorldSize * static_cast<Float32>(VirtualShadowPageGutter) / static_cast<Float32>(VirtualShadowPhysicalPageContentSize);
            const Aabb pageBounds = GetVirtualShadowPageLightSpaceBounds(packet.clipmaps, physicalPage.key, gutterWorldSize);

            VirtualShadowDirtyPageDraw draw;
            draw.key = physicalPage.key;
            draw.physicalPageIndex = physicalPageIndex;
            draw.physicalOrigin = GetVirtualShadowPhysicalPageOrigin(physicalPageIndex, atlasExtent_);
            draw.pageViewProjection = BuildPageViewProjection(packet.clipmaps.lightBasis, pageBounds);
            for (const VirtualShadowSceneItem& item : input.items)
            {
                if (!item.opaque || !item.castShadows || !item.worldBounds.IsFiniteAndValid() ||
                    !TransformBoundsToLightSpace(packet.clipmaps.lightBasis, item.worldBounds).Intersects(pageBounds))
                {
                    continue;
                }
                draw.casterRenderItemIDs.push_back(item.renderItemID);
                if (item.renderItem != nullptr)
                {
                    draw.casters.push_back(item.renderItem);
                }
            }
            packet.statistics.casterDraws += static_cast<UInt32>(draw.casterRenderItemIDs.size());
            packet.dirtyPages.push_back(std::move(draw));
        }

        packet.residentPageTable = pageCache_.BuildResidentPageTable();
        packet.statistics.requested = resolution.requested;
        packet.statistics.resident = pageCache_.GetResidentPageCount();
        packet.statistics.allocated = resolution.allocated;
        packet.statistics.cached = pageCache_.GetCachedPageCount();
        packet.statistics.dirty = pageCache_.GetDirtyPageCount();
        packet.statistics.evicted = resolution.evicted;
        packet.statistics.missing = resolution.missing;
        return packet;
    }

    VirtualShadowFramePacket VirtualShadowViewCache::PrepareFrame(
        UInt64 frameIndex, UInt64 cameraCutRevision, const RTCamera& camera, const RTScene& scene, UInt32 targetWidth, UInt32 targetHeight)
    {
        VirtualShadowLightInput light;
        for (SizeT lightIndex = 0; lightIndex < scene.GetLightCount(); ++lightIndex)
        {
            const std::shared_ptr<RTLight> sceneLight = scene.GetLight(lightIndex);
            if (sceneLight != nullptr && sceneLight->GetType() == RTLightType::Directional)
            {
                light = VirtualShadowLightInput{sceneLight->CastShadows(),
                                                sceneLight->GetDirection(),
                                                sceneLight->GetShadowDistance(),
                                                sceneLight->GetDepthBias(),
                                                sceneLight->GetNormalBias(),
                                                sceneLight->GetShadowRevision()};
                break;
            }
        }

        std::vector<VirtualShadowSceneItem> items;
        items.reserve(scene.GetRenderItemCount());
        for (SizeT itemIndex = 0; itemIndex < scene.GetRenderItemCount(); ++itemIndex)
        {
            const std::shared_ptr<RTRenderItem> renderItem = scene.GetRenderItem(itemIndex);
            if (renderItem == nullptr)
            {
                continue;
            }
            const Aabb localBounds = Aabb::FromCenterExtents(renderItem->GetBoundsCenter(), renderItem->GetBoundsExtents());
            const std::shared_ptr<RTMaterialResource> material = std::dynamic_pointer_cast<RTMaterialResource>(renderItem->GetMaterialResource());
            const bool opaque = material != nullptr && material->GetDesc().renderQueue == RenderQueue::Opaque;
            items.push_back({renderItem->GetRenderItemID(),
                             renderItem->GetRevision(),
                             localBounds.Transformed(renderItem->GetLocalToWorld()),
                             opaque,
                             renderItem->CastShadows(),
                             renderItem->ReceiveShadows(),
                             renderItem.get()});
        }

        VirtualShadowPrepareInput input;
        input.frameIndex = frameIndex;
        input.cameraCutRevision = cameraCutRevision;
        input.cameraLocalToWorld = camera.GetLocalToWorld();
        input.viewProjection = BuildCameraViewProjection(camera, rhi::RhiExtent2D{targetWidth, targetHeight});
        input.light = light;
        input.items = items;
        return PrepareFrame(input);
    }

    VirtualShadowFramePacket VirtualShadowViewCache::PrepareGpuFrame(const VirtualShadowPrepareInput& input)
    {
        VirtualShadowFramePacket packet;
        packet.atlasExtent = atlasExtent_;
        packet.frameIndex = input.frameIndex;
        packet.screenWidth = input.screenWidth;
        packet.screenHeight = input.screenHeight;
        const Matrix44& cameraLocalToWorld = input.cameraLocalToWorld;
        packet.cameraWorldPosition = Vector4(cameraLocalToWorld.Get(0, 3), cameraLocalToWorld.Get(1, 3), cameraLocalToWorld.Get(2, 3), 1.0f);
        Vector3 cameraForward = cameraLocalToWorld.TransformDirection(Vector3::UnitZ()).Normalized();
        if (cameraForward.LengthSquared() == 0.0f)
        {
            cameraForward = Vector3::UnitZ();
        }
        packet.cameraWorldForward = Vector4(cameraForward, 0.0f);
        if (!input.light.enabled || GetGpuPhysicalPageCapacity() == 0)
        {
            return packet;
        }

        packet.clipmaps = BuildVirtualShadowClipmaps(input.cameraLocalToWorld, input.light.direction, input.light.shadowDistance);
        if (!packet.clipmaps.valid)
        {
            packet.valid = false;
            return packet;
        }

        Matrix44 inverseViewProjection = Matrix44::Identity();
        if (!TryInvertMatrix(input.viewProjection, inverseViewProjection))
        {
            packet.valid = false;
            return packet;
        }

        const bool resetForShadowDistance = !hasShadowDistance_ || !NearlyEqual(lastShadowDistance_, input.light.shadowDistance);
        lastCameraCutRevision_ = input.cameraCutRevision;
        hasCameraCutRevision_ = true;
        lastShadowDistance_ = input.light.shadowDistance;
        hasShadowDistance_ = true;

        packet.enabled = true;
        packet.gpuDriven = true;
        packet.depthBias = input.light.depthBias;
        packet.normalBias = input.light.normalBias;
        packet.inverseViewProjection = inverseViewProjection.Transposed();

        std::vector<VirtualShadowCasterSnapshot> casters;
        casters.reserve(input.items.size());
        for (const VirtualShadowSceneItem& item : input.items)
        {
            if (item.opaque && item.castShadows && item.worldBounds.IsFiniteAndValid())
            {
                casters.push_back({item.renderItemID, item.revision, item.worldBounds, true});
            }
        }
        const VirtualShadowInvalidationResult invalidation = invalidationTracker_.Update(input.frameIndex,
                                                                                         packet.clipmaps,
                                                                                         input.light.direction,
                                                                                         casters,
                                                                                         VirtualShadowInvalidationCoverage::AllAbsolutePages,
                                                                                         VirtualShadowPageTableCapacity);
        if (invalidation.invalidatedKeys.size() > VirtualShadowPageTableCapacity)
        {
            packet.invalidateAllGpuPages = true;
        }
        else
        {
            packet.invalidatedPageKeys = invalidation.invalidatedKeys;
        }
        packet.resetGpuCache = invalidation.fullInvalidation || resetForShadowDistance || gpuCacheResetPending_;
        return packet;
    }

    VirtualShadowFramePacket VirtualShadowViewCache::PrepareGpuFrame(
        UInt64 frameIndex, UInt64 cameraCutRevision, const RTCamera& camera, const RTScene& scene, UInt32 targetWidth, UInt32 targetHeight)
    {
        VirtualShadowLightInput light;
        for (SizeT lightIndex = 0; lightIndex < scene.GetLightCount(); ++lightIndex)
        {
            const std::shared_ptr<RTLight> sceneLight = scene.GetLight(lightIndex);
            if (sceneLight != nullptr && sceneLight->GetType() == RTLightType::Directional)
            {
                light = VirtualShadowLightInput{sceneLight->CastShadows(),
                                                sceneLight->GetDirection(),
                                                sceneLight->GetShadowDistance(),
                                                sceneLight->GetDepthBias(),
                                                sceneLight->GetNormalBias(),
                                                sceneLight->GetShadowRevision()};
                break;
            }
        }

        std::vector<VirtualShadowSceneItem> items;
        items.reserve(scene.GetRenderItemCount());
        for (SizeT itemIndex = 0; itemIndex < scene.GetRenderItemCount(); ++itemIndex)
        {
            const std::shared_ptr<RTRenderItem> renderItem = scene.GetRenderItem(itemIndex);
            if (renderItem == nullptr)
            {
                continue;
            }
            const Aabb localBounds = Aabb::FromCenterExtents(renderItem->GetBoundsCenter(), renderItem->GetBoundsExtents());
            const std::shared_ptr<RTMaterialResource> material = std::dynamic_pointer_cast<RTMaterialResource>(renderItem->GetMaterialResource());
            const bool opaque = material != nullptr && material->GetDesc().renderQueue == RenderQueue::Opaque;
            items.push_back({renderItem->GetRenderItemID(),
                             renderItem->GetRevision(),
                             localBounds.Transformed(renderItem->GetLocalToWorld()),
                             opaque,
                             renderItem->CastShadows(),
                             renderItem->ReceiveShadows(),
                             renderItem.get()});
        }

        VirtualShadowPrepareInput input;
        input.frameIndex = frameIndex;
        input.cameraCutRevision = cameraCutRevision;
        input.screenWidth = targetWidth;
        input.screenHeight = targetHeight;
        input.cameraLocalToWorld = camera.GetLocalToWorld();
        input.viewProjection = BuildCameraViewProjection(camera, rhi::RhiExtent2D{targetWidth, targetHeight});
        input.light = light;
        input.items = items;
        return PrepareGpuFrame(input);
    }

    bool VirtualShadowViewCache::EnsureSamplingPageTable(rhi::RhiDevice& device, const std::string& viewName)
    {
        if (resourceDevice_ != &device)
        {
            comparisonSampler_.reset();
            atlasTexture_.reset();
            gpuPageMarksBuffer_.reset();
            gpuPageTableBuffer_.reset();
            gpuRequestListBuffer_.reset();
            gpuRequestCountsBuffer_.reset();
            samplingPageTableBuffer_.reset();
            gpuPhysicalPagesBuffer_.reset();
            resourceDevice_ = &device;
            gpuDrivenAvailable_ = true;
            gpuCacheResetPending_ = true;
        }

        if (samplingPageTableBuffer_ == nullptr)
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = sizeof(UInt32);
            desc.usage = rhi::RhiBufferUsage::Storage;
            desc.memoryUsage = rhi::RhiBufferMemoryUsage::GpuOnly;
            desc.structureStride = sizeof(UInt32);
            const std::string debugName = viewName + ".VirtualShadowSamplingPageTable";
            desc.debugName = debugName.c_str();
            samplingPageTableBuffer_ = device.CreateBuffer(desc);
        }
        return samplingPageTableBuffer_ != nullptr;
    }

    bool VirtualShadowViewCache::EnsureGpuResources(rhi::RhiDevice& device, const std::string& viewName)
    {
        if (!EnsureSamplingPageTable(device, viewName))
        {
            return false;
        }

        if (atlasTexture_ == nullptr)
        {
            rhi::RhiTextureDesc atlasDesc = {};
            atlasDesc.width = atlasExtent_;
            atlasDesc.height = atlasExtent_;
            atlasDesc.format = rhi::RhiFormat::Depth32Float;
            atlasDesc.usage =
                static_cast<rhi::RhiTextureUsage>(static_cast<UInt32>(rhi::RhiTextureUsage::DepthStencil) | static_cast<UInt32>(rhi::RhiTextureUsage::Sampled));
            const std::string debugName = viewName + ".VirtualShadowAtlas";
            atlasDesc.debugName = debugName.c_str();
            atlasTexture_ = device.CreateTexture(atlasDesc);
        }

        if (comparisonSampler_ == nullptr)
        {
            rhi::RhiSamplerDesc samplerDesc = {};
            samplerDesc.filter = rhi::RhiSamplerFilter::Point;
            samplerDesc.addressU = rhi::RhiSamplerAddressMode::Clamp;
            samplerDesc.addressV = rhi::RhiSamplerAddressMode::Clamp;
            samplerDesc.addressW = rhi::RhiSamplerAddressMode::Clamp;
            samplerDesc.reductionMode = rhi::RhiSamplerReductionMode::Comparison;
            samplerDesc.comparisonFunction = rhi::RhiCompareFunction::LessEqual;
            comparisonSampler_ = device.CreateSampler(samplerDesc);
        }

        const auto createStorageBuffer = [&device](UInt64 size, UInt32 stride, const char* debugName)
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = size;
            desc.usage = rhi::RhiBufferUsage::Storage;
            desc.memoryUsage = rhi::RhiBufferMemoryUsage::GpuOnly;
            desc.structureStride = stride;
            desc.debugName = debugName;
            return device.CreateBuffer(desc);
        };
        if (gpuPageMarksBuffer_ == nullptr)
        {
            gpuPageMarksBuffer_ =
                createStorageBuffer(static_cast<UInt64>(VirtualShadowLogicalPageCount) * sizeof(UInt32), sizeof(UInt32), "VirtualShadowPageMarks");
            gpuCacheResetPending_ = true;
        }
        if (gpuPageTableBuffer_ == nullptr)
        {
            gpuPageTableBuffer_ =
                createStorageBuffer(static_cast<UInt64>(VirtualShadowLogicalPageCount) * sizeof(UInt32), sizeof(UInt32), "VirtualShadowDensePageTable");
            gpuCacheResetPending_ = true;
        }
        if (gpuRequestListBuffer_ == nullptr)
        {
            gpuRequestListBuffer_ =
                createStorageBuffer(static_cast<UInt64>(VirtualShadowLogicalPageCount) * sizeof(UInt32), sizeof(UInt32), "VirtualShadowRequestList");
            gpuCacheResetPending_ = true;
        }
        if (gpuRequestCountsBuffer_ == nullptr)
        {
            gpuRequestCountsBuffer_ =
                createStorageBuffer(static_cast<UInt64>(VirtualShadowClipmapLevelCount) * sizeof(UInt32), sizeof(UInt32), "VirtualShadowRequestCounts");
            gpuCacheResetPending_ = true;
        }
        if (gpuPhysicalPagesBuffer_ == nullptr)
        {
            gpuPhysicalPagesBuffer_ = createStorageBuffer(static_cast<UInt64>(GetGpuPhysicalPageCapacity()) * sizeof(VirtualShadowGpuPhysicalPage),
                                                          sizeof(VirtualShadowGpuPhysicalPage),
                                                          "VirtualShadowPhysicalPages");
            gpuCacheResetPending_ = true;
        }

        return atlasTexture_ != nullptr && comparisonSampler_ != nullptr && gpuPageMarksBuffer_ != nullptr && gpuPageTableBuffer_ != nullptr &&
               gpuRequestListBuffer_ != nullptr && gpuRequestCountsBuffer_ != nullptr && gpuPhysicalPagesBuffer_ != nullptr;
    }

    bool VirtualShadowViewCache::CanUseGpuDriven(const rhi::RhiDevice& device) const noexcept
    {
        return resourceDevice_ != &device || gpuDrivenAvailable_;
    }

    void VirtualShadowViewCache::DisableGpuDriven() noexcept
    {
        gpuDrivenAvailable_ = false;
        pageCache_.InvalidateAll();
    }

    void VirtualShadowViewCache::MarkRendered(std::span<const VirtualShadowPageKey> keys)
    {
        pageCache_.MarkRendered(keys);
    }

    UInt32 VirtualShadowViewCache::GetAtlasExtent() const noexcept
    {
        return atlasExtent_;
    }

    rhi::RhiTexture* VirtualShadowViewCache::GetAtlasTexture() noexcept
    {
        return atlasTexture_.get();
    }

    const rhi::RhiTexture* VirtualShadowViewCache::GetAtlasTexture() const noexcept
    {
        return atlasTexture_.get();
    }

    rhi::RhiSampler* VirtualShadowViewCache::GetComparisonSampler() noexcept
    {
        return comparisonSampler_.get();
    }

    const rhi::RhiSampler* VirtualShadowViewCache::GetComparisonSampler() const noexcept
    {
        return comparisonSampler_.get();
    }

    rhi::RhiBuffer* VirtualShadowViewCache::GetGpuPageMarksBuffer() noexcept
    {
        return gpuPageMarksBuffer_.get();
    }

    rhi::RhiBuffer* VirtualShadowViewCache::GetGpuPageTableBuffer() noexcept
    {
        return gpuPageTableBuffer_.get();
    }

    rhi::RhiBuffer* VirtualShadowViewCache::GetGpuRequestListBuffer() noexcept
    {
        return gpuRequestListBuffer_.get();
    }

    rhi::RhiBuffer* VirtualShadowViewCache::GetGpuRequestCountsBuffer() noexcept
    {
        return gpuRequestCountsBuffer_.get();
    }

    rhi::RhiBuffer* VirtualShadowViewCache::GetSamplingPageTableBuffer() noexcept
    {
        return gpuPageTableBuffer_ != nullptr ? gpuPageTableBuffer_.get() : samplingPageTableBuffer_.get();
    }

    rhi::RhiBuffer* VirtualShadowViewCache::GetGpuPhysicalPagesBuffer() noexcept
    {
        return gpuPhysicalPagesBuffer_.get();
    }

    UInt32 VirtualShadowViewCache::GetGpuPhysicalPageCapacity() const noexcept
    {
        return std::min(GetVirtualShadowPhysicalPageCapacity(atlasExtent_), VirtualShadowPageTableCapacity);
    }

    bool VirtualShadowViewCache::ConsumeGpuCacheReset() noexcept
    {
        const bool reset = gpuCacheResetPending_;
        gpuCacheResetPending_ = false;
        return reset;
    }

    VirtualShadowPageCache& VirtualShadowViewCache::GetPageCache() noexcept
    {
        return pageCache_;
    }

    const VirtualShadowPageCache& VirtualShadowViewCache::GetPageCache() const noexcept
    {
        return pageCache_;
    }

    VirtualShadowGpuConstants BuildVirtualShadowGpuConstants(const VirtualShadowFramePacket& packet) noexcept
    {
        VirtualShadowGpuConstants constants = {};
        if (!packet.valid || !packet.enabled || !packet.clipmaps.valid || packet.atlasExtent == 0)
        {
            return constants;
        }

        const VirtualShadowLightBasis& basis = packet.clipmaps.lightBasis;
        constants.lightRight = Vector4(basis.right, 0.0f);
        constants.lightUp = Vector4(basis.up, 0.0f);
        constants.lightDirection = Vector4(basis.forward, 0.0f);
        const Float32 normalizedDepthBias = ConvertVirtualShadowWorldDepthBiasToNormalized(packet.depthBias, packet.clipmaps.shadowDistance * 2.0f);
        constants.atlasAndBias = Vector4(1.0f / static_cast<Float32>(packet.atlasExtent),
                                         normalizedDepthBias,
                                         packet.normalBias,
                                         static_cast<Float32>(VirtualShadowPhysicalPageContentSize) / static_cast<Float32>(VirtualShadowPhysicalPageSize));
        for (UInt32 levelIndex = 0; levelIndex < VirtualShadowClipmapLevelCount; ++levelIndex)
        {
            const VirtualShadowClipmapLevel& level = packet.clipmaps.levels[levelIndex];
            const Float32 depthCenter = static_cast<Float32>(level.depthEpoch) * packet.clipmaps.depthStep;
            VirtualShadowGpuClipmap& gpuLevel = constants.clipmaps[levelIndex];
            gpuLevel.lightOriginAndPageWorldSize = Vector4(static_cast<Float32>(level.originPageX) * level.pageWorldSize,
                                                           static_cast<Float32>(level.originPageY) * level.pageWorldSize,
                                                           depthCenter,
                                                           level.pageWorldSize);
            gpuLevel.radiusAndDepthRange = Vector4(
                level.worldRadius, depthCenter - packet.clipmaps.shadowDistance, depthCenter + packet.clipmaps.shadowDistance, packet.clipmaps.depthStep);
            gpuLevel.originPageX = level.originPageX;
            gpuLevel.originPageY = level.originPageY;
            gpuLevel.depthEpoch = level.depthEpoch;
        }
        constants.enabled = 1;
        constants.atlasExtent = packet.atlasExtent;
        constants.inverseViewProjection = packet.inverseViewProjection;
        constants.screenWidth = packet.screenWidth;
        constants.screenHeight = packet.screenHeight;
        constants.physicalPageCapacity =
            packet.atlasExtent == 0 ? 0 : std::min(GetVirtualShadowPhysicalPageCapacity(packet.atlasExtent), VirtualShadowPageTableCapacity);
        constants.frameIndex = static_cast<UInt32>(packet.frameIndex);
        constants.resetCache = packet.resetGpuCache ? 1u : 0u;
        constants.gpuDriven = packet.gpuDriven ? 1u : 0u;
        constants.cameraWorldPosition = packet.cameraWorldPosition;
        constants.cameraWorldForward = packet.cameraWorldForward;
        if (packet.gpuDriven)
        {
            if (packet.invalidateAllGpuPages)
            {
                constants.invalidationCount = InvalidVirtualShadowGpuInvalidationCount;
            }
            else
            {
                constants.invalidationCount =
                    static_cast<UInt32>(std::min(packet.invalidatedPageKeys.size(), static_cast<SizeT>(VirtualShadowPageTableCapacity)));
                for (UInt32 keyIndex = 0; keyIndex < constants.invalidationCount; ++keyIndex)
                {
                    constants.entries[keyIndex].key0 = packet.invalidatedPageKeys[keyIndex].key0;
                    constants.entries[keyIndex].key1 = packet.invalidatedPageKeys[keyIndex].key1;
                }
            }
        }
        else
        {
            const std::span<const VirtualShadowGpuPageEntry> entries = packet.residentPageTable.GetGpuEntries();
            std::copy(entries.begin(), entries.end(), constants.entries);
        }
        return constants;
    }
} // namespace ve
