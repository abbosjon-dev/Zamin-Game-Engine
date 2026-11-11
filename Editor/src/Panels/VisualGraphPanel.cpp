#include <cstdlib>
#include "VisualGraphPanel.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Asset/AssetRegistry.h"
#include <algorithm>
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
                default:                   return IM_COL32(255, 194, 85, 255);
            }
        }
        bool EditValue(const char* label, Visual::Type type, Visual::Value& v)
        {
            using namespace Visual;
            ImGui::PushID(label);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(112);
            ImGui::SetNextItemWidth(-1);
            bool changed = false;
            if (!Matches(type, v))
            {
                ImGui::TextDisabled("Invalid literal (%s)", TypeName(type));
                if (ImGui::Button("Reset literal"))
                {
                    v = DefaultValue(type);
                    changed = true;
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
                    if (ImGui::SmallButton("Previous") && page > 0) --page;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Next") && size_t((page + 1) * 16) < v.size()) ++page;
                    state->SetInt(pageID, page);
                    size_t remove = SIZE_MAX;
                    for (size_t i = size_t(page) * 16; i < std::min(v.size(), size_t(page + 1) * 16); ++i)
                    {
                        ImGui::PushID(int(i));
                        changed |= EditValue(("[" + std::to_string(i) + "]").c_str(), ElementType(type), v[i]);
                        if (ImGui::SmallButton("Remove item")) remove = i;
                        ImGui::PopID();
                    }
                    if (remove != SIZE_MAX)
                    {
                        v.erase(v.begin() + remove);
                        changed = true;
                    }
                    if (v.size() < 1024 && ImGui::Button("Add item"))
                    {
                        v.push_back(DefaultValue(ElementType(type)));
                        changed = true;
                    }
                    if (ImGui::Button("Paste array JSON"))
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
                                if (!parsed.is_discarded() && Matches(type, parsed))
                                {
                                    v = std::move(parsed);
                                    changed = true;
                                }
                            }
                            catch (const std::exception&)
                            {
                                ImGui::TextDisabled("Invalid array JSON");
                            }
                        }
                    }
                    ImGui::TreePop();
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
                if (id == UINT64_MAX) ImGui::TextDisabled("Self");
                else ImGui::TextDisabled("%llu", (unsigned long long)id);
                if (ImGui::Button("Self"))
                {
                    v = UINT64_MAX;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("None"))
                {
                    v = 0ull;
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::Button("Drop entity");
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
            ImGui::PopID();
            return changed;
        }
    }
    bool VisualGraphPanel::EditLiteral(const char* label, Visual::Type type, Visual::Value& value) { return EditValue(label, type, value); }
    void VisualGraphPanel::SetContext(const Ref<Scene>& scene, bool editable, uint64_t selection)
    {
        m_Scene = scene;
        m_Editable = editable;
        m_Entity = selection;
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
            m_Graph = Visual::Graph::Load(path);
            m_Path = path;
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
        if (ImGui::Button("Fit graph")) m_Fit = true;
        ImGui::SameLine();
        ImGui::TextColored({ .66f, .54f, 1, 1 }, "ZAMIN  /  VISUAL SCRIPT");
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", m_Path.filename().string().c_str(), m_Dirty ? " *" : "");
        ImGui::BeginDisabled(!m_Editable);
        ImGui::SetNextItemWidth(260);
        ImGui::InputText("##graphPath", m_File, sizeof(m_File));
        ImGui::SameLine();
        if (ImGui::Button("Open"))
        {
            auto p = FileSystem::SafeJoin(Project::GetAssetDirectory(), m_File);
            if (!p.empty()) Open(p);
        }
        ImGui::SameLine();
        if (ImGui::Button("New") && !m_Dirty)
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
        if (ImGui::Button("Save"))
        {
            if (m_Path.empty() || !FileSystem::IsSubPathOnDisk(m_Path, Project::GetAssetDirectory())) m_Error = "Choose a path inside Assets.";
            else if (m_Graph.Save(m_Path))
            {
                m_Dirty = false;
                m_Error.clear();
                if (auto* r = Project::GetAssetRegistry()) r->OnFileAdded(m_Path);
            }
            else m_Error = "Could not save graph; your edits are still here.";
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_Undo.empty());
        if (ImGui::Button("Undo"))
        {
            m_Redo.push_back(m_Graph.Serialize());
            m_Graph = Visual::Graph::Parse(m_Undo.back());
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
        if (ImGui::Button("Redo"))
        {
            m_Undo.push_back(m_Graph.Serialize());
            m_Graph = Visual::Graph::Parse(m_Redo.back());
            m_ValidateDirty = true;
            m_Descriptions.clear();
            m_Selection.clear();
            m_Redo.pop_back();
            m_Dirty = true;
            m_From = 0;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard edits"))
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
        if (ImGui::Button("Copy")) CopySelection();
        ImGui::SameLine();
        if (ImGui::Button("Paste")) PasteSelection();
        ImGui::SameLine();
        if (ImGui::Button("Duplicate"))
        {
            CopySelection();
            PasteSelection();
        }
        ImGui::EndDisabled();
        if (!m_Error.empty()) ImGui::TextColored({ 1, .4f, .4f, 1 }, "%s", m_Error.c_str());
        if (m_ValidateDirty)
        {
            m_Diagnostics = m_Graph.Validate();
            m_ValidateDirty = false;
            m_Descriptions.clear();
        }
        const auto& diagnostics = m_Diagnostics;
        if (diagnostics.empty()) ImGui::TextColored({ .35f, .85f, .6f, 1 }, "Compiled  |  %zu nodes  |  %zu wires  |  %zu variables", m_Graph.Nodes.size(), m_Graph.Links.size(), m_Graph.Variables.size());
        else
        {
            ImGui::TextColored({ 1, .65f, .3f, 1 }, "%zu compile errors", diagnostics.size());
            if (ImGui::TreeNode("Diagnostics"))
            {
                for (auto& d : diagnostics)
                    if (ImGui::Selectable(("Node " + std::to_string(d.Node) + ": " + d.Message).c_str())) m_Selected = d.Node;
                ImGui::TreePop();
            }
        }
        ImGui::Separator();
        float height = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("palette", { 205, height }, ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Node library");
        ImGui::InputTextWithHint("##search", "Search nodes...", m_Search, sizeof(m_Search));
        ImGui::TextDisabled("Click to add a node");
        ImGui::BeginDisabled(!m_Editable);
        std::string cat;
        for (auto& [key, def] : Visual::Catalog())
        {
            std::string q = m_Search, name = key;
            std::transform(q.begin(), q.end(), q.begin(), ::tolower);
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (!q.empty() && name.find(q) == std::string::npos) continue;
            if (ImGui::Selectable((def.Category + " / " + key).c_str()))
            {
                Snapshot();
                uint64_t nextID = 1;
                for (auto& n : m_Graph.Nodes) nextID = std::max(nextID, uint64_t(n.ID) + 1);
                if (nextID > UINT32_MAX || m_Graph.Nodes.size() >= 4096)
                {
                    m_Error = "Node count/ID limit reached";
                    continue;
                }
                const auto id = uint32_t(nextID);
                float x = (100 - m_Pan.x) / m_Zoom, y = (100 - m_Pan.y) / m_Zoom;
                m_Graph.Nodes.push_back({ id, key, x, y });
                if (key.starts_with("Function.")) m_Graph.Nodes.back().Inputs["Name"] = m_Graph.Functions.empty() ? "MyFunction" : m_Graph.Functions.begin()->first;
                m_Selected = id;
                m_Selection = { id };
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
            if (!a || !b || !Catalog().contains(a->Kind) || !Catalog().contains(b->Kind)) continue;
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
                if (auto* h = dynamic_cast<VisualGraphHost*>(c->Instance)) runningNode = h->VM.CurrentNode;
        }
        bool clickedPin = false;
        for (auto& n : m_Graph.Nodes)
        {
            auto it = Catalog().find(n.Kind);
            if (it == Catalog().end()) continue;
            const auto& d = Describe(n);
            auto a = screen(n.X, n.Y), b = screen(n.X + 220, n.Y + 65 + 24 * (float)std::max(d.Inputs.size(), d.Outputs.size()));
            draw->AddRectFilled({ a.x + 4, a.y + 5 }, { b.x + 4, b.y + 5 }, IM_COL32(0, 0, 0, 70), 8);
            draw->AddRectFilled(a, b, IM_COL32(40, 44, 61, 255), 7);
            draw->AddRectFilled(a, { b.x, a.y + 30 * m_Zoom }, n.Kind.starts_with("Event.") ? IM_COL32(125, 47, 76, 255) : d.Pure ? IM_COL32(42, 98, 83, 255)
                                                                                                                                  : IM_COL32(83, 57, 137, 255),
                                7);
            draw->AddText(nullptr, ImGui::GetFontSize() * m_Zoom, { a.x + 10 * m_Zoom, a.y + 6 * m_Zoom }, IM_COL32(245, 246, 255, 255), n.Kind.c_str());
            if (m_Selection.contains(n.ID) || n.ID == m_Selected) draw->AddRect(a, b, IM_COL32(191, 152, 255, 255), 7, 0, 2);
            if (n.ID == runningNode) draw->AddRect(a, b, IM_COL32(255, 205, 80, 255), 7, 0, 3);
            if (n.Breakpoint) draw->AddCircleFilled({ b.x - 10, a.y + 12 }, 5, IM_COL32(255, 70, 80, 255));
            for (int side = 0; side < 2; ++side)
            {
                auto& ps = side ? d.Outputs : d.Inputs;
                for (auto& p : ps)
                {
                    auto pos = pin(n, ps, p.Name, side);
                    draw->AddCircleFilled(pos, 5 * m_Zoom, Color(p.Kind));
                    float textWidth = ImGui::CalcTextSize(p.Name.c_str()).x * m_Zoom;
                    draw->AddText(nullptr, ImGui::GetFontSize() * m_Zoom, { pos.x + (side ? -textWidth - 12 * m_Zoom : 12 * m_Zoom), pos.y - 7 * m_Zoom }, Color(p.Kind), p.Name.c_str());
                    float dx = mouse.x - pos.x, dy = mouse.y - pos.y;
                    if (hover && dx * dx + dy * dy < 100 && m_Editable)
                    {
                        ImGui::SetTooltip("%s (%s) | Right-click: disconnect", p.Name.c_str(), TypeName(p.Kind));
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
                for (const auto& node : m_Graph.Nodes) m_Selection.insert(node.ID);
            }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) CopySelection();
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteSelection();
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            {
                CopySelection();
                PasteSelection();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) DeleteSelection();
        }
        if (hover && ImGui::IsMouseClicked(1) && !clickedPin) m_From = 0;
        draw->AddText({ origin.x + 12, origin.y + size.y - 24 }, IM_COL32(150, 155, 176, 255), "Middle drag: pan  |  Wheel: zoom  |  Output > input: wire");
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
                ImGui::SeparatorText("Live variables");
                ImGui::TextWrapped("%s", actor.GetName().c_str());
                size_t shown = 0;
                for (const auto& [name, value] : host->VM.Variables())
                {
                    if (shown++ == 16)
                    {
                        ImGui::TextDisabled("More variables below...");
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
        }
        ImGui::TextUnformatted("Selected node");
        ImGui::BeginDisabled(!m_Editable);
        auto it = std::find_if(m_Graph.Nodes.begin(), m_Graph.Nodes.end(), [&](auto& n) { return n.ID == m_Selected; });
        if (it != m_Graph.Nodes.end() && Catalog().contains(it->Kind))
        {
            auto& n = *it;
            ImGui::TextWrapped("%s  #%u", n.Kind.c_str(), n.ID);
            bool bp = n.Breakpoint;
            if (ImGui::Checkbox("Breakpoint", &bp))
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
                    ImGui::TextDisabled("%s: connected", p.Name.c_str());
                    continue;
                }
                auto v = n.Inputs.contains(p.Name) ? n.Inputs.at(p.Name) : p.Default;
                if (EditValue(p.Name.c_str(), p.Kind, v))
                {
                    Snapshot();
                    n.Inputs[p.Name] = v;
                }
            }
            if (ImGui::Button("Delete node"))
            {
                Snapshot();
                auto selected = m_Selection;
                if (selected.empty()) selected.insert(m_Selected);
                std::erase_if(m_Graph.Links, [&](const auto& link) { return selected.contains(link.From) || selected.contains(link.To); });
                std::erase_if(m_Graph.Nodes, [&](const auto& node) { return selected.contains(node.ID); });
                m_Selected = 0;
                m_Selection.clear();
            }
        }
        ImGui::SeparatorText("Graph variables");
        ImGui::InputTextWithHint("##variable", "Variable name", m_Name, sizeof(m_Name));
        static int type = 2;
        const char* types[] = { "Exec", "Bool", "Number", "String", "Vec2", "Vec3", "Vec4", "Entity", "ArrayBool", "ArrayNumber", "ArrayString", "ArrayVec2", "ArrayVec3", "ArrayVec4", "ArrayEntity" };
        ImGui::Combo("Type", &type, types, int(Type::Count));
        if (ImGui::Button("Add variable") && m_Name[0] && type > 0 && !m_Graph.Variables.contains(m_Name))
        {
            Snapshot();
            Value d = DefaultValue(Type(type));
            m_Graph.Variables[m_Name] = { (Type)type, d };
        }
        std::string remove, renameFrom, renameTo;
        for (auto& [name, v] : m_Graph.Variables)
        {
            auto value = v.Default;
            if (EditValue(name.c_str(), v.Kind, value))
            {
                Snapshot();
                v.Default = value;
            }
            ImGui::PushID(name.c_str());
            if (ImGui::SmallButton("Remove")) remove = name;
            ImGui::SameLine();
            char renamed[129]{};
            std::snprintf(renamed, sizeof(renamed), "%s", name.c_str());
            if (ImGui::InputText("Rename", renamed, sizeof(renamed), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                renameFrom = name;
                renameTo = renamed;
            }
            ImGui::PopID();
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
        Functions();
        ImGui::EndDisabled();
        ImGui::SeparatorText("Scene instance");
        Entity selected = m_Scene ? m_Scene->GetEntityByUUID(m_Entity) : Entity{};
        if (selected)
        {
            ImGui::TextWrapped("%s", selected.GetName().c_str());
            auto* c = selected.TryGetComponent<NativeScriptComponent>();
            ImGui::BeginDisabled(!m_Editable || m_Dirty || m_Path.empty());
            bool replace = c && !c->ClassName.empty() && c->ClassName != "Zamin.VisualGraph";
            if (replace) ImGui::TextWrapped("This entity has a C++ script. Attaching replaces it.");
            if (ImGui::Button(replace ? "Replace script with this graph" : "Attach saved graph"))
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
                ImGui::SeparatorText("Instance overrides");
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
                    auto value = overrides.contains(name) && Matches(var.Kind, overrides[name]) ? overrides[name] : var.Default;
                    ImGui::PushID("instance");
                    if (EditValue(name.c_str(), var.Kind, value))
                    {
                        overrides[name] = value;
                        c->Fields["Overrides"].Set(overrides.dump());
                        if (OnModified) OnModified();
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Reset instance overrides"))
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
                    ImGui::Checkbox("Enable breakpoints", &vm.Debug);
                    ImGui::Text("Node: %u | Pending: %zu", vm.CurrentNode, vm.Pending());
                    ImGui::Text("Retained data: %.1f KiB", vm.RetainedBytes() / 1024.0);
                    if (!vm.CallStack().empty()) ImGui::TextWrapped("Calls: %s", vm.CallStack().c_str());
                    const auto frame = vm.DebugFrame();
                    if (!frame.empty() && ImGui::TreeNode("Function frame"))
                    {
                        ImGui::TextWrapped("%s", frame.dump(2).c_str());
                        ImGui::TreePop();
                    }
                    if (ImGui::Button(vm.Paused ? "Continue" : "Pause"))
                    {
                        if (vm.Paused) vm.Continue();
                        else vm.Paused = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Step")) vm.Step();
                    if (!vm.Error().empty()) ImGui::TextWrapped("%s", vm.Error().c_str());
                    for (auto& [name, value] : vm.Variables()) ImGui::TextWrapped("%s = %s", name.c_str(), value.dump().c_str());
                    if (ImGui::TreeNode("Execution trace"))
                    {
                        for (auto id : vm.Trace()) ImGui::Text("#%u", id);
                        ImGui::TreePop();
                    }
                }
            }
        }
        else ImGui::TextDisabled("Select a scene entity to attach.");
    }
}
