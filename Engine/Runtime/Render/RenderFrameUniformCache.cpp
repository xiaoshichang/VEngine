#include "Engine/Runtime/Render/RenderFrameUniformCache.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

namespace ve
{
    void RenderFrameUniformCache::Initialize(FrameUniformAllocator& allocator) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(allocator_ == nullptr);
        allocator_ = &allocator;
    }

    void RenderFrameUniformCache::Reset() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        frameUniforms_.clear();
        viewUniforms_.clear();
        objectUniforms_.clear();
    }

    void RenderFrameUniformCache::Shutdown() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        Reset();
        allocator_ = nullptr;
    }

    UniformBufferAllocation RenderFrameUniformCache::GetFrameUniform(const RTScene& scene)
    {
        VE_ASSERT(allocator_ != nullptr);
        const auto found = frameUniforms_.find(&scene);
        if (found != frameUniforms_.end())
        {
            return found->second;
        }

        const SceneUniformData data = BuildSceneUniformData(scene);
        const UniformBufferAllocation allocation = allocator_->Upload(&data, sizeof(data));
        frameUniforms_.emplace(&scene, allocation);
        return allocation;
    }

    UniformBufferAllocation RenderFrameUniformCache::GetViewUniform(const RTCamera* camera, rhi::RhiExtent2D targetExtent)
    {
        VE_ASSERT(allocator_ != nullptr);
        const ViewUniformKey key{camera, targetExtent.width, targetExtent.height};
        const auto found = viewUniforms_.find(key);
        if (found != viewUniforms_.end())
        {
            return found->second;
        }

        const ViewUniformData data = BuildViewUniformData(camera, targetExtent);
        const UniformBufferAllocation allocation = allocator_->Upload(&data, sizeof(data));
        viewUniforms_.emplace(key, allocation);
        return allocation;
    }

    UniformBufferAllocation RenderFrameUniformCache::GetObjectUniform(const RTRenderItem& item)
    {
        VE_ASSERT(allocator_ != nullptr);
        const auto found = objectUniforms_.find(&item);
        if (found != objectUniforms_.end())
        {
            return found->second;
        }

        const ObjectUniformData data = BuildObjectUniformData(item);
        const UniformBufferAllocation allocation = allocator_->Upload(&data, sizeof(data));
        objectUniforms_.emplace(&item, allocation);
        return allocation;
    }
} // namespace ve
