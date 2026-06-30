#include "Editor/macOS/MacEditorWindowPlacement.h"

#include "Engine/Runtime/Platform/DebugConsole.h"

#import <AppKit/AppKit.h>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] NSWindow* GetWindowFromNativeHandle(void* nativeWindowHandle)
        {
            if (nativeWindowHandle == nullptr)
            {
                return nil;
            }

            id object = (__bridge id)nativeWindowHandle;
            if ([object isKindOfClass:[NSWindow class]])
            {
                return static_cast<NSWindow*>(object);
            }
            if ([object isKindOfClass:[NSView class]])
            {
                return [static_cast<NSView*>(object) window];
            }

            return nil;
        }

        void PlaceWindowAtVisibleTopLeft(NSWindow* window)
        {
            if (window == nil)
            {
                return;
            }

            NSScreen* screen = [window screen];
            if (screen == nil)
            {
                screen = [NSScreen mainScreen];
            }
            if (screen == nil)
            {
                return;
            }

            const NSRect visibleFrame = [screen visibleFrame];
            const NSRect frame = [window frame];
            const NSPoint origin = NSMakePoint(NSMinX(visibleFrame), NSMaxY(visibleFrame) - frame.size.height);
            [window setFrameOrigin:origin];
        }
    } // namespace

    void PlaceMacEditorStartupWindows(void* nativeWindowHandle)
    {
        NSWindow* editorWindow = GetWindowFromNativeHandle(nativeWindowHandle);
        if (editorWindow == nil)
        {
            return;
        }

        PlaceWindowAtVisibleTopLeft(editorWindow);
        PlaceDebugConsoleNearWindow(nativeWindowHandle);
    }
} // namespace ve::editor
