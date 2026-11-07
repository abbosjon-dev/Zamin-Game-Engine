#include "Zamin/Core/Display.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Core/Window.h"
#include "Zamin/Project/PlayerSettings.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Zamin {

    namespace {
        struct DisplayState
        {
            Window* BoundWindow = nullptr;
            bool Fullscreen = false;
            bool VSync = true;
            ScreenResolution Resolution; // set by SetResolution (0 x 0: not set)
            DisplayAdjustment Adjustment;
            bool Set = false; // an option was set before a window was bound
        };

        bool InWindowRange(uint32_t value) { return value >= PlayerSettings::MinWindowSize && value <= PlayerSettings::MaxWindowSize; }

        // The window takes a resolution: its windowed size, and exclusive fullscreen's video mode.
        void ApplyResolution(Window& window, const ScreenResolution& r)
        {
            window.SetWindowedSize(r.Width, r.Height);
            window.SetFullscreenResolution(r.Width, r.Height);
        }

        DisplayState& State()
        {
            static DisplayState s_State;
            return s_State;
        }

        std::string SettingKey(const char* name) { return std::string(Display::SettingsKey) + "." + name; }
    }

    void Display::BindWindow(Window* window)
    {
        DisplayState& s = State();
        s.BoundWindow = window;
        if (!window)
            return;
        if (s.Set)
        {
            if (s.Resolution.IsValid())
                ApplyResolution(*window, s.Resolution);
            window->SetVSync(s.VSync);
            window->SetFullscreen(s.Fullscreen);
        }
        else
        {
            s.Fullscreen = window->IsFullscreen();
            s.VSync = window->IsVSync();
        }
    }

    bool Display::HasWindow() { return State().BoundWindow != nullptr; }

    void Display::SetFullscreen(bool fullscreen)
    {
        DisplayState& s = State();
        s.Fullscreen = fullscreen;
        s.Set = true;
        if (s.BoundWindow)
            s.BoundWindow->SetFullscreen(fullscreen);
    }

    bool Display::IsFullscreen()
    {
        const DisplayState& s = State();
        return s.BoundWindow ? s.BoundWindow->IsFullscreen() : s.Fullscreen;
    }

    void Display::SetVSync(bool enabled)
    {
        DisplayState& s = State();
        s.VSync = enabled;
        s.Set = true;
        if (s.BoundWindow)
            s.BoundWindow->SetVSync(enabled);
    }

    bool Display::IsVSync()
    {
        const DisplayState& s = State();
        return s.BoundWindow ? s.BoundWindow->IsVSync() : s.VSync;
    }

    // ------------------------------------------------------------------ resolution
    bool Display::SetResolution(uint32_t width, uint32_t height)
    {
        if (!InWindowRange(width) || !InWindowRange(height))
            return false;
        DisplayState& s = State();
        s.Resolution = { width, height };
        s.Set = true;
        if (s.BoundWindow)
            ApplyResolution(*s.BoundWindow, s.Resolution);
        return true;
    }

    ScreenResolution Display::GetResolution()
    {
        const DisplayState& s = State();
        return s.BoundWindow ? s.BoundWindow->GetWindowedSize() : s.Resolution;
    }

    std::vector<ScreenResolution> Display::GetResolutions()
    {
        const DisplayState& s = State();
        return MakeResolutionList(s.BoundWindow ? s.BoundWindow->GetVideoModes() : std::vector<ScreenResolution>(), GetResolution());
    }

    std::vector<ScreenResolution> Display::MakeResolutionList(const std::vector<ScreenResolution>& modes, const ScreenResolution& current)
    {
        static const ScreenResolution s_Common[] = { { 1280, 720 }, { 1366, 768 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 } };
        std::vector<ScreenResolution> list;
        uint32_t maxWidth = 0, maxHeight = 0;
        for (const ScreenResolution& mode : modes)
        {
            maxWidth = std::max(maxWidth, mode.Width);
            maxHeight = std::max(maxHeight, mode.Height);
        }
        if (modes.empty())
            list.assign(std::begin(s_Common), std::end(s_Common));
        else
            for (const ScreenResolution& mode : modes)
                if (mode.Width >= 640 && mode.Height >= 360 && InWindowRange(mode.Width) && InWindowRange(mode.Height))
                    list.push_back(mode);
        if (current.IsValid() && InWindowRange(current.Width) && InWindowRange(current.Height))
            list.push_back(current);
        auto area = [](const ScreenResolution& r) { return (uint64_t)r.Width * r.Height; };
        std::sort(list.begin(), list.end(), [&](const ScreenResolution& a, const ScreenResolution& b) {
            return area(a) != area(b) ? area(a) < area(b) : a.Width < b.Width;
        });
        list.erase(std::unique(list.begin(), list.end()), list.end());
        // (A window of its own larger than the monitor stays: it is what the player has now.)
        if (!modes.empty())
            std::erase_if(list, [&](const ScreenResolution& r) { return (r.Width > maxWidth || r.Height > maxHeight) && r != current; });
        return list;
    }

    // ------------------------------------------------------------------ gamma / brightness
    bool DisplayAdjustment::IsIdentity() const { return std::abs(Gamma - 1.0f) < 1e-3f && std::abs(Brightness - 1.0f) < 1e-3f; }

    void DisplayAdjustment::Sanitize()
    {
        Gamma = std::isfinite(Gamma) ? std::clamp(Gamma, MinGamma, MaxGamma) : 1.0f;
        Brightness = std::isfinite(Brightness) ? std::clamp(Brightness, MinBrightness, MaxBrightness) : 1.0f;
    }

    glm::vec3 DisplayAdjustment::Apply(const glm::vec3& displayColor) const
    {
        DisplayAdjustment a = *this;
        a.Sanitize();
        const glm::vec3 lit = glm::clamp(displayColor * a.Brightness, glm::vec3(0.0f), glm::vec3(1.0f));
        return glm::pow(lit, glm::vec3(1.0f / a.Gamma));
    }

    void Display::SetGamma(float gamma)
    {
        DisplayAdjustment& a = State().Adjustment;
        a.Gamma = gamma;
        a.Sanitize();
    }

    float Display::GetGamma() { return State().Adjustment.Gamma; }

    void Display::SetBrightness(float brightness)
    {
        DisplayAdjustment& a = State().Adjustment;
        a.Brightness = brightness;
        a.Sanitize();
    }

    float Display::GetBrightness() { return State().Adjustment.Brightness; }

    DisplayAdjustment Display::GetAdjustment() { return State().Adjustment; }

    void Display::SetAdjustment(const DisplayAdjustment& adjustment)
    {
        State().Adjustment = adjustment;
        State().Adjustment.Sanitize();
    }

    // ------------------------------------------------------------------ saved
    bool Display::SaveSettings()
    {
        SaveData::SetBool(SettingKey("Fullscreen"), IsFullscreen());
        SaveData::SetBool(SettingKey("VSync"), IsVSync());
        SaveData::SetFloat(SettingKey("Gamma"), GetGamma());
        SaveData::SetFloat(SettingKey("Brightness"), GetBrightness());
        // The resolution only once the game chose one (SetResolution): a window the player just dragged bigger
        // does not become the next start's size.
        if (const ScreenResolution r = State().Resolution; r.IsValid())
        {
            SaveData::SetInt(SettingKey("Width"), (int)r.Width);
            SaveData::SetInt(SettingKey("Height"), (int)r.Height);
        }
        return SaveData::Save();
    }

    bool Display::LoadSettings()
    {
        if (!HasSavedSettings())
            return false;
        const SavedDisplaySettings saved = GetSavedSettings();
        if (saved.Resolution)
            SetResolution(*saved.Resolution);
        if (saved.VSync || saved.Fullscreen)
        {
            SetVSync(saved.VSync.value_or(true));
            SetFullscreen(saved.Fullscreen.value_or(false));
        }
        if (saved.Gamma)
            SetGamma(*saved.Gamma);
        if (saved.Brightness)
            SetBrightness(*saved.Brightness);
        return true;
    }

    bool Display::HasSavedSettings() { return !GetSavedSettings().IsEmpty(); }

    SavedDisplaySettings Display::GetSavedSettings()
    {
        SavedDisplaySettings saved;
        if (SaveData::GetType(SettingKey("Fullscreen")) == SaveValueType::Bool)
            saved.Fullscreen = SaveData::GetBool(SettingKey("Fullscreen"));
        if (SaveData::GetType(SettingKey("VSync")) == SaveValueType::Bool)
            saved.VSync = SaveData::GetBool(SettingKey("VSync"));
        // (Kept in range: a hand-edited file cannot make the picture black.)
        if (SaveData::GetType(SettingKey("Gamma")) == SaveValueType::Float)
        {
            DisplayAdjustment a;
            a.Gamma = SaveData::GetFloat(SettingKey("Gamma"), 1.0f);
            a.Sanitize();
            saved.Gamma = a.Gamma;
        }
        if (SaveData::GetType(SettingKey("Width")) == SaveValueType::Int && SaveData::GetType(SettingKey("Height")) == SaveValueType::Int)
        {
            const int w = SaveData::GetInt(SettingKey("Width")), h = SaveData::GetInt(SettingKey("Height"));
            if (w > 0 && h > 0 && InWindowRange((uint32_t)w) && InWindowRange((uint32_t)h))
                saved.Resolution = ScreenResolution{ (uint32_t)w, (uint32_t)h };
        }
        if (SaveData::GetType(SettingKey("Brightness")) == SaveValueType::Float)
        {
            DisplayAdjustment a;
            a.Brightness = SaveData::GetFloat(SettingKey("Brightness"), 1.0f);
            a.Sanitize();
            saved.Brightness = a.Brightness;
        }
        return saved;
    }

    void Display::ClearSavedSettings()
    {
        SaveData::DeleteKey(SettingKey("Fullscreen"));
        SaveData::DeleteKey(SettingKey("VSync"));
        SaveData::DeleteKey(SettingKey("Gamma"));
        SaveData::DeleteKey(SettingKey("Brightness"));
        SaveData::DeleteKey(SettingKey("Width"));
        SaveData::DeleteKey(SettingKey("Height"));
    }

}
