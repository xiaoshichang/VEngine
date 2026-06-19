#include "Player/Windows/WindowsPlayer.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scene/SceneSystem.h"
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
    } // namespace

    WindowsPlayer::WindowsPlayer(ApplicationInitParam initParam)
        : Application(std::move(initParam))
        , viewportClient_("PlayerViewport")
    {
    }

    WindowsPlayer::~WindowsPlayer()
    {
        UnInit();
        runtimeAssetLoader_.Shutdown();
    }

    const ViewportClient& WindowsPlayer::GetViewportClient() const noexcept
    {
        return viewportClient_;
    }

    ViewportClient& WindowsPlayer::GetViewportClient() noexcept
    {
        return viewportClient_;
    }

    ErrorCode WindowsPlayer::InitializeRendering(Window& mainWindow)
    {
        viewportClient_.SyncFromWindow(mainWindow);

        const ErrorCode renderResult = Application::InitializeRendering(mainWindow);
        if (renderResult != ErrorCode::None)
        {
            return renderResult;
        }

        RegisterSceneThreadViewportCallback();
        InitializePackagedProject();
        return ErrorCode::None;
    }

    void WindowsPlayer::RegisterSceneThreadViewportCallback()
    {
        SceneSystem& sceneSystem = GetRuntime().GetSceneSystem();
        sceneSystem.SetRuntimeOSEventCallback([this](const OSEvent& event) { HandleSceneThreadOSEvent(event); });
    }

    void WindowsPlayer::HandleSceneThreadOSEvent(const OSEvent& event)
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

    void WindowsPlayer::InitializePackagedProject()
    {
        const Path dataRoot = FindPackagedDataRoot();
        if (dataRoot.IsEmpty())
        {
            VE_LOG_INFO_CATEGORY("Player", "No packaged project data root found. Starting with an empty runtime scene.");
            return;
        }

        FileSystem::SetProjectRoot(dataRoot);
        GetRuntime().GetResourceSystem().SetProjectRoot(dataRoot);

        Result<PackagedProjectDescriptor> descriptor = LoadPackagedProjectDescriptor(dataRoot);
        if (!descriptor)
        {
            VE_LOG_ERROR_CATEGORY("Player", "Failed to read packaged project descriptor '{}': {}", dataRoot.GetString(), descriptor.GetError().GetMessage());
            return;
        }

        const Path manifestPath = dataRoot / PackageManifestFilename;
        const ErrorCode loaderResult = runtimeAssetLoader_.Initialize(RuntimeAssetLoaderInitParam{manifestPath});
        if (loaderResult != ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Player", "Failed to initialize runtime asset loader '{}': {}", manifestPath.GetString(), ToString(loaderResult));
            return;
        }

        packagedStartScene_ = descriptor.GetValue().startScene.empty() ? DefaultStartScene : descriptor.GetValue().startScene;
        pendingPackagedStartupSceneLoad_ = true;

        GetRuntime().GetSceneSystem().SetEditorCallback(SceneSystemEditorCallback{
            .onStartFrame = [this]() { LoadPendingPackagedStartupScene(); },
        });

        VE_LOG_INFO_CATEGORY("Player",
                             "Packaged project data root initialized: '{}', project '{}', start scene '{}', manifest asset count {}.",
                             dataRoot.GetString(),
                             descriptor.GetValue().name,
                             packagedStartScene_,
                             runtimeAssetLoader_.GetAssetManifest().GetAssetCount());
    }

    void WindowsPlayer::LoadPendingPackagedStartupScene()
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

        Result<Scene*> sceneResult =
            GetRuntime().GetSceneSystem().LoadScene(SceneLoadDesc{sceneAssetID.GetValue(), SceneLoadMode::Single}, runtimeAssetLoader_, GetRuntime().GetResourceSystem());
        if (!sceneResult)
        {
            VE_LOG_ERROR_CATEGORY("Player", "Failed to load packaged start scene '{}': {}", packagedStartScene_, sceneResult.GetError().GetMessage());
            return;
        }

        VE_LOG_INFO_CATEGORY("Player", "Loaded packaged start scene '{}'.", packagedStartScene_);
    }
} // namespace ve
