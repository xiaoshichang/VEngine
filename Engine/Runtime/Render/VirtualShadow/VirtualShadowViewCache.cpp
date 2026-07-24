#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Render/RenderCameraMath.h"
#include "Engine/Runtime/Render/RenderScene.h"

#include <algorithm>
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

    } // namespace

    VirtualShadowViewCache::VirtualShadowViewCache(UInt32 atlasExtent)
        : atlasExtent_(atlasExtent)
    {
    }

    VirtualShadowFramePacket VirtualShadowViewCache::PrepareFrame(const VirtualShadowPrepareInput& input)
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
        lastShadowDistance_ = input.light.shadowDistance;
        hasShadowDistance_ = true;

        packet.enabled = true;
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
                                                                                         VirtualShadowMaxInvalidationPageCount);
        if (invalidation.invalidatedKeys.size() > VirtualShadowMaxInvalidationPageCount)
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

    VirtualShadowFramePacket
    VirtualShadowViewCache::PrepareFrame(UInt64 frameIndex, const RTCamera& camera, const RTScene& scene, UInt32 targetWidth, UInt32 targetHeight)
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
                                                sceneLight->GetNormalBias()};
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
                             renderItem->CastShadows()});
        }

        VirtualShadowPrepareInput input;
        input.frameIndex = frameIndex;
        input.screenWidth = targetWidth;
        input.screenHeight = targetHeight;
        input.cameraLocalToWorld = camera.GetLocalToWorld();
        input.viewProjection = BuildCameraViewProjection(camera, rhi::RhiExtent2D{targetWidth, targetHeight});
        input.light = light;
        input.items = items;
        return PrepareFrame(input);
    }

    bool VirtualShadowViewCache::EnsureSamplingResources(rhi::RhiDevice& device, const std::string& viewName)
    {
        if (resourceDevice_ != &device)
        {
            comparisonSampler_.reset();
            atlasTexture_.reset();
            fallbackAtlasTexture_.reset();
            gpuPageMarksBuffer_.reset();
            gpuPageTableBuffer_.reset();
            gpuRequestListBuffer_.reset();
            gpuRequestCountsBuffer_.reset();
            samplingPageTableBuffer_.reset();
            gpuPhysicalPagesBuffer_.reset();
            resourceDevice_ = &device;
            gpuShadowsAvailable_ = true;
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

        if (fallbackAtlasTexture_ == nullptr)
        {
            rhi::RhiTextureDesc textureDesc = {};
            textureDesc.width = 1;
            textureDesc.height = 1;
            textureDesc.format = rhi::RhiFormat::Depth32Float;
            textureDesc.usage =
                static_cast<rhi::RhiTextureUsage>(static_cast<UInt32>(rhi::RhiTextureUsage::DepthStencil) | static_cast<UInt32>(rhi::RhiTextureUsage::Sampled));
            const std::string debugName = viewName + ".VirtualShadowFallbackAtlas";
            textureDesc.debugName = debugName.c_str();
            fallbackAtlasTexture_ = device.CreateTexture(textureDesc);
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

        return samplingPageTableBuffer_ != nullptr && fallbackAtlasTexture_ != nullptr && comparisonSampler_ != nullptr;
    }

    bool VirtualShadowViewCache::EnsureGpuResources(rhi::RhiDevice& device, const std::string& viewName)
    {
        if (!EnsureSamplingResources(device, viewName))
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

    bool VirtualShadowViewCache::CanUseGpuShadows(const rhi::RhiDevice& device) const noexcept
    {
        return resourceDevice_ != &device || gpuShadowsAvailable_;
    }

    void VirtualShadowViewCache::DisableGpuShadows() noexcept
    {
        gpuShadowsAvailable_ = false;
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

    rhi::RhiTexture* VirtualShadowViewCache::GetFallbackAtlasTexture() noexcept
    {
        return fallbackAtlasTexture_.get();
    }

    const rhi::RhiTexture* VirtualShadowViewCache::GetFallbackAtlasTexture() const noexcept
    {
        return fallbackAtlasTexture_.get();
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
        return std::min(GetVirtualShadowPhysicalPageCapacity(atlasExtent_), VirtualShadowMaxPhysicalPageCount);
    }

    bool VirtualShadowViewCache::ConsumeGpuCacheReset() noexcept
    {
        const bool reset = gpuCacheResetPending_;
        gpuCacheResetPending_ = false;
        return reset;
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
            packet.atlasExtent == 0 ? 0 : std::min(GetVirtualShadowPhysicalPageCapacity(packet.atlasExtent), VirtualShadowMaxPhysicalPageCount);
        constants.frameIndex = static_cast<UInt32>(packet.frameIndex);
        constants.resetCache = packet.resetGpuCache ? 1u : 0u;
        constants.cameraWorldPosition = packet.cameraWorldPosition;
        constants.cameraWorldForward = packet.cameraWorldForward;
        if (packet.invalidateAllGpuPages)
        {
            constants.invalidationCount = InvalidVirtualShadowGpuInvalidationCount;
        }
        else
        {
            constants.invalidationCount =
                static_cast<UInt32>(std::min(packet.invalidatedPageKeys.size(), static_cast<SizeT>(VirtualShadowMaxInvalidationPageCount)));
            for (UInt32 keyIndex = 0; keyIndex < constants.invalidationCount; ++keyIndex)
            {
                constants.invalidationEntries[keyIndex].key0 = packet.invalidatedPageKeys[keyIndex].key0;
                constants.invalidationEntries[keyIndex].key1 = packet.invalidatedPageKeys[keyIndex].key1;
            }
        }
        return constants;
    }
} // namespace ve
