#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Input/InputSystem.h"
#include "Engine/Runtime/Scene/SceneRenderExtractor.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace ve
{
    namespace
    {
        constexpr UInt64 GameViewTextureId = 3;

        [[nodiscard]] ImVec2 GetViewportImageSize()
        {
            ImVec2 imageSize = ImGui::GetContentRegionAvail();
            imageSize.x = std::max(imageSize.x, 1.0f);
            imageSize.y = std::max(imageSize.y, 1.0f);
            return imageSize;
        }

        [[nodiscard]] UInt32 ToTextureExtent(Float32 logicalSize, Float32 framebufferScale) noexcept
        {
            const Float32 scaledSize = std::max(logicalSize * std::max(framebufferScale, 1.0f), 1.0f);
            return static_cast<UInt32>(std::ceil(scaledSize));
        }

        void DrawInactiveGameView(const ImVec2& imageSize)
        {
            const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 canvasMax(canvasPos.x + imageSize.x, canvasPos.y + imageSize.y);
            drawList->AddRectFilled(canvasPos,
                                    canvasMax,
                                    ImGui::GetColorU32(ImVec4(0.035f, 0.04f, 0.045f, 1.0f)));
            drawList->AddRect(canvasPos,
                              canvasMax,
                              ImGui::GetColorU32(ImVec4(0.20f, 0.22f, 0.24f, 1.0f)));
            ImGui::Dummy(imageSize);
        }

        void SubmitKey(InputSystem& inputSystem, ImGuiKey imguiKey, KeyCode keyCode)
        {
            inputSystem.SubmitKey(keyCode, ImGui::IsKeyDown(imguiKey));
        }
    } // namespace

    EditorViewportRenderRequest WindowsEditorPanels::MakeGameViewRequest(EditorProjectService& projectService,
                                                                         EngineRuntime& runtime,
                                                                         UInt32 width,
                                                                         UInt32 height)
    {
        const Float32 aspectRatio =
            height > 0 ? static_cast<Float32>(width) / static_cast<Float32>(height) : (16.0f / 9.0f);

        EditorViewportRenderRequest request;
        request.textureId = GameViewTextureId;
        request.width = width;
        request.height = height;
        request.snapshot = ExtractSceneRenderSnapshot(
            projectService.GetActiveScene(), runtime.GetResourceManager(), nextViewportFrameId_++, aspectRatio);
        return request;
    }

    void WindowsEditorPanels::DrawGameView(EditorProjectService& projectService, EngineRuntime& runtime)
    {
        const ImVec2 imageSize = GetViewportImageSize();
        if (!projectService.IsPlaying())
        {
            DrawInactiveGameView(imageSize);
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        const UInt32 width = ToTextureExtent(imageSize.x, io.DisplayFramebufferScale.x);
        const UInt32 height = ToTextureExtent(imageSize.y, io.DisplayFramebufferScale.y);
        EditorViewportRenderRequest request = MakeGameViewRequest(projectService, runtime, width, height);
        const bool hasCamera = request.snapshot.hasMainCamera;

        DrawViewportImage(GameViewTextureId, imageSize, "No active Camera", !hasCamera);
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        SubmitGameViewInput(runtime, imageMin, imageMax, width, height, ImGui::IsItemHovered());
        viewportRequests_.push_back(std::move(request));
    }

    void WindowsEditorPanels::SubmitGameViewInput(EngineRuntime& runtime,
                                                  const ImVec2& imageMin,
                                                  const ImVec2& imageMax,
                                                  UInt32 width,
                                                  UInt32 height,
                                                  bool imageHovered)
    {
        InputSystem& inputSystem = runtime.GetInputSystem();
        inputSystem.BeginMainFrame();
        inputSystem.SetViewportSize(width, height);
        inputSystem.SetFocused(imageHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));

        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 imageSize(std::max(imageMax.x - imageMin.x, 1.0f), std::max(imageMax.y - imageMin.y, 1.0f));
        const Float32 localMouseX = std::clamp(io.MousePos.x - imageMin.x, 0.0f, imageSize.x);
        const Float32 localMouseY = std::clamp(io.MousePos.y - imageMin.y, 0.0f, imageSize.y);
        const Float32 scaleX = static_cast<Float32>(width) / imageSize.x;
        const Float32 scaleY = static_cast<Float32>(height) / imageSize.y;
        inputSystem.SubmitMousePosition(localMouseX * scaleX, localMouseY * scaleY);

        if (imageHovered)
        {
            inputSystem.SubmitMouseWheel(io.MouseWheel);
            inputSystem.SubmitMouseButtonState(MouseButton::Left,
                                               ImGui::IsMouseDown(ImGuiMouseButton_Left),
                                               ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                                               ImGui::IsMouseReleased(ImGuiMouseButton_Left));
            inputSystem.SubmitMouseButtonState(MouseButton::Right,
                                               ImGui::IsMouseDown(ImGuiMouseButton_Right),
                                               ImGui::IsMouseClicked(ImGuiMouseButton_Right),
                                               ImGui::IsMouseReleased(ImGuiMouseButton_Right));
            inputSystem.SubmitMouseButtonState(MouseButton::Middle,
                                               ImGui::IsMouseDown(ImGuiMouseButton_Middle),
                                               ImGui::IsMouseClicked(ImGuiMouseButton_Middle),
                                               ImGui::IsMouseReleased(ImGuiMouseButton_Middle));

            SubmitKey(inputSystem, ImGuiKey_A, KeyCode::A);
            SubmitKey(inputSystem, ImGuiKey_B, KeyCode::B);
            SubmitKey(inputSystem, ImGuiKey_C, KeyCode::C);
            SubmitKey(inputSystem, ImGuiKey_D, KeyCode::D);
            SubmitKey(inputSystem, ImGuiKey_E, KeyCode::E);
            SubmitKey(inputSystem, ImGuiKey_F, KeyCode::F);
            SubmitKey(inputSystem, ImGuiKey_G, KeyCode::G);
            SubmitKey(inputSystem, ImGuiKey_H, KeyCode::H);
            SubmitKey(inputSystem, ImGuiKey_I, KeyCode::I);
            SubmitKey(inputSystem, ImGuiKey_J, KeyCode::J);
            SubmitKey(inputSystem, ImGuiKey_K, KeyCode::K);
            SubmitKey(inputSystem, ImGuiKey_L, KeyCode::L);
            SubmitKey(inputSystem, ImGuiKey_M, KeyCode::M);
            SubmitKey(inputSystem, ImGuiKey_N, KeyCode::N);
            SubmitKey(inputSystem, ImGuiKey_O, KeyCode::O);
            SubmitKey(inputSystem, ImGuiKey_P, KeyCode::P);
            SubmitKey(inputSystem, ImGuiKey_Q, KeyCode::Q);
            SubmitKey(inputSystem, ImGuiKey_R, KeyCode::R);
            SubmitKey(inputSystem, ImGuiKey_S, KeyCode::S);
            SubmitKey(inputSystem, ImGuiKey_T, KeyCode::T);
            SubmitKey(inputSystem, ImGuiKey_U, KeyCode::U);
            SubmitKey(inputSystem, ImGuiKey_V, KeyCode::V);
            SubmitKey(inputSystem, ImGuiKey_W, KeyCode::W);
            SubmitKey(inputSystem, ImGuiKey_X, KeyCode::X);
            SubmitKey(inputSystem, ImGuiKey_Y, KeyCode::Y);
            SubmitKey(inputSystem, ImGuiKey_Z, KeyCode::Z);
            SubmitKey(inputSystem, ImGuiKey_0, KeyCode::Num0);
            SubmitKey(inputSystem, ImGuiKey_1, KeyCode::Num1);
            SubmitKey(inputSystem, ImGuiKey_2, KeyCode::Num2);
            SubmitKey(inputSystem, ImGuiKey_3, KeyCode::Num3);
            SubmitKey(inputSystem, ImGuiKey_4, KeyCode::Num4);
            SubmitKey(inputSystem, ImGuiKey_5, KeyCode::Num5);
            SubmitKey(inputSystem, ImGuiKey_6, KeyCode::Num6);
            SubmitKey(inputSystem, ImGuiKey_7, KeyCode::Num7);
            SubmitKey(inputSystem, ImGuiKey_8, KeyCode::Num8);
            SubmitKey(inputSystem, ImGuiKey_9, KeyCode::Num9);
            SubmitKey(inputSystem, ImGuiKey_Space, KeyCode::Space);
            SubmitKey(inputSystem, ImGuiKey_Enter, KeyCode::Enter);
            SubmitKey(inputSystem, ImGuiKey_Escape, KeyCode::Escape);
            SubmitKey(inputSystem, ImGuiKey_Tab, KeyCode::Tab);
            SubmitKey(inputSystem, ImGuiKey_Backspace, KeyCode::Backspace);
            SubmitKey(inputSystem, ImGuiKey_LeftArrow, KeyCode::Left);
            SubmitKey(inputSystem, ImGuiKey_RightArrow, KeyCode::Right);
            SubmitKey(inputSystem, ImGuiKey_UpArrow, KeyCode::Up);
            SubmitKey(inputSystem, ImGuiKey_DownArrow, KeyCode::Down);
            SubmitKey(inputSystem, ImGuiKey_LeftShift, KeyCode::LeftShift);
            SubmitKey(inputSystem, ImGuiKey_RightShift, KeyCode::RightShift);
            SubmitKey(inputSystem, ImGuiKey_LeftCtrl, KeyCode::LeftControl);
            SubmitKey(inputSystem, ImGuiKey_RightCtrl, KeyCode::RightControl);
            SubmitKey(inputSystem, ImGuiKey_LeftAlt, KeyCode::LeftAlt);
            SubmitKey(inputSystem, ImGuiKey_RightAlt, KeyCode::RightAlt);
        }

        (void)runtime.GetGameThreadSystem().SubmitInputSnapshot(inputSystem.CreateGameSnapshot());
    }
} // namespace ve
