#include "Engine/Runtime/Render/FrameContext.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

namespace ve
{
    bool FrameContext::Initialize(rhi::RhiDevice& device, UInt32 contextIndex)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(commandList_ == nullptr);
        VE_ASSERT(completionFence_ == nullptr);
        static_cast<void>(contextIndex);

        commandList_ = device.CreateCommandList();
        if (commandList_ == nullptr)
        {
            return false;
        }

        completionFence_ = device.CreateFence(0);
        if (completionFence_ == nullptr)
        {
            commandList_.reset();
            return false;
        }

        return true;
    }

    bool FrameContext::WaitAndReset()
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(commandList_ != nullptr);
        VE_ASSERT(completionFence_ != nullptr);

        if (submittedFenceValue_ != 0 && !completionFence_->Wait(submittedFenceValue_))
        {
            return false;
        }

        submittedFenceValue_ = 0;
        return true;
    }

    bool FrameContext::WaitForFrameStartAndReset(rhi::RhiSwapchain& swapchain)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(commandList_ != nullptr);
        VE_ASSERT(completionFence_ != nullptr);

        if (!swapchain.WaitForFrameStart(*completionFence_, submittedFenceValue_))
        {
            return false;
        }

        submittedFenceValue_ = 0;
        return true;
    }

    bool FrameContext::Shutdown()
    {
        VE_ASSERT_RENDER_THREAD();
        if (commandList_ == nullptr && completionFence_ == nullptr)
        {
            return true;
        }

        if (commandList_ == nullptr || completionFence_ == nullptr || !WaitAndReset())
        {
            return false;
        }

        commandList_.reset();
        completionFence_.reset();
        nextFenceValue_ = 1;
        return true;
    }

    bool FrameContext::IsInitialized() const noexcept
    {
        return commandList_ != nullptr && completionFence_ != nullptr;
    }

    ErrorCode FrameContext::Submit(SubmitCallback submit, void* submitContext) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        if (submit == nullptr)
        {
            return ErrorCode::InvalidArgument;
        }
        const UInt64 submissionFenceValue = GetNextSubmissionFenceValue();
        if (!submit(submitContext, submissionFenceValue))
        {
            return ErrorCode::PlatformError;
        }
        MarkSubmitted(submissionFenceValue);
        return ErrorCode::None;
    }

    rhi::RhiCommandList& FrameContext::GetCommandList() noexcept
    {
        VE_ASSERT(commandList_ != nullptr);
        return *commandList_;
    }

    rhi::RhiFence& FrameContext::GetCompletionFence() noexcept
    {
        VE_ASSERT(completionFence_ != nullptr);
        return *completionFence_;
    }

    UInt64 FrameContext::GetNextSubmissionFenceValue() const noexcept
    {
        return nextFenceValue_;
    }

    UInt64 FrameContext::GetSubmittedFenceValue() const noexcept
    {
        return submittedFenceValue_;
    }

    void FrameContext::MarkSubmitted(UInt64 fenceValue) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(submittedFenceValue_ == 0);
        VE_ASSERT(fenceValue == nextFenceValue_);
        submittedFenceValue_ = fenceValue;
        ++nextFenceValue_;
    }

} // namespace ve
