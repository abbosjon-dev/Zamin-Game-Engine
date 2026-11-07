#include "AudioMixerPanel.h"
#include "../UI.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    namespace {
        constexpr float s_MaxDecibels = 6.0f; // AudioSettings::MaxVolume

        float DecibelPosition(float decibels)
        {
            return std::clamp((decibels - AudioGain::MinDecibels) / (s_MaxDecibels - AudioGain::MinDecibels), 0.0f, 1.0f);
        }

        std::string FormatDecibels(float gain)
        {
            if (gain <= 0.0f || AudioGain::ToDecibels(gain) <= AudioGain::MinDecibels)
                return "-inf dB";
            return std::format("{:+.1f} dB", AudioGain::ToDecibels(gain));
        }

        // "850", "1.2k", "12k" (a strip's narrow filter button).
        std::string ShortHz(float hz)
        {
            if (hz >= 10000.0f)
                return std::format("{:.0f}k", hz / 1000.0f);
            if (hz >= 1000.0f)
                return std::format("{:.1f}k", hz / 1000.0f);
            return std::format("{:.0f}", hz);
        }

        // The name shortened with "..." to fit `width`.
        std::string FitText(const std::string& text, float width)
        {
            if (ImGui::CalcTextSize(text.c_str()).x <= width)
                return text;
            std::string s = text;
            while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > width)
                s.pop_back();
            return s + "...";
        }

        // A colour for each branch under Master (the strip's top bar), so children read as part of it.
        ImU32 BranchColor(const AudioSettings& settings, const std::string& name)
        {
            static const ImVec4 colors[] = {
                { 0.30f, 0.62f, 0.95f, 1.0f }, { 0.95f, 0.62f, 0.25f, 1.0f }, { 0.70f, 0.45f, 0.95f, 1.0f },
                { 0.95f, 0.40f, 0.55f, 1.0f }, { 0.40f, 0.85f, 0.85f, 1.0f }, { 0.85f, 0.85f, 0.35f, 1.0f },
            };
            if (name == AudioSettings::Master)
                return UI::AccentU32();
            // The child of Master this group is under (or is).
            std::string top = name;
            for (const AudioGroupSettings* g = settings.Find(top); g && g->Parent != AudioSettings::Master && !g->Parent.empty();
                 g = settings.Find(top))
                top = g->Parent;
            std::vector<std::string> branches = settings.GetChildren(AudioSettings::Master);
            auto it = std::find(branches.begin(), branches.end(), top);
            size_t index = it != branches.end() ? (size_t)(it - branches.begin()) : 0;
            return ImGui::GetColorU32(colors[index % (sizeof(colors) / sizeof(colors[0]))]);
        }
    }

    void AudioMixerPanel::Apply(const AudioSettings& settings, bool structural)
    {
        (void)structural;
        AudioEngine::SetMixerSettings(settings);
        Ref<Project> project = Project::GetActive();
        if (m_Playing || !project)
            return;
        // Edit mode: the project's defaults (Solo is kept for the session, it is never saved).
        AudioSettings& defaults = project->GetConfig().Audio;
        bool modified = !defaults.EqualsIgnoringSolo(AudioEngine::GetMixerSettings());
        defaults = AudioEngine::GetMixerSettings();
        if (modified && OnProjectModified)
            OnProjectModified();
    }

    void AudioMixerPanel::OnImGuiRender(bool* show)
    {
        if (!*show)
            return;
        if (m_FocusRequested)
        {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        // First time (no saved layout for it): a tab next to the content browser.
        if (ImGuiWindow* browser = ImGui::FindWindowByName("###ContentBrowser"); browser && browser->DockId)
            ImGui::SetNextWindowDockID(browser->DockId, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 52.0f, ImGui::GetFontSize() * 24.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Tr(ICON_FA_SLIDERS "  Audio Mixer###AudioMixer"), show))
        {
            ImGui::End();
            return;
        }

        AudioSettings settings = AudioEngine::GetMixerSettings();
        Ref<Project> project = Project::GetActive();
        bool changed = false, structural = false;

        // ---- toolbar
        if (ImGui::Button(Tr(ICON_FA_PLUS "  Guruh")))
        {
            m_NewGroupName.clear();
            m_NewGroupError.clear();
            m_NewGroupParent = AudioSettings::Master;
            ImGui::OpenPopup("##addGroup");
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Yangi mixer guruhi (masalan Ovozlar, Qadamlar, Muhit): Audio Source'lar unga chiqadi");
        if (DrawAddGroupPopup(settings))
            changed = structural = true;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_GEAR))
            ImGui::OpenPopup("##mixerSettings");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Audio sozlamalari (streaming chegarasi, ovozlar limiti, Doppler)");
        if (ImGui::BeginPopup("##mixerSettings"))
        {
            ImGui::TextDisabled("Shundan katta fayllar diskdan oqim bilan o'qiladi");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
            if (ImGui::DragFloat("Streaming chegarasi", &settings.StreamThresholdMB, 0.1f, 0.01f, 4096.0f, "%.2f MB"))
                changed = true;
            ImGui::TextDisabled("Musiqa (Audio::PlayMusic) har doim oqim bilan o'qiladi");
            ImGui::Separator();
            ImGui::TextDisabled("Bir vaqtda ijro etiladigan ovozlar (musiqasiz)");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
            if (ImGui::DragInt("Ovozlar limiti", &settings.MaxVoices, 0.25f, 1, AudioSettings::MaxVoicesLimit, "%d ta", ImGuiSliderFlags_AlwaysClamp))
                changed = true;
            if (ImGui::IsItemHovered())
                UI::Tooltip("Limit to'lsa yangi ovoz ustuvorligi pastroq (teng bo'lsa - manbaning qoidasi bo'yicha) ovozning o'rnini oladi "
                            "yoki ijro etilmaydi (Audio Source > Ovozlar limiti)");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
            if (ImGui::SliderFloat("Doppler koeffitsiyenti", &settings.DopplerFactor, 0.0f, AudioSettings::MaxDopplerFactor, "%.2f"))
                changed = true;
            if (ImGui::IsItemHovered())
                UI::Tooltip("Harakatlanayotgan 3D ovozlar tonining o'zgarishi: 1 - tabiiy, 0 - o'chirilgan; har manbaning o'z "
                            "Doppler qiymatiga ko'paytiriladi");
            ImGui::EndPopup();
        }
        if (settings.AnySolo())
        {
            ImGui::SameLine();
            if (ImGui::Button(Tr(ICON_FA_HEADPHONES "  Solo'ni bekor qilish")))
            {
                for (AudioGroupSettings& g : settings.Groups)
                    g.Solo = false;
                changed = true;
            }
        }
        if (AudioEngine::IsAvailable())
        {
            // The voice limit at work.
            AudioVoiceStats stats = AudioEngine::GetVoiceStats();
            ImGui::SameLine();
            bool full = (int)stats.Voices >= stats.MaxVoices;
            ImGui::TextColored(full ? ImVec4(0.95f, 0.55f, 0.3f, 1.0f) : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled],
                               ICON_FA_LAYER_GROUP " %zu / %d", stats.Voices, stats.MaxVoices);
            if (ImGui::IsItemHovered())
                UI::Tooltip(std::format("Ijro etilayotgan ovozlar / limit (musiqa hisobga kirmaydi)\n"
                                        "Limit sababli to'xtatilgan: {}   ijro etilmagan: {}", stats.Stolen, stats.Rejected).c_str());
        }
        ImGui::SameLine();
        if (!AudioEngine::IsAvailable())
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Audio qurilmasi yo'q - faqat sozlash");
        else if (m_Playing)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_PLAY "  O'yin davomida: o'zgarishlar faqat shu o'yin uchun");
            if (project && !project->GetConfig().Audio.EqualsIgnoringSolo(settings))
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(Tr(ICON_FA_FLOPPY_DISK "  Standart qilib saqlash")))
                {
                    project->GetConfig().Audio = settings;
                    if (OnProjectModified)
                        OnProjectModified();
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Hozirgi mixerni loyihaning standart sozlamasi qilish (.zproject)");
                ImGui::SameLine();
                if (ImGui::SmallButton(Tr(ICON_FA_ROTATE_LEFT "  Qaytarish")))
                {
                    AudioSettings defaults = project->GetConfig().Audio;
                    AudioEngine::SetMixerSettings(defaults);
                    settings = defaults;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Loyihaning standart mixeriga qaytish");
            }
        }
        else
            ImGui::TextDisabled("Loyiha standarti (.zproject) - o'ng tugma: ota guruh, o'chirish");

        DrawMusic();
        ImGui::Separator();

        // ---- channel strips
        if (ImGui::BeginChild("##strips", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
        {
            float height = std::max(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ScrollbarSize - 2.0f);
            bool first = true;
            for (const auto& [name, depth] : settings.GetTreeOrder())
            {
                if (!settings.Find(name))
                    continue; // removed by the strip before
                if (!first)
                    ImGui::SameLine(0.0f, depth > 1 ? 4.0f : 12.0f); // children close to their parent
                first = false;
                size_t before = settings.Groups.size();
                std::vector<std::string> parents;
                for (const AudioGroupSettings& g : settings.Groups)
                    parents.push_back(g.Parent);
                if (DrawStrip(settings, name, depth, height))
                {
                    changed = true;
                    std::vector<std::string> after;
                    for (const AudioGroupSettings& g : settings.Groups)
                        after.push_back(g.Parent);
                    if (settings.Groups.size() != before || after != parents)
                        structural = true;
                }
                if (structural)
                    break; // the order changed: drawn anew next frame
            }
        }
        ImGui::EndChild();

        if (changed)
            Apply(settings, structural);
        ImGui::End();
    }

    void AudioMixerPanel::DrawMusic()
    {
        std::filesystem::path music = AudioEngine::GetMusic();
        if (music.empty())
        {
            ImGui::TextDisabled(ICON_FA_MUSIC "  Musiqa yo'q  (skriptda: Audio::PlayMusic, Audio::CrossfadeTo)");
            return;
        }
        size_t tracks = AudioEngine::GetMusicVoiceCount();
        std::string state = AudioEngine::IsMusicPaused() ? "  (pauza)" : tracks > 1 ? "  (almashmoqda: crossfade)"
                          : AudioEngine::GetMusicHandle() == 0 ? "  (eskisi so'nmoqda)" : "";
        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), ICON_FA_MUSIC "  Musiqa: %s%s", Project::GetAssetRelativePath(music).c_str(),
                           state.c_str());
        if (uint32_t handle = AudioEngine::GetMusicHandle())
        {
            float length = AudioEngine::GetLength(handle);
            if (length > 0.0f)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s / %s", AssetInfo::FormatDuration(AudioEngine::GetCursor(handle)).c_str(),
                                    AssetInfo::FormatDuration(length).c_str());
            }
            if (AudioEngine::IsStreaming(handle))
            {
                ImGui::SameLine();
                ImGui::TextDisabled(ICON_FA_WAVE_SQUARE " oqim");
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(AudioEngine::IsMusicPaused() ? ICON_FA_PLAY "##music" : ICON_FA_PAUSE "##music"))
        {
            if (AudioEngine::IsMusicPaused())
                AudioEngine::ResumeMusic();
            else
                AudioEngine::PauseMusic();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_STOP "##music"))
            AudioEngine::StopMusic(1.0f);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Musiqani so'ndirib to'xtatish (1 s)");
    }

    bool AudioMixerPanel::DrawAddGroupPopup(AudioSettings& settings)
    {
        bool added = false;
        if (!ImGui::BeginPopup("##addGroup"))
            return false;
        ImGui::TextColored(UI::AccentColor(), ICON_FA_SLIDERS "  Yangi guruh");
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
        bool enter = ImGui::InputTextWithHint("##name", Tr("Nomi (masalan Ovozlar)"), &m_NewGroupName, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
        if (ImGui::BeginCombo("Ota guruh", m_NewGroupParent.c_str()))
        {
            for (const auto& [name, depth] : settings.GetTreeOrder())
                if (ImGui::Selectable((std::string((size_t)depth * 3, ' ') + name).c_str(), name == m_NewGroupParent))
                    m_NewGroupParent = name;
            ImGui::EndCombo();
        }
        if (ImGui::Button(Tr(ICON_FA_PLUS "  Qo'shish")) || enter)
        {
            std::string name = m_NewGroupName;
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);
            if (name.empty())
                m_NewGroupError = "Nom kiriting";
            else if (settings.Find(name))
                m_NewGroupError = std::format("'{}' nomli guruh bor", name);
            else if (settings.AddGroup(name, m_NewGroupParent))
            {
                ZM_INFO("Audio guruhi qo'shildi: {} ({} ichida)", name, m_NewGroupParent);
                added = true;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!m_NewGroupError.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1.0f), "%s", m_NewGroupError.c_str());
        ImGui::EndPopup();
        return added;
    }

    void AudioMixerPanel::DrawMeter(const AudioLevel& level, float width, float height, bool silent)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        const float gap = 2.0f, bar = (width - gap) * 0.5f;
        const float yellow = DecibelPosition(-18.0f), red = DecibelPosition(-6.0f);
        auto y = [&](float t) { return origin.y + height * (1.0f - t); };
        float values[2] = { level.Left, level.Right };
        for (int c = 0; c < 2; c++)
        {
            float x0 = origin.x + c * (bar + gap), x1 = x0 + bar;
            draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height), IM_COL32(18, 20, 24, 255), 2.0f);
            float t = values[c] > 0.0f ? DecibelPosition(AudioGain::ToDecibels(values[c])) : 0.0f;
            if (t <= 0.0f)
                continue;
            const float alpha = silent ? 0.35f : 1.0f;
            auto segment = [&](float from, float to, ImVec4 color) {
                float a = std::min(from, t), b = std::min(to, t);
                if (b > a)
                    draw->AddRectFilled(ImVec2(x0, y(b)), ImVec2(x1, y(a)), ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alpha)));
            };
            segment(0.0f, yellow, ImVec4(0.25f, 0.82f, 0.45f, 1.0f));
            segment(yellow, red, ImVec4(0.92f, 0.82f, 0.25f, 1.0f));
            segment(red, 1.0f, ImVec4(0.95f, 0.32f, 0.25f, 1.0f));
        }
        // 0 dB line and the held peak.
        float zero = y(DecibelPosition(0.0f));
        draw->AddLine(ImVec2(origin.x - 1.0f, zero), ImVec2(origin.x + width + 1.0f, zero), IM_COL32(200, 200, 210, 120));
        if (level.Hold > 0.0f)
        {
            float hold = y(DecibelPosition(AudioGain::ToDecibels(level.Hold)));
            ImU32 color = level.Hold >= 1.0f ? IM_COL32(255, 80, 60, 255) : IM_COL32(235, 235, 240, 220);
            draw->AddLine(ImVec2(origin.x, hold), ImVec2(origin.x + width, hold), color, 2.0f);
        }
        ImGui::Dummy(ImVec2(width, height));
    }

    bool AudioMixerPanel::DrawStrip(AudioSettings& settings, const std::string& name, int depth, float height)
    {
        AudioGroupSettings* group = settings.Find(name);
        if (!group)
            return false;
        bool changed = false;
        const bool isMaster = name == AudioSettings::Master;
        const float font = ImGui::GetFontSize();
        const float width = font * 6.4f, pad = 6.0f;
        const float effective = settings.GetEffectiveGain(name);
        const bool silent = effective <= 0.0f;
        // A low window: no route line and no voice count (both in the tooltips).
        const bool compact = height < font * 12.5f;

        ImGui::PushID(name.c_str());
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 start = ImGui::GetCursorScreenPos();
        ImVec2 end(start.x + width, start.y + height);
        draw->AddRectFilled(start, end, isMaster ? IM_COL32(36, 44, 44, 255) : IM_COL32(32, 34, 40, 255), 5.0f);
        draw->AddRect(start, end, IM_COL32(60, 64, 74, 255), 5.0f);
        draw->AddRectFilled(start, ImVec2(end.x, start.y + 4.0f), BranchColor(settings, name), 5.0f, ImDrawFlags_RoundCornersTop);

        ImGui::BeginGroup();
        // Name and where it goes.
        ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + 8.0f));
        if (ImFont* bold = ImGuiLayer::GetBoldFont())
            ImGui::PushFont(bold);
        std::string title = FitText(name, width - 2.0f * pad);
        ImGui::SetCursorScreenPos(ImVec2(start.x + (width - ImGui::CalcTextSize(title.c_str()).x) * 0.5f, start.y + 8.0f));
        ImGui::TextColored(silent ? ImVec4(0.55f, 0.57f, 0.62f, 1.0f) : ImVec4(0.92f, 0.94f, 0.97f, 1.0f), "%s", title.c_str());
        if (ImGuiLayer::GetBoldFont())
            ImGui::PopFont();
        size_t voices = AudioEngine::GetGroupVoiceCount(name);
        std::string details = std::format("{} -> {}\nShu guruhda o'ynayotgan ovozlar: {}\nMixerdan keyingi balandlik: {}", name,
                                          isMaster ? std::string("chiqish") : group->Parent, voices, FormatDecibels(effective));
        if (group->Filter.LowPass)
            details += "\nLow-pass: " + UI::FormatHz(group->Filter.GetLowPass());
        if (group->Filter.HighPass)
            details += "\nHigh-pass: " + UI::FormatHz(group->Filter.GetHighPass());
        if (ImGui::IsItemHovered())
            UI::Tooltip(details.c_str());
        if (!compact)
        {
            std::string route = isMaster ? std::string("chiqish") : FitText("> " + group->Parent, width - 2.0f * pad);
            ImGui::SetCursorScreenPos(ImVec2(start.x + (width - ImGui::CalcTextSize(route.c_str()).x) * 0.5f, ImGui::GetCursorScreenPos().y));
            ImGui::TextDisabled("%s", route.c_str());
        }
        (void)depth;

        // Fader | meter | scale.
        const float bottom = (compact ? font * 2.9f : font * 4.6f) + ImGui::GetFrameHeight() + 2.0f; // + the filter button
        const float top = ImGui::GetCursorScreenPos().y + 4.0f;
        const float faderHeight = std::max(40.0f, end.y - bottom - top);
        const float faderWidth = font * 1.45f, meterWidth = font * 0.95f;
        float decibels = group->Volume > 0.0f ? AudioGain::ToDecibels(group->Volume) : AudioGain::MinDecibels;
        ImGui::SetCursorScreenPos(ImVec2(start.x + pad, top));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.11f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.14f, 0.15f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.16f, 0.17f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.78f, 0.80f, 0.84f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, UI::AccentColor());
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 12.0f);
        if (ImGui::VSliderFloat("##fader", ImVec2(faderWidth, faderHeight), &decibels, AudioGain::MinDecibels, s_MaxDecibels, ""))
        {
            group->Volume = std::clamp(AudioGain::FromDecibels(decibels), 0.0f, AudioSettings::MaxVolume);
            changed = true;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(5);
        if (ImGui::IsItemHovered())
        {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                group->Volume = 1.0f;
                changed = true;
            }
            UI::Tooltip(std::format("{}: {} ({:.0f}%)\nIkki marta bosish: 0 dB", name, FormatDecibels(group->Volume), group->Volume * 100.0f).c_str());
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x + pad + faderWidth + 5.0f, top));
        DrawMeter(AudioEngine::GetGroupLevel(name), meterWidth, faderHeight, silent);
        // Scale.
        if (faderHeight > font * 6.0f)
        {
            const float x = start.x + pad + faderWidth + 5.0f + meterWidth + 4.0f;
            for (float mark : { 6.0f, 0.0f, -12.0f, -24.0f, -48.0f })
            {
                float yMark = top + faderHeight * (1.0f - DecibelPosition(mark));
                std::string text = mark > 0.0f ? std::format("+{:.0f}", mark) : std::format("{:.0f}", mark);
                draw->AddLine(ImVec2(x - 3.0f, yMark), ImVec2(x - 1.0f, yMark), IM_COL32(140, 145, 155, 200));
                draw->AddText(ImGui::GetFont(), font * 0.78f, ImVec2(x, yMark - font * 0.4f), IM_COL32(140, 145, 155, 220), text.c_str());
            }
        }

        // Value, mute / solo, voices.
        float y = top + faderHeight + 4.0f;
        std::string value = FormatDecibels(group->Volume);
        ImGui::SetCursorScreenPos(ImVec2(start.x + (width - ImGui::CalcTextSize(value.c_str()).x) * 0.5f, y));
        ImGui::TextColored(group->Volume > 1.0f ? ImVec4(0.95f, 0.70f, 0.35f, 1.0f) : ImVec4(0.85f, 0.87f, 0.90f, 1.0f), "%s", value.c_str());
        const float buttonWidth = (width - 2.0f * pad - 4.0f) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(start.x + pad, ImGui::GetCursorScreenPos().y + 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, group->Muted ? ImVec4(0.80f, 0.25f, 0.22f, 1.0f) : ImVec4(0.22f, 0.23f, 0.27f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, group->Muted ? ImVec4(0.90f, 0.32f, 0.28f, 1.0f) : ImVec4(0.30f, 0.31f, 0.36f, 1.0f));
        if (ImGui::Button(Tr("M"), ImVec2(buttonWidth, 0)))
        {
            group->Muted = !group->Muted;
            changed = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            UI::Tooltip(group->Muted ? "O'chirilgan (mute) - yoqish" : "O'chirish (mute)");
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, group->Solo ? ImVec4(0.90f, 0.75f, 0.20f, 1.0f) : ImVec4(0.22f, 0.23f, 0.27f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, group->Solo ? ImVec4(0.95f, 0.82f, 0.30f, 1.0f) : ImVec4(0.30f, 0.31f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, group->Solo ? ImVec4(0.10f, 0.10f, 0.10f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::BeginDisabled(isMaster);
        if (ImGui::Button(Tr("S"), ImVec2(buttonWidth, 0)))
        {
            group->Solo = !group->Solo;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip(isMaster ? "Master'ni yakka qilib bo'lmaydi" : "Solo: faqat shu guruh (va bolalari) eshitiladi; saqlanmaydi");

        // The group's filter: what it does now, a click edits it.
        {
            const AudioFilterSettings& filter = group->Filter;
            std::string label = filter.LowPass && filter.HighPass ? std::string("LP + HP")
                              : filter.LowPass                    ? "LP " + ShortHz(filter.GetLowPass())
                              : filter.HighPass                   ? "HP " + ShortHz(filter.GetHighPass())
                                                                  : std::string(ICON_FA_FILTER);
            ImGui::SetCursorScreenPos(ImVec2(start.x + pad, ImGui::GetCursorScreenPos().y + 2.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, filter.IsActive() ? ImVec4(0.22f, 0.40f, 0.70f, 1.0f) : ImVec4(0.22f, 0.23f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, filter.IsActive() ? ImVec4(0.28f, 0.48f, 0.80f, 1.0f) : ImVec4(0.30f, 0.31f, 0.36f, 1.0f));
            if (!filter.IsActive())
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Button((label + "##filter").c_str(), ImVec2(width - 2.0f * pad, 0)))
                ImGui::OpenPopup("##filterPopup");
            ImGui::PopStyleColor(filter.IsActive() ? 2 : 3);
            if (ImGui::IsItemHovered())
            {
                std::string tip = "Filtr (bosing: sozlash)";
                if (filter.LowPass)
                    tip += "\nLow-pass " + UI::FormatHz(filter.GetLowPass()) + ": yuqori chastotalar bo'g'iladi";
                if (filter.HighPass)
                    tip += "\nHigh-pass " + UI::FormatHz(filter.GetHighPass()) + ": past chastotalar kesiladi";
                UI::Tooltip(tip.c_str());
            }
            if (ImGui::BeginPopup("##filterPopup"))
            {
                ImGui::TextDisabled("%s guruhi filtri (ovozlari va bola guruhlari)", name.c_str());
                ImGui::Separator();
                const float rowWidth = font * 14.0f;
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Low-pass");
                ImGui::SameLine(font * 5.5f);
                changed |= UI::AudioFilterRow("lowPass", group->Filter.LowPass, group->Filter.LowPassCutoff, rowWidth);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("High-pass");
                ImGui::SameLine(font * 5.5f);
                changed |= UI::AudioFilterRow("highPass", group->Filter.HighPass, group->Filter.HighPassCutoff, rowWidth);
                ImGui::TextDisabled("Masalan suv ostida: SFX low-pass 500 Hz. Skriptda: Audio::SetGroupLowPass(\"%s\", 500)", name.c_str());
                ImGui::EndPopup();
            }
        }
        if (!compact)
        {
            std::string count = silent && !group->Muted ? std::string(ICON_FA_VOLUME_XMARK " jim")
                              : voices ? std::format(ICON_FA_VOLUME_HIGH " {}", voices) : std::string("-");
            ImGui::SetCursorScreenPos(ImVec2(start.x + (width - ImGui::CalcTextSize(count.c_str()).x) * 0.5f, ImGui::GetCursorScreenPos().y + 2.0f));
            ImGui::TextDisabled("%s", count.c_str());
            if (ImGui::IsItemHovered())
                UI::Tooltip(details.c_str());
        }
        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(width, height));
        ImGui::EndGroup();

        // Right click: parent, reset, remove.
        if (ImGui::BeginPopupContextItem("##strip"))
        {
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::Separator();
            if (ImGui::BeginMenu(Tr("Ota guruh"), !isMaster))
            {
                for (const auto& [other, otherDepth] : settings.GetTreeOrder())
                {
                    bool allowed = other != name && !settings.IsDescendantOf(other, name);
                    if (ImGui::MenuItem((std::string((size_t)otherDepth * 2, ' ') + other).c_str(), nullptr, other == group->Parent, allowed) &&
                        other != group->Parent && settings.SetParent(name, other))
                        changed = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(Tr("0 dB ga qaytarish")))
            {
                group->Volume = 1.0f;
                changed = true;
            }
            ImGui::Separator();
            bool builtIn = AudioSettings::IsBuiltIn(name);
            if (ImGui::MenuItem(Tr(ICON_FA_TRASH "  O'chirish"), nullptr, false, !builtIn))
            {
                std::string parent = group->Parent;
                if (settings.RemoveGroup(name))
                {
                    ZM_INFO("Audio guruhi o'chirildi: {} (bolalari va ovozlari {} ga o'tdi)", name, parent);
                    changed = true;
                }
            }
            if (builtIn && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Asosiy guruhlar (Master, Music, SFX, UI) o'chirilmaydi");
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

}
