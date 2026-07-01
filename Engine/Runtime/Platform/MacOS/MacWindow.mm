#include "Engine/Runtime/Platform/MacOS/MacWindow.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Platform/DebugConsole.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <utility>

@class VEngineMacWindowDelegate;
@class VEngineMacContentView;

@interface VEngineMacWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) ve::MacWindow* owner;
@end

@interface VEngineMacContentView : NSView
@property(nonatomic, assign) ve::MacWindow* owner;
@end

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

        [[nodiscard]] VEngineMacWindowDelegate* AsDelegate(void* delegate)
        {
            return (__bridge VEngineMacWindowDelegate*)delegate;
        }

        [[nodiscard]] WindowExtent GetViewExtent(NSView* view)
        {
            const NSRect bounds = view != nil ? [view bounds] : NSZeroRect;
            return WindowExtent{
                static_cast<uint32_t>(std::max(0.0, bounds.size.width)),
                static_cast<uint32_t>(std::max(0.0, bounds.size.height)),
            };
        }

        [[nodiscard]] NSPoint GetLocalMousePoint(NSView* view, NSEvent* event)
        {
            if (view == nil || event == nil)
            {
                return NSZeroPoint;
            }

            const NSPoint windowPoint = [event locationInWindow];
            NSPoint viewPoint = [view convertPoint:windowPoint fromView:nil];
            if (![view isFlipped])
            {
                const NSRect bounds = [view bounds];
                viewPoint.y = bounds.size.height - viewPoint.y;
            }
            return viewPoint;
        }

        [[nodiscard]] Int32 ToInputCoordinate(CGFloat value) noexcept
        {
            return static_cast<Int32>(std::max<CGFloat>(0.0, value));
        }

        [[nodiscard]] InputModifierFlags GetInputModifiers(NSEventModifierFlags flags) noexcept
        {
            InputModifierFlags modifiers = InputModifierFlags::None;
            if ((flags & NSEventModifierFlagShift) != 0)
            {
                modifiers |= InputModifierFlags::Shift;
            }
            if ((flags & NSEventModifierFlagControl) != 0)
            {
                modifiers |= InputModifierFlags::Control;
            }
            if ((flags & NSEventModifierFlagOption) != 0)
            {
                modifiers |= InputModifierFlags::Alt;
            }
            if ((flags & NSEventModifierFlagCommand) != 0)
            {
                modifiers |= InputModifierFlags::Super;
            }
            if ((flags & NSEventModifierFlagCapsLock) != 0)
            {
                modifiers |= InputModifierFlags::CapsLock;
            }
            if ((flags & NSEventModifierFlagNumericPad) != 0)
            {
                modifiers |= InputModifierFlags::NumLock;
            }
            return modifiers;
        }

        [[nodiscard]] InputMouseButton ToMouseButton(NSInteger buttonNumber) noexcept
        {
            switch (buttonNumber)
            {
            case 0:
                return InputMouseButton::Left;
            case 1:
                return InputMouseButton::Right;
            case 2:
                return InputMouseButton::Middle;
            case 3:
                return InputMouseButton::X1;
            case 4:
                return InputMouseButton::X2;
            default:
                return InputMouseButton::Left;
            }
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
        [window makeMainWindow];
        [NSApp activateIgnoringOtherApps:YES];
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
        VEngineMacWindowDelegate* delegate = AsDelegate(delegate_);
        if (delegate != nil)
        {
            delegate.owner = nullptr;
        }

        if (VEngineMacContentView* contentView = static_cast<VEngineMacContentView*>(AsNSView(view_)); contentView != nil)
        {
            contentView.owner = nullptr;
        }

        [window setDelegate:nil];
        if (pressedMouseButtonCount_ > 0)
        {
            [NSEvent stopPeriodicEvents];
            pressedMouseButtonCount_ = 0;
        }
        [window close];
        if (metalLayer_ != nullptr)
        {
            CFRelease(metalLayer_);
            metalLayer_ = nullptr;
        }
        if (view_ != nullptr)
        {
            CFRelease(view_);
            view_ = nullptr;
        }
        if (delegate_ != nullptr)
        {
            CFRelease(delegate_);
            delegate_ = nullptr;
        }
        if (window_ != nullptr)
        {
            CFRelease(window_);
            window_ = nullptr;
        }
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
        SetDebugConsoleCommandHandler(std::move(handler));
    }

    void MacWindow::PumpCommands()
    {
        PumpDebugConsoleCommands();
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
        [window setReleasedWhenClosed:NO];

        VEngineMacContentView* view = [[VEngineMacContentView alloc] initWithFrame:frame];
        if (view == nil)
        {
            return ErrorCode::PlatformError;
        }
        view.owner = this;
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

        VEngineMacWindowDelegate* delegate = [[VEngineMacWindowDelegate alloc] init];
        delegate.owner = this;
        [window setDelegate:delegate];
        [window setAcceptsMouseMovedEvents:YES];

        window_ = window;
        view_ = view;
        metalLayer_ = [metalLayer retain];
        delegate_ = delegate;

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

    void MacWindow::RequestClose()
    {
        if (shouldClose_)
        {
            return;
        }

        shouldClose_ = true;
        visible_ = false;
        focused_ = false;
        QueueOSEvent(OSEvent{OSEventType::WindowHidden});
    }

    void MacWindow::OnNativeWindowWillClose()
    {
        RequestClose();
    }

    void MacWindow::OnNativeWindowFocusChanged(bool focused)
    {
        focused_ = focused;
        if (!focused_ && pressedMouseButtonCount_ > 0)
        {
            [NSEvent stopPeriodicEvents];
            pressedMouseButtonCount_ = 0;
        }
        QueueOSEvent(OSEvent{focused ? OSEventType::WindowFocusGained : OSEventType::WindowFocusLost});
    }

    void MacWindow::OnNativeWindowMiniaturized(bool minimized)
    {
        minimized_ = minimized;
        QueueOSEvent(OSEvent{minimized ? OSEventType::WindowMinimized : OSEventType::WindowRestored});
    }

    void MacWindow::OnNativeWindowResized()
    {
        NSView* view = AsNSView(view_);
        if (view == nil)
        {
            return;
        }

        clientExtent_ = GetViewExtent(view);
        CAMetalLayer* metalLayer = AsCAMetalLayer(metalLayer_);
        if (metalLayer != nil)
        {
            metalLayer.frame = view.bounds;
            metalLayer.drawableSize = CGSizeMake(clientExtent_.width, clientExtent_.height);
        }

        OSEvent event;
        event.type = OSEventType::WindowResized;
        event.width = clientExtent_.width;
        event.height = clientExtent_.height;
        QueueOSEvent(event);
    }

    void MacWindow::OnNativeMouseMoved(Int32 x, Int32 y)
    {
        OSEvent event;
        event.type = OSEventType::MouseMoved;
        event.mouseX = x;
        event.mouseY = y;
        QueueOSEvent(event);
    }

    void MacWindow::OnNativeMouseButton(OSEventType type, InputMouseButton button, Int32 x, Int32 y)
    {
        if (type == OSEventType::MouseButtonDown)
        {
            ++pressedMouseButtonCount_;
            if (pressedMouseButtonCount_ == 1)
            {
                [NSEvent startPeriodicEventsAfterDelay:0.0 withPeriod:0.016];
            }
        }
        else if (type == OSEventType::MouseButtonUp)
        {
            if (pressedMouseButtonCount_ > 0)
            {
                --pressedMouseButtonCount_;
            }

            if (pressedMouseButtonCount_ == 0)
            {
                [NSEvent stopPeriodicEvents];
            }
        }

        OSEvent event;
        event.type = type;
        event.mouseButton = button;
        event.mouseX = x;
        event.mouseY = y;
        QueueOSEvent(event);
    }

    void MacWindow::OnNativeMouseWheel(Float32 deltaX, Float32 deltaY, Int32 x, Int32 y)
    {
        OSEvent event;
        event.type = OSEventType::MouseWheel;
        event.mouseWheelX = deltaX;
        event.mouseWheelY = deltaY;
        event.mouseX = x;
        event.mouseY = y;
        QueueOSEvent(event);
    }

    void MacWindow::OnNativeKey(OSEventType type, UInt32 keyCode, UInt32 scanCode, InputModifierFlags modifiers, bool repeat)
    {
        OSEvent event;
        event.type = type;
        event.keyCode = keyCode;
        event.scanCode = scanCode;
        event.modifiers = modifiers;
        event.isRepeat = repeat;
        QueueOSEvent(event);
    }

    void MacWindow::OnNativeText(UInt32 codepoint)
    {
        OSEvent event;
        event.type = OSEventType::TextInput;
        event.textCodepoint = codepoint;
        QueueOSEvent(event);
    }
} // namespace ve

@implementation VEngineMacWindowDelegate

- (BOOL)windowShouldClose:(id)sender
{
    (void)sender;
    if (_owner != nullptr)
    {
        _owner->RequestClose();
    }
    return NO;
}

- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    if (_owner != nullptr)
    {
        _owner->OnNativeWindowWillClose();
    }
}

- (void)windowDidBecomeKey:(NSNotification*)notification
{
    (void)notification;
    if (_owner != nullptr)
    {
        _owner->OnNativeWindowFocusChanged(true);
    }
}

- (void)windowDidResignKey:(NSNotification*)notification
{
    (void)notification;
    if (_owner != nullptr)
    {
        _owner->OnNativeWindowFocusChanged(false);
    }
}

- (void)windowDidMiniaturize:(NSNotification*)notification
{
    (void)notification;
    if (_owner != nullptr)
    {
        _owner->OnNativeWindowMiniaturized(true);
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)notification
{
    (void)notification;
    if (_owner != nullptr)
    {
        _owner->OnNativeWindowMiniaturized(false);
    }
}

- (void)windowDidResize:(NSNotification*)notification
{
    (void)notification;
    if (_owner != nullptr)
    {
        _owner->OnNativeWindowResized();
    }
}

@end

@implementation VEngineMacContentView

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)isFlipped
{
    return YES;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] makeFirstResponder:self];
}

- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    if (_owner != nullptr)
    {
        _owner->OnNativeWindowResized();
    }
}

- (void)dispatchMouseMove:(NSEvent*)event
{
    if (_owner == nullptr)
    {
        return;
    }

    const NSPoint point = ve::GetLocalMousePoint(self, event);
    _owner->OnNativeMouseMoved(ve::ToInputCoordinate(point.x), ve::ToInputCoordinate(point.y));
}

- (void)dispatchMouseButton:(NSEvent*)event type:(ve::OSEventType)type
{
    if (_owner == nullptr)
    {
        return;
    }

    const NSPoint point = ve::GetLocalMousePoint(self, event);
    _owner->OnNativeMouseButton(type, ve::ToMouseButton([event buttonNumber]), ve::ToInputCoordinate(point.x), ve::ToInputCoordinate(point.y));
}

- (void)mouseMoved:(NSEvent*)event
{
    [self dispatchMouseMove:event];
}

- (void)mouseDragged:(NSEvent*)event
{
    [self dispatchMouseMove:event];
}

- (void)rightMouseDragged:(NSEvent*)event
{
    [self dispatchMouseMove:event];
}

- (void)otherMouseDragged:(NSEvent*)event
{
    [self dispatchMouseMove:event];
}

- (void)mouseDown:(NSEvent*)event
{
    [[self window] makeFirstResponder:self];
    [self dispatchMouseButton:event type:ve::OSEventType::MouseButtonDown];
}

- (void)mouseUp:(NSEvent*)event
{
    [self dispatchMouseButton:event type:ve::OSEventType::MouseButtonUp];
}

- (void)rightMouseDown:(NSEvent*)event
{
    [[self window] makeFirstResponder:self];
    [self dispatchMouseButton:event type:ve::OSEventType::MouseButtonDown];
}

- (void)rightMouseUp:(NSEvent*)event
{
    [self dispatchMouseButton:event type:ve::OSEventType::MouseButtonUp];
}

- (void)otherMouseDown:(NSEvent*)event
{
    [[self window] makeFirstResponder:self];
    [self dispatchMouseButton:event type:ve::OSEventType::MouseButtonDown];
}

- (void)otherMouseUp:(NSEvent*)event
{
    [self dispatchMouseButton:event type:ve::OSEventType::MouseButtonUp];
}

- (void)scrollWheel:(NSEvent*)event
{
    if (_owner == nullptr)
    {
        return;
    }

    const NSPoint point = ve::GetLocalMousePoint(self, event);
    _owner->OnNativeMouseWheel(static_cast<ve::Float32>([event scrollingDeltaX]),
                               static_cast<ve::Float32>([event scrollingDeltaY]),
                               ve::ToInputCoordinate(point.x),
                               ve::ToInputCoordinate(point.y));
}

- (void)periodic:(NSEvent*)event
{
    [self dispatchMouseMove:event];
}

- (void)keyDown:(NSEvent*)event
{
    if (_owner == nullptr)
    {
        return;
    }

    _owner->OnNativeKey(ve::OSEventType::KeyboardKeyDown,
                        static_cast<ve::UInt32>([event keyCode]),
                        static_cast<ve::UInt32>([event keyCode]),
                        ve::GetInputModifiers([event modifierFlags]),
                        [event isARepeat] == YES);

    NSString* characters = [event characters];
    for (NSUInteger index = 0; index < [characters length]; ++index)
    {
        const unichar character = [characters characterAtIndex:index];
        if (character >= 0x20 || character == '\n' || character == '\t')
        {
            _owner->OnNativeText(static_cast<ve::UInt32>(character));
        }
    }
}

- (void)keyUp:(NSEvent*)event
{
    if (_owner == nullptr)
    {
        return;
    }

    _owner->OnNativeKey(ve::OSEventType::KeyboardKeyUp,
                        static_cast<ve::UInt32>([event keyCode]),
                        static_cast<ve::UInt32>([event keyCode]),
                        ve::GetInputModifiers([event modifierFlags]),
                        false);
}

@end
