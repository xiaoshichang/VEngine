#include "Engine/Runtime/Core/Assert.h"
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

void IgnoreAssertion(const ve::AssertionInfo& info)
{
    (void)info;
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

    const ve::Result<void> initializeResult = ve::InitializeLogging(MakeQuietConfig());
    passed &= Expect(initializeResult.IsOk(), "InitializeLogging should succeed with a quiet config");
    passed &= Expect(ve::IsLoggingInitialized(), "Logging should be initialized after InitializeLogging");

    const ve::Result<void> duplicateInitializeResult = ve::InitializeLogging(MakeQuietConfig());
    passed &= Expect(!duplicateInitializeResult.IsOk(), "Repeated InitializeLogging should fail");
    passed &= Expect(duplicateInitializeResult.GetError().GetCode() == ve::ErrorCode::InvalidState,
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

    const ve::Result<void> initializeResult = ve::InitializeLogging(config);
    passed &= Expect(initializeResult.IsOk(), "InitializeLogging should succeed for file output");

    VE_LOG_INFO("Hello {}", 42);
    VE_LOG_WARN_CATEGORY("Resource", "Missing {}", std::string("Texture"));

    ve::ShutdownLogging();

    const std::string logText = ReadTextFile(logPath);
    passed &= Expect(logText.find("[Info][General]") != std::string::npos, "Default category should be General");
    passed &= Expect(logText.find("Hello 42") != std::string::npos, "std::format message should be written");
    passed &= Expect(logText.find("[Warn][Resource]") != std::string::npos, "Explicit category should be written");
    passed &= Expect(logText.find("Missing Texture") != std::string::npos, "Formatted category message should be written");
    passed &= Expect(logText.find("LoggingTests.cpp") != std::string::npos, "Source file should be written");

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

    const ve::Result<void> initializeResult = ve::InitializeLogging(config);
    passed &= Expect(initializeResult.IsOk(), "InitializeLogging should succeed for callback test");

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

bool TestAssertionIntegration()
{
    bool passed = true;

    ve::ShutdownLogging();

    const ve::Result<void> installBeforeInitResult = ve::InstallAssertionLogHandler();
    passed &= Expect(!installBeforeInitResult.IsOk(), "Assertion logging install should require initialized logging");
    passed &= Expect(installBeforeInitResult.GetError().GetCode() == ve::ErrorCode::InvalidState,
                     "Assertion logging install before init should return InvalidState");

    std::vector<CapturedLog> capturedLogs;
    gCapturedLogs = &capturedLogs;

    ve::AssertionHandler previousAssertionHandler = ve::GetAssertionHandler();
    ve::SetAssertionHandler(IgnoreAssertion);

    const ve::Result<void> initializeResult = ve::InitializeLogging(MakeQuietConfig());
    passed &= Expect(initializeResult.IsOk(), "InitializeLogging should succeed for assertion integration");

    ve::SetLogCallback(CaptureLog);

    const ve::Result<void> installResult = ve::InstallAssertionLogHandler();
    passed &= Expect(installResult.IsOk(), "Assertion logging install should succeed after logging initialization");

    const ve::Result<void> repeatedInstallResult = ve::InstallAssertionLogHandler();
    passed &= Expect(!repeatedInstallResult.IsOk(), "Repeated assertion logging install should fail");
    passed &= Expect(repeatedInstallResult.GetError().GetCode() == ve::ErrorCode::InvalidState,
                     "Repeated assertion logging install should return InvalidState");

    const ve::AssertionInfo assertionInfo{
        "value != nullptr",
        "value should exist",
        "LoggingTests.cpp",
        "TestAssertionIntegration",
        123,
    };
    ve::ReportAssertionFailure(assertionInfo);

    passed &= Expect(capturedLogs.size() == 1, "Assertion failure should be routed to logging callback");
    if (capturedLogs.size() == 1)
    {
        passed &= Expect(capturedLogs[0].severity == ve::LogSeverity::Fatal, "Assertion log should use Fatal severity");
        passed &= Expect(capturedLogs[0].category == "Assert", "Assertion log should use Assert category");
        passed &= Expect(capturedLogs[0].message.find("value != nullptr") != std::string::npos,
                         "Assertion log should include expression");
        passed &= Expect(capturedLogs[0].message.find("LoggingTests.cpp:123") != std::string::npos,
                         "Assertion log should include assertion location");
    }

    ve::UninstallAssertionLogHandler();

    capturedLogs.clear();
    ve::ReportAssertionFailure(assertionInfo);
    passed &= Expect(capturedLogs.empty(), "UninstallAssertionLogHandler should restore previous handler");

    ve::SetLogCallback(nullptr);
    gCapturedLogs = nullptr;
    ve::SetAssertionHandler(previousAssertionHandler);
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
    passed &= TestAssertionIntegration();

    ve::ShutdownLogging();

    if (passed)
    {
        std::cout << "VEngineLoggingTests passed" << '\n';
        return 0;
    }

    return 1;
}
