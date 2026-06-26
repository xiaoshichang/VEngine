namespace VEngine.Scripting;

public enum LightType
{
    Directional = 0,
    Point = 1,
}

public sealed class Light
{
    private readonly nint nativeComponent_;

    internal Light(nint nativeComponent)
    {
        nativeComponent_ = nativeComponent;
    }

    public bool Exists => NativeApi.HasLight(nativeComponent_);

    public LightType Type
    {
        get => NativeApi.GetLightType(nativeComponent_);
        set => NativeApi.SetLightType(nativeComponent_, value);
    }

    public Vector3 Color
    {
        get => NativeApi.GetLightColor(nativeComponent_);
        set => NativeApi.SetLightColor(nativeComponent_, value);
    }

    public float Intensity
    {
        get => NativeApi.GetLightIntensity(nativeComponent_);
        set => NativeApi.SetLightIntensity(nativeComponent_, value);
    }

    public float Range
    {
        get => NativeApi.GetLightRange(nativeComponent_);
        set => NativeApi.SetLightRange(nativeComponent_, value);
    }

    public bool CastShadows
    {
        get => NativeApi.GetLightCastShadows(nativeComponent_);
        set => NativeApi.SetLightCastShadows(nativeComponent_, value);
    }
}
