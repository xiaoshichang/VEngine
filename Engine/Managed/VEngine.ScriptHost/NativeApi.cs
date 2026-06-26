using System.Runtime.CompilerServices;
using System.Text;

namespace VEngine.Scripting;

internal static unsafe class NativeApi
{
    private static delegate* unmanaged[Cdecl]<nint, float*, float*, float*, void> getTransformLocalPosition_;
    private static delegate* unmanaged[Cdecl]<nint, float, float, float, void> setTransformLocalPosition_;
    private static delegate* unmanaged[Cdecl]<byte*, void> logInfo_;

    public static void Register(delegate* unmanaged[Cdecl]<nint, float*, float*, float*, void> getTransformLocalPosition,
                                delegate* unmanaged[Cdecl]<nint, float, float, float, void> setTransformLocalPosition,
                                delegate* unmanaged[Cdecl]<byte*, void> logInfo)
    {
        getTransformLocalPosition_ = getTransformLocalPosition;
        setTransformLocalPosition_ = setTransformLocalPosition;
        logInfo_ = logInfo;
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
