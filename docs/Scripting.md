# Zamin Engine: C++ skriptlar qo'llanmasi

O'yin mantig'i Zamin'da C++ da yoziladi: har bir skript `ScriptableEntity` dan meros olgan klass, u sahnadagi obyektga
**C++ Script** komponenti orqali ulanadi. Bu qo'llanma skript yaratishdan tortib hayot sikli, inspektor maydonlari,
hodisalar, fizika, input, sahnalar, audio, hot reload va eksportgacha hammasini tushuntiradi. Har bir funksiya nomi
`Zamin/src/Zamin/Scripting/ScriptableEntity.h` dagi haqiqiy API; misollar namuna loyihalardan
(`Projects/Platformer2D`, `Projects/Showcase3D`, `Projects/Adventure3D`) yoki
[2D](Tutorial-2D.md) / [3D](Tutorial-3D.md) qo'llanmalaridan olingan va kompilyatsiya qilib sinalgan.

Kod misollari `#include <Zamin/Zamin.h>` va `using namespace Zamin;` bilan yoziladi.

Mundarija:

1. [Skript qanday ishlaydi](#1-skript-qanday-ishlaydi)
2. [Birinchi skript](#2-birinchi-skript)
3. [Skript fayli tuzilishi](#3-skript-fayli-tuzilishi)
4. [Inspektor maydonlari (ZM_FIELD)](#4-inspektor-maydonlari-zm_field)
5. [Hayot sikli](#5-hayot-sikli)
6. [Hodisa callback'lari](#6-hodisa-callbacklari)
7. [Obyektlar, komponentlar, ierarxiya](#7-obyektlar-komponentlar-ierarxiya)
8. [Input](#8-input)
9. [Fizika va so'rovlar](#9-fizika-va-sorovlar)
10. [Vaqt, taymerlar, tween](#10-vaqt-taymerlar-tween)
11. [Xabarlar, hodisalar shinasi, global holat](#11-xabarlar-hodisalar-shinasi-global-holat)
12. [Sahnalar](#12-sahnalar)
13. [Audio](#13-audio)
14. [Grafika, rang va effektlar](#14-grafika-rang-va-effektlar)
15. [UI](#15-ui)
16. [Animatsiya](#16-animatsiya)
17. [AI va navigatsiya](#17-ai-va-navigatsiya)
18. [Saqlash, lokalizatsiya, log](#18-saqlash-lokalizatsiya-log)
19. [Bir nechta fayl va skriptlar orasida aloqa](#19-bir-nechta-fayl-va-skriptlar-orasida-aloqa)
20. [Kompilyatsiya va hot reload](#20-kompilyatsiya-va-hot-reload)
21. [Eksport qilingan o'yinda skriptlar](#21-eksport-qilingan-oyinda-skriptlar)
22. [Namuna skriptlar](#22-namuna-skriptlar)
23. [Ko'p uchraydigan xatolar](#23-kop-uchraydigan-xatolar)
24. [API ma'lumotnomasi](#24-api-malumotnomasi)

---

## 1. Skript qanday ishlaydi

- Loyihaning C++ kodi `Source/` papkasida turadi (`.zproject` dagi `SourceDirectory`). Loyiha yaratilganda editor
  `CMakeLists.txt` yozadi: `Source/*.cpp` va `Source/*.h` fayllar `zamin_add_game_module(<Loyiha> ...)` bilan bitta
  **skript moduli** — `Binaries/lib<Loyiha>.so` (`ScriptModulePath`) bo'lib kompilyatsiya qilinadi. Yangi fayl
  qo'shish uchun CMake'ni o'zgartirish shart emas (`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`).
- Dvigatel `libZamin.so` umumiy kutubxona: editor, player va o'yin moduli uning bitta nusxasidan foydalanadi, shuning
  uchun skript `Scene`, `Input`, `Audio` va boshqa hamma API'ni to'g'ridan-to'g'ri chaqiradi.
- Har bir skript klassi faylning oxirida `ZM_REGISTER_SCRIPT(Klass, maydonlar...)` bilan ro'yxatdan o'tadi. Modul
  yuklanganda (`ScriptEngine::LoadModule`) klasslar `ScriptRegistry` ga yoziladi va inspektorda **C++ Script →
  Klass** ro'yxatida ko'rinadi; modul yopilganda o'zlari chiqib ketadi.
- Obyektdagi `NativeScriptComponent` faqat klass nomi (`ClassName`), inspektorda berilgan maydon qiymatlari
  (`Fields`) va `Enabled` belgisini saqlaydi. O'yin boshlanganda (editorda **Play**, yoki `ZaminPlayer`) sahna har bir
  shunday komponent uchun klass nusxasini yaratadi, maydon qiymatlarini unga yozadi va callback'larni chaqiradi.
- Editorda **Play** sahnaning nusxasida ishlaydi: o'yin davomida skript o'zgartirgan hamma narsa **Stop** bosilganda
  yo'qoladi, tahrirlangan sahna joyida qoladi.

## 2. Birinchi skript

1. Loyiha oching (launcher yoki **Fayl → Loyihani ochish...**). Yangi loyihada `Source/Scripts.cpp` allaqachon bor:
   unda `Rotator` va `PlayerController` klasslari (shablon sahnadagi `Cube` obyekti `Rotator` bilan aylanadi).
2. **Skriptlar → Yangi C++ skript...** (yoki **Kontent** panelida bo'sh joyga o'ng tugma → **Yangi C++ skript...**).
   Oynada:
   - **Klass nomi** — masalan `EnemyAI`. Nom darhol tekshiriladi: bo'sh bo'lmasligi, raqam bilan boshlanmasligi,
     faqat lotin harflari, raqamlar va `_` bo'lishi kerak; C++ kalit so'zlari va `using namespace Zamin` bilan
     to'qnashadigan engine nomlari (`Camera`, `Entity`, `Input` ...) qabul qilinmaydi — oyna `CameraController`
     kabi nom taklif qiladi. Shu nomli klass yoki `Source/<Nom>.cpp` fayli bor bo'lsa ham ogohlantiradi.
   - **Shablon** — **To'liq** (hamma callback'lar misollar bilan, har xil inspektor maydonlari: slayder, ro'yxat,
     rang, havola) yoki **Minimal** (faqat `OnCreate` va `OnUpdate`, bitta maydon).
   - **Yaratish** — `Source/<Nom>.cpp` yoziladi va skriptlar darhol kompilyatsiya qilinadi.
3. Obyektni tanlang → inspektorda **+ Komponent → Boshqa → C++ Script** → **Klass** ro'yxatidan klassni tanlang.
   Klass yonidagi qalam tugmasi uning kodini tashqi muharrirda ochadi (**Tahrirlash → Sozlamalar** dagi tashqi
   muharrir; `Source` dagi `ZM_REGISTER_SCRIPT` bo'yicha topiladi).
4. **▶ O'ynash** (`Ctrl+P`). Skriptning `ZM_INFO(...)` xabarlari **Konsol** panelida chiqadi.
5. Kodni o'zgartirgach **Skriptlar → Kompilyatsiya + qayta yuklash** (`Ctrl+B`). Batafsil: [20-bo'lim](#20-kompilyatsiya-va-hot-reload).

**Skriptlar** menyusining pastida modulda ro'yxatdan o'tgan hamma klasslar ko'rinadi (yonida maydonlar soni);
bosilganda klass kodi ochiladi.

## 3. Skript fayli tuzilishi

Yangi loyihaning `Source/Scripts.cpp` faylidagi eng oddiy skript:

```cpp
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
```

- `#include <Zamin/Zamin.h>` — dvigatelning butun ommaviy API'si (sahna, komponentlar, input, audio, matematika ...).
- Klass `ScriptableEntity` dan **public** meros oladi; callback'lar `override` bilan yoziladi (keraklilarigina).
- Ommaviy a'zo o'zgaruvchilar `ZM_FIELD` bilan ro'yxatga qo'shilsa, inspektorda tahrirlanadi (4-bo'lim);
  qolganlari (`m_` bilan boshlanadigan `private` a'zolar) faqat kodning o'zi uchun.
- `Timestep` — kadr vaqti sekundda; `float` ga o'z-o'zidan aylanadi (`(float)ts` yoki `ts.GetSeconds()`).
- `Transform()` — obyektning `TransformComponent`'i (`Translation`, `Rotation` — Euler burchaklar radianda, `Scale`).
- Bir faylda bir nechta klass bo'lishi mumkin; har biri o'zining `ZM_REGISTER_SCRIPT` qatoriga ega.

## 4. Inspektor maydonlari (ZM_FIELD)

`ZM_REGISTER_SCRIPT(Klass, ...)` ichida sanalgan a'zolar inspektorda **C++ Skript** bo'limida chiqadi. Qiymatlar
sahna faylida (`"NativeScript": { "Class": ..., "Fields": {...} }`) saqlanadi va o'yin boshlanganda nusxaga yoziladi —
koddagi boshlang'ich qiymat faqat inspektorda o'zgartirilmagan maydon uchun ishlaydi.

Qo'llab-quvvatlanadigan turlar (`ScriptFieldType`): `float`, `int`, `bool`, `glm::vec2`, `glm::vec3`, `glm::vec4`,
`std::string`, `EntityRef` (boshqa obyektga havola), `AnimationCurve` (egri chiziq muharriri bilan) va istalgan `enum` /
`enum class` (har qanday asosiy tur). Boshqa tur `ZM_FIELD` da kompilyatsiya xatosini beradi.

| Makros | Inspektorda |
|---|---|
| `ZM_FIELD(Klass, A'zo)` | Turiga mos boshqaruv (son, belgilash, vektor, matn, havola ...) |
| `ZM_FIELD_RANGE(Klass, A'zo, Min, Max)` | `[Min, Max]` slayder (`float`, `int`, `glm::vecN`; `AnimationCurve` uchun ko'rinadigan oraliq) |
| `ZM_FIELD_COLOR(Klass, A'zo)` | Rang tanlagich (`glm::vec3` / `glm::vec4`) |
| `ZM_FIELD_COLOR_HDR(Klass, A'zo)` | Rang tanlagich, qiymatlar 1 dan katta bo'lishi mumkin |
| `ZM_FIELD_TOOLTIP(Klass, A'zo, "matn")` | Sichqoncha ustiga kelganda yordam matni |
| `ZM_FIELD_ENUM(Klass, A'zo, "A", "B", {"C", 10})` | Ochiladigan ro'yxat (`enum` yoki `int`) |

Atributlar zanjir bo'lib ham qo'shiladi (`ScriptFieldInfo`): `.Range(min, max)`, `.Tooltip("...")`,
`.Label("Nom")`, `.Header("Bo'lim")`, `.Color(hdr)`, `.Multiline()`, `.Options({...})`. Ular faqat inspektordagi
ko'rinishni o'zgartiradi. `Projects/Platformer2D/Source/Player.cpp` dan:

```cpp
ZM_REGISTER_SCRIPT(PlayerController,
    ZM_FIELD_RANGE(PlayerController, MoveSpeed, 0.0f, 20.0f).Header("Harakat").Tooltip("Yurish tezligi (birlik/sekund)"),
    ZM_FIELD_RANGE(PlayerController, JumpImpulse, 0.0f, 30.0f).Tooltip("Sakrash kuchi (impuls)"),
    ZM_FIELD(PlayerController, FallLimit).Tooltip("Shu balandlikdan pastga tushsa, o'yinchi boshlang'ich joyga qaytadi"),
    ZM_FIELD(PlayerController, DropThrough).Label("Platformadan tushish")
        .Tooltip("Pastga (S) + sakrash: bir tomonlama platformadan (plitka yoki Box Collider) pastga tushib ketadi"))
```

Ro'yxat (enum) — `Projects/Platformer2D/Source/Gameplay.cpp` dagi `MovingPlatform`:

```cpp
enum class Motion { Smooth, Linear };
Motion Mode = Motion::Smooth;
...
ZM_FIELD_ENUM(MovingPlatform, Mode, "Silliq (sinus)", "Bir tekis").Label("Harakat turi")
```

Variantlarning qiymati avvalgisidan bittaga ortadi (`{"A", "B", {"Z", 10}, "W"}` → 0, 1, 10, 11). Aniq qiymat
(`{"Boss", 10}`) enum'ning haqiqiy qiymati bo'lishi kerak — asosiy turi berilmagan enum faqat o'z qiymatlari
oralig'ini saqlay oladi (aniq tur bering: `enum class Mode : int`). Maydon turi `int` ↔ `enum` o'zgarsa, sahnadagi
qiymat saqlanib qoladi.

**Havola (`EntityRef`)** — boshqa obyektga ishora. Inspektorda ierarxiyadan obyektni maydonga sudrab qo'yiladi yoki
ro'yxatdan tanlanadi; kodda `Resolve(ref)` bilan `Entity` olinadi (topilmasa bo'sh `Entity`). Obyekt nusxalanganda yoki
prefab'dan yaratilganda ichki havolalar yangi nusxaga moslashadi. [3D qo'llanmadagi](Tutorial-3D.md) kamera:

```cpp
EntityRef Target;  // ierarxiyadan sudrab qo'yiladi
...
Entity target = Resolve(Target);
if (!target)
    return;
```

**O'yin paytida** inspektordagi maydonni o'zgartirish ishlayotgan nusxaga darhol yoziladi (tajriba qilish uchun), lekin
sahnaga saqlanmaydi — **Stop** dan keyin avvalgi qiymat qaytadi. O'yin paytida klassni almashtirib bo'lmaydi.

## 5. Hayot sikli

| Callback | Qachon chaqiriladi |
|---|---|
| `OnCreate()` | Bir marta, nusxa yaratilganda (o'yin boshlanganda yoki obyekt paydo bo'lganda). Sahnadagi hamma `OnCreate`'lar birinchi `OnEnable`'dan oldin |
| `OnEnable()` | `OnCreate`'dan keyin (skript yoqilgan bo'lsa) va har safar qayta yoqilganda |
| `OnStart()` | Bir marta, birinchi `OnUpdate`'dan oldin — o'sha paytda mavjud hamma skriptlar `OnCreate`/`OnEnable`'dan o'tgan: boshqa obyektlarni qidirish uchun qulay joy |
| `OnUpdate(ts)` | Har kadr (fizika qadamidan oldin), skript yoqilgan bo'lsa |
| `OnLateUpdate(ts)` | Har kadr, fizikadan keyin — kamera kuzatish uchun |
| `OnDisable()` | O'chirilganda (`SetEnabled(false)`, obyekt nofaol bo'lganda) va `OnDestroy`'dan oldin (yoqilgan bo'lsa) |
| `OnDestroy()` | Bir marta: obyekt o'chirilganda yoki o'yin to'xtaganda |

Tartib: `OnCreate → OnEnable → OnStart → (OnUpdate → OnLateUpdate)... → OnDisable → OnDestroy`.

**Yoqilgan / o'chirilgan skript.** Inspektordagi **Faol** belgisi (`NativeScriptComponent::Enabled`), kodda
`SetEnabled(bool)` / `IsEnabled()`. `SetEnabled` `OnEnable` / `OnDisable` ni darhol chaqiradi. O'chirilgan skript
`OnUpdate` / `OnLateUpdate` olmaydi, taymer va tween'lari to'xtab turadi va yoqilganda davom etadi (`OnStart` qayta
chaqirilmaydi). `HasStarted()` — `OnStart` o'tganmi.

**Faol / nofaol obyekt.** Obyektning o'z belgisi (`TagComponent::Active`) va ota-onalarinikilar: `SetActive(false)`
(shu obyekt), `SetActive(entity, false)` yoki `entity.SetActive(false)` butun ierarxiyani o'chiradi. Skriptlar
`SetActive` qaytishidan oldin `OnDisable` oladi (avval ota, keyin bolalar), ularning fizik jismlari dunyodan chiqadi,
hech narsasi chizilmaydi; qayta yoqilganda `OnEnable` keladi. `IsActive()` — o'z belgisi, `IsActiveInHierarchy()` —
ota-onalari bilan, `IsActiveAndEnabled()` — skript ham yoqilgan va obyekt ham faol. O'yin boshida nofaol obyekt
skripti faqat `OnCreate` oladi; `OnEnable` / `OnStart` u faol bo'lganda keladi.

Hodisa callback'lari (to'qnashuv, trigger, xabar, `OnClick` ...) o'chirilgan skriptga ham keladi — u o'zini javoban
yoqishi mumkin. Xabarlar va hodisalar shinasi nofaol obyekt skriptiga ham yetadi, `OnClick` esa kelmaydi (nofaol
tugma bosilmaydi).

## 6. Hodisa callback'lari

Hammasi `virtual`, bo'sh standart amalga ega — faqat keraklisini `override` qiling.

| Callback | Qachon |
|---|---|
| `OnCollisionBegin(Entity other)` / `OnCollisionEnd(other)` | Qattiq to'qnashuv (ikkala collider ham sensor emas), har obyekt jufti uchun bir marta; 2D va 3D |
| `OnTriggerBegin(Entity other)` / `OnTriggerEnd(other)` | Kamida bittasi sensor (**Trigger (sensor)** / `IsSensor`) bo'lgan tegish; 2D, 3D va Character Controller 3D. `OnTriggerEnd` ichida turgan obyekt o'chirilganda ham keladi |
| `OnJointBreak(const JointBreak2D&)` | Shu obyektning 2D joint'i uzildi (`Type`, `ConnectedBody`, `Force`, `Torque`) |
| `OnJointBreak3D(const JointBreak3D&)` | Shu obyektning 3D joint'i uzildi |
| `OnClick()` | Shu obyektdagi UI Button bosildi (sichqoncha, yoki tanlangan paytda Enter / gamepad A) |
| `OnPointerEnter/Exit/Down/Up/Click(const UIPointerEvent&)` | Sichqoncha UI obyekt ustida (`e.Target`, `e.Position`, `e.ScreenPosition`, `e.Inside`) |
| `OnSelect()` / `OnDeselect()` / `OnCancel()` | Klaviatura / gamepad navigatsiyasi: tanlandi / tanlov ketdi / Escape yoki B |
| `OnValueChanged(float)` | UI Slider o'yinchi tomonidan surildi |
| `OnToggleChanged(bool)` | UI Toggle almashdi |
| `OnTextChanged / OnTextSubmit / OnTextEndEdit(const std::string&)` | UI Input Field: matn o'zgardi / Enter / tahrir tugadi |
| `OnScrollChanged(const glm::vec2&)` | UI Scroll View kontenti surildi (x: 0 chap..1 o'ng, y: 1 tepa..0 past) |
| `OnSoundFinished(const SoundFinished&)` | Shu obyektning ovozi o'zi tugadi (`Handle`, `Clip`, `FromAudioSource`, `Stolen`) |
| `OnParticlesFinished()` | Takrorlanmaydigan Particle System o'z siklini tugatdi |
| `OnTargetSpotted(Entity)` / `OnTargetLost(Entity)` | Sight Sensor nishonni ko'rdi / yo'qotdi |
| `OnPatrolPointReached(int index)` | Patrol Path nuqtaga yetdi |
| `OnMessage(const ScriptMessage&)` | Shu obyektga xabar keldi (`SendMessage`, `Broadcast` ...) |
| `OnAnimationEvent(const AnimationEventInfo&)` | Animation Player / Animator event'i (`Name`, `Parameter`, `Clip`, `Time`, `Source`, `Skeletal`) |
| `OnLanguageChanged(const std::string&)` | O'yin tili almashdi — `Localize(...)` bilan yasalgan matnlarni qayta yozing |
| `OnSceneLoaded / OnSceneUnloaded(const SceneEventInfo&)`, `OnActiveSceneChanged(prev, current)` | SceneManager hodisalari (`Name`, `Path`, `BuildIndex`, `Mode`) |

Ko'pchiligi sahnaning hodisalar shinasiga ham chiqadi (masalan `"SoundFinished"`, `"ParticlesFinished"`,
`"TargetSpotted"`, `"UISliderChanged"`, `"SceneLoaded"`) — boshqa skriptlar `Subscribe` bilan tinglashi mumkin.

Tanga misoli (`Projects/Platformer2D/Source/Gameplay.cpp`): collider'i sensor, shuning uchun tegish
`OnTriggerBegin` bo'lib keladi:

```cpp
void OnTriggerBegin(Entity other) override
{
    if (m_Collected || !other.HasTag("Player"))
        return;
    m_Collected = true;
    Emit("CoinCollected"); // GameManager obuna bo'lgan
    Instantiate(Effect, GetWorldPosition());
    Destroy();
}
```

## 7. Obyektlar, komponentlar, ierarxiya

**Komponentlar.** `GetComponent<T>()` (bo'lishi shart), `TryGetComponent<T>()` (yo'q bo'lsa `nullptr`),
`HasComponent<T>()`, `AddComponent<T>(...)`. Barcha komponentlar va ularning maydonlari: [Components.md](Components.md).

```cpp
if (auto* sprite = TryGetComponent<SpriteRendererComponent>(); sprite && dir != 0.0f)
    sprite->FlipX = dir < 0.0f;
```

**Obyekt haqida.** `GetEntity()`, `GetScene()`, `Transform()`, `GetName()`, `SetName("...")`,
`GetScriptClassName()` (nusxa qaysi klassdan yaratilgan).

**Qidirish.** `FindEntity("Nom")` — birinchi shu nomli obyekt (keshlangan, har kadr chaqirish arzon), avval shu
sahnada, keyin boshqa yuklangan sahnalarda; `FindEntities("Nom")` — hammasi. Teglar (**Teglar** komponenti):
`HasTag("Enemy")`, `AddTag(...)`, `RemoveTag(...)`, `FindEntitiesWithTag("Enemy")`, `FindEntityWithTag(...)`.
Tur bo'yicha: `FindObjectsOfType<T>()` / `FindObjectOfType<T>()` — `T` komponent yoki skript klassi bo'lishi mumkin:

```cpp
for (auto* enemy : FindObjectsOfType<EnemyAI>()) enemy->Alert();
if (auto* hud = FindObjectOfType<Hud>()) hud->SetScore(m_Score);
```

**Boshqa obyektning skripti.** `GetScript<T>(entity)` — `T*` yoki `nullptr`
(`Projects/Adventure3D/Source/World.cpp`):

```cpp
if (auto* game = GetScript<AdventureGame>(FindEntity("Game"))) game->Collect();
```

**Yaratish va o'chirish.** `Instantiate(entity, position)` / `Instantiate(entity)` — mavjud obyektdan nusxa (o'z faollik
belgisi bilan); `Instantiate("Prefabs/Ball.zprefab", position)` — prefab assetidan ([Prefabs.md](Prefabs.md)).
`Destroy(entity)` / `Destroy()` — kadr oxirida o'chiriladi.

**Ierarxiya.** `GetParent()`, `SetParent(parent, keepWorldTransform = true)`, `GetWorldPosition()`, `GetChildren()`,
`GetChildCount()`, `GetChild(i)`, `FindChild("Nom", recursive = true)`, `GetDescendants()`, `GetRoot()`. Bolalar
`Transform`'i otasiga nisbatan. Bolalar / ota-onalardan komponent yoki skript: `GetComponentInChildren<T>()`,
`GetComponentsInChildren<T>()`, `GetComponentInParent<T>()`, `GetComponentsInParent<T>()`, obyektning o'zi uchun
`FindInChildren<T>()`, `FindInParent<T>()` (hammasida `includeSelf = true` standart).

**Faollik.** `SetActive(bool)`, `SetActive(entity, bool)`, `IsActive()`, `IsActiveInHierarchy()`,
`IsActiveAndEnabled()` (5-bo'lim).

## 8. Input

Nomlangan harakatlar va o'qlar **Loyiha sozlamalari → Input** oynasida tugma, sichqoncha yoki gamepad'ga bog'lanadi
va `.zproject` da saqlanadi ([ProjectSettings.md](ProjectSettings.md)). Yangi loyihadagi standartlar:

| Nom | Turi | Bog'lanishlar |
|---|---|---|
| `Jump` | harakat | Space, gamepad A |
| `Fire` | harakat | sichqoncha chap tugmasi, chap Ctrl, o'ng trigger |
| `Interact` | harakat | E, gamepad X |
| `Sprint` | harakat | chap Shift, chap stik bosilishi |
| `Pause` | harakat | Escape, gamepad Start |
| `Horizontal` | o'q | A/D, ←/→, D-pad, chap stik X |
| `Vertical` | o'q | S/W, ↓/↑, D-pad, chap stik Y |
| `Move` | 2D vektor | `Horizontal` + `Vertical`, uzunligi 1 bilan cheklangan |

```cpp
float dir = Input::GetAxis("Horizontal");             // -1..1
glm::vec2 move = Input::GetVector("Move");            // yoki Input::GetAxis2D("Horizontal", "Vertical")
if (Input::IsActionJustPressed("Jump")) ...           // aynan bitta kadr true
if (Input::IsActionPressed("Sprint")) ...             // bosib turilgan paytda
if (Input::IsActionJustReleased("Fire")) ...
```

Qurilmaning o'zi: `Input::IsKeyPressed(Key::W)`, `IsKeyJustPressed`, `IsKeyJustReleased`,
`IsMouseButtonPressed(MouseButton::Left)` (va `...JustPressed` / `...JustReleased`), `GetMousePosition()`,
`GetMouseDelta()`, `GetMouseScrollDelta()`, `IsGamepadConnected(id)`, `IsGamepadButtonPressed(GamepadButton::A)`,
`GetGamepadAxis(GamepadAxis::LeftX)`. Holat har kadr boshida bir marta olinadi: `JustPressed` / `JustReleased` aynan
bitta kadr davomida `true`.

Skriptning o'yin ko'rinishiga bog'liq yordamchilari (piksel, chap-yuqori burchakdan): `GetMousePosition()`,
`GetViewportSize()`, `GetMouseWorldPosition()` (2D: asosiy kamera orqali z = 0 tekisligiga), `ScreenToWorld(screen, z)`,
`ScreenPointToRay(screen)` va `GetMouseRay()` (`Geometry::Raycast` bilan tanlash uchun).

Kursor: `Cursor::SetLockMode(CursorLockMode::Locked)` (sichqoncha bilan qarash — `Input::GetMouseDelta()`),
`Cursor::SetVisible(false)`, `Cursor::SetImage("Textures/crosshair.png", { 16, 16 })`.

## 9. Fizika va so'rovlar

To'liq qo'llanma (jismlar, collider'lar, joint'lar, qatlamlar, sozlamalar, namunalar): [Physics.md](Physics.md).

**2D** (obyektda `Rigidbody2DComponent`): `GetVelocity()`, `SetVelocity(v)`, `ApplyImpulse(i)`, `ApplyForce(f)`,
`SetPosition2D(p)` (jismni ham ko'chiradi), `DropThroughPlatforms(seconds)` (bir tomonlama platformadan tushish),
uxlash: `IsSleeping()`, `WakeUp()`, `PutToSleep()` va boshqa obyekt uchun `IsSleeping(other)`, `WakeUp(other)`.
Boshqa obyektga: `SetVelocity(other, v)`, `ApplyImpulse(other, i)`.

```cpp
// Projects/Platformer2D/Source/Player.cpp
glm::vec2 velocity = GetVelocity();
float target = dir * MoveSpeed;
velocity.x += (target - velocity.x) * std::min(1.0f, 12.0f * (float)ts);
SetVelocity(velocity);
if (Input::IsActionJustPressed("Jump") && m_GroundContacts > 0)
    ApplyImpulse({ 0.0f, JumpImpulse });
```

**3D** (`Rigidbody3DComponent` + 3D collider): `GetVelocity3D()`, `SetVelocity3D(v)`, `GetAngularVelocity3D()`,
`SetAngularVelocity3D(v)`, `ApplyImpulse3D(i)`, `ApplyForce3D(f)`, `ApplyTorque3D(t)`, `SetPosition3D(p)`,
`IsSleeping3D()`, `WakeUp3D()`, `PutToSleep3D()` (va `(other)` variantlari), `SetVelocity3D(other, v)`,
`ApplyImpulse3D(other, i)`.

**Qahramon** (`CharacterController3DComponent`): `Move(velocity)` — gorizontal tezlik, `Jump(speed)` (faqat yerda),
`Grounded`, `Velocity` (oxirgi qadamdagi haqiqiy tezlik), `VerticalVelocity`. Tortishish va zinapoyalarni
komponentning o'zi hisoblaydi:

```cpp
auto* cc = TryGetComponent<CharacterController3DComponent>();
glm::vec2 input = Input::GetVector("Move");
cc->Move(glm::vec3(input.x * Speed, 0.0f, -input.y * Speed));
if (Input::IsActionJustPressed("Jump"))
    cc->Jump(JumpSpeed);
```

**So'rovlar.** Natija `RaycastHit` (`Hit`, `EntityID`, `Point`, `Normal`, `Distance`; `if (hit)` bilan tekshiriladi;
obyekt: `GetScene().GetEntityByUUID(hit.EntityID)`).

| 2D | 3D |
|---|---|
| `Raycast2D(origin, dir, maxDistance, layerMask)` | `Raycast3D(origin, dir, maxDistance, layerMask)` |
| `Raycast2DAll(..., includeTriggers)` | `Raycast3DAll(...)` |
| `OverlapCircle2D(center, r, mask)` | `OverlapSphere3D(center, r, mask)` |
| `OverlapBox2D(center, halfExtents, angleDegrees, mask)` | `OverlapBox3D(center, halfExtents, rotation, mask)` |
| `OverlapPoint2D(point, mask)` | `OverlapCapsule3D(p0, p1, r, mask)` |
| | `SphereCast3D`, `BoxCast3D`, `CapsuleCast3D` (+ `...All`) — "qalin nur" |

To'qnashuv qatlamlari **Loyiha sozlamalari → Fizika qatlamlari** da nomlanadi (2D va 3D umumiy): `LayerMask({ "Ground",
"Enemy" })` — bit niqobi, `LayerIndex("Player")` — indeks (yo'q bo'lsa -1).

```cpp
if (RaycastHit hit = Raycast3D(GetWorldPosition(), { 0, 0, 1 }, 20.0f, LayerMask({ "Enemy" })))
    Hurt(GetScene().GetEntityByUUID(hit.EntityID));
```

Joint'lar va boshqa fizik sozlamalar komponentlar orqali (`GetComponent<RevoluteJoint2DComponent>().MotorSpeed`,
`GetComponent<HingeJoint3DComponent>().JointAngle` ...) — [Components.md](Components.md).

## 10. Vaqt, taymerlar, tween

- `GetTime()` — skriptning sahnasi ishga tushgandan beri o'tgan soniyalar (o'yin soati: vaqt masshtabi va pauzaga ergashadi). Hamma joydan: `Time::GetDeltaTime()`,
  `Time::GetUnscaledDeltaTime()`, `Time::GetFixedDeltaTime()`, `Time::GetFrameCount()`, `Time::GetTimeSinceStartup()`.
- Vaqt masshtabi: `SetTimeScale(0.5f)` (sekin harakat), `GetTimeScale()`; pauza: `PauseGame()`, `ResumeGame()`,
  `IsGamePaused()`. `OnUpdate` vaqti, taymerlar, fizika va animatsiyalar unga ergashadi, UI esa yo'q.
  `GetUnscaledDeltaTime()` — haqiqiy kadr vaqti. Pauza menyusini boshqaradigan skript `OnCreate` da
  `SetUpdateWhenPaused(true)` qiladi; taymer/tween'larni haqiqiy vaqtda yurgizish — `SetTimersUnscaled(true)`.
- `QuitGame()` — o'yindan chiqish (player yopiladi, editorda Play to'xtaydi).

**Taymerlar** (asosiy oqimda, `OnUpdate`'dan keyin; callback ichidan ham chaqirish xavfsiz): `Invoke(sekund, fn)`,
`InvokeRepeating(kechikish, interval, fn)`, `CancelInvokes()`, `IsInvoking()`.

**Tween**: `Tween(davomiylik, [](float t) {...}, Ease::OutBack, onComplete)` — `t` 0..1 (easing bilan),
`TweenPosition(target, sekund, ease, onComplete)`, `TweenScale(...)`, `CancelTweens()`, `IsTweening()`.
`Ease`: `Linear`, `InQuad`, `OutQuad`, `InOutQuad`, `OutCubic`, `InOutCubic`, `OutBack`, `OutElastic`, `OutBounce`
(`ApplyEase(ease, t)`).

```cpp
// Tutorial-3D: hamma kristal yig'ilgach 3 soniyadan keyin sahna boshidan yuklanadi
Invoke(RestartDelay, [this]() { ReloadScene(SceneTransition::Fade(0.5f)); });
```

Silliq harakat uchun `Math::Damp(current, target, sharpness, dt)` (kadr tezligiga bog'liq emas),
`Math::SmoothDamp`, `Math::MoveTowards`, `Math::Remap` va boshqalar — [Core.md](Core.md).

## 11. Xabarlar, hodisalar shinasi, global holat

**Xabarlar** — aniq obyektning skriptiga darhol yetkaziladi (avval `AddMessageHandler` bilan shu nomga bog'langan
funksiyalar, keyin `OnMessage`). Yuk (`payload`) istalgan nusxalanadigan qiymat; `msg.Get<T>()` bilan o'qiladi
(sonlar bir-biriga aylanadi), `msg.Sender` — yuborgan obyekt.

```cpp
// Dushman
AddMessageHandler("Damage", [this](const ScriptMessage& m) { Health -= m.Get<float>(); });
// O'yinchi
SendMessage(FindEntityWithTag("Enemy"), "Damage", 25.0f);
```

`SendMessage(target, nom, yuk)` (skripti bor bo'lsa `true`), `SendMessageToChildren(...)`, `SendMessageUpwards(...)`,
`Broadcast(nom, yuk)` (sahnadagi hamma skriptlarga), `RemoveMessageHandlers(nom)`.

**Hodisalar shinasi** (publish / subscribe) — yuboruvchi kim tinglashini bilmaydi. Shu skript qilgan obunalar skript
o'chirilganda o'zi bekor bo'ladi.

```cpp
// GameManager (Projects/Platformer2D/Source/GameManager.cpp)
m_CoinSubscription = Subscribe("CoinCollected", [this](const ScriptMessage&) { AddCoin(); });
...
Unsubscribe(m_CoinSubscription);
// Tanga
Emit("CoinCollected");            // obuna bo'lganlarga hozir
EmitDeferred("LevelDone", 2);     // shu kadrning OnLateUpdate'idan keyin
```

`GetEventBus()` — sahnaning shinasi.

**Global holat va servislar** — sahnalardan tashqarida, har sahna almashishida saqlanadi:

```cpp
GameState::Increment("Coins");
int coins = GameState::GetInt("Coins");
WatchGameState("Coins", [this](std::string_view) { RefreshHud(); }); // skript bilan birga tugaydi
auto& stats = GetOrCreateService<SessionStats>();                    // GetService<T>() - yo'q bo'lsa nullptr
```

`UnwatchGameState(id)` kuzatishni bekor qiladi. Batafsil: [Scenes.md](Scenes.md).

## 12. Sahnalar

Yo'llar `Assets` ga nisbatan; build ro'yxatidagi sahnani nomi yoki indeksi bilan ham chaqirish mumkin (**Loyiha
sozlamalari → Build sahnalar**). Almashish kadr oxirida bo'ladi.

```cpp
LoadScene("Scenes/Level2.zscene");
LoadScene("Scenes/Level2.zscene", SceneTransition::Fade(0.5f));   // qorong'ilashib, qayta yorishadi
LoadScene("Scenes/HUD.zscene", LoadSceneMode::Additive);          // yoniga qo'shib
LoadScene(2);                                                      // build indeksi
if (!LoadNextScene(SceneTransition::Fade(0.5f))) LoadScene(0);     // oxirgi daraja: menyuga
ReloadScene();
```

`GetSceneBuildIndex()`, `GetActiveSceneBuildIndex()`, `GetBuildSceneCount()`, `UnloadScene("HUD")`,
`DontDestroyOnLoad()` / `DontDestroyOnLoad(entity)` (sahna almashganda ham qoladi), `GetSceneManager()`.
Fonda yuklash (yuklanish ekrani) — `LoadSceneAsync` (`Projects/Platformer2D/Source/SceneFlow.cpp`):

```cpp
m_Load = LoadSceneAsync(Target, LoadSceneMode::Single, SceneTransition::Fade(Fade));
if (m_Load)
    m_Load->SetAllowSceneActivation(false); // ekran tayyor bo'lganda o'zi ruxsat beradi
...
// GetProgress() 0..1 (0.9 - yuklandi, faollashtirishni kutmoqda), IsReady(), HasFailed()
```

Sub-sahnalar (Sub-sahna komponenti): `LoadSubScene()` / `LoadSubScene(host)`, `UnloadSubScene()`, `GetSubScene()`.
Batafsil: [Scenes.md](Scenes.md).

## 13. Audio

To'liq qo'llanma (mixer, musiqa, 3D, filtrlar, limitlar, zonalar, profiler): [Audio.md](Audio.md).

Bir martalik ovozlar (qaytaradi: `AudioEngine` handle'i, 0 — chalinmadi):

```cpp
PlaySound("Audio/coin.wav");                                // SFX guruhi
PlaySound("Audio/click.wav", 0.8f, 1.0f, "UI");             // ovoz, pitch, mixer guruhi
PlaySoundAt("Audio/boom.wav", GetWorldPosition());          // 3D o'rni bilan
SoundParams step; step.PitchRandom = { 0.9f, 1.1f }; step.MaxInstances = 3;
PlaySound("Audio/step.wav", step);                          // tasodifiy oraliq, limit, ustuvorlik, FadeIn ...
```

Obyektning **Audio Source** komponenti: `PlayAudio()` / `PlayAudio(fadeIn)`, `StopAudio()` / `StopAudio(fadeOut)`,
`PauseAudio()`, `ResumeAudio()`, `IsAudioPlaying()`, `IsAudioPaused()`, `SetAudioVolume(v)` / `GetAudioVolume()`,
`SetAudioPitch(p)` / `GetAudioPitch()`, `GetAudioTime()` / `SetAudioTime(s)`, `GetAudioLength()`,
`FadeAudio(volume, seconds)`, `GetAudioFade()`, `SetAudioLowPass(hz)`, `SetAudioHighPass(hz)`. Ko'pchiligining boshqa
obyekt uchun `(Entity other, ...)` varianti bor.

Musiqa va mixer — `Audio` klassi: `Audio::PlayMusic("Audio/theme.wav")`, `Audio::CrossfadeTo("Audio/boss.wav", 2.0f)`,
`Audio::StopMusic()`, `Audio::SetGroupVolume("SFX", 0.5f)`, `Audio::SetGroupMuted("Music", true)`,
`Audio::SaveSettings()` / `LoadSettings()`.

## 14. Grafika, rang va effektlar

To'liq qo'llanma (kameralar, materiallar, yorug'lik, soyalar, post-processing, particle'lar): [Rendering.md](Rendering.md).

- **Material** (`MeshRendererComponent`): `GetMaterial()` — shu obyektning o'z nusxasi (boshqalarga ta'sir qilmaydi),
  `GetMaterial(submesh)`, `GetSharedMaterial()` (umumiy `.zmat` — hammaga ta'sir qiladi), `SetSharedMaterial(path)`,
  `ResetMaterial()`; boshqa obyekt uchun `(Entity other, ...)` variantlari.

  ```cpp
  GetMaterial()->SetColor({ 1.0f, 0.2f, 0.2f, 1.0f });
  GetMaterial()->SetEmission({ 1.0f, 0.5f, 0.1f }, 4.0f);
  ```
- **2D rang** (Sprite Renderer, bo'lmasa Circle Renderer, Text, Tilemap): `GetColor()`, `SetColor(c)`,
  `TweenColor(target, sekund, ease, onComplete)`, `FadeTo(alpha, sekund, ...)`.
- **Sprite effektlari**: `Flash(color, duration)` (urilish chaqnashi), `SetOutline(color, thickness)`.
- **Tint animatsiya** (Tint Animation komponenti): `PlayTint()`, `PlayTint(color, duration, mode, cycles, ease)`,
  `StopTint()`, `IsTintPlaying()`.
- **Sprite kadrlari**: `GetSpriteSheet()`, `SetSprite("kadr")`, `PlaySpriteSequence("run", fps, loop)`,
  `PlaySpriteClip("HeroSide")`, `GetSpriteClip()`.
- **Kamera**: `ShakeCamera(intensity, duration, frequency)`, `StopCameraShake()`, `SetCameraTarget(entity, snap)`
  (asosiy kameraning Camera 2D Follow nishoni).
- **Particle**: `EmitParticles(n)`.
- **Debug chizish** (faqat ko'rish uchun; Shipping build'da yo'q): `DebugDraw::Line(a, b, DebugColor::Red)`,
  `DebugDraw::WireSphere(c, r)`, `DebugDraw::Text(p, "matn")` ...

## 15. UI

To'liq qo'llanma (langarlar, Canvas, layout, vidjetlar, hodisalar, navigatsiya, scroll view): [UI.md](UI.md).

UI obyektlari (**UI Transform** komponenti) uchun animatsiyalar o'yin pauzasida ham yuradi; har biri tween ID'sini
qaytaradi (0 — UI Transform yo'q):

```cpp
UIScaleTo({ 1.2f, 1.2f }, 0.2f);                                 // pivot atrofida kattalashadi
UIFadeTo(0.0f, 0.3f, Ease::OutQuad, [this] { SetActive(false); });
UIMoveTo({ 0.0f, 40.0f }, 0.4f);                                 // 40 UI birligi yuqoriroqda
UIPunchScale(0.15f);                                             // qisqa "sakrash"
TweenUI(UITweenProperty::Alpha, glm::vec4(0.3f), 0.6f, { .Easing = Ease::InOutQuad, .Loop = UITweenLoop::PingPong });
```

`CancelUITweens(complete)`, `ShowUI(panel)` / `HideUI(panel, animate)` (UI Animation komponenti bilan ochiladi /
yopiladi). UI hodisalari — 6-bo'lim (`OnClick`, `OnPointer...`, `OnValueChanged` ...). Namunalar:
`Projects/Platformer2D/Source/PauseMenu.cpp`, `GameMenu.cpp`, `UIWidgetsDemo.cpp`, `UILayoutDemo.cpp`.

## 16. Animatsiya

- **Property animatsiya** (Animation Player, `.zanim` kliplar): `PlayAnimation("Klip", speed)` (`speed < 0` —
  oxiridan orqaga), `StopAnimation()`, `IsAnimationPlaying()`.
- **Skeletal** (Animator): komponent orqali — `GetComponent<AnimatorComponent>().SetFloat("Speed", v)`, `SetBool`,
  `SetTrigger`, `Play("Walk", ...)` (`Projects/Adventure3D/Source/Player.cpp` holat mashinasiga `Speed` va `Grounded`
  beradi).
- **Event'lar**: `OnAnimationEvent(e)` yoki nom bo'yicha `AddAnimationEventHandler("Footstep", fn)`,
  `RemoveAnimationEventHandlers("Footstep")`.

Batafsil: [Animation.md](Animation.md).

## 17. AI va navigatsiya

Nav Agent komponenti bo'lsa: `SetDestination(point)`, `StopMoving()`, `SetNavTarget(entity)`, `IsMoving()`,
`GetRemainingDistance()`, `HasArrived()`, `FindPath(from, to)`, `HasNavLineOfSight(from, to)`. Ko'rish (Sight Sensor):
`CanSee(target)`, `CanSeeTarget()`, `GetSeenTarget()`, `GetLastSeenPosition()`, `RemembersTarget()`. Patrul (Patrol
Path): `PausePatrol()`, `ResumePatrol()`, `IsPatrolling()`, `GetPatrolPoint()`, `GetPatrolTarget()`,
`IsWaitingAtPatrolPoint()`, `SetPatrolPoint(i)`. Guruh (Formation): `SetFormationShape(FormationShape::Line)`,
`GetFormationShape()`, `AddFormationMember(e)`, `RemoveFormationMember(e)`, `IsFormationInPlace()`,
`GetFormationLeader()`, `GetFormationSlot(slot)`. Holat mashinasi va xulq daraxti inspektorda ko'rinishi uchun:
`WatchStateMachine(machine)`, `UnwatchStateMachine(...)`, `GetWatchedStateMachines()`, `WatchBehaviorTree(tree)`,
`UnwatchBehaviorTree(...)`, `GetWatchedBehaviorTrees()`. Batafsil va misollar: [AI.md](AI.md)
(`Projects/Adventure3D/Source/Guard.cpp`, `Projects/Showcase3D/Source/AI.cpp`).

## 18. Saqlash, lokalizatsiya, log

**SaveData** — o'yinlar orasida saqlanadigan qiymatlar: `SaveData::SetInt/SetFloat/SetString/SetBool(kalit, qiymat)`,
`GetInt/GetFloat/GetString/GetBool(kalit, standart)`, `HasKey`, `DeleteKey`, `DeleteAll`, `Save()`. Fayl —
`~/.local/share/<Kompaniya>/<Mahsulot>/savedata.json` (**Loyiha sozlamalari → Player** dagi kompaniya va mahsulot
nomi; kompaniyasiz — `~/.local/share/zamin/<Mahsulot>/`); o'yin to'xtaganda, sahna almashganda va dastur yopilganda
avtomatik yoziladi.

```cpp
SaveData::SetInt("TotalCoins", SaveData::GetInt("TotalCoins") + 1);
```

**Lokalizatsiya** — `Localize("menu.start")`, `Localize("hud.score", score)` (`{0}` o'rniga), `LocalizePlural(...)`;
til almashsa `OnLanguageChanged` keladi. Til fayllari: `Assets/Localization/<til>.json` ([ProjectSettings.md](ProjectSettings.md)).

**Log** — `ZM_TRACE`, `ZM_INFO`, `ZM_WARN`, `ZM_ERROR`, `ZM_CRITICAL` (`std::format` sintaksisi) editorning
**Konsol** paneliga va player log fayliga yoziladi:

```cpp
ZM_INFO("Daraja tugadi! Keyingisi: {}", NextScene);
```

**Tekshiruvlar** — `ZM_ASSERT(shart, "xabar {}", qiymat)` faqat Debug build'da yoki CMake `-DZAMIN_ASSERTS=ON` bilan
kompilyatsiya qilinadi (editor skript modulini Release'da build qiladi — skriptdagi `ZM_ASSERT` odatda o'chiq);
`ZM_VERIFY(shart, ...)` har doim tekshiriladi va natijani qaytaradi: `if (!ZM_VERIFY(texture, "'{}' rasmi yo'q", path)) return;`.
Batafsil: [Core.md](Core.md).

## 19. Bir nechta fayl va skriptlar orasida aloqa

`Source/` dagi har bir `.cpp` va `.h` modulga kiradi. Klassni boshqa skriptlar ham ishlatishi kerak bo'lsa, uning
e'lonini header'ga chiqaring (`Projects/Platformer2D/Source/GameManager.h`):

```cpp
#pragma once
#include <Zamin/Zamin.h>

class GameManager : public Zamin::ScriptableEntity
{
public:
    void OnEnable() override;
    void OnMessage(const Zamin::ScriptMessage& message) override;
    void AddCoin();
    ...
};
```

`ZM_REGISTER_SCRIPT(GameManager, ...)` esa `.cpp` faylda (bitta joyda) bo'ladi. Aloqa usullari, bog'liqlik
kamayishi tartibida: hodisalar shinasi (`Emit` / `Subscribe` — yuboruvchi qabul qiluvchini bilmaydi), xabarlar
(`SendMessage` — nom va yuk bilan), to'g'ridan-to'g'ri chaqiruv (`GetScript<GameManager>(entity)->AddCoin()`),
global holat (`GameState`, `Services`).

## 20. Kompilyatsiya va hot reload

- **Skriptlar → Kompilyatsiya + qayta yuklash** (`Ctrl+B`) — editor loyiha papkasidagi `Build/` da
  `cmake -DCMAKE_BUILD_TYPE=Release -DZamin_DIR=<engine>` (birinchi marta) va `cmake --build Build --parallel`
  ni fonda ishga tushiradi. Muvaffaqiyatli bo'lsa modul qayta yuklanadi.
- **Oyna → Kompilyatsiya** (yoki **Skriptlar → Kompilyatsiya natijalari**) — **Muammolar** (xato va ogohlantirishlar
  ro'yxati; bosilganda fayl shu qatorda tashqi muharrirda ochiladi) va **To'liq chiqish**. Holat qatorida oxirgi
  kompilyatsiya natijasi ko'rinadi.
- **Skriptlar → Avtomatik hot-reload** (standart yoqilgan) — modul fayli editordan tashqarida (IDE, terminalda
  `cmake --build`) o'zgarsa, editor uni o'zi qayta yuklaydi. Linker faylni joyida yozgani uchun, yangi modul fayl
  o'zgarmay qolgandan keyingina yuklanadi. **Skriptlar → Modulni qayta yuklash** — qo'lda.
- Modul yuklashdan oldin vaqtinchalik faylga nusxalanadi, shuning uchun asl `.so` ni editor ochiq turganda qayta
  build qilish mumkin.
- Qayta yuklashda o'yin rejimi to'xtatiladi (`Stop`), skript nusxalari yo'q qilinadi; inspektordagi maydon qiymatlari
  sahnada saqlangani uchun yangi kod ular bilan ishlaydi. Maydon nomi o'zgarsa, eski qiymat yangi maydonga o'tmaydi.
- Terminalda build: `cmake -S <loyiha> -B <loyiha>/Build -DZamin_DIR=<engine>/build && cmake --build <loyiha>/Build`.
  Namuna loyihalar engine bilan birga build qilinadi (ildizdagi `CMakeLists.txt`). Sanitizer va coverage build'lari
  namuna modullarini o'zining `GameModules` papkasiga yozadi; dasturlar modulni boshqa papkadan
  `ZAMIN_SCRIPT_MODULE_DIR=<papka>` bilan yuklaydi ([Testing.md](Testing.md)).

## 21. Eksport qilingan o'yinda skriptlar

**Fayl → O'yinni build qilish...** skript modulini alohida `Build/ExportRelease` papkasida Release rejimida
(`-O2 -DNDEBUG`) qayta kompilyatsiya qiladi va o'yin papkasiga qo'yadi; player uni loyiha bilan birga yuklaydi.
Release'da `ZM_ASSERT` tekshirilmaydi va `DebugDraw` (Shipping build'da) chizilmaydi. Batafsil: [Runtime.md](Runtime.md).

## 22. Namuna skriptlar

| Fayl | Nimani ko'rsatadi |
|---|---|
| `Projects/Platformer2D/Source/Player.cpp` | 2D qahramon: tezlik, sakrash, kamera silkinishi, chaqnash, bir tomonlama platformalar |
| `Projects/Platformer2D/Source/Gameplay.cpp` | Tanga (trigger + hodisa), kamera kuzatish, harakatlanuvchi platforma (enum maydon), daraja oxiri |
| `Projects/Platformer2D/Source/GameManager.*` | Header + `.cpp`, obuna, `SaveData`, lokalizatsiya |
| `Projects/Platformer2D/Source/SceneFlow.cpp` | Yuklanish ekrani (`LoadSceneAsync`), `DontDestroyOnLoad`, servislar |
| `Projects/Platformer2D/Source/PauseMenu.cpp`, `GameMenu.cpp` | Pauza va bosh menyu, UI hodisalari, `SetUpdateWhenPaused` |
| `Projects/Platformer2D/Source/Joints.cpp`, `Ragdoll.cpp`, `Effectors.cpp` | 2D joint'lar, ragdoll, effektorlar |
| `Projects/Platformer2D/Source/Chase.cpp`, `TopDown.cpp` | Navigatsiya, top-down boshqaruv |
| `Projects/Showcase3D/Source/Showcase.cpp` | 3D fizika, raycast'lar, materiallar, particle, audio namunalari |
| `Projects/Showcase3D/Source/AI.cpp`, `Squad.cpp` | Ko'rish sensori, holat mashinasi, guruh shakllari |
| `Projects/Adventure3D/Source/Player.cpp` | Uchinchi shaxs qahramon: Character Controller 3D, Animator parametrlari |
| `Projects/Adventure3D/Source/Guard.cpp`, `World.cpp` | Qorovul AI, kristallar, darvoza animatsiyasi |
| [Tutorial-2D.md](Tutorial-2D.md), [Tutorial-3D.md](Tutorial-3D.md) | Noldan qadam-baqadam o'yin va to'liq kod |

## 23. Ko'p uchraydigan xatolar

| Belgi | Sabab va yechim |
|---|---|
| Inspektorda **Klass yuklanmagan** (qizil nom) | Modul kompilyatsiya qilinmagan yoki klass `ZM_REGISTER_SCRIPT` siz. `Ctrl+B`, keyin **Oyna → Kompilyatsiya** ni tekshiring |
| Klass ro'yxati bo'sh: "Skriptlar yo'q. 'Skriptlar > Kompilyatsiya' ni bosing" | Modul hali yo'q — `Ctrl+B` |
| `ZM_FIELD: qo'llab-quvvatlanmaydigan tur` kompilyatsiya xatosi | Maydon turi 4-bo'limdagi ro'yxatda yo'q |
| `OnCollisionBegin` kelmaydi | Collider sensor bo'lsa `OnTriggerBegin` keladi; 2D/3D jism (`Rigidbody`) borligini tekshiring |
| 3D trigger ishlamaydi | Sensor collider ham `Rigidbody 3D` (masalan Static) bilan bo'lishi kerak — jismsiz collider fizik dunyoga qo'shilmaydi |
| `Transform()` noto'g'ri ishlaydi / `Destroy()` hech narsa qilmaydi | Klass nomi ScriptableEntity funksiyasi bilan bir xil (masalan `Transform`) — yangi skript oynasi bunday nomni qabul qilmaydi; boshqa nom bering |
| O'yin paytidagi o'zgarish yo'qoldi | Play sahna nusxasida ishlaydi; maydonlarni Stop holatida o'zgartiring |
| Hot reload'dan keyin Play to'xtadi | Shunday ishlaydi: modul almashganda o'yin rejimi to'xtatiladi |

## 24. API ma'lumotnomasi

`ScriptableEntity` ning barcha ommaviy funksiyalari (bo'limlarga ko'ra). Imzolar va izohlar —
`Zamin/src/Zamin/Scripting/ScriptableEntity.h`. `Tests/docs_check.py` bu ro'yxat header bilan mosligini tekshiradi.

| Soha | Funksiyalar |
|---|---|
| Hayot sikli | `OnCreate`, `OnEnable`, `OnStart`, `OnUpdate`, `OnLateUpdate`, `OnDisable`, `OnDestroy` |
| Fizika hodisalari | `OnCollisionBegin`, `OnCollisionEnd`, `OnTriggerBegin`, `OnTriggerEnd`, `OnJointBreak`, `OnJointBreak3D` |
| UI hodisalari | `OnClick`, `OnPointerEnter`, `OnPointerExit`, `OnPointerDown`, `OnPointerUp`, `OnPointerClick`, `OnSelect`, `OnDeselect`, `OnCancel`, `OnValueChanged`, `OnToggleChanged`, `OnTextChanged`, `OnTextSubmit`, `OnTextEndEdit`, `OnScrollChanged` |
| Boshqa hodisalar | `OnSoundFinished`, `OnParticlesFinished`, `OnTargetSpotted`, `OnTargetLost`, `OnPatrolPointReached`, `OnMessage`, `OnAnimationEvent`, `OnLanguageChanged`, `OnSceneLoaded`, `OnSceneUnloaded`, `OnActiveSceneChanged` |
| Yoqish va faollik | `IsEnabled`, `SetEnabled`, `HasStarted`, `SetActive`, `IsActive`, `IsActiveInHierarchy`, `IsActiveAndEnabled`, `GetScriptClassName` |
| Komponentlar va obyekt | `GetComponent`, `TryGetComponent`, `HasComponent`, `AddComponent`, `GetEntity`, `GetScene`, `Transform`, `GetName`, `SetName` |
| Qidirish | `FindEntity`, `FindEntities`, `HasTag`, `AddTag`, `RemoveTag`, `FindEntitiesWithTag`, `FindEntityWithTag`, `FindObjectsOfType`, `FindObjectOfType`, `GetScript` |
| Yaratish | `Instantiate`, `Destroy` |
| Ierarxiya | `GetParent`, `SetParent`, `GetWorldPosition`, `GetChildren`, `GetChildCount`, `GetChild`, `FindChild`, `GetDescendants`, `GetRoot`, `GetComponentInChildren`, `GetComponentsInChildren`, `GetComponentInParent`, `GetComponentsInParent`, `FindInChildren`, `FindInParent` |
| 2D fizika | `GetVelocity`, `SetVelocity`, `ApplyImpulse`, `ApplyForce`, `SetPosition2D`, `DropThroughPlatforms`, `IsSleeping`, `WakeUp`, `PutToSleep` |
| 3D fizika | `GetVelocity3D`, `SetVelocity3D`, `GetAngularVelocity3D`, `SetAngularVelocity3D`, `ApplyImpulse3D`, `ApplyForce3D`, `ApplyTorque3D`, `SetPosition3D`, `IsSleeping3D`, `WakeUp3D`, `PutToSleep3D` |
| So'rovlar | `Raycast2D`, `Raycast2DAll`, `LayerMask`, `LayerIndex`, `OverlapCircle2D`, `OverlapBox2D`, `OverlapPoint2D`, `Raycast3D`, `Raycast3DAll`, `OverlapSphere3D`, `OverlapBox3D`, `OverlapCapsule3D`, `SphereCast3D`, `SphereCast3DAll`, `BoxCast3D`, `BoxCast3DAll`, `CapsuleCast3D`, `CapsuleCast3DAll` |
| Navigatsiya | `SetDestination`, `StopMoving`, `SetNavTarget`, `IsMoving`, `GetRemainingDistance`, `HasArrived`, `FindPath`, `HasNavLineOfSight` |
| Ko'rish va patrul | `CanSee`, `CanSeeTarget`, `GetSeenTarget`, `GetLastSeenPosition`, `RemembersTarget`, `PausePatrol`, `ResumePatrol`, `IsPatrolling`, `GetPatrolPoint`, `GetPatrolTarget`, `IsWaitingAtPatrolPoint`, `SetPatrolPoint` |
| Guruh va AI | `SetFormationShape`, `GetFormationShape`, `AddFormationMember`, `RemoveFormationMember`, `IsFormationInPlace`, `GetFormationLeader`, `GetFormationSlot`, `WatchStateMachine`, `UnwatchStateMachine`, `GetWatchedStateMachines`, `WatchBehaviorTree`, `UnwatchBehaviorTree`, `GetWatchedBehaviorTrees` |
| Ekran va sichqoncha | `GetMousePosition`, `GetViewportSize`, `GetMouseWorldPosition`, `ScreenToWorld`, `ScreenPointToRay`, `GetMouseRay` |
| Kamera | `ShakeCamera`, `StopCameraShake`, `SetCameraTarget` |
| Material | `GetMaterial`, `GetSharedMaterial`, `SetSharedMaterial`, `ResetMaterial` |
| Particle va sprite | `EmitParticles`, `Flash`, `SetOutline`, `GetSpriteSheet`, `SetSprite`, `PlaySpriteSequence`, `PlaySpriteClip`, `GetSpriteClip` |
| Rang va tint | `GetColor`, `SetColor`, `TweenColor`, `FadeTo`, `PlayTint`, `StopTint`, `IsTintPlaying` |
| UI animatsiya | `TweenUI`, `UIScaleTo`, `UIFadeTo`, `UIMoveTo`, `UIPunchScale`, `CancelUITweens`, `ShowUI`, `HideUI` |
| Sahnalar | `LoadScene`, `LoadNextScene`, `ReloadScene`, `GetSceneBuildIndex`, `GetActiveSceneBuildIndex`, `GetBuildSceneCount`, `LoadSceneAsync`, `UnloadScene`, `DontDestroyOnLoad`, `GetSceneManager`, `LoadSubScene`, `UnloadSubScene`, `GetSubScene` |
| Audio | `PlaySound`, `PlaySoundAt`, `PlayAudio`, `StopAudio`, `PauseAudio`, `ResumeAudio`, `IsAudioPlaying`, `IsAudioPaused`, `SetAudioVolume`, `GetAudioVolume`, `SetAudioPitch`, `GetAudioPitch`, `GetAudioTime`, `SetAudioTime`, `GetAudioLength`, `FadeAudio`, `GetAudioFade`, `SetAudioLowPass`, `SetAudioHighPass` |
| Vaqt | `GetTime`, `SetTimeScale`, `GetTimeScale`, `PauseGame`, `ResumeGame`, `IsGamePaused`, `GetUnscaledDeltaTime`, `SetUpdateWhenPaused`, `IsUpdateWhenPaused`, `SetTimersUnscaled`, `IsTimersUnscaled`, `QuitGame` |
| Taymer va tween | `Invoke`, `InvokeRepeating`, `CancelInvokes`, `IsInvoking`, `Tween`, `TweenPosition`, `TweenScale`, `CancelTweens`, `IsTweening` |
| Havolalar | `Resolve` |
| Xabarlar | `AddMessageHandler`, `RemoveMessageHandlers`, `SendMessage`, `SendMessageToChildren`, `SendMessageUpwards`, `Broadcast` |
| Animatsiya | `AddAnimationEventHandler`, `RemoveAnimationEventHandlers`, `PlayAnimation`, `StopAnimation`, `IsAnimationPlaying` |
| Hodisalar shinasi | `Subscribe`, `Unsubscribe`, `Emit`, `EmitDeferred`, `GetEventBus` |
| Global holat | `WatchGameState`, `UnwatchGameState`, `GetService`, `GetOrCreateService` |
| Ichki | `TickTimers` — sahna har kadr chaqiradi (taymer va tween'larni yurgizadi); skriptdan chaqirilmaydi |

Ko'p ishlatiladigan boshqa API'lar: `Input` (8-bo'lim), `Time`, `Math::...` va `Random::...` ([Core.md](Core.md)),
`Audio` (13-bo'lim), `SaveData`, `GameState`, `Services`, `Localization` (18-bo'lim), `DebugDraw` (14-bo'lim),
`Scene` (`GetScene()`: `GetPrimaryCameraEntity()`, `GetEntityByUUID(id)`, `GetWorldPosition(entity)`,
`SetActive(entity, bool)` ...).
