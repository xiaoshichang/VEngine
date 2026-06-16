#include "Engine/Runtime/Resource/AssetManifest.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <boost/json.hpp>

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

        [[nodiscard]] Guid ReadGuid(const boost::json::object& object, boost::json::string_view key)
        {
            Result<Guid> guid = Guid::Parse(ReadString(object, key));
            return guid ? guid.GetValue() : Guid();
        }

        [[nodiscard]] boost::json::array WriteGuidArray(const std::vector<Guid>& guids)
        {
            boost::json::array array;
            for (const Guid& guid : guids)
            {
                array.emplace_back(guid.ToString());
            }
            return array;
        }

        [[nodiscard]] std::vector<Guid> ReadGuidArray(const boost::json::object& object, boost::json::string_view key)
        {
            std::vector<Guid> guids;
            const boost::json::value* value = object.if_contains(key);
            if (value == nullptr || !value->is_array())
            {
                return guids;
            }

            for (const boost::json::value& item : value->as_array())
            {
                if (!item.is_string())
                {
                    continue;
                }

                Result<Guid> guid = Guid::Parse(std::string(item.as_string()));
                if (guid)
                {
                    guids.push_back(guid.GetValue());
                }
            }

            return guids;
        }
    } // namespace

    bool AssetManifest::IsEmpty() const noexcept
    {
        return assets_.empty();
    }

    void AssetManifest::Clear() noexcept
    {
        assets_.clear();
        guidsByRuntimePath_.clear();
    }

    SizeT AssetManifest::GetAssetCount() const noexcept
    {
        return assets_.size();
    }

    const AssetRecord* AssetManifest::Find(const Guid& guid) const noexcept
    {
        const auto it = assets_.find(guid);
        return it != assets_.end() ? &it->second : nullptr;
    }

    const AssetRecord* AssetManifest::FindByRuntimePath(const Path& runtimePath) const noexcept
    {
        const auto guidIt = guidsByRuntimePath_.find(runtimePath.GetString());
        if (guidIt == guidsByRuntimePath_.end())
        {
            return nullptr;
        }

        return Find(guidIt->second);
    }

    const std::unordered_map<Guid, AssetRecord>& AssetManifest::GetAssets() const noexcept
    {
        return assets_;
    }

    const std::unordered_map<std::string, Guid>& AssetManifest::GetGuidsByRuntimePath() const noexcept
    {
        return guidsByRuntimePath_;
    }

    ErrorCode AssetManifest::AddOrUpdate(AssetRecord record)
    {
        if (record.guid.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const Guid guid = record.guid;
        const std::string runtimePathKey = record.runtimePath.GetString();
        if (!runtimePathKey.empty())
        {
            if (const auto existingGuid = guidsByRuntimePath_.find(runtimePathKey);
                existingGuid != guidsByRuntimePath_.end() && existingGuid->second != guid)
            {
                assets_.erase(existingGuid->second);
            }
        }

        if (const auto existing = assets_.find(guid); existing != assets_.end())
        {
            guidsByRuntimePath_.erase(existing->second.runtimePath.GetString());
        }

        assets_.insert_or_assign(guid, std::move(record));
        if (!runtimePathKey.empty())
        {
            guidsByRuntimePath_.insert_or_assign(runtimePathKey, guid);
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

            Result<AssetRecord> record = ReadRecord(value.as_object());
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
        object["version"] = 1;

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
        case ResourceType::Mesh:
            return "Mesh";
        case ResourceType::Material:
            return "Material";
        case ResourceType::Scene:
            return "Scene";
        case ResourceType::Text:
            return "Text";
        case ResourceType::Binary:
            return "Binary";
        }

        return "Unknown";
    }

    ResourceType AssetManifest::ParseType(std::string_view text) noexcept
    {
        if (text == "Mesh")
        {
            return ResourceType::Mesh;
        }

        if (text == "Material")
        {
            return ResourceType::Material;
        }

        if (text == "Scene")
        {
            return ResourceType::Scene;
        }

        if (text == "Text")
        {
            return ResourceType::Text;
        }

        if (text == "Binary")
        {
            return ResourceType::Binary;
        }

        return ResourceType::Unknown;
    }

    boost::json::object AssetManifest::WriteRecord(const AssetRecord& record)
    {
        boost::json::object object;
        object["guid"] = record.guid.ToString();
        object["type"] = ToString(record.type);
        object["runtimePath"] = record.runtimePath.GetString();
        object["dependencies"] = WriteGuidArray(record.dependencies);
        object["contentHash"] = record.contentHash;
        return object;
    }

    Result<AssetRecord> AssetManifest::ReadRecord(const boost::json::object& object)
    {
        AssetRecord record;
        record.guid = ReadGuid(object, "guid");
        if (record.guid.IsEmpty())
        {
            return Result<AssetRecord>::Failure(Error(ErrorCode::InvalidArgument, "Asset GUID is invalid."));
        }

        record.type = ParseType(ReadString(object, "type"));
        record.runtimePath = Path(ReadString(object, "runtimePath"));
        record.dependencies = ReadGuidArray(object, "dependencies");
        record.contentHash = ReadString(object, "contentHash");
        return Result<AssetRecord>::Success(std::move(record));
    }
} // namespace ve
