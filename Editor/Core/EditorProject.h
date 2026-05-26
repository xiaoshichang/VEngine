#pragma once

#include "Engine/Runtime/Asset/AssetDatabase.h"
#include "Engine/Runtime/Asset/AssetGuid.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Scene/Scene.h"

#include <string>
#include <string_view>
#include <vector>

namespace ve
{
    class GameThreadSystem;
    class ResourceManager;

    struct EditorProjectSceneReference
    {
        AssetGuid guid;
        Path path;
    };

    struct EditorProjectDescriptor
    {
        UInt32 version = 1;
        AssetGuid guid;
        std::string displayName;
        std::string engineVersion;
        std::vector<std::string> targetPlatforms;
        EditorProjectSceneReference startupScene;
    };

    enum class EditorProjectDiagnosticSeverity
    {
        Info,
        Warning,
        Error,
    };

    struct EditorProjectDiagnostic
    {
        EditorProjectDiagnosticSeverity severity = EditorProjectDiagnosticSeverity::Info;
        std::string message;
    };

    class EditorProjectService
    {
    public:
        [[nodiscard]] static Result<EditorProjectDescriptor> LoadProjectDescriptor(const Path& projectRoot);
        [[nodiscard]] static ErrorCode CreateProjectSkeleton(const Path& projectRoot, std::string_view displayName);

        [[nodiscard]] ErrorCode OpenProject(const Path& projectRoot, ResourceManager& resourceManager);
        void CloseProject(GameThreadSystem* gameThreadSystem = nullptr) noexcept;
        [[nodiscard]] ErrorCode BindActiveScene(GameThreadSystem& gameThreadSystem, ResourceManager& resourceManager);

        [[nodiscard]] bool HasOpenProject() const noexcept;
        [[nodiscard]] const Path& GetProjectRoot() const noexcept;
        [[nodiscard]] const EditorProjectDescriptor& GetDescriptor() const noexcept;
        [[nodiscard]] AssetDatabase& GetAssetDatabase() noexcept;
        [[nodiscard]] const AssetDatabase& GetAssetDatabase() const noexcept;
        [[nodiscard]] Scene& GetCurrentEditScene() noexcept;
        [[nodiscard]] const Scene& GetCurrentEditScene() const noexcept;
        [[nodiscard]] bool IsDirty() const noexcept;
        void MarkDirty() noexcept;
        void ClearDirty() noexcept;

        [[nodiscard]] Path GetGeneratedRoot() const;
        [[nodiscard]] Path GetEditorLayoutPath() const;
        [[nodiscard]] const std::vector<EditorProjectDiagnostic>& GetDiagnostics() const noexcept;

    private:
        void ClearOpenedProject() noexcept;
        void AddDiagnostic(EditorProjectDiagnosticSeverity severity, std::string message);
        [[nodiscard]] ErrorCode ValidateDirectoryContract(const Path& projectRoot);
        [[nodiscard]] ErrorCode EnsureGeneratedDirectories(const Path& projectRoot);
        [[nodiscard]] ErrorCode OpenStartupScene(ResourceManager& resourceManager);
        void OpenEmptyEditScene();

        Path projectRoot_;
        EditorProjectDescriptor descriptor_;
        AssetDatabase assetDatabase_;
        Scene currentEditScene_;
        std::vector<EditorProjectDiagnostic> diagnostics_;
        bool hasOpenProject_ = false;
        bool dirty_ = false;
    };
} // namespace ve
