#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"

#include <memory>
#include <new>
#include <string>
#include <vector>

namespace ve
{
    class GameObject;

    /// Base type for data and behavior attached to a GameObject.
    ///
    /// Components are owned by exactly one GameObject. Scene update traverses the GameObject tree and calls OnUpdate()
    /// for enabled GameObjects and enabled Components.
    class Component : public NonCopyable
    {
    public:
        Component() = default;
        virtual ~Component() = default;

        /// Returns the GameObject that owns this component, or nullptr before attachment.
        [[nodiscard]] GameObject* GetOwner() noexcept;

        /// Returns the GameObject that owns this component, or nullptr before attachment.
        [[nodiscard]] const GameObject* GetOwner() const noexcept;

        /// Returns true when this component participates in Scene update.
        [[nodiscard]] bool IsEnabled() const noexcept;

        /// Enables or disables Scene update for this component.
        void SetEnabled(bool enabled) noexcept;

        /// Called by Scene update while this component and its owner are enabled.
        virtual void OnUpdate(Float32 deltaSeconds);

    private:
        friend class GameObject;

        void SetOwner(GameObject* owner) noexcept;

        GameObject* owner_ = nullptr;
        bool enabled_ = true;
    };

    /// Local transform data attached to a GameObject.
    class TransformComponent final : public Component
    {
    public:
        ~TransformComponent() override;

        [[nodiscard]] const Vector3& GetLocalPosition() const noexcept;
        void SetLocalPosition(const Vector3& position) noexcept;

        [[nodiscard]] const Quaternion& GetLocalRotation() const noexcept;
        void SetLocalRotation(const Quaternion& rotation) noexcept;

        [[nodiscard]] const Vector3& GetLocalScale() const noexcept;
        void SetLocalScale(const Vector3& scale) noexcept;

        [[nodiscard]] Matrix44 GetLocalMatrix() const noexcept;

        [[nodiscard]] TransformComponent* GetParent() noexcept;
        [[nodiscard]] const TransformComponent* GetParent() const noexcept;

        [[nodiscard]] SizeT GetChildCount() const noexcept;
        [[nodiscard]] TransformComponent* GetChild(SizeT index) noexcept;
        [[nodiscard]] const TransformComponent* GetChild(SizeT index) const noexcept;

        [[nodiscard]] GameObject* GetChildGameObject(SizeT index) noexcept;
        [[nodiscard]] const GameObject* GetChildGameObject(SizeT index) const noexcept;

        [[nodiscard]] Result<GameObject*> CreateChild(std::string name = {});
        [[nodiscard]] bool DestroyChild(GameObject& child) noexcept;
        void ClearChildren() noexcept;

    private:
        friend class GameObject;

        void SetParent(TransformComponent* parent) noexcept;

        Vector3 localPosition_ = Vector3::Zero();
        Quaternion localRotation_ = Quaternion::Identity();
        Vector3 localScale_ = Vector3::One();
        TransformComponent* parent_ = nullptr;
        std::vector<std::unique_ptr<GameObject>> children_;
    };

    /// Renderable static mesh attachment used by the first-stage forward renderer.
    class MeshRenderComponent final : public Component
    {
    public:
        [[nodiscard]] const std::string& GetMeshAssetPath() const noexcept;
        void SetMeshAssetPath(std::string meshAssetPath);

        [[nodiscard]] const std::string& GetMaterialAssetPath() const noexcept;
        void SetMaterialAssetPath(std::string materialAssetPath);

        [[nodiscard]] bool IsVisible() const noexcept;
        void SetVisible(bool visible) noexcept;

    private:
        std::string meshAssetPath_;
        std::string materialAssetPath_;
        bool visible_ = true;
    };

    /// Camera data used to build view and projection state for rendering.
    class CameraComponent final : public Component
    {
    public:
        [[nodiscard]] bool IsPrimary() const noexcept;
        void SetPrimary(bool primary) noexcept;

        [[nodiscard]] Float32 GetVerticalFieldOfViewRadians() const noexcept;
        void SetVerticalFieldOfViewRadians(Float32 fieldOfViewRadians) noexcept;

        [[nodiscard]] Float32 GetNearClipPlane() const noexcept;
        void SetNearClipPlane(Float32 nearClipPlane) noexcept;

        [[nodiscard]] Float32 GetFarClipPlane() const noexcept;
        void SetFarClipPlane(Float32 farClipPlane) noexcept;

    private:
        bool primary_ = false;
        Float32 verticalFieldOfViewRadians_ = 1.0471975512f;
        Float32 nearClipPlane_ = 0.1f;
        Float32 farClipPlane_ = 1000.0f;
    };

    /// Supported light kinds for the first-stage Scene model.
    enum class LightType
    {
        Directional,
        Point,
    };

    /// Light data consumed by future render-world extraction.
    class LightComponent final : public Component
    {
    public:
        [[nodiscard]] LightType GetLightType() const noexcept;
        void SetLightType(LightType type) noexcept;

        [[nodiscard]] const Vector3& GetColor() const noexcept;
        void SetColor(const Vector3& color) noexcept;

        [[nodiscard]] Float32 GetIntensity() const noexcept;
        void SetIntensity(Float32 intensity) noexcept;

    private:
        LightType type_ = LightType::Directional;
        Vector3 color_ = Vector3::One();
        Float32 intensity_ = 1.0f;
    };
} // namespace ve
