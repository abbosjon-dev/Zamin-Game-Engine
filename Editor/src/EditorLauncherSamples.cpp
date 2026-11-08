#include "EditorLayer.h"
#include "UI.h"

#include "Zamin/ImGui/ImGuiLayer.h"
#include "Zamin/ImGui/Icons.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace Zamin {

    namespace fs = std::filesystem;

    // Launcher: namuna o'yinlar kartalar ko'rinishida - har birida loyihaning rasmi (.zproject "Thumbnail", o'yinning
    // haqiqiy skrinshotidan olingan kichik JPG / PNG), nomi (Player > mahsulot nomi) va tavsifi ("Description").
    // Kartani bosish loyihani ochadi. Ro'yxat bir necha soniyada bir yangilanadi, rasmlar bir marta yuklanadi.

    const std::vector<ProjectSummary>& EditorLayer::GetLauncherSamples()
    {
        const double now = ImGui::GetTime();
        if (m_LauncherSamplesTime >= 0.0 && now - m_LauncherSamplesTime < 3.0)
            return m_LauncherSamples;
        m_LauncherSamplesTime = now;
        m_LauncherSamples.clear();
        const fs::path exe = FileSystem::GetExecutableDirectory();
        for (const fs::path& base : { exe / ".." / ".." / "Projects", exe / ".." / "Projects", exe / "Projects" })
        {
            std::error_code ec;
            if (!fs::is_directory(base, ec))
                continue;
            for (const auto& dir : fs::directory_iterator(base, ec))
            {
                if (!dir.is_directory(ec))
                    continue;
                for (const auto& f : fs::directory_iterator(dir.path(), ec))
                    if (f.path().extension() == ".zproject")
                        if (auto summary = ProjectSerializer::ReadSummary(fs::weakly_canonical(f.path(), ec)))
                            m_LauncherSamples.push_back(std::move(*summary));
            }
            break;
        }
        // the ones with a picture first, then by name
        std::sort(m_LauncherSamples.begin(), m_LauncherSamples.end(), [](const ProjectSummary& a, const ProjectSummary& b) {
            if (a.Thumbnail.empty() != b.Thumbnail.empty())
                return !a.Thumbnail.empty();
            return a.GetDisplayName() < b.GetDisplayName();
        });
        return m_LauncherSamples;
    }

    bool EditorLayer::UI_LauncherSamples(bool oneRow)
    {
        const std::vector<ProjectSummary>& samples = GetLauncherSamples();
        if (samples.empty())
            return false;

        // as many cards to a row as fit (at least ~210 px each), pictures 16:9
        const float spacing = 12.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, (int)((avail + spacing) / (210.0f + spacing)));
        const bool foldable = oneRow || m_LauncherAllSamples;
        const size_t shown = oneRow ? std::min(samples.size(), (size_t)columns) : samples.size();

        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        ImGui::TextUnformatted(Tr(ICON_FA_WAND_MAGIC_SPARKLES "  Namuna o'yinlar"));
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("(%d)", (int)samples.size());
        // one row of cards when the launcher also lists the user's own work; the button shows the rest
        if (foldable && samples.size() > (size_t)columns)
        {
            const std::string label = oneRow ? std::format("{} ({})###samplesFold", Tr("Hammasini ko'rsatish"), samples.size())
                                             : std::format("{}###samplesFold", Tr("Kamroq ko'rsatish"));
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                                                                 ImGui::CalcTextSize(label.c_str(), nullptr, true).x -
                                                                 ImGui::GetStyle().FramePadding.x * 2.0f));
            if (ImGui::SmallButton(label.c_str()))
                m_LauncherAllSamples = oneRow;
        }
        ImGui::Spacing();

        const float cardW = std::floor((avail - spacing * (float)(columns - 1)) / (float)columns);
        const float imageH = std::floor(cardW * 9.0f / 16.0f);
        const float lineH = ImGui::GetFontSize();
        const float cardH = imageH + lineH * 3.4f + 18.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const fs::path* picked = nullptr;
        for (size_t i = 0; i < shown; i++)
        {
            const ProjectSummary& s = samples[i];
            if (i % (size_t)columns != 0)
                ImGui::SameLine(0.0f, spacing);
            ImGui::PushID(s.ProjectFile.string().c_str());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##sample", ImVec2(cardW, cardH)))
                picked = &s.ProjectFile;
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 end(p.x + cardW, p.y + cardH);
            const float rounding = 8.0f;
            dl->AddRectFilled(p, end, hovered ? UI::AccentU32(0.22f) : IM_COL32(255, 255, 255, 10), rounding);
            if (hovered)
                dl->AddRect(p, end, UI::AccentU32(0.9f), rounding, 0, 2.0f);

            // the picture (loaded once; none: a gradient with a gamepad)
            Ref<Texture2D> texture;
            if (!s.Thumbnail.empty())
            {
                const std::string key = s.Thumbnail.string();
                auto it = m_LauncherThumbnails.find(key);
                if (it == m_LauncherThumbnails.end())
                {
                    Ref<Texture2D> loaded = Texture2D::Create(key);
                    it = m_LauncherThumbnails.emplace(key, loaded && loaded->IsLoaded() ? loaded : nullptr).first;
                }
                texture = it->second;
            }
            const ImVec2 imageMin(p.x + 4.0f, p.y + 4.0f), imageMax(end.x - 4.0f, p.y + 4.0f + imageH - 8.0f);
            if (texture)
            {
                // fill the 16:9 box, cropping the picture's longer side; file textures are bottom row first
                const float aspect = (float)texture->GetWidth() / (float)std::max(1u, texture->GetHeight());
                const float boxAspect = (imageMax.x - imageMin.x) / std::max(1.0f, imageMax.y - imageMin.y);
                ImVec2 uv0(0.0f, 1.0f), uv1(1.0f, 0.0f);
                if (aspect > boxAspect)
                {
                    const float cut = (1.0f - boxAspect / aspect) * 0.5f;
                    uv0.x = cut;
                    uv1.x = 1.0f - cut;
                }
                else if (aspect < boxAspect)
                {
                    const float cut = (1.0f - aspect / boxAspect) * 0.5f;
                    uv0.y = 1.0f - cut;
                    uv1.y = cut;
                }
                dl->AddImageRounded((ImTextureID)(uintptr_t)texture->GetRendererID(), imageMin, imageMax, uv0, uv1,
                                    hovered ? IM_COL32_WHITE : IM_COL32(225, 225, 225, 255), rounding - 3.0f, ImDrawFlags_RoundCornersTop);
            }
            else
            {
                dl->AddRectFilledMultiColor(imageMin, imageMax, IM_COL32(30, 70, 70, 255), IM_COL32(20, 40, 60, 255), IM_COL32(12, 18, 26, 255),
                                            IM_COL32(18, 50, 50, 255));
                ImGui::PushFont(ImGuiLayer::GetLargeIconFont());
                const ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_GAMEPAD);
                dl->AddText(ImVec2((imageMin.x + imageMax.x - iconSize.x) * 0.5f, (imageMin.y + imageMax.y - iconSize.y) * 0.5f),
                            IM_COL32(150, 220, 200, 200), ICON_FA_GAMEPAD);
                ImGui::PopFont();
            }

            // the name and two lines of the description (cut at the card's edge)
            const float textX = p.x + 10.0f;
            float y = imageMax.y + 6.0f;
            dl->AddText(ImGuiLayer::GetBoldFont(), lineH, ImVec2(textX, y), ImGui::GetColorU32(ImGuiCol_Text), s.GetDisplayName().c_str());
            y += lineH + 3.0f;
            if (!s.Description.empty())
            {
                const ImVec4 clip(p.x + 6.0f, y, end.x - 8.0f, end.y - 6.0f);
                dl->AddText(ImGui::GetFont(), lineH * 0.9f, ImVec2(textX, y), ImGui::GetColorU32(ImGuiCol_TextDisabled), s.Description.c_str(),
                            nullptr, cardW - 20.0f, &clip);
            }
            if (hovered)
            {
                ImGui::BeginTooltip();
                ImGui::PushFont(ImGuiLayer::GetBoldFont());
                ImGui::TextUnformatted(s.GetDisplayName().c_str());
                ImGui::PopFont();
                if (!s.Version.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", s.Version.c_str());
                }
                if (!s.Description.empty())
                {
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
                    ImGui::TextUnformatted(s.Description.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::TextDisabled("%s", s.ProjectFile.string().c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(0, 10));
        return picked && OpenProject(*picked);
    }

}
