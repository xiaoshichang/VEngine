#include "Editor/macOS/MacEditorProjectPacker.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

namespace ve::editor
{
    namespace
    {
        constexpr const char* MacPlatformName = "Mac";
        constexpr const char* MacPlayerExecutableName = "VEngineMacPlayer";
        constexpr const char* PackageInfoFilename = "PackageInfo.json";
        constexpr const char* MacDotNetRuntimeRelativePath = "DotNet/osx-arm64/10.0.9";
        constexpr const char* MacManagedHostRelativePath = "Managed/VEngine.ScriptHost";

#if defined(VE_CMAKE_BINARY_DIR)
        constexpr const char* CMakeBinaryDirectory = VE_CMAKE_BINARY_DIR;
#else
        constexpr const char* CMakeBinaryDirectory = "";
#endif

#if defined(VE_CMAKE_BUILD_CONFIG)
        constexpr const char* CMakeBuildConfig = VE_CMAKE_BUILD_CONFIG;
#else
        constexpr const char* CMakeBuildConfig = "Debug";
#endif

        [[nodiscard]] std::string MakeErrorMessage(const char* operation, const Path& path, const std::error_code& errorCode)
        {
            return std::string(operation) + " failed for '" + path.GetString() + "': " + errorCode.message();
        }

        [[nodiscard]] std::string EscapeXml(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            for (const char value : text)
            {
                switch (value)
                {
                case '&':
                    result += "&amp;";
                    break;
                case '<':
                    result += "&lt;";
                    break;
                case '>':
                    result += "&gt;";
                    break;
                case '"':
                    result += "&quot;";
                    break;
                case '\'':
                    result += "&apos;";
                    break;
                default:
                    result.push_back(value);
                    break;
                }
            }

            return result;
        }

        [[nodiscard]] std::string SanitizeBundleIdentifierSegment(std::string text)
        {
            std::string result;
            result.reserve(text.size());

            bool previousDash = false;
            for (char value : text)
            {
                const unsigned char character = static_cast<unsigned char>(value);
                if (std::isalnum(character) != 0)
                {
                    result.push_back(static_cast<char>(std::tolower(character)));
                    previousDash = false;
                    continue;
                }

                if (value == '-' || value == '_')
                {
                    if (!previousDash && !result.empty())
                    {
                        result.push_back('-');
                        previousDash = true;
                    }
                }
            }

            while (!result.empty() && result.back() == '-')
            {
                result.pop_back();
            }

            return result.empty() ? "vengine-project" : result;
        }
    } // namespace

    const char* EditorProjectPackerMac::GetPlatformName() const noexcept
    {
        return MacPlatformName;
    }

    std::string EditorProjectPackerMac::GetRunningStatusMessage() const
    {
        return "Packaging macOS app.";
    }

    std::string EditorProjectPackerMac::GetSucceededStatusMessage() const
    {
        return "macOS app package completed.";
    }

    void EditorProjectPackerMac::ConfigurePackagePaths()
    {
        const std::string packageDirectoryName = MakePackageDirectoryName(projectName_, timestamp_);
        outputRoot_ = buildRoot_ / MacPlatformName / packageDirectoryName;
        logDirectory_ = outputRoot_;
        logPath_ = logDirectory_ / ("Package_Mac_" + timestamp_ + ".log");
        appBundleRoot_ = outputRoot_ / (packageDirectoryName + ".app");
        appContentsRoot_ = appBundleRoot_ / "Contents";
        appResourcesRoot_ = appContentsRoot_ / "Resources";
        packageBinRoot_ = appContentsRoot_ / "MacOS";
        packageDataRoot_ = appContentsRoot_ / "Data";
        packageRuntimeLogRoot_ = appContentsRoot_ / "Logs";

        cmakeBinaryRoot_ = Path(CMakeBinaryDirectory);
        cmakeBuildConfig_ = CMakeBuildConfig;

        if (cmakeBinaryRoot_.IsEmpty())
        {
            const Path executableDirectory = FileSystem::GetExecutableDirectory();
            const Path buildConfigDirectory = executableDirectory.GetParentPath().GetParentPath().GetParentPath();
            cmakeBinaryRoot_ = buildConfigDirectory.GetParentPath();
        }

        if (cmakeBuildConfig_.empty())
        {
            cmakeBuildConfig_ = "Debug";
        }
    }

    void EditorProjectPackerMac::InitializeSteps()
    {
        steps_ = {
            PackageStepState{.name = "Prepare macOS package directories"},
            PackageStepState{.name = "Refresh asset database"},
            PackageStepState{.name = "Export asset manifest"},
            PackageStepState{.name = "Copy runtime assets"},
            PackageStepState{.name = "Copy managed scripts"},
            PackageStepState{.name = "Build macOS player"},
            PackageStepState{.name = "Copy macOS player"},
            PackageStepState{.name = "Copy macOS managed runtime"},
            PackageStepState{.name = "Write macOS Info.plist"},
            PackageStepState{.name = "Sign macOS app"},
            PackageStepState{.name = "Write package info"},
        };
    }

    ErrorCode EditorProjectPackerMac::RunStep(size_t stepIndex, Editor& editor)
    {
        switch (stepIndex)
        {
        case 0:
            return PrepareMacBundleDirectories();
        case 1:
            return RefreshAssetDatabase(editor);
        case 2:
            return ExportAssetManifest(editor);
        case 3:
            return CopyRuntimeAssets(editor);
        case 4:
            return CopyManagedScripts();
        case 5:
            return BuildMacPlayer();
        case 6:
            return CopyMacPlayerExecutable();
        case 7:
            return CopyMacPlayerManagedRuntime();
        case 8:
            return WriteMacInfoPlist();
        case 9:
            return SignMacAppBundle();
        case 10:
            return WriteMacPackageInfo();
        default:
            return ErrorCode::InvalidState;
        }
    }

    void EditorProjectPackerMac::ResetPlatformState()
    {
        appBundleRoot_ = Path();
        appContentsRoot_ = Path();
        appResourcesRoot_ = Path();
        cmakeBinaryRoot_ = Path();
        cmakeBuildConfig_.clear();
    }

    ErrorCode EditorProjectPackerMac::PrepareMacBundleDirectories()
    {
        ErrorCode result = CreateDirectory(appContentsRoot_);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = CreateDirectory(appResourcesRoot_);
        if (result != ErrorCode::None)
        {
            return result;
        }

        return PreparePackageDirectories();
    }

    ErrorCode EditorProjectPackerMac::BuildMacPlayer()
    {
        if (cmakeBinaryRoot_.IsEmpty() || !FileSystem::IsDirectory(cmakeBinaryRoot_))
        {
            LogError(ErrorCode::NotFound, "CMake binary root was not found: " + cmakeBinaryRoot_.GetString());
            return ErrorCode::NotFound;
        }

        LogLine("CMake binary root: " + cmakeBinaryRoot_.GetString());
        ErrorCode result = RunShellCommand(BuildCMakeBuildCommand());
        if (result != ErrorCode::None)
        {
            return result;
        }

        const Path playerOutputPath = GetMacPlayerBuildOutputPath();
        if (!FileSystem::IsFile(playerOutputPath))
        {
            LogError(ErrorCode::NotFound, "Built macOS player was not found: " + playerOutputPath.GetString());
            return ErrorCode::NotFound;
        }

        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerMac::CopyMacPlayerExecutable()
    {
        const Path playerSourcePath = GetMacPlayerBuildOutputPath();
        const Path playerDestinationPath = packageBinRoot_ / MacPlayerExecutableName;

        ErrorCode result = CopyFileWithDirectories(playerSourcePath, playerDestinationPath);
        if (result != ErrorCode::None)
        {
            return result;
        }

        std::error_code errorCode;
        std::filesystem::permissions(ToNativePath(playerDestinationPath),
                                     std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::add,
                                     errorCode);
        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Set executable permissions", playerDestinationPath, errorCode));
            return ErrorCode::IOError;
        }

        LogLine("Copied macOS player: " + playerSourcePath.GetString() + " -> " + playerDestinationPath.GetString());

        const std::filesystem::path buildOutputDirectory = ToNativePath(playerSourcePath.GetParentPath());
        errorCode.clear();
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(buildOutputDirectory, errorCode))
        {
            if (errorCode)
            {
                LogError(ErrorCode::IOError, MakeErrorMessage("List directory", playerSourcePath.GetParentPath(), errorCode));
                return ErrorCode::IOError;
            }

            const std::filesystem::path extension = entry.path().extension();
            if (entry.is_regular_file(errorCode) && extension == ".dylib")
            {
                const Path sourceLibrary(entry.path().generic_string());
                const Path destinationLibrary = packageBinRoot_ / entry.path().filename().generic_string();
                result = CopyFileWithDirectories(sourceLibrary, destinationLibrary);
                if (result != ErrorCode::None)
                {
                    return result;
                }

                LogLine("Copied macOS player dylib: " + sourceLibrary.GetString() + " -> " + destinationLibrary.GetString());
                continue;
            }

            errorCode.clear();
            if (entry.is_directory(errorCode) && extension == ".framework")
            {
                const Path sourceFramework(entry.path().generic_string());
                const Path destinationFramework = appContentsRoot_ / "Frameworks" / entry.path().filename().generic_string();
                result = CopyDirectory(sourceFramework, destinationFramework);
                if (result != ErrorCode::None)
                {
                    return result;
                }

                LogLine("Copied macOS player framework: " + sourceFramework.GetString() + " -> " + destinationFramework.GetString());
            }

            errorCode.clear();
        }

        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("List directory", playerSourcePath.GetParentPath(), errorCode));
            return ErrorCode::IOError;
        }

        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerMac::CopyMacPlayerManagedRuntime()
    {
        const Path editorExecutableDirectory = FileSystem::GetExecutableDirectory();
        const Path editorBundleContents = editorExecutableDirectory.GetParentPath();
        const Path scriptHostSource = editorBundleContents / "Resources" / MacManagedHostRelativePath;
        const Path scriptHostDestination = appResourcesRoot_ / MacManagedHostRelativePath;

        ErrorCode result = CopyDirectory(scriptHostSource, scriptHostDestination);
        if (result != ErrorCode::None)
        {
            return result;
        }

        const Path dotNetSource = editorBundleContents / "Resources" / MacDotNetRuntimeRelativePath;
        const Path dotNetDestination = appResourcesRoot_ / MacDotNetRuntimeRelativePath;
        if (!FileSystem::IsDirectory(dotNetSource))
        {
            LogError(ErrorCode::NotFound, "App-local macOS .NET runtime source was not found: " + dotNetSource.GetString());
            return ErrorCode::NotFound;
        }

        result = CopyDirectory(dotNetSource, dotNetDestination);
        if (result != ErrorCode::None)
        {
            return result;
        }

        LogLine("Copied macOS managed runtime files.");
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerMac::WriteMacInfoPlist()
    {
        const std::string bundleName = projectName_.empty() ? "VEngineProject" : projectName_;
        const std::string bundleIdentifier = "com.vengine.packaged." + SanitizeBundleIdentifierSegment(bundleName);

        std::ostringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"https://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               << "<plist version=\"1.0\">\n"
               << "<dict>\n"
               << "    <key>CFBundleDevelopmentRegion</key>\n"
               << "    <string>en</string>\n"
               << "    <key>CFBundleExecutable</key>\n"
               << "    <string>" << MacPlayerExecutableName << "</string>\n"
               << "    <key>CFBundleIdentifier</key>\n"
               << "    <string>" << EscapeXml(bundleIdentifier) << "</string>\n"
               << "    <key>CFBundleInfoDictionaryVersion</key>\n"
               << "    <string>6.0</string>\n"
               << "    <key>CFBundleName</key>\n"
               << "    <string>" << EscapeXml(bundleName) << "</string>\n"
               << "    <key>CFBundlePackageType</key>\n"
               << "    <string>APPL</string>\n"
               << "    <key>CFBundleShortVersionString</key>\n"
               << "    <string>0.1.0</string>\n"
               << "    <key>CFBundleVersion</key>\n"
               << "    <string>1</string>\n"
               << "</dict>\n"
               << "</plist>\n";

        const Path infoPlistPath = appContentsRoot_ / "Info.plist";
        const ErrorCode result = FileSystem::WriteTextFile(infoPlistPath, stream.str());
        if (result != ErrorCode::None)
        {
            LogError(result, "Failed to write Info.plist: " + infoPlistPath.GetString());
            return result;
        }

        LogLine("Wrote macOS Info.plist: " + infoPlistPath.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerMac::SignMacAppBundle()
    {
        return RunShellCommand(std::string("/usr/bin/codesign --force --deep --sign - ") + ShellQuote(appBundleRoot_.GetString()));
    }

    ErrorCode EditorProjectPackerMac::WriteMacPackageInfo()
    {
        return WritePackageInfo(PackageInfoDesc{
            .packageInfoPath = appResourcesRoot_ / PackageInfoFilename,
            .platform = MacPlatformName,
            .playerExecutable = std::string("Contents/MacOS/") + MacPlayerExecutableName,
            .dataRoot = "Contents/Data",
            .appBundle = appBundleRoot_.GetString(),
        });
    }

    ErrorCode EditorProjectPackerMac::RunShellCommand(const std::string& command)
    {
        LogLine("Running command: " + command);
        const int result = std::system(command.c_str());
        if (result != 0)
        {
            LogError(ErrorCode::PlatformError, "Command failed with exit code " + std::to_string(result) + ": " + command);
            return ErrorCode::PlatformError;
        }

        return ErrorCode::None;
    }

    std::string EditorProjectPackerMac::BuildCMakeBuildCommand() const
    {
        std::ostringstream stream;
        stream << "cmake"
               << " --build " << ShellQuote(cmakeBinaryRoot_.GetString()) << " --config " << ShellQuote(cmakeBuildConfig_) << " --target VEngineMacPlayer";
        return stream.str();
    }

    Path EditorProjectPackerMac::GetMacPlayerBuildOutputPath() const
    {
        return cmakeBinaryRoot_ / cmakeBuildConfig_ / MacPlayerExecutableName;
    }

    std::string EditorProjectPackerMac::ShellQuote(std::string_view text)
    {
        std::string result = "'";
        for (const char value : text)
        {
            if (value == '\'')
            {
                result += "'\\''";
            }
            else
            {
                result.push_back(value);
            }
        }

        result.push_back('\'');
        return result;
    }

    std::unique_ptr<EditorProjectPacker> CreateEditorProjectPackerForHostPlatform()
    {
        return std::make_unique<EditorProjectPackerMac>();
    }
} // namespace ve::editor
