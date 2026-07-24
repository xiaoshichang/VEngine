#include "Editor/Panels/SceneViewPanel/SceneViewPanel.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <utility>

namespace ve::editor
{
    namespace
    {
        constexpr UInt32 MinSceneViewExtent = 1;
        constexpr float ControlButtonWidth = 82.0f;
        constexpr float ControlButtonHeight = 0.0f;
        constexpr float PopupWidth = 320.0f;
        constexpr float GridPopupWidth = 340.0f;
        constexpr float GizmosPopupWidth = 280.0f;
        constexpr float MaxPitchRadians = Math::HalfPi - 0.01f;
        constexpr float MaxMouseLookDelta = 128.0f;
        constexpr float CameraLookSmoothingSpeed = 48.0f;
        constexpr float AxisOverlaySize = 96.0f;
        constexpr float AxisOverlayPadding = 14.0f;
        constexpr float AxisOverlayRadius = 28.0f;
        constexpr float AxisOverlayLineThickness = 2.5f;
        constexpr float AxisOverlayDotRadius = 4.0f;
        constexpr Float32 GridUnitSizes[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};

        struct AxisOverlayEntry
        {
            const char* label = "";
            Vector3 direction = Vector3::Zero();
            ImU32 color = 0;
            float depth = 0.0f;
        };

        [[nodiscard]] std::array<float, 3> ToFloat3(const Vector3& value) noexcept
        {
            return {value.GetX(), value.GetY(), value.GetZ()};
        }

        [[nodiscard]] Vector3 ToVector3(const std::array<float, 3>& value) noexcept
        {
            return Vector3(value[0], value[1], value[2]);
        }

        [[nodiscard]] std::array<float, 4> ToFloat4(const rhi::RhiColor& value) noexcept
        {
            return {value.r, value.g, value.b, value.a};
        }

        [[nodiscard]] rhi::RhiColor ToRhiColor(const std::array<float, 4>& value) noexcept
        {
            return rhi::RhiColor{value[0], value[1], value[2], value[3]};
        }

        [[nodiscard]] float GetInputAxis(ImGuiKey positiveKey, ImGuiKey negativeKey) noexcept
        {
            float value = 0.0f;
            if (ImGui::IsKeyDown(positiveKey))
            {
                value += 1.0f;
            }
            if (ImGui::IsKeyDown(negativeKey))
            {
                value -= 1.0f;
            }
            return value;
        }

        [[nodiscard]] ImVec2 ProjectAxisDirection(const Vector3& direction, const ImVec2& origin) noexcept
        {
            return ImVec2(origin.x + (direction.GetX() * AxisOverlayRadius), origin.y - (direction.GetY() * AxisOverlayRadius));
        }

        [[nodiscard]] bool AxisDepthLess(const AxisOverlayEntry& left, const AxisOverlayEntry& right) noexcept
        {
            return left.depth < right.depth;
        }

        [[nodiscard]] float GetGridUnitSizeByIndex(Int32 index) noexcept
        {
            const Int32 clampedIndex = std::clamp<Int32>(index, 0, static_cast<Int32>(IM_ARRAYSIZE(GridUnitSizes) - 1));
            return GridUnitSizes[clampedIndex];
        }
    } // namespace

    SceneViewPanel::SceneViewPanel()
        : sceneViewTexture_(nullptr)
        , sceneViewCamera_(nullptr)
        , sceneViewState_(std::make_shared<RenderViewState>(RenderViewStateDesc{"EditorSceneView", 2560}))
    {
        sceneViewCamera_ = std::make_shared<RTCamera>(BuildCameraInitParam());
    }

    void SceneViewPanel::Init(Editor& editor)
    {
        editor_ = &editor;
        if (sceneViewCamera_ == nullptr)
        {
            sceneViewCamera_ = std::make_shared<RTCamera>(BuildCameraInitParam());
        }

        if (sceneViewTexture_ == nullptr)
        {
            RenderTextureDesc desc = {};
            desc.name = "EditorSceneViewTexture";
            desc.colorFormat = rhi::RhiFormat::Bgra8Unorm;
            sceneViewTexture_ = std::make_shared<RenderTexture>(desc);
        }

        UpdateSceneViewCamera();
    }

    const RenderTexture& SceneViewPanel::GetSceneViewTexture() const noexcept
    {
        return *sceneViewTexture_;
    }

    RenderTexture& SceneViewPanel::GetSceneViewTexture() noexcept
    {
        return *sceneViewTexture_;
    }

    RTCameraInitParam SceneViewPanel::GetSceneViewCameraInitParam() const noexcept
    {
        return BuildCameraInitParam();
    }

    rhi::RhiFillMode SceneViewPanel::GetFillMode() const noexcept
    {
        return fillMode_;
    }

    bool SceneViewPanel::IsGridEnabled() const noexcept
    {
        return grid_.enabled;
    }

    Float32 SceneViewPanel::GetGridOpacity() const noexcept
    {
        return grid_.opacity;
    }

    Float32 SceneViewPanel::GetGridUnitSize() const noexcept
    {
        return GetGridUnitSizeByIndex(grid_.unitSizeIndex);
    }

    Gizmos& SceneViewPanel::GetGizmos() noexcept
    {
        return gizmos_;
    }

    const Gizmos& SceneViewPanel::GetGizmos() const noexcept
    {
        return gizmos_;
    }

    Matrix44 SceneViewPanel::GetSceneViewCameraLocalToWorld() const noexcept
    {
        return BuildCameraLocalToWorld();
    }

    std::shared_ptr<RenderViewState> SceneViewPanel::GetRenderViewState() const noexcept
    {
        return sceneViewState_;
    }

    const char* SceneViewPanel::GetName() const noexcept
    {
        return "Scene View";
    }

    void SceneViewPanel::RenderContent()
    {
        VE_ASSERT_MESSAGE(editor_ != nullptr, "SceneViewPanel requires Init before Render.");
        VE_ASSERT_MESSAGE(sceneViewTexture_ != nullptr, "SceneViewPanel requires Init before Render.");

        RenderControlBar();
        ImGui::Separator();

        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        const WindowExtent desiredExtent = ToRenderTargetExtent(canvasSize);
        if (desiredExtent.width != renderTargetExtent_.width || desiredExtent.height != renderTargetExtent_.height || !sceneViewTexture_->IsValid())
        {
            RebuildSceneViewTexture(*editor_, desiredExtent);
        }

        UpdateSceneViewCamera();

        const ImVec2 imageSize(static_cast<float>(desiredExtent.width), static_cast<float>(desiredExtent.height));
        void* resourceView = sceneViewTexture_->GetRenderResourceViewHandle();
        if (resourceView == nullptr)
        {
            ImGui::Button("Scene View texture pending", imageSize);
            UpdateCameraFromInput(ImGui::IsItemHovered(), ImGui::IsItemClicked(), ImGui::IsItemClicked(ImGuiMouseButton_Right));
            return;
        }

        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(resourceView))), imageSize);
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const bool viewportHovered = ImGui::IsItemHovered();
        const bool viewportClicked = ImGui::IsItemClicked();
        const bool viewportRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        UpdateCameraFromInput(viewportHovered, viewportClicked, viewportRightClicked);
        RenderSceneViewOverlays(imageMin, imageSize);
    }

    void SceneViewPanel::RenderControlBar()
    {
        if (ImGui::Button("Camera", ImVec2(ControlButtonWidth, ControlButtonHeight)))
        {
            ImGui::OpenPopup("SceneViewCameraPopup");
        }
        RenderCameraPopup();

        ImGui::SameLine();
        if (ImGui::Button("Render", ImVec2(ControlButtonWidth, ControlButtonHeight)))
        {
            ImGui::OpenPopup("SceneViewRenderPopup");
        }
        RenderRenderPopup();

        ImGui::SameLine();
        if (ImGui::Button("Overlay", ImVec2(ControlButtonWidth, ControlButtonHeight)))
        {
            ImGui::OpenPopup("SceneViewOverlayPopup");
        }
        RenderOverlayPopup();

        ImGui::SameLine();
        if (ImGui::Button("Grid", ImVec2(ControlButtonWidth, ControlButtonHeight)))
        {
            ImGui::OpenPopup("SceneViewGridPopup");
        }
        RenderGridPopup();

        ImGui::SameLine();
        if (ImGui::Button("Gizmos", ImVec2(ControlButtonWidth, ControlButtonHeight)))
        {
            ImGui::OpenPopup("SceneViewGizmosPopup");
        }
        RenderGizmosPopup();
    }

    void SceneViewPanel::RenderCameraPopup()
    {
        ImGui::SetNextWindowSize(ImVec2(PopupWidth, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopup("SceneViewCameraPopup"))
        {
            return;
        }

        std::array<float, 3> position = ToFloat3(camera_.position);
        if (ImGui::DragFloat3("Position", position.data(), 0.05f, 0.0f, 0.0f, "%.3f"))
        {
            camera_.position = ToVector3(position);
        }

        float yawDegrees = ToDegrees(camera_.yawRadians);
        if (ImGui::DragFloat("Yaw", &yawDegrees, 0.2f, 0.0f, 0.0f, "%.2f deg"))
        {
            camera_.yawRadians = ToRadians(yawDegrees);
            camera_.targetYawRadians = camera_.yawRadians;
        }

        float pitchDegrees = ToDegrees(camera_.pitchRadians);
        if (ImGui::DragFloat("Pitch", &pitchDegrees, 0.2f, -89.0f, 89.0f, "%.2f deg"))
        {
            camera_.pitchRadians = Clamp(ToRadians(pitchDegrees), -MaxPitchRadians, MaxPitchRadians);
            camera_.targetPitchRadians = camera_.pitchRadians;
        }

        ImGui::DragFloat("Move Speed", &camera_.moveSpeed, 0.05f, 0.1f, 100.0f, "%.2f");
        ImGui::DragFloat("Look Sensitivity", &camera_.lookSensitivity, 0.0001f, 0.0001f, 0.1f, "%.4f");

        int projectionMode = camera_.projectionMode == RTCameraProjectionMode::Perspective ? 0 : 1;
        const char* projectionModes[] = {"Perspective", "Orthographic"};
        if (ImGui::Combo("Projection", &projectionMode, projectionModes, IM_ARRAYSIZE(projectionModes)))
        {
            camera_.projectionMode = projectionMode == 0 ? RTCameraProjectionMode::Perspective : RTCameraProjectionMode::Orthographic;
        }

        ImGui::DragFloat("FOV Radians", &camera_.verticalFieldOfViewRadians, 0.01f, 0.001f, Math::Pi, "%.3f");
        ImGui::DragFloat("Ortho Size", &camera_.orthographicSize, 0.1f, 0.001f, 1000.0f, "%.3f");
        ImGui::Checkbox("Auto Aspect", &camera_.automaticAspectRatio);
        if (!camera_.automaticAspectRatio)
        {
            ImGui::DragFloat("Aspect Override", &camera_.aspectRatio, 0.01f, 0.001f, 100.0f, "%.3f");
        }
        ImGui::DragFloat("Near", &camera_.nearClipPlane, 0.01f, 0.001f, 1000.0f, "%.3f");
        ImGui::DragFloat("Far", &camera_.farClipPlane, 1.0f, 0.001f, 100000.0f, "%.3f");

        if (ImGui::Button("Reset Camera"))
        {
            camera_ = SceneViewCameraState{};
        }

        std::array<float, 4> clearColor = ToFloat4(camera_.clearColor);
        if (ImGui::ColorEdit4("Clear Color", clearColor.data()))
        {
            camera_.clearColor = ToRhiColor(clearColor);
        }

        UpdateSceneViewCamera();
        ImGui::EndPopup();
    }

    void SceneViewPanel::RenderGizmosPopup()
    {
        ImGui::SetNextWindowSize(ImVec2(GizmosPopupWidth, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopup("SceneViewGizmosPopup"))
        {
            return;
        }

        GizmoComponentVisibility& visibility = gizmos_.GetComponentVisibility();
        ImGui::Checkbox("Camera", &visibility.camera);
        ImGui::Checkbox("Light", &visibility.light);
        ImGui::EndPopup();
    }

    void SceneViewPanel::RenderRenderPopup()
    {
        ImGui::SetNextWindowSize(ImVec2(PopupWidth, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopup("SceneViewRenderPopup"))
        {
            return;
        }

        bool wireframe = fillMode_ == rhi::RhiFillMode::Wireframe;
        if (ImGui::Checkbox("Wireframe", &wireframe))
        {
            fillMode_ = wireframe ? rhi::RhiFillMode::Wireframe : rhi::RhiFillMode::Solid;
        }

        ImGui::EndPopup();
    }

    void SceneViewPanel::RenderOverlayPopup()
    {
        ImGui::SetNextWindowSize(ImVec2(PopupWidth, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopup("SceneViewOverlayPopup"))
        {
            return;
        }

        ImGui::Checkbox("Axis", &overlays_.showAxis);
        ImGui::EndPopup();
    }

    void SceneViewPanel::RenderGridPopup()
    {
        ImGui::SetNextWindowSize(ImVec2(GridPopupWidth, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopup("SceneViewGridPopup"))
        {
            return;
        }

        ImGui::Checkbox("Enable Grid", &grid_.enabled);

        ImGui::BeginDisabled(!grid_.enabled);
        ImGui::SliderFloat("Opacity", &grid_.opacity, 0.0f, 1.0f, "%.2f");

        int unitSizeIndex = grid_.unitSizeIndex;
        const char* unitLabels[] = {"0.25", "0.5", "1", "2", "4", "8"};
        if (ImGui::Combo("Unit Size", &unitSizeIndex, unitLabels, IM_ARRAYSIZE(unitLabels)))
        {
            grid_.unitSizeIndex = unitSizeIndex;
        }
        ImGui::EndDisabled();

        ImGui::EndPopup();
    }

    void SceneViewPanel::RenderSceneViewOverlays(const ImVec2& imageMin, const ImVec2& imageSize)
    {
        if (overlays_.showAxis)
        {
            RenderAxisOverlay(imageMin, imageSize);
        }
    }

    void SceneViewPanel::RenderAxisOverlay(const ImVec2& imageMin, const ImVec2& imageSize)
    {
        if (imageSize.x <= AxisOverlaySize || imageSize.y <= AxisOverlaySize)
        {
            return;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList == nullptr)
        {
            return;
        }

        const Matrix44 cameraLocalToWorld = BuildCameraLocalToWorld();
        const Vector3 cameraRight = cameraLocalToWorld.TransformDirection(Vector3::UnitX()).Normalized();
        const Vector3 cameraUp = cameraLocalToWorld.TransformDirection(Vector3::UnitY()).Normalized();
        const Vector3 cameraForward = cameraLocalToWorld.TransformDirection(Vector3::UnitZ()).Normalized();

        AxisOverlayEntry axes[] = {
            AxisOverlayEntry{
                "X",
                Vector3(Vector3::Dot(Vector3::UnitX(), cameraRight), Vector3::Dot(Vector3::UnitX(), cameraUp), Vector3::Dot(Vector3::UnitX(), cameraForward)),
                IM_COL32(225, 72, 72, 255),
                0.0f},
            AxisOverlayEntry{
                "Y",
                Vector3(Vector3::Dot(Vector3::UnitY(), cameraRight), Vector3::Dot(Vector3::UnitY(), cameraUp), Vector3::Dot(Vector3::UnitY(), cameraForward)),
                IM_COL32(92, 190, 92, 255),
                0.0f},
            AxisOverlayEntry{
                "Z",
                Vector3(Vector3::Dot(Vector3::UnitZ(), cameraRight), Vector3::Dot(Vector3::UnitZ(), cameraUp), Vector3::Dot(Vector3::UnitZ(), cameraForward)),
                IM_COL32(86, 132, 235, 255),
                0.0f},
        };

        for (AxisOverlayEntry& axis : axes)
        {
            axis.depth = -axis.direction.GetZ();
        }
        std::sort(std::begin(axes), std::end(axes), AxisDepthLess);

        const ImVec2 origin(imageMin.x + imageSize.x - AxisOverlayPadding - (AxisOverlaySize * 0.5f),
                            imageMin.y + AxisOverlayPadding + (AxisOverlaySize * 0.5f));
        const ImU32 panelColor = IM_COL32(24, 28, 34, 150);
        const ImU32 borderColor = IM_COL32(255, 255, 255, 36);
        const ImVec2 panelMin(origin.x - (AxisOverlaySize * 0.5f), origin.y - (AxisOverlaySize * 0.5f));
        const ImVec2 panelMax(origin.x + (AxisOverlaySize * 0.5f), origin.y + (AxisOverlaySize * 0.5f));
        drawList->AddRectFilled(panelMin, panelMax, panelColor, 6.0f);
        drawList->AddRect(panelMin, panelMax, borderColor, 6.0f);

        for (const AxisOverlayEntry& axis : axes)
        {
            const ImVec2 end = ProjectAxisDirection(axis.direction, origin);
            const float depthAlpha = 0.55f + (Clamp(axis.depth, -1.0f, 1.0f) + 1.0f) * 0.225f;
            const ImU32 color = (axis.color & IM_COL32(255, 255, 255, 0)) | (static_cast<ImU32>(255.0f * depthAlpha) << IM_COL32_A_SHIFT);

            drawList->AddLine(origin, end, color, AxisOverlayLineThickness);
            drawList->AddCircleFilled(end, AxisOverlayDotRadius, color);
            drawList->AddText(ImVec2(end.x + 5.0f, end.y - 7.0f), color, axis.label);
        }
    }

    void SceneViewPanel::UpdateCameraFromInput(bool viewportHovered, bool viewportClicked, bool viewportRightClicked)
    {
        const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        const bool mouseClicked =
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
        if (popupOpen || !ImGui::IsWindowFocused() || (!viewportHovered && mouseClicked))
        {
            sceneViewFocused_ = false;
            cameraLookActive_ = false;
        }

        if (viewportClicked || viewportRightClicked)
        {
            sceneViewFocused_ = true;
        }

        if (viewportRightClicked)
        {
            cameraLookActive_ = true;
            skipNextMouseLookDelta_ = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            cameraLookActive_ = false;
            skipNextMouseLookDelta_ = false;
        }

        if (!sceneViewFocused_ || popupOpen || (ImGui::IsAnyItemActive() && !cameraLookActive_))
        {
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        bool cameraChanged = false;

        if (cameraLookActive_)
        {
            if (skipNextMouseLookDelta_)
            {
                skipNextMouseLookDelta_ = false;
                camera_.targetYawRadians = camera_.yawRadians;
                camera_.targetPitchRadians = camera_.pitchRadians;
            }
            else
            {
                const float mouseDeltaX = Clamp(static_cast<float>(editor_->GetInput().GetMouseDeltaX()), -MaxMouseLookDelta, MaxMouseLookDelta);
                const float mouseDeltaY = Clamp(static_cast<float>(editor_->GetInput().GetMouseDeltaY()), -MaxMouseLookDelta, MaxMouseLookDelta);
                if (mouseDeltaX != 0.0f || mouseDeltaY != 0.0f)
                {
                    camera_.targetYawRadians += mouseDeltaX * camera_.lookSensitivity;
                    camera_.targetPitchRadians = Clamp(camera_.targetPitchRadians - (mouseDeltaY * camera_.lookSensitivity), -MaxPitchRadians, MaxPitchRadians);
                }
            }
        }

        cameraChanged = ApplyCameraLookSmoothing(io.DeltaTime) || cameraChanged;

        Vector3 moveDirection = Vector3::Zero();
        moveDirection += GetForwardDirection() * GetInputAxis(ImGuiKey_W, ImGuiKey_S);
        moveDirection += GetRightDirection() * GetInputAxis(ImGuiKey_D, ImGuiKey_A);
        moveDirection += Vector3::UnitY() * GetInputAxis(ImGuiKey_E, ImGuiKey_Q);

        if (moveDirection.LengthSquared() > Math::DefaultEpsilon)
        {
            const float speedMultiplier = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift) ? 3.0f : 1.0f;
            camera_.position += moveDirection.Normalized() * (camera_.moveSpeed * speedMultiplier * io.DeltaTime);
            cameraChanged = true;
        }

        if (cameraChanged)
        {
            UpdateSceneViewCamera();
        }
    }

    bool SceneViewPanel::ApplyCameraLookSmoothing(Float32 deltaSeconds) noexcept
    {
        const Float32 clampedDeltaSeconds = Clamp(deltaSeconds, 0.0f, 1.0f / 15.0f);
        const Float32 smoothingFactor = Clamp(clampedDeltaSeconds * CameraLookSmoothingSpeed, 0.0f, 1.0f);
        const Float32 previousYaw = camera_.yawRadians;
        const Float32 previousPitch = camera_.pitchRadians;

        camera_.yawRadians = Lerp(camera_.yawRadians, camera_.targetYawRadians, smoothingFactor);
        camera_.pitchRadians = Lerp(camera_.pitchRadians, camera_.targetPitchRadians, smoothingFactor);

        return !NearlyEqual(camera_.yawRadians, previousYaw) || !NearlyEqual(camera_.pitchRadians, previousPitch);
    }

    void SceneViewPanel::UpdateSceneViewCamera()
    {
        if (sceneViewCamera_ != nullptr)
        {
            sceneViewCamera_->ApplyUpdateParam(BuildCameraUpdateParam());
        }
    }

    void SceneViewPanel::RebuildSceneViewTexture(Editor& editor, WindowExtent extent)
    {
        VE_ASSERT_SCENE_THREAD();

        RenderTextureDesc desc = {};
        desc.name = sceneViewTexture_->GetName();
        desc.extent = extent;
        desc.colorFormat = sceneViewTexture_->GetColorFormat();

        auto renderTexture = std::make_shared<RenderTexture>(std::move(desc));
        renderTexture->InitRenderResource(editor.GetRenderSystem());
        sceneViewTexture_ = std::move(renderTexture);
        renderTargetExtent_ = extent;
    }

    RTCameraInitParam SceneViewPanel::BuildCameraInitParam() const noexcept
    {
        RTCameraInitParam initParam = {};
        initParam.projectionMode = camera_.projectionMode;
        initParam.verticalFieldOfViewRadians = camera_.verticalFieldOfViewRadians;
        initParam.orthographicSize = camera_.orthographicSize;
        initParam.automaticAspectRatio = camera_.automaticAspectRatio;
        initParam.aspectRatio = (std::max)(camera_.aspectRatio, 0.001f);
        initParam.nearClipPlane = camera_.nearClipPlane;
        initParam.farClipPlane = (std::max)(camera_.farClipPlane, camera_.nearClipPlane + 0.001f);
        initParam.clearColor = camera_.clearColor;
        initParam.localToWorld = BuildCameraLocalToWorld();
        return initParam;
    }

    RTCameraUpdateParam SceneViewPanel::BuildCameraUpdateParam() const noexcept
    {
        RTCameraUpdateParam updateParam = {};
        updateParam.dirtyFlags = RTCameraDirtyFlags::All;
        updateParam.projectionMode = camera_.projectionMode;
        updateParam.verticalFieldOfViewRadians = camera_.verticalFieldOfViewRadians;
        updateParam.orthographicSize = camera_.orthographicSize;
        updateParam.automaticAspectRatio = camera_.automaticAspectRatio;
        updateParam.aspectRatio = (std::max)(camera_.aspectRatio, 0.001f);
        updateParam.nearClipPlane = camera_.nearClipPlane;
        updateParam.farClipPlane = (std::max)(camera_.farClipPlane, camera_.nearClipPlane + 0.001f);
        updateParam.clearColor = camera_.clearColor;
        updateParam.localToWorld = BuildCameraLocalToWorld();
        return updateParam;
    }

    Matrix44 SceneViewPanel::BuildCameraLocalToWorld() const noexcept
    {
        return Matrix44::Translation(camera_.position) * Matrix44::RotationY(camera_.yawRadians) * Matrix44::RotationX(camera_.pitchRadians);
    }

    Vector3 SceneViewPanel::GetForwardDirection() const noexcept
    {
        return BuildCameraLocalToWorld().TransformDirection(Vector3::UnitZ()).Normalized();
    }

    Vector3 SceneViewPanel::GetRightDirection() const noexcept
    {
        return BuildCameraLocalToWorld().TransformDirection(Vector3::UnitX()).Normalized();
    }

    WindowExtent SceneViewPanel::ToRenderTargetExtent(const ImVec2& size) noexcept
    {
        const float width = (std::max)(size.x, static_cast<float>(MinSceneViewExtent));
        const float height = (std::max)(size.y, static_cast<float>(MinSceneViewExtent));
        return WindowExtent{static_cast<UInt32>(width), static_cast<UInt32>(height)};
    }
} // namespace ve::editor
