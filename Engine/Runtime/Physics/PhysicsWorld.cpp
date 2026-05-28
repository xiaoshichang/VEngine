#include "Engine/Runtime/Physics/PhysicsWorld.h"

#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <algorithm>

namespace ve
{
    namespace
    {
        [[nodiscard]] bool PassesFilter(const ColliderProxy& proxy, UInt64 queryMask, bool includeTriggers) noexcept
        {
            return (proxy.layer & queryMask) != 0 && (includeTriggers || !proxy.isTrigger);
        }

        [[nodiscard]] std::optional<ShapeRaycastHit> RaycastProxy(const Ray& ray,
                                                                  const ColliderProxy& proxy) noexcept
        {
            return proxy.shape == ColliderShape::Sphere ? RaycastSphere(ray, proxy.sphere)
                                                        : RaycastOrientedBox(ray, proxy.box);
        }

        [[nodiscard]] RaycastHit MakeRaycastHit(const ColliderProxy& proxy,
                                                const ShapeRaycastHit& shapeHit) noexcept
        {
            RaycastHit hit;
            hit.gameObjectId = proxy.gameObjectId;
            hit.collider = proxy.collider;
            hit.distance = shapeHit.distance;
            hit.position = shapeHit.position;
            hit.normal = shapeHit.normal;
            hit.layer = proxy.layer;
            hit.isTrigger = proxy.isTrigger;
            return hit;
        }

        void SyncGameObject(const GameObject& gameObject, std::vector<ColliderProxy>& proxies)
        {
            if (!gameObject.IsActiveInHierarchy())
            {
                return;
            }

            const TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
            const ColliderComponent* collider = gameObject.GetComponent<ColliderComponent>();
            if (transform != nullptr && collider != nullptr && collider->IsColliderEnabled())
            {
                ColliderProxy proxy;
                proxy.gameObjectId = gameObject.GetId();
                proxy.collider = collider;
                proxy.shape = collider->GetShape();
                proxy.layer = collider->GetLayer();
                proxy.collidesWith = collider->GetCollidesWith();
                proxy.isTrigger = collider->IsTrigger();
                if (proxy.shape == ColliderShape::Sphere)
                {
                    proxy.sphere = collider->BuildWorldSphere(*transform);
                    proxy.bounds = BuildAabb(proxy.sphere);
                }
                else
                {
                    proxy.box = collider->BuildWorldBox(*transform);
                    proxy.bounds = BuildAabb(proxy.box);
                }
                proxies.push_back(proxy);
            }

            for (const GameObject* child : gameObject.GetChildren())
            {
                SyncGameObject(*child, proxies);
            }
        }
    } // namespace

    void PhysicsWorld::Clear()
    {
        colliderProxies_.clear();
    }

    void PhysicsWorld::SyncFromScene(const Scene& scene)
    {
        colliderProxies_.clear();
        for (const GameObject* root : scene.GetRootGameObjects())
        {
            SyncGameObject(*root, colliderProxies_);
        }
    }

    SizeT PhysicsWorld::GetColliderCount() const noexcept
    {
        return colliderProxies_.size();
    }

    std::optional<RaycastHit>
    PhysicsWorld::RaycastClosest(const Ray& ray, UInt64 queryMask, bool includeTriggers) const
    {
        std::optional<RaycastHit> closest;
        for (const ColliderProxy& proxy : colliderProxies_)
        {
            if (!PassesFilter(proxy, queryMask, includeTriggers))
            {
                continue;
            }

            const std::optional<ShapeRaycastHit> shapeHit = RaycastProxy(ray, proxy);
            if (!shapeHit)
            {
                continue;
            }

            RaycastHit hit = MakeRaycastHit(proxy, *shapeHit);
            if (!closest || hit.distance < closest->distance ||
                (NearlyEqual(hit.distance, closest->distance) && hit.gameObjectId < closest->gameObjectId))
            {
                closest = hit;
            }
        }

        return closest;
    }

    std::vector<RaycastHit>
    PhysicsWorld::RaycastAll(const Ray& ray, UInt64 queryMask, bool includeTriggers) const
    {
        std::vector<RaycastHit> hits;
        for (const ColliderProxy& proxy : colliderProxies_)
        {
            if (!PassesFilter(proxy, queryMask, includeTriggers))
            {
                continue;
            }

            const std::optional<ShapeRaycastHit> shapeHit = RaycastProxy(ray, proxy);
            if (shapeHit)
            {
                hits.push_back(MakeRaycastHit(proxy, *shapeHit));
            }
        }

        std::sort(hits.begin(),
                  hits.end(),
                  [](const RaycastHit& left, const RaycastHit& right)
                  {
                      if (!NearlyEqual(left.distance, right.distance))
                      {
                          return left.distance < right.distance;
                      }
                      return left.gameObjectId < right.gameObjectId;
                  });
        return hits;
    }
} // namespace ve
