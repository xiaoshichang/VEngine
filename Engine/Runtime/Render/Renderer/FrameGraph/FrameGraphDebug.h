#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"
#include "Engine/Runtime/Threading/Synchronization.h"

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ve
{
    inline constexpr UInt32 InvalidFrameGraphDebugPassIndex = std::numeric_limits<UInt32>::max();

    class FrameGraphDebugPreviewTexture;

    enum class FrameGraphDebugCaptureStatus
    {
        Idle,
        Armed,
        Capturing,
        Ready,
        Failed,
    };

    enum class FrameGraphDebugResourceKind
    {
        Texture,
        Buffer,
    };

    enum class FrameGraphDebugDependencyHazard
    {
        Raw,
        War,
        Waw,
    };

    enum class FrameGraphDebugPreviewState
    {
        Unavailable,
        Ready,
        Failed,
    };

    enum class FrameGraphDebugPassType
    {
        Raster,
        Compute,
    };

    struct FrameGraphDebugCaptureRequest
    {
        UInt64 requestId = 0;
        Float32 previewScale = 0.5F;
    };

    struct FrameGraphDebugDependency
    {
        UInt32 beforePass = InvalidFrameGraphDebugPassIndex;
        UInt32 afterPass = InvalidFrameGraphDebugPassIndex;
        FrameGraphDebugResourceKind resourceKind = FrameGraphDebugResourceKind::Texture;
        UInt32 resourceIndex = InvalidFrameGraphResourceIndex;
        UInt32 version = 0;
        FrameGraphDebugDependencyHazard hazard = FrameGraphDebugDependencyHazard::Raw;
    };

    struct FrameGraphDebugAccess
    {
        FrameGraphDebugResourceKind resourceKind = FrameGraphDebugResourceKind::Texture;
        UInt32 resourceIndex = InvalidFrameGraphResourceIndex;
        UInt32 inputVersion = 0;
        std::optional<UInt32> outputVersion;
        UInt32 accessValue = 0;
        bool write = false;
    };

    struct FrameGraphDebugAttachment
    {
        UInt32 textureIndex = InvalidFrameGraphResourceIndex;
        UInt32 version = 0;
        rhi::RhiLoadAction loadAction = rhi::RhiLoadAction::DontCare;
        rhi::RhiStoreAction storeAction = rhi::RhiStoreAction::DontCare;
        bool depth = false;
        bool readOnly = false;
    };

    struct FrameGraphDebugPass
    {
        std::string name;
        FrameGraphDebugPassType type = FrameGraphDebugPassType::Raster;
        UInt32 registrationIndex = InvalidFrameGraphDebugPassIndex;
        std::optional<UInt32> compiledIndex;
        bool retained = false;
        bool culled = false;
        bool sideEffect = false;
        std::vector<FrameGraphDebugAccess> accesses;
        std::vector<FrameGraphDebugAttachment> attachments;
        std::vector<FrameGraphTextureHandle> textureUavBarriers;
        std::vector<FrameGraphBufferHandle> bufferUavBarriers;
    };

    struct FrameGraphDebugPreview
    {
        FrameGraphDebugPreviewState state = FrameGraphDebugPreviewState::Unavailable;
        std::string message;
        rhi::RhiExtent2D sourceExtent = {};
        rhi::RhiExtent2D previewExtent = {};
        rhi::RhiFormat sourceFormat = rhi::RhiFormat::Unknown;
        std::shared_ptr<FrameGraphDebugPreviewTexture> texture;
    };

    struct FrameGraphDebugResourceVersion
    {
        std::optional<UInt32> producer;
        std::vector<UInt32> readers;
        std::optional<UInt32> firstCompiledUse;
        std::optional<UInt32> lastCompiledUse;
        bool exported = false;
        FrameGraphDebugPreview preview;
    };

    struct FrameGraphDebugTexture
    {
        std::string name;
        FrameGraphTextureDesc desc = {};
        bool imported = false;
        bool swapchain = false;
        std::vector<FrameGraphDebugResourceVersion> versions;
    };

    struct FrameGraphDebugBuffer
    {
        std::string name;
        UInt64 size = 0;
        std::vector<FrameGraphDebugResourceVersion> versions;
    };

    struct FrameGraphDebugData
    {
        UInt64 requestId = 0;
        UInt64 frameIndex = 0;
        Float32 previewScale = 0.5F;
        std::vector<FrameGraphDebugPass> passes;
        std::vector<FrameGraphDebugTexture> textures;
        std::vector<FrameGraphDebugBuffer> buffers;
        std::vector<FrameGraphDebugDependency> dependencies;
        std::vector<UInt32> executionPassIndices;
    };

    /// Mutable render-thread state for one capture while its FrameGraph is recorded and submitted.
    struct FrameGraphDebugFrameCapture
    {
        FrameGraphDebugCaptureRequest request;
        std::shared_ptr<FrameGraphDebugData> data;
        std::string failureMessage;
    };

    struct FrameGraphDebugSourceVersion
    {
        UInt32 producer = InvalidFrameGraphDebugPassIndex;
        std::vector<UInt32> readers;
        UInt32 nextWriter = InvalidFrameGraphDebugPassIndex;
        bool exported = false;
    };

    struct FrameGraphDebugSourcePass
    {
        std::string name;
        FrameGraphDebugPassType type = FrameGraphDebugPassType::Raster;
        UInt32 registrationIndex = InvalidFrameGraphDebugPassIndex;
        bool retained = false;
        bool sideEffect = false;
        std::vector<FrameGraphDebugAccess> accesses;
        std::vector<FrameGraphDebugAttachment> attachments;
        std::vector<FrameGraphTextureHandle> textureUavBarriers;
        std::vector<FrameGraphBufferHandle> bufferUavBarriers;
    };

    struct FrameGraphDebugSourceTexture
    {
        std::string name;
        FrameGraphTextureDesc desc = {};
        bool imported = false;
        bool swapchain = false;
        std::vector<FrameGraphDebugSourceVersion> versions;
    };

    struct FrameGraphDebugSourceBuffer
    {
        std::string name;
        UInt64 size = 0;
        std::vector<FrameGraphDebugSourceVersion> versions;
    };

    struct FrameGraphDebugSourceGraph
    {
        UInt64 frameIndex = 0;
        std::vector<FrameGraphDebugSourcePass> passes;
        std::vector<FrameGraphDebugSourceTexture> textures;
        std::vector<FrameGraphDebugSourceBuffer> buffers;
        std::vector<UInt32> compiledPassOrder;
    };

    struct FrameGraphDebugCapturePlanEntry
    {
        UInt32 textureIndex = InvalidFrameGraphResourceIndex;
        UInt32 version = 0;
        rhi::RhiExtent2D previewExtent = {};
        bool swapchain = false;
        bool requiresSampleableStaging = false;
        UInt32 captureBeforePass = InvalidFrameGraphDebugPassIndex;
    };

    struct FrameGraphDebugBuildResult
    {
        std::shared_ptr<FrameGraphDebugData> data;
        std::vector<FrameGraphDebugCapturePlanEntry> capturePlan;
    };

    struct FrameGraphDebugCapturePublishResult
    {
        bool published = false;
        std::shared_ptr<const FrameGraphDebugData> dataToRetire;
    };

    class FrameGraphDebugCaptureExchange final : public NonCopyable
    {
    public:
        [[nodiscard]] ErrorCode RequestCapture(Float32 previewScale);
        [[nodiscard]] std::optional<FrameGraphDebugCaptureRequest> ConsumeRequest();
        /// Publishes matching capture data and explicitly transfers any displaced or rejected data back to the caller.
        [[nodiscard]] FrameGraphDebugCapturePublishResult Publish(std::shared_ptr<const FrameGraphDebugData> data);
        void Fail(UInt64 requestId, std::string message);
        [[nodiscard]] std::shared_ptr<const FrameGraphDebugData> TakePublishedData();
        [[nodiscard]] FrameGraphDebugCaptureStatus GetStatus() const;
        [[nodiscard]] std::string GetFailureMessage() const;
        /// Resets exchange state and transfers exchange-held data back to the caller.
        [[nodiscard]] std::shared_ptr<const FrameGraphDebugData> Reset() noexcept;

    private:
        mutable Mutex mutex_;
        UInt64 nextRequestId_ = 1;
        std::optional<FrameGraphDebugCaptureRequest> pendingRequest_;
        std::optional<UInt64> activeRequestId_;
        std::shared_ptr<const FrameGraphDebugData> publishedData_;
        FrameGraphDebugCaptureStatus status_ = FrameGraphDebugCaptureStatus::Idle;
        std::string failureMessage_;
    };

    [[nodiscard]] bool IsFrameGraphDebugPreviewScaleValid(Float32 scale) noexcept;
    [[nodiscard]] rhi::RhiExtent2D CalculateFrameGraphDebugPreviewExtent(UInt32 width, UInt32 height, Float32 scale) noexcept;
    [[nodiscard]] bool
    ShouldPublishFrameGraphDebugCapture(ErrorCode submitResult, const std::shared_ptr<FrameGraphDebugData>& data, std::string_view failureMessage) noexcept;
    [[nodiscard]] FrameGraphDebugCapturePublishResult CompleteFrameGraphDebugCapture(FrameGraphDebugCaptureExchange& exchange,
                                                                                     ErrorCode submitResult,
                                                                                     FrameGraphDebugCaptureRequest request,
                                                                                     std::shared_ptr<FrameGraphDebugData> data,
                                                                                     std::string failureMessage);
    [[nodiscard]] std::vector<std::shared_ptr<FrameGraphDebugPreviewTexture>> CollectFrameGraphDebugPreviewTextures(const FrameGraphDebugData& data);
    [[nodiscard]] FrameGraphDebugBuildResult BuildFrameGraphDebugData(const FrameGraphDebugSourceGraph& source, FrameGraphDebugCaptureRequest request);
} // namespace ve
