#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/Component.h"

namespace ve
{
    enum class ColliderShapeType
    {
        Box,
        Sphere,
        Capsule,
    };

    enum class ColliderDirectionAxis
    {
        X,
        Y,
        Z,
    };

    struct ColliderDesc
    {
        ColliderShapeType shapeType = ColliderShapeType::Box;
        bool trigger = false;
        Vector3 center = Vector3::Zero();
        Vector3 size = Vector3::One();
        Float32 radius = 0.5f;
        Float32 height = 2.0f;
        ColliderDirectionAxis direction = ColliderDirectionAxis::Y;
        Float32 staticFriction = 0.6f;
        Float32 dynamicFriction = 0.6f;
        Float32 bounciness = 0.0f;
    };

    class ColliderBackend final
    {
    public:
        [[nodiscard]] bool IsRuntimeShapeDirty() const noexcept;
        void MarkRuntimeShapeDirty() noexcept;
        void ClearRuntimeShapeDirty() noexcept;

    private:
        bool runtimeShapeDirty_ = true;
    };

    class ColliderComponent final : public Component
    {
    public:
        ColliderComponent(Scene& scene, GameObject& owner) noexcept;
        ~ColliderComponent() override = default;

        [[nodiscard]] const ColliderDesc& GetDesc() const noexcept;
        void SetDesc(const ColliderDesc& desc) noexcept;

        [[nodiscard]] ColliderBackend& GetBackend() noexcept;
        [[nodiscard]] const ColliderBackend& GetBackend() const noexcept;

        [[nodiscard]] ColliderShapeType GetShapeType() const noexcept;
        void SetShapeType(ColliderShapeType shapeType) noexcept;

        [[nodiscard]] bool IsTrigger() const noexcept;
        void SetTrigger(bool trigger) noexcept;

        [[nodiscard]] const Vector3& GetCenter() const noexcept;
        void SetCenter(const Vector3& center) noexcept;

        [[nodiscard]] const Vector3& GetSize() const noexcept;
        void SetSize(const Vector3& size) noexcept;

        [[nodiscard]] Float32 GetRadius() const noexcept;
        void SetRadius(Float32 radius) noexcept;

        [[nodiscard]] Float32 GetHeight() const noexcept;
        void SetHeight(Float32 height) noexcept;

        [[nodiscard]] ColliderDirectionAxis GetDirection() const noexcept;
        void SetDirection(ColliderDirectionAxis direction) noexcept;

        [[nodiscard]] Float32 GetStaticFriction() const noexcept;
        void SetStaticFriction(Float32 staticFriction) noexcept;

        [[nodiscard]] Float32 GetDynamicFriction() const noexcept;
        void SetDynamicFriction(Float32 dynamicFriction) noexcept;

        [[nodiscard]] Float32 GetBounciness() const noexcept;
        void SetBounciness(Float32 bounciness) noexcept;

    private:
        void MarkBackendDirty() noexcept;

        ColliderDesc desc_;
        ColliderBackend backend_;
    };
} // namespace ve
