#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace ve
{
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

        [[nodiscard]] bool NearlyEqualMatrix(const Matrix44& left, const Matrix44& right) noexcept
        {
            for (SizeT row = 0; row < 4; ++row)
            {
                for (SizeT column = 0; column < 4; ++column)
                {
                    if (!NearlyEqual(left.Get(row, column), right.Get(row, column)))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] bool NearlyEqualDirection(const Vector3& left, const Vector3& right) noexcept
        {
            return NearlyEqual(left.GetX(), right.GetX()) && NearlyEqual(left.GetY(), right.GetY()) && NearlyEqual(left.GetZ(), right.GetZ());
        }
    } // namespace

    VirtualShadowFramePacket VirtualShadowViewCache::PrepareFrame(const VirtualShadowPrepareInput& input)
    {
        if (input.resetSceneCache)
        {
            pendingResetSceneCache_ = true;
            pendingInvalidateViewPages_ = false;
            std::vector<Aabb>().swap(pendingChangedCasterBounds_);
        }
        else if (!pendingResetSceneCache_ && !pendingInvalidateViewPages_)
        {
            for (const Aabb& changedBounds : input.changedCasterBounds)
            {
                if (!changedBounds.IsFiniteAndValid())
                {
                    continue;
                }
                if (pendingChangedCasterBounds_.size() >= VirtualShadowMaxPendingInvalidationBoundCount)
                {
                    pendingInvalidateViewPages_ = true;
                    std::vector<Aabb>().swap(pendingChangedCasterBounds_);
                    break;
                }
                pendingChangedCasterBounds_.push_back(changedBounds);
            }
        }

        VirtualShadowFramePacket packet;
        packet.viewID = input.viewID;
        packet.projectionRevision = projectionRevision_;
        packet.frameIndex = input.frameIndex;
        packet.screenWidth = input.screenWidth;
        packet.screenHeight = input.screenHeight;
        packet.cameraWorldPosition =
            Vector4(input.cameraLocalToWorld.Get(0, 3), input.cameraLocalToWorld.Get(1, 3), input.cameraLocalToWorld.Get(2, 3), 1.0f);
        Vector3 cameraForward = input.cameraLocalToWorld.TransformDirection(Vector3::UnitZ()).Normalized();
        if (cameraForward.LengthSquared() == 0.0f)
        {
            cameraForward = Vector3::UnitZ();
        }
        packet.cameraWorldForward = Vector4(cameraForward, 0.0f);
        if (packet.viewID > VirtualShadowMaximumViewID || packet.viewID == InvalidVirtualShadowViewID)
        {
            FailVirtualShadow("VSM requires a valid stable view ID.");
        }
        if (!input.light.enabled)
        {
            FailVirtualShadow("VSM requires an enabled shadow-casting directional light.");
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

        bool projectionCompatible = hasProjectionSignature_ && NearlyEqual(projectionShadowDistance_, packet.clipmaps.shadowDistance) &&
                                    NearlyEqualDirection(projectionLightDirection_, input.light.direction);
        if (hasProjectionSignature_ && projectionCompatible)
        {
            for (UInt32 levelIndex = 0; levelIndex < VirtualShadowClipmapLevelCount; ++levelIndex)
            {
                projectionCompatible &=
                    NearlyEqual(projectionPageWorldSizes_[levelIndex], packet.clipmaps.levels[levelIndex].pageWorldSize) &&
                    projectionDepthEpochs_[levelIndex] == packet.clipmaps.levels[levelIndex].depthEpoch;
            }
        }

        if (!hasProjectionSignature_ || !projectionCompatible)
        {
            projectionRevision_ = projectionRevision_ == std::numeric_limits<UInt32>::max() ? 1u : projectionRevision_ + 1u;
            packet.invalidateViewPages = hasProjectionSignature_;
            projectionShadowDistance_ = packet.clipmaps.shadowDistance;
            projectionLightDirection_ = input.light.direction;
            for (UInt32 levelIndex = 0; levelIndex < VirtualShadowClipmapLevelCount; ++levelIndex)
            {
                projectionPageWorldSizes_[levelIndex] = packet.clipmaps.levels[levelIndex].pageWorldSize;
                projectionDepthEpochs_[levelIndex] = packet.clipmaps.levels[levelIndex].depthEpoch;
            }
            hasProjectionSignature_ = true;
        }
        packet.projectionRevision = projectionRevision_;
        packet.invalidateViewPages |= pendingInvalidateViewPages_;
        packet.depthBias = input.light.depthBias;
        packet.normalBias = input.light.normalBias;
        packet.inverseViewProjection = inverseViewProjection.Transposed();
        packet.resetSceneCache = pendingResetSceneCache_;

        std::unordered_set<VirtualShadowPageKey, VirtualShadowPageKeyHash> invalidatedKeys;
        bool capacityExceeded = false;
        if (!packet.invalidateViewPages)
        {
            for (const Aabb& changedBounds : pendingChangedCasterBounds_)
            {
                const VirtualShadowPageKeyBuildResult projection =
                    BuildAbsoluteVirtualShadowPageKeysForBounds(
                        packet.viewID, packet.clipmaps, changedBounds, VirtualShadowMaxInvalidationPageCount);
                if (projection.exceededCapacity)
                {
                    capacityExceeded = true;
                    break;
                }
                for (const VirtualShadowPageKey key : projection.keys)
                {
                    if (!invalidatedKeys.contains(key) && invalidatedKeys.size() >= VirtualShadowMaxInvalidationPageCount)
                    {
                        capacityExceeded = true;
                        break;
                    }
                    invalidatedKeys.insert(key);
                }
                if (capacityExceeded)
                {
                    break;
                }
            }
        }
        if (capacityExceeded)
        {
            packet.invalidateViewPages = true;
        }
        if (!packet.invalidateViewPages)
        {
            packet.invalidatedPageKeys.assign(invalidatedKeys.begin(), invalidatedKeys.end());
            std::ranges::sort(packet.invalidatedPageKeys,
                              [](VirtualShadowPageKey left, VirtualShadowPageKey right)
                              { return left.key1 != right.key1 ? left.key1 < right.key1 : left.key0 < right.key0; });
        }

        const bool receiverChanged = !hasReceiverSignature_ || !NearlyEqualMatrix(receiverViewProjection_, input.viewProjection) ||
                                     !NearlyEqualMatrix(receiverCameraLocalToWorld_, input.cameraLocalToWorld);
        packet.requiresRequestUpdate =
            receiverChanged || packet.resetSceneCache || packet.invalidateViewPages || !packet.invalidatedPageKeys.empty();
        packet.requiresPageRendering = packet.requiresRequestUpdate;
        receiverViewProjection_ = input.viewProjection;
        receiverCameraLocalToWorld_ = input.cameraLocalToWorld;
        hasReceiverSignature_ = true;

        pendingChangedCasterBounds_.clear();
        pendingResetSceneCache_ = false;
        pendingInvalidateViewPages_ = false;
        return packet;
    }

    void VirtualShadowViewCache::QueueChangedCasterBounds(std::span<const Aabb> changedCasterBounds)
    {
        if (pendingResetSceneCache_ || pendingInvalidateViewPages_)
        {
            return;
        }
        for (const Aabb& changedBounds : changedCasterBounds)
        {
            if (!changedBounds.IsFiniteAndValid())
            {
                continue;
            }
            if (pendingChangedCasterBounds_.size() >= VirtualShadowMaxPendingInvalidationBoundCount)
            {
                pendingInvalidateViewPages_ = true;
                std::vector<Aabb>().swap(pendingChangedCasterBounds_);
                return;
            }
            pendingChangedCasterBounds_.push_back(changedBounds);
        }
    }

    VirtualShadowGpuConstants BuildVirtualShadowGpuConstants(const VirtualShadowFramePacket& packet) noexcept
    {
        VirtualShadowGpuConstants constants = {};
        if (!packet.valid || !packet.clipmaps.valid || packet.atlasExtent == 0)
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
                                         0.0f);
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
        constants.atlasExtent = packet.atlasExtent;
        constants.inverseViewProjection = packet.inverseViewProjection;
        constants.screenWidth = packet.screenWidth;
        constants.screenHeight = packet.screenHeight;
        constants.physicalPageCapacity = std::min(GetVirtualShadowPhysicalPageCapacity(packet.atlasExtent), VirtualShadowMaxPhysicalPageCount);
        constants.frameIndex = static_cast<UInt32>(packet.frameIndex);
        constants.cameraWorldPosition = packet.cameraWorldPosition;
        constants.cameraWorldForward = packet.cameraWorldForward;
        constants.viewID = packet.viewID;
        if (packet.invalidateViewPages)
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
