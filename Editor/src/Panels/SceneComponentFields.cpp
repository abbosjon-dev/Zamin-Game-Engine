#include "SceneHierarchyPanel.h"
#include "Zamin/Utils/StringUtils.h"
#include "AssetInspectorPanel.h"
#include "AnimatorEditing.h"
#include "../UI.h"

#include <Zamin/Utils/FileSystem.h>
#include <Zamin/Utils/Image.h>

#include <imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <set>

#include "InspectorNotes.h"

namespace Zamin {
    bool SceneHierarchyPanel::DrawEntityRefField(const char* name, EntityRef& ref, const char* tooltip)
    {
        UI::PropertyLabel(name, tooltip);
        ImGui::PushID(name);
        bool changed = false;

        Entity target = ref.ID && m_Context ? m_Context->GetEntityByUUID(ref.ID) : Entity();
        // A reference that points outside the prefab (prefab mode: an entity that is not in the
        // prefab; an instance: an entity outside the instance) is kept as it is, but an Apply does not
        // write it into the prefab (other scenes do not have that entity).
        const bool external = ref.ID && IsPrefabExternalReference(ref.ID);
        bool missing = ref.ID && !target && !external;

        float buttonWidth = ImGui::GetFrameHeight();
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float width = std::max(20.0f, ImGui::GetContentRegionAvail().x - (buttonWidth + spacing) * 2.0f);
        std::string display;
        if (!ref.ID) display = ICON_FA_LINK "  (yo'q)";
        else if (missing) display = ICON_FA_TRIANGLE_EXCLAMATION "  (topilmadi)";
        else if (!target) display = std::string(ICON_FA_LINK_SLASH "  ") + Tr("(prefabdan tashqarida)");
        else display = std::string(external ? ICON_FA_LINK_SLASH "  " : "") + GetEntityIcon(target) + "  " + target.GetName();

        ImVec4 textColor = missing ? ImVec4(0.95f, 0.45f, 0.35f, 1.0f)
                         : external ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f)
                         : !ref.ID ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                         : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        if (ImGui::Button(display.c_str(), ImVec2(width, 0)))
        {
            m_EntityPickerFilter.clear();
            ImGui::OpenPopup("##entityPicker");
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            UI::Tooltip(external ? Tr("Qo'llashda yozilmaydi (prefabdan tashqarida): havola shu sahnada qoladi, prefabga va uning "
                                      "boshqa nusxalariga o'tmaydi")
                        : missing ? "Bog'langan obyekt sahnada yo'q. Boshqasini tanlang yoki tozalang"
                                  : "Ierarxiyadan obyektni tortib tashlang yoki bosib tanlang");

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
            {
                uint64_t id = *(const uint64_t*)payload->Data;
                if (id != ref.ID)
                {
                    ref.ID = id;
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopup("##entityPicker"))
        {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            ImGui::InputTextWithHint("##filter", Tr(ICON_FA_MAGNIFYING_GLASS "  Qidirish..."), &m_EntityPickerFilter);
            ImGui::Separator();
            std::string filter = StringUtils::ToLower(m_EntityPickerFilter);
            ImGui::BeginChild("##list", ImVec2(ImGui::GetFontSize() * 16.0f, ImGui::GetFontSize() * 14.0f), ImGuiChildFlags_None);
            if (ImGui::Selectable(Tr(ICON_FA_XMARK "  (yo'q)"), ref.ID == 0))
            {
                if (ref.ID) changed = true;
                ref.ID = 0;
                ImGui::CloseCurrentPopup();
            }
            if (m_Context)
            {
                for (Entity e : m_Context->GetEntitiesInOrder())
                {
                    const std::string& entityName = e.GetName();
                    if (!filter.empty() && StringUtils::ToLower(entityName).find(filter) == std::string::npos)
                        continue;
                    ImGui::PushID((int)(uint32_t)e);
                    std::string label = std::string(GetEntityIcon(e)) + "  " + entityName;
                    if (ImGui::Selectable(label.c_str(), (uint64_t)e.GetUUID() == ref.ID))
                    {
                        if ((uint64_t)e.GetUUID() != ref.ID) changed = true;
                        ref.ID = e.GetUUID();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        // Locate: select the referenced entity in the hierarchy.
        ImGui::SameLine();
        ImGui::BeginDisabled(!target);
        if (ImGui::Button(ICON_FA_CROSSHAIRS, ImVec2(buttonWidth, 0)))
            SetSelectedEntity(target);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && target)
            UI::Tooltip("Ierarxiyada ko'rsatish");

        // Clear
        ImGui::SameLine();
        ImGui::BeginDisabled(!ref.ID);
        if (ImGui::Button(ICON_FA_XMARK, ImVec2(buttonWidth, 0)))
        {
            ref.ID = 0;
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ref.ID)
            UI::Tooltip("Tozalash");

        ImGui::PopID();
        return changed;
    }

    bool SceneHierarchyPanel::IsPrefabExternalReference(uint64_t id) const
    {
        if (!id || !m_Context)
            return false;
        Entity target = m_Context->GetEntityByUUID(UUID(id));
        if (m_PrefabStageMode)
            return !target; // the prefab's entities are all there is
        if (!m_PrefabInspector.Entity)
            return false;
        Entity root = PrefabUtility::GetInstanceRoot(m_Context->GetEntityByUUID(m_PrefabInspector.Entity));
        if (!root || !target)
            return false;
        for (Entity e = target; e;)
        {
            if (e == root)
                return false;
            const UUID parent = e.GetComponent<RelationshipComponent>().Parent;
            e = (uint64_t)parent ? m_Context->GetEntityByUUID(parent) : Entity();
        }
        return true;
    }

    static std::string TrimTag(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return {};
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    bool SceneHierarchyPanel::PeekTransformClipboard(TransformComponent& out)
    {
        // The system clipboard first (copied in another editor window), then our own copy.
        const char* text = ImGui::GetClipboardText();
        if (text && SceneSerializer::DeserializeTransform(text, out))
            return true;
        if (m_TransformClipboard)
        {
            out = *m_TransformClipboard;
            return true;
        }
        return false;
    }

    bool SceneHierarchyPanel::DrawTransformMenu(Entity entity)
    {
        // Reset / paste act on every selected entity (the inspector shows the primary one).
        std::vector<Entity> targets;
        if (IsSelected(entity))
            for (Entity e : m_Selection.GetEntities())
                if (e) targets.push_back(e);
        if (targets.empty())
            targets.push_back(entity);
        if (ImGui::IsWindowAppearing()) // read the clipboard once per opening
            m_CanPasteTransform = PeekTransformClipboard(m_PasteTransform);

        bool changed = false;
        auto apply = [&](auto&& edit) {
            for (Entity e : targets)
                edit(e.GetComponent<TransformComponent>());
            changed = true;
        };

        if (targets.size() > 1)
        {
            ImGui::TextDisabled(ICON_FA_OBJECT_GROUP "  %zu ta obyektga qo'llanadi", targets.size());
            ImGui::Separator();
        }
        if (ImGui::MenuItem(Tr(ICON_FA_ARROW_ROTATE_LEFT "  Tiklash (hammasi)")))
            apply([](TransformComponent& t) { t = TransformComponent(); });
        if (ImGui::MenuItem(Tr("      Pozitsiyani tiklash")))
            apply([](TransformComponent& t) { t.Translation = glm::vec3(0.0f); });
        if (ImGui::MenuItem(Tr("      Aylanishni tiklash")))
            apply([](TransformComponent& t) { t.Rotation = glm::vec3(0.0f); });
        if (ImGui::MenuItem(Tr("      Masshtabni tiklash")))
            apply([](TransformComponent& t) { t.Scale = glm::vec3(1.0f); });

        ImGui::Separator();
        if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Nusxalash")))
        {
            const TransformComponent& t = entity.GetComponent<TransformComponent>();
            m_TransformClipboard = t;
            m_PasteTransform = t;
            m_CanPasteTransform = true;
            ImGui::SetClipboardText(SceneSerializer::SerializeTransform(t).c_str());
            ZM_INFO("Transform nusxalandi: {}", entity.GetName());
        }
        const TransformComponent clip = m_PasteTransform;
        const bool canPaste = m_CanPasteTransform;
        if (ImGui::MenuItem(Tr(ICON_FA_PASTE "  Joylash (hammasi)"), nullptr, false, canPaste))
            apply([&](TransformComponent& t) { t = clip; });
        if (ImGui::MenuItem(Tr("      Faqat pozitsiyani joylash"), nullptr, false, canPaste))
            apply([&](TransformComponent& t) { t.Translation = clip.Translation; });
        if (ImGui::MenuItem(Tr("      Faqat aylanishni joylash"), nullptr, false, canPaste))
            apply([&](TransformComponent& t) { t.Rotation = clip.Rotation; });
        if (ImGui::MenuItem(Tr("      Faqat masshtabni joylash"), nullptr, false, canPaste))
            apply([&](TransformComponent& t) { t.Scale = clip.Scale; });
        if (canPaste)
        {
            glm::vec3 r = glm::degrees(clip.Rotation);
            ImGui::Separator();
            ImGui::TextDisabled("Buferda: P(%.2f, %.2f, %.2f)", clip.Translation.x, clip.Translation.y, clip.Translation.z);
            ImGui::TextDisabled("          A(%.1f, %.1f, %.1f)  M(%.2f, %.2f, %.2f)", r.x, r.y, r.z, clip.Scale.x, clip.Scale.y, clip.Scale.z);
        }
        else
        {
            ImGui::Separator();
            ImGui::TextDisabled("Bufer bo'sh: avval 'Nusxalash'ni bosing");
        }
        return changed;
    }

    // ------------------------------------------------------------------ localization
    bool SceneHierarchyPanel::DrawLocalizationKey(TextComponent& c)
    {
        bool changed = false;
        UI::PropertyLabel("Lokalizatsiya kaliti", "Til fayllaridagi kalit (masalan menu.start): matn joriy tildagi satr bilan almashadi va til "
                                                  "o'zgarganda o'zi yangilanadi. Hech bir tilda bo'lmasa yuqoridagi Matn ko'rsatiladi. "
                                                  "Kalitlar: Oyna > Lokalizatsiya.");
        ImGui::PushID("localizationKey");
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
        changed |= ImGui::InputTextWithHint("##key", Tr("(yo'q: Matn o'zi)"), &c.LocalizationKey);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LANGUAGE, ImVec2(button, 0.0f)))
        {
            m_LocalizationKeyFilter.clear();
            ImGui::OpenPopup("##keys");
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Til fayllaridan kalit tanlash");
        if (ImGui::BeginPopup("##keys"))
        {
            // Every key of the offered languages.
            std::vector<std::string> keys;
            for (const std::string& code : Localization::GetLanguages())
                if (const StringTable* table = Localization::GetTable(code))
                    for (const auto& [key, entry] : table->Strings)
                        if (std::find(keys.begin(), keys.end(), key) == keys.end())
                            keys.push_back(key);
            std::sort(keys.begin(), keys.end());
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputTextWithHint("##filter", Tr(ICON_FA_MAGNIFYING_GLASS "  qidirish"), &m_LocalizationKeyFilter);
            if (keys.empty())
                ImGui::TextDisabled("Til fayllarida kalit yo'q (Loyiha sozlamalari > Lokalizatsiya)");
            if (ImGui::BeginChild("##list", ImVec2(420.0f, std::min(320.0f, 24.0f * (float)keys.size() + 8.0f))))
                for (const std::string& key : keys)
                {
                    const std::string* text = Localization::Find(key);
                    if (!m_LocalizationKeyFilter.empty() && !ProjectSettingsIndex::TextContains(key, m_LocalizationKeyFilter) &&
                        !(text && ProjectSettingsIndex::TextContains(*text, m_LocalizationKeyFilter)))
                        continue;
                    if (ImGui::Selectable(key.c_str(), key == c.LocalizationKey))
                    {
                        c.LocalizationKey = key;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (text)
                    {
                        ImGui::SameLine(170.0f);
                        ImGui::TextDisabled("%s", text->c_str());
                    }
                }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::PopID();
        if (!c.LocalizationKey.empty())
        {
            UI::PropertyLabel("");
            if (const std::string* text = Localization::Find(c.LocalizationKey))
                ImGui::TextDisabled(ICON_FA_LANGUAGE " %s: %s", Localization::GetLanguage().c_str(), text->c_str());
            else
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.32f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " Kalit hech bir tilda yo'q: Matn ko'rsatiladi");
        }
        if (changed)
            c.ResetLocalized(); // looked up again before it is drawn
        return changed;
    }

    // ------------------------------------------------------------------ audio
    bool SceneHierarchyPanel::DrawSpatialDefaultsRow(float& minDistance, float& maxDistance)
    {
        const AudioSpatialSettings& defaults = Audio::GetSpatialDefaults();
        static const char* s_RolloffLabels[] = { "teskari (Inverse)", "chiziqli (Linear)", "eksponensial", "masofasiz" };
        UI::PropertyLabel("", "Loyihaning 3D ovoz standartlari: yangi Audio Source va PlaySoundAt shu masofalar bilan boshlanadi; "
                              "masofa modeli hamma 3D ovozlarga ta'sir qiladi (Loyiha sozlamalari > Audio).");
        ImGui::PushID("spatialDefaults");
        const bool same = std::abs(minDistance - defaults.MinDistance) < 1e-4f && std::abs(maxDistance - defaults.MaxDistance) < 1e-4f;
        ImGui::BeginDisabled(same);
        bool changed = false;
        if (ImGui::SmallButton(Tr(ICON_FA_ROTATE_LEFT "  Standart")))
        {
            minDistance = defaults.MinDistance;
            maxDistance = defaults.MaxDistance;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("loyiha: %.1f - %.1f m, %s", defaults.MinDistance, defaults.MaxDistance,
                            s_RolloffLabels[std::clamp((int)defaults.Rolloff, 0, 3)]);
        ImGui::PopID();
        return changed;
    }

    bool SceneHierarchyPanel::DrawAudioOutput(std::string& output)
    {
        bool changed = false;
        const AudioSettings& mixer = AudioEngine::GetMixerSettings();
        UI::PropertyLabel("Chiqish guruhi", "Ovoz mixerning qaysi guruhida ijro etiladi: guruh balandligi, o'chirish (mute) "
                                            "va solo unga va ota guruhlariga ta'sir qiladi (Oyna > Audio Mixer).");
        ImGui::PushID("audioOutput");
        const bool known = mixer.Find(output) != nullptr;
        std::string preview = known ? std::format(ICON_FA_SLIDERS "  {}", output)
                                    : std::format(ICON_FA_TRIANGLE_EXCLAMATION "  {} (yo'q - SFX)", output);
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::BeginCombo("##group", preview.c_str()))
        {
            for (const auto& [name, depth] : mixer.GetTreeOrder())
            {
                std::string label = std::string((size_t)depth * 3, ' ') + name;
                float gain = mixer.GetEffectiveGain(name);
                if (ImGui::Selectable(label.c_str(), name == output))
                {
                    output = name;
                    changed = true;
                }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.6f);
                if (gain <= 0.0f)
                    ImGui::TextDisabled(ICON_FA_VOLUME_XMARK "  jim");
                else
                    ImGui::TextDisabled("%.1f dB", AudioGain::ToDecibels(gain));
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
        {
            if (!known)
                UI::Tooltip("Bu guruh mixerda yo'q (o'chirilgan): ovoz SFX guruhida ijro etiladi");
            else if (float gain = mixer.GetEffectiveGain(output); gain <= 0.0f)
                UI::Tooltip("Guruh yoki uning ota guruhi o'chirilgan (mute / solo): ovoz eshitilmaydi");
            else
                UI::Tooltip(std::format("Mixerdan keyingi balandlik: {:.1f} dB", AudioGain::ToDecibels(gain)).c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SLIDERS, ImVec2(button, 0)) && OnAudioMixerRequested)
            OnAudioMixerRequested();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Audio Mixer oynasini ochish");
        ImGui::PopID();
        return changed;
    }

    void SceneHierarchyPanel::DrawAudioSourceState(Entity entity, AudioSourceComponent& c)
    {
        UI::PropertyHeader(ICON_FA_WAVE_SQUARE "  Holat (o'yin)", "Hozir ijro etilayotgan ovoz: o'yin davomida jonli");
        SoundInfo info;
        const bool alive = m_Context->GetAudioInfo(entity, info);
        auto formatTime = [](float seconds) {
            seconds = std::max(0.0f, seconds);
            int minutes = (int)(seconds / 60.0f);
            return std::format("{}:{:04.1f}", minutes, seconds - minutes * 60.0f);
        };

        UI::PropertyLabel("Holat");
        if (!AudioEngine::IsAvailable())
            ImGui::TextDisabled(ICON_FA_TRIANGLE_EXCLAMATION "  Audio qurilmasi yo'q");
        else if (alive && info.Paused)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_PAUSE "  Pauza");
        else if (alive && info.Playing)
            ImGui::TextColored(UI::AccentColor(), ICON_FA_PLAY "  Ijro etilmoqda%s", info.Looping ? "  (takror)" : "");
        else if (c.RuntimeHandle && AudioEngine::WasStolen(c.RuntimeHandle))
        {
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.35f, 1.0f), ICON_FA_STOP "  To'xtatildi: ovozlar limiti");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Limit to'lgani uchun o'rni boshqa (muhimroq yoki yangi) ovozga berildi");
        }
        else
            ImGui::TextDisabled(ICON_FA_STOP "  To'xtagan");

        // Time: a progress bar; a click (or drag) on it jumps there.
        UI::PropertyLabel("Vaqt");
        {
            float fraction = alive && info.Length > 0.0f ? std::clamp(info.Time / info.Length, 0.0f, 1.0f) : 0.0f;
            std::string text = alive ? std::format("{} / {}", formatTime(info.Time), formatTime(info.Length)) : std::string("--");
            ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
            ImVec2 start = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, info.Paused ? ImVec4(0.75f, 0.6f, 0.25f, 1.0f) : UI::AccentColor());
            ImGui::ProgressBar(fraction, size, text.c_str());
            ImGui::PopStyleColor();
            if (alive && info.Length > 0.0f)
            {
                ImGui::SetCursorScreenPos(start);
                ImGui::InvisibleButton("##seek", size);
                if (ImGui::IsItemActive())
                {
                    float t = std::clamp((ImGui::GetIO().MousePos.x - start.x) / std::max(1.0f, size.x), 0.0f, 1.0f);
                    m_Context->SetAudioTime(entity, t * info.Length);
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Bosing yoki suring: shu joyga o'tish");
            }
        }

        UI::PropertyLabel("");
        ImGui::PushID("audioControls");
        if (ImGui::Button(alive ? ICON_FA_ROTATE_RIGHT "  Qayta" : ICON_FA_PLAY "  Ijro"))
            m_Context->PlayAudio(entity);
        if (ImGui::IsItemHovered())
            UI::Tooltip("PlayAudio(): boshidan (yangi tasodifiy balandlik / ton bilan)");
        ImGui::SameLine();
        ImGui::BeginDisabled(!alive);
        const bool paused = alive && info.Paused;
        if (ImGui::Button(paused ? ICON_FA_PLAY : ICON_FA_PAUSE))
        {
            if (paused)
                m_Context->ResumeAudio(entity);
            else
                m_Context->PauseAudio(entity);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip(paused ? "ResumeAudio(): to'xtagan joyidan davom" : "PauseAudio(): shu joyida to'xtatish");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP))
            m_Context->StopAudio(entity);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("StopAudio(): to'xtatish (ovoz bo'shatiladi)");
        ImGui::EndDisabled();
        ImGui::PopID();
        if (!alive)
            return;

        // What is heard: the sound's volume after its variation, the mixer and the distance.
        UI::PropertyLabel("Eshitilishi", "Balandlik x tasodifiy ko'paytuvchi x fayl balandligi x so'nish x mixer guruhi (ota guruhlari "
                                         "bilan) x masofa va yo'nalish konusi (3D)");
        {
            float db = AudioGain::ToDecibels(info.AudibleVolume);
            float fraction = std::clamp((db - AudioGain::MinDecibels) / (6.0f - AudioGain::MinDecibels), 0.0f, 1.0f);
            std::string text = info.AudibleVolume <= 0.0f || db <= AudioGain::MinDecibels ? std::string("jim") : std::format("{:+.1f} dB", db);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.8f, 0.45f, 1.0f));
            ImGui::ProgressBar(info.Paused ? 0.0f : fraction, ImVec2(-FLT_MIN, 0.0f), text.c_str());
            ImGui::PopStyleColor();
            if (info.ClipGain != 1.0f)
                ImGui::TextDisabled("balandlik %.2f x%.2f  fayl x%.2f", info.Volume, info.VolumeVariation, info.ClipGain);
            else
                ImGui::TextDisabled("balandlik %.2f x%.2f", info.Volume, info.VolumeVariation);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Manbaning balandligi, bu ijroda olgan tasodifiy ko'paytuvchisi va faylning o'z balandligi (import sozlamasi)");
            ImGui::TextDisabled("guruh %s %s", info.Group.empty() ? "-" : info.Group.c_str(),
                                info.GroupGain <= 0.0f ? "jim" : std::format("{:+.1f} dB", AudioGain::ToDecibels(info.GroupGain)).c_str());
            if (info.Spatial && info.Cone.IsActive())
            {
                ImGui::TextDisabled("masofa %.1f m x%.2f  konus x%.2f", info.Distance, info.Attenuation / std::max(info.ConeGain, 1e-6f),
                                    info.ConeGain);
                if (ImGui::IsItemHovered())
                    UI::Tooltip(info.ConeGain >= 0.999f ? "Tinglovchi ichki konus ichida (oldida): to'liq balandlik"
                                : info.ConeGain <= info.Cone.OuterGain + 1e-4f ? "Tinglovchi tashqi konusdan tashqarida (orqada)"
                                                                               : "Tinglovchi ichki va tashqi konus orasida");
            }
            else if (info.Spatial)
                ImGui::TextDisabled("masofa %.1f m x%.2f", info.Distance, info.Attenuation);
        }

        // The fade envelope: where it is and where it goes.
        UI::PropertyLabel("So'nish", "Fade konverti (0..1): FadeIn, FadeAudio yoki so'nib to'xtash");
        {
            std::string text = info.Fade == info.FadeTarget ? std::format("{:.0f}%", info.Fade * 100.0f)
                                                            : std::format("{:.0f}% -> {:.0f}%", info.Fade * 100.0f, info.FadeTarget * 100.0f);
            if (info.Stopping)
                text += "  so'nib to'xtamoqda";
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, info.Stopping ? ImVec4(0.9f, 0.5f, 0.3f, 1.0f) : ImVec4(0.45f, 0.65f, 0.95f, 1.0f));
            ImGui::ProgressBar(std::clamp(info.Fade, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f), text.c_str());
            ImGui::PopStyleColor();
        }
        if (info.Filter.IsActive())
        {
            UI::PropertyLabel("Filtr");
            std::string filter;
            if (info.Filter.LowPass)
                filter += "low-pass " + UI::FormatHz(info.Filter.GetLowPass());
            if (info.Filter.HighPass)
                filter += (filter.empty() ? "" : "   ") + std::string("high-pass ") + UI::FormatHz(info.Filter.GetHighPass());
            ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), ICON_FA_FILTER "  %s", filter.c_str());
        }

        UI::PropertyLabel("Ton (hozir)", "So'ralgan ton x tasodifiy ko'paytuvchi x Doppler");
        ImGui::Text("%.3f", info.AppliedPitch);
        ImGui::TextDisabled("tasodifiy x%.3f", info.PitchVariation);
        if (info.Spatial)
        {
            const float doppler = info.DopplerPitch;
            ImVec4 color = doppler > 1.002f ? ImVec4(0.95f, 0.7f, 0.35f, 1.0f) : doppler < 0.998f ? ImVec4(0.45f, 0.7f, 1.0f, 1.0f)
                                                                                                 : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
            ImGui::TextColored(color, "Doppler x%.3f", doppler);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Yaqinlashayotganda > 1 (baland), uzoqlashayotganda < 1 (past)");
            UI::PropertyLabel("Tezlik", "Manba harakatidan o'lchanadi (Doppler uchun)");
            ImGui::Text("%.1f m/s", glm::length(info.Velocity));
        }
        AudioVoiceStats stats = AudioEngine::GetVoiceStats();
        UI::PropertyLabel("Ovozlar", "Loyihada hozir ijro etilayotgan ovozlar / limit (musiqasiz)");
        ImGui::TextDisabled("%zu / %d%s%s", stats.Voices, stats.MaxVoices, info.Streamed ? "   " ICON_FA_WAVE_SQUARE " oqim" : "",
                            info.Preloaded ? "   " ICON_FA_MEMORY " xotirada" : "");
    }

    // ------------------------------------------------------------------ 2D renderers
    bool SceneHierarchyPanel::DrawSortingLayer(uint32_t& layer, int& order)
    {
        bool changed = false;
        const SortingLayerList& layers = Project::GetSortingLayers();
        UI::PropertyLabel("Saralash qatlami", "Orqadan oldinga: avval qatlam (Loyiha sozlamalari > Saralash qatlamlari "
                                              "ro'yxatidagi o'rni), keyin qatlamdagi tartib, keyin kameradan uzoqligi. "
                                              "(Y) qatlamda tartibdan oldin Y: yuqoriroqdagi orqada chiziladi.");
        ImGui::PushID("sortingLayer");
        const SortingLayer* current = layers.Find(layer);
        std::string preview = current ? std::format(ICON_FA_LAYER_GROUP "  {}{}", current->Name, current->YSort ? "  (Y)" : "")
                                      : std::format(ICON_FA_TRIANGLE_EXCLAMATION "  #{} (yo'q - Default)", layer);
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::BeginCombo("##layer", preview.c_str()))
        {
            const auto& list = layers.GetLayers();
            for (size_t i = 0; i < list.size(); i++)
            {
                ImGui::PushID((int)list[i].ID);
                std::string label = std::format("{}  {}{}", i + 1, list[i].Name, list[i].YSort ? "  (Y bo'yicha)" : "");
                if (ImGui::Selectable(label.c_str(), list[i].ID == layer))
                {
                    layer = list[i].ID;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (!current && ImGui::IsItemHovered())
            UI::Tooltip("Bu qatlam loyihada yo'q (o'chirilgan): Default qatlamida chiziladi");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PEN, ImVec2(button, 0)) && OnSortingLayersRequested)
            OnSortingLayersRequested();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Qatlamlarni tahrirlash (Loyiha sozlamalari)");
        ImGui::PopID();
        changed |= UI::PropertyInt("Qatlamdagi tartib", order);
        return changed;
    }

    bool SceneHierarchyPanel::DrawCollisionLayer(const char* label, int& layer, bool inherit, const char* tooltip)
    {
        bool changed = false;
        const CollisionLayers2D& layers = Project::GetCollisionLayers2D();
        UI::PropertyLabel(label, tooltip);
        ImGui::PushID(label);
        auto name = [&](int l) { return std::format("{}  {}", l, layers.GetName(l)); };
        std::string preview = layer < 0 ? std::string("(Rigidbody 2D qatlami)") : std::format(ICON_FA_LAYER_GROUP "  {}", name(layer));
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::BeginCombo("##collisionLayer", preview.c_str()))
        {
            if (inherit && ImGui::Selectable(Tr("(Rigidbody 2D qatlami)"), layer < 0))
            {
                layer = -1;
                changed = true;
            }
            std::vector<int> list = layers.GetNamedLayers();
            if (layer >= 0 && std::find(list.begin(), list.end(), layer) == list.end())
                list.insert(std::upper_bound(list.begin(), list.end(), layer), layer); // unnamed, but in use
            for (int l : list)
            {
                ImGui::PushID(l);
                if (ImGui::Selectable(name(l).c_str(), l == layer))
                {
                    layer = l;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PEN, ImVec2(button, 0)) && OnPhysicsLayersRequested)
            OnPhysicsLayersRequested();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Qatlamlar va qaysilari to'qnashishini tahrirlash (Loyiha sozlamalari > Fizika qatlamlari)");
        ImGui::PopID();
        return changed;
    }

    // ------------------------------------------------------------------ navigation
    // What a baked grid holds (or that it is not baked yet), and the bake button.
    // A behavior tree a script watches (WatchBehaviorTree): the branch running now, and folded out every
    // node with its last answer (bright: ticked in the last tick) and the blackboard.


    static bool DrawNavGridStatus(Scene* scene, Entity entity, const NavGrid& grid, bool baked, uint32_t bakeCount, float milliseconds,
                                  const char* extra, bool fromFile)
    {
        bool bakedNow = false;
        UI::PropertyHeader("Grid", "Grid o'yin boshlanganda (Boshida qurish) yoki tugma / skript bilan quriladi. \"Faylda saqlash\" yoqilsa - "
                                   "sahna saqlanganda .znav fayliga ham yoziladi va manbalar o'zgarmagan bo'lsa o'yin uni o'qiydi");
        UI::PropertyLabel("Holat");
        if (!baked || grid.IsEmpty())
            ImGui::TextDisabled("%s", Tr("qurilmagan"));
        else
        {
            const size_t walkable = grid.CountWalkable();
            ImGui::TextDisabled("%d x %d katak, %zu yuriladigan (%.0f%%)", grid.GetWidth(), grid.GetHeight(), walkable,
                                100.0 * (double)walkable / (double)std::max<size_t>(1, grid.GetCellCount()));
            UI::PropertyLabel("");
            ImGui::TextDisabled("%.2f ms, %u marta qurildi%s", milliseconds, bakeCount, extra ? extra : "");
            if (fromFile)
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), ICON_FA_FLOPPY_DISK "  %s", Tr(".znav faylidan o'qildi (manbalar o'zgarmagan)"));
            }
        }
        UI::PropertyLabel("");
        ImGui::BeginDisabled(!scene);
        if (ImGui::Button(Tr(ICON_FA_ARROWS_ROTATE "  Grid'ni qurish")) && scene)
            bakedNow = scene->BakeNavGrid(entity);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Hozirgi sahnadan qaytadan quradi va viewport'da ko'rsatadi (Gizmo'lar > Navigatsiya). O'yin paytida - agentlar yangi "
                        "grid bo'yicha yo'l topadi");
        return bakedNow;
    }

    static bool DrawNavPathSettings(NavGridPathSettings& paths)
    {
        bool changed = UI::PropertyBool("Diagonal", paths.Diagonals);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("8 qo'shni (diagonal qadam sqrt 2 turadi), o'chiq - faqat 4 tomon");
        if (paths.Diagonals)
        {
            changed |= UI::PropertyBool("Burchak kesish", paths.CutCorners);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Diagonal qadam bitta to'siq katak yonidan o'tishi mumkin (burchakka tegib o'tadi). O'chiq - ikkala yon katak "
                            "ham bo'sh bo'lishi kerak");
        }
        changed |= UI::PropertyBool("Boshida qurish", paths.BakeOnStart);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("O'yin boshlanganda (skriptlardan oldin) quriladi. O'chiq bo'lsa skriptdan: GetScene().BakeNavGrid(entity)");
        if (paths.BakeOnStart)
        {
            changed |= UI::PropertyBool("Faylda saqlash (.znav)", paths.CacheBake);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Sahna saqlanganda grid qurilib sahna yonidagi .znav fayliga yoziladi; o'yin uni qurish o'rniga o'qiydi - "
                            "grid nimadan qurilgan bo'lsa (sozlamalar, tilemap / statik collider'lar) o'zgarmagan bo'lsagina. Katta 3D "
                            "grid'lar uchun: yuklanish tezlashadi. O'chiq - har safar quriladi (doim yangi)");
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawNavGrid2D(Entity entity, NavGrid2DComponent& c)
    {
        bool changed = false;
        InactiveComponentNote(entity, "Obyekt nofaol: grid qurilmaydi, agentlar uni ishlatmaydi");
        UI::PropertyHeader("Maydon");
        changed |= UI::PropertyBool("Avto chegara", c.AutoBounds);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Manbalar (tilemap'lar, statik collider'lar) egallagan to'rtburchak. O'chiq - obyekt pozitsiyasi + siljish atrofida "
                        "o'lcham");
        if (!c.AutoBounds)
        {
            changed |= UI::PropertyFloat2("Siljish", c.Offset, 0.05f);
            UI::PropertyLabel("O'lcham", "Dunyo birligida (grid dunyo o'qlari bo'yicha: obyekt burilishi / masshtabi ta'sir qilmaydi)");
            if (ImGui::DragFloat2("##size", &c.Size.x, 0.1f, 0.0f, 10000.0f, "%.2f"))
            {
                c.Size = glm::max(c.Size, glm::vec2(0.0f));
                changed = true;
            }
        }
        UI::PropertyLabel("Katak o'lchami", "Bir katak tomoni (dunyo birligi). Kichik - aniqroq, lekin ko'proq katak");
        if (ImGui::DragFloat("##cell", &c.CellSize, 0.01f, NavGrid2DComponent::MinCellSize, NavGrid2DComponent::MaxCellSize, "%.2f",
                             ImGuiSliderFlags_AlwaysClamp))
            changed = true;
        UI::PropertyLabel("Agent radiusi", "To'siqqa shundan yaqin kataklar ham yopiladi (agentlar devorga tegmay yuradi)");
        if (ImGui::DragFloat("##radius", &c.AgentRadius, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            changed = true;

        UI::PropertyHeader("Manbalar");
        changed |= UI::PropertyBool("Tilemap'lar", c.UseTilemaps);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Collision qatlamlaridagi to'qnashadigan plitkalar yopiq (tileset'dagi plitka sozlamasi: Solid; Bir tomonlama - "
                        "pastdagi belgi bilan)");
        if (c.UseTilemaps)
        {
            changed |= DrawEntityRefField("Tilemap", c.Tilemap, "Faqat shu tilemap (tanlanmasa - sahnadagi hamma tilemap'lar)");
            changed |= UI::PropertyBool("Bir tomonlama", c.OneWayBlocks);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("OneWay plitkalar va bir tomonlama box collider'lar ham to'siq (yuqoridan ko'rinishdagi o'yinlar uchun)");
        }
        changed |= UI::PropertyBool("Statik collider'lar", c.UseColliders);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Static Rigidbody 2D'ning box / circle / polygon / edge collider'lari (trigger'lar va harakatlanuvchi jismlar hisobga "
                        "olinmaydi)");
        changed |= DrawLayerMask("Qatlamlar", c.LayerMask, "Faqat shu to'qnashuv qatlamlaridagi tilemap'lar va collider'lar to'siq bo'ladi");

        UI::PropertyHeader("Yo'llar");
        changed |= DrawNavPathSettings(c.Paths);
        changed |= UI::PropertyBool("Qayta qurish", c.AutoRebuild);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("O'yin paytida manbalar o'zgarsa (plitka qo'yilsa / buzilsa, statik collider siljisa) soniyasiga bir necha marta "
                        "tekshirib qayta quradi");

        if (DrawNavGridStatus(m_Context.get(), entity, c.Grid, c.Baked, c.BakeCount, c.BakeMilliseconds, nullptr, c.FromBakeFile))
            ZM_INFO("NavGrid 2D '{}' qurildi: {}x{} katak, {} tasi yuriladigan", entity.GetName(), c.Grid.GetWidth(), c.Grid.GetHeight(),
                    c.Grid.CountWalkable());
        return changed;
    }

    bool SceneHierarchyPanel::DrawNavGrid3D(Entity entity, NavGrid3DComponent& c)
    {
        bool changed = false;
        InactiveComponentNote(entity, "Obyekt nofaol: grid qurilmaydi, agentlar uni ishlatmaydi");
        UI::PropertyHeader("Maydon", "Obyekt pozitsiyasi + siljish atrofidagi quti: har katak markazidan pastga nur tashlanadi");
        changed |= UI::PropertyFloat3("Siljish", c.Offset, 0.05f);
        UI::PropertyLabel("O'lcham", "X / Z - grid maydoni, Y - nurlar qutining tepasidan shuncha pastga tushadi");
        if (ImGui::DragFloat3("##size", &c.Size.x, 0.1f, 0.0f, 10000.0f, "%.2f"))
        {
            c.Size = glm::max(c.Size, glm::vec3(0.0f));
            changed = true;
        }
        UI::PropertyLabel("");
        ImGui::BeginDisabled(!m_Context || !m_SceneEditable);
        if (ImGui::Button(Tr(ICON_FA_EXPAND "  Statik collider'larga moslash")) && m_Context)
        {
            // The box around the static 3D colliders' bounds.
            AABB bounds;
            bool any = false;
            for (auto e : m_Context->GetAllEntitiesWith<Rigidbody3DComponent>())
            {
                Entity body{ e, m_Context.get() };
                AABB b;
                if (body.GetComponent<Rigidbody3DComponent>().Type == Rigidbody3DComponent::BodyType::Static && m_Context->GetEntityBounds(body, b))
                {
                    bounds = any ? bounds.Merged(b) : b;
                    any = true;
                }
            }
            if (any)
            {
                const glm::vec3 position = m_Context->GetWorldPosition(entity);
                c.Size = glm::max(bounds.Max - bounds.Min + glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(c.CellSize));
                c.Offset = (bounds.Min + bounds.Max) * 0.5f + glm::vec3(0.0f, 2.0f, 0.0f) - position;
                changed = true;
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Maydonni sahnadagi statik 3D jismlar (Rigidbody 3D Static) chegarasiga moslaydi (tepada 4 birlik zaxira bilan)");
        UI::PropertyLabel("Katak o'lchami", "Bir katak tomoni (dunyo birligi)");
        changed |= ImGui::DragFloat("##cell", &c.CellSize, 0.01f, NavGrid3DComponent::MinCellSize, NavGrid3DComponent::MaxCellSize, "%.2f",
                                    ImGuiSliderFlags_AlwaysClamp);

        UI::PropertyHeader("Agent");
        UI::PropertyLabel("Radius", "To'siq yoki jarlik chetiga shundan yaqin kataklar yopiladi");
        changed |= ImGui::DragFloat("##radius", &c.AgentRadius, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Bo'yi", "Pol ustida shuncha balandlikkacha bo'sh joy kerak (past shift, to'siqlar)");
        changed |= ImGui::DragFloat("##height", &c.AgentHeight, 0.01f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Maks. qiyalik", "Daraja: bundan tik sirt yurilmaydi");
        changed |= ImGui::SliderFloat("##slope", &c.MaxSlope, 0.0f, 89.0f, "%.0f°", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Zina balandligi", "Qo'shni kataklar pollari orasidagi eng katta farq (zinapoya). Undan baland - o'tib bo'lmaydi");
        changed |= ImGui::DragFloat("##step", &c.StepHeight, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        changed |= DrawLayerMask("Qatlamlar", c.LayerMask, "Faqat shu to'qnashuv qatlamlaridagi statik collider'lar pol / to'siq bo'ladi");

        UI::PropertyHeader("Yo'llar");
        changed |= DrawNavPathSettings(c.Paths);

        const std::string extra = std::format(", {} ta statik jism", c.BakeBodies);
        if (DrawNavGridStatus(m_Context.get(), entity, c.Grid, c.Baked, c.BakeCount, c.BakeMilliseconds, extra.c_str(), c.FromBakeFile))
            ZM_INFO("NavGrid 3D '{}' qurildi: {}x{} katak, {} tasi yuriladigan, {} ta statik jism ({:.1f} ms)", entity.GetName(), c.Grid.GetWidth(),
                    c.Grid.GetHeight(), c.Grid.CountWalkable(), c.BakeBodies, c.BakeMilliseconds);
        return changed;
    }

    bool SceneHierarchyPanel::DrawNavAgent(Entity entity, NavAgentComponent& c)
    {
        bool changed = UI::PropertyBool("Dunyo navigatsiyasi", c.WorldNavigation);
        if (changed) c.ResetRuntime();
        if (c.WorldNavigation)
        {
            UI::PropertyLabel("Qidiruv kengayish limiti");
            changed |= ImGui::DragScalar("##worldExpanded", ImGuiDataType_U32, &c.WorldMaxExpanded, 32.f);
            c.WorldMaxExpanded = std::clamp(c.WorldMaxExpanded, 1u, 1048576u);
            if (m_Context && m_Context->IsRunning())
            {
                ImGui::Text("World atlas %llu | requests %u | traversals %u", (unsigned long long)c.AtlasRevision, c.PathRequests, c.TraversalCompletions);
                if (m_Context->GetNavTraversalLink(entity))
                {
                    ImGui::Text("Pending link %llu / ticket %llu", (unsigned long long)c.PendingTraversalLink, (unsigned long long)c.TraversalTicket);
                    const auto resultID = ImGui::GetID(("worldTraversalResult" + std::to_string(uint64_t(entity.GetUUID()))).c_str());
                    if (ImGui::Button("Complete traversal")) ImGui::GetStateStorage()->SetInt(resultID, m_Context->CompleteNavTraversal(entity, c.PendingTraversalLink, c.AtlasRevision, c.TraversalTicket) ? 0 : 1);
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel traversal"))
                    {
                        m_Context->CancelNavTraversal(entity);
                        ImGui::GetStateStorage()->SetInt(resultID, 0);
                    }
                    if (ImGui::GetStateStorage()->GetInt(resultID)) ImGui::TextWrapped("Portal chiqishi band yoki sorov bajarishga tayyor emas.");
                }
            }
            ImGui::TextWrapped("Cell-centre route; explicit portal completion. Character exits are checked for overlap. Smoothing and local avoidance use the legacy grid mode.");
        }
        changed |= DrawEntityRefField("Grid", c.Grid, "NavGrid 2D / 3D komponentli obyekt (tanlanmasa - agent turgan joyni o'z ichiga "
                                                      "olgan grid)");
        changed |= DrawEntityRefField("Nishon", c.Target, "Tanlansa - agent shu obyekt ortidan yuradi (yo'lni har yangilash oralig'ida "
                                                         "qayta topadi)");
        InactiveComponentNote(entity, "Obyekt nofaol: harakatlanmaydi");
        if (!c.WorldNavigation && m_Context && !m_Context->GetNavAgentGrid(entity) && m_Context->IsRunning())
        {
            UI::PropertyLabel("");
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s",
                               Tr("Qurilgan grid yo'q: sahnaga NavGrid 2D / 3D qo'shing"));
        }
        UI::PropertyHeader("Harakat");
        UI::PropertyLabel("Tezlik", "Dunyo birligi / soniya");
        changed |= ImGui::DragFloat("##speed", &c.Speed, 0.05f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Tezlanish", "Birlik / s^2: tezlikni oshirish va tormozlash (0 - darhol)");
        changed |= ImGui::DragFloat("##accel", &c.Acceleration, 0.1f, 0.0f, 10000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Burish tezligi", "Daraja / soniya: harakat yo'nalishiga burilish (0 - burilmaydi)");
        changed |= ImGui::DragFloat("##angular", &c.AngularSpeed, 1.0f, 0.0f, 10000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        changed |= UI::PropertyBool("Qarab yurish", c.UpdateRotation);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Yurgan tomoniga qaraydi: 3D - obyektning -Z o'qi oldinga (Y atrofida), 2D - +X o'qi (Z atrofida). Dinamik jismlarni "
                        "fizika buradi");
        UI::PropertyLabel("To'xtash", "To'xtash masofasi: yo'l oxiriga shuncha qolganda to'xtaydi");
        changed |= ImGui::DragFloat("##stop", &c.StoppingDistance, 0.01f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        changed |= UI::PropertyBool("Avto tormoz", c.AutoBraking);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Oxiriga yaqinlashganda sekinlashib aynan to'xtaydi (o'chiq - oxirigacha to'liq tezlikda)");
        UI::PropertyLabel("Radius", "Burchakka shuncha yaqinlashsa - o'tdi deb hisoblanadi");
        changed |= ImGui::DragFloat("##radius", &c.Radius, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Oldinga qarash", "Yo'l bo'ylab shuncha oldindagi nuqtaga qarab yuradi (burchaklar silliqroq)");
        changed |= ImGui::DragFloat("##lookahead", &c.LookAhead, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Pol ustida", "3D, transform bilan yurganda: obyekt boshi poldan shuncha baland");
        changed |= ImGui::DragFloat("##base", &c.BaseOffset, 0.01f, -1000.0f, 1000.0f, "%.2f");

        UI::PropertyHeader("Yo'l");
        UI::PropertyLabel("Yangilash (s)", "Soniya: harakat paytida yo'l shu oraliqda qayta topiladi (0 - faqat SetDestination / grid "
                                                "o'zgarganda)");
        changed |= ImGui::DragFloat("##repath", &c.RepathInterval, 0.01f, 0.0f, 1000.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
        changed |= UI::PropertyBool("Silliqlash", c.SmoothPath);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("String pulling: to'g'ri ko'rinadigan kataklar tashlab ketiladi - yo'l faqat burilish kerak bo'lgan burchaklardan "
                        "o'tadi (o'chiq - katak markazlari bo'ylab zig-zag)");
        changed |= UI::PropertyBool("Spline", c.SplinePath);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Burchaklar Catmull-Rom egri chizig'i bilan yumaloqlanadi (to'siqqa kirib qoladigan joyda to'g'ri chiziq qoladi)");

        UI::PropertyHeader("To'siqni aylanib o'tish", "Oldinga uchta fizika nuri (mo'ylov): biri yurish yo'nalishida, ikkitasi yonlarga. "
                                                     "Tekkan narsadan (boshqa agentlar, qutilar) chetga buriladi. Gizmo'lar > Navigatsiya - nurlar");
        changed |= UI::PropertyBool("Yoqilgan", c.Avoidance);
        if (c.Avoidance)
        {
            UI::PropertyLabel("Nur uzunligi", "Oldingi nur uzunligi (yon nurlar - 3/4 qismi), dunyo birligida");
            changed |= ImGui::DragFloat("##feelerLength", &c.FeelerLength, 0.02f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            UI::PropertyLabel("Nur burchagi", "Yon nurlar oldinga nisbatan shuncha darajaga buriladi");
            changed |= ImGui::SliderFloat("##feelerAngle", &c.FeelerAngle, 0.0f, 89.0f, "%.0f°", ImGuiSliderFlags_AlwaysClamp);
            UI::PropertyLabel("Nur balandligi", "3D: nurlar obyekt boshidan shuncha balanddan chiqadi");
            changed |= ImGui::DragFloat("##feelerHeight", &c.FeelerHeight, 0.01f, -100.0f, 100.0f, "%.2f");
            UI::PropertyLabel("Kuchi", "Yo'l bilan aralashish og'irligi: 1 - to'liq tezligigacha chetga buradi, 0 - o'chiq");
            changed |= ImGui::DragFloat("##avoidWeight", &c.AvoidanceWeight, 0.01f, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            changed |= DrawLayerMask("Qatlamlar", c.AvoidanceMask, "Nurlar faqat shu to'qnashuv qatlamlaridagi collider'larni ko'radi");
            changed |= UI::PropertyBool("Statik jismlar ham", c.AvoidStatic);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Statik collider'lar va tilemap'lardan ham qochadi (odatda shart emas: grid ularni allaqachon chetlab o'tadi)");
        }

        if (m_Context && m_Context->IsRunning())
        {
            UI::PropertyHeader("Jonli");
            UI::PropertyLabel("Holat");
            const char* state = c.Arrived ? "yetib keldi" : c.IsMoving() ? "yuryapti" : c.HasDestination ? "kutyapti" : "manzil yo'q";
            const bool targetPaused = c.Target && !c.IsFollowingTarget();
            ImGui::TextDisabled("%s  |  %s%s%s", Tr(state), NavPathStatusName(c.PathStatus), targetPaused ? "  |  " : "",
                                targetPaused ? Tr("nishon to'xtatilgan") : "");
            UI::PropertyLabel("Qolgan masofa");
            ImGui::TextDisabled("%.2f birlik, %zu burchak, tezlik %.2f", c.Remaining, c.Path.size(), glm::length(c.Velocity));
            UI::PropertyLabel("Manzil");
            ImGui::TextDisabled("%.2f, %.2f, %.2f  (%u marta yo'l topildi)", c.Destination.x, c.Destination.y, c.Destination.z, c.PathRequests);
            if (c.BlockedRepaths > 0)
            {
                UI::PropertyLabel("");
                ImGui::TextDisabled("%s: %u", Tr("to'siq tufayli qayta"), c.BlockedRepaths);
            }
            if (c.Avoidance)
            {
                UI::PropertyLabel("Nurlar");
                const char* names[] = { "oldinda", "chapda", "o'ngda" };
                std::string hits;
                for (int i = 0; i < 3; i++)
                    if (c.FeelerHits & (1u << i))
                        hits += std::string(hits.empty() ? "" : ", ") + Tr(names[i]);
                ImGui::TextDisabled("%s", !c.FeelersActive ? Tr("ishlamayapti") : hits.empty() ? Tr("bo'sh") : hits.c_str());
            }
            UI::PropertyLabel("");
            if (ImGui::Button(Tr(ICON_FA_STOP "  To'xtatish")))
                c.Stop();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && c.Target)
                UI::Tooltip("Manzil o'chadi va agent nishon ortidan yurmaydi (\"Nishon ortidan\" - yana yuradi)");
            if (targetPaused)
            {
                ImGui::SameLine();
                if (ImGui::Button(Tr(ICON_FA_PLAY "  Nishon ortidan")))
                    c.ResumeTarget();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("To'xtatish / SetDestination'dan keyin agent yana nishon ortidan yuradi (skript: SetNavTarget)");
            }
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawNavObstacle(Entity entity, NavObstacleComponent& c)
    {
        bool changed = false;
        InactiveComponentNote(entity, "Obyekt nofaol: kataklarni yopmaydi");
        UI::PropertyHeader("Shakl", "O'yin paytida grid'ning shu shakl ostidagi (va grid agent radiusicha atrofidagi) kataklari yopiladi");
        int shape = (int)c.Shape;
        static const char* const s_Shapes[] = { "Quti", "Doira" };
        static const char* const s_ShapeTips[] = { "To'g'ri burchakli quti (obyekt bilan birga buriladi)", "2D - doira, 3D - tik silindr" };
        if (UI::PropertySegmented("Tur", shape, s_Shapes, 2, s_ShapeTips))
        {
            c.Shape = (NavObstacleComponent::ShapeType)shape;
            changed = true;
        }
        changed |= UI::PropertyFloat3("Markaz", c.Center, 0.05f);
        if (c.Shape == NavObstacleComponent::ShapeType::Box)
        {
            UI::PropertyLabel("O'lcham", "Qutining to'liq o'lchami obyekt o'qlarida (masshtab bilan): 2D - X / Y, 3D - X / Y / Z");
            if (ImGui::DragFloat3("##size", &c.Size.x, 0.02f, 0.0f, 10000.0f, "%.2f"))
            {
                c.Size = glm::max(c.Size, glm::vec3(0.0f));
                changed = true;
            }
        }
        else
        {
            UI::PropertyLabel("Radius", "Doira / silindr radiusi (masshtab bilan)");
            changed |= ImGui::DragFloat("##radius", &c.Radius, 0.01f, 0.0f, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            UI::PropertyLabel("Bo'yi", "3D: silindr balandligi (markaz atrofida)");
            changed |= ImGui::DragFloat("##height", &c.Size.y, 0.02f, 0.0f, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        changed |= DrawEntityRefField("Grid", c.Grid, "Faqat shu NavGrid 2D / 3D (tanlanmasa - ustidan o'tgan hamma qurilgan grid'lar)");

        UI::PropertyHeader("Qayta yopish");
        UI::PropertyLabel("Siljish chegarasi", "Shundan ko'proq siljisa / burilsa kataklar qayta yopiladi (faqat o'zgarganlari)");
        changed |= ImGui::DragFloat("##threshold", &c.MoveThreshold, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        changed |= UI::PropertyBool("Faqat turganda", c.CarveOnlyStationary);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Harakatlanayotganda kataklarni yopmaydi (agentlar uni nurlar bilan aylanib o'tadi), to'xtab turganda yopadi");
        if (c.CarveOnlyStationary)
        {
            UI::PropertyLabel("Turish vaqti", "Soniya: shuncha qimirlamay tursa yopadi");
            changed |= ImGui::DragFloat("##still", &c.StationaryTime, 0.01f, 0.0f, 100.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
        }

        if (m_Context && m_Context->IsRunning())
        {
            UI::PropertyHeader("Jonli");
            UI::PropertyLabel("Holat");
            if (c.IsCarving())
            {
                ImGui::TextDisabled("%zu %s, %zu grid", c.GetCarvedCellCount(), Tr("katak yopiq"), c.Carvings.size());
                UI::PropertyLabel("");
                ImGui::TextDisabled("%u %s", c.CarveCount, Tr("marta yangilandi"));
            }
            else
                ImGui::TextDisabled("%s", c.CarveOnlyStationary && c.StillTime < c.StationaryTime ? Tr("harakatda (yopmaydi)") : Tr("hech narsa yopmaydi"));
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawSightSensor(Entity entity, SightSensorComponent& c)
    {
        bool changed = false;
        InactiveComponentNote(entity, "Obyekt nofaol: ko'rmaydi");
        UI::PropertyHeader("Ko'rish maydoni", "Oldinga (3D: -Z, 2D: +X) qaragan konus: masofa va burchak ichida, orada to'siq bo'lmasa - ko'radi. "
                                              "Gizmo'lar > Navigatsiya - konus");
        UI::PropertyLabel("Masofa", "Dunyo birligida: bundan uzoqni ko'rmaydi");
        changed |= ImGui::DragFloat("##distance", &c.ViewDistance, 0.05f, 0.0f, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Burchak", "Konusning to'liq burchagi (daraja): 360 - hamma tomonni");
        changed |= ImGui::SliderFloat("##fov", &c.FieldOfView, 0.0f, 360.0f, "%.0f°", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Yaqin masofa", "Shundan yaqindagini qaysi tomonda bo'lsa ham sezadi (0 - faqat konus)");
        changed |= ImGui::DragFloat("##near", &c.NearDistance, 0.02f, 0.0f, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Ko'z balandligi", "3D: nurlar obyekt boshidan shuncha balanddan chiqadi");
        changed |= ImGui::DragFloat("##eye", &c.EyeHeight, 0.01f, -1000.0f, 1000.0f, "%.2f");

        UI::PropertyHeader("Nishonlar");
        changed |= DrawEntityRefField("Nishon", c.Target, "Faqat shu obyektni qidiradi (tanlanmasa - quyidagi teg'li hamma obyektlar)");
        if (!c.Target)
            changed |= UI::PropertyString("Teg", c.TargetTag);
        changed |= DrawLayerMask("To'suvchi qatlamlar", c.BlockingMask, "Shu to'qnashuv qatlamlaridagi collider'lar ko'rishni to'sadi");
        UI::PropertyLabel("Tekshirish (s)", "Soniya: shu oraliqda qaraydi (0 - har kadr)");
        changed |= ImGui::DragFloat("##interval", &c.CheckInterval, 0.01f, 0.0f, 60.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Xotira (s)", "Ko'rmay qolgach oxirgi ko'rgan joyini shuncha soniya eslab turadi (RemembersTarget)");
        changed |= ImGui::DragFloat("##memory", &c.MemoryTime, 0.05f, 0.0f, 10000.0f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);

        if (m_Context && m_Context->IsRunning())
        {
            UI::PropertyHeader("Jonli");
            UI::PropertyLabel("Holat");
            if (c.CanSee)
            {
                Entity seen = m_Context->GetEntityByUUID(c.SeenTarget);
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), ICON_FA_EYE "  %s: %s", Tr("ko'ryapti"), seen ? seen.GetName().c_str() : "?");
            }
            else if (c.HasSeen)
                ImGui::TextDisabled("%s (%.1f s %s)", Tr("ko'rmayapti"), c.TimeSinceSeen, Tr("oldin ko'rgan"));
            else
                ImGui::TextDisabled("%s", Tr("hech kimni ko'rmagan"));
            if (c.HasSeen)
            {
                UI::PropertyLabel("Oxirgi joy");
                ImGui::TextDisabled("%.2f, %.2f, %.2f", c.LastSeenPosition.x, c.LastSeenPosition.y, c.LastSeenPosition.z);
            }
            UI::PropertyLabel("Tekshiruvlar");
            ImGui::TextDisabled("%u  (%d %s)", c.Checks, c.VisibleCount, Tr("ta ko'rinadi"));
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawPatrolPath(Entity entity, PatrolPathComponent& c)
    {
        bool changed = false;
        InactiveComponentNote(entity, "Obyekt nofaol: patrul qilmaydi");
        const bool hasAgent = entity.HasComponent<NavAgentComponent>();

        UI::PropertyHeader("Nuqtalar", "Patrul nuqtalari qayerdan olinadi. Gizmo'lar > Navigatsiya - yo'l, nuqtalar va hozirgi nishon");
        int source = (int)c.Source;
        static const char* const s_Sources[] = { "Ro'yxat", "Bolalar", "Spline" };
        static const char* const s_SourceTips[] = { "Quyidagi ro'yxatdagi nuqtalar (dunyo koordinatalari yoki boshlang'ich joydan siljish)",
                                                    "Yo'l obyektining faol bolalari - ierarxiya tartibida (viewportda gizmo bilan suriladi)",
                                                    "Yo'l obyektining Spline nuqtalari (viewportda Spline tahrirlagichi bilan)" };
        if (UI::PropertySegmented("Manba", source, s_Sources, 3, s_SourceTips))
        {
            c.Source = (PatrolPathComponent::PointSource)source;
            changed = true;
        }
        if (c.Source == PatrolPathComponent::PointSource::Points)
        {
            changed |= UI::PropertyBool("Boshlang'ich joydan", c.RelativeToStart);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Nuqtalar - o'yin boshlanganda obyekt turgan joydan siljish (prefab nusxalari o'z joyida patrul qiladi); "
                            "o'chiq - dunyo koordinatalari");
            int remove = -1;
            const float button = ImGui::GetFrameHeight();
            const float waitWidth = 58.0f;
            for (size_t i = 0; i < c.Points.size(); i++)
            {
                ImGui::PushID((int)i);
                PatrolPathComponent::Waypoint& w = c.Points[i];
                UI::PropertyLabel(std::format("  {}", i).c_str());
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                // A wide inspector: position, wait and remove on one row; a narrow one: the wait below.
                const bool oneRow = ImGui::GetContentRegionAvail().x >= 300.0f;
                ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - spacing - (oneRow ? waitWidth + spacing : 0.0f)));
                changed |= ImGui::DragFloat3("##p", &w.Position.x, 0.02f, 0.0f, 0.0f, "%.2f");
                if (oneRow)
                    ImGui::SameLine();
                else
                {
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_XMARK, ImVec2(button, 0)))
                        remove = (int)i;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        UI::Tooltip("Nuqtani o'chirish");
                    UI::PropertyLabel("      kutish");
                }
                ImGui::SetNextItemWidth(oneRow ? waitWidth : -FLT_MIN);
                if (ImGui::DragFloat("##wait", &w.Wait, 0.02f, -1.0f, 1000.0f, w.Wait < 0.0f ? "umumiy" : "%.1f s", ImGuiSliderFlags_AlwaysClamp))
                {
                    if (w.Wait < 0.0f)
                        w.Wait = -1.0f;
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Shu nuqtada kutish (soniya). Chapga oxirigacha sudralsa - umumiy \"Kutish\" qiymati");
                if (oneRow)
                {
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_XMARK, ImVec2(button, 0)))
                        remove = (int)i;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        UI::Tooltip("Nuqtani o'chirish");
                }
                ImGui::PopID();
            }
            if (remove >= 0)
            {
                c.Points.erase(c.Points.begin() + remove);
                changed = true;
            }
            UI::PropertyLabel("");
            ImGui::BeginDisabled(!m_SceneEditable || c.Points.size() >= PatrolPathComponent::MaxWaypoints);
            if (ImGui::Button(Tr(ICON_FA_PLUS "  Nuqta qo'shish")))
            {
                // Past the last point, the way the round goes; the first one where the entity is.
                PatrolPathComponent::Waypoint w;
                const size_t n = c.Points.size();
                if (n >= 2)
                {
                    glm::vec3 step = c.Points[n - 1].Position - c.Points[n - 2].Position;
                    if (glm::length(step) < 1e-3f)
                        step = glm::vec3(1.0f, 0.0f, 0.0f);
                    w.Position = c.Points[n - 1].Position + step;
                }
                else if (n == 1)
                    w.Position = c.Points[0].Position + glm::vec3(2.0f, 0.0f, 0.0f);
                else if (!c.RelativeToStart && m_Context)
                    w.Position = m_Context->GetWorldPosition(entity);
                c.Points.push_back(w);
                changed = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(c.RelativeToStart || !m_Context);
            if (ImGui::Button(Tr(ICON_FA_LOCATION_CROSSHAIRS "  Obyekt joyi")))
            {
                PatrolPathComponent::Waypoint w;
                w.Position = m_Context->GetWorldPosition(entity);
                c.Points.push_back(w);
                changed = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Obyekt hozir turgan joyni nuqta qilib qo'shish (uni viewportda surib, yana bosing)");
            ImGui::EndDisabled();
        }
        else
        {
            const bool spline = c.Source == PatrolPathComponent::PointSource::Spline;
            changed |= DrawEntityRefField("Yo'l", c.Route, spline ? "Spline komponentli obyekt (tanlanmasa - Spline komponenti bor eng yaqin ota)"
                                                                 : "Shu obyektning faol bolalari - patrul nuqtalari (ierarxiya tartibida)");
            if (spline)
            {
                UI::PropertyLabel("Oraliq", "0 - spline'ning o'z nuqtalari; > 0 - egri chiziq bo'ylab shuncha masofada (dunyo birligida) "
                                            "nuqtalar (egrilik aniqroq kuzatiladi; kutishni 0 qiling)");
                changed |= ImGui::DragFloat("##spacing", &c.SplineSpacing, 0.02f, 0.0f, 1000.0f, c.SplineSpacing > 0.0f ? "%.2f" : "o'z nuqtalari",
                                            ImGuiSliderFlags_AlwaysClamp);
            }
        }
        std::vector<glm::vec3> points;
        if (m_Context)
            m_Context->GetPatrolWaypoints(entity, points);
        UI::PropertyLabel("");
        if (points.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", Tr("Patrul nuqtalari yo'q"));
        else
        {
            float length = 0.0f;
            for (size_t i = 1; i < points.size(); i++)
                length += glm::length(points[i] - points[i - 1]);
            ImGui::TextDisabled("%zu %s, %.1f %s", points.size(), Tr("ta nuqta"), length, Tr("birlik"));
        }

        UI::PropertyHeader("Patrul");
        static const char* const s_Modes[] = { "Aylanma (oxiridan boshiga)", "Borib-qaytish", "Tasodifiy nuqta", "Bir marta (oxirida to'xtaydi)" };
        int mode = (int)c.Mode;
        if (UI::PropertyCombo("Rejim", mode, s_Modes, 4))
        {
            c.Mode = (PatrolPathComponent::PatrolMode)mode;
            changed = true;
        }
        UI::PropertyLabel("Kutish", "Har nuqtada shuncha soniya turadi (nuqtaning o'z qiymati bo'lsa - o'shancha)");
        changed |= ImGui::DragFloat("##waitTime", &c.WaitTime, 0.02f, 0.0f, 1000.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Tasodifiy +/-", "Kutish vaqti har safar shunchagacha ko'p yoki kam bo'ladi (soniya)");
        changed |= ImGui::DragFloat("##variance", &c.WaitVariance, 0.02f, 0.0f, 1000.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Yetish masofasi", "Nuqtaga shuncha yaqinlashsa - yetdi (Nav Agent bilan: uning to'xtash masofasi ham qo'shiladi)");
        changed |= ImGui::DragFloat("##arrive", &c.ArriveDistance, 0.01f, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        changed |= UI::PropertyBool("Eng yaqindan", c.StartAtNearest);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Eng yaqin nuqtadan boshlaydi (o'chiq - birinchisidan)");
        changed |= UI::PropertyBool("Boshida yurish", c.PlayOnStart);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("O'yin boshlanganda patrulni boshlaydi. O'chiq bo'lsa skriptdan: ResumePatrol()");
        if (hasAgent)
        {
            UI::PropertyLabel("");
            ImGui::TextDisabled(ICON_FA_PERSON_WALKING "  %s", Tr("Nav Agent bilan yuradi (tezligi - agentniki)"));
        }
        else
        {
            UI::PropertyLabel("Tezlik", "Nav Agent yo'q: nuqtadan nuqtaga to'g'ri chiziq bo'ylab, birlik / soniya");
            changed |= ImGui::DragFloat("##speed", &c.Speed, 0.05f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            changed |= UI::PropertyBool("Qarab yurish", c.FaceMovement);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Yurgan tomoniga buriladi: 3D - -Z o'qi oldinga, 2D - +X o'qi");
        }

        if (m_Context && m_Context->IsRunning())
        {
            UI::PropertyHeader("Jonli");
            UI::PropertyLabel("Holat");
            if (c.Finished)
                ImGui::TextDisabled("%s", Tr("tugadi (oxirgi nuqtada)"));
            else if (!c.Patrolling)
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), ICON_FA_PAUSE "  %s", Tr("to'xtatilgan (skript boshqaryapti)"));
            else if (c.Waiting)
                ImGui::TextDisabled("%d-%s, %s %.1f s", c.Current, Tr("nuqtada"), Tr("kutyapti"), std::max(0.0f, c.WaitTimer));
            else
                ImGui::TextDisabled("%d-%s", c.Current, Tr("nuqtaga yuryapti"));
            UI::PropertyLabel("Hisob");
            ImGui::TextDisabled("%u %s, %u %s", c.Arrivals, Tr("yetdi"), c.Laps, Tr("aylana"));
            if (c.Skipped > 0)
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "%u %s", c.Skipped, Tr("nuqtaga yo'l topilmadi (o'tkazib yuborildi)"));
            }
            UI::PropertyLabel("");
            if (ImGui::Button(c.IsPatrolling() ? Tr(ICON_FA_PAUSE "  To'xtatish") : Tr(ICON_FA_PLAY "  Davom etish")))
                c.IsPatrolling() ? (void)m_Context->PausePatrol(entity) : (void)m_Context->ResumePatrol(entity, true);
            ImGui::SameLine();
            ImGui::BeginDisabled(points.empty());
            if (ImGui::Button(Tr(ICON_FA_FORWARD_STEP "  Keyingi nuqta")))
                m_Context->SetPatrolWaypoint(entity, points.empty() ? 0 : (std::max(0, c.Current) + 1) % (int)points.size());
            ImGui::EndDisabled();
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawFormation(Entity entity, FormationComponent& c)
    {
        bool changed = false;
        InactiveComponentNote(entity, "Obyekt nofaol: guruh yurmaydi");

        UI::PropertyHeader("Shakl", "A'zolar boshliq (shu obyekt) atrofida qanday turadi. Gizmo'lar > Navigatsiya - joylar, yo'nalish va kim qayerga");
        static const char* const s_Shapes[] = { "Qator", "Kolonna", "Pona", "Doira" };
        static const char* const s_ShapeTips[] = { "Yonma-yon, boshliq bilan bir qatorda (o'ngida, chapida, navbat bilan)",
                                                   "Boshliq ortidan bir qator bo'lib",
                                                   "V shaklida, boshliq uchida (ikki qanot orqaga)",
                                                   "Boshliq atrofida halqa (boshliq bilan burilmaydi; a'zolar tashqariga qaraydi)" };
        int shape = (int)c.Shape;
        if (UI::PropertySegmented("Tur", shape, s_Shapes, 4, s_ShapeTips))
        {
            c.Shape = (FormationShape)shape;
            changed = true;
        }
        UI::PropertyLabel("Oraliq", "Qo'shni a'zolar orasidagi masofa, birlik (doira: halqa bo'ylab; boshliqdan kamida shuncha)");
        changed |= ImGui::DragFloat("##spacing", &c.Spacing, 0.02f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (c.Shape == FormationShape::Wedge)
        {
            UI::PropertyLabel("Pona burchagi", "V qanchalik ochiq: ikki qanot orasidagi burchak (gradus)");
            changed |= ImGui::DragFloat("##wedge", &c.WedgeAngle, 0.5f, 10.0f, 170.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
        }

        UI::PropertyHeader("A'zolar", "Boshliq ortidan yuradiganlar: ro'yxat va (ixtiyoriy) teg bo'yicha. Boshliqning o'zi a'zo bo'lmaydi");
        int remove = -1;
        for (size_t i = 0; i < c.Members.size(); i++)
        {
            ImGui::PushID((int)i);
            const std::string label = std::format("  {}", i + 1);
            changed |= DrawEntityRefField(label.c_str(), c.Members[i], "Guruh a'zosi (ierarxiyadan tortib tashlang)");
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PopID();
            ImGui::PushID((int)(i + 100000));
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(ImGui::GetFrameHeight(), 0)))
                remove = (int)i;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("A'zoni ro'yxatdan olib tashlash");
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            c.Members.erase(c.Members.begin() + remove);
            changed = true;
        }
        UI::PropertyLabel("");
        ImGui::BeginDisabled(!m_SceneEditable || c.Members.size() >= FormationComponent::MaxMembers);
        if (ImGui::Button(Tr(ICON_FA_PLUS "  A'zo qo'shish")))
        {
            c.Members.push_back({});
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Bo'sh qator qo'shadi: unga ierarxiyadan obyektni tortib tashlang");
        ImGui::SameLine();
        // The other entities selected in the hierarchy (Ctrl+click) join at once.
        std::vector<Entity> picked;
        for (Entity e : m_Selection.GetEntities())
            if (e && e != entity && e.IsValid())
                picked.push_back(e);
        ImGui::BeginDisabled(picked.empty() || !m_Context);
        if (ImGui::Button(Tr(ICON_FA_USERS "  Tanlanganlar")))
            for (Entity e : picked)
                changed |= m_Context->AddFormationMember(entity, e);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Ierarxiyada (Ctrl bilan) birga tanlangan obyektlarni a'zo qilish");
        ImGui::EndDisabled();
        UI::PropertyLabel("Teg", "Bo'sh bo'lmasa - shu tegli har bir faol obyekt ham a'zo (masalan \"Askar\")");
        changed |= ImGui::InputTextWithHint("##tag", Tr("(yo'q)"), &c.MemberTag);
        const std::vector<Entity> members = m_Context ? m_Context->GetFormationMembers(entity) : std::vector<Entity>();
        UI::PropertyLabel("");
        if (members.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", Tr("A'zolar yo'q"));
        else
        {
            size_t agents = 0;
            for (Entity m : members)
                agents += m.HasComponent<NavAgentComponent>() ? 1 : 0;
            // Wrapped: the value column is narrow at the default inspector width.
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%zu %s, %zu %s", members.size(), Tr("ta a'zo"), agents, Tr("tasi Nav Agent bilan"));
            ImGui::PopStyleColor();
        }

        UI::PropertyHeader("Harakat");
        changed |= UI::PropertyBool("Eng yaqin joyga", c.AssignNearest);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Har a'zo jami eng kam yuriladigan joyni oladi (shakl, a'zolar yoki yo'nalish ko'p o'zgarsa - qaytadan). O'chiq - ro'yxat tartibida");
        changed |= UI::PropertyBool("Yo'nalishga qarash", c.FaceHeading);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Joyida turgan a'zo guruh qaragan tomonga buriladi (doira: tashqariga)");
        changed |= UI::PropertyBool("Grid ichida", c.KeepOnGrid);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Nav grid'ning to'siq katagidagi joy eng yaqin yuriladigan katakka, devor ortidagisi boshliq tomonga suriladi");
        UI::PropertyLabel("Yuborish oralig'i", "Nav Agent a'zolar joyi siljiganda shuncha soniyada bir yangi manzil oladi");
        changed |= ImGui::DragFloat("##interval", &c.UpdateInterval, 0.01f, 0.0f, 10.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Joy chegarasi", "A'zo joyiga shuncha yaqin bo'lsa - joyida (Nav Agent: to'xtash masofasi ham qo'shiladi)");
        changed |= ImGui::DragFloat("##tolerance", &c.SlotTolerance, 0.01f, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Tezlik", "Nav Agent'siz a'zolar: joyiga to'g'ri chiziq bo'ylab, birlik / soniya (Nav Agent'li - o'z tezligi bilan; "
                                    "boshliqdan bir oz tezroq bo'lsin)");
        changed |= ImGui::DragFloat("##speed", &c.Speed, 0.05f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

        if (m_Context && m_Context->IsRunning())
        {
            UI::PropertyHeader("Jonli");
            UI::PropertyLabel("Holat");
            if (c.SlotMembers.empty())
                ImGui::TextDisabled("%s", Tr("a'zolar yo'q"));
            else if (c.IsInPlace())
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.5f, 1.0f), ICON_FA_CHECK "  %s (%d)", Tr("hammasi joyida"), c.InPlace);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), ICON_FA_PERSON_WALKING "  %d / %zu %s", c.InPlace, c.SlotMembers.size(), Tr("joyida"));
            UI::PropertyLabel("Boshliq");
            ImGui::TextDisabled("%s, %u %s", c.LeaderMoving ? Tr("yuryapti") : Tr("turibdi"), c.Assignments, Tr("marta taqsimlandi"));
            UI::PropertyLabel("");
            for (int i = 0; i < 4; i++)
            {
                if (i > 0)
                    ImGui::SameLine();
                const bool current = (int)c.Shape == i;
                if (current)
                    ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentColor());
                if (ImGui::SmallButton(Tr(s_Shapes[i])))
                    m_Context->SetFormationShape(entity, (FormationShape)i);
                if (current)
                    ImGui::PopStyleColor();
            }
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawLayerMask(const char* label, uint16_t& mask, const char* tooltip)
    {
        bool changed = false;
        const CollisionLayers2D& layers = Project::GetCollisionLayers2D();
        const std::vector<int> named = layers.GetNamedLayers();
        uint16_t namedBits = 0;
        for (int l : named)
            namedBits |= CollisionLayers2D::GetCategoryBits(l);
        const uint16_t unnamedBits = (uint16_t)~namedBits;
        // "Hammasi", "Hech biri", or the names (the unnamed ones as one).
        std::string preview;
        if (mask == CollisionLayers2D::AllLayers)
            preview = "Hammasi";
        else if (mask == 0)
            preview = "Hech biri";
        else
        {
            for (int l : named)
                if (mask & CollisionLayers2D::GetCategoryBits(l))
                    preview += (preview.empty() ? "" : ", ") + layers.GetName(l);
            if (mask & unnamedBits)
                preview += preview.empty() ? "nomsiz qatlamlar" : " + nomsiz";
        }
        UI::PropertyLabel(label, tooltip);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##layerMask", (ICON_FA_LAYER_GROUP "  " + preview).c_str()))
        {
            if (ImGui::SmallButton(Tr("Hammasi")))
            {
                changed = mask != CollisionLayers2D::AllLayers;
                mask = CollisionLayers2D::AllLayers;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Hech biri")))
            {
                changed = mask != 0;
                mask = 0;
            }
            ImGui::Separator();
            for (int l : named)
            {
                ImGui::PushID(l);
                bool on = (mask & CollisionLayers2D::GetCategoryBits(l)) != 0;
                if (ImGui::Checkbox(std::format("{}  {}", l, layers.GetName(l)).c_str(), &on))
                {
                    mask = on ? (uint16_t)(mask | CollisionLayers2D::GetCategoryBits(l)) : (uint16_t)(mask & ~CollisionLayers2D::GetCategoryBits(l));
                    changed = true;
                }
                ImGui::PopID();
            }
            if (unnamedBits)
            {
                bool on = (mask & unnamedBits) == unnamedBits;
                if (ImGui::Checkbox(Tr("Nomsiz qatlamlar"), &on))
                {
                    mask = on ? (uint16_t)(mask | unnamedBits) : (uint16_t)(mask & ~unnamedBits);
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Loyiha sozlamalarida nomi berilmagan qatlamlar (hammasi birga)");
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return changed;
    }

    void SceneHierarchyPanel::DrawEffectorNotes(Entity entity, bool fluid)
    {
        // The colliders that make the effector's area: box, circle and polygon ones (edges have no inside).
        int areaColliders = 0, triggers = 0;
        auto count = [&](const auto* c) {
            if (!c)
                return;
            areaColliders++;
            triggers += c->IsSensor ? 1 : 0;
        };
        count(entity.TryGetComponent<BoxCollider2DComponent>());
        count(entity.TryGetComponent<CircleCollider2DComponent>());
        count(entity.TryGetComponent<PolygonCollider2DComponent>());
        const char* warning = nullptr;
        const char* info = nullptr;
        if (!entity.HasComponent<Rigidbody2DComponent>())
            warning = "Rigidbody 2D (odatda statik) va kollider kerak: effektor shu kolliderlar ichidagi jismlarga ta'sir qiladi";
        else if (areaColliders == 0)
            warning = "Box / Circle / Polygon Collider 2D kerak: effektor maydoni shu kolliderlar";
        else if (triggers < areaColliders)
        {
            if (fluid)
                warning = "Suv kollideri trigger bo'lishi kerak ('Trigger (sensor)' ni yoqing): aks holda jismlar ichiga kira olmaydi";
            else
                info = "Kollider trigger emas: faqat unga tegib turgan jismlar itariladi";
        }
        if (!warning && !info)
            return;
        UI::PropertyLabel("");
        ImGui::PushTextWrapPos(0.0f);
        if (warning)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", warning);
        else
            ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  %s", info);
        ImGui::PopTextWrapPos();
    }

    bool SceneHierarchyPanel::DrawPhysicsMaterialField(Entity entity, std::string& path, const PhysicsMaterialData& own, bool& active, bool threeD)
    {
        bool changed = false, edit = false;
        Ref<PhysicsMaterialAsset> asset = path.empty() ? nullptr : PhysicsMaterialLibrary::Load(path);
        UI::AssetPathOptions options;
        options.ActionIcon = ICON_FA_PEN;
        options.ActionTooltip = "Fizik materialni tahrirlash (inspektorda ochiladi)";
        options.ActionClicked = &edit;
        options.Missing = !path.empty() && !asset;
        options.Tooltip = "Fizik material asset (.zphysmat): ishqalanish, sakrash, zichlik va ular boshqa kollider bilan qanday "
                          "qo'shilishi. Tanlansa quyidagi o'z qiymatlari o'rniga ishlatiladi; fayl o'zgarsa o'yin davomida ham yangilanadi.";
        if (UI::PropertyAssetPath("Fizik material", path, PhysicsMaterial::Extension, ICON_FA_GEM, options))
        {
            changed = true;
            asset = path.empty() ? nullptr : PhysicsMaterialLibrary::Load(path);
        }
        if (edit && asset && OnAssetEditRequested)
            OnAssetEditRequested(Project::GetAssetFileSystemPath(path));
        active = asset != nullptr;
        if (asset)
        {
            const PhysicsMaterialData& d = asset->GetData();
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            if (threeD)
            {
                // Jolt's own rule is the same as Box2D's (friction sqrt(a * b), bounce max(a, b)).
                auto combine = [](PhysicsCombine mode) { return mode == PhysicsCombine::Default ? "Standart" : PhysicsMaterial::GetCombineName(mode); };
                ImGui::TextDisabled(ICON_FA_GEM "  Ishqalanish %.2f (%s), sakrash %.2f (%s), sakrash chegarasi %.2f m/s. Zichlik 3D'da "
                                    "ishlatilmaydi: massa - Rigidbody 3D'da",
                                    d.Friction, combine(d.FrictionCombine), d.Restitution, combine(d.RestitutionCombine), d.RestitutionThreshold);
            }
            else
                ImGui::TextDisabled(ICON_FA_GEM "  Ishqalanish %.2f (%s), sakrash %.2f (%s), zichlik %.2f", d.Friction,
                                    PhysicsMaterial::GetCombineName(d.FrictionCombine), d.Restitution,
                                    PhysicsMaterial::GetCombineName(d.RestitutionCombine), d.Density);
            ImGui::PopTextWrapPos();
        }
        else if (path.empty() && Project::GetActive())
        {
            UI::PropertyLabel("");
            ImGui::BeginDisabled(!m_SceneEditable);
            bool create = ImGui::SmallButton(Tr(ICON_FA_PLUS "  Shu qiymatlardan material yaratish"));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Quyidagi qiymatlarni Assets/Physics/ ga .zphysmat qilib saqlaydi va shu kolliderga biriktiradi - keyin "
                            "uni boshqa kolliderlar ham ishlatishi mumkin.");
            if (create)
            {
                std::filesystem::path dir = Project::GetAssetDirectory() / "Physics";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                std::string name = entity.GetName().empty() ? std::string("PhysicsMaterial") : entity.GetName();
                for (char& ch : name)
                    if (std::string_view("/\\:*?\"<>|").find(ch) != std::string_view::npos)
                        ch = '_';
                std::filesystem::path file = AssetFiles::MakeUniquePath(dir, name + PhysicsMaterial::Extension);
                if (PhysicsMaterial::Save(file, own))
                {
                    if (AssetRegistry* registry = Project::GetAssetRegistry())
                        registry->OnFileAdded(file);
                    path = Project::GetAssetRelativePath(file);
                    changed = true;
                    if (OnFilesCreated)
                        OnFilesCreated({ file });
                    ZM_INFO("Fizik material yaratildi: {}", path);
                }
                else
                    ZM_ERROR("Fizik material saqlanmadi: {}", file.string());
            }
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawColliderPoints(std::vector<glm::vec2>& points, ColliderPointKind kind, int minPoints)
    {
        bool changed = false;
        const bool editing = m_ColliderPointEdit == kind;
        UI::PropertyLabel("Viewportda", "Nuqtalarni sahnada sudrab o'zgartirish. Chiziq ustiga bosish - o'sha joyga yangi nuqta, "
                                        "Ctrl + nuqtaga bosish - nuqtani o'chirish. Esc yoki tugma qayta bosilsa - tugaydi.");
        ImGui::BeginDisabled(!m_SceneEditable);
        if (editing)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentColor());
        if (ImGui::Button(editing ? ICON_FA_CHECK "  Tahrirlashni tugatish" : ICON_FA_DRAW_POLYGON "  Nuqtalarni tahrirlash", ImVec2(-FLT_MIN, 0)))
        {
            m_ColliderPointEdit = editing ? ColliderPointKind::None : kind;
            if (!editing)
            {
                m_TilePaint = false;
                m_JointAnchorEdit = m_Joint3DAnchorEdit = -1;
                m_SplinePointEdit = false;
            }
        }
        if (editing)
            ImGui::PopStyleColor();
        ImGui::EndDisabled();

        UI::PropertyHeader(std::format("Nuqtalar ({})", points.size()).c_str(), "Obyekt koordinatalarida (masshtabi bilan ko'paytiriladi)");
        int remove = -1;
        const float button = ImGui::GetFrameHeight();
        for (size_t i = 0; i < points.size(); i++)
        {
            ImGui::PushID((int)i);
            UI::PropertyLabel(std::format("  {}", i).c_str());
            ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
            changed |= ImGui::DragFloat2("##p", &points[i].x, 0.01f, 0.0f, 0.0f, "%.3f");
            ImGui::SameLine();
            ImGui::BeginDisabled((int)points.size() <= minPoints);
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(button, 0)))
                remove = (int)i;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip((int)points.size() <= minPoints ? "Kamida shuncha nuqta kerak" : "Nuqtani o'chirish");
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            points.erase(points.begin() + remove);
            changed = true;
        }
        UI::PropertyLabel("");
        if (ImGui::Button(Tr(ICON_FA_PLUS "  Nuqta qo'shish"), ImVec2(-FLT_MIN, 0)))
        {
            // Polygons: in the middle of the closing edge; lines: one step further on.
            if (points.empty())
                points.push_back({ 0.0f, 0.0f });
            else if (points.size() == 1)
                points.push_back(points[0] + glm::vec2(1.0f, 0.0f));
            else if (kind == ColliderPointKind::Polygon)
                points.push_back((points.back() + points.front()) * 0.5f);
            else
                points.push_back(points.back() + (points.back() - points[points.size() - 2]));
            changed = true;
        }
        return changed;
    }

    bool SceneHierarchyPanel::FitPolygonToSprite(Entity entity, PolygonCollider2DComponent& c, int maxVertices)
    {
        auto* sprite = entity.TryGetComponent<SpriteRendererComponent>();
        if (!sprite)
            return false;
        AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
        const auto* animation = entity.TryGetComponent<SpriteAnimationComponent>();
        const glm::mat4 rect = Scene::GetSpriteRectTransform(*sprite, animation, registry);
        // The opaque outline of the frame the sprite shows (in its unit square, y up).
        std::vector<glm::vec2> unit;
        if (!sprite->TexturePath.empty())
        {
            ImageData image;
            if (Image::Load(Project::GetAssetFileSystemPath(sprite->TexturePath).string(), image, 4, false))
            {
                const SpriteRegion region = Scene::GetSpriteRegion(*sprite, animation, registry);
                const float w = (float)image.Width, h = (float)image.Height;
                // UVs have v up (textures are loaded bottom row first); the picture's rows go down.
                const int x0 = (int)std::floor(region.UVMin.x * w + 0.01f), x1 = (int)std::ceil(region.UVMax.x * w - 0.01f);
                const int y0 = (int)std::floor((1.0f - region.UVMax.y) * h + 0.01f), y1 = (int)std::ceil((1.0f - region.UVMin.y) * h - 0.01f);
                unit = Polygon2D::TraceAlphaOutline(image.Pixels.data(), (int)image.Width, (int)image.Height, x0, y0, x1, y1, 16, maxVertices);
            }
        }
        if (unit.size() < 3)
            unit = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } }; // the sprite's rectangle
        std::vector<glm::vec2> points;
        for (glm::vec2 u : unit)
        {
            if (sprite->FlipX) u.x = 1.0f - u.x;
            if (sprite->FlipY) u.y = 1.0f - u.y;
            glm::vec4 p = rect * glm::vec4(u.x - 0.5f, u.y - 0.5f, 0.0f, 1.0f);
            points.push_back(glm::vec2(p) - c.Offset);
        }
        if (sprite->FlipX != sprite->FlipY)
            std::reverse(points.begin(), points.end()); // mirrored: counter-clockwise again
        c.Points = std::move(points);
        return true;
    }

    // A frame of a texture shown in a box of `size` px (the whole texture without a frame).
    static void SpriteImage(const Ref<Texture2D>& texture, const SpriteRegion& region, float size)
    {
        glm::vec2 px = region.Pixels;
        float scale = size / std::max(1.0f, std::max(px.x, px.y));
        ImVec2 extent(std::max(4.0f, px.x * scale), std::max(4.0f, px.y * scale));
        // ImGui wants the top-left corner first: v flipped.
        ImGui::Image((ImTextureID)(uintptr_t)texture->GetRendererID(), extent, ImVec2(region.UVMin.x, region.UVMax.y),
                     ImVec2(region.UVMax.x, region.UVMin.y));
    }

    bool SceneHierarchyPanel::DrawSpriteRenderer(Entity entity, SpriteRendererComponent& c)
    {
        bool changed = UI::PropertyColor4("Rang", c.Color);
        if (UI::PropertyAssetPath("Tekstura", c.TexturePath, ".png;.jpg;.jpeg;.bmp;.tga", ICON_FA_FILE_IMAGE))
        {
            c.Texture = c.TexturePath.empty() ? nullptr
                : TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TexturePath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
            changed = true;
        }

        // ---- sprite sheet frame
        const SpriteSheet* sheet = c.TexturePath.empty() ? nullptr : SpriteSheet::Get(c.TexturePath);
        const bool missingFrame = !c.SpriteName.empty() && (!sheet || !sheet->Find(c.SpriteName));
        UI::PropertyLabel("Sprite (kadr)", "Teksturaning nomlangan kadri (Sprite muharriri bilan kesiladi, .zmeta faylida saqlanadi). "
                                           "Bo'sh - butun rasm.");
        ImGui::PushID("spriteFrame");
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
        std::string preview = c.SpriteName.empty() ? std::string("(butun rasm)")
                            : missingFrame ? std::format(ICON_FA_TRIANGLE_EXCLAMATION "  {} (topilmadi)", c.SpriteName) : c.SpriteName;
        ImGui::BeginDisabled(!sheet && c.SpriteName.empty());
        if (ImGui::BeginCombo("##frame", preview.c_str(), ImGuiComboFlags_HeightLarge))
        {
            if (ImGui::Selectable(Tr("(butun rasm)"), c.SpriteName.empty()))
            {
                c.SpriteName.clear();
                changed = true;
            }
            if (sheet)
                for (const SpriteFrame& f : sheet->Frames)
                {
                    std::string label = std::format("{}  ({}x{})", f.Name, f.Size.x, f.Size.y);
                    if (ImGui::Selectable(label.c_str(), f.Name == c.SpriteName))
                    {
                        c.SpriteName = f.Name;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered() && c.Texture)
                    {
                        ImGui::BeginTooltip();
                        SpriteImage(c.Texture, SpriteRegion::FromFrame(f, c.Texture->GetWidth(), c.Texture->GetHeight()), 96.0f);
                        ImGui::EndTooltip();
                    }
                }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !sheet && c.SpriteName.empty())
            UI::Tooltip("Teksturada kadrlar yo'q: Sprite muharririda kesing (" ICON_FA_SCISSORS " tugma)");
        ImGui::SameLine();
        ImGui::BeginDisabled(c.TexturePath.empty());
        if (ImGui::Button(ICON_FA_SCISSORS, ImVec2(button, 0)) && OnSpriteEditorRequested)
            OnSpriteEditorRequested(c.TexturePath);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Sprite muharriri: teksturani kadrlarga kesish");
        ImGui::PopID();

        if (c.Texture)
        {
            UI::PropertyLabel("");
            SpriteRegion region = Scene::GetSpriteRegion(c, nullptr, Project::GetActive() ? Project::GetAssetRegistry() : nullptr);
            SpriteImage(c.Texture, region, 64.0f);
        }

        // ---- draw mode
        const char* modes[] = { "Oddiy (Simple)", "9-slice (Sliced)", "Plitka (Tiled)" };
        int mode = (int)c.DrawMode;
        UI::PropertyLabel("Chizish rejimi", "Oddiy: rasm kvadratga cho'ziladi (o'lchami - masshtab). 9-slice: burchaklar o'z o'lchamida qoladi, "
                                            "chetlar va o'rta cho'ziladi - panellar, tugmalar, ramkalar uchun. Plitka: chetlar va o'rta takrorlanadi.");
        ImGui::PushID("drawMode");
        if (ImGui::Combo("##v", &mode, modes, 3) && mode != (int)c.DrawMode)
        {
            // Switching to a sized mode: the scale becomes the size, so the sprite keeps its
            // footprint and the corners are not stretched.
            if (c.DrawMode == SpriteDrawMode::Simple && c.Size == glm::vec2(1.0f))
            {
                auto& t = entity.GetComponent<TransformComponent>();
                if (glm::abs(t.Scale.x) > 1e-4f && glm::abs(t.Scale.y) > 1e-4f && (t.Scale.x != 1.0f || t.Scale.y != 1.0f))
                {
                    c.Size = glm::abs(glm::vec2(t.Scale));
                    t.Scale.x = t.Scale.x < 0.0f ? -1.0f : 1.0f;
                    t.Scale.y = t.Scale.y < 0.0f ? -1.0f : 1.0f;
                }
            }
            c.DrawMode = (SpriteDrawMode)mode;
            changed = true;
        }
        ImGui::PopID();
        if (c.DrawMode != SpriteDrawMode::Simple)
        {
            if (UI::PropertyFloat2("O'lcham (birlik)", c.Size, 0.02f))
            {
                c.Size = glm::max(c.Size, glm::vec2(0.0f));
                changed = true;
            }
            UI::PropertyLabel("Chegaralar (px)", "Chap, o'ng, yuqori, past - burchaklar shu piksellar. Hammasi 0 bo'lsa kadrning "
                                                 "chegaralari ishlatiladi (Sprite muharriri).");
            ImGui::PushID("border");
            if (ImGui::DragFloat4("##v", &c.SliceBorder.x, 0.25f, 0.0f, 4096.0f, "%.0f"))
            {
                c.SliceBorder = glm::max(c.SliceBorder, glm::vec4(0.0f));
                changed = true;
            }
            ImGui::PopID();
            if (!c.HasSliceBorder())
            {
                const SpriteFrame* frame = sheet && !c.SpriteName.empty() ? sheet->Find(c.SpriteName) : nullptr;
                UI::PropertyLabel("");
                if (frame && frame->HasBorder())
                    ImGui::TextDisabled("Kadrdan: %.0f, %.0f, %.0f, %.0f", frame->Border.x, frame->Border.y, frame->Border.z, frame->Border.w);
                else
                    ImGui::TextDisabled(c.DrawMode == SpriteDrawMode::Tiled ? "Chegarasiz: butun rasm takrorlanadi" : "Chegara yo'q: oddiy cho'zish");
            }
            if (UI::PropertyFloat("Piksel / birlik", c.PixelsPerUnit, 0.5f, 0.01f, 10000.0f, "%.1f"))
            {
                c.PixelsPerUnit = std::max(0.01f, c.PixelsPerUnit);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Bir dunyo birligiga necha piksel: chegara burchaklarining va plitkalarning o'lchami");
        }
        else
        {
            ImGui::BeginDisabled(!c.SpriteName.empty());
            changed |= UI::PropertyFloat("Takrorlash", c.TilingFactor, 0.05f, 0.0f, 100.0f);
            ImGui::EndDisabled();
        }

        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText(Tr("Saralash"));
        changed |= DrawSortingLayer(c.SortingLayer, c.SortingOrder);
        if (UI::PropertyBool("Pixel-art (aniq piksellar)", c.PixelArt) && !c.TexturePath.empty())
        {
            c.Texture = TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TexturePath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
            changed = true;
        }
        changed |= UI::PropertyBool("Gorizontal aks (Flip X)", c.FlipX);
        changed |= UI::PropertyBool("Vertikal aks (Flip Y)", c.FlipY);
        changed |= UI::PropertyBool(ICON_FA_FIRE "  Yorug'lik (Lit)", c.Lit);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Light 2D yorug'liklari va sahnaning 2D muhit yorug'ligi (Sahna sozlamalari) bilan yoritiladi. "
                        "O'chiq: rasm o'z rangida chiziladi");

        // ---- effects: hit flash, outline
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText(Tr("Effektlar"));
        UI::PropertyLabel("Flash rangi", "Rasm shu rangga aralashadi (oq - urilish chaqnashi). Skriptdan: Flash(rang, soniya)");
        changed |= ImGui::ColorEdit4("##flashColor", &c.FlashColor.x, ImGuiColorEditFlags_AlphaBar);
        UI::PropertyLabel("Flash", "0 - o'chiq, 1 - butunlay flash rangida. Skriptdagi Flash() buning ustidan so'nib boradigan flash o'ynaydi");
        if (ImGui::SliderFloat("##flashAmount", &c.FlashAmount, 0.0f, 1.0f, "%.2f"))
        {
            c.FlashAmount = std::clamp(c.FlashAmount, 0.0f, 1.0f);
            changed = true;
        }
        {
            const bool running = m_Context && m_Context->IsRunning();
            UI::PropertyLabel("");
            ImGui::BeginDisabled(!running);
            if (ImGui::Button(Tr(ICON_FA_BOLT "  Sinash")))
                c.Flash(glm::vec4(glm::vec3(c.FlashColor), 1.0f), 0.3f);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip(running ? "Flash rangida 0.3 soniyalik chaqnash" : "O'yin rejimida sinab ko'ring");
            if (running && c.IsFlashing())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%.2f", c.GetFlashAmount());
            }
        }
        UI::PropertyLabel("Kontur rangi");
        changed |= ImGui::ColorEdit4("##outlineColor", &c.OutlineColor.x, ImGuiColorEditFlags_AlphaBar);
        UI::PropertyLabel("Kontur (px)", "Rasmning ko'rinadigan piksellari atrofidagi chegara, tekstura pikselida (0 - o'chiq). "
                                         "Skriptdan: SetOutline(rang, qalinlik)");
        if (ImGui::DragFloat("##outlineThickness", &c.OutlineThickness, 0.05f, 0.0f, 16.0f, "%.1f"))
        {
            c.OutlineThickness = std::clamp(c.OutlineThickness, 0.0f, 64.0f);
            changed = true;
        }
        if (c.OutlineThickness > 0.0f)
        {
            // The renderer's own rule (a screen UI sprite in a sized mode is laid out in its UI rect).
            const auto* ui = entity.TryGetComponent<UITransformComponent>();
            const glm::vec2* uiSize = ui && c.DrawMode != SpriteDrawMode::Simple ? &ui->Size : nullptr;
            if (!Scene::CanDrawOutline(c, ui ? nullptr : entity.TryGetComponent<SpriteAnimationComponent>(),
                                       Project::GetActive() ? Project::GetAssetRegistry() : nullptr, uiSize))
            {
                UI::PropertyLabel("");
                ImGui::TextDisabled(!c.Texture ? "Kontur uchun tekstura kerak"
                                               : "Kontur faqat bitta bo'lak bo'lib chiziladigan sprite'da (chegarasiz, takrorlashsiz)");
            }
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawUIImage(Entity entity, UIImageComponent& c)
    {
        (void)entity;
        bool changed = UI::PropertyColor4("Rang", c.Color);
        if (UI::PropertyAssetPath("Tekstura", c.TexturePath, ".png;.jpg;.jpeg;.bmp;.tga", ICON_FA_FILE_IMAGE))
        {
            c.Texture = c.TexturePath.empty() ? nullptr
                : TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TexturePath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
            changed = true;
        }
        const SpriteSheet* sheet = c.TexturePath.empty() ? nullptr : SpriteSheet::Get(c.TexturePath);
        const bool missingFrame = !c.SpriteName.empty() && (!sheet || !sheet->Find(c.SpriteName));
        UI::PropertyLabel("Sprite (kadr)", "Teksturaning nomlangan kadri (Sprite muharriri). Bo'sh - butun rasm. Tekstura bo'lmasa - oddiy rang");
        ImGui::PushID("uiImageFrame");
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
        std::string preview = c.SpriteName.empty() ? std::string("(butun rasm)")
                            : missingFrame ? std::format(ICON_FA_TRIANGLE_EXCLAMATION "  {} (topilmadi)", c.SpriteName) : c.SpriteName;
        ImGui::BeginDisabled(!sheet && c.SpriteName.empty());
        if (ImGui::BeginCombo("##frame", preview.c_str(), ImGuiComboFlags_HeightLarge))
        {
            if (ImGui::Selectable(Tr("(butun rasm)"), c.SpriteName.empty()))
            {
                c.SpriteName.clear();
                changed = true;
            }
            if (sheet)
                for (const SpriteFrame& f : sheet->Frames)
                {
                    std::string label = std::format("{}  ({}x{})", f.Name, f.Size.x, f.Size.y);
                    if (ImGui::Selectable(label.c_str(), f.Name == c.SpriteName))
                    {
                        c.SpriteName = f.Name;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered() && c.Texture)
                    {
                        ImGui::BeginTooltip();
                        SpriteImage(c.Texture, SpriteRegion::FromFrame(f, c.Texture->GetWidth(), c.Texture->GetHeight()), 96.0f);
                        ImGui::EndTooltip();
                    }
                }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(c.TexturePath.empty());
        if (ImGui::Button(ICON_FA_SCISSORS, ImVec2(button, 0)) && OnSpriteEditorRequested)
            OnSpriteEditorRequested(c.TexturePath);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Sprite muharriri: teksturani kadrlarga kesish");
        ImGui::PopID();

        const char* types[] = { "Oddiy (Simple)", "9-slice (Sliced)", "Plitka (Tiled)", "To'ldirilgan (Filled)" };
        int type = (int)c.Type;
        UI::PropertyLabel("Turi", "Oddiy: rasm to'rtburchakka cho'ziladi. 9-slice: burchaklar o'z o'lchamida (panel, tugma). "
                                  "Plitka: chetlar va o'rta takrorlanadi. To'ldirilgan: rasmning bir qismi (chiziq, taymer, qayta tiklanish)");
        ImGui::PushID("uiImageType");
        if (ImGui::Combo("##v", &type, types, 4)) { c.Type = (UIImageType)type; changed = true; }
        ImGui::PopID();
        if (c.Type == UIImageType::Simple || c.Type == UIImageType::Filled)
            changed |= UI::PropertyBool("Nisbatni saqlash", c.PreserveAspect);
        if (c.Type == UIImageType::Sliced || c.Type == UIImageType::Tiled)
        {
            UI::PropertyLabel("Chegaralar (px)", "Chap, o'ng, yuqori, past. Hammasi 0 bo'lsa kadrning chegaralari (Sprite muharriri)");
            ImGui::PushID("uiImageBorder");
            if (ImGui::DragFloat4("##v", &c.SliceBorder.x, 0.25f, 0.0f, 4096.0f, "%.0f"))
            {
                c.SliceBorder = glm::max(c.SliceBorder, glm::vec4(0.0f));
                changed = true;
            }
            ImGui::PopID();
            if (UI::PropertyFloat("Chegara masshtabi", c.BorderScale, 0.01f, 0.01f, 100.0f, "%.2f"))
            {
                c.BorderScale = std::max(0.01f, c.BorderScale);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Burchaklar ekranda chegara piksellari x shu son (plitkalar ham)");
        }
        if (c.Type == UIImageType::Filled)
        {
            const char* methods[] = { "Gorizontal", "Vertikal", "Radial 360" };
            int method = (int)c.FillMethod;
            if (UI::PropertyCombo("To'ldirish usuli", method, methods, 3))
            {
                c.FillMethod = (UIFillMethod)method;
                c.FillOrigin = 0;
                changed = true;
            }
            const char* horizontal[] = { "Chap", "O'ng" };
            const char* vertical[] = { "Past", "Yuqori" };
            const char* radial[] = { "Yuqori", "O'ng", "Past", "Chap" };
            const char* const* origins = c.FillMethod == UIFillMethod::Horizontal ? horizontal : c.FillMethod == UIFillMethod::Vertical ? vertical : radial;
            const int originCount = c.FillMethod == UIFillMethod::Radial360 ? 4 : 2;
            c.FillOrigin = std::clamp(c.FillOrigin, 0, originCount - 1);
            changed |= UI::PropertyCombo("Boshlanish", c.FillOrigin, origins, originCount);
            changed |= UI::PropertySlider("Miqdor", c.FillAmount, 0.0f, 1.0f);
            if (c.FillMethod == UIFillMethod::Radial360)
                changed |= UI::PropertyBool("Soat yo'nalishida", c.FillClockwise);
        }
        if (UI::PropertyBool("Pixel-art (aniq piksellar)", c.PixelArt) && !c.TexturePath.empty())
        {
            c.Texture = TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TexturePath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
            changed = true;
        }
        changed |= UI::PropertyBool("Sichqonchani to'sadi", c.RaycastTarget);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Yoqiq: rasm ostidagi UI tugma, slayder va boshqalar bosilmaydi (masalan, dialog foni)");
        return changed;
    }

    // ------------------------------------------------------------------ tint animation
    // The gradient as a strip `height` px high (a checkerboard under see-through colours).
    static void DrawTintGradient(const std::vector<TintKey>& keys, float width, float height, float marker = -1.0f)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + width, p0.y + height);
        const float cell = height * 0.5f;
        for (float x = p0.x; x < p1.x; x += cell)
            for (int row = 0; row < 2; row++)
            {
                bool dark = ((int)((x - p0.x) / cell) + row) % 2 == 0;
                dl->AddRectFilled(ImVec2(x, p0.y + row * cell), ImVec2(std::min(x + cell, p1.x), p0.y + (row + 1) * cell),
                                  dark ? IM_COL32(90, 90, 96, 255) : IM_COL32(140, 140, 148, 255));
            }
        const int steps = std::max(8, (int)(width / 4.0f));
        for (int i = 0; i < steps; i++)
        {
            float t0 = (float)i / steps, t1 = (float)(i + 1) / steps;
            glm::vec4 ca = TintAnimation::EvaluateGradient(keys, t0), cb = TintAnimation::EvaluateGradient(keys, t1);
            ImU32 a = ImGui::ColorConvertFloat4ToU32(ImVec4(ca.r, ca.g, ca.b, ca.a));
            ImU32 b = ImGui::ColorConvertFloat4ToU32(ImVec4(cb.r, cb.g, cb.b, cb.a));
            dl->AddRectFilledMultiColor(ImVec2(p0.x + width * t0, p0.y), ImVec2(p0.x + width * t1, p1.y), a, b, b, a);
        }
        for (const TintKey& k : keys)
        {
            float x = p0.x + width * std::clamp(k.Time, 0.0f, 1.0f);
            dl->AddTriangleFilled(ImVec2(x - 4.0f, p1.y + 5.0f), ImVec2(x + 4.0f, p1.y + 5.0f), ImVec2(x, p1.y - 1.0f), IM_COL32(230, 230, 235, 255));
        }
        if (marker >= 0.0f)
        {
            float x = p0.x + width * std::clamp(marker, 0.0f, 1.0f);
            dl->AddLine(ImVec2(x, p0.y - 2.0f), ImVec2(x, p1.y + 2.0f), IM_COL32(255, 210, 80, 255), 2.0f);
        }
        dl->AddRect(p0, p1, IM_COL32(20, 20, 24, 255));
        ImGui::Dummy(ImVec2(width, height + 6.0f));
    }

    bool SceneHierarchyPanel::DrawBoneAttachment(Entity entity, BoneAttachmentComponent& c)
    {
        bool changed = DrawEntityRefField("Model obyekti", c.Target,
                                          "Skinned modelli (Mesh Renderer + skelet) obyekt. Bo'sh: eng yaqin shunday ota obyekt");
        Entity target = m_Context ? m_Context->GetBoneAttachmentTarget(entity) : Entity();
        const auto* mr = target ? target.TryGetComponent<MeshRendererComponent>() : nullptr;
        const Model* model = mr && mr->ModelAsset && mr->ModelAsset->IsSkinned() ? mr->ModelAsset.get() : nullptr;
        UI::PropertyLabel("");
        if (!target)
            ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.4f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Skinned model topilmadi (ota obyektlarda ham)");
        else if (!model)
            ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.4f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  '%s' modelida skelet yo'q", target.GetName().c_str());
        else
            ImGui::TextDisabled(ICON_FA_ARROW_RIGHT " %s  (%zu suyak)", target.GetName().c_str(), model->GetSkeleton().Joints.size());

        // The bone: the skeleton's joints, indented like the tree.
        UI::PropertyLabel("Suyak", "Shu suyakka ergashadi (Animator pozasi bilan, editorda ham)");
        const bool known = !model || FindJoint(model->GetSkeleton(), c.Bone) >= 0;
        if (!known)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.6f, 0.4f, 1.0f));
        if (ImGui::BeginCombo("##bone", c.Bone.empty() ? "(tanlang)" : c.Bone.c_str(), ImGuiComboFlags_HeightLarge))
        {
            if (!known)
                ImGui::PopStyleColor();
            if (model)
            {
                const auto& joints = model->GetSkeleton().Joints;
                std::vector<int> depth(joints.size(), 0);
                for (size_t i = 0; i < joints.size(); i++)
                {
                    if (joints[i].Parent >= 0 && joints[i].Parent < (int)i)
                        depth[i] = depth[(size_t)joints[i].Parent] + 1;
                    const std::string label = std::string((size_t)depth[i] * 2, ' ') + ICON_FA_BONE " " + joints[i].Name;
                    if (ImGui::Selectable(std::format("{}##j{}", label, i).c_str(), joints[i].Name == c.Bone))
                    {
                        c.Bone = joints[i].Name;
                        changed = true;
                    }
                }
            }
            else
                ImGui::TextDisabled("Avval skinned modelli obyektni tanlang");
            ImGui::EndCombo();
        }
        else if (!known)
            ImGui::PopStyleColor();
        if (!known && ImGui::IsItemHovered())
            UI::Tooltip("Modelning skeletida bunday suyak yo'q: obyekt odatdagidek otasiga ergashadi");
        changed |= UI::PropertyBool("Suyak masshtabi", c.InheritBoneScale);
        if (ImGui::IsItemHovered())
            UI::Tooltip("O'chiq: faqat suyakning joyi va burilishi (import masshtabi, siqilish e'tiborga olinmaydi). Model obyektining o'z "
                        "masshtabi baribir ta'sir qiladi");
        UI::PropertyLabel("");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Transform endi suyakka nisbatan siljish (offset): gizmo ham suyakka nisbatan suradi");
        ImGui::PopTextWrapPos();
        UI::PropertyLabel("");
        ImGui::BeginDisabled(!m_SceneEditable);
        if (ImGui::SmallButton(Tr(ICON_FA_CROSSHAIRS "  Suyakka qo'yish (offset 0)")))
        {
            auto& tc = entity.GetComponent<TransformComponent>();
            tc.Translation = glm::vec3(0.0f);
            tc.Rotation = glm::vec3(0.0f);
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Joy va burilish 0: obyekt aynan suyak nuqtasida, suyak yo'nalishida (masshtab o'zgarmaydi)");
        return changed;
    }

    bool SceneHierarchyPanel::DrawSubScene(Entity entity, SubSceneComponent& c)
    {
        namespace fs = std::filesystem;
        bool changed = false;
        bool open = false;
        const fs::path file = c.ScenePath.empty() ? fs::path() : Project::GetAssetFileSystemPath(c.ScenePath);
        std::error_code ec;
        const bool exists = !file.empty() && fs::is_regular_file(file, ec);
        UI::AssetPathOptions options;
        options.ActionIcon = ICON_FA_PEN;
        options.ActionTooltip = "Sub-sahnani tahrirlash uchun ochish (ochiq sahna avval saqlanadi)";
        options.ActionClicked = &open;
        options.Missing = !c.ScenePath.empty() && !exists;
        if (exists && GetAssetThumbnail)
            options.PreviewTexture = GetAssetThumbnail(file);
        options.PreviewTopRowFirst = true;
        options.Tooltip = "Shu obyekt joyiga qo'yiladigan sahna fayli (.zscene): kontent brauzeridan tashlang. O'yinda u qo'shimcha "
                          "(additive) yuklanadi - o'z obyektlari, skriptlari va fizika dunyosi bilan";
        if (UI::PropertyAssetPath("Sahna", c.ScenePath, ".zscene", ICON_FA_LAYER_GROUP, options))
            changed = true;
        if (open && !c.ScenePath.empty())
            m_PendingOpenScene = c.ScenePath; // once the inspector is drawn (it still reads this entity)
        changed |= UI::PropertyBool("Birga yuklash", c.LoadOnStart);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Yoqilgan: sahna yuklanganda shu kadrning o'zida yuklanadi. O'chirilgan: skript LoadSubScene() "
                        "chaqirganda (masalan, o'yinchi yaqinlashganda)");
        changed |= UI::PropertyBool("Obyekt joyiga", c.ApplyTransform);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Sub-sahnaning ildiz obyektlari shu obyektning dunyo transformiga ko'chiriladi (yuklanganda bir marta). "
                        "O'chirilgan: o'z faylidagi joyida");
        changed |= UI::PropertyBool("Editorda chizish", c.ShowInEditor);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Sahna ko'rinishida sub-sahna joyida chiziladi (faqat ko'rish uchun: tanlab bo'lmaydi)");

        // What the file holds (read again when it changes).
        if (exists)
        {
            const auto time = fs::last_write_time(file, ec);
            if (m_SubSceneFile.Path != c.ScenePath || m_SubSceneFile.Time != time)
            {
                m_SubSceneFile.Path = c.ScenePath;
                m_SubSceneFile.Time = time;
                m_SubSceneFile.Readable = SceneSerializer::ReadSceneInfo(file, m_SubSceneFile.Info);
            }
            UI::PropertyLabel("Tarkibi");
            if (m_SubSceneFile.Readable)
            {
                const SceneFileInfo& info = m_SubSceneFile.Info;
                ImGui::TextDisabled("%s: %zu obyekt (%zu ildiz)", info.Name.c_str(), info.EntityCount, info.RootCount);
                if (!info.Metadata.Description.empty() && ImGui::IsItemHovered())
                    UI::Tooltip(info.Metadata.Description.c_str());
            }
            else
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", m_SubSceneFile.Info.Error.c_str());
        }
        if (GetSubSceneProblem)
            if (const std::string problem = GetSubSceneProblem(entity); !problem.empty() && exists)
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", problem.c_str());
            }

        // In the game: whether it is in, and loading / unloading it by hand.
        if (m_SceneManager)
        {
            Ref<Scene> loaded = m_SceneManager->GetSubScene(entity);
            UI::PropertyLabel("O'yinda");
            if (loaded)
            {
                ImGui::TextColored(UI::AccentColor(), ICON_FA_CIRCLE_CHECK "  yuklangan (%zu obyekt)", loaded->GetRegistry().view<IDComponent>().size());
                ImGui::SameLine();
                if (ImGui::SmallButton(Tr("Yopish")))
                    m_SceneManager->UnloadSubScene(entity);
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Sub-sahnani kadr oxirida yopish (UnloadSubScene)");
            }
            else
            {
                ImGui::TextDisabled("yuklanmagan");
                ImGui::SameLine();
                ImGui::BeginDisabled(c.ScenePath.empty());
                if (ImGui::SmallButton(Tr("Yuklash")))
                    m_SceneManager->LoadSubScene(entity);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    UI::Tooltip("Sub-sahnani kadr oxirida shu obyekt joyiga yuklash (LoadSubScene)");
            }
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawFootsteps(Entity entity, FootstepsComponent& c)
    {
        bool changed = false;
        InactiveComponentNote(entity, "Obyekt nofaol: event'lar kelmaydi, qadam eshitilmaydi");
        // ---- sounds: a cleared row goes away, the empty one at the end adds one
        int remove = -1;
        for (int i = 0; i <= (int)c.Sounds.size(); i++)
        {
            const bool adding = i == (int)c.Sounds.size();
            std::string path = adding ? std::string() : c.Sounds[(size_t)i];
            ImGui::PushID(i + 300);
            const std::string label = adding ? std::string("Ovoz qo'shish") : std::format("Qadam {}", i + 1);
            UI::AssetPathOptions options;
            options.Tooltip = adding ? "Qadam ovozini shu yerga tashlang yoki tanlang (bir nechta: har qadamda tasodifiy, ketma-ket takrorlanmaydi)"
                                     : "Qadam ovozi. Tozalash - ro'yxatdan olib tashlaydi";
            if (UI::PropertyAssetPath(label.c_str(), path, ".wav;.mp3;.flac;.ogg", ICON_FA_FILE_AUDIO, options))
            {
                if (adding)
                {
                    if (!path.empty())
                        c.Sounds.push_back(path);
                }
                else if (path.empty())
                    remove = i;
                else
                    c.Sounds[(size_t)i] = path;
                changed = true;
            }
            ImGui::PopID();
        }
        if (remove >= 0)
            c.Sounds.erase(c.Sounds.begin() + remove);

        auto* anim = entity.TryGetComponent<AnimatorComponent>();
        UI::PropertyLabel("Event nomi", "Shu nomli animatsiya event'i qadam: Animator klip event'lari (Animatsiya oynasida "
                                        "skelet klipiga qo'shiladi), Animation Player yoki sprite klip event'lari");
        changed |= ImGui::InputText("##event", &c.EventName);
        if (anim && !c.EventName.empty() &&
            std::none_of(anim->Events.begin(), anim->Events.end(), [&](const AnimatorEvent& e) { return e.Name == c.EventName; }))
        {
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), ICON_FA_CIRCLE_INFO "  Animator'da '%s' event'i yo'q: Animatsiya oynasida "
                               "yurish kliplariga oyoq yerga tekkan kadrlarda qo'shing", c.EventName.c_str());
            ImGui::PopTextWrapPos();
        }
        changed |= UI::PropertySlider("Balandlik", c.Volume, 0.0f, 2.0f);
        auto range = [&](const char* label, const char* id, glm::vec2& v, float lo, float hi, const char* tooltip) {
            UI::PropertyLabel(label, tooltip);
            ImGui::PushID(id);
            bool edited = ImGui::DragFloatRange2("##v", &v.x, &v.y, 0.005f, lo, hi, "%.2f", "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();
            return edited;
        };
        changed |= range("Balandlik oralig'i", "stepVolumeRandom", c.VolumeRandom, 0.0f, 2.0f, "Har qadamda tasodifiy ko'paytuvchi: min - max");
        changed |= range("Ton oralig'i", "stepPitchRandom", c.PitchRandom, 0.1f, 3.0f, "Har qadamda tasodifiy ton ko'paytuvchisi (0.92 - 1.08 ~ +-1.4 yarim ton)");

        UI::PropertyHeader(ICON_FA_PERSON_RUNNING "  Tezlik bo'yicha", "Tez yurganda qadam balandroq va sal yuqoriroq tonda: tezlik Animator'ning "
                                                                     "float parametridan (odatda blend tree parametri), mos yozuv tezligida "
                                                                     "ovoz o'z holicha. Juda sekinda (idle'ga o'tishda) eshitilmaydi.");
        UI::PropertyLabel("Tezlik parametri", "Animator float parametri ((yo'q): tezlik hisobga olinmaydi)");
        if (anim)
            changed |= AnimatorEditing::FloatParameterCombo("##speedParam", c.SpeedParameter, *anim, "(yo'q)");
        else
        {
            changed |= ImGui::InputText("##speedParam", &c.SpeedParameter);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Obyektda Animator yo'q: tezlik hisobga olinmaydi");
        }
        changed |= UI::PropertyFloat("Mos tezlik", c.ReferenceSpeed, 0.05f, 0.0f, 100.0f);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Shu tezlikda (m/s) ovoz o'z balandligi va tonida (yurish tezligi)");
        changed |= UI::PropertyFloat("Ton x tezlik", c.SpeedPitch, 0.005f, 0.0f, 1.0f);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ton = 1 + shu x (tezlik / mos tezlik - 1): 0.12 da ikki baravar tezlikda 12% yuqori");
        changed |= UI::PropertyFloat("Balandlik x tezlik", c.SpeedVolume, 0.005f, 0.0f, 2.0f);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Balandlik = 1 + shu x (tezlik / mos tezlik - 1), 0.2 .. 1.6 oralig'ida");
        changed |= UI::PropertyFloat("Eng kichik tezlik", c.MinSpeed, 0.01f, 0.0f, 10.0f);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Bundan sekinda qadam eshitilmaydi (idle bilan aralashganda)");
        changed |= UI::PropertyFloat("Oraliq (s)", c.MinInterval, 0.005f, 0.0f, 1.0f);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Bundan yaqin ikki event bitta qadam (crossfade'da ikki klip birga event bersa)");
        {
            // What the speed parameter does now.
            float speed = c.ReferenceSpeed;
            if (anim)
                if (const AnimatorParameter* p = anim->FindParameter(c.SpeedParameter); p && p->Kind == AnimatorParameter::Type::Float)
                    speed = p->Value;
            float v = 1.0f, pitch = 1.0f;
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f); // wraps inside the value column
            if (c.GetSpeedScale(speed, v, pitch))
                ImGui::TextDisabled("Tezlik %.2f: balandlik x%.2f, ton x%.2f", speed, v, pitch);
            else
                ImGui::TextDisabled("Tezlik %.2f: juda sekin, eshitilmaydi", speed);
            ImGui::PopTextWrapPos();
        }

        UI::PropertyHeader(ICON_FA_VOLUME_HIGH "  Ijro", "Qaysi mixer guruhida va 3D ovoz sifatida (obyekt turgan joyda) eshitiladi");
        changed |= DrawAudioOutput(c.Output);
        changed |= UI::PropertyBool("3D ovoz", c.Spatial);
        if (c.Spatial)
        {
            changed |= UI::PropertyFloat("Min masofa", c.MinDistance, 0.05f, 0.0f, 1000.0f);
            changed |= UI::PropertyFloat("Max masofa", c.MaxDistance, 0.1f, 0.0f, 10000.0f);
            changed |= DrawSpatialDefaultsRow(c.MinDistance, c.MaxDistance);
        }
        UI::PropertyLabel("");
        ImGui::BeginDisabled(c.Sounds.empty());
        if (ImGui::SmallButton(Tr(ICON_FA_PLAY "  Qadamni tinglash")))
        {
            // Past the mixer at the reference speed, like a step sounds (a new random variation each time).
            const int index = c.PickSound();
            if (index >= 0 && !c.Sounds[(size_t)index].empty())
            {
                SoundParams params;
                params.Volume = c.Volume;
                params.VolumeRandom = c.VolumeRandom;
                params.PitchRandom = c.PitchRandom;
                params.Group = c.Output;
                AudioEngine::Play(Project::GetAssetFileSystemPath(c.Sounds[(size_t)index]), params);
            }
        }
        ImGui::EndDisabled();
        if (!m_SceneEditable)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%u qadam", c.StepCount);
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawAnimationPlayer(Entity entity, AnimationPlayerComponent& c)
    {
        bool changed = false;
        InactiveComponentNote(entity, "Obyekt nofaol: animatsiya o'ynamaydi (joyida to'xtab turadi)");
        // The clips: a cleared row goes away; the empty row at the end adds one.
        int remove = -1;
        for (int i = 0; i <= (int)c.Clips.size(); i++)
        {
            const bool adding = i == (int)c.Clips.size();
            std::string path = adding ? std::string() : c.Clips[(size_t)i];
            ImGui::PushID(i);
            std::string label = adding ? std::string("Klip qo'shish") : std::format("Klip {}", i + 1);
            UI::AssetPathOptions options;
            bool open = false;
            if (!adding)
            {
                options.ActionIcon = ICON_FA_TIMELINE;
                options.ActionTooltip = "Animatsiya oynasida ochish (kalit kadrlar, event'lar)";
                options.ActionClicked = &open;
                options.Missing = !path.empty() && !PropertyClipLibrary::Load(path);
            }
            options.Tooltip = adding ? ".zanim faylini shu yerga tashlang yoki tanlang"
                                     : i == 0 ? "Birinchi klip: o'yin boshlanganda o'ynaydi (Avto boshlash). Tozalash - ro'yxatdan olib tashlaydi"
                                              : "O'ynaladigan klip (.zanim): skript Play(\"nom\") bilan. Tozalash - ro'yxatdan olib tashlaydi";
            if (UI::PropertyAssetPath(label.c_str(), path, ".zanim", ICON_FA_FILM, options))
            {
                if (adding)
                {
                    if (!path.empty())
                        c.Clips.push_back(path);
                }
                else if (path.empty())
                    remove = i;
                else
                    c.Clips[(size_t)i] = path;
                changed = true;
            }
            if (open && OnAnimationWindowRequested)
                OnAnimationWindowRequested(entity, i);
            ImGui::PopID();
        }
        if (remove >= 0)
            c.Clips.erase(c.Clips.begin() + remove);
        UI::PropertyLabel("");
        ImGui::BeginDisabled(!Project::GetActive() || !m_SceneEditable);
        if (ImGui::Button(Tr(ICON_FA_FILE_CIRCLE_PLUS "  Yangi klip")))
        {
            // Assets/Animations/<entity name>.zanim (a free name), added and opened in the Animation window.
            std::string name;
            for (char ch : entity.GetName())
                name += std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' ? ch : '_';
            if (name.empty()) name = "Clip";
            std::filesystem::path dir = Project::GetAssetDirectory() / "Animations";
            std::filesystem::path file = AssetFiles::MakeUniquePath(dir, name + PropertyClip::Extension);
            PropertyClip clip;
            if (PropertyClipSerializer::Serialize(clip, file))
            {
                if (AssetRegistry* registry = Project::GetAssetRegistry())
                    registry->OnFileAdded(file);
                c.Clips.push_back(Project::GetAssetRelativePath(file));
                changed = true;
                ZM_INFO("Yangi animatsiya klipi: {}", c.Clips.back());
                if (OnFilesCreated)
                    OnFilesCreated({ file });
                if (OnAnimationWindowRequested)
                    OnAnimationWindowRequested(entity, (int)c.Clips.size() - 1);
            }
            else
                ZM_ERROR("Animatsiya klipi yozilmadi: {}", file.string());
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Assets/Animations ichida bo'sh .zanim klip yaratiladi, ro'yxatga qo'shiladi va Animatsiya oynasida ochiladi");
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_TIMELINE "  Oyna")) && OnAnimationWindowRequested)
            OnAnimationWindowRequested(entity, c.Current >= 0 ? c.Current : 0);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Animatsiya oynasi: treklar, kalit kadrlar, event'lar, ko'rib chiqish");

        changed |= UI::PropertyFloat("Tezlik", c.Speed, 0.01f, -10.0f, 10.0f);
        UI::PropertyLabel("Avto boshlash", "O'yin boshlanganda birinchi klip o'ynaydi");
        changed |= ImGui::Checkbox("##playonstart", &c.PlayOnStart);
        {
            const char* items[] = { "Klipdagidek", "Bir marta", "Takror", "Borib-qaytish" };
            int wrap = (int)c.Wrap;
            if (UI::PropertyCombo("Tugaganda", wrap, items, 4))
            {
                c.Wrap = (AnimationPlayerWrap)wrap;
                changed = true;
            }
        }
        if (c.Current >= 0)
        {
            UI::PropertyLabel("Holat");
            ImGui::Text("%s  %s  %.2f / %.2f s", c.Playing ? ICON_FA_PLAY : ICON_FA_PAUSE, c.GetCurrentClipName().c_str(), c.GetTime(), c.GetDuration());
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawTintAnimation(Entity entity, TintAnimationComponent& c)
    {
        bool changed = false;
        const bool renders = entity.HasComponent<SpriteRendererComponent>() || entity.HasComponent<CircleRendererComponent>() ||
                             entity.HasComponent<TextComponent>() || entity.HasComponent<TilemapComponent>() ||
                             entity.HasComponent<Light2DComponent>();
        if (!renders)
        {
            UI::PropertyLabel("");
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Bo'yaydigan narsa yo'q (Sprite, Circle, Text, Tilemap yoki Light 2D)");
        }

        // ---- gradient: a strip with the keys, then the keys one by one
        UI::PropertyLabel("Gradiyent", "Chiziladigan rang = renderer rangi x shu gradiyent rangi (animatsiya vaqtida). "
                                       "Oq - o'zgarishsiz, alfa 0 - ko'rinmas. Renderer'ning o'z rangi o'zgarmaydi");
        const bool playing = c.Playing || c.EditorPreview;
        DrawTintGradient(c.Keys, std::max(40.0f, ImGui::GetContentRegionAvail().x), ImGui::GetFrameHeight() * 0.8f,
                         playing || c.Elapsed > 0.0f || c.StartOffset > 0.0f ? c.GetPhase() : -1.0f);
        int remove = -1;
        bool sort = false;
        for (int i = 0; i < (int)c.Keys.size(); i++)
        {
            TintKey& k = c.Keys[(size_t)i];
            ImGui::PushID(i);
            UI::PropertyLabel(i == 0 ? "Kalitlar" : "", "Vaqt (0..1, davomiylikka nisbatan) va rang. Kalitlar orasida rang tekis o'tadi");
            const float button = ImGui::GetFrameHeight();
            const float swatch = ImGui::GetFrameHeight() * 1.8f;
            const float avail = ImGui::GetContentRegionAvail().x - button - swatch - ImGui::GetStyle().ItemSpacing.x * 2.0f;
            ImGui::SetNextItemWidth(std::max(40.0f, avail));
            if (ImGui::DragFloat("##time", &k.Time, 0.005f, 0.0f, 1.0f, "vaqt %.2f"))
                changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit())
                sort = true;
            ImGui::SameLine();
            // The swatch opens the picker (rgba, the alpha shown over a checkerboard).
            changed |= ImGui::ColorEdit4("##color", &k.Color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar |
                                                                ImGuiColorEditFlags_AlphaPreviewHalf);
            ImGui::SameLine();
            ImGui::BeginDisabled(c.Keys.size() <= 1);
            if (ImGui::Button(ICON_FA_TRASH, ImVec2(button, 0)))
                remove = i;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip(c.Keys.size() <= 1 ? "Kamida bitta kalit qoladi" : "Kalitni o'chirish");
            ImGui::PopID();
        }
        if (remove >= 0 && c.Keys.size() > 1)
        {
            c.Keys.erase(c.Keys.begin() + remove);
            changed = true;
        }
        UI::PropertyLabel("");
        if (ImGui::Button(Tr(ICON_FA_PLUS "  Kalit")))
        {
            // In the widest gap, with the colour the gradient has there (nothing changes until it is edited).
            TintAnimation::SortKeys(c.Keys);
            float time = 0.5f, widest = -1.0f;
            for (size_t i = 0; i + 1 < c.Keys.size(); i++)
                if (c.Keys[i + 1].Time - c.Keys[i].Time > widest)
                {
                    widest = c.Keys[i + 1].Time - c.Keys[i].Time;
                    time = (c.Keys[i].Time + c.Keys[i + 1].Time) * 0.5f;
                }
            if (c.Keys.size() == 1)
                time = c.Keys[0].Time < 0.5f ? 1.0f : 0.0f;
            c.Keys.push_back({ time, TintAnimation::EvaluateGradient(c.Keys, time) });
            sort = changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_ARROWS_ROTATE "  Teskari")))
        {
            for (TintKey& k : c.Keys)
                k.Time = 1.0f - k.Time;
            sort = changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Gradiyentni teskari aylantirish");
        if (sort)
            TintAnimation::SortKeys(c.Keys);

        // ---- timing
        if (UI::PropertyFloat("Davomiylik (s)", c.Duration, 0.01f, 0.0f, 3600.0f, "%.2f"))
        {
            c.Duration = std::max(0.0f, c.Duration);
            changed = true;
        }
        const char* modes[] = { "Bir marta (Once)", "Takror (Loop)", "Borib-qaytish (PingPong)" };
        int mode = (int)c.Mode;
        if (UI::PropertyCombo("Rejim", mode, modes, 3))
        {
            c.Mode = (TintPlayMode)mode;
            changed = true;
        }
        static const char* eases[EaseCount] = { "Linear", "In Quad", "Out Quad", "In-Out Quad", "Out Cubic", "In-Out Cubic",
                                                "Out Back", "Out Elastic", "Out Bounce" };
        int ease = (int)c.Easing;
        if (UI::PropertyCombo("Silliqlik (ease)", ease, eases, EaseCount))
        {
            c.Easing = (Ease)ease;
            changed = true;
        }
        if (c.Mode != TintPlayMode::Once)
        {
            if (UI::PropertyInt("Sikllar (0 = cheksiz)", c.Cycles))
            {
                c.Cycles = std::max(0, c.Cycles);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Shuncha marta aylanib to'xtaydi (PingPong: borib-qaytish bitta sikl). Masalan miltillash: oq -> shaffof, PingPong, 6 sikl");
        }
        if (UI::PropertyFloat("Boshlanish siljishi (s)", c.StartOffset, 0.01f, 0.0f, 3600.0f, "%.2f"))
        {
            c.StartOffset = std::max(0.0f, c.StartOffset);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Animatsiya shuncha soniyadan boshlanadi: bir xil obyektlar bir vaqtda miltillamasin");
        changed |= UI::PropertyBool("O'yin boshida", c.PlayOnStart);
        if (ImGui::IsItemHovered())
            UI::Tooltip("O'yin boshlanganda (yoki obyekt paydo bo'lganda) o'zi boshlanadi. Skriptdan: PlayTint() / StopTint()");

        // ---- preview (scene view) / play controls (game, and Simulate: it plays the tints too)
        UI::PropertyLabel("");
        const bool running = m_Context && (m_Context->IsRunning() || m_Context->IsSimulating());
        if (running)
        {
            if (ImGui::Button(c.Playing ? ICON_FA_STOP "  To'xtatish" : ICON_FA_PLAY "  Boshlash"))
            {
                if (c.Playing) c.Stop();
                else c.Play(true);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%.2f s%s", c.Elapsed, c.IsFinished() ? "  (tugadi)" : "");
        }
        else
        {
            if (ImGui::Button(c.EditorPreview ? ICON_FA_STOP "  Ko'rishni to'xtatish" : ICON_FA_PLAY "  Sahnada ko'rish"))
            {
                c.EditorPreview = !c.EditorPreview;
                c.Elapsed = 0.0f;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Editor sahnasida aylanib o'ynaydi (saqlanmaydi; o'yin har doim boshidan boshlanadi)");
        }
        return changed;
    }

    // ------------------------------------------------------------------ 2D light
    bool SceneHierarchyPanel::DrawLight2D(Entity entity, Light2DComponent& c)
    {
        InactiveComponentNote(entity, "Obyekt nofaol: yorug'lik hisobga olinmaydi");
        bool changed = false;
        const char* types[] = { "Nuqta (Point)", "Konus (Spot)" };
        int type = (int)c.Type;
        if (UI::PropertyCombo("Turi", type, types, 2))
        {
            c.Type = (Light2DType)type;
            changed = true;
        }
        changed |= UI::PropertyColor3("Rang", c.Color);
        if (UI::PropertyFloat("Kuch", c.Intensity, 0.01f, 0.0f, 100.0f, "%.2f"))
        {
            c.Intensity = std::max(0.0f, c.Intensity);
            changed = true;
        }
        if (UI::PropertyFloat("Radius", c.Radius, 0.02f, 0.0f, 1000.0f, "%.2f"))
        {
            c.Radius = std::max(0.0f, c.Radius);
            c.InnerRadius = std::min(c.InnerRadius, c.Radius);
            changed = true;
        }
        if (UI::PropertyFloat("Ichki radius", c.InnerRadius, 0.02f, 0.0f, 1000.0f, "%.2f"))
        {
            c.InnerRadius = std::clamp(c.InnerRadius, 0.0f, c.Radius);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Shu masofagacha to'liq yorqinlik, keyin radiusgacha so'nadi");
        UI::PropertyLabel(EditorText::Choose("So'nish", "Falloff"), "(1 - t)^daraja: 1 - chiziqli, 2 - yumshoq, 4 - yorqin markaz, tez so'nadi");
        if (ImGui::SliderFloat("##falloff", &c.Falloff, 0.25f, 6.0f, "%.2f"))
        {
            c.Falloff = std::clamp(c.Falloff, 0.1f, 8.0f);
            changed = true;
        }
        if (c.Type == Light2DType::Spot)
        {
            UI::PropertyLabel("Konus burchagi", "Konusning to'liq kengligi (gradus). Obyektning +X o'qi bo'ylab: aylantirish bilan buriladi");
            if (ImGui::SliderFloat("##spotAngle", &c.SpotAngle, 1.0f, 360.0f, "%.0f\xc2\xb0"))
            {
                c.SpotAngle = std::clamp(c.SpotAngle, 1.0f, 360.0f);
                changed = true;
            }
            UI::PropertyLabel("Chet yumshoqligi", "Konus chetining qanchasi (0..1) yumshoq so'nadi");
            if (ImGui::SliderFloat("##spotSoftness", &c.SpotSoftness, 0.0f, 1.0f, "%.2f"))
            {
                c.SpotSoftness = std::clamp(c.SpotSoftness, 0.0f, 1.0f);
                changed = true;
            }
        }
        UI::PropertyLabel("Miltillash", "Kuch shuncha (0..1) tebranadi: mash'al, olov. 0 - tekis");
        if (ImGui::SliderFloat("##flicker", &c.Flicker, 0.0f, 1.0f, "%.2f"))
        {
            c.Flicker = std::clamp(c.Flicker, 0.0f, 1.0f);
            changed = true;
        }
        if (c.Flicker > 0.0f && UI::PropertyFloat("Miltillash tezligi", c.FlickerSpeed, 0.1f, 0.0f, 100.0f, "%.1f"))
        {
            c.FlickerSpeed = std::max(0.0f, c.FlickerSpeed);
            changed = true;
        }
        // Which renderers it lights: only the Lit ones.
        if (m_Context)
        {
            size_t lit = 0;
            for (auto e : m_Context->GetAllEntitiesWith<SpriteRendererComponent>())
                if (Entity(e, m_Context.get()).GetComponent<SpriteRendererComponent>().Lit) lit++;
            for (auto e : m_Context->GetAllEntitiesWith<TilemapComponent>())
                if (Entity(e, m_Context.get()).GetComponent<TilemapComponent>().Lit) lit++;
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            if (lit == 0)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Sahnada \"Lit\" belgilangan sprite/tilemap yo'q: "
                                   "yorug'lik faqat ularni yoritadi");
            else
                ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  %zu ta Lit renderer'ni yoritadi (muhit yorug'ligi: Sahna sozlamalari > 2D yorug'lik)", lit);
            ImGui::PopTextWrapPos();
        }
        return changed;
    }

    // ------------------------------------------------------------------ camera: pixel perfect
    bool SceneHierarchyPanel::DrawPixelPerfect(CameraComponent& c)
    {
        bool changed = false;
        PixelPerfectSettings& pp = c.PixelPerfect;
        UI::PropertyHeader(ICON_FA_CHESS_BOARD "  Pixel-perfect",
                           "Har bir tekstura pikseli ekranda bir xil butun sondagi pikselga aylanadi (pixel-art o'yinlar). "
                           "Faqat o'yin ko'rinishida (Play / Player); editor sahnasi o'zgarmaydi");
        changed |= UI::PropertyBool("Yoqilgan", pp.Enabled);
        if (!pp.Enabled)
            return changed;
        if (UI::PropertyFloat("Piksel / birlik", pp.PixelsPerUnit, 0.25f, PixelPerfect::MinPixelsPerUnit, PixelPerfect::MaxPixelsPerUnit, "%.1f"))
        {
            PixelPerfect::Sanitize(pp);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Bir dunyo birligida nechta tekstura pikseli (sprite'lar shu o'lchamda chizilgan bo'lishi kerak)");
        UI::PropertyLabel("Asosiy o'lcham (px)", "O'yin shu o'lchamdagi (art piksel) ekran uchun yasalgan: kattalashtirish - shu o'lcham "
                                                 "ko'rinishga sig'adigan eng katta butun son. Katta oyna dunyodan ko'proq ko'rsatadi");
        if (ImGui::DragInt2("##reference", &pp.ReferenceResolution.x, 1.0f, 1, 8192))
        {
            PixelPerfect::Sanitize(pp);
            changed = true;
        }
        changed |= UI::PropertyBool("Past o'lchamda chizish", pp.UpscaleRenderTarget);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Sahna art o'lchamida chiziladi va aniq piksellar bilan kattalashtiriladi: aylantirilgan sprite'lar va "
                        "chiziqlar ham piksel to'rida qoladi (UI ham shu o'lchamda)");
        changed |= UI::PropertyBool("Kamerani pikselga", pp.SnapCamera);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ko'rinish butun piksellarda siljiydi - harakatda piksellar titramaydi");
        changed |= UI::PropertyBool("Sprite'larni pikselga", pp.SnapSprites);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Sprite va tilemap'lar ham butun piksellarda chiziladi (transformlari o'zgarmaydi)");
        if (m_Context)
        {
            glm::vec2 viewport = m_Context->GetViewportSize();
            if (viewport.x > 0.0f && viewport.y > 0.0f)
            {
                PixelPerfectView v = PixelPerfect::Compute(pp, (uint32_t)viewport.x, (uint32_t)viewport.y);
                UI::PropertyLabel("");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  %.0fx%.0f oynada: x%d, %ux%u art piksel, %.2f x %.2f birlik", viewport.x, viewport.y,
                                    v.Zoom, (unsigned)((float)v.ViewSize.x / (float)v.Zoom), (unsigned)((float)v.ViewSize.y / (float)v.Zoom),
                                    v.VisibleWorldSize.x, v.VisibleWorldSize.y);
                ImGui::PopTextWrapPos();
            }
        }
        return changed;
    }

    // ------------------------------------------------------------------ tilemap
    static const char* TileToolName(SceneHierarchyPanel::TileTool tool)
    {
        switch (tool)
        {
            case SceneHierarchyPanel::TileTool::Brush: return "Mo'yqalam";
            case SceneHierarchyPanel::TileTool::Rectangle: return "To'rtburchak";
            case SceneHierarchyPanel::TileTool::Fill: return "To'ldirish";
            case SceneHierarchyPanel::TileTool::Eraser: return "O'chirg'ich";
            case SceneHierarchyPanel::TileTool::Picker: return "Tomizg'ich";
        }
        return "";
    }

    void SceneHierarchyPanel::SetTileTool(TileTool tool)
    {
        if (tool == TileTool::Picker && m_TileTool != TileTool::Picker)
            m_ToolBeforePicker = m_TileTool;
        m_TileTool = tool;
        m_TilePaint = true;
    }

    void SceneHierarchyPanel::PickTile(int tile, int rule)
    {
        if (tile < 0)
            return;
        m_TileBrush = TileBrush::Single(tile);
        m_PaletteScrollRequest++;
        m_TileRule = rule;
        if (m_TileTool == TileTool::Picker)
            m_TileTool = m_ToolBeforePicker == TileTool::Picker || m_ToolBeforePicker == TileTool::Eraser ? TileTool::Brush : m_ToolBeforePicker;
    }

    // The 3 x 3 neighbourhood of an auto-tile variant: the cell in the middle, its neighbours of the
    // same terrain filled.
    static void DrawAutoTileMask(ImDrawList* draw, ImVec2 min, float size, AutoTileMode mode, int variant)
    {
        const float cell = size / 3.0f;
        const ImU32 on = UI::AccentU32(0.95f), off = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f)), center = IM_COL32(240, 240, 240, 255);
        uint8_t mask8 = 0;
        if (mode == AutoTileMode::Blob8)
            mask8 = AutoTile::BlobMask(variant);
        else
            mask8 = (uint8_t)(((variant & AutoTile::North) ? AutoTile::N : 0) | ((variant & AutoTile::West) ? AutoTile::W : 0) |
                              ((variant & AutoTile::East) ? AutoTile::E : 0) | ((variant & AutoTile::South) ? AutoTile::S : 0));
        // Rows top to bottom: NW N NE / W . E / SW S SE
        const uint8_t bits[9] = { AutoTile::NW, AutoTile::N, AutoTile::NE, AutoTile::W, 0, AutoTile::E, AutoTile::SW, AutoTile::S, AutoTile::SE };
        for (int i = 0; i < 9; i++)
        {
            ImVec2 a(min.x + (i % 3) * cell + 1.0f, min.y + (i / 3) * cell + 1.0f), b(a.x + cell - 2.0f, a.y + cell - 2.0f);
            bool corner = i == 0 || i == 2 || i == 6 || i == 8;
            if (i == 4)
                draw->AddRectFilled(a, b, center, 1.0f);
            else if (mode == AutoTileMode::Edges4 && corner)
                continue;
            else
                draw->AddRectFilled(a, b, (mask8 & bits[i]) ? on : off, 1.0f);
        }
    }

    bool SceneHierarchyPanel::SaveAutoTiles(const std::string& tilesetPath, const AutoTileSet& set)
    {
        AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
        const AssetMetadata* meta = registry ? registry->Find(std::filesystem::path(tilesetPath)) : nullptr;
        if (!meta)
        {
            ZM_ERROR("Auto-tile qoidalari saqlanmadi: '{}' asset registry'da topilmadi", tilesetPath);
            return false;
        }
        AssetImportSettings import = meta->Import; // the texture's own settings and its sprites stay
        set.WriteTo(import);
        if (!registry->SetImportSettings(meta->Handle, import))
        {
            ZM_ERROR("Auto-tile qoidalari saqlanmadi: {}", tilesetPath);
            return false;
        }
        return true;
    }

    bool SceneHierarchyPanel::DrawTilemap(Entity entity, TilemapComponent& c)
    {
        bool changed = false;
        // Another tilemap: its own tileset's rules, from its first layer (the paint tool stays).
        if ((uint64_t)entity.GetUUID() != m_TileEntity)
        {
            if (m_TileEntity)
            {
                m_TileRule = -1;
                m_EditRule = false;
                m_EditRuleVariant = -1;
                m_TileLayer = std::max(0, c.FindLayer("Asosiy"));
            }
            m_TileEntity = (uint64_t)entity.GetUUID();
        }
        if (UI::PropertyAssetPath("Tileset", c.TilesetPath, ".png;.jpg;.jpeg", ICON_FA_FILE_IMAGE)) { c.Tileset = nullptr; changed = true; m_TileRule = -1; }
        if (UI::PropertyBool("Pixel-art", c.PixelArt)) { c.Tileset = nullptr; changed = true; }
        changed |= UI::PropertyInt("Tileset ustunlari", c.TilesetColumns);
        changed |= UI::PropertyInt("Tileset qatorlari", c.TilesetRows);
        c.TilesetColumns = std::max(1, c.TilesetColumns); c.TilesetRows = std::max(1, c.TilesetRows);
        int w = c.Width, h = c.Height;
        bool resized = UI::PropertyInt("Kenglik (plitka)", w);
        resized |= UI::PropertyInt("Balandlik (plitka)", h);
        if (resized) { c.Resize(std::clamp(w, 1, 1024), std::clamp(h, 1, 1024)); changed = true; }
        changed |= UI::PropertyFloat("Plitka o'lchami", c.TileSize, 0.01f, 0.01f, 100.0f);
        changed |= UI::PropertyColor4("Rang", c.Color);
        changed |= DrawSortingLayer(c.SortingLayer, c.SortingOrder);
        changed |= UI::PropertyBool(ICON_FA_FIRE "  Yorug'lik (Lit)", c.Lit);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Plitkalar Light 2D yorug'liklari va 2D muhit yorug'ligi bilan yoritiladi (g'orlar, tungi sahnalar)");
        changed |= UI::PropertyBool("Collision (fizika)", c.Collision);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Collision belgilangan qatlamlardagi plitkalar qattiq (qo'shni plitkalar bitta to'rtburchakka birlashadi)");
        InactiveComponentNote(entity, c.Collision ? "Obyekt nofaol: tilemap chizilmaydi, kolliziyasi o'chirilgan (faollashganda qayta quriladi)"
                                                  : "Obyekt nofaol: tilemap chizilmaydi");
        {
            PhysicsMaterialData own;
            own.Friction = c.Friction;
            own.Restitution = c.Restitution;
            own.RestitutionThreshold = 1.0f;
            own.Density = 0.0f;
            bool material = false;
            changed |= DrawPhysicsMaterialField(entity, c.MaterialPath, own, material);
            ImGui::BeginDisabled(material);
            changed |= UI::PropertySlider("Ishqalanish", c.Friction, 0.0f, 1.0f);
            changed |= UI::PropertySlider("Sakrash", c.Restitution, 0.0f, 1.0f);
            ImGui::EndDisabled();
            changed |= DrawCollisionLayer("Qatlam", c.CollisionLayer, false,
                                          "Plitkalarning to'qnashuv qatlami (Loyiha sozlamalari > Fizika qatlamlari)");
        }
        c.EnsureSize();

        // ---- layers
        m_TileLayer = std::clamp(m_TileLayer, 0, c.GetLayerCount() - 1);
        UI::PropertyHeader(ICON_FA_LAYER_GROUP "  Qatlamlar", "Har bir qatlamning o'z plitkalari bor. Ro'yxat tartibida chiziladi (birinchisi "
                                                             "orqada); bo'yash faol (belgilangan) qatlamga tushadi");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetColumnIndex(1);
        {
            const float rowHeight = ImGui::GetFrameHeight();
            const float listHeight = std::min(6, c.GetLayerCount()) * (rowHeight + ImGui::GetStyle().ItemSpacing.y) +
                                     ImGui::GetStyle().WindowPadding.y * 2.0f;
            ImGui::BeginChild("##tileLayers", ImVec2(-FLT_MIN, listHeight), ImGuiChildFlags_Border);
            // Top of the list = the layer drawn last (in front), like image editors.
            for (int i = c.GetLayerCount() - 1; i >= 0; i--)
            {
                TilemapLayer& layer = c.Layers[(size_t)i];
                ImGui::PushID(i);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, ImGui::GetStyle().FramePadding.y));
                if (UI::ToolbarButton(layer.Visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH, layer.Visible ? "Ko'rinadi (yashirish)" : "Yashirin (ko'rsatish)"))
                {
                    layer.Visible = !layer.Visible;
                    changed = true;
                }
                ImGui::SameLine(0, 2);
                ImGui::PushStyleColor(ImGuiCol_Text, layer.Collision ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (UI::ToolbarButton(ICON_FA_VECTOR_SQUARE, layer.Collision ? "Collision: plitkalari qattiq (o'chirish)" : "Collision yo'q (yoqish)"))
                {
                    layer.Collision = !layer.Collision;
                    changed = true;
                }
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
                ImGui::SameLine();
                std::string label = std::format("{}##layer", layer.Name);
                if (ImGui::Selectable(label.c_str(), m_TileLayer == i, ImGuiSelectableFlags_None, ImVec2(0, rowHeight)))
                    m_TileLayer = i;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip(std::format("{}: {} ta plitka{}{}", layer.Name, c.CountTiles(i), layer.Collision ? ", qattiq" : "",
                                            layer.SortingOrder ? std::format(", tartib {:+}", layer.SortingOrder) : std::string()).c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        UI::PropertyLabel("");
        {
            ImGui::BeginDisabled(c.GetLayerCount() >= TilemapComponent::MaxLayers);
            if (UI::ToolbarButton(ICON_FA_PLUS, "Yangi qatlam (faol qatlam ustiga)"))
            {
                int added = c.AddLayer("Qatlam");
                if (added >= 0)
                {
                    // Right above the active one.
                    int to = std::min(m_TileLayer + 1, c.GetLayerCount() - 1);
                    c.MoveLayer(added, to);
                    m_TileLayer = to;
                    changed = true;
                }
            }
            ImGui::SameLine(0, 2);
            if (UI::ToolbarButton(ICON_FA_COPY, "Faol qatlamdan nusxa"))
            {
                TilemapLayer copy = c.Layers[(size_t)m_TileLayer];
                int added = c.AddLayer(copy.Name);
                if (added >= 0)
                {
                    std::string name = c.Layers[(size_t)added].Name;
                    c.Layers[(size_t)added] = std::move(copy);
                    c.Layers[(size_t)added].Name = name;
                    int to = std::min(m_TileLayer + 1, c.GetLayerCount() - 1);
                    c.MoveLayer(added, to);
                    m_TileLayer = to;
                    changed = true;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(m_TileLayer >= c.GetLayerCount() - 1);
            if (UI::ToolbarButton(ICON_FA_ARROW_UP, "Yuqoriga (oldinroq chiziladi)") && c.MoveLayer(m_TileLayer, m_TileLayer + 1))
            {
                m_TileLayer++;
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(m_TileLayer <= 0);
            if (UI::ToolbarButton(ICON_FA_ARROW_DOWN, "Pastga (orqaroq chiziladi)") && c.MoveLayer(m_TileLayer, m_TileLayer - 1))
            {
                m_TileLayer--;
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(c.GetLayerCount() <= 1);
            if (UI::ToolbarButton(ICON_FA_TRASH, "Faol qatlamni o'chirish (plitkalari bilan)") && c.RemoveLayer(m_TileLayer))
            {
                m_TileLayer = std::clamp(m_TileLayer, 0, c.GetLayerCount() - 1);
                changed = true;
            }
            ImGui::EndDisabled();
        }
        {
            TilemapLayer& layer = c.Layers[(size_t)m_TileLayer];
            UI::PropertyLabel("Nomi");
            std::string name = layer.Name;
            if (ImGui::InputText("##layerName", &name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) || ImGui::IsItemDeactivatedAfterEdit())
            {
                int other = c.FindLayer(name);
                if (!name.empty() && (other < 0 || other == m_TileLayer) && name != layer.Name)
                {
                    layer.Name = name;
                    changed = true;
                }
            }
            UI::PropertyLabel("Shaffoflik", "Qatlam plitkalarining ko'rinishi (tilemap rangining alfasiga ko'paytiriladi)");
            if (ImGui::SliderFloat("##layerOpacity", &layer.Opacity, 0.0f, 1.0f, "%.2f"))
            {
                layer.Opacity = std::clamp(layer.Opacity, 0.0f, 1.0f);
                changed = true;
            }
            UI::PropertyLabel("Tartib (+)", "Tilemap tartibiga qo'shiladi: masalan +2 - sprite'lar (0) oldida chiziladigan old plan qatlami");
            changed |= ImGui::DragInt("##layerOrder", &layer.SortingOrder, 0.1f, -1000, 1000);
            changed |= UI::PropertyBool("Ko'rinadi", layer.Visible);
            changed |= UI::PropertyBool("Collision", layer.Collision);
            UI::PropertyLabel("Plitkalar");
            ImGui::TextDisabled("%zu / %d", c.CountTiles(m_TileLayer), c.Width * c.Height);
            if (layer.Collision && !c.Collision)
            {
                UI::PropertyLabel("");
                ImGui::TextDisabled("Tilemap'ning Collision'i o'chiq");
            }
        }

        // ---- paint tools
        UI::PropertyHeader(ICON_FA_PAINTBRUSH "  Bo'yash", "Viewport'da sichqoncha bilan. B - mo'yqalam, R - to'rtburchak, G - to'ldirish, "
                                                         "E - o'chirg'ich, I - tomizg'ich, Esc - chiqish. Shift bosilsa o'chiradi. Ctrl+Z - bekor qilish");
        UI::PropertyLabel("Rejim");
        ImGui::Checkbox(Tr(ICON_FA_PEN "  Viewport'da chizish"), &m_TilePaint);
        UI::PropertyLabel("Asbob");
        {
            struct ToolButton { TileTool Tool; const char* Icon; const char* Tip; };
            static const ToolButton s_Tools[] = {
                { TileTool::Brush, ICON_FA_PAINTBRUSH, "Mo'yqalam (B): bosib sudrang. Shift - o'chiradi" },
                { TileTool::Rectangle, ICON_FA_VECTOR_SQUARE, "To'rtburchak (R): burchakdan burchakka sudrang. Shift - o'chiradi" },
                { TileTool::Fill, ICON_FA_FILL_DRIP, "To'ldirish (G): bir xil qo'shni plitkalar sohasini to'ldiradi. Shift - sohani o'chiradi" },
                { TileTool::Eraser, ICON_FA_ERASER, "O'chirg'ich (E)" },
                { TileTool::Picker, ICON_FA_EYEDROPPER, "Tomizg'ich (I): xaritadagi plitkani tanlaydi" },
            };
            for (size_t i = 0; i < std::size(s_Tools); i++)
            {
                if (i) ImGui::SameLine(0, 2);
                if (UI::ToolbarButton(s_Tools[i].Icon, s_Tools[i].Tip, m_TilePaint && m_TileTool == s_Tools[i].Tool))
                    SetTileTool(s_Tools[i].Tool);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", TileToolName(m_TileTool));
        }
        UI::PropertyLabel("");
        if (ImGui::Button(Tr(ICON_FA_ERASER "  Qatlamni tozalash")))
        {
            TilemapLayer& layer = c.Layers[(size_t)m_TileLayer];
            if (std::any_of(layer.Tiles.begin(), layer.Tiles.end(), [](int t) { return t >= 0; }))
            {
                std::fill(layer.Tiles.begin(), layer.Tiles.end(), -1);
                changed = true;
            }
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Faol qatlamning hamma plitkalarini o'chiradi (Ctrl+Z bilan qaytadi)");

        // ---- auto-tiles (rules of the tileset, kept in its .zmeta)
        const AutoTileSet* stored = AutoTileSet::Get(c.TilesetPath);
        AutoTileSet rules = stored ? *stored : AutoTileSet();
        if (m_TileRule >= (int)rules.Rules.size())
            m_TileRule = -1;
        bool rulesEdited = false;
        UI::PropertyHeader(ICON_FA_BORDER_ALL "  Auto-tile", "Qoida tanlansa, asboblar plitka o'rniga shu yerni (terrain) chizadi: har bir katak "
                                                            "qo'shnilariga qarab to'g'ri plitkani oladi, qo'shnilari ham yangilanadi. Qoidalar "
                                                            "tileset'ning .zmeta faylida saqlanadi (shu tileset'li hamma tilemap'larda)");
        UI::PropertyLabel("Qoida");
        {
            std::string preview = m_TileRule >= 0 ? std::string(ICON_FA_BORDER_ALL "  ") + rules.Rules[(size_t)m_TileRule].Name : std::string("(oddiy plitka)");
            if (ImGui::BeginCombo("##rule", preview.c_str()))
            {
                if (ImGui::Selectable(Tr("(oddiy plitka)"), m_TileRule < 0))
                    m_TileRule = -1;
                for (int i = 0; i < (int)rules.Rules.size(); i++)
                {
                    const AutoTileRule& r = rules.Rules[(size_t)i];
                    std::string item = std::format(ICON_FA_BORDER_ALL "  {}  ({})##{}", r.Name, r.Mode == AutoTileMode::Blob8 ? "8 qo'shni, 47" : "4 qo'shni, 16", i);
                    if (ImGui::Selectable(item.c_str(), m_TileRule == i))
                    {
                        m_TileRule = i;
                        if (m_TileTool == TileTool::Picker) m_TileTool = TileTool::Brush;
                        m_TilePaint = true;
                    }
                }
                ImGui::EndCombo();
            }
            UI::PropertyLabel("");
            ImGui::BeginDisabled(c.TilesetPath.empty() || !m_SceneEditable);
            if (UI::ToolbarButton(ICON_FA_PLUS, "Yangi qoida (tanlangan plitkadan boshlab ketma-ket)"))
            {
                AutoTileRule rule = AutoTileRule::Sequential(rules.MakeUniqueName("Yer"), AutoTileMode::Edges4, std::max(0, GetSelectedTile()));
                rules.Rules.push_back(std::move(rule));
                m_TileRule = (int)rules.Rules.size() - 1;
                m_EditRule = true;
                m_EditRuleVariant = -1;
                rulesEdited = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(m_TileRule < 0);
            if (UI::ToolbarButton(ICON_FA_PEN, "Qoidani tahrirlash", m_EditRule && m_TileRule >= 0))
            {
                m_EditRule = !m_EditRule;
                m_EditRuleVariant = -1;
            }
            ImGui::SameLine(0, 2);
            if (UI::ToolbarButton(ICON_FA_TRASH, "Qoidani o'chirish (xaritadagi plitkalar qoladi)") && m_TileRule >= 0)
            {
                rules.Rules.erase(rules.Rules.begin() + m_TileRule);
                m_TileRule = -1;
                m_EditRule = false;
                rulesEdited = true;
            }
            ImGui::EndDisabled();
        }
        if (c.TilesetPath.empty())
        {
            UI::PropertyLabel("");
            ImGui::TextDisabled("Avval tileset tanlang");
        }
        if (m_TileRule >= 0 && m_EditRule)
        {
            AutoTileRule& rule = rules.Rules[(size_t)m_TileRule];
            UI::PropertyLabel("Nomi");
            std::string name = rule.Name;
            if (ImGui::InputText("##ruleName", &name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) || ImGui::IsItemDeactivatedAfterEdit())
                if (!name.empty() && name != rule.Name && rules.Find(name) < 0)
                {
                    rule.Name = name;
                    rulesEdited = true;
                }
            const char* modes[] = { "4 qo'shni (16 plitka)", "8 qo'shni, blob (47 plitka)" };
            int mode = (int)rule.Mode;
            if (UI::PropertyCombo("Turi", mode, modes, 2))
            {
                // A new layout: the tiles from the first one on, in order.
                int first = rule.HasTiles() ? *std::min_element(rule.Tiles.begin(), rule.Tiles.end(), [](int a, int b) {
                    return (a < 0 ? INT32_MAX : a) < (b < 0 ? INT32_MAX : b); }) : 0;
                rule = AutoTileRule::Sequential(rule.Name, (AutoTileMode)mode, first);
                m_EditRuleVariant = -1;
                rulesEdited = true;
            }
            if (UI::PropertyBool("Chegaradan tashqarisi ham", rule.ConnectOutside))
                rulesEdited = true;
            if (ImGui::IsItemHovered())
                UI::Tooltip("Xarita chegarasi tashqarisi shu yer deb hisoblanadi (chetlarida qirra chizilmaydi)");
            UI::PropertyLabel("Ketma-ket", "Hamma variantlarga shu plitkadan boshlab ketma-ket plitkalarni beradi (tileset shu tartibda chizilgan bo'lsa)");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
            ImGui::DragInt("##seqFirst", &m_RuleFirstTile, 0.2f, 0, std::max(0, c.TilesetColumns * c.TilesetRows - 1));
            ImGui::SameLine();
            if (ImGui::Button(Tr("Qo'llash")))
            {
                rule = AutoTileRule::Sequential(rule.Name, rule.Mode, m_RuleFirstTile);
                rulesEdited = true;
            }

            // The variants: the neighbour pattern and its tile. Click one, then a tile in the palette.
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Variantlar");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Variantni bosing, keyin palitradan plitkani tanlang (keyingi variantga o'tadi). O'ng tugma - tozalash");
            ImGui::TableSetColumnIndex(1);
            if (!c.Tileset && !c.TilesetPath.empty())
                c.Tileset = TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TilesetPath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
            const float thumb = std::max(14.0f, ImGui::GetFontSize() * 1.1f);
            const float cellWidth = thumb * 2.0f + 6.0f;
            const int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 4.0f) / (cellWidth + 4.0f)));
            const int cols = std::max(1, c.TilesetColumns), rows = std::max(1, c.TilesetRows);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (int v = 0; v < rule.GetVariantCount(); v++)
            {
                if (v % perRow) ImGui::SameLine(0, 4);
                ImGui::PushID(v);
                ImVec2 p = ImGui::GetCursorScreenPos();
                bool clicked = ImGui::InvisibleButton("##variant", ImVec2(cellWidth, thumb + 4.0f));
                bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && rule.Tiles[(size_t)v] >= 0)
                {
                    rule.Tiles[(size_t)v] = -1;
                    rulesEdited = true;
                }
                if (clicked)
                    m_EditRuleVariant = m_EditRuleVariant == v ? -1 : v;
                ImU32 frame = m_EditRuleVariant == v ? UI::AccentU32() : hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered) : ImGui::GetColorU32(ImGuiCol_FrameBg);
                draw->AddRectFilled(p, ImVec2(p.x + cellWidth, p.y + thumb + 4.0f), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
                draw->AddRect(p, ImVec2(p.x + cellWidth, p.y + thumb + 4.0f), frame, 3.0f, 0, m_EditRuleVariant == v ? 2.0f : 1.0f);
                DrawAutoTileMask(draw, ImVec2(p.x + 2.0f, p.y + 2.0f), thumb, rule.Mode, v);
                int tile = rule.Tiles[(size_t)v];
                ImVec2 t0(p.x + thumb + 4.0f, p.y + 2.0f), t1(t0.x + thumb, t0.y + thumb);
                if (tile >= 0 && c.Tileset)
                {
                    int tx = tile % cols, ty = tile / cols;
                    ImVec2 uv0((float)tx / cols, 1.0f - (float)ty / rows), uv1((float)(tx + 1) / cols, 1.0f - (float)(ty + 1) / rows);
                    draw->AddImage((ImTextureID)(uintptr_t)c.Tileset->GetRendererID(), t0, t1, uv0, uv1);
                }
                else
                    draw->AddText(ImVec2(t0.x + thumb * 0.3f, t0.y + thumb * 0.15f), ImGui::GetColorU32(ImGuiCol_TextDisabled), tile >= 0 ? "#" : "-");
                if (hovered)
                    UI::Tooltip(tile >= 0 ? std::format("Variant {}: plitka {}", v, tile).c_str() : std::format("Variant {}: plitka yo'q", v).c_str());
                ImGui::PopID();
            }
        }

        // ---- palette + the selected tile's properties (both kept in the tileset's .zmeta)
        const TilesetProperties* storedProperties = TilesetProperties::Get(c.TilesetPath);
        TilesetProperties properties = storedProperties ? *storedProperties : TilesetProperties();
        bool propertiesEdited = false;
        UI::PropertyHeader(ICON_FA_TABLE_CELLS "  Palitra", "G'ildirak - kattalashtirish / kichraytirish (sichqoncha ostida), o'rta yoki o'ng "
                                                          "tugma bilan sudrash - surish. Bosish - plitka, sudrash - bir nechta plitkali "
                                                          "mo'yqalam. Pastki chetini tortib balandligini o'zgartiring");
        // The palette takes the whole width of the inspector: the property grid pauses around it.
        UI::EndPropertyGrid();
        m_PaletteViews[0].Resizable = true;
        DrawTilePalette(c, m_PaletteViews[0], std::max(120.0f, ImGui::GetFontSize() * 16.0f), rules, rulesEdited, properties, propertiesEdited);
        UI::BeginPropertyGrid("##tilemapAfterPalette"); // never fails here: the window is drawing (its section's grid did not)
        DrawTileProperties(c, properties, propertiesEdited);

        if (rulesEdited && !c.TilesetPath.empty())
            SaveAutoTiles(c.TilesetPath, rules);
        if (propertiesEdited && !c.TilesetPath.empty())
            SaveTileProperties(c.TilesetPath, properties);
        return changed;
    }

    bool SceneHierarchyPanel::SaveTileProperties(const std::string& tilesetPath, const TilesetProperties& properties)
    {
        AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
        const AssetMetadata* meta = registry ? registry->Find(std::filesystem::path(tilesetPath)) : nullptr;
        if (!meta)
        {
            ZM_ERROR("Plitka xususiyatlari saqlanmadi: '{}' asset registry'da topilmadi", tilesetPath);
            return false;
        }
        AssetImportSettings import = meta->Import; // sprites, auto-tile rules and texture settings stay
        properties.WriteTo(import);
        if (!registry->SetImportSettings(meta->Handle, import))
        {
            ZM_ERROR("Plitka xususiyatlari saqlanmadi: {}", tilesetPath);
            return false;
        }
        return true;
    }

    void SceneHierarchyPanel::SetTileBrush(const TileBrush& brush)
    {
        if (brush.Width < 1 || brush.Height < 1 || (int)brush.Tiles.size() != brush.Width * brush.Height)
            return;
        m_TileBrush = brush;
        m_PaletteScrollRequest++;
    }

    // Tile `index` of a cols x rows tileset as UVs of its (bottom-up) texture.
    static void TileUV(int index, int cols, int rows, ImVec2& uv0, ImVec2& uv1)
    {
        int tx = index % cols, ty = index / cols;
        uv0 = ImVec2((float)tx / cols, 1.0f - (float)ty / rows);
        uv1 = ImVec2((float)(tx + 1) / cols, 1.0f - (float)(ty + 1) / rows);
    }

    static const char* TileCollisionLabel(TileCollision collision)
    {
        switch (collision)
        {
            case TileCollision::Solid: return "Qattiq";
            case TileCollision::None: return "Yo'q (o'tib ketiladi)";
            case TileCollision::OneWay: return "Bir tomonlama (platforma)";
        }
        return "";
    }

    void SceneHierarchyPanel::DrawTilePalette(TilemapComponent& c, TilePaletteView& view, float height, AutoTileSet& rules, bool& rulesEdited,
                                              TilesetProperties& props, bool& propsEdited)
    {
        if (!c.Tileset && !c.TilesetPath.empty())
            c.Tileset = TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TilesetPath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
        if (!c.Tileset)
        {
            ImGui::TextDisabled("Tileset teksturasini tanlang");
            return;
        }
        const int cols = std::max(1, c.TilesetColumns), rows = std::max(1, c.TilesetRows), count = cols * rows;
        const float texW = (float)std::max(1u, c.Tileset->GetWidth()), texH = (float)std::max(1u, c.Tileset->GetHeight());
        const ImTextureID texture = (ImTextureID)(uintptr_t)c.Tileset->GetRendererID();
        AutoTileRule* activeRule = m_TileRule >= 0 && m_TileRule < (int)rules.Rules.size() ? &rules.Rules[(size_t)m_TileRule] : nullptr;
        const bool assigning = activeRule && m_EditRule && m_EditRuleVariant >= 0;
        const bool addingFrames = !assigning && m_AnimFramesTile >= 0;
        if (view.Tileset != c.TilesetPath)
        {
            if (!view.Tileset.empty())
                view.AutoFit = true; // another tileset: fitted afresh
            view.Tileset = c.TilesetPath;
            view.DragStart = view.DragEnd = -1;
        }
        ImGui::PushID(&view);

        // ---- toolbar: zoom, the property marks, the brush
        // Fitted: the inspector's palette to its width (its height is dragged by the user), the
        // window's to the whole area.
        const ImGuiStyle& style = ImGui::GetStyle();
        float fitZoom = (ImGui::GetContentRegionAvail().x - style.ScrollbarSize - style.WindowPadding.x * 2.0f - 2.0f) / texW;
        if (height <= 0.0f)
        {
            float areaH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * (assigning || addingFrames ? 2.0f : 1.0f) -
                          style.WindowPadding.y * 2.0f - 4.0f;
            fitZoom = std::min(fitZoom, areaH / texH);
        }
        fitZoom = std::clamp(fitZoom, 0.25f, 32.0f);
        if (view.AutoFit || view.Zoom <= 0.0f)
        {
            view.Zoom = fitZoom;
            view.AutoFit = true;
        }
        if (UI::ToolbarButton(ICON_FA_MAGNIFYING_GLASS_MINUS, "Kichraytirish (g'ildirak)"))
        {
            view.Zoom = std::max(0.25f, view.Zoom / 1.25f);
            view.AutoFit = false;
        }
        ImGui::SameLine(0, 2);
        if (UI::ToolbarButton(ICON_FA_MAGNIFYING_GLASS_PLUS, "Kattalashtirish (g'ildirak)"))
        {
            view.Zoom = std::min(32.0f, view.Zoom * 1.25f);
            view.AutoFit = false;
        }
        ImGui::SameLine(0, 2);
        if (UI::ToolbarButton(ICON_FA_EXPAND, height > 0.0f ? "Kenglikka moslash" : "Oynaga moslash", view.AutoFit))
            view.AutoFit = true;
        ImGui::SameLine(0, 2);
        if (UI::ToolbarButton(ICON_FA_TAGS, "Plitka xususiyatlari belgilari: qizil x - collision yo'q, pushti - bir tomonlama, "
                                            "uchburchak - animatsiya", m_ShowTileProperties))
            m_ShowTileProperties = !m_ShowTileProperties;
        if (view.Resizable && OnTilePaletteWindowRequested)
        {
            ImGui::SameLine(0, 2);
            if (UI::ToolbarButton(ICON_FA_UP_RIGHT_FROM_SQUARE, "Alohida oynada ochish (Oyna > Plitka palitrasi)"))
                OnTilePaletteWindowRequested();
        }
        ImGui::SameLine();
        if (m_TileBrush.IsSingle() || activeRule)
            ImGui::TextDisabled("%.0f%%", view.Zoom * 100.0f);
        else
            ImGui::TextDisabled("%.0f%%  " ICON_FA_PAINTBRUSH " %d x %d", view.Zoom * 100.0f, m_TileBrush.Width, m_TileBrush.Height);
        if (assigning)
            ImGui::TextColored(UI::AccentColor(), ICON_FA_HAND_POINTER "  Variant %d uchun plitkani bosing", m_EditRuleVariant);
        else if (addingFrames)
            ImGui::TextColored(UI::AccentColor(), ICON_FA_HAND_POINTER "  Plitka %d animatsiyasiga kadr: plitkani bosing", m_AnimFramesTile);

        // ---- the scrolling area
        ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | (view.Resizable ? ImGuiChildFlags_ResizeY : 0);
        float areaHeight = height > 0.0f ? height : 0.0f; // the window: the rest of it
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(24, 26, 30, 255));
        ImGui::BeginChild("##paletteArea", ImVec2(-FLT_MIN, areaHeight), childFlags, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PopStyleColor();
        ImGuiIO& io = ImGui::GetIO();
        // Zoom around the cursor before the tiles are laid out (the content takes the new size at once).
        if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f)
        {
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImVec2 local(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
            float old = view.Zoom;
            view.Zoom = std::clamp(view.Zoom * std::pow(1.2f, io.MouseWheel), 0.25f, 32.0f);
            view.AutoFit = false;
            float k = view.Zoom / old;
            ImGui::SetScrollX(ImGui::GetScrollX() + local.x * (k - 1.0f));
            ImGui::SetScrollY(ImGui::GetScrollY() + local.y * (k - 1.0f));
        }
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(texW * view.Zoom, texH * view.Zoom);
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        ImGui::InvisibleButton("##tiles", ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // the wheel zooms here, it does not scroll the inspector
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)))
        {
            ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
        }
        const float cw = size.x / cols, ch = size.y / rows;
        auto tileAt = [&](ImVec2 p, bool clampToSet) {
            int tx = (int)std::floor((p.x - origin.x) / cw), ty = (int)std::floor((p.y - origin.y) / ch);
            if (clampToSet)
            {
                tx = std::clamp(tx, 0, cols - 1);
                ty = std::clamp(ty, 0, rows - 1);
            }
            else if (tx < 0 || ty < 0 || tx >= cols || ty >= rows)
                return -1;
            return ty * cols + tx;
        };
        auto tileMin = [&](int index) { return ImVec2(origin.x + (index % cols) * cw, origin.y + (index / cols) * ch); };
        // The brush was picked elsewhere (the picker, automation): bring it into view.
        if (view.ScrolledTo != m_PaletteScrollRequest)
        {
            view.ScrolledTo = m_PaletteScrollRequest;
            int topLeft = m_TileBrush.GetTopLeft();
            if (topLeft >= 0 && topLeft < count && !activeRule)
            {
                float localY = (topLeft / cols) * ch, localX = (topLeft % cols) * cw;
                float viewH = ImGui::GetWindowHeight(), viewW = ImGui::GetWindowWidth();
                if (localY < ImGui::GetScrollY() || localY + ch * m_TileBrush.Height > ImGui::GetScrollY() + viewH)
                    ImGui::SetScrollY(std::max(0.0f, localY + ch * m_TileBrush.Height * 0.5f - viewH * 0.5f));
                if (localX < ImGui::GetScrollX() || localX + cw * m_TileBrush.Width > ImGui::GetScrollX() + viewW)
                    ImGui::SetScrollX(std::max(0.0f, localX + cw * m_TileBrush.Width * 0.5f - viewW * 0.5f));
            }
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        {
            // A checkerboard under the transparent parts (only where it can be seen).
            ImVec2 clipMin = dl->GetClipRectMin(), clipMax = dl->GetClipRectMax();
            ImVec2 a(std::max(origin.x, clipMin.x), std::max(origin.y, clipMin.y)), b(std::min(end.x, clipMax.x), std::min(end.y, clipMax.y));
            if (b.x > a.x && b.y > a.y)
            {
                dl->AddRectFilled(a, b, IM_COL32(56, 58, 64, 255));
                const float cell = 8.0f;
                int x0 = (int)std::floor((a.x - origin.x) / cell), y0 = (int)std::floor((a.y - origin.y) / cell);
                for (int y = y0; origin.y + y * cell < b.y; y++)
                    for (int x = x0 + ((x0 + y) & 1); origin.x + x * cell < b.x; x += 2)
                    {
                        ImVec2 c0(std::max(a.x, origin.x + x * cell), std::max(a.y, origin.y + y * cell));
                        ImVec2 c1(std::min(b.x, origin.x + (x + 1) * cell), std::min(b.y, origin.y + (y + 1) * cell));
                        if (c1.x > c0.x && c1.y > c0.y)
                            dl->AddRectFilled(c0, c1, IM_COL32(76, 78, 86, 255));
                    }
            }
        }
        dl->AddImage(texture, origin, end, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        if (cw >= 10.0f && ch >= 10.0f)
        {
            const ImU32 line = IM_COL32(0, 0, 0, 70);
            for (int x = 1; x < cols; x++)
                dl->AddLine(ImVec2(origin.x + x * cw, origin.y), ImVec2(origin.x + x * cw, end.y), line);
            for (int y = 1; y < rows; y++)
                dl->AddLine(ImVec2(origin.x, origin.y + y * ch), ImVec2(end.x, origin.y + y * ch), line);
        }
        // Collision / animation marks.
        if (m_ShowTileProperties && !props.IsEmpty())
        {
            const ImU32 red = IM_COL32(255, 90, 90, 235), oneWay = IM_COL32(242, 102, 230, 245), play = IM_COL32(120, 210, 255, 240);
            const float m = std::max(3.0f, std::min(cw, ch) * 0.22f);
            for (int t : props.GetTilesWith(TileCollision::None))
            {
                if (t >= count) break;
                ImVec2 a = tileMin(t);
                ImVec2 p0(a.x + cw - m - 2.0f, a.y + 2.0f), p1(a.x + cw - 2.0f, a.y + m + 2.0f);
                dl->AddLine(p0, p1, red, 2.0f);
                dl->AddLine(ImVec2(p1.x, p0.y), ImVec2(p0.x, p1.y), red, 2.0f);
            }
            for (int t : props.GetTilesWith(TileCollision::OneWay))
            {
                if (t >= count) break;
                ImVec2 a = tileMin(t);
                dl->AddRectFilled(a, ImVec2(a.x + cw, a.y + std::max(2.0f, ch * 0.08f)), oneWay);
                float cx = a.x + cw * 0.5f, top = a.y + std::max(3.0f, ch * 0.12f);
                dl->AddTriangleFilled(ImVec2(cx, top), ImVec2(cx - m * 0.6f, top + m * 0.7f), ImVec2(cx + m * 0.6f, top + m * 0.7f), oneWay);
            }
            for (const TileAnimation& anim : props.GetAnimations())
            {
                if (anim.Tile >= count) break;
                ImVec2 a = tileMin(anim.Tile);
                ImVec2 b(a.x + cw - 2.0f, a.y + ch - 2.0f);
                dl->AddTriangleFilled(ImVec2(b.x - m, b.y - m), ImVec2(b.x, b.y - m * 0.5f), ImVec2(b.x - m, b.y), play);
            }
        }
        // The selected rule's tiles; the edited animation's frames (numbered); the brush.
        if (activeRule)
        {
            for (int t = 0; t < count; t++)
                if (activeRule->Contains(t))
                {
                    ImVec2 a = tileMin(t);
                    dl->AddRect(a, ImVec2(a.x + cw, a.y + ch), UI::AccentU32(0.9f), 0.0f, 0, 2.0f);
                }
        }
        else
        {
            int topLeft = m_TileBrush.GetTopLeft();
            if (topLeft >= 0 && topLeft < count)
            {
                ImVec2 a = tileMin(topLeft);
                ImVec2 b(a.x + cw * m_TileBrush.Width, a.y + ch * m_TileBrush.Height);
                dl->AddRectFilled(a, b, UI::AccentU32(0.16f));
                dl->AddRect(a, b, UI::AccentU32(), 0.0f, 0, 2.0f);
            }
        }
        const int framesOf = addingFrames ? m_AnimFramesTile : m_TilePropertiesOpen ? m_TileBrush.GetTopLeft() : -1;
        if (const TileAnimation* anim = framesOf >= 0 ? props.FindAnimation(framesOf) : nullptr)
            for (int k = 0; k < (int)anim->Frames.size(); k++)
            {
                int f = anim->Frames[(size_t)k];
                if (f < 0 || f >= count) continue;
                ImVec2 a = tileMin(f);
                std::string label = std::to_string(k + 1);
                ImVec2 ts = ImGui::CalcTextSize(label.c_str());
                ImVec2 p(a.x + 2.0f, a.y + 2.0f);
                dl->AddRectFilled(p, ImVec2(p.x + ts.x + 4.0f, p.y + ts.y), IM_COL32(20, 90, 140, 220), 2.0f);
                dl->AddText(ImVec2(p.x + 2.0f, p.y), IM_COL32(255, 255, 255, 255), label.c_str());
            }
        if (view.DragStart >= 0)
        {
            TileBrush dragged = TileBrush::FromTileset(cols, rows, view.DragStart, view.DragEnd);
            ImVec2 a = tileMin(dragged.GetTopLeft());
            ImVec2 b(a.x + cw * dragged.Width, a.y + ch * dragged.Height);
            dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 40));
            dl->AddRect(a, b, IM_COL32(255, 255, 255, 230), 0.0f, 0, 2.0f);
        }

        // ---- input
        const int hoveredTile = hovered ? tileAt(io.MousePos, false) : -1;
        if (hoveredTile >= 0)
        {
            ImVec2 a = tileMin(hoveredTile);
            dl->AddRect(a, ImVec2(a.x + cw, a.y + ch), IM_COL32(255, 255, 255, 190), 0.0f, 0, 1.5f);
            if (view.DragStart < 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                std::string tip = std::format("Plitka {}", hoveredTile);
                TileCollision collision = props.GetCollision(hoveredTile);
                if (collision != TileCollision::Solid)
                    tip += std::format("\nCollision: {}", TileCollisionLabel(collision));
                if (const TileAnimation* anim = props.FindAnimation(hoveredTile))
                    tip += std::format("\nAnimatsiya: {} kadr, {:.1f} FPS", anim->Frames.size(), anim->FPS);
                if (int rule = rules.FindRuleOf(hoveredTile); rule >= 0)
                    tip += std::format("\nAuto-tile: {}", rules.Rules[(size_t)rule].Name);
                UI::Tooltip(tip.c_str());
            }
        }
        if (hoveredTile >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (assigning)
            {
                activeRule->Tiles[(size_t)m_EditRuleVariant] = hoveredTile;
                m_EditRuleVariant = m_EditRuleVariant + 1 < activeRule->GetVariantCount() ? m_EditRuleVariant + 1 : -1;
                rulesEdited = true;
            }
            else if (addingFrames)
            {
                const TileAnimation* existing = props.FindAnimation(m_AnimFramesTile);
                TileAnimation anim = existing ? *existing : TileAnimation{ m_AnimFramesTile, {}, 6.0f };
                if ((int)anim.Frames.size() < TileAnimation::MaxFrames)
                {
                    anim.Frames.push_back(hoveredTile);
                    props.SetAnimation(std::move(anim));
                    propsEdited = true;
                }
            }
            else
                view.DragStart = view.DragEnd = hoveredTile;
        }
        if (view.DragStart >= 0)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                view.DragEnd = tileAt(io.MousePos, true);
            else
            {
                // Released: the tile (or the rectangle of tiles) is the brush now (in view already).
                SetTileBrush(TileBrush::FromTileset(cols, rows, view.DragStart, view.DragEnd));
                view.ScrolledTo = m_PaletteScrollRequest;
                view.DragStart = view.DragEnd = -1;
                m_TileRule = -1;
                if (m_TileTool == TileTool::Eraser || m_TileTool == TileTool::Picker)
                    m_TileTool = TileTool::Brush;
                m_TilePaint = true;
            }
        }
        ImGui::EndChild();
        ImGui::PopID();
    }

    void SceneHierarchyPanel::DrawTileProperties(TilemapComponent& c, TilesetProperties& props, bool& propsEdited)
    {
        const int cols = std::max(1, c.TilesetColumns), rows = std::max(1, c.TilesetRows), count = cols * rows;
        const int tile = m_TileBrush.GetTopLeft();
        std::vector<int> selected;
        for (int t : m_TileBrush.Tiles)
            if (t >= 0 && t < count)
                selected.push_back(t);
        UI::PropertyHeader(ICON_FA_TAGS "  Plitka xususiyatlari", "Tileset'ning har bir plitkasi uchun (uning .zmeta faylida, shu tileset'li hamma "
                                                                "tilemap'larda): collision turi va animatsiya");
        UI::PropertyLabel("Ko'rsatish");
        ImGui::Checkbox("##tileProps", &m_TilePropertiesOpen);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Tanlangan plitkaning collision va animatsiya sozlamalari");
        if (!m_TilePropertiesOpen)
        {
            m_AnimFramesTile = -1;
            return;
        }
        if (c.TilesetPath.empty() || tile < 0 || tile >= count || selected.empty())
        {
            UI::PropertyLabel("");
            ImGui::TextDisabled("Palitradan plitka tanlang");
            m_AnimFramesTile = -1;
            return;
        }
        if (m_AnimFramesTile >= 0 && m_AnimFramesTile != tile)
            m_AnimFramesTile = -1; // another tile was picked
        const ImTextureID texture = c.Tileset ? (ImTextureID)(uintptr_t)c.Tileset->GetRendererID() : (ImTextureID)0;
        const float thumb = std::max(18.0f, ImGui::GetFrameHeight());
        auto image = [&](int t, float size) {
            ImVec2 uv0, uv1;
            TileUV(t, cols, rows, uv0, uv1);
            if (texture)
                ImGui::Image(texture, ImVec2(size, size), uv0, uv1);
            else
                ImGui::Dummy(ImVec2(size, size));
        };
        UI::PropertyLabel("Plitka");
        image(tile, thumb);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (selected.size() == 1)
            ImGui::Text("%d", tile);
        else
            ImGui::Text("%d ta (%d x %d)", (int)selected.size(), m_TileBrush.Width, m_TileBrush.Height);

        // ---- collision (every selected tile)
        bool mixed = false;
        TileCollision collision = props.GetCollision(tile);
        for (int t : selected)
            mixed |= props.GetCollision(t) != collision;
        UI::PropertyLabel("Collision", "Collision yoqilgan qatlamlarda: qattiq; yo'q - o't, gul, bezaklar (o'tib ketiladi); bir tomonlama - "
                                       "pastdan sakrab o'tiladi, ustiga qo'niladi (skriptdan DropThroughPlatforms bilan tushib ketiladi)");
        if (ImGui::BeginCombo("##tileCollision", mixed ? "(turlicha)" : TileCollisionLabel(collision)))
        {
            for (TileCollision option : { TileCollision::Solid, TileCollision::None, TileCollision::OneWay })
                if (ImGui::Selectable(TileCollisionLabel(option), !mixed && option == collision))
                {
                    for (int t : selected)
                        props.SetCollision(t, option);
                    propsEdited = true;
                }
            ImGui::EndCombo();
        }

        // ---- animation (of the top-left tile)
        const TileAnimation* anim = props.FindAnimation(tile);
        UI::PropertyLabel("Animatsiya", "Xaritada shu plitka turgan joyda kadrlar navbat bilan ko'rsatiladi (editorda ham). "
                                        "Fizika shu plitkaning collision'idan foydalanadi");
        bool animated = anim != nullptr;
        if (ImGui::Checkbox("##tileAnimated", &animated))
        {
            if (animated)
                props.SetAnimation({ tile, selected.size() > 1 ? selected : std::vector<int>{ tile }, 6.0f });
            else
            {
                props.RemoveAnimation(tile);
                m_AnimFramesTile = -1;
            }
            propsEdited = true;
            anim = props.FindAnimation(tile);
        }
        if (!anim)
            return;
        // A live preview of the animation.
        ImGui::SameLine();
        image(anim->GetFrame(ImGui::GetTime()), thumb);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu kadr, %.2f s", anim->Frames.size(), anim->GetDuration());

        UI::PropertyLabel("FPS", "Sekundiga necha kadr (0 - birinchi kadrda turadi)");
        float fps = anim->FPS;
        if (ImGui::DragFloat("##tileFps", &fps, 0.1f, 0.0f, 60.0f, "%.1f"))
        {
            TileAnimation edited = *anim;
            edited.FPS = std::clamp(fps, 0.0f, 60.0f);
            props.SetAnimation(std::move(edited));
            propsEdited = true;
            anim = props.FindAnimation(tile);
        }
        UI::PropertyLabel("Kadrlar", "Navbat bilan ko'rsatiladigan plitkalar. O'ng tugma - kadrni olib tashlash");
        {
            const float small = std::max(16.0f, ImGui::GetFontSize() * 1.3f);
            const float perRow = std::max(1.0f, std::floor((ImGui::GetContentRegionAvail().x + 3.0f) / (small + 3.0f + ImGui::GetStyle().FramePadding.x * 2.0f)));
            int remove = -1;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));
            for (int k = 0; k < (int)anim->Frames.size(); k++)
            {
                if (k % (int)perRow) ImGui::SameLine();
                ImGui::PushID(k);
                ImVec2 uv0, uv1;
                TileUV(anim->Frames[(size_t)k], cols, rows, uv0, uv1);
                if (texture)
                    ImGui::ImageButton("##frame", texture, ImVec2(small, small), uv0, uv1);
                else
                    ImGui::Button("##frame", ImVec2(small, small));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    remove = k;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip(std::format("Kadr {}: plitka {} (o'ng tugma - olib tashlash)", k + 1, anim->Frames[(size_t)k]).c_str());
                ImGui::PopID();
            }
            ImGui::PopStyleVar(2);
            if (remove >= 0)
            {
                TileAnimation edited = *anim;
                edited.Frames.erase(edited.Frames.begin() + remove);
                if (edited.Frames.empty())
                    edited.Frames.push_back(tile);
                props.SetAnimation(std::move(edited));
                propsEdited = true;
                anim = props.FindAnimation(tile);
            }
        }
        UI::PropertyLabel("");
        if (UI::ToolbarButton(ICON_FA_PLUS, "Palitradan kadr qo'shish: endi bosilgan plitkalar oxiriga qo'shiladi (yana bosing - tugatish)",
                              m_AnimFramesTile == tile))
            m_AnimFramesTile = m_AnimFramesTile == tile ? -1 : tile;
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(selected.size() < 2);
        if (UI::ToolbarButton(ICON_FA_TABLE_CELLS, "Kadrlar = tanlangan plitkalar (palitrada bir nechtasini sudrab tanlang)") && anim)
        {
            TileAnimation edited = *anim;
            edited.Frames = selected;
            props.SetAnimation(std::move(edited));
            propsEdited = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        if (UI::ToolbarButton(ICON_FA_ROTATE_LEFT, "Kadrlarni tozalash (faqat plitkaning o'zi qoladi)") && anim)
        {
            TileAnimation edited = *anim;
            edited.Frames = { tile };
            props.SetAnimation(std::move(edited));
            propsEdited = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d ta animatsiya", (int)props.GetAnimations().size());
    }

    void SceneHierarchyPanel::DrawTilePaletteWindow(bool* open)
    {
        if (!open || !*open)
            return;
        ImGui::SetNextWindowSize(ImVec2(440, 560), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Tr(ICON_FA_TABLE_CELLS "  Plitka palitrasi###TilePalette"), open))
        {
            ImGui::End();
            return;
        }
        Entity e = GetSelectedEntity();
        TilemapComponent* c = e && e.IsValid() ? e.TryGetComponent<TilemapComponent>() : nullptr;
        if (!c)
        {
            ImGui::TextDisabled("Ierarxiyada tilemap tanlang");
            ImGui::End();
            return;
        }
        c->EnsureSize();
        const int layer = std::clamp(m_TileLayer, 0, c->GetLayerCount() - 1);
        ImGui::TextUnformatted(ICON_FA_TABLE_CELLS);
        ImGui::SameLine();
        ImGui::Text("%s", e.GetName().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s  |  qatlam: %s  |  %s", c->TilesetPath.empty() ? "(tileset yo'q)" : c->TilesetPath.c_str(),
                            c->Layers[(size_t)layer].Name.c_str(), m_TilePaint ? TileToolName(m_TileTool) : "bo'yash o'chiq");
        const AutoTileSet* storedRules = AutoTileSet::Get(c->TilesetPath);
        AutoTileSet rules = storedRules ? *storedRules : AutoTileSet();
        const TilesetProperties* storedProperties = TilesetProperties::Get(c->TilesetPath);
        TilesetProperties properties = storedProperties ? *storedProperties : TilesetProperties();
        bool rulesEdited = false, propertiesEdited = false;
        if (m_TileRule >= (int)rules.Rules.size())
            m_TileRule = -1;
        m_PaletteViews[1].Resizable = false;
        DrawTilePalette(*c, m_PaletteViews[1], 0.0f, rules, rulesEdited, properties, propertiesEdited);
        if (rulesEdited && !c->TilesetPath.empty())
            SaveAutoTiles(c->TilesetPath, rules);
        if (propertiesEdited && !c->TilesetPath.empty())
            SaveTileProperties(c->TilesetPath, properties);
        ImGui::End();
    }

    bool SceneHierarchyPanel::DrawJoint2DCommon(Entity entity, Joint2DComponentBase& c, Joint2DType type)
    {
        bool changed = DrawEntityRefField("Ulangan jism", c.ConnectedBody,
                                          "Boshqa obyektning Rigidbody 2D yoki Tilemap'i (ierarxiyadan tortib tashlang). "
                                          "Bo'sh - jism dunyodagi qo'zg'almas nuqtaga bog'lanadi");
        // What keeps the joint from being made (it waits for it while the game runs).
        const char* problem = nullptr;
        Entity other = c.ConnectedBody && m_Context ? m_Context->GetEntityByUUID(c.ConnectedBody.ID) : Entity();
        if (!entity.HasComponent<Rigidbody2DComponent>())
            problem = "Rigidbody 2D kerak: busiz joint yaratilmaydi";
        else if (c.ConnectedBody && !other)
            problem = "Ulangan obyekt sahnada yo'q (o'chirilgan, yoki prefab boshqa sahnada yaratilgan): joint yaratilmaydi";
        else if (other && other == entity)
            problem = "Obyekt o'ziga ulana olmaydi";
        else if (other && !other.HasComponent<Rigidbody2DComponent>() && !other.HasComponent<TilemapComponent>())
            problem = "Ulangan obyektda Rigidbody 2D (yoki Tilemap) yo'q";
        if (problem)
        {
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", problem);
            ImGui::PopTextWrapPos();
        }
        InactiveComponentNote(entity, "Obyekt nofaol: joint yo'q (faollashganda qayta yaratiladi)");

        // The anchors (and the axis / limits) dragged in the viewport.
        const bool editing = m_JointAnchorEdit == (int)type;
        UI::PropertyLabel("Viewportda", "Langarlarni sahnada sudrab qo'yish: langar (doira), ulangan langar (romb), sirpanish / osma o'qi "
                                        "va chegaralari. Shift - mayda qadam (0.1 birlik / 5\xc2\xb0). Esc yoki tugma qayta bosilsa - tugaydi");
        ImGui::BeginDisabled(!m_SceneEditable);
        if (editing)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentColor());
        if (ImGui::Button(editing ? ICON_FA_CHECK "  Tahrirlashni tugatish" : ICON_FA_ANCHOR "  Langarlarni tahrirlash", ImVec2(-FLT_MIN, 0)))
            SetJointAnchorEdit(editing ? -1 : (int)type);
        if (editing)
            ImGui::PopStyleColor();
        ImGui::EndDisabled();

        changed |= UI::PropertyFloat2("Langar", c.Anchor, 0.01f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Shu jismdagi bog'lanish nuqtasi - jismning o'z koordinatalarida (masshtabsiz, collider siljishi kabi)");
        changed |= UI::PropertyBool("Avto ulangan langar", c.AutoConfigureConnectedAnchor);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Ulangan langar joint yaratilganda \"Langar\" turgan nuqtaga qo'yiladi (o'yin davomida shu yerda ko'rinadi)");
        ImGui::BeginDisabled(c.AutoConfigureConnectedAnchor);
        changed |= UI::PropertyFloat2(c.ConnectedBody ? "Ulangan langar" : "Dunyodagi nuqta", c.ConnectedAnchor, 0.01f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            UI::Tooltip(c.ConnectedBody ? "Ulangan jismdagi nuqta, uning o'z koordinatalarida"
                                        : "Ulangan jism yo'q: joint dunyodagi shu nuqtaga bog'lanadi");
        ImGui::EndDisabled();
        if (!c.AutoConfigureConnectedAnchor && m_SceneEditable && m_Context && (!other || other != entity))
        {
            UI::PropertyLabel("");
            if (ImGui::SmallButton(Tr(ICON_FA_LOCATION_CROSSHAIRS "  Langar turgan joyga")))
            {
                // Where this body's anchor is now, in the connected body's frame (or the world).
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(m_Context->GetWorldTransform(entity), p, r, sc);
                const float cs = std::cos(r.z), sn = std::sin(r.z);
                glm::vec2 world(p.x + cs * c.Anchor.x - sn * c.Anchor.y, p.y + sn * c.Anchor.x + cs * c.Anchor.y);
                if (other)
                {
                    glm::vec3 op, orot, osc;
                    Math::DecomposeTransform(m_Context->GetWorldTransform(other), op, orot, osc);
                    const glm::vec2 d = world - glm::vec2(op);
                    const float oc = std::cos(orot.z), os = std::sin(orot.z);
                    world = glm::vec2(oc * d.x + os * d.y, -os * d.x + oc * d.y);
                }
                c.ConnectedAnchor = world;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ulangan langarni hozir shu jismning langari turgan nuqtaga qo'yadi (ulangan jism koordinatalarida)");
        }
        changed |= UI::PropertyBool("Bir-biri bilan to'qnashsin", c.CollideConnected);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("O'chiq bo'lsa ulangan ikki jism bir-biridan o'tib ketadi (ragdoll bo'g'imlari uchun qulay)");

        UI::PropertyHeader("Uzilish", "Joint tortish kuchi yoki momenti shundan oshsa uziladi: yo'qoladi va skriptga "
                                      "OnJointBreak keladi. 0 - hech qachon uzilmaydi");
        UI::PropertyLabel("Kuch chegarasi", "Nyuton (N). 0 - cheksiz");
        if (ImGui::DragFloat("##breakForce", &c.BreakForce, 1.0f, 0.0f, 1e7f, c.BreakForce > 0.0f ? "%.1f N" : "cheksiz"))
        {
            c.BreakForce = std::max(0.0f, c.BreakForce);
            changed = true;
        }
        UI::PropertyLabel("Moment chegarasi", "N*m. 0 - cheksiz");
        if (ImGui::DragFloat("##breakTorque", &c.BreakTorque, 1.0f, 0.0f, 1e7f, c.BreakTorque > 0.0f ? "%.1f N*m" : "cheksiz"))
        {
            c.BreakTorque = std::max(0.0f, c.BreakTorque);
            changed = true;
        }
        return changed;
    }

    void SceneHierarchyPanel::DrawJoint2DStatus(const Joint2DComponentBase& c, const char* readout)
    {
        if (!m_Context || (!m_Context->IsRunning() && !m_Context->IsSimulating()))
            return;
        UI::PropertyHeader("Holati", "O'yin davomidagi qiymatlar (har fizika qadamidan keyin)");
        UI::PropertyLabel("Joint");
        if (c.Broken)
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_LINK_SLASH "  Uzilgan (%.1f N, %.1f N*m)", c.ReactionForce, c.ReactionTorque);
        else if (c.RuntimeJoint)
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), ICON_FA_LINK "  Ishlayapti");
        else
            ImGui::TextDisabled(ICON_FA_LINK "  Kutmoqda (jismlar yo'q)");
        if (!c.RuntimeJoint)
            return;
        UI::PropertyLabel("Kuch / moment");
        ImGui::TextDisabled("%.1f N   %.1f N*m", c.ReactionForce, c.ReactionTorque);
        // One row per line ("name: value").
        for (std::string_view rest = readout ? readout : ""; !rest.empty();)
        {
            size_t end = rest.find('\n');
            std::string_view line = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
            size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                continue;
            UI::PropertyLabel(std::string(line.substr(0, colon)).c_str());
            ImGui::TextDisabled("%s", std::string(line.substr(colon + 1 + (colon + 1 < line.size() && line[colon + 1] == ' ' ? 1 : 0))).c_str());
        }
    }

    bool SceneHierarchyPanel::DrawJoint3DCommon(Entity entity, Joint3DComponentBase& c, Joint3DType type)
    {
        bool changed = DrawEntityRefField("Ulangan jism", c.ConnectedBody,
                                          "Boshqa obyektning Rigidbody 3D'si (ierarxiyadan tortib tashlang). "
                                          "Bo'sh - jism dunyodagi qo'zg'almas nuqtaga bog'lanadi");
        // What keeps the joint from being made (it waits for it while the game runs).
        const char* problem = nullptr;
        Entity other = c.ConnectedBody && m_Context ? m_Context->GetEntityByUUID(c.ConnectedBody.ID) : Entity();
        if (!entity.HasComponent<Rigidbody3DComponent>())
            problem = "Rigidbody 3D kerak: busiz joint yaratilmaydi";
        else if (c.ConnectedBody && !other)
            problem = "Ulangan obyekt sahnada yo'q (o'chirilgan, yoki prefab boshqa sahnada yaratilgan): joint yaratilmaydi";
        else if (other && other == entity)
            problem = "Obyekt o'ziga ulana olmaydi";
        else if (other && !other.HasComponent<Rigidbody3DComponent>())
            problem = "Ulangan obyektda Rigidbody 3D yo'q";
        if (problem)
        {
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", problem);
            ImGui::PopTextWrapPos();
        }
        InactiveComponentNote(entity, "Obyekt nofaol: joint yo'q (faollashganda qayta yaratiladi)");

        // The anchors (and the axis / limits) dragged in the viewport.
        const bool editing = m_Joint3DAnchorEdit == (int)type;
        UI::PropertyLabel("Viewportda", "Langarlarni sahnada sudrab qo'yish: langar (doira), ulangan langar (romb), o'q (kvadrat), "
                                        "chegaralar (+/-). Nuqta ekran tekisligida suriladi; tanlangan nuqtada ko'chirish gizmosi "
                                        "ham chiqadi. Shift - mayda qadam (0.1 birlik / 5\xc2\xb0, o'q - asosiy o'qlarga). Esc yoki "
                                        "tugma qayta bosilsa - tugaydi");
        ImGui::BeginDisabled(!m_SceneEditable);
        if (editing)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentColor());
        if (ImGui::Button(editing ? ICON_FA_CHECK "  Tahrirlashni tugatish" : ICON_FA_ANCHOR "  Langarlarni tahrirlash", ImVec2(-FLT_MIN, 0)))
            SetJoint3DAnchorEdit(editing ? -1 : (int)type);
        if (editing)
            ImGui::PopStyleColor();
        ImGui::EndDisabled();

        changed |= UI::PropertyFloat3("Langar", c.Anchor, 0.01f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Shu jismdagi bog'lanish nuqtasi - obyektning o'z koordinatalarida (masshtab bilan, 3D collider siljishi kabi)");
        changed |= UI::PropertyBool("Avto ulangan langar", c.AutoConfigureConnectedAnchor);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Ulangan langar joint yaratilganda \"Langar\" turgan nuqtaga qo'yiladi (o'yin davomida shu yerda ko'rinadi)");
        ImGui::BeginDisabled(c.AutoConfigureConnectedAnchor);
        changed |= UI::PropertyFloat3(c.ConnectedBody ? "Ulangan langar" : "Dunyodagi nuqta", c.ConnectedAnchor, 0.01f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            UI::Tooltip(c.ConnectedBody ? "Ulangan jismdagi nuqta, uning o'z koordinatalarida"
                                        : "Ulangan jism yo'q: joint dunyodagi shu nuqtaga bog'lanadi");
        ImGui::EndDisabled();
        if (!c.AutoConfigureConnectedAnchor && m_SceneEditable && m_Context && (!other || other != entity))
        {
            UI::PropertyLabel("");
            if (ImGui::SmallButton(Tr(ICON_FA_LOCATION_CROSSHAIRS "  Langar turgan joyga")))
            {
                // Where this body's anchor is now, in the connected body's frame (or the world).
                glm::vec3 world = glm::vec3(m_Context->GetWorldTransform(entity) * glm::vec4(c.Anchor, 1.0f));
                if (other)
                    world = glm::vec3(glm::inverse(m_Context->GetWorldTransform(other)) * glm::vec4(world, 1.0f));
                c.ConnectedAnchor = world;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ulangan langarni hozir shu jismning langari turgan nuqtaga qo'yadi (ulangan jism koordinatalarida)");
        }
        changed |= UI::PropertyBool("Bir-biri bilan to'qnashsin", c.CollideConnected);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("O'chiq bo'lsa ulangan ikki jism bir-biridan o'tib ketadi (ragdoll bo'g'imlari, zanjir halqalari uchun qulay)");

        UI::PropertyHeader("Uzilish", "Joint tortish kuchi yoki momenti shundan oshsa uziladi: yo'qoladi va skriptga "
                                      "OnJointBreak3D keladi. 0 - hech qachon uzilmaydi");
        UI::PropertyLabel("Kuch chegarasi", "Nyuton (N). 0 - cheksiz");
        if (ImGui::DragFloat("##breakForce", &c.BreakForce, 1.0f, 0.0f, 1e7f, c.BreakForce > 0.0f ? "%.1f N" : "cheksiz"))
        {
            c.BreakForce = std::max(0.0f, c.BreakForce);
            changed = true;
        }
        UI::PropertyLabel("Moment chegarasi", "N*m. 0 - cheksiz");
        if (ImGui::DragFloat("##breakTorque", &c.BreakTorque, 1.0f, 0.0f, 1e7f, c.BreakTorque > 0.0f ? "%.1f N*m" : "cheksiz"))
        {
            c.BreakTorque = std::max(0.0f, c.BreakTorque);
            changed = true;
        }
        return changed;
    }

    void SceneHierarchyPanel::DrawJoint3DStatus(const Joint3DComponentBase& c, const char* readout)
    {
        if (!m_Context || (!m_Context->IsRunning() && !m_Context->IsSimulating()))
            return;
        UI::PropertyHeader("Holati", "O'yin davomidagi qiymatlar (har fizika qadamidan keyin)");
        UI::PropertyLabel("Joint");
        if (c.Broken)
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_LINK_SLASH "  Uzilgan (%.1f N, %.1f N*m)", c.ReactionForce, c.ReactionTorque);
        else if (c.RuntimeJoint)
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), ICON_FA_LINK "  Ishlayapti");
        else
            ImGui::TextDisabled(ICON_FA_LINK "  Kutmoqda (jismlar yo'q)");
        if (!c.RuntimeJoint)
            return;
        UI::PropertyLabel("Kuch / moment");
        ImGui::TextDisabled("%.1f N   %.1f N*m", c.ReactionForce, c.ReactionTorque);
        for (std::string_view rest = readout ? readout : ""; !rest.empty();)
        {
            size_t end = rest.find('\n');
            std::string_view line = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
            size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                continue;
            UI::PropertyLabel(std::string(line.substr(0, colon)).c_str());
            ImGui::TextDisabled("%s", std::string(line.substr(colon + 1 + (colon + 1 < line.size() && line[colon + 1] == ' ' ? 1 : 0))).c_str());
        }
    }

    bool SceneHierarchyPanel::DrawConvexHullCollider(Entity entity, ConvexHullCollider3DComponent& c)
    {
        bool changed = UI::PropertyAssetPath("Model fayli", c.ModelPath, ".gltf;.glb;.obj", ICON_FA_CUBES);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Qobiq shu modeldan quriladi. Bo'sh - obyektning Mesh Renderer'idagi model yoki primitiv");
        UI::PropertyLabel("Maks. burchaklar", "Qobiq ko'pi bilan shuncha burchakka ega bo'ladi (4..256). Kam - tezroq, lekin qobiq "
                                              "meshdan biroz kichikroq: eng chetdagi nuqtalar olinadi");
        int maxVertices = c.MaxVertices;
        if (ImGui::SliderInt("##maxVertices", &maxVertices, CollisionShapes3D::MinHullVertices, CollisionShapes3D::MaxHullVertices))
        {
            c.MaxVertices = std::clamp(maxVertices, CollisionShapes3D::MinHullVertices, CollisionShapes3D::MaxHullVertices);
            changed = true;
        }
        // What the hull came out as (cached per model: cheap to ask every frame).
        std::string error;
        Ref<const ConvexHull3D> hull = Scene::GetConvexHullCollider(entity, &error);
        UI::PropertyLabel("Qobiq", "Fizik jism oladigan shakl: viewport'da collider chiziqlari bilan ko'rinadi");
        if (hull)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled(ICON_FA_SHAPES "  %zu burchak, %zu yuz (%zu nuqtadan)%s", hull->Points.size(), hull->Faces.size(),
                                hull->SourceVertexCount, hull->Simplified ? ", soddalashtirilgan" : "");
            ImGui::PopTextWrapPos();
        }
        else
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", error.c_str());
            ImGui::PopTextWrapPos();
        }
        if (!entity.HasComponent<Rigidbody3DComponent>())
        {
            UI::PropertyLabel("");
            ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Rigidbody 3D bilan ishlaydi");
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawMeshCollider(Entity entity, MeshCollider3DComponent& c)
    {
        bool changed = UI::PropertyAssetPath("Model fayli", c.ModelPath, ".gltf;.glb;.obj", ICON_FA_CUBES);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Collider shu modelning uchburchaklaridan quriladi. Bo'sh - obyektning Mesh Renderer'idagi model yoki primitiv");
        // What the body gets (cached per model: cheap to ask every frame).
        std::string error;
        Ref<const CollisionMesh3D> mesh = Scene::GetMeshCollider(entity, &error);
        UI::PropertyLabel("Mesh", "Fizik jism oladigan uchburchaklar: viewport'da collider chiziqlari bilan ko'rinadi. Uchburchaklar "
                                  "old tomonidan (chizilgan tomonidan) qattiq");
        ImGui::PushTextWrapPos(0.0f);
        if (mesh)
            ImGui::TextDisabled(ICON_FA_DRAW_POLYGON "  %zu uchburchak, %zu nuqta", mesh->GetTriangleCount(), mesh->Positions.size());
        else
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", error.c_str());
        ImGui::PopTextWrapPos();
        const auto* rb = entity.TryGetComponent<Rigidbody3DComponent>();
        if (!rb)
        {
            UI::PropertyLabel("");
            ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Rigidbody 3D (Statik yoki Kinematik) bilan ishlaydi");
        }
        else if (rb->Type == Rigidbody3DComponent::BodyType::Dynamic)
        {
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Dinamik jism mesh collider'ni hisobga "
                               "olmaydi: Rigidbody 3D turini Statik yoki Kinematik qiling, harakatlanadigan jism uchun Convex Hull "
                               "Collider 3D ishlating");
            ImGui::PopTextWrapPos();
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawCamera2DFollow(Entity entity, Camera2DFollowComponent& c)
    {
        bool changed = DrawEntityRefField("Nishon", c.Target, "Kuzatiladigan obyekt (ierarxiyadan tortib tashlang). O'yin davomida "
                                                              "fizika va skriptlardan keyin har kadrda uning ortidan siljiydi (faqat x / y)");
        Entity target = c.Target && m_Context ? m_Context->GetEntityByUUID(c.Target.ID) : Entity();
        if (!c.Target || (target && target == entity))
        {
            UI::PropertyLabel("");
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s",
                               !c.Target ? "Nishon tanlanmagan: kamera joyida turadi" : "O'zini kuzata olmaydi");
        }
        InactiveComponentNote(entity, "Obyekt nofaol: kuzatmaydi");

        auto float2 = [&](const char* label, const char* tooltip, glm::vec2& v, float speed, float min, float max) {
            UI::PropertyLabel(label, tooltip);
            ImGui::PushID(label);
            bool edited = ImGui::DragFloat2("##v", &v.x, speed, min, max, "%.2f");
            ImGui::PopID();
            if (edited && min < max)
                v = glm::clamp(v, glm::vec2(min), glm::vec2(max));
            return edited;
        };
        changed |= float2("Siljish", "Nishon pozitsiyasiga qo'shiladi (masalan, o'yinchidan biroz yuqoriga qarash)", c.Offset, 0.05f, 0.0f, 0.0f);
        changed |= float2("Silliqlik (s)", "Kamera masofaning ~63% ini necha soniyada bosib o'tadi (x / y alohida). 0 - nishonga mahkam",
                          c.Damping, 0.01f, 0.0f, 10.0f);
        changed |= float2("O'lik zona", "Ko'rinish markazidagi to'rtburchak (to'liq eni / bo'yi, birlik): nishon uning ichida "
                                         "yurganda kamera qimirlamaydi", c.DeadZone, 0.05f, 0.0f, 1000.0f);

        UI::PropertyHeader("Oldinga qarash", "Kamera nishon oxirgi marta harakatlangan tomonga biroz oldinga o'tadi");
        changed |= float2("Masofa", "Harakat yo'nalishi bo'yicha qancha oldinga (birlik, x / y). 0 - o'chiq", c.LookAhead, 0.05f, -100.0f, 100.0f);
        UI::PropertyLabel("Silliqlik (s)", "Oldinga qarash qancha sekin almashadi (yo'nalish o'zgarganda)");
        if (ImGui::DragFloat("##lookDamping", &c.LookAheadDamping, 0.01f, 0.0f, 10.0f, "%.2f"))
        {
            c.LookAheadDamping = std::clamp(c.LookAheadDamping, 0.0f, 10.0f);
            changed = true;
        }
        UI::PropertyLabel("Min. tezlik", "Nishon shundan tezroq yursa yo'nalish almashadi (birlik/s): mayda qimirlashlar e'tiborga olinmaydi");
        if (ImGui::DragFloat("##lookSpeed", &c.LookAheadMinSpeed, 0.05f, 0.0f, 100.0f, "%.2f"))
        {
            c.LookAheadMinSpeed = std::clamp(c.LookAheadMinSpeed, 0.0f, 100.0f);
            changed = true;
        }

        UI::PropertyHeader("Chegaralar", "Kameraning butun ko'rinishi (nishon chuqurligida) shu dunyo to'rtburchagidan chiqmaydi");
        changed |= UI::PropertyBool("Chegaralash", c.UseBounds);
        if (c.UseBounds)
        {
            changed |= float2("Min (chap-past)", nullptr, c.BoundsMin, 0.1f, 0.0f, 0.0f);
            changed |= float2("Maks (o'ng-yuqori)", nullptr, c.BoundsMax, 0.1f, 0.0f, 0.0f);
            UI::PropertyLabel("");
            ImGui::BeginDisabled(!m_SceneEditable);
            if (ImGui::Button(Tr(ICON_FA_EXPAND "  Sahna chegaralari")) && m_Context)
            {
                AABB box;
                if (m_Context->ComputeSceneBounds(box))
                {
                    c.BoundsMin = glm::vec2(box.Min);
                    c.BoundsMax = glm::vec2(box.Max);
                    changed = true;
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Sahnada chiziladigan hamma narsani o'rab oluvchi to'rtburchak (Shift+F kabi)");
            if (!entity.HasComponent<CameraComponent>())
            {
                UI::PropertyLabel("");
                ImGui::TextDisabled("Kamera komponentisiz: faqat markaz chegaralanadi");
            }
        }
        changed |= UI::PropertyBool("Boshida nishonga", c.SnapOnStart);
        if (ImGui::IsItemHovered())
            UI::Tooltip("O'yin boshlanganda kamera darhol nishon ustida turadi (aks holda joyidan silliq yetib boradi)");

        UI::PropertyLabel("");
        ImGui::BeginDisabled(!target || target == entity || !m_SceneEditable);
        if (ImGui::Button(Tr(ICON_FA_CROSSHAIRS "  Kamerani nishonga qo'yish")) && target && m_Context)
        {
            glm::mat4 world = m_Context->GetWorldTransform(entity);
            glm::vec3 targetPosition = m_Context->GetWorldPosition(target);
            glm::vec2 halfView(0.0f);
            if (entity.HasComponent<CameraComponent>())
                halfView = Camera2DFollow::GetHalfView(m_Context->GetCameraProjection(entity), world, targetPosition.z);
            glm::vec2 position = Camera2DFollow::GetSnapPosition(c, glm::vec2(targetPosition), halfView);
            world[3].x = position.x;
            world[3].y = position.y;
            m_Context->SetWorldTransform(entity, world);
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Editorda: kamerani o'yin boshidagi joyiga (nishon + siljish, chegaralar ichida) qo'yadi");
        return changed;
    }

    bool SceneHierarchyPanel::DrawSpline(Entity entity, SplineComponent& c)
    {
        bool changed = false;
        Spline& path = c.Path;

        // The points dragged in the viewport.
        const bool editing = m_SplinePointEdit;
        UI::PropertyLabel("Viewportda", "Nuqtalarni sahnada sudrab o'zgartirish (3D: kamera tekisligida, tanlangan nuqtada o'qli gizmo). "
                                        "Chiziq ustiga bosish - o'sha joyga yangi nuqta, Ctrl + nuqtaga bosish / Delete - o'chirish, bo'sh joyga "
                                        "ikki marta bosish - oxiriga nuqta, Shift - 0.1 qadam. Esc yoki tugma qayta bosilsa - tugaydi");
        ImGui::BeginDisabled(!m_SceneEditable);
        if (editing)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentColor());
        if (ImGui::Button(editing ? ICON_FA_CHECK "  Tahrirlashni tugatish" : ICON_FA_BEZIER_CURVE "  Nuqtalarni tahrirlash", ImVec2(-FLT_MIN, 0)))
            SetSplinePointEdit(!editing);
        if (editing)
            ImGui::PopStyleColor();
        ImGui::EndDisabled();

        static const char* const types[] = { "Catmull-Rom (silliq)", "Bezier (tutqichli)", "Chiziqli" };
        int type = (int)path.Type;
        const bool typeChanged = UI::PropertyCombo("Turi", type, types, 3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Catmull-Rom - har bir nuqtadan silliq o'tadi; Bezier - har nuqtaning ikki tutqichi egrilikni boshqaradi "
                        "(viewportda sudraladi); chiziqli - nuqtalar to'g'ri chiziqlar bilan ulanadi");
        if (typeChanged && type != (int)path.Type)
        {
            // To Bezier: handles that keep the curve it had.
            if ((SplineType)type == SplineType::Bezier && path.Type == SplineType::CatmullRom)
                path.AutoTangents();
            path.Type = (SplineType)type;
            changed = true;
        }
        changed |= UI::PropertyBool("Yopiq (halqa)", path.Closed);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Oxirgi nuqta birinchisiga ulanadi (aylanma yo'l)");
        if (path.Type == SplineType::CatmullRom)
        {
            UI::PropertyLabel("Alpha", "Nuqtalar orasidagi masofa hisobi: 0 - bir xil (uniform), 0.5 - centripetal (yaqin nuqtalarda "
                                       "halqa va o'tkir uchlar bo'lmaydi), 1 - xordal");
            if (ImGui::SliderFloat("##alpha", &path.Alpha, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
                changed = true;
        }
        changed |= UI::PropertyColor4("Chiziq rangi", c.Color);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Editorda yo'l shu rangda chiziladi (o'yinda ko'rinmaydi)");
        UI::PropertyLabel("Uzunligi", "Yo'l uzunligi obyektning o'z birliklarida (masshtabsiz). Skriptda: GetScene()->GetSplineLength(entity)");
        ImGui::TextDisabled("%.2f birlik  |  %d segment", c.GetLength(), path.GetSegmentCount());

        // The points (local coordinates); Bezier: their handles.
        UI::PropertyHeader(std::format("Nuqtalar ({})", path.Points.size()).c_str(), "Obyekt koordinatalarida (uning transformi bilan siljiydi va aylanadi)");
        int remove = -1;
        const float button = ImGui::GetFrameHeight();
        for (size_t i = 0; i < path.Points.size(); i++)
        {
            ImGui::PushID((int)i);
            SplinePoint& p = path.Points[i];
            UI::PropertyLabel(std::format("  {}", i).c_str());
            ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
            changed |= ImGui::DragFloat3("##p", &p.Position.x, 0.02f, 0.0f, 0.0f, "%.2f");
            ImGui::SameLine();
            ImGui::BeginDisabled(path.Points.size() <= 2);
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(button, 0)))
                remove = (int)i;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                UI::Tooltip(path.Points.size() <= 2 ? "Kamida 2 ta nuqta kerak" : "Nuqtani o'chirish");
            if (path.Type == SplineType::Bezier)
            {
                UI::PropertyLabel("      kirish");
                changed |= ImGui::DragFloat3("##in", &p.InTangent.x, 0.02f, 0.0f, 0.0f, "%.2f");
                UI::PropertyLabel("      chiqish");
                changed |= ImGui::DragFloat3("##out", &p.OutTangent.x, 0.02f, 0.0f, 0.0f, "%.2f");
            }
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            path.Points.erase(path.Points.begin() + remove);
            changed = true;
        }

        UI::PropertyLabel("");
        ImGui::BeginDisabled(!m_SceneEditable);
        if (ImGui::Button(Tr(ICON_FA_PLUS "  Nuqta qo'shish")))
        {
            // Past the last point, in the direction the path ends in.
            SplinePoint p;
            const size_t n = path.Points.size();
            if (n >= 2)
            {
                const glm::vec3 last = path.Points[n - 1].Position, before = path.Points[n - 2].Position;
                glm::vec3 step = last - before;
                if (glm::length(step) < 1e-3f)
                    step = glm::vec3(1.0f, 0.0f, 0.0f);
                p.Position = last + step;
            }
            else if (n == 1)
                p.Position = path.Points[0].Position + glm::vec3(1.0f, 0.0f, 0.0f);
            path.Points.push_back(p);
            if (path.Type == SplineType::Bezier)
                path.AutoTangents((int)path.Points.size() - 1);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_ARROWS_LEFT_RIGHT "  Teskari")))
        {
            std::reverse(path.Points.begin(), path.Points.end());
            for (SplinePoint& p : path.Points)
                std::swap(p.InTangent, p.OutTangent);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Yo'l yo'nalishini almashtirish (oxiri boshiga aylanadi)");
        if (path.Type == SplineType::Bezier)
        {
            ImGui::SameLine();
            if (ImGui::Button(Tr(ICON_FA_WAND_MAGIC_SPARKLES "  Avto tutqichlar")))
            {
                path.AutoTangents();
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Hamma tutqichlarni nuqtalardan silliq o'tadigan qilib qayta hisoblash (Catmull-Rom shakli)");
        }
        ImGui::EndDisabled();
        UI::PropertyLabel("");
        ImGui::TextDisabled("Skriptda: GetScene()->GetSplinePosition(entity, masofa)");
        (void)entity;
        return changed;
    }

    bool SceneHierarchyPanel::DrawSplineFollower(Entity entity, SplineFollowerComponent& c)
    {
        bool changed = DrawEntityRefField("Yo'l", c.Path, "Spline komponentli obyekt (ierarxiyadan tortib tashlang). Tanlanmasa - Spline "
                                                          "komponenti bor eng yaqin ota obyekt");
        Entity path = m_Context ? m_Context->GetSplineFollowerPath(entity) : Entity();
        if (!path || path == entity || (m_Context && m_Context->IsAncestorOf(entity, path)))
        {
            UI::PropertyLabel("");
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s",
                               !path ? "Yo'l topilmadi: Spline komponentli obyektni tanlang"
                                     : "O'z yo'lini (yoki uni olib yuruvchi obyektni) kuzata olmaydi");
        }
        InactiveComponentNote(entity, "Obyekt nofaol: harakatlanmaydi");

        UI::PropertyLabel("Tezlik", "Dunyo birligi / soniya, yo'l bo'ylab (manfiy - orqaga)");
        changed |= ImGui::DragFloat("##speed", &c.Speed, 0.05f, -1000.0f, 1000.0f, "%.2f");
        static const char* const modes[] = { "Bir marta", "Aylanma (takror)", "Borib-qaytish" };
        int mode = (int)c.Mode;
        if (UI::PropertyCombo("Rejim", mode, modes, 3))
        {
            c.Mode = (SplineFollowerComponent::FollowMode)mode;
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Bir marta - oxiriga yetib to'xtaydi; aylanma - qaytadan boshlaydi (yopiq yo'lda aylanib yuradi); "
                        "borib-qaytish - uchlarida orqaga qaytadi");
        UI::PropertyLabel("Boshlanish", "Qayerdan boshlaydi: yo'l uzunligining ulushi");
        float start = c.StartPosition * 100.0f;
        if (ImGui::SliderFloat("##start", &start, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
        {
            c.StartPosition = start / 100.0f;
            changed = true;
        }
        static const char* const rotations[] = { "Yo'q", "Yo'l bo'ylab (3D)", "Yo'l bo'ylab (2D)" };
        int rotation = (int)c.Rotation;
        if (UI::PropertyCombo("Burilish", rotation, rotations, 3))
        {
            c.Rotation = (SplineFollowerComponent::RotationMode)rotation;
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Yo'l bo'ylab qarab boradi: 3D - obyektning -Z o'qi oldinga, Y yuqoriga; 2D - +X o'qi yo'l bo'ylab (Z atrofida). "
                        "Yo'q - burilish o'zgarmaydi (platformalar)");
        UI::PropertyLabel("Siljish", "Yo'ldan siljish yo'lning o'z o'qlarida: x - o'ngga, y - yuqoriga, z - yo'l bo'ylab");
        changed |= ImGui::DragFloat3("##offset", &c.Offset.x, 0.02f, 0.0f, 0.0f, "%.2f");
        changed |= UI::PropertyBool("Boshida o'ynash", c.PlayOnStart);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("O'yin boshlanganda darhol yuradi. O'chiq bo'lsa skriptdan: GetComponent<SplineFollowerComponent>().Play()");

        if (m_Context && m_Context->IsRunning())
        {
            UI::PropertyHeader("Jonli");
            UI::PropertyLabel("Holat");
            ImGui::TextDisabled("%s  |  %.2f / %.2f birlik", c.Finished ? "oxiriga yetdi" : c.Playing ? "yuryapti" : "to'xtagan", c.Distance,
                                m_Context->GetSplineLength(path));
            UI::PropertyLabel("");
            ImGui::ProgressBar(m_Context->GetSplineFollowerProgress(entity), ImVec2(-FLT_MIN, 0.0f));
            UI::PropertyLabel("");
            if (ImGui::Button(c.Playing ? ICON_FA_PAUSE "  Pauza" : ICON_FA_PLAY "  Davom"))
            {
                if (c.Playing)
                    c.Pause();
                else
                    c.Play();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr(ICON_FA_BACKWARD_STEP "  Boshidan")))
                c.Restart();
        }
        else
        {
            UI::PropertyLabel("");
            ImGui::BeginDisabled(!path || path == entity || !m_SceneEditable);
            if (ImGui::Button(Tr(ICON_FA_CROSSHAIRS "  Boshlanish nuqtasiga qo'yish")) && path && m_Context)
            {
                glm::mat4 world = m_Context->GetWorldTransform(entity);
                const glm::vec3 p = m_Context->GetSplinePosition(path, c.StartPosition * m_Context->GetSplineLength(path));
                world[3] = glm::vec4(p, 1.0f);
                m_Context->SetWorldTransform(entity, world);
                changed = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Obyektni yo'ldagi boshlanish nuqtasiga ko'chirish (o'yin boshida u baribir shu yerga qo'yiladi; siljishsiz)");
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawSpriteAnimation(Entity entity, SpriteAnimationComponent& c)
    {
        InactiveComponentNote(entity, "Obyekt nofaol: animatsiya to'xtab turadi");
        bool changed = false;
        const auto* sprite = entity.TryGetComponent<SpriteRendererComponent>();
        const SpriteSheet* sheet = sprite && !sprite->TexturePath.empty() ? SpriteSheet::Get(sprite->TexturePath) : nullptr;

        // ---- clips (.zsanim): a cleared row goes away; the empty row at the end adds one
        {
            int remove = -1;
            bool listChanged = false;
            for (int i = 0; i <= (int)c.Clips.size(); i++)
            {
                const bool adding = i == (int)c.Clips.size();
                std::string path = adding ? std::string() : c.Clips[(size_t)i];
                ImGui::PushID(i + 7000);
                std::string label = adding ? std::string("Klip qo'shish") : std::format("Klip {}", i + 1);
                UI::AssetPathOptions options;
                bool open = false;
                if (!adding)
                {
                    options.ActionIcon = ICON_FA_PEN;
                    options.ActionTooltip = "Klipni asset inspektorida tahrirlash (kadrlar, FPS, event'lar)";
                    options.ActionClicked = &open;
                    options.Missing = !path.empty() && !SpriteClipLibrary::Load(path);
                }
                options.Tooltip = adding ? ".zsanim faylini shu yerga tashlang yoki tanlang"
                                         : i == 0 ? "Birinchi klip: o'yin boshlanganda o'ynaydi va editorda ko'rinadi. Tozalash - ro'yxatdan olib tashlaydi"
                                                  : "Skript PlaySpriteClip(\"nom\") bilan o'ynatadi. Tozalash - ro'yxatdan olib tashlaydi";
                if (UI::PropertyAssetPath(label.c_str(), path, ".zsanim", ICON_FA_FILM, options))
                {
                    if (adding)
                    {
                        if (!path.empty())
                            c.Clips.push_back(path);
                    }
                    else if (path.empty())
                        remove = i;
                    else
                        c.Clips[(size_t)i] = path;
                    listChanged = true;
                }
                if (open && OnAssetEditRequested && !path.empty())
                    OnAssetEditRequested(Project::GetAssetFileSystemPath(path));
                ImGui::PopID();
            }
            if (remove >= 0)
                c.Clips.erase(c.Clips.begin() + remove);
            if (listChanged)
            {
                // The editor shows the (new) first clip at once.
                c.ResolveClips();
                c.CurrentClip = -1;
                c.AppliedRevision = 0;
                if (c.PlayOnStart && !c.ClipAssets.empty() && c.ClipAssets[0])
                {
                    c.ApplyClip(c.ClipAssets[0]->GetClip());
                    c.AppliedRevision = c.ClipAssets[0]->GetRevision();
                    c.CurrentClip = 0;
                }
                changed = true;
            }
            UI::PropertyLabel("");
            ImGui::BeginDisabled(!Project::GetActive() || !m_SceneEditable);
            if (ImGui::Button(Tr(ICON_FA_FILE_CIRCLE_PLUS "  Shu kadrlardan klip")))
            {
                // Assets/Animations/<entity name>.zsanim (a free name) with the frames set below.
                std::string name;
                for (char ch : entity.GetName())
                    name += std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' ? ch : '_';
                if (name.empty()) name = "Sprite";
                std::filesystem::path dir = Project::GetAssetDirectory() / "Animations";
                std::filesystem::path file = AssetFiles::MakeUniquePath(dir, name + SpriteClip::Extension);
                SpriteClip clip;
                clip.Texture = sprite ? sprite->TexturePath : std::string();
                clip.Frames = c.Frames;
                clip.Columns = c.Columns;
                clip.Rows = c.Rows;
                clip.StartFrame = c.StartFrame;
                clip.FrameCount = c.FrameCount;
                if (clip.UsesFrameList())
                {
                    // Only the frames it plays.
                    const int start = std::clamp(c.StartFrame, 0, (int)clip.Frames.size());
                    const int count = std::clamp(c.FrameCount, 1, (int)clip.Frames.size() - start);
                    clip.Frames = std::vector<std::string>(clip.Frames.begin() + start, clip.Frames.begin() + start + std::max(0, count));
                    clip.StartFrame = 0;
                    clip.FrameCount = (int)clip.Frames.size();
                }
                clip.FPS = c.FPS;
                clip.Loop = c.Loop;
                if (SpriteClipSerializer::Serialize(clip, file))
                {
                    if (AssetRegistry* registry = Project::GetAssetRegistry())
                        registry->OnFileAdded(file);
                    c.Clips.push_back(Project::GetAssetRelativePath(file));
                    changed = true;
                    ZM_INFO("Yangi sprite animatsiya klipi: {}", c.Clips.back());
                    if (OnFilesCreated)
                        OnFilesCreated({ file });
                    if (OnAssetEditRequested)
                        OnAssetEditRequested(file);
                }
                else
                    ZM_ERROR("Sprite animatsiya klipi yozilmadi: {}", file.string());
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Assets/Animations ichida hozirgi kadrlar, FPS va takrorlash bilan .zsanim klip yaratiladi va ro'yxatga qo'shiladi");
            if (!c.Clips.empty())
            {
                UI::PropertyLabel("Avto boshlash", "O'yin boshlanganda birinchi klip o'ynaydi (editorda ham u ko'rinadi)");
                changed |= ImGui::Checkbox("##playonstart", &c.PlayOnStart);
                const std::string current = c.GetCurrentClipName();
                if (!current.empty())
                {
                    UI::PropertyLabel("Hozirgi klip");
                    ImGui::Text("%s  %s", c.Playing ? ICON_FA_PLAY : ICON_FA_PAUSE, current.c_str());
                }
                UI::PropertyLabel("");
                ImGui::TextDisabled("Klip quyidagi kadrlarni o'zi belgilaydi");
            }
            ImGui::Separator();
        }

        const char* sources[] = { "Grid (ustun x qator)", "Sprite sheet kadrlari" };
        int source = c.UsesFrameList() ? 1 : 0;
        UI::PropertyLabel("Kadrlar manbai", "Grid: tekstura teng kataklarga bo'linadi. Sprite sheet: teksturaning nomlangan kadrlari "
                                            "(Sprite muharriri) ro'yxati - turli o'lchamli kadrlar ham bo'ladi.");
        ImGui::PushID("source");
        if (ImGui::Combo("##v", &source, sources, 2))
        {
            if (source == 0)
                c.Frames.clear();
            else if (sheet)
                c.PlayFrames(sheet->FindSequence(sheet->GetSequencePrefixes().empty() ? "" : sheet->GetSequencePrefixes().front()), c.FPS, c.Loop);
            changed = true;
        }
        ImGui::PopID();
        if (source == 1 && !c.UsesFrameList())
        {
            UI::PropertyLabel("");
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Teksturada kadrlar yo'q");
        }

        if (c.UsesFrameList())
        {
            // Sequences of the sheet ("run_0", "run_1", ...) by their prefix.
            UI::PropertyLabel("Ketma-ketlik", "Bir xil nomli, raqamlangan kadrlar (masalan run_0, run_1, ...) tartib bilan qo'yiladi");
            ImGui::PushID("sequence");
            if (ImGui::BeginCombo("##v", std::format("{} ta kadr", c.Frames.size()).c_str()))
            {
                if (sheet)
                {
                    for (const std::string& prefix : sheet->GetSequencePrefixes())
                    {
                        std::vector<std::string> sequence = sheet->FindSequence(prefix);
                        if (ImGui::Selectable(std::format("{}  ({} ta)", prefix, sequence.size()).c_str()))
                        {
                            c.PlayFrames(sequence, c.FPS, c.Loop);
                            changed = true;
                        }
                    }
                    if (ImGui::Selectable(std::format("(hammasi, {} ta)", sheet->Frames.size()).c_str()))
                    {
                        c.PlayFrames(sheet->FindSequence(""), c.FPS, c.Loop);
                        changed = true;
                    }
                }
                else
                    ImGui::TextDisabled("Teksturada kadrlar yo'q");
                ImGui::EndCombo();
            }
            ImGui::PopID();
            UI::PropertyLabel("Kadrlar");
            std::string list;
            for (size_t i = 0; i < c.Frames.size() && i < 12; i++)
                list += (i ? ", " : "") + c.Frames[i];
            if (c.Frames.size() > 12)
                list += std::format(" ... (+{})", c.Frames.size() - 12);
            ImGui::TextWrapped("%s", list.c_str());
            int missing = 0;
            for (const std::string& name : c.Frames)
                if (!sheet || !sheet->Find(name))
                    missing++;
            if (missing > 0)
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %d ta kadr teksturada topilmadi", missing);
            }
        }
        else
        {
            changed |= UI::PropertyInt("Ustunlar", c.Columns);
            changed |= UI::PropertyInt("Qatorlar", c.Rows);
        }
        changed |= UI::PropertyInt("Boshlang'ich kadr", c.StartFrame);
        changed |= UI::PropertyInt("Kadrlar soni", c.FrameCount);
        changed |= UI::PropertyFloat("FPS", c.FPS, 0.1f, 0.0f, 120.0f);
        changed |= UI::PropertyBool("Takrorlash", c.Loop);
        changed |= UI::PropertyBool("Ijroda", c.Playing);
        c.Columns = std::max(1, c.Columns); c.Rows = std::max(1, c.Rows);
        c.FrameCount = std::max(1, c.FrameCount); c.StartFrame = std::max(0, c.StartFrame);
        UI::PropertyLabel("Hozirgi kadr");
        if (c.UsesFrameList())
            ImGui::Text("%d  (%s)", c.GetCurrentFrame(), c.GetCurrentFrameName().c_str());
        else
            ImGui::Text("%d", c.GetCurrentFrame());
        return changed;
    }

    bool SceneHierarchyPanel::DrawTagsComponent(TagsComponent& c)
    {
        bool changed = false;
        const ImGuiStyle& style = ImGui::GetStyle();

        UI::PropertyLabel("Teglar");
        if (c.Tags.empty())
            ImGui::TextDisabled("(teg yo'q)");
        else
        {
            // Chips wrap onto new lines; clicking one removes the tag.
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            int remove = -1;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor(0.55f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.27f, 0.24f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.82f, 0.30f, 0.27f, 1.0f));
            for (int i = 0; i < (int)c.Tags.size(); i++)
            {
                std::string label = std::string(ICON_FA_TAG " ") + c.Tags[i] + "  " ICON_FA_XMARK;
                float w = ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f;
                if (i > 0 && ImGui::GetItemRectMax().x + style.ItemSpacing.x + w <= right)
                    ImGui::SameLine();
                ImGui::PushID(i);
                if (ImGui::Button(label.c_str()))
                    remove = i;
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Tegni olib tashlash");
                ImGui::PopID();
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            if (remove >= 0)
            {
                c.Tags.erase(c.Tags.begin() + remove);
                changed = true;
            }
        }

        // Add: type a new tag (Enter) or pick one used in the project / scene.
        UI::PropertyLabel("Qo'shish");
        const float buttonW = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - buttonW - style.ItemSpacing.x));
        std::string toAdd;
        if (ImGui::InputTextWithHint("##newTag", Tr("Yangi teg (Enter)"), &m_NewTagBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            toAdd = TrimTag(m_NewTagBuffer);
            m_NewTagBuffer.clear();
            ImGui::SetKeyboardFocusHere(-1); // keep typing the next tag
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LIST "##tagSuggestions", ImVec2(buttonW, 0)))
            ImGui::OpenPopup("TagSuggestions");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Mavjud teglardan tanlash");
        if (ImGui::BeginPopup("TagSuggestions"))
        {
            std::vector<std::string> known;
            if (auto project = Project::GetActive())
                known = project->GetConfig().Tags;
            for (auto& t : m_Context->GetAllTags())
                if (std::find(known.begin(), known.end(), t) == known.end())
                    known.push_back(t);
            std::string filter = StringUtils::ToLower(TrimTag(m_NewTagBuffer));
            int shown = 0;
            for (const auto& t : known)
            {
                if (c.Has(t) || (!filter.empty() && StringUtils::ToLower(t).find(filter) == std::string::npos))
                    continue;
                if (ImGui::Selectable((std::string(ICON_FA_TAG "  ") + t).c_str()))
                {
                    toAdd = t;
                    m_NewTagBuffer.clear();
                }
                shown++;
            }
            if (shown == 0)
                ImGui::TextDisabled("Taklif yo'q: yangi teg nomini yozing");
            ImGui::EndPopup();
        }

        if (!toAdd.empty() && c.Add(toAdd))
        {
            changed = true;
            // New names go to the project's tag list (Project settings > Tags).
            if (Project::RegisterTag(toAdd) && OnProjectSettingsModified)
                OnProjectSettingsModified();
        }

        UI::PropertyLabel("");
        ImGui::TextDisabled("Skriptda: HasTag(\"%s\"), FindEntitiesWithTag(...)", c.Tags.empty() ? "Enemy" : c.Tags.front().c_str());
        return changed;
    }

    bool SceneHierarchyPanel::DrawScriptField(const ScriptFieldInfo& f, ScriptFieldValue& v)
    {
        if (!f.HeaderText.empty())
            UI::PropertyHeader(f.HeaderText.c_str());

        const char* label = f.GetDisplayName().c_str();
        const char* tooltip = f.TooltipText.empty() ? nullptr : f.TooltipText.c_str();
        if (UI::PrefabFieldScope* scope = UI::CurrentPrefabFieldScope())
            scope->NextPath = "NativeScript/Fields/" + PrefabOverrides::EscapeSegment(f.Name);
        if (f.Type == ScriptFieldType::Entity)
        {
            EntityRef ref = v.Get<EntityRef>();
            if (!DrawEntityRefField(label, ref, tooltip))
                return false;
            v.Set(ref);
            return true;
        }

        UI::PropertyLabel(label, tooltip);
        ImGui::PushID(f.Name.c_str());
        bool changed = false;
        const ImGuiSliderFlags clamp = ImGuiSliderFlags_AlwaysClamp;
        auto vecField = [&](auto value, int components) {
            float* p = &value.x;
            bool edited = false;
            if (f.IsColor && components == 3)
                edited = ImGui::ColorEdit3("##v", p, ImGuiColorEditFlags_Float | (f.IsHdr ? ImGuiColorEditFlags_HDR : 0));
            else if (f.IsColor && components == 4)
                edited = ImGui::ColorEdit4("##v", p, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                                                     (f.IsHdr ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float : 0));
            else if (f.HasRange)
                edited = ImGui::SliderScalarN("##v", ImGuiDataType_Float, p, components, &f.RangeMin, &f.RangeMax, "%.3f", clamp);
            else
                edited = ImGui::DragScalarN("##v", ImGuiDataType_Float, p, components, 0.05f, nullptr, nullptr, "%.3f");
            if (edited) v.Set(value);
            return edited;
        };

        switch (f.Type)
        {
            case ScriptFieldType::Float:
            {
                float value = v.Get<float>();
                changed = f.HasRange ? ImGui::SliderFloat("##v", &value, f.RangeMin, f.RangeMax, "%.3f", clamp)
                                     : ImGui::DragFloat("##v", &value, 0.05f);
                if (changed) v.Set(value);
                break;
            }
            case ScriptFieldType::Int:
            case ScriptFieldType::Enum:
            {
                bool isEnum = f.Type == ScriptFieldType::Enum;
                int64_t value = isEnum ? v.GetEnum() : (int64_t)v.Get<int>();
                if (!f.EnumOptions.empty())
                {
                    const ScriptEnumOption* current = f.FindOption(value);
                    std::string preview = current ? current->Name : std::format("({})", value);
                    if (ImGui::BeginCombo("##v", preview.c_str()))
                    {
                        for (const auto& option : f.EnumOptions)
                        {
                            bool selected = option.Value == value;
                            if (ImGui::Selectable(option.Name.c_str(), selected) && !selected)
                            {
                                value = option.Value;
                                changed = true;
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                else if (isEnum)
                    changed = ImGui::DragScalar("##v", ImGuiDataType_S64, &value, 0.2f);
                else
                {
                    int i = (int)value;
                    int lo = (int)std::ceil(f.RangeMin), hi = (int)std::floor(f.RangeMax);
                    changed = f.HasRange && lo <= hi ? ImGui::SliderInt("##v", &i, lo, hi, "%d", clamp) : ImGui::DragInt("##v", &i);
                    value = i;
                }
                if (changed)
                {
                    if (isEnum) v.SetEnum(value);
                    else v.Set((int)value);
                }
                break;
            }
            case ScriptFieldType::Bool:
            {
                bool value = v.Get<bool>();
                changed = ImGui::Checkbox("##v", &value);
                if (changed) v.Set(value);
                break;
            }
            case ScriptFieldType::Vec2: changed = vecField(v.Get<glm::vec2>(), 2); break;
            case ScriptFieldType::Vec3: changed = vecField(v.Get<glm::vec3>(), 3); break;
            case ScriptFieldType::Vec4: changed = vecField(v.Get<glm::vec4>(), 4); break;
            case ScriptFieldType::String:
                changed = f.IsMultiline ? ImGui::InputTextMultiline("##v", &v.StringValue, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f))
                                        : ImGui::InputText("##v", &v.StringValue);
                break;
            case ScriptFieldType::Curve:
                // ZM_FIELD_RANGE: the values the view shows at least.
                changed = UI::CurveEditor("##v", v.CurveValue, f.HasRange ? f.RangeMin : 0.0f, f.HasRange ? f.RangeMax : 1.0f);
                break;
            default:
                ImGui::TextDisabled("(noma'lum tur)");
                break;
        }
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tooltip);
        ImGui::PopID();
        if (changed)
            f.ClampToRange(v);
        return changed;
    }

}
