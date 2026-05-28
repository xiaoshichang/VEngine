using System;

namespace VEngine;

public readonly struct Quaternion
{
    public Quaternion(float x, float y, float z, float w)
    {
        X = x;
        Y = y;
        Z = z;
        W = w;
    }

    public float X { get; }

    public float Y { get; }

    public float Z { get; }

    public float W { get; }

    public static Quaternion Identity => new(0.0f, 0.0f, 0.0f, 1.0f);

    public static Quaternion FromEulerXYZ(float xRadians, float yRadians, float zRadians)
    {
        float halfX = xRadians * 0.5f;
        float halfY = yRadians * 0.5f;
        float halfZ = zRadians * 0.5f;

        float sinX = MathF.Sin(halfX);
        float cosX = MathF.Cos(halfX);
        float sinY = MathF.Sin(halfY);
        float cosY = MathF.Cos(halfY);
        float sinZ = MathF.Sin(halfZ);
        float cosZ = MathF.Cos(halfZ);

        return new Quaternion(
            (sinX * cosY * cosZ) + (cosX * sinY * sinZ),
            (cosX * sinY * cosZ) - (sinX * cosY * sinZ),
            (cosX * cosY * sinZ) + (sinX * sinY * cosZ),
            (cosX * cosY * cosZ) - (sinX * sinY * sinZ));
    }

    public override string ToString()
    {
        return FormattableString.Invariant($"({X}, {Y}, {Z}, {W})");
    }
}
