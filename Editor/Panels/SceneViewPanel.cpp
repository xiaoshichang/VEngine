#include "Editor/Panels/SceneViewPanel.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <array>
#include <imgui.h>

namespace ve::editor
{
    namespace
    {
        constexpr UInt32 MinSceneViewExtent = 1;
        constexpr float ControlButtonWidth = 82.0f;
        constexpr float ControlButtonHeight = 0.0f;
        constexpr float PopupWidth = 320.0f;
        constexpr float MaxPitchRadians = Math::HalfPi - 0.01f;
        constexpr float MaxMouseLookDelta = 128.0f;

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
    } // namespace

    SceneViewPanel::SceneViewPanel()
        : sceneViewTexture_(nullptr)
        , sceneViewCamera_(nullptr)
    {
        sceneViewCamera_ = std::make_shared<RTCamera>(BuildCameraDesc());
    }

    void SceneViewPanel::Init(Editor& editor)
    {
        if (sceneViewCamera_ == nullptr)
        {
            sceneViewCamera_ = std::make_shared<RTCamera>(BuildCameraDesc());
        }

        if (sceneViewTexture_ == nullptr)
        {
            RenderTextureDesc desc = {};
            desc.name = "EditorSceneViewTexture";
            desc.colorFormat = rhi::RhiFormat::Bgra8Unorm;
            sceneViewTexture_ = std::make_shared<RenderTexture>(desc);
            editor.KeepImGuiTextureAlive(sceneViewTexture_);
        }

        UpdateSceneViewCamera();
    }

    void SceneViewPanel::Render(Editor& editor, const ImVec2& position, const ImVec2& size)
    {
        activeEditor_ = &editor;
        BasePanel::Render(position, size);
        activeEditor_ = nullptr;
    }

    const RenderTexture& SceneViewPanel::GetSceneViewTexture() const noexcept
    {
        return *sceneViewTexture_;
    }

    RenderTexture& SceneViewPanel::GetSceneViewTexture() noexcept
    {
        return *sceneViewTexture_;
    }

    RTCameraDesc SceneViewPanel::GetSceneViewCameraDesc() const noexcept
    {
        return BuildCameraDesc();
    }

    rhi::RhiFillMode SceneViewPanel::GetFillMode() const noexcept
    {
        return fillMode_;
    }

    const char* SceneViewPanel::GetName() const noexcept
    {
        return "Scene View";
    }

    void SceneViewPanel::RenderContent()
    {
        VE_ASSERT_MESSAGE(activeEditor_ != nullptr, "SceneViewPanel::RenderContent requires SceneViewPanel::Render.");
        VE_ASSERT_MESSAGE(sceneViewTexture_ != nullptr, "SceneViewPanel requires Init before Render.");

        RenderControlBar();
        ImGui::Separator();

        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        const WindowExtent desiredExtent = ToRenderTargetExtent(canvasSize);
        bool textureRebuilt = false;
        if (desiredExtent.width != renderTargetExtent_.width || desiredExtent.height != renderTargetExtent_.height || !sceneViewTexture_->IsValid())
        {
            RebuildSceneViewTexture(*activeEditor_, desiredExtent);
            textureRebuilt = true;
        }

        cameraAspectRatio_ = static_cast<Float32>(desiredExtent.width) / static_cast<Float32>((std::max)(desiredExtent.height, MinSceneViewExtent));
        UpdateSceneViewCamera();

        const ImVec2 imageSize(static_cast<float>(desiredExtent.width), static_cast<float>(desiredExtent.height));
        void* resourceView = sceneViewTexture_->GetRenderResourceViewHandle();
        if (textureRebuilt || resourceView == nullptr)
        {
            ImGui::Button("Scene View texture pending", imageSize);
            UpdateCameraFromInput(ImGui::IsItemHovered(), ImGui::IsItemClicked(), ImGui::IsItemClicked(ImGuiMouseButton_Right));
            return;
        }

        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(resourceView))), imageSize);
        UpdateCameraFromInput(ImGui::IsItemHovered(), ImGui::IsItemClicked(), ImGui::IsItemClicked(ImGuiMouseButton_Right));
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
    }

    void SceneViewPanel::RenderCameraPopup()
    {
        ImGui::SetNextWindowSize(ImVec2(PopupWidth, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopup("SceneViewCameraPopup"))
        {
            return;
        }

        std::array<float, 3> position = ToFloat3(cameraPosition_);
        if (ImGui::DragFloat3("Position", position.data(), 0.05f, 0.0f, 0.0f, "%.3f"))
        {
            cameraPosition_ = ToVector3(position);
        }

        float yawDegrees = ToDegrees(cameraYawRadians_);
        if (ImGui::DragFloat("Yaw", &yawDegrees, 0.2f, 0.0f, 0.0f, "%.2f deg"))
        {
            cameraYawRadians_ = ToRadians(yawDegrees);
        }

        float pitchDegrees = ToDegrees(cameraPitchRadians_);
        if (ImGui::DragFloat("Pitch", &pitchDegrees, 0.2f, -89.0f, 89.0f, "%.2f deg"))
        {
            cameraPitchRadians_ = Clamp(ToRadians(pitchDegrees), -MaxPitchRadians, MaxPitchRadians);
        }

        ImGui::DragFloat("Move Speed", &cameraMoveSpeed_, 0.05f, 0.1f, 100.0f, "%.2f");
        ImGui::DragFloat("Look Sensitivity", &cameraLookSensitivity_, 0.0001f, 0.0001f, 0.1f, "%.4f");

        int projectionMode = cameraProjectionMode_ == RTCameraProjectionMode::Perspective ? 0 : 1;
        const char* projectionModes[] = {"Perspective", "Orthographic"};
        if (ImGui::Combo("Projection", &projectionMode, projectionModes, IM_ARRAYSIZE(projectionModes)))
        {
            cameraProjectionMode_ = projectionMode == 0 ? RTCameraProjectionMode::Perspective : RTCameraProjectionMode::Orthographic;
        }

        ImGui::DragFloat("FOV Radians", &cameraVerticalFieldOfViewRadians_, 0.01f, 0.001f, Math::Pi, "%.3f");
        ImGui::DragFloat("Ortho Size", &cameraOrthographicSize_, 0.1f, 0.001f, 1000.0f, "%.3f");
        ImGui::DragFloat("Aspect", &cameraAspectRatio_, 0.01f, 0.001f, 100.0f, "%.3f");
        ImGui::DragFloat("Near", &cameraNearClipPlane_, 0.01f, 0.001f, 1000.0f, "%.3f");
        ImGui::DragFloat("Far", &cameraFarClipPlane_, 1.0f, 0.001f, 100000.0f, "%.3f");

        std::array<float, 4> clearColor = ToFloat4(cameraClearColor_);
        if (ImGui::ColorEdit4("Clear Color", clearColor.data()))
        {
            cameraClearColor_ = ToRhiColor(clearColor);
        }

        UpdateSceneViewCamera();
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

    void SceneViewPanel::UpdateCameraFromInput(bool viewportHovered, bool viewportClicked, bool viewportRightClicked)
    {
        const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        const bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                                  ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
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
            }
            else
            {
                const float mouseDeltaX = Clamp(io.MouseDelta.x, -MaxMouseLookDelta, MaxMouseLookDelta);
                const float mouseDeltaY = Clamp(io.MouseDelta.y, -MaxMouseLookDelta, MaxMouseLookDelta);
                if (mouseDeltaX != 0.0f || mouseDeltaY != 0.0f)
                {
                    cameraYawRadians_ += mouseDeltaX * cameraLookSensitivity_;
                    cameraPitchRadians_ = Clamp(cameraPitchRadians_ - (mouseDeltaY * cameraLookSensitivity_), -MaxPitchRadians, MaxPitchRadians);
                    cameraChanged = true;
                }
            }
        }

        Vector3 moveDirection = Vector3::Zero();
        moveDirection += GetForwardDirection() * GetInputAxis(ImGuiKey_W, ImGuiKey_S);
        moveDirection += GetRightDirection() * GetInputAxis(ImGuiKey_D, ImGuiKey_A);
        moveDirection += Vector3::UnitY() * GetInputAxis(ImGuiKey_E, ImGuiKey_Q);

        if (moveDirection.LengthSquared() > Math::DefaultEpsilon)
        {
            const float speedMultiplier = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift) ? 3.0f : 1.0f;
            cameraPosition_ += moveDirection.Normalized() * (cameraMoveSpeed_ * speedMultiplier * io.DeltaTime);
            cameraChanged = true;
        }

        if (cameraChanged)
        {
            UpdateSceneViewCamera();
        }
    }

    void SceneViewPanel::UpdateSceneViewCamera()
    {
        if (sceneViewCamera_ != nullptr)
        {
            sceneViewCamera_->SetDesc(BuildCameraDesc());
        }
    }

    void SceneViewPanel::RebuildSceneViewTexture(Editor& editor, WindowExtent extent)
    {
        VE_ASSERT_SCENE_THREAD();

        sceneViewTexture_->Resize(extent);
        sceneViewTexture_->InitRenderResource(editor.GetRenderSystem());
        renderTargetExtent_ = extent;
    }

    RTCameraDesc SceneViewPanel::BuildCameraDesc() const noexcept
    {
        RTCameraDesc desc = {};
        desc.primary = false;
        desc.projectionMode = cameraProjectionMode_;
        desc.verticalFieldOfViewRadians = cameraVerticalFieldOfViewRadians_;
        desc.orthographicSize = cameraOrthographicSize_;
        desc.aspectRatio = (std::max)(cameraAspectRatio_, 0.001f);
        desc.nearClipPlane = cameraNearClipPlane_;
        desc.farClipPlane = (std::max)(cameraFarClipPlane_, cameraNearClipPlane_ + 0.001f);
        desc.clearColor = cameraClearColor_;
        desc.localToWorld = BuildCameraLocalToWorld();
        return desc;
    }

    Matrix44 SceneViewPanel::BuildCameraLocalToWorld() const noexcept
    {
        return Matrix44::Translation(cameraPosition_) * Matrix44::RotationY(cameraYawRadians_) * Matrix44::RotationX(cameraPitchRadians_);
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
