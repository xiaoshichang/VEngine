#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Vector3.h"

namespace ve
{
    /// Unit quaternion used to represent 3D orientation and rotation composition.
    ///
    /// Store gameplay rotations as normalized quaternions to avoid Euler angle drift. Angles are expressed in radians.
    class Quaternion
    {
    public:
        /// Creates the identity rotation.
        constexpr Quaternion() noexcept = default;

        /// Creates a quaternion from explicit component values.
        ///
        /// Use this for deserialization or low-level math. Prefer `FromAxisAngle` for gameplay-facing construction.
        constexpr Quaternion(Float32 x, Float32 y, Float32 z, Float32 w) noexcept
            : x_(x)
            , y_(y)
            , z_(z)
            , w_(w)
        {
        }

        /// Returns the identity rotation.
        [[nodiscard]] static constexpr Quaternion Identity() noexcept
        {
            return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
        }

        /// Builds a rotation around `axis` by `radians`.
        ///
        /// The axis is normalized internally. A near-zero axis returns identity so callers can safely pass optional
        /// axes.
        [[nodiscard]] static Quaternion FromAxisAngle(const Vector3& axis, Float32 radians) noexcept
        {
            const Vector3 normalizedAxis = axis.Normalized();
            if (normalizedAxis == Vector3::Zero())
            {
                return Quaternion::Identity();
            }

            const Float32 halfAngle = radians * 0.5f;
            const Float32 s = Sin(halfAngle);
            return Quaternion(normalizedAxis.GetX() * s, normalizedAxis.GetY() * s, normalizedAxis.GetZ() * s, Cos(halfAngle)).Normalized();
        }

        /// Builds a quaternion from Euler angles applied as X, then Y, then Z rotations.
        ///
        /// Use this at import or editor boundaries. Runtime systems should prefer storing quaternions directly.
        [[nodiscard]] static Quaternion FromEulerXYZ(Float32 xRadians, Float32 yRadians, Float32 zRadians) noexcept
        {
            const Quaternion x = FromAxisAngle(Vector3::UnitX(), xRadians);
            const Quaternion y = FromAxisAngle(Vector3::UnitY(), yRadians);
            const Quaternion z = FromAxisAngle(Vector3::UnitZ(), zRadians);
            return (z * y * x).Normalized();
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

        /// Returns the squared quaternion length.
        [[nodiscard]] constexpr Float32 LengthSquared() const noexcept
        {
            return (x_ * x_) + (y_ * y_) + (z_ * z_) + (w_ * w_);
        }

        /// Returns the quaternion length.
        [[nodiscard]] Float32 Length() const noexcept
        {
            return Sqrt(LengthSquared());
        }

        /// Returns a normalized copy, or identity when the quaternion is too small to normalize.
        [[nodiscard]] Quaternion Normalized(Float32 epsilon = Math::DefaultEpsilon) const noexcept
        {
            const Float32 length = Length();
            return length > epsilon ? Quaternion(x_ / length, y_ / length, z_ / length, w_ / length) : Quaternion::Identity();
        }

        /// Returns the conjugate rotation.
        ///
        /// For normalized quaternions this is also the inverse rotation.
        [[nodiscard]] constexpr Quaternion Conjugated() const noexcept
        {
            return Quaternion(-x_, -y_, -z_, w_);
        }

        /// Converts this quaternion to a 4x4 rotation matrix.
        ///
        /// The result follows `Matrix44`'s row-major storage and column-vector multiplication convention.
        [[nodiscard]] Matrix44 ToMatrix44() const noexcept
        {
            const Quaternion q = Normalized();
            const Float32 xx = q.x_ * q.x_;
            const Float32 yy = q.y_ * q.y_;
            const Float32 zz = q.z_ * q.z_;
            const Float32 xy = q.x_ * q.y_;
            const Float32 xz = q.x_ * q.z_;
            const Float32 yz = q.y_ * q.z_;
            const Float32 wx = q.w_ * q.x_;
            const Float32 wy = q.w_ * q.y_;
            const Float32 wz = q.w_ * q.z_;

            return Matrix44(std::array<Float32, 16>{
                1.0f - (2.0f * (yy + zz)),
                2.0f * (xy - wz),
                2.0f * (xz + wy),
                0.0f,
                2.0f * (xy + wz),
                1.0f - (2.0f * (xx + zz)),
                2.0f * (yz - wx),
                0.0f,
                2.0f * (xz - wy),
                2.0f * (yz + wx),
                1.0f - (2.0f * (xx + yy)),
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            });
        }

        /// Rotates a direction vector by this quaternion.
        ///
        /// Translation is not involved; use `Matrix44` for full point transforms.
        [[nodiscard]] Vector3 RotateVector(const Vector3& vector) const noexcept
        {
            return ToMatrix44().TransformDirection(vector);
        }

        /// Returns true when every component is within `epsilon` of `other`.
        [[nodiscard]] bool IsNearlyEqual(const Quaternion& other, Float32 epsilon = Math::DefaultEpsilon) const noexcept
        {
            return NearlyEqual(x_, other.x_, epsilon) && NearlyEqual(y_, other.y_, epsilon) && NearlyEqual(z_, other.z_, epsilon) &&
                   NearlyEqual(w_, other.w_, epsilon);
        }

        /// Returns the composed rotation.
        ///
        /// `parent * child` applies `child` first and then `parent`, matching `Matrix44` multiplication order.
        [[nodiscard]] constexpr Quaternion operator*(const Quaternion& right) const noexcept
        {
            return Quaternion((w_ * right.x_) + (x_ * right.w_) + (y_ * right.z_) - (z_ * right.y_),
                              (w_ * right.y_) - (x_ * right.z_) + (y_ * right.w_) + (z_ * right.x_),
                              (w_ * right.z_) + (x_ * right.y_) - (y_ * right.x_) + (z_ * right.w_),
                              (w_ * right.w_) - (x_ * right.x_) - (y_ * right.y_) - (z_ * right.z_));
        }

    private:
        Float32 x_ = 0.0f;
        Float32 y_ = 0.0f;
        Float32 z_ = 0.0f;
        Float32 w_ = 1.0f;
    };
} // namespace ve
