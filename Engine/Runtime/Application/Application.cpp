#include "Engine/Runtime/Application/Application.h"

#include "Engine/Runtime/Asset/AssetDatabase.h"
#include "Engine/Runtime/Asset/SceneAssetLoader.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Scene/RenderComponents.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/ScriptContext.h"
#include "Engine/Runtime/Scripting/ScriptHost.h"
#include "Engine/Runtime/Scripting/ScriptProject.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>

#ifndef VE_DEFAULT_PROJECT_DIR
#define VE_DEFAULT_PROJECT_DIR ""
#endif

#ifndef VE_BUILD_CONFIGURATION
#define VE_BUILD_CONFIGURATION "Debug"
#endif

namespace ve
{
    namespace
    {
        using boost::json::object;
        using boost::json::value;

        constexpr std::string_view ProjectDescriptorFileName = ".veproject";
        constexpr std::string_view DefaultSampleScenePath = "Assets/Samples/Scenes/AssetPipelineSample.vescene";

        void ShutdownEngineRuntime(EngineRuntime& runtime)
        {
            runtime.Shutdown();
        }

        [[nodiscard]] const value* FindMember(const object& jsonObject, const char* name)
        {
            const auto iter = jsonObject.find(name);
            return iter == jsonObject.end() ? nullptr : &iter->value();
        }

        [[nodiscard]] std::string ReadString(const object& jsonObject, const char* name)
        {
            const value* member = FindMember(jsonObject, name);
            return member != nullptr && member->is_string() ? std::string(member->as_string()) : std::string();
        }

        [[nodiscard]] Path ReadStartupScenePathFromProjectDescriptor(const Path& projectRoot)
        {
            Result<std::string> textResult = FileSystem::ReadTextFile(projectRoot / ProjectDescriptorFileName);
            if (!textResult)
            {
                return {};
            }

            boost::system::error_code parseError;
            value rootValue = boost::json::parse(textResult.GetValue(), parseError);
            if (parseError || !rootValue.is_object())
            {
                return {};
            }

            const object& root = rootValue.as_object();
            if (ReadString(root, "format") != "VEngine.Project")
            {
                return {};
            }

            const value* startupSceneValue = FindMember(root, "startupScene");
            if (startupSceneValue == nullptr || !startupSceneValue->is_object())
            {
                return {};
            }

            return Path(ReadString(startupSceneValue->as_object(), "path"));
        }

        [[nodiscard]] Path ResolveStartupScenePath(const Path& projectRoot, const Path& overrideScenePath)
        {
            if (!overrideScenePath.IsEmpty())
            {
                return overrideScenePath;
            }

            const Path descriptorScenePath = ReadStartupScenePathFromProjectDescriptor(projectRoot);
            if (!descriptorScenePath.IsEmpty())
            {
                return descriptorScenePath;
            }

            return Path(DefaultSampleScenePath);
        }

        [[nodiscard]] ScriptBuildConfiguration GetApplicationScriptConfiguration()
        {
            Result<ScriptBuildConfiguration> configuration = ParseScriptBuildConfiguration(VE_BUILD_CONFIGURATION);
            return configuration ? configuration.GetValue() : ScriptBuildConfiguration::Debug;
        }
    } // namespace

    Application::Application(std::string name)
        : desc_()
    {
        desc_.name = std::move(name);

        if (desc_.name.empty())
        {
            desc_.name = "VEngine";
        }

        desc_.mainWindow.title = desc_.name;
    }

    Application::Application(ApplicationDesc desc)
        : desc_(std::move(desc))
    {
        if (desc_.name.empty())
        {
            desc_.name = "VEngine";
        }

        if (desc_.mainWindow.title.empty())
        {
            desc_.mainWindow.title = desc_.name;
        }
    }

    Application::~Application() = default;

    int Application::Run()
    {
        exitCode_ = RunApplication();
        return exitCode_;
    }

    ErrorCode Application::InitializeEngineRuntime()
    {
        ErrorCode runtimeResult = engineRuntime_.Initialize(desc_.runtime);

        if (runtimeResult != ErrorCode::None)
        {
            VE_LOG_ERROR("Failed to initialize engine runtime: {}", ToString(runtimeResult));
        }

        return runtimeResult;
    }

    Result<std::unique_ptr<Window>> Application::CreateMainWindow()
    {
        Result<std::unique_ptr<Window>> windowResult = Window::Create(desc_.mainWindow);

        if (!windowResult)
        {
            VE_LOG_ERROR("Failed to create main window: {}", windowResult.GetError().GetMessage());
        }

        return windowResult;
    }

    ErrorCode Application::InitializeRendering(Window& mainWindow)
    {
        RenderSystem& renderSystem = engineRuntime_.GetRenderSystem();

        ErrorCode deviceResult = renderSystem.InitializeDevice(desc_.runtime.renderSystem.device);
        if (deviceResult != ErrorCode::None)
        {
            VE_LOG_ERROR("Failed to initialize render device: {}", ToString(deviceResult));
            return deviceResult;
        }

        const WindowExtent extent = mainWindow.GetClientExtent();
        RenderSurfaceDesc surfaceDesc;
        surfaceDesc.nativeWindow = mainWindow.GetNativeHandle();
        surfaceDesc.nativeLayer = mainWindow.GetNativeLayer();
        surfaceDesc.width = extent.width;
        surfaceDesc.height = extent.height;

        ErrorCode swapchainResult = renderSystem.CreateMainSwapchain(surfaceDesc);
        if (swapchainResult != ErrorCode::None)
        {
            VE_LOG_ERROR("Failed to create main render swapchain: {}", ToString(swapchainResult));
            renderSystem.ShutdownDevice();
            return swapchainResult;
        }

        GameThreadSystem& gameThreadSystem = engineRuntime_.GetGameThreadSystem();
        ErrorCode renderConnectionResult = gameThreadSystem.SetRenderSystem(&renderSystem);
        if (renderConnectionResult != ErrorCode::None)
        {
            VE_LOG_ERROR("Failed to connect Game Thread to RenderSystem: {}", ToString(renderConnectionResult));
            renderSystem.DestroyMainSwapchain();
            renderSystem.ShutdownDevice();
            return renderConnectionResult;
        }

        return ErrorCode::None;
    }

    void Application::ShutdownRendering() noexcept
    {
        GameThreadSystem& gameThreadSystem = engineRuntime_.GetGameThreadSystem();
        gameThreadSystem.ClearActiveScene();
        gameThreadSystem.ClearRenderSystem();

        RenderSystem& renderSystem = engineRuntime_.GetRenderSystem();
        renderSystem.DestroyMainSwapchain();
        renderSystem.ShutdownDevice();
    }

    ErrorCode Application::CreateInitialScene()
    {
        if (desc_.sceneStartup)
        {
            return desc_.sceneStartup(engineRuntime_);
        }

        return CreateSampleScene();
    }

    void Application::DestroyInitialScene() noexcept
    {
        if (desc_.sceneShutdown)
        {
            desc_.sceneShutdown(engineRuntime_);
            return;
        }

        DestroySampleScene();
    }

    ErrorCode Application::CreateSampleScene()
    {
        ResourceManager& resourceManager = engineRuntime_.GetResourceManager();
        sampleScene_ = std::make_unique<Scene>();

        Path projectRoot = desc_.projectRoot;
        if (projectRoot.IsEmpty() && std::string_view(VE_DEFAULT_PROJECT_DIR).size() > 0)
        {
            projectRoot = Path(VE_DEFAULT_PROJECT_DIR);
        }

        if (!projectRoot.IsEmpty())
        {
            FileSystem::SetProjectRoot(projectRoot);

            AssetDatabase assetDatabase;
            const ErrorCode assetDatabaseResult = assetDatabase.Open(projectRoot);
            if (assetDatabaseResult == ErrorCode::None)
            {
                const Path startupScenePath = ResolveStartupScenePath(projectRoot, desc_.sampleScenePath);

                ReflectionRegistry reflectionRegistry;
                RegisterSceneReflectionTypes(reflectionRegistry);

                const Path scenePath = assetDatabase.ResolveProjectPath(startupScenePath);

                Result<ScriptProjectConfig> scriptConfig = LoadScriptProjectConfig(projectRoot);
                if (scriptConfig && scriptConfig.GetValue().HasWindowsScripts())
                {
                    const WindowsScriptProjectConfig& windowsScripts = scriptConfig.GetValue().windows;
                    const ScriptBuildConfiguration scriptConfiguration = GetApplicationScriptConfiguration();
                    WindowsScriptBuildArtifacts artifacts =
                        GetWindowsPackagedScriptBuildArtifacts(projectRoot, windowsScripts.assemblyName);
                    if (ValidateWindowsScriptBuildArtifacts(artifacts) != ErrorCode::None)
                    {
                        artifacts = GetWindowsGeneratedScriptBuildArtifacts(projectRoot,
                                                                            scriptConfiguration,
                                                                            windowsScripts.assemblyName);
                    }

                    const ErrorCode scriptArtifactsResult = ValidateWindowsScriptBuildArtifacts(artifacts);
                    if (scriptArtifactsResult != ErrorCode::None)
                    {
                        VE_LOG_ERROR_CATEGORY("Script",
                                              "Configured Windows scripts are missing required output files for '{}'.",
                                              windowsScripts.assemblyName);
                        return scriptArtifactsResult;
                    }

                    scriptHost_ = std::make_unique<ScriptHost>();
                    Result<ScriptHostInfo> hostInfo =
                        scriptHost_->Initialize(ScriptHostDesc{artifacts.projectRuntimeConfigPath,
                                                               artifacts.scriptApiAssemblyPath});
                    if (!hostInfo)
                    {
                        VE_LOG_ERROR_CATEGORY("Script",
                                              "Failed to initialize ScriptHost: {}",
                                              hostInfo.GetError().GetMessage());
                        scriptHost_.reset();
                        return hostInfo.GetError().GetCode();
                    }

                    scriptContext_ = std::make_unique<ScriptContext>(*scriptHost_);
                    Result<ScriptOperationResult> loadScripts =
                        scriptContext_->LoadProjectAssembly(artifacts.projectAssemblyPath);
                    if (!loadScripts)
                    {
                        VE_LOG_ERROR_CATEGORY("Script",
                                              "Failed to load project script assembly: {}",
                                              loadScripts.GetError().GetMessage());
                        scriptContext_.reset();
                        scriptHost_.reset();
                        return loadScripts.GetError().GetCode();
                    }

                    sampleScene_->SetScriptContext(scriptContext_.get());
                    VE_LOG_INFO_CATEGORY("Script", "Loaded Windows script assembly '{}'.", windowsScripts.assemblyName);
                }
                else if (!scriptConfig)
                {
                    VE_LOG_WARN_CATEGORY("Script",
                                         "Project scripting configuration could not be read: {}",
                                         scriptConfig.GetError().GetMessage());
                }

                const ErrorCode sceneLoadResult =
                    LoadSceneAsset(*sampleScene_, reflectionRegistry, resourceManager, assetDatabase, scenePath);
                if (sceneLoadResult == ErrorCode::None)
                {
                    sampleScene_->UpdateTransforms();
                    ErrorCode bindResult =
                        engineRuntime_.GetGameThreadSystem().SetActiveScene(sampleScene_.get(), &resourceManager);
                    if (bindResult != ErrorCode::None)
                    {
                        VE_LOG_ERROR("Failed to bind asset sample scene to GameThreadSystem: {}", ToString(bindResult));
                        return bindResult;
                    }

                    return ErrorCode::None;
                }

                VE_LOG_WARN("Failed to load asset sample scene '{}': {}. Falling back to code sample.",
                            scenePath.GetString(),
                            ToString(sceneLoadResult));
            }
            else
            {
                VE_LOG_WARN("Failed to open AssetDatabase at '{}': {}. Falling back to code sample.",
                            projectRoot.GetString(),
                            ToString(assetDatabaseResult));
            }
        }

        GameObject& camera = sampleScene_->CreateGameObject("SampleCamera");
        TransformComponent& cameraTransform = camera.AddComponent<TransformComponent>();
        cameraTransform.SetLocalPosition(Vector3(2.0f, 1.6f, -3.5f));
        cameraTransform.SetLocalRotation(Quaternion::FromEulerXYZ(ToRadians(20.0f), ToRadians(-30.0f), 0.0f));
        camera.AddComponent<CameraComponent>();

        GameObject& light = sampleScene_->CreateGameObject("SampleDirectionalLight");
        light.AddComponent<TransformComponent>().SetLocalRotation(
            Quaternion::FromEulerXYZ(ToRadians(-35.0f), ToRadians(20.0f), 0.0f));
        LightComponent& lightComponent = light.AddComponent<LightComponent>();
        lightComponent.SetIntensity(1.25f);

        GameObject& cube = sampleScene_->CreateGameObject("SampleCube");
        cube.AddComponent<TransformComponent>();
        MeshRendererComponent& renderer = cube.AddComponent<MeshRendererComponent>();
        renderer.SetMesh(resourceManager.GetFallbackMesh());
        renderer.SetMaterial(resourceManager.GetDefaultMaterial());

        sampleScene_->UpdateTransforms();
        ErrorCode sceneResult =
            engineRuntime_.GetGameThreadSystem().SetActiveScene(sampleScene_.get(), &resourceManager);
        if (sceneResult != ErrorCode::None)
        {
            VE_LOG_ERROR("Failed to bind sample scene to GameThreadSystem: {}", ToString(sceneResult));
            return sceneResult;
        }

        return ErrorCode::None;
    }

    void Application::DestroySampleScene() noexcept
    {
        if (engineRuntime_.IsInitialized())
        {
            engineRuntime_.GetGameThreadSystem().ClearActiveScene();
        }

        sampleScene_.reset();
        scriptContext_.reset();
        scriptHost_.reset();
    }

    int Application::RunMainLoop(Window& mainWindow)
    {
        int exitCode = 0;

        while (!mainWindow.ShouldClose())
        {
            mainWindow.PumpCommands();

            const WindowPumpStatus pumpStatus = mainWindow.PumpEvents();
            if (pumpStatus.result == WindowPumpResult::Quit)
            {
                exitCode = pumpStatus.exitCode;
                break;
            }

            if (desc_.frameUpdate)
            {
                desc_.frameUpdate(mainWindow, engineRuntime_);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return exitCode;
    }

    int Application::RunApplication()
    {
        VE_LOG_INFO("{} starting", desc_.name);

        auto cleanup = [this](std::unique_ptr<Window>& window)
        {
            if (window != nullptr)
            {
                window->PumpCommands();
                window->SetCommandHandler({});
            }

            window.reset();
            ShutdownEngineRuntime(engineRuntime_);
        };

        ErrorCode runtimeResult = InitializeEngineRuntime();
        if (runtimeResult != ErrorCode::None)
        {
            std::unique_ptr<Window> emptyWindow;
            cleanup(emptyWindow);
            return 1;
        }

        Result<std::unique_ptr<Window>> windowResult = CreateMainWindow();
        if (!windowResult)
        {
            std::unique_ptr<Window> emptyWindow;
            cleanup(emptyWindow);
            return 1;
        }

        std::unique_ptr<Window> mainWindow = windowResult.MoveValue();
        if (desc_.initializeRenderingOnStartup)
        {
            ErrorCode renderResult = InitializeRendering(*mainWindow);
            if (renderResult != ErrorCode::None)
            {
                cleanup(mainWindow);
                return 1;
            }
        }

        if (desc_.windowConfigure)
        {
            desc_.windowConfigure(*mainWindow, engineRuntime_);
        }

        mainWindow->SetCommandHandler(
            [this, window = mainWindow.get()](std::string_view command)
            {
                if (desc_.commandHandler)
                {
                    desc_.commandHandler(command, *window, engineRuntime_);
                    return;
                }

                VE_LOG_INFO_CATEGORY("GM", "Unhandled GM command: {}", command);
            });

        ErrorCode sceneResult = CreateInitialScene();
        if (sceneResult != ErrorCode::None)
        {
            ShutdownRendering();
            cleanup(mainWindow);
            return 1;
        }

        const int result = RunMainLoop(*mainWindow);
        DestroyInitialScene();
        ShutdownRendering();
        cleanup(mainWindow);

        VE_LOG_INFO("{} stopped with exit code {}", desc_.name, result);
        return result;
    }

    const std::string& Application::GetName() const noexcept
    {
        return desc_.name;
    }

    int Application::GetExitCode() const noexcept
    {
        return exitCode_;
    }

    EngineRuntime& Application::GetRuntime() noexcept
    {
        return engineRuntime_;
    }

    const EngineRuntime& Application::GetRuntime() const noexcept
    {
        return engineRuntime_;
    }
} // namespace ve
