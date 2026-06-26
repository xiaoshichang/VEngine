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
    private static readonly Dictionary<ulong, ScriptInstanceState> Scripts = new();
    private static readonly List<ScriptTypeInfo> ScriptTypes = new();
    private static AssemblyLoadContext? projectContext_;
    private static Assembly? projectAssembly_;
    private static string? projectDirectory_;
    private static ulong nextHandle_ = 1;

    [UnmanagedCallersOnly]
    public static void RegisterNativeApi(NativeScriptApi* nativeApi)
    {
        NativeApi.Register(nativeApi);
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
        foreach (ScriptInstanceState state in Scripts.Values)
        {
            if (state.LifecycleStarted)
            {
                state.Component.OnDestroy();
                state.LifecycleStarted = false;
            }
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
    public static ulong CreateScript(nint nativeComponent, byte* scriptTypeName, int invokeOnCreate)
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
        ScriptInstanceState state = new(script);
        Scripts[handle] = state;
        if (invokeOnCreate != 0)
        {
            StartScriptLifecycle(state);
        }
        return handle;
    }

    [UnmanagedCallersOnly]
    public static void DestroyScript(ulong script)
    {
        if (!Scripts.Remove(script, out ScriptInstanceState? state))
        {
            return;
        }

        if (state.LifecycleStarted)
        {
            state.Component.OnDestroy();
            state.LifecycleStarted = false;
        }
    }

    [UnmanagedCallersOnly]
    public static void OnCreate(ulong script)
    {
        if (Scripts.TryGetValue(script, out ScriptInstanceState? state))
        {
            StartScriptLifecycle(state);
        }
    }

    [UnmanagedCallersOnly]
    public static nint GetScriptFieldsJson(ulong script)
    {
        if (!Scripts.TryGetValue(script, out ScriptInstanceState? state))
        {
            return 0;
        }

        string json = JsonSerializer.Serialize(ReadSerializableFields(state.Component));
        return AllocateUtf8(json);
    }

    [UnmanagedCallersOnly]
    public static int SetScriptFieldsJson(ulong script, byte* fieldsJson)
    {
        if (!Scripts.TryGetValue(script, out ScriptInstanceState? state))
        {
            return 1;
        }

        return ApplySerializedFields(state.Component, ReadUtf8(fieldsJson)) ? 0 : 1;
    }

    [UnmanagedCallersOnly]
    public static int SetScriptFieldJson(ulong script, byte* fieldName, byte* valueJson)
    {
        if (!Scripts.TryGetValue(script, out ScriptInstanceState? state))
        {
            return 1;
        }

        return ApplySerializedField(state.Component, ReadUtf8(fieldName), ReadUtf8(valueJson)) ? 0 : 1;
    }

    [UnmanagedCallersOnly]
    public static void OnUpdate(ulong script, float deltaSeconds)
    {
        if (Scripts.TryGetValue(script, out ScriptInstanceState? state) && state.LifecycleStarted)
        {
            state.Component.OnUpdate(deltaSeconds);
        }
    }

    [UnmanagedCallersOnly]
    public static void OnLateUpdate(ulong script, float deltaSeconds)
    {
        if (Scripts.TryGetValue(script, out ScriptInstanceState? state) && state.LifecycleStarted)
        {
            state.Component.OnLateUpdate(deltaSeconds);
        }
    }

    [UnmanagedCallersOnly]
    public static void OnEnable(ulong script)
    {
        if (Scripts.TryGetValue(script, out ScriptInstanceState? state) && state.LifecycleStarted)
        {
            state.Component.OnEnable();
        }
    }

    [UnmanagedCallersOnly]
    public static void OnDisable(ulong script)
    {
        if (Scripts.TryGetValue(script, out ScriptInstanceState? state) && state.LifecycleStarted)
        {
            state.Component.OnDisable();
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

            ScriptTypes.Add(new ScriptTypeInfo(type.FullName ?? type.Name, type.Name, GetSerializableFields(type)));
        }

        ScriptTypes.Sort((left, right) => string.CompareOrdinal(left.TypeName, right.TypeName));
    }

    private static bool IsScriptComponentType(Type type)
    {
        return type.IsClass && !type.IsAbstract && type.GetConstructor(Type.EmptyTypes) != null && typeof(ScriptComponent).IsAssignableFrom(type);
    }

    private static List<ScriptFieldInfo> GetSerializableFields(Type scriptType)
    {
        List<ScriptFieldInfo> fields = new();
        object? defaultInstance = null;
        try
        {
            defaultInstance = Activator.CreateInstance(scriptType);
        }
        catch (Exception exception)
        {
            NativeApi.LogInfo("Failed to create default script instance for field metadata: " + exception.Message);
        }

        foreach (FieldInfo field in scriptType.GetFields(BindingFlags.Instance | BindingFlags.Public))
        {
            if (field.IsStatic || field.IsInitOnly || field.IsLiteral || field.GetCustomAttribute<NonSerializedAttribute>() != null)
            {
                continue;
            }

            if (!TryGetScriptFieldKind(field.FieldType, out string kind))
            {
                continue;
            }

            object? defaultValue = defaultInstance != null ? field.GetValue(defaultInstance) : GetFallbackDefaultValue(field.FieldType);
            fields.Add(new ScriptFieldInfo(field.Name,
                                           field.Name,
                                           kind,
                                           field.FieldType.FullName ?? field.FieldType.Name,
                                           field.FieldType.IsEnum ? Enum.GetNames(field.FieldType) : Array.Empty<string>(),
                                           NormalizeFieldValue(defaultValue, field.FieldType)));
        }

        fields.Sort((left, right) => string.CompareOrdinal(left.Name, right.Name));
        return fields;
    }

    private static bool TryGetScriptFieldKind(Type type, out string kind)
    {
        if (type == typeof(bool))
        {
            kind = "Bool";
            return true;
        }
        if (type == typeof(int))
        {
            kind = "Int";
            return true;
        }
        if (type == typeof(float))
        {
            kind = "Float";
            return true;
        }
        if (type == typeof(string))
        {
            kind = "String";
            return true;
        }
        if (type == typeof(Vector3))
        {
            kind = "Vector3";
            return true;
        }
        if (type == typeof(Color))
        {
            kind = "Color";
            return true;
        }
        if (type.IsEnum)
        {
            kind = "Enum";
            return true;
        }

        kind = "Unsupported";
        return false;
    }

    private static object? GetFallbackDefaultValue(Type type)
    {
        if (type == typeof(string))
        {
            return string.Empty;
        }

        return type.IsValueType ? Activator.CreateInstance(type) : null;
    }

    private static object? NormalizeFieldValue(object? value, Type type)
    {
        if (type.IsEnum)
        {
            return value?.ToString() ?? string.Empty;
        }

        return value;
    }

    private static void StartScriptLifecycle(ScriptInstanceState state)
    {
        if (state.LifecycleStarted)
        {
            return;
        }

        state.Component.OnCreate();
        state.LifecycleStarted = true;
    }

    private static Dictionary<string, object?> ReadSerializableFields(ScriptComponent script)
    {
        Dictionary<string, object?> values = new();
        Type scriptType = script.GetType();
        foreach (FieldInfo field in scriptType.GetFields(BindingFlags.Instance | BindingFlags.Public))
        {
            if (field.IsStatic || field.IsInitOnly || field.IsLiteral || field.GetCustomAttribute<NonSerializedAttribute>() != null)
            {
                continue;
            }

            if (!TryGetScriptFieldKind(field.FieldType, out _))
            {
                continue;
            }

            values[field.Name] = NormalizeFieldValue(field.GetValue(script), field.FieldType);
        }

        return values;
    }

    private static bool ApplySerializedFields(ScriptComponent script, string serializedFieldsJson)
    {
        if (string.IsNullOrWhiteSpace(serializedFieldsJson))
        {
            return true;
        }

        Dictionary<string, JsonElement>? values;
        try
        {
            values = JsonSerializer.Deserialize<Dictionary<string, JsonElement>>(serializedFieldsJson);
        }
        catch (JsonException exception)
        {
            NativeApi.LogInfo("Failed to parse serialized script fields: " + exception.Message);
            return false;
        }

        if (values == null)
        {
            return true;
        }

        Type scriptType = script.GetType();
        foreach ((string fieldName, JsonElement value) in values)
        {
            ApplySerializedField(script, scriptType, fieldName, value);
        }

        return true;
    }

    private static bool ApplySerializedField(ScriptComponent script, string fieldName, string valueJson)
    {
        if (string.IsNullOrWhiteSpace(fieldName) || string.IsNullOrWhiteSpace(valueJson))
        {
            return false;
        }

        try
        {
            using JsonDocument document = JsonDocument.Parse(valueJson);
            return ApplySerializedField(script, script.GetType(), fieldName, document.RootElement);
        }
        catch (JsonException exception)
        {
            NativeApi.LogInfo("Failed to parse serialized script field '" + fieldName + "': " + exception.Message);
            return false;
        }
    }

    private static bool ApplySerializedField(ScriptComponent script, Type scriptType, string fieldName, JsonElement value)
    {
        FieldInfo? field = scriptType.GetField(fieldName, BindingFlags.Instance | BindingFlags.Public);
        if (field == null || field.IsStatic || field.IsInitOnly || field.IsLiteral || field.GetCustomAttribute<NonSerializedAttribute>() != null)
        {
            return false;
        }

        if (!TryGetScriptFieldKind(field.FieldType, out _) || !TryReadFieldValue(value, field.FieldType, out object? fieldValue))
        {
            NativeApi.LogInfo("Failed to apply serialized script field: " + scriptType.FullName + "." + fieldName);
            return false;
        }

        field.SetValue(script, fieldValue);
        return true;
    }

    private static bool TryReadFieldValue(JsonElement value, Type type, out object? fieldValue)
    {
        try
        {
            if (type == typeof(bool))
            {
                fieldValue = value.GetBoolean();
                return true;
            }
            if (type == typeof(int))
            {
                fieldValue = value.GetInt32();
                return true;
            }
            if (type == typeof(float))
            {
                fieldValue = value.GetSingle();
                return true;
            }
            if (type == typeof(string))
            {
                fieldValue = value.GetString() ?? string.Empty;
                return true;
            }
            if (type == typeof(Vector3))
            {
                fieldValue = value.Deserialize<Vector3>();
                return fieldValue != null;
            }
            if (type == typeof(Color))
            {
                fieldValue = value.Deserialize<Color>();
                return fieldValue != null;
            }
            if (type.IsEnum)
            {
                fieldValue = value.ValueKind == JsonValueKind.String
                    ? Enum.Parse(type, value.GetString() ?? string.Empty)
                    : Enum.ToObject(type, value.GetInt32());
                return true;
            }
        }
        catch (Exception)
        {
        }

        fieldValue = null;
        return false;
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
                                         [property: JsonPropertyName("displayName")] string DisplayName,
                                         [property: JsonPropertyName("fields")] IReadOnlyList<ScriptFieldInfo> Fields);

    private sealed record ScriptFieldInfo([property: JsonPropertyName("name")] string Name,
                                          [property: JsonPropertyName("displayName")] string DisplayName,
                                          [property: JsonPropertyName("kind")] string Kind,
                                          [property: JsonPropertyName("managedTypeName")] string ManagedTypeName,
                                          [property: JsonPropertyName("enumNames")] IReadOnlyList<string> EnumNames,
                                          [property: JsonPropertyName("defaultValue")] object? DefaultValue);

    private sealed class ScriptInstanceState
    {
        public ScriptInstanceState(ScriptComponent component)
        {
            Component = component;
        }

        public ScriptComponent Component { get; }
        public bool LifecycleStarted { get; set; }
    }
}
