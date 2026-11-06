#include "ScenesPanel.h"

#include "../UI.h"

#include <Zamin/ImGui/Icons.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>

namespace Zamin {

    namespace fs = std::filesystem;

    static const ImVec4 s_OldColor{ 0.95f, 0.75f, 0.35f, 1.0f };
    static const ImU32 s_StarColor = IM_COL32(255, 205, 70, 255);

    static std::string Lower(std::string_view s)
    {
        std::string out(s);
        for (char& c : out)
            c = (char)std::tolower((unsigned char)c);
        return out;
    }

    static size_t CountEntities(Scene& scene)
    {
        return scene.GetRegistry().view<IDComponent>().size();
    }

    // ------------------------------------------------------------------ the project's scene files
    void ScenesPanel::Refresh(bool force)
    {
        if (!Project::GetActive())
        {
            m_Items.clear();
            return;
        }
        const double now = Platform::GetTime();
        if (!force && !m_Dirty && now - m_LastScan < 3.0)
            return;
        m_LastScan = now;
        m_Dirty = false;
        for (Item& item : m_Items)
            item.Seen = false;
        const fs::path assets = Project::GetAssetDirectory();
        std::error_code ec;
        bool added = false;
        for (auto it = fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code fec;
            const std::string name = it->path().filename().string();
            if (name.starts_with("."))
            {
                if (it->is_directory(fec))
                    it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(fec) || it->path().extension() != ".zscene")
                continue;
            const fs::path path = it->path().lexically_normal();
            const uintmax_t size = it->file_size(fec);
            const fs::file_time_type time = it->last_write_time(fec);
            auto found = std::find_if(m_Items.begin(), m_Items.end(), [&](const Item& item) { return item.Path == path; });
            bool fresh = false;
            if (found == m_Items.end())
            {
                Item item;
                item.Path = path;
                item.Relative = Project::GetAssetRelativePath(path);
                m_Items.push_back(std::move(item));
                found = std::prev(m_Items.end());
                fresh = added = true;
            }
            found->Seen = true;
            if (fresh || found->Size != size || found->Time != time)
            {
                found->Size = size;
                found->Time = time;
                SceneSerializer::ReadSceneInfo(path, found->Info);
            }
            else if (fs::path thumbnail = SceneMetadata::GetThumbnailPath(path); found->Info.Thumbnail.empty() && fs::is_regular_file(thumbnail, fec))
                found->Info.Thumbnail = thumbnail; // drawn after the scene was read
        }
        std::erase_if(m_Items, [](const Item& item) { return !item.Seen; });
        if (added || force)
            std::sort(m_Items.begin(), m_Items.end(), [](const Item& a, const Item& b) { return AssetFiles::CompareNames(a.Relative, b.Relative) < 0; });
    }

    const ScenesPanel::Item* ScenesPanel::FindItem(const std::string& relative) const
    {
        auto it = std::find_if(m_Items.begin(), m_Items.end(), [&](const Item& item) { return item.Relative == relative; });
        return it != m_Items.end() ? &*it : nullptr;
    }

    // ------------------------------------------------------------------ build list / start scene
    int ScenesPanel::FindBuildEntry(const std::string& relative)
    {
        Ref<Project> project = Project::GetActive();
        if (!project)
            return -1;
        const auto& list = project->GetConfig().BuildScenes;
        for (size_t i = 0; i < list.size(); i++)
            if (list[i].Path.generic_string() == relative)
                return (int)i;
        return -1;
    }

    void ScenesPanel::AddToBuild(const std::string& relative)
    {
        if (Project::AddBuildScene(relative))
        {
            ZM_INFO("Build ro'yxatiga qo'shildi: {} (indeks {})", relative, Project::GetBuildIndex(relative));
            Modified();
        }
    }

    void ScenesPanel::RemoveFromBuild(const std::string& relative)
    {
        const int index = FindBuildEntry(relative);
        if (index < 0)
            return;
        auto& list = Project::GetActive()->GetConfig().BuildScenes;
        list.erase(list.begin() + index);
        ZM_INFO("Build ro'yxatidan olib tashlandi: {}", relative);
        Modified();
    }

    void ScenesPanel::SetBuildEnabled(const std::string& relative, bool enabled)
    {
        const int index = FindBuildEntry(relative);
        if (index < 0)
            return;
        Project::GetActive()->GetConfig().BuildScenes[(size_t)index].Enabled = enabled;
        Modified();
    }

    bool ScenesPanel::IsStartScene(const std::string& relative)
    {
        Ref<Project> project = Project::GetActive();
        return project && project->GetConfig().StartScene.generic_string() == relative;
    }

    void ScenesPanel::SetStartScene(const std::string& relative)
    {
        Ref<Project> project = Project::GetActive();
        if (!project || IsStartScene(relative))
            return;
        project->GetConfig().StartScene = relative;
        project->GetConfig().StartSceneHandle = {}; // written from the asset registry when saved
        ZM_INFO("Boshlang'ich sahna: {}", relative);
        Modified();
    }

    void ScenesPanel::SetPlayAdditive(const std::string& relative, bool on)
    {
        auto it = std::find(m_PlayAdditive.begin(), m_PlayAdditive.end(), relative);
        if (on && it == m_PlayAdditive.end())
            m_PlayAdditive.push_back(relative);
        else if (!on && it != m_PlayAdditive.end())
            m_PlayAdditive.erase(it);
    }

    void ScenesPanel::Modified()
    {
        if (OnProjectModified)
            OnProjectModified();
    }

    // ------------------------------------------------------------------ drawing
    void ScenesPanel::OnImGuiRender(bool* open)
    {
        if (open && !*open)
            return;
        if (m_FocusRequested)
        {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        if (m_DefaultDock)
            ImGui::SetNextWindowDockID(m_DefaultDock, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_FA_CLAPPERBOARD "  Sahnalar###ScenesPanel", open))
        {
            ImGui::End();
            return;
        }
        if (!Project::GetActive())
        {
            ImGui::TextDisabled("Loyiha ochilmagan");
            ImGui::End();
            return;
        }
        Refresh(false);
        DrawToolbar();
        if (m_SceneManager)
        {
            // A wide window: the game's scenes and its global state side by side.
            if (ImGui::GetContentRegionAvail().x > 720.0f &&
                ImGui::BeginTable("##playSplit", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                DrawRuntimeScenes();
                ImGui::TableNextColumn();
                DrawGlobalState();
                ImGui::EndTable();
            }
            else
            {
                DrawRuntimeScenes();
                DrawGlobalState();
            }
        }

        // The project's scenes: the build list's first (in build order), then the others by name.
        std::vector<const Item*> shown;
        const std::string search = Lower(m_Search);
        for (const Item& item : m_Items)
        {
            const int build = FindBuildEntry(item.Relative);
            if (m_OnlyBuild && build < 0)
                continue;
            if (!search.empty())
            {
                std::string haystack = Lower(item.Relative + " " + item.Info.Name + " " + item.Info.Metadata.Description + " " + item.Info.Metadata.Author);
                for (const std::string& tag : item.Info.Metadata.Tags)
                    haystack += " #" + Lower(tag);
                if (haystack.find(search) == std::string::npos)
                    continue;
            }
            shown.push_back(&item);
        }
        std::stable_sort(shown.begin(), shown.end(), [](const Item* a, const Item* b) {
            const int ia = FindBuildEntry(a->Relative), ib = FindBuildEntry(b->Relative);
            if ((ia >= 0) != (ib >= 0))
                return ia >= 0;
            return ia >= 0 && ia < ib;
        });

        if (m_SceneManager)
            ImGui::SeparatorText(ICON_FA_FOLDER_OPEN "  Loyiha sahnalari");
        DrawSelectedActions();
        if (ImGui::BeginChild("##sceneCards", ImVec2(0, 0), ImGuiChildFlags_None))
        {
            if (shown.empty())
                ImGui::TextDisabled(m_Items.empty() ? "Loyihada sahna yo'q" : "Qidiruvga mos sahna yo'q");
            else if (m_ListView)
                DrawList(shown);
            else
                DrawCards(shown);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void ScenesPanel::DrawToolbar()
    {
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x * 0.35f));
        ImGui::InputTextWithHint("##sceneSearch", ICON_FA_MAGNIFYING_GLASS "  nom, ta'rif, #teg", &m_Search);
        ImGui::SameLine();
        ImGui::Checkbox("Faqat build", &m_OnlyBuild);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Faqat build ro'yxatidagi sahnalar (Loyiha sozlamalari > Build sahnalar)");
        ImGui::SameLine();
        if (ImGui::SmallButton(m_ListView ? ICON_FA_TABLE_CELLS "##grid" : ICON_FA_LIST "##list"))
            m_ListView = !m_ListView;
        if (ImGui::IsItemHovered())
            UI::Tooltip(m_ListView ? "Kartalar" : "Ro'yxat");
        if (!m_ListView)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderFloat("##cardSize", &m_CardWidth, 120.0f, 320.0f, "%.0f");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_CAMERA "##thumbnails") && OnRenderThumbnails)
        {
            // The scenes without a picture (Shift: every scene).
            const bool all = ImGui::GetIO().KeyShift;
            std::vector<fs::path> scenes;
            for (const Item& item : m_Items)
                if (all || item.Info.Thumbnail.empty())
                    scenes.push_back(item.Path);
            if (scenes.empty())
                ZM_INFO("Hamma sahnaning rasmi bor (Shift + bosish: hammasini qayta chizish)");
            else
                OnRenderThumbnails(scenes);
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Rasmi yo'q sahnalarning rasmini chizish (Shift: hammasini qayta)");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_ROTATE "##rescan"))
            Refresh(true);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Sahna fayllarini qayta o'qish");
        ImGui::SameLine();
        const auto& build = Project::GetActive()->GetConfig().BuildScenes;
        const size_t enabled = (size_t)std::count_if(build.begin(), build.end(), [](const BuildSceneEntry& e) { return e.Enabled; });
        ImGui::TextDisabled("%zu ta sahna, build: %zu", m_Items.size(), enabled);
    }

    void ScenesPanel::DrawRuntimeScenes()
    {
        ImGui::SeparatorText(ICON_FA_PLAY "  O'yinda yuklangan sahnalar");
        std::vector<Ref<Scene>> scenes = m_SceneManager->GetLoadedScenes();
        if (const Ref<Scene>& persistent = m_SceneManager->GetDontDestroyOnLoadScene())
            scenes.push_back(persistent);
        const Ref<Scene>& active = m_SceneManager->GetActiveScene();
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##loadedScenes", 5, flags))
        {
            ImGui::TableSetupColumn("##active", ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn("Sahna", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Build", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Obyekt", ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableHeadersRow();
            Ref<Scene> makeActive, unload, show;
            for (size_t i = 0; i < scenes.size(); i++)
            {
                const Ref<Scene>& scene = scenes[i];
                const bool persistent = m_SceneManager->IsDontDestroyOnLoadScene(scene.get());
                const bool isActive = scene == active;
                ImGui::PushID((int)i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (isActive)
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(s_StarColor), ICON_FA_STAR);
                else if (persistent)
                    ImGui::TextDisabled(ICON_FA_THUMBTACK);
                if (ImGui::IsItemHovered())
                    UI::Tooltip(isActive ? "Faol sahna: yangi obyektlar shu sahnaga tushadi, uning sozlamalari (osmon, post) chiziladi"
                                         : persistent ? "DontDestroyOnLoad: har Single yuklashdan omon qoladi" : "");
                ImGui::TableNextColumn();
                const bool shown = scene.get() == m_ShownScene;
                std::string label = persistent ? std::string(SceneManager::DontDestroyOnLoadSceneName) : scene->GetName();
                if (ImGui::Selectable(std::format("{}{}##row", label, shown ? "  " ICON_FA_EYE : "").c_str(), shown,
                                      ImGuiSelectableFlags_AllowDoubleClick))
                    show = scene;
                if (ImGui::IsItemHovered())
                {
                    const std::string path = m_SceneManager->GetScenePath(scene.get());
                    UI::Tooltip(std::format("{}\nBosing: ierarxiyada ko'rsatish", path.empty() ? std::string("(faylsiz)") : path).c_str());
                }
                ImGui::TableNextColumn();
                const int buildIndex = m_SceneManager->GetBuildIndex(scene.get());
                if (buildIndex >= 0)
                    ImGui::Text("#%d", buildIndex);
                else
                    ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                ImGui::Text("%zu", CountEntities(*scene));
                ImGui::TableNextColumn();
                if (!persistent)
                {
                    ImGui::BeginDisabled(isActive);
                    if (ImGui::SmallButton(ICON_FA_STAR "##makeActive"))
                        makeActive = scene;
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        UI::Tooltip(isActive ? "Faol sahna" : "Faol sahna qilish (SetActiveScene)");
                    ImGui::SameLine();
                    ImGui::BeginDisabled(m_SceneManager->GetSceneCount() <= 1);
                    if (ImGui::SmallButton(ICON_FA_XMARK "##unload"))
                        unload = scene;
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        UI::Tooltip(m_SceneManager->GetSceneCount() <= 1 ? "Oxirgi sahnani yopib bo'lmaydi (boshqasini Single yuklang)"
                                                                         : "Yopish (UnloadScene, kadr oxirida)");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (makeActive && m_SceneManager->SetActiveScene(makeActive))
                ZM_INFO("Faol sahna: {}", makeActive->GetName());
            if (unload && m_SceneManager->UnloadScene(unload))
                ZM_INFO("Sahna yopilmoqda: {}", unload->GetName());
            if (show && OnShowRuntimeScene)
                OnShowRuntimeScene(show);
        }
        for (const Ref<AsyncOperation>& op : m_SceneManager->GetAsyncOperations())
        {
            ImGui::TextDisabled(ICON_FA_SPINNER "  %s", op->GetPath().generic_string().c_str());
            ImGui::SameLine();
            ImGui::ProgressBar(op->GetProgress(), ImVec2(-FLT_MIN, 0.0f));
        }
        if (m_SceneManager->IsTransitioning())
            ImGui::TextDisabled(ICON_FA_CIRCLE_HALF_STROKE "  O'tish: %.0f%%", m_SceneManager->GetTransitionAlpha() * 100.0f);
        ImGui::Spacing();
    }

    void ScenesPanel::DrawGlobalState()
    {
        const size_t values = GameState::GetCount(), services = Services::GetCount();
        const std::string title = std::format(ICON_FA_DATABASE "  Global holat: {} ta qiymat, {} ta servis###globalState", values, services);
        if (!ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            return;
        if (values == 0 && services == 0)
        {
            ImGui::TextDisabled("Bo'sh: skriptlar GameState::Set... / Services::Register bilan to'ldiradi");
            ImGui::Spacing();
            return;
        }
        if (values > 6)
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##stateFilter", ICON_FA_MAGNIFYING_GLASS "  kalit", &m_StateFilter);
        }
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
        if (values > 0 && ImGui::BeginTable("##gameState", 3, flags))
        {
            ImGui::TableSetupColumn("Kalit", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("Qiymat", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("##persistent", ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableHeadersRow();
            const std::string filter = Lower(m_StateFilter);
            for (const std::string& key : GameState::GetKeys())
            {
                if (!filter.empty() && Lower(key).find(filter) == std::string::npos)
                    continue;
                const GameState::Value* value = GameState::Find(key);
                if (!value)
                    continue;
                ImGui::PushID(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(key.c_str());
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                // Edited live: the game's listeners hear it (WatchGameState).
                if (const int* i = std::get_if<int>(value))
                {
                    int v = *i;
                    if (ImGui::DragInt("##v", &v))
                        GameState::SetInt(key, v);
                }
                else if (const float* f = std::get_if<float>(value))
                {
                    float v = *f;
                    if (ImGui::DragFloat("##v", &v, 0.05f))
                        GameState::SetFloat(key, v);
                }
                else if (const bool* b = std::get_if<bool>(value))
                {
                    bool v = *b;
                    if (ImGui::Checkbox("##v", &v))
                        GameState::SetBool(key, v);
                }
                else if (const std::string* str = std::get_if<std::string>(value))
                {
                    std::string v = *str;
                    if (ImGui::InputText("##v", &v, ImGuiInputTextFlags_EnterReturnsTrue))
                        GameState::SetString(key, v);
                }
                ImGui::TableNextColumn();
                if (GameState::IsPersistent(key))
                {
                    ImGui::TextDisabled(ICON_FA_FLOPPY_DISK);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Doimiy: GameState::Save() bilan SaveData'ga yoziladi");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (services > 0)
        {
            std::string list;
            for (const std::string& name : Services::GetNames())
                list += (list.empty() ? "" : ", ") + name;
            ImGui::TextDisabled(ICON_FA_CUBES "  Servislar:");
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(list.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
    }

    void ScenesPanel::DrawSelectedActions()
    {
        const Item* item = FindItem(m_Selected);
        if (!item)
            return;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(item->Info.Name.c_str());
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Ochish") && OnOpenScene)
            OnOpenScene(item->Path);
        if (ImGui::IsItemHovered())
            UI::Tooltip(m_SceneManager ? "O'yin to'xtatiladi va sahna editorda ochiladi" : "Editorda ochish");
        ImGui::SameLine();
        const int entry = FindBuildEntry(item->Relative);
        if (entry < 0)
        {
            if (ImGui::Button(ICON_FA_PLUS "  Build'ga"))
                AddToBuild(item->Relative);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Build ro'yxatining oxiriga qo'shish (LoadScene(indeks))");
        }
        else if (ImGui::Button(ICON_FA_MINUS "  Build'dan"))
            RemoveFromBuild(item->Relative);
        ImGui::SameLine();
        ImGui::BeginDisabled(IsStartScene(item->Relative));
        if (ImGui::Button(ICON_FA_HOUSE "  Boshlang'ich"))
            SetStartScene(item->Relative);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip(IsStartScene(item->Relative) ? "Bu boshlang'ich sahna" : "O'yin shu sahnadan boshlansin");
        if (!m_SceneManager)
        {
            ImGui::SameLine();
            bool additive = IsPlayAdditive(item->Relative);
            if (ImGui::Checkbox("Play bilan", &additive))
                SetPlayAdditive(item->Relative, additive);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Play bosilganda ochiq sahna yonida additiv yuklash (shu sessiya uchun)");
        }
        if (m_SceneManager)
        {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_LAYER_GROUP "  Additiv"))
                m_SceneManager->LoadSceneAsync(item->Relative, LoadSceneMode::Additive);
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'yinga qo'shimcha sahna sifatida yuklash (LoadSceneMode::Additive)");
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_RIGHT_TO_BRACKET "  Almashtirish"))
                m_SceneManager->LoadScene(fs::path(item->Relative), LoadSceneMode::Single, SceneTransition::Fade(0.25f));
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'yinni shu sahnaga o'tkazish (Single, so'nish bilan)");
        }
        ImGui::Separator();
    }

    void ScenesPanel::DrawItemTooltip(const Item& item)
    {
        const SceneFileInfo& info = item.Info;
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(info.Name.c_str());
        ImGui::TextDisabled("%s", item.Relative.c_str());
        if (!info.Error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", info.Error.c_str());
        if (!info.Metadata.Description.empty())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted(info.Metadata.Description.c_str());
            ImGui::PopTextWrapPos();
        }
        if (!info.Metadata.Author.empty())
            ImGui::Text("Muallif: %s", info.Metadata.Author.c_str());
        if (!info.Metadata.Tags.empty())
        {
            std::string tags;
            for (const std::string& tag : info.Metadata.Tags)
                tags += (tags.empty() ? "#" : "  #") + tag;
            ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", tags.c_str());
        }
        ImGui::Text("%zu ta obyekt (%zu ildiz)", info.EntityCount, info.RootCount);
        if (info.Version < SceneMigration::CurrentVersion)
            ImGui::TextColored(s_OldColor, "Format v%d: yuklashda v%d ga yangilanadi", info.Version, SceneMigration::CurrentVersion);
        const int build = Project::GetBuildIndex(item.Relative);
        if (build >= 0)
            ImGui::Text("Build indeksi: %d", build);
        else if (FindBuildEntry(item.Relative) >= 0)
            ImGui::TextDisabled("Build ro'yxatida o'chirilgan");
        if (IsStartScene(item.Relative))
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(s_StarColor), ICON_FA_HOUSE "  Boshlang'ich sahna");
        ImGui::TextDisabled("Ikki marta bosing: ochish  |  o'ng tugma: menyu");
        ImGui::EndTooltip();
    }

    void ScenesPanel::DrawItemMenu(const Item& item)
    {
        if (!ImGui::BeginPopupContextItem("##sceneMenu"))
            return;
        m_Selected = item.Relative;
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Ochish") && OnOpenScene)
            OnOpenScene(item.Path);
        const int entry = FindBuildEntry(item.Relative);
        if (entry < 0)
        {
            if (ImGui::MenuItem(ICON_FA_PLUS "  Build ro'yxatiga qo'shish"))
                AddToBuild(item.Relative);
        }
        else
        {
            const bool enabled = Project::GetActive()->GetConfig().BuildScenes[(size_t)entry].Enabled;
            if (ImGui::MenuItem(enabled ? ICON_FA_TOGGLE_OFF "  Build'da o'chirish" : ICON_FA_TOGGLE_ON "  Build'da yoqish"))
                SetBuildEnabled(item.Relative, !enabled);
            if (ImGui::MenuItem(ICON_FA_MINUS "  Build ro'yxatidan olib tashlash"))
                RemoveFromBuild(item.Relative);
        }
        if (ImGui::MenuItem(ICON_FA_HOUSE "  Boshlang'ich sahna qilish", nullptr, IsStartScene(item.Relative), !IsStartScene(item.Relative)))
            SetStartScene(item.Relative);
        if (!m_SceneManager)
        {
            bool additive = IsPlayAdditive(item.Relative);
            if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Play bilan qo'shimcha yuklash", nullptr, &additive))
                SetPlayAdditive(item.Relative, additive);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Play bosilganda bu sahna ochiq sahna yonida additiv yuklanadi (masalan, umumiy UI yoki menejerlar sahnasi)");
        }
        if (m_SceneManager)
        {
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  O'yinga additiv yuklash"))
                m_SceneManager->LoadSceneAsync(item.Relative, LoadSceneMode::Additive);
            if (ImGui::MenuItem(ICON_FA_RIGHT_TO_BRACKET "  O'yinni shu sahnaga o'tkazish"))
                m_SceneManager->LoadScene(fs::path(item.Relative), LoadSceneMode::Single, SceneTransition::Fade(0.25f));
        }
        if (ImGui::MenuItem(ICON_FA_CAMERA "  Rasmini chizish") && OnRenderThumbnails)
            OnRenderThumbnails({ item.Path });
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_LOCATION_CROSSHAIRS "  Kontent brauzerda ko'rsatish") && OnRevealScene)
            OnRevealScene(item.Path);
        if (ImGui::MenuItem(ICON_FA_COPY "  Yo'lni nusxalash"))
            ImGui::SetClipboardText(item.Relative.c_str());
        ImGui::EndPopup();
    }

    void ScenesPanel::DrawCards(const std::vector<const Item*>& items)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float cardW = m_CardWidth;
        const float thumbH = std::floor(cardW * 9.0f / 16.0f);
        const float lineH = ImGui::GetTextLineHeight();
        const float cardH = thumbH + lineH * 2.0f + style.FramePadding.y * 3.0f;
        const float spacing = style.ItemSpacing.x;
        const int columns = std::max(1, (int)((ImGui::GetContentRegionAvail().x + spacing) / (cardW + spacing)));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < items.size(); i++)
        {
            const Item& item = *items[i];
            ImGui::PushID(item.Relative.c_str());
            if (i % (size_t)columns != 0)
                ImGui::SameLine(0.0f, spacing);
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + cardW, min.y + cardH);
            ImGui::InvisibleButton("##card", ImVec2(cardW, cardH));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                m_Selected = item.Relative;
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && OnOpenScene)
                OnOpenScene(item.Path);
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", item.Relative.c_str(), item.Relative.size() + 1);
                ImGui::Text(ICON_FA_EARTH_ASIA "  %s", item.Info.Name.c_str());
                ImGui::EndDragDropSource();
            }
            DrawItemMenu(item);
            if (hovered && !ImGui::IsPopupOpen("##sceneMenu"))
                DrawItemTooltip(item);

            std::error_code ec;
            const bool isOpen = !m_OpenScenePath.empty() && fs::equivalent(item.Path, m_OpenScenePath, ec);
            const bool selected = m_Selected == item.Relative;
            const Scene* loaded = m_SceneManager ? m_SceneManager->GetSceneByPath(item.Relative).get() : nullptr;
            const bool active = loaded && m_SceneManager->GetActiveScene().get() == loaded;
            // The card.
            draw->AddRectFilled(min, max, selected ? IM_COL32(52, 74, 104, 255) : hovered ? IM_COL32(48, 52, 60, 255) : IM_COL32(36, 38, 44, 255), 5.0f);
            if (isOpen)
                draw->AddRect(min, max, IM_COL32(90, 170, 255, 255), 5.0f, 0, 2.0f);
            else if (loaded)
                draw->AddRect(min, max, IM_COL32(110, 200, 120, 255), 5.0f, 0, 2.0f);
            const ImVec2 tmin(min.x + 4.0f, min.y + 4.0f), tmax(max.x - 4.0f, min.y + 4.0f + thumbH - 8.0f);
            Ref<Texture2D> thumbnail = m_Thumbnails ? m_Thumbnails->Get(item.Path) : nullptr;
            if (thumbnail)
                draw->AddImageRounded((ImTextureID)(uintptr_t)thumbnail->GetRendererID(), tmin, tmax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
            else
            {
                draw->AddRectFilled(tmin, tmax, IM_COL32(26, 28, 33, 255), 4.0f);
                const char* icon = ICON_FA_EARTH_ASIA;
                const ImVec2 size = ImGui::CalcTextSize(icon);
                draw->AddText(ImVec2((tmin.x + tmax.x - size.x) * 0.5f, (tmin.y + tmax.y - size.y) * 0.5f - lineH * 0.4f), IM_COL32(110, 115, 125, 255), icon);
                const char* note = "rasm yo'q";
                const ImVec2 noteSize = ImGui::CalcTextSize(note);
                draw->AddText(ImVec2((tmin.x + tmax.x - noteSize.x) * 0.5f, (tmin.y + tmax.y) * 0.5f + lineH * 0.3f), IM_COL32(95, 100, 110, 255), note);
            }
            // Badges over the picture: the build index, the start scene, the active / loaded scene, an old format.
            float bx = tmin.x + 4.0f;
            auto badge = [&](const std::string& text, ImU32 bg, ImU32 fg, bool right = false) {
                const ImVec2 size = ImGui::CalcTextSize(text.c_str());
                const ImVec2 at = right ? ImVec2(tmax.x - size.x - 10.0f, tmin.y + 4.0f) : ImVec2(bx, tmin.y + 4.0f);
                draw->AddRectFilled(at, ImVec2(at.x + size.x + 6.0f, at.y + size.y + 2.0f), bg, 3.0f);
                draw->AddText(ImVec2(at.x + 3.0f, at.y + 1.0f), fg, text.c_str());
                if (!right)
                    bx += size.x + 10.0f;
            };
            const int build = Project::GetBuildIndex(item.Relative);
            const int entry = FindBuildEntry(item.Relative);
            if (build >= 0)
                badge(std::format("#{}", build), IM_COL32(40, 90, 160, 235), IM_COL32_WHITE);
            else if (entry >= 0)
                badge("#-", IM_COL32(70, 70, 76, 235), IM_COL32(190, 190, 190, 255));
            if (IsStartScene(item.Relative))
                badge(ICON_FA_HOUSE, IM_COL32(150, 110, 20, 235), IM_COL32_WHITE);
            if (active)
                badge(ICON_FA_STAR, IM_COL32(30, 30, 30, 220), s_StarColor);
            else if (loaded)
                badge(ICON_FA_PLAY, IM_COL32(40, 110, 60, 235), IM_COL32_WHITE);
            if (IsPlayAdditive(item.Relative))
                badge(ICON_FA_LAYER_GROUP, IM_COL32(90, 60, 140, 235), IM_COL32_WHITE);
            if (item.Info.Version < SceneMigration::CurrentVersion)
                badge(std::format("v{}", item.Info.Version), IM_COL32(120, 85, 20, 235), IM_COL32(255, 225, 160, 255), true);
            // Name and the second line (folder / tags).
            const float textX = min.x + 6.0f;
            float textY = tmax.y + style.FramePadding.y;
            const float clipX = max.x - 6.0f;
            draw->PushClipRect(ImVec2(textX, textY), ImVec2(clipX, max.y), true);
            draw->AddText(ImVec2(textX, textY), isOpen ? IM_COL32(140, 200, 255, 255) : IM_COL32(230, 230, 230, 255), item.Info.Name.c_str());
            textY += lineH + 1.0f;
            std::string second;
            if (!item.Info.Metadata.Tags.empty())
                for (const std::string& tag : item.Info.Metadata.Tags)
                    second += (second.empty() ? "#" : " #") + tag;
            else
            {
                const fs::path folder = fs::path(item.Relative).parent_path();
                second = folder.empty() ? std::string("Assets") : folder.generic_string();
            }
            draw->AddText(ImVec2(textX, textY), item.Info.Metadata.Tags.empty() ? IM_COL32(130, 135, 145, 255) : IM_COL32(120, 170, 230, 255), second.c_str());
            draw->PopClipRect();
            ImGui::PopID();
        }
    }

    void ScenesPanel::DrawList(const std::vector<const Item*>& items)
    {
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
        if (!ImGui::BeginTable("##sceneList", 6, flags))
            return;
        const float thumbW = 64.0f, thumbH = 36.0f;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##thumb", ImGuiTableColumnFlags_WidthFixed, thumbW + 4.0f);
        ImGui::TableSetupColumn("Sahna", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Build", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Obyekt", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("Teglar", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();
        for (const Item* itemPtr : items)
        {
            const Item& item = *itemPtr;
            ImGui::PushID(item.Relative.c_str());
            ImGui::TableNextRow(ImGuiTableRowFlags_None, thumbH + 4.0f);
            ImGui::TableNextColumn();
            if (Ref<Texture2D> thumbnail = m_Thumbnails ? m_Thumbnails->Get(item.Path) : nullptr)
                ImGui::Image((ImTextureID)(uintptr_t)thumbnail->GetRendererID(), ImVec2(thumbW, thumbH));
            else
                ImGui::Dummy(ImVec2(thumbW, thumbH));
            ImGui::TableNextColumn();
            const bool start = IsStartScene(item.Relative);
            if (ImGui::Selectable(std::format("{}{}##row", start ? ICON_FA_HOUSE "  " : "", item.Info.Name).c_str(), m_Selected == item.Relative,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, thumbH)))
            {
                m_Selected = item.Relative;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && OnOpenScene)
                    OnOpenScene(item.Path);
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", item.Relative.c_str(), item.Relative.size() + 1);
                ImGui::Text(ICON_FA_EARTH_ASIA "  %s", item.Info.Name.c_str());
                ImGui::EndDragDropSource();
            }
            DrawItemMenu(item);
            if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##sceneMenu"))
                DrawItemTooltip(item);
            ImGui::TableNextColumn();
            const int build = Project::GetBuildIndex(item.Relative);
            if (build >= 0)
                ImGui::Text("#%d", build);
            else
                ImGui::TextDisabled(FindBuildEntry(item.Relative) >= 0 ? "o'chiq" : "-");
            ImGui::TableNextColumn();
            ImGui::Text("%zu", item.Info.EntityCount);
            ImGui::TableNextColumn();
            std::string tags;
            for (const std::string& tag : item.Info.Metadata.Tags)
                tags += (tags.empty() ? "#" : " #") + tag;
            ImGui::TextDisabled("%s", tags.c_str());
            ImGui::TableNextColumn();
            if (item.Info.Version < SceneMigration::CurrentVersion)
                ImGui::TextColored(s_OldColor, "v%d", item.Info.Version);
            else
                ImGui::Text("v%d", item.Info.Version);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

}
