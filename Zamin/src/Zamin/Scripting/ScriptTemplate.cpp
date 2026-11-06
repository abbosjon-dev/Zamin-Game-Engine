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
        constexpr std::string_view FullTemplate = R"ZM(// $CLASS$ - C++ gameplay skripti.
// Ishlatish: obyektga "C++ Skript" komponentini qo'shing va "Klass" ro'yxatidan $CLASS$ ni tanlang.
// O'zgartirgach: Skriptlar > Kompilyatsiya (Ctrl+B) - editor modulni o'zi qayta yuklaydi.
#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

class $CLASS$ : public ScriptableEntity
{
public:
    // ================================================================ inspektor maydonlari
    // Pastdagi ZM_REGISTER_SCRIPT ro'yxatidagi maydonlar inspektorda tahrirlanadi.
    enum class MoveMode { Idle, Patrol, Follow };

    float Speed = 3.0f;                            // ZM_FIELD_RANGE: 0..10 slayder
    MoveMode Mode = MoveMode::Idle;                // ZM_FIELD_ENUM: ochiladigan ro'yxat
    glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
    int Health = 100;
    bool ShowDebug = false;
    glm::vec4 Tint = { 1.0f, 1.0f, 1.0f, 1.0f };   // ZM_FIELD_COLOR: rang tanlagich
    std::string Greeting = "Salom";
    EntityRef Target;                              // boshqa obyekt: ierarxiyadan sudrab qo'ying

    // ================================================================ hayot sikli
    // Tartib: OnCreate -> OnEnable -> OnStart -> (OnUpdate -> OnLateUpdate) ... -> OnDisable -> OnDestroy

    // Bir marta, skript yaratilganda (o'yin boshlanganda yoki obyekt paydo bo'lganda).
    // Boshqa skriptlar hali yaratilmagan bo'lishi mumkin - ularni OnStart'da qidiring.
    void OnCreate() override
    {
        ZM_INFO("$CLASS$ yaratildi: {} ({})", GetName(), Greeting);
    }

    // Skript yoqilganda: OnCreate'dan keyin va har safar SetEnabled(true) qilinganda.
    // Hodisalarga obuna bo'lish uchun qulay joy (OnDisable'da bekor qilinadi).
    void OnEnable() override
    {
        m_GameOverSubscription = Subscribe("GameOver", [this](const ScriptMessage&) { SetEnabled(false); });
    }

    // Birinchi OnUpdate'dan oldin bir marta. Bu paytda sahnadagi hamma skriptlar
    // OnCreate/OnEnable'dan o'tgan: boshqa obyektlarni topish uchun eng yaxshi joy.
    void OnStart() override
    {
        Transform().Translation += Offset;
        m_StartPosition = Transform().Translation;
        if (auto* sprite = TryGetComponent<SpriteRendererComponent>())
            sprite->Color = Tint;
        if (Entity target = Resolve(Target))
            ZM_INFO("$CLASS$: nishon - {}", target.GetName());
    }

    // Har kadrda, fizikadan oldin. Input nomlari: Fayl > Loyiha sozlamalari > Input.
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
            ZM_INFO("$CLASS$: sakrash!");
        // Debug chizish (editor va Development build'da; Shipping'da chaqiruvlar bo'sh):
        // DebugDraw::Arrow(GetWorldPosition(), GetWorldPosition() + glm::vec3(0, 2, 0), DebugColor::Green);
        // DebugDraw::Text(GetWorldPosition() + glm::vec3(0, 2.5f, 0), GetName());
    }

    // Har kadrda, fizikadan keyin (masalan, kamera kuzatishi uchun).
    void OnLateUpdate(Timestep /*ts*/) override
    {
    }

    // Skript o'chirilganda (SetEnabled(false)) va OnDestroy'dan oldin.
    void OnDisable() override
    {
        Unsubscribe(m_GameOverSubscription);
        m_GameOverSubscription = 0;
    }

    // Obyekt o'chirilganda yoki o'yin to'xtaganda.
    void OnDestroy() override
    {
        // O'yinlar orasida saqlanadigan qiymat: SaveData::SetInt("BestHealth", Health);
    }

    // ================================================================ fizika (2D va 3D)
    // Qattiq to'qnashuv boshlandi / tugadi (ikkala collider ham sensor emas).
    void OnCollisionBegin(Entity other) override
    {
        if (ShowDebug)
            ZM_INFO("$CLASS$ urildi: {}", other.GetName());
    }

    void OnCollisionEnd(Entity /*other*/) override
    {
    }

    // Sensor hududiga (collider'da "Sensor" belgisi) kirildi / chiqildi.
    void OnTriggerBegin(Entity other) override
    {
        if (ShowDebug && other.HasTag("Player"))
            ZM_INFO("$CLASS$: o'yinchi hududga kirdi");
        // Masalan, yig'iladigan buyum: PlaySound("Audio/pickup.wav"); Destroy();
    }

    void OnTriggerEnd(Entity /*other*/) override
    {
    }

    // ================================================================ UI va xabarlar
    // Shu obyektdagi UI tugma (UI Button) bosildi.
    void OnClick() override
    {
        ZM_INFO("$CLASS$: tugma bosildi");
    }

    // SendMessage(entity, "Damage", 10) yoki Broadcast(...) bilan kelgan xabarlar.
    void OnMessage(const ScriptMessage& message) override
    {
        if (message.Name == "Damage")
        {
            Health -= message.Get<int>();
            if (Health <= 0)
                Destroy(); // kadr oxirida o'chiriladi
        }
    }

    // ================================================================ sahnalar
    // Sahna yuklandi (o'yinning birinchi sahnasi ham; Additive ham). LoadScene(1) - build indeksi bo'yicha,
    // LoadNextScene() - build ro'yxatidagi keyingisi, ReloadScene() - shu darajani qaytadan.
    void OnSceneLoaded(const SceneEventInfo& scene) override
    {
        if (ShowDebug)
            ZM_INFO("$CLASS$: '{}' sahnasi yuklandi (build indeksi {})", scene.Name, scene.BuildIndex);
    }

private:
    glm::vec3 m_StartPosition{ 0.0f };
    SubscriptionID m_GameOverSubscription = 0;
};

// Inspektorda ko'rinadigan maydonlar. Atributlarni zanjir qilib ham qo'shish mumkin:
//   ZM_FIELD($CLASS$, Speed).Range(0, 10).Tooltip("...").Header("...").Label("...")
ZM_REGISTER_SCRIPT($CLASS$,
    ZM_FIELD_RANGE($CLASS$, Speed, 0.0f, 10.0f).Header("Harakat").Tooltip("Yurish tezligi (birlik/sekund)"),
    ZM_FIELD_ENUM($CLASS$, Mode, "Boshqaruv (Input)", "Patrul", "Nishonga ergashish").Label("Rejim"),
    ZM_FIELD_TOOLTIP($CLASS$, Offset, "OnStart'da joyiga qo'shiladigan siljish"),
    ZM_FIELD_RANGE($CLASS$, Health, 0, 100).Header("Holat"),
    ZM_FIELD_TOOLTIP($CLASS$, ShowDebug, "To'qnashuv va triggerlarni konsolga yozish"),
    ZM_FIELD_COLOR($CLASS$, Tint).Header("Ko'rinish").Tooltip("Sprite rangi (OnStart'da qo'llanadi)"),
    ZM_FIELD($CLASS$, Greeting).Multiline(),
    ZM_FIELD($CLASS$, Target).Header("Havolalar").Tooltip("Ierarxiyadan obyektni shu yerga sudrab qo'ying"))
)ZM";

        constexpr std::string_view MinimalTemplate = R"ZM(// $CLASS$ - C++ gameplay skripti.
// Ishlatish: obyektga "C++ Skript" komponentini qo'shing va "Klass" ro'yxatidan $CLASS$ ni tanlang.
#include <Zamin/Zamin.h>

using namespace Zamin;

class $CLASS$ : public ScriptableEntity
{
public:
    float RotationSpeed = 0.0f; // radian/sekund (inspektorda o'zgartiring)

    void OnCreate() override
    {
        ZM_INFO("$CLASS$ yaratildi: {}", GetName());
    }

    void OnUpdate(Timestep ts) override
    {
        Transform().Rotation.y += RotationSpeed * (float)ts;
    }
};

ZM_REGISTER_SCRIPT($CLASS$, ZM_FIELD_TOOLTIP($CLASS$, RotationSpeed, "Y o'qi atrofida aylanish tezligi"))
)ZM";

        constexpr std::string_view ProjectStarter = R"ZM(// O'yin skriptlari (C++). Har bir klass ZM_REGISTER_SCRIPT orqali ro'yxatdan o'tadi
// va Editor'da "C++ Skript" komponentida tanlanadi. Yangi skript: Skriptlar > Yangi C++ skript.
#include <Zamin/Zamin.h>

using namespace Zamin;

// Obyektni doimiy aylantiradi.
class Rotator : public ScriptableEntity
{
public:
    glm::vec3 Speed = { 0.0f, 1.0f, 0.0f }; // radian/sekund

    void OnUpdate(Timestep ts) override
    {
        Transform().Rotation += Speed * (float)ts;
    }
};
ZM_REGISTER_SCRIPT(Rotator, ZM_FIELD_TOOLTIP(Rotator, Speed, "Har bir o'q atrofida aylanish tezligi (radian/sekund)"))

// WASD / strelkalar / gamepad bilan harakatlanadigan obyekt. "Horizontal", "Vertical" va
// "Jump" nomlari Loyiha sozlamalari > Input oynasida tugmalarga bog'lanadi.
class PlayerController : public ScriptableEntity
{
public:
    float MoveSpeed = 5.0f;

    void OnStart() override
    {
        // SaveData: o'yinlar orasida saqlanadigan qiymatlar (~/.local/share/zamin/<loyiha>)
        int launches = SaveData::GetInt("Launches") + 1;
        SaveData::SetInt("Launches", launches);
        ZM_INFO("PlayerController ishga tushdi: {}, {}-marta", GetName(), launches);
    }

    void OnUpdate(Timestep ts) override
    {
        glm::vec2 move = Input::GetAxis2D("Horizontal", "Vertical");
        Transform().Translation += glm::vec3(move.x, 0.0f, -move.y) * MoveSpeed * (float)ts;
        if (Input::IsActionJustPressed("Jump"))
            ZM_INFO("Sakrash!");
    }

    // Sensor collider'ga kirildi (masalan, yig'iladigan buyum).
    void OnTriggerBegin(Entity other) override
    {
        ZM_INFO("{} hududiga kirildi", other.GetName());
    }
};
ZM_REGISTER_SCRIPT(PlayerController,
    ZM_FIELD_RANGE(PlayerController, MoveSpeed, 0.0f, 20.0f).Tooltip("Yurish tezligi (birlik/sekund)"))
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
                return "Klass nomi bo'sh";
            if (std::isdigit((unsigned char)name[0]))
                return "Klass nomi raqam bilan boshlanmasligi kerak";
            for (char ch : name)
                if (!(std::isalnum((unsigned char)ch) || ch == '_'))
                    return std::format("Ruxsat etilmagan belgi: '{}' (faqat lotin harflari, raqamlar va _)", ch);
            if (name.size() >= 2 && name[0] == '_' && (name[1] == '_' || std::isupper((unsigned char)name[1])))
                return "'_' + katta harf yoki '__' bilan boshlanadigan nomlar C++ uchun band";
            if (name.find("__") != std::string_view::npos)
                return "'__' (ikki pastki chiziq) C++ uchun band";
            if (Contains(CppKeywords, name))
                return std::format("'{}' - C++ kalit so'zi yoki band nom", name);
            if (Contains(EngineNames, name))
                return std::format("'{0}' - Zamin engine'idagi nom bilan to'qnashadi (using namespace Zamin). "
                                   "Masalan '{0}Controller' yoki '{0}Script' deb nomlang", name);
            if (Contains(TemplateNames, name))
                return std::format("'{0}' - skript funksiyasi nomi bilan bir xil. Masalan '{0}Script' deb nomlang", name);
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
