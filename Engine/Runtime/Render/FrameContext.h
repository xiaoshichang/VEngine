#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/FrameUniformAllocator.h"
#include "Engine/Runtime/Render/RenderFrameConfig.h"
#include "Engine/Runtime/Render/RenderFrameUniformCache.h"

#include <memory>
#include <memory_resource>
#include <vector>

namespace ve
{
    /// Owns resources that may be reused only after one submitted frame completes on the GPU.
    class FrameContext final : public NonCopyable
    {
    public:
        using SubmitCallback = bool (*)(void* context, UInt64 fenceValue) noexcept;

        /// The optional retention allocator must outlive this FrameContext.
        explicit FrameContext(std::pmr::memory_resource* retentionMemoryResource = std::pmr::get_default_resource());

        [[nodiscard]] bool Initialize(rhi::RhiDevice& device, UInt32 contextIndex);
        [[nodiscard]] bool WaitForFrameStartAndReset(rhi::RhiSwapchain& swapchain);
        [[nodiscard]] bool WaitAndReset();
        [[nodiscard]] bool Shutdown();
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Keeps an RHI object alive until this context's submitted fence has completed.
        void RetainInFlightGpuFrameObject(std::shared_ptr<rhi::RhiObject> object);
        [[nodiscard]] ErrorCode Submit(SubmitCallback submit, void* submitContext) noexcept;
        [[nodiscard]] UniformBufferAllocation UploadUniform(const void* data, UInt64 size);
        [[nodiscard]] UniformBufferAllocation GetFrameUniform(const RTScene& scene);
        [[nodiscard]] UniformBufferAllocation GetViewUniform(const RTCamera* camera, rhi::RhiExtent2D targetExtent);
        [[nodiscard]] UniformBufferAllocation GetObjectUniform(const RTRenderItem& item);

        [[nodiscard]] rhi::RhiCommandList& GetCommandList() noexcept;
        [[nodiscard]] rhi::RhiFence& GetCompletionFence() noexcept;
        [[nodiscard]] UInt64 GetNextSubmissionFenceValue() const noexcept;
        [[nodiscard]] UInt64 GetSubmittedFenceValue() const noexcept;
        void MarkSubmitted(UInt64 fenceValue) noexcept;

    private:
        std::unique_ptr<rhi::RhiCommandList> commandList_;
        std::unique_ptr<rhi::RhiFence> completionFence_;
        std::pmr::vector<std::shared_ptr<rhi::RhiObject>> inFlightGpuFrameObjects_;
        FrameUniformAllocator uniformAllocator_;
        RenderFrameUniformCache uniformCache_;
        UInt64 submittedFenceValue_ = 0;
        UInt64 nextFenceValue_ = 1;
    };
} // namespace ve
