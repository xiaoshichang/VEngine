#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Vector4.h"
#include "Engine/Runtime/Render/RenderFrameConfig.h"
#include "Engine/Runtime/Render/RenderResourceLifetime.h"

#include <array>
#include <memory>
#include <type_traits>

namespace ve
{
    class RTCamera;
    class RTRenderItem;
    class RTScene;

    struct UniformBufferAllocation
    {
        rhi::RhiBuffer* buffer = nullptr;
        UInt64 offset = 0;
        UInt64 size = 0;
    };

    struct alignas(16) SceneUniformData
    {
        Vector4 directionalLightDirection = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
        Vector4 directionalLightColorAndIntensity = Vector4(1.0f, 1.0f, 1.0f, 0.0f);
        Vector4 ambientColor = Vector4(0.35f, 0.35f, 0.35f, 1.0f);
    };

    struct alignas(16) ViewUniformData
    {
        Matrix44 viewProjection = Matrix44::Identity();
        Vector4 cameraWorldPosition = Vector4::Zero();
        Vector4 cameraWorldForward = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
    };

    struct alignas(16) ObjectUniformData
    {
        Matrix44 localToWorld = Matrix44::Identity();
        UInt32 receiveShadows = 1;
        UInt32 padding[3] = {};
    };

    static_assert(sizeof(SceneUniformData) == 48);
    static_assert(sizeof(ViewUniformData) == 96);
    static_assert(sizeof(ObjectUniformData) == 80);
    static_assert(std::is_trivially_copyable_v<SceneUniformData>);
    static_assert(std::is_trivially_copyable_v<ViewUniformData>);
    static_assert(std::is_trivially_copyable_v<ObjectUniformData>);

    class RTDynamicUniformBuffer final : public NonCopyable
    {
    public:
        [[nodiscard]] UniformBufferAllocation
        GetOrUpdate(rhi::RhiDevice& device, UInt32 frameSlotIndex, const void* data, UInt64 size, UInt64 revision, const char* debugName);
        [[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;

    private:
        std::array<std::shared_ptr<rhi::RhiBuffer>, RenderFrameContextCount> buffers_;
        std::array<UInt64, RenderFrameContextCount> uploadedRevisions_{};
    };

    [[nodiscard]] SceneUniformData BuildSceneUniformData(const RTScene& scene) noexcept;
    [[nodiscard]] ViewUniformData BuildViewUniformData(const RTCamera* camera, rhi::RhiExtent2D targetExtent) noexcept;
    [[nodiscard]] ObjectUniformData BuildObjectUniformData(const RTRenderItem& item) noexcept;
} // namespace ve
