using VEngine;

namespace VEngine.Scripting;

public sealed class Transform
{
    private readonly ulong _nativeHandle;

    internal Transform(ulong nativeHandle)
    {
        _nativeHandle = nativeHandle;
    }

    public Vector3 LocalPosition
    {
        get => ScriptBridge.GetLocalPosition(_nativeHandle);
        set => ScriptBridge.SetLocalPosition(_nativeHandle, value);
    }

    public Quaternion LocalRotation
    {
        get => ScriptBridge.GetLocalRotation(_nativeHandle);
        set => ScriptBridge.SetLocalRotation(_nativeHandle, value);
    }

    public Vector3 LocalScale
    {
        get => ScriptBridge.GetLocalScale(_nativeHandle);
        set => ScriptBridge.SetLocalScale(_nativeHandle, value);
    }

    public bool IsValid => _nativeHandle != 0;
}
