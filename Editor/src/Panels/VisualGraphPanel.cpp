#include <cstdlib>
#include "VisualGraphPanel.h"
#include "VisualGraphLayout.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Editor/EditorLanguage.h"
#include "Zamin/Gameplay/CombatWorld.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
namespace Zamin {
    namespace {
        ImU32 Color(Visual::Type t)
        {
            switch (t)
            {
                case Visual::Type::Exec:   return IM_COL32(235, 238, 250, 255);
                case Visual::Type::Bool:   return IM_COL32(235, 88, 120, 255);
                case Visual::Type::Number: return IM_COL32(85, 220, 160, 255);
                case Visual::Type::String: return IM_COL32(220, 118, 240, 255);
                case Visual::Type::Entity: return IM_COL32(86, 177, 249, 255);
                case Visual::Type::Struct:
                case Visual::Type::ArrayStruct: return IM_COL32(64, 205, 225, 255);
                case Visual::Type::Enum:
                case Visual::Type::ArrayEnum: return IM_COL32(245, 150, 60, 255);
                case Visual::Type::Curve:  return IM_COL32(150, 230, 90, 255);
                default:                   return IM_COL32(255, 194, 85, 255);
            }
        }
        // An enumerated Number pin (Visual::PinChoices): a combo of translated names that stores the index.
        bool EditChoice(const char* label, const std::vector<const char*>& choices, Visual::Value& v)
        {
            ImGui::PushID(label);
            GraphLayout::InlineLabel(label);
            const double x = v.is_number() ? v.get<double>() : -1;
            const int index = std::floor(x) == x && x >= 0 && x < double(choices.size()) ? int(x) : -1;
            bool changed = false;
            if (ImGui::BeginCombo("##choice", index >= 0 ? Tr(choices[size_t(index)]) : Tr("Noma'lum qiymat")))
            {
                for (int i = 0; i < int(choices.size()); ++i)
                {
                    ImGui::PushID(i);
                    if (ImGui::Selectable(Tr(choices[size_t(i)]), i == index) && i != index)
                    {
                        v = double(i);
                        changed = true;
                    }
                    if (i == index) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            GraphLayout::RecordKey(label);
            ImGui::PopID();
            return changed;
        }
        // Play only: the scene's combat counters and the selected actor's status effects (at most 16, one book).
        void CombatLive(Scene& scene, Entity actor)
        {
            auto* combat = scene.GetRegistry().ctx().find<Gameplay::CombatWorld>();
            if (!combat) return;
            ImGui::SeparatorText(Tr("Jang holati###CombatLive"));
            const auto stats = combat->Stats();
            ImGui::Text("%s: %u / %zu", Tr("Uchayotgan snaryadlar"), stats.Live, Gameplay::ProjectilePool::Capacity);
            ImGui::Text("%s: %u / %u", Tr("Pool vizuallari"), stats.Visuals, stats.Pooled);
            ImGui::Text("%s: %u", Tr("Kadrdagi qadamlar"), stats.Steps);
            ImGui::Text("%s: %zu / %zu", Tr("Sahnadagi effektlar"), combat->EffectCount(), Gameplay::CombatWorld::MaxEffects);
            if (!actor) return;
            std::array<Gameplay::EffectInstance, Gameplay::EffectBook::Capacity> effects;
            const size_t count = combat->Effects(actor, effects);
            if (count == 0)
            {
                ImGui::TextDisabled("%s", Tr("Tanlangan aktyorda effekt yo'q"));
                return;
            }
            ImGui::TextDisabled("%s", Tr("Effekt, to'plam, qolgan vaqt"));
            for (size_t i = 0; i < count; ++i) ImGui::Text("%s  x%u  %.2f s", effects[i].Key.c_str(), effects[i].Stacks, effects[i].Remaining);
        }
        // A curve value: a small graph preview (drag a key to move it; one undo step when released) and its keys, each
        // with time, value, interpolation and (cubic) tangents. Recorded under "<label>/Preview", "<label>/keys",
        // "<label>/Add key" and "<label>/<i>/Time|Value|Mode|In tangent|Out tangent|Delete key".
        bool EditCurve(const char* label, Visual::Value& v)
        {
            using namespace Visual;
            static std::map<ImGuiID, Value> drafts; // a key being dragged in a preview: the curve shown meanwhile
            bool changed = false;
            GraphLayout::FitText(label);
            GraphLayout::PushScope(label);
            const ImGuiID previewID = ImGui::GetID("##preview");
            const auto draft = drafts.find(previewID);
            auto keys = Visual::CurveKeys(draft != drafts.end() ? draft->second : v);
            // The preview: the key range (at least 1 wide) and the value range of the drawn curve.
            const float width = std::max(80.f, ImGui::GetContentRegionAvail().x), height = 96.f, pad = 8.f;
            const ImVec2 a = ImGui::GetCursorScreenPos(), b{ a.x + width, a.y + height };
            ImGui::InvisibleButton("##preview", { width, height });
            GraphLayout::RecordKey("Preview");
            auto* storage = ImGui::GetStateStorage();
            const ImGuiID rangeID = ImGui::GetID("##range");
            double t0 = keys.empty() ? 0 : keys.front().Time, t1 = keys.empty() ? 1 : keys.back().Time;
            if (t1 - t0 < 1e-6)
            {
                t0 -= .5;
                t1 += .5;
            }
            constexpr int samples = 96;
            double lo = INFINITY, hi = -INFINITY;
            for (int i = 0; i <= samples; ++i)
            {
                const double y = Visual::EvaluateCurve(keys, t0 + (t1 - t0) * i / samples);
                lo = std::min(lo, y);
                hi = std::max(hi, y);
            }
            if (hi - lo < 1e-6)
            {
                lo -= 1;
                hi += 1;
            }
            // While a key is dragged the axes stay as they were when the drag started.
            if (draft != drafts.end())
            {
                t0 = storage->GetFloat(rangeID + 1, float(t0));
                t1 = storage->GetFloat(rangeID + 2, float(t1));
                lo = storage->GetFloat(rangeID + 3, float(lo));
                hi = storage->GetFloat(rangeID + 4, float(hi));
            }
            auto toScreen = [&](double t, double y) { return ImVec2(a.x + pad + float((t - t0) / (t1 - t0)) * (width - 2 * pad), b.y - pad - float((y - lo) / (hi - lo)) * (height - 2 * pad)); };
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(a, b, IM_COL32(24, 27, 38, 255), 4);
            draw->AddRect(a, b, IM_COL32(70, 76, 98, 255), 4);
            if (lo < 0 && hi > 0) draw->AddLine(toScreen(t0, 0), toScreen(t1, 0), IM_COL32(60, 66, 86, 255));
            ImVec2 points[samples + 1];
            for (int i = 0; i <= samples; ++i)
            {
                const double t = t0 + (t1 - t0) * i / samples;
                points[i] = toScreen(t, Visual::EvaluateCurve(keys, t));
            }
            draw->AddPolyline(points, samples + 1, IM_COL32(150, 230, 90, 255), 0, 2);
            const auto mouse = ImGui::GetIO().MousePos;
            const ImGuiID dragID = ImGui::GetID("##dragKey");
            int dragged = storage->GetInt(dragID, -1);
            for (size_t i = 0; i < keys.size(); ++i)
            {
                const auto p = toScreen(keys[i].Time, keys[i].Value);
                draw->AddCircleFilled(p, int(i) == dragged ? 6.f : 4.5f, int(i) == dragged ? IM_COL32(255, 220, 120, 255) : IM_COL32(235, 238, 250, 255));
            }
            if (keys.empty()) draw->AddText({ a.x + pad, a.y + pad }, IM_COL32(150, 155, 176, 255), Tr("Egri chiziq bo'sh"));
            if (ImGui::IsItemActivated())
            {
                // Pick the key under the pointer (within 8 px).
                dragged = -1;
                float best = 64;
                for (size_t i = 0; i < keys.size(); ++i)
                {
                    const auto p = toScreen(keys[i].Time, keys[i].Value);
                    const float d = (p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y);
                    if (d < best)
                    {
                        best = d;
                        dragged = int(i);
                    }
                }
                storage->SetInt(dragID, dragged);
                if (dragged >= 0)
                {
                    drafts[previewID] = v;
                    storage->SetFloat(rangeID + 1, float(t0));
                    storage->SetFloat(rangeID + 2, float(t1));
                    storage->SetFloat(rangeID + 3, float(lo));
                    storage->SetFloat(rangeID + 4, float(hi));
                }
            }
            if (ImGui::IsItemActive() && dragged >= 0 && dragged < int(keys.size()) && drafts.contains(previewID))
            {
                // A key moves between its neighbours' times (never onto them).
                auto& key = keys[size_t(dragged)];
                const double t = t0 + (mouse.x - a.x - pad) / (width - 2 * pad) * (t1 - t0);
                const double y = lo + (b.y - pad - mouse.y) / (height - 2 * pad) * (hi - lo);
                const double span = (t1 - t0) * 1e-3;
                const double before = dragged > 0 ? keys[size_t(dragged) - 1].Time + span : -1e9, after = size_t(dragged) + 1 < keys.size() ? keys[size_t(dragged) + 1].Time - span : 1e9;
                key.Time = std::clamp(t, before, std::max(before, after));
                key.Value = y;
                drafts[previewID] = Visual::MakeCurve(keys);
                ImGui::SetTooltip("%.3f, %.3f", key.Time, key.Value);
            }
            if (ImGui::IsItemDeactivated())
            {
                if (auto done = drafts.find(previewID); done != drafts.end())
                {
                    if (done->second != v && Visual::IsCurve(done->second))
                    {
                        v = done->second;
                        changed = true;
                    }
                    drafts.erase(done);
                }
                storage->SetInt(dragID, -1);
                keys = Visual::CurveKeys(v);
            }
            {
                // The key list (edits apply at once; a time is committed with Enter and must stay unique). While a key is
                // dragged it shows the dragged curve, read-only, so the column keeps its layout.
                const bool dragging = drafts.find(previewID) != drafts.end();
                if (dragging) ImGui::BeginDisabled();
                bool edited = false;
                size_t remove = SIZE_MAX;
                if (GraphLayout::TreeNode("##keys", std::string(Tr("Kalitlar")) + " (" + std::to_string(keys.size()) + ")", "keys"))
                {
                    GraphLayout::PushScope("keys");
                    for (size_t i = 0; i < keys.size(); ++i)
                    {
                        auto& key = keys[i];
                        ImGui::PushID(int(i));
                        GraphLayout::PushScope(std::to_string(i));
                        if (i) ImGui::Separator();
                        // A time is committed with Enter (a half-typed time must not jump past other keys).
                        char text[64]{};
                        std::snprintf(text, sizeof(text), "%.6g", key.Time);
                        GraphLayout::InlineLabel(Tr("Vaqt"));
                        char* end = nullptr;
                        double time = key.Time;
                        if (ImGui::InputText("##time", text, sizeof(text), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific)) time = std::strtod(text, &end);
                        if (end && end != text && std::isfinite(time) && time != key.Time)
                        {
                            if (std::any_of(keys.begin(), keys.end(), [&](const Visual::CurveKey& other) { return other.Time == time; })) ImGui::SetTooltip("%s", Tr("Bu vaqtda kalit bor"));
                            else
                            {
                                key.Time = time;
                                edited = true;
                            }
                        }
                        GraphLayout::RecordKey("Time");
                        GraphLayout::InlineLabel(Tr("Qiymat"));
                        if (ImGui::InputDouble("##value", &key.Value, 0, 0, "%.3f") && std::isfinite(key.Value)) edited = true;
                        GraphLayout::RecordKey("Value");
                        GraphLayout::InlineLabel(Tr("Interpolyatsiya"));
                        static const char* modes[] = { "Konstant", "Chiziqli", "Kubik" };
                        if (ImGui::BeginCombo("##mode", Tr(modes[int(key.Mode)])))
                        {
                            for (int m = 0; m < 3; ++m)
                            {
                                const bool picked = ImGui::Selectable(Tr(modes[m]), int(key.Mode) == m);
                                GraphLayout::RecordRect(std::string("popup/") + Visual::CurveModeName(Visual::CurveMode(m)), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                                if (picked && int(key.Mode) != m)
                                {
                                    key.Mode = Visual::CurveMode(m);
                                    edited = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                        GraphLayout::RecordKey("Mode");
                        // Tangents shape the cubic segments around the key: In the one that ends here, Out the next.
                        if (i > 0 && keys[i - 1].Mode == Visual::CurveMode::Cubic)
                        {
                            GraphLayout::InlineLabel(Tr("Kirish tangensi"));
                            if (ImGui::InputDouble("##in", &key.In, 0, 0, "%.3f") && std::isfinite(key.In)) edited = true;
                            GraphLayout::RecordKey("In tangent");
                        }
                        if (key.Mode == Visual::CurveMode::Cubic && i + 1 < keys.size())
                        {
                            GraphLayout::InlineLabel(Tr("Chiqish tangensi"));
                            if (ImGui::InputDouble("##out", &key.Out, 0, 0, "%.3f") && std::isfinite(key.Out)) edited = true;
                            GraphLayout::RecordKey("Out tangent");
                        }
                        if (GraphLayout::SmallButton(Tr("Kalitni o'chirish###Delete key"))) remove = i;
                        GraphLayout::PopScope();
                        ImGui::PopID();
                    }
                    GraphLayout::PopScope();
                    ImGui::TreePop();
                }
                if (remove != SIZE_MAX)
                {
                    keys.erase(keys.begin() + std::ptrdiff_t(remove));
                    edited = true;
                }
                if (keys.size() < MaxCurveKeys && GraphLayout::Button(Tr("Kalit qo'shish###Add key")))
                {
                    // A new key one time unit after the last, at its value (the first key: 0, 0).
                    Visual::CurveKey key;
                    if (!keys.empty())
                    {
                        key.Time = keys.back().Time + 1;
                        key.Value = keys.back().Value;
                    }
                    keys.push_back(key);
                    edited = true;
                }
                if (dragging) ImGui::EndDisabled();
                if (edited && !dragging && !changed)
                {
                    auto next = Visual::MakeCurve(std::move(keys));
                    if (Visual::IsCurve(next) && next != v)
                    {
                        v = std::move(next);
                        changed = true;
                    }
                }
            }
            GraphLayout::PopScope();
            return changed;
        }
        // A literal editor. graph/named resolve a named type: an Enum value shows its entries as a combo, a Struct
        // value one editor per field, an ArrayStruct value struct elements.
        bool EditValue(const char* label, Visual::Type type, Visual::Value& v, const Visual::Graph* graph = nullptr, const std::string& named = {})
        {
            using namespace Visual;
            ImGui::PushID(label);
            bool changed = false;
            if (type == Type::Curve && IsCurve(v))
            {
                changed = EditCurve(label, v);
                ImGui::PopID();
                return changed;
            }
            GraphLayout::InlineLabel(label);
            const auto* enumeration = (type == Type::Enum || type == Type::ArrayEnum) && graph && graph->Enums.contains(named) ? &graph->Enums.at(named) : nullptr;
            const auto* structure = (type == Type::Struct || type == Type::ArrayStruct) && graph && graph->Structs.contains(named) ? &graph->Structs.at(named) : nullptr;
            if (!Matches(type, v) || ((structure || enumeration) && !graph->Fits(type, named, v)))
            {
                ImGui::TextDisabled("%s (%s)", Tr("Yaroqsiz qiymat"), TypeName(type));
                if (GraphLayout::Button(Tr("Qiymatni tiklash###Reset literal")))
                {
                    v = graph ? graph->DefaultFor(type, named) : DefaultValue(type);
                    changed = true;
                }
                ImGui::PopID();
                return changed;
            }
            if (type == Type::Struct)
            {
                // One editor per field, under a tree that names the struct.
                if (!structure) ImGui::TextDisabled("%s: %s", Tr("Noma'lum struktura"), named.c_str());
                else if (GraphLayout::TreeNode("##struct", named + " {" + std::to_string(structure->Fields.size()) + "}", std::string(label) + "/fields"))
                {
                    // A nested struct field opens its own tree; its fields are recorded under "<label>/<field>".
                    GraphLayout::PushScope(label);
                    for (const auto& field : structure->Fields)
                    {
                        auto member = v.at(field.Name);
                        if (EditValue(field.Name.c_str(), field.Kind, member, graph, field.Enum))
                        {
                            v[field.Name] = std::move(member);
                            changed = true;
                        }
                    }
                    GraphLayout::PopScope();
                    ImGui::TreePop();
                }
                ImGui::PopID();
                return changed;
            }
            if (IsArray(type))
            {
                if (ImGui::TreeNode("##array", "%zu items (%s)", v.size(), TypeName(ElementType(type))))
                {
                    auto* state = ImGui::GetStateStorage();
                    const auto pageID = ImGui::GetID("arrayPage");
                    int page = std::clamp(state->GetInt(pageID, 0), 0, int(v.size() / 16));
                    if (ImGui::SmallButton(Tr("Oldingi###Previous")) && page > 0) --page;
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Tr("Keyingi###Next")) && size_t((page + 1) * 16) < v.size()) ++page;
                    state->SetInt(pageID, page);
                    size_t remove = SIZE_MAX;
                    for (size_t i = size_t(page) * 16; i < std::min(v.size(), size_t(page + 1) * 16); ++i)
                    {
                        ImGui::PushID(int(i));
                        changed |= EditValue(("[" + std::to_string(i) + "]").c_str(), ElementType(type), v[i], graph, named);
                        if (ImGui::SmallButton(Tr("Elementni o'chirish###Remove item"))) remove = i;
                        ImGui::PopID();
                    }
                    if (remove != SIZE_MAX)
                    {
                        v.erase(v.begin() + remove);
                        changed = true;
                    }
                    if (v.size() < 1024 && ImGui::Button(Tr("Element qo'shish###Add item")))
                    {
                        v.push_back(graph ? graph->DefaultFor(ElementType(type), named) : DefaultValue(ElementType(type)));
                        changed = true;
                    }
                    if (ImGui::Button(Tr("Massiv JSON'ini qo'yish###Paste array JSON")))
                    {
                        const char* text = ImGui::GetClipboardText();
                        if (text && std::strlen(text) <= 65536)
                        {
                            try
                            {
                                auto parsed = Value::parse(text, [](int depth, Value::parse_event_t, Value&) {
                                    if (depth > 8) throw std::runtime_error("Array nesting limit");
                                    return true;
                                }, false);
                                if (!parsed.is_discarded() && Matches(type, parsed) && (!graph || graph->Fits(type, named, parsed)))
                                {
                                    v = std::move(parsed);
                                    changed = true;
                                }
                            }
                            catch (const std::exception&)
                            {
                                ImGui::TextDisabled("%s", Tr("Massiv JSON'i yaroqsiz"));
                            }
                        }
                    }
                    ImGui::TreePop();
                }
            }
            else if (type == Type::Enum && enumeration)
            {
                // An enum value: its entries by name; the stored value is the entry's integer.
                const auto* current = enumeration->Find(v.get<int64_t>());
                if (ImGui::BeginCombo("##value", current ? current->Name.c_str() : Tr("Noma'lum qiymat")))
                {
                    for (const auto& entry : enumeration->Entries)
                        if (ImGui::Selectable(entry.Name.c_str(), &entry == current) && &entry != current)
                        {
                            v = entry.Value;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
            }
            else if (type == Type::Enum)
            {
                int64_t x = v.get<int64_t>();
                if (ImGui::InputScalar("##value", ImGuiDataType_S64, &x))
                {
                    v = x;
                    changed = true;
                }
            }
            else if (type == Type::Number)
            {
                double x = v.get<double>();
                if (ImGui::InputDouble("##value", &x, 0, 0, "%.4f") && std::isfinite(x))
                {
                    v = x;
                    changed = true;
                }
            }
            else if (type == Type::Bool)
            {
                bool x = v.get<bool>();
                if (ImGui::Checkbox("##value", &x))
                {
                    v = x;
                    changed = true;
                }
            }
            else if (type == Type::String)
            {
                std::vector<char> buf(std::max<size_t>(1024, v.get_ref<const std::string&>().size() + 256), 0);
                std::memcpy(buf.data(), v.get_ref<const std::string&>().data(), v.get_ref<const std::string&>().size());
                if (ImGui::InputText("##value", buf.data(), buf.size()))
                {
                    v = buf.data();
                    changed = true;
                }
            }
            else if (type == Type::Entity)
            {
                uint64_t id = v.get<uint64_t>();
                if (id == UINT64_MAX) ImGui::TextDisabled("%s", Tr("O'zi"));
                else ImGui::TextDisabled("%llu", (unsigned long long)id);
                if (GraphLayout::Button(Tr("O'zi###Self")))
                {
                    v = UINT64_MAX;
                    changed = true;
                }
                GraphLayout::SameLineIfFits(GraphLayout::ButtonWidth(Tr("Hech biri###None")));
                if (GraphLayout::Button(Tr("Hech biri###None")))
                {
                    v = 0ull;
                    changed = true;
                }
                GraphLayout::SameLineIfFits(GraphLayout::ButtonWidth(Tr("Obyektni shu yerga tashlang###Drop entity")));
                GraphLayout::Button(Tr("Obyektni shu yerga tashlang###Drop entity"));
                if (ImGui::BeginDragDropTarget())
                {
                    if (auto* p = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
                    {
                        if (p->DataSize == sizeof(uint64_t))
                        {
                            v = *(const uint64_t*)p->Data;
                            changed = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            else
            {
                float x[4]{};
                for (size_t i = 0; i < v.size(); ++i) x[i] = v[i].get<float>();
                int n = type == Type::Vec2 ? 2 : type == Type::Vec3 ? 3
                                                                    : 4;
                if (ImGui::InputScalarN("##value", ImGuiDataType_Float, x, n))
                {
                    changed = true;
                    for (int i = 0; i < n; ++i)
                    {
                        if (!std::isfinite(x[i])) x[i] = 0;
                        v[i] = x[i];
                    }
                }
            }
            if (!IsArray(type)) GraphLayout::RecordKey(label);
            ImGui::PopID();
            return changed;
        }
    }
    bool VisualGraphPanel::EditLiteral(const char* label, Visual::Type type, Visual::Value& value, const std::string& named) { return EditValue(label, type, value, &m_Graph, named); }
    void VisualGraphPanel::SetContext(const Ref<Scene>& scene, bool editable, uint64_t selection)
    {
        m_Scene = scene;
        m_Editable = editable;
        m_Entity = selection;
    }
    bool VisualGraphPanel::SelectNode(uint32_t id)
    {
        if (!m_Graph.Find(id)) return false;
        m_Selected = id;
        m_Selection = { id };
        return true;
    }
    void VisualGraphPanel::Open(const std::filesystem::path& path)
    {
        try
        {
            if (m_Dirty)
            {
                m_Error = "Save the current graph before opening another asset.";
                return;
            }
            m_Graph = Visual::Graph::Load(path, Project::GetAssetDirectory());
            m_Path = path;
            std::snprintf(m_ParentPath, sizeof(m_ParentPath), "%s", m_Graph.Parent.c_str());
            std::snprintf(m_File, sizeof(m_File), "%s", Project::GetAssetRelativePath(path).c_str());
            m_Undo.clear();
            m_Redo.clear();
            m_Selected = 0;
            m_From = 0;
            m_Error.clear();
            m_Dirty = false;
            m_ValidateDirty = true;
            m_Descriptions.clear();
            m_Selection.clear();
            m_FocusFunction.clear();
            m_FocusNodes.clear();
            m_Pan = { 40, 40 };
            m_Zoom = 1;
            m_Fit = true;
            m_Focus = true;
            if (std::getenv("ZAMIN_EDITOR_WORLD_AGENT_QA"))
            {
                if (!m_Graph.Validate().empty()) throw std::runtime_error("Agent graph validation failed");
                ZM_CORE_INFO("Agent graph editor QA: {} nodes, {} links, {}", m_Graph.Nodes.size(), m_Graph.Links.size(), path.string());
            }
        }
        catch (const std::exception& e)
        {
            m_Error = e.what();
        }
    }
    void VisualGraphPanel::LogQA(const std::string& name) const
    {
        if (!m_Error.empty())
        {
            ZM_ERROR("Blueprint graph QA: could not open {}: {}", name, m_Error);
            return;
        }
        const auto diagnostics = m_Graph.Validate();
        ZM_CORE_INFO("Blueprint graph QA: {} - {} nodes, {} links, {} variables, {} diagnostics", name, m_Graph.Nodes.size(), m_Graph.Links.size(),
                     m_Graph.Variables.size(), diagnostics.size());
        for (size_t i = 0; i < diagnostics.size() && i < 8; i++)
            ZM_WARN("Blueprint graph QA: {} node {}: {}", name, diagnostics[i].Node, diagnostics[i].Message);
    }
    void VisualGraphPanel::Snapshot()
    {
        m_Undo.push_back(m_Graph.Serialize());
        size_t bytes = 0;
        for (auto& state : m_Undo) bytes += state.dump().size();
        while (m_Undo.size() > 1 && (m_Undo.size() > 64 || bytes > 32 * 1024 * 1024))
        {
            bytes -= m_Undo.front().dump().size();
            m_Undo.erase(m_Undo.begin());
        }
        m_Redo.clear();
        m_ValidateDirty = true;
        m_Dirty = true;
    }
    void VisualGraphPanel::OnImGuiRender(bool* show)
    {
        if (!*show) return;
        if (m_Focus)
        {
            ImGui::SetNextWindowFocus();
            m_Focus = false;
        }
        ImGui::SetNextWindowSize({ 1450, 800 }, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Visual Script###VisualGraph", show))
        {
            ImGui::End();
            return;
        }
        if (ImGui::Button(Tr("Grafni sig'dirish###Fit graph"))) m_Fit = true;
        ImGui::SameLine();
        ImGui::TextColored({ .66f, .54f, 1, 1 }, "ZAMIN  /  VISUAL SCRIPT");
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", m_Path.filename().string().c_str(), m_Dirty ? " *" : "");
        if (m_Graph.SourceVersion < Visual::Graph::FormatVersion)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(Tr("(format %d: saqlash %d formatida yozadi)"), m_Graph.SourceVersion, Visual::Graph::FormatVersion);
        }
        if (!m_Graph.Parent.empty())
        {
            // A child graph: its parent chain, the nearest first.
            ImGui::SameLine();
            std::string chain = m_Graph.Parent;
            for (size_t i = 1; i < m_Graph.Inherited.Chain.size(); ++i) chain += " <- " + m_Graph.Inherited.Chain[i];
            ImGui::TextColored({ .55f, .85f, .55f, 1 }, "%s: %s", Tr("Ota graf"), chain.c_str());
        }
        if (!m_Macro.empty() && !m_Graph.Macros.contains(m_Macro)) OpenMacro(""); // e.g. undone
        if (!m_Macro.empty())
        {
            // An opened macro: the canvas shows its body only.
            ImGui::SameLine();
            ImGui::TextColored({ 1, .78f, .45f, 1 }, "%s: %s", Tr("Makro"), m_Macro.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Butun graf###Whole graph"))) OpenMacro("");
            GraphLayout::RecordRect("header/Whole graph", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
        if (!m_FocusFunction.empty())
        {
            // An opened typed function: its body is framed, the rest of the graph dimmed.
            ImGui::SameLine();
            ImGui::TextColored({ .55f, .85f, 1, 1 }, "%s: %s", Tr("Funksiya"), m_FocusFunction.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Butun graf###Whole graph")))
            {
                m_FocusFunction.clear();
                m_FocusNodes.clear();
                m_Fit = true;
            }
        }
        ImGui::BeginDisabled(!m_Editable);
        ImGui::SetNextItemWidth(260);
        ImGui::InputText("##graphPath", m_File, sizeof(m_File));
        ImGui::SameLine();
        if (ImGui::Button(Tr("Ochish###Open")))
        {
            auto p = FileSystem::SafeJoin(Project::GetAssetDirectory(), m_File);
            if (!p.empty()) Open(p);
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Yangi###New")) && !m_Dirty)
        {
            m_Graph = Visual::Graph::Example();
            m_ValidateDirty = true;
            m_Descriptions.clear();
            m_Selection.clear();
            m_Path = FileSystem::SafeJoin(Project::GetAssetDirectory(), m_File);
            m_Dirty = true;
            m_Undo.clear();
            m_Redo.clear();
            m_Error.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Saqlash###Save")))
        {
            if (m_Path.empty() || !FileSystem::IsSubPathOnDisk(m_Path, Project::GetAssetDirectory())) m_Error = "Choose a path inside Assets.";
            else if (m_Graph.Save(m_Path))
            {
                m_Graph.SourceVersion = Visual::Graph::FormatVersion; // the file now holds the current format
                m_Dirty = false;
                m_Error.clear();
                if (auto* r = Project::GetAssetRegistry()) r->OnFileAdded(m_Path);
            }
            else m_Error = "Could not save graph; your edits are still here.";
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_Undo.empty());
        if (ImGui::Button(Tr("Bekor qilish (Undo)###Undo")))
        {
            m_Redo.push_back(m_Graph.Serialize());
            m_Graph = Visual::Graph::Parse(m_Undo.back());
            Reinherit(); // a snapshot holds the graph's own members; the parent chain comes from disk again
            m_ValidateDirty = true;
            m_Descriptions.clear();
            m_Selection.clear();
            m_Undo.pop_back();
            m_Dirty = true;
            m_From = 0;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_Redo.empty());
        if (ImGui::Button(Tr("Qaytarish (Redo)###Redo")))
        {
            m_Undo.push_back(m_Graph.Serialize());
            m_Graph = Visual::Graph::Parse(m_Redo.back());
            Reinherit();
            m_ValidateDirty = true;
            m_Descriptions.clear();
            m_Selection.clear();
            m_Redo.pop_back();
            m_Dirty = true;
            m_From = 0;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(Tr("O\'zgarishlarni tashlash###Discard edits")))
        {
            m_Dirty = false;
            if (std::filesystem::exists(m_Path)) Open(m_Path);
            else
            {
                m_Graph = {};
                m_Path.clear();
                m_Undo.clear();
                m_Redo.clear();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Nusxalash###Copy"))) CopySelection();
        ImGui::SameLine();
        if (ImGui::Button(Tr("Qo\'yish###Paste"))) PasteSelection();
        ImGui::SameLine();
        if (ImGui::Button(Tr("Ko\'paytirish###Duplicate")))
        {
            CopySelection();
            PasteSelection();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Funksiyaga yig'ish###Collapse to function"))) CollapseSelection();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr("Tanlangan tugunlar yangi tiplangan funksiyaga ko'chadi, o'rniga chaqiruv qo'yiladi (Ctrl+G)"));
        ImGui::EndDisabled();
        if (!m_Error.empty()) ImGui::TextColored({ 1, .4f, .4f, 1 }, "%s", m_Error.c_str());
        if (m_ValidateDirty)
        {
            m_Diagnostics = m_Graph.Validate();
            m_Warnings = m_Graph.Warnings();
            m_ValidateDirty = false;
            m_Descriptions.clear();
            if (!m_FocusFunction.empty())
            {
                m_FocusNodes = m_Graph.FunctionNodes(m_FocusFunction);
                if (m_FocusNodes.empty()) m_FocusFunction.clear();
            }
        }
        const auto& diagnostics = m_Diagnostics;
        if (diagnostics.empty()) ImGui::TextColored({ .35f, .85f, .6f, 1 }, "%s  |  %zu %s  |  %zu %s  |  %zu %s", Tr("Kompilyatsiya bo'ldi"), m_Graph.Nodes.size(), Tr("tugun"), m_Graph.Links.size(), Tr("bog'lanish"), m_Graph.Variables.size(), Tr("o'zgaruvchi"));
        if (!m_Warnings.empty())
        {
            // Warnings stay on the status line (hover: the list, click: the first node) so the panels do not move.
            if (diagnostics.empty()) ImGui::SameLine();
            ImGui::TextColored({ 1, .85f, .35f, 1 }, "|  %zu %s", m_Warnings.size(), Tr("ogohlantirish"));
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                for (const auto& w : m_Warnings) ImGui::Text("%s %u: %s", Tr("Tugun"), w.Node, w.Message.c_str());
                ImGui::EndTooltip();
            }
            if (ImGui::IsItemClicked()) m_Selected = m_Warnings.front().Node;
        }
        if (!diagnostics.empty())
        {
            ImGui::TextColored({ 1, .65f, .3f, 1 }, "%zu %s", diagnostics.size(), Tr("kompilyatsiya xatosi"));
            if (ImGui::TreeNode("Diagnostics"))
            {
                for (auto& d : diagnostics)
                {
                    // A finding in a macro names the instance; the macro's body node is in parentheses.
                    const auto text = std::string(Tr("Tugun")) + " " + std::to_string(d.Node) + (d.Source ? " (" + std::to_string(d.Source) + ")" : "") + ": " + d.Message;
                    if (ImGui::Selectable(text.c_str()))
                        if (const auto* node = m_Graph.Find(d.Node))
                        {
                            if (node->Macro != m_Macro) OpenMacro(node->Macro);
                            m_Selected = d.Node;
                        }
                }
                ImGui::TreePop();
            }
        }
        ImGui::Separator();
        float height = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("palette", { 205, height }, ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(Tr("Tugunlar kutubxonasi"));
        ImGui::InputTextWithHint("##search", Tr("Tugun qidirish..."), m_Search, sizeof(m_Search));
        ImGui::TextDisabled("%s", Tr("Bosib tugun qo'shing"));
        ImGui::BeginDisabled(!m_Editable);
        std::string q = m_Search;
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);
        auto matches = [&](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), ::tolower);
            return q.empty() || text.find(q) != std::string::npos;
        };
        auto addNode = [&](const std::string& kind, std::map<std::string, Visual::Value> inputs) {
            uint64_t nextID = 1;
            for (auto& n : m_Graph.Nodes) nextID = std::max(nextID, uint64_t(n.ID) + 1);
            if (nextID > UINT32_MAX || m_Graph.Nodes.size() >= 4096)
            {
                m_Error = Tr("Tugunlar soni chegarasiga yetildi");
                return;
            }
            Snapshot();
            const auto id = uint32_t(nextID);
            m_Graph.Nodes.push_back({ id, kind, (100 - m_Pan.x) / m_Zoom, (100 - m_Pan.y) / m_Zoom, std::move(inputs), false, m_Macro });
            m_Selected = id;
            m_Selection = { id };
        };
        // The graph's own typed functions first: one call entry each, found by "call" or the function's name.
        for (const auto& [name, signature] : m_Graph.Functions)
            if (matches("function.call " + name) && ImGui::Selectable((std::string(Tr("Funksiyalar")) + " / " + Tr("Chaqirish") + " " + name).c_str()))
                addNode("Function.Call", { { "Name", name } });
        // Then Make/Break of each struct, found by "make", "break" or the struct's name.
        for (const auto& [name, definition] : m_Graph.Structs)
            for (const char* kind : { "Make", "Break" })
                if (matches(std::string("struct.") + kind + " " + name) && ImGui::Selectable((std::string(Tr("Strukturalar")) + " / " + kind + " " + name).c_str()))
                    addNode(std::string("Struct.") + kind, { { "Struct", name } });
        // Then the graph's macros (not the open macro itself: a macro cannot use itself).
        for (const auto& [name, macro] : m_Graph.Macros)
            if (name != m_Macro && matches("macro.instance " + name) && ImGui::Selectable((std::string(Tr("Makroslar")) + " / " + name).c_str()))
                addNode("Macro.Instance", { { "Name", name } });
        for (auto& [key, def] : Visual::Catalog())
        {
            if (!matches(key)) continue;
            // A macro's entry/exit come with the macro; a macro body holds no events or function entries.
            if (key == "Macro.Entry" || key == "Macro.Exit") continue;
            if (!m_Macro.empty() && (key.starts_with("Event.") || key == "Function.Entry" || key == "Function")) continue;
            if (ImGui::Selectable((def.Category + " / " + key).c_str()))
            {
                std::map<std::string, Visual::Value> inputs;
                if (key.starts_with("Function.")) inputs["Name"] = m_Graph.Functions.empty() ? "MyFunction" : m_Graph.Functions.begin()->first;
                if (key == "Macro.Instance" && !m_Graph.Macros.empty()) inputs["Name"] = m_Graph.Macros.begin()->first;
                // Call parent nodes start with the graph's first override (function / event).
                if (key == "Parent.Call" && !m_Graph.FunctionOverrides().empty()) inputs["Name"] = *m_Graph.FunctionOverrides().begin();
                if (key == "Parent.Event" && !m_Graph.EventOverrides().empty()) inputs["Name"] = *m_Graph.EventOverrides().begin();
                // An enum node starts with the graph's first enum (variable/local Get/Set take their own instead).
                const bool enumInput = std::any_of(def.Inputs.begin(), def.Inputs.end(), [](const Visual::Pin& p) { return p.Name == "Enum"; });
                if (enumInput && !m_Graph.Enums.empty() && !key.starts_with("Get.") && !key.starts_with("Set.") && !key.starts_with("Local.")) inputs["Enum"] = m_Graph.Enums.begin()->first;
                // A struct node starts with the graph's first struct (variable/local Get/Set take their own) and field.
                const bool structInput = std::any_of(def.Inputs.begin(), def.Inputs.end(), [](const Visual::Pin& p) { return p.Name == "Struct"; });
                if (structInput && !m_Graph.Structs.empty() && !key.starts_with("Get.") && !key.starts_with("Set.") && !key.starts_with("Local."))
                {
                    inputs["Struct"] = m_Graph.Structs.begin()->first;
                    if (key == "Struct.GetField" || key == "Struct.SetField") inputs["Field"] = m_Graph.Structs.begin()->second.Fields.front().Name;
                }
                addNode(key, std::move(inputs));
            }
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::SameLine();
        float width = std::max(150.f, ImGui::GetContentRegionAvail().x - 325);
        ImGui::BeginChild("canvas", { width, height }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        Canvas();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("properties", { 0, height }, ImGuiChildFlags_Borders);
        Properties();
        ImGui::EndChild();
        ImGui::End();
        GraphLayout::Flush();
    }
    void VisualGraphPanel::Canvas()
    {
        using namespace Visual;
        auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
        if (m_Fit && !m_Graph.Nodes.empty())
        {
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            for (auto& n : m_Graph.Nodes)
            {
                if (!Visible(n) || (!m_FocusNodes.empty() && !m_FocusNodes.contains(n.ID))) continue;
                auto it = Visual::Catalog().find(n.Kind);
                const auto& definition = Describe(n);
                size_t rows = it == Visual::Catalog().end() ? 1 : std::max(definition.Inputs.size(), definition.Outputs.size());
                minX = std::min(minX, n.X);
                minY = std::min(minY, n.Y);
                maxX = std::max(maxX, n.X + 220);
                maxY = std::max(maxY, n.Y + 65 + 24 * (float)rows);
            }
            m_Zoom = std::clamp(std::min((size.x - 50) / std::max(1.f, maxX - minX), (size.y - 60) / std::max(1.f, maxY - minY)), .35f, 1.f);
            m_Pan = { 25 - minX * m_Zoom, 25 - minY * m_Zoom };
            m_Fit = false;
        }
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, { origin.x + size.x, origin.y + size.y }, IM_COL32(20, 23, 33, 255));
        ImGui::InvisibleButton("graphCanvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        bool hover = ImGui::IsItemHovered();
        auto mouse = ImGui::GetIO().MousePos;
        auto screen = [&](float x, float y) { return ImVec2(origin.x + m_Pan.x + x * m_Zoom, origin.y + m_Pan.y + y * m_Zoom); };
        if (!ImGui::IsMouseDown(0)) m_Dragging = false;
        if (hover && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            m_Pan.x += ImGui::GetIO().MouseDelta.x;
            m_Pan.y += ImGui::GetIO().MouseDelta.y;
        }
        if (hover && ImGui::GetIO().MouseWheel)
        {
            float old = m_Zoom;
            m_Zoom = std::clamp(m_Zoom * std::pow(1.12f, ImGui::GetIO().MouseWheel), .35f, 1.5f);
            m_Pan.x = mouse.x - origin.x - (mouse.x - origin.x - m_Pan.x) * m_Zoom / old;
            m_Pan.y = mouse.y - origin.y - (mouse.y - origin.y - m_Pan.y) * m_Zoom / old;
        }
        float grid = 32 * m_Zoom;
        for (float x = std::fmod(m_Pan.x, grid); x < size.x; x += grid) draw->AddLine({ origin.x + x, origin.y }, { origin.x + x, origin.y + size.y }, IM_COL32(33, 37, 49, 255));
        for (float y = std::fmod(m_Pan.y, grid); y < size.y; y += grid) draw->AddLine({ origin.x, origin.y + y }, { origin.x + size.x, origin.y + y }, IM_COL32(33, 37, 49, 255));
        auto pin = [&](const Node& n, const std::vector<Pin>& ps, const std::string& name, bool out) {size_t i=0;for(;i<ps.size();++i)if(ps[i].Name==name)break;return screen(n.X+(out?220:0),n.Y+48+(float)i*24); };
        std::map<uint32_t, const Node*> nodeMap;
        for (const auto& node : m_Graph.Nodes) nodeMap[node.ID] = &node;
        for (auto& l : m_Graph.Links)
        {
            auto* a = nodeMap.contains(l.From) ? nodeMap.at(l.From) : nullptr;
            auto* b = nodeMap.contains(l.To) ? nodeMap.at(l.To) : nullptr;
            if (!a || !b || !Visible(*a) || !Visible(*b) || !Catalog().contains(a->Kind) || !Catalog().contains(b->Kind)) continue;
            const auto& def = Describe(*a);
            auto from = pin(*a, def.Outputs, l.Output, true), to = pin(*b, Describe(*b).Inputs, l.Input, false);
            float bend = std::max(50.f, std::abs(to.x - from.x) * .45f);
            auto p = std::find_if(def.Outputs.begin(), def.Outputs.end(), [&](auto& p) { return p.Name == l.Output; });
            draw->AddBezierCubic(from, { from.x + bend, from.y }, { to.x - bend, to.y }, to, p == def.Outputs.end() ? IM_COL32(255, 80, 80, 255) : Color(p->Kind), 2.5f * m_Zoom);
        }
        if (m_From)
        {
            auto* n = m_Graph.Find(m_From);
            if (n && Catalog().contains(n->Kind))
            {
                auto p = pin(*n, Describe(*n).Outputs, m_Output, true);
                draw->AddBezierCubic(p, { p.x + 70, p.y }, { mouse.x - 70, mouse.y }, mouse, IM_COL32(210, 185, 255, 255), 2);
            }
        }
        uint32_t runningNode = 0;
        if (!m_Editable && m_Scene)
        {
            auto e = m_Scene->GetEntityByUUID(m_Entity);
            auto* c = e ? e.TryGetComponent<NativeScriptComponent>() : nullptr;
            if (c && c->Instance && c->ClassName == "Zamin.VisualGraph")
                if (auto* h = dynamic_cast<VisualGraphHost*>(c->Instance))
                {
                    // A node copied from a macro shows on its instance (graph view) or on its body node (macro view).
                    uint32_t source = 0;
                    runningNode = h->VM.Origin(h->VM.CurrentNode, &source);
                    if (!m_Macro.empty() && source) runningNode = source;
                }
        }
        bool clickedPin = false;
        std::string openFunction, openMacro;
        for (auto& n : m_Graph.Nodes)
        {
            auto it = Catalog().find(n.Kind);
            if (it == Catalog().end() || !Visible(n)) continue;
            const auto& d = Describe(n);
            auto a = screen(n.X, n.Y), b = screen(n.X + 220, n.Y + 65 + 24 * (float)std::max(d.Inputs.size(), d.Outputs.size()));
            draw->AddRectFilled({ a.x + 4, a.y + 5 }, { b.x + 4, b.y + 5 }, IM_COL32(0, 0, 0, 70), 8);
            draw->AddRectFilled(a, b, IM_COL32(40, 44, 61, 255), 7);
            draw->AddRectFilled(a, { b.x, a.y + 30 * m_Zoom }, n.Kind.starts_with("Event.") ? IM_COL32(125, 47, 76, 255) : d.Pure ? IM_COL32(42, 98, 83, 255)
                                                                                                                                  : IM_COL32(83, 57, 137, 255),
                                7);
            // A macro instance shows the macro's name in its header.
            const std::string header = n.Kind == "Macro.Instance" && n.Inputs.contains("Name") && n.Inputs.at("Name").is_string() ? n.Kind + ": " + n.Inputs.at("Name").get<std::string>() : n.Kind;
            draw->AddText(nullptr, ImGui::GetFontSize() * m_Zoom, { a.x + 10 * m_Zoom, a.y + 6 * m_Zoom }, IM_COL32(245, 246, 255, 255), header.c_str());
            if (m_Selection.contains(n.ID) || n.ID == m_Selected) draw->AddRect(a, b, IM_COL32(191, 152, 255, 255), 7, 0, 2);
            if (n.ID == runningNode) draw->AddRect(a, b, IM_COL32(255, 205, 80, 255), 7, 0, 3);
            if (n.Breakpoint) draw->AddCircleFilled({ b.x - 10, a.y + 12 }, 5, IM_COL32(255, 70, 80, 255));
            if (GraphLayout::Recording()) GraphLayout::RecordRect("canvas/node/" + std::to_string(n.ID), a, { b.x, a.y + 30 * m_Zoom });
            for (int side = 0; side < 2; ++side)
            {
                auto& ps = side ? d.Outputs : d.Inputs;
                for (auto& p : ps)
                {
                    auto pos = pin(n, ps, p.Name, side);
                    draw->AddCircleFilled(pos, 5 * m_Zoom, Color(p.Kind));
                    if (GraphLayout::Recording()) GraphLayout::RecordRect("canvas/" + std::to_string(n.ID) + (side ? "/out/" : "/in/") + p.Name, { pos.x - 4, pos.y - 4 }, { pos.x + 4, pos.y + 4 });
                    float textWidth = ImGui::CalcTextSize(p.Name.c_str()).x * m_Zoom;
                    draw->AddText(nullptr, ImGui::GetFontSize() * m_Zoom, { pos.x + (side ? -textWidth - 12 * m_Zoom : 12 * m_Zoom), pos.y - 7 * m_Zoom }, Color(p.Kind), p.Name.c_str());
                    float dx = mouse.x - pos.x, dy = mouse.y - pos.y;
                    if (hover && dx * dx + dy * dy < 100 && m_Editable)
                    {
                        ImGui::SetTooltip("%s (%s) | %s", p.Name.c_str(), TypeName(p.Kind), Tr("O'ng tugma: uzish"));
                        if (ImGui::IsMouseClicked(1))
                        {
                            Snapshot();
                            std::erase_if(m_Graph.Links, [&](auto& l) { return side ? (l.From == n.ID && l.Output == p.Name) : (l.To == n.ID && l.Input == p.Name); });
                            clickedPin = true;
                        }
                        if (ImGui::IsMouseClicked(0))
                        {
                            clickedPin = true;
                            if (side)
                            {
                                m_From = n.ID;
                                m_Output = p.Name;
                            }
                            else if (m_From)
                            {
                                Snapshot();
                                Link l{ m_From, m_Output, n.ID, p.Name };
                                auto candidate = m_Graph;
                                candidate.Links.push_back(l);
                                auto before = m_Graph.Validate();
                                auto after = candidate.Validate();
                                bool addedError = std::any_of(after.begin(), after.end(), [&](const auto& d) { return std::none_of(before.begin(), before.end(), [&](const auto& old) { return old.Node == d.Node && old.Message == d.Message; }); });
                                if (!addedError)
                                {
                                    m_Graph.Links.push_back(l);
                                    m_Error.clear();
                                }
                                else m_Error = after.back().Message;
                                m_From = 0;
                            }
                        }
                    }
                }
            }
            if (!m_FocusNodes.empty() && !m_FocusNodes.contains(n.ID)) draw->AddRectFilled(a, b, IM_COL32(20, 23, 33, 175), 7);
            // Double-clicking a typed function node opens that function (Graph::FunctionNodes).
            if (hover && !clickedPin && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y && ImGui::IsMouseDoubleClicked(0) && n.Kind.starts_with("Function.") && n.Inputs.contains("Name") && n.Inputs.at("Name").is_string())
                openFunction = n.Inputs.at("Name").get<std::string>();
            // Double-clicking a macro instance opens the macro's body.
            if (hover && !clickedPin && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y && ImGui::IsMouseDoubleClicked(0) && n.Kind == "Macro.Instance" && n.Inputs.contains("Name") && n.Inputs.at("Name").is_string())
                openMacro = n.Inputs.at("Name").get<std::string>();
            if (hover && !clickedPin && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y && ImGui::IsMouseClicked(0))
            {
                m_Selected = n.ID;
                if (ImGui::GetIO().KeyCtrl)
                {
                    if (!m_Selection.erase(n.ID)) m_Selection.insert(n.ID);
                }
                else if (!m_Selection.contains(n.ID)) m_Selection = { n.ID };
            }
            if (m_Editable && m_Selected == n.ID && hover && ImGui::IsMouseDragging(0) && !m_From && mouse.y >= a.y && mouse.y < a.y + 30 * m_Zoom)
            {
                if (ImGui::GetIO().MouseDelta.x || ImGui::GetIO().MouseDelta.y)
                {
                    if (!m_Dragging)
                    {
                        Snapshot();
                        m_Dragging = true;
                    }
                    for (auto& selected : m_Graph.Nodes)
                        if (m_Selection.contains(selected.ID) || selected.ID == m_Selected)
                        {
                            selected.X += ImGui::GetIO().MouseDelta.x / m_Zoom;
                            selected.Y += ImGui::GetIO().MouseDelta.y / m_Zoom;
                        }
                    m_Dirty = true;
                }
            }
        }
        if (m_Editable && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
        {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
            {
                m_Selection.clear();
                for (const auto& node : m_Graph.Nodes)
                    if (Visible(node)) m_Selection.insert(node.ID);
            }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) CopySelection();
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteSelection();
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            {
                CopySelection();
                PasteSelection();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) DeleteSelection();
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false)) CollapseSelection();
        }
        if (hover && ImGui::IsMouseClicked(1) && !clickedPin) m_From = 0;
        if (!openFunction.empty()) FocusFunction(openFunction);
        if (!openMacro.empty()) OpenMacro(openMacro);
        draw->AddText({ origin.x + 12, origin.y + size.y - 24 }, IM_COL32(150, 155, 176, 255), Tr("O'rta tugma: surish  |  G'ildirak: masshtab  |  Chiqish > kirish: bog'lash"));
    }
    void VisualGraphPanel::Properties()
    {
        using namespace Visual;
        // Put live values before disabled authoring defaults so a running graph is inspectable
        // without scrolling past two copies of every variable's initial value.
        if (!m_Editable && m_Scene)
        {
            auto actor = m_Scene->GetEntityByUUID(m_Entity);
            auto* script = actor ? actor.TryGetComponent<NativeScriptComponent>() : nullptr;
            auto* host = script && script->ClassName == "Zamin.VisualGraph" ? dynamic_cast<VisualGraphHost*>(script->Instance) : nullptr;
            if (host && script->Fields.contains("Graph") && script->Fields.at("Graph").StringValue == Project::GetAssetRelativePath(m_Path))
            {
                ImGui::SeparatorText(Tr("Jonli o'zgaruvchilar###Live variables"));
                ImGui::TextWrapped("%s", actor.GetName().c_str());
                size_t shown = 0;
                for (const auto& [name, value] : host->VM.Variables())
                {
                    if (shown++ == 16)
                    {
                        ImGui::TextDisabled("%s", Tr("Pastda yana o'zgaruvchilar bor..."));
                        break;
                    }
                    if (value.is_string())
                    {
                        const auto& text = value.get_ref<const std::string&>();
                        ImGui::TextWrapped("%s = %.*s%s", name.c_str(), int(std::min(text.size(), size_t(160))), text.c_str(), text.size() > 160 ? "..." : "");
                    }
                    else ImGui::TextWrapped("%s = %s", name.c_str(), value.dump().c_str());
                }
                ImGui::Separator();
            }
            CombatLive(*m_Scene, actor);
        }
        GraphLayout::RecordRegion("properties");
        ImGui::TextUnformatted(Tr("Tanlangan tugun"));
        ImGui::BeginDisabled(!m_Editable);
        GraphLayout::PushScope("node");
        auto it = std::find_if(m_Graph.Nodes.begin(), m_Graph.Nodes.end(), [&](auto& n) { return n.ID == m_Selected; });
        if (it != m_Graph.Nodes.end() && Catalog().contains(it->Kind))
        {
            auto& n = *it;
            ImGui::TextWrapped("%s  #%u", n.Kind.c_str(), n.ID);
            bool bp = n.Breakpoint;
            if (ImGui::Checkbox(Tr("To'xtash nuqtasi###Breakpoint"), &bp))
            {
                Snapshot();
                n.Breakpoint = bp;
            }
            for (auto& p : Describe(n).Inputs)
            {
                if (p.Kind == Type::Exec) continue;
                bool wired = std::any_of(m_Graph.Links.begin(), m_Graph.Links.end(), [&](auto& l) { return l.To == n.ID && l.Input == p.Name; });
                if (wired)
                {
                    ImGui::TextDisabled("%s: %s", p.Name.c_str(), Tr("ulangan"));
                    continue;
                }
                auto v = n.Inputs.contains(p.Name) ? n.Inputs.at(p.Name) : p.Default;
                // The Enum literal of an enum node: a combo of the graph's enums (empty: the variable's own enum).
                if (p.Name == "Enum" && p.Kind == Type::String && n.Kind.find("Enum") != std::string::npos && v.is_string())
                {
                    ImGui::PushID("enumType");
                    GraphLayout::InlineLabel(p.Name.c_str());
                    const auto current = v.get<std::string>();
                    if (ImGui::BeginCombo("##enum", current.empty() ? Tr("(o'zgaruvchining enumi)") : current.c_str()))
                    {
                        if ((n.Kind.starts_with("Get.") || n.Kind.starts_with("Set.") || n.Kind.starts_with("Local.")) && ImGui::Selectable(Tr("(o'zgaruvchining enumi)"), current.empty()) && !current.empty())
                        {
                            Snapshot();
                            n.Inputs.erase("Enum");
                        }
                        for (const auto& [name, definition] : m_Graph.Enums)
                            if (ImGui::Selectable(name.c_str(), name == current) && name != current)
                            {
                                Snapshot();
                                n.Inputs["Enum"] = name;
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                    continue;
                }
                // The Name literal of a macro instance: a combo of the graph's macros (not the open macro itself).
                if (p.Name == "Name" && n.Kind == "Macro.Instance" && v.is_string())
                {
                    ImGui::PushID("macroName");
                    GraphLayout::InlineLabel(p.Name.c_str());
                    const auto current = v.get<std::string>();
                    if (ImGui::BeginCombo("##macro", current.c_str()))
                    {
                        for (const auto& [name, macro] : m_Graph.Macros)
                            if (name != m_Macro && ImGui::Selectable(name.c_str(), name == current) && name != current)
                            {
                                Snapshot();
                                n.Inputs["Name"] = name;
                                m_Graph.PruneInvalidPins(); // wires/literals of pins the other macro lacks
                            }
                        ImGui::EndCombo();
                    }
                    GraphLayout::RecordKey(p.Name);
                    ImGui::PopID();
                    continue;
                }
                // The Struct literal of a struct node: a combo of the graph's structs; Field: the struct's fields.
                if ((p.Name == "Struct" || p.Name == "Field") && p.Kind == Type::String && n.Kind.find("Struct") != std::string::npos && v.is_string())
                {
                    const bool field = p.Name == "Field";
                    std::string structName;
                    for (const auto* pins : { &Describe(n).Inputs, &Describe(n).Outputs })
                        for (const auto& candidate : *pins)
                            if ((candidate.Kind == Type::Struct || candidate.Kind == Type::ArrayStruct) && structName.empty()) structName = candidate.Enum;
                    std::vector<std::string> options;
                    if (field)
                    {
                        if (auto definition = m_Graph.Structs.find(structName); definition != m_Graph.Structs.end())
                            for (const auto& f : definition->second.Fields) options.push_back(f.Name);
                    }
                    else
                        for (const auto& [name, definition] : m_Graph.Structs) options.push_back(name);
                    const bool variableType = !field && (n.Kind.starts_with("Get.") || n.Kind.starts_with("Set.") || n.Kind.starts_with("Local."));
                    ImGui::PushID(p.Name.c_str());
                    GraphLayout::InlineLabel(p.Name.c_str());
                    const auto current = v.get<std::string>();
                    const char* variableLabel = Tr("(o'zgaruvchining strukturasi)");
                    if (ImGui::BeginCombo("##named", current.empty() && variableType ? variableLabel : current.c_str()))
                    {
                        if (variableType && ImGui::Selectable(variableLabel, current.empty()) && !current.empty())
                        {
                            Snapshot();
                            n.Inputs.erase(p.Name);
                        }
                        for (const auto& option : options)
                            if (ImGui::Selectable(option.c_str(), option == current) && option != current)
                            {
                                Snapshot();
                                n.Inputs[p.Name] = option;
                            }
                        ImGui::EndCombo();
                    }
                    GraphLayout::RecordKey(p.Name);
                    ImGui::PopID();
                    continue;
                }
                if (const auto* choices = p.Kind == Type::Number ? PinChoices(n.Kind, p.Name) : nullptr; choices && v.is_number())
                {
                    if (EditChoice(p.Name.c_str(), *choices, v))
                    {
                        Snapshot();
                        n.Inputs[p.Name] = v;
                    }
                    continue;
                }
                if (EditValue(p.Name.c_str(), p.Kind, v, &m_Graph, p.Enum))
                {
                    Snapshot();
                    n.Inputs[p.Name] = v;
                }
            }
            if (GraphLayout::Button(Tr("Tugunni o'chirish###Delete node"))) DeleteSelection();
        }
        GraphLayout::PopScope();
        Inheritance();
        const bool variablesOpen = ImGui::CollapsingHeader(Tr("Graf o'zgaruvchilari###Graph variables"), ImGuiTreeNodeFlags_DefaultOpen);
        GraphLayout::Record(Tr("Graf o'zgaruvchilari###Graph variables"));
        GraphLayout::PushScope("variables");
        if (variablesOpen) GraphVariables();
        GraphLayout::PopScope();
        Functions();
        Enums();
        Structs();
        Macros();
        ImGui::EndDisabled();
        ImGui::SeparatorText(Tr("Sahnadagi nusxa###Scene instance"));
        SceneInstance();
    }
    void VisualGraphPanel::GraphVariables()
    {
        using namespace Visual;
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##variable", Tr("O'zgaruvchi nomi"), m_Name, sizeof(m_Name));
        GraphLayout::RecordKey("Variable name");
        TypeCombo(Tr("Turi###Type"), m_NewVariableType, m_NewVariableEnum);
        if (GraphLayout::Button(Tr("O'zgaruvchi qo'shish###Add variable")) && m_Name[0] && !m_Graph.Variables.contains(m_Name))
        {
            const std::string named = IsNamed(m_NewVariableType) ? m_NewVariableEnum : "";
            const auto initial = m_Graph.DefaultFor(m_NewVariableType, named);
            if (m_Graph.Fits(m_NewVariableType, named, initial))
            {
                Snapshot();
                m_Graph.Variables[m_Name] = { m_NewVariableType, initial, named };
            }
        }
        std::string remove, renameFrom, renameTo;
        for (auto& [name, v] : m_Graph.Variables)
        {
            // An inherited variable is read-only here (its parent owns it); instance overrides still apply.
            if (m_Graph.IsInherited("Variable", name))
            {
                ImGui::PushID(name.c_str());
                ImGui::BeginDisabled();
                auto value = v.Default;
                EditValue(name.c_str(), v.Kind, value, &m_Graph, v.Enum);
                ImGui::EndDisabled();
                GraphLayout::PushScope(name);
                GraphLayout::FitText((std::string(Tr("meros")) + ": " + (m_Graph.Inherited.Chain.empty() ? m_Graph.Parent : m_Graph.Inherited.Chain.front())).c_str(), true);
                GraphLayout::RecordKey("inherited");
                GraphLayout::PopScope();
                ImGui::PopID();
                ImGui::Spacing();
                continue;
            }
            auto value = v.Default;
            if (EditValue(name.c_str(), v.Kind, value, &m_Graph, v.Enum))
            {
                Snapshot();
                v.Default = value;
            }
            ImGui::PushID(name.c_str());
            GraphLayout::PushScope(name);
            if (GraphLayout::SmallButton(Tr("Olib tashlash###Remove"))) remove = name;
            ImGui::SameLine();
            char renamed[129]{};
            std::snprintf(renamed, sizeof(renamed), "%s", name.c_str());
            // The name field fills the row; Enter renames (the label is in the tooltip, not right of the field).
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##rename", renamed, sizeof(renamed), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                renameFrom = name;
                renameTo = renamed;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr("Qayta nomlash: yangi nomni yozing va Enter bosing"));
            GraphLayout::RecordKey("Rename");
            GraphLayout::PopScope();
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (!remove.empty())
        {
            Snapshot();
            m_Graph.Variables.erase(remove);
        }
        if (!renameFrom.empty())
        {
            Snapshot();
            if (!m_Graph.RenameVariable(renameFrom, renameTo)) m_Error = "Variable rename rejected: duplicate/empty name";
        }
    }
    void VisualGraphPanel::SceneInstance()
    {
        using namespace Visual;
        GraphLayout::PushScope("instance");
        Entity selected = m_Scene ? m_Scene->GetEntityByUUID(m_Entity) : Entity{};
        if (selected)
        {
            ImGui::TextWrapped("%s", selected.GetName().c_str());
            auto* c = selected.TryGetComponent<NativeScriptComponent>();
            ImGui::BeginDisabled(!m_Editable || m_Dirty || m_Path.empty());
            bool replace = c && !c->ClassName.empty() && c->ClassName != "Zamin.VisualGraph";
            if (replace) ImGui::TextWrapped("This entity has a C++ script. Attaching replaces it.");
            if (GraphLayout::Button(replace ? Tr("Skriptni shu graf bilan almashtirish###Replace script with this graph") : Tr("Saqlangan grafni biriktirish###Attach saved graph")))
            {
                if (!c) c = &selected.AddComponent<NativeScriptComponent>();
                c->ClassName = "Zamin.VisualGraph";
                c->Fields.clear();
                c->Fields["Graph"].Set(Project::GetAssetRelativePath(m_Path));
                if (OnModified) OnModified();
            }
            ImGui::EndDisabled();
            if (c && c->ClassName == "Zamin.VisualGraph" && c->Fields.contains("Graph") && c->Fields.at("Graph").StringValue == Project::GetAssetRelativePath(m_Path))
            {
                ImGui::SeparatorText(Tr("Nusxa qiymatlari###Instance overrides"));
                ImGui::BeginDisabled(!m_Editable);
                Value overrides = Value::object();
                auto fi = c->Fields.find("Overrides");
                if (fi != c->Fields.end() && !fi->second.StringValue.empty())
                {
                    auto parsed = Value::parse(fi->second.StringValue, nullptr, false);
                    if (parsed.is_object()) overrides = std::move(parsed);
                }
                for (auto& [name, var] : m_Graph.Variables)
                {
                    auto value = overrides.contains(name) && m_Graph.Fits(var.Kind, var.Enum, overrides[name]) ? overrides[name] : var.Default;
                    ImGui::PushID("instance");
                    if (EditValue(name.c_str(), var.Kind, value, &m_Graph, var.Enum))
                    {
                        overrides[name] = value;
                        c->Fields["Overrides"].Set(overrides.dump());
                        if (OnModified) OnModified();
                    }
                    ImGui::PopID();
                }
                if (GraphLayout::Button(Tr("Nusxa qiymatlarini tiklash###Reset instance overrides")))
                {
                    c->Fields.erase("Overrides");
                    if (OnModified) OnModified();
                }
                ImGui::EndDisabled();
            }
            if (c && c->Instance && c->ClassName == "Zamin.VisualGraph")
            {
                auto* host = dynamic_cast<VisualGraphHost*>(c->Instance);
                if (host)
                {
                    auto& vm = host->VM;
                    ImGui::Checkbox(Tr("To'xtash nuqtalarini yoqish###Enable breakpoints"), &vm.Debug);
                    ImGui::Text("%s: %u | %s: %zu", Tr("Tugun"), vm.CurrentNode, Tr("Navbatda"), vm.Pending());
                    ImGui::Text("%s: %.1f KiB", Tr("Saqlangan ma'lumot"), vm.RetainedBytes() / 1024.0);
                    if (!vm.CallStack().empty()) ImGui::TextWrapped("Calls: %s", vm.CallStack().c_str());
                    const auto frame = vm.DebugFrame();
                    if (!frame.empty() && ImGui::TreeNode("Function frame"))
                    {
                        ImGui::TextWrapped("%s", frame.dump(2).c_str());
                        ImGui::TreePop();
                    }
                    if (ImGui::Button(vm.Paused ? Tr("Davom etish###Continue") : Tr("Pauza###Pause")))
                    {
                        if (vm.Paused) vm.Continue();
                        else vm.Paused = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(Tr("Qadam###Step"))) vm.Step();
                    if (!vm.Error().empty()) ImGui::TextWrapped("%s", vm.Error().c_str());
                    for (auto& [name, value] : vm.Variables()) ImGui::TextWrapped("%s = %s", name.c_str(), value.dump().c_str());
                    if (ImGui::TreeNode("Execution trace"))
                    {
                        for (auto id : vm.Trace()) ImGui::Text("#%u", vm.Origin(id));
                        ImGui::TreePop();
                    }
                }
            }
        }
        else ImGui::TextWrapped("%s", Tr("Biriktirish uchun sahnadan obyekt tanlang."));
        GraphLayout::PopScope();
    }
}
