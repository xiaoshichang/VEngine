#include "Editor/Panels/GameViewPanel.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneSystem.h"

#include <algorithm>
#include <imgui.h>

namespace ve::editor
{
    namespace
    {
        constexpr UInt32 MinGameViewExtent = 1;
        constexpr float DefaultGameViewAspectRatio = 16.0f / 9.0f;

        [[nodiscard]] float GetGameViewAspectRatio(const Editor& editor) noexcept
        {
            const Scene* scene = editor.GetSceneSystem().GetScene();
            if (scene == nullptr)
            {
                return DefaultGameViewAspectRatio;
            }

            const CameraComponent* camera = scene->GetMainCamera();
            if (camera == nullptr)
            {
                return DefaultGameViewAspectRatio;
            }

            return (std::max)(camera->GetAspectRatio(), 0.001f);
        }
    } // namespace

    GameViewPanel::GameViewPanel()
        : gameViewTexture_(nullptr)
    {
    }

    void GameViewPanel::Init(Editor& editor)
    {
        editor_ = &editor;
        if (gameViewTexture_ == nullptr)
        {
            auto desc = RenderTextureDesc{
                .name = "EditorGameViewTexture",
                .extent = {},
                .colorFormat = rhi::RhiFormat::Bgra8Unorm,
            };
            gameViewTexture_ = std::make_shared<RenderTexture>(desc);
            editor.KeepImGuiTextureAlive(gameViewTexture_);
        }
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
        VE_ASSERT_MESSAGE(editor_ != nullptr, "GameViewPanel requires Init before Render.");

        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        const float aspectRatio = GetGameViewAspectRatio(*editor_);
        const ImVec2 fittedImageSize = CalculateFittedImageSize(canvasSize, aspectRatio);
        const WindowExtent desiredExtent = ToRenderTargetExtent(fittedImageSize);
        bool textureRebuilt = false;
        if (desiredExtent.width != renderTargetExtent_.width || desiredExtent.height != renderTargetExtent_.height || !gameViewTexture_->IsValid())
        {
            RebuildGameViewTexture(*editor_, desiredExtent);
            textureRebuilt = true;
        }

        const ImVec2 imageSize(static_cast<float>(desiredExtent.width), static_cast<float>(desiredExtent.height));
        const ImVec2 cursorPosition = ImGui::GetCursorPos();
        const ImVec2 imageOffset((std::max)(0.0f, (canvasSize.x - imageSize.x) * 0.5f), (std::max)(0.0f, (canvasSize.y - imageSize.y) * 0.5f));
        ImGui::SetCursorPos(ImVec2(cursorPosition.x + imageOffset.x, cursorPosition.y + imageOffset.y));

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
