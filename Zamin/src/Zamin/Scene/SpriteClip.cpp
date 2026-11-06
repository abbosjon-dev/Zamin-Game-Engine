#include "Zamin/Scene/SpriteClip.h"

#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <format>
#include <unordered_map>

namespace Zamin {

    namespace fs = std::filesystem;
    using json = nlohmann::json;

    void SpriteClip::SortEvents()
    {
        std::stable_sort(Events.begin(), Events.end(), [](const SpriteClipEvent& a, const SpriteClipEvent& b) { return a.Frame < b.Frame; });
    }

    // ------------------------------------------------------------------ files
    std::string SpriteClipSerializer::SerializeToString(const SpriteClip& clip)
    {
        json events = json::array();
        for (const SpriteClipEvent& e : clip.Events)
        {
            json ev = { { "Frame", e.Frame }, { "Name", e.Name } };
            if (!e.Parameter.empty())
                ev["Parameter"] = e.Parameter;
            events.push_back(std::move(ev));
        }
        json j = { { "Version", SpriteClip::CurrentVersion }, { "Texture", clip.Texture }, { "FPS", clip.FPS }, { "Loop", clip.Loop } };
        if (clip.UsesFrameList())
            j["Frames"] = clip.Frames;
        else
            j["Grid"] = { { "Columns", clip.Columns }, { "Rows", clip.Rows }, { "StartFrame", clip.StartFrame }, { "FrameCount", clip.FrameCount } };
        j["Events"] = std::move(events);
        return j.dump(2);
    }

    bool SpriteClipSerializer::DeserializeFromString(const std::string& text, SpriteClip& out, std::string* error)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            if (error) *error = "JSON o'qilmadi";
            return false;
        }
        if (j.value("Version", 1) > SpriteClip::CurrentVersion)
        {
            if (error) *error = std::format("yangiroq versiya ({})", j.value("Version", 1));
            return false;
        }
        auto integer = [](const json& o, const char* key, int fallback) {
            auto it = o.find(key);
            return it != o.end() && it->is_number_integer() ? it->get<int>() : fallback;
        };
        SpriteClip clip;
        if (auto it = j.find("Texture"); it != j.end() && it->is_string())
            clip.Texture = it->get<std::string>();
        if (auto it = j.find("FPS"); it != j.end() && it->is_number() && std::isfinite(it->get<float>()))
            clip.FPS = std::clamp(it->get<float>(), 0.0f, 240.0f);
        if (auto it = j.find("Loop"); it != j.end() && it->is_boolean())
            clip.Loop = it->get<bool>();
        if (auto frames = j.find("Frames"); frames != j.end() && frames->is_array())
            for (const json& f : *frames)
                if (f.is_string())
                    clip.Frames.push_back(f.get<std::string>());
        if (auto grid = j.find("Grid"); grid != j.end() && grid->is_object())
        {
            clip.Columns = std::max(1, integer(*grid, "Columns", 4));
            clip.Rows = std::max(1, integer(*grid, "Rows", 1));
            clip.StartFrame = std::max(0, integer(*grid, "StartFrame", 0));
            clip.FrameCount = std::max(1, integer(*grid, "FrameCount", 4));
        }
        if (clip.UsesFrameList())
        {
            clip.StartFrame = 0;
            clip.FrameCount = (int)clip.Frames.size();
        }
        if (auto events = j.find("Events"); events != j.end() && events->is_array())
            for (const json& e : *events)
            {
                if (!e.is_object())
                    continue;
                SpriteClipEvent ev;
                ev.Frame = std::clamp(integer(e, "Frame", 0), 0, std::max(0, clip.GetFrameCount() - 1));
                ev.Name = e.value("Name", std::string());
                ev.Parameter = e.value("Parameter", std::string());
                clip.Events.push_back(std::move(ev));
            }
        clip.SortEvents();
        out = std::move(clip);
        return true;
    }

    bool SpriteClipSerializer::Serialize(const SpriteClip& clip, const fs::path& path)
    {
        std::error_code ec;
        if (path.has_parent_path())
            fs::create_directories(path.parent_path(), ec);
        return FileSystem::WriteTextFile(path, SerializeToString(clip) + "\n");
    }

    bool SpriteClipSerializer::Deserialize(const fs::path& path, SpriteClip& out, std::string* error)
    {
        std::error_code ec;
        if (!fs::exists(path, ec))
        {
            if (error) *error = "fayl topilmadi";
            return false;
        }
        return DeserializeFromString(FileSystem::ReadTextFile(path), out, error);
    }

    // ------------------------------------------------------------------ assets
    static uint64_t s_SpriteClipRevision = 1;

    void SpriteClipAsset::SetClip(SpriteClip clip)
    {
        m_Clip = std::move(clip);
        m_Revision = ++s_SpriteClipRevision;
    }

    bool SpriteClipAsset::Reload(std::string* error)
    {
        SpriteClip clip;
        if (!SpriteClipSerializer::Deserialize(m_Path, clip, error))
            return false;
        if (!(clip == m_Clip))
            SetClip(std::move(clip));
        return true;
    }

    bool SpriteClipAsset::Save() const
    {
        return !m_Path.empty() && SpriteClipSerializer::Serialize(m_Clip, m_Path);
    }

    static std::unordered_map<std::string, Ref<SpriteClipAsset>> s_SpriteClipCache;

    static std::string SpriteClipKey(const fs::path& path)
    {
        fs::path full = Project::GetAssetFileSystemPath(path);
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(full, ec);
        return (ec ? fs::absolute(full, ec).lexically_normal() : canonical).generic_string();
    }

    Ref<SpriteClipAsset> SpriteClipLibrary::Load(const fs::path& path)
    {
        if (path.empty())
            return nullptr;
        std::string key = SpriteClipKey(path);
        if (auto it = s_SpriteClipCache.find(key); it != s_SpriteClipCache.end())
            return it->second;
        if (!AssetImportLog::ShouldRetry(key))
            return nullptr;
        SpriteClip clip;
        std::string error;
        if (!SpriteClipSerializer::Deserialize(key, clip, &error))
        {
            AssetImportLog::Report(key, error);
            return nullptr;
        }
        AssetImportLog::Clear(key);
        Ref<SpriteClipAsset> asset = CreateRef<SpriteClipAsset>(std::move(clip), fs::path(key));
        s_SpriteClipCache[key] = asset;
        return asset;
    }

    Ref<SpriteClipAsset> SpriteClipLibrary::Find(const fs::path& path)
    {
        auto it = s_SpriteClipCache.find(SpriteClipKey(path));
        return it != s_SpriteClipCache.end() ? it->second : nullptr;
    }

    bool SpriteClipLibrary::IsFileLoaded(const fs::path& path) { return Find(path) != nullptr; }

    size_t SpriteClipLibrary::ReloadFile(const fs::path& path, bool* failed)
    {
        auto it = s_SpriteClipCache.find(SpriteClipKey(path));
        if (it == s_SpriteClipCache.end())
            return 0;
        std::string error;
        uint64_t revision = it->second->GetRevision();
        if (!it->second->Reload(&error))
        {
            AssetImportLog::Report(it->first, error, true);
            if (failed)
                *failed = true;
            return 0;
        }
        AssetImportLog::Clear(it->first);
        return it->second->GetRevision() != revision ? 1 : 0;
    }

    void SpriteClipLibrary::OnFileMoved(const fs::path& from, const fs::path& to)
    {
        std::string fromKey = SpriteClipKey(from), toKey = SpriteClipKey(to);
        if (fromKey == toKey)
            return;
        std::string prefix = fromKey + "/";
        std::vector<std::pair<std::string, Ref<SpriteClipAsset>>> moved;
        for (auto it = s_SpriteClipCache.begin(); it != s_SpriteClipCache.end();)
        {
            if (it->first == fromKey || it->first.starts_with(prefix))
            {
                moved.emplace_back(toKey + it->first.substr(fromKey.size()), it->second);
                it = s_SpriteClipCache.erase(it);
            }
            else
                ++it;
        }
        for (auto& [key, asset] : moved)
        {
            asset->SetPath(fs::path(key));
            s_SpriteClipCache[key] = asset;
        }
    }

    void SpriteClipLibrary::Clear() { s_SpriteClipCache.clear(); }

    // ------------------------------------------------------------------ playback
    void CollectSpriteFrames(int from, int to, int count, bool loop, std::vector<int>& out)
    {
        out.clear();
        if (count <= 0 || to <= from)
            return;
        if (!loop)
        {
            const int last = count - 1;
            if (from >= last)
                return;
            for (int f = std::max(from + 1, 0); f <= std::min(to, last); f++)
                out.push_back(f);
            return;
        }
        // A hitch of a whole cycle or more: the last cycle's frames, each once.
        int first = std::max(from + 1, to - count + 1);
        for (int f = first; f <= to; f++)
            out.push_back(((f % count) + count) % count);
    }

    // ------------------------------------------------------------------ SpriteAnimationComponent
    void SpriteAnimationComponent::ResolveClips()
    {
        ClipAssets.resize(Clips.size());
        LoadedClips.resize(Clips.size());
        for (size_t i = 0; i < Clips.size(); i++)
        {
            // Looked up already (loaded, or failed until the path changes): kept.
            if (LoadedClips[i] == Clips[i])
                continue;
            LoadedClips[i] = Clips[i];
            ClipAssets[i] = Clips[i].empty() ? nullptr : SpriteClipLibrary::Load(Clips[i]);
        }
        if (CurrentClip >= (int)Clips.size())
            CurrentClip = -1;
    }

    int SpriteAnimationComponent::FindClip(std::string_view nameOrPath) const
    {
        for (size_t i = 0; i < Clips.size(); i++)
        {
            if (Clips[i] == nameOrPath)
                return (int)i;
            if (fs::path(Clips[i]).stem().string() == nameOrPath)
                return (int)i;
        }
        return -1;
    }

    bool SpriteAnimationComponent::PlayClip(std::string_view nameOrPath, bool restart)
    {
        return PlayClip(FindClip(nameOrPath), restart);
    }

    bool SpriteAnimationComponent::PlayClip(int index, bool restart)
    {
        ResolveClips();
        if (index < 0 || index >= (int)Clips.size() || !ClipAssets[(size_t)index])
            return false;
        Started = true;
        const SpriteClipAsset& asset = *ClipAssets[(size_t)index];
        if (index == CurrentClip && Playing && !restart)
        {
            if (AppliedRevision != asset.GetRevision())
            {
                ApplyClip(asset.GetClip());
                AppliedRevision = asset.GetRevision();
            }
            return true;
        }
        ApplyClip(asset.GetClip());
        AppliedRevision = asset.GetRevision();
        CurrentClip = index;
        Playing = true;
        Time = 0.0f;
        LastFrame = -1;
        return true;
    }

    void SpriteAnimationComponent::ApplyClip(const SpriteClip& clip)
    {
        Frames = clip.Frames;
        if (clip.UsesFrameList())
        {
            StartFrame = 0;
            FrameCount = (int)clip.Frames.size();
        }
        else
        {
            Columns = std::max(1, clip.Columns);
            Rows = std::max(1, clip.Rows);
            StartFrame = std::max(0, clip.StartFrame);
            FrameCount = std::max(1, clip.FrameCount);
        }
        FPS = clip.FPS;
        Loop = clip.Loop;
    }

}
