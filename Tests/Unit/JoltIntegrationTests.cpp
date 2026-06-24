#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <iostream>

namespace
{
    namespace PhysicsLayers
    {
        constexpr JPH::ObjectLayer Moving = 0;
        constexpr JPH::ObjectLayer Count = 1;
    } // namespace PhysicsLayers

    namespace BroadPhaseLayers
    {
        constexpr JPH::BroadPhaseLayer Moving(0);
        constexpr JPH::uint Count = 1;
    } // namespace BroadPhaseLayers

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    class JoltRuntime
    {
    public:
        JoltRuntime()
        {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }

        ~JoltRuntime()
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }

        JoltRuntime(const JoltRuntime&) = delete;
        JoltRuntime& operator=(const JoltRuntime&) = delete;
    };

    bool TestJoltRigidBodyFallsInsideVEngineTestTarget()
    {
        using namespace JPH::literals;

        JoltRuntime runtime;

        JPH::ObjectLayerPairFilterTable objectLayerPairFilter(PhysicsLayers::Count);
        objectLayerPairFilter.EnableCollision(PhysicsLayers::Moving, PhysicsLayers::Moving);

        JPH::BroadPhaseLayerInterfaceTable broadPhaseLayerInterface(PhysicsLayers::Count, BroadPhaseLayers::Count);
        broadPhaseLayerInterface.MapObjectToBroadPhaseLayer(PhysicsLayers::Moving, BroadPhaseLayers::Moving);

        JPH::ObjectVsBroadPhaseLayerFilterTable objectVsBroadPhaseLayerFilter(
            broadPhaseLayerInterface,
            BroadPhaseLayers::Count,
            objectLayerPairFilter,
            PhysicsLayers::Count);

        JPH::PhysicsSystem physicsSystem;
        physicsSystem.Init(16, 0, 16, 16, broadPhaseLayerInterface, objectVsBroadPhaseLayerFilter, objectLayerPairFilter);

        JPH::TempAllocatorImpl tempAllocator(1024 * 1024);
        JPH::JobSystemSingleThreaded jobSystem(JPH::cMaxPhysicsJobs);

        JPH::BodyCreationSettings sphereSettings(
            new JPH::SphereShape(0.5f),
            JPH::RVec3(0.0_r, 2.0_r, 0.0_r),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Dynamic,
            PhysicsLayers::Moving);

        JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
        const JPH::BodyID sphereId = bodyInterface.CreateAndAddBody(sphereSettings, JPH::EActivation::Activate);

        bool passed = true;
        passed &= Expect(!sphereId.IsInvalid(), "Jolt should create a dynamic body through the VEngine test target");
        if (!passed)
        {
            return false;
        }

        const JPH::RVec3 startPosition = bodyInterface.GetCenterOfMassPosition(sphereId);

        for (int step = 0; step < 10; ++step)
        {
            const JPH::EPhysicsUpdateError updateResult = physicsSystem.Update(1.0f / 60.0f, 1, &tempAllocator, &jobSystem);
            passed &= Expect(updateResult == JPH::EPhysicsUpdateError::None, "Jolt physics update should complete without errors");
        }

        const JPH::RVec3 endPosition = bodyInterface.GetCenterOfMassPosition(sphereId);
        passed &= Expect(endPosition.GetY() < startPosition.GetY(), "Jolt dynamic body should fall under gravity");

        bodyInterface.RemoveBody(sphereId);
        bodyInterface.DestroyBody(sphereId);

        return passed;
    }
} // namespace

int main()
{
    if (TestJoltRigidBodyFallsInsideVEngineTestTarget())
    {
        std::cout << "VEngineJoltIntegrationTests passed" << '\n';
        return 0;
    }

    return 1;
}
