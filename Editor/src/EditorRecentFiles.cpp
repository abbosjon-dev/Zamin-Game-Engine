// Recent projects and scenes (EditorLayer): most recently used lists kept per user in the config
// directory (recent-projects.json, recent-scenes.json - each scene with its project file), shown in
// the File menu and on the launcher.
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>

#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {
        fs::path RecentFile(const char* name)
        {
            const fs::path dir = EditorPreferences::GetConfigDirectory();
            std::error_code ec;
            fs::create_directories(dir, ec);
            return dir / name;
        }

        // The form the lists keep paths in (the same file once, however it was reached).
        std::string Canonical(const fs::path& path)
        {
            std::error_code ec;
            fs::path out = fs::weakly_canonical(fs::absolute(path, ec), ec);
            return (ec ? fs::absolute(path) : out).string();
        }

        bool FileExists(const std::string& path)
        {
            std::error_code ec;
            return fs::exists(path, ec);
        }
    }

    // ------------------------------------------------------------------ projects
    void EditorLayer::LoadRecentProjects()
    {
        m_RecentProjects.Clear();
        m_RecentProjects.Load(RecentFile("recent-projects.json"));
        m_RecentProjects.Prune(FileExists);
    }

    void EditorLayer::AddRecentProject(const fs::path& path)
    {
        m_RecentProjects.Add(Canonical(path));
        m_RecentProjects.Save(RecentFile("recent-projects.json"));
    }

    // ------------------------------------------------------------------ scenes
    void EditorLayer::LoadRecentScenes()
    {
        m_RecentScenes.Clear();
        m_RecentScenes.Load(RecentFile("recent-scenes.json"));
        if (m_RecentScenes.Prune(FileExists) > 0)
            SaveRecentScenes();
    }

    void EditorLayer::SaveRecentScenes()
    {
        m_RecentScenes.Save(RecentFile("recent-scenes.json"));
    }

    void EditorLayer::AddRecentScene(const fs::path& scene)
    {
        if (!Project::GetActive() || scene.empty())
            return;
        m_RecentScenes.Add(Canonical(scene), Canonical(Project::GetProjectFilePath()));
        SaveRecentScenes();
    }

    bool EditorLayer::OpenRecentScene(const RecentFileList::Entry& entry)
    {
        const RecentFileList::Entry recent = entry; // the list changes while the scene opens
        if (!FileExists(recent.Path))
        {
            ZM_WARN("Sahna topilmadi (ro'yxatdan olib tashlandi): {}", recent.Path);
            m_RecentScenes.Remove(recent.Path);
            SaveRecentScenes();
            return false;
        }
        std::error_code ec;
        const bool projectOpen = Project::GetActive() && !recent.Owner.empty() && fs::equivalent(Project::GetProjectFilePath(), recent.Owner, ec);
        if (!projectOpen)
        {
            if (recent.Owner.empty() || !FileExists(recent.Owner) || !OpenProject(recent.Owner))
            {
                ZM_WARN("Sahnaning loyihasi ochilmadi: {}", recent.Owner);
                return false;
            }
            // The project opened its start scene: nothing more when that is the one.
            if (!m_EditorScenePath.empty() && fs::equivalent(m_EditorScenePath, recent.Path, ec))
                return true;
        }
        return OpenScene(recent.Path);
    }

    void EditorLayer::UI_RecentScenesMenu()
    {
        std::vector<RecentFileList::Entry> scenes;
        if (Project::GetActive())
            scenes = m_RecentScenes.GetForOwner(Canonical(Project::GetProjectFilePath()), 12);
        if (m_OpenMenu.ends_with("/recent") && !scenes.empty())
            ImGui::OpenPopup(ICON_FA_CLOCK_ROTATE_LEFT "  So'nggi sahnalar");
        if (!ImGui::BeginMenu(Tr(ICON_FA_CLOCK_ROTATE_LEFT "  So'nggi sahnalar"), !scenes.empty()))
            return;
        std::optional<RecentFileList::Entry> picked;
        const int64_t now = GetUnixTime();
        std::error_code ec;
        for (size_t i = 0; i < scenes.size(); i++)
        {
            const RecentFileList::Entry& e = scenes[i];
            const fs::path path(e.Path);
            // The folder under Assets as the item's right column ("Scenes/").
            std::string folder = fs::path(Project::GetAssetRelativePath(path)).parent_path().generic_string();
            if (!folder.empty())
                folder += "/";
            const bool isOpen = !m_EditorScenePath.empty() && fs::equivalent(path, m_EditorScenePath, ec);
            const std::string label = std::format(ICON_FA_FILE "  {}##recent{}", path.stem().string(), i);
            if (ImGui::MenuItem(label.c_str(), folder.c_str(), isOpen))
                picked = e;
            if (ImGui::IsItemHovered())
                UI::Tooltip(std::format("{}\nOchilgan: {}", Project::GetAssetRelativePath(path), FormatTimeAgo(e.Time, now)).c_str());
        }
        ImGui::Separator();
        if (ImGui::MenuItem(Tr(ICON_FA_BROOM "  Ro'yxatni tozalash")))
        {
            m_RecentScenes.ClearOwner(Canonical(Project::GetProjectFilePath()));
            SaveRecentScenes();
        }
        ImGui::EndMenu();
        if (picked)
            OpenRecentScene(*picked);
    }

    bool EditorLayer::UI_LauncherRecentScenes()
    {
        std::vector<RecentFileList::Entry> scenes;
        for (const RecentFileList::Entry& e : m_RecentScenes.GetEntries())
            if (scenes.size() < 5 && FileExists(e.Path))
                scenes.push_back(e);
        if (scenes.empty())
            return false;
        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        ImGui::TextUnformatted(ICON_FA_CLOCK_ROTATE_LEFT "  So'nggi sahnalar");
        ImGui::PopFont();
        ImGui::Spacing();
        const int64_t now = GetUnixTime();
        std::optional<RecentFileList::Entry> picked;
        for (const RecentFileList::Entry& e : scenes)
        {
            const fs::path scene(e.Path), project(e.Owner);
            ImGui::PushID(e.Path.c_str());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const float h = ImGui::GetFrameHeight() * 2.0f;
            if (ImGui::InvisibleButton("##scene", ImVec2(w, h)))
                picked = e;
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hovered ? UI::AccentU32(0.157f) : IM_COL32(255, 255, 255, 8), 6.0f);
            d->AddText(ImGuiLayer::GetBoldFont(), ImGui::GetFontSize(), ImVec2(p.x + 14, p.y + 6), ImGui::GetColorU32(ImGuiCol_Text),
                       (std::string(ICON_FA_FILE "   ") + scene.stem().string()).c_str());
            // Which project, where in it, how long ago.
            std::string where = scene.filename().string();
            std::error_code ec;
            if (!project.empty())
            {
                const fs::path relative = scene.lexically_relative(project.parent_path() / "Assets");
                if (!relative.empty() && !relative.generic_string().starts_with(".."))
                    where = relative.generic_string();
            }
            const std::string detail = std::format("{}   \xc2\xb7   {}   \xc2\xb7   {}", project.empty() ? std::string("?") : project.stem().string(), where,
                                                   FormatTimeAgo(e.Time, now));
            d->AddText(ImVec2(p.x + 14 + ImGui::GetFontSize() * 1.6f, p.y + h * 0.5f + 1), ImGui::GetColorU32(ImGuiCol_TextDisabled), detail.c_str());
            if (hovered)
                UI::Tooltip(e.Path.c_str());
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 2));
        }
        ImGui::Dummy(ImVec2(0, 10));
        return picked && OpenRecentScene(*picked);
    }

}
