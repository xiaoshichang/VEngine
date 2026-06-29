#include "Player/Windows/VEnginePlayer.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scene/SceneSystem.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <boost/json.hpp>
#include <utility>

namespace ve
{
    namespace
    {
        constexpr const char* PackageDataDirectoryName = "Data";
        constexpr const char* PackageManifestFilename = "AssetManifest.json";
        constexpr const char* ProjectDescriptorFilename = "VEProject.json";
        constexpr const char* ScriptAssemblyManifestPath = "Scripts/ScriptAssembly.json";
        constexpr const char* DefaultStartScene = "Assets/Scenes/SampleScene.vescene";

        struct PackagedProjectDescriptor
        {
            std::string name;
            std::string startScene = DefaultStartScene;
        };

        [[nodiscard]] std::string ReadString(const boost::json::object& object, boost::json::string_view key, std::string fallback = {})
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }

        [[nodiscard]] Path GetDefaultPackagedDataRoot()
        {
            const Path executableDirectory = FileSystem::GetExecutableDirectory();
            if (executableDirectory.IsEmpty())
            {
                return Path();
            }

            return executableDirectory.GetParentPath() / PackageDataDirectoryName;
        }

        [[nodiscard]] bool IsPackagedDataRoot(const Path& path)
        {
            return !path.IsEmpty() && FileSystem::IsFile(path / PackageManifestFilename) && FileSystem::IsFile(path / ProjectDescriptorFilename);
        }

        [[nodiscard]] Path FindPackagedDataRoot()
        {
            const Path configuredProjectRoot = FileSystem::GetProjectRoot();
            if (IsPackagedDataRoot(configuredProjectRoot))
            {
                return configuredProjectRoot;
            }

            const Path defaultPackagedDataRoot = GetDefaultPackagedDataRoot();
            if (IsPackagedDataRoot(defaultPackagedDataRoot))
            {
                return defaultPackagedDataRoot;
            }

            return Path();
        }

        [[nodiscard]] Result<PackagedProjectDescriptor> LoadPackagedProjectDescriptor(const Path& dataRoot)
        {
            Result<std::string> text = FileSystem::ReadTextFile(dataRoot / ProjectDescriptorFilename);
            if (!text)
            {
                return Result<PackagedProjectDescriptor>::Failure(text.GetError());
            }

            Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
            if (!json)
            {
                return Result<PackagedProjectDescriptor>::Failure(json.GetError());
            }

            if (!json.GetValue().is_object())
            {
                return Result<PackagedProjectDescriptor>::Failure(Error(ErrorCode::InvalidArgument, "Project descriptor root must be a JSON object."));
            }

            const boost::json::object& object = json.GetValue().as_object();
            PackagedProjectDescriptor descriptor;
            descriptor.name = ReadString(object, "name");
            descriptor.startScene = ReadString(object, "startScene", descriptor.startScene);
            return Result<PackagedProjectDescriptor>::Success(std::move(descriptor));
        }

        [[nodiscard]] Result<Path> LoadPackagedScriptAssemblyPath(const Path& dataRoot)
        {
            const Path manifestPath = dataRoot / ScriptAssemblyManifestPath;
            if (!FileSystem::IsFile(manifestPath))
            {
                return Result<Path>::Failure(Error(ErrorCode::NotFound, "Packaged script manifest was not found."));
            }

            Result<std::string> text = FileSystem::ReadTextFile(manifestPath);
            if (!text)
            {
                return Result<Path>::Failure(text.GetError());
            }

            Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
            if (!json)
            {
                return Result<Path>::Failure(json.GetError());
            }

            if (!json.GetValue().is_object())
            {
                return Result<Path>::Failure(Error(ErrorCode::InvalidArgument, "Script assembly manifest root must be a JSON object."));
            }

            const std::string assemblyPath = ReadString(json.GetValue().as_object(), "assemblyPath");
            if (assemblyPath.empty())
            {
                return Result<Path>::Failure(Error(ErrorCode::InvalidArgument, "Script assembly manifest is missing assemblyPath."));
            }

            return Result<Path>::Success(dataRoot / assemblyPath);
        }
    } // namespace

    VEnginePlayer::VEnginePlayer(ApplicationInitParam initParam)
        : Application(std::move(initParam))
        , viewportClient_("PlayerViewport")
    {
    }

    VEnginePlayer::~VEnginePlayer()
    {
        UnInit();
        runtimeAssetLoader_.Shutdown();
    }

    const ViewportClient& VEnginePlayer::GetViewportClient() const noexcept
    {
        return viewportClient_;
    }

    ViewportClient& VEnginePlayer::GetViewportClient() noexcept
    {
        return viewportClient_;
    }

    ErrorCode VEnginePlayer::InitializeRendering(Window& mainWindow)
    {
        // 1. Mirror the initial window surface into the Player viewport before render resources bind to it.
        viewportClient_.SyncFromWindow(mainWindow);

        // 2. Let the shared Application path create the RHI device and main swapchain.
        const ErrorCode renderResult = Application::InitializeRendering(mainWindow);
        if (renderResult != ErrorCode::None)
        {
            return renderResult;
        }

        // 3. Register Scene Thread hooks before the loop starts so viewport and startup-scene work stay off the Main Thread.
        RegisterSceneThreadCallbacks();

        // 4. Probe for packaged data beside the executable and schedule the package start scene when present.
        InitializePackagedProject();
        return ErrorCode::None;
    }

    void VEnginePlayer::RegisterSceneThreadCallbacks()
    {
        SceneSystem& sceneSystem = GetRuntime().GetSceneSystem();
        sceneSystem.SetRuntimeOSEventCallback([this](const OSEvent& event) { HandleSceneThreadOSEvent(event); });
        sceneSystem.SetRuntimeStartFrameCallback([this]() { LoadPendingPackagedStartupScene(); });
    }

    void VEnginePlayer::HandleSceneThreadOSEvent(const OSEvent& event)
    {
        VE_ASSERT_SCENE_THREAD();

        switch (event.type)
        {
        case OSEventType::WindowResized:
            if (event.width == 0 || event.height == 0)
            {
                break;
            }

            // Scene Thread owns render-facing viewport state after startup, so resize is applied here instead of on
            // the Main Thread. The binding itself stays window-backed; only the extent changes.
            viewportClient_.ResizeWindowSurface(WindowExtent{event.width, event.height});
            break;
        default:
            break;
        }
    }

    void VEnginePlayer::InitializePackagedProject()
    {
        // 1. A loose developer run has no packaged Data directory, so it simply starts with the empty runtime scene.
        const Path dataRoot = FindPackagedDataRoot();
        if (dataRoot.IsEmpty())
        {
            VE_LOG_INFO_CATEGORY("Player", "No packaged project data root found. Starting with an empty runtime scene.");
            return;
        }

        // 2. Point global file/resource lookups at the packaged Data directory before reading package-owned assets.
        ActivatePackagedProjectRoot(dataRoot);

        // 3. Read the project descriptor first. If this fails, leave the Player alive with the empty scene.
        Result<PackagedProjectDescriptor> descriptor = LoadPackagedProjectDescriptor(dataRoot);
        if (!descriptor)
        {
            VE_LOG_ERROR_CATEGORY("Player", "Failed to read packaged project descriptor '{}': {}", dataRoot.GetString(), descriptor.GetError().GetMessage());
            return;
        }

        // 4. Load the runtime manifest. Scene construction is deferred to the Scene Thread on its first frame.
        if (!InitializePackagedAssetLoader(dataRoot))
        {
            return;
        }

        (void)InitializePackagedScripts(dataRoot);

        // 5. Record the start scene for a one-shot Scene Thread load once the main loop starts.
        SchedulePackagedStartupSceneLoad(descriptor.GetValue().startScene);

        VE_LOG_INFO_CATEGORY("Player",
                             "Packaged project data root initialized: '{}', project '{}', start scene '{}', manifest asset count {}.",
                             dataRoot.GetString(),
                             descriptor.GetValue().name,
                             packagedStartScene_,
                             runtimeAssetLoader_.GetAssetManifest().GetAssetCount());
    }

    void VEnginePlayer::ActivatePackagedProjectRoot(const Path& dataRoot)
    {
        FileSystem::SetProjectRoot(dataRoot);
        GetRuntime().GetResourceSystem().SetProjectRoot(dataRoot);
    }

    bool VEnginePlayer::InitializePackagedAssetLoader(const Path& dataRoot)
    {
        const Path manifestPath = dataRoot / PackageManifestFilename;
        const ErrorCode loaderResult = runtimeAssetLoader_.Initialize(RuntimeAssetLoaderInitParam{manifestPath});
        if (loaderResult != ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Player", "Failed to initialize runtime asset loader '{}': {}", manifestPath.GetString(), ToString(loaderResult));
            return false;
        }

        return true;
    }

    bool VEnginePlayer::InitializePackagedScripts(const Path& dataRoot)
    {
        Result<Path> projectAssemblyPath = LoadPackagedScriptAssemblyPath(dataRoot);
        if (!projectAssemblyPath)
        {
            VE_LOG_INFO_CATEGORY("Player", "No packaged script assembly loaded: {}", projectAssemblyPath.GetError().GetMessage());
            return false;
        }

        const Path hostRoot = FileSystem::GetExecutableDirectory() / "Managed" / "VEngine.ScriptHost";
        const Path hostAssemblyPath = hostRoot / "VEngine.ScriptHost.dll";
        const ErrorCode hostResult = GetRuntime().GetScriptingSystem().LoadAssembly(
            ScriptingAssemblyLoadDesc{hostAssemblyPath, "VEngine.Scripting.NativeScriptBridge, VEngine.ScriptHost"});
        if (hostResult != ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Player", "Failed to load VEngine.ScriptHost '{}': {}", hostAssemblyPath.GetString(), ToString(hostResult));
            return false;
        }

        const ErrorCode projectResult = GetRuntime().GetScriptingSystem().LoadProjectAssembly(ScriptingProjectAssemblyLoadDesc{projectAssemblyPath.GetValue()});
        if (projectResult != ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Player", "Failed to load project scripts '{}': {}", projectAssemblyPath.GetValue().GetString(), ToString(projectResult));
            return false;
        }

        VE_LOG_INFO_CATEGORY("Player", "Loaded packaged script assembly '{}'.", projectAssemblyPath.GetValue().GetString());
        return true;
    }

    void VEnginePlayer::SchedulePackagedStartupSceneLoad(std::string startScene)
    {
        packagedStartScene_ = startScene.empty() ? DefaultStartScene : std::move(startScene);
        pendingPackagedStartupSceneLoad_ = true;
    }

    void VEnginePlayer::LoadPendingPackagedStartupScene()
    {
        VE_ASSERT_SCENE_THREAD();
        if (!pendingPackagedStartupSceneLoad_)
        {
            return;
        }

        pendingPackagedStartupSceneLoad_ = false;
        if (!runtimeAssetLoader_.IsInitialized())
        {
            VE_LOG_ERROR_CATEGORY("Player", "Cannot load packaged start scene because RuntimeAssetLoader is not initialized.");
            return;
        }

        Result<AssetID> sceneAssetID = runtimeAssetLoader_.FindAssetIDByRuntimePath(Path(packagedStartScene_));
        if (!sceneAssetID)
        {
            VE_LOG_ERROR_CATEGORY("Player", "Packaged start scene '{}' was not found in AssetManifest: {}", packagedStartScene_, sceneAssetID.GetError().GetMessage());
            return;
        }

        SceneLoadRequest loadRequest;
        loadRequest.source = SceneLoadSource::Asset;
        loadRequest.scene = sceneAssetID.GetValue();
        loadRequest.executionMode = SceneExecutionMode::Runtime;
        loadRequest.provider = &runtimeAssetLoader_;
        loadRequest.resourceSystem = &GetRuntime().GetResourceSystem();
        loadRequest.scriptingSystem = &GetRuntime().GetScriptingSystem();
        GetRuntime().GetSceneSystem().LoadScene(loadRequest);
        VE_LOG_INFO_CATEGORY("Player", "Loaded packaged start scene '{}'.", packagedStartScene_);
    }
} // namespace ve
