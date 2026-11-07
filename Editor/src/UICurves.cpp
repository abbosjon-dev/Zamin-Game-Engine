// Inspector widgets for values over a particle's life and over time: colour gradients and curves
// (ParticleCurve, AnimationCurve).
#include "UI.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin::UI {

    static ImU32 ToU32(const glm::vec4& c)
    {
        glm::vec4 v = glm::clamp(c, glm::vec4(0.0f), glm::vec4(1.0f));
        return ImGui::ColorConvertFloat4ToU32(ImVec4(v.r, v.g, v.b, v.a));
    }

    // A grey checkerboard behind see-through colours.
    static void DrawCheckerboard(ImDrawList* dl, ImVec2 min, ImVec2 max, float cell)
    {
        dl->AddRectFilled(min, max, IM_COL32(150, 150, 150, 255));
        dl->PushClipRect(min, max, true);
        int row = 0;
        for (float y = min.y; y < max.y; y += cell, row++)
            for (float x = min.x + ((row & 1) ? cell : 0.0f); x < max.x; x += cell * 2.0f)
                dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, max.x), std::min(y + cell, max.y)), IM_COL32(95, 95, 95, 255));
        dl->PopClipRect();
    }

    bool GradientEditor(const char* id, ParticleGradient& g)
    {
        ImGui::PushID(id);
        bool changed = false;
        if (g.Stops.empty())
        {
            g.Stops.push_back({ 0.0f, glm::vec4(1.0f) });
            changed = true;
        }
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID selectedKey = ImGui::GetID("##selected"), dragKey = ImGui::GetID("##drag");
        int selected = std::clamp(storage->GetInt(selectedKey, 0), 0, (int)g.Stops.size() - 1);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float width = std::max(60.0f, ImGui::GetContentRegionAvail().x);
        const float barHeight = ImGui::GetFrameHeight();
        const float markerHeight = std::round(ImGui::GetFontSize() * 0.8f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##gradient", ImVec2(width, barHeight + markerHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        auto xOf = [&](float t) { return origin.x + std::clamp(t, 0.0f, 1.0f) * width; };
        const ImVec2 barMax(origin.x + width, origin.y + barHeight);

        // The bar: segment by segment between the stops (Fixed: blocks of one colour).
        DrawCheckerboard(dl, origin, barMax, barHeight * 0.5f);
        float previousX = origin.x;
        glm::vec4 previous = g.Stops.front().Color;
        for (const auto& stop : g.Stops)
        {
            const float x = xOf(stop.Time);
            if (x > previousX)
            {
                if (g.Fixed)
                    dl->AddRectFilled(ImVec2(previousX, origin.y), ImVec2(x, barMax.y), ToU32(previous));
                else
                    dl->AddRectFilledMultiColor(ImVec2(previousX, origin.y), ImVec2(x, barMax.y), ToU32(previous), ToU32(stop.Color),
                                                ToU32(stop.Color), ToU32(previous));
            }
            previousX = x;
            previous = stop.Color;
        }
        if (barMax.x > previousX)
            dl->AddRectFilled(ImVec2(previousX, origin.y), barMax, ToU32(previous));
        dl->AddRect(origin, barMax, IM_COL32(20, 22, 26, 255));

        // The markers: a little house under the bar for each stop, filled with its colour (opaque).
        const float half = markerHeight * 0.45f;
        for (int i = 0; i < (int)g.Stops.size(); i++)
        {
            const float x = xOf(g.Stops[i].Time);
            const float top = barMax.y + 1.0f;
            ImVec2 tip(x, top), left(x - half, top + half), right(x + half, top + half);
            ImVec2 boxMin(x - half, top + half), boxMax(x + half, top + markerHeight - 1.0f);
            const ImU32 outline = i == selected ? AccentU32() : IM_COL32(200, 205, 210, 255);
            glm::vec4 opaque = g.Stops[i].Color;
            opaque.a = 1.0f;
            dl->AddTriangleFilled(tip, right, left, outline);
            dl->AddRectFilled(boxMin, boxMax, ToU32(opaque));
            dl->AddRect(boxMin, boxMax, outline, 0.0f, 0, i == selected ? 2.0f : 1.0f);
        }

        // The stop under the mouse (by its marker's x, anywhere over the widget).
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        auto stopAt = [&]() {
            int best = -1;
            float bestDistance = half + 3.0f;
            for (int i = 0; i < (int)g.Stops.size(); i++)
            {
                const float d = std::abs(mouse.x - xOf(g.Stops[i].Time));
                if (d <= bestDistance && mouse.y >= barMax.y - 2.0f)
                {
                    best = i;
                    bestDistance = d;
                }
            }
            return best;
        };
        const float mouseT = std::clamp((mouse.x - origin.x) / width, 0.0f, 1.0f);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const int hit = stopAt();
            if (hit >= 0)
            {
                selected = hit;
                storage->SetInt(dragKey, 1);
            }
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && stopAt() < 0)
        {
            selected = g.AddStop(mouseT);
            changed = true;
        }
        if (active && storage->GetInt(dragKey, 0) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f) && g.Stops[selected].Time != mouseT)
        {
            // Moved past a neighbour: the stops are sorted again and the selection follows it.
            ParticleGradient::Stop moved = g.Stops[selected];
            moved.Time = mouseT;
            g.Stops[selected] = moved;
            g.Sort();
            selected = (int)(std::find(g.Stops.begin(), g.Stops.end(), moved) - g.Stops.begin());
            changed = true;
        }
        if (!active)
            storage->SetInt(dragKey, 0);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            const int hit = stopAt();
            if (hit >= 0 && g.Stops.size() > 1)
            {
                g.Stops.erase(g.Stops.begin() + hit);
                selected = std::min(selected, (int)g.Stops.size() - 1);
                changed = true;
            }
        }
        if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const glm::vec4 c = g.Evaluate(mouseT);
            Tooltip(std::format("{:.0f}%: ({:.2f}, {:.2f}, {:.2f}, {:.2f})\nIkki marta bosish - yangi nuqta, nuqtani sudrash - surish, "
                                   "o'ng tugma - o'chirish", mouseT * 100.0f, c.r, c.g, c.b, c.a).c_str());
        }

        // The selected stop: colour, position, remove; and blended / fixed.
        selected = std::clamp(selected, 0, (int)g.Stops.size() - 1);
        auto& stop = g.Stops[selected];
        changed |= ImGui::ColorEdit4("##color", &stop.Color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar |
                                                                ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_Float);
        ImGui::SameLine();
        const float buttonWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f));
        float percent = stop.Time * 100.0f;
        if (ImGui::DragFloat("##time", &percent, 0.5f, 0.0f, 100.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp))
        {
            ParticleGradient::Stop moved = stop;
            moved.Time = percent / 100.0f;
            stop = moved;
            g.Sort();
            selected = (int)(std::find(g.Stops.begin(), g.Stops.end(), moved) - g.Stops.begin());
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            Tooltip("Tanlangan nuqtaning o'rni (umrning foizi)");
        ImGui::SameLine();
        if (ImGui::Selectable(g.Fixed ? ICON_FA_WAVE_SQUARE : ICON_FA_CHART_LINE, false, 0, ImVec2(buttonWidth - 8.0f, 0.0f)))
        {
            g.Fixed = !g.Fixed;
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            Tooltip(g.Fixed ? "Pog'onali: har nuqtaning rangi keyingisigacha turadi (bosing - silliq)"
                               : "Silliq: nuqtalar orasida aralashadi (bosing - pog'onali)");
        ImGui::SameLine();
        ImGui::BeginDisabled(g.Stops.size() <= 1);
        if (ImGui::Button(ICON_FA_TRASH, ImVec2(buttonWidth, 0.0f)))
        {
            g.Stops.erase(g.Stops.begin() + selected);
            selected = std::min(selected, (int)g.Stops.size() - 1);
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            Tooltip("Tanlangan nuqtani o'chirish");

        storage->SetInt(selectedKey, selected);
        ImGui::PopID();
        return changed;
    }

    // ------------------------------------------------------------------ curves
    static const char* const s_Interpolations[] = { "Pog'onali", "Chiziqli", "Silliq (Bezier)" };

    // The editor of a key list over [t0, t1] (the view's time span): ParticleCurve (0..1, shown as
    // % of the life) and AnimationCurve (seconds) share it. `emptyValue`: the value a curve without
    // keys starts from.
    static bool KeyCurveEditor(std::vector<AnimationKey>& keys, float t0, float t1, bool percent, float valueMin, float valueMax,
                               float emptyValue)
    {
        bool changed = false;
        if (keys.empty())
        {
            AnimationKey a;
            a.Time = t0;
            a.Value.x = emptyValue;
            AnimationKey b = a;
            b.Time = t1;
            keys = { a, b };
            changed = true;
        }
        if (!(t1 > t0))
            t1 = t0 + 1.0f;
        const float span = t1 - t0;
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID selectedKey = ImGui::GetID("##selected"), dragKey = ImGui::GetID("##drag");
        int selected = std::clamp(storage->GetInt(selectedKey, 0), 0, (int)keys.size() - 1);

        // The view: at least valueMin..valueMax, more when the curve goes beyond.
        float curveMin, curveMax;
        CurveKeys::GetRange(keys, t0, t1, curveMin, curveMax);
        float bottom = std::min(valueMin, curveMin), top = std::max(valueMax, curveMax);
        if (top - bottom < 1e-3f)
            top = bottom + 1.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float width = std::max(60.0f, ImGui::GetContentRegionAvail().x);
        const float height = std::round(ImGui::GetFontSize() * 4.2f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 max(origin.x + width, origin.y + height);
        ImGui::InvisibleButton("##curve", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        const float pad = 5.0f; // keys at the edges stay clickable
        auto toScreen = [&](float t, float v) {
            return ImVec2(origin.x + pad + (t - t0) / span * (width - pad * 2.0f), max.y - pad - (v - bottom) / (top - bottom) * (height - pad * 2.0f));
        };
        auto fromScreen = [&](ImVec2 p, float& t, float& v) {
            t = t0 + std::clamp((p.x - origin.x - pad) / (width - pad * 2.0f), 0.0f, 1.0f) * span;
            v = std::clamp(bottom + (max.y - pad - p.y) / (height - pad * 2.0f) * (top - bottom), bottom, top);
        };
        auto timeText = [&](float t) { return percent ? std::format("{:.0f}%", (t - t0) / span * 100.0f) : std::format("{:.2f} s", t); };

        dl->AddRectFilled(origin, max, IM_COL32(24, 26, 31, 255), 3.0f);
        for (int i = 1; i < 4; i++)
        {
            const float x = origin.x + pad + i * 0.25f * (width - pad * 2.0f);
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, max.y), IM_COL32(255, 255, 255, 14));
        }
        if (bottom < 0.0f && top > 0.0f)
        {
            const float y = toScreen(t0, 0.0f).y;
            dl->AddLine(ImVec2(origin.x, y), ImVec2(max.x, y), IM_COL32(255, 255, 255, 40));
        }
        const std::string topText = std::format("{:.2g}", top), bottomText = std::format("{:.2g}", bottom);
        dl->AddText(ImVec2(origin.x + 3.0f, origin.y + 1.0f), IM_COL32(150, 155, 165, 160), topText.c_str());
        dl->AddText(ImVec2(origin.x + 3.0f, max.y - ImGui::GetFontSize() - 1.0f), IM_COL32(150, 155, 165, 160), bottomText.c_str());
        if (!percent)
        {
            const std::string endText = std::format("{:.2g} s", t1);
            const ImVec2 size = ImGui::CalcTextSize(endText.c_str());
            dl->AddText(ImVec2(max.x - size.x - 3.0f, max.y - ImGui::GetFontSize() - 1.0f), IM_COL32(150, 155, 165, 160), endText.c_str());
        }

        // The curve itself, then its keys.
        constexpr int samples = 64;
        ImVec2 points[samples + 1];
        for (int i = 0; i <= samples; i++)
        {
            const float t = t0 + span * (i / (float)samples);
            points[i] = toScreen(t, CurveKeys::EvaluateScalar(keys, t));
        }
        dl->PushClipRect(origin, max, true);
        dl->AddPolyline(points, samples + 1, AccentU32(), 0, 2.0f);
        for (int i = 0; i < (int)keys.size(); i++)
        {
            const AnimationKey& k = keys[i];
            const ImVec2 p = toScreen(k.Time, k.Value.x);
            dl->AddCircleFilled(p, i == selected ? 5.0f : 4.0f, i == selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 205, 210, 255));
            if (i == selected)
                dl->AddCircle(p, 6.5f, AccentU32(), 0, 1.5f);
        }
        dl->PopClipRect();
        dl->AddRect(origin, max, IM_COL32(60, 64, 72, 255), 3.0f);

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        auto keyAt = [&]() {
            int best = -1;
            float bestDistance = 8.0f * 8.0f;
            for (int i = 0; i < (int)keys.size(); i++)
            {
                const ImVec2 p = toScreen(keys[i].Time, keys[i].Value.x);
                const float d = (p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y);
                if (d <= bestDistance)
                {
                    best = i;
                    bestDistance = d;
                }
            }
            return best;
        };
        float mouseT, mouseV;
        fromScreen(mouse, mouseT, mouseV);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const int hit = keyAt();
            if (hit >= 0)
            {
                selected = hit;
                storage->SetInt(dragKey, 1);
            }
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && keyAt() < 0)
        {
            selected = CurveKeys::Insert(keys, mouseT);
            keys[selected].Value.x = mouseV;
            CurveKeys::UpdateTangents(keys);
            changed = true;
        }
        if (active && storage->GetInt(dragKey, 0) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
        {
            // Between its neighbours: the keys keep their order.
            AnimationKey& k = keys[selected];
            const float gap = span * 1e-3f;
            const float lo = selected > 0 ? keys[selected - 1].Time + gap : t0;
            const float hi = selected + 1 < (int)keys.size() ? keys[selected + 1].Time - gap : t1;
            const float t = std::clamp(mouseT, std::min(lo, hi), std::max(lo, hi));
            if (t != k.Time || mouseV != k.Value.x)
            {
                k.Time = t;
                k.Value.x = mouseV;
                CurveKeys::UpdateTangents(keys);
                changed = true;
            }
        }
        if (!active)
            storage->SetInt(dragKey, 0);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            const int hit = keyAt();
            if (hit >= 0)
            {
                selected = hit;
                ImGui::OpenPopup("##key");
            }
            else
                ImGui::OpenPopup("##shapes");
        }
        if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsPopupOpen("##key") && !ImGui::IsPopupOpen("##shapes"))
            Tooltip(std::format("{}: {:.3f}\nIkki marta bosish - yangi kalit, kalitni sudrash - surish, o'ng tugma - menyu",
                                   timeText(mouseT), CurveKeys::EvaluateScalar(keys, mouseT)).c_str());

        if (ImGui::BeginPopup("##key"))
        {
            selected = std::clamp(selected, 0, (int)keys.size() - 1);
            AnimationKey& k = keys[selected];
            ImGui::TextDisabled("Keyingi kalitgacha:");
            for (int i = 0; i < 3; i++)
                if (ImGui::MenuItem(s_Interpolations[i], nullptr, (int)k.Interpolation == i))
                {
                    k.Interpolation = (KeyInterpolation)i;
                    changed = true;
                }
            if (k.Interpolation == KeyInterpolation::Bezier && ImGui::MenuItem(Tr("Tekis (ease)"), nullptr, k.TangentMode == KeyTangentMode::Flat))
            {
                k.TangentMode = k.TangentMode == KeyTangentMode::Flat ? KeyTangentMode::Auto : KeyTangentMode::Flat;
                CurveKeys::UpdateTangents(keys);
                changed = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_TRASH "  O'chirish"), nullptr, false, keys.size() > 1))
            {
                keys.erase(keys.begin() + selected);
                selected = std::min(selected, (int)keys.size() - 1);
                CurveKeys::UpdateTangents(keys);
                changed = true;
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("##shapes"))
        {
            ImGui::TextDisabled("Tayyor shakllar:");
            const float hi = std::max(valueMax, valueMin + 1e-3f), lo = valueMin;
            // Keys at the ends of the view (and one in the middle), linear or smooth.
            auto make = [&](std::initializer_list<std::pair<float, float>> points, bool smooth) {
                std::vector<AnimationKey> out;
                for (const auto& [x, v] : points)
                {
                    AnimationKey key;
                    key.Time = t0 + span * x;
                    key.Value.x = v;
                    key.Interpolation = smooth ? KeyInterpolation::Bezier : KeyInterpolation::Linear;
                    out.push_back(key);
                }
                CurveKeys::Sort(out);
                return out;
            };
            auto shape = [&](const char* name, std::vector<AnimationKey> k) {
                if (ImGui::MenuItem(name))
                {
                    keys = std::move(k);
                    selected = 0;
                    changed = true;
                }
            };
            shape("Doimiy", make({ { 0.0f, hi }, { 1.0f, hi } }, false));
            shape("Kamayish (chiziqli)", make({ { 0.0f, hi }, { 1.0f, lo } }, false));
            shape("O'sish (chiziqli)", make({ { 0.0f, lo }, { 1.0f, hi } }, false));
            shape("Kamayish (silliq)", make({ { 0.0f, hi }, { 1.0f, lo } }, true));
            shape("O'sish (silliq)", make({ { 0.0f, lo }, { 1.0f, hi } }, true));
            shape("Ko'tarilib tushish", make({ { 0.0f, lo }, { 0.5f, hi }, { 1.0f, lo } }, true));
            ImGui::EndPopup();
        }

        // The selected key: time, value, interpolation.
        selected = std::clamp(selected, 0, (int)keys.size() - 1);
        AnimationKey& k = keys[selected];
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float third = std::max(30.0f, (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f);
        ImGui::SetNextItemWidth(third);
        if (percent)
        {
            float value = (k.Time - t0) / span * 100.0f;
            const float lo = selected > 0 ? (keys[selected - 1].Time - t0) / span * 100.0f + 0.1f : 0.0f;
            const float hi = selected + 1 < (int)keys.size() ? (keys[selected + 1].Time - t0) / span * 100.0f - 0.1f : 100.0f;
            if (ImGui::DragFloat("##t", &value, 0.5f, std::min(lo, hi), std::max(lo, hi), "%.1f%%", ImGuiSliderFlags_AlwaysClamp))
            {
                k.Time = t0 + value / 100.0f * span;
                CurveKeys::UpdateTangents(keys);
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                Tooltip("Tanlangan kalitning vaqti (umrning foizi)");
        }
        else
        {
            // Seconds: the first / last key may move outwards (the view follows).
            float value = k.Time;
            const float gap = span * 1e-3f;
            const float lo = selected > 0 ? keys[selected - 1].Time + gap : -1e6f;
            const float hi = selected + 1 < (int)keys.size() ? keys[selected + 1].Time - gap : 1e6f;
            if (ImGui::DragFloat("##t", &value, std::max(0.001f, span * 0.005f), std::min(lo, hi), std::max(lo, hi), "%.3f s", ImGuiSliderFlags_AlwaysClamp))
            {
                k.Time = value;
                CurveKeys::UpdateTangents(keys);
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                Tooltip("Tanlangan kalitning vaqti (soniya)");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(third);
        if (ImGui::DragFloat("##v", &k.Value.x, 0.01f, 0.0f, 0.0f, "%.3f"))
        {
            CurveKeys::UpdateTangents(keys);
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            Tooltip("Tanlangan kalitning qiymati");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        int interpolation = (int)k.Interpolation;
        if (ImGui::Combo("##i", &interpolation, s_Interpolations, 3))
        {
            k.Interpolation = (KeyInterpolation)interpolation;
            CurveKeys::UpdateTangents(keys);
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            Tooltip("Tanlangan kalitdan keyingisigacha qanday o'tadi");

        storage->SetInt(selectedKey, selected);
        return changed;
    }

    bool CurveEditor(const char* id, ParticleCurve& curve, float valueMin, float valueMax)
    {
        ImGui::PushID(id);
        const bool changed = KeyCurveEditor(curve.Keys, 0.0f, 1.0f, true, valueMin, valueMax, 1.0f);
        ImGui::PopID();
        return changed;
    }

    bool CurveEditor(const char* id, AnimationCurve& curve, float valueMin, float valueMax)
    {
        ImGui::PushID(id);
        // The view spans the keys (a curve of one key or none: a second).
        float t0 = curve.GetStartTime(), t1 = curve.GetEndTime();
        if (curve.Keys.size() < 2 || !(t1 - t0 > 1e-4f))
            t1 = t0 + 1.0f;
        bool changed = KeyCurveEditor(curve.Keys, t0, t1, false, valueMin, valueMax, 0.0f);

        // Before the first / after the last key.
        static const char* const wraps[] = { "Ushlab turish", "Takrorlash", "Borib-qaytish" };
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float half = std::max(40.0f, (ImGui::GetContentRegionAvail().x - spacing) * 0.5f);
        ImGui::SetNextItemWidth(half);
        int pre = (int)curve.PreWrap;
        if (ImGui::Combo("##pre", &pre, wraps, 3))
        {
            curve.PreWrap = (AnimationWrap)pre;
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            Tooltip("Birinchi kalitdan oldin: ushlab turish (birinchi qiymat), takrorlash yoki borib-qaytish");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        int post = (int)curve.PostWrap;
        if (ImGui::Combo("##post", &post, wraps, 3))
        {
            curve.PostWrap = (AnimationWrap)post;
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            Tooltip("Oxirgi kalitdan keyin: ushlab turish (oxirgi qiymat), takrorlash yoki borib-qaytish");
        ImGui::PopID();
        return changed;
    }

    bool PropertyGradient(const char* label, ParticleGradient& gradient, const char* tooltip)
    {
        PropertyLabel(label, tooltip);
        return GradientEditor(label, gradient);
    }

    bool PropertyCurve(const char* label, ParticleCurve& curve, float valueMin, float valueMax, const char* tooltip)
    {
        PropertyLabel(label, tooltip);
        return CurveEditor(label, curve, valueMin, valueMax);
    }

    bool PropertyCurve(const char* label, AnimationCurve& curve, float valueMin, float valueMax, const char* tooltip)
    {
        PropertyLabel(label, tooltip);
        return CurveEditor(label, curve, valueMin, valueMax);
    }

}
