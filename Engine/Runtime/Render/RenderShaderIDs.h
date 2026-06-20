#pragma once

#include "Engine/Runtime/Render/ShaderManager.h"

namespace ve
{
    namespace RenderShaderNames
    {
        inline constexpr const char* OpaqueSceneVertex = "OpaqueScene.Vertex";
        inline constexpr const char* OpaqueSceneFragment = "OpaqueScene.Fragment";
        inline constexpr const char* SceneGridVertex = "SceneGrid.Vertex";
        inline constexpr const char* SceneGridFragment = "SceneGrid.Fragment";
    } // namespace RenderShaderNames

    namespace RenderShaderIDs
    {
        inline const ShaderID OpaqueSceneVertex{RenderShaderNames::OpaqueSceneVertex, 0};
        inline const ShaderID OpaqueSceneFragment{RenderShaderNames::OpaqueSceneFragment, 0};
        inline const ShaderID SceneGridVertex{RenderShaderNames::SceneGridVertex, 0};
        inline const ShaderID SceneGridFragment{RenderShaderNames::SceneGridFragment, 0};
    } // namespace RenderShaderIDs
} // namespace ve
