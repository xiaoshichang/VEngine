#pragma once

#include <string>

namespace ve::editor
{
    class Editor;

    void QueueEditorStartupProject(Editor& editor, const std::string& startupProjectPath);
} // namespace ve::editor
