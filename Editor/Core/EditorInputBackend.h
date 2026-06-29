#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Input/OSEvent.h"

#include <memory>

namespace ve::editor
{
    /// Owns platform-specific editor input integration and OS event routing decisions.
    class EditorInputBackend : public NonMovable
    {
    public:
        virtual ~EditorInputBackend() = default;

        [[nodiscard]] virtual ErrorCode Init(void* nativeWindowHandle) = 0;
        virtual void BeginOSEventFrame() noexcept = 0;
        virtual void StartFrame() = 0;
        [[nodiscard]] virtual bool OnOSEvent(const OSEvent& event) = 0;
        virtual void Shutdown() noexcept = 0;
        [[nodiscard]] virtual bool IsInitialized() const noexcept = 0;
        [[nodiscard]] virtual Int32 GetMouseDeltaX() const noexcept = 0;
        [[nodiscard]] virtual Int32 GetMouseDeltaY() const noexcept = 0;
    };

    [[nodiscard]] std::unique_ptr<EditorInputBackend> CreateWinEditorInputBackend();
    [[nodiscard]] std::unique_ptr<EditorInputBackend> CreateMacEditorInputBackend();
} // namespace ve::editor
