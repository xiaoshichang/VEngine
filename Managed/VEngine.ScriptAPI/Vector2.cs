using System;

namespace VEngine;

public readonly struct Vector2
{
    public Vector2(float x, float y)
    {
        X = x;
        Y = y;
    }

    public float X { get; }

    public float Y { get; }

    public static Vector2 Zero => new(0.0f, 0.0f);

    public override string ToString()
    {
        return FormattableString.Invariant($"({X}, {Y})");
    }
}
