#include "Engine/Runtime/Physics/PhysicsSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Physics/PhysicsSystemBackendJolt.h"
#include "Engine/Runtime/Scene/ColliderComponent.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/RigidbodyComponent.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <algorithm>
#include <cstdint>
#include <new>
#include <vector>

namespace ve
{
    namespace
    {
        struct PhysicsSystemSceneSyncEntry
        {
            const Scene* scene = nullptr;
            GameObject* gameObject = nullptr;
            PhysicsBodyHandle body;
            PhysicsBodyMotionType motionType = PhysicsBodyMotionType::Static;
            bool hasRigidbody = false;
            bool seenInCurrentSync = false;
            bool hasLastSceneBodyTransform = false;
            PhysicsBodyTransform lastSceneBodyTransform;
        };

        [[nodiscard]] bool AreSameTransform(const PhysicsBodyTransform& left, const PhysicsBodyTransform& right) noexcept
        {
            return left.position.IsNearlyEqual(right.position) && left.rotation.IsNearlyEqual(right.rotation);
        }

        [[nodiscard]] PhysicsShapeDesc BuildPhysicsShapeDesc(const ColliderDesc& colliderDesc)
        {
            switch (colliderDesc.shapeType)
            {
            case ColliderShapeType::Box:
                return PhysicsShapeDesc::CreateBox(colliderDesc.size * 0.5f);
            case ColliderShapeType::Sphere:
                return PhysicsShapeDesc::CreateSphere(colliderDesc.radius);
            case ColliderShapeType::Capsule:
            {
                const Float32 halfHeight = std::max((colliderDesc.height * 0.5f) - colliderDesc.radius, Math::DefaultEpsilon);
                return PhysicsShapeDesc::CreateCapsule(colliderDesc.radius, halfHeight);
            }
            }

            return PhysicsShapeDesc::CreateSphere(0.0f);
        }

        [[nodiscard]] PhysicsBodyMotionType BuildPhysicsMotionType(const RigidbodyComponent* rigidbody) noexcept
        {
            if (rigidbody == nullptr)
            {
                return PhysicsBodyMotionType::Static;
            }

            return rigidbody->IsKinematic() ? PhysicsBodyMotionType::Kinematic : PhysicsBodyMotionType::Dynamic;
        }

        [[nodiscard]] PhysicsBodyTransform BuildBodyTransform(const TransformComponent& transform, const ColliderDesc& colliderDesc) noexcept
        {
            PhysicsBodyTransform bodyTransform;
            bodyTransform.rotation = transform.GetLocalRotation();
            bodyTransform.position = transform.GetLocalPosition() + bodyTransform.rotation.RotateVector(colliderDesc.center);
            return bodyTransform;
        }

        void ApplyBodyTransform(TransformComponent& transform, const ColliderDesc& colliderDesc, const PhysicsBodyTransform& bodyTransform) noexcept
        {
            transform.SetLocalRotation(bodyTransform.rotation);
            transform.SetLocalPosition(bodyTransform.position - bodyTransform.rotation.RotateVector(colliderDesc.center));
        }

        [[nodiscard]] PhysicsBodyDesc BuildPhysicsBodyDesc(GameObject& gameObject,
                                                           const TransformComponent& transform,
                                                           const ColliderComponent& collider,
                                                           const RigidbodyComponent* rigidbody) noexcept
        {
            const ColliderDesc& colliderDesc = collider.GetDesc();
            PhysicsBodyDesc bodyDesc;
            bodyDesc.shape = BuildPhysicsShapeDesc(colliderDesc);
            bodyDesc.motionType = BuildPhysicsMotionType(rigidbody);
            bodyDesc.transform = BuildBodyTransform(transform, colliderDesc);
            bodyDesc.friction = colliderDesc.dynamicFriction;
            bodyDesc.restitution = colliderDesc.bounciness;
            bodyDesc.trigger = colliderDesc.trigger;
            bodyDesc.userData = static_cast<UInt64>(reinterpret_cast<std::uintptr_t>(&gameObject));

            if (rigidbody != nullptr)
            {
                const RigidbodyDesc& rigidbodyDesc = rigidbody->GetDesc();
                bodyDesc.mass = rigidbodyDesc.mass;
                bodyDesc.linearDamping = rigidbodyDesc.linearDamping;
                bodyDesc.angularDamping = rigidbodyDesc.angularDamping;
                bodyDesc.gravityFactor = rigidbodyDesc.useGravity ? 1.0f : 0.0f;
            }

            return bodyDesc;
        }

        [[nodiscard]] RigidbodyComponent* GetEnabledRigidbody(GameObject& gameObject) noexcept
        {
            RigidbodyComponent* rigidbody = gameObject.GetComponent<RigidbodyComponent>();
            return rigidbody != nullptr && rigidbody->IsEnabled() && rigidbody->DetectsCollisions() ? rigidbody : nullptr;
        }

        [[nodiscard]] ColliderComponent* GetEnabledCollider(GameObject& gameObject) noexcept
        {
            ColliderComponent* collider = gameObject.GetComponent<ColliderComponent>();
            RigidbodyComponent* rigidbody = gameObject.GetComponent<RigidbodyComponent>();
            if (rigidbody != nullptr && rigidbody->IsEnabled() && !rigidbody->DetectsCollisions())
            {
                return nullptr;
            }

            return collider != nullptr && collider->IsEnabled() ? collider : nullptr;
        }

        [[nodiscard]] PhysicsSystemSceneSyncEntry* FindSceneSyncEntry(std::vector<PhysicsSystemSceneSyncEntry>& entries, const GameObject& gameObject) noexcept
        {
            const auto entryIt = std::find_if(
                entries.begin(), entries.end(), [&gameObject](const PhysicsSystemSceneSyncEntry& entry) { return entry.gameObject == &gameObject; });
            return entryIt != entries.end() ? &(*entryIt) : nullptr;
        }

        void MarkSceneEntriesUnseen(std::vector<PhysicsSystemSceneSyncEntry>& entries, const Scene& scene) noexcept
        {
            for (PhysicsSystemSceneSyncEntry& entry : entries)
            {
                if (entry.scene == &scene)
                {
                    entry.seenInCurrentSync = false;
                }
            }
        }

        void ClearRigidbodyHandleRecursive(GameObject& gameObject) noexcept
        {
            if (RigidbodyComponent* rigidbody = gameObject.GetComponent<RigidbodyComponent>(); rigidbody != nullptr)
            {
                rigidbody->ClearPhysicsBodyHandle();
            }

            TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
            if (transform == nullptr)
            {
                return;
            }

            for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
            {
                GameObject* child = transform->GetChildGameObject(childIndex);
                if (child != nullptr)
                {
                    ClearRigidbodyHandleRecursive(*child);
                }
            }
        }
    } // namespace

    struct PhysicsSystemSceneSyncState
    {
        std::vector<PhysicsSystemSceneSyncEntry> entries;
    };

    namespace
    {
        void ClearAllSceneSyncHandles(PhysicsSystemSceneSyncState& syncState) noexcept
        {
            for (PhysicsSystemSceneSyncEntry& entry : syncState.entries)
            {
                if (entry.gameObject != nullptr)
                {
                    ClearRigidbodyHandleRecursive(*entry.gameObject);
                }
            }

            syncState.entries.clear();
        }

        [[nodiscard]] ErrorCode
        SyncGameObjectBeforeStep(PhysicsSystem& physicsSystem, PhysicsSystemSceneSyncState& syncState, Scene& scene, GameObject& gameObject)
        {
            ColliderComponent* collider = GetEnabledCollider(gameObject);
            if (collider != nullptr)
            {
                TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
                if (transform == nullptr)
                {
                    return ErrorCode::InvalidState;
                }

                if (collider->GetShapeType() == ColliderShapeType::Capsule && collider->GetDirection() != ColliderDirectionAxis::Y)
                {
                    return ErrorCode::Unsupported;
                }

                RigidbodyComponent* rigidbody = GetEnabledRigidbody(gameObject);
                PhysicsBodyDesc bodyDesc = BuildPhysicsBodyDesc(gameObject, *transform, *collider, rigidbody);
                const bool hasRigidbody = rigidbody != nullptr;
                PhysicsSystemSceneSyncEntry* entry = FindSceneSyncEntry(syncState.entries, gameObject);
                if (entry == nullptr)
                {
                    try
                    {
                        syncState.entries.push_back(PhysicsSystemSceneSyncEntry{
                            &scene,
                            &gameObject,
                        });
                    }
                    catch (const std::bad_alloc&)
                    {
                        return ErrorCode::OutOfMemory;
                    }

                    entry = &syncState.entries.back();
                }

                entry->seenInCurrentSync = true;
                const bool shouldRecreateBody = !entry->body.IsValid() || entry->hasRigidbody != hasRigidbody || entry->motionType != bodyDesc.motionType ||
                                                collider->GetBackend().IsRuntimeShapeDirty() ||
                                                (rigidbody != nullptr && rigidbody->GetBackend().IsRuntimeBodyDirty());
                if (shouldRecreateBody)
                {
                    if (entry->body.IsValid())
                    {
                        Result<Vector3> linearVelocity = physicsSystem.GetBodyLinearVelocity(entry->body);
                        if (linearVelocity)
                        {
                            bodyDesc.linearVelocity = linearVelocity.GetValue();
                        }
                        else if (linearVelocity.GetError().GetCode() != ErrorCode::NotFound)
                        {
                            return linearVelocity.GetError().GetCode();
                        }

                        Result<Vector3> angularVelocity = physicsSystem.GetBodyAngularVelocity(entry->body);
                        if (angularVelocity)
                        {
                            bodyDesc.angularVelocity = angularVelocity.GetValue();
                        }
                        else if (angularVelocity.GetError().GetCode() != ErrorCode::NotFound)
                        {
                            return angularVelocity.GetError().GetCode();
                        }

                        const ErrorCode destroyResult = physicsSystem.DestroyBody(entry->body);
                        if (destroyResult != ErrorCode::None && destroyResult != ErrorCode::NotFound)
                        {
                            return destroyResult;
                        }
                        entry->body.Reset();
                    }

                    Result<PhysicsBodyHandle> body = physicsSystem.CreateBody(bodyDesc);
                    if (!body)
                    {
                        return body.GetError().GetCode();
                    }

                    entry->body = body.GetValue();
                    entry->motionType = bodyDesc.motionType;
                    entry->hasRigidbody = hasRigidbody;
                    entry->lastSceneBodyTransform = bodyDesc.transform;
                    entry->hasLastSceneBodyTransform = true;
                    collider->GetBackend().ClearRuntimeShapeDirty();
                    if (rigidbody != nullptr)
                    {
                        rigidbody->SetPhysicsBodyHandle(entry->body);
                        rigidbody->GetBackend().ClearRuntimeBodyDirty();
                    }
                }
                else
                {
                    if (!entry->hasLastSceneBodyTransform || !AreSameTransform(entry->lastSceneBodyTransform, bodyDesc.transform))
                    {
                        const ErrorCode transformResult = physicsSystem.SetBodyTransform(entry->body, bodyDesc.transform);
                        if (transformResult != ErrorCode::None)
                        {
                            return transformResult;
                        }

                        entry->lastSceneBodyTransform = bodyDesc.transform;
                        entry->hasLastSceneBodyTransform = true;
                    }

                    if (rigidbody != nullptr)
                    {
                        if (rigidbody->GetPhysicsBodyHandle() != entry->body)
                        {
                            rigidbody->SetPhysicsBodyHandle(entry->body);
                        }
                    }
                }
            }

            TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
            if (transform == nullptr)
            {
                return ErrorCode::None;
            }

            for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
            {
                GameObject* child = transform->GetChildGameObject(childIndex);
                if (child == nullptr)
                {
                    continue;
                }

                const ErrorCode childResult = SyncGameObjectBeforeStep(physicsSystem, syncState, scene, *child);
                if (childResult != ErrorCode::None)
                {
                    return childResult;
                }
            }

            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode DestroyUnseenSceneEntries(PhysicsSystem& physicsSystem, PhysicsSystemSceneSyncState& syncState, const Scene& scene)
        {
            for (auto entryIt = syncState.entries.begin(); entryIt != syncState.entries.end();)
            {
                PhysicsSystemSceneSyncEntry& entry = *entryIt;
                if (entry.scene != &scene || entry.seenInCurrentSync)
                {
                    ++entryIt;
                    continue;
                }

                if (entry.body.IsValid())
                {
                    const ErrorCode destroyResult = physicsSystem.DestroyBody(entry.body);
                    if (destroyResult != ErrorCode::None && destroyResult != ErrorCode::NotFound)
                    {
                        return destroyResult;
                    }
                }

                entryIt = syncState.entries.erase(entryIt);
            }

            return ErrorCode::None;
        }
    } // namespace

    PhysicsSystem::PhysicsSystem()
        : sceneSyncState_(std::make_unique<PhysicsSystemSceneSyncState>())
    {
    }

    PhysicsSystem::~PhysicsSystem()
    {
        Shutdown();
    }

    ErrorCode PhysicsSystem::Initialize(const PhysicsSystemInitParam& initParam, JobSystem& jobSystem)
    {
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        backend_ = CreateBackend(initParam.backendType);
        if (backend_ == nullptr)
        {
            return ErrorCode::Unsupported;
        }

        const ErrorCode result = backend_->Initialize(initParam, jobSystem);
        if (result != ErrorCode::None)
        {
            backend_.reset();
            return result;
        }

        initialized_ = true;
        initParam_ = initParam;
        jobSystem_ = &jobSystem;
        return ErrorCode::None;
    }

    void PhysicsSystem::Shutdown() noexcept
    {
        if (backend_ != nullptr)
        {
            backend_->Shutdown();
            backend_.reset();
        }

        if (sceneSyncState_ != nullptr)
        {
            sceneSyncState_->entries.clear();
        }

        jobSystem_ = nullptr;
        initialized_ = false;
    }

    ErrorCode PhysicsSystem::ResetSimulation()
    {
        if (!initialized_)
        {
            return ErrorCode::InvalidState;
        }

        if (sceneSyncState_ != nullptr)
        {
            ClearAllSceneSyncHandles(*sceneSyncState_);
        }

        VE_ASSERT_MESSAGE(jobSystem_ != nullptr, "PhysicsSystem::ResetSimulation requires the JobSystem used for initialization.");

        if (backend_ != nullptr)
        {
            backend_->Shutdown();
            backend_.reset();
        }

        backend_ = CreateBackend(initParam_.backendType);
        if (backend_ == nullptr)
        {
            initialized_ = false;
            return ErrorCode::Unsupported;
        }

        const ErrorCode result = backend_->Initialize(initParam_, *jobSystem_);
        if (result != ErrorCode::None)
        {
            backend_.reset();
            initialized_ = false;
            return result;
        }

        return ErrorCode::None;
    }

    bool PhysicsSystem::IsInitialized() const noexcept
    {
        return initialized_ && backend_ != nullptr && backend_->IsInitialized();
    }

    PhysicsBackendType PhysicsSystem::GetBackendType() const noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::GetBackendType requires an initialized physics system.");
        return backend_->GetBackendType();
    }

    ErrorCode PhysicsSystem::StepSimulation(const PhysicsStepDesc& desc)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::StepSimulation requires an initialized physics system.");
        return backend_->StepSimulation(desc);
    }

    Result<PhysicsBodyHandle> PhysicsSystem::CreateBody(const PhysicsBodyDesc& desc)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::CreateBody requires an initialized physics system.");
        return backend_->CreateBody(desc);
    }

    ErrorCode PhysicsSystem::DestroyBody(PhysicsBodyHandle body)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::DestroyBody requires an initialized physics system.");
        return backend_->DestroyBody(body);
    }

    Result<PhysicsBodyTransform> PhysicsSystem::GetBodyTransform(PhysicsBodyHandle body) const
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::GetBodyTransform requires an initialized physics system.");
        return backend_->GetBodyTransform(body);
    }

    ErrorCode PhysicsSystem::SetBodyTransform(PhysicsBodyHandle body, const PhysicsBodyTransform& transform)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::SetBodyTransform requires an initialized physics system.");
        return backend_->SetBodyTransform(body, transform);
    }

    Result<Vector3> PhysicsSystem::GetBodyLinearVelocity(PhysicsBodyHandle body) const
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::GetBodyLinearVelocity requires an initialized physics system.");
        return backend_->GetBodyLinearVelocity(body);
    }

    ErrorCode PhysicsSystem::SetBodyLinearVelocity(PhysicsBodyHandle body, Vector3 velocity)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::SetBodyLinearVelocity requires an initialized physics system.");
        return backend_->SetBodyLinearVelocity(body, velocity);
    }

    Result<Vector3> PhysicsSystem::GetBodyAngularVelocity(PhysicsBodyHandle body) const
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::GetBodyAngularVelocity requires an initialized physics system.");
        return backend_->GetBodyAngularVelocity(body);
    }

    ErrorCode PhysicsSystem::SetBodyAngularVelocity(PhysicsBodyHandle body, Vector3 velocity)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::SetBodyAngularVelocity requires an initialized physics system.");
        return backend_->SetBodyAngularVelocity(body, velocity);
    }

    ErrorCode PhysicsSystem::SyncSceneBeforeStep(Scene& scene)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::SyncSceneBeforeStep requires an initialized physics system.");
        VE_ASSERT_MESSAGE(sceneSyncState_ != nullptr, "PhysicsSystem scene sync state should be available.");

        MarkSceneEntriesUnseen(sceneSyncState_->entries, scene);

        const SizeT rootCount = scene.GetRootGameObjectCount();
        for (SizeT rootIndex = 0; rootIndex < rootCount; ++rootIndex)
        {
            GameObject* root = scene.GetRootGameObject(rootIndex);
            if (root == nullptr)
            {
                continue;
            }

            const ErrorCode syncResult = SyncGameObjectBeforeStep(*this, *sceneSyncState_, scene, *root);
            if (syncResult != ErrorCode::None)
            {
                return syncResult;
            }
        }

        return DestroyUnseenSceneEntries(*this, *sceneSyncState_, scene);
    }

    ErrorCode PhysicsSystem::WriteBackSceneAfterStep(Scene& scene)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::WriteBackSceneAfterStep requires an initialized physics system.");
        VE_ASSERT_MESSAGE(sceneSyncState_ != nullptr, "PhysicsSystem scene sync state should be available.");

        const auto writeBackRecursive = [this](GameObject& gameObject, auto&& self) -> ErrorCode
        {
            PhysicsSystemSceneSyncEntry* entry = FindSceneSyncEntry(sceneSyncState_->entries, gameObject);
            ColliderComponent* collider = GetEnabledCollider(gameObject);
            RigidbodyComponent* rigidbody = GetEnabledRigidbody(gameObject);
            if (entry != nullptr && entry->body.IsValid() && collider != nullptr && rigidbody != nullptr && !rigidbody->IsKinematic())
            {
                Result<PhysicsBodyTransform> bodyTransform = GetBodyTransform(entry->body);
                if (!bodyTransform)
                {
                    return bodyTransform.GetError().GetCode();
                }

                TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
                if (transform == nullptr)
                {
                    return ErrorCode::InvalidState;
                }

                ApplyBodyTransform(*transform, collider->GetDesc(), bodyTransform.GetValue());
                entry->lastSceneBodyTransform = BuildBodyTransform(*transform, collider->GetDesc());
                entry->hasLastSceneBodyTransform = true;
            }

            TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
            if (transform == nullptr)
            {
                return ErrorCode::None;
            }

            for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
            {
                GameObject* child = transform->GetChildGameObject(childIndex);
                if (child == nullptr)
                {
                    continue;
                }

                const ErrorCode childResult = self(*child, self);
                if (childResult != ErrorCode::None)
                {
                    return childResult;
                }
            }

            return ErrorCode::None;
        };

        const SizeT rootCount = scene.GetRootGameObjectCount();
        for (SizeT rootIndex = 0; rootIndex < rootCount; ++rootIndex)
        {
            GameObject* root = scene.GetRootGameObject(rootIndex);
            if (root == nullptr)
            {
                continue;
            }

            const ErrorCode result = writeBackRecursive(*root, writeBackRecursive);
            if (result != ErrorCode::None)
            {
                return result;
            }
        }

        return ErrorCode::None;
    }

    void PhysicsSystem::ClearSceneSyncState(Scene& scene) noexcept
    {
        if (!IsInitialized() || sceneSyncState_ == nullptr)
        {
            return;
        }

        const SizeT rootCount = scene.GetRootGameObjectCount();
        for (SizeT rootIndex = 0; rootIndex < rootCount; ++rootIndex)
        {
            GameObject* root = scene.GetRootGameObject(rootIndex);
            if (root != nullptr)
            {
                ClearRigidbodyHandleRecursive(*root);
            }
        }

        for (auto entryIt = sceneSyncState_->entries.begin(); entryIt != sceneSyncState_->entries.end();)
        {
            PhysicsSystemSceneSyncEntry& entry = *entryIt;
            if (entry.scene != &scene)
            {
                ++entryIt;
                continue;
            }

            if (entry.body.IsValid())
            {
                const ErrorCode destroyResult = DestroyBody(entry.body);
                VE_ASSERT_MESSAGE(destroyResult == ErrorCode::None || destroyResult == ErrorCode::NotFound,
                                  "PhysicsSystem::ClearSceneSyncState failed to destroy a tracked physics body.");
            }

            entryIt = sceneSyncState_->entries.erase(entryIt);
        }
    }

    std::unique_ptr<PhysicsSystemBackend> PhysicsSystem::CreateBackend(PhysicsBackendType backendType)
    {
        PhysicsBackendType resolvedBackend = backendType;
        if (resolvedBackend == PhysicsBackendType::Auto)
        {
            resolvedBackend = PhysicsBackendType::Jolt;
        }

        switch (resolvedBackend)
        {
        case PhysicsBackendType::Jolt:
            return std::make_unique<PhysicsSystemBackendJolt>();
        case PhysicsBackendType::Auto:
            return nullptr;
        }

        return nullptr;
    }
} // namespace ve
