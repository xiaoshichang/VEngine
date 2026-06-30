#pragma once

#include "Editor/Core/EditorInputBackend.h"

namespace ve::editor
{
    class MacEditorInputBackend final : public EditorInputBackend
    {
    public:
        ~MacEditorInputBackend() override;

        [[nodiscard]] ErrorCode Init(void* nativeWindowHandle) override;
        void BeginOSEventFrame() noexcept override;
        void StartFrame() override;
        [[nodiscard]] bool OnOSEvent(const OSEvent& event) override;
        void Shutdown() noexcept override;
        [[nodiscard]] bool IsInitialized() const noexcept override;
        [[nodiscard]] Int32 GetMouseDeltaX() const noexcept override;
        [[nodiscard]] Int32 GetMouseDeltaY() const noexcept override;

    private:
        void StoreMousePosition(const OSEvent& event) noexcept;

        bool initialized_ = false;
        Int32 mouseX_ = 0;
        Int32 mouseY_ = 0;
        Int32 mouseDeltaX_ = 0;
        Int32 mouseDeltaY_ = 0;
        double lastFrameTimeSeconds_ = 0.0;
        int lastMouseCursor_ = -1;
        bool hasMousePosition_ = false;
        bool mouseCursorHidden_ = false;
        void* nativeView_ = nullptr;
    };
} // namespace ve::editor
