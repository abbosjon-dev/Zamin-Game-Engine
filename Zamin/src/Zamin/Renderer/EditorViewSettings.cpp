#include "Zamin/Renderer/EditorViewSettings.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <type_traits>

namespace Zamin {

    using json = nlohmann::json;

    // ------------------------------------------------------------------ snapping
    float SnapSettings::GetStep(int operation) const
    {
        switch (operation)
        {
            case 1:  return Rotate;
            case 2:  return Scale;
            default: return Translate;
        }
    }

    static float SanitizeStep(float value, float fallback, float minimum)
    {
        if (!std::isfinite(value)) return fallback;
        return std::max(value, minimum);
    }

    void SnapSettings::Sanitize()
    {
        Translate = SanitizeStep(Translate, 0.5f, MinStep);
        Rotate = std::min(SanitizeStep(Rotate, 15.0f, MinStep), 360.0f);
        Scale = SanitizeStep(Scale, 0.1f, MinStep);
    }

    // ------------------------------------------------------------------ grid
    void GridSettings::Sanitize()
    {
        CellSize = SanitizeStep(CellSize, 1.0f, MinCellSize);
        MajorEvery = std::clamp(MajorEvery, 1, MaxMajorEvery);
        FadeDistance = std::clamp(SanitizeStep(FadeDistance, 1.0f, 0.1f), 0.1f, 10.0f);
        Color = glm::clamp(Color, glm::vec4(0.0f), glm::vec4(1.0f));
        MajorColor = glm::clamp(MajorColor, glm::vec4(0.0f), glm::vec4(1.0f));
    }

    // ------------------------------------------------------------------ camera bookmarks
    void CameraBookmarks::Set(int slot, const EditorCameraState& state)
    {
        if (slot >= 1 && slot <= SlotCount)
            m_Slots[slot - 1] = state;
    }

    void CameraBookmarks::Clear(int slot)
    {
        if (slot >= 1 && slot <= SlotCount)
            m_Slots[slot - 1].reset();
    }

    const EditorCameraState* CameraBookmarks::Get(int slot) const
    {
        if (slot < 1 || slot > SlotCount || !m_Slots[slot - 1])
            return nullptr;
        return &*m_Slots[slot - 1];
    }

    bool CameraBookmarks::IsEmpty() const { return Count() == 0; }

    int CameraBookmarks::Count() const
    {
        return (int)std::count_if(m_Slots.begin(), m_Slots.end(), [](const auto& s) { return s.has_value(); });
    }

    // ------------------------------------------------------------------ inspector headers
    bool InspectorFoldState::IsOpen(const std::string& key, bool defaultOpen) const
    {
        auto it = m_Open.find(key);
        return it == m_Open.end() ? defaultOpen : it->second;
    }

    bool InspectorFoldState::SetOpen(const std::string& key, bool open, bool defaultOpen)
    {
        if (key.empty())
            return false;
        auto it = m_Open.find(key);
        if (open == defaultOpen)
        {
            if (it == m_Open.end())
                return false;
            m_Open.erase(it);
            return true;
        }
        if (it != m_Open.end() && it->second == open)
            return false;
        if (it == m_Open.end() && m_Open.size() >= MaxEntries)
            return false;
        m_Open[key] = open;
        return true;
    }

    // ------------------------------------------------------------------ serialization
    std::string EditorViewSettings::SceneKey(const std::filesystem::path& scenePath)
    {
        if (scenePath.empty())
            return {};
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(scenePath, ec);
        return (ec ? scenePath : abs).lexically_normal().generic_string();
    }

    // Floats written with 6 significant digits (0.1, not 0.10000000149011612).
    static double Num(float v)
    {
        std::string text = std::format("{:.6g}", v);
        double out = v;
        std::from_chars(text.data(), text.data() + text.size(), out); // locale independent
        return out;
    }
    static json Vec(const glm::vec3& v) { return json::array({ Num(v.x), Num(v.y), Num(v.z) }); }
    static json Vec(const glm::vec4& v) { return json::array({ Num(v.x), Num(v.y), Num(v.z), Num(v.w) }); }

    template<typename T>
    static void Read(const json& j, const char* key, T& out)
    {
        auto it = j.find(key);
        if (it == j.end()) return;
        if constexpr (std::is_same_v<T, bool>)
        {
            if (it->is_boolean()) out = it->get<bool>();
        }
        else
        {
            if (it->is_number()) out = it->get<T>();
        }
    }

    template<int N>
    static void ReadVec(const json& j, const char* key, glm::vec<N, float>& out)
    {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array() || it->size() != N) return;
        glm::vec<N, float> v;
        for (int i = 0; i < N; i++)
        {
            if (!(*it)[i].is_number()) return;
            v[i] = (*it)[i].get<float>();
        }
        out = v;
    }

    std::string EditorViewSettings::ToJson() const
    {
        json j;
        j["ZaminEditorView"] = 1;
        j["Snap"] = {
            { "Enabled", Snap.Enabled },
            { "Translate", Num(Snap.Translate) },
            { "Rotate", Num(Snap.Rotate) },
            { "Scale", Num(Snap.Scale) },
        };
        j["Grid"] = {
            { "Visible", Grid.Visible },
            { "CellSize", Num(Grid.CellSize) },
            { "MajorEvery", Grid.MajorEvery },
            { "Color", Vec(Grid.Color) },
            { "MajorColor", Vec(Grid.MajorColor) },
            { "ShowAxes", Grid.ShowAxes },
            { "FadeDistance", Num(Grid.FadeDistance) },
        };
        json scenes = json::object();
        for (const auto& [scene, bookmarks] : Bookmarks)
        {
            if (scene.empty() || bookmarks.IsEmpty())
                continue; // unsaved scenes and empty sets are not kept
            json slots = json::object();
            for (int slot = 1; slot <= CameraBookmarks::SlotCount; slot++)
                if (const EditorCameraState* s = bookmarks.Get(slot))
                    slots[std::to_string(slot)] = {
                        { "FocalPoint", Vec(s->FocalPoint) },
                        { "Distance", Num(s->Distance) },
                        { "Pitch", Num(s->Pitch) },
                        { "Yaw", Num(s->Yaw) },
                        { "Orthographic", s->Orthographic },
                    };
            scenes[scene] = std::move(slots);
        }
        j["CameraBookmarks"] = std::move(scenes);
        json headers = json::object();
        for (const auto& [key, open] : Inspector.GetAll())
            headers[key] = open;
        j["InspectorHeaders"] = std::move(headers);
        return j.dump(2);
    }

    bool EditorViewSettings::FromJson(const std::string& text)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("ZaminEditorView"))
            return false;

        if (auto it = j.find("Snap"); it != j.end() && it->is_object())
        {
            Read(*it, "Enabled", Snap.Enabled);
            Read(*it, "Translate", Snap.Translate);
            Read(*it, "Rotate", Snap.Rotate);
            Read(*it, "Scale", Snap.Scale);
            Snap.Sanitize();
        }
        if (auto it = j.find("Grid"); it != j.end() && it->is_object())
        {
            Read(*it, "Visible", Grid.Visible);
            Read(*it, "CellSize", Grid.CellSize);
            Read(*it, "MajorEvery", Grid.MajorEvery);
            ReadVec(*it, "Color", Grid.Color);
            ReadVec(*it, "MajorColor", Grid.MajorColor);
            Read(*it, "ShowAxes", Grid.ShowAxes);
            Read(*it, "FadeDistance", Grid.FadeDistance);
            Grid.Sanitize();
        }
        if (auto it = j.find("CameraBookmarks"); it != j.end() && it->is_object())
        {
            for (const auto& [scene, slots] : it->items())
            {
                if (scene.empty() || !slots.is_object())
                    continue;
                CameraBookmarks bookmarks;
                for (const auto& [key, value] : slots.items())
                {
                    int slot = std::atoi(key.c_str());
                    if (slot < 1 || slot > CameraBookmarks::SlotCount || !value.is_object())
                        continue;
                    EditorCameraState s;
                    ReadVec(value, "FocalPoint", s.FocalPoint);
                    Read(value, "Distance", s.Distance);
                    Read(value, "Pitch", s.Pitch);
                    Read(value, "Yaw", s.Yaw);
                    Read(value, "Orthographic", s.Orthographic);
                    if (!std::isfinite(s.Distance) || s.Distance <= 0.0f)
                        s.Distance = 12.0f;
                    bookmarks.Set(slot, s);
                }
                Bookmarks[scene] = bookmarks;
            }
        }
        if (auto it = j.find("InspectorHeaders"); it != j.end() && it->is_object())
        {
            std::map<std::string, bool> headers;
            for (const auto& [key, open] : it->items())
                if (!key.empty() && open.is_boolean() && headers.size() < InspectorFoldState::MaxEntries)
                    headers[key] = open.get<bool>();
            Inspector.SetAll(std::move(headers));
        }
        return true;
    }

    bool EditorViewSettings::Save(const std::filesystem::path& path) const
    {
        return FileSystem::WriteTextFile(path, ToJson());
    }

    bool EditorViewSettings::Load(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return false;
        return FromJson(FileSystem::ReadTextFile(path));
    }

}
