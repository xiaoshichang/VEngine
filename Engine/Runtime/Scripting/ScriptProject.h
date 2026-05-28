#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>
#include <string_view>

namespace ve
{
    enum class ScriptBuildConfiguration
    {
        Debug,
        Release,
    };

    struct WindowsScriptProjectConfig
    {
        Path projectPath;
        std::string assemblyName;

        [[nodiscard]] bool IsConfigured() const noexcept
        {
            return !projectPath.IsEmpty() && !assemblyName.empty();
        }
    };

    struct ScriptProjectConfig
    {
        WindowsScriptProjectConfig windows;

        [[nodiscard]] bool HasWindowsScripts() const noexcept
        {
            return windows.IsConfigured();
        }
    };

    struct WindowsScriptBuildArtifacts
    {
        Path outputDirectory;
        Path projectAssemblyPath;
        Path projectDepsJsonPath;
        Path projectRuntimeConfigPath;
        Path projectPdbPath;
        Path scriptApiAssemblyPath;
        Path scriptApiPdbPath;
    };

    [[nodiscard]] const char* ToString(ScriptBuildConfiguration configuration) noexcept;
    [[nodiscard]] Result<ScriptBuildConfiguration> ParseScriptBuildConfiguration(std::string_view text);

    [[nodiscard]] Result<ScriptProjectConfig> LoadScriptProjectConfig(const Path& projectRoot);

    [[nodiscard]] Path GetWindowsScriptGeneratedOutputDirectory(const Path& projectRoot,
                                                                ScriptBuildConfiguration configuration,
                                                                std::string_view assemblyName);
    [[nodiscard]] Path GetWindowsScriptGeneratedIntermediateDirectory(const Path& projectRoot,
                                                                      ScriptBuildConfiguration configuration,
                                                                      std::string_view assemblyName);
    [[nodiscard]] Path GetWindowsPackagedScriptDirectory(const Path& contentRoot);

    [[nodiscard]] WindowsScriptBuildArtifacts
    GetWindowsScriptBuildArtifacts(const Path& outputDirectory, std::string_view assemblyName);
    [[nodiscard]] WindowsScriptBuildArtifacts
    GetWindowsGeneratedScriptBuildArtifacts(const Path& projectRoot,
                                            ScriptBuildConfiguration configuration,
                                            std::string_view assemblyName);
    [[nodiscard]] WindowsScriptBuildArtifacts GetWindowsPackagedScriptBuildArtifacts(const Path& contentRoot,
                                                                                    std::string_view assemblyName);

    [[nodiscard]] ErrorCode EnsureWindowsScriptGeneratedDirectories(const Path& projectRoot,
                                                                    ScriptBuildConfiguration configuration,
                                                                    std::string_view assemblyName);
    [[nodiscard]] ErrorCode ValidateWindowsScriptBuildArtifacts(const WindowsScriptBuildArtifacts& artifacts);
    [[nodiscard]] Result<WindowsScriptBuildArtifacts>
    BuildWindowsScriptProject(const Path& projectRoot,
                              const WindowsScriptProjectConfig& config,
                              ScriptBuildConfiguration configuration);
} // namespace ve
