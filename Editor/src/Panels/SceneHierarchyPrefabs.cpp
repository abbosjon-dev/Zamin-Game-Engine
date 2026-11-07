// Prefab instances in the hierarchy and the inspector: the rows of linked entities (prefab colour,
// marks for added / changed entities and missing prefabs), the "Prefab" menu, the inspector's prefab
// bar with the list of changes, the marks on changed component headers and values, removed
// components. What the user picks becomes a PrefabRequest (EditorLayer runs it).
#include "SceneHierarchyPanel.h"
#include "../UI.h"

#include <filesystem>
#include <format>

namespace Zamin {

    namespace {
        const ImVec4 s_PrefabBlue(0.365f, 0.663f, 1.0f, 1.0f); // #5DA9FF
        const ImVec4 s_PrefabAdded(0.45f, 0.86f, 0.45f, 1.0f);
        const ImVec4 s_PrefabBroken(0.95f, 0.38f, 0.32f, 1.0f);

        std::string FileName(const std::string& path) { return std::filesystem::path(path).filename().string(); }

        // The component's header name (its scene-file key when the inspector has no other name for it).
        std::string ComponentName(std::string_view key)
        {
            const std::string_view name = SceneHierarchyPanel::GetComponentDisplayName(key);
            return name.empty() || name == "Komponent" ? std::string(key) : std::string(name);
        }

        // Why the instance's prefab cannot be followed ("" when it can).
        std::string PrefabProblem(PrefabDatabase* db, Entity root)
        {
            if (!db || !PrefabUtility::IsInstanceRoot(root))
                return {};
            const PrefabDatabase::Template* t = db->Get(PrefabUtility::GetPrefabPath(root));
            if (!t || !t->Readable)
                return t && !t->Issues.empty() ? t->Issues.front() : std::string(Tr("Prefab fayli topilmadi"));
            if (!t->Linkable)
                return std::format("{}: {}", Tr("Prefab bog'lanmaydi"), t->Issues.empty() ? std::string() : t->Issues.front());
            if (PrefabUtility::IsLegacyInstance(root))
                return Tr("Prefabga bog'lanmagan (eski nusxa)");
            for (const std::string& issue : t->Issues)
                if (issue.find("sikli") != std::string::npos)
                    return issue;
            return {};
        }

        size_t CountChanges(Entity root)
        {
            return PrefabUtility::IsInstanceRoot(root) ? PrefabUtility::GetOverrides(root).size() : 0;
        }

        // The name of a prefab entity the instance deleted (from the prefab).
        std::string RemovedName(PrefabDatabase* db, Entity root, uint64_t local)
        {
            if (const PrefabDatabase::Template* t = db ? db->Get(PrefabUtility::GetPrefabPath(root)) : nullptr; t && t->Entities)
                if (Entity e = t->Entities->GetEntityByUUID(UUID(local)))
                    return e.GetName();
            return std::format("#{}", local);
        }
    }

    void SceneHierarchyPanel::RequestPrefab(PrefabRequest::Op op, Entity entity, PrefabScope scope, std::string path, std::string target)
    {
        if (!OnPrefabRequest || !entity || !entity.IsValid())
            return;
        PrefabRequest request;
        request.Operation = op;
        request.Entity = entity.GetUUID();
        request.Scope = scope;
        request.Path = std::move(path);
        request.Target = std::move(target);
        OnPrefabRequest(request);
    }

    bool SceneHierarchyPanel::CanReparent(Entity entity, Entity newParent) const
    {
        if (!m_SceneEditable || !entity)
            return true;
        Entity root = PrefabUtility::GetInstanceRoot(entity);
        if (!root || root == entity || !PrefabUtility::IsLinked(entity))
            return true; // an instance root, an added entity: anywhere
        // An entity of the prefab stays in its instance (it would lose its link).
        return newParent && PrefabUtility::GetInstanceRoot(newParent) == root;
    }

    bool SceneHierarchyPanel::GetPrefabRowColor(Entity entity, ImVec4& color)
    {
        if (!entity.HasComponent<PrefabLinkComponent>() && !entity.HasComponent<EditorMetadataComponent>())
            return false;
        if (PrefabUtility::IsInstanceRoot(entity) || PrefabUtility::IsLinked(entity))
        {
            color = s_PrefabBlue;
            return true;
        }
        return false;
    }

    void SceneHierarchyPanel::DrawPrefabRowMarks(Entity entity, const ImVec2& rowMin, const ImVec2& rowMax)
    {
        Entity root = PrefabUtility::GetInstanceRoot(entity);
        if (!root)
            return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float iconWidth = ImGui::CalcTextSize(ICON_FA_POWER_OFF).x;
        float x = rowMax.x - 6.0f - (entity.IsActive() ? 0.0f : iconWidth + 6.0f);
        const float midY = (rowMin.y + rowMax.y) * 0.5f;
        auto hovered = [&](float x0, float x1) {
            return ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(x0, rowMin.y), ImVec2(x1, rowMax.y));
        };
        auto mark = [&](const char* text, const ImVec4& color, const std::string& tip) {
            const ImVec2 size = ImGui::CalcTextSize(text);
            x -= size.x;
            dl->AddText(ImVec2(x, midY - size.y * 0.5f), ImGui::GetColorU32(color), text);
            if (hovered(x, x + size.x))
                UI::Tooltip(tip.c_str());
            x -= 5.0f;
        };
        if (entity == root)
        {
            if (const std::string problem = PrefabProblem(m_Prefabs, root); !problem.empty())
                mark(ICON_FA_TRIANGLE_EXCLAMATION, s_PrefabBroken, std::format("{}\n{}", PrefabUtility::GetPrefabPath(root), problem));
        }
        if (PrefabUtility::IsAdded(entity))
            mark(ICON_FA_PLUS, s_PrefabAdded, Tr("Prefab nusxasiga qo'shilgan (prefabda yo'q): Apply bilan prefabga qo'shiladi"));
        // The root of a prefab nested in the instance's prefab.
        if (const uint64_t local = entity != root && m_Prefabs ? PrefabUtility::GetPrefabEntity(entity) : 0)
            if (const PrefabDatabase::Template* t = m_Prefabs->Get(PrefabUtility::GetPrefabPath(root)))
                if (auto nested = t->NestedOwner.find(local); nested != t->NestedOwner.end() && nested->second.Owner == local)
                    mark(ICON_FA_CUBES, ImVec4(0.55f, 0.72f, 0.95f, 0.85f),
                         std::format("{}: {}", Tr("Ichma-ich prefab"), nested->second.Prefab));
        if (PrefabUtility::HasOverrides(entity))
        {
            const auto& link = entity.GetComponent<PrefabLinkComponent>();
            x -= 4.0f;
            dl->AddCircleFilled(ImVec2(x, midY), 3.0f, UI::AccentU32());
            if (hovered(x - 5.0f, x + 5.0f))
            {
                std::string tip = Tr("Prefabdan farqli:");
                for (const std::string& p : link.Overrides)
                    tip += "\n  " + p;
                for (const std::string& k : link.RemovedComponents)
                    tip += std::format("\n  - {}", k);
                if (!link.RemovedEntities.empty())
                    tip += std::format("\n  {} {}", link.RemovedEntities.size(), Tr("ta obyekt o'chirilgan"));
                UI::Tooltip(tip.c_str());
            }
        }
    }

    void SceneHierarchyPanel::DrawPrefabMenu(Entity entity)
    {
        Entity root = PrefabUtility::GetInstanceRoot(entity);
        if (!root)
            return;
        if (!ImGui::BeginMenu(Tr(ICON_FA_BOX "  Prefab")))
            return;
        const std::string path = PrefabUtility::GetPrefabPath(root);
        ImGui::TextDisabled("%s", path.c_str());
        ImGui::Separator();
        const bool legacy = PrefabUtility::IsLegacyInstance(root);
        const bool editable = m_SceneEditable && !legacy && PrefabProblem(m_Prefabs, root).empty();
        const size_t changes = CountChanges(root);
        if (ImGui::MenuItem(Tr(ICON_FA_PEN_TO_SQUARE "  Prefab rejimida ochish"), nullptr, false, m_SceneEditable && OnOpenPrefabRequested))
            OnOpenPrefabRequested(path, PrefabUtility::GetPrefabEntity(entity));
        if (ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Kontent brauzerda ko'rsatish")))
            RequestPrefab(PrefabRequest::Op::ShowInBrowser, root);
        if (entity != root && ImGui::MenuItem(Tr(ICON_FA_ARROW_UP "  Nusxa ildizini tanlash")))
            SetSelectedEntity(root);
        ImGui::Separator();
        const std::string applyAll = std::format(ICON_FA_UPLOAD "  {}: {}", Tr("Hammasini qo'llash"), FileName(path));
        if (ImGui::MenuItem(applyAll.c_str(), nullptr, false, editable && changes > 0))
            RequestPrefab(PrefabRequest::Op::Apply, root);
        if (ImGui::MenuItem(Tr(ICON_FA_ROTATE_LEFT "  Hammasini qaytarish"), nullptr, false, editable && changes > 0))
            RequestPrefab(PrefabRequest::Op::Revert, root);
        if (entity != root)
        {
            if (PrefabUtility::IsAdded(entity))
            {
                if (ImGui::MenuItem(Tr(ICON_FA_UPLOAD "  Obyektni prefabga qo'shish"), nullptr, false, editable))
                    RequestPrefab(PrefabRequest::Op::Apply, entity, PrefabScope::Entity);
                if (ImGui::MenuItem(Tr(ICON_FA_TRASH "  Qo'shilganini olib tashlash"), nullptr, false, editable))
                    RequestPrefab(PrefabRequest::Op::Revert, entity, PrefabScope::Entity);
            }
            else
            {
                const bool own = PrefabUtility::HasOverrides(entity);
                if (ImGui::MenuItem(Tr(ICON_FA_UPLOAD "  Obyektni qo'llash"), nullptr, false, editable && own))
                    RequestPrefab(PrefabRequest::Op::Apply, entity, PrefabScope::Entity);
                if (ImGui::MenuItem(Tr(ICON_FA_ROTATE_LEFT "  Obyektni asliga qaytarish"), nullptr, false, editable && own))
                    RequestPrefab(PrefabRequest::Op::Revert, entity, PrefabScope::Entity);
            }
        }
        const auto* rootLink = root.TryGetComponent<PrefabLinkComponent>();
        const bool removed = rootLink && !rootLink->RemovedEntities.empty();
        if (ImGui::MenuItem(Tr(ICON_FA_ROTATE_RIGHT "  O'chirilganlarni tiklash"), nullptr, false, editable && removed))
            RequestPrefab(PrefabRequest::Op::RestoreRemoved, root);
        ImGui::Separator();
        if (ImGui::MenuItem(Tr(ICON_FA_LINK_SLASH "  Bog'lanishni uzish"), nullptr, false, m_SceneEditable))
            RequestPrefab(PrefabRequest::Op::Unpack, root);
        if (ImGui::IsItemHovered())
            UI::Tooltip(Tr("Nusxa oddiy obyektlarga aylanadi (prefab o'zgarsa ham o'zgarmaydi)"));
        if (ImGui::MenuItem(Tr(ICON_FA_LINK_SLASH "  To'liq ajratish"), nullptr, false, m_SceneEditable))
            RequestPrefab(PrefabRequest::Op::UnpackCompletely, root);
        if (ImGui::IsItemHovered())
            UI::Tooltip(Tr("Ichidagi boshqa prefab nusxalari ham ajratiladi"));
        ImGui::EndMenu();
    }

    void SceneHierarchyPanel::DrawPrefabBar(Entity entity)
    {
        Entity root = PrefabUtility::GetInstanceRoot(entity);
        if (!root)
            return;
        const std::string path = PrefabUtility::GetPrefabPath(root);
        const std::string problem = PrefabProblem(m_Prefabs, root);
        const bool editable = m_SceneEditable && problem.empty();
        const std::vector<PrefabOverrideInfo> changes = PrefabUtility::GetOverrides(root);

        ImGui::PushID("prefabBar");
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::Indent(6.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, problem.empty() ? s_PrefabBlue : s_PrefabBroken);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(problem.empty() ? ICON_FA_BOX : ICON_FA_TRIANGLE_EXCLAMATION);
        ImGui::SameLine();
        ImGui::Text("%s %s", Tr("Prefab:"), FileName(path).c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            UI::Tooltip(problem.empty() ? path.c_str() : std::format("{}\n{}", path, problem).c_str());
        if (entity != root)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s: %s)", Tr("nusxa"), root.GetName().c_str());
        }
        if (const uint64_t local = PrefabUtility::GetPrefabEntity(entity); local && m_Prefabs)
            if (const PrefabDatabase::Template* t = m_Prefabs->Get(path); t)
                if (auto nested = t->NestedOwner.find(local); nested != t->NestedOwner.end())
                {
                    // It comes from a prefab nested in this one: its values can go into that one too.
                    ImGui::TextColored(ImVec4(0.55f, 0.72f, 0.95f, 0.9f), ICON_FA_CUBES "  %s: %s", Tr("Ichma-ich prefabdan"),
                                       FileName(nested->second.Prefab).c_str());
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(std::format("{}\n{}", nested->second.Prefab,
                                                Tr("Qiymatni o'ng tugma bilan shu prefabga ham qo'llash mumkin")).c_str());
                }

        // [Tanlash] [Brauzerda] [O'zgarishlar (N)] [Qo'llash] [Qaytarish]
        if (entity != root)
        {
            if (UI::ToolbarButton(ICON_FA_ARROW_UP, Tr("Nusxa ildizini tanlash")))
                SetSelectedEntity(root);
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(!m_SceneEditable || !OnOpenPrefabRequested);
        if (UI::ToolbarButton(ICON_FA_PEN_TO_SQUARE, Tr("Prefab rejimida ochish (prefabni alohida tahrirlash)")) && OnOpenPrefabRequested)
            OnOpenPrefabRequested(path, PrefabUtility::GetPrefabEntity(entity));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_FOLDER_OPEN, Tr("Kontent brauzerda ko'rsatish")))
            RequestPrefab(PrefabRequest::Op::ShowInBrowser, root);
        const std::string changesLabel = std::format("{} ({}) " ICON_FA_CARET_DOWN "###changes", Tr("O'zgarishlar"), changes.size());
        UI::SameLineIfFits(changesLabel.c_str());
        ImGui::BeginDisabled(changes.empty());
        if (ImGui::Button(changesLabel.c_str()) || (m_OpenPrefabChanges && !changes.empty()))
            ImGui::OpenPopup("##prefabChanges");
        m_OpenPrefabChanges = false;
        ImGui::EndDisabled();
        UI::SameLineIfFits(Tr(ICON_FA_UPLOAD "  Qo'llash"));
        ImGui::BeginDisabled(!editable || changes.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor(1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::AccentMidColor());
        if (ImGui::Button(Tr(ICON_FA_UPLOAD "  Qo'llash")))
            RequestPrefab(PrefabRequest::Op::Apply, root);
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip(std::format("{} {}", Tr("Nusxaning barcha o'zgarishlarini prefabga yozish:"), FileName(path)).c_str());
        UI::SameLineIfFits(Tr(ICON_FA_ROTATE_LEFT "  Qaytarish"));
        if (ImGui::Button(Tr(ICON_FA_ROTATE_LEFT "  Qaytarish")))
            RequestPrefab(PrefabRequest::Op::Revert, root);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip(Tr("Hamma o'zgarishlarni bekor qilish: prefabdagi holatga qaytadi (qo'shilgan obyektlar o'chiriladi)"));
        ImGui::EndDisabled();

        if (ImGui::BeginPopup("##prefabChanges"))
        {
            ImGui::TextDisabled("%s", path.c_str());
            ImGui::Separator();
            UUID lastEntity = 0;
            bool first = true;
            int row = 0;
            for (const PrefabOverrideInfo& change : changes)
            {
                ImGui::PushID(row++);
                Entity e = change.Entity ? m_Context->GetEntityByUUID(change.Entity) : Entity();
                if (first || change.Entity != lastEntity)
                {
                    if (!first)
                        ImGui::Spacing();
                    const std::string name = e ? e.GetName() : RemovedName(m_Prefabs, root, change.LocalID);
                    ImGui::TextColored(s_PrefabBlue, "%s  %s", e ? ICON_FA_CUBE : ICON_FA_TRASH, name.c_str());
                    lastEntity = change.Entity;
                    first = false;
                }
                const char* kind = "";
                PrefabScope scope = PrefabScope::Field;
                std::string label;
                switch (change.Type)
                {
                    case PrefabOverrideInfo::Kind::Value: kind = ICON_FA_PEN; label = change.Path; break;
                    case PrefabOverrideInfo::Kind::AddedComponent:
                        kind = ICON_FA_PLUS;
                        label = std::format("{} ({})", ComponentName(change.Path).c_str(), Tr("qo'shilgan"));
                        break;
                    case PrefabOverrideInfo::Kind::RemovedComponent:
                        kind = ICON_FA_MINUS;
                        label = std::format("{} ({})", ComponentName(change.Path).c_str(), Tr("olib tashlangan"));
                        break;
                    case PrefabOverrideInfo::Kind::AddedEntity:
                        kind = ICON_FA_PLUS;
                        scope = PrefabScope::Entity;
                        label = Tr("qo'shilgan obyekt");
                        break;
                    case PrefabOverrideInfo::Kind::RemovedEntity:
                        kind = ICON_FA_MINUS;
                        label = Tr("o'chirilgan obyekt");
                        break;
                }
                ImGui::Indent(14.0f);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", kind);
                ImGui::SameLine();
                ImGui::TextUnformatted(label.c_str());
                const float buttons = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.x;
                ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f, ImGui::GetWindowContentRegionMax().x - buttons));
                ImGui::BeginDisabled(!editable);
                if (change.Type == PrefabOverrideInfo::Kind::RemovedEntity)
                {
                    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), 0.0f));
                    ImGui::SameLine();
                    if (UI::ToolbarButton(ICON_FA_ROTATE_LEFT, Tr("O'chirilganlarni tiklash")))
                        RequestPrefab(PrefabRequest::Op::RestoreRemoved, root);
                }
                else if (e)
                {
                    if (UI::ToolbarButton(ICON_FA_UPLOAD, Tr("Prefabga qo'llash")))
                        RequestPrefab(PrefabRequest::Op::Apply, e, scope, change.Path);
                    ImGui::SameLine();
                    if (UI::ToolbarButton(ICON_FA_ROTATE_LEFT, Tr("Asliga qaytarish")))
                        RequestPrefab(PrefabRequest::Op::Revert, e, scope, change.Path);
                }
                ImGui::EndDisabled();
                ImGui::Unindent(14.0f);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::Unindent(6.0f);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        const ImVec2 end(start.x + width, ImGui::GetCursorScreenPos().y);
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(start, end, ImGui::GetColorU32(ImVec4(0.20f, 0.36f, 0.58f, 0.18f)), 4.0f);
        dl->AddRectFilled(start, ImVec2(start.x + 3.0f, end.y), ImGui::GetColorU32(problem.empty() ? s_PrefabBlue : s_PrefabBroken), 2.0f);
        dl->ChannelsMerge();
        ImGui::PopID();
    }

    void SceneHierarchyPanel::DrawRemovedPrefabComponents(Entity entity)
    {
        const auto* link = entity.TryGetComponent<PrefabLinkComponent>();
        if (!link || !link->PrefabEntity || link->RemovedComponents.empty())
            return;
        const std::vector<std::string> removed = link->RemovedComponents;
        const bool editable = m_SceneEditable;
        for (const std::string& key : removed)
        {
            ImGui::PushID(key.c_str());
            ImGui::Spacing();
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const float height = ImGui::GetFontSize() + 12.0f;
            const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + height);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRect(min, max, ImGui::GetColorU32(ImVec4(0.55f, 0.58f, 0.64f, 0.45f)), 3.0f);
            dl->AddRectFilled(ImVec2(min.x - 4.0f, min.y), ImVec2(min.x - 1.0f, max.y), UI::AccentU32());
            ImGui::SetCursorScreenPos(ImVec2(min.x + 8.0f, min.y + (height - ImGui::GetFrameHeight()) * 0.5f));
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(ICON_FA_MINUS "  (%s) %s", Tr("olib tashlangan"), ComponentName(key).c_str());
            const float buttons = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.x + 6.0f;
            ImGui::SameLine(max.x - min.x - buttons);
            ImGui::BeginDisabled(!editable);
            if (UI::ToolbarButton(ICON_FA_ROTATE_LEFT, Tr("Qaytarish: komponent prefabdagidek qaytadi")))
                RequestPrefab(PrefabRequest::Op::Revert, entity, PrefabScope::Field, key);
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_UPLOAD, Tr("Olib tashlashni qo'llash: prefabdan ham olib tashlanadi")))
                RequestPrefab(PrefabRequest::Op::Apply, entity, PrefabScope::Field, key);
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            ImGui::PopID();
        }
    }

    void SceneHierarchyPanel::DrawPrefabSectionMarks(Entity entity, std::string_view key, const ImVec2& headerMin, const ImVec2& headerMax)
    {
        const auto* link = entity.TryGetComponent<PrefabLinkComponent>();
        if (!link || !link->PrefabEntity)
            return;
        std::vector<std::string> paths;
        bool added = false;
        for (const std::string& p : link->Overrides)
            if (PrefabOverrides::RootKey(p) == key)
            {
                paths.push_back(p);
                added = added || p == key;
            }
        if (paths.empty())
            return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(headerMin.x - 4.0f, headerMin.y), ImVec2(headerMin.x - 1.0f, headerMax.y), UI::AccentU32(), 1.0f);
        if (added)
        {
            const char* badge = ICON_FA_PLUS;
            const ImVec2 size = ImGui::CalcTextSize(badge);
            const float x = headerMax.x - (headerMax.y - headerMin.y) * 1.9f - size.x;
            dl->AddText(ImVec2(x, (headerMin.y + headerMax.y - size.y) * 0.5f), ImGui::GetColorU32(s_PrefabAdded), badge);
        }
        if (ImGui::IsMouseHoveringRect(headerMin, headerMax) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive())
        {
            std::string tip = added ? std::string(Tr("Nusxaga qo'shilgan komponent (prefabda yo'q)")) : std::string(Tr("Prefabdan farqli qiymatlar:"));
            if (!added)
                for (const std::string& p : paths)
                    tip += "\n  " + p;
            UI::Tooltip(tip.c_str());
        }
    }

    void SceneHierarchyPanel::DrawPrefabComponentMenuItems(Entity entity, std::string_view key)
    {
        const auto* link = entity.TryGetComponent<PrefabLinkComponent>();
        if (!link || !link->PrefabEntity || !PrefabUtility::GetInstanceRoot(entity))
            return;
        bool changed = false;
        for (const std::string& p : link->Overrides)
            changed = changed || PrefabOverrides::RootKey(p) == key;
        ImGui::Separator();
        const bool editable = m_SceneEditable && changed;
        const std::vector<std::string> targets = m_Prefabs ? PrefabUtility::GetApplyTargets(entity, *m_Prefabs) : std::vector<std::string>();
        if (targets.size() > 1)
        {
            if (ImGui::BeginMenu(Tr(ICON_FA_UPLOAD "  Komponentni prefabga qo'llash"), editable))
            {
                for (size_t i = 0; i < targets.size(); i++)
                    if (ImGui::MenuItem(FileName(targets[i]).c_str()))
                        RequestPrefab(PrefabRequest::Op::Apply, entity, PrefabScope::Component, std::string(key), i == 0 ? std::string() : targets[i]);
                ImGui::EndMenu();
            }
        }
        else if (ImGui::MenuItem(Tr(ICON_FA_UPLOAD "  Komponentni prefabga qo'llash"), nullptr, false, editable))
            RequestPrefab(PrefabRequest::Op::Apply, entity, PrefabScope::Component, std::string(key));
        if (ImGui::MenuItem(Tr(ICON_FA_ROTATE_LEFT "  Komponentni asliga qaytarish"), nullptr, false, editable))
            RequestPrefab(PrefabRequest::Op::Revert, entity, PrefabScope::Component, std::string(key));
    }

}
