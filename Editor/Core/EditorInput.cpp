#include "Editor/Core/EditorInput.h"

#include "Editor/Core/EditorInputBackend.h"
#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Logging/Log.h"

namespace ve::editor
{
    EditorInput::EditorInput() = default;

    EditorInput::~EditorInput()
    {
        Shutdown();
    }

    ErrorCode EditorInput::Init(void* nativeWindowHandle)
    {
        if (backend_ != nullptr)
        {
            return ErrorCode::InvalidState;
        }

#if VE_PLATFORM_WINDOWS
        backend_ = CreateWinEditorInputBackend();
#elif VE_PLATFORM_MACOS
        backend_ = CreateMacEditorInputBackend();
#else
        VE_LOG_WARN_CATEGORY("Editor", "Editor input backend is unsupported on this platform.");
        return ErrorCode::Unsupported;
#endif

        if (backend_ == nullptr)
        {
            return ErrorCode::Unsupported;
        }

        const ErrorCode initResult = backend_->Init(nativeWindowHandle);
        if (initResult != ErrorCode::None)
        {
            backend_.reset();
            return initResult;
        }

        return ErrorCode::None;
    }

    void EditorInput::BeginOSEventFrame() noexcept
    {
        if (backend_ == nullptr)
        {
            return;
        }

        backend_->BeginOSEventFrame();
    }

    void EditorInput::StartFrame()
    {
        if (backend_ == nullptr)
        {
            return;
        }

        backend_->StartFrame();
    }

    bool EditorInput::OnOSEvent(const OSEvent& event)
    {
        if (backend_ == nullptr)
        {
            return false;
        }

        return backend_->OnOSEvent(event);
    }

    void EditorInput::Shutdown() noexcept
    {
        if (backend_ == nullptr)
        {
            return;
        }

        backend_->Shutdown();
        backend_.reset();
    }

    bool EditorInput::IsInitialized() const noexcept
    {
        return backend_ != nullptr && backend_->IsInitialized();
    }

    Int32 EditorInput::GetMouseDeltaX() const noexcept
    {
        return backend_ != nullptr ? backend_->GetMouseDeltaX() : 0;
    }

    Int32 EditorInput::GetMouseDeltaY() const noexcept
    {
        return backend_ != nullptr ? backend_->GetMouseDeltaY() : 0;
    }
} // namespace ve::editor
