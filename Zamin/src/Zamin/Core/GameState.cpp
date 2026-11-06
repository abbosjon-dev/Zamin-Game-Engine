#include "Zamin/Core/GameState.h"
#include "Zamin/Core/Log.h"

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <set>

namespace Zamin {

    namespace {

        struct ListenerEntry
        {
            GameState::ListenerID ID = 0;
            std::string Key;      // "" = every key
            std::shared_ptr<GameState::Listener> Function;
            uint64_t Owner = 0;
        };

        struct State
        {
            std::map<std::string, GameState::Value, std::less<>> Values;
            std::set<std::string, std::less<>> Persistent;
            std::vector<ListenerEntry> Listeners;
            GameState::ListenerID NextListener = 1;
            uint64_t Revision = 0;
        };

        State& Get()
        {
            static State s_State;
            return s_State;
        }

        void Notify(std::string_view key)
        {
            State& state = Get();
            state.Revision++;
            // A copy: a listener may add or remove listeners (its own too) while they are called.
            std::vector<std::pair<GameState::ListenerID, std::shared_ptr<GameState::Listener>>> called;
            for (const ListenerEntry& entry : state.Listeners)
                if (entry.Key.empty() || entry.Key == key)
                    called.emplace_back(entry.ID, entry.Function);
            for (const auto& [id, function] : called)
            {
                const bool alive = std::any_of(state.Listeners.begin(), state.Listeners.end(), [&](const ListenerEntry& e) { return e.ID == id; });
                if (alive && function && *function)
                    (*function)(key);
            }
        }

    }

    // ------------------------------------------------------------------ values
    void GameState::Set(std::string_view key, Value value)
    {
        if (key.empty())
            return;
        State& state = Get();
        auto it = state.Values.find(key);
        if (it != state.Values.end())
        {
            if (it->second == value)
                return; // unchanged: nobody is told
            it->second = std::move(value);
        }
        else
            state.Values.emplace(std::string(key), std::move(value));
        Notify(key);
    }

    void GameState::SetInt(std::string_view key, int value) { Set(key, value); }
    void GameState::SetFloat(std::string_view key, float value) { Set(key, value); }
    void GameState::SetBool(std::string_view key, bool value) { Set(key, value); }
    void GameState::SetString(std::string_view key, std::string_view value) { Set(key, std::string(value)); }

    const GameState::Value* GameState::Find(std::string_view key)
    {
        State& state = Get();
        auto it = state.Values.find(key);
        return it != state.Values.end() ? &it->second : nullptr;
    }

    int GameState::GetInt(std::string_view key, int defaultValue)
    {
        const Value* value = Find(key);
        if (!value)
            return defaultValue;
        if (const int* i = std::get_if<int>(value))
            return *i;
        if (const float* f = std::get_if<float>(value))
            return (int)*f;
        return defaultValue;
    }

    float GameState::GetFloat(std::string_view key, float defaultValue)
    {
        const Value* value = Find(key);
        if (!value)
            return defaultValue;
        if (const float* f = std::get_if<float>(value))
            return *f;
        if (const int* i = std::get_if<int>(value))
            return (float)*i;
        return defaultValue;
    }

    bool GameState::GetBool(std::string_view key, bool defaultValue)
    {
        const Value* value = Find(key);
        const bool* b = value ? std::get_if<bool>(value) : nullptr;
        return b ? *b : defaultValue;
    }

    std::string GameState::GetString(std::string_view key, std::string_view defaultValue)
    {
        const Value* value = Find(key);
        const std::string* s = value ? std::get_if<std::string>(value) : nullptr;
        return s ? *s : std::string(defaultValue);
    }

    int GameState::Increment(std::string_view key, int amount)
    {
        const int value = GetInt(key, 0) + amount;
        SetInt(key, value);
        return value;
    }

    bool GameState::Has(std::string_view key)
    {
        return Find(key) != nullptr;
    }

    SaveValueType GameState::GetType(std::string_view key)
    {
        const Value* value = Find(key);
        if (!value)
            return SaveValueType::None;
        switch (value->index())
        {
            case 0: return SaveValueType::Int;
            case 1: return SaveValueType::Float;
            case 2: return SaveValueType::Bool;
            default: return SaveValueType::String;
        }
    }

    bool GameState::Remove(std::string_view key)
    {
        State& state = Get();
        auto it = state.Values.find(key);
        if (it == state.Values.end())
            return false;
        const std::string removed = it->first;
        state.Values.erase(it);
        Notify(removed);
        return true;
    }

    void GameState::Clear(bool notify)
    {
        State& state = Get();
        if (state.Values.empty())
            return;
        std::vector<std::string> keys;
        for (const auto& [key, value] : state.Values)
            keys.push_back(key);
        state.Values.clear();
        state.Persistent.clear();
        state.Revision++;
        if (notify)
            for (const std::string& key : keys)
                Notify(key);
    }

    std::vector<std::string> GameState::GetKeys()
    {
        std::vector<std::string> keys;
        for (const auto& [key, value] : Get().Values)
            keys.push_back(key);
        return keys;
    }

    size_t GameState::GetCount()
    {
        return Get().Values.size();
    }

    std::string GameState::Format(const Value& value)
    {
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>)
                return v ? "true" : "false";
            else if constexpr (std::is_same_v<T, std::string>)
                return "\"" + v + "\"";
            else if constexpr (std::is_same_v<T, float>)
                return std::format("{:g}", v);
            else
                return std::to_string(v);
        }, value);
    }

    // ------------------------------------------------------------------ persistence
    void GameState::SetPersistent(std::string_view key, bool persistent)
    {
        if (key.empty())
            return;
        State& state = Get();
        if (persistent)
            state.Persistent.emplace(key);
        else if (auto it = state.Persistent.find(key); it != state.Persistent.end())
            state.Persistent.erase(it);
    }

    bool GameState::IsPersistent(std::string_view key)
    {
        const State& state = Get();
        return state.Persistent.find(key) != state.Persistent.end();
    }

    size_t GameState::Save()
    {
        State& state = Get();
        size_t written = 0;
        for (const std::string& key : state.Persistent)
        {
            const std::string saveKey = SaveDataPrefix + key;
            const Value* value = Find(key);
            if (!value)
            {
                SaveData::DeleteKey(saveKey);
                continue;
            }
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int>)
                    SaveData::SetInt(saveKey, v);
                else if constexpr (std::is_same_v<T, float>)
                    SaveData::SetFloat(saveKey, v);
                else if constexpr (std::is_same_v<T, bool>)
                    SaveData::SetBool(saveKey, v);
                else
                    SaveData::SetString(saveKey, v);
            }, *value);
            written++;
        }
        SaveData::Save();
        return written;
    }

    size_t GameState::Load()
    {
        const std::string_view prefix = SaveDataPrefix;
        size_t read = 0;
        for (const std::string& saveKey : SaveData::GetKeys())
        {
            if (!saveKey.starts_with(prefix) || saveKey.size() == prefix.size())
                continue;
            const std::string key = saveKey.substr(prefix.size());
            switch (SaveData::GetType(saveKey))
            {
                case SaveValueType::Int: SetInt(key, SaveData::GetInt(saveKey)); break;
                case SaveValueType::Float: SetFloat(key, SaveData::GetFloat(saveKey)); break;
                case SaveValueType::Bool: SetBool(key, SaveData::GetBool(saveKey)); break;
                case SaveValueType::String: SetString(key, SaveData::GetString(saveKey)); break;
                default: continue;
            }
            SetPersistent(key);
            read++;
        }
        return read;
    }

    // ------------------------------------------------------------------ listeners
    GameState::ListenerID GameState::AddListener(std::string_view key, Listener listener, uint64_t owner)
    {
        State& state = Get();
        ListenerEntry entry;
        entry.ID = state.NextListener++;
        entry.Key = std::string(key);
        entry.Function = std::make_shared<Listener>(std::move(listener));
        entry.Owner = owner;
        state.Listeners.push_back(std::move(entry));
        return state.Listeners.back().ID;
    }

    bool GameState::RemoveListener(ListenerID id)
    {
        auto& listeners = Get().Listeners;
        return std::erase_if(listeners, [&](const ListenerEntry& e) { return e.ID == id; }) > 0;
    }

    size_t GameState::RemoveListeners(uint64_t owner)
    {
        if (!owner)
            return 0;
        auto& listeners = Get().Listeners;
        return std::erase_if(listeners, [&](const ListenerEntry& e) { return e.Owner == owner; });
    }

    void GameState::ClearListeners()
    {
        Get().Listeners.clear();
    }

    size_t GameState::GetListenerCount()
    {
        return Get().Listeners.size();
    }

    uint64_t GameState::GetRevision()
    {
        return Get().Revision;
    }

}
