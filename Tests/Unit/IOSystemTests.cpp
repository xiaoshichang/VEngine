#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/IO/IOSystem.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <vector>

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

    bool ExpectOk(ve::ErrorCode result, const char* message)
    {
        if (result == ve::ErrorCode::None)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result) << '\n';
        return false;
    }

    template<typename T>
    bool ExpectResultOk(const ve::Result<T>& result, const char* message)
    {
        if (result)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result.GetError().GetCode());

        if (!result.GetError().GetMessage().empty())
        {
            std::cerr << ": " << result.GetError().GetMessage();
        }

        std::cerr << '\n';
        return false;
    }

    ve::Path GetTestRoot()
    {
        return ve::FileSystem::GetCurrentWorkingDirectory() / "Generated/IOSystemTests";
    }

    void RemoveTestRoot()
    {
        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path("Generated") / "IOSystemTests", error);
    }

    ve::IOSystemInitParam MakeIOSystemDesc()
    {
        ve::IOSystemInitParam desc;
        desc.threadName = "IOSystemTestThread";
        return desc;
    }

    ve::IOReadRequestDesc MakeReadDesc(const ve::Path& path)
    {
        ve::IOReadRequestDesc desc;
        desc.path = path;
        desc.debugName = path.GetFilename();
        return desc;
    }

    bool WaitForCompletedRead(ve::IOSystem& ioSystem, ve::IOReadResult& outResult)
    {
        for (int attempt = 0; attempt < 1000; ++attempt)
        {
            if (ioSystem.TryPopCompletedRead(outResult))
            {
                return true;
            }

            ve::SleepFor(std::chrono::milliseconds(1));
        }

        return false;
    }

    bool TestInitializeAndShutdown()
    {
        bool passed = true;

        ve::IOSystem ioSystem;
        passed &= Expect(!ioSystem.IsInitialized(), "New IOSystem should not be initialized");
        passed &= ExpectOk(ioSystem.Initialize(MakeIOSystemDesc()), "IOSystem should initialize");
        passed &= Expect(ioSystem.IsInitialized(), "Initialized IOSystem should report initialized");
        ioSystem.Shutdown();
        passed &= Expect(!ioSystem.IsInitialized(), "Shutdown IOSystem should report uninitialized");
        ioSystem.Shutdown();

        return passed;
    }

    bool TestRepeatedInitializeWhileRunningFails()
    {
        bool passed = true;

        ve::IOSystem ioSystem;
        passed &= ExpectOk(ioSystem.Initialize(MakeIOSystemDesc()), "Initial IOSystem Initialize should succeed");

        const ve::ErrorCode repeatedInitialize = ioSystem.Initialize(MakeIOSystemDesc());
        passed &=
            Expect(repeatedInitialize != ve::ErrorCode::None, "Repeated IOSystem Initialize while running should fail");
        passed &= Expect(repeatedInitialize == ve::ErrorCode::InvalidState,
                         "Repeated IOSystem Initialize should report InvalidState");

        ioSystem.Shutdown();
        return passed;
    }

    bool TestInitializeAfterShutdownSucceeds()
    {
        bool passed = true;

        ve::IOSystem ioSystem;
        passed &= ExpectOk(ioSystem.Initialize(MakeIOSystemDesc()), "Initial IOSystem lifecycle should initialize");
        ioSystem.Shutdown();
        passed &=
            ExpectOk(ioSystem.Initialize(MakeIOSystemDesc()), "Standalone IOSystem should initialize after Shutdown");
        ioSystem.Shutdown();

        return passed;
    }

    bool TestScheduleBeforeInitializeAndAfterShutdownFails()
    {
        bool passed = true;

        const ve::Path path = GetTestRoot() / "BeforeAfter.bin";
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(path, "data"), "Test file should be written");

        ve::IOSystem ioSystem;
        ve::Result<ve::IORequestHandle> beforeInitialize = ioSystem.ReadBinaryFile(MakeReadDesc(path));
        passed &= Expect(!beforeInitialize, "Read before Initialize should fail");
        if (!beforeInitialize)
        {
            passed &= Expect(beforeInitialize.GetError().GetCode() == ve::ErrorCode::InvalidState,
                             "Read before Initialize should report InvalidState");
        }

        passed &= ExpectOk(ioSystem.Initialize(MakeIOSystemDesc()),
                           "IOSystem should initialize before shutdown failure test");
        ioSystem.Shutdown();

        ve::Result<ve::IORequestHandle> afterShutdown = ioSystem.ReadBinaryFile(MakeReadDesc(path));
        passed &= Expect(!afterShutdown, "Read after Shutdown should fail until reinitialized");
        if (!afterShutdown)
        {
            passed &= Expect(afterShutdown.GetError().GetCode() == ve::ErrorCode::InvalidState,
                             "Read after Shutdown should report InvalidState");
        }

        return passed;
    }

    bool TestReadBinaryFileCompletes()
    {
        bool passed = true;

        const ve::Path path = GetTestRoot() / "Read/Payload.bin";
        const std::array<std::byte, 5> bytes = {
            std::byte{0x01},
            std::byte{0x02},
            std::byte{0x7F},
            std::byte{0x80},
            std::byte{0xFF},
        };

        passed &= ExpectOk(ve::FileSystem::WriteBinaryFile(path, bytes.data(), bytes.size()),
                           "Binary payload should be written before async read");

        ve::IOSystem ioSystem;
        passed &= ExpectOk(ioSystem.Initialize(MakeIOSystemDesc()), "IOSystem should initialize for read test");

        ve::Result<ve::IORequestHandle> request = ioSystem.ReadBinaryFile(MakeReadDesc(path));
        passed &= ExpectResultOk(request, "ReadBinaryFile should schedule");

        if (request)
        {
            ioSystem.Wait(request.GetValue());
            passed &= Expect(ioSystem.IsComplete(request.GetValue()), "Waited IO request should report complete");
        }

        ve::IOReadResult result;
        passed &= Expect(WaitForCompletedRead(ioSystem, result), "Completed read should be available for polling");
        passed &= Expect(result.IsOk(), "Completed read should report success");
        passed &= Expect(result.path == path, "Completed read should preserve request path");
        passed &= Expect(result.data == std::vector<std::byte>(bytes.begin(), bytes.end()),
                         "Completed read should match bytes");

        ioSystem.Shutdown();
        return passed;
    }

    bool TestMissingFileCompletesWithError()
    {
        bool passed = true;

        ve::IOSystem ioSystem;
        passed &= ExpectOk(ioSystem.Initialize(MakeIOSystemDesc()), "IOSystem should initialize for missing file test");

        const ve::Path missingPath = GetTestRoot() / "Missing/Nope.bin";
        ve::Result<ve::IORequestHandle> request = ioSystem.ReadBinaryFile(MakeReadDesc(missingPath));
        passed &= ExpectResultOk(request, "Missing file read should still schedule");

        if (request)
        {
            ioSystem.Wait(request.GetValue());
        }

        ve::IOReadResult result;
        passed &= Expect(WaitForCompletedRead(ioSystem, result), "Missing file completion should be available");
        passed &= Expect(!result.IsOk(), "Missing file completion should report failure");
        passed &= Expect(result.error.GetCode() == ve::ErrorCode::NotFound, "Missing file should report NotFound");

        ioSystem.Shutdown();
        return passed;
    }

    bool TestWaitAll()
    {
        bool passed = true;

        const ve::Path firstPath = GetTestRoot() / "WaitAll/First.bin";
        const ve::Path secondPath = GetTestRoot() / "WaitAll/Second.bin";
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(firstPath, "first"), "First WaitAll file should be written");
        passed &=
            ExpectOk(ve::FileSystem::WriteTextFile(secondPath, "second"), "Second WaitAll file should be written");

        ve::IOSystem ioSystem;
        passed &= ExpectOk(ioSystem.Initialize(MakeIOSystemDesc()), "IOSystem should initialize for WaitAll test");

        ve::Result<ve::IORequestHandle> first = ioSystem.ReadBinaryFile(MakeReadDesc(firstPath));
        ve::Result<ve::IORequestHandle> second = ioSystem.ReadBinaryFile(MakeReadDesc(secondPath));
        passed &= ExpectResultOk(first, "First WaitAll read should schedule");
        passed &= ExpectResultOk(second, "Second WaitAll read should schedule");

        if (first && second)
        {
            std::array<ve::IORequestHandle, 2> handles{first.GetValue(), second.GetValue()};
            ioSystem.WaitAll(handles);
            passed &= Expect(ioSystem.IsComplete(handles[0]), "First WaitAll handle should complete");
            passed &= Expect(ioSystem.IsComplete(handles[1]), "Second WaitAll handle should complete");
        }

        int completedCount = 0;
        ve::IOReadResult result;
        while (ioSystem.TryPopCompletedRead(result))
        {
            passed &= Expect(result.IsOk(), "WaitAll completed reads should succeed");
            ++completedCount;
        }
        passed &= Expect(completedCount == 2, "WaitAll test should produce two completed reads");

        ioSystem.Shutdown();
        return passed;
    }

    bool TestShutdownDrainsAcceptedReads()
    {
        bool passed = true;

        const ve::Path path = GetTestRoot() / "Shutdown/Drain.bin";
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(path, "drain"), "Shutdown drain file should be written");

        ve::IOSystem ioSystem;
        passed &=
            ExpectOk(ioSystem.Initialize(MakeIOSystemDesc()), "IOSystem should initialize for shutdown drain test");

        ve::Result<ve::IORequestHandle> request = ioSystem.ReadBinaryFile(MakeReadDesc(path));
        passed &= ExpectResultOk(request, "Read should schedule before Shutdown");

        ioSystem.Shutdown();

        if (request)
        {
            passed &= Expect(ioSystem.IsComplete(request.GetValue()),
                             "Shutdown should drain accepted reads before returning");
        }

        return passed;
    }

    bool TestInvalidPathRejected()
    {
        bool passed = true;

        ve::IOSystem ioSystem;
        passed &= ExpectOk(ioSystem.Initialize(MakeIOSystemDesc()), "IOSystem should initialize for invalid path test");

        ve::IOReadRequestDesc desc;
        ve::Result<ve::IORequestHandle> request = ioSystem.ReadBinaryFile(desc);
        passed &= Expect(!request, "Empty path read should fail during scheduling");
        if (!request)
        {
            passed &= Expect(request.GetError().GetCode() == ve::ErrorCode::InvalidArgument,
                             "Empty path read should report InvalidArgument");
        }

        ioSystem.Shutdown();
        return passed;
    }
} // namespace

int main()
{
    RemoveTestRoot();

    bool passed = true;

    passed &= TestInitializeAndShutdown();
    passed &= TestRepeatedInitializeWhileRunningFails();
    passed &= TestInitializeAfterShutdownSucceeds();
    passed &= TestScheduleBeforeInitializeAndAfterShutdownFails();
    passed &= TestReadBinaryFileCompletes();
    passed &= TestMissingFileCompletesWithError();
    passed &= TestWaitAll();
    passed &= TestShutdownDrainsAcceptedReads();
    passed &= TestInvalidPathRejected();

    RemoveTestRoot();

    if (passed)
    {
        std::cout << "VEngineIOSystemTests passed" << '\n';
        return 0;
    }

    return 1;
}
