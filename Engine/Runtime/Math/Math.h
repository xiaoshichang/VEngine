#pragma once

#include "Engine/Runtime/Core/Types.h"

#include <algorithm>
#include <cmath>

namespace ve
{
    /// Small scalar helpers shared by the runtime math primitives.
    ///
    /// Usage:
    /// ```cpp
    /// const float radians = ve::ToRadians(90.0f);
    /// const bool same = ve::NearlyEqual(length, 1.0f);
    /// ```
    namespace Math
    {
        /// Single-precision value of pi used by first-stage runtime math code.
        inline constexpr Float32 Pi = 3.14159265358979323846f;

        /// Two-pi constant for angle wrapping and full-turn calculations.
        inline constexpr Float32 TwoPi = Pi * 2.0f;

        /// Half-pi constant for right-angle calculations.
        inline constexpr Float32 HalfPi = Pi * 0.5f;

        /// Default tolerance for approximate single-precision comparisons.
        inline constexpr Float32 DefaultEpsilon = 1.0e-5f;
    } // namespace Math

    /// Converts degrees to radians.
    ///
    /// Use this at API boundaries where editor or tool data is stored in degrees but runtime math expects radians.
    [[nodiscard]] constexpr Float32 ToRadians(Float32 degrees) noexcept
    {
        return degrees * (Math::Pi / 180.0f);
    }

    /// Converts radians to degrees.
    ///
    /// This is intended for diagnostics, editor display, and tests; core math APIs use radians.
    [[nodiscard]] constexpr Float32 ToDegrees(Float32 radians) noexcept
    {
        return radians * (180.0f / Math::Pi);
    }

    /// Clamps a scalar value to the inclusive range [`minimum`, `maximum`].
    ///
    /// The caller is expected to pass `minimum <= maximum`; this helper mirrors `std::clamp` while keeping engine
    /// naming.
    [[nodiscard]] constexpr Float32 Clamp(Float32 value, Float32 minimum, Float32 maximum) noexcept
    {
        return value < minimum ? minimum : (value > maximum ? maximum : value);
    }

    /// Linearly interpolates between `start` and `end`.
    ///
    /// `t` is not clamped so callers can use this for extrapolation as well as interpolation.
    [[nodiscard]] constexpr Float32 Lerp(Float32 start, Float32 end, Float32 t) noexcept
    {
        return start + ((end - start) * t);
    }

    /// Returns true when two scalar values are within an absolute tolerance.
    ///
    /// Use this for tests and editor-facing comparisons; avoid it for gameplay thresholds that need explicit domain
    /// rules.
    [[nodiscard]] inline bool NearlyEqual(Float32 left, Float32 right, Float32 epsilon = Math::DefaultEpsilon) noexcept
    {
        return std::abs(left - right) <= epsilon;
    }

    /// Returns the square root of a non-negative scalar.
    ///
    /// This wrapper keeps math code on the VEngine naming style and can become a platform optimization point later.
    [[nodiscard]] inline Float32 Sqrt(Float32 value) noexcept
    {
        return std::sqrt(value);
    }

    /// Returns the sine of a radian angle.
    ///
    /// Angles in the math module are radians unless a function name explicitly says otherwise.
    [[nodiscard]] inline Float32 Sin(Float32 radians) noexcept
    {
        return std::sin(radians);
    }

    /// Returns the cosine of a radian angle.
    ///
    /// Angles in the math module are radians unless a function name explicitly says otherwise.
    [[nodiscard]] inline Float32 Cos(Float32 radians) noexcept
    {
        return std::cos(radians);
    }
} // namespace ve
