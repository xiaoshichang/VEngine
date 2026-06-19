#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <memory>
#include <unordered_map>

namespace ve
{
    using ShaderID = UInt64;

    class ShaderManager final : public NonMovable
    {
    public:
        ShaderManager() = default;
        ~ShaderManager() = default;

        [[nodiscard]] rhi::RhiShaderModule* GetShader(ShaderID id) noexcept;
        [[nodiscard]] const rhi::RhiShaderModule* GetShader(ShaderID id) const noexcept;
        [[nodiscard]] rhi::RhiShaderModule* GetOrCompileShader(rhi::RhiDevice& device, ShaderID id, const rhi::RhiShaderModuleDesc& desc);
        void Clear() noexcept;

    private:
        std::unordered_map<ShaderID, std::unique_ptr<rhi::RhiShaderModule>> shaders_;
    };
} // namespace ve
