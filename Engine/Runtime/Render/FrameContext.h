#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderFrameConfig.h"

#include <memory>

namespace ve
{
    /// Owns the command list and completion fence for one reusable GPU submission slot.
    class FrameContext final : public NonCopyable
    {
    public:
        using SubmitCallback = bool (*)(void* context, UInt64 fenceValue) noexcept;

        FrameContext() = default;

        [[nodiscard]] bool Initialize(rhi::RhiDevice& device, UInt32 contextIndex);
        [[nodiscard]] bool WaitForFrameStartAndReset(rhi::RhiSwapchain& swapchain);
        [[nodiscard]] bool WaitAndReset();
        [[nodiscard]] bool Shutdown();
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] ErrorCode Submit(SubmitCallback submit, void* submitContext) noexcept;

        [[nodiscard]] rhi::RhiCommandList& GetCommandList() noexcept;
        [[nodiscard]] rhi::RhiFence& GetCompletionFence() noexcept;
        [[nodiscard]] UInt64 GetNextSubmissionFenceValue() const noexcept;
        [[nodiscard]] UInt64 GetSubmittedFenceValue() const noexcept;
        void MarkSubmitted(UInt64 fenceValue) noexcept;

    private:
        std::unique_ptr<rhi::RhiCommandList> commandList_;
        std::unique_ptr<rhi::RhiFence> completionFence_;
        UInt64 submittedFenceValue_ = 0;
        UInt64 nextFenceValue_ = 1;
    };
} // namespace ve
