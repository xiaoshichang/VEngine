#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>

namespace ve::editor
{
    struct EditorProjectDescriptor
    {
        std::string name;
        std::string engineVersion;
        std::string startScene = "Assets/Scenes/SampleScene.vescene";
    };

    class EditorProject
    {
    public:
        static constexpr const char* DescriptorFilename = "VEProject.json";
        static constexpr const char* AssetsDirectoryName = "Assets";
        static constexpr const char* LibraryDirectoryName = "Library";

        [[nodiscard]] static Path GetDescriptorPath(const Path& projectRoot);
        [[nodiscard]] static Path GetAssetsPath(const Path& projectRoot);
        [[nodiscard]] static Path GetLibraryPath(const Path& projectRoot);
        [[nodiscard]] static bool IsProjectRoot(const Path& projectRoot);

        [[nodiscard]] static ErrorCode EnsureLayout(const Path& projectRoot);
        [[nodiscard]] static ErrorCode SaveDescriptor(const Path& projectRoot, const EditorProjectDescriptor& descriptor);
        [[nodiscard]] static Result<EditorProjectDescriptor> LoadDescriptor(const Path& projectRoot);
    };
} // namespace ve::editor
