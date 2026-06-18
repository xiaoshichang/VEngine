#include "Editor/Panels/GameViewPanel.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderSystem.h"

#include <imgui.h>

#include <algorithm>

namespace ve::editor
{
    namespace
    {
        constexpr UInt32 MinGameViewExtent = 1;
    }

    GameViewPanel::GameViewPanel()
        : gameViewTexture_(nullptr)
    {
    }

    void GameViewPanel::Init(Editor& editor)
    {
        auto desc = RenderTextureDesc{
            .name = "EditorGameViewTexture",
            .extent = {},
            .colorFormat = rhi::RhiFormat::Bgra8Unorm,
        };
        gameViewTexture_ = std::make_shared<RenderTexture>(desc);
        editor.KeepImGuiTextureAlive(gameViewTexture_);
    }

    void GameViewPanel::Render(Editor& editor, const ImVec2& position, const ImVec2& size)
    {
        activeEditor_ = &editor;
        BasePanel::Render(position, size);
        activeEditor_ = nullptr;
    }

    const RenderTexture& GameViewPanel::GetGameViewTexture() const noexcept
    {
        return *gameViewTexture_;
    }

    RenderTexture& GameViewPanel::GetGameViewTexture() noexcept
    {
        return *gameViewTexture_;
    }

    const char* GameViewPanel::GetName() const noexcept
    {
        return "Game View";
    }

    void GameViewPanel::RenderContent()
    {
        VE_ASSERT_MESSAGE(activeEditor_ != nullptr, "GameViewPanel::RenderContent requires GameViewPanel::Render.");

        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        const WindowExtent desiredExtent = ToRenderTargetExtent(canvasSize);
        bool textureRebuilt = false;
        if (desiredExtent.width != renderTargetExtent_.width ||
            desiredExtent.height != renderTargetExtent_.height ||
            !gameViewTexture_->IsValid())
        {
            RebuildGameViewTexture(*activeEditor_, desiredExtent);
            textureRebuilt = true;
        }

        const ImVec2 imageSize(static_cast<float>(desiredExtent.width), static_cast<float>(desiredExtent.height));
        void* resourceView = gameViewTexture_->GetRenderResourceViewHandle();
        if (textureRebuilt || resourceView == nullptr)
        {
            ImGui::Button("Game View texture pending", imageSize);
            return;
        }

        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(resourceView))), imageSize);
    }

    void GameViewPanel::RebuildGameViewTexture(Editor& editor, WindowExtent extent)
    {
        VE_ASSERT_SCENE_THREAD();

        gameViewTexture_->Resize(extent);
        gameViewTexture_->InitRenderResource(editor.GetRenderSystem());

        renderTargetExtent_ = extent;
    }

    WindowExtent GameViewPanel::ToRenderTargetExtent(const ImVec2& size) noexcept
    {
        const float width = (std::max)(size.x, static_cast<float>(MinGameViewExtent));
        const float height = (std::max)(size.y, static_cast<float>(MinGameViewExtent));
        return WindowExtent{static_cast<UInt32>(width), static_cast<UInt32>(height)};
    }
} // namespace ve::editor
