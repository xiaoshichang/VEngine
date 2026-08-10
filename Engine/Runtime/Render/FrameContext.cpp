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

        transientResourcePool_.Initialize(device);
        return true;
    }

    bool FrameContext::WaitAndReset()
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(commandList_ != nullptr);
        VE_ASSERT(completionFence_ != nullptr);

        const UInt64 completedFenceValue = submittedFenceValue_;
        if (completedFenceValue != 0 && !completionFence_->Wait(completedFenceValue))
        {
            return false;
        }

        submittedFenceValue_ = 0;
        PrepareForReuse(completedFenceValue);
        return true;
    }

    bool FrameContext::WaitForFrameStartAndReset(rhi::RhiSwapchain& swapchain)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(commandList_ != nullptr);
        VE_ASSERT(completionFence_ != nullptr);

        const UInt64 completedFenceValue = submittedFenceValue_;
        if (!swapchain.WaitForFrameStart(*completionFence_, completedFenceValue))
        {
            return false;
        }

        submittedFenceValue_ = 0;
        PrepareForReuse(completedFenceValue);
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

        RhiObjectList transientObjects = transientResourcePool_.Shutdown();
        transientObjects.clear();
        pendingDeleteRTResourceQueue_.clear();
        submittedFrameIndex_ = 0;
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

    FrameTransientResourcePool& FrameContext::GetTransientResourcePool() noexcept
    {
        return transientResourcePool_;
    }

    void FrameContext::EnqueuePendingDeleteResource(UInt64 fenceValue, RhiObjectList resources)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(fenceValue != 0);
        VE_ASSERT(!resources.empty());
        VE_ASSERT(pendingDeleteRTResourceQueue_.empty() || pendingDeleteRTResourceQueue_.back().fenceValue <= fenceValue);

        pendingDeleteRTResourceQueue_.push_back(PendingDeleteRTResourceEntry{fenceValue, std::move(resources)});
    }

    void FrameContext::ClearRetiredRhiObjectsAfterWaitIdle() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        pendingDeleteRTResourceQueue_.clear();
    }

    void FrameContext::SetSubmittedFrameIndex(UInt64 frameIndex) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(frameIndex != 0);
        VE_ASSERT(submittedFrameIndex_ == 0);
        submittedFrameIndex_ = frameIndex;
    }

    UInt64 FrameContext::TakeSubmittedFrameIndex() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        const UInt64 frameIndex = submittedFrameIndex_;
        submittedFrameIndex_ = 0;
        return frameIndex;
    }

    void FrameContext::PrepareForReuse(UInt64 completedFenceValue) noexcept
    {
        CollectRetiredRhiObjects(completedFenceValue);
        transientResourcePool_.BeginFrame();
    }

    void FrameContext::CollectRetiredRhiObjects(UInt64 completedFenceValue) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        while (!pendingDeleteRTResourceQueue_.empty() && pendingDeleteRTResourceQueue_.front().fenceValue <= completedFenceValue)
        {
            pendingDeleteRTResourceQueue_.pop_front();
        }
    }

} // namespace ve
