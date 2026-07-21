#include "Engine/Runtime/Render/RenderFrameUniformCache.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderCameraMath.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>

namespace ve
{
    namespace
    {
        [[nodiscard]] const RTLight* FindDirectionalLight(const RTScene& scene) noexcept
        {
            for (SizeT lightIndex = 0; lightIndex < scene.GetLightCount(); ++lightIndex)
            {
                const std::shared_ptr<RTLight> light = scene.GetLight(lightIndex);
                if (light != nullptr && light->GetType() == RTLightType::Directional)
                {
                    return light.get();
                }
            }
            return nullptr;
        }

        [[nodiscard]] FrameUniformData BuildFrameUniformData(const RTScene& scene) noexcept
        {
            FrameUniformData data = {};
            const RTLight* light = FindDirectionalLight(scene);
            if (light == nullptr)
            {
                return data;
            }

            const Vector3& direction = light->GetDirection();
            const Vector3& color = light->GetColor();
            data.directionalLightDirection = Vector4(direction, 0.0f);
            data.directionalLightColorAndIntensity =
                Vector4(std::max(color.GetX(), 0.0f), std::max(color.GetY(), 0.0f), std::max(color.GetZ(), 0.0f), std::max(light->GetIntensity(), 0.0f));
            return data;
        }

        [[nodiscard]] ViewUniformData BuildViewUniformData(const RTCamera* camera, rhi::RhiExtent2D targetExtent) noexcept
        {
            ViewUniformData data = {};
            data.viewProjection = (camera != nullptr ? BuildCameraViewProjection(*camera, targetExtent) : Matrix44::Identity()).Transposed();
            if (camera != nullptr)
            {
                const Matrix44& localToWorld = camera->GetLocalToWorld();
                data.cameraWorldPosition = Vector4(localToWorld.Get(0, 3), localToWorld.Get(1, 3), localToWorld.Get(2, 3), 1.0f);
            }
            return data;
        }

        [[nodiscard]] ObjectUniformData BuildObjectUniformData(const RTRenderItem& item) noexcept
        {
            ObjectUniformData data = {};
            data.localToWorld = item.GetLocalToWorld().Transposed();
            return data;
        }
    } // namespace

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

        const FrameUniformData data = BuildFrameUniformData(scene);
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
