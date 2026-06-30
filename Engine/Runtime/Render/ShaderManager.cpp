#include "Engine/Runtime/Render/ShaderManager.h"

#include "Engine/Runtime/Core/Assert.h"
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

        [[nodiscard]] std::string BuildShaderCompileFailureMessage(const rhi::RhiDevice& device, const ShaderID& id, const rhi::RhiShaderModuleDesc& desc)
        {
            std::string message = "ShaderManager failed to compile shader.";
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

    rhi::RhiShaderModule* ShaderManager::GetShader(ShaderID id) noexcept
    {
        const auto existing = shaders_.find(id);
        return existing != shaders_.end() ? existing->second.get() : nullptr;
    }

    const rhi::RhiShaderModule* ShaderManager::GetShader(ShaderID id) const noexcept
    {
        const auto existing = shaders_.find(id);
        return existing != shaders_.end() ? existing->second.get() : nullptr;
    }

    rhi::RhiShaderModule* ShaderManager::GetOrCompileShader(rhi::RhiDevice& device, ShaderID id, const rhi::RhiShaderModuleDesc& desc)
    {
        VE_ASSERT_RENDER_THREAD();
        if (rhi::RhiShaderModule* shader = GetShader(id); shader != nullptr)
        {
            return shader;
        }

        std::unique_ptr<rhi::RhiShaderModule> shader = device.CreateShaderModule(desc);
        if (shader == nullptr)
        {
            const std::string message = BuildShaderCompileFailureMessage(device, id, desc);
            VE_ASSERT_MESSAGE(shader != nullptr, message.c_str());
            return nullptr;
        }

        rhi::RhiShaderModule* shaderPtr = shader.get();
        shaders_.emplace(id, std::move(shader));
        return shaderPtr;
    }

    void ShaderManager::Clear() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        shaders_.clear();
    }
} // namespace ve
