#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Platform.h"

#if VE_PLATFORM_APPLE
extern "C"
{
    void* objc_autoreleasePoolPush(void);
    void objc_autoreleasePoolPop(void* context);
}
#endif

namespace ve
{
    /// Drains Objective-C autoreleased temporaries on engine-owned threads.
    ///
    /// AppKit installs pools around its own event dispatch, but VEngine owns long-running C++ loops for Main, Scene,
    /// and Render work. Apple platform code that touches Cocoa or Metal from those loops needs an explicit pool.
    class PlatformAutoreleasePool final : public NonCopyable
    {
    public:
        PlatformAutoreleasePool() noexcept
        {
#if VE_PLATFORM_APPLE
            context_ = objc_autoreleasePoolPush();
#endif
        }

        ~PlatformAutoreleasePool() noexcept
        {
#if VE_PLATFORM_APPLE
            objc_autoreleasePoolPop(context_);
#endif
        }

    private:
#if VE_PLATFORM_APPLE
        void* context_ = nullptr;
#endif
    };
} // namespace ve
