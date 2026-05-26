#pragma once

#include "Engine/Runtime/Asset/AssetGuid.h"
#include "Engine/Runtime/FileSystem/Path.h"

namespace ve
{
    struct AssetReference
    {
        AssetGuid guid;
        Path path;
    };
} // namespace ve
