#pragma once

#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/ResourceManifest.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ve
{
    struct ResourceSystemInitParam
    {
        Path projectRoot;
    };

    using ResourceResolveCallback = std::function<Result<ResourceRecord>(const Guid&)>;

    struct LoadedResourceData
    {
        Guid guid;
        ResourceType type = ResourceType::Unknown;
        Path runtimePath;
        std::string text;
        std::vector<std::byte> bytes;
    };

    class ResourceSystem : public NonMovable
    {
    public:
        ResourceSystem() = default;
        ~ResourceSystem() = default;

        [[nodiscard]] ErrorCode Initialize(const ResourceSystemInitParam& desc);
        void Shutdown() noexcept;
        void SetProjectRoot(Path projectRoot) noexcept;
        void SetManifestPath(Path manifestPath) noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const Path& GetProjectRoot() const noexcept;
        [[nodiscard]] const Path& GetManifestPath() const noexcept;
        [[nodiscard]] const ResourceManifest& GetManifest() const noexcept;
        [[nodiscard]] ResourceManifest& GetManifest() noexcept;

        void SetResourceResolveCallback(ResourceResolveCallback callback) noexcept;

        [[nodiscard]] ErrorCode ReloadManifest();
        [[nodiscard]] ErrorCode ReloadManifest(Path manifestPath);
        [[nodiscard]] Result<ResourceRecord> FindResource(const Guid& guid) const;
        [[nodiscard]] Result<LoadedResourceData> LoadResource(const Guid& guid);
        void ClearCache() noexcept;

    private:
        [[nodiscard]] Path ResolveRuntimePath(const ResourceRecord& record) const;
        [[nodiscard]] Result<LoadedResourceData> LoadFromRecord(const ResourceRecord& record);

        Path projectRoot_;
        Path manifestPath_;
        mutable ResourceManifest manifest_;
        ResourceResolveCallback resourceResolveCallback_;
        std::unordered_map<Guid, LoadedResourceData> cache_;
        bool initialized_ = false;
    };
} // namespace ve
