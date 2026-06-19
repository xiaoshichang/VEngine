#pragma once

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
        [[nodiscard]] RTCameraDesc GetSceneViewCameraDesc() const noexcept;
        [[nodiscard]] rhi::RhiFillMode GetFillMode() const noexcept;

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;

        void RenderControlBar();
        void RenderCameraPopup();
        void RenderRenderPopup();
        void UpdateCameraFromInput(bool viewportHovered, bool viewportClicked, bool viewportRightClicked);
        [[nodiscard]] bool ApplyCameraLookSmoothing(Float32 deltaSeconds) noexcept;
        void UpdateSceneViewCamera();
        void RebuildSceneViewTexture(Editor& editor, WindowExtent extent);
        [[nodiscard]] RTCameraDesc BuildCameraDesc() const noexcept;
        [[nodiscard]] Matrix44 BuildCameraLocalToWorld() const noexcept;
        [[nodiscard]] Vector3 GetForwardDirection() const noexcept;
        [[nodiscard]] Vector3 GetRightDirection() const noexcept;
        [[nodiscard]] static WindowExtent ToRenderTargetExtent(const ImVec2& size) noexcept;

        std::shared_ptr<RenderTexture> sceneViewTexture_;
        std::shared_ptr<RTCamera> sceneViewCamera_;
        WindowExtent renderTargetExtent_ = {};
        Editor* activeEditor_ = nullptr;
        Vector3 cameraPosition_{0.0f, 4.0f, -10.0f};
        Float32 cameraYawRadians_ = 0.0f;
        Float32 cameraPitchRadians_ = 0.0f;
        Float32 targetCameraYawRadians_ = 0.0f;
        Float32 targetCameraPitchRadians_ = 0.0f;
        Float32 cameraMoveSpeed_ = 6.0f;
        Float32 cameraLookSensitivity_ = 0.0025f;
        RTCameraProjectionMode cameraProjectionMode_ = RTCameraProjectionMode::Perspective;
        Float32 cameraVerticalFieldOfViewRadians_ = 1.0471975512f;
        Float32 cameraOrthographicSize_ = 5.0f;
        Float32 cameraAspectRatio_ = 1.7777778f;
        Float32 cameraNearClipPlane_ = 0.1f;
        Float32 cameraFarClipPlane_ = 1000.0f;
        rhi::RhiColor cameraClearColor_{0.05f, 0.07f, 0.10f, 1.0f};
        rhi::RhiFillMode fillMode_ = rhi::RhiFillMode::Solid;
        bool sceneViewFocused_ = false;
        bool cameraLookActive_ = false;
        bool skipNextMouseLookDelta_ = false;
    };
} // namespace ve::editor
