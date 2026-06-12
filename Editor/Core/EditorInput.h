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
        void StartFrame();
        [[nodiscard]] bool OnOSEvent(const OSEvent& event);
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        bool initialized_ = false;
    };
} // namespace ve::editor
