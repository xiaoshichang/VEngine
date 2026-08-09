#include "Engine/Runtime/Resource/ShaderPass.h"

#include "Engine/Runtime/Core/Error.h"

#include <string>

namespace ve
{
    const char* ToString(ShaderPassType type) noexcept
    {
        switch (type)
        {
        case ShaderPassType::DepthOnly:
            return "DepthOnly";
        case ShaderPassType::OpaqueForward:
            return "OpaqueForward";
        case ShaderPassType::TransparentForward:
            return "TransparentForward";
        case ShaderPassType::ShadowCaster:
            return "ShadowCaster";
        case ShaderPassType::Internal:
            return "Internal";
        }
        return "Unknown";
    }

    Result<ShaderPassType> ParseShaderPassType(std::string_view value)
    {
        if (value == "DepthOnly")
        {
            return Result<ShaderPassType>::Success(ShaderPassType::DepthOnly);
        }
        if (value == "OpaqueForward")
        {
            return Result<ShaderPassType>::Success(ShaderPassType::OpaqueForward);
        }
        if (value == "TransparentForward")
        {
            return Result<ShaderPassType>::Success(ShaderPassType::TransparentForward);
        }
        if (value == "ShadowCaster")
        {
            return Result<ShaderPassType>::Success(ShaderPassType::ShadowCaster);
        }
        if (value == "Internal")
        {
            return Result<ShaderPassType>::Success(ShaderPassType::Internal);
        }
        return Result<ShaderPassType>::Failure(Error(ErrorCode::InvalidArgument, "Unknown shader pass '" + std::string(value) + "'."));
    }
}
