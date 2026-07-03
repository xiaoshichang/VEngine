using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
#if !VENGINE_IOS_NATIVEAOT
using System.Runtime.Loader;
#endif
using System.Text;
using System.Text.Json;

namespace VEngine.Scripting;

public static unsafe class NativeScriptBridge
{
    private const DynamicallyAccessedMemberTypes ScriptTypeReflectionMembers =
        DynamicallyAccessedMemberTypes.PublicParameterlessConstructor | DynamicallyAccessedMemberTypes.PublicFields;

    private static readonly Dictionary<ulong, ScriptInstanceState> Scripts = new();
    private static readonly List<ScriptTypeInfo> ScriptTypes = new();
    private static readonly List<LinkedScriptTypeRegistration> LinkedScriptTypes = new();
#if !VENGINE_IOS_NATIVEAOT
    private static AssemblyLoadContext? projectContext_;
#endif
#if !VENGINE_IOS_NATIVEAOT
    private static Assembly? projectAssembly_;
    private static string? projectDirectory_;
#endif
    private static ulong nextHandle_ = 1;

    [UnmanagedCallersOnly(EntryPoint = "VEngine_RegisterNativeApi")]
    public static void RegisterNativeApiExport(NativeScriptApi* nativeApi)
    {
        RegisterNativeApi(nativeApi);
    }

    public static void RegisterNativeApi(NativeScriptApi* nativeApi)
    {
        NativeApi.Register(nativeApi);
    }

    public static void RegisterLinkedScriptType<[DynamicallyAccessedMembers(ScriptTypeReflectionMembers)] T>()
        where T : ScriptComponent, new()
    {
        Type scriptType = typeof(T);
        if (IsLinkedScriptTypeRegistered(scriptType))
        {
            return;
        }

        LinkedScriptTypes.Add(new LinkedScriptTypeRegistration(scriptType, static () => new T(), GetSerializableFieldInfos(scriptType)));
    }

#if !VENGINE_IOS_NATIVEAOT
    [RequiresUnreferencedCode("Use RegisterLinkedScriptType<T>() for NativeAOT so constructors and fields are preserved.")]
    public static void RegisterLinkedScriptTypes(Type[] scriptTypes)
    {
        foreach (Type scriptType in scriptTypes)
        {
            RegisterLinkedScriptType(scriptType);
        }
    }
#endif

    public static int LoadProjectAssembly(byte* assemblyPath)
    {
#if VENGINE_IOS_NATIVEAOT
        _ = assemblyPath;
        UnloadProjectAssemblyInternal();
        RefreshScriptTypes();
        return 0;
#else
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
#endif
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_LoadProjectAssembly")]
    public static int LoadProjectAssemblyExport(byte* assemblyPath)
    {
        return LoadProjectAssembly(assemblyPath);
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_UnloadProjectAssembly")]
    public static void UnloadProjectAssemblyExport()
    {
        UnloadProjectAssembly();
    }

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
#if !VENGINE_IOS_NATIVEAOT
        projectAssembly_ = null;
        projectDirectory_ = null;

        if (projectContext_ != null)
        {
            projectContext_.Unload();
            projectContext_ = null;
        }
#endif
    }

    public static nint GetScriptTypesJson()
    {
        return AllocateUtf8(WriteScriptTypesJson());
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_GetScriptTypesJson")]
    public static nint GetScriptTypesJsonExport()
    {
        return GetScriptTypesJson();
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_FreeString")]
    public static void FreeStringExport(nint text)
    {
        FreeString(text);
    }

    public static void FreeString(nint text)
    {
        if (text != 0)
        {
            NativeMemory.Free((void*)text);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_CreateScript")]
    public static ulong CreateScriptExport(nint nativeComponent, byte* scriptTypeName, int invokeOnCreate)
    {
        return CreateScript(nativeComponent, scriptTypeName, invokeOnCreate);
    }

    public static ulong CreateScript(nint nativeComponent, byte* scriptTypeName, int invokeOnCreate)
    {
        if (nativeComponent == 0)
        {
            NativeApi.LogInfo("CreateScript failed because native component is missing.");
            return 0;
        }

        string typeName = ReadUtf8(scriptTypeName);
        ScriptComponent script;
        LinkedScriptTypeRegistration? scriptRegistration = null;
#if VENGINE_IOS_NATIVEAOT
        scriptRegistration = FindLinkedScriptRegistration(typeName);
        if (scriptRegistration == null)
        {
            NativeApi.LogInfo("CreateScript failed because script type was not found or invalid: " + typeName);
            return 0;
        }

        script = scriptRegistration.CreateInstance();
#else
        Type? scriptType = FindScriptType(typeName);
        if (scriptType == null || !IsScriptComponentType(scriptType))
        {
            NativeApi.LogInfo("CreateScript failed because script type was not found or invalid: " + typeName);
            return 0;
        }

        if (Activator.CreateInstance(scriptType) is not ScriptComponent createdScript)
        {
            NativeApi.LogInfo("CreateScript failed because Activator returned a non-script instance: " + typeName);
            return 0;
        }

        script = createdScript;
#endif

        ulong handle = nextHandle_++;
        script.NativeComponent = nativeComponent;
        ScriptInstanceState state = new(script, scriptRegistration);
        Scripts[handle] = state;
        if (invokeOnCreate != 0)
        {
            StartScriptLifecycle(state);
        }
        return handle;
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_DestroyScript")]
    public static void DestroyScriptExport(ulong script)
    {
        DestroyScript(script);
    }

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

    [UnmanagedCallersOnly(EntryPoint = "VEngine_OnCreate")]
    public static void OnCreateExport(ulong script)
    {
        OnCreate(script);
    }

    public static void OnCreate(ulong script)
    {
        if (Scripts.TryGetValue(script, out ScriptInstanceState? state))
        {
            StartScriptLifecycle(state);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_GetScriptFieldsJson")]
    public static nint GetScriptFieldsJsonExport(ulong script)
    {
        return GetScriptFieldsJson(script);
    }

    public static nint GetScriptFieldsJson(ulong script)
    {
        if (!Scripts.TryGetValue(script, out ScriptInstanceState? state))
        {
            return 0;
        }

        return AllocateUtf8(WriteSerializableFieldsJson(state));
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_SetScriptFieldsJson")]
    public static int SetScriptFieldsJsonExport(ulong script, byte* fieldsJson)
    {
        return SetScriptFieldsJson(script, fieldsJson);
    }

    public static int SetScriptFieldsJson(ulong script, byte* fieldsJson)
    {
        if (!Scripts.TryGetValue(script, out ScriptInstanceState? state))
        {
            return 1;
        }

        return ApplySerializedFields(state, ReadUtf8(fieldsJson)) ? 0 : 1;
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_SetScriptFieldJson")]
    public static int SetScriptFieldJsonExport(ulong script, byte* fieldName, byte* valueJson)
    {
        return SetScriptFieldJson(script, fieldName, valueJson);
    }

    public static int SetScriptFieldJson(ulong script, byte* fieldName, byte* valueJson)
    {
        if (!Scripts.TryGetValue(script, out ScriptInstanceState? state))
        {
            return 1;
        }

        return ApplySerializedField(state, ReadUtf8(fieldName), ReadUtf8(valueJson)) ? 0 : 1;
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_OnUpdate")]
    public static void OnUpdateExport(ulong script, float deltaSeconds)
    {
        OnUpdate(script, deltaSeconds);
    }

    public static void OnUpdate(ulong script, float deltaSeconds)
    {
        if (Scripts.TryGetValue(script, out ScriptInstanceState? state) && state.LifecycleStarted)
        {
            state.Component.OnUpdate(deltaSeconds);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_OnLateUpdate")]
    public static void OnLateUpdateExport(ulong script, float deltaSeconds)
    {
        OnLateUpdate(script, deltaSeconds);
    }

    public static void OnLateUpdate(ulong script, float deltaSeconds)
    {
        if (Scripts.TryGetValue(script, out ScriptInstanceState? state) && state.LifecycleStarted)
        {
            state.Component.OnLateUpdate(deltaSeconds);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_OnEnable")]
    public static void OnEnableExport(ulong script)
    {
        OnEnable(script);
    }

    public static void OnEnable(ulong script)
    {
        if (Scripts.TryGetValue(script, out ScriptInstanceState? state) && state.LifecycleStarted)
        {
            state.Component.OnEnable();
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "VEngine_OnDisable")]
    public static void OnDisableExport(ulong script)
    {
        OnDisable(script);
    }

    public static void OnDisable(ulong script)
    {
        if (Scripts.TryGetValue(script, out ScriptInstanceState? state) && state.LifecycleStarted)
        {
            state.Component.OnDisable();
        }
    }

#if !VENGINE_IOS_NATIVEAOT
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
#endif

    private static void RefreshScriptTypes()
    {
        ScriptTypes.Clear();
#if VENGINE_IOS_NATIVEAOT
        foreach (LinkedScriptTypeRegistration registration in LinkedScriptTypes)
        {
            AddScriptType(registration);
        }
#else
        if (projectAssembly_ == null)
        {
            return;
        }

        AddScriptTypesFromAssembly(projectAssembly_);
#endif

        ScriptTypes.Sort((left, right) => string.CompareOrdinal(left.TypeName, right.TypeName));
    }

#if !VENGINE_IOS_NATIVEAOT
    private static void AddScriptTypesFromAssembly(Assembly assembly)
    {
        foreach (Type type in assembly.GetTypes())
        {
            AddScriptType(type);
        }
    }

    private static void AddScriptType([DynamicallyAccessedMembers(ScriptTypeReflectionMembers)] Type type)
    {
        if (!IsScriptComponentType(type))
        {
            return;
        }

        ScriptTypes.Add(new ScriptTypeInfo(type.FullName ?? type.Name, type.Name, GetSerializableFields(type)));
    }
#endif

    private static void AddScriptType(LinkedScriptTypeRegistration registration)
    {
        ScriptTypes.Add(new ScriptTypeInfo(registration.TypeName, registration.DisplayName, GetSerializableFields(registration)));
    }

#if !VENGINE_IOS_NATIVEAOT
    private static Type? FindScriptType(string typeName)
    {
        if (projectAssembly_ != null)
        {
            Type? scriptType = projectAssembly_.GetType(typeName, throwOnError: false, ignoreCase: false);
            if (scriptType != null)
            {
                return scriptType;
            }
        }

        foreach (LinkedScriptTypeRegistration registration in LinkedScriptTypes)
        {
            if (registration.TypeName == typeName)
            {
                return registration.Type;
            }
        }

        foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            Type? scriptType = assembly.GetType(typeName, throwOnError: false, ignoreCase: false);
            if (scriptType != null)
            {
                return scriptType;
            }
        }

        return null;
    }
#endif

    private static LinkedScriptTypeRegistration? FindLinkedScriptRegistration(string typeName)
    {
        foreach (LinkedScriptTypeRegistration registration in LinkedScriptTypes)
        {
            if (registration.TypeName == typeName)
            {
                return registration;
            }
        }

        return null;
    }

    private static bool IsLinkedScriptTypeRegistered(Type scriptType)
    {
        string typeName = scriptType.FullName ?? scriptType.Name;
        foreach (LinkedScriptTypeRegistration registration in LinkedScriptTypes)
        {
            if (registration.TypeName == typeName)
            {
                return true;
            }
        }

        return false;
    }

#if !VENGINE_IOS_NATIVEAOT
    [RequiresUnreferencedCode("Use RegisterLinkedScriptType<T>() for NativeAOT so constructors and fields are preserved.")]
    private static void RegisterLinkedScriptType(Type scriptType)
    {
        if (!IsScriptComponentType(scriptType) || IsLinkedScriptTypeRegistered(scriptType))
        {
            return;
        }

        LinkedScriptTypes.Add(new LinkedScriptTypeRegistration(
            scriptType,
            () => (ScriptComponent)Activator.CreateInstance(scriptType)!,
            GetSerializableFieldInfos(scriptType)));
    }

    private static bool IsScriptComponentType([DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicParameterlessConstructor)] Type type)
    {
        return type.IsClass && !type.IsAbstract && type.GetConstructor(Type.EmptyTypes) != null && typeof(ScriptComponent).IsAssignableFrom(type);
    }

    private static List<ScriptFieldInfo> GetSerializableFields([DynamicallyAccessedMembers(ScriptTypeReflectionMembers)] Type scriptType)
    {
        return GetSerializableFields(scriptType, GetSerializableFieldInfos(scriptType), () => CreateDefaultInstance(scriptType));
    }
#endif

    private static List<ScriptFieldInfo> GetSerializableFields(LinkedScriptTypeRegistration registration)
    {
        return GetSerializableFields(registration.Type, registration.SerializableFields, registration.CreateInstance);
    }

    private static List<ScriptFieldInfo> GetSerializableFields(Type scriptType, IReadOnlyList<FieldInfo> serializableFields, Func<object?> createDefaultInstance)
    {
        List<ScriptFieldInfo> fields = new();
        object? defaultInstance = null;
        try
        {
            defaultInstance = createDefaultInstance();
        }
        catch (Exception exception)
        {
            NativeApi.LogInfo("Failed to create default script instance for field metadata: " + exception.Message);
        }

        foreach (FieldInfo field in serializableFields)
        {
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

    private static List<FieldInfo> GetSerializableFieldInfos([DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicFields)] Type scriptType)
    {
        List<FieldInfo> fields = new();
        foreach (FieldInfo field in scriptType.GetFields(BindingFlags.Instance | BindingFlags.Public))
        {
            if (IsSerializableField(field))
            {
                fields.Add(field);
            }
        }

        fields.Sort((left, right) => string.CompareOrdinal(left.Name, right.Name));
        return fields;
    }

    private static bool IsSerializableField(FieldInfo field)
    {
        return !field.IsStatic && !field.IsInitOnly && !field.IsLiteral && field.GetCustomAttribute<NonSerializedAttribute>() == null;
    }

#if !VENGINE_IOS_NATIVEAOT
    private static object? CreateDefaultInstance([DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicParameterlessConstructor)] Type scriptType)
    {
        return Activator.CreateInstance(scriptType);
    }
#endif

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
        if (type == typeof(bool))
        {
            return false;
        }
        if (type == typeof(int))
        {
            return 0;
        }
        if (type == typeof(float))
        {
            return 0.0F;
        }
        if (type == typeof(Vector3))
        {
            return new Vector3(0.0F, 0.0F, 0.0F);
        }
        if (type == typeof(Color))
        {
            return new Color(0.0F, 0.0F, 0.0F, 1.0F);
        }
        if (type.IsEnum)
        {
            return Enum.ToObject(type, 0);
        }

        return null;
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

    private static bool ApplySerializedFields(ScriptInstanceState state, string serializedFieldsJson)
    {
        if (string.IsNullOrWhiteSpace(serializedFieldsJson))
        {
            return true;
        }

        try
        {
            using JsonDocument document = JsonDocument.Parse(serializedFieldsJson);
            if (document.RootElement.ValueKind == JsonValueKind.Null || document.RootElement.ValueKind == JsonValueKind.Undefined)
            {
                return true;
            }

            if (document.RootElement.ValueKind != JsonValueKind.Object)
            {
                NativeApi.LogInfo("Serialized script fields must be a JSON object.");
                return false;
            }

            foreach (JsonProperty field in document.RootElement.EnumerateObject())
            {
                ApplySerializedField(state, field.Name, field.Value);
            }

            return true;
        }
        catch (JsonException exception)
        {
            NativeApi.LogInfo("Failed to parse serialized script fields: " + exception.Message);
            return false;
        }
    }

    private static bool ApplySerializedField(ScriptInstanceState state, string fieldName, string valueJson)
    {
        if (string.IsNullOrWhiteSpace(fieldName) || string.IsNullOrWhiteSpace(valueJson))
        {
            return false;
        }

        try
        {
            using JsonDocument document = JsonDocument.Parse(valueJson);
            return ApplySerializedField(state, fieldName, document.RootElement);
        }
        catch (JsonException exception)
        {
            NativeApi.LogInfo("Failed to parse serialized script field '" + fieldName + "': " + exception.Message);
            return false;
        }
    }

    private static bool ApplySerializedField(ScriptInstanceState state, string fieldName, JsonElement value)
    {
        FieldInfo? field = FindRuntimeSerializableField(state, fieldName);
        if (field == null)
        {
            return false;
        }

        if (!TryGetScriptFieldKind(field.FieldType, out _) || !TryReadFieldValue(value, field.FieldType, out object? fieldValue))
        {
            NativeApi.LogInfo("Failed to apply serialized script field: " + state.Component.GetType().FullName + "." + fieldName);
            return false;
        }

        field.SetValue(state.Component, fieldValue);
        return true;
    }

    private static IReadOnlyList<FieldInfo> GetRuntimeSerializableFields(ScriptInstanceState state)
    {
#if VENGINE_IOS_NATIVEAOT
        return state.Registration?.SerializableFields ?? Array.Empty<FieldInfo>();
#else
        return GetSerializableFieldInfos(state.Component.GetType());
#endif
    }

    private static FieldInfo? FindRuntimeSerializableField(ScriptInstanceState state, string fieldName)
    {
        foreach (FieldInfo field in GetRuntimeSerializableFields(state))
        {
            if (field.Name == fieldName)
            {
                return field;
            }
        }

        return null;
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
                if (TryReadVector3(value, out Vector3 vector))
                {
                    fieldValue = vector;
                    return true;
                }

                fieldValue = null;
                return false;
            }
            if (type == typeof(Color))
            {
                if (TryReadColor(value, out Color color))
                {
                    fieldValue = color;
                    return true;
                }

                fieldValue = null;
                return false;
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

    private static string WriteScriptTypesJson()
    {
        using MemoryStream stream = new();
        using (Utf8JsonWriter writer = new(stream))
        {
            writer.WriteStartArray();
            foreach (ScriptTypeInfo scriptType in ScriptTypes)
            {
                writer.WriteStartObject();
                writer.WriteString("typeName", scriptType.TypeName);
                writer.WriteString("displayName", scriptType.DisplayName);
                writer.WritePropertyName("fields");
                WriteScriptFieldsMetadataJson(writer, scriptType.Fields);
                writer.WriteEndObject();
            }
            writer.WriteEndArray();
        }

        return Encoding.UTF8.GetString(stream.ToArray());
    }

    private static string WriteSerializableFieldsJson(ScriptInstanceState state)
    {
        using MemoryStream stream = new();
        using (Utf8JsonWriter writer = new(stream))
        {
            writer.WriteStartObject();
            foreach (FieldInfo field in GetRuntimeSerializableFields(state))
            {
                if (!TryGetScriptFieldKind(field.FieldType, out _))
                {
                    continue;
                }

                writer.WritePropertyName(field.Name);
                WriteSupportedFieldValue(writer, NormalizeFieldValue(field.GetValue(state.Component), field.FieldType));
            }
            writer.WriteEndObject();
        }

        return Encoding.UTF8.GetString(stream.ToArray());
    }

    private static void WriteScriptFieldsMetadataJson(Utf8JsonWriter writer, IReadOnlyList<ScriptFieldInfo> fields)
    {
        writer.WriteStartArray();
        foreach (ScriptFieldInfo field in fields)
        {
            writer.WriteStartObject();
            writer.WriteString("name", field.Name);
            writer.WriteString("displayName", field.DisplayName);
            writer.WriteString("kind", field.Kind);
            writer.WriteString("managedTypeName", field.ManagedTypeName);

            writer.WritePropertyName("enumNames");
            writer.WriteStartArray();
            foreach (string enumName in field.EnumNames)
            {
                writer.WriteStringValue(enumName);
            }
            writer.WriteEndArray();

            writer.WritePropertyName("defaultValue");
            WriteSupportedFieldValue(writer, field.DefaultValue);
            writer.WriteEndObject();
        }
        writer.WriteEndArray();
    }

    private static void WriteSupportedFieldValue(Utf8JsonWriter writer, object? value)
    {
        switch (value)
        {
        case null:
            writer.WriteNullValue();
            break;
        case bool boolValue:
            writer.WriteBooleanValue(boolValue);
            break;
        case int intValue:
            writer.WriteNumberValue(intValue);
            break;
        case float floatValue:
            writer.WriteNumberValue(floatValue);
            break;
        case string stringValue:
            writer.WriteStringValue(stringValue);
            break;
        case Vector3 vector:
            writer.WriteStartObject();
            writer.WriteNumber("X", vector.X);
            writer.WriteNumber("Y", vector.Y);
            writer.WriteNumber("Z", vector.Z);
            writer.WriteEndObject();
            break;
        case Color color:
            writer.WriteStartObject();
            writer.WriteNumber("R", color.R);
            writer.WriteNumber("G", color.G);
            writer.WriteNumber("B", color.B);
            writer.WriteNumber("A", color.A);
            writer.WriteEndObject();
            break;
        default:
            writer.WriteStringValue(value.ToString());
            break;
        }
    }

    private static bool TryReadVector3(JsonElement value, out Vector3 vector)
    {
        if (value.ValueKind == JsonValueKind.Object &&
            TryReadFloatProperty(value, "X", "x", out float x) &&
            TryReadFloatProperty(value, "Y", "y", out float y) &&
            TryReadFloatProperty(value, "Z", "z", out float z))
        {
            vector = new Vector3(x, y, z);
            return true;
        }

        vector = new Vector3();
        return false;
    }

    private static bool TryReadColor(JsonElement value, out Color color)
    {
        if (value.ValueKind == JsonValueKind.Object &&
            TryReadFloatProperty(value, "R", "r", out float r) &&
            TryReadFloatProperty(value, "G", "g", out float g) &&
            TryReadFloatProperty(value, "B", "b", out float b))
        {
            float a = 1.0F;
            if (value.TryGetProperty("A", out JsonElement alpha) || value.TryGetProperty("a", out alpha))
            {
                if (!alpha.TryGetSingle(out a))
                {
                    color = new Color();
                    return false;
                }
            }

            color = new Color(r, g, b, a);
            return true;
        }

        color = new Color();
        return false;
    }

    private static bool TryReadFloatProperty(JsonElement value, string primaryName, string fallbackName, out float result)
    {
        if (!value.TryGetProperty(primaryName, out JsonElement property) && !value.TryGetProperty(fallbackName, out property))
        {
            result = 0.0F;
            return false;
        }

        return property.TryGetSingle(out result);
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

    private sealed record ScriptTypeInfo(string TypeName, string DisplayName, IReadOnlyList<ScriptFieldInfo> Fields);

    private sealed record ScriptFieldInfo(string Name,
                                          string DisplayName,
                                          string Kind,
                                          string ManagedTypeName,
                                          IReadOnlyList<string> EnumNames,
                                          object? DefaultValue);

    private sealed class LinkedScriptTypeRegistration
    {
        public LinkedScriptTypeRegistration(Type type, Func<ScriptComponent> createInstance, IReadOnlyList<FieldInfo> serializableFields)
        {
            Type = type;
            TypeName = type.FullName ?? type.Name;
            DisplayName = type.Name;
            CreateInstance = createInstance;
            SerializableFields = serializableFields;
        }

        public Type Type { get; }
        public string TypeName { get; }
        public string DisplayName { get; }
        public Func<ScriptComponent> CreateInstance { get; }
        public IReadOnlyList<FieldInfo> SerializableFields { get; }
    }

    private sealed class ScriptInstanceState
    {
        public ScriptInstanceState(ScriptComponent component, LinkedScriptTypeRegistration? registration)
        {
            Component = component;
            Registration = registration;
        }

        public ScriptComponent Component { get; }
        public LinkedScriptTypeRegistration? Registration { get; }
        public bool LifecycleStarted { get; set; }
    }
}
