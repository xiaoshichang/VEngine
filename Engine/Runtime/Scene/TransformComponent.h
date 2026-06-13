#pragma once

#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/Component.h"

#include <memory>
#include <string>
#include <vector>

namespace ve
{
    class GameObject;

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
} // namespace ve
