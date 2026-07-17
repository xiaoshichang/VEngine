namespace VEngine.Scripting;

public enum CameraProjectionMode
{
    Perspective = 0,
    Orthographic = 1,
}

public sealed class Camera
{
    private readonly nint nativeComponent_;

    internal Camera(nint nativeComponent)
    {
        nativeComponent_ = nativeComponent;
    }

    public bool Exists => NativeApi.HasCamera(nativeComponent_);

    public CameraProjectionMode ProjectionMode
    {
        get => NativeApi.GetCameraProjectionMode(nativeComponent_);
        set => NativeApi.SetCameraProjectionMode(nativeComponent_, value);
    }

    public float VerticalFieldOfViewRadians
    {
        get => NativeApi.GetCameraVerticalFieldOfViewRadians(nativeComponent_);
        set => NativeApi.SetCameraVerticalFieldOfViewRadians(nativeComponent_, value);
    }

    public float OrthographicSize
    {
        get => NativeApi.GetCameraOrthographicSize(nativeComponent_);
        set => NativeApi.SetCameraOrthographicSize(nativeComponent_, value);
    }

    public float AspectRatio
    {
        get => NativeApi.GetCameraAspectRatio(nativeComponent_);
        set => NativeApi.SetCameraAspectRatio(nativeComponent_, value);
    }

    public bool IsAspectRatioAutomatic => NativeApi.IsCameraAspectRatioAutomatic(nativeComponent_);

    public void ResetAspectRatio()
    {
        NativeApi.ResetCameraAspectRatio(nativeComponent_);
    }

    public float NearClipPlane
    {
        get => NativeApi.GetCameraNearClipPlane(nativeComponent_);
        set => NativeApi.SetCameraNearClipPlane(nativeComponent_, value);
    }

    public float FarClipPlane
    {
        get => NativeApi.GetCameraFarClipPlane(nativeComponent_);
        set => NativeApi.SetCameraFarClipPlane(nativeComponent_, value);
    }

    public Color ClearColor
    {
        get => NativeApi.GetCameraClearColor(nativeComponent_);
        set => NativeApi.SetCameraClearColor(nativeComponent_, value);
    }
}
