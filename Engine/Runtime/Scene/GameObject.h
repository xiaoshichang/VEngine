#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Scene/Component.h"
#include "Engine/Runtime/Scene/SceneTypes.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ve
{
    class Scene;

    class GameObject : public NonMovable
    {
    public:
        ~GameObject();

        [[nodiscard]] SceneObjectId GetId() const noexcept;

        [[nodiscard]] const std::string& GetName() const noexcept;
        void SetName(std::string name);

        [[nodiscard]] bool IsActiveSelf() const noexcept;
        [[nodiscard]] bool IsActiveInHierarchy() const noexcept;
        void SetActive(bool active);

        [[nodiscard]] Scene& GetScene() noexcept;
        [[nodiscard]] const Scene& GetScene() const noexcept;

        [[nodiscard]] GameObject* GetParent() noexcept;
        [[nodiscard]] const GameObject* GetParent() const noexcept;
        [[nodiscard]] const std::vector<GameObject*>& GetChildren() const noexcept;
        void SetParent(GameObject* parent);

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
            auto component = std::make_unique<T>(std::forward<Args>(args)...);
            return static_cast<T&>(AddComponent(std::move(component)));
        }

        template<typename T>
        [[nodiscard]] T* GetComponent() noexcept
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
            for (std::unique_ptr<Component>& component : components_)
            {
                if (auto* typedComponent = dynamic_cast<T*>(component.get()))
                {
                    return typedComponent;
                }
            }

            return nullptr;
        }

        template<typename T>
        [[nodiscard]] const T* GetComponent() const noexcept
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
            for (const std::unique_ptr<Component>& component : components_)
            {
                if (const auto* typedComponent = dynamic_cast<const T*>(component.get()))
                {
                    return typedComponent;
                }
            }

            return nullptr;
        }

        [[nodiscard]] const std::vector<std::unique_ptr<Component>>& GetComponents() const noexcept;

        Component& AddComponent(std::unique_ptr<Component> component);
        bool RemoveComponent(Component& component);

        template<typename T>
        bool RemoveComponent()
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
            if (T* component = GetComponent<T>())
            {
                return RemoveComponent(*component);
            }

            return false;
        }

    private:
        friend class Component;
        friend class Scene;
        friend class TransformComponent;

        GameObject(Scene& scene, SceneObjectId id, std::string name);

        void DestroyComponents() noexcept;
        void RefreshActiveInHierarchy();
        void RefreshComponentsActiveState();
        void MarkTransformHierarchyDirty();
        void DetachFromParent() noexcept;
        void AttachToParent(GameObject& parent);
        [[nodiscard]] bool IsAncestorOf(const GameObject& object) const noexcept;

        Scene* scene_ = nullptr;
        SceneObjectId id_ = InvalidSceneObjectId;
        std::string name_;
        bool activeSelf_ = true;
        bool activeInHierarchy_ = true;
        GameObject* parent_ = nullptr;
        std::vector<GameObject*> children_;
        std::vector<std::unique_ptr<Component>> components_;
    };
} // namespace ve
