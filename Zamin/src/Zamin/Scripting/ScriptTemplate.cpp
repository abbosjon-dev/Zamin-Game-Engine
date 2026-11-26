#include "Zamin/Scripting/ScriptTemplate.h"

#include <algorithm>
#include <iterator>
#include <cctype>
#include <format>

namespace Zamin {

    namespace {

        // Placeholder replaced by the class name.
        constexpr std::string_view ClassToken = "$CLASS$";

        // Every callback of ScriptableEntity, each with a short example, plus one field of
        // each inspector kind (range, enum, color, tooltip, header, entity reference).
        constexpr std::string_view FullTemplate = R"ZM(// $CLASS$ - a C++ gameplay script.
// To use it: add the "C++ Script" component to an entity and choose $CLASS$ in the "Class" list.
// After a change: Scripts > Compile + reload (Ctrl+B) - the editor reloads the module by itself.
#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

class $CLASS$ : public ScriptableEntity
{
public:
    // ================================================================ inspector fields
    // The fields listed in ZM_REGISTER_SCRIPT below are edited in the inspector.
    enum class MoveMode { Idle, Patrol, Follow };

    float Speed = 3.0f;                            // ZM_FIELD_RANGE: a 0..10 slider
    MoveMode Mode = MoveMode::Idle;                // ZM_FIELD_ENUM: a drop-down list
    glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
    int Health = 100;
    bool ShowDebug = false;
    glm::vec4 Tint = { 1.0f, 1.0f, 1.0f, 1.0f };   // ZM_FIELD_COLOR: a colour picker
    std::string Greeting = "Hello";
    EntityRef Target;                              // another entity: drag it here from the hierarchy

    // ================================================================ life cycle
    // Order: OnCreate -> OnEnable -> OnStart -> (OnUpdate -> OnLateUpdate) ... -> OnDisable -> OnDestroy

    // Once, when the script is created (when the game starts or the entity appears).
    // Other scripts may not exist yet - look for them in OnStart.
    void OnCreate() override
    {
        ZM_INFO("$CLASS$ created: {} ({})", GetName(), Greeting);
    }

    // When the script is enabled: after OnCreate and on every SetEnabled(true).
    // A good place to subscribe to events (cancelled in OnDisable).
    void OnEnable() override
    {
        m_GameOverSubscription = Subscribe("GameOver", [this](const ScriptMessage&) { SetEnabled(false); });
    }

    // Once, before the first OnUpdate. By now every script in the scene has been through
    // OnCreate/OnEnable: the best place to find other entities.
    void OnStart() override
    {
        Transform().Translation += Offset;
        m_StartPosition = Transform().Translation;
        if (auto* sprite = TryGetComponent<SpriteRendererComponent>())
            sprite->Color = Tint;
        if (Entity target = Resolve(Target))
            ZM_INFO("$CLASS$: target - {}", target.GetName());
    }

    // Every frame, before physics. Input names: File > Project settings > Input.
    void OnUpdate(Timestep ts) override
    {
        switch (Mode)
        {
            case MoveMode::Idle:
            {
                glm::vec2 move = Input::GetAxis2D("Horizontal", "Vertical");
                Transform().Translation += glm::vec3(move.x, move.y, 0.0f) * Speed * (float)ts;
                break;
            }
            case MoveMode::Patrol:
                Transform().Translation.x = m_StartPosition.x + std::sin(GetTime() * Speed) * 2.0f;
                break;
            case MoveMode::Follow:
                if (Entity target = Resolve(Target))
                {
                    glm::vec3 to = target.GetComponent<TransformComponent>().Translation - Transform().Translation;
                    if (glm::length(to) > 0.1f)
                        Transform().Translation += glm::normalize(to) * Speed * (float)ts;
                }
                break;
        }

        if (Input::IsActionJustPressed("Jump"))
            ZM_INFO("$CLASS$: jump!");
        // Debug drawing (in the editor and Development builds; the calls are empty in Shipping):
        // DebugDraw::Arrow(GetWorldPosition(), GetWorldPosition() + glm::vec3(0, 2, 0), DebugColor::Green);
        // DebugDraw::Text(GetWorldPosition() + glm::vec3(0, 2.5f, 0), GetName());
    }

    // Every frame, after physics (for a camera that follows, for example).
    void OnLateUpdate(Timestep /*ts*/) override
    {
    }

    // When the script is disabled (SetEnabled(false)) and before OnDestroy.
    void OnDisable() override
    {
        Unsubscribe(m_GameOverSubscription);
        m_GameOverSubscription = 0;
    }

    // When the entity is destroyed or the game stops.
    void OnDestroy() override
    {
        // A value kept between runs of the game: SaveData::SetInt("BestHealth", Health);
    }

    // ================================================================ physics (2D and 3D)
    // A solid collision began / ended (neither collider is a sensor).
    void OnCollisionBegin(Entity other) override
    {
        if (ShowDebug)
            ZM_INFO("$CLASS$ hit: {}", other.GetName());
    }

    void OnCollisionEnd(Entity /*other*/) override
    {
    }

    // Something entered / left a sensor area (the collider's "Sensor" flag).
    void OnTriggerBegin(Entity other) override
    {
        if (ShowDebug && other.HasTag("Player"))
            ZM_INFO("$CLASS$: the player entered the area");
        // A pickup, for example: PlaySound("Audio/pickup.wav"); Destroy();
    }

    void OnTriggerEnd(Entity /*other*/) override
    {
    }

    // ================================================================ UI and messages
    // The UI button (UI Button) on this entity was clicked.
    void OnClick() override
    {
        ZM_INFO("$CLASS$: the button was clicked");
    }

    // Messages sent with SendMessage(entity, "Damage", 10) or Broadcast(...).
    void OnMessage(const ScriptMessage& message) override
    {
        if (message.Name == "Damage")
        {
            Health -= message.Get<int>();
            if (Health <= 0)
                Destroy(); // destroyed at the end of the frame
        }
    }

    // ================================================================ scenes
    // A scene was loaded (the game's first scene too; Additive too). LoadScene(1) - by build index,
    // LoadNextScene() - the next one in the build list, ReloadScene() - this level again.
    void OnSceneLoaded(const SceneEventInfo& scene) override
    {
        if (ShowDebug)
            ZM_INFO("$CLASS$: scene '{}' loaded (build index {})", scene.Name, scene.BuildIndex);
    }

private:
    glm::vec3 m_StartPosition{ 0.0f };
    SubscriptionID m_GameOverSubscription = 0;
};

// The fields the inspector shows. Attributes can also be chained:
//   ZM_FIELD($CLASS$, Speed).Range(0, 10).Tooltip("...").Header("...").Label("...")
ZM_REGISTER_SCRIPT($CLASS$,
    ZM_FIELD_RANGE($CLASS$, Speed, 0.0f, 10.0f).Header("Movement").Tooltip("Movement speed (units per second)"),
    ZM_FIELD_ENUM($CLASS$, Mode, "Control (Input)", "Patrol", "Follow the target").Label("Mode"),
    ZM_FIELD_TOOLTIP($CLASS$, Offset, "The offset added to the position in OnStart"),
    ZM_FIELD_RANGE($CLASS$, Health, 0, 100).Header("State"),
    ZM_FIELD_TOOLTIP($CLASS$, ShowDebug, "Log collisions and triggers to the console"),
    ZM_FIELD_COLOR($CLASS$, Tint).Header("Appearance").Tooltip("The sprite's colour (applied in OnStart)"),
    ZM_FIELD($CLASS$, Greeting).Multiline(),
    ZM_FIELD($CLASS$, Target).Header("References").Tooltip("Drag an entity here from the hierarchy"))
)ZM";

        constexpr std::string_view MinimalTemplate = R"ZM(// $CLASS$ - a C++ gameplay script.
// To use it: add the "C++ Script" component to an entity and choose $CLASS$ in the "Class" list.
#include <Zamin/Zamin.h>

using namespace Zamin;

class $CLASS$ : public ScriptableEntity
{
public:
    float RotationSpeed = 0.0f; // radians per second (change it in the inspector)

    void OnCreate() override
    {
        ZM_INFO("$CLASS$ created: {}", GetName());
    }

    void OnUpdate(Timestep ts) override
    {
        Transform().Rotation.y += RotationSpeed * (float)ts;
    }
};

ZM_REGISTER_SCRIPT($CLASS$, ZM_FIELD_TOOLTIP($CLASS$, RotationSpeed, "Rotation speed around the Y axis"))
)ZM";

        constexpr std::string_view ProjectStarter = R"ZM(// The game's scripts (C++). Every class is registered with ZM_REGISTER_SCRIPT
// and chosen in the editor's "C++ Script" component. A new script: Scripts > New C++ script.
#include <Zamin/Zamin.h>

using namespace Zamin;

// Rotates the entity all the time.
class Rotator : public ScriptableEntity
{
public:
    glm::vec3 Speed = { 0.0f, 1.0f, 0.0f }; // radians per second

    void OnUpdate(Timestep ts) override
    {
        Transform().Rotation += Speed * (float)ts;
    }
};
ZM_REGISTER_SCRIPT(Rotator, ZM_FIELD_TOOLTIP(Rotator, Speed, "Rotation speed around each axis (radians per second)"))

// An entity moved with WASD / the arrows / a gamepad. The names "Horizontal", "Vertical" and
// "Jump" are bound to keys in Project settings > Input.
class PlayerController : public ScriptableEntity
{
public:
    float MoveSpeed = 5.0f;

    void OnStart() override
    {
        // SaveData: values kept between runs of the game (~/.local/share/zamin/<project>)
        int launches = SaveData::GetInt("Launches") + 1;
        SaveData::SetInt("Launches", launches);
        ZM_INFO("PlayerController started: {}, launch {}", GetName(), launches);
    }

    void OnUpdate(Timestep ts) override
    {
        glm::vec2 move = Input::GetAxis2D("Horizontal", "Vertical");
        Transform().Translation += glm::vec3(move.x, 0.0f, -move.y) * MoveSpeed * (float)ts;
        if (Input::IsActionJustPressed("Jump"))
            ZM_INFO("Jump!");
    }

    // Something entered the sensor collider (a pickup, for example).
    void OnTriggerBegin(Entity other) override
    {
        ZM_INFO("Entered the area of {}", other.GetName());
    }
};
ZM_REGISTER_SCRIPT(PlayerController,
    ZM_FIELD_RANGE(PlayerController, MoveSpeed, 0.0f, 20.0f).Tooltip("Movement speed (units per second)"))
)ZM";

        constexpr std::string_view CppKeywords[] = {
            "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
            "case", "catch", "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept",
            "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await",
            "co_return", "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast",
            "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
            "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
            "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register",
            "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
            "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
            "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
            "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq", "final", "override",
            "glm", "std", "Zamin",
        };

        // Everything declared in namespace Zamin (generated from the engine headers at build time):
        // with `using namespace Zamin;` a global class of the same name is ambiguous.
        constexpr std::string_view EngineNames[] = {
#include "ZaminEngineNames.inc"
            "Zamin",
        };

        // ScriptableEntity members the templates call: inside the class the class name would hide
        // them (`Transform()` would construct the script, `Destroy()` would do nothing), plus the
        // full template's nested type (data members may share the class name).
        constexpr std::string_view TemplateNames[] = {
            "Transform", "Resolve", "Subscribe", "Unsubscribe", "SetEnabled", "IsEnabled", "GetName",
            "GetTime", "Destroy", "TryGetComponent", "GetComponent", "HasComponent", "AddComponent",
            "Instantiate", "FindEntity", "SendMessage", "Broadcast", "Emit", "Invoke", "Tween",
            "PlaySound", "LoadScene", "GetScript", "OnCreate", "OnEnable", "OnStart", "OnUpdate",
            "OnLateUpdate", "OnDisable", "OnDestroy", "OnCollisionBegin", "OnCollisionEnd",
            "OnTriggerBegin", "OnTriggerEnd", "OnClick", "OnMessage", "MoveMode",
        };

        template<size_t N>
        bool Contains(const std::string_view (&list)[N], std::string_view name)
        {
            return std::find(std::begin(list), std::end(list), name) != std::end(list);
        }


    }

    namespace ScriptTemplate {

        std::string SanitizeClassName(std::string_view name)
        {
            std::string out;
            for (char ch : name)
                if (std::isalnum((unsigned char)ch) || ch == '_')
                    out += ch;
            if (!out.empty() && std::isdigit((unsigned char)out[0]))
                out.insert(out.begin(), '_');
            return out;
        }

        std::string ValidateClassName(std::string_view name)
        {
            if (name.empty())
                return "The class name is empty";
            if (std::isdigit((unsigned char)name[0]))
                return "The class name must not start with a digit";
            for (char ch : name)
                if (!(std::isalnum((unsigned char)ch) || ch == '_'))
                    return std::format("Character not allowed: '{}' (only Latin letters, digits and _)", ch);
            if (name.size() >= 2 && name[0] == '_' && (name[1] == '_' || std::isupper((unsigned char)name[1])))
                return "Names starting with '_' + a capital letter or '__' are reserved in C++";
            if (name.find("__") != std::string_view::npos)
                return "'__' (two underscores) is reserved in C++";
            if (Contains(CppKeywords, name))
                return std::format("'{}' is a C++ keyword or a reserved name", name);
            if (Contains(EngineNames, name))
                return std::format("'{0}' clashes with a name in the Zamin engine (using namespace Zamin). Name it '{0}Controller' or '{0}Script', for example", name);
            if (Contains(TemplateNames, name))
                return std::format("'{0}' is the same as a script function name. Name it '{0}Script', for example", name);
            return {};
        }

        bool IsValidClassName(std::string_view name) { return ValidateClassName(name).empty(); }

        std::string Generate(std::string_view className, ScriptTemplateKind kind)
        {
            std::string text(kind == ScriptTemplateKind::Minimal ? MinimalTemplate : FullTemplate);
            for (size_t pos = text.find(ClassToken); pos != std::string::npos; pos = text.find(ClassToken, pos + className.size()))
                text.replace(pos, ClassToken.size(), className);
            return text;
        }

        std::string GenerateProjectStarter() { return std::string(ProjectStarter); }

        const char* GetKindName(ScriptTemplateKind kind)
        {
            switch (kind)
            {
                case ScriptTemplateKind::Full:    return "To'liq";
                case ScriptTemplateKind::Minimal: return "Minimal";
            }
            return "";
        }

        const char* GetKindDescription(ScriptTemplateKind kind)
        {
            switch (kind)
            {
                case ScriptTemplateKind::Full:
                    return "Hamma callback'lar (OnCreate, OnEnable, OnStart, OnUpdate, OnTriggerBegin ...) misollar bilan, "
                           "har xil inspektor maydonlari (slayder, ro'yxat, rang, havola)";
                case ScriptTemplateKind::Minimal:
                    return "Faqat OnCreate va OnUpdate, bitta maydon";
            }
            return "";
        }

    }

}
