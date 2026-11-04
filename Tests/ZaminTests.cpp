// Headless unit tests (no window / GPU needed).
#include <Zamin/Zamin.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
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
