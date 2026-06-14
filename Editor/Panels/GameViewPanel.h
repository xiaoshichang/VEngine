#pragma once

#include "Editor/Panels/BasePanel.h"
#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Render/ViewportClient.h"

struct ImVec2;

namespace ve::editor
{
    class Editor;

    class GameViewPanel final : public BasePanel
    {
    public:
        GameViewPanel();

        void Render(Editor& editor, const ImVec2& position, const ImVec2& size);

        [[nodiscard]] const ViewportClient& GetViewportClient() const noexcept;
        [[nodiscard]] ViewportClient& GetViewportClient() noexcept;

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;

        void RebuildViewportClient(Editor& editor, WindowExtent extent);
        [[nodiscard]] static WindowExtent ToRenderTargetExtent(const ImVec2& size) noexcept;

        ViewportClient viewportClient_;
        WindowExtent renderTargetExtent_ = {};
        Editor* activeEditor_ = nullptr;
    };
} // namespace ve::editor
