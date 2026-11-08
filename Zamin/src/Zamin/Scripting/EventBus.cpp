#include "Zamin/Scripting/EventBus.h"
#include "Zamin/Core/Log.h"

#include <algorithm>

namespace Zamin {

    std::string ScriptMessage::GetString(std::string fallback) const
    {
        if (const auto* v = std::any_cast<std::string>(&Payload))
            return *v;
        if (const auto* v = std::any_cast<const char*>(&Payload))
            return *v ? std::string(*v) : fallback;
        if (const auto* v = std::any_cast<char*>(&Payload))
            return *v ? std::string(*v) : fallback;
        if (const auto* v = std::any_cast<std::string_view>(&Payload))
            return std::string(*v);
        return fallback;
    }

    SubscriptionID EventBus::Subscribe(std::string eventName, MessageHandler handler, uint64_t owner)
    {
        if (eventName.empty() || !handler)
            return 0;
        Subscription s;
        s.ID = m_NextID++;
        s.Event = std::move(eventName);
        s.Owner = owner;
        s.Handler = std::move(handler);
        SubscriptionID id = s.ID;
        Add(std::move(s));
        return id;
    }

    void EventBus::Add(Subscription subscription)
    {
        if (m_Depth > 0)
            m_Pending.push_back(std::move(subscription));
        else
        {
            auto it = m_Channels.find(std::string_view(subscription.Event));
            if (it == m_Channels.end())
                it = m_Channels.emplace(subscription.Event, std::vector<Subscription>{}).first;
            it->second.push_back(std::move(subscription));
        }
    }

    bool EventBus::Unsubscribe(SubscriptionID id)
    {
        if (id == 0)
            return false;
        if (auto it = std::find_if(m_Pending.begin(), m_Pending.end(), [id](const Subscription& s) { return s.ID == id; });
            it != m_Pending.end())
        {
            m_Pending.erase(it);
            return true;
        }
        for (auto& [name, subs] : m_Channels)
            for (size_t i = 0; i < subs.size(); i++)
            {
                if (subs[i].ID != id || subs[i].Removed)
                    continue;
                if (m_Depth > 0)
                {
                    // A handler may be running right now: only flag it.
                    subs[i].Removed = true;
                    m_HasRemoved = true;
                }
                else
                    subs.erase(subs.begin() + (std::ptrdiff_t)i);
                return true;
            }
        return false;
    }

    int EventBus::UnsubscribeOwner(uint64_t owner)
    {
        if (owner == 0)
            return 0;
        int removed = (int)std::erase_if(m_Pending, [owner](const Subscription& s) { return s.Owner == owner; });
        for (auto& [name, subs] : m_Channels)
        {
            if (m_Depth > 0)
            {
                for (auto& s : subs)
                    if (s.Owner == owner && !s.Removed)
                    {
                        s.Removed = true;
                        m_HasRemoved = true;
                        removed++;
                    }
            }
            else
                removed += (int)std::erase_if(subs, [owner](const Subscription& s) { return s.Owner == owner; });
        }
        return removed;
    }

    void EventBus::ForgetSender(const Scene* scene)
    {
        for (ScriptMessage& message : m_Queue)
            if (message.Sender.GetScene() == scene)
                message.Sender = {};
    }

    void EventBus::Clear()
    {
        m_Pending.clear();
        m_Queue.clear();
        if (m_Depth > 0)
        {
            for (auto& [name, subs] : m_Channels)
                for (auto& s : subs)
                    s.Removed = true;
            m_HasRemoved = true;
        }
        else
            m_Channels.clear();
    }

    int EventBus::Emit(const ScriptMessage& message)
    {
        auto it = m_Channels.find(std::string_view(message.Name));
        if (it == m_Channels.end() || it->second.empty())
            return 0;
        if (m_Depth >= MaxEmitDepth)
        {
            if (!m_DepthWarned)
                ZM_CORE_WARN("Hodisa '{}' juda chuqur ichma-ich chaqirildi ({} daraja) va tashlab yuborildi (cheksiz tsikl?)",
                             message.Name, MaxEmitDepth);
            m_DepthWarned = true;
            return 0;
        }

        // The channel vector does not grow while m_Depth > 0 (new subscriptions are
        // pending), so indexing stays valid even if a handler subscribes or emits.
        std::vector<Subscription>& subs = it->second;
        const size_t count = subs.size();
        int calls = 0;
        struct DepthGuard
        {
            EventBus& Bus;
            explicit DepthGuard(EventBus& bus) : Bus(bus) { Bus.m_Depth++; }
            ~DepthGuard() { if (--Bus.m_Depth == 0) Bus.EndDelivery(); }
        } guard(*this);
        for (size_t i = 0; i < count; i++)
        {
            if (subs[i].Removed)
                continue;
            subs[i].Handler(message);
            calls++;
        }
        return calls;
    }

    int EventBus::Emit(std::string eventName, std::any payload, Entity sender)
    {
        ScriptMessage message{ std::move(eventName), sender, std::move(payload) };
        return Emit(message);
    }

    void EventBus::EndDelivery()
    {
        m_DepthWarned = false;
        if (m_HasRemoved)
        {
            for (auto it = m_Channels.begin(); it != m_Channels.end();)
            {
                std::erase_if(it->second, [](const Subscription& s) { return s.Removed; });
                it = it->second.empty() ? m_Channels.erase(it) : std::next(it);
            }
            m_HasRemoved = false;
        }
        if (!m_Pending.empty())
        {
            std::vector<Subscription> pending = std::move(m_Pending);
            m_Pending.clear();
            for (auto& s : pending)
                Add(std::move(s));
        }
    }

    void EventBus::EmitDeferred(ScriptMessage message)
    {
        if (message.Name.empty())
            return;
        m_Queue.push_back(std::move(message));
    }

    void EventBus::EmitDeferred(std::string eventName, std::any payload, Entity sender)
    {
        EmitDeferred(ScriptMessage{ std::move(eventName), sender, std::move(payload) });
    }

    int EventBus::Flush()
    {
        if (m_Queue.empty())
            return 0;
        std::vector<ScriptMessage> queue = std::move(m_Queue);
        m_Queue.clear();
        int calls = 0;
        for (const ScriptMessage& message : queue)
            calls += Emit(message);
        return calls;
    }

    size_t EventBus::GetSubscriberCount(std::string_view eventName) const
    {
        size_t n = 0;
        if (auto it = m_Channels.find(eventName); it != m_Channels.end())
            n += (size_t)std::count_if(it->second.begin(), it->second.end(), [](const Subscription& s) { return !s.Removed; });
        n += (size_t)std::count_if(m_Pending.begin(), m_Pending.end(), [&](const Subscription& s) { return s.Event == eventName; });
        return n;
    }

    size_t EventBus::GetSubscriptionCount() const
    {
        size_t n = m_Pending.size();
        for (const auto& [name, subs] : m_Channels)
            n += (size_t)std::count_if(subs.begin(), subs.end(), [](const Subscription& s) { return !s.Removed; });
        return n;
    }

}
