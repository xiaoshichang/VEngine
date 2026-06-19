#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector2.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Math/Vector4.h"
#include "Engine/Runtime/Memory/PoolAllocator.h"

#include <iostream>
#include <new>
#include <type_traits>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool ExpectOk(ve::ErrorCode result, const char* message)
    {
        if (result == ve::ErrorCode::None)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result) << '\n';
        return false;
    }

    bool TestScalarHelpers()
    {
        bool passed = true;

        passed &= Expect(ve::NearlyEqual(ve::ToRadians(180.0f), ve::Math::Pi), "Degrees should convert to radians");
        passed &= Expect(ve::NearlyEqual(ve::ToDegrees(ve::Math::HalfPi), 90.0f), "Radians should convert to degrees");
        passed &= Expect(ve::Clamp(3.0f, 0.0f, 2.0f) == 2.0f, "Clamp should cap values above the range");
        passed &= Expect(ve::Clamp(-1.0f, 0.0f, 2.0f) == 0.0f, "Clamp should cap values below the range");
        passed &= Expect(ve::Lerp(10.0f, 20.0f, 0.25f) == 12.5f, "Lerp should interpolate without clamping");

        return passed;
    }

    bool TestVector2()
    {
        bool passed = true;

        const ve::Vector2 a(3.0f, 4.0f);
        const ve::Vector2 b(1.0f, 2.0f);

        passed &= Expect(a.Length() == 5.0f, "Vector2 length should use both components");
        passed &= Expect((a + b) == ve::Vector2(4.0f, 6.0f), "Vector2 addition should be component-wise");
        passed &= Expect((a - b) == ve::Vector2(2.0f, 2.0f), "Vector2 subtraction should be component-wise");
        passed &= Expect((a * 2.0f) == ve::Vector2(6.0f, 8.0f), "Vector2 scalar multiply should scale components");
        passed &= Expect(ve::Vector2::Dot(a, b) == 11.0f, "Vector2 dot product should multiply matching components");
        passed &= Expect(a.Normalized().IsNearlyEqual(ve::Vector2(0.6f, 0.8f)), "Vector2 normalization should produce unit length");

        return passed;
    }

    bool TestVector3()
    {
        bool passed = true;

        const ve::Vector3 x = ve::Vector3::UnitX();
        const ve::Vector3 y = ve::Vector3::UnitY();
        const ve::Vector3 z = ve::Vector3::UnitZ();

        passed &= Expect(ve::Vector3::Dot(x, y) == 0.0f, "Orthogonal Vector3 axes should have zero dot product");
        passed &= Expect(ve::Vector3::Cross(x, y) == z, "Vector3 cross product should build perpendicular axes");
        passed &= Expect(ve::Vector3(0.0f, 3.0f, 4.0f).Length() == 5.0f, "Vector3 length should include Z");
        passed &= Expect(ve::Vector3(0.0f, 0.0f, 0.0f).Normalized() == ve::Vector3::Zero(), "Normalizing a zero Vector3 should return zero");

        return passed;
    }

    bool TestVector4()
    {
        bool passed = true;

        const ve::Vector4 value(ve::Vector3(1.0f, 2.0f, 3.0f), 4.0f);

        passed &= Expect(value.GetXYZ() == ve::Vector3(1.0f, 2.0f, 3.0f), "Vector4 should expose XYZ as Vector3");
        passed &= Expect(value.GetW() == 4.0f, "Vector4 should preserve W");
        passed &= Expect(ve::Vector4::Dot(value, ve::Vector4::One()) == 10.0f, "Vector4 dot should include W");
        passed &= Expect((value / 2.0f).IsNearlyEqual(ve::Vector4(0.5f, 1.0f, 1.5f, 2.0f)), "Vector4 division should scale all components");

        return passed;
    }

    bool TestMatrix44()
    {
        bool passed = true;

        const ve::Matrix44 translation = ve::Matrix44::Translation(ve::Vector3(10.0f, 20.0f, 30.0f));
        const ve::Matrix44 scale = ve::Matrix44::Scale(ve::Vector3(2.0f, 3.0f, 4.0f));
        const ve::Matrix44 combined = translation * scale;

        passed &= Expect(translation.TransformPoint(ve::Vector3::Zero()).IsNearlyEqual(ve::Vector3(10.0f, 20.0f, 30.0f)),
                         "Matrix44 translation should affect points");
        passed &= Expect(translation.TransformDirection(ve::Vector3::UnitX()).IsNearlyEqual(ve::Vector3::UnitX()),
                         "Matrix44 translation should not affect directions");
        passed &= Expect(combined.TransformPoint(ve::Vector3(1.0f, 1.0f, 1.0f)).IsNearlyEqual(ve::Vector3(12.0f, 23.0f, 34.0f)),
                         "Matrix44 multiplication should apply scale before translation");
        passed &= Expect(ve::Matrix44::RotationZ(ve::ToRadians(90.0f)).TransformDirection(ve::Vector3::UnitX()).IsNearlyEqual(ve::Vector3::UnitY()),
                         "Matrix44 Z rotation should rotate X into Y");
        passed &=
            Expect(ve::Matrix44::Identity().Transposed().IsNearlyEqual(ve::Matrix44::Identity()), "Matrix44 transpose of identity should remain identity");

        return passed;
    }

    bool TestQuaternion()
    {
        bool passed = true;

        const ve::Quaternion zQuarterTurn = ve::Quaternion::FromAxisAngle(ve::Vector3::UnitZ(), ve::ToRadians(90.0f));
        const ve::Vector3 rotated = zQuarterTurn.RotateVector(ve::Vector3::UnitX());

        passed &= Expect(rotated.IsNearlyEqual(ve::Vector3::UnitY()), "Quaternion axis-angle should rotate around Z");
        passed &= Expect(ve::NearlyEqual(zQuarterTurn.Length(), 1.0f), "Quaternion axis-angle should produce unit quaternions");
        passed &= Expect(ve::Quaternion(0.0f, 0.0f, 0.0f, 0.0f).Normalized().IsNearlyEqual(ve::Quaternion::Identity()),
                         "Normalizing a zero quaternion should return identity");
        passed &= Expect(ve::Quaternion::FromEulerXYZ(0.0f, 0.0f, ve::ToRadians(90.0f)).RotateVector(ve::Vector3::UnitX()).IsNearlyEqual(ve::Vector3::UnitY()),
                         "Quaternion Euler construction should apply Z rotation");

        return passed;
    }

    bool TestMathTypesFitPoolAllocatedObjects()
    {
        struct MathRecord
        {
            ve::Matrix44 world;
            ve::Quaternion rotation;
            ve::Vector3 velocity;
        };

        bool passed = true;

        passed &= Expect(std::is_trivially_copyable_v<ve::Vector3>, "Vector3 should remain trivially copyable");
        passed &= Expect(std::is_trivially_copyable_v<ve::Matrix44>, "Matrix44 should remain trivially copyable");
        passed &= Expect(std::is_trivially_copyable_v<ve::Quaternion>, "Quaternion should remain trivially copyable");

        ve::PoolAllocator allocator;
        passed &= ExpectOk(allocator.Initialize(ve::PoolAllocatorDesc{sizeof(MathRecord), 1, alignof(MathRecord)}),
                           "PoolAllocator should initialize for a math record");

        void* memory = allocator.Allocate();
        passed &= Expect(memory != nullptr, "PoolAllocator should allocate storage for a math record");

        MathRecord* record = ::new (memory) MathRecord{
            ve::Matrix44::Translation(ve::Vector3(1.0f, 2.0f, 3.0f)),
            ve::Quaternion::Identity(),
            ve::Vector3(0.0f, 1.0f, 0.0f),
        };

        passed &= Expect(record->world.TransformPoint(ve::Vector3::Zero()).IsNearlyEqual(ve::Vector3(1.0f, 2.0f, 3.0f)),
                         "Pool-allocated objects should be able to store and use Matrix44 values");
        passed &= Expect(record->rotation.RotateVector(ve::Vector3::UnitX()).IsNearlyEqual(ve::Vector3::UnitX()),
                         "Pool-allocated objects should be able to store and use Quaternion values");
        passed &= Expect(record->velocity == ve::Vector3::UnitY(), "Pool-allocated objects should store Vector3 values");

        record->~MathRecord();
        passed &= Expect(allocator.Free(memory), "PoolAllocator should free the math record storage");

        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestScalarHelpers();
    passed &= TestVector2();
    passed &= TestVector3();
    passed &= TestVector4();
    passed &= TestMatrix44();
    passed &= TestQuaternion();
    passed &= TestMathTypesFitPoolAllocatedObjects();

    if (passed)
    {
        std::cout << "VEngineMathTests passed" << '\n';
        return 0;
    }

    return 1;
}
