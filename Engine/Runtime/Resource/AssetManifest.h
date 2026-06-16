#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <boost/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace ve
{
    enum class ResourceType
    {
        Unknown,
        Mesh,
        Material,
        Scene,
        Text,
        Binary,
    };

    struct AssetRecord
    {
        Guid guid;
        ResourceType type = ResourceType::Unknown;
        Path runtimePath;
        std::vector<Guid> dependencies;
        std::string contentHash;
    };

    class AssetManifest
    {
    public:
        AssetManifest() = default;
        ~AssetManifest() = default;

        [[nodiscard]] static const char* ToString(ResourceType type) noexcept;

        [[nodiscard]] bool IsEmpty() const noexcept;
        void Clear() noexcept;

        [[nodiscard]] SizeT GetAssetCount() const noexcept;
        [[nodiscard]] const AssetRecord* Find(const Guid& guid) const noexcept;
        [[nodiscard]] const AssetRecord* FindByRuntimePath(const Path& runtimePath) const noexcept;
        [[nodiscard]] const std::unordered_map<Guid, AssetRecord>& GetAssets() const noexcept;
        [[nodiscard]] const std::unordered_map<std::string, Guid>& GetGuidsByRuntimePath() const noexcept;

        [[nodiscard]] ErrorCode AddOrUpdate(AssetRecord record);
        [[nodiscard]] ErrorCode LoadFromFile(const Path& path);
        [[nodiscard]] ErrorCode SaveToFile(const Path& path) const;

    private:
        [[nodiscard]] static ResourceType ParseType(std::string_view text) noexcept;
        [[nodiscard]] static boost::json::object WriteRecord(const AssetRecord& record);
        [[nodiscard]] static Result<AssetRecord> ReadRecord(const boost::json::object& object);

        std::unordered_map<Guid, AssetRecord> assets_;
        std::unordered_map<std::string, Guid> guidsByRuntimePath_;
    };
} // namespace ve
