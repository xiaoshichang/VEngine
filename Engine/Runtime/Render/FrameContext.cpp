#include "Engine/Runtime/Render/FrameContext.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <utility>

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

        uniformAllocator_.Initialize(device);
        uniformCache_.Initialize(uniformAllocator_);
        transientResourcePool_.Initialize(device);

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

        submittedFrameObject_.reset();
        transientResources_.clear();
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

        transientResourcePool_.Shutdown();
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

    void FrameContext::RetainTransientResource(std::unique_ptr<rhi::RhiObject> resource)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(resource != nullptr);
        transientResources_.push_back(std::move(resource));
    }

    void FrameContext::RetainSubmittedFrameObject(std::shared_ptr<void> object)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(object != nullptr);
        VE_ASSERT(submittedFrameObject_ == nullptr);
        submittedFrameObject_ = std::move(object);
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

    FrameGraphTransientResourcePool& FrameContext::GetFrameGraphTransientResourcePool() noexcept
    {
        VE_ASSERT(commandList_ != nullptr);
        return transientResourcePool_;
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

    void FrameContext::MarkSubmitted(UInt64 fenceValue) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(submittedFenceValue_ == 0);
        VE_ASSERT(fenceValue == nextFenceValue_);
        submittedFenceValue_ = fenceValue;
        ++nextFenceValue_;
    }

    void FrameRenderPipelineData::RetainTransientResource(std::unique_ptr<rhi::RhiObject> resource) const
    {
        VE_ASSERT(frameContext != nullptr);
        frameContext->RetainTransientResource(std::move(resource));
    }

    rhi::RhiCommandList& FrameRenderPipelineData::GetCommandList() const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->GetCommandList();
    }

    FrameGraphTransientResourcePool& FrameRenderPipelineData::GetFrameGraphTransientResourcePool() const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->GetFrameGraphTransientResourcePool();
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
