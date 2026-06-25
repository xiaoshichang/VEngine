#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneSerialization.h"
#include "Engine/Runtime/Scripting/DotNetScriptingBackend.h"
#include "Engine/Runtime/Scripting/DotnetScriptableComponent.h"
#include "Engine/Runtime/Scripting/IOSAOTScriptingBackend.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"
#include "Engine/Runtime/Scripting/WindowsJITScriptingBackend.h"

#include <iostream>
#include <string>
#include <type_traits>

namespace
{
    template<typename TComponent>
    concept HasSetScriptingSystem = requires(TComponent& component, ve::ScriptingSystem* scriptingSystem)
    {
        component.SetScriptingSystem(scriptingSystem);
    };

    template<typename TComponent>
    concept HasSetScriptTypeName = requires(TComponent& component, std::string scriptTypeName)
    {
        component.SetScriptTypeName(std::move(scriptTypeName));
    };

    template<typename TScene>
    concept HasLoadSceneWithoutScriptingSystem = requires(TScene& scene, std::string text)
    {
        ve::SceneSerialization::LoadFromString(scene, text);
    };

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool TestRequiredScriptingSystem()
    {
        bool passed = true;

        ve::ScriptingSystem scriptingSystem;
        passed &= Expect(scriptingSystem.Initialize(ve::ScriptingSystemInitParam{}) == ve::ErrorCode::None, "ScriptingSystem should initialize");
        passed &= Expect(scriptingSystem.GetBackendType() == ve::ScriptingBackendType::WindowsJIT, "Default Windows backend should be WindowsJIT");
        passed &= Expect(scriptingSystem.LoadAssembly(ve::ScriptingAssemblyLoadDesc{}) == ve::ErrorCode::InvalidArgument,
                         "ScriptingSystem should reject an empty assembly path");

        ve::Result<ve::ScriptInstanceHandle> createResult = scriptingSystem.CreateScriptInstance(ve::ScriptInstanceDesc{nullptr, "Example.Script"});
        passed &= Expect(!createResult.IsOk(), "ScriptingSystem should reject script instance creation before assembly load");
        passed &= Expect(createResult.GetError().GetCode() == ve::ErrorCode::InvalidState,
                         "ScriptingSystem script creation before assembly load should be InvalidState");

        scriptingSystem.Shutdown();
        return passed;
    }

    bool TestScriptingInterfaces()
    {
        static_assert(std::is_abstract_v<ve::DotNetScriptingBackend>, "DotNetScriptingBackend should be an interface.");
        static_assert(std::is_base_of_v<ve::DotNetScriptingBackend, ve::WindowsJITScriptingBackend>,
                      "WindowsJITScriptingBackend should implement the DotNet scripting interface.");
        static_assert(std::is_base_of_v<ve::DotNetScriptingBackend, ve::IOSAOTScriptingBackend>,
                      "IOSAOTScriptingBackend should implement the DotNet scripting interface.");
        static_assert(std::is_abstract_v<ve::ScriptableComponent>, "ScriptableComponent should be an interface.");
        static_assert(std::is_base_of_v<ve::ScriptableComponent, ve::DotnetScriptableComponent>,
                      "DotnetScriptableComponent should implement the ScriptableComponent interface.");
        static_assert(!HasSetScriptingSystem<ve::ScriptableComponent>, "ScriptableComponent should not expose ScriptingSystem mutation.");
        static_assert(!HasSetScriptTypeName<ve::ScriptableComponent>, "ScriptableComponent should not expose script type mutation.");
        static_assert(!HasSetScriptingSystem<ve::DotnetScriptableComponent>, "DotnetScriptableComponent should not expose ScriptingSystem mutation.");
        static_assert(!HasSetScriptTypeName<ve::DotnetScriptableComponent>, "DotnetScriptableComponent should not expose script type mutation.");
        static_assert(!std::is_constructible_v<ve::DotnetScriptableComponent, ve::Scene&, ve::GameObject&>,
                      "DotnetScriptableComponent should require a script type and scripting system.");
        static_assert(!std::is_constructible_v<ve::DotnetScriptableComponent, ve::Scene&, ve::GameObject&, std::string>,
                      "DotnetScriptableComponent should not support script-type-only construction.");
        static_assert(std::is_constructible_v<ve::DotnetScriptableComponent, ve::Scene&, ve::GameObject&, std::string, ve::ScriptingSystem&>,
                      "Runtime DotnetScriptableComponent construction should require a ScriptingSystem reference.");
        static_assert(!std::is_constructible_v<ve::DotnetScriptableComponent, ve::Scene&, ve::GameObject&, std::string, ve::ScriptingSystem*>,
                      "DotnetScriptableComponent should not accept a nullable ScriptingSystem pointer in its constructor.");
        static_assert(!HasLoadSceneWithoutScriptingSystem<ve::Scene>, "SceneSerialization should require a ScriptingSystem for scene loading.");
        return true;
    }

    bool TestDotnetScriptableComponentSceneIntegration()
    {
        bool passed = true;

        ve::ScriptingSystem scriptingSystem;
        passed &= Expect(scriptingSystem.Initialize(ve::ScriptingSystemInitParam{}) == ve::ErrorCode::None, "ScriptingSystem should initialize for script components");

        ve::Scene sourceScene("Scripting Smoke");
        ve::Result<ve::GameObject*> rootResult = sourceScene.CreateRootGameObject("Script Host");
        passed &= Expect(rootResult.IsOk(), "Script host GameObject should be created");
        ve::GameObject* root = rootResult.GetValue();

        ve::Result<ve::DotnetScriptableComponent*> scriptResult =
            root->AddComponentWithoutRenderRegistration<ve::DotnetScriptableComponent>("Game.PlayerController", scriptingSystem);
        passed &= Expect(scriptResult.IsOk(), "DotnetScriptableComponent should be added");
        ve::DotnetScriptableComponent* script = scriptResult.GetValue();
        passed &= Expect(script->GetScriptTypeName() == "Game.PlayerController", "Script type name should be stored");
        passed &= Expect(root->GetComponentCount() == 2, "Root should expose TransformComponent and DotnetScriptableComponent");
        passed &= Expect(root->GetComponent<ve::DotnetScriptableComponent>() == script, "DotnetScriptableComponent should be retrievable by concrete type");
        passed &= Expect(root->GetComponent<ve::ScriptableComponent>() == script, "DotnetScriptableComponent should be retrievable through ScriptableComponent");

        root->Update(1.0f / 60.0f);
        root->LateUpdate(1.0f / 60.0f);
        script->SetEnabled(false);
        script->SetEnabled(true);
        passed &= Expect(!script->HasScriptInstance(), "DotnetScriptableComponent without a loaded assembly should not create a managed instance");

        ve::Result<std::string> serialized = ve::SceneSerialization::SaveToString(sourceScene);
        passed &= Expect(serialized.IsOk(), "Scene with DotnetScriptableComponent should serialize");
        passed &= Expect(serialized.GetValue().find("\"type\": \"DotnetScriptableComponent\"") != std::string::npos,
                         "Serialized scene should include DotnetScriptableComponent");
        passed &= Expect(serialized.GetValue().find("\"scriptTypeName\": \"Game.PlayerController\"") != std::string::npos,
                         "Serialized scene should include script type name");

        ve::Scene loadedScene;
        passed &= Expect(ve::SceneSerialization::LoadFromString(loadedScene, serialized.GetValue(), scriptingSystem) == ve::ErrorCode::None,
                         "Scene with DotnetScriptableComponent should deserialize");
        ve::GameObject* loadedRoot = loadedScene.GetRootGameObject(0);
        passed &= Expect(loadedRoot != nullptr, "Loaded root should exist");
        ve::DotnetScriptableComponent* loadedScript = loadedRoot != nullptr ? loadedRoot->GetComponent<ve::DotnetScriptableComponent>() : nullptr;
        passed &= Expect(loadedScript != nullptr, "Loaded root should have DotnetScriptableComponent");
        passed &= Expect(loadedScript != nullptr && loadedScript->GetScriptTypeName() == "Game.PlayerController", "Script type name should round-trip");

        passed &= Expect(root->RemoveComponent<ve::DotnetScriptableComponent>(), "DotnetScriptableComponent should be removable");
        passed &= Expect(root->GetComponent<ve::DotnetScriptableComponent>() == nullptr, "Removed DotnetScriptableComponent should not be retrievable");
        passed &= Expect(root->GetComponent<ve::ScriptableComponent>() == nullptr, "Removed DotnetScriptableComponent should not be retrievable through ScriptableComponent");
        scriptingSystem.Shutdown();
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestRequiredScriptingSystem();
    passed &= TestScriptingInterfaces();
    passed &= TestDotnetScriptableComponentSceneIntegration();

    if (passed)
    {
        std::cout << "VEngineScriptingTests passed" << '\n';
        return 0;
    }

    return 1;
}
