#include "Engine/Runtime/Scripting/IOSAOTScriptingBackend.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/Scripting/Binding/NativeScriptBinding.h"
#include "Engine/Runtime/Scripting/NativeAOTScriptBridgeSymbols.h"

#include <boost/json.hpp>
#include <string_view>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] ScriptFieldKind ParseScriptFieldKind(std::string_view text) noexcept
        {
            if (text == "Bool")
            {
                return ScriptFieldKind::Bool;
            }
            if (text == "Int")
            {
                return ScriptFieldKind::Int;
            }
            if (text == "Float")
            {
                return ScriptFieldKind::Float;
            }
            if (text == "String")
            {
                return ScriptFieldKind::String;
            }
            if (text == "Vector3")
            {
                return ScriptFieldKind::Vector3;
            }
            if (text == "Color")
            {
                return ScriptFieldKind::Color;
            }
            if (text == "Enum")
            {
                return ScriptFieldKind::Enum;
            }

            return ScriptFieldKind::Unsupported;
        }

        [[nodiscard]] ScriptFieldInfo ParseScriptFieldInfo(const boost::json::object& object)
        {
            ScriptFieldInfo info;
            if (const boost::json::value* name = object.if_contains("name"); name != nullptr && name->is_string())
            {
                info.name = std::string(name->as_string());
            }
            if (const boost::json::value* displayName = object.if_contains("displayName"); displayName != nullptr && displayName->is_string())
            {
                info.displayName = std::string(displayName->as_string());
            }
            if (const boost::json::value* kind = object.if_contains("kind"); kind != nullptr && kind->is_string())
            {
                info.kind = ParseScriptFieldKind(std::string_view(kind->as_string().data(), kind->as_string().size()));
            }
            if (const boost::json::value* managedTypeName = object.if_contains("managedTypeName"); managedTypeName != nullptr && managedTypeName->is_string())
            {
                info.managedTypeName = std::string(managedTypeName->as_string());
            }
            if (const boost::json::value* enumNames = object.if_contains("enumNames"); enumNames != nullptr && enumNames->is_array())
            {
                for (const boost::json::value& enumName : enumNames->as_array())
                {
                    if (enumName.is_string())
                    {
                        info.enumNames.emplace_back(enumName.as_string());
                    }
                }
            }
            if (const boost::json::value* defaultValue = object.if_contains("defaultValue"); defaultValue != nullptr)
            {
                info.defaultValueJson = boost::json::serialize(*defaultValue);
            }
            return info;
        }

        [[nodiscard]] ScriptTypeInfo ParseScriptTypeInfo(const boost::json::object& object)
        {
            ScriptTypeInfo info;
            if (const boost::json::value* typeName = object.if_contains("typeName"); typeName != nullptr && typeName->is_string())
            {
                info.typeName = std::string(typeName->as_string());
            }
            if (const boost::json::value* displayName = object.if_contains("displayName"); displayName != nullptr && displayName->is_string())
            {
                info.displayName = std::string(displayName->as_string());
            }
            if (const boost::json::value* fields = object.if_contains("fields"); fields != nullptr && fields->is_array())
            {
                for (const boost::json::value& field : fields->as_array())
                {
                    if (field.is_object())
                    {
                        info.fields.push_back(ParseScriptFieldInfo(field.as_object()));
                    }
                }
            }
            return info;
        }
    } // namespace

    ErrorCode IOSAOTScriptingBackend::Initialize(const ScriptingSystemInitParam& initParam)
    {
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        const bool hasExplicitEntryPoints = initParam.nativeAotEntryPoints != nullptr;
        ManagedScriptEntryPoints resolvedEntryPoints =
            hasExplicitEntryPoints ? *initParam.nativeAotEntryPoints : CreateLinkedNativeAOTScriptEntryPoints();

        if (HasCompleteNativeAOTScriptBridge(resolvedEntryPoints))
        {
            entryPoints_ = resolvedEntryPoints;
            bridgeLoaded_ = true;
            RegisterNativeScriptApi(entryPoints_);
        }
        else if (hasExplicitEntryPoints)
        {
            return ErrorCode::InvalidArgument;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    void IOSAOTScriptingBackend::Shutdown() noexcept
    {
        UnloadProjectAssembly();
        entryPoints_ = {};
        bridgeLoaded_ = false;
        initialized_ = false;
    }

    ScriptingBackendType IOSAOTScriptingBackend::GetBackendType() const noexcept
    {
        return ScriptingBackendType::IOSAOT;
    }

    ErrorCode IOSAOTScriptingBackend::LoadAssembly(const ScriptingAssemblyLoadDesc& desc)
    {
        static_cast<void>(desc);
        if (!initialized_)
        {
            return ErrorCode::InvalidState;
        }

        return bridgeLoaded_ ? ErrorCode::None : ErrorCode::Unsupported;
    }

    ErrorCode IOSAOTScriptingBackend::LoadProjectAssembly(const ScriptingProjectAssemblyLoadDesc& desc)
    {
        if (!initialized_ || !bridgeLoaded_)
        {
            return ErrorCode::InvalidState;
        }

        if (entryPoints_.loadProjectAssembly == nullptr)
        {
            return ErrorCode::None;
        }

        return entryPoints_.loadProjectAssembly(desc.assemblyPath.GetString().c_str()) == 0 ? ErrorCode::None : ErrorCode::InvalidState;
    }

    void IOSAOTScriptingBackend::UnloadProjectAssembly() noexcept
    {
        if (entryPoints_.unloadProjectAssembly != nullptr)
        {
            entryPoints_.unloadProjectAssembly();
        }
    }

    std::vector<ScriptTypeInfo> IOSAOTScriptingBackend::GetAvailableScriptTypes()
    {
        std::vector<ScriptTypeInfo> scriptTypes;
        if (!bridgeLoaded_ || entryPoints_.getScriptTypesJson == nullptr)
        {
            return scriptTypes;
        }

        const char* jsonText = entryPoints_.getScriptTypesJson();
        if (jsonText == nullptr)
        {
            return scriptTypes;
        }

        Result<boost::json::value> json = JsonUtils::Parse(jsonText);
        if (entryPoints_.freeString != nullptr)
        {
            entryPoints_.freeString(jsonText);
        }

        if (!json || !json.GetValue().is_array())
        {
            return scriptTypes;
        }

        for (const boost::json::value& value : json.GetValue().as_array())
        {
            if (value.is_object())
            {
                scriptTypes.push_back(ParseScriptTypeInfo(value.as_object()));
            }
        }

        return scriptTypes;
    }

    Result<ScriptInstanceHandle> IOSAOTScriptingBackend::CreateScriptInstance(const ScriptInstanceDesc& desc)
    {
        if (!initialized_ || !bridgeLoaded_)
        {
            return Result<ScriptInstanceHandle>::Failure(Error(ErrorCode::InvalidState, "iOS NativeAOT script bridge is not loaded."));
        }

        if (desc.component == nullptr || desc.typeName.empty() || entryPoints_.create == nullptr)
        {
            return Result<ScriptInstanceHandle>::Failure(
                Error(ErrorCode::InvalidArgument, "iOS NativeAOT script creation requires a component and type name."));
        }

        const ScriptInstanceHandle handle = entryPoints_.create(desc.component, desc.typeName.c_str(), desc.invokeOnCreate ? 1 : 0);
        if (handle == 0)
        {
            return Result<ScriptInstanceHandle>::Failure(Error(ErrorCode::InvalidState, "iOS NativeAOT script creation returned an empty handle."));
        }

        return Result<ScriptInstanceHandle>::Success(handle);
    }

    void IOSAOTScriptingBackend::DestroyScriptInstance(ScriptInstanceHandle script) noexcept
    {
        if (script != 0 && entryPoints_.destroy != nullptr)
        {
            entryPoints_.destroy(script);
        }
    }

    void IOSAOTScriptingBackend::InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds) noexcept
    {
        if (script == 0)
        {
            return;
        }

        switch (event)
        {
        case ScriptLifecycleEvent::OnCreate:
            if (entryPoints_.createEvent != nullptr)
            {
                entryPoints_.createEvent(script);
            }
            break;
        case ScriptLifecycleEvent::OnDestroy:
            DestroyScriptInstance(script);
            break;
        case ScriptLifecycleEvent::OnUpdate:
            if (entryPoints_.update != nullptr)
            {
                entryPoints_.update(script, deltaSeconds);
            }
            break;
        case ScriptLifecycleEvent::OnLateUpdate:
            if (entryPoints_.lateUpdate != nullptr)
            {
                entryPoints_.lateUpdate(script, deltaSeconds);
            }
            break;
        case ScriptLifecycleEvent::OnEnable:
            if (entryPoints_.enable != nullptr)
            {
                entryPoints_.enable(script);
            }
            break;
        case ScriptLifecycleEvent::OnDisable:
            if (entryPoints_.disable != nullptr)
            {
                entryPoints_.disable(script);
            }
            break;
        }
    }

    Result<std::string> IOSAOTScriptingBackend::GetScriptFieldsJson(ScriptInstanceHandle script)
    {
        if (script == 0 || entryPoints_.getFieldsJson == nullptr)
        {
            return Result<std::string>::Failure(Error(ErrorCode::InvalidState, "iOS NativeAOT script field read requires a valid script instance."));
        }

        const char* jsonText = entryPoints_.getFieldsJson(script);
        if (jsonText == nullptr)
        {
            return Result<std::string>::Failure(Error(ErrorCode::InvalidState, "iOS NativeAOT script bridge failed to read script fields."));
        }

        std::string result(jsonText);
        if (entryPoints_.freeString != nullptr)
        {
            entryPoints_.freeString(jsonText);
        }

        return Result<std::string>::Success(std::move(result));
    }

    ErrorCode IOSAOTScriptingBackend::SetScriptFieldsJson(ScriptInstanceHandle script, std::string_view fieldsJson)
    {
        if (script == 0 || entryPoints_.setFieldsJson == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        std::string fields(fieldsJson);
        return entryPoints_.setFieldsJson(script, fields.c_str()) == 0 ? ErrorCode::None : ErrorCode::InvalidArgument;
    }

    ErrorCode IOSAOTScriptingBackend::SetScriptFieldJson(ScriptInstanceHandle script, std::string_view fieldName, std::string_view valueJson)
    {
        if (script == 0 || entryPoints_.setFieldJson == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        std::string name(fieldName);
        std::string value(valueJson);
        return entryPoints_.setFieldJson(script, name.c_str(), value.c_str()) == 0 ? ErrorCode::None : ErrorCode::InvalidArgument;
    }
} // namespace ve
