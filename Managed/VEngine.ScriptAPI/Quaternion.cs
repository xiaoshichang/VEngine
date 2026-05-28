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

    public override string ToString()
    {
        return FormattableString.Invariant($"({X}, {Y}, {Z}, {W})");
    }
}
