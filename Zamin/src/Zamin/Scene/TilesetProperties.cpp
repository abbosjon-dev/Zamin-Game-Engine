#include "Zamin/Scene/TilesetProperties.h"

#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Components.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Zamin {

    using ordered_json = nlohmann::ordered_json;

    const char* TileCollisionName(TileCollision collision)
    {
        switch (collision)
        {
            case TileCollision::Solid: return "Solid";
            case TileCollision::None: return "None";
            case TileCollision::OneWay: return "OneWay";
        }
        return "Solid";
    }

    static bool TileCollisionFromName(std::string_view name, TileCollision& out)
    {
        for (TileCollision c : { TileCollision::Solid, TileCollision::None, TileCollision::OneWay })
            if (name == TileCollisionName(c))
            {
                out = c;
                return true;
            }
        return false;
    }

    static bool IsValidTile(int tile) { return tile >= 0 && tile <= TilesetProperties::MaxTile; }

    // ------------------------------------------------------------------ animation
    int TileAnimation::GetFrameIndex(double time) const
    {
        if (Frames.empty())
            return -1;
        if (!(FPS > 0.0f) || !std::isfinite(time) || time <= 0.0)
            return 0;
        double frame = std::floor(time * (double)FPS);
        return (int)std::fmod(frame, (double)Frames.size());
    }

    int TileAnimation::GetFrame(double time) const
    {
        int index = GetFrameIndex(time);
        return index < 0 ? Tile : Frames[(size_t)index];
    }

    float TileAnimation::GetDuration() const
    {
        return Frames.empty() || !(FPS > 0.0f) ? 0.0f : (float)Frames.size() / FPS;
    }

    // ------------------------------------------------------------------ properties
    TileCollision TilesetProperties::GetCollision(int tile) const
    {
        return tile >= 0 && tile < (int)m_Collision.size() ? m_Collision[(size_t)tile] : TileCollision::Solid;
    }

    void TilesetProperties::SetCollision(int tile, TileCollision collision)
    {
        if (!IsValidTile(tile))
            return;
        if (collision == TileCollision::Solid)
        {
            if (tile < (int)m_Collision.size())
                m_Collision[(size_t)tile] = TileCollision::Solid;
            // Trailing defaults are not kept (equal sets compare equal).
            while (!m_Collision.empty() && m_Collision.back() == TileCollision::Solid)
                m_Collision.pop_back();
            return;
        }
        if (tile >= (int)m_Collision.size())
            m_Collision.resize((size_t)tile + 1, TileCollision::Solid);
        m_Collision[(size_t)tile] = collision;
    }

    std::vector<int> TilesetProperties::GetTilesWith(TileCollision collision) const
    {
        std::vector<int> tiles;
        for (int i = 0; i < (int)m_Collision.size(); i++)
            if (m_Collision[(size_t)i] == collision)
                tiles.push_back(i);
        return tiles;
    }

    bool TilesetProperties::HasCollisionSettings() const
    {
        return std::any_of(m_Collision.begin(), m_Collision.end(), [](TileCollision c) { return c != TileCollision::Solid; });
    }

    const TileAnimation* TilesetProperties::FindAnimation(int tile) const
    {
        if (tile < 0 || tile >= (int)m_AnimationIndex.size())
            return nullptr;
        int index = m_AnimationIndex[(size_t)tile];
        return index < 0 ? nullptr : &m_Animations[(size_t)index];
    }

    bool TilesetProperties::SetAnimation(TileAnimation animation)
    {
        if (!IsValidTile(animation.Tile))
            return false;
        std::erase_if(animation.Frames, [](int f) { return !IsValidTile(f); });
        if ((int)animation.Frames.size() > TileAnimation::MaxFrames)
            animation.Frames.resize((size_t)TileAnimation::MaxFrames);
        if (!std::isfinite(animation.FPS))
            animation.FPS = 0.0f;
        animation.FPS = std::clamp(animation.FPS, 0.0f, 240.0f);
        if (animation.Frames.empty())
        {
            RemoveAnimation(animation.Tile);
            return true;
        }
        auto it = std::lower_bound(m_Animations.begin(), m_Animations.end(), animation.Tile,
                                   [](const TileAnimation& a, int tile) { return a.Tile < tile; });
        if (it != m_Animations.end() && it->Tile == animation.Tile)
            *it = std::move(animation);
        else
            m_Animations.insert(it, std::move(animation));
        RebuildIndex();
        return true;
    }

    bool TilesetProperties::RemoveAnimation(int tile)
    {
        auto it = std::find_if(m_Animations.begin(), m_Animations.end(), [&](const TileAnimation& a) { return a.Tile == tile; });
        if (it == m_Animations.end())
            return false;
        m_Animations.erase(it);
        RebuildIndex();
        return true;
    }

    void TilesetProperties::RebuildIndex()
    {
        m_AnimationIndex.clear();
        if (m_Animations.empty())
            return;
        m_AnimationIndex.assign((size_t)m_Animations.back().Tile + 1, -1);
        for (int i = 0; i < (int)m_Animations.size(); i++)
            m_AnimationIndex[(size_t)m_Animations[(size_t)i].Tile] = i;
    }

    bool TilesetProperties::operator==(const TilesetProperties& other) const
    {
        return m_Collision == other.m_Collision && m_Animations == other.m_Animations;
    }

    std::string TilesetProperties::ToJson() const
    {
        ordered_json j = ordered_json::object();
        if (HasCollisionSettings())
        {
            ordered_json collision = ordered_json::object();
            for (TileCollision c : { TileCollision::None, TileCollision::OneWay })
            {
                std::vector<int> tiles = GetTilesWith(c);
                if (!tiles.empty())
                    collision[TileCollisionName(c)] = tiles;
            }
            j["Collision"] = std::move(collision);
        }
        if (!m_Animations.empty())
        {
            ordered_json list = ordered_json::array();
            for (const TileAnimation& a : m_Animations)
            {
                ordered_json item;
                item["Tile"] = a.Tile;
                item["Frames"] = a.Frames;
                item["FPS"] = a.FPS;
                list.push_back(std::move(item));
            }
            j["Animations"] = std::move(list);
        }
        return j.dump();
    }

    bool TilesetProperties::FromJson(std::string_view text, TilesetProperties& out)
    {
        ordered_json j = ordered_json::parse(text.begin(), text.end(), nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        TilesetProperties props;
        if (auto collision = j.find("Collision"); collision != j.end() && collision->is_object())
            for (auto& [name, tiles] : collision->items())
            {
                TileCollision type;
                if (!TileCollisionFromName(name, type) || !tiles.is_array())
                    continue;
                for (const ordered_json& t : tiles)
                    if (t.is_number_integer())
                        props.SetCollision(t.get<int>(), type);
            }
        if (auto animations = j.find("Animations"); animations != j.end() && animations->is_array())
            for (const ordered_json& item : *animations)
            {
                if (!item.is_object() || !item.contains("Tile") || !item["Tile"].is_number_integer())
                    continue;
                TileAnimation a;
                a.Tile = item["Tile"].get<int>();
                if (auto frames = item.find("Frames"); frames != item.end() && frames->is_array())
                    for (const ordered_json& f : *frames)
                        if (f.is_number_integer())
                            a.Frames.push_back(f.get<int>());
                if (auto fps = item.find("FPS"); fps != item.end() && fps->is_number())
                    a.FPS = fps->get<float>();
                props.SetAnimation(std::move(a));
            }
        out = std::move(props);
        return true;
    }

    TilesetProperties TilesetProperties::FromImportSettings(const AssetImportSettings& import)
    {
        TilesetProperties props;
        if (const AssetImportSettings::Value* v = import.Find(ImportKey))
            if (const auto* raw = std::get_if<AssetImportSettings::Json>(v))
                FromJson(raw->Text, props);
        return props;
    }

    void TilesetProperties::WriteTo(AssetImportSettings& import) const
    {
        if (IsEmpty())
            import.Remove(ImportKey);
        else
            import.Set(ImportKey, AssetImportSettings::Json{ ToJson() });
    }

    namespace {
        struct PropertiesCacheEntry
        {
            const AssetRegistry* Registry = nullptr;
            uint64_t Revision = ~0ull;
            TilesetProperties Properties;
        };
        std::unordered_map<std::string, PropertiesCacheEntry>& PropertiesCache()
        {
            static std::unordered_map<std::string, PropertiesCacheEntry> s_Cache;
            return s_Cache;
        }
    }

    const TilesetProperties* TilesetProperties::Get(const AssetRegistry* registry, std::string_view tilesetPath)
    {
        if (!registry || tilesetPath.empty())
            return nullptr;
        auto& cache = PropertiesCache();
        auto it = cache.find(std::string(tilesetPath));
        if (it == cache.end())
            it = cache.emplace(std::string(tilesetPath), PropertiesCacheEntry{}).first;
        PropertiesCacheEntry& entry = it->second;
        if (entry.Registry != registry || entry.Revision != registry->GetRevision())
        {
            const AssetMetadata* meta = registry->Find(std::filesystem::path(tilesetPath));
            entry.Properties = meta ? FromImportSettings(meta->Import) : TilesetProperties();
            entry.Registry = registry;
            entry.Revision = registry->GetRevision();
        }
        return entry.Properties.IsEmpty() ? nullptr : &entry.Properties;
    }

    const TilesetProperties* TilesetProperties::Get(std::string_view tilesetPath)
    {
        if (tilesetPath.empty() || !Project::GetActive())
            return nullptr;
        return Get(Project::GetAssetRegistry(), tilesetPath);
    }

    void TilesetProperties::ClearCache() { PropertiesCache().clear(); }

    // ------------------------------------------------------------------ tilemap collision
    TileCollision TilemapCollision::GetCell(const TilemapComponent& tilemap, int x, int y, const TilesetProperties* properties)
    {
        TileCollision result = TileCollision::None;
        for (int i = 0; i < tilemap.GetLayerCount(); i++)
        {
            if (!tilemap.Layers[(size_t)i].Collision)
                continue;
            int tile = tilemap.Get(x, y, i);
            if (tile < 0)
                continue;
            TileCollision c = properties ? properties->GetCollision(tile) : TileCollision::Solid;
            if (c == TileCollision::Solid)
                return c;
            if (c == TileCollision::OneWay)
                result = c;
        }
        return result;
    }

    std::vector<TileColliderBox> TilemapCollision::BuildBoxes(const TilemapComponent& tilemap, const TilesetProperties* properties)
    {
        std::vector<TileColliderBox> boxes;
        std::vector<TileCollision> row((size_t)std::max(0, tilemap.Width));
        for (int y = 0; y < tilemap.Height; y++)
        {
            for (int x = 0; x < tilemap.Width; x++)
                row[(size_t)x] = GetCell(tilemap, x, y, properties);
            int x = 0;
            while (x < tilemap.Width)
            {
                TileCollision type = row[(size_t)x];
                if (type == TileCollision::None)
                {
                    x++;
                    continue;
                }
                int start = x;
                while (x < tilemap.Width && row[(size_t)x] == type)
                    x++;
                boxes.push_back({ start, x, y, type });
            }
        }
        return boxes;
    }

}
