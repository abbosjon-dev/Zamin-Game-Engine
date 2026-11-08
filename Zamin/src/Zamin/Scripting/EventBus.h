#pragma once

#include "Zamin/Scene/Entity.h"

#include <any>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Zamin {

    /// A named message with an optional payload. Used by the script messages
    /// (SendMessage / Broadcast) and by the scene's event bus (Subscribe / Emit).
    ///
    /// @code
    ///   SendMessage(enemy, "Damage", 25.0f);
    ///   void OnMessage(const ScriptMessage& m) override
    ///   {
    ///       if (m.Name == "Damage") Health -= m.Get<float>();
    ///   }
    /// @endcode
    struct ScriptMessage
    {
        std::string Name;
        Entity Sender;      ///< the entity whose script sent it (invalid when sent by engine / global code)
        std::any Payload;   ///< anything copyable: numbers, strings, glm vectors, Entity, your own structs ...

        bool HasPayload() const { return Payload.has_value(); }
        template<typename T> bool Is() const { return Payload.type() == typeid(T); }
        /// Pointer to the payload when it holds exactly T (no conversions), else nullptr.
        template<typename T> const T* TryGet() const { return std::any_cast<T>(&Payload); }

        /// The payload as T, or `fallback` when it holds something else. Numbers convert
        /// into each other (an int payload can be read as float) and string literals
        /// can be read as std::string.
        template<typename T>
        T Get(T fallback = T{}) const
        {
            if constexpr (std::is_same_v<T, std::string>)
                return GetString(std::move(fallback));
            else
            {
                if (const T* v = std::any_cast<T>(&Payload))
                    return *v;
                if constexpr (std::is_arithmetic_v<T>)
                {
                    if (auto* v = std::any_cast<int>(&Payload))      return static_cast<T>(*v);
                    if (auto* v = std::any_cast<float>(&Payload))    return static_cast<T>(*v);
                    if (auto* v = std::any_cast<double>(&Payload))   return static_cast<T>(*v);
                    if (auto* v = std::any_cast<bool>(&Payload))     return static_cast<T>(*v);
                    if (auto* v = std::any_cast<int64_t>(&Payload))  return static_cast<T>(*v);
                    if (auto* v = std::any_cast<uint32_t>(&Payload)) return static_cast<T>(*v);
                    if (auto* v = std::any_cast<uint64_t>(&Payload)) return static_cast<T>(*v);
                }
                return fallback;
            }
        }

        /// The payload as a string: a std::string, a string literal / char pointer or a std::string_view;
        /// `fallback` otherwise. Out of line (EventBus.cpp): inlined where the message was made with a string
        /// literal, GCC -O2 followed std::any's impossible std::string branch into the literal and warned
        /// (-Warray-bounds) in the caller's code.
        std::string GetString(std::string fallback = {}) const;
    };

    using MessageHandler = std::function<void(const ScriptMessage&)>;
    using SubscriptionID = uint64_t;

    /// Publish / subscribe channel shared by all scripts of a scene (Scene::GetEventBus()).
    /// Senders do not need to know who listens:
    ///
    /// @code
    ///   // UI script
    ///   Subscribe("ScoreChanged", [this](const ScriptMessage& m) { SetText(m.Get<int>()); });
    ///   // anywhere else
    ///   Emit("ScoreChanged", score);
    ///
    /// @endcode
    /// Safe to use from inside handlers: subscribing, unsubscribing and emitting while an
    /// event is being delivered is allowed (new subscribers start with the next event).
    class EventBus
    {
    public:
        /// owner: UUID of the entity whose script owns the subscription (0 = nobody).
        /// The scene removes owned subscriptions when that script is destroyed.
        SubscriptionID Subscribe(std::string eventName, MessageHandler handler, uint64_t owner = 0);
        bool Unsubscribe(SubscriptionID id);
        /// Removes every subscription of `owner`; returns how many were removed.
        int UnsubscribeOwner(uint64_t owner);
        /// Removes all subscriptions and queued events.
        void Clear();
        /// Queued (EmitDeferred) events sent by entities of `scene` lose their sender: the scene is
        /// going away (SceneManager unloading it while the bus lives on).
        void ForgetSender(const Scene* scene);

        /// Calls every handler subscribed to message.Name, in subscription order.
        /// Returns the number of handlers called.
        int Emit(const ScriptMessage& message);
        int Emit(std::string eventName, std::any payload = {}, Entity sender = {});
        /// Queues the event; Flush() delivers the queue (the scene flushes once per frame,
        /// after OnLateUpdate). Events queued during Flush() wait for the next one.
        void EmitDeferred(ScriptMessage message);
        void EmitDeferred(std::string eventName, std::any payload = {}, Entity sender = {});
        int Flush();

        size_t GetSubscriberCount(std::string_view eventName) const;
        bool HasSubscribers(std::string_view eventName) const { return GetSubscriberCount(eventName) > 0; }
        size_t GetSubscriptionCount() const;
        size_t GetQueuedCount() const { return m_Queue.size(); }

        /// Deeper nested Emit() calls are dropped (protects against handler A -> B -> A loops).
        static constexpr int MaxEmitDepth = 32;

    private:
        struct Subscription
        {
            SubscriptionID ID = 0;
            std::string Event;
            uint64_t Owner = 0;
            MessageHandler Handler;
            bool Removed = false;
        };
        struct StringHash
        {
            using is_transparent = void;
            size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
        };

        void Add(Subscription subscription);
        void EndDelivery();

    private:
        std::unordered_map<std::string, std::vector<Subscription>, StringHash, std::equal_to<>> m_Channels;
        /// Subscriptions made while an event is being delivered (merged afterwards, so the
        /// vectors being iterated never reallocate under a running handler).
        std::vector<Subscription> m_Pending;
        std::vector<ScriptMessage> m_Queue;
        SubscriptionID m_NextID = 1;
        int m_Depth = 0;
        bool m_HasRemoved = false;
        bool m_DepthWarned = false;
    };

}
