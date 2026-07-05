#include "Editor/macOS/IOSEditorProjectPacker.h"

#include "Editor/Core/EditorScriptProjectGenerator.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <algorithm>
#include <array>
#include <boost/json.hpp>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <sys/wait.h>
#include <vector>

namespace ve::editor
{
    namespace
    {
        constexpr const char* IOSPlatformName = "iOS";
        constexpr const char* PackageInfoFilename = "PackageInfo.json";
        constexpr const char* IOSPlayerTargetName = "VEngineIOSPlayer";

#if defined(VE_PROJECT_SOURCE_DIR)
        constexpr const char* EngineSourceDirectory = VE_PROJECT_SOURCE_DIR;
#else
        constexpr const char* EngineSourceDirectory = "";
#endif

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

        [[nodiscard]] bool IsDefaultIOSDevelopmentTeamOption(std::string_view text) noexcept
        {
            return text.empty() || text == "Auto";
        }

        [[nodiscard]] bool IsDefaultIOSProvisioningProfileOption(std::string_view text) noexcept
        {
            return text.empty() || text == "Automatic";
        }

        [[nodiscard]] std::string Trim(std::string text)
        {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
            {
                text.pop_back();
            }

            size_t first = 0;
            while (first < text.size() && (text[first] == ' ' || text[first] == '\t' || text[first] == '\n' || text[first] == '\r'))
            {
                ++first;
            }

            return first == 0 ? text : text.substr(first);
        }

        [[nodiscard]] bool IsValidIOSDeploymentTargetText(std::string_view text) noexcept
        {
            if (text.empty() || text.front() == '.' || text.back() == '.')
            {
                return false;
            }

            bool hasDot = false;
            bool previousDot = false;
            for (const char value : text)
            {
                if (value == '.')
                {
                    if (previousDot)
                    {
                        return false;
                    }

                    hasDot = true;
                    previousDot = true;
                    continue;
                }

                if (value < '0' || value > '9')
                {
                    return false;
                }

                previousDot = false;
            }

            return hasDot;
        }

        [[nodiscard]] std::string ReadIOSDeploymentTargetFromSetup()
        {
            if (std::string(EngineSourceDirectory).empty())
            {
                return {};
            }

            const Path settingsPath = Path(std::string(EngineSourceDirectory)) / "ThirdParty" / "IOSSettings.json";
            Result<std::string> textResult = FileSystem::ReadTextFile(settingsPath);
            if (!textResult)
            {
                return {};
            }

            Result<boost::json::value> jsonResult = JsonUtils::Parse(textResult.GetValue());
            if (!jsonResult || !jsonResult.GetValue().is_object())
            {
                return {};
            }

            const boost::json::value* deploymentTargetValue = jsonResult.GetValue().as_object().if_contains("deploymentTarget");
            if (deploymentTargetValue == nullptr || !deploymentTargetValue->is_string())
            {
                return {};
            }

            std::string deploymentTarget(deploymentTargetValue->as_string());
            return IsValidIOSDeploymentTargetText(deploymentTarget) ? deploymentTarget : std::string{};
        }

        [[nodiscard]] std::string DetectIOSDeploymentTargetFromXcode()
        {
            FILE* pipe = popen("xcrun --sdk iphoneos --show-sdk-version 2>/dev/null", "r");
            if (pipe == nullptr)
            {
                return {};
            }

            std::array<char, 128> buffer{};
            std::string text;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            {
                text += buffer.data();
            }

            pclose(pipe);
            text = Trim(std::move(text));
            return IsValidIOSDeploymentTargetText(text) ? text : std::string{};
        }

        [[nodiscard]] std::string ResolveIOSDeploymentTarget()
        {
            std::string deploymentTarget = ReadIOSDeploymentTargetFromSetup();
            if (!deploymentTarget.empty())
            {
                return deploymentTarget;
            }

            deploymentTarget = DetectIOSDeploymentTargetFromXcode();
            if (!deploymentTarget.empty())
            {
                return deploymentTarget;
            }

            return "16.4";
        }

        struct ParsedNuGetVersion
        {
            std::vector<unsigned long long> numericParts;
            std::string suffix;
            bool hasSuffix = false;
        };

        [[nodiscard]] ParsedNuGetVersion ParseNuGetVersion(std::string_view text)
        {
            ParsedNuGetVersion result;

            const size_t metadataPosition = text.find('+');
            const std::string_view versionWithoutMetadata =
                metadataPosition == std::string_view::npos ? text : text.substr(0, metadataPosition);
            const size_t suffixPosition = versionWithoutMetadata.find('-');
            const std::string_view numericText = suffixPosition == std::string_view::npos
                ? versionWithoutMetadata
                : versionWithoutMetadata.substr(0, suffixPosition);

            if (suffixPosition != std::string_view::npos)
            {
                result.hasSuffix = true;
                result.suffix = std::string(versionWithoutMetadata.substr(suffixPosition + 1));
            }

            size_t segmentStart = 0;
            while (segmentStart <= numericText.size())
            {
                const size_t segmentEnd = numericText.find('.', segmentStart);
                const std::string_view segment = segmentEnd == std::string_view::npos
                    ? numericText.substr(segmentStart)
                    : numericText.substr(segmentStart, segmentEnd - segmentStart);

                unsigned long long value = 0;
                for (const char character : segment)
                {
                    if (std::isdigit(static_cast<unsigned char>(character)) == 0)
                    {
                        value = 0;
                        break;
                    }

                    value = value * 10ULL + static_cast<unsigned long long>(character - '0');
                }

                result.numericParts.push_back(value);

                if (segmentEnd == std::string_view::npos)
                {
                    break;
                }

                segmentStart = segmentEnd + 1;
            }

            if (result.numericParts.empty())
            {
                result.numericParts.push_back(0);
            }

            return result;
        }

        [[nodiscard]] int CompareNuGetVersionNames(std::string_view left, std::string_view right)
        {
            const ParsedNuGetVersion leftVersion = ParseNuGetVersion(left);
            const ParsedNuGetVersion rightVersion = ParseNuGetVersion(right);
            const size_t numericPartCount = std::max(leftVersion.numericParts.size(), rightVersion.numericParts.size());

            for (size_t index = 0; index < numericPartCount; ++index)
            {
                const unsigned long long leftPart = index < leftVersion.numericParts.size() ? leftVersion.numericParts[index] : 0ULL;
                const unsigned long long rightPart = index < rightVersion.numericParts.size() ? rightVersion.numericParts[index] : 0ULL;
                if (leftPart < rightPart)
                {
                    return -1;
                }

                if (leftPart > rightPart)
                {
                    return 1;
                }
            }

            if (leftVersion.hasSuffix != rightVersion.hasSuffix)
            {
                return leftVersion.hasSuffix ? -1 : 1;
            }

            if (leftVersion.suffix < rightVersion.suffix)
            {
                return -1;
            }

            if (leftVersion.suffix > rightVersion.suffix)
            {
                return 1;
            }

            return 0;
        }
    } // namespace

    const char* EditorProjectPackerIOS::GetPlatformName() const noexcept
    {
        return IOSPlatformName;
    }

    std::string EditorProjectPackerIOS::GetRunningStatusMessage() const
    {
        return "Exporting iOS Xcode project.";
    }

    std::string EditorProjectPackerIOS::GetSucceededStatusMessage() const
    {
        return "iOS Xcode project exported.";
    }

    void EditorProjectPackerIOS::ConfigurePackagePaths()
    {
        const std::string packageDirectoryName = MakePackageDirectoryName(projectName_, timestamp_);
        outputRoot_ = buildRoot_ / IOSPlatformName / packageDirectoryName;
        logDirectory_ = outputRoot_;
        logPath_ = logDirectory_ / ("Package_iOS_" + timestamp_ + ".log");
        stagingRoot_ = outputRoot_ / "Staging";
        packageDataRoot_ = stagingRoot_ / "Data";
        packageBinRoot_ = stagingRoot_ / "Bin";
        packageRuntimeLogRoot_ = Path();
        xcodeBinaryRoot_ = outputRoot_ / "Xcode";
        archivePath_ = outputRoot_ / (packageDirectoryName + ".xcarchive");
        ipaExportRoot_ = outputRoot_ / "Export";
        exportOptionsPlistPath_ = outputRoot_ / "ExportOptions.plist";
        nativeAOTOutputRoot_ = outputRoot_ / "NativeAOT";
        bundleIdentifier_ = buildSettings_.ios.bundleIdentifier.empty() ? "com.vengine.packaged." + SanitizeBundleIdentifierSegment(projectName_)
                                                                         : buildSettings_.ios.bundleIdentifier;
        iosSDK_ = buildSettings_.ios.sdk.empty() ? "iphoneos" : buildSettings_.ios.sdk;
        buildConfiguration_ = "Release";
        developmentTeam_ = IsDefaultIOSDevelopmentTeamOption(buildSettings_.ios.developmentTeam) ? std::string{} : buildSettings_.ios.developmentTeam;
        codeSignStyle_ = buildSettings_.ios.codeSignStyle.empty() ? "Automatic" : NormalizeCodeSignStyle(buildSettings_.ios.codeSignStyle);
        provisioningProfileSpecifier_ = IsDefaultIOSProvisioningProfileOption(buildSettings_.ios.provisioningProfileSpecifier)
            ? std::string{}
            : buildSettings_.ios.provisioningProfileSpecifier;
        codeSignIdentity_ = buildSettings_.ios.codeSignIdentity.empty() ? "Apple Development" : buildSettings_.ios.codeSignIdentity;
        deploymentTarget_ = ResolveIOSDeploymentTarget();
        exportMethod_ = buildSettings_.ios.exportMethod.empty() ? "development" : buildSettings_.ios.exportMethod;
        orientation_ = buildSettings_.ios.orientation.empty() ? "Landscape" : buildSettings_.ios.orientation;
    }

    void EditorProjectPackerIOS::InitializeSteps()
    {
        steps_ = {
            PackageStepState{.name = "Validate iOS packaging environment"},
            PackageStepState{.name = "Prepare iOS package directories"},
            PackageStepState{.name = "Refresh asset database"},
            PackageStepState{.name = "Export asset manifest"},
            PackageStepState{.name = "Copy runtime assets"},
            PackageStepState{.name = "Publish iOS NativeAOT scripts"},
            PackageStepState{.name = "Configure iOS Xcode project"},
            PackageStepState{.name = "Write package info"},
        };
    }

    ErrorCode EditorProjectPackerIOS::RunStep(size_t stepIndex, Editor& editor)
    {
        switch (stepIndex)
        {
        case 0:
            return ValidateIOSPackagingEnvironment();
        case 1:
            return PrepareIOSPackageDirectories();
        case 2:
            return RefreshAssetDatabase(editor);
        case 3:
            return ExportAssetManifest(editor);
        case 4:
            return CopyRuntimeAssets(editor);
        case 5:
            return PublishIOSNativeAOTScripts();
        case 6:
            return ConfigureIOSXcodeProject();
        case 7:
            return WriteIOSPackageInfo();
        default:
            return ErrorCode::InvalidState;
        }
    }

    void EditorProjectPackerIOS::ResetPlatformState()
    {
        stagingRoot_ = Path();
        xcodeBinaryRoot_ = Path();
        archivePath_ = Path();
        ipaExportRoot_ = Path();
        exportOptionsPlistPath_ = Path();
        nativeAOTOutputRoot_ = Path();
        nativeAOTLibraryPath_ = Path();
        nativeAOTRuntimeNativeRoot_ = Path();
        bundleIdentifier_.clear();
        iosSDK_.clear();
        buildConfiguration_.clear();
        developmentTeam_.clear();
        codeSignStyle_.clear();
        provisioningProfileSpecifier_.clear();
        codeSignIdentity_.clear();
        deploymentTarget_.clear();
        exportMethod_.clear();
        orientation_.clear();
    }

    ErrorCode EditorProjectPackerIOS::ValidateIOSPackagingEnvironment()
    {
        if (std::string(EngineSourceDirectory).empty())
        {
            LogError(ErrorCode::InvalidState, "VE_PROJECT_SOURCE_DIR is not available in the Editor build.");
            return ErrorCode::InvalidState;
        }

        if (!IsValidBundleIdentifier(bundleIdentifier_))
        {
            LogError(ErrorCode::InvalidArgument,
                     "Invalid iOS bundle identifier: " + bundleIdentifier_ +
                         ". Use reverse-DNS segments containing only letters, numbers, and hyphens, or set VE_IOS_BUNDLE_IDENTIFIER.");
            return ErrorCode::InvalidArgument;
        }

        if (!IsValidIOSSDK(iosSDK_))
        {
            LogError(ErrorCode::InvalidArgument, "Invalid iOS SDK: " + iosSDK_ + ". Use iphoneos or iphonesimulator.");
            return ErrorCode::InvalidArgument;
        }

        if (!IsValidCodeSignStyle(codeSignStyle_))
        {
            LogError(ErrorCode::InvalidArgument,
                     "Invalid iOS code sign style: " + codeSignStyle_ + ". Use VE_IOS_CODE_SIGN_STYLE=Automatic or VE_IOS_CODE_SIGN_STYLE=Manual.");
            return ErrorCode::InvalidArgument;
        }

        if (!IsSimulatorBuild() && codeSignStyle_ == "Manual" && provisioningProfileSpecifier_.empty())
        {
            LogError(ErrorCode::InvalidArgument,
                     "Manual iOS code signing requires VE_IOS_PROVISIONING_PROFILE_SPECIFIER so Xcode archive/export can select a provisioning profile.");
            return ErrorCode::InvalidArgument;
        }

        if (!IsValidExportMethod(exportMethod_))
        {
            LogError(ErrorCode::InvalidArgument,
                     "Invalid iOS export method: " + exportMethod_ +
                         ". Use VE_IOS_EXPORT_METHOD=development, ad-hoc, app-store, or enterprise.");
            return ErrorCode::InvalidArgument;
        }

        if (!IsValidDeploymentTarget(deploymentTarget_))
        {
            LogError(ErrorCode::InvalidArgument,
                     "Invalid iOS deployment target: " + deploymentTarget_ + ". Use a numeric version such as VE_IOS_DEPLOYMENT_TARGET=16.4.");
            return ErrorCode::InvalidArgument;
        }

        if (!IsValidOrientation(orientation_))
        {
            LogError(ErrorCode::InvalidArgument, "Invalid iOS orientation: " + orientation_ + ". Use Landscape, Portrait, or Adaptive.");
            return ErrorCode::InvalidArgument;
        }

        if (orientation_ == "Adaptive")
        {
            LogError(ErrorCode::Unsupported, "Adaptive iOS orientation is not supported yet. Use Landscape or Portrait.");
            return ErrorCode::Unsupported;
        }

        ErrorCode result = RunPreflightCommand("cmake --version >/dev/null", "CMake was not found. Install CMake or make it available on PATH before packaging for iOS.");
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = RunPreflightCommand("xcodebuild -version >/dev/null",
                                     "xcodebuild was not found. Install Xcode and select it with xcode-select before packaging for iOS.");
        if (result != ErrorCode::None)
        {
            return result;
        }

        const Path projectFile = GetGeneratedScriptProjectPath();
        if (!FileSystem::IsFile(projectFile))
        {
            LogLine("No generated script project found. Skipping dotnet preflight for iOS NativeAOT scripts: " + projectFile.GetString());
            return ErrorCode::None;
        }

        result = RunPreflightCommand("dotnet --version >/dev/null",
                                     "dotnet was not found. Install the .NET SDK required for iOS NativeAOT script publish before packaging for iOS.");
        if (result != ErrorCode::None)
        {
            return result;
        }

        const Path scriptHostProjectPath = GetScriptHostProjectPath();
        if (!FileSystem::IsFile(scriptHostProjectPath))
        {
            LogError(ErrorCode::NotFound, "VEngine.ScriptHost project was not found for iOS NativeAOT publish: " + scriptHostProjectPath.GetString());
            return ErrorCode::NotFound;
        }

        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerIOS::PrepareIOSPackageDirectories()
    {
        ErrorCode result = CreateDirectory(stagingRoot_);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = CreateDirectory(nativeAOTOutputRoot_);
        if (result != ErrorCode::None)
        {
            return result;
        }

        return PreparePackageDirectories();
    }

    ErrorCode EditorProjectPackerIOS::PublishIOSNativeAOTScripts()
    {
        const Path projectFile = GetGeneratedScriptProjectPath();
        if (!FileSystem::IsFile(projectFile))
        {
            LogLine("No generated script project found. Skipping iOS NativeAOT script publish: " + projectFile.GetString());
            return ErrorCode::None;
        }

        const Path scriptHostProjectPath = GetScriptHostProjectPath();
        if (!FileSystem::IsFile(scriptHostProjectPath))
        {
            LogError(ErrorCode::NotFound, "VEngine.ScriptHost project was not found for iOS NativeAOT publish: " + scriptHostProjectPath.GetString());
            return ErrorCode::NotFound;
        }

        ErrorCode result = RunShellCommand(BuildNativeAOTPublishCommand());
        if (result != ErrorCode::None)
        {
            return result;
        }

        nativeAOTLibraryPath_ = FindPublishedNativeAOTLibrary();
        if (nativeAOTLibraryPath_.IsEmpty())
        {
            LogError(ErrorCode::NotFound, "iOS NativeAOT publish did not produce a static library under: " + nativeAOTOutputRoot_.GetString());
            return ErrorCode::NotFound;
        }

        LogLine("Published iOS NativeAOT script library: " + nativeAOTLibraryPath_.GetString());

        nativeAOTRuntimeNativeRoot_ = FindNativeAOTRuntimeNativeDirectory();
        if (nativeAOTRuntimeNativeRoot_.IsEmpty())
        {
            LogError(ErrorCode::NotFound,
                     "iOS NativeAOT runtime native library directory was not found. Set VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR if NuGet packages are not under the default cache.");
            return ErrorCode::NotFound;
        }

        LogLine("Using iOS NativeAOT runtime native libraries: " + nativeAOTRuntimeNativeRoot_.GetString());
        return WriteIOSNativeAOTScriptManifest();
    }

    ErrorCode EditorProjectPackerIOS::WriteIOSNativeAOTScriptManifest()
    {
        const Path destinationRoot = packageDataRoot_ / "Scripts";
        ErrorCode result = CreateDirectory(destinationRoot);
        if (result != ErrorCode::None)
        {
            return result;
        }

        boost::json::object root;
        root["schemaVersion"] = 1;
        root["assemblyPath"] = "Scripts/NativeAOT";
        root["scriptTypes"] = boost::json::array();

        const Path manifestPath = destinationRoot / "ScriptAssembly.json";
        result = FileSystem::WriteTextFile(manifestPath, JsonUtils::SerializePretty(root));
        if (result != ErrorCode::None)
        {
            LogError(result, "Failed to write iOS NativeAOT script manifest: " + manifestPath.GetString());
            return result;
        }

        LogLine("Wrote iOS NativeAOT script manifest: " + manifestPath.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerIOS::ConfigureIOSXcodeProject()
    {
        if (std::string(EngineSourceDirectory).empty())
        {
            LogError(ErrorCode::InvalidState, "VE_PROJECT_SOURCE_DIR is not available in the Editor build.");
            return ErrorCode::InvalidState;
        }

        return RunShellCommand(BuildCMakeConfigureCommand());
    }

    ErrorCode EditorProjectPackerIOS::ArchiveIOSPlayer()
    {
        if (IsSimulatorBuild())
        {
            return RunShellCommand(BuildXcodeSimulatorBuildCommand());
        }

        return RunShellCommand(BuildXcodeArchiveCommand());
    }

    ErrorCode EditorProjectPackerIOS::ExportIOSArchive()
    {
        if (IsSimulatorBuild())
        {
            return PrepareIOSSimulatorPackage();
        }

        ErrorCode result = WriteIOSExportOptionsPlist();
        if (result != ErrorCode::None)
        {
            return result;
        }

        return RunShellCommand(BuildXcodeExportCommand());
    }

    ErrorCode EditorProjectPackerIOS::PrepareIOSSimulatorPackage()
    {
        const Path simulatorAppSource = GetIOSSimulatorAppBundlePath();
        if (!FileSystem::IsDirectory(simulatorAppSource))
        {
            LogError(ErrorCode::NotFound, "Built iOS simulator app was not found: " + simulatorAppSource.GetString());
            return ErrorCode::NotFound;
        }

        const Path simulatorAppDestination = outputRoot_ / (MakePackageDirectoryName(projectName_, timestamp_) + ".app");
        ErrorCode result = CopyDirectory(simulatorAppSource, simulatorAppDestination);
        if (result != ErrorCode::None)
        {
            return result;
        }

        LogLine("Copied iOS simulator app: " + simulatorAppSource.GetString() + " -> " + simulatorAppDestination.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerIOS::WriteIOSExportOptionsPlist()
    {
        std::ostringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"https://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               << "<plist version=\"1.0\">\n"
               << "<dict>\n"
               << "    <key>method</key>\n"
               << "    <string>" << EscapeXml(exportMethod_) << "</string>\n"
               << "    <key>signingStyle</key>\n"
               << "    <string>" << EscapeXml(GetExportSigningStyle()) << "</string>\n";

        if (!developmentTeam_.empty())
        {
            stream << "    <key>teamID</key>\n"
                   << "    <string>" << EscapeXml(developmentTeam_) << "</string>\n";
        }

        if (!provisioningProfileSpecifier_.empty())
        {
            stream << "    <key>provisioningProfiles</key>\n"
                   << "    <dict>\n"
                   << "        <key>" << EscapeXml(bundleIdentifier_) << "</key>\n"
                   << "        <string>" << EscapeXml(provisioningProfileSpecifier_) << "</string>\n"
                   << "    </dict>\n";
        }

        if (!codeSignIdentity_.empty())
        {
            stream << "    <key>signingCertificate</key>\n"
                   << "    <string>" << EscapeXml(codeSignIdentity_) << "</string>\n";
        }

        stream << "</dict>\n"
               << "</plist>\n";

        const ErrorCode result = FileSystem::WriteTextFile(exportOptionsPlistPath_, stream.str());
        if (result != ErrorCode::None)
        {
            LogError(result, "Failed to write iOS export options plist: " + exportOptionsPlistPath_.GetString());
            return result;
        }

        LogLine("Wrote iOS export options plist: " + exportOptionsPlistPath_.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerIOS::WriteIOSPackageInfo()
    {
        const Path xcodeProjectPath = xcodeBinaryRoot_ / "VEngine.xcodeproj";
        return WritePackageInfo(PackageInfoDesc{
            .packageInfoPath = outputRoot_ / PackageInfoFilename,
            .platform = IOSPlatformName,
            .playerExecutable = std::string(IOSPlayerTargetName),
            .dataRoot = packageDataRoot_.GetString(),
            .appBundle = {},
            .xcodeProject = xcodeProjectPath.GetString(),
        });
    }

    ErrorCode EditorProjectPackerIOS::RunShellCommand(const std::string& command)
    {
        LogLine("Running command: " + command);
        const std::string loggedCommand = command + " 2>&1";
        FILE* pipe = popen(loggedCommand.c_str(), "r");
        if (pipe == nullptr)
        {
            LogError(ErrorCode::PlatformError, "Failed to launch command: " + command);
            return ErrorCode::PlatformError;
        }

        std::array<char, 4096> buffer{};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        {
            std::string line(buffer.data());
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            {
                line.pop_back();
            }

            LogLine(line);
        }

        const int result = pclose(pipe);
        if (result != 0)
        {
            const int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : result;
            LogError(ErrorCode::PlatformError, "Command failed with exit code " + std::to_string(exitCode) + ": " + command);
            return ErrorCode::PlatformError;
        }

        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackerIOS::RunPreflightCommand(const std::string& command, const std::string& failureMessage)
    {
        LogLine("Checking iOS packaging prerequisite: " + command);
        const int result = std::system(command.c_str());
        if (result != 0)
        {
            LogError(ErrorCode::PlatformError, failureMessage);
            return ErrorCode::PlatformError;
        }

        return ErrorCode::None;
    }

    std::string EditorProjectPackerIOS::BuildCMakeConfigureCommand() const
    {
        std::ostringstream stream;
        stream << "cmake"
               << " -S " << ShellQuote(EngineSourceDirectory)
               << " -B " << ShellQuote(xcodeBinaryRoot_.GetString())
               << " -G Xcode"
               << " -DCMAKE_TOOLCHAIN_FILE=" << ShellQuote(std::string(EngineSourceDirectory) + "/CMake/Toolchains/IOS.cmake")
               << " -DVE_IOS_SDK=" << ShellQuote(iosSDK_)
               << " -DVE_IOS_ARCHITECTURES=arm64"
               << " -DVE_BUILD_PLAYER=ON"
               << " -DVE_BUILD_EDITOR=OFF"
               << " -DVE_BUILD_TESTS=OFF"
               << " -DVE_BUILD_TOOLS=OFF"
               << " -DVE_BUILD_MAC_PLAYER=OFF"
               << " -DVE_BUILD_IOS_PLAYER=ON"
               << " -DVE_ENABLE_D3D11=OFF"
               << " -DVE_ENABLE_D3D12=OFF"
               << " -DVE_ENABLE_METAL=ON"
               << " -DVE_IOS_BUNDLE_IDENTIFIER=" << ShellQuote(bundleIdentifier_)
               << " -DVE_IOS_CODE_SIGN_STYLE=" << ShellQuote(codeSignStyle_)
               << " -DVE_IOS_DEPLOYMENT_TARGET=" << ShellQuote(deploymentTarget_)
               << " -DVE_IOS_ORIENTATION=" << ShellQuote(orientation_)
               << " -DCMAKE_OSX_DEPLOYMENT_TARGET=" << ShellQuote(deploymentTarget_)
               << " -DVE_IOS_PACKAGE_DATA_ROOT=" << ShellQuote(packageDataRoot_.GetString());

        if (!developmentTeam_.empty())
        {
            stream << " -DVE_IOS_DEVELOPMENT_TEAM=" << ShellQuote(developmentTeam_);
        }

        if (!provisioningProfileSpecifier_.empty())
        {
            stream << " -DVE_IOS_PROVISIONING_PROFILE_SPECIFIER=" << ShellQuote(provisioningProfileSpecifier_);
        }

        if (!codeSignIdentity_.empty())
        {
            stream << " -DVE_IOS_CODE_SIGN_IDENTITY=" << ShellQuote(codeSignIdentity_);
        }

        if (!nativeAOTLibraryPath_.IsEmpty())
        {
            stream << " -DVE_IOS_NATIVEAOT_LIBRARY=" << ShellQuote(nativeAOTLibraryPath_.GetString());
        }

        if (!nativeAOTRuntimeNativeRoot_.IsEmpty())
        {
            stream << " -DVE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR=" << ShellQuote(nativeAOTRuntimeNativeRoot_.GetString());
        }

        return stream.str();
    }

    std::string EditorProjectPackerIOS::BuildNativeAOTPublishCommand() const
    {
        std::ostringstream stream;
        stream << "dotnet publish " << ShellQuote(GetGeneratedScriptProjectPath().GetString())
               << " --configuration " << ShellQuote(buildConfiguration_)
               << " --framework net10.0"
               << " --runtime " << ShellQuote(GetNativeAOTRuntimeIdentifier())
               << " --output " << ShellQuote(nativeAOTOutputRoot_.GetString())
               << " -p:VEngineEnableIOSNativeAOT=true"
               << " -p:VEngineScriptHostProject=" << ShellQuote(GetScriptHostProjectPath().GetString())
               << " -p:PublishAot=true"
               << " -p:PublishAotUsingRuntimePack=true"
               << " -p:NativeLib=Static"
               << " -p:SelfContained=true"
               << " -p:AppleMinOSVersion=" << ShellQuote(deploymentTarget_)
               << " --nologo";
        return stream.str();
    }

    std::string EditorProjectPackerIOS::BuildXcodeArchiveCommand() const
    {
        std::ostringstream stream;
        stream << "xcodebuild"
               << " -project " << ShellQuote(xcodeBinaryRoot_.GetString() + "/VEngine.xcodeproj")
               << " -scheme " << ShellQuote(IOSPlayerTargetName)
               << " -configuration " << ShellQuote(buildConfiguration_)
               << " -sdk iphoneos"
               << " -destination " << ShellQuote("generic/platform=iOS")
               << " -archivePath " << ShellQuote(archivePath_.GetString())
               << " CODE_SIGN_STYLE=" << ShellQuote(codeSignStyle_);

        if (!developmentTeam_.empty())
        {
            stream << " DEVELOPMENT_TEAM=" << ShellQuote(developmentTeam_);
        }

        if (!provisioningProfileSpecifier_.empty())
        {
            stream << " PROVISIONING_PROFILE_SPECIFIER=" << ShellQuote(provisioningProfileSpecifier_);
        }

        if (!codeSignIdentity_.empty())
        {
            stream << " CODE_SIGN_IDENTITY=" << ShellQuote(codeSignIdentity_);
        }

        if (ShouldAllowProvisioningUpdates())
        {
            stream << " -allowProvisioningUpdates";
        }

        stream << " archive";
        return stream.str();
    }

    std::string EditorProjectPackerIOS::BuildXcodeSimulatorBuildCommand() const
    {
        std::ostringstream stream;
        stream << "xcodebuild"
               << " -project " << ShellQuote(xcodeBinaryRoot_.GetString() + "/VEngine.xcodeproj")
               << " -scheme " << ShellQuote(IOSPlayerTargetName)
               << " -configuration " << ShellQuote(buildConfiguration_)
               << " -sdk iphonesimulator"
               << " -destination " << ShellQuote("generic/platform=iOS Simulator")
               << " CODE_SIGNING_ALLOWED=NO"
               << " CODE_SIGNING_REQUIRED=NO"
               << " CODE_SIGN_IDENTITY="
               << " build";
        return stream.str();
    }

    std::string EditorProjectPackerIOS::BuildXcodeExportCommand() const
    {
        std::ostringstream stream;
        stream << "xcodebuild"
               << " -exportArchive"
               << " -archivePath " << ShellQuote(archivePath_.GetString())
               << " -exportPath " << ShellQuote(ipaExportRoot_.GetString())
               << " -exportOptionsPlist " << ShellQuote(exportOptionsPlistPath_.GetString());

        if (ShouldAllowProvisioningUpdates())
        {
            stream << " -allowProvisioningUpdates";
        }

        return stream.str();
    }

    Path EditorProjectPackerIOS::GetGeneratedScriptProjectPath() const
    {
        return EditorScriptProjectGenerator::GetGeneratedProjectPath(projectRoot_, projectName_);
    }

    Path EditorProjectPackerIOS::GetScriptHostProjectPath() const
    {
        if (std::string(EngineSourceDirectory).empty())
        {
            return Path();
        }

        return Path(std::string(EngineSourceDirectory)) / "Engine" / "Managed" / "VEngine.ScriptHost" / "VEngine.ScriptHost.csproj";
    }

    Path EditorProjectPackerIOS::FindPublishedNativeAOTLibrary() const
    {
        const std::string assemblyName = GetNativeAOTAssemblyName();
        std::error_code errorCode;
        Path fallbackLibraryPath;
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(ToNativePath(nativeAOTOutputRoot_), errorCode))
        {
            if (errorCode)
            {
                return Path();
            }

            if (entry.is_regular_file(errorCode) && entry.path().extension() == ".a")
            {
                const std::string stem = entry.path().stem().generic_string();
                if (stem == assemblyName || stem == "lib" + assemblyName)
                {
                    return Path(entry.path().generic_string());
                }

                if (fallbackLibraryPath.IsEmpty())
                {
                    fallbackLibraryPath = Path(entry.path().generic_string());
                }
            }

            errorCode.clear();
        }

        return fallbackLibraryPath;
    }

    Path EditorProjectPackerIOS::FindNativeAOTRuntimeNativeDirectory() const
    {
        if (const char* runtimeNativeDirectory = std::getenv("VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR");
            runtimeNativeDirectory != nullptr && runtimeNativeDirectory[0] != '\0')
        {
            const Path overridePath(runtimeNativeDirectory);
            return FileSystem::IsDirectory(overridePath) ? overridePath : Path();
        }

        std::string packageRoot;
        if (const char* nugetPackages = std::getenv("NUGET_PACKAGES"); nugetPackages != nullptr && nugetPackages[0] != '\0')
        {
            packageRoot = nugetPackages;
        }
        else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
        {
            packageRoot = std::string(home) + "/.nuget/packages";
        }
        else if (const char* userProfile = std::getenv("USERPROFILE"); userProfile != nullptr && userProfile[0] != '\0')
        {
            packageRoot = std::string(userProfile) + "/.nuget/packages";
        }

        if (packageRoot.empty())
        {
            return Path();
        }

        const std::filesystem::path packageDirectory = std::filesystem::path(packageRoot) / GetNativeAOTRuntimePackageName();
        std::error_code errorCode;
        if (!std::filesystem::is_directory(packageDirectory, errorCode))
        {
            return Path();
        }

        std::filesystem::path bestCandidate;
        std::string bestVersion;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(packageDirectory, errorCode))
        {
            if (errorCode)
            {
                return Path();
            }

            if (!entry.is_directory(errorCode))
            {
                errorCode.clear();
                continue;
            }

            const std::filesystem::path candidate = entry.path() / "runtimes" / GetNativeAOTRuntimeIdentifier() / "native";
            const std::string versionName = entry.path().filename().generic_string();
            if (std::filesystem::exists(candidate / "libRuntime.WorkstationGC.a", errorCode) &&
                (bestVersion.empty() || CompareNuGetVersionNames(versionName, bestVersion) > 0))
            {
                bestCandidate = candidate;
                bestVersion = versionName;
            }

            errorCode.clear();
        }

        return bestCandidate.empty() ? Path() : Path(bestCandidate.generic_string());
    }

    std::string EditorProjectPackerIOS::GetNativeAOTAssemblyName() const
    {
        return projectName_ + ".Scripts";
    }

    std::string EditorProjectPackerIOS::GetNativeAOTRuntimeIdentifier() const
    {
        return IsSimulatorBuild() ? "iossimulator-arm64" : "ios-arm64";
    }

    std::string EditorProjectPackerIOS::GetNativeAOTRuntimePackageName() const
    {
        return "microsoft.netcore.app.runtime.nativeaot." + GetNativeAOTRuntimeIdentifier();
    }

    Path EditorProjectPackerIOS::GetIOSSimulatorAppBundlePath() const
    {
        return xcodeBinaryRoot_ / (buildConfiguration_ + "-iphonesimulator") / (std::string(IOSPlayerTargetName) + ".app");
    }

    bool EditorProjectPackerIOS::IsSimulatorBuild() const noexcept
    {
        return iosSDK_ == "iphonesimulator";
    }

    bool EditorProjectPackerIOS::ShouldAllowProvisioningUpdates() const noexcept
    {
        return codeSignStyle_ == "Automatic" && !developmentTeam_.empty();
    }

    std::string EditorProjectPackerIOS::GetExportSigningStyle() const
    {
        return codeSignStyle_ == "Manual" ? "manual" : "automatic";
    }

    std::string EditorProjectPackerIOS::ShellQuote(std::string_view text)
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

    std::string EditorProjectPackerIOS::SanitizeBundleIdentifierSegment(std::string text)
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

    bool EditorProjectPackerIOS::IsValidBundleIdentifier(std::string_view text) noexcept
    {
        if (text.empty())
        {
            return false;
        }

        size_t segmentStart = 0;
        for (size_t index = 0; index < text.size(); ++index)
        {
            const unsigned char character = static_cast<unsigned char>(text[index]);
            if (text[index] == '.')
            {
                if (index == segmentStart || text[index - 1] == '-')
                {
                    return false;
                }

                segmentStart = index + 1;
                continue;
            }

            if (std::isalnum(character) != 0)
            {
                continue;
            }

            if (text[index] == '-' && index != segmentStart)
            {
                continue;
            }

            return false;
        }

        return segmentStart < text.size() && text.back() != '-';
    }

    bool EditorProjectPackerIOS::IsValidCodeSignStyle(std::string_view text) noexcept
    {
        return text == "Automatic" || text == "Manual";
    }

    bool EditorProjectPackerIOS::IsValidExportMethod(std::string_view text) noexcept
    {
        return text == "development" || text == "ad-hoc" || text == "app-store" || text == "enterprise";
    }

    bool EditorProjectPackerIOS::IsValidIOSSDK(std::string_view text) noexcept
    {
        return text == "iphoneos" || text == "iphonesimulator";
    }

    bool EditorProjectPackerIOS::IsValidOrientation(std::string_view text) noexcept
    {
        return text == "Landscape" || text == "Portrait" || text == "Adaptive";
    }

    bool EditorProjectPackerIOS::IsValidDeploymentTarget(std::string_view text) noexcept
    {
        if (text.empty() || text.front() == '.' || text.back() == '.')
        {
            return false;
        }

        bool hasDot = false;
        bool previousDot = false;
        for (const char value : text)
        {
            const unsigned char character = static_cast<unsigned char>(value);
            if (value == '.')
            {
                if (previousDot)
                {
                    return false;
                }

                hasDot = true;
                previousDot = true;
                continue;
            }

            if (std::isdigit(character) == 0)
            {
                return false;
            }

            previousDot = false;
        }

        return hasDot;
    }

    std::string EditorProjectPackerIOS::NormalizeCodeSignStyle(std::string text)
    {
        std::string normalized;
        normalized.reserve(text.size());
        for (const char value : text)
        {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
        }

        if (normalized == "automatic")
        {
            return "Automatic";
        }

        if (normalized == "manual")
        {
            return "Manual";
        }

        return text;
    }
} // namespace ve::editor
