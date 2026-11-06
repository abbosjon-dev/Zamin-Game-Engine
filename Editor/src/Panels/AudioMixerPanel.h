#pragma once

#include <Zamin/Zamin.h>

#include <functional>
#include <string>

namespace Zamin {

    // "Audio Mixer" window: a channel strip per mixer group (Master > Music, SFX, UI and the
    // project's own groups) with its fader (dB), stereo level meter, mute / solo, its parent and
    // how many sounds play in it; adding / removing / moving groups; the music that plays now and
    // the streaming threshold. In edit mode the changes are the project's defaults (.zproject);
    // while the game runs they only last for that run (or are saved as the defaults on request).
    class AudioMixerPanel
    {
    public:
        void OnImGuiRender(bool* show);
        // The game runs (Play): edits go to the running mixer only.
        void SetPlaying(bool playing) { m_Playing = playing; }
        void Focus() { m_FocusRequested = true; }
        // The project's audio defaults changed (the .zproject needs saving).
        std::function<void()> OnProjectModified;

    private:
        // One group's strip; `settings` is edited in place. Returns true when something changed.
        bool DrawStrip(AudioSettings& settings, const std::string& name, int depth, float height);
        void DrawMeter(const AudioLevel& level, float width, float height, bool silent);
        bool DrawAddGroupPopup(AudioSettings& settings);
        void DrawMusic();
        void Apply(const AudioSettings& settings, bool structural);

    private:
        bool m_Playing = false;
        bool m_FocusRequested = false;
        std::string m_NewGroupName;
        std::string m_NewGroupParent = AudioSettings::Master;
        std::string m_NewGroupError;
        bool m_OpenAddPopup = false;
    };

}
