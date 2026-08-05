#pragma once

#include "Editor/Panels/BasePanel/BasePanel.h"
#include "Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h"

#include <memory>
#include <vector>

namespace ax::NodeEditor
{
    struct EditorContext;
}

namespace ve::editor
{
    class FrameGraphDebugPanel final : public BasePanel
    {
    public:
        void Init(Editor& editor) override;
        [[nodiscard]] bool Shutdown();

    private:
        enum class SelectionKind
        {
            None,
            Pass,
            Resource,
            Dependency,
        };

        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;
        void PollCapture();
        void RenderToolbar();
        void RenderTopologyLegend();
        void RenderLegendEntry(const char* label, FrameGraphDebugPanelVisualRole role, bool line, bool& first);
        void RenderTopology(float height);
        void RenderNodePin(UInt32 passIndex, const FrameGraphDebugPanelPin& pin, bool output, std::optional<std::string>& deferredTooltip);
        void RenderTables();
        void RenderDetails();
        void RenderPreviewPane();
        void RenderPassTable();
        void RenderResourceTable();
        void RenderDependencyTable();
        void RenderPassDetails(const FrameGraphDebugPass& pass);
        void RenderResourceDetails();
        void RenderDependencyDetails(const FrameGraphDebugDependency& dependency);
        void RenderTexturePreview(const FrameGraphDebugTexture& texture, UInt32 version);
        void ResetSnapshotUiState();
        void SelectPass(UInt32 passIndex, bool navigate);
        void SelectResource(FrameGraphDebugResourceKind kind, UInt32 resourceIndex, UInt32 version, bool navigate);
        void SelectDependency(UInt32 dependencyIndex, bool navigate);

        Editor* editor_ = nullptr;
        ax::NodeEditor::EditorContext* nodeEditorContext_ = nullptr;
        std::shared_ptr<const FrameGraphDebugData> debugData_;
        FrameGraphDebugPanelIdRegistry idRegistry_;
        std::vector<FrameGraphDebugPanelNodePosition> nodePositions_;
        std::vector<bool> positionedNodes_;
        Float32 previewScale_ = 0.5F;
        Float32 previewZoom_ = 1.0F;
        Float32 previewFitScale_ = 1.0F;
        Float32 previewPanX_ = 0.0F;
        Float32 previewPanY_ = 0.0F;
        char nameSearch_[128] = {};
        SelectionKind selectionKind_ = SelectionKind::None;
        FrameGraphDebugResourceKind selectedResourceKind_ = FrameGraphDebugResourceKind::Texture;
        UInt32 selectedPassIndex_ = InvalidFrameGraphDebugPassIndex;
        UInt32 selectedResourceIndex_ = InvalidFrameGraphResourceIndex;
        UInt32 selectedVersion_ = 0;
        UInt32 selectedDependencyIndex_ = InvalidFrameGraphDebugPassIndex;
        Int32 passKindFilter_ = 0;
        Int32 resourceKindFilter_ = 0;
        bool showCulled_ = true;
        bool previewFit_ = true;
        bool navigateToSelection_ = false;
        bool captureResultPending_ = false;
        bool nodeLayoutValid_ = false;
    };
} // namespace ve::editor
