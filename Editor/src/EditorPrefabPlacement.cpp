// Placing prefabs from the content browser (EditorLayer): every prefab of a multi-selection dragged
// onto the viewport is placed, where the mouse is (on the mesh / collider under it, else on the
// ground plane - z = 0 in 2D), in a row along the camera when there are several, on the grid when
// snapping is on, with names of their own; Ctrl at the drop keeps the prefabs' saved places. Shift at
// the drop starts "prefab placing": every click in the viewport places the same prefabs again until
// Esc or a right click. Each drop / click is one undo step.
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>

#include <format>

namespace fs = std::filesystem;

namespace Zamin {

    std::vector<std::string> EditorLayer::GetDroppedPrefabs(const std::string& payload)
    {
        // The browser's multi-selection when the drag came from it (the payload is one of them).
        const std::string dropped = PrefabDatabase::NormalizePath(payload);
        std::vector<std::string> prefabs;
        bool payloadInDrag = false;
        for (const fs::path& path : m_ContentBrowserPanel.GetDraggedPaths())
        {
            if (AssetFiles::GetType(path) != AssetType::Prefab)
                continue;
            const std::string rel = PrefabDatabase::NormalizePath(Project::GetAssetRelativePath(path));
            payloadInDrag |= rel == dropped;
            if (std::find(prefabs.begin(), prefabs.end(), rel) == prefabs.end())
                prefabs.push_back(rel);
        }
        if (!payloadInDrag)
            prefabs = { dropped }; // dragged from somewhere else (the Scenes panel ...)
        return prefabs;
    }

    bool EditorLayer::PrefabDropPoint(glm::vec3& point)
    {
        // The mouse (automation: a point of the viewport given instead).
        const ImVec2 mouse = m_PrefabPlaceAt ? *m_PrefabPlaceAt : ImGui::GetMousePos();
        const glm::vec2 size = m_ViewportBounds[1] - m_ViewportBounds[0];
        if (size.x <= 0 || size.y <= 0)
            return false;
        const glm::vec2 ndc = { (mouse.x - m_ViewportBounds[0].x) / size.x * 2.0f - 1.0f, 1.0f - (mouse.y - m_ViewportBounds[0].y) / size.y * 2.0f };
        Ray ray = Ray::FromNDC(ndc, glm::inverse(m_EditorCamera.GetViewProjection()));
        if (ray.Direction == glm::vec3(0.0f))
            return false;
        ray.Direction = glm::normalize(ray.Direction);
        // On what is under the mouse (its box), else on the ground: y = 0 (2D view: z = 0).
        if (!m_2DMode && !m_PrefabPlaceAt)
            if (Entity hovered = m_HoveredEntity; hovered && hovered.IsValid() && hovered.GetScene() == m_EditorScene.get())
            {
                AABB box;
                if (m_EditorScene->ComputeBounds({ hovered }, box, false))
                    if (auto hit = Geometry::Raycast(ray, box))
                    {
                        point = hit->Point;
                        return true;
                    }
            }
        const Plane ground = m_2DMode ? Plane::FromPointNormal(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f))
                                      : Plane::FromPointNormal(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        if (auto hit = Geometry::Raycast(ray, ground, 1.0e5f))
        {
            point = hit->Point;
            return true;
        }
        if (auto hit = Geometry::Raycast(ray, ground.Flipped(), 1.0e5f))
        {
            point = hit->Point;
            return true;
        }
        // Looking away from the ground: where the camera looks.
        point = m_EditorCamera.GetFocalPoint();
        if (m_2DMode)
            point.z = 0.0f;
        return true;
    }

    std::vector<Entity> EditorLayer::PlacePrefabsAtMouse(const std::vector<std::string>& prefabs, bool keepSaved)
    {
        std::vector<Entity> roots;
        if (m_SceneState != SceneState::Edit || !m_EditorScene || prefabs.empty() || !Project::GetActive())
            return roots;
        CommitUndo(); // what was pending is its own step
        glm::vec3 point(0.0f);
        const bool atMouse = !keepSaved && PrefabDropPoint(point);
        for (const std::string& prefab : prefabs)
        {
            Entity root = PrefabUtility::InstantiateLinked(*m_EditorScene, m_PrefabDatabase, prefab, {});
            if (!root)
            {
                ZM_ERROR("Prefab yuklanmadi: {}", prefab);
                continue;
            }
            roots.push_back(root);
        }
        if (roots.empty())
            return roots;
        m_EditorScene->ResolveAssets(); // the meshes' models: their bounds decide where they stand

        // A UI element without a canvas of its own goes onto the scene's canvas.
        Entity canvas;
        for (auto handle : m_EditorScene->GetAllEntitiesWith<UICanvasComponent>())
        {
            Entity c(handle, m_EditorScene.get());
            if (std::find(roots.begin(), roots.end(), PrefabUtility::GetInstanceRoot(c)) == roots.end())
            {
                canvas = c;
                break;
            }
        }
        std::vector<Entity> world;
        for (Entity root : roots)
        {
            if (root.HasComponent<UITransformComponent>())
            {
                if (!root.HasComponent<UICanvasComponent>() && canvas)
                    m_EditorScene->SetParent(root, canvas, false);
                continue; // UI keeps its anchors and position
            }
            world.push_back(root);
        }

        if (atMouse && !world.empty())
        {
            // Sizes first: the row's spacing and where each one stands (its bottom on the point in
            // 3D, its centre in 2D).
            std::vector<AABB> boxes(world.size());
            std::vector<bool> sized(world.size(), false);
            std::vector<float> widths;
            for (size_t i = 0; i < world.size(); i++)
            {
                sized[i] = m_EditorScene->ComputeBounds({ world[i] }, boxes[i]);
                if (sized[i])
                    widths.push_back(m_2DMode ? boxes[i].Max.x - boxes[i].Min.x
                                              : std::max(boxes[i].Max.x - boxes[i].Min.x, boxes[i].Max.z - boxes[i].Min.z));
            }
            glm::vec3 right = m_2DMode ? glm::vec3(1.0f, 0.0f, 0.0f) : m_EditorCamera.GetRightDirection();
            if (!m_2DMode)
                right.y = 0.0f;
            const std::vector<glm::vec3> spots = PrefabPlacement::Layout(world.size(), point, right, PrefabPlacement::Spacing(widths, 1.0f));
            const bool snap = m_ViewSettings.Snap.Enabled && !ImGui::GetIO().KeyAlt;
            const float step = std::max(0.001f, m_ViewSettings.Snap.Translate);
            for (size_t i = 0; i < world.size(); i++)
            {
                auto& t = world[i].GetComponent<TransformComponent>();
                const glm::vec3 position = m_EditorScene->GetWorldPosition(world[i]);
                glm::vec3 anchor = position;
                if (sized[i])
                {
                    const glm::vec3 center = (boxes[i].Min + boxes[i].Max) * 0.5f;
                    anchor = m_2DMode ? glm::vec3(center.x, center.y, position.z) : glm::vec3(center.x, boxes[i].Min.y, center.z);
                }
                glm::vec3 target = spots[i];
                if (snap)
                {
                    target.x = std::round(target.x / step) * step;
                    if (m_2DMode)
                        target.y = std::round(target.y / step) * step;
                    else
                        target.z = std::round(target.z / step) * step;
                }
                if (m_2DMode)
                    target.z = anchor.z;
                t.Translation += target - anchor;
            }
        }

        // Names of their own ("Enemy (2)"): the root's name is the instance's, not an override.
        for (Entity root : roots)
            root.SetName(PrefabPlacement::UniqueName(root.GetName(), [&](std::string_view name) {
                for (Entity other : m_EditorScene->FindEntitiesByName(name))
                    if (other != root)
                        return true;
                return false;
            }));
        m_EditorScene->InvalidateNameCache();
        m_SceneHierarchyPanel.GetSelection().Set(roots, roots.front());
        const std::string first = roots.front().GetName();
        MarkDirty(roots.size() > 1 ? std::format("Prefab qo'shildi: {} \xc3\x97{}", first, roots.size()) : std::format("Prefab qo'shildi: {}", first));
        CommitUndo();
        return roots;
    }

    void EditorLayer::DropPrefabsOnViewport(const std::vector<std::string>& prefabs)
    {
        if (m_SceneState != SceneState::Edit)
            return;
        const ImGuiIO& io = ImGui::GetIO();
        const std::vector<Entity> placed = PlacePrefabsAtMouse(prefabs, io.KeyCtrl);
        if (placed.empty())
            return;
        if (io.KeyShift)
            StartPrefabPlacing(prefabs);
        else
            ShowViewportMessage(placed.size() > 1 ? std::format(ICON_FA_BOX "  {} {}", placed.size(), Tr("ta prefab qo'yildi"))
                                                  : std::format(ICON_FA_BOX "  {}: {}", Tr("Prefab qo'yildi"), placed.front().GetName()));
    }

    void EditorLayer::StartPrefabPlacing(const std::vector<std::string>& prefabs)
    {
        m_PrefabPlacing.Prefabs = prefabs;
        m_PrefabPlacing.Active = !prefabs.empty();
        m_PrefabPlacing.Placed = 0;
        ShowViewportMessage(std::format(ICON_FA_BOX "  {}", Tr("Prefab qo'yish rejimi: bosing - qo'yiladi, Esc yoki o'ng tugma - chiqish")));
    }

    bool EditorLayer::HandlePrefabPlacingClick()
    {
        if (!m_PrefabPlacing.Active)
            return false;
        if (m_SceneState != SceneState::Edit)
        {
            m_PrefabPlacing = {};
            return false;
        }
        const std::vector<Entity> placed = PlacePrefabsAtMouse(m_PrefabPlacing.Prefabs, false);
        m_PrefabPlacing.Placed += placed.size();
        return true;
    }

    void EditorLayer::UI_PrefabPlacing()
    {
        if (!m_PrefabPlacing.Active)
            return;
        if (m_SceneState != SceneState::Edit || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        {
            if (m_PrefabPlacing.Placed)
                ZM_INFO("Prefab qo'yish rejimi tugadi: {} ta qo'yildi", m_PrefabPlacing.Placed);
            m_PrefabPlacing = {};
            return;
        }
        // What a click places, next to the mouse.
        std::string names;
        for (size_t i = 0; i < m_PrefabPlacing.Prefabs.size() && i < 3; i++)
            names += (i ? ", " : "") + fs::path(m_PrefabPlacing.Prefabs[i]).stem().string();
        if (m_PrefabPlacing.Prefabs.size() > 3)
            names += std::format(" +{}", m_PrefabPlacing.Prefabs.size() - 3);
        const std::string title = std::format(ICON_FA_BOX "  {}: {}", Tr("Joylanadi"), names);
        const std::string hint = std::format("{}  ({} {})", Tr("Bosing: qo'yish  -  Esc / o'ng tugma: chiqish"), m_PrefabPlacing.Placed,
                                             Tr("ta qo'yildi"));
        ImVec2 at = m_PrefabPlaceAt ? *m_PrefabPlaceAt : ImGui::GetMousePos();
        if (!m_ViewportHovered && !m_PrefabPlaceAt)
            at = ImVec2(m_ViewportBounds[0].x + 12.0f, m_ViewportBounds[1].y - 60.0f);
        at.x += 18.0f;
        at.y += 22.0f;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImVec2 pad(8.0f, 5.0f);
        const ImVec2 titleSize = ImGui::CalcTextSize(title.c_str()), hintSize = ImGui::CalcTextSize(hint.c_str());
        const ImVec2 size(std::max(titleSize.x, hintSize.x) + pad.x * 2.0f, titleSize.y + hintSize.y + 3.0f + pad.y * 2.0f);
        at.x = std::min(at.x, m_ViewportBounds[1].x - size.x - 4.0f);
        at.y = std::min(at.y, m_ViewportBounds[1].y - size.y - 4.0f);
        const ImVec2 max(at.x + size.x, at.y + size.y);
        const ImU32 accent = IM_COL32(93, 169, 255, 255);
        dl->AddRectFilled(at, max, IM_COL32(24, 26, 32, 235), 5.0f);
        dl->AddRect(at, max, accent, 5.0f, 0, 1.5f);
        dl->AddRectFilled(at, ImVec2(at.x + 3.0f, max.y), accent, 5.0f, ImDrawFlags_RoundCornersLeft);
        dl->AddText(ImVec2(at.x + pad.x, at.y + pad.y), IM_COL32(235, 238, 245, 255), title.c_str());
        dl->AddText(ImVec2(at.x + pad.x, at.y + pad.y + titleSize.y + 3.0f), IM_COL32(160, 168, 182, 255), hint.c_str());
        // The point a click places at.
        glm::vec3 point;
        if ((m_ViewportHovered || m_PrefabPlaceAt) && PrefabDropPoint(point))
        {
            const glm::vec4 clip = OverlayViewProjection() * glm::vec4(point, 1.0f);
            if (clip.w > 0.0f)
            {
                const glm::vec2 ndc = glm::vec2(clip) / clip.w;
                const ImVec2 screen(m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * (m_ViewportBounds[1].x - m_ViewportBounds[0].x),
                                    m_ViewportBounds[0].y + (0.5f - ndc.y * 0.5f) * (m_ViewportBounds[1].y - m_ViewportBounds[0].y));
                dl->AddCircle(screen, 7.0f, accent, 0, 2.0f);
                dl->AddLine(ImVec2(screen.x - 11.0f, screen.y), ImVec2(screen.x + 11.0f, screen.y), accent, 1.5f);
                dl->AddLine(ImVec2(screen.x, screen.y - 11.0f), ImVec2(screen.x, screen.y + 11.0f), accent, 1.5f);
            }
        }
    }

}
