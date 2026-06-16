#pragma once

#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/ResourceManifest.h"
#include "Engine/Runtime/Resource/ResourceSystem.h"

#include <vector>

namespace ve
{
    struct RuntimeResourceLoaderInitParam
    {
        Path projectRoot;
        Path manifestPath;
    };

    class RuntimeResourceLoader : public NonMovable
    {
    public:
        RuntimeResourceLoader() = default;
        ~RuntimeResourceLoader() = default;

        [[nodiscard]] ErrorCode Initialize(const RuntimeResourceLoaderInitParam& desc);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const Path& GetProjectRoot() const noexcept;
        [[nodiscard]] const Path& GetManifestPath() const noexcept;
        [[nodiscard]] const ResourceManifest& GetManifest() const noexcept;

        [[nodiscard]] Result<ResourceRecord> FindResource(const Guid& guid) const;
        [[nodiscard]] Result<ResourceRecord> FindResource(const Path& runtimePath) const;
        [[nodiscard]] Result<std::vector<ResourceRecord>> ResolveLoadOrder(const Guid& guid) const;
        [[nodiscard]] Result<std::vector<ResourceRecord>> ResolveLoadOrder(const Path& runtimePath) const;
        [[nodiscard]] Result<LoadedResourceData> LoadResource(const Guid& guid, ResourceSystem& resourceSystem);
        [[nodiscard]] Result<LoadedResourceData> LoadResource(const Path& runtimePath, ResourceSystem& resourceSystem);
        [[nodiscard]] ResourceLoadOperation LoadResourceAsync(const Guid& guid, ResourceSystem& resourceSystem);
        [[nodiscard]] ResourceLoadOperation LoadResourceAsync(const Path& runtimePath, ResourceSystem& resourceSystem);

    private:
        [[nodiscard]] Result<std::vector<ResourceRecord>> ResolveLoadOrderRecursive(
            const Guid& guid,
            std::vector<ResourceRecord>& loadOrder,
            std::vector<Guid>& visiting,
            std::vector<Guid>& visited) const;

        Path projectRoot_;
        Path manifestPath_;
        ResourceManifest manifest_;
        bool initialized_ = false;
    };
} // namespace ve
