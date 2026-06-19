#include "Engine/Runtime/Resource/ResourceObject.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Resource/ResourceSystem.h"

#include <boost/json.hpp>
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

    void MaterialResource::InitRenderResource(RenderSystem& renderSystem)
    {
        renderSystem.InitRenderResource(rtMaterialResource_, ParseMaterialRenderDesc(text_, GetAssetRecord()));
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
