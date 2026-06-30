#include "Engine/Runtime/Core/Version.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct CompileOptions
    {
        std::filesystem::path sourcePath;
        std::filesystem::path outputDirectory;
        std::string shaderName;
        std::filesystem::path dxcExecutable = "dxc";
        std::filesystem::path fxcExecutable = "fxc";
        std::filesystem::path slangExecutable = "slangc";
    };

    struct ShaderStage
    {
        std::string_view displayName;
        std::string_view shortName;
        std::string_view entryPoint;
        std::string_view slangStage;
        std::string_view d3d11Profile;
        std::string_view dxcProfile;
    };

    struct BindingInfo
    {
        std::string name;
        std::string kind;
        std::string hlslRegister;
        int bindGroup = 0;
        int binding = 0;
        int metalIndex = 0;
    };

    constexpr ShaderStage ShaderStages[] = {
        {"Vertex", "VS", "VSMain", "vertex", "vs_5_0", "vs_6_0"},
        {"Pixel", "PS", "PSMain", "pixel", "ps_5_0", "ps_6_0"},
    };

    void PrintHelp()
    {
        std::cout << "VEngineShaderTool\n"
                  << "\n"
                  << "Commands:\n"
                  << "  compile --source <file> --output <dir> --name <shader> [--dxc <path>] [--fxc <path>] "
                     "[--slang <path>]\n"
                  << "  --help\n";
        std::cout << "Shader flow: HLSL -> Slang/FXC/DXC -> DXBC, DXIL, Metal MSL, reflection\n";
    }

    std::string EscapeJson(std::string_view text)
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

    std::string QuoteCommandArgument(std::string_view argument)
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

    int RunProcess(const std::vector<std::string>& arguments)
    {
        std::ostringstream command;

        for (size_t index = 0; index < arguments.size(); ++index)
        {
            if (index > 0)
            {
                command << ' ';
            }

            command << QuoteCommandArgument(arguments[index]);
        }

        std::string commandLine = command.str();

#if defined(_WIN32)
        commandLine = "\"" + commandLine + "\"";
#endif

        std::cout << commandLine << '\n';
        return std::system(commandLine.c_str());
    }

    std::optional<std::string> ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);

        if (!input)
        {
            return std::nullopt;
        }

        std::ostringstream content;
        content << input.rdbuf();
        return content.str();
    }

    bool WriteTextFile(const std::filesystem::path& path, std::string_view content)
    {
        std::ofstream output(path, std::ios::binary);

        if (!output)
        {
            return false;
        }

        output << content;
        return true;
    }

    std::string StripLineComment(std::string_view line)
    {
        const size_t commentStart = line.find("//");

        if (commentStart == std::string_view::npos)
        {
            return std::string(line);
        }

        return std::string(line.substr(0, commentStart));
    }

    std::optional<std::string> ValidateExplicitBindings(std::string_view source)
    {
        std::istringstream input{std::string(source)};
        std::string line;
        int lineNumber = 0;
        const std::regex cbufferDeclaration(R"(\bcbuffer\s+[A-Za-z_][A-Za-z0-9_]*)");
        const std::regex textureDeclaration(R"(\bTexture[A-Za-z0-9_<>]*\s+[A-Za-z_][A-Za-z0-9_]*)");
        const std::regex samplerDeclaration(R"(\bSamplerState\s+[A-Za-z_][A-Za-z0-9_]*)");

        while (std::getline(input, line))
        {
            ++lineNumber;
            const std::string strippedLine = StripLineComment(line);

            const bool hasBindableDeclaration = std::regex_search(strippedLine, cbufferDeclaration) || std::regex_search(strippedLine, textureDeclaration) ||
                                                std::regex_search(strippedLine, samplerDeclaration);

            if (!hasBindableDeclaration)
            {
                continue;
            }

            if (strippedLine.find("register(") == std::string::npos)
            {
                return "Bindable shader resource on line " + std::to_string(lineNumber) + " must declare an explicit register.";
            }

            if (strippedLine.find("space") == std::string::npos)
            {
                return "Bindable shader resource on line " + std::to_string(lineNumber) + " must declare an explicit register space.";
            }
        }

        return std::nullopt;
    }

    std::string BuildD3D11Source(std::string source)
    {
        const std::regex registerSpacePattern(R"(register\s*\(\s*([bstu][0-9]+)\s*,\s*space[0-9]+\s*\))");
        return std::regex_replace(source, registerSpacePattern, "register($1)");
    }

    void AppendBinding(std::vector<BindingInfo>& bindings, const std::smatch& match, std::string kind, std::string registerPrefix)
    {
        BindingInfo binding;
        binding.kind = std::move(kind);
        binding.name = match[1].str();
        binding.binding = std::stoi(match[2].str());
        binding.bindGroup = std::stoi(match[3].str());
        binding.hlslRegister = registerPrefix + std::to_string(binding.binding);
        bindings.push_back(std::move(binding));
    }

    std::vector<BindingInfo> ExtractBindings(std::string_view source)
    {
        std::vector<BindingInfo> bindings;
        const std::string sourceText(source);

        const std::regex cbufferRegex(R"(\bcbuffer\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*register\s*\(\s*b([0-9]+)\s*,\s*space([0-9]+)\s*\))");
        const std::regex textureRegex(R"(\bTexture[A-Za-z0-9_<>]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*register\s*\(\s*t([0-9]+)\s*,\s*space([0-9]+)\s*\))");
        const std::regex samplerRegex(R"(\bSamplerState\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*register\s*\(\s*s([0-9]+)\s*,\s*space([0-9]+)\s*\))");

        for (std::sregex_iterator it(sourceText.begin(), sourceText.end(), cbufferRegex), end; it != end; ++it)
        {
            AppendBinding(bindings, *it, "ConstantBuffer", "b");
        }

        for (std::sregex_iterator it(sourceText.begin(), sourceText.end(), textureRegex), end; it != end; ++it)
        {
            AppendBinding(bindings, *it, "Texture", "t");
        }

        for (std::sregex_iterator it(sourceText.begin(), sourceText.end(), samplerRegex), end; it != end; ++it)
        {
            AppendBinding(bindings, *it, "Sampler", "s");
        }

        std::map<std::string, int> nextMetalIndices;

        for (BindingInfo& binding : bindings)
        {
            std::string metalSpace = "buffer";

            if (binding.kind == "Texture")
            {
                metalSpace = "texture";
            }
            else if (binding.kind == "Sampler")
            {
                metalSpace = "sampler";
            }

            binding.metalIndex = nextMetalIndices[metalSpace]++;
        }

        return bindings;
    }

    std::filesystem::path BuildArtifactPath(const CompileOptions& options, const ShaderStage& stage, std::string_view extension)
    {
        return options.outputDirectory / (options.shaderName + "." + std::string(stage.shortName) + "." + std::string(extension));
    }

#if defined(_WIN32)
    bool CompileStageWindows(const CompileOptions& options, const ShaderStage& stage, const std::filesystem::path& d3d11SourcePath)
    {
        const std::filesystem::path dxbcPath = BuildArtifactPath(options, stage, "dxbc");
        const std::filesystem::path dxilPath = BuildArtifactPath(options, stage, "dxil");
        const std::filesystem::path metalPath = BuildArtifactPath(options, stage, "metal");
        const std::filesystem::path reflectionPath = BuildArtifactPath(options, stage, "reflect.json");

        if (RunProcess({
                options.fxcExecutable.string(),
                "/nologo",
                "/T",
                std::string(stage.d3d11Profile),
                "/E",
                std::string(stage.entryPoint),
                "/Fo",
                dxbcPath.string(),
                d3d11SourcePath.string(),
            }) != 0)
        {
            return false;
        }

        if (RunProcess({
                options.dxcExecutable.string(),
                "-nologo",
                "-T",
                std::string(stage.dxcProfile),
                "-E",
                std::string(stage.entryPoint),
                "-Fo",
                dxilPath.string(),
                options.sourcePath.string(),
            }) != 0)
        {
            return false;
        }

        if (RunProcess({
                options.slangExecutable.string(),
                "-stage",
                std::string(stage.slangStage),
                "-entry",
                std::string(stage.entryPoint),
                "-profile",
                std::string(stage.dxcProfile),
                "-target",
                "metal",
                "-o",
                metalPath.string(),
                "-reflection-json",
                reflectionPath.string(),
                options.sourcePath.string(),
            }) != 0)
        {
            return false;
        }

        return true;
    }
#endif

    bool CompileStageApple(const CompileOptions& options, const ShaderStage& stage)
    {
        const std::filesystem::path metalPath = BuildArtifactPath(options, stage, "metal");
        const std::filesystem::path reflectionPath = BuildArtifactPath(options, stage, "reflect.json");

        if (RunProcess({
                options.slangExecutable.string(),
                "-stage",
                std::string(stage.slangStage),
                "-entry",
                std::string(stage.entryPoint),
                "-profile",
                std::string(stage.dxcProfile),
                "-target",
                "metal",
                "-o",
                metalPath.string(),
                "-reflection-json",
                reflectionPath.string(),
                options.sourcePath.string(),
            }) != 0)
        {
            return false;
        }

        std::cout << "  Metal MSL: " << metalPath << '\n';
        std::cout << "  Reflection: " << reflectionPath << '\n';

        return true;
    }

    bool WriteNormalizedReflection(const CompileOptions& options, const std::vector<BindingInfo>& bindings)
    {
        const std::filesystem::path reflectionPath = options.outputDirectory / (options.shaderName + ".veshader.json");
        std::ofstream output(reflectionPath, std::ios::binary);

        if (!output)
        {
            std::cerr << "Failed to write " << reflectionPath << '\n';
            return false;
        }

        output << "{\n";
        output << "  \"schemaVersion\": 1,\n";
        output << "  \"name\": \"" << EscapeJson(options.shaderName) << "\",\n";
        output << "  \"source\": \"" << EscapeJson(options.sourcePath.generic_string()) << "\",\n";
        output << "  \"stages\": [\n";

        for (size_t index = 0; index < std::size(ShaderStages); ++index)
        {
            const ShaderStage& stage = ShaderStages[index];
            output << "    {\n";
            output << "      \"stage\": \"" << stage.displayName << "\",\n";
            output << "      \"entry\": \"" << stage.entryPoint << "\",\n";
            output << "      \"artifacts\": {\n";
            output << "        \"d3d11\": \"" << EscapeJson(BuildArtifactPath(options, stage, "dxbc").generic_string()) << "\",\n";
            output << "        \"d3d12\": \"" << EscapeJson(BuildArtifactPath(options, stage, "dxil").generic_string()) << "\",\n";
            output << "        \"metal\": \"" << EscapeJson(BuildArtifactPath(options, stage, "metal").generic_string()) << "\",\n";
            output << "        \"reflection\": \"" << EscapeJson(BuildArtifactPath(options, stage, "reflect.json").generic_string()) << "\"\n";
            output << "      }\n";
            output << "    }";

            if (index + 1 < std::size(ShaderStages))
            {
                output << ',';
            }

            output << '\n';
        }

        output << "  ],\n";
        output << "  \"resources\": [\n";

        for (size_t index = 0; index < bindings.size(); ++index)
        {
            const BindingInfo& binding = bindings[index];
            output << "    {\n";
            output << "      \"name\": \"" << EscapeJson(binding.name) << "\",\n";
            output << "      \"kind\": \"" << binding.kind << "\",\n";
            output << "      \"bindGroup\": " << binding.bindGroup << ",\n";
            output << "      \"binding\": " << binding.binding << ",\n";
            output << "      \"hlslRegister\": \"" << binding.hlslRegister << "\",\n";
            output << "      \"hlslSpace\": " << binding.bindGroup << ",\n";
            output << "      \"metalIndex\": " << binding.metalIndex << "\n";
            output << "    }";

            if (index + 1 < bindings.size())
            {
                output << ',';
            }

            output << '\n';
        }

        output << "  ]\n";
        output << "}\n";
        return true;
    }

    std::optional<std::string> ReadOptionValue(int& index, int argc, char* argv[])
    {
        if (index + 1 >= argc)
        {
            return std::nullopt;
        }

        ++index;
        return std::string(argv[index]);
    }

    std::optional<CompileOptions> ParseCompileOptions(int argc, char* argv[])
    {
        CompileOptions options;

        for (int index = 2; index < argc; ++index)
        {
            const std::string_view argument = argv[index];
            std::optional<std::string> value;

            if (argument == "--source")
            {
                value = ReadOptionValue(index, argc, argv);
                if (!value)
                {
                    return std::nullopt;
                }

                options.sourcePath = *value;
            }
            else if (argument == "--output")
            {
                value = ReadOptionValue(index, argc, argv);
                if (!value)
                {
                    return std::nullopt;
                }

                options.outputDirectory = *value;
            }
            else if (argument == "--name")
            {
                value = ReadOptionValue(index, argc, argv);
                if (!value)
                {
                    return std::nullopt;
                }

                options.shaderName = *value;
            }
            else if (argument == "--dxc")
            {
                value = ReadOptionValue(index, argc, argv);
                if (!value)
                {
                    return std::nullopt;
                }

                options.dxcExecutable = *value;
            }
            else if (argument == "--fxc")
            {
                value = ReadOptionValue(index, argc, argv);
                if (!value)
                {
                    return std::nullopt;
                }

                options.fxcExecutable = *value;
            }
            else if (argument == "--slang")
            {
                value = ReadOptionValue(index, argc, argv);
                if (!value)
                {
                    return std::nullopt;
                }

                options.slangExecutable = *value;
            }
            else
            {
                std::cerr << "Unknown argument: " << argument << '\n';
                return std::nullopt;
            }
        }

        if (options.sourcePath.empty() || options.outputDirectory.empty() || options.shaderName.empty())
        {
            return std::nullopt;
        }

        return options;
    }

    int CompileShader(const CompileOptions& options)
    {
        const std::optional<std::string> source = ReadTextFile(options.sourcePath);

        if (!source)
        {
            std::cerr << "Failed to read shader source: " << options.sourcePath << '\n';
            return 1;
        }

        if (const std::optional<std::string> validationError = ValidateExplicitBindings(*source))
        {
            std::cerr << *validationError << '\n';
            return 1;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(options.outputDirectory, errorCode);

        if (errorCode)
        {
            std::cerr << "Failed to create shader output directory: " << options.outputDirectory << '\n';
            return 1;
        }

        const std::filesystem::path d3d11SourcePath = options.outputDirectory / (options.shaderName + ".D3D11.hlsl");

        if (!WriteTextFile(d3d11SourcePath, BuildD3D11Source(*source)))
        {
            std::cerr << "Failed to write D3D11 shader source: " << d3d11SourcePath << '\n';
            return 1;
        }

        for (const ShaderStage& stage : ShaderStages)
        {
#if defined(_WIN32)
            if (!CompileStageWindows(options, stage, d3d11SourcePath))
            {
                std::cerr << "Failed to compile " << options.shaderName << " " << stage.displayName << " shader" << '\n';
                return 1;
            }
#else
            if (!CompileStageApple(options, stage))
            {
                std::cerr << "Failed to compile " << options.shaderName << " " << stage.displayName << " shader" << '\n';
                return 1;
            }
#endif
        }

        const std::vector<BindingInfo> bindings = ExtractBindings(*source);

        if (!WriteNormalizedReflection(options, bindings))
        {
            return 1;
        }

        std::cout << "Shader flow complete: " << options.shaderName << '\n';
        return 0;
    }
} // namespace

int main(int argc, char* argv[])
{
    const ve::BuildInfo buildInfo = ve::GetBuildInfo();

    if (argc <= 1 || std::string_view(argv[1]) == "--help")
    {
        PrintHelp();
        std::cout << "Build: " << buildInfo.projectName << " " << buildInfo.version << '\n';
        return 0;
    }

    const std::string_view command = argv[1];

    if (command == "compile")
    {
        const std::optional<CompileOptions> options = ParseCompileOptions(argc, argv);

        if (!options)
        {
            PrintHelp();
            return 1;
        }

        return CompileShader(*options);
    }

    std::cerr << "Unknown command: " << command << '\n';
    PrintHelp();
    return 1;
}
