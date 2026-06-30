#include "Engine/Runtime/Platform/MacOS/MacDebugConsoleBackend.h"

#include "Engine/Runtime/Core/BuildConfig.h"

#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>

#include <algorithm>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <utility>

namespace ve
{
    struct MacDebugConsoleState;

    void CommitMacDebugConsoleInput(MacDebugConsoleState& state, NSString* inputText);
}

@interface VEngineMacDebugConsoleDelegate : NSObject <NSWindowDelegate, NSTextFieldDelegate>
@property(nonatomic, assign) ve::MacDebugConsoleState* state;
@end

namespace ve
{
    namespace
    {
        constexpr std::size_t MaxRetainedLogLines = 2000;
        constexpr CGFloat ConsoleWidth = 900.0;
        constexpr CGFloat ConsoleHeight = 520.0;
        constexpr CGFloat InputHeight = 28.0;
        constexpr CGFloat Padding = 8.0;

        struct MacDebugConsoleLogLine
        {
            LogSeverity severity = LogSeverity::Info;
            std::string text;
        };
    } // namespace

    struct MacDebugConsoleState
    {
        std::mutex mutex;
        std::queue<std::string> pendingCommands;
        std::deque<MacDebugConsoleLogLine> retainedLogLines;
        DebugConsoleCommandHandler commandHandler;
        void* window = nullptr;
        void* textView = nullptr;
        void* inputField = nullptr;
        void* delegate = nullptr;
        bool uiReady = false;
        bool initialized = false;
    };

    namespace
    {
        MacDebugConsoleState& GetMacDebugConsoleState()
        {
            static auto* state = new MacDebugConsoleState();
            return *state;
        }

        [[nodiscard]] NSWindow* AsWindow(void* window)
        {
            return (__bridge NSWindow*)window;
        }

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

        [[nodiscard]] NSTextView* AsTextView(void* textView)
        {
            return (__bridge NSTextView*)textView;
        }

        [[nodiscard]] NSColor* GetColorForSeverity(LogSeverity severity)
        {
            switch (severity)
            {
            case LogSeverity::Trace:
                return [NSColor lightGrayColor];
            case LogSeverity::Debug:
                return [NSColor colorWithCalibratedRed:0.35 green:0.95 blue:0.45 alpha:1.0];
            case LogSeverity::Info:
                return [NSColor whiteColor];
            case LogSeverity::Warn:
                return [NSColor colorWithCalibratedRed:1.0 green:0.85 blue:0.25 alpha:1.0];
            case LogSeverity::Error:
            case LogSeverity::Fatal:
                return [NSColor colorWithCalibratedRed:1.0 green:0.35 blue:0.35 alpha:1.0];
            }

            return [NSColor whiteColor];
        }

        void RunOnMainThreadSync(void (^block)())
        {
            if ([NSThread isMainThread])
            {
                block();
                return;
            }

            dispatch_sync(dispatch_get_main_queue(), block);
        }

        void RunOnMainThreadAsync(void (^block)())
        {
            dispatch_async(dispatch_get_main_queue(), block);
        }

        void AppendTextViewLine(NSTextView* textView, LogSeverity severity, NSString* line)
        {
            if (textView == nil || line == nil)
            {
                return;
            }

            NSString* lineWithNewline = [line stringByAppendingString:@"\n"];
            NSDictionary* attributes = @{
                NSForegroundColorAttributeName : GetColorForSeverity(severity),
                NSFontAttributeName : [NSFont userFixedPitchFontOfSize:12.0],
            };
            NSAttributedString* attributedLine = [[NSAttributedString alloc] initWithString:lineWithNewline attributes:attributes];
            [[textView textStorage] appendAttributedString:attributedLine];
            [textView scrollRangeToVisible:NSMakeRange([[textView string] length], 0)];
            [attributedLine release];
        }

        void RedrawConsoleLocked(MacDebugConsoleState& state)
        {
            NSTextView* textView = AsTextView(state.textView);
            if (textView == nil)
            {
                return;
            }

            [[textView textStorage] setAttributedString:[[[NSAttributedString alloc] initWithString:@""] autorelease]];
            for (const MacDebugConsoleLogLine& line : state.retainedLogLines)
            {
                NSString* text = [NSString stringWithUTF8String:line.text.c_str()];
                if (text != nil)
                {
                    AppendTextViewLine(textView, line.severity, text);
                }
            }
        }

        void AppendWelcomeLocked(MacDebugConsoleState& state)
        {
            state.retainedLogLines.push_back(MacDebugConsoleLogLine{LogSeverity::Info, "VEngine Debug Console"});
            state.retainedLogLines.push_back(MacDebugConsoleLogLine{LogSeverity::Info, "Type GM commands at the prompt. Logs appear above this line."});
        }

        void WriteFallbackConsoleLog(LogSeverity severity, std::string_view line)
        {
            std::ostream& stream = severity >= LogSeverity::Warn ? std::cerr : std::clog;
            stream << line << '\n';
            stream.flush();
        }

        void CreateMacDebugConsoleWindow(MacDebugConsoleState& state)
        {
            const NSRect frame = NSMakeRect(0.0, 0.0, ConsoleWidth, ConsoleHeight);
            constexpr NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
            NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                           styleMask:styleMask
                                                             backing:NSBackingStoreBuffered
                                                               defer:NO];
            if (window == nil)
            {
                return;
            }

            [window setTitle:@"VEngine Debug Console"];

            NSView* contentView = [window contentView];
            const NSRect contentBounds = [contentView bounds];
            const NSRect inputFrame = NSMakeRect(Padding, Padding, contentBounds.size.width - Padding * 2.0, InputHeight);
            const NSRect scrollFrame = NSMakeRect(Padding,
                                                  Padding * 2.0 + InputHeight,
                                                  contentBounds.size.width - Padding * 2.0,
                                                  contentBounds.size.height - Padding * 3.0 - InputHeight);

            NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:scrollFrame];
            [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
            [scrollView setHasVerticalScroller:YES];
            [scrollView setBorderType:NSBezelBorder];

            NSTextView* textView = [[NSTextView alloc] initWithFrame:[[scrollView contentView] bounds]];
            [textView setEditable:NO];
            [textView setSelectable:YES];
            [textView setRichText:YES];
            [textView setFont:[NSFont userFixedPitchFontOfSize:12.0]];
            [textView setTextColor:[NSColor whiteColor]];
            [textView setBackgroundColor:[NSColor colorWithCalibratedWhite:0.08 alpha:1.0]];
            [textView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
            [scrollView setDocumentView:textView];

            NSTextField* inputField = [[NSTextField alloc] initWithFrame:inputFrame];
            [inputField setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
            [inputField setPlaceholderString:@"GM>"];
            [inputField setFont:[NSFont userFixedPitchFontOfSize:13.0]];

            VEngineMacDebugConsoleDelegate* delegate = [[VEngineMacDebugConsoleDelegate alloc] init];
            delegate.state = &state;
            [window setDelegate:delegate];
            [inputField setDelegate:delegate];

            [contentView addSubview:scrollView];
            [contentView addSubview:inputField];
            [window center];
            [window makeKeyAndOrderFront:nil];

            state.window = window;
            state.textView = textView;
            state.inputField = inputField;
            state.delegate = delegate;
            state.uiReady = true;

            [textView release];
            [scrollView release];
            [inputField release];
            RedrawConsoleLocked(state);
        }

        [[nodiscard]] CGFloat Clamp(CGFloat value, CGFloat minValue, CGFloat maxValue)
        {
            if (maxValue < minValue)
            {
                return minValue;
            }

            return std::min(std::max(value, minValue), maxValue);
        }

        [[nodiscard]] NSRect GetFramePlacedRightOfEditor(const NSRect editorFrame, const NSSize consoleSize, const NSRect visibleFrame, CGFloat spacing)
        {
            const CGFloat minX = NSMinX(visibleFrame);
            const CGFloat maxX = NSMaxX(visibleFrame) - consoleSize.width;
            const CGFloat minY = NSMinY(visibleFrame);
            const CGFloat maxY = NSMaxY(visibleFrame) - consoleSize.height;
            const CGFloat preferredX = NSMaxX(editorFrame) + spacing;
            const CGFloat preferredY = NSMaxY(editorFrame) - consoleSize.height;
            return NSMakeRect(Clamp(preferredX, minX, maxX), Clamp(preferredY, minY, maxY), consoleSize.width, consoleSize.height);
        }

        [[nodiscard]] NSRect GetFramePlacedBelowEditor(const NSRect editorFrame, const NSSize consoleSize, const NSRect visibleFrame, CGFloat spacing)
        {
            const CGFloat minX = NSMinX(visibleFrame);
            const CGFloat maxX = NSMaxX(visibleFrame) - consoleSize.width;
            const CGFloat minY = NSMinY(visibleFrame);
            const CGFloat maxY = NSMaxY(visibleFrame) - consoleSize.height;
            const CGFloat preferredX = NSMinX(editorFrame);
            const CGFloat preferredY = NSMinY(editorFrame) - spacing - consoleSize.height;
            return NSMakeRect(Clamp(preferredX, minX, maxX), Clamp(preferredY, minY, maxY), consoleSize.width, consoleSize.height);
        }

        void PlaceDebugConsoleWindowNearEditor(NSWindow* consoleWindow, NSWindow* editorWindow)
        {
            if (consoleWindow == nil || editorWindow == nil)
            {
                return;
            }

            NSScreen* screen = [editorWindow screen];
            if (screen == nil)
            {
                screen = [NSScreen mainScreen];
            }
            if (screen == nil)
            {
                return;
            }

            constexpr CGFloat spacing = 12.0;
            const NSRect visibleFrame = [screen visibleFrame];
            const NSRect editorFrame = [editorWindow frame];
            const NSSize consoleSize = [consoleWindow frame].size;
            const CGFloat rightSpace = NSMaxX(visibleFrame) - NSMaxX(editorFrame) - spacing;

            const NSRect consoleFrame = rightSpace >= consoleSize.width ? GetFramePlacedRightOfEditor(editorFrame, consoleSize, visibleFrame, spacing)
                                                                         : GetFramePlacedBelowEditor(editorFrame, consoleSize, visibleFrame, spacing);
            [consoleWindow setFrame:consoleFrame display:YES];
        }
    } // namespace

    void CommitMacDebugConsoleInput(MacDebugConsoleState& state, NSString* inputText)
    {
        if (inputText == nil || [inputText length] == 0)
        {
            return;
        }

        const char* utf8Command = [inputText UTF8String];
        if (utf8Command == nullptr || utf8Command[0] == '\0')
        {
            return;
        }

        std::lock_guard lock(state.mutex);
        state.pendingCommands.emplace(utf8Command);
    }

    void MacDebugConsoleBackend::Initialize()
    {
#if VE_BUILD_DEBUG
        MacDebugConsoleState& state = GetMacDebugConsoleState();
        std::lock_guard lock(state.mutex);
        if (state.initialized)
        {
            return;
        }

        RunOnMainThreadSync(^{
            CreateMacDebugConsoleWindow(state);
        });

        if (state.uiReady && state.retainedLogLines.empty())
        {
            AppendWelcomeLocked(state);
            RedrawConsoleLocked(state);
        }

        state.initialized = state.uiReady;
#endif
    }

    void MacDebugConsoleBackend::SetCommandHandler(DebugConsoleCommandHandler handler)
    {
        MacDebugConsoleState& state = GetMacDebugConsoleState();
        std::lock_guard lock(state.mutex);
        state.commandHandler = std::move(handler);
    }

    void MacDebugConsoleBackend::PumpCommands()
    {
        MacDebugConsoleState& state = GetMacDebugConsoleState();
        std::queue<std::string> commands;
        DebugConsoleCommandHandler handler;

        {
            std::lock_guard lock(state.mutex);
            commands.swap(state.pendingCommands);
            handler = state.commandHandler;
        }

        while (!commands.empty())
        {
            if (handler)
            {
                handler(commands.front());
            }

            commands.pop();
        }
    }

    void MacDebugConsoleBackend::WriteLog(LogSeverity severity, std::string_view line)
    {
        MacDebugConsoleState& state = GetMacDebugConsoleState();
        std::string ownedLine(line);
        bool uiReady = false;

        {
            std::lock_guard lock(state.mutex);
            uiReady = state.uiReady;
            state.retainedLogLines.push_back(MacDebugConsoleLogLine{severity, ownedLine});
            if (state.retainedLogLines.size() > MaxRetainedLogLines)
            {
                state.retainedLogLines.pop_front();
            }
        }

        if (!uiReady)
        {
            WriteFallbackConsoleLog(severity, ownedLine);
            return;
        }

        RunOnMainThreadAsync(^{
            std::lock_guard lock(state.mutex);
            if (!state.uiReady)
            {
                return;
            }

            NSString* text = [NSString stringWithUTF8String:ownedLine.c_str()];
            if (text != nil)
            {
                AppendTextViewLine(AsTextView(state.textView), severity, text);
            }
        });
    }

    void MacDebugConsoleBackend::PlaceNearWindow(void* nativeWindowHandle)
    {
#if VE_BUILD_DEBUG
        MacDebugConsoleState& state = GetMacDebugConsoleState();
        RunOnMainThreadSync(^{
            NSWindow* editorWindow = GetWindowFromNativeHandle(nativeWindowHandle);
            if (editorWindow == nil)
            {
                return;
            }

            std::lock_guard lock(state.mutex);
            if (!state.uiReady)
            {
                return;
            }

            PlaceDebugConsoleWindowNearEditor(AsWindow(state.window), editorWindow);
        });
#else
        (void)nativeWindowHandle;
#endif
    }

    std::unique_ptr<DebugConsoleBackend> CreateMacDebugConsoleBackend()
    {
        return std::make_unique<MacDebugConsoleBackend>();
    }
} // namespace ve

@implementation VEngineMacDebugConsoleDelegate

- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    if (_state == nullptr)
    {
        return;
    }

    std::lock_guard lock(_state->mutex);
    _state->uiReady = false;
    _state->window = nullptr;
    _state->textView = nullptr;
    _state->inputField = nullptr;
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    (void)textView;
    if (commandSelector != @selector(insertNewline:))
    {
        return NO;
    }

    if (_state != nullptr)
    {
        NSString* inputText = [control stringValue];
        ve::CommitMacDebugConsoleInput(*_state, inputText);
        [control setStringValue:@""];
    }

    return YES;
}

@end
