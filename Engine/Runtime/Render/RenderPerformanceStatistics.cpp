#include "Engine/Runtime/Render/RenderPerformanceStatistics.h"

namespace ve
{
    void RenderPerformanceStatisticsExchange::ActivateScene(UInt64 sceneIdentity, UInt64 sourceFrameIndex)
    {
        LockGuard lock(mutex_);
        if (sceneIdentity == activeSceneIdentity_)
        {
            return;
        }

        activeSceneIdentity_ = sceneIdentity;
        latest_ = {};
        latest_.sourceFrameIndex = sourceFrameIndex;
        latest_.sceneIdentity = sceneIdentity;
    }

    void RenderPerformanceStatisticsExchange::Publish(const RenderPerformanceStatistics& statistics)
    {
        LockGuard lock(mutex_);
        if (statistics.sceneIdentity != activeSceneIdentity_ ||
            (latest_.virtualShadow.available && statistics.sourceFrameIndex < latest_.sourceFrameIndex))
        {
            return;
        }

        latest_ = statistics;
    }

    void RenderPerformanceStatisticsExchange::Reset()
    {
        LockGuard lock(mutex_);
        activeSceneIdentity_ = 0;
        latest_ = {};
    }

    RenderPerformanceStatistics RenderPerformanceStatisticsExchange::GetLatest() const
    {
        LockGuard lock(mutex_);
        return latest_;
    }
} // namespace ve
