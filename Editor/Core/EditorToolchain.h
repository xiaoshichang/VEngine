#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"

namespace ve::editor
{
    struct EditorShaderToolchain
    {
        Path shaderTool;
#if VE_PLATFORM_WINDOWS
        Path dxc;
        Path fxc;
        Path slang;
#elif VE_PLATFORM_MACOS
        Path slang;
#endif
    };

    [[nodiscard]] Result<EditorShaderToolchain> ResolveEditorShaderToolchain();
    [[nodiscard]] ErrorCode ValidateEditorToolchain();
} // namespace ve::editor
