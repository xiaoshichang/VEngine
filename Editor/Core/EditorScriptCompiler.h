#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>

namespace ve::editor
{
    struct EditorScriptCompileDesc
    {
        Path projectRoot;
        std::string projectName;
        Path scriptHostAssemblyPath;
    };

    struct EditorScriptCompileResult
    {
        Path assemblyPath;
        Path outputDirectory;
    };

    class EditorScriptCompiler
    {
    public:
        [[nodiscard]] Result<EditorScriptCompileResult> CompileProjectScripts(const EditorScriptCompileDesc& desc) const;

    private:
        [[nodiscard]] static Path ResolveProjectFile(const EditorScriptCompileDesc& desc);
    };
} // namespace ve::editor
