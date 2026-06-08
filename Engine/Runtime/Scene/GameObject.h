#pragma once

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Scene/Component.h"

#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ve
{
/// Node in a Scene-owned hierarchy.
///
/// GameObject owns its children and Components. A GameObject may be reparented in future milestones, but this first
/// Scene skeleton keeps ownership explicit through unique pointers.
class GameObject final : public NonMovable
{
public:
    GameObject();
    explicit GameObject(std::string name);
    ~GameObject();

    [[nodiscard]] const std::string& GetName() const noexcept;
    void SetName(std::string name);

    [[nodiscard]] bool IsActive() const noexcept;
    void SetActive(bool active) noexcept;

    [[nodiscard]] GameObject* GetParent() noexcept;
    [[nodiscard]] const GameObject* GetParent() const noexcept;

    [[nodiscard]] SizeT GetChildCount() const noexcept;
    [[nodiscard]] GameObject* GetChild(SizeT index) noexcept;
    [[nodiscard]] const GameObject* GetChild(SizeT index) const noexcept;

    [[nodiscard]] Result<GameObject*> CreateChild(std::string name = {});
    [[nodiscard]] bool DestroyChild(GameObject& child) noexcept;
    void ClearChildren() noexcept;

    [[nodiscard]] SizeT GetComponentCount() const noexcept;
    [[nodiscard]] Component* GetComponent(SizeT index) noexcept;
    [[nodiscard]] const Component* GetComponent(SizeT index) const noexcept;

    template <typename TComponent, typename... TArgs>
    [[nodiscard]] Result<TComponent*> AddComponent(TArgs&&... args)
    {
        static_assert(std::is_base_of_v<Component, TComponent>, "TComponent must derive from ve::Component.");

        try
        {
            std::unique_ptr<TComponent> component = std::make_unique<TComponent>(std::forward<TArgs>(args)...);
            TComponent* componentPointer = component.get();
            componentPointer->SetOwner(this);
            components_.push_back(std::move(component));
            return Result<TComponent*>::Success(componentPointer);
        }
        catch (const std::bad_alloc&)
        {
            return Result<TComponent*>::Failure(Error(ErrorCode::OutOfMemory, "GameObject component allocation failed."));
        }
    }

    template <typename TComponent>
    [[nodiscard]] TComponent* GetComponent() noexcept
    {
        static_assert(std::is_base_of_v<Component, TComponent>, "TComponent must derive from ve::Component.");

        for (const std::unique_ptr<Component>& component : components_)
        {
            if (TComponent* typedComponent = dynamic_cast<TComponent*>(component.get()))
            {
                return typedComponent;
            }
        }

        return nullptr;
    }

    template <typename TComponent>
    [[nodiscard]] const TComponent* GetComponent() const noexcept
    {
        static_assert(std::is_base_of_v<Component, TComponent>, "TComponent must derive from ve::Component.");

        for (const std::unique_ptr<Component>& component : components_)
        {
            if (const TComponent* typedComponent = dynamic_cast<const TComponent*>(component.get()))
            {
                return typedComponent;
            }
        }

        return nullptr;
    }

    template <typename TComponent>
    [[nodiscard]] bool RemoveComponent() noexcept
    {
        static_assert(std::is_base_of_v<Component, TComponent>, "TComponent must derive from ve::Component.");

        for (auto it = components_.begin(); it != components_.end(); ++it)
        {
            if (dynamic_cast<TComponent*>(it->get()) != nullptr)
            {
                (*it)->SetOwner(nullptr);
                components_.erase(it);
                return true;
            }
        }

        return false;
    }

    void Update(Float32 deltaSeconds);

private:
    void SetParent(GameObject* parent) noexcept;

    std::string name_;
    GameObject* parent_ = nullptr;
    std::vector<std::unique_ptr<GameObject>> children_;
    std::vector<std::unique_ptr<Component>> components_;
    bool active_ = true;
};
}
