#pragma once

#include "Engine/Runtime/Asset/AssetGuid.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Resource/ResourceManager.h"

#include <string>
#include <vector>

namespace ve
{
    struct MeshAssetData
    {
        AssetGuid sourceGuid;
        std::string name;
        std::vector<MeshVertex> vertices;
    };

    struct MaterialAssetData
    {
        AssetGuid guid;
        std::string name;
        Vector3 baseColor = Vector3(0.8f, 0.8f, 0.8f);
    };

    [[nodiscard]] Result<MeshAssetData> LoadMeshAsset(const Path& path);
    [[nodiscard]] ErrorCode SaveMeshAsset(const Path& path, const MeshAssetData& mesh);

    [[nodiscard]] Result<MaterialAssetData> LoadMaterialAsset(const Path& path);
    [[nodiscard]] ErrorCode SaveMaterialAsset(const Path& path, const MaterialAssetData& material);
} // namespace ve
