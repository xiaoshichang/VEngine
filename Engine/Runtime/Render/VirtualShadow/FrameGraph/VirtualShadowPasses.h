#pragma once

#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowFrameGraph.h"

namespace ve
{
    class FrameGraph;

    void AddVirtualShadowStep1_ClearScenePass(FrameGraph& graph, VirtualShadowPreparedFamilyGraphData& family, VirtualShadowFrameGraphResources& resources);

    void AddVirtualShadowStep2_ClearViewRequestsPass(FrameGraph& graph,
                                                     const VirtualShadowPreparedViewGraphData& view,
                                                     VirtualShadowFrameGraphResources& resources,
                                                     bool uavBarrierBeforeExecute);

    void AddVirtualShadowStep3_MarkRequestsPass(FrameGraph& graph,
                                                const VirtualShadowPreparedViewGraphData& view,
                                                VirtualShadowFrameGraphResources& resources,
                                                bool pageMarksBarrierBeforeExecute);

    void AddVirtualShadowStep4_ResolvePageHitsPass(FrameGraph& graph,
                                                   const VirtualShadowPreparedViewGraphData& view,
                                                   VirtualShadowFrameGraphResources& resources,
                                                   bool uavBarrierBeforeExecute,
                                                   bool countPersistedRequests);

    void AddVirtualShadowStep5_AllocatePagesPass(FrameGraph& graph,
                                                 const VirtualShadowPreparedViewGraphData& view,
                                                 VirtualShadowFrameGraphResources& resources,
                                                 bool pageTableBarrierBeforeExecute);

    void AddVirtualShadowStep6_ClearPhysicalPagesPass(FrameGraph& frameGraph,
                                                      const VirtualShadowPreparedViewGraphData& view,
                                                      VirtualShadowFrameGraphResources& resources,
                                                      bool atlasUavBarrierBeforeExecute = false);

    void AddVirtualShadowStep7_RenderCastersPass(FrameGraph& frameGraph,
                                                 const VirtualShadowPreparedViewGraphData& view,
                                                 VirtualShadowFrameGraphResources& resources);

    void
    AddVirtualShadowStep8_MarkRenderedPass(FrameGraph& frameGraph, const VirtualShadowPreparedViewGraphData& view, VirtualShadowFrameGraphResources& resources);

    void
    AddVirtualShadowStep9_FinalizeScenePass(FrameGraph& frameGraph, VirtualShadowPreparedFamilyGraphData& family, VirtualShadowFrameGraphResources& resources);

    void AddVirtualShadowStep10_ReuseStatisticsPass(FrameGraph& frameGraph, VirtualShadowFrameGraphResources& resources);

    void AddVirtualShadowStep11_StatisticsReadbackPass(FrameGraph& frameGraph, VirtualShadowFrameGraphResources& resources);
} // namespace ve
