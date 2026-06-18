#include "Editor/Core/EditorResourceLoader.h"

#include <utility>

namespace ve::editor
{
    ErrorCode EditorResourceLoader::Initialize(Path projectRoot)
    {
        if (projectRoot.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        projectRoot_ = std::move(projectRoot);
        initialized_ = true;
        return ErrorCode::None;
    }

    void EditorResourceLoader::Shutdown() noexcept
    {
        projectRoot_ = Path();
        initialized_ = false;
    }

    bool EditorResourceLoader::IsInitialized() const noexcept
    {
        return initialized_;
    }

    const Path& EditorResourceLoader::GetProjectRoot() const noexcept
    {
        return projectRoot_;
    }
} // namespace ve::editor
