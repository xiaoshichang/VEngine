#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace ve::editor
{
    /// Persists Editor project history in the current user's platform settings store.
    class EditorProjectRegistry
    {
    public:
        static constexpr std::size_t MaxRecentProjectCount = 10;

        [[nodiscard]] static std::vector<std::string> LoadRecentProjects();
        static void SaveRecentProjects(std::span<const std::string> projectPaths);
    };
} // namespace ve::editor
