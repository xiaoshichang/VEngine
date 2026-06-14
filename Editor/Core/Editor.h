#pragma once

#include "Editor/Core/EditorAssetDatabase.h"
#include "Editor/Core/EditorInput.h"
#include "Editor/Core/EditorProjectRegistry.h"
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
    class ProjectEditingView;
    class ProjectSelectionView;

    enum class EditorSelectionType
    {
        None,
        GameObject,
        Asset,
    };

    /// Owns editor-level lifecycle and scene callbacks.
    class Editor : public NonMovable
    {
    public:
        Editor() = default;
        ~Editor();

        [[nodiscard]] ErrorCode Init(EngineRuntime& runtime,
                                     ApplicationCommandQueue& mainThreadCommandQueue,
                                     void* nativeWindowHandle);
        void StartFrame();
        [[nodiscard]] std::shared_ptr<FrameRenderer> Render();
        void UnInit() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] SceneSystem& GetSceneSystem() noexcept;
        [[nodiscard]] const SceneSystem& GetSceneSystem() const noexcept;
        [[nodiscard]] RenderSystem& GetRenderSystem() noexcept;
        [[nodiscard]] EditorAssetDatabase& GetAssetDatabase() noexcept;
        [[nodiscard]] const EditorAssetDatabase& GetAssetDatabase() const noexcept;
        void SetSelectedGameObject(ve::GameObject* gameObject) noexcept;
        void SetSelectedAsset(Path assetPath);
        void ClearSelection() noexcept;
        [[nodiscard]] EditorSelectionType GetSelectionType() const noexcept;
        [[nodiscard]] ve::GameObject* GetSelectedGameObject() noexcept;
        [[nodiscard]] const ve::GameObject* GetSelectedGameObject() const noexcept;
        [[nodiscard]] const Path& GetSelectedAssetPath() const noexcept;
        void KeepImGuiTextureAlive(std::shared_ptr<RenderTexture> renderTexture);

        void OpenProject(std::string projectPath);
        void ShowProjectSelection() noexcept;
        [[nodiscard]] const std::string& GetCurrentProjectPath() const noexcept;
        [[nodiscard]] const std::string& GetCurrentProjectName() const noexcept;
        [[nodiscard]] const std::vector<std::string>& GetRecentProjects() const noexcept;
        [[nodiscard]] static std::string GetProjectDisplayName(const std::string& projectPath);

    private:
        enum class MainView
        {
            ProjectSelection,
            ProjectEditing,
        };

        [[nodiscard]] ErrorCode InitRenderBackend(RenderSystem& renderSystem);
        void ShutdownRenderBackend() noexcept;
        void AddRecentProject(const std::string& projectPath);
        void SetCurrentProject(std::string projectPath);
        void EnqueueMainWindowTitleUpdate();
        static void ApplyMainWindowTitle(void* nativeWindowHandle, const std::string& title);
        [[nodiscard]] std::string BuildMainWindowTitle() const;
        [[nodiscard]] const char* GetRenderBackendName() const noexcept;

        SceneSystem* sceneSystem_ = nullptr;
        RenderSystem* renderSystem_ = nullptr;
        ApplicationCommandQueue* mainThreadCommandQueue_ = nullptr;
        void* nativeWindowHandle_ = nullptr;
        EditorInput input_;
        ProjectSelectionView* projectSelectionView_ = nullptr;
        ProjectEditingView* projectEditingView_ = nullptr;
        RenderBackend renderBackend_ = RenderBackend::D3D12;
        EditorAssetDatabase assetDatabase_;
        EditorSelectionType selectionType_ = EditorSelectionType::None;
        ve::GameObject* selectedGameObject_ = nullptr;
        Path selectedAssetPath_;
        std::atomic_bool initialized_{false};
        MainView mainView_ = MainView::ProjectSelection;

        // ImGui consumes native texture handles as raw IDs. Keep the owning RenderTexture objects alive at editor
        // scope, and let panels register those textures once when their editor-side view is initialized.
        std::vector<std::shared_ptr<RenderTexture>> retainedImGuiRenderTextures_;
        std::vector<std::string> recentProjects_;
        std::string currentProjectPath_;
        std::string currentProjectName_;
    };
} // namespace ve::editor
