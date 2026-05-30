#include "Engine/Runtime/Physics/PhysicsSystem.h"

#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Physics/ColliderComponent.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
    constexpr ve::Float32 GravityMetersPerSecondSquared = -9.81f;
    constexpr ve::Float32 Restitution = 0.05f;
    constexpr ve::Float32 FrictionDamping = 0.75f;
    constexpr ve::Float32 PositionalCorrectionPercent = 0.8f;
    constexpr ve::Float32 PenetrationSlop = 0.001f;

    [[nodiscard]] ve::Float32 Abs(ve::Float32 value) noexcept
    {
        return std::abs(value);
    }

    [[nodiscard]] ve::Float32 ComponentAt(const ve::Vector3& vector, ve::SizeT index) noexcept
    {
        switch (index)
        {
        case 0:
            return vector.GetX();
        case 1:
            return vector.GetY();
        default:
            return vector.GetZ();
        }
    }

    void SetComponent(ve::Vector3& vector, ve::SizeT index, ve::Float32 value) noexcept
    {
        switch (index)
        {
        case 0:
            vector.SetX(value);
            break;
        case 1:
            vector.SetY(value);
            break;
        default:
            vector.SetZ(value);
            break;
        }
    }

    [[nodiscard]] ve::Float32 SafeInverse(ve::Float32 value) noexcept
    {
        return value > ve::Math::DefaultEpsilon ? 1.0f / value : 0.0f;
    }

    [[nodiscard]] ve::Float32 Clamp01(ve::Float32 value) noexcept
    {
        return ve::Clamp(value, 0.0f, 1.0f);
    }

    [[nodiscard]] ve::Vector3 ApplyDiagonalInertia(const ve::Vector3& inverseInertia,
                                                   const ve::Vector3& value) noexcept
    {
        return ve::Vector3(value.GetX() * inverseInertia.GetX(),
                           value.GetY() * inverseInertia.GetY(),
                           value.GetZ() * inverseInertia.GetZ());
    }

    [[nodiscard]] ve::Quaternion IntegrateRotation(const ve::Quaternion& rotation,
                                                   const ve::Vector3& angularVelocity,
                                                   ve::Float32 fixedDeltaSeconds) noexcept
    {
        const ve::Float32 angle = angularVelocity.Length() * fixedDeltaSeconds;
        if (angle <= ve::Math::DefaultEpsilon)
        {
            return rotation.Normalized();
        }

        return (ve::Quaternion::FromAxisAngle(angularVelocity, angle) * rotation).Normalized();
    }

    [[nodiscard]] ve::Vector3 LerpVector(const ve::Vector3& left,
                                         const ve::Vector3& right,
                                         ve::Float32 alpha) noexcept
    {
        return left + ((right - left) * alpha);
    }

    [[nodiscard]] ve::Quaternion Slerp(const ve::Quaternion& left,
                                       const ve::Quaternion& right,
                                       ve::Float32 alpha) noexcept
    {
        ve::Float32 dot = (left.GetX() * right.GetX()) + (left.GetY() * right.GetY()) +
                          (left.GetZ() * right.GetZ()) + (left.GetW() * right.GetW());
        ve::Quaternion target = right;
        if (dot < 0.0f)
        {
            target = ve::Quaternion(-right.GetX(), -right.GetY(), -right.GetZ(), -right.GetW());
            dot = -dot;
        }

        if (dot > 0.9995f)
        {
            return ve::Quaternion(left.GetX() + ((target.GetX() - left.GetX()) * alpha),
                                  left.GetY() + ((target.GetY() - left.GetY()) * alpha),
                                  left.GetZ() + ((target.GetZ() - left.GetZ()) * alpha),
                                  left.GetW() + ((target.GetW() - left.GetW()) * alpha))
                .Normalized();
        }

        const ve::Float32 theta = std::acos(ve::Clamp(dot, -1.0f, 1.0f));
        const ve::Float32 sinTheta = std::sin(theta);
        if (Abs(sinTheta) <= ve::Math::DefaultEpsilon)
        {
            return left;
        }

        const ve::Float32 leftWeight = std::sin((1.0f - alpha) * theta) / sinTheta;
        const ve::Float32 rightWeight = std::sin(alpha * theta) / sinTheta;
        return ve::Quaternion((left.GetX() * leftWeight) + (target.GetX() * rightWeight),
                              (left.GetY() * leftWeight) + (target.GetY() * rightWeight),
                              (left.GetZ() * leftWeight) + (target.GetZ() * rightWeight),
                              (left.GetW() * leftWeight) + (target.GetW() * rightWeight))
            .Normalized();
    }

    [[nodiscard]] ve::Vector3 ClosestPointOnBox(const ve::Vector3& point, const ve::OrientedBox& box) noexcept
    {
        const ve::Vector3 offset = point - box.center;
        ve::Vector3 closest = box.center;
        for (ve::SizeT index = 0; index < box.axes.size(); ++index)
        {
            const ve::Float32 halfExtent = Abs(ComponentAt(box.halfExtents, index));
            const ve::Float32 distance = ve::Vector3::Dot(offset, box.axes[index]);
            closest += box.axes[index] * ve::Clamp(distance, -halfExtent, halfExtent);
        }
        return closest;
    }

    [[nodiscard]] ve::Float32 ProjectRadius(const ve::OrientedBox& box, const ve::Vector3& axis) noexcept
    {
        ve::Float32 radius = 0.0f;
        for (ve::SizeT index = 0; index < box.axes.size(); ++index)
        {
            radius += Abs(ComponentAt(box.halfExtents, index)) * Abs(ve::Vector3::Dot(box.axes[index], axis));
        }
        return radius;
    }

    [[nodiscard]] ve::Vector3 SupportPoint(const ve::OrientedBox& box, const ve::Vector3& direction) noexcept
    {
        ve::Vector3 point = box.center;
        for (ve::SizeT index = 0; index < box.axes.size(); ++index)
        {
            const ve::Float32 sign = ve::Vector3::Dot(box.axes[index], direction) >= 0.0f ? 1.0f : -1.0f;
            point += box.axes[index] * ComponentAt(box.halfExtents, index) * sign;
        }
        return point;
    }

    [[nodiscard]] bool AabbOverlap(const ve::Aabb& left,
                                   const ve::Aabb& right,
                                   ve::Vector3& outNormal,
                                   ve::Float32& outPenetration) noexcept
    {
        const ve::Vector3 leftCenter = (left.minimum + left.maximum) * 0.5f;
        const ve::Vector3 rightCenter = (right.minimum + right.maximum) * 0.5f;
        const ve::Float32 overlaps[3] = {
            std::min(left.maximum.GetX(), right.maximum.GetX()) - std::max(left.minimum.GetX(), right.minimum.GetX()),
            std::min(left.maximum.GetY(), right.maximum.GetY()) - std::max(left.minimum.GetY(), right.minimum.GetY()),
            std::min(left.maximum.GetZ(), right.maximum.GetZ()) - std::max(left.minimum.GetZ(), right.minimum.GetZ()),
        };

        if (overlaps[0] < 0.0f || overlaps[1] < 0.0f || overlaps[2] < 0.0f)
        {
            return false;
        }

        ve::SizeT axis = 0;
        if (overlaps[1] < overlaps[axis])
        {
            axis = 1;
        }
        if (overlaps[2] < overlaps[axis])
        {
            axis = 2;
        }

        outPenetration = overlaps[axis];
        outNormal = ve::Vector3::Zero();
        SetComponent(outNormal, axis, ComponentAt(leftCenter - rightCenter, axis) >= 0.0f ? 1.0f : -1.0f);
        return true;
    }

    struct Contact
    {
        ve::UInt32 left = 0;
        ve::UInt32 right = 0;
        ve::Vector3 normal = ve::Vector3::UnitY();
        ve::Vector3 point = ve::Vector3::Zero();
        ve::Float32 penetration = 0.0f;
        bool isTrigger = false;
    };
} // namespace

namespace ve
{
    struct PhysicsSystemInternalsAccess
    {
        using BodyProxy = PhysicsSystem::BodyProxy;
    };

    namespace
    {
        using BodyProxy = PhysicsSystemInternalsAccess::BodyProxy;

        [[nodiscard]] PhysicsPose MakePose(const TransformComponent& transform) noexcept
        {
            PhysicsPose pose;
            pose.position = transform.GetLocalPosition();
            pose.rotation = transform.GetLocalRotation();
            pose.scale = transform.GetLocalScale();
            return pose;
        }

        void BuildShape(BodyProxy& proxy)
        {
            if (proxy.collider == nullptr || proxy.transform == nullptr)
            {
                return;
            }

            proxy.shape = proxy.collider->GetShape();
            if (proxy.shape == ColliderShape::Sphere)
            {
                proxy.sphere = proxy.collider->BuildWorldSphere(*proxy.transform);
            }
            else
            {
                proxy.box = proxy.collider->BuildWorldBox(*proxy.transform);
            }
        }

        [[nodiscard]] Vector3 CalculateInverseInertia(const BodyProxy& proxy)
        {
            if (proxy.rigidBody == nullptr || proxy.inverseMass <= 0.0f)
            {
                return Vector3::Zero();
            }

            const Float32 mass = proxy.rigidBody->GetMass();
            if (proxy.collider == nullptr || proxy.shape == ColliderShape::Sphere)
            {
                const Float32 radius = proxy.shape == ColliderShape::Sphere ? std::max(proxy.sphere.radius, 0.001f)
                                                                            : 0.5f;
                const Float32 inertia = 0.4f * mass * radius * radius;
                const Float32 inverse = SafeInverse(inertia);
                return Vector3(inverse, inverse, inverse);
            }

            const Vector3 size = proxy.box.halfExtents * 2.0f;
            const Float32 width = std::max(Abs(size.GetX()), 0.001f);
            const Float32 height = std::max(Abs(size.GetY()), 0.001f);
            const Float32 depth = std::max(Abs(size.GetZ()), 0.001f);
            return Vector3(SafeInverse((mass * ((height * height) + (depth * depth))) / 12.0f),
                           SafeInverse((mass * ((width * width) + (depth * depth))) / 12.0f),
                           SafeInverse((mass * ((width * width) + (height * height))) / 12.0f));
        }

        [[nodiscard]] bool CollisionEnabledBetween(const BodyProxy& left, const BodyProxy& right) noexcept
        {
            if (left.collider == nullptr || right.collider == nullptr)
            {
                return false;
            }

            return (left.collider->GetCollidesWith() & right.collider->GetLayer()) != 0 &&
                   (right.collider->GetCollidesWith() & left.collider->GetLayer()) != 0;
        }

        [[nodiscard]] bool BuildContact(UInt32 leftIndex,
                                        UInt32 rightIndex,
                                        const BodyProxy& left,
                                        const BodyProxy& right,
                                        Contact& outContact) noexcept
        {
            if (!CollisionEnabledBetween(left, right))
            {
                return false;
            }

            Vector3 normal = Vector3::UnitY();
            Vector3 point = (left.pose.position + right.pose.position) * 0.5f;
            Float32 penetration = 0.0f;
            bool touching = false;

            if (left.shape == ColliderShape::Sphere && right.shape == ColliderShape::Sphere)
            {
                const Vector3 delta = left.sphere.center - right.sphere.center;
                const Float32 distance = delta.Length();
                const Float32 radiusSum = left.sphere.radius + right.sphere.radius;
                touching = distance <= radiusSum;
                if (touching)
                {
                    normal = distance > Math::DefaultEpsilon ? delta / distance : Vector3::UnitY();
                    penetration = radiusSum - distance;
                    point = right.sphere.center + (normal * right.sphere.radius);
                }
            }
            else if (left.shape == ColliderShape::Sphere && right.shape == ColliderShape::Box)
            {
                const Vector3 closest = ClosestPointOnBox(left.sphere.center, right.box);
                const Vector3 delta = left.sphere.center - closest;
                const Float32 distance = delta.Length();
                touching = distance <= left.sphere.radius;
                if (touching)
                {
                    normal = distance > Math::DefaultEpsilon ? delta / distance
                                                             : (left.sphere.center - right.box.center).Normalized();
                    if (normal == Vector3::Zero())
                    {
                        normal = Vector3::UnitY();
                    }
                    penetration = left.sphere.radius - distance;
                    point = closest;
                }
            }
            else if (left.shape == ColliderShape::Box && right.shape == ColliderShape::Sphere)
            {
                const Vector3 closest = ClosestPointOnBox(right.sphere.center, left.box);
                const Vector3 delta = closest - right.sphere.center;
                const Float32 distance = delta.Length();
                touching = distance <= right.sphere.radius;
                if (touching)
                {
                    normal = distance > Math::DefaultEpsilon ? delta / distance
                                                             : (left.box.center - right.sphere.center).Normalized();
                    if (normal == Vector3::Zero())
                    {
                        normal = Vector3::UnitY();
                    }
                    penetration = right.sphere.radius - distance;
                    point = closest;
                }
            }
            else
            {
                if (!Overlaps(left.box, right.box))
                {
                    return false;
                }

                touching = AabbOverlap(BuildAabb(left.box), BuildAabb(right.box), normal, penetration);
                point = (SupportPoint(left.box, -normal) + SupportPoint(right.box, normal)) * 0.5f;
            }

            if (!touching || penetration <= Math::DefaultEpsilon)
            {
                return false;
            }

            outContact.left = leftIndex;
            outContact.right = rightIndex;
            outContact.normal = normal.Normalized();
            outContact.point = point;
            outContact.penetration = penetration;
            outContact.isTrigger = left.collider->IsTrigger() || right.collider->IsTrigger();
            return true;
        }

        void ApplyPositionalCorrection(BodyProxy& left, BodyProxy& right, const Contact& contact)
        {
            const Float32 totalInverseMass = left.inverseMass + right.inverseMass;
            if (totalInverseMass <= Math::DefaultEpsilon)
            {
                return;
            }

            const Float32 correctionMagnitude =
                std::max(contact.penetration - PenetrationSlop, 0.0f) * PositionalCorrectionPercent / totalInverseMass;
            const Vector3 correction = contact.normal * correctionMagnitude;

            if (left.inverseMass > 0.0f)
            {
                left.pose.position += correction * left.inverseMass;
            }
            if (right.inverseMass > 0.0f)
            {
                right.pose.position -= correction * right.inverseMass;
            }
        }

        void ApplyImpulse(BodyProxy& left, BodyProxy& right, const Contact& contact)
        {
            RigidBodyComponent* leftRigidBody = left.rigidBody;
            RigidBodyComponent* rightRigidBody = right.rigidBody;
            const Float32 totalInverseMass = left.inverseMass + right.inverseMass;
            if (totalInverseMass <= Math::DefaultEpsilon)
            {
                return;
            }

            Vector3 leftVelocity = leftRigidBody != nullptr ? leftRigidBody->GetLinearVelocity() : Vector3::Zero();
            Vector3 rightVelocity = rightRigidBody != nullptr ? rightRigidBody->GetLinearVelocity() : Vector3::Zero();
            Vector3 leftAngular = leftRigidBody != nullptr ? leftRigidBody->GetAngularVelocity() : Vector3::Zero();
            Vector3 rightAngular = rightRigidBody != nullptr ? rightRigidBody->GetAngularVelocity() : Vector3::Zero();

            const Vector3 leftArm = contact.point - left.pose.position;
            const Vector3 rightArm = contact.point - right.pose.position;
            const Vector3 leftContactVelocity = leftVelocity + Vector3::Cross(leftAngular, leftArm);
            const Vector3 rightContactVelocity = rightVelocity + Vector3::Cross(rightAngular, rightArm);
            const Vector3 relativeVelocity = leftContactVelocity - rightContactVelocity;
            const Float32 velocityAlongNormal = Vector3::Dot(relativeVelocity, contact.normal);
            if (velocityAlongNormal > 0.0f)
            {
                return;
            }

            const Vector3 leftAngularTerm =
                Vector3::Cross(ApplyDiagonalInertia(left.inverseInertia, Vector3::Cross(leftArm, contact.normal)),
                               leftArm);
            const Vector3 rightAngularTerm =
                Vector3::Cross(ApplyDiagonalInertia(right.inverseInertia, Vector3::Cross(rightArm, contact.normal)),
                               rightArm);
            const Float32 denominator =
                totalInverseMass + Vector3::Dot(contact.normal, leftAngularTerm + rightAngularTerm);
            if (denominator <= Math::DefaultEpsilon)
            {
                return;
            }

            const Float32 impulseMagnitude = -(1.0f + Restitution) * velocityAlongNormal / denominator;
            const Vector3 impulse = contact.normal * std::max(impulseMagnitude, 0.0f);

            if (leftRigidBody != nullptr && left.inverseMass > 0.0f)
            {
                leftVelocity += impulse * left.inverseMass;
                leftAngular += ApplyDiagonalInertia(left.inverseInertia, Vector3::Cross(leftArm, impulse));
                leftVelocity *= FrictionDamping;
                leftRigidBody->SetLinearVelocity(leftVelocity);
                leftRigidBody->SetAngularVelocity(leftAngular);
            }

            if (rightRigidBody != nullptr && right.inverseMass > 0.0f)
            {
                rightVelocity -= impulse * right.inverseMass;
                rightAngular -= ApplyDiagonalInertia(right.inverseInertia, Vector3::Cross(rightArm, impulse));
                rightVelocity *= FrictionDamping;
                rightRigidBody->SetLinearVelocity(rightVelocity);
                rightRigidBody->SetAngularVelocity(rightAngular);
            }
        }

        void SyncGameObject(GameObject& gameObject, std::vector<BodyProxy>& proxies)
        {
            if (!gameObject.IsActiveInHierarchy())
            {
                return;
            }

            TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
            ColliderComponent* collider = gameObject.GetComponent<ColliderComponent>();
            RigidBodyComponent* rigidBody = gameObject.GetComponent<RigidBodyComponent>();
            if (transform != nullptr && (collider != nullptr || rigidBody != nullptr))
            {
                BodyProxy proxy;
                proxy.gameObjectId = gameObject.GetId();
                proxy.transform = transform;
                proxy.collider = collider != nullptr && collider->IsColliderEnabled() ? collider : nullptr;
                proxy.rigidBody = rigidBody != nullptr && rigidBody->IsActiveAndEnabled() ? rigidBody : nullptr;
                proxy.bodyType = proxy.rigidBody != nullptr ? proxy.rigidBody->GetBodyType() : RigidBodyType::Static;
                proxy.interpolationMode = proxy.rigidBody != nullptr ? proxy.rigidBody->GetInterpolationMode()
                                                                      : PhysicsInterpolationMode::None;
                proxy.inverseMass = proxy.bodyType == RigidBodyType::Dynamic && proxy.rigidBody != nullptr
                                        ? SafeInverse(proxy.rigidBody->GetMass())
                                        : 0.0f;
                proxy.pose = MakePose(*transform);
                BuildShape(proxy);
                proxy.inverseInertia = CalculateInverseInertia(proxy);
                proxies.push_back(proxy);
            }

            for (GameObject* child : gameObject.GetChildren())
            {
                SyncGameObject(*child, proxies);
            }
        }
    } // namespace

    void PhysicsSystem::Clear()
    {
        bodyProxies_.clear();
        poseHistory_.clear();
        queryWorld_.Clear();
        lastStepStats_ = PhysicsStepStats{};
    }

    void PhysicsSystem::SyncFromScene(Scene& scene)
    {
        scene.UpdateTransforms();
        bodyProxies_.clear();
        for (GameObject* root : scene.GetRootGameObjects())
        {
            SyncGameObject(*root, bodyProxies_);
        }

        for (BodyProxy& proxy : bodyProxies_)
        {
            const auto historyIter = poseHistory_.find(proxy.gameObjectId);
            if (historyIter == poseHistory_.end())
            {
                PoseHistory history;
                history.previous = proxy.pose;
                history.current = proxy.pose;
                history.interpolationMode = proxy.interpolationMode;
                poseHistory_.emplace(proxy.gameObjectId, history);
            }
            else
            {
                PoseHistory& history = historyIter->second;
                history.previous = history.current;
                history.interpolationMode = proxy.interpolationMode;
            }
        }
    }

    PhysicsStepStats PhysicsSystem::Step(Float32 fixedDeltaSeconds)
    {
        lastStepStats_ = PhysicsStepStats{};
        if (fixedDeltaSeconds <= 0.0f)
        {
            return lastStepStats_;
        }

        for (BodyProxy& proxy : bodyProxies_)
        {
            if (proxy.rigidBody == nullptr || proxy.bodyType != RigidBodyType::Dynamic)
            {
                continue;
            }

            ++lastStepStats_.dynamicBodyCount;
            RigidBodyComponent& rigidBody = *proxy.rigidBody;
            Vector3 linearVelocity = rigidBody.GetLinearVelocity();
            Vector3 angularVelocity = rigidBody.GetAngularVelocity();

            if (rigidBody.UsesGravity())
            {
                linearVelocity += Vector3(0.0f, GravityMetersPerSecondSquared * rigidBody.GetGravityScale(), 0.0f) *
                                  fixedDeltaSeconds;
            }
            linearVelocity += rigidBody.GetAccumulatedForce() * proxy.inverseMass * fixedDeltaSeconds;
            angularVelocity += ApplyDiagonalInertia(proxy.inverseInertia, rigidBody.GetAccumulatedTorque()) *
                               fixedDeltaSeconds;

            const Float32 linearDamping = Clamp01(1.0f - (rigidBody.GetLinearDamping() * fixedDeltaSeconds));
            const Float32 angularDamping = Clamp01(1.0f - (rigidBody.GetAngularDamping() * fixedDeltaSeconds));
            linearVelocity *= linearDamping;
            angularVelocity *= angularDamping;

            proxy.pose.position += linearVelocity * fixedDeltaSeconds;
            proxy.pose.rotation = IntegrateRotation(proxy.pose.rotation, angularVelocity, fixedDeltaSeconds);
            rigidBody.SetLinearVelocity(linearVelocity);
            rigidBody.SetAngularVelocity(angularVelocity);
        }

        for (BodyProxy& proxy : bodyProxies_)
        {
            if (proxy.transform != nullptr)
            {
                proxy.transform->SetLocalPosition(proxy.pose.position);
                proxy.transform->SetLocalRotation(proxy.pose.rotation);
            }
        }
        for (BodyProxy& proxy : bodyProxies_)
        {
            if (proxy.transform != nullptr)
            {
                proxy.transform->GetScene().UpdateTransforms();
                break;
            }
        }
        for (BodyProxy& proxy : bodyProxies_)
        {
            BuildShape(proxy);
        }

        std::vector<Contact> contacts;
        for (SizeT leftIndex = 0; leftIndex < bodyProxies_.size(); ++leftIndex)
        {
            for (SizeT rightIndex = leftIndex + 1; rightIndex < bodyProxies_.size(); ++rightIndex)
            {
                BodyProxy& left = bodyProxies_[leftIndex];
                BodyProxy& right = bodyProxies_[rightIndex];
                if (left.collider == nullptr || right.collider == nullptr)
                {
                    continue;
                }
                if (left.inverseMass <= 0.0f && right.inverseMass <= 0.0f)
                {
                    continue;
                }

                Contact contact;
                if (BuildContact(static_cast<UInt32>(leftIndex), static_cast<UInt32>(rightIndex), left, right, contact))
                {
                    contacts.push_back(contact);
                }
            }
        }

        for (const Contact& contact : contacts)
        {
            if (contact.isTrigger)
            {
                ++lastStepStats_.triggerOverlapCount;
                continue;
            }

            ++lastStepStats_.contactCount;
            BodyProxy& left = bodyProxies_[contact.left];
            BodyProxy& right = bodyProxies_[contact.right];
            ApplyPositionalCorrection(left, right, contact);
            ApplyImpulse(left, right, contact);
        }

        for (BodyProxy& proxy : bodyProxies_)
        {
            if (proxy.transform != nullptr)
            {
                proxy.transform->SetLocalPosition(proxy.pose.position);
                proxy.transform->SetLocalRotation(proxy.pose.rotation);
                BuildShape(proxy);
            }

            if (proxy.rigidBody != nullptr)
            {
                proxy.rigidBody->ClearAccumulators();
            }

            PoseHistory& history = poseHistory_[proxy.gameObjectId];
            history.current = proxy.pose;
            history.linearVelocity =
                proxy.rigidBody != nullptr ? proxy.rigidBody->GetLinearVelocity() : Vector3::Zero();
            history.angularVelocity =
                proxy.rigidBody != nullptr ? proxy.rigidBody->GetAngularVelocity() : Vector3::Zero();
            history.interpolationMode = proxy.interpolationMode;
        }

        return lastStepStats_;
    }

    void PhysicsSystem::WriteBackTransforms()
    {
        for (BodyProxy& proxy : bodyProxies_)
        {
            if (proxy.transform != nullptr && proxy.rigidBody != nullptr && proxy.bodyType == RigidBodyType::Dynamic)
            {
                proxy.transform->SetLocalPosition(proxy.pose.position);
                proxy.transform->SetLocalRotation(proxy.pose.rotation);
            }
        }
    }

    void PhysicsSystem::SyncQueriesFromScene(const Scene& scene)
    {
        queryWorld_.SyncFromScene(scene);
    }

    const PhysicsWorld& PhysicsSystem::GetQueryWorld() const noexcept
    {
        return queryWorld_;
    }

    PhysicsWorld& PhysicsSystem::GetQueryWorld() noexcept
    {
        return queryWorld_;
    }

    const PhysicsStepStats& PhysicsSystem::GetLastStepStats() const noexcept
    {
        return lastStepStats_;
    }

    std::optional<PhysicsPose>
    PhysicsSystem::GetPresentationPose(SceneObjectId gameObjectId, Float32 alpha) const noexcept
    {
        const auto iter = poseHistory_.find(gameObjectId);
        if (iter == poseHistory_.end())
        {
            return std::nullopt;
        }

        const PoseHistory& history = iter->second;
        PhysicsPose pose = history.current;
        const Float32 clampedAlpha = Clamp01(alpha);
        switch (history.interpolationMode)
        {
        case PhysicsInterpolationMode::Interpolate:
            pose.position = LerpVector(history.previous.position, history.current.position, clampedAlpha);
            pose.rotation = Slerp(history.previous.rotation, history.current.rotation, clampedAlpha);
            break;
        case PhysicsInterpolationMode::Extrapolate:
            pose.position = history.current.position + (history.linearVelocity * clampedAlpha);
            pose.rotation = IntegrateRotation(history.current.rotation, history.angularVelocity, clampedAlpha);
            break;
        case PhysicsInterpolationMode::None:
        default:
            break;
        }
        return pose;
    }

    SizeT PhysicsSystem::GetPoseHistoryCount() const noexcept
    {
        return poseHistory_.size();
    }
} // namespace ve
