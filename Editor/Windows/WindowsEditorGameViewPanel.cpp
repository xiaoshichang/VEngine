#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Application/EngineRuntime.h"
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
        viewportRequests_.push_back(std::move(request));
    }
} // namespace ve
