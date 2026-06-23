#pragma once

#include "Engine/Runtime/Core/EnumFlags.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Render/RenderResource.h"

#include <memory>
#include <string>
#include <vector>

namespace ve
{
    enum class RTRenderItemDirtyFlags : UInt32
    {
        None = 0,
        MeshResource = 1u << 0,
        MaterialResource = 1u << 1,
        Bounds = 1u << 2,
        Transform = 1u << 3,
        All = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3),
    };

    template<>
    struct EnableEnumFlags<RTRenderItemDirtyFlags> : std::true_type
    {
    };

    [[nodiscard]] constexpr bool HasRTRenderItemDirtyFlag(RTRenderItemDirtyFlags value, RTRenderItemDirtyFlags flag) noexcept
    {
        return (ToUnderlying(value & flag) != 0u);
    }

    struct RTRenderItemInitParam
    {
        std::shared_ptr<RHIResource> meshResource;
        std::shared_ptr<RHIResource> materialResource;
        Vector3 boundsCenter = Vector3::Zero();
        Vector3 boundsExtents = Vector3::One();
        Matrix44 localToWorld = Matrix44::Identity();
    };

    struct RTRenderItemUpdateParam
    {
        RTRenderItemDirtyFlags dirtyFlags = RTRenderItemDirtyFlags::None;
        std::shared_ptr<RHIResource> meshResource;
        std::shared_ptr<RHIResource> materialResource;
        Vector3 boundsCenter = Vector3::Zero();
        Vector3 boundsExtents = Vector3::One();
        Matrix44 localToWorld = Matrix44::Identity();
    };

    enum class RTCameraProjectionMode
    {
        Perspective,
        Orthographic,
    };

    enum class RTCameraDirtyFlags : UInt32
    {
        None = 0,
        Primary = 1u << 0,
        Projection = 1u << 1,
        ClearColor = 1u << 2,
        Transform = 1u << 3,
        All = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3),
    };

    template<>
    struct EnableEnumFlags<RTCameraDirtyFlags> : std::true_type
    {
    };

    [[nodiscard]] constexpr bool HasRTCameraDirtyFlag(RTCameraDirtyFlags value, RTCameraDirtyFlags flag) noexcept
    {
        return (ToUnderlying(value & flag) != 0u);
    }

    struct RTCameraInitParam
    {
        bool primary = false;
        RTCameraProjectionMode projectionMode = RTCameraProjectionMode::Perspective;
        Float32 verticalFieldOfViewRadians = 1.0471975512f;
        Float32 orthographicSize = 5.0f;
        Float32 aspectRatio = 1.7777778f;
        Float32 nearClipPlane = 0.1f;
        Float32 farClipPlane = 1000.0f;
        rhi::RhiColor clearColor{0.05f, 0.07f, 0.10f, 1.0f};
        Matrix44 localToWorld = Matrix44::Identity();
    };

    struct RTCameraUpdateParam
    {
        RTCameraDirtyFlags dirtyFlags = RTCameraDirtyFlags::None;
        bool primary = false;
        RTCameraProjectionMode projectionMode = RTCameraProjectionMode::Perspective;
        Float32 verticalFieldOfViewRadians = 1.0471975512f;
        Float32 orthographicSize = 5.0f;
        Float32 aspectRatio = 1.7777778f;
        Float32 nearClipPlane = 0.1f;
        Float32 farClipPlane = 1000.0f;
        rhi::RhiColor clearColor{0.05f, 0.07f, 0.10f, 1.0f};
        Matrix44 localToWorld = Matrix44::Identity();
    };

    enum class RTLightType
    {
        Directional,
        Point,
    };

    enum class RTLightDirtyFlags : UInt32
    {
        None = 0,
        Type = 1u << 0,
        Color = 1u << 1,
        Intensity = 1u << 2,
        Range = 1u << 3,
        Cone = 1u << 4,
        Shadows = 1u << 5,
        Transform = 1u << 6,
        All = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6),
    };

    template<>
    struct EnableEnumFlags<RTLightDirtyFlags> : std::true_type
    {
    };

    [[nodiscard]] constexpr bool HasRTLightDirtyFlag(RTLightDirtyFlags value, RTLightDirtyFlags flag) noexcept
    {
        return (ToUnderlying(value & flag) != 0u);
    }

    struct RTLightInitParam
    {
        RTLightType type = RTLightType::Directional;
        Vector3 color = Vector3::One();
        Vector3 direction = Vector3::UnitZ();
        Float32 intensity = 1.0f;
        Float32 range = 0.0f;
        Float32 innerConeAngleRadians = 0.0f;
        Float32 outerConeAngleRadians = 0.0f;
        bool castShadows = false;
        Matrix44 localToWorld = Matrix44::Identity();
    };

    struct RTLightUpdateParam
    {
        RTLightDirtyFlags dirtyFlags = RTLightDirtyFlags::None;
        RTLightType type = RTLightType::Directional;
        Vector3 color = Vector3::One();
        Vector3 direction = Vector3::UnitZ();
        Float32 intensity = 1.0f;
        Float32 range = 0.0f;
        Float32 innerConeAngleRadians = 0.0f;
        Float32 outerConeAngleRadians = 0.0f;
        bool castShadows = false;
        Matrix44 localToWorld = Matrix44::Identity();
    };

    /// Render-thread representation of one MeshRenderComponent.
    class RTRenderItem final : public NonCopyable
    {
    public:
        explicit RTRenderItem(RTRenderItemInitParam initParam);

        void ApplyUpdateParam(RTRenderItemUpdateParam updateParam);

        [[nodiscard]] const std::shared_ptr<RHIResource>& GetMeshResource() const noexcept;
        void SetMeshResource(std::shared_ptr<RHIResource> resource) noexcept;

        [[nodiscard]] const std::shared_ptr<RHIResource>& GetMaterialResource() const noexcept;
        void SetMaterialResource(std::shared_ptr<RHIResource> resource) noexcept;
        [[nodiscard]] const Vector3& GetBoundsCenter() const noexcept;
        [[nodiscard]] const Vector3& GetBoundsExtents() const noexcept;
        [[nodiscard]] const Matrix44& GetLocalToWorld() const noexcept;

    private:
        std::shared_ptr<RHIResource> meshResource_;
        std::shared_ptr<RHIResource> materialResource_;
        Vector3 boundsCenter_ = Vector3::Zero();
        Vector3 boundsExtents_ = Vector3::One();
        Matrix44 localToWorld_ = Matrix44::Identity();
    };

    /// Render-thread representation of one CameraComponent.
    ///
    /// The uniform buffer placeholder is where future camera constant data should live once RHI buffer allocation and
    /// upload scheduling are wired into RenderSystem.
    class RTCamera final : public NonCopyable
    {
    public:
        explicit RTCamera(RTCameraInitParam initParam);

        void ApplyUpdateParam(RTCameraUpdateParam updateParam);

        [[nodiscard]] bool IsPrimary() const noexcept;
        [[nodiscard]] RTCameraProjectionMode GetProjectionMode() const noexcept;
        [[nodiscard]] Float32 GetVerticalFieldOfViewRadians() const noexcept;
        [[nodiscard]] Float32 GetOrthographicSize() const noexcept;
        [[nodiscard]] Float32 GetAspectRatio() const noexcept;
        [[nodiscard]] Float32 GetNearClipPlane() const noexcept;
        [[nodiscard]] Float32 GetFarClipPlane() const noexcept;
        [[nodiscard]] const rhi::RhiColor& GetClearColor() const noexcept;
        [[nodiscard]] const Matrix44& GetLocalToWorld() const noexcept;

        [[nodiscard]] const std::shared_ptr<RHIResource>& GetUniformBufferResource() const noexcept;
        void SetUniformBufferResource(std::shared_ptr<RHIResource> resource) noexcept;

    private:
        bool primary_ = false;
        RTCameraProjectionMode projectionMode_ = RTCameraProjectionMode::Perspective;
        Float32 verticalFieldOfViewRadians_ = 1.0471975512f;
        Float32 orthographicSize_ = 5.0f;
        Float32 aspectRatio_ = 1.7777778f;
        Float32 nearClipPlane_ = 0.1f;
        Float32 farClipPlane_ = 1000.0f;
        rhi::RhiColor clearColor_{0.05f, 0.07f, 0.10f, 1.0f};
        Matrix44 localToWorld_ = Matrix44::Identity();
        std::shared_ptr<RHIResource> uniformBufferResource_;
    };

    /// Render-thread representation of one LightComponent.
    ///
    /// The uniform buffer placeholder is where future light constant data should live once RHI buffer allocation and
    /// upload scheduling are wired into RenderSystem.
    class RTLight final : public NonCopyable
    {
    public:
        explicit RTLight(RTLightInitParam initParam);

        void ApplyUpdateParam(RTLightUpdateParam updateParam);

        [[nodiscard]] RTLightType GetType() const noexcept;
        [[nodiscard]] const Vector3& GetColor() const noexcept;
        [[nodiscard]] const Vector3& GetDirection() const noexcept;
        [[nodiscard]] Float32 GetIntensity() const noexcept;
        [[nodiscard]] Float32 GetRange() const noexcept;
        [[nodiscard]] Float32 GetInnerConeAngleRadians() const noexcept;
        [[nodiscard]] Float32 GetOuterConeAngleRadians() const noexcept;
        [[nodiscard]] bool CastShadows() const noexcept;
        [[nodiscard]] const Matrix44& GetLocalToWorld() const noexcept;

        [[nodiscard]] const std::shared_ptr<RHIResource>& GetUniformBufferResource() const noexcept;
        void SetUniformBufferResource(std::shared_ptr<RHIResource> resource) noexcept;

    private:
        RTLightType type_ = RTLightType::Directional;
        Vector3 color_ = Vector3::One();
        Vector3 direction_ = Vector3::UnitZ();
        Float32 intensity_ = 1.0f;
        Float32 range_ = 0.0f;
        Float32 innerConeAngleRadians_ = 0.0f;
        Float32 outerConeAngleRadians_ = 0.0f;
        bool castShadows_ = false;
        Matrix44 localToWorld_ = Matrix44::Identity();
        std::shared_ptr<RHIResource> uniformBufferResource_;
    };

    /// Render-thread scene owned by a Scene and later consumed by renderer/viewports/render textures.
    class RTScene final : public NonCopyable
    {
    public:
        RTScene() = default;
        ~RTScene() = default;

        void AddRenderItem(std::shared_ptr<RTRenderItem> item);
        void RemoveRenderItem(const std::shared_ptr<RTRenderItem>& item) noexcept;
        void AddCamera(std::shared_ptr<RTCamera> camera);
        void RemoveCamera(const std::shared_ptr<RTCamera>& camera) noexcept;
        void AddLight(std::shared_ptr<RTLight> light);
        void RemoveLight(const std::shared_ptr<RTLight>& light) noexcept;
        void Clear() noexcept;

        [[nodiscard]] SizeT GetRenderItemCount() const noexcept;
        [[nodiscard]] std::shared_ptr<RTRenderItem> GetRenderItem(SizeT index) const noexcept;
        [[nodiscard]] SizeT GetCameraCount() const noexcept;
        [[nodiscard]] std::shared_ptr<RTCamera> GetCamera(SizeT index) const noexcept;
        [[nodiscard]] SizeT GetLightCount() const noexcept;
        [[nodiscard]] std::shared_ptr<RTLight> GetLight(SizeT index) const noexcept;

    private:
        std::vector<std::shared_ptr<RTRenderItem>> renderItems_;
        std::vector<std::shared_ptr<RTCamera>> cameras_;
        std::vector<std::shared_ptr<RTLight>> lights_;
    };
} // namespace ve
