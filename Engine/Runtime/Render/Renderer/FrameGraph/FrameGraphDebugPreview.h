#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace ve
{
    class RenderPassContext;

    enum class FrameGraphDebugPreviewMode
    {
        Color,
        Depth,
        UnsignedInteger,
        Unsupported,
    };

    [[nodiscard]] FrameGraphDebugPreviewMode SelectFrameGraphDebugPreviewMode(rhi::RhiFormat format) noexcept;
    [[nodiscard]] bool NeedsFrameGraphDebugStaging(rhi::RhiTextureUsage usage) noexcept;

    /// Applies the same contrast curve used by depth preview shaders to normalized device depth.
    [[nodiscard]] Float32 RemapFrameGraphDebugDepthForPreview(Float32 deviceDepth) noexcept;

    /// Render-thread owner for one persistent RGBA debugger preview texture.
    ///
    /// The owner may be shared with Editor data, but the live RHI texture must be released through Reset on the Render
    /// Thread before final destruction. Destroying an owner that still contains a texture is a fatal lifetime error.
    class FrameGraphDebugPreviewTexture final : public NonCopyable
    {
    public:
        FrameGraphDebugPreviewTexture() = default;
        ~FrameGraphDebugPreviewTexture();

        [[nodiscard]] ErrorCode Initialize(rhi::RhiDevice& device, rhi::RhiExtent2D extent, std::string debugName);
        [[nodiscard]] rhi::RhiTexture* GetTexture() noexcept;
        [[nodiscard]] const rhi::RhiTexture* GetTexture() const noexcept;
        [[nodiscard]] std::shared_ptr<rhi::RhiTexture> GetTextureShared() const noexcept;
        [[nodiscard]] void* GetNativeSampledViewHandle() const noexcept;
        void Reset();

    private:
        std::shared_ptr<rhi::RhiTexture> texture_;
        std::atomic<void*> nativeSampledViewHandle_{nullptr};
    };

    /// Records one fullscreen source conversion inside an already-begun Rgba8Unorm color render pass.
    [[nodiscard]] ErrorCode RecordFrameGraphDebugPreviewConversion(const rhi::RhiTexture& source, FrameGraphDebugPreviewMode mode, RenderPassContext& context);
} // namespace ve
