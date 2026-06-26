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

    class EditorScriptProjectGenerator
    {
    public:
        [[nodiscard]] static Result<Path> GenerateProject(const EditorScriptProjectGenerateDesc& desc);
        [[nodiscard]] static Path GetGeneratedProjectPath(const Path& projectRoot, std::string_view projectName);
    };
} // namespace ve::editor
