#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowFrameGraph.h"

#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"
namespace ve
{
    namespace
    {
        [[nodiscard]] FrameGraphTextureDesc BuildVirtualShadowAtlasDesc(const rhi::RhiTexture& atlas) noexcept
        {
            FrameGraphTextureDesc atlasDesc = {};
            atlasDesc.dimension = atlas.GetDimension();
            atlasDesc.width = atlas.GetWidth();
            atlasDesc.height = atlas.GetHeight();
            atlasDesc.format = atlas.GetFormat();
            atlasDesc.usage =
                static_cast<rhi::RhiTextureUsage>(static_cast<UInt32>(rhi::RhiTextureUsage::Sampled) | static_cast<UInt32>(rhi::RhiTextureUsage::Storage));
            return atlasDesc;
        }
    } // namespace

    VirtualShadowFrameGraphResources
    ImportVirtualShadowFrameGraphResources(FrameGraph& frameGraph, VirtualShadowSceneCache& sceneCache, rhi::RhiBuffer& statisticsReadback)
    {
        rhi::RhiTexture* atlas = sceneCache.GetAtlasTexture();
        rhi::RhiBuffer* pageMarks = sceneCache.GetPageMarksBuffer();
        rhi::RhiBuffer* pageTable = sceneCache.GetPageTableBuffer();
        rhi::RhiBuffer* requestList = sceneCache.GetRequestListBuffer();
        rhi::RhiBuffer* requestCounts = sceneCache.GetRequestCountsBuffer();
        rhi::RhiBuffer* physicalPages = sceneCache.GetPhysicalPagesBuffer();
        rhi::RhiBuffer* statistics = sceneCache.GetStatisticsBuffer();
        if (atlas == nullptr || pageMarks == nullptr || pageTable == nullptr || requestList == nullptr || requestCounts == nullptr ||
            physicalPages == nullptr || statistics == nullptr)
        {
            FailVirtualShadow("VSM frame-graph import requires a complete persistent scene cache.");
        }
        if (atlas->GetFormat() != rhi::RhiFormat::R32Uint)
        {
            FailVirtualShadow("VSM frame-graph import requires an R32Uint physical atlas.");
        }

        VirtualShadowFrameGraphResources resources;
        resources.atlas = frameGraph.ImportTexture("VirtualShadowAtlas", BuildVirtualShadowAtlasDesc(*atlas), {atlas});
        resources.pageMarks = frameGraph.ImportBuffer("VirtualShadowPageMarks", {pageMarks});
        resources.pageTable = frameGraph.ImportBuffer("VirtualShadowPageTable", {pageTable});
        resources.requestList = frameGraph.ImportBuffer("VirtualShadowRequestList", {requestList});
        resources.requestCounts = frameGraph.ImportBuffer("VirtualShadowRequestCounts", {requestCounts});
        resources.physicalPages = frameGraph.ImportBuffer("VirtualShadowPhysicalPages", {physicalPages});
        resources.statistics = frameGraph.ImportBuffer("VirtualShadowStatistics", {statistics});
        resources.statisticsReadback = frameGraph.ImportBuffer("VirtualShadowStatisticsReadback", {&statisticsReadback});
        resources.physicalPageCapacity = sceneCache.GetPhysicalPageCapacity();
        return resources;
    }
} // namespace ve
