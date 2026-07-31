#include "Editor/Core/EditorStartup.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Logging/Log.h"

#include <cstddef>

namespace ve::editor
{
    EditorStartupOptions ParseEditorStartupOptions(std::span<const std::string> arguments)
    {
        EditorStartupOptions options;
        for (std::size_t argumentIndex = 1; argumentIndex < arguments.size(); ++argumentIndex)
        {
            if (arguments[argumentIndex] != "--project")
            {
                continue;
            }

            if (argumentIndex + 1 >= arguments.size() || arguments[argumentIndex + 1].empty() || arguments[argumentIndex + 1].starts_with("--"))
            {
                options.errorMessage = "Editor command-line option --project requires a non-empty path argument.";
                return options;
            }

            options.startupProjectPath = arguments[argumentIndex + 1];
            return options;
        }
        return options;
    }

    void QueueEditorStartupOptions(Editor& editor, const EditorStartupOptions& options)
    {
        if (!options.errorMessage.empty())
        {
            VE_LOG_ERROR_CATEGORY("Editor", "{}", options.errorMessage);
            return;
        }

        if (!options.startupProjectPath.empty())
        {
            VE_LOG_INFO_CATEGORY("Editor", "Opening startup project: {}", options.startupProjectPath);
            editor.RequestOpenProject(options.startupProjectPath);
        }
    }
} // namespace ve::editor
