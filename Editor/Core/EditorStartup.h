#pragma once

#include <span>
#include <string>

namespace ve::editor
{
    class Editor;

    struct EditorStartupOptions
    {
        std::string startupProjectPath;
        std::string errorMessage;
    };

    [[nodiscard]] EditorStartupOptions ParseEditorStartupOptions(std::span<const std::string> arguments);
    void QueueEditorStartupOptions(Editor& editor, const EditorStartupOptions& options);
} // namespace ve::editor
