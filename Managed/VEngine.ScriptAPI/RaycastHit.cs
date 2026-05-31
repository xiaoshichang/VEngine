using VEngine.Scripting;

namespace VEngine;

public readonly struct RaycastHit
{
    public RaycastHit(GameObject gameObject, float distance, Vector3 position, Vector3 normal)
    {
        GameObject = gameObject;
        Distance = distance;
        Position = position;
        Normal = normal;
    }

    public GameObject GameObject { get; }

    public float Distance { get; }

    public Vector3 Position { get; }

    public Vector3 Normal { get; }
}
