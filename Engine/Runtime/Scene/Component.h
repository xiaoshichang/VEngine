#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

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
} // namespace ve
