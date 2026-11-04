// Headless unit tests (no window / GPU needed).
#include <Zamin/Zamin.h>

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
