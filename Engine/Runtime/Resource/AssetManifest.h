#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetRecord.h"

#include <boost/json.hpp>

#include <string>
#include <unordered_map>

namespace ve
{
    class AssetManifest
    {
    public:
        AssetManifest() = default;
        ~AssetManifest() = default;

        [[nodiscard]] static const char* ToString(ResourceType type) noexcept;

        [[nodiscard]] bool IsEmpty() const noexcept;
        void Clear() noexcept;

        [[nodiscard]] SizeT GetAssetCount() const noexcept;
        [[nodiscard]] const ManifestAssetRecord* Find(const AssetID& id) const noexcept;
        [[nodiscard]] const ManifestAssetRecord* FindByRuntimePath(const Path& runtimePath) const noexcept;
        [[nodiscard]] const std::unordered_map<AssetID, ManifestAssetRecord>& GetAssets() const noexcept;
        [[nodiscard]] const std::unordered_map<std::string, AssetID>& GetAssetIDsByRuntimePath() const noexcept;

        [[nodiscard]] ErrorCode AddOrUpdate(ManifestAssetRecord record);
        [[nodiscard]] ErrorCode LoadFromFile(const Path& path);
        [[nodiscard]] ErrorCode SaveToFile(const Path& path) const;

    private:
        [[nodiscard]] static ResourceType ParseType(std::string_view text) noexcept;
        [[nodiscard]] static boost::json::object WriteAssetID(const AssetID& id);
        [[nodiscard]] static boost::json::array WriteAssetIDArray(const std::vector<AssetID>& ids);
        [[nodiscard]] static Result<AssetID> ReadAssetID(const boost::json::object& object);
        [[nodiscard]] static std::vector<AssetID> ReadAssetIDArray(const boost::json::object& object,
                                                                   boost::json::string_view key,
                                                                   UInt64 version);
        [[nodiscard]] static boost::json::object WriteRecord(const ManifestAssetRecord& record);
        [[nodiscard]] static Result<ManifestAssetRecord> ReadRecord(const boost::json::object& object, UInt64 version);

        std::unordered_map<AssetID, ManifestAssetRecord> assets_;
        std::unordered_map<std::string, AssetID> assetIDsByRuntimePath_;
    };
} // namespace ve
