#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Tools/Package/PackageService.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
    constexpr const char* ProjectGuid = "11111111-1111-4111-8111-111111111111";
    constexpr const char* SourceGuid = "22222222-2222-4222-8222-222222222222";
    constexpr const char* MaterialGuid = "33333333-3333-4333-8333-333333333333";
    constexpr const char* SceneGuid = "44444444-4444-4444-8444-444444444444";

    using boost::json::array;
    using boost::json::object;
    using boost::json::value;

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
        if (result == ve::ErrorCode::None)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result) << '\n';
        return false;
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

    ve::Path GetTestRoot()
    {
        return ve::FileSystem::GetCurrentWorkingDirectory() / "Generated/PackageTests";
    }

    ve::Path GetProjectRoot()
    {
        return GetTestRoot() / "Project";
    }

    void RemoveTestRoot()
    {
        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path("Generated") / "PackageTests", error);
    }

    void ReplaceAll(std::string& text, std::string_view token, std::string_view replacement)
    {
        size_t offset = 0;
        while ((offset = text.find(token, offset)) != std::string::npos)
        {
            text.replace(offset, token.size(), replacement);
            offset += replacement.size();
        }
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

    [[nodiscard]] bool ReadBool(const object& jsonObject, const char* name, bool fallback) noexcept
    {
        const value* member = FindMember(jsonObject, name);
        return member != nullptr && member->is_bool() ? member->as_bool() : fallback;
    }

    [[nodiscard]] bool ArrayContainsString(const array& jsonArray, std::string_view expected)
    {
        for (const value& item : jsonArray)
        {
            if (item.is_string() && item.as_string() == expected)
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] bool ExpectManifestIsPrettyPrinted(const std::string& manifestText)
    {
        return Expect(manifestText.ends_with('\n'), "Manifest should end with a newline") &&
               Expect(manifestText.find("\n  \"format\": \"VEngine.AssetManifest\"") != std::string::npos,
                      "Manifest should be indented for readability");
    }

    [[nodiscard]] bool ParseManifest(const std::string& manifestText, object& manifestObject)
    {
        boost::system::error_code parseError;
        value root = boost::json::parse(manifestText, parseError);
        if (parseError || !root.is_object())
        {
            std::cerr << "FAILED: Manifest should parse as a JSON object\n";
            return false;
        }

        manifestObject = root.as_object();
        return true;
    }

    bool WritePackageProject()
    {
        const ve::Path root = GetProjectRoot();

        std::string project = R"json({
  "format": "VEngine.Project",
  "version": 1,
  "guid": "$PROJECT_GUID$",
  "name": "PackageTestProject",
  "engineVersion": "0.1.0",
  "startupScene": {
    "guid": "$SCENE_GUID$",
    "path": "Assets/Samples/Scenes/TestScene.vescene"
  },
  "targetPlatforms": ["Windows", "iOS"]
})json";
        ReplaceAll(project, "$PROJECT_GUID$", ProjectGuid);
        ReplaceAll(project, "$SCENE_GUID$", SceneGuid);

        std::string metadata = R"json({
  "format": "VEngine.AssetMetadata",
  "version": 1,
  "guid": "$SOURCE_GUID$",
  "assetType": "SourceModel",
  "source": "Assets/Samples/Models/TestQuad.obj",
  "sourceHash": "",
  "importer": "ObjModel",
  "importerVersion": 1,
  "settings": {},
  "artifacts": [
    {
      "type": "Mesh",
      "path": "Generated/Assets/ImportCache/$SOURCE_GUID$/TestQuad.vemesh"
    }
  ],
  "dependencies": []
})json";
        ReplaceAll(metadata, "$SOURCE_GUID$", SourceGuid);

        std::string material = R"json({
  "format": "VEngine.Material",
  "version": 1,
  "guid": "$MATERIAL_GUID$",
  "name": "PackageMaterial",
  "parameters": {
    "baseColor": [0.4, 0.5, 0.6]
  },
  "textures": []
})json";
        ReplaceAll(material, "$MATERIAL_GUID$", MaterialGuid);

        std::string scene = R"json({
  "format": "VEngine.Scene",
  "version": 1,
  "guid": "$SCENE_GUID$",
  "name": "PackageScene",
  "scene": {
    "gameObjects": []
  }
})json";
        ReplaceAll(scene, "$SCENE_GUID$", SceneGuid);

        bool passed = true;
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / ".veproject", project),
                           "Project descriptor should be written");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Assets/Samples/Models/TestQuad.obj", "# quad\n"),
                           "Source model should be written");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Assets/Samples/Models/TestQuad.obj.veasset",
                                                         metadata),
                           "Source metadata should be written");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Assets/Samples/Materials/TestMaterial.vematerial",
                                                         material),
                           "Material asset should be written");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Assets/Samples/Scenes/TestScene.vescene", scene),
                           "Scene asset should be written");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Generated/Assets/ImportCache" / SourceGuid /
                                                             "TestQuad.vemesh",
                                                         "mesh artifact\n"),
                           "Generated mesh artifact should be written");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Generated/Shaders/Windows/D3D11/Basic/Basic.cso",
                                                         "d3d11 shader\n"),
                           "Windows shader artifact should be written");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Generated/Shaders/iOS/Metal/Basic/Basic.metallib",
                                                         "metal shader\n"),
                           "iOS shader artifact should be written");
        return passed;
    }

    bool TestWindowsPackageLayout()
    {
        bool passed = true;

        ve::PackageRequest request;
        request.projectRoot = GetProjectRoot();
        request.outputRoot = GetProjectRoot() / "Generated/Build/Windows/Debug";
        request.platform = ve::PackagePlatform::Windows;
        request.configuration = ve::PackageConfiguration::Debug;
        request.includeRuntimeBinaries = false;

        ve::Result<ve::PackageResult> result = ve::StagePackage(request);
        passed &= ExpectOk(result, "Windows package should stage");
        if (!result)
        {
            return false;
        }

        const ve::Path contentRoot = result.GetValue().contentRoot;
        passed &= Expect(ve::FileSystem::IsFile(contentRoot / ".veproject"),
                         "Windows package should contain .veproject");
        passed &= Expect(ve::FileSystem::IsFile(contentRoot / "AssetManifest.veassetmanifest"),
                         "Windows package should contain asset manifest");
        passed &= Expect(ve::FileSystem::IsFile(contentRoot / "Assets/Samples/Scenes/TestScene.vescene"),
                         "Windows package should contain scene asset");
        passed &= Expect(ve::FileSystem::IsFile(contentRoot / "Assets/Samples/Materials/TestMaterial.vematerial"),
                         "Windows package should contain material asset");
        passed &= Expect(ve::FileSystem::IsFile(contentRoot / "Assets/Samples/Models/TestQuad.obj.veasset"),
                         "Windows package should contain source metadata");
        passed &= Expect(!ve::FileSystem::IsFile(contentRoot / "Assets/Samples/Models/TestQuad.obj"),
                         "Windows package should not include source model payloads");
        passed &= Expect(ve::FileSystem::IsFile(contentRoot / "Generated/Assets/ImportCache" / SourceGuid /
                                                    "TestQuad.vemesh"),
                         "Windows package should contain generated mesh artifact");
        passed &= Expect(ve::FileSystem::IsDirectory(contentRoot / "Generated/Shaders/Windows/D3D11"),
                         "Windows package should contain D3D11 shader root");
        passed &= Expect(ve::FileSystem::IsDirectory(contentRoot / "Generated/Shaders/Windows/D3D12"),
                         "Windows package should contain D3D12 shader root");

        ve::Result<std::string> manifest = ve::FileSystem::ReadTextFile(contentRoot / "AssetManifest.veassetmanifest");
        passed &= ExpectOk(manifest, "Windows package manifest should be readable");
        if (manifest)
        {
            passed &= ExpectManifestIsPrettyPrinted(manifest.GetValue());

            object manifestObject;
            if (ParseManifest(manifest.GetValue(), manifestObject))
            {
                passed &= Expect(ReadString(manifestObject, "format") == "VEngine.AssetManifest",
                                 "Manifest should declare the VEngine asset manifest format");
                passed &= Expect(ReadBool(manifestObject, "readOnlyContent", false),
                                 "Manifest should mark Content as read-only");

                const value* shaderRoots = FindMember(manifestObject, "shaderRoots");
                passed &= Expect(shaderRoots != nullptr && shaderRoots->is_array(),
                                 "Manifest should contain shaderRoots");
                if (shaderRoots != nullptr && shaderRoots->is_array())
                {
                    passed &= Expect(ArrayContainsString(shaderRoots->as_array(), "Generated/Shaders/Windows/D3D11"),
                                     "Manifest should list the D3D11 shader root");
                    passed &= Expect(ArrayContainsString(shaderRoots->as_array(), "Generated/Shaders/Windows/D3D12"),
                                     "Manifest should list the D3D12 shader root");
                }
            }
            else
            {
                passed = false;
            }
        }

        return passed;
    }

    bool TestIosPackageLayout()
    {
        bool passed = true;

        ve::PackageRequest request;
        request.projectRoot = GetProjectRoot();
        request.outputRoot = GetProjectRoot() / "Generated/Build/iOS/Debug";
        request.platform = ve::PackagePlatform::iOS;
        request.configuration = ve::PackageConfiguration::Debug;
        request.includeRuntimeBinaries = false;

        ve::Result<ve::PackageResult> result = ve::StagePackage(request);
        passed &= ExpectOk(result, "iOS package should stage");
        if (!result)
        {
            return false;
        }

        passed &= Expect(result.GetValue().packageRoot.GetFilename() == "VEngineIOSPlayer.app",
                         "iOS package root should be an app bundle staging directory");
        passed &= Expect(ve::FileSystem::IsFile(result.GetValue().contentRoot / ".veproject"),
                         "iOS bundle Content should contain .veproject");
        passed &= Expect(ve::FileSystem::IsFile(result.GetValue().contentRoot / "AssetManifest.veassetmanifest"),
                         "iOS bundle Content should contain asset manifest");
        passed &= Expect(ve::FileSystem::IsDirectory(result.GetValue().contentRoot / "Generated/Shaders/iOS/Metal"),
                         "iOS bundle Content should contain Metal shader root");

        ve::Result<std::string> manifest =
            ve::FileSystem::ReadTextFile(result.GetValue().contentRoot / "AssetManifest.veassetmanifest");
        passed &= ExpectOk(manifest, "iOS package manifest should be readable");
        if (manifest)
        {
            passed &= ExpectManifestIsPrettyPrinted(manifest.GetValue());

            object manifestObject;
            if (ParseManifest(manifest.GetValue(), manifestObject))
            {
                passed &= Expect(ReadString(manifestObject, "platform") == "iOS",
                                 "iOS package manifest should record platform");

                const value* shaderRoots = FindMember(manifestObject, "shaderRoots");
                passed &= Expect(shaderRoots != nullptr && shaderRoots->is_array(),
                                 "Manifest should contain shaderRoots");
                if (shaderRoots != nullptr && shaderRoots->is_array())
                {
                    passed &= Expect(ArrayContainsString(shaderRoots->as_array(), "Generated/Shaders/iOS/Metal"),
                                     "Manifest should list the Metal shader root");
                }
            }
            else
            {
                passed = false;
            }
        }

        return passed;
    }
} // namespace

int main()
{
    RemoveTestRoot();

    bool passed = true;
    passed &= WritePackageProject();
    passed &= TestWindowsPackageLayout();
    passed &= TestIosPackageLayout();

    RemoveTestRoot();

    if (!passed)
    {
        return 1;
    }

    std::cout << "VEnginePackageTests passed" << '\n';
    return 0;
}
