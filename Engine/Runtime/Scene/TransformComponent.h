#pragma once

#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/Component.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ve
{
    class GameObject;
    class Scene;
    class SceneSerialization;

    /// Local transform data attached to a GameObject.
    class TransformComponent final : public Component
    {
    public:
        TransformComponent(Scene& scene, GameObject& owner) noexcept;
        ~TransformComponent() override;

        [[nodiscard]] const Vector3& GetLocalPosition() const noexcept;
        void SetLocalPosition(const Vector3& position) noexcept;

        [[nodiscard]] const Quaternion& GetLocalRotation() const noexcept;
        void SetLocalRotation(const Quaternion& rotation) noexcept;

        [[nodiscard]] const Vector3& GetLocalScale() const noexcept;
        void SetLocalScale(const Vector3& scale) noexcept;

        [[nodiscard]] Matrix44 GetLocalMatrix() const noexcept;
        [[nodiscard]] Matrix44 GetWorldMatrix() const noexcept;

        [[nodiscard]] TransformComponent* GetParent() noexcept;
        [[nodiscard]] const TransformComponent* GetParent() const noexcept;

        [[nodiscard]] SizeT GetChildCount() const noexcept;
        [[nodiscard]] TransformComponent* GetChild(SizeT index) noexcept;
        [[nodiscard]] const TransformComponent* GetChild(SizeT index) const noexcept;

        [[nodiscard]] GameObject* GetChildGameObject(SizeT index) noexcept;
        [[nodiscard]] const GameObject* GetChildGameObject(SizeT index) const noexcept;

        [[nodiscard]] bool DestroyChild(GameObject& child) noexcept;
        void ClearChildren() noexcept;

        using TransformChangedCallback = std::function<void()>;

        [[nodiscard]] UInt64 AddTransformChangedCallback(TransformChangedCallback callback);
        void RemoveTransformChangedCallback(UInt64 callbackId) noexcept;

    private:
        friend class GameObject;
        friend class Scene;
        friend class SceneSerialization;

        [[nodiscard]] Result<GameObject*> CreateChild(std::string name = {});
        void SetParent(TransformComponent* parent) noexcept;
        void MarkHierarchyDirty() noexcept;
        void NotifyTransformChanged() noexcept;
        void UpdateWorldCache() const noexcept;

        struct TransformChangedCallbackEntry
        {
            UInt64 id = 0;
            TransformChangedCallback callback;
        };

        Vector3 localPosition_ = Vector3::Zero();
        Quaternion localRotation_ = Quaternion::Identity();
        Vector3 localScale_ = Vector3::One();
        TransformComponent* parent_ = nullptr;
        std::vector<std::unique_ptr<GameObject>> children_;
        std::vector<TransformChangedCallbackEntry> transformChangedCallbacks_;
        mutable Matrix44 localMatrixCache_ = Matrix44::Identity();
        mutable Matrix44 worldMatrixCache_ = Matrix44::Identity();
        mutable bool transformDirty_ = true;
        UInt64 nextTransformChangedCallbackId_ = 1;
    };
} // namespace ve
