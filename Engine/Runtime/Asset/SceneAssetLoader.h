#pragma once

#include "Engine/Runtime/Asset/AssetDatabase.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/Scene.h"

namespace ve
{
    [[nodiscard]] ErrorCode LoadSceneAsset(Scene& scene,
                                           const ReflectionRegistry& reflectionRegistry,
                                           ResourceManager& resourceManager,
                                           const AssetDatabase& assetDatabase,
                                           const Path& scenePath);
} // namespace ve
