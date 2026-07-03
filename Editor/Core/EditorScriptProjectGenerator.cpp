#include "Editor/Core/EditorScriptProjectGenerator.h"

#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <set>

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

        [[nodiscard]] std::string EscapeCSharpString(std::string_view text)
        {
            std::string escaped;
            escaped.reserve(text.size());
            for (const char value : text)
            {
                switch (value)
                {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '"':
                    escaped += "\\\"";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped += value;
                    break;
                }
            }

            return escaped;
        }

        [[nodiscard]] std::string StripCSharpComments(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            bool inLineComment = false;
            bool inBlockComment = false;
            bool inString = false;
            bool inChar = false;
            bool escaped = false;

            for (size_t index = 0; index < text.size(); ++index)
            {
                const char value = text[index];
                const char next = index + 1 < text.size() ? text[index + 1] : '\0';

                if (inLineComment)
                {
                    if (value == '\n')
                    {
                        inLineComment = false;
                        result.push_back(value);
                    }
                    else
                    {
                        result.push_back(' ');
                    }
                    continue;
                }

                if (inBlockComment)
                {
                    if (value == '*' && next == '/')
                    {
                        inBlockComment = false;
                        result.push_back(' ');
                        result.push_back(' ');
                        ++index;
                    }
                    else
                    {
                        result.push_back(value == '\n' ? '\n' : ' ');
                    }
                    continue;
                }

                if (!inString && !inChar && value == '/' && next == '/')
                {
                    inLineComment = true;
                    result.push_back(' ');
                    result.push_back(' ');
                    ++index;
                    continue;
                }

                if (!inString && !inChar && value == '/' && next == '*')
                {
                    inBlockComment = true;
                    result.push_back(' ');
                    result.push_back(' ');
                    ++index;
                    continue;
                }

                result.push_back(value);

                if (escaped)
                {
                    escaped = false;
                    continue;
                }

                if ((inString || inChar) && value == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (!inChar && value == '"')
                {
                    inString = !inString;
                }
                else if (!inString && value == '\'')
                {
                    inChar = !inChar;
                }
            }

            return result;
        }

        [[nodiscard]] std::string ExtractCSharpNamespace(const std::string& text)
        {
            static const std::regex namespaceRegex(R"(\bnamespace\s+([A-Za-z_][A-Za-z0-9_.]*))");
            std::smatch match;
            return std::regex_search(text, match, namespaceRegex) ? match[1].str() : std::string();
        }

        [[nodiscard]] std::vector<std::string> ExtractScriptTypeNames(const std::string& text)
        {
            static const std::regex classRegex(R"(\bclass\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([^{;]+))");

            const std::string namespaceName = ExtractCSharpNamespace(text);
            std::vector<std::string> scriptTypeNames;
            for (std::sregex_iterator iterator(text.begin(), text.end(), classRegex), end; iterator != end; ++iterator)
            {
                const std::string className = (*iterator)[1].str();
                const std::string baseList = (*iterator)[2].str();
                if (baseList.find("ScriptComponent") == std::string::npos)
                {
                    continue;
                }

                scriptTypeNames.push_back(namespaceName.empty() ? className : namespaceName + "." + className);
            }

            return scriptTypeNames;
        }

        [[nodiscard]] std::vector<std::string> CollectScriptTypeNames(const Path& projectRoot, const std::vector<Path>& scriptAssetPaths)
        {
            std::set<std::string> uniqueTypeNames;
            for (const Path& scriptPath : scriptAssetPaths)
            {
                Result<std::string> scriptText = FileSystem::ReadTextFile(projectRoot / scriptPath);
                if (!scriptText)
                {
                    continue;
                }

                const std::string strippedText = StripCSharpComments(scriptText.GetValue());
                for (std::string& typeName : ExtractScriptTypeNames(strippedText))
                {
                    uniqueTypeNames.insert(std::move(typeName));
                }
            }

            return std::vector<std::string>(uniqueTypeNames.begin(), uniqueTypeNames.end());
        }

        [[nodiscard]] Path GetGeneratedSourceDirectory(const Path& projectRoot)
        {
            return projectRoot / EditorProject::LibraryDirectoryName / "Scripting" / "Generated";
        }

        [[nodiscard]] Path MakeNativeAOTRegistryPath(const Path& projectRoot)
        {
            return GetGeneratedSourceDirectory(projectRoot) / "VEngineNativeAOTScriptRegistry.g.cs";
        }

        [[nodiscard]] std::string MakeGlobalTypeName(std::string_view typeName)
        {
            return std::string("global::") + std::string(typeName);
        }

        [[nodiscard]] std::string BuildNativeAOTRegistrySource(const std::vector<std::string>& scriptTypeNames)
        {
            std::ostringstream source;
            source << "// <auto-generated />\n";
            source << "#if VENGINE_IOS_NATIVEAOT\n";
            source << "using System;\n";
            source << "using System.Runtime.CompilerServices;\n";
            source << "using System.Runtime.InteropServices;\n";
            source << "using VEngine.Scripting;\n\n";
            source << "namespace VEngine.Generated;\n\n";
            source << "internal static class VEngineNativeAOTScriptRegistry\n";
            source << "{\n";
            source << "    [ModuleInitializer]\n";
            source << "    internal static void Register()\n";
            source << "    {\n";
            for (const std::string& scriptTypeName : scriptTypeNames)
            {
                source << "        NativeScriptBridge.RegisterLinkedScriptType<" << MakeGlobalTypeName(scriptTypeName) << ">();\n";
            }
            source << "    }\n\n";
            source << "    internal static readonly string[] RegisteredTypeNames = new string[]\n";
            source << "    {\n";
            for (const std::string& scriptTypeName : scriptTypeNames)
            {
                source << "        \"" << EscapeCSharpString(scriptTypeName) << "\",\n";
            }
            source << "    };\n";
            source << "}\n";
            source << "\n";
            source << "internal static unsafe class VEngineNativeAOTScriptExports\n";
            source << "{\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_RegisterNativeApi\")]\n";
            source << "    internal static void RegisterNativeApi(NativeScriptApi* nativeApi)\n";
            source << "    {\n";
            source << "        NativeScriptBridge.RegisterNativeApi(nativeApi);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_LoadProjectAssembly\")]\n";
            source << "    internal static int LoadProjectAssembly(byte* assemblyPath)\n";
            source << "    {\n";
            source << "        return NativeScriptBridge.LoadProjectAssembly(assemblyPath);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_UnloadProjectAssembly\")]\n";
            source << "    internal static void UnloadProjectAssembly()\n";
            source << "    {\n";
            source << "        NativeScriptBridge.UnloadProjectAssembly();\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_GetScriptTypesJson\")]\n";
            source << "    internal static nint GetScriptTypesJson()\n";
            source << "    {\n";
            source << "        return NativeScriptBridge.GetScriptTypesJson();\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_FreeString\")]\n";
            source << "    internal static void FreeString(nint text)\n";
            source << "    {\n";
            source << "        NativeScriptBridge.FreeString(text);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_CreateScript\")]\n";
            source << "    internal static ulong CreateScript(nint nativeComponent, byte* scriptTypeName, int invokeOnCreate)\n";
            source << "    {\n";
            source << "        return NativeScriptBridge.CreateScript(nativeComponent, scriptTypeName, invokeOnCreate);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_DestroyScript\")]\n";
            source << "    internal static void DestroyScript(ulong script)\n";
            source << "    {\n";
            source << "        NativeScriptBridge.DestroyScript(script);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_OnCreate\")]\n";
            source << "    internal static void OnCreate(ulong script)\n";
            source << "    {\n";
            source << "        NativeScriptBridge.OnCreate(script);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_GetScriptFieldsJson\")]\n";
            source << "    internal static nint GetScriptFieldsJson(ulong script)\n";
            source << "    {\n";
            source << "        return NativeScriptBridge.GetScriptFieldsJson(script);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_SetScriptFieldsJson\")]\n";
            source << "    internal static int SetScriptFieldsJson(ulong script, byte* fieldsJson)\n";
            source << "    {\n";
            source << "        return NativeScriptBridge.SetScriptFieldsJson(script, fieldsJson);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_SetScriptFieldJson\")]\n";
            source << "    internal static int SetScriptFieldJson(ulong script, byte* fieldName, byte* valueJson)\n";
            source << "    {\n";
            source << "        return NativeScriptBridge.SetScriptFieldJson(script, fieldName, valueJson);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_OnUpdate\")]\n";
            source << "    internal static void OnUpdate(ulong script, float deltaSeconds)\n";
            source << "    {\n";
            source << "        NativeScriptBridge.OnUpdate(script, deltaSeconds);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_OnLateUpdate\")]\n";
            source << "    internal static void OnLateUpdate(ulong script, float deltaSeconds)\n";
            source << "    {\n";
            source << "        NativeScriptBridge.OnLateUpdate(script, deltaSeconds);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_OnEnable\")]\n";
            source << "    internal static void OnEnable(ulong script)\n";
            source << "    {\n";
            source << "        NativeScriptBridge.OnEnable(script);\n";
            source << "    }\n\n";
            source << "    [UnmanagedCallersOnly(EntryPoint = \"VEngine_OnDisable\")]\n";
            source << "    internal static void OnDisable(ulong script)\n";
            source << "    {\n";
            source << "        NativeScriptBridge.OnDisable(script);\n";
            source << "    }\n";
            source << "}\n";
            source << "#endif\n";
            return source.str();
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
        const std::vector<std::string> nativeAOTScriptTypeNames = CollectScriptTypeNames(desc.projectRoot, scriptAssetPaths);

        const Path generatedSourceDirectory = GetGeneratedSourceDirectory(desc.projectRoot);
        ErrorCode directoryResult = FileSystem::CreateDirectories(generatedSourceDirectory);
        if (directoryResult != ErrorCode::None)
        {
            return Result<Path>::Failure(Error(directoryResult, "Failed to create generated script source directory."));
        }

        const Path nativeAOTRegistryPath = MakeNativeAOTRegistryPath(desc.projectRoot);
        ErrorCode registryWriteResult = FileSystem::WriteTextFile(nativeAOTRegistryPath, BuildNativeAOTRegistrySource(nativeAOTScriptTypeNames));
        if (registryWriteResult != ErrorCode::None)
        {
            return Result<Path>::Failure(Error(registryWriteResult, "Failed to write NativeAOT script registry source."));
        }

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
        project << "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n";
        project << "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n";
        project << "    <OutputType>Library</OutputType>\n";
        project << "    <PublishAot Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true'\">true</PublishAot>\n";
        project << "    <PublishAotUsingRuntimePack Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true'\">true</PublishAotUsingRuntimePack>\n";
        project << "    <NativeLib Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true'\">Static</NativeLib>\n";
        project << "    <SelfContained Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true'\">true</SelfContained>\n";
        project << "    <InvariantGlobalization Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true'\">true</InvariantGlobalization>\n";
        project << "    <AppleMinOSVersion Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true' and '$(AppleMinOSVersion)' == ''\">17.0</AppleMinOSVersion>\n";
        project << "    <IsAotCompatible Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true'\">true</IsAotCompatible>\n";
        project << "    <JsonSerializerIsReflectionEnabledByDefault Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true'\">false</JsonSerializerIsReflectionEnabledByDefault>\n";
        project << "    <DefineConstants Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true'\">"
                << "$(DefineConstants);VENGINE_IOS_NATIVEAOT</DefineConstants>\n";
        project << "    <VEngineScriptHostAssembly Condition=\"'$(VEngineScriptHostAssembly)' == ''\">"
                << "$(MSBuildThisFileDirectory)..\\..\\..\\Build\\windows-msvc-debug\\Managed\\VEngine.ScriptHost\\VEngine.ScriptHost.dll"
                << "</VEngineScriptHostAssembly>\n";
        project << "    <VEngineScriptHostProject Condition=\"'$(VEngineScriptHostProject)' == ''\"></VEngineScriptHostProject>\n";
        project << "  </PropertyGroup>\n\n";

        project << "  <Target Name=\"ValidateVEngineIOSNativeAOTInputs\" BeforeTargets=\"ResolveReferences\" "
                   "Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true' and '$(VEngineScriptHostProject)' == ''\">\n";
        project << "    <Error Text=\"VEngineEnableIOSNativeAOT requires VEngineScriptHostProject to point at VEngine.ScriptHost.csproj.\" />\n";
        project << "  </Target>\n\n";

        project << "  <ItemGroup>\n";
        for (const Path& scriptPath : scriptAssetPaths)
        {
            const std::string includePath = ToMsBuildPath(ToProjectFileRelativePath(scriptPath));
            const std::string linkPath = ToMsBuildPath(scriptPath.GetString());
            project << "    <Compile Include=\"" << EscapeXml(includePath) << "\" Link=\"" << EscapeXml(linkPath) << "\" />\n";
        }
        project << "    <Compile Include=\"Generated\\VEngineNativeAOTScriptRegistry.g.cs\" />\n";
        project << "  </ItemGroup>\n\n";

        project << "  <ItemGroup Condition=\"'$(VEngineEnableIOSNativeAOT)' != 'true'\">\n";
        project << "    <Reference Include=\"VEngine.ScriptHost\">\n";
        project << "      <HintPath>$(VEngineScriptHostAssembly)</HintPath>\n";
        project << "      <Private>false</Private>\n";
        project << "    </Reference>\n";
        project << "  </ItemGroup>\n";
        project << "  <ItemGroup Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true' and '$(VEngineScriptHostProject)' != ''\">\n";
        project << "    <ProjectReference Include=\"$(VEngineScriptHostProject)\" />\n";
        project << "  </ItemGroup>\n";
        project << "  <ItemGroup Condition=\"'$(VEngineEnableIOSNativeAOT)' == 'true'\">\n";
        project << "    <TrimmerRootAssembly Include=\"VEngine.ScriptHost\" />\n";
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

    Path EditorScriptProjectGenerator::GetNativeAOTRegistryPath(const Path& projectRoot)
    {
        return MakeNativeAOTRegistryPath(projectRoot);
    }
} // namespace ve::editor
