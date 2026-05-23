#include "Engine/Runtime/Platform/Windows/Win32DebugConsole.h"

#include "Engine/Runtime/Core/BuildConfig.h"
#include "Engine/Runtime/Logging/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
constexpr std::wstring_view PromptText = L"GM> ";
constexpr size_t MaxRetainedLogLines = 2000;
constexpr int MouseWheelLinesPerStep = 3;
constexpr std::wstring_view WelcomeLines[] = {
    L"VEngine Debug Console",
    L"Type GM commands at the prompt. Logs appear above this line.",
};

struct DebugConsoleLogLine
{
    ve::LogSeverity severity = ve::LogSeverity::Info;
    std::wstring text;
};

struct DebugConsoleState
{
    std::atomic_bool acceptsInput = false;
    std::atomic_bool inputThreadStarted = false;
    std::mutex mutex;
    std::queue<std::string> pendingCommands;
    std::deque<DebugConsoleLogLine> retainedLogLines;
    std::wstring inputLine;
    size_t cursor = 0;
    int logScrollOffset = 0;
    ve::Win32DebugConsoleCommandHandler commandHandler;
    HANDLE inputHandle = INVALID_HANDLE_VALUE;
    HANDLE outputHandle = INVALID_HANDLE_VALUE;
    WORD defaultAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    SHORT promptRow = 0;
    bool rendererActive = false;
    bool layoutInitialized = false;
};

DebugConsoleState& GetDebugConsoleState()
{
    // The input thread can block in ReadConsoleInput until process exit, so the shared state intentionally outlives it.
    static auto* state = new DebugConsoleState();
    return *state;
}

WORD GetColorForSeverity(ve::LogSeverity severity)
{
    constexpr WORD traceColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    constexpr WORD debugColor = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    constexpr WORD infoColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    constexpr WORD warnColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    constexpr WORD errorColor = FOREGROUND_RED | FOREGROUND_INTENSITY;

    switch (severity)
    {
    case ve::LogSeverity::Trace:
        return traceColor;
    case ve::LogSeverity::Debug:
        return debugColor;
    case ve::LogSeverity::Info:
        return infoColor;
    case ve::LogSeverity::Warn:
        return warnColor;
    case ve::LogSeverity::Error:
    case ve::LogSeverity::Fatal:
        return errorColor;
    }

    return infoColor;
}

std::wstring Utf8ToWide(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }

    const int requiredLength = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);

    if (requiredLength <= 0)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        wideText.data(),
        requiredLength);
    return wideText;
}

std::string WideToUtf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }

    const int requiredLength = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (requiredLength <= 0)
    {
        return {};
    }

    std::string utf8Text(static_cast<size_t>(requiredLength), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        utf8Text.data(),
        requiredLength,
        nullptr,
        nullptr);
    return utf8Text;
}

bool HasConsoleHandle(HANDLE handle)
{
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

bool TryGetConsoleInfo(HANDLE outputHandle, CONSOLE_SCREEN_BUFFER_INFO& consoleInfo)
{
    return HasConsoleHandle(outputHandle) && GetConsoleScreenBufferInfo(outputHandle, &consoleInfo) != 0;
}

int GetVisibleHeight(const CONSOLE_SCREEN_BUFFER_INFO& consoleInfo)
{
    return std::max<int>(consoleInfo.srWindow.Bottom - consoleInfo.srWindow.Top + 1, 1);
}

int GetLogAreaHeight(const CONSOLE_SCREEN_BUFFER_INFO& consoleInfo)
{
    return std::max<int>(GetVisibleHeight(consoleInfo) - 1, 0);
}

void WriteAt(HANDLE outputHandle, SHORT x, SHORT y, std::wstring_view text)
{
    if (text.empty())
    {
        return;
    }

    DWORD written = 0;
    WriteConsoleOutputCharacterW(
        outputHandle,
        text.data(),
        static_cast<DWORD>(text.size()),
        COORD{x, y},
        &written);
}

void WriteAtWithAttributes(HANDLE outputHandle, SHORT x, SHORT y, std::wstring_view text, WORD attributes)
{
    if (text.empty())
    {
        return;
    }

    WriteAt(outputHandle, x, y, text);

    DWORD written = 0;
    FillConsoleOutputAttribute(outputHandle, attributes, static_cast<DWORD>(text.size()), COORD{x, y}, &written);
}

void FillRow(HANDLE outputHandle, SHORT y, SHORT width, WORD attributes)
{
    if (width <= 0)
    {
        return;
    }

    DWORD written = 0;
    FillConsoleOutputCharacterW(outputHandle, L' ', static_cast<DWORD>(width), COORD{0, y}, &written);
    FillConsoleOutputAttribute(outputHandle, attributes, static_cast<DWORD>(width), COORD{0, y}, &written);
}

bool EnsureConsoleLayoutLocked(DebugConsoleState& state)
{
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo = {};

    if (!state.rendererActive || !TryGetConsoleInfo(state.outputHandle, consoleInfo))
    {
        return false;
    }

    if (consoleInfo.dwSize.X <= 0)
    {
        return false;
    }

    state.promptRow = consoleInfo.srWindow.Bottom;
    state.layoutInitialized = true;
    return true;
}

int GetMaxLogScrollOffsetLocked(const DebugConsoleState& state)
{
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo = {};

    if (!TryGetConsoleInfo(state.outputHandle, consoleInfo))
    {
        return 0;
    }

    const int logAreaHeight = GetLogAreaHeight(consoleInfo);
    const int retainedLineCount = static_cast<int>(state.retainedLogLines.size());
    return std::max(retainedLineCount - logAreaHeight, 0);
}

void ClampLogScrollOffsetLocked(DebugConsoleState& state)
{
    state.logScrollOffset = std::clamp(state.logScrollOffset, 0, GetMaxLogScrollOffsetLocked(state));
}

void DrawPromptLocked(DebugConsoleState& state)
{
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo = {};

    if (!EnsureConsoleLayoutLocked(state) || !TryGetConsoleInfo(state.outputHandle, consoleInfo))
    {
        return;
    }

    const SHORT width = consoleInfo.dwSize.X;
    const SHORT promptRow = state.promptRow;

    if (width <= 0)
    {
        return;
    }

    FillRow(state.outputHandle, promptRow, width, state.defaultAttributes);
    SetConsoleTextAttribute(state.outputHandle, state.defaultAttributes);
    WriteAt(state.outputHandle, 0, promptRow, PromptText);

    const SHORT promptWidth = static_cast<SHORT>(std::min<size_t>(PromptText.size(), static_cast<size_t>(width)));
    const SHORT inputWidth = static_cast<SHORT>(std::max<SHORT>(width - promptWidth, 0));
    size_t visibleStart = 0;

    if (inputWidth > 0 && state.cursor >= static_cast<size_t>(inputWidth))
    {
        visibleStart = state.cursor - static_cast<size_t>(inputWidth) + 1;
    }

    const size_t visibleCount = std::min(state.inputLine.size() - visibleStart, static_cast<size_t>(inputWidth));

    if (visibleCount > 0)
    {
        const std::wstring_view inputView(state.inputLine.data(), state.inputLine.size());
        WriteAt(state.outputHandle, promptWidth, promptRow, inputView.substr(visibleStart, visibleCount));
    }

    const size_t cursorOffset = state.cursor >= visibleStart ? state.cursor - visibleStart : 0;
    const SHORT cursorX = static_cast<SHORT>(std::min<size_t>(
        static_cast<size_t>(promptWidth) + cursorOffset,
        static_cast<size_t>(std::max<SHORT>(width - 1, 0))));
    SetConsoleCursorPosition(state.outputHandle, COORD{cursorX, promptRow});
}

void DrawLogViewportLocked(DebugConsoleState& state)
{
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo = {};

    if (!EnsureConsoleLayoutLocked(state) || !TryGetConsoleInfo(state.outputHandle, consoleInfo))
    {
        return;
    }

    const SHORT width = consoleInfo.dwSize.X;
    const SHORT logTop = consoleInfo.srWindow.Top;
    const int logAreaHeight = GetLogAreaHeight(consoleInfo);

    if (width <= 0 || logAreaHeight <= 0)
    {
        return;
    }

    for (int row = 0; row < logAreaHeight; ++row)
    {
        FillRow(state.outputHandle, static_cast<SHORT>(logTop + row), width, state.defaultAttributes);
    }

    ClampLogScrollOffsetLocked(state);

    const size_t retainedLineCount = state.retainedLogLines.size();
    const size_t visibleLineCount = std::min(retainedLineCount, static_cast<size_t>(logAreaHeight));

    if (visibleLineCount == 0)
    {
        return;
    }

    const size_t liveStart = retainedLineCount - visibleLineCount;
    const size_t startIndex =
        liveStart >= static_cast<size_t>(state.logScrollOffset) ? liveStart - static_cast<size_t>(state.logScrollOffset) : 0;

    for (size_t index = 0; index < visibleLineCount; ++index)
    {
        const DebugConsoleLogLine& line = state.retainedLogLines[startIndex + index];
        const SHORT row = static_cast<SHORT>(logTop + static_cast<SHORT>(index));
        WriteAtWithAttributes(
            state.outputHandle,
            0,
            row,
            std::wstring_view(line.text).substr(0, static_cast<size_t>(width)),
            GetColorForSeverity(line.severity));
    }
}

void AppendLogSegmentLocked(DebugConsoleState& state, ve::LogSeverity severity, std::wstring text)
{
    const bool wasViewingHistory = state.logScrollOffset > 0;

    state.retainedLogLines.push_back(DebugConsoleLogLine{severity, std::move(text)});

    if (state.retainedLogLines.size() > MaxRetainedLogLines)
    {
        state.retainedLogLines.pop_front();
    }

    if (wasViewingHistory)
    {
        ++state.logScrollOffset;
    }

    ClampLogScrollOffsetLocked(state);
}

void WriteStructuredConsoleLogLocked(DebugConsoleState& state, ve::LogSeverity severity, std::wstring_view line)
{
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo = {};

    if (!EnsureConsoleLayoutLocked(state) || !TryGetConsoleInfo(state.outputHandle, consoleInfo))
    {
        return;
    }

    const SHORT width = consoleInfo.dwSize.X;

    if (width <= 0)
    {
        return;
    }

    if (line.empty())
    {
        AppendLogSegmentLocked(state, severity, {});
        DrawLogViewportLocked(state);
        DrawPromptLocked(state);
        return;
    }

    size_t offset = 0;
    const size_t chunkSize = static_cast<size_t>(width);

    while (offset < line.size())
    {
        const size_t newline = line.find(L'\n', offset);
        const size_t logicalEnd = newline == std::wstring_view::npos ? line.size() : newline;

        if (logicalEnd == offset)
        {
            AppendLogSegmentLocked(state, severity, {});
        }
        else
        {
            size_t chunkOffset = offset;

            while (chunkOffset < logicalEnd)
            {
                const size_t count = std::min(chunkSize, logicalEnd - chunkOffset);
                AppendLogSegmentLocked(state, severity, std::wstring(line.substr(chunkOffset, count)));
                chunkOffset += count;
            }
        }

        if (newline == std::wstring_view::npos)
        {
            break;
        }

        offset = newline + 1;
    }

    DrawLogViewportLocked(state);
    DrawPromptLocked(state);
}

void AppendWelcomeLocked(DebugConsoleState& state)
{
    for (std::wstring_view line : WelcomeLines)
    {
        AppendLogSegmentLocked(state, ve::LogSeverity::Info, std::wstring(line));
    }

    DrawLogViewportLocked(state);
    DrawPromptLocked(state);
}

void WriteSimpleConsoleLog(ve::LogSeverity severity, std::string_view line)
{
    std::ostream& stream = severity >= ve::LogSeverity::Warn ? std::cerr : std::clog;
    HANDLE consoleHandle = GetStdHandle(STD_ERROR_HANDLE);

    if (!HasConsoleHandle(consoleHandle))
    {
        consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    }

    CONSOLE_SCREEN_BUFFER_INFO consoleInfo = {};
    const bool canColor = TryGetConsoleInfo(consoleHandle, consoleInfo);

    if (canColor)
    {
        SetConsoleTextAttribute(consoleHandle, GetColorForSeverity(severity));
    }

    stream << line << '\n';
    stream.flush();

    if (canColor)
    {
        SetConsoleTextAttribute(consoleHandle, consoleInfo.wAttributes);
    }
}

void QueueConsoleCommandLocked(DebugConsoleState& state, std::string command)
{
    if (!command.empty() && command.back() == '\r')
    {
        command.pop_back();
    }

    if (!command.empty())
    {
        state.pendingCommands.push(std::move(command));
    }
}

void CommitInputLineLocked(DebugConsoleState& state)
{
    QueueConsoleCommandLocked(state, WideToUtf8(state.inputLine));
    state.inputLine.clear();
    state.cursor = 0;
    DrawPromptLocked(state);
}

void HandleKeyEventLocked(DebugConsoleState& state, const KEY_EVENT_RECORD& keyEvent)
{
    switch (keyEvent.wVirtualKeyCode)
    {
    case VK_RETURN:
        CommitInputLineLocked(state);
        return;
    case VK_BACK:
        if (state.cursor > 0)
        {
            state.inputLine.erase(state.cursor - 1, 1);
            --state.cursor;
        }
        break;
    case VK_DELETE:
        if (state.cursor < state.inputLine.size())
        {
            state.inputLine.erase(state.cursor, 1);
        }
        break;
    case VK_LEFT:
        if (state.cursor > 0)
        {
            --state.cursor;
        }
        break;
    case VK_RIGHT:
        if (state.cursor < state.inputLine.size())
        {
            ++state.cursor;
        }
        break;
    case VK_HOME:
        state.cursor = 0;
        break;
    case VK_END:
        state.cursor = state.inputLine.size();
        break;
    case VK_ESCAPE:
        state.inputLine.clear();
        state.cursor = 0;
        break;
    default:
        if (keyEvent.uChar.UnicodeChar >= L' ')
        {
            state.inputLine.insert(state.cursor, 1, keyEvent.uChar.UnicodeChar);
            ++state.cursor;
        }
        break;
    }

    DrawPromptLocked(state);
}

void HandleMouseEventLocked(DebugConsoleState& state, const MOUSE_EVENT_RECORD& mouseEvent)
{
    if (mouseEvent.dwEventFlags != MOUSE_WHEELED)
    {
        return;
    }

    const short wheelDelta = static_cast<short>(HIWORD(mouseEvent.dwButtonState));
    const int wheelSteps = std::max<int>(std::abs(wheelDelta) / WHEEL_DELTA, 1);
    const int scrollLines = wheelSteps * MouseWheelLinesPerStep;

    if (wheelDelta > 0)
    {
        state.logScrollOffset += scrollLines;
    }
    else
    {
        state.logScrollOffset -= scrollLines;
    }

    ClampLogScrollOffsetLocked(state);
    DrawLogViewportLocked(state);
    DrawPromptLocked(state);
}

void RunConsoleInputThread()
{
    DebugConsoleState& state = GetDebugConsoleState();

    while (state.acceptsInput.load())
    {
        INPUT_RECORD inputRecord = {};
        DWORD recordsRead = 0;

        if (!ReadConsoleInputW(state.inputHandle, &inputRecord, 1, &recordsRead))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (recordsRead == 0)
        {
            continue;
        }

        std::lock_guard lock(state.mutex);

        if (inputRecord.EventType == KEY_EVENT && inputRecord.Event.KeyEvent.bKeyDown)
        {
            HandleKeyEventLocked(state, inputRecord.Event.KeyEvent);
        }
        else if (inputRecord.EventType == MOUSE_EVENT)
        {
            HandleMouseEventLocked(state, inputRecord.Event.MouseEvent);
        }
        else if (inputRecord.EventType == WINDOW_BUFFER_SIZE_EVENT)
        {
            DrawLogViewportLocked(state);
            DrawPromptLocked(state);
        }
    }
}

void ConfigureConsoleInput(HANDLE inputHandle)
{
    DWORD inputMode = 0;

    if (GetConsoleMode(inputHandle, &inputMode) == 0)
    {
        return;
    }

    inputMode |= ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS;
    inputMode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE);
    SetConsoleMode(inputHandle, inputMode);
}
}

namespace ve
{
void InitializeWin32DebugConsole()
{
#if VE_BUILD_DEBUG
    if (GetConsoleWindow() == nullptr)
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS) == 0)
        {
            (void)AllocConsole();
            SetConsoleTitleW(L"VEngine Debug Console");
        }
    }

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    FILE* stream = nullptr;
    (void)freopen_s(&stream, "CONOUT$", "w", stdout);
    (void)freopen_s(&stream, "CONOUT$", "w", stderr);
    (void)freopen_s(&stream, "CONIN$", "r", stdin);

    std::ios::sync_with_stdio(true);
    std::cout.clear();
    std::cerr.clear();
    std::clog.clear();

    DebugConsoleState& state = GetDebugConsoleState();

    {
        std::lock_guard lock(state.mutex);
        state.inputHandle = GetStdHandle(STD_INPUT_HANDLE);
        state.outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);

        CONSOLE_SCREEN_BUFFER_INFO consoleInfo = {};
        if (TryGetConsoleInfo(state.outputHandle, consoleInfo))
        {
            state.defaultAttributes = consoleInfo.wAttributes;
            state.rendererActive = true;

            CONSOLE_CURSOR_INFO cursorInfo = {};
            cursorInfo.dwSize = 20;
            cursorInfo.bVisible = TRUE;
            SetConsoleCursorInfo(state.outputHandle, &cursorInfo);
            if (!state.layoutInitialized && state.retainedLogLines.empty())
            {
                AppendWelcomeLocked(state);
            }
            else
            {
                DrawPromptLocked(state);
            }
        }
    }

    if (HasConsoleHandle(state.inputHandle))
    {
        ConfigureConsoleInput(state.inputHandle);
    }

    state.acceptsInput.store(true);

    bool expected = false;
    if (HasConsoleHandle(state.inputHandle) && state.inputThreadStarted.compare_exchange_strong(expected, true))
    {
        std::thread(RunConsoleInputThread).detach();
    }
#endif
}

void SetWin32DebugConsoleCommandHandler(Win32DebugConsoleCommandHandler handler)
{
    DebugConsoleState& state = GetDebugConsoleState();
    std::lock_guard lock(state.mutex);
    state.commandHandler = std::move(handler);
}

void PumpWin32DebugConsoleCommands()
{
    DebugConsoleState& state = GetDebugConsoleState();
    std::queue<std::string> commands;
    Win32DebugConsoleCommandHandler handler;

    {
        std::lock_guard lock(state.mutex);
        // Drain on the caller's thread so future GM commands do not mutate runtime state from the console thread.
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

void WriteWin32DebugConsoleLog(LogSeverity severity, std::string_view line)
{
    DebugConsoleState& state = GetDebugConsoleState();

    {
        std::lock_guard lock(state.mutex);

        if (state.rendererActive)
        {
            WriteStructuredConsoleLogLocked(state, severity, Utf8ToWide(line));
            return;
        }
    }

    WriteSimpleConsoleLog(severity, line);
}
}
