#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Math/Vector4.h"

#include <array>

namespace ve
{
/// Four-by-four floating point matrix for affine transforms and clip-space projection data.
///
/// Matrices are stored in row-major order and are used with column-vector multiplication through `matrix * vector`.
/// Translation lives in the last column, so `Matrix44::Translation(Vector3(1, 2, 3)).TransformPoint(Vector3::Zero())`
/// returns `(1, 2, 3)`.
class Matrix44
{
public:
    /// Creates an identity matrix.
    constexpr Matrix44() noexcept
        : values_{
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        }
    {
    }

    /// Creates a matrix from row-major values.
    ///
    /// Usage:
    /// ```cpp
    /// Matrix44 m({
    ///     1, 0, 0, 0,
    ///     0, 1, 0, 0,
    ///     0, 0, 1, 0,
    ///     0, 0, 0, 1,
    /// });
    /// ```
    explicit constexpr Matrix44(const std::array<Float32, 16>& rowMajorValues) noexcept
        : values_(rowMajorValues)
    {
    }

    /// Returns an identity matrix.
    [[nodiscard]] static constexpr Matrix44 Identity() noexcept
    {
        return Matrix44();
    }

    /// Returns a matrix where every element is zero.
    [[nodiscard]] static constexpr Matrix44 Zero() noexcept
    {
        return Matrix44(std::array<Float32, 16>{
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
        });
    }

    /// Builds a translation matrix.
    ///
    /// Use this for local-to-parent transforms where the position is expressed in parent space.
    [[nodiscard]] static constexpr Matrix44 Translation(const Vector3& translation) noexcept
    {
        Matrix44 matrix;
        matrix.Set(0, 3, translation.GetX());
        matrix.Set(1, 3, translation.GetY());
        matrix.Set(2, 3, translation.GetZ());
        return matrix;
    }

    /// Builds a non-uniform scale matrix.
    ///
    /// Scale values are applied along the local X, Y, and Z axes.
    [[nodiscard]] static constexpr Matrix44 Scale(const Vector3& scale) noexcept
    {
        Matrix44 matrix = Matrix44::Zero();
        matrix.Set(0, 0, scale.GetX());
        matrix.Set(1, 1, scale.GetY());
        matrix.Set(2, 2, scale.GetZ());
        matrix.Set(3, 3, 1.0f);
        return matrix;
    }

    /// Builds a rotation matrix around the X axis.
    ///
    /// `radians` follows the standard right-handed positive-angle convention around the axis.
    [[nodiscard]] static Matrix44 RotationX(Float32 radians) noexcept
    {
        const Float32 c = Cos(radians);
        const Float32 s = Sin(radians);
        Matrix44 matrix;
        matrix.Set(1, 1, c);
        matrix.Set(1, 2, -s);
        matrix.Set(2, 1, s);
        matrix.Set(2, 2, c);
        return matrix;
    }

    /// Builds a rotation matrix around the Y axis.
    ///
    /// `radians` follows the standard right-handed positive-angle convention around the axis.
    [[nodiscard]] static Matrix44 RotationY(Float32 radians) noexcept
    {
        const Float32 c = Cos(radians);
        const Float32 s = Sin(radians);
        Matrix44 matrix;
        matrix.Set(0, 0, c);
        matrix.Set(0, 2, s);
        matrix.Set(2, 0, -s);
        matrix.Set(2, 2, c);
        return matrix;
    }

    /// Builds a rotation matrix around the Z axis.
    ///
    /// `radians` follows the standard right-handed positive-angle convention around the axis.
    [[nodiscard]] static Matrix44 RotationZ(Float32 radians) noexcept
    {
        const Float32 c = Cos(radians);
        const Float32 s = Sin(radians);
        Matrix44 matrix;
        matrix.Set(0, 0, c);
        matrix.Set(0, 1, -s);
        matrix.Set(1, 0, s);
        matrix.Set(1, 1, c);
        return matrix;
    }

    /// Returns a read-only view of the row-major storage.
    ///
    /// Use this when copying matrix data into engine-owned serialization or rendering buffers.
    [[nodiscard]] constexpr const std::array<Float32, 16>& GetValues() const noexcept
    {
        return values_;
    }

    /// Returns the matrix element at `row`, `column`.
    ///
    /// Valid indices are 0 through 3; this function does not bounds-check in release builds.
    [[nodiscard]] constexpr Float32 Get(SizeT row, SizeT column) const noexcept
    {
        return values_[(row * 4) + column];
    }

    /// Sets the matrix element at `row`, `column`.
    ///
    /// Valid indices are 0 through 3; this function does not bounds-check in release builds.
    constexpr void Set(SizeT row, SizeT column, Float32 value) noexcept
    {
        values_[(row * 4) + column] = value;
    }

    /// Transforms a homogeneous vector by this matrix.
    ///
    /// Use `Vector4(point, 1)` for points and `Vector4(direction, 0)` for directions.
    [[nodiscard]] constexpr Vector4 TransformVector(const Vector4& vector) const noexcept
    {
        return Vector4(
            (Get(0, 0) * vector.GetX()) + (Get(0, 1) * vector.GetY()) + (Get(0, 2) * vector.GetZ())
                + (Get(0, 3) * vector.GetW()),
            (Get(1, 0) * vector.GetX()) + (Get(1, 1) * vector.GetY()) + (Get(1, 2) * vector.GetZ())
                + (Get(1, 3) * vector.GetW()),
            (Get(2, 0) * vector.GetX()) + (Get(2, 1) * vector.GetY()) + (Get(2, 2) * vector.GetZ())
                + (Get(2, 3) * vector.GetW()),
            (Get(3, 0) * vector.GetX()) + (Get(3, 1) * vector.GetY()) + (Get(3, 2) * vector.GetZ())
                + (Get(3, 3) * vector.GetW()));
    }

    /// Transforms a 3D point by this matrix using homogeneous `w = 1`.
    ///
    /// If the transformed W is neither zero nor one, the result is perspective-divided.
    [[nodiscard]] Vector3 TransformPoint(const Vector3& point) const noexcept
    {
        const Vector4 transformed = TransformVector(Vector4(point, 1.0f));
        if (!NearlyEqual(transformed.GetW(), 0.0f) && !NearlyEqual(transformed.GetW(), 1.0f))
        {
            return transformed.GetXYZ() / transformed.GetW();
        }

        return transformed.GetXYZ();
    }

    /// Transforms a 3D direction by this matrix using homogeneous `w = 0`.
    ///
    /// Translation does not affect directions.
    [[nodiscard]] constexpr Vector3 TransformDirection(const Vector3& direction) const noexcept
    {
        return TransformVector(Vector4(direction, 0.0f)).GetXYZ();
    }

    /// Returns the transpose of this matrix.
    ///
    /// Use this when an API or shader path expects the opposite matrix storage convention.
    [[nodiscard]] constexpr Matrix44 Transposed() const noexcept
    {
        Matrix44 result = Matrix44::Zero();
        for (SizeT row = 0; row < 4; ++row)
        {
            for (SizeT column = 0; column < 4; ++column)
            {
                result.Set(row, column, Get(column, row));
            }
        }

        return result;
    }

    /// Returns true when every element is within `epsilon` of `other`.
    [[nodiscard]] bool IsNearlyEqual(const Matrix44& other, Float32 epsilon = Math::DefaultEpsilon) const noexcept
    {
        for (SizeT index = 0; index < values_.size(); ++index)
        {
            if (!NearlyEqual(values_[index], other.values_[index], epsilon))
            {
                return false;
            }
        }

        return true;
    }

    /// Returns the product of this matrix and `right`.
    ///
    /// With the engine's column-vector convention, `parent * child` applies `child` first and then `parent`.
    [[nodiscard]] constexpr Matrix44 operator*(const Matrix44& right) const noexcept
    {
        Matrix44 result = Matrix44::Zero();
        for (SizeT row = 0; row < 4; ++row)
        {
            for (SizeT column = 0; column < 4; ++column)
            {
                Float32 value = 0.0f;
                for (SizeT inner = 0; inner < 4; ++inner)
                {
                    value += Get(row, inner) * right.Get(inner, column);
                }
                result.Set(row, column, value);
            }
        }

        return result;
    }

    /// Returns the transformed homogeneous vector.
    [[nodiscard]] constexpr Vector4 operator*(const Vector4& vector) const noexcept
    {
        return TransformVector(vector);
    }

private:
    std::array<Float32, 16> values_;
};
}
