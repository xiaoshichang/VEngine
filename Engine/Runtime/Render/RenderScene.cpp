#include "Engine/Runtime/Render/RenderScene.h"

#include <algorithm>
#include <utility>

namespace ve
{
    RTRenderItem::RTRenderItem(RTRenderItemInitParam initParam)
        : meshResource_(std::move(initParam.meshResource))
        , materialResource_(std::move(initParam.materialResource))
        , boundsCenter_(initParam.boundsCenter)
        , boundsExtents_(initParam.boundsExtents)
        , localToWorld_(initParam.localToWorld)
        , renderItemID_(initParam.renderItemID)
        , castShadows_(initParam.castShadows)
        , receiveShadows_(initParam.receiveShadows)
        , revision_(initParam.revision)
    {
    }

    void RTRenderItem::ApplyUpdateParam(RTRenderItemUpdateParam updateParam)
    {
        if (HasRTRenderItemDirtyFlag(updateParam.dirtyFlags, RTRenderItemDirtyFlags::MeshResource))
        {
            meshResource_ = std::move(updateParam.meshResource);
        }

        if (HasRTRenderItemDirtyFlag(updateParam.dirtyFlags, RTRenderItemDirtyFlags::MaterialResource))
        {
            materialResource_ = std::move(updateParam.materialResource);
        }

        if (HasRTRenderItemDirtyFlag(updateParam.dirtyFlags, RTRenderItemDirtyFlags::Bounds))
        {
            boundsCenter_ = updateParam.boundsCenter;
            boundsExtents_ = updateParam.boundsExtents;
        }

        if (HasRTRenderItemDirtyFlag(updateParam.dirtyFlags, RTRenderItemDirtyFlags::Transform))
        {
            localToWorld_ = updateParam.localToWorld;
        }

        if (HasRTRenderItemDirtyFlag(updateParam.dirtyFlags, RTRenderItemDirtyFlags::Shadows))
        {
            castShadows_ = updateParam.castShadows;
            receiveShadows_ = updateParam.receiveShadows;
        }

        if (HasRTRenderItemDirtyFlag(updateParam.dirtyFlags, RTRenderItemDirtyFlags::Revision))
        {
            renderItemID_ = updateParam.renderItemID;
            revision_ = updateParam.revision;
        }
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

    const Vector3& RTRenderItem::GetBoundsCenter() const noexcept
    {
        return boundsCenter_;
    }

    const Vector3& RTRenderItem::GetBoundsExtents() const noexcept
    {
        return boundsExtents_;
    }

    const Matrix44& RTRenderItem::GetLocalToWorld() const noexcept
    {
        return localToWorld_;
    }

    UInt64 RTRenderItem::GetRenderItemID() const noexcept
    {
        return renderItemID_;
    }

    bool RTRenderItem::CastShadows() const noexcept
    {
        return castShadows_;
    }

    bool RTRenderItem::ReceiveShadows() const noexcept
    {
        return receiveShadows_;
    }

    UInt64 RTRenderItem::GetRevision() const noexcept
    {
        return revision_;
    }

    RTCamera::RTCamera(RTCameraInitParam initParam)
        : projectionMode_(initParam.projectionMode)
        , verticalFieldOfViewRadians_(initParam.verticalFieldOfViewRadians)
        , orthographicSize_(initParam.orthographicSize)
        , automaticAspectRatio_(initParam.automaticAspectRatio)
        , aspectRatio_(initParam.aspectRatio)
        , nearClipPlane_(initParam.nearClipPlane)
        , farClipPlane_(initParam.farClipPlane)
        , clearColor_(initParam.clearColor)
        , localToWorld_(initParam.localToWorld)
    {
    }

    void RTCamera::ApplyUpdateParam(RTCameraUpdateParam updateParam)
    {
        if (HasRTCameraDirtyFlag(updateParam.dirtyFlags, RTCameraDirtyFlags::Projection))
        {
            projectionMode_ = updateParam.projectionMode;
            verticalFieldOfViewRadians_ = updateParam.verticalFieldOfViewRadians;
            orthographicSize_ = updateParam.orthographicSize;
            automaticAspectRatio_ = updateParam.automaticAspectRatio;
            aspectRatio_ = updateParam.aspectRatio;
            nearClipPlane_ = updateParam.nearClipPlane;
            farClipPlane_ = updateParam.farClipPlane;
        }

        if (HasRTCameraDirtyFlag(updateParam.dirtyFlags, RTCameraDirtyFlags::ClearColor))
        {
            clearColor_ = updateParam.clearColor;
        }

        if (HasRTCameraDirtyFlag(updateParam.dirtyFlags, RTCameraDirtyFlags::Transform))
        {
            localToWorld_ = updateParam.localToWorld;
        }
    }

    RTCameraProjectionMode RTCamera::GetProjectionMode() const noexcept
    {
        return projectionMode_;
    }

    Float32 RTCamera::GetVerticalFieldOfViewRadians() const noexcept
    {
        return verticalFieldOfViewRadians_;
    }

    Float32 RTCamera::GetOrthographicSize() const noexcept
    {
        return orthographicSize_;
    }

    bool RTCamera::IsAspectRatioAutomatic() const noexcept
    {
        return automaticAspectRatio_;
    }

    Float32 RTCamera::GetAspectRatio() const noexcept
    {
        return aspectRatio_;
    }

    Float32 RTCamera::GetNearClipPlane() const noexcept
    {
        return nearClipPlane_;
    }

    Float32 RTCamera::GetFarClipPlane() const noexcept
    {
        return farClipPlane_;
    }

    const rhi::RhiColor& RTCamera::GetClearColor() const noexcept
    {
        return clearColor_;
    }

    const Matrix44& RTCamera::GetLocalToWorld() const noexcept
    {
        return localToWorld_;
    }

    const std::shared_ptr<RHIResource>& RTCamera::GetUniformBufferResource() const noexcept
    {
        return uniformBufferResource_;
    }

    void RTCamera::SetUniformBufferResource(std::shared_ptr<RHIResource> resource) noexcept
    {
        uniformBufferResource_ = std::move(resource);
    }

    RTLight::RTLight(RTLightInitParam initParam)
        : type_(initParam.type)
        , color_(initParam.color)
        , direction_(initParam.direction)
        , intensity_(initParam.intensity)
        , range_(initParam.range)
        , innerConeAngleRadians_(initParam.innerConeAngleRadians)
        , outerConeAngleRadians_(initParam.outerConeAngleRadians)
        , castShadows_(initParam.castShadows)
        , shadowDistance_(initParam.shadowDistance)
        , depthBias_(initParam.depthBias)
        , normalBias_(initParam.normalBias)
        , shadowRevision_(initParam.shadowRevision)
        , localToWorld_(initParam.localToWorld)
    {
    }

    void RTLight::ApplyUpdateParam(RTLightUpdateParam updateParam)
    {
        if (HasRTLightDirtyFlag(updateParam.dirtyFlags, RTLightDirtyFlags::Type))
        {
            type_ = updateParam.type;
        }

        if (HasRTLightDirtyFlag(updateParam.dirtyFlags, RTLightDirtyFlags::Color))
        {
            color_ = updateParam.color;
        }

        if (HasRTLightDirtyFlag(updateParam.dirtyFlags, RTLightDirtyFlags::Intensity))
        {
            intensity_ = updateParam.intensity;
        }

        if (HasRTLightDirtyFlag(updateParam.dirtyFlags, RTLightDirtyFlags::Range))
        {
            range_ = updateParam.range;
        }

        if (HasRTLightDirtyFlag(updateParam.dirtyFlags, RTLightDirtyFlags::Cone))
        {
            innerConeAngleRadians_ = updateParam.innerConeAngleRadians;
            outerConeAngleRadians_ = updateParam.outerConeAngleRadians;
        }

        if (HasRTLightDirtyFlag(updateParam.dirtyFlags, RTLightDirtyFlags::Shadows))
        {
            castShadows_ = updateParam.castShadows;
            shadowDistance_ = updateParam.shadowDistance;
            depthBias_ = updateParam.depthBias;
            normalBias_ = updateParam.normalBias;
            shadowRevision_ = updateParam.shadowRevision;
        }

        if (HasRTLightDirtyFlag(updateParam.dirtyFlags, RTLightDirtyFlags::Transform))
        {
            direction_ = updateParam.direction;
            localToWorld_ = updateParam.localToWorld;
        }
    }

    RTLightType RTLight::GetType() const noexcept
    {
        return type_;
    }

    const Vector3& RTLight::GetColor() const noexcept
    {
        return color_;
    }

    const Vector3& RTLight::GetDirection() const noexcept
    {
        return direction_;
    }

    Float32 RTLight::GetIntensity() const noexcept
    {
        return intensity_;
    }

    Float32 RTLight::GetRange() const noexcept
    {
        return range_;
    }

    Float32 RTLight::GetInnerConeAngleRadians() const noexcept
    {
        return innerConeAngleRadians_;
    }

    Float32 RTLight::GetOuterConeAngleRadians() const noexcept
    {
        return outerConeAngleRadians_;
    }

    bool RTLight::CastShadows() const noexcept
    {
        return castShadows_;
    }

    Float32 RTLight::GetShadowDistance() const noexcept
    {
        return shadowDistance_;
    }

    Float32 RTLight::GetDepthBias() const noexcept
    {
        return depthBias_;
    }

    Float32 RTLight::GetNormalBias() const noexcept
    {
        return normalBias_;
    }

    UInt64 RTLight::GetShadowRevision() const noexcept
    {
        return shadowRevision_;
    }

    const Matrix44& RTLight::GetLocalToWorld() const noexcept
    {
        return localToWorld_;
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
            std::find_if(renderItems_.begin(), renderItems_.end(), [&item](const std::shared_ptr<RTRenderItem>& candidate) { return candidate == item; });

        if (existing == renderItems_.end())
        {
            renderItems_.push_back(std::move(item));
        }
    }

    void RTScene::RemoveRenderItem(const std::shared_ptr<RTRenderItem>& item) noexcept
    {
        renderItems_.erase(std::remove(renderItems_.begin(), renderItems_.end(), item), renderItems_.end());
    }

    void RTScene::AddLight(std::shared_ptr<RTLight> light)
    {
        if (light == nullptr)
        {
            return;
        }

        const auto existing = std::find_if(lights_.begin(), lights_.end(), [&light](const std::shared_ptr<RTLight>& candidate) { return candidate == light; });

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
