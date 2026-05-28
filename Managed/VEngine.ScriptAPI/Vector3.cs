using System;

namespace VEngine;

public readonly struct Vector3
{
    public Vector3(float x, float y, float z)
    {
        X = x;
        Y = y;
        Z = z;
    }

    public float X { get; }

    public float Y { get; }

    public float Z { get; }

    public static Vector3 Zero => new(0.0f, 0.0f, 0.0f);

    public static Vector3 One => new(1.0f, 1.0f, 1.0f);

    public override string ToString()
    {
        return FormattableString.Invariant($"({X}, {Y}, {Z})");
    }
}
