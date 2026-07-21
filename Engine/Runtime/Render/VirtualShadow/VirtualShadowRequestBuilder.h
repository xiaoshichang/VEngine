#pragma once

#include "Engine/Runtime/Math/Bounds.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h"

#include <span>
#include <vector>

namespace ve
{
    struct VirtualShadowReceiver
    {
        UInt64 renderItemID = 0;
        Aabb worldBounds = Aabb(Vector3::Zero(), Vector3::Zero());
        bool receiveShadows = true;
    };

    struct VirtualShadowRequestBuildInput
    {
        Matrix44 viewProjection = Matrix44::Identity();
        Matrix44 cameraLocalToWorld = Matrix44::Identity();
        VirtualShadowClipmapSet clipmaps;
        std::span<const VirtualShadowReceiver> receivers;
    };

    [[nodiscard]] std::vector<VirtualShadowPageRequest> BuildVirtualShadowPageRequests(const VirtualShadowRequestBuildInput& input);
} // namespace ve
