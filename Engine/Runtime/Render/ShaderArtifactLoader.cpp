#include "Engine/Runtime/Render/ShaderArtifactLoader.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"

#include <boost/json.hpp>
#include <filesystem>
#include <unordered_map>

namespace ve
{
    namespace
    {
        [[nodiscard]] std::string ReadString(const boost::json::object& object, std::string_view key)
        {
            const boost::json::value* value = object.if_contains(key);
            return value != nullptr && value->is_string() ? std::string(value->as_string()) : std::string();
        }

        [[nodiscard]] Path FindDescriptor(std::string_view shaderName)
        {
            const Path importedRoot = FileSystem::GetProjectRoot() / "Library" / "Imported";
            if (!FileSystem::IsDirectory(importedRoot))
            {
                return {};
            }

            static std::string indexedProjectRoot;
            static std::unordered_map<std::string, Path> descriptors;
            if (indexedProjectRoot == FileSystem::GetProjectRoot().GetString())
            {
                const auto descriptor = descriptors.find(std::string(shaderName));
                return descriptor != descriptors.end() ? descriptor->second : Path();
            }

            indexedProjectRoot = FileSystem::GetProjectRoot().GetString();
            descriptors.clear();
            std::error_code errorCode;
            for (std::filesystem::recursive_directory_iterator it(importedRoot.GetString(), errorCode), end; it != end && !errorCode;
                 it.increment(errorCode))
            {
                const std::string filename = it->path().filename().generic_string();
                constexpr std::string_view suffix = ".veshader.json";
                if (it->is_regular_file(errorCode) && filename.ends_with(suffix))
                {
                    descriptors.insert_or_assign(filename.substr(0, filename.size() - suffix.size()), Path(it->path().generic_string()));
                }
            }
            const auto descriptor = descriptors.find(std::string(shaderName));
            return descriptor != descriptors.end() ? descriptor->second : Path();
        }

        [[nodiscard]] rhi::RhiShaderStage ParseStage(std::string_view value) noexcept
        {
            if (value == "Compute") return rhi::RhiShaderStage::Compute;
            if (value == "Pixel" || value == "Fragment") return rhi::RhiShaderStage::Fragment;
            return rhi::RhiShaderStage::Vertex;
        }

        [[nodiscard]] Path ResolveArtifactPath(const Path& descriptorPath, std::string_view artifact)
        {
            const Path path{std::string(artifact)};
            if (path.IsAbsolute()) return path;
            const Path projectPath = FileSystem::GetProjectRoot() / path;
            return FileSystem::IsFile(projectPath) ? projectPath : descriptorPath.GetParentPath() / path;
        }
    } // namespace

    Result<ShaderArtifactModule> LoadShaderArtifact(std::string_view shaderName, std::string_view passName, rhi::RhiShaderStage stage)
    {
        const Path descriptorPath = FindDescriptor(shaderName);
        if (descriptorPath.IsEmpty())
        {
            return Result<ShaderArtifactModule>::Failure(Error(ErrorCode::NotFound, "Imported shader descriptor not found: " + std::string(shaderName)));
        }

        Result<std::string> descriptorText = FileSystem::ReadTextFile(descriptorPath);
        if (!descriptorText) return Result<ShaderArtifactModule>::Failure(descriptorText.GetError());
        Result<boost::json::value> descriptorJson = JsonUtils::Parse(descriptorText.GetValue());
        if (!descriptorJson || !descriptorJson.GetValue().is_object())
        {
            return Result<ShaderArtifactModule>::Failure(Error(ErrorCode::InvalidArgument, "Imported shader descriptor is invalid."));
        }

        const boost::json::value* passesValue = descriptorJson.GetValue().as_object().if_contains("passes");
        if (passesValue == nullptr || !passesValue->is_array())
        {
            return Result<ShaderArtifactModule>::Failure(Error(ErrorCode::InvalidArgument, "Imported shader descriptor has no passes."));
        }

        const boost::json::object* selectedStage = nullptr;
        for (const boost::json::value& passValue : passesValue->as_array())
        {
            if (!passValue.is_object() || ReadString(passValue.as_object(), "name") != passName) continue;
            const boost::json::value* stagesValue = passValue.as_object().if_contains("stages");
            if (stagesValue == nullptr || !stagesValue->is_array()) break;
            for (const boost::json::value& stageValue : stagesValue->as_array())
            {
                if (stageValue.is_object() && ParseStage(ReadString(stageValue.as_object(), "stage")) == stage)
                {
                    selectedStage = &stageValue.as_object();
                    break;
                }
            }
            break;
        }
        if (selectedStage == nullptr)
        {
            return Result<ShaderArtifactModule>::Failure(Error(ErrorCode::NotFound, "Imported shader pass stage not found."));
        }

        ShaderArtifactModule module;
        module.stage = stage;
        module.entryPoint = ReadString(*selectedStage, "entry");
        const boost::json::value* artifactsValue = selectedStage->if_contains("artifacts");
        if (module.entryPoint.empty() || artifactsValue == nullptr || !artifactsValue->is_object())
        {
            return Result<ShaderArtifactModule>::Failure(Error(ErrorCode::InvalidArgument, "Imported shader stage has no entry or artifacts."));
        }
        const boost::json::object& artifacts = artifactsValue->as_object();
#if VE_PLATFORM_WINDOWS
        Result<std::vector<std::byte>> d3d11 = FileSystem::ReadBinaryFile(ResolveArtifactPath(descriptorPath, ReadString(artifacts, "d3d11")));
        Result<std::vector<std::byte>> d3d12 = FileSystem::ReadBinaryFile(ResolveArtifactPath(descriptorPath, ReadString(artifacts, "d3d12")));
        if (!d3d11 || !d3d12) return Result<ShaderArtifactModule>::Failure(!d3d11 ? d3d11.GetError() : d3d12.GetError());
        module.d3d11Bytecode = d3d11.MoveValue();
        module.d3d12Bytecode = d3d12.MoveValue();
#elif VE_PLATFORM_MACOS || VE_PLATFORM_IOS
        Result<std::string> metal = FileSystem::ReadTextFile(ResolveArtifactPath(descriptorPath, ReadString(artifacts, "metal")));
        if (!metal) return Result<ShaderArtifactModule>::Failure(metal.GetError());
        module.metalSource = metal.MoveValue();
#else
        return Result<ShaderArtifactModule>::Failure(Error(ErrorCode::Unsupported, "Unsupported platform for shader artifacts."));
#endif
        return Result<ShaderArtifactModule>::Success(std::move(module));
    }

    rhi::RhiShaderModule* GetOrCompileShaderArtifact(ShaderManager& shaderManager,
                                                     rhi::RhiDevice& device,
                                                     ShaderID id,
                                                     std::string_view shaderName,
                                                     std::string_view passName,
                                                     rhi::RhiShaderStage stage,
                                                     const char* debugName)
    {
        if (rhi::RhiShaderModule* cached = shaderManager.GetShader(id); cached != nullptr)
        {
            return cached;
        }
        Result<ShaderArtifactModule> result = LoadShaderArtifact(shaderName, passName, stage);
        if (!result)
        {
            VE_LOG_ERROR("Failed to load imported shader artifact '{}.{}': {}", shaderName, passName, result.GetError().GetMessage());
            return nullptr;
        }
        ShaderArtifactModule& module = result.GetValue();
        rhi::RhiShaderModuleDesc desc = {};
        desc.stage = stage;
        desc.entryPoint = module.entryPoint.c_str();
        desc.debugName = debugName;
        if (device.GetBackend() == rhi::RhiBackend::D3D11)
        {
            desc.codeFormat = rhi::RhiShaderCodeFormat::Bytecode;
            desc.bytecode = module.d3d11Bytecode.data();
            desc.bytecodeSize = module.d3d11Bytecode.size();
        }
        else if (device.GetBackend() == rhi::RhiBackend::D3D12)
        {
            desc.codeFormat = rhi::RhiShaderCodeFormat::Bytecode;
            desc.bytecode = module.d3d12Bytecode.data();
            desc.bytecodeSize = module.d3d12Bytecode.size();
        }
        else
        {
            desc.codeFormat = rhi::RhiShaderCodeFormat::Source;
            desc.source = module.metalSource.c_str();
        }
        return shaderManager.GetOrCompileShader(device, std::move(id), desc);
    }
} // namespace ve
