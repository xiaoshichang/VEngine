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
        return desc_.meshResource;
    }

    void RTRenderItem::SetMeshResource(std::shared_ptr<RHIResource> resource) noexcept
    {
        desc_.meshResource = std::move(resource);
    }

    const std::shared_ptr<RHIResource>& RTRenderItem::GetMaterialResource() const noexcept
    {
        return desc_.materialResource;
    }

    void RTRenderItem::SetMaterialResource(std::shared_ptr<RHIResource> resource) noexcept
    {
        desc_.materialResource = std::move(resource);
    }

    RTCamera::RTCamera(RTCameraDesc desc)
        : desc_(std::move(desc))
    {
    }

    const RTCameraDesc& RTCamera::GetDesc() const noexcept
    {
        return desc_;
    }

    void RTCamera::SetDesc(RTCameraDesc desc)
    {
        desc_ = std::move(desc);
    }

    const std::shared_ptr<RHIResource>& RTCamera::GetUniformBufferResource() const noexcept
    {
        return uniformBufferResource_;
    }

    void RTCamera::SetUniformBufferResource(std::shared_ptr<RHIResource> resource) noexcept
    {
        uniformBufferResource_ = std::move(resource);
    }

    RTLight::RTLight(RTLightDesc desc)
        : desc_(std::move(desc))
    {
    }

    const RTLightDesc& RTLight::GetDesc() const noexcept
    {
        return desc_;
    }

    void RTLight::SetDesc(RTLightDesc desc)
    {
        desc_ = std::move(desc);
    }

    const std::shared_ptr<RHIResource>& RTLight::GetUniformBufferResource() const noexcept
    {
        return uniformBufferResource_;
    }

    void RTLight::SetUniformBufferResource(std::shared_ptr<RHIResource> resource) noexcept
    {
        uniformBufferResource_ = std::move(resource);
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

    void RTScene::AddCamera(std::shared_ptr<RTCamera> camera)
    {
        if (camera == nullptr)
        {
            return;
        }

        const auto existing =
            std::find_if(cameras_.begin(),
                         cameras_.end(),
                         [&camera](const std::shared_ptr<RTCamera>& candidate) { return candidate == camera; });

        if (existing == cameras_.end())
        {
            cameras_.push_back(std::move(camera));
        }
    }

    void RTScene::RemoveCamera(const std::shared_ptr<RTCamera>& camera) noexcept
    {
        cameras_.erase(std::remove(cameras_.begin(), cameras_.end(), camera), cameras_.end());
    }

    void RTScene::AddLight(std::shared_ptr<RTLight> light)
    {
        if (light == nullptr)
        {
            return;
        }

        const auto existing =
            std::find_if(lights_.begin(),
                         lights_.end(),
                         [&light](const std::shared_ptr<RTLight>& candidate) { return candidate == light; });

        if (existing == lights_.end())
        {
            lights_.push_back(std::move(light));
        }
    }

    void RTScene::RemoveLight(const std::shared_ptr<RTLight>& light) noexcept
    {
        lights_.erase(std::remove(lights_.begin(), lights_.end(), light), lights_.end());
    }

    void RTScene::Clear() noexcept
    {
        renderItems_.clear();
        cameras_.clear();
        lights_.clear();
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

    SizeT RTScene::GetCameraCount() const noexcept
    {
        return cameras_.size();
    }

    std::shared_ptr<RTCamera> RTScene::GetCamera(SizeT index) const noexcept
    {
        if (index >= cameras_.size())
        {
            return nullptr;
        }

        return cameras_[index];
    }

    SizeT RTScene::GetLightCount() const noexcept
    {
        return lights_.size();
    }

    std::shared_ptr<RTLight> RTScene::GetLight(SizeT index) const noexcept
    {
        if (index >= lights_.size())
        {
            return nullptr;
        }

        return lights_[index];
    }
} // namespace ve
