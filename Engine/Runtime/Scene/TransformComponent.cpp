#include "Engine/Runtime/Scene/TransformComponent.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Scene/GameObject.h"

#include <algorithm>
#include <new>
#include <utility>

namespace ve
{
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
} // namespace ve
