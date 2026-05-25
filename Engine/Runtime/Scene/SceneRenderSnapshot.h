#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Math/Vector4.h"
#include "Engine/Runtime/Resource/ResourceHandle.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/SceneTypes.h"

#include <vector>

namespace ve
{
    struct SceneRenderCamera
    {
        SceneObjectId objectId = InvalidSceneObjectId;
        Matrix44 viewMatrix = Matrix44::Identity();
        Matrix44 projectionMatrix = Matrix44::Identity();
        Vector4 viewportRect = Vector4(0.0f, 0.0f, 1.0f, 1.0f);
        Vector4 clearColor = Vector4(0.05f, 0.07f, 0.10f, 1.0f);
    };

    struct SceneRenderDirectionalLight
    {
        SceneObjectId objectId = InvalidSceneObjectId;
        Vector3 direction = Vector3(0.0f, -1.0f, 0.0f);
        Vector3 color = Vector3::One();
        Float32 intensity = 1.0f;
    };

    struct SceneRenderDrawItem
    {
        SceneObjectId objectId = InvalidSceneObjectId;
        ResourceHandle<MeshResource> mesh;
        ResourceHandle<MaterialResource> material;
        Matrix44 worldMatrix = Matrix44::Identity();
        Vector3 boundsCenter = Vector3::Zero();
        Vector3 boundsExtents = Vector3(0.5f, 0.5f, 0.5f);
    };

    struct SceneRenderSnapshot
    {
        UInt64 frameId = 0;
        bool hasMainCamera = false;
        SceneRenderCamera mainCamera;
        std::vector<SceneRenderDirectionalLight> directionalLights;
        std::vector<SceneRenderDrawItem> drawItems;
    };
} // namespace ve
