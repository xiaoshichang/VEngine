#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace ve
{
    struct ShaderID
    {
        std::string name;
        Int32 variant = 0;

        [[nodiscard]] bool operator==(const ShaderID& other) const noexcept;
    };

    struct ShaderIDHash
    {
        [[nodiscard]] SizeT operator()(const ShaderID& id) const noexcept;
    };

    class RHIShaderModuleManager final : public NonMovable
    {
    public:
        RHIShaderModuleManager() = default;
        ~RHIShaderModuleManager() = default;

        [[nodiscard]] rhi::RhiShaderModule* GetShaderModule(ShaderID id) noexcept;
        [[nodiscard]] const rhi::RhiShaderModule* GetShaderModule(ShaderID id) const noexcept;
        [[nodiscard]] rhi::RhiShaderModule* TryGetOrCreateShaderModule(rhi::RhiDevice& device, ShaderID id, const rhi::RhiShaderModuleDesc& desc);
        [[nodiscard]] rhi::RhiShaderModule* GetOrCreateShaderModule(rhi::RhiDevice& device, ShaderID id, const rhi::RhiShaderModuleDesc& desc);
        void Clear() noexcept;

    private:
        std::unordered_map<ShaderID, std::unique_ptr<rhi::RhiShaderModule>, ShaderIDHash> shaderModules_;
    };
} // namespace ve
