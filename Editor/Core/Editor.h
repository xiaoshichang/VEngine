#pragma once

#include "Editor/Core/EditorAssetDatabase.h"
#include "Editor/Core/EditorEventDispatcher.h"
#include "Editor/Core/EditorEvents.h"
#include "Editor/Core/EditorInput.h"
#include "Editor/Core/EditorProject.h"
#include "Editor/Core/EditorProjectRegistry.h"
#include "Editor/Core/EditorResourceLoader.h"
#include "Editor/Core/EditorScriptCompiler.h"
#include "Editor/Core/EditorScriptDatabase.h"
#include "Engine/Runtime/Application/ApplicationCommandQueue.h"
#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/RenderSystem.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace ve
{
    class GameObject;
}

namespace ve::editor
{
    struct EditorFrameDrawData;
    struct EditorFrameRenderViews;
    class ProjectEditingView;
    class ProjectSelectionView;

    /// Owns editor-level lifecycle and scene callbacks.
    class Editor : public NonMovable
    {
    public:
        Editor() = default;
        ~Editor();

        [[nodiscard]] ErrorCode Init(EngineRuntime& runtime, ApplicationCommandQueue& mainThreadCommandQueue, void* nativeWindowHandle);
        void StartFrame();
        [[nodiscard]] std::shared_ptr<FrameRenderPipeline> Render();
        void UnInit() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] SceneSystem& GetSceneSystem() noexcept;
        [[nodiscard]] const SceneSystem& GetSceneSystem() const noexcept;
        [[nodiscard]] EngineRuntime& GetRuntime() noexcept;
        [[nodiscard]] const EngineRuntime& GetRuntime() const noexcept;
        [[nodiscard]] RenderSystem& GetRenderSystem() noexcept;
        [[nodiscard]] const EditorInput& GetInput() const noexcept;
        [[nodiscard]] EditorAssetDatabase& GetAssetDatabase() noexcept;
        [[nodiscard]] const EditorAssetDatabase& GetAssetDatabase() const noexcept;
        [[nodiscard]] EditorResourceLoader& GetResourceLoader() noexcept;
        [[nodiscard]] const EditorResourceLoader& GetResourceLoader() const noexcept;
        [[nodiscard]] EditorScriptDatabase& GetScriptDatabase() noexcept;
        [[nodiscard]] const EditorScriptDatabase& GetScriptDatabase() const noexcept;
        [[nodiscard]] EditorEventDispatcher& GetEventDispatcher() noexcept;
        [[nodiscard]] const EditorEventDispatcher& GetEventDispatcher() const noexcept;
        void SetSelectedGameObject(ve::GameObject* gameObject);
        void SetSelectedAsset(Path assetPath);
        void ClearSelection();
        [[nodiscard]] EditorSelectionType GetSelectionType() const noexcept;
        [[nodiscard]] EditorSelectionChangedEvent GetSelection() const;
        [[nodiscard]] ve::GameObject* GetSelectedGameObject() noexcept;
        [[nodiscard]] const ve::GameObject* GetSelectedGameObject() const noexcept;
        [[nodiscard]] const Path& GetSelectedAssetPath() const noexcept;
        void KeepImGuiTextureAlive(std::shared_ptr<RenderTexture> renderTexture);
        [[nodiscard]] std::vector<AssetID> CollectActiveResourceRoots() const;
        void CollectUnusedResources();
        [[nodiscard]] bool IsPlaying() const noexcept;
        [[nodiscard]] bool CanStartPlay() const noexcept;
        [[nodiscard]] bool CanStopPlay() const noexcept;
        void StartPlay();
        void StopPlay();

        void OpenProject(std::string projectPath);
        void ShowProjectSelection();
        void OpenScene(Path scenePath);
        void SaveCurrentScene();
        void RecompileScripts();
        [[nodiscard]] bool CanSaveCurrentScene() const noexcept;
        [[nodiscard]] const std::string& GetCurrentProjectPath() const noexcept;
        [[nodiscard]] const std::string& GetCurrentProjectName() const noexcept;
        [[nodiscard]] const Path& GetCurrentScenePath() const noexcept;
        [[nodiscard]] const std::vector<std::string>& GetRecentProjects() const noexcept;
        [[nodiscard]] static std::string GetProjectDisplayName(const std::string& projectPath);

    private:
        enum class MainView
        {
            ProjectSelection,
            ProjectEditing,
        };

        enum class EditorPlayState
        {
            Editing,
            Playing,
        };

        void RenderActiveMainView();
        [[nodiscard]] std::shared_ptr<EditorFrameDrawData> CaptureImGuiFrameDrawData() const;
        [[nodiscard]] EditorFrameRenderViews CollectFrameRenderViews() const;
        [[nodiscard]] EditorOverlayRenderCallback BuildOverlayRenderCallback(std::shared_ptr<EditorFrameDrawData> frameDrawData) const;
        void AddSceneViewRenderer(EditorRenderFramePipelineInitParam& pipelineInitParam,
                                  const EditorFrameRenderViews& views,
                                  const std::shared_ptr<RTScene>& renderScene) const;
        void AddGameViewRenderer(EditorRenderFramePipelineInitParam& pipelineInitParam,
                                 const EditorFrameRenderViews& views,
                                 const std::shared_ptr<RTScene>& renderScene) const;
        [[nodiscard]] std::shared_ptr<RTScene> GetActiveRenderScene() const;
        void ShutdownOpenProjectState() noexcept;
        [[nodiscard]] Result<EditorProjectDescriptor> PrepareOpenProjectDescriptor(const Path& projectRoot, const std::string& projectPath);
        [[nodiscard]] ErrorCode InitializeOpenProjectAssetServices(const Path& projectRoot, const std::string& projectPath);
        void ActivateOpenProjectContext(std::string projectPath, const Path& projectRoot, const EditorProjectDescriptor& descriptor);
        void LoadOpenProjectStartScene(const EditorProjectDescriptor& descriptor);
        [[nodiscard]] ErrorCode LoadScriptHostAssembly();
        void EnterProjectEditingView();
        void AddRecentProject(const std::string& projectPath);
        void SetCurrentProject(std::string projectPath);
        void DispatchSelectionChanged();
        void EnqueueMainWindowTitleUpdate();
        static void ApplyMainWindowTitle(void* nativeWindowHandle, const std::string& title);
        [[nodiscard]] std::string BuildMainWindowTitle() const;
        [[nodiscard]] const char* GetRenderBackendName() const noexcept;
        [[nodiscard]] ErrorCode InitRenderBackend(RenderSystem& renderSystem);
        void ShutdownRenderBackend() noexcept;

        SceneSystem* sceneSystem_ = nullptr;
        EngineRuntime* runtime_ = nullptr;
        RenderSystem* renderSystem_ = nullptr;
        ApplicationCommandQueue* mainThreadCommandQueue_ = nullptr;
        void* nativeWindowHandle_ = nullptr;
        EditorInput input_;
        ProjectSelectionView* projectSelectionView_ = nullptr;
        ProjectEditingView* projectEditingView_ = nullptr;
        RenderBackend renderBackend_ = RenderBackend::D3D12;
        EditorAssetDatabase assetDatabase_;
        EditorResourceLoader resourceLoader_;
        EditorScriptCompiler scriptCompiler_;
        EditorScriptDatabase scriptDatabase_;
        EditorEventDispatcher eventDispatcher_;
        EditorSelectionType selectionType_ = EditorSelectionType::None;
        ve::GameObject* selectedGameObject_ = nullptr;
        Path selectedAssetPath_;
        std::atomic_bool initialized_{false};
        MainView mainView_ = MainView::ProjectSelection;
        EditorPlayState playState_ = EditorPlayState::Editing;
        UInt64 playSessionID_ = 0;

        // ImGui consumes native texture handles as raw IDs. Keep the owning RenderTexture objects alive at editor
        // scope, and let panels register those textures once when their editor-side view is initialized.
        std::vector<std::shared_ptr<RenderTexture>> retainedImGuiRenderTextures_;
        std::vector<std::string> recentProjects_;
        std::string currentProjectPath_;
        std::string currentProjectName_;
        Path currentScenePath_;
        std::string editingSceneSnapshot_;
    };
} // namespace ve::editor
