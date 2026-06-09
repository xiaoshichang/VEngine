#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Scene/Component.h"
#include "Engine/Runtime/Scene/GameObject.h"

#include <iostream>
#include <type_traits>

namespace
{
    template<typename T>
    concept HasGetParent = requires(T& value) { value.GetParent(); };

    template<typename T>
    concept HasGetChildCount = requires(const T& value) { value.GetChildCount(); };

    template<typename T>
    concept HasGetChild = requires(T& value) { value.GetChild(0); };

    template<typename T>
    concept HasCreateChild = requires(T& value) { value.CreateChild(); };

    template<typename T>
    concept HasDestroyChild = requires(T& value, ve::GameObject& child) { value.DestroyChild(child); };

    template<typename T>
    concept HasClearChildren = requires(T& value) { value.ClearChildren(); };

    template<typename T>
    concept HasIsActive = requires(const T& value) { value.IsActive(); };

    template<typename T>
    concept HasSetActive = requires(T& value) { value.SetActive(true); };

    static_assert(!HasGetParent<ve::GameObject>, "GameObject should no longer expose GetParent");
    static_assert(!HasGetChildCount<ve::GameObject>, "GameObject should no longer expose GetChildCount");
    static_assert(!HasGetChild<ve::GameObject>, "GameObject should no longer expose GetChild");
    static_assert(!HasCreateChild<ve::GameObject>, "GameObject should no longer expose CreateChild");
    static_assert(!HasDestroyChild<ve::GameObject>, "GameObject should no longer expose DestroyChild");
    static_assert(!HasClearChildren<ve::GameObject>, "GameObject should no longer expose ClearChildren");
    static_assert(!HasIsActive<ve::GameObject>, "GameObject should no longer expose IsActive");
    static_assert(!HasSetActive<ve::GameObject>, "GameObject should no longer expose SetActive");

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool TestGameObjectStartsWithTransformComponent()
    {
        bool passed = true;

        ve::GameObject gameObject("Root");
        ve::TransformComponent* transform = gameObject.GetComponent<ve::TransformComponent>();

        passed &= Expect(transform != nullptr, "GameObject should have TransformComponent by default");
        passed &= Expect(gameObject.GetComponentCount() == 1, "Default GameObject should own one component");
        passed &= Expect(gameObject.GetComponent<ve::LightComponent>() == nullptr,
                         "GameObject should not allocate LightComponent by default");

        return passed;
    }

    bool TestComponentSlotsAreUniquePerType()
    {
        bool passed = true;

        ve::GameObject gameObject;

        ve::Result<ve::LightComponent*> addLightResult = gameObject.AddComponent<ve::LightComponent>();
        passed &= Expect(addLightResult.IsOk(), "Adding first LightComponent should succeed");

        ve::LightComponent* light = nullptr;
        if (addLightResult.IsOk())
        {
            light = addLightResult.GetValue();
        }

        ve::Result<ve::LightComponent*> addSecondLightResult = gameObject.AddComponent<ve::LightComponent>();
        passed &= Expect(!addSecondLightResult.IsOk(), "Adding duplicate LightComponent should fail");
        passed &= Expect(addSecondLightResult.GetError().GetCode() == ve::ErrorCode::InvalidState,
                         "Duplicate LightComponent should report InvalidState");
        passed &= Expect(light != nullptr && gameObject.GetComponent<ve::LightComponent>() == light,
                         "GetComponent<LightComponent> should return the original slot instance");
        passed &= Expect(gameObject.GetComponentCount() == 2,
                         "GameObject should count Transform + Light after first successful add");

        return passed;
    }

    bool TestTransformOwnsHierarchyRelationships()
    {
        bool passed = true;

        ve::GameObject root("Root");
        ve::TransformComponent* rootTransform = root.GetComponent<ve::TransformComponent>();
        passed &= Expect(rootTransform != nullptr, "Root GameObject should expose TransformComponent");
        if (rootTransform == nullptr)
        {
            return false;
        }

        ve::Result<ve::GameObject*> createChildResult = rootTransform->CreateChild("Child");
        passed &= Expect(createChildResult.IsOk(), "TransformComponent should create child GameObject");
        if (!createChildResult.IsOk())
        {
            return false;
        }

        ve::GameObject* child = createChildResult.GetValue();
        ve::TransformComponent* childTransform = child->GetComponent<ve::TransformComponent>();
        passed &= Expect(childTransform != nullptr, "Child GameObject should expose TransformComponent");
        if (childTransform == nullptr)
        {
            return false;
        }

        passed &= Expect(rootTransform->GetChildCount() == 1, "Parent transform should track one child");
        passed &= Expect(rootTransform->GetChild(0) == childTransform,
                         "Parent transform should return child transform");
        passed &= Expect(childTransform->GetParent() == rootTransform, "Child transform should reference parent");
        passed &= Expect(rootTransform->GetChildGameObject(0) == child,
                         "Transform should expose child GameObject at index");

        passed &= Expect(rootTransform->DestroyChild(*child), "TransformComponent should destroy owned child");
        passed &= Expect(rootTransform->GetChildCount() == 0, "Parent transform should have no children after destroy");

        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestGameObjectStartsWithTransformComponent();
    passed &= TestComponentSlotsAreUniquePerType();
    passed &= TestTransformOwnsHierarchyRelationships();

    if (passed)
    {
        std::cout << "VEngineSceneTests passed" << '\n';
        return 0;
    }

    return 1;
}
