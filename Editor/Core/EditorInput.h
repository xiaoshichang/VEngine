#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Input/OSEvent.h"

#include <memory>

namespace ve::editor
{
    class EditorInputBackend;

    /// Cross-platform entry point for Editor-side platform input integration.
    class EditorInput : public NonMovable
    {
    public:
        EditorInput();
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
        std::unique_ptr<EditorInputBackend> backend_;
    };
} // namespace ve::editor
