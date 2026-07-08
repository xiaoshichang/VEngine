#include "Engine/Runtime/Platform/MacOS/MacDebugConsoleBackend.h"

#include "Engine/Runtime/Core/BuildConfig.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>

extern char** environ;

namespace ve
{
    namespace
    {
        constexpr std::string_view PromptText = ">>> ";
        constexpr std::string_view WelcomeLines[] = {
            "VEngine Debug Console",
            "Type GM commands at the prompt. Logs appear above this line.",
        };

        struct MacDebugConsoleState
        {
            std::atomic_bool inputThreadStarted = false;
            std::atomic_bool acceptsInput = false;
            std::mutex mutex;
            std::queue<std::string> pendingCommands;
            DebugConsoleCommandHandler commandHandler;
            std::ofstream logFile;
            std::filesystem::path rootPath;
            std::filesystem::path logPath;
            std::filesystem::path commandFifoPath;
            std::filesystem::path scriptPath;
            int commandFileDescriptor = -1;
            bool initialized = false;
        };

        MacDebugConsoleState& GetMacDebugConsoleState()
        {
            // The input thread may block in read() until process exit, so the shared state intentionally outlives it.
            static auto* state = new MacDebugConsoleState();
            return *state;
        }

        [[nodiscard]] const char* GetAnsiColorForSeverity(LogSeverity severity) noexcept
        {
            switch (severity)
            {
            case LogSeverity::Trace:
                return "\033[0m";
            case LogSeverity::Debug:
                return "\033[32m";
            case LogSeverity::Info:
                return "\033[0m";
            case LogSeverity::Warn:
                return "\033[35m";
            case LogSeverity::Error:
            case LogSeverity::Fatal:
                return "\033[31m";
            }

            return "\033[0m";
        }

        [[nodiscard]] std::string ShellQuote(std::string_view text)
        {
            std::string result = "'";
            for (const char value : text)
            {
                if (value == '\'')
                {
                    result += "'\\''";
                }
                else
                {
                    result.push_back(value);
                }
            }

            result.push_back('\'');
            return result;
        }

        [[nodiscard]] std::filesystem::path MakeConsoleRootPath()
        {
            return std::filesystem::temp_directory_path() / ("vengine-debug-console-" + std::to_string(static_cast<long long>(getpid())));
        }

        [[nodiscard]] std::string BuildTerminalScript(const MacDebugConsoleState& state)
        {
            std::string script;
            script += "#!/bin/zsh\n";
            script += "emulate -R zsh\n";
            script += "setopt NO_BEEP\n";
            script += "VENGINE_APP_PID=" + std::to_string(static_cast<long long>(getpid())) + "\n";
            script += "VENGINE_CONSOLE_PID=$$\n";
            script += "LOG_FILE=" + ShellQuote(state.logPath.string()) + "\n";
            script += "COMMAND_FIFO=" + ShellQuote(state.commandFifoPath.string()) + "\n";
            script += "PROMPT=" + ShellQuote(PromptText) + "\n";
            script += R"VENGINE_TERMINAL(
CURRENT_INPUT=''
CURSOR=0
CONSOLE_TITLE="VEngine Debug Console ${VENGINE_APP_PID}"
CONSOLE_TTY=$(tty 2>/dev/null)
LOG_CHUNK_FILE="${LOG_FILE}.chunk"
LOG_OFFSET=0
VENGINE_PROCESS_WATCH_PID=''
VENGINE_STTY_STATE=$(stty -g 2>/dev/null)
printf '\033]0;%s\007' "$CONSOLE_TITLE"
printf '\033[8;42;180t'
touch "$LOG_FILE"
: > "$LOG_CHUNK_FILE"
cleanup() {
    if [ -n "$VENGINE_STTY_STATE" ]; then
        stty "$VENGINE_STTY_STATE" >/dev/null 2>&1
    fi
    if [ -n "$VENGINE_PROCESS_WATCH_PID" ]; then
        kill "$VENGINE_PROCESS_WATCH_PID" >/dev/null 2>&1
    fi
    rm -f "$LOG_CHUNK_FILE"
}
request_terminal_close() {
    if [[ -z "$CONSOLE_TTY" ]]; then
        return
    fi
    (
        sleep 0.2
        /usr/bin/osascript <<APPLESCRIPT
set targetTty to "$CONSOLE_TTY"
set targetTitle to "$CONSOLE_TITLE"
tell application "Terminal"
    repeat with terminalWindow in windows
        try
            repeat with terminalTab in tabs of terminalWindow
                if tty of terminalTab is targetTty then
                    if (count of tabs of terminalWindow) is greater than 1 then
                        close terminalTab
                    else
                        close terminalWindow
                    end if
                    return
                end if
            end repeat
        end try
    end repeat
    repeat with terminalWindow in windows
        try
            if name of terminalWindow contains targetTitle then
                close terminalWindow
                return
            end if
        end try
    end repeat
end tell
APPLESCRIPT
    ) >/dev/null 2>&1 &!
}
shutdown_console() {
    cleanup
    request_terminal_close
    exit 0
}
watch_vengine_process() {
    while kill -0 "$VENGINE_APP_PID" >/dev/null 2>&1; do
        sleep 0.5
    done
    kill -TERM "$VENGINE_CONSOLE_PID" >/dev/null 2>&1
}
get_terminal_columns() {
    local columns="$COLUMNS"
    if [[ -z "$columns" || "$columns" == *[^0-9]* ]]; then
        columns=$(tput cols 2>/dev/null)
    fi
    if [[ -z "$columns" || "$columns" == *[^0-9]* || "$columns" -le 0 ]]; then
        columns=180
    fi
    print -r -- "$columns"
}
redraw_prompt() {
    local columns prompt_width input_width visible_start visible_text cursor_x
    columns=$(get_terminal_columns)
    prompt_width=${#PROMPT}
    input_width=$((columns - prompt_width))
    if (( input_width < 1 )); then
        input_width=1
    fi
    visible_start=0
    if (( CURSOR >= input_width )); then
        visible_start=$((CURSOR - input_width + 1))
    fi
    visible_text="${CURRENT_INPUT:$visible_start:$input_width}"
    cursor_x=$((prompt_width + CURSOR - visible_start))
    printf '\r\033[2K%s%s' "$PROMPT" "$visible_text"
    printf '\r'
    if (( cursor_x > 0 )); then
        printf '\033[%dC' "$cursor_x"
    fi
}
emit_log_chunk() {
    local last_byte
    if [[ ! -s "$LOG_CHUNK_FILE" ]]; then
        return
    fi
    printf '\r\033[2K'
    cat "$LOG_CHUNK_FILE"
    last_byte=$(tail -c 1 "$LOG_CHUNK_FILE" 2>/dev/null | od -An -t u1)
    last_byte=${last_byte//[[:space:]]/}
    if [[ "$last_byte" != "10" ]]; then
        printf '\n'
    fi
    : > "$LOG_CHUNK_FILE"
    redraw_prompt
}
poll_logs() {
    local size start
    size=$(wc -c < "$LOG_FILE" 2>/dev/null)
    size=${size//[^0-9]/}
    if [[ -z "$size" ]]; then
        return
    fi
    if (( size < LOG_OFFSET )); then
        LOG_OFFSET=0
    fi
    if (( size <= LOG_OFFSET )); then
        return
    fi
    start=$((LOG_OFFSET + 1))
    tail -c +"$start" "$LOG_FILE" > "$LOG_CHUNK_FILE" 2>/dev/null
    LOG_OFFSET=$size
    emit_log_chunk
}
send_current_input() {
    if [[ -n "$CURRENT_INPUT" ]]; then
        printf '%s\n' "$CURRENT_INPUT" > "$COMMAND_FIFO"
    fi
    CURRENT_INPUT=''
    CURSOR=0
    redraw_prompt
}
insert_key() {
    local key="$1"
    CURRENT_INPUT="${CURRENT_INPUT:0:$CURSOR}${key}${CURRENT_INPUT:$CURSOR}"
    CURSOR=$((CURSOR + ${#key}))
    redraw_prompt
}
backspace_key() {
    if (( CURSOR <= 0 )); then
        return
    fi
    CURRENT_INPUT="${CURRENT_INPUT:0:$((CURSOR - 1))}${CURRENT_INPUT:$CURSOR}"
    CURSOR=$((CURSOR - 1))
    redraw_prompt
}
delete_key() {
    if (( CURSOR >= ${#CURRENT_INPUT} )); then
        return
    fi
    CURRENT_INPUT="${CURRENT_INPUT:0:$CURSOR}${CURRENT_INPUT:$((CURSOR + 1))}"
    redraw_prompt
}
move_cursor_left() {
    if (( CURSOR > 0 )); then
        CURSOR=$((CURSOR - 1))
        redraw_prompt
    fi
}
move_cursor_right() {
    if (( CURSOR < ${#CURRENT_INPUT} )); then
        CURSOR=$((CURSOR + 1))
        redraw_prompt
    fi
}
move_cursor_home() {
    CURSOR=0
    redraw_prompt
}
move_cursor_end() {
    CURSOR=${#CURRENT_INPUT}
    redraw_prompt
}
clear_input() {
    CURRENT_INPUT=''
    CURSOR=0
    redraw_prompt
}
handle_escape_sequence() {
    local next final extra
    if ! read -rs -k 1 -t 0.01 next; then
        clear_input
        return
    fi
    if [[ "$next" != "[" && "$next" != "O" ]]; then
        clear_input
        return
    fi
    if ! read -rs -k 1 -t 0.01 final; then
        return
    fi
    case "$final" in
        D)
            move_cursor_left
            ;;
        C)
            move_cursor_right
            ;;
        H)
            move_cursor_home
            ;;
        F)
            move_cursor_end
            ;;
        1|4|7|8)
            read -rs -k 1 -t 0.01 extra
            case "$final" in
                1|7)
                    move_cursor_home
                    ;;
                4|8)
                    move_cursor_end
                    ;;
            esac
            ;;
        3)
            read -rs -k 1 -t 0.01 extra
            delete_key
            ;;
    esac
}
handle_key() {
    local key="$1"
    case "$key" in
        $'\r'|$'\n')
            printf '\r\033[2K'
            send_current_input
            ;;
        $'\177'|$'\b')
            backspace_key
            ;;
        $'\004')
            delete_key
            ;;
        $'\003')
            shutdown_console
            ;;
        $'\001')
            move_cursor_home
            ;;
        $'\005')
            move_cursor_end
            ;;
        $'\025')
            clear_input
            ;;
        $'\033')
            handle_escape_sequence
            ;;
        *)
            insert_key "$key"
            ;;
    esac
}
watch_vengine_process &
VENGINE_PROCESS_WATCH_PID=$!
trap cleanup EXIT
trap shutdown_console INT TERM
stty -echo -icanon min 0 time 0 >/dev/null 2>&1
poll_logs
redraw_prompt
while true; do
    if ! kill -0 "$VENGINE_APP_PID" >/dev/null 2>&1; then
        break
    fi
    poll_logs
    key=''
    if read -rs -k 1 -t 0.05 key; then
        handle_key "$key"
    fi
done
shutdown_console
)VENGINE_TERMINAL";
            return script;
        }

        [[nodiscard]] bool WriteTextFile(const std::filesystem::path& path, std::string_view text)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                return false;
            }

            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            return static_cast<bool>(file);
        }

        [[nodiscard]] bool LaunchTerminalScript(const std::filesystem::path& scriptPath)
        {
            const std::string scriptPathText = scriptPath.string();
            const char* argv[] = {
                "/usr/bin/open",
                "-a",
                "Terminal",
                scriptPathText.c_str(),
                nullptr,
            };

            pid_t processID = 0;
            const int result = posix_spawn(&processID, "/usr/bin/open", nullptr, nullptr, const_cast<char* const*>(argv), environ);
            return result == 0;
        }

        void WriteFallbackConsoleLog(LogSeverity severity, std::string_view line)
        {
            std::ostream& stream = severity >= LogSeverity::Warn ? std::cerr : std::clog;
            stream << line << '\n';
            stream.flush();
        }

        void WriteTerminalLogLineLocked(MacDebugConsoleState& state, LogSeverity severity, std::string_view line)
        {
            if (!state.logFile.is_open())
            {
                return;
            }

            state.logFile << GetAnsiColorForSeverity(severity) << line << "\033[0m\n";
            state.logFile.flush();
        }

        void QueueConsoleCommand(MacDebugConsoleState& state, std::string command)
        {
            while (!command.empty() && (command.back() == '\n' || command.back() == '\r'))
            {
                command.pop_back();
            }

            if (command.empty())
            {
                return;
            }

            std::lock_guard lock(state.mutex);
            state.pendingCommands.push(std::move(command));
        }

        void ConsumeCommandText(MacDebugConsoleState& state, std::string& pendingText, std::string_view text)
        {
            pendingText.append(text.data(), text.size());

            size_t newline = pendingText.find('\n');
            while (newline != std::string::npos)
            {
                QueueConsoleCommand(state, pendingText.substr(0, newline));
                pendingText.erase(0, newline + 1);
                newline = pendingText.find('\n');
            }
        }

        void RunConsoleInputThread()
        {
            MacDebugConsoleState& state = GetMacDebugConsoleState();
            std::string pendingText;
            char buffer[1024] = {};

            while (state.acceptsInput.load())
            {
                const ssize_t readCount = read(state.commandFileDescriptor, buffer, sizeof(buffer));
                if (readCount > 0)
                {
                    ConsumeCommandText(state, pendingText, std::string_view(buffer, static_cast<size_t>(readCount)));
                    continue;
                }

                if (readCount == 0 || errno == EINTR)
                {
                    continue;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        [[nodiscard]] bool PrepareTerminalConsole(MacDebugConsoleState& state)
        {
            std::error_code errorCode;
            state.rootPath = MakeConsoleRootPath();
            std::filesystem::remove_all(state.rootPath, errorCode);
            errorCode.clear();
            std::filesystem::create_directories(state.rootPath, errorCode);
            if (errorCode)
            {
                return false;
            }

            state.logPath = state.rootPath / "VEngineDebugConsole.log";
            state.commandFifoPath = state.rootPath / "VEngineDebugConsoleCommands.fifo";
            state.scriptPath = state.rootPath / "VEngineDebugConsole.command";

            if (mkfifo(state.commandFifoPath.c_str(), 0600) != 0)
            {
                return false;
            }

            state.commandFileDescriptor = open(state.commandFifoPath.c_str(), O_RDWR | O_CLOEXEC);
            if (state.commandFileDescriptor < 0)
            {
                return false;
            }

            state.logFile.open(state.logPath, std::ios::binary | std::ios::trunc);
            if (!state.logFile)
            {
                return false;
            }

            for (std::string_view line : WelcomeLines)
            {
                WriteTerminalLogLineLocked(state, LogSeverity::Info, line);
            }

            if (!WriteTextFile(state.scriptPath, BuildTerminalScript(state)))
            {
                return false;
            }

            if (chmod(state.scriptPath.c_str(), 0700) != 0)
            {
                return false;
            }

            return LaunchTerminalScript(state.scriptPath);
        }
    } // namespace

    void MacDebugConsoleBackend::Initialize()
    {
#if VE_BUILD_DEBUG
        MacDebugConsoleState& state = GetMacDebugConsoleState();
        {
            std::lock_guard lock(state.mutex);
            if (state.initialized)
            {
                return;
            }

            state.initialized = PrepareTerminalConsole(state);
            if (!state.initialized)
            {
                WriteFallbackConsoleLog(LogSeverity::Warn, "Failed to start VEngine Debug Console in Terminal.app.");
                return;
            }

            state.acceptsInput.store(true);
        }

        bool expected = false;
        if (state.inputThreadStarted.compare_exchange_strong(expected, true))
        {
            std::thread(RunConsoleInputThread).detach();
        }
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
        {
            std::lock_guard lock(state.mutex);
            if (state.initialized)
            {
                WriteTerminalLogLineLocked(state, severity, line);
                return;
            }
        }

        WriteFallbackConsoleLog(severity, line);
    }

    void MacDebugConsoleBackend::PlaceNearWindow(void* nativeWindowHandle)
    {
        (void)nativeWindowHandle;
    }

    std::unique_ptr<DebugConsoleBackend> CreateMacDebugConsoleBackend()
    {
        return std::make_unique<MacDebugConsoleBackend>();
    }
} // namespace ve
