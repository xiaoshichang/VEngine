#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include <vector>

namespace ve
{
    class FrameGraph;
    struct FrameRenderPipelineData;

    /// Frame-local CPU preparation for one VSM-enabled family view.
    ///
    /// FrameGraph tracks the shared backing buffers as whole resources. The slice retains the byte ranges used by this
    /// view, while receiverDepth identifies the exact depth version produced by its DepthPrePass.
    struct VirtualShadowPreparedViewGraphData
    {
        UInt32 viewIndex = 0;
        VirtualShadowFramePacket packet;
        VirtualShadowPageTableSlice slice;
        FrameGraphTextureHandle receiverDepth;
        VirtualShadowViewDrawDiagnostics* drawDiagnostics = nullptr;
    };

    /// Frame-local CPU preparation shared by every VSM view in one renderer family.
    struct VirtualShadowPreparedFamilyGraphData
    {
        VirtualShadowSceneCache* sceneCache = nullptr;
        std::vector<VirtualShadowPreparedViewGraphData> views;
        FrameGraphTextureHandle atlas;
        FrameGraphBufferHandle physicalPages;
        FrameGraphBufferHandle statistics;
    };

    /// Logical handles for one import of every persistent scene-shared VSM resource.
    struct VirtualShadowFrameGraphResources
    {
        FrameGraphTextureHandle atlas;
        FrameGraphBufferHandle pageMarks;
        FrameGraphBufferHandle pageTable;
        FrameGraphBufferHandle requestList;
        FrameGraphBufferHandle requestCounts;
        FrameGraphBufferHandle physicalPages;
        FrameGraphBufferHandle statistics;
        FrameGraphBufferHandle statisticsReadback;
        /// Actual number of entries allocated in physicalPages; it may be lower than the atlas-derived theoretical capacity.
        UInt32 physicalPageCapacity = 0;
    };

    /// Imports the complete scene-shared VSM backing bundle once during FrameGraph setup.
    [[nodiscard]] VirtualShadowFrameGraphResources
    ImportVirtualShadowFrameGraphResources(FrameGraph& frameGraph, VirtualShadowSceneCache& sceneCache, rhi::RhiBuffer& statisticsReadback);
} // namespace ve
