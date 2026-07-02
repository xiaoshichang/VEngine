#include "Editor/Windows/WinEditorProjectPacker.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <memory>
#include <string>

namespace ve::editor
{
    namespace
    {
        constexpr const char* WindowsPlatformName = "Windows";
        constexpr const char* WindowsPlayerExecutableName = "VEngineWinPlayer.exe";
        constexpr const char* PackageInfoFilename = "PackageInfo.json";
        constexpr const char* WindowsDotNetRuntimeRelativePath = "DotNet/win-x64/10.0.9";
    } // namespace

    const char* EditorProjectPackerWin::GetPlatformName() const noexcept
    {
        return WindowsPlatformName;
    }

    std::string EditorProjectPackerWin::GetRunningStatusMessage() const
    {
        return "Packaging Windows build.";
    }

    std::string EditorProjectPackerWin::GetSucceededStatusMessage() const
    {
        return "Windows package completed.";
    }

    void EditorProjectPackerWin::ConfigurePackagePaths()
    {
        logPath_ = logDirectory_ / ("Package_Windows_" + timestamp_ + ".log");
        outputRoot_ = buildRoot_ / WindowsPlatformName / MakePackageDirectoryName(projectName_, timestamp_);
        packageBinRoot_ = outputRoot_ / "Bin";
        packageDataRoot_ = outputRoot_ / "Data";
        packageRuntimeLogRoot_ = outputRoot_ / "Logs";
    }

    void EditorProjectPackerWin::InitializeSteps()
    {
        steps_ = {
            PackageStepState{.name = "Prepare package directories"},
            PackageStepState{.name = "Refresh asset database"},
            PackageStepState{.name = "Export asset manifest"},
            PackageStepState{.name = "Copy runtime assets"},
            PackageStepState{.name = "Copy managed scripts"},
            PackageStepState{.name = "Copy Windows player"},
            PackageStepState{.name = "Copy Windows managed runtime"},
            PackageStepState{.name = "Write package info"},
        };
    }

    ErrorCode EditorProjectPackerWin::RunStep(size_t stepIndex, Editor& editor)
    {
        switch (stepIndex)
        {
        case 0:
            return PreparePackageDirectories();
        case 1:
            return RefreshAssetDatabase(editor);
        case 2:
            return ExportAssetManifest(editor);
        case 3:
            return CopyRuntimeAssets(editor);
        case 4:
            return CopyManagedScripts();
        case 5:
            return CopyWindowsPlayerExecutable();
        case 6:
            return CopyWindowsPlayerManagedRuntime();
        case 7:
            return WriteWindowsPackageInfo();
        default:
            return ErrorCode::InvalidState;
        }
    }

    ErrorCode EditorProjectPackerWin::CopyWindowsPlayerExecutable()
    {
        const Path playerSourcePath = FileSystem::GetExecutableDirectory() / WindowsPlayerExecutableName;
        const Path playerDestinationPath = packageBinRoot_ / WindowsPlayerExecutableName;

        if (!FileSystem::IsFile(playerSourcePath))
        {
            LogError(ErrorCode::NotFound, "Windows player executable was not found: " + playerSourcePath.GetString());
            return ErrorCode::NotFound;
        }

        const ErrorCode result = CopyFileWithDirectories(playerSourcePath, playerDestinationPath);
        if (result != ErrorCode::None)
        {
            return result;
        }

        LogLine("Copied Windows player: " + playerSourcePath.GetString() + " -> " + playerDestinationPath.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerWin::CopyWindowsPlayerManagedRuntime()
    {
        const Path executableDirectory = FileSystem::GetExecutableDirectory();
        const Path scriptHostSource = executableDirectory / "Managed" / "VEngine.ScriptHost";
        const Path scriptHostDestination = packageBinRoot_ / "Managed" / "VEngine.ScriptHost";

        ErrorCode result = CopyDirectory(scriptHostSource, scriptHostDestination);
        if (result != ErrorCode::None)
        {
            return result;
        }

        const Path dotNetSource = executableDirectory / WindowsDotNetRuntimeRelativePath;
        const Path dotNetDestination = packageBinRoot_ / WindowsDotNetRuntimeRelativePath;
        if (!FileSystem::IsDirectory(dotNetSource))
        {
            LogError(ErrorCode::NotFound, "App-local .NET runtime source was not found: " + dotNetSource.GetString());
            return ErrorCode::NotFound;
        }

        result = CopyDirectory(dotNetSource, dotNetDestination);
        if (result != ErrorCode::None)
        {
            return result;
        }

        LogLine("Copied app-local .NET runtime: " + dotNetSource.GetString() + " -> " + dotNetDestination.GetString());
        LogLine("Copied Windows player managed runtime files.");
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerWin::WriteWindowsPackageInfo()
    {
        return WritePackageInfo(PackageInfoDesc{
            .packageInfoPath = outputRoot_ / PackageInfoFilename,
            .platform = WindowsPlatformName,
            .playerExecutable = std::string("Bin/") + WindowsPlayerExecutableName,
            .dataRoot = "Data",
        });
    }

    std::unique_ptr<EditorProjectPacker> CreateEditorProjectPackerForHostPlatform()
    {
        return std::make_unique<EditorProjectPackerWin>();
    }
} // namespace ve::editor
