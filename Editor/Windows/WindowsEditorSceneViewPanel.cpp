#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Scene/SceneRenderExtractor.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace ve
{
    namespace
    {
        constexpr UInt64 SceneViewTextureId = 2;
        constexpr Float32 SceneViewOverlayMargin = 12.0f;
        constexpr Float32 SceneViewGizmoReservedSize = 112.0f;
        constexpr Float32 AxisGizmoPositiveLength = 34.0f;
        constexpr Float32 AxisGizmoNegativeLength = 17.0f;
        constexpr Float32 AxisGizmoMinimumProjectedScale = 0.24f;
        constexpr Float32 AxisGizmoEndpointRadius = 7.0f;
        constexpr Float32 AxisGizmoNegativeEndpointRadius = 3.0f;

        struct SceneViewAxis
        {
            Vector3 worldAxis;
            const char* label = "";
            ImU32 color = 0;
            ImU32 dimColor = 0;
        };

        struct SceneViewAxisDrawCommand
        {
            Vector3 viewDirection;
            const char* label = "";
            ImU32 color = 0;
            Float32 length = 0.0f;
            Float32 endpointRadius = 0.0f;
            bool drawLabel = false;
        };

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

        [[nodiscard]] Matrix44 Perspective(Float32 fovYRadians,
                                           Float32 aspectRatio,
                                           Float32 nearPlane,
                                           Float32 farPlane)
        {
            const Float32 f = 1.0f / std::tan(fovYRadians * 0.5f);
            Matrix44 matrix = Matrix44::Zero();
            matrix.Set(0, 0, f / aspectRatio);
            matrix.Set(1, 1, f);
            matrix.Set(2, 2, farPlane / (farPlane - nearPlane));
            matrix.Set(2, 3, (-nearPlane * farPlane) / (farPlane - nearPlane));
            matrix.Set(3, 2, 1.0f);
            return matrix;
        }

        [[nodiscard]] Quaternion BuildSceneViewCameraRotation(Float32 pitchRadians, Float32 yawRadians) noexcept
        {
            return Quaternion::FromEulerXYZ(pitchRadians, yawRadians, 0.0f);
        }

        [[nodiscard]] Matrix44 BuildSceneViewMatrix(const Vector3& position,
                                                    Float32 pitchRadians,
                                                    Float32 yawRadians) noexcept
        {
            const Quaternion rotation = BuildSceneViewCameraRotation(pitchRadians, yawRadians);
            return rotation.Conjugated().ToMatrix44() * Matrix44::Translation(-position);
        }

        [[nodiscard]] ImU32 ColorWithAlpha(ImU32 color, UInt8 alpha) noexcept
        {
            return (color & IM_COL32(255, 255, 255, 0)) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
        }

        [[nodiscard]] ImVec2 ProjectAxisToOverlay(const Vector3& viewDirection) noexcept
        {
            return ImVec2(viewDirection.GetX(), -viewDirection.GetY());
        }

        [[nodiscard]] Float32 ProjectedLength(const ImVec2& direction) noexcept
        {
            return std::sqrt((direction.x * direction.x) + (direction.y * direction.y));
        }

        [[nodiscard]] ImVec2 MakeAxisEndpoint(const ImVec2& center,
                                              const Vector3& viewDirection,
                                              Float32 axisLength) noexcept
        {
            const ImVec2 projected = ProjectAxisToOverlay(viewDirection);
            const Float32 projectedLength = ProjectedLength(projected);
            if (projectedLength <= 0.001f)
            {
                return center;
            }

            const Float32 visibleLength =
                axisLength * std::max(projectedLength, AxisGizmoMinimumProjectedScale);
            return ImVec2(center.x + ((projected.x / projectedLength) * visibleLength),
                          center.y + ((projected.y / projectedLength) * visibleLength));
        }

        void DrawSceneViewAxisCommand(ImDrawList& drawList,
                                      const ImVec2& center,
                                      const SceneViewAxisDrawCommand& command)
        {
            const ImVec2 endpoint = MakeAxisEndpoint(center, command.viewDirection, command.length);
            drawList.AddLine(center, endpoint, command.color, command.drawLabel ? 2.25f : 1.4f);
            drawList.AddCircleFilled(endpoint, command.endpointRadius, command.color);

            if (!command.drawLabel)
            {
                return;
            }

            const ImVec2 textSize = ImGui::CalcTextSize(command.label);
            const ImVec2 textPosition(endpoint.x - (textSize.x * 0.5f), endpoint.y - (textSize.y * 0.5f));
            drawList.AddText(textPosition, IM_COL32(255, 255, 255, 255), command.label);
        }

        void DrawSceneViewAxisGizmo(const ImVec2& imageMin,
                                    const ImVec2& imageMax,
                                    const Quaternion& cameraRotation)
        {
            const Float32 viewportWidth = imageMax.x - imageMin.x;
            const Float32 viewportHeight = imageMax.y - imageMin.y;
            if (viewportWidth < 64.0f || viewportHeight < 64.0f)
            {
                return;
            }

            const Float32 reservedSize = std::min(SceneViewGizmoReservedSize,
                                                  std::min(viewportWidth, viewportHeight) -
                                                      (SceneViewOverlayMargin * 2.0f));
            if (reservedSize < 48.0f)
            {
                return;
            }

            const ImVec2 reservedMin(imageMax.x - SceneViewOverlayMargin - reservedSize,
                                     imageMin.y + SceneViewOverlayMargin);
            const ImVec2 reservedMax(reservedMin.x + reservedSize, reservedMin.y + reservedSize);
            const ImVec2 center((reservedMin.x + reservedMax.x) * 0.5f, (reservedMin.y + reservedMax.y) * 0.5f);

            constexpr std::array<SceneViewAxis, 3> axes = {
                SceneViewAxis{Vector3::UnitX(), "X", IM_COL32(230, 74, 74, 255), IM_COL32(135, 52, 52, 120)},
                SceneViewAxis{Vector3::UnitY(), "Y", IM_COL32(86, 210, 98, 255), IM_COL32(54, 130, 64, 120)},
                SceneViewAxis{Vector3::UnitZ(), "Z", IM_COL32(84, 144, 245, 255), IM_COL32(52, 86, 150, 120)},
            };

            const Quaternion viewRotation = cameraRotation.Conjugated();
            std::array<SceneViewAxisDrawCommand, 6> drawCommands;
            SizeT commandCount = 0;
            for (const SceneViewAxis& axis : axes)
            {
                const Vector3 viewAxis = viewRotation.RotateVector(axis.worldAxis).Normalized();

                drawCommands[commandCount++] = SceneViewAxisDrawCommand{
                    viewAxis * -1.0f,
                    "",
                    axis.dimColor,
                    AxisGizmoNegativeLength,
                    AxisGizmoNegativeEndpointRadius,
                    false,
                };

                drawCommands[commandCount++] = SceneViewAxisDrawCommand{
                    viewAxis,
                    axis.label,
                    viewAxis.GetZ() > 0.0f ? ColorWithAlpha(axis.color, 190) : axis.color,
                    AxisGizmoPositiveLength,
                    AxisGizmoEndpointRadius,
                    true,
                };
            }

            std::sort(drawCommands.begin(),
                      drawCommands.end(),
                      [](const SceneViewAxisDrawCommand& left, const SceneViewAxisDrawCommand& right)
                      {
                          return left.viewDirection.GetZ() > right.viewDirection.GetZ();
                      });

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(imageMin, imageMax, true);

            drawList->AddCircleFilled(center, reservedSize * 0.42f, IM_COL32(12, 16, 20, 118));
            drawList->AddCircle(center, reservedSize * 0.42f, IM_COL32(98, 108, 122, 95), 0, 1.0f);
            drawList->AddCircleFilled(center, 3.0f, IM_COL32(218, 224, 232, 210));

            for (const SceneViewAxisDrawCommand& command : drawCommands)
            {
                DrawSceneViewAxisCommand(*drawList, center, command);
            }

            drawList->PopClipRect();
        }
    } // namespace

    void WindowsEditorPanels::DrawViewportImage(UInt64 textureId,
                                                const ImVec2& imageSize,
                                                const char* overlayText,
                                                bool showOverlayText)
    {
        ImGui::Image(static_cast<ImTextureID>(textureId), imageSize, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));

        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRect(imageMin, imageMax, ImGui::GetColorU32(ImVec4(0.20f, 0.22f, 0.24f, 1.0f)));

        if (showOverlayText && overlayText != nullptr && overlayText[0] != '\0')
        {
            const ImVec2 padding(8.0f, 6.0f);
            const ImVec2 textSize = ImGui::CalcTextSize(overlayText);
            const ImVec2 textMin(imageMin.x + 8.0f, imageMin.y + 8.0f);
            const ImVec2 textMax(textMin.x + textSize.x + padding.x * 2.0f,
                                 textMin.y + textSize.y + padding.y * 2.0f);
            drawList->AddRectFilled(textMin,
                                    textMax,
                                    ImGui::GetColorU32(ImVec4(0.02f, 0.025f, 0.03f, 0.78f)),
                                    4.0f);
            drawList->AddText(ImVec2(textMin.x + padding.x, textMin.y + padding.y),
                              ImGui::GetColorU32(ImGuiCol_Text),
                              overlayText);
        }
    }

    void WindowsEditorPanels::HandleSceneViewCameraInput(const ImVec2& imageSize)
    {
        (void)imageSize;

        ImGuiIO& io = ImGui::GetIO();
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            sceneViewLookActive_ = true;
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            sceneViewLookActive_ = false;
        }

        const Quaternion rotation =
            BuildSceneViewCameraRotation(sceneViewCamera_.pitchRadians, sceneViewCamera_.yawRadians);
        const Vector3 forward = rotation.RotateVector(Vector3::UnitZ()).Normalized();
        const Vector3 right = rotation.RotateVector(Vector3::UnitX()).Normalized();
        const Vector3 up = Vector3::UnitY();

        if (hovered && io.MouseWheel != 0.0f)
        {
            if (sceneViewLookActive_)
            {
                sceneViewCamera_.moveSpeed =
                    std::clamp(sceneViewCamera_.moveSpeed * std::pow(1.15f, io.MouseWheel), 0.1f, 80.0f);
            }
            else
            {
                sceneViewCamera_.position += forward * (io.MouseWheel * sceneViewCamera_.moveSpeed * 0.35f);
            }
        }

        if (!sceneViewLookActive_)
        {
            return;
        }

        ImGui::SetMouseCursor(ImGuiMouseCursor_None);

        constexpr Float32 lookSensitivity = 0.003f;
        sceneViewCamera_.yawRadians += io.MouseDelta.x * lookSensitivity;
        sceneViewCamera_.pitchRadians += io.MouseDelta.y * lookSensitivity;
        sceneViewCamera_.pitchRadians = std::clamp(sceneViewCamera_.pitchRadians, ToRadians(-85.0f), ToRadians(85.0f));

        Vector3 move = Vector3::Zero();
        if (ImGui::IsKeyDown(ImGuiKey_W))
        {
            move += forward;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S))
        {
            move -= forward;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D))
        {
            move += right;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A))
        {
            move -= right;
        }
        if (ImGui::IsKeyDown(ImGuiKey_E))
        {
            move += up;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q))
        {
            move -= up;
        }

        const Float32 deltaTime = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
        const Vector3 normalizedMove = move.Normalized();
        if (normalizedMove != Vector3::Zero())
        {
            sceneViewCamera_.position += normalizedMove * (sceneViewCamera_.moveSpeed * deltaTime);
        }
    }

    EditorViewportRenderRequest WindowsEditorPanels::MakeSceneViewRequest(EditorProjectService& projectService,
                                                                          EngineRuntime& runtime,
                                                                          UInt32 width,
                                                                          UInt32 height)
    {
        const Float32 aspectRatio =
            height > 0 ? static_cast<Float32>(width) / static_cast<Float32>(height) : (16.0f / 9.0f);
        SceneRenderSnapshot snapshot = ExtractSceneRenderSnapshot(
            projectService.GetActiveScene(), runtime.GetResourceManager(), nextViewportFrameId_++, aspectRatio);

        snapshot.hasMainCamera = true;
        snapshot.mainCamera.objectId = InvalidSceneObjectId;
        snapshot.mainCamera.viewMatrix = BuildSceneViewMatrix(
            sceneViewCamera_.position, sceneViewCamera_.pitchRadians, sceneViewCamera_.yawRadians);
        snapshot.mainCamera.projectionMatrix = Perspective(ToRadians(60.0f), aspectRatio, 0.05f, 1000.0f);
        snapshot.mainCamera.viewportRect = Vector4(0.0f, 0.0f, 1.0f, 1.0f);
        snapshot.mainCamera.clearColor = Vector4(0.05f, 0.07f, 0.10f, 1.0f);

        EditorViewportRenderRequest request;
        request.textureId = SceneViewTextureId;
        request.width = width;
        request.height = height;
        request.snapshot = std::move(snapshot);
        return request;
    }

    void WindowsEditorPanels::DrawSceneView(EditorProjectService& projectService, EngineRuntime& runtime)
    {
        const ImVec2 imageSize = GetViewportImageSize();
        DrawViewportImage(SceneViewTextureId, imageSize, nullptr, false);
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        const Quaternion cameraRotation =
            BuildSceneViewCameraRotation(sceneViewCamera_.pitchRadians, sceneViewCamera_.yawRadians);
        DrawSceneViewAxisGizmo(imageMin, imageMax, cameraRotation);
        HandleSceneViewCameraInput(imageSize);

        const ImGuiIO& io = ImGui::GetIO();
        const UInt32 width = ToTextureExtent(imageSize.x, io.DisplayFramebufferScale.x);
        const UInt32 height = ToTextureExtent(imageSize.y, io.DisplayFramebufferScale.y);
        viewportRequests_.push_back(MakeSceneViewRequest(projectService, runtime, width, height));
    }
} // namespace ve
