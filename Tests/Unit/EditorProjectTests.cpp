#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Resource/ResourceManager.h"

#include <filesystem>
#include <iostream>
#include <system_error>

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

    bool ExpectOk(ve::ErrorCode result, const char* message)
    {
        if (result != ve::ErrorCode::None)
        {
            std::cerr << "FAILED: " << message << " (" << ve::ToString(result) << ")" << '\n';
            return false;
        }

        return true;
    }

    template<typename T>
    bool ExpectOk(const ve::Result<T>& result, const char* message)
    {
        if (!result)
        {
            std::cerr << "FAILED: " << message << " (" << result.GetError().GetMessage() << ")" << '\n';
            return false;
        }

        return true;
    }

    ve::Path GetTestRoot()
    {
        return ve::FileSystem::GetCurrentWorkingDirectory() / "Generated/EditorProjectTests";
    }

    void CleanTestRoot()
    {
        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path("Generated") / "EditorProjectTests", error);
    }

    bool TestCreateAndOpenProject()
    {
        bool passed = true;

        CleanTestRoot();

        const ve::Path projectRoot = GetTestRoot() / "Project";
        passed &= ExpectOk(ve::EditorProjectService::CreateProjectSkeleton(projectRoot, "EditorProjectTest"),
                           "CreateProjectSkeleton should create a project");
        passed &= Expect(ve::FileSystem::IsFile(projectRoot / ".veproject"), "Project descriptor should be created");
        passed &= Expect(ve::FileSystem::IsDirectory(projectRoot / "Assets"), "Assets directory should be created");
        passed &= Expect(ve::FileSystem::IsDirectory(projectRoot / "Generated/Build"),
                         "Generated/Build should be created");
        passed &= Expect(ve::FileSystem::IsDirectory(projectRoot / "Generated/Editor/Workspace"),
                         "Generated/Editor/Workspace should be created");

        ve::Result<ve::EditorProjectDescriptor> descriptor =
            ve::EditorProjectService::LoadProjectDescriptor(projectRoot);
        passed &= ExpectOk(descriptor, "Project descriptor should parse");
        if (descriptor)
        {
            passed &= Expect(descriptor.GetValue().displayName == "EditorProjectTest",
                             "Project descriptor should preserve display name");
            passed &= Expect(descriptor.GetValue().guid.IsValid(), "Project descriptor should have project GUID");
            passed &= Expect(!descriptor.GetValue().engineVersion.empty(),
                             "Project descriptor should have engine version");
            passed &= Expect(!descriptor.GetValue().targetPlatforms.empty(),
                             "Project descriptor should have target platforms");
            passed &= Expect(descriptor.GetValue().startupScene.path == ve::Path("Assets/Scenes/Main.vescene"),
                             "Project descriptor should point at startup scene");
        }

        ve::ResourceManager resourceManager;
        ve::EditorProjectService projectService;
        passed &= ExpectOk(projectService.OpenProject(projectRoot, resourceManager),
                           "EditorProjectService should open created project");
        passed &= Expect(projectService.HasOpenProject(), "Project service should report an open project");
        passed &= Expect(projectService.GetProjectRoot() == projectRoot, "Project root should be stored");
        passed &= Expect(projectService.GetAssetDatabase().GetRecords().size() == 1,
                         "AssetDatabase should discover the startup scene");
        passed &= Expect(projectService.GetCurrentEditScene().GetGameObjectCount() == 0,
                         "Empty startup scene should load as an empty edit scene");
        passed &= Expect(projectService.GetEditorLayoutPath() ==
                             (projectRoot / "Generated/Editor/Workspace/Layout.ini"),
                         "Editor layout path should live under Generated/Editor");
        passed &= Expect(!projectService.IsDirty(), "Opened project should not be dirty");

        projectService.MarkDirty();
        passed &= Expect(projectService.IsDirty(), "MarkDirty should set dirty state");
        projectService.ClearDirty();
        passed &= Expect(!projectService.IsDirty(), "ClearDirty should reset dirty state");

        CleanTestRoot();
        return passed;
    }

    bool TestRejectsMissingDescriptor()
    {
        bool passed = true;

        CleanTestRoot();
        const ve::Path projectRoot = GetTestRoot() / "MissingDescriptor";
        passed &= ExpectOk(ve::FileSystem::CreateDirectories(projectRoot / "Assets"),
                           "Test project Assets directory should be created");

        ve::ResourceManager resourceManager;
        ve::EditorProjectService projectService;
        passed &= Expect(projectService.OpenProject(projectRoot, resourceManager) == ve::ErrorCode::NotFound,
                         "OpenProject should reject roots without .veproject");
        passed &= Expect(!projectService.HasOpenProject(), "Rejected project should not stay open");

        CleanTestRoot();
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestCreateAndOpenProject();
    passed &= TestRejectsMissingDescriptor();

    if (passed)
    {
        std::cout << "VEngineEditorProjectTests passed" << '\n';
        return 0;
    }

    return 1;
}
