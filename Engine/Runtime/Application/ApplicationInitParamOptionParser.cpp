#include "Engine/Runtime/Application/ApplicationInitParamOptionParser.h"

#include "Engine/Runtime/Core/Error.h"

#include <string_view>
#include <utility>
#include <vector>

#if VE_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#endif

namespace ve
{
    namespace
    {
        [[nodiscard]] ApplicationInitParam MakeDefaultApplicationInitParam()
        {
            ApplicationInitParam initParam;
#if VE_PLATFORM_APPLE
            initParam.runtime.renderSystem.device.backend = RenderBackend::Metal;
#endif
            return initParam;
        }

#if VE_PLATFORM_WINDOWS
        [[nodiscard]] Result<std::string> WideToUtf8(std::wstring_view text)
        {
            if (text.empty())
            {
                return Result<std::string>::Success({});
            }

            const int requiredLength = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (requiredLength <= 0)
            {
                return Result<std::string>::Failure(Error(ErrorCode::PlatformError, "Failed to measure a UTF-8 command-line argument."));
            }

            std::string result(static_cast<size_t>(requiredLength), '\0');
            if (WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), requiredLength, nullptr, nullptr) <= 0)
            {
                return Result<std::string>::Failure(Error(ErrorCode::PlatformError, "Failed to convert a command-line argument to UTF-8."));
            }

            return Result<std::string>::Success(std::move(result));
        }
#endif
    } // namespace

    Result<ApplicationInitParam> ApplicationInitParamOptionParser::Parse(std::span<const std::string> arguments)
    {
        ApplicationInitParam initParam = MakeDefaultApplicationInitParam();
#if VE_PLATFORM_WINDOWS
        bool renderBackendSpecified = false;
#endif
        bool startupProjectSpecified = false;

        for (size_t argumentIndex = 1; argumentIndex < arguments.size(); ++argumentIndex)
        {
            const std::string_view argument = arguments[argumentIndex];

#if VE_PLATFORM_WINDOWS
            if (!renderBackendSpecified && argument == "--dx11")
            {
                initParam.runtime.renderSystem.device.backend = RenderBackend::D3D11;
                renderBackendSpecified = true;
                continue;
            }

            if (!renderBackendSpecified && argument == "--dx12")
            {
                initParam.runtime.renderSystem.device.backend = RenderBackend::D3D12;
                renderBackendSpecified = true;
                continue;
            }
#endif

            if (argument == "--render-debug-layer")
            {
                initParam.runtime.renderSystem.device.enableDebugDevice = true;
                continue;
            }

            if (argument == "--project")
            {
                if (argumentIndex + 1 >= arguments.size() || arguments[argumentIndex + 1].empty() || arguments[argumentIndex + 1].starts_with("--"))
                {
                    return Result<ApplicationInitParam>::Failure(
                        Error(ErrorCode::InvalidArgument, "Application command-line option --project requires a non-empty path argument."));
                }

                if (!startupProjectSpecified)
                {
                    initParam.startupProjectPath = arguments[argumentIndex + 1];
                    startupProjectSpecified = true;
                }
                ++argumentIndex;
            }
        }

        return Result<ApplicationInitParam>::Success(std::move(initParam));
    }

    Result<ApplicationInitParam> ApplicationInitParamOptionParser::Parse(int argumentCount, char* arguments[])
    {
        if (argumentCount < 0 || (argumentCount > 0 && arguments == nullptr))
        {
            return Result<ApplicationInitParam>::Failure(Error(ErrorCode::InvalidArgument, "Invalid application command-line argument array."));
        }

        std::vector<std::string> utf8Arguments;
        utf8Arguments.reserve(static_cast<size_t>(argumentCount));
        for (int argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex)
        {
            utf8Arguments.emplace_back(arguments[argumentIndex] == nullptr ? "" : arguments[argumentIndex]);
        }
        return Parse(utf8Arguments);
    }

#if VE_PLATFORM_WINDOWS
    Result<ApplicationInitParam> ApplicationInitParamOptionParser::ParseCurrentProcessCommandLine()
    {
        int argumentCount = 0;
        LPWSTR* nativeArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (nativeArguments == nullptr)
        {
            return Result<ApplicationInitParam>::Failure(Error(ErrorCode::PlatformError, "Failed to read the Windows process command line."));
        }

        std::vector<std::string> utf8Arguments;
        utf8Arguments.reserve(static_cast<size_t>(argumentCount));
        for (int argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex)
        {
            Result<std::string> argumentResult = WideToUtf8(nativeArguments[argumentIndex]);
            if (!argumentResult)
            {
                LocalFree(nativeArguments);
                return Result<ApplicationInitParam>::Failure(argumentResult.GetError());
            }
            utf8Arguments.push_back(argumentResult.MoveValue());
        }

        LocalFree(nativeArguments);
        return Parse(utf8Arguments);
    }
#endif
} // namespace ve
