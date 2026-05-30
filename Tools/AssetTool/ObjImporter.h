#pragma once

#include "Engine/Runtime/Asset/AssetDatabase.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>

namespace ve
{
    struct ObjImportResult
    {
        AssetGuid guid;
        Path metadataPath;
        Path meshArtifactPath;
        std::string sourceHash;
        SizeT vertexCount = 0;
    };

    struct ObjImportOptions
    {
        bool force = false;
    };

    [[nodiscard]] Result<ObjImportResult> ImportObjModel(AssetDatabase& assetDatabase,
                                                         const Path& sourcePath,
                                                         ObjImportOptions options);

    [[nodiscard]] inline Result<ObjImportResult> ImportObjModel(AssetDatabase& assetDatabase,
                                                                const Path& sourcePath,
                                                                bool force)
    {
        ObjImportOptions options;
        options.force = force;
        return ImportObjModel(assetDatabase, sourcePath, options);
    }
} // namespace ve
