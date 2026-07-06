#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>
#include <string_view>
#include <vector>

namespace ve::editor
{
    struct EditorScriptProjectGenerateDesc
    {
        Path projectRoot;
        std::string projectName;
        std::vector<Path> scriptAssetPaths;
    };

    struct EditorIOSNativeAOTScriptProjectGenerateDesc
    {
        Path projectRoot;
        Path outputDirectory;
        std::string projectName;
        std::vector<Path> scriptAssetPaths;
        Path scriptHostProjectPath;
        std::string appleMinOSVersion;
    };

    class EditorScriptProjectGenerator
    {
    public:
        [[nodiscard]] static Result<Path> GenerateProject(const EditorScriptProjectGenerateDesc& desc);
        [[nodiscard]] static Result<Path> GenerateIOSNativeAOTProject(const EditorIOSNativeAOTScriptProjectGenerateDesc& desc);
        [[nodiscard]] static Path GetGeneratedProjectPath(const Path& projectRoot, std::string_view projectName);
        [[nodiscard]] static Path GetGeneratedIOSNativeAOTProjectPath(const Path& outputDirectory, std::string_view projectName);
        [[nodiscard]] static Path GetNativeAOTRegistryPath(const Path& outputDirectory);
    };
} // namespace ve::editor
