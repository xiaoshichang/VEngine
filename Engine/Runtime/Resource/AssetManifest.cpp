#include "Engine/Runtime/Resource/AssetManifest.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] std::string ReadString(const boost::json::object& object,
                                             boost::json::string_view key,
                                             std::string fallback = {})
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }

        [[nodiscard]] UInt64 ReadUInt64(const boost::json::object& object,
                                        boost::json::string_view key,
                                        UInt64 fallback = 0)
        {
            const boost::json::value* value = object.if_contains(key);
            if (value == nullptr)
            {
                return fallback;
            }

            if (value->is_uint64())
            {
                return static_cast<UInt64>(value->as_uint64());
            }

            if (value->is_int64() && value->as_int64() >= 0)
            {
                return static_cast<UInt64>(value->as_int64());
            }

            return fallback;
        }

        [[nodiscard]] Result<Guid> ReadGuid(const boost::json::object& object, boost::json::string_view key)
        {
            Result<Guid> guid = Guid::Parse(ReadString(object, key));
            if (!guid)
            {
                return Result<Guid>::Failure(guid.GetError());
            }

            return guid;
        }
    } // namespace

    bool AssetManifest::IsEmpty() const noexcept
    {
        return assets_.empty();
    }

    void AssetManifest::Clear() noexcept
    {
        assets_.clear();
        assetIDsByRuntimePath_.clear();
    }

    SizeT AssetManifest::GetAssetCount() const noexcept
    {
        return assets_.size();
    }

    const ManifestAssetRecord* AssetManifest::Find(const AssetID& id) const noexcept
    {
        const auto it = assets_.find(id);
        return it != assets_.end() ? &it->second : nullptr;
    }

    const ManifestAssetRecord* AssetManifest::FindByRuntimePath(const Path& runtimePath) const noexcept
    {
        const auto idIt = assetIDsByRuntimePath_.find(runtimePath.GetString());
        if (idIt == assetIDsByRuntimePath_.end())
        {
            return nullptr;
        }

        return Find(idIt->second);
    }

    const std::unordered_map<AssetID, ManifestAssetRecord>& AssetManifest::GetAssets() const noexcept
    {
        return assets_;
    }

    const std::unordered_map<std::string, AssetID>& AssetManifest::GetAssetIDsByRuntimePath() const noexcept
    {
        return assetIDsByRuntimePath_;
    }

    ErrorCode AssetManifest::AddOrUpdate(ManifestAssetRecord record)
    {
        if (record.asset.id.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const AssetID id = record.asset.id;
        const std::string runtimePathKey = record.asset.runtimePath.GetString();
        if (!runtimePathKey.empty())
        {
            if (const auto existingID = assetIDsByRuntimePath_.find(runtimePathKey);
                existingID != assetIDsByRuntimePath_.end() && existingID->second != id)
            {
                assets_.erase(existingID->second);
            }
        }

        if (const auto existing = assets_.find(id); existing != assets_.end())
        {
            assetIDsByRuntimePath_.erase(existing->second.asset.runtimePath.GetString());
        }

        assets_.insert_or_assign(id, std::move(record));
        if (!runtimePathKey.empty())
        {
            assetIDsByRuntimePath_.insert_or_assign(runtimePathKey, id);
        }
        return ErrorCode::None;
    }

    ErrorCode AssetManifest::LoadFromFile(const Path& path)
    {
        Clear();

        Result<std::string> text = FileSystem::ReadTextFile(path);
        if (!text)
        {
            return text.GetError().GetCode();
        }

        Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
        if (!json)
        {
            return json.GetError().GetCode();
        }

        if (!json.GetValue().is_object())
        {
            return ErrorCode::InvalidArgument;
        }

        const boost::json::object& object = json.GetValue().as_object();
        const UInt64 version = ReadUInt64(object, "version", 1);
        const boost::json::value* assetsValue = object.if_contains("assets");
        if (assetsValue == nullptr || !assetsValue->is_array())
        {
            return ErrorCode::InvalidArgument;
        }

        for (const boost::json::value& value : assetsValue->as_array())
        {
            if (!value.is_object())
            {
                return ErrorCode::InvalidArgument;
            }

            Result<ManifestAssetRecord> record = ReadRecord(value.as_object(), version);
            if (!record)
            {
                return record.GetError().GetCode();
            }

            const ErrorCode addResult = AddOrUpdate(record.MoveValue());
            if (addResult != ErrorCode::None)
            {
                return addResult;
            }
        }

        return ErrorCode::None;
    }

    ErrorCode AssetManifest::SaveToFile(const Path& path) const
    {
        boost::json::object object;
        object["version"] = 2;

        boost::json::array assets;
        for (const auto& pair : assets_)
        {
            assets.emplace_back(WriteRecord(pair.second));
        }

        object["assets"] = std::move(assets);
        return FileSystem::WriteTextFile(path, JsonUtils::SerializePretty(object));
    }

    const char* AssetManifest::ToString(ResourceType type) noexcept
    {
        switch (type)
        {
        case ResourceType::Unknown:
            return "Unknown";
        case ResourceType::Scene:
            return "Scene";
        case ResourceType::Mesh:
            return "Mesh";
        case ResourceType::Material:
            return "Material";
        case ResourceType::Texture:
            return "Texture";
        }

        return "Unknown";
    }

    ResourceType AssetManifest::ParseType(std::string_view text) noexcept
    {
        if (text == "Scene")
        {
            return ResourceType::Scene;
        }

        if (text == "Mesh")
        {
            return ResourceType::Mesh;
        }

        if (text == "Material")
        {
            return ResourceType::Material;
        }

        if (text == "Texture")
        {
            return ResourceType::Texture;
        }

        return ResourceType::Unknown;
    }

    boost::json::object AssetManifest::WriteAssetID(const AssetID& id)
    {
        boost::json::object object;
        object["guid"] = id.GetGuid().ToString();
        object["subID"] = id.GetSubID();
        return object;
    }

    boost::json::array AssetManifest::WriteAssetIDArray(const std::vector<AssetID>& ids)
    {
        boost::json::array array;
        for (const AssetID& id : ids)
        {
            array.emplace_back(WriteAssetID(id));
        }
        return array;
    }

    Result<AssetID> AssetManifest::ReadAssetID(const boost::json::object& object)
    {
        Result<Guid> guid = ReadGuid(object, "guid");
        if (!guid)
        {
            return Result<AssetID>::Failure(guid.GetError());
        }

        return Result<AssetID>::Success(AssetID(guid.GetValue(), ReadUInt64(object, "subID", 0)));
    }

    std::vector<AssetID> AssetManifest::ReadAssetIDArray(const boost::json::object& object,
                                                         boost::json::string_view key,
                                                         UInt64 version)
    {
        std::vector<AssetID> ids;
        const boost::json::value* value = object.if_contains(key);
        if (value == nullptr || !value->is_array())
        {
            return ids;
        }

        for (const boost::json::value& item : value->as_array())
        {
            if (version <= 1)
            {
                if (!item.is_string())
                {
                    continue;
                }

                Result<Guid> guid = Guid::Parse(std::string(item.as_string()));
                if (guid)
                {
                    ids.emplace_back(guid.GetValue(), 0);
                }
                continue;
            }

            if (!item.is_object())
            {
                continue;
            }

            Result<AssetID> id = ReadAssetID(item.as_object());
            if (id)
            {
                ids.push_back(id.GetValue());
            }
        }

        return ids;
    }

    boost::json::object AssetManifest::WriteRecord(const ManifestAssetRecord& record)
    {
        boost::json::object object;
        object["assetID"] = WriteAssetID(record.asset.id);
        object["type"] = ToString(record.asset.type);
        object["runtimePath"] = record.asset.runtimePath.GetString();
        object["dependencies"] = WriteAssetIDArray(record.asset.dependencies);
        object["contentHash"] = record.asset.contentHash;
        object["bundle"] = record.bundle;
        return object;
    }

    Result<ManifestAssetRecord> AssetManifest::ReadRecord(const boost::json::object& object, UInt64 version)
    {
        ManifestAssetRecord record;
        if (version <= 1)
        {
            Result<Guid> guid = ReadGuid(object, "guid");
            if (!guid)
            {
                return Result<ManifestAssetRecord>::Failure(guid.GetError());
            }
            record.asset.id = AssetID(guid.GetValue(), 0);
        }
        else
        {
            const boost::json::value* assetIDValue = object.if_contains("assetID");
            if (assetIDValue == nullptr || !assetIDValue->is_object())
            {
                return Result<ManifestAssetRecord>::Failure(
                    Error(ErrorCode::InvalidArgument, "Asset manifest record missing assetID."));
            }

            Result<AssetID> id = ReadAssetID(assetIDValue->as_object());
            if (!id)
            {
                return Result<ManifestAssetRecord>::Failure(id.GetError());
            }
            record.asset.id = id.GetValue();
        }

        if (record.asset.id.IsEmpty())
        {
            return Result<ManifestAssetRecord>::Failure(Error(ErrorCode::InvalidArgument, "AssetID is invalid."));
        }

        record.asset.type = ParseType(ReadString(object, "type"));
        record.asset.runtimePath = Path(ReadString(object, "runtimePath"));
        record.asset.dependencies = ReadAssetIDArray(object, "dependencies", version);
        record.asset.contentHash = ReadString(object, "contentHash");
        record.bundle = ReadString(object, "bundle");
        return Result<ManifestAssetRecord>::Success(std::move(record));
    }
} // namespace ve
