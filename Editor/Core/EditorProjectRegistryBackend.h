#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ve::editor
{
    /// Owns platform-specific persistence for editor project history.
    class EditorProjectRegistryBackend : public NonMovable
    {
    public:
        virtual ~EditorProjectRegistryBackend() = default;

        [[nodiscard]] virtual std::vector<std::string> LoadRecentProjects() = 0;
        virtual void SaveRecentProjects(std::span<const std::string> projectPaths) = 0;
    };

    [[nodiscard]] std::unique_ptr<EditorProjectRegistryBackend> CreateWinEditorProjectRegistryBackend();
    [[nodiscard]] std::unique_ptr<EditorProjectRegistryBackend> CreateMacEditorProjectRegistryBackend();
} // namespace ve::editor
