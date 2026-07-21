#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.h"

#include "Engine/Runtime/Math/Frustum.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace ve
{
    namespace
    {
        template<typename Function>
        void VisitCorners(const Aabb& bounds, Function&& function)
        {
            const Vector3& minimum = bounds.GetMinimum();
            const Vector3& maximum = bounds.GetMaximum();
            for (UInt32 cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
            {
                function(Vector3((cornerIndex & 1u) != 0u ? maximum.GetX() : minimum.GetX(),
                                 (cornerIndex & 2u) != 0u ? maximum.GetY() : minimum.GetY(),
                                 (cornerIndex & 4u) != 0u ? maximum.GetZ() : minimum.GetZ()));
            }
        }

        UInt32 BuildRequestPriority(const VirtualShadowRequestBuildInput& input, const VirtualShadowReceiver& receiver, UInt32 levelIndex) noexcept
        {
            Float32 minimumX = std::numeric_limits<Float32>::max();
            Float32 minimumY = std::numeric_limits<Float32>::max();
            Float32 maximumX = std::numeric_limits<Float32>::lowest();
            Float32 maximumY = std::numeric_limits<Float32>::lowest();
            VisitCorners(receiver.worldBounds,
                         [&](const Vector3& corner)
                         {
                             const Vector3 ndc = input.viewProjection.TransformPoint(corner);
                             minimumX = std::min(minimumX, ndc.GetX());
                             minimumY = std::min(minimumY, ndc.GetY());
                             maximumX = std::max(maximumX, ndc.GetX());
                             maximumY = std::max(maximumY, ndc.GetY());
                         });
            const Float32 screenCoverage = std::clamp((maximumX - minimumX) * (maximumY - minimumY) * 0.25f, 0.0f, 1.0f);
            const Vector3 cameraPosition(input.cameraLocalToWorld.Get(0, 3), input.cameraLocalToWorld.Get(1, 3), input.cameraLocalToWorld.Get(2, 3));
            const Float32 distance = (receiver.worldBounds.GetCenter() - cameraPosition).Length();
            const Float32 inverseDistance = 1.0f / (1.0f + distance);
            const UInt32 coverageBits = static_cast<UInt32>(screenCoverage * 16383.0f);
            const UInt32 distanceBits = static_cast<UInt32>(std::clamp(inverseDistance, 0.0f, 1.0f) * 16383.0f);
            return ((3u - levelIndex) << 28u) | (coverageBits << 14u) | distanceBits;
        }
    } // namespace

    std::vector<VirtualShadowPageRequest> BuildVirtualShadowPageRequests(const VirtualShadowRequestBuildInput& input)
    {
        if (!input.clipmaps.valid)
        {
            return {};
        }

        const Frustum frustum = Frustum::FromViewProjection(input.viewProjection);
        std::unordered_map<VirtualShadowPageKey, UInt32, VirtualShadowPageKeyHash> priorities;

        for (const VirtualShadowReceiver& receiver : input.receivers)
        {
            if (!receiver.receiveShadows || !receiver.worldBounds.IsFiniteAndValid() || !frustum.Intersects(receiver.worldBounds))
            {
                continue;
            }

            Float32 minimumLightX = std::numeric_limits<Float32>::max();
            Float32 minimumLightY = std::numeric_limits<Float32>::max();
            Float32 maximumLightX = std::numeric_limits<Float32>::lowest();
            Float32 maximumLightY = std::numeric_limits<Float32>::lowest();
            Float32 minimumCameraDepth = std::numeric_limits<Float32>::max();
            Float32 maximumCameraDepth = std::numeric_limits<Float32>::lowest();
            const Vector3 cameraPosition(input.cameraLocalToWorld.Get(0, 3), input.cameraLocalToWorld.Get(1, 3), input.cameraLocalToWorld.Get(2, 3));
            Vector3 cameraForward = input.cameraLocalToWorld.TransformDirection(Vector3::UnitZ()).Normalized();
            if (cameraForward.LengthSquared() == 0.0f)
            {
                cameraForward = Vector3::UnitZ();
            }
            VisitCorners(receiver.worldBounds,
                         [&](const Vector3& corner)
                         {
                             const Vector3 lightCorner = input.clipmaps.lightBasis.TransformPoint(corner);
                             minimumLightX = std::min(minimumLightX, lightCorner.GetX());
                             minimumLightY = std::min(minimumLightY, lightCorner.GetY());
                             maximumLightX = std::max(maximumLightX, lightCorner.GetX());
                             maximumLightY = std::max(maximumLightY, lightCorner.GetY());
                             const Float32 cameraDepth = Vector3::Dot(corner - cameraPosition, cameraForward);
                             minimumCameraDepth = std::min(minimumCameraDepth, cameraDepth);
                             maximumCameraDepth = std::max(maximumCameraDepth, cameraDepth);
                         });

            for (UInt32 levelIndex = 0; levelIndex < VirtualShadowClipmapLevelCount; ++levelIndex)
            {
                const VirtualShadowClipmapLevel& level = input.clipmaps.levels[levelIndex];
                const Float32 sliceMinimumDepth = levelIndex == 0 ? 0.0f : input.clipmaps.levels[levelIndex - 1u].worldRadius;
                if (maximumCameraDepth < sliceMinimumDepth || minimumCameraDepth > level.worldRadius)
                {
                    continue;
                }
                const Int32 workingMinimumX = level.originPageX - static_cast<Int32>(VirtualShadowPagesPerAxis / 2u);
                const Int32 workingMinimumY = level.originPageY - static_cast<Int32>(VirtualShadowPagesPerAxis / 2u);
                const Int32 workingMaximumX = workingMinimumX + static_cast<Int32>(VirtualShadowPagesPerAxis) - 1;
                const Int32 workingMaximumY = workingMinimumY + static_cast<Int32>(VirtualShadowPagesPerAxis) - 1;
                const Int32 receiverMinimumX = static_cast<Int32>(std::floor(minimumLightX / level.pageWorldSize));
                const Int32 receiverMinimumY = static_cast<Int32>(std::floor(minimumLightY / level.pageWorldSize));
                const Int32 receiverMaximumX = static_cast<Int32>(std::floor(maximumLightX / level.pageWorldSize));
                const Int32 receiverMaximumY = static_cast<Int32>(std::floor(maximumLightY / level.pageWorldSize));

                const Int32 minimumPageX = std::max(receiverMinimumX, workingMinimumX);
                const Int32 minimumPageY = std::max(receiverMinimumY, workingMinimumY);
                const Int32 maximumPageX = std::min(receiverMaximumX, workingMaximumX);
                const Int32 maximumPageY = std::min(receiverMaximumY, workingMaximumY);
                if (minimumPageX > maximumPageX || minimumPageY > maximumPageY)
                {
                    continue;
                }

                const UInt32 priority = BuildRequestPriority(input, receiver, levelIndex);
                for (Int32 pageY = minimumPageY; pageY <= maximumPageY; ++pageY)
                {
                    for (Int32 pageX = minimumPageX; pageX <= maximumPageX; ++pageX)
                    {
                        const VirtualShadowPageKey key = VirtualShadowPageKey::Create(pageX, pageY, levelIndex, level.depthEpoch);
                        if (!key.IsValid())
                        {
                            continue;
                        }
                        auto [iterator, inserted] = priorities.emplace(key, priority);
                        if (!inserted)
                        {
                            iterator->second = std::max(iterator->second, priority);
                        }
                    }
                }
            }
        }

        std::vector<VirtualShadowPageRequest> result;
        result.reserve(priorities.size());
        for (const auto& [key, priority] : priorities)
        {
            result.push_back({key, priority});
        }
        std::ranges::sort(result,
                          [](const VirtualShadowPageRequest& left, const VirtualShadowPageRequest& right)
                          {
                              if (left.priority != right.priority)
                              {
                                  return left.priority > right.priority;
                              }
                              if (left.key.key0 != right.key.key0)
                              {
                                  return left.key.key0 < right.key.key0;
                              }
                              return left.key.key1 < right.key.key1;
                          });
        return result;
    }
} // namespace ve
