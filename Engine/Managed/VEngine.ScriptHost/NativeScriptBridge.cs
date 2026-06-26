using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace VEngine.Scripting;

public static unsafe class NativeScriptBridge
{
    private static readonly Dictionary<ulong, ScriptComponent> Scripts = new();
    private static readonly List<ScriptTypeInfo> ScriptTypes = new();
    private static AssemblyLoadContext? projectContext_;
    private static Assembly? projectAssembly_;
    private static string? projectDirectory_;
    private static ulong nextHandle_ = 1;

    [UnmanagedCallersOnly]
    public static void RegisterNativeApi(delegate* unmanaged[Cdecl]<nint, float*, float*, float*, void> getTransformLocalPosition,
                                         delegate* unmanaged[Cdecl]<nint, float, float, float, void> setTransformLocalPosition,
                                         delegate* unmanaged[Cdecl]<byte*, void> logInfo)
    {
        NativeApi.Register(getTransformLocalPosition, setTransformLocalPosition, logInfo);
    }

    [UnmanagedCallersOnly]
    public static int LoadProjectAssembly(byte* assemblyPath)
    {
        string path = ReadUtf8(assemblyPath);
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            return 1;
        }

        UnloadProjectAssemblyInternal();

        string fullPath = Path.GetFullPath(path);
        projectContext_ = new AssemblyLoadContext("VEngineProjectScripts", isCollectible: true);
        projectContext_.Resolving += ResolveProjectDependency;
        projectDirectory_ = Path.GetDirectoryName(fullPath);
        projectAssembly_ = LoadAssemblyWithoutFileLock(projectContext_, fullPath);
        RefreshScriptTypes();
        return 0;
    }

    [UnmanagedCallersOnly]
    public static void UnloadProjectAssembly()
    {
        UnloadProjectAssemblyInternal();
    }

    private static void UnloadProjectAssemblyInternal()
    {
        foreach (ScriptComponent script in Scripts.Values)
        {
            script.OnDestroy();
        }

        Scripts.Clear();
        ScriptTypes.Clear();
        projectAssembly_ = null;
        projectDirectory_ = null;

        if (projectContext_ != null)
        {
            projectContext_.Unload();
            projectContext_ = null;
        }
    }

    [UnmanagedCallersOnly]
    public static nint GetScriptTypesJson()
    {
        string json = JsonSerializer.Serialize(ScriptTypes);
        return AllocateUtf8(json);
    }

    [UnmanagedCallersOnly]
    public static void FreeString(nint text)
    {
        if (text != 0)
        {
            NativeMemory.Free((void*)text);
        }
    }

    [UnmanagedCallersOnly]
    public static ulong CreateScript(nint nativeComponent, byte* scriptTypeName)
    {
        if (nativeComponent == 0 || projectAssembly_ == null)
        {
            NativeApi.LogInfo("CreateScript failed because native component or project assembly is missing.");
            return 0;
        }

        string typeName = ReadUtf8(scriptTypeName);
        Type? scriptType = projectAssembly_.GetType(typeName, throwOnError: false, ignoreCase: false);
        if (scriptType == null || !IsScriptComponentType(scriptType))
        {
            NativeApi.LogInfo("CreateScript failed because script type was not found or invalid: " + typeName);
            return 0;
        }

        if (Activator.CreateInstance(scriptType) is not ScriptComponent script)
        {
            NativeApi.LogInfo("CreateScript failed because Activator returned a non-script instance: " + typeName);
            return 0;
        }

        ulong handle = nextHandle_++;
        script.NativeComponent = nativeComponent;
        Scripts[handle] = script;
        script.OnCreate();
        return handle;
    }

    [UnmanagedCallersOnly]
    public static void DestroyScript(ulong script)
    {
        if (!Scripts.Remove(script, out ScriptComponent? component))
        {
            return;
        }

        component.OnDestroy();
    }

    [UnmanagedCallersOnly]
    public static void OnUpdate(ulong script, float deltaSeconds)
    {
        if (Scripts.TryGetValue(script, out ScriptComponent? component))
        {
            component.OnUpdate(deltaSeconds);
        }
    }

    [UnmanagedCallersOnly]
    public static void OnLateUpdate(ulong script, float deltaSeconds)
    {
        if (Scripts.TryGetValue(script, out ScriptComponent? component))
        {
            component.OnLateUpdate(deltaSeconds);
        }
    }

    [UnmanagedCallersOnly]
    public static void OnEnable(ulong script)
    {
        if (Scripts.TryGetValue(script, out ScriptComponent? component))
        {
            component.OnEnable();
        }
    }

    [UnmanagedCallersOnly]
    public static void OnDisable(ulong script)
    {
        if (Scripts.TryGetValue(script, out ScriptComponent? component))
        {
            component.OnDisable();
        }
    }

    private static Assembly? ResolveProjectDependency(AssemblyLoadContext context, AssemblyName assemblyName)
    {
        if (assemblyName.Name == typeof(ScriptComponent).Assembly.GetName().Name)
        {
            return typeof(ScriptComponent).Assembly;
        }

        if (string.IsNullOrEmpty(projectDirectory_))
        {
            return null;
        }

        string candidate = Path.Combine(projectDirectory_, assemblyName.Name + ".dll");
        return File.Exists(candidate) ? LoadAssemblyWithoutFileLock(context, candidate) : null;
    }

    private static Assembly LoadAssemblyWithoutFileLock(AssemblyLoadContext context, string assemblyPath)
    {
        byte[] assemblyBytes = File.ReadAllBytes(assemblyPath);
        string pdbPath = Path.ChangeExtension(assemblyPath, ".pdb");
        if (File.Exists(pdbPath))
        {
            byte[] pdbBytes = File.ReadAllBytes(pdbPath);
            return context.LoadFromStream(new MemoryStream(assemblyBytes), new MemoryStream(pdbBytes));
        }

        return context.LoadFromStream(new MemoryStream(assemblyBytes));
    }

    private static void RefreshScriptTypes()
    {
        ScriptTypes.Clear();
        if (projectAssembly_ == null)
        {
            return;
        }

        foreach (Type type in projectAssembly_.GetTypes())
        {
            if (!IsScriptComponentType(type))
            {
                continue;
            }

            ScriptTypes.Add(new ScriptTypeInfo(type.FullName ?? type.Name, type.Name));
        }

        ScriptTypes.Sort((left, right) => string.CompareOrdinal(left.TypeName, right.TypeName));
    }

    private static bool IsScriptComponentType(Type type)
    {
        return type.IsClass && !type.IsAbstract && type.GetConstructor(Type.EmptyTypes) != null && typeof(ScriptComponent).IsAssignableFrom(type);
    }

    private static string ReadUtf8(byte* text)
    {
        if (text == null)
        {
            return string.Empty;
        }

        int length = 0;
        while (text[length] != 0)
        {
            ++length;
        }

        return Encoding.UTF8.GetString(text, length);
    }

    private static nint AllocateUtf8(string text)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text + "\0");
        void* memory = NativeMemory.Alloc((nuint)bytes.Length);
        fixed (byte* source = bytes)
        {
            Buffer.MemoryCopy(source, memory, bytes.Length, bytes.Length);
        }

        return (nint)memory;
    }

    private sealed record ScriptTypeInfo([property: JsonPropertyName("typeName")] string TypeName,
                                         [property: JsonPropertyName("displayName")] string DisplayName);
}
