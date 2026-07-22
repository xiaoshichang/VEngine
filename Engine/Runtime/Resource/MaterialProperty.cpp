#include "Engine/Runtime/Resource/MaterialProperty.h"

#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/JsonUtils.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace ve
{
    namespace
    {
        [[nodiscard]] Float32 ReadFloat(const boost::json::value& value, Float32 fallback) noexcept
        {
            if (value.is_double())
            {
                return static_cast<Float32>(value.as_double());
            }

            if (value.is_int64())
            {
                return static_cast<Float32>(value.as_int64());
            }

            if (value.is_uint64())
            {
                return static_cast<Float32>(value.as_uint64());
            }

            return fallback;
        }

        [[nodiscard]] UInt32 ReadUInt32(const boost::json::object& object, boost::json::string_view key, UInt32 fallback = 0) noexcept
        {
            const boost::json::value* value = object.if_contains(key);
            if (value == nullptr)
            {
                return fallback;
            }

            if (value->is_uint64())
            {
                return static_cast<UInt32>(value->as_uint64());
            }

            if (value->is_int64() && value->as_int64() >= 0)
            {
                return static_cast<UInt32>(value->as_int64());
            }

            return fallback;
        }

        [[nodiscard]] std::string ReadString(const boost::json::object& object, boost::json::string_view key, std::string fallback = {})
        {
            const boost::json::value* value = object.if_contains(key);
            if (value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }

        [[nodiscard]] MaterialPropertyBindingKind ParseBindingKind(std::string_view text) noexcept
        {
            if (text == "ConstantBuffer")
            {
                return MaterialPropertyBindingKind::ConstantBuffer;
            }

            if (text == "Texture")
            {
                return MaterialPropertyBindingKind::Texture;
            }

            return MaterialPropertyBindingKind::None;
        }

        [[nodiscard]] const char* ToString(MaterialPropertyBindingKind kind) noexcept
        {
            switch (kind)
            {
            case MaterialPropertyBindingKind::ConstantBuffer:
                return "ConstantBuffer";
            case MaterialPropertyBindingKind::Texture:
                return "Texture";
            case MaterialPropertyBindingKind::None:
                break;
            }

            return "None";
        }

        [[nodiscard]] MaterialPropertyValue MakeDefaultValue(MaterialPropertyType type) noexcept
        {
            MaterialPropertyValue value;
            value.type = type;
            if (type == MaterialPropertyType::Color)
            {
                value.vectorValue = Vector4::One();
            }
            return value;
        }

        void CopyBytes(std::vector<std::byte>& bytes, UInt32 offset, const void* source, UInt32 size)
        {
            if (source == nullptr || size == 0 || offset >= bytes.size())
            {
                return;
            }

            const UInt32 writableSize = (std::min)(size, static_cast<UInt32>(bytes.size() - offset));
            std::memcpy(bytes.data() + offset, source, writableSize);
        }
    } // namespace

    const char* ToString(MaterialPropertyType type) noexcept
    {
        switch (type)
        {
        case MaterialPropertyType::Float:
            return "Float";
        case MaterialPropertyType::Float4:
            return "Float4";
        case MaterialPropertyType::Color:
            return "Color";
        case MaterialPropertyType::Texture2D:
            return "Texture2D";
        case MaterialPropertyType::Unknown:
            break;
        }

        return "Unknown";
    }

    MaterialPropertyType ParseMaterialPropertyType(std::string_view text) noexcept
    {
        if (text == "Float")
        {
            return MaterialPropertyType::Float;
        }

        if (text == "Float4")
        {
            return MaterialPropertyType::Float4;
        }

        if (text == "Color")
        {
            return MaterialPropertyType::Color;
        }

        if (text == "Texture2D")
        {
            return MaterialPropertyType::Texture2D;
        }

        return MaterialPropertyType::Unknown;
    }

    bool IsNumericMaterialProperty(MaterialPropertyType type) noexcept
    {
        return type == MaterialPropertyType::Float || type == MaterialPropertyType::Float4 || type == MaterialPropertyType::Color;
    }

    boost::json::object WriteAssetIDJson(const AssetID& id)
    {
        boost::json::object object;
        object["guid"] = id.GetGuid().ToString();
        object["subID"] = id.GetSubID();
        return object;
    }

    Result<AssetID> ReadAssetIDJson(const boost::json::value& value)
    {
        if (value.is_null())
        {
            return Result<AssetID>::Success(AssetID());
        }

        if (!value.is_object())
        {
            return Result<AssetID>::Failure(Error(ErrorCode::InvalidArgument, "AssetID value must be an object."));
        }

        const boost::json::object& object = value.as_object();
        Result<Guid> guid = Guid::Parse(ReadString(object, "guid"));
        if (!guid)
        {
            return Result<AssetID>::Failure(guid.GetError());
        }

        return Result<AssetID>::Success(AssetID(guid.GetValue(), ReadUInt32(object, "subID")));
    }

    boost::json::value WriteMaterialPropertyValueJson(const MaterialPropertyValue& value)
    {
        switch (value.type)
        {
        case MaterialPropertyType::Float:
            return JsonUtils::MakeFloat(value.floatValue);
        case MaterialPropertyType::Float4:
        case MaterialPropertyType::Color:
        {
            boost::json::array array;
            array.emplace_back(JsonUtils::MakeFloat(value.vectorValue.GetX()));
            array.emplace_back(JsonUtils::MakeFloat(value.vectorValue.GetY()));
            array.emplace_back(JsonUtils::MakeFloat(value.vectorValue.GetZ()));
            array.emplace_back(JsonUtils::MakeFloat(value.vectorValue.GetW()));
            return array;
        }
        case MaterialPropertyType::Texture2D:
            return value.assetValue.IsEmpty() ? boost::json::value(nullptr) : boost::json::value(WriteAssetIDJson(value.assetValue));
        case MaterialPropertyType::Unknown:
            break;
        }

        return nullptr;
    }

    Result<MaterialPropertyValue> ReadMaterialPropertyValueJson(MaterialPropertyType type, const boost::json::value& value)
    {
        MaterialPropertyValue result = MakeDefaultValue(type);

        switch (type)
        {
        case MaterialPropertyType::Float:
            result.floatValue = ReadFloat(value, 0.0f);
            return Result<MaterialPropertyValue>::Success(std::move(result));
        case MaterialPropertyType::Float4:
        case MaterialPropertyType::Color:
        {
            if (!value.is_array() || value.as_array().size() < 4)
            {
                return Result<MaterialPropertyValue>::Failure(Error(ErrorCode::InvalidArgument, "Float4 material property must be an array with four values."));
            }

            const boost::json::array& array = value.as_array();
            result.vectorValue =
                Vector4(ReadFloat(array[0], 0.0f), ReadFloat(array[1], 0.0f), ReadFloat(array[2], 0.0f), ReadFloat(array[3], type == MaterialPropertyType::Color ? 1.0f : 0.0f));
            return Result<MaterialPropertyValue>::Success(std::move(result));
        }
        case MaterialPropertyType::Texture2D:
        {
            Result<AssetID> id = ReadAssetIDJson(value);
            if (!id)
            {
                return Result<MaterialPropertyValue>::Failure(id.GetError());
            }

            result.assetValue = id.GetValue();
            return Result<MaterialPropertyValue>::Success(std::move(result));
        }
        case MaterialPropertyType::Unknown:
            break;
        }

        return Result<MaterialPropertyValue>::Failure(Error(ErrorCode::InvalidArgument, "Unsupported material property type."));
    }

    Result<ShaderMaterialLayout> ReadShaderMaterialLayoutJson(const boost::json::object& object)
    {
        ShaderMaterialLayout layout;

        if (const boost::json::value* materialValue = object.if_contains("material"); materialValue != nullptr && materialValue->is_object())
        {
            const boost::json::object& material = materialValue->as_object();
            layout.constantBufferName = ReadString(material, "constantBuffer", layout.constantBufferName);
            layout.constantBufferSize = ReadUInt32(material, "constantBufferSize", layout.constantBufferSize);

            const boost::json::value* propertiesValue = material.if_contains("properties");
            if (propertiesValue != nullptr && propertiesValue->is_array())
            {
                for (const boost::json::value& propertyValue : propertiesValue->as_array())
                {
                    if (!propertyValue.is_object())
                    {
                        return Result<ShaderMaterialLayout>::Failure(Error(ErrorCode::InvalidArgument, "Material property descriptor must be an object."));
                    }

                    const boost::json::object& propertyObject = propertyValue.as_object();
                    ShaderMaterialPropertyDesc property;
                    property.name = ReadString(propertyObject, "name");
                    property.displayName = ReadString(propertyObject, "displayName", property.name);
                    property.type = ParseMaterialPropertyType(ReadString(propertyObject, "type"));
                    if (property.name.empty() || property.type == MaterialPropertyType::Unknown)
                    {
                        return Result<ShaderMaterialLayout>::Failure(Error(ErrorCode::InvalidArgument, "Material property descriptor has invalid name or type."));
                    }

                    property.defaultValue = MakeDefaultValue(property.type);
                    if (const boost::json::value* defaultValue = propertyObject.if_contains("default"); defaultValue != nullptr)
                    {
                        Result<MaterialPropertyValue> parsedDefault = ReadMaterialPropertyValueJson(property.type, *defaultValue);
                        if (!parsedDefault)
                        {
                            return Result<ShaderMaterialLayout>::Failure(parsedDefault.GetError());
                        }
                        property.defaultValue = parsedDefault.GetValue();
                    }

                    if (const boost::json::value* bindingValue = propertyObject.if_contains("binding"); bindingValue != nullptr && bindingValue->is_object())
                    {
                        const boost::json::object& binding = bindingValue->as_object();
                        property.binding.kind = ParseBindingKind(ReadString(binding, "kind"));
                        property.binding.buffer = ReadString(binding, "buffer");
                        property.binding.field = ReadString(binding, "field");
                        property.binding.offset = ReadUInt32(binding, "offset");
                        property.binding.size = ReadUInt32(binding, "size");
                        property.binding.slot = ReadUInt32(binding, "slot");
                        property.binding.space = ReadUInt32(binding, "space");
                    }

                    layout.properties.push_back(std::move(property));
                }
            }
        }

        return Result<ShaderMaterialLayout>::Success(std::move(layout));
    }

    boost::json::object WriteShaderMaterialLayoutJson(const ShaderMaterialLayout& layout)
    {
        boost::json::object material;
        material["constantBuffer"] = layout.constantBufferName;
        material["constantBufferSize"] = layout.constantBufferSize;

        boost::json::array properties;
        for (const ShaderMaterialPropertyDesc& property : layout.properties)
        {
            boost::json::object propertyObject;
            propertyObject["name"] = property.name;
            propertyObject["displayName"] = property.displayName;
            propertyObject["type"] = ToString(property.type);
            propertyObject["default"] = WriteMaterialPropertyValueJson(property.defaultValue);

            boost::json::object binding;
            binding["kind"] = ToString(property.binding.kind);
            binding["buffer"] = property.binding.buffer;
            binding["field"] = property.binding.field;
            binding["offset"] = property.binding.offset;
            binding["size"] = property.binding.size;
            binding["slot"] = property.binding.slot;
            binding["space"] = property.binding.space;
            propertyObject["binding"] = std::move(binding);
            properties.emplace_back(std::move(propertyObject));
        }

        material["properties"] = std::move(properties);
        return material;
    }

    Result<std::vector<MaterialPropertyValue>> ResolveMaterialPropertyValues(const ShaderMaterialLayout& layout, const boost::json::object& materialProperties)
    {
        std::vector<MaterialPropertyValue> values;
        values.reserve(layout.properties.size());

        for (const ShaderMaterialPropertyDesc& property : layout.properties)
        {
            MaterialPropertyValue value = property.defaultValue;
            if (const boost::json::value* propertyValue = materialProperties.if_contains(property.name); propertyValue != nullptr)
            {
                Result<MaterialPropertyValue> parsedValue = ReadMaterialPropertyValueJson(property.type, *propertyValue);
                if (!parsedValue)
                {
                    return Result<std::vector<MaterialPropertyValue>>::Failure(parsedValue.GetError());
                }

                value = parsedValue.GetValue();
            }

            values.push_back(std::move(value));
        }

        return Result<std::vector<MaterialPropertyValue>>::Success(std::move(values));
    }

    boost::json::object WriteMaterialPropertyValuesJson(const ShaderMaterialLayout& layout, const std::vector<MaterialPropertyValue>& values)
    {
        boost::json::object object;
        const SizeT count = (std::min)(layout.properties.size(), values.size());
        for (SizeT index = 0; index < count; ++index)
        {
            object[layout.properties[index].name] = WriteMaterialPropertyValueJson(values[index]);
        }
        return object;
    }

    std::vector<std::byte> BuildMaterialConstantData(const ShaderMaterialLayout& layout, const std::vector<MaterialPropertyValue>& values)
    {
        std::vector<std::byte> bytes(layout.constantBufferSize);
        const SizeT count = (std::min)(layout.properties.size(), values.size());
        for (SizeT index = 0; index < count; ++index)
        {
            const ShaderMaterialPropertyDesc& property = layout.properties[index];
            const MaterialPropertyValue& value = values[index];
            if (property.binding.kind != MaterialPropertyBindingKind::ConstantBuffer)
            {
                continue;
            }

            if (value.type == MaterialPropertyType::Float)
            {
                CopyBytes(bytes, property.binding.offset, &value.floatValue, sizeof(value.floatValue));
            }
            else if (value.type == MaterialPropertyType::Float4 || value.type == MaterialPropertyType::Color)
            {
                const Float32 vector[4] = {value.vectorValue.GetX(), value.vectorValue.GetY(), value.vectorValue.GetZ(), value.vectorValue.GetW()};
                CopyBytes(bytes, property.binding.offset, vector, sizeof(vector));
            }
        }

        return bytes;
    }
} // namespace ve
