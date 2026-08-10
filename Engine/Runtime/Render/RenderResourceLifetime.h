#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Core/Types.h"

#include <memory>
#include <utility>
#include <vector>

namespace ve
{
    using RhiObjectList = std::vector<std::shared_ptr<rhi::RhiObject>>;

    struct PendingDeleteRTResourceEntry
    {
        UInt64 fenceValue = 0;
        RhiObjectList resources;
    };

    template<typename TObject>
    void MoveRhiObject(RhiObjectList& objects, std::shared_ptr<TObject>& object)
    {
        if (object != nullptr)
        {
            objects.emplace_back(std::move(object));
        }
    }
} // namespace ve
