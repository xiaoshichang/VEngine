#include "Engine/Runtime/Asset/AssetDatabase.h"
#include "Engine/Runtime/Core/Version.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Tools/AssetTool/ObjImporter.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct CommandLine
    {
        std::vector<std::string_view> args;
    };

    void PrintHelp()
    {
        const ve::BuildInfo buildInfo = ve::GetBuildInfo();
        std::cout << "VEngineAssetTool (" << buildInfo.projectName << " " << buildInfo.version << ")\n";
        std::cout << "Commands:\n";
        std::cout << "  scan --project <project-root>\n";
        std::cout << "  import --project <project-root> --source <source-path> [--force]\n";
        std::cout << "  reimport --project <project-root> --asset <guid-or-path> [--force]\n";
        std::cout << "  validate --project <project-root>\n";
    }

    [[nodiscard]] std::string_view FindOption(const CommandLine& commandLine, std::string_view option)
    {
        for (size_t index = 0; index + 1 < commandLine.args.size(); ++index)
        {
            if (commandLine.args[index] == option)
            {
                return commandLine.args[index + 1];
            }
        }

        return {};
    }

    [[nodiscard]] bool HasOption(const CommandLine& commandLine, std::string_view option)
    {
        for (std::string_view arg : commandLine.args)
        {
            if (arg == option)
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] bool OpenAssetDatabase(const CommandLine& commandLine, ve::AssetDatabase& assetDatabase)
    {
        const std::string_view project = FindOption(commandLine, "--project");
        if (project.empty())
        {
            std::cerr << "Missing required option: --project\n";
            return false;
        }

        const ve::ErrorCode result = assetDatabase.Open(ve::Path(project));
        if (result != ve::ErrorCode::None)
        {
            std::cerr << "Failed to open asset database: " << ve::ToString(result) << '\n';
            return false;
        }

        return true;
    }

    [[nodiscard]] int RunScan(const CommandLine& commandLine)
    {
        ve::AssetDatabase assetDatabase;
        if (!OpenAssetDatabase(commandLine, assetDatabase))
        {
            return 1;
        }

        std::cout << "Asset records: " << assetDatabase.GetRecords().size() << '\n';
        return 0;
    }

    [[nodiscard]] int RunValidate(const CommandLine& commandLine)
    {
        ve::AssetDatabase assetDatabase;
        if (!OpenAssetDatabase(commandLine, assetDatabase))
        {
            return 1;
        }

        const ve::ErrorCode result = assetDatabase.Validate();
        if (result != ve::ErrorCode::None)
        {
            std::cerr << "Asset validation failed: " << ve::ToString(result) << '\n';
            return 1;
        }

        std::cout << "Asset validation passed\n";
        return 0;
    }

    [[nodiscard]] bool IsSupportedImportSource(const ve::Path& sourcePath)
    {
        return sourcePath.GetExtension() == ".obj";
    }

    [[nodiscard]] int RunImportSource(ve::AssetDatabase& assetDatabase,
                                      const ve::Path& sourcePath,
                                      bool force)
    {
        if (!IsSupportedImportSource(sourcePath))
        {
            std::cerr << "Unsupported import source: " << sourcePath.GetString() << '\n';
            return 1;
        }

        ve::Result<ve::ObjImportResult> importResult = ve::ImportObjModel(assetDatabase, sourcePath, force);
        if (!importResult)
        {
            std::cerr << "Import failed: " << importResult.GetError().GetMessage() << '\n';
            return 1;
        }

        const ve::ObjImportResult& result = importResult.GetValue();
        std::cout << "Imported " << sourcePath.GetString() << '\n';
        std::cout << "  guid: " << result.guid.ToString() << '\n';
        std::cout << "  metadata: " << result.metadataPath.GetString() << '\n';
        std::cout << "  mesh: " << result.meshArtifactPath.GetString() << '\n';
        std::cout << "  vertices: " << result.vertexCount << '\n';
        return 0;
    }

    [[nodiscard]] int RunImport(const CommandLine& commandLine)
    {
        ve::AssetDatabase assetDatabase;
        if (!OpenAssetDatabase(commandLine, assetDatabase))
        {
            return 1;
        }

        const std::string_view source = FindOption(commandLine, "--source");
        if (source.empty())
        {
            std::cerr << "Missing required option: --source\n";
            return 1;
        }

        return RunImportSource(assetDatabase, ve::Path(source), HasOption(commandLine, "--force"));
    }

    [[nodiscard]] int RunReimport(const CommandLine& commandLine)
    {
        ve::AssetDatabase assetDatabase;
        if (!OpenAssetDatabase(commandLine, assetDatabase))
        {
            return 1;
        }

        const std::string_view asset = FindOption(commandLine, "--asset");
        if (asset.empty())
        {
            std::cerr << "Missing required option: --asset\n";
            return 1;
        }

        const ve::AssetRecord* record = nullptr;
        if (ve::Result<ve::AssetGuid> guid = ve::AssetGuid::Parse(asset))
        {
            record = assetDatabase.FindAsset(guid.GetValue());
        }

        if (record == nullptr)
        {
            record = assetDatabase.FindAssetByPath(ve::Path(asset));
        }

        if (record == nullptr || record->assetType != ve::AssetType::SourceModel)
        {
            std::cerr << "Source asset was not found: " << asset << '\n';
            return 1;
        }

        return RunImportSource(assetDatabase, record->source, HasOption(commandLine, "--force"));
    }
} // namespace

int main(int argc, char* argv[])
{
    ve::LoggingConfig loggingConfig = ve::MakeDefaultLoggingConfig();
    loggingConfig.enableFile = false;
    const ve::ErrorCode loggingResult = ve::InitializeLogging(loggingConfig);
    if (loggingResult != ve::ErrorCode::None)
    {
        std::cerr << "Failed to initialize logging: " << ve::ToString(loggingResult) << '\n';
        return 1;
    }

    CommandLine commandLine;
    for (int index = 1; index < argc; ++index)
    {
        commandLine.args.emplace_back(argv[index]);
    }

    int exitCode = 0;
    if (commandLine.args.empty() || commandLine.args[0] == "--help" || commandLine.args[0] == "help")
    {
        PrintHelp();
    }
    else if (commandLine.args[0] == "scan")
    {
        exitCode = RunScan(commandLine);
    }
    else if (commandLine.args[0] == "import")
    {
        exitCode = RunImport(commandLine);
    }
    else if (commandLine.args[0] == "reimport")
    {
        exitCode = RunReimport(commandLine);
    }
    else if (commandLine.args[0] == "validate")
    {
        exitCode = RunValidate(commandLine);
    }
    else
    {
        std::cerr << "Unknown command: " << commandLine.args[0] << '\n';
        PrintHelp();
        exitCode = 1;
    }

    ve::ShutdownLogging();
    return exitCode;
}
