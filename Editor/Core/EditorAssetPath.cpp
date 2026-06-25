#include "Editor/Core/EditorAssetPath.h"

#include "Editor/Core/EditorProject.h"

#include <string>

namespace ve::editor
{
    namespace
    {
        constexpr const char* BuiltinAssetDirectoryName = "BuiltinAsset";
        constexpr const char* EditorOnlyAssetDirectoryName = "EditorOnlyAsset";

        [[nodiscard]] bool StartsWithPathRoot(const Path& path, const char* root) noexcept
        {
            const std::string& text = path.GetString();
            return text == root || text.starts_with(std::string(root) + "/");
        }

        [[nodiscard]] Path AppendFallbackExtension(Path path, std::string_view fallbackExtension)
        {
            if (!path.IsEmpty() && path.GetExtension().empty() && !fallbackExtension.empty())
            {
                return Path(path.GetString() + std::string(fallbackExtension));
            }

            return path;
        }

        [[nodiscard]] Path TryToRootRelativePath(const Path& physicalPath, const Path& physicalRoot, const char* logicalRoot)
        {
            const std::string& root = physicalRoot.GetString();
            const std::string& path = physicalPath.GetString();
            if (root.empty())
            {
                return Path();
            }

            if (path == root)
            {
                return Path(logicalRoot);
            }

            const std::string prefix = root.ends_with('/') ? root : root + "/";
            if (!path.starts_with(prefix))
            {
                return Path();
            }

            return Path(std::string(logicalRoot) + "/" + path.substr(prefix.size()));
        }
    } // namespace

    Path GetBuiltinAssetsRootPath(const Path& projectRoot)
    {
        if (projectRoot.IsEmpty())
        {
            return Path(BuiltinAssetDirectoryName);
        }

        return projectRoot.GetParentPath() / BuiltinAssetDirectoryName;
    }

    Path GetEditorOnlyAssetsRootPath(const Path& projectRoot)
    {
        if (projectRoot.IsEmpty())
        {
            return Path(EditorOnlyAssetDirectoryName);
        }

        return projectRoot.GetParentPath() / EditorOnlyAssetDirectoryName;
    }

    bool IsEditorContentPath(const Path& path) noexcept
    {
        return StartsWithPathRoot(path, EditorProject::AssetsDirectoryName) || StartsWithPathRoot(path, EditorProject::LibraryDirectoryName) ||
               StartsWithPathRoot(path, BuiltinAssetDirectoryName) || StartsWithPathRoot(path, EditorOnlyAssetDirectoryName);
    }

    Path ResolveEditorAssetReference(std::string_view reference, std::string_view fallbackExtension)
    {
        if (reference.empty())
        {
            return Path();
        }

        Path path(reference);
        if (!IsEditorContentPath(path))
        {
            path = Path(EditorProject::AssetsDirectoryName) / path;
        }

        return AppendFallbackExtension(path, fallbackExtension);
    }

    Path ResolveEditorContentPath(const Path& projectRoot, const Path& contentPath)
    {
        if (contentPath.IsEmpty() || contentPath.IsAbsolute())
        {
            return contentPath;
        }

        if (StartsWithPathRoot(contentPath, BuiltinAssetDirectoryName))
        {
            return projectRoot.GetParentPath() / contentPath;
        }

        if (StartsWithPathRoot(contentPath, EditorOnlyAssetDirectoryName))
        {
            return projectRoot.GetParentPath() / contentPath;
        }

        return projectRoot / contentPath;
    }

    Path ToEditorContentPath(const Path& projectRoot, const Path& physicalPath)
    {
        Path contentPath = TryToRootRelativePath(physicalPath, projectRoot / EditorProject::AssetsDirectoryName, EditorProject::AssetsDirectoryName);
        if (!contentPath.IsEmpty())
        {
            return contentPath;
        }

        contentPath = TryToRootRelativePath(physicalPath, projectRoot / EditorProject::LibraryDirectoryName, EditorProject::LibraryDirectoryName);
        if (!contentPath.IsEmpty())
        {
            return contentPath;
        }

        contentPath = TryToRootRelativePath(physicalPath, GetBuiltinAssetsRootPath(projectRoot), BuiltinAssetDirectoryName);
        if (!contentPath.IsEmpty())
        {
            return contentPath;
        }

        contentPath = TryToRootRelativePath(physicalPath, GetEditorOnlyAssetsRootPath(projectRoot), EditorOnlyAssetDirectoryName);
        if (!contentPath.IsEmpty())
        {
            return contentPath;
        }

        return physicalPath;
    }
} // namespace ve::editor
