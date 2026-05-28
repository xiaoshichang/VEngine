using System;
using System.Runtime.InteropServices;
using System.Text;
using VEngine;

namespace VEngine.Scripting;

internal enum NativeLogSeverity
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5,
}

internal enum NativeScriptBridgeStatus
{
    Success = 0,
    InvalidHandle = -1,
    MissingComponent = -2,
    InvalidArgument = -3,
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeScriptVector3
{
    public float X;
    public float Y;
    public float Z;

    public static NativeScriptVector3 FromVector3(Vector3 value)
    {
        return new NativeScriptVector3
        {
            X = value.X,
            Y = value.Y,
            Z = value.Z,
        };
    }

    public Vector3 ToVector3()
    {
        return new Vector3(X, Y, Z);
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeScriptQuaternion
{
    public float X;
    public float Y;
    public float Z;
    public float W;

    public static NativeScriptQuaternion FromQuaternion(Quaternion value)
    {
        return new NativeScriptQuaternion
        {
            X = value.X,
            Y = value.Y,
            Z = value.Z,
            W = value.W,
        };
    }

    public Quaternion ToQuaternion()
    {
        return new Quaternion(X, Y, Z, W);
    }
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeScriptBridgeApi
{
    public int Version;
    public int Size;
    public IntPtr UserData;

    public delegate* unmanaged[Stdcall]<IntPtr, int, byte*, int, int> Log;
    public delegate* unmanaged[Stdcall]<IntPtr, double> GetTotalSeconds;
    public delegate* unmanaged[Stdcall]<IntPtr, float> GetDeltaSeconds;
    public delegate* unmanaged[Stdcall]<IntPtr, ulong> GetFrameIndex;

    public delegate* unmanaged[Stdcall]<IntPtr, ulong, byte*, int, int> GetGameObjectName;
    public delegate* unmanaged[Stdcall]<IntPtr, ulong, byte*, int, int> SetGameObjectName;

    public delegate* unmanaged[Stdcall]<IntPtr, ulong, NativeScriptVector3*, int> GetLocalPosition;
    public delegate* unmanaged[Stdcall]<IntPtr, ulong, NativeScriptVector3, int> SetLocalPosition;
    public delegate* unmanaged[Stdcall]<IntPtr, ulong, NativeScriptQuaternion*, int> GetLocalRotation;
    public delegate* unmanaged[Stdcall]<IntPtr, ulong, NativeScriptQuaternion, int> SetLocalRotation;
    public delegate* unmanaged[Stdcall]<IntPtr, ulong, NativeScriptVector3*, int> GetLocalScale;
    public delegate* unmanaged[Stdcall]<IntPtr, ulong, NativeScriptVector3, int> SetLocalScale;
}

internal static unsafe class ScriptBridge
{
    private const int ExpectedVersion = 1;

    private static NativeScriptBridgeApi _api;
    private static bool _initialized;

    public static int Initialize(NativeScriptBridgeApi* api, int apiSize)
    {
        if (api == null)
        {
            return 1;
        }

        if (apiSize < sizeof(NativeScriptBridgeApi) || api->Size < sizeof(NativeScriptBridgeApi))
        {
            return 2;
        }

        if (api->Version != ExpectedVersion)
        {
            return 3;
        }

        if (api->Log == null ||
            api->GetTotalSeconds == null ||
            api->GetDeltaSeconds == null ||
            api->GetFrameIndex == null ||
            api->GetGameObjectName == null ||
            api->SetGameObjectName == null ||
            api->GetLocalPosition == null ||
            api->SetLocalPosition == null ||
            api->GetLocalRotation == null ||
            api->SetLocalRotation == null ||
            api->GetLocalScale == null ||
            api->SetLocalScale == null)
        {
            return 4;
        }

        _api = *api;
        _initialized = true;
        return 0;
    }

    public static void Log(NativeLogSeverity severity, string? message)
    {
        EnsureInitialized();

        byte[] bytes = Encoding.UTF8.GetBytes(message ?? string.Empty);
        fixed (byte* messageBytes = bytes)
        {
            ThrowIfBridgeError(_api.Log(_api.UserData, (int)severity, messageBytes, bytes.Length));
        }
    }

    public static double GetTotalSeconds()
    {
        EnsureInitialized();
        return _api.GetTotalSeconds(_api.UserData);
    }

    public static float GetDeltaSeconds()
    {
        EnsureInitialized();
        return _api.GetDeltaSeconds(_api.UserData);
    }

    public static ulong GetFrameIndex()
    {
        EnsureInitialized();
        return _api.GetFrameIndex(_api.UserData);
    }

    public static string GetGameObjectName(ulong nativeHandle)
    {
        EnsureValidHandle(nativeHandle);
        EnsureInitialized();

        int requiredSize = _api.GetGameObjectName(_api.UserData, nativeHandle, null, 0);
        ThrowIfBridgeError(requiredSize);

        byte[] bytes = new byte[requiredSize + 1];
        fixed (byte* buffer = bytes)
        {
            int copiedSize = _api.GetGameObjectName(_api.UserData, nativeHandle, buffer, bytes.Length);
            ThrowIfBridgeError(copiedSize);
            return Encoding.UTF8.GetString(bytes, 0, copiedSize);
        }
    }

    public static void SetGameObjectName(ulong nativeHandle, string? value)
    {
        EnsureValidHandle(nativeHandle);
        EnsureInitialized();

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* nameBytes = bytes)
        {
            ThrowIfBridgeError(_api.SetGameObjectName(_api.UserData, nativeHandle, nameBytes, bytes.Length));
        }
    }

    public static Vector3 GetLocalPosition(ulong nativeHandle)
    {
        EnsureValidHandle(nativeHandle);
        EnsureInitialized();

        NativeScriptVector3 value;
        ThrowIfBridgeError(_api.GetLocalPosition(_api.UserData, nativeHandle, &value));
        return value.ToVector3();
    }

    public static void SetLocalPosition(ulong nativeHandle, Vector3 value)
    {
        EnsureValidHandle(nativeHandle);
        EnsureInitialized();
        ThrowIfBridgeError(_api.SetLocalPosition(_api.UserData, nativeHandle, NativeScriptVector3.FromVector3(value)));
    }

    public static Quaternion GetLocalRotation(ulong nativeHandle)
    {
        EnsureValidHandle(nativeHandle);
        EnsureInitialized();

        NativeScriptQuaternion value;
        ThrowIfBridgeError(_api.GetLocalRotation(_api.UserData, nativeHandle, &value));
        return value.ToQuaternion();
    }

    public static void SetLocalRotation(ulong nativeHandle, Quaternion value)
    {
        EnsureValidHandle(nativeHandle);
        EnsureInitialized();
        ThrowIfBridgeError(
            _api.SetLocalRotation(_api.UserData, nativeHandle, NativeScriptQuaternion.FromQuaternion(value)));
    }

    public static Vector3 GetLocalScale(ulong nativeHandle)
    {
        EnsureValidHandle(nativeHandle);
        EnsureInitialized();

        NativeScriptVector3 value;
        ThrowIfBridgeError(_api.GetLocalScale(_api.UserData, nativeHandle, &value));
        return value.ToVector3();
    }

    public static void SetLocalScale(ulong nativeHandle, Vector3 value)
    {
        EnsureValidHandle(nativeHandle);
        EnsureInitialized();
        ThrowIfBridgeError(_api.SetLocalScale(_api.UserData, nativeHandle, NativeScriptVector3.FromVector3(value)));
    }

    private static void EnsureInitialized()
    {
        if (!_initialized)
        {
            throw new InvalidOperationException("VEngine script bridge has not been initialized.");
        }
    }

    private static void EnsureValidHandle(ulong nativeHandle)
    {
        if (nativeHandle == 0)
        {
            throw new InvalidOperationException("The native script handle is no longer valid.");
        }
    }

    private static void ThrowIfBridgeError(int status)
    {
        if (status >= 0)
        {
            return;
        }

        string message = (NativeScriptBridgeStatus)status switch
        {
            NativeScriptBridgeStatus.InvalidHandle => "The native script handle is stale or invalid.",
            NativeScriptBridgeStatus.MissingComponent => "The GameObject does not have the requested native component.",
            NativeScriptBridgeStatus.InvalidArgument => "The native script bridge received an invalid argument.",
            _ => $"Native script bridge call failed with status {status}.",
        };

        throw new InvalidOperationException(message);
    }
}
