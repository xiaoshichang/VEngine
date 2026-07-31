#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Threading/Synchronization.h"

namespace ve
{
    struct VirtualShadowPerformanceStatistics
    {
        bool available = false;
        UInt32 totalPhysicalPages = 0;
        UInt32 allocatedPhysicalPages = 0;
        UInt32 requestedPages = 0;
        UInt32 cachedPages = 0;
        UInt32 newlyAllocatedPages = 0;
        UInt32 redrawnPages = 0;
        UInt32 unmappedPages = 0;
    };

    struct RenderPerformanceStatistics
    {
        UInt64 sourceFrameIndex = 0;
        UInt64 sceneIdentity = 0;
        VirtualShadowPerformanceStatistics virtualShadow;
    };

    class RenderPerformanceStatisticsExchange final : public NonCopyable
    {
    public:
        void ActivateScene(UInt64 sceneIdentity, UInt64 sourceFrameIndex);
        void Publish(const RenderPerformanceStatistics& statistics);
        void Reset();
        [[nodiscard]] RenderPerformanceStatistics GetLatest() const;

    private:
        mutable Mutex mutex_;
        RenderPerformanceStatistics latest_;
        UInt64 activeSceneIdentity_ = 0;
    };
} // namespace ve
