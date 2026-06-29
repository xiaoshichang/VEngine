#pragma once

#include "Editor/Core/EditorProjectRegistryBackend.h"

namespace ve::editor
{
    class MacEditorProjectRegistryBackend final : public EditorProjectRegistryBackend
    {
    public:
        [[nodiscard]] std::vector<std::string> LoadRecentProjects() override;
        void SaveRecentProjects(std::span<const std::string> projectPaths) override;
    };
} // namespace ve::editor
