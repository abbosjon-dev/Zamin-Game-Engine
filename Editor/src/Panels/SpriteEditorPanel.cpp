#include "SpriteEditorPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    namespace {
        const ImVec4 s_WarnColor(0.95f, 0.75f, 0.3f, 1.0f);
        const ImVec4 s_ErrorColor(0.95f, 0.42f, 0.38f, 1.0f);

        struct PivotPreset
        {
            const char* Name;
            glm::vec2 Pivot;
        };
        const PivotPreset s_PivotPresets[] = {
            { "Markaz", { 0.5f, 0.5f } }, { "Past (oyoq)", { 0.5f, 0.0f } }, { "Yuqori", { 0.5f, 1.0f } },
            { "Chap", { 0.0f, 0.5f } }, { "O'ng", { 1.0f, 0.5f } }, { "Chap-past", { 0.0f, 0.0f } },
            { "O'ng-past", { 1.0f, 0.0f } }, { "Chap-yuqori", { 0.0f, 1.0f } }, { "O'ng-yuqori", { 1.0f, 1.0f } },
        };

        AssetRegistry* GetRegistry() { return Project::GetActive() ? Project::GetAssetRegistry() : nullptr; }

        // A pivot as a preset combo plus the exact values; true when it changed.
        bool PivotField(const char* label, glm::vec2& pivot)
        {
            bool changed = false;
            UI::PropertyLabel(label, "Kadr ichidagi nuqta: obyektning pozitsiyasi shu yerda bo'ladi (0,0 - chap-past, 1,1 - o'ng-yuqori).");
            ImGui::PushID(label);
            const char* current = "Boshqa";
            for (const PivotPreset& p : s_PivotPresets)
                if (glm::all(glm::lessThan(glm::abs(p.Pivot - pivot), glm::vec2(1e-4f))))
                    current = p.Name;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            if (ImGui::BeginCombo("##preset", current))
            {
                for (const PivotPreset& p : s_PivotPresets)
                    if (ImGui::Selectable(p.Name, p.Name == current))
                    {
                        pivot = p.Pivot;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat2("##value", &pivot.x, 0.01f, 0.0f, 1.0f, "%.2f"))
            {
                pivot = glm::clamp(pivot, glm::vec2(0.0f), glm::vec2(1.0f));
                changed = true;
            }
            ImGui::PopID();
            return changed;
        }

        bool IntPair(const char* label, glm::ivec2& v, int min, int max, const char* tooltip = nullptr)
        {
            UI::PropertyLabel(label, tooltip);
            ImGui::PushID(label);
            bool changed = ImGui::DragInt2("##v", &v.x, 0.25f, min, max);
            if (changed)
                v = glm::clamp(v, glm::ivec2(min), glm::ivec2(max));
            ImGui::PopID();
            return changed;
        }
    }

    // ------------------------------------------------------------------ state
    void SpriteEditorPanel::Open(const std::string& texturePath)
    {
        m_FocusRequested = true;
        if (texturePath == m_TexturePath)
            return;
        if (HasUnsavedChanges())
        {
            m_PendingOpen = texturePath;
            m_OpenUnsavedPopup = true;
            return;
        }
        Load(texturePath);
    }

    void SpriteEditorPanel::Reset()
    {
        m_PendingOpen.reset();
        m_PendingClose = false;
        Load("");
    }

    void SpriteEditorPanel::Load(const std::string& texturePath)
    {
        m_TexturePath = texturePath;
        m_Texture = nullptr;
        m_Width = m_Height = 0;
        m_Handle = {};
        m_Saved = m_Edit = SpriteSheet();
        m_Selected = -1;
        m_Zoom = 0.0f;
        m_Pan = glm::vec2(0.0f);
        m_Pixels.reset();
        m_PixelsFailed = false;
        m_Drawing = false;
        m_SeenRevision = ~0ull;
        if (texturePath.empty())
            return;
        // Nearest filtering: every texel is a crisp square however far it is zoomed in.
        m_Texture = TextureLibrary::Load(Project::GetAssetFileSystemPath(texturePath), TextureFilter::Nearest);
        if (m_Texture && m_Texture->IsLoaded())
        {
            m_Width = m_Texture->GetWidth();
            m_Height = m_Texture->GetHeight();
        }
        if (AssetRegistry* registry = GetRegistry())
        {
            if (const AssetMetadata* meta = registry->Find(std::filesystem::path(texturePath)))
            {
                m_Handle = meta->Handle;
                m_Saved = SpriteSheet::FromImportSettings(meta->Import);
            }
            m_SeenRevision = registry->GetRevision();
        }
        m_Edit = m_Saved;
        m_Selected = m_Edit.Frames.empty() ? -1 : 0;
        // The would-be grid and the slice settings only help before the texture is cut.
        m_ShowSlicePreview = m_Edit.Frames.empty();
        m_SliceSectionRequest = m_Edit.Frames.empty() ? 1 : 0;
        // Frames are named after the file by default ("player_0", "player_1", ...).
        m_Slice.NamePrefix = std::filesystem::path(texturePath).stem().string();
    }

    void SpriteEditorPanel::Sync()
    {
        AssetRegistry* registry = GetRegistry();
        if (!registry || m_TexturePath.empty() || registry->GetRevision() == m_SeenRevision)
            return;
        m_SeenRevision = registry->GetRevision();
        const AssetMetadata* meta = m_Handle ? registry->Find(m_Handle) : registry->Find(std::filesystem::path(m_TexturePath));
        if (!meta)
            return; // gone: the edits stay (Save says why it cannot write them)
        m_Handle = meta->Handle;
        if (meta->RelativePath != m_TexturePath)
            m_TexturePath = meta->RelativePath; // moved / renamed: followed by its UUID
        SpriteSheet onDisk = SpriteSheet::FromImportSettings(meta->Import);
        if (onDisk == m_Saved)
            return;
        // Changed outside (another editor, version control): taken over unless edited here.
        if (!HasUnsavedChanges())
            m_Edit = onDisk;
        m_Saved = std::move(onDisk);
        if (m_Selected >= (int)m_Edit.Frames.size())
            m_Selected = (int)m_Edit.Frames.size() - 1;
    }

    const ImageData* SpriteEditorPanel::GetPixels()
    {
        if (!m_Pixels && !m_PixelsFailed && !m_TexturePath.empty())
        {
            auto pixels = std::make_unique<ImageData>();
            std::string error;
            // Rows top first, like the frame rectangles.
            if (Image::Load(Project::GetAssetFileSystemPath(m_TexturePath).string(), *pixels, 4, false, &error) && pixels->IsValid())
                m_Pixels = std::move(pixels);
            else
            {
                m_PixelsFailed = true;
                ZM_WARN("Sprite muharriri: rasm piksellari o'qilmadi ({}): {}", m_TexturePath, error);
            }
        }
        return m_Pixels.get();
    }

    bool SpriteEditorPanel::Save()
    {
        AssetRegistry* registry = GetRegistry();
        const AssetMetadata* meta = !registry ? nullptr : m_Handle ? registry->Find(m_Handle) : registry->Find(std::filesystem::path(m_TexturePath));
        if (!meta)
        {
            SetStatus("Saqlanmadi: rasm asset registry'da yo'q", true);
            ZM_ERROR("Sprite kadrlari saqlanmadi: '{}' asset registry'da topilmadi", m_TexturePath);
            return false;
        }
        AssetImportSettings import = meta->Import; // the texture's own settings stay
        m_Edit.WriteTo(import);
        std::string path = meta->RelativePath;
        if (!registry->SetImportSettings(meta->Handle, import))
        {
            SetStatus("Saqlanmadi", true);
            return false;
        }
        m_SeenRevision = registry->GetRevision();
        m_Saved = m_Edit;
        SetStatus(std::format("Saqlandi: {} ta kadr", m_Edit.Frames.size()));
        ZM_INFO("Sprite kadrlari saqlandi: {} ({} ta)", path, m_Edit.Frames.size());
        return true;
    }

    void SpriteEditorPanel::Revert()
    {
        m_Edit = m_Saved;
        if (m_Selected >= (int)m_Edit.Frames.size())
            m_Selected = (int)m_Edit.Frames.size() - 1;
    }

    size_t SpriteEditorPanel::Slice(bool replace)
    {
        if (m_Width == 0 || m_Height == 0)
            return 0;
        const ImageData* pixels = m_Slice.SkipEmpty ? GetPixels() : nullptr;
        std::vector<SpriteFrame> frames = SpriteSheet::SliceGrid(m_Slice, m_Width, m_Height,
                                                                 pixels && pixels->Width == m_Width && pixels->Height == m_Height ? pixels->Pixels.data() : nullptr);
        if (replace)
            m_Edit.Frames.clear();
        const int first = (int)m_Edit.Frames.size();
        for (SpriteFrame& f : frames)
        {
            f.Name = m_Edit.MakeUniqueName(f.Name);
            m_Edit.Frames.push_back(std::move(f));
        }
        m_Selected = frames.empty() ? (m_Edit.Frames.empty() ? -1 : 0) : first;
        SetStatus(std::format("{} ta kadr kesildi", frames.size()), frames.empty());
        return frames.size();
    }

    void SpriteEditorPanel::SetStatus(std::string text, bool error)
    {
        m_Status = std::move(text);
        m_StatusError = error;
        m_StatusTime = ImGui::GetTime();
    }

    glm::vec2 SpriteEditorPanel::ScreenToPixel(const ImVec2& p) const
    {
        glm::vec2 center = { (m_CanvasMin.x + m_CanvasMax.x) * 0.5f, (m_CanvasMin.y + m_CanvasMax.y) * 0.5f };
        float zoom = std::max(m_Zoom, 1e-4f);
        return (glm::vec2(p.x, p.y) - center - m_Pan) / zoom + glm::vec2((float)m_Width, (float)m_Height) * 0.5f;
    }

    ImVec2 SpriteEditorPanel::PixelToScreen(const glm::vec2& p) const
    {
        glm::vec2 center = { (m_CanvasMin.x + m_CanvasMax.x) * 0.5f, (m_CanvasMin.y + m_CanvasMax.y) * 0.5f };
        glm::vec2 s = center + m_Pan + (p - glm::vec2((float)m_Width, (float)m_Height) * 0.5f) * m_Zoom;
        return ImVec2(s.x, s.y);
    }

    // ------------------------------------------------------------------ window
    void SpriteEditorPanel::OnImGuiRender(bool* show)
    {
        if (!*show)
            return;
        if (m_FocusRequested)
        {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(1180, 720), ImGuiCond_FirstUseEver);
        bool open = true;
        std::string title = std::format(ICON_FA_SCISSORS "  {}{}###SpriteEditor", Tr("Sprite muharriri"), HasUnsavedChanges() ? " *" : "");
        if (!ImGui::Begin(title.c_str(), &open))
        {
            ImGui::End();
            if (!open)
                *show = false;
            return;
        }
        if (!open)
        {
            if (HasUnsavedChanges())
            {
                m_PendingClose = true;
                m_OpenUnsavedPopup = true;
            }
            else
                *show = false;
        }
        Sync();
        if (m_Texture && m_Texture->IsLoaded())
        {
            // Reloaded (hot reload) with another size: the frames stay in pixels.
            m_Width = m_Texture->GetWidth();
            m_Height = m_Texture->GetHeight();
        }

        DrawToolbar();
        ImGui::Separator();
        const float sidebar = std::min(340.0f, ImGui::GetContentRegionAvail().x * 0.4f);
        if (ImGui::BeginChild("##sidebar", ImVec2(sidebar, 0), ImGuiChildFlags_Borders))
            DrawSidebar();
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##canvasArea", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            DrawCanvas();
        ImGui::EndChild();

        // Keys while the window has focus (not while typing in a field).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_Selected >= 0 && m_Selected < (int)m_Edit.Frames.size())
            {
                m_Edit.Frames.erase(m_Edit.Frames.begin() + m_Selected);
                m_Selected = std::min(m_Selected, (int)m_Edit.Frames.size() - 1);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F, false))
                m_Zoom = 0.0f;
        }
        DrawUnsavedPopup(show);
        ImGui::End();
    }

    void SpriteEditorPanel::DrawUnsavedPopup(bool* show)
    {
        if (m_OpenUnsavedPopup)
        {
            ImGui::OpenPopup("###SpriteEditorUnsaved");
            m_OpenUnsavedPopup = false;
        }
        if (!ImGui::BeginPopupModal(Tr("Saqlanmagan kadrlar###SpriteEditorUnsaved"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION "  '%s' kadrlari saqlanmagan.", m_TexturePath.c_str());
        ImGui::TextDisabled("%zu ta kadr (saqlangani %zu ta)", m_Edit.Frames.size(), m_Saved.Frames.size());
        ImGui::Spacing();
        auto proceed = [&]() {
            if (m_PendingClose)
                *show = false;
            if (m_PendingOpen)
                Load(*m_PendingOpen);
            m_PendingOpen.reset();
            m_PendingClose = false;
            ImGui::CloseCurrentPopup();
        };
        if (ImGui::Button(Tr(ICON_FA_FLOPPY_DISK "  Saqlash")))
        {
            if (Save())
                proceed();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_XMARK "  Saqlamaslik")))
        {
            Revert();
            proceed();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Bekor qilish")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_PendingOpen.reset();
            m_PendingClose = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void SpriteEditorPanel::DrawToolbar()
    {
        const bool dirty = HasUnsavedChanges();
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button(Tr(ICON_FA_FLOPPY_DISK "  Saqlash")))
            Save();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Kadrlarni rasmning .zmeta fayliga yozish (Ctrl+S)");
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_ROTATE_LEFT "  Qaytarish")))
            Revert();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_Width == 0);
        if (ImGui::Button(Tr(ICON_FA_EXPAND "  Sig'dirish")))
            m_Zoom = 0.0f;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Rasmni oynaga sig'dirish (F)");
        ImGui::SameLine();
        if (ImGui::Button("1:1"))
        {
            m_Zoom = 1.0f;
            m_Pan = glm::vec2(0.0f);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox(Tr("Grid ko'rinishi"), &m_ShowSlicePreview);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Kesish sozlamalari bo'yicha kataklar rasm ustida (hali kesilmagan)");
        ImGui::SameLine();
        if (dirty)
            ImGui::TextColored(s_WarnColor, ICON_FA_CIRCLE_DOT " saqlanmagan");
        else if (!m_TexturePath.empty())
            ImGui::TextDisabled("%s", m_TexturePath.c_str());
        if (!m_Status.empty() && ImGui::GetTime() - m_StatusTime < 5.0)
        {
            ImGui::SameLine();
            ImGui::TextColored(m_StatusError ? s_ErrorColor : UI::AccentColor(), "%s", m_Status.c_str());
        }
    }

    void SpriteEditorPanel::DrawSidebar()
    {
        if (UI::BeginPropertyGrid("##spriteTexture"))
        {
            std::string path = m_TexturePath;
            if (UI::PropertyAssetPath("Rasm", path, ".png;.jpg;.jpeg;.bmp;.tga", ICON_FA_FILE_IMAGE) && path != m_TexturePath)
                Open(path);
            // Every texture of the project, for picking without the content browser.
            if (AssetRegistry* registry = GetRegistry())
            {
                UI::PropertyLabel("");
                if (ImGui::BeginCombo("##pick", ICON_FA_IMAGES "  Rasmni tanlash...", ImGuiComboFlags_HeightLarge))
                {
                    for (const AssetMetadata* meta : registry->GetAssets(AssetType::Texture))
                    {
                        size_t frames = SpriteSheet::FromImportSettings(meta->Import).Frames.size();
                        std::string label = frames ? std::format("{}  ({} kadr)", meta->RelativePath, frames) : meta->RelativePath;
                        if (ImGui::Selectable(label.c_str(), meta->RelativePath == m_TexturePath))
                            Open(meta->RelativePath);
                    }
                    ImGui::EndCombo();
                }
            }
            if (!m_TexturePath.empty())
            {
                UI::PropertyLabel("O'lcham");
                if (m_Width > 0)
                    ImGui::Text("%u x %u px", m_Width, m_Height);
                else
                    ImGui::TextColored(s_ErrorColor, "rasm o'qilmadi");
                UI::PropertyLabel("Kadrlar");
                ImGui::Text("%zu ta", m_Edit.Frames.size());
            }
            UI::EndPropertyGrid();
        }
        if (m_TexturePath.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped(ICON_FA_CIRCLE_INFO "  Rasmni kontent oynasidan shu yerga torting yoki ro'yxatdan tanlang.");
            return;
        }
        if (m_Width == 0)
            return;
        DrawSliceSettings();
        DrawFrameList();
        DrawSelectedFrame();
    }

    void SpriteEditorPanel::DrawSliceSettings()
    {
        ImGui::Spacing();
        if (m_SliceSectionRequest >= 0)
        {
            ImGui::SetNextItemOpen(m_SliceSectionRequest == 1);
            m_SliceSectionRequest = -1;
        }
        if (!ImGui::CollapsingHeader(Tr(ICON_FA_SCISSORS "  Grid bo'yicha kesish"), ImGuiTreeNodeFlags_DefaultOpen))
            return;
        if (!UI::BeginPropertyGrid("##slice"))
            return;
        const char* modes[] = { "Katak o'lchami", "Ustun / qator soni" };
        int mode = (int)m_Slice.Mode;
        UI::PropertyLabel("Usul", "Katak o'lchami: har bir kadr shuncha piksel. Ustun / qator soni: rasm teng bo'linadi.");
        if (ImGui::Combo("##mode", &mode, modes, 2))
            m_Slice.Mode = (SpriteSliceSettings::SliceMode)mode;
        if (m_Slice.Mode == SpriteSliceSettings::SliceMode::CellSize)
            IntPair("Katak (px)", m_Slice.CellSize, 1, 8192);
        else
            IntPair("Ustun, qator", m_Slice.CellCount, 1, 1024);
        IntPair("Siljish (px)", m_Slice.Offset, 0, 8192, "Chap-yuqori burchakdan birinchi katakkacha");
        IntPair("Oraliq (px)", m_Slice.Padding, 0, 8192, "Qo'shni kataklar orasidagi bo'sh joy");
        PivotField("Pivot", m_Slice.Pivot);
        UI::PropertyLabel("Nom prefiksi", "Kadrlar <prefiks>_0, <prefiks>_1, ... deb nomlanadi (Sprite Animation ketma-ketlik sifatida topadi)");
        ImGui::InputText("##prefix", &m_Slice.NamePrefix);
        UI::PropertyLabel("Bo'shlarini tashlash", "Hamma piksellari shaffof kataklar kadr bo'lmaydi");
        ImGui::Checkbox("##skip", &m_Slice.SkipEmpty);
        UI::PropertyLabel("Almashtirish", "Bor kadrlar o'chirilib, yangilari qo'yiladi (o'chirilmasa - qo'shiladi)");
        ImGui::Checkbox("##replace", &m_SliceReplace);

        glm::ivec2 cell = SpriteSheet::GetCellSize(m_Slice, m_Width, m_Height);
        SpriteSliceSettings all = m_Slice;
        all.SkipEmpty = false;
        size_t cells = SpriteSheet::SliceGrid(all, m_Width, m_Height).size();
        UI::PropertyLabel("Natija");
        ImGui::TextDisabled("%d x %d px, %zu katak", cell.x, cell.y, cells);
        UI::PropertyLabel("");
        if (ImGui::Button(Tr(ICON_FA_SCISSORS "  Kesish"), ImVec2(-FLT_MIN, 0)))
            Slice(m_SliceReplace);
        UI::EndPropertyGrid();
    }

    void SpriteEditorPanel::DrawFrameList()
    {
        ImGui::Spacing();
        if (!ImGui::CollapsingHeader(std::format(ICON_FA_LIST "  Kadrlar ({})###frames", m_Edit.Frames.size()).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            return;
        if (ImGui::Button(Tr(ICON_FA_PLUS "  Yangi")))
        {
            SpriteFrame f;
            f.Name = m_Edit.MakeUniqueName(m_Slice.NamePrefix);
            f.Size = glm::min(glm::ivec2(SpriteSheet::GetCellSize(m_Slice, m_Width, m_Height)), glm::ivec2((int)m_Width, (int)m_Height));
            f.Pivot = m_Slice.Pivot;
            m_Edit.Frames.push_back(f);
            m_Selected = (int)m_Edit.Frames.size() - 1;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Yangi kadr (yoki rasm ustida sichqoncha bilan to'rtburchak torting)");
        ImGui::SameLine();
        ImGui::BeginDisabled(m_Selected < 0);
        if (ImGui::Button(Tr(ICON_FA_TRASH "  O'chirish")) && m_Selected >= 0 && m_Selected < (int)m_Edit.Frames.size())
        {
            m_Edit.Frames.erase(m_Edit.Frames.begin() + m_Selected);
            m_Selected = std::min(m_Selected, (int)m_Edit.Frames.size() - 1);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_Edit.Frames.empty());
        if (ImGui::Button(Tr(ICON_FA_BROOM "  Hammasi")))
        {
            m_Edit.Frames.clear();
            m_Selected = -1;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Hamma kadrlarni o'chirish");

        const float rows = std::clamp((float)m_Edit.Frames.size(), 3.0f, 9.0f);
        if (ImGui::BeginChild("##frameList", ImVec2(0, rows * ImGui::GetTextLineHeightWithSpacing() + 8.0f), ImGuiChildFlags_Borders))
        {
            ImGuiListClipper clipper;
            clipper.Begin((int)m_Edit.Frames.size());
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                {
                    const SpriteFrame& f = m_Edit.Frames[(size_t)i];
                    ImGui::PushID(i);
                    std::string label = std::format("{}##row", f.Name);
                    if (ImGui::Selectable(label.c_str(), i == m_Selected))
                        m_Selected = i;
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
                    ImGui::TextDisabled("%d,%d  %dx%d", f.Position.x, f.Position.y, f.Size.x, f.Size.y);
                    ImGui::PopID();
                }
        }
        ImGui::EndChild();
    }

    void SpriteEditorPanel::DrawSelectedFrame()
    {
        if (m_Selected < 0 || m_Selected >= (int)m_Edit.Frames.size())
            return;
        ImGui::Spacing();
        if (!ImGui::CollapsingHeader(Tr(ICON_FA_CROP "  Tanlangan kadr"), ImGuiTreeNodeFlags_DefaultOpen))
            return;
        SpriteFrame& f = m_Edit.Frames[(size_t)m_Selected];
        if (!UI::BeginPropertyGrid("##frame"))
            return;
        UI::PropertyLabel("Nomi");
        std::string name = f.Name;
        if (ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
        {
            if (!name.empty() && name != f.Name)
            {
                int other = m_Edit.FindIndex(name);
                if (other >= 0 && other != m_Selected)
                    SetStatus(std::format("'{}' nomli kadr bor", name), true);
                else
                    f.Name = name;
            }
        }
        IntPair("Pozitsiya (px)", f.Position, 0, (int)std::max(m_Width, m_Height));
        IntPair("O'lcham (px)", f.Size, 1, (int)std::max(m_Width, m_Height));
        PivotField("Pivot", f.Pivot);
        UI::PropertyLabel("9-slice (px)", "Chap, o'ng, yuqori, past chegaralar: Sliced / Tiled rejimdagi Sprite Renderer'da burchaklar "
                                          "cho'zilmaydi. 0 - chegara yo'q.");
        if (ImGui::DragFloat4("##border", &f.Border.x, 0.25f, 0.0f, (float)std::max(f.Size.x, f.Size.y), "%.0f"))
            f.Border = glm::max(f.Border, glm::vec4(0.0f));
        if (f.Position.x + f.Size.x > (int)m_Width || f.Position.y + f.Size.y > (int)m_Height)
        {
            UI::PropertyLabel("");
            ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  Rasmdan chiqib ketgan");
        }
        if (m_Texture)
        {
            UI::PropertyLabel("Ko'rinishi");
            glm::vec2 uvMin, uvMax;
            f.GetUV(m_Width, m_Height, uvMin, uvMax);
            float scale = 96.0f / std::max(1, std::max(f.Size.x, f.Size.y));
            ImGui::Image((ImTextureID)(uintptr_t)m_Texture->GetRendererID(), ImVec2(f.Size.x * scale, f.Size.y * scale),
                         ImVec2(uvMin.x, uvMax.y), ImVec2(uvMax.x, uvMin.y));
        }
        UI::EndPropertyGrid();
    }

    // ------------------------------------------------------------------ canvas
    void SpriteEditorPanel::DrawCanvas()
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.x = std::max(avail.x, 64.0f);
        avail.y = std::max(avail.y, 64.0f);
        m_CanvasMin = ImGui::GetCursorScreenPos();
        m_CanvasMax = ImVec2(m_CanvasMin.x + avail.x, m_CanvasMin.y + avail.y);
        ImGui::InvisibleButton("##canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(m_CanvasMin, m_CanvasMax, true);
        dl->AddRectFilled(m_CanvasMin, m_CanvasMax, IM_COL32(22, 24, 28, 255));

        if (!m_Texture || m_Width == 0 || m_Height == 0)
        {
            const char* text = m_TexturePath.empty() ? "Rasm tanlanmagan" : "Rasm o'qilmadi";
            ImVec2 size = ImGui::CalcTextSize(text);
            dl->AddText(ImVec2((m_CanvasMin.x + m_CanvasMax.x - size.x) * 0.5f, (m_CanvasMin.y + m_CanvasMax.y - size.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
            dl->PopClipRect();
            return;
        }

        if (m_Zoom <= 0.0f)
        {
            m_Zoom = std::max(0.05f, std::min(avail.x / m_Width, avail.y / m_Height) * 0.92f);
            m_Pan = glm::vec2(0.0f);
        }
        // Zoom around the mouse, pan with the middle / right button.
        if (hovered && io.MouseWheel != 0.0f)
        {
            glm::vec2 before = ScreenToPixel(io.MousePos);
            m_Zoom = std::clamp(m_Zoom * std::pow(1.2f, io.MouseWheel), 0.05f, 128.0f);
            ImVec2 after = PixelToScreen(before);
            m_Pan += glm::vec2(io.MousePos.x - after.x, io.MousePos.y - after.y);
        }
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)))
            m_Pan += glm::vec2(io.MouseDelta.x, io.MouseDelta.y);

        const ImVec2 p0 = PixelToScreen({ 0.0f, 0.0f });
        const ImVec2 p1 = PixelToScreen({ (float)m_Width, (float)m_Height });
        // Checkerboard under the transparent parts.
        {
            const float cell = 10.0f;
            ImVec2 a(std::max(p0.x, m_CanvasMin.x), std::max(p0.y, m_CanvasMin.y));
            ImVec2 b(std::min(p1.x, m_CanvasMax.x), std::min(p1.y, m_CanvasMax.y));
            dl->AddRectFilled(a, b, IM_COL32(58, 60, 66, 255));
            int startX = (int)std::floor((a.x - p0.x) / cell), startY = (int)std::floor((a.y - p0.y) / cell);
            for (int y = startY; p0.y + y * cell < b.y; y++)
                for (int x = startX + ((startX + y) & 1); p0.x + x * cell < b.x; x += 2)
                {
                    ImVec2 c0(std::max(a.x, p0.x + x * cell), std::max(a.y, p0.y + y * cell));
                    ImVec2 c1(std::min(b.x, p0.x + (x + 1) * cell), std::min(b.y, p0.y + (y + 1) * cell));
                    if (c1.x > c0.x && c1.y > c0.y)
                        dl->AddRectFilled(c0, c1, IM_COL32(82, 84, 92, 255));
                }
        }
        dl->AddImage((ImTextureID)(uintptr_t)m_Texture->GetRendererID(), p0, p1, ImVec2(0, 1), ImVec2(1, 0));
        dl->AddRect(p0, p1, IM_COL32(120, 125, 135, 200));

        auto rectOf = [&](glm::ivec2 pos, glm::ivec2 size, ImVec2& a, ImVec2& b) {
            a = PixelToScreen(glm::vec2(pos));
            b = PixelToScreen(glm::vec2(pos + size));
        };
        // The grid the slice settings would make (not cut yet).
        if (m_ShowSlicePreview)
        {
            SpriteSliceSettings preview = m_Slice;
            preview.SkipEmpty = false;
            std::vector<SpriteFrame> cells = SpriteSheet::SliceGrid(preview, m_Width, m_Height);
            if (cells.size() <= 4096)
                for (const SpriteFrame& c : cells)
                {
                    ImVec2 a, b;
                    rectOf(c.Position, c.Size, a, b);
                    dl->AddRect(a, b, IM_COL32(90, 170, 255, 90));
                }
        }

        // Frames: the one under the mouse, the selected one with its 9-slice lines and pivot.
        const glm::vec2 mouse = ScreenToPixel(io.MousePos);
        int hoveredFrame = -1;
        if (hovered)
            for (int i = (int)m_Edit.Frames.size() - 1; i >= 0; i--)
            {
                const SpriteFrame& f = m_Edit.Frames[(size_t)i];
                if (mouse.x >= f.Position.x && mouse.y >= f.Position.y && mouse.x < f.Position.x + f.Size.x && mouse.y < f.Position.y + f.Size.y)
                {
                    hoveredFrame = i;
                    break;
                }
            }
        for (int i = 0; i < (int)m_Edit.Frames.size(); i++)
        {
            if (i == m_Selected)
                continue;
            const SpriteFrame& f = m_Edit.Frames[(size_t)i];
            ImVec2 a, b;
            rectOf(f.Position, f.Size, a, b);
            if (b.x < m_CanvasMin.x || a.x > m_CanvasMax.x || b.y < m_CanvasMin.y || a.y > m_CanvasMax.y)
                continue;
            dl->AddRect(a, b, i == hoveredFrame ? IM_COL32(255, 255, 255, 230) : IM_COL32(255, 255, 255, 120), 0.0f, 0, i == hoveredFrame ? 2.0f : 1.0f);
            if (b.x - a.x > 34.0f && b.y - a.y > 16.0f)
            {
                ImGui::PushClipRect(a, b, true);
                ImVec2 size = ImGui::CalcTextSize(f.Name.c_str());
                dl->AddRectFilled(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(a.x + size.x + 6.0f, a.y + size.y + 3.0f), IM_COL32(16, 18, 22, 150), 2.0f);
                dl->AddText(ImVec2(a.x + 3.0f, a.y + 2.0f), IM_COL32(255, 255, 255, 200), f.Name.c_str());
                ImGui::PopClipRect();
            }
        }
        if (m_Selected >= 0 && m_Selected < (int)m_Edit.Frames.size())
        {
            const SpriteFrame& f = m_Edit.Frames[(size_t)m_Selected];
            ImVec2 a, b;
            rectOf(f.Position, f.Size, a, b);
            const ImU32 accent = UI::AccentU32();
            dl->AddRectFilled(a, b, UI::AccentU32(0.12f));
            if (f.HasBorder())
            {
                const ImU32 green = IM_COL32(120, 230, 120, 220);
                float l = a.x + f.Border.x * m_Zoom, r = b.x - f.Border.y * m_Zoom;
                float t = a.y + f.Border.z * m_Zoom, bt = b.y - f.Border.w * m_Zoom;
                if (f.Border.x > 0.0f) dl->AddLine(ImVec2(l, a.y), ImVec2(l, b.y), green);
                if (f.Border.y > 0.0f) dl->AddLine(ImVec2(r, a.y), ImVec2(r, b.y), green);
                if (f.Border.z > 0.0f) dl->AddLine(ImVec2(a.x, t), ImVec2(b.x, t), green);
                if (f.Border.w > 0.0f) dl->AddLine(ImVec2(a.x, bt), ImVec2(b.x, bt), green);
            }
            dl->AddRect(a, b, accent, 0.0f, 0, 2.5f);
            ImVec2 pivot(a.x + (b.x - a.x) * f.Pivot.x, b.y - (b.y - a.y) * f.Pivot.y);
            dl->AddCircleFilled(pivot, 4.0f, IM_COL32(255, 210, 80, 255));
            dl->AddCircle(pivot, 4.5f, IM_COL32(20, 20, 20, 255));
            ImVec2 textSize = ImGui::CalcTextSize(f.Name.c_str());
            ImVec2 label(a.x, a.y - textSize.y - 4.0f);
            dl->AddRectFilled(ImVec2(label.x - 2.0f, label.y - 1.0f), ImVec2(label.x + textSize.x + 4.0f, label.y + textSize.y + 1.0f), IM_COL32(20, 22, 26, 220), 3.0f);
            dl->AddText(ImVec2(label.x + 1.0f, label.y), accent, f.Name.c_str());
        }

        // Left button: a click picks a frame, a drag draws a new one (snapped to pixels).
        auto toCell = [&](const glm::vec2& p) {
            return glm::clamp(glm::ivec2(glm::floor(p)), glm::ivec2(0), glm::ivec2((int)m_Width - 1, (int)m_Height - 1));
        };
        if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_DrawStart = m_DrawEnd = toCell(mouse);
            m_Drawing = false;
        }
        if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f))
                m_Drawing = true;
            m_DrawEnd = toCell(mouse);
        }
        if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (m_Drawing)
            {
                glm::ivec2 a = glm::min(m_DrawStart, m_DrawEnd), b = glm::max(m_DrawStart, m_DrawEnd);
                SpriteFrame f;
                f.Name = m_Edit.MakeUniqueName(m_Slice.NamePrefix);
                f.Position = a;
                f.Size = b - a + glm::ivec2(1);
                f.Pivot = m_Slice.Pivot;
                m_Edit.Frames.push_back(f);
                m_Selected = (int)m_Edit.Frames.size() - 1;
                SetStatus(std::format("Yangi kadr: {} ({}x{})", f.Name, f.Size.x, f.Size.y));
            }
            else
                m_Selected = hoveredFrame;
            m_Drawing = false;
        }
        if (m_Drawing)
        {
            glm::ivec2 a = glm::min(m_DrawStart, m_DrawEnd), b = glm::max(m_DrawStart, m_DrawEnd) + glm::ivec2(1);
            ImVec2 s0 = PixelToScreen(glm::vec2(a)), s1 = PixelToScreen(glm::vec2(b));
            dl->AddRectFilled(s0, s1, IM_COL32(255, 210, 80, 40));
            dl->AddRect(s0, s1, IM_COL32(255, 210, 80, 255), 0.0f, 0, 1.5f);
        }

        // Pixel under the mouse and the zoom, bottom-left.
        std::string info = std::format("{:.0f}%", m_Zoom * 100.0f);
        if (hovered && mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x < m_Width && mouse.y < m_Height)
            info = std::format("x {}  y {}   |   {}", (int)mouse.x, (int)mouse.y, info);
        if (hoveredFrame >= 0)
            info += "   |   " + m_Edit.Frames[(size_t)hoveredFrame].Name;
        ImVec2 infoSize = ImGui::CalcTextSize(info.c_str());
        ImVec2 infoPos(m_CanvasMin.x + 8.0f, m_CanvasMax.y - infoSize.y - 8.0f);
        dl->AddRectFilled(ImVec2(infoPos.x - 4.0f, infoPos.y - 2.0f), ImVec2(infoPos.x + infoSize.x + 4.0f, infoPos.y + infoSize.y + 2.0f), IM_COL32(16, 18, 22, 200), 3.0f);
        dl->AddText(infoPos, IM_COL32(220, 225, 232, 255), info.c_str());
        const char* hint = "G'ildirak: zoom   |   O'rta / o'ng tugma: surish   |   Sudrash: yangi kadr   |   Del: o'chirish";
        ImVec2 hintSize = ImGui::CalcTextSize(hint);
        if (hintSize.x + infoSize.x + 40.0f < avail.x)
            dl->AddText(ImVec2(m_CanvasMax.x - hintSize.x - 8.0f, m_CanvasMax.y - hintSize.y - 8.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
        dl->PopClipRect();
    }

}
