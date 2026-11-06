#pragma once

#include "Zamin/Core/SaveData.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Zamin {

    // The game's global state: named values (int, float, bool, text) that every script of every
    // scene reads and writes - the score, the lives, which door is open, the chosen character. They
    // live outside the scenes, so scene loads keep them (Services holds C++ objects the same way).
    //
    //     GameState::Increment("Coins");                       // 0 -> 1
    //     int coins = GameState::GetInt("Coins");
    //     GameState::SetBool("Door.Gate.Open", true);
    //     WatchGameState("Coins", [this](std::string_view) { RefreshHud(); }); // in a script
    //
    // In memory only, unless saved: keys marked persistent (SetPersistent) are written to SaveData by
    // Save() (under "GameState.<key>", then SaveData::Save()) and read back by Load() - progress,
    // records, unlocked levels. The editor clears the state when Play starts and stops (each run
    // starts as the game does). Main thread only.
    class GameState
    {
    public:
        using Value = std::variant<int, float, bool, std::string>;
        // Called after a value changed (or was removed: Has(key) is false then); `key` is the key.
        using Listener = std::function<void(std::string_view key)>;
        using ListenerID = uint64_t;
        static constexpr const char* SaveDataPrefix = "GameState.";

        static void SetInt(std::string_view key, int value);
        static void SetFloat(std::string_view key, float value);
        static void SetBool(std::string_view key, bool value);
        static void SetString(std::string_view key, std::string_view value);
        static void Set(std::string_view key, Value value);
        // Numbers convert between int and float on read; other type mismatches give the default.
        static int GetInt(std::string_view key, int defaultValue = 0);
        static float GetFloat(std::string_view key, float defaultValue = 0.0f);
        static bool GetBool(std::string_view key, bool defaultValue = false);
        static std::string GetString(std::string_view key, std::string_view defaultValue = {});
        // Adds to an int (a missing key starts at 0); returns the new value.
        static int Increment(std::string_view key, int amount = 1);
        static bool Has(std::string_view key);
        static SaveValueType GetType(std::string_view key);
        static const Value* Find(std::string_view key);
        static bool Remove(std::string_view key);
        // Every value goes (listeners stay; each hears its keys removed only when notify is set).
        static void Clear(bool notify = false);
        static std::vector<std::string> GetKeys(); // sorted
        static size_t GetCount();
        // The value as text ("12", "0.5", "true", "\"Ali\"") - the editor's view.
        static std::string Format(const Value& value);

        // ---- persistence (SaveData)
        // A persistent key is written by Save() and read by Load() (also before it has a value).
        static void SetPersistent(std::string_view key, bool persistent = true);
        static bool IsPersistent(std::string_view key);
        // Writes the persistent keys to SaveData ("GameState.<key>"; ones removed here are deleted
        // there) and saves the file. Returns how many values were written.
        static size_t Save();
        // Reads every "GameState.*" key from SaveData (they become persistent keys). Returns how many.
        static size_t Load();

        // ---- change listeners ("" = every key). `owner` (optional) groups listeners for
        // RemoveListeners: scripts' WatchGameState uses the script, so they end with it.
        static ListenerID AddListener(std::string_view key, Listener listener, uint64_t owner = 0);
        static bool RemoveListener(ListenerID id);
        static size_t RemoveListeners(uint64_t owner);
        // Every listener goes (the editor before the script module is unloaded).
        static void ClearListeners();
        static size_t GetListenerCount();
        // Grows with every change (hosts notice them).
        static uint64_t GetRevision();
    };

}
