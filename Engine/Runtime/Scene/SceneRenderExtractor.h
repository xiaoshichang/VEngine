#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneRenderSnapshot.h"

namespace ve
{
    [[nodiscard]] SceneRenderSnapshot ExtractSceneRenderSnapshot(Scene& scene,
                                                                 const ResourceManager& resourceManager,
                                                                 UInt64 frameId,
                                                                 Float32 aspectRatio = 16.0f / 9.0f);
} // namespace ve
