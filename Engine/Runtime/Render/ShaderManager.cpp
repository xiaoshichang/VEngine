#include "Engine/Runtime/Render/ShaderManager.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <utility>

namespace ve
{
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
        VE_ASSERT_MESSAGE(shader != nullptr, "ShaderManager failed to compile shader.");

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
