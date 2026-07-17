#include "Engine/Runtime/Render/RenderFrameUniformCache.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <cmath>

namespace ve
{
    namespace
    {
        [[nodiscard]] Matrix44 BuildRigidInverse(const Matrix44& localToWorld) noexcept
        {
            Matrix44 inverse = Matrix44::Identity();
            for (SizeT row = 0; row < 3; ++row)
            {
                for (SizeT column = 0; column < 3; ++column)
                {
                    inverse.Set(row, column, localToWorld.Get(column, row));
                }
            }

            const Vector3 translation(localToWorld.Get(0, 3), localToWorld.Get(1, 3), localToWorld.Get(2, 3));
            for (SizeT row = 0; row < 3; ++row)
            {
                const Float32 value =
                    -((inverse.Get(row, 0) * translation.GetX()) + (inverse.Get(row, 1) * translation.GetY()) + (inverse.Get(row, 2) * translation.GetZ()));
                inverse.Set(row, 3, value);
            }

            return inverse;
        }

        [[nodiscard]] Float32 ResolveAspectRatio(const RTCamera& camera, rhi::RhiExtent2D targetExtent) noexcept
        {
            if (!camera.IsAspectRatioAutomatic())
            {
                return std::max(camera.GetAspectRatio(), 0.001f);
            }

            const Float32 width = static_cast<Float32>(std::max(targetExtent.width, 1u));
            const Float32 height = static_cast<Float32>(std::max(targetExtent.height, 1u));
            return width / height;
        }

        [[nodiscard]] Matrix44 BuildPerspectiveProjection(const RTCamera& camera, Float32 aspectRatio) noexcept
        {
            const Float32 nearClip = std::max(camera.GetNearClipPlane(), 0.001f);
            const Float32 farClip = std::max(camera.GetFarClipPlane(), nearClip + 0.001f);
            const Float32 fieldOfView = std::max(camera.GetVerticalFieldOfViewRadians(), 0.001f);
            const Float32 yScale = 1.0f / std::tan(fieldOfView * 0.5f);
            const Float32 xScale = yScale / aspectRatio;

            Matrix44 projection = Matrix44::Zero();
            projection.Set(0, 0, xScale);
            projection.Set(1, 1, yScale);
            projection.Set(2, 2, farClip / (farClip - nearClip));
            projection.Set(2, 3, -(nearClip * farClip) / (farClip - nearClip));
            projection.Set(3, 2, 1.0f);
            return projection;
        }

        [[nodiscard]] Matrix44 BuildOrthographicProjection(const RTCamera& camera, Float32 aspectRatio) noexcept
        {
            const Float32 nearClip = camera.GetNearClipPlane();
            const Float32 farClip = std::max(camera.GetFarClipPlane(), nearClip + 0.001f);
            const Float32 height = std::max(camera.GetOrthographicSize(), 0.001f);
            const Float32 width = height * aspectRatio;

            Matrix44 projection = Matrix44::Identity();
            projection.Set(0, 0, 2.0f / width);
            projection.Set(1, 1, 2.0f / height);
            projection.Set(2, 2, 1.0f / (farClip - nearClip));
            projection.Set(2, 3, -nearClip / (farClip - nearClip));
            return projection;
        }

        [[nodiscard]] Matrix44 BuildViewProjection(const RTCamera* camera, rhi::RhiExtent2D targetExtent) noexcept
        {
            if (camera == nullptr)
            {
                return Matrix44::Identity();
            }

            const Float32 aspectRatio = ResolveAspectRatio(*camera, targetExtent);
            const Matrix44 projection = camera->GetProjectionMode() == RTCameraProjectionMode::Orthographic ? BuildOrthographicProjection(*camera, aspectRatio)
                                                                                                            : BuildPerspectiveProjection(*camera, aspectRatio);
            return projection * BuildRigidInverse(camera->GetLocalToWorld());
        }

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
            data.viewProjection = BuildViewProjection(camera, targetExtent).Transposed();
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
