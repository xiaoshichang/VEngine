#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Input/OSEvent.h"

namespace ve::editor
{
    /// Owns Editor-side ImGui platform input integration and OS event routing decisions.
    class EditorInput : public NonMovable
    {
    public:
        EditorInput() = default;
        ~EditorInput();

        [[nodiscard]] ErrorCode Init(void* nativeWindowHandle);
        void BeginOSEventFrame() noexcept;
        void StartFrame();
        [[nodiscard]] bool OnOSEvent(const OSEvent& event);
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] Int32 GetMouseDeltaX() const noexcept;
        [[nodiscard]] Int32 GetMouseDeltaY() const noexcept;

    private:
        void StoreMousePosition(const OSEvent& event) noexcept;

        bool initialized_ = false;
        Int32 mouseX_ = 0;
        Int32 mouseY_ = 0;
        Int32 mouseDeltaX_ = 0;
        Int32 mouseDeltaY_ = 0;
        bool hasMousePosition_ = false;
    };
} // namespace ve::editor
