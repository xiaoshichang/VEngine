#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Resource/ResourceManifest.h"
#include "Engine/Runtime/Resource/ResourceSystem.h"

#include <iostream>

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

    bool TestManifestRoundTrip()
    {
        bool passed = true;

        ve::ResourceManifest manifest;
        ve::ResourceRecord record;
        record.guid = ve::Guid::Parse("11111111-1111-1111-1111-111111111111").GetValue();
        record.type = ve::ResourceType::Mesh;
        record.runtimePath = ve::Path("Library/Imported/11111111-1111-1111-1111-111111111111/Cube.vemesh");
        record.contentHash = "abc123";
        passed &= Expect(manifest.AddOrUpdate(record) == ve::ErrorCode::None, "Manifest should accept a record");

        const ve::Path tempPath = ve::FileSystem::GetCurrentWorkingDirectory() / "ResourceManifestTests.json";
        passed &= Expect(manifest.SaveToFile(tempPath) == ve::ErrorCode::None, "Manifest should save");

        ve::ResourceManifest loadedManifest;
        passed &= Expect(loadedManifest.LoadFromFile(tempPath) == ve::ErrorCode::None, "Manifest should load");

        const ve::ResourceRecord* loadedRecord =
            loadedManifest.Find(ve::Guid::Parse("11111111-1111-1111-1111-111111111111").GetValue());
        passed &= Expect(loadedRecord != nullptr, "Loaded manifest should contain the record");
        passed &= Expect(loadedRecord != nullptr && loadedRecord->runtimePath == record.runtimePath,
                         "Runtime path should round-trip");

        (void)ve::FileSystem::RemoveFile(tempPath);
        return passed;
    }

    bool TestResourceSystemLoad()
    {
        bool passed = true;

        const ve::Path workingDirectory = ve::FileSystem::GetCurrentWorkingDirectory();
        const ve::Path meshPath = workingDirectory / "ResourceSystemMesh.vemesh";

        passed &= Expect(ve::FileSystem::WriteTextFile(meshPath, "mesh-data") == ve::ErrorCode::None,
                         "Should write test mesh file");

        ve::ResourceSystem resourceSystem;
        passed &= Expect(resourceSystem.Initialize(ve::ResourceSystemInitParam{workingDirectory}) ==
                             ve::ErrorCode::None,
                         "ResourceSystem should initialize");
        resourceSystem.SetManifestPath(workingDirectory / "ResourceSystemTests.json");

        ve::ResourceManifest manifest;
        ve::ResourceRecord record;
        record.guid = ve::Guid::Parse("22222222-2222-2222-2222-222222222222").GetValue();
        record.type = ve::ResourceType::Mesh;
        record.runtimePath = ve::Path(meshPath.GetFilename());
        passed &= Expect(manifest.AddOrUpdate(record) == ve::ErrorCode::None, "Manifest should accept mesh record");
        passed &= Expect(manifest.SaveToFile(resourceSystem.GetManifestPath()) == ve::ErrorCode::None,
                         "Manifest should save");
        passed &= Expect(resourceSystem.ReloadManifest() == ve::ErrorCode::None,
                         "ResourceSystem should load manifest");

        ve::Result<ve::LoadedResourceData> loaded =
            resourceSystem.LoadResource(ve::Guid::Parse("22222222-2222-2222-2222-222222222222").GetValue());
        passed &= Expect(loaded.IsOk(), "ResourceSystem should load mesh resource");
        passed &= Expect(loaded.IsOk() && loaded.GetValue().text == "mesh-data", "Loaded resource text should match");

        resourceSystem.Shutdown();
        (void)ve::FileSystem::RemoveFile(resourceSystem.GetManifestPath());
        (void)ve::FileSystem::RemoveFile(meshPath);
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;
    passed &= TestManifestRoundTrip();
    passed &= TestResourceSystemLoad();
    if (passed)
    {
        std::cout << "VEngineResourceManifestTests passed" << '\n';
        return 0;
    }

    return 1;
}
