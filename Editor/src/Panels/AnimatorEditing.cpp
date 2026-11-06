#include "AnimatorEditing.h"
#include "../UI.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin::AnimatorEditing {

    static const ImVec4 s_WarnColor(0.95f, 0.75f, 0.3f, 1.0f);

    std::string StateName(const AnimatorComponent& c, int index)
    {
        if (index < 0)
            return "Har qanday holat";
        return index < (int)c.States.size() ? c.States[(size_t)index].Name : std::string("?");
    }

    bool ClipCombo(const char* id, std::string& clip, const Model* model, const char* none)
    {
        bool changed = false;
        const char* preview = clip.empty() ? (none ? none : "(klip)") : clip.c_str();
        if (ImGui::BeginCombo(id, preview))
        {
            if (none && ImGui::Selectable(none, clip.empty()))
            {
                clip.clear();
                changed = true;
            }
            if (model)
                for (const auto& a : model->GetAnimations())
                    if (ImGui::Selectable(a.Name.c_str(), a.Name == clip))
                    {
                        clip = a.Name;
                        changed = true;
                    }
            if (!model || model->GetAnimations().empty())
                ImGui::TextDisabled("Modelda klip yo'q");
            ImGui::EndCombo();
        }
        // A clip the model does not have (renamed, another model): shown, not lost.
        if (model && !clip.empty() && model->FindAnimation(clip) < 0 && ImGui::IsItemHovered())
            UI::Tooltip("Modelda bunday klip yo'q");
        return changed;
    }

    bool FloatParameterCombo(const char* id, std::string& parameter, const AnimatorComponent& c, const char* none)
    {
        bool changed = false;
        if (ImGui::BeginCombo(id, parameter.empty() ? none : parameter.c_str()))
        {
            if (ImGui::Selectable(none, parameter.empty()))
            {
                parameter.clear();
                changed = true;
            }
            for (const auto& p : c.Parameters)
                if (p.Kind == AnimatorParameter::Type::Float && ImGui::Selectable(p.Name.c_str(), p.Name == parameter))
                {
                    parameter = p.Name;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        return changed;
    }

    // Two columns: a fixed label column and the value (full width).
    static bool BeginFields(const char* id)
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
            return false;
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 96.0f);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    static void Field(const char* label, const char* tooltip = nullptr)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip(tooltip);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    bool DrawParameters(AnimatorComponent& c, bool live)
    {
        bool changed = false;
        const char* types[] = { "Float", "Bool", "Trigger" };
        int remove = -1;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        for (size_t i = 0; i < c.Parameters.size(); i++)
        {
            auto& p = c.Parameters[i];
            ImGui::PushID((int)i);
            const float button = ImGui::GetFrameHeight();
            const float avail = ImGui::GetContentRegionAvail().x - button - spacing * 3.0f;
            ImGui::SetNextItemWidth(avail * 0.38f);
            changed |= ImGui::InputText("##n", &p.Name);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(avail * 0.30f);
            int t = (int)p.Kind;
            if (ImGui::Combo("##t", &t, types, 3))
            {
                p.Kind = (AnimatorParameter::Type)t;
                if (p.Kind != AnimatorParameter::Type::Float)
                    p.Value = p.Value != 0.0f ? 1.0f : 0.0f;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(avail * 0.32f);
            if (p.Kind == AnimatorParameter::Type::Float)
                changed |= ImGui::DragFloat("##v", &p.Value, 0.05f, 0.0f, 0.0f, "%.2f");
            else if (p.Kind == AnimatorParameter::Type::Bool)
            {
                bool b = p.Value != 0.0f;
                if (ImGui::Checkbox("##v", &b))
                {
                    p.Value = b ? 1.0f : 0.0f;
                    changed = true;
                }
            }
            else
            {
                // A trigger: pressed sets it; it stays lit until a transition uses it up.
                const bool pending = p.Value != 0.0f;
                if (pending)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
                if (ImGui::Button(live ? ICON_FA_BOLT "  Ishga tushirish" : (pending ? ICON_FA_BOLT "  Yoqilgan" : ICON_FA_BOLT "  O'chiq"),
                                  ImVec2(avail * 0.32f, 0)))
                {
                    p.Value = live ? 1.0f : (pending ? 0.0f : 1.0f);
                    changed = true;
                }
                if (pending)
                    ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    UI::Tooltip(live ? "Trigger: bir marta yoqiladi, uni ishlatgan o'tish o'chiradi (to'q sariq - kutmoqda)"
                                     : "Trigger boshlang'ich qiymati");
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(button, 0)))
                remove = (int)i;
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            c.Parameters.erase(c.Parameters.begin() + remove);
            changed = true;
        }
        if (ImGui::SmallButton(ICON_FA_PLUS "  Parametr"))
        {
            std::string name = "Param";
            for (int n = (int)c.Parameters.size(); c.FindParameter(name); n++)
                name = std::format("Param{}", n);
            c.Parameters.push_back({ name, AnimatorParameter::Type::Float, 0.0f });
            changed = true;
        }
        return changed;
    }

    bool DrawState(AnimatorComponent& c, int index, const Model* model)
    {
        if (index < 0 || index >= (int)c.States.size())
            return false;
        bool changed = false;
        AnimatorState& st = c.States[(size_t)index];
        ImGui::PushID(index);
        if (BeginFields("##state"))
        {
            Field("Nom");
            changed |= ImGui::InputText("##name", &st.Name);
            Field("Tur", "Klip: bitta animatsiya. Blend tree 1D: float parametr bo'yicha bir nechta klip aralashadi "
                         "(masalan tezlik: turish / yurish / yugurish) - ular bir xil normalizatsiyalangan vaqtda o'ynaydi");
            const char* kinds[] = { "Klip", "Blend tree 1D" };
            int kind = (int)st.Type;
            if (ImGui::Combo("##type", &kind, kinds, 2))
            {
                st.Type = (AnimatorStateType)kind;
                if (st.IsBlendTree() && st.Motions.empty())
                {
                    // Starts from the state's clip (and the next clips of the model).
                    if (!st.Clip.empty())
                        st.Motions.push_back({ st.Clip, 0.0f, 1.0f });
                    if (model)
                        for (const auto& a : model->GetAnimations())
                            if (st.Motions.size() < 2 && a.Name != st.Clip)
                                st.Motions.push_back({ a.Name, (float)st.Motions.size(), 1.0f });
                }
                if (st.IsBlendTree() && st.BlendParameter.empty())
                    for (const auto& p : c.Parameters)
                        if (p.Kind == AnimatorParameter::Type::Float)
                        {
                            st.BlendParameter = p.Name;
                            break;
                        }
                changed = true;
            }
            if (!st.IsBlendTree())
            {
                Field("Klip");
                changed |= ClipCombo("##clip", st.Clip, model, "(birinchi klip)");
            }
            else
            {
                Field("Parametr", "Qaysi float parametr motion'larni tanlaydi (chegaralari bo'yicha)");
                changed |= FloatParameterCombo("##bp", st.BlendParameter, c, "(tanlanmagan)");
                if (st.BlendParameter.empty())
                {
                    Field("");
                    ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  Float parametr tanlang");
                }
            }
            Field("Tezlik");
            changed |= ImGui::DragFloat("##spd", &st.Speed, 0.01f, -10.0f, 10.0f, "%.2f");
            Field("Tezlik x", "Ixtiyoriy: tezlik shu float parametrga ko'paytiriladi");
            changed |= FloatParameterCombo("##sp", st.SpeedParameter, c, "(yo'q)");
            Field("Takrorlash");
            changed |= ImGui::Checkbox("##loop", &st.Loop);
            Field("Boshlang'ich", "Holat mashinasi shu holatdan boshlanadi");
            bool isDefault = c.DefaultState == index;
            if (ImGui::Checkbox("##default", &isDefault) && isDefault)
            {
                c.DefaultState = index;
                changed = true;
            }
            ImGui::EndTable();
        }
        if (st.IsBlendTree())
        {
            // Motions: clip, threshold, its own speed; the live weight under each.
            ImGui::TextDisabled("Motion'lar (klip, chegara, tezlik)");
            const bool live = c.BlendTree == index && c.BlendWeights.size() == st.Motions.size();
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            int remove = -1;
            for (size_t i = 0; i < st.Motions.size(); i++)
            {
                AnimatorBlendMotion& m = st.Motions[i];
                ImGui::PushID((int)i + 100);
                const float button = ImGui::GetFrameHeight();
                const float avail = ImGui::GetContentRegionAvail().x - button - spacing * 3.0f;
                ImGui::SetNextItemWidth(avail * 0.46f);
                changed |= ClipCombo("##mclip", m.Clip, model);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(avail * 0.30f);
                changed |= ImGui::DragFloat("##th", &m.Threshold, 0.02f, 0.0f, 0.0f, "%.2f");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Chegara: parametr shu qiymatda bo'lganda faqat shu klip o'ynaydi");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(avail * 0.24f);
                changed |= ImGui::DragFloat("##ms", &m.Speed, 0.01f, 0.01f, 10.0f, "x%.2f");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip("Shu klipning daraxt ichidagi o'z tezligi");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_XMARK, ImVec2(button, 0)))
                    remove = (int)i;
                if (live)
                {
                    const float w = c.BlendWeights[i];
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, UI::AccentColor());
                    ImGui::ProgressBar(w, ImVec2(-FLT_MIN, 4.0f), "");
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }
            if (remove >= 0)
            {
                st.Motions.erase(st.Motions.begin() + remove);
                changed = true;
            }
            if (ImGui::SmallButton(ICON_FA_PLUS "  Motion"))
            {
                AnimatorBlendMotion m;
                m.Threshold = st.Motions.empty() ? 0.0f : st.Motions.back().Threshold + 1.0f;
                if (model)
                    for (const auto& a : model->GetAnimations())
                    {
                        bool used = false;
                        for (const auto& other : st.Motions)
                            used |= other.Clip == a.Name;
                        if (!used)
                        {
                            m.Clip = a.Name;
                            break;
                        }
                    }
                st.Motions.push_back(m);
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_ARROW_DOWN_WIDE_SHORT "  Tartiblash"))
            {
                std::stable_sort(st.Motions.begin(), st.Motions.end(),
                                 [](const AnimatorBlendMotion& a, const AnimatorBlendMotion& b) { return a.Threshold < b.Threshold; });
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Motion'larni chegara bo'yicha tartiblash (natija o'zgarmaydi, ko'rinish uchun)");
        }
        ImGui::PopID();
        return changed;
    }

    bool DrawTransition(AnimatorComponent& c, int index)
    {
        if (index < 0 || index >= (int)c.Transitions.size())
            return false;
        bool changed = false;
        AnimatorTransition& tr = c.Transitions[(size_t)index];
        ImGui::PushID(index + 5000);
        if (BeginFields("##transition"))
        {
            Field("Qayerdan");
            if (ImGui::BeginCombo("##from", StateName(c, tr.From).c_str()))
            {
                for (int k = -1; k < (int)c.States.size(); k++)
                    if (ImGui::Selectable(StateName(c, k).c_str(), k == tr.From))
                    {
                        tr.From = k;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            Field("Qayerga");
            if (ImGui::BeginCombo("##to", StateName(c, tr.To).c_str()))
            {
                for (int k = 0; k < (int)c.States.size(); k++)
                    if (ImGui::Selectable(StateName(c, k).c_str(), k == tr.To))
                    {
                        tr.To = k;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            Field("O'tish vaqti", "Crossfade davomiyligi, soniya (0 - darhol)");
            changed |= ImGui::DragFloat("##fade", &tr.Fade, 0.01f, 0.0f, 5.0f, "%.2f s");
            Field("Exit time", "Yoqilgan bo'lsa, o'tish faqat klip shu nuqtaga (normalizatsiyalangan vaqt: 1 = oxiri) yetgandan keyin");
            changed |= ImGui::Checkbox("##hasexit", &tr.HasExitTime);
            if (tr.HasExitTime)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::DragFloat("##exit", &tr.ExitTime, 0.01f, 0.0f, 10.0f, "%.2f");
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled(tr.Conditions.empty() ? (tr.HasExitTime ? "Shartsiz: exit time'da o'tadi" : "Shartsiz: darhol o'tadi")
                                                  : "Shartlar (hammasi bajarilsa)");
        const char* ops[] = { ">", "<", "rost", "yolg'on", "trigger" };
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        int remove = -1;
        for (size_t k = 0; k < tr.Conditions.size(); k++)
        {
            auto& cd = tr.Conditions[k];
            ImGui::PushID((int)k);
            const float button = ImGui::GetFrameHeight();
            const float avail = ImGui::GetContentRegionAvail().x - button - spacing * 3.0f;
            const AnimatorParameter* param = c.FindParameter(cd.Parameter);
            ImGui::SetNextItemWidth(avail * 0.42f);
            if (ImGui::BeginCombo("##p", cd.Parameter.empty() ? "(parametr)" : cd.Parameter.c_str()))
            {
                for (auto& p : c.Parameters)
                    if (ImGui::Selectable(p.Name.c_str(), p.Name == cd.Parameter))
                    {
                        cd.Parameter = p.Name;
                        // The comparison that fits the parameter's type.
                        if (p.Kind == AnimatorParameter::Type::Trigger)
                            cd.Mode = AnimatorCondition::Op::Triggered;
                        else if (p.Kind == AnimatorParameter::Type::Bool && cd.Mode != AnimatorCondition::Op::IsFalse)
                            cd.Mode = AnimatorCondition::Op::IsTrue;
                        else if (p.Kind == AnimatorParameter::Type::Float && cd.Mode != AnimatorCondition::Op::Less)
                            cd.Mode = AnimatorCondition::Op::Greater;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            if (!cd.Parameter.empty() && !param && ImGui::IsItemHovered())
                UI::Tooltip("Bunday parametr yo'q");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(avail * 0.28f);
            int op = (int)cd.Mode;
            if (ImGui::Combo("##op", &op, ops, 5))
            {
                cd.Mode = (AnimatorCondition::Op)op;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(avail * 0.30f);
            if (cd.Mode == AnimatorCondition::Op::Greater || cd.Mode == AnimatorCondition::Op::Less)
                changed |= ImGui::DragFloat("##th", &cd.Threshold, 0.05f, 0.0f, 0.0f, "%.2f");
            else
                ImGui::Dummy(ImVec2(avail * 0.30f, ImGui::GetFrameHeight()));
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(button, 0)))
                remove = (int)k;
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            tr.Conditions.erase(tr.Conditions.begin() + remove);
            changed = true;
        }
        if (ImGui::SmallButton(ICON_FA_PLUS "  Shart"))
        {
            AnimatorCondition cd;
            if (!c.Parameters.empty())
            {
                cd.Parameter = c.Parameters.front().Name;
                cd.Mode = c.Parameters.front().Kind == AnimatorParameter::Type::Trigger ? AnimatorCondition::Op::Triggered
                        : c.Parameters.front().Kind == AnimatorParameter::Type::Bool    ? AnimatorCondition::Op::IsTrue
                                                                                        : AnimatorCondition::Op::Greater;
            }
            tr.Conditions.push_back(cd);
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    int AddState(AnimatorComponent& c, const Model* model, bool blendTree, const glm::vec2& position)
    {
        AnimatorState st;
        std::string base = blendTree ? "Blend" : "Holat";
        st.Name = base;
        for (int n = 1;; n++)
        {
            bool used = false;
            for (const auto& other : c.States)
                used |= other.Name == st.Name;
            if (!used)
                break;
            st.Name = std::format("{} {}", base, n);
        }
        st.GraphPosition = position;
        const auto* clips = model ? &model->GetAnimations() : nullptr;
        if (!blendTree)
        {
            if (clips && !clips->empty())
                st.Clip = (*clips)[std::min(c.States.size(), clips->size() - 1)].Name;
        }
        else
        {
            st.Type = AnimatorStateType::BlendTree1D;
            for (const auto& p : c.Parameters)
                if (p.Kind == AnimatorParameter::Type::Float)
                {
                    st.BlendParameter = p.Name;
                    break;
                }
            if (st.BlendParameter.empty())
            {
                // A tree needs a float: "Speed" (made when missing).
                if (!c.FindParameter("Speed"))
                    c.Parameters.push_back({ "Speed", AnimatorParameter::Type::Float, 0.0f });
                st.BlendParameter = "Speed";
            }
            if (clips)
                for (size_t i = 0; i < clips->size() && i < 3; i++)
                    st.Motions.push_back({ (*clips)[i].Name, (float)i, 1.0f });
        }
        c.States.push_back(std::move(st));
        if (c.States.size() == 1)
            c.DefaultState = 0;
        return (int)c.States.size() - 1;
    }

    void RemoveState(AnimatorComponent& c, int index)
    {
        if (index < 0 || index >= (int)c.States.size())
            return;
        c.States.erase(c.States.begin() + index);
        for (auto it = c.Transitions.begin(); it != c.Transitions.end();)
        {
            if (it->From == index || it->To == index)
            {
                it = c.Transitions.erase(it);
                continue;
            }
            if (it->From > index) it->From--;
            if (it->To > index) it->To--;
            ++it;
        }
        if (c.DefaultState == index)
            c.DefaultState = 0;
        else if (c.DefaultState > index)
            c.DefaultState--;
        c.DefaultState = std::clamp(c.DefaultState, 0, std::max(0, (int)c.States.size() - 1));
        // The preview / game enters a valid state again on its next update.
        c.CurrentState = -1;
        c.BlendTree = -1;
        c.PreviousBlendTree = -1;
        c.PreviousState = -1;
    }

    void DrawLiveStatus(const AnimatorComponent& c, const Model* model, bool grid)
    {
        auto row = [&](const char* label, const char* tooltip = nullptr) {
            if (grid)
                UI::PropertyLabel(label, tooltip);
            else
            {
                ImGui::TextDisabled("%s", label);
                ImGui::SameLine(96.0f);
                ImGui::SetNextItemWidth(-FLT_MIN);
            }
        };
        auto bar = [](float fraction, const std::string& text, const ImVec4& color) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
            ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f), text.c_str());
            ImGui::PopStyleColor();
        };
        const ImVec4 accent = UI::AccentColor(), fade(0.85f, 0.55f, 0.15f, 1.0f), weight(0.40f, 0.62f, 0.95f, 1.0f);
        const AnimatorState* tree = c.GetBlendTreeState();
        // What plays.
        row("Holat", "Hozir o'ynayotgan holat (holat mashinasi) yoki klip");
        if (c.UsesStateMachine() && c.CurrentState >= 0)
            ImGui::TextColored(accent, "%s  %s%s", tree ? ICON_FA_SHUFFLE : ICON_FA_PLAY, c.GetCurrentStateName(), tree ? "  (blend tree)" : "");
        else
            ImGui::TextColored(accent, ICON_FA_PLAY "  %s", c.Clip.empty() ? "(birinchi klip)" : c.Clip.c_str());
        // How far it is.
        const float normalized = c.GetNormalizedTime(model);
        const int cycle = (int)std::floor(std::max(0.0f, normalized));
        const float fraction = c.Loop ? normalized - std::floor(normalized) : std::clamp(normalized, 0.0f, 1.0f);
        row("Vaqt", "Normalizatsiyalangan vaqt: 0 - boshi, 1 - oxiri (takrorlanganda qaysi aylanish)");
        bar(fraction, c.Loop && cycle > 0 ? std::format("{:.2f}  ({}-aylanish)", fraction, cycle + 1) : std::format("{:.2f}", fraction), accent);
        // The crossfade.
        if (c.IsFading())
        {
            row("O'tish", "Crossfade: oldingi holatdan hozirgisiga");
            const std::string from = c.PreviousState >= 0 ? StateName(c, c.PreviousState)
                                   : c.PreviousBlendTree >= 0 ? StateName(c, c.PreviousBlendTree)
                                   : (c.PreviousClip.empty() ? std::string("?") : c.PreviousClip);
            const std::string to = c.UsesStateMachine() && c.CurrentState >= 0 ? std::string(c.GetCurrentStateName()) : c.Clip;
            ImGui::TextColored(fade, "%s  " ICON_FA_ARROW_RIGHT "  %s", from.c_str(), to.c_str());
            row("");
            bar(c.GetFadeProgress(), std::format("{:.0f}%  ({:.2f} / {:.2f} s)", c.GetFadeProgress() * 100.0f, c.FadeElapsed, c.FadeDuration), fade);
        }
        // A blend tree's weights.
        if (tree && c.BlendWeights.size() == tree->Motions.size())
            for (size_t i = 0; i < tree->Motions.size(); i++)
            {
                const auto& m = tree->Motions[i];
                std::string label = std::format("  {}", m.Clip.empty() ? "?" : m.Clip);
                row(label.c_str(), "Blend tree motion'ining og'irligi");
                bar(c.BlendWeights[i], std::format("{:.2f}  (chegara {:.2f})", c.BlendWeights[i], m.Threshold), weight);
            }
        // Layers.
        for (size_t i = 0; i < c.Layers.size(); i++)
        {
            const AnimatorLayer& l = c.Layers[i];
            std::string label = std::format(ICON_FA_LAYER_GROUP " {}", l.Name);
            row(label.c_str(), "Qatlamning amaldagi og'irligi (parametr va fade bilan)");
            bar(l.EffectiveWeight, std::format("{}  {:.2f}", l.Clip.empty() ? "(klip yo'q)" : l.Clip.c_str(), l.EffectiveWeight), weight);
        }
    }

}
