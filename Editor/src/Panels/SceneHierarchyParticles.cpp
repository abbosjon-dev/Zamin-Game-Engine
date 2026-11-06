// The Particle System section of the inspector.
#include "SceneHierarchyPanel.h"
#include "../UI.h"

#include <Zamin/Renderer/SpriteSheet.h>

#include <imgui_stdlib.h>

#include <algorithm>
#include <format>

namespace Zamin {

    using PS = ParticleSystemComponent;

    // A module: a full-width foldout row with its on / off box (the property grid pauses around
    // it). Its rows follow while it is open; they are greyed out while it is off.
    static bool ModuleHeader(const char* id, const char* icon, const char* title, bool& enabled, bool& changed, const char* tooltip,
                             bool& wasOn)
    {
        UI::EndPropertyGrid();
        ImGui::PushID(id);
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                                         ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding;
        ImGui::PushStyleColor(ImGuiCol_Header, enabled ? ImVec4(0.13f, 0.20f, 0.19f, 1.0f) : ImVec4(0.12f, 0.13f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.17f, 0.25f, 0.24f, 1.0f));
        const ImVec2 start = ImGui::GetCursorPos();
        ImGui::SetNextItemOpen(enabled, ImGuiCond_Once);
        // Room for the box after the arrow: the title starts behind it.
        const bool open = ImGui::TreeNodeEx("##module", flags, "        %s  %s", icon, title);
        ImGui::PopStyleColor(2);
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip(tooltip);
        const ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(start.x + ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f, start.y));
        wasOn = enabled;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Checkbox("##on", &enabled))
            changed = true;
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip(enabled ? "Modul yoqilgan (bosing - o'chirish)" : "Modul o'chiq (bosing - yoqish)");
        ImGui::SetCursorPos(after);
        ImGui::PopID();
        UI::BeginPropertyGrid(id); // never fails here: the window is drawing (its section's grid did not)
        return open;
    }

    static void Note(const char* text, bool warning = false)
    {
        UI::PropertyLabel("");
        ImGui::PushTextWrapPos(0.0f);
        if (warning)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", text);
        else
            ImGui::TextDisabled("%s", text);
        ImGui::PopTextWrapPos();
    }

    static bool PropertyEnum(const char* label, int& value, const char* const* items, int count, const char* tooltip = nullptr)
    {
        bool changed = UI::PropertyCombo(label, value, items, count);
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip(tooltip);
        return changed;
    }

    bool SceneHierarchyPanel::DrawParticleSystem(Entity entity, ParticleSystemComponent& c)
    {
        bool changed = false;
        UI::PropertyLabel("");
        if (ImGui::Button(c.Playing ? ICON_FA_PAUSE "  Pauza" : ICON_FA_PLAY "  Ijro")) { c.Playing = !c.Playing; }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Qayta")) c.Clear();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu", c.Particles.size());
        changed |= UI::PropertyBool("Takrorlash", c.Looping);
        changed |= UI::PropertyBool("Editorda ko'rsatish", c.PlayInEditor);
        changed |= UI::PropertyFloat("Chiqarish (1/s)", c.EmissionRate, 0.5f, 0.0f, 10000.0f);
        changed |= UI::PropertyInt("Portlash (burst)", c.Burst);
        changed |= UI::PropertyInt("Maksimal soni", c.MaxParticles);

        // ---- shape
        UI::PropertyHeader(ICON_FA_SHAPES "  Shakl", "Particle'lar qayerdan va qaysi yo'nalishda chiqadi (emitterning o'z o'qlarida)");
        static const char* const shapes[] = { "Nuqta", "Doira", "Sfera", "Quti", "Konus", "Chiziq (2D)" };
        int shape = (int)c.Shape;
        if (UI::PropertyCombo("Shakl", shape, shapes, 6)) { c.Shape = (PS::EmitterShape)shape; changed = true; }
        switch (c.Shape)
        {
            case PS::EmitterShape::Box:
                changed |= UI::PropertyFloat3("O'lchami", c.ShapeExtents);
                break;
            case PS::EmitterShape::Edge:
                changed |= UI::PropertyFloat("Uzunligi", c.EdgeLength, 0.01f, 0.0f, 1000.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Lokal X o'qi bo'ylab chiziq (markazi emitterda): yomg'ir, sharshara, platforma cheti");
                break;
            case PS::EmitterShape::Cone:
            {
                changed |= UI::PropertyFloat("Asos radiusi", c.ShapeRadius, 0.01f, 0.0f, 1000.0f);
                if (UI::PropertySlider("Burchak (°)", c.ConeAngle, 0.0f, 89.0f))
                {
                    c.ConeAngle = std::clamp(c.ConeAngle, 0.0f, 89.0f);
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Asos chetidagi particle o'qdan qancha og'adi (0 - to'g'ri nur / silindr)");
                static const char* const from[] = { "Asosdan", "Hajmidan" };
                int f = (int)c.ConeFrom;
                if (PropertyEnum("Chiqish joyi", f, from, 2, "Asosdan: konusning tubidan; Hajmidan: konus ichidagi istalgan joydan (Uzunlik chuqurlikkacha)"))
                {
                    c.ConeFrom = (PS::ConeEmitFrom)f;
                    changed = true;
                }
                if (c.ConeFrom == PS::ConeEmitFrom::Volume)
                    changed |= UI::PropertyFloat("Uzunligi", c.ConeLength, 0.01f, 0.0f, 1000.0f);
                changed |= UI::PropertySlider("Qalinlik", c.ShapeThickness, 0.0f, 1.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Radiusning qancha qismidan chiqadi (chetdan ichkariga): 1 - butun asos, 0 - faqat cheti (halqa)");
                changed |= UI::PropertyBool("Tekis (2D)", c.ConeFlat);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Asos - o'qqa ko'ndalang chiziq, hamma narsa XY tekisligida (2D o'yinlar uchun yelpig'ich)");
                break;
            }
            case PS::EmitterShape::Circle:
            case PS::EmitterShape::Sphere:
                changed |= UI::PropertyFloat("Radius", c.ShapeRadius, 0.01f, 0.0f, 1000.0f);
                changed |= UI::PropertySlider("Qalinlik", c.ShapeThickness, 0.0f, 1.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Radiusning qancha qismidan chiqadi (chetdan ichkariga): 1 - butun hajm, 0 - faqat sirti / cheti");
                break;
            default: break;
        }
        if (c.Shape == PS::EmitterShape::Circle || c.Shape == PS::EmitterShape::Sphere || c.Shape == PS::EmitterShape::Box)
        {
            changed |= UI::PropertyBool("Markazdan tashqariga", c.AlignToShape);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Particle'lar Yo'nalish o'rniga markazdan tashqariga uchadi (tarqalish shu yo'nalish atrofida): portlash");
        }

        // ---- motion
        UI::PropertyHeader(ICON_FA_PERSON_RUNNING "  Harakat");
        changed |= UI::PropertyFloat("Umr (min)", c.LifetimeMin, 0.01f, 0.01f, 100.0f);
        changed |= UI::PropertyFloat("Umr (max)", c.LifetimeMax, 0.01f, 0.01f, 100.0f);
        changed |= UI::PropertyFloat("Tezlik (min)", c.SpeedMin, 0.05f);
        changed |= UI::PropertyFloat("Tezlik (max)", c.SpeedMax, 0.05f);
        changed |= UI::PropertyFloat3(c.Shape == PS::EmitterShape::Cone ? "Yo'nalish (o'q)" : "Yo'nalish", c.Direction);
        ImGui::BeginDisabled(c.Shape == PS::EmitterShape::Cone);
        changed |= UI::PropertySlider("Tarqalish (°)", c.Spread, 0.0f, 180.0f);
        ImGui::EndDisabled();
        if (c.Shape == PS::EmitterShape::Cone && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Konusda tarqalishni uning burchagi beradi");
        changed |= UI::PropertyFloat3("Gravitatsiya", c.Gravity);
        changed |= UI::PropertyFloat("Qarshilik", c.Drag, 0.01f, 0.0f, 50.0f);
        changed |= UI::PropertyFloat("Aylanish (max)", c.AngularVelocityMax, 0.05f, 0.0f, 100.0f);

        // ---- look
        UI::PropertyHeader(ICON_FA_EYE "  Ko'rinish");
        ImGui::BeginDisabled(c.ColorOverLifetime.Enabled);
        changed |= UI::PropertyColor4("Boshlang'ich rang", c.StartColor);
        changed |= UI::PropertyColor4("Oxirgi rang", c.EndColor);
        ImGui::EndDisabled();
        if (c.ColorOverLifetime.Enabled)
            Note("Rangni \"Rang (umr bo'yi)\" moduli beradi");
        changed |= UI::PropertyFloat("Boshlang'ich o'lcham", c.StartSize, 0.01f, 0.0f, 100.0f);
        ImGui::BeginDisabled(c.SizeOverLifetime.Enabled);
        changed |= UI::PropertyFloat("Oxirgi o'lcham", c.EndSize, 0.01f, 0.0f, 100.0f);
        ImGui::EndDisabled();
        if (c.SizeOverLifetime.Enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("O'lcham = Boshlang'ich o'lcham x \"O'lcham (umr bo'yi)\" egri chizig'i");
        changed |= UI::PropertyBool("Doira shaklida", c.RenderAsCircle);
        changed |= UI::PropertyBool("Qo'shimcha yorqinlik (additive)", c.Additive);
        changed |= UI::PropertyBool("Dunyo koordinatalari", c.WorldSpace);
        changed |= UI::PropertyBool("Kameraga qaragan", c.Billboard);
        if (UI::PropertyAssetPath("Tekstura", c.TexturePath, ".png;.jpg;.jpeg", ICON_FA_FILE_IMAGE)) { c.Texture = nullptr; changed = true; }
        changed |= DrawSortingLayer(c.SortingLayer, c.SortingOrder);

        // ---- modules
        bool wasOn = false;
        {
            auto& m = c.ColorOverLifetime;
            if (ModuleHeader("##colorModule", ICON_FA_PALETTE, "Rang (umr bo'yi)", m.Enabled, changed,
                             "Particle rangi butun umri davomida: ko'p nuqtali gradient (Boshlang'ich / Oxirgi rang o'rniga)", wasOn))
            {
                // Switched on for the first time: the gradient starts as the two colours it had.
                if (m.Enabled && !wasOn && m.Gradient == PS::ColorOverLifetimeModule{}.Gradient)
                    m.Gradient = ParticleGradient::TwoStops(c.StartColor, c.EndColor);
                ImGui::BeginDisabled(!m.Enabled);
                changed |= UI::PropertyGradient("Gradient", m.Gradient, "Chap - tug'ilganda, o'ng - o'lganda. Alfa - shaffoflik");
                changed |= UI::PropertyBool("Ikki gradient orasida", m.RandomBetweenTwo);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Har bir particle ikki gradient orasidagi tasodifiy rangda (rang-barang uchqunlar, barglar)");
                if (m.RandomBetweenTwo)
                    changed |= UI::PropertyGradient("Gradient B", m.GradientB);
                ImGui::EndDisabled();
            }
            else if (m.Enabled && !wasOn && m.Gradient == PS::ColorOverLifetimeModule{}.Gradient)
                m.Gradient = ParticleGradient::TwoStops(c.StartColor, c.EndColor);
        }
        {
            auto& m = c.SizeOverLifetime;
            auto seed = [&]() {
                // Switched on for the first time: the curve starts as the sizes it had.
                if (m.Enabled && !wasOn && m.Curve == PS::SizeOverLifetimeModule{}.Curve)
                    m.Curve = ParticleCurve::Linear(1.0f, c.StartSize > 0.0f ? c.EndSize / c.StartSize : 0.0f);
            };
            if (ModuleHeader("##sizeModule", ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "O'lcham (umr bo'yi)", m.Enabled, changed,
                             "Particle o'lchami butun umri davomida: Boshlang'ich o'lcham x egri chiziq (Oxirgi o'lcham o'rniga)", wasOn))
            {
                seed();
                ImGui::BeginDisabled(!m.Enabled);
                changed |= UI::PropertyCurve("Egri chiziq", m.Curve, 0.0f, 1.0f, "Boshlang'ich o'lchamga ko'paytiriladi: 1 - o'zi, 0 - ko'rinmaydi");
                changed |= UI::PropertyBool("Ikki egri chiziq orasida", m.RandomBetweenTwo);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Har bir particle ikki egri chiziq orasidagi tasodifiy o'lchamda");
                if (m.RandomBetweenTwo)
                    changed |= UI::PropertyCurve("Egri chiziq B", m.CurveB);
                ImGui::EndDisabled();
            }
            else
                seed();
        }
        {
            auto& m = c.VelocityOverLifetime;
            if (ModuleHeader("##velocityModule", ICON_FA_WIND, "Tezlik (umr bo'yi)", m.Enabled, changed,
                             "Particle harakatiga umri davomida qo'shiladigan tezlik: chiziqli, orbita, radial; o'z tezligiga ko'paytuvchi", wasOn))
            {
                ImGui::BeginDisabled(!m.Enabled);
                changed |= UI::PropertyFloat3("Chiziqli", m.Linear);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Birlik / s: shamol, oqim (particle'ning o'z tezligini o'zgartirmaydi)");
                changed |= UI::PropertyCurve("Chiziqli (egri)", m.LinearCurve, 0.0f, 1.0f, "Chiziqli tezlikka umr bo'yi ko'paytiriladi");
                changed |= UI::PropertyBool("Dunyo o'qlarida", m.World);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Chiziqli tezlik dunyo o'qlarida (o'chiq: emitter aylansa u ham aylanadi)");
                changed |= UI::PropertyFloat3("Orbita (rad/s)", m.Orbital, 0.02f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Emitter markazi atrofida uning X, Y, Z o'qlari bo'yicha aylanish: girdob, sehrli halqa");
                changed |= UI::PropertyFloat("Radial", m.Radial, 0.05f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Markazdan tashqariga (manfiy - markazga) birlik / s");
                changed |= UI::PropertyCurve("Tezlik ko'paytuvchisi", m.SpeedModifier, 0.0f, 1.0f,
                                             "Particle'ning o'z tezligiga umr bo'yi ko'paytiriladi (0 - to'xtaydi)");
                ImGui::EndDisabled();
            }
        }
        {
            auto& m = c.LimitVelocity;
            if (ModuleHeader("##limitModule", ICON_FA_GAUGE_HIGH, "Tezlik chegarasi", m.Enabled, changed,
                             "Particle tezligi chegaradan oshsa, ortig'i so'ndiriladi (sekinlashib to'xtaydigan uchqunlar, tutun)", wasOn))
            {
                ImGui::BeginDisabled(!m.Enabled);
                if (UI::PropertyFloat("Maksimal tezlik", m.MaxSpeed, 0.05f, 0.0f, 10000.0f))
                {
                    m.MaxSpeed = std::max(0.0f, m.MaxSpeed);
                    changed = true;
                }
                changed |= UI::PropertyCurve("Chegara (egri)", m.MaxSpeedCurve, 0.0f, 1.0f, "Maksimal tezlikka umr bo'yi ko'paytiriladi");
                changed |= UI::PropertySlider("So'ndirish", m.Dampen, 0.0f, 1.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Chegaradan ortiq tezlikning har 1/60 soniyada qancha qismi yo'qoladi (1 - darhol chegaraga)");
                ImGui::EndDisabled();
            }
        }
        {
            auto& m = c.TextureSheet;
            if (ModuleHeader("##sheetModule", ICON_FA_FILM, "Tekstura sheet (flipbook)", m.Enabled, changed,
                             "Tekstura kadrlar to'plami: particle umri davomida (yoki FPS bilan) kadrdan kadrga o'tadi - olov, tutun, portlash", wasOn))
            {
                ImGui::BeginDisabled(!m.Enabled);
                static const char* const sources[] = { "Panjara (ustun x qator)", "Sprite kadrlari (.zmeta)" };
                int source = (int)m.Source;
                if (PropertyEnum("Kadrlar", source, sources, 2, "Panjara: tekstura teng kataklarga bo'linadi (chapdan o'ngga, yuqoridan pastga); "
                                                                 "Sprite kadrlari: teksturaning Sprite Editor'da kesilgan nomli kadrlari"))
                {
                    m.Source = (PS::TextureSheetModule::FrameSource)source;
                    changed = true;
                }
                int frames = 0;
                if (m.Source == PS::TextureSheetModule::FrameSource::Grid)
                {
                    glm::ivec2 grid(m.Columns, m.Rows);
                    UI::PropertyLabel("Ustun x qator");
                    if (ImGui::DragInt2("##grid", &grid.x, 0.1f, 1, 64))
                    {
                        m.Columns = std::clamp(grid.x, 1, 256);
                        m.Rows = std::clamp(grid.y, 1, 256);
                        changed = true;
                    }
                    UI::PropertyLabel("Kadrlar soni", "0 - hamma kataklar; aks holda faqat birinchi N tasi (oxirgi qator to'liq bo'lmasa)");
                    if (ImGui::DragInt("##count", &m.FrameCount, 0.1f, 0, m.Columns * m.Rows, m.FrameCount == 0 ? "hammasi" : "%d"))
                    {
                        m.FrameCount = std::max(0, m.FrameCount);
                        changed = true;
                    }
                    frames = ParticleSimulation::GetGridFrameCount(c);
                    changed |= UI::PropertyBool("Tasodifiy qator", m.RandomRow);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        UI::Tooltip("Har bir particle bitta tasodifiy qatorni o'ynaydi (bir teksturada bir nechta animatsiya)");
                }
                else
                {
                    const SpriteSheet* sheet = c.TexturePath.empty() ? nullptr : SpriteSheet::Get(c.TexturePath);
                    UI::PropertyLabel("Prefiks", "Faqat \"<prefiks>_<raqam>\" nomli kadrlar raqam tartibida (bo'sh - hamma kadrlar)");
                    if (sheet && !sheet->GetSequencePrefixes().empty())
                    {
                        if (ImGui::BeginCombo("##prefix", m.SpritePrefix.empty() ? "(hamma kadrlar)" : m.SpritePrefix.c_str()))
                        {
                            if (ImGui::Selectable("(hamma kadrlar)", m.SpritePrefix.empty())) { m.SpritePrefix.clear(); changed = true; }
                            for (const std::string& prefix : sheet->GetSequencePrefixes())
                                if (ImGui::Selectable(prefix.c_str(), prefix == m.SpritePrefix)) { m.SpritePrefix = prefix; changed = true; }
                            ImGui::EndCombo();
                        }
                    }
                    else
                        changed |= ImGui::InputText("##prefix", &m.SpritePrefix);
                    frames = !sheet ? 0 : m.SpritePrefix.empty() ? (int)sheet->Frames.size() : (int)sheet->FindSequence(m.SpritePrefix).size();
                    if (!sheet && !c.TexturePath.empty())
                        Note("Teksturada sprite kadrlari yo'q (Sprite Editor > Kesish)", true);
                }
                static const char* const modes[] = { "Umr bo'yi", "Kadr tezligi (FPS)" };
                int mode = (int)m.Mode;
                if (PropertyEnum("Vaqt", mode, modes, 2, "Umr bo'yi: har particle kadrlardan umri davomida Sikllar marta o'tadi; "
                                                         "Kadr tezligi: soniyasiga shuncha kadr (umridan qat'i nazar)"))
                {
                    m.Mode = (PS::TextureSheetModule::TimeMode)mode;
                    changed = true;
                }
                if (m.Mode == PS::TextureSheetModule::TimeMode::Lifetime)
                    changed |= UI::PropertyFloat("Sikllar", m.Cycles, 0.05f, 0.0f, 100.0f);
                else
                    changed |= UI::PropertyFloat("FPS", m.FrameRate, 0.25f, 0.0f, 240.0f);
                UI::PropertyLabel("Boshlang'ich kadr");
                if (ImGui::DragInt("##start", &m.StartFrame, 0.1f, 0, std::max(0, frames - 1)))
                {
                    m.StartFrame = std::max(0, m.StartFrame);
                    changed = true;
                }
                changed |= UI::PropertyBool("Tasodifiy boshlanish", m.RandomStartFrame);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Har bir particle tasodifiy kadrdan boshlaydi (bir xil ko'rinmasligi uchun)");
                if (c.TexturePath.empty())
                    Note("Tekstura tanlanmagan (Ko'rinish > Tekstura)", true);
                else
                    Note(std::format("{} ta kadr", frames).c_str());
                ImGui::EndDisabled();
            }
        }
        (void)entity;
        return changed;
    }

}
