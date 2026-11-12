#include <box2d/box2d.h>
#include <Zamin/Gameplay/GameplayRules.h>
#include "SaveWorld.h"
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
        return value.dump();
    }
    GameSave SaveWorld::Capture(const std::string& schema) const
    {
        Require(!m_Busy && m_Scene->IsRunning(), "Gameplay snapshot requires idle Play state");
        GameSave save{ m_Scene->GetName(), schema, {} };
        auto& rules = RuleWorld::For(*m_Scene);
        auto& ai = AIWorld::For(*m_Scene);
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
                Require(Signature(e) == s.Signature, "Gameplay configuration changed");
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
}
