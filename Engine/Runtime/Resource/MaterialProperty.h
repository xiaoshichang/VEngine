#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector4.h"
#include "Engine/Runtime/Resource/AssetID.h"

#include <boost/json.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace ve
{
    enum class MaterialPropertyType
    {
        Unknown,
        Float,
        Float4,
        Color,
        Texture2D,
    };

    enum class MaterialPropertyBindingKind
    {
        None,
        ConstantBuffer,
        Texture,
    };

    struct MaterialPropertyBinding
    {
        MaterialPropertyBindingKind kind = MaterialPropertyBindingKind::None;
        std::string buffer;
        std::string field;
        UInt32 offset = 0;
        UInt32 size = 0;
        UInt32 slot = 0;
        UInt32 space = 0;
    };

    struct MaterialPropertyValue
    {
        MaterialPropertyType type = MaterialPropertyType::Unknown;
        Float32 floatValue = 0.0f;
        Vector4 vectorValue = Vector4::Zero();
        AssetID assetValue;
    };

    struct ShaderMaterialPropertyDesc
    {
        std::string name;
        std::string displayName;
        MaterialPropertyType type = MaterialPropertyType::Unknown;
        MaterialPropertyBinding binding;
        MaterialPropertyValue defaultValue;
    };

    struct ShaderMaterialLayout
    {
        std::string constantBufferName = "MaterialConstants";
        UInt32 constantBufferSize = 256;
        std::vector<ShaderMaterialPropertyDesc> properties;
    };

    [[nodiscard]] const char* ToString(MaterialPropertyType type) noexcept;
    [[nodiscard]] MaterialPropertyType ParseMaterialPropertyType(std::string_view text) noexcept;
    [[nodiscard]] bool IsNumericMaterialProperty(MaterialPropertyType type) noexcept;

    [[nodiscard]] boost::json::object WriteAssetIDJson(const AssetID& id);
    [[nodiscard]] Result<AssetID> ReadAssetIDJson(const boost::json::value& value);

    [[nodiscard]] boost::json::value WriteMaterialPropertyValueJson(const MaterialPropertyValue& value);
    [[nodiscard]] Result<MaterialPropertyValue> ReadMaterialPropertyValueJson(MaterialPropertyType type, const boost::json::value& value);

    [[nodiscard]] Result<ShaderMaterialLayout> ReadShaderMaterialLayoutJson(const boost::json::object& object);
    [[nodiscard]] boost::json::object WriteShaderMaterialLayoutJson(const ShaderMaterialLayout& layout);

    [[nodiscard]] Result<std::vector<MaterialPropertyValue>> ResolveMaterialPropertyValues(
        const ShaderMaterialLayout& layout, const boost::json::object& materialProperties);
    [[nodiscard]] boost::json::object WriteMaterialPropertyValuesJson(
        const ShaderMaterialLayout& layout, const std::vector<MaterialPropertyValue>& values);

    [[nodiscard]] std::vector<std::byte> BuildMaterialConstantData(
        const ShaderMaterialLayout& layout, const std::vector<MaterialPropertyValue>& values);
} // namespace ve
