#include "Engine/Runtime/Render/RHIShaderModuleManager.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <functional>
#include <string>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] const char* ToString(rhi::RhiBackend backend) noexcept
        {
            switch (backend)
            {
            case rhi::RhiBackend::D3D11:
                return "D3D11";
            case rhi::RhiBackend::D3D12:
                return "D3D12";
            case rhi::RhiBackend::Metal:
                return "Metal";
            }

            return "Unknown";
        }

        [[nodiscard]] const char* ToString(rhi::RhiShaderStage stage) noexcept
        {
            switch (stage)
            {
            case rhi::RhiShaderStage::Vertex:
                return "Vertex";
            case rhi::RhiShaderStage::Fragment:
                return "Fragment";
            case rhi::RhiShaderStage::Compute:
                return "Compute";
            }

            return "Unknown";
        }

        [[nodiscard]] const char* ToString(rhi::RhiShaderCodeFormat format) noexcept
        {
            switch (format)
            {
            case rhi::RhiShaderCodeFormat::Source:
                return "Source";
            case rhi::RhiShaderCodeFormat::Bytecode:
                return "Bytecode";
            }

            return "Unknown";
        }

        [[nodiscard]] std::string BuildShaderModuleCreateFailureMessage(const rhi::RhiDevice& device,
                                                                         const ShaderID& id,
                                                                         const rhi::RhiShaderModuleDesc& desc)
        {
            std::string message = "RHIShaderModuleManager failed to create shader module.";
            message += " name='";
            message += id.name.empty() ? "<empty>" : id.name;
            message += "'";
            message += " variant=" + std::to_string(id.variant);
            message += " debugName='";
            message += desc.debugName != nullptr ? desc.debugName : "<null>";
            message += "'";
            message += " stage=";
            message += ToString(desc.stage);
            message += " entry='";
            message += desc.entryPoint != nullptr ? desc.entryPoint : "<null>";
            message += "'";
            message += " backend=";
            message += ToString(device.GetBackend());
            message += " codeFormat=";
            message += ToString(desc.codeFormat);

            const char* backendError = device.GetLastErrorMessage();
            if (backendError != nullptr && backendError[0] != '\0')
            {
                message += " backendError='";
                message += backendError;
                message += "'";
            }

            return message;
        }
    } // namespace

    bool ShaderID::operator==(const ShaderID& other) const noexcept
    {
        return variant == other.variant && name == other.name;
    }

    SizeT ShaderIDHash::operator()(const ShaderID& id) const noexcept
    {
        const SizeT nameHash = std::hash<std::string>{}(id.name);
        const SizeT variantHash = std::hash<Int32>{}(id.variant);
        return nameHash ^ (variantHash + 0x9E3779B97F4A7C15ull + (nameHash << 6) + (nameHash >> 2));
    }

    rhi::RhiShaderModule* RHIShaderModuleManager::GetShaderModule(ShaderID id) noexcept
    {
        const auto existing = shaderModules_.find(id);
        return existing != shaderModules_.end() ? existing->second.get() : nullptr;
    }

    const rhi::RhiShaderModule* RHIShaderModuleManager::GetShaderModule(ShaderID id) const noexcept
    {
        const auto existing = shaderModules_.find(id);
        return existing != shaderModules_.end() ? existing->second.get() : nullptr;
    }

    rhi::RhiShaderModule*
    RHIShaderModuleManager::TryGetOrCreateShaderModule(rhi::RhiDevice& device, ShaderID id, const rhi::RhiShaderModuleDesc& desc)
    {
        VE_ASSERT_RENDER_THREAD();
        if (rhi::RhiShaderModule* shaderModule = GetShaderModule(id); shaderModule != nullptr)
        {
            return shaderModule;
        }

        std::unique_ptr<rhi::RhiShaderModule> shaderModule = device.CreateShaderModule(desc);
        if (shaderModule == nullptr)
        {
            const std::string message = BuildShaderModuleCreateFailureMessage(device, id, desc);
            VE_LOG_ERROR("{}", message);
            return nullptr;
        }

        rhi::RhiShaderModule* shaderModulePtr = shaderModule.get();
        shaderModules_.emplace(std::move(id), std::move(shaderModule));
        return shaderModulePtr;
    }

    rhi::RhiShaderModule*
    RHIShaderModuleManager::GetOrCreateShaderModule(rhi::RhiDevice& device, ShaderID id, const rhi::RhiShaderModuleDesc& desc)
    {
        rhi::RhiShaderModule* shaderModule = TryGetOrCreateShaderModule(device, id, desc);
        if (shaderModule == nullptr)
        {
            const std::string message = BuildShaderModuleCreateFailureMessage(device, id, desc);
            VE_ASSERT_MESSAGE(shaderModule != nullptr, message.c_str());
        }
        return shaderModule;
    }

    void RHIShaderModuleManager::Clear() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        shaderModules_.clear();
    }
} // namespace ve
