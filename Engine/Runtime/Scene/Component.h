#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"

namespace ve
{
    class GameObject;
    class Scene;

    class Component : public NonMovable
    {
    public:
        virtual ~Component();

        [[nodiscard]] GameObject& GetGameObject() noexcept;
        [[nodiscard]] const GameObject& GetGameObject() const noexcept;

        [[nodiscard]] Scene& GetScene() noexcept;
        [[nodiscard]] const Scene& GetScene() const noexcept;

        [[nodiscard]] bool IsEnabled() const noexcept;
        void SetEnabled(bool enabled);

        [[nodiscard]] bool IsActiveAndEnabled() const noexcept;

    protected:
        Component() = default;

        virtual void OnCreate() {}

        virtual void OnDestroy() {}

        virtual void OnEnable() {}

        virtual void OnDisable() {}

        virtual void OnUpdate() {}

        virtual void OnLateUpdate() {}

    private:
        friend class GameObject;
        friend class Scene;

        void AttachToGameObject(GameObject& owner) noexcept;
        void DetachFromGameObject() noexcept;
        void DispatchCreate();
        void DispatchDestroy();
        void DispatchEnable();
        void DispatchDisable();
        void DispatchUpdate();
        void DispatchLateUpdate();

        GameObject* owner_ = nullptr;
        bool enabled_ = true;
        bool created_ = false;
        bool activeAndEnabled_ = false;
    };
} // namespace ve
