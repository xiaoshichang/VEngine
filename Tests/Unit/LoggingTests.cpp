#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Logging/Log.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct CapturedLog
{
    ve::LogSeverity severity = ve::LogSeverity::Info;
    std::string category;
    std::string message;
    std::string file;
    std::string function;
    std::uint_least32_t line = 0;
};

std::vector<CapturedLog>* gCapturedLogs = nullptr;

void CaptureLog(const ve::LogRecord& record)
{
    if (gCapturedLogs == nullptr)
    {
        return;
    }

    CapturedLog capturedLog;
    capturedLog.severity = record.severity;
    capturedLog.category = record.category != nullptr ? record.category : "";
    capturedLog.message = std::string(record.message);
    capturedLog.file = record.location.file_name() != nullptr ? record.location.file_name() : "";
    capturedLog.function = record.location.function_name() != nullptr ? record.location.function_name() : "";
    capturedLog.line = record.location.line();
    gCapturedLogs->push_back(std::move(capturedLog));
}

bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }

    return condition;
}

std::filesystem::path GetLoggingTestDirectory()
{
    return std::filesystem::current_path() / "Generated" / "LoggingTests";
}

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

ve::LoggingConfig MakeQuietConfig()
{
    ve::LoggingConfig config = ve::MakeDefaultLoggingConfig();
    config.minimumSeverity = ve::LogSeverity::Trace;
    config.enableConsole = false;
    config.enableFile = false;
    config.enableDebuggerOutput = false;
    return config;
}

bool TestInitialization()
{
    bool passed = true;

    ve::ShutdownLogging();
    passed &= Expect(!ve::IsLoggingInitialized(), "Logging should start uninitialized");

    const ve::ErrorCode initializeResult = ve::InitializeLogging(MakeQuietConfig());
    passed &= Expect(
        initializeResult == ve::ErrorCode::None,
        "InitializeLogging should succeed with a quiet config");
    passed &= Expect(ve::IsLoggingInitialized(), "Logging should be initialized after InitializeLogging");

    const ve::ErrorCode duplicateInitializeResult = ve::InitializeLogging(MakeQuietConfig());
    passed &= Expect(duplicateInitializeResult != ve::ErrorCode::None, "Repeated InitializeLogging should fail");
    passed &= Expect(
        duplicateInitializeResult == ve::ErrorCode::InvalidState,
        "Repeated InitializeLogging should return InvalidState");

    ve::ShutdownLogging();
    passed &= Expect(!ve::IsLoggingInitialized(), "ShutdownLogging should clear initialization state");

    ve::ShutdownLogging();
    passed &= Expect(!ve::IsLoggingInitialized(), "ShutdownLogging should be safe when already shut down");

    return passed;
}

bool TestFileOutputAndFormatting()
{
    bool passed = true;

    ve::ShutdownLogging();

    const std::filesystem::path logDirectory = GetLoggingTestDirectory();
    const std::filesystem::path logPath = logDirectory / "LoggingOutput.log";
    std::filesystem::remove(logPath);
    std::filesystem::create_directories(logDirectory);

    ve::LoggingConfig config = MakeQuietConfig();
    config.enableFile = true;
    config.filePath = logPath;

    const ve::ErrorCode initializeResult = ve::InitializeLogging(config);
    passed &= Expect(initializeResult == ve::ErrorCode::None, "InitializeLogging should succeed for file output");

    VE_LOG_INFO("Hello {}", 42);
    VE_LOG_WARN_CATEGORY("Resource", "Missing {}", std::string("Texture"));
    VE_LOG_ERROR_CATEGORY("Resource", "Failed {}", std::string("Shader"));

    ve::ShutdownLogging();

    const std::string logText = ReadTextFile(logPath);
    const auto getLineContaining = [&logText](std::string_view text)
    {
        const size_t position = logText.find(text);
        if (position == std::string::npos)
        {
            return std::string();
        }

        const size_t lineStart = logText.rfind('\n', position);
        const size_t lineEnd = logText.find('\n', position);
        const size_t start = lineStart == std::string::npos ? 0 : lineStart + 1;
        const size_t count = lineEnd == std::string::npos ? std::string::npos : lineEnd - start;
        return logText.substr(start, count);
    };

    const std::string infoLine = getLineContaining("Hello 42");
    const std::string warnLine = getLineContaining("Missing Texture");
    const std::string errorLine = getLineContaining("Failed Shader");

    passed &= Expect(logText.find("[Info][General]") != std::string::npos, "Default category should be General");
    passed &= Expect(logText.find("Hello 42") != std::string::npos, "std::format message should be written");
    passed &= Expect(logText.find("[Warn][Resource]") != std::string::npos, "Explicit category should be written");
    passed &= Expect(logText.find("Missing Texture") != std::string::npos, "Formatted category message should be written");
    passed &= Expect(logText.find("[Error][Resource]") != std::string::npos, "Error category should be written");
    passed &= Expect(logText.find("Failed Shader") != std::string::npos, "Error message should be written");
    passed &= Expect(infoLine.find("LoggingTests.cpp") == std::string::npos, "Info logs should omit source location");
    passed &= Expect(warnLine.find("LoggingTests.cpp") == std::string::npos, "Warn logs should omit source location");
    passed &= Expect(errorLine.find("LoggingTests.cpp") != std::string::npos, "Error logs should include source file");

    return passed;
}

bool TestSeverityFilterAndCallback()
{
    bool passed = true;

    ve::ShutdownLogging();

    std::vector<CapturedLog> capturedLogs;
    gCapturedLogs = &capturedLogs;

    ve::LoggingConfig config = MakeQuietConfig();
    config.minimumSeverity = ve::LogSeverity::Warn;

    const ve::ErrorCode initializeResult = ve::InitializeLogging(config);
    passed &= Expect(initializeResult == ve::ErrorCode::None, "InitializeLogging should succeed for callback test");

    ve::SetLogCallback(CaptureLog);

    VE_LOG_INFO_CATEGORY("Filter", "Should not appear {}", 1);
    VE_LOG_ERROR_CATEGORY("Filter", "Should appear {}", 2);

    passed &= Expect(capturedLogs.size() == 1, "Severity filter should suppress messages below minimum severity");

    if (capturedLogs.size() == 1)
    {
        passed &= Expect(capturedLogs[0].severity == ve::LogSeverity::Error, "Callback should receive severity");
        passed &= Expect(capturedLogs[0].category == "Filter", "Callback should receive category");
        passed &= Expect(capturedLogs[0].message == "Should appear 2", "Callback should receive formatted message");
        passed &= Expect(capturedLogs[0].line > 0, "Callback should receive source line");
    }

    ve::SetLogCallback(nullptr);
    VE_LOG_FATAL_CATEGORY("Filter", "No callback");
    passed &= Expect(capturedLogs.size() == 1, "Clearing callback should stop callback delivery");

    gCapturedLogs = nullptr;
    ve::ShutdownLogging();

    return passed;
}

}

int main()
{
    bool passed = true;

    passed &= TestInitialization();
    passed &= TestFileOutputAndFormatting();
    passed &= TestSeverityFilterAndCallback();

    ve::ShutdownLogging();

    if (passed)
    {
        std::cout << "VEngineLoggingTests passed" << '\n';
        return 0;
    }

    return 1;
}
