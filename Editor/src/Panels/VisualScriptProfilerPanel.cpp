#include "VisualScriptProfilerPanel.h"
#include "VisualGraphLayout.h"
#include "Zamin/Editor/EditorLanguage.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include <imgui.h>
#include <algorithm>
#include <format>
namespace Zamin {
    Visual::Machine::Profile CollectGraphProfile(Scene& scene, const std::string& graph, size_t* instances, Visual::Graph* loaded)
    {
        Visual::Machine::Profile profile;
        size_t count = 0;
        auto& registry = scene.GetRegistry();
        for (auto handle : registry.view<NativeScriptComponent>())
        {
            const auto& script = registry.get<NativeScriptComponent>(handle);
            auto* host = script.ClassName == "Zamin.VisualGraph" ? dynamic_cast<VisualGraphHost*>(script.Instance) : nullptr;
            if (!host || host->Graph != graph) continue;
            profile.Merge(host->VM.GetProfile());
            if (loaded && count == 0 && loaded->Nodes.empty()) *loaded = host->VM.Loaded();
            ++count;
        }
        if (instances) *instances = count;
        return profile;
    }
    void VisualScriptProfilerPanel::SetContext(const Ref<Scene>& scene, bool playing, const std::string& graph, const Visual::Graph* source)
    {
        if (graph != m_Graph || (playing && !m_Playing))
        {
            // Another graph, or a new Play: the previous profile goes.
            m_Profile = {};
            m_Loaded = {};
            m_Instances = 0;
        }
        m_Scene = scene;
        m_Playing = playing;
        m_Graph = graph;
        m_Source = source;
        if (m_Playing && m_Scene && !m_Graph.empty())
        {
            size_t instances = 0;
            auto profile = CollectGraphProfile(*m_Scene, m_Graph, &instances, &m_Loaded);
            if (instances)
            {
                m_Profile = std::move(profile);
                m_Instances = instances;
            }
        }
    }
    std::string VisualScriptProfilerPanel::NodeName(uint32_t id) const
    {
        // The open graph's node (a macro instance, a node of the graph itself), else the running program's (an
        // inherited node).
        if (m_Source)
            if (const auto* n = m_Source->Find(id)) return std::format("#{} {}", id, n->Kind == "Native.Call" || n->Kind == "Native.Pure" ? "C++ " + Visual::NativeKey(*n) : n->Kind);
        if (const auto* n = m_Loaded.Find(id)) return std::format("#{} {}", id, n->Kind);
        return std::format("#{}", id);
    }
    void VisualScriptProfilerPanel::OnImGuiRender(bool* show)
    {
        if (!*show) return;
        // First opened: at the bottom right, off the canvas's top left where a graph's events usually are.
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({ viewport->WorkPos.x + viewport->WorkSize.x - 640, viewport->WorkPos.y + viewport->WorkSize.y - 540 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 620, 520 }, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Tr("Visual Script profileri###VisualScriptProfiler"), show))
        {
            ImGui::End();
            return;
        }
        GraphLayout::PushScope("profiler");
        ImGui::Checkbox(Tr("Yozib borish###Record"), &m_Record);
        GraphLayout::Record(Tr("Yozib borish###Record"));
        ImGui::SameLine();
        if (GraphLayout::Button(Tr("Tozalash###Reset")))
        {
            m_Profile = {};
            if (m_Scene)
            {
                auto& registry = m_Scene->GetRegistry();
                for (auto handle : registry.view<NativeScriptComponent>())
                    if (auto* host = dynamic_cast<VisualGraphHost*>(registry.get<NativeScriptComponent>(handle).Instance); host && host->Graph == m_Graph) host->VM.ResetProfile();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_Graph.empty() ? Tr("(graf ochilmagan)") : m_Graph.c_str());
        double total = 0;
        for (const auto& [id, n] : m_Profile.Nodes) total += n.Exclusive;
        ImGui::Text(Tr("Nusxalar: %zu  |  ishga tushishlar: %llu  |  VM vaqti: %.3f ms  |  tugunlar: %zu"), m_Instances, (unsigned long long)m_Profile.Runs, m_Profile.Seconds * 1000.0, m_Profile.Nodes.size());
        GraphLayout::RecordValue("profiler/instances", double(m_Instances));
        GraphLayout::RecordValue("profiler/nodes", double(m_Profile.Nodes.size()));
        GraphLayout::RecordValue("profiler/functions", double(m_Profile.Functions.size()));
        if (m_Profile.Nodes.empty())
        {
            ImGui::TextWrapped("%s", Tr("Grafni Visual Script oynasida oching va o'yinni ishga tushiring (Play): har bir tugunning chaqiruvlari va vaqti shu yerda ko'rinadi."));
            GraphLayout::PopScope();
            ImGui::End();
            return;
        }
        // The hot path: the most taken execution path from the costliest entry.
        const auto path = m_Profile.HotPath(24);
        if (ImGui::CollapsingHeader(Tr("Issiq yo'l###Hot path"), ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < path.size(); ++i)
            {
                const auto& n = m_Profile.Nodes.contains(path[i]) ? m_Profile.Nodes.at(path[i]) : Visual::Machine::NodeProfile{};
                const std::string label = std::format("{}{}  ({} x, {:.3f} ms)###hot{}", i ? "-> " : "", NodeName(path[i]), n.Calls, n.Exclusive * 1000.0, i);
                if (ImGui::Selectable(label.c_str()) && OnSelectNode) OnSelectNode(path[i]);
                GraphLayout::RecordRect("profiler/hot/" + std::to_string(i), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            }
            GraphLayout::RecordValue("profiler/hotPath", double(path.size()));
        }
        if (ImGui::CollapsingHeader(Tr("Tugunlar###Nodes"), ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* sorts[] = { "Tugun", "Chaqiruvlar", "O'zi (ms)", "Jami (ms)" };
            ImGui::SetNextItemWidth(180);
            if (ImGui::BeginCombo(Tr("Tartib###Sort"), Tr(sorts[m_Sort])))
            {
                for (int i = 0; i < 4; ++i)
                    if (ImGui::Selectable(Tr(sorts[i]), i == m_Sort)) m_Sort = i;
                ImGui::EndCombo();
            }
            std::vector<uint32_t> ids;
            for (const auto& [id, n] : m_Profile.Nodes) ids.push_back(id);
            const auto& nodes = m_Profile.Nodes;
            std::sort(ids.begin(), ids.end(), [&](uint32_t a, uint32_t b) {
                const auto &x = nodes.at(a), &y = nodes.at(b);
                if (m_Sort == 1 && x.Calls != y.Calls) return x.Calls > y.Calls;
                if (m_Sort == 2 && x.Exclusive != y.Exclusive) return x.Exclusive > y.Exclusive;
                if (m_Sort == 3 && x.Inclusive != y.Inclusive) return x.Inclusive > y.Inclusive;
                return a < b;
            });
            if (ids.size() > 200) ids.resize(200);
            if (ImGui::BeginTable("##profileNodes", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                                  { 0, std::min(12.0f, float(ids.size()) + 1.5f) * ImGui::GetFrameHeight() }))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(Tr("Tugun"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(Tr("Chaqiruvlar"), ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn(Tr("O'zi (ms)"), ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn(Tr("Jami (ms)"), ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 50);
                ImGui::TableHeadersRow();
                for (uint32_t id : ids)
                {
                    const auto& n = nodes.at(id);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable((NodeName(id) + "###node" + std::to_string(id)).c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && OnSelectNode) OnSelectNode(id);
                    GraphLayout::RecordRect("profiler/node/" + std::to_string(id), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", (unsigned long long)n.Calls);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", n.Exclusive * 1000.0);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", n.Inclusive * 1000.0);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f", total > 0 ? 100.0 * n.Exclusive / total : 0.0);
                }
                ImGui::EndTable();
            }
        }
        if (!m_Profile.Functions.empty() && ImGui::CollapsingHeader(Tr("Funksiyalar###Functions"), ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("##profileFunctions", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn(Tr("Funksiya"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(Tr("Chaqiruvlar"), ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn(Tr("O'zi (ms)"), ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn(Tr("Jami (ms)"), ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableHeadersRow();
                for (const auto& [name, f] : m_Profile.Functions)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(name.c_str());
                    GraphLayout::RecordRect("profiler/function/" + name, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", (unsigned long long)f.Calls);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", f.Exclusive * 1000.0);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", f.Inclusive * 1000.0);
                }
                ImGui::EndTable();
            }
        }
        GraphLayout::PopScope();
        ImGui::End();
    }
}
