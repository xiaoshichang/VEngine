#pragma once

#include "Engine/Runtime/Math/Bounds.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h"

#include <span>
#include <vector>

namespace ve
{
    struct VirtualShadowLightInput
    {
        bool enabled = false;
        Vector3 direction = Vector3::UnitZ();
        Float32 shadowDistance = 200.0f;
        Float32 depthBias = 0.001f;
        Float32 normalBias = 0.05f;
    };

    struct VirtualShadowPrepareInput
    {
        UInt64 frameIndex = 0;
        UInt32 viewID = InvalidVirtualShadowViewID;
        UInt32 screenWidth = 0;
        UInt32 screenHeight = 0;
        Matrix44 viewProjection = Matrix44::Identity();
        Matrix44 cameraLocalToWorld = Matrix44::Identity();
        VirtualShadowLightInput light;
        std::span<const Aabb> changedCasterBounds;
        bool resetSceneCache = false;
    };

    struct VirtualShadowFramePacket
    {
        bool valid = true;
        UInt32 viewID = InvalidVirtualShadowViewID;
        UInt32 projectionRevision = 0;
        UInt32 atlasExtent = 0;
        Float32 depthBias = 0.0f;
        Float32 normalBias = 0.0f;
        VirtualShadowClipmapSet clipmaps;
        bool resetSceneCache = false;
        bool invalidateViewPages = false;
        /// True when the persistent receiver request list must be rebuilt from this frame's depth.
        bool requiresRequestUpdate = true;
        /// True when this view can produce new or dirty physical pages and therefore needs page raster passes.
        bool requiresPageRendering = true;
        UInt64 frameIndex = 0;
        UInt32 screenWidth = 0;
        UInt32 screenHeight = 0;
        Matrix44 inverseViewProjection = Matrix44::Identity();
        Vector4 cameraWorldPosition = Vector4::Zero();
        Vector4 cameraWorldForward = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
        std::vector<VirtualShadowPageKey> invalidatedPageKeys;
    };

    constexpr SizeT VirtualShadowMaxPendingInvalidationBoundCount = VirtualShadowMaxInvalidationPageCount;

    class VirtualShadowViewCache
    {
    public:
        VirtualShadowViewCache() = default;

        [[nodiscard]] VirtualShadowFramePacket PrepareFrame(const VirtualShadowPrepareInput& input);
        void QueueChangedCasterBounds(std::span<const Aabb> changedCasterBounds);

    private:
        UInt32 projectionRevision_ = 0;
        Float32 projectionShadowDistance_ = 0.0f;
        Vector3 projectionLightDirection_ = Vector3::UnitZ();
        Float32 projectionPageWorldSizes_[VirtualShadowClipmapLevelCount] = {};
        Int32 projectionDepthEpochs_[VirtualShadowClipmapLevelCount] = {};
        bool hasProjectionSignature_ = false;
        Matrix44 receiverViewProjection_ = Matrix44::Identity();
        Matrix44 receiverCameraLocalToWorld_ = Matrix44::Identity();
        bool hasReceiverSignature_ = false;
        std::vector<Aabb> pendingChangedCasterBounds_;
        bool pendingResetSceneCache_ = false;
        bool pendingInvalidateViewPages_ = false;
    };

    [[nodiscard]] VirtualShadowGpuConstants BuildVirtualShadowGpuConstants(const VirtualShadowFramePacket& packet) noexcept;
} // namespace ve
