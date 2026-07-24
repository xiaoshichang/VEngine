#pragma once

#include "Engine/Runtime/Math/Bounds.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ve
{
    class RTCamera;
    class RTScene;

    namespace rhi
    {
        class RhiBuffer;
        class RhiDevice;
        class RhiSampler;
        class RhiTexture;
    } // namespace rhi

    struct VirtualShadowLightInput
    {
        bool enabled = false;
        Vector3 direction = Vector3::UnitZ();
        Float32 shadowDistance = 200.0f;
        Float32 depthBias = 0.001f;
        Float32 normalBias = 0.05f;
    };

    struct VirtualShadowSceneItem
    {
        UInt64 renderItemID = 0;
        UInt64 revision = 0;
        Aabb worldBounds = Aabb(Vector3::Zero(), Vector3::Zero());
        bool opaque = true;
        bool castShadows = true;
    };

    struct VirtualShadowPrepareInput
    {
        UInt64 frameIndex = 0;
        UInt32 screenWidth = 0;
        UInt32 screenHeight = 0;
        Matrix44 viewProjection = Matrix44::Identity();
        Matrix44 cameraLocalToWorld = Matrix44::Identity();
        VirtualShadowLightInput light;
        std::span<const VirtualShadowSceneItem> items;
    };

    struct VirtualShadowFramePacket
    {
        bool valid = true;
        bool enabled = false;
        UInt32 atlasExtent = 0;
        Float32 depthBias = 0.0f;
        Float32 normalBias = 0.0f;
        VirtualShadowClipmapSet clipmaps;
        bool resetGpuCache = false;
        UInt64 frameIndex = 0;
        UInt32 screenWidth = 0;
        UInt32 screenHeight = 0;
        Matrix44 inverseViewProjection = Matrix44::Identity();
        Vector4 cameraWorldPosition = Vector4::Zero();
        Vector4 cameraWorldForward = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
        std::vector<VirtualShadowPageKey> invalidatedPageKeys;
        bool invalidateAllGpuPages = false;
    };

    class VirtualShadowViewCache
    {
    public:
        explicit VirtualShadowViewCache(UInt32 atlasExtent);
        ~VirtualShadowViewCache();

        [[nodiscard]] VirtualShadowFramePacket PrepareFrame(const VirtualShadowPrepareInput& input);
        [[nodiscard]] VirtualShadowFramePacket
        PrepareFrame(UInt64 frameIndex, const RTCamera& camera, const RTScene& scene, UInt32 targetWidth, UInt32 targetHeight);
        [[nodiscard]] bool EnsureSamplingResources(rhi::RhiDevice& device, const std::string& viewName);
        [[nodiscard]] bool EnsureGpuResources(rhi::RhiDevice& device, const std::string& viewName);
        [[nodiscard]] bool CanUseGpuShadows(const rhi::RhiDevice& device) const noexcept;
        void DisableGpuShadows() noexcept;

        [[nodiscard]] UInt32 GetAtlasExtent() const noexcept;
        [[nodiscard]] rhi::RhiTexture* GetAtlasTexture() noexcept;
        [[nodiscard]] const rhi::RhiTexture* GetAtlasTexture() const noexcept;
        [[nodiscard]] rhi::RhiTexture* GetFallbackAtlasTexture() noexcept;
        [[nodiscard]] const rhi::RhiTexture* GetFallbackAtlasTexture() const noexcept;
        [[nodiscard]] rhi::RhiSampler* GetComparisonSampler() noexcept;
        [[nodiscard]] const rhi::RhiSampler* GetComparisonSampler() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetGpuPageMarksBuffer() noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetGpuPageTableBuffer() noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetGpuRequestListBuffer() noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetGpuRequestCountsBuffer() noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetSamplingPageTableBuffer() noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetGpuPhysicalPagesBuffer() noexcept;
        [[nodiscard]] UInt32 GetGpuPhysicalPageCapacity() const noexcept;
        [[nodiscard]] bool ConsumeGpuCacheReset() noexcept;

    private:
        UInt32 atlasExtent_ = 0;
        VirtualShadowInvalidationTracker invalidationTracker_;
        Float32 lastShadowDistance_ = 0.0f;
        bool hasShadowDistance_ = false;
        rhi::RhiDevice* resourceDevice_ = nullptr;
        std::unique_ptr<rhi::RhiTexture> atlasTexture_;
        std::unique_ptr<rhi::RhiTexture> fallbackAtlasTexture_;
        std::unique_ptr<rhi::RhiSampler> comparisonSampler_;
        std::unique_ptr<rhi::RhiBuffer> gpuPageMarksBuffer_;
        std::unique_ptr<rhi::RhiBuffer> gpuPageTableBuffer_;
        std::unique_ptr<rhi::RhiBuffer> gpuRequestListBuffer_;
        std::unique_ptr<rhi::RhiBuffer> gpuRequestCountsBuffer_;
        std::unique_ptr<rhi::RhiBuffer> samplingPageTableBuffer_;
        std::unique_ptr<rhi::RhiBuffer> gpuPhysicalPagesBuffer_;
        bool gpuShadowsAvailable_ = true;
        bool gpuCacheResetPending_ = true;
    };

    [[nodiscard]] VirtualShadowGpuConstants BuildVirtualShadowGpuConstants(const VirtualShadowFramePacket& packet) noexcept;
} // namespace ve
