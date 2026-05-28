using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace VEngine.Scripting;

public static unsafe class ScriptApiBootstrap
{
    private static readonly Dictionary<ulong, ScriptInstanceState> Instances = new();
    private static Assembly? s_projectAssembly;
    private static ulong s_nextInstanceId = 1;
    private static string s_lastError = string.Empty;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int InitializeHost(void* bridgeApi, int bridgeApiSize)
    {
        try
        {
            int result = ScriptBridge.Initialize((NativeScriptBridgeApi*)bridgeApi, bridgeApiSize);
            if (result != 0)
            {
                s_lastError = $"Script bridge initialization failed with status {result}.";
            }

            return result;
        }
        catch (Exception exception)
        {
            return CaptureException(exception);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int LoadProjectAssembly(byte* assemblyPath, int assemblyPathSizeInBytes)
    {
        try
        {
            if (s_projectAssembly != null)
            {
                throw new InvalidOperationException("A project script assembly is already loaded.");
            }

            string path = ReadUtf8(assemblyPath, assemblyPathSizeInBytes);
            if (string.IsNullOrWhiteSpace(path))
            {
                throw new ArgumentException("Project script assembly path is empty.", nameof(assemblyPath));
            }

            string fullPath = Path.GetFullPath(path);
            if (!File.Exists(fullPath))
            {
                throw new FileNotFoundException("Project script assembly was not found.", fullPath);
            }

            s_projectAssembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(fullPath);
            s_lastError = string.Empty;
            return 0;
        }
        catch (Exception exception)
        {
            return CaptureException(exception);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static ulong CreateScriptInstance(byte* scriptTypeName, int scriptTypeNameSizeInBytes, ulong nativeHandle)
    {
        try
        {
            if (s_projectAssembly == null)
            {
                throw new InvalidOperationException("No project script assembly has been loaded.");
            }

            if (nativeHandle == 0)
            {
                throw new ArgumentException("Native script handle is invalid.", nameof(nativeHandle));
            }

            string typeName = ReadUtf8(scriptTypeName, scriptTypeNameSizeInBytes);
            if (string.IsNullOrWhiteSpace(typeName))
            {
                throw new ArgumentException("Script type name is empty.", nameof(scriptTypeName));
            }

            Type scriptType = ResolveScriptType(typeName);
            if (!typeof(ScriptBehaviour).IsAssignableFrom(scriptType))
            {
                throw new InvalidOperationException($"Script type '{typeName}' must derive from ScriptBehaviour.");
            }

            if (Activator.CreateInstance(scriptType) is not ScriptBehaviour behaviour)
            {
                throw new InvalidOperationException($"Script type '{typeName}' could not be constructed.");
            }

            behaviour.AttachNativeHandle(nativeHandle);

            ulong instanceId = AllocateInstanceId();
            Instances.Add(instanceId, new ScriptInstanceState(behaviour, scriptType.FullName ?? typeName));
            s_lastError = string.Empty;
            return instanceId;
        }
        catch (Exception exception)
        {
            CaptureException(exception);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int InvokeLifecycle(ulong instanceId, int lifecycle, float deltaSeconds)
    {
        try
        {
            ScriptInstanceState state = GetInstance(instanceId);
            switch ((ScriptLifecycleKind)lifecycle)
            {
            case ScriptLifecycleKind.OnCreate:
                state.Behaviour.InvokeOnCreate();
                break;
            case ScriptLifecycleKind.OnDestroy:
                state.Behaviour.InvokeOnDestroy();
                break;
            case ScriptLifecycleKind.OnEnable:
                state.Behaviour.InvokeOnEnable();
                break;
            case ScriptLifecycleKind.OnDisable:
                state.Behaviour.InvokeOnDisable();
                break;
            case ScriptLifecycleKind.OnUpdate:
                state.Behaviour.InvokeOnUpdate(deltaSeconds);
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(lifecycle), lifecycle, "Unknown script lifecycle method.");
            }

            s_lastError = string.Empty;
            return 0;
        }
        catch (Exception exception)
        {
            return CaptureException(exception);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int DestroyScriptInstance(ulong instanceId)
    {
        try
        {
            ScriptInstanceState state = GetInstance(instanceId);
            Instances.Remove(instanceId);
            state.Behaviour.DetachNativeHandle();

            if (state.Behaviour is IDisposable disposable)
            {
                disposable.Dispose();
            }

            s_lastError = string.Empty;
            return 0;
        }
        catch (Exception exception)
        {
            return CaptureException(exception);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int GetLastError(byte* buffer, int bufferSizeInBytes)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(s_lastError);
        if (buffer != null && bufferSizeInBytes > 0)
        {
            int copySize = Math.Min(bytes.Length, bufferSizeInBytes - 1);
            for (int index = 0; index < copySize; ++index)
            {
                buffer[index] = bytes[index];
            }

            buffer[copySize] = 0;
        }

        return bytes.Length;
    }

    private static ScriptInstanceState GetInstance(ulong instanceId)
    {
        if (!Instances.TryGetValue(instanceId, out ScriptInstanceState? state))
        {
            throw new InvalidOperationException($"Unknown script instance id {instanceId}.");
        }

        return state;
    }

    private static Type ResolveScriptType(string typeName)
    {
        Type? scriptType = s_projectAssembly?.GetType(typeName, throwOnError: false, ignoreCase: false);
        scriptType ??= Type.GetType(typeName, throwOnError: false, ignoreCase: false);

        if (scriptType == null)
        {
            throw new TypeLoadException($"Script type '{typeName}' was not found in the project script assembly.");
        }

        return scriptType;
    }

    private static ulong AllocateInstanceId()
    {
        ulong instanceId = s_nextInstanceId++;
        if (instanceId == 0)
        {
            instanceId = s_nextInstanceId++;
        }

        return instanceId;
    }

    private static string ReadUtf8(byte* text, int sizeInBytes)
    {
        if (text == null)
        {
            if (sizeInBytes == 0)
            {
                return string.Empty;
            }

            throw new ArgumentNullException(nameof(text));
        }

        if (sizeInBytes < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(sizeInBytes), sizeInBytes, "String byte size is negative.");
        }

        return Encoding.UTF8.GetString(text, sizeInBytes);
    }

    private static int CaptureException(Exception exception)
    {
        s_lastError = exception.ToString();
        return 1;
    }

    private sealed class ScriptInstanceState
    {
        public ScriptInstanceState(ScriptBehaviour behaviour, string typeName)
        {
            Behaviour = behaviour;
            TypeName = typeName;
        }

        public ScriptBehaviour Behaviour { get; }

        public string TypeName { get; }
    }
}

internal enum ScriptLifecycleKind
{
    OnCreate = 0,
    OnDestroy = 1,
    OnEnable = 2,
    OnDisable = 3,
    OnUpdate = 4,
}
