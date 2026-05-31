using VEngine;

namespace VEngine.Scripting;

public sealed class Camera
{
    private readonly ulong _nativeHandle;

    internal Camera(ulong nativeHandle)
    {
        _nativeHandle = nativeHandle;
    }

    public static Camera? Main => ScriptBridge.GetMainCamera();

    public Ray ScreenPointToRay(Vector2 screenPoint)
    {
        return ScriptBridge.ScreenPointToRay(_nativeHandle, screenPoint);
    }

    public bool IsValid => _nativeHandle != 0;
}
