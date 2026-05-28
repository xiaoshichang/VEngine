#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Physics/PhysicsGeometry.h"
#include "Engine/Runtime/Scene/Component.h"

namespace ve
{
    enum class ColliderShape
    {
        Box,
        Sphere,
    };

    class TransformComponent;

    class ColliderComponent final : public Component
    {
    public:
        [[nodiscard]] ColliderShape GetShape() const noexcept;
        void SetShape(ColliderShape shape);

        [[nodiscard]] const Vector3& GetCenter() const noexcept;
        void SetCenter(const Vector3& center);

        [[nodiscard]] const Vector3& GetBoxSize() const noexcept;
        void SetBoxSize(const Vector3& size);

        [[nodiscard]] Float32 GetSphereRadius() const noexcept;
        void SetSphereRadius(Float32 radius);

        [[nodiscard]] UInt64 GetLayer() const noexcept;
        void SetLayer(UInt64 layer);

        [[nodiscard]] UInt64 GetCollidesWith() const noexcept;
        void SetCollidesWith(UInt64 mask);

        [[nodiscard]] bool IsTrigger() const noexcept;
        void SetTrigger(bool trigger);

        [[nodiscard]] bool IsColliderEnabled() const noexcept;
        void SetColliderEnabled(bool enabled);

        [[nodiscard]] Sphere BuildWorldSphere(const TransformComponent& transform) const noexcept;
        [[nodiscard]] OrientedBox BuildWorldBox(const TransformComponent& transform) const noexcept;

    private:
        ColliderShape shape_ = ColliderShape::Box;
        Vector3 center_ = Vector3::Zero();
        Vector3 boxSize_ = Vector3::One();
        Float32 sphereRadius_ = 0.5f;
        UInt64 layer_ = 1ull;
        UInt64 collidesWith_ = ~0ull;
        bool isTrigger_ = false;
        bool enabled_ = true;
    };
} // namespace ve
