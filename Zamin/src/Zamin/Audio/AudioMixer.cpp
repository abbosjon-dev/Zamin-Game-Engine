#include "Zamin/Audio/AudioMixer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Zamin {

    using json = nlohmann::json;

    // ------------------------------------------------------------------ filters
    float AudioFilterSettings::GetLowPass() const
    {
        return LowPass ? std::clamp(std::isfinite(LowPassCutoff) ? LowPassCutoff : 1000.0f, MinCutoff, MaxCutoff) : 0.0f;
    }

    float AudioFilterSettings::GetHighPass() const
    {
        return HighPass ? std::clamp(std::isfinite(HighPassCutoff) ? HighPassCutoff : 300.0f, MinCutoff, MaxCutoff) : 0.0f;
    }

    void AudioFilterSettings::Sanitize()
    {
        LowPassCutoff = std::isfinite(LowPassCutoff) ? std::clamp(LowPassCutoff, MinCutoff, MaxCutoff) : 1000.0f;
        HighPassCutoff = std::isfinite(HighPassCutoff) ? std::clamp(HighPassCutoff, MinCutoff, MaxCutoff) : 300.0f;
    }

    // ------------------------------------------------------------------ groups
    std::vector<AudioGroupSettings> AudioSettings::DefaultGroups()
    {
        return { { Master, "" }, { Music, Master }, { SFX, Master }, { UI, Master } };
    }

    bool AudioSettings::IsBuiltIn(std::string_view name)
    {
        return name == Master || name == Music || name == SFX || name == UI;
    }

    int AudioSettings::IndexOf(std::string_view name) const
    {
        for (size_t i = 0; i < Groups.size(); i++)
            if (Groups[i].Name == name)
                return (int)i;
        return -1;
    }

    const AudioGroupSettings* AudioSettings::Find(std::string_view name) const
    {
        int i = IndexOf(name);
        return i >= 0 ? &Groups[i] : nullptr;
    }

    AudioGroupSettings* AudioSettings::Find(std::string_view name)
    {
        int i = IndexOf(name);
        return i >= 0 ? &Groups[i] : nullptr;
    }

    bool AudioSettings::AddGroup(const std::string& name, const std::string& parent)
    {
        if (name.empty() || Find(name))
            return false;
        AudioGroupSettings group;
        group.Name = name;
        group.Parent = Find(parent) ? parent : Master;
        Groups.push_back(std::move(group));
        return true;
    }

    bool AudioSettings::RemoveGroup(const std::string& name)
    {
        int index = IndexOf(name);
        if (index < 0 || IsBuiltIn(name))
            return false;
        std::string parent = Groups[index].Parent;
        for (AudioGroupSettings& g : Groups)
            if (g.Parent == name)
                g.Parent = parent;
        Groups.erase(Groups.begin() + index);
        return true;
    }

    bool AudioSettings::SetParent(const std::string& name, const std::string& parent)
    {
        AudioGroupSettings* group = Find(name);
        if (!group || name == Master || !Find(parent) || parent == name || IsDescendantOf(parent, name))
            return false;
        group->Parent = parent;
        return true;
    }

    bool AudioSettings::IsDescendantOf(std::string_view name, std::string_view ancestor) const
    {
        const AudioGroupSettings* g = Find(name);
        // At most one step per group: a broken (looping) list cannot hang here.
        for (size_t steps = 0; g && !g->Parent.empty() && steps <= Groups.size(); steps++)
        {
            if (g->Parent == ancestor)
                return true;
            g = Find(g->Parent);
        }
        return false;
    }

    std::vector<std::string> AudioSettings::GetChildren(std::string_view name) const
    {
        std::vector<std::string> children;
        for (const AudioGroupSettings& g : Groups)
            if (g.Parent == name && g.Name != name)
                children.push_back(g.Name);
        return children;
    }

    std::vector<std::pair<std::string, int>> AudioSettings::GetTreeOrder() const
    {
        std::vector<std::pair<std::string, int>> order;
        std::function<void(const std::string&, int)> visit = [&](const std::string& name, int depth) {
            if (std::any_of(order.begin(), order.end(), [&](const auto& o) { return o.first == name; }))
                return; // a loop in an unsanitized list
            order.emplace_back(name, depth);
            for (const std::string& child : GetChildren(name))
                visit(child, depth + 1);
        };
        if (Find(Master))
            visit(Master, 0);
        // Groups not reachable from Master (only in an unsanitized list): listed at the end.
        for (const AudioGroupSettings& g : Groups)
            if (std::none_of(order.begin(), order.end(), [&](const auto& o) { return o.first == g.Name; }))
                order.emplace_back(g.Name, 1);
        return order;
    }

    bool AudioSettings::AnySolo() const
    {
        return std::any_of(Groups.begin(), Groups.end(), [](const AudioGroupSettings& g) { return g.Solo; });
    }

    float AudioSettings::GetLocalGain(std::string_view name) const
    {
        const AudioGroupSettings* group = Find(name);
        if (!group || group->Muted)
            return 0.0f;
        if (AnySolo() && !group->Solo)
        {
            // On a soloed group's path: one of its children (they are part of it) or one of the
            // parents its sound passes through.
            bool onPath = false;
            for (const AudioGroupSettings& g : Groups)
                if (g.Solo && (IsDescendantOf(name, g.Name) || IsDescendantOf(g.Name, name)))
                {
                    onPath = true;
                    break;
                }
            if (!onPath)
                return 0.0f;
        }
        return std::clamp(group->Volume, 0.0f, MaxVolume);
    }

    float AudioSettings::GetEffectiveGain(std::string_view name) const
    {
        const AudioGroupSettings* g = Find(name);
        if (!g)
            return 0.0f;
        float gain = 1.0f;
        for (size_t steps = 0; g && steps <= Groups.size(); steps++)
        {
            gain *= GetLocalGain(g->Name);
            if (g->Parent.empty())
                break;
            g = Find(g->Parent);
        }
        return gain;
    }

    void AudioSettings::Sanitize()
    {
        // Unique, non-empty names (the first one of a name wins).
        std::vector<AudioGroupSettings> groups;
        for (AudioGroupSettings& g : Groups)
            if (!g.Name.empty() && std::none_of(groups.begin(), groups.end(), [&](const AudioGroupSettings& o) { return o.Name == g.Name; }))
                groups.push_back(std::move(g));
        // The built-ins exist, Master first.
        for (const AudioGroupSettings& builtIn : DefaultGroups())
            if (std::none_of(groups.begin(), groups.end(), [&](const AudioGroupSettings& o) { return o.Name == builtIn.Name; }))
                groups.push_back(builtIn);
        auto master = std::find_if(groups.begin(), groups.end(), [](const AudioGroupSettings& g) { return g.Name == Master; });
        std::rotate(groups.begin(), master, master + 1);
        Groups = std::move(groups);

        Groups[0].Parent.clear();
        for (size_t i = 1; i < Groups.size(); i++)
        {
            AudioGroupSettings& g = Groups[i];
            if (g.Parent.empty() || g.Parent == g.Name || !Find(g.Parent))
                g.Parent = Master;
        }
        // Loops (A under B under A): broken by moving the group under Master.
        for (size_t i = 1; i < Groups.size(); i++)
        {
            std::vector<std::string> seen = { Groups[i].Name };
            const AudioGroupSettings* g = &Groups[i];
            while (g && !g->Parent.empty())
            {
                if (std::find(seen.begin(), seen.end(), g->Parent) != seen.end())
                {
                    Groups[i].Parent = Master;
                    break;
                }
                seen.push_back(g->Parent);
                g = Find(g->Parent);
            }
        }
        for (AudioGroupSettings& g : Groups)
        {
            g.Volume = std::isfinite(g.Volume) ? std::clamp(g.Volume, 0.0f, MaxVolume) : 1.0f;
            g.Filter.Sanitize();
        }
        if (!std::isfinite(StreamThresholdMB) || StreamThresholdMB <= 0.0f)
            StreamThresholdMB = 4.0f;
        StreamThresholdMB = std::clamp(StreamThresholdMB, 0.01f, 4096.0f);
        MaxVoices = std::clamp(MaxVoices, 1, MaxVoicesLimit);
        DopplerFactor = std::isfinite(DopplerFactor) ? std::clamp(DopplerFactor, 0.0f, MaxDopplerFactor) : 1.0f;
    }

    bool AudioSettings::EqualsIgnoringSolo(const AudioSettings& other) const
    {
        if (Groups.size() != other.Groups.size() || StreamThresholdMB != other.StreamThresholdMB || MaxVoices != other.MaxVoices ||
            DopplerFactor != other.DopplerFactor)
            return false;
        for (size_t i = 0; i < Groups.size(); i++)
        {
            const AudioGroupSettings& a = Groups[i];
            const AudioGroupSettings& b = other.Groups[i];
            if (a.Name != b.Name || a.Parent != b.Parent || a.Volume != b.Volume || a.Muted != b.Muted || !(a.Filter == b.Filter))
                return false;
        }
        return true;
    }

    bool AudioSettings::IsDefault() const { return EqualsIgnoringSolo(AudioSettings()); }

    std::string AudioSettings::ToJson() const
    {
        json groups = json::array();
        for (const AudioGroupSettings& g : Groups)
        {
            json item = { { "Name", g.Name }, { "Volume", g.Volume } };
            if (!g.Parent.empty())
                item["Parent"] = g.Parent;
            if (g.Muted)
                item["Muted"] = true;
            // Only a filter that was set up (older files stay the same).
            if (!g.Filter.IsDefault())
                item["Filter"] = { { "LowPass", g.Filter.LowPass }, { "LowPassCutoff", g.Filter.LowPassCutoff },
                                   { "HighPass", g.Filter.HighPass }, { "HighPassCutoff", g.Filter.HighPassCutoff } };
            groups.push_back(std::move(item));
        }
        json j = { { "StreamThresholdMB", StreamThresholdMB }, { "MaxVoices", MaxVoices }, { "DopplerFactor", DopplerFactor }, { "Groups", groups } };
        return j.dump(4);
    }

    bool AudioSettings::FromJson(const std::string& text, AudioSettings& out)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        AudioSettings settings;
        if (auto it = j.find("StreamThresholdMB"); it != j.end() && it->is_number())
            settings.StreamThresholdMB = it->get<float>();
        if (auto it = j.find("MaxVoices"); it != j.end() && it->is_number())
            settings.MaxVoices = (int)std::clamp(it->get<double>(), 1.0, (double)MaxVoicesLimit);
        if (auto it = j.find("DopplerFactor"); it != j.end() && it->is_number())
            settings.DopplerFactor = it->get<float>();
        if (auto it = j.find("Groups"); it != j.end() && it->is_array())
        {
            settings.Groups.clear();
            for (const json& item : *it)
            {
                if (!item.is_object())
                    continue;
                AudioGroupSettings g;
                if (auto v = item.find("Name"); v != item.end() && v->is_string())
                    g.Name = v->get<std::string>();
                if (auto v = item.find("Parent"); v != item.end() && v->is_string())
                    g.Parent = v->get<std::string>();
                if (auto v = item.find("Volume"); v != item.end() && v->is_number())
                    g.Volume = v->get<float>();
                if (auto v = item.find("Muted"); v != item.end() && v->is_boolean())
                    g.Muted = v->get<bool>();
                if (auto f = item.find("Filter"); f != item.end() && f->is_object())
                {
                    if (auto v = f->find("LowPass"); v != f->end() && v->is_boolean())
                        g.Filter.LowPass = v->get<bool>();
                    if (auto v = f->find("LowPassCutoff"); v != f->end() && v->is_number())
                        g.Filter.LowPassCutoff = v->get<float>();
                    if (auto v = f->find("HighPass"); v != f->end() && v->is_boolean())
                        g.Filter.HighPass = v->get<bool>();
                    if (auto v = f->find("HighPassCutoff"); v != f->end() && v->is_number())
                        g.Filter.HighPassCutoff = v->get<float>();
                }
                settings.Groups.push_back(std::move(g));
            }
        }
        settings.Sanitize();
        out = std::move(settings);
        return true;
    }

    // ------------------------------------------------------------------ player settings
    AudioPlayerSettings AudioPlayerSettings::Capture(const AudioSettings& mixer, float musicVolume)
    {
        AudioPlayerSettings settings;
        for (const AudioGroupSettings& g : mixer.Groups)
            settings.Groups.push_back({ g.Name, g.Volume, g.Muted });
        settings.MusicVolume = std::isfinite(musicVolume) ? std::clamp(musicVolume, 0.0f, AudioSettings::MaxVolume) : 1.0f;
        return settings;
    }

    size_t AudioPlayerSettings::ApplyTo(AudioSettings& mixer) const
    {
        size_t applied = 0;
        for (const Group& saved : Groups)
            if (AudioGroupSettings* g = mixer.Find(saved.Name))
            {
                g->Volume = std::isfinite(saved.Volume) ? std::clamp(saved.Volume, 0.0f, AudioSettings::MaxVolume) : 1.0f;
                g->Muted = saved.Muted;
                applied++;
            }
        return applied;
    }

    std::string AudioPlayerSettings::ToJson() const
    {
        json groups = json::array();
        for (const Group& g : Groups)
            groups.push_back({ { "Name", g.Name }, { "Volume", g.Volume }, { "Muted", g.Muted } });
        json j = { { "Version", 1 }, { "MusicVolume", MusicVolume }, { "Groups", groups } };
        return j.dump();
    }

    bool AudioPlayerSettings::FromJson(const std::string& text, AudioPlayerSettings& out)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        AudioPlayerSettings settings;
        if (auto it = j.find("MusicVolume"); it != j.end() && it->is_number())
        {
            float v = it->get<float>();
            settings.MusicVolume = std::isfinite(v) ? std::clamp(v, 0.0f, AudioSettings::MaxVolume) : 1.0f;
        }
        if (auto it = j.find("Groups"); it != j.end() && it->is_array())
            for (const json& item : *it)
            {
                if (!item.is_object())
                    continue;
                Group group;
                if (auto v = item.find("Name"); v != item.end() && v->is_string())
                    group.Name = v->get<std::string>();
                if (group.Name.empty() || std::any_of(settings.Groups.begin(), settings.Groups.end(), [&](const Group& g) { return g.Name == group.Name; }))
                    continue;
                if (auto v = item.find("Volume"); v != item.end() && v->is_number())
                {
                    float volume = v->get<float>();
                    group.Volume = std::isfinite(volume) ? std::clamp(volume, 0.0f, AudioSettings::MaxVolume) : 1.0f;
                }
                if (auto v = item.find("Muted"); v != item.end() && v->is_boolean())
                    group.Muted = v->get<bool>();
                settings.Groups.push_back(std::move(group));
            }
        out = std::move(settings);
        return true;
    }

    // ------------------------------------------------------------------ decibels
    float AudioGain::ToDecibels(float gain)
    {
        if (gain <= 0.0f)
            return MinDecibels;
        return std::max(MinDecibels, 20.0f * std::log10(gain));
    }

    float AudioGain::FromDecibels(float decibels)
    {
        if (decibels <= MinDecibels)
            return 0.0f;
        return std::pow(10.0f, decibels / 20.0f);
    }

}
