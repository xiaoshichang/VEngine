#pragma once

#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/Component.h"

namespace ve
{
    class TransformComponent final : public Component
    {
    public:
        [[nodiscard]] const Vector3& GetLocalPosition() const noexcept;
        void SetLocalPosition(const Vector3& position);

        [[nodiscard]] const Quaternion& GetLocalRotation() const noexcept;
        void SetLocalRotation(const Quaternion& rotation);

        [[nodiscard]] const Vector3& GetLocalScale() const noexcept;
        void SetLocalScale(const Vector3& scale);

        [[nodiscard]] const Matrix44& GetLocalMatrix() const noexcept;
        [[nodiscard]] const Matrix44& GetWorldMatrix() const noexcept;
        [[nodiscard]] Vector3 GetWorldPosition() const noexcept;
        [[nodiscard]] Vector3 GetForward() const noexcept;

        [[nodiscard]] bool IsDirty() const noexcept;
        void MarkDirty() noexcept;
        void UpdateWorldTransform(const Matrix44& parentWorld);

    private:
        friend class GameObject;

        void RebuildLocalMatrix();

        Vector3 localPosition_ = Vector3::Zero();
        Quaternion localRotation_ = Quaternion::Identity();
        Vector3 localScale_ = Vector3::One();
        Matrix44 localMatrix_ = Matrix44::Identity();
        Matrix44 worldMatrix_ = Matrix44::Identity();
        bool dirty_ = true;
    };
} // namespace ve
