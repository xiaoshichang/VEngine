#include "Engine/Runtime/Logging/Log.h"

#if VE_PLATFORM_WINDOWS
#include "Engine/Runtime/Platform/Windows/Win32DebugConsole.h"
#endif

#include <boost/log/core.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#if VE_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
    struct LoggingState
    {
        bool initialized = false;
        ve::LoggingConfig config;
        ve::LogCallback callback = nullptr;
    };

    std::mutex gLoggingMutex;
    LoggingState gLoggingState;

    boost::log::sources::logger_mt& GetBoostLogger()
    {
        static boost::log::sources::logger_mt logger;
        return logger;
    }

    bool IsAtLeastSeverity(ve::LogSeverity severity, ve::LogSeverity minimumSeverity) noexcept
    {
        return static_cast<int>(severity) >= static_cast<int>(minimumSeverity);
    }

    std::string MakeTimestamp()
    {
        using Clock = std::chrono::system_clock;

        const auto now = Clock::now();
        const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();

        const std::time_t time = Clock::to_time_t(now);
        std::tm localTime{};

#if VE_PLATFORM_WINDOWS
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif

        char timestamp[32]{};
        std::snprintf(timestamp,
                      sizeof(timestamp),
                      "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                      localTime.tm_year + 1900,
                      localTime.tm_mon + 1,
                      localTime.tm_mday,
                      localTime.tm_hour,
                      localTime.tm_min,
                      localTime.tm_sec,
                      static_cast<long long>(milliseconds));
        return timestamp;
    }

    std::string MakeThreadIdString()
    {
        std::ostringstream stream;
        stream << std::this_thread::get_id();
        return stream.str();
    }

    std::string MakeFileName(const ve::SourceLocation& location)
    {
        const char* fileName = location.file_name();
        if (fileName == nullptr)
        {
            return "<unknown>";
        }

        return std::filesystem::path(fileName).filename().string();
    }

    std::string FormatLogLine(const ve::LogRecord& record)
    {
        const char* category = record.category != nullptr ? record.category : "General";

        std::string line = std::format("[{}][{}][{}][Thread {}] {}",
                                       MakeTimestamp(),
                                       ve::ToString(record.severity),
                                       category,
                                       MakeThreadIdString(),
                                       record.message);

        if (IsAtLeastSeverity(record.severity, ve::LogSeverity::Error))
        {
            const char* functionName =
                record.location.function_name() != nullptr ? record.location.function_name() : "<unknown>";
            line += std::format(" ({}:{} {})", MakeFileName(record.location), record.location.line(), functionName);
        }

        return line;
    }

    void WriteDebuggerOutput(std::string_view line)
    {
#if VE_PLATFORM_WINDOWS
        std::string output(line);
        output.push_back('\n');
        OutputDebugStringA(output.c_str());
#else
        (void)line;
#endif
    }

    void WriteConsoleOutput(ve::LogSeverity severity, std::string_view line)
    {
#if VE_PLATFORM_WINDOWS
        ve::WriteWin32DebugConsoleLog(severity, line);
#else
        std::ostream& stream = IsAtLeastSeverity(severity, ve::LogSeverity::Warn) ? std::cerr : std::clog;
        stream << line << '\n';
#endif
    }

    void WriteFallbackOutput(ve::LogSeverity severity, std::string_view line)
    {
        WriteConsoleOutput(severity, line);
    }

} // namespace

namespace ve
{
    LoggingConfig MakeDefaultLoggingConfig()
    {
        LoggingConfig config;

#if VE_BUILD_DEBUG
        config.minimumSeverity = LogSeverity::Trace;
#else
        config.minimumSeverity = LogSeverity::Info;
#endif

        config.enableConsole = true;
        config.enableFile = VE_PLATFORM_IOS == 0;
        config.enableDebuggerOutput = VE_PLATFORM_WINDOWS != 0;
        config.filePath = std::filesystem::path("Logs") / "VEngine.log";
        return config;
    }

    ErrorCode InitializeLogging(const LoggingConfig& config)
    {
        std::lock_guard lock(gLoggingMutex);

        if (gLoggingState.initialized)
        {
            return ErrorCode::InvalidState;
        }

        try
        {
            boost::log::core::get()->remove_all_sinks();

            if (config.enableConsole && VE_PLATFORM_WINDOWS == 0)
            {
                boost::log::add_console_log(
                    std::clog, boost::log::keywords::format = "%Message%", boost::log::keywords::auto_flush = true);
            }

            if (config.enableFile)
            {
                const std::filesystem::path parentPath = config.filePath.parent_path();
                if (!parentPath.empty())
                {
                    std::filesystem::create_directories(parentPath);
                }

                boost::log::add_file_log(boost::log::keywords::file_name = config.filePath.string(),
                                         boost::log::keywords::open_mode = std::ios_base::out | std::ios_base::trunc,
                                         boost::log::keywords::format = "%Message%",
                                         boost::log::keywords::auto_flush = true);
            }

            boost::log::add_common_attributes();

            gLoggingState.config = config;
            gLoggingState.initialized = true;
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            (void)error;
            boost::log::core::get()->remove_all_sinks();
            return ErrorCode::IOError;
        }
        catch (const std::exception& error)
        {
            (void)error;
            boost::log::core::get()->remove_all_sinks();
            return ErrorCode::Unknown;
        }
        catch (...)
        {
            boost::log::core::get()->remove_all_sinks();
            return ErrorCode::Unknown;
        }

        return ErrorCode::None;
    }

    void ShutdownLogging() noexcept
    {
        std::lock_guard lock(gLoggingMutex);

        gLoggingState.initialized = false;
        gLoggingState.callback = nullptr;

        try
        {
            boost::log::core::get()->flush();
            boost::log::core::get()->remove_all_sinks();
        }
        catch (...)
        {
        }
    }

    bool IsLoggingInitialized() noexcept
    {
        std::lock_guard lock(gLoggingMutex);
        return gLoggingState.initialized;
    }

    bool ShouldLog(LogSeverity severity) noexcept
    {
        std::lock_guard lock(gLoggingMutex);
        const LoggingConfig config = gLoggingState.initialized ? gLoggingState.config : MakeDefaultLoggingConfig();
        return IsAtLeastSeverity(severity, config.minimumSeverity);
    }

    void SetLogCallback(LogCallback callback) noexcept
    {
        std::lock_guard lock(gLoggingMutex);
        gLoggingState.callback = callback;
    }

    LogCallback GetLogCallback() noexcept
    {
        std::lock_guard lock(gLoggingMutex);
        return gLoggingState.callback;
    }

    void LogMessage(LogSeverity severity, const char* category, std::string_view message, SourceLocation location)
    {
        LogCallback callback = nullptr;
        bool initialized = false;
        bool enableConsole = false;
        bool enableDebuggerOutput = false;

        {
            std::lock_guard lock(gLoggingMutex);
            const LoggingConfig config = gLoggingState.initialized ? gLoggingState.config : MakeDefaultLoggingConfig();
            if (!IsAtLeastSeverity(severity, config.minimumSeverity))
            {
                return;
            }

            callback = gLoggingState.callback;
            initialized = gLoggingState.initialized;
            enableConsole = config.enableConsole;
            enableDebuggerOutput = config.enableDebuggerOutput;
        }

        const LogRecord record{severity, category != nullptr ? category : "General", message, location};

        if (callback != nullptr)
        {
            callback(record);
        }

        const std::string line = FormatLogLine(record);

        if (initialized)
        {
            BOOST_LOG(GetBoostLogger()) << line;

#if VE_PLATFORM_WINDOWS
            if (enableConsole)
            {
                WriteConsoleOutput(severity, line);
            }
#else
            (void)enableConsole;
#endif
        }
        else
        {
            WriteFallbackOutput(severity, line);
        }

        if (enableDebuggerOutput)
        {
            WriteDebuggerOutput(line);
        }
    }

    const char* ToString(LogSeverity severity) noexcept
    {
        switch (severity)
        {
        case LogSeverity::Trace:
            return "Trace";
        case LogSeverity::Debug:
            return "Debug";
        case LogSeverity::Info:
            return "Info";
        case LogSeverity::Warn:
            return "Warn";
        case LogSeverity::Error:
            return "Error";
        case LogSeverity::Fatal:
            return "Fatal";
        }

        return "Unknown";
    }

    namespace detail
    {
        void
        LogFormattedMessage(LogSeverity severity, const char* category, std::string message, SourceLocation location)
        {
            LogMessage(severity, category, message, location);
        }
    } // namespace detail
} // namespace ve
