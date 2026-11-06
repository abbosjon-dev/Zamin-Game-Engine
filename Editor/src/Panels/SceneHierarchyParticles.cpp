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
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Tirik particle'lar soni");
        // Where the system is in its cycle.
        {
            const float delay = std::max(0.0f, c.StartDelay);
            const float cycle = std::max(0.01f, c.Duration);
            UI::PropertyLabel("Sikl", "Tizim o'z siklining qayerida (kechikish, keyin Davomiylik)");
            std::string text;
            float fraction = 0.0f;
            if (c.Finished)
                text = "tugadi";
            else if (c.PlayTime < delay)
            {
                text = std::format("kechikish {:.1f} / {:.1f} s", c.PlayTime, delay);
                fraction = delay > 0.0f ? c.PlayTime / delay : 0.0f;
            }
            else
            {
                const float t = std::min(c.PlayTime - delay, cycle);
                text = std::format("{:.1f} / {:.1f} s", t, cycle);
                fraction = t / cycle;
            }
            ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f), text.c_str());
        }

        // ---- main
        UI::PropertyHeader(ICON_FA_HOURGLASS_HALF "  Asosiy", "Sikl, vaqt, fazo va tizim tugaganda nima bo'lishi");
        if (UI::PropertyFloat("Davomiylik (s)", c.Duration, 0.05f, 0.01f, 10000.0f))
        {
            c.Duration = std::max(0.01f, c.Duration);
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Bir sikl: portlashlar jadvali shu vaqt ichida; takrorlanmasa - shuncha chiqaradi, keyin tugaydi");
        changed |= UI::PropertyBool("Takrorlash", c.Looping);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Sikl tugagach yana boshidan (o'chiq: bir marta - portlash, otishma effekti)");
        ImGui::BeginDisabled(!c.Looping);
        if (UI::PropertyBool("Oldindan to'la", c.Prewarm))
        {
            c.Clear(); // previewed from the start: full at once
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Prewarm (faqat takrorlanadiganda): tizim boshidanoq bir butun sikl ishlagandek to'la - tutun ustuni, "
                        "gulxan uchqunlari. Sub-emitterlar bu vaqtda ishlamaydi");
        if (UI::PropertyFloat("Kechikish", c.StartDelay, 0.02f, 0.0f, 10000.0f, "%.2f s"))
        {
            c.StartDelay = std::max(0.0f, c.StartDelay);
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Boshlanish kechikishi: shuncha soniyadan keyin chiqara boshlaydi (bir nechta effektni ketma-ket boshlash)");
        if (UI::PropertyFloat("Vaqt tezligi", c.SimulationSpeed, 0.01f, 0.0f, 100.0f, "x%.2f"))
        {
            c.SimulationSpeed = std::max(0.0f, c.SimulationSpeed);
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Simulyatsiya tezligi: tizim vaqti shuncha marta tez o'tadi: 0.5 - sekin harakat, 2 - ikki baravar tez (0 - qotib qoladi)");
        {
            static const char* const spaces[] = { "Lokal (emitter bilan)", "Dunyo", "Boshqa obyekt" };
            int space = (int)c.Space;
            if (PropertyEnum("Fazo", space, spaces, 3, "Dunyo: particle'lar tug'ilgan joyida qoladi (emitter yursa iz qoladi); "
                                                       "Lokal: emitter bilan birga siljiydi va aylanadi; Boshqa obyekt: o'sha "
                                                       "obyekt bilan (masalan kema ustidagi to'p - kema bilan, to'p burilishi bilan emas)"))
            {
                c.Space = (PS::SimulationSpace)space;
                c.Clear(); // the particles' positions were in the old space
                changed = true;
            }
            if (c.Space == PS::SimulationSpace::Custom)
            {
                if (DrawEntityRefField("Fazo obyekti", c.CustomSpace, "Particle'lar shu obyekt bilan birga siljiydi va aylanadi"))
                {
                    c.Clear();
                    changed = true;
                }
                if (!c.CustomSpace)
                    Note("Obyekt tanlanmagan: dunyo fazosida", true);
                else if (!(m_Context && m_Context->GetEntityByUUID(c.CustomSpace.ID)))
                    Note("Obyekt topilmadi: dunyo fazosida", true);
            }
        }
        {
            static const char* const actions[] = { "Hech narsa", "Obyektni o'chirish", "Obyektni yo'q qilish" };
            int action = (int)c.StopActionType;
            if (PropertyEnum("Tugaganda", action, actions, 3, "Takrorlanmaydigan tizim siklini o'ynab, oxirgi particle'i o'lganda "
                                                              "(faqat o'yin paytida): obyekt o'chiriladi yoki yo'q qilinadi. Skriptlar "
                                                              "OnParticlesFinished va \"ParticlesFinished\" hodisasini oladi"))
            {
                c.StopActionType = (PS::StopAction)action;
                changed = true;
            }
            if (c.Looping && c.StopActionType != PS::StopAction::None)
                Note("Takrorlanadigan tizim hech qachon tugamaydi");
        }
        changed |= UI::PropertyBool("Editorda ko'rsatish", c.PlayInEditor);
        changed |= UI::PropertyInt("Maksimal soni", c.MaxParticles);

        // ---- emission
        UI::PropertyHeader(ICON_FA_BURST "  Emissiya", "Qancha particle va qachon chiqadi");
        changed |= UI::PropertyFloat("Chiqarish (1/s)", c.EmissionRate, 0.5f, 0.0f, 10000.0f);
        changed |= UI::PropertyInt("Boshida portlash", c.Burst);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Tizim ijro boshlanganda bir marta shuncha particle (har siklda emas - buning uchun portlashlar jadvali)");
        changed |= DrawParticleBursts(c);

        // ---- shape
        UI::PropertyHeader(ICON_FA_SHAPES "  Shakl", "Particle'lar qayerdan va qaysi yo'nalishda chiqadi (emitterning o'z o'qlarida)");
        static const char* const shapes[] = { "Nuqta", "Doira", "Sfera", "Quti", "Konus", "Chiziq (2D)", "Mesh sirti" };
        int shape = (int)c.Shape;
        if (UI::PropertyCombo("Shakl", shape, shapes, 7)) { c.Shape = (PS::EmitterShape)shape; changed = true; }
        switch (c.Shape)
        {
            case PS::EmitterShape::Mesh:
            {
                static const char* const sources[] = { "Primitiv", "Model fayli", "Mesh Renderer" };
                int source = (int)c.MeshSource;
                if (PropertyEnum("Mesh manbai", source, sources, 3, "Primitiv: kub, sfera...; Model fayli: .gltf / .glb / .obj; "
                                                                   "Mesh Renderer: shu obyektning o'zi ko'rsatayotgan mesh"))
                {
                    c.MeshSource = (PS::MeshShapeSource)source;
                    changed = true;
                }
                if (c.MeshSource == PS::MeshShapeSource::Primitive)
                {
                    static const char* const primitives[] = { "Kub", "Sfera", "Tekislik", "Silindr", "Konus" };
                    int primitive = std::clamp((int)c.MeshPrimitive, 0, 4);
                    if (UI::PropertyCombo("Primitiv", primitive, primitives, 5)) { c.MeshPrimitive = (PrimitiveMesh)primitive; changed = true; }
                }
                else if (c.MeshSource == PS::MeshShapeSource::Model)
                    changed |= UI::PropertyAssetPath("Model", c.MeshModelPath, ".gltf;.glb;.obj", ICON_FA_CUBE);
                else if (!entity.HasComponent<MeshRendererComponent>())
                    Note("Obyektda Mesh Renderer yo'q", true);
                static const char* const modes[] = { "Sirt (uchburchaklar)", "Qirralar", "Uchlar (vertex)" };
                int mode = (int)c.MeshMode;
                if (PropertyEnum("Qayerdan", mode, modes, 3, "Sirt: uchburchaklarning istalgan nuqtasidan (yuzasiga qarab teng); "
                                                             "Qirralar: qirralar bo'ylab (tekis yuzlar ichidagi diagonallarsiz); Uchlar: faqat uchlaridan"))
                {
                    c.MeshMode = (PS::MeshEmitMode)mode;
                    changed = true;
                }
                changed |= UI::PropertyBool("Normal bo'ylab", c.MeshAlongNormal);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Particle'lar sirtdan tashqariga (normal bo'ylab, tarqalish shu atrofida) uchadi; o'chiq: Yo'nalish bo'ylab");
                changed |= UI::PropertyFloat("Sirtdan masofa", c.MeshNormalOffset, 0.005f, -100.0f, 100.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Normal bo'ylab shuncha tashqarida tug'iladi (manfiy - ichkarida)");
                if (c.MeshRuntime)
                    Note(std::format("{} ta uchburchak, yuzasi {:.2f}", c.MeshRuntime->GetTriangleCount(), c.MeshRuntime->GetArea()).c_str());
                else if (c.MeshSource == PS::MeshShapeSource::Model && c.MeshModelPath.empty())
                    Note("Model tanlanmagan: markazdan chiqadi", true);
                break;
            }
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
        changed |= UI::PropertyBool("Kameraga qaragan", c.Billboard);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("3D: har particle kameraga qaraydi (GPU instancing bilan bitta chizish buyrug'ida); o'chiq: XY tekisligida (2D)");
        {
            static const char* const sorts[] = { "Avto", "Yo'q (tug'ilish tartibi)", "Masofa (orqadan oldinga)", "Eskisi ustida", "Yangisi ustida" };
            int sort = (int)c.Sorting;
            if (PropertyEnum("Saralash", sort, sorts, 5, "Qaysi particle ustida chiziladi. Avto: shaffof (additive emas) tizimlar kameradan "
                                                         "uzoqlik bo'yicha orqadan oldinga - yaqindagisi uzoqdagisini to'g'ri yopadi; "
                                                         "additive'lar tartibsiz (yorug'lik qo'shiladi, tartib ahamiyatsiz)"))
            {
                c.Sorting = (PS::SortMode)sort;
                changed = true;
            }
        }
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
            auto& m = c.Noise;
            if (ModuleHeader("##noiseModule", ICON_FA_TORNADO, "Shovqin (turbulentlik)", m.Enabled, changed,
                             "Silliq shovqin maydoni particle'larni burab-silkitib suradi: tutun, olov tillari, sehrli chang, pashshalar", wasOn))
            {
                ImGui::BeginDisabled(!m.Enabled);
                changed |= UI::PropertyFloat3("Kuch (birlik/s)", m.Strength, 0.02f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Har bir o'q bo'ylab eng ko'p tezlik (0 - shu o'q bo'ylab yo'q; 2D o'yinlarda Z = 0)");
                changed |= UI::PropertyCurve("Kuch (egri)", m.StrengthCurve, 0.0f, 1.0f, "Kuchga umr bo'yi ko'paytiriladi");
                if (UI::PropertyFloat("Chastota", m.Frequency, 0.01f, 0.0f, 100.0f))
                {
                    m.Frequency = std::max(0.0f, m.Frequency);
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Bir birlikda nechta girdob: kichik - katta silliq to'lqinlar, katta - mayda titroq");
                changed |= UI::PropertyFloat("Siljish tezligi", m.ScrollSpeed, 0.01f, -100.0f, 100.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Maydon vaqt o'tishi bilan qanchalik tez o'zgaradi (0 - qotib turadi)");
                UI::PropertyLabel("Oktavalar", "Ustma-ust qatlamlar: har biri ikki baravar mayda (ko'proq - tabiiyroq, sekinroq)");
                if (ImGui::SliderInt("##octaves", &m.Octaves, 1, 4))
                {
                    m.Octaves = std::clamp(m.Octaves, 1, 4);
                    changed = true;
                }
                if (m.Octaves > 1)
                    changed |= UI::PropertySlider("Oktava kuchi", m.OctaveMultiplier, 0.0f, 1.0f);
                UI::PropertyLabel("Urug' (seed)", "Boshqa son - shu turdagi boshqa maydon (bir xil effektlar bir xil harakatlanmasligi uchun)");
                int seed = (int)m.Seed;
                if (ImGui::DragInt("##seed", &seed, 0.2f, 0, 1 << 30))
                {
                    m.Seed = (uint32_t)std::max(0, seed);
                    changed = true;
                }
                ImGui::EndDisabled();
            }
        }
        {
            auto& m = c.Collision;
            using Type = PS::CollisionModule::CollisionType;
            if (ModuleHeader("##collisionModule", ICON_FA_BASKETBALL, "To'qnashuv", m.Enabled, changed,
                             "Particle'lar yer tekisligidan yoki fizika colliderlaridan sakraydi: uchqunlar, yomg'ir tomchilari, tushayotgan barglar", wasOn))
            {
                ImGui::BeginDisabled(!m.Enabled);
                static const char* const types[] = { "Tekislik", "Dunyo (3D fizika)", "Dunyo (2D fizika)" };
                int type = (int)m.Type;
                if (PropertyEnum("Turi", type, types, 3, "Tekislik: cheksiz yer tekisligi (arzon, editorda ham ishlaydi); Dunyo: fizika "
                                                         "colliderlariga nur (raycast) - faqat o'yin / simulyatsiya paytida"))
                {
                    m.Type = (Type)type;
                    changed = true;
                }
                if (m.Type == Type::Plane)
                {
                    changed |= DrawEntityRefField("Tekislik obyekti", m.PlaneEntity,
                                                  "Tanlansa: tekislik o'sha obyekt orqali o'tadi, uning lokal Y o'qi - normal (yer, platforma). "
                                                  "Bo'sh: quyidagi normal va masofa (dunyo koordinatalarida)");
                    if (!m.PlaneEntity)
                    {
                        changed |= UI::PropertyFloat3("Normal", m.PlaneNormal, 0.01f);
                        changed |= UI::PropertyFloat("Masofa", m.PlaneOffset, 0.01f, -10000.0f, 10000.0f);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                            UI::Tooltip("Tekislik boshlang'ich nuqtadan normal bo'ylab shuncha uzoqda: yuqoriga normal uchun - yer balandligi (Y)");
                    }
                }
                else
                {
                    changed |= DrawLayerMask("Qatlamlar", m.CollidesWith, "Faqat shu to'qnashuv qatlamlaridagi colliderlar bilan");
                    Note(m.Type == Type::World3D ? "3D fizika colliderlari bilan - faqat o'yin / simulyatsiya paytida"
                                                 : "2D fizika colliderlari bilan - faqat o'yin / simulyatsiya paytida");
                }
                changed |= UI::PropertySlider("Sakrash", m.Bounce, 0.0f, 1.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Sirtga qarab tezlikning qancha qismi qaytadi: 0 - yopishadi, 1 - to'liq sakraydi");
                changed |= UI::PropertySlider("So'ndirish", m.Dampen, 0.0f, 1.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Har urilishda sirt bo'ylab tezlikning qancha qismi yo'qoladi (ishqalanish; sirtda yotganda - har 1/60 s)");
                changed |= UI::PropertySlider("Umr yo'qotish", m.LifetimeLoss, 0.0f, 1.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Har urilishda butun umrining qancha qismi yo'qoladi: 1 - urilganda o'ladi (yomg'ir tomchisi)");
                if (UI::PropertyFloat("O'ldirish tezligi", m.MinKillSpeed, 0.01f, 0.0f, 1000.0f))
                {
                    m.MinKillSpeed = std::max(0.0f, m.MinKillSpeed);
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Urilgandan keyin bundan sekin bo'lsa o'ladi (0 - hech qachon)");
                if (UI::PropertyFloat("Radius ko'paytuvchi", m.RadiusScale, 0.01f, 0.0f, 10.0f))
                {
                    m.RadiusScale = std::max(0.0f, m.RadiusScale);
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Particle - o'lchamining yarmi x shu radiusli shar (0 - nuqta)");
                ImGui::EndDisabled();
            }
        }
        {
            auto& m = c.SubEmitters;
            using Trigger = PS::SubEmitter::TriggerType;
            if (ModuleHeader("##subModule", ICON_FA_BURST, "Sub-emitterlar", m.Enabled, changed,
                             "Particle tug'ilganda, o'lganda yoki urilganda boshqa obyektning Particle System'i o'sha joyda yangi particle'lar "
                             "chiqaradi: mushak (salyut), yomg'ir sachrashi, uchqun izi", wasOn))
            {
                ImGui::BeginDisabled(!m.Enabled);
                static const char* const triggers[] = { "Tug'ilganda", "O'lganda", "Urilganda", "Umr bo'yi" };
                int remove = -1;
                for (int i = 0; i < (int)m.Emitters.size(); i++)
                {
                    auto& sub = m.Emitters[(size_t)i];
                    ImGui::PushID(i);
                    UI::PropertyLabel(std::format("#{}", i + 1).c_str());
                    int trigger = (int)sub.Trigger;
                    ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x));
                    if (ImGui::Combo("##trigger", &trigger, triggers, 4))
                    {
                        sub.Trigger = (Trigger)trigger;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        UI::Tooltip("Qachon: tug'ilganda / o'lganda / urilganda (To'qnashuv moduli) - har safar Soni ta; "
                                    "Umr bo'yi - har tirik particle'dan soniyasiga Soni ta");
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_TRASH, ImVec2(ImGui::GetFrameHeight(), 0)))
                        remove = i;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        UI::Tooltip("Olib tashlash");
                    changed |= DrawEntityRefField("Shablon", sub.Emitter, "Particle System'i bor obyekt: yangi particle'lar uning sozlamalari bilan "
                                                                         "(shakli o'sha nuqta atrofida). Odatda o'zi chiqarmaydi (Chiqarish 0)");
                    if (sub.Emitter)
                    {
                        Entity target = m_Context ? m_Context->GetEntityByUUID(sub.Emitter.ID) : Entity();
                        if (target && target == entity)
                            Note("Tizim o'ziga sub-emitter bo'la olmaydi", true);
                        else if (target && !target.HasComponent<ParticleSystemComponent>())
                            Note("Bu obyektda Particle System yo'q", true);
                        else if (target && !target.GetComponent<ParticleSystemComponent>().PlayInEditor)
                            Note("Shablonda \"Editorda ko'rsatish\" o'chiq: editorda ko'rinmaydi");
                    }
                    if (UI::PropertyInt(sub.Trigger == Trigger::Lifetime ? "Soni (1/s)" : "Soni", sub.Count))
                    {
                        sub.Count = std::clamp(sub.Count, 0, 100000);
                        changed = true;
                    }
                    changed |= UI::PropertySlider("Ehtimollik", sub.Probability, 0.0f, 1.0f);
                    changed |= UI::PropertyFloat("Tezlik merosi", sub.InheritVelocity, 0.01f, -10.0f, 10.0f);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        UI::Tooltip("Ota particle tezligining qancha qismi yangilariga qo'shiladi (1 - to'liq: harakatda portlaydi)");
                    changed |= UI::PropertyBool("Rang merosi", sub.InheritColor);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        UI::Tooltip("Yangi particle'lar rangi ota particle rangiga (RGB) ko'paytiriladi: har xil rangli mushaklar");
                    changed |= UI::PropertyBool("O'lcham merosi", sub.InheritSize);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        UI::Tooltip("Yangi particle'lar o'lchami ota particle o'lchamiga ko'paytiriladi");
                    ImGui::PopID();
                }
                if (remove >= 0)
                {
                    m.Emitters.erase(m.Emitters.begin() + remove);
                    changed = true;
                }
                UI::PropertyLabel("");
                if (ImGui::Button(ICON_FA_PLUS "  Qo'shish"))
                {
                    m.Emitters.push_back({});
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_WAND_MAGIC_SPARKLES "  Yangi shablon"))
                    m_PendingSubEmitterFor = entity.GetUUID();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Bola obyekt yaratadi (o'zi chiqarmaydigan Particle System bilan) va uni \"O'lganda\" sub-emitter qiladi");
                if (m.Emitters.empty())
                    Note("Sub-emitter yo'q: qo'shing va shablon obyektni tanlang");
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
        {
            auto& m = c.Trails;
            if (ModuleHeader("##trailsModule", ICON_FA_METEOR, "Iz (trail)", m.Enabled, changed,
                             "Particle orqasida kameraga qaragan lenta - u yaqinda o'tgan joylar orqali: kometalar, mushak raketalari, sehrli chiziqlar", wasOn))
            {
                ImGui::BeginDisabled(!m.Enabled);
                changed |= UI::PropertySlider("Ulushi", m.Ratio, 0.0f, 1.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Particle'larning qancha qismi iz qoldiradi (1 - hammasi)");
                if (UI::PropertyFloat("Umri (s)", m.Lifetime, 0.01f, 0.0f, 100.0f))
                {
                    m.Lifetime = std::max(0.0f, m.Lifetime);
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Izning har bir nuqtasi shuncha soniya yashaydi (iz uzunligi = tezlik x umri)");
                if (UI::PropertyFloat("Nuqta oralig'i", m.MinVertexDistance, 0.005f, 0.0f, 100.0f))
                {
                    m.MinVertexDistance = std::max(0.0f, m.MinVertexDistance);
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Particle shuncha siljiganda yangi nuqta qo'shiladi (kichik - silliqroq, qimmatroq)");
                if (UI::PropertyInt("Maksimal nuqtalar", m.MaxPoints))
                {
                    m.MaxPoints = std::clamp(m.MaxPoints, 2, 256);
                    changed = true;
                }
                if (UI::PropertyFloat("Kenglik", m.Width, 0.01f, 0.0f, 100.0f))
                {
                    m.Width = std::max(0.0f, m.Width);
                    changed = true;
                }
                changed |= UI::PropertyBool("O'lchamga ko'paytirish", m.WidthFromSize);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Kenglik = particle o'lchami x Kenglik (o'chiq: Kenglik birliklarda)");
                changed |= UI::PropertyCurve("Kenglik (egri)", m.WidthCurve, 0.0f, 1.0f, "Chap - particle yonida, o'ng - izning oxirida");
                changed |= UI::PropertyGradient("Rang", m.Gradient, "Chap - particle yonida, o'ng - izning oxirida. Alfa - shaffoflik");
                changed |= UI::PropertyBool("Particle rangida", m.InheritColor);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Iz rangi particle'ning hozirgi rangiga ko'paytiriladi");
                if (c.TrailPool.Capacity > 0)
                    Note(std::format("{} ta iz", c.TrailPool.GetUsedCount()).c_str());
                ImGui::EndDisabled();
            }
        }
        return changed;
    }

    // The bursts list: a full-width table (time, count, cycles, interval, chance) between two
    // parts of the property grid.
    bool SceneHierarchyPanel::DrawParticleBursts(ParticleSystemComponent& c)
    {
        bool changed = false;
        UI::PropertyLabel("Portlashlar", "Har siklda: Vaqt - sikl boshidan soniya; Soni - nechta particle; Sikl - necha marta "
                                         "(" ICON_FA_INFINITY " - sikl oxirigacha); Oraliq - takrorlar orasidagi soniya; % - ehtimollik");
        if (ImGui::Button(ICON_FA_PLUS "  Portlash qo'shish"))
        {
            PS::EmissionBurst burst;
            if (!c.Bursts.empty())
                burst.Time = std::min(std::max(0.0f, c.Duration - 0.01f), c.Bursts.back().Time + 0.5f);
            c.Bursts.push_back(burst);
            changed = true;
        }
        if (c.Bursts.empty())
            return changed;
        UI::EndPropertyGrid();
        const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame |
                                      ImGuiTableFlags_PadOuterX;
        int remove = -1;
        if (ImGui::BeginTable("##bursts", 6, flags))
        {
            ImGui::TableSetupColumn("Vaqt", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Soni", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn("Sikl", ImGuiTableColumnFlags_WidthStretch, 0.7f);
            ImGui::TableSetupColumn("Oraliq", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)c.Bursts.size(); i++)
            {
                auto& b = c.Bursts[(size_t)i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##time", &b.Time, 0.01f, 0.0f, std::max(0.0f, c.Duration), "%.2f s"))
                {
                    b.Time = std::max(0.0f, b.Time);
                    changed = true;
                }
                if (b.Time >= std::max(0.01f, c.Duration) && ImGui::IsItemHovered())
                    UI::Tooltip("Vaqt Davomiylikdan katta: bu portlash hech qachon chiqmaydi");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt("##count", &b.Count, 0.5f, 0, 100000))
                {
                    b.Count = std::clamp(b.Count, 0, 100000);
                    changed = true;
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt("##cycles", &b.Cycles, 0.05f, 0, 1000, b.Cycles == 0 ? ICON_FA_INFINITY : "%d"))
                {
                    b.Cycles = std::clamp(b.Cycles, 0, 100000);
                    changed = true;
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::BeginDisabled(b.Cycles == 1);
                if (ImGui::DragFloat("##interval", &b.Interval, 0.01f, 0.0f, 1000.0f, "%.2f s"))
                {
                    b.Interval = std::max(0.0f, b.Interval);
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::TableSetColumnIndex(4);
                ImGui::SetNextItemWidth(-FLT_MIN);
                float percent = b.Probability * 100.0f;
                if (ImGui::DragFloat("##chance", &percent, 0.5f, 0.0f, 100.0f, "%.0f%%"))
                {
                    b.Probability = std::clamp(percent / 100.0f, 0.0f, 1.0f);
                    changed = true;
                }
                ImGui::TableSetColumnIndex(5);
                if (ImGui::Button(ICON_FA_TRASH, ImVec2(ImGui::GetFrameHeight(), 0)))
                    remove = i;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Olib tashlash");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (remove >= 0)
        {
            c.Bursts.erase(c.Bursts.begin() + remove);
            changed = true;
        }
        UI::BeginPropertyGrid("##afterBursts");
        return changed;
    }

    void SceneHierarchyPanel::CreateSubEmitterTemplate(UUID ownerID)
    {
        Entity owner = m_Context ? m_Context->GetEntityByUUID(ownerID) : Entity();
        if (!owner || !owner.HasComponent<ParticleSystemComponent>())
            return;
        Entity created = m_Context->CreateEntity(owner.GetName() + " (sub-emitter)");
        // A burst that only the parent makes: nothing of its own, every way out, a short life.
        auto& t = created.AddComponent<ParticleSystemComponent>();
        t.EmissionRate = 0.0f;
        t.Burst = 0;
        t.MaxParticles = 2000;
        t.Shape = PS::EmitterShape::Sphere;
        t.ShapeRadius = 0.05f;
        t.Spread = 180.0f;
        t.SpeedMin = 1.5f;
        t.SpeedMax = 3.5f;
        t.LifetimeMin = 0.5f;
        t.LifetimeMax = 1.0f;
        t.Gravity = { 0.0f, -3.0f, 0.0f };
        t.StartSize = 0.15f;
        t.EndSize = 0.0f;
        m_Context->SetParent(created, owner, false);
        auto& ps = owner.GetComponent<ParticleSystemComponent>(); // after the new component (storage may have moved)
        PS::SubEmitter sub;
        sub.Trigger = PS::SubEmitter::TriggerType::Death;
        sub.Emitter.ID = (uint64_t)created.GetUUID();
        sub.InheritColor = true;
        ps.SubEmitters.Enabled = true;
        ps.SubEmitters.Emitters.push_back(sub);
        MarkModified();
    }

}
