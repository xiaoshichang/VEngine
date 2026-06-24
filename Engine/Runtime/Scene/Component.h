#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

namespace ve
{
    class GameObject;
    class Scene;

    /// Base type for data and behavior attached to a GameObject.
    ///
    /// Components are owned by exactly one GameObject. Scene update traverses the GameObject tree and calls OnUpdate()
    /// for enabled GameObjects and enabled Components.
    class Component : public NonCopyable
    {
    public:
        virtual ~Component() = default;

        /// Returns the GameObject that owns this component, or nullptr during teardown after removal.
        [[nodiscard]] GameObject* GetOwner() noexcept;

        /// Returns the GameObject that owns this component, or nullptr during teardown after removal.
        [[nodiscard]] const GameObject* GetOwner() const noexcept;

        /// Returns true when this component participates in Scene update.
        [[nodiscard]] bool IsEnabled() const noexcept;

        /// Enables or disables Scene update for this component.
        virtual void SetEnabled(bool enabled) noexcept;

        /// Called after this component is attached to a GameObject.
        virtual void OnCreate();

        /// Called before this component is detached from a GameObject or destroyed with its owner.
        virtual void OnDestroy();

        /// Called by Scene update while this component and its owner are enabled.
        virtual void OnUpdate(Float32 deltaSeconds);

        /// Called after all Scene update work for the frame has completed.
        virtual void OnLateUpdate(Float32 deltaSeconds);

        /// Called when this component becomes enabled.
        virtual void OnEnable();

        /// Called when this component becomes disabled.
        virtual void OnDisable();

    private:
        friend class GameObject;

        void ClearOwner() noexcept;

    protected:
        Component(Scene& scene, GameObject& owner) noexcept;

        [[nodiscard]] Scene* GetScene() noexcept;
        [[nodiscard]] const Scene* GetScene() const noexcept;

        GameObject* owner_ = nullptr;
        Scene* scene_ = nullptr;
        bool enabled_ = true;
    };
} // namespace ve
