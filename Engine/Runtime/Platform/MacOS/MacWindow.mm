#include "Engine/Runtime/Platform/MacOS/MacWindow.h"

#include "Engine/Runtime/Core/Assert.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] NSWindow* AsNSWindow(void* window)
        {
            return (__bridge NSWindow*)window;
        }

        [[nodiscard]] NSView* AsNSView(void* view)
        {
            return (__bridge NSView*)view;
        }

        [[nodiscard]] CAMetalLayer* AsCAMetalLayer(void* layer)
        {
            return (__bridge CAMetalLayer*)layer;
        }

        [[nodiscard]] WindowExtent GetViewExtent(NSView* view)
        {
            const NSRect bounds = view != nil ? [view bounds] : NSZeroRect;
            return WindowExtent{
                static_cast<uint32_t>(std::max(0.0, bounds.size.width)),
                static_cast<uint32_t>(std::max(0.0, bounds.size.height)),
            };
        }
    } // namespace

    MacWindow::~MacWindow()
    {
        Close();
    }

    Result<std::unique_ptr<MacWindow>> MacWindow::Create(const WindowDesc& desc)
    {
        auto window = std::unique_ptr<MacWindow>(new MacWindow());
        const ErrorCode result = window->Initialize(desc);
        if (result != ErrorCode::None)
        {
            return Result<std::unique_ptr<MacWindow>>::Failure(Error(result));
        }

        return Result<std::unique_ptr<MacWindow>>::Success(std::move(window));
    }

    Result<std::unique_ptr<Window>> MacWindow::CreatePlatformWindow(const WindowDesc& desc)
    {
        Result<std::unique_ptr<MacWindow>> windowResult = Create(desc);
        if (!windowResult)
        {
            return Result<std::unique_ptr<Window>>::Failure(windowResult.GetError());
        }

        return Result<std::unique_ptr<Window>>::Success(windowResult.MoveValue());
    }

    void MacWindow::Show()
    {
        NSWindow* window = AsNSWindow(window_);
        if (window == nil)
        {
            return;
        }

        [window makeKeyAndOrderFront:nil];
        visible_ = true;
        focused_ = true;
    }

    void MacWindow::Close()
    {
        NSWindow* window = AsNSWindow(window_);
        if (window == nil)
        {
            return;
        }

        shouldClose_ = true;
        [window close];
        window_ = nullptr;
        view_ = nullptr;
        metalLayer_ = nullptr;
    }

    WindowPumpStatus MacWindow::PumpEvents()
    {
        @autoreleasepool
        {
            NSEvent* event = nil;
            while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                               untilDate:[NSDate distantPast]
                                                  inMode:NSDefaultRunLoopMode
                                                 dequeue:YES]) != nil)
            {
                [NSApp sendEvent:event];
            }
        }

        if (shouldClose_)
        {
            return WindowPumpStatus{WindowPumpResult::Quit, 0};
        }

        return WindowPumpStatus{};
    }

    bool MacWindow::TryPopOSEvent(OSEvent& outEvent)
    {
        if (pendingOSEvents_.empty())
        {
            return false;
        }

        outEvent = pendingOSEvents_.front();
        pendingOSEvents_.pop_front();
        return true;
    }

    void MacWindow::SetCommandHandler(WindowCommandHandler handler)
    {
        (void)handler;
    }

    void MacWindow::PumpCommands()
    {
    }

    bool MacWindow::ShouldClose() const noexcept
    {
        return shouldClose_;
    }

    bool MacWindow::IsVisible() const noexcept
    {
        return visible_;
    }

    bool MacWindow::IsFocused() const noexcept
    {
        return focused_;
    }

    bool MacWindow::IsMinimized() const noexcept
    {
        return minimized_;
    }

    WindowExtent MacWindow::GetClientExtent() const noexcept
    {
        return clientExtent_;
    }

    const std::string& MacWindow::GetTitle() const noexcept
    {
        return title_;
    }

    void* MacWindow::GetNativeHandle() const noexcept
    {
        return view_;
    }

    void* MacWindow::GetNativeLayer() const noexcept
    {
        return metalLayer_;
    }

    ErrorCode MacWindow::Initialize(const WindowDesc& desc)
    {
        title_ = desc.title.empty() ? "VEngine" : desc.title;
        clientExtent_.width = std::max(desc.width, 1u);
        clientExtent_.height = std::max(desc.height, 1u);

        const NSRect frame = NSMakeRect(0.0, 0.0, static_cast<double>(clientExtent_.width), static_cast<double>(clientExtent_.height));
        constexpr NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:styleMask
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        if (window == nil)
        {
            return ErrorCode::PlatformError;
        }

        [window setTitle:[NSString stringWithUTF8String:title_.c_str()]];

        NSView* view = [[NSView alloc] initWithFrame:frame];
        if (view == nil)
        {
            return ErrorCode::PlatformError;
        }

        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        if (metalLayer == nil)
        {
            return ErrorCode::PlatformError;
        }

        metalLayer.frame = view.bounds;
        metalLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
        [view setWantsLayer:YES];
        [view setLayer:metalLayer];
        [window setContentView:view];
        [window center];

        window_ = (__bridge_retained void*)window;
        view_ = (__bridge_retained void*)view;
        metalLayer_ = (__bridge_retained void*)metalLayer;

        if (desc.visible)
        {
            Show();
        }

        return ErrorCode::None;
    }

    void MacWindow::QueueOSEvent(OSEvent event)
    {
        pendingOSEvents_.push_back(event);
    }
} // namespace ve
