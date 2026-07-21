#pragma once

#include "Engine/Runtime/Render/ShaderManager.h"

namespace ve
{
    namespace RenderShaderNames
    {
        inline constexpr const char* OpaqueSceneVertex = "OpaqueScene.Vertex";
        inline constexpr const char* OpaqueSceneFragment = "OpaqueScene.Fragment";
        inline constexpr const char* VirtualShadowClearVertex = "VirtualShadow.Clear.Vertex";
    } // namespace RenderShaderNames

    namespace RenderShaderIDs
    {
        inline const ShaderID OpaqueSceneVertex{RenderShaderNames::OpaqueSceneVertex, 0};
        inline const ShaderID OpaqueSceneFragment{RenderShaderNames::OpaqueSceneFragment, 0};
        inline const ShaderID VirtualShadowClearVertex{RenderShaderNames::VirtualShadowClearVertex, 0};
    } // namespace RenderShaderIDs
} // namespace ve
