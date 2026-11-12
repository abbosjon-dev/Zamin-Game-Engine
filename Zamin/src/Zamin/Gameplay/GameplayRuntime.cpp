#include "GameplayRuntime.h"
#include "GameplayRules.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Math/Math.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Utils/FileSystem.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <fstream>
#include <box2d/b2_body.h>
#include <limits>
#include <cmath>
#include <stdexcept>
namespace Zamin::Gameplay {
    std::optional<Visual::Value> DispatchRules(Entity, Entity, const std::string&, const std::map<std::string, Visual::Value>&);
    namespace {
        using V = Visual::Value;
        struct Args
        {
            const std::map<std::string, V>& Data;
            float N(const char* key) const
            {
                double v = Data.at(key).get<double>();
                if (!std::isfinite(v) || std::abs(v) > std::numeric_limits<float>::max()) throw std::runtime_error("Gameplay float overflow");
                return float(v);
            }
            bool B(const char* key) const { return Data.at(key).get<bool>(); }
            std::string S(const char* key) const { return Data.at(key).get<std::string>(); }
            glm::vec3 Vec(const char* key) const
            {
                const auto& v = Data.at(key);
                glm::vec3 result{};
                for (size_t i = 0; i < std::min(v.size(), size_t(3)); ++i)
                {
                    double n = v.at(i).get<double>();
                    if (!std::isfinite(n) || std::abs(n) > 1e9) throw std::runtime_error("Gameplay vector range");
                    result[int(i)] = float(n);
                }
                return result;
            }
            Entity Ref(Scene& scene, const char* key) const
            {
                const auto id = Data.at(key).get<uint64_t>();
                return id ? scene.GetEntityByUUID(UUID(id)) : Entity{};
            }
            uint16_t Mask(const char* key) const
            {
                float n = N(key);
                if (n < 0 || n > 65535 || std::floor(n) != n) throw std::runtime_error("Gameplay layer mask must be integer 0..65535");
                return uint16_t(n);
            }
        };
        V Vector(glm::vec3 v) { return V::array({ v.x, v.y, v.z }); }
        V Status(bool success, std::string reason = {}) { return { { "Success", success }, { "Reason", std::move(reason) } }; }
        struct Pose
        {
            glm::vec3 Position{}, Rotation{}, Scale{ 1 };
        };
        Pose World(Entity e)
        {
            Pose p;
            if (!Math::DecomposeTransform(e.GetScene()->GetWorldTransform(e), p.Position, p.Rotation, p.Scale)) throw std::runtime_error("Gameplay target has invalid world transform");
            return p;
        }
        void SetWorld(Entity e, const Pose& p)
        {
            auto& scene = *e.GetScene();
            const auto matrix = glm::translate(glm::mat4(1), p.Position) * glm::mat4_cast(glm::quat(p.Rotation)) * glm::scale(glm::mat4(1), p.Scale);
            if (auto parent = scene.GetParent(e))
            {
                const auto parentWorld = scene.GetWorldTransform(parent);
                if (std::abs(glm::determinant(parentWorld)) < 1e-8f) throw std::runtime_error("Gameplay target parent is singular");
                glm::vec3 t, r, s;
                if (!Math::DecomposeTransform(glm::inverse(parentWorld) * matrix, t, r, s)) throw std::runtime_error("Gameplay transform cannot be represented under parent");
            }
            scene.SetWorldTransform(e, matrix);
        }
        glm::vec3 Velocity(Entity e)
        {
            auto& scene = *e.GetScene();
            if (auto* c = e.TryGetComponent<CharacterController3DComponent>()) return c->Velocity;
            if (auto* b = e.TryGetComponent<Rigidbody2DComponent>(); b && b->RuntimeBody && scene.GetPhysicsWorld2D()) return glm::vec3(PhysicsWorld2D::GetLinearVelocity((b2Body*)b->RuntimeBody), 0);
            if (auto* b = e.TryGetComponent<Rigidbody3DComponent>(); b && b->RuntimeBodyID != 0xffffffff && scene.GetPhysicsWorld3D()) return scene.GetPhysicsWorld3D()->GetLinearVelocity(b->RuntimeBodyID);
            return {};
        }
        bool Grounded2D(Entity e, float probe, uint16_t mask)
        {
            auto& scene = *e.GetScene();
            if (Velocity(e).y > .1f) return false;
            auto world = scene.GetWorldTransform(e);
            glm::vec3 center = scene.GetWorldPosition(e);
            float width = .25f, height = .5f;
            if (auto* box = e.TryGetComponent<BoxCollider2DComponent>())
            {
                center = glm::vec3(world * glm::vec4(box->Offset, 0, 1));
                width = std::abs(world[0].x) * box->Size.x + std::abs(world[1].x) * box->Size.y;
                height = std::abs(world[0].y) * box->Size.x + std::abs(world[1].y) * box->Size.y;
            }
            else if (auto* circle = e.TryGetComponent<CircleCollider2DComponent>())
            {
                center = glm::vec3(world * glm::vec4(circle->Offset, 0, 1));
                width = height = circle->Radius * std::max(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])));
            }
            for (float side : { -.8f, 0.0f, .8f })
                for (const auto& hit : scene.Raycast2DAll(glm::vec2(center) + glm::vec2(side * width, 0), { 0, -1 }, height + probe, mask, false))
                    if (hit.EntityID != e.GetUUID() && hit.Normal.y >= .65f) return true;
            return false;
        }
        bool Grounded(Entity e, bool twoD, bool topDown, float probe, uint16_t mask)
        {
            if (twoD) return topDown || Grounded2D(e, probe, mask);
            auto* c = e.TryGetComponent<CharacterController3DComponent>();
            return c && c->Grounded;
        }
        void StopActor(Entity e, bool twoD, bool topDown)
        {
            auto& scene = *e.GetScene();
            if (twoD)
            {
                if (auto* b = e.TryGetComponent<Rigidbody2DComponent>(); b && b->RuntimeBody && scene.GetPhysicsWorld2D())
                {
                    auto v = PhysicsWorld2D::GetLinearVelocity((b2Body*)b->RuntimeBody);
                    PhysicsWorld2D::SetLinearVelocity((b2Body*)b->RuntimeBody, { 0, topDown ? 0.0f : v.y });
                }
            }
            else if (auto* c = e.TryGetComponent<CharacterController3DComponent>()) c->MoveVelocity = {};
        }
        std::filesystem::path CheckpointPath(const std::string& slot)
        {
            if (slot.empty() || slot.size() > 64 || slot.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) throw std::runtime_error("Checkpoint slot must be 1..64 letters/digits/_/-");
            return SaveData::GetFilePath().parent_path() / "gameplay-checkpoints" / (slot + ".json");
        }
        bool ReadCheckpoint(const std::filesystem::path& file, Checkpoint& point, std::string& error)
        {
            std::ifstream input(file, std::ios::binary);
            if (!input)
            {
                error = "Checkpoint file unavailable";
                return false;
            }
            char data[4097];
            input.read(data, sizeof(data));
            const auto size = input.gcount();
            if (size > 4096)
            {
                error = "Checkpoint exceeds 4 KiB";
                return false;
            }
            return DecodeCheckpoint(std::string_view(data, size_t(size)), point, error);
        }
        void Restore(Entity actor, const Checkpoint& point)
        {
            auto& scene = *actor.GetScene();
            auto pose = World(actor);
            pose.Position = point.Position;
            pose.Rotation = point.Rotation;
            SetWorld(actor, pose);
            if (auto* body = actor.TryGetComponent<Rigidbody2DComponent>(); body && body->RuntimeBody && scene.GetPhysicsWorld2D())
            {
                PhysicsWorld2D::SetTransform((b2Body*)body->RuntimeBody, glm::vec2(pose.Position), pose.Rotation.z);
                PhysicsWorld2D::SetLinearVelocity((b2Body*)body->RuntimeBody, {});
                ((b2Body*)body->RuntimeBody)->SetAngularVelocity(0);
                scene.SetAwake2D(actor, true);
            }
            if (auto* world = scene.GetPhysicsWorld3D())
            {
                if (auto* body = actor.TryGetComponent<Rigidbody3DComponent>(); body && body->RuntimeBodyID != 0xffffffff)
                {
                    world->SetTransform(body->RuntimeBodyID, pose.Position, glm::quat(pose.Rotation));
                    world->SetLinearVelocity(body->RuntimeBodyID, {});
                    world->SetAngularVelocity(body->RuntimeBodyID, {});
                }
                if (auto* c = actor.TryGetComponent<CharacterController3DComponent>())
                {
                    if (c->RuntimeID != 0xffffffff) world->SetCharacterPosition(c->RuntimeID, pose.Position);
                    c->MoveVelocity = c->Velocity = {};
                    c->VerticalVelocity = 0;
                    c->Grounded = false;
                }
            }
        }
    }
    void Runtime::Reset()
    {
        m_Motors.clear();
        m_Cameras.clear();
        m_Checkpoints.clear();
        m_Frame = 0;
        m_IO = 0;
        m_AfterPhysics = false;
    }
    void Runtime::BeginFrame(Scene& scene)
    {
        ++m_Frame;
        m_IO = 0;
        m_AfterPhysics = false;
        std::erase_if(m_Motors, [&](const auto& item) {auto e=scene.GetEntityByUUID(UUID(item.first));return !e||uint32_t(e)!=item.second.Handle; });
        std::erase_if(m_Cameras, [&](const auto& item) {auto e=scene.GetEntityByUUID(UUID(item.first));return !e||uint32_t(e)!=item.second.Handle; });
    }
    void Runtime::Stop(Scene& scene)
    {
        for (auto& [id, record] : m_Motors)
        {
            auto e = scene.GetEntityByUUID(UUID(id));
            if (e && uint32_t(e) == record.Handle) StopActor(e, record.TwoD, record.TopDown);
            record.State = {};
            record.Frame = UINT64_MAX;
        }
        for (auto& [id, record] : m_Cameras) record.State.Initialized = false;
    }
    Visual::Value Runtime::Dispatch(Entity host, Entity target, const std::string& kind, const std::map<std::string, Visual::Value>& values)
    {
        if (!host || !target || host.GetScene() != target.GetScene()) throw std::runtime_error("Gameplay targets must be in the graph scene");
        auto& scene = *host.GetScene();
        Args a{ values };
        const uint64_t id = target.GetUUID();
        if (kind == "Gameplay.Respawn")
        {
            auto& rules = RuleWorld::For(scene);
            const double grace = a.N("Grace");
            std::string error;
            if (!rules.CanRevive(target, grace, error)) return Status(false, error);
            std::map<std::string, V> restore;
            for (const char* key : { "Slot", "Persistent", "Clearance", "Mask" }) restore[key] = values.at(key);
            auto result = Dispatch(host, target, "Gameplay.CheckpointRestore", restore);
            if (!result.at("Success").get<bool>()) return result;
            if (!rules.Revive(target, grace, error)) throw std::runtime_error("Respawn health commit failed: " + error);
            return Status(true);
        }
        if (auto result = DispatchRules(host, target, kind, values)) return *result;
        auto measuredVelocity = [&](Entity source) {
            auto found = m_Motors.find(uint64_t(source.GetUUID()));
            if (found != m_Motors.end() && found->second.Frame == m_Frame && found->second.TwoD && (!m_AfterPhysics || scene.GetPhysics2DStepsLastFrame() == 0)) return found->second.Measured;
            return Velocity(source);
        };
        if (kind == "Gameplay.Input")
        {
            const bool enabled = a.B("Enabled") && !scene.IsTextInputFocused();
            auto move = enabled ? Input::GetVector(a.S("Move")) : glm::vec2(0);
            auto look = enabled ? Input::GetMouseDelta() : glm::vec2(0);
            return V{ { "Move", V::array({ move.x, move.y }) }, { "Look", V::array({ look.x, look.y }) }, { "Jump", enabled && Input::IsActionJustPressed(a.S("Jump")) }, { "JumpHeld", enabled && Input::IsActionPressed(a.S("Jump")) }, { "Sprint", enabled && Input::IsActionPressed(a.S("Sprint")) }, { "Enabled", enabled } };
        }
        if (kind == "Gameplay.Motion")
        {
            const bool twoD = a.B("TwoD"), topDown = a.B("TopDown");
            const auto v = measuredVelocity(target);
            const float speed = twoD ? (topDown ? glm::length(glm::vec2(v)) : std::abs(v.x)) : glm::length(glm::vec2(v.x, v.z));
            return V{ { "Velocity", Vector(v) }, { "Speed", speed }, { "Grounded", Grounded(target, twoD, topDown, .12f, a.Mask("GroundMask")) }, { "Position", Vector(scene.GetWorldPosition(target)) } };
        }
        if (kind == "Gameplay.Motor2D" || kind == "Gameplay.Motor3D")
        {
            const bool twoD = kind == "Gameplay.Motor2D", topDown = twoD && a.B("TopDown");
            if (!scene.IsActiveInHierarchy(target)) throw std::runtime_error("Motor target is inactive");
            const float probe = a.N("Probe");
            if (probe < 0 || probe > 1) throw std::runtime_error("Ground probe must be 0..1");
            auto* body = target.TryGetComponent<Rigidbody2DComponent>();
            auto* character = target.TryGetComponent<CharacterController3DComponent>();
            if (twoD && (!body || body->Type != Rigidbody2DComponent::BodyType::Dynamic || !body->RuntimeBody)) throw std::runtime_error("Motor2D requires a running dynamic Rigidbody2D");
            if (!twoD && (!character || character->RuntimeID == 0xffffffff || target.HasComponent<Rigidbody3DComponent>())) throw std::runtime_error("Motor3D requires a running CharacterController3D without Rigidbody3D");
            if (!m_Motors.contains(id) && m_Motors.size() >= 64) throw std::runtime_error("Gameplay motor limit 64");
            auto& record = m_Motors[id];
            if (record.Handle != uint32_t(target))
            {
                record = {};
                record.Handle = uint32_t(target);
            }
            auto* rules = scene.GetRegistry().ctx().find<RuleWorld>();
            auto health = rules ? rules->Health(target) : std::optional<HealthState>{};
            const uint32_t deaths = health ? health->Deaths : 0;
            if (record.Deaths != deaths)
            {
                record.State = {};
                record.Deaths = deaths;
            }
            if (record.Frame == m_Frame) throw std::runtime_error("Motor can advance only once per actor per frame");
            MotorSettings settings{ a.N("Speed"), a.N("Acceleration"), a.N("Deceleration"), a.N("AirControl"), a.N("SprintMultiplier"), a.N("JumpSpeed"), a.N("Coyote"), a.N("JumpBuffer"), a.N("JumpCut") };
            MotorInput input;
            input.Move = glm::vec2(a.Vec("Move"));
            input.Delta = a.N("Delta");
            input.JumpPressed = !topDown && a.B("Jump");
            input.JumpHeld = a.B("JumpHeld");
            input.Sprint = a.B("Sprint");
            input.Enabled = a.B("Enabled");
            if (health && health->Dead) input.Enabled = false;
            auto measured = Velocity(target);
            input.Grounded = Grounded(target, twoD, topDown, probe, a.Mask("GroundMask"));
            if (!twoD)
            {
                const auto requested = input.Move;
                input.Move = { requested.x, -requested.y };
                if (auto camera = a.Ref(scene, "Camera"))
                {
                    const auto matrix = scene.GetWorldTransform(camera);
                    glm::vec2 right(matrix[0].x, matrix[0].z), forward(-matrix[2].x, -matrix[2].z);
                    if (glm::length(right) > .001f && glm::length(forward) > .001f) input.Move = glm::normalize(right) * requested.x + glm::normalize(forward) * requested.y;
                }

                input.HorizontalVelocity = { measured.x, measured.z };
                input.VerticalVelocity = character->VerticalVelocity;
            }
            else
            {
                if (!topDown) input.Move.y = 0;
                input.HorizontalVelocity = topDown ? glm::vec2(measured) : glm::vec2(measured.x, 0);
                input.VerticalVelocity = measured.y;
            }
            auto next = record.State;
            MotorOutput result;
            std::string error;
            if (!StepMotor(settings, next, input, result, error)) throw std::runtime_error(error);
            if (twoD) PhysicsWorld2D::SetLinearVelocity((b2Body*)body->RuntimeBody, topDown ? result.Horizontal : glm::vec2(result.Horizontal.x, result.Vertical));
            else
            {
                character->Move({ result.Horizontal.x, 0, result.Horizontal.y });
                character->VerticalVelocity = result.Vertical;
                if (result.Jumped) character->Grounded = false;
            }
            record.State = next;
            record.Measured = measured;
            record.Frame = m_Frame;
            record.TwoD = twoD;
            record.TopDown = topDown;
            return V{ { "Velocity", Vector(twoD ? (topDown ? glm::vec3(result.Horizontal, 0) : glm::vec3(result.Horizontal.x, result.Vertical, 0)) : glm::vec3(result.Horizontal.x, result.Vertical, result.Horizontal.y)) }, { "Jumped", result.Jumped }, { "Grounded", input.Grounded }, { "Facing", next.Facing } };
        }
        if (kind == "Gameplay.BindAnimator")
        {
            auto source = a.Ref(scene, "Source");
            if (!source)
            {
                if (values.at("Source").get<uint64_t>()) throw std::runtime_error("Animator source missing");
                source = target;
            }
            auto* animator = target.TryGetComponent<AnimatorComponent>();
            if (!animator) throw std::runtime_error("Animator binding requires Animator");
            const bool twoD = a.B("TwoD"), topDown = a.B("TopDown");
            const auto velocity = measuredVelocity(source);
            const auto pose = World(source);
            const auto local = glm::inverse(glm::quat(pose.Rotation)) * velocity;
            const float speed = twoD ? (topDown ? glm::length(glm::vec2(velocity)) : std::abs(velocity.x)) : glm::length(glm::vec2(velocity.x, velocity.z));
            const float scale = a.N("ReferenceSpeed"), damping = a.N("Damping"), delta = a.N("Delta");
            if (scale <= 0 || scale > 1000 || damping < 0 || damping > 10 || delta < 0) throw std::runtime_error("Invalid Animator binding timing/reference speed");
            const bool grounded = Grounded(source, twoD, topDown, .12f, a.Mask("GroundMask"));
            std::map<std::string, std::pair<AnimatorParameter::Type, float>> updates;
            auto add = [&](const char* pin, AnimatorParameter::Type type, float value) {auto name=a.S(pin);if(name.empty())return;if(updates.contains(name))throw std::runtime_error("Animator binding parameter names must be distinct");updates[name]={type,value}; };
            add("SpeedParameter", AnimatorParameter::Type::Float, speed);
            add("HorizontalParameter", AnimatorParameter::Type::Float, std::clamp((twoD ? velocity.x : local.x) / scale, -1.0f, 1.0f));
            add("ForwardParameter", AnimatorParameter::Type::Float, std::clamp((twoD ? (topDown ? velocity.y : 0) : -local.z) / scale, -1.0f, 1.0f));
            add("VerticalParameter", AnimatorParameter::Type::Float, velocity.y);
            add("GroundedParameter", AnimatorParameter::Type::Bool, grounded ? 1.0f : 0.0f);
            for (const auto& [name, update] : updates)
            {
                auto* p = animator->FindParameter(name);
                if (!p || p->Kind != update.first || !std::isfinite(p->Value) || !std::isfinite(update.second)) return V{ { "Success", false }, { "Speed", speed }, { "Grounded", grounded } };
            }
            for (const auto& [name, update] : updates)
            {
                if (update.first == AnimatorParameter::Type::Bool) animator->SetBool(name, grounded);
                else animator->SetFloatDamped(name, update.second, damping, delta);
            }
            return V{ { "Success", true }, { "Speed", speed }, { "Grounded", grounded } };
        }
        if (kind == "Gameplay.Camera")
        {
            if (!target.HasComponent<CameraComponent>()) throw std::runtime_error("Gameplay camera requires Camera");
            auto anchor = a.Ref(scene, "Anchor");
            if (!anchor || anchor == target) throw std::runtime_error("Camera anchor missing or self");
            if (!m_Cameras.contains(id) && m_Cameras.size() >= 16) throw std::runtime_error("Gameplay camera limit 16");
            auto& record = m_Cameras[id];
            if (record.Handle != uint32_t(target))
            {
                record = {};
                record.Handle = uint32_t(target);
            }
            auto pose = World(target);
            CameraInput input;
            input.Current = pose.Position;
            input.Anchor = scene.GetWorldPosition(anchor) + a.Vec("Eye");
            input.Offset = a.Vec("Offset");
            input.Look = glm::vec2(a.Vec("Look"));
            input.Delta = a.N("Delta");
            input.Damping = a.N("Damping");
            input.Sensitivity = a.N("Sensitivity");
            input.SnapDistance = a.N("SnapDistance");
            input.Snap = a.B("Snap");
            input.Orbit = a.B("Orbit");
            auto next = record.State;
            glm::vec3 position;
            std::string error;
            if (!StepCamera(next, input, position, error)) throw std::runtime_error(error);
            bool blocked = false;
            if (input.Orbit)
            {
                const float radius = a.N("Radius");
                if (radius < .01f || radius > 2) throw std::runtime_error("Camera radius must be .01..2");
                const auto delta = position - input.Anchor;
                const float distance = glm::length(delta);
                if (distance < .05f) throw std::runtime_error("Orbit camera offset is too short");
                for (const auto& hit : scene.SphereCast3DAll(input.Anchor, radius, delta / distance, distance, a.Mask("Mask"), false))
                    if (hit.EntityID != anchor.GetUUID() && hit.EntityID != target.GetUUID())
                    {
                        position = input.Anchor + delta / distance * std::max(.05f, hit.Distance - .03f);
                        blocked = true;
                        break;
                    }
                const auto direction = glm::normalize(input.Anchor - position);
                const auto up = std::abs(glm::dot(direction, glm::vec3(0, 1, 0))) > .98f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                glm::vec3 t, s;
                if (!Math::DecomposeTransform(glm::inverse(glm::lookAt(position, input.Anchor, up)), t, pose.Rotation, s)) throw std::runtime_error("Camera view is invalid");
            }
            else
            {
                const auto minimum = glm::vec2(a.Vec("BoundsMin")), maximum = glm::vec2(a.Vec("BoundsMax"));
                if (minimum.x > maximum.x || minimum.y > maximum.y) throw std::runtime_error("Camera bounds are inverted");
                const auto half = Camera2DFollow::GetHalfView(target.GetComponent<CameraComponent>().Camera.GetProjection(), scene.GetWorldTransform(target), scene.GetWorldPosition(anchor).z);
                auto clamped = Camera2DFollow::ClampToBounds(glm::vec2(position), half, minimum, maximum);
                position.x = clamped.x;
                position.y = clamped.y;
            }
            pose.Position = position;
            SetWorld(target, pose);
            record.State = next;
            record.Anchor = anchor.GetUUID();
            return V{ { "Position", Vector(position) }, { "Blocked", blocked } };
        }
        if (kind.starts_with("Gameplay.Checkpoint"))
        {
            const auto slot = a.S("Slot");
            const auto path = CheckpointPath(slot);
            const bool persistent = a.B("Persistent");
            if (persistent && ++m_IO > 8) throw std::runtime_error("Checkpoint I/O budget exceeds 8 per graph frame");
            try
            {
                if (kind == "Gameplay.CheckpointClear")
                {
                    if (persistent)
                    {
                        std::error_code ec;
                        std::filesystem::remove(path, ec);
                        if (ec) return Status(false, ec.message());
                    }
                    m_Checkpoints.erase(slot);
                    return Status(true);
                }
                Checkpoint point;
                std::string error;
                if (kind == "Gameplay.CheckpointCapture")
                {
                    if (!m_Checkpoints.contains(slot) && m_Checkpoints.size() >= 16) return Status(false, "Checkpoint cache limit 16");
                    const auto pose = World(target);
                    point = { scene.GetName(), uint64_t(target.GetUUID()), pose.Position, pose.Rotation };
                    const auto document = EncodeCheckpoint(point);
                    if (persistent)
                    {
                        std::filesystem::create_directories(path.parent_path());
                        if (!FileSystem::WriteTextFileAtomic(path, document)) return Status(false, "Atomic checkpoint write failed");
                    }
                    m_Checkpoints[slot] = point;
                    return Status(true);
                }
                if (persistent)
                {
                    if (!ReadCheckpoint(path, point, error)) return kind == "Gameplay.CheckpointHas" ? V(false) : Status(false, error);
                }
                else
                {
                    auto found = m_Checkpoints.find(slot);
                    if (found == m_Checkpoints.end()) return kind == "Gameplay.CheckpointHas" ? V(false) : Status(false, "Checkpoint is not captured");
                    point = found->second;
                }
                if (point.Scene != scene.GetName() || point.Actor != uint64_t(target.GetUUID())) return kind == "Gameplay.CheckpointHas" ? V(false) : Status(false, "Checkpoint scene/actor identity differs");
                if (kind == "Gameplay.CheckpointHas") return true;
                const float clearance = a.N("Clearance");
                if (clearance < 0 || clearance > 10) throw std::runtime_error("Checkpoint clearance must be 0..10");
                if (clearance > 0)
                {
                    auto center = point.Position;
                    const auto pose = World(target);
                    const auto matrix = glm::translate(glm::mat4(1), point.Position) * glm::mat4_cast(glm::quat(point.Rotation)) * glm::scale(glm::mat4(1), pose.Scale);
                    if (auto* c = target.TryGetComponent<CharacterController3DComponent>()) center.y += c->Height * .5f * std::abs(pose.Scale.y);
                    else if (auto* box = target.TryGetComponent<BoxCollider2DComponent>()) center = glm::vec3(matrix * glm::vec4(box->Offset, 0, 1));
                    else if (auto* circle = target.TryGetComponent<CircleCollider2DComponent>()) center = glm::vec3(matrix * glm::vec4(circle->Offset, 0, 1));
                    else if (auto* box3D = target.TryGetComponent<BoxCollider3DComponent>()) center = glm::vec3(matrix * glm::vec4(box3D->Offset, 1));
                    auto overlaps = target.HasComponent<Rigidbody2DComponent>() ? scene.OverlapCircle2D(glm::vec2(center), clearance, a.Mask("Mask"), false) : scene.OverlapSphere3D(center, clearance, a.Mask("Mask"), false);
                    if (std::any_of(overlaps.begin(), overlaps.end(), [&](Entity e) { return e != target; })) return Status(false, "Checkpoint position obstructed");
                }
                if (!m_Checkpoints.contains(slot) && m_Checkpoints.size() >= 16) return Status(false, "Checkpoint cache limit 16");
                Restore(target, point);
                m_Checkpoints[slot] = point;
                m_Motors.erase(id);
                for (auto& [camera, record] : m_Cameras)
                    if (record.Anchor == id) record.State.Initialized = false;
                return Status(true);
            }
            catch (const std::exception& e)
            {
                return Status(false, e.what());
            }
        }
        throw std::runtime_error("Unknown gameplay operation");
    }
}
