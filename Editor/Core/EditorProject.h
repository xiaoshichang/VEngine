#pragma once

#include "Engine/Runtime/Asset/AssetDatabase.h"
#include "Engine/Runtime/Asset/AssetGuid.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scripting/ScriptProject.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ve
{
    class GameThreadSystem;
    class InputSystem;
    class ResourceManager;
    class ScriptContext;
    class ScriptHost;

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
        ScriptProjectConfig scripting;
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

    enum class EditorMeshRendererAssetSlot
    {
        Mesh,
        Material,
    };

    struct EditorAuthoredAssetReference
    {
        AssetGuid guid;
        Path path;
        bool hasValue = false;

        [[nodiscard]] bool IsSet() const noexcept
        {
            return hasValue && (guid.IsValid() || !path.IsEmpty());
        }
    };

    struct EditorMeshRendererAssetReferences
    {
        SceneObjectId gameObjectId = InvalidSceneObjectId;
        SizeT componentIndex = 0;
        EditorAuthoredAssetReference mesh;
        EditorAuthoredAssetReference material;
    };

    using EditorProjectDiagnosticSink = std::function<void(EditorProjectDiagnosticSeverity, std::string)>;

    class EditorProjectAssetService
    {
    public:
        void Clear() noexcept;
        [[nodiscard]] ErrorCode Open(const Path& projectRoot, const EditorProjectDiagnosticSink& diagnostics);
        [[nodiscard]] ErrorCode Refresh(const EditorProjectDiagnosticSink& diagnostics);
        [[nodiscard]] ErrorCode Validate(const EditorProjectDiagnosticSink& diagnostics) const;

        [[nodiscard]] AssetDatabase& GetDatabase() noexcept;
        [[nodiscard]] const AssetDatabase& GetDatabase() const noexcept;

    private:
        AssetDatabase assetDatabase_;
    };

    class EditorProjectSceneService
    {
    public:
        void Clear() noexcept;
        void OpenEmptyEditScene() noexcept;
        [[nodiscard]] ErrorCode OpenScene(const Path& projectRelativeScenePath,
                                          AssetDatabase& assetDatabase,
                                          ResourceManager& resourceManager,
                                          const EditorProjectDiagnosticSink& diagnostics);
        [[nodiscard]] ErrorCode OpenSceneFromRecord(const AssetRecord& record,
                                                    AssetDatabase& assetDatabase,
                                                    ResourceManager& resourceManager,
                                                    const EditorProjectDiagnosticSink& diagnostics);
        [[nodiscard]] ErrorCode SaveCurrentScene(AssetDatabase& assetDatabase,
                                                 const EditorProjectDiagnosticSink& diagnostics);

        [[nodiscard]] Scene& GetCurrentEditScene() noexcept;
        [[nodiscard]] const Scene& GetCurrentEditScene() const noexcept;
        [[nodiscard]] bool HasCurrentScene() const noexcept;
        [[nodiscard]] const Path& GetCurrentScenePath() const noexcept;
        [[nodiscard]] const AssetGuid& GetCurrentSceneGuid() const noexcept;
        [[nodiscard]] bool IsDirty() const noexcept;
        void MarkDirty() noexcept;
        void MarkActiveSceneEdited(bool isPlaying) noexcept;
        void ClearDirty() noexcept;

        [[nodiscard]] const EditorAuthoredAssetReference*
        FindMeshRendererAssetReference(SceneObjectId gameObjectId,
                                       SizeT componentIndex,
                                       EditorMeshRendererAssetSlot slot) const noexcept;

    private:
        Scene currentEditScene_;
        Path currentScenePath_;
        AssetGuid currentSceneGuid_;
        std::vector<EditorMeshRendererAssetReferences> meshRendererAssetReferences_;
        bool dirty_ = false;
    };

    class EditorProjectScriptService
    {
    public:
        void Clear() noexcept;
        [[nodiscard]] ErrorCode GenerateWorkspace(const Path& projectRoot,
                                                  const EditorProjectDiagnosticSink& diagnostics);
        [[nodiscard]] ErrorCode BuildScripts(const Path& projectRoot,
                                             const EditorProjectDescriptor& descriptor,
                                             ScriptBuildConfiguration configuration,
                                             const EditorProjectDiagnosticSink& diagnostics);
        [[nodiscard]] ErrorCode PreparePlayModeScripts(const Path& projectRoot,
                                                       const EditorProjectDescriptor& descriptor,
                                                       ScriptBuildConfiguration configuration,
                                                       InputSystem* inputSystem,
                                                       Scene& playScene,
                                                       const EditorProjectDiagnosticSink& diagnostics);

    private:
        std::unique_ptr<ScriptHost> scriptHost_;
        std::unique_ptr<ScriptContext> scriptContext_;
    };

    class EditorProjectPlayModeService
    {
    public:
        void Clear(EditorProjectScriptService& scriptService) noexcept;
        [[nodiscard]] ErrorCode Start(const Path& projectRoot,
                                      const EditorProjectDescriptor& descriptor,
                                      Scene& editScene,
                                      GameThreadSystem& gameThreadSystem,
                                      ResourceManager& resourceManager,
                                      EditorProjectScriptService& scriptService,
                                      InputSystem* inputSystem,
                                      const EditorProjectDiagnosticSink& diagnostics);
        [[nodiscard]] ErrorCode Stop(GameThreadSystem& gameThreadSystem,
                                     EditorProjectScriptService& scriptService,
                                     const EditorProjectDiagnosticSink& diagnostics);
        void Stop(EditorProjectScriptService& scriptService, const EditorProjectDiagnosticSink& diagnostics);

        [[nodiscard]] Scene& GetActiveScene(Scene& editScene) noexcept;
        [[nodiscard]] const Scene& GetActiveScene(const Scene& editScene) const noexcept;
        [[nodiscard]] bool IsPlaying() const noexcept;

    private:
        Scene playScene_;
        bool isPlaying_ = false;
    };

    class EditorProjectService
    {
    public:
        EditorProjectService();
        ~EditorProjectService();

        [[nodiscard]] static Result<EditorProjectDescriptor> LoadProjectDescriptor(const Path& projectRoot);
        [[nodiscard]] static ErrorCode CreateProjectSkeleton(const Path& projectRoot, std::string_view displayName);

        [[nodiscard]] ErrorCode OpenProject(const Path& projectRoot, ResourceManager& resourceManager);
        void CloseProject(GameThreadSystem* gameThreadSystem = nullptr) noexcept;
        [[nodiscard]] ErrorCode BindActiveScene(GameThreadSystem& gameThreadSystem, ResourceManager& resourceManager);
        [[nodiscard]] ErrorCode RefreshAssetDatabase();
        [[nodiscard]] ErrorCode GenerateScriptWorkspace();
        [[nodiscard]] ErrorCode OpenScene(const Path& projectRelativeScenePath, ResourceManager& resourceManager);
        [[nodiscard]] ErrorCode SaveCurrentScene();

        [[nodiscard]] bool HasOpenProject() const noexcept;
        [[nodiscard]] const Path& GetProjectRoot() const noexcept;
        [[nodiscard]] const EditorProjectDescriptor& GetDescriptor() const noexcept;
        [[nodiscard]] AssetDatabase& GetAssetDatabase() noexcept;
        [[nodiscard]] const AssetDatabase& GetAssetDatabase() const noexcept;
        [[nodiscard]] Scene& GetCurrentEditScene() noexcept;
        [[nodiscard]] const Scene& GetCurrentEditScene() const noexcept;
        [[nodiscard]] Scene& GetActiveScene() noexcept;
        [[nodiscard]] const Scene& GetActiveScene() const noexcept;
        [[nodiscard]] bool HasCurrentScene() const noexcept;
        [[nodiscard]] const Path& GetCurrentScenePath() const noexcept;
        [[nodiscard]] const AssetGuid& GetCurrentSceneGuid() const noexcept;
        [[nodiscard]] bool IsPlaying() const noexcept;
        [[nodiscard]] bool HasWindowsScripts() const noexcept;
        [[nodiscard]] ErrorCode BuildScripts(ScriptBuildConfiguration configuration);
        [[nodiscard]] ErrorCode StartPlayMode(GameThreadSystem& gameThreadSystem,
                                              ResourceManager& resourceManager,
                                              InputSystem* inputSystem = nullptr);
        [[nodiscard]] ErrorCode StopPlayMode(GameThreadSystem& gameThreadSystem, ResourceManager& resourceManager);
        void StopPlayMode();
        [[nodiscard]] bool IsDirty() const noexcept;
        void MarkDirty() noexcept;
        void MarkActiveSceneEdited() noexcept;
        void ClearDirty() noexcept;

        [[nodiscard]] Path GetGeneratedRoot() const;
        [[nodiscard]] Path GetEditorLayoutPath() const;
        [[nodiscard]] const std::vector<EditorProjectDiagnostic>& GetDiagnostics() const noexcept;
        [[nodiscard]] const EditorAuthoredAssetReference*
        FindMeshRendererAssetReference(SceneObjectId gameObjectId,
                                       SizeT componentIndex,
                                       EditorMeshRendererAssetSlot slot) const noexcept;
    private:
        void ClearOpenedProject() noexcept;
        void AddDiagnostic(EditorProjectDiagnosticSeverity severity, std::string message);
        [[nodiscard]] ErrorCode ValidateDirectoryContract(const Path& projectRoot);
        [[nodiscard]] ErrorCode EnsureGeneratedDirectories(const Path& projectRoot);
        [[nodiscard]] ErrorCode OpenStartupScene(ResourceManager& resourceManager);
        [[nodiscard]] EditorProjectDiagnosticSink MakeDiagnosticSink();

        Path projectRoot_;
        EditorProjectDescriptor descriptor_;
        EditorProjectAssetService assetService_;
        EditorProjectSceneService sceneService_;
        EditorProjectScriptService scriptService_;
        EditorProjectPlayModeService playModeService_;
        std::vector<EditorProjectDiagnostic> diagnostics_;
        bool hasOpenProject_ = false;
    };
} // namespace ve
