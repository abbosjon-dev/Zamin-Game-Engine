#include "Zamin/Core/Display.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Core/Window.h"

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
            DisplayAdjustment Adjustment;
            bool Set = false; // an option was set before a window was bound
        };

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
        return SaveData::Save();
    }

    bool Display::LoadSettings()
    {
        if (!HasSavedSettings())
            return false;
        const SavedDisplaySettings saved = GetSavedSettings();
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
    }

}
