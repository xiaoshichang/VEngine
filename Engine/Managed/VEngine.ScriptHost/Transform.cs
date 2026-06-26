namespace VEngine.Scripting;

public sealed class Transform
{
    private readonly nint nativeComponent_;

    internal Transform(nint nativeComponent)
    {
        nativeComponent_ = nativeComponent;
    }

    public Vector3 LocalPosition
    {
        get => NativeApi.GetTransformLocalPosition(nativeComponent_);
        set => NativeApi.SetTransformLocalPosition(nativeComponent_, value);
    }
}
