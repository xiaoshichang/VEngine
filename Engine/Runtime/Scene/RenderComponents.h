#pragma once

#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Vector2.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Math/Vector4.h"
#include "Engine/Runtime/Resource/ResourceHandle.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/Component.h"

namespace ve
{
    enum class CameraProjectionMode
    {
        Perspective,
        Orthographic,
    };

    class CameraComponent final : public Component
    {
    public:
        [[nodiscard]] CameraProjectionMode GetProjectionMode() const noexcept;
        void SetProjectionMode(CameraProjectionMode mode);

        [[nodiscard]] Float32 GetFieldOfViewRadians() const noexcept;
        void SetFieldOfViewRadians(Float32 radians);

        [[nodiscard]] Float32 GetOrthographicSize() const noexcept;
        void SetOrthographicSize(Float32 size);

        [[nodiscard]] Float32 GetNearPlane() const noexcept;
        void SetNearPlane(Float32 nearPlane);

        [[nodiscard]] Float32 GetFarPlane() const noexcept;
        void SetFarPlane(Float32 farPlane);

        [[nodiscard]] const Vector4& GetViewportRect() const noexcept;
        void SetViewportRect(const Vector4& viewportRect);

        [[nodiscard]] const Vector4& GetClearColor() const noexcept;
        void SetClearColor(const Vector4& clearColor);

        [[nodiscard]] Matrix44 GetViewMatrix() const noexcept;
        [[nodiscard]] Matrix44 GetProjectionMatrix(Float32 aspectRatio) const noexcept;

    private:
        CameraProjectionMode projectionMode_ = CameraProjectionMode::Perspective;
        Float32 fieldOfViewRadians_ = ToRadians(60.0f);
        Float32 orthographicSize_ = 10.0f;
        Float32 nearPlane_ = 0.1f;
        Float32 farPlane_ = 1000.0f;
        Vector4 viewportRect_ = Vector4(0.0f, 0.0f, 1.0f, 1.0f);
        Vector4 clearColor_ = Vector4(0.05f, 0.07f, 0.10f, 1.0f);
    };

    class MeshRendererComponent final : public Component
    {
    public:
        [[nodiscard]] ResourceHandle<MeshResource> GetMesh() const noexcept;
        void SetMesh(ResourceHandle<MeshResource> mesh);

        [[nodiscard]] ResourceHandle<MaterialResource> GetMaterial() const noexcept;
        void SetMaterial(ResourceHandle<MaterialResource> material);

        [[nodiscard]] bool IsVisible() const noexcept;
        void SetVisible(bool visible);

        [[nodiscard]] const Vector3& GetLocalBoundsCenter() const noexcept;
        [[nodiscard]] const Vector3& GetLocalBoundsExtents() const noexcept;
        void SetLocalBounds(const Vector3& center, const Vector3& extents);

    private:
        ResourceHandle<MeshResource> mesh_;
        ResourceHandle<MaterialResource> material_;
        bool visible_ = true;
        Vector3 localBoundsCenter_ = Vector3::Zero();
        Vector3 localBoundsExtents_ = Vector3(0.5f, 0.5f, 0.5f);
    };

    enum class LightType
    {
        Directional,
    };

    class LightComponent final : public Component
    {
    public:
        [[nodiscard]] LightType GetLightType() const noexcept;
        void SetLightType(LightType lightType);

        [[nodiscard]] const Vector3& GetColor() const noexcept;
        void SetColor(const Vector3& color);

        [[nodiscard]] Float32 GetIntensity() const noexcept;
        void SetIntensity(Float32 intensity);

        [[nodiscard]] Vector3 GetDirection() const noexcept;

    private:
        LightType lightType_ = LightType::Directional;
        Vector3 color_ = Vector3::One();
        Float32 intensity_ = 1.0f;
    };
} // namespace ve
