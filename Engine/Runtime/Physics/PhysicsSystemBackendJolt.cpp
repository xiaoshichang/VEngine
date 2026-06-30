#include "Engine/Runtime/Physics/PhysicsSystemBackendJolt.h"

#include "Engine/Runtime/Core/Assert.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <mutex>
#include <new>
#include <vector>

namespace ve
{
    namespace
    {
        namespace PhysicsLayers
        {
            constexpr JPH::ObjectLayer Static = 0;
            constexpr JPH::ObjectLayer Dynamic = 1;
            constexpr JPH::ObjectLayer Count = 2;
        } // namespace PhysicsLayers

        namespace BroadPhaseLayers
        {
            constexpr JPH::BroadPhaseLayer Static(0);
            constexpr JPH::BroadPhaseLayer Dynamic(1);
            constexpr JPH::uint Count = 2;
        } // namespace BroadPhaseLayers

        struct JoltRuntimeState
        {
            std::mutex mutex;
            UInt32 referenceCount = 0;
        };

        [[nodiscard]] JoltRuntimeState& GetJoltRuntimeState()
        {
            static JoltRuntimeState state;
            return state;
        }

        [[nodiscard]] ErrorCode AcquireJoltRuntime()
        {
            JoltRuntimeState& state = GetJoltRuntimeState();
            std::lock_guard<std::mutex> lock(state.mutex);

            if (state.referenceCount == 0)
            {
                try
                {
                    JPH::RegisterDefaultAllocator();
                    JPH::Factory::sInstance = new JPH::Factory();
                    JPH::RegisterTypes();
                }
                catch (const std::bad_alloc&)
                {
                    delete JPH::Factory::sInstance;
                    JPH::Factory::sInstance = nullptr;
                    return ErrorCode::OutOfMemory;
                }
            }

            ++state.referenceCount;
            return ErrorCode::None;
        }

        void ReleaseJoltRuntime() noexcept
        {
            JoltRuntimeState& state = GetJoltRuntimeState();
            std::lock_guard<std::mutex> lock(state.mutex);

            VE_ASSERT_MESSAGE(state.referenceCount > 0, "Jolt runtime reference count underflowed.");
            --state.referenceCount;
            if (state.referenceCount == 0)
            {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        }

        [[nodiscard]] JPH::Vec3 ToJoltVec3(Vector3 value) noexcept
        {
            return JPH::Vec3(value.GetX(), value.GetY(), value.GetZ());
        }

        [[nodiscard]] JPH::RVec3 ToJoltRVec3(Vector3 value) noexcept
        {
            return JPH::RVec3(value.GetX(), value.GetY(), value.GetZ());
        }

        [[nodiscard]] JPH::Quat ToJoltQuat(const Quaternion& value) noexcept
        {
            return JPH::Quat(value.GetX(), value.GetY(), value.GetZ(), value.GetW());
        }

        [[nodiscard]] Vector3 FromJoltVec3(JPH::Vec3 value) noexcept
        {
            return Vector3(value.GetX(), value.GetY(), value.GetZ());
        }

        [[nodiscard]] Vector3 FromJoltRVec3(JPH::RVec3 value) noexcept
        {
            return Vector3(static_cast<Float32>(value.GetX()), static_cast<Float32>(value.GetY()), static_cast<Float32>(value.GetZ()));
        }

        [[nodiscard]] Quaternion FromJoltQuat(JPH::Quat value) noexcept
        {
            return Quaternion(value.GetX(), value.GetY(), value.GetZ(), value.GetW());
        }

        [[nodiscard]] JPH::BodyID ToJoltBodyId(PhysicsBodyHandle body) noexcept
        {
            return JPH::BodyID(body.GetRawValue());
        }

        [[nodiscard]] PhysicsBodyHandle FromJoltBodyId(JPH::BodyID body) noexcept
        {
            return PhysicsBodyHandle::FromRawValue(body.GetIndexAndSequenceNumber());
        }

        [[nodiscard]] JPH::EMotionType ToJoltMotionType(PhysicsBodyMotionType motionType) noexcept
        {
            switch (motionType)
            {
            case PhysicsBodyMotionType::Static:
                return JPH::EMotionType::Static;
            case PhysicsBodyMotionType::Kinematic:
                return JPH::EMotionType::Kinematic;
            case PhysicsBodyMotionType::Dynamic:
                return JPH::EMotionType::Dynamic;
            }

            return JPH::EMotionType::Dynamic;
        }

        [[nodiscard]] JPH::ObjectLayer ToJoltObjectLayer(PhysicsBodyMotionType motionType) noexcept
        {
            return motionType == PhysicsBodyMotionType::Static ? PhysicsLayers::Static : PhysicsLayers::Dynamic;
        }

        [[nodiscard]] JPH::EActivation ToJoltActivation(bool activate) noexcept
        {
            return activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
        }

        [[nodiscard]] ErrorCode ValidateShapeDesc(const PhysicsShapeDesc& desc)
        {
            switch (desc.type)
            {
            case PhysicsShapeType::Sphere:
                return desc.radius > 0.0f ? ErrorCode::None : ErrorCode::InvalidArgument;
            case PhysicsShapeType::Box:
                if (desc.halfExtent.GetX() <= 0.0f || desc.halfExtent.GetY() <= 0.0f || desc.halfExtent.GetZ() <= 0.0f)
                {
                    return ErrorCode::InvalidArgument;
                }

                return ErrorCode::None;
            case PhysicsShapeType::Capsule:
                return desc.radius > 0.0f && desc.halfHeight > 0.0f ? ErrorCode::None : ErrorCode::InvalidArgument;
            }

            return ErrorCode::InvalidArgument;
        }

        [[nodiscard]] JPH::RefConst<JPH::Shape> CreateJoltShape(const PhysicsShapeDesc& desc)
        {
            switch (desc.type)
            {
            case PhysicsShapeType::Sphere:
                return new JPH::SphereShape(desc.radius);
            case PhysicsShapeType::Box:
                return new JPH::BoxShape(ToJoltVec3(desc.halfExtent));
            case PhysicsShapeType::Capsule:
                return new JPH::CapsuleShape(desc.halfHeight, desc.radius);
            }

            return nullptr;
        }
    } // namespace

    struct PhysicsSystemBackendJoltImpl
    {
        std::unique_ptr<JPH::ObjectLayerPairFilterTable> objectLayerPairFilter;
        std::unique_ptr<JPH::BroadPhaseLayerInterfaceTable> broadPhaseLayerInterface;
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> objectVsBroadPhaseLayerFilter;
        std::unique_ptr<JPH::PhysicsSystem> physicsSystem;
        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JPH::JobSystemSingleThreaded> jobSystem;
        std::vector<JPH::BodyID> bodyIds;
        bool joltRuntimeAcquired = false;
        bool initialized = false;
    };

    PhysicsSystemBackendJolt::PhysicsSystemBackendJolt()
        : impl_(std::make_unique<PhysicsSystemBackendJoltImpl>())
    {
    }

    PhysicsSystemBackendJolt::~PhysicsSystemBackendJolt()
    {
        Shutdown();
    }

    ErrorCode PhysicsSystemBackendJolt::Initialize(const PhysicsSystemInitParam& initParam)
    {
        if (impl_->initialized)
        {
            return ErrorCode::InvalidState;
        }

        if (initParam.maxBodies == 0 || initParam.maxBodyPairs == 0 || initParam.maxContactConstraints == 0 || initParam.tempAllocatorSizeBytes == 0)
        {
            return ErrorCode::InvalidArgument;
        }

        const ErrorCode runtimeResult = AcquireJoltRuntime();
        if (runtimeResult != ErrorCode::None)
        {
            return runtimeResult;
        }
        impl_->joltRuntimeAcquired = true;

        try
        {
            impl_->objectLayerPairFilter = std::make_unique<JPH::ObjectLayerPairFilterTable>(PhysicsLayers::Count);
            impl_->objectLayerPairFilter->EnableCollision(PhysicsLayers::Static, PhysicsLayers::Dynamic);
            impl_->objectLayerPairFilter->EnableCollision(PhysicsLayers::Dynamic, PhysicsLayers::Dynamic);

            impl_->broadPhaseLayerInterface =
                std::make_unique<JPH::BroadPhaseLayerInterfaceTable>(PhysicsLayers::Count, BroadPhaseLayers::Count);
            impl_->broadPhaseLayerInterface->MapObjectToBroadPhaseLayer(PhysicsLayers::Static, BroadPhaseLayers::Static);
            impl_->broadPhaseLayerInterface->MapObjectToBroadPhaseLayer(PhysicsLayers::Dynamic, BroadPhaseLayers::Dynamic);

            impl_->objectVsBroadPhaseLayerFilter = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(*impl_->broadPhaseLayerInterface,
                                                                                                             BroadPhaseLayers::Count,
                                                                                                             *impl_->objectLayerPairFilter,
                                                                                                             PhysicsLayers::Count);

            impl_->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
            impl_->physicsSystem->Init(initParam.maxBodies,
                                       initParam.bodyMutexCount,
                                       initParam.maxBodyPairs,
                                       initParam.maxContactConstraints,
                                       *impl_->broadPhaseLayerInterface,
                                       *impl_->objectVsBroadPhaseLayerFilter,
                                       *impl_->objectLayerPairFilter);
            impl_->physicsSystem->SetGravity(ToJoltVec3(initParam.gravity));

            impl_->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(initParam.tempAllocatorSizeBytes);
            impl_->jobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        }
        catch (const std::bad_alloc&)
        {
            Shutdown();
            return ErrorCode::OutOfMemory;
        }

        impl_->initialized = true;
        return ErrorCode::None;
    }

    void PhysicsSystemBackendJolt::Shutdown() noexcept
    {
        if (!impl_->initialized && !impl_->joltRuntimeAcquired)
        {
            return;
        }

        if (impl_->physicsSystem != nullptr)
        {
            JPH::BodyInterface& bodyInterface = impl_->physicsSystem->GetBodyInterface();
            for (JPH::BodyID bodyId : impl_->bodyIds)
            {
                if (bodyInterface.IsAdded(bodyId))
                {
                    bodyInterface.RemoveBody(bodyId);
                }
                bodyInterface.DestroyBody(bodyId);
            }
            impl_->bodyIds.clear();
        }

        impl_->jobSystem.reset();
        impl_->tempAllocator.reset();
        impl_->physicsSystem.reset();
        impl_->objectVsBroadPhaseLayerFilter.reset();
        impl_->broadPhaseLayerInterface.reset();
        impl_->objectLayerPairFilter.reset();
        impl_->initialized = false;

        if (impl_->joltRuntimeAcquired)
        {
            ReleaseJoltRuntime();
            impl_->joltRuntimeAcquired = false;
        }
    }

    bool PhysicsSystemBackendJolt::IsInitialized() const noexcept
    {
        return impl_->initialized;
    }

    PhysicsBackendType PhysicsSystemBackendJolt::GetBackendType() const noexcept
    {
        return PhysicsBackendType::Jolt;
    }

    ErrorCode PhysicsSystemBackendJolt::StepSimulation(const PhysicsStepDesc& desc)
    {
        if (!impl_->initialized)
        {
            return ErrorCode::InvalidState;
        }

        if (desc.deltaSeconds <= 0.0f || desc.collisionSteps == 0)
        {
            return ErrorCode::InvalidArgument;
        }

        const JPH::EPhysicsUpdateError updateResult =
            impl_->physicsSystem->Update(desc.deltaSeconds, static_cast<int>(desc.collisionSteps), impl_->tempAllocator.get(), impl_->jobSystem.get());
        return updateResult == JPH::EPhysicsUpdateError::None ? ErrorCode::None : ErrorCode::PlatformError;
    }

    Result<PhysicsBodyHandle> PhysicsSystemBackendJolt::CreateBody(const PhysicsBodyDesc& desc)
    {
        if (!impl_->initialized)
        {
            return Result<PhysicsBodyHandle>::Failure(Error(ErrorCode::InvalidState, "Physics backend is not initialized."));
        }

        const ErrorCode shapeValidationResult = ValidateShapeDesc(desc.shape);
        if (shapeValidationResult != ErrorCode::None)
        {
            return Result<PhysicsBodyHandle>::Failure(Error(shapeValidationResult, "Physics body requires a valid shape description."));
        }

        if ((desc.motionType == PhysicsBodyMotionType::Dynamic && desc.mass <= 0.0f) || desc.linearDamping < 0.0f || desc.angularDamping < 0.0f)
        {
            return Result<PhysicsBodyHandle>::Failure(Error(ErrorCode::InvalidArgument, "Physics body requires valid mass and damping values."));
        }

        JPH::RefConst<JPH::Shape> shape = CreateJoltShape(desc.shape);
        if (shape == nullptr)
        {
            return Result<PhysicsBodyHandle>::Failure(Error(ErrorCode::OutOfMemory, "Failed to create Jolt shape."));
        }

        JPH::BodyCreationSettings settings(shape,
                                           ToJoltRVec3(desc.transform.position),
                                           ToJoltQuat(desc.transform.rotation),
                                           ToJoltMotionType(desc.motionType),
                                           ToJoltObjectLayer(desc.motionType));
        settings.mLinearVelocity = ToJoltVec3(desc.linearVelocity);
        settings.mAngularVelocity = ToJoltVec3(desc.angularVelocity);
        settings.mLinearDamping = desc.linearDamping;
        settings.mAngularDamping = desc.angularDamping;
        settings.mGravityFactor = desc.gravityFactor;
        settings.mFriction = desc.friction;
        settings.mRestitution = desc.restitution;
        settings.mUserData = desc.userData;
        settings.mIsSensor = desc.trigger;
        if (desc.motionType == PhysicsBodyMotionType::Dynamic)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = desc.mass;
        }

        JPH::BodyInterface& bodyInterface = impl_->physicsSystem->GetBodyInterface();
        const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, ToJoltActivation(desc.startActive));
        if (bodyId.IsInvalid())
        {
            return Result<PhysicsBodyHandle>::Failure(Error(ErrorCode::OutOfMemory, "Jolt failed to allocate a physics body."));
        }

        try
        {
            impl_->bodyIds.push_back(bodyId);
        }
        catch (const std::bad_alloc&)
        {
            bodyInterface.RemoveBody(bodyId);
            bodyInterface.DestroyBody(bodyId);
            return Result<PhysicsBodyHandle>::Failure(Error(ErrorCode::OutOfMemory, "Failed to store physics body handle."));
        }

        return Result<PhysicsBodyHandle>::Success(FromJoltBodyId(bodyId));
    }

    ErrorCode PhysicsSystemBackendJolt::DestroyBody(PhysicsBodyHandle body)
    {
        if (!impl_->initialized)
        {
            return ErrorCode::InvalidState;
        }

        if (!body.IsValid())
        {
            return ErrorCode::InvalidArgument;
        }

        const JPH::BodyID bodyId = ToJoltBodyId(body);
        JPH::BodyInterface& bodyInterface = impl_->physicsSystem->GetBodyInterface();
        if (!bodyInterface.IsAdded(bodyId))
        {
            return ErrorCode::NotFound;
        }

        bodyInterface.RemoveBody(bodyId);
        bodyInterface.DestroyBody(bodyId);
        const auto bodyIt = std::find(impl_->bodyIds.begin(), impl_->bodyIds.end(), bodyId);
        if (bodyIt != impl_->bodyIds.end())
        {
            impl_->bodyIds.erase(bodyIt);
        }
        return ErrorCode::None;
    }

    Result<PhysicsBodyTransform> PhysicsSystemBackendJolt::GetBodyTransform(PhysicsBodyHandle body) const
    {
        if (!impl_->initialized)
        {
            return Result<PhysicsBodyTransform>::Failure(Error(ErrorCode::InvalidState, "Physics backend is not initialized."));
        }

        if (!body.IsValid())
        {
            return Result<PhysicsBodyTransform>::Failure(Error(ErrorCode::InvalidArgument, "Physics body handle is invalid."));
        }

        const JPH::BodyID bodyId = ToJoltBodyId(body);
        const JPH::BodyInterface& bodyInterface = impl_->physicsSystem->GetBodyInterface();
        if (!bodyInterface.IsAdded(bodyId))
        {
            return Result<PhysicsBodyTransform>::Failure(Error(ErrorCode::NotFound, "Physics body was not found."));
        }

        JPH::RVec3 position = JPH::RVec3::sZero();
        JPH::Quat rotation = JPH::Quat::sIdentity();
        bodyInterface.GetPositionAndRotation(bodyId, position, rotation);

        PhysicsBodyTransform transform;
        transform.position = FromJoltRVec3(position);
        transform.rotation = FromJoltQuat(rotation);
        return Result<PhysicsBodyTransform>::Success(transform);
    }

    ErrorCode PhysicsSystemBackendJolt::SetBodyTransform(PhysicsBodyHandle body, const PhysicsBodyTransform& transform)
    {
        if (!impl_->initialized)
        {
            return ErrorCode::InvalidState;
        }

        if (!body.IsValid())
        {
            return ErrorCode::InvalidArgument;
        }

        const JPH::BodyID bodyId = ToJoltBodyId(body);
        JPH::BodyInterface& bodyInterface = impl_->physicsSystem->GetBodyInterface();
        if (!bodyInterface.IsAdded(bodyId))
        {
            return ErrorCode::NotFound;
        }

        bodyInterface.SetPositionAndRotation(bodyId, ToJoltRVec3(transform.position), ToJoltQuat(transform.rotation), JPH::EActivation::Activate);
        return ErrorCode::None;
    }

    Result<Vector3> PhysicsSystemBackendJolt::GetBodyLinearVelocity(PhysicsBodyHandle body) const
    {
        if (!impl_->initialized)
        {
            return Result<Vector3>::Failure(Error(ErrorCode::InvalidState, "Physics backend is not initialized."));
        }

        if (!body.IsValid())
        {
            return Result<Vector3>::Failure(Error(ErrorCode::InvalidArgument, "Physics body handle is invalid."));
        }

        const JPH::BodyID bodyId = ToJoltBodyId(body);
        const JPH::BodyInterface& bodyInterface = impl_->physicsSystem->GetBodyInterface();
        if (!bodyInterface.IsAdded(bodyId))
        {
            return Result<Vector3>::Failure(Error(ErrorCode::NotFound, "Physics body was not found."));
        }

        return Result<Vector3>::Success(FromJoltVec3(bodyInterface.GetLinearVelocity(bodyId)));
    }

    ErrorCode PhysicsSystemBackendJolt::SetBodyLinearVelocity(PhysicsBodyHandle body, Vector3 velocity)
    {
        if (!impl_->initialized)
        {
            return ErrorCode::InvalidState;
        }

        if (!body.IsValid())
        {
            return ErrorCode::InvalidArgument;
        }

        const JPH::BodyID bodyId = ToJoltBodyId(body);
        JPH::BodyInterface& bodyInterface = impl_->physicsSystem->GetBodyInterface();
        if (!bodyInterface.IsAdded(bodyId))
        {
            return ErrorCode::NotFound;
        }

        bodyInterface.SetLinearVelocity(bodyId, ToJoltVec3(velocity));
        return ErrorCode::None;
    }

    Result<Vector3> PhysicsSystemBackendJolt::GetBodyAngularVelocity(PhysicsBodyHandle body) const
    {
        if (!impl_->initialized)
        {
            return Result<Vector3>::Failure(Error(ErrorCode::InvalidState, "Physics backend is not initialized."));
        }

        if (!body.IsValid())
        {
            return Result<Vector3>::Failure(Error(ErrorCode::InvalidArgument, "Physics body handle is invalid."));
        }

        const JPH::BodyID bodyId = ToJoltBodyId(body);
        const JPH::BodyInterface& bodyInterface = impl_->physicsSystem->GetBodyInterface();
        if (!bodyInterface.IsAdded(bodyId))
        {
            return Result<Vector3>::Failure(Error(ErrorCode::NotFound, "Physics body was not found."));
        }

        return Result<Vector3>::Success(FromJoltVec3(bodyInterface.GetAngularVelocity(bodyId)));
    }

    ErrorCode PhysicsSystemBackendJolt::SetBodyAngularVelocity(PhysicsBodyHandle body, Vector3 velocity)
    {
        if (!impl_->initialized)
        {
            return ErrorCode::InvalidState;
        }

        if (!body.IsValid())
        {
            return ErrorCode::InvalidArgument;
        }

        const JPH::BodyID bodyId = ToJoltBodyId(body);
        JPH::BodyInterface& bodyInterface = impl_->physicsSystem->GetBodyInterface();
        if (!bodyInterface.IsAdded(bodyId))
        {
            return ErrorCode::NotFound;
        }

        bodyInterface.SetAngularVelocity(bodyId, ToJoltVec3(velocity));
        return ErrorCode::None;
    }
} // namespace ve
