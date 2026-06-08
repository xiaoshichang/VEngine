#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Vector3.h"

namespace ve
{
    /// Four-component floating point vector for homogeneous coordinates, colors, and packed shader constants.
    ///
    /// `Vector4` is a value type and does not allocate. Use it directly in arrays or constant-buffer staging
    /// structures.
    class Vector4
    {
    public:
        /// Creates the zero vector `(0, 0, 0, 0)`.
        constexpr Vector4() noexcept = default;

        /// Creates a vector from explicit component values.
        ///
        /// Usage: `Vector4 color(1.0f, 0.5f, 0.0f, 1.0f);`
        constexpr Vector4(Float32 x, Float32 y, Float32 z, Float32 w) noexcept
            : x_(x)
            , y_(y)
            , z_(z)
            , w_(w)
        {
        }

        /// Creates a vector from a `Vector3` plus an explicit W component.
        ///
        /// Use `w = 1` for points and `w = 0` for directions when building homogeneous coordinates.
        constexpr Vector4(const Vector3& xyz, Float32 w) noexcept
            : x_(xyz.GetX())
            , y_(xyz.GetY())
            , z_(xyz.GetZ())
            , w_(w)
        {
        }

        /// Returns the zero vector `(0, 0, 0, 0)`.
        [[nodiscard]] static constexpr Vector4 Zero() noexcept
        {
            return Vector4(0.0f, 0.0f, 0.0f, 0.0f);
        }

        /// Returns the one vector `(1, 1, 1, 1)`.
        [[nodiscard]] static constexpr Vector4 One() noexcept
        {
            return Vector4(1.0f, 1.0f, 1.0f, 1.0f);
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

        /// Returns the W component.
        [[nodiscard]] constexpr Float32 GetW() const noexcept
        {
            return w_;
        }

        /// Returns the XYZ portion as a `Vector3`.
        [[nodiscard]] constexpr Vector3 GetXYZ() const noexcept
        {
            return Vector3(x_, y_, z_);
        }

        /// Returns the squared length.
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
        [[nodiscard]] Vector4 Normalized(Float32 epsilon = Math::DefaultEpsilon) const noexcept
        {
            const Float32 length = Length();
            return length > epsilon ? (*this / length) : Vector4::Zero();
        }

        /// Returns true when every component is within `epsilon` of `other`.
        [[nodiscard]] bool IsNearlyEqual(const Vector4& other, Float32 epsilon = Math::DefaultEpsilon) const noexcept
        {
            return NearlyEqual(x_, other.x_, epsilon) && NearlyEqual(y_, other.y_, epsilon) &&
                   NearlyEqual(z_, other.z_, epsilon) && NearlyEqual(w_, other.w_, epsilon);
        }

        /// Returns the dot product of two vectors.
        [[nodiscard]] static constexpr Float32 Dot(const Vector4& left, const Vector4& right) noexcept
        {
            return (left.x_ * right.x_) + (left.y_ * right.y_) + (left.z_ * right.z_) + (left.w_ * right.w_);
        }

        /// Returns the component-wise sum.
        [[nodiscard]] constexpr Vector4 operator+(const Vector4& right) const noexcept
        {
            return Vector4(x_ + right.x_, y_ + right.y_, z_ + right.z_, w_ + right.w_);
        }

        /// Returns the component-wise difference.
        [[nodiscard]] constexpr Vector4 operator-(const Vector4& right) const noexcept
        {
            return Vector4(x_ - right.x_, y_ - right.y_, z_ - right.z_, w_ - right.w_);
        }

        /// Returns this vector multiplied by a scalar.
        [[nodiscard]] constexpr Vector4 operator*(Float32 scalar) const noexcept
        {
            return Vector4(x_ * scalar, y_ * scalar, z_ * scalar, w_ * scalar);
        }

        /// Returns this vector divided by a scalar.
        ///
        /// The caller is responsible for avoiding division by zero.
        [[nodiscard]] constexpr Vector4 operator/(Float32 scalar) const noexcept
        {
            return Vector4(x_ / scalar, y_ / scalar, z_ / scalar, w_ / scalar);
        }

        /// Returns true when all components are exactly equal.
        ///
        /// Prefer `IsNearlyEqual` for values that have gone through floating point arithmetic.
        [[nodiscard]] friend constexpr bool operator==(const Vector4& left, const Vector4& right) noexcept
        {
            return left.x_ == right.x_ && left.y_ == right.y_ && left.z_ == right.z_ && left.w_ == right.w_;
        }

        /// Returns true when any component differs exactly.
        [[nodiscard]] friend constexpr bool operator!=(const Vector4& left, const Vector4& right) noexcept
        {
            return !(left == right);
        }

        /// Returns a vector multiplied by a scalar written on the left side.
        [[nodiscard]] friend constexpr Vector4 operator*(Float32 scalar, const Vector4& value) noexcept
        {
            return value * scalar;
        }

    private:
        Float32 x_ = 0.0f;
        Float32 y_ = 0.0f;
        Float32 z_ = 0.0f;
        Float32 w_ = 0.0f;
    };
} // namespace ve
