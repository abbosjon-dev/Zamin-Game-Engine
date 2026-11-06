#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // One mixer group (bus): the sounds routed to it and its child groups are mixed, multiplied by
    // its fader and passed on to its parent. Master is the root every group ends up in.
    struct AudioGroupSettings
    {
        std::string Name;
        std::string Parent;  // "" only for Master
        float Volume = 1.0f; // linear gain of the fader (0 = silent, 1 = unchanged, up to MaxVolume)
        bool Muted = false;
        // While any group is soloed only the soloed groups (with their children and the parents
        // they pass through) are heard. A mixing tool: kept for the session, never saved.
        bool Solo = false;

        bool operator==(const AudioGroupSettings&) const = default;
    };

    // The project's audio defaults (.zproject "Audio"): the mixer groups and when sounds stream.
    struct AudioSettings
    {
        static constexpr const char* Master = "Master";
        static constexpr const char* Music = "Music";
        static constexpr const char* SFX = "SFX";
        static constexpr const char* UI = "UI";
        static constexpr float MaxVolume = 2.0f; // +6 dB

        // Master first, then the groups in the order they were added (built-ins: Music, SFX, UI).
        std::vector<AudioGroupSettings> Groups = DefaultGroups();
        // Files larger than this are streamed from disk instead of being decoded into memory
        // (music always streams; an Audio Source can ask for it with its Stream flag).
        float StreamThresholdMB = 4.0f;

        static std::vector<AudioGroupSettings> DefaultGroups();
        static bool IsBuiltIn(std::string_view name);

        int IndexOf(std::string_view name) const;
        const AudioGroupSettings* Find(std::string_view name) const;
        AudioGroupSettings* Find(std::string_view name);
        // Adds a group under `parent` (Master when it is unknown). False for an empty or taken name.
        bool AddGroup(const std::string& name, const std::string& parent = Master);
        // Removes a group of the project's own (not a built-in one); its children move to its parent.
        bool RemoveGroup(const std::string& name);
        // Moves a group under another one. False for Master, unknown names and when `parent` is the
        // group itself or one of its children (a loop).
        bool SetParent(const std::string& name, const std::string& parent);
        // True when `ancestor` is one of the parents of `name` (not `name` itself).
        bool IsDescendantOf(std::string_view name, std::string_view ancestor) const;
        // The group's children in list order.
        std::vector<std::string> GetChildren(std::string_view name) const;
        // Every group, parents before their children (depth first from Master, list order among
        // siblings) with its depth (Master = 0): the order the mixer window shows them in.
        std::vector<std::pair<std::string, int>> GetTreeOrder() const;

        bool AnySolo() const;
        // The gain the group's own fader applies to what passes through it: its volume, 0 when it
        // is muted or when other groups are soloed and it is not on their path. 0 for unknown names.
        float GetLocalGain(std::string_view name) const;
        // What a sound routed to this group is multiplied by in the mix: the local gains of the
        // group and all its parents up to Master. 0 for unknown names.
        float GetEffectiveGain(std::string_view name) const;

        // Master first, built-ins present, unique non-empty names, every parent known (no loops;
        // otherwise Master), volumes in 0..MaxVolume, threshold positive.
        void Sanitize();
        // Equal to the defaults (Solo is not compared: it is never saved).
        bool IsDefault() const;
        bool EqualsIgnoringSolo(const AudioSettings& other) const;

        // The "Audio" section of the .zproject (Solo is left out).
        std::string ToJson() const;
        // Reads the section (unknown / missing values: defaults), then sanitizes. False for bad JSON.
        static bool FromJson(const std::string& text, AudioSettings& out);

        bool operator==(const AudioSettings&) const = default;
    };

    // Fader helpers: linear gain <-> decibels (gain 0 = -inf, shown as the floor).
    namespace AudioGain {
        constexpr float MinDecibels = -60.0f;
        float ToDecibels(float gain);
        float FromDecibels(float decibels); // <= MinDecibels -> 0
    }

}
