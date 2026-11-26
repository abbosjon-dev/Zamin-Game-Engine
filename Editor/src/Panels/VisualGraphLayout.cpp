#include "VisualGraphLayout.h"
#include "Zamin/Utils/FileSystem.h"
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <vector>
namespace Zamin::GraphLayout {
    namespace {
        struct State
        {
            const char* Path = std::getenv("ZAMIN_EDITOR_VISUAL_LAYOUT");
            std::vector<std::string> Scope;
            nlohmann::json Items = nlohmann::json::object(), Regions = nlohmann::json::object();
            std::string Written;
        };
        State& S()
        {
            static State state;
            return state;
        }
        // The right edge items may reach: the window's work rectangle (inside padding and the scrollbar).
        float RightEdge() { return ImGui::GetCurrentWindow()->WorkRect.Max.x; }
        std::string Id(const char* label)
        {
            std::string_view text = label;
            if (auto id = text.find("###"); id != std::string_view::npos) return std::string(text.substr(id + 3));
            return std::string(Visible(label));
        }
    }
    std::string_view Visible(const char* label)
    {
        std::string_view text = label;
        return text.substr(0, text.find("##"));
    }
    bool Recording() { return S().Path && *S().Path; }
    void PushScope(const std::string& scope) { S().Scope.push_back(scope); }
    void PopScope()
    {
        if (!S().Scope.empty()) S().Scope.pop_back();
    }
    void RecordKey(const std::string& id)
    {
        if (!Recording()) return;
        std::string key;
        for (const auto& part : S().Scope) key += part + "/";
        key += id;
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        // Clipped: the item reaches past the window's visible clip rectangle at the right (its end is not shown).
        S().Items[key] = { { "Rect", { min.x, min.y, max.x, max.y } }, { "Clipped", max.x > ImGui::GetCurrentWindow()->InnerClipRect.Max.x + 1.f } };
    }
    void Record(const char* label) { RecordKey(Id(label)); }
    void RecordRect(const std::string& key, ImVec2 min, ImVec2 max)
    {
        if (Recording()) S().Items[key] = { { "Rect", { min.x, min.y, max.x, max.y } }, { "Clipped", false } };
    }
    void RecordRegion(const char* name)
    {
        if (!Recording()) return;
        const auto* window = ImGui::GetCurrentWindow();
        S().Regions[name] = { window->InnerRect.Min.x, window->InnerRect.Min.y, window->InnerRect.Max.x, window->InnerRect.Max.y };
    }
    void Flush()
    {
        if (!Recording()) return;
        auto& state = S();
        const std::string text = nlohmann::json{ { "Items", state.Items }, { "Regions", state.Regions } }.dump();
        if (text != state.Written && FileSystem::WriteTextFileAtomic(state.Path, text)) state.Written = text;
        state.Items = nlohmann::json::object();
        state.Regions = nlohmann::json::object();
        state.Scope.clear();
    }
    void FitText(const char* text, bool disabled)
    {
        bool cut = false;
        const auto shown = Cut(text, std::max(1.f, ImGui::GetContentRegionAvail().x), cut);
        if (disabled) ImGui::TextDisabled("%s", shown.c_str());
        else ImGui::TextUnformatted(shown.c_str());
        if (cut && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
        RecordKey(std::string(Visible(text)) + "#label");
    }
    std::string Field(const char* label)
    {
        FitText(std::string(Visible(label)).c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        return std::string("##") + label;
    }
    void InlineLabel(const char* text, float column)
    {
        const float start = ImGui::GetCursorPosX(), avail = ImGui::GetContentRegionAvail().x;
        const float width = ImGui::CalcTextSize(text).x;
        ImGui::AlignTextToFramePadding();
        if (width + ImGui::GetStyle().ItemSpacing.x <= column && avail >= column + 90)
        {
            ImGui::TextUnformatted(text);
            ImGui::SameLine(start + column);
        }
        else FitText(text);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }
    float ButtonWidth(const char* label, bool small)
    {
        const std::string visible(Visible(label));
        (void)small; // a small button keeps the horizontal frame padding
        return ImGui::CalcTextSize(visible.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.f;
    }
    void SameLineIfFits(float width)
    {
        const float x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
        if (x + width <= RightEdge()) ImGui::SameLine();
    }
    bool Button(const char* label)
    {
        const bool pressed = ImGui::Button(label);
        Record(label);
        return pressed;
    }
    bool SmallButton(const char* label)
    {
        const bool pressed = ImGui::SmallButton(label);
        Record(label);
        return pressed;
    }
    std::string Cut(const std::string& text, float width, bool& cut)
    {
        cut = ImGui::CalcTextSize(text.c_str()).x > width;
        if (!cut) return text;
        std::string shown = text;
        while (!shown.empty() && ImGui::CalcTextSize((shown + "...").c_str()).x > width)
        {
            shown.pop_back();
            while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80) shown.pop_back();
            if (!shown.empty() && static_cast<unsigned char>(shown.back()) >= 0xC0) shown.pop_back();
        }
        return shown + "...";
    }
    bool TreeNode(const char* id, const std::string& text, const std::string& key)
    {
        bool cut = false;
        const auto shown = Cut(text, std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::GetTreeNodeToLabelSpacing()), cut);
        const bool open = ImGui::TreeNode(id, "%s", shown.c_str());
        if (cut && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.c_str());
        RecordKey(key.empty() ? text : key);
        return open;
    }
}
