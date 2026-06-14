#pragma once

#include "Editor/Panels/BasePanel.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <vector>

namespace ve::editor
{
    class EditorAssetDatabase;

    class Editor;

    class AssetsPanel final : public BasePanel
    {
    public:
        void Render(Editor& editor, const ImVec2& position, const ImVec2& size);

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;
        void RenderDirectoryTree(const std::vector<Path>& directories, const Path& directory);
        void RenderAssetFiles(EditorAssetDatabase& assetDatabase);
        [[nodiscard]] std::vector<Path> BuildDirectoryList(const EditorAssetDatabase& assetDatabase) const;

        Editor* editor_ = nullptr;
        Path currentDirectory_ = Path("Assets");
    };
} // namespace ve::editor
