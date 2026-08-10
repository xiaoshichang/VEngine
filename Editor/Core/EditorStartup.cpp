#include "Editor/Core/EditorStartup.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Logging/Log.h"

namespace ve::editor
{
    void QueueEditorStartupProject(Editor& editor, const std::string& startupProjectPath)
    {
        if (!startupProjectPath.empty())
        {
            VE_LOG_INFO_CATEGORY("Editor", "Opening startup project: {}", startupProjectPath);
            editor.RequestOpenProject(startupProjectPath);
        }
    }
} // namespace ve::editor
