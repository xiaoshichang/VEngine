#include "Engine/Runtime/Physics/PhysicsWorld.h"

#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

namespace ve
{
    namespace
    {
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
} // namespace ve
