using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace VEngine.Scripting;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeScriptApi
{
    public delegate* unmanaged[Cdecl]<nint, float*, float*, float*, void> GetTransformLocalPosition;
    public delegate* unmanaged[Cdecl]<nint, float, float, float, void> SetTransformLocalPosition;
    public delegate* unmanaged[Cdecl]<nint, int> HasCamera;
    public delegate* unmanaged[Cdecl]<nint, int> GetCameraIsPrimary;
    public delegate* unmanaged[Cdecl]<nint, int, void> SetCameraIsPrimary;
    public delegate* unmanaged[Cdecl]<nint, int> GetCameraProjectionMode;
    public delegate* unmanaged[Cdecl]<nint, int, void> SetCameraProjectionMode;
    public delegate* unmanaged[Cdecl]<nint, float> GetCameraVerticalFieldOfViewRadians;
    public delegate* unmanaged[Cdecl]<nint, float, void> SetCameraVerticalFieldOfViewRadians;
    public delegate* unmanaged[Cdecl]<nint, float> GetCameraOrthographicSize;
    public delegate* unmanaged[Cdecl]<nint, float, void> SetCameraOrthographicSize;
    public delegate* unmanaged[Cdecl]<nint, float> GetCameraAspectRatio;
    public delegate* unmanaged[Cdecl]<nint, float, void> SetCameraAspectRatio;
    public delegate* unmanaged[Cdecl]<nint, float> GetCameraNearClipPlane;
    public delegate* unmanaged[Cdecl]<nint, float, void> SetCameraNearClipPlane;
    public delegate* unmanaged[Cdecl]<nint, float> GetCameraFarClipPlane;
    public delegate* unmanaged[Cdecl]<nint, float, void> SetCameraFarClipPlane;
    public delegate* unmanaged[Cdecl]<nint, float*, float*, float*, float*, void> GetCameraClearColor;
    public delegate* unmanaged[Cdecl]<nint, float, float, float, float, void> SetCameraClearColor;
    public delegate* unmanaged[Cdecl]<nint, int> HasLight;
    public delegate* unmanaged[Cdecl]<nint, int> GetLightType;
    public delegate* unmanaged[Cdecl]<nint, int, void> SetLightType;
    public delegate* unmanaged[Cdecl]<nint, float*, float*, float*, void> GetLightColor;
    public delegate* unmanaged[Cdecl]<nint, float, float, float, void> SetLightColor;
    public delegate* unmanaged[Cdecl]<nint, float> GetLightIntensity;
    public delegate* unmanaged[Cdecl]<nint, float, void> SetLightIntensity;
    public delegate* unmanaged[Cdecl]<nint, float> GetLightRange;
    public delegate* unmanaged[Cdecl]<nint, float, void> SetLightRange;
    public delegate* unmanaged[Cdecl]<nint, int> GetLightCastShadows;
    public delegate* unmanaged[Cdecl]<nint, int, void> SetLightCastShadows;
    public delegate* unmanaged[Cdecl]<byte*, void> LogInfo;
}

internal static unsafe class NativeApi
{
    private static delegate* unmanaged[Cdecl]<nint, float*, float*, float*, void> getTransformLocalPosition_;
    private static delegate* unmanaged[Cdecl]<nint, float, float, float, void> setTransformLocalPosition_;
    private static delegate* unmanaged[Cdecl]<nint, int> hasCamera_;
    private static delegate* unmanaged[Cdecl]<nint, int> getCameraIsPrimary_;
    private static delegate* unmanaged[Cdecl]<nint, int, void> setCameraIsPrimary_;
    private static delegate* unmanaged[Cdecl]<nint, int> getCameraProjectionMode_;
    private static delegate* unmanaged[Cdecl]<nint, int, void> setCameraProjectionMode_;
    private static delegate* unmanaged[Cdecl]<nint, float> getCameraVerticalFieldOfViewRadians_;
    private static delegate* unmanaged[Cdecl]<nint, float, void> setCameraVerticalFieldOfViewRadians_;
    private static delegate* unmanaged[Cdecl]<nint, float> getCameraOrthographicSize_;
    private static delegate* unmanaged[Cdecl]<nint, float, void> setCameraOrthographicSize_;
    private static delegate* unmanaged[Cdecl]<nint, float> getCameraAspectRatio_;
    private static delegate* unmanaged[Cdecl]<nint, float, void> setCameraAspectRatio_;
    private static delegate* unmanaged[Cdecl]<nint, float> getCameraNearClipPlane_;
    private static delegate* unmanaged[Cdecl]<nint, float, void> setCameraNearClipPlane_;
    private static delegate* unmanaged[Cdecl]<nint, float> getCameraFarClipPlane_;
    private static delegate* unmanaged[Cdecl]<nint, float, void> setCameraFarClipPlane_;
    private static delegate* unmanaged[Cdecl]<nint, float*, float*, float*, float*, void> getCameraClearColor_;
    private static delegate* unmanaged[Cdecl]<nint, float, float, float, float, void> setCameraClearColor_;
    private static delegate* unmanaged[Cdecl]<nint, int> hasLight_;
    private static delegate* unmanaged[Cdecl]<nint, int> getLightType_;
    private static delegate* unmanaged[Cdecl]<nint, int, void> setLightType_;
    private static delegate* unmanaged[Cdecl]<nint, float*, float*, float*, void> getLightColor_;
    private static delegate* unmanaged[Cdecl]<nint, float, float, float, void> setLightColor_;
    private static delegate* unmanaged[Cdecl]<nint, float> getLightIntensity_;
    private static delegate* unmanaged[Cdecl]<nint, float, void> setLightIntensity_;
    private static delegate* unmanaged[Cdecl]<nint, float> getLightRange_;
    private static delegate* unmanaged[Cdecl]<nint, float, void> setLightRange_;
    private static delegate* unmanaged[Cdecl]<nint, int> getLightCastShadows_;
    private static delegate* unmanaged[Cdecl]<nint, int, void> setLightCastShadows_;
    private static delegate* unmanaged[Cdecl]<byte*, void> logInfo_;

    public static void Register(NativeScriptApi* nativeApi)
    {
        if (nativeApi == null)
        {
            return;
        }

        getTransformLocalPosition_ = nativeApi->GetTransformLocalPosition;
        setTransformLocalPosition_ = nativeApi->SetTransformLocalPosition;
        hasCamera_ = nativeApi->HasCamera;
        getCameraIsPrimary_ = nativeApi->GetCameraIsPrimary;
        setCameraIsPrimary_ = nativeApi->SetCameraIsPrimary;
        getCameraProjectionMode_ = nativeApi->GetCameraProjectionMode;
        setCameraProjectionMode_ = nativeApi->SetCameraProjectionMode;
        getCameraVerticalFieldOfViewRadians_ = nativeApi->GetCameraVerticalFieldOfViewRadians;
        setCameraVerticalFieldOfViewRadians_ = nativeApi->SetCameraVerticalFieldOfViewRadians;
        getCameraOrthographicSize_ = nativeApi->GetCameraOrthographicSize;
        setCameraOrthographicSize_ = nativeApi->SetCameraOrthographicSize;
        getCameraAspectRatio_ = nativeApi->GetCameraAspectRatio;
        setCameraAspectRatio_ = nativeApi->SetCameraAspectRatio;
        getCameraNearClipPlane_ = nativeApi->GetCameraNearClipPlane;
        setCameraNearClipPlane_ = nativeApi->SetCameraNearClipPlane;
        getCameraFarClipPlane_ = nativeApi->GetCameraFarClipPlane;
        setCameraFarClipPlane_ = nativeApi->SetCameraFarClipPlane;
        getCameraClearColor_ = nativeApi->GetCameraClearColor;
        setCameraClearColor_ = nativeApi->SetCameraClearColor;
        hasLight_ = nativeApi->HasLight;
        getLightType_ = nativeApi->GetLightType;
        setLightType_ = nativeApi->SetLightType;
        getLightColor_ = nativeApi->GetLightColor;
        setLightColor_ = nativeApi->SetLightColor;
        getLightIntensity_ = nativeApi->GetLightIntensity;
        setLightIntensity_ = nativeApi->SetLightIntensity;
        getLightRange_ = nativeApi->GetLightRange;
        setLightRange_ = nativeApi->SetLightRange;
        getLightCastShadows_ = nativeApi->GetLightCastShadows;
        setLightCastShadows_ = nativeApi->SetLightCastShadows;
        logInfo_ = nativeApi->LogInfo;
    }

    public static Vector3 GetTransformLocalPosition(nint nativeComponent)
    {
        if (nativeComponent == 0 || getTransformLocalPosition_ == null)
        {
            return new Vector3();
        }

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        getTransformLocalPosition_(nativeComponent, &x, &y, &z);
        return new Vector3(x, y, z);
    }

    public static void SetTransformLocalPosition(nint nativeComponent, Vector3 position)
    {
        if (nativeComponent == 0 || setTransformLocalPosition_ == null)
        {
            return;
        }

        setTransformLocalPosition_(nativeComponent, position.X, position.Y, position.Z);
    }

    public static bool HasCamera(nint nativeComponent)
    {
        return nativeComponent != 0 && hasCamera_ != null && hasCamera_(nativeComponent) != 0;
    }

    public static bool GetCameraIsPrimary(nint nativeComponent)
    {
        return nativeComponent != 0 && getCameraIsPrimary_ != null && getCameraIsPrimary_(nativeComponent) != 0;
    }

    public static void SetCameraIsPrimary(nint nativeComponent, bool primary)
    {
        if (nativeComponent != 0 && setCameraIsPrimary_ != null)
        {
            setCameraIsPrimary_(nativeComponent, primary ? 1 : 0);
        }
    }

    public static CameraProjectionMode GetCameraProjectionMode(nint nativeComponent)
    {
        return nativeComponent != 0 && getCameraProjectionMode_ != null ? (CameraProjectionMode)getCameraProjectionMode_(nativeComponent) : CameraProjectionMode.Perspective;
    }

    public static void SetCameraProjectionMode(nint nativeComponent, CameraProjectionMode projectionMode)
    {
        if (nativeComponent != 0 && setCameraProjectionMode_ != null)
        {
            setCameraProjectionMode_(nativeComponent, (int)projectionMode);
        }
    }

    public static float GetCameraVerticalFieldOfViewRadians(nint nativeComponent)
    {
        return nativeComponent != 0 && getCameraVerticalFieldOfViewRadians_ != null ? getCameraVerticalFieldOfViewRadians_(nativeComponent) : 0.0f;
    }

    public static void SetCameraVerticalFieldOfViewRadians(nint nativeComponent, float fieldOfViewRadians)
    {
        if (nativeComponent != 0 && setCameraVerticalFieldOfViewRadians_ != null)
        {
            setCameraVerticalFieldOfViewRadians_(nativeComponent, fieldOfViewRadians);
        }
    }

    public static float GetCameraOrthographicSize(nint nativeComponent)
    {
        return nativeComponent != 0 && getCameraOrthographicSize_ != null ? getCameraOrthographicSize_(nativeComponent) : 0.0f;
    }

    public static void SetCameraOrthographicSize(nint nativeComponent, float orthographicSize)
    {
        if (nativeComponent != 0 && setCameraOrthographicSize_ != null)
        {
            setCameraOrthographicSize_(nativeComponent, orthographicSize);
        }
    }

    public static float GetCameraAspectRatio(nint nativeComponent)
    {
        return nativeComponent != 0 && getCameraAspectRatio_ != null ? getCameraAspectRatio_(nativeComponent) : 0.0f;
    }

    public static void SetCameraAspectRatio(nint nativeComponent, float aspectRatio)
    {
        if (nativeComponent != 0 && setCameraAspectRatio_ != null)
        {
            setCameraAspectRatio_(nativeComponent, aspectRatio);
        }
    }

    public static float GetCameraNearClipPlane(nint nativeComponent)
    {
        return nativeComponent != 0 && getCameraNearClipPlane_ != null ? getCameraNearClipPlane_(nativeComponent) : 0.0f;
    }

    public static void SetCameraNearClipPlane(nint nativeComponent, float nearClipPlane)
    {
        if (nativeComponent != 0 && setCameraNearClipPlane_ != null)
        {
            setCameraNearClipPlane_(nativeComponent, nearClipPlane);
        }
    }

    public static float GetCameraFarClipPlane(nint nativeComponent)
    {
        return nativeComponent != 0 && getCameraFarClipPlane_ != null ? getCameraFarClipPlane_(nativeComponent) : 0.0f;
    }

    public static void SetCameraFarClipPlane(nint nativeComponent, float farClipPlane)
    {
        if (nativeComponent != 0 && setCameraFarClipPlane_ != null)
        {
            setCameraFarClipPlane_(nativeComponent, farClipPlane);
        }
    }

    public static Color GetCameraClearColor(nint nativeComponent)
    {
        if (nativeComponent == 0 || getCameraClearColor_ == null)
        {
            return new Color(0.0f, 0.0f, 0.0f, 1.0f);
        }

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
        getCameraClearColor_(nativeComponent, &r, &g, &b, &a);
        return new Color(r, g, b, a);
    }

    public static void SetCameraClearColor(nint nativeComponent, Color color)
    {
        if (nativeComponent != 0 && setCameraClearColor_ != null)
        {
            setCameraClearColor_(nativeComponent, color.R, color.G, color.B, color.A);
        }
    }

    public static bool HasLight(nint nativeComponent)
    {
        return nativeComponent != 0 && hasLight_ != null && hasLight_(nativeComponent) != 0;
    }

    public static LightType GetLightType(nint nativeComponent)
    {
        return nativeComponent != 0 && getLightType_ != null ? (LightType)getLightType_(nativeComponent) : LightType.Directional;
    }

    public static void SetLightType(nint nativeComponent, LightType lightType)
    {
        if (nativeComponent != 0 && setLightType_ != null)
        {
            setLightType_(nativeComponent, (int)lightType);
        }
    }

    public static Vector3 GetLightColor(nint nativeComponent)
    {
        if (nativeComponent == 0 || getLightColor_ == null)
        {
            return new Vector3();
        }

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        getLightColor_(nativeComponent, &r, &g, &b);
        return new Vector3(r, g, b);
    }

    public static void SetLightColor(nint nativeComponent, Vector3 color)
    {
        if (nativeComponent != 0 && setLightColor_ != null)
        {
            setLightColor_(nativeComponent, color.X, color.Y, color.Z);
        }
    }

    public static float GetLightIntensity(nint nativeComponent)
    {
        return nativeComponent != 0 && getLightIntensity_ != null ? getLightIntensity_(nativeComponent) : 0.0f;
    }

    public static void SetLightIntensity(nint nativeComponent, float intensity)
    {
        if (nativeComponent != 0 && setLightIntensity_ != null)
        {
            setLightIntensity_(nativeComponent, intensity);
        }
    }

    public static float GetLightRange(nint nativeComponent)
    {
        return nativeComponent != 0 && getLightRange_ != null ? getLightRange_(nativeComponent) : 0.0f;
    }

    public static void SetLightRange(nint nativeComponent, float range)
    {
        if (nativeComponent != 0 && setLightRange_ != null)
        {
            setLightRange_(nativeComponent, range);
        }
    }

    public static bool GetLightCastShadows(nint nativeComponent)
    {
        return nativeComponent != 0 && getLightCastShadows_ != null && getLightCastShadows_(nativeComponent) != 0;
    }

    public static void SetLightCastShadows(nint nativeComponent, bool castShadows)
    {
        if (nativeComponent != 0 && setLightCastShadows_ != null)
        {
            setLightCastShadows_(nativeComponent, castShadows ? 1 : 0);
        }
    }

    public static void LogInfo(string message)
    {
        if (logInfo_ == null)
        {
            return;
        }

        byte[] bytes = Encoding.UTF8.GetBytes(message + "\0");
        fixed (byte* text = bytes)
        {
            logInfo_(text);
        }
    }
}
