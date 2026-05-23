#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Math.h"

namespace ve
{
/// Three-component floating point vector for positions, directions, colors, scales, and normals.
///
/// `Vector3` is intentionally a compact value type. Store it directly inside runtime objects; use `PoolAllocator` for
/// the owning objects when many transform-like records need fixed-size allocation.
class Vector3
{
public:
    /// Creates the zero vector `(0, 0, 0)`.
    constexpr Vector3() noexcept = default;

    /// Creates a vector from explicit component values.
    ///
    /// Usage: `Vector3 position(0.0f, 1.0f, 5.0f);`
    constexpr Vector3(Float32 x, Float32 y, Float32 z) noexcept
        : x_(x)
        , y_(y)
        , z_(z)
    {
    }

    /// Returns the zero vector `(0, 0, 0)`.
    [[nodiscard]] static constexpr Vector3 Zero() noexcept
    {
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    /// Returns the one vector `(1, 1, 1)`.
    [[nodiscard]] static constexpr Vector3 One() noexcept
    {
        return Vector3(1.0f, 1.0f, 1.0f);
    }

    /// Returns the unit X vector `(1, 0, 0)`.
    [[nodiscard]] static constexpr Vector3 UnitX() noexcept
    {
        return Vector3(1.0f, 0.0f, 0.0f);
    }

    /// Returns the unit Y vector `(0, 1, 0)`.
    [[nodiscard]] static constexpr Vector3 UnitY() noexcept
    {
        return Vector3(0.0f, 1.0f, 0.0f);
    }

    /// Returns the unit Z vector `(0, 0, 1)`.
    [[nodiscard]] static constexpr Vector3 UnitZ() noexcept
    {
        return Vector3(0.0f, 0.0f, 1.0f);
    }

    /// Returns the X component.
    [[nodiscard]] constexpr Float32 GetX() const noexcept
    {
        return x_;
    }

    /// Returns the Y component.
    [[nodiscard]] constexpr Float32 GetY() const noexcept
    {
        return y_;
    }

    /// Returns the Z component.
    [[nodiscard]] constexpr Float32 GetZ() const noexcept
    {
        return z_;
    }

    /// Replaces the X component.
    constexpr void SetX(Float32 x) noexcept
    {
        x_ = x;
    }

    /// Replaces the Y component.
    constexpr void SetY(Float32 y) noexcept
    {
        y_ = y;
    }

    /// Replaces the Z component.
    constexpr void SetZ(Float32 z) noexcept
    {
        z_ = z;
    }

    /// Returns the squared length.
    ///
    /// Use this for distance comparisons when the actual length is not needed.
    [[nodiscard]] constexpr Float32 LengthSquared() const noexcept
    {
        return Dot(*this, *this);
    }

    /// Returns the Euclidean length.
    [[nodiscard]] Float32 Length() const noexcept
    {
        return Sqrt(LengthSquared());
    }

    /// Returns a unit-length copy of this vector, or zero when the vector is too small to normalize.
    [[nodiscard]] Vector3 Normalized(Float32 epsilon = Math::DefaultEpsilon) const noexcept
    {
        const Float32 length = Length();
        return length > epsilon ? (*this / length) : Vector3::Zero();
    }

    /// Returns true when every component is within `epsilon` of `other`.
    [[nodiscard]] bool IsNearlyEqual(const Vector3& other, Float32 epsilon = Math::DefaultEpsilon) const noexcept
    {
        return NearlyEqual(x_, other.x_, epsilon) && NearlyEqual(y_, other.y_, epsilon)
               && NearlyEqual(z_, other.z_, epsilon);
    }

    /// Returns the dot product of two vectors.
    [[nodiscard]] static constexpr Float32 Dot(const Vector3& left, const Vector3& right) noexcept
    {
        return (left.x_ * right.x_) + (left.y_ * right.y_) + (left.z_ * right.z_);
    }

    /// Returns the cross product of two vectors.
    ///
    /// Use this to build perpendicular axes; input direction order determines the handedness of the result.
    [[nodiscard]] static constexpr Vector3 Cross(const Vector3& left, const Vector3& right) noexcept
    {
        return Vector3(
            (left.y_ * right.z_) - (left.z_ * right.y_),
            (left.z_ * right.x_) - (left.x_ * right.z_),
            (left.x_ * right.y_) - (left.y_ * right.x_));
    }

    /// Returns the component-wise sum.
    [[nodiscard]] constexpr Vector3 operator+(const Vector3& right) const noexcept
    {
        return Vector3(x_ + right.x_, y_ + right.y_, z_ + right.z_);
    }

    /// Returns the component-wise difference.
    [[nodiscard]] constexpr Vector3 operator-(const Vector3& right) const noexcept
    {
        return Vector3(x_ - right.x_, y_ - right.y_, z_ - right.z_);
    }

    /// Returns this vector with every component negated.
    [[nodiscard]] constexpr Vector3 operator-() const noexcept
    {
        return Vector3(-x_, -y_, -z_);
    }

    /// Returns this vector multiplied by a scalar.
    [[nodiscard]] constexpr Vector3 operator*(Float32 scalar) const noexcept
    {
        return Vector3(x_ * scalar, y_ * scalar, z_ * scalar);
    }

    /// Returns this vector divided by a scalar.
    ///
    /// The caller is responsible for avoiding division by zero.
    [[nodiscard]] constexpr Vector3 operator/(Float32 scalar) const noexcept
    {
        return Vector3(x_ / scalar, y_ / scalar, z_ / scalar);
    }

    /// Adds another vector into this one.
    constexpr Vector3& operator+=(const Vector3& right) noexcept
    {
        x_ += right.x_;
        y_ += right.y_;
        z_ += right.z_;
        return *this;
    }

    /// Subtracts another vector from this one.
    constexpr Vector3& operator-=(const Vector3& right) noexcept
    {
        x_ -= right.x_;
        y_ -= right.y_;
        z_ -= right.z_;
        return *this;
    }

    /// Multiplies this vector by a scalar in place.
    constexpr Vector3& operator*=(Float32 scalar) noexcept
    {
        x_ *= scalar;
        y_ *= scalar;
        z_ *= scalar;
        return *this;
    }

    /// Divides this vector by a scalar in place.
    ///
    /// The caller is responsible for avoiding division by zero.
    constexpr Vector3& operator/=(Float32 scalar) noexcept
    {
        x_ /= scalar;
        y_ /= scalar;
        z_ /= scalar;
        return *this;
    }

    /// Returns true when all components are exactly equal.
    ///
    /// Prefer `IsNearlyEqual` for values that have gone through floating point arithmetic.
    [[nodiscard]] friend constexpr bool operator==(const Vector3& left, const Vector3& right) noexcept
    {
        return left.x_ == right.x_ && left.y_ == right.y_ && left.z_ == right.z_;
    }

    /// Returns true when any component differs exactly.
    [[nodiscard]] friend constexpr bool operator!=(const Vector3& left, const Vector3& right) noexcept
    {
        return !(left == right);
    }

    /// Returns a vector multiplied by a scalar written on the left side.
    [[nodiscard]] friend constexpr Vector3 operator*(Float32 scalar, const Vector3& value) noexcept
    {
        return value * scalar;
    }

private:
    Float32 x_ = 0.0f;
    Float32 y_ = 0.0f;
    Float32 z_ = 0.0f;
};
}
