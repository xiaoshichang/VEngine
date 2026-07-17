#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/FrameUniformAllocator.h"
#include "Engine/Runtime/Render/RenderFrameUniformCache.h"

#include <memory>
#include <vector>

namespace ve
{
    inline constexpr UInt32 RenderFrameContextCount = 2;

    /// Owns resources that may be reused only after one submitted frame completes on the GPU.
    class FrameContext final : public NonCopyable
    {
    public:
        [[nodiscard]] bool Initialize(rhi::RhiDevice& device, UInt32 contextIndex);
        [[nodiscard]] bool WaitAndReset();
        [[nodiscard]] bool Shutdown();
        [[nodiscard]] bool IsInitialized() const noexcept;

        void RetainTransientResource(std::unique_ptr<rhi::RhiObject> resource);
        [[nodiscard]] UniformBufferAllocation UploadUniform(const void* data, UInt64 size);
        [[nodiscard]] UniformBufferAllocation GetFrameUniform(const RTScene& scene);
        [[nodiscard]] UniformBufferAllocation GetViewUniform(const RTCamera* camera, rhi::RhiExtent2D targetExtent);
        [[nodiscard]] UniformBufferAllocation GetObjectUniform(const RTRenderItem& item);

        [[nodiscard]] rhi::RhiCommandList& GetCommandList() noexcept;
        [[nodiscard]] rhi::RhiFence& GetCompletionFence() noexcept;
        [[nodiscard]] UInt64 GetNextSubmissionFenceValue() const noexcept;
        void MarkSubmitted(UInt64 fenceValue) noexcept;

    private:
        std::unique_ptr<rhi::RhiCommandList> commandList_;
        std::unique_ptr<rhi::RhiFence> completionFence_;
        std::vector<std::unique_ptr<rhi::RhiObject>> transientResources_;
        FrameUniformAllocator uniformAllocator_;
        RenderFrameUniformCache uniformCache_;
        UInt64 submittedFenceValue_ = 0;
        UInt64 nextFenceValue_ = 1;
    };
} // namespace ve
