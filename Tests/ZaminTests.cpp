// Headless unit tests (no window / GPU needed).
#include <Zamin/Zamin.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace Zamin;

// ------------------------------------------------------------------ mini framework
struct TestCase { const char* Name; std::function<void()> Fn; };
static std::vector<TestCase>& Tests() { static std::vector<TestCase> t; return t; }
static int s_Failures = 0;
struct Registrar { Registrar(const char* n, std::function<void()> f) { Tests().push_back({ n, std::move(f) }); } };
#define TEST(name) static void name(); static Registrar s_reg_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) { std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_Failures++; } } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::abs((a) - (b)) <= (eps))

// ------------------------------------------------------------------ test scripts
static int g_Created = 0, g_Updated = 0, g_Destroyed = 0, g_Collisions = 0;
static float g_SeenSpeed = 0.0f;
static std::string g_SeenName;

class TestScript : public ScriptableEntity
{
public:
    float Speed = 1.0f;
    std::string Label = "default";
    glm::vec3 Offset{ 0.0f };

    void OnCreate() override { g_Created++; g_SeenSpeed = Speed; g_SeenName = Label; }
    void OnUpdate(Timestep ts) override { g_Updated++; Transform().Translation.x += Speed * (float)ts; }
    void OnDestroy() override { g_Destroyed++; }
    void OnCollisionBegin(Entity other) override { g_Collisions++; }
};
ZM_REGISTER_SCRIPT(TestScript, ZM_FIELD(TestScript, Speed), ZM_FIELD(TestScript, Label), ZM_FIELD(TestScript, Offset))

class SpawnerScript : public ScriptableEntity
{
public:
    void OnUpdate(Timestep) override
    {
        if (m_Done) return;
        Entity prefab = FindEntity("Prefab");
        Instantiate(prefab, { 5.0f, 0.0f, 0.0f });
        Destroy(prefab);
        m_Done = true;
    }
private:
    bool m_Done = false;
};
ZM_REGISTER_SCRIPT(SpawnerScript)

static void ResetCounters() { g_Created = g_Updated = g_Destroyed = g_Collisions = 0; g_SeenSpeed = 0.0f; g_SeenName.clear(); }

// ------------------------------------------------------------------ tests
TEST(UUIDsAreUnique)
{
    std::set<uint64_t> ids;
    for (int i = 0; i < 10000; i++)
        ids.insert((uint64_t)UUID());
    CHECK(ids.size() == 10000);
}

TEST(EntityLifecycle)
{
    auto scene = CreateRef<Scene>();
    Entity a = scene->CreateEntity("A");
    Entity b = scene->CreateEntity("B");
    CHECK(a.HasComponent<TransformComponent>());
    CHECK(a.GetName() == "A");
    CHECK(scene->FindEntityByName("B") == b);
    CHECK(scene->GetEntityByUUID(a.GetUUID()) == a);

    a.AddComponent<SpriteRendererComponent>(glm::vec4(1, 0, 0, 1));
    Entity c = scene->DuplicateEntity(a);
    CHECK(c.HasComponent<SpriteRendererComponent>());
    CHECK(c.GetUUID() != a.GetUUID());
    CHECK(c.GetComponent<SpriteRendererComponent>().Color.r == 1.0f);

    UUID bID = b.GetUUID();
    scene->DestroyEntity(b);
    CHECK(!scene->FindEntityByName("B"));
    CHECK(!scene->GetEntityByUUID(bID));
}

TEST(SerializerRoundTrip)
{
    auto scene = CreateRef<Scene>();
    scene->SetName("RoundTrip");
    scene->GetSettings().Gravity = { 0.0f, -20.0f };
    Entity e = scene->CreateEntity("Hero");
    e.GetTransform().Translation = { 1, 2, 3 };
    e.GetTransform().Rotation = { 0.1f, 0.2f, 0.3f };
    auto& mr = e.AddComponent<MeshRendererComponent>();
    mr.Type = PrimitiveMesh::Sphere;
    mr.Mat.Metallic = 0.75f;
    auto& rb = e.AddComponent<Rigidbody2DComponent>();
    rb.Type = Rigidbody2DComponent::BodyType::Kinematic;
    e.AddComponent<BoxCollider2DComponent>().Size = { 2.0f, 3.0f };
    auto& cam = e.AddComponent<CameraComponent>();
    cam.Camera.SetProjectionType(SceneCamera::ProjectionType::Orthographic);
    cam.Camera.SetOrthographicSize(22.0f);
    e.AddComponent<TextComponent>().Text = "Oʻzbekiston — Salom";
    auto& nsc = e.AddComponent<NativeScriptComponent>();
    nsc.ClassName = "TestScript";
    ScriptFieldValue speed; speed.Set(7.5f);
    ScriptFieldValue label; label.Set(std::string("hello"));
    nsc.Fields["Speed"] = speed;
    nsc.Fields["Label"] = label;

    std::string text = SceneSerializer(scene).SerializeToString();

    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    CHECK(loaded->GetName() == "RoundTrip");
    CHECK_NEAR(loaded->GetSettings().Gravity.y, -20.0f, 1e-5f);
    Entity l = loaded->GetEntityByUUID(e.GetUUID());
    CHECK((bool)l);
    CHECK(l.GetName() == "Hero");
    CHECK_NEAR(l.GetTransform().Translation.z, 3.0f, 1e-5f);
    CHECK_NEAR(l.GetTransform().Rotation.y, 0.2f, 1e-5f);
    CHECK(l.GetComponent<MeshRendererComponent>().Type == PrimitiveMesh::Sphere);
    CHECK_NEAR(l.GetComponent<MeshRendererComponent>().Mat.Metallic, 0.75f, 1e-5f);
    CHECK(l.GetComponent<Rigidbody2DComponent>().Type == Rigidbody2DComponent::BodyType::Kinematic);
    CHECK_NEAR(l.GetComponent<BoxCollider2DComponent>().Size.y, 3.0f, 1e-5f);
    CHECK(l.GetComponent<CameraComponent>().Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic);
    CHECK_NEAR(l.GetComponent<CameraComponent>().Camera.GetOrthographicSize(), 22.0f, 1e-5f);
    CHECK(l.GetComponent<TextComponent>().Text == "Oʻzbekiston — Salom");
    auto& lnsc = l.GetComponent<NativeScriptComponent>();
    CHECK(lnsc.ClassName == "TestScript");
    CHECK_NEAR(lnsc.Fields["Speed"].Get<float>(), 7.5f, 1e-5f);
    CHECK(lnsc.Fields["Label"].Get<std::string>() == "hello");

    // Serializing again yields identical text (stable ordering).
    CHECK(SceneSerializer(loaded).SerializeToString() == text);
}

TEST(SceneCopyIsIndependent)
{
    auto scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Box");
    e.AddComponent<SpriteRendererComponent>();
    auto copy = Scene::Copy(scene);
    Entity c = copy->GetEntityByUUID(e.GetUUID());
    CHECK((bool)c);
    c.GetTransform().Translation.x = 10.0f;
    CHECK(e.GetTransform().Translation.x == 0.0f);
}

TEST(ScriptLifecycleAndFields)
{
    ResetCounters();
    CHECK(ScriptRegistry::Find("TestScript") != nullptr);
    CHECK(ScriptRegistry::Find("TestScript")->Fields.size() == 3);

    auto scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Scripted");
    auto& nsc = e.AddComponent<NativeScriptComponent>();
    nsc.ClassName = "TestScript";
    ScriptFieldValue v; v.Set(2.0f);
    nsc.Fields["Speed"] = v;
    ScriptFieldValue s; s.Set(std::string("from-inspector"));
    nsc.Fields["Label"] = s;

    scene->OnRuntimeStart();
    CHECK(g_Created == 1);
    CHECK_NEAR(g_SeenSpeed, 2.0f, 1e-6f);
    CHECK(g_SeenName == "from-inspector");
    for (int i = 0; i < 10; i++)
        scene->OnUpdateRuntime(0.1f);
    CHECK(g_Updated == 10);
    CHECK_NEAR(e.GetTransform().Translation.x, 2.0f, 1e-4f);

    scene->SetPaused(true);
    scene->OnUpdateRuntime(0.1f);
    CHECK(g_Updated == 10);
    scene->Step(1);
    scene->OnUpdateRuntime(0.1f);
    CHECK(g_Updated == 11);

    scene->OnRuntimeStop();
    CHECK(g_Destroyed == 1);
}

TEST(PhysicsGravityAndCollision)
{
    ResetCounters();
    auto scene = CreateRef<Scene>();
    Entity ground = scene->CreateEntity("Ground");
    ground.GetTransform().Translation = { 0, -2, 0 };
    ground.GetTransform().Scale = { 20, 1, 1 };
    ground.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
    ground.AddComponent<BoxCollider2DComponent>();

    Entity box = scene->CreateEntity("Box");
    box.GetTransform().Translation = { 0, 3, 0 };
    box.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    box.AddComponent<BoxCollider2DComponent>();
    auto& nsc = box.AddComponent<NativeScriptComponent>();
    nsc.ClassName = "TestScript";
    ScriptFieldValue v; v.Set(0.0f);
    nsc.Fields["Speed"] = v;

    scene->OnRuntimeStart();
    for (int i = 0; i < 180; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    float y = box.GetTransform().Translation.y;
    // Box (half size 0.5) rests on top of the ground (top at -1.5) => y ~= -1.0
    CHECK_NEAR(y, -1.0f, 0.05f);
    CHECK(g_Collisions >= 1);
    scene->OnRuntimeStop();
    CHECK(box.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
}

TEST(InstantiateAndDeferredDestroy)
{
    auto scene = CreateRef<Scene>();
    Entity prefab = scene->CreateEntity("Prefab");
    prefab.AddComponent<CircleRendererComponent>();
    UUID prefabID = prefab.GetUUID();
    Entity spawner = scene->CreateEntity("Spawner");
    spawner.AddComponent<NativeScriptComponent>().ClassName = "SpawnerScript";

    scene->OnRuntimeStart();
    scene->OnUpdateRuntime(0.016f);
    CHECK(!scene->GetEntityByUUID(prefabID));
    Entity clone = scene->FindEntityByName("Prefab");
    CHECK((bool)clone);
    CHECK(clone && clone.HasComponent<CircleRendererComponent>());
    CHECK(clone && std::abs(clone.GetTransform().Translation.x - 5.0f) < 1e-5f);
    scene->OnRuntimeStop();
}

TEST(DecomposeTransform)
{
    TransformComponent t;
    t.Translation = { 1.0f, -2.0f, 3.5f };
    t.Rotation = { 0.3f, -0.7f, 1.1f };
    t.Scale = { 2.0f, 0.5f, 1.5f };
    glm::vec3 tr, rot, sc;
    CHECK(Math::DecomposeTransform(t.GetTransform(), tr, rot, sc));
    CHECK_NEAR(tr.y, -2.0f, 1e-4f);
    CHECK_NEAR(sc.x, 2.0f, 1e-4f);
    TransformComponent r;
    r.Translation = tr; r.Rotation = rot; r.Scale = sc;
    glm::mat4 a = t.GetTransform(), b = r.GetTransform();
    float maxDiff = 0.0f;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            maxDiff = std::max(maxDiff, std::abs(a[i][j] - b[i][j]));
    CHECK(maxDiff < 1e-4f);
}

TEST(ProjectTemplate)
{
    auto dir = std::filesystem::temp_directory_path() / ("zamin-test-" + std::to_string((uint64_t)UUID()));
    auto project = Project::CreateFromTemplate(dir, "TestGame");
    CHECK(project != nullptr);
    CHECK(std::filesystem::exists(dir / "TestGame.zproject"));
    CHECK(std::filesystem::exists(dir / "CMakeLists.txt"));
    CHECK(std::filesystem::exists(dir / "Source" / "Scripts.cpp"));
    CHECK(std::filesystem::exists(dir / "Assets" / "Scenes" / "Main.zscene"));

    auto loaded = Project::Load(dir / "TestGame.zproject");
    CHECK(loaded != nullptr);
    CHECK(loaded && loaded->GetConfig().Name == "TestGame");
    CHECK(Project::GetScriptModulePath().filename() == "libTestGame.so");

    auto scene = CreateRef<Scene>();
    CHECK(SceneSerializer(scene).Deserialize(Project::GetAssetFileSystemPath("Scenes/Main.zscene")));
    CHECK((bool)scene->GetPrimaryCameraEntity());
    CHECK((bool)scene->FindEntityByName("Cube"));

    Project::SetActive(nullptr);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(HierarchyWorldTransforms)
{
    auto scene = CreateRef<Scene>();
    Entity parent = scene->CreateEntity("Parent");
    Entity child = scene->CreateEntity("Child");
    parent.GetTransform().Translation = { 10, 0, 0 };
    parent.GetTransform().Scale = { 2, 2, 2 };
    child.GetTransform().Translation = { 1, 0, 0 };

    CHECK(scene->SetParent(child, parent, false));
    CHECK(scene->GetParent(child) == parent);
    CHECK_NEAR(scene->GetWorldPosition(child).x, 12.0f, 1e-4f);
    CHECK(!scene->SetParent(parent, child)); // cycle rejected

    // keepWorldTransform: unparent keeps the world position
    CHECK(scene->SetParent(child, {}, true));
    CHECK_NEAR(child.GetTransform().Translation.x, 12.0f, 1e-4f);
    CHECK(parent.GetComponent<RelationshipComponent>().Children.empty());

    // re-parent with keepWorld => local position adapts
    CHECK(scene->SetParent(child, parent, true));
    CHECK_NEAR(child.GetTransform().Translation.x, 1.0f, 1e-4f);

    // duplicate copies the subtree with new ids
    Entity copy = scene->DuplicateEntity(parent);
    auto& kids = copy.GetComponent<RelationshipComponent>().Children;
    CHECK(kids.size() == 1);
    CHECK(kids.size() == 1 && kids[0] != child.GetUUID());

    // serialization keeps the hierarchy
    std::string text = SceneSerializer(scene).SerializeToString();
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    Entity lc = loaded->GetEntityByUUID(child.GetUUID());
    CHECK(lc && loaded->GetParent(lc).GetUUID() == parent.GetUUID());
    CHECK(lc && std::abs(loaded->GetWorldPosition(lc).x - 12.0f) < 1e-4f);

    // destroying the parent destroys children
    UUID childID = child.GetUUID();
    scene->DestroyEntity(parent);
    CHECK(!scene->GetEntityByUUID(childID));
}

TEST(PrefabsAndClipboard)
{
    auto scene = CreateRef<Scene>();
    Entity root = scene->CreateEntity("Enemy");
    root.AddComponent<SpriteRendererComponent>();
    Entity gun = scene->CreateEntity("Gun");
    gun.GetTransform().Translation = { 0.5f, 0, 0 };
    scene->SetParent(gun, root, false);

    auto path = std::filesystem::temp_directory_path() / ("zamin-prefab-" + std::to_string((uint64_t)UUID()) + ".zprefab");
    CHECK(scene->SavePrefab(root, path));

    auto other = CreateRef<Scene>();
    Entity a = other->InstantiatePrefab(path);
    Entity b = other->InstantiatePrefab(path);
    CHECK(a && b && a != b);
    CHECK(a && a.GetName() == "Enemy" && a.HasComponent<SpriteRendererComponent>());
    CHECK(a && a.GetUUID() != root.GetUUID());
    CHECK(a && a.GetComponent<RelationshipComponent>().Children.size() == 1);
    if (a && !a.GetComponent<RelationshipComponent>().Children.empty())
    {
        Entity g = other->GetEntityByUUID(a.GetComponent<RelationshipComponent>().Children[0]);
        CHECK(g && g.GetName() == "Gun" && other->GetParent(g) == a);
    }
    CHECK(other->GetRegistry().view<IDComponent>().size() == 4);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(SpriteAnimationFrames)
{
    SpriteAnimationComponent anim;
    anim.Play(4, 4, 10.0f, true);
    CHECK(anim.GetCurrentFrame() == 4);
    anim.Time = 0.25f;
    CHECK(anim.GetCurrentFrame() == 6);
    anim.Time = 0.45f;
    CHECK(anim.GetCurrentFrame() == 4); // looped
    anim.Play(8, 3, 10.0f, false);
    anim.Time = 5.0f;
    CHECK(anim.GetCurrentFrame() == 10);
    CHECK(anim.IsFinished());
}

static int g_Collisions3D = 0;
class Collision3DScript : public ScriptableEntity
{
public:
    void OnCollisionBegin(Entity other) override { if (other.GetName() == "Floor") g_Collisions3D++; }
};
ZM_REGISTER_SCRIPT(Collision3DScript)

TEST(Physics3D)
{
    g_Collisions3D = 0;
    auto scene = CreateRef<Scene>();
    Entity floor = scene->CreateEntity("Floor");
    floor.GetTransform().Scale = { 20, 1, 20 };
    floor.GetTransform().Translation = { 0, -0.5f, 0 };
    floor.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    floor.AddComponent<BoxCollider3DComponent>();

    Entity box = scene->CreateEntity("Box");
    box.GetTransform().Translation = { 0, 5, 0 };
    box.AddComponent<Rigidbody3DComponent>();
    box.AddComponent<BoxCollider3DComponent>();
    box.AddComponent<NativeScriptComponent>().ClassName = "Collision3DScript";

    Entity ball = scene->CreateEntity("Ball");
    ball.GetTransform().Translation = { 3, 2, 0 };
    ball.AddComponent<Rigidbody3DComponent>();
    ball.AddComponent<SphereCollider3DComponent>().Radius = 0.5f;

    scene->OnRuntimeStart();
    CHECK(scene->GetPhysicsWorld3D() != nullptr);
    for (int i = 0; i < 240; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(box.GetTransform().Translation.y, 0.5f, 0.05f);
    CHECK_NEAR(ball.GetTransform().Translation.y, 0.5f, 0.05f);
    CHECK(g_Collisions3D >= 1);

    RaycastHit hit = scene->Raycast3D({ 0, 10, 0 }, { 0, -1, 0 }, 50.0f);
    CHECK(hit.Hit);
    CHECK(hit && hit.EntityID == box.GetUUID());
    CHECK_NEAR(hit.Point.y, 1.0f, 0.05f);
    CHECK_NEAR(hit.Normal.y, 1.0f, 0.05f);
    scene->OnRuntimeStop();
    CHECK(scene->GetPhysicsWorld3D() == nullptr);
}

TEST(Raycast2DAndSceneRequests)
{
    auto scene = CreateRef<Scene>();
    Entity wall = scene->CreateEntity("Wall");
    wall.GetTransform().Translation = { 5, 0, 0 };
    wall.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
    wall.AddComponent<BoxCollider2DComponent>();
    scene->OnRuntimeStart();
    RaycastHit hit = scene->Raycast2D({ 0, 0 }, { 1, 0 }, 20.0f);
    CHECK(hit.Hit && hit.EntityID == wall.GetUUID());
    CHECK_NEAR(hit.Point.x, 4.5f, 1e-3f);
    CHECK_NEAR(hit.Distance, 4.5f, 1e-3f);
    CHECK(!scene->Raycast2D({ 0, 0 }, { -1, 0 }, 20.0f).Hit);

    scene->RequestLoadScene("Scenes/Next.zscene");
    CHECK(scene->GetPendingSceneLoad() == "Scenes/Next.zscene");
    scene->ClearPendingSceneLoad();
    CHECK(scene->GetPendingSceneLoad().empty());
    scene->OnRuntimeStop();
}

TEST(Kinematic3DFollowsTransformAtLowFPS)
{
    // Regression: with several physics sub-steps per frame the kinematic velocity
    // must target the whole frame, otherwise the body oscillates and explodes.
    auto scene = CreateRef<Scene>();
    Entity k = scene->CreateEntity("Kinematic");
    k.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Kinematic;
    k.AddComponent<SphereCollider3DComponent>();
    scene->OnRuntimeStart();
    float maxError = 0.0f;
    for (int i = 0; i < 120; i++)
    {
        k.GetTransform().Translation.y = std::sin(i * 0.1f) * 2.0f;
        scene->OnUpdateRuntime(0.1f); // 10 FPS => 5 sub-steps
        glm::vec3 p; glm::quat q;
        scene->GetPhysicsWorld3D()->GetTransform(k.GetComponent<Rigidbody3DComponent>().RuntimeBodyID, p, q);
        maxError = std::max(maxError, std::abs(p.y - k.GetTransform().Translation.y));
    }
    CHECK(maxError < 0.25f);
    scene->OnRuntimeStop();
}

TEST(TilemapCollisionAndResize)
{
    auto scene = CreateRef<Scene>();
    Entity map = scene->CreateEntity("Map");
    map.GetTransform().Translation = { -5, -2, 0 };
    auto& tm = map.AddComponent<TilemapComponent>();
    tm.Width = 10; tm.Height = 4;
    tm.EnsureSize();
    for (int x = 0; x < 10; x++) tm.Set(x, 0, 1); // floor: top at y = -1
    CHECK(tm.Get(3, 0) == 1 && tm.Get(3, 1) == -1 && tm.Get(-1, 0) == -1);

    Entity box = scene->CreateEntity("Box");
    box.GetTransform().Translation = { 0, 3, 0 };
    box.AddComponent<Rigidbody2DComponent>();
    box.AddComponent<BoxCollider2DComponent>();
    scene->OnRuntimeStart();
    for (int i = 0; i < 180; i++) scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(box.GetTransform().Translation.y, -0.5f, 0.05f);
    RaycastHit hit = scene->Raycast2D({ 0, 5 }, { 0, -1 }, 20.0f);
    CHECK(hit && hit.EntityID == box.GetUUID());
    scene->OnRuntimeStop();

    tm.Resize(20, 2);
    CHECK(tm.Width == 20 && tm.Height == 2 && tm.Tiles.size() == 40);
    CHECK(tm.Get(3, 0) == 1 && tm.Get(15, 0) == -1);

    std::string text = SceneSerializer(scene).SerializeToString();
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    Entity lm = loaded->FindEntityByName("Map");
    CHECK(lm && lm.GetComponent<TilemapComponent>().Get(9, 0) == 1 && lm.GetComponent<TilemapComponent>().Width == 20);
}

TEST(ParticlesSimulate)
{
    auto scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Fx");
    auto& ps = e.AddComponent<ParticleSystemComponent>();
    ps.EmissionRate = 100.0f;
    ps.Burst = 10;
    ps.LifetimeMin = ps.LifetimeMax = 0.5f;
    ps.Gravity = { 0, -10, 0 };
    scene->OnRuntimeStart();
    scene->OnUpdateRuntime(0.1f);
    size_t count = ps.Particles.size();
    CHECK(count >= 15 && count <= 25); // burst 10 + ~10 emitted
    for (int i = 0; i < 10; i++) scene->OnUpdateRuntime(0.1f);
    CHECK(ps.Particles.size() <= 60);    // old ones die after 0.5 s
    ps.Playing = false;
    for (int i = 0; i < 10; i++) scene->OnUpdateRuntime(0.1f);
    CHECK(ps.Particles.empty());
    scene->EmitParticles(e, 5);
    CHECK(ps.Particles.size() == 5);
    scene->OnRuntimeStop();
}

TEST(SkeletalPoseEvaluation)
{
    Skeleton sk;
    sk.Joints.resize(2);
    sk.Joints[0].Name = "Root";
    sk.Joints[1].Name = "Tip";
    sk.Joints[1].Parent = 0;
    sk.Joints[1].RestTranslation = { 0, 1, 0 };
    sk.Joints[1].InverseBind = glm::translate(glm::mat4(1.0f), { 0, -1, 0 });

    // bind pose => identity skinning matrices
    std::vector<glm::mat4> joints;
    EvaluatePose(sk, nullptr, 0.0f, joints);
    CHECK(joints.size() == 2);
    glm::vec4 p = joints[1] * glm::vec4(0, 2, 0, 1);
    CHECK_NEAR(p.y, 2.0f, 1e-5f);

    // rotate the tip joint 90 degrees around Z at t = 1
    AnimationClip clip;
    clip.Duration = 1.0f;
    AnimationChannel ch;
    ch.JointIndex = 1;
    ch.Target = AnimationChannel::Path::Rotation;
    ch.Times = { 0.0f, 1.0f };
    glm::quat q = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    ch.Values = { glm::vec4(0, 0, 0, 1), glm::vec4(q.x, q.y, q.z, q.w) };
    clip.Channels.push_back(ch);

    EvaluatePose(sk, &clip, 1.0f, joints);
    p = joints[1] * glm::vec4(0, 2, 0, 1); // point one unit above the tip joint
    CHECK_NEAR(p.x, -1.0f, 1e-4f);
    CHECK_NEAR(p.y, 1.0f, 1e-4f);

    EvaluatePose(sk, &clip, 0.5f, joints); // slerp halfway = 45 degrees
    p = joints[1] * glm::vec4(0, 2, 0, 1);
    CHECK_NEAR(p.x, -std::sqrt(0.5f), 1e-4f);
}

TEST(CharacterController3D)
{
    auto scene = CreateRef<Scene>();
    Entity floor = scene->CreateEntity("Floor");
    floor.GetTransform().Translation = { 0, -0.5f, 0 };
    floor.GetTransform().Scale = { 40, 1, 40 };
    floor.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    floor.AddComponent<BoxCollider3DComponent>();

    Entity step = scene->CreateEntity("Step"); // 0.25 high stair at x = 3
    step.GetTransform().Translation = { 3.5f, 0.125f, 0 };
    step.GetTransform().Scale = { 3, 0.25f, 4 };
    step.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    step.AddComponent<BoxCollider3DComponent>();

    Entity hero = scene->CreateEntity("Hero");
    hero.GetTransform().Translation = { 0, 2, 0 };
    auto& cc = hero.AddComponent<CharacterController3DComponent>();

    scene->OnRuntimeStart();
    for (int i = 0; i < 120; i++) scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(cc.Grounded);
    CHECK_NEAR(hero.GetTransform().Translation.y, 0.0f, 0.05f);

    // walk onto the stair (step-up)
    cc.Move({ 3.0f, 0, 0 });
    for (int i = 0; i < 90; i++) scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(hero.GetTransform().Translation.x > 3.0f);
    CHECK_NEAR(hero.GetTransform().Translation.y, 0.25f, 0.06f);

    // jump
    cc.Move({ 0, 0, 0 });
    CHECK(cc.Jump(5.0f));
    float maxY = 0.0f;
    for (int i = 0; i < 90; i++)
    {
        scene->OnUpdateRuntime(1.0f / 60.0f);
        maxY = std::max(maxY, hero.GetTransform().Translation.y);
    }
    CHECK(maxY > 1.2f);
    CHECK(cc.Grounded);

    RaycastHit hit = scene->Raycast3D(hero.GetTransform().Translation + glm::vec3(0, 3, 0), { 0, -1, 0 }, 10.0f);
    CHECK(hit && hit.EntityID == hero.GetUUID());
    scene->OnRuntimeStop();
}

TEST(AnimatorStateMachine)
{
    auto scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Hero");
    auto& a = e.AddComponent<AnimatorComponent>();
    a.Parameters = { { "Speed", AnimatorParameter::Type::Float, 0.0f },
                     { "Grounded", AnimatorParameter::Type::Bool, 1.0f },
                     { "Attack", AnimatorParameter::Type::Trigger, 0.0f } };
    a.States = { { "Idle", "Idle" }, { "Run", "Run" }, { "Air", "Jump" }, { "Attack", "Attack", 1.0f, false } };
    AnimatorTransition toRun{ 0, 1 };   toRun.Conditions = { { "Speed", AnimatorCondition::Op::Greater, 0.5f } };
    AnimatorTransition toIdle{ 1, 0 };  toIdle.Conditions = { { "Speed", AnimatorCondition::Op::Less, 0.5f } };
    AnimatorTransition toAir{ -1, 2 };  toAir.Conditions = { { "Grounded", AnimatorCondition::Op::IsFalse } };
    AnimatorTransition land{ 2, 0 };    land.Conditions = { { "Grounded", AnimatorCondition::Op::IsTrue } };
    AnimatorTransition attack{ -1, 3 }; attack.Conditions = { { "Attack", AnimatorCondition::Op::Triggered } };
    AnimatorTransition back{ 3, 0 };    back.HasExitTime = true; back.ExitTime = 1.0f;
    a.Transitions = { toRun, toIdle, toAir, land, attack, back };

    scene->OnRuntimeStart();
    scene->OnUpdateRuntime(0.016f);
    CHECK(std::string(a.GetCurrentStateName()) == "Idle");
    a.SetFloat("Speed", 3.0f);
    scene->OnUpdateRuntime(0.016f);
    CHECK(std::string(a.GetCurrentStateName()) == "Run");
    CHECK(a.Clip == "Run" && a.IsFading());
    a.SetBool("Grounded", false);
    scene->OnUpdateRuntime(0.016f);
    CHECK(std::string(a.GetCurrentStateName()) == "Air");
    a.SetBool("Grounded", true);
    a.SetFloat("Speed", 0.0f);
    scene->OnUpdateRuntime(0.016f);
    CHECK(std::string(a.GetCurrentStateName()) == "Idle");
    a.SetTrigger("Attack");
    scene->OnUpdateRuntime(0.016f);
    CHECK(std::string(a.GetCurrentStateName()) == "Attack");
    CHECK(a.GetFloat("Attack") == 0.0f); // consumed
    scene->OnUpdateRuntime(0.016f);      // no clip => normalized time 1 => exit
    CHECK(std::string(a.GetCurrentStateName()) == "Idle");

    // serialization round trip keeps the graph
    std::string text = SceneSerializer(scene).SerializeToString();
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    auto& la = loaded->FindEntityByName("Hero").GetComponent<AnimatorComponent>();
    CHECK(la.States.size() == 4 && la.Transitions.size() == 6 && la.Parameters.size() == 3);
    CHECK(la.Transitions[0].Conditions.size() == 1 && la.Transitions[5].HasExitTime);
    scene->OnRuntimeStop();
}

TEST(IrradianceSH)
{
    // A uniform environment of radiance 1 must give irradiance/PI == 1 in every direction.
    const int w = 64, h = 32;
    std::vector<float> img((size_t)w * h * 3, 1.0f);
    auto sh = EnvironmentMap::ComputeIrradianceSH(img.data(), w, h, 3);
    CHECK_NEAR(sh[0].x * 0.282095f, 1.0f, 0.03f);
    for (int i = 1; i < 9; i++) CHECK(glm::length(sh[i]) < 0.05f);

    // Sky bright on top, dark below => upward normals receive more light.
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++)
                img[((size_t)y * w + x) * 3 + c] = y >= h / 2 ? 2.0f : 0.0f; // rows >= h/2 are the upper hemisphere
    sh = EnvironmentMap::ComputeIrradianceSH(img.data(), w, h, 3);
    auto eval = [&](glm::vec3 n) { return sh[0].x * 0.282095f + sh[1].x * 0.488603f * n.y + sh[2].x * 0.488603f * n.z + sh[3].x * 0.488603f * n.x; };
    CHECK(eval({ 0, 1, 0 }) > 1.5f);
    CHECK(eval({ 0, -1, 0 }) < 0.5f);
}

// ------------------------------------------------------------------ EntityRef fields
static std::string g_RefName;
static bool g_RefOtherResolved = true;

class RefScript : public ScriptableEntity
{
public:
    EntityRef Target;
    EntityRef Other;

    void OnCreate() override
    {
        Entity t = Resolve(Target);
        g_RefName = t ? t.GetName() : std::string();
        g_RefOtherResolved = (bool)Resolve(Other);
    }
};
ZM_REGISTER_SCRIPT(RefScript, ZM_FIELD(RefScript, Target), ZM_FIELD(RefScript, Other))

static void SetRefField(Entity e, const char* field, uint64_t id)
{
    ScriptFieldValue v;
    v.Set(EntityRef{ id });
    e.GetComponent<NativeScriptComponent>().Fields[field] = v;
}

static uint64_t GetRefField(Entity e, const char* field)
{
    auto& fields = e.GetComponent<NativeScriptComponent>().Fields;
    auto it = fields.find(field);
    return it != fields.end() && it->second.Type == ScriptFieldType::Entity ? it->second.Get<EntityRef>().ID : 0;
}

TEST(EntityRefSerialization)
{
    CHECK(ScriptFieldTypeOf<EntityRef>() == ScriptFieldType::Entity);
    CHECK(ScriptFieldTypeFromString(ScriptFieldTypeToString(ScriptFieldType::Entity)) == ScriptFieldType::Entity);
    const ScriptClassInfo* info = ScriptRegistry::Find("RefScript");
    CHECK(info && info->Fields.size() == 2);
    CHECK(info && info->Fields[0].Type == ScriptFieldType::Entity && info->Fields[0].Name == "Target");
    CHECK(!EntityRef{} && (bool)EntityRef{ 5 });

    // Copy between a stored value and a live field.
    EntityRef live{ 0xFEDCBA9876543210ull };
    ScriptFieldValue v;
    v.Type = ScriptFieldType::Entity;
    v.ReadFrom(&live);
    CHECK(v.Get<EntityRef>().ID == live.ID);
    EntityRef out;
    v.WriteTo(&out);
    CHECK(out.ID == live.ID);

    auto scene = CreateRef<Scene>();
    Entity target = scene->CreateEntity("Target");
    Entity holder = scene->CreateEntity("Holder");
    holder.AddComponent<NativeScriptComponent>().ClassName = "RefScript";
    SetRefField(holder, "Target", target.GetUUID());
    SetRefField(holder, "Other", 0xFEDCBA9876543210ull); // dangling, full 64-bit ID
    Entity empty = scene->CreateEntity("Empty");
    empty.AddComponent<NativeScriptComponent>().ClassName = "RefScript";
    SetRefField(empty, "Target", 0);

    std::string text = SceneSerializer(scene).SerializeToString();
    CHECK(text.find("\"Entity\"") != std::string::npos);

    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    Entity lh = loaded->FindEntityByName("Holder");
    CHECK((bool)lh);
    CHECK(lh && lh.GetComponent<NativeScriptComponent>().Fields["Target"].Type == ScriptFieldType::Entity);
    CHECK(lh && GetRefField(lh, "Target") == (uint64_t)target.GetUUID());
    CHECK(lh && GetRefField(lh, "Other") == 0xFEDCBA9876543210ull); // no precision loss
    CHECK(GetRefField(loaded->FindEntityByName("Empty"), "Target") == 0);
    CHECK(SceneSerializer(loaded).SerializeToString() == text);

    // Resolved at runtime on a runtime copy of the scene (like the editor's Play).
    auto runtime = Scene::Copy(loaded);
    g_RefName = "?";
    g_RefOtherResolved = true;
    Entity rh = runtime->FindEntityByName("Holder");
    runtime->DestroyEntity(runtime->FindEntityByName("Empty"));
    runtime->OnRuntimeStart();
    CHECK(g_RefName == "Target");
    CHECK(!g_RefOtherResolved);
    auto* script = ScriptableEntity::GetScript<RefScript>(rh);
    CHECK(script && script->Target.ID == (uint64_t)target.GetUUID());
    CHECK(script && script->Resolve(script->Target) == runtime->FindEntityByName("Target"));
    CHECK(script && !script->Resolve(EntityRef{}));
    runtime->OnRuntimeStop();
}

TEST(EntityRefRemapOnCopy)
{
    auto scene = CreateRef<Scene>();
    Entity outside = scene->CreateEntity("Outside");
    Entity root = scene->CreateEntity("Root");
    Entity child = scene->CreateEntity("Child");
    CHECK(scene->SetParent(child, root, false));
    root.AddComponent<NativeScriptComponent>().ClassName = "RefScript";
    SetRefField(root, "Target", child.GetUUID());   // inside the subtree
    SetRefField(root, "Other", outside.GetUUID());  // outside the subtree

    // Duplicate (Ctrl+D / Instantiate): inner references follow the copy.
    Entity copy = scene->DuplicateEntity(root);
    auto& copyChildren = copy.GetComponent<RelationshipComponent>().Children;
    CHECK(copyChildren.size() == 1);
    if (copyChildren.size() == 1)
    {
        CHECK(copyChildren[0] != child.GetUUID());
        CHECK(GetRefField(copy, "Target") == (uint64_t)copyChildren[0]);
    }
    CHECK(GetRefField(copy, "Other") == (uint64_t)outside.GetUUID());
    CHECK(GetRefField(root, "Target") == (uint64_t)child.GetUUID()); // original untouched

    // Clipboard / prefab path: serialize a subtree and paste it with fresh IDs.
    std::string text = SceneSerializer::SerializeEntities(*scene, { root, child });
    auto pasted = SceneSerializer::DeserializeEntities(*scene, text, true);
    CHECK(pasted.size() == 2);
    if (pasted.size() == 2)
    {
        Entity pastedRoot = pasted[0], pastedChild = pasted[1];
        CHECK(pastedRoot.GetName() == "Root" && pastedChild.GetName() == "Child");
        CHECK(pastedChild.GetUUID() != child.GetUUID());
        CHECK(GetRefField(pastedRoot, "Target") == (uint64_t)pastedChild.GetUUID());
        CHECK(GetRefField(pastedRoot, "Other") == (uint64_t)outside.GetUUID());
    }

    // Instantiate at runtime: the spawned script resolves its own child.
    scene->OnRuntimeStart();
    Entity spawned = scene->Instantiate(root);
    auto* script = ScriptableEntity::GetScript<RefScript>(spawned);
    CHECK(script != nullptr);
    if (script)
    {
        Entity resolved = script->Resolve(script->Target);
        CHECK(resolved && resolved.GetName() == "Child");
        CHECK(resolved && scene->GetParent(resolved) == spawned);
    }
    scene->OnRuntimeStop();
}

// ------------------------------------------------------------------ timers (Invoke)
static int g_InvokeOnce = 0, g_InvokeRepeat = 0, g_InvokeChained = 0, g_SelfCancel = 0;

class TimerScript : public ScriptableEntity
{
public:
    void OnCreate() override
    {
        Invoke(0.5f, [] { g_InvokeOnce++; });
        InvokeRepeating(0.25f, 0.5f, [] { g_InvokeRepeat++; });
        // A callback that schedules another timer (re-entrant use).
        Invoke(0.25f, [this] { Invoke(0.25f, [] { g_InvokeChained++; }); });
    }
};
ZM_REGISTER_SCRIPT(TimerScript)

class SelfCancelScript : public ScriptableEntity
{
public:
    void OnCreate() override
    {
        InvokeRepeating(0.0f, 0.125f, [this] {
            if (++g_SelfCancel == 3)
                CancelInvokes(); // cancel from inside the running callback
        });
    }
};
ZM_REGISTER_SCRIPT(SelfCancelScript)

TEST(InvokeTimers)
{
    g_InvokeOnce = g_InvokeRepeat = g_InvokeChained = g_SelfCancel = 0;
    auto scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Timers");
    e.AddComponent<NativeScriptComponent>().ClassName = "TimerScript";
    Entity c = scene->CreateEntity("SelfCancel");
    c.AddComponent<NativeScriptComponent>().ClassName = "SelfCancelScript";

    const float dt = 0.125f; // exactly representable: no float drift in the counters
    auto step = [&](int frames) { for (int i = 0; i < frames; i++) scene->OnUpdateRuntime(dt); };

    scene->OnRuntimeStart();
    auto* timers = ScriptableEntity::GetScript<TimerScript>(e);
    CHECK(timers && timers->IsInvoking());

    step(1); // t = 0.125
    CHECK(g_InvokeRepeat == 0 && g_InvokeOnce == 0);
    step(1); // t = 0.25: first repeat + the chain schedules its inner timer
    CHECK(g_InvokeRepeat == 1);
    CHECK(g_InvokeChained == 0);
    step(1); // t = 0.375
    CHECK(g_InvokeOnce == 0);
    step(1); // t = 0.5
    CHECK(g_InvokeOnce == 1);
    CHECK(g_InvokeChained == 1);
    step(12); // t = 2.0: repeats at 0.25, 0.75, 1.25, 1.75
    CHECK(g_InvokeOnce == 1);
    CHECK(g_InvokeChained == 1);
    CHECK(g_InvokeRepeat == 4);

    // Self-cancelling repeat stopped after 3 calls.
    CHECK(g_SelfCancel == 3);
    auto* selfCancel = ScriptableEntity::GetScript<SelfCancelScript>(c);
    CHECK(selfCancel && !selfCancel->IsInvoking());

    // Paused scenes do not tick timers.
    scene->SetPaused(true);
    step(8);
    CHECK(g_InvokeRepeat == 4);
    scene->SetPaused(false);
    step(2); // t = 2.25
    CHECK(g_InvokeRepeat == 5);

    // CancelInvokes stops everything.
    if (timers)
    {
        timers->CancelInvokes();
        CHECK(!timers->IsInvoking());
        // Invoke from outside a frame (e.g. from another script) still works after a cancel.
        int late = 0;
        timers->Invoke(0.25f, [&late] { late++; });
        step(8);
        CHECK(late == 1);
        CHECK(!timers->IsInvoking());
    }
    CHECK(g_InvokeRepeat == 5);
    scene->OnRuntimeStop();
}

// ------------------------------------------------------------------ tweens / easing
static int g_TweenComplete = 0, g_TweenCalls = 0;
static float g_TweenLast = -1.0f;

class TweenScript : public ScriptableEntity
{
public:
    void OnCreate() override
    {
        TweenPosition({ 4.0f, 0.0f, 0.0f }, 1.0f, Ease::Linear, [this] {
            g_TweenComplete++;
            TweenScale(glm::vec3(3.0f), 0.5f, Ease::Linear); // chained from the completion callback
        });
        Tween(0.5f, [](float t) { g_TweenLast = t; g_TweenCalls++; }, Ease::InQuad);
    }
};
ZM_REGISTER_SCRIPT(TweenScript)

TEST(EasingFunctions)
{
    const Ease all[] = { Ease::Linear, Ease::InQuad, Ease::OutQuad, Ease::InOutQuad, Ease::OutCubic,
                         Ease::InOutCubic, Ease::OutBack, Ease::OutElastic, Ease::OutBounce };
    for (Ease e : all)
    {
        CHECK_NEAR(ApplyEase(e, 0.0f), 0.0f, 1e-5f);
        CHECK_NEAR(ApplyEase(e, 1.0f), 1.0f, 1e-5f);
        // input is clamped to [0, 1]
        CHECK_NEAR(ApplyEase(e, -0.5f), ApplyEase(e, 0.0f), 1e-6f);
        CHECK_NEAR(ApplyEase(e, 1.5f), ApplyEase(e, 1.0f), 1e-6f);
    }

    CHECK_NEAR(ApplyEase(Ease::Linear, 0.25f), 0.25f, 1e-6f);
    CHECK_NEAR(ApplyEase(Ease::InQuad, 0.5f), 0.25f, 1e-6f);
    CHECK_NEAR(ApplyEase(Ease::OutQuad, 0.5f), 0.75f, 1e-6f);
    CHECK_NEAR(ApplyEase(Ease::InOutQuad, 0.25f), 0.125f, 1e-6f);
    CHECK_NEAR(ApplyEase(Ease::InOutQuad, 0.5f), 0.5f, 1e-6f);
    CHECK_NEAR(ApplyEase(Ease::InOutQuad, 0.75f), 0.875f, 1e-6f);
    CHECK_NEAR(ApplyEase(Ease::OutCubic, 0.5f), 0.875f, 1e-6f);
    CHECK_NEAR(ApplyEase(Ease::InOutCubic, 0.25f), 0.0625f, 1e-6f);
    CHECK_NEAR(ApplyEase(Ease::InOutCubic, 0.5f), 0.5f, 1e-6f);
    CHECK_NEAR(ApplyEase(Ease::OutBounce, 1.0f / 2.75f), 1.0f, 1e-5f); // first bounce touches the ground
    CHECK_NEAR(ApplyEase(Ease::OutBounce, 0.5f), 0.765625f, 1e-5f);

    // Monotonic curves never go backwards; InOut curves are point-symmetric.
    const Ease monotonic[] = { Ease::Linear, Ease::InQuad, Ease::OutQuad, Ease::InOutQuad, Ease::OutCubic, Ease::InOutCubic };
    for (Ease e : monotonic)
        for (int i = 1; i <= 100; i++)
            CHECK(ApplyEase(e, i / 100.0f) >= ApplyEase(e, (i - 1) / 100.0f) - 1e-6f);
    for (Ease e : { Ease::InOutQuad, Ease::InOutCubic })
        for (int i = 0; i <= 20; i++)
            CHECK_NEAR(ApplyEase(e, i / 20.0f) + ApplyEase(e, 1.0f - i / 20.0f), 1.0f, 1e-5f);

    // Back / elastic overshoot past 1, bounce stays inside [0, 1].
    float maxBack = 0.0f, maxElastic = 0.0f, minBounce = 1.0f, maxBounce = 0.0f;
    for (int i = 0; i <= 200; i++)
    {
        float t = i / 200.0f;
        maxBack = std::max(maxBack, ApplyEase(Ease::OutBack, t));
        maxElastic = std::max(maxElastic, ApplyEase(Ease::OutElastic, t));
        minBounce = std::min(minBounce, ApplyEase(Ease::OutBounce, t));
        maxBounce = std::max(maxBounce, ApplyEase(Ease::OutBounce, t));
    }
    CHECK(maxBack > 1.05f && maxBack < 1.15f);
    CHECK(maxElastic > 1.1f);
    CHECK(minBounce >= 0.0f && maxBounce <= 1.0f + 1e-5f);
}

TEST(TweensRunAndChain)
{
    g_TweenComplete = g_TweenCalls = 0;
    g_TweenLast = -1.0f;
    auto scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Tweened");
    e.AddComponent<NativeScriptComponent>().ClassName = "TweenScript";
    const float dt = 0.125f;
    auto step = [&](int frames) { for (int i = 0; i < frames; i++) scene->OnUpdateRuntime(dt); };

    scene->OnRuntimeStart();
    auto* script = ScriptableEntity::GetScript<TweenScript>(e);
    CHECK(script && script->IsTweening());

    step(2); // t = 0.25
    CHECK_NEAR(e.GetTransform().Translation.x, 1.0f, 1e-5f);
    CHECK_NEAR(g_TweenLast, 0.25f, 1e-6f); // InQuad(0.5)
    step(2); // t = 0.5: generic tween finished
    CHECK_NEAR(e.GetTransform().Translation.x, 2.0f, 1e-5f);
    CHECK_NEAR(g_TweenLast, 1.0f, 1e-6f);
    CHECK(g_TweenCalls == 4);
    step(4); // t = 1.0: position tween finished, scale tween chained
    CHECK_NEAR(e.GetTransform().Translation.x, 4.0f, 1e-5f);
    CHECK(g_TweenComplete == 1);
    CHECK(g_TweenCalls == 4);
    CHECK_NEAR(e.GetTransform().Scale.x, 1.0f, 1e-5f);
    step(2); // t = 1.25: halfway through the scale tween
    CHECK_NEAR(e.GetTransform().Scale.y, 2.0f, 1e-5f);
    step(2); // t = 1.5
    CHECK_NEAR(e.GetTransform().Scale.z, 3.0f, 1e-5f);
    step(4);
    CHECK(g_TweenComplete == 1);
    CHECK_NEAR(e.GetTransform().Translation.x, 4.0f, 1e-5f);
    CHECK_NEAR(e.GetTransform().Scale.x, 3.0f, 1e-5f);
    CHECK(script && !script->IsTweening());

    // CancelTweens freezes the value where it is.
    if (script)
    {
        script->TweenPosition({ 0.0f, 0.0f, 0.0f }, 1.0f, Ease::Linear);
        step(2);
        CHECK_NEAR(e.GetTransform().Translation.x, 3.0f, 1e-5f);
        script->CancelTweens();
        CHECK(!script->IsTweening());
        step(8);
        CHECK_NEAR(e.GetTransform().Translation.x, 3.0f, 1e-5f);
    }
    scene->OnRuntimeStop();
}

// ------------------------------------------------------------------ input action map
static bool SameBindings(const std::vector<InputBinding>& a, const std::vector<InputBinding>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (!(a[i] == b[i])) return false;
    return true;
}

TEST(InputActionMapDefaults)
{
    InputActionMap map = InputActionMap::CreateDefault();
    const InputAction* jump = map.FindAction("Jump");
    CHECK(jump != nullptr);
    CHECK(jump && std::find(jump->Bindings.begin(), jump->Bindings.end(), InputBinding::FromKey(Key::Space)) != jump->Bindings.end());
    CHECK(jump && std::find(jump->Bindings.begin(), jump->Bindings.end(), InputBinding::FromGamepadButton(GamepadButton::A)) != jump->Bindings.end());
    const InputAction* fire = map.FindAction("Fire");
    CHECK(fire && std::find(fire->Bindings.begin(), fire->Bindings.end(), InputBinding::FromMouse(MouseButton::Left)) != fire->Bindings.end());
    CHECK(map.FindAction("Nope") == nullptr);

    const InputAxis* h = map.FindAxis("Horizontal");
    const InputAxis* v = map.FindAxis("Vertical");
    CHECK(h && v);
    if (h && v)
    {
        CHECK(h->GamepadAxisCode == (int)GamepadAxis::LeftX && !h->Invert);
        CHECK(v->GamepadAxisCode == (int)GamepadAxis::LeftY && v->Invert);
        CHECK(!h->Negative.empty() && h->Negative[0] == InputBinding::FromKey(Key::A));
        CHECK(!h->Positive.empty() && h->Positive[0] == InputBinding::FromKey(Key::D));
        CHECK(!v->Positive.empty() && v->Positive[0] == InputBinding::FromKey(Key::W));
        CHECK(h->Deadzone > 0.0f && h->Deadzone < 0.5f);
    }
    CHECK(map.MakeUniqueActionName("Jump") == "Jump 2");
    CHECK(map.MakeUniqueActionName("Dash") == "Dash");
    CHECK(map.MakeUniqueAxisName("Vertical") == "Vertical 2");

    // Without a project the default map is used.
    Project::SetActive(nullptr);
    CHECK(Project::GetInputActionMap().FindAction("Jump") != nullptr);
}

TEST(InputBindingNames)
{
    // Every named key survives a string round trip and names are unique.
    std::set<std::string> names;
    for (Key k : GetAllKeys())
    {
        std::string name = KeyToString(k);
        CHECK(!name.empty());
        names.insert(name);
        Key back;
        CHECK(KeyFromString(name, back) && back == k);
        InputBinding b;
        CHECK(InputBindingFromString(InputBindingToString(InputBinding::FromKey(k)), b) && b == InputBinding::FromKey(k));
    }
    CHECK(names.size() == GetAllKeys().size());
    CHECK(GetAllKeys().size() > 100);

    for (int i = 0; i < MouseButtonCount; i++)
    {
        InputBinding b, src = InputBinding::FromMouse((MouseButton)i);
        CHECK(InputBindingFromString(InputBindingToString(src), b) && b == src);
    }
    for (int i = 0; i < GamepadButtonCount; i++)
    {
        InputBinding b, src = InputBinding::FromGamepadButton((GamepadButton)i);
        CHECK(InputBindingFromString(InputBindingToString(src), b) && b == src);
    }
    for (int i = 0; i < GamepadAxisCount; i++)
        for (int dir : { 1, -1 })
        {
            InputBinding b, src = InputBinding::FromGamepadAxis((GamepadAxis)i, dir);
            CHECK(InputBindingFromString(InputBindingToString(src), b) && b == src && b.Direction == dir);
        }

    CHECK(InputBindingToString(InputBinding::FromKey(Key::Space)) == "Key:Space");
    CHECK(InputBindingToString(InputBinding::FromGamepadAxis(GamepadAxis::LeftY, -1)) == "GamepadAxis:LeftY-");
    InputBinding b;
    CHECK(InputBindingFromString("Mouse:Right", b) && b == InputBinding::FromMouse(MouseButton::Right));
    CHECK(InputBindingFromString("GamepadAxis:RightTrigger", b) && b == InputBinding::FromGamepadAxis(GamepadAxis::RightTrigger, 1));
    CHECK(!InputBindingFromString("Key:NoSuchKey", b));
    CHECK(!InputBindingFromString("Joystick:A", b));
    CHECK(!InputBindingFromString("Space", b));
    CHECK(!(InputBinding::FromGamepadAxis(GamepadAxis::LeftX, 1) == InputBinding::FromGamepadAxis(GamepadAxis::LeftX, -1)));
    CHECK(!(InputBinding::FromKey(Key::A) == InputBinding::FromGamepadButton(GamepadButton::A)));
}

TEST(InputActionMapSerialization)
{
    InputActionMap map;
    map.Actions.push_back({ "Dash", { InputBinding::FromKey(Key::LeftShift), InputBinding::FromGamepadAxis(GamepadAxis::LeftTrigger, 1) } });
    map.Actions.push_back({ "Shoot", { InputBinding::FromMouse(MouseButton::Right) } });
    map.Actions.push_back({ "Unbound", {} });
    InputAxis steer;
    steer.Name = "Steer";
    steer.Negative = { InputBinding::FromKey(Key::Q) };
    steer.Positive = { InputBinding::FromKey(Key::E), InputBinding::FromGamepadButton(GamepadButton::RightBumper) };
    steer.GamepadAxisCode = (int)GamepadAxis::RightX;
    steer.Invert = true;
    steer.Deadzone = 0.3f;
    InputAxis none;
    none.Name = "NoStick";
    map.Axes = { steer, none };

    std::string text = ProjectSerializer::InputMapToString(map);
    InputActionMap loaded;
    CHECK(ProjectSerializer::InputMapFromString(text, loaded));
    CHECK(loaded.Actions.size() == 3 && loaded.Axes.size() == 2);
    if (loaded.Actions.size() == 3 && loaded.Axes.size() == 2)
    {
        for (size_t i = 0; i < 3; i++)
        {
            CHECK(loaded.Actions[i].Name == map.Actions[i].Name);
            CHECK(SameBindings(loaded.Actions[i].Bindings, map.Actions[i].Bindings));
        }
        const InputAxis& a = loaded.Axes[0];
        CHECK(a.Name == "Steer" && a.Invert && a.GamepadAxisCode == (int)GamepadAxis::RightX);
        CHECK_NEAR(a.Deadzone, 0.3f, 1e-6f);
        CHECK(SameBindings(a.Negative, steer.Negative) && SameBindings(a.Positive, steer.Positive));
        CHECK(loaded.Axes[1].GamepadAxisCode == -1 && !loaded.Axes[1].Invert);
    }
    CHECK(ProjectSerializer::InputMapToString(loaded) == text);
    Log::SetMinLevel(LogLevel::Critical); // the parse error below is expected
    CHECK(!ProjectSerializer::InputMapFromString("{ not json", loaded));
    Log::SetMinLevel(LogLevel::Error);

    // Unknown bindings are skipped, the rest is kept.
    CHECK(ProjectSerializer::InputMapFromString(R"({"Actions":[{"Name":"Jump","Bindings":["Key:Space","Key:Bogus",5]}],"Axes":[]})", loaded));
    CHECK(loaded.Actions.size() == 1 && loaded.Actions[0].Bindings.size() == 1);
}

TEST(ProjectInputMapRoundTrip)
{
    auto dir = std::filesystem::temp_directory_path() / ("zamin-input-" + std::to_string((uint64_t)UUID()));
    auto project = Project::CreateFromTemplate(dir, "InputGame");
    CHECK(project != nullptr);
    if (project)
    {
        // New projects start with the default actions.
        CHECK(project->GetConfig().InputActions.FindAction("Jump") != nullptr);
        CHECK(Project::GetInputActionMap().FindAxis("Horizontal") != nullptr);

        // Edit + save + reload.
        auto& map = project->GetConfig().InputActions;
        map.FindAction("Jump")->Bindings = { InputBinding::FromKey(Key::K) };
        map.Actions.push_back({ "Dash", { InputBinding::FromKey(Key::LeftAlt) } });
        map.Axes.erase(map.Axes.begin()); // drop Horizontal
        CHECK(Project::SaveActive(dir / "InputGame.zproject"));

        auto loaded = Project::Load(dir / "InputGame.zproject");
        CHECK(loaded != nullptr);
        if (loaded)
        {
            const auto& lm = loaded->GetConfig().InputActions;
            CHECK(lm.FindAction("Jump") && SameBindings(lm.FindAction("Jump")->Bindings, { InputBinding::FromKey(Key::K) }));
            CHECK(lm.FindAction("Dash") != nullptr);
            CHECK(lm.FindAxis("Horizontal") == nullptr);
            CHECK(lm.FindAxis("Vertical") != nullptr);
            CHECK(&Project::GetInputActionMap() == &lm);
        }

        // An older project file without an input section gets the defaults.
        CHECK(FileSystem::WriteTextFile(dir / "Old.zproject", R"({"Project":{"Name":"Old","StartScene":"Scenes/Main.zscene"}})"));
        auto old = Project::Load(dir / "Old.zproject");
        CHECK(old && old->GetConfig().InputActions.FindAction("Jump") && old->GetConfig().InputActions.FindAxis("Horizontal"));
    }
    Project::SetActive(nullptr);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// Advances the simulated input by one frame.
static void NextFrame() { Input::Update(); }

TEST(InputKeyAndMouseFrames)
{
    Input::ResetState();
    NextFrame();
    CHECK(!Input::IsKeyPressed(Key::Space) && !Input::IsKeyJustPressed(Key::Space) && !Input::IsKeyJustReleased(Key::Space));

    // A press is visible from the next frame on and "just pressed" for exactly one frame.
    Input::OnKeyEvent(Key::Space, true);
    CHECK(!Input::IsKeyPressed(Key::Space));
    NextFrame();
    CHECK(Input::IsKeyPressed(Key::Space) && Input::IsKeyJustPressed(Key::Space) && !Input::IsKeyJustReleased(Key::Space));
    NextFrame();
    CHECK(Input::IsKeyPressed(Key::Space) && !Input::IsKeyJustPressed(Key::Space));
    NextFrame();
    CHECK(Input::IsKeyPressed(Key::Space) && !Input::IsKeyJustPressed(Key::Space));

    Input::OnKeyEvent(Key::Space, false);
    NextFrame();
    CHECK(!Input::IsKeyPressed(Key::Space) && Input::IsKeyJustReleased(Key::Space) && !Input::IsKeyJustPressed(Key::Space));
    NextFrame();
    CHECK(!Input::IsKeyJustReleased(Key::Space));

    // A tap between two frames still reports both edges once.
    Input::OnKeyEvent(Key::E, true);
    Input::OnKeyEvent(Key::E, false);
    NextFrame();
    CHECK(!Input::IsKeyPressed(Key::E) && Input::IsKeyJustPressed(Key::E) && Input::IsKeyJustReleased(Key::E));
    NextFrame();
    CHECK(!Input::IsKeyJustPressed(Key::E) && !Input::IsKeyJustReleased(Key::E));

    // Released and pressed again between two frames: a new press.
    Input::OnKeyEvent(Key::A, true);
    NextFrame();
    NextFrame();
    Input::OnKeyEvent(Key::A, false);
    Input::OnKeyEvent(Key::A, true);
    NextFrame();
    CHECK(Input::IsKeyPressed(Key::A) && Input::IsKeyJustPressed(Key::A) && Input::IsKeyJustReleased(Key::A));
    CHECK(!Input::IsKeyPressed(Key::D));

    // Codes outside the key range are ignored.
    Input::OnKeyEvent((Key)5, true);
    Input::OnKeyEvent((Key)60000, true);
    NextFrame();
    CHECK(!Input::IsKeyPressed((Key)5) && !Input::IsKeyPressed((Key)60000) && !Input::IsKeyJustPressed((Key)60000));

    // Mouse: buttons, position delta (no jump on the first frame) and scroll.
    Input::ResetState();
    Input::OnMouseMovedEvent(100.0f, 50.0f);
    NextFrame();
    CHECK(Input::GetMousePosition() == glm::vec2(100.0f, 50.0f));
    CHECK(Input::GetMouseDelta() == glm::vec2(0.0f));
    Input::OnMouseButtonEvent(MouseButton::Left, true);
    Input::OnMouseMovedEvent(105.0f, 47.0f);
    Input::OnMouseMovedEvent(110.0f, 45.0f);
    Input::OnMouseScrolledEvent(0.0f, 1.0f);
    Input::OnMouseScrolledEvent(0.5f, 2.0f);
    NextFrame();
    CHECK(Input::IsMouseButtonPressed(MouseButton::Left) && Input::IsMouseButtonJustPressed(MouseButton::Left));
    CHECK(!Input::IsMouseButtonPressed(MouseButton::Right) && !Input::IsMouseButtonJustPressed(MouseButton::Right));
    CHECK(Input::GetMouseDelta() == glm::vec2(10.0f, -5.0f));
    CHECK(Input::GetMouseScrollDelta() == glm::vec2(0.5f, 3.0f));
    CHECK_NEAR(Input::GetMouseX(), 110.0f, 1e-6f);
    NextFrame();
    CHECK(Input::IsMouseButtonPressed(MouseButton::Left) && !Input::IsMouseButtonJustPressed(MouseButton::Left));
    CHECK(Input::GetMouseDelta() == glm::vec2(0.0f) && Input::GetMouseScrollDelta() == glm::vec2(0.0f));
    Input::OnMouseButtonEvent(MouseButton::Left, false);
    NextFrame();
    CHECK(!Input::IsMouseButtonPressed(MouseButton::Left) && Input::IsMouseButtonJustReleased(MouseButton::Left));
    NextFrame();
    CHECK(!Input::IsMouseButtonJustReleased(MouseButton::Left));
    Input::OnMouseButtonEvent((MouseButton)200, true);
    NextFrame();
    CHECK(!Input::IsMouseButtonPressed((MouseButton)200));

    // Gamepad buttons follow the same frame rules (polled state).
    CHECK(!Input::IsGamepadConnected(0));
    Input::SimulateGamepadButton(0, GamepadButton::B, true);
    NextFrame();
    CHECK(Input::IsGamepadConnected(0) && Input::IsGamepadButtonPressed(GamepadButton::B) && Input::IsGamepadButtonJustPressed(GamepadButton::B));
    CHECK(!Input::IsGamepadConnected(1) && !Input::IsGamepadButtonPressed(GamepadButton::B, 1));
    NextFrame();
    CHECK(!Input::IsGamepadButtonJustPressed(GamepadButton::B));
    Input::SimulateGamepadButton(0, GamepadButton::B, false);
    NextFrame();
    CHECK(Input::IsGamepadButtonJustReleased(GamepadButton::B) && !Input::IsGamepadButtonPressed(GamepadButton::B));
    CHECK(!Input::IsGamepadConnected(-1) && !Input::IsGamepadConnected(Input::MaxGamepads));

    Input::ResetState();
    CHECK(!Input::IsKeyPressed(Key::A) && !Input::IsGamepadConnected(0));
}

TEST(InputActionQueries)
{
    Project::SetActive(nullptr); // default map: Jump = Space / Gamepad A, Fire = Mouse Left / LeftControl / RightTrigger
    Input::ResetState();
    NextFrame();
    CHECK(!Input::IsActionPressed("Jump") && !Input::IsActionJustPressed("Jump") && !Input::IsActionJustReleased("Jump"));

    Input::OnKeyEvent(Key::Space, true);
    NextFrame();
    CHECK(Input::IsActionPressed("Jump") && Input::IsActionJustPressed("Jump") && !Input::IsActionJustReleased("Jump"));
    NextFrame();
    CHECK(Input::IsActionPressed("Jump") && !Input::IsActionJustPressed("Jump"));

    // A second binding while the action is held is not a new press ...
    Input::SimulateGamepadButton(0, GamepadButton::A, true);
    NextFrame();
    CHECK(Input::IsActionPressed("Jump") && !Input::IsActionJustPressed("Jump"));
    // ... and releasing one of two held bindings is not a release.
    Input::OnKeyEvent(Key::Space, false);
    NextFrame();
    CHECK(Input::IsActionPressed("Jump") && !Input::IsActionJustReleased("Jump") && !Input::IsActionJustPressed("Jump"));
    Input::SimulateGamepadButton(0, GamepadButton::A, false);
    NextFrame();
    CHECK(!Input::IsActionPressed("Jump") && Input::IsActionJustReleased("Jump") && !Input::IsActionJustPressed("Jump"));
    NextFrame();
    CHECK(!Input::IsActionJustReleased("Jump"));

    // Gamepad-only press, and a second gamepad.
    Input::SimulateGamepadButton(0, GamepadButton::A, true);
    NextFrame();
    CHECK(Input::IsActionJustPressed("Jump"));
    Input::SimulateGamepadButton(0, GamepadButton::A, false);
    NextFrame();
    Input::SimulateGamepadButton(1, GamepadButton::A, true);
    NextFrame();
    CHECK(Input::IsActionPressed("Jump") && Input::IsActionJustPressed("Jump"));
    CHECK(Input::IsGamepadButtonJustPressed(GamepadButton::A, 1) && !Input::IsGamepadButtonPressed(GamepadButton::A, 0));
    // Pad 0 pressing while pad 1 holds: still the same press.
    Input::SimulateGamepadButton(0, GamepadButton::A, true);
    NextFrame();
    CHECK(Input::IsActionPressed("Jump") && !Input::IsActionJustPressed("Jump"));
    Input::SimulateGamepadButton(0, GamepadButton::A, false);
    Input::SimulateGamepadButton(1, GamepadButton::A, false);
    NextFrame();
    CHECK(Input::IsActionJustReleased("Jump"));

    // A quick tap between two frames.
    Input::OnKeyEvent(Key::Space, true);
    Input::OnKeyEvent(Key::Space, false);
    NextFrame();
    CHECK(!Input::IsActionPressed("Jump") && Input::IsActionJustPressed("Jump") && Input::IsActionJustReleased("Jump"));
    NextFrame();
    CHECK(!Input::IsActionJustPressed("Jump") && !Input::IsActionJustReleased("Jump"));

    // Gamepad axis as a button: Fire listens to the right trigger (-1 released .. 1 pressed).
    CHECK(!Input::IsActionPressed("Fire"));
    Input::SimulateGamepadAxis(0, GamepadAxis::RightTrigger, 0.0f); // half way = 0.5: not yet
    NextFrame();
    CHECK(!Input::IsActionPressed("Fire"));
    Input::SimulateGamepadAxis(0, GamepadAxis::RightTrigger, 0.8f);
    NextFrame();
    CHECK(Input::IsActionPressed("Fire") && Input::IsActionJustPressed("Fire"));
    Input::OnMouseButtonEvent(MouseButton::Left, true);
    NextFrame();
    CHECK(Input::IsActionPressed("Fire") && !Input::IsActionJustPressed("Fire"));
    Input::SimulateGamepadAxis(0, GamepadAxis::RightTrigger, -1.0f);
    Input::OnMouseButtonEvent(MouseButton::Left, false);
    NextFrame();
    CHECK(!Input::IsActionPressed("Fire") && Input::IsActionJustReleased("Fire"));

    // A gamepad that disconnects while held releases the action.
    Input::SimulateGamepadButton(0, GamepadButton::A, true);
    NextFrame();
    CHECK(Input::IsActionPressed("Jump"));
    Input::SimulateGamepadConnected(0, false);
    NextFrame();
    CHECK(!Input::IsActionPressed("Jump") && Input::IsActionJustReleased("Jump") && !Input::IsGamepadConnected(0));

    // Unknown names are simply inactive.
    CHECK(!Input::IsActionPressed("NoSuchAction") && !Input::IsActionJustPressed("NoSuchAction") && !Input::IsActionJustReleased("NoSuchAction"));
    CHECK(Input::GetAxis("NoSuchAxis") == 0.0f);

    // The active project's map is used.
    auto project = Project::New();
    project->GetConfig().InputActions.FindAction("Jump")->Bindings = { InputBinding::FromKey(Key::K) };
    Input::ResetState();
    Input::OnKeyEvent(Key::Space, true);
    NextFrame();
    CHECK(!Input::IsActionPressed("Jump"));
    Input::OnKeyEvent(Key::K, true);
    NextFrame();
    CHECK(Input::IsActionPressed("Jump") && Input::IsActionJustPressed("Jump"));

    // Definitions can be queried directly, too.
    InputAction dash{ "Dash", { InputBinding::FromKey(Key::LeftShift), InputBinding::FromMouse(MouseButton::Right) } };
    InputAction unbound{ "Unbound", {} };
    CHECK(!Input::IsActionPressed(dash));
    Input::OnMouseButtonEvent(MouseButton::Right, true);
    NextFrame();
    CHECK(Input::IsActionPressed(dash) && Input::IsActionJustPressed(dash));
    CHECK(!Input::IsActionPressed(unbound) && !Input::IsActionJustPressed(unbound) && !Input::IsActionJustReleased(unbound));

    Project::SetActive(nullptr);
    Input::ResetState();
}

TEST(InputAxes)
{
    Project::SetActive(nullptr); // Horizontal: A/Left/DPadLeft - D/Right/DPadRight + LeftX; Vertical: S - W + LeftY (inverted)
    Input::ResetState();
    NextFrame();
    CHECK(Input::GetAxis("Horizontal") == 0.0f && Input::GetAxis("Vertical") == 0.0f);

    Input::OnKeyEvent(Key::D, true);
    NextFrame();
    CHECK(Input::GetAxis("Horizontal") == 1.0f);
    Input::OnKeyEvent(Key::A, true);
    NextFrame();
    CHECK(Input::GetAxis("Horizontal") == 0.0f); // both directions cancel
    Input::OnKeyEvent(Key::D, false);
    NextFrame();
    CHECK(Input::GetAxis("Horizontal") == -1.0f);
    Input::OnKeyEvent(Key::A, false);
    Input::OnKeyEvent(Key::Right, true);
    NextFrame();
    CHECK(Input::GetAxis("Horizontal") == 1.0f);

    // Diagonal keyboard input is normalized by GetAxis2D.
    Input::OnKeyEvent(Key::W, true);
    NextFrame();
    CHECK(Input::GetAxis("Vertical") == 1.0f);
    glm::vec2 move = Input::GetAxis2D();
    CHECK_NEAR(glm::length(move), 1.0f, 1e-5f);
    CHECK_NEAR(move.x, 0.70710678f, 1e-5f);
    CHECK_NEAR(move.y, 0.70710678f, 1e-5f);
    Input::OnKeyEvent(Key::Right, false);
    Input::OnKeyEvent(Key::W, false);
    NextFrame();
    CHECK(Input::GetAxis2D() == glm::vec2(0.0f));

    // Analog stick: deadzone (0.15) and rescaling.
    Input::SimulateGamepadAxis(0, GamepadAxis::LeftX, 0.1f);
    NextFrame();
    CHECK(Input::GetAxis("Horizontal") == 0.0f);
    CHECK(Input::GetGamepadAxis(GamepadAxis::LeftX) == 0.0f); // raw query uses its own deadzone
    Input::SimulateGamepadAxis(0, GamepadAxis::LeftX, 0.575f);
    NextFrame();
    CHECK_NEAR(Input::GetAxis("Horizontal"), 0.5f, 1e-5f);
    CHECK_NEAR(Input::GetGamepadAxis(GamepadAxis::LeftX), 0.575f, 1e-6f);
    Input::SimulateGamepadAxis(0, GamepadAxis::LeftX, -1.0f);
    NextFrame();
    CHECK_NEAR(Input::GetAxis("Horizontal"), -1.0f, 1e-6f);
    CHECK_NEAR(Input::GetAxis2D().x, -1.0f, 1e-6f);

    // Inverted vertical stick: up (-1) reads +1.
    Input::SimulateGamepadAxis(0, GamepadAxis::LeftY, -1.0f);
    NextFrame();
    CHECK_NEAR(Input::GetAxis("Vertical"), 1.0f, 1e-6f);
    CHECK_NEAR(glm::length(Input::GetAxis2D()), 1.0f, 1e-5f);

    // The input with the larger magnitude wins.
    Input::SimulateGamepadAxis(0, GamepadAxis::LeftX, 0.575f); // 0.5
    Input::OnKeyEvent(Key::A, true);                            // -1
    NextFrame();
    CHECK(Input::GetAxis("Horizontal") == -1.0f);
    Input::OnKeyEvent(Key::A, false);
    NextFrame();
    CHECK_NEAR(Input::GetAxis("Horizontal"), 0.5f, 1e-5f);

    // D-pad buttons are digital bindings; a second gamepad with a bigger value wins.
    Input::SimulateGamepadAxis(0, GamepadAxis::LeftX, 0.0f);
    Input::SimulateGamepadButton(0, GamepadButton::DPadLeft, true);
    NextFrame();
    CHECK(Input::GetAxis("Horizontal") == -1.0f);
    Input::SimulateGamepadButton(0, GamepadButton::DPadLeft, false);
    Input::SimulateGamepadAxis(0, GamepadAxis::LeftX, 0.4f);
    Input::SimulateGamepadAxis(1, GamepadAxis::LeftX, -0.8f);
    NextFrame();
    CHECK_NEAR(Input::GetAxis("Horizontal"), Input::ApplyDeadzone(-0.8f, 0.15f), 1e-6f);

    // Custom axes: a trigger (0..1) with its own deadzone, and stick halves used as buttons.
    InputAxis throttle;
    throttle.Name = "Throttle";
    throttle.GamepadAxisCode = (int)GamepadAxis::RightTrigger;
    throttle.Deadzone = 0.1f;
    Input::SimulateGamepadAxis(0, GamepadAxis::RightTrigger, -1.0f);
    NextFrame();
    CHECK(Input::GetAxis(throttle) == 0.0f);
    Input::SimulateGamepadAxis(0, GamepadAxis::RightTrigger, 0.0f);
    NextFrame();
    CHECK_NEAR(Input::GetAxis(throttle), 0.4f / 0.9f, 1e-5f);
    Input::SimulateGamepadAxis(0, GamepadAxis::RightTrigger, 1.0f);
    NextFrame();
    CHECK_NEAR(Input::GetAxis(throttle), 1.0f, 1e-6f);
    throttle.Invert = true;
    CHECK_NEAR(Input::GetAxis(throttle), -1.0f, 1e-6f);

    InputAxis steer;
    steer.Name = "Steer";
    steer.Negative = { InputBinding::FromGamepadAxis(GamepadAxis::RightX, -1) };
    steer.Positive = { InputBinding::FromGamepadAxis(GamepadAxis::RightX, 1) };
    Input::SimulateGamepadAxis(0, GamepadAxis::RightX, -0.7f);
    NextFrame();
    CHECK(Input::GetAxis(steer) == -1.0f);
    Input::SimulateGamepadAxis(0, GamepadAxis::RightX, 0.3f);
    NextFrame();
    CHECK(Input::GetAxis(steer) == 0.0f);
    InputAxis empty;
    CHECK(Input::GetAxis(empty) == 0.0f);

    // Deadzone helper.
    CHECK(Input::ApplyDeadzone(0.1f, 0.2f) == 0.0f);
    CHECK(Input::ApplyDeadzone(-0.2f, 0.2f) == 0.0f);
    CHECK_NEAR(Input::ApplyDeadzone(-0.6f, 0.2f), -0.5f, 1e-6f);
    CHECK_NEAR(Input::ApplyDeadzone(1.0f, 0.2f), 1.0f, 1e-6f);
    CHECK_NEAR(Input::ApplyDeadzone(0.5f, 0.0f), 0.5f, 1e-6f);
    CHECK(Input::ApplyDeadzone(1.0f, 1.0f) <= 1.0f);

    Input::ResetState();
}

TEST(SaveDataValuesAndFile)
{
    auto dir = std::filesystem::temp_directory_path() / ("zamin-save-" + std::to_string((uint64_t)UUID()));
    auto file = dir / "save.json";
    SaveData::SetFilePath(file);
    CHECK(SaveData::GetFilePath() == file);
    CHECK(SaveData::GetKeys().empty() && !SaveData::HasKey("Score") && !SaveData::IsDirty());
    CHECK(SaveData::GetType("Score") == SaveValueType::None);

    // Defaults for missing keys.
    CHECK(SaveData::GetInt("Score", 7) == 7);
    CHECK(SaveData::GetFloat("Volume", 0.5f) == 0.5f);
    CHECK(SaveData::GetString("Name", "x") == "x");
    CHECK(SaveData::GetBool("Muted", true));

    const std::string unicode = "Abbos O'g'li \xE2\x80\x94 \xE2\x9C\x93 \"qo'shtirnoq\"\n";
    SaveData::SetInt("Score", 1234);
    SaveData::SetInt("Negative", -42);
    SaveData::SetInt("Max", std::numeric_limits<int>::max());
    SaveData::SetFloat("Volume", 0.75f);
    SaveData::SetFloat("Pi", 3.14159274f);
    SaveData::SetFloat("Tiny", 1.0e-7f);
    SaveData::SetString("Name", unicode);
    SaveData::SetString("Empty", "");
    SaveData::SetBool("Muted", true);
    SaveData::SetBool("Off", false);
    CHECK(SaveData::IsDirty());
    CHECK(SaveData::GetInt("Score") == 1234 && SaveData::GetInt("Negative") == -42);
    CHECK(SaveData::GetFloat("Volume") == 0.75f && SaveData::GetString("Name") == unicode);
    CHECK(SaveData::GetBool("Muted") && !SaveData::GetBool("Off", true));
    CHECK(SaveData::HasKey("Empty") && SaveData::GetString("Empty", "d").empty());
    CHECK(SaveData::GetType("Score") == SaveValueType::Int && SaveData::GetType("Pi") == SaveValueType::Float);
    CHECK(SaveData::GetType("Name") == SaveValueType::String && SaveData::GetType("Off") == SaveValueType::Bool);
    CHECK(std::string(SaveData::TypeToString(SaveValueType::Float)) == "Float");

    // Numbers convert, other mismatches give the default.
    CHECK(SaveData::GetFloat("Score") == 1234.0f);
    CHECK(SaveData::GetInt("Pi") == 3 && SaveData::GetInt("Volume", 9) == 0);
    CHECK(SaveData::GetInt("Name", 5) == 5 && SaveData::GetString("Score", "d") == "d");
    CHECK(!SaveData::GetBool("Score", false) && SaveData::GetInt("Muted", -1) == -1);

    // Setting a key again replaces the value and its type.
    SaveData::SetString("Negative", "minus");
    CHECK(SaveData::GetType("Negative") == SaveValueType::String && SaveData::GetInt("Negative", 9) == 9);

    // Save -> file on disk; unsaved changes are dropped by Reload().
    CHECK(SaveData::Save());
    CHECK(!SaveData::IsDirty());
    CHECK(std::filesystem::exists(file));
    CHECK(!std::filesystem::exists(dir / "save.json.tmp"));
    SaveData::SetInt("Score", 1);
    SaveData::DeleteKey("Muted");
    CHECK(SaveData::IsDirty() && !SaveData::HasKey("Muted"));
    CHECK(SaveData::Reload());
    CHECK(SaveData::GetInt("Score") == 1234 && SaveData::GetBool("Muted") && !SaveData::IsDirty());

    // Another file is separate; coming back reads everything exactly (like a new session).
    SaveData::SetFilePath(dir / "other.json");
    CHECK(!SaveData::HasKey("Score") && SaveData::GetKeys().empty());
    SaveData::SetFilePath(file);
    CHECK(SaveData::GetInt("Score") == 1234 && SaveData::GetInt("Max") == std::numeric_limits<int>::max());
    CHECK(SaveData::GetFloat("Pi") == 3.14159274f && SaveData::GetFloat("Tiny") == 1.0e-7f);
    CHECK(SaveData::GetString("Name") == unicode && SaveData::GetString("Negative") == "minus");
    CHECK(!SaveData::GetBool("Off", true) && SaveData::GetType("Volume") == SaveValueType::Float);
    auto keys = SaveData::GetKeys();
    CHECK(keys.size() == 10 && std::is_sorted(keys.begin(), keys.end()));

    // Writing the same value does not mark the data dirty.
    SaveData::SetInt("Score", 1234);
    SaveData::SetString("Name", unicode);
    CHECK(!SaveData::IsDirty());

    // Switching files saves unsaved data first.
    SaveData::SetInt("Coins", 5);
    SaveData::SetFilePath(dir / "other.json");
    SaveData::SetFilePath(file);
    CHECK(SaveData::GetInt("Coins") == 5);

    // DeleteKey / DeleteAll.
    SaveData::DeleteKey("Coins");
    SaveData::DeleteKey("NoSuchKey");
    CHECK(!SaveData::HasKey("Coins"));
    SaveData::DeleteAll();
    CHECK(SaveData::GetKeys().empty() && SaveData::IsDirty());
    CHECK(SaveData::Save() && SaveData::Reload());
    CHECK(SaveData::GetKeys().empty());

    // A broken file is kept as .bak and the game starts with empty data.
    CHECK(FileSystem::WriteTextFile(file, "{ broken"));
    Log::SetMinLevel(LogLevel::Critical); // the errors below are expected
    CHECK(!SaveData::Reload());
    CHECK(SaveData::GetKeys().empty() && std::filesystem::exists(dir / "save.json.bak"));
    SaveData::SetBool("Recovered", true);
    CHECK(SaveData::Save() && SaveData::Reload() && SaveData::GetBool("Recovered"));

    // Hand-edited files: out-of-range ints are clamped, unsupported values skipped.
    CHECK(FileSystem::WriteTextFile(file, R"({"Version":1,"Data":{"Big":99999999999,"Small":-99999999999,"Arr":[1,2],"Null":null,"F":2.5,"S":"ok","B":false}})"));
    CHECK(SaveData::Reload());
    Log::SetMinLevel(LogLevel::Error);
    CHECK(SaveData::GetInt("Big") == std::numeric_limits<int>::max() && SaveData::GetInt("Small") == std::numeric_limits<int>::min());
    CHECK(!SaveData::HasKey("Arr") && !SaveData::HasKey("Null"));
    CHECK(SaveData::GetFloat("F") == 2.5f && SaveData::GetString("S") == "ok" && !SaveData::GetBool("B", true));

    SaveData::SetFilePath({});
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(SaveDataProjectLocation)
{
    const char* oldXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = oldXdg ? oldXdg : "";
    const bool hadXdg = oldXdg != nullptr;
    auto dir = std::filesystem::temp_directory_path() / ("zamin-xdg-" + std::to_string((uint64_t)UUID()));
    setenv("XDG_DATA_HOME", dir.c_str(), 1);

    // <XDG_DATA_HOME>/zamin/<project>, with a file-system safe name.
    CHECK(SaveData::GetDirectoryForProject("My Game") == dir / "zamin" / "My Game");
    CHECK(SaveData::GetDirectoryForProject("a/b:c*") == dir / "zamin" / "a_b_c_");
    CHECK(SaveData::GetDirectoryForProject("..") == dir / "zamin" / "Untitled");
    CHECK(SaveData::GetDirectoryForProject("") == dir / "zamin" / "Untitled");
    CHECK(SaveData::GetDirectoryForProject(" .Hidden. ") == dir / "zamin" / "Hidden");
    // A relative XDG_DATA_HOME is ignored (XDG spec): ~/.local/share is used.
    setenv("XDG_DATA_HOME", "relative/dir", 1);
    if (const char* home = std::getenv("HOME"); home && *home)
        CHECK(SaveData::GetDirectoryForProject("G") == std::filesystem::path(home) / ".local" / "share" / "zamin" / "G");
    setenv("XDG_DATA_HOME", dir.c_str(), 1);

    // The data follows the active project; the previous project's data is written first.
    auto a = Project::New();
    a->GetConfig().Name = "SaveGameA";
    CHECK(SaveData::GetFilePath() == dir / "zamin" / "SaveGameA" / "savedata.json");
    SaveData::SetInt("Level", 3);
    auto b = Project::New();
    b->GetConfig().Name = "SaveGameB";
    CHECK(!SaveData::HasKey("Level"));
    CHECK(std::filesystem::exists(dir / "zamin" / "SaveGameA" / "savedata.json"));
    SaveData::SetInt("Level", 9);
    Project::SetActive(a);
    CHECK(SaveData::GetInt("Level") == 3);
    Project::SetActive(b);
    CHECK(SaveData::GetInt("Level") == 9);

    // Stopping play mode flushes the data.
    SaveData::SetInt("Level", 10);
    CHECK(SaveData::IsDirty());
    {
        Scene scene;
        scene.CreateEntity("E");
        scene.OnRuntimeStart();
        scene.OnUpdateRuntime(0.016f);
        scene.OnRuntimeStop();
    }
    CHECK(!SaveData::IsDirty());
    CHECK(FileSystem::ReadTextFile(dir / "zamin" / "SaveGameB" / "savedata.json").find("10") != std::string::npos);

    Project::SetActive(nullptr);
    SaveData::SetFilePath({});
    if (hadXdg) setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else unsetenv("XDG_DATA_HOME");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ------------------------------------------------------------------ M01: Random
TEST(RandomDeterminism)
{
    // PCG32 reference output (pcg32-demo: seed 42, stream 54).
    RandomGenerator ref(42u, 54u);
    const uint32_t expected[] = { 0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu };
    for (uint32_t e : expected)
        CHECK(ref.NextUInt() == e);

    // Same seed -> same sequence, different seed -> different sequence.
    RandomGenerator a(1234), b(1234), c(1235);
    bool allSame = true, anyDifferent = false;
    for (int i = 0; i < 100; i++)
    {
        float x = a.Float();
        allSame &= x == b.Float();
        anyDifferent |= x != c.Float();
    }
    CHECK(allSame);
    CHECK(anyDifferent);
    CHECK(a.GetSeed() == 1234);

    // SetSeed restarts the sequence (including the cached Gaussian pair).
    RandomGenerator g(7);
    float first[4] = { g.Float(), g.Gaussian(), g.Range(0.0f, 10.0f), (float)g.Int(1, 100) };
    g.Gaussian(); // leaves a spare value behind
    g.SetSeed(7);
    float again[4] = { g.Float(), g.Gaussian(), g.Range(0.0f, 10.0f), (float)g.Int(1, 100) };
    for (int i = 0; i < 4; i++)
        CHECK(first[i] == again[i]);

    // The global generator follows Random::SetSeed.
    Random::SetSeed(99);
    RandomGenerator local(99);
    CHECK(Random::GetSeed() == 99);
    CHECK(Random::Float() == local.Float());
    CHECK(Random::Range(-5, 5) == local.Range(-5, 5));
    CHECK(Random::InsideCircle(2.0f) == local.InsideCircle(2.0f));

    // Randomize picks a new seed (two in a row are practically never equal).
    RandomGenerator r1, r2;
    CHECK(r1.GetSeed() != r2.GetSeed());

    // Shuffle: a permutation, reproducible with the seed.
    std::vector<int> v1(50), v2(50);
    for (int i = 0; i < 50; i++) v1[i] = v2[i] = i;
    RandomGenerator s1(5), s2(5);
    s1.Shuffle(v1);
    s2.Shuffle(v2);
    CHECK(v1 == v2);
    std::vector<int> sorted = v1;
    std::sort(sorted.begin(), sorted.end());
    bool permutation = true;
    for (int i = 0; i < 50; i++) permutation &= sorted[i] == i;
    CHECK(permutation);
    bool moved = false;
    for (int i = 0; i < 50; i++) moved |= v1[i] != i;
    CHECK(moved);
}

TEST(RandomRanges)
{
    RandomGenerator rng(2024);
    const int N = 20000;

    // Float in [0, 1), mean ~0.5
    double sum = 0.0;
    bool inRange = true;
    for (int i = 0; i < N; i++)
    {
        float f = rng.Float();
        inRange &= f >= 0.0f && f < 1.0f;
        sum += f;
    }
    CHECK(inRange);
    CHECK_NEAR(sum / N, 0.5, 0.01);

    // Float ranges (also with swapped / equal ends)
    bool rangeOk = true;
    double rangeSum = 0.0;
    for (int i = 0; i < N; i++)
    {
        float f = rng.Range(-3.0f, 7.0f);
        rangeOk &= f >= -3.0f && f <= 7.0f;
        rangeSum += f;
        float s = rng.Range(5.0f, 2.0f);
        rangeOk &= s >= 2.0f && s <= 5.0f;
    }
    CHECK(rangeOk);
    CHECK_NEAR(rangeSum / N, 2.0, 0.1);
    CHECK(rng.Range(4.0f, 4.0f) == 4.0f);

    // Int: both ends inclusive, every value reachable, roughly uniform
    int counts[6] = {};
    bool intOk = true;
    for (int i = 0; i < N; i++)
    {
        int d = rng.Int(1, 6);
        intOk &= d >= 1 && d <= 6;
        if (d >= 1 && d <= 6) counts[d - 1]++;
    }
    CHECK(intOk);
    for (int c : counts)
        CHECK(std::abs(c - N / 6) < N / 60); // within 10 %
    CHECK(rng.Range(5, 5) == 5);
    bool swappedOk = true;
    for (int i = 0; i < 100; i++) { int v = rng.Range(10, -10); swappedOk &= v >= -10 && v <= 10; }
    CHECK(swappedOk);
    // Extreme ranges do not overflow
    bool extremeOk = true;
    for (int i = 0; i < 100; i++)
    {
        int v = rng.Int(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        (void)v;
        int w = rng.Int(std::numeric_limits<int>::max() - 1, std::numeric_limits<int>::max());
        extremeOk &= w >= std::numeric_limits<int>::max() - 1;
        int m = rng.Int(std::numeric_limits<int>::min(), std::numeric_limits<int>::min() + 2);
        extremeOk &= m <= std::numeric_limits<int>::min() + 2;
    }
    CHECK(extremeOk);

    // Index
    CHECK(rng.Index(0) == 0);
    CHECK(rng.Index(1) == 0);
    bool indexOk = true;
    for (int i = 0; i < 1000; i++) indexOk &= rng.Index(7) < 7;
    CHECK(indexOk);
    std::vector<std::string> names = { "a", "b", "c" };
    std::set<std::string> picked;
    for (int i = 0; i < 200; i++) picked.insert(rng.Pick(names));
    CHECK(picked.size() == 3);

    // Bool probabilities
    int trues = 0;
    bool never = false, always = true;
    for (int i = 0; i < N; i++)
    {
        trues += rng.Bool(0.25f) ? 1 : 0;
        never |= rng.Bool(0.0f);
        always &= rng.Bool(1.0f);
    }
    CHECK(!never);
    CHECK(always);
    CHECK_NEAR((double)trues / N, 0.25, 0.015);
    float sign = rng.Sign();
    CHECK(sign == 1.0f || sign == -1.0f);

    // Angle / Gaussian
    bool angleOk = true;
    for (int i = 0; i < 1000; i++) { float a = rng.Angle(); angleOk &= a >= 0.0f && a < 6.2832f; }
    CHECK(angleOk);
    double gSum = 0.0, gSq = 0.0;
    for (int i = 0; i < N; i++) { double g = rng.Gaussian(10.0f, 2.0f); gSum += g; gSq += g * g; }
    double mean = gSum / N, var = gSq / N - mean * mean;
    CHECK_NEAR(mean, 10.0, 0.06);
    CHECK_NEAR(std::sqrt(var), 2.0, 0.06);

    // Weighted choice
    const float weights[] = { 1.0f, 0.0f, 3.0f, -2.0f };
    int wc[4] = {};
    for (int i = 0; i < N; i++)
    {
        int k = rng.WeightedIndex(weights);
        if (k >= 0) wc[k]++;
    }
    CHECK(wc[1] == 0 && wc[3] == 0);
    CHECK_NEAR((double)wc[2] / N, 0.75, 0.015);
    const float zero[] = { 0.0f, -1.0f };
    CHECK(rng.WeightedIndex(zero) == -1);
    CHECK(rng.WeightedIndex({}) == -1);

    // Colors stay valid
    bool colorOk = true;
    for (int i = 0; i < 500; i++)
    {
        glm::vec4 c = rng.ColorHSV();
        colorOk &= c.r >= 0.0f && c.r <= 1.0f && c.g >= 0.0f && c.g <= 1.0f && c.b >= 0.0f && c.b <= 1.0f && c.a == 1.0f;
    }
    CHECK(colorOk);
    glm::vec4 red = rng.ColorHSV(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    CHECK_NEAR(red.r, 1.0f, 1e-5f);
    CHECK_NEAR(red.g, 0.0f, 1e-5f);
    CHECK_NEAR(red.b, 0.0f, 1e-5f);
}

TEST(RandomGeometry)
{
    RandomGenerator rng(77);
    const int N = 20000;

    // Inside a circle: within the radius, uniform over the area (1/4 inside r/2).
    int innerCircle = 0;
    bool circleOk = true;
    glm::vec2 circleMean(0.0f);
    for (int i = 0; i < N; i++)
    {
        glm::vec2 p = rng.InsideCircle(3.0f);
        float len = glm::length(p);
        circleOk &= len <= 3.0f + 1e-4f;
        innerCircle += len < 1.5f ? 1 : 0;
        circleMean += p;
    }
    CHECK(circleOk);
    CHECK_NEAR((double)innerCircle / N, 0.25, 0.015);
    CHECK(glm::length(circleMean / (float)N) < 0.06f);

    // On a circle / sphere: exactly on the radius, centred on the origin.
    bool onOk = true;
    glm::vec3 sphereMean(0.0f);
    int upperHalf = 0;
    for (int i = 0; i < N; i++)
    {
        onOk &= std::abs(glm::length(rng.OnUnitCircle()) - 1.0f) < 1e-4f;
        onOk &= std::abs(glm::length(rng.OnCircle(2.5f)) - 2.5f) < 1e-3f;
        glm::vec3 s = rng.OnSphere(4.0f);
        onOk &= std::abs(glm::length(s) - 4.0f) < 1e-3f;
        sphereMean += s;
        upperHalf += s.y > 0.0f ? 1 : 0;
        onOk &= std::abs(glm::length(rng.OnUnitSphere()) - 1.0f) < 1e-4f;
    }
    CHECK(onOk);
    CHECK(glm::length(sphereMean / (float)N) < 0.1f);
    CHECK_NEAR((double)upperHalf / N, 0.5, 0.015);

    // Inside a sphere: within the radius, 1/8 of the points inside r/2.
    int innerSphere = 0;
    bool sphereOk = true;
    for (int i = 0; i < N; i++)
    {
        glm::vec3 p = rng.InsideSphere(2.0f);
        float len = glm::length(p);
        sphereOk &= len <= 2.0f + 1e-4f;
        innerSphere += len < 1.0f ? 1 : 0;
    }
    CHECK(sphereOk);
    CHECK_NEAR((double)innerSphere / N, 0.125, 0.012);

    // Box and rotation
    bool boxOk = true, rotOk = true;
    for (int i = 0; i < 1000; i++)
    {
        glm::vec3 b = rng.InsideBox({ -1.0f, 2.0f, 5.0f }, { 1.0f, 3.0f, 5.0f });
        boxOk &= b.x >= -1.0f && b.x <= 1.0f && b.y >= 2.0f && b.y <= 3.0f && b.z == 5.0f;
        glm::quat q = rng.Rotation();
        rotOk &= std::abs(glm::length(q) - 1.0f) < 1e-4f;
    }
    CHECK(boxOk);
    CHECK(rotOk);
}

// ------------------------------------------------------------------ M01: tags
class TagProbe : public ScriptableEntity
{
public:
};
ZM_REGISTER_SCRIPT(TagProbe)

TEST(TagsQueries)
{
    auto scene = CreateRef<Scene>();
    Entity e1 = scene->CreateEntity("Goblin");
    Entity e2 = scene->CreateEntity("Orc");
    Entity e3 = scene->CreateEntity("Tree");
    Entity e4 = scene->CreateEntity("Dragon");

    CHECK(!e1.HasTag("Enemy"));
    CHECK(e4.AddTag("Boss"));       // creates the component
    CHECK(e4.HasComponent<TagsComponent>());
    CHECK(e4.AddTag("Enemy"));
    CHECK(e2.AddTag("Enemy"));
    CHECK(e1.AddTag("Enemy"));
    CHECK(!e1.AddTag("Enemy"));     // duplicate
    CHECK(!e3.AddTag(""));          // empty
    CHECK(!e3.HasComponent<TagsComponent>());
    CHECK(e1.HasTag("Enemy") && !e1.HasTag("enemy")); // case sensitive
    CHECK(!e3.HasTag("Enemy"));

    // Results come in scene order, whatever order the tags were added in.
    std::vector<Entity> enemies = scene->FindEntitiesWithTag("Enemy");
    CHECK(enemies.size() == 3);
    CHECK(enemies.size() == 3 && enemies[0] == e1 && enemies[1] == e2 && enemies[2] == e4);
    CHECK(scene->FindEntityWithTag("Enemy") == e1);
    CHECK(scene->FindEntityWithTag("Boss") == e4);
    CHECK(!scene->FindEntityWithTag("Nobody"));
    CHECK(scene->FindEntitiesWithTag("Nobody").empty());
    CHECK(scene->CountEntitiesWithTag("Enemy") == 3);
    CHECK(scene->GetAllTags() == std::vector<std::string>({ "Boss", "Enemy" }));

    CHECK(e1.RemoveTag("Enemy"));
    CHECK(!e1.RemoveTag("Enemy"));
    CHECK(!e3.RemoveTag("Enemy"));
    CHECK(scene->FindEntityWithTag("Enemy") == e2);

    // Copies keep their tags.
    Entity copy = scene->DuplicateEntity(e4);
    CHECK(copy.HasTag("Boss") && copy.HasTag("Enemy"));
    CHECK(scene->CountEntitiesWithTag("Boss") == 2);

    // Serialization round trip (order of the tags is kept).
    e2.AddTag("Green");
    SceneSerializer serializer(scene);
    std::string text = serializer.SerializeToString();
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    Entity lo = loaded->FindEntityByName("Orc");
    CHECK(lo && lo.HasComponent<TagsComponent>());
    CHECK(lo && lo.GetComponent<TagsComponent>().Tags == std::vector<std::string>({ "Enemy", "Green" }));
    Entity lg = loaded->FindEntityByName("Goblin");
    CHECK(lg && lg.HasComponent<TagsComponent>() && lg.GetComponent<TagsComponent>().Tags.empty()); // kept, empty
    CHECK(loaded->FindEntityByName("Tree") && !loaded->FindEntityByName("Tree").HasComponent<TagsComponent>());
    CHECK(loaded->CountEntitiesWithTag("Boss") == 2);

    // Hand-edited files: empty and duplicate entries are dropped.
    auto edited = CreateRef<Scene>();
    CHECK(SceneSerializer(edited).DeserializeFromString(
        R"({"Scene":"T","Entities":[{"Entity":12345,"Tag":"X","Tags":["A","","A","B",7]}]})"));
    Entity ex = edited->FindEntityByName("X");
    CHECK(ex && ex.GetComponent<TagsComponent>().Tags == std::vector<std::string>({ "A", "B" }));

    // Runtime copy + script helpers.
    e3.AddComponent<NativeScriptComponent>().ClassName = "TagProbe";
    auto runtime = Scene::Copy(scene);
    runtime->OnRuntimeStart();
    auto* probe = ScriptableEntity::GetScript<TagProbe>(runtime->FindEntityByName("Tree"));
    CHECK(probe != nullptr);
    if (probe)
    {
        CHECK(!probe->HasTag("Tree"));
        CHECK(probe->AddTag("Tree"));
        CHECK(probe->HasTag("Tree"));
        CHECK(probe->FindEntityWithTag("Tree") == probe->GetEntity());
        CHECK(probe->FindEntitiesWithTag("Enemy").size() == 3); // Orc, Dragon, its copy
        CHECK(probe->FindEntityWithTag("Enemy") == runtime->FindEntityByName("Orc"));
        CHECK(probe->RemoveTag("Tree"));
    }
    CHECK(!scene->FindEntityWithTag("Tree")); // the editor scene is untouched
    runtime->OnRuntimeStop();
}

TEST(ProjectTagList)
{
    auto dir = std::filesystem::temp_directory_path() / ("zamin-tags-" + std::to_string((uint64_t)UUID()));
    auto project = Project::CreateFromTemplate(dir, "TagGame");
    CHECK(project != nullptr);
    if (project)
    {
        CHECK(project->GetConfig().Tags == ProjectConfig::DefaultTags());
        CHECK(Project::RegisterTag("Coin"));
        CHECK(!Project::RegisterTag("Coin"));
        CHECK(!Project::RegisterTag(""));
        CHECK(Project::SaveActive(dir / "TagGame.zproject"));
        auto loaded = Project::Load(dir / "TagGame.zproject");
        CHECK(loaded && loaded->GetConfig().Tags.size() == ProjectConfig::DefaultTags().size() + 1);
        CHECK(loaded && loaded->GetConfig().Tags.back() == "Coin");

        CHECK(FileSystem::WriteTextFile(dir / "Edited.zproject", R"({"Project":{"Name":"E"},"Tags":["A","","A","B"]})"));
        auto edited = Project::Load(dir / "Edited.zproject");
        CHECK(edited && edited->GetConfig().Tags == std::vector<std::string>({ "A", "B" }));
        CHECK(FileSystem::WriteTextFile(dir / "Old.zproject", R"({"Project":{"Name":"Old"}})"));
        auto old = Project::Load(dir / "Old.zproject");
        CHECK(old && old->GetConfig().Tags == ProjectConfig::DefaultTags());
    }
    Project::SetActive(nullptr);
    CHECK(!Project::RegisterTag("NoProject"));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ------------------------------------------------------------------ M01: messages / events
static std::vector<std::string> g_MessageLog;

class MessageReceiver : public ScriptableEntity
{
public:
    float Health = 100.0f;
    int Received = 0;

    void OnCreate() override
    {
        AddMessageHandler("Damage", [this](const ScriptMessage& m) {
            Health -= m.Get<float>();
            g_MessageLog.push_back(GetName() + ":handler");
        });
    }

    void OnMessage(const ScriptMessage& m) override
    {
        Received++;
        g_MessageLog.push_back(GetName() + ":" + m.Name);
        if (m.Name == "Ping" && m.Sender && m.Sender != GetEntity())
            SendMessage(m.Sender, "Pong", GetName());
        if (m.Name == "Loop")
            SendMessage(GetEntity(), "Loop"); // endless: stopped by the depth guard
        if (m.Name == "Mute")
            RemoveMessageHandlers("Damage");
    }
};
ZM_REGISTER_SCRIPT(MessageReceiver)

TEST(ScriptMessages)
{
    // Payload conversions
    ScriptMessage m{ "X", {}, 5 };
    CHECK(m.HasPayload() && m.Is<int>() && !m.Is<float>());
    CHECK(m.Get<int>() == 5);
    CHECK(m.Get<float>() == 5.0f);
    CHECK(m.Get<double>() == 5.0);
    CHECK(m.Get<bool>() == true);
    CHECK(m.Get<std::string>("none") == "none");
    CHECK(m.TryGet<int>() && *m.TryGet<int>() == 5 && !m.TryGet<float>());
    ScriptMessage f{ "F", {}, 2.75f };
    CHECK(f.Get<int>() == 2);
    ScriptMessage s{ "S", {}, "salom" };
    CHECK(s.Get<std::string>() == "salom");
    CHECK(s.Get<int>(-1) == -1);
    ScriptMessage v{ "V", {}, glm::vec3(1, 2, 3) };
    CHECK(v.Get<glm::vec3>() == glm::vec3(1, 2, 3));
    CHECK(v.Get<float>(9.0f) == 9.0f);
    ScriptMessage empty{ "E" };
    CHECK(!empty.HasPayload() && empty.Get<int>(3) == 3);

    // Hierarchy: Root -> (A -> A1), B; C is a separate root; D has no script.
    auto scene = CreateRef<Scene>();
    Entity root = scene->CreateEntity("Root");
    Entity a = scene->CreateEntity("A");
    Entity a1 = scene->CreateEntity("A1");
    Entity b = scene->CreateEntity("B");
    Entity c = scene->CreateEntity("C");
    Entity d = scene->CreateEntity("D");
    scene->SetParent(a, root);
    scene->SetParent(a1, a);
    scene->SetParent(b, root);
    scene->SetParent(d, root);
    for (Entity e : { root, a, a1, b, c })
        e.AddComponent<NativeScriptComponent>().ClassName = "MessageReceiver";

    // Without running scripts nothing is delivered.
    CHECK(!scene->SendMessage(a, ScriptMessage{ "Hello" }));

    scene->OnRuntimeStart();
    auto* ra = ScriptableEntity::GetScript<MessageReceiver>(a);
    auto* rroot = ScriptableEntity::GetScript<MessageReceiver>(root);
    auto* rc = ScriptableEntity::GetScript<MessageReceiver>(c);
    CHECK(ra && rroot && rc);
    if (ra && rroot && rc)
    {
        // Direct message: named handler first, then OnMessage.
        g_MessageLog.clear();
        CHECK(rc->SendMessage(a, "Damage", 30));
        CHECK_NEAR(ra->Health, 70.0f, 1e-5f);
        CHECK(g_MessageLog == std::vector<std::string>({ "A:handler", "A:Damage" }));
        CHECK(!rc->SendMessage(d, "Damage", 10));        // no script
        CHECK(!rc->SendMessage(Entity{}, "Damage", 10));  // invalid entity

        // Replies: the sender is known.
        g_MessageLog.clear();
        CHECK(rc->SendMessage(a, "Ping"));
        CHECK(g_MessageLog == std::vector<std::string>({ "A:Ping", "C:Pong" }));

        // Children (depth-first) and upwards.
        g_MessageLog.clear();
        CHECK(rroot->SendMessageToChildren("Wave") == 4);
        CHECK(g_MessageLog == std::vector<std::string>({ "Root:Wave", "A:Wave", "A1:Wave", "B:Wave" }));
        g_MessageLog.clear();
        CHECK(rroot->SendMessageToChildren("Wave", {}, false) == 3);
        CHECK(g_MessageLog.size() == 3 && g_MessageLog.front() == "A:Wave");
        auto* ra1 = ScriptableEntity::GetScript<MessageReceiver>(a1);
        g_MessageLog.clear();
        CHECK(ra1 && ra1->SendMessageUpwards("Up") == 3);
        CHECK(g_MessageLog == std::vector<std::string>({ "A1:Up", "A:Up", "Root:Up" }));
        CHECK(ra1 && ra1->SendMessageUpwards("Up", {}, false) == 2);

        // Broadcast reaches every script in scene order.
        g_MessageLog.clear();
        CHECK(rc->Broadcast("All", 1) == 5);
        CHECK(g_MessageLog == std::vector<std::string>({ "Root:All", "A:All", "A1:All", "B:All", "C:All" }));

        // Removing handlers.
        CHECK(rc->SendMessage(a, "Mute"));
        CHECK(rc->SendMessage(a, "Damage", 30));
        CHECK_NEAR(ra->Health, 70.0f, 1e-5f);

        // Endless recursion is cut off.
        int before = ra->Received;
        CHECK(rc->SendMessage(a, "Loop"));
        CHECK(ra->Received - before == Scene::MaxMessageDepth);
        // ... and messaging still works afterwards.
        CHECK(rc->SendMessage(a, "Hello"));
    }
    scene->OnRuntimeStop();
    g_MessageLog.clear();
}

static int g_StaticEvents = 0;

class EventListener : public ScriptableEntity
{
public:
    int Coins = 0;
    SubscriptionID Sub = 0;
    void OnCreate() override
    {
        Sub = Subscribe("CoinCollected", [this](const ScriptMessage& m) {
            Coins += m.Get<int>(1);
            g_MessageLog.push_back(GetName() + ":" + std::to_string(m.Get<int>(1)));
        });
    }
};
ZM_REGISTER_SCRIPT(EventListener)

class EventEmitter : public ScriptableEntity
{
public:
    int Frame = 0;
    void OnUpdate(Timestep) override
    {
        Frame++;
        if (Frame == 1)
        {
            Emit("CoinCollected", 2);
            EmitDeferred("CoinCollected", 5); // after OnLateUpdate
            g_MessageLog.push_back("emitted");
        }
    }
    void OnLateUpdate(Timestep) override { if (Frame == 1) g_MessageLog.push_back("late"); }
};
ZM_REGISTER_SCRIPT(EventEmitter)

TEST(EventBusBasics)
{
    EventBus bus;
    std::vector<std::string> log;
    SubscriptionID s1 = bus.Subscribe("Hit", [&](const ScriptMessage& m) { log.push_back("s1:" + std::to_string(m.Get<int>())); });
    SubscriptionID s2 = bus.Subscribe("Hit", [&](const ScriptMessage&) { log.push_back("s2"); }, 77);
    CHECK(s1 != 0 && s2 != 0 && s1 != s2);
    CHECK(bus.Subscribe("", [](const ScriptMessage&) {}) == 0);
    CHECK(bus.Subscribe("Hit", MessageHandler{}) == 0);
    CHECK(bus.GetSubscriberCount("Hit") == 2 && bus.HasSubscribers("Hit") && !bus.HasSubscribers("Miss"));

    CHECK(bus.Emit("Hit", 4) == 2);
    CHECK(log == std::vector<std::string>({ "s1:4", "s2" }));
    CHECK(bus.Emit("Miss") == 0);

    // Unsubscribing
    CHECK(bus.Unsubscribe(s1));
    CHECK(!bus.Unsubscribe(s1));
    CHECK(!bus.Unsubscribe(0));
    log.clear();
    CHECK(bus.Emit("Hit", 1) == 1 && log == std::vector<std::string>({ "s2" }));
    CHECK(bus.UnsubscribeOwner(77) == 1);
    CHECK(bus.Emit("Hit") == 0 && bus.GetSubscriptionCount() == 0);

    // Deferred events wait for Flush; events queued during Flush wait for the next one.
    int count = 0;
    bus.Subscribe("Tick", [&](const ScriptMessage& m) {
        count++;
        if (m.Get<int>() < 3) bus.EmitDeferred("Tick", m.Get<int>() + 1);
    });
    bus.EmitDeferred("Tick", 1);
    CHECK(count == 0 && bus.GetQueuedCount() == 1);
    CHECK(bus.Flush() == 1 && count == 1 && bus.GetQueuedCount() == 1);
    CHECK(bus.Flush() == 1 && count == 2);
    CHECK(bus.Flush() == 1 && count == 3);
    CHECK(bus.Flush() == 0 && count == 3);

    // Changing subscriptions from inside a handler.
    bus.Clear();
    log.clear();
    SubscriptionID self = 0, victim = 0;
    self = bus.Subscribe("E", [&](const ScriptMessage&) {
        log.push_back("self");
        bus.Unsubscribe(self);    // removes itself
        bus.Unsubscribe(victim);  // and a later handler (not called anymore)
        bus.Subscribe("E", [&](const ScriptMessage&) { log.push_back("late"); }); // starts with the next event
        bus.Emit("Other");        // nested emit of another event
    });
    victim = bus.Subscribe("E", [&](const ScriptMessage&) { log.push_back("victim"); });
    bus.Subscribe("Other", [&](const ScriptMessage&) { log.push_back("other"); });
    CHECK(bus.Emit("E") == 1);
    CHECK(log == std::vector<std::string>({ "self", "other" }));
    log.clear();
    CHECK(bus.Emit("E") == 1 && log == std::vector<std::string>({ "late" }));
    CHECK(bus.GetSubscriberCount("E") == 1);

    // Clear inside a handler stops the remaining handlers.
    bus.Clear();
    log.clear();
    bus.Subscribe("C", [&](const ScriptMessage&) { log.push_back("first"); bus.Clear(); });
    bus.Subscribe("C", [&](const ScriptMessage&) { log.push_back("second"); });
    CHECK(bus.Emit("C") == 1 && log == std::vector<std::string>({ "first" }));
    CHECK(bus.GetSubscriptionCount() == 0);

    // Endless re-emitting is cut off at MaxEmitDepth.
    int depth = 0;
    bus.Subscribe("Rec", [&](const ScriptMessage&) { depth++; bus.Emit("Rec"); });
    bus.Emit("Rec");
    CHECK(depth == EventBus::MaxEmitDepth);
    bus.Clear();

    // Free functions / static handlers work too.
    bus.Subscribe("S", [](const ScriptMessage&) { g_StaticEvents++; });
    bus.Emit("S");
    CHECK(g_StaticEvents == 1);
}

TEST(EventBusInScene)
{
    auto scene = CreateRef<Scene>();
    Entity emitter = scene->CreateEntity("Emitter");
    emitter.AddComponent<NativeScriptComponent>().ClassName = "EventEmitter";
    Entity l1 = scene->CreateEntity("L1");
    l1.AddComponent<NativeScriptComponent>().ClassName = "EventListener";
    Entity l2 = scene->CreateEntity("L2");
    l2.AddComponent<NativeScriptComponent>().ClassName = "EventListener";

    g_MessageLog.clear();
    scene->OnRuntimeStart();
    CHECK(scene->GetEventBus().GetSubscriberCount("CoinCollected") == 2);
    scene->OnUpdateRuntime(0.016f);
    // Immediate delivery during OnUpdate, deferred one after OnLateUpdate.
    CHECK(g_MessageLog == std::vector<std::string>({ "L1:2", "L2:2", "emitted", "late", "L1:5", "L2:5" }));
    auto* listener = ScriptableEntity::GetScript<EventListener>(l1);
    CHECK(listener && listener->Coins == 7);

    // The sender of an event is the emitting entity.
    Entity sender;
    scene->GetEventBus().Subscribe("Who", [&](const ScriptMessage& m) { sender = m.Sender; });
    if (auto* em = ScriptableEntity::GetScript<EventEmitter>(emitter)) em->Emit("Who");
    CHECK(sender == emitter);

    // A destroyed script's subscriptions go away with it.
    scene->DestroyEntity(l2);
    CHECK(scene->GetEventBus().GetSubscriberCount("CoinCollected") == 1);
    CHECK(scene->GetEventBus().Emit("CoinCollected", 1) == 1);
    if (listener)
    {
        CHECK(listener->Unsubscribe(listener->Sub));
        CHECK(scene->GetEventBus().GetSubscriberCount("CoinCollected") == 0);
    }

    // Stopping play mode clears everything (handlers may live in the script module).
    scene->GetEventBus().EmitDeferred("Pending");
    scene->OnRuntimeStop();
    CHECK(scene->GetEventBus().GetSubscriptionCount() == 0);
    CHECK(scene->GetEventBus().GetQueuedCount() == 0);
    g_MessageLog.clear();
}

// ------------------------------------------------------------------ M01: hierarchy queries
class HierarchyProbe : public ScriptableEntity
{
};
ZM_REGISTER_SCRIPT(HierarchyProbe)

class LeafProbe : public ScriptableEntity
{
};
ZM_REGISTER_SCRIPT(LeafProbe)

TEST(HierarchyQueries)
{
    // Root -> A (light) -> A1 (camera, light), B (light); Other is unrelated.
    auto scene = CreateRef<Scene>();
    Entity root = scene->CreateEntity("Root");
    Entity a = scene->CreateEntity("A");
    Entity a1 = scene->CreateEntity("A1");
    Entity b = scene->CreateEntity("B");
    Entity other = scene->CreateEntity("Other");
    scene->SetParent(a, root);
    scene->SetParent(a1, a);
    scene->SetParent(b, root);
    a.AddComponent<PointLightComponent>().Intensity = 1.0f;
    a1.AddComponent<PointLightComponent>().Intensity = 2.0f;
    b.AddComponent<PointLightComponent>().Intensity = 3.0f;
    other.AddComponent<PointLightComponent>();
    a1.AddComponent<CameraComponent>();
    root.AddComponent<NativeScriptComponent>().ClassName = "HierarchyProbe";
    a1.AddComponent<NativeScriptComponent>().ClassName = "LeafProbe";

    // Scene-level helpers
    CHECK(scene->GetChildren(root) == std::vector<Entity>({ a, b }));
    CHECK(scene->GetChildCount(root) == 2 && scene->GetChildCount(a1) == 0);
    CHECK(scene->GetChild(root, 1) == b && !scene->GetChild(root, 2));
    CHECK(scene->FindChild(root, "A1") == a1);
    CHECK(!scene->FindChild(root, "A1", false));
    CHECK(scene->FindChild(root, "B", false) == b);
    CHECK(!scene->FindChild(root, "Other"));
    CHECK(scene->GetDescendants(root) == std::vector<Entity>({ a, a1, b }));
    CHECK(scene->GetDescendants(a1).empty());
    CHECK(scene->GetRoot(a1) == root && scene->GetRoot(root) == root);
    CHECK(scene->GetChildren(Entity{}).empty() && !scene->GetRoot(Entity{}));

    scene->OnRuntimeStart();
    auto* top = ScriptableEntity::GetScript<HierarchyProbe>(root);
    auto* leaf = ScriptableEntity::GetScript<LeafProbe>(a1);
    CHECK(top && leaf);
    if (top && leaf)
    {
        CHECK(top->GetChildren() == std::vector<Entity>({ a, b }));
        CHECK(top->GetChildCount() == 2 && top->GetChild(0) == a);
        CHECK(top->FindChild("A1") == a1 && !top->FindChild("A1", false));
        CHECK(top->GetDescendants().size() == 3);
        CHECK(leaf->GetRoot() == root && leaf->GetParent() == a);

        // In children: depth-first, the entity itself first when included.
        CHECK(top->GetComponentInChildren<PointLightComponent>() == &a.GetComponent<PointLightComponent>());
        CHECK(top->GetComponentInChildren<CameraComponent>() == &a1.GetComponent<CameraComponent>());
        CHECK(top->GetComponentInChildren<TextComponent>() == nullptr);
        auto lights = top->GetComponentsInChildren<PointLightComponent>();
        CHECK(lights.size() == 3);
        CHECK(lights.size() == 3 && lights[0]->Intensity == 1.0f && lights[1]->Intensity == 2.0f && lights[2]->Intensity == 3.0f);
        CHECK(top->FindInChildren<CameraComponent>() == a1);
        CHECK(!top->FindInChildren<TextComponent>());

        root.AddComponent<PointLightComponent>().Intensity = 9.0f;
        CHECK(top->GetComponentInChildren<PointLightComponent>()->Intensity == 9.0f);
        CHECK(top->GetComponentInChildren<PointLightComponent>(false)->Intensity == 1.0f);
        CHECK(top->GetComponentsInChildren<PointLightComponent>().size() == 4);
        CHECK(top->GetComponentsInChildren<PointLightComponent>(false).size() == 3);

        // In parents: nearest first.
        CHECK(leaf->GetComponentInParent<PointLightComponent>()->Intensity == 2.0f);       // itself
        CHECK(leaf->GetComponentInParent<PointLightComponent>(false)->Intensity == 1.0f);  // A
        CHECK(leaf->GetComponentsInParent<PointLightComponent>().size() == 3);
        CHECK(leaf->GetComponentsInParent<PointLightComponent>(false).size() == 2);
        CHECK(leaf->GetComponentInParent<CameraComponent>(false) == nullptr);
        CHECK(leaf->FindInParent<PointLightComponent>(false) == a);
        CHECK(leaf->FindInParent<CameraComponent>() == a1);

        // Script classes work like components.
        CHECK(top->GetComponentInChildren<LeafProbe>() == leaf);
        CHECK(top->GetComponentInChildren<HierarchyProbe>() == top);
        CHECK(top->GetComponentInChildren<HierarchyProbe>(false) == nullptr);
        CHECK(leaf->GetComponentInParent<HierarchyProbe>() == top);
        CHECK(leaf->GetComponentInParent<LeafProbe>(false) == nullptr);
        CHECK(top->GetComponentsInChildren<ScriptableEntity>().size() == 2);
        CHECK(top->FindInChildren<LeafProbe>() == a1);

        // Reparenting is reflected right away.
        scene->SetParent(b, a);
        CHECK(top->GetChildren() == std::vector<Entity>({ a }));
        CHECK(scene->GetChildren(a) == std::vector<Entity>({ a1, b }));
        CHECK(top->GetComponentsInChildren<PointLightComponent>(false).size() == 3);
        CHECK(top->FindChild("B", false) == Entity{});
        CHECK(top->FindChild("B") == b);
    }
    scene->OnRuntimeStop();
}

// ------------------------------------------------------------------ M01: name cache
TEST(FindEntityNameCache)
{
    auto scene = CreateRef<Scene>();
    std::vector<Entity> entities;
    for (int i = 0; i < 300; i++)
        entities.push_back(scene->CreateEntity("E" + std::to_string(i)));
    Entity dup1 = scene->CreateEntity("Dup");
    Entity dup2 = scene->CreateEntity("Dup");
    Entity dup3 = scene->CreateEntity("Dup");

    const uint32_t rebuilds0 = scene->GetNameCacheStats().Rebuilds;
    CHECK(scene->FindEntityByName("E150") == entities[150]);
    CHECK(scene->GetNameCacheStats().Rebuilds == rebuilds0 + 1);
    bool allFound = true;
    for (int round = 0; round < 5; round++)
        for (int i = 0; i < 300; i++)
            allFound &= scene->FindEntityByName("E" + std::to_string(i)) == entities[i];
    CHECK(allFound);
    CHECK(!scene->FindEntityByName("Missing"));
    CHECK(!scene->FindEntityByName(""));
    CHECK(scene->GetNameCacheStats().Rebuilds == rebuilds0 + 1); // 1500 lookups, one build

    // Duplicates: the first one in scene order; all of them on request.
    CHECK(scene->FindEntityByName("Dup") == dup1);
    CHECK(scene->FindEntitiesByName("Dup") == std::vector<Entity>({ dup1, dup2, dup3 }));
    CHECK(scene->FindEntitiesByName("Missing").empty());

    // Creating / destroying / SetName are seen immediately.
    Entity fresh = scene->CreateEntity("Fresh");
    CHECK(scene->FindEntityByName("Fresh") == fresh);
    scene->DestroyEntity(dup1);
    CHECK(scene->FindEntityByName("Dup") == dup2);
    CHECK(scene->FindEntitiesByName("Dup").size() == 2);
    entities[5].SetName("Renamed");
    CHECK(scene->FindEntityByName("Renamed") == entities[5]);
    CHECK(!scene->FindEntityByName("E5"));
    entities[6].SetName("");
    CHECK(entities[6].GetName() == "Entity");

    // A direct write to the name: a stale hit is detected at once ...
    CHECK(scene->FindEntityByName("E7") == entities[7]); // cache is clean now
    uint32_t beforeDirect = scene->GetNameCacheStats().Rebuilds;
    entities[7].GetComponent<TagComponent>().Tag = "Direct7";
    CHECK(!scene->FindEntityByName("E7"));
    CHECK(scene->FindEntityByName("Direct7") == entities[7]);
    CHECK(scene->GetNameCacheStats().Rebuilds == beforeDirect + 1);
    // ... a new name is found on the next frame at the latest ...
    scene->FindEntityByName("E9"); // validated for this frame
    entities[8].GetComponent<TagComponent>().Tag = "Direct8";
    scene->OnRuntimeStart();
    scene->OnUpdateRuntime(0.016f);
    CHECK(scene->FindEntityByName("Direct8") == entities[8]);
    CHECK(!scene->FindEntityByName("E8"));
    // ... or right away after InvalidateNameCache().
    entities[10].GetComponent<TagComponent>().Tag = "Direct10";
    scene->InvalidateNameCache();
    CHECK(scene->FindEntityByName("Direct10") == entities[10]);

    // One validation per frame, not per lookup.
    uint32_t validations = scene->GetNameCacheStats().Validations;
    uint32_t rebuilds = scene->GetNameCacheStats().Rebuilds;
    for (int frame = 0; frame < 10; frame++)
    {
        scene->OnUpdateRuntime(0.016f);
        for (int i = 0; i < 50; i++)
            scene->FindEntityByName("E" + std::to_string(100 + i));
    }
    CHECK(scene->GetNameCacheStats().Validations == validations + 10);
    CHECK(scene->GetNameCacheStats().Rebuilds == rebuilds);

    // Scripts use the same cache (and runtime spawns are found).
    Entity spawned = scene->Instantiate(fresh);
    CHECK(scene->FindEntitiesByName("Fresh").size() == 2);
    CHECK(scene->FindEntityByName("Fresh") == fresh);
    scene->DestroyEntity(fresh);
    CHECK(scene->FindEntityByName("Fresh") == spawned);
    scene->OnRuntimeStop();

    // A copied scene has its own cache.
    auto copy = Scene::Copy(scene);
    Entity copied = copy->FindEntityByName("E42");
    CHECK(copied && copied != entities[42] && copied.GetScene() == copy.get());
    CHECK(copied && copied.GetUUID() == entities[42].GetUUID());
    copied.SetName("OnlyInCopy");
    CHECK(copy->FindEntityByName("OnlyInCopy") == copied);
    CHECK(!scene->FindEntityByName("OnlyInCopy"));
    CHECK(scene->FindEntityByName("E42") == entities[42]);

    // Loading a scene fills the cache with the loaded names.
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(SceneSerializer(scene).SerializeToString()));
    CHECK(loaded->FindEntityByName("Renamed") && loaded->FindEntityByName("Renamed").GetUUID() == entities[5].GetUUID());
    CHECK(loaded->FindEntitiesByName("Dup").size() == 2);
}

// ------------------------------------------------------------------ script lifecycle (OnEnable / OnStart / OnDisable)
static std::vector<std::string> g_Life;
static bool HasLife(const std::string& entry) { return std::find(g_Life.begin(), g_Life.end(), entry) != g_Life.end(); }
static int CountLife(const std::string& entry) { return (int)std::count(g_Life.begin(), g_Life.end(), entry); }
static int IndexOfLife(const std::string& entry)
{
    auto it = std::find(g_Life.begin(), g_Life.end(), entry);
    return it == g_Life.end() ? -1 : (int)(it - g_Life.begin());
}

class LifeScript : public ScriptableEntity
{
public:
    int Mode = 0; // 1: disable in OnCreate, 2: disable in OnStart, 3: disable in OnEnable, 4: spawn in OnUpdate
    int TimerFired = 0;

    void OnCreate() override { Log("Create"); if (Mode == 1) SetEnabled(false); }
    void OnEnable() override
    {
        Log("Enable");
        if (Mode == 3 && !m_DidToggle) { m_DidToggle = true; SetEnabled(false); }
    }
    void OnStart() override { Log("Start"); if (Mode == 2) SetEnabled(false); }
    void OnUpdate(Timestep) override
    {
        Log("Update");
        if (Mode == 4 && !m_Spawned)
        {
            m_Spawned = true;
            Entity clone = Instantiate(FindEntity("Template"));
            clone.SetName("Clone");
        }
    }
    void OnLateUpdate(Timestep) override { Log("Late"); }
    void OnDisable() override { Log("Disable"); }
    void OnDestroy() override { Log("Destroy"); }

private:
    void Log(const char* what) { g_Life.push_back(GetName() + ":" + what); }
    bool m_DidToggle = false, m_Spawned = false;
};
ZM_REGISTER_SCRIPT(LifeScript, ZM_FIELD(LifeScript, Mode))

static Entity AddLifeScript(Scene& scene, const std::string& name, int mode = 0, bool enabled = true)
{
    Entity e = scene.CreateEntity(name);
    auto& nsc = e.AddComponent<NativeScriptComponent>();
    nsc.ClassName = "LifeScript";
    nsc.Enabled = enabled;
    ScriptFieldValue v; v.Set(mode);
    nsc.Fields["Mode"] = v;
    return e;
}

TEST(ScriptLifecycleOrder)
{
    g_Life.clear();
    auto scene = CreateRef<Scene>();
    Entity a = AddLifeScript(*scene, "A");
    Entity b = AddLifeScript(*scene, "B");
    Entity c = AddLifeScript(*scene, "C", 0, false); // disabled in the inspector

    // Every OnCreate first, then OnEnable (only for enabled scripts); OnStart waits for the first frame.
    scene->OnRuntimeStart();
    CHECK((g_Life == std::vector<std::string>{ "A:Create", "B:Create", "C:Create", "A:Enable", "B:Enable" }));
    auto* sa = ScriptableEntity::GetScript<LifeScript>(a);
    auto* sc = ScriptableEntity::GetScript<LifeScript>(c);
    CHECK(sa && sa->IsEnabled() && !sa->HasStarted());
    CHECK(sc && !sc->IsEnabled());

    g_Life.clear();
    scene->OnUpdateRuntime(0.016f);
    CHECK((g_Life == std::vector<std::string>{ "A:Start", "B:Start", "A:Update", "B:Update", "A:Late", "B:Late" }));
    CHECK(sa && sa->HasStarted());
    CHECK(sc && !sc->HasStarted());

    g_Life.clear();
    scene->OnUpdateRuntime(0.016f);
    CHECK((g_Life == std::vector<std::string>{ "A:Update", "B:Update", "A:Late", "B:Late" }));

    // Enabling: OnEnable at once, OnStart right before its first OnUpdate.
    g_Life.clear();
    scene->SetScriptEnabled(c, true);
    CHECK((g_Life == std::vector<std::string>{ "C:Enable" }));
    CHECK(scene->IsScriptEnabled(c) && c.GetComponent<NativeScriptComponent>().Enabled);
    scene->OnUpdateRuntime(0.016f);
    CHECK((g_Life == std::vector<std::string>{ "C:Enable", "C:Start", "A:Update", "B:Update", "C:Update", "A:Late", "B:Late", "C:Late" }));

    // Disabling from a script: OnDisable at once, no per-frame callbacks, timers pause.
    sa->Invoke(0.05f, [sa]() { sa->TimerFired++; });
    g_Life.clear();
    sa->SetEnabled(false);
    CHECK((g_Life == std::vector<std::string>{ "A:Disable" }));
    CHECK(!sa->IsEnabled() && !a.GetComponent<NativeScriptComponent>().Enabled);
    sa->SetEnabled(false); // no change -> no callback
    CHECK(g_Life.size() == 1);
    for (int i = 0; i < 10; i++)
        scene->OnUpdateRuntime(0.016f);
    CHECK(!HasLife("A:Update") && !HasLife("A:Late"));
    CHECK(CountLife("B:Update") == 10);
    CHECK(sa->TimerFired == 0 && sa->IsInvoking());

    // A direct write to the component is noticed at the start of the next frame.
    g_Life.clear();
    b.GetComponent<NativeScriptComponent>().Enabled = false;
    scene->OnUpdateRuntime(0.016f);
    CHECK(!g_Life.empty() && g_Life.front() == "B:Disable");
    CHECK(!HasLife("B:Update"));

    // Re-enabling does not start the script again; its paused timer continues.
    g_Life.clear();
    sa->SetEnabled(true);
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(0.016f);
    CHECK(CountLife("A:Enable") == 1 && !HasLife("A:Start"));
    CHECK(CountLife("A:Update") == 5);
    CHECK(sa->TimerFired == 1);

    // Stop: enabled scripts get OnDisable then OnDestroy, disabled ones only OnDestroy.
    g_Life.clear();
    scene->OnRuntimeStop();
    CHECK(CountLife("A:Destroy") == 1 && CountLife("B:Destroy") == 1 && CountLife("C:Destroy") == 1);
    CHECK(CountLife("A:Disable") == 1 && IndexOfLife("A:Disable") + 1 == IndexOfLife("A:Destroy"));
    CHECK(CountLife("C:Disable") == 1 && IndexOfLife("C:Disable") + 1 == IndexOfLife("C:Destroy"));
    CHECK(!HasLife("B:Disable")); // B was already disabled
    CHECK(!a.GetComponent<NativeScriptComponent>().Instance);
}

static int g_FlipCount = 0;
class FlipFlopScript : public ScriptableEntity
{
public:
    // Toggles itself back from both callbacks: must not recurse forever.
    void OnEnable() override { if (++g_FlipCount < 1000) SetEnabled(false); }
    void OnDisable() override { if (++g_FlipCount < 1000) SetEnabled(true); }
};
ZM_REGISTER_SCRIPT(FlipFlopScript)

TEST(ScriptLifecycleEdgeCases)
{
    g_Life.clear();
    auto scene = CreateRef<Scene>();
    AddLifeScript(*scene, "OffInCreate", 1);
    AddLifeScript(*scene, "OffInStart", 2);
    Entity toggled = AddLifeScript(*scene, "OffInEnable", 3);
    AddLifeScript(*scene, "Spawner", 4);
    Entity tmpl = AddLifeScript(*scene, "Template");
    tmpl.GetComponent<NativeScriptComponent>().Enabled = false; // the template itself stays idle
    Entity flip = scene->CreateEntity("Flip");
    flip.AddComponent<NativeScriptComponent>().ClassName = "FlipFlopScript";

    scene->OnRuntimeStart();
    CHECK(HasLife("OffInCreate:Create") && !HasLife("OffInCreate:Enable"));
    CHECK(CountLife("OffInEnable:Enable") == 1 && CountLife("OffInEnable:Disable") == 1);
    CHECK(!scene->IsScriptEnabled(toggled));
    CHECK(g_FlipCount > 0 && g_FlipCount < 100); // stopped by the nesting guard

    // Paused at start: nothing starts until a frame is stepped.
    scene->SetPaused(true);
    g_Life.clear();
    scene->OnUpdateRuntime(0.016f);
    CHECK(g_Life.empty());
    scene->Step(1);
    scene->OnUpdateRuntime(0.016f);
    CHECK(HasLife("OffInStart:Start") && HasLife("OffInStart:Disable") && !HasLife("OffInStart:Update"));
    CHECK(!HasLife("OffInCreate:Start") && !HasLife("Template:Start"));
    CHECK(IndexOfLife("Spawner:Start") >= 0 && IndexOfLife("Spawner:Start") < IndexOfLife("Spawner:Update"));
    // The clone (copied from a disabled template, then enabled) is created during Spawner's
    // OnUpdate: OnCreate now, but no OnStart / OnUpdate / OnLateUpdate in this frame.
    Entity clone = scene->FindEntityByName("Clone");
    CHECK((bool)clone);
    CHECK(HasLife("Template:Create")); // the clone's OnCreate runs before it is renamed
    CHECK(!HasLife("Clone:Update") && !HasLife("Clone:Late"));
    scene->SetPaused(false);

    g_Life.clear();
    scene->SetScriptEnabled(clone, true);
    scene->OnUpdateRuntime(0.016f);
    CHECK(IndexOfLife("Clone:Enable") == 0);
    CHECK(IndexOfLife("Clone:Start") >= 0 && IndexOfLife("Clone:Start") < IndexOfLife("Clone:Update"));
    CHECK(IndexOfLife("Spawner:Update") >= 0 && IndexOfLife("Clone:Start") < IndexOfLife("Spawner:Update"));
    CHECK(CountLife("Clone:Start") == 1);

    // Destroying an enabled script: OnDisable, then OnDestroy.
    g_Life.clear();
    scene->DestroyEntity(clone);
    CHECK((g_Life == std::vector<std::string>{ "Clone:Disable", "Clone:Destroy" }));
    scene->OnRuntimeStop();

    // The Enabled flag is saved only when it is off (older files stay byte-identical).
    auto saved = CreateRef<Scene>();
    Entity on = saved->CreateEntity("On");
    on.AddComponent<NativeScriptComponent>().ClassName = "LifeScript";
    Entity off = saved->CreateEntity("Off");
    off.AddComponent<NativeScriptComponent>().ClassName = "LifeScript";
    off.GetComponent<NativeScriptComponent>().Enabled = false;
    std::string text = SceneSerializer(saved).SerializeToString();
    CHECK(std::count(text.begin(), text.end(), 'E') > 0);
    CHECK(text.find("\"Enabled\": false") != std::string::npos);
    CHECK(text.find("\"Enabled\": true") == std::string::npos);
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    CHECK(loaded->FindEntityByName("On").GetComponent<NativeScriptComponent>().Enabled);
    CHECK(!loaded->FindEntityByName("Off").GetComponent<NativeScriptComponent>().Enabled);
    // Copies (Ctrl+D, prefabs, play-mode scene copy) keep the flag.
    CHECK(!saved->DuplicateEntity(off).GetComponent<NativeScriptComponent>().Enabled);
    CHECK(!Scene::Copy(saved)->FindEntityByName("Off").GetComponent<NativeScriptComponent>().Enabled);
}

TEST(DestroyKeepsOtherScriptsAlive)
{
    // Regression: entt fills the hole of a removed component by moving the last one; the
    // moved NativeScriptComponent used to lose its Instance (leaked, never updated again).
    g_Life.clear();
    auto scene = CreateRef<Scene>();
    std::vector<Entity> entities;
    for (int i = 0; i < 6; i++)
        entities.push_back(AddLifeScript(*scene, "S" + std::to_string(i)));
    scene->OnRuntimeStart();
    scene->OnUpdateRuntime(0.016f);

    scene->DestroyEntity(entities[0]);                                   // immediate
    entities[2].GetComponent<NativeScriptComponent>().Instance->Destroy(); // deferred
    scene->OnUpdateRuntime(0.016f);
    g_Life.clear();
    scene->OnUpdateRuntime(0.016f);
    for (int i : { 1, 3, 4, 5 })
    {
        CHECK(entities[i].GetComponent<NativeScriptComponent>().Instance != nullptr);
        CHECK(CountLife("S" + std::to_string(i) + ":Update") == 1);
    }
    scene->OnRuntimeStop();
    for (int i = 0; i < 6; i++)
        CHECK(CountLife("S" + std::to_string(i) + ":Destroy") == (i == 0 || i == 2 ? 0 : 1));
}

// ------------------------------------------------------------------ triggers (OnTriggerBegin / OnTriggerEnd)
static std::vector<std::string> g_Contacts;
static int CountContact(const std::string& entry) { return (int)std::count(g_Contacts.begin(), g_Contacts.end(), entry); }
static int IndexOfContact(const std::string& entry)
{
    auto it = std::find(g_Contacts.begin(), g_Contacts.end(), entry);
    return it == g_Contacts.end() ? -1 : (int)(it - g_Contacts.begin());
}

class ContactLogScript : public ScriptableEntity
{
public:
    void OnCollisionBegin(Entity other) override { Log("CollisionBegin", other); }
    void OnCollisionEnd(Entity other) override { Log("CollisionEnd", other); }
    void OnTriggerBegin(Entity other) override { Log("TriggerBegin", other); }
    void OnTriggerEnd(Entity other) override { Log("TriggerEnd", other); }
private:
    void Log(const char* what, Entity other)
    {
        g_Contacts.push_back(GetName() + ":" + what + ":" + (other ? other.GetName() : std::string("?")));
    }
};
ZM_REGISTER_SCRIPT(ContactLogScript)

static void AddContactLog(Entity e) { e.AddComponent<NativeScriptComponent>().ClassName = "ContactLogScript"; }

TEST(TriggerCallbacks2D)
{
    g_Contacts.clear();
    auto scene = CreateRef<Scene>();
    Entity ground = scene->CreateEntity("Ground");
    ground.GetTransform().Translation = { 0, -2, 0 };
    ground.GetTransform().Scale = { 20, 1, 1 };
    ground.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
    ground.AddComponent<BoxCollider2DComponent>();
    AddContactLog(ground);

    Entity zone = scene->CreateEntity("Zone"); // sensor the box falls through
    zone.GetTransform().Translation = { 0, 1, 0 };
    zone.GetTransform().Scale = { 4, 1, 1 };
    zone.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
    zone.AddComponent<BoxCollider2DComponent>().IsSensor = true;
    AddContactLog(zone);

    // Two fixtures on one body: scripts still hear one Begin per entity pair.
    Entity box = scene->CreateEntity("Box");
    box.GetTransform().Translation = { 0, 4, 0 };
    box.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    box.AddComponent<BoxCollider2DComponent>();
    box.AddComponent<CircleCollider2DComponent>().Radius = 0.5f;
    AddContactLog(box);

    scene->OnRuntimeStart();
    for (int i = 0; i < 180; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(box.GetTransform().Translation.y, -1.0f, 0.05f); // went through the sensor, rests on the ground

    CHECK(CountContact("Box:TriggerBegin:Zone") == 1);
    CHECK(CountContact("Box:TriggerEnd:Zone") == 1);
    CHECK(CountContact("Zone:TriggerBegin:Box") == 1);
    CHECK(CountContact("Zone:TriggerEnd:Box") == 1);
    CHECK(CountContact("Box:CollisionBegin:Ground") == 1);
    CHECK(CountContact("Ground:CollisionBegin:Box") == 1);
    CHECK(CountContact("Box:CollisionBegin:Zone") == 0 && CountContact("Zone:CollisionBegin:Box") == 0);
    CHECK(CountContact("Box:TriggerBegin:Ground") == 0);
    CHECK(CountContact("Box:CollisionEnd:Ground") == 0); // still resting
    int enter = IndexOfContact("Box:TriggerBegin:Zone"), leave = IndexOfContact("Box:TriggerEnd:Zone");
    CHECK(enter >= 0 && enter < leave && leave < IndexOfContact("Box:CollisionBegin:Ground"));
    scene->OnRuntimeStop();
}

TEST(TriggerEndWhenDestroyed)
{
    g_Contacts.clear();
    auto scene = CreateRef<Scene>();
    scene->GetSettings().Gravity = { 0.0f, 0.0f };
    Entity zone = scene->CreateEntity("Zone");
    zone.GetTransform().Scale = { 6, 6, 1 };
    zone.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
    zone.AddComponent<BoxCollider2DComponent>().IsSensor = true;
    AddContactLog(zone);
    Entity a = scene->CreateEntity("A");
    a.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    a.AddComponent<BoxCollider2DComponent>();
    AddContactLog(a);
    Entity b = scene->CreateEntity("B");
    b.GetTransform().Translation = { 1.5f, 0, 0 };
    b.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    b.AddComponent<CircleCollider2DComponent>();
    AddContactLog(b);

    scene->OnRuntimeStart();
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(CountContact("A:TriggerBegin:Zone") == 1 && CountContact("Zone:TriggerBegin:A") == 1);
    CHECK(CountContact("B:TriggerBegin:Zone") == 1);

    // The survivor hears OnTriggerEnd with a still valid `other` while it is being destroyed ...
    g_Contacts.clear();
    a.GetComponent<NativeScriptComponent>().Instance->Destroy(); // deferred: end of this frame
    scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK((g_Contacts == std::vector<std::string>{ "Zone:TriggerEnd:A" }));
    // ... and only once (Box2D's own end-contact for the destroyed body is ignored).
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(g_Contacts.size() == 1);

    // Destroying the sensor ends the trigger for the bodies inside it.
    g_Contacts.clear();
    scene->DestroyEntity(zone);
    CHECK((g_Contacts == std::vector<std::string>{ "B:TriggerEnd:Zone" }));
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(g_Contacts.size() == 1);
    scene->OnRuntimeStop();
}

TEST(TriggerCallbacks3D)
{
    g_Contacts.clear();
    auto scene = CreateRef<Scene>();
    Entity floor = scene->CreateEntity("Floor");
    floor.GetTransform().Translation = { 0, -0.5f, 0 };
    floor.GetTransform().Scale = { 30, 1, 30 };
    floor.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    floor.AddComponent<BoxCollider3DComponent>();
    AddContactLog(floor);

    Entity zone = scene->CreateEntity("Zone"); // the ball falls through it
    zone.GetTransform().Translation = { 0, 3, 0 };
    zone.GetTransform().Scale = { 3, 1, 3 };
    zone.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    zone.AddComponent<BoxCollider3DComponent>().IsSensor = true;
    AddContactLog(zone);

    Entity ball = scene->CreateEntity("Ball");
    ball.GetTransform().Translation = { 0, 6, 0 };
    ball.AddComponent<Rigidbody3DComponent>();
    ball.AddComponent<SphereCollider3DComponent>().Radius = 0.5f;
    AddContactLog(ball);

    // A character walking into a sensor (CharacterVirtual + its inner body: still one Begin).
    Entity pad = scene->CreateEntity("Pad");
    pad.GetTransform().Translation = { 8, 1, 0 };
    pad.GetTransform().Scale = { 2, 2, 2 };
    pad.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    pad.AddComponent<BoxCollider3DComponent>().IsSensor = true;
    AddContactLog(pad);
    Entity hero = scene->CreateEntity("Hero");
    hero.GetTransform().Translation = { 4, 0.1f, 0 };
    auto& cc = hero.AddComponent<CharacterController3DComponent>();
    AddContactLog(hero);

    scene->OnRuntimeStart();
    for (int i = 0; i < 60; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    cc.Move({ 3.0f, 0, 0 });
    for (int i = 0; i < 150; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(ball.GetTransform().Translation.y, 0.5f, 0.05f);
    CHECK(hero.GetTransform().Translation.x > 8.0f); // walked through the pad (sensors do not block)

    CHECK(CountContact("Ball:TriggerBegin:Zone") == 1 && CountContact("Ball:TriggerEnd:Zone") == 1);
    CHECK(CountContact("Zone:TriggerBegin:Ball") == 1 && CountContact("Zone:TriggerEnd:Ball") == 1);
    CHECK(CountContact("Ball:CollisionBegin:Zone") == 0);
    CHECK(CountContact("Ball:CollisionBegin:Floor") == 1);
    int leave = IndexOfContact("Ball:TriggerEnd:Zone");
    CHECK(leave >= 0 && leave < IndexOfContact("Ball:CollisionBegin:Floor"));

    CHECK(CountContact("Hero:TriggerBegin:Pad") == 1);
    CHECK(CountContact("Pad:TriggerBegin:Hero") == 1);
    CHECK(CountContact("Hero:CollisionBegin:Pad") == 0);
    CHECK(CountContact("Hero:CollisionBegin:Floor") == 1);
    scene->OnRuntimeStop();
}

// ------------------------------------------------------------------ script field attributes (enum, color, range, tooltip)
class FieldAttributeScript : public ScriptableEntity
{
public:
    enum class Tiny : uint8_t { A = 0, B = 1, Max = 255 };
    enum class Signed8 : int8_t { Minus = -100, Zero = 0, Plus = 100 };
    enum class Wide : int64_t { Low = -(1ll << 40), High = 1ll << 40 };
    enum Plain { PlainA, PlainB, PlainC }; // unscoped, int-sized

    uint8_t GuardBefore = 0xAB;
    Tiny Small = Tiny::B;
    uint8_t GuardAfter = 0xCD;
    Signed8 Neg = Signed8::Minus;
    Wide Big = Wide::High;
    Plain Simple = PlainB;
    int Difficulty = 1;
    float Speed = 5.0f;
    int Lives = 3;
    glm::vec3 Offset{ 0.0f };
    glm::vec4 Tint{ 1.0f };
    glm::vec3 Glow{ 2.0f, 1.0f, 0.5f };
    std::string Notes = "line1\nline2";
};
ZM_REGISTER_SCRIPT(FieldAttributeScript,
    ZM_FIELD_ENUM(FieldAttributeScript, Small, "A", "B", { "Max", FieldAttributeScript::Tiny::Max }),
    ZM_FIELD(FieldAttributeScript, Neg),
    ZM_FIELD(FieldAttributeScript, Big),
    ZM_FIELD_ENUM(FieldAttributeScript, Simple, "First", "Second", { "Tenth", 10 }, "Eleventh").Label("Oddiy"),
    ZM_FIELD_ENUM(FieldAttributeScript, Difficulty, "Oson", "O'rta", "Qiyin"),
    ZM_FIELD_RANGE(FieldAttributeScript, Speed, 20.0f, 0.5f).Tooltip("Tezlik").Header("Harakat"),
    ZM_FIELD_RANGE(FieldAttributeScript, Lives, 0.5f, 3.7f),
    ZM_FIELD_RANGE(FieldAttributeScript, Offset, -1, 1),
    ZM_FIELD_COLOR(FieldAttributeScript, Tint),
    ZM_FIELD_COLOR_HDR(FieldAttributeScript, Glow),
    ZM_FIELD_TOOLTIP(FieldAttributeScript, Notes, "Izoh").Multiline())

static const ScriptFieldInfo* FindField(const ScriptClassInfo* info, const char* name)
{
    if (!info) return nullptr;
    for (const auto& f : info->Fields)
        if (f.Name == name) return &f;
    return nullptr;
}

TEST(ScriptFieldEnumColorRange)
{
    const ScriptClassInfo* info = ScriptRegistry::Find("FieldAttributeScript");
    CHECK(info && info->Fields.size() == 11);
    const ScriptFieldInfo* small = FindField(info, "Small");
    const ScriptFieldInfo* neg = FindField(info, "Neg");
    const ScriptFieldInfo* big = FindField(info, "Big");
    const ScriptFieldInfo* simple = FindField(info, "Simple");
    const ScriptFieldInfo* difficulty = FindField(info, "Difficulty");
    const ScriptFieldInfo* speed = FindField(info, "Speed");
    const ScriptFieldInfo* lives = FindField(info, "Lives");
    const ScriptFieldInfo* offset = FindField(info, "Offset");
    const ScriptFieldInfo* tint = FindField(info, "Tint");
    const ScriptFieldInfo* glow = FindField(info, "Glow");
    const ScriptFieldInfo* notes = FindField(info, "Notes");
    CHECK(small && neg && big && simple && difficulty && speed && lives && offset && tint && glow && notes);
    if (!(small && neg && big && simple && difficulty && speed && lives && offset && tint && glow && notes))
        return;

    // ---- enums: type, underlying size / sign, option values (auto values continue)
    CHECK(small->Type == ScriptFieldType::Enum && small->EnumSize == 1 && !small->EnumSigned);
    CHECK(neg->Type == ScriptFieldType::Enum && neg->EnumSize == 1 && neg->EnumSigned && neg->EnumOptions.empty());
    CHECK(big->Type == ScriptFieldType::Enum && big->EnumSize == 8 && big->EnumSigned);
    CHECK(simple->Type == ScriptFieldType::Enum && simple->EnumSize == sizeof(int));
    CHECK(small->EnumOptions.size() == 3 && small->EnumOptions[2].Name == "Max" && small->EnumOptions[2].Value == 255);
    CHECK(simple->EnumOptions.size() == 4);
    CHECK(simple->EnumOptions.size() == 4 && simple->EnumOptions[0].Value == 0 && simple->EnumOptions[1].Value == 1 &&
          simple->EnumOptions[2].Value == 10 && simple->EnumOptions[3].Value == 11);
    CHECK(simple->FindOption(10) && simple->FindOption(10)->Name == "Tenth");
    CHECK(!simple->FindOption(5));
    CHECK(simple->GetDisplayName() == "Oddiy" && small->GetDisplayName() == "Small");
    CHECK(difficulty->Type == ScriptFieldType::Int && difficulty->EnumOptions.size() == 3 && difficulty->EnumOptions[2].Name == "Qiyin");

    // ---- range / tooltip / header / color / multiline
    CHECK(speed->HasRange && speed->RangeMin == 0.5f && speed->RangeMax == 20.0f); // swapped into order
    CHECK(speed->TooltipText == "Tezlik" && speed->HeaderText == "Harakat");
    CHECK(!lives->TooltipText.size() && lives->HasRange);
    CHECK(offset->HasRange && offset->RangeMin == -1.0f && offset->RangeMax == 1.0f);
    CHECK(tint->IsColor && !tint->IsHdr && tint->Type == ScriptFieldType::Vec4);
    CHECK(glow->IsColor && glow->IsHdr && glow->Type == ScriptFieldType::Vec3);
    CHECK(notes->IsMultiline && notes->TooltipText == "Izoh");
    CHECK(!FindField(info, "Speed")->IsColor && !small->HasRange);

    ScriptFieldValue v;
    v.Set(25.0f); speed->ClampToRange(v); CHECK(v.Get<float>() == 20.0f);
    v.Set(-3.0f); speed->ClampToRange(v); CHECK(v.Get<float>() == 0.5f);
    v.Set(7.0f);  speed->ClampToRange(v); CHECK(v.Get<float>() == 7.0f);
    v.Set(9);     lives->ClampToRange(v); CHECK(v.Get<int>() == 3);  // whole numbers inside [0.5, 3.7]
    v.Set(0);     lives->ClampToRange(v); CHECK(v.Get<int>() == 1);
    v.Set(glm::vec3(-5.0f, 0.25f, 9.0f)); offset->ClampToRange(v);
    CHECK(v.Get<glm::vec3>() == glm::vec3(-1.0f, 0.25f, 1.0f));
    v.Set(100.0f); lives->ClampToRange(v); CHECK(v.Get<float>() == 100.0f); // other type: untouched
    v.Set(100.0f); tint->ClampToRange(v); CHECK(v.Get<float>() == 100.0f);  // no range

    // ---- Load / Store with the member's real size and sign
    auto* script = static_cast<FieldAttributeScript*>(info->Factory());
    ScriptFieldValue loaded;
    small->Load(script, loaded);
    CHECK(loaded.Type == ScriptFieldType::Enum && loaded.GetEnum() == 1);
    ScriptFieldValue in; in.SetEnum(255);
    CHECK(small->Store(script, in));
    CHECK(script->Small == FieldAttributeScript::Tiny::Max);
    CHECK(script->GuardBefore == 0xAB && script->GuardAfter == 0xCD); // only one byte written
    small->Load(script, loaded);
    CHECK(loaded.GetEnum() == 255); // unsigned underlying type: not -1
    neg->Load(script, loaded);
    CHECK(loaded.GetEnum() == -100);
    in.SetEnum(100); CHECK(neg->Store(script, in)); CHECK(script->Neg == FieldAttributeScript::Signed8::Plus);
    big->Load(script, loaded);
    CHECK(loaded.GetEnum() == (1ll << 40));
    in.SetEnum(-(1ll << 40)); CHECK(big->Store(script, in)); CHECK(script->Big == FieldAttributeScript::Wide::Low);
    in.Set(FieldAttributeScript::PlainC); // Set<T> with an enum type
    CHECK(in.Type == ScriptFieldType::Enum && in.Get<FieldAttributeScript::Plain>() == FieldAttributeScript::PlainC);
    CHECK(simple->Store(script, in) && script->Simple == FieldAttributeScript::PlainC);

    // Int <-> Enum conversions keep the number (a field changed from int to an enum or back).
    ScriptFieldValue asInt; asInt.Set(2);
    CHECK(simple->Store(script, asInt) && script->Simple == FieldAttributeScript::PlainC);
    ScriptFieldValue asEnum; asEnum.SetEnum(2);
    CHECK(difficulty->Store(script, asEnum) && script->Difficulty == 2);
    ScriptFieldValue huge; huge.SetEnum(1ll << 40);
    ScriptFieldValue converted;
    CHECK(difficulty->Convert(huge, converted) && converted.Type == ScriptFieldType::Int &&
          converted.Get<int>() == std::numeric_limits<int>::max());
    ScriptFieldValue wrong; wrong.Set(1.5f);
    CHECK(!simple->Store(script, wrong) && !simple->Convert(wrong, converted));
    CHECK(script->Simple == FieldAttributeScript::PlainC);
    tint->Load(script, loaded);
    CHECK(loaded.Type == ScriptFieldType::Vec4 && loaded.Get<glm::vec4>() == glm::vec4(1.0f));
    delete script;

    // ---- stored values reach the instance at play start (with Int -> Enum conversion) and
    // Enum values survive serialization (negative and 64-bit included).
    auto scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Attrs");
    auto& nsc = e.AddComponent<NativeScriptComponent>();
    nsc.ClassName = "FieldAttributeScript";
    ScriptFieldValue smallValue; smallValue.SetEnum(255);
    ScriptFieldValue negValue; negValue.SetEnum(100);
    ScriptFieldValue bigValue; bigValue.SetEnum(-(1ll << 40));
    ScriptFieldValue simpleAsInt; simpleAsInt.Set(10); // an old scene where Simple was an int
    nsc.Fields["Small"] = smallValue;
    nsc.Fields["Neg"] = negValue;
    nsc.Fields["Big"] = bigValue;
    nsc.Fields["Simple"] = simpleAsInt;

    std::string text = SceneSerializer(scene).SerializeToString();
    CHECK(text.find("\"Enum\"") != std::string::npos);
    auto loadedScene = CreateRef<Scene>();
    CHECK(SceneSerializer(loadedScene).DeserializeFromString(text));
    Entity le = loadedScene->FindEntityByName("Attrs");
    CHECK(le && le.GetComponent<NativeScriptComponent>().Fields["Big"].Type == ScriptFieldType::Enum);
    CHECK(le && le.GetComponent<NativeScriptComponent>().Fields["Big"].GetEnum() == -(1ll << 40));
    CHECK(le && le.GetComponent<NativeScriptComponent>().Fields["Small"].GetEnum() == 255);
    CHECK(SceneSerializer(loadedScene).SerializeToString() == text);

    loadedScene->OnRuntimeStart();
    auto* live = ScriptableEntity::GetScript<FieldAttributeScript>(le);
    CHECK(live && live->Small == FieldAttributeScript::Tiny::Max && live->GuardAfter == 0xCD);
    CHECK(live && live->Neg == FieldAttributeScript::Signed8::Plus);
    CHECK(live && live->Big == FieldAttributeScript::Wide::Low);
    CHECK(live && (int)live->Simple == 10);
    loadedScene->OnRuntimeStop();

    // Hand-edited / foreign JSON: a float value for an Enum field is truncated, junk becomes 0.
    auto junk = CreateRef<Scene>();
    std::string edited = text;
    size_t pos = edited.find("-1099511627776");
    CHECK(pos != std::string::npos);
    if (pos != std::string::npos)
    {
        edited.replace(pos, std::strlen("-1099511627776"), "7.9");
        CHECK(SceneSerializer(junk).DeserializeFromString(edited));
        Entity je = junk->FindEntityByName("Attrs");
        CHECK(je && je.GetComponent<NativeScriptComponent>().Fields["Big"].GetEnum() == 7);
    }
}

// ------------------------------------------------------------------ new-script templates
// Skips string / char literals and comments; true when (), {} and [] are balanced.
static bool BracketsBalanced(const std::string& code)
{
    std::vector<char> stack;
    for (size_t i = 0; i < code.size(); i++)
    {
        char c = code[i];
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') { i = code.find('\n', i); if (i == std::string::npos) break; continue; }
        if (c == '"' || c == '\'')
        {
            for (i++; i < code.size() && code[i] != c; i++)
                if (code[i] == '\\') i++;
            continue;
        }
        if (c == '(' || c == '{' || c == '[') stack.push_back(c);
        else if (c == ')' || c == '}' || c == ']')
        {
            char open = c == ')' ? '(' : c == '}' ? '{' : '[';
            if (stack.empty() || stack.back() != open) return false;
            stack.pop_back();
        }
    }
    return stack.empty();
}

TEST(ScriptTemplates)
{
    std::string full = ScriptTemplate::Generate("EnemyAI", ScriptTemplateKind::Full);
    CHECK(full.find("$CLASS$") == std::string::npos);
    CHECK(full.find("class EnemyAI : public ScriptableEntity") != std::string::npos);
    CHECK(full.find("ZM_REGISTER_SCRIPT(EnemyAI,") != std::string::npos);
    CHECK(full.find("#include <Zamin/Zamin.h>") != std::string::npos);
    CHECK(BracketsBalanced(full));
    // Every ScriptableEntity callback, overridden.
    for (const char* callback : { "OnCreate()", "OnEnable()", "OnStart()", "OnUpdate(Timestep", "OnLateUpdate(Timestep",
                                  "OnDisable()", "OnDestroy()", "OnCollisionBegin(Entity", "OnCollisionEnd(Entity",
                                  "OnTriggerBegin(Entity", "OnTriggerEnd(Entity", "OnClick()", "OnMessage(const ScriptMessage&" })
        CHECK(full.find(std::string("void ") + callback) != std::string::npos);
    CHECK(std::count(full.begin(), full.end(), '\n') > 100);
    // ... and every kind of inspector field attribute.
    for (const char* attribute : { "ZM_FIELD_RANGE(", "ZM_FIELD_ENUM(", "ZM_FIELD_COLOR(", "ZM_FIELD_TOOLTIP(", ".Header(",
                                   ".Tooltip(", ".Label(", ".Multiline(", "EntityRef " })
        CHECK(full.find(attribute) != std::string::npos);

    std::string minimal = ScriptTemplate::Generate("Spinner", ScriptTemplateKind::Minimal);
    CHECK(minimal.find("class Spinner : public ScriptableEntity") != std::string::npos);
    CHECK(minimal.find("void OnCreate()") != std::string::npos && minimal.find("void OnUpdate(Timestep") != std::string::npos);
    CHECK(minimal.find("OnTriggerBegin") == std::string::npos);
    CHECK(minimal.find("ZM_REGISTER_SCRIPT(Spinner,") != std::string::npos);
    CHECK(BracketsBalanced(minimal) && minimal.size() < full.size() / 3);

    std::string starter = ScriptTemplate::GenerateProjectStarter();
    CHECK(starter.find("ZM_REGISTER_SCRIPT(Rotator") != std::string::npos);
    CHECK(starter.find("ZM_REGISTER_SCRIPT(PlayerController") != std::string::npos);
    CHECK(BracketsBalanced(starter));
    CHECK(std::string(ScriptTemplate::GetKindName(ScriptTemplateKind::Full)) != ScriptTemplate::GetKindName(ScriptTemplateKind::Minimal));

    // ---- class names
    for (const char* ok : { "Player", "EnemyAI", "Health", "my_script2", "_private", "CameraController", "Animation" })
        CHECK(ScriptTemplate::IsValidClassName(ok));
    for (const char* bad : { "", "2Fast", "my script", "Player!", "O\xCA\xBByinchi", "class", "override", "_Upper", "a__b",
                             "Entity", "Camera", "Input", "Scene", "Random", "TransformComponent", "Transform", "Destroy",
                             "OnUpdate", "MoveMode", "std", "glm" })
    {
        CHECK(!ScriptTemplate::IsValidClassName(bad));
        CHECK(!ScriptTemplate::ValidateClassName(bad).empty());
    }
    CHECK(ScriptTemplate::ValidateClassName("Camera").find("CameraController") != std::string::npos); // suggests a fix
    CHECK(ScriptTemplate::SanitizeClassName("my enemy-2") == "myenemy2");
    CHECK(ScriptTemplate::SanitizeClassName("2fast") == "_2fast");
    CHECK(ScriptTemplate::SanitizeClassName("O\xCA\xBByin!") == "Oyin");
    CHECK(ScriptTemplate::SanitizeClassName("") == "");
    // The engine name list is generated from the headers, so new engine types are covered too.
    CHECK(!ScriptTemplate::IsValidClassName("ScriptTemplateKind") && !ScriptTemplate::IsValidClassName("EventBus"));
}

// ------------------------------------------------------------------ script component removed / replaced at runtime
static int g_RemovalLive = 0; // RemovalScript instances not deleted yet
static std::vector<std::string> g_Removal;
static bool HasRemoval(const std::string& entry) { return std::find(g_Removal.begin(), g_Removal.end(), entry) != g_Removal.end(); }
static int IndexOfRemoval(const std::string& entry)
{
    auto it = std::find(g_Removal.begin(), g_Removal.end(), entry);
    return it == g_Removal.end() ? -1 : (int)(it - g_Removal.begin());
}

class RemovalScript : public ScriptableEntity
{
public:
    // 1 = removes its own script in OnUpdate, 2 = OnDestroy destroys the parent,
    // 3 = OnStart removes the script of the entity "Victim"
    int Mode = 0;
    int Pings = 0;

    RemovalScript() { g_RemovalLive++; }
    ~RemovalScript() override { g_RemovalLive--; }

    void OnCreate() override { Subscribe("Ping", [this](const ScriptMessage&) { Pings++; }); }
    void OnStart() override
    {
        Log("Start");
        if (Mode == 3)
            if (Entity victim = FindEntity("Victim"))
                victim.RemoveComponent<NativeScriptComponent>();
    }
    void OnUpdate(Timestep) override
    {
        if (Mode == 1 && !m_Removed)
        {
            m_Removed = true;
            GetEntity().RemoveComponent<NativeScriptComponent>();
            // The instance lives until the end of the frame: members are still usable here.
            Pings += 100;
            Log("AfterRemove");
        }
    }
    void OnDisable() override { Log("Disable"); }
    void OnDestroy() override
    {
        Log("Destroy");
        if (Mode == 2)
            if (Entity parent = GetParent())
                GetScene().DestroyEntity(parent);
    }

private:
    void Log(const char* what) { g_Removal.push_back(GetName() + ":" + what); }
    bool m_Removed = false;
};
ZM_REGISTER_SCRIPT(RemovalScript, ZM_FIELD(RemovalScript, Mode))

static Entity AddRemovalScript(Scene& scene, const std::string& name, int mode = 0)
{
    Entity e = scene.CreateEntity(name);
    auto& nsc = e.AddComponent<NativeScriptComponent>();
    nsc.ClassName = "RemovalScript";
    ScriptFieldValue v; v.Set(mode);
    nsc.Fields["Mode"] = v;
    return e;
}

TEST(ScriptComponentRemovedAtRuntime)
{
    g_Removal.clear();
    g_RemovalLive = 0;
    {
        auto scene = CreateRef<Scene>();
        Entity a = AddRemovalScript(*scene, "A");
        Entity b = AddRemovalScript(*scene, "B");
        Entity self = AddRemovalScript(*scene, "Self", 1);
        Entity remover = AddRemovalScript(*scene, "Remover", 3);
        Entity victim = AddRemovalScript(*scene, "Victim");
        scene->OnRuntimeStart();
        CHECK(g_RemovalLive == 5);
        CHECK(scene->GetEventBus().GetSubscriberCount("Ping") == 5);
        CHECK(a.GetComponent<NativeScriptComponent>().Instance->GetScriptClassName() == "RemovalScript");

        // Remover's OnStart removes Victim's script while Victim is still waiting for its
        // own OnStart (which then never comes). Self removes its own script inside OnUpdate
        // and keeps running until OnUpdate returns.
        scene->OnUpdateRuntime(0.016f);
        CHECK(!victim.HasComponent<NativeScriptComponent>());
        CHECK(HasRemoval("Victim:Disable") && HasRemoval("Victim:Destroy") && !HasRemoval("Victim:Start"));
        CHECK(!self.HasComponent<NativeScriptComponent>());
        CHECK(IndexOfRemoval("Self:Disable") >= 0 && IndexOfRemoval("Self:Disable") < IndexOfRemoval("Self:Destroy"));
        CHECK(IndexOfRemoval("Self:Destroy") < IndexOfRemoval("Self:AfterRemove"));
        CHECK(g_RemovalLive == 3); // deleted at the end of the frame
        CHECK(scene->GetEventBus().GetSubscriberCount("Ping") == 3);

        // Entity::RemoveComponent (the inspector's "remove component", script code):
        // OnDisable + OnDestroy, and the subscriptions end with the instance.
        g_Removal.clear();
        a.RemoveComponent<NativeScriptComponent>();
        CHECK((g_Removal == std::vector<std::string>{ "A:Disable", "A:Destroy" }));
        CHECK(!a.HasComponent<NativeScriptComponent>());
        CHECK(scene->GetEventBus().Emit("Ping") == 2); // B and Remover

        // AddOrReplaceComponent: the old instance ends, the new component has none yet.
        g_Removal.clear();
        NativeScriptComponent copy = b.GetComponent<NativeScriptComponent>();
        CHECK(copy.Instance == nullptr && copy.ClassName == "RemovalScript");
        b.AddOrReplaceComponent<NativeScriptComponent>(copy);
        CHECK((g_Removal == std::vector<std::string>{ "B:Disable", "B:Destroy" }));
        CHECK(b.GetComponent<NativeScriptComponent>().Instance == nullptr);
        CHECK(scene->GetEventBus().Emit("Ping") == 1);

        // Assigning data onto a live component keeps its instance (no leak, no orphan) ...
        auto& rc = remover.GetComponent<NativeScriptComponent>();
        ScriptableEntity* live = rc.Instance;
        CHECK(live != nullptr);
        copy.ClassName = "TestScript";
        rc = copy;
        CHECK(rc.Instance == live && rc.ClassName == "TestScript");
        CHECK(live->GetScriptClassName() == "RemovalScript"); // the inspector edits this class's fields
        scene->GetRegistry().replace<NativeScriptComponent>(remover, copy);
        CHECK(remover.GetComponent<NativeScriptComponent>().Instance == live);

        // ... and removing it behind the scene's back (raw registry) detaches it without
        // callbacks: no more events reach it and it is still deleted.
        g_Removal.clear();
        scene->GetRegistry().remove<NativeScriptComponent>(remover);
        CHECK(g_Removal.empty());
        CHECK(scene->GetEventBus().GetSubscriberCount("Ping") == 0);
        CHECK(scene->GetEventBus().Emit("Ping") == 0);
        CHECK(g_RemovalLive == 3);
        scene->OnUpdateRuntime(0.016f);
        CHECK(g_RemovalLive == 0);
        scene->OnRuntimeStop();
    }
    CHECK(g_RemovalLive == 0);

    // Removing a script in edit mode (no instance) is a plain removal.
    auto edit = CreateRef<Scene>();
    Entity e = AddRemovalScript(*edit, "Edit");
    e.RemoveComponent<NativeScriptComponent>();
    CHECK(!e.HasComponent<NativeScriptComponent>() && g_RemovalLive == 0);
}

TEST(DestroyEntityReentrant)
{
    g_Removal.clear();
    g_RemovalLive = 0;
    {
        // The children are created before their parent, so the parent's components are the
        // last ones in their pools and move when the children are removed.
        auto scene = CreateRef<Scene>();
        Entity child = AddRemovalScript(*scene, "Child", 2); // OnDestroy destroys its parent
        Entity sibling = AddRemovalScript(*scene, "Sibling");
        Entity parent = scene->CreateEntity("Parent");
        Entity root = scene->CreateEntity("Root");
        CHECK(scene->SetParent(child, parent, false));
        CHECK(scene->SetParent(sibling, parent, false));
        CHECK(scene->SetParent(parent, root, false));
        // Removing the sibling moves `filler` into its slot, removing the parent moves `other`
        // into the parent's: with a parent of its own, a stale pointer to that slot looks
        // like a live relationship.
        Entity other = AddRemovalScript(*scene, "Other");
        Entity filler = scene->CreateEntity("Filler");
        CHECK(scene->SetParent(other, root, false));
        CHECK(scene->SetParent(filler, root, false));
        scene->OnRuntimeStart();
        scene->OnUpdateRuntime(0.016f);

        // Destroying the parent: the first child's OnDestroy destroys the parent again
        // (from inside the outer DestroyEntity's children loop).
        const UUID parentID = parent.GetUUID();
        scene->DestroyEntity(parent);
        CHECK(!parent.IsValid() && !child.IsValid() && !sibling.IsValid());
        const std::vector<UUID> rest{ other.GetUUID(), filler.GetUUID() };
        CHECK(root.IsValid() && root.GetComponent<RelationshipComponent>().Children == rest);
        CHECK(HasRemoval("Child:Destroy") && HasRemoval("Sibling:Destroy"));
        CHECK(!scene->GetEntityByUUID(parentID));
        CHECK(other.IsValid() && !scene->FindEntityByName("Parent"));

        // Destroying the child directly: its OnDestroy destroys the parent and with it the
        // child itself, while the child's own DestroyEntity is still running.
        g_Removal.clear();
        Entity p2 = scene->CreateEntity("P2");
        Entity c2 = AddRemovalScript(*scene, "C2", 2);
        Entity s2 = AddRemovalScript(*scene, "S2");
        CHECK(scene->SetParent(c2, p2, false));
        CHECK(scene->SetParent(s2, p2, false));
        CHECK(scene->SetParent(p2, root, false));
        std::vector<Entity> spawned{ p2, c2, s2 };
        scene->OnEntitiesSpawned(spawned);
        scene->OnUpdateRuntime(0.016f);
        CHECK(HasRemoval("C2:Start") && HasRemoval("S2:Start"));
        scene->DestroyEntity(c2);
        CHECK(!c2.IsValid() && !p2.IsValid() && !s2.IsValid());
        CHECK(root.GetComponent<RelationshipComponent>().Children == rest);
        CHECK(HasRemoval("C2:Destroy") && HasRemoval("S2:Destroy"));

        scene->OnUpdateRuntime(0.016f);
        CHECK(g_RemovalLive == 1); // only Other is left
        scene->OnRuntimeStop();
        CHECK(g_RemovalLive == 0);
    }
    CHECK(g_RemovalLive == 0);
}

// ------------------------------------------------------------------ M02: editor UX (hierarchy order, multi-selection)
static std::string Names(const std::vector<Entity>& entities)
{
    std::string out;
    for (Entity e : entities)
        out += (out.empty() ? "" : ",") + (e ? e.GetName() : std::string("?"));
    return out;
}

static std::string ChildNames(Scene& scene, Entity parent) { return Names(scene.GetChildren(parent)); }

TEST(HierarchySiblingOrder)
{
    auto scene = CreateRef<Scene>();
    Entity a = scene->CreateEntity("A");
    Entity b = scene->CreateEntity("B");
    Entity c = scene->CreateEntity("C");
    CHECK(Names(scene->GetRootEntities()) == "A,B,C");
    CHECK(scene->GetSiblingIndex(a) == 0 && scene->GetSiblingIndex(c) == 2);

    // Root order (the scene order) can be changed.
    CHECK(scene->SetSiblingIndex(c, 0));
    CHECK(Names(scene->GetRootEntities()) == "C,A,B");
    CHECK(Names(scene->GetEntitiesInOrder()) == "C,A,B");
    CHECK(scene->SetSiblingIndex(c, -1)); // last
    CHECK(Names(scene->GetRootEntities()) == "A,B,C");
    CHECK(scene->SetSiblingIndex(a, 99)); // clamped
    CHECK(Names(scene->GetRootEntities()) == "B,C,A");
    CHECK(scene->MoveEntityNextTo(a, b, false)); // before B
    CHECK(Names(scene->GetRootEntities()) == "A,B,C");
    CHECK(scene->MoveEntityNextTo(a, b, true));  // after B
    CHECK(Names(scene->GetRootEntities()) == "B,A,C");
    CHECK(scene->FindEntityByName("A") == a); // the name cache follows

    // Children: the parent's list.
    Entity p = scene->CreateEntity("P");
    Entity x = scene->CreateEntity("X");
    Entity y = scene->CreateEntity("Y");
    Entity z = scene->CreateEntity("Z");
    for (Entity e : { x, y, z })
        CHECK(scene->SetParent(e, p, false));
    CHECK(ChildNames(*scene, p) == "X,Y,Z");
    CHECK(scene->SetSiblingIndex(z, 0));
    CHECK(ChildNames(*scene, p) == "Z,X,Y");
    CHECK(scene->GetSiblingIndex(y) == 2);
    CHECK(Names(scene->GetEntitiesInHierarchyOrder()) == "B,A,C,P,Z,X,Y");

    // Into another parent next to a sibling, keeping the world position.
    p.GetTransform().Translation = { 10.0f, 0.0f, 0.0f };
    c.GetTransform().Translation = { 1.0f, 2.0f, 3.0f };
    CHECK(scene->MoveEntityNextTo(c, x, true));
    CHECK(scene->GetParent(c) == p);
    CHECK(ChildNames(*scene, p) == "Z,X,C,Y");
    CHECK(Names(scene->GetRootEntities()) == "B,A,P");
    CHECK_NEAR(scene->GetWorldPosition(c).x, 1.0f, 1e-4f);
    CHECK_NEAR(c.GetTransform().Translation.x, -9.0f, 1e-4f);
    // ... and back to the root list, before A.
    CHECK(scene->MoveEntityNextTo(c, a, false));
    CHECK(!scene->GetParent(c) && ChildNames(*scene, p) == "Z,X,Y");
    CHECK(Names(scene->GetRootEntities()) == "B,C,A,P");
    CHECK_NEAR(c.GetTransform().Translation.x, 1.0f, 1e-4f);

    // A parent can not go next to its own descendant (cycle); nothing changes.
    CHECK(!scene->MoveEntityNextTo(p, x, false));
    CHECK(!scene->GetParent(p) && ChildNames(*scene, p) == "Z,X,Y");
    CHECK(!scene->MoveEntityNextTo(a, a, true));
    CHECK(scene->GetSiblingIndex(Entity()) == -1);
}

TEST(SceneOrderStableOnDestroy)
{
    auto scene = CreateRef<Scene>();
    Entity a = scene->CreateEntity("A");
    Entity b = scene->CreateEntity("B");
    scene->CreateEntity("C");
    scene->CreateEntity("D");
    Entity e = scene->CreateEntity("E");
    scene->DestroyEntity(b); // swap-and-pop would have moved E into B's place
    CHECK(Names(scene->GetEntitiesInOrder()) == "A,C,D,E");
    scene->DestroyEntity(a);
    CHECK(Names(scene->GetEntitiesInOrder()) == "C,D,E");
    scene->DestroyEntity(e);
    CHECK(Names(scene->GetEntitiesInOrder()) == "C,D");
    scene->CreateEntity("F");
    CHECK(Names(scene->GetEntitiesInOrder()) == "C,D,F");
    CHECK(scene->FindEntityByName("F") && !scene->FindEntityByName("A"));
}

TEST(HierarchyOrderSerialization)
{
    auto scene = CreateRef<Scene>();
    Entity p = scene->CreateEntity("P");
    Entity x = scene->CreateEntity("X");
    Entity y = scene->CreateEntity("Y");
    Entity z = scene->CreateEntity("Z");
    Entity r = scene->CreateEntity("R");
    for (Entity e : { x, y, z })
        scene->SetParent(e, p, false);

    // Children in file order need no extra data (unchanged scene files stay unchanged).
    std::string plain = SceneSerializer(scene).SerializeToString();
    CHECK(plain.find("ChildOrder") == std::string::npos);

    scene->SetSiblingIndex(z, 0);  // P: Z,X,Y
    scene->SetSiblingIndex(r, 0);  // roots: R,P
    std::string text = SceneSerializer(scene).SerializeToString();
    CHECK(text.find("ChildOrder") != std::string::npos);

    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    Entity lp = loaded->FindEntityByName("P");
    CHECK(ChildNames(*loaded, lp) == "Z,X,Y");
    CHECK(Names(loaded->GetRootEntities()) == "R,P");
    CHECK(SceneSerializer(loaded).SerializeToString() == text); // stable

    // Copy / paste of the subtree keeps the order with fresh IDs.
    std::vector<Entity> subtree{ p };
    for (Entity c : scene->GetDescendants(p)) subtree.push_back(c);
    std::string clip = SceneSerializer::SerializeEntities(*scene, std::vector<Entity>{ p, x, y, z });
    auto other = CreateRef<Scene>();
    auto pasted = SceneSerializer::DeserializeEntities(*other, clip, true);
    CHECK(pasted.size() == 4 && pasted.front().GetName() == "P");
    CHECK(pasted.size() == 4 && ChildNames(*other, pasted.front()) == "Z,X,Y");
    CHECK(pasted.size() == 4 && pasted.front().GetUUID() != p.GetUUID());
    // A prefab-style depth-first subset is already in order.
    std::string prefab = SceneSerializer::SerializeEntities(*scene, subtree);
    CHECK(prefab.find("ChildOrder") == std::string::npos);
    auto third = CreateRef<Scene>();
    auto inst = SceneSerializer::DeserializeEntities(*third, prefab, true);
    CHECK(!inst.empty() && ChildNames(*third, inst.front()) == "Z,X,Y");

    // Runtime copies (play mode) keep the order too.
    auto runtime = Scene::Copy(scene);
    CHECK(ChildNames(*runtime, runtime->FindEntityByName("P")) == "Z,X,Y");
    CHECK(Names(runtime->GetRootEntities()) == "R,P");
}

TEST(MultiEntityDuplicateDestroy)
{
    auto scene = CreateRef<Scene>();
    Entity a = scene->CreateEntity("A");
    Entity a1 = scene->CreateEntity("A1");
    scene->SetParent(a1, a, false);
    Entity b = scene->CreateEntity("B");
    Entity c = scene->CreateEntity("C");
    Entity d = scene->CreateEntity("D");
    // A refers to B, B to its own child-free self, C to D (not duplicated).
    a.AddComponent<NativeScriptComponent>().ClassName = "RefScript";
    SetRefField(a, "Target", b.GetUUID());
    SetRefField(a, "Other", a1.GetUUID());
    b.AddComponent<NativeScriptComponent>().ClassName = "RefScript";
    SetRefField(b, "Target", d.GetUUID());

    // Nested and repeated entries collapse to the top-level ones, in hierarchy order.
    CHECK(Names(scene->GetTopLevelEntities({ c, a1, b, a, b, Entity() })) == "A,B,C");
    CHECK(scene->GetTopLevelEntities({}).empty());

    auto copies = scene->DuplicateEntities({ b, a1, a });
    CHECK(Names(copies) == "A,B");
    CHECK(Names(scene->GetRootEntities()) == "A,A,B,B,C,D"); // each copy right after its original
    CHECK(copies.size() == 2 && copies[0] != a && copies[1] != b);
    if (copies.size() == 2)
    {
        Entity ca = copies[0], cb = copies[1];
        CHECK(scene->GetChildCount(ca) == 1);
        Entity ca1 = scene->GetChild(ca, 0);
        CHECK(ca1 && ca1 != a1 && ca1.GetName() == "A1");
        // References inside the duplicated set follow the copies; outside ones stay.
        CHECK(GetRefField(ca, "Target") == (uint64_t)cb.GetUUID());
        CHECK(GetRefField(ca, "Other") == (uint64_t)ca1.GetUUID());
        CHECK(GetRefField(cb, "Target") == (uint64_t)d.GetUUID());
        CHECK(GetRefField(a, "Target") == (uint64_t)b.GetUUID()); // originals untouched

        // Destroying a mixed selection: nested / repeated entries are fine.
        CHECK(scene->DestroyEntities({ ca1, ca, cb, cb }) == 2);
        CHECK(!ca.IsValid() && !ca1.IsValid() && !cb.IsValid());
    }
    CHECK(Names(scene->GetRootEntities()) == "A,B,C,D");
    CHECK(scene->DestroyEntities({ a, c }) == 2);
    CHECK(Names(scene->GetEntitiesInOrder()) == "B,D");
    CHECK(!a1.IsValid());

    // A child duplicated alone stays under the same parent, after the original.
    Entity p = scene->CreateEntity("P");
    Entity x = scene->CreateEntity("X");
    Entity y = scene->CreateEntity("Y");
    scene->SetParent(x, p, false);
    scene->SetParent(y, p, false);
    auto childCopy = scene->DuplicateEntities({ x });
    CHECK(childCopy.size() == 1 && scene->GetParent(childCopy[0]) == p);
    CHECK(ChildNames(*scene, p) == "X,X,Y");
}

TEST(EntitySelectionModel)
{
    auto scene = CreateRef<Scene>();
    Entity a = scene->CreateEntity("A");
    Entity b = scene->CreateEntity("B");
    Entity c = scene->CreateEntity("C");
    Entity d = scene->CreateEntity("D");
    Entity e = scene->CreateEntity("E");
    std::vector<Entity> rows{ a, b, c, d, e };

    EntitySelection sel;
    CHECK(sel.IsEmpty() && !sel.GetPrimary());
    sel.Select(b);
    CHECK(sel.Size() == 1 && sel.GetPrimary() == b && sel.Contains(b) && !sel.Contains(a));
    sel.Toggle(d); // Ctrl+click
    CHECK(sel.Size() == 2 && sel.GetPrimary() == d && sel.Contains(d));
    sel.Toggle(b);
    CHECK(sel.Size() == 1 && !sel.Contains(b) && sel.GetPrimary() == d);
    CHECK(sel.GetAnchor() == b); // ranges continue from the last Ctrl-clicked row

    // Shift+click: range from the anchor, replacing the selection.
    sel.Select(b);
    sel.SelectRange(rows, d);
    CHECK(Names(sel.GetEntities()) == "B,C,D" && sel.GetPrimary() == d && sel.GetAnchor() == b);
    sel.SelectRange(rows, a); // the anchor stays: B..A
    CHECK(sel.Size() == 2 && sel.Contains(a) && sel.Contains(b) && !sel.Contains(d));
    // Ctrl+Shift: added to the selection.
    sel.Select(e);
    sel.SelectRange(rows, d, true);
    CHECK(sel.Size() == 2);
    sel.Toggle(a);
    sel.SelectRange(rows, b, true);
    CHECK(Names(sel.GetEntities()) == "E,D,A,B" && sel.GetPrimary() == b);
    // Anchor not listed (collapsed / filtered out): just the clicked row.
    sel.Select(c);
    sel.SelectRange({ a, b }, b);
    CHECK(sel.Size() == 1 && sel.GetPrimary() == b);

    sel.Set({ a, c, e }, c);
    CHECK(sel.Size() == 3 && sel.GetPrimary() == c);
    sel.Set({ a, c, e });
    CHECK(sel.GetPrimary() == e);
    sel.Add(b, false);
    CHECK(sel.GetPrimary() == e && sel.Contains(b));

    // Destroyed entities are dropped; the primary falls back to the latest one left.
    scene->DestroyEntity(e);
    sel.Prune();
    CHECK(Names(sel.GetEntities()) == "A,C,B" && sel.GetPrimary() == b);
    sel.Remove(b);
    CHECK(sel.GetPrimary() == c);

    // Same entities in a copy of the scene (play mode / undo snapshot).
    auto copy = Scene::Copy(scene);
    uint64_t revision = sel.GetRevision();
    sel.Prune();
    sel.Remap(copy.get());
    CHECK(sel.GetRevision() == revision); // not a new pick: the inspector stays where it is
    CHECK(sel.Size() == 2 && sel.GetPrimary() && sel.GetPrimary().GetScene() == copy.get());
    CHECK(sel.GetPrimary().GetUUID() == c.GetUUID());
    CHECK(!sel.Contains(a) && sel.Contains(copy->GetEntityByUUID(a.GetUUID())));
    sel.Remap(nullptr);
    CHECK(sel.IsEmpty() && sel.GetRevision() == revision);
    sel.Select(Entity());
    CHECK(sel.IsEmpty() && sel.GetRevision() > revision); // a pick, even of nothing
    revision = sel.GetRevision();
    sel.Clear();
    CHECK(sel.GetRevision() > revision);
}

TEST(GroupTransformEditing)
{
    auto scene = CreateRef<Scene>();
    Entity a = scene->CreateEntity("A");
    Entity b = scene->CreateEntity("B");
    Entity parent = scene->CreateEntity("Parent");
    Entity child = scene->CreateEntity("Child");
    a.GetTransform().Translation = { -2.0f, 0.0f, 0.0f };
    b.GetTransform().Translation = { 2.0f, 0.0f, 0.0f };
    b.GetTransform().Scale = { 1.0f, 2.0f, 1.0f };
    parent.GetTransform().Translation = { 0.0f, 0.0f, 5.0f };
    parent.GetTransform().Scale = glm::vec3(2.0f);
    scene->SetParent(child, parent, false);
    child.GetTransform().Translation = { 1.0f, 0.0f, 0.0f }; // world (2, 0, 5)
    std::vector<Entity> group{ a, b, child };

    glm::mat4 center = GroupTransformEdit::ComputePivot(*scene, group, a, true, false);
    CHECK_NEAR(center[3].x, 2.0f / 3.0f, 1e-4f); // (-2 + 2 + 2) / 3
    CHECK_NEAR(center[3].z, 5.0f / 3.0f, 1e-4f);
    CHECK_NEAR(center[0].x, 1.0f, 1e-5f); // world axes, unit scale
    glm::mat4 pivotOnA = GroupTransformEdit::ComputePivot(*scene, group, a, false, false);
    CHECK_NEAR(pivotOnA[3].x, -2.0f, 1e-4f);

    // Translate: everything moves by the same world offset (the child in its parent's space).
    GroupTransformEdit edit;
    edit.Begin(*scene, group, center);
    CHECK(edit.IsActive() && edit.GetCount() == 3);
    edit.Update(glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 1.0f, 0.0f)) * center, GroupTransformEdit::Operation::Translate);
    edit.Update(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 0.0f)) * center, GroupTransformEdit::Operation::Translate);
    CHECK_NEAR(a.GetTransform().Translation.x, -1.0f, 1e-4f);
    CHECK_NEAR(a.GetTransform().Translation.y, 2.0f, 1e-4f);
    CHECK_NEAR(b.GetTransform().Translation.x, 3.0f, 1e-4f);
    CHECK_NEAR(scene->GetWorldPosition(child).x, 3.0f, 1e-4f);
    CHECK_NEAR(scene->GetWorldPosition(child).y, 2.0f, 1e-4f);
    CHECK_NEAR(child.GetTransform().Translation.x, 1.5f, 1e-4f); // parent scale 2
    CHECK_NEAR(b.GetTransform().Scale.y, 2.0f, 1e-5f);
    // Cancel restores the start.
    edit.Cancel();
    CHECK(!edit.IsActive());
    CHECK_NEAR(a.GetTransform().Translation.x, -2.0f, 1e-5f);
    CHECK_NEAR(child.GetTransform().Translation.x, 1.0f, 1e-5f);

    // Rotate 90 degrees around Y about the center of A and B.
    std::vector<Entity> pair{ a, b };
    glm::mat4 mid = GroupTransformEdit::ComputePivot(*scene, pair, a, true, false);
    edit.Begin(*scene, pair, mid);
    glm::mat4 rotated = mid * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
    edit.Update(rotated, GroupTransformEdit::Operation::Rotate);
    edit.End();
    CHECK_NEAR(a.GetTransform().Translation.x, 0.0f, 1e-4f);
    CHECK_NEAR(a.GetTransform().Translation.z, 2.0f, 1e-4f);  // (-2,0,0) -> (0,0,2)
    CHECK_NEAR(b.GetTransform().Translation.z, -2.0f, 1e-4f);
    CHECK_NEAR(a.GetTransform().Rotation.y, glm::radians(90.0f), 1e-4f);
    CHECK_NEAR(b.GetTransform().Scale.y, 2.0f, 1e-4f);

    // Scale x2 on X about the center: positions spread, local scales double on that axis.
    a.GetTransform() = TransformComponent(glm::vec3(-2.0f, 0.0f, 0.0f));
    b.GetTransform() = TransformComponent(glm::vec3(2.0f, 0.0f, 0.0f));
    b.GetTransform().Scale = { 1.0f, 2.0f, 1.0f };
    mid = GroupTransformEdit::ComputePivot(*scene, pair, a, true, false);
    edit.Begin(*scene, pair, mid);
    edit.Update(mid * glm::scale(glm::mat4(1.0f), glm::vec3(1.5f, 1.0f, 1.0f)), GroupTransformEdit::Operation::Scale);
    edit.Update(mid * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 1.0f)), GroupTransformEdit::Operation::Scale);
    edit.End();
    CHECK_NEAR(a.GetTransform().Translation.x, -4.0f, 1e-4f);
    CHECK_NEAR(b.GetTransform().Translation.x, 4.0f, 1e-4f);
    CHECK_NEAR(a.GetTransform().Scale.x, 2.0f, 1e-4f);
    CHECK_NEAR(b.GetTransform().Scale.x, 2.0f, 1e-4f);
    CHECK_NEAR(b.GetTransform().Scale.y, 2.0f, 1e-4f); // other axes keep their own scale

    // Single entity with the pivot on itself behaves like a plain gizmo.
    std::vector<Entity> single{ b };
    glm::mat4 own = GroupTransformEdit::ComputePivot(*scene, single, b, false, false);
    edit.Begin(*scene, single, own);
    edit.Update(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f)) * own, GroupTransformEdit::Operation::Translate);
    edit.End();
    CHECK_NEAR(b.GetTransform().Translation.z, -1.0f, 1e-4f);
    CHECK_NEAR(b.GetTransform().Scale.x, 2.0f, 1e-5f);
    // Entities destroyed during an edit are skipped.
    edit.Begin(*scene, pair, mid);
    scene->DestroyEntity(a);
    edit.Update(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * mid, GroupTransformEdit::Operation::Translate);
    edit.End();
    CHECK_NEAR(b.GetTransform().Translation.x, 5.0f, 1e-4f);
}

TEST(TransformClipboardText)
{
    TransformComponent t;
    t.Translation = { 1.5f, -2.0f, 3.25f };
    t.Rotation = { 0.1f, 0.2f, -0.3f };
    t.Scale = { 2.0f, 1.0f, 0.5f };
    std::string text = SceneSerializer::SerializeTransform(t);
    TransformComponent out;
    CHECK(SceneSerializer::DeserializeTransform(text, out));
    CHECK_NEAR(out.Translation.z, 3.25f, 1e-6f);
    CHECK_NEAR(out.Rotation.z, -0.3f, 1e-6f);
    CHECK_NEAR(out.Scale.x, 2.0f, 1e-6f);
    // Anything else on the clipboard is not a transform and leaves the target alone.
    TransformComponent keep(glm::vec3(7.0f));
    CHECK(!SceneSerializer::DeserializeTransform("", keep));
    CHECK(!SceneSerializer::DeserializeTransform("salom", keep));
    CHECK(!SceneSerializer::DeserializeTransform("{\"ZaminEntities\":1,\"Entities\":[]}", keep));
    CHECK(!SceneSerializer::DeserializeTransform("{\"ZaminTransform\": [", keep));
    CHECK_NEAR(keep.Translation.x, 7.0f, 1e-6f);
    // Missing parts fall back to the defaults.
    CHECK(SceneSerializer::DeserializeTransform("{\"ZaminTransform\":1,\"Translation\":[4,5,6]}", keep));
    CHECK_NEAR(keep.Translation.y, 5.0f, 1e-6f);
    CHECK_NEAR(keep.Scale.y, 1.0f, 1e-6f);
}

// ------------------------------------------------------------------ M02: viewport (snap, grid, camera)
TEST(EditorSnapSettings)
{
    SnapSettings snap;
    CHECK(!snap.Enabled);
    CHECK(!snap.IsActive(false));
    CHECK(snap.IsActive(true));          // Ctrl turns snapping on...
    snap.Enabled = true;
    CHECK(snap.IsActive(false));
    CHECK(!snap.IsActive(true));         // ...or off while the toggle is on
    snap.Translate = 0.25f;
    snap.Rotate = 45.0f;
    snap.Scale = 0.5f;
    CHECK_NEAR(snap.GetStep(0), 0.25f, 1e-6f);
    CHECK_NEAR(snap.GetStep(1), 45.0f, 1e-6f);
    CHECK_NEAR(snap.GetStep(2), 0.5f, 1e-6f);
    CHECK_NEAR(snap.GetStep(-1), 0.25f, 1e-6f); // "no gizmo" shows the translate step

    snap.Translate = 0.0f;
    snap.Rotate = 720.0f;
    snap.Scale = std::numeric_limits<float>::quiet_NaN();
    snap.Sanitize();
    CHECK_NEAR(snap.Translate, SnapSettings::MinStep, 1e-7f);
    CHECK_NEAR(snap.Rotate, 360.0f, 1e-6f);
    CHECK_NEAR(snap.Scale, 0.1f, 1e-6f);

    GridSettings grid;
    grid.CellSize = -2.0f;
    grid.MajorEvery = 0;
    grid.FadeDistance = 50.0f;
    grid.Color = glm::vec4(2.0f, -1.0f, 0.5f, 1.5f);
    grid.Sanitize();
    CHECK_NEAR(grid.CellSize, GridSettings::MinCellSize, 1e-7f);
    CHECK(grid.MajorEvery == 1);
    CHECK_NEAR(grid.FadeDistance, 10.0f, 1e-6f);
    CHECK_NEAR(grid.Color.r, 1.0f, 1e-6f);
    CHECK_NEAR(grid.Color.g, 0.0f, 1e-6f);
    CHECK_NEAR(grid.Color.a, 1.0f, 1e-6f);
}

TEST(EditorViewSettingsJson)
{
    CameraBookmarks bm;
    CHECK(bm.IsEmpty());
    EditorCameraState a;
    a.FocalPoint = { 1.0f, 2.0f, 3.0f };
    a.Distance = 7.5f;
    a.Pitch = -0.4f;
    a.Yaw = 2.0f;
    a.Orthographic = true;
    bm.Set(2, a);
    bm.Set(0, a);  // out of range: ignored
    bm.Set(6, a);
    CHECK(bm.Count() == 1);
    CHECK(bm.Has(2) && !bm.Has(1) && !bm.Has(0) && !bm.Has(6));
    CHECK(bm.Get(2) && bm.Get(2)->Orthographic);

    EditorViewSettings settings;
    settings.Snap.Enabled = true;
    settings.Snap.Translate = 0.25f;
    settings.Snap.Rotate = 45.0f;
    settings.Grid.Visible = false;
    settings.Grid.CellSize = 0.5f;
    settings.Grid.MajorEvery = 4;
    settings.Grid.Color = { 0.1f, 0.2f, 0.3f, 0.4f };
    settings.Grid.ShowAxes = false;
    std::string sceneKey = EditorViewSettings::SceneKey("/tmp/zamin-test/Scenes/../Scenes/Main.zscene");
    CHECK(sceneKey == "/tmp/zamin-test/Scenes/Main.zscene");
    CHECK(EditorViewSettings::SceneKey("").empty());
    settings.Bookmarks[sceneKey] = bm;
    settings.GetBookmarks("").Set(1, a);              // unsaved scene: not written
    settings.GetBookmarks("/tmp/zamin-test/Empty.zscene"); // empty set: not written

    std::string text = settings.ToJson();
    EditorViewSettings loaded;
    CHECK(loaded.FromJson(text));
    CHECK(loaded.Snap.Enabled);
    CHECK_NEAR(loaded.Snap.Translate, 0.25f, 1e-6f);
    CHECK_NEAR(loaded.Snap.Rotate, 45.0f, 1e-6f);
    CHECK_NEAR(loaded.Snap.Scale, 0.1f, 1e-6f);
    CHECK(!loaded.Grid.Visible);
    CHECK_NEAR(loaded.Grid.CellSize, 0.5f, 1e-6f);
    CHECK(loaded.Grid.MajorEvery == 4);
    CHECK_NEAR(loaded.Grid.Color.b, 0.3f, 1e-6f);
    CHECK_NEAR(loaded.Grid.Color.a, 0.4f, 1e-6f);
    CHECK(!loaded.Grid.ShowAxes);
    CHECK(loaded.Bookmarks.size() == 1);
    CHECK(loaded.Bookmarks.count("") == 0);
    const EditorCameraState* back = loaded.GetBookmarks("/tmp/zamin-test/Scenes/Main.zscene").Get(2);
    CHECK(back != nullptr);
    if (back)
    {
        CHECK_NEAR(back->FocalPoint.y, 2.0f, 1e-6f);
        CHECK_NEAR(back->Distance, 7.5f, 1e-6f);
        CHECK_NEAR(back->Pitch, -0.4f, 1e-6f);
        CHECK_NEAR(back->Yaw, 2.0f, 1e-6f);
        CHECK(back->Orthographic);
    }

    // Not settings: nothing changes.
    EditorViewSettings keep;
    keep.Snap.Translate = 3.0f;
    CHECK(!keep.FromJson(""));
    CHECK(!keep.FromJson("{ broken"));
    CHECK(!keep.FromJson("{\"ZaminEntities\":1}"));
    CHECK_NEAR(keep.Snap.Translate, 3.0f, 1e-6f);
    // Missing / wrongly typed fields keep their values; bad numbers are sanitized.
    CHECK(keep.FromJson("{\"ZaminEditorView\":1,\"Snap\":{\"Rotate\":\"x\",\"Scale\":-1},\"Grid\":{\"CellSize\":0,\"Color\":[1,2]},"
                        "\"CameraBookmarks\":{\"/a.zscene\":{\"9\":{},\"3\":{\"Distance\":-4}}}}"));
    CHECK_NEAR(keep.Snap.Translate, 3.0f, 1e-6f);
    CHECK_NEAR(keep.Snap.Rotate, 15.0f, 1e-6f);
    CHECK_NEAR(keep.Snap.Scale, SnapSettings::MinStep, 1e-7f);
    CHECK_NEAR(keep.Grid.CellSize, GridSettings::MinCellSize, 1e-7f);
    CHECK_NEAR(keep.Grid.Color.a, GridSettings().Color.a, 1e-6f);
    CHECK(keep.Bookmarks["/a.zscene"].Count() == 1);
    CHECK(keep.Bookmarks["/a.zscene"].Get(3) && keep.Bookmarks["/a.zscene"].Get(3)->Distance > 0.0f);
}

TEST(EditorCameraBookmarkTransition)
{
    EditorCamera camera(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.SetViewportSize(1600, 900);
    camera.Focus({ 0.0f, 0.0f, 0.0f }, 10.0f);
    camera.SetOrientation(0.3f, 3.0f);

    EditorCameraState saved = camera.GetViewState();
    CHECK_NEAR(saved.Distance, 10.0f, 1e-6f);
    CHECK_NEAR(saved.Yaw, 3.0f, 1e-6f);
    CHECK(!saved.Orthographic);

    // At once: exactly the stored view.
    EditorCameraState target;
    target.FocalPoint = { 10.0f, 2.0f, -4.0f };
    target.Distance = 40.0f;
    target.Pitch = 0.5f;
    target.Yaw = -3.0f; // 6 - 2*pi = -0.28 rad away from 3.0 the short way
    camera.SetViewState(target);
    CHECK_NEAR(camera.GetFocalPoint().x, 10.0f, 1e-5f);
    CHECK_NEAR(camera.GetDistance(), 40.0f, 1e-4f);
    glm::vec3 expected = target.FocalPoint - camera.GetForwardDirection() * 40.0f;
    CHECK_NEAR(glm::length(camera.GetPosition() - expected), 0.0f, 1e-3f);

    // Smoothly: halfway the camera is between both views and turns the short way around.
    camera.SetViewState(saved);
    camera.TransitionTo(target, 1.0f);
    CHECK(camera.IsTransitioning());
    camera.OnUpdate(0.5f, false);
    CHECK(camera.IsTransitioning());
    CHECK_NEAR(camera.GetFocalPoint().x, 5.0f, 1e-3f);
    CHECK_NEAR(camera.GetDistance(), 20.0f, 1e-2f); // zoom is even in log space: sqrt(10 * 40)
    float shortWay = 3.0f + std::remainder(-3.0f - 3.0f, glm::two_pi<float>()) * 0.5f;
    CHECK_NEAR(camera.GetYaw(), shortWay, 1e-3f);
    CHECK(camera.GetYaw() > 3.0f); // not back through 0
    camera.OnUpdate(0.6f, false);
    CHECK(!camera.IsTransitioning());
    CHECK_NEAR(camera.GetYaw(), -3.0f, 1e-6f); // ends exactly on the bookmark
    CHECK_NEAR(camera.GetDistance(), 40.0f, 1e-4f);

    // The projection switches at once; scrolling stops a transition where it is.
    EditorCameraState flat = target;
    flat.Orthographic = true;
    camera.TransitionTo(flat, 1.0f);
    CHECK(camera.IsOrthographic());
    MouseScrolledEvent scroll(0.0f, 1.0f);
    camera.OnEvent(scroll);
    CHECK(!camera.IsTransitioning());
    camera.TransitionTo(saved, 0.0f);
    CHECK(!camera.IsTransitioning());
    CHECK(!camera.IsOrthographic());
    CHECK_NEAR(camera.GetDistance(), 10.0f, 1e-4f);
}

// Every corner of `box` inside the camera's view (normalized device coordinates); returns the
// largest |x| / |y| a corner reaches.
static float MaxCornerNdc(const EditorCamera& camera, const AABB& box, bool& inside)
{
    inside = true;
    float extent = 0.0f;
    glm::mat4 vp = camera.GetViewProjection();
    for (int i = 0; i < 8; i++)
    {
        glm::vec3 c((i & 1) ? box.Max.x : box.Min.x, (i & 2) ? box.Max.y : box.Min.y, (i & 4) ? box.Max.z : box.Min.z);
        glm::vec4 clip = vp * glm::vec4(c, 1.0f);
        if (clip.w <= 0.0f) { inside = false; continue; }
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f) inside = false;
        extent = std::max({ extent, std::abs(ndc.x), std::abs(ndc.y) });
    }
    return extent;
}

TEST(EditorCameraFrameBounds)
{
    EditorCamera camera(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.SetViewportSize(1600, 900);
    camera.SetOrientation(0.35f, -0.6f);
    camera.Focus({ 50.0f, 0.0f, 0.0f }, 3.0f);

    AABB box{ { -4.0f, 0.0f, -2.0f }, { 6.0f, 3.0f, 8.0f } };
    EditorCameraState framed = camera.ComputeFrameState(box);
    CHECK_NEAR(framed.Pitch, 0.35f, 1e-6f); // the view direction does not change
    CHECK_NEAR(framed.Yaw, -0.6f, 1e-6f);
    CHECK_NEAR(framed.FocalPoint.x, 1.0f, 1e-5f);
    CHECK_NEAR(framed.FocalPoint.y, 1.5f, 1e-5f);
    CHECK_NEAR(framed.FocalPoint.z, 3.0f, 1e-5f);
    camera.SetViewState(framed);
    bool inside = false;
    float extent = MaxCornerNdc(camera, box, inside);
    CHECK(inside);
    CHECK(extent > 0.8f); // and tight: a corner reaches the margin
    glm::vec4 center = camera.GetViewProjection() * glm::vec4(framed.FocalPoint, 1.0f);
    CHECK_NEAR(center.x / center.w, 0.0f, 1e-4f);
    CHECK_NEAR(center.y / center.w, 0.0f, 1e-4f);

    // A tall narrow viewport: the horizontal field of view is the limit.
    camera.SetViewportSize(400, 1000);
    camera.SetViewState(camera.ComputeFrameState(box));
    extent = MaxCornerNdc(camera, box, inside);
    CHECK(inside);
    CHECK(extent > 0.8f);
    // Seen from straight above, a flat wide box fills the view sideways.
    camera.SetViewportSize(1600, 900);
    camera.SetOrientation(1.5f, 0.0f);
    AABB flat{ { -30.0f, 0.0f, -1.0f }, { 30.0f, 0.2f, 1.0f } };
    camera.SetViewState(camera.ComputeFrameState(flat));
    extent = MaxCornerNdc(camera, flat, inside);
    CHECK(inside);
    CHECK(extent > 0.8f);

    // 2D (orthographic, looking down -Z): the box's extent across the view fits tightly.
    camera.SetViewportSize(1600, 900);
    camera.SetOrientation(0.0f, 0.0f);
    camera.SetOrthographic(true);
    AABB level{ { 0.0f, -1.0f, 0.0f }, { 32.0f, 11.0f, 0.0f } };
    camera.SetViewState(camera.ComputeFrameState(level));
    extent = MaxCornerNdc(camera, level, inside);
    CHECK(inside);
    CHECK(extent > 0.8f);

    // A single point is framed from a sensible distance, not from inside it.
    camera.SetOrthographic(false);
    AABB point{ glm::vec3(2.0f), glm::vec3(2.0f) };
    EditorCameraState p = camera.ComputeFrameState(point);
    CHECK(p.Distance > 1.5f && p.Distance < 10.0f);
    // Huge scenes stay inside the far plane.
    AABB huge{ glm::vec3(-5000.0f), glm::vec3(5000.0f) };
    CHECK(camera.ComputeFrameState(huge).Distance <= camera.GetFarClip() * 0.5f + 1e-3f);
}

TEST(NameLookupFollowsSceneOrder)
{
    Ref<Scene> scene = CreateRef<Scene>();
    Entity a = scene->CreateEntity("Twin");
    Entity m = scene->CreateEntity("Middle");
    Entity b = scene->CreateEntity("Twin");
    CHECK(scene->FindEntityByName("Twin") == a);
    // Reordering the roots (hierarchy drag & drop) changes which "Twin" comes first at once.
    CHECK(scene->SetSiblingIndex(b, 0));
    CHECK(scene->FindEntityByName("Twin") == b);
    std::vector<Entity> twins = scene->FindEntitiesByName("Twin");
    CHECK(twins.size() == 2 && twins[0] == b && twins[1] == a);
    CHECK(scene->MoveEntityNextTo(b, m, true)); // a, Middle, b
    CHECK(scene->FindEntityByName("Twin") == a);
    twins = scene->FindEntitiesByName("Twin");
    CHECK(twins.size() == 2 && twins[0] == a && twins[1] == b);
    // A move that changes nothing does not throw the cache away.
    uint32_t rebuilds = scene->GetNameCacheStats().Rebuilds;
    CHECK(scene->SetSiblingIndex(b, 2));
    CHECK(scene->FindEntityByName("Twin") == a);
    CHECK(scene->GetNameCacheStats().Rebuilds == rebuilds);
}

TEST(DestroyKeepsSceneOrder)
{
    // Many entities, some with children; a third of them goes in one batch, then more one by
    // one and through the deferred queue: the survivors keep their order every time.
    Ref<Scene> scene = CreateRef<Scene>();
    std::vector<Entity> roots;
    for (int i = 0; i < 3000; i++)
        roots.push_back(scene->CreateEntity(std::format("E{}", i)));
    std::vector<Entity> children;
    for (int i = 0; i < 3000; i += 50)
    {
        Entity c = scene->CreateEntity(std::format("C{}", i));
        scene->SetParent(c, roots[i], false);
        Entity g = scene->CreateEntity(std::format("G{}", i));
        scene->SetParent(g, c, false);
        children.push_back(c);
        children.push_back(g);
    }
    auto ids = [&]() {
        std::vector<uint64_t> out;
        for (Entity e : scene->GetEntitiesInOrder())
            out.push_back((uint64_t)e.GetUUID());
        return out;
    };
    std::vector<uint64_t> expected = ids();
    auto forget = [&](Entity e) {
        std::vector<Entity> gone{ e };
        for (Entity d : scene->GetDescendants(e))
            gone.push_back(d);
        for (Entity g : gone)
            std::erase(expected, (uint64_t)g.GetUUID());
    };

    std::vector<Entity> batch;
    for (int i = 0; i < 3000; i += 3)
        batch.push_back(roots[i]);
    batch.push_back(children[3]); // a grandchild whose parent stays
    for (Entity e : batch)
        forget(e);
    CHECK(scene->DestroyEntities(batch) == batch.size());
    CHECK(ids() == expected);
    CHECK(scene->FindEntityByName("E0") == Entity() && scene->FindEntityByName("E1") == roots[1]);

    // One by one, from the front (a subtree in the middle of the order).
    for (int i : { 1, 50, 2 })
    {
        forget(roots[i]);
        scene->DestroyEntity(roots[i]);
        CHECK(ids() == expected);
    }
    // Deferred (the runtime's queue: a child and its parent both queued, in either order).
    Entity c100 = scene->FindEntityByName("C100");
    CHECK(c100 && scene->GetParent(c100) == roots[100]);
    forget(roots[100]);
    forget(roots[4]);
    scene->DestroyEntityDeferred(c100);
    scene->DestroyEntityDeferred(roots[100]);
    scene->DestroyEntityDeferred(roots[4]);
    scene->OnUpdateRuntime(0.016f); // no camera: nothing is drawn
    CHECK(ids() == expected);
    CHECK(!roots[100].IsValid() && !roots[4].IsValid() && roots[5].IsValid());
    // A new entity still goes last.
    Entity last = scene->CreateEntity("Last");
    CHECK(scene->GetEntitiesInOrder().back() == last);
}

TEST(SceneBoundsForFraming)
{
    Ref<Scene> scene = CreateRef<Scene>();
    AABB box;
    CHECK(!scene->ComputeSceneBounds(box)); // empty scene: nothing to frame

    Entity sprite = scene->CreateEntity("Sprite");
    sprite.AddComponent<SpriteRendererComponent>();
    sprite.GetComponent<TransformComponent>().Translation = { 3.0f, 1.0f, 0.0f };
    sprite.GetComponent<TransformComponent>().Scale = { 2.0f, 2.0f, 1.0f };
    CHECK(scene->GetEntityBounds(sprite, box));
    CHECK_NEAR(box.Min.x, 2.0f, 1e-5f);
    CHECK_NEAR(box.Max.x, 4.0f, 1e-5f);
    CHECK_NEAR(box.Min.y, 0.0f, 1e-5f);
    CHECK_NEAR(box.Max.y, 2.0f, 1e-5f);

    Entity cube = scene->CreateEntity("Cube");
    cube.AddComponent<MeshRendererComponent>(); // primitive cube, no GPU mesh needed
    cube.GetComponent<TransformComponent>().Rotation = { 0.0f, glm::radians(45.0f), 0.0f };
    CHECK(scene->GetEntityBounds(cube, box));
    CHECK_NEAR(box.Max.x, std::sqrt(0.5f), 1e-4f); // the rotated corners reach further
    CHECK_NEAR(box.Max.y, 0.5f, 1e-5f);

    Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<MeshRendererComponent>().Type = PrimitiveMesh::Plane;
    ground.GetComponent<TransformComponent>().Scale = { 20.0f, 1.0f, 20.0f };
    ground.GetComponent<TransformComponent>().Translation = { 0.0f, -1.0f, 0.0f };
    CHECK(scene->GetEntityBounds(ground, box));
    CHECK_NEAR(box.Min.x, -10.0f, 1e-4f);
    CHECK_NEAR(box.Min.y, -1.0f, 1e-5f);
    CHECK_NEAR(box.Max.y, -1.0f, 1e-5f); // flat

    Entity map = scene->CreateEntity("Map");
    auto& tm = map.AddComponent<TilemapComponent>();
    tm.Width = 4;
    tm.Height = 2;
    tm.TileSize = 0.5f;
    map.GetComponent<TransformComponent>().Translation = { 10.0f, 0.0f, 0.0f };
    CHECK(scene->GetEntityBounds(map, box));
    CHECK_NEAR(box.Min.x, 10.0f, 1e-5f);
    CHECK_NEAR(box.Max.x, 12.0f, 1e-5f);
    CHECK_NEAR(box.Max.y, 1.0f, 1e-5f);

    // No renderer (or a text without a loaded font): the position.
    Entity light = scene->CreateEntity("Light");
    light.AddComponent<PointLightComponent>();
    light.GetComponent<TransformComponent>().Translation = { 0.0f, 25.0f, 0.0f };
    CHECK(scene->GetEntityBounds(light, box));
    CHECK_NEAR(box.Min.y, 25.0f, 1e-5f);
    CHECK_NEAR(box.Max.y, 25.0f, 1e-5f);

    // Screen-space UI is not in the world.
    Entity ui = scene->CreateEntity("Button");
    ui.AddComponent<UITransformComponent>();
    ui.AddComponent<SpriteRendererComponent>();
    ui.GetComponent<TransformComponent>().Translation = { 900.0f, 900.0f, 0.0f };
    CHECK(!scene->GetEntityBounds(ui, box));

    // Children count with their parent (in world space).
    Entity parent = scene->CreateEntity("Parent");
    parent.GetComponent<TransformComponent>().Translation = { -20.0f, 0.0f, 0.0f };
    Entity child = scene->CreateEntity("Child");
    child.AddComponent<SpriteRendererComponent>();
    child.GetComponent<TransformComponent>().Translation = { 0.0f, 5.0f, 0.0f };
    scene->SetParent(child, parent, false);
    CHECK(scene->ComputeBounds({ parent }, box, true));
    CHECK_NEAR(box.Min.x, -20.5f, 1e-5f);
    CHECK_NEAR(box.Max.y, 5.5f, 1e-5f);
    CHECK(scene->ComputeBounds({ parent }, box, false));
    CHECK_NEAR(box.Max.y, 0.0f, 1e-5f);
    CHECK(!scene->ComputeBounds({ ui }, box));
    CHECK(!scene->ComputeBounds({}, box));

    // The whole scene: what draws, except the UI button. The light (y 25) and the empty
    // parent draw nothing: a sun high above the level would push the view off the content.
    CHECK(scene->ComputeSceneBounds(box));
    CHECK_NEAR(box.Min.x, -20.5f, 1e-4f); // the child sprite
    CHECK_NEAR(box.Max.x, 12.0f, 1e-4f);
    CHECK_NEAR(box.Min.y, -1.0f, 1e-4f);
    CHECK_NEAR(box.Max.y, 5.5f, 1e-4f);
    CHECK_NEAR(box.Min.z, -10.0f, 1e-4f);
    CHECK_NEAR(box.Max.z, 10.0f, 1e-4f);

    // Hidden in the editor (the hierarchy's eye): left out with the whole subtree.
    scene->SetHiddenInEditor(parent, true); // and with it the child sprite at x -20
    Entity parked = scene->CreateEntity("Template");
    parked.AddComponent<MeshRendererComponent>();
    parked.GetComponent<TransformComponent>().Translation = { 0.0f, -50.0f, 0.0f };
    scene->SetHiddenInEditor(parked, true);
    CHECK(scene->ComputeSceneBounds(box));
    CHECK_NEAR(box.Min.x, -10.0f, 1e-4f); // the ground's edge
    CHECK_NEAR(box.Min.y, -1.0f, 1e-4f);
    CHECK_NEAR(box.Max.y, 2.0f, 1e-4f);   // the scaled sprite
    // The selection's F still frames a hidden entity that is selected.
    CHECK(scene->ComputeBounds({ parked }, box));
    CHECK_NEAR(box.Min.y, -50.5f, 1e-4f);

    // Nothing draws: the positions are framed; everything hidden: nothing to frame.
    Ref<Scene> bare = CreateRef<Scene>();
    Entity sun = bare->CreateEntity("Sun");
    sun.AddComponent<DirectionalLightComponent>();
    sun.GetComponent<TransformComponent>().Translation = { 0.0f, 10.0f, 0.0f };
    Entity cam = bare->CreateEntity("Camera");
    cam.AddComponent<CameraComponent>();
    cam.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 5.0f };
    CHECK(bare->ComputeSceneBounds(box));
    CHECK_NEAR(box.Min.y, 0.0f, 1e-5f);
    CHECK_NEAR(box.Max.y, 10.0f, 1e-5f);
    CHECK_NEAR(box.Max.z, 5.0f, 1e-5f);
    bare->SetHiddenInEditor(sun, true);
    bare->SetHiddenInEditor(cam, true);
    CHECK(!bare->ComputeSceneBounds(box));
}

TEST(GizmoDuplicateWhileDragging)
{
    Ref<Scene> scene = CreateRef<Scene>();
    Entity a = scene->CreateEntity("A");
    Entity b = scene->CreateEntity("B");
    b.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 2.0f };
    auto translate = [](glm::vec3 t) { return glm::translate(glm::mat4(1.0f), t); };

    // The gizmo was clicked but not dragged: nothing moved (an Alt+click makes no copies).
    {
        GroupTransformEdit click;
        glm::mat4 start = GroupTransformEdit::ComputePivot(*scene, { a, b }, a, false, false);
        click.Begin(*scene, { a, b }, start);
        CHECK(!click.HasMoved(start));
        glm::mat4 noise = start;
        noise[3][0] += 1e-7f;
        CHECK(!click.HasMoved(noise));
        CHECK(click.HasMoved(translate({ 0.01f, 0.0f, 0.0f })));
        CHECK(click.HasMoved(glm::rotate(start, 0.01f, glm::vec3(0.0f, 1.0f, 0.0f))));
        CHECK(click.GetStartPivot() == start);
        click.End();
        CHECK(scene->GetEntitiesInOrder().size() == 2);
    }

    // Alt + drag: duplicate first, then drag the copies; the originals do not move.
    std::vector<Entity> copies = scene->DuplicateEntities({ a, b });
    CHECK(copies.size() == 2);
    GroupTransformEdit edit;
    glm::mat4 pivot = GroupTransformEdit::ComputePivot(*scene, copies, copies[0], false, false);
    edit.Begin(*scene, copies, pivot);
    edit.Update(translate({ 3.0f, 0.0f, 0.0f }), GroupTransformEdit::Operation::Translate);
    CHECK_NEAR(a.GetComponent<TransformComponent>().Translation.x, 0.0f, 1e-6f);
    CHECK_NEAR(copies[0].GetComponent<TransformComponent>().Translation.x, 3.0f, 1e-5f);
    CHECK_NEAR(copies[1].GetComponent<TransformComponent>().Translation.z, 2.0f, 1e-5f);
    CHECK_NEAR(copies[1].GetComponent<TransformComponent>().Translation.x, 3.0f, 1e-5f);

    // Ctrl+D in the middle of the drag: copies of the dragged entities stay at the current
    // place and the drag goes on with them, restarted from the current gizmo matrix.
    glm::mat4 current = translate({ 3.0f, 0.0f, 0.0f });
    std::vector<Entity> stamps = scene->DuplicateEntities(copies);
    edit.Begin(*scene, stamps, current);
    edit.Update(translate({ 7.0f, 1.0f, 0.0f }), GroupTransformEdit::Operation::Translate);
    CHECK_NEAR(copies[0].GetComponent<TransformComponent>().Translation.x, 3.0f, 1e-5f); // left behind
    CHECK_NEAR(stamps[0].GetComponent<TransformComponent>().Translation.x, 7.0f, 1e-5f);
    CHECK_NEAR(stamps[0].GetComponent<TransformComponent>().Translation.y, 1.0f, 1e-5f);
    CHECK_NEAR(stamps[1].GetComponent<TransformComponent>().Translation.z, 2.0f, 1e-5f);
    edit.End();
    CHECK(scene->GetEntitiesInOrder().size() == 6);

    // The same with scaling: the copy keeps the scale it had, the dragged one goes on.
    Entity s = scene->CreateEntity("S");
    edit.Begin(*scene, { s }, glm::mat4(1.0f));
    glm::mat4 twice = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    edit.Update(twice, GroupTransformEdit::Operation::Scale);
    std::vector<Entity> sc = scene->DuplicateEntities({ s });
    edit.Begin(*scene, sc, twice);
    edit.Update(glm::scale(glm::mat4(1.0f), glm::vec3(3.0f)), GroupTransformEdit::Operation::Scale);
    CHECK_NEAR(s.GetComponent<TransformComponent>().Scale.x, 2.0f, 1e-5f);
    CHECK_NEAR(sc[0].GetComponent<TransformComponent>().Scale.x, 3.0f, 1e-5f);
}

// ------------------------------------------------------------------ M02: inspector / hierarchy (31-35)
static std::string Join(const std::vector<std::string>& keys)
{
    std::string out;
    for (const auto& k : keys)
        out += (out.empty() ? "" : ",") + k;
    return out;
}

TEST(ComponentKeysMatchSceneFiles)
{
    // Every inspector component has a scene-file key, and copying a component yields exactly
    // the JSON the scene file has under that key (no GPU assets are touched).
    auto scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("All");
    e.AddComponent<TagsComponent>().Add("Enemy");
    e.AddComponent<CameraComponent>();
    e.AddComponent<SpriteRendererComponent>();
    e.AddComponent<SpriteAnimationComponent>();
    e.AddComponent<CircleRendererComponent>();
    e.AddComponent<TextComponent>().Text = "Salom";
    e.AddComponent<MeshRendererComponent>();
    e.AddComponent<AnimatorComponent>();
    e.AddComponent<DirectionalLightComponent>();
    e.AddComponent<PointLightComponent>();
    e.AddComponent<Rigidbody2DComponent>();
    e.AddComponent<BoxCollider2DComponent>();
    e.AddComponent<CircleCollider2DComponent>();
    e.AddComponent<Rigidbody3DComponent>();
    e.AddComponent<BoxCollider3DComponent>();
    e.AddComponent<SphereCollider3DComponent>();
    e.AddComponent<CapsuleCollider3DComponent>();
    e.AddComponent<CharacterController3DComponent>();
    e.AddComponent<TilemapComponent>();
    e.AddComponent<ParticleSystemComponent>();
    e.AddComponent<UITransformComponent>();
    e.AddComponent<UIButtonComponent>();
    e.AddComponent<AudioSourceComponent>();
    e.AddComponent<NativeScriptComponent>().ClassName = "TestScript";

    const auto& types = ComponentRegistry::GetTypes();
    CHECK(types.size() == 25);
    CHECK(types.front().Key == "Transform");
    std::vector<std::string> keys = ComponentRegistry::GetKeys(e);
    CHECK(keys.size() == types.size());
    std::string file = SceneSerializer::SerializeEntities(*scene, { e });
    for (const auto& type : types)
    {
        std::string clip = SceneSerializer::SerializeComponent(e, type.Key);
        CHECK(!clip.empty());
        CHECK(SceneSerializer::GetSerializedComponentKey(clip) == type.Key);
        CHECK(file.find("\"" + std::string(type.Key) + "\":") != std::string::npos);
        CHECK(ComponentRegistry::Find(type.Key) == &type);
    }
    CHECK(ComponentKey<Rigidbody2DComponent>() == "Rigidbody2D");
    CHECK(ComponentKey<NativeScriptComponent>() == "NativeScript");
    CHECK(!ComponentRegistry::Find("Editor") && !ComponentRegistry::Find("Relationship"));
    CHECK(!ComponentRegistry::Remove(e, "Transform"));       // every entity keeps one
    CHECK(ComponentRegistry::Remove(e, "PointLight") && !e.HasComponent<PointLightComponent>());
    CHECK(!ComponentRegistry::Remove(e, "PointLight"));      // already gone
    CHECK(SceneSerializer::SerializeComponent(e, "PointLight").empty());
}

TEST(InspectorComponentOrder)
{
    auto scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Player");
    e.AddComponent<NativeScriptComponent>().ClassName = "TestScript";
    e.AddComponent<Rigidbody2DComponent>();
    e.AddComponent<BoxCollider2DComponent>();
    CHECK(Join(ComponentRegistry::GetOrderedKeys(e)) == "Transform,Rigidbody2D,BoxCollider2D,NativeScript");

    // The script to the top (right below Transform, which never moves).
    CHECK(ComponentRegistry::MoveComponent(e, "NativeScript", 0));
    CHECK(Join(ComponentRegistry::GetOrderedKeys(e)) == "Transform,NativeScript,Rigidbody2D,BoxCollider2D");
    CHECK(e.HasComponent<EditorMetadataComponent>());
    CHECK(Join(e.GetComponent<EditorMetadataComponent>().ComponentOrder) == "NativeScript,Rigidbody2D,BoxCollider2D");
    CHECK(!ComponentRegistry::MoveComponent(e, "Transform", 3));
    CHECK(!ComponentRegistry::MoveComponent(e, "NativeScript", 1));   // already there
    CHECK(!ComponentRegistry::MoveComponent(e, "Camera", 1));         // the entity has none
    CHECK(ComponentRegistry::GetComponentIndex(e, "BoxCollider2D") == 3);

    // One step down, then to the end (-1).
    CHECK(ComponentRegistry::MoveComponent(e, "Rigidbody2D", 3));
    CHECK(Join(ComponentRegistry::GetOrderedKeys(e)) == "Transform,NativeScript,BoxCollider2D,Rigidbody2D");
    CHECK(ComponentRegistry::MoveComponent(e, "NativeScript", -1));
    CHECK(Join(ComponentRegistry::GetOrderedKeys(e)) == "Transform,BoxCollider2D,Rigidbody2D,NativeScript");

    // Saved with the scene; a component added later goes to the bottom.
    std::string text = SceneSerializer(scene).SerializeToString();
    CHECK(text.find("\"ComponentOrder\"") != std::string::npos);
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    Entity le = loaded->FindEntityByName("Player");
    CHECK(Join(ComponentRegistry::GetOrderedKeys(le)) == "Transform,BoxCollider2D,Rigidbody2D,NativeScript");
    le.AddComponent<CameraComponent>();
    CHECK(Join(ComponentRegistry::GetOrderedKeys(le)) == "Transform,BoxCollider2D,Rigidbody2D,NativeScript,Camera");
    // Removed components drop out; play-mode copies / duplicates keep the order.
    le.RemoveComponent<Rigidbody2DComponent>();
    CHECK(Join(ComponentRegistry::GetOrderedKeys(le)) == "Transform,BoxCollider2D,NativeScript,Camera");
    auto runtime = Scene::Copy(loaded);
    CHECK(Join(ComponentRegistry::GetOrderedKeys(runtime->FindEntityByName("Player"))) == "Transform,BoxCollider2D,NativeScript,Camera");
    Entity dup = loaded->DuplicateEntity(le);
    CHECK(Join(ComponentRegistry::GetOrderedKeys(dup)) == "Transform,BoxCollider2D,NativeScript,Camera");

    // Back to the default order: nothing is stored any more (the file is as before).
    CHECK(ComponentRegistry::MoveComponent(e, "Rigidbody2D", 1));
    CHECK(Join(ComponentRegistry::GetOrderedKeys(e)) == "Transform,Rigidbody2D,BoxCollider2D,NativeScript");
    CHECK(!e.HasComponent<EditorMetadataComponent>());
    CHECK(SceneSerializer(scene).SerializeToString().find("\"Editor\"") == std::string::npos);

    // Sorting rules on their own: unknown / missing keys are ignored, Transform always first.
    CHECK(Join(ComponentRegistry::SortKeys({ "Transform", "A", "B", "C" }, { "C", "Transform", "X", "A" })) == "Transform,C,A,B");
    CHECK(Join(ComponentRegistry::SortKeys({ "A", "B" }, {})) == "A,B");
}

TEST(ComponentClipboardCopyPaste)
{
    auto scene = CreateRef<Scene>();
    Entity a = scene->CreateEntity("A");
    auto& rb = a.AddComponent<Rigidbody2DComponent>();
    rb.Type = Rigidbody2DComponent::BodyType::Dynamic;
    rb.GravityScale = 0.25f;
    rb.FixedRotation = true;
    auto& script = a.AddComponent<NativeScriptComponent>();
    script.ClassName = "TestScript";
    ScriptFieldValue speed;
    speed.Set(7.5f);
    script.Fields["Speed"] = speed;
    a.GetTransform().Translation = { 1.0f, 2.0f, 3.0f };

    std::string clip = SceneSerializer::SerializeComponent(a, "Rigidbody2D");
    CHECK(SceneSerializer::GetSerializedComponentKey(clip) == "Rigidbody2D");
    CHECK(SceneSerializer::GetSerializedComponentKey("salom").empty());
    CHECK(SceneSerializer::GetSerializedComponentKey("{\"ZaminComponent\":\"Nope\",\"Data\":{}}").empty());
    CHECK(SceneSerializer::GetSerializedComponentKey(SceneSerializer::SerializeTransform(a.GetTransform())).empty());

    // Pasted onto an entity without one: added with the values.
    Entity b = scene->CreateEntity("B");
    CHECK(SceneSerializer::DeserializeComponent(clip, b));
    CHECK(b.HasComponent<Rigidbody2DComponent>());
    CHECK(b.GetComponent<Rigidbody2DComponent>().Type == Rigidbody2DComponent::BodyType::Dynamic);
    CHECK_NEAR(b.GetComponent<Rigidbody2DComponent>().GravityScale, 0.25f, 1e-6f);
    CHECK(b.GetComponent<Rigidbody2DComponent>().FixedRotation);

    // Onto one that has it: values replaced, nothing else touched; across scenes too.
    auto other = CreateRef<Scene>();
    Entity c = other->CreateEntity("C");
    c.AddComponent<Rigidbody2DComponent>().GravityScale = 3.0f;
    c.AddComponent<BoxCollider2DComponent>().Size = { 2.0f, 2.0f };
    CHECK(SceneSerializer::DeserializeComponent(clip, c));
    CHECK_NEAR(c.GetComponent<Rigidbody2DComponent>().GravityScale, 0.25f, 1e-6f);
    CHECK_NEAR(c.GetComponent<BoxCollider2DComponent>().Size.x, 2.0f, 1e-6f);

    // Script component with its field values; Transform through the same path.
    std::string scriptClip = SceneSerializer::SerializeComponent(a, "NativeScript");
    CHECK(SceneSerializer::DeserializeComponent(scriptClip, c));
    CHECK(c.GetComponent<NativeScriptComponent>().ClassName == "TestScript");
    CHECK(c.GetComponent<NativeScriptComponent>().Fields.count("Speed") == 1);
    CHECK_NEAR(c.GetComponent<NativeScriptComponent>().Fields["Speed"].Get<float>(), 7.5f, 1e-6f);
    CHECK(c.GetComponent<NativeScriptComponent>().Instance == nullptr);
    CHECK(SceneSerializer::DeserializeComponent(SceneSerializer::SerializeComponent(a, "Transform"), c));
    CHECK_NEAR(c.GetTransform().Translation.z, 3.0f, 1e-6f);

    // Anything else is refused and leaves the target as it was.
    CHECK(!SceneSerializer::DeserializeComponent("salom", c));
    CHECK(!SceneSerializer::DeserializeComponent("{\"ZaminComponent\":\"Rigidbody2D\",\"Data\":{\"GravityScale\":\"x\"}}", c));
    CHECK_NEAR(c.GetComponent<Rigidbody2DComponent>().GravityScale, 0.25f, 1e-6f);
    CHECK(!SceneSerializer::DeserializeComponent(clip, Entity()));
    CHECK(SceneSerializer::SerializeComponent(b, "Camera").empty());

    // A pasted component works at runtime like any other (body created on start).
    auto play = Scene::Copy(scene);
    play->OnRuntimeStart();
    Entity pb = play->FindEntityByName("B");
    CHECK(pb.GetComponent<Rigidbody2DComponent>().RuntimeBody != nullptr);
    play->OnRuntimeStop();
}

TEST(EditorHiddenAndLockedFlags)
{
    auto scene = CreateRef<Scene>();
    Entity parent = scene->CreateEntity("Parent");
    Entity child = scene->CreateEntity("Child");
    Entity grandchild = scene->CreateEntity("Grandchild");
    Entity other = scene->CreateEntity("Other");
    scene->SetParent(child, parent, false);
    scene->SetParent(grandchild, child, false);
    CHECK(!scene->IsHiddenInEditor(grandchild) && !scene->IsLockedInEditor(grandchild));
    CHECK(scene->CollectHiddenInEditor().empty());

    // A hidden / locked parent hides / locks its whole subtree.
    scene->SetHiddenInEditor(parent, true);
    scene->SetLockedInEditor(child, true);
    CHECK(scene->IsHiddenInEditor(parent, false) && !scene->IsHiddenInEditor(child, false));
    CHECK(scene->IsHiddenInEditor(child) && scene->IsHiddenInEditor(grandchild));
    CHECK(!scene->IsLockedInEditor(parent) && scene->IsLockedInEditor(child) && scene->IsLockedInEditor(grandchild));
    CHECK(!scene->IsLockedInEditor(grandchild, false));
    CHECK(!scene->IsHiddenInEditor(other) && !scene->IsLockedInEditor(other));
    CHECK(scene->CountHiddenInEditor() == 1 && scene->CountLockedInEditor() == 1);
    auto hidden = scene->CollectHiddenInEditor();
    CHECK(hidden.size() == 3 && hidden.contains(grandchild) && !hidden.contains(other));
    // What viewport clicks go through (the picking pass leaves these out): the locked subtree.
    auto locked = scene->CollectLockedInEditor();
    CHECK(locked.size() == 2 && locked.contains(child) && locked.contains(grandchild) && !locked.contains(parent));

    // Saved with the scene, kept by play-mode copies and duplicates.
    std::string text = SceneSerializer(scene).SerializeToString();
    CHECK(text.find("\"Hidden\": true") != std::string::npos && text.find("\"Locked\": true") != std::string::npos);
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    CHECK(loaded->IsHiddenInEditor(loaded->FindEntityByName("Grandchild")));
    CHECK(loaded->IsLockedInEditor(loaded->FindEntityByName("Child"), false));
    CHECK(SceneSerializer(loaded).SerializeToString() == text);
    auto runtime = Scene::Copy(scene);
    CHECK(runtime->IsHiddenInEditor(runtime->FindEntityByName("Parent"), false));
    Entity copy = scene->DuplicateEntity(parent);
    CHECK(scene->IsHiddenInEditor(copy, false) && scene->CountHiddenInEditor() == 2);
    scene->DestroyEntity(copy);
    // Script Instantiate() of a hidden / locked template: the clones start visible and unlocked.
    Entity clone = scene->Instantiate(parent);
    CHECK(clone && !scene->IsHiddenInEditor(clone) && scene->CountHiddenInEditor() == 1);
    CHECK(scene->CountLockedInEditor() == 1 && !scene->IsLockedInEditor(scene->FindChild(clone, "Grandchild")));
    scene->DestroyEntity(clone);
    CHECK(scene->IsHiddenInEditor(parent, false) && scene->IsLockedInEditor(child, false));

    // Prefabs leave the flags out (instances start visible / unlocked), but keep the component order.
    child.AddComponent<Rigidbody2DComponent>();
    child.AddComponent<BoxCollider2DComponent>();
    CHECK(ComponentRegistry::MoveComponent(child, "BoxCollider2D", 1));
    std::string prefab = SceneSerializer::SerializeEntities(*scene, { parent, child, grandchild }, false);
    CHECK(prefab.find("\"Hidden\"") == std::string::npos && prefab.find("\"Locked\"") == std::string::npos);
    CHECK(prefab.find("\"ComponentOrder\"") != std::string::npos);
    auto third = CreateRef<Scene>();
    auto inst = SceneSerializer::DeserializeEntities(*third, prefab, true);
    CHECK(inst.size() == 3 && !third->IsHiddenInEditor(inst.back()) && third->CountLockedInEditor() == 0);
    Entity instChild = third->FindEntityByName("Child");
    CHECK(Join(ComponentRegistry::GetOrderedKeys(instChild)) == "Transform,BoxCollider2D,Rigidbody2D");
    CHECK(SceneSerializer::SerializeEntities(*scene, { parent }).find("\"Hidden\"") != std::string::npos); // copy / paste keeps them

    // Clearing the flags removes the editor data again (files stay as they were before).
    scene->SetHiddenInEditor(parent, false);
    CHECK(!parent.HasComponent<EditorMetadataComponent>());
    scene->SetLockedInEditor(child, false);
    CHECK(child.HasComponent<EditorMetadataComponent>()); // still holds the component order
    CHECK(ComponentRegistry::MoveComponent(child, "BoxCollider2D", -1));
    CHECK(!child.HasComponent<EditorMetadataComponent>());
    CHECK(SceneSerializer(scene).SerializeToString().find("\"Editor\"") == std::string::npos);

    // A broken file with a parent cycle cannot hang the ancestor walk.
    parent.GetComponent<RelationshipComponent>().Parent = grandchild.GetUUID();
    CHECK(!scene->IsHiddenInEditor(parent));
    parent.GetComponent<RelationshipComponent>().Parent = 0;

    // Hidden entities still run at runtime (the flag only filters the editor's view).
    scene->SetHiddenInEditor(other, true);
    other.AddComponent<NativeScriptComponent>().ClassName = "TestScript";
    ResetCounters();
    auto game = Scene::Copy(scene);
    game->OnRuntimeStart();
    game->OnUpdateRuntime(0.5f);
    CHECK(g_Updated == 1);
    CHECK_NEAR(game->FindEntityByName("Other").GetTransform().Translation.x, 0.5f, 1e-5f);
    game->OnRuntimeStop();
}

TEST(InspectorFoldStateJson)
{
    InspectorFoldState fold;
    CHECK(fold.IsOpen("Rigidbody2D") && !fold.IsOpen("Animator.StateMachine", false));
    CHECK(fold.SetOpen("Rigidbody2D", false));
    CHECK(!fold.SetOpen("Rigidbody2D", false));            // unchanged
    CHECK(!fold.IsOpen("Rigidbody2D"));
    CHECK(fold.SetOpen("Animator.StateMachine", true, false));
    CHECK(fold.SetOpen("NativeScript:PlayerController", false));
    CHECK(!fold.SetOpen("", false));
    CHECK(fold.GetAll().size() == 3);
    CHECK(fold.SetOpen("Rigidbody2D", true));              // back to the default: forgotten
    CHECK(fold.GetAll().size() == 2 && fold.IsOpen("Rigidbody2D"));

    EditorViewSettings settings;
    settings.Inspector = fold;
    std::string text = settings.ToJson();
    CHECK(text.find("InspectorHeaders") != std::string::npos);
    EditorViewSettings loaded;
    CHECK(loaded.FromJson(text));
    CHECK(loaded.Inspector.GetAll() == fold.GetAll());
    CHECK(!loaded.Inspector.IsOpen("NativeScript:PlayerController"));
    CHECK(loaded.Inspector.IsOpen("Animator.StateMachine", false));
    CHECK(loaded.Inspector.IsOpen("NativeScript:Enemy")); // other script classes are separate

    // Junk values are skipped; older files without the section keep the current state.
    EditorViewSettings junk;
    CHECK(junk.FromJson("{\"ZaminEditorView\":1,\"InspectorHeaders\":{\"Camera\":false,\"Bad\":3,\"\":false}}"));
    CHECK(junk.Inspector.GetAll().size() == 1 && !junk.Inspector.IsOpen("Camera"));
    CHECK(junk.FromJson("{\"ZaminEditorView\":1}"));
    CHECK(!junk.Inspector.IsOpen("Camera"));

    // Bounded: a long session with many script classes does not grow the file without end.
    InspectorFoldState many;
    for (size_t i = 0; i < InspectorFoldState::MaxEntries + 20; i++)
        many.SetOpen("NativeScript:S" + std::to_string(i), false);
    CHECK(many.GetAll().size() == InspectorFoldState::MaxEntries);
}

// ------------------------------------------------------------------ content browser / assets
static std::filesystem::path MakeTempDir(const char* prefix)
{
    auto dir = std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string((uint64_t)UUID()));
    std::filesystem::create_directories(dir);
    return dir;
}

static void WriteFile(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    FileSystem::WriteTextFile(path, text);
}

TEST(AssetTypesAndFilter)
{
    CHECK(AssetFiles::GetType("Scenes/Level1.zscene") == AssetType::Scene);
    CHECK(AssetFiles::GetType("a/Ball.ZPREFAB") == AssetType::Prefab);
    CHECK(AssetFiles::GetType("coin.PNG") == AssetType::Texture && AssetFiles::GetType("x.jpeg") == AssetType::Texture);
    CHECK(AssetFiles::GetType("Robot.gltf") == AssetType::Model && AssetFiles::GetType("m.glb") == AssetType::Model);
    CHECK(AssetFiles::GetType("jump.ogg") == AssetType::Audio && AssetFiles::GetType("Inter.ttf") == AssetType::Font);
    CHECK(AssetFiles::GetType("sunset.hdr") == AssetType::Environment);
    CHECK(AssetFiles::GetType("Player.cpp") == AssetType::Script && AssetFiles::GetType("Player.h") == AssetType::Script);
    CHECK(AssetFiles::GetType("Game.zproject") == AssetType::Project);
    CHECK(AssetFiles::GetType("notes.txt") == AssetType::Other && AssetFiles::GetType("Makefile") == AssetType::Other);
    CHECK(AssetFiles::GetType("Textures", true) == AssetType::Folder);
    CHECK(std::string(AssetFiles::GetImportFolder(AssetType::Texture)) == "Textures");
    CHECK(std::string(AssetFiles::GetImportFolder(AssetType::Model)) == "Models");
    CHECK(std::string(AssetFiles::GetImportFolder(AssetType::Other)).empty());
    for (int t = 0; t < (int)AssetType::Count; t++)
        CHECK(AssetFiles::FindTypeByKey(AssetFiles::GetTypeKey((AssetType)t)) == (AssetType)t);
    CHECK(!AssetFiles::FindTypeByKey("Nope"));

    AssetTypeFilter filter;
    CHECK(!filter.IsActive() && filter.Matches(AssetType::Folder) && filter.Matches(AssetType::Audio));
    filter.Set(AssetType::Texture, true);
    filter.Toggle(AssetType::Model);
    filter.Set(AssetType::Folder, true); // folders are not a filter type
    CHECK(filter.IsActive() && filter.Count() == 2);
    CHECK(filter.Matches(AssetType::Texture) && filter.Matches(AssetType::Model));
    CHECK(!filter.Matches(AssetType::Folder) && !filter.Matches(AssetType::Audio) && !filter.Matches(AssetType::Project));
    filter.Set(AssetType::Other, true);
    CHECK(filter.Matches(AssetType::Project)); // the project file counts as "other"
    filter.Toggle(AssetType::Model);
    CHECK(!filter.Matches(AssetType::Model) && filter.Count() == 2);
    filter.Clear();
    CHECK(!filter.IsActive());
    CHECK(AssetTypeFilter::GetFilterableTypes().size() == 10);

    // Natural, case-insensitive name order.
    CHECK(AssetFiles::CompareNames("Level2", "Level10") < 0);
    CHECK(AssetFiles::CompareNames("level10", "Level2") > 0);
    CHECK(AssetFiles::CompareNames("apple", "Banana") < 0);
    CHECK(AssetFiles::CompareNames("a", "a") == 0 && AssetFiles::CompareNames("a", "ab") < 0);
    CHECK(AssetFiles::CompareNames("img007", "img7") != 0);

    CHECK(AssetFiles::FormatSize(512) == "512 B");
    CHECK(AssetFiles::FormatSize(1536) == "1.5 KB");
    CHECK(AssetFiles::FormatSize(300 * 1024) == "300 KB");
    CHECK(AssetFiles::FormatSize(5u * 1024 * 1024 + 300 * 1024) == "5.3 MB");
}

TEST(AssetListingAndSorting)
{
    namespace fs = std::filesystem;
    fs::path root = MakeTempDir("zamin-list-");
    WriteFile(root / "Level10.zscene", "{}");
    WriteFile(root / "Level2.zscene", "{}");
    WriteFile(root / "big.png", std::string(4000, 'x'));
    WriteFile(root / "small.png", "x");
    WriteFile(root / ".hidden.png", "x");
    WriteFile(root / "Textures" / "wall.png", "xx");
    WriteFile(root / "Textures" / "Deep" / "floor.png", "xxx");
    WriteFile(root / "Models" / "Robot.gltf", "{}");
    WriteFile(root / "Build" / "junk.png", "x");
    WriteFile(root / ".git" / "obj.png", "x");

    AssetListOptions options;
    auto names = [](const std::vector<AssetEntry>& entries) {
        std::vector<std::string> out;
        for (const auto& e : entries) out.push_back(e.Name);
        return out;
    };

    // One folder: folders first, then files in natural order; hidden names skipped.
    auto entries = AssetFiles::List(root, options);
    CHECK((names(entries) == std::vector<std::string>{ "Build", "Models", "Textures", "big.png", "Level2.zscene", "Level10.zscene", "small.png" }));
    CHECK(entries[0].IsDirectory && entries[0].Type == AssetType::Folder);
    CHECK(entries[3].Type == AssetType::Texture && entries[3].Size == 4000);

    // Excluded folders are left out (and not searched below).
    options.Excluded = { root / "Build" };
    CHECK(AssetFiles::List(root, options).size() == 6);

    // Type filter: the whole subtree, files of those types only.
    options.Filter.Set(AssetType::Texture, true);
    entries = AssetFiles::List(root, options);
    CHECK((names(entries) == std::vector<std::string>{ "big.png", "floor.png", "small.png", "wall.png" }));
    options.Filter.Set(AssetType::Model, true);
    CHECK(AssetFiles::List(root, options).size() == 5);

    // Search: the whole subtree too, folders included (a folder's name matches what is inside it);
    // combined with the filter.
    options.Filter.Clear();
    options.Search = "DEEP";
    entries = AssetFiles::List(root, options);
    CHECK(entries.size() == 2 && entries[0].IsDirectory && entries[0].Name == "Deep" && entries[1].Name == "floor.png");
    options.Search = "floor";
    CHECK((names(AssetFiles::List(root, options)) == std::vector<std::string>{ "floor.png" }));
    options.Search = "l";
    options.Filter.Set(AssetType::Scene, true);
    CHECK((names(AssetFiles::List(root, options)) == std::vector<std::string>{ "Level2.zscene", "Level10.zscene" }));

    // Sorting by size (descending): folders stay first.
    options = {};
    options.SortBy = AssetSortColumn::Size;
    options.Ascending = false;
    entries = AssetFiles::List(root, options);
    CHECK(entries[0].IsDirectory && entries[3].Name == "big.png" && entries.back().Size <= entries[4].Size);
    AssetFiles::Sort(entries, AssetSortColumn::Name, false);
    CHECK(entries[0].Name == "Textures" && entries.back().Name == "big.png");
    AssetFiles::Sort(entries, AssetSortColumn::Type, true);
    CHECK(entries[3].Type == AssetType::Texture && entries.back().Type == AssetType::Scene); // "Rasm" < "Sahna"

    CHECK(AssetFiles::List(root / "missing", {}).empty());
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(AssetJsonReferenceRewrite)
{
    // Asset paths: a file, or everything inside a folder.
    CHECK(AssetFiles::RemapPath("Textures/a.png", "Textures/a.png", "Art/a.png") == std::optional<std::string>("Art/a.png"));
    CHECK(AssetFiles::RemapPath("Textures/Deep/b.png", "Textures", "Art/Tex") == std::optional<std::string>("Art/Tex/Deep/b.png"));
    CHECK(!AssetFiles::RemapPath("Textures2/b.png", "Textures", "Art"));
    CHECK(!AssetFiles::RemapPath("Textures/a.png", "", "Art"));
    CHECK(!AssetFiles::RemapPath("a.png", "Textures/a.png", "b.png"));

    // Only string values change; keys, numbers and the layout stay byte for byte.
    std::string text = "{\n  \"Textures/a.png\": \"Textures/a.png\",\n  \"List\": [ \"Textures/a.png\", 1.50, \"x\" ],\n"
                       "  \"Nested\": { \"uri\" :  \"Textures/a.png\" }, \"Esc\": \"Q\\\"\\\\\\u00e9\"\n}";
    std::vector<std::string> keys;
    std::string original = text;
    size_t n = AssetFiles::RewriteJsonStrings(text, [&](std::string_view key, const std::string& value) -> std::optional<std::string> {
        keys.emplace_back(key);
        if (value == "Q\"\\\xc3\xa9") return std::string("R \"2\"");
        return AssetFiles::RemapPath(value, "Textures/a.png", "Art/a b.png");
    });
    CHECK(n == 4);
    CHECK((keys == std::vector<std::string>{ "Textures/a.png", "List", "List", "uri", "Esc" })); // array items: the array's key
    CHECK(text == "{\n  \"Textures/a.png\": \"Art/a b.png\",\n  \"List\": [ \"Art/a b.png\", 1.50, \"x\" ],\n"
                  "  \"Nested\": { \"uri\" :  \"Art/a b.png\" }, \"Esc\": \"R \\\"2\\\"\"\n}");
    std::string same = original;
    CHECK(AssetFiles::RewriteJsonStrings(same, [](std::string_view, const std::string&) { return std::optional<std::string>(); }) == 0);
    CHECK(same == original);
    // The engine's own scene text parses again after a rewrite.
    auto scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Sprite");
    e.AddComponent<SpriteRendererComponent>().TexturePath = "Textures/a.png";
    std::string sceneText = SceneSerializer(scene).SerializeToString();
    CHECK(AssetFiles::RewriteJsonStrings(sceneText, [](std::string_view, const std::string& v) {
        return AssetFiles::RemapPath(v, "Textures", "Sprites"); }) == 1);
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(sceneText));
    CHECK(loaded->FindEntityByName("Sprite").GetComponent<SpriteRendererComponent>().TexturePath == "Sprites/a.png");

    // Nested arrays and objects inside arrays report the right keys.
    std::string nested = "{\"A\": [[\"x\"], {\"k\": \"y\"}, \"z\"], \"B\": \"w\"}";
    keys.clear();
    AssetFiles::RewriteJsonStrings(nested, [&](std::string_view key, const std::string&) { keys.emplace_back(key); return std::optional<std::string>(); });
    CHECK((keys == std::vector<std::string>{ "A", "k", "A", "B" }));

    // Which strings can be asset paths: shaped like a file, not under a name / text key.
    CHECK(AssetFiles::IsAssetPathValue("Texture", "Textures/a.png"));
    CHECK(AssetFiles::IsAssetPathValue("Value", "hero.png"));         // a script string field
    CHECK(AssetFiles::IsAssetPathValue("Value", "Scenes/Level2.zscene"));
    CHECK(!AssetFiles::IsAssetPathValue("Value", "Player"));          // a bare word is a name
    CHECK(!AssetFiles::IsAssetPathValue("Tag", "Player/hero.png"));   // entity names never
    CHECK(!AssetFiles::IsAssetPathValue("Tags", "AI/Patrol"));
    CHECK(!AssetFiles::IsAssetPathValue("Text", "Textures/a.png"));
    CHECK(!AssetFiles::IsAssetPathValue("Texture", ""));
    AssetFiles::PathMoves moves{ { "Player", "Characters/Player" } };
    CHECK(AssetFiles::RemapAssetReference("Texture", "Player/hero.png", moves) == std::optional<std::string>("Characters/Player/hero.png"));
    CHECK(!AssetFiles::RemapAssetReference("Tags", "Player", moves));
    CHECK(!AssetFiles::RemapAssetReference("Tag", "Player", moves));
    CHECK(!AssetFiles::RemapAssetReference("Texture", "Players/hero.png", moves));
}

TEST(AssetMoveUpdatesReferences)
{
    namespace fs = std::filesystem;
    fs::path project = MakeTempDir("zamin-move-");
    fs::path assets = project / "Assets", source = project / "Source";
    WriteFile(assets / "Textures" / "coin.png", "png");
    WriteFile(assets / "Textures" / "wall.png", "png");
    WriteFile(assets / "Models" / "Shrine.bin", "bin");
    WriteFile(assets / "Models" / "Shrine.gltf",
              "{\"images\": [{\"uri\": \"../Textures/wall.png\"}], \"buffers\": [{\"uri\": \"Shrine.bin\"}, {\"uri\": \"data:application/octet-stream;base64,AA==\"}]}");
    WriteFile(source / "Game.cpp", "std::string Effect = \"Textures/coin.png\";\n");
    WriteFile(source / "Other.cpp", "int x = 0;\n");

    // A real scene file: a sprite and a script string field use the texture.
    auto scene = CreateRef<Scene>();
    Entity coin = scene->CreateEntity("Coin");
    coin.AddComponent<SpriteRendererComponent>().TexturePath = "Textures/coin.png";
    auto& script = coin.AddComponent<NativeScriptComponent>();
    script.ClassName = "TestScript";
    ScriptFieldValue label;
    label.Set(std::string("Textures/coin.png"));
    script.Fields["Label"] = label;
    fs::path scenePath = assets / "Scenes" / "Level.zscene";
    fs::create_directories(scenePath.parent_path());
    CHECK(SceneSerializer(scene).Serialize(scenePath));
    std::string before = FileSystem::ReadTextFile(scenePath);

    // Not allowed: into itself, where it already is, a taken name, out of the asset folder.
    CHECK(!AssetFiles::CheckMove(assets / "Textures", assets / "Textures", assets).empty());
    CHECK(!AssetFiles::CheckMove(assets / "Textures", assets / "Textures" / "..", assets).empty());
    WriteFile(assets / "Taken" / "coin.png", "other");
    CHECK(!AssetFiles::CheckMove(assets / "Textures" / "coin.png", assets / "Taken", assets).empty());
    CHECK(!AssetFiles::CheckMove(assets / "Textures" / "coin.png", source, assets).empty());
    CHECK(!AssetFiles::CheckMove(assets, project / "Source", assets).empty());
    CHECK(!AssetFiles::CheckMove(assets / "missing.png", assets / "Models", assets).empty());
    fs::create_directories(assets / "Sprites");
    CHECK(AssetFiles::CheckMove(assets / "Textures" / "coin.png", assets / "Sprites", assets).empty());

    // Move one texture: the scene's sprite and script field follow, the C++ file is reported.
    auto result = AssetFiles::MoveInto({ assets / "Textures" / "coin.png" }, assets / "Sprites", assets, source);
    CHECK(result.Errors.empty() && result.Moved.size() == 1);
    CHECK(fs::exists(assets / "Sprites" / "coin.png") && !fs::exists(assets / "Textures" / "coin.png"));
    CHECK(result.UpdatedReferences == 2 && result.UpdatedFiles.size() == 1);
    CHECK(result.SourceMentions.size() == 1 && result.SourceMentions[0].filename() == "Game.cpp");
    std::string after = FileSystem::ReadTextFile(scenePath);
    CHECK(after.size() + 2 == before.size()); // "Textures" -> "Sprites" twice, nothing else changed
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).Deserialize(scenePath));
    Entity loadedCoin = loaded->FindEntityByName("Coin");
    CHECK(loadedCoin.GetComponent<SpriteRendererComponent>().TexturePath == "Sprites/coin.png");
    CHECK(loadedCoin.GetComponent<NativeScriptComponent>().Fields["Label"].Get<std::string>() == "Sprites/coin.png");
    CHECK(FileSystem::ReadTextFile(source / "Game.cpp").find("Textures/coin.png") != std::string::npos); // code is not edited

    // Move the model alone: its relative file names still find the same files.
    fs::create_directories(assets / "Models" / "Props");
    result = AssetFiles::MoveInto({ assets / "Models" / "Shrine.gltf" }, assets / "Models" / "Props", assets, source);
    CHECK(result.Errors.empty() && result.UpdatedReferences == 2);
    std::string gltf = FileSystem::ReadTextFile(assets / "Models" / "Props" / "Shrine.gltf");
    CHECK(gltf.find("\"uri\": \"../../Textures/wall.png\"") != std::string::npos);
    CHECK(gltf.find("\"uri\": \"../Shrine.bin\"") != std::string::npos);
    CHECK(gltf.find("data:application/octet-stream;base64,AA==") != std::string::npos);
    auto deps = AssetFiles::GetModelDependencies(assets / "Models" / "Props" / "Shrine.gltf");
    CHECK(deps.size() == 2);

    // Move a folder the model uses (with a nested selection): the model's uri and the scene follow.
    fs::create_directories(assets / "Art");
    result = AssetFiles::MoveInto({ assets / "Textures", assets / "Textures" / "wall.png", assets / "Sprites" }, assets / "Art", assets, source);
    CHECK(result.Errors.empty() && result.Moved.size() == 2);
    CHECK(fs::exists(assets / "Art" / "Textures" / "wall.png") && fs::exists(assets / "Art" / "Sprites" / "coin.png"));
    gltf = FileSystem::ReadTextFile(assets / "Models" / "Props" / "Shrine.gltf");
    CHECK(gltf.find("\"uri\": \"../../Art/Textures/wall.png\"") != std::string::npos);
    CHECK(result.MapPath(fs::weakly_canonical(assets) / "Sprites" / "coin.png") == fs::weakly_canonical(assets) / "Art" / "Sprites" / "coin.png");
    loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).Deserialize(scenePath));
    CHECK(loaded->FindEntityByName("Coin").GetComponent<SpriteRendererComponent>().TexturePath == "Art/Sprites/coin.png");

    // Rename works the same way; a taken or bad name changes nothing.
    result = AssetFiles::Rename(assets / "Art" / "Sprites" / "coin.png", "gold.png", assets, source);
    CHECK(result.Errors.empty() && result.UpdatedReferences == 2);
    CHECK(AssetFiles::Rename(assets / "Art" / "Sprites" / "gold.png", "a/b.png", assets).Errors.size() == 1);
    WriteFile(assets / "Art" / "Sprites" / "taken.png", "x");
    CHECK(AssetFiles::Rename(assets / "Art" / "Sprites" / "gold.png", "taken.png", assets).Errors.size() == 1);
    loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).Deserialize(scenePath));
    CHECK(loaded->FindEntityByName("Coin").GetComponent<SpriteRendererComponent>().TexturePath == "Art/Sprites/gold.png");

    // Moving the scene file itself; a second item with a taken name is refused, the first still moves.
    fs::create_directories(assets / "Levels");
    WriteFile(assets / "Levels" / "taken.png", "x");
    result = AssetFiles::MoveInto({ scenePath, assets / "Art" / "Sprites" / "taken.png" }, assets / "Levels", assets);
    CHECK(result.Moved.size() == 1 && result.Errors.size() == 1);
    CHECK(fs::exists(assets / "Levels" / "Level.zscene"));

    std::error_code ec;
    fs::remove_all(project, ec);
}

TEST(AssetMoveKeepsNamesAndTags)
{
    // A folder named like entities and tags: renaming / moving it rewrites only asset paths.
    namespace fs = std::filesystem;
    fs::path project = MakeTempDir("zamin-move-names-");
    fs::path assets = project / "Assets";
    WriteFile(assets / "Player" / "hero.png", "png");

    auto scene = CreateRef<Scene>();
    scene->SetName("Player");
    Entity player = scene->CreateEntity("Player");
    player.AddComponent<TagsComponent>().Add("Player");
    player.AddComponent<SpriteRendererComponent>().TexturePath = "Player/hero.png";
    player.AddComponent<TextComponent>().Text = "Player";
    auto& script = player.AddComponent<NativeScriptComponent>();
    script.ClassName = "Player";
    ScriptFieldValue target, icon;
    target.Set(std::string("Player"));
    icon.Set(std::string("Player/hero.png"));
    script.Fields["Target"] = target;
    script.Fields["Icon"] = icon;
    Entity named = scene->CreateEntity("Player/hero.png"); // a (strange) name shaped like the path
    named.AddComponent<TagsComponent>().Add("Player/hero.png");
    fs::path scenePath = assets / "Scenes" / "Level.zscene";
    fs::create_directories(scenePath.parent_path());
    CHECK(SceneSerializer(scene).Serialize(scenePath));

    auto check = [&](const std::string& folder) {
        auto loaded = CreateRef<Scene>();
        CHECK(SceneSerializer(loaded).Deserialize(scenePath));
        CHECK(loaded->GetName() == "Player");
        Entity p = loaded->FindEntityByName("Player");
        CHECK(p && p.GetComponent<TagsComponent>().Has("Player"));
        CHECK(loaded->FindEntityWithTag("Player") == p);
        CHECK(p.GetComponent<TextComponent>().Text == "Player");
        CHECK(p.GetComponent<SpriteRendererComponent>().TexturePath == folder + "/hero.png");
        auto& nsc = p.GetComponent<NativeScriptComponent>();
        CHECK(nsc.ClassName == "Player");
        CHECK(nsc.Fields["Target"].Get<std::string>() == "Player");
        CHECK(nsc.Fields["Icon"].Get<std::string>() == folder + "/hero.png");
        Entity n = loaded->FindEntityByName("Player/hero.png");
        CHECK(n && n.GetComponent<TagsComponent>().Has("Player/hero.png"));
    };

    auto result = AssetFiles::Rename(assets / "Player", "Hero", assets);
    CHECK(result.Errors.empty() && result.UpdatedReferences == 2); // the sprite and the Icon field
    check("Hero");
    fs::create_directories(assets / "Characters");
    result = AssetFiles::MoveInto({ assets / "Hero" }, assets / "Characters", assets);
    CHECK(result.Errors.empty() && result.UpdatedReferences == 2);
    check("Characters/Hero");

    std::error_code ec;
    fs::remove_all(project, ec);
}

TEST(AssetImportFromOutside)
{
    namespace fs = std::filesystem;
    fs::path outside = MakeTempDir("zamin-import-src-");
    fs::path assets = MakeTempDir("zamin-import-dst-") / "Assets";
    WriteFile(outside / "hero.png", "png");
    WriteFile(outside / "Robot" / "Robot.gltf",
              "{\"buffers\": [{\"uri\": \"Robot.bin\"}], \"images\": [{\"uri\": \"tex/skin%20a.png\"}, {\"uri\": \"../shared.png\"}]}");
    WriteFile(outside / "Robot" / "Robot.bin", "bin");
    WriteFile(outside / "Robot" / "tex" / "skin a.png", "png");
    WriteFile(outside / "shared.png", "png");
    WriteFile(outside / "Pack" / "a.wav", "wav");
    WriteFile(outside / "Pack" / "Sub" / "b.wav", "wav");

    // Plain files: copied under a free name, never overwritten.
    auto result = AssetFiles::Import({ outside / "hero.png" }, assets / "Textures");
    CHECK(result.Errors.empty() && result.Imported.size() == 1 && fs::exists(assets / "Textures" / "hero.png"));
    result = AssetFiles::Import({ outside / "hero.png" }, assets / "Textures");
    CHECK(result.Imported.size() == 1 && result.Imported[0].filename() == "hero_1.png");
    CHECK(fs::exists(outside / "hero.png")); // the original stays

    // A glTF with separate files: its own folder, the files next to it, the uri still valid.
    result = AssetFiles::Import({ outside / "Robot" / "Robot.gltf" }, assets / "Models");
    CHECK(result.Errors.empty() && result.Imported.size() == 1);
    fs::path model = result.Imported.empty() ? fs::path() : result.Imported[0];
    CHECK(model.filename() == "Robot.gltf" && model.parent_path().filename() == "Robot");
    CHECK(fs::exists(assets / "Models" / "Robot" / "Robot.bin"));
    CHECK(fs::exists(assets / "Models" / "Robot" / "tex" / "skin a.png"));
    CHECK(fs::exists(assets / "Models" / "Robot" / "shared.png"));
    std::string gltf = FileSystem::ReadTextFile(model);
    CHECK(gltf.find("\"uri\": \"shared.png\"") != std::string::npos && gltf.find("tex/skin%20a.png") != std::string::npos);
    CHECK(AssetFiles::GetModelDependencies(model).size() == 3);
    result = AssetFiles::Import({ outside / "Robot" / "Robot.gltf" }, assets / "Models");
    CHECK(result.Imported.size() == 1 && result.Imported[0].parent_path().filename() == "Robot_1");

    // Folders are copied with everything inside; a folder cannot go into itself; missing files fail.
    result = AssetFiles::Import({ outside / "Pack", outside / "nothing.png" }, assets);
    CHECK(result.Imported.size() == 1 && result.Errors.size() == 1);
    CHECK(fs::exists(assets / "Pack" / "Sub" / "b.wav"));
    result = AssetFiles::Import({ assets / "Pack" }, assets / "Pack" / "Sub");
    CHECK(result.Imported.empty() && result.Errors.size() == 1);

    std::error_code ec;
    fs::remove_all(outside, ec);
    fs::remove_all(assets.parent_path(), ec);
}

// ------------------------------------------------------------------ asset registry, .zmeta, file watcher
TEST(AssetTypeDetection)
{
    CHECK(AssetTypes::FromPath("Textures/coin.PNG") == AssetType::Texture);
    CHECK(AssetTypes::FromExtension("jpg") == AssetType::Texture && AssetTypes::FromExtension(".TGA") == AssetType::Texture);
    CHECK(AssetTypes::FromPath("Models/Robot.glb") == AssetType::Model && AssetTypes::FromPath("m.obj") == AssetType::Model);
    CHECK(AssetTypes::FromPath("Audio/jump.wav") == AssetType::Audio && AssetTypes::FromPath("music.MP3") == AssetType::Audio);
    CHECK(AssetTypes::FromPath("Scenes/L1.zscene") == AssetType::Scene && AssetTypes::FromPath("Ball.zprefab") == AssetType::Prefab);
    CHECK(AssetTypes::FromPath("Fonts/Inter.otf") == AssetType::Font && AssetTypes::FromPath("a.ttf") == AssetType::Font);
    CHECK(AssetTypes::FromPath("Sky/sunset.hdr") == AssetType::Environment);
    CHECK(AssetTypes::FromPath("Materials/Metal.zmat") == AssetType::Material);
    CHECK(AssetTypes::FromPath("Source/Player.cpp") == AssetType::Script);
    CHECK(AssetTypes::FromPath("Game.zproject") == AssetType::Project);
    CHECK(AssetTypes::FromPath("Robot.bin") == AssetType::Other && AssetTypes::FromPath("README") == AssetType::Other);
    CHECK(AssetTypes::FromPath("coin.png.zmeta") == AssetType::Other);
    CHECK(AssetTypes::FromPath("Textures", true) == AssetType::Folder && AssetTypes::FromExtension("") == AssetType::Other);
    CHECK(AssetFiles::GetType("Metal.zmat") == AssetType::Material); // the content browser uses the same detection

    // The registry keeps (and gives a .zmeta to) these types only.
    for (AssetType t : { AssetType::Scene, AssetType::Prefab, AssetType::Texture, AssetType::Model, AssetType::Audio,
                         AssetType::Font, AssetType::Environment, AssetType::Material })
        CHECK(AssetTypes::IsRegistered(t));
    for (AssetType t : { AssetType::Folder, AssetType::Script, AssetType::Project, AssetType::Other })
        CHECK(!AssetTypes::IsRegistered(t));
    for (int t = 0; t < (int)AssetType::Count; t++)
    {
        for (std::string_view ext : AssetTypes::GetExtensions((AssetType)t))
            CHECK(AssetTypes::FromExtension(ext) == (AssetType)t);
        CHECK(AssetTypes::FromKey(AssetTypes::GetKey((AssetType)t)) == (AssetType)t);
    }
    CHECK(std::string(AssetTypes::GetKey(AssetType::Material)) == "Material");
    CHECK(std::string(AssetTypes::GetImportFolder(AssetType::Material)) == "Materials");
}

TEST(AssetHandleText)
{
    AssetHandle none;
    CHECK(!none.IsValid() && !none && none.ToString().empty());
    AssetHandle h(0x0123456789abcdefull);
    CHECK(h.ToString() == "0123456789abcdef");
    CHECK(AssetHandle::FromString("0123456789ABCDEF") == h && AssetHandle::FromString("0x123456789abcdef") == h);
    CHECK(AssetHandle::FromString("ff") == AssetHandle(255));
    CHECK(!AssetHandle::FromString("") && !AssetHandle::FromString("0") && !AssetHandle::FromString("xyz"));
    CHECK(!AssetHandle::FromString("12345678901234567") && !AssetHandle::FromString("Textures/a.png"));
    std::set<uint64_t> seen;
    for (int i = 0; i < 1000; i++)
    {
        AssetHandle g = AssetHandle::Generate();
        CHECK(g.IsValid() && AssetHandle::FromString(g.ToString()) == g);
        seen.insert(g.GetValue());
    }
    CHECK(seen.size() == 1000);
    std::unordered_map<AssetHandle, int> byHandle;
    byHandle[h] = 1;
    CHECK(byHandle.count(AssetHandle(0x0123456789abcdefull)) == 1 && byHandle.count(AssetHandle(1)) == 0);
}

TEST(AssetMetaFileRoundTrip)
{
    namespace fs = std::filesystem;
    CHECK(AssetMetaFile::IsMetaFile("Textures/coin.png.zmeta") && AssetMetaFile::IsMetaFile("a.ZMETA"));
    CHECK(!AssetMetaFile::IsMetaFile("coin.png") && !AssetMetaFile::IsMetaFile("zmeta"));
    CHECK(AssetMetaFile::GetMetaPath("Textures/coin.png") == fs::path("Textures/coin.png.zmeta"));
    CHECK(AssetMetaFile::GetAssetPath("Textures/coin.png.zmeta") == fs::path("Textures/coin.png"));

    AssetMetadata meta;
    meta.Handle = AssetHandle(0xabcdef0012345678ull);
    meta.Type = AssetType::Texture;
    meta.Import.SetString("Filter", "Nearest");
    meta.Import.SetBool("Mipmaps", false);
    meta.Import.SetNumber("Scale", 2.0);
    meta.Import.SetNumber("Gamma", 2.2);
    meta.Import.Set("Platforms", AssetImportSettings::Json{ "{\"Web\":{\"MaxSize\":512}}" });
    std::string text = AssetMetaFile::Serialize(meta);
    CHECK(text.find("\"UUID\": \"abcdef0012345678\"") != std::string::npos && text.find("\"Type\": \"Texture\"") != std::string::npos);
    CHECK(text.find("\"UUID\"") < text.find("\"Type\"") && text.find("\"Type\"") < text.find("\"Import\""));
    CHECK(text.find("\"Scale\": 2") != std::string::npos && text.find("\"Scale\": 2.0") == std::string::npos); // whole stays whole

    AssetMetadata back;
    CHECK(AssetMetaFile::Deserialize(text, back));
    CHECK(back.Handle == meta.Handle && back.Type == AssetType::Texture && back.Import == meta.Import);
    CHECK(back.Import.GetString("Filter") == "Nearest" && !back.Import.GetBool("Mipmaps", true));
    CHECK_NEAR(back.Import.GetFloat("Gamma", 0.0f), 2.2f, 1e-5f);
    // Missing values and values of another kind fall back.
    CHECK(back.Import.GetBool("Filter", true) && back.Import.GetNumber("Missing", 7.0) == 7.0 && back.Import.GetString("Scale", "x") == "x");
    CHECK(back.Import.Remove("Gamma") && !back.Import.Has("Gamma") && !back.Import.Remove("Gamma"));

    // A number as UUID and an unknown type are fine; no UUID, a bad one or broken JSON are not.
    AssetMetadata other;
    CHECK(AssetMetaFile::Deserialize("{\"UUID\": 255, \"Type\": \"Nope\"}", other) && other.Handle == AssetHandle(255));
    CHECK(other.Import.IsEmpty());
    std::string error;
    CHECK(!AssetMetaFile::Deserialize("{\"Type\": \"Texture\"}", other, &error) && !error.empty());
    CHECK(!AssetMetaFile::Deserialize("{\"UUID\": \"zz\"}", other, &error) && !error.empty());
    CHECK(!AssetMetaFile::Deserialize("<<<<<<< HEAD\n", other, &error) && !error.empty());

    // On disk, beside the asset.
    fs::path dir = MakeTempDir("zamin-meta-");
    WriteFile(dir / "coin.png", "png");
    CHECK(!AssetMetaFile::Read(dir / "coin.png", &error) && error.empty()); // missing: no error
    CHECK(AssetMetaFile::Write(dir / "coin.png", meta) && fs::exists(dir / "coin.png.zmeta"));
    auto read = AssetMetaFile::Read(dir / "coin.png");
    CHECK(read && read->Handle == meta.Handle && read->HasMetaFile && read->Import == meta.Import && read->Type == AssetType::Texture);
    // The same content is not written again (no needless changes for version control and the file watcher).
    auto past = fs::last_write_time(dir / "coin.png.zmeta") - std::chrono::hours(1);
    fs::last_write_time(dir / "coin.png.zmeta", past);
    CHECK(AssetMetaFile::Write(dir / "coin.png", meta) && fs::last_write_time(dir / "coin.png.zmeta") == past);
    meta.Import.SetBool("Mipmaps", true);
    CHECK(AssetMetaFile::Write(dir / "coin.png", meta) && fs::last_write_time(dir / "coin.png.zmeta") != past);
    WriteFile(dir / "coin.png.zmeta", "{ broken");
    CHECK(!AssetMetaFile::Read(dir / "coin.png", &error) && !error.empty());
    CHECK(!AssetMetaFile::Write(dir / "coin.png", AssetMetadata{})); // no handle: nothing written

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AssetRegistryScanAndLookup)
{
    namespace fs = std::filesystem;
    fs::path root = MakeTempDir("zamin-registry-");
    fs::path assets = root / "Assets";
    WriteFile(assets / "Textures" / "coin.png", "png");
    WriteFile(assets / "Textures" / "wall.jpg", "jpg");
    WriteFile(assets / "Models" / "Robot.gltf", "{}");
    WriteFile(assets / "Models" / "Robot.bin", "bin"); // a model's buffer: not an asset of its own
    WriteFile(assets / "Scenes" / "Level1.zscene", "{}");
    WriteFile(assets / "Audio" / "jump.wav", "wav");
    WriteFile(assets / "notes.txt", "x");
    WriteFile(assets / ".cache" / "junk.png", "x");    // hidden: skipped
    const char* files[] = { "Textures/coin.png", "Textures/wall.jpg", "Models/Robot.gltf", "Scenes/Level1.zscene", "Audio/jump.wav" };

    // The player only reads: every asset gets a handle, nothing is written.
    AssetRegistry player(false);
    AssetRegistry::ScanResult scan = player.Scan(assets);
    CHECK(scan.Assets == 5 && scan.CreatedMeta == 0 && player.IsScanned());
    CHECK(!fs::exists(assets / "Textures" / "coin.png.zmeta"));
    const AssetMetadata* coin = player.Find(fs::path("Textures/coin.png"));
    CHECK(coin && coin->Handle.IsValid() && !coin->HasMetaFile && coin->Type == AssetType::Texture);

    // The editor writes the missing .zmeta files.
    AssetRegistry editor(true);
    scan = editor.Scan(assets);
    CHECK(scan.Assets == 5 && scan.CreatedMeta == 5 && scan.Problems.empty());
    for (const char* f : files)
        CHECK(fs::exists(assets / (std::string(f) + ".zmeta")));
    CHECK(!fs::exists(assets / "Models" / "Robot.bin.zmeta") && !fs::exists(assets / "notes.txt.zmeta"));
    CHECK(!fs::exists(assets / ".cache" / "junk.png.zmeta"));

    // Lookups both ways, by relative or absolute path.
    AssetHandle coinHandle = editor.GetHandle("Textures/coin.png");
    CHECK(coinHandle.IsValid() && editor.GetPath(coinHandle) == "Textures/coin.png");
    CHECK(editor.GetHandle(assets / "Textures" / "coin.png") == coinHandle);
    CHECK(editor.GetHandle("Textures/../Textures/coin.png") == coinHandle);
    CHECK(editor.Find(coinHandle) && editor.Find(coinHandle)->HasMetaFile && editor.Find(coinHandle)->Type == AssetType::Texture);
    CHECK(!editor.GetHandle("Textures/missing.png") && editor.GetPath(AssetHandle(42)).empty() && !editor.Contains(AssetHandle(42)));
    CHECK(!editor.Find(fs::path("/elsewhere/coin.png")) && editor.ToRelativePath("/elsewhere/coin.png").empty());
    CHECK(editor.ToRelativePath(assets / "Textures" / "coin.png") == "Textures/coin.png");
    CHECK(editor.ToFileSystemPath("Textures/coin.png") == assets / "Textures" / "coin.png");
    CHECK(editor.GetAssets(AssetType::Texture).size() == 2 && editor.GetAssets().size() == 5);
    CHECK(editor.GetAssets().front()->RelativePath == "Audio/jump.wav"); // sorted by path

    // The handles come from the .zmeta files: the same in the player, and after scanning again.
    AssetRegistry again(false);
    again.Scan(assets);
    CHECK(again.GetHandle("Textures/coin.png") == coinHandle && again.Find(coinHandle)->HasMetaFile && again.GetCount() == 5);
    scan = editor.Scan(assets);
    CHECK(scan.CreatedMeta == 0 && editor.GetHandle("Textures/coin.png") == coinHandle);

    // The content browser does not list .zmeta files.
    for (const AssetEntry& entry : AssetFiles::List(assets / "Textures", {}))
        CHECK(!AssetMetaFile::IsMetaFile(entry.Path));
    CHECK(AssetFiles::List(assets / "Textures", {}).size() == 2);

    // A file copied together with its .zmeta gets an identity of its own (the older .zmeta keeps
    // the UUID); a wrong type is corrected; an unreadable .zmeta is left alone (a session handle);
    // .zmeta files without an asset are counted.
    fs::copy_file(assets / "Textures" / "coin.png", assets / "Textures" / "coin_copy.png");
    fs::copy_file(assets / "Textures" / "coin.png.zmeta", assets / "Textures" / "coin_copy.png.zmeta");
    fs::last_write_time(assets / "Textures" / "coin_copy.png.zmeta",
                        fs::last_write_time(assets / "Textures" / "coin.png.zmeta") + std::chrono::hours(1));
    AssetMetadata wall = *editor.Find(fs::path("Textures/wall.jpg"));
    wall.Type = AssetType::Audio;
    AssetMetaFile::Write(assets / "Textures" / "wall.jpg", wall);
    WriteFile(assets / "Audio" / "jump.wav.zmeta", "<<<<<<< HEAD\n");
    WriteFile(assets / "Textures" / "gone.png.zmeta", AssetMetaFile::Serialize(wall));
    scan = editor.Scan(assets);
    CHECK(scan.Assets == 6 && scan.RepairedMeta == 2 && scan.BrokenMeta == 1 && scan.OrphanMeta == 1 && scan.Problems.size() == 2);
    CHECK(editor.GetHandle("Textures/coin.png") == coinHandle);
    AssetHandle copyHandle = editor.GetHandle("Textures/coin_copy.png");
    CHECK(copyHandle.IsValid() && copyHandle != coinHandle);
    CHECK(AssetMetaFile::Read(assets / "Textures" / "coin_copy.png")->Handle == copyHandle);
    CHECK(AssetMetaFile::Read(assets / "Textures" / "wall.jpg")->Type == AssetType::Texture);
    CHECK(editor.GetHandle("Textures/wall.jpg") == wall.Handle);
    CHECK(FileSystem::ReadTextFile(assets / "Audio" / "jump.wav.zmeta").starts_with("<<<<<<<")); // not overwritten
    CHECK(editor.Find(fs::path("Audio/jump.wav")) && !editor.Find(fs::path("Audio/jump.wav"))->HasMetaFile);

    // A missing folder: nothing, no crash.
    CHECK(AssetRegistry(true).Scan(root / "Nothing").Assets == 0);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(AssetRegistryFollowsFileChanges)
{
    namespace fs = std::filesystem;
    fs::path root = MakeTempDir("zamin-registry-changes-");
    fs::path assets = root / "Assets";
    WriteFile(assets / "Textures" / "coin.png", "png");
    WriteFile(assets / "Textures" / "Ui" / "button.png", "png");
    AssetRegistry registry(true);
    registry.Scan(assets);
    AssetHandle coin = registry.GetHandle("Textures/coin.png"), button = registry.GetHandle("Textures/Ui/button.png");
    CHECK(coin && button && coin != button);

    // A new file is registered (and gets a .zmeta); other files are not.
    WriteFile(assets / "Audio" / "hit.wav", "wav");
    CHECK(registry.OnFileAdded(assets / "Audio" / "hit.wav") == 1);
    CHECK(registry.GetHandle("Audio/hit.wav").IsValid() && fs::exists(assets / "Audio" / "hit.wav.zmeta"));
    CHECK(registry.OnFileAdded(assets / "Audio" / "hit.wav") == 0); // a repeated event
    WriteFile(assets / "readme.txt", "x");
    CHECK(registry.OnFileAdded(assets / "readme.txt") == 0 && registry.GetCount() == 3);
    // A new folder: everything inside it.
    WriteFile(assets / "Fonts" / "a.ttf", "ttf");
    WriteFile(assets / "Fonts" / "Bold" / "b.otf", "otf");
    CHECK(registry.OnFileAdded(assets / "Fonts") == 2 && registry.GetCount() == 5);

    // Renamed / moved in the content browser: the .zmeta goes along, the handle stays.
    CHECK(AssetFiles::Rename(assets / "Textures" / "coin.png", "gold.png", assets).Moved.size() == 1);
    CHECK(fs::exists(assets / "Textures" / "gold.png.zmeta") && !fs::exists(assets / "Textures" / "coin.png.zmeta"));
    CHECK(registry.OnFileMoved(assets / "Textures" / "coin.png", assets / "Textures" / "gold.png") == 1);
    CHECK(registry.GetHandle("Textures/gold.png") == coin && !registry.GetHandle("Textures/coin.png"));
    auto moved = AssetFiles::MoveInto({ assets / "Textures" / "Ui" }, assets, assets);
    CHECK(moved.Moved.size() == 1 && fs::exists(assets / "Ui" / "button.png.zmeta"));
    CHECK(registry.OnFileMoved(assets / "Textures" / "Ui", assets / "Ui") == 1 && registry.GetPath(button) == "Ui/button.png");
    WriteFile(assets / "Audio" / "Sfx" / ".keep", "");
    moved = AssetFiles::MoveInto({ assets / "Audio" / "hit.wav" }, assets / "Audio" / "Sfx", assets);
    CHECK(moved.Moved.size() == 1 && fs::exists(assets / "Audio" / "Sfx" / "hit.wav.zmeta") && !fs::exists(assets / "Audio" / "hit.wav.zmeta"));

    // Moved outside the editor together with its .zmeta (the watcher may report the new place
    // first): the handle follows the .zmeta.
    fs::rename(assets / "Ui" / "button.png", assets / "Textures" / "button.png");
    fs::rename(assets / "Ui" / "button.png.zmeta", assets / "Textures" / "button.png.zmeta");
    CHECK(registry.OnFileAdded(assets / "Textures" / "button.png") == 1);
    CHECK(registry.GetPath(button) == "Textures/button.png");
    CHECK(registry.OnFileRemoved(assets / "Ui" / "button.png") == 0); // already followed

    // A copy with its .zmeta (the original still there) gets a new identity.
    fs::copy_file(assets / "Textures" / "gold.png", assets / "Textures" / "gold2.png");
    fs::copy_file(assets / "Textures" / "gold.png.zmeta", assets / "Textures" / "gold2.png.zmeta");
    CHECK(registry.OnFileAdded(assets / "Textures" / "gold2.png") == 1);
    AssetHandle gold2 = registry.GetHandle("Textures/gold2.png");
    CHECK(gold2.IsValid() && gold2 != coin && registry.GetHandle("Textures/gold.png") == coin);
    CHECK(AssetMetaFile::Read(assets / "Textures" / "gold2.png")->Handle == gold2);
    CHECK(registry.TakeProblems().size() == 1 && registry.TakeProblems().empty());

    // Deleted: a file (its .zmeta too), a folder (everything below it).
    CHECK(AssetFiles::Remove(assets / "Textures" / "gold2.png") && !fs::exists(assets / "Textures" / "gold2.png.zmeta"));
    CHECK(registry.OnFileRemoved(assets / "Textures" / "gold2.png") == 1 && !registry.Find(gold2));
    CHECK(AssetFiles::Remove(assets / "Fonts") && !fs::exists(assets / "Fonts"));
    CHECK(registry.OnFileRemoved(assets / "Fonts") == 2 && registry.GetCount() == 3);
    CHECK(!AssetFiles::Remove(assets / "Fonts"));

    // The .zmeta changed on disk: new import settings are reported, a new UUID (from version
    // control) is adopted, a deleted .zmeta is written again.
    fs::path goldMeta = assets / "Textures" / "gold.png.zmeta";
    AssetMetadata edited = *registry.Find(coin);
    edited.Import.SetString("Filter", "Nearest");
    AssetMetaFile::Write(assets / "Textures" / "gold.png", edited);
    const AssetMetadata* changed = registry.OnMetaFileChanged(goldMeta);
    CHECK(changed && changed->Handle == coin && changed->Import.GetString("Filter") == "Nearest");
    CHECK(!registry.OnMetaFileChanged(goldMeta)); // nothing new
    edited.Handle = AssetHandle(0x1234);
    AssetMetaFile::Write(assets / "Textures" / "gold.png", edited);
    CHECK(!registry.OnMetaFileChanged(goldMeta));
    CHECK(registry.GetHandle("Textures/gold.png") == AssetHandle(0x1234) && !registry.Find(coin));
    fs::remove(goldMeta);
    registry.OnMetaFileChanged(goldMeta);
    CHECK(AssetMetaFile::Read(assets / "Textures" / "gold.png") && AssetMetaFile::Read(assets / "Textures" / "gold.png")->Handle == AssetHandle(0x1234));
    WriteFile(goldMeta, "{ broken");
    CHECK(!registry.OnMetaFileChanged(goldMeta) && registry.GetHandle("Textures/gold.png") == AssetHandle(0x1234));

    // Import settings set in the editor are saved to the .zmeta.
    AssetImportSettings settings;
    settings.SetBool("Mipmaps", false);
    CHECK(registry.SetImportSettings(AssetHandle(0x1234), settings));
    CHECK(AssetMetaFile::Read(assets / "Textures" / "gold.png")->Import == settings);
    CHECK(!registry.SetImportSettings(AssetHandle(99), settings));

    // The player never writes, not even for a new file.
    AssetRegistry player(false);
    player.Scan(assets);
    WriteFile(assets / "new.png", "png");
    CHECK(player.OnFileAdded(assets / "new.png") == 1 && !fs::exists(assets / "new.png.zmeta"));
    CHECK(player.Find(fs::path("new.png")) && !player.Find(fs::path("new.png"))->HasMetaFile);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(AssetHotReloadAppliesChanges)
{
    namespace fs = std::filesystem;
    fs::path root = MakeTempDir("zamin-hotreload-");
    fs::path assets = root / "Assets";
    WriteFile(assets / "Textures" / "coin.png", "png");
    AssetRegistry registry(true);
    registry.Scan(assets);
    AssetHotReload hotReload;

    WriteFile(assets / "Textures" / "new.png", "png");
    auto report = hotReload.Apply({ { assets / "Textures" / "new.png", FileChangeType::Added, false } }, &registry);
    CHECK(report.Added.size() == 1 && report.HasStructureChanges());
    CHECK(registry.GetHandle("Textures/new.png").IsValid() && fs::exists(assets / "Textures" / "new.png.zmeta"));
    CHECK(report.Reloaded.empty() && report.Failed.empty()); // nothing loaded uses it (headless)

    // .zmeta files are not asset changes of their own.
    report = hotReload.Apply({ { assets / "Textures" / "new.png.zmeta", FileChangeType::Added, false } }, &registry);
    CHECK(report.IsEmpty());
    // A changed file the registry did not know yet (a missed event) is registered.
    WriteFile(assets / "Audio" / "hit.wav", "wav");
    report = hotReload.Apply({ { assets / "Audio" / "hit.wav", FileChangeType::Modified, false } }, &registry);
    CHECK(report.Modified.size() == 1 && !report.HasStructureChanges() && registry.GetHandle("Audio/hit.wav").IsValid());
    report = hotReload.Apply({ { assets / "Textures" / "coin.png", FileChangeType::Removed, false },
                                     { assets / "Audio", FileChangeType::Removed, true } }, &registry);
    CHECK(report.Removed.size() == 2 && !registry.Find(fs::path("Textures/coin.png")) && !registry.Find(fs::path("Audio/hit.wav")));
    CHECK(registry.GetCount() == 1);
    // No registry (no project): only reloading.
    CHECK(hotReload.Apply({ { assets / "Textures" / "new.png", FileChangeType::Modified, false } }, nullptr).Modified.size() == 1);
    CHECK(!hotReload.IsRunning() && hotReload.Update(&registry).IsEmpty()); // no watcher: nothing

    CHECK(AssetHotReload::CanReload("a.png") && AssetHotReload::CanReload("f.TTF") && !AssetHotReload::CanReload("s.zscene"));
    CHECK(!AssetHotReload::IsLoaded(assets / "Textures" / "new.png") && AssetHotReload::ReloadAsset(assets / "Textures" / "new.png") == 0);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(ProjectAssetRegistry)
{
    namespace fs = std::filesystem;
    Project::SetActive(nullptr);
    CHECK(Project::GetAssetRegistry() == nullptr); // nothing without a project

    fs::path dir = MakeTempDir("zamin-project-assets-");
    CHECK(Project::CreateFromTemplate(dir, "AssetGame") != nullptr);
    WriteFile(dir / "Assets" / "Textures" / "hero.png", "png");
    CHECK(Project::Load(dir / "AssetGame.zproject") != nullptr);

    // Like the player: scanned on first use, nothing written.
    bool writes = AssetRegistry::GetWriteMetaFilesByDefault();
    AssetRegistry::SetWriteMetaFilesByDefault(false);
    AssetRegistry* registry = Project::GetAssetRegistry();
    CHECK(registry && registry->IsScanned() && registry->GetCount() == 2); // Scenes/Main.zscene + hero.png
    CHECK(registry->GetAssetDirectory() == AssetRegistry::NormalizeDirectory(Project::GetAssetDirectory()));
    CHECK(registry->Find(fs::path("Scenes/Main.zscene")) && registry->Find(fs::path("Textures/hero.png")));
    CHECK(!fs::exists(dir / "Assets" / "Textures" / "hero.png.zmeta"));
    CHECK(Project::GetAssetRegistry() == registry); // the same registry, not scanned again

    // Like the editor: a project opened with writing on gets its .zmeta files.
    AssetRegistry::SetWriteMetaFilesByDefault(true);
    CHECK(Project::Load(dir / "AssetGame.zproject") != nullptr);
    registry = Project::GetAssetRegistry();
    CHECK(registry && registry->GetLastScan().CreatedMeta == 2 && fs::exists(dir / "Assets" / "Textures" / "hero.png.zmeta"));
    AssetHandle hero = registry->GetHandle("Textures/hero.png");
    CHECK(Project::Load(dir / "AssetGame.zproject") && Project::GetAssetRegistry()->GetHandle("Textures/hero.png") == hero);
    AssetRegistry::SetWriteMetaFilesByDefault(writes);

    Project::SetActive(nullptr);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AssetRegistrySnapshots)
{
    namespace fs = std::filesystem;
    fs::path root = MakeTempDir("zamin-registry-snapshot-");
    fs::path assets = root / "Assets";
    WriteFile(assets / "Textures" / "coin.png", "coin-png");
    WriteFile(assets / "Textures" / "wall.jpg", "wall-jpeg-data");
    WriteFile(assets / "Models" / "Robot.gltf", "{}");
    WriteFile(assets / "Scenes" / "Level1.zscene", "{}");

    AssetRegistry editor(true);
    editor.Scan(assets);
    AssetHandle coin = editor.GetHandle("Textures/coin.png"), wall = editor.GetHandle("Textures/wall.jpg");
    AssetImportSettings nearest;
    nearest.SetString("Filter", "Nearest");
    nearest.SetBool("Mipmaps", false);
    nearest.SetNumber("Scale", 0.5);
    nearest.Set("Future", AssetImportSettings::Json{ "[1,2]" });
    uint64_t revision = editor.GetRevision();
    CHECK(editor.SetImportSettings(coin, nearest) && editor.GetRevision() != revision);

    // The registry as one document and back: sorted by path, every field kept, the same text again.
    std::string text = editor.Serialize();
    AssetRegistry::Snapshot snapshot;
    std::string error = "x";
    CHECK(AssetRegistry::Deserialize(text, snapshot, &error) && error.empty() && snapshot.size() == 4);
    CHECK(snapshot == editor.GetSnapshot());
    CHECK(snapshot.size() == 4 && snapshot[0].Path == "Models/Robot.gltf" && snapshot[1].Path == "Scenes/Level1.zscene");
    if (snapshot.size() == 4)
    {
        const AssetRegistry::SnapshotEntry& c = snapshot[2];
        CHECK(c.Path == "Textures/coin.png" && c.Handle == coin && c.Type == AssetType::Texture && c.HasMetaFile);
        CHECK(c.Size == 8 && c.Time != 0 && c.Import == nearest && snapshot[3].Import.IsEmpty() && snapshot[1].Type == AssetType::Scene);
    }
    CHECK(AssetRegistry::SerializeSnapshot(snapshot) == text);
    CHECK(text.find("\"Version\": 1") != std::string::npos && text.find("\"Future\"") != std::string::npos);

    // Text that is not a registry document; entries without a UUID / path or repeating one are skipped.
    CHECK(!AssetRegistry::Deserialize("not json", snapshot, &error) && !error.empty());
    CHECK(!AssetRegistry::Deserialize("{\"Assets\": []}", snapshot, &error) && !error.empty());
    CHECK(!AssetRegistry::Deserialize("{\"Version\": 99, \"Assets\": []}", snapshot, &error));
    CHECK(!AssetRegistry::Deserialize("{\"Version\": 1}", snapshot, &error));
    size_t skipped = 0;
    CHECK(AssetRegistry::Deserialize(R"({"Version": 1, "Assets": [
        {"UUID": "zz", "Path": "a.png"}, {"UUID": "00000000000000aa", "Path": "../x.png"},
        {"UUID": "00000000000000ab", "Path": "b.png", "Type": "Model", "Size": 5, "Time": 7, "Meta": true, "Import": {"sRGB": false}},
        {"UUID": "00000000000000ab", "Path": "c.png"}, {"UUID": "00000000000000ac", "Path": "./b.png"}, 5,
        {"UUID": "00000000000000ad", "Path": "Data/level.bin", "Type": "Model"}]})", snapshot, &error, &skipped));
    CHECK(snapshot.size() == 2 && skipped == 5);
    if (snapshot.size() == 2)
    {
        // The extension decides the type of a registered kind of file; others keep the stored one.
        CHECK(snapshot[0].Path == "b.png" && snapshot[0].Handle == AssetHandle(0xab) && snapshot[0].Type == AssetType::Texture);
        CHECK(snapshot[0].Size == 5 && snapshot[0].Time == 7 && snapshot[0].HasMetaFile && !snapshot[0].Import.GetBool("sRGB", true));
        CHECK(snapshot[1].Type == AssetType::Model);
    }

    // Saved to a file (not rewritten when it holds the same text) and read back.
    fs::path cache = root / ".zamin" / "AssetRegistry.json";
    CHECK(editor.SaveSnapshot(cache) && fs::exists(cache));
    auto savedTime = fs::last_write_time(cache);
    CHECK(editor.SaveSnapshot(cache) && fs::last_write_time(cache) == savedTime);
    std::optional<AssetRegistry::Snapshot> before = AssetRegistry::LoadSnapshot(cache, &error);
    CHECK(before && *before == editor.GetSnapshot());
    CHECK(!AssetRegistry::LoadSnapshot(root / "missing.json", &error) && error.empty());
    WriteFile(root / "broken.json", "{");
    CHECK(!AssetRegistry::LoadSnapshot(root / "broken.json", &error) && !error.empty());

    // A player loads it without reading (or writing) .zmeta files.
    fs::path wallMeta = assets / "Textures" / "wall.jpg.zmeta";
    fs::remove(wallMeta);
    AssetRegistry player(false);
    CHECK(before && player.Load(assets, *before) && player.IsScanned() && player.GetCount() == 4);
    CHECK(player.GetHandle("Textures/wall.jpg") == wall && player.Find(coin) && player.Find(coin)->Import == nearest);
    CHECK(player.GetAssetDirectory() == AssetRegistry::NormalizeDirectory(assets) && !fs::exists(wallMeta));
    // A file of the snapshot is missing: nothing loaded (the folder changed), unless not verified.
    AssetRegistry::Snapshot stale = before ? *before : AssetRegistry::Snapshot{};
    AssetRegistry::SnapshotEntry gone;
    gone.Handle = AssetHandle(77);
    gone.Type = AssetType::Texture;
    gone.Path = "Textures/gone.png";
    stale.push_back(gone);
    CHECK(!player.Load(assets, stale) && !player.IsScanned() && player.GetCount() == 0);
    CHECK(player.Load(assets, stale, false) && player.GetCount() == 5 && player.GetPath(AssetHandle(77)) == "Textures/gone.png");
    editor.OnMetaFileChanged(wallMeta); // deleted by hand: written again with the same UUID
    CHECK(fs::exists(wallMeta) && AssetMetaFile::Read(assets / "Textures" / "wall.jpg")->Handle == wall);

    // Moved while no registry watched (the editor was closed): with its .zmeta (the UUID is found at
    // the new path), and without it (same type, size and time as the asset that is gone; the .zmeta
    // left behind is removed).
    fs::create_directories(assets / "Items");
    fs::rename(assets / "Textures" / "coin.png", assets / "Items" / "gold.png");
    fs::rename(assets / "Textures" / "coin.png.zmeta", assets / "Items" / "gold.png.zmeta");
    fs::rename(assets / "Textures" / "wall.jpg", assets / "Textures" / "stone.jpg");
    {
        AssetRegistry blind(false); // without the snapshot: the .zmeta-less file is a stranger
        const AssetRegistry::ScanResult& scan = blind.Scan(assets);
        CHECK(blind.GetHandle("Items/gold.png") == coin && blind.GetHandle("Textures/stone.jpg") != wall);
        CHECK(scan.MovedOutside == 0 && scan.OrphanMeta == 1 && blind.TakeDetectedMoves().empty());
    }
    AssetRegistry reopened(true);
    const AssetRegistry::ScanResult& scan = reopened.Scan(assets, before ? &*before : nullptr);
    CHECK(scan.MovedOutside == 2 && scan.RecoveredMeta == 1 && scan.OrphanMeta == 0 && scan.CreatedMeta == 0);
    CHECK(reopened.GetHandle("Items/gold.png") == coin && reopened.GetHandle("Textures/stone.jpg") == wall);
    CHECK(reopened.Find(wall) && reopened.Find(wall)->HasMetaFile && reopened.Find(coin)->Import == nearest);
    CHECK(fs::exists(assets / "Textures" / "stone.jpg.zmeta") && !fs::exists(wallMeta));
    CHECK(AssetMetaFile::Read(assets / "Textures" / "stone.jpg")->Handle == wall);
    std::vector<AssetRegistry::PathMove> moves = reopened.TakeDetectedMoves();
    std::sort(moves.begin(), moves.end());
    CHECK(moves.size() == 2 && moves[0] == AssetRegistry::PathMove("Textures/coin.png", "Items/gold.png") &&
          moves[1] == AssetRegistry::PathMove("Textures/wall.jpg", "Textures/stone.jpg"));
    CHECK(reopened.TakeDetectedMoves().empty());

    // No guessing: two files that look the same renamed without their .zmeta, or a copy (the
    // original is still there) keep / get identities of their own.
    WriteFile(assets / "Audio" / "a.wav", "same");
    WriteFile(assets / "Audio" / "b.wav", "same");
    auto when = fs::last_write_time(assets / "Audio" / "a.wav");
    fs::last_write_time(assets / "Audio" / "b.wav", when);
    CHECK(reopened.OnFileAdded(assets / "Audio") == 2);
    AssetRegistry::Snapshot withAudio = reopened.GetSnapshot();
    fs::rename(assets / "Audio" / "a.wav", assets / "Audio" / "c.wav");
    fs::rename(assets / "Audio" / "b.wav", assets / "Audio" / "d.wav");
    fs::copy_file(assets / "Items" / "gold.png", assets / "Items" / "gold2.png");
    fs::last_write_time(assets / "Items" / "gold2.png", fs::last_write_time(assets / "Items" / "gold.png"));
    {
        AssetRegistry third(false);
        const AssetRegistry::ScanResult& again = third.Scan(assets, &withAudio);
        AssetHandle c = third.GetHandle("Audio/c.wav"), d = third.GetHandle("Audio/d.wav");
        CHECK(c && d && !reopened.Contains(c) && !reopened.Contains(d) && again.RecoveredMeta == 0 && again.MovedOutside == 0);
        CHECK(third.GetHandle("Items/gold2.png") && third.GetHandle("Items/gold2.png") != coin && third.GetHandle("Items/gold.png") == coin);
    }
    fs::remove(assets / "Items" / "gold2.png");

    // While the editor runs (file watcher events): renamed without its .zmeta, the asset keeps its
    // identity whichever event comes first; its old .zmeta goes away.
    AssetRegistry live(true);
    live.Scan(assets);
    CHECK(live.GetHandle("Textures/stone.jpg") == wall);
    fs::rename(assets / "Textures" / "stone.jpg", assets / "Textures" / "rock.jpg");
    CHECK(live.OnFileRemoved(assets / "Textures" / "stone.jpg") == 1);
    CHECK(live.OnFileAdded(assets / "Textures" / "rock.jpg") == 1 && live.GetHandle("Textures/rock.jpg") == wall);
    CHECK(fs::exists(assets / "Textures" / "rock.jpg.zmeta") && !fs::exists(assets / "Textures" / "stone.jpg.zmeta"));
    moves = live.TakeDetectedMoves();
    CHECK(moves.size() == 1 && moves[0] == AssetRegistry::PathMove("Textures/stone.jpg", "Textures/rock.jpg"));
    fs::rename(assets / "Textures" / "rock.jpg", assets / "Textures" / "pebble.jpg");
    CHECK(live.OnFileAdded(assets / "Textures" / "pebble.jpg") == 1 && live.GetHandle("Textures/pebble.jpg") == wall);
    CHECK(!live.Find(fs::path("Textures/rock.jpg")) && live.OnFileRemoved(assets / "Textures" / "rock.jpg") == 0);
    moves = live.TakeDetectedMoves();
    CHECK(moves.size() == 1 && moves[0] == AssetRegistry::PathMove("Textures/rock.jpg", "Textures/pebble.jpg"));
    // A rewritten file is followed (its new size), so a later rename is still recognized.
    WriteFile(assets / "Textures" / "pebble.jpg", "a bigger pebble");
    CHECK(live.OnFileModified(assets / "Textures" / "pebble.jpg") && live.Find(wall)->FileSize == 15);
    CHECK(!live.OnFileModified(assets / "Textures" / "nothing.jpg"));
    fs::rename(assets / "Textures" / "pebble.jpg", assets / "Textures" / "boulder.jpg");
    live.OnFileRemoved(assets / "Textures" / "pebble.jpg");
    CHECK(live.OnFileAdded(assets / "Textures" / "boulder.jpg") == 1 && live.GetHandle("Textures/boulder.jpg") == wall);
    // A brand new file is not mistaken for anything.
    WriteFile(assets / "Textures" / "new.png", "brand new");
    CHECK(live.OnFileAdded(assets / "Textures" / "new.png") == 1 && live.GetHandle("Textures/new.png") != wall);
    CHECK(live.TakeDetectedMoves().size() == 1); // boulder only

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(ProjectAssetRegistryCache)
{
    namespace fs = std::filesystem;
    fs::path dir = MakeTempDir("zamin-project-registry-cache-");
    CHECK(Project::CreateFromTemplate(dir, "CacheGame") != nullptr);
    CHECK(FileSystem::ReadTextFile(dir / ".gitignore").find(".zamin/") != std::string::npos);
    fs::path textures = dir / "Assets" / "Textures";
    WriteFile(textures / "hero.png", "hero-png");
    bool writes = AssetRegistry::GetWriteMetaFilesByDefault();

    // The editor: scanned, then the snapshot goes to the cache folder.
    AssetRegistry::SetWriteMetaFilesByDefault(true);
    CHECK(Project::Load(dir / "CacheGame.zproject") != nullptr);
    CHECK(Project::GetCacheDirectory() == fs::absolute(dir) / ".zamin");
    CHECK(Project::GetAssetRegistryCachePath() == Project::GetCacheDirectory() / "AssetRegistry.json");
    AssetRegistry* registry = Project::GetAssetRegistry();
    AssetHandle hero = registry ? registry->GetHandle("Textures/hero.png") : AssetHandle();
    CHECK(hero && Project::SaveAssetRegistryCache() && fs::exists(dir / ".zamin" / "AssetRegistry.json"));

    // Renamed without its .zmeta while the editor was closed: found again at the next open.
    fs::rename(textures / "hero.png", textures / "player.png");
    CHECK(Project::Load(dir / "CacheGame.zproject") != nullptr);
    registry = Project::GetAssetRegistry();
    CHECK(registry && registry->GetHandle("Textures/player.png") == hero && registry->GetLastScan().MovedOutside == 1);
    CHECK(fs::exists(textures / "player.png.zmeta") && !fs::exists(textures / "hero.png.zmeta"));
    std::vector<AssetRegistry::PathMove> moves = registry ? registry->TakeDetectedMoves() : std::vector<AssetRegistry::PathMove>{};
    CHECK(moves.size() == 1 && moves[0] == AssetRegistry::PathMove("Textures/hero.png", "Textures/player.png"));

    // An exported game reads the registry file next to its .zproject (no .zmeta needed) ...
    CHECK(registry && registry->SaveSnapshot(dir / Project::BuildAssetRegistryFile));
    for (const fs::path& meta : { textures / "player.png.zmeta", dir / "Assets" / "Scenes" / "Main.zscene.zmeta" })
        fs::remove(meta);
    AssetRegistry::SetWriteMetaFilesByDefault(false);
    CHECK(Project::Load(dir / "CacheGame.zproject") != nullptr);
    registry = Project::GetAssetRegistry();
    CHECK(registry && registry->GetCount() == 2 && registry->GetHandle("Textures/player.png") == hero);
    CHECK(registry && registry->Find(hero)->HasMetaFile && !fs::exists(textures / "player.png.zmeta"));
    // ... unless it is out of date: then the folder is read (session handles for files without .zmeta).
    fs::rename(textures / "player.png", textures / "player2.png");
    CHECK(Project::Load(dir / "CacheGame.zproject") != nullptr);
    registry = Project::GetAssetRegistry();
    CHECK(registry && registry->GetCount() == 2 && registry->Find(fs::path("Textures/player2.png")) && !fs::exists(textures / "player2.png.zmeta"));
    AssetRegistry::SetWriteMetaFilesByDefault(writes);

    Project::SetActive(nullptr);
    CHECK(Project::GetCacheDirectory().empty() && !Project::SaveAssetRegistryCache());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(FileChangeQueueDebounce)
{
    FileChangeQueue queue;
    queue.Push("/a/x.png", FileChangeType::Modified, false, 0.0);
    queue.Push("/a/x.png", FileChangeType::Modified, false, 0.2); // still being written
    queue.Push("/a/y.png", FileChangeType::Added, false, 0.1);
    CHECK(queue.Pop(0.25, 0.3).empty() && queue.GetPendingCount() == 2); // nothing settled yet
    auto out = queue.Pop(0.45, 0.3);
    CHECK(out.size() == 1 && out[0].Path == "/a/y.png" && out[0].Type == FileChangeType::Added);
    out = queue.Pop(0.6, 0.3);
    CHECK(out.size() == 1 && out[0].Path == "/a/x.png" && out[0].Type == FileChangeType::Modified);

    queue.Push("/a/s.zscene", FileChangeType::Removed, false, 1.0); // a safe save: removed, written again
    queue.Push("/a/s.zscene", FileChangeType::Added, false, 1.01);
    queue.Push("/a/tmp~", FileChangeType::Added, false, 1.0);       // came and went
    queue.Push("/a/tmp~", FileChangeType::Removed, false, 1.02);
    queue.Push("/a/n.png", FileChangeType::Added, false, 1.0);      // new, then written some more
    queue.Push("/a/n.png", FileChangeType::Modified, false, 1.03);
    queue.Push("/a/m.png", FileChangeType::Modified, false, 1.0);   // changed, then deleted
    queue.Push("/a/m.png", FileChangeType::Removed, false, 1.04);
    queue.Push("/a/Dir", FileChangeType::Added, true, 1.05);
    out = queue.Pop(2.0, 0.3);
    CHECK(out.size() == 4 && queue.GetPendingCount() == 0);
    if (out.size() == 4)
    {
        CHECK(out[0].Path == "/a/s.zscene" && out[0].Type == FileChangeType::Modified);
        CHECK(out[1].Path == "/a/n.png" && out[1].Type == FileChangeType::Added);
        CHECK(out[2].Path == "/a/m.png" && out[2].Type == FileChangeType::Removed);
        CHECK(out[3].Path == "/a/Dir" && out[3].IsDirectory);
    }
    queue.Push("/a/z", FileChangeType::Added, false, 3.0);
    queue.Clear();
    CHECK(queue.GetPendingCount() == 0 && queue.Pop(10.0, 0.0).empty());
}

static bool HasFileChange(const std::vector<FileChange>& changes, const std::filesystem::path& path, FileChangeType type)
{
    return std::any_of(changes.begin(), changes.end(), [&](const FileChange& c) { return c.Path == path && c.Type == type; });
}

// Polls like the editor does every frame until `done` or a timeout.
static std::vector<FileChange> WaitForFileChanges(FileWatcher& watcher, const std::function<bool(const std::vector<FileChange>&)>& done)
{
    std::vector<FileChange> all;
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5))
    {
        for (FileChange& change : watcher.Poll())
            all.push_back(std::move(change));
        if (done(all))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return all;
}

TEST(FileWatcherSeesChanges)
{
    namespace fs = std::filesystem;
    for (bool native : { true, false })
    {
        fs::path dir = MakeTempDir(native ? "zamin-watch-native-" : "zamin-watch-poll-");
        WriteFile(dir / "Textures" / "coin.png", "png");
        WriteFile(dir / "old.wav", "wav");
        FileWatcherOptions options;
        options.Debounce = 0.05;
        options.PollInterval = 0.05;
        options.NativeEvents = native;
        FileWatcher watcher;
        CHECK(watcher.Start(dir, options) && watcher.IsWatching() && watcher.GetDirectory() == dir);
#if defined(__linux__)
        CHECK(watcher.UsesNativeEvents() == native);
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CHECK(watcher.Poll().empty()); // nothing changed, nothing reported

        WriteFile(dir / "Textures" / "coin.png", "png, but bigger");
        WriteFile(dir / "Textures" / "new.png", "new");
        fs::remove(dir / "old.wav");
        WriteFile(dir / "Models" / "Robot" / "robot.gltf", "{}");
        WriteFile(dir / ".git" / "index", "x"); // hidden: never reported
        auto changes = WaitForFileChanges(watcher, [&](const std::vector<FileChange>& c) {
            return HasFileChange(c, dir / "Textures" / "coin.png", FileChangeType::Modified) &&
                   HasFileChange(c, dir / "Textures" / "new.png", FileChangeType::Added) &&
                   HasFileChange(c, dir / "old.wav", FileChangeType::Removed) &&
                   HasFileChange(c, dir / "Models" / "Robot" / "robot.gltf", FileChangeType::Added);
        });
        CHECK(HasFileChange(changes, dir / "Textures" / "coin.png", FileChangeType::Modified));
        CHECK(HasFileChange(changes, dir / "Textures" / "new.png", FileChangeType::Added));
        CHECK(HasFileChange(changes, dir / "old.wav", FileChangeType::Removed));
        CHECK(HasFileChange(changes, dir / "Models" / "Robot" / "robot.gltf", FileChangeType::Added));
        CHECK(std::any_of(changes.begin(), changes.end(), [&](const FileChange& c) { return c.Path == dir / "Models" && c.IsDirectory; }));
        CHECK(std::none_of(changes.begin(), changes.end(), [](const FileChange& c) { return c.Path.string().find(".git") != std::string::npos; }));
        // Each path once (several writes of one file come out as one change).
        std::set<std::string> unique;
        for (const FileChange& c : changes)
            unique.insert(c.Path.string());
        CHECK(unique.size() == changes.size());
        // Another file in the same folder later: the new file is not reported again.
        WriteFile(dir / "Textures" / "other.png", "other");
        changes = WaitForFileChanges(watcher, [&](const std::vector<FileChange>& c) {
            return HasFileChange(c, dir / "Textures" / "other.png", FileChangeType::Added);
        });
        CHECK(changes.size() == 1 && HasFileChange(changes, dir / "Textures" / "other.png", FileChangeType::Added));

        // A folder renamed: its files leave the old place and appear at the new one...
        fs::rename(dir / "Models", dir / "Meshes");
        changes = WaitForFileChanges(watcher, [&](const std::vector<FileChange>& c) {
            return HasFileChange(c, dir / "Models" / "Robot" / "robot.gltf", FileChangeType::Removed) &&
                   HasFileChange(c, dir / "Meshes" / "Robot" / "robot.gltf", FileChangeType::Added);
        });
        CHECK(HasFileChange(changes, dir / "Models" / "Robot" / "robot.gltf", FileChangeType::Removed));
        CHECK(HasFileChange(changes, dir / "Meshes" / "Robot" / "robot.gltf", FileChangeType::Added));
        // ... and what happens inside it later is still seen.
        WriteFile(dir / "Meshes" / "Robot" / "robot.gltf", "{\"asset\": {}}");
        changes = WaitForFileChanges(watcher, [&](const std::vector<FileChange>& c) {
            return HasFileChange(c, dir / "Meshes" / "Robot" / "robot.gltf", FileChangeType::Modified);
        });
        CHECK(HasFileChange(changes, dir / "Meshes" / "Robot" / "robot.gltf", FileChangeType::Modified));

        watcher.Stop();
        CHECK(!watcher.IsWatching() && watcher.Poll().empty());
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    FileWatcher missing;
    CHECK(!missing.Start("/definitely/not/a/folder") && !missing.IsWatching());
}

TEST(TextureImportSettingsInMeta)
{
    // Missing keys: the defaults (how textures always loaded).
    AssetImportSettings empty;
    CHECK(AssetImporter::ReadTextureSettings(empty) == TextureImportSettings{});
    TextureImportSettings defaults;
    CHECK(defaults.Filter == TextureFilter::Linear && defaults.Wrap == TextureWrap::Repeat && defaults.Mipmaps && defaults.SRGB);

    // Written next to keys of other tools, read back the same.
    AssetImportSettings import;
    import.SetString("Compression", "BC7"); // not ours: kept
    TextureImportSettings pixel{ TextureFilter::Nearest, TextureWrap::MirroredRepeat, false, false };
    AssetImporter::WriteTextureSettings(pixel, import);
    CHECK(import.GetString("Filter") == "Nearest" && import.GetString("Wrap") == "Mirror");
    CHECK(import.GetBool("Mipmaps", true) == false && import.GetBool("sRGB", true) == false);
    CHECK(import.GetString("Compression") == "BC7");
    CHECK(AssetImporter::ReadTextureSettings(import) == pixel);

    // Through a .zmeta file and back.
    AssetMetadata meta;
    meta.Handle = AssetHandle::Generate();
    meta.Type = AssetType::Texture;
    meta.Import = import;
    AssetMetadata read;
    CHECK(AssetMetaFile::Deserialize(AssetMetaFile::Serialize(meta), read));
    CHECK(AssetImporter::ReadTextureSettings(read.Import) == pixel && read.Import.GetString("Compression") == "BC7");

    // Hand-written spellings, wrong values.
    AssetImportSettings hand;
    hand.SetString("Filter", "point");
    hand.SetString("Wrap", "ClampToEdge");
    hand.SetString("sRGB", "yes"); // not a bool: default
    TextureImportSettings handRead = AssetImporter::ReadTextureSettings(hand);
    CHECK(handRead.Filter == TextureFilter::Nearest && handRead.Wrap == TextureWrap::ClampToEdge && handRead.SRGB);
    hand.SetString("Filter", "Blurry");
    hand.SetNumber("Wrap", 2.0);
    handRead = AssetImporter::ReadTextureSettings(hand);
    CHECK(handRead.Filter == TextureFilter::Linear && handRead.Wrap == TextureWrap::Repeat);

    for (TextureWrap wrap : { TextureWrap::Repeat, TextureWrap::ClampToEdge, TextureWrap::MirroredRepeat })
        CHECK(AssetImporter::WrapFromKey(AssetImporter::GetWrapKey(wrap)) == wrap);
    for (TextureFilter filter : { TextureFilter::Linear, TextureFilter::Nearest })
        CHECK(AssetImporter::FilterFromKey(AssetImporter::GetFilterKey(filter)) == filter);
}

TEST(ModelImportSettingsInMeta)
{
    CHECK(AssetImporter::ReadModelSettings(AssetImportSettings{}) == ModelImportSettings{});
    ModelImportSettings cm{ 0.01f, ModelUpAxis::Z, true };
    AssetImportSettings import;
    AssetImporter::WriteModelSettings(cm, import);
    CHECK(import.GetString("UpAxis") == "Z" && import.GetBool("FlipUVs", false));
    ModelImportSettings back = AssetImporter::ReadModelSettings(import);
    CHECK_NEAR(back.Scale, 0.01f, 1e-6f);
    CHECK(back.UpAxis == ModelUpAxis::Z && back.FlipUVs);

    // Scale: 0, negative or huge values are not taken as they are.
    import.SetNumber("Scale", 0.0);
    CHECK(AssetImporter::ReadModelSettings(import).Scale == 1.0f);
    import.SetNumber("Scale", -3.0);
    CHECK(AssetImporter::ReadModelSettings(import).Scale == 1.0f);
    import.SetNumber("Scale", 1e9);
    CHECK(AssetImporter::ReadModelSettings(import).Scale == AssetImporter::MaxModelScale);
    import.SetString("UpAxis", "z-up");
    CHECK(AssetImporter::ReadModelSettings(import).UpAxis == ModelUpAxis::Z);

    // The import transform: scale, then Z-up turned into Y-up.
    ModelImportSettings identity;
    glm::vec4 p = identity.GetTransform() * glm::vec4(1, 2, 3, 1);
    CHECK_NEAR(p.x, 1.0f, 1e-6f); CHECK_NEAR(p.y, 2.0f, 1e-6f); CHECK_NEAR(p.z, 3.0f, 1e-6f);
    ModelImportSettings zUp{ 2.0f, ModelUpAxis::Z, false };
    glm::vec4 up = zUp.GetTransform() * glm::vec4(0, 0, 1, 1);    // the file's up...
    CHECK_NEAR(up.x, 0.0f, 1e-5f); CHECK_NEAR(up.y, 2.0f, 1e-5f); CHECK_NEAR(up.z, 0.0f, 1e-5f); // ...is our up, scaled
    glm::vec4 back2 = zUp.GetTransform() * glm::vec4(0, 1, 0, 1); // the file's +Y (its "back") is -Z
    CHECK_NEAR(back2.y, 0.0f, 1e-5f); CHECK_NEAR(back2.z, -2.0f, 1e-5f);
}

TEST(ModelImportTransformSkinning)
{
    // A two-joint arm, its tip turned 90 degrees: skinning the transformed vertex with the
    // transformed skeleton gives the transformed result of the original.
    Skeleton sk;
    sk.Joints.resize(2);
    sk.Joints[1].Parent = 0;
    sk.Joints[1].RestTranslation = { 0, 1, 0 };
    sk.Joints[1].InverseBind = glm::translate(glm::mat4(1.0f), { 0, -1, 0 });
    sk.RootTransform = glm::translate(glm::mat4(1.0f), { 0.5f, 0, 0 });
    AnimationClip clip;
    clip.Duration = 1.0f;
    AnimationChannel ch;
    ch.JointIndex = 1;
    ch.Target = AnimationChannel::Path::Rotation;
    ch.Times = { 0.0f, 1.0f };
    glm::quat q = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    ch.Values = { glm::vec4(0, 0, 0, 1), glm::vec4(q.x, q.y, q.z, q.w) };
    clip.Channels.push_back(ch);

    std::vector<glm::mat4> before, after;
    EvaluatePose(sk, &clip, 0.7f, before);
    glm::mat4 t = ModelImportSettings{ 0.5f, ModelUpAxis::Z, false }.GetTransform();
    Skeleton imported = sk;
    ApplyImportTransform(imported, t);
    EvaluatePose(imported, &clip, 0.7f, after);
    glm::vec4 v(0.2f, 2.0f, 0.3f, 1.0f);
    glm::vec4 expected = t * (before[1] * v);
    glm::vec4 got = after[1] * (t * v);
    CHECK_NEAR(got.x, expected.x, 1e-4f); CHECK_NEAR(got.y, expected.y, 1e-4f); CHECK_NEAR(got.z, expected.z, 1e-4f);
    // Bind pose: the transformed vertex stays where the import put it.
    EvaluatePose(imported, nullptr, 0.0f, after);
    EvaluatePose(sk, nullptr, 0.0f, before);
    glm::vec4 bindExpected = t * (before[1] * v), bindGot = after[1] * (t * v);
    CHECK_NEAR(bindGot.y, bindExpected.y, 1e-4f); CHECK_NEAR(bindGot.z, bindExpected.z, 1e-4f);
}

TEST(ImportSettingsOfProjectAssets)
{
    namespace fs = std::filesystem;
    Project::SetActive(nullptr);
    CHECK(AssetImporter::GetTextureSettings("anything.png") == TextureImportSettings{}); // no project: defaults

    fs::path dir = MakeTempDir("zamin-import-settings-");
    CHECK(Project::CreateFromTemplate(dir, "ImportGame") != nullptr);
    WriteFile(dir / "Assets" / "Textures" / "tiles.png", "png");
    WriteFile(dir / "Assets" / "Models" / "ship.glb", "glb");
    CHECK(Project::Load(dir / "ImportGame.zproject") != nullptr);
    AssetRegistry* registry = Project::GetAssetRegistry();
    CHECK(registry != nullptr);
    if (!registry)
        return;

    // Settings set in the registry (as the asset inspector does) are what loading uses.
    AssetHandle tiles = registry->GetHandle("Textures/tiles.png");
    AssetImportSettings import = registry->Find(tiles)->Import;
    TextureImportSettings pixel{ TextureFilter::Nearest, TextureWrap::ClampToEdge, false, true };
    AssetImporter::WriteTextureSettings(pixel, import);
    CHECK(registry->SetImportSettings(tiles, import));
    fs::path tilesFile = Project::GetAssetFileSystemPath("Textures/tiles.png");
    CHECK(AssetImporter::GetTextureSettings(tilesFile) == pixel);
    CHECK(AssetImporter::GetTextureSettings("Textures/tiles.png") == pixel); // relative works too
    // The pixel-art copy of sprites / tilemaps: crisp, no mipmaps, the rest as imported.
    TextureImportSettings sprite = TextureLibrary::GetLoadSettings(tilesFile, true);
    CHECK(sprite.Filter == TextureFilter::Nearest && !sprite.Mipmaps && sprite.Wrap == TextureWrap::ClampToEdge);
    CHECK(TextureLibrary::GetLoadSettings(tilesFile, false) == pixel);
    CHECK(AssetImporter::GetTextureSettings(dir / "elsewhere.png") == TextureImportSettings{});

    AssetHandle ship = registry->GetHandle("Models/ship.glb");
    import = registry->Find(ship)->Import;
    AssetImporter::WriteModelSettings({ 0.01f, ModelUpAxis::Z, false }, import);
    registry->SetImportSettings(ship, import);
    ModelImportSettings shipSettings = AssetImporter::GetModelSettings(Project::GetAssetFileSystemPath("Models/ship.glb"));
    CHECK(shipSettings.UpAxis == ModelUpAxis::Z);
    CHECK_NEAR(shipSettings.Scale, 0.01f, 1e-6f);

    // Saved to the .zmeta when the registry writes them (the editor): a new session reads them.
    if (registry->GetWriteMetaFiles())
    {
        auto onDisk = AssetMetaFile::Read(tilesFile);
        CHECK(onDisk && AssetImporter::ReadTextureSettings(onDisk->Import) == pixel);
    }

    // A model that cannot be read is not loaded (no GPU objects made) and is tried again after it changes.
    WriteFile(dir / "Assets" / "Models" / "broken.gltf", "{ not json");
    fs::path broken = Project::GetAssetFileSystemPath("Models/broken.gltf");
    Log::SetMinLevel(LogLevel::Critical); // the expected load errors stay out of the output
    CHECK(Model::Load(broken) == nullptr);
    CHECK(!Model::IsFileLoaded(broken));
    bool failed = false;
    CHECK(Model::ReloadFile(broken, &failed) == 0 && !failed); // nothing loaded to update: no failure
    CHECK(Model::Load(Project::GetAssetFileSystemPath("Models/missing.obj")) == nullptr);
    Log::SetMinLevel(LogLevel::Error);
    CHECK(AssetHotReload::CanReload("ship.glb") && AssetHotReload::CanReload("rock.obj"));
    Model::ClearCache();

    Project::SetActive(nullptr);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AssetReferencesFollowUUIDs)
{
    namespace fs = std::filesystem;
    Project::SetActive(nullptr);
    {
        // Without a project nothing but the path is written.
        auto plain = CreateRef<Scene>();
        plain->CreateEntity("S").AddComponent<SpriteRendererComponent>().TexturePath = "Textures/coin.png";
        CHECK(SceneSerializer(plain).SerializeToString().find("TextureUUID") == std::string::npos);
    }

    fs::path dir = MakeTempDir("zamin-asset-refs-");
    CHECK(Project::CreateFromTemplate(dir, "RefGame") != nullptr);
    fs::path assets = dir / "Assets";
    WriteFile(assets / "Textures" / "coin.png", "png");
    WriteFile(assets / "Models" / "ship.glb", "glb");
    WriteFile(assets / "Audio" / "jump.wav", "wav");
    WriteFile(assets / "Environment" / "sky.hdr", "hdr");
    bool writes = AssetRegistry::GetWriteMetaFilesByDefault();
    AssetRegistry::SetWriteMetaFilesByDefault(true); // like the editor
    CHECK(Project::Load(dir / "RefGame.zproject") != nullptr);
    AssetRegistry* registry = Project::GetAssetRegistry();
    CHECK(registry != nullptr);
    if (!registry)
    {
        AssetRegistry::SetWriteMetaFilesByDefault(writes);
        return;
    }
    AssetHandle coin = registry->GetHandle("Textures/coin.png"), ship = registry->GetHandle("Models/ship.glb");
    AssetHandle jump = registry->GetHandle("Audio/jump.wav"), sky = registry->GetHandle("Environment/sky.hdr");
    CHECK(coin && ship && jump && sky);

    // ---- saving: the UUID next to each path the registry knows
    auto scene = CreateRef<Scene>();
    Entity sprite = scene->CreateEntity("Coin");
    sprite.AddComponent<SpriteRendererComponent>().TexturePath = "Textures/coin.png";
    Entity mesh = scene->CreateEntity("Ship");
    auto& mr = mesh.AddComponent<MeshRendererComponent>();
    mr.Type = PrimitiveMesh::Model;
    mr.ModelPath = "Models/ship.glb";
    mr.AlbedoPath = "Textures/coin.png";
    mr.NormalPath = "Textures/missing.png"; // unknown: path only
    Entity sound = scene->CreateEntity("Sound");
    sound.AddComponent<AudioSourceComponent>().Path = "Audio/jump.wav";
    scene->GetSettings().EnvironmentMap = "Environment/sky.hdr";
    std::string text = SceneSerializer(scene).SerializeToString();
    CHECK(text.find("\"TextureUUID\": \"" + coin.ToString() + "\"") != std::string::npos);
    CHECK(text.find("\"ModelUUID\": \"" + ship.ToString() + "\"") != std::string::npos);
    CHECK(text.find("\"AlbedoMapUUID\": \"" + coin.ToString() + "\"") != std::string::npos);
    CHECK(text.find("NormalMapUUID") == std::string::npos);
    CHECK(text.find("\"PathUUID\": \"" + jump.ToString() + "\"") != std::string::npos);
    CHECK(text.find("\"MapUUID\": \"" + sky.ToString() + "\"") != std::string::npos);
    fs::path scenePath = assets / "Scenes" / "Level.zscene";
    CHECK(SceneSerializer(scene).Serialize(scenePath));
    std::string prefabText = SceneSerializer::SerializeEntities(*scene, { sprite }, false);

    // ---- loading unchanged files: the same paths, nothing repaired
    {
        auto loaded = CreateRef<Scene>();
        SceneSerializer serializer(loaded);
        CHECK(serializer.Deserialize(scenePath) && serializer.GetRepairedReferences().empty());
        CHECK(loaded->FindEntityByName("Coin").GetComponent<SpriteRendererComponent>().TexturePath == "Textures/coin.png");
        CHECK(loaded->FindEntityByName("Ship").GetComponent<MeshRendererComponent>().NormalPath == "Textures/missing.png");
    }

    // ---- renamed outside the editor (with their .zmeta files), editor closed: a new scan
    fs::create_directories(assets / "Art");
    fs::rename(assets / "Textures" / "coin.png", assets / "Art" / "gold.png");
    fs::rename(assets / "Textures" / "coin.png.zmeta", assets / "Art" / "gold.png.zmeta");
    fs::rename(assets / "Audio" / "jump.wav", assets / "Audio" / "hop.wav");
    fs::rename(assets / "Audio" / "jump.wav.zmeta", assets / "Audio" / "hop.wav.zmeta");
    fs::rename(assets / "Environment" / "sky.hdr", assets / "Environment" / "dusk.hdr");
    fs::rename(assets / "Environment" / "sky.hdr.zmeta", assets / "Environment" / "dusk.hdr.zmeta");
    registry->Scan(assets);
    CHECK(registry->GetHandle("Art/gold.png") == coin && registry->GetHandle("Audio/hop.wav") == jump);
    {
        auto loaded = CreateRef<Scene>();
        SceneSerializer serializer(loaded);
        CHECK(serializer.Deserialize(scenePath));
        CHECK(loaded->FindEntityByName("Coin").GetComponent<SpriteRendererComponent>().TexturePath == "Art/gold.png");
        auto& lmr = loaded->FindEntityByName("Ship").GetComponent<MeshRendererComponent>();
        CHECK(lmr.AlbedoPath == "Art/gold.png" && lmr.ModelPath == "Models/ship.glb" && lmr.NormalPath == "Textures/missing.png");
        CHECK(loaded->FindEntityByName("Sound").GetComponent<AudioSourceComponent>().Path == "Audio/hop.wav");
        CHECK(loaded->GetSettings().EnvironmentMap == "Environment/dusk.hdr");
        const auto& repairs = serializer.GetRepairedReferences();
        CHECK(repairs.size() == 4); // sprite, albedo map, audio, environment
        CHECK(std::any_of(repairs.begin(), repairs.end(), [](const auto& r) { return r.OldPath == "Textures/coin.png" && r.NewPath == "Art/gold.png"; }));
        // Saved again: the new paths, the same UUIDs.
        std::string again = SceneSerializer(loaded).SerializeToString();
        CHECK(again.find("\"Art/gold.png\"") != std::string::npos && again.find("Textures/coin.png") == std::string::npos);
        CHECK(again.find("\"TextureUUID\": \"" + coin.ToString() + "\"") != std::string::npos);
    }
    // Prefabs (and pasted entities) the same way.
    {
        auto target = CreateRef<Scene>();
        std::vector<SceneSerializer::AssetReferenceRepair> repairs;
        auto created = SceneSerializer::DeserializeEntities(*target, prefabText, true, &repairs);
        CHECK(created.size() == 1 && created[0].GetComponent<SpriteRendererComponent>().TexturePath == "Art/gold.png");
        CHECK(repairs.size() == 1 && repairs[0].NewPath == "Art/gold.png");
    }
    // A path that exists wins over the UUID; an unknown UUID leaves the path alone.
    {
        WriteFile(assets / "Textures" / "coin.png", "another png"); // a new file under the old name
        registry->OnFileAdded(assets / "Textures" / "coin.png");
        auto loaded = CreateRef<Scene>();
        SceneSerializer serializer(loaded);
        CHECK(serializer.Deserialize(scenePath));
        CHECK(loaded->FindEntityByName("Coin").GetComponent<SpriteRendererComponent>().TexturePath == "Textures/coin.png");
        std::string unknown = R"({"Scene":"X","Entities":[{"Entity":1,"Tag":"A","SpriteRenderer":{"Texture":"Gone/a.png","TextureUUID":"00000000deadbeef"}}]})";
        auto other = CreateRef<Scene>();
        SceneSerializer otherSerializer(other);
        CHECK(otherSerializer.DeserializeFromString(unknown) && otherSerializer.GetRepairedReferences().empty());
        CHECK(other->FindEntityByName("A").GetComponent<SpriteRendererComponent>().TexturePath == "Gone/a.png");
    }

    // ---- the project's start scene, renamed with its .zmeta
    {
        auto project = Project::GetActive();
        project->GetConfig().StartScene = "Scenes/Level.zscene";
        CHECK(Project::SaveActive(Project::GetProjectFilePath()));
        CHECK(project->GetConfig().StartSceneHandle == registry->GetHandle("Scenes/Level.zscene"));
        CHECK(FileSystem::ReadTextFile(Project::GetProjectFilePath()).find("StartSceneUUID") != std::string::npos);
        fs::rename(scenePath, assets / "Scenes" / "World.zscene");
        fs::rename(AssetMetaFile::GetMetaPath(scenePath), assets / "Scenes" / "World.zscene.zmeta");
        auto reopened = Project::Load(dir / "RefGame.zproject");
        CHECK(reopened && reopened->GetConfig().StartScene.generic_string() == "Scenes/World.zscene");
    }

    AssetRegistry::SetWriteMetaFilesByDefault(writes);
    Project::SetActive(nullptr);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AssetMovesOutsideEditorAreDetected)
{
    namespace fs = std::filesystem;
    fs::path root = MakeTempDir("zamin-outside-moves-");
    fs::path assets = root / "Assets";
    WriteFile(assets / "Textures" / "coin.png", "png");
    WriteFile(assets / "Textures" / "gem.png", "png");
    WriteFile(assets / "Sfx" / "hit.wav", "wav");
    WriteFile(assets / "Sfx" / "win.wav", "wav");
    AssetRegistry registry(true);
    registry.Scan(assets);
    AssetHandle coin = registry.GetHandle("Textures/coin.png"), gem = registry.GetHandle("Textures/gem.png");
    AssetHandle hit = registry.GetHandle("Sfx/hit.wav");
    auto moveWithMeta = [&](const fs::path& from, const fs::path& to) {
        fs::create_directories(to.parent_path());
        fs::rename(from, to);
        fs::rename(AssetMetaFile::GetMetaPath(from), AssetMetaFile::GetMetaPath(to));
    };

    // Removal seen first, then the new name.
    moveWithMeta(assets / "Textures" / "coin.png", assets / "Textures" / "gold.png");
    registry.OnFileRemoved(assets / "Textures" / "coin.png");
    registry.OnFileAdded(assets / "Textures" / "gold.png");
    auto moves = registry.TakeDetectedMoves();
    CHECK(moves.size() == 1 && moves[0].first == "Textures/coin.png" && moves[0].second == "Textures/gold.png");
    CHECK(registry.GetHandle("Textures/gold.png") == coin && registry.TakeDetectedMoves().empty());

    // The new name seen first.
    moveWithMeta(assets / "Textures" / "gem.png", assets / "Gems" / "gem.png");
    registry.OnFileAdded(assets / "Gems" / "gem.png");
    registry.OnFileRemoved(assets / "Textures" / "gem.png");
    moves = registry.TakeDetectedMoves();
    CHECK(moves.size() == 1 && moves[0].first == "Textures/gem.png" && moves[0].second == "Gems/gem.png");
    CHECK(registry.GetHandle("Gems/gem.png") == gem);

    // A copy (the original still there) is not a move; nor is a file put back where it was.
    fs::copy_file(assets / "Gems" / "gem.png", assets / "Gems" / "gem2.png");
    fs::copy_file(assets / "Gems" / "gem.png.zmeta", assets / "Gems" / "gem2.png.zmeta");
    registry.OnFileAdded(assets / "Gems" / "gem2.png");
    CHECK(registry.TakeDetectedMoves().empty() && registry.GetHandle("Gems/gem2.png") != gem);
    std::string hitText = FileSystem::ReadTextFile(assets / "Sfx" / "hit.wav");
    fs::path parked = root / "hit.wav";
    fs::rename(assets / "Sfx" / "hit.wav", parked);
    registry.OnFileRemoved(assets / "Sfx" / "hit.wav");
    fs::rename(parked, assets / "Sfx" / "hit.wav");
    registry.OnFileAdded(assets / "Sfx" / "hit.wav");
    CHECK(registry.TakeDetectedMoves().empty() && registry.GetHandle("Sfx/hit.wav") == hit);

    // A whole folder renamed: every asset inside.
    fs::rename(assets / "Sfx", assets / "Sounds");
    registry.OnFileRemoved(assets / "Sfx");
    registry.OnFileAdded(assets / "Sounds");
    moves = registry.TakeDetectedMoves();
    std::sort(moves.begin(), moves.end());
    CHECK(moves.size() == 2 && moves[0] == AssetRegistry::PathMove("Sfx/hit.wav", "Sounds/hit.wav") &&
          moves[1] == AssetRegistry::PathMove("Sfx/win.wav", "Sounds/win.wav"));

    // Through the hot reload report (what the editor gets each frame), and the references follow.
    WriteFile(assets / "Scenes" / "Level.zscene",
              R"({"Scene":"Level","Entities":[{"Entity":1,"Tag":"Textures/gold.png","SpriteRenderer":{"Texture":"Textures/gold.png"}}]})");
    registry.OnFileAdded(assets / "Scenes" / "Level.zscene");
    moveWithMeta(assets / "Textures" / "gold.png", assets / "Art" / "gold.png");
    AssetHotReload hotReload;
    auto report = hotReload.Apply({ { assets / "Textures" / "gold.png", FileChangeType::Removed, false },
                                    { assets / "Art" / "gold.png", FileChangeType::Added, false } }, &registry);
    CHECK(report.Moved.size() == 1 && report.Moved[0].second == "Art/gold.png" && !report.IsEmpty());
    AssetFiles::MoveResult result;
    for (const auto& [from, to] : report.Moved)
        result.Moved.push_back({ assets / from, assets / to, false });
    AssetFiles::UpdateReferences(result, assets);
    std::string level = FileSystem::ReadTextFile(assets / "Scenes" / "Level.zscene");
    CHECK(result.UpdatedReferences == 1 && level.find("\"Texture\":\"Art/gold.png\"") != std::string::npos);
    CHECK(level.find("\"Tag\":\"Textures/gold.png\"") != std::string::npos); // a name, not a reference

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(AssetSearchByPath)
{
    namespace fs = std::filesystem;
    auto match = [](const char* query, const char* path, AssetType type = AssetType::Texture) {
        return AssetSearch(query).Matches(path, type);
    };
    // A part of the whole path, any case; folders count.
    CHECK(match("coin", "Assets/Textures/coin.png") && match("COIN", "Assets/Textures/Coin.PNG"));
    CHECK(!match("coin", "Assets/Textures/wall.png"));
    CHECK(match("textures", "Assets/Textures/wall.png") && match("textures", "Assets/Textures", AssetType::Folder));
    CHECK(match("Textures/", "Assets/Textures/wall.png") && !match("Textures/", "Assets/Textures", AssetType::Folder));
    CHECK(match("textures/wa", "Assets/Textures/wall.png") && match("Textures\\wall", "Assets/Textures/wall.png"));
    // '/' separated parts over folders and the name, in order.
    CHECK(match("tex/coin", "Assets/Textures/Items/coin.png") && match("as/it/co", "Assets/Textures/Items/coin.png"));
    CHECK(!match("coin/tex", "Assets/Textures/Items/coin.png") && !match("tex/gold", "Assets/Textures/Items/coin.png"));
    CHECK(match("/textures//coin", "Assets/Textures/coin.png") && match("ass/tex/", "Assets/Textures/coin.png"));
    CHECK(!match("ass/tex/", "Assets/Textures", AssetType::Folder) && !match("tex/coin/", "Assets/Textures/coin.png"));
    // Wildcards; every word must match.
    CHECK(match("*.png", "Assets/a.png") && !match("*.png", "Assets/a.jpg") && match("c*n", "Assets/coin.png"));
    CHECK(match("tex coin", "Assets/Textures/coin.png") && !match("tex gold", "Assets/Textures/coin.png"));
    CHECK(match("*", "anything") && AssetSearch::MatchPattern("abc", "a*c") && !AssetSearch::MatchPattern("abc", "c*a"));
    // Types by key or name; several t: words = any of them.
    CHECK(match("t:rasm", "a.png") && match("t:texture", "a.png") && match("t:TEX", "a.png") && !match("t:model", "a.png"));
    CHECK(match("t:model robot", "Models/Robot.gltf", AssetType::Model) && !match("t:model robot", "Models/Robot.png"));
    CHECK(match("t:audio t:model", "x.wav", AssetType::Audio) && match("tur:sahna", "L.zscene", AssetType::Scene));
    CHECK(match("t:skript", "P.cpp", AssetType::Script) && match("t:hdr", "sky.hdr", AssetType::Environment) && match("t:muhit", "s.hdr", AssetType::Environment));
    AssetSearch many("t:s");
    CHECK(many.HasTypeFilter() && std::find(many.GetTypes().begin(), many.GetTypes().end(), AssetType::Scene) != many.GetTypes().end() &&
          std::find(many.GetTypes().begin(), many.GetTypes().end(), AssetType::Font) != many.GetTypes().end()); // "Shrift"
    CHECK(!match("t:xyz", "a.png") && AssetSearch("t:").IsEmpty() && AssetSearch("   ").IsEmpty() && !AssetSearch("a").IsEmpty());
    // Skipping the top folder (paths from the project folder): plain words do not match "Assets/"
    // or "Source/", words with '/' and the top folder's own name still do.
    auto top = [](const char* query, const char* path, AssetType type = AssetType::Texture) {
        return AssetSearch(query).Matches(path, type, true);
    };
    CHECK(match("a", "Assets/Textures/coin.png") && !top("a", "Assets/Textures/coin.png") && !top("set", "Assets/Textures/coin.png"));
    CHECK(top("tex", "Assets/Textures/coin.png") && top("coin", "Assets/Textures/coin.png") && top("*.png", "Assets/Textures/coin.png"));
    CHECK(top("assets/tex", "Assets/Textures/coin.png") && top("Assets/", "Assets/Textures/coin.png") && top("as/co", "Assets/Textures/coin.png"));
    CHECK(top("assets", "Assets", AssetType::Folder) && top("ou", "Source", AssetType::Folder) && !top("ou", "Source/CoinScript.cpp", AssetType::Script));
    CHECK(top("project", "Game.zproject", AssetType::Other) && top("script", "Source/CoinScript.cpp", AssetType::Script));

    // The content browser's listing: every sub folder, paths matched from the search root.
    fs::path root = MakeTempDir("zamin-search-");
    WriteFile(root / "Assets" / "Textures" / "coin.png", "x");
    WriteFile(root / "Assets" / "Textures" / "Items" / "gold_coin.png", "x");
    WriteFile(root / "Assets" / "Textures" / "Items" / "gold_coin.png.zmeta", "{}");
    WriteFile(root / "Assets" / "Models" / "Robot.gltf", "{}");
    WriteFile(root / "Source" / "CoinScript.cpp", "//");
    WriteFile(root / "Build" / "coin.png", "x");
    auto names = [](const std::vector<AssetEntry>& entries) {
        std::vector<std::string> out;
        for (const AssetEntry& e : entries)
            out.push_back(e.Name);
        std::sort(out.begin(), out.end());
        return out;
    };
    AssetListOptions options;
    options.SearchRoot = root;
    options.Excluded = { root / "Build" };
    options.Search = "coin";
    CHECK(names(AssetFiles::List(root, options)) == std::vector<std::string>({ "CoinScript.cpp", "coin.png", "gold_coin.png" }));
    CHECK(AssetFiles::List(root / "Assets" / "Models", options).empty()); // only below the listed folder
    options.Search = "assets/textures/";
    CHECK(names(AssetFiles::List(root, options)) == std::vector<std::string>({ "Items", "coin.png", "gold_coin.png" }));
    options.Search = "Textures/Items/";
    CHECK(names(AssetFiles::List(root / "Assets", options)) == std::vector<std::string>({ "gold_coin.png" }));
    options.Search = "t:rasm";
    CHECK(names(AssetFiles::List(root, options)) == std::vector<std::string>({ "coin.png", "gold_coin.png" }));
    options.Search = "src/coin t:skript";
    CHECK(names(AssetFiles::List(root, options)).empty()); // "Source" does not contain "src"
    options.Search = "sou/coin t:skript";
    CHECK(names(AssetFiles::List(root, options)) == std::vector<std::string>({ "CoinScript.cpp" }));
    std::vector<AssetEntry> found = AssetFiles::List(root, options);
    CHECK(found.size() == 1 && found[0].Path == root / "Source" / "CoinScript.cpp" && found[0].Type == AssetType::Script);
    // As the editor searches: "as" or "ou" do not find every file below "Assets" / "Source".
    options.Search = "as";
    CHECK(names(AssetFiles::List(root, options)).size() >= 5); // all of Assets/
    options.SkipTopFolder = true;
    CHECK(names(AssetFiles::List(root, options)) == std::vector<std::string>({ "Assets" }));
    options.Search = "ou";
    CHECK(names(AssetFiles::List(root, options)) == std::vector<std::string>({ "Source" }));
    options.Search = "coin";
    CHECK(names(AssetFiles::List(root, options)) == std::vector<std::string>({ "CoinScript.cpp", "coin.png", "gold_coin.png" }));
    options.Search = "assets/textures/";
    CHECK(names(AssetFiles::List(root, options)) == std::vector<std::string>({ "Items", "coin.png", "gold_coin.png" }));
    options.Search = "textures";
    CHECK(names(AssetFiles::List(root / "Assets", options)) == std::vector<std::string>({ "Items", "Textures", "coin.png", "gold_coin.png" }));
    options.SkipTopFolder = false;
    // Without a search root the paths start at the listed folder.
    options.SearchRoot.clear();
    options.Search = "assets/";
    CHECK(AssetFiles::List(root / "Assets", options).empty() && AssetFiles::List(root, options).size() >= 5);
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(ContentBrowserSettingsJson)
{
    EditorViewSettings settings;
    CHECK(!settings.ContentBrowser.ListView && !settings.ContentBrowser.Filter.IsActive());
    settings.ContentBrowser.ListView = true;
    settings.ContentBrowser.ThumbnailSize = 120.0f;
    settings.ContentBrowser.Filter.Set(AssetType::Texture, true);
    settings.ContentBrowser.Filter.Set(AssetType::Audio, true);
    settings.ContentBrowser.SortBy = AssetSortColumn::Modified;
    settings.ContentBrowser.SortAscending = false;
    CHECK(settings.ContentBrowser.SearchEverywhere); // the whole project by default
    settings.ContentBrowser.SearchEverywhere = false;
    std::string text = settings.ToJson();
    CHECK(text.find("\"Texture\"") != std::string::npos && text.find("\"List\"") != std::string::npos);

    EditorViewSettings loaded;
    CHECK(loaded.FromJson(text));
    CHECK(loaded.ContentBrowser.ListView && loaded.ContentBrowser.ThumbnailSize == 120.0f && !loaded.ContentBrowser.SearchEverywhere);
    CHECK(loaded.ContentBrowser.Filter.Mask == settings.ContentBrowser.Filter.Mask);
    CHECK(loaded.ContentBrowser.SortBy == AssetSortColumn::Modified && !loaded.ContentBrowser.SortAscending);

    // Junk is ignored / clamped; an older file without the section keeps the defaults.
    EditorViewSettings junk;
    CHECK(junk.FromJson("{\"ZaminEditorView\":1,\"ContentBrowser\":{\"View\":3,\"ThumbnailSize\":9000,"
                        "\"Filter\":[\"Texture\",\"Folder\",\"Bogus\",7],\"SortBy\":\"Colour\"}}"));
    CHECK(!junk.ContentBrowser.ListView && junk.ContentBrowser.ThumbnailSize == ContentBrowserSettings::MaxThumbnailSize);
    CHECK(junk.ContentBrowser.Filter.Count() == 1 && junk.ContentBrowser.Filter.Has(AssetType::Texture));
    CHECK(junk.ContentBrowser.SortBy == AssetSortColumn::Name);
    EditorViewSettings old;
    CHECK(old.FromJson("{\"ZaminEditorView\":1}"));
    CHECK(!old.ContentBrowser.ListView && old.ContentBrowser.ThumbnailSize == 84.0f);

    // Camera bookmarks follow a moved scene file (or a moved folder of scenes).
    EditorViewSettings view;
    view.GetBookmarks("/p/Assets/Scenes/A.zscene").Set(1, EditorCameraState{});
    view.GetBookmarks("/p/Assets/Scenes/B.zscene").Set(2, EditorCameraState{});
    view.GetBookmarks("/p/Assets/Other.zscene").Set(3, EditorCameraState{});
    CHECK(view.MoveSceneBookmarks("/p/Assets/Scenes", "/p/Assets/Levels") == 2);
    CHECK(view.GetBookmarks("/p/Assets/Levels/A.zscene").Has(1) && view.GetBookmarks("/p/Assets/Levels/B.zscene").Has(2));
    CHECK(!view.Bookmarks.contains(EditorViewSettings::SceneKey("/p/Assets/Scenes/A.zscene")));
    CHECK(view.MoveSceneBookmarks("/p/Assets/Other.zscene", "/p/Assets/Levels/Other.zscene") == 1);
    CHECK(view.GetBookmarks("/p/Assets/Levels/Other.zscene").Has(3));
}

TEST(SelectionSummaryForStatusBar)
{
    auto scene = CreateRef<Scene>();
    Entity world = scene->CreateEntity("World");
    Entity player = scene->CreateEntity("Player");
    Entity weapon = scene->CreateEntity("Weapon");
    Entity tip = scene->CreateEntity("Tip");
    Entity other = scene->CreateEntity("Other");
    scene->SetParent(player, world, false);
    scene->SetParent(weapon, player, false);
    scene->SetParent(tip, weapon, false);
    world.GetComponent<TransformComponent>().Translation = { 10.0f, 0.0f, 0.0f };
    player.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
    other.GetComponent<TransformComponent>().Translation = { -3.0f, 0.0f, 1.0f };
    player.AddComponent<SpriteRendererComponent>();
    player.AddComponent<Rigidbody2DComponent>();
    player.AddComponent<TagsComponent>().Add("Player");

    EntitySelection selection;
    CHECK(SummarizeSelection(*scene, selection).Count == 0);

    selection.Select(player);
    SelectionSummary s = SummarizeSelection(*scene, selection);
    CHECK(s.Count == 1 && s.Primary == player && s.Name == "Player");
    CHECK(s.Path == "World/Player" && s.ID == (uint64_t)player.GetUUID());
    CHECK_NEAR(s.Position.x, 11.0f, 1e-4f);
    CHECK_NEAR(s.Position.z, 3.0f, 1e-4f);
    CHECK((s.Components == std::vector<std::string>{ "Transform", "Tags", "SpriteRenderer", "Rigidbody2D" }));
    CHECK(s.ChildCount == 1 && s.DescendantCount == 2);
    CHECK(s.Tags.size() == 1 && s.Tags[0] == "Player");
    CHECK(!s.Hidden && !s.Locked);

    // Several entities: the primary's details, the center of all of them, hidden / locked counts.
    scene->SetHiddenInEditor(world, true);
    scene->SetLockedInEditor(other, true);
    selection.Set({ player, other }, other);
    s = SummarizeSelection(*scene, selection);
    CHECK(s.Count == 2 && s.Name == "Other" && s.Path == "Other" && s.Locked && !s.Hidden);
    CHECK(s.HiddenCount == 1 && s.LockedCount == 1);
    CHECK_NEAR(s.Center.x, (11.0f - 3.0f) * 0.5f, 1e-4f);
    CHECK_NEAR(s.Center.z, 2.0f, 1e-4f);
    selection.Set({ tip });
    s = SummarizeSelection(*scene, selection);
    CHECK(s.Path == "World/Player/Weapon/Tip" && s.Hidden && s.ChildCount == 0);
}

// ------------------------------------------------------------------ active / inactive entities (M03)
TEST(ActiveFlagAndHierarchy)
{
    auto scene = CreateRef<Scene>();
    Entity parent = scene->CreateEntity("Parent");
    Entity child = scene->CreateEntity("Child");
    Entity grandchild = scene->CreateEntity("Grandchild");
    Entity other = scene->CreateEntity("Other");
    scene->SetParent(child, parent, false);
    scene->SetParent(grandchild, child, false);
    CHECK(parent.GetComponent<TagComponent>().Active && parent.IsActive() && scene->IsActive(parent));
    CHECK(scene->IsActiveInHierarchy(grandchild) && scene->CountInactiveInHierarchy() == 0);
    CHECK(!scene->IsActiveInHierarchy(Entity()) && !scene->IsActive(Entity()));

    // An inactive parent switches its whole subtree off; the children keep their own flag.
    scene->SetActive(parent, false);
    CHECK(!scene->IsActive(parent) && child.IsActive() && grandchild.IsActive());
    CHECK(!scene->IsActiveInHierarchy(parent) && !scene->IsActiveInHierarchy(child) && !grandchild.IsActiveInHierarchy());
    CHECK(other.IsActiveInHierarchy() && scene->CountInactiveInHierarchy() == 3);
    scene->SetActive(parent, true);
    CHECK(grandchild.IsActiveInHierarchy() && scene->CountInactiveInHierarchy() == 0);

    // A switched-off entity in the middle: itself and everything below it.
    child.SetActive(false);
    CHECK(parent.IsActiveInHierarchy() && !child.IsActiveInHierarchy() && !grandchild.IsActiveInHierarchy());
    // Parent and child both off: switching the parent back on keeps the child's subtree off.
    parent.SetActive(false);
    CHECK(scene->CountInactiveInHierarchy() == 3);
    parent.SetActive(true);
    CHECK(parent.IsActiveInHierarchy() && !child.IsActiveInHierarchy() && !grandchild.IsActiveInHierarchy());

    // Reparenting: out from under an inactive entity -> on, below one -> off (the whole subtree).
    scene->SetParent(grandchild, other, false);
    CHECK(grandchild.IsActiveInHierarchy());
    scene->SetParent(other, child, false);
    CHECK(!other.IsActiveInHierarchy() && !grandchild.IsActiveInHierarchy());
    scene->MoveEntityNextTo(other, parent, true, false); // a root again
    CHECK(other.IsActiveInHierarchy() && grandchild.IsActiveInHierarchy());
    scene->SetParent(grandchild, child, false);
    CHECK(!grandchild.IsActiveInHierarchy());

    // A direct write is noticed after InvalidateActiveCache() or on the next frame.
    other.GetComponent<TagComponent>().Active = false;
    scene->InvalidateActiveCache();
    CHECK(!other.IsActiveInHierarchy());
    other.GetComponent<TagComponent>().Active = true;
    scene->OnUpdateRuntime(0.016f); // a new frame (nothing runs: no scripts, no primary camera)
    CHECK(other.IsActiveInHierarchy());

    // A broken file with a parent cycle cannot hang the walk or the subtree collection.
    parent.GetComponent<RelationshipComponent>().Parent = child.GetUUID();
    child.GetComponent<RelationshipComponent>().Children.push_back(parent.GetUUID());
    parent.GetComponent<TagComponent>().Active = false;
    scene->InvalidateActiveCache();
    CHECK(!parent.IsActiveInHierarchy() && !child.IsActiveInHierarchy() && scene->CountInactiveInHierarchy() == 3);
    parent.GetComponent<RelationshipComponent>().Parent = 0;
    std::erase(child.GetComponent<RelationshipComponent>().Children, parent.GetUUID());
    parent.GetComponent<TagComponent>().Active = true;
    scene->InvalidateActiveCache();
    CHECK(parent.IsActiveInHierarchy() && !child.IsActiveInHierarchy());

    // Saved only when off, so files of fully active scenes stay as they were.
    child.SetActive(true);
    CHECK(SceneSerializer(scene).SerializeToString().find("\"Active\"") == std::string::npos);
    child.SetActive(false);
    std::string text = SceneSerializer(scene).SerializeToString();
    CHECK(text.find("\"Active\": false") != std::string::npos && text.find("\"Active\": true") == std::string::npos);
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    CHECK(!loaded->FindEntityByName("Child").IsActive() && loaded->FindEntityByName("Grandchild").IsActive());
    CHECK(loaded->FindEntityByName("Parent").IsActiveInHierarchy() && !loaded->FindEntityByName("Grandchild").IsActiveInHierarchy());
    CHECK(SceneSerializer(loaded).SerializeToString() == text);

    // Kept by the play-mode copy, duplicates, Instantiate() and prefabs (game state, unlike Hidden).
    auto runtime = Scene::Copy(scene);
    CHECK(!runtime->FindEntityByName("Child").IsActive() && !runtime->FindEntityByName("Grandchild").IsActiveInHierarchy());
    CHECK(runtime->FindEntityByName("Parent").IsActiveInHierarchy());
    Entity copy = scene->DuplicateEntity(child);
    CHECK(!copy.IsActive() && scene->GetParent(copy) == parent && !scene->FindChild(copy, "Grandchild").IsActiveInHierarchy());
    CHECK(scene->CountInactiveInHierarchy() == 4);
    scene->DestroyEntity(copy);
    CHECK(scene->CountInactiveInHierarchy() == 2);
    Entity clone = scene->Instantiate(parent);
    CHECK(clone.IsActiveInHierarchy() && !scene->FindChild(clone, "Child").IsActive());
    CHECK(!scene->FindChild(clone, "Grandchild").IsActiveInHierarchy());
    scene->DestroyEntity(clone);
    std::string prefab = SceneSerializer::SerializeEntities(*scene, { parent, child, grandchild }, false);
    CHECK(prefab.find("\"Active\": false") != std::string::npos);
    auto third = CreateRef<Scene>();
    third->CreateEntity("Existing").SetActive(false);
    CHECK(third->CountInactiveInHierarchy() == 1);
    auto inst = SceneSerializer::DeserializeEntities(*third, prefab, true);
    CHECK(inst.size() == 3 && third->CountInactiveInHierarchy() == 3); // loading refreshes the cache at once
    CHECK(third->FindEntityByName("Parent").IsActiveInHierarchy() && !third->FindEntityByName("Grandchild").IsActiveInHierarchy());
}

TEST(InactiveEntitiesAreNotDrawn)
{
    auto scene = CreateRef<Scene>();
    Entity group = scene->CreateEntity("Group");
    Entity sprite = scene->CreateEntity("Sprite");
    sprite.AddComponent<SpriteRendererComponent>();
    scene->SetParent(sprite, group, false);
    Entity mesh = scene->CreateEntity("Mesh");
    mesh.AddComponent<MeshRendererComponent>();
    mesh.GetTransform().Translation = { 10.0f, 0.0f, 0.0f };
    Entity light = scene->CreateEntity("Light");
    light.AddComponent<PointLightComponent>();
    Entity hud = scene->CreateEntity("HUD");
    hud.AddComponent<UITransformComponent>();
    hud.AddComponent<UIButtonComponent>();
    Entity shown = scene->CreateEntity("Shown");
    shown.AddComponent<CircleRendererComponent>();
    CHECK(scene->CollectRenderSkipped().empty());

    // What RenderScene leaves out: every inactive entity and its subtree - meshes, sprites, UI,
    // lights - in every view (the game view too).
    group.SetActive(false);
    mesh.SetActive(false);
    light.SetActive(false);
    hud.SetActive(false);
    auto skipped = scene->CollectRenderSkipped();
    CHECK(skipped.size() == 5 && skipped.contains(sprite) && skipped.contains(mesh));
    CHECK(skipped.contains(light) && skipped.contains(hud) && !skipped.contains(shown));
    // The editor's eye adds to it only in the editor's views.
    scene->SetHiddenInEditor(shown, true);
    CHECK(!scene->CollectRenderSkipped().contains(shown));
    SceneRenderOptions editorView;
    editorView.SkipEditorHidden = true;
    CHECK(scene->CollectRenderSkipped(editorView).size() == 6 && scene->CollectRenderSkipped(editorView).contains(shown));
    scene->SetHiddenInEditor(shown, false);

    // Frame All (Shift+F) frames what is drawn: not the inactive mesh far away.
    AABB box;
    CHECK(scene->ComputeSceneBounds(box));
    CHECK_NEAR(box.Max.x, 0.5f, 1e-4f);
    mesh.SetActive(true);
    CHECK(scene->ComputeSceneBounds(box));
    CHECK_NEAR(box.Max.x, 10.5f, 1e-4f);
    // Switching the group on brings its child back.
    group.SetActive(true);
    CHECK(!scene->CollectRenderSkipped().contains(sprite) && scene->CollectRenderSkipped().size() == 2);
}

TEST(InactiveEntitiesLeavePhysics2D)
{
    auto scene = CreateRef<Scene>();
    Entity ball = scene->CreateEntity("Ball");
    ball.GetTransform().Translation = { 0.0f, 10.0f, 0.0f };
    ball.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    ball.AddComponent<CircleCollider2DComponent>();
    Entity holder = scene->CreateEntity("Holder"); // inactive when play starts, with a body below it
    holder.GetTransform().Translation = { 5.0f, 0.0f, 0.0f };
    holder.SetActive(false);
    Entity crate = scene->CreateEntity("Crate");
    crate.GetTransform().Translation = { 0.0f, 10.0f, 0.0f };
    crate.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    crate.AddComponent<BoxCollider2DComponent>();
    scene->SetParent(crate, holder, false);
    Entity map = scene->CreateEntity("Map");
    map.GetTransform().Translation = { -20.0f, -20.0f, 0.0f };
    auto& tm = map.AddComponent<TilemapComponent>();
    tm.Width = 4; tm.Height = 1;
    tm.EnsureSize();
    tm.Set(0, 0, 1);

    scene->OnRuntimeStart();
    CHECK(ball.GetComponent<Rigidbody2DComponent>().RuntimeBody != nullptr);
    CHECK(crate.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr); // its parent is off
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody != nullptr);
    for (int i = 0; i < 30; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    float fallen = ball.GetTransform().Translation.y;
    CHECK(fallen < 9.5f);
    CHECK_NEAR(crate.GetTransform().Translation.y, 10.0f, 1e-5f);
    RaycastHit hit = scene->Raycast2D({ -5.0f, fallen }, { 1.0f, 0.0f }, 10.0f);
    CHECK(hit.Hit && hit.EntityID == (uint64_t)ball.GetUUID());

    // Switched off: the body leaves the world, the transform stays where it was.
    ball.SetActive(false);
    CHECK(ball.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
    CHECK(ball.GetComponent<CircleCollider2DComponent>().RuntimeFixture == nullptr);
    for (int i = 0; i < 30; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(ball.GetTransform().Translation.y, fallen, 1e-5f);
    CHECK(!scene->Raycast2D({ -5.0f, fallen }, { 1.0f, 0.0f }, 10.0f).Hit);

    // Back on: a new body at the current (moved) transform, starting at rest.
    ball.GetTransform().Translation = { 3.0f, 20.0f, 0.0f };
    ball.SetActive(true);
    CHECK(ball.GetComponent<Rigidbody2DComponent>().RuntimeBody && ball.GetComponent<CircleCollider2DComponent>().RuntimeFixture);
    scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(ball.GetTransform().Translation.x, 3.0f, 1e-4f);
    CHECK(ball.GetTransform().Translation.y < 20.0f && ball.GetTransform().Translation.y > 19.9f); // the old speed is gone

    // An inactive parent switched on: the child's body appears at its world position.
    holder.SetActive(true);
    CHECK(crate.GetComponent<Rigidbody2DComponent>().RuntimeBody != nullptr);
    scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(scene->GetWorldPosition(crate).x, 5.0f, 1e-4f);
    CHECK(scene->GetWorldPosition(crate).y < 10.0f && scene->GetWorldPosition(crate).y > 9.9f);
    // Tilemap collision goes and comes back with its entity.
    map.SetActive(false);
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody == nullptr);
    map.SetActive(true);
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody != nullptr);
    // A direct write (inspector) takes effect on the next frame.
    crate.GetComponent<TagComponent>().Active = false;
    scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(crate.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
    scene->OnRuntimeStop();

    // Simulation mode (physics without scripts) follows too.
    scene->OnSimulationStart();
    CHECK(crate.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr && ball.GetComponent<Rigidbody2DComponent>().RuntimeBody);
    crate.GetComponent<TagComponent>().Active = true;
    scene->InvalidateActiveCache();
    scene->SetActive(holder, false);
    CHECK(crate.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
    scene->SetActive(holder, true);
    CHECK(crate.GetComponent<Rigidbody2DComponent>().RuntimeBody != nullptr);
    scene->OnSimulationStop();
}

TEST(InactiveEntitiesEndContacts)
{
    g_Contacts.clear();
    auto scene = CreateRef<Scene>();
    scene->GetSettings().Gravity = { 0.0f, 0.0f };
    Entity zone = scene->CreateEntity("Zone");
    zone.GetTransform().Scale = { 6, 6, 1 };
    zone.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
    zone.AddComponent<BoxCollider2DComponent>().IsSensor = true;
    AddContactLog(zone);
    Entity a = scene->CreateEntity("A");
    a.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    a.AddComponent<BoxCollider2DComponent>();
    AddContactLog(a);

    scene->OnRuntimeStart();
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(CountContact("A:TriggerBegin:Zone") == 1 && CountContact("Zone:TriggerBegin:A") == 1);

    // Switching off ends the contact for the other side at once, and only once.
    g_Contacts.clear();
    a.SetActive(false);
    CHECK((g_Contacts == std::vector<std::string>{ "Zone:TriggerEnd:A" }));
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(g_Contacts.size() == 1);

    // Back on inside the sensor: a fresh Begin on both sides.
    g_Contacts.clear();
    a.SetActive(true);
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(CountContact("A:TriggerBegin:Zone") == 1 && CountContact("Zone:TriggerBegin:A") == 1);
    CHECK(CountContact("A:TriggerEnd:Zone") == 0);
    scene->OnRuntimeStop();
}

TEST(InactiveEntitiesLeavePhysics3D)
{
    g_Contacts.clear();
    auto scene = CreateRef<Scene>();
    Entity ball = scene->CreateEntity("Ball");
    ball.GetTransform().Translation = { 0, 10, 0 };
    ball.AddComponent<Rigidbody3DComponent>();
    ball.AddComponent<SphereCollider3DComponent>().Radius = 0.5f;
    Entity hero = scene->CreateEntity("Hero");
    hero.GetTransform().Translation = { 20, 0, 0 };
    hero.AddComponent<CharacterController3DComponent>();
    Entity sleeper = scene->CreateEntity("Sleeper"); // inactive from the start
    sleeper.GetTransform().Translation = { -10, 5, 0 };
    sleeper.AddComponent<Rigidbody3DComponent>();
    sleeper.AddComponent<BoxCollider3DComponent>();
    sleeper.SetActive(false);

    scene->OnRuntimeStart();
    PhysicsWorld3D* world = scene->GetPhysicsWorld3D();
    CHECK(world != nullptr);
    CHECK(ball.GetComponent<Rigidbody3DComponent>().RuntimeBodyID != 0xffffffff);
    CHECK(sleeper.GetComponent<Rigidbody3DComponent>().RuntimeBodyID == 0xffffffff);
    CHECK(hero.GetComponent<CharacterController3DComponent>().RuntimeID != 0xffffffff);
    for (int i = 0; i < 30; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    float fallen = ball.GetTransform().Translation.y;
    CHECK(fallen < 9.5f);
    CHECK(scene->Raycast3D({ -5.0f, fallen, 0.0f }, { 1, 0, 0 }, 10.0f).Hit);

    ball.SetActive(false);
    CHECK(ball.GetComponent<Rigidbody3DComponent>().RuntimeBodyID == 0xffffffff);
    for (int i = 0; i < 30; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(ball.GetTransform().Translation.y, fallen, 1e-5f);
    CHECK(!scene->Raycast3D({ -5.0f, fallen, 0.0f }, { 1, 0, 0 }, 10.0f).Hit);
    ball.GetTransform().Translation = { 2, 30, 0 };
    ball.SetActive(true);
    uint32_t id = ball.GetComponent<Rigidbody3DComponent>().RuntimeBodyID;
    CHECK(id != 0xffffffff);
    if (id != 0xffffffff)
    {
        glm::vec3 p; glm::quat q;
        world->GetTransform(id, p, q);
        CHECK_NEAR(p.y, 30.0f, 1e-4f);
        CHECK_NEAR(p.x, 2.0f, 1e-4f);
    }

    // The character leaves too and comes back where the entity is now.
    hero.SetActive(false);
    CHECK(hero.GetComponent<CharacterController3DComponent>().RuntimeID == 0xffffffff);
    hero.GetTransform().Translation = { 25, 3, 0 };
    for (int i = 0; i < 10; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(hero.GetTransform().Translation.x, 25.0f, 1e-5f);
    CHECK_NEAR(hero.GetTransform().Translation.y, 3.0f, 1e-5f); // no gravity while off
    hero.SetActive(true);
    uint32_t cid = hero.GetComponent<CharacterController3DComponent>().RuntimeID;
    CHECK(cid != 0xffffffff);
    if (cid != 0xffffffff)
        CHECK_NEAR(world->GetCharacterPosition(cid).x, 25.0f, 1e-4f);
    // Off and on many times: the freed character slot is reused.
    for (int i = 0; i < 5; i++)
    {
        hero.SetActive(false);
        hero.SetActive(true);
    }
    CHECK(hero.GetComponent<CharacterController3DComponent>().RuntimeID == cid);

    sleeper.SetActive(true);
    CHECK(sleeper.GetComponent<Rigidbody3DComponent>().RuntimeBodyID != 0xffffffff);
    scene->OnRuntimeStop();

    // A sensor contact ends for the other side when one of them is switched off.
    g_Contacts.clear();
    auto zoneScene = CreateRef<Scene>();
    zoneScene->GetSettings().Gravity3D = { 0.0f, 0.0f, 0.0f };
    Entity zone = zoneScene->CreateEntity("Zone");
    zone.GetTransform().Scale = { 4, 4, 4 };
    zone.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    zone.AddComponent<BoxCollider3DComponent>().IsSensor = true;
    AddContactLog(zone);
    Entity orb = zoneScene->CreateEntity("Orb");
    orb.AddComponent<Rigidbody3DComponent>();
    orb.AddComponent<SphereCollider3DComponent>().Radius = 0.25f;
    AddContactLog(orb);
    zoneScene->OnRuntimeStart();
    for (int i = 0; i < 5; i++)
        zoneScene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(CountContact("Orb:TriggerBegin:Zone") == 1 && CountContact("Zone:TriggerBegin:Orb") == 1);
    g_Contacts.clear();
    orb.SetActive(false);
    CHECK((g_Contacts == std::vector<std::string>{ "Zone:TriggerEnd:Orb" }));
    for (int i = 0; i < 5; i++)
        zoneScene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(g_Contacts.size() == 1);
    orb.SetActive(true);
    for (int i = 0; i < 5; i++)
        zoneScene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(CountContact("Orb:TriggerBegin:Zone") == 1 && CountContact("Zone:TriggerBegin:Orb") == 1);
    zoneScene->OnRuntimeStop();
}

// Switches entities from its callbacks. Mode 1: its own entity off in OnUpdate; 2: flips its
// own entity in OnEnable / OnDisable (a runaway the guard must stop); 3: switches "Target" off
// in its first OnUpdate.
static int g_ActiveFlips = 0;
class ActiveToggleScript : public ScriptableEntity
{
public:
    int Mode = 0;
    void OnEnable() override
    {
        Log("Enable");
        if (Mode == 2 && ++g_ActiveFlips < 1000) GetEntity().SetActive(false);
    }
    void OnDisable() override
    {
        Log("Disable");
        if (Mode == 2 && ++g_ActiveFlips < 1000) GetEntity().SetActive(true);
    }
    void OnUpdate(Timestep) override
    {
        Log("Update");
        if (Mode == 1) GetEntity().SetActive(false);
        if (Mode == 3 && !m_Done)
        {
            m_Done = true;
            FindEntity("Target").SetActive(false);
        }
    }
    void OnLateUpdate(Timestep) override { Log("Late"); }
private:
    void Log(const char* what) { g_Life.push_back(GetName() + ":" + what); }
    bool m_Done = false;
};
ZM_REGISTER_SCRIPT(ActiveToggleScript, ZM_FIELD(ActiveToggleScript, Mode))

static Entity AddToggleScript(Scene& scene, const std::string& name, int mode)
{
    Entity e = scene.CreateEntity(name);
    auto& nsc = e.AddComponent<NativeScriptComponent>();
    nsc.ClassName = "ActiveToggleScript";
    ScriptFieldValue v; v.Set(mode);
    nsc.Fields["Mode"] = v;
    return e;
}

TEST(InactiveEntityScripts)
{
    g_Life.clear();
    auto scene = CreateRef<Scene>();
    Entity root = scene->CreateEntity("Root");
    Entity a = AddLifeScript(*scene, "A");
    scene->SetParent(a, root, false);
    Entity b = AddLifeScript(*scene, "B");
    Entity sleeper = AddLifeScript(*scene, "Sleeper");
    sleeper.SetActive(false);                        // inactive when play starts
    Entity off = AddLifeScript(*scene, "Off", 0, false); // script disabled and entity inactive
    off.SetActive(false);

    // Every script is created; only those on active entities get OnEnable.
    scene->OnRuntimeStart();
    CHECK((g_Life == std::vector<std::string>{ "A:Create", "B:Create", "Sleeper:Create", "Off:Create", "A:Enable", "B:Enable" }));
    g_Life.clear();
    scene->OnUpdateRuntime(0.016f);
    CHECK((g_Life == std::vector<std::string>{ "A:Start", "B:Start", "A:Update", "B:Update", "A:Late", "B:Late" }));

    // An ancestor switched off: OnDisable at once, then no OnUpdate / OnLateUpdate, timers pause.
    auto* sa = ScriptableEntity::GetScript<LifeScript>(a);
    CHECK(sa != nullptr);
    if (!sa) return;
    sa->Invoke(0.05f, [sa]() { sa->TimerFired++; });
    g_Life.clear();
    root.SetActive(false);
    CHECK((g_Life == std::vector<std::string>{ "A:Disable" }));
    CHECK(sa->IsEnabled() && scene->IsScriptEnabled(a)); // the script's own flag is untouched
    for (int i = 0; i < 10; i++)
        scene->OnUpdateRuntime(0.016f);
    CHECK(!HasLife("A:Update") && !HasLife("A:Late") && CountLife("B:Update") == 10);
    CHECK(sa->TimerFired == 0 && sa->IsInvoking());

    // Back on: OnEnable (no second OnStart) and the paused timer continues.
    g_Life.clear();
    root.SetActive(true);
    CHECK((g_Life == std::vector<std::string>{ "A:Enable" }));
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(0.016f);
    CHECK(CountLife("A:Update") == 5 && !HasLife("A:Start") && sa->TimerFired == 1);

    // Inactive at start: OnEnable when switched on, OnStart before its first OnUpdate.
    g_Life.clear();
    sleeper.SetActive(true);
    CHECK((g_Life == std::vector<std::string>{ "Sleeper:Enable" }));
    scene->OnUpdateRuntime(0.016f);
    CHECK(IndexOfLife("Sleeper:Start") >= 0 && IndexOfLife("Sleeper:Start") < IndexOfLife("Sleeper:Update"));

    // A disabled script stays off when its entity comes on; enabling it while the entity is
    // inactive waits for the entity.
    g_Life.clear();
    off.SetActive(true);
    CHECK(g_Life.empty());
    off.SetActive(false);
    scene->SetScriptEnabled(off, true);
    CHECK(g_Life.empty());
    off.SetActive(true);
    CHECK((g_Life == std::vector<std::string>{ "Off:Enable" }));

    // A direct write to TagComponent::Active is noticed at the start of the next frame.
    g_Life.clear();
    b.GetComponent<TagComponent>().Active = false;
    scene->OnUpdateRuntime(0.016f);
    CHECK(!g_Life.empty() && g_Life.front() == "B:Disable" && !HasLife("B:Update"));
    b.GetComponent<TagComponent>().Active = true;
    g_Life.clear();
    scene->OnUpdateRuntime(0.016f);
    CHECK(!g_Life.empty() && g_Life.front() == "B:Enable" && HasLife("B:Update"));

    // Reparenting below an inactive entity switches the script off at once.
    g_Life.clear();
    root.SetActive(false);
    scene->SetParent(b, root);
    CHECK((g_Life == std::vector<std::string>{ "A:Disable", "B:Disable" }));
    // Destroying an inactive entity: no second OnDisable.
    g_Life.clear();
    scene->DestroyEntity(root);
    CHECK((g_Life == std::vector<std::string>{ "A:Destroy", "B:Destroy" }));
    // Stop: inactive entities' scripts get only OnDestroy.
    g_Life.clear();
    sleeper.SetActive(false);
    scene->OnRuntimeStop();
    CHECK(CountLife("Sleeper:Disable") == 1 && CountLife("Sleeper:Destroy") == 1);
    CHECK(IndexOfLife("Sleeper:Disable") < IndexOfLife("Sleeper:Destroy"));
    CHECK(CountLife("Off:Disable") == 1 && CountLife("Off:Destroy") == 1);
}

TEST(ScriptsSwitchingEntitiesOff)
{
    g_Life.clear();
    g_ActiveFlips = 0;
    auto scene = CreateRef<Scene>();
    AddToggleScript(*scene, "Self", 1);
    AddToggleScript(*scene, "Switcher", 3);
    Entity target = AddLifeScript(*scene, "Target");
    Entity flip = AddToggleScript(*scene, "Flip", 2);

    scene->OnRuntimeStart();
    CHECK(g_ActiveFlips > 0 && g_ActiveFlips < 100); // the runaway is stopped by the nesting guard
    g_Life.clear();
    scene->OnUpdateRuntime(0.016f);
    // Its own entity off inside OnUpdate: OnDisable at once, no OnLateUpdate this frame.
    CHECK(IndexOfLife("Self:Update") >= 0 && IndexOfLife("Self:Disable") == IndexOfLife("Self:Update") + 1);
    CHECK(!HasLife("Self:Late"));
    // Another entity switched off before its turn: OnDisable, and no OnUpdate in the same frame.
    CHECK(HasLife("Target:Disable") && !HasLife("Target:Update") && !HasLife("Target:Late"));
    CHECK(!target.IsActive());
    g_Life.clear();
    for (int i = 0; i < 3; i++)
        scene->OnUpdateRuntime(0.016f);
    CHECK(!HasLife("Self:Update") && !HasLife("Target:Update") && CountLife("Switcher:Update") == 3);
    (void)flip;
    scene->OnRuntimeStop();
}

// Uses the script-side active API (task 046). Mode 1: in its first OnUpdate switches "Door"
// off and tries entities that are not in its scene; mode 2: answers the messages "Off" / "On"
// by switching its own entity (a script on an inactive entity still gets messages).
class ActiveApiScript : public ScriptableEntity
{
public:
    int Mode = 0;
    void OnEnable() override { Log("Enable"); }
    void OnDisable() override { Log("Disable"); }
    void OnUpdate(Timestep) override
    {
        Log("Update");
        if (Mode == 1 && !m_Done)
        {
            m_Done = true;
            SetActive(FindEntity("Door"), false);
            SetActive(Entity(), false);            // nothing to switch: ignored
            if (Foreign) SetActive(Foreign, false); // another scene's entity: ignored
        }
    }
    void OnMessage(const ScriptMessage& message) override
    {
        if (message.Name == "Off") SetActive(false);
        if (message.Name == "On") SetActive(true);
    }
    Entity Foreign;
private:
    void Log(const char* what) { g_Life.push_back(GetName() + ":" + what); }
    bool m_Done = false;
};
ZM_REGISTER_SCRIPT(ActiveApiScript, ZM_FIELD(ActiveApiScript, Mode))

static Entity AddActiveApiScript(Scene& scene, const std::string& name, int mode)
{
    Entity e = scene.CreateEntity(name);
    auto& nsc = e.AddComponent<NativeScriptComponent>();
    nsc.ClassName = "ActiveApiScript";
    ScriptFieldValue v; v.Set(mode);
    nsc.Fields["Mode"] = v;
    return e;
}

TEST(ScriptActiveApi)
{
    g_Life.clear();
    auto other = CreateRef<Scene>();
    Entity foreign = other->CreateEntity("Foreign");
    auto scene = CreateRef<Scene>();
    Entity manager = AddActiveApiScript(*scene, "Manager", 1);
    Entity door = AddActiveApiScript(*scene, "Door", 2);
    Entity knob = AddLifeScript(*scene, "Knob");
    scene->SetParent(knob, door, false);

    scene->OnRuntimeStart();
    auto* sm = ScriptableEntity::GetScript<ActiveApiScript>(manager);
    auto* sd = ScriptableEntity::GetScript<ActiveApiScript>(door);
    auto* sk = ScriptableEntity::GetScript<LifeScript>(knob);
    CHECK(sm && sd && sk);
    if (!sm || !sd || !sk) return;
    sm->Foreign = foreign;
    CHECK(sd->IsActive() && sd->IsActiveInHierarchy() && sd->IsActiveAndEnabled());

    // Another entity switched off from OnUpdate: its subtree gets OnDisable at once, parent first.
    g_Life.clear();
    scene->OnUpdateRuntime(0.016f);
    CHECK(IndexOfLife("Door:Disable") >= 0 && IndexOfLife("Knob:Disable") == IndexOfLife("Door:Disable") + 1);
    CHECK(IndexOfLife("Manager:Update") < IndexOfLife("Door:Disable"));
    CHECK(!HasLife("Door:Update") && !HasLife("Knob:Update"));
    CHECK(!door.IsActive() && !sd->IsActive() && !sd->IsActiveInHierarchy() && !sd->IsActiveAndEnabled());
    CHECK(sd->IsEnabled());                                       // the script's own flag stays on
    CHECK(sk->IsActive() && !sk->IsActiveInHierarchy() && !sk->IsActiveAndEnabled());
    CHECK(foreign.IsActive() && other->CountInactiveInHierarchy() == 0); // other scenes are left alone

    // A script on an inactive entity still gets messages and can switch itself back on.
    g_Life.clear();
    CHECK(scene->SendMessage(door, ScriptMessage{ "On" }));
    CHECK((g_Life == std::vector<std::string>{ "Door:Enable", "Knob:Enable" }));
    CHECK(sd->IsActiveAndEnabled() && sk->IsActiveInHierarchy());
    g_Life.clear();
    scene->OnUpdateRuntime(0.016f);
    CHECK(HasLife("Door:Update") && HasLife("Knob:Update"));
    // ...and off again: the same calls in the same order, nothing twice.
    g_Life.clear();
    scene->SendMessage(door, ScriptMessage{ "Off" });
    scene->SendMessage(door, ScriptMessage{ "Off" });
    CHECK((g_Life == std::vector<std::string>{ "Door:Disable", "Knob:Disable" }));

    // IsActiveAndEnabled needs both: the entity active and the script's own flag on.
    scene->SendMessage(door, ScriptMessage{ "On" });
    sd->SetEnabled(false);
    CHECK(sd->IsActiveInHierarchy() && !sd->IsActiveAndEnabled());
    sd->SetEnabled(true);
    CHECK(sd->IsActiveAndEnabled());
    scene->OnRuntimeStop();
}

// Records in OnEnable / OnDisable whether its first child still / already has a 2D body.
static std::vector<std::string> g_BodySeen;
class ChildBodyScript : public ScriptableEntity
{
public:
    void OnEnable() override { Record("Enable"); }
    void OnDisable() override { Record("Disable"); }
private:
    void Record(const char* what)
    {
        Entity child = GetChild(0);
        auto* rb = child ? child.TryGetComponent<Rigidbody2DComponent>() : nullptr;
        g_BodySeen.push_back(std::string(what) + (rb && rb->RuntimeBody ? ":body" : ":none"));
    }
};
ZM_REGISTER_SCRIPT(ChildBodyScript)

TEST(ActiveOnEnableOnDisable)
{
    g_Life.clear();
    g_BodySeen.clear();
    auto scene = CreateRef<Scene>();
    Entity root = AddLifeScript(*scene, "Root");
    Entity child = AddLifeScript(*scene, "Child");
    Entity grandchild = AddLifeScript(*scene, "Grandchild");
    scene->SetParent(child, root, false);
    scene->SetParent(grandchild, child, false);
    Entity holder = scene->CreateEntity("Holder");
    holder.AddComponent<NativeScriptComponent>().ClassName = "ChildBodyScript";
    Entity crate = scene->CreateEntity("Crate");
    crate.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    crate.AddComponent<BoxCollider2DComponent>();
    scene->SetParent(crate, holder, false);
    Entity tmpl = AddLifeScript(*scene, "Template");
    tmpl.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    tmpl.AddComponent<BoxCollider2DComponent>();
    tmpl.SetActive(false);

    scene->OnRuntimeStart();
    CHECK((g_BodySeen == std::vector<std::string>{ "Enable:body" }));
    scene->OnUpdateRuntime(0.016f);

    // A whole subtree: OnDisable parent first, OnEnable parent first; nothing delivered twice.
    g_Life.clear();
    root.SetActive(false);
    root.SetActive(false);
    CHECK((g_Life == std::vector<std::string>{ "Root:Disable", "Child:Disable", "Grandchild:Disable" }));
    g_Life.clear();
    root.SetActive(true);
    CHECK((g_Life == std::vector<std::string>{ "Root:Enable", "Child:Enable", "Grandchild:Enable" }));
    // Off and on again within one frame: both calls, then updates as before (no second OnStart).
    g_Life.clear();
    child.SetActive(false);
    child.SetActive(true);
    CHECK((g_Life == std::vector<std::string>{ "Child:Disable", "Grandchild:Disable", "Child:Enable", "Grandchild:Enable" }));
    scene->OnUpdateRuntime(0.016f);
    CHECK(CountLife("Child:Update") == 1 && !HasLife("Child:Start"));

    // OnDisable still sees the children's bodies (they leave after every OnDisable); OnEnable
    // already sees them (they come back before any OnEnable).
    g_BodySeen.clear();
    holder.SetActive(false);
    CHECK(crate.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
    holder.SetActive(true);
    CHECK((g_BodySeen == std::vector<std::string>{ "Disable:body", "Enable:body" }));

    // While paused (editor) the calls still come at once; updates wait for the game to go on.
    g_Life.clear();
    scene->SetPaused(true);
    root.SetActive(false);
    CHECK((g_Life == std::vector<std::string>{ "Root:Disable", "Child:Disable", "Grandchild:Disable" }));
    root.SetActive(true);
    scene->OnUpdateRuntime(0.016f);
    CHECK(!HasLife("Root:Update"));
    scene->SetPaused(false);

    // An inactive template: its clone gets OnCreate only, and switched on right away it gets its
    // body and OnEnable at once, then OnStart before its first OnUpdate.
    g_Life.clear();
    Entity clone = scene->Instantiate(tmpl);
    CHECK((g_Life == std::vector<std::string>{ "Template:Create" }));
    CHECK(!clone.IsActive() && clone.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
    clone.SetName("Clone");
    clone.SetActive(true);
    CHECK((g_Life == std::vector<std::string>{ "Template:Create", "Clone:Enable" }));
    CHECK(clone.GetComponent<Rigidbody2DComponent>().RuntimeBody != nullptr);
    CHECK(tmpl.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr); // the template stays off
    g_Life.clear();
    scene->OnUpdateRuntime(0.016f);
    CHECK(IndexOfLife("Clone:Start") >= 0 && IndexOfLife("Clone:Start") < IndexOfLife("Clone:Update"));
    CHECK(!HasLife("Template:Update"));

    // An active copy with an inactive child: the parent is enabled, the child only created.
    g_Life.clear();
    Entity spawnRoot = AddLifeScript(*scene, "SpawnRoot");
    Entity spawnChild = AddLifeScript(*scene, "SpawnChild");
    scene->SetParent(spawnChild, spawnRoot, false);
    spawnChild.SetActive(false);
    g_Life.clear();
    Entity spawned = scene->Instantiate(spawnRoot);
    CHECK((g_Life == std::vector<std::string>{ "SpawnRoot:Create", "SpawnChild:Create", "SpawnRoot:Enable" }));
    g_Life.clear();
    scene->FindChild(spawned, "SpawnChild").SetActive(true);
    CHECK((g_Life == std::vector<std::string>{ "SpawnChild:Enable" }));

    // Destroying an active subtree: OnDisable before OnDestroy for each.
    g_Life.clear();
    scene->DestroyEntity(root);
    CHECK(IndexOfLife("Root:Disable") >= 0 && IndexOfLife("Root:Disable") < IndexOfLife("Root:Destroy"));
    CHECK(CountLife("Grandchild:Disable") == 1 && CountLife("Grandchild:Destroy") == 1);
    scene->OnRuntimeStop();
}

TEST(ActiveStateSerialization)
{
    auto scene = CreateRef<Scene>();
    Entity level = scene->CreateEntity("Level");
    Entity door = scene->CreateEntity("Door");
    Entity knob = scene->CreateEntity("Knob");
    Entity lamp = scene->CreateEntity("Lamp");
    scene->SetParent(door, level, false);
    scene->SetParent(knob, door, false);
    door.SetActive(false);
    lamp.SetActive(false);

    // On disk: only the switched-off entities carry the key, and loading gives the same state.
    auto dir = std::filesystem::temp_directory_path() / ("zamin-active-" + std::to_string((uint64_t)UUID()));
    std::filesystem::create_directories(dir);
    CHECK(SceneSerializer(scene).Serialize(dir / "Level.zscene"));
    std::string text = FileSystem::ReadTextFile(dir / "Level.zscene");
    size_t keys = 0;
    for (size_t at = text.find("\"Active\""); at != std::string::npos; at = text.find("\"Active\"", at + 1))
        keys++;
    CHECK(keys == 2);
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).Deserialize(dir / "Level.zscene"));
    CHECK(loaded->FindEntityByName("Level").IsActive() && !loaded->FindEntityByName("Door").IsActive());
    CHECK(loaded->FindEntityByName("Knob").IsActive() && !loaded->FindEntityByName("Knob").IsActiveInHierarchy());
    CHECK(!loaded->FindEntityByName("Lamp").IsActive() && loaded->CountInactiveInHierarchy() == 3);
    CHECK(SceneSerializer(loaded).SerializeToString() == SceneSerializer(scene).SerializeToString()); // stable (undo snapshots)

    // Hand-edited files: 0 / 1 work like false / true, anything else keeps the default (active).
    {
        auto edited = CreateRef<Scene>();
        std::string zero = text, odd = text;
        for (size_t at = zero.find("\"Active\": false"); at != std::string::npos; at = zero.find("\"Active\": false"))
            zero.replace(at, 15, "\"Active\": 0");
        CHECK(SceneSerializer(edited).DeserializeFromString(zero));
        CHECK(!edited->FindEntityByName("Door").IsActive() && edited->CountInactiveInHierarchy() == 3);
        for (size_t at = odd.find("\"Active\": false"); at != std::string::npos; at = odd.find("\"Active\": false"))
            odd.replace(at, 15, "\"Active\": \"no\"");
        auto broken = CreateRef<Scene>();
        CHECK(SceneSerializer(broken).DeserializeFromString(odd));
        CHECK(broken->FindEntityByName("Door").IsActive() && broken->CountInactiveInHierarchy() == 0);
    }

    // The play-mode copy is independent of the scene it was made from.
    auto runtime = Scene::Copy(scene);
    CHECK(!runtime->FindEntityByName("Door").IsActive() && runtime->CountInactiveInHierarchy() == 3);
    runtime->FindEntityByName("Door").SetActive(true);
    CHECK(!door.IsActive() && runtime->CountInactiveInHierarchy() == 1);

    // Copy / paste (new UUIDs) and prefab files keep the flags of the whole subtree.
    std::string clip = SceneSerializer::SerializeEntities(*scene, { level, door, knob });
    auto pasted = SceneSerializer::DeserializeEntities(*scene, clip, true);
    CHECK(pasted.size() == 3 && pasted[0].IsActive() && !pasted[1].IsActive() && !pasted[2].IsActiveInHierarchy());
    CHECK(pasted[1].GetUUID() != door.GetUUID());
    CHECK(scene->SavePrefab(level, dir / "Level.zprefab"));
    auto game = CreateRef<Scene>();
    Entity instance = game->InstantiatePrefab(dir / "Level.zprefab");
    CHECK(instance && instance.IsActive());
    CHECK(!game->FindChild(instance, "Door").IsActive() && !game->FindChild(instance, "Knob").IsActiveInHierarchy());

    // A prefab whose root is off, spawned into a running scene: no bodies until switched on.
    Entity crate = scene->CreateEntity("Crate");
    crate.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    crate.AddComponent<BoxCollider2DComponent>();
    crate.SetActive(false);
    CHECK(scene->SavePrefab(crate, dir / "Crate.zprefab"));
    CHECK(FileSystem::ReadTextFile(dir / "Crate.zprefab").find("\"Active\": false") != std::string::npos);
    game->OnRuntimeStart();
    Entity spawned = game->InstantiatePrefab(dir / "Crate.zprefab");
    CHECK(spawned && !spawned.IsActive() && spawned.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
    spawned.SetActive(true);
    CHECK(spawned.GetComponent<Rigidbody2DComponent>().RuntimeBody != nullptr);
    game->OnRuntimeStop();
    std::filesystem::remove_all(dir);
}

TEST(SelectionSummaryInactive)
{
    auto scene = CreateRef<Scene>();
    Entity group = scene->CreateEntity("Group");
    Entity member = scene->CreateEntity("Member");
    Entity free = scene->CreateEntity("Free");
    scene->SetParent(member, group, false);
    EntitySelection selection;
    selection.Set({ member, free }, member);
    SelectionSummary s = SummarizeSelection(*scene, selection);
    CHECK(!s.Inactive && !s.InactiveSelf && s.InactiveCount == 0);
    group.SetActive(false);
    s = SummarizeSelection(*scene, selection);
    CHECK(s.Inactive && !s.InactiveSelf && s.InactiveCount == 1); // through its parent
    member.SetActive(false);
    free.SetActive(false);
    s = SummarizeSelection(*scene, selection);
    CHECK(s.Inactive && s.InactiveSelf && s.InactiveCount == 2);
}

// ------------------------------------------------------------------ M03: what else an inactive entity stops
TEST(InactiveParticlesStop)
{
    auto scene = CreateRef<Scene>();
    Entity group = scene->CreateEntity("Group");
    Entity sparks = scene->CreateEntity("Sparks");
    scene->SetParent(sparks, group, false);
    auto& ps = sparks.AddComponent<ParticleSystemComponent>();
    ps.EmissionRate = 100.0f;
    ps.Burst = 7;
    ps.LifetimeMin = ps.LifetimeMax = 5.0f;
    Entity sleeping = scene->CreateEntity("Sleeping"); // inactive when play starts
    sleeping.AddComponent<ParticleSystemComponent>().Burst = 3;
    sleeping.SetActive(false);

    scene->OnRuntimeStart();
    for (int i = 0; i < 10; i++)
        scene->OnUpdateRuntime(0.02f);
    CHECK(ps.Particles.size() >= 7 + 15 && ps.BurstDone);
    auto& sleepingPs = sleeping.GetComponent<ParticleSystemComponent>();
    CHECK(sleepingPs.Particles.empty() && !sleepingPs.BurstDone); // never started

    // An ancestor switched off: the live particles go at once and nothing is emitted meanwhile,
    // not even by EmitParticles.
    group.SetActive(false);
    CHECK(ps.Particles.empty() && !ps.BurstDone && ps.EmitAccumulator == 0.0f);
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(0.02f);
    scene->EmitParticles(sparks, 20);
    CHECK(ps.Particles.empty());

    // Back on: the system starts over, Burst included.
    group.SetActive(true);
    scene->OnUpdateRuntime(0.02f);
    CHECK(ps.Particles.size() >= 7 && ps.Particles.size() <= 7 + 3 && ps.BurstDone);
    scene->EmitParticles(sparks, 4);
    CHECK(ps.Particles.size() >= 11);
    sleeping.SetActive(true);
    scene->OnUpdateRuntime(0.02f);
    CHECK(sleepingPs.Particles.size() == 3 && sleepingPs.BurstDone);

    // A direct write to the flag is noticed on the next update.
    sparks.GetComponent<TagComponent>().Active = false;
    scene->OnUpdateRuntime(0.02f);
    CHECK(ps.Particles.empty() && !ps.BurstDone);
    scene->OnRuntimeStop();
}

static size_t g_AudioSeenInEnable = 0, g_AudioSeenInDisable = 0;
static int g_AudioPlayInDisable = -1;

class AudioProbeScript : public ScriptableEntity
{
public:
    void OnEnable() override { if (IsAudioPlaying()) g_AudioSeenInEnable++; }
    void OnDisable() override
    {
        if (IsAudioPlaying()) g_AudioSeenInDisable++;
        g_AudioPlayInDisable = PlayAudio() ? 1 : 0; // refused: the entity is already inactive
    }
};
ZM_REGISTER_SCRIPT(AudioProbeScript)

// 16-bit mono PCM .wav with a quiet tone.
static void WriteTestWav(const std::filesystem::path& path, float seconds)
{
    const uint32_t rate = 22050, frames = (uint32_t)(rate * seconds), dataSize = frames * 2;
    std::string bytes;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) bytes.push_back((char)((v >> (8 * i)) & 0xff)); };
    auto u16 = [&](uint16_t v) { bytes.push_back((char)(v & 0xff)); bytes.push_back((char)(v >> 8)); };
    bytes += "RIFF"; u32(36 + dataSize); bytes += "WAVE";
    bytes += "fmt "; u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    bytes += "data"; u32(dataSize);
    for (uint32_t i = 0; i < frames; i++)
        u16((uint16_t)(int16_t)(std::sin((float)i * 0.06f) * 6000.0f));
    FileSystem::WriteTextFile(path, bytes);
}

TEST(InactiveAudioSourcesStop)
{
    // An engine without an output device: sounds really load and play, nothing is heard.
    bool wasAvailable = AudioEngine::IsAvailable();
    AudioEngine::Init(true);
    CHECK(AudioEngine::IsAvailable());
    if (!AudioEngine::IsAvailable())
        return;
    auto dir = std::filesystem::temp_directory_path() / ("zamin-audio-" + std::to_string((uint64_t)UUID()));
    std::filesystem::create_directories(dir);
    std::string wav = (dir / "tone.wav").string();
    WriteTestWav(wav, 1.0f);

    auto scene = CreateRef<Scene>();
    auto source = [&](const std::string& name, bool playOnStart) {
        Entity e = scene->CreateEntity(name);
        auto& a = e.AddComponent<AudioSourceComponent>();
        a.Path = wav;
        a.Loop = true;
        a.PlayOnStart = playOnStart;
        return e;
    };
    Entity music = source("Music", true);
    music.AddComponent<NativeScriptComponent>().ClassName = "AudioProbeScript";
    Entity group = scene->CreateEntity("Group");
    Entity hum = source("Hum", true);
    scene->SetParent(hum, group, false);
    Entity later = source("Later", true); // inactive when play starts
    later.SetActive(false);
    Entity manual = source("Manual", false);
    Entity spatial = source("Spatial", true);
    spatial.GetComponent<AudioSourceComponent>().Spatial = true;
    spatial.GetTransform().Translation = { 3.0f, 0.0f, 0.0f };

    // Edit mode: no sound.
    CHECK(!scene->PlayAudio(manual) && !scene->IsAudioPlaying(manual));

    g_AudioSeenInEnable = g_AudioSeenInDisable = 0;
    g_AudioPlayInDisable = -1;
    scene->OnRuntimeStart();
    CHECK(scene->IsAudioPlaying(music) && scene->IsAudioPlaying(hum) && scene->IsAudioPlaying(spatial));
    CHECK(!scene->IsAudioPlaying(later) && later.GetComponent<AudioSourceComponent>().RuntimeHandle == 0);
    CHECK(!scene->IsAudioPlaying(manual));
    CHECK(AudioEngine::GetPlayingCount() == 3);
    CHECK(g_AudioSeenInEnable == 1); // the source plays before the script's OnEnable

    // Switched off: the script's OnDisable still hears it, then it stops and its handle goes.
    music.SetActive(false);
    CHECK(g_AudioSeenInDisable == 1 && g_AudioPlayInDisable == 0);
    CHECK(!scene->IsAudioPlaying(music) && music.GetComponent<AudioSourceComponent>().RuntimeHandle == 0);
    CHECK(!scene->PlayAudio(music)); // refused while inactive
    // Through an ancestor.
    group.SetActive(false);
    CHECK(!scene->IsAudioPlaying(hum) && AudioEngine::GetPlayingCount() == 1);
    scene->OnUpdateRuntime(0.016f);
    CHECK(!scene->IsAudioPlaying(music) && !scene->IsAudioPlaying(hum));

    // Back on: PlayOnStart sources start again; the others stay silent.
    music.SetActive(true);
    group.SetActive(true);
    later.SetActive(true);
    CHECK(scene->IsAudioPlaying(music) && scene->IsAudioPlaying(hum) && scene->IsAudioPlaying(later));
    CHECK(g_AudioSeenInEnable == 2);
    CHECK(scene->PlayAudio(manual) && scene->IsAudioPlaying(manual));
    manual.SetActive(false);
    CHECK(!scene->IsAudioPlaying(manual));
    manual.SetActive(true);
    CHECK(!scene->IsAudioPlaying(manual)); // not PlayOnStart: it waits for PlayAudio
    scene->StopAudio(hum);
    CHECK(!scene->IsAudioPlaying(hum) && hum.GetComponent<AudioSourceComponent>().RuntimeHandle == 0);
    CHECK(AudioEngine::GetPlayingCount() == 3); // music, later, spatial

    // Spawned: an active clone plays at once, a clone of an inactive template once switched on.
    Entity clone = scene->Instantiate(spatial);
    CHECK(scene->IsAudioPlaying(clone) && scene->IsAudioPlaying(spatial));
    CHECK(clone.GetComponent<AudioSourceComponent>().RuntimeHandle != spatial.GetComponent<AudioSourceComponent>().RuntimeHandle);
    spatial.SetActive(false);
    Entity sleepingClone = scene->Instantiate(spatial);
    CHECK(!scene->IsAudioPlaying(sleepingClone));
    sleepingClone.SetActive(true);
    CHECK(scene->IsAudioPlaying(sleepingClone));

    // A direct write to the flag stops it at the start of the next frame; destroying stops it too.
    later.GetComponent<TagComponent>().Active = false;
    scene->OnUpdateRuntime(0.016f);
    CHECK(!scene->IsAudioPlaying(later));
    size_t before = AudioEngine::GetPlayingCount();
    scene->DestroyEntity(clone);
    CHECK(AudioEngine::GetPlayingCount() == before - 1);

    scene->OnRuntimeStop();
    CHECK(AudioEngine::GetPlayingCount() == 0);
    if (!wasAvailable)
        AudioEngine::Shutdown();
    std::filesystem::remove_all(dir);
}

TEST(InactiveAnimatorsPause)
{
    auto scene = CreateRef<Scene>();
    Entity rig = scene->CreateEntity("Rig");
    Entity hero = scene->CreateEntity("Hero");
    scene->SetParent(hero, rig, false);
    auto& anim = hero.AddComponent<AnimatorComponent>();
    anim.Parameters = { { "Attack", AnimatorParameter::Type::Trigger, 0.0f } };
    anim.States = { { "Idle", "", 1.0f, true }, { "Attack", "", 1.0f, false } };
    AnimatorTransition attack{ -1, 1 };
    attack.Conditions = { { "Attack", AnimatorCondition::Op::Triggered } };
    anim.Transitions = { attack };
    auto& sprite = hero.AddComponent<SpriteAnimationComponent>();
    Entity sleeper = scene->CreateEntity("Sleeper"); // inactive when play starts
    auto& sleeperAnim = sleeper.AddComponent<AnimatorComponent>();
    sleeper.SetActive(false);

    scene->OnRuntimeStart();
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(0.1f);
    CHECK_NEAR(anim.Time, 0.5f, 1e-4f);
    CHECK_NEAR(sprite.Time, 0.5f, 1e-4f);
    CHECK(std::string(anim.GetCurrentStateName()) == "Idle");
    CHECK(sleeperAnim.Time == 0.0f);

    // An ancestor switched off: time, state and pending triggers wait.
    rig.SetActive(false);
    anim.SetTrigger("Attack");
    for (int i = 0; i < 5; i++)
        scene->OnUpdateRuntime(0.1f);
    CHECK_NEAR(anim.Time, 0.5f, 1e-4f);
    CHECK_NEAR(sprite.Time, 0.5f, 1e-4f);
    CHECK(std::string(anim.GetCurrentStateName()) == "Idle" && anim.GetBool("Attack"));

    // Back on: it continues from there (and takes the pending trigger).
    rig.SetActive(true);
    scene->OnUpdateRuntime(0.1f);
    CHECK(std::string(anim.GetCurrentStateName()) == "Attack" && !anim.GetBool("Attack"));
    CHECK_NEAR(sprite.Time, 0.6f, 1e-4f);
    sleeper.SetActive(true);
    scene->OnUpdateRuntime(0.1f);
    CHECK_NEAR(sleeperAnim.Time, 0.1f, 1e-4f);
    scene->OnRuntimeStop();
}

static int g_ButtonClicks = 0;
class ClickCountScript : public ScriptableEntity
{
public:
    void OnClick() override { g_ButtonClicks++; }
};
ZM_REGISTER_SCRIPT(ClickCountScript)

TEST(InactiveUIButtons)
{
    auto scene = CreateRef<Scene>();
    scene->OnViewportResize(800, 600);
    auto button = [&](const std::string& name, int order) {
        Entity e = scene->CreateEntity(name);
        auto& ui = e.AddComponent<UITransformComponent>();
        ui.Anchor = UITransformComponent::AnchorPoint::Center;
        ui.Position = { 0.0f, 0.0f };
        ui.Size = { 200.0f, 60.0f };
        ui.Order = order;
        e.AddComponent<UIButtonComponent>();
        e.AddComponent<NativeScriptComponent>().ClassName = "ClickCountScript";
        return e;
    };
    Entity back = button("Back", 0);
    Entity panel = scene->CreateEntity("Panel");
    Entity front = button("Front", 1); // on top of Back, inside a panel
    scene->SetParent(front, panel, false);
    auto& backButton = back.GetComponent<UIButtonComponent>();
    auto& frontButton = front.GetComponent<UIButtonComponent>();
    const glm::vec2 center{ 400.0f, 300.0f };
    auto frame = [&](bool down) {
        scene->SetViewportMouse(center, down);
        scene->OnUpdateRuntime(0.016f);
    };

    g_ButtonClicks = 0;
    scene->OnRuntimeStart();
    frame(false);
    CHECK(frontButton.Hovered && !backButton.Hovered);

    // The front button switched off (through its panel): the mouse reaches the one below.
    panel.SetActive(false);
    frame(false);
    CHECK(!frontButton.Hovered && backButton.Hovered);
    CHECK(scene->CollectRenderSkipped().contains(front)); // not drawn either
    frame(true);
    CHECK(backButton.Pressed && !frontButton.Pressed);
    frame(false);
    CHECK(backButton.Clicked && !frontButton.Clicked && g_ButtonClicks == 1);

    // A press in progress is cancelled when its button goes inactive: no click later.
    panel.SetActive(true);
    frame(false);
    frame(true);
    CHECK(frontButton.Pressed && frontButton.Hovered);
    front.SetActive(false);
    CHECK(!frontButton.Pressed && !frontButton.Hovered);
    frame(true);
    CHECK(!frontButton.Pressed && !backButton.Pressed); // Back was not pressed: no new press
    front.SetActive(true);
    frame(false);
    CHECK(!frontButton.Clicked && !backButton.Clicked && g_ButtonClicks == 1);

    // Active again: clickable as before.
    frame(true);
    frame(false);
    CHECK(frontButton.Clicked && g_ButtonClicks == 2);
    scene->OnRuntimeStop();
}

TEST(InactiveCamerasDoNotCount)
{
    auto scene = CreateRef<Scene>();
    scene->OnViewportResize(800, 600);
    auto camera = [&](const std::string& name, float x, const glm::vec4& clear) {
        Entity e = scene->CreateEntity(name);
        auto& c = e.AddComponent<CameraComponent>();
        c.Camera.SetProjectionType(SceneCamera::ProjectionType::Orthographic);
        c.Camera.SetViewportSize(800, 600);
        c.ClearColor = clear;
        e.GetTransform().Translation = { x, 0.0f, 10.0f };
        return e;
    };
    Entity main = camera("Main", -10.0f, { 1.0f, 0.0f, 0.0f, 1.0f });
    Entity rig = scene->CreateEntity("CutsceneRig");
    Entity cutscene = camera("Cutscene", 10.0f, { 0.0f, 1.0f, 0.0f, 1.0f }); // also Primary, created later
    scene->SetParent(cutscene, rig, false);
    Entity spare = camera("Spare", 0.0f, { 0.0f, 0.0f, 1.0f, 1.0f });
    spare.GetComponent<CameraComponent>().Primary = false;

    // Cameras are switched by switching their entities (or a parent).
    rig.SetActive(false);
    CHECK(scene->GetPrimaryCameraEntity() == main);
    CHECK_NEAR(scene->GetClearColor().r, 1.0f, 1e-6f);
    CHECK_NEAR(scene->ScreenToWorld({ 400.0f, 300.0f }).x, -10.0f, 1e-3f);
    rig.SetActive(true);
    main.SetActive(false);
    CHECK(scene->GetPrimaryCameraEntity() == cutscene);
    CHECK_NEAR(scene->GetClearColor().g, 1.0f, 1e-6f);
    CHECK_NEAR(scene->ScreenToWorld({ 400.0f, 300.0f }).x, 10.0f, 1e-3f);

    // No active primary camera: none (a non-primary one does not step in); the editor can still
    // find the switched-off one.
    rig.SetActive(false);
    CHECK(!scene->GetPrimaryCameraEntity());
    CHECK(scene->GetPrimaryCameraEntity(true) == main || scene->GetPrimaryCameraEntity(true) == cutscene);
    CHECK(scene->ScreenToWorld({ 400.0f, 300.0f }) == glm::vec3(0.0f));
    CHECK_NEAR(scene->GetClearColor().b, 0.11f, 1e-6f); // the default background

    // The runtime copy sees the same (play mode starts from Scene::Copy). Updating it renders
    // nothing headless as long as no camera is active.
    auto copy = Scene::Copy(scene);
    CHECK(!copy->GetPrimaryCameraEntity() && copy->GetPrimaryCameraEntity(true));
    if (!copy->GetPrimaryCameraEntity()) // (would render, which needs a GPU)
    {
        copy->OnRuntimeStart();
        copy->OnUpdateRuntime(0.016f);
        CHECK(!copy->GetPrimaryCameraEntity());
        copy->OnRuntimeStop();
    }
    main.SetActive(true);
    CHECK(scene->GetPrimaryCameraEntity() == main);
}

TEST(InactiveLightsDoNotCount)
{
    auto scene = CreateRef<Scene>();
    Entity sunA = scene->CreateEntity("SunA");
    sunA.AddComponent<DirectionalLightComponent>();
    Entity sunB = scene->CreateEntity("SunB");
    sunB.AddComponent<DirectionalLightComponent>();

    // The first sun in scene order lights the scene; the other one is extra (unused).
    Scene::LightSet set = scene->CollectLights(glm::vec3(0.0f));
    CHECK(set.Sun == (entt::entity)sunA && set.ExtraSuns == 1 && set.InactiveLights == 0 && set.PointLights.empty());
    Entity first = sunA, second = sunB;
    CHECK(scene->GetSunEntity() == first);
    // Scene order, not creation order of the light components: moved first, SunB is the sun.
    CHECK(scene->MoveEntityNextTo(sunB, sunA, false));
    CHECK(scene->CollectLights(glm::vec3(0.0f)).Sun == (entt::entity)sunB && scene->GetSunEntity() == sunB);
    CHECK(scene->MoveEntityNextTo(sunA, sunB, false));
    CHECK(scene->GetSunEntity() == sunA);
    // Switched off: the other one takes its place; both off: no sun.
    first.SetActive(false);
    set = scene->CollectLights(glm::vec3(0.0f));
    CHECK(set.Sun == (entt::entity)second && set.ExtraSuns == 0 && set.InactiveLights == 1);
    CHECK(scene->GetSunEntity() == second);
    second.SetActive(false);
    set = scene->CollectLights(glm::vec3(0.0f));
    CHECK(set.Sun == entt::null && set.InactiveLights == 2 && !scene->GetSunEntity());
    first.SetActive(true);
    second.SetActive(true);

    // 20 lamps in a row (x = 0, 10, ..., 190) below one group: 16 fit, the nearest to the view.
    Entity street = scene->CreateEntity("Street");
    std::vector<Entity> lamps;
    for (int i = 0; i < 20; i++)
    {
        Entity lamp = scene->CreateEntity("Lamp" + std::to_string(i));
        lamp.AddComponent<PointLightComponent>();
        lamp.GetTransform().Translation = { i * 10.0f, 3.0f, 0.0f };
        scene->SetParent(lamp, street, false);
        lamps.push_back(lamp);
    }
    auto uses = [&](const Scene::LightSet& s, int index) {
        return std::find(s.PointLights.begin(), s.PointLights.end(), (entt::entity)lamps[index]) != s.PointLights.end();
    };
    set = scene->CollectLights(glm::vec3(0.0f));
    CHECK(set.PointLights.size() == Renderer3D::MaxPointLights && set.DroppedPointLights == 4);
    CHECK(uses(set, 0) && uses(set, 15) && !uses(set, 16) && !uses(set, 19));
    CHECK(set.PointLights.front() == (entt::entity)lamps[0]); // nearest first
    set = scene->CollectLights(glm::vec3(190.0f, 0.0f, 0.0f));
    CHECK(uses(set, 19) && uses(set, 4) && !uses(set, 3) && !uses(set, 0));

    // Switched-off lamps take no slot: the next farther ones light the scene instead.
    lamps[1].SetActive(false);
    lamps[2].SetActive(false);
    lamps[3].SetActive(false);
    set = scene->CollectLights(glm::vec3(0.0f));
    CHECK(set.PointLights.size() == Renderer3D::MaxPointLights && set.DroppedPointLights == 1 && set.InactiveLights == 3);
    CHECK(!uses(set, 1) && !uses(set, 2) && !uses(set, 3) && uses(set, 18) && !uses(set, 19));
    // Up to 16: every one that counts, in pool order (nothing is sorted).
    for (int i = 10; i < 20; i++)
        lamps[i].SetActive(false);
    set = scene->CollectLights(glm::vec3(1000.0f, 0.0f, 0.0f));
    CHECK(set.PointLights.size() == 7 && set.DroppedPointLights == 0 && set.InactiveLights == 13);
    CHECK(uses(set, 0) && uses(set, 4) && uses(set, 9) && !uses(set, 10));
    // The group switched off: every lamp below it is out, whatever its own flag says.
    street.SetActive(false);
    set = scene->CollectLights(glm::vec3(0.0f));
    CHECK(set.PointLights.empty() && set.InactiveLights == 20 && set.Sun != entt::null);
    street.SetActive(true);
    for (Entity lamp : lamps)
        lamp.SetActive(true);

    // A direct write (inspector) counts after InvalidateActiveCache / on the next frame.
    lamps[0].GetComponent<TagComponent>().Active = false;
    scene->InvalidateActiveCache();
    set = scene->CollectLights(glm::vec3(0.0f));
    CHECK(!uses(set, 0) && uses(set, 16) && set.InactiveLights == 1);
    lamps[0].GetComponent<TagComponent>().Active = true;
    scene->InvalidateActiveCache();

    // Editor view: lamps hidden with the eye are left out there, but they are not inactive.
    scene->SetHiddenInEditor(lamps[0], true);
    SceneRenderOptions editorView;
    editorView.SkipEditorHidden = true;
    set = scene->CollectLights(glm::vec3(0.0f), editorView);
    CHECK(!uses(set, 0) && set.InactiveLights == 0);
    CHECK(uses(scene->CollectLights(glm::vec3(0.0f)), 0)); // the game sees it
    scene->SetHiddenInEditor(lamps[0], false);

    // The play-mode copy keeps the flags (and its own state from then on).
    first.SetActive(false);
    lamps[5].SetActive(false);
    auto runtime = Scene::Copy(scene);
    Scene::LightSet copied = runtime->CollectLights(glm::vec3(0.0f));
    CHECK(copied.InactiveLights == 2 && copied.Sun != entt::null);
    CHECK(Entity(copied.Sun, runtime.get()).GetName() == second.GetName());
    runtime->FindEntityByName("Lamp5").SetActive(true);
    CHECK(runtime->CollectLights(glm::vec3(0.0f)).InactiveLights == 1 && scene->CollectLights(glm::vec3(0.0f)).InactiveLights == 2);
}

TEST(InactiveTilemapCollision)
{
    g_Contacts.clear();
    auto scene = CreateRef<Scene>();
    Entity level = scene->CreateEntity("Level");
    Entity map = scene->CreateEntity("Map");
    scene->SetParent(map, level, false);
    map.GetTransform().Translation = { -5.0f, -1.0f, 0.0f }; // tiles cover x -5..5, y -1..0
    auto& tm = map.AddComponent<TilemapComponent>();
    tm.Width = 10;
    tm.Height = 1;
    tm.EnsureSize();
    for (int x = 0; x < 10; x++)
        tm.Set(x, 0, 0);
    AddContactLog(map);
    Entity crate = scene->CreateEntity("Crate"); // rests on the tiles
    crate.GetTransform().Translation = { 0.0f, 0.5f, 0.0f };
    crate.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    crate.AddComponent<BoxCollider2DComponent>();
    AddContactLog(crate);
    Entity shelf = scene->CreateEntity("Shelf"); // far away, with a crate asleep on it
    shelf.GetTransform().Translation = { 40.0f, -0.5f, 0.0f };
    shelf.GetTransform().Scale = { 4.0f, 1.0f, 1.0f };
    shelf.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
    shelf.AddComponent<BoxCollider2DComponent>();
    Entity sleeper = scene->CreateEntity("Sleeper");
    sleeper.GetTransform().Translation = { 40.0f, 0.5f, 0.0f };
    sleeper.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    sleeper.AddComponent<BoxCollider2DComponent>();

    scene->OnRuntimeStart();
    for (int i = 0; i < 120; i++) // everything comes to rest (and falls asleep)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(CountContact("Crate:CollisionBegin:Map") == 1 && CountContact("Map:CollisionBegin:Crate") == 1);
    const float resting = crate.GetTransform().Translation.y;
    CHECK_NEAR(resting, 0.5f, 0.05f);
    auto ray = [&](float x) { return scene->Raycast2D({ x, 5.0f }, { 0.0f, -1.0f }, 20.0f); };
    CHECK(ray(4.5f).Hit && ray(4.5f).EntityID == (uint64_t)map.GetUUID());

    // Switched off: the collision goes at once, the crate resting on it hears the contact end
    // (once), wakes up and falls; rays go through.
    g_Contacts.clear();
    map.SetActive(false);
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody == nullptr);
    CHECK((g_Contacts == std::vector<std::string>{ "Crate:CollisionEnd:Map" }));
    CHECK(!ray(4.5f).Hit);
    for (int i = 0; i < 30; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(crate.GetTransform().Translation.y < resting - 1.0f);
    CHECK(g_Contacts.size() == 1);
    CHECK_NEAR(map.GetTransform().Translation.y, -1.0f, 1e-6f);

    // Edited and moved while off: back on, the collision is built from the tiles as they are now,
    // where the tilemap is now. Its first tile appears in the sleeping crate's side: it wakes up
    // and is pushed out.
    tm.Set(9, 0, -1);
    map.GetTransform().Translation = { 40.3f, 0.0f, 0.0f }; // tile 0 covers x 40.3..41.3, y 0..1
    const float sleeperX = sleeper.GetTransform().Translation.x;
    CHECK_NEAR(sleeperX, 40.0f, 0.01f);
    map.SetActive(true);
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody != nullptr);
    CHECK(ray(48.8f).Hit && ray(48.8f).EntityID == (uint64_t)map.GetUUID());
    CHECK(!ray(49.8f).Hit && !ray(4.5f).Hit);
    for (int i = 0; i < 30; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(sleeper.GetTransform().Translation.x < sleeperX - 0.05f);

    // Its parent switched off and on: the collision follows the hierarchy.
    level.SetActive(false);
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody == nullptr && !ray(48.8f).Hit);
    map.SetActive(false);
    level.SetActive(true);
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody == nullptr); // still off itself
    // Collision switched off meanwhile: coming back builds nothing.
    tm.Collision = false;
    map.SetActive(true);
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody == nullptr && !ray(48.8f).Hit);
    scene->OnRuntimeStop();

    // A tilemap that is off when play starts gets no collision until it is switched on.
    tm.Collision = true;
    map.SetActive(false);
    scene->OnSimulationStart(); // (simulation: physics without scripts)
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody == nullptr);
    map.SetActive(true);
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody != nullptr);
    scene->OnSimulationStop();
    CHECK(map.GetComponent<TilemapComponent>().RuntimeBody == nullptr);
}

TEST(InactiveCharacterControllers)
{
    g_Contacts.clear();
    auto scene = CreateRef<Scene>();
    Entity ground = scene->CreateEntity("Ground");
    ground.GetTransform().Translation = { 0.0f, -0.5f, 0.0f };
    ground.GetTransform().Scale = { 40.0f, 1.0f, 40.0f };
    ground.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    ground.AddComponent<BoxCollider3DComponent>();
    AddContactLog(ground);
    Entity squad = scene->CreateEntity("Squad");
    Entity hero = scene->CreateEntity("Hero");
    scene->SetParent(hero, squad, false);
    hero.GetTransform().Translation = { 0.0f, 0.05f, 0.0f };
    hero.AddComponent<CharacterController3DComponent>();
    AddContactLog(hero);

    scene->OnRuntimeStart();
    PhysicsWorld3D* world = scene->GetPhysicsWorld3D();
    CHECK(world != nullptr);
    if (!world) return;
    auto& cc = hero.GetComponent<CharacterController3DComponent>();
    for (int i = 0; i < 30; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(cc.RuntimeID != 0xffffffff && cc.Grounded);
    CHECK(CountContact("Hero:CollisionBegin:Ground") == 1 && CountContact("Ground:CollisionBegin:Hero") == 1);
    auto ray = [&]() { return scene->Raycast3D({ -3.0f, 0.9f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 6.0f); };
    CHECK(ray().Hit && ray().EntityID == (uint64_t)hero.GetUUID()); // its capsule (inner body)

    // Switched off: out of the 3D world at once - nothing to hit, the ground hears the contact end
    // (once), no gravity, not grounded, no jumping.
    g_Contacts.clear();
    hero.SetActive(false);
    CHECK(cc.RuntimeID == 0xffffffff && !cc.Grounded);
    CHECK((g_Contacts == std::vector<std::string>{ "Ground:CollisionEnd:Hero" }));
    CHECK(!ray().Hit);
    CHECK(!cc.Jump(5.0f) && cc.VerticalVelocity == 0.0f);
    hero.GetTransform().Translation = { 2.0f, 3.0f, 0.0f }; // teleported while off
    for (int i = 0; i < 30; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(hero.GetTransform().Translation.x, 2.0f, 1e-6f);
    CHECK_NEAR(hero.GetTransform().Translation.y, 3.0f, 1e-6f);
    CHECK(g_Contacts.size() == 1 && cc.RuntimeID == 0xffffffff);

    // On again below a switched-off parent: still out. The parent moved and switched on: the
    // character appears at the hero's world position now, at rest (no speed kept from before).
    squad.SetActive(false);
    hero.SetActive(true);
    CHECK(cc.RuntimeID == 0xffffffff && !scene->IsActiveInHierarchy(hero));
    squad.GetTransform().Translation = { 1.0f, 0.0f, 0.0f };
    cc.VerticalVelocity = -50.0f; // whatever was left: a new character starts at rest
    squad.SetActive(true);
    CHECK(cc.RuntimeID != 0xffffffff);
    if (cc.RuntimeID != 0xffffffff)
    {
        CHECK_NEAR(world->GetCharacterPosition(cc.RuntimeID).x, 3.0f, 1e-4f);
        CHECK_NEAR(world->GetCharacterPosition(cc.RuntimeID).y, 3.0f, 1e-4f);
    }
    CHECK(cc.VerticalVelocity == 0.0f && !cc.Grounded);
    scene->OnUpdateRuntime(1.0f / 60.0f);
    float y = scene->GetWorldPosition(hero).y;
    CHECK(y < 3.0f && y > 2.95f);
    CHECK_NEAR(scene->GetWorldPosition(hero).x, 3.0f, 1e-3f);
    for (int i = 0; i < 90; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(cc.Grounded && std::abs(scene->GetWorldPosition(hero).y) < 0.05f);
    CHECK(CountContact("Hero:CollisionBegin:Ground") == 1 && CountContact("Ground:CollisionBegin:Hero") == 1);
    CHECK(cc.Jump(5.0f));
    scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(scene->GetWorldPosition(hero).y > 0.01f);
    for (int i = 0; i < 90; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);

    // The ground switched off below it: it falls, and hears the contact end once.
    g_Contacts.clear();
    ground.SetActive(false);
    CHECK(CountContact("Hero:CollisionEnd:Ground") == 1 && g_Contacts.size() == 1);
    for (int i = 0; i < 30; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(!cc.Grounded && scene->GetWorldPosition(hero).y < -0.5f);
    scene->OnRuntimeStop();
    CHECK(cc.RuntimeID == 0xffffffff);

    // Simulation mode (physics without scripts): an inactive hero has no character either.
    hero.SetActive(false);
    ground.SetActive(true);
    scene->OnSimulationStart();
    CHECK(scene->GetPhysicsWorld3D() && cc.RuntimeID == 0xffffffff);
    hero.SetActive(true);
    CHECK(cc.RuntimeID != 0xffffffff);
    scene->OnSimulationStop();
}

// Something a character touches is switched off and on again before the next physics step (a
// "reset", or an inspector click while paused): the scripts hear the contact end at once, then
// begin again with the new body, and the later real end is heard as well.
TEST(CharacterContactsSwitchedOffAndOnInOneFrame)
{
    g_Contacts.clear();
    auto scene = CreateRef<Scene>();
    Entity ground = scene->CreateEntity("Ground");
    ground.GetTransform().Translation = { 0.0f, -0.5f, 0.0f };
    ground.GetTransform().Scale = { 6.0f, 1.0f, 6.0f }; // x in [-3, 3]
    ground.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    ground.AddComponent<BoxCollider3DComponent>();
    AddContactLog(ground);
    Entity zone = scene->CreateEntity("Zone");
    zone.GetTransform().Translation = { 0.0f, 1.0f, 0.0f };
    zone.GetTransform().Scale = { 2.0f, 2.0f, 2.0f }; // x in [-1, 1]
    zone.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    zone.AddComponent<BoxCollider3DComponent>().IsSensor = true;
    AddContactLog(zone);
    Entity hero = scene->CreateEntity("Hero");
    hero.GetTransform().Translation = { 0.0f, 0.05f, 0.0f };
    auto& cc = hero.AddComponent<CharacterController3DComponent>();
    AddContactLog(hero);

    scene->OnRuntimeStart();
    auto run = [&](int frames) {
        for (int i = 0; i < frames; i++)
            scene->OnUpdateRuntime(1.0f / 60.0f);
    };
    run(30);
    CHECK(cc.Grounded);
    CHECK(CountContact("Hero:CollisionBegin:Ground") == 1 && CountContact("Hero:TriggerBegin:Zone") == 1);

    for (Entity e : { ground, zone })
    {
        const std::string kind = e == ground ? "Collision" : "Trigger";
        g_Contacts.clear();
        e.SetActive(false);
        e.SetActive(true);
        CHECK((g_Contacts == std::vector<std::string>{ "Hero:" + kind + "End:" + e.GetName() }));
        g_Contacts.clear();
        run(30);
        CHECK(cc.Grounded);
        CHECK(CountContact("Hero:" + kind + "Begin:" + e.GetName()) == 1);
        CHECK(CountContact(e.GetName() + ":" + kind + "Begin:Hero") == 1);
        CHECK(g_Contacts.size() == 2);
    }

    // The real end (walking out of the zone and off the ground's edge) is heard too.
    g_Contacts.clear();
    cc.Move({ 4.0f, 0.0f, 0.0f });
    run(120);
    CHECK(!cc.Grounded && scene->GetWorldPosition(hero).x > 3.0f);
    CHECK(CountContact("Hero:CollisionEnd:Ground") == 1 && CountContact("Ground:CollisionEnd:Hero") == 1);
    CHECK(CountContact("Hero:TriggerEnd:Zone") == 1 && CountContact("Zone:TriggerEnd:Hero") == 1);
    CHECK(CountContact("Hero:CollisionBegin:Ground") == 0 && CountContact("Hero:TriggerBegin:Zone") == 0);
    scene->OnRuntimeStop();
}

// Logs like ContactLogScript. On the first Begin it hears it switches the entities named in
// g_SwitchOnContact (except the one it touched) off and on again - inside the dispatch of a
// batch that may still hold events of their old bodies.
static std::vector<std::string> g_SwitchOnContact;
class SwitchOnContactScript : public ContactLogScript
{
public:
    void OnCollisionBegin(Entity other) override { ContactLogScript::OnCollisionBegin(other); Switch(other); }
    void OnTriggerBegin(Entity other) override { ContactLogScript::OnTriggerBegin(other); Switch(other); }
private:
    void Switch(Entity other)
    {
        if (m_Done) return;
        m_Done = true;
        for (const std::string& name : g_SwitchOnContact)
            if (Entity e = FindEntity(name); e && e != other)
            {
                e.SetActive(false);
                e.SetActive(true);
            }
    }
    bool m_Done = false;
};
ZM_REGISTER_SCRIPT(SwitchOnContactScript)

TEST(ContactsOfBodiesReplacedDuringDispatch)
{
    // The hero lands in a trigger zone on the ground: both Begins come in one batch. The first
    // one heard switches the other entity off and on: the rest of the batch is about its old
    // body and is dropped; its new body begins once, and the end is heard once when leaving.
    g_Contacts.clear();
    g_SwitchOnContact = { "Ground", "Zone" };
    auto scene = CreateRef<Scene>();
    Entity ground = scene->CreateEntity("Ground");
    ground.GetTransform().Translation = { 0.0f, -0.5f, 0.0f };
    ground.GetTransform().Scale = { 6.0f, 1.0f, 6.0f };
    ground.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    ground.AddComponent<BoxCollider3DComponent>();
    AddContactLog(ground);
    Entity zone = scene->CreateEntity("Zone");
    zone.GetTransform().Translation = { 0.0f, 1.0f, 0.0f };
    zone.GetTransform().Scale = { 2.0f, 2.0f, 2.0f };
    zone.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    zone.AddComponent<BoxCollider3DComponent>().IsSensor = true;
    AddContactLog(zone);
    Entity hero = scene->CreateEntity("Hero");
    auto& cc = hero.AddComponent<CharacterController3DComponent>();
    hero.AddComponent<NativeScriptComponent>().ClassName = "SwitchOnContactScript";

    scene->OnRuntimeStart();
    for (int i = 0; i < 30; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(cc.Grounded);
    CHECK(CountContact("Hero:CollisionBegin:Ground") == 1 && CountContact("Hero:TriggerBegin:Zone") == 1);
    CHECK(CountContact("Ground:CollisionBegin:Hero") == 1 && CountContact("Zone:TriggerBegin:Hero") == 1);
    g_Contacts.clear();
    cc.Move({ 4.0f, 0.0f, 0.0f });
    for (int i = 0; i < 120; i++)
        scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(!cc.Grounded);
    CHECK(CountContact("Hero:CollisionEnd:Ground") == 1 && CountContact("Ground:CollisionEnd:Hero") == 1);
    CHECK(CountContact("Hero:TriggerEnd:Zone") == 1 && CountContact("Zone:TriggerEnd:Hero") == 1);
    scene->OnRuntimeStop();
    g_SwitchOnContact.clear();
}

TEST(PrefabInstancesKeepActiveState)
{
    g_Life.clear();
    auto dir = std::filesystem::temp_directory_path() / ("zamin-prefab-active-" + std::to_string((uint64_t)UUID()));
    std::filesystem::create_directories(dir);

    // Turret (active) > Barrel (off itself, scripted, with a body) > Muzzle (active, scripted).
    auto source = CreateRef<Scene>();
    Entity turret = source->CreateEntity("Turret");
    Entity barrel = AddLifeScript(*source, "Barrel");
    barrel.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
    barrel.AddComponent<BoxCollider2DComponent>();
    Entity muzzle = AddLifeScript(*source, "Muzzle");
    source->SetParent(barrel, turret, false);
    source->SetParent(muzzle, barrel, false);
    barrel.SetActive(false);
    const auto prefab = dir / "Turret.zprefab";
    CHECK(source->SavePrefab(turret, prefab));
    const std::string saved = FileSystem::ReadTextFile(prefab);

    // Instances in a running scene: every entity starts as it was saved, independently.
    auto game = CreateRef<Scene>();
    game->GetSettings().Gravity = { 0.0f, 0.0f };
    game->OnRuntimeStart();
    Entity a = game->InstantiatePrefab(prefab);
    Entity b = game->InstantiatePrefab(prefab);
    CHECK(a && b && a != b);
    if (!a || !b) return;
    Entity barrelA = game->FindChild(a, "Barrel"), barrelB = game->FindChild(b, "Barrel");
    Entity muzzleA = game->FindChild(a, "Muzzle");
    CHECK(a.IsActive() && !barrelA.IsActive() && !barrelB.IsActive());
    CHECK(muzzleA.IsActive() && !muzzleA.IsActiveInHierarchy());
    CHECK(barrelA.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
    game->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(CountLife("Barrel:Create") == 2 && CountLife("Barrel:Enable") == 0 && CountLife("Barrel:Update") == 0);
    CHECK(CountLife("Muzzle:Create") == 2 && CountLife("Muzzle:Start") == 0 && CountLife("Muzzle:Update") == 0);
    // One instance switched on: only its subtree joins (OnEnable, then OnStart, body).
    barrelA.SetActive(true);
    CHECK(barrelA.GetComponent<Rigidbody2DComponent>().RuntimeBody != nullptr);
    CHECK(barrelB.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
    CHECK(CountLife("Barrel:Enable") == 1 && CountLife("Muzzle:Enable") == 1);
    game->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(CountLife("Barrel:Start") == 1 && CountLife("Barrel:Update") == 1 && CountLife("Muzzle:Update") == 1);
    game->OnRuntimeStop();
    CHECK(FileSystem::ReadTextFile(prefab) == saved); // the prefab itself is untouched

    // Placed in an edited scene, each instance keeps its own state through save / load and the
    // play-mode copy.
    auto level = CreateRef<Scene>();
    Entity first = level->InstantiatePrefab(prefab);
    Entity second = level->InstantiatePrefab(prefab);
    level->FindChild(first, "Barrel").SetActive(true);
    second.SetActive(false);
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(SceneSerializer(level).SerializeToString()));
    std::vector<Entity> turrets = loaded->FindEntitiesByName("Turret");
    CHECK(turrets.size() == 2);
    if (turrets.size() == 2)
    {
        Entity l1 = turrets[0].GetUUID() == first.GetUUID() ? turrets[0] : turrets[1];
        Entity l2 = l1 == turrets[0] ? turrets[1] : turrets[0];
        CHECK(l1.IsActive() && loaded->FindChild(l1, "Barrel").IsActive() && loaded->FindChild(l1, "Muzzle").IsActiveInHierarchy());
        CHECK(!l2.IsActive() && !loaded->FindChild(l2, "Barrel").IsActive() && loaded->FindChild(l2, "Muzzle").IsActive());
        auto play = Scene::Copy(loaded);
        CHECK(play->CountInactiveInHierarchy() == 3 && loaded->CountInactiveInHierarchy() == 3);
    }

    // Templates in the scene (Instantiate(entity)).
    g_Life.clear();
    auto scene = CreateRef<Scene>();
    scene->GetSettings().Gravity = { 0.0f, 0.0f };
    auto withBody = [&](const std::string& name) {
        Entity e = scene->CreateEntity(name);
        e.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
        e.AddComponent<BoxCollider2DComponent>();
        return e;
    };
    Entity ghost = withBody("Ghost"); // switched off itself, at the root
    ghost.GetTransform().Translation = { -10.0f, 0.0f, 0.0f };
    ghost.SetActive(false);
    Entity templates = scene->CreateEntity("Templates"); // a switched-off group of templates
    templates.GetTransform().Translation = { 10.0f, 0.0f, 0.0f };
    templates.SetActive(false);
    Entity bullet = withBody("Bullet");
    bullet.GetTransform().Translation = { 1.0f, 2.0f, 0.0f };
    scene->SetParent(bullet, templates, false);
    Entity spark = scene->CreateEntity("Spark"); // inside the bullet, off itself
    scene->SetParent(spark, bullet, false);
    spark.SetActive(false);
    Entity mine = withBody("Mine"); // off itself, inside the switched-off group too
    scene->SetParent(mine, templates, false);
    mine.SetActive(false);
    Entity rack = scene->CreateEntity("Rack"); // an active parent
    rack.GetTransform().Translation = { 20.0f, 0.0f, 0.0f };
    Entity shell = withBody("Shell");
    scene->SetParent(shell, rack, false);

    scene->OnRuntimeStart();
    auto bodyAt = [&](Entity e, const glm::vec2& expected) {
        void* body = e.GetComponent<Rigidbody2DComponent>().RuntimeBody;
        if (!body) return false;
        scene->OnUpdateRuntime(1.0f / 60.0f); // the transform follows the body
        glm::vec3 p = scene->GetWorldPosition(e);
        return std::abs(p.x - expected.x) < 1e-3f && std::abs(p.y - expected.y) < 1e-3f;
    };
    // A switched-off template gives a switched-off clone; switched on, it joins where it is.
    Entity ghostClone = scene->Instantiate(ghost);
    CHECK(ghostClone && !ghostClone.IsActive() && ghostClone.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
    ghostClone.SetActive(true);
    CHECK(bodyAt(ghostClone, { -10.0f, 0.0f }));
    // An active template in a switched-off group: the clone is a root, as active as the template
    // itself, where the template is in the world; the subtree keeps its flags.
    Entity bulletClone = scene->Instantiate(bullet);
    CHECK(bulletClone && !scene->GetParent(bulletClone) && bulletClone.IsActiveInHierarchy());
    CHECK(bodyAt(bulletClone, { 11.0f, 2.0f }));
    Entity sparkClone = scene->FindChild(bulletClone, "Spark");
    CHECK(sparkClone && sparkClone != spark && !sparkClone.IsActive());
    CHECK(bullet.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr && scene->GetParent(bullet) == templates);
    // With a position: the world position of the root clone.
    Entity placed = scene->Instantiate(bullet, glm::vec3(5.0f, 5.0f, 0.0f));
    CHECK(placed && !scene->GetParent(placed) && bodyAt(placed, { 5.0f, 5.0f }));
    // Switched off itself as well: a root clone that is still off.
    Entity mineClone = scene->Instantiate(mine);
    CHECK(mineClone && !scene->GetParent(mineClone) && !mineClone.IsActive());
    CHECK(mineClone.GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
    mineClone.SetActive(true);
    CHECK(bodyAt(mineClone, { 10.0f, 0.0f }));
    // Below an active parent nothing changes: the clone stays next to the template, and the
    // position is local to that parent.
    Entity shellClone = scene->Instantiate(shell, glm::vec3(2.0f, 0.0f, 0.0f));
    CHECK(shellClone && scene->GetParent(shellClone) == rack && bodyAt(shellClone, { 22.0f, 0.0f }));
    // The same through the script API.
    Entity spawner = AddLifeScript(*scene, "Spawner");
    scene->OnEntitiesSpawned({ spawner });
    auto* script = ScriptableEntity::GetScript<LifeScript>(spawner);
    CHECK(script != nullptr);
    if (script)
    {
        Entity fromScript = script->Instantiate(bullet, glm::vec3(-3.0f, 1.0f, 0.0f));
        CHECK(fromScript && !scene->GetParent(fromScript) && bodyAt(fromScript, { -3.0f, 1.0f }));
        // A prefab file: the whole subtree as it was saved (absolute path without a project).
        Entity fromFile = script->Instantiate(prefab.string(), glm::vec3(0.0f, 30.0f, 0.0f));
        CHECK(fromFile && fromFile.IsActive() && !scene->FindChild(fromFile, "Barrel").IsActive());
        CHECK(scene->FindChild(fromFile, "Barrel").GetComponent<Rigidbody2DComponent>().RuntimeBody == nullptr);
        CHECK_NEAR(scene->GetWorldPosition(fromFile).y, 30.0f, 1e-5f);
    }
    scene->OnRuntimeStop();
    std::filesystem::remove_all(dir);
}

// A template in a rotated and scaled, switched-off group: the root clone has the template's
// world rotation and scale whether a position is given or not (only the position differs).
TEST(InstantiateFromTransformedInactiveGroup)
{
    auto scene = CreateRef<Scene>();
    Entity group = scene->CreateEntity("Templates");
    group.GetTransform().Translation = { 10.0f, 0.0f, 0.0f };
    group.GetTransform().Rotation = { 0.0f, 0.0f, 0.5f };
    group.GetTransform().Scale = { 2.0f, 2.0f, 2.0f };
    group.SetActive(false);
    Entity crate = scene->CreateEntity("Crate");
    scene->SetParent(crate, group, false);
    crate.GetTransform().Translation = { 1.0f, 0.0f, 0.0f };
    crate.GetTransform().Rotation = { 0.0f, 0.0f, 0.25f };
    crate.GetTransform().Scale = { 1.5f, 1.5f, 1.5f };
    Entity child = scene->CreateEntity("Lid"); // keeps its local transform below the clone
    scene->SetParent(child, crate, false);
    child.GetTransform().Translation = { 0.0f, 1.0f, 0.0f };
    const glm::vec3 templateWorld = scene->GetWorldPosition(crate);
    const glm::vec3 lidOffset = scene->GetWorldPosition(child) - templateWorld;

    scene->OnRuntimeStart();
    Entity here = scene->Instantiate(crate);
    Entity placed = scene->Instantiate(crate, glm::vec3(-4.0f, 3.0f, 0.0f));
    CHECK(here && placed && !scene->GetParent(here) && !scene->GetParent(placed));
    for (Entity e : { here, placed })
    {
        const auto& t = e.GetTransform();
        CHECK_NEAR(t.Rotation.z, 0.75f, 1e-4f);
        CHECK_NEAR(t.Rotation.x, 0.0f, 1e-4f);
        CHECK_NEAR(t.Scale.x, 3.0f, 1e-4f);
        CHECK_NEAR(t.Scale.y, 3.0f, 1e-4f);
        CHECK_NEAR(t.Scale.z, 3.0f, 1e-4f);
        Entity lid = scene->FindChild(e, "Lid");
        CHECK(lid && lid.GetTransform().Translation == glm::vec3(0.0f, 1.0f, 0.0f));
        if (lid)
        {
            glm::vec3 offset = scene->GetWorldPosition(lid) - scene->GetWorldPosition(e);
            CHECK_NEAR(offset.x, lidOffset.x, 1e-4f);
            CHECK_NEAR(offset.y, lidOffset.y, 1e-4f);
        }
    }
    CHECK_NEAR(here.GetTransform().Translation.x, templateWorld.x, 1e-4f);
    CHECK_NEAR(here.GetTransform().Translation.y, templateWorld.y, 1e-4f);
    CHECK(placed.GetTransform().Translation == glm::vec3(-4.0f, 3.0f, 0.0f));
    scene->OnRuntimeStop();
}

// Activity across hierarchy, physics and scripts at once: random switching / reparenting while
// the game runs, checked against a brute-force walk up the parents after every step.
TEST(ActivityHierarchyPhysicsScripts)
{
    g_Life.clear();
    auto scene = CreateRef<Scene>();
    scene->GetSettings().Gravity = { 0.0f, 0.0f };
    const int count = 12;
    std::vector<Entity> nodes;
    uint32_t seed = 12345;
    auto next = [&](uint32_t range) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % range; };
    for (int i = 0; i < count; i++)
    {
        Entity e = AddLifeScript(*scene, "N" + std::to_string(i));
        e.GetTransform().Translation = { i * 3.0f, 0.0f, 0.0f };
        e.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
        e.AddComponent<CircleCollider2DComponent>().Radius = 0.25f;
        if (i > 0 && next(3) != 0)
            scene->SetParent(e, nodes[next((uint32_t)i)], true);
        if (next(4) == 0)
            e.SetActive(false);
        nodes.push_back(e);
    }
    auto expected = [&](Entity e) {
        for (Entity p = e; p; p = scene->GetParent(p))
            if (!p.IsActive())
                return false;
        return true;
    };
    auto enabled = [&](Entity e) {
        const std::string name = e.GetName();
        return CountLife(name + ":Enable") - CountLife(name + ":Disable") == 1;
    };
    int mismatches = 0;
    auto verify = [&]() {
        for (Entity e : nodes)
        {
            const bool on = expected(e);
            if (scene->IsActiveInHierarchy(e) != on) mismatches++;
            if ((e.GetComponent<Rigidbody2DComponent>().RuntimeBody != nullptr) != on) mismatches++;
            if (enabled(e) != on) mismatches++;
            const int balance = CountLife(e.GetName() + ":Enable") - CountLife(e.GetName() + ":Disable");
            if (balance < 0 || balance > 1) mismatches++; // never twice in a row
        }
    };

    scene->OnRuntimeStart();
    scene->OnUpdateRuntime(1.0f / 60.0f);
    verify();
    size_t inactiveAtStart = scene->CountInactiveInHierarchy();
    for (int step = 0; step < 300; step++)
    {
        Entity e = nodes[next(count)];
        switch (next(5))
        {
            case 0: case 1: e.SetActive(!e.IsActive()); break;
            case 2:
            {
                Entity parent = next(3) == 0 ? Entity() : nodes[next(count)];
                if (!parent || (parent != e && !scene->IsAncestorOf(e, parent)))
                    scene->SetParent(e, parent, true);
                break;
            }
            case 3: // a direct write (inspector), picked up on the next frame
                e.GetComponent<TagComponent>().Active = !e.IsActive();
                scene->OnUpdateRuntime(1.0f / 60.0f);
                break;
            default:
            {
                // One frame: only scripts of active entities update.
                std::vector<int> before;
                for (Entity n : nodes) before.push_back(CountLife(n.GetName() + ":Update"));
                scene->OnUpdateRuntime(1.0f / 60.0f);
                for (int i = 0; i < count; i++)
                    if ((CountLife(nodes[i].GetName() + ":Update") - before[i] == 1) != expected(nodes[i]))
                        mismatches++;
                break;
            }
        }
        verify();
    }
    CHECK(mismatches == 0);
    CHECK(inactiveAtStart > 0 || scene->CountInactiveInHierarchy() > 0); // the run did switch things

    // Destroying a switched-off entity: OnDestroy once, no second OnDisable.
    Entity victim;
    for (Entity e : nodes)
        if (!e.IsActive() && scene->GetChildCount(e) == 0) { victim = e; break; }
    if (!victim)
    {
        victim = AddLifeScript(*scene, "Victim");
        scene->OnEntitiesSpawned({ victim });
        victim.SetActive(false);
    }
    const std::string name = victim.GetName();
    const int disables = CountLife(name + ":Disable");
    scene->DestroyEntityDeferred(victim);
    scene->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(CountLife(name + ":Destroy") == 1 && CountLife(name + ":Disable") == disables);
    scene->OnRuntimeStop();

    // 3D: a sleeping box wakes up and falls when the floor it rests on is switched off, and one
    // a switched-on wall appears in is pushed out.
    auto world3D = CreateRef<Scene>();
    auto staticBox = [&](const std::string& name, const glm::vec3& at, const glm::vec3& scale) {
        Entity e = world3D->CreateEntity(name);
        e.GetTransform().Translation = at;
        e.GetTransform().Scale = scale;
        e.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
        e.AddComponent<BoxCollider3DComponent>();
        return e;
    };
    Entity floor = staticBox("Floor", { 0.0f, -0.5f, 0.0f }, { 4.0f, 1.0f, 4.0f });
    Entity table = staticBox("Table", { 20.0f, -0.5f, 0.0f }, { 4.0f, 1.0f, 4.0f });
    Entity wall = staticBox("Wall", { 20.8f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }); // overlaps the second box by 0.2
    wall.SetActive(false);
    auto dynamicBox = [&](const std::string& name, const glm::vec3& at) {
        Entity e = world3D->CreateEntity(name);
        e.GetTransform().Translation = at;
        e.AddComponent<Rigidbody3DComponent>();
        e.AddComponent<BoxCollider3DComponent>();
        return e;
    };
    Entity crate = dynamicBox("Crate", { 0.0f, 0.5f, 0.0f });
    Entity cup = dynamicBox("Cup", { 20.0f, 0.5f, 0.0f });
    world3D->OnRuntimeStart();
    for (int i = 0; i < 180; i++) // at rest, asleep
        world3D->OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(crate.GetTransform().Translation.y, 0.5f, 0.02f);
    floor.SetActive(false);
    for (int i = 0; i < 30; i++)
        world3D->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(crate.GetTransform().Translation.y < 0.0f);
    const float cupX = cup.GetTransform().Translation.x;
    wall.SetActive(true);
    for (int i = 0; i < 30; i++)
        world3D->OnUpdateRuntime(1.0f / 60.0f);
    CHECK(cup.GetTransform().Translation.x < cupX - 0.05f);
    CHECK(table.GetComponent<Rigidbody3DComponent>().RuntimeBodyID != 0xffffffff);
    world3D->OnRuntimeStop();
}

// ------------------------------------------------------------------ asset inspector / references
static std::string Base64(const std::string& bytes)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3)
    {
        uint32_t v = ((uint8_t)bytes[i] << 16) | ((uint8_t)bytes[i + 1] << 8) | (uint8_t)bytes[i + 2];
        for (int k = 3; k >= 0; k--) out += table[(v >> (6 * k)) & 63];
    }
    if (i + 1 == bytes.size())
    {
        uint32_t v = (uint8_t)bytes[i] << 16;
        out += table[(v >> 18) & 63]; out += table[(v >> 12) & 63]; out += "==";
    }
    else if (i + 2 == bytes.size())
    {
        uint32_t v = ((uint8_t)bytes[i] << 16) | ((uint8_t)bytes[i + 1] << 8);
        out += table[(v >> 18) & 63]; out += table[(v >> 12) & 63]; out += table[(v >> 6) & 63]; out += '=';
    }
    return out;
}

TEST(AssetInfoImages)
{
    namespace fs = std::filesystem;
    fs::path dir = MakeTempDir("zamin-image-info-");
    // 3x2 RGB and 2x2 RGBA (alpha 0 / 128 / 255 / 64).
    const uint8_t rgb[3 * 2 * 3] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 10, 20, 30, 40, 50, 60, 70, 80, 90 };
    CHECK(Image::WritePNG((dir / "rgb.png").string(), 3, 2, 3, rgb));
    const uint8_t rgba[2 * 2 * 4] = { 200, 100, 50, 0, 10, 20, 30, 128, 1, 2, 3, 255, 9, 8, 7, 64 };
    CHECK(Image::WritePNG((dir / "rgba.png").string(), 2, 2, 4, rgba));
    WriteFile(dir / "broken.png", "this is not an image");

    ImageFileInfo info = AssetInfo::ReadImage(dir / "rgb.png");
    CHECK(info.Valid && info.Error.empty());
    CHECK(info.Width == 3 && info.Height == 2 && info.Channels == 3);
    CHECK(!info.Is16Bit && !info.IsHDR && info.Format == "PNG");
    ImageFileInfo alpha = AssetInfo::ReadImage(dir / "rgba.png");
    CHECK(alpha.Valid && alpha.Channels == 4 && alpha.Width == 2);
    ImageFileInfo broken = AssetInfo::ReadImage(dir / "broken.png");
    CHECK(!broken.Valid && !broken.Error.empty());
    ImageFileInfo missing = AssetInfo::ReadImage(dir / "nope.png");
    CHECK(!missing.Valid && missing.Error == "fayl topilmadi");

    // CPU pixels and channel views (what the preview shows for R / G / B / A).
    ImageData data;
    std::string error;
    CHECK(Image::Load((dir / "rgba.png").string(), data, 4, false, &error));
    CHECK(data.IsValid() && data.Width == 2 && data.Height == 2 && data.Channels == 4 && data.FileChannels == 4);
    CHECK(data.Pixels.size() == 16 && data.Pixels[0] == 200 && data.Pixels[7] == 128);
    std::vector<uint8_t> a = Image::MakeChannelView(data.Pixels, ImageChannelView::Alpha);
    CHECK(a.size() == 16 && a[0] == 0 && a[1] == 0 && a[3] == 255 && a[4] == 128 && a[8] == 255 && a[12] == 64);
    std::vector<uint8_t> r = Image::MakeChannelView(data.Pixels, ImageChannelView::Red);
    CHECK(r[0] == 200 && r[1] == 200 && r[2] == 200 && r[3] == 255 && r[4] == 10);
    std::vector<uint8_t> g = Image::MakeChannelView(data.Pixels, ImageChannelView::Green);
    CHECK(g[0] == 100 && g[4] == 20);
    std::vector<uint8_t> opaque = Image::MakeChannelView(data.Pixels, ImageChannelView::RGB);
    CHECK(opaque[0] == 200 && opaque[1] == 100 && opaque[2] == 50 && opaque[3] == 255 && opaque[7] == 255);
    CHECK(Image::MakeChannelView(data.Pixels, ImageChannelView::RGBA) == data.Pixels);
    ImageData flipped;
    CHECK(Image::Load((dir / "rgb.png").string(), flipped, 4, true));
    CHECK(flipped.FileChannels == 3 && flipped.Pixels[0] == 10 && flipped.Pixels[3] == 255); // bottom row first
    CHECK(!Image::Load((dir / "broken.png").string(), data, 4, false, &error) && !error.empty() && !data.IsValid());

    // GPU memory and mip levels.
    CHECK(AssetInfo::GetMipLevelCount(256, 128) == 9);
    CHECK(AssetInfo::GetMipLevelCount(1, 1) == 1);
    CHECK(AssetInfo::GetMipLevelCount(300, 1) == 9);
    CHECK(AssetInfo::EstimateTextureMemory(256, 256, 4, false) == 262144);
    CHECK(AssetInfo::EstimateTextureMemory(256, 256, 4, true) == 349524);
    CHECK(AssetInfo::EstimateTextureMemory(4, 2, 4, true) == 44);
    CHECK(AssetInfo::FormatDuration(0.25) == "0.250 s");
    CHECK(AssetInfo::FormatDuration(12.5) == "12.5 s");
    CHECK(AssetInfo::FormatDuration(125.3) == "2:05.3");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AssetInfoAudio)
{
    namespace fs = std::filesystem;
    fs::path dir = MakeTempDir("zamin-audio-info-");
    WriteTestWav(dir / "tone.wav", 0.5f); // mono, 22050 Hz, 16-bit
    WriteFile(dir / "broken.wav", "RIFF....WAVEnope");

    AudioFileInfo info = AssetInfo::ReadAudio(dir / "tone.wav");
    CHECK(info.Valid && info.Error.empty());
    CHECK(info.Format == "WAV" && info.SampleFormat == "16-bit");
    CHECK(info.Channels == 1 && info.SampleRate == 22050);
    CHECK(info.Frames == 11025);
    CHECK_NEAR(info.Duration, 0.5, 0.001);

    std::string error;
    std::vector<glm::vec2> peaks = AssetInfo::ReadAudioWaveform(dir / "tone.wav", 64, &error);
    CHECK(peaks.size() == 64 && error.empty());
    bool inRange = true;
    float lowest = 1.0f, highest = -1.0f;
    for (const glm::vec2& p : peaks)
    {
        inRange = inRange && p.x >= -1.0f && p.y <= 1.0f && p.x <= p.y;
        lowest = std::min(lowest, p.x);
        highest = std::max(highest, p.y);
    }
    CHECK(inRange);
    CHECK(highest > 0.15f && lowest < -0.15f); // the tone peaks at 6000 / 32768
    CHECK(highest < 0.2f);

    // The inspector's player: length, position and seeking of a playing sound (no device: the
    // sound does not advance, but it is loaded and can be moved around in).
    bool wasAvailable = AudioEngine::IsAvailable();
    AudioEngine::Init(true);
    if (AudioEngine::IsAvailable())
    {
        uint32_t sound = AudioEngine::Play(dir / "tone.wav", 0.8f);
        CHECK(sound != 0 && AudioEngine::IsPlaying(sound));
        CHECK_NEAR(AudioEngine::GetLength(sound), 0.5f, 0.01f);
        CHECK_NEAR(AudioEngine::GetCursor(sound), 0.0f, 0.01f);
        AudioEngine::Seek(sound, 0.25f);
        CHECK_NEAR(AudioEngine::GetCursor(sound), 0.25f, 0.01f);
        AudioEngine::Seek(sound, 9.0f); // past the end: the last frame
        CHECK(AudioEngine::GetCursor(sound) <= 0.5f && AudioEngine::GetCursor(sound) > 0.45f);
        AudioEngine::SetVolume(sound, 0.3f);
        AudioEngine::Stop(sound);
        CHECK(!AudioEngine::IsPlaying(sound) && AudioEngine::GetCursor(sound) == 0.0f && AudioEngine::GetLength(sound) == 0.0f);
        CHECK(AudioEngine::GetCursor(12345) == 0.0f);
    }
    if (!wasAvailable)
        AudioEngine::Shutdown();

    AudioFileInfo broken = AssetInfo::ReadAudio(dir / "broken.wav");
    CHECK(!broken.Valid && !broken.Error.empty());
    CHECK(AssetInfo::ReadAudioWaveform(dir / "broken.wav", 64, &error).empty() && !error.empty());
    CHECK(AssetInfo::ReadAudio(dir / "nope.wav").Error == "fayl topilmadi");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AssetInfoModels)
{
    namespace fs = std::filesystem;
    fs::path dir = MakeTempDir("zamin-model-info-");
    // One triangle, a 2-joint skin and two animations: "Walk" moves a joint (input max 2.5 stated),
    // "Wave" moves the mesh node (no max: the last key time is read from the buffer).
    std::string buffer;
    auto f32 = [&](float v) { char b[4]; std::memcpy(b, &v, 4); buffer.append(b, 4); };
    auto u16 = [&](uint16_t v) { buffer.push_back((char)(v & 0xff)); buffer.push_back((char)(v >> 8)); };
    for (float v : { 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f }) f32(v); // 0: positions (36)
    u16(0); u16(1); u16(2); u16(0);                                        // 36: indices (6 + 2 padding)
    f32(0.0f); f32(2.5f);                                                  // 44: Walk times
    for (float v : { 0.f, 0.f, 0.f, 0.f, 1.f, 0.f }) f32(v);               // 52: translations (24)
    f32(0.0f); f32(1.25f);                                                 // 76: Wave times
    std::string gltf = R"({
  "asset": { "version": "2.0", "generator": "Zamin test" },
  "buffers": [ { "byteLength": 84, "uri": "data:application/octet-stream;base64,)" + Base64(buffer) + R"(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 }, { "buffer": 0, "byteOffset": 36, "byteLength": 6 },
    { "buffer": 0, "byteOffset": 44, "byteLength": 8 }, { "buffer": 0, "byteOffset": 52, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 76, "byteLength": 8 } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0] },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [2.5] },
    { "bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3" },
    { "bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR" } ],
  "images": [ { "uri": "tex/missing%20one.png" } ],
  "textures": [ { "source": 0 } ],
  "materials": [ { "name": "Red", "pbrMetallicRoughness": { "baseColorFactor": [1, 0, 0, 1], "metallicFactor": 0.2,
                   "roughnessFactor": 0.7, "baseColorTexture": { "index": 0 } } } ],
  "meshes": [ { "name": "Tri", "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1, "material": 0 } ] } ],
  "nodes": [ { "name": "Body", "mesh": 0 }, { "name": "Hip", "children": [2] }, { "name": "Knee" } ],
  "skins": [ { "joints": [1, 2] } ],
  "animations": [
    { "name": "Walk", "samplers": [ { "input": 2, "output": 3 } ], "channels": [ { "sampler": 0, "target": { "node": 1, "path": "translation" } } ] },
    { "name": "Wave", "samplers": [ { "input": 4, "output": 3 } ], "channels": [ { "sampler": 0, "target": { "node": 0, "path": "translation" } } ] } ],
  "scenes": [ { "nodes": [0, 1] } ], "scene": 0
})";
    WriteFile(dir / "tri.gltf", gltf);
    ModelFileInfo info = AssetInfo::ReadModel(dir / "tri.gltf");
    CHECK(info.Valid && info.Error.empty());
    CHECK(info.Format == "glTF" && info.Version == "2.0" && info.Generator == "Zamin test");
    CHECK(info.Meshes == 1 && info.Nodes == 3 && info.Images == 1 && info.Skins == 1 && info.Joints == 2);
    CHECK(info.Primitives.size() == 1 && info.Vertices == 3 && info.Triangles == 1);
    if (info.Primitives.size() == 1)
    {
        CHECK(info.Primitives[0].Mesh == "Tri" && info.Primitives[0].Material == "Red");
        CHECK(info.Primitives[0].Vertices == 3 && info.Primitives[0].Triangles == 1 && !info.Primitives[0].Skinned);
    }
    CHECK(info.Materials.size() == 1);
    if (!info.Materials.empty())
    {
        CHECK(info.Materials[0].Name == "Red");
        CHECK_NEAR(info.Materials[0].BaseColor.r, 1.0f, 1e-6f);
        CHECK_NEAR(info.Materials[0].Metallic, 0.2f, 1e-6f);
        CHECK(info.Materials[0].Maps == std::vector<std::string>{ "Albedo" });
    }
    CHECK(info.Animations.size() == 2);
    if (info.Animations.size() == 2)
    {
        CHECK(info.Animations[0].Name == "Walk" && info.Animations[0].TargetsJoints && info.Animations[0].Channels == 1);
        CHECK_NEAR(info.Animations[0].Duration, 2.5f, 1e-5f);
        CHECK(info.Animations[1].Name == "Wave" && !info.Animations[1].TargetsJoints);
        CHECK_NEAR(info.Animations[1].Duration, 1.25f, 1e-5f);
    }
    CHECK(info.ExternalFiles == std::vector<std::string>{ "tex/missing one.png" });
    CHECK(info.MissingFiles == std::vector<std::string>{ "tex/missing one.png" });

    WriteFile(dir / "quad.obj", "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n");
    ModelFileInfo obj = AssetInfo::ReadModel(dir / "quad.obj");
    CHECK(obj.Valid && obj.Format == "OBJ" && obj.Triangles == 2 && obj.Primitives.size() == 1);
    WriteFile(dir / "broken.gltf", "{ not json");
    ModelFileInfo broken = AssetInfo::ReadModel(dir / "broken.gltf");
    CHECK(!broken.Valid && !broken.Error.empty());
    CHECK(AssetInfo::ReadModel(dir / "nope.glb").Error == "fayl topilmadi");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

static std::vector<std::string> UnusedPaths(const std::vector<UnusedAsset>& unused)
{
    std::vector<std::string> paths;
    for (const UnusedAsset& u : unused)
        paths.push_back(u.Path);
    return paths;
}

TEST(AssetReferenceIndexScans)
{
    namespace fs = std::filesystem;
    fs::path dir = MakeTempDir("zamin-asset-usage-");
    fs::path assets = dir / "Assets";
    for (const char* name : { "a.png", "b.png", "c.png", "unused.png" })
        WriteFile(assets / "Textures" / name, "png");
    WriteFile(assets / "Audio" / "x.wav", "wav");
    WriteFile(assets / "Audio" / "y.wav", "wav");
    WriteFile(assets / "Models" / "m.bin", "bin");
    WriteFile(assets / "Models" / "m.gltf", R"({ "asset": { "version": "2.0" },
        "buffers": [ { "uri": "m.bin", "byteLength": 3 } ],
        "images": [ { "uri": "../Textures/b.png" }, { "uri": "gone.png" }, { "uri": "data:image/png;base64,AAAA" } ] })");
    WriteFile(assets / "Prefabs" / "p.zprefab", R"({ "Entities": [ { "Tag": "Textures/unused.png",
        "MeshRenderer": { "Model": "Models/m.gltf", "Mesh": "Model" } } ] })");
    WriteFile(assets / "Scenes" / "Old.zscene", R"({ "Scene": "Old", "Entities": [ { "Tag": "C", "SpriteRenderer": { "Texture": "Textures/c.png" } } ] })");
    WriteFile(dir / "Source" / "Game.cpp", "// Audio/y.wav was the old sound\n"
                                           "/* \"Audio/y.wav\" */\n"
                                           "void Play() { int n = 1'000; char c = '\"'; PlaySound(\"Audio/x.wav\"); Load(\"Audio/missing.wav\"); }\n");
    WriteFile(dir / "Game.zproject", R"({ "Project": { "Name": "Game", "StartScene": "Scenes/Main.zscene", "AssetDirectory": "Assets",
        "ScriptModulePath": "Binaries/libGame.so", "EngineVersion": "0.1.0" } })");

    AssetRegistry registry(true);
    WriteFile(assets / "Scenes" / "Main.zscene", "{}");
    registry.Scan(assets);
    AssetHandle a = registry.GetHandle("Textures/a.png"), prefab = registry.GetHandle("Prefabs/p.zprefab");
    CHECK(a && prefab);
    // The prefab by UUID only: its path in the scene is out of date (renamed outside the editor).
    std::string main = R"({ "Scene": "Main", "Entities": [
        { "Tag": "A", "SpriteRenderer": { "Texture": "Textures/a.png", "TextureUUID": ")" + a.ToString() + R"(" } },
        { "Tag": "B", "Prefab": { "Source": "Prefabs/old_name.zprefab", "SourceUUID": ")" + prefab.ToString() + R"(" } },
        { "Tag": "Gone", "SpriteRenderer": { "Texture": "Textures/gone.png" } },
        { "Tag": "Restart", "NativeScript": { "Class": "Door", "Fields": [ { "Name": "Next", "Value": "Scenes/Main.zscene" } ] } } ] })";
    WriteFile(assets / "Scenes" / "Main.zscene", main);

    AssetReferenceIndex::Sources sources;
    sources.AssetDirectory = assets;
    sources.ProjectDirectory = dir;
    sources.ProjectFile = dir / "Game.zproject";
    sources.SourceDirectory = dir / "Source";
    sources.Registry = &registry;
    AssetReferenceIndex index;
    index.Build(sources);
    CHECK(index.IsBuilt());
    CHECK(index.GetScannedFileCount() == 6); // 2 scenes, prefab, gltf, project file, Game.cpp

    const auto& byA = index.GetReferrers("Textures/a.png");
    CHECK(byA.size() == 1 && index.GetReferrers(assets / "Textures" / "a.png").size() == 1);
    if (byA.size() == 1)
    {
        CHECK(byA[0].Name == "Assets/Scenes/Main.zscene" && byA[0].AssetPath == "Scenes/Main.zscene");
        CHECK(byA[0].Has(AssetReferenceKind::Path) && byA[0].Has(AssetReferenceKind::UUID) && byA[0].Count == 1);
    }
    const auto& byPrefab = index.GetReferrers("Prefabs/p.zprefab");
    CHECK(byPrefab.size() == 1 && !byPrefab.empty() && byPrefab[0].Has(AssetReferenceKind::UUID) && !byPrefab[0].Has(AssetReferenceKind::Path));
    const auto& byModel = index.GetReferrers("Models/m.gltf");
    CHECK(byModel.size() == 1 && !byModel.empty() && byModel[0].AssetPath == "Prefabs/p.zprefab");
    const auto& byB = index.GetReferrers("Textures/b.png");
    CHECK(byB.size() == 1 && !byB.empty() && byB[0].Has(AssetReferenceKind::ModelFile) && byB[0].AssetPath == "Models/m.gltf");
    CHECK(index.IsReferenced("Models/m.bin"));
    const auto& byX = index.GetReferrers("Audio/x.wav");
    CHECK(byX.size() == 1 && !byX.empty() && byX[0].Name == "Source/Game.cpp" && byX[0].Has(AssetReferenceKind::Code));
    // The start scene: the project file (its own restart field does not count).
    const auto& byMain = index.GetReferrers("Scenes/Main.zscene");
    CHECK(byMain.size() == 1 && !byMain.empty() && byMain[0].Name == "Game.zproject");
    CHECK(!index.IsReferenced("Textures/unused.png")); // only an entity name
    CHECK(!index.IsReferenced("Audio/y.wav"));         // only in comments
    CHECK(index.GetReferences("Scenes/Main.zscene") == (std::vector<std::string>{ "Prefabs/p.zprefab", "Textures/a.png" }));
    CHECK(index.GetReferences(assets / "Models" / "m.gltf") == (std::vector<std::string>{ "Models/m.bin", "Textures/b.png" }));

    CHECK(UnusedPaths(index.FindUnused()) == (std::vector<std::string>{ "Audio/y.wav", "Scenes/Old.zscene", "Textures/unused.png" }));
    // Reachable from the project file and the code: Old.zscene opens nowhere, so c.png is unused too.
    CHECK(UnusedPaths(index.FindUnused(true)) ==
          (std::vector<std::string>{ "Audio/y.wav", "Scenes/Old.zscene", "Textures/c.png", "Textures/unused.png" }));
    std::vector<UnusedAsset> unused = index.FindUnused();
    CHECK(!unused.empty() && unused[0].Type == AssetType::Audio && unused[0].Size == 3);

    std::vector<std::string> missing;
    for (const MissingAssetReference& m : index.GetMissing())
        missing.push_back(m.Name + " -> " + m.Path);
    CHECK(missing == (std::vector<std::string>{ "Assets/Models/m.gltf -> Models/gone.png", "Assets/Scenes/Main.zscene -> Textures/gone.png",
                                                "Source/Game.cpp -> Audio/missing.wav" }));

    // The open scene's unsaved state instead of the file: a.png is not used any more.
    sources.Overrides[(assets / "Scenes" / "Main.zscene").string()] =
        R"({ "Entities": [ { "Prefab": { "Source": "Prefabs/p.zprefab" } } ] })";
    index.Build(sources);
    CHECK(!index.IsReferenced("Textures/a.png"));
    CHECK(index.GetReferrers("Prefabs/p.zprefab").size() == 1);

    // Without a registry: no UUIDs (the renamed prefab is missing then), assets from the folder.
    sources.Overrides.clear();
    sources.Registry = nullptr;
    index.Build(sources);
    CHECK(index.GetAssets().size() == 10); // 4 textures, 2 sounds, model, prefab, 2 scenes
    CHECK(!index.IsReferenced("Prefabs/p.zprefab") && index.IsReferenced("Textures/a.png"));
    bool missingPrefab = false;
    for (const MissingAssetReference& m : index.GetMissing())
        missingPrefab = missingPrefab || m.Path == "Prefabs/old_name.zprefab";
    CHECK(missingPrefab);

    index.Clear();
    CHECK(!index.IsBuilt() && index.GetReferrers("Textures/a.png").empty());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// Console error lines that mention `needle` (the asset import log writes each failure once).
static int CountLoggedErrors(const std::string& needle)
{
    int count = 0;
    Log::ForEach([&](const LogMessage& m) {
        if (m.Level >= LogLevel::Error && m.Text.find(needle) != std::string::npos)
            count++;
    });
    return count;
}

TEST(AssetImportLogReportsOnce)
{
    namespace fs = std::filesystem;
    Project::SetActive(nullptr);
    AssetImportLog::ClearAll();
    fs::path dir = MakeTempDir("zamin-import-log-");
    uint64_t logged = AssetImportLog::GetLoggedCount();
    bool terminal = Log::GetTerminalOutput();
    Log::SetTerminalOutput(false); // the failures below are expected (they are checked in the log)

    // Readability and stb reasons in words.
    WriteFile(dir / "empty.png", "");
    WriteFile(dir / "fine.txt", "x");
    CHECK(AssetImportLog::CheckReadable(dir / "nothing.png") == "fayl topilmadi");
    CHECK(AssetImportLog::CheckReadable(dir) == "bu papka, fayl emas");
    CHECK(AssetImportLog::CheckReadable(dir / "empty.png") == "fayl bo'sh" && AssetImportLog::CheckReadable(dir / "fine.txt").empty());
    CHECK(AssetImportLog::DescribeImageError("unknown image type").find("stb: unknown image type") != std::string::npos);
    CHECK(AssetImportLog::DescribeImageError(nullptr).find("stb: unknown") != std::string::npos);

    // Reported once per file and reason, again for a new reason, a reload, after a change or a success.
    fs::path file = dir / "Textures" / "hero.png";
    CHECK(AssetImportLog::Report(file, "fayl topilmadi") && AssetImportLog::GetLoggedCount() == logged + 1);
    uint64_t revision = AssetImportLog::GetRevision();
    CHECK(!AssetImportLog::Report(file, "fayl topilmadi") && !AssetImportLog::Report(dir / "Textures" / "." / "hero.png", "fayl topilmadi"));
    CHECK(AssetImportLog::GetRevision() == revision && AssetImportLog::GetLoggedCount() == logged + 1);
    std::optional<AssetImportError> error = AssetImportLog::Find(file);
    CHECK(error && error->Reason == "fayl topilmadi" && error->Count == 3 && error->Type == AssetType::Texture && !error->Reload);
    CHECK(error && error->File == fs::path(AssetImportLog::MakeKey(file)) && error->Name == fs::path(AssetImportLog::MakeKey(file)).generic_string());
    CHECK(AssetImportLog::GetCount() == 1 && AssetImportLog::HasError(file) && !AssetImportLog::HasError(dir / "other.png"));
    CHECK(AssetImportLog::Report(file, "rasm buzilgan") && AssetImportLog::GetRevision() != revision);
    CHECK(AssetImportLog::Report(file, "rasm buzilgan", true) && AssetImportLog::Find(file)->Reload);
    CHECK(!AssetImportLog::Report(file, "rasm buzilgan", true));
    AssetImportLog::OnFileChanged(dir / "Textures"); // a folder: everything inside
    CHECK(AssetImportLog::Report(file, "rasm buzilgan", true));
    AssetImportLog::Clear(file);
    CHECK(!AssetImportLog::HasError(file) && AssetImportLog::GetCount() == 0);
    CHECK(AssetImportLog::Report(file, "rasm buzilgan", true));
    CHECK(AssetImportLog::GetLoggedCount() == logged + 5 && CountLoggedErrors("hero.png") == 5);

    // A failed file is not loaded again while it stays the same (missing / same size and time).
    CHECK(!AssetImportLog::ShouldRetry(file)); // still missing
    CHECK(AssetImportLog::ShouldRetry(dir / "never-failed.png"));
    WriteFile(file, "now it exists");
    CHECK(AssetImportLog::ShouldRetry(file));
    AssetImportLog::Report(file, "rasm buzilgan");
    CHECK(!AssetImportLog::ShouldRetry(file));
    WriteFile(file, "written again, longer than before");
    CHECK(AssetImportLog::ShouldRetry(file));
    AssetImportLog::Report(file, "rasm buzilgan");
    AssetImportLog::OnFileChanged(file);
    CHECK(AssetImportLog::ShouldRetry(file));
    AssetImportLog::ClearAll();
    CHECK(AssetImportLog::GetCount() == 0 && AssetImportLog::GetErrors().empty());

    // The loaders (headless: they fail before anything reaches the GPU). Each file is reported once
    // with its path and reason however often it is asked for.
    WriteFile(dir / "broken.png", "this is not a png");
    for (int i = 0; i < 3; i++)
    {
        CHECK(TextureLibrary::Load(dir / "missing.png") == nullptr);
        CHECK(TextureLibrary::Load(dir / "missing.png", TextureFilter::Nearest) == nullptr);
        CHECK(TextureLibrary::Load(dir / "broken.png") == nullptr);
    }
    CHECK(CountLoggedErrors("missing.png") == 1 && CountLoggedErrors("broken.png") == 1);
    error = AssetImportLog::Find(dir / "missing.png");
    CHECK(error && error->Reason == "fayl topilmadi" && error->Count == 6);
    error = AssetImportLog::Find(dir / "broken.png");
    CHECK(error && error->Reason.find("stb:") != std::string::npos);

    WriteFile(dir / "broken.ttf", "not a font at all");
    CHECK(Font::Load(dir / "missing.ttf") == nullptr && Font::Load(dir / "missing.ttf") == nullptr);
    CHECK(Font::Load(dir / "broken.ttf") == nullptr && Font::Load(dir / "broken.ttf") == nullptr);
    CHECK(CountLoggedErrors("missing.ttf") == 1 && CountLoggedErrors("broken.ttf") == 1);
    CHECK(AssetImportLog::Find(dir / "missing.ttf")->Reason == "fayl topilmadi");
    CHECK(AssetImportLog::Find(dir / "broken.ttf")->Reason.find("shrift") != std::string::npos);
    CHECK(AssetImportLog::Find(dir / "broken.ttf")->Type == AssetType::Font);

    WriteFile(dir / "sky.hdr", "#?RADIANCE but broken");
    CHECK(EnvironmentMap::Load(dir / "sky.hdr") == nullptr && EnvironmentMap::Load(dir / "nosky.hdr") == nullptr);
    CHECK(AssetImportLog::Find(dir / "sky.hdr") && AssetImportLog::Find(dir / "sky.hdr")->Reason.find("stb:") != std::string::npos);
    CHECK(AssetImportLog::Find(dir / "nosky.hdr") && AssetImportLog::Find(dir / "nosky.hdr")->Reason == "fayl topilmadi");

    WriteFile(dir / "Models" / "broken.gltf", "{ not json");
    CHECK(Model::Load(dir / "Models" / "broken.gltf") == nullptr && Model::Load(dir / "Models" / "absent.gltf") == nullptr);
    CHECK(AssetImportLog::Find(dir / "Models" / "broken.gltf") && AssetImportLog::Find(dir / "Models" / "absent.gltf"));
    CHECK(AssetImportLog::Find(dir / "Models" / "absent.gltf")->Reason == "fayl topilmadi");

    // Scenes and prefabs: the reason, with the file's path.
    WriteFile(dir / "Scenes" / "broken.zscene", "{ \"Scene\": ");
    WriteFile(dir / "Scenes" / "notascene.zscene", "{ \"Hello\": 1 }");
    Ref<Scene> scene = CreateRef<Scene>();
    SceneSerializer serializer(scene);
    CHECK(!serializer.Deserialize(dir / "Scenes" / "absent.zscene") && serializer.GetLastError() == "fayl topilmadi");
    CHECK(!serializer.Deserialize(dir / "Scenes" / "broken.zscene") && serializer.GetLastError().starts_with("JSON xatosi"));
    CHECK(!serializer.Deserialize(dir / "Scenes" / "broken.zscene"));
    CHECK(!serializer.Deserialize(dir / "Scenes" / "notascene.zscene") && serializer.GetLastError().find("Scene") != std::string::npos);
    CHECK(CountLoggedErrors("broken.zscene") == 1 && CountLoggedErrors("absent.zscene") == 1 && CountLoggedErrors("notascene.zscene") == 1);
    CHECK(AssetImportLog::Find(dir / "Scenes" / "broken.zscene")->Type == AssetType::Scene);
    CHECK(!serializer.DeserializeFromString("[1, 2") && serializer.GetLastError().starts_with("JSON xatosi"));
    CHECK(serializer.DeserializeFromString("{\"Scene\": \"Ok\", \"Entities\": []}") && serializer.GetLastError().empty());
    std::string fine = serializer.SerializeToString();
    WriteFile(dir / "Scenes" / "broken.zscene", fine); // fixed: loads, the error goes away
    CHECK(serializer.Deserialize(dir / "Scenes" / "broken.zscene") && !AssetImportLog::HasError(dir / "Scenes" / "broken.zscene"));

    WriteFile(dir / "Prefabs" / "bad.zprefab", "{ \"ZaminEntities\": 1 }");
    for (int i = 0; i < 4; i++)
    {
        CHECK(!scene->InstantiatePrefab(dir / "Prefabs" / "gone.zprefab"));
        CHECK(!scene->InstantiatePrefab(dir / "Prefabs" / "bad.zprefab"));
    }
    CHECK(CountLoggedErrors("gone.zprefab") == 1 && CountLoggedErrors("bad.zprefab") == 1);
    CHECK(AssetImportLog::Find(dir / "Prefabs" / "gone.zprefab")->Reason == "fayl topilmadi");
    CHECK(AssetImportLog::Find(dir / "Prefabs" / "bad.zprefab")->Reason.find("Entities") != std::string::npos);
    std::string error2;
    CHECK(SceneSerializer::DeserializeEntities(*scene, "nope", true, nullptr, &error2).empty() && error2.starts_with("JSON xatosi"));

    // Audio (no device needed): a missing / undecodable sound is reported once, and not decoded again.
    WriteFile(dir / "Audio" / "broken.wav", "RIFF but not really");
    bool wasAvailable = AudioEngine::IsAvailable();
    AudioEngine::Init(true);
    if (AudioEngine::IsAvailable())
    {
        for (int i = 0; i < 3; i++)
        {
            CHECK(AudioEngine::Play(dir / "Audio" / "nosound.wav") == 0);
            CHECK(AudioEngine::Play(dir / "Audio" / "broken.wav") == 0);
        }
        CHECK(CountLoggedErrors("nosound.wav") == 1 && CountLoggedErrors("broken.wav") == 1);
        CHECK(AssetImportLog::Find(dir / "Audio" / "nosound.wav")->Reason == "fayl topilmadi");
        CHECK(AssetImportLog::Find(dir / "Audio" / "broken.wav")->Reason.find("miniaudio") != std::string::npos);
        CHECK(AssetImportLog::Find(dir / "Audio" / "broken.wav")->Count == 3);
    }
    if (!wasAvailable)
        AudioEngine::Shutdown();

    // Sorted by name; with a project the names are relative to its asset folder.
    std::vector<AssetImportError> errors = AssetImportLog::GetErrors();
    CHECK(errors.size() >= 10 && std::is_sorted(errors.begin(), errors.end(),
                                                [](const AssetImportError& a, const AssetImportError& b) { return a.Name < b.Name; }));
    fs::path projectDir = dir / "Game";
    CHECK(Project::CreateFromTemplate(projectDir, "ImportGame") != nullptr && Project::Load(projectDir / "ImportGame.zproject"));
    AssetImportLog::Report(projectDir / "Assets" / "Textures" / "lost.png", "fayl topilmadi");
    CHECK(AssetImportLog::Find(projectDir / "Assets" / "Textures" / "lost.png")->Name == "Textures/lost.png");
    CHECK(CountLoggedErrors("'Textures/lost.png' - fayl topilmadi") == 1);

    Log::SetTerminalOutput(terminal);
    Project::SetActive(nullptr);
    AssetImportLog::ClearAll();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AssetDeletionWarnings)
{
    namespace fs = std::filesystem;
    fs::path dir = MakeTempDir("zamin-delete-warn-");
    fs::path assets = dir / "Assets";
    for (const char* name : { "a.png", "b.png", "unused.png" })
        WriteFile(assets / "Textures" / name, "png");
    WriteFile(assets / "Audio" / "x.wav", "wav");
    WriteFile(assets / "Models" / "m.bin", "bin");
    WriteFile(assets / "Models" / "m.gltf", R"({ "asset": { "version": "2.0" },
        "buffers": [ { "uri": "m.bin", "byteLength": 3 } ], "images": [ { "uri": "../Textures/b.png" } ] })");
    WriteFile(assets / "Prefabs" / "p.zprefab", R"({ "Entities": [ { "Tag": "P", "MeshRenderer": { "Model": "Models/m.gltf" } } ] })");
    WriteFile(assets / "Scenes" / "Main.zscene", R"({ "Scene": "Main", "Entities": [
        { "Tag": "A", "SpriteRenderer": { "Texture": "Textures/a.png" } },
        { "Tag": "A2", "SpriteRenderer": { "Texture": "Textures/a.png" } },
        { "Tag": "B", "Prefab": { "Source": "Prefabs/p.zprefab" } } ] })");
    WriteFile(assets / "Scenes" / "Other.zscene", R"({ "Scene": "Other", "Entities": [
        { "Tag": "A", "SpriteRenderer": { "Texture": "Textures/a.png" } } ] })");
    WriteFile(dir / "Source" / "Game.cpp", "void Jump() { PlaySound(\"Audio/x.wav\"); }\n");

    AssetReferenceIndex index;
    CHECK(index.FindUsesOutside({ assets / "Textures" / "a.png" }).empty()); // not built yet
    AssetReferenceIndex::Sources sources;
    sources.AssetDirectory = assets;
    sources.ProjectDirectory = dir;
    sources.SourceDirectory = dir / "Source";
    index.Build(sources);

    // Nothing uses it: no warning.
    CHECK(index.FindUsesOutside({ "Textures/unused.png" }).empty());
    // A texture two scenes use (one of them twice), by absolute or relative path.
    std::vector<AssetInUse> uses = index.FindUsesOutside({ assets / "Textures" / "a.png" });
    std::vector<AssetInUse> byRelative = index.FindUsesOutside({ "Textures/a.png" });
    CHECK(uses.size() == 1 && byRelative.size() == 1 && byRelative[0].Referrers.size() == uses[0].Referrers.size());
    if (uses.size() == 1)
    {
        CHECK(uses[0].Path == "Textures/a.png" && uses[0].Type == AssetType::Texture && uses[0].Referrers.size() == 2);
        CHECK(uses[0].GetReferenceCount() == 3 && uses[0].Referrers[0].Name == "Assets/Scenes/Main.zscene");
    }
    // A folder: what is used from outside it counts, what only its own files use does not (the
    // model's buffer); the texture a model in it uses is not being deleted.
    uses = index.FindUsesOutside({ assets / "Models" });
    CHECK(uses.size() == 1 && uses[0].Path == "Models/m.gltf" && uses[0].Referrers.size() == 1 &&
          uses[0].Referrers[0].AssetPath == "Prefabs/p.zprefab");
    CHECK(index.FindUsesOutside({ "Models/" }).size() == 1);
    uses = index.FindUsesOutside({ "Models/m.bin" });
    CHECK(uses.size() == 1 && uses[0].Referrers.size() == 1 && uses[0].Referrers[0].Has(AssetReferenceKind::ModelFile));
    // Deleted together with what uses them: only the prefab, which the main scene uses, is left.
    uses = index.FindUsesOutside({ "Models", "Prefabs/p.zprefab" });
    CHECK(uses.size() == 1 && uses[0].Path == "Prefabs/p.zprefab" && uses[0].Referrers[0].AssetPath == "Scenes/Main.zscene");
    CHECK(index.FindUsesOutside({ "Models", "Prefabs", "Scenes" }).empty());
    // Several at once, sorted; code that names a file counts too.
    uses = index.FindUsesOutside({ "Textures", "Audio/x.wav" });
    CHECK(uses.size() == 3 && uses[0].Path == "Audio/x.wav" && uses[1].Path == "Textures/a.png" && uses[2].Path == "Textures/b.png");
    if (uses.size() == 3)
        CHECK(uses[0].Referrers.size() == 1 && uses[0].Referrers[0].Has(AssetReferenceKind::Code) &&
              uses[0].Referrers[0].Name == "Source/Game.cpp" && uses[0].Referrers[0].AssetPath.empty());
    // A scene deleted together with its only user is fine; a C++ file outside the assets is not an asset.
    CHECK(index.FindUsesOutside({ "Textures/a.png", "Scenes" }).empty());
    CHECK(index.FindUsesOutside({ dir / "Source" / "Game.cpp" }).empty() && index.FindUsesOutside({}).empty());

    // The open scene's unsaved state counts instead of its file (the editor passes it).
    sources.Overrides[(assets / "Scenes" / "Other.zscene").string()] = R"({ "Scene": "Other", "Entities": [] })";
    index.Build(sources);
    uses = index.FindUsesOutside({ "Textures/a.png" });
    CHECK(uses.size() == 1 && uses[0].Referrers.size() == 1 && uses[0].GetReferenceCount() == 2);

    // A new scene that was never saved counts too, under its own name (no file to read or show).
    CHECK(index.FindUsesOutside({ "Textures/unused.png" }).empty());
    AssetReferenceIndex::UnsavedFile open;
    open.File = dir / ".zamin" / "Unsaved" / "open-scene.zscene";
    open.Name = "Yangi sahna (ochiq sahna, saqlanmagan)";
    open.Text = R"({ "Scene": "Yangi sahna", "Entities": [
        { "Tag": "U", "SpriteRenderer": { "Texture": "Textures/unused.png" } },
        { "Tag": "M", "SpriteRenderer": { "Texture": "Textures/missing.png" } } ] })";
    sources.Unsaved = { open };
    index.Build(sources);
    uses = index.FindUsesOutside({ "Textures/unused.png" });
    CHECK(uses.size() == 1 && uses[0].Referrers.size() == 1 && uses[0].GetReferenceCount() == 1);
    if (uses.size() == 1 && uses[0].Referrers.size() == 1)
    {
        const AssetReferrer& r = uses[0].Referrers[0];
        CHECK(r.Unsaved && r.Name == open.Name && r.AssetPath.empty() && r.Has(AssetReferenceKind::Path));
    }
    CHECK(!index.GetReferrers("Textures/a.png").empty() && !index.GetReferrers("Textures/a.png")[0].Unsaved);
    bool missingFromOpen = false;
    for (const MissingAssetReference& m : index.GetMissing())
        missingFromOpen |= m.Unsaved && m.Name == open.Name && m.Path == "Textures/missing.png";
    CHECK(missingFromOpen);
    std::vector<UnusedAsset> unused = index.FindUnused();
    CHECK(std::none_of(unused.begin(), unused.end(), [](const UnusedAsset& u) { return u.Path == "Textures/unused.png"; }));
    // A path where a file is would be read twice: it is not taken as an unsaved one.
    sources.Unsaved[0].File = assets / "Scenes" / "Main.zscene";
    index.Build(sources);
    CHECK(index.FindUsesOutside({ "Textures/unused.png" }).empty());
    sources.Unsaved.clear();
    index.Build(sources);
    CHECK(index.FindUsesOutside({ "Textures/unused.png" }).empty() && index.GetMissing().empty());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AssetThumbnailsAndDiskCache)
{
    namespace fs = std::filesystem;
    fs::path dir = MakeTempDir("zamin-thumbnails-");

    // Downscaling: fits the size, keeps the aspect, averages with alpha weights, never enlarges.
    std::vector<uint8_t> pixels = {
        255, 0, 0, 0,     0, 0, 255, 255,   10, 20, 30, 255,  10, 20, 30, 255,
        0, 0, 255, 255,   0, 0, 255, 255,   50, 60, 70, 255,  30, 40, 50, 255,
    };
    ThumbnailImage small = AssetThumbnails::Downscale(pixels.data(), 4, 2, 2);
    CHECK(small.IsValid() && small.Width == 2 && small.Height == 1);
    if (small.IsValid())
    {
        // Left 2x2: one transparent red pixel and three blue ones -> blue, 3/4 opaque.
        CHECK(small.Pixels[0] == 0 && small.Pixels[2] == 255 && small.Pixels[3] == 191);
        CHECK(small.Pixels[4] == 25 && small.Pixels[5] == 35 && small.Pixels[6] == 45 && small.Pixels[7] == 255);
    }
    ThumbnailImage same = AssetThumbnails::Downscale(pixels.data(), 4, 2, 128);
    CHECK(same.Width == 4 && same.Height == 2 && same.Pixels == pixels);
    CHECK(!AssetThumbnails::Downscale(nullptr, 4, 2, 2).IsValid() && AssetThumbnails::Downscale(pixels.data(), 4, 2, 1).Width == 1);

    // A PNG (top row red, bottom row blue): the picture is top row first whatever another loader
    // left stb's global flip setting at.
    std::vector<uint8_t> image(64 * 32 * 4);
    for (uint32_t y = 0; y < 32; y++)
        for (uint32_t x = 0; x < 64; x++)
        {
            uint8_t* p = &image[(y * 64 + x) * 4];
            p[0] = y < 16 ? 255 : 0;
            p[1] = 0;
            p[2] = y < 16 ? 0 : 255;
            p[3] = 255;
        }
    std::vector<uint8_t> png = Image::EncodePNG(64, 32, 4, image.data());
    CHECK(!png.empty() && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    fs::path pic = dir / "Textures" / "pic.png";
    fs::create_directories(pic.parent_path());
    {
        std::ofstream out(pic, std::ios::binary);
        out.write((const char*)png.data(), (std::streamsize)png.size());
    }
    // A texture load sets stb to flip rows (its failure is expected: kept off the terminal).
    WriteFile(dir / "broken.png", "not an image");
    bool terminal = Log::GetTerminalOutput();
    Log::SetTerminalOutput(false);
    CHECK(TextureLibrary::Load(dir / "broken.png") == nullptr);
    Log::SetTerminalOutput(terminal);
    AssetImportLog::Clear(dir / "broken.png");
    ThumbnailImage thumb;
    std::string error;
    CHECK(AssetThumbnails::CanGenerate(pic) && AssetThumbnails::Generate(pic, 16, thumb, &error) && error.empty());
    CHECK(thumb.Width == 16 && thumb.Height == 8 && thumb.IsValid());
    if (thumb.IsValid())
        CHECK(thumb.Pixels[0] == 255 && thumb.Pixels[2] == 0 && thumb.Pixels[(7 * 16) * 4 + 2] == 255 && thumb.Pixels[(7 * 16) * 4] == 0);
    // ... also on another thread (the content browser's worker).
    ThumbnailImage threaded;
    std::thread worker([&]() { AssetThumbnails::Generate(pic, 16, threaded); });
    worker.join();
    CHECK(threaded.Pixels == thumb.Pixels);
    // Made on this thread, too: the loaders after it still flip when they ask for it (stb's
    // per-thread setting would otherwise hide the global one from now on).
    ImageData flippedPic, plainPic;
    CHECK(Image::Load(pic.string(), flippedPic, 4, true) && Image::Load(pic.string(), plainPic, 4, false));
    CHECK(flippedPic.Pixels.size() == image.size() && flippedPic.Pixels[0] == 0 && flippedPic.Pixels[2] == 255); // bottom (blue) row first
    CHECK(plainPic.Pixels == image);
    CHECK(AssetThumbnails::Generate(pic, 16, threaded) && threaded.Pixels == thumb.Pixels);

    // An HDR map is tone mapped (1.0 -> 0.5 -> gamma), a font gets "Aa", other files nothing.
    std::string hdr = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    for (int i = 0; i < 8; i++)
        hdr += std::string("\x80\x80\x80\x81", 4); // RGBE of 1.0
    WriteFile(dir / "sky.hdr", hdr);
    CHECK(AssetThumbnails::Generate(dir / "sky.hdr", 128, thumb, &error) && thumb.Width == 4 && thumb.Height == 2);
    if (thumb.IsValid())
        CHECK(thumb.Pixels[0] >= 184 && thumb.Pixels[0] <= 188 && thumb.Pixels[3] == 255);
    fs::path fontFile = Renderer::GetResourcesDirectory() / "fonts" / "Inter-SemiBold.ttf";
    if (fs::exists(fontFile))
    {
        CHECK(AssetThumbnails::Generate(fontFile, 64, thumb, &error) && thumb.Width == 64 && thumb.Height == 64);
        size_t inked = 0;
        for (size_t i = 3; i < thumb.Pixels.size(); i += 4)
            inked += thumb.Pixels[i] > 128;
        CHECK(inked > 200 && inked < 64 * 64 / 2); // letters, not a filled square
    }
    CHECK(!AssetThumbnails::Generate(dir / "broken.png", 64, thumb, &error) && error.find("stb:") != std::string::npos && !thumb.IsValid());
    CHECK(!AssetThumbnails::Generate(dir / "gone.png", 64, thumb, &error) && error == "fayl topilmadi");
    WriteFile(dir / "bad.ttf", "nope");
    CHECK(!AssetThumbnails::Generate(dir / "bad.ttf", 64, thumb, &error) && !error.empty());
    CHECK(!AssetThumbnails::CanGenerate(dir / "Robot.gltf") && !AssetThumbnails::CanGenerate(dir / "Level.zscene"));

    // Stamps: the file's size and time, and the size made for.
    std::optional<ThumbnailStamp> stamp = ThumbnailStamp::Read(pic, 128);
    CHECK(stamp && stamp->Size == png.size() && stamp->MaxSize == 128 && stamp->Time != 0);
    CHECK(!ThumbnailStamp::Read(dir / "gone.png", 128));
    CHECK(stamp && *stamp == ThumbnailStamp::From(png.size(), fs::last_write_time(pic), 128));

    // The disk cache: saved and found again for the same version only.
    ThumbnailDiskCache disabled;
    CHECK(!disabled.IsEnabled() && !disabled.Save(pic, *stamp, small) && !disabled.Load(pic, *stamp, thumb));
    ThumbnailDiskCache cache(dir / ".zamin" / "Thumbnails");
    CHECK(AssetThumbnails::Generate(pic, 32, thumb) && stamp && cache.Save(pic, *stamp, thumb));
    fs::path cacheFile = cache.GetCacheFile(pic);
    CHECK(fs::exists(cacheFile) && cacheFile.parent_path() == dir / ".zamin" / "Thumbnails" && cacheFile.extension() == ".zthumb");
    CHECK(cache.GetCacheFile(dir / "Textures" / "." / "pic.png") == cacheFile && cache.GetCacheFile(dir / "other.png") != cacheFile);
    ThumbnailImage loaded;
    CHECK(cache.Load(pic, *stamp, loaded) && loaded.Width == thumb.Width && loaded.Height == thumb.Height && loaded.Pixels == thumb.Pixels);
    ThumbnailStamp newer = *stamp;
    newer.Time += 1;
    CHECK(!cache.Load(pic, newer, loaded));
    ThumbnailStamp bigger = *stamp;
    bigger.MaxSize = 256;
    CHECK(!cache.Load(pic, bigger, loaded) && !cache.Load(dir / "other.png", *stamp, loaded));
    // Broken cache files are ignored, and pruned with the thumbnails of files that are gone.
    fs::path hdrStampFile = cache.GetCacheFile(dir / "sky.hdr");
    CHECK(AssetThumbnails::Generate(dir / "sky.hdr", 32, thumb) && cache.Save(dir / "sky.hdr", *ThumbnailStamp::Read(dir / "sky.hdr", 32), thumb));
    WriteFile(cache.GetCacheFile(dir / "x.png"), "ZTHB garbage");
    WriteFile(dir / ".zamin" / "Thumbnails" / "half.zthumb.tmp", "x");
    CHECK(!cache.Load(dir / "x.png", *stamp, loaded));
    fs::remove(dir / "sky.hdr");
    CHECK(cache.Prune() == 3 && fs::exists(cacheFile) && !fs::exists(hdrStampFile));
    CHECK(cache.Load(pic, *stamp, loaded));
    cache.Remove(pic);
    CHECK(!fs::exists(cacheFile) && !cache.Load(pic, *stamp, loaded));
    CHECK(cache.Save(pic, *stamp, small) && cache.Clear() == 1 && !fs::exists(cacheFile));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ------------------------------------------------------------------ materials (.zmat)
// A texture without a GPU: the material tests hand these out instead of loading files.
class FakeTexture : public Texture2D
{
public:
    explicit FakeTexture(std::string path) : m_Path(std::move(path)) {}
    const TextureSpecification& GetSpecification() const override { return m_Spec; }
    uint32_t GetWidth() const override { return 1; }
    uint32_t GetHeight() const override { return 1; }
    uint32_t GetRendererID() const override { return 0; }
    const std::string& GetPath() const override { return m_Path; }
    void SetData(const void*, uint32_t) override {}
    void Bind(uint32_t = 0) const override {}
    bool IsLoaded() const override { return true; }
    bool operator==(const Texture& other) const override { return this == &other; }
    bool Reload(std::string* = nullptr) override { return true; }
    bool Reimport(const TextureImportSettings&, std::string* = nullptr) override { return true; }

private:
    TextureSpecification m_Spec;
    std::string m_Path;
};

// Installs a texture loader that makes one FakeTexture per path and counts the calls.
struct FakeTextureLoader
{
    std::unordered_map<std::string, Ref<Texture2D>> Textures;
    int Calls = 0;
    std::set<std::string> Missing; // paths that "fail" (nullptr)

    FakeTextureLoader()
    {
        MaterialLibrary::SetTextureLoader([this](const std::string& path) -> Ref<Texture2D> {
            Calls++;
            if (Missing.count(path))
                return nullptr;
            auto& texture = Textures[path];
            if (!texture)
                texture = CreateRef<FakeTexture>(path);
            return texture;
        });
    }
    ~FakeTextureLoader()
    {
        MaterialLibrary::SetTextureLoader(nullptr);
        MaterialLibrary::Clear();
    }
};

TEST(MaterialFormatRoundtrip)
{
    // Defaults: an empty object is the default material, and the default material is what the
    // renderer's Material starts as.
    MaterialData defaults;
    MaterialData read;
    CHECK(MaterialSerializer::DeserializeFromString("{}", read) && read == defaults);
    CHECK(defaults.Shader == MaterialShader::Lit && defaults.BlendMode == MaterialBlendMode::Opaque);
    CHECK(defaults.Tiling == glm::vec2(1.0f) && defaults.Offset == glm::vec2(0.0f) && defaults.Albedo == glm::vec4(1.0f));
    CHECK(MaterialData::FromMaterial(Material{}) == defaults);

    MaterialData d;
    d.Shader = MaterialShader::Unlit;
    d.BlendMode = MaterialBlendMode::Cutout;
    d.AlphaCutoff = 0.35f;
    d.DoubleSided = true;
    d.Albedo = { 0.2f, 0.4f, 0.6f, 0.8f };
    d.AlbedoMap = "Textures/leaves.png";
    d.Metallic = 0.75f;
    d.Roughness = 0.125f;
    d.MetalRoughMap = "Textures/mr.png";
    d.NormalMap = "Textures/n.png";
    d.NormalStrength = 1.5f;
    d.Emission = { 1.0f, 0.5f, 0.25f };
    d.EmissionStrength = 3.0f;
    d.EmissiveMap = "Textures/e.png";
    d.Tiling = { 2.0f, 4.0f };
    d.Offset = { 0.5f, -0.25f };
    std::string text = MaterialSerializer::SerializeToString(d);
    CHECK(text.find("\"Version\": 1") != std::string::npos);
    CHECK(text.find("\"Shader\": \"Unlit\"") != std::string::npos && text.find("\"BlendMode\": \"Cutout\"") != std::string::npos);
    MaterialData back;
    CHECK(MaterialSerializer::DeserializeFromString(text, back));
    CHECK(back == d);
    CHECK(d.GetTexturePaths() == std::vector<std::string>({ "Textures/leaves.png", "Textures/mr.png", "Textures/n.png", "Textures/e.png" }));

    // Old single-number tiling, unknown names, out of range cutoff, partial vectors.
    CHECK(MaterialSerializer::DeserializeFromString(R"({ "Tiling": 4, "Shader": "Toon", "BlendMode": "Additive", "AlphaCutoff": 3,
                                                        "Albedo": [0.5, 0.5] })", read));
    CHECK(read.Tiling == glm::vec2(4.0f) && read.Shader == MaterialShader::Lit && read.BlendMode == MaterialBlendMode::Opaque);
    CHECK(read.AlphaCutoff == 1.0f && read.Albedo == glm::vec4(1.0f));
    CHECK(MaterialSerializer::DeserializeFromString(R"({ "Tiling": [3, 0.5], "BlendMode": "Transparent", "Version": 99 })", read));
    CHECK(read.Tiling == glm::vec2(3.0f, 0.5f) && read.BlendMode == MaterialBlendMode::Transparent);

    std::string error;
    CHECK(!MaterialSerializer::DeserializeFromString("{ broken", read, &error) && !error.empty());
    CHECK(!MaterialSerializer::DeserializeFromString("[1, 2]", read, &error));
    CHECK(!MaterialSerializer::Deserialize("/no/such/dir/x.zmat", read, &error) && !error.empty());

    for (MaterialShader shader : { MaterialShader::Lit, MaterialShader::Unlit })
        CHECK(MaterialSerializer::ShaderFromString(MaterialSerializer::ShaderToString(shader)) == shader);
    for (MaterialBlendMode mode : { MaterialBlendMode::Opaque, MaterialBlendMode::Cutout, MaterialBlendMode::Transparent })
        CHECK(MaterialSerializer::BlendModeFromString(MaterialSerializer::BlendModeToString(mode)) == mode);
    CHECK(!MaterialSerializer::ShaderFromString("lit") && !MaterialSerializer::BlendModeFromString(""));

    // Values into a renderer material and back.
    Material m;
    d.ApplyValuesTo(m);
    CHECK(m.Shader == MaterialShader::Unlit && m.BlendMode == MaterialBlendMode::Cutout && m.DoubleSided && m.AlphaCutoff == 0.35f);
    CHECK(m.Tiling == glm::vec2(2.0f, 4.0f) && m.Offset == glm::vec2(0.5f, -0.25f) && m.EmissionStrength == 3.0f);
    MaterialData values = MaterialData::FromMaterial(m);
    values.AlbedoMap = d.AlbedoMap; values.MetalRoughMap = d.MetalRoughMap; values.NormalMap = d.NormalMap; values.EmissiveMap = d.EmissiveMap;
    CHECK(values == d);
}

TEST(MaterialAssetLibraryAndReload)
{
    namespace fs = std::filesystem;
    Project::SetActive(nullptr);
    FakeTextureLoader loader;
    fs::path dir = MakeTempDir("zamin-materials-");
    fs::path file = dir / "Materials" / "Gold.zmat";
    MaterialData gold;
    gold.Albedo = { 1.0f, 0.8f, 0.3f, 1.0f };
    gold.Metallic = 1.0f;
    gold.AlbedoMap = (dir / "Textures" / "gold.png").string();
    CHECK(MaterialSerializer::Serialize(gold, file)); // makes the folder

    // One shared object per file, whatever the spelling of the path.
    Ref<MaterialAsset> a = MaterialLibrary::Load(file);
    CHECK(a && a->GetData() == gold && a->GetPath() == fs::path(MaterialLibrary::MakeKey(file)));
    CHECK(MaterialLibrary::Load(dir / "Materials" / "." / "Gold.zmat") == a && MaterialLibrary::Find(file) == a);
    CHECK(MaterialLibrary::IsFileLoaded(file) && MaterialLibrary::GetLoaded().size() == 1);
    CHECK(a->GetMaterial().AlbedoMap && a->GetMaterial().AlbedoMap->GetPath() == gold.AlbedoMap && a->GetMaterial().Metallic == 1.0f);
    CHECK(!a->GetMaterial().NormalMap && loader.Calls == 1); // empty paths never reach the loader
    CHECK(MaterialLibrary::UsesTexture(dir / "Textures" / "gold.png") && !MaterialLibrary::UsesTexture(dir / "Textures" / "x.png"));

    // Edits: only changed texture paths are looked up again; every change has a new revision.
    uint64_t revision = a->GetRevision();
    MaterialData edit = a->GetData();
    edit.Roughness = 0.1f;
    a->SetData(edit);
    CHECK(a->GetRevision() != revision && loader.Calls == 1 && a->GetMaterial().Roughness == 0.1f);
    revision = a->GetRevision();
    a->SetData(edit); // the same values: nothing changes
    CHECK(a->GetRevision() == revision);
    edit.NormalMap = "Textures/n.png";
    a->SetData(edit);
    CHECK(loader.Calls == 2 && a->GetMaterial().NormalMap && a->GetMaterial().NormalMap->GetPath() == "Textures/n.png");

    // Instances: a copy whose edits leave the shared asset alone.
    Ref<MaterialAsset> copy = a->Clone();
    CHECK(copy != a && copy->GetData() == a->GetData() && copy->GetMaterial().AlbedoMap == a->GetMaterial().AlbedoMap);
    MaterialData red = copy->GetData();
    red.Albedo = { 1.0f, 0.0f, 0.0f, 1.0f };
    copy->SetData(red);
    CHECK(a->GetData().Albedo == gold.Albedo && copy->GetMaterial().Albedo == red.Albedo && copy->GetRevision() != a->GetRevision());
    CHECK(MaterialLibrary::Find(file) == a); // the clone is not cached

    // The file changed on disk: read again into the same object (what hot reload does).
    MaterialData changed = gold;
    changed.Roughness = 0.9f;
    changed.Tiling = { 3.0f, 3.0f };
    CHECK(MaterialSerializer::Serialize(changed, file));
    bool failed = false;
    CHECK(MaterialLibrary::ReloadFile(file, &failed) == 1 && !failed);
    CHECK(MaterialLibrary::Find(file) == a && a->GetData() == changed && a->GetMaterial().Tiling == glm::vec2(3.0f));
    // Saved by the editor with what it already holds: nothing to update.
    CHECK(a->Save() && MaterialLibrary::ReloadFile(file, &failed) == 0 && !failed);
    // Broken on disk: the old content stays and the error is reported (once) for the file.
    AssetImportLog::ClearAll();
    FileSystem::WriteTextFile(file, "{ not json");
    CHECK(MaterialLibrary::ReloadFile(file, &failed) == 0 && failed);
    CHECK(a->GetData() == changed && AssetImportLog::HasError(file));
    CHECK(AssetImportLog::Find(file)->Reload);
    CHECK(MaterialSerializer::Serialize(changed, file));
    CHECK(a->Reload() && !AssetImportLog::HasError(file));

    // A texture appeared / changed: the materials using it take it again.
    loader.Missing.insert("Textures/n.png");
    edit = a->GetData();
    edit.NormalMap = "Textures/n.png";
    a->SetData(edit);
    CHECK(!a->GetMaterial().NormalMap);
    loader.Missing.clear();
    CHECK(MaterialLibrary::ReloadFile(dir / "Textures" / "gold.png") == 1 && a->GetMaterial().NormalMap);
    // The same through hot reload, when a texture file a material names appears.
    {
        MaterialData withAbsolute = a->GetData();
        withAbsolute.EmissiveMap = (dir / "Textures" / "glow.png").string();
        loader.Missing.insert(withAbsolute.EmissiveMap);
        a->SetData(withAbsolute);
        CHECK(!a->GetMaterial().EmissiveMap);
        loader.Missing.clear();
        AssetHotReload watcher;
        AssetHotReload::Report added = watcher.Apply({ { dir / "Textures" / "glow.png", FileChangeType::Added, false } }, nullptr);
        CHECK(added.Reloaded.size() == 1 && a->GetMaterial().EmissiveMap);
    }

    // Hot reload knows .zmat files.
    CHECK(AssetHotReload::CanReload("Materials/Glass.zmat") && AssetHotReload::IsLoaded(file));
    changed.Metallic = 0.5f;
    CHECK(MaterialSerializer::Serialize(changed, file));
    AssetHotReload hotReload;
    AssetHotReload::Report report = hotReload.Apply({ { file, FileChangeType::Modified, false } }, nullptr);
    CHECK(report.Reloaded.size() == 1 && report.Failed.empty() && a->GetData().Metallic == 0.5f);

    // Renamed: the cached object follows (scenes resolving the new path get it).
    fs::path moved = dir / "Materials" / "Brass.zmat";
    fs::rename(file, moved);
    MaterialLibrary::OnFileMoved(file, moved);
    CHECK(!MaterialLibrary::Find(file) && MaterialLibrary::Load(moved) == a && a->GetPath() == fs::path(MaterialLibrary::MakeKey(moved)));
    // A whole folder moved.
    fs::rename(dir / "Materials", dir / "Mats");
    MaterialLibrary::OnFileMoved(dir / "Materials", dir / "Mats");
    CHECK(MaterialLibrary::Find(dir / "Mats" / "Brass.zmat") == a);

    // Missing / broken files: nullptr, reported once, not read again until the file changes.
    AssetImportLog::ClearAll();
    CHECK(!MaterialLibrary::Load(dir / "Nope.zmat") && AssetImportLog::HasError(dir / "Nope.zmat"));
    CHECK(!AssetImportLog::ShouldRetry(dir / "Nope.zmat") && !MaterialLibrary::Load(dir / "Nope.zmat"));
    FileSystem::WriteTextFile(dir / "Broken.zmat", "[]");
    CHECK(!MaterialLibrary::Load(dir / "Broken.zmat") && AssetImportLog::HasError(dir / "Broken.zmat"));
    MaterialSerializer::Serialize(MaterialData{}, dir / "Broken.zmat");
    AssetImportLog::OnFileChanged(dir / "Broken.zmat");
    CHECK(MaterialLibrary::Load(dir / "Broken.zmat") && !AssetImportLog::HasError(dir / "Broken.zmat"));
    AssetImportLog::ClearAll();

    // Released when nothing holds it any more.
    CHECK(!MaterialLibrary::ReleaseIfUnused(dir / "Mats" / "Brass.zmat"));
    a.reset();
    CHECK(MaterialLibrary::ReleaseIfUnused(dir / "Mats" / "Brass.zmat") && !MaterialLibrary::Find(dir / "Mats" / "Brass.zmat"));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(MeshRendererMaterialAssets)
{
    namespace fs = std::filesystem;
    FakeTextureLoader loader;
    fs::path dir = MakeTempDir("zamin-mesh-materials-");
    CHECK(Project::CreateFromTemplate(dir, "MatGame") != nullptr);
    fs::path assets = dir / "Assets";
    WriteFile(assets / "Textures" / "wood.png", "png");
    MaterialData gold;
    gold.Albedo = { 1.0f, 0.8f, 0.3f, 1.0f };
    gold.Metallic = 1.0f;
    MaterialData red;
    red.Albedo = { 1.0f, 0.0f, 0.0f, 1.0f };
    red.AlbedoMap = "Textures/wood.png";
    CHECK(MaterialSerializer::Serialize(gold, assets / "Materials" / "Gold.zmat"));
    CHECK(MaterialSerializer::Serialize(red, assets / "Materials" / "Red.zmat"));
    bool writes = AssetRegistry::GetWriteMetaFilesByDefault();
    AssetRegistry::SetWriteMetaFilesByDefault(true);
    CHECK(Project::Load(dir / "MatGame.zproject") != nullptr);
    AssetRegistry* registry = Project::GetAssetRegistry();
    CHECK(registry && registry->GetHandle("Materials/Gold.zmat") && registry->GetHandle("Textures/wood.png"));
    // Texture UUIDs go next to their paths in the .zmat (renames outside the editor are followed).
    std::string redText = MaterialSerializer::SerializeToString(red);
    CHECK(redText.find("\"AlbedoMapUUID\": \"" + registry->GetHandle("Textures/wood.png").ToString() + "\"") != std::string::npos);
    CHECK(MaterialSerializer::Serialize(red, assets / "Materials" / "Red.zmat"));

    // A scene with material assets: the path (+ UUID) and per-submesh slots round trip.
    auto scene = CreateRef<Scene>();
    Entity box = scene->CreateEntity("Box");
    auto& mr = box.AddComponent<MeshRendererComponent>();
    mr.MaterialPath = "Materials/Gold.zmat";
    mr.Mat.Tiling = { 2.0f, 3.0f };
    mr.Mat.Offset = { 0.5f, 0.25f };
    Entity statue = scene->CreateEntity("Statue");
    auto& model = statue.AddComponent<MeshRendererComponent>();
    model.Type = PrimitiveMesh::Model;
    model.ModelPath = "Models/statue.gltf";
    model.SubmeshMaterials = { "", "Materials/Red.zmat" };
    Entity plain = scene->CreateEntity("Plain");
    plain.AddComponent<MeshRendererComponent>().Mat.Tiling = glm::vec2(4.0f);
    std::string text = SceneSerializer(scene).SerializeToString();
    CHECK(text.find("\"MaterialUUID\": \"" + registry->GetHandle("Materials/Gold.zmat").ToString() + "\"") != std::string::npos);
    CHECK(text.find("\"Tiling\": 4.0") != std::string::npos); // uniform tiling stays one number

    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    Entity box2 = loaded->FindEntityByName("Box"), statue2 = loaded->FindEntityByName("Statue"), plain2 = loaded->FindEntityByName("Plain");
    CHECK(box2 && statue2 && plain2);
    if (box2 && statue2 && plain2)
    {
        auto& b = box2.GetComponent<MeshRendererComponent>();
        auto& s = statue2.GetComponent<MeshRendererComponent>();
        CHECK(b.MaterialPath == "Materials/Gold.zmat" && b.Mat.Tiling == glm::vec2(2.0f, 3.0f) && b.Mat.Offset == glm::vec2(0.5f, 0.25f));
        CHECK(s.MaterialPath.empty() && s.SubmeshMaterials == std::vector<std::string>({ "", "Materials/Red.zmat" }));
        CHECK(plain2.GetComponent<MeshRendererComponent>().Mat.Tiling == glm::vec2(4.0f));

        // Resolved: the shared library objects (no GPU mesh needed for the materials).
        Scene::ResolveMeshMaterials(b);
        Scene::ResolveMeshMaterials(s);
        Ref<MaterialAsset> goldAsset = MaterialLibrary::Find(assets / "Materials" / "Gold.zmat");
        CHECK(goldAsset && b.MaterialRef == goldAsset && s.SubmeshMaterialRefs.size() == 2 && !s.SubmeshMaterialRefs[0]);
        CHECK(s.SubmeshMaterialRefs.size() == 2 && s.SubmeshMaterialRefs[1] && s.SubmeshMaterialRefs[1]->GetMaterial().AlbedoMap);

        // What is drawn: the asset replaces the inline material of a primitive ...
        Material drawn = Scene::ComposeMeshMaterial(b, nullptr);
        CHECK(drawn.Metallic == 1.0f && drawn.Albedo == gold.Albedo && drawn.Tiling == glm::vec2(1.0f));
        // ... and is shared: an edit of the asset shows on every renderer using it.
        MaterialData edit = goldAsset->GetData();
        edit.Roughness = 0.05f;
        goldAsset->SetData(edit);
        CHECK(Scene::ComposeMeshMaterial(b, nullptr).Roughness == 0.05f);
        // Models: the slot's asset, else the component's asset, else the model's material + inline tint.
        Material modelMaterial;
        modelMaterial.Albedo = { 0.5f, 0.5f, 0.5f, 1.0f };
        modelMaterial.Tiling = { 2.0f, 2.0f };
        modelMaterial.Offset = { 0.1f, 0.0f };
        s.Mat.Albedo = { 1.0f, 0.5f, 1.0f, 1.0f };
        s.Mat.Tiling = { 3.0f, 1.0f };
        s.Mat.Offset = { 0.0f, 0.5f };
        CHECK(Scene::ComposeMeshMaterial(s, &modelMaterial, 1).Albedo == red.Albedo);
        Material tinted = Scene::ComposeMeshMaterial(s, &modelMaterial, 0);
        CHECK(tinted.Albedo == glm::vec4(0.5f, 0.25f, 0.5f, 1.0f) && tinted.Tiling == glm::vec2(6.0f, 2.0f));
        CHECK_NEAR(tinted.Offset.x, 0.3f, 1e-6f);
        CHECK_NEAR(tinted.Offset.y, 0.5f, 1e-6f);
        CHECK(Scene::ComposeMeshMaterial(s, &modelMaterial, 7).Albedo == tinted.Albedo); // no slot that far
        s.MaterialPath = "Materials/Gold.zmat";
        Scene::ResolveMeshMaterials(s);
        CHECK(Scene::ComposeMeshMaterial(s, &modelMaterial, 0).Metallic == 1.0f && Scene::ComposeMeshMaterial(s, &modelMaterial, 1).Albedo == red.Albedo);
        CHECK(Scene::GetMeshMaterialAsset(s, 0) == goldAsset && Scene::GetMeshMaterialAsset(s, 1) == s.SubmeshMaterialRefs[1]);
    }

    // An old scene (before material assets): loads as it was, single-number tiling.
    auto old = CreateRef<Scene>();
    CHECK(SceneSerializer(old).DeserializeFromString(R"({ "Scene": "Old", "Entities": [ { "Entity": 5, "Tag": "Floor",
        "MeshRenderer": { "Mesh": "Plane", "Tiling": 10.0, "Roughness": 0.9 } } ] })"));
    if (Entity floor = old->FindEntityByName("Floor"))
    {
        auto& f = floor.GetComponent<MeshRendererComponent>();
        CHECK(f.MaterialPath.empty() && f.SubmeshMaterials.empty() && f.Mat.Tiling == glm::vec2(10.0f) && f.Mat.Offset == glm::vec2(0.0f));
        CHECK(f.Mat.Roughness == 0.9f);
    }

    // Renamed outside the editor (with its .zmeta): the scene finds the material by its UUID, and
    // the material finds its texture the same way.
    fs::rename(assets / "Materials" / "Gold.zmat", assets / "Materials" / "Shiny.zmat");
    fs::rename(assets / "Materials" / "Gold.zmat.zmeta", assets / "Materials" / "Shiny.zmat.zmeta");
    fs::rename(assets / "Textures" / "wood.png", assets / "Textures" / "oak.png");
    fs::rename(assets / "Textures" / "wood.png.zmeta", assets / "Textures" / "oak.png.zmeta");
    CHECK(Project::Load(dir / "MatGame.zproject") != nullptr);
    auto repaired = CreateRef<Scene>();
    CHECK(SceneSerializer(repaired).DeserializeFromString(text));
    if (Entity e = repaired->FindEntityByName("Box"))
        CHECK(e.GetComponent<MeshRendererComponent>().MaterialPath == "Materials/Shiny.zmat");
    MaterialData redBack;
    CHECK(MaterialSerializer::Deserialize(assets / "Materials" / "Red.zmat", redBack) && redBack.AlbedoMap == "Textures/oak.png");
    CHECK(MaterialSerializer::Serialize(redBack, assets / "Materials" / "Red.zmat")); // saved with the repaired path

    // Renamed in the editor: material files that refer to the texture are rewritten as well.
    AssetFiles::MoveResult result = AssetFiles::Rename(assets / "Textures" / "oak.png", "pine.png", assets);
    CHECK(result.Errors.empty() && std::any_of(result.UpdatedFiles.begin(), result.UpdatedFiles.end(),
                                               [](const fs::path& f) { return f.filename() == "Red.zmat"; }));
    CHECK(MaterialSerializer::Deserialize(assets / "Materials" / "Red.zmat", redBack) && redBack.AlbedoMap == "Textures/pine.png");

    // Who uses what: the material refers to its texture, the scene to the material.
    AssetReferenceIndex index;
    CHECK(index.BuildForActiveProject());
    std::vector<std::string> uses = index.GetReferences(assets / "Materials" / "Red.zmat");
    CHECK(std::find(uses.begin(), uses.end(), "Textures/pine.png") != uses.end());

    AssetRegistry::SetWriteMetaFilesByDefault(writes);
    Project::SetActive(nullptr);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(TransparentSortBackToFront)
{
    // Camera at z = 10 looking at the origin: farther = smaller z. Equal depths keep their order.
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<glm::vec3> positions = { { 0, 0, 0 }, { 0, 0, -5 }, { 0, 0, 5 }, { 3, 0, 0 } };
    CHECK(Renderer3D::SortBackToFront(view, positions) == std::vector<size_t>({ 1, 0, 3, 2 }));
    CHECK(Renderer3D::SortBackToFront(view, {}).empty());
    // Behind the camera counts as nearest (negative depth): drawn last.
    CHECK(Renderer3D::SortBackToFront(view, { { 0, 0, 20 }, { 0, 0, 0 } }) == std::vector<size_t>({ 1, 0 }));
}

TEST(MaterialSurfaceStates)
{
    // Opaque single-sided: its own pass, back faces culled, no blending, casts a plain shadow.
    Material opaque;
    SurfaceState s = Renderer3D::GetSurfaceState(opaque, glm::mat4(1.0f));
    CHECK(s.Queue == SurfaceState::Pass::Opaque && s.Cull == CullMode::Back && !s.Mirrored && !s.TwoSidedBlend);
    CHECK(s.Blend == BlendMode::None && s.DepthWrite && s.CastsShadow && !s.AlphaTestedShadow);
    CHECK(!Renderer3D::GetSurfaceState(opaque, glm::mat4(1.0f), false).CastsShadow); // MeshRenderer.CastShadows off
    // Unlit changes the lighting, not the pass.
    Material unlit;
    unlit.Shader = MaterialShader::Unlit;
    CHECK(Renderer3D::GetSurfaceState(unlit, glm::mat4(1.0f)).Queue == SurfaceState::Pass::Opaque);

    // Cutout: drawn after the opaque pass, the shadow is alpha tested (same holes).
    Material cutout;
    cutout.BlendMode = MaterialBlendMode::Cutout;
    s = Renderer3D::GetSurfaceState(cutout, glm::mat4(1.0f));
    CHECK(s.Queue == SurfaceState::Pass::Cutout && s.CastsShadow && s.AlphaTestedShadow && s.DepthWrite && s.Blend == BlendMode::None);
    s = Renderer3D::GetSurfaceState(cutout, glm::mat4(1.0f), false);
    CHECK(!s.CastsShadow && !s.AlphaTestedShadow);

    // Transparent: premultiplied blending, no depth write, never a shadow (even when asked).
    Material glass;
    glass.BlendMode = MaterialBlendMode::Transparent;
    s = Renderer3D::GetSurfaceState(glass, glm::mat4(1.0f), true);
    CHECK(s.Queue == SurfaceState::Pass::Transparent && s.Blend == BlendMode::Premultiplied && !s.DepthWrite);
    CHECK(!s.CastsShadow && !s.AlphaTestedShadow && s.Cull == CullMode::Back && !s.TwoSidedBlend);
    glass.DoubleSided = true; // inner faces first, then the outer ones
    s = Renderer3D::GetSurfaceState(glass, glm::mat4(1.0f));
    CHECK(s.Cull == CullMode::None && s.TwoSidedBlend);

    // Double-sided: nothing culled. A mirroring transform (odd number of negative scales) turns
    // the winding around: the front faces are culled instead of the back ones.
    Material leaf;
    leaf.DoubleSided = true;
    CHECK(Renderer3D::GetSurfaceState(leaf, glm::mat4(1.0f)).Cull == CullMode::None);
    glm::mat4 mirror = glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f));
    s = Renderer3D::GetSurfaceState(opaque, mirror);
    CHECK(s.Mirrored && s.Cull == CullMode::Front);
    s = Renderer3D::GetSurfaceState(leaf, mirror);
    CHECK(s.Mirrored && s.Cull == CullMode::None);
    CHECK(!Renderer3D::GetSurfaceState(opaque, glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, -1.0f, 1.0f))).Mirrored);
    glm::mat4 turned = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 1.0f, -2.0f)) *
                       glm::rotate(glm::mat4(1.0f), 2.5f, glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f))) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    s = Renderer3D::GetSurfaceState(opaque, turned);
    CHECK(!s.Mirrored && s.Cull == CullMode::Back);
}

TEST(PrimitiveWindingFacesOutward)
{
    // Back faces are culled on single-sided materials: every primitive triangle must wind
    // counter-clockwise seen from the side its normals point to.
    for (PrimitiveMesh type : { PrimitiveMesh::Cube, PrimitiveMesh::Sphere, PrimitiveMesh::Plane, PrimitiveMesh::Cylinder, PrimitiveMesh::Cone })
    {
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        MeshLibrary::GeneratePrimitive(type, vertices, indices);
        CHECK(!vertices.empty() && indices.size() % 3 == 0 && indices.size() >= 6);
        int inward = 0, checked = 0;
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const MeshVertex &a = vertices[indices[i]], &b = vertices[indices[i + 1]], &c = vertices[indices[i + 2]];
            glm::vec3 face = glm::cross(b.Position - a.Position, c.Position - a.Position);
            if (glm::length(face) < 1e-7f) continue; // degenerate (sphere poles)
            checked++;
            if (glm::dot(face, a.Normal + b.Normal + c.Normal) <= 0.0f)
                inward++;
        }
        CHECK(checked > 0 && inward == 0);
    }
}

TEST(GLTFMaterialModes)
{
    namespace fs = std::filesystem;
    fs::path dir = MakeTempDir("zamin-gltf-modes-");
    // alphaMode / alphaCutoff / doubleSided / KHR_materials_unlit -> blend mode, cutoff, double-sided, shader
    WriteFile(dir / "Garden.gltf", R"({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "KHR_materials_unlit" ],
  "materials": [
    { "name": "Plain" },
    { "name": "Leaves", "alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true },
    { "name": "Fence", "alphaMode": "MASK" },
    { "name": "Glass", "alphaMode": "BLEND", "pbrMetallicRoughness": { "baseColorFactor": [ 1, 1, 1, 0.25 ], "roughnessFactor": 0.05 } },
    { "name": "Sign", "extensions": { "KHR_materials_unlit": {} }, "emissiveFactor": [ 1, 0.5, 0 ] }
  ]
})");
    std::vector<Material> materials;
    std::string error;
    CHECK(Model::ReadMaterials(dir / "Garden.gltf", materials, &error));
    CHECK(materials.size() == 5);
    if (materials.size() == 5)
    {
        const Material &plain = materials[0], &leaves = materials[1], &fence = materials[2], &glass = materials[3], &sign = materials[4];
        CHECK(plain.BlendMode == MaterialBlendMode::Opaque && plain.Shader == MaterialShader::Lit && !plain.DoubleSided);
        CHECK(leaves.BlendMode == MaterialBlendMode::Cutout && leaves.DoubleSided);
        CHECK_NEAR(leaves.AlphaCutoff, 0.3f, 1e-6f);
        CHECK(fence.BlendMode == MaterialBlendMode::Cutout && !fence.DoubleSided);
        CHECK_NEAR(fence.AlphaCutoff, 0.5f, 1e-6f); // the glTF default
        // glTF BLEND composites the whole surface by alpha (no preserved highlights)
        CHECK(glass.BlendMode == MaterialBlendMode::Transparent && !glass.PreserveSpecular);
        CHECK_NEAR(glass.Albedo.a, 0.25f, 1e-6f);
        CHECK_NEAR(glass.Roughness, 0.05f, 1e-6f);
        CHECK(sign.Shader == MaterialShader::Unlit && sign.BlendMode == MaterialBlendMode::Opaque);
        CHECK(sign.EmissionStrength == 1.0f && sign.Emission == glm::vec3(1.0f, 0.5f, 0.0f));
        CHECK(!plain.AlbedoMap && !glass.AlbedoMap); // no textures (no GPU)
    }
    // Unreadable files fail with a reason; .obj files have one default material.
    WriteFile(dir / "Broken.gltf", "{ not json");
    error.clear();
    CHECK(!Model::ReadMaterials(dir / "Broken.gltf", materials, &error) && !error.empty() && materials.empty());
    CHECK(!Model::ReadMaterials(dir / "Missing.glb", materials, &error));
    WriteFile(dir / "Crate.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    CHECK(Model::ReadMaterials(dir / "Crate.obj", materials) && materials.size() == 1 &&
          materials[0].BlendMode == MaterialBlendMode::Opaque && !materials[0].DoubleSided);
    fs::remove_all(dir);
}

TEST(InlineMaterialModesRoundtrip)
{
    // The inline material's shader, blend mode, cutoff, double-sided and preserve-specular
    // settings round trip through a scene.
    auto scene = CreateRef<Scene>();
    auto& glass = scene->CreateEntity("Glass").AddComponent<MeshRendererComponent>();
    glass.Type = PrimitiveMesh::Sphere;
    glass.Mat.BlendMode = MaterialBlendMode::Transparent;
    glass.Mat.Albedo.a = 0.3f;
    glass.Mat.PreserveSpecular = false;
    glass.Mat.DoubleSided = true;
    auto& leaf = scene->CreateEntity("Leaf").AddComponent<MeshRendererComponent>();
    leaf.Type = PrimitiveMesh::Plane;
    leaf.Mat.BlendMode = MaterialBlendMode::Cutout;
    leaf.Mat.AlphaCutoff = 0.35f;
    leaf.Mat.DoubleSided = true;
    auto& sign = scene->CreateEntity("Sign").AddComponent<MeshRendererComponent>();
    sign.Mat.Shader = MaterialShader::Unlit;
    scene->CreateEntity("Box").AddComponent<MeshRendererComponent>();
    std::string text = SceneSerializer(scene).SerializeToString();
    CHECK(text.find("\"BlendMode\": \"Transparent\"") != std::string::npos && text.find("\"Shader\": \"Unlit\"") != std::string::npos);

    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    const Material& g = loaded->FindEntityByName("Glass").GetComponent<MeshRendererComponent>().Mat;
    const Material& l = loaded->FindEntityByName("Leaf").GetComponent<MeshRendererComponent>().Mat;
    const Material& u = loaded->FindEntityByName("Sign").GetComponent<MeshRendererComponent>().Mat;
    const Material& b = loaded->FindEntityByName("Box").GetComponent<MeshRendererComponent>().Mat;
    CHECK(g.BlendMode == MaterialBlendMode::Transparent && !g.PreserveSpecular && g.DoubleSided && g.Shader == MaterialShader::Lit);
    CHECK_NEAR(g.Albedo.a, 0.3f, 1e-6f);
    CHECK(l.BlendMode == MaterialBlendMode::Cutout && l.DoubleSided && l.PreserveSpecular);
    CHECK_NEAR(l.AlphaCutoff, 0.35f, 1e-6f);
    CHECK(u.Shader == MaterialShader::Unlit && u.BlendMode == MaterialBlendMode::Opaque && !u.DoubleSided);
    CHECK(b.Shader == MaterialShader::Lit && b.BlendMode == MaterialBlendMode::Opaque && !b.DoubleSided && b.PreserveSpecular);

    // Scenes from before blend modes: a see-through color stays see-through (the whole surface
    // fades, as it was drawn then); everything else is opaque and single-sided.
    const char* legacy = R"({ "Scene": "Old", "Version": 1, "Entities": [
      { "Entity": 11, "Tag": "Ghost", "Transform": { "Translation": [0, 0, 0], "Rotation": [0, 0, 0], "Scale": [1, 1, 1] },
        "MeshRenderer": { "Mesh": "Sphere", "Albedo": [1, 1, 1, 0.4], "Tiling": 2 } },
      { "Entity": 12, "Tag": "Wall", "Transform": { "Translation": [0, 0, 0], "Rotation": [0, 0, 0], "Scale": [1, 1, 1] },
        "MeshRenderer": { "Mesh": "Cube", "Albedo": [0.5, 0.5, 0.5, 1] } } ] })";
    auto old = CreateRef<Scene>();
    CHECK(SceneSerializer(old).DeserializeFromString(legacy));
    const Material& ghost = old->FindEntityByName("Ghost").GetComponent<MeshRendererComponent>().Mat;
    const Material& wall = old->FindEntityByName("Wall").GetComponent<MeshRendererComponent>().Mat;
    CHECK(ghost.BlendMode == MaterialBlendMode::Transparent && !ghost.PreserveSpecular && ghost.Tiling == glm::vec2(2.0f));
    CHECK(wall.BlendMode == MaterialBlendMode::Opaque && !wall.DoubleSided && wall.Shader == MaterialShader::Lit);
    CHECK_NEAR(wall.AlphaCutoff, 0.5f, 1e-6f);

    // .zmat: PreserveSpecular round trips; files without it keep it on.
    MaterialData data;
    CHECK(data.PreserveSpecular);
    data.BlendMode = MaterialBlendMode::Transparent;
    data.PreserveSpecular = false;
    MaterialData read;
    CHECK(MaterialSerializer::DeserializeFromString(MaterialSerializer::SerializeToString(data), read) && read == data);
    CHECK(MaterialSerializer::DeserializeFromString(R"({ "BlendMode": "Transparent" })", read) && read.PreserveSpecular);
    Material applied;
    data.ApplyValuesTo(applied);
    CHECK(!applied.PreserveSpecular && !MaterialData::FromMaterial(applied).PreserveSpecular);

    // A model's own modes stay unless the inline material asks for others.
    MeshRendererComponent model;
    model.Type = PrimitiveMesh::Model;
    Material own;
    own.BlendMode = MaterialBlendMode::Cutout;
    own.AlphaCutoff = 0.2f;
    Material composed = Scene::ComposeMeshMaterial(model, &own, 0);
    CHECK(composed.BlendMode == MaterialBlendMode::Cutout && composed.Shader == MaterialShader::Lit && !composed.DoubleSided);
    CHECK_NEAR(composed.AlphaCutoff, 0.2f, 1e-6f);
    model.Mat.BlendMode = MaterialBlendMode::Transparent;
    model.Mat.PreserveSpecular = false;
    model.Mat.Shader = MaterialShader::Unlit;
    model.Mat.DoubleSided = true;
    composed = Scene::ComposeMeshMaterial(model, &own, 0);
    CHECK(composed.BlendMode == MaterialBlendMode::Transparent && !composed.PreserveSpecular);
    CHECK(composed.Shader == MaterialShader::Unlit && composed.DoubleSided);
    own.DoubleSided = true; // the model's double-sided parts stay double-sided
    model.Mat = Material{};
    CHECK(Scene::ComposeMeshMaterial(model, &own, 0).DoubleSided);
}

// A script that tints its own copy of the material (task 095).
class MaterialTintScript : public ScriptableEntity
{
public:
    Ref<MaterialAsset> Mine, MineAgain, Shared, OfOther, OfNothing;
    void OnStart() override
    {
        Mine = GetMaterial();
        Mine->SetColor(glm::vec3(1.0f, 0.0f, 0.0f));
        MineAgain = GetMaterial();
        Shared = GetSharedMaterial();
        OfOther = GetMaterial(FindEntity("Other"));
        OfNothing = GetMaterial(FindEntity("Empty"));
    }
};
ZM_REGISTER_SCRIPT(MaterialTintScript)

TEST(MaterialInstancesCopyOnWrite)
{
    namespace fs = std::filesystem;
    Project::SetActive(nullptr);
    FakeTextureLoader loader;
    fs::path dir = MakeTempDir("zamin-material-instances-");
    const std::string goldFile = (dir / "Gold.zmat").string(), redFile = (dir / "Red.zmat").string();
    MaterialData gold;
    gold.Albedo = { 1.0f, 0.8f, 0.3f, 1.0f };
    gold.Metallic = 1.0f;
    gold.AlbedoMap = (dir / "gold.png").string();
    MaterialData red;
    red.Albedo = { 1.0f, 0.0f, 0.0f, 1.0f };
    CHECK(MaterialSerializer::Serialize(gold, goldFile) && MaterialSerializer::Serialize(red, redFile));

    auto scene = CreateRef<Scene>();
    Entity a = scene->CreateEntity("A"), b = scene->CreateEntity("B"), plain = scene->CreateEntity("Plain"), statue = scene->CreateEntity("Statue");
    a.AddComponent<MeshRendererComponent>();
    b.AddComponent<MeshRendererComponent>();
    plain.AddComponent<MeshRendererComponent>();
    statue.AddComponent<MeshRendererComponent>().Type = PrimitiveMesh::Model;
    auto& ma = a.GetComponent<MeshRendererComponent>();
    auto& mb = b.GetComponent<MeshRendererComponent>();
    // Setting a material asset: only .zmat files; the shared library object.
    CHECK(!Scene::SetMeshMaterial(ma, "Textures/wood.png"));
    CHECK(Scene::SetMeshMaterial(ma, goldFile) && Scene::SetMeshMaterial(mb, goldFile));
    Ref<MaterialAsset> shared = MaterialLibrary::Find(goldFile);
    CHECK(shared && ma.MaterialRef == shared && mb.MaterialRef == shared && !shared->IsInstance());
    std::string savedText = SceneSerializer(scene).SerializeToString();

    // The first GetMaterialInstance makes the copy, later ones return it.
    CHECK(!Scene::HasMaterialInstances(ma));
    Ref<MaterialAsset> instance = Scene::GetMaterialInstance(ma);
    CHECK(instance && instance != shared && instance->IsInstance() && Scene::GetMaterialInstance(ma) == instance);
    CHECK(Scene::HasMaterialInstances(ma) && !Scene::HasMaterialInstances(mb));
    CHECK(instance->GetData() == shared->GetData() && instance->GetMaterial().AlbedoMap == shared->GetMaterial().AlbedoMap);
    CHECK(instance->GetPath() == shared->GetPath() && MaterialLibrary::Find(goldFile) == shared); // not cached
    // Its edits change this renderer only: not the asset, not the other renderer, not the file.
    instance->SetColor(glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(instance->GetColor() == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) && shared->GetColor() == gold.Albedo);
    CHECK(Scene::ComposeMeshMaterial(ma, nullptr).Albedo == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    CHECK(Scene::ComposeMeshMaterial(mb, nullptr).Albedo == gold.Albedo);
    CHECK(Scene::GetMeshMaterialAsset(ma) == shared); // the shared one
    CHECK(!instance->Save());
    std::string error;
    CHECK(!instance->Reload(&error) && !error.empty());
    MaterialData onDisk;
    CHECK(MaterialSerializer::Deserialize(goldFile, onDisk) && onDisk == gold);
    // Quick edits clamp what has a range; a new texture path is looked up.
    int calls = loader.Calls;
    instance->SetMetallic(2.0f);
    instance->SetRoughness(-1.0f);
    instance->SetOpacity(0.25f);
    instance->SetEmission({ 1.0f, 0.5f, 0.0f }, -3.0f);
    instance->SetTiling({ 2.0f, 3.0f });
    instance->SetOffset({ 0.5f, 0.0f });
    instance->SetAlbedoMap("Textures/blue.png");
    const Material& im = instance->GetMaterial();
    CHECK(im.Metallic == 1.0f && im.Roughness == 0.0f && im.Albedo.a == 0.25f && im.EmissionStrength == 0.0f);
    CHECK(im.Emission == glm::vec3(1.0f, 0.5f, 0.0f) && im.Tiling == glm::vec2(2.0f, 3.0f) && im.Offset == glm::vec2(0.5f, 0.0f));
    CHECK(loader.Calls == calls + 1 && im.AlbedoMap && im.AlbedoMap->GetPath() == "Textures/blue.png");
    CHECK(shared->GetData() == gold);
    // Runtime only: the scene saves what it saved before.
    CHECK(SceneSerializer(scene).SerializeToString() == savedText);

    // The inline material: an instance of it, its textures as they are (nothing loaded again).
    auto& mp = plain.GetComponent<MeshRendererComponent>();
    mp.Mat.Albedo = { 0.0f, 1.0f, 0.0f, 1.0f };
    mp.AlbedoPath = "Textures/wood.png";
    Scene::ResolveMeshMaterials(mp);
    calls = loader.Calls;
    Ref<MaterialAsset> inlineInstance = Scene::GetMaterialInstance(mp);
    CHECK(inlineInstance && inlineInstance->IsInstance() && loader.Calls == calls);
    CHECK(inlineInstance->GetData().AlbedoMap == "Textures/wood.png" && inlineInstance->GetMaterial().AlbedoMap == mp.Mat.AlbedoMap);
    inlineInstance->SetColor(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
    CHECK(Scene::ComposeMeshMaterial(mp, nullptr).Albedo == glm::vec4(0.2f, 0.2f, 0.2f, 1.0f) && mp.Mat.Albedo == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    CHECK(inlineInstance->GetMaterial().AlbedoMap == mp.Mat.AlbedoMap); // a value edit keeps the texture

    // Models: the main instance is the tint of every part; a part's instance is its whole material.
    auto& ms = statue.GetComponent<MeshRendererComponent>();
    CHECK(Scene::SetMeshMaterial(ms, redFile, 1) && ms.SubmeshMaterials == std::vector<std::string>({ "", redFile }));
    Ref<MaterialAsset> redShared = MaterialLibrary::Find(redFile);
    Material modelMaterial;
    modelMaterial.Albedo = { 0.5f, 0.5f, 0.5f, 1.0f };
    Scene::GetMaterialInstance(ms)->SetColor(glm::vec3(1.0f, 0.0f, 0.0f));
    CHECK(Scene::ComposeMeshMaterial(ms, &modelMaterial, 0).Albedo == glm::vec4(0.5f, 0.0f, 0.0f, 1.0f));
    CHECK(Scene::ComposeMeshMaterial(ms, &modelMaterial, 1).Albedo == red.Albedo); // the slot's asset
    Ref<MaterialAsset> part1 = Scene::GetMaterialInstance(ms, 1);
    CHECK(part1 && part1 != redShared && part1->GetData() == redShared->GetData());
    part1->SetColor(glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(Scene::ComposeMeshMaterial(ms, &modelMaterial, 1).Albedo == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) && redShared->GetColor() == red.Albedo);
    // A part without a slot: a copy of what it shows (the model's material with the tint; no model
    // is loaded here, so the tint alone).
    Ref<MaterialAsset> part0 = Scene::GetMaterialInstance(ms, 0);
    CHECK(part0 && part0 != ms.MaterialInstance && part0->GetColor() == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    part0->SetMetallic(0.7f);
    CHECK(Scene::ComposeMeshMaterial(ms, &modelMaterial, 0).Metallic == 0.7f);
    // Setting a material drops the copies of the old one; Clear goes back to the shared ones.
    CHECK(Scene::SetMeshMaterial(ms, goldFile) && !Scene::HasMaterialInstances(ms));
    CHECK(Scene::ComposeMeshMaterial(ms, &modelMaterial, 0).Albedo == gold.Albedo && Scene::ComposeMeshMaterial(ms, &modelMaterial, 1).Albedo == red.Albedo);
    CHECK(Scene::SetMeshMaterial(ms, "", 1) && ms.SubmeshMaterials.empty());
    Scene::ClearMaterialInstances(mp);
    CHECK(Scene::ComposeMeshMaterial(mp, nullptr).Albedo == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));

    // Play starts on a copy of the scene: no instances there. A duplicate (Instantiate) gets its own.
    Ref<Scene> copy = Scene::Copy(scene);
    if (Entity c = copy->FindEntityByName("A"))
        CHECK(!Scene::HasMaterialInstances(c.GetComponent<MeshRendererComponent>()));
    Entity dup = scene->DuplicateEntity(a);
    auto& md = dup.GetComponent<MeshRendererComponent>();
    Ref<MaterialAsset> original = Scene::GetMaterialInstance(a.GetComponent<MeshRendererComponent>());
    CHECK(md.MaterialInstance && md.MaterialInstance != original && md.MaterialInstance->GetColor() == original->GetColor());
    md.MaterialInstance->SetColor(glm::vec3(1.0f));
    CHECK(original->GetColor() == glm::vec4(0.0f, 0.0f, 1.0f, 0.25f));

    // Through the script API in a running game.
    {
        auto game = CreateRef<Scene>();
        Entity hero = game->CreateEntity("Hero"), other = game->CreateEntity("Other");
        game->CreateEntity("Empty");
        hero.AddComponent<MeshRendererComponent>().MaterialPath = goldFile;
        other.AddComponent<MeshRendererComponent>().MaterialPath = goldFile;
        hero.AddComponent<NativeScriptComponent>().ClassName = "MaterialTintScript";
        Scene::ResolveMeshMaterials(hero.GetComponent<MeshRendererComponent>()); // no GPU mesh
        Scene::ResolveMeshMaterials(other.GetComponent<MeshRendererComponent>());
        game->OnRuntimeStart();
        game->OnUpdateRuntime(0.016f);
        auto* script = ScriptableEntity::GetScript<MaterialTintScript>(hero);
        CHECK(script != nullptr);
        if (script)
        {
            CHECK(script->Mine && script->Mine == script->MineAgain && script->Mine->IsInstance() && script->Shared == shared);
            CHECK(script->OfOther && script->OfOther != script->Mine && !script->OfNothing);
            CHECK(Scene::ComposeMeshMaterial(hero.GetComponent<MeshRendererComponent>(), nullptr).Albedo == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
            CHECK(Scene::ComposeMeshMaterial(other.GetComponent<MeshRendererComponent>(), nullptr).Albedo == gold.Albedo);
            CHECK(shared->GetColor() == gold.Albedo);
            // Another shared material: the copies go.
            CHECK(script->SetSharedMaterial(redFile) && !Scene::HasMaterialInstances(hero.GetComponent<MeshRendererComponent>()));
            CHECK(script->GetSharedMaterial() == redShared && !script->SetSharedMaterial("Textures/x.png"));
            script->GetMaterial()->SetColor(glm::vec3(0.0f));
            script->ResetMaterial();
            CHECK(Scene::ComposeMeshMaterial(hero.GetComponent<MeshRendererComponent>(), nullptr).Albedo == red.Albedo);
            script->Mine = script->MineAgain = script->Shared = script->OfOther = nullptr;
        }
        game->OnRuntimeStop();
    }

    // Shared materials changed while playing are taken back when play stops (the editor's snapshot).
    MaterialLibrary::Snapshot snapshot = MaterialLibrary::TakeSnapshot();
    CHECK(snapshot.Materials.size() == 2);
    shared->SetColor(glm::vec3(0.1f, 0.2f, 0.3f));
    std::vector<std::string> restored = MaterialLibrary::RestoreSnapshot(snapshot);
    CHECK(restored.size() == 1 && shared->GetData() == gold);
    CHECK(MaterialLibrary::RestoreSnapshot(snapshot).empty()); // nothing changed since
    // Saved meanwhile: the file is what counts, the edit stays.
    shared->SetMetallic(0.3f);
    CHECK(shared->Save() && MaterialLibrary::RestoreSnapshot(snapshot).empty() && shared->GetData().Metallic == 0.3f);
    // Loaded while playing and changed: read again from its file.
    MaterialData blue;
    blue.Albedo = { 0.0f, 0.0f, 1.0f, 1.0f };
    CHECK(MaterialSerializer::Serialize(blue, dir / "Blue.zmat"));
    Ref<MaterialAsset> blueShared = MaterialLibrary::Load(dir / "Blue.zmat");
    CHECK(blueShared);
    if (blueShared)
    {
        blueShared->SetColor(glm::vec3(1.0f));
        restored = MaterialLibrary::RestoreSnapshot(snapshot);
        CHECK(restored.size() == 1 && blueShared->GetData() == blue);
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// A binary glTF: the JSON chunk and one BIN chunk (images in buffer views).
static std::string MakeGLB(std::string json, std::string bin)
{
    while (json.size() % 4) json += ' ';
    while (bin.size() % 4) bin += '\0';
    auto u32 = [](uint32_t v) { return std::string((const char*)&v, 4); };
    std::string glb = u32(0x46546C67) + u32(2) + u32((uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
    glb += u32((uint32_t)json.size()) + u32(0x4E4F534A) + json;
    glb += u32((uint32_t)bin.size()) + u32(0x004E4942) + bin;
    return glb;
}

TEST(GLTFMaterialExport)
{
    namespace fs = std::filesystem;
    Project::SetActive(nullptr);
    FakeTextureLoader loader;
    fs::path dir = MakeTempDir("zamin-gltf-export-");
    const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + "fake image bytes";
    WriteFile(dir / "tex a.png", "normal map");
    WriteFile(dir / "robot.gltf", R"({ "asset": { "version": "2.0" },
        "images": [ { "uri": "data:image/png;base64,)" + Base64(png) + R"(" }, { "uri": "tex%20a.png" } ],
        "textures": [ { "source": 0 }, { "source": 1 } ],
        "materials": [
          { "name": "Metal/Shiny:1", "emissiveFactor": [1, 0.5, 0], "normalTexture": { "index": 1, "scale": 0.5 },
            "pbrMetallicRoughness": { "baseColorFactor": [0.5, 0.25, 1.0, 1.0], "metallicFactor": 1.0, "roughnessFactor": 0.2,
                                      "baseColorTexture": { "index": 0 } } },
          { "name": "Glass", "alphaMode": "BLEND", "doubleSided": true, "pbrMetallicRoughness": { "baseColorFactor": [1, 1, 1, 0.3] } },
          { "name": "Glass", "alphaMode": "MASK", "alphaCutoff": 0.3, "normalTexture": { "index": 1 } },
          { }
        ] })");

    ModelMaterialExport result;
    std::string error;
    CHECK(Model::ExportMaterials(dir / "robot.gltf", {}, result, &error));
    fs::path out = dir / "robot_Materials";
    CHECK(result.OutputDirectory == out && Model::GetDefaultMaterialExportDirectory(dir / "robot.gltf") == out);
    CHECK(result.Materials == std::vector<fs::path>({ out / "Metal_Shiny_1.zmat", out / "Glass.zmat", out / "Glass_1.zmat", out / "robot_Material3.zmat" }));
    CHECK(result.Written.size() == 5 && result.Kept.empty() && result.Warnings.empty());
    // The embedded image comes out byte for byte; the file next to the model is used where it is.
    fs::path extracted = out / "Metal_Shiny_1_BaseColor.png";
    std::vector<uint8_t> bytes = FileSystem::ReadBinaryFile(extracted);
    CHECK(std::string(bytes.begin(), bytes.end()) == png);
    MaterialData metal, glass, cutout, plain;
    CHECK(MaterialSerializer::Deserialize(out / "Metal_Shiny_1.zmat", metal));
    CHECK(MaterialSerializer::Deserialize(out / "Glass.zmat", glass));
    CHECK(MaterialSerializer::Deserialize(out / "Glass_1.zmat", cutout));
    CHECK(MaterialSerializer::Deserialize(out / "robot_Material3.zmat", plain));
    CHECK(metal.AlbedoMap == extracted.generic_string() && metal.NormalMap == (dir / "tex a.png").generic_string());
    CHECK_NEAR(metal.Albedo.r, std::pow(0.5f, 1.0f / 2.2f), 1e-5f);
    CHECK(metal.Metallic == 1.0f && metal.Emission == glm::vec3(1.0f, 0.5f, 0.0f) && metal.EmissionStrength == 1.0f);
    CHECK_NEAR(metal.Roughness, 0.2f, 1e-6f);
    CHECK_NEAR(metal.NormalStrength, 0.5f, 1e-6f);
    CHECK(glass.BlendMode == MaterialBlendMode::Transparent && glass.DoubleSided && !glass.PreserveSpecular && glass.AlbedoMap.empty());
    CHECK_NEAR(glass.Albedo.a, 0.3f, 1e-6f);
    CHECK(cutout.BlendMode == MaterialBlendMode::Cutout && cutout.NormalMap == metal.NormalMap);
    CHECK_NEAR(cutout.AlphaCutoff, 0.3f, 1e-6f);
    CHECK(plain == MaterialData{});
    // The exported material loads with its texture.
    Ref<MaterialAsset> loaded = MaterialLibrary::Load(out / "Metal_Shiny_1.zmat");
    CHECK(loaded && loaded->GetMaterial().AlbedoMap && loaded->GetMaterial().NormalMap);

    // Again: what is there is kept (and still listed); with Overwrite it is written again, and a
    // material in use shows the new content.
    MaterialData edited = metal;
    edited.Roughness = 0.9f;
    CHECK(MaterialSerializer::Serialize(edited, out / "Metal_Shiny_1.zmat"));
    CHECK(Model::ExportMaterials(dir / "robot.gltf", {}, result, &error));
    CHECK(result.Written.empty() && result.Kept.size() == 5 && result.Materials.size() == 4);
    MaterialData kept;
    CHECK(MaterialSerializer::Deserialize(out / "Metal_Shiny_1.zmat", kept) && kept == edited); // not written over
    ModelMaterialExportOptions overwrite;
    overwrite.Overwrite = true;
    if (loaded)
        loaded->SetData(edited);
    CHECK(Model::ExportMaterials(dir / "robot.gltf", overwrite, result, &error) && result.Written.size() == 5 && result.Kept.empty());
    MaterialData rewritten;
    CHECK(MaterialSerializer::Deserialize(out / "Metal_Shiny_1.zmat", rewritten) && rewritten == metal);
    CHECK(!loaded || loaded->GetData() == metal);
    // Another folder.
    ModelMaterialExportOptions elsewhere;
    elsewhere.OutputDirectory = dir / "Mats";
    CHECK(Model::ExportMaterials(dir / "robot.gltf", elsewhere, result, &error) && fs::exists(dir / "Mats" / "Glass.zmat"));

    // A missing image is reported, the material is still written.
    WriteFile(dir / "broken.gltf", R"({ "asset": { "version": "2.0" }, "images": [ { "uri": "nope.png" } ], "textures": [ { "source": 0 } ],
        "materials": [ { "name": "Skin", "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } } ] })");
    CHECK(Model::ExportMaterials(dir / "broken.gltf", {}, result, &error) && result.Warnings.size() == 1 && result.Materials.size() == 1);
    // Nothing to export / not glTF / unreadable.
    WriteFile(dir / "empty.gltf", R"({ "asset": { "version": "2.0" } })");
    CHECK(!Model::ExportMaterials(dir / "empty.gltf", {}, result, &error) && !error.empty());
    WriteFile(dir / "cube.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    CHECK(!Model::ExportMaterials(dir / "cube.obj", {}, result, &error));
    CHECK(!Model::ExportMaterials(dir / "none.glb", {}, result, &error));

    // A .glb in a project: images from the binary chunk, paths relative to Assets with UUIDs.
    fs::path projectDir = dir / "Game";
    CHECK(Project::CreateFromTemplate(projectDir, "ExportGame") != nullptr);
    bool writes = AssetRegistry::GetWriteMetaFilesByDefault();
    AssetRegistry::SetWriteMetaFilesByDefault(true);
    CHECK(Project::Load(projectDir / "ExportGame.zproject") != nullptr);
    fs::path assets = Project::GetAssetDirectory();
    std::string jpeg = std::string("\xFF\xD8\xFF\xE0", 4) + "jpeg data";
    std::string glbJson = std::format(R"({{ "asset": {{ "version": "2.0" }}, "buffers": [ {{ "byteLength": {} }} ],
        "bufferViews": [ {{ "buffer": 0, "byteOffset": 0, "byteLength": {} }} ],
        "images": [ {{ "bufferView": 0, "mimeType": "image/jpeg", "name": "Skin Color" }} ], "textures": [ {{ "source": 0 }} ],
        "materials": [ {{ "name": "Body", "pbrMetallicRoughness": {{ "baseColorTexture": {{ "index": 0 }}, "metallicFactor": 0.25 }} }} ] }})",
        jpeg.size(), jpeg.size());
    std::string glb = MakeGLB(glbJson, jpeg);
    fs::create_directories(assets / "Models");
    CHECK(FileSystem::WriteBinaryFile(assets / "Models" / "robot.glb", glb.data(), glb.size()));
    CHECK(Model::ExportMaterials(assets / "Models" / "robot.glb", {}, result, &error));
    CHECK(result.Materials.size() == 1 && result.Written.size() == 2 && result.Warnings.empty());
    fs::path body = assets / "Models" / "robot_Materials" / "Body.zmat";
    std::vector<uint8_t> jpegBack = FileSystem::ReadBinaryFile(assets / "Models" / "robot_Materials" / "Skin Color.jpg");
    CHECK(std::string(jpegBack.begin(), jpegBack.end()) == jpeg);
    MaterialData bodyData;
    CHECK(MaterialSerializer::Deserialize(body, bodyData) && bodyData.AlbedoMap == "Models/robot_Materials/Skin Color.jpg");
    CHECK(bodyData.Metallic == 0.25f);
    AssetRegistry* registry = Project::GetAssetRegistry();
    CHECK(registry && registry->GetHandle("Models/robot_Materials/Body.zmat") && registry->GetHandle("Models/robot_Materials/Skin Color.jpg"));
    if (registry)
        CHECK(FileSystem::ReadTextFile(body).find("\"AlbedoMapUUID\": \"" + registry->GetHandle("Models/robot_Materials/Skin Color.jpg").ToString()) !=
              std::string::npos);

    AssetRegistry::SetWriteMetaFilesByDefault(writes);
    Project::SetActive(nullptr);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

int main()
{
    Log::SetMinLevel(LogLevel::Error);
    int index = 0;
    for (auto& t : Tests())
    {
        int before = s_Failures;
        t.Fn();
        std::printf("[%2d] %-32s %s\n", ++index, t.Name, s_Failures == before ? "OK" : "FAILED");
    }
    std::printf("\n%zu test, %d xato\n", Tests().size(), s_Failures);
    return s_Failures == 0 ? 0 : 1;
}
