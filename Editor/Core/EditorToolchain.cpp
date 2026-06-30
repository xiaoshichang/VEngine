#include "Editor/Core/EditorToolchain.h"

#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"

#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ve::editor
{
    namespace
    {
        struct RequiredTool
        {
            std::string displayName;
            std::vector<Path> candidates;
            std::string pathExecutableName;
            Path resolvedPath;
        };

        [[nodiscard]] RequiredTool MakeRequiredTool(std::string displayName)
        {
            RequiredTool tool;
            tool.displayName = std::move(displayName);
            return tool;
        }

        [[nodiscard]] Path GetEditorBinaryDirectory(const Path& executableDirectory)
        {
#if VE_PLATFORM_MACOS
            const Path appBundlePath = executableDirectory.GetParentPath().GetParentPath();
            if (appBundlePath.GetFilename().ends_with(".app"))
            {
                return appBundlePath.GetParentPath();
            }
#else
            (void)executableDirectory;
#endif
            return executableDirectory;
        }

        [[nodiscard]] bool LooksLikeRepositoryRoot(const Path& path)
        {
            return FileSystem::IsDirectory(path / "ThirdParty") && FileSystem::IsDirectory(path / "Engine") && FileSystem::IsDirectory(path / "Editor") &&
                   FileSystem::IsFile(path / "CMakeLists.txt");
        }

        [[nodiscard]] Path FindRepositoryRootFrom(Path path)
        {
            while (!path.IsEmpty())
            {
                if (LooksLikeRepositoryRoot(path))
                {
                    return path;
                }

                const Path parentPath = path.GetParentPath();
                if (parentPath == path)
                {
                    break;
                }

                path = parentPath;
            }

            return Path();
        }

        [[nodiscard]] Path FindRepositoryRoot()
        {
            const Path executableDirectory = FileSystem::GetExecutableDirectory();
            const Path binaryDirectory = GetEditorBinaryDirectory(executableDirectory);

            for (const Path& root : {FindRepositoryRootFrom(binaryDirectory), FindRepositoryRootFrom(executableDirectory), FindRepositoryRootFrom(FileSystem::GetCurrentWorkingDirectory())})
            {
                if (!root.IsEmpty())
                {
                    return root;
                }
            }

            return Path();
        }

        [[nodiscard]] std::vector<Path> SplitPathEnvironment(std::string_view pathText)
        {
            std::vector<Path> paths;
            size_t start = 0;
#if VE_PLATFORM_WINDOWS
            constexpr char PathSeparator = ';';
#else
            constexpr char PathSeparator = ':';
#endif

            while (start <= pathText.size())
            {
                const size_t separator = pathText.find(PathSeparator, start);
                const size_t count = separator == std::string_view::npos ? std::string_view::npos : separator - start;
                std::string_view segment = pathText.substr(start, count);
                if (!segment.empty())
                {
                    paths.emplace_back(segment);
                }

                if (separator == std::string_view::npos)
                {
                    break;
                }

                start = separator + 1;
            }

            return paths;
        }

        [[nodiscard]] Path FindExecutableInPath(std::string_view executableName)
        {
            const char* pathText = std::getenv("PATH");
            if (pathText == nullptr || pathText[0] == '\0')
            {
                return Path();
            }

            for (const Path& directory : SplitPathEnvironment(pathText))
            {
                const Path candidate = directory / executableName;
                if (FileSystem::IsFile(candidate))
                {
                    return candidate;
                }
            }

            return Path();
        }

        void AddCandidate(RequiredTool& tool, Path candidate)
        {
            if (!candidate.IsEmpty())
            {
                tool.candidates.push_back(std::move(candidate));
            }
        }

        void AddPathCandidate(RequiredTool& tool, std::string_view executableName)
        {
            tool.pathExecutableName = std::string(executableName);
            const Path pathCandidate = FindExecutableInPath(executableName);
            if (!pathCandidate.IsEmpty())
            {
                AddCandidate(tool, pathCandidate);
            }
        }

        [[nodiscard]] Path ResolveToolPath(const RequiredTool& tool)
        {
            for (const Path& candidate : tool.candidates)
            {
                if (FileSystem::IsFile(candidate))
                {
                    return candidate;
                }
            }

            return Path();
        }

        [[nodiscard]] std::string JoinCandidates(const RequiredTool& tool)
        {
            std::ostringstream stream;
            for (size_t index = 0; index < tool.candidates.size(); ++index)
            {
                if (index > 0)
                {
                    stream << ", ";
                }

                stream << tool.candidates[index].GetString();
            }

            if (!tool.pathExecutableName.empty())
            {
                if (!tool.candidates.empty())
                {
                    stream << ", ";
                }

                stream << "PATH search for " << tool.pathExecutableName;
            }

            return stream.str();
        }

        void LogToolchainResolution(const std::vector<RequiredTool>& tools)
        {
            for (const RequiredTool& tool : tools)
            {
                if (!tool.resolvedPath.IsEmpty())
                {
                    VE_LOG_DEBUG_CATEGORY("Editor", "Resolved editor tool: " + tool.displayName + " -> " + tool.resolvedPath.GetString());
                }
            }
        }

        [[nodiscard]] Result<EditorShaderToolchain> ResolveShaderToolchainImpl(bool logMissingTools)
        {
            const Path executableDirectory = FileSystem::GetExecutableDirectory();
            const Path editorBinaryDirectory = GetEditorBinaryDirectory(executableDirectory);
            const Path repositoryRoot = FindRepositoryRoot();

            std::vector<RequiredTool> tools;

#if VE_PLATFORM_WINDOWS
            RequiredTool shaderTool = MakeRequiredTool("VEngineShaderTool");
            AddCandidate(shaderTool, editorBinaryDirectory / "VEngineShaderTool.exe");
            AddCandidate(shaderTool, executableDirectory / "VEngineShaderTool.exe");
            AddPathCandidate(shaderTool, "VEngineShaderTool.exe");
            tools.push_back(std::move(shaderTool));

            RequiredTool dxc = MakeRequiredTool("DirectX Shader Compiler dxc.exe");
            if (!repositoryRoot.IsEmpty())
            {
                AddCandidate(dxc, repositoryRoot / "ThirdParty/DirectXShaderCompiler/Build/Windows64/1.9.2602.17/Tools/x64/dxc.exe");
            }
            AddPathCandidate(dxc, "dxc.exe");
            tools.push_back(std::move(dxc));

            RequiredTool fxc = MakeRequiredTool("Windows SDK fxc.exe");
            if (!repositoryRoot.IsEmpty())
            {
                AddCandidate(fxc, repositoryRoot / "ThirdParty/WindowsSdkTools/Tools/x64/fxc.exe");
            }
            AddPathCandidate(fxc, "fxc.exe");
            tools.push_back(std::move(fxc));

            RequiredTool slang = MakeRequiredTool("Slang compiler slangc.exe");
            if (!repositoryRoot.IsEmpty())
            {
                AddCandidate(slang, repositoryRoot / "ThirdParty/Slang/slang-2026.12-windows-x86_64/bin/slangc.exe");
            }
            AddPathCandidate(slang, "slangc.exe");
            tools.push_back(std::move(slang));
#elif VE_PLATFORM_MACOS
            RequiredTool shaderTool = MakeRequiredTool("VEngineShaderTool");
            AddCandidate(shaderTool, editorBinaryDirectory / "VEngineShaderTool");
            AddCandidate(shaderTool, executableDirectory / "VEngineShaderTool");
            AddPathCandidate(shaderTool, "VEngineShaderTool");
            tools.push_back(std::move(shaderTool));

            RequiredTool slang = MakeRequiredTool("Slang compiler slangc");
            if (!repositoryRoot.IsEmpty())
            {
                AddCandidate(slang, repositoryRoot / "ThirdParty/Slang/slang-2026.12-macos-aarch64/bin/slangc");
            }
            AddPathCandidate(slang, "slangc");
            tools.push_back(std::move(slang));

#else
            return Result<EditorShaderToolchain>::Failure(Error(ErrorCode::Unsupported, "Editor toolchain validation is unsupported on this platform."));
#endif

            size_t missingToolCount = 0;
            for (RequiredTool& tool : tools)
            {
                tool.resolvedPath = ResolveToolPath(tool);
                if (tool.resolvedPath.IsEmpty())
                {
                    ++missingToolCount;
                }
            }

            if (missingToolCount > 0)
            {
                if (logMissingTools)
                {
                    VE_LOG_ERROR_CATEGORY("Editor",
                                          "Editor toolchain validation failed. Repository root: " + repositoryRoot.GetString() +
                                              ", executable directory: " + executableDirectory.GetString() + ", binary directory: " +
                                              editorBinaryDirectory.GetString() + ".");

                    for (const RequiredTool& tool : tools)
                    {
                        if (tool.resolvedPath.IsEmpty())
                        {
                            VE_LOG_ERROR_CATEGORY("Editor", "Missing editor tool: " + tool.displayName + ". Checked: " + JoinCandidates(tool));
                        }
                    }
                }

                return Result<EditorShaderToolchain>::Failure(Error(ErrorCode::NotFound, "Required editor toolchain executable was not found."));
            }

            EditorShaderToolchain toolchain;
            toolchain.shaderTool = tools[0].resolvedPath;
#if VE_PLATFORM_WINDOWS
            toolchain.dxc = tools[1].resolvedPath;
            toolchain.fxc = tools[2].resolvedPath;
            toolchain.slang = tools[3].resolvedPath;
#elif VE_PLATFORM_MACOS
            toolchain.slang = tools[1].resolvedPath;
#endif

            if (logMissingTools)
            {
                LogToolchainResolution(tools);
            }

            return Result<EditorShaderToolchain>::Success(std::move(toolchain));
        }
    } // namespace

    Result<EditorShaderToolchain> ResolveEditorShaderToolchain()
    {
        return ResolveShaderToolchainImpl(false);
    }

    ErrorCode ValidateEditorToolchain()
    {
        Result<EditorShaderToolchain> toolchain = ResolveShaderToolchainImpl(true);
        return toolchain ? ErrorCode::None : toolchain.GetError().GetCode();
    }
} // namespace ve::editor
