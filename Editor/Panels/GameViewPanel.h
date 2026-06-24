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

        void Init(Editor& editor) override;

        [[nodiscard]] const RenderTexture& GetGameViewTexture() const noexcept;
        [[nodiscard]] RenderTexture& GetGameViewTexture() noexcept;

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;

        void RebuildGameViewTexture(Editor& editor, WindowExtent extent);
        [[nodiscard]] static WindowExtent ToRenderTargetExtent(const ImVec2& imageSize) noexcept;
        [[nodiscard]] static ImVec2 CalculateFittedImageSize(const ImVec2& canvasSize, float aspectRatio) noexcept;

        std::shared_ptr<RenderTexture> gameViewTexture_;
        WindowExtent renderTargetExtent_ = {};
        Editor* editor_ = nullptr;
    };
} // namespace ve::editor
