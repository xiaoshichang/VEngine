#pragma once

#include "Engine/RHI/Common/RhiTypes.h"

#include <memory>
#include <vector>

namespace ve
{
    class RTCamera;
    class RTRenderTexture;
    class RTRenderViewState;
    class RTScene;
    struct RendererRenderTarget
    {
        std::shared_ptr<RTRenderTexture> colorTexture;
        rhi::RhiLoadAction colorLoadAction = rhi::RhiLoadAction::Clear;
    };

    struct RenderView
    {
        std::shared_ptr<RTCamera> camera;
        std::shared_ptr<RTRenderViewState> viewState;
        RendererRenderTarget target;
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
    };

    struct RenderViewFamily
    {
        std::shared_ptr<RTScene> scene;
        std::vector<RenderView> views;
    };
} // namespace ve
