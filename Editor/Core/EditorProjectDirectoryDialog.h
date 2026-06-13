#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace ve::editor
{
    class Editor;

    class ProjectDirectoryDialog : public NonMovable
    {
    public:
        ProjectDirectoryDialog() = default;

        void RequestOpen();
        void Render(Editor& editor);

    private:
        void RefreshPathBuffer();
        [[nodiscard]] bool NavigateToDirectory(const std::filesystem::path& directoryPath);
        [[nodiscard]] static std::string PathToUtf8String(const std::filesystem::path& path);

        std::filesystem::path currentDirectory_;
        std::array<char, 1024> pathBuffer_{};
        std::string errorMessage_;
        bool isOpen_ = false;
        std::vector<std::filesystem::path> entries_;
    };
} // namespace ve::editor
