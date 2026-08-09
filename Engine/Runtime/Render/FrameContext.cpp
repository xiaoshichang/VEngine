#include "Engine/Runtime/Render/FrameContext.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <limits>
#include <utility>

namespace ve
{
    FrameContext::FrameContext(std::pmr::memory_resource* retentionMemoryResource)
        : inFlightGpuFrameObjects_(retentionMemoryResource != nullptr ? retentionMemoryResource : std::pmr::get_default_resource())
    {
    }

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

        uniformAllocator_.Initialize(device);
        uniformCache_.Initialize(uniformAllocator_);
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

        inFlightGpuFrameObjects_.clear();
        uniformCache_.Reset();
        uniformAllocator_.Reset();
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

        inFlightGpuFrameObjects_.clear();
        uniformCache_.Reset();
        uniformAllocator_.Reset();
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

        uniformCache_.Shutdown();
        uniformAllocator_.Shutdown();
        commandList_.reset();
        completionFence_.reset();
        nextFenceValue_ = 1;
        return true;
    }

    bool FrameContext::IsInitialized() const noexcept
    {
        return commandList_ != nullptr && completionFence_ != nullptr;
    }

    void FrameContext::RetainInFlightGpuFrameObject(std::shared_ptr<rhi::RhiObject> object)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(object != nullptr);
        inFlightGpuFrameObjects_.push_back(std::move(object));
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

    UniformBufferAllocation FrameContext::UploadUniform(const void* data, UInt64 size)
    {
        return uniformAllocator_.Upload(data, size);
    }

    UniformBufferAllocation FrameContext::GetFrameUniform(const RTScene& scene)
    {
        return uniformCache_.GetFrameUniform(scene);
    }

    UniformBufferAllocation FrameContext::GetViewUniform(const RTCamera* camera, rhi::RhiExtent2D targetExtent)
    {
        return uniformCache_.GetViewUniform(camera, targetExtent);
    }

    UniformBufferAllocation FrameContext::GetObjectUniform(const RTRenderItem& item)
    {
        return uniformCache_.GetObjectUniform(item);
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

    void FrameRenderPipelineData::RetainInFlightGpuFrameObject(std::shared_ptr<rhi::RhiObject> object) const
    {
        VE_ASSERT(frameContext != nullptr);
        frameContext->RetainInFlightGpuFrameObject(std::move(object));
    }

    rhi::RhiCommandList& FrameRenderPipelineData::GetCommandList() const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->GetCommandList();
    }

    UniformBufferAllocation FrameRenderPipelineData::UploadUniform(const void* data, UInt64 size) const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->UploadUniform(data, size);
    }

    UniformBufferAllocation FrameRenderPipelineData::GetFrameUniform(const RTScene& scene) const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->GetFrameUniform(scene);
    }

    UniformBufferAllocation FrameRenderPipelineData::GetViewUniform(const RTCamera* camera, rhi::RhiExtent2D targetExtent) const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->GetViewUniform(camera, targetExtent);
    }

    UniformBufferAllocation FrameRenderPipelineData::GetObjectUniform(const RTRenderItem& item) const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->GetObjectUniform(item);
    }
} // namespace ve
