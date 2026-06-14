#include "Engine/Runtime/Scene/TransformComponent.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Scene/GameObject.h"

#include <algorithm>
#include <new>
#include <utility>

namespace ve
{
    TransformComponent::TransformComponent(Scene& scene, GameObject& owner) noexcept
        : Component(scene, owner)
    {
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
        MarkHierarchyDirty();
    }

    const Quaternion& TransformComponent::GetLocalRotation() const noexcept
    {
        return localRotation_;
    }

    void TransformComponent::SetLocalRotation(const Quaternion& rotation) noexcept
    {
        localRotation_ = rotation.Normalized();
        MarkHierarchyDirty();
    }

    const Vector3& TransformComponent::GetLocalScale() const noexcept
    {
        return localScale_;
    }

    void TransformComponent::SetLocalScale(const Vector3& scale) noexcept
    {
        localScale_ = scale;
        MarkHierarchyDirty();
    }

    Matrix44 TransformComponent::GetLocalMatrix() const noexcept
    {
        UpdateWorldCache();
        return localMatrixCache_;
    }

    Matrix44 TransformComponent::GetWorldMatrix() const noexcept
    {
        UpdateWorldCache();
        return worldMatrixCache_;
    }

    UInt64 TransformComponent::GetHierarchyRevision() const noexcept
    {
        UpdateWorldCache();
        return worldRevision_;
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
            std::unique_ptr<GameObject> child = std::make_unique<GameObject>(*GetScene(), std::move(name));
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
        MarkHierarchyDirty();
    }

    void TransformComponent::MarkHierarchyDirty() noexcept
    {
        transformDirty_ = true;
        ++hierarchyRevision_;
        worldRevision_ = 0;
    }

    void TransformComponent::UpdateWorldCache() const noexcept
    {
        const UInt64 parentHierarchyRevision = parent_ != nullptr ? parent_->GetHierarchyRevision() : 0;
        if (!transformDirty_ && cachedParentHierarchyRevision_ == parentHierarchyRevision)
        {
            return;
        }

        localMatrixCache_ = Matrix44::Translation(localPosition_) * localRotation_.ToMatrix44() * Matrix44::Scale(localScale_);
        worldMatrixCache_ = parent_ != nullptr ? parent_->GetWorldMatrix() * localMatrixCache_ : localMatrixCache_;
        cachedParentHierarchyRevision_ = parentHierarchyRevision;
        worldRevision_ = (parentHierarchyRevision * 1315423911ULL) ^ hierarchyRevision_;
        transformDirty_ = false;
    }
} // namespace ve
