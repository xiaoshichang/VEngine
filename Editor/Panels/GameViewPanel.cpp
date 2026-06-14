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
        : viewportClient_("EditorGameView")
    {
    }

    void GameViewPanel::Render(Editor& editor, const ImVec2& position, const ImVec2& size)
    {
        activeEditor_ = &editor;
        BasePanel::Render(position, size);
        activeEditor_ = nullptr;
    }

    const ViewportClient& GameViewPanel::GetViewportClient() const noexcept
    {
        return viewportClient_;
    }

    ViewportClient& GameViewPanel::GetViewportClient() noexcept
    {
        return viewportClient_;
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
        if (desiredExtent.width != renderTargetExtent_.width || desiredExtent.height != renderTargetExtent_.height || !viewportClient_.IsRenderTargetBound())
        {
            RebuildViewportClient(*activeEditor_, desiredExtent);
        }

        const ImVec2 imageSize(static_cast<float>(desiredExtent.width), static_cast<float>(desiredExtent.height));
        std::shared_ptr<RTRenderTarget> rtRenderTarget = viewportClient_.GetRenderTarget().GetRTRenderTarget();
        void* sampledView = rtRenderTarget != nullptr ? rtRenderTarget->GetNativeSampledViewHandle() : nullptr;
        if (sampledView == nullptr)
        {
            ImGui::Button("Game View texture pending", imageSize);
            return;
        }

        activeEditor_->KeepImGuiTextureAlive(rtRenderTarget);
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(sampledView))), imageSize);
    }

    void GameViewPanel::RebuildViewportClient(Editor& editor, WindowExtent extent)
    {
        VE_ASSERT_SCENE_THREAD();

        RenderTargetDesc renderTargetDesc = {};
        renderTargetDesc.name = "EditorGameViewRenderTarget";
        renderTargetDesc.kind = RenderTargetKind::Texture;
        renderTargetDesc.extent = extent;
        renderTargetDesc.colorFormat = rhi::RhiFormat::Bgra8Unorm;

        RenderTarget renderTarget(renderTargetDesc);
        ErrorCode initResult = renderTarget.InitRenderResource(editor.GetRenderSystem());
        VE_ASSERT_MESSAGE(initResult == ErrorCode::None, "GameViewPanel failed to initialize render target resource.");

        viewportClient_.BindRenderTarget(std::move(renderTarget));
        renderTargetExtent_ = extent;
    }

    WindowExtent GameViewPanel::ToRenderTargetExtent(const ImVec2& size) noexcept
    {
        const float width = (std::max)(size.x, static_cast<float>(MinGameViewExtent));
        const float height = (std::max)(size.y, static_cast<float>(MinGameViewExtent));
        return WindowExtent{static_cast<UInt32>(width), static_cast<UInt32>(height)};
    }
} // namespace ve::editor
