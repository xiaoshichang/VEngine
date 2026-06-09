#include "Engine/Runtime/Scene/Component.h"
#include "Engine/Runtime/Scene/GameObject.h"

#include <algorithm>
#include <utility>

namespace ve
{
    GameObject* Component::GetOwner() noexcept
    {
        return owner_;
    }

    const GameObject* Component::GetOwner() const noexcept
    {
        return owner_;
    }

    bool Component::IsEnabled() const noexcept
    {
        return enabled_;
    }

    void Component::SetEnabled(bool enabled) noexcept
    {
        enabled_ = enabled;
    }

    void Component::OnUpdate(Float32 deltaSeconds)
    {
        static_cast<void>(deltaSeconds);
    }

    void Component::SetOwner(GameObject* owner) noexcept
    {
        owner_ = owner;
    }

    TransformComponent::~TransformComponent()
    {
        ClearChildren();
    }

    const Vector3& TransformComponent::GetLocalPosition() const noexcept
    {
        return localPosition_;
    }

    void TransformComponent::SetLocalPosition(const Vector3& position) noexcept
    {
        localPosition_ = position;
    }

    const Quaternion& TransformComponent::GetLocalRotation() const noexcept
    {
        return localRotation_;
    }

    void TransformComponent::SetLocalRotation(const Quaternion& rotation) noexcept
    {
        localRotation_ = rotation.Normalized();
    }

    const Vector3& TransformComponent::GetLocalScale() const noexcept
    {
        return localScale_;
    }

    void TransformComponent::SetLocalScale(const Vector3& scale) noexcept
    {
        localScale_ = scale;
    }

    Matrix44 TransformComponent::GetLocalMatrix() const noexcept
    {
        return Matrix44::Translation(localPosition_) * localRotation_.ToMatrix44() * Matrix44::Scale(localScale_);
    }

    TransformComponent* TransformComponent::GetParent() noexcept
    {
        return parent_;
    }

    const TransformComponent* TransformComponent::GetParent() const noexcept
    {
        return parent_;
    }

    SizeT TransformComponent::GetChildCount() const noexcept
    {
        return children_.size();
    }

    TransformComponent* TransformComponent::GetChild(SizeT index) noexcept
    {
        GameObject* childGameObject = GetChildGameObject(index);
        if (childGameObject == nullptr)
        {
            return nullptr;
        }

        return childGameObject->GetComponent<TransformComponent>();
    }

    const TransformComponent* TransformComponent::GetChild(SizeT index) const noexcept
    {
        const GameObject* childGameObject = GetChildGameObject(index);
        if (childGameObject == nullptr)
        {
            return nullptr;
        }

        return childGameObject->GetComponent<TransformComponent>();
    }

    GameObject* TransformComponent::GetChildGameObject(SizeT index) noexcept
    {
        if (index >= children_.size())
        {
            return nullptr;
        }

        return children_[index].get();
    }

    const GameObject* TransformComponent::GetChildGameObject(SizeT index) const noexcept
    {
        if (index >= children_.size())
        {
            return nullptr;
        }

        return children_[index].get();
    }

    Result<GameObject*> TransformComponent::CreateChild(std::string name)
    {
        try
        {
            std::unique_ptr<GameObject> child = std::make_unique<GameObject>(std::move(name));
            GameObject* childPointer = child.get();

            TransformComponent* childTransform = childPointer->GetComponent<TransformComponent>();
            VE_ASSERT_MESSAGE(childTransform != nullptr, "GameObject should always own TransformComponent.");
            if (childTransform == nullptr)
            {
                return Result<GameObject*>::Failure(
                    Error(ErrorCode::InvalidState, "GameObject child is missing TransformComponent."));
            }

            childTransform->SetParent(this);
            children_.push_back(std::move(child));
            return Result<GameObject*>::Success(childPointer);
        }
        catch (const std::bad_alloc&)
        {
            return Result<GameObject*>::Failure(Error(ErrorCode::OutOfMemory, "GameObject child allocation failed."));
        }
    }

    bool TransformComponent::DestroyChild(GameObject& child) noexcept
    {
        auto it =
            std::find_if(children_.begin(),
                         children_.end(),
                         [&child](const std::unique_ptr<GameObject>& candidate) { return candidate.get() == &child; });

        if (it == children_.end())
        {
            return false;
        }

        TransformComponent* childTransform = (*it)->GetComponent<TransformComponent>();
        if (childTransform != nullptr)
        {
            childTransform->SetParent(nullptr);
        }

        children_.erase(it);
        return true;
    }

    void TransformComponent::ClearChildren() noexcept
    {
        for (std::unique_ptr<GameObject>& child : children_)
        {
            TransformComponent* childTransform = child->GetComponent<TransformComponent>();
            if (childTransform != nullptr)
            {
                childTransform->SetParent(nullptr);
            }
        }

        children_.clear();
    }

    void TransformComponent::SetParent(TransformComponent* parent) noexcept
    {
        parent_ = parent;
    }

    const std::string& MeshRenderComponent::GetMeshAssetPath() const noexcept
    {
        return meshAssetPath_;
    }

    void MeshRenderComponent::SetMeshAssetPath(std::string meshAssetPath)
    {
        meshAssetPath_ = std::move(meshAssetPath);
    }

    const std::string& MeshRenderComponent::GetMaterialAssetPath() const noexcept
    {
        return materialAssetPath_;
    }

    void MeshRenderComponent::SetMaterialAssetPath(std::string materialAssetPath)
    {
        materialAssetPath_ = std::move(materialAssetPath);
    }

    bool MeshRenderComponent::IsVisible() const noexcept
    {
        return visible_;
    }

    void MeshRenderComponent::SetVisible(bool visible) noexcept
    {
        visible_ = visible;
    }

    bool CameraComponent::IsPrimary() const noexcept
    {
        return primary_;
    }

    void CameraComponent::SetPrimary(bool primary) noexcept
    {
        primary_ = primary;
    }

    Float32 CameraComponent::GetVerticalFieldOfViewRadians() const noexcept
    {
        return verticalFieldOfViewRadians_;
    }

    void CameraComponent::SetVerticalFieldOfViewRadians(Float32 fieldOfViewRadians) noexcept
    {
        verticalFieldOfViewRadians_ = fieldOfViewRadians;
    }

    Float32 CameraComponent::GetNearClipPlane() const noexcept
    {
        return nearClipPlane_;
    }

    void CameraComponent::SetNearClipPlane(Float32 nearClipPlane) noexcept
    {
        nearClipPlane_ = nearClipPlane;
    }

    Float32 CameraComponent::GetFarClipPlane() const noexcept
    {
        return farClipPlane_;
    }

    void CameraComponent::SetFarClipPlane(Float32 farClipPlane) noexcept
    {
        farClipPlane_ = farClipPlane;
    }

    LightType LightComponent::GetLightType() const noexcept
    {
        return type_;
    }

    void LightComponent::SetLightType(LightType type) noexcept
    {
        type_ = type;
    }

    const Vector3& LightComponent::GetColor() const noexcept
    {
        return color_;
    }

    void LightComponent::SetColor(const Vector3& color) noexcept
    {
        color_ = color;
    }

    Float32 LightComponent::GetIntensity() const noexcept
    {
        return intensity_;
    }

    void LightComponent::SetIntensity(Float32 intensity) noexcept
    {
        intensity_ = intensity;
    }
} // namespace ve
