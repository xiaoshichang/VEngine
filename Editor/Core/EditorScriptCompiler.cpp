#include "Editor/Core/EditorScriptCompiler.h"

#include "Editor/Core/EditorScriptProjectGenerator.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"

#include <cstddef>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] std::string QuoteProcessArgument(std::string_view argument)
        {
            std::string quoted = "\"";
            for (const char value : argument)
            {
                if (value == '"')
                {
                    quoted += "\\\"";
                }
                else
                {
                    quoted += value;
                }
            }
            quoted += "\"";
            return quoted;
        }

        [[nodiscard]] std::string BuildProcessCommandLine(const std::vector<std::string>& arguments)
        {
            std::ostringstream command;
            for (std::size_t index = 0; index < arguments.size(); ++index)
            {
                if (index > 0)
                {
                    command << ' ';
                }
                command << QuoteProcessArgument(arguments[index]);
            }
            return command.str();
        }

#if defined(_WIN32)
        [[nodiscard]] std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int requiredLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (requiredLength <= 0)
            {
                return {};
            }

            std::wstring wideText(static_cast<std::size_t>(requiredLength), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wideText.data(), requiredLength);
            return wideText;
        }

        [[nodiscard]] std::string WideToUtf8(const std::wstring& text)
        {
            if (text.empty())
            {
                return {};
            }

            const int requiredLength = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (requiredLength <= 0)
            {
                return {};
            }

            std::string utf8Text(static_cast<std::size_t>(requiredLength), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8Text.data(), requiredLength, nullptr, nullptr);
            return utf8Text;
        }

        [[nodiscard]] std::string ConvertProcessOutputToUtf8(std::string_view output)
        {
            if (output.empty())
            {
                return {};
            }

            // dotnet uses the active Windows console code page when stdout is redirected.
            // Preserve UTF-8 output when it is already valid, otherwise decode the local code page.
            const std::wstring utf8Decoded = Utf8ToWide(output);
            if (!utf8Decoded.empty())
            {
                return std::string(output);
            }

            const int requiredLength = MultiByteToWideChar(CP_ACP, 0, output.data(), static_cast<int>(output.size()), nullptr, 0);
            if (requiredLength <= 0)
            {
                return std::string(output);
            }

            std::wstring localCodePageText(static_cast<std::size_t>(requiredLength), L'\0');
            MultiByteToWideChar(CP_ACP, 0, output.data(), static_cast<int>(output.size()), localCodePageText.data(), requiredLength);
            const std::string converted = WideToUtf8(localCodePageText);
            return converted.empty() ? std::string(output) : converted;
        }
#endif

        [[nodiscard]] int RunProcess(const std::vector<std::string>& arguments)
        {
            if (arguments.empty())
            {
                return -1;
            }

            const std::string commandLineText = BuildProcessCommandLine(arguments);
#if defined(_WIN32)
            std::wstring commandLine = Utf8ToWide(commandLineText);
            std::wstring workingDirectory = Utf8ToWide(FileSystem::GetCurrentWorkingDirectory().GetString());
            if (commandLine.empty())
            {
                return -1;
            }

            STARTUPINFOW startupInfo = {};
            startupInfo.cb = sizeof(startupInfo);
            startupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            startupInfo.wShowWindow = SW_HIDE;

            SECURITY_ATTRIBUTES pipeSecurity = {};
            pipeSecurity.nLength = sizeof(pipeSecurity);
            pipeSecurity.bInheritHandle = TRUE;
            HANDLE readPipe = nullptr;
            HANDLE writePipe = nullptr;
            if (CreatePipe(&readPipe, &writePipe, &pipeSecurity, 0) == FALSE || SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0) == FALSE)
            {
                if (readPipe != nullptr)
                {
                    CloseHandle(readPipe);
                }
                if (writePipe != nullptr)
                {
                    CloseHandle(writePipe);
                }
                return -1;
            }
            startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            startupInfo.hStdOutput = writePipe;
            startupInfo.hStdError = writePipe;

            PROCESS_INFORMATION processInfo = {};
            const BOOL created = CreateProcessW(nullptr,
                                                commandLine.data(),
                                                nullptr,
                                                nullptr,
                                                TRUE,
                                                CREATE_NO_WINDOW,
                                                nullptr,
                                                workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                                &startupInfo,
                                                &processInfo);
            if (created == FALSE)
            {
                CloseHandle(readPipe);
                CloseHandle(writePipe);
                return -1;
            }

            CloseHandle(writePipe);
            std::string output;
            std::thread outputReader([readPipe, &output]()
            {
                char buffer[4096];
                DWORD bytesRead = 0;
                while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) != FALSE && bytesRead > 0)
                {
                    output.append(buffer, bytesRead);
                }
                CloseHandle(readPipe);
            });
            WaitForSingleObject(processInfo.hProcess, INFINITE);
            outputReader.join();
            DWORD exitCode = 1;
            const BOOL gotExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            if (!output.empty())
            {
                VE_LOG_INFO_CATEGORY("Editor", "C# build output:\n{}", ConvertProcessOutputToUtf8(output));
            }
            return gotExitCode != FALSE ? static_cast<int>(exitCode) : -1;
#else
            return std::system(commandLineText.c_str());
#endif
        }
    } // namespace

    Result<EditorScriptCompileResult> EditorScriptCompiler::CompileProjectScripts(const EditorScriptCompileDesc& desc) const
    {
        if (desc.projectRoot.IsEmpty() || desc.projectName.empty() || desc.scriptHostAssemblyPath.IsEmpty())
        {
            return Result<EditorScriptCompileResult>::Failure(Error(ErrorCode::InvalidArgument, "Script compilation requires a project root, project name, and ScriptHost assembly."));
        }

        const Path projectFile = ResolveProjectFile(desc);
        if (!FileSystem::IsFile(projectFile))
        {
            return Result<EditorScriptCompileResult>::Failure(Error(ErrorCode::NotFound, "Project script csproj was not found: " + projectFile.GetString()));
        }

        if (!FileSystem::IsFile(desc.scriptHostAssemblyPath))
        {
            return Result<EditorScriptCompileResult>::Failure(Error(ErrorCode::NotFound, "VEngine.ScriptHost.dll was not found: " + desc.scriptHostAssemblyPath.GetString()));
        }

        const Path outputDirectory = desc.projectRoot / "Library" / "Scripting" / "output";
        const ErrorCode createDirectoryResult = FileSystem::CreateDirectories(outputDirectory);
        if (createDirectoryResult != ErrorCode::None)
        {
            return Result<EditorScriptCompileResult>::Failure(Error(createDirectoryResult, "Failed to create script output directory."));
        }

        const std::vector<std::string> arguments = {
            "dotnet",
            "build",
            projectFile.GetString(),
            "--configuration",
            "Debug",
            "--output",
            outputDirectory.GetString(),
            "-p:VEngineScriptHostAssembly=" + desc.scriptHostAssemblyPath.GetString(),
            "--nologo",
        };
        const int result = RunProcess(arguments);
        if (result != 0)
        {
            return Result<EditorScriptCompileResult>::Failure(Error(ErrorCode::PlatformError, "dotnet build failed for project scripts."));
        }

        EditorScriptCompileResult compileResult;
        compileResult.outputDirectory = outputDirectory;
        compileResult.assemblyPath = outputDirectory / (desc.projectName + ".Scripts.dll");
        if (!FileSystem::IsFile(compileResult.assemblyPath))
        {
            return Result<EditorScriptCompileResult>::Failure(Error(ErrorCode::NotFound, "Compiled script assembly was not found: " + compileResult.assemblyPath.GetString()));
        }

        return Result<EditorScriptCompileResult>::Success(std::move(compileResult));
    }

    Path EditorScriptCompiler::ResolveProjectFile(const EditorScriptCompileDesc& desc)
    {
        return EditorScriptProjectGenerator::GetGeneratedProjectPath(desc.projectRoot, desc.projectName);
    }

} // namespace ve::editor
