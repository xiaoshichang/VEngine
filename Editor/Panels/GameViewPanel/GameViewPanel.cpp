#include "Editor/Panels/GameViewPanel/GameViewPanel.h"

#include "Editor/Core/Editor.h"
#include "Engine/Render/PBR/HdrColorPipeline.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneSystem.h"

#include <algorithm>
#include <imgui.h>
#include <utility>

namespace ve::editor
{
    namespace
    {
        constexpr UInt32 MinGameViewExtent = 1;
        constexpr const char* GameViewHdrTextureName = "EditorGameViewHdr";
        constexpr const char* GameViewPreviewTextureName = "EditorGameViewPreview";

        [[nodiscard]] const CameraComponent* GetGameViewCamera(const Editor& editor) noexcept
        {
            const Scene* scene = editor.GetSceneSystem().GetScene();
            return scene != nullptr ? scene->GetCamera() : nullptr;
        }
    } // namespace

    GameViewPanel::GameViewPanel()
        : gameViewHdrTexture_(nullptr)
        , gameViewPreviewTexture_(nullptr)
        , gameViewState_(std::make_shared<RenderViewState>(RenderViewStateDesc{"EditorGameView"}))
    {
    }

    void GameViewPanel::Init(Editor& editor)
    {
        editor_ = &editor;
        if (gameViewHdrTexture_ == nullptr)
        {
            gameViewHdrTexture_ = std::make_shared<RenderTexture>(pbr::BuildHdrSceneColorDesc({}, GameViewHdrTextureName));
        }
        if (gameViewPreviewTexture_ == nullptr)
        {
            RenderTextureDesc desc = {};
            desc.name = GameViewPreviewTextureName;
            desc.colorFormat = rhi::RhiFormat::Bgra8Unorm;
            desc.createDepthTexture = false;
            gameViewPreviewTexture_ = std::make_shared<RenderTexture>(std::move(desc));
        }
    }

    const RenderTexture& GameViewPanel::GetGameViewHdrTexture() const noexcept
    {
        return *gameViewHdrTexture_;
    }

    const RenderTexture& GameViewPanel::GetGameViewPreviewTexture() const noexcept
    {
        return *gameViewPreviewTexture_;
    }

    std::shared_ptr<RenderViewState> GameViewPanel::GetRenderViewState() const noexcept
    {
        return gameViewState_;
    }

    const char* GameViewPanel::GetName() const noexcept
    {
        return "Game View";
    }

    void GameViewPanel::RenderContent()
    {
        VE_ASSERT_MESSAGE(editor_ != nullptr, "GameViewPanel requires Init before Render.");

        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        const CameraComponent* camera = GetGameViewCamera(*editor_);
        const ImVec2 fittedImageSize =
            camera != nullptr && !camera->IsAspectRatioAutomatic() ? CalculateFittedImageSize(canvasSize, camera->GetAspectRatio()) : canvasSize;
        const WindowExtent desiredExtent = ToRenderTargetExtent(fittedImageSize);
        if (desiredExtent.width != renderTargetExtent_.width || desiredExtent.height != renderTargetExtent_.height || !gameViewHdrTexture_->IsValid() ||
            !gameViewPreviewTexture_->IsValid())
        {
            RebuildGameViewTextures(*editor_, desiredExtent);
        }

        const ImVec2 imageSize(static_cast<float>(desiredExtent.width), static_cast<float>(desiredExtent.height));
        const ImVec2 cursorPosition = ImGui::GetCursorPos();
        const ImVec2 imageOffset((std::max)(0.0f, (canvasSize.x - imageSize.x) * 0.5f), (std::max)(0.0f, (canvasSize.y - imageSize.y) * 0.5f));
        ImGui::SetCursorPos(ImVec2(cursorPosition.x + imageOffset.x, cursorPosition.y + imageOffset.y));

        void* resourceView = gameViewPreviewTexture_->GetRenderResourceViewHandle();
        if (resourceView == nullptr)
        {
            ImGui::Button("Game View texture pending", imageSize);
            return;
        }

        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(resourceView))), imageSize);
    }

    void GameViewPanel::RebuildGameViewTextures(Editor& editor, WindowExtent extent)
    {
        VE_ASSERT_SCENE_THREAD();

        auto hdrTexture = std::make_shared<RenderTexture>(pbr::BuildHdrSceneColorDesc(extent, GameViewHdrTextureName));
        hdrTexture->InitRenderResource(editor.GetRenderSystem());

        RenderTextureDesc previewDesc = {};
        previewDesc.name = GameViewPreviewTextureName;
        previewDesc.extent = extent;
        previewDesc.colorFormat = rhi::RhiFormat::Bgra8Unorm;
        previewDesc.createDepthTexture = false;
        auto previewTexture = std::make_shared<RenderTexture>(std::move(previewDesc));
        previewTexture->InitRenderResource(editor.GetRenderSystem());

        gameViewHdrTexture_ = std::move(hdrTexture);
        gameViewPreviewTexture_ = std::move(previewTexture);

        renderTargetExtent_ = extent;
    }

    WindowExtent GameViewPanel::ToRenderTargetExtent(const ImVec2& imageSize) noexcept
    {
        const float width = (std::max)(imageSize.x, static_cast<float>(MinGameViewExtent));
        const float height = (std::max)(imageSize.y, static_cast<float>(MinGameViewExtent));
        return WindowExtent{static_cast<UInt32>(width), static_cast<UInt32>(height)};
    }

    ImVec2 GameViewPanel::CalculateFittedImageSize(const ImVec2& canvasSize, float aspectRatio) noexcept
    {
        const float canvasWidth = (std::max)(canvasSize.x, static_cast<float>(MinGameViewExtent));
        const float canvasHeight = (std::max)(canvasSize.y, static_cast<float>(MinGameViewExtent));
        const float safeAspectRatio = (std::max)(aspectRatio, 0.001f);
        const float canvasAspectRatio = canvasWidth / canvasHeight;

        if (canvasAspectRatio > safeAspectRatio)
        {
            return ImVec2(canvasHeight * safeAspectRatio, canvasHeight);
        }

        return ImVec2(canvasWidth, canvasWidth / safeAspectRatio);
    }
} // namespace ve::editor
