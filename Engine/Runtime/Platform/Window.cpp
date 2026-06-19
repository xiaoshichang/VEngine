#include "Engine/Runtime/Platform/Window.h"

#include "Engine/Runtime/Core/Error.h"

#if VE_PLATFORM_WINDOWS
#include "Engine/Runtime/Platform/Windows/Win32Window.h"
#endif

namespace ve
{
    Result<std::unique_ptr<Window>> Window::Create(const WindowDesc& desc)
    {
#if VE_PLATFORM_WINDOWS
        return Win32Window::CreatePlatformWindow(desc);
#else
        return Result<std::unique_ptr<Window>>::Failure(Error(ErrorCode::Unsupported, "This platform does not have a Window backend yet."));
#endif
    }
} // namespace ve
