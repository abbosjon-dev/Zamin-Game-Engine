#include <box2d/box2d.h>
#include <Zamin/Gameplay/GameplayRules.h>
#include "SaveWorld.h"
#include "QuestWorld.h"
#include "LoadoutWorld.h"
#include "CombatWorld.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Scene/WorldStreaming.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/FileSystem.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
namespace Zamin::Gameplay {
    namespace {
        using V = Visual::Value;
        void Require(bool ok, const char* error)
        {
            if (!ok) throw std::runtime_error(error);
        }
        V Vec(glm::vec3 p) { return V::array({ p.x, p.y, p.z }); }
        struct Guard
        {
            bool& Busy;
            explicit Guard(bool& busy) : Busy(busy)
            {
                Require(!busy, "Gameplay save operation reentered");
                Busy = true;
            }
            ~Guard() { Busy = false; }
        };
        void StopAndSync(Scene& scene, Entity e)
        {
            if (auto* manager = scene.GetSceneManager())
                if (auto* w = manager->GetWorldStreaming(); w && w->IsAnchor(&scene)) w->CancelNavigationQuery(e.GetUUID());
            if (auto* nav = e.TryGetComponent<NavAgentComponent>()) nav->Stop(true);
            auto p = e.GetTransform().Translation, r = e.GetTransform().Rotation;
            if (auto* b = e.TryGetComponent<Rigidbody2DComponent>(); b && b->RuntimeBody && scene.GetPhysicsWorld2D())
            {
                auto* body = (b2Body*)b->RuntimeBody;
                PhysicsWorld2D::SetTransform(body, glm::vec2(p), r.z);
                PhysicsWorld2D::SetLinearVelocity(body, {});
                body->SetAngularVelocity(0);
                scene.SetAwake2D(e, true);
            }
            if (auto* world = scene.GetPhysicsWorld3D())
            {
                if (auto* b = e.TryGetComponent<Rigidbody3DComponent>(); b && b->RuntimeBodyID != ~0u)
                {
                    world->SetTransform(b->RuntimeBodyID, p, glm::quat(r));
                    world->SetLinearVelocity(b->RuntimeBodyID, {});
                    world->SetAngularVelocity(b->RuntimeBodyID, {});
                }
                if (auto* c = e.TryGetComponent<CharacterController3DComponent>())
                {
                    if (c->RuntimeID != ~0u) world->SetCharacterPosition(c->RuntimeID, p);
                    c->MoveVelocity = c->Velocity = {};
                    c->VerticalVelocity = 0;
                    c->Grounded = false;
                }
            }
        }
    }
    SaveWorld& SaveWorld::For(Scene& scene)
    {
        auto& ctx = scene.GetRegistry().ctx();
        if (auto* s = ctx.find<SaveWorld>()) return *s;
        return ctx.emplace<SaveWorld>(&scene);
    }
    void ResetSaves(Scene& scene) { scene.GetRegistry().ctx().erase<SaveWorld>(); }
    void BeginSaveFrame(Scene& scene)
    {
        if (auto* s = scene.GetRegistry().ctx().find<SaveWorld>()) s->BeginFrame();
    }
    Entity SaveWorld::Find(const std::string& key) const
    {
        auto i = m_Bindings.find(key);
        if (i == m_Bindings.end()) return {};
        auto e = m_Scene->GetEntityByUUID(UUID(i->second.ID));
        return e && uint32_t(e) == i->second.Handle ? e : Entity{};
    }
    bool SaveWorld::Bind(Entity e, const std::string& key, std::string& error)
    {
        error.clear();
        if (m_Busy || !m_Scene->IsRunning() || !e || e.GetScene() != m_Scene || !ValidSaveKey(key) || uint64_t(e.GetComponent<RelationshipComponent>().Parent) || e.HasComponent<BoneAttachmentComponent>())
        {
            error = "Save binding needs idle Play, valid key and a scene root";
            return false;
        }
        for (auto& [name, b] : m_Bindings)
            if (name != key && b.ID == uint64_t(e.GetUUID()) && b.Handle == uint32_t(e))
            {
                error = "Entity already has a save key";
                return false;
            }
        if (auto current = Find(key); current && current != e)
        {
            error = "Save key already bound";
            return false;
        }
        if (!m_Bindings.contains(key) && m_Bindings.size() >= 256)
        {
            error = "Save binding limit 256";
            return false;
        }
        m_Bindings[key] = { uint64_t(e.GetUUID()), uint32_t(e) };
        return true;
    }
    Entity SaveWorld::Spawn(const std::string& key, const std::string& prefab, glm::vec3 position, std::string& error)
    {
        error.clear();
        try
        {
            Require(!m_Busy && m_Scene->IsRunning() && ValidSaveKey(key), "Invalid persistent spawn request");
            if (auto e = Find(key)) return e;
            Require(m_Bindings.contains(key) || m_Bindings.size() < 256, "Save binding limit 256");
            Require(m_Spawns < 8, "Persistent spawn budget 8");
            ++m_Spawns;
            for (float n : { position.x, position.y, position.z }) Require(std::isfinite(n) && std::abs(n) <= 1e9, "Invalid spawn position");
            auto root = Project::GetAssetDirectory();
            auto path = FileSystem::SafeJoin(root, prefab);
            Require(!prefab.empty() && path.extension() == ".zprefab" && !path.empty() && FileSystem::IsSubPathOnDisk(path, root), "Persistent prefab must be inside Assets");
            auto e = m_Scene->InstantiatePrefab(path);
            Require(bool(e), "Persistent prefab spawn failed");
            e.GetTransform().Translation = position;
            m_Scene->InvalidateWorldTransforms();
            StopAndSync(*m_Scene, e);
            if (!Bind(e, key, error))
            {
                m_Scene->DestroyEntity(e);
                throw std::runtime_error(error);
            }
            return e;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return {};
        }
    }
    bool SaveWorld::Unbind(const std::string& key) { return !m_Busy && m_Bindings.erase(key) > 0; }
    std::string SaveWorld::Reference(uint64_t id) const
    {
        if (!id) return {};
        for (auto& [key, b] : m_Bindings)
            if (b.ID == id && Find(key)) return key;
        return "uuid:" + std::to_string(id);
    }
    std::string SaveWorld::Signature(Entity e) const
    {
        Require(!uint64_t(e.GetComponent<RelationshipComponent>().Parent) && !e.HasComponent<BoneAttachmentComponent>(), "Saved entities must remain scene roots");
        auto& rules = RuleWorld::For(*m_Scene);
        auto& ai = AIWorld::For(*m_Scene);
        V value = { { "Parent", Reference(e.GetComponent<RelationshipComponent>().Parent) } };
        if (auto* a = rules.FindActor(e))
        {
            value["Bag"] = { a->Inventory.Slots, a->Inventory.Stack };
            if (a->Health)
            {
                auto& s = a->Health->Settings;
                value["Health"] = { s.Maximum, s.Shield, s.HitGrace, s.RespawnDelay, s.Team };
            }
        }
        auto interaction = rules.m_Interactions.find(uint64_t(e.GetUUID()));
        if (interaction != rules.m_Interactions.end() && interaction->second.Handle == uint32_t(e))
        {
            auto& s = interaction->second.Settings;
            value["Interaction"] = { s.Prompt, s.RequiredItem, s.RewardItem, s.RequiredCount, s.RewardCount, s.MaxUses, s.Consume, s.Deactivate, s.Enabled, s.Cooldown, s.Heal, Vec(s.Offset) };
        }
        if (auto* a = ai.Find(e))
        {
            auto& s = a->Spec;
            auto& b = s.Brain;
            auto& r = s.Recovery;
            V points = V::array();
            for (auto p : s.Points) points.push_back(Vec(p));
            value["AI"] = { b.View, b.Lose, b.Near, b.FieldOfView, b.Memory, b.Leash, b.AttackEnter, b.AttackExit, b.Damage, b.AttackCooldown, b.Wait, b.Arrive, b.Repath, b.Speed, b.TwoD, b.PingPong, s.Tag, Reference(s.Target), points, Vec(s.Eye), Vec(s.Facing), s.Mask, s.Relative, r.Enabled, r.StallSeconds, r.CooldownSeconds, r.ProgressDistance, r.MaxRetries, Reference(a->Owner) };
            // Delegated attacks extend the array only when set: default brains keep byte-identical signatures, so
            // every save written before the flag existed still loads.
            if (!b.Strike) value["AI"].push_back(false);
        }
        if (auto* b = e.TryGetComponent<Rigidbody2DComponent>()) value["Body2D"] = int(b->Type);
        if (auto* b = e.TryGetComponent<Rigidbody3DComponent>()) value["Body3D"] = int(b->Type);
        value["Character"] = e.HasComponent<CharacterController3DComponent>();
        if (auto* n = e.TryGetComponent<NavAgentComponent>()) value["Grid"] = Reference(n->Grid.ID);
        if (auto* n = e.TryGetComponent<NativeScriptComponent>())
        {
            value["Script"] = n->ClassName;
            if (auto* g = dynamic_cast<VisualGraphHost*>(n->Instance)) value["Graph"] = g->Graph;
        }
        if (auto* q = QuestWorld::For(*m_Scene).Find(e))
        {
            V definitions = V::object();
            for (auto& [key, s] : q->Definitions)
            {
                V objectives = V::array();
                for (auto& o : s.Objectives) objectives.push_back({ o.Key, o.Label, o.Match, int(o.Kind), o.Goal });
                definitions[key] = { s.Title, objectives, s.Requires, s.Rewards, s.XP };
            }
            value["Quests"] = definitions;
        }
        if (auto* r = LoadoutWorld::For(*m_Scene).Find(e))
        {
            V items = V::object(), abilities = V::object();
            for (auto& [key, i] : r->Items) items[key] = { int(i.Slot), i.Modifiers };
            for (auto& [key, a] : r->Abilities)
            {
                abilities[key] = { int(a.Kind), a.Amount, a.Cost, a.Cooldown, a.Range, a.Ingredient, a.Count, a.Unlocked };
                // Combat links extend the array only when present: unlinked abilities keep byte-identical v3
                // signatures, so every save written before the links existed still loads.
                if (!a.Projectile.empty() || !a.Area.empty() || !a.Effect.empty())
                    for (const auto* link : { &a.Projectile, &a.Area, &a.Effect }) abilities[key].push_back(*link);
            }
            value["Loadout"] = { r->Base, items, abilities };
        }
        return value.dump();
    }
    GameSave SaveWorld::Capture(const std::string& schema) const
    {
        Require(!m_Busy && m_Scene->IsRunning(), "Gameplay snapshot requires idle Play state");
        GameSave save{ m_Scene->GetName(), schema, {} };
        auto& rules = RuleWorld::For(*m_Scene);
        auto& ai = AIWorld::For(*m_Scene);
        // A scene that uses combat (definitions, effects or projectiles) writes v4; others keep v1/v2/v3.
        const auto* combat = m_Scene->GetRegistry().ctx().find<CombatWorld>();
        if (combat && !combat->Persists()) combat = nullptr;
        const auto reference = [this](uint64_t id) { return Reference(id); };
        for (auto& [key, binding] : m_Bindings)
        {
            SavedEntity out;
            out.Key = key;
            auto e = Find(key);
            out.Present = bool(e);
            if (e)
            {
                auto& t = e.GetTransform();
                out.Position = t.Translation;
                out.Rotation = t.Rotation;
                out.Scale = t.Scale;
                out.Active = e.IsActive();
                out.Signature = Signature(e);
                if (auto* q = QuestWorld::For(*m_Scene).Find(e))
                {
                    out.QuestBook = q->Book;
                    save.Version = std::max(save.Version, 2);
                }
                if (auto* r = LoadoutWorld::For(*m_Scene).Find(e))
                {
                    // Energy is saved as is: a MaxEnergy effect bonus is part of a v4 snapshot (its effects).
                    out.Loadout = r->State;
                    save.Version = 3;
                }
                if (auto* a = rules.FindActor(e))
                {
                    out.HasActor = true;
                    out.Inventory = a->Inventory;
                    out.Attack = std::max(0.0, a->AttackUntil - rules.m_Now);
                    if (a->Health)
                    {
                        auto& h = *a->Health;
                        out.HasHealth = true;
                        out.Health = h.Current;
                        out.Shield = h.Shield;
                        out.Grace = std::max(0.0, h.ImmuneUntil - rules.m_Now);
                        out.Respawn = h.Dead ? std::max(0.0, h.Settings.RespawnDelay - (rules.m_Now - h.DiedAt)) : 0;
                        out.Deaths = h.Deaths;
                        out.Dead = h.Dead;
                        if (combat) out.Effects = combat->SaveEffects(e, reference);
                    }
                }
                auto i = rules.m_Interactions.find(binding.ID);
                if (i != rules.m_Interactions.end() && i->second.Handle == binding.Handle)
                {
                    out.HasInteraction = true;
                    out.Uses = i->second.Uses;
                    out.InteractionCooldown = std::max(0.0, i->second.Until - rules.m_Now);
                }
                if (auto* a = ai.Find(e))
                {
                    out.HasAI = true;
                    out.AI = a->State;
                    if (a->State.Target)
                    {
                        out.Target = Reference(a->State.Target);
                        Require(ValidSaveKey(out.Target), "AI victim must have a save binding");
                    }
                }
            }
            save.Entities.push_back(std::move(out));
        }
        if (combat)
        {
            save.Version = 4;
            save.Combat = combat->SaveCombat(reference);
        }
        EncodeGameSave(save);
        return save;
    }
    SaveResult SaveWorld::Restore(const GameSave& input, const std::string& schema)
    {
        SaveResult result;
        try
        {
            Guard guard(m_Busy);
            Require(m_Scene->IsRunning(), "Gameplay restore requires Play");
            std::string error;
            GameSave save;
            Require(DecodeGameSave(EncodeGameSave(input), save, error), "Invalid gameplay snapshot");
            Require(save.Scene == m_Scene->GetName() && save.Schema == schema, "Gameplay scene/schema mismatch");
            Require(save.Entities.size() == m_Bindings.size(), "Gameplay binding set changed");
            auto& rules = RuleWorld::For(*m_Scene);
            auto& ai = AIWorld::For(*m_Scene);
            auto actors = rules.m_Actors;
            auto interactions = rules.m_Interactions;
            auto brains = ai.m_Records;
            auto& questWorld = QuestWorld::For(*m_Scene);
            auto quests = questWorld.m_Actors;
            auto& loadoutWorld = LoadoutWorld::For(*m_Scene);
            auto loadouts = loadoutWorld.m_Records;
            // Combat: v4 restores effects and projectiles in flight; older saves migrate to "none of either".
            auto* combat = save.Version >= 4 ? &CombatWorld::For(*m_Scene) : m_Scene->GetRegistry().ctx().find<CombatWorld>();
            CombatWorld::RestorePlan staged;
            if (combat)
            {
                if (save.Version >= 4) Require(save.Combat && save.Combat->Signature == combat->Signature(), "Combat definitions changed");
                std::vector<uint64_t> bound;
                for (auto& s : save.Entities)
                    if (auto e = Find(s.Key)) bound.push_back(e.GetUUID());
                combat->BeginRestore(staged, bound);
            }
            const CombatWorld::Resolver resolve = [this](const std::string& reference) {
                CombatWorld::Resolved r;
                if (reference.starts_with("uuid:"))
                {
                    r.ID = std::stoull(reference.substr(5));
                    r.Live = m_Scene->GetEntityByUUID(UUID(r.ID));
                }
                else if (!reference.empty())
                {
                    r.Live = Find(reference);
                    r.ID = r.Live ? uint64_t(r.Live.GetUUID()) : 0;
                }
                return r;
            };
            std::vector<std::pair<Entity, const SavedEntity*>> apply;
            std::set<std::string> present;
            for (auto& s : save.Entities)
                if (s.Present) present.insert(s.Key);
            for (auto& s : save.Entities)
            {
                Require(m_Bindings.contains(s.Key), "Missing gameplay save binding");
                auto e = Find(s.Key);
                if (!s.Present)
                {
                    if (e) apply.emplace_back(e, &s);
                    continue;
                }
                Require(bool(e), "Recreate the saved entity slot before loading");
                auto signature = Signature(e);
                if (save.Version == 1)
                {
                    auto legacy = V::parse(signature);
                    legacy.erase("Quests");
                    signature = legacy.dump();
                }
                if (save.Version < 3)
                {
                    auto legacy = V::parse(signature);
                    legacy.erase("Loadout");
                    signature = legacy.dump();
                }
                Require(signature == s.Signature, "Gameplay configuration changed");
                // Effects are staged first: their stat bonus (a MaxEnergy buff) bounds the saved loadout energy.
                Require(s.Effects.empty() || (s.HasHealth && combat), "Saved effects without a combat health actor");
                const CombatStats bonus = combat ? combat->StageEffects(staged, e, s.Effects, resolve) : CombatStats{};
                auto* loadout = loadoutWorld.Find(e);
                Require(save.Version < 3 || bool(loadout) == bool(s.Loadout), "Loadout layout changed");
                if (loadout)
                {
                    auto& next = loadouts.at(uint64_t(e.GetUUID()));
                    if (save.Version < 3)
                    {
                        next.State = {};
                        next.State.Energy = next.Base[3];
                        for (auto& [key, a] : next.Abilities) next.State.Abilities.emplace(key, AbilityProgress{ 0, 0, a.Unlocked });
                    }
                    else
                    {
                        Require(ValidateLoadout(next.Base, next.Items, next.Abilities, *s.Loadout, error, bonus), "Invalid saved loadout");
                        next.State = *s.Loadout;
                    }
                    next.Events.clear();
                    next.Dropped = 0;
                }
                auto* q = questWorld.Find(e);
                Require(q || s.QuestBook.Quests.empty(), "Saved quest actor not configured");
                if (q)
                {
                    auto& next = quests.at(uint64_t(e.GetUUID()));
                    if (save.Version == 1)
                    {
                        next.Book = {};
                        for (auto& [key, definition] : next.Definitions) next.Book.Quests.emplace(key, NewQuestProgress(definition));
                    }
                    else
                    {
                        Require(s.QuestBook.Quests.size() == next.Definitions.size(), "Saved quest definitions changed");
                        uint64_t xp = 0;
                        for (auto& [key, definition] : next.Definitions)
                        {
                            auto progress = s.QuestBook.Quests.find(key);
                            Require(progress != s.QuestBook.Quests.end(), "Saved quest missing");
                            Require(ValidateQuestProgress(definition, progress->second, error), "Invalid saved quest progress");
                            if (progress->second.Mode != QuestMode::Locked)
                                for (auto& required : definition.Requires) Require(s.QuestBook.Quests.at(required).Mode == QuestMode::Claimed, "Saved prerequisite not claimed");
                            if (progress->second.Mode == QuestMode::Claimed) xp += definition.XP;
                        }
                        Require(xp == s.QuestBook.XP, "Saved XP disagrees with claimed quests");
                        next.Book = s.QuestBook;
                    }
                    next.Events.clear();
                    next.Dropped = 0;
                }
                if (e.HasComponent<Rigidbody2DComponent>() || e.HasComponent<Rigidbody3DComponent>() || e.HasComponent<CharacterController3DComponent>()) Require(e.GetTransform().Scale == s.Scale, "Physical save scale changed");
                auto id = uint64_t(e.GetUUID());
                auto* a = rules.FindActor(e);
                Require(s.HasActor == bool(a) && s.HasHealth == bool(a && a->Health), "Gameplay actor layout changed");
                if (a)
                {
                    auto& next = actors.at(id);
                    Require(s.Inventory.Slots == a->Inventory.Slots && s.Inventory.Stack == a->Inventory.Stack, "Inventory configuration changed");
                    next.Inventory = s.Inventory;
                    next.AttackUntil = rules.m_Now + s.Attack;
                    next.Events.clear();
                    next.Dropped = 0;
                    if (next.Health)
                    {
                        auto& h = *next.Health;
                        Require(s.Health <= h.Settings.Maximum && s.Shield <= h.Settings.Shield && s.Respawn <= h.Settings.RespawnDelay, "Saved health exceeds configuration");
                        h.Current = s.Health;
                        h.Shield = s.Shield;
                        h.ImmuneUntil = rules.m_Now + s.Grace;
                        h.DiedAt = rules.m_Now - (h.Settings.RespawnDelay - s.Respawn);
                        h.Deaths = s.Deaths;
                        h.Dead = s.Dead;
                    }
                }
                auto it = interactions.find(id);
                bool has = it != interactions.end() && it->second.Handle == uint32_t(e);
                Require(s.HasInteraction == has, "Interaction layout changed");
                if (has)
                {
                    auto& next = it->second;
                    Require((!next.Settings.MaxUses || s.Uses <= uint32_t(next.Settings.MaxUses)) && s.InteractionCooldown <= next.Settings.Cooldown, "Invalid saved interaction progress");
                    next.Uses = s.Uses;
                    next.Until = rules.m_Now + s.InteractionCooldown;
                }
                auto* brain = ai.Find(e);
                Require(s.HasAI == bool(brain), "AI layout changed");
                if (brain)
                {
                    auto& next = brains.at(id);
                    auto& b = s.AI.Brain;
                    Require((next.Spec.Points.empty() ? b.Waypoint == 0 : b.Waypoint < next.Spec.Points.size()) && b.Remember <= next.Spec.Brain.Memory && b.WaitLeft <= next.Spec.Brain.Wait, "Invalid saved AI progress");
                    next.State = s.AI;
                    next.State.Target = 0;
                    next.TargetHandle = ~0u;
                    if (!s.Target.empty())
                    {
                        auto target = Find(s.Target);
                        Require(bool(target) && present.contains(s.Target), "Saved AI victim unavailable");
                        next.State.Target = target.GetUUID();
                        next.TargetHandle = uint32_t(target);
                    }
                    next.Spec.Enabled = s.AI.Enabled;
                    next.State.Visible = false;
                    next.State.Status = "Restored";
                    next.State.Retries = next.State.Dropped = 0;
                    next.Events.clear();
                    next.Recovery.Reset();
                    next.Issued = false;
                    next.RepathLeft = 0;
                    next.Deaths = s.Deaths;
                    next.Route.clear();
                    for (auto p : next.Spec.Points) next.Route.push_back(next.Spec.Relative ? s.AI.Home + p : p);
                }
                apply.emplace_back(e, &s);
            }
            if (combat && save.Combat) combat->StageProjectiles(staged, *save.Combat, resolve);
            // All parsing, compatibility checks and allocations precede the commit. Callbacks run last.
            for (auto& [e, s] : apply)
                if (s->Present)
                {
                    auto& t = e.GetTransform();
                    t.Translation = s->Position;
                    t.Rotation = s->Rotation;
                    t.Scale = s->Scale;
                }
            m_Scene->InvalidateWorldTransforms();
            rules.m_Actors.swap(actors);
            rules.m_Interactions.swap(interactions);
            ai.m_Records.swap(brains);
            questWorld.m_Actors.swap(quests);
            loadoutWorld.m_Records.swap(loadouts);
            // No shot fired or effect applied before the load acts on the restored state: the snapshot's
            // projectiles and bound actors' effects replace them (none for a v1/v2/v3 save).
            if (combat)
            {
                const size_t flying = combat->LiveCount(), effects = combat->EffectCount();
                combat->CommitRestore(staged);
                if (save.Version >= 4) ZM_CORE_INFO("Jang holati tiklandi: {} effekt, {} snaryad", combat->EffectCount(), combat->LiveCount());
                else if (flying || effects != combat->EffectCount())
                    ZM_CORE_INFO("Eski save v{} yuklandi: {} ta uchayotgan snaryad bekor qilindi, {} ta effekt olib tashlandi", save.Version, flying, effects - combat->EffectCount());
            }
            for (auto& [e, s] : apply) StopAndSync(*m_Scene, e);
            for (auto handle : m_Scene->GetRegistry().view<NativeScriptComponent>())
                if (auto* g = dynamic_cast<VisualGraphHost*>(m_Scene->GetRegistry().get<NativeScriptComponent>(handle).Instance)) g->InvalidateGameplayState();
            for (auto& [e, s] : apply)
                if (e) m_Scene->SetActive(e, s->Present && s->Active);
            result.Success = true;
            result.Entities = save.Entities.size();
        }
        catch (const std::exception& e)
        {
            result.Reason = e.what();
        }
        return result;
    }
    std::filesystem::path SaveWorld::Path(const std::string& slot) const
    {
        Require(ValidSaveKey(slot), "Save slot must be 1..64 letters/digits/_/-");
        return SaveData::GetFilePath().parent_path() / "gameplay-saves" / (slot + ".json");
    }
    bool SaveWorld::Read(const std::filesystem::path& path, GameSave& save, std::string& error) const
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Gameplay save unavailable";
            return false;
        }
        std::string bytes(2 * 1024 * 1024 + 1, '\0');
        stream.read(bytes.data(), std::streamsize(bytes.size()));
        bytes.resize(size_t(stream.gcount()));
        return DecodeGameSave(bytes, save, error);
    }
    SaveResult SaveWorld::Save(const std::string& slot, const std::string& schema)
    {
        SaveResult r;
        try
        {
            Require(!m_Busy && m_IO < 2, "Gameplay save I/O budget or busy");
            ++m_IO;
            auto file = Path(slot);
            auto data = Capture(schema);
            auto bytes = EncodeGameSave(data);
            GameSave previous;
            std::string error;
            if (Read(file, previous, error)) Require(FileSystem::WriteTextFileAtomic(file.string() + ".bak", EncodeGameSave(previous)), "Cannot preserve gameplay backup");
            Require(FileSystem::WriteTextFileAtomic(file, bytes), "Cannot write gameplay save");
            r.Success = true;
            r.Entities = data.Entities.size();
        }
        catch (const std::exception& e)
        {
            r.Reason = e.what();
        }
        return r;
    }
    SaveResult SaveWorld::Load(const std::string& slot, const std::string& schema)
    {
        SaveResult r;
        try
        {
            Require(!m_Busy && m_IO < 2, "Gameplay load I/O budget or busy");
            ++m_IO;
            auto file = Path(slot);
            GameSave save;
            std::string error;
            bool recovered = false;
            if (!Read(file, save, error))
            {
                Require(Read(file.string() + ".bak", save, error), "Gameplay save and backup unavailable/invalid");
                recovered = true;
            }
            r = Restore(save, schema);
            r.Recovered = r.Success && recovered;
        }
        catch (const std::exception& e)
        {
            r.Reason = e.what();
        }
        return r;
    }
    SaveResult SaveWorld::Inspect(const std::string& slot)
    {
        SaveResult r;
        try
        {
            Require(!m_Busy && m_IO < 2, "Gameplay inspect I/O budget or busy");
            ++m_IO;
            GameSave save;
            std::string error;
            auto file = Path(slot);
            if (!Read(file, save, error))
            {
                if (!Read(file.string() + ".bak", save, error))
                {
                    r.Reason = error;
                    return r;
                }
                r.Recovered = true;
            }
            r.Success = true;
            r.Entities = save.Entities.size();
        }
        catch (const std::exception& e)
        {
            r.Reason = e.what();
        }
        return r;
    }
    GameTransfer SaveWorld::CaptureTransfer(const std::string& key, const std::string& schema) const
    {
        Require(!m_Busy && m_Scene->IsRunning(), "Gameplay transfer requires idle Play state");
        Require(ValidSaveKey(key) && ValidSaveKey(schema), "Invalid gameplay transfer key/schema");
        auto e = Find(key);
        Require(e && e.IsActive(), "Transfer requires a bound active actor");
        auto& rules = RuleWorld::For(*m_Scene);
        const auto* a = rules.FindActor(e);
        Require(a && a->Health && !a->Health->Dead && a->Health->Current > 0, "Transfer requires a living actor");
        GameTransfer t;
        t.Scene = m_Scene->GetName();
        t.Schema = schema;
        t.Key = key;
        t.Signature = Signature(e);
        t.Health = a->Health->Current;
        t.Shield = a->Health->Shield;
        t.Deaths = a->Health->Deaths;
        t.Inventory = a->Inventory;
        if (auto* q = QuestWorld::For(*m_Scene).Find(e)) t.QuestBook = q->Book;
        if (auto* r = LoadoutWorld::For(*m_Scene).Find(e))
        {
            t.Loadout = r->State;
            // Status effects stay behind: the carried energy is bounded by the maximum without their bonus, and
            // cooldowns end at the level exit.
            t.Loadout->Energy = std::min(t.Loadout->Energy, EffectiveStats(r->Base, r->Items, r->State)[3]);
            for (auto& [name, progress] : t.Loadout->Abilities) progress.Remaining = 0;
        }
        EncodeGameTransfer(t);
        return t;
    }
    SaveResult SaveWorld::RestoreTransfer(const GameTransfer& input, const std::string& key, const std::string& schema)
    {
        SaveResult result;
        try
        {
            Guard guard(m_Busy);
            Require(m_Scene->IsRunning(), "Gameplay transfer requires Play");
            std::string error;
            GameTransfer t;
            Require(DecodeGameTransfer(EncodeGameTransfer(input), t, error), "Invalid gameplay transfer");
            Require(t.Schema == schema, "Gameplay transfer schema mismatch");
            Require(t.Key == key, "Gameplay transfer key mismatch");
            auto e = Find(key);
            Require(e && e.IsActive(), "Transfer actor is not bound here");
            Require(Signature(e) == t.Signature, "Transfer content changed");
            auto& rules = RuleWorld::For(*m_Scene);
            auto* a = rules.FindActor(e);
            Require(a && a->Health, "Transfer actor has no health");
            Require(!a->Health->Dead, "Transfer target is dead");
            Require(t.Inventory.Slots == a->Inventory.Slots && t.Inventory.Stack == a->Inventory.Stack, "Inventory configuration changed");
            Require(t.Health <= a->Health->Settings.Maximum && t.Shield <= a->Health->Settings.Shield, "Saved health exceeds configuration");
            // Stage every part before the commit; a bad late field leaves the actor untouched.
            auto& questWorld = QuestWorld::For(*m_Scene);
            auto* q = questWorld.Find(e);
            Require(q || t.QuestBook.Quests.empty(), "Saved quest actor not configured");
            QuestBookSave book;
            if (q)
            {
                Require(t.QuestBook.Quests.size() == q->Definitions.size(), "Saved quest definitions changed");
                uint64_t xp = 0;
                for (auto& [name, definition] : q->Definitions)
                {
                    auto progress = t.QuestBook.Quests.find(name);
                    Require(progress != t.QuestBook.Quests.end(), "Saved quest missing");
                    Require(ValidateQuestProgress(definition, progress->second, error), "Invalid saved quest progress");
                    if (progress->second.Mode != QuestMode::Locked)
                        for (auto& required : definition.Requires) Require(t.QuestBook.Quests.at(required).Mode == QuestMode::Claimed, "Saved prerequisite not claimed");
                    if (progress->second.Mode == QuestMode::Claimed) xp += definition.XP;
                }
                Require(xp == t.QuestBook.XP, "Saved XP disagrees with claimed quests");
                book = t.QuestBook;
            }
            auto& loadoutWorld = LoadoutWorld::For(*m_Scene);
            auto* loadout = loadoutWorld.Find(e);
            Require(bool(loadout) == bool(t.Loadout), "Loadout layout changed");
            LoadoutState state;
            if (loadout)
            {
                state = *t.Loadout;
                for (auto& [name, progress] : state.Abilities) progress.Remaining = 0;
                Require(ValidateLoadout(loadout->Base, loadout->Items, loadout->Abilities, state, error), "Invalid saved loadout");
            }
            auto* combat = m_Scene->GetRegistry().ctx().find<CombatWorld>();
            // Commit: nothing below throws.
            a->Inventory = t.Inventory;
            a->AttackUntil = rules.m_Now;
            a->Events.clear();
            a->Dropped = 0;
            auto& h = *a->Health;
            h.Current = t.Health;
            h.Shield = t.Shield;
            h.Deaths = t.Deaths;
            h.Dead = false;
            h.ImmuneUntil = rules.m_Now;
            h.DiedAt = rules.m_Now - h.Settings.RespawnDelay;
            if (q)
            {
                q->Book = std::move(book);
                q->Events.clear();
                q->Dropped = 0;
            }
            if (loadout)
            {
                loadout->State = std::move(state);
                loadout->Events.clear();
                loadout->Dropped = 0;
            }
            if (combat) combat->ClearEffects(e);
            for (auto handle : m_Scene->GetRegistry().view<NativeScriptComponent>())
                if (auto* g = dynamic_cast<VisualGraphHost*>(m_Scene->GetRegistry().get<NativeScriptComponent>(handle).Instance)) g->InvalidateGameplayState();
            result.Success = true;
            result.Entities = 1;
        }
        catch (const std::exception& e)
        {
            result.Reason = e.what();
        }
        return result;
    }
    std::filesystem::path SaveWorld::TransferPath(const std::string& slot) const
    {
        Require(ValidSaveKey(slot), "Transfer slot must be 1..64 letters/digits/_/-");
        return SaveData::GetFilePath().parent_path() / "gameplay-transfers" / (slot + ".json");
    }
    bool SaveWorld::ReadTransfer(const std::filesystem::path& path, GameTransfer& transfer, std::string& error) const
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Gameplay transfer unavailable";
            return false;
        }
        std::string bytes(256 * 1024 + 1, '\0');
        stream.read(bytes.data(), std::streamsize(bytes.size()));
        bytes.resize(size_t(stream.gcount()));
        return DecodeGameTransfer(bytes, transfer, error);
    }
    SaveResult SaveWorld::WriteTransfer(const std::string& slot, const std::string& key, const std::string& schema)
    {
        SaveResult r;
        try
        {
            Require(!m_Busy && m_IO < 2, "Gameplay transfer I/O budget or busy");
            ++m_IO;
            auto file = TransferPath(slot);
            auto data = CaptureTransfer(key, schema);
            auto bytes = EncodeGameTransfer(data);
            GameTransfer previous;
            std::string error;
            if (ReadTransfer(file, previous, error)) Require(FileSystem::WriteTextFileAtomic(file.string() + ".bak", EncodeGameTransfer(previous)), "Cannot preserve gameplay transfer backup");
            Require(FileSystem::WriteTextFileAtomic(file, bytes), "Cannot write gameplay transfer");
            r.Success = true;
            r.Entities = 1;
        }
        catch (const std::exception& e)
        {
            r.Reason = e.what();
        }
        return r;
    }
    SaveResult SaveWorld::LoadTransfer(const std::string& slot, const std::string& key, const std::string& schema)
    {
        SaveResult r;
        try
        {
            Require(!m_Busy && m_IO < 2, "Gameplay transfer I/O budget or busy");
            ++m_IO;
            auto file = TransferPath(slot);
            GameTransfer transfer;
            std::string error;
            bool recovered = false;
            if (!ReadTransfer(file, transfer, error))
            {
                Require(ReadTransfer(file.string() + ".bak", transfer, error), "Gameplay transfer and backup unavailable/invalid");
                recovered = true;
            }
            r = RestoreTransfer(transfer, key, schema);
            r.Recovered = r.Success && recovered;
        }
        catch (const std::exception& e)
        {
            r.Reason = e.what();
        }
        return r;
    }
    SaveResult SaveWorld::InspectTransfer(const std::string& slot)
    {
        SaveResult r;
        try
        {
            Require(!m_Busy && m_IO < 2, "Gameplay transfer inspect I/O budget or busy");
            ++m_IO;
            GameTransfer transfer;
            std::string error;
            auto file = TransferPath(slot);
            if (!ReadTransfer(file, transfer, error))
            {
                if (!ReadTransfer(file.string() + ".bak", transfer, error))
                {
                    r.Reason = error;
                    return r;
                }
                r.Recovered = true;
            }
            r.Success = true;
            r.Entities = 1;
        }
        catch (const std::exception& e)
        {
            r.Reason = e.what();
        }
        return r;
    }
}
