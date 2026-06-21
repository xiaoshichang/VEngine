#pragma once

#include "Engine/Runtime/Render/ShaderManager.h"

namespace ve
{
    namespace RenderShaderNames
    {
        inline constexpr const char* OpaqueSceneVertex = "OpaqueScene.Vertex";
        inline constexpr const char* OpaqueSceneFragment = "OpaqueScene.Fragment";
    } // namespace RenderShaderNames

    namespace RenderShaderIDs
    {
        inline const ShaderID OpaqueSceneVertex{RenderShaderNames::OpaqueSceneVertex, 0};
        inline const ShaderID OpaqueSceneFragment{RenderShaderNames::OpaqueSceneFragment, 0};
    } // namespace RenderShaderIDs
} // namespace ve
