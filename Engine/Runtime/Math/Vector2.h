#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Math.h"

namespace ve
{
    /// Two-component floating point vector for 2D positions, sizes, UVs, and screen-space math.
    ///
    /// `Vector2` is a small value type: pass it by value, store it directly in components, and avoid heap allocation
    /// for individual vectors. If many objects containing vectors need stable allocation, allocate those owning objects
    /// through an allocator such as `PoolAllocator`.
    class Vector2
    {
    public:
        /// Creates the zero vector `(0, 0)`.
        constexpr Vector2() noexcept = default;

        /// Creates a vector from explicit component values.
        ///
        /// Usage: `Vector2 uv(0.5f, 1.0f);`
        constexpr Vector2(Float32 x, Float32 y) noexcept
            : x_(x)
            , y_(y)
        {
        }

        /// Returns the zero vector `(0, 0)`.
        [[nodiscard]] static constexpr Vector2 Zero() noexcept
        {
            return Vector2(0.0f, 0.0f);
        }

        /// Returns the one vector `(1, 1)`.
        [[nodiscard]] static constexpr Vector2 One() noexcept
        {
            return Vector2(1.0f, 1.0f);
        }

        /// Returns the unit X vector `(1, 0)`.
        [[nodiscard]] static constexpr Vector2 UnitX() noexcept
        {
            return Vector2(1.0f, 0.0f);
        }

        /// Returns the unit Y vector `(0, 1)`.
        [[nodiscard]] static constexpr Vector2 UnitY() noexcept
        {
            return Vector2(0.0f, 1.0f);
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
        [[nodiscard]] Vector2 Normalized(Float32 epsilon = Math::DefaultEpsilon) const noexcept
        {
            const Float32 length = Length();
            return length > epsilon ? (*this / length) : Vector2::Zero();
        }

        /// Returns true when every component is within `epsilon` of `other`.
        [[nodiscard]] bool IsNearlyEqual(const Vector2& other, Float32 epsilon = Math::DefaultEpsilon) const noexcept
        {
            return NearlyEqual(x_, other.x_, epsilon) && NearlyEqual(y_, other.y_, epsilon);
        }

        /// Returns the dot product of two vectors.
        [[nodiscard]] static constexpr Float32 Dot(const Vector2& left, const Vector2& right) noexcept
        {
            return (left.x_ * right.x_) + (left.y_ * right.y_);
        }

        /// Returns the component-wise sum.
        [[nodiscard]] constexpr Vector2 operator+(const Vector2& right) const noexcept
        {
            return Vector2(x_ + right.x_, y_ + right.y_);
        }

        /// Returns the component-wise difference.
        [[nodiscard]] constexpr Vector2 operator-(const Vector2& right) const noexcept
        {
            return Vector2(x_ - right.x_, y_ - right.y_);
        }

        /// Returns this vector with every component negated.
        [[nodiscard]] constexpr Vector2 operator-() const noexcept
        {
            return Vector2(-x_, -y_);
        }

        /// Returns this vector multiplied by a scalar.
        [[nodiscard]] constexpr Vector2 operator*(Float32 scalar) const noexcept
        {
            return Vector2(x_ * scalar, y_ * scalar);
        }

        /// Returns this vector divided by a scalar.
        ///
        /// The caller is responsible for avoiding division by zero.
        [[nodiscard]] constexpr Vector2 operator/(Float32 scalar) const noexcept
        {
            return Vector2(x_ / scalar, y_ / scalar);
        }

        /// Adds another vector into this one.
        constexpr Vector2& operator+=(const Vector2& right) noexcept
        {
            x_ += right.x_;
            y_ += right.y_;
            return *this;
        }

        /// Subtracts another vector from this one.
        constexpr Vector2& operator-=(const Vector2& right) noexcept
        {
            x_ -= right.x_;
            y_ -= right.y_;
            return *this;
        }

        /// Multiplies this vector by a scalar in place.
        constexpr Vector2& operator*=(Float32 scalar) noexcept
        {
            x_ *= scalar;
            y_ *= scalar;
            return *this;
        }

        /// Divides this vector by a scalar in place.
        ///
        /// The caller is responsible for avoiding division by zero.
        constexpr Vector2& operator/=(Float32 scalar) noexcept
        {
            x_ /= scalar;
            y_ /= scalar;
            return *this;
        }

        /// Returns true when all components are exactly equal.
        ///
        /// Prefer `IsNearlyEqual` for values that have gone through floating point arithmetic.
        [[nodiscard]] friend constexpr bool operator==(const Vector2& left, const Vector2& right) noexcept
        {
            return left.x_ == right.x_ && left.y_ == right.y_;
        }

        /// Returns true when any component differs exactly.
        [[nodiscard]] friend constexpr bool operator!=(const Vector2& left, const Vector2& right) noexcept
        {
            return !(left == right);
        }

        /// Returns a vector multiplied by a scalar written on the left side.
        [[nodiscard]] friend constexpr Vector2 operator*(Float32 scalar, const Vector2& value) noexcept
        {
            return value * scalar;
        }

    private:
        Float32 x_ = 0.0f;
        Float32 y_ = 0.0f;
    };
} // namespace ve
