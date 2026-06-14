#include "Engine/Runtime/Render/RenderScene.h"

#include <algorithm>
#include <utility>

namespace ve
{
    RTRenderItem::RTRenderItem(RTRenderItemDesc desc)
        : desc_(std::move(desc))
    {
    }

    const RTRenderItemDesc& RTRenderItem::GetDesc() const noexcept
    {
        return desc_;
    }

    void RTRenderItem::SetDesc(RTRenderItemDesc desc)
    {
        desc_ = std::move(desc);
    }

    const std::shared_ptr<RHIResource>& RTRenderItem::GetMeshResource() const noexcept
    {
        return meshResource_;
    }

    void RTRenderItem::SetMeshResource(std::shared_ptr<RHIResource> resource) noexcept
    {
        meshResource_ = std::move(resource);
    }

    const std::shared_ptr<RHIResource>& RTRenderItem::GetMaterialResource() const noexcept
    {
        return materialResource_;
    }

    void RTRenderItem::SetMaterialResource(std::shared_ptr<RHIResource> resource) noexcept
    {
        materialResource_ = std::move(resource);
    }

    void RTScene::AddRenderItem(std::shared_ptr<RTRenderItem> item)
    {
        if (item == nullptr)
        {
            return;
        }

        const auto existing =
            std::find_if(renderItems_.begin(),
                         renderItems_.end(),
                         [&item](const std::shared_ptr<RTRenderItem>& candidate) { return candidate == item; });

        if (existing == renderItems_.end())
        {
            renderItems_.push_back(std::move(item));
        }
    }

    void RTScene::RemoveRenderItem(const std::shared_ptr<RTRenderItem>& item) noexcept
    {
        renderItems_.erase(std::remove(renderItems_.begin(), renderItems_.end(), item), renderItems_.end());
    }

    void RTScene::Clear() noexcept
    {
        renderItems_.clear();
    }

    SizeT RTScene::GetRenderItemCount() const noexcept
    {
        return renderItems_.size();
    }

    std::shared_ptr<RTRenderItem> RTScene::GetRenderItem(SizeT index) const noexcept
    {
        if (index >= renderItems_.size())
        {
            return nullptr;
        }

        return renderItems_[index];
    }
} // namespace ve
