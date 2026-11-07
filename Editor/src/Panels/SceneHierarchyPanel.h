#pragma once

#include <Zamin/Zamin.h>
#include <Zamin/Scene/ParticlePresets.h>

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_map>

namespace Zamin {

    class AssetInspectorPanel;

    // What the hierarchy / inspector asks of a prefab instance (EditorLayer runs it after the panel
    // has drawn: the scene may change under it). The entity is held by UUID.
    struct PrefabRequest
    {
        enum class Op : uint8_t { Apply, Revert, RestoreRemoved, Unpack, UnpackCompletely, ShowInBrowser };
        Op Operation = Op::Apply;
        UUID Entity = 0;
        PrefabScope Scope = PrefabScope::Instance;
        std::string Path;     // the component key / the override path (Component / Field)
        std::string Target;   // the prefab to apply to ("": the instance's own)
    };

    // Hierarchy (entity list) + Inspector (component editor).
    class SceneHierarchyPanel
    {
    public:
        // The Inspector shows this panel's asset (a file picked in the content browser) instead of
        // the entities until the entity selection changes (AcknowledgeSelection: picked now).
        void SetAssetInspector(AssetInspectorPanel* panel) { m_AssetInspector = panel; }
        void AcknowledgeSelection() { m_SeenSelectionRevision = m_Selection.GetRevision(); }

        SceneHierarchyPanel() = default;

        // keepSelection: the same entities (by UUID) stay selected in the new scene (play mode, undo).
        void SetContext(const Ref<Scene>& scene, bool keepSelection = false);
        // Play mode: the game's scene manager (null otherwise). With more than one scene loaded the
        // hierarchy lists every loaded scene (the active one marked, DontDestroyOnLoad apart): the
        // shown scene's tree as usual, the others folded below it (read-only); a click on one of them
        // asks for it to be shown (OnRuntimeSceneSelected).
        void SetSceneManager(SceneManager* manager) { m_SceneManager = manager; }
        std::function<void(const Ref<Scene>&)> OnRuntimeSceneSelected;
        void OnImGuiRender(bool* showHierarchy, bool* showInspector);
        void DrawHierarchy();
        void DrawInspector();

        // ---- selection (Ctrl+click toggles, Shift+click selects a range of rows)
        // The primary entity (the last one picked) is what the inspector shows; the gizmo
        // pivot, focus, rename, tile painting... use it.
        Entity GetSelectedEntity() const { return m_Selection.GetPrimary(); }
        // Replaces the selection with this one entity (an invalid entity clears it).
        void SetSelectedEntity(Entity entity) { m_Selection.Select(entity); }
        const EntitySelection& GetSelection() const { return m_Selection; }
        EntitySelection& GetSelection() { return m_Selection; }
        size_t GetSelectionCount() const { return m_Selection.Size(); }
        bool IsSelected(Entity entity) const { return m_Selection.Contains(entity); }
        // Selected entities without those inside another selected subtree, in hierarchy order:
        // what delete / duplicate / copy / move / the gizmo work on.
        std::vector<Entity> GetSelectedRoots() const;
        // A click outside the hierarchy (viewport): Ctrl toggles, Shift adds, otherwise only this one.
        void SelectWithModifiers(Entity entity, bool ctrl, bool shift);
        void SelectAll();

        // Duplicates / deletes every selected subtree (one undo step for the caller).
        // The copies become the new selection. skipLocked: only the subtrees the gizmo can
        // move (Alt + drag / Ctrl+D while dragging leave locked ones alone).
        size_t DuplicateSelection(bool skipLocked = false);
        size_t DeleteSelection();

        // ---- eye / lock buttons (editor-only flags saved with the scene, see EditorMetadataComponent)
        // H: hides the selection, or shows it again when the primary entity is hidden. Returns how
        // many entities changed (0 = nothing selected / not editable); `hidden` = the new state.
        size_t ToggleSelectionHidden(bool* hidden = nullptr);
        size_t ToggleSelectionLocked(bool* locked = nullptr);
        size_t ShowAll();     // clears every hidden flag of the scene
        size_t UnlockAll();   // clears every lock
        // ---- active state (TagComponent::Active: game state saved with the scene, see Scene::SetActive)
        // Alt+Shift+A: switches the selection off, or on again when the primary entity is off.
        // Works while playing too (the running scene reacts at once: OnDisable / OnEnable, bodies).
        // Returns how many entities were targeted (0 = nothing selected); `active` = the new state.
        size_t ToggleSelectionActive(bool* active = nullptr);
        // Edit mode only: eye / lock toggles, component reordering and pasting.
        void SetSceneEditable(bool editable) { m_SceneEditable = editable; }

        // Inspector headers' open / collapsed state (owned by the editor layer, saved per user).
        void SetFoldState(InspectorFoldState* state) { m_FoldState = state; }

        // Hierarchy search text ("#tag" filters by gameplay tag).
        void SetFilter(const std::string& filter) { m_Filter = filter; }
        // Keeps the inspector scrolled to its end for a few frames (screenshots / automation).
        void ScrollInspectorToBottom() { m_ScrollInspectorFrames = 30; m_ScrollInspectorY = -1.0f; }
        // Automation: the inspector scrolled `y` px down (at most to the bottom).
        void ScrollInspectorTo(float y) { m_ScrollInspectorFrames = 30; m_ScrollInspectorY = y; }
        // Automation: the Mesh Renderer's "Standart material..." list opens (selected entity).
        void OpenStandardMaterialPicker() { m_OpenStandardMaterials = true; }

        // Row icon of an entity (by its main component) and the inspector header name of a
        // component key (status bar, menus).
        static const char* GetEntityIcon(Entity entity);
        static const char* GetComponentDisplayName(std::string_view key);

        // Entity creation shortcuts (also used by the editor menu).
        Entity CreateEntityPreset(const char* preset);
        // ---- particle presets (SceneHierarchyParticles.cpp)
        // A preset to apply once the panel has drawn: onto `entity`'s Particle System (replacing its
        // settings, templates as children), or onto a new entity when `entity` is null.
        void RequestParticlePreset(Entity entity, ParticlePreset preset);
        // The same from a .zparticle file (path relative to Assets); false when it cannot be read.
        bool RequestParticlePresetFile(Entity entity, const std::filesystem::path& assetPath);
        // The requested preset applied (selected, scene modified); the entity, or none.
        Entity ApplyPendingParticlePreset();
        // OnOpenSceneRequested / OnRuntimeSceneSelected asked for while drawing (after both panels).
        void ApplyPendingSceneRequests();
        // `entity`'s Particle System as Assets/Particles/<name>.zparticle (sub-emitter templates in it).
        bool SaveParticlePreset(Entity entity);
        // The built-ins and the project's presets as menu items (`entity` null: each makes a new entity).
        bool DrawParticlePresetItems(Entity entity);
        static const char* GetParticlePresetIcon(std::string_view key);
        void DrawCreateMenu();

        // Invalidate cached script defaults (after module reload).
        void OnScriptsReloaded() { m_ScriptDefaults.clear(); }
        bool IsHierarchyFocused() const { return m_HierarchyFocused; }

        // Tilemap painting (used by the viewport)
        enum class TileTool { Brush = 0, Rectangle, Fill, Eraser, Picker };
        bool IsTilePainting() const
        {
            Entity e = GetSelectedEntity();
            // Hidden / locked tilemaps are not painted in the viewport.
            return m_TilePaint && e && e.HasComponent<TilemapComponent>() &&
                   !e.GetScene()->IsHiddenInEditor(e) && !e.GetScene()->IsLockedInEditor(e);
        }
        void SetTilePaint(bool paint) { m_TilePaint = paint; }

        // Collider points edited in the viewport: the selected entity's Polygon / Edge collider 2D
        // (None when it has no such collider or is hidden / locked).
        enum class ColliderPointKind { None = 0, Polygon, Edge };
        ColliderPointKind GetColliderPointEdit() const
        {
            Entity e = GetSelectedEntity();
            if (m_ColliderPointEdit == ColliderPointKind::None || !e || e.GetScene()->IsHiddenInEditor(e) || e.GetScene()->IsLockedInEditor(e))
                return ColliderPointKind::None;
            if (m_ColliderPointEdit == ColliderPointKind::Polygon && !e.HasComponent<PolygonCollider2DComponent>())
                return ColliderPointKind::None;
            if (m_ColliderPointEdit == ColliderPointKind::Edge && !e.HasComponent<EdgeCollider2DComponent>())
                return ColliderPointKind::None;
            return m_ColliderPointEdit;
        }
        void SetColliderPointEdit(ColliderPointKind kind)
        {
            m_ColliderPointEdit = kind;
            if (kind != ColliderPointKind::None)
            {
                m_JointAnchorEdit = m_Joint3DAnchorEdit = -1;
                m_SplinePointEdit = false;
            }
        }
        // The selected entity's Spline points edited in the viewport (false when it has no Spline
        // component or is hidden / locked).
        bool IsSplinePointEditing() const
        {
            Entity e = GetSelectedEntity();
            return m_SplinePointEdit && e && e.HasComponent<SplineComponent>() && !e.GetScene()->IsHiddenInEditor(e) &&
                   !e.GetScene()->IsLockedInEditor(e);
        }
        void SetSplinePointEdit(bool edit)
        {
            m_SplinePointEdit = edit;
            if (edit)
            {
                m_ColliderPointEdit = ColliderPointKind::None;
                m_JointAnchorEdit = m_Joint3DAnchorEdit = -1;
                m_TilePaint = false;
            }
        }
        // The 2D joint of the selected entity whose anchors / axis / limits are dragged in the viewport
        // (a Joint2DType), -1 when none (or the entity has no such joint, or is hidden / locked).
        int GetJointAnchorEdit() const
        {
            Entity e = GetSelectedEntity();
            if (m_JointAnchorEdit < 0 || !e || e.GetScene()->IsHiddenInEditor(e) || e.GetScene()->IsLockedInEditor(e))
                return -1;
            return HasJoint2D(e, (Joint2DType)m_JointAnchorEdit) ? m_JointAnchorEdit : -1;
        }
        void SetJointAnchorEdit(int type)
        {
            m_JointAnchorEdit = type;
            if (type >= 0)
            {
                m_ColliderPointEdit = ColliderPointKind::None;
                m_Joint3DAnchorEdit = -1;
                m_TilePaint = false;
                m_SplinePointEdit = false;
            }
        }
        // The same for a 3D joint of the selected entity (a Joint3DType), -1 when none.
        int GetJoint3DAnchorEdit() const
        {
            Entity e = GetSelectedEntity();
            if (m_Joint3DAnchorEdit < 0 || !e || e.GetScene()->IsHiddenInEditor(e) || e.GetScene()->IsLockedInEditor(e))
                return -1;
            return HasJoint3D(e, (Joint3DType)m_Joint3DAnchorEdit) ? m_Joint3DAnchorEdit : -1;
        }
        void SetJoint3DAnchorEdit(int type)
        {
            m_Joint3DAnchorEdit = type;
            if (type >= 0)
            {
                m_ColliderPointEdit = ColliderPointKind::None;
                m_JointAnchorEdit = -1;
                m_TilePaint = false;
                m_SplinePointEdit = false;
            }
        }
        static bool HasJoint3D(Entity e, Joint3DType type)
        {
            switch (type)
            {
                case Joint3DType::Fixed: return e.HasComponent<FixedJoint3DComponent>();
                case Joint3DType::Hinge: return e.HasComponent<HingeJoint3DComponent>();
                case Joint3DType::Distance: return e.HasComponent<DistanceJoint3DComponent>();
                case Joint3DType::Slider: return e.HasComponent<SliderJoint3DComponent>();
                case Joint3DType::Cone: return e.HasComponent<ConeJoint3DComponent>();
            }
            return false;
        }
        static bool HasJoint2D(Entity e, Joint2DType type)
        {
            switch (type)
            {
                case Joint2DType::Distance: return e.HasComponent<DistanceJoint2DComponent>();
                case Joint2DType::Revolute: return e.HasComponent<RevoluteJoint2DComponent>();
                case Joint2DType::Prismatic: return e.HasComponent<PrismaticJoint2DComponent>();
                case Joint2DType::Weld: return e.HasComponent<WeldJoint2DComponent>();
                case Joint2DType::Wheel: return e.HasComponent<WheelJoint2DComponent>();
            }
            return false;
        }
        // The points of a polygon collider follow the opaque outline of the entity's sprite (its
        // current frame); without a picture, the sprite's rectangle. False when it has no sprite.
        static bool FitPolygonToSprite(Entity entity, PolygonCollider2DComponent& c, int maxVertices = 12);
        // The brush's top-left tile (the tile picked in the palette).
        int GetSelectedTile() const { return m_TileBrush.GetTopLeft(); }
        // What the tools paint with: the picked tile, or a block of tiles dragged out in the palette.
        const TileBrush& GetTileBrush() const { return m_TileBrush; }
        void SetTileBrush(const TileBrush& brush);
        TileTool GetTileTool() const { return m_TileTool; }
        // Switches painting on with that tool (the picker remembers the tool to go back to).
        void SetTileTool(TileTool tool);
        // The tile layer painted on (clamped to the selected tilemap's layers by the caller).
        int GetActiveTileLayer() const { return m_TileLayer; }
        void SetActiveTileLayer(int layer) { m_TileLayer = std::max(0, layer); }
        // The selected tilemap's tileset auto-tile rule the tools paint with (-1: the selected tile).
        int GetSelectedAutoTileRule() const { return m_TileRule; }
        void SetSelectedAutoTileRule(int rule) { m_TileRule = rule; }
        // The selected rule's variant editor (open / closed); `variant`: the one palette clicks fill.
        void SetAutoTileRuleEditor(bool open, int variant = -1) { m_EditRule = open; m_EditRuleVariant = variant; }
        // The picker found `tile` (of auto-tile rule `rule`, -1 none) on the map: it becomes the brush.
        void PickTile(int tile, int rule = -1);
        // ---- tile palette window (Oyna > Plitka palitrasi): the selected tilemap's palette, bigger
        void DrawTilePaletteWindow(bool* open);
        // Automation: the palette zoom (0 = fit the width) of the inspector (0) / the window (1), and
        // the tile whose collision / animation the inspector edits.
        void SetTilePaletteZoom(int view, float zoom)
        {
            TilePaletteView& v = m_PaletteViews[std::clamp(view, 0, 1)];
            v.Zoom = zoom;
            v.AutoFit = zoom <= 0.0f;
        }
        void SetTilePropertiesOpen(bool open) { m_TilePropertiesOpen = open; }
        std::function<void()> OnTilePaletteWindowRequested;

        std::function<void(Entity)> OnFocusRequested;
        std::function<void()> OnSceneModified;
        std::function<void(Entity)> OnSavePrefabRequested;
        // An asset (absolute path) is to be edited in the inspector (a mesh renderer's material).
        std::function<void(const std::filesystem::path&)> OnAssetEditRequested;
        // Sub-scenes (SubSceneComponent): the content of `host`'s sub-scene to list under it (the
        // editor's preview / the loaded one in play mode; null: none), why there is none, and the
        // request to open a scene file (relative to the assets) for editing.
        std::function<Ref<Scene>(Entity host)> GetSubScene;
        std::function<std::string(Entity host)> GetSubSceneProblem;
        std::function<void(const std::filesystem::path&)> OnOpenSceneRequested;
        std::function<void(const std::filesystem::path&, Entity parent)> OnPrefabDropped;
        // A material asset (.zmat, path relative to Assets) was dropped onto an entity's row.
        std::function<void(const std::string&, Entity target)> OnMaterialDropped;
        // Files were written from the inspector (a model's materials): the content browser lists them.
        std::function<void(const std::vector<std::filesystem::path>&)> OnFilesCreated;
        // The content browser's thumbnail of an asset file (0 = none yet): shown in front of the
        // material fields (a sphere of the material).
        std::function<uint32_t(const std::filesystem::path&)> GetAssetThumbnail;
        // The project's tag list changed (a new tag was typed in the inspector).
        std::function<void()> OnProjectSettingsModified;
        // The fold state (SetFoldState) changed: the editor saves its view settings.
        std::function<void()> OnFoldStateModified;
        // The Sprite Editor is to open for a texture (path relative to Assets).
        std::function<void(const std::string&)> OnSpriteEditorRequested;
        // Project settings > Sorting layers is to open.
        std::function<void()> OnSortingLayersRequested;
        // The Audio Source's mixer button: the Audio Mixer window opens.
        std::function<void()> OnAudioMixerRequested;
        // Project settings > Physics layers is to open.
        std::function<void()> OnPhysicsLayersRequested;
        // The Animation window is to open on an entity's Animation Player (clip: index in its list).
        std::function<void(Entity, int)> OnAnimationWindowRequested;
        // The script inspector's "open the code" button: the class's source in the external editor.
        std::function<void(const std::string&)> OnOpenScriptSource;
        // The Animator graph window for this entity's state machine.
        std::function<void(Entity)> OnAnimatorGraphRequested;
        // Prefab instances: their links are shown (rows, the inspector's prefab bar, changed values);
        // Apply / Revert / Unpack go through OnPrefabRequest.
        void SetPrefabDatabase(PrefabDatabase* db) { m_Prefabs = db; }
        std::function<void(const PrefabRequest&)> OnPrefabRequest;
        // The inspector's list of the instance's changes opens (automation / screenshots).
        void OpenPrefabChanges() { m_OpenPrefabChanges = true; }
        // Prefab mode (the scene is a prefab opened on its own): references to entities that are not
        // in it point outside the prefab (shown as such, not as missing).
        void SetPrefabStageMode(bool stage) { m_PrefabStageMode = stage; }
        bool IsPrefabStageMode() const { return m_PrefabStageMode; }

    private:
        // Where a hierarchy row drop puts the dragged entities.
        enum class DropZone { None, Before, After, Into, FirstChild, RootEnd };
        struct PendingClick { Entity Target; bool Ctrl = false, Shift = false; };
        struct PendingDrop { UUID Dragged = 0; Entity Target; DropZone Zone = DropZone::None; };

        template<typename T>
        void DisplayAddComponentEntry(const char* name, const char* icon);
        // The Animation Player section: its clips (the Animation window opens them), speed, wrap.
        bool DrawAnimationPlayer(Entity entity, struct AnimationPlayerComponent& c);
        // The Material part of the Mesh Renderer section: the material asset (and per-submesh ones)
        // or the inline material. True when something changed.
        bool DrawMeshMaterial(Entity entity, struct MeshRendererComponent& c);
        // "Standart material...": a built-in material (MaterialPresets) as the mesh's material asset
        // - one shared file per project under Assets/Materials/Standart. True when it changed.
        bool DrawStandardMaterialPicker(struct MeshRendererComponent& c);
        // Models: the button that writes the model's glTF materials as .zmat files and gives each
        // part its own (Model::ExportMaterials). True when the slots changed.
        bool DrawExtractModelMaterials(struct MeshRendererComponent& c);
        // Inspector sections are collected first (AddSection) and then drawn in the entity's
        // component order (ComponentRegistry::GetOrderedKeys).
        struct InspectorSection { std::string_view Key; std::function<void()> Draw; };
        template<typename T, typename UIFunction>
        void AddSection(const char* icon, const std::string& name, Entity entity, UIFunction uiFunction,
                        bool removable, bool& modified, const std::function<bool(T&)>& settingsMenu = {});
        template<typename T, typename UIFunction>
        void DrawSection(const char* icon, const std::string& name, Entity entity, UIFunction& uiFunction,
                         bool removable, bool& modified, const std::function<bool(T&)>& settingsMenu);
        // Copy / paste values / move up-down items of a component's menu. True when it changed the scene.
        bool DrawComponentMenuItems(Entity entity, std::string_view key);
        // Header drag & drop: dropping a component header before / after another one reorders them.
        void DrawSectionDragDrop(std::string_view key);
        // Fold-state key of a header: the component key, scripts per class.
        static std::string FoldKey(Entity entity, std::string_view key);
        // Reads the component clipboard (the system clipboard first, then our own copy).
        void RefreshComponentClipboard();
        // Pastes the clipboard component onto the selected entities (the primary when not
        // selected): addMissing = also to those without that component. Returns how many changed.
        size_t PasteComponent(Entity entity, bool addMissing);
        // Entities a component menu acts on: the selection when `entity` is part of it.
        std::vector<Entity> GetMenuTargets(Entity entity) const;
        // Eye / lock buttons left of a hierarchy row.
        void DrawRowToggles(Entity entity, const ImVec2& rowMin, const ImVec2& rowMax);
        // Sets the flag on `entity`, or on the whole selection when `entity` is part of it.
        size_t SetFlagFromRow(Entity entity, bool lockFlag, bool value);
        // The same for the active flag (allowed while playing). Returns how many were targeted.
        size_t SetActiveFromRow(Entity entity, bool active);
        // Active checkbox of the inspector header and the "inactive" note under the name.
        void DrawActiveToggle(Entity entity);
        void DrawInactiveNote(Entity entity);
        void ApplyPendingClick();
        void ApplyPendingDrop();
        // Accepts a hierarchy entity dropped on the row drawn last (before / after it, or into it).
        void DrawRowDropTarget(Entity entity, bool open, bool hasChildren);
        // Transform header menu: reset / copy / paste (applied to every selected entity).
        bool DrawTransformMenu(Entity entity);
        bool PeekTransformClipboard(TransformComponent& out);

        void DrawComponents(Entity entity);
        void DrawEntityNode(Entity entity, int depth);
        void DrawEntityContextMenu(Entity entity);
        // ---- prefab instances (SceneHierarchyPrefabs.cpp)
        void RequestPrefab(PrefabRequest::Op op, Entity entity, PrefabScope scope = PrefabScope::Instance, std::string path = {},
                           std::string target = {});
        // The "Prefab" submenu of a row's context menu.
        void DrawPrefabMenu(Entity entity);
        // The row's prefab marks (a changed / added entity) at its right end; the label colour.
        void DrawPrefabRowMarks(Entity entity, const ImVec2& rowMin, const ImVec2& rowMax);
        bool GetPrefabRowColor(Entity entity, ImVec4& color);
        // The bar under the inspector's name: the prefab, its changes, Apply / Revert.
        void DrawPrefabBar(Entity entity);
        // The prefab components the instance removed, as ghost headers (Revert / apply the removal).
        void DrawRemovedPrefabComponents(Entity entity);
        // A component header of an instance: the changed-values bar, the added badge, its menu items.
        void DrawPrefabSectionMarks(Entity entity, std::string_view key, const ImVec2& headerMin, const ImVec2& headerMax);
        void DrawPrefabComponentMenuItems(Entity entity, std::string_view key);
        // Whether `entity` may move below `newParent` (an entity of a prefab stays in its instance).
        bool CanReparent(Entity entity, Entity newParent) const;
        void DrawScriptComponent(Entity entity, bool& modified);
        // One script field with its attributes (header, label, tooltip, range, color, enum).
        bool DrawScriptField(const ScriptFieldInfo& field, ScriptFieldValue& value);
        // EntityRef script field: drop from the hierarchy, pick from a list, locate, clear.
        bool DrawEntityRefField(const char* name, EntityRef& ref, const char* tooltip = nullptr);
        bool DrawBoneAttachment(Entity entity, BoneAttachmentComponent& c);
        bool DrawFootsteps(Entity entity, FootstepsComponent& c);
        bool DrawSubScene(Entity entity, SubSceneComponent& c);
        // The read-only rows of `host`'s sub-scene under it in the hierarchy.
        void DrawSubSceneRows(Entity host, const Ref<Scene>& sub);
        // Gameplay tags: removable chips + add field with suggestions.
        bool DrawTagsComponent(TagsComponent& c);
        // 2D renderers: the sorting layer (from the project's list) and the order inside it.
        bool DrawSortingLayer(uint32_t& layer, int& order);
        // Audio Source: the mixer group it plays in (from the mixer's groups) + a button to the mixer.
        bool DrawAudioOutput(std::string& output);
        // The project's default 3D distances / distance model (Project settings > Audio) under a 3D
        // sound's distances, with a button that puts them back to the defaults.
        bool DrawSpatialDefaultsRow(float& minDistance, float& maxDistance);
        // A Text component's localization key: typed or picked from the language files, with its text now.
        bool DrawLocalizationKey(TextComponent& c);
        std::string m_LocalizationKeyFilter;
        // Audio Source while the game runs: playing / paused / stopped, the time with a progress bar
        // (click: jump there), what is heard after the random variation, the mixer and the distance,
        // the pitch with its variation and Doppler; Play / Pause / Stop buttons.
        void DrawAudioSourceState(Entity entity, AudioSourceComponent& c);
        // Sprite Renderer: texture, sprite sheet frame, draw mode (Simple / Sliced / Tiled) ...
        bool DrawSpriteRenderer(Entity entity, SpriteRendererComponent& c);
        // Sprite Animation: grid cells or a list of sprite sheet frames.
        bool DrawSpriteAnimation(Entity entity, SpriteAnimationComponent& c);
        // UI Image: texture, sprite sheet frame, type (Simple / Sliced / Tiled / Filled) and its settings.
        bool DrawUIImage(Entity entity, UIImageComponent& c);
        // Camera 2D Follow: target, damping, dead zone, look-ahead, bounds.
        bool DrawCamera2DFollow(Entity entity, Camera2DFollowComponent& c);
        bool DrawSpline(Entity entity, SplineComponent& c);
        bool DrawSplineFollower(Entity entity, SplineFollowerComponent& c);
        // Navigation: the grids' settings, their bake button and what they hold; the agent's settings
        // and, while playing, its path.
        bool DrawNavGrid2D(Entity entity, NavGrid2DComponent& c);
        bool DrawNavGrid3D(Entity entity, NavGrid3DComponent& c);
        bool DrawNavAgent(Entity entity, NavAgentComponent& c);
        bool DrawNavObstacle(Entity entity, NavObstacleComponent& c);
        bool DrawSightSensor(Entity entity, SightSensorComponent& c);
        bool DrawPatrolPath(Entity entity, PatrolPathComponent& c);
        bool DrawFormation(Entity entity, FormationComponent& c);
        // 2D joints: the connected body (with what keeps the joint from being made), the anchors and
        // the break limits; the type's own settings follow in its section, then the live read-outs.
        // `type`: which joint it is (the viewport editing toggle).
        bool DrawJoint2DCommon(Entity entity, Joint2DComponentBase& c, Joint2DType type);
        // A mask of the project's 2D collision layers: a check box per named layer (and one for the
        // unnamed ones together), "all" / "none".
        bool DrawLayerMask(const char* label, uint16_t& mask, const char* tooltip);
        // What an area / buoyancy effector needs: a Rigidbody 2D with colliders that have an area
        // (fluid: triggers). Notes / warnings, nothing to edit.
        void DrawEffectorNotes(Entity entity, bool fluid);
        // A 2D collision layer of the project (inherit: -1 = "the Rigidbody 2D's" is offered too).
        bool DrawCollisionLayer(const char* label, int& layer, bool inherit, const char* tooltip);
        // A physics material asset (.zphysmat) field: open / create one from `own` values.
        // `active` is set when the path names a material that loaded (its values replace the own ones).
        // threeD: a 3D collider's (the material's density is not used: the Rigidbody 3D has the mass).
        bool DrawPhysicsMaterialField(Entity entity, std::string& path, const PhysicsMaterialData& own, bool& active, bool threeD = false);
        // The points of a polygon / edge collider: a list (drag, add, remove) and the viewport edit toggle.
        bool DrawColliderPoints(std::vector<glm::vec2>& points, ColliderPointKind kind, int minPoints);
        void DrawJoint2DStatus(const Joint2DComponentBase& c, const char* readout);
        // 3D joints: the same as the 2D ones (connected body and what keeps the joint from being made,
        // anchors, collisions, break limits) and their live read-outs.
        bool DrawJoint3DCommon(Entity entity, Joint3DComponentBase& c, Joint3DType type);
        void DrawJoint3DStatus(const Joint3DComponentBase& c, const char* readout);
        // Convex Hull Collider 3D: the source (Mesh Renderer / model file), the corner limit and what
        // the hull came out as.
        bool DrawConvexHullCollider(Entity entity, ConvexHullCollider3DComponent& c);
        bool DrawMeshCollider(Entity entity, MeshCollider3DComponent& c);
        bool DrawTintAnimation(Entity entity, TintAnimationComponent& c);
        bool DrawLight2D(Entity entity, Light2DComponent& c);
        bool DrawPixelPerfect(CameraComponent& c);
        // Particle System: playback, emission, shape, motion, look and the optional modules
        // (colour / size / velocity over lifetime, speed limit, texture sheet) - SceneHierarchyParticles.cpp.
        bool DrawParticleSystem(Entity entity, ParticleSystemComponent& c);
        bool DrawParticleBursts(ParticleSystemComponent& c);
        // The row above its modules: play / pause, restart, stop, count, presets, the module filter.
        void DrawParticleToolbar(Entity entity, ParticleSystemComponent& c);
        // The preset library menu (built-ins, the project's .zparticle files, Save as preset).
        void DrawParticlePresetMenu(Entity entity);
        // A child entity with a Particle System that emits nothing itself, made a Death sub-emitter of `owner`'s.
        void CreateSubEmitterTemplate(UUID owner);
        // Tilemap: tileset, layers, paint tools, auto-tile rules, palette.
        bool DrawTilemap(Entity entity, TilemapComponent& c);
        // Writes the auto-tile rules into the tileset's .zmeta.
        bool SaveAutoTiles(const std::string& tilesetPath, const AutoTileSet& set);
        // Writes the per-tile collision / animations into the tileset's .zmeta.
        bool SaveTileProperties(const std::string& tilesetPath, const TilesetProperties& properties);
        // How a palette is scrolled / zoomed and what is being dragged in it (inspector, window).
        struct TilePaletteView
        {
            float Zoom = 0.0f;          // screen px per tileset px (0: fitted, see AutoFit)
            bool AutoFit = true;        // follows the area's size until it is zoomed by hand
            std::string Tileset;        // the tileset it was fitted for
            int DragStart = -1, DragEnd = -1;
            bool Resizable = true;
            uint64_t ScrolledTo = 0;    // the last m_PaletteScrollRequest it scrolled for
        };
        // The palette of a tilemap's tileset: mouse wheel zooms (around the cursor), the scrollbars or a
        // middle / right drag scroll; a click picks a tile, a drag a rectangle of tiles (a multi-tile
        // brush). While an auto-tile variant / animation frames are being picked, clicks go there.
        // `height`: of the scrolling area (<= 0: the rest of the window). Edits go into `rules` / `props`.
        void DrawTilePalette(TilemapComponent& c, TilePaletteView& view, float height, AutoTileSet& rules, bool& rulesEdited,
                             TilesetProperties& props, bool& propsEdited);
        // The selected tile's collision and animation (the tileset's, see TilesetProperties).
        void DrawTileProperties(TilemapComponent& c, TilesetProperties& props, bool& propsEdited);
        bool MatchesFilter(Entity entity, const std::string& lowerFilter);
        const std::unordered_map<std::string, ScriptFieldValue>& GetScriptDefaults(const std::string& className);
        void MarkModified() { if (OnSceneModified) OnSceneModified(); }

    private:
        Ref<Scene> m_Context;
        SceneManager* m_SceneManager = nullptr;
        // The scene file the inspector's Sub-sahna section describes (read again when it changes).
        struct SubSceneFileCache
        {
            std::string Path;
            std::filesystem::file_time_type Time{};
            SceneFileInfo Info;
            bool Readable = false;
        } m_SubSceneFile;
        // The other loaded scenes of play mode, folded below the shown scene's tree.
        void DrawRuntimeSceneHeader();
        void DrawOtherRuntimeScenes();
        EntitySelection m_Selection;
        std::vector<Entity> m_VisibleRows;   // hierarchy rows in display order (Shift+click ranges)
        PendingClick m_PendingClick;
        PendingDrop m_PendingDrop;
        Entity m_ClickedSelected;            // pressed on an already selected row: decided on release
        std::optional<TransformComponent> m_TransformClipboard;
        TransformComponent m_PasteTransform;
        bool m_CanPasteTransform = false;
        std::string m_Filter;
        std::string m_AddComponentFilter;
        std::string m_EntityPickerFilter;
        std::string m_NewTagBuffer;
        entt::entity m_RenamingEntity = entt::null;
        int m_ScrollInspectorFrames = 0;
        float m_ScrollInspectorY = -1.0f; // < 0: the bottom
        bool m_OpenStandardMaterials = false;
        AssetInspectorPanel* m_AssetInspector = nullptr;
        uint64_t m_SeenSelectionRevision = 0;
        std::string m_RenameBuffer;
        bool m_PendingDelete = false, m_PendingDuplicate = false;
        int m_RowIndex = 0;
        bool m_HierarchyFocused = false;
        bool m_TilePaint = false;
        ColliderPointKind m_ColliderPointEdit = ColliderPointKind::None;
        bool m_SplinePointEdit = false;
        int m_JointAnchorEdit = -1;   // Joint2DType edited in the viewport, -1 = none
        int m_Joint3DAnchorEdit = -1; // Joint3DType edited in the viewport, -1 = none
        TileTool m_TileTool = TileTool::Brush, m_ToolBeforePicker = TileTool::Brush;
        int m_TileLayer = 0;
        int m_TileRule = -1;          // auto-tile rule of the tileset, -1 = plain tile
        bool m_EditRule = false;      // the rule's variant editor is open
        int m_EditRuleVariant = -1;   // the variant the next palette click goes to
        int m_RuleFirstTile = 0;
        uint64_t m_TileEntity = 0;    // the tilemap the tile state above belongs to
        TileBrush m_TileBrush;        // the palette selection (1 x 1 = one tile)
        uint64_t m_PaletteScrollRequest = 0; // the brush changed outside the palette (picker): palettes scroll to it
        TilePaletteView m_PaletteViews[2];  // the inspector's palette, the palette window's
        bool m_ShowTileProperties = true;    // collision / animation marks over the palette tiles
        bool m_TilePropertiesOpen = false;   // the inspector's "tile properties" part is open
        int m_AnimFramesTile = -1;    // palette clicks add frames to this tile's animation (-1: off)
        std::unordered_map<std::string, std::unordered_map<std::string, ScriptFieldValue>> m_ScriptDefaults;

        // inspector: sections of the entity being drawn, component clipboard, reordering
        std::vector<InspectorSection> m_Sections;
        std::vector<std::string> m_SectionOrder;   // the drawn entity's component keys, in order
        std::string m_ComponentClipboard;          // our own copy (the system clipboard is read first)
        std::string m_PasteText, m_PasteKey;       // clipboard contents when the menu opened
        struct PendingMove { std::string Key, Target; bool After = false; int Index = -1; };
        PendingMove m_PendingMove;                 // applied after the inspector is drawn
        std::optional<ParticlePreset> m_PendingParticlePreset; // applied after the inspector (RequestParticlePreset)
        UUID m_PendingParticlePresetFor = 0;                     // 0: onto a new entity
        UUID m_PendingSubEmitterFor = 0;           // "New template" of a particle system's sub-emitters (after the inspector)
        // Another scene asked for while the hierarchy / inspector is drawn (a sub-scene to open for
        // editing, a loaded scene to show in play mode): the panels' scene is replaced, so only once
        // both are drawn (ApplyPendingSceneRequests).
        std::filesystem::path m_PendingOpenScene;
        Ref<Scene> m_PendingRuntimeScene;
        InspectorFoldState* m_FoldState = nullptr;
        bool m_SceneEditable = true;
        PrefabDatabase* m_Prefabs = nullptr;
        bool m_OpenPrefabChanges = false;
        bool m_PrefabStageMode = false;
        // The reference (an inspected entity's EntityRef) points outside its prefab: an Apply keeps
        // it out of the prefab file.
        bool IsPrefabExternalReference(uint64_t id) const;
        // The inspected entity of a prefab instance (this frame): where its values can be applied.
        struct PrefabInspectorState
        {
            UUID Entity = 0;
            std::vector<std::string> Targets;
        } m_PrefabInspector;
        float m_GutterX = 0.0f;                    // left edge of the hierarchy's eye / lock column
    };

}
