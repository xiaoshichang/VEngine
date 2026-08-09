#pragma once

#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Render/ShaderManager.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ve
{
    class ShaderManager;
    struct ShaderArtifactModule
    {
        rhi::RhiShaderStage stage = rhi::RhiShaderStage::Vertex;
        std::string entryPoint;
        std::vector<std::byte> d3d11Bytecode;
        std::vector<std::byte> d3d12Bytecode;
        std::string metalSource;
    };

    [[nodiscard]] Result<ShaderArtifactModule> LoadShaderArtifact(std::string_view shaderName,
                                                                   std::string_view passName,
                                                                   rhi::RhiShaderStage stage);

    [[nodiscard]] rhi::RhiShaderModule* GetOrCompileShaderArtifact(ShaderManager& shaderManager,
                                                                    rhi::RhiDevice& device,
                                                                    ShaderID id,
                                                                    std::string_view shaderName,
                                                                    std::string_view passName,
                                                                    rhi::RhiShaderStage stage,
                                                                    const char* debugName);
} // namespace ve
