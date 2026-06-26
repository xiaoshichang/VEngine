#include "Editor/Core/EditorScriptProjectGenerator.h"

#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] std::string EscapeXml(std::string_view text)
        {
            std::string escaped;
            escaped.reserve(text.size());
            for (const char value : text)
            {
                switch (value)
                {
                case '&':
                    escaped += "&amp;";
                    break;
                case '<':
                    escaped += "&lt;";
                    break;
                case '>':
                    escaped += "&gt;";
                    break;
                case '"':
                    escaped += "&quot;";
                    break;
                case '\'':
                    escaped += "&apos;";
                    break;
                default:
                    escaped += value;
                    break;
                }
            }

            return escaped;
        }

        [[nodiscard]] std::string MakeRootNamespace(std::string_view projectName)
        {
            std::string rootNamespace;
            rootNamespace.reserve(projectName.size());
            for (const unsigned char value : projectName)
            {
                if (std::isalnum(value) != 0 || value == '_' || value == '.')
                {
                    rootNamespace += static_cast<char>(value);
                }
                else
                {
                    rootNamespace += '_';
                }
            }

            if (rootNamespace.empty() || std::isdigit(static_cast<unsigned char>(rootNamespace.front())) != 0)
            {
                rootNamespace.insert(rootNamespace.begin(), '_');
            }

            return rootNamespace;
        }

        [[nodiscard]] std::string ToProjectFileRelativePath(const Path& assetPath)
        {
            return "../../" + assetPath.GetString();
        }

        [[nodiscard]] std::string ToMsBuildPath(std::string path)
        {
            std::replace(path.begin(), path.end(), '/', '\\');
            return path;
        }
    } // namespace

    Result<Path> EditorScriptProjectGenerator::GenerateProject(const EditorScriptProjectGenerateDesc& desc)
    {
        if (desc.projectRoot.IsEmpty() || desc.projectName.empty())
        {
            return Result<Path>::Failure(Error(ErrorCode::InvalidArgument, "Script project generation requires a project root and project name."));
        }

        std::vector<Path> scriptAssetPaths = desc.scriptAssetPaths;
        std::sort(scriptAssetPaths.begin(),
                  scriptAssetPaths.end(),
                  [](const Path& left, const Path& right) { return left.GetString() < right.GetString(); });

        const std::string assemblyName = desc.projectName + ".Scripts";
        const std::string rootNamespace = MakeRootNamespace(desc.projectName);

        std::ostringstream project;
        project << "<Project>\n";
        project << "  <PropertyGroup>\n";
        project << "    <BaseIntermediateOutputPath>$(MSBuildThisFileDirectory)output\\obj\\</BaseIntermediateOutputPath>\n";
        project << "    <MSBuildProjectExtensionsPath>$(BaseIntermediateOutputPath)</MSBuildProjectExtensionsPath>\n";
        project << "    <BaseOutputPath>$(MSBuildThisFileDirectory)output\\bin\\</BaseOutputPath>\n";
        project << "    <OutputPath>$(MSBuildThisFileDirectory)output\\bin\\$(Configuration)\\</OutputPath>\n";
        project << "  </PropertyGroup>\n";
        project << "  <Import Project=\"Sdk.props\" Sdk=\"Microsoft.NET.Sdk\" />\n\n";
        project << "  <PropertyGroup>\n";
        project << "    <TargetFramework>net10.0</TargetFramework>\n";
        project << "    <AssemblyName>" << EscapeXml(assemblyName) << "</AssemblyName>\n";
        project << "    <RootNamespace>" << EscapeXml(rootNamespace) << "</RootNamespace>\n";
        project << "    <Nullable>enable</Nullable>\n";
        project << "    <ImplicitUsings>enable</ImplicitUsings>\n";
        project << "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n";
        project << "    <OutputType>Library</OutputType>\n";
        project << "    <VEngineScriptHostAssembly Condition=\"'$(VEngineScriptHostAssembly)' == ''\">"
                << "$(MSBuildThisFileDirectory)..\\..\\..\\Build\\windows-msvc-debug\\Managed\\VEngine.ScriptHost\\VEngine.ScriptHost.dll"
                << "</VEngineScriptHostAssembly>\n";
        project << "  </PropertyGroup>\n\n";

        project << "  <ItemGroup>\n";
        for (const Path& scriptPath : scriptAssetPaths)
        {
            const std::string includePath = ToMsBuildPath(ToProjectFileRelativePath(scriptPath));
            const std::string linkPath = ToMsBuildPath(scriptPath.GetString());
            project << "    <Compile Include=\"" << EscapeXml(includePath) << "\" Link=\"" << EscapeXml(linkPath) << "\" />\n";
        }
        project << "  </ItemGroup>\n\n";

        project << "  <ItemGroup>\n";
        project << "    <Reference Include=\"VEngine.ScriptHost\">\n";
        project << "      <HintPath>$(VEngineScriptHostAssembly)</HintPath>\n";
        project << "      <Private>false</Private>\n";
        project << "    </Reference>\n";
        project << "  </ItemGroup>\n";
        project << "  <Import Project=\"Sdk.targets\" Sdk=\"Microsoft.NET.Sdk\" />\n";
        project << "</Project>\n";

        const Path projectPath = GetGeneratedProjectPath(desc.projectRoot, desc.projectName);
        const ErrorCode writeResult = FileSystem::WriteTextFile(projectPath, project.str());
        if (writeResult != ErrorCode::None)
        {
            return Result<Path>::Failure(Error(writeResult, "Failed to write generated script project file."));
        }

        return Result<Path>::Success(projectPath);
    }

    Path EditorScriptProjectGenerator::GetGeneratedProjectPath(const Path& projectRoot, std::string_view projectName)
    {
        return projectRoot / EditorProject::LibraryDirectoryName / "Scripting" / (std::string(projectName) + ".Scripts.csproj");
    }
} // namespace ve::editor
