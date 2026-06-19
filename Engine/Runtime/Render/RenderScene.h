#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Render/RenderResource.h"

#include <memory>
#include <string>
#include <vector>

namespace ve
{
    struct RTRenderItemDesc
    {
        std::shared_ptr<RHIResource> meshResource;
        std::shared_ptr<RHIResource> materialResource;
        Vector3 boundsCenter = Vector3::Zero();
        Vector3 boundsExtents = Vector3::One();
        Matrix44 localToWorld = Matrix44::Identity();
    };

    enum class RTCameraProjectionMode
    {
        Perspective,
        Orthographic,
    };

    struct RTCameraDesc
    {
        bool primary = false;
        RTCameraProjectionMode projectionMode = RTCameraProjectionMode::Perspective;
        Float32 verticalFieldOfViewRadians = 1.0471975512f;
        Float32 orthographicSize = 5.0f;
        Float32 aspectRatio = 1.7777778f;
        Float32 nearClipPlane = 0.1f;
        Float32 farClipPlane = 1000.0f;
        rhi::RhiColor clearColor{0.05f, 0.07f, 0.10f, 1.0f};
        Matrix44 localToWorld = Matrix44::Identity();
    };

    enum class RTLightType
    {
        Directional,
        Point,
    };

    struct RTLightDesc
    {
        RTLightType type = RTLightType::Directional;
        Vector3 color = Vector3::One();
        Vector3 direction = Vector3::UnitZ();
        Float32 intensity = 1.0f;
        Float32 range = 0.0f;
        Float32 innerConeAngleRadians = 0.0f;
        Float32 outerConeAngleRadians = 0.0f;
        bool castShadows = false;
        Matrix44 localToWorld = Matrix44::Identity();
    };

    /// Render-thread representation of one MeshRenderComponent.
    class RTRenderItem final : public NonCopyable
    {
    public:
        explicit RTRenderItem(RTRenderItemDesc desc);

        [[nodiscard]] const RTRenderItemDesc& GetDesc() const noexcept;
        void SetDesc(RTRenderItemDesc desc);

        [[nodiscard]] const std::shared_ptr<RHIResource>& GetMeshResource() const noexcept;
        void SetMeshResource(std::shared_ptr<RHIResource> resource) noexcept;

        [[nodiscard]] const std::shared_ptr<RHIResource>& GetMaterialResource() const noexcept;
        void SetMaterialResource(std::shared_ptr<RHIResource> resource) noexcept;

    private:
        RTRenderItemDesc desc_;
    };

    /// Render-thread representation of one CameraComponent.
    ///
    /// The uniform buffer placeholder is where future camera constant data should live once RHI buffer allocation and
    /// upload scheduling are wired into RenderSystem.
    class RTCamera final : public NonCopyable
    {
    public:
        explicit RTCamera(RTCameraDesc desc);

        [[nodiscard]] const RTCameraDesc& GetDesc() const noexcept;
        void SetDesc(RTCameraDesc desc);

        [[nodiscard]] const std::shared_ptr<RHIResource>& GetUniformBufferResource() const noexcept;
        void SetUniformBufferResource(std::shared_ptr<RHIResource> resource) noexcept;

    private:
        RTCameraDesc desc_;
        std::shared_ptr<RHIResource> uniformBufferResource_;
    };

    /// Render-thread representation of one LightComponent.
    ///
    /// The uniform buffer placeholder is where future light constant data should live once RHI buffer allocation and
    /// upload scheduling are wired into RenderSystem.
    class RTLight final : public NonCopyable
    {
    public:
        explicit RTLight(RTLightDesc desc);

        [[nodiscard]] const RTLightDesc& GetDesc() const noexcept;
        void SetDesc(RTLightDesc desc);

        [[nodiscard]] const std::shared_ptr<RHIResource>& GetUniformBufferResource() const noexcept;
        void SetUniformBufferResource(std::shared_ptr<RHIResource> resource) noexcept;

    private:
        RTLightDesc desc_;
        std::shared_ptr<RHIResource> uniformBufferResource_;
    };

    /// Render-thread scene owned by a Scene and later consumed by renderer/viewports/render textures.
    class RTScene final : public NonCopyable
    {
    public:
        RTScene() = default;
        ~RTScene() = default;

        void AddRenderItem(std::shared_ptr<RTRenderItem> item);
        void RemoveRenderItem(const std::shared_ptr<RTRenderItem>& item) noexcept;
        void AddCamera(std::shared_ptr<RTCamera> camera);
        void RemoveCamera(const std::shared_ptr<RTCamera>& camera) noexcept;
        void AddLight(std::shared_ptr<RTLight> light);
        void RemoveLight(const std::shared_ptr<RTLight>& light) noexcept;
        void Clear() noexcept;

        [[nodiscard]] SizeT GetRenderItemCount() const noexcept;
        [[nodiscard]] std::shared_ptr<RTRenderItem> GetRenderItem(SizeT index) const noexcept;
        [[nodiscard]] SizeT GetCameraCount() const noexcept;
        [[nodiscard]] std::shared_ptr<RTCamera> GetCamera(SizeT index) const noexcept;
        [[nodiscard]] SizeT GetLightCount() const noexcept;
        [[nodiscard]] std::shared_ptr<RTLight> GetLight(SizeT index) const noexcept;

    private:
        std::vector<std::shared_ptr<RTRenderItem>> renderItems_;
        std::vector<std::shared_ptr<RTCamera>> cameras_;
        std::vector<std::shared_ptr<RTLight>> lights_;
    };
} // namespace ve
