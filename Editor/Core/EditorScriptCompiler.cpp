#include "Editor/Core/EditorScriptCompiler.h"

#include "Editor/Core/EditorScriptProjectGenerator.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <cstdlib>

namespace ve::editor
{
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

        const std::string command = "dotnet build " + Quote(projectFile) + " --configuration Debug --output " + Quote(outputDirectory) +
                                    " -p:VEngineScriptHostAssembly=" + Quote(desc.scriptHostAssemblyPath) + " --nologo";
        const int result = std::system(command.c_str());
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

    std::string EditorScriptCompiler::Quote(const Path& path)
    {
        return Quote(path.GetString());
    }

    std::string EditorScriptCompiler::Quote(const std::string& text)
    {
        std::string quoted = "\"";
        quoted += text;
        quoted += "\"";
        return quoted;
    }
} // namespace ve::editor
