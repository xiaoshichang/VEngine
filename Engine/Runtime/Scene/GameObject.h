#pragma once

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/Component.h"
#include "Engine/Runtime/Scene/LightComponent.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/DotnetScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"

#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace ve
{
    class Scene;

    /// Node in a Scene-owned hierarchy.
    ///
    /// GameObject owns a fixed first-stage component set.
    /// Parent-child hierarchy ownership lives on TransformComponent to match Unity-style transform trees.
    class GameObject final : public NonMovable
    {
    public:
        explicit GameObject(Scene& scene);
        GameObject(Scene& scene, std::string name);
        ~GameObject();

        [[nodiscard]] const std::string& GetName() const noexcept;
        void SetName(std::string name);

        [[nodiscard]] SizeT GetComponentCount() const noexcept;
        [[nodiscard]] Component* GetComponent(SizeT index) noexcept;
        [[nodiscard]] const Component* GetComponent(SizeT index) const noexcept;

        template<typename TComponent, typename... TArgs>
        [[nodiscard]] Result<TComponent*> AddComponent(TArgs&&... args)
        {
            static_assert(std::is_base_of_v<Component, TComponent>, "TComponent must derive from ve::Component.");
            static_assert(IsSupportedComponentTypeV<TComponent>,
                          "TComponent must be one of: TransformComponent, MeshRenderComponent, CameraComponent, or "
                          "LightComponent, ScriptableComponent, or DotnetScriptableComponent.");
            static_assert(!std::is_same_v<TComponent, ScriptableComponent>, "Add a concrete script component such as DotnetScriptableComponent.");

            if constexpr (std::is_same_v<TComponent, DotnetScriptableComponent>)
            {
                return AddComponentInternal<TComponent>(true, std::forward<TArgs>(args)...);
            }

            std::unique_ptr<TComponent>* componentSlot = ResolveComponentSlot<TComponent>();
            VE_ASSERT_MESSAGE(componentSlot != nullptr, "componentSlot should not be nullptr");
            if (componentSlot == nullptr)
            {
                return Result<TComponent*>::Failure(Error(ErrorCode::InvalidArgument, "GameObject component slot lookup failed."));
            }

            if (*componentSlot != nullptr)
            {
                return Result<TComponent*>::Failure(Error(ErrorCode::InvalidState, "GameObject already owns this component type."));
            }

            try
            {
                return AddComponentInternal<TComponent>(true, std::forward<TArgs>(args)...);
            }
            catch (const std::bad_alloc&)
            {
                return Result<TComponent*>::Failure(Error(ErrorCode::OutOfMemory, "GameObject component allocation failed."));
            }
        }

        template<typename TComponent>
        [[nodiscard]] TComponent* GetComponent() noexcept
        {
            static_assert(std::is_base_of_v<Component, TComponent>, "TComponent must derive from ve::Component.");
            static_assert(IsSupportedComponentTypeV<TComponent>,
                          "TComponent must be one of: TransformComponent, MeshRenderComponent, CameraComponent, or "
                          "LightComponent, ScriptableComponent, or DotnetScriptableComponent.");

            if constexpr (std::is_same_v<TComponent, ScriptableComponent>)
            {
                return scriptableCmpt_.get();
            }
            else if constexpr (std::is_same_v<TComponent, DotnetScriptableComponent>)
            {
                return dynamic_cast<DotnetScriptableComponent*>(scriptableCmpt_.get());
            }
            else
            {
                std::unique_ptr<TComponent>* componentSlot = ResolveComponentSlot<TComponent>();
                VE_ASSERT_MESSAGE(componentSlot != nullptr, "componentSlot should not be nullptr");
                return componentSlot != nullptr ? componentSlot->get() : nullptr;
            }
        }

        template<typename TComponent>
        [[nodiscard]] const TComponent* GetComponent() const noexcept
        {
            static_assert(std::is_base_of_v<Component, TComponent>, "TComponent must derive from ve::Component.");
            static_assert(IsSupportedComponentTypeV<TComponent>,
                          "TComponent must be one of: TransformComponent, MeshRenderComponent, CameraComponent, or "
                          "LightComponent, ScriptableComponent, or DotnetScriptableComponent.");

            if constexpr (std::is_same_v<TComponent, ScriptableComponent>)
            {
                return scriptableCmpt_.get();
            }
            else if constexpr (std::is_same_v<TComponent, DotnetScriptableComponent>)
            {
                return dynamic_cast<const DotnetScriptableComponent*>(scriptableCmpt_.get());
            }
            else
            {
                const std::unique_ptr<TComponent>* componentSlot = ResolveComponentSlot<TComponent>();
                VE_ASSERT_MESSAGE(componentSlot != nullptr, "componentSlot should not be nullptr");
                return componentSlot != nullptr ? componentSlot->get() : nullptr;
            }
        }

        template<typename TComponent>
        [[nodiscard]] bool RemoveComponent() noexcept
        {
            static_assert(std::is_base_of_v<Component, TComponent>, "TComponent must derive from ve::Component.");
            static_assert(IsSupportedComponentTypeV<TComponent>,
                          "TComponent must be one of: TransformComponent, MeshRenderComponent, CameraComponent, or "
                          "LightComponent, ScriptableComponent, or DotnetScriptableComponent.");

            // Transform drives hierarchy and cannot be removed from a live GameObject.
            if constexpr (std::is_same_v<TComponent, TransformComponent>)
            {
                return false;
            }
            else if constexpr (std::is_same_v<TComponent, ScriptableComponent> || std::is_same_v<TComponent, DotnetScriptableComponent>)
            {
                if (scriptableCmpt_ == nullptr)
                {
                    return false;
                }

                if constexpr (std::is_same_v<TComponent, DotnetScriptableComponent>)
                {
                    if (dynamic_cast<DotnetScriptableComponent*>(scriptableCmpt_.get()) == nullptr)
                    {
                        return false;
                    }
                }

                if (scriptableCmpt_->IsEnabled())
                {
                    scriptableCmpt_->SetEnabled(false);
                }
                scriptableCmpt_->OnDestroy();
                scriptableCmpt_->ClearOwner();
                scriptableCmpt_.reset();
                return true;
            }
            else
            {
                std::unique_ptr<TComponent>* componentSlot = ResolveComponentSlot<TComponent>();
                VE_ASSERT_MESSAGE(componentSlot != nullptr, "componentSlot should not be nullptr");
                if (componentSlot == nullptr || *componentSlot == nullptr)
                {
                    return false;
                }

                if ((*componentSlot)->IsEnabled())
                {
                    (*componentSlot)->SetEnabled(false);
                }
                (*componentSlot)->OnDestroy();

                if constexpr (std::is_same_v<TComponent, MeshRenderComponent>)
                {
                    (*componentSlot)->UnregisterTransformChangedCallback();
                }
                else if constexpr (std::is_same_v<TComponent, CameraComponent>)
                {
                    (*componentSlot)->UnregisterTransformChangedCallback();
                }
                else if constexpr (std::is_same_v<TComponent, LightComponent>)
                {
                    (*componentSlot)->UnregisterTransformChangedCallback();
                }
                (*componentSlot)->ClearOwner();
                componentSlot->reset();
                return true;
            }
        }

        void Update(Float32 deltaSeconds);
        void LateUpdate(Float32 deltaSeconds);

        template<typename TComponent, typename... TArgs>
        [[nodiscard]] Result<TComponent*> AddComponentWithoutRenderRegistration(TArgs&&... args)
        {
            static_assert(std::is_base_of_v<Component, TComponent>, "TComponent must derive from ve::Component.");
            static_assert(IsSupportedComponentTypeV<TComponent>,
                          "TComponent must be one of: TransformComponent, MeshRenderComponent, CameraComponent, or "
                          "LightComponent, ScriptableComponent, or DotnetScriptableComponent.");
            static_assert(!std::is_same_v<TComponent, ScriptableComponent>, "Add a concrete script component such as DotnetScriptableComponent.");

            return AddComponentInternal<TComponent>(false, std::forward<TArgs>(args)...);
        }

    private:
        friend class Scene;
        friend class TransformComponent;

        template<typename TComponent>
        static constexpr bool IsSupportedComponentTypeV = std::is_same_v<TComponent, TransformComponent> || std::is_same_v<TComponent, MeshRenderComponent> ||
                                                          std::is_same_v<TComponent, CameraComponent> || std::is_same_v<TComponent, LightComponent> ||
                                                          std::is_same_v<TComponent, ScriptableComponent> || std::is_same_v<TComponent, DotnetScriptableComponent>;

        template<typename TComponent>
        [[nodiscard]] std::unique_ptr<TComponent>* ResolveComponentSlot() noexcept
        {
            if constexpr (std::is_same_v<TComponent, TransformComponent>)
            {
                return &transformCmpt_;
            }
            else if constexpr (std::is_same_v<TComponent, MeshRenderComponent>)
            {
                return &meshRenderCmpt_;
            }
            else if constexpr (std::is_same_v<TComponent, CameraComponent>)
            {
                return &cameraCmpt_;
            }
            else if constexpr (std::is_same_v<TComponent, LightComponent>)
            {
                return &lightCmpt_;
            }
            else
            {
                return nullptr;
            }
        }

        template<typename TComponent>
        [[nodiscard]] const std::unique_ptr<TComponent>* ResolveComponentSlot() const noexcept
        {
            if constexpr (std::is_same_v<TComponent, TransformComponent>)
            {
                return &transformCmpt_;
            }
            else if constexpr (std::is_same_v<TComponent, MeshRenderComponent>)
            {
                return &meshRenderCmpt_;
            }
            else if constexpr (std::is_same_v<TComponent, CameraComponent>)
            {
                return &cameraCmpt_;
            }
            else if constexpr (std::is_same_v<TComponent, LightComponent>)
            {
                return &lightCmpt_;
            }
            else
            {
                return nullptr;
            }
        }

        void InitializeRequiredComponents();

        template<typename TComponent, typename... TArgs>
        [[nodiscard]] Result<TComponent*> AddComponentInternal(bool registerRenderThread, TArgs&&... args)
        {
            if constexpr (std::is_same_v<TComponent, DotnetScriptableComponent>)
            {
                static_cast<void>(registerRenderThread);
                if (scriptableCmpt_ != nullptr)
                {
                    return Result<TComponent*>::Failure(Error(ErrorCode::InvalidState, "GameObject already owns a scriptable component."));
                }

                try
                {
                    std::unique_ptr<TComponent> component = std::make_unique<TComponent>(*scene_, *this, std::forward<TArgs>(args)...);
                    TComponent* componentPointer = component.get();
                    scriptableCmpt_ = std::move(component);
                    componentPointer->OnCreate();
                    if (componentPointer->IsEnabled())
                    {
                        componentPointer->OnEnable();
                    }
                    return Result<TComponent*>::Success(componentPointer);
                }
                catch (const std::bad_alloc&)
                {
                    return Result<TComponent*>::Failure(Error(ErrorCode::OutOfMemory, "GameObject component allocation failed."));
                }
            }
            else
            {
                std::unique_ptr<TComponent>* componentSlot = ResolveComponentSlot<TComponent>();
                VE_ASSERT_MESSAGE(componentSlot != nullptr, "componentSlot should not be nullptr");
                if (componentSlot == nullptr)
                {
                    return Result<TComponent*>::Failure(Error(ErrorCode::InvalidArgument, "GameObject component slot lookup failed."));
                }

                if (*componentSlot != nullptr)
                {
                    return Result<TComponent*>::Failure(Error(ErrorCode::InvalidState, "GameObject already owns this component type."));
                }

                try
                {
                    std::unique_ptr<TComponent> component = std::make_unique<TComponent>(*scene_, *this, std::forward<TArgs>(args)...);
                    TComponent* componentPointer = component.get();
                    *componentSlot = std::move(component);
                    componentPointer->OnCreate();
                    if (componentPointer->IsEnabled())
                    {
                        componentPointer->OnEnable();
                    }
                    if (registerRenderThread)
                    {
                        if constexpr (std::is_same_v<TComponent, MeshRenderComponent>)
                        {
                            componentPointer->RegisterRenderItemToRenderThread();
                        }
                        else if constexpr (std::is_same_v<TComponent, CameraComponent>)
                        {
                            componentPointer->RegisterCameraToRenderThread();
                        }
                        else if constexpr (std::is_same_v<TComponent, LightComponent>)
                        {
                            componentPointer->RegisterLightToRenderThread();
                        }
                    }
                    return Result<TComponent*>::Success(componentPointer);
                }
                catch (const std::bad_alloc&)
                {
                    return Result<TComponent*>::Failure(Error(ErrorCode::OutOfMemory, "GameObject component allocation failed."));
                }
            }
        }

        std::string name_;
        Scene* scene_ = nullptr;
        std::unique_ptr<TransformComponent> transformCmpt_;
        std::unique_ptr<MeshRenderComponent> meshRenderCmpt_;
        std::unique_ptr<CameraComponent> cameraCmpt_;
        std::unique_ptr<LightComponent> lightCmpt_;
        std::unique_ptr<ScriptableComponent> scriptableCmpt_;
    };
} // namespace ve
