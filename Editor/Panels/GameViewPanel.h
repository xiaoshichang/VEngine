#pragma once

#include "Editor/Panels/BasePanel.h"
#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <memory>

struct ImVec2;

namespace ve::editor
{
    class Editor;

    class GameViewPanel final : public BasePanel
    {
    public:
        GameViewPanel();

        void Init(Editor& editor);
        void Render(Editor& editor, const ImVec2& position, const ImVec2& size);

        [[nodiscard]] const RenderTexture& GetGameViewTexture() const noexcept;
        [[nodiscard]] RenderTexture& GetGameViewTexture() noexcept;

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;

        void RebuildGameViewTexture(Editor& editor, WindowExtent extent);
        [[nodiscard]] static WindowExtent ToRenderTargetExtent(const ImVec2& size) noexcept;

        std::shared_ptr<RenderTexture> gameViewTexture_;
        WindowExtent renderTargetExtent_ = {};
        Editor* activeEditor_ = nullptr;
    };
} // namespace ve::editor
