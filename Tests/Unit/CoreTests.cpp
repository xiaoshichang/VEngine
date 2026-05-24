#include "Engine/Runtime/Application/Application.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/BuildConfig.h"
#include "Engine/Runtime/Core/Compiler.h"
#include "Engine/Runtime/Core/EnumFlags.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/ScopeExit.h"
#include "Engine/Runtime/Core/SourceLocation.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Core/Version.h"

#include <boost/json.hpp>
#include <iostream>
#include <memory>
#include <string_view>
#include <type_traits>

namespace ve
{
    enum class CoreTestFlags : UInt32
    {
        None = 0,
        Read = 1u << 0u,
        Write = 1u << 1u,
        Execute = 1u << 2u,
    };

    template<>
    struct EnableEnumFlags<CoreTestFlags> : std::true_type
    {
    };
} // namespace ve

namespace
{
    struct CapturedAssertion
    {
        bool called = false;
        const char* expression = nullptr;
        const char* message = nullptr;
        const char* file = nullptr;
        const char* function = nullptr;
        int line = 0;
    };

    CapturedAssertion* gCapturedAssertion = nullptr;

    void CaptureAssertion(const ve::AssertionInfo& info)
    {
        if (gCapturedAssertion == nullptr)
        {
            return;
        }

        gCapturedAssertion->called = true;
        gCapturedAssertion->expression = info.expression;
        gCapturedAssertion->message = info.message;
        gCapturedAssertion->file = info.file;
        gCapturedAssertion->function = info.function;
        gCapturedAssertion->line = info.line;
    }

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool TestApplicationAndVersion()
    {
        bool passed = true;

        ve::ApplicationDesc desc;
        desc.name = "VEngineTests";
        desc.mainWindow.title = "VEngine Tests";
        desc.mainWindow.width = 640;
        desc.mainWindow.height = 480;
        desc.mainWindow.visible = false;

        ve::Application application(desc);

        passed &= Expect(application.GetName() == "VEngineTests", "Application name should match constructor input");
        passed &= Expect(application.GetExitCode() == 0, "Application exit code should default to success");

        const ve::BuildInfo buildInfo = ve::GetBuildInfo();
        passed &= Expect(std::string_view(buildInfo.projectName) == "VEngine", "Project name should be VEngine");
        passed &= Expect(buildInfo.version != nullptr, "Build version should be available");
        passed &= Expect(buildInfo.platform != nullptr, "Build platform should be available");

        boost::json::object boostJsonSmokeValue;
        boostJsonSmokeValue["project"] = buildInfo.projectName;
        passed &= Expect(boostJsonSmokeValue.at("project").as_string() == "VEngine", "Boost.JSON should be available");

        return passed;
    }

    bool TestCoreTypesAndMacros()
    {
        bool passed = true;

        passed &= Expect(sizeof(ve::Int8) == 1, "Int8 should be 1 byte");
        passed &= Expect(sizeof(ve::UInt8) == 1, "UInt8 should be 1 byte");
        passed &= Expect(sizeof(ve::Int16) == 2, "Int16 should be 2 bytes");
        passed &= Expect(sizeof(ve::UInt16) == 2, "UInt16 should be 2 bytes");
        passed &= Expect(sizeof(ve::Int32) == 4, "Int32 should be 4 bytes");
        passed &= Expect(sizeof(ve::UInt32) == 4, "UInt32 should be 4 bytes");
        passed &= Expect(sizeof(ve::Int64) == 8, "Int64 should be 8 bytes");
        passed &= Expect(sizeof(ve::UInt64) == 8, "UInt64 should be 8 bytes");
        passed &= Expect(sizeof(ve::Float32) == 4, "Float32 should be 4 bytes");
        passed &= Expect(sizeof(ve::Float64) == 8, "Float64 should be 8 bytes");
        passed &= Expect(sizeof(ve::SizeT) == sizeof(std::size_t), "SizeT should match std::size_t");

        passed &=
            Expect((VE_BUILD_DEBUG + VE_BUILD_RELEASE) == 1, "Exactly one build configuration macro should be set");
        passed &=
            Expect((VE_COMPILER_MSVC || VE_COMPILER_CLANG || VE_COMPILER_GCC), "A known compiler macro should be set");
        passed &= Expect(VE_PLATFORM_WINDOWS == 1, "Windows tests should build with VE_PLATFORM_WINDOWS enabled");

        const ve::SourceLocation location = ve::SourceLocation::current();
        passed &= Expect(location.file_name() != nullptr, "SourceLocation should expose a file name");
        passed &= Expect(location.function_name() != nullptr, "SourceLocation should expose a function name");
        passed &= Expect(location.line() > 0, "SourceLocation should expose a line number");

        return passed;
    }

    bool TestError()
    {
        bool passed = true;

        const ve::Error ok;
        passed &= Expect(ok.IsOk(), "Default Error should be success");
        passed &= Expect(ok.GetCode() == ve::ErrorCode::None, "Default Error should use ErrorCode::None");

        const ve::Error error(ve::ErrorCode::InvalidArgument, "invalid value");
        passed &= Expect(!error.IsOk(), "Non-None Error should be failure");
        passed &= Expect(error.GetCode() == ve::ErrorCode::InvalidArgument, "Error should preserve code");
        passed &= Expect(error.GetMessage() == "invalid value", "Error should preserve message");

        passed &= Expect(std::string_view(ve::ToString(ve::ErrorCode::None)) == "None", "None string should be stable");
        passed &= Expect(std::string_view(ve::ToString(ve::ErrorCode::InvalidState)) == "InvalidState",
                         "InvalidState string should be stable");
        passed &= Expect(std::string_view(ve::ToString(ve::ErrorCode::IOError)) == "IOError",
                         "IOError string should be stable");

        return passed;
    }

    bool TestResult()
    {
        bool passed = true;

        auto success = ve::Result<int>::Success(42);
        passed &= Expect(success.IsOk(), "Result<int> success should be ok");
        passed &= Expect(static_cast<bool>(success), "Result<int> bool conversion should match IsOk");
        passed &= Expect(success.GetValue() == 42, "Result<int> success should preserve value");

        auto failure = ve::Result<int>::Failure(ve::Error(ve::ErrorCode::NotFound, "missing"));
        passed &= Expect(!failure.IsOk(), "Result<int> failure should not be ok");
        passed &=
            Expect(failure.GetError().GetCode() == ve::ErrorCode::NotFound, "Result<int> should preserve error code");
        passed &= Expect(failure.GetError().GetMessage() == "missing", "Result<int> should preserve error message");

        auto moveOnly = ve::Result<std::unique_ptr<int>>::Success(std::make_unique<int>(7));
        std::unique_ptr<int> movedValue = moveOnly.MoveValue();
        passed &= Expect(movedValue != nullptr, "Result<T> should support move-only values");
        passed &= Expect(*movedValue == 7, "Result<T> should move out the stored value");

        return passed;
    }

    bool TestAssertions()
    {
        bool passed = true;

        CapturedAssertion captured;
        ve::AssertionHandler previousHandler = ve::GetAssertionHandler();
        gCapturedAssertion = &captured;
        ve::SetAssertionHandler(CaptureAssertion);

        const ve::AssertionInfo info{"value > 0", "value must be positive", "CoreTests.cpp", "TestAssertions", 123};
        ve::ReportAssertionFailure(info);

        ve::SetAssertionHandler(previousHandler);
        gCapturedAssertion = nullptr;

        passed &= Expect(captured.called, "Assertion handler should be called");
        passed &=
            Expect(std::string_view(captured.expression) == "value > 0", "Assertion expression should be captured");
        passed &=
            Expect(std::string_view(captured.message) == "value must be positive", "Assertion message should match");
        passed &= Expect(std::string_view(captured.file) == "CoreTests.cpp", "Assertion file should match");
        passed &= Expect(std::string_view(captured.function) == "TestAssertions", "Assertion function should match");
        passed &= Expect(captured.line == 123, "Assertion line should match");

        int evaluated = 0;
        VE_VERIFY(++evaluated == 1);
        passed &= Expect(evaluated == 1, "VE_VERIFY should evaluate its expression");

        return passed;
    }

    struct NonCopyableProbe : ve::NonCopyable
    {
    };

    struct NonMovableProbe : ve::NonMovable
    {
    };

    bool TestUtilities()
    {
        bool passed = true;

        passed &= Expect(!std::is_copy_constructible_v<NonCopyableProbe>,
                         "NonCopyable derived types should not be copy constructible");
        passed &= Expect(!std::is_copy_assignable_v<NonCopyableProbe>,
                         "NonCopyable derived types should not be copy assignable");
        passed &= Expect(!std::is_copy_constructible_v<NonMovableProbe>,
                         "NonMovable derived types should not be copy constructible");
        passed &= Expect(!std::is_move_constructible_v<NonMovableProbe>,
                         "NonMovable derived types should not be move constructible");

        int cleanupCount = 0;
        {
            auto cleanup = ve::MakeScopeExit([&cleanupCount]() { ++cleanupCount; });
        }
        passed &= Expect(cleanupCount == 1, "ScopeExit should run on scope exit");

        {
            auto cleanup = ve::MakeScopeExit([&cleanupCount]() { ++cleanupCount; });
            cleanup.Dismiss();
        }
        passed &= Expect(cleanupCount == 1, "Dismissed ScopeExit should not run");

        {
            auto cleanup = ve::MakeScopeExit([&cleanupCount]() { ++cleanupCount; });
            auto movedCleanup = std::move(cleanup);
        }
        passed &= Expect(cleanupCount == 2, "Moved ScopeExit should run once");

        ve::CoreTestFlags flags = ve::CoreTestFlags::Read | ve::CoreTestFlags::Write;
        passed &= Expect((flags & ve::CoreTestFlags::Read) == ve::CoreTestFlags::Read,
                         "EnumFlags should support bitwise and");
        flags |= ve::CoreTestFlags::Execute;
        passed &= Expect((flags & ve::CoreTestFlags::Execute) == ve::CoreTestFlags::Execute,
                         "EnumFlags should support compound or");
        flags &= ve::CoreTestFlags::Read;
        passed &= Expect(flags == ve::CoreTestFlags::Read, "EnumFlags should support compound and");

        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestApplicationAndVersion();
    passed &= TestCoreTypesAndMacros();
    passed &= TestError();
    passed &= TestResult();
    passed &= TestAssertions();
    passed &= TestUtilities();

    if (passed)
    {
        std::cout << "VEngineTests passed" << '\n';
        return 0;
    }

    return 1;
}
