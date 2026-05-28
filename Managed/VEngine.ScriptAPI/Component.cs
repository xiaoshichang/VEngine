namespace VEngine.Scripting;

public abstract class Component
{
    private ulong _nativeHandle;

    internal Component()
    {
    }

    public GameObject GameObject => new(_nativeHandle);

    public Transform Transform => new(_nativeHandle);

    internal ulong NativeHandle => _nativeHandle;

    internal void AttachNativeHandle(ulong nativeHandle)
    {
        _nativeHandle = nativeHandle;
    }

    internal void DetachNativeHandle()
    {
        _nativeHandle = 0;
    }
}
