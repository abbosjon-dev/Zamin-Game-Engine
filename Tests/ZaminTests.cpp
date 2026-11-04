// Headless unit tests (no window / GPU needed).
#include <Zamin/Zamin.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
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
