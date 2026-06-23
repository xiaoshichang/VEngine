#pragma once

#include "Editor/Core/Gizmos.h"
#include "Editor/Panels/BasePanel.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <memory>

namespace ve::editor
{
    class Editor;

    class SceneViewPanel final : public BasePanel
    {
    public:
        SceneViewPanel();

        void Init(Editor& editor);
        void Render(Editor& editor, const ImVec2& position, const ImVec2& size);

        [[nodiscard]] const RenderTexture& GetSceneViewTexture() const noexcept;
        [[nodiscard]] RenderTexture& GetSceneViewTexture() noexcept;
        [[nodiscard]] RTCameraInitParam GetSceneViewCameraInitParam() const noexcept;
        [[nodiscard]] rhi::RhiFillMode GetFillMode() const noexcept;
        [[nodiscard]] bool IsGridEnabled() const noexcept;
        [[nodiscard]] Float32 GetGridOpacity() const noexcept;
        [[nodiscard]] Float32 GetGridUnitSize() const noexcept;
        [[nodiscard]] Gizmos& GetGizmos() noexcept;
        [[nodiscard]] const Gizmos& GetGizmos() const noexcept;
        [[nodiscard]] Matrix44 GetSceneViewCameraLocalToWorld() const noexcept;

    private:
        struct SceneViewCameraState
        {
            Vector3 position{0.0f, 4.0f, -10.0f};
            Float32 yawRadians = 0.0f;
            Float32 pitchRadians = 0.0f;
            Float32 targetYawRadians = 0.0f;
            Float32 targetPitchRadians = 0.0f;
            Float32 moveSpeed = 6.0f;
            Float32 lookSensitivity = 0.0025f;
            RTCameraProjectionMode projectionMode = RTCameraProjectionMode::Perspective;
            Float32 verticalFieldOfViewRadians = 1.0471975512f;
            Float32 orthographicSize = 5.0f;
            Float32 aspectRatio = 1.7777778f;
            Float32 nearClipPlane = 0.1f;
            Float32 farClipPlane = 1000.0f;
            rhi::RhiColor clearColor{0.05f, 0.07f, 0.10f, 1.0f};
        };

        struct SceneViewOverlayState
        {
            bool showAxis = true;
        };

        struct SceneViewGridState
        {
            bool enabled = true;
            Float32 opacity = 0.45f;
            Int32 unitSizeIndex = 1;
        };

        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;

        void RenderControlBar();
        void RenderCameraPopup();
        void RenderRenderPopup();
        void RenderOverlayPopup();
        void RenderGridPopup();
        void RenderGizmosPopup();
        void RenderSceneViewOverlays(const ImVec2& imageMin, const ImVec2& imageSize);
        void RenderAxisOverlay(const ImVec2& imageMin, const ImVec2& imageSize);
        void UpdateCameraFromInput(bool viewportHovered, bool viewportClicked, bool viewportRightClicked);
        [[nodiscard]] bool ApplyCameraLookSmoothing(Float32 deltaSeconds) noexcept;
        void UpdateSceneViewCamera();
        void RebuildSceneViewTexture(Editor& editor, WindowExtent extent);
        [[nodiscard]] RTCameraInitParam BuildCameraInitParam() const noexcept;
        [[nodiscard]] RTCameraUpdateParam BuildCameraUpdateParam() const noexcept;
        [[nodiscard]] Matrix44 BuildCameraLocalToWorld() const noexcept;
        [[nodiscard]] Vector3 GetForwardDirection() const noexcept;
        [[nodiscard]] Vector3 GetRightDirection() const noexcept;
        [[nodiscard]] static WindowExtent ToRenderTargetExtent(const ImVec2& size) noexcept;

        std::shared_ptr<RenderTexture> sceneViewTexture_;
        std::shared_ptr<RTCamera> sceneViewCamera_;
        WindowExtent renderTargetExtent_ = {};
        Editor* activeEditor_ = nullptr;
        SceneViewCameraState camera_;
        SceneViewOverlayState overlays_;
        SceneViewGridState grid_;
        Gizmos gizmos_;
        rhi::RhiFillMode fillMode_ = rhi::RhiFillMode::Solid;
        bool sceneViewFocused_ = false;
        bool cameraLookActive_ = false;
        bool skipNextMouseLookDelta_ = false;
    };
} // namespace ve::editor
