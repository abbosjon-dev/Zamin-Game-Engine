#include "ProjectSettingsPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <unordered_map>

namespace Zamin {

    namespace {
        const ImVec4 s_ChangedColor(0.95f, 0.75f, 0.30f, 1.0f);

        const ProjectSettingsCategory& Info(ProjectSettingsPanel::Category category)
        {
            return ProjectSettingsIndex::GetCategories()[(size_t)category];
        }

        // A small filled dot (a category with unsaved changes).
        void ChangedDot(const ImVec2& center)
        {
            ImGui::GetWindowDrawList()->AddCircleFilled(center, ImGui::GetFontSize() * 0.2f, ImGui::GetColorU32(s_ChangedColor));
        }
    }

    // ------------------------------------------------------------------ panel
    void ProjectSettingsPanel::Focus(Tab tab)
    {
        m_FocusRequested = true;
        switch (tab)
        {
            case Tab::Input:         m_Category = Category::Input; break;
            case Tab::SaveData:      m_Category = Category::SaveData; break;
            case Tab::Tags:          m_Category = Category::TagsAndLayers; m_RequestedLayersPage = 0; break;
            case Tab::General:       m_Category = Category::General; break;
            case Tab::SortingLayers: m_Category = Category::TagsAndLayers; m_RequestedLayersPage = 1; break;
            case Tab::PhysicsLayers: m_Category = Category::Physics; break;
            case Tab::BuildScenes:   m_Category = Category::BuildScenes; break;
            case Tab::Quality:       m_Category = Category::Quality; break;
            case Tab::Audio:         m_Category = Category::Audio; break;
            case Tab::Debug:         m_Category = Category::Debug; break;
            case Tab::Player:        m_Category = Category::Player; break;
            case Tab::Graphics:      m_Category = Category::Graphics; break;
            case Tab::Time:          m_Category = Category::Time; break;
            case Tab::Localization:  m_Category = Category::Localization; break;
        }
        // The asked category must be in the list.
        if (!ProjectSettingsIndex::Matches(Info(m_Category), m_Search))
            m_Search.clear();
    }

    void ProjectSettingsPanel::OnProjectChanged()
    {
        m_Dirty = false;
        StopListening();
        m_SavedFor = nullptr;
        m_SavedSections.clear();
        m_DirtySections.clear();
        m_ProjectUsageValid = false;
        m_UsageFor = nullptr;
        m_QualityLevel = -1;
        m_RenamingTag = -1;
        m_RenamingLayer = ~0u;
        m_PendingPreset = -1;
        m_IconPreview = nullptr;
        m_IconPreviewPath.clear();
        m_VersionEditing = false;
    }

    void ProjectSettingsPanel::RefreshSaved()
    {
        Ref<Project> project = Project::GetActive();
        m_SavedFor = project.get();
        m_DirtySections.clear();
        if (!project)
        {
            m_Saved = ProjectConfig();
            m_SavedSections.clear();
            return;
        }
        // The file as it is on disk (no file yet: what is in memory counts as saved).
        ProjectConfig saved = project->GetConfig();
        const std::filesystem::path file = Project::GetProjectFilePath();
        std::error_code ec;
        if (!file.empty() && std::filesystem::is_regular_file(file, ec))
        {
            ProjectConfig read;
            if (ProjectSerializer::ConfigFromString(FileSystem::ReadTextFile(file), read))
                saved = std::move(read);
        }
        m_Saved = std::move(saved);
        m_SavedSections = ProjectSerializer::ConfigToSections(m_Saved);
    }

    void ProjectSettingsPanel::UpdateDirtySections(const ProjectConfig& config)
    {
        if (!m_Dirty)
        {
            m_DirtySections.clear();
            return;
        }
        m_DirtySections = ProjectSettingsIndex::DiffSections(m_SavedSections, ProjectSerializer::ConfigToSections(config));
        // Changed back to what the file has: nothing to save.
        if (m_DirtySections.empty())
            m_Dirty = false;
    }

    bool ProjectSettingsPanel::IsCategoryDirty(Category category) const
    {
        return m_Dirty && ProjectSettingsIndex::IsChanged(Info(category), m_DirtySections);
    }

    void ProjectSettingsPanel::ApplyRuntime(Category category, ProjectConfig& config)
    {
        switch (category)
        {
            case Category::Quality:
                Quality::SetSettings(config.Quality, true);
                break;
            case Category::Audio:
                // In play mode the mixer is the game's: the defaults apply when it stops.
                if (m_SceneEditable)
                    AudioEngine::SetMixerSettings(config.Audio);
                break;
            case Category::Time:
            case Category::Physics:
                ApplySceneDefaults(config);
                break;
            case Category::Localization:
                // The language files again (another folder / list); the preview language stays when offered.
                Project::ApplyLocalization(true);
                break;
            default:
                break;
        }
    }

    void ProjectSettingsPanel::RevertCategory(Category category, ProjectConfig& config)
    {
        const ProjectConfig& s = m_Saved;
        switch (category)
        {
            case Category::General:
                config.Name = s.Name;
                config.AssetDirectory = s.AssetDirectory;
                config.SourceDirectory = s.SourceDirectory;
                config.ScriptModulePath = s.ScriptModulePath;
                break;
            case Category::Player:        config.Player = s.Player; m_VersionEditing = false; m_IconPreviewPath.clear(); break;
            case Category::Quality:       config.Quality = s.Quality; m_QualityLevel = -1; break;
            case Category::Graphics:      config.Graphics = s.Graphics; break;
            case Category::Time:          config.Time = s.Time; break;
            case Category::Physics:       config.CollisionLayers = s.CollisionLayers; config.Physics = s.Physics; break;
            case Category::Audio:         config.Audio = s.Audio; break;
            case Category::Input:         config.InputActions = s.InputActions; StopListening(); break;
            case Category::TagsAndLayers:
                config.Tags = s.Tags;
                config.SortingLayers = s.SortingLayers;
                m_RenamingTag = -1;
                m_RenamingLayer = ~0u;
                break;
            case Category::BuildScenes:
                config.BuildScenes = s.BuildScenes;
                config.StartScene = s.StartScene;
                config.StartSceneHandle = s.StartSceneHandle;
                break;
            case Category::Debug:         config.DebugDraw = s.DebugDraw; break;
            case Category::Localization:  config.Localization = s.Localization; break;
            default:                      break;
        }
        ApplyRuntime(category, config);
        m_Dirty = true; // compared with the file again next frame
    }

    void ProjectSettingsPanel::RevertAll(ProjectConfig& config)
    {
        for (int i = 0; i < (int)Category::Count; i++)
            RevertCategory((Category)i, config);
        ZM_INFO("Loyiha sozlamalari saqlangan holatiga qaytarildi");
    }

    void ProjectSettingsPanel::SaveIfDirty()
    {
        Ref<Project> project = Project::GetActive();
        if (!m_Dirty || !project)
            return;
        if (Project::SaveActive(Project::GetProjectFilePath()))
        {
            m_Dirty = false;
            m_SavedFor = project.get();
            m_Saved = project->GetConfig();
            m_SavedSections = ProjectSerializer::ConfigToSections(m_Saved);
            m_DirtySections.clear();
            ZM_INFO("Loyiha sozlamalari saqlandi: {}", Project::GetProjectFilePath().filename().string());
        }
        else
            ZM_ERROR("Loyiha sozlamalari saqlanmadi: {}", Project::GetProjectFilePath().string());
    }

    void ProjectSettingsPanel::OnImGuiRender(bool* show)
    {
        if (!*show)
        {
            StopListening();
            return;
        }
        if (m_FocusRequested)
        {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        if (m_FloatingRequested)
        {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(m_FloatingRect.x, m_FloatingRect.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(m_FloatingRect.z, m_FloatingRect.w), ImGuiCond_Always);
        }
        else
            ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 52.0f, ImGui::GetFontSize() * 34.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Tr(ICON_FA_GEAR "  Loyiha sozlamalari###ProjectSettings"), show))
        {
            ImGui::End();
            return;
        }

        auto project = Project::GetActive();
        if (!project)
        {
            ImGui::TextDisabled("Loyiha ochilmagan");
            ImGui::End();
            return;
        }
        auto& config = project->GetConfig();
        if (m_SavedFor != project.get())
            RefreshSaved();
        UpdateDirtySections(config);

        // ---- save bar: save, revert, what changed
        ImGui::BeginDisabled(!m_Dirty);
        if (ImGui::Button(Tr(ICON_FA_FLOPPY_DISK "  Saqlash")))
            SaveIfDirty();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("O'zgarishlarni loyiha fayliga yozish (Ctrl+S sahna bilan birga saqlaydi)");
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_ROTATE_LEFT "  Bekor qilish")))
            ImGui::OpenPopup("RevertAllSettings");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Hamma bo'limlarni loyiha faylidagi (oxirgi saqlangan) holatiga qaytarish");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (m_Dirty)
        {
            int changed = 0;
            for (int i = 0; i < (int)Category::Count; i++)
                changed += IsCategoryDirty((Category)i) ? 1 : 0;
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(s_ChangedColor, ICON_FA_CIRCLE "  %s: %d", Tr("Saqlanmagan o'zgarishli bo'limlar"), std::max(changed, 1));
        }
        else
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(ICON_FA_CHECK "  %s: %s", Tr("Saqlangan"), Project::GetProjectFilePath().filename().string().c_str());
        }
        if (ImGui::BeginPopup("RevertAllSettings"))
        {
            ImGui::TextUnformatted("Barcha saqlanmagan o'zgarishlar bekor qilinsinmi?");
            if (ImGui::Button(Tr("Ha, qaytarish")))
            {
                RevertAll(config);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Yo'q")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Separator();

        const bool compact = ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 30.0f;
        if (compact)
        {
            DrawCategoryList(ImGui::GetContentRegionAvail().x, true);
            ImGui::Separator();
            if (ImGui::BeginChild("##content", ImVec2(0, 0)))
                DrawCategoryContent(config);
            ImGui::EndChild();
        }
        else
        {
            const float listWidth = std::clamp(ImGui::GetFontSize() * 12.5f, 150.0f, ImGui::GetContentRegionAvail().x * 0.35f);
            if (ImGui::BeginChild("##categories", ImVec2(listWidth, 0), ImGuiChildFlags_Borders))
                DrawCategoryList(listWidth, false);
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::BeginChild("##content", ImVec2(0, 0)))
                DrawCategoryContent(config);
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void ProjectSettingsPanel::DrawCategoryList(float width, bool compact)
    {
        const auto& categories = ProjectSettingsIndex::GetCategories();
        ImGui::SetNextItemWidth(compact ? std::max(80.0f, width * 0.45f) : -FLT_MIN);
        ImGui::InputTextWithHint("##search", Tr(ICON_FA_MAGNIFYING_GLASS "  Sozlamani qidirish"), &m_Search);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Bo'lim nomi yoki sozlama nomi (masalan \"soya\", \"deadzone\", \"teg\"): mos sozlamalar belgilanadi");

        // The categories that match the search (all without one).
        std::vector<int> shown;
        std::vector<std::vector<std::string>> hits(categories.size());
        for (size_t i = 0; i < categories.size(); i++)
            if (ProjectSettingsIndex::Matches(categories[i], m_Search, &hits[i]))
                shown.push_back((int)i);
        if (!shown.empty() && std::find(shown.begin(), shown.end(), (int)m_Category) == shown.end())
            m_Category = (Category)shown.front(); // the search hid the open one

        if (compact)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            const ProjectSettingsCategory& current = categories[(size_t)m_Category];
            const std::string preview = std::format("{}  {}{}", current.Icon, Tr(current.Title), IsCategoryDirty(m_Category) ? "  *" : "");
            if (ImGui::BeginCombo("##category", preview.c_str(), ImGuiComboFlags_HeightLarge))
            {
                for (int i : shown)
                {
                    const std::string label = std::format("{}  {}{}", categories[(size_t)i].Icon, Tr(categories[(size_t)i].Title),
                                                          IsCategoryDirty((Category)i) ? "  *" : "");
                    if (ImGui::Selectable(label.c_str(), i == (int)m_Category))
                        m_Category = (Category)i;
                }
                ImGui::EndCombo();
            }
            if (shown.empty())
                ImGui::TextDisabled("Hech narsa topilmadi");
            return;
        }

        ImGui::Spacing();
        for (int i : shown)
        {
            const ProjectSettingsCategory& c = categories[(size_t)i];
            ImGui::PushID(i);
            const bool selected = i == (int)m_Category;
            const std::string label = std::format("{}  {}", c.Icon, Tr(c.Title));
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetFrameHeight())))
                m_Category = (Category)i;
            if (IsCategoryDirty((Category)i))
            {
                const ImVec2 max = ImGui::GetItemRectMax(), min = ImGui::GetItemRectMin();
                ChangedDot(ImVec2(max.x - ImGui::GetFontSize() * 0.45f, (min.y + max.y) * 0.5f));
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Saqlanmagan o'zgarishlar bor");
            }
            // Under a search: the matching settings, a click opens the category.
            if (!m_Search.empty())
                for (size_t h = 0; h < hits[(size_t)i].size() && h < 4; h++)
                {
                    ImGui::PushID((int)h);
                    ImGui::Indent(ImGui::GetFontSize() * 1.2f);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::Selectable(hits[(size_t)i][h].c_str(), false))
                        m_Category = (Category)i;
                    ImGui::PopStyleColor();
                    ImGui::Unindent(ImGui::GetFontSize() * 1.2f);
                    ImGui::PopID();
                }
            ImGui::PopID();
        }
        if (shown.empty())
            ImGui::TextDisabled("Hech narsa topilmadi");
    }

    void ProjectSettingsPanel::DrawCategoryContent(ProjectConfig& config)
    {
        const ProjectSettingsCategory& info = Info(m_Category);
        // Header: the category's title, what it is, its own revert.
        const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(UI::AccentColor(), "%s  %s", info.Icon.c_str(), Tr(info.Title).c_str());
        if (IsCategoryDirty(m_Category))
        {
            const char* revert = ICON_FA_ROTATE_LEFT "  Bo'limni qaytarish";
            ImGui::SameLine();
            const float width = ImGui::CalcTextSize(revert).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 8.0f, rightEdge - width));
            if (ImGui::SmallButton(revert))
                RevertCategory(m_Category, config);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Shu bo'limni loyiha faylidagi holatiga qaytarish");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", Tr(info.Description).c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // The search highlights the matching labels of the category.
        UI::SearchHighlightText() = m_Search;
        switch (m_Category)
        {
            case Category::General:       DrawGeneral(config); break;
            case Category::Player:        DrawPlayer(config); break;
            case Category::Quality:       DrawQuality(config); break;
            case Category::Graphics:      DrawGraphics(config); break;
            case Category::Time:          DrawTime(config); break;
            case Category::Physics:       DrawPhysics(config); break;
            case Category::Audio:         DrawAudio(config); break;
            case Category::Input:         DrawInputMap(config.InputActions); break;
            case Category::TagsAndLayers: DrawTagsAndLayers(config); break;
            case Category::BuildScenes:   DrawBuildScenes(config); break;
            case Category::Localization:  DrawLocalization(config); break;
            case Category::Debug:         DrawDebug(config); break;
            case Category::SaveData:      DrawSaveData(); break;
            default:                      break;
        }
        UI::SearchHighlightText().clear();
        if (m_ScrollFrames > 0)
        {
            m_ScrollFrames--;
            ImGui::SetScrollY(m_ScrollRequest < 0.0f ? ImGui::GetScrollMaxY() : std::min(m_ScrollRequest, ImGui::GetScrollMaxY()));
        }
        // Listening for a key only while the input map is shown.
        if (m_Category != Category::Input)
            StopListening();
    }

    void ProjectSettingsPanel::DrawGeneral(ProjectConfig& c)
    {
        if (!UI::BeginPropertyGrid("general"))
            return;
        UI::PropertyLabel("Loyiha nomi", "Loyihaning nomi (Player bo'limida mahsulot nomi berilmasa - oyna sarlavhasi va SaveData papkasi ham). "
                                         "Loyiha faylining nomi o'zgarmaydi.");
        std::string name = c.Name;
        if (ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
        {
            if (!name.empty() && name != c.Name)
            {
                c.Name = name;
                m_Dirty = true;
            }
        }
        UI::PropertyLabel("O'yin", "Player bo'limidagi mahsulot nomi, versiya va kompaniya (Application::GetProductName / GetVersion)");
        ImGui::Text("%s %s%s", c.Player.GetProductName(c.Name).c_str(), c.Player.Version.c_str(),
                    c.Player.CompanyName.empty() ? "" : std::format("  ({})", c.Player.CompanyName).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(Tr(ICON_FA_WINDOW_MAXIMIZE "  Player")))
            m_Category = Category::Player;
        UI::PropertyLabel("Papka");
        ImGui::TextWrapped("%s", Project::GetProjectDirectory().string().c_str());
        UI::PropertyLabel("Loyiha fayli", "Format versiyasi: eski fayllar o'qilganda joriy formatga o'tkaziladi (saqlanganda shunday yoziladi); "
                                          "dvigatel tushunmagan kalitlar o'zgarmay saqlanadi");
        ImGui::TextUnformatted(Project::GetProjectFilePath().filename().string().c_str());
        ImGui::SameLine();
        if (c.FileVersion < ProjectMigration::CurrentVersion)
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.32f, 1.0f), "v%d -> v%d (saqlanganda)", c.FileVersion, ProjectMigration::CurrentVersion);
        else if (c.FileVersion > ProjectMigration::CurrentVersion)
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "v%d (yangiroq dvigatel)", c.FileVersion);
        else
            ImGui::TextDisabled("format v%d", c.FileVersion);
        if (!c.ExtraSections.empty())
        {
            UI::PropertyLabel("Noma'lum kalitlar", "Fayldagi bu dvigatel o'qimaydigan kalitlar (boshqa vositalar, yangiroq versiya): saqlanganda "
                                                   "o'zgarmay yoziladi");
            std::string keys;
            for (const auto& [key, text] : c.ExtraSections)
                keys += (keys.empty() ? "" : ", ") + key;
            ImGui::TextWrapped("%s", keys.c_str());
        }
        UI::PropertyLabel("Assets papkasi");
        ImGui::TextUnformatted(c.AssetDirectory.generic_string().c_str());
        UI::PropertyLabel("Skriptlar papkasi");
        ImGui::TextUnformatted(c.SourceDirectory.generic_string().c_str());
        UI::PropertyLabel("Skript moduli");
        ImGui::TextUnformatted(c.ScriptModulePath.empty() ? "-" : c.ScriptModulePath.generic_string().c_str());
        UI::PropertyLabel("Boshlang'ich sahna");
        ImGui::TextUnformatted(c.StartScene.empty() ? "-" : c.StartScene.generic_string().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(Tr(ICON_FA_LIST_OL "  Build sahnalar")))
            m_Category = Category::BuildScenes;
        UI::PropertyLabel("Sifat darajasi");
        ImGui::Text("%s  (o'yin: %s)", Quality::GetLevelName().empty() ? "-" : Quality::GetLevelName().c_str(),
                    c.Quality.Levels[(size_t)std::clamp(c.Quality.DefaultLevel, 0, (int)c.Quality.Levels.size() - 1)].Name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(Tr(ICON_FA_GAUGE_HIGH "  Sifat")))
            m_Category = Category::Quality;
        UI::EndPropertyGrid();
    }

    void ProjectSettingsPanel::DrawDebug(ProjectConfig& c)
    {
        if (!UI::BeginPropertyGrid("debug"))
            return;
        // Debug drawing in the player (the editor viewport has its own toggles in the toolbar).
        UI::PropertyHeader(ICON_FA_BUG "  Debug chizish (o'yinda)",
                           "Skriptlardagi DebugDraw chaqiruvlari (chiziq, quti, sfera, o'q, matn) va fizika ko'rinishlari "
                           "ZaminPlayer'da. Shipping build'da debug chizish umuman yo'q; --no-debug-draw bayrog'i ham o'chiradi. "
                           "Editor viewport'ining o'z sozlamalari bor (asboblar panelidagi " ICON_FA_BUG " tugma).");
        bool changed = false;
        DebugDrawSettings& d = c.DebugDraw;
        changed |= UI::PropertyBool("Yoqilgan", d.Enabled);
        ImGui::BeginDisabled(!d.Enabled);
        changed |= UI::PropertyBool("Raycastlar", d.ShowRaycasts);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Har bir Raycast2D / Raycast3D so'rovi: nur, tegish nuqtasi va normal (tegmasa qizil)");
        if (d.ShowRaycasts)
            changed |= UI::PropertyFloat("Raycast vaqti (s)", d.RaycastDuration, 0.01f, 0.0f, DebugDrawSettings::MaxRaycastDuration, "%.2f");
        changed |= UI::PropertyBool("Kontakt nuqtalari", d.ShowContacts);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Fizik jismlar tegib turgan nuqtalar va normallari (2D va 3D): uxlayotganlari xira, qizil chiziq - shakllar "
                        "qancha kirgani, halqa - 3D zarba; --debug-contacts bayrog'i ham yoqadi");
        changed |= UI::PropertyBool("Joint'lar", d.ShowJoints);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ishlayotgan 2D / 3D joint'lar: langarlar, o'qlar, uzilishga yaqinligi rangda; --debug-joints bayrog'i ham yoqadi");
        if (d.ShowContacts || d.ShowJoints)
        {
            changed |= UI::PropertyBool("Fizika qiymatlari", d.ShowContactDetails);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Kontaktlar ustida chuqurlik, zarba tezligi va impuls, joint'larda kuch / moment yozuvlari; "
                            "--debug-contact-details bayrog'i ham yoqadi");
        }
        changed |= UI::PropertyBool("Personaj tezligi", d.ShowVelocities);
        if (ImGui::IsItemHovered())
            UI::Tooltip("3D character controller'larning tezlik vektori (o'q va tezlik, m/s); --debug-velocities bayrog'i ham yoqadi");
        changed |= UI::PropertyBool("Jismlar tezligi", d.ShowBodyVelocities);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Harakatlanayotgan dinamik jismlarning (2D va 3D) tezlik vektori");
        if (d.ShowVelocities || d.ShowBodyVelocities)
            changed |= UI::PropertyFloat("Tezlik o'qi (s)", d.VelocityScale, 0.01f, DebugDrawSettings::MinVelocityScale,
                                         DebugDrawSettings::MaxVelocityScale, "%.2f");
        changed |= UI::PropertyBool("Navigatsiya", d.ShowNavigation);
        if (ImGui::IsItemHovered())
            UI::Tooltip("NavGrid'larning yopiq (qizil) va to'siq yopgan (to'q sariq) kataklari, agentlar yo'li va manzili, mo'ylov nurlari, "
                        "Nav Obstacle shakllari, ko'rish konuslari va patrul yo'llari; --debug-nav bayrog'i ham yoqadi");
        if (d.ShowNavigation)
        {
            changed |= UI::PropertyBool("Yuriladigan kataklar", d.ShowNavigationWalkable);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Yuriladigan kataklar ham (yashil); --debug-nav-cells bayrog'i ham yoqadi");
        }
        changed |= UI::PropertyFloat("Chiziq qalinligi (px)", d.LineWidth, 0.05f, DebugDrawSettings::MinLineWidth, DebugDrawSettings::MaxLineWidth, "%.1f");
        changed |= UI::PropertyFloat("Matn o'lchami (px)", d.TextSize, 0.1f, DebugDrawSettings::MinTextSize, DebugDrawSettings::MaxTextSize, "%.0f");
        ImGui::EndDisabled();
        if (changed)
        {
            d.Sanitize();
            m_Dirty = true;
        }
        UI::EndPropertyGrid();
    }

    // ------------------------------------------------------------------ build scenes
    void ProjectSettingsPanel::DrawBuildScenes(ProjectConfig& config)
    {
        auto& list = config.BuildScenes;
        ImGui::TextWrapped("O'yin shu sahnalardan quriladi. Yoqilganlar tartib bo'yicha build indeksini oladi (0, 1, 2 ...): skriptda "
                           "LoadScene(1) yoki nomi bilan LoadScene(\"Level2\"). Ro'yxat bo'sh bo'lmasa, eksportga faqat shu sahnalar "
                           "(va boshlang'ich sahna) kiradi. Tartibni sudrab o'zgartiring (o'ng tugma: menyu).");
        ImGui::Spacing();

        // ---- the start scene: what the player opens first (any scene of the project, or a dropped one).
        if (UI::BeginPropertyGrid("startScene"))
        {
            UI::PropertyLabel("Boshlang'ich sahna", "O'yin shu sahnadan boshlanadi (player). Fayli topilmasa - birinchi yoqilgan "
                                                                 "build sahnasidan. Content Browser'dan .zscene tortib tashlash ham mumkin");
            const std::string current = config.StartScene.generic_string();
            std::error_code ec;
            const bool startExists = !current.empty() && std::filesystem::is_regular_file(Project::GetAssetFileSystemPath(config.StartScene), ec);
            const float buttons = (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x) * 2.0f;
            ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - buttons));
            if (!current.empty() && !startExists)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
            const std::string preview = current.empty() ? std::string("(tanlanmagan)") : current;
            const bool comboOpen = ImGui::BeginCombo("##startScene", preview.c_str(), ImGuiComboFlags_HeightLarge);
            if (!current.empty() && !startExists)
                ImGui::PopStyleColor();
            if (comboOpen)
            {
                // Build scenes first (with their indices), then the other scenes of the project.
                std::vector<std::string> scenes;
                for (auto it = std::filesystem::recursive_directory_iterator(Project::GetAssetDirectory(), ec);
                     !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                    if (it->path().extension() == ".zscene")
                        scenes.push_back(Project::GetAssetRelativePath(it->path()));
                std::sort(scenes.begin(), scenes.end());
                std::stable_partition(scenes.begin(), scenes.end(), [&](const std::string& scene) { return Project::GetBuildIndex(scene) >= 0; });
                std::stable_sort(scenes.begin(), scenes.end(), [&](const std::string& a, const std::string& b) {
                    const int ia = Project::GetBuildIndex(a), ib = Project::GetBuildIndex(b);
                    return ia >= 0 && ib >= 0 ? ia < ib : ia >= 0 && ib < 0;
                });
                for (const std::string& scene : scenes)
                {
                    const int index = Project::GetBuildIndex(scene);
                    const std::string label = index >= 0 ? std::format("{}  {}##{}", index, scene, scene) : std::format("    {}##{}", scene, scene);
                    if (ImGui::Selectable(label.c_str(), scene == current) && scene != current)
                    {
                        config.StartScene = scene;
                        config.StartSceneHandle = {};
                        m_Dirty = true;
                    }
                }
                if (scenes.empty())
                    ImGui::TextDisabled("Loyihada sahna yo'q");
                ImGui::EndCombo();
            }
            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload* active = ImGui::GetDragDropPayload();
                if (active && active->IsDataType("CONTENT_BROWSER_ITEM") && std::filesystem::path((const char*)active->Data).extension() == ".zscene")
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        std::filesystem::path dropped((const char*)payload->Data);
                        config.StartScene = dropped.is_absolute() ? std::filesystem::path(Project::GetAssetRelativePath(dropped)) : dropped;
                        config.StartSceneHandle = {};
                        m_Dirty = true;
                    }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BUILD_SCENE"))
                {
                    const int index = *(const int*)payload->Data;
                    if (index >= 0 && index < (int)list.size())
                    {
                        config.StartScene = list[(size_t)index].Path;
                        config.StartSceneHandle = {};
                        m_Dirty = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!startExists || !OnOpenSceneRequested);
            if (UI::ToolbarButton(ICON_FA_FOLDER_OPEN, "Boshlang'ich sahnani ochish"))
                OnOpenSceneRequested(config.StartScene);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(m_OpenScenePath.empty() || m_OpenScenePath.generic_string() == current);
            if (UI::ToolbarButton(ICON_FA_HOUSE, "Ochiq sahnani boshlang'ich qilish"))
            {
                config.StartScene = m_OpenScenePath;
                config.StartSceneHandle = {};
                m_Dirty = true;
            }
            ImGui::EndDisabled();
            UI::EndPropertyGrid();
        }
        ImGui::Spacing();

        // Add: the open scene, every scene of the project, or a .zscene dropped from the content browser.
        const std::string open = m_OpenScenePath.generic_string();
        const bool openListed = std::any_of(list.begin(), list.end(), [&](const BuildSceneEntry& e) { return e.Path.generic_string() == open; });
        ImGui::BeginDisabled(open.empty() || openListed);
        if (ImGui::Button(Tr(ICON_FA_PLUS "  Ochiq sahnani qo'shish")) && Project::AddBuildScene(m_OpenScenePath))
            m_Dirty = true;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip(open.empty() ? "Ochiq sahna hali saqlanmagan" : openListed ? "Ochiq sahna ro'yxatda bor" : open.c_str());
        const char* addAllLabel = ICON_FA_LAYER_GROUP "  Barcha sahnalarni qo'shish";
        UI::SameLineIfFits(addAllLabel);
        if (ImGui::Button(addAllLabel))
        {
            std::vector<std::string> found;
            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(Project::GetAssetDirectory(), ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                if (it->path().extension() == ".zscene")
                    found.push_back(Project::GetAssetRelativePath(it->path()));
            std::sort(found.begin(), found.end());
            size_t added = 0;
            for (const std::string& scene : found)
                added += Project::AddBuildScene(scene) ? 1 : 0;
            if (added)
                m_Dirty = true;
        }
        const char* clearLabel = ICON_FA_TRASH "  Tozalash";
        UI::SameLineIfFits(clearLabel);
        ImGui::BeginDisabled(list.empty());
        if (ImGui::Button(clearLabel))
        {
            list.clear();
            m_Dirty = true;
        }
        ImGui::EndDisabled();
        if (OnScenesPanelRequested)
        {
            const char* scenesLabel = ICON_FA_CLAPPERBOARD "  Sahnalar oynasi";
            UI::SameLineIfFits(scenesLabel);
            if (ImGui::Button(scenesLabel))
                OnScenesPanelRequested();
            if (ImGui::IsItemHovered())
                UI::Tooltip("Loyihaning hamma sahnalari: build ro'yxatiga qo'shish, boshlang'ich qilish, Play'da qo'shimcha sahnalar");
        }
        ImGui::Spacing();

        const std::string start = config.StartScene.generic_string();
        int remove = -1, moveFrom = -1, moveTo = -1;
        int buildIndex = 0;
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
        const float button = ImGui::GetFrameHeight();
        if (ImGui::BeginTable("##buildScenes", 5, flags))
        {
            ImGui::TableSetupColumn("##on", ImGuiTableColumnFlags_WidthFixed, button * 1.6f);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00").x + 4.0f);
            ImGui::TableSetupColumn("Sahna", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##start", ImGuiTableColumnFlags_WidthFixed, button);
            ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, button);
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)list.size(); i++)
            {
                BuildSceneEntry& entry = list[(size_t)i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                // Drag handle + on / off.
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled(ICON_FA_GRIP_LINES);
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload("BUILD_SCENE", &i, sizeof(int));
                    ImGui::TextUnformatted(entry.Path.generic_string().c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::SameLine(0.0f, 2.0f);
                if (ImGui::Checkbox("##enabled", &entry.Enabled))
                    m_Dirty = true;
                if (ImGui::IsItemHovered())
                    UI::Tooltip(entry.Enabled ? "Buildga kiradi (o'chirilsa indeksi yo'qoladi)" : "O'chirilgan: buildga kirmaydi");
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                if (entry.Enabled)
                    ImGui::TextColored(UI::AccentColor(), "%d", buildIndex++);
                else
                    ImGui::TextDisabled("-");
                // The scene's name (its folder dimmed); drag to reorder, right-click for more.
                ImGui::TableSetColumnIndex(2);
                std::error_code ec;
                const bool exists = std::filesystem::exists(Project::GetAssetFileSystemPath(entry.Path), ec);
                const std::string folder = entry.Path.parent_path().generic_string();
                const std::string label = std::format("{}  {}", exists ? ICON_FA_EARTH_ASIA : ICON_FA_TRIANGLE_EXCLAMATION, entry.Path.stem().string());
                ImGui::PushStyleColor(ImGuiCol_Text, !exists ? ImVec4(0.95f, 0.45f, 0.40f, 1.0f)
                                                    : entry.Enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::AlignTextToFramePadding();
                ImGui::Selectable(label.c_str(), entry.Path.generic_string() == open, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap);
                ImGui::PopStyleColor();
                const bool rowHovered = ImGui::IsItemHovered();
                if (rowHovered)
                {
                    UI::Tooltip(!exists ? std::format("Fayl topilmadi: {}", entry.Path.generic_string()).c_str()
                                        : std::format("{}\nIkki marta bosing: ochish. Sudrab tartibni o'zgartiring, o'ng tugma: menyu",
                                                      entry.Path.generic_string()).c_str());
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && exists && OnOpenSceneRequested)
                        OnOpenSceneRequested(entry.Path);
                }
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("BUILD_SCENE", &i, sizeof(int));
                    ImGui::TextUnformatted(entry.Path.generic_string().c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BUILD_SCENE"))
                    {
                        moveFrom = *(const int*)payload->Data;
                        moveTo = i;
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::BeginPopupContextItem("##row"))
                {
                    if (ImGui::MenuItem(Tr(ICON_FA_ARROW_UP "  Yuqoriga"), nullptr, false, i > 0))
                    {
                        moveFrom = i;
                        moveTo = i - 1;
                    }
                    if (ImGui::MenuItem(Tr(ICON_FA_ARROW_DOWN "  Pastga"), nullptr, false, i + 1 < (int)list.size()))
                    {
                        moveFrom = i;
                        moveTo = i + 1;
                    }
                    if (ImGui::MenuItem(Tr(ICON_FA_STAR "  Boshlang'ich sahna qilish"), nullptr, false, entry.Path.generic_string() != start))
                    {
                        config.StartScene = entry.Path;
                        config.StartSceneHandle = {};
                        m_Dirty = true;
                    }
                    if (ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Ochish"), nullptr, false, exists && OnOpenSceneRequested != nullptr))
                        OnOpenSceneRequested(entry.Path);
                    ImGui::Separator();
                    if (ImGui::MenuItem(Tr(ICON_FA_XMARK "  Ro'yxatdan olib tashlash")))
                        remove = i;
                    ImGui::EndPopup();
                }
                if (!folder.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", folder.c_str());
                }
                // The start scene (the player starts with it).
                ImGui::TableSetColumnIndex(3);
                const bool isStart = entry.Path.generic_string() == start;
                ImGui::PushStyleColor(ImGuiCol_Text, isStart ? ImVec4(1.0f, 0.82f, 0.30f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (UI::ToolbarButton(ICON_FA_STAR, isStart ? "Boshlang'ich sahna (player shu bilan boshlanadi)" : "Boshlang'ich sahna qilish", false,
                                      ImVec2(button, 0)) && !isStart)
                {
                    config.StartScene = entry.Path;
                    config.StartSceneHandle = {};
                    m_Dirty = true;
                }
                ImGui::PopStyleColor();
                ImGui::TableSetColumnIndex(4);
                if (UI::ToolbarButton(ICON_FA_XMARK, "Ro'yxatdan olib tashlash", false, ImVec2(button, 0)))
                    remove = i;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (list.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Ro'yxat bo'sh: eksportga hamma sahnalar kiradi, build indekslari yo'q.");
        }
        // A scene file dropped from the content browser joins the list.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##buildDrop", ImVec2(std::max(1.0f, avail.x), std::max(30.0f, std::min(avail.y, 80.0f))));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::filesystem::path path((const char*)payload->Data);
                if (Project::AddBuildScene(path.is_absolute() ? std::filesystem::path(Project::GetAssetRelativePath(path)) : path))
                    m_Dirty = true;
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BUILD_SCENE"))
            {
                moveFrom = *(const int*)payload->Data;
                moveTo = (int)list.size() - 1;
            }
            ImGui::EndDragDropTarget();
        }
        if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo && moveFrom < (int)list.size() && moveTo < (int)list.size())
        {
            BuildSceneEntry moved = list[(size_t)moveFrom];
            list.erase(list.begin() + moveFrom);
            list.insert(list.begin() + moveTo, moved);
            m_Dirty = true;
        }
        else if (remove >= 0 && remove < (int)list.size())
        {
            list.erase(list.begin() + remove);
            m_Dirty = true;
        }
        // What is wrong with the start scene / the list (also checked before an export).
        const std::vector<BuildSceneIssue> issues = Project::CheckBuildScenes(config, Project::GetAssetDirectory());
        if (!issues.empty())
        {
            ImGui::Spacing();
            for (const BuildSceneIssue& issue : issues)
            {
                ImGui::PushID(&issue);
                ImGui::TextColored(issue.Error ? ImVec4(0.95f, 0.45f, 0.40f, 1.0f) : ImVec4(0.95f, 0.72f, 0.32f, 1.0f),
                                   "%s  %s", issue.Error ? ICON_FA_CIRCLE_EXCLAMATION : ICON_FA_TRIANGLE_EXCLAMATION, issue.Message.c_str());
                // A fix where there is an obvious one.
                if (issue.Type == BuildSceneIssue::Kind::StartSceneNotListed)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Tr(ICON_FA_PLUS "  Ro'yxatga qo'shish")) && Project::AddBuildScene(config.StartScene))
                        m_Dirty = true;
                }
                else if (issue.Type == BuildSceneIssue::Kind::StartSceneDisabled && issue.Entry >= 0 && issue.Entry < (int)list.size())
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Tr(ICON_FA_CHECK "  Yoqish")))
                    {
                        list[(size_t)issue.Entry].Enabled = true;
                        m_Dirty = true;
                    }
                }
                else if (issue.Type == BuildSceneIssue::Kind::Duplicate && issue.Entry >= 0 && issue.Entry < (int)list.size())
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Tr(ICON_FA_XMARK "  Takrorini olib tashlash")))
                    {
                        list.erase(list.begin() + issue.Entry);
                        m_Dirty = true;
                        ImGui::PopID();
                        break; // the indices of the other issues moved
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::Spacing();
        DrawExportSettings(config);
    }

    void ProjectSettingsPanel::DrawExportSettings(ProjectConfig& config)
    {
        ExportSettings& e = config.Export;
        bool changed = false;
        if (UI::BeginPropertyGrid("exportSettings"))
        {
            UI::PropertyHeader(ICON_FA_FILTER "  Eksport",
                               "Eksportga faqat o'yin ishlatadigan assetlar kiradi: build sahnalari va ular orqali kerak bo'lganlar (prefab, "
                               "material, model, klip ...), loyiha fayli (ikonka, splash) va C++ skriptlardagi \"Audio/jump.wav\" kabi "
                               "satrlar. Resources/ va til fayllari papkasi doim kiradi. Skript nomini o'zi yasaydigan assetlarni "
                               "\"Doim kiritish\" ro'yxatiga qo'shing. Chiqarib tashlanganlar ro'yxati: excluded-assets.txt.");
            changed |= UI::PropertyBool("Faqat ishlatilganlar", e.ExcludeUnusedAssets);
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'chirilsa, Assets papkasidagi hamma fayl eksportga kiradi (build ro'yxatida yo'q sahnalardan tashqari)");
            changed |= UI::PropertyBool("Prefab ogohlantirishlari - xato", e.PrefabWarningsAsErrors);
            if (ImGui::IsItemHovered())
                UI::Tooltip(Tr("Eksportdagi prefab tekshiruvining ogohlantirishlari (tashqi havolalar, noma'lum komponentlar, eskirgan "
                               "o'zgarishlar ...) ham eksportni muvaffaqiyatsiz qiladi"));
            UI::PropertyLabel("Doim kiritish", "Fayl yoki papka (Assets ichida); * istalgan qism: \"Levels/*.zscene\", \"Data/*\"");
            int remove = -1;
            for (size_t i = 0; i < e.AlwaysInclude.size(); i++)
            {
                ImGui::PushID((int)i);
                if (i > 0)
                    UI::PropertyLabel("");
                if (ImGui::SmallButton(ICON_FA_XMARK))
                    remove = (int)i;
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Ro'yxatdan olib tashlash");
                ImGui::SameLine();
                ImGui::TextUnformatted(ICON_FA_FOLDER_OPEN);
                ImGui::SameLine();
                ImGui::TextUnformatted(e.AlwaysInclude[i].c_str());
                ImGui::PopID();
            }
            if (!e.AlwaysInclude.empty())
                UI::PropertyLabel("");
            const float addWidth = ImGui::CalcTextSize(ICON_FA_PLUS).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - addWidth - ImGui::GetStyle().ItemSpacing.x));
            const bool enter = ImGui::InputTextWithHint("##newInclude", Tr("Data/  yoki  Levels/*.zscene"), &m_NewExportInclude,
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
            // Content Browser: a file or a folder dropped on the field.
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    std::filesystem::path path((const char*)payload->Data);
                    m_NewExportInclude = path.is_absolute() ? Project::GetAssetRelativePath(path) : path.generic_string();
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if ((ImGui::Button(ICON_FA_PLUS) || enter) && !m_NewExportInclude.empty())
            {
                std::string path = std::filesystem::path(m_NewExportInclude).lexically_normal().generic_string();
                while (path.size() > 1 && path.back() == '/')
                    path.pop_back();
                if (!path.empty() && path != "." && std::find(e.AlwaysInclude.begin(), e.AlwaysInclude.end(), path) == e.AlwaysInclude.end())
                {
                    e.AlwaysInclude.push_back(path);
                    changed = true;
                }
                m_NewExportInclude.clear();
            }
            if (remove >= 0)
            {
                e.AlwaysInclude.erase(e.AlwaysInclude.begin() + remove);
                changed = true;
            }
            UI::EndPropertyGrid();
        }
        if (changed)
            m_Dirty = true;

        // What the next export would take (made again when the settings change or on request).
        if (!m_HasExportPreview || !(m_ExportPreviewFor == e))
        {
            const ExportSettings saved = Project::GetActive() ? Project::GetActive()->GetConfig().Export : ExportSettings();
            if (Project::GetActive())
                Project::GetActive()->GetConfig().Export = e; // (the config being edited is the active one's copy or itself)
            m_ExportPreview = ExportAssets::PlanForActiveProject();
            if (Project::GetActive())
                Project::GetActive()->GetConfig().Export = saved;
            m_ExportPreviewFor = e;
            m_HasExportPreview = true;
        }
        ImGui::TextDisabled(ICON_FA_BOX "  Keyingi eksport: %zu ta fayl (%s)", m_ExportPreview.Included.size(),
                            ExportAssets::FormatSize(m_ExportPreview.IncludedBytes).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(Tr(ICON_FA_ROTATE "  Yangilash")))
            m_HasExportPreview = false;
        if (m_ExportPreview.Excluded.empty())
            ImGui::TextDisabled(ICON_FA_CHECK "  Hamma asset ishlatiladi - hech narsa chiqarib tashlanmaydi");
        else
        {
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.32f, 1.0f), ICON_FA_FILTER "  Chiqarib tashlanadi: %zu ta (%s)", m_ExportPreview.Excluded.size(),
                               ExportAssets::FormatSize(m_ExportPreview.ExcludedBytes).c_str());
            if (ImGui::BeginTable("##excludedAssets", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                                            ImGuiTableFlags_SizingStretchProp,
                                  ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * (float)std::min<size_t>(m_ExportPreview.Excluded.size() + 1, 9) + 4.0f)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(Tr("Asset"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn(Tr("Hajmi"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
                ImGui::TableSetupColumn(Tr("Sabab"), ImGuiTableColumnFlags_WidthStretch, 2.4f);
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < m_ExportPreview.Excluded.size(); i++)
                {
                    const ExcludedAsset& asset = m_ExportPreview.Excluded[i];
                    ImGui::PushID((int)i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(asset.Path.c_str());
                    if (ImGui::BeginPopupContextItem("##excludedMenu"))
                    {
                        if (ImGui::MenuItem(Tr(ICON_FA_PLUS "  Doim kiritish")))
                        {
                            e.AlwaysInclude.push_back(asset.Path);
                            m_Dirty = true;
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", ExportAssets::FormatSize(asset.Size).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", asset.Reason.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }

    // ------------------------------------------------------------------ tags and layers
    static std::string TrimText(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return {};
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    void ProjectSettingsPanel::ScanProjectUsage()
    {
        Ref<Project> project = Project::GetActive();
        m_UsageFor = project.get();
        m_ProjectUsage = SceneFileUsage();
        m_ProjectUsageValid = project != nullptr;
        if (!project)
            return;
        // The open scene is counted from memory (it may have unsaved changes).
        const std::filesystem::path open = m_OpenScenePath.empty() ? std::filesystem::path() : Project::GetAssetFileSystemPath(m_OpenScenePath);
        m_ProjectUsage = SceneFileReferences::ScanDirectory(Project::GetAssetDirectory(), open);
    }

    void ProjectSettingsPanel::RenameTag(ProjectConfig& config, int index, const std::string& name)
    {
        auto& tags = config.Tags;
        if (index < 0 || index >= (int)tags.size() || name.empty() || std::find(tags.begin(), tags.end(), name) != tags.end())
            return;
        const std::string old = tags[(size_t)index];
        // The open scene's entities keep the tag under its new name.
        size_t entities = 0;
        if (m_Scene && m_SceneEditable)
        {
            for (Entity e : m_Scene->FindEntitiesWithTag(old))
            {
                auto& c = e.GetComponent<TagsComponent>();
                if (c.Has(name)) c.Remove(old);
                else std::replace(c.Tags.begin(), c.Tags.end(), old, name);
                entities++;
            }
            if (entities > 0 && OnSceneModified)
                OnSceneModified();
        }
        // The other scenes and prefabs of the project, on disk.
        size_t files = 0;
        if (m_RenameInFiles && m_SceneEditable)
        {
            std::vector<std::string> errors;
            const std::filesystem::path open = m_OpenScenePath.empty() ? std::filesystem::path() : Project::GetAssetFileSystemPath(m_OpenScenePath);
            files = SceneFileReferences::RenameTagInDirectory(Project::GetAssetDirectory(), old, name, open, &errors);
            for (const std::string& error : errors)
                ZM_ERROR("Teg qayta nomlanmadi: {}", error);
        }
        tags[(size_t)index] = name;
        m_Dirty = true;
        m_ProjectUsageValid = false;
        ZM_INFO("Teg '{}' -> '{}': ochiq sahnada {} ta obyekt, {} ta boshqa sahna / prefab fayli yangilandi", old, name, entities, files);
        // The entities and files have the new name now, so "Bekor qilish" must not bring the old one
        // back into the list: the rename goes into the project file at once.
        if (entities > 0 || files > 0)
            CommitTagRename(old, name);
    }

    void ProjectSettingsPanel::CommitTagRename(const std::string& from, const std::string& to)
    {
        // Only the saved file's tag list changes; the other unsaved settings stay unsaved.
        std::vector<std::string>& saved = m_Saved.Tags;
        auto it = std::find(saved.begin(), saved.end(), from);
        if (it == saved.end())
            return; // a tag added since the last save: saved with the rest of the list
        if (std::find(saved.begin(), saved.end(), to) != saved.end())
            saved.erase(it); // the saved list already has the new name (removed from the list since)
        else
            *it = to;
        m_SavedSections = ProjectSerializer::ConfigToSections(m_Saved);
        const std::filesystem::path file = Project::GetProjectFilePath();
        std::error_code ec;
        if (file.empty() || !std::filesystem::is_regular_file(file, ec))
            return; // not saved yet: what is in memory counts as saved
        if (FileSystem::WriteTextFile(file, ProjectSerializer::ConfigToString(m_Saved)))
        {
            // The file is in the current format now.
            m_Saved.FileVersion = ProjectMigration::CurrentVersion;
            if (Ref<Project> project = Project::GetActive())
                project->GetConfig().FileVersion = ProjectMigration::CurrentVersion;
            ZM_INFO("Teg nomi loyiha fayliga darhol yozildi ({}): sahna va prefablar bilan bir xil bo'lsin", file.filename().string());
        }
        else
            ZM_ERROR("Teg nomi loyiha fayliga yozilmadi: {}", file.string());
    }

    void ProjectSettingsPanel::DrawTagsAndLayers(ProjectConfig& config)
    {
        if (m_UsageFor != Project::GetActive().get())
            m_ProjectUsageValid = false;
        if (!m_ProjectUsageValid)
            ScanProjectUsage();
        if (ImGui::BeginTabBar("##tagsAndLayers"))
        {
            auto flags = [&](int page) { return m_RequestedLayersPage == page ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
            if (ImGui::BeginTabItem(Tr(ICON_FA_TAGS "  Teglar"), nullptr, flags(0)))
            {
                m_LayersPage = 0;
                DrawTags(config);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Tr(ICON_FA_LAYER_GROUP "  Saralash qatlamlari"), nullptr, flags(1)))
            {
                m_LayersPage = 1;
                DrawSortingLayers(config);
                ImGui::EndTabItem();
            }
            m_RequestedLayersPage = -1;
            ImGui::EndTabBar();
        }
    }

    void ProjectSettingsPanel::DrawTags(ProjectConfig& config)
    {
        auto& tags = config.Tags;
        ImGui::TextWrapped("Teglar obyektlarni guruhlaydi (masalan, barcha dushmanlar). Inspektorda \"Teglar\" komponenti "
                           "bilan beriladi, skriptda HasTag(\"Enemy\") va FindEntitiesWithTag(\"Enemy\") bilan so'raladi. "
                           "Bu ro'yxat inspektordagi takliflar uchun; ro'yxatda yo'q teglar ham ishlaydi.");
        ImGui::Spacing();

        // Add a new tag
        std::string trimmed = TrimText(m_NewTag);
        const bool duplicate = std::find(tags.begin(), tags.end(), trimmed) != tags.end();
        ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x * 0.6f));
        bool submit = ImGui::InputTextWithHint("##newTag", Tr("Yangi teg nomi"), &m_NewTag, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(trimmed.empty() || duplicate);
        submit |= ImGui::Button(Tr(ICON_FA_PLUS "  Qo'shish"));
        ImGui::EndDisabled();
        if (submit && !trimmed.empty() && !duplicate)
        {
            tags.push_back(trimmed);
            m_NewTag.clear();
            m_Dirty = true;
        }
        if (duplicate && !trimmed.empty())
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Bu teg allaqachon bor");
        }

        // Tags used in the open scene or the project's files but missing from the list
        std::vector<std::string> missing;
        auto addMissing = [&](const std::string& t) {
            if (std::find(tags.begin(), tags.end(), t) == tags.end() && std::find(missing.begin(), missing.end(), t) == missing.end())
                missing.push_back(t);
        };
        if (m_Scene)
            for (auto& t : m_Scene->GetAllTags())
                addMissing(t);
        for (const auto& [t, count] : m_ProjectUsage.Tags)
            addMissing(t);
        if (!missing.empty())
        {
            const std::string label = std::format(ICON_FA_PLUS "  Ishlatilganlarni qo'shish ({})", missing.size());
            UI::SameLineIfFits(label.c_str());
            if (ImGui::Button(label.c_str()))
            {
                tags.insert(tags.end(), missing.begin(), missing.end());
                m_Dirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                std::string list;
                for (auto& t : missing) list += (list.empty() ? "" : ", ") + t;
                UI::Tooltip(("Sahnalarda ishlatilgan, ro'yxatda yo'q: " + list).c_str());
            }
        }
        ImGui::Spacing();

        if (tags.empty())
        {
            ImGui::TextDisabled("Ro'yxat bo'sh");
            return;
        }

        int remove = -1, moveUp = -1, rename = -1;
        std::string renameTo;
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("##tags", 4, flags))
        {
            ImGui::TableSetupColumn("Teg", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Ochiq sahnada", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.5f);
            ImGui::TableSetupColumn("Loyihada", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 3.4f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)tags.size(); i++)
            {
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const size_t used = m_Scene ? m_Scene->CountEntitiesWithTag(tags[i]) : 0;
                const size_t inFiles = m_ProjectUsage.GetTagCount(tags[i]);
                // Renaming also renames the tag on the scene's entities: only while editing.
                const bool canRename = m_SceneEditable || (used == 0 && inFiles == 0);
                if (m_RenamingTag == i && !canRename)
                    m_RenamingTag = -1; // play started while the rename field was open
                if (m_RenamingTag == i)
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (m_FocusTagRename)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_FocusTagRename = false;
                    }
                    if (ImGui::InputText("##rename", &m_TagRenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        std::string name = TrimText(m_TagRenameBuffer);
                        if (!name.empty() && std::find(tags.begin(), tags.end(), name) == tags.end())
                        {
                            rename = i;
                            renameTo = name;
                        }
                        m_RenamingTag = -1;
                    }
                    else if (ImGui::IsItemDeactivated() || ImGui::IsKeyPressed(ImGuiKey_Escape))
                        m_RenamingTag = -1;
                }
                else
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text(ICON_FA_TAG "  %s", tags[i].c_str());
                    UI::HighlightIfSearched(tags[i].c_str());
                    if (canRename && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        m_RenamingTag = i;
                        m_TagRenameBuffer = tags[i];
                        m_FocusTagRename = true;
                    }
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                if (used > 0) ImGui::Text("%zu ta obyekt", used);
                else ImGui::TextDisabled("-");
                ImGui::TableSetColumnIndex(2);
                ImGui::AlignTextToFramePadding();
                if (used + inFiles > 0)
                {
                    ImGui::Text("%zu", used + inFiles);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(std::format("Ochiq sahnada {}, boshqa {} ta sahna / prefab faylida {} ta obyekt", used,
                                                m_ProjectUsage.Files, inFiles).c_str());
                }
                else
                {
                    ImGui::TextDisabled("0");
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Hech bir sahna yoki prefabda ishlatilmagan");
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::BeginDisabled(i == 0);
                if (ImGui::Button(ICON_FA_ARROW_UP)) moveUp = i;
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) UI::Tooltip("Yuqoriga");
                ImGui::SameLine();
                ImGui::BeginDisabled(!canRename);
                if (ImGui::Button(ICON_FA_PEN))
                {
                    m_RenamingTag = i;
                    m_TagRenameBuffer = tags[i];
                    m_FocusTagRename = true;
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    UI::Tooltip(canRename ? (m_RenameInFiles ? "Qayta nomlash (ochiq sahnadagi obyektlarda va loyihadagi boshqa sahna / prefablarda ham)"
                                                             : "Qayta nomlash (ochiq sahnadagi obyektlarda ham)")
                                          : "Sahnada ishlatilgan tegni o'yin davomida qayta nomlab bo'lmaydi");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_TRASH)) remove = i;
                if (ImGui::IsItemHovered()) UI::Tooltip("Ro'yxatdan olib tashlash (obyektlardagi teglar qoladi)");
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (rename >= 0)
            RenameTag(config, rename, renameTo);
        if (moveUp > 0)
        {
            std::swap(tags[moveUp], tags[moveUp - 1]);
            m_Dirty = true;
        }
        if (remove >= 0)
        {
            tags.erase(tags.begin() + remove);
            if (m_RenamingTag == remove) m_RenamingTag = -1;
            m_Dirty = true;
        }

        ImGui::Spacing();
        ImGui::Checkbox(Tr("Qayta nomlashda loyihadagi boshqa sahna va prefablar ham yangilansin"), &m_RenameInFiles);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Assets ichidagi .zscene / .zprefab fayllardagi obyektlarning teglari ham yangi nomga o'tadi (fayllar darhol "
                        "yoziladi). Ochiq sahna xotirada o'zgaradi: uni saqlang. Obyektlar o'zgargan qayta nomlash loyiha fayliga ham "
                        "darhol yoziladi, shuning uchun 'Bekor qilish' eski nomni qaytarmaydi.");
        ImGui::TextDisabled("Loyiha: %zu ta sahna / prefab fayli, %zu ta obyekt (ochiq sahnadan tashqari)", m_ProjectUsage.Files,
                            m_ProjectUsage.Entities);
        ImGui::SameLine();
        if (ImGui::SmallButton(Tr(ICON_FA_ARROWS_ROTATE "  Qayta hisoblash")))
            ScanProjectUsage();
    }

    // How many 2D renderers of the scene are on each layer (by ID).
    static std::unordered_map<uint32_t, size_t> CountSortingLayerUse(Scene& scene)
    {
        std::unordered_map<uint32_t, size_t> used;
        for (auto e : scene.GetAllEntitiesWith<SpriteRendererComponent>())
            used[Entity(e, &scene).GetComponent<SpriteRendererComponent>().SortingLayer]++;
        for (auto e : scene.GetAllEntitiesWith<CircleRendererComponent>())
            used[Entity(e, &scene).GetComponent<CircleRendererComponent>().SortingLayer]++;
        for (auto e : scene.GetAllEntitiesWith<TextComponent>())
            used[Entity(e, &scene).GetComponent<TextComponent>().SortingLayer]++;
        for (auto e : scene.GetAllEntitiesWith<TilemapComponent>())
            used[Entity(e, &scene).GetComponent<TilemapComponent>().SortingLayer]++;
        for (auto e : scene.GetAllEntitiesWith<ParticleSystemComponent>())
            used[Entity(e, &scene).GetComponent<ParticleSystemComponent>().SortingLayer]++;
        return used;
    }

    void ProjectSettingsPanel::DrawSortingLayers(ProjectConfig& config)
    {
        SortingLayerList& layers = config.SortingLayers;
        ImGui::TextWrapped("2D renderer'lar (sprite, doira, matn, tilemap, particle) shu qatlamlarning birida chiziladi. Ro'yxat "
                           "tartibi - chizish tartibi: yuqoridagi qatlam orqada, pastdagisi hammasidan oldinda. Qatlam ichida "
                           "\"Qatlamdagi tartib\", keyin kameradan uzoqlik hal qiladi. Obyektlar qatlamni ID bilan eslaydi: "
                           "qayta nomlash va surish sahnalarni buzmaydi; o'chirilgan qatlamdagilar Default'da chiziladi.");
        ImGui::TextWrapped("\"Y\" belgilangan qatlam (top-down o'yinlar) avval Y bo'yicha saralanadi: ekranda yuqoriroq turgan "
                           "(Y katta) orqada, pastroqdagisi oldinda chiziladi. Saralash nuqtasi - obyektning pivot'i (oyoqlari); "
                           "shu qatlamdagi ota-obyekt ostidagi qismlar otaning Y'i bilan birga, o'zaro tartib bo'yicha chiziladi.");
        ImGui::Spacing();

        std::string trimmed = TrimText(m_NewSortingLayer);
        const bool duplicate = !trimmed.empty() && layers.FindByName(trimmed);
        const bool full = layers.GetCount() >= SortingLayerList::MaxLayers;
        ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x * 0.6f));
        bool submit = ImGui::InputTextWithHint("##newLayer", Tr("Yangi qatlam nomi"), &m_NewSortingLayer, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(trimmed.empty() || duplicate || full);
        submit |= ImGui::Button(Tr(ICON_FA_PLUS "  Qo'shish##layer"));
        ImGui::EndDisabled();
        if (submit && !trimmed.empty() && !duplicate && !full && layers.Add(trimmed))
        {
            m_NewSortingLayer.clear();
            m_Dirty = true;
        }
        if (duplicate)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Bu nom band");
        }
        ImGui::Spacing();

        const std::unordered_map<uint32_t, size_t> used = m_Scene ? CountSortingLayerUse(*m_Scene) : std::unordered_map<uint32_t, size_t>();
        int moveFrom = -1, moveTo = -1;
        uint32_t remove = ~0u;
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
        uint32_t ySortToggle = ~0u;
        if (ImGui::BeginTable("##sortingLayers", 4, flags))
        {
            const float buttons = 4.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 1.2f);
            ImGui::TableSetupColumn("Qatlam (sahnada / loyihada)", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 4.0f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, buttons);
            ImGui::TableHeadersRow();
            const auto& list = layers.GetLayers();
            for (int i = 0; i < (int)list.size(); i++)
            {
                const SortingLayer& layer = list[(size_t)i];
                ImGui::PushID((int)layer.ID);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%d", i + 1);
                ImGui::TableSetColumnIndex(1);
                if (m_RenamingLayer == layer.ID)
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (m_FocusLayerRename)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_FocusLayerRename = false;
                    }
                    if (ImGui::InputText("##rename", &m_LayerRenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        if (layers.Rename(layer.ID, TrimText(m_LayerRenameBuffer)))
                            m_Dirty = true;
                        m_RenamingLayer = ~0u;
                    }
                    else if (ImGui::IsItemDeactivated() || ImGui::IsKeyPressed(ImGuiKey_Escape))
                        m_RenamingLayer = ~0u;
                }
                else
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text(ICON_FA_LAYER_GROUP "  %s", layer.Name.c_str());
                    if (ImGui::IsItemHovered())
                    {
                        if (ImGui::IsMouseDoubleClicked(0))
                        {
                            m_RenamingLayer = layer.ID;
                            m_LayerRenameBuffer = layer.Name;
                            m_FocusLayerRename = true;
                        }
                        UI::Tooltip(layer.ID == SortingLayerList::DefaultID ? "Asosiy qatlam (o'chirib bo'lmaydi). Ikki marta bosing: qayta nomlash"
                                                                            : "Ikki marta bosing: qayta nomlash");
                    }
                    UI::HighlightIfSearched(layer.Name.c_str());
                    // How many renderers use it: in the open scene, and in the project's other files.
                    auto it = used.find(layer.ID);
                    const size_t inScene = it != used.end() ? it->second : 0;
                    const size_t inFiles = m_ProjectUsage.GetSortingLayerCount(layer.ID);
                    ImGui::SameLine();
                    if (inScene + inFiles > 0)
                        ImGui::TextDisabled("%zu / %zu", inScene, inScene + inFiles);
                    else
                        ImGui::TextDisabled("-");
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(std::format("Ochiq sahnada {} ta, butun loyihada {} ta 2D renderer (sprite, doira, matn, tilemap, "
                                                "particle)", inScene, inScene + inFiles).c_str());
                }
                ImGui::TableSetColumnIndex(2);
                bool ySort = layer.YSort;
                if (ImGui::Checkbox("##ysort", &ySort))
                    ySortToggle = layer.ID;
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Y bo'yicha saralash (top-down): Y katta - orqada, kichik - oldinda. Tartib faqat bir xil "
                                "Y'dagilarni (bitta qahramon qismlarini) ajratadi");
                ImGui::TableSetColumnIndex(3);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::BeginDisabled(i == 0);
                if (ImGui::Button(ICON_FA_ARROW_UP)) { moveFrom = i; moveTo = i - 1; }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) UI::Tooltip("Orqaroqqa (oldinroq chiziladi)");
                ImGui::SameLine();
                ImGui::BeginDisabled(i + 1 == (int)list.size());
                if (ImGui::Button(ICON_FA_ARROW_DOWN)) { moveFrom = i; moveTo = i + 1; }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) UI::Tooltip("Oldinroqqa (keyinroq chiziladi)");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_PEN))
                {
                    m_RenamingLayer = layer.ID;
                    m_LayerRenameBuffer = layer.Name;
                    m_FocusLayerRename = true;
                }
                if (ImGui::IsItemHovered()) UI::Tooltip("Qayta nomlash (obyektlar qatlamni ID bilan eslaydi)");
                ImGui::SameLine();
                ImGui::BeginDisabled(layer.ID == SortingLayerList::DefaultID);
                if (ImGui::Button(ICON_FA_TRASH)) remove = layer.ID;
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    UI::Tooltip(layer.ID == SortingLayerList::DefaultID ? "Default qatlamini o'chirib bo'lmaydi"
                                                                        : "O'chirish (undagi obyektlar Default'da chiziladi)");
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ySortToggle != ~0u && layers.SetYSort(ySortToggle, !layers.IsYSorted(ySortToggle)))
            m_Dirty = true;
        if (moveFrom >= 0 && layers.Move((size_t)moveFrom, (size_t)moveTo))
            m_Dirty = true;
        if (remove != ~0u && layers.Remove(remove))
        {
            if (m_RenamingLayer == remove) m_RenamingLayer = ~0u;
            m_Dirty = true;
        }
    }

    // ------------------------------------------------------------------ save data
    void ProjectSettingsPanel::DrawSaveData()
    {
        namespace fs = std::filesystem;
        const fs::path file = SaveData::GetFilePath();

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("O'yin skriptlari SaveData::SetInt / SetFloat / SetString / SetBool bilan saqlagan qiymatlar "
                           "(o'yinlar orasida saqlanadi).");
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Fayl:");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", file.string().c_str());

        if (ImGui::Button(Tr(ICON_FA_FLOPPY_DISK "  Faylga yozish")))
            SaveData::Save();
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_ARROWS_ROTATE "  Qayta o'qish")))
            SaveData::Reload();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Saqlanmagan o'zgarishlarni bekor qilib, faylni qaytadan o'qish");
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_FOLDER_OPEN "  Papkani ochish")))
        {
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            Platform::OpenInFileBrowser(file.parent_path());
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_TRASH "  Hammasini o'chirish")))
            ImGui::OpenPopup("ClearSaveData");
        if (ImGui::BeginPopup("ClearSaveData"))
        {
            ImGui::TextUnformatted("Barcha saqlangan qiymatlar o'chirilsinmi? (fayl ham yangilanadi)");
            if (ImGui::Button(Tr("Ha, o'chirish")))
            {
                SaveData::DeleteAll();
                SaveData::Save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Yo'q")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (SaveData::IsDirty())
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Faylga hali yozilmagan o'zgarishlar bor");
        ImGui::Spacing();

        // ---- values
        const std::vector<std::string> keys = SaveData::GetKeys();
        std::string remove;
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
        if (keys.empty())
            ImGui::TextDisabled("Hozircha hech narsa saqlanmagan");
        else if (ImGui::BeginTable("##saveData", 4, tableFlags))
        {
            ImGui::TableSetupColumn("Kalit", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("Tur", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.8f);
            ImGui::TableSetupColumn("Qiymat", ImGuiTableColumnFlags_WidthStretch, 0.62f);
            ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableHeadersRow();
            for (const std::string& key : keys)
            {
                ImGui::PushID(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(key.c_str());
                ImGui::TableNextColumn();
                SaveValueType type = SaveData::GetType(key);
                ImGui::TextDisabled("%s", SaveData::TypeToString(type));
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                switch (type)
                {
                    case SaveValueType::Int:
                    {
                        int v = SaveData::GetInt(key);
                        if (ImGui::InputInt("##v", &v))
                            SaveData::SetInt(key, v);
                        break;
                    }
                    case SaveValueType::Float:
                    {
                        float v = SaveData::GetFloat(key);
                        if (ImGui::DragFloat("##v", &v, 0.01f, 0.0f, 0.0f, "%.4g"))
                            SaveData::SetFloat(key, v);
                        break;
                    }
                    case SaveValueType::String:
                    {
                        std::string v = SaveData::GetString(key);
                        if (ImGui::InputText("##v", &v))
                            SaveData::SetString(key, v);
                        break;
                    }
                    case SaveValueType::Bool:
                    {
                        bool v = SaveData::GetBool(key);
                        if (ImGui::Checkbox("##v", &v))
                            SaveData::SetBool(key, v);
                        break;
                    }
                    default:
                        break;
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(ICON_FA_TRASH, ImVec2(ImGui::GetFrameHeight(), 0)))
                    remove = key;
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Kalitni o'chirish");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!remove.empty())
            SaveData::DeleteKey(remove);

        // ---- new value
        ImGui::Spacing();
        ImGui::SeparatorText(Tr(ICON_FA_PLUS "  Yangi qiymat"));
        static const char* s_TypeNames[] = { "Int", "Float", "String", "Bool" };
        const float typeWidth = ImGui::GetFontSize() * 6.0f;
        ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - typeWidth - ImGui::GetFontSize() * 6.5f));
        bool submit = ImGui::InputTextWithHint("##newKey", Tr("kalit nomi"), &m_NewSaveKey, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(typeWidth);
        ImGui::Combo("##newType", &m_NewSaveType, s_TypeNames, IM_ARRAYSIZE(s_TypeNames));
        ImGui::SameLine();
        const bool exists = !m_NewSaveKey.empty() && SaveData::HasKey(m_NewSaveKey);
        ImGui::BeginDisabled(m_NewSaveKey.empty() || exists);
        submit |= ImGui::Button(Tr(ICON_FA_PLUS "  Qo'shish"));
        ImGui::EndDisabled();
        if (exists)
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Bu kalit allaqachon bor");
        if (submit && !m_NewSaveKey.empty() && !exists)
        {
            switch (m_NewSaveType)
            {
                case 0: SaveData::SetInt(m_NewSaveKey, 0); break;
                case 1: SaveData::SetFloat(m_NewSaveKey, 0.0f); break;
                case 2: SaveData::SetString(m_NewSaveKey, ""); break;
                default: SaveData::SetBool(m_NewSaveKey, false); break;
            }
            m_NewSaveKey.clear();
        }
    }

}
