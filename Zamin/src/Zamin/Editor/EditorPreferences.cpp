#include "Zamin/Editor/EditorPreferences.h"
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

    namespace {
        const ColorPreset s_AccentPresets[] = {
            { "Emerald", "Zumrad", { 0.180f, 0.737f, 0.580f } },
            { "Blue", "Ko'k", { 0.259f, 0.522f, 0.957f } },
            { "Violet", "Binafsha", { 0.612f, 0.420f, 0.937f } },
            { "Orange", "To'q sariq", { 0.957f, 0.553f, 0.200f } },
            { "Red", "Qizil", { 0.902f, 0.302f, 0.333f } },
            { "Pink", "Pushti", { 0.925f, 0.380f, 0.643f } },
            { "Gold", "Oltin", { 0.871f, 0.706f, 0.188f } },
            { "Cyan", "Moviy", { 0.192f, 0.722f, 0.851f } },
        };

        const ColorPreset s_TintPresets[] = {
            { "Red", "Qizg'ish", { 1.7f, 0.9f, 0.9f } },
            { "Blue", "Ko'kish", { 0.9f, 1.05f, 1.6f } },
            { "Green", "Yashil", { 0.95f, 1.45f, 1.0f } },
            { "Violet", "Binafsha", { 1.35f, 0.95f, 1.55f } },
            { "Amber", "Qahrabo", { 1.55f, 1.25f, 0.8f } },
            { "Dark", "Qorong'i", { 0.7f, 0.7f, 0.7f } },
        };

        int FindPresetIn(std::span<const ColorPreset> presets, const glm::vec3& color)
        {
            for (size_t i = 0; i < presets.size(); i++)
                if (glm::all(glm::lessThanEqual(glm::abs(presets[i].Color - color), glm::vec3(0.002f))))
                    return (int)i;
            return -1;
        }

        float Finite(float value, float fallback) { return std::isfinite(value) ? value : fallback; }

        glm::vec3 SanitizeColor(const glm::vec3& color, const glm::vec3& fallback, float maximum)
        {
            for (int i = 0; i < 3; i++)
                if (!std::isfinite(color[i])) return fallback;
            return glm::clamp(color, glm::vec3(0.0f), glm::vec3(maximum));
        }

        // Floats written with 6 significant digits (0.1, not 0.10000000149011612).
        double Num(float v)
        {
            std::string text = std::format("{:.6g}", v);
            double out = v;
            std::from_chars(text.data(), text.data() + text.size(), out); // locale independent
            return out;
        }
        json Vec(const glm::vec3& v) { return json::array({ Num(v.x), Num(v.y), Num(v.z) }); }

        template<typename T>
        void Read(const json& j, const char* key, T& out)
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

        void ReadVec(const json& j, const char* key, glm::vec3& out)
        {
            auto it = j.find(key);
            if (it == j.end() || !it->is_array() || it->size() != 3) return;
            glm::vec3 v;
            for (int i = 0; i < 3; i++)
            {
                if (!(*it)[i].is_number()) return;
                v[i] = (*it)[i].get<float>();
            }
            out = v;
        }
    }

    // ------------------------------------------------------------------ theme
    glm::vec3 EditorThemeSettings::GetHoverShade(const glm::vec3& accent)
    {
        return glm::clamp(glm::mix(accent, glm::vec3(1.0f), 0.18f), glm::vec3(0.0f), glm::vec3(1.0f));
    }

    glm::vec3 EditorThemeSettings::GetDarkShade(const glm::vec3& accent)
    {
        return glm::clamp(accent * 0.68f, glm::vec3(0.0f), glm::vec3(1.0f));
    }

    std::span<const ColorPreset> EditorThemeSettings::GetAccentPresets() { return s_AccentPresets; }

    int EditorThemeSettings::FindAccentPreset() const { return FindPresetIn(s_AccentPresets, Accent); }

    void EditorThemeSettings::Sanitize()
    {
        Accent = SanitizeColor(Accent, GetDefaultAccent(), 1.0f);
        FontSize = std::clamp(std::round(Finite(FontSize, DefaultFontSize)), MinFontSize, MaxFontSize);
    }

    // ------------------------------------------------------------------ play-mode tint
    glm::vec3 PlayModeTintSettings::GetMultiplier() const
    {
        return glm::mix(glm::vec3(1.0f), Color, std::clamp(Strength, 0.0f, 1.0f));
    }

    glm::vec4 PlayModeTintSettings::Apply(const glm::vec4& color) const
    {
        const glm::vec3 rgb = glm::clamp(glm::vec3(color) * GetMultiplier(), glm::vec3(0.0f), glm::vec3(1.0f));
        return { rgb, color.a };
    }

    std::span<const ColorPreset> PlayModeTintSettings::GetPresets() { return s_TintPresets; }

    int PlayModeTintSettings::FindPreset() const { return FindPresetIn(s_TintPresets, Color); }

    void PlayModeTintSettings::Sanitize()
    {
        Color = SanitizeColor(Color, PlayModeTintSettings().Color, MaxChannel);
        Strength = std::clamp(Finite(Strength, 1.0f), 0.0f, 1.0f);
    }

    // ------------------------------------------------------------------ camera
    void EditorCameraDefaults::Sanitize()
    {
        FOV = std::clamp(Finite(FOV, 45.0f), MinFOV, MaxFOV);
        NearClip = std::clamp(Finite(NearClip, 0.1f), MinNear, MaxNear);
        FarClip = std::clamp(Finite(FarClip, 1000.0f), NearClip * 10.0f, MaxFar);
        FlySpeed = std::clamp(Finite(FlySpeed, 6.0f), MinFlySpeed, MaxFlySpeed);
        FlyAcceleration = std::clamp(Finite(FlyAcceleration, 0.0f), 0.0f, MaxAcceleration);
        LookSensitivity = std::clamp(Finite(LookSensitivity, 1.0f), MinSensitivity, MaxSensitivity);
    }

    // ------------------------------------------------------------------ preferences
    void EditorPreferences::Sanitize()
    {
        Theme.Sanitize();
        PlayTint.Sanitize();
        Camera.Sanitize();
        Autosave.Sanitize();
        Idle.Sanitize();
        if (Language != "uz" && Language != "en")
            Language = "uz";
    }

    std::string EditorPreferences::ToJson() const
    {
        json j;
        j["ZaminEditorPreferences"] = 1;
        j["Language"] = Language;
        j["Theme"] = {
            { "Accent", Vec(Theme.Accent) },
            { "FontSize", Num(Theme.FontSize) },
        };
        j["PlayModeTint"] = {
            { "Enabled", PlayTint.Enabled },
            { "Color", Vec(PlayTint.Color) },
            { "Strength", Num(PlayTint.Strength) },
            { "InSimulate", PlayTint.InSimulate },
        };
        j["EditorCamera"] = {
            { "FOV", Num(Camera.FOV) },
            { "Near", Num(Camera.NearClip) },
            { "Far", Num(Camera.FarClip) },
            { "FlySpeed", Num(Camera.FlySpeed) },
            { "FlyAcceleration", Num(Camera.FlyAcceleration) },
            { "LookSensitivity", Num(Camera.LookSensitivity) },
            { "InvertY", Camera.InvertY },
        };
        j["Console"] = {
            { "Timestamps", Console.ShowTimestamps },
            { "Collapse", Console.Collapse },
            { "ClearOnPlay", Console.ClearOnPlay },
            { "AutoScroll", Console.AutoScroll },
            { "Trace", Console.ShowTrace },
            { "Info", Console.ShowInfo },
            { "Warn", Console.ShowWarn },
            { "Error", Console.ShowError },
        };
        j["Autosave"] = {
            { "Enabled", Autosave.Enabled },
            { "IntervalMinutes", Autosave.IntervalMinutes },
        };
        j["Undo"] = {
            { "RecordSelection", Undo.RecordSelection },
        };
        j["Layout"] = {
            { "Current", Layout.Current },
        };
        j["IdleThrottle"] = {
            { "Enabled", Idle.Enabled },
            { "UnfocusedFPS", Num(Idle.UnfocusedFPS) },
            { "IdleFPS", Num(Idle.IdleFPS) },
            { "IdleDelay", Num(Idle.IdleDelay) },
        };
        j["ExternalEditor"] = {
            { "Command", ExternalEditor.Command },
        };
        j["Shortcuts"] = json::object();
        for (const auto& [id, chord] : Shortcuts)
            j["Shortcuts"][id] = chord;
        return j.dump(2);
    }

    bool EditorPreferences::FromJson(const std::string& text)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("ZaminEditorPreferences"))
            return false;
        if (auto it = j.find("Language"); it != j.end() && it->is_string())
            Language = it->get<std::string>();
        if (auto it = j.find("Theme"); it != j.end() && it->is_object())
        {
            ReadVec(*it, "Accent", Theme.Accent);
            Read(*it, "FontSize", Theme.FontSize);
        }
        if (auto it = j.find("PlayModeTint"); it != j.end() && it->is_object())
        {
            Read(*it, "Enabled", PlayTint.Enabled);
            ReadVec(*it, "Color", PlayTint.Color);
            Read(*it, "Strength", PlayTint.Strength);
            Read(*it, "InSimulate", PlayTint.InSimulate);
        }
        if (auto it = j.find("EditorCamera"); it != j.end() && it->is_object())
        {
            Read(*it, "FOV", Camera.FOV);
            Read(*it, "Near", Camera.NearClip);
            Read(*it, "Far", Camera.FarClip);
            Read(*it, "FlySpeed", Camera.FlySpeed);
            Read(*it, "FlyAcceleration", Camera.FlyAcceleration);
            Read(*it, "LookSensitivity", Camera.LookSensitivity);
            Read(*it, "InvertY", Camera.InvertY);
        }
        if (auto it = j.find("Console"); it != j.end() && it->is_object())
        {
            Read(*it, "Timestamps", Console.ShowTimestamps);
            Read(*it, "Collapse", Console.Collapse);
            Read(*it, "ClearOnPlay", Console.ClearOnPlay);
            Read(*it, "AutoScroll", Console.AutoScroll);
            Read(*it, "Trace", Console.ShowTrace);
            Read(*it, "Info", Console.ShowInfo);
            Read(*it, "Warn", Console.ShowWarn);
            Read(*it, "Error", Console.ShowError);
        }
        if (auto it = j.find("Autosave"); it != j.end() && it->is_object())
        {
            Read(*it, "Enabled", Autosave.Enabled);
            if (auto interval = it->find("IntervalMinutes"); interval != it->end() && interval->is_number())
                Autosave.IntervalMinutes = (int)std::lround(interval->get<double>());
        }
        if (auto it = j.find("Undo"); it != j.end() && it->is_object())
            Read(*it, "RecordSelection", Undo.RecordSelection);
        if (auto it = j.find("Layout"); it != j.end() && it->is_object())
            if (auto current = it->find("Current"); current != it->end() && current->is_string())
                Layout.Current = current->get<std::string>();
        if (auto it = j.find("IdleThrottle"); it != j.end() && it->is_object())
        {
            Read(*it, "Enabled", Idle.Enabled);
            Read(*it, "UnfocusedFPS", Idle.UnfocusedFPS);
            Read(*it, "IdleFPS", Idle.IdleFPS);
            Read(*it, "IdleDelay", Idle.IdleDelay);
        }
        if (auto it = j.find("ExternalEditor"); it != j.end() && it->is_object())
            if (auto command = it->find("Command"); command != it->end() && command->is_string())
                ExternalEditor.Command = command->get<std::string>();
        if (auto it = j.find("Shortcuts"); it != j.end() && it->is_object())
        {
            Shortcuts.clear();
            for (auto& [id, chord] : it->items())
                if (chord.is_string())
                    Shortcuts[id] = chord.get<std::string>();
        }
        Sanitize();
        return true;
    }

    bool EditorPreferences::Save(const std::filesystem::path& path) const
    {
        return FileSystem::WriteTextFile(path, ToJson());
    }

    bool EditorPreferences::Load(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return false;
        return FromJson(FileSystem::ReadTextFile(path));
    }

    std::filesystem::path EditorPreferences::GetConfigDirectory()
    {
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
            return std::filesystem::path(xdg) / "zamin";
        if (const char* home = std::getenv("HOME"); home && *home)
            return std::filesystem::path(home) / ".config" / "zamin";
        return std::filesystem::temp_directory_path() / "zamin";
    }

}
