#include "Zamin/Core/Display.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Core/Window.h"

#include <string>

namespace Zamin {

    namespace {
        struct DisplayState
        {
            Window* BoundWindow = nullptr;
            bool Fullscreen = false;
            bool VSync = true;
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

    bool Display::SaveSettings()
    {
        SaveData::SetBool(SettingKey("Fullscreen"), IsFullscreen());
        SaveData::SetBool(SettingKey("VSync"), IsVSync());
        return SaveData::Save();
    }

    bool Display::LoadSettings()
    {
        if (!HasSavedSettings())
            return false;
        SetVSync(SaveData::GetBool(SettingKey("VSync"), true));
        SetFullscreen(SaveData::GetBool(SettingKey("Fullscreen"), false));
        return true;
    }

    bool Display::HasSavedSettings()
    {
        return SaveData::GetType(SettingKey("Fullscreen")) == SaveValueType::Bool || SaveData::GetType(SettingKey("VSync")) == SaveValueType::Bool;
    }

    SavedDisplaySettings Display::GetSavedSettings()
    {
        SavedDisplaySettings saved;
        if (SaveData::GetType(SettingKey("Fullscreen")) == SaveValueType::Bool)
            saved.Fullscreen = SaveData::GetBool(SettingKey("Fullscreen"));
        if (SaveData::GetType(SettingKey("VSync")) == SaveValueType::Bool)
            saved.VSync = SaveData::GetBool(SettingKey("VSync"));
        return saved;
    }

    void Display::ClearSavedSettings()
    {
        SaveData::DeleteKey(SettingKey("Fullscreen"));
        SaveData::DeleteKey(SettingKey("VSync"));
    }

}
