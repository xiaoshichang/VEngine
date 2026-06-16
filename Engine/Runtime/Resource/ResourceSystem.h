#pragma once

#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/ResourceManifest.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ve
{
    struct ResourceSystemInitParam
    {
        Path projectRoot;
    };

    struct LoadedResourceData
    {
        Guid guid;
        ResourceType type = ResourceType::Unknown;
        Path runtimePath;
        std::string text;
        std::vector<std::byte> bytes;
    };

    struct ResourceCollectUnusedParams
    {
        std::vector<Guid> rootGuids;
        bool unloadPinnedResources = false;
    };

    class ResourceLoadOperation
    {
    public:
        explicit ResourceLoadOperation(Result<LoadedResourceData> result);

        [[nodiscard]] bool IsComplete() const noexcept;
        [[nodiscard]] const Result<LoadedResourceData>& GetResult() const noexcept;
        [[nodiscard]] Result<LoadedResourceData>& GetResult() noexcept;

    private:
        Result<LoadedResourceData> result_;
    };

    class ResourceSystem : public NonMovable
    {
    public:
        ResourceSystem() = default;
        ~ResourceSystem() = default;

        [[nodiscard]] ErrorCode Initialize(const ResourceSystemInitParam& desc);
        void Shutdown() noexcept;
        void SetProjectRoot(Path projectRoot) noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const Path& GetProjectRoot() const noexcept;

        [[nodiscard]] Result<LoadedResourceData> LoadResource(const ResourceRecord& record);
        [[nodiscard]] ResourceLoadOperation LoadResourceAsync(const ResourceRecord& record);
        [[nodiscard]] ErrorCode UnloadResource(const Guid& guid);
        [[nodiscard]] SizeT CollectUnusedResources(const ResourceCollectUnusedParams& params);
        void ClearCache() noexcept;

    private:
        struct LoadedResourceEntry
        {
            LoadedResourceData data;
            std::vector<Guid> dependencies;
            bool pinned = false;
        };

        [[nodiscard]] Path ResolveRuntimePath(const ResourceRecord& record) const;
        [[nodiscard]] Result<LoadedResourceData> LoadFromRecord(const ResourceRecord& record);
        void MarkReachableResource(const Guid& guid, std::vector<Guid>& reachableResources) const;

        Path projectRoot_;
        std::unordered_map<Guid, LoadedResourceEntry> cache_;
        bool initialized_ = false;
    };
} // namespace ve
