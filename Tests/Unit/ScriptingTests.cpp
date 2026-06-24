#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneSerialization.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"

#include <iostream>
#include <string>

namespace
{
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

    bool TestScriptableComponentSceneIntegration()
    {
        bool passed = true;

        ve::Scene sourceScene("Scripting Smoke");
        ve::Result<ve::GameObject*> rootResult = sourceScene.CreateRootGameObject("Script Host");
        passed &= Expect(rootResult.IsOk(), "Script host GameObject should be created");
        ve::GameObject* root = rootResult.GetValue();

        ve::Result<ve::ScriptableComponent*> scriptResult = root->AddComponentWithoutRenderRegistration<ve::ScriptableComponent>("Game.PlayerController");
        passed &= Expect(scriptResult.IsOk(), "ScriptableComponent should be added");
        ve::ScriptableComponent* script = scriptResult.GetValue();
        passed &= Expect(script->GetScriptTypeName() == "Game.PlayerController", "Script type name should be stored");
        passed &= Expect(root->GetComponentCount() == 2, "Root should expose TransformComponent and ScriptableComponent");
        passed &= Expect(root->GetComponent<ve::ScriptableComponent>() == script, "ScriptableComponent should be retrievable by type");

        root->Update(1.0f / 60.0f);
        root->LateUpdate(1.0f / 60.0f);
        script->SetEnabled(false);
        script->SetEnabled(true);
        passed &= Expect(!script->HasScriptInstance(), "ScriptableComponent without a ScriptingSystem should not create a managed instance");

        ve::Result<std::string> serialized = ve::SceneSerialization::SaveToString(sourceScene);
        passed &= Expect(serialized.IsOk(), "Scene with ScriptableComponent should serialize");
        passed &= Expect(serialized.GetValue().find("\"type\": \"ScriptableComponent\"") != std::string::npos,
                         "Serialized scene should include ScriptableComponent");
        passed &= Expect(serialized.GetValue().find("\"scriptTypeName\": \"Game.PlayerController\"") != std::string::npos,
                         "Serialized scene should include script type name");

        ve::Scene loadedScene;
        passed &= Expect(ve::SceneSerialization::LoadFromString(loadedScene, serialized.GetValue()) == ve::ErrorCode::None,
                         "Scene with ScriptableComponent should deserialize");
        ve::GameObject* loadedRoot = loadedScene.GetRootGameObject(0);
        passed &= Expect(loadedRoot != nullptr, "Loaded root should exist");
        ve::ScriptableComponent* loadedScript = loadedRoot != nullptr ? loadedRoot->GetComponent<ve::ScriptableComponent>() : nullptr;
        passed &= Expect(loadedScript != nullptr, "Loaded root should have ScriptableComponent");
        passed &= Expect(loadedScript != nullptr && loadedScript->GetScriptTypeName() == "Game.PlayerController", "Script type name should round-trip");

        passed &= Expect(root->RemoveComponent<ve::ScriptableComponent>(), "ScriptableComponent should be removable");
        passed &= Expect(root->GetComponent<ve::ScriptableComponent>() == nullptr, "Removed ScriptableComponent should not be retrievable");
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestRequiredScriptingSystem();
    passed &= TestScriptableComponentSceneIntegration();

    if (passed)
    {
        std::cout << "VEngineScriptingTests passed" << '\n';
        return 0;
    }

    return 1;
}
