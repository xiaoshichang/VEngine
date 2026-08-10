#include "Engine/Runtime/Render/RenderUniformBuffer.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderCameraMath.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <utility>

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
    } // namespace

    UniformBufferAllocation
    RTDynamicUniformBuffer::GetOrUpdate(rhi::RhiDevice& device, UInt32 frameSlotIndex, const void* data, UInt64 size, UInt64 revision, const char* debugName)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(frameSlotIndex < RenderFrameContextCount);
        VE_ASSERT(data != nullptr);
        VE_ASSERT(size > 0);
        if (frameSlotIndex >= RenderFrameContextCount || data == nullptr || size == 0)
        {
            return {};
        }

        std::shared_ptr<rhi::RhiBuffer>& buffer = buffers_[frameSlotIndex];
        bool requiresUpload = false;
        if (buffer == nullptr || buffer->GetSize() != size)
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = size;
            desc.usage = rhi::RhiBufferUsage::Uniform;
            desc.memoryUsage = rhi::RhiBufferMemoryUsage::CpuToGpu;
            desc.debugName = debugName;
            buffer = device.CreateBuffer(desc);
            VE_ASSERT_MESSAGE(buffer != nullptr, "Failed to create an owner-local uniform buffer.");
            requiresUpload = true;
        }

        if (buffer == nullptr)
        {
            return {};
        }
        if (requiresUpload || uploadedRevisions_[frameSlotIndex] != revision)
        {
            device.UpdateBuffer(*buffer, 0, data, size, rhi::RhiBufferUpdateMode::Discard);
            uploadedRevisions_[frameSlotIndex] = revision;
        }
        return UniformBufferAllocation{buffer.get(), 0, size};
    }

    RhiObjectList RTDynamicUniformBuffer::TakeRhiObjects() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        RhiObjectList objects;
        objects.reserve(RenderFrameContextCount);
        for (std::shared_ptr<rhi::RhiBuffer>& buffer : buffers_)
        {
            MoveRhiObject(objects, buffer);
        }
        uploadedRevisions_.fill(0);
        return objects;
    }

    SceneUniformData BuildSceneUniformData(const RTScene& scene) noexcept
    {
        SceneUniformData data = {};
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

    ViewUniformData BuildViewUniformData(const RTCamera* camera, rhi::RhiExtent2D targetExtent) noexcept
    {
        ViewUniformData data = {};
        data.viewProjection = (camera != nullptr ? BuildCameraViewProjection(*camera, targetExtent) : Matrix44::Identity()).Transposed();
        if (camera != nullptr)
        {
            const Matrix44& localToWorld = camera->GetLocalToWorld();
            data.cameraWorldPosition = Vector4(localToWorld.Get(0, 3), localToWorld.Get(1, 3), localToWorld.Get(2, 3), 1.0f);
            Vector3 cameraForward = localToWorld.TransformDirection(Vector3::UnitZ()).Normalized();
            if (cameraForward.LengthSquared() == 0.0f)
            {
                cameraForward = Vector3::UnitZ();
            }
            data.cameraWorldForward = Vector4(cameraForward, 0.0f);
        }
        return data;
    }

    ObjectUniformData BuildObjectUniformData(const RTRenderItem& item) noexcept
    {
        ObjectUniformData data = {};
        data.localToWorld = item.GetLocalToWorld().Transposed();
        data.receiveShadows = item.ReceiveShadows() ? 1u : 0u;
        return data;
    }
} // namespace ve
