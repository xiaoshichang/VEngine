namespace VEngine.Scripting;

public sealed class GameObject
{
    private readonly ulong _nativeHandle;

    internal GameObject(ulong nativeHandle)
    {
        _nativeHandle = nativeHandle;
    }

    public string Name
    {
        get => ScriptBridge.GetGameObjectName(_nativeHandle);
        set => ScriptBridge.SetGameObjectName(_nativeHandle, value);
    }

    public bool IsValid => _nativeHandle != 0;
}
