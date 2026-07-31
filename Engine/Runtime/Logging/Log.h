#pragma once

#include "Engine/Runtime/Core/BuildConfig.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Core/SourceLocation.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace ve
{
    /// Severity levels used by the VEngine logging facade.
    enum class LogSeverity
    {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Fatal,
    };

    /// Runtime configuration for Boost.Log sink setup.
    struct LoggingConfig
    {
#if VE_BUILD_DEBUG
        LogSeverity minimumSeverity = LogSeverity::Trace;
#else
        LogSeverity minimumSeverity = LogSeverity::Info;
#endif

        bool enableConsole = true;
        bool enableFile = true;
        bool enableDebuggerOutput = VE_PLATFORM_WINDOWS != 0;
        std::filesystem::path filePath = std::filesystem::path("Logs") / "VEngine.log";
    };

    /// Structured log record delivered to callbacks before the formatted line reaches sinks.
    struct LogRecord
    {
        LogSeverity severity = LogSeverity::Info;
        const char* category = "General";
        std::string_view message;
        SourceLocation location = SourceLocation::current();
    };

    /// Optional log callback used by tests and future Editor Console integration.
    using LogCallback = void (*)(const LogRecord& record);

    [[nodiscard]] LoggingConfig MakeDefaultLoggingConfig();

    /// Initializes Boost.Log sinks. Returns InvalidState if logging is already initialized.
    [[nodiscard]] ErrorCode InitializeLogging(const LoggingConfig& config = MakeDefaultLoggingConfig());

    /// Removes logging sinks and clears logging-owned global state.
    void ShutdownLogging() noexcept;

    /// Flushes every initialized logging sink before returning.
    void FlushLogging() noexcept;

    [[nodiscard]] bool IsLoggingInitialized() noexcept;

    /// Returns true when a message of the supplied severity would pass the current severity filter.
    [[nodiscard]] bool ShouldLog(LogSeverity severity) noexcept;

    void SetLogCallback(LogCallback callback) noexcept;
    [[nodiscard]] LogCallback GetLogCallback() noexcept;

    /// Logs an already formatted message through the facade.
    void LogMessage(LogSeverity severity, const char* category, std::string_view message, SourceLocation location = SourceLocation::current());

    [[nodiscard]] const char* ToString(LogSeverity severity) noexcept;

    namespace detail
    {
        inline void AppendFormattedLogMessage(std::ostringstream& stream, std::string_view remaining)
        {
            stream << remaining;
        }

        template<typename TArg, typename... TArgs>
        void AppendFormattedLogMessage(std::ostringstream& stream, std::string_view remaining, TArg&& arg, TArgs&&... args)
        {
            const std::size_t placeholder = remaining.find("{}");
            if (placeholder == std::string_view::npos)
            {
                stream << remaining << ' ' << std::forward<TArg>(arg);
                ((stream << ' ' << std::forward<TArgs>(args)), ...);
                return;
            }

            stream << remaining.substr(0, placeholder) << std::forward<TArg>(arg);
            AppendFormattedLogMessage(stream, remaining.substr(placeholder + 2), std::forward<TArgs>(args)...);
        }

        template<typename... TArgs>
        [[nodiscard]] std::string FormatLogMessage(std::string_view formatString, TArgs&&... args)
        {
            std::ostringstream stream;
            AppendFormattedLogMessage(stream, formatString, std::forward<TArgs>(args)...);
            return stream.str();
        }

        void LogFormattedMessage(LogSeverity severity, const char* category, std::string message, SourceLocation location);
    } // namespace detail
} // namespace ve

#define VE_DETAIL_LOG(severityName, categoryName, ...)                                                                                                         \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (::ve::ShouldLog(::ve::LogSeverity::severityName))                                                                                                  \
        {                                                                                                                                                      \
            ::ve::detail::LogFormattedMessage(                                                                                                                 \
                ::ve::LogSeverity::severityName, categoryName, ::ve::detail::FormatLogMessage(__VA_ARGS__), ::ve::SourceLocation::current());                  \
        }                                                                                                                                                      \
    } while (false)

#define VE_LOG_TRACE(...) VE_DETAIL_LOG(Trace, "General", __VA_ARGS__)
#define VE_LOG_DEBUG(...) VE_DETAIL_LOG(Debug, "General", __VA_ARGS__)
#define VE_LOG_INFO(...) VE_DETAIL_LOG(Info, "General", __VA_ARGS__)
#define VE_LOG_WARN(...) VE_DETAIL_LOG(Warn, "General", __VA_ARGS__)
#define VE_LOG_ERROR(...) VE_DETAIL_LOG(Error, "General", __VA_ARGS__)
#define VE_LOG_FATAL(...) VE_DETAIL_LOG(Fatal, "General", __VA_ARGS__)

#define VE_LOG_TRACE_CATEGORY(categoryName, ...) VE_DETAIL_LOG(Trace, categoryName, __VA_ARGS__)
#define VE_LOG_DEBUG_CATEGORY(categoryName, ...) VE_DETAIL_LOG(Debug, categoryName, __VA_ARGS__)
#define VE_LOG_INFO_CATEGORY(categoryName, ...) VE_DETAIL_LOG(Info, categoryName, __VA_ARGS__)
#define VE_LOG_WARN_CATEGORY(categoryName, ...) VE_DETAIL_LOG(Warn, categoryName, __VA_ARGS__)
#define VE_LOG_ERROR_CATEGORY(categoryName, ...) VE_DETAIL_LOG(Error, categoryName, __VA_ARGS__)
#define VE_LOG_FATAL_CATEGORY(categoryName, ...) VE_DETAIL_LOG(Fatal, categoryName, __VA_ARGS__)
