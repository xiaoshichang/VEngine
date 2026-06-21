#include "Engine/Runtime/Resource/ResourceObject.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Resource/ResourceSystem.h"

#include <boost/json.hpp>
#include <cstddef>
#include <string_view>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] Float32 ReadFloat(const boost::json::value& value, Float32 fallback) noexcept
        {
            if (value.is_double())
            {
                return static_cast<Float32>(value.as_double());
            }

            if (value.is_int64())
            {
                return static_cast<Float32>(value.as_int64());
            }

            if (value.is_uint64())
            {
                return static_cast<Float32>(value.as_uint64());
            }

            return fallback;
        }

        [[nodiscard]] std::string ReadString(const boost::json::object& object, boost::json::string_view key, std::string fallback = {})
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }

        [[nodiscard]] rhi::RhiShaderStage ParseShaderStage(std::string_view text) noexcept
        {
            if (text == "Pixel" || text == "Fragment")
            {
                return rhi::RhiShaderStage::Fragment;
            }

            return rhi::RhiShaderStage::Vertex;
        }

        [[nodiscard]] Path ResolveArtifactPath(const Path& projectRoot, const Path& shaderRuntimePath, std::string_view artifactPath)
        {
            Path path(artifactPath);
            if (path.IsAbsolute())
            {
                return path;
            }

            const Path relativeToProject = projectRoot / path;
            if (FileSystem::IsFile(relativeToProject))
            {
                return relativeToProject;
            }

            return projectRoot / shaderRuntimePath.GetParentPath() / path;
        }

        [[nodiscard]] Result<RTShaderResourceDesc> ParseShaderRenderDesc(const std::string& text, const AssetRecord& record, const Path& projectRoot)
        {
            Result<boost::json::value> json = JsonUtils::Parse(text);
            if (!json || !json.GetValue().is_object())
            {
                return Result<RTShaderResourceDesc>::Failure(Error(ErrorCode::InvalidArgument, "Shader descriptor root must be a JSON object."));
            }

            const boost::json::object& object = json.GetValue().as_object();
            RTShaderResourceDesc desc;
            desc.name = ReadString(object, "name", record.runtimePath.GetString());

            const boost::json::value* stagesValue = object.if_contains("stages");
            if (stagesValue == nullptr || !stagesValue->is_array())
            {
                return Result<RTShaderResourceDesc>::Failure(Error(ErrorCode::InvalidArgument, "Shader descriptor missing stages array."));
            }

            for (const boost::json::value& stageValue : stagesValue->as_array())
            {
                if (!stageValue.is_object())
                {
                    continue;
                }

                const boost::json::object& stageObject = stageValue.as_object();
                const boost::json::value* artifactsValue = stageObject.if_contains("artifacts");
                if (artifactsValue == nullptr || !artifactsValue->is_object())
                {
                    continue;
                }

                const boost::json::object& artifacts = artifactsValue->as_object();
                RTShaderStageResourceDesc stageDesc;
                stageDesc.stage = ParseShaderStage(ReadString(stageObject, "stage"));
                stageDesc.entryPoint = ReadString(stageObject, "entry", stageDesc.stage == rhi::RhiShaderStage::Vertex ? "VSMain" : "PSMain");
                stageDesc.debugName = desc.name + (stageDesc.stage == rhi::RhiShaderStage::Vertex ? ".Vertex" : ".Fragment");

                const Path d3d11Path = ResolveArtifactPath(projectRoot, record.runtimePath, ReadString(artifacts, "d3d11"));
                Result<std::vector<std::byte>> d3d11Bytes = FileSystem::ReadBinaryFile(d3d11Path);
                if (!d3d11Bytes)
                {
                    return Result<RTShaderResourceDesc>::Failure(d3d11Bytes.GetError());
                }
                stageDesc.d3d11Bytecode = d3d11Bytes.MoveValue();

                const Path d3d12Path = ResolveArtifactPath(projectRoot, record.runtimePath, ReadString(artifacts, "d3d12"));
                Result<std::vector<std::byte>> d3d12Bytes = FileSystem::ReadBinaryFile(d3d12Path);
                if (!d3d12Bytes)
                {
                    return Result<RTShaderResourceDesc>::Failure(d3d12Bytes.GetError());
                }
                stageDesc.d3d12Bytecode = d3d12Bytes.MoveValue();

                const Path metalPath = ResolveArtifactPath(projectRoot, record.runtimePath, ReadString(artifacts, "metal"));
                Result<std::string> metalSource = FileSystem::ReadTextFile(metalPath);
                if (!metalSource)
                {
                    return Result<RTShaderResourceDesc>::Failure(metalSource.GetError());
                }
                stageDesc.metalSource = metalSource.MoveValue();

                desc.stages.push_back(std::move(stageDesc));
            }

            return Result<RTShaderResourceDesc>::Success(std::move(desc));
        }


        [[nodiscard]] bool ReadFloat3(const boost::json::value& value, Float32 out[3], const Float32 fallback[3]) noexcept
        {
            if (!value.is_array() || value.as_array().size() < 3)
            {
                out[0] = fallback[0];
                out[1] = fallback[1];
                out[2] = fallback[2];
                return false;
            }

            const boost::json::array& array = value.as_array();
            out[0] = ReadFloat(array[0], fallback[0]);
            out[1] = ReadFloat(array[1], fallback[1]);
            out[2] = ReadFloat(array[2], fallback[2]);
            return true;
        }

        [[nodiscard]] RTMeshResourceDesc ParseMeshRenderDesc(const std::string& text, const AssetRecord& record)
        {
            RTMeshResourceDesc desc;
            desc.name = record.runtimePath.GetString();

            Result<boost::json::value> json = JsonUtils::Parse(text);
            if (!json || !json.GetValue().is_object())
            {
                return desc;
            }

            const boost::json::object& object = json.GetValue().as_object();
            const boost::json::array* normals = nullptr;
            if (const boost::json::value* normalsValue = object.if_contains("normals"); normalsValue != nullptr && normalsValue->is_array())
            {
                normals = &normalsValue->as_array();
            }

            if (const boost::json::value* verticesValue = object.if_contains("vertices"); verticesValue != nullptr && verticesValue->is_array())
            {
                constexpr Float32 DefaultPosition[3] = {0.0f, 0.0f, 0.0f};
                constexpr Float32 DefaultNormal[3] = {0.0f, 1.0f, 0.0f};
                SizeT vertexIndex = 0;
                for (const boost::json::value& vertexValue : verticesValue->as_array())
                {
                    RTMeshVertex vertex = {};
                    if (ReadFloat3(vertexValue, vertex.position, DefaultPosition))
                    {
                        if (normals != nullptr && vertexIndex < normals->size())
                        {
                            const bool normalRead = ReadFloat3((*normals)[vertexIndex], vertex.normal, DefaultNormal);
                            (void)normalRead;
                        }
                        else
                        {
                            vertex.normal[0] = DefaultNormal[0];
                            vertex.normal[1] = DefaultNormal[1];
                            vertex.normal[2] = DefaultNormal[2];
                        }

                        desc.vertices.push_back(vertex);
                    }

                    ++vertexIndex;
                }
            }

            if (const boost::json::value* indicesValue = object.if_contains("indices"); indicesValue != nullptr && indicesValue->is_array())
            {
                for (const boost::json::value& indexValue : indicesValue->as_array())
                {
                    if (indexValue.is_uint64())
                    {
                        desc.indices.push_back(static_cast<UInt32>(indexValue.as_uint64()));
                    }
                    else if (indexValue.is_int64() && indexValue.as_int64() >= 0)
                    {
                        desc.indices.push_back(static_cast<UInt32>(indexValue.as_int64()));
                    }
                }
            }

            return desc;
        }

        [[nodiscard]] RTMaterialResourceDesc ParseMaterialRenderDesc(const std::string& text, const AssetRecord& record)
        {
            RTMaterialResourceDesc desc;
            desc.name = record.runtimePath.GetString();

            Result<boost::json::value> json = JsonUtils::Parse(text);
            if (!json || !json.GetValue().is_object())
            {
                return desc;
            }

            const boost::json::object& object = json.GetValue().as_object();
            const boost::json::value* parametersValue = object.if_contains("parameters");
            if (parametersValue == nullptr || !parametersValue->is_object())
            {
                return desc;
            }

            const boost::json::value* baseColorValue = parametersValue->as_object().if_contains("baseColor");
            if (baseColorValue == nullptr || !baseColorValue->is_array() || baseColorValue->as_array().size() < 4)
            {
                return desc;
            }

            const boost::json::array& baseColor = baseColorValue->as_array();
            desc.baseColor =
                Vector4(ReadFloat(baseColor[0], 1.0f), ReadFloat(baseColor[1], 1.0f), ReadFloat(baseColor[2], 1.0f), ReadFloat(baseColor[3], 1.0f));
            return desc;
        }

        [[nodiscard]] RTMaterialResourceDesc BuildMaterialRenderDesc(
            const std::string& text, const AssetRecord& record, std::shared_ptr<RTShaderResource> shaderResource)
        {
            RTMaterialResourceDesc desc = ParseMaterialRenderDesc(text, record);
            desc.shaderResource = std::move(shaderResource);
            return desc;
        }
    } // namespace

    ResourceObject::ResourceObject(AssetRecord record)
        : record_(std::move(record))
    {
    }

    ResourceObject::~ResourceObject() = default;

    const AssetRecord& ResourceObject::GetAssetRecord() const noexcept
    {
        return record_;
    }

    const AssetID& ResourceObject::GetAssetID() const noexcept
    {
        return record_.id;
    }

    ResourceType ResourceObject::GetType() const noexcept
    {
        return record_.type;
    }

    const Path& ResourceObject::GetRuntimePath() const noexcept
    {
        return record_.runtimePath;
    }

    const std::vector<AssetID>& ResourceObject::GetDependencies() const noexcept
    {
        return record_.dependencies;
    }

    ErrorCode ResourceObject::Load(ResourceLoadContext& context)
    {
        for (const AssetID& dependency : record_.dependencies)
        {
            Result<ResourceObject*> dependencyResource = context.RequestDependency(dependency);
            if (!dependencyResource)
            {
                return dependencyResource.GetError().GetCode();
            }
        }

        return ErrorCode::None;
    }

    void ResourceObject::InitRenderResource(RenderSystem& renderSystem)
    {
        (void)renderSystem;
    }

    void ResourceObject::ReleaseRenderResource(RenderSystem& renderSystem) noexcept
    {
        (void)renderSystem;
    }

    MeshResource::MeshResource(AssetRecord record, std::string text)
        : ResourceObject(std::move(record))
        , text_(std::move(text))
        , rtMeshResource_(std::make_shared<RTMeshResource>(ParseMeshRenderDesc(text_, GetAssetRecord())))
    {
    }

    const std::string& MeshResource::GetText() const noexcept
    {
        return text_;
    }

    std::shared_ptr<RTMeshResource> MeshResource::GetRTMeshResource() const noexcept
    {
        return rtMeshResource_;
    }

    void MeshResource::InitRenderResource(RenderSystem& renderSystem)
    {
        renderSystem.InitRenderResource(rtMeshResource_, ParseMeshRenderDesc(text_, GetAssetRecord()));
    }

    void MeshResource::ReleaseRenderResource(RenderSystem& renderSystem) noexcept
    {
        try
        {
            renderSystem.EnqueueCommand(
                RenderCommand{"ReleaseRTMeshResource", [rtMeshResource = rtMeshResource_]() { rtMeshResource->ResetRenderResource(); }});
        }
        catch (...)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "MeshResource failed to enqueue render resource release.");
        }
    }

    MaterialResource::MaterialResource(AssetRecord record, std::string text)
        : ResourceObject(std::move(record))
        , text_(std::move(text))
        , rtMaterialResource_(std::make_shared<RTMaterialResource>(ParseMaterialRenderDesc(text_, GetAssetRecord())))
    {
    }

    const std::string& MaterialResource::GetText() const noexcept
    {
        return text_;
    }

    std::shared_ptr<RTMaterialResource> MaterialResource::GetRTMaterialResource() const noexcept
    {
        return rtMaterialResource_;
    }

    ErrorCode MaterialResource::Load(ResourceLoadContext& context)
    {
        for (const AssetID& dependency : GetDependencies())
        {
            Result<ResourceObject*> dependencyResource = context.RequestDependency(dependency);
            if (!dependencyResource)
            {
                return dependencyResource.GetError().GetCode();
            }

            if (dependencyResource.GetValue()->GetType() == ResourceType::Shader)
            {
                ShaderResource* shaderResource = dynamic_cast<ShaderResource*>(dependencyResource.GetValue());
                if (shaderResource != nullptr)
                {
                    rtShaderResource_ = shaderResource->GetRTShaderResource();
                }
            }
        }

        rtMaterialResource_ = std::make_shared<RTMaterialResource>(BuildMaterialRenderDesc(text_, GetAssetRecord(), rtShaderResource_));
        return ErrorCode::None;
    }

    void MaterialResource::InitRenderResource(RenderSystem& renderSystem)
    {
        renderSystem.InitRenderResource(rtMaterialResource_, BuildMaterialRenderDesc(text_, GetAssetRecord(), rtShaderResource_));
    }

    void MaterialResource::ReleaseRenderResource(RenderSystem& renderSystem) noexcept
    {
        try
        {
            renderSystem.EnqueueCommand(
                RenderCommand{"ReleaseRTMaterialResource", [rtMaterialResource = rtMaterialResource_]() { rtMaterialResource->ResetRenderResource(); }});
        }
        catch (...)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "MaterialResource failed to enqueue render resource release.");
        }
    }

    ShaderResource::ShaderResource(AssetRecord record, std::string text)
        : ResourceObject(std::move(record))
        , text_(std::move(text))
        , reflectionText_(text_)
        , rtShaderResource_(std::make_shared<RTShaderResource>(RTShaderResourceDesc{}))
    {
    }

    const std::string& ShaderResource::GetText() const noexcept
    {
        return text_;
    }

    const std::string& ShaderResource::GetReflectionText() const noexcept
    {
        return reflectionText_;
    }

    std::shared_ptr<RTShaderResource> ShaderResource::GetRTShaderResource() const noexcept
    {
        return rtShaderResource_;
    }

    ErrorCode ShaderResource::Load(ResourceLoadContext& context)
    {
        Result<RTShaderResourceDesc> desc = ParseShaderRenderDesc(text_, GetAssetRecord(), context.resourceSystem.GetProjectRoot());
        if (!desc)
        {
            return desc.GetError().GetCode();
        }

        reflectionText_ = text_;
        rtShaderResource_ = std::make_shared<RTShaderResource>(desc.MoveValue());
        return ErrorCode::None;
    }

    void ShaderResource::InitRenderResource(RenderSystem& renderSystem)
    {
        renderSystem.InitRenderResource(rtShaderResource_, rtShaderResource_->GetDesc());
    }

    void ShaderResource::ReleaseRenderResource(RenderSystem& renderSystem) noexcept
    {
        try
        {
            renderSystem.EnqueueCommand(
                RenderCommand{"ReleaseRTShaderResource", [rtShaderResource = rtShaderResource_]() { rtShaderResource->ResetRenderResource(); }});
        }
        catch (...)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "ShaderResource failed to enqueue render resource release.");
        }
    }

    SceneResource::SceneResource(AssetRecord record, std::string text)
        : ResourceObject(std::move(record))
        , text_(std::move(text))
    {
    }

    const std::string& SceneResource::GetText() const noexcept
    {
        return text_;
    }

    TextureResource::TextureResource(AssetRecord record, std::vector<std::byte> bytes)
        : ResourceObject(std::move(record))
        , bytes_(std::move(bytes))
    {
    }

    const std::vector<std::byte>& TextureResource::GetBytes() const noexcept
    {
        return bytes_;
    }
} // namespace ve
