#pragma once

#include "Engine/Runtime/FileSystem/Path.h"

#include <string_view>

namespace ve::editor
{
    [[nodiscard]] Path GetBuiltinAssetsRootPath(const Path& projectRoot);
    [[nodiscard]] Path GetEditorAssetsRootPath(const Path& projectRoot);
    [[nodiscard]] bool IsEditorContentPath(const Path& path) noexcept;
    [[nodiscard]] Path ResolveEditorAssetReference(std::string_view reference, std::string_view fallbackExtension);
    [[nodiscard]] Path ResolveEditorContentPath(const Path& projectRoot, const Path& contentPath);
    [[nodiscard]] Path ToEditorContentPath(const Path& projectRoot, const Path& physicalPath);
} // namespace ve::editor
