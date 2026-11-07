#include "Zamin/Renderer/Quality.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Renderer/Texture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace Zamin {

    using json = nlohmann::json;

    namespace {

        float FiniteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

        std::string Lower(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c - 'A' + 'a');
            return out;
        }

        const char* FilterName(ShadowFilter filter)
        {
            switch (filter)
            {
                case ShadowFilter::Hard:    return "Hard";
                case ShadowFilter::PCF3x3:  return "PCF3x3";
                case ShadowFilter::PCF5x5:  return "PCF5x5";
                case ShadowFilter::Poisson: return "Poisson";
            }
            return "PCF5x5";
        }

        bool FilterFromName(std::string_view name, ShadowFilter& out)
        {
            for (int i = 0; i < ShadowFilterCount; i++)
                if (name == FilterName((ShadowFilter)i))
                {
                    out = (ShadowFilter)i;
                    return true;
                }
            return false;
        }

        const char* FilterLabel(ShadowFilter filter)
        {
            switch (filter)
            {
                case ShadowFilter::Hard:    return "qattiq";
                case ShadowFilter::PCF3x3:  return "PCF 3x3";
                case ShadowFilter::PCF5x5:  return "PCF 5x5";
                case ShadowFilter::Poisson: return "Poisson";
            }
            return "";
        }

        // ---- the active level
        struct QualityState
        {
            QualitySettings Settings;
            bool HasSettings = false;
            int Level = -1;
            QualityLevel Active = QualityLevel::Unlimited();
            uint64_t Changes = 0;
        };

        QualityState& State()
        {
            static QualityState s_State;
            return s_State;
        }

        void ApplyActive(int level)
        {
            QualityState& s = State();
            s.Level = s.HasSettings && level >= 0 && level < (int)s.Settings.Levels.size() ? level : -1;
            s.Active = s.Level >= 0 ? s.Settings.Levels[(size_t)s.Level] : QualityLevel::Unlimited();
            s.Changes++;
            Texture2D::SetSamplingQuality(s.Active.Anisotropy, s.Active.TextureQuality);
        }

        std::string LevelKey() { return std::string(Quality::SettingsKey) + ".Level"; }
    }

    // ------------------------------------------------------------------ level
    void QualityLevel::Sanitize()
    {
        if (ShadowAtlasSize < 1536) ShadowAtlasSize = 1024;
        else if (ShadowAtlasSize < 3072) ShadowAtlasSize = 2048;
        else ShadowAtlasSize = 4096;
        if ((int)MaxShadowFilter >= ShadowFilterCount)
            MaxShadowFilter = ShadowFilter::PCF5x5;
        MaxShadowCascades = std::clamp(MaxShadowCascades, 1, ShadowSettings::MaxCascades);
        ShadowDistanceScale = std::clamp(FiniteOr(ShadowDistanceScale, 1.0f), 0.1f, 4.0f);
        RenderScale = std::clamp(FiniteOr(RenderScale, 1.0f), MinScale, MaxScale);
        int anisotropy = 1;
        while (anisotropy * 2 <= std::clamp(Anisotropy, 1, MaxAnisotropy))
            anisotropy *= 2;
        Anisotropy = anisotropy;
        TextureQuality = std::clamp(TextureQuality, 0, MaxTextureQuality);
    }

    int QualityLevel::FilterCost(ShadowFilter filter)
    {
        switch (filter)
        {
            case ShadowFilter::Hard:    return 0;
            case ShadowFilter::PCF3x3:  return 1;
            case ShadowFilter::Poisson: return 2;
            case ShadowFilter::PCF5x5:  return 3;
        }
        return 3;
    }

    ShadowSettings QualityLevel::ApplyTo(const ShadowSettings& shadows) const
    {
        ShadowSettings out = shadows;
        out.Sanitize();
        out.AtlasSize = std::min(out.AtlasSize, ShadowAtlasSize);
        if (FilterCost(out.Filter) > FilterCost(MaxShadowFilter))
            out.Filter = MaxShadowFilter;
        out.CascadeCount = std::min(out.CascadeCount, MaxShadowCascades);
        out.Sanitize();
        return out;
    }

    PostProcessSettings QualityLevel::ApplyTo(const PostProcessSettings& post) const
    {
        PostProcessSettings out = post;
        out.SSAO = out.SSAO && SSAO;
        out.Bloom = out.Bloom && Bloom;
        out.FXAA = out.FXAA && FXAA;
        out.AutoExposure = out.AutoExposure && AutoExposure;
        return out;
    }

    float QualityLevel::ApplyRenderScale(float renderScale) const
    {
        return FiniteOr(renderScale, 1.0f) * RenderScale;
    }

    float QualityLevel::ApplyShadowDistance(float distance) const
    {
        return FiniteOr(distance, 40.0f) * ShadowDistanceScale;
    }

    std::vector<std::string> QualityLevel::DescribeLimits(const ShadowSettings& shadows, const PostProcessSettings& post, float renderScale,
                                                          float shadowDistance) const
    {
        std::vector<std::string> lines;
        if (!Shadows)
            lines.push_back("Quyosh soyalari o'chirilgan");
        else
        {
            ShadowSettings scene = shadows;
            scene.Sanitize();
            const ShadowSettings limited = ApplyTo(scene);
            if (limited.AtlasSize != scene.AtlasSize)
                lines.push_back(std::format("Soya atlasi {} -> {}", scene.AtlasSize, limited.AtlasSize));
            if (limited.Filter != scene.Filter)
                lines.push_back(std::format("Soya filtri {} -> {}", FilterLabel(scene.Filter), FilterLabel(limited.Filter)));
            if (limited.CascadeCount != scene.CascadeCount)
                lines.push_back(std::format("Kaskadlar {} -> {}", scene.CascadeCount, limited.CascadeCount));
            if (ShadowDistanceScale != 1.0f)
                lines.push_back(std::format("Soya masofasi {:.0f} -> {:.0f} m", shadowDistance, ApplyShadowDistance(shadowDistance)));
        }
        if (post.SSAO && !SSAO) lines.push_back("SSAO o'chirilgan");
        if (post.Bloom && !Bloom) lines.push_back("Bloom o'chirilgan");
        if (post.FXAA && !FXAA) lines.push_back("FXAA o'chirilgan");
        if (post.AutoExposure && !AutoExposure) lines.push_back("Avto ekspozitsiya o'chirilgan");
        if (RenderScale != 1.0f)
            lines.push_back(std::format("Render masshtabi x{:.2f} -> x{:.2f}", renderScale, ApplyRenderScale(renderScale)));
        return lines;
    }

    QualityLevel QualityLevel::Unlimited()
    {
        QualityLevel level;
        level.Name = "Unlimited";
        return level;
    }

    // ------------------------------------------------------------------ settings
    std::vector<QualityLevel> QualitySettings::DefaultLevels()
    {
        QualityLevel low;
        low.Name = "Low";
        low.ShadowAtlasSize = 1024;
        low.MaxShadowFilter = ShadowFilter::Hard;
        low.MaxShadowCascades = 1;
        low.ShadowDistanceScale = 0.5f;
        low.SSAO = false;
        low.Bloom = false;
        low.FXAA = false;
        low.AutoExposure = false;
        low.RenderScale = 0.75f;
        low.Anisotropy = 1;
        low.TextureQuality = 1;

        QualityLevel medium;
        medium.Name = "Medium";
        medium.ShadowAtlasSize = 2048;
        medium.MaxShadowFilter = ShadowFilter::PCF3x3;
        medium.MaxShadowCascades = 2;
        medium.ShadowDistanceScale = 0.75f;
        medium.SSAO = false;

        QualityLevel high;
        high.Name = "High";
        high.ShadowAtlasSize = 4096;
        high.MaxShadowFilter = ShadowFilter::PCF3x3;
        high.MaxShadowCascades = 3;

        // Ultra limits nothing a scene asks for: projects without levels look as they always did. The
        // levels' anisotropic filtering starts off for the same reason (a project turns it on, 4..16).
        QualityLevel ultra;
        ultra.Name = "Ultra";

        return { low, medium, high, ultra };
    }

    int QualitySettings::Find(std::string_view name) const
    {
        const std::string wanted = Lower(name);
        for (size_t i = 0; i < Levels.size(); i++)
            if (Lower(Levels[i].Name) == wanted)
                return (int)i;
        return -1;
    }

    std::string QualitySettings::MakeUniqueName(std::string_view base) const
    {
        std::string name(base.empty() ? std::string_view("Level") : base);
        for (int i = 2; Find(name) >= 0; i++)
            name = std::format("{} {}", base.empty() ? std::string_view("Level") : base, i);
        return name;
    }

    void QualitySettings::Sanitize()
    {
        if (Levels.size() > (size_t)MaxLevels)
            Levels.resize((size_t)MaxLevels);
        if (Levels.empty())
        {
            Levels = DefaultLevels();
            DefaultLevel = (int)Levels.size() - 1;
        }
        for (size_t i = 0; i < Levels.size(); i++)
        {
            QualityLevel& level = Levels[i];
            level.Sanitize();
            // Names: not empty, unique (case is ignored).
            std::string base = level.Name.empty() ? std::string("Level") : level.Name;
            std::string name = base;
            auto taken = [&](const std::string& candidate) {
                for (size_t j = 0; j < i; j++)
                    if (Lower(Levels[j].Name) == Lower(candidate))
                        return true;
                return false;
            };
            for (int n = 2; taken(name); n++)
                name = std::format("{} {}", base, n);
            level.Name = name;
        }
        DefaultLevel = std::clamp(DefaultLevel, 0, (int)Levels.size() - 1);
    }

    std::string QualitySettings::ToJson() const
    {
        json levels = json::array();
        for (const QualityLevel& l : Levels)
            levels.push_back({
                { "Name", l.Name },
                { "Shadows", l.Shadows },
                { "ShadowAtlas", l.ShadowAtlasSize },
                { "ShadowFilter", FilterName(l.MaxShadowFilter) },
                { "ShadowCascades", l.MaxShadowCascades },
                { "ShadowDistance", l.ShadowDistanceScale },
                { "SSAO", l.SSAO },
                { "Bloom", l.Bloom },
                { "FXAA", l.FXAA },
                { "AutoExposure", l.AutoExposure },
                { "RenderScale", l.RenderScale },
                { "Anisotropy", l.Anisotropy },
                { "TextureQuality", l.TextureQuality },
            });
        const bool valid = DefaultLevel >= 0 && DefaultLevel < (int)Levels.size();
        return json{ { "Default", valid ? Levels[(size_t)DefaultLevel].Name : std::string() }, { "Levels", levels } }.dump(4);
    }

    bool QualitySettings::FromJson(std::string_view text, QualitySettings& out)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        const std::vector<QualityLevel> builtIn = DefaultLevels();
        QualitySettings result;
        result.Levels.clear();
        if (auto levels = j.find("Levels"); levels != j.end() && levels->is_array())
            for (const json& item : *levels)
            {
                if (!item.is_object())
                    continue;
                QualityLevel level;
                std::string name = item.value("Name", std::string());
                // A level named like a built-in one starts from it: its values that are missing in the file.
                for (const QualityLevel& b : builtIn)
                    if (Lower(b.Name) == Lower(name))
                        level = b;
                level.Name = name;
                auto readBool = [&](const char* key, bool& v) {
                    if (auto it = item.find(key); it != item.end() && it->is_boolean()) v = it->get<bool>();
                };
                auto readInt = [&](const char* key, int& v) {
                    if (auto it = item.find(key); it != item.end() && it->is_number()) v = it->get<int>();
                };
                auto readFloat = [&](const char* key, float& v) {
                    if (auto it = item.find(key); it != item.end() && it->is_number()) v = it->get<float>();
                };
                readBool("Shadows", level.Shadows);
                if (auto it = item.find("ShadowAtlas"); it != item.end() && it->is_number())
                    level.ShadowAtlasSize = (uint32_t)std::max(0, it->get<int>());
                if (auto it = item.find("ShadowFilter"); it != item.end() && it->is_string())
                    FilterFromName(it->get<std::string>(), level.MaxShadowFilter);
                readInt("ShadowCascades", level.MaxShadowCascades);
                readFloat("ShadowDistance", level.ShadowDistanceScale);
                readBool("SSAO", level.SSAO);
                readBool("Bloom", level.Bloom);
                readBool("FXAA", level.FXAA);
                readBool("AutoExposure", level.AutoExposure);
                readFloat("RenderScale", level.RenderScale);
                readInt("Anisotropy", level.Anisotropy);
                readInt("TextureQuality", level.TextureQuality);
                result.Levels.push_back(std::move(level));
            }
        if (result.Levels.empty())
            result.Levels = builtIn;
        result.Sanitize();
        result.DefaultLevel = (int)result.Levels.size() - 1;
        if (auto def = j.find("Default"); def != j.end())
        {
            if (def->is_string())
            {
                int index = result.Find(def->get<std::string>());
                if (index >= 0)
                    result.DefaultLevel = index;
            }
            else if (def->is_number_integer())
                result.DefaultLevel = std::clamp(def->get<int>(), 0, (int)result.Levels.size() - 1);
        }
        out = std::move(result);
        return true;
    }

    // ------------------------------------------------------------------ runtime
    void Quality::SetSettings(const QualitySettings& settings, bool keepLevel)
    {
        QualityState& s = State();
        const std::string previous = s.Level >= 0 ? s.Active.Name : std::string();
        s.Settings = settings;
        s.Settings.Sanitize();
        s.HasSettings = true;
        int level = s.Settings.DefaultLevel;
        if (keepLevel && !previous.empty())
            if (int same = s.Settings.Find(previous); same >= 0)
                level = same;
        ApplyActive(level);
    }

    const QualitySettings& Quality::GetSettings() { return State().Settings; }

    void Quality::Reset()
    {
        QualityState& s = State();
        s.Settings = QualitySettings();
        s.HasSettings = false;
        ApplyActive(-1);
    }

    int Quality::GetLevelCount() { return State().HasSettings ? (int)State().Settings.Levels.size() : 0; }
    int Quality::GetLevel() { return State().Level; }
    std::string Quality::GetLevelName() { return State().Level >= 0 ? State().Active.Name : std::string(); }

    std::vector<std::string> Quality::GetLevelNames()
    {
        std::vector<std::string> names;
        if (State().HasSettings)
            for (const QualityLevel& l : State().Settings.Levels)
                names.push_back(l.Name);
        return names;
    }

    bool Quality::SetLevel(int index)
    {
        if (index < 0 || index >= GetLevelCount())
            return false;
        if (index != State().Level)
            ApplyActive(index);
        return true;
    }

    bool Quality::SetLevel(std::string_view name)
    {
        return State().HasSettings && SetLevel(State().Settings.Find(name));
    }

    bool Quality::SetLevelFromString(std::string_view text)
    {
        int index = -1;
        if (!text.empty() && std::from_chars(text.data(), text.data() + text.size(), index).ptr == text.data() + text.size())
            return SetLevel(index);
        return SetLevel(text);
    }

    const QualityLevel& Quality::GetActive() { return State().Active; }
    uint64_t Quality::GetChangeCount() { return State().Changes; }

    bool Quality::SaveSettings()
    {
        if (GetLevel() < 0)
            return false;
        SaveData::SetString(LevelKey(), GetLevelName());
        return SaveData::Save();
    }

    bool Quality::LoadSettings()
    {
        if (!HasSavedSettings())
            return false;
        const std::string name = SaveData::GetString(LevelKey());
        if (!SetLevel(name))
        {
            ZM_CORE_WARN("Saqlangan sifat darajasi '{}' loyihada yo'q - standart daraja qoldi", name);
            return false;
        }
        return true;
    }

    bool Quality::HasSavedSettings() { return SaveData::GetType(LevelKey()) == SaveValueType::String; }
    void Quality::ClearSavedSettings() { SaveData::DeleteKey(LevelKey()); }

}
