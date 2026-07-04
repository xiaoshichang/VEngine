#include "Engine/Runtime/Platform/iOS/IOSWindow.h"

#include "Engine/Runtime/Core/Assert.h"

#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <utility>

namespace ve
{
    namespace
    {
        void* gRegisteredMainView = nullptr;
        IOSWindow* gActiveWindow = nullptr;

        [[nodiscard]] UIView* AsUIView(void* view)
        {
            return (__bridge UIView*)view;
        }

        [[nodiscard]] CAMetalLayer* AsCAMetalLayer(void* layer)
        {
            return (__bridge CAMetalLayer*)layer;
        }

        [[nodiscard]] WindowExtent GetLayerDrawableExtent(CAMetalLayer* layer)
        {
            if (layer == nil)
            {
                return WindowExtent{1, 1};
            }

            const CGSize drawableSize = layer.drawableSize;
            return WindowExtent{
                static_cast<uint32_t>(std::max<CGFloat>(1.0, drawableSize.width)),
                static_cast<uint32_t>(std::max<CGFloat>(1.0, drawableSize.height)),
            };
        }

        void ConfigureMetalLayer(UIView* view, CAMetalLayer* layer)
        {
            if (view == nil || layer == nil)
            {
                return;
            }

            UIScreen* screen = view.window != nil ? view.window.screen : UIScreen.mainScreen;
            layer.opaque = YES;
            layer.contentsScale = screen.scale;
            layer.frame = view.bounds;
            layer.drawableSize = CGSizeMake(std::max<CGFloat>(1.0, view.bounds.size.width * layer.contentsScale),
                                            std::max<CGFloat>(1.0, view.bounds.size.height * layer.contentsScale));
        }
    } // namespace

    IOSWindow::~IOSWindow()
    {
        Close();
    }

    void IOSWindow::RegisterMainView(void* view) noexcept
    {
        ClearRegisteredMainView();
        UIView* nativeView = AsUIView(view);
        if (nativeView == nil)
        {
            return;
        }

        gRegisteredMainView = (void*)[nativeView retain];
    }

    void IOSWindow::ClearRegisteredMainView() noexcept
    {
        if (gRegisteredMainView == nullptr)
        {
            return;
        }

        CFRelease(gRegisteredMainView);
        gRegisteredMainView = nullptr;
    }

    void IOSWindow::NotifyRegisteredMainViewResized()
    {
        if (gActiveWindow != nullptr)
        {
            gActiveWindow->OnNativeViewResized();
        }
    }

    Result<std::unique_ptr<IOSWindow>> IOSWindow::Create(const WindowDesc& desc)
    {
        auto window = std::unique_ptr<IOSWindow>(new IOSWindow());
        const ErrorCode result = window->Initialize(desc);
        if (result != ErrorCode::None)
        {
            return Result<std::unique_ptr<IOSWindow>>::Failure(Error(result));
        }

        return Result<std::unique_ptr<IOSWindow>>::Success(std::move(window));
    }

    Result<std::unique_ptr<Window>> IOSWindow::CreatePlatformWindow(const WindowDesc& desc)
    {
        Result<std::unique_ptr<IOSWindow>> windowResult = Create(desc);
        if (!windowResult)
        {
            return Result<std::unique_ptr<Window>>::Failure(windowResult.GetError());
        }

        return Result<std::unique_ptr<Window>>::Success(windowResult.MoveValue());
    }

    void IOSWindow::Show()
    {
        UIView* view = AsUIView(view_);
        if (view != nil)
        {
            view.hidden = NO;
        }

        visible_ = true;
        focused_ = true;
    }

    void IOSWindow::Close()
    {
        shouldClose_ = true;
        visible_ = false;
        focused_ = false;

        if (gActiveWindow == this)
        {
            gActiveWindow = nullptr;
        }

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
    }

    WindowPumpStatus IOSWindow::PumpEvents()
    {
        if (shouldClose_)
        {
            return WindowPumpStatus{WindowPumpResult::Quit, 0};
        }

        return WindowPumpStatus{};
    }

    bool IOSWindow::TryPopOSEvent(OSEvent& outEvent)
    {
        if (pendingOSEvents_.empty())
        {
            return false;
        }

        outEvent = pendingOSEvents_.front();
        pendingOSEvents_.pop_front();
        return true;
    }

    void IOSWindow::SetCommandHandler(WindowCommandHandler handler)
    {
        static_cast<void>(handler);
    }

    void IOSWindow::PumpCommands()
    {
    }

    bool IOSWindow::ShouldClose() const noexcept
    {
        return shouldClose_;
    }

    bool IOSWindow::IsVisible() const noexcept
    {
        return visible_;
    }

    bool IOSWindow::IsFocused() const noexcept
    {
        return focused_;
    }

    bool IOSWindow::IsMinimized() const noexcept
    {
        return minimized_;
    }

    WindowExtent IOSWindow::GetClientExtent() const noexcept
    {
        return clientExtent_;
    }

    const std::string& IOSWindow::GetTitle() const noexcept
    {
        return title_;
    }

    void* IOSWindow::GetNativeHandle() const noexcept
    {
        return view_;
    }

    void* IOSWindow::GetNativeLayer() const noexcept
    {
        return metalLayer_;
    }

    ErrorCode IOSWindow::Initialize(const WindowDesc& desc)
    {
        title_ = desc.title.empty() ? "VEngine" : desc.title;

        if (gRegisteredMainView == nullptr)
        {
            return ErrorCode::PlatformError;
        }

        UIView* view = AsUIView(gRegisteredMainView);
        if (view == nil || ![view.layer isKindOfClass:[CAMetalLayer class]])
        {
            ClearRegisteredMainView();
            return ErrorCode::PlatformError;
        }

        CAMetalLayer* metalLayer = (CAMetalLayer*)view.layer;
        ConfigureMetalLayer(view, metalLayer);

        view_ = gRegisteredMainView;
        gRegisteredMainView = nullptr;
        metalLayer_ = (void*)[metalLayer retain];
        clientExtent_ = GetLayerDrawableExtent(metalLayer);
        gActiveWindow = this;

        if (desc.visible)
        {
            Show();
        }

        return ErrorCode::None;
    }

    void IOSWindow::QueueOSEvent(OSEvent event)
    {
        pendingOSEvents_.push_back(event);
    }

    void IOSWindow::OnNativeViewResized()
    {
        UIView* view = AsUIView(view_);
        CAMetalLayer* metalLayer = AsCAMetalLayer(metalLayer_);
        if (view == nil || metalLayer == nil)
        {
            return;
        }

        ConfigureMetalLayer(view, metalLayer);
        const WindowExtent nextExtent = GetLayerDrawableExtent(metalLayer);
        if (nextExtent.width == clientExtent_.width && nextExtent.height == clientExtent_.height)
        {
            return;
        }

        clientExtent_ = nextExtent;

        OSEvent event;
        event.type = OSEventType::WindowResized;
        event.width = clientExtent_.width;
        event.height = clientExtent_.height;
        QueueOSEvent(event);
    }
} // namespace ve
