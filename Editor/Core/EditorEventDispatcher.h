#pragma once

#include "Engine/Runtime/Core/Types.h"

#include <algorithm>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace ve::editor
{
    class EditorEventSubscription
    {
    public:
        EditorEventSubscription() = default;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return eventType_ != nullptr && id_ != 0;
        }

    private:
        friend class EditorEventDispatcher;

        EditorEventSubscription(const std::type_info& eventType, UInt64 id) noexcept
            : eventType_(&eventType)
            , id_(id)
        {
        }

        [[nodiscard]] std::type_index GetEventType() const noexcept
        {
            return std::type_index(*eventType_);
        }

        const std::type_info* eventType_ = nullptr;
        UInt64 id_ = 0;
    };

    class EditorEventDispatcher
    {
    public:
        template<typename Event>
        [[nodiscard]] EditorEventSubscription Subscribe(std::function<void(const Event&)> handler)
        {
            if (!handler)
            {
                return {};
            }

            const UInt64 id = nextSubscriptionID_++;
            HandlerRecord record = {};
            record.id = id;
            record.callback = [handler = std::move(handler)](const void* event) { handler(*static_cast<const Event*>(event)); };

            handlersByEventType_[std::type_index(typeid(Event))].push_back(std::move(record));
            return EditorEventSubscription(typeid(Event), id);
        }

        void Unsubscribe(EditorEventSubscription subscription)
        {
            if (!subscription.IsValid())
            {
                return;
            }

            auto handlersIt = handlersByEventType_.find(subscription.GetEventType());
            if (handlersIt == handlersByEventType_.end())
            {
                return;
            }

            std::vector<HandlerRecord>& handlers = handlersIt->second;
            handlers.erase(
                std::remove_if(handlers.begin(), handlers.end(), [subscription](const HandlerRecord& handler) { return handler.id == subscription.id_; }),
                handlers.end());
            if (handlers.empty())
            {
                handlersByEventType_.erase(handlersIt);
            }
        }

        template<typename Event>
        void Dispatch(const Event& event) const
        {
            const auto handlersIt = handlersByEventType_.find(std::type_index(typeid(Event)));
            if (handlersIt == handlersByEventType_.end())
            {
                return;
            }

            const std::vector<HandlerRecord> handlers = handlersIt->second;
            for (const HandlerRecord& handler : handlers)
            {
                handler.callback(&event);
            }
        }

    private:
        struct HandlerRecord
        {
            UInt64 id = 0;
            std::function<void(const void*)> callback;
        };

        std::unordered_map<std::type_index, std::vector<HandlerRecord>> handlersByEventType_;
        UInt64 nextSubscriptionID_ = 1;
    };
} // namespace ve::editor
