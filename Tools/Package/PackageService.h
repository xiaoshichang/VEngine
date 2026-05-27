#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>
#include <string_view>
#include <vector>

namespace ve
{
    enum class PackagePlatform
    {
        Windows,
        iOS,
    };

    enum class PackageConfiguration
    {
        Debug,
        Release,
    };

    [[nodiscard]] const char* ToString(PackagePlatform platform) noexcept;
    [[nodiscard]] const char* ToString(PackageConfiguration configuration) noexcept;
    [[nodiscard]] Result<PackagePlatform> ParsePackagePlatform(std::string_view text);
    [[nodiscard]] Result<PackageConfiguration> ParsePackageConfiguration(std::string_view text);

    struct PackageRequest
    {
        Path projectRoot;
        Path outputRoot;
        Path playerExecutable;
        PackagePlatform platform = PackagePlatform::Windows;
        PackageConfiguration configuration = PackageConfiguration::Debug;
        bool includeRuntimeBinaries = true;
    };

    struct PackageResult
    {
        Path packageRoot;
        Path contentRoot;
        Path manifestPath;
        SizeT assetCount = 0;
        SizeT artifactCount = 0;
        std::vector<Path> stagedFiles;
    };

    [[nodiscard]] Result<PackageResult> StagePackage(const PackageRequest& request);
} // namespace ve
