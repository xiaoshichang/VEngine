#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }
        return condition;
    }
} // namespace

int main()
{
    const std::filesystem::path logPath = std::filesystem::current_path() / "VEngineAssertionLoggingTests.log";
    std::error_code errorCode;
    std::filesystem::remove(logPath, errorCode);

    ve::LoggingConfig config;
    config.minimumSeverity = ve::LogSeverity::Trace;
    config.enableConsole = false;
    config.enableFile = true;
    config.enableDebuggerOutput = false;
    config.filePath = logPath;
    if (!Expect(ve::InitializeLogging(config) == ve::ErrorCode::None, "Logging should initialize"))
    {
        return 1;
    }

    const ve::AssertionInfo assertion{"syntheticExpression", "synthetic assertion message", "SyntheticAssertion.cpp", "SyntheticFunction", 42};
    ve::ReportAssertionFailure(assertion);
    ve::ShutdownLogging();

    std::ifstream stream(logPath, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const bool passed = Expect(contents.find("[Fatal][Assert]") != std::string::npos, "Assertion should use Fatal/Assert") &&
                        Expect(contents.find("Expression: syntheticExpression") != std::string::npos, "Expression should be logged") &&
                        Expect(contents.find("Message: synthetic assertion message") != std::string::npos, "Message should be logged") &&
                        Expect(contents.find("File: SyntheticAssertion.cpp:42") != std::string::npos, "Source location should be logged") &&
                        Expect(contents.find("Function: SyntheticFunction") != std::string::npos, "Function should be logged");
    stream.close();
    std::filesystem::remove(logPath, errorCode);
    return passed ? 0 : 1;
}
