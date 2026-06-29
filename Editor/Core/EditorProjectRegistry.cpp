#include "Editor/Core/EditorProjectRegistry.h"

#include "Editor/Core/EditorProjectRegistryBackend.h"
#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Logging/Log.h"

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] std::unique_ptr<EditorProjectRegistryBackend> CreateEditorProjectRegistryBackend()
        {
#if VE_PLATFORM_WINDOWS
            return CreateWinEditorProjectRegistryBackend();
#elif VE_PLATFORM_MACOS
            return CreateMacEditorProjectRegistryBackend();
#else
            return nullptr;
#endif
        }
    } // namespace

    std::vector<std::string> EditorProjectRegistry::LoadRecentProjects()
    {
        std::unique_ptr<EditorProjectRegistryBackend> backend = CreateEditorProjectRegistryBackend();
        if (backend == nullptr)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Editor project registry backend is unsupported on this platform.");
            return {};
        }

        return backend->LoadRecentProjects();
    }

    void EditorProjectRegistry::SaveRecentProjects(std::span<const std::string> projectPaths)
    {
        std::unique_ptr<EditorProjectRegistryBackend> backend = CreateEditorProjectRegistryBackend();
        if (backend == nullptr)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Editor project registry backend is unsupported on this platform.");
            return;
        }

        backend->SaveRecentProjects(projectPaths);
    }
} // namespace ve::editor
