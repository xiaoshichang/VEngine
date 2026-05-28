#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Serialization/SceneSerialization.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/ScriptComponent.h"
#include "Engine/Runtime/Scripting/ScriptContext.h"
#include "Engine/Runtime/Scripting/ScriptHost.h"
#include "Engine/Runtime/Scripting/ScriptProject.h"
#include "Engine/Runtime/Time/Time.h"

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifndef VE_TEST_SOURCE_DIR
#define VE_TEST_SOURCE_DIR "."
#endif

namespace
{
    std::vector<std::string> gScriptLogs;

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    template<typename T>
    bool ExpectOk(const ve::Result<T>& result, const char* message)
    {
        if (result)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result.GetError().GetCode());
        if (!result.GetError().GetMessage().empty())
        {
            std::cerr << ": " << result.GetError().GetMessage();
        }
        std::cerr << '\n';
        return false;
    }

    [[nodiscard]] bool ContainsLog(std::string_view text)
    {
        for (const std::string& message : gScriptLogs)
        {
            if (message.find(text) != std::string::npos)
            {
                return true;
            }
        }

        return false;
    }

    void CaptureLog(const ve::LogRecord& record)
    {
        if (std::string_view(record.category) == "Script")
        {
            gScriptLogs.emplace_back(record.message);
        }
    }

    [[nodiscard]] ve::Path GetSourceRoot()
    {
        return ve::Path(VE_TEST_SOURCE_DIR);
    }

    [[nodiscard]] ve::Result<ve::WindowsScriptBuildArtifacts> BuildSmokeScripts()
    {
        ve::WindowsScriptProjectConfig config;
        config.projectPath = ve::Path("Tests/Scripting/SmokeScripts/VEngine.SmokeScripts.csproj");
        config.assemblyName = "VEngine.SmokeScripts";
        return ve::BuildWindowsScriptProject(GetSourceRoot(), config, ve::ScriptBuildConfiguration::Debug);
    }

    [[nodiscard]] bool InitializeHost(ve::ScriptHost& host, const ve::WindowsScriptBuildArtifacts& artifacts)
    {
        ve::Result<ve::ScriptHostInfo> hostInfo =
            host.Initialize(ve::ScriptHostDesc{artifacts.projectRuntimeConfigPath, artifacts.scriptApiAssemblyPath});
        return ExpectOk(hostInfo, "ScriptHost should initialize");
    }

    bool TestHostMissingRuntimeConfigDiagnostics()
    {
        ve::ScriptHost host;
        ve::Result<ve::ScriptHostInfo> result =
            host.Initialize(ve::ScriptHostDesc{ve::Path("Generated/Missing.runtimeconfig.json"),
                                               ve::Path("Generated/Missing.dll")});
        return Expect(!result, "Missing ScriptHost runtimeconfig should fail") &&
               Expect(result.GetError().GetCode() == ve::ErrorCode::NotFound,
                      "Missing ScriptHost runtimeconfig should report NotFound") &&
               Expect(result.GetError().GetMessage().find("runtimeconfig") != std::string::npos,
                      "Missing ScriptHost runtimeconfig diagnostic should name runtimeconfig");
    }

    bool TestHostMissingScriptApiDiagnostics()
    {
        const ve::Path runtimeConfigPath =
            ve::FileSystem::GetCurrentWorkingDirectory() / "Generated/ScriptingTests/MissingApi.runtimeconfig.json";
        const ve::ErrorCode writeResult = ve::FileSystem::WriteTextFile(runtimeConfigPath, "{}\n");
        if (!Expect(writeResult == ve::ErrorCode::None, "Runtimeconfig placeholder should be written"))
        {
            return false;
        }

        ve::ScriptHost host;
        ve::Result<ve::ScriptHostInfo> result =
            host.Initialize(ve::ScriptHostDesc{runtimeConfigPath,
                                               ve::Path("Generated/ScriptingTests/Missing/VEngine.ScriptAPI.dll")});
        return Expect(!result, "Missing ScriptAPI assembly should fail") &&
               Expect(result.GetError().GetCode() == ve::ErrorCode::NotFound,
                      "Missing ScriptAPI assembly should report NotFound") &&
               Expect(result.GetError().GetMessage().find("VEngine.ScriptAPI assembly") != std::string::npos,
                      "Missing ScriptAPI diagnostic should name VEngine.ScriptAPI assembly");
    }

    bool TestScriptComponentSerialization()
    {
        gScriptLogs.clear();

        ve::ReflectionRegistry reflectionRegistry;
        ve::RegisterSceneReflectionTypes(reflectionRegistry);

        ve::Scene sourceScene;
        ve::GameObject& gameObject = sourceScene.CreateGameObject("Scripted");
        ve::ScriptComponent& script = gameObject.AddComponent<ve::ScriptComponent>();
        script.SetScriptTypeName("VEngine.SmokeScripts.LifecycleProbe");
        script.SetAssemblyName("VEngine.SmokeScripts");

        const std::string json = ve::SerializeSceneToJson(sourceScene, reflectionRegistry);

        ve::Scene loadedScene;
        bool passed = Expect(ve::DeserializeSceneFromJson(loadedScene, reflectionRegistry, json) == ve::ErrorCode::None,
                             "ScriptComponent scene JSON should deserialize");
        ve::GameObject* loadedObject = loadedScene.FindGameObject(gameObject.GetId());
        ve::ScriptComponent* loadedScript =
            loadedObject != nullptr ? loadedObject->GetComponent<ve::ScriptComponent>() : nullptr;
        passed &= Expect(loadedScript != nullptr, "Loaded scene should contain ScriptComponent");
        passed &= Expect(loadedScript != nullptr &&
                             loadedScript->GetScriptTypeName() == "VEngine.SmokeScripts.LifecycleProbe",
                         "ScriptComponent should serialize scriptTypeName");
        passed &= Expect(loadedScript != nullptr && loadedScript->GetAssemblyName() == "VEngine.SmokeScripts",
                         "ScriptComponent should serialize assemblyName");
        passed &= Expect(!ContainsLog("Scene has no ScriptContext"),
                         "Edit-time ScriptComponent serialization should not require ScriptContext");
        passed &= Expect(loadedScript != nullptr && loadedScript->GetLastError().empty(),
                         "Loaded edit-time ScriptComponent should not enter failed state without ScriptContext");
        return passed;
    }

    bool TestManagedLifecycleBridgeAndTransform(const ve::WindowsScriptBuildArtifacts& artifacts)
    {
        gScriptLogs.clear();

        ve::ScriptHost host;
        if (!InitializeHost(host, artifacts))
        {
            return false;
        }

        ve::ScriptContext context(host);
        ve::Result<ve::ScriptOperationResult> loadResult = context.LoadProjectAssembly(artifacts.projectAssemblyPath);
        if (!ExpectOk(loadResult, "Project script assembly should load"))
        {
            return false;
        }

        ve::SceneDesc sceneDesc;
        sceneDesc.scriptContext = &context;
        ve::Scene scene(sceneDesc);
        ve::GameObject& gameObject = scene.CreateGameObject("LifecycleObject");
        ve::TransformComponent& transform = gameObject.AddComponent<ve::TransformComponent>();
        ve::ScriptComponent& script = gameObject.AddComponent<ve::ScriptComponent>();
        script.SetScriptTypeName("VEngine.SmokeScripts.LifecycleProbe");

        ve::Time::Reset();
        ve::Time::Advance(0.125f);
        scene.Update();

        bool passed = true;
        passed &= Expect(script.IsScriptValid(), "Lifecycle script should remain valid");
        passed &= Expect(transform.GetLocalPosition().GetX() > 0.12f,
                         "Managed script should mutate Transform local position");
        passed &= Expect(ContainsLog("LifecycleProbe.OnCreate:LifecycleObject"),
                         "Managed OnCreate should log through bridge");
        passed &= Expect(ContainsLog("LifecycleProbe.OnEnable"), "Managed OnEnable should run");
        passed &= Expect(ContainsLog("LifecycleProbe.OnUpdate:1:0.125"), "Managed OnUpdate should read Time");

        scene.Clear();
        passed &= Expect(ContainsLog("LifecycleProbe.OnDisable"), "Managed OnDisable should run");
        passed &= Expect(ContainsLog("LifecycleProbe.OnDestroy"), "Managed OnDestroy should run");
        return passed;
    }

    bool TestManagedExceptionDisablesScript(const ve::WindowsScriptBuildArtifacts& artifacts)
    {
        ve::ScriptHost host;
        if (!InitializeHost(host, artifacts))
        {
            return false;
        }

        ve::ScriptContext context(host);
        ve::Result<ve::ScriptOperationResult> loadResult = context.LoadProjectAssembly(artifacts.projectAssemblyPath);
        if (!ExpectOk(loadResult, "Project script assembly should load for exception test"))
        {
            return false;
        }

        ve::SceneDesc sceneDesc;
        sceneDesc.scriptContext = &context;
        ve::Scene scene(sceneDesc);
        ve::GameObject& gameObject = scene.CreateGameObject("ThrowingObject");
        gameObject.AddComponent<ve::TransformComponent>();
        ve::ScriptComponent& script = gameObject.AddComponent<ve::ScriptComponent>();
        script.SetScriptTypeName("VEngine.SmokeScripts.ThrowingProbe");

        ve::Time::Reset();
        ve::Time::Advance(0.016f);
        scene.Update();

        bool passed = true;
        passed &= Expect(!script.IsScriptValid(), "Managed exception should mark ScriptComponent invalid");
        passed &= Expect(script.GetLastError().find("ThrowingProbe update failure") != std::string::npos,
                         "Managed exception diagnostic should include managed message");
        return passed;
    }

    bool TestScriptContextReloadAfterStop(const ve::WindowsScriptBuildArtifacts& artifacts)
    {
        bool passed = true;
        for (int pass = 0; pass < 2; ++pass)
        {
            ve::ScriptHost host;
            passed &= InitializeHost(host, artifacts);
            if (!passed)
            {
                return false;
            }

            {
                ve::ScriptContext context(host);
                ve::Result<ve::ScriptOperationResult> loadResult =
                    context.LoadProjectAssembly(artifacts.projectAssemblyPath);
                passed &= ExpectOk(loadResult, "Project script assembly should load after previous teardown");

                ve::SceneDesc sceneDesc;
                sceneDesc.scriptContext = &context;
                ve::Scene scene(sceneDesc);
                ve::GameObject& gameObject = scene.CreateGameObject("ReloadObject");
                gameObject.AddComponent<ve::TransformComponent>();
                ve::ScriptComponent& script = gameObject.AddComponent<ve::ScriptComponent>();
                script.SetScriptTypeName("VEngine.SmokeScripts.LifecycleProbe");
                passed &= Expect(script.IsScriptValid(), "Reloaded script should create a valid instance");
                scene.Clear();
            }
        }

        return passed;
    }
} // namespace

int main()
{
    ve::LoggingConfig loggingConfig = ve::MakeDefaultLoggingConfig();
    loggingConfig.enableConsole = false;
    loggingConfig.enableFile = false;
    (void)ve::InitializeLogging(loggingConfig);
    ve::SetLogCallback(&CaptureLog);

    bool passed = true;
    passed &= TestHostMissingRuntimeConfigDiagnostics();
    passed &= TestHostMissingScriptApiDiagnostics();

    ve::Result<ve::WindowsScriptBuildArtifacts> artifacts = BuildSmokeScripts();
    passed &= ExpectOk(artifacts, "Smoke scripts should build");
    if (artifacts)
    {
        passed &= TestScriptComponentSerialization();
        passed &= TestManagedLifecycleBridgeAndTransform(artifacts.GetValue());
        passed &= TestManagedExceptionDisablesScript(artifacts.GetValue());
        passed &= TestScriptContextReloadAfterStop(artifacts.GetValue());
    }

    ve::SetLogCallback(nullptr);
    ve::ShutdownLogging();

    if (!passed)
    {
        return 1;
    }

    std::cout << "VEngineScriptingTests passed" << '\n';
    return 0;
}
