# Zamin Engine: fizika (2D va 3D)

Bu qo'llanma Zamin'ning ikki fizika dunyosini tushuntiradi: **2D** (Box2D) va **3D** (Jolt Physics). Jismlar va
collider'lar, joint'lar, effektorlar, Character Controller 3D, fizik materiallar, to'qnashuv qatlamlari, skriptdan
boshqarish, to'qnashuv va trigger callback'lari, raycast / overlap / shape cast so'rovlari, uxlash, debug ko'rinishlari
va determinizm — editorda qanday sozlanishi va C++ skriptda qanday ishlatilishi bilan.

Komponentlarning to'liq maydonlari (turi, standart qiymati) [Components.md](Components.md) da: [Fizika 2D](Components.md#4-fizika-2d)
va [Fizika 3D](Components.md#5-fizika-3d). Skript API'ning umumiy qo'llanmasi — [Scripting.md](Scripting.md). Bu yerdagi
misollar `#include <Zamin/Zamin.h>` va `using namespace Zamin;` bilan yozilgan.

Mundarija:

1. [Asosiy tushunchalar](#1-asosiy-tushunchalar)
2. [Qadam, gravitatsiya va simulyatsiya sozlamalari](#2-qadam-gravitatsiya-va-simulyatsiya-sozlamalari)
3. [2D jismlar va collider'lar](#3-2d-jismlar-va-colliderlar)
4. [2D effektorlar: platforma, shamol, suv](#4-2d-effektorlar-platforma-shamol-suv)
5. [2D joint'lar](#5-2d-jointlar)
6. [3D jismlar va collider'lar](#6-3d-jismlar-va-colliderlar)
7. [3D joint'lar va ragdoll](#7-3d-jointlar-va-ragdoll)
8. [Character Controller 3D](#8-character-controller-3d)
9. [Fizik materiallar (.zphysmat)](#9-fizik-materiallar-zphysmat)
10. [To'qnashuv qatlamlari](#10-toqnashuv-qatlamlari)
11. [Skriptdan boshqarish: tezlik, kuch, teleport](#11-skriptdan-boshqarish-tezlik-kuch-teleport)
12. [To'qnashuv, trigger va joint uzilishi callback'lari](#12-toqnashuv-trigger-va-joint-uzilishi-callbacklari)
13. [So'rovlar: raycast, overlap, shape cast](#13-sorovlar-raycast-overlap-shape-cast)
14. [Uxlash](#14-uxlash)
15. [Debug ko'rinishlari](#15-debug-korinishlari)
16. [Determinizm](#16-determinizm)
17. [Namuna sahnalar](#17-namuna-sahnalar)
18. [Ko'p uchraydigan muammolar](#18-kop-uchraydigan-muammolar)
19. [Testlar](#19-testlar)
20. [Qisqa API ma'lumotnomasi](#20-qisqa-api-malumotnomasi)

---

## 1. Asosiy tushunchalar

**Ikki dunyo.** Har sahnada o'yin (Play) yoki simulyatsiya paytida ikkita mustaqil dunyo bo'lishi mumkin: 2D (Box2D,
`PhysicsWorld2D`) va 3D (Jolt, `PhysicsWorld3D`). 2D jism faqat 2D collider'lar bilan, 3D jism faqat 3D collider'lar bilan
to'qnashadi. Dunyo kerak bo'lganda yaratiladi: sahnada 3D jism bo'lmasa Jolt ishga tushmaydi (o'yin davomida 3D prefab
paydo bo'lsa — o'sha paytda ishga tushadi).

**Jism + collider.** Fizik obyekt — bitta obyektda jism (`Rigidbody2DComponent` yoki `Rigidbody3DComponent`) va bir yoki
bir nechta collider. Jismsiz collider dunyoga qo'shilmaydi; 3D jism collider'siz bo'lsa ogohlantirish chiqadi. Bitta
obyektda bir nechta collider (masalan quti + uning ustidagi trigger shar) — bitta jismning shakllari.

**Jism turlari** (inspektorda **Turi**):

| Tur | Nima qiladi | Misol |
|---|---|---|
| `Static` | Qimirlamaydi, massasi cheksiz | yer, devor, tilemap |
| `Dynamic` | Gravitatsiya, kuch, to'qnashuv ta'sir qiladi; transformini fizika yozadi | quti, to'p, qahramon (2D) |
| `Kinematic` | Faqat skript / animatsiya bilan harakatlanadi, uni hech narsa itarmaydi; ustidagilarni olib yuradi | lift, harakatlanuvchi platforma |

**Transformni kim boshqaradi.** Static va Kinematic jismlar obyektning Transform'iga ergashadi — skript yoki Animation
Player ularni `Transform().Translation` bilan surishi mumkin (kinematik 3D jismga tezlik ham hisoblab beriladi, ustidagilar
birga yuradi). **Dynamic** jismning transformini har qadamdan keyin fizika qayta yozadi: uni joyidan ko'chirish uchun
`SetPosition2D` / `SetPosition3D` ishlating (jism ham ko'chadi va uyg'onadi), harakat uchun — tezlik, kuch yoki impuls
([11-bo'lim](#11-skriptdan-boshqarish-tezlik-kuch-teleport)).

**Birliklar.** Masofa — metr (sahna birligi), massa — kg, vaqt — soniya, kuch — N, burchak tezligi — radian/s.
`TransformComponent::Rotation` radianda; joint burchaklari va inspektordagi burchaklar — gradusda. 2D collider o'lchamlari
transform masshtabiga ko'paytiriladi (Box Collider 2D `Size` — **yarim** o'lcham), joint langarlari esa jismning o'z
koordinatalarida, masshtabsiz.

**Kadr tartibi** (`Scene::OnUpdateRuntime`): UI → skriptlarning `OnUpdate` → taymerlar va tween'lar → Nav Agent va
Spline Follower → **fizika qadamlari** → to'qnashuv / trigger / joint uzilishi callback'lari → Animation Player →
`OnLateUpdate` → kamera kuzatish → sprite / tint animatsiyalari, Animator, audio. Demak `OnUpdate`'da berilgan tezlik
shu kadrning o'zida qo'llanadi, `OnLateUpdate` esa jismlarning yangi joyini ko'radi.

**Qachon ishlaydi.** Fizika faqat **Play** (`Ctrl+P`) va **Fizikani simulyatsiya qilish** rejimlarida yuradi (viewport
toolbar'idagi tugma: skriptlarsiz, editor kamerasi bilan). Tahrir rejimida jismlar yo'q — collider'lar faqat chiziladi
(**Ko'rinish > Fizik colliderlar**). Nofaol obyekt (`SetActive(false)`) fizikadan chiqariladi va qayta yoqilganda joriy
transformida yaratiladi ([Scripting.md](Scripting.md#5-hayot-sikli)).

---

## 2. Qadam, gravitatsiya va simulyatsiya sozlamalari

**Qat'iy qadam.** Ikkala dunyo ham `1 / StepRate` soniyalik teng qadamlar bilan yuradi: kadr vaqti qancha qadamni
sig'dirsa, shuncha qadam (ko'pi bilan `MaxStepsPerFrame`, qolgani tashlanadi — o'yin sekinlashadi, lekin orqada qolib
ketmaydi). Natija kadr tezligiga bog'liq emas.

| Sozlama | Qayerda | Standart |
|---|---|---|
| Qadam chastotasi (`PhysicsStep.StepRate`, 20 ... 240 Hz) | Loyiha sozlamalari > **Vaqt** (`FixedStepRate`); sahna o'zinikini *Sahna sozlamalari > Fizika* da qo'yishi mumkin | 60 |
| Kadrda ko'pi bilan qadam (`MaxStepsPerFrame`, 1 ... 20) | shu yerda | 5 |
| Gravitatsiya 2D / 3D | Loyiha sozlamalari > **Fizika** (presetlar: Yer, Oy, Mars, Yupiter, Platformer, Vaznsizlik); sahnada — **Loyihadan** belgisini o'chirib | (0, -9.8) / (0, -9.81, 0) |

Sahna sozlamalari (**Oyna > Sahna sozlamalari**, *Fizika* bo'limi) — `SceneSettings` ning `Gravity`, `Gravity3D`,
`PhysicsStep`, `Physics2D`, `Physics3D` qiymatlari, sahna fayliga yoziladi:

| Bo'lim | Sozlama | Ma'nosi |
|---|---|---|
| Box2D (2D) | **Sub-qadamlar** (`SubSteps`, 1 ... 16) | Har qat'iy qadam shuncha kichik Box2D qadamiga bo'linadi: uzun zanjirlar, og'ir yuk osilgan joint'lar va ragdoll'lar cho'zilmaydi. Effektorlar va skriptning `ApplyForce` kuchi har sub-qadamda qo'llanadi, joint uzilishi har sub-qadamdan keyin tekshiriladi |
| | **Tezlik / pozitsiya iteratsiyalari** (6 / 2) | Solver o'tishlari: ko'proq — qattiqroq minoralar va joint'lar, ko'proq vaqt |
| | **Uxlash**, **Uxlashgacha** | [14-bo'lim](#14-uxlash) |
| Jolt (3D) | **Uxlash**, **Uxlashgacha** | [14-bo'lim](#14-uxlash) |

O'yin davomida shu bo'limda jonli statistika: jismlar, uyg'oq / uxlayotganlar, joint va kontaktlar soni, kadrdagi Box2D
qadamlari. Skriptdan: `GetScene().GetPhysics2DStats()` / `GetPhysics3DStats()` (`PhysicsStats2D` / `PhysicsStats3D`:
`Bodies`, `Moving`, `Awake`, `Sleeping`, `Joints`, `Contacts`, `Steps`; 3D'da `Characters` ham).

Loyiha sozlamalari haqida batafsil: [ProjectSettings.md](ProjectSettings.md#8-fizika-gravitatsiya-va-toqnashuv-qatlamlari),
vaqt: [ProjectSettings.md](ProjectSettings.md#7-vaqt-va-time-api) (`Time::GetFixedDeltaTime()` — qadam uzunligi).

---

## 3. 2D jismlar va collider'lar

**Tez boshlash.** *Ierarxiya > Yaratish > 2D > Fizik quti* (yoki *Fizik shar*): Sprite / Circle Renderer + Rigidbody 2D
(Dynamic) + Box / Circle Collider 2D tayyor obyekt. Yer uchun shu obyektning **Turi**ni `Static` qiling va masshtabini
cho'zing. ▶ bosing — quti tushib yerda to'xtaydi.

**Rigidbody 2D** (`Rigidbody2DComponent`): `Type`, `FixedRotation` (**Aylanmasin** — platformer qahramoni ag'darilmasin),
`GravityScale` (0 — uchadi, 2 — og'irroq tushadi), `LinearDamping` / `AngularDamping` (havo qarshiligi), `Bullet` (tez kichik
jism devordan o'tib ketmasin), `Layer` (to'qnashuv qatlami), `Sleeping` (uxlash rejimi).

**Collider'lar** — umumiy maydonlar: `Offset`, `Density` (massa = zichlik x yuza), `Friction`, `Restitution`
(sakrovchanlik 0..1), `RestitutionThreshold` (shundan sekin urilish sakramaydi), `IsSensor` (**Trigger (sensor)** —
to'qnashmaydi, faqat `OnTriggerBegin` / `OnTriggerEnd` beradi), `MaterialPath` (`.zphysmat` — bo'lsa yuqoridagi qiymatlar
o'rniga, [9-bo'lim](#9-fizik-materiallar-zphysmat)), `Layer` (-1 — jismning qatlami).

| Collider | Shakl | Eslatma |
|---|---|---|
| **Box Collider 2D** | to'rtburchak, `Size` — yarim o'lcham | `OneWay` — bir tomonlama platforma (pastdan o'tiladi) |
| **Circle Collider 2D** | doira, `Radius` | masshtabning kattasiga ko'paytiriladi |
| **Polygon Collider 2D** | istalgan ko'pburchak (`Points`) | botiq yoki 8 tadan ko'p burchakli shakl quloq kesish (ear clipping) bilan bo'linib, ko'pi bilan 8 burchakli qavariq bo'laklarga birlashtiriladi (`Polygon2D::Decompose`); o'zini kesgan kontur qavariq qobiq bo'ladi; inspektorda bo'laklar soni |
| **Edge Collider 2D** | nuqtalar orqali chiziq, `Loop` — yopiq halqa | yuzasi (massasi) yo'q — yer, qiyalik, devor uchun; `OneSided` — Box2D chain: bo'g'inlarga ilinmaydigan bir tomonlama silliq zanjir (qattiq tomoni — nuqtalar yo'nalishining chap tomoni) |
| **Tilemap** | qattiq plitkalardan birlashtirilgan to'rtburchaklar | `TilemapComponent::Collision`, `CollisionLayer`, `Friction` / `MaterialPath`; qatlamning `Collision` belgisi va plitka xususiyatlari (qattiq / yo'q / bir tomonlama) |

**Nuqtalarni viewport'da tahrirlash** (Polygon va Edge): inspektordagi **Nuqtalarni tahrirlash** — nuqtani sudrash,
chiziqqa bosish — yangi nuqta, `Ctrl` + bosish yoki `Delete` — o'chirish, `Shift` — 0.1 qadam, `Esc` — tugatish; har
harakat bitta `Ctrl+Z`. Polygon'da **Spraytga moslash** — sprayt kadrining shaffof bo'lmagan qismi chetidan (ko'pi bilan
12 nuqtali) kontur.

**Bir tomonlama platformalar.** Box Collider 2D'ning **Bir tomonlama** belgisi yoki tilemap plitkasi: yuqoriga
harakatlanayotgan yoki sirtdan pastdagi jism o'tib ketadi, ustiga qo'nadi (tez tushayotgani ham). Skriptga to'qnashuv
faqat platforma ushlab turganda keladi. Pastga tushib ketish — `DropThroughPlatforms(soniya)` (standart 0.35 s):

```cpp
// Platformer2D PlayerController: pastga (S) + sakrash - bir tomonlama platformadan tushib ketadi.
if (Input::IsActionJustPressed("Jump") && m_GroundContacts > 0)
{
    if (DropThrough && Input::GetAxis("Vertical") < -0.5f)
        DropThroughPlatforms(0.3f);
    else
        ApplyImpulse({ 0.0f, JumpImpulse });
}
```

---

## 4. 2D effektorlar: platforma, shamol, suv

Effektor obyektning o'z collider'lariga (odatda Static jism) qo'shiladi.

**Platform Effector 2D** — obyektning barcha collider'larini (yoki tilemap plitkalarini) platformaga aylantiradi:
`OneWay`, `SurfaceArc` (180 — klassik bir tomonlama; kichigi burchaklardan ham o'tkazadi, kattasi yondan ham ushlaydi),
`RotationOffset`, yon tomonda ishqalanish / sakrashni o'chirish (`UseSideFriction`, `UseSideBounce`, `SideArc`) — qahramon
devorga yopishib qolmaydi. Box Collider 2D'ning eski "Bir tomonlama" belgisi inspektorda bir bosishda effektorga
o'tkaziladi. Namuna: Platformer2D `Scenes/Shapes2D.zscene`.

**Area Effector 2D** (shamol, oqim) — collider'lari (odatda trigger) ichidagi dinamik jismlarni har qadamda itaradi:

| Maydon | Ma'nosi |
|---|---|
| `ForceAngle`, `UseGlobalAngle` | yo'nalish gradusda (0 — +X, 90 — yuqoriga); obyekt bilan buriladi yoki dunyoda |
| `ForceMagnitude`, `ScaleWithMass` | kuch, N; **Massaga ko'paytirish** — N/kg: yengil va og'ir jism bir xil tezlanadi |
| `ForceVariation` | har qadamda tasodifiy qo'shimcha (shiddat) |
| `Drag`, `AngularDrag` | ichkarida sekinlatish (1/s) |
| `ColliderMask` | qaysi to'qnashuv qatlamlariga ta'sir qiladi |

Trigger bo'lmagan collider'li effektor unga tegib turganlarni itaradi (konveyer). `AffectedBodies` — o'yin paytida
nechta jism itarildi (inspektorda ham ko'rinadi).

**Buoyancy Effector 2D** (suv) — collider ichida `SurfaceLevel` gacha suyuqlik. Jism siqib chiqargan suyuqlik og'irligi
(`Density` x sirt ostidagi yuza x gravitatsiya) botgan qismining markaziga yuqoriga qo'yiladi: zichligi suyuqlikdan kichik
collider suzadi va to'g'rilanadi, kattasi cho'kadi. `LinearDrag` / `AngularDrag` botgan qismiga qarab, `FlowAngle` /
`FlowMagnitude` / `FlowVariation` — oqim (daryo), `ColliderMask`. Collider **trigger** bo'lishi kerak (inspektor
ogohlantiradi). Viewport'da shamol o'qlari, suv sathi va oqim yo'nalishi chiziladi.

Namuna: Platformer2D `Scenes/Effectors2D.zscene` — hovuzda suzayotgan qutilar va cho'kkan toshlar, shamol ustuni, yon
shamol, `LaserScanner` (Raycast2DAll) va `BlastOnClick` (OverlapCircle2D) skriptlari
(`Projects/Platformer2D/Source/Effectors.cpp`).

---

## 5. 2D joint'lar

Joint obyektning Rigidbody 2D'sini **ulangan jismga** (boshqa obyektning Rigidbody 2D yoki Tilemap'i) yoki — u bo'sh
bo'lsa — dunyodagi qo'zg'almas nuqtaga bog'laydi. *+ Komponent > Fizika 2D* menyusida:

| Joint | Nima | Asosiy maydonlar |
|---|---|---|
| **Distance Joint 2D** | qattiq tayoq, arqon yoki prujina | `Distance` / `AutoConfigureDistance`, `MaxDistanceOnly` (arqon), `Frequency` + `DampingRatio` (prujina) |
| **Revolute Joint 2D** | sharnir (g'ildirak, eshik, mayatnik) | `EnableLimit`, `LowerAngle` / `UpperAngle`, `EnableMotor`, `MotorSpeed`, `MaxMotorTorque`; o'yinda `JointAngle`, `JointSpeed` |
| **Prismatic Joint 2D** | o'q bo'ylab sirpanish (lift, porshen) | `Axis`, chegara, motor |
| **Weld Joint 2D** | payvand — qattiq yoki yumshoq | `Frequency` bilan prujinali |
| **Wheel Joint 2D** | g'ildirak: aylanish + osma prujinasi | `Axis`, prujina, chegara, motor |

**Umumiy maydonlar** (`Joint2DComponentBase`): `ConnectedBody` (`EntityRef`), `Anchor` / `ConnectedAnchor` (jismlarning o'z
koordinatalarida), `AutoConfigureConnectedAnchor` (ulangan langar joint yaratilganda langar turgan joyga qo'yiladi),
`CollideConnected` (**Bir-biri bilan to'qnashsin**), `BreakForce` / `BreakTorque` (**Uzilish** bo'limi: 0 — uzilmaydi).
O'yin paytida o'qiladi: `ReactionForce`, `ReactionTorque`, `Broken`.

**Hayoti.** Joint ikkala jism dunyoda bo'lganda yaratiladi, ulardan biri nofaol bo'lsa yoki o'chirilsa yo'qoladi va
qaytganda qayta yaratiladi. Motor, chegara va prujina o'yin davomida o'zgarsa darhol qo'llanadi, `ConnectedBody`
almashsa joint qayta quriladi. Uzilgan joint (`OnJointBreak`, [12-bo'lim](#12-toqnashuv-trigger-va-joint-uzilishi-callbacklari))
butunlay yo'qoladi; `Broken = false` uni qayta yaratadi. Duplikat va prefab nusxalarida ulangan jism nusxaning o'z
jismiga qayta bog'lanadi; sahnada yo'q jismga ulangan joint uchun bir marta ogohlantirish chiqadi.

**Viewport'da tahrirlash.** Inspektordagi **Langarlarni tahrirlash**: langar (doira), ulangan langar (romb), sirpanish /
osma o'qi (kvadrat) va chegaralar sudraladi, `Shift` — 0.1 birlik / 5° qadam, `Esc` — tugatish; **Langar turgan joyga** —
ulangan langarni bir bosishda langar ustiga qo'yadi. Joint'lar viewport'da *Gizmolar > Joint'lar (2D / 3D)* bilan chiziladi (tanlanganda yoki hammasi).

Motorni skriptdan boshqarish (Platformer2D `CarController`, `Projects/Platformer2D/Source/Joints.cpp`):

```cpp
class CarController : public ScriptableEntity
{
public:
    EntityRef RearWheel, FrontWheel;
    float MaxSpeed = 720.0f; // gradus / s
    float Torque = 60.0f;

    void OnUpdate(Timestep) override
    {
        const float input = Input::GetAxis("Horizontal");
        for (EntityRef ref : { RearWheel, FrontWheel })
        {
            Entity wheel = Resolve(ref);
            if (!wheel || !wheel.HasComponent<WheelJoint2DComponent>())
                continue;
            auto& joint = wheel.GetComponent<WheelJoint2DComponent>();
            joint.EnableMotor = true;
            joint.MotorSpeed = -input * MaxSpeed;   // soat mili bo'yicha - o'ngga
            joint.MaxMotorTorque = Torque;
        }
    }
};
ZM_REGISTER_SCRIPT(CarController, ZM_FIELD(CarController, RearWheel), ZM_FIELD(CarController, FrontWheel),
                   ZM_FIELD(CarController, MaxSpeed), ZM_FIELD(CarController, Torque))
```

Namunalar: Platformer2D `Scenes/Joints2D.zscene` (ko'prik, lift, mayatnik, arqon, prujina, uziladigan chiroq, motorli
mashina — A / D) va `Scenes/Ragdoll2D.zscene` (`Prefabs/Ragdoll.zprefab`: 11 bo'lak, chegarali sharnirlar, 2 sub-qadam;
sichqoncha yoki R — yangi ragdoll).

---

## 6. 3D jismlar va collider'lar

**Tez boshlash.** *Ierarxiya > Yaratish > 3D > Fizik kub* (yoki *Fizik shar*, *Fizik yer (statik)*): mesh + Rigidbody 3D +
mos collider.

**Rigidbody 3D** (`Rigidbody3DComponent`): `Type`, `Mass` (kg), `LinearDamping` / `AngularDamping`, `GravityFactor`,
`LockRotation` (tik turadi), `Layer`, `ContinuousCollision` (**Uzluksiz (CCD)**), `Sleeping`.

**Collider'lar** — umumiy: `Offset`, `Friction`, `Restitution`, `IsSensor`, `MaterialPath`. O'lchamlar transform
masshtabiga ko'paytiriladi.

| Collider | Shakl | Qaysi jism uchun |
|---|---|---|
| **Box Collider 3D** | quti, `HalfExtents` — yarim o'lchamlar | hammasi |
| **Sphere Collider 3D** | shar, `Radius` | hammasi |
| **Capsule Collider 3D** | Y o'qi bo'ylab kapsula: `Radius`, `HalfHeight` | hammasi |
| **Convex Hull Collider 3D** | Mesh Renderer'dagi model / primitiv (yoki `ModelPath` modeli) vertekslarining qavariq qobig'i, ko'pi bilan `MaxVertices` (**Maks. burchaklar**, 4 ... 256) — ko'p bo'lsa eng chetdagi nuqtalar | dinamik jismlar (botiqlar to'lib qoladi: kosa — yaxlit blok) |
| **Mesh Collider 3D** | modelning o'z uchburchaklari (Jolt MeshShape): zinapoya, ark, tunnel, yarim quvur, notekis yer | **faqat Static va Kinematic** — dinamik jism uni hisobga olmaydi (ogohlantirish); uchburchaklar old tomonidan qattiq |

Qobiq va uchburchaklar har model uchun bir marta quriladi (`CollisionShapes3D`, GPU'siz) va fayl yoki uning `.zmeta`
import sozlamalari o'zgarsa qayta quriladi; inspektorda burchak / uchburchak soni, viewport'da (*Fizik colliderlar*)
qirralari ko'rinadi.

**Bir jismda trigger va qattiq collider** birga bo'lishi mumkin (quti + uning atrofidagi "radar" sferasi): trigger qismi
urilmaydi, qahramonni to'xtatmaydi, so'rovlar uni faqat `includeTriggers` bilan ko'radi.

**Uzluksiz to'qnashuv (CCD).** Tez jism (o'q, otilgan to'p) qadamlar orasida yupqa devordan o'tib ketishi mumkin.
`ContinuousCollision` yoqilsa jism har qadamda yo'li bo'ylab tekshiriladi (Jolt LinearCast) — qimmatroq, faqat tez va
kichiklar uchun. O'yin davomida ham yoqiladi. Namuna: Showcase3D `Scenes/Physics3D.zscene` — CCD yoqiq / o'chiq ikki
shar yupqa oynaga otiladi (`Launcher3D` skripti hisoblaydi: "O'tib ketdi" / "To'xtadi").

To'p otish (Showcase3D `BallShooter`): prefabdan nusxa va boshlang'ich tezlik:

```cpp
glm::vec3 forward = Transform().GetForward();
Entity ball = Instantiate("Prefabs/Ball.zprefab", GetWorldPosition() + forward * 1.5f);
SetVelocity3D(ball, forward * Speed);
```

---

## 7. 3D joint'lar va ragdoll

Jolt constraint'lari, 2D joint'lar bilan bir xil mantiq (ulangan jism yoki dunyo nuqtasi, langarlar, avto langar,
**Bir-biri bilan to'qnashsin**, uzilish, nofaol bo'lganda yo'qolib qaytish, duplikat / prefab'da qayta bog'lanish):

| Joint | Nima | Asosiy maydonlar |
|---|---|---|
| **Fixed Joint 3D** | mahkam (birga harakatlanadi) | — |
| **Hinge Joint 3D** | sharnir: eshik, g'ildirak, tirsak | `Axis`, chegaralar (-180..0 / 0..180), motor (`MotorSpeed`, `MaxMotorTorque`), prujina (`EnableSpring`, `SpringTargetAngle`, `SpringFrequency`, `SpringDamping`); o'yinda `JointAngle`, `JointSpeed` |
| **Distance Joint 3D** | tayoq / arqon / oraliq | `MinDistance` .. `MaxDistance`, `MaxDistanceOnly`, `Frequency` |
| **Slider Joint 3D** | o'q bo'ylab sirpanish | `Axis`, chegaralar, motor; o'yinda `JointTranslation` |
| **Cone Joint 3D** | shar-sharnir (yelka, son): Jolt SwingTwist | `SwingLimit` (konus burchagi), `TwistLower` / `TwistUpper`, `FrictionTorque` |

Uzilganda skript `OnJointBreak3D(const JointBreak3D&)` oladi. Inspektorda holat (kuch, moment, burchak, siljish,
masofa, og'ish / buralish), viewport'da langarlar, o'q va burchak yoyi, sirpanish chegaralari, konus (*Gizmolar >
Joint'lar (2D / 3D)*); **Langarlarni tahrirlash** — langarlar, o'q va chegaralar tanlangan nuqtadagi ko'chirish gizmosi bilan.

**Ragdoll.** Showcase3D `Scenes/Ragdoll3D.zscene` — `Prefabs/Ragdoll3D.zprefab`: 11 bo'lak, yelka / son / bel / bo'yinda
Cone Joint 3D, tirsak va tizzada chegarali Hinge Joint 3D, bo'g'imlar o'zaro to'qnashmaydi. `RagdollSpawner3D` skripti:
R — yana 3 ta, sichqoncha / F — kameradan otish. Ragdoll yasashda: har bo'lak Dynamic Rigidbody 3D + Capsule / Box
collider, ota-ona bo'lak `ConnectedBody`, **Bir-biri bilan to'qnashsin** o'chiq, joint'larga `FrictionTorque` — a'zolar
silkinib ketmaydi.

Boshqa namuna: Showcase3D `Scenes/Joints3D.zscene` (prujinali eshik, sharlar zanjiri, lift — `SliderShuttle`, motorli
tegirmon, payvandlangan L, qiyalikdan dumalaydigan qavariq qobiqlar).

---

## 8. Character Controller 3D

Qahramon uchun Rigidbody 3D o'rniga **Character Controller 3D** (`CharacterController3DComponent`, Jolt CharacterVirtual):
kapsula qiyaliklar (`MaxSlope`, gradus) va zinapoyalar (`StepHeight`) bo'ylab yuradi, devorlarga sirpanadi, harakatlanuvchi
platformada turadi, gravitatsiyani o'zi qo'llaydi (`GravityScale`). Obyekt pozitsiyasi — **oyoq** nuqtasi. Yaratish:
*Ierarxiya > Yaratish > 3D > Qahramon (Character Controller)*.

| Maydon | Ma'nosi |
|---|---|
| `Radius`, `Height`, `Mass` | kapsula va massa (itarishda) |
| `MaxSlope`, `StepHeight`, `StickToFloor` | qiyalik chegarasi, zinapoya balandligi, pastga tushishda yerga yopishish |
| `Layer` | to'qnashuv qatlami (jismlar bilan bir xil matritsa) |
| `PushBodies`, `PushForce` | **Jismlarni itaradi**: yurib borib tekkan dinamik jismlarni ko'pi bilan `PushForce` N bilan (standart 100 N; og'irlar uchun ko'proq) |
| `MoveVelocity`, `VerticalVelocity` | skript beradi: gorizontal tezlik (`Move()`), vertikal (`Jump()` yoki to'g'ridan-to'g'ri) |
| `Grounded`, `Velocity` | o'qiladi: yerda turibdimi, oxirgi qadamdagi haqiqiy tezlik (to'qnashuvlardan keyin) |

Skript har kadr gorizontal tezlikni beradi; `Jump(speed)` faqat yerda bo'lsa ishlaydi (`true` qaytaradi). Adventure3D
qahramoni (`Projects/Adventure3D/Source/Player.cpp`, qisqartirilgan):

```cpp
void OnUpdate(Timestep ts) override
{
    auto* cc = TryGetComponent<CharacterController3DComponent>();
    if (!cc) return;

    glm::vec2 input = Input::GetVector("Move");          // WASD / strelkalar / chap stik
    bool run = Input::IsActionPressed("Sprint");

    // Kameraga nisbatan yo'nalish
    glm::vec3 forward(0, 0, -1), right(1, 0, 0);
    if (Entity cam = FindEntity(Camera))
    {
        glm::vec3 f = cam.GetComponent<TransformComponent>().GetForward();
        f.y = 0.0f;
        if (glm::length(f) > 0.01f) forward = glm::normalize(f);
        right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    }
    glm::vec3 wish = forward * input.y + right * input.x;
    float targetSpeed = glm::length(input) * (run ? RunSpeed : WalkSpeed);
    glm::vec3 target = glm::length(wish) > 0.001f ? glm::normalize(wish) * targetSpeed : glm::vec3(0.0f);
    m_Velocity = Math::Damp(m_Velocity, target, Acceleration, ts);   // kadr tezligiga bog'liq emas
    cc->Move(m_Velocity);

    if (Input::IsActionJustPressed("Jump"))
        cc->Jump(JumpSpeed);

    if (Transform().Translation.y < -25.0f)     // dunyodan tushib ketdi
    {
        SetPosition3D(m_Spawn);
        cc->VerticalVelocity = 0.0f;
    }
}
```

- Teleport — `SetPosition3D` (personaj ham ko'chadi). Personaj sensor collider'ga kirsa ikkala tomon ham
  `OnTriggerBegin` oladi (kristallar, tanga, zona) — [Tutorial-3D.md](Tutorial-3D.md).
- Nofaol qilingan personaj dunyodan chiqariladi (gravitatsiya yo'q, `Grounded` false, `Jump` ishlamaydi) va qayta
  yoqilganda joriy joyida tinch holatda yaratiladi.
- Animator root motion yoqilgan bo'lsa personajni klipning o'zi yurgizadi ([Animation.md](Animation.md#10-root-motion)).
- Nav Agent ham Character Controller 3D bilan ishlaydi ([AI.md](AI.md)).

---

## 9. Fizik materiallar (.zphysmat)

Bir xil sirtni ko'p collider'da ishlatish uchun — **fizik material** asset. *Kontent brauzer > o'ng tugma > Yangi fizik
material (2D)* menyusida tayyorlari: Standart, Muz, Rezina to'p, Yopishqoq, Metall, Yog'och. Fayl — JSON
(`PhysicsMaterial::ToJson`):

```json
{
    "Friction": 0.8,
    "Restitution": 0.85,
    "RestitutionThreshold": 0.2,
    "Density": 1.0,
    "FrictionCombine": "Default",
    "RestitutionCombine": "Maximum"
}
```

(`Projects/Platformer2D/Assets/Physics/Bouncy.zphysmat`). Collider'ning **Fizik material** maydoniga sudrab qo'ying — uning
o'z qiymatlari o'rniga material qiymatlari ishlatiladi; maydon bo'sh bo'lsa — collider'niki.

**Qo'shilish usullari** (`PhysicsCombine`): ikki collider tekkanda ishqalanish / sakrash qanday aralashadi —
`Default` (Box2D qoidasi: ishqalanish √(a·b), sakrash max(a, b)), `Average`, `Minimum`, `Multiply`, `Maximum`. Ikki
collider turli usul so'rasa ro'yxatda pastrog'i yutadi (Maximum > Multiply > Minimum > Average > Default). Muz uchun
`Minimum` — ustida hamma narsa sirpanadi; rezina uchun `Maximum` — nimaga tegsa ham sakraydi.

- 3D collider'lar ham shu fayllarni oladi (ishqalanish, sakrash, chegarasi, usullar); **zichlik 3D'da ishlatilmaydi** —
  3D massa Rigidbody 3D'ning `Mass` maydonida.
- Material inspektorda tahrirlanadi; fayl o'zgarsa (tashqi muharrirda ham) o'yin davomida darhol qo'llanadi (hot reload).
- Tilemap ham material oladi (`TilemapComponent::MaterialPath`).

Namunalar: Platformer2D `Physics/Ice.zphysmat`, `Physics/Bouncy.zphysmat` (`Scenes/Shapes2D.zscene`: muz qiyalik, rezina
to'shak); Showcase3D `Physics/Muz.zphysmat`, `Physics/Rezina.zphysmat`, `Physics/Sakraydigan.zphysmat`
(`Scenes/Physics3D.zscene`).

---

## 10. To'qnashuv qatlamlari

Loyihada **16 ta nomli qatlam** va qaysi juftliklar to'qnashishini ko'rsatuvchi **matritsa** — 2D va 3D uchun bir xil
(`CollisionLayers2D`, `CollisionLayers` taxallusi; `.zproject` dagi `"CollisionLayers2D"` bo'limi). *Loyiha sozlamalari >
Fizika*: qatlamlar nomi, burchakli sarlavhali matritsa, tayyor presetlar (hammasi to'qnashadi, har qatlam faqat o'zi bilan,
"Platformer 2D", "3D aksiya" ...), qatorni nusxalash / qo'yish.

Qatlamni kim tanlaydi:

- Rigidbody 2D va Rigidbody 3D — `Layer` (jismning hamma collider'lari);
- 2D collider — o'z `Layer` i (-1 — jismniki);
- Tilemap — `CollisionLayer`; Character Controller 3D — `Layer`.

Ikki collider faqat matritsa ularning qatlamlariga ruxsat bersa tegadi (Box2D filtri: category — qatlam biti, mask —
to'qnashadigan qatlamlar; Jolt ObjectLayer). O'yin davomida qatlam ham, matritsa ham darhol qo'llanadi.

So'rovlarda qatlam niqobi (`uint16_t`, standart `0xFFFF` — hammasi):

```cpp
uint16_t ground = LayerMask({ "Ground" });                    // nom bo'yicha bitlar (noma'lum nom tashlab ketiladi)
RaycastHit hit = Raycast2D(GetWorldPosition(), { 0, -1 }, 1.2f, ground);
uint16_t notGhosts = (uint16_t)~LayerMask({ "Arvoh" });       // "Arvoh" qatlamidan boshqa hammasi
int index = LayerIndex("Ragdoll");                             // -1 - bunday qatlam yo'q
```

Namunalar: Platformer2D (`Ground`, `Ghost` — faqat yerga tegadigan sharlar, `Ragdoll` / `RagdollLimb` — ragdoll a'zolari
o'zaro to'qnashmaydi); Showcase3D (`Arvoh` — panjaradan o'tib ketadigan sharlar, `Panjara`).

---

## 11. Skriptdan boshqarish: tezlik, kuch, teleport

`ScriptableEntity` funksiyalari shu obyektning jismiga ta'sir qiladi; `Entity other` qabul qiladiganlari — boshqa
obyektnikiga.

| Nima | 2D | 3D |
|---|---|---|
| Tezlikni o'qish | `GetVelocity()` | `GetVelocity3D()`, `GetAngularVelocity3D()` |
| Tezlik berish | `SetVelocity(v)`, `SetVelocity(other, v)` | `SetVelocity3D(v)`, `SetVelocity3D(other, v)`, `SetAngularVelocity3D(w)` |
| Impuls (bir zarba) | `ApplyImpulse(i)`, `ApplyImpulse(other, i)` | `ApplyImpulse3D(i)`, `ApplyImpulse3D(other, i)` |
| Doimiy kuch (har kadr) | `ApplyForce(f)` | `ApplyForce3D(f)`, `ApplyTorque3D(t)` |
| Teleport (jism bilan) | `SetPosition2D(p)` | `SetPosition3D(p)` |
| Uxlash | `IsSleeping()`, `WakeUp()`, `PutToSleep()` | `IsSleeping3D()`, `WakeUp3D()`, `PutToSleep3D()` |

- **Impuls** — tezlikni bir zumda o'zgartiradi (sakrash, portlash, o'q). **Kuch** — har kadr berilsa uzluksiz ta'sir
  (dvigatel, shamol). 2D'da skriptning `ApplyForce` kuchi har qadam va sub-qadamda o'rtacha qo'llanadi: bir xil kuch istalgan
  FPS, qadam chastotasi va sub-qadamlar sonida bir xil natija beradi.
- **Teleport** jismni uyg'otadi; 3D'da `SetPosition3D` tezlikni ham to'xtatadi (Showcase3D `Respawn3D`: qayta joylash +
  `SetAngularVelocity3D(glm::vec3(0.0f))`).
- Kinematik jismni transform orqali yurgizish (Showcase3D `Sweeper3D`): `Transform().Translation = ...` — jism tezlik bilan
  yetib boradi va ustidagilarni itaradi / olib yuradi.
- Komponentning o'zi orqali ham: `GetComponent<Rigidbody2DComponent>().GravityScale = 0.0f;` — o'yin davomida
  qo'llanadi (qatlam, CCD, uxlash rejimi ham).

2D platformer qahramoni (Platformer2D `PlayerController`, qisqartirilgan) — tezlikni silliq o'zgartirish va impuls bilan
sakrash; yerda turishni to'qnashuvlar soni bilan bilish:

```cpp
class PlayerController : public ScriptableEntity
{
public:
    float MoveSpeed = 7.0f;
    float JumpImpulse = 9.0f;

    void OnUpdate(Timestep ts) override
    {
        glm::vec2 velocity = GetVelocity();
        float target = Input::GetAxis("Horizontal") * MoveSpeed;
        velocity.x += (target - velocity.x) * std::min(1.0f, 12.0f * (float)ts);
        SetVelocity(velocity);

        if (Input::IsActionJustPressed("Jump") && m_GroundContacts > 0)
        {
            SetVelocity({ velocity.x, 0.0f });
            ApplyImpulse({ 0.0f, JumpImpulse });
        }
    }
    // Yer va platformalar - qattiq to'qnashuv; tangalar sensor bo'lgani uchun bu yerga kelmaydi.
    void OnCollisionBegin(Entity) override { m_GroundContacts++; }
    void OnCollisionEnd(Entity) override { m_GroundContacts = std::max(0, m_GroundContacts - 1); }

private:
    int m_GroundContacts = 0;
};
ZM_REGISTER_SCRIPT(PlayerController, ZM_FIELD(PlayerController, MoveSpeed), ZM_FIELD(PlayerController, JumpImpulse))
```

Rigidbody 2D'da **Aylanmasin** (`FixedRotation`) yoqilgan bo'lsin — aks holda qahramon ag'dariladi. Yerda turishni aniqroq
bilish kerak bo'lsa oyoq ostiga qisqa `Raycast2D` yoki `OverlapBox2D` ([13-bo'lim](#13-sorovlar-raycast-overlap-shape-cast)).

---

## 12. To'qnashuv, trigger va joint uzilishi callback'lari

| Callback | Qachon |
|---|---|
| `OnCollisionBegin(Entity other)` / `OnCollisionEnd(other)` | ikkala collider ham sensor emas: qattiq tegish boshlandi / tugadi |
| `OnTriggerBegin(Entity other)` / `OnTriggerEnd(other)` | kamida bittasi sensor (`IsSensor`): kirdi / chiqdi; 2D, 3D va Character Controller 3D |
| `OnJointBreak(const JointBreak2D&)` | shu obyektning 2D joint'i uzildi: `Type` (`Joint2DType`), `ConnectedBody`, `Force`, `Torque` |
| `OnJointBreak3D(const JointBreak3D&)` | shu obyektning 3D joint'i uzildi: `Type` (`Joint3DType`), ... |

Qoidalar:

- Callback **ikkala** obyektning skriptlariga keladi va **har obyekt jufti uchun bir marta** (bir jismda bir nechta
  collider bo'lsa ham). Qaysi collider tekkanini emas, qaysi obyekt tekkanini bilasiz — kerak bo'lsa tegini tekshiring
  (`other.HasTag("Player")`).
- Callback'lar fizika qadamlaridan keyin, dunyo qulfdan chiqqanda chaqiriladi — ichida `Destroy`, `Instantiate`,
  `SetActive`, `SetPosition2D` bemalol ishlaydi.
- Ichida turgan obyekt o'chirilsa yoki nofaol qilinsa ham `OnTriggerEnd` / `OnCollisionEnd` keladi.
- Uxlab qolgan jismning kontaktlari saqlanadi: uxlagan quti trigger plitasidan "chiqib ketmaydi"; kinematik trigger
  uxlab yotgan jismni ham sezadi (3D).
- Callback'lar o'chirilgan skriptga ham keladi (u o'zini yoqishi mumkin).

Tanga (Platformer2D `Coin`, `Projects/Platformer2D/Source/Gameplay.cpp`) — collider sensor, shuning uchun
`OnTriggerBegin`:

```cpp
void OnTriggerBegin(Entity other) override
{
    if (m_Collected || !other.HasTag("Player"))
        return;
    m_Collected = true;
    Emit("CoinCollected");                       // GameManager obuna bo'lgan
    Instantiate(Effect, GetWorldPosition());
    Destroy();
}
```

Ichidagilarni sanash (Showcase3D `TriggerGlow3D`): `OnTriggerBegin` da `m_Inside[(uint64_t)other.GetUUID()] = ...`,
`OnTriggerEnd` da `m_Inside.erase(...)` — plita bosilganda yonadi.

Joint uzilishi (Platformer2D `BreakableJoint`):

```cpp
void OnJointBreak(const JointBreak2D& joint) override
{
    ZM_INFO("{} joint'i uzildi: {:.0f} N", GetEntity().GetName(), joint.Force);
    Flash({ 1.0f, 0.3f, 0.2f, 1.0f }, 0.4f);
}
```

---

## 13. So'rovlar: raycast, overlap, shape cast

Hamma so'rovlar o'yin / simulyatsiya paytida ishlaydi (tahrir rejimida natija bo'sh), qatlam niqobi (`layerMask`) va
`includeTriggers` (standart — trigger'lar hisobga olinmaydi) oladi.

**Natija** — `RaycastHit`: `Hit` (`if (hit)` ham ishlaydi), `EntityID` (UUID — obyekt: `GetScene().GetEntityByUUID(hit.EntityID)`),
`Point`, `Normal`, `Distance`.

**2D:**

| Funksiya | Nima qaytaradi |
|---|---|
| `Raycast2D(origin, direction, maxDistance = 100, layerMask)` | eng yaqin tegish |
| `Raycast2DAll(origin, direction, maxDistance, layerMask, includeTriggers)` | nur yo'lidagi har obyekt (bir marta), eng yaqini birinchi |
| `OverlapCircle2D(center, radius, layerMask, includeTriggers)` | doira ichidagi obyektlar, markazga yaqini birinchi |
| `OverlapBox2D(center, halfExtents, angleDegrees, layerMask, includeTriggers)` | burilgan to'rtburchak ichidagilar |
| `OverlapPoint2D(point, layerMask, includeTriggers)` | nuqtani o'z ichiga olgan collider'lar |

**3D:**

| Funksiya | Nima qaytaradi |
|---|---|
| `Raycast3D(origin, direction, maxDistance = 100, layerMask)` | eng yaqin tegish |
| `Raycast3DAll(...)` | yo'ldagi har obyekt, yaqinidan |
| `OverlapSphere3D(center, radius, ...)`, `OverlapBox3D(center, halfExtents, rotation, ...)`, `OverlapCapsule3D(point0, point1, radius, ...)` | shakl ichidagi obyektlar (personaj kapsulalari ham), markazga yaqini birinchi |
| `SphereCast3D(origin, radius, direction, maxDistance, ...)`, `BoxCast3D(center, halfExtents, direction, rotation, maxDistance, ...)`, `CapsuleCast3D(point0, point1, radius, direction, maxDistance, ...)` | "qalin nur": shakl yo'nalish bo'ylab suriladi — `Distance` shakl qancha yurgani, `Point` tegish nuqtasi, `Normal` sirt |
| `SphereCast3DAll`, `BoxCast3DAll`, `CapsuleCast3DAll` | yo'ldagi hammasi |

Muhim tafsilotlar:

- Overlap so'rovlari obyektning **o'zini ham** topadi (ichida bo'lsa) — kerak bo'lmasa tashlab keting.
- 3D nur yoki shakl collider ichidan boshlansa uni urmaydi: o'z tanasi ichidan otilgan nur uni o'tkazib yuboradi.
  Mesh Collider 3D faqat old tomonidan uriladi.
- Bir xil funksiyalar `Scene` da ham bor (`GetScene().Raycast2D(...)`, `GetScene().OverlapSphere3D(...)`) — skriptdan
  tashqaridagi kod uchun.

Portlash (Platformer2D `BlastOnClick`): sichqoncha bosilgan joydagi jismlarni topib, markazdan uzoqqa itaradi:

```cpp
void Blast(const glm::vec2& center)
{
    for (Entity e : OverlapCircle2D(center, Radius))
    {
        glm::vec2 d = glm::vec2(e.GetComponent<TransformComponent>().Translation) - center;
        const float distance = glm::length(d);
        d = distance > 1e-3f ? d / distance : glm::vec2(0.0f, 1.0f);
        const float falloff = std::clamp(1.0f - distance / Radius, 0.2f, 1.0f);   // yaqinroq - kuchliroq
        ApplyImpulse(e, (d + glm::vec2(0.0f, 0.35f)) * Strength * falloff);
    }
    DebugDraw::Circle({ center, 0.5f }, Radius, glm::vec4(1.0f, 0.6f, 0.25f, 0.9f), 0.9f, false);
}
// Chaqiriq: if (Input::IsActionJustPressed("Fire")) Blast(GetMouseWorldPosition());
```

3D yerni tekshirish (`ScriptableEntity.h` dagi misol) va dushmanni nishonga olish:

```cpp
RaycastHit ground = SphereCast3D(GetWorldPosition() + glm::vec3(0, 0.5f, 0), 0.3f, { 0, -1, 0 }, 1.0f);
if (RaycastHit hit = Raycast3D(GetWorldPosition(), Transform().GetForward(), 20.0f, LayerMask({ "Enemy" })))
    SendMessage(GetScene().GetEntityByUUID(hit.EntityID), "Damage", 25.0f);
```

Namunalar: Showcase3D `Scenes/Queries3D.zscene` — `SphereSweep3D` (SphereCast3DAll, "Arvoh" qatlami niqobdan chiqarilgan),
`OverlapGlow3D` (OverlapSphere3D ichidagilar yorishadi), `LaserAll3D` (Raycast3DAll); Platformer2D `Scenes/Effectors2D.zscene`
— `LaserScanner` (Raycast2DAll), `BlastOnClick`. Sichqoncha ostidagi nuqta uchun 3D'da `GetMouseRay()` + `Raycast3D`,
2D'da `GetMouseWorldPosition()` + `OverlapPoint2D`.

---

## 14. Uxlash

Bir muddat qimirlamay turgan jism — unga tegib yoki ulanib turganlar bilan birga (Box2D / Jolt orollari) — hisoblanmaydi,
toki biror narsa uni uyg'otmaguncha: urilish, joint tortishi, skript (`WakeUp`, `SetPosition2D`, kuch, impuls). Katta
sahnalarda bu juda ko'p vaqt tejaydi.

- Sahna: *Sahna sozlamalari > Fizika > Box2D / Jolt* — **Uxlash** (o'chirilsa hamma jism doim hisoblanadi) va
  **Uxlashgacha** (soniya, standart 0.5).
- Har jism (`Sleeping`, `SleepMode`): **Uyg'oq boshlanadi** (`StartAwake`), **Uxlab boshlanadi** (`StartAsleep` — urilguncha
  kutib turadigan minora), **Hech uxlamaydi** (`NeverSleep`).
- Inspektorda o'yin paytida holat va **Uyg'otish** / **Uxlatish** tugmalari; *Fizik colliderlar* ko'rinishida uxlayotgan
  jismlar ko'kish-kulrang.
- Skript: 2D `IsSleeping()`, `WakeUp()`, `PutToSleep()`, `IsSleeping(other)`, `WakeUp(other)`; 3D `IsSleeping3D()`,
  `WakeUp3D()`, `PutToSleep3D()` va `(other)` turlari. `Scene` da: `IsSleeping2D(entity)`, `IsSleeping3D(entity)`.

```cpp
// Showcase3D SleepTint3D: uxlayotgan jism ko'k bo'ladi.
void OnUpdate(Timestep) override
{
    if (auto* mr = TryGetComponent<MeshRendererComponent>())
        mr->Mat.Albedo = IsSleeping3D() ? SleepColor : m_Awake;
}
```

Olib tashlangan jism ustida uxlab yotganlar uyg'onadi va tushadi; qayta paydo bo'lgan jism ichidagilar ham uyg'onadi.

---

## 15. Debug ko'rinishlari

**Editorda:**

- **Fizik colliderlar** (viewport toolbar'i yoki *Ko'rinish* menyusi): barcha 2D / 3D collider'lar, Convex Hull va Mesh
  collider qirralari, tilemap qutilari (bir tomonlamalari pushti, strelka bilan), uxlayotgan jismlar ko'kish-kulrang.
- **Debug chizish** menyusi (toolbar'dagi hasharot tugmasi): *Raycastlar* (har so'rov: nur, tegish nuqtasi, normal,
  tartib raqami; tegmasa — qizil; ko'rsatish vaqti), *Kontakt nuqtalari* (yashil nuqta, sariq normal, kirish chuqurligi
  qizil chiziq; **Qiymatlar** — chuqurlik mm, 3D zarba tezligi va impulsi, joint kuchi va uzilish chegarasidan foizi),
  *Joint'lar* (ikkala langar, o'q; rangi uzilishga yaqinligiga qarab yashil → sariq → qizil, uzilmaydigani ko'k, uzilgani
  qizil X), *Personaj tezligi* va *Jismlar tezligi* (o'q va m/s).
- *Gizmolar* (ko'z tugmasining menyusi yoki *Ko'rinish > Gizmolar*): *Joint'lar (2D / 3D)* — o'chiq, tanlanganda yoki hammasi.

**Player'da:** loyiha sozlamalari *Debug* bo'limi yoki buyruq qatori: `--debug-raycasts`, `--debug-contacts`,
`--debug-contact-details`, `--debug-joints`, `--debug-velocities`, `--no-debug-draw` ([Runtime.md](Runtime.md#4-buyruq-qatori)).

**Skriptdan** (`Zamin/Debug/DebugDraw.h`) — o'z shakllaringiz va sozlamalar:

```cpp
DebugDraw::Line(from, to, DebugColor::Red);                       // bir kadr
DebugDraw::WireSphere(GetWorldPosition(), 2.0f, DebugColor::Cyan, 0.5f);   // 0.5 s ko'rinadi
DebugDraw::WireCapsule(p0, p1, 0.4f, DebugColor::Yellow);
DebugDraw::Text(GetWorldPosition() + glm::vec3(0, 2, 0), "Salom", DebugColor::White);
DebugDraw::GetSettings().ShowRaycasts = true;                     // o'yin to'xtaguncha
if (!DebugDraw::IsShowingRaycasts()) { /* o'zimiz chizamiz */ }
```

Shipping build'da (`-DZAMIN_SHIPPING=ON` yoki `-DZAMIN_DEBUG_DRAW=OFF`) hamma `DebugDraw` chaqiruvlari bo'sh inline
funksiyalar — skriptda qoldirish mumkin. Kontakt ma'lumoti vositalar va testlar uchun:
`PhysicsWorld3D::GetDebugContacts` (`PhysicsContactPoint`: normal, chuqurlik, zarba tezligi, impuls, yoshi, uxlash).
Namunalar: Showcase3D `Scenes/PhysicsDebug3D.zscene` (`PhysicsDebugView3D` — C / J / V tugmalari), `Scenes/DebugDraw.zscene`.

---

## 16. Determinizm

Bir xil sahna har safar **bit-bit bir xil** simulyatsiya beradi (2D va 3D): ikki marta ishga tushirilganda, faylga
saqlab o'qilgandan keyin va obyektlar boshqa tartibda yaratilgan bo'lsa ham. Jismlar, joint'lar va personajlar sahna
tartibida yaratiladi; editorning Play nusxasi (`Scene::Copy`) ierarxiya tartibida — fayldan o'qilgan sahna kabi, shuning
uchun editorda o'ynalgan va eksport qilingan o'yin bir xil harakat qiladi. Tekshiruv: `PhysicsDeterminism2D` va
`PhysicsDeterminism3D` testlari.

Determinizmni o'zingiz buzmaslik uchun: tasodifiy sonlar uchun `Random` ni seed bilan ishlating ([Core.md](Core.md#8-tasodifiy-sonlar-random)),
fizik qiymatlarni `OnUpdate` da o'zgaruvchan `ts` emas, qat'iy qadamga mos tarzda bering (kuch / tezlik), bitta funksiya
argumentlarida bir nechta tasodifiy son olmang (hisoblash tartibi kompilyatorga bog'liq — `BlastOnClick` dagi izohga qarang).

---

## 17. Namuna sahnalar

| Loyiha / sahna | Nima ko'rsatadi | Skriptlar |
|---|---|---|
| Platformer2D `Scenes/Level1.zscene` ... `Level3` | platformer: qahramon, tangalar (sensor), harakatlanuvchi platformalar | `PlayerController`, `Coin`, `MovingPlatform` |
| Platformer2D `Scenes/Shapes2D.zscene` | Edge yer, botiq Polygon ko'prik, polygon toshlar, muz va rezina materiallar, Platform Effector, "Ghost" qatlami | `ShapeSpawner`, `ShapeLauncher` |
| Platformer2D `Scenes/Effectors2D.zscene` | suv (Buoyancy), shamol (Area), Raycast2DAll, OverlapCircle2D | `LaserScanner`, `BlastOnClick` |
| Platformer2D `Scenes/Joints2D.zscene` | 5 xil 2D joint, uziladigan chiroq, motorli mashina | `CarController`, `JointMotorPingPong`, `BreakableJoint` |
| Platformer2D `Scenes/Ragdoll2D.zscene` | 2D ragdoll prefabi, uxlab turgan minora, sub-qadamlar | `RagdollSpawner` |
| Platformer2D `Scenes/Tilemap2D.zscene` | tilemap collision, bir tomonlama plitkalar | — |
| Showcase3D `Scenes/Physics3D.zscene` | 3D materiallar, trigger plita, uxlab turgan piramida, CCD, jismlarni itaradigan qahramon | `Launcher3D`, `SleepTint3D`, `Sweeper3D`, `Respawn3D`, `TriggerGlow3D`, `PatrolWalker` |
| Showcase3D `Scenes/Joints3D.zscene` | 5 xil 3D joint, qavariq qobiqlar | `SliderShuttle` |
| Showcase3D `Scenes/Ragdoll3D.zscene` | 3D ragdoll prefabi | `RagdollSpawner3D` |
| Showcase3D `Scenes/Queries3D.zscene` | 3D so'rovlar, Mesh Collider (yarim quvur), "Arvoh" qatlami | `SphereSweep3D`, `OverlapGlow3D`, `LaserAll3D` |
| Showcase3D `Scenes/PhysicsDebug3D.zscene` | kontakt / joint / tezlik ko'rinishlari | `PhysicsDebugView3D` |
| Adventure3D `Scenes/Island.zscene` | Character Controller, kinematik platforma, kinematik darvoza tabaqalari, sensor kristallar | `ThirdPersonController`, `MovingPlatform3D`, `Crystal` |

---

## 18. Ko'p uchraydigan muammolar

| Belgi | Sabab va yechim |
|---|---|
| Obyekt tushmaydi, to'qnashmaydi | Faqat collider bor — jism yo'q (2D: Rigidbody 2D qo'shing), jism `Static`, yoki `GravityScale` / `GravityFactor` 0. Fizika faqat Play / Simulate'da ishlaydi |
| Konsolda `'X': Rigidbody3D uchun collider yo'q (Box/Sphere/Capsule/Convex Hull/Mesh Collider 3D qo'shing)` | 3D jismga shakl kerak |
| `'X': Mesh Collider 3D faqat statik yoki kinematik jismda ishlaydi - dinamik jism uni hisobga olmaydi` | Dinamik jismga Convex Hull Collider 3D yoki oddiy shakllar qo'ying |
| `'X' obyektidagi ... ulangan jismi (...) sahnada yo'q - joint yaratilmaydi` | Joint'ning `ConnectedBody` si o'chirilgan yoki boshqa sahnada; inspektorda qayta tanlang |
| `Convex Hull Collider 3D qurilmadi` / `Mesh Collider 3D qurilmadi` | Mesh Renderer'da model / primitiv yo'q yoki model fayli o'qilmadi (sabab xabarda) |
| Qahramon ag'dariladi | Rigidbody 2D **Aylanmasin** yoki Rigidbody 3D `LockRotation`; 3D'da Character Controller 3D yaxshiroq |
| Tez jism devordan o'tib ketadi | 2D `Bullet`, 3D **Uzluksiz (CCD)**; yupqa devorni qalinroq qiling yoki qadam chastotasini oshiring |
| Uzun zanjir / ragdoll cho'ziladi | *Sahna sozlamalari > Fizika > Sub-qadamlar* 2 ... 4, iteratsiyalarni oshiring; ulangan jismlar massasi juda farq qilmasin |
| `OnCollisionBegin` kelmaydi | Collider sensor — `OnTriggerBegin` keladi; yoki to'qnashuv qatlamlari matritsasi bu juftlikni o'chirgan; yoki jism Static + Static |
| Raycast hech narsani topmaydi | `maxDistance` kichik, niqobda kerakli qatlam yo'q, nur collider ichidan boshlangan (3D), maqsad trigger (`includeTriggers` bering), yoki tahrir rejimi |
| Dinamik jismni `Transform` bilan surish ishlamaydi | Fizika transformni qayta yozadi — `SetPosition2D` / `SetPosition3D`, tezlik yoki kinematik tur ishlating |
| Qahramon devorga yopishib qoladi (2D) | Platform Effector 2D'da yon tomon ishqalanishini o'chiring yoki ishqalanishi 0 bo'lgan material |

---

## 19. Testlar

Fizika unit testlari `Tests/ZaminTests.cpp` da (headless, GPU'siz). Faqat ularni ishga tushirish:

```bash
./build/bin/ZaminTests "*Physics*:*Joint*:*Collider*:*Trigger*:*Overlap*:*Raycast*:*Character*"
```

Asosiylari: `PhysicsGravityAndCollision`, `Physics3D`, `TriggerCallbacks2D`, `TriggerCallbacks3D`, `TriggerEndWhenDestroyed`,
`Overlap2DQueries`, `Raycast2DAllOrdering`, `Overlap3DQueries`, `Raycast3DAllOrdering`, `Physics2DStepSettingsAndSubSteps`,
`Physics2DSleeping`, `Physics3DSleepingAndWakeUp`, `Physics2DScriptForcesAcrossSteps`, `PhysicsMaterialCombineAndJson`,
`CollisionLayers2DMatrixAndFiltering`, `CollisionLayers3DMatrixAndFiltering`, `Polygon2DDecomposition`,
`AreaEffector2DPushesBodies`, `BuoyancyEffector2DFloats`, `Joint2DBreakForceAndScriptCallback`,
`Joint3DBreakForceAndScriptCallback`, `ConeJoint3DSwingTwistAndFriction`, `MeshCollider3DStaticAndCached`,
`CharacterController3D`, `CharacterController3DPushesBodies`, `PhysicsDeterminism2D`, `PhysicsDeterminism3D`. Barcha testlar
katalogi: [Testing.md](Testing.md).

---

## 20. Qisqa API ma'lumotnomasi

| Nima | API |
|---|---|
| Jismlar | `Rigidbody2DComponent` (`Type`, `FixedRotation`, `GravityScale`, `Bullet`, `Layer`, `Sleeping`), `Rigidbody3DComponent` (`Type`, `Mass`, `GravityFactor`, `LockRotation`, `ContinuousCollision`, `Layer`, `Sleeping`) |
| 2D collider'lar | `BoxCollider2DComponent`, `CircleCollider2DComponent`, `PolygonCollider2DComponent`, `EdgeCollider2DComponent`; `Polygon2D::Decompose` |
| 3D collider'lar | `BoxCollider3DComponent`, `SphereCollider3DComponent`, `CapsuleCollider3DComponent`, `ConvexHullCollider3DComponent`, `MeshCollider3DComponent` |
| Effektorlar | `PlatformEffector2DComponent`, `AreaEffector2DComponent`, `BuoyancyEffector2DComponent` |
| Joint'lar | `DistanceJoint2DComponent`, `RevoluteJoint2DComponent`, `PrismaticJoint2DComponent`, `WeldJoint2DComponent`, `WheelJoint2DComponent`; `FixedJoint3DComponent`, `HingeJoint3DComponent`, `DistanceJoint3DComponent`, `SliderJoint3DComponent`, `ConeJoint3DComponent` |
| Personaj | `CharacterController3DComponent::Move`, `Jump`, `Grounded`, `Velocity`, `VerticalVelocity`, `PushBodies`, `PushForce` |
| Harakat (skript) | `GetVelocity`, `SetVelocity`, `ApplyImpulse`, `ApplyForce`, `SetPosition2D`, `DropThroughPlatforms`; `GetVelocity3D`, `SetVelocity3D`, `GetAngularVelocity3D`, `SetAngularVelocity3D`, `ApplyImpulse3D`, `ApplyForce3D`, `ApplyTorque3D`, `SetPosition3D` |
| Uxlash | `IsSleeping`, `WakeUp`, `PutToSleep`, `IsSleeping3D`, `WakeUp3D`, `PutToSleep3D`; `Scene::IsSleeping2D`, `Scene::IsSleeping3D` |
| Callback'lar | `OnCollisionBegin`, `OnCollisionEnd`, `OnTriggerBegin`, `OnTriggerEnd`, `OnJointBreak` (`JointBreak2D`), `OnJointBreak3D` (`JointBreak3D`) |
| So'rovlar 2D | `Raycast2D`, `Raycast2DAll`, `OverlapCircle2D`, `OverlapBox2D`, `OverlapPoint2D` |
| So'rovlar 3D | `Raycast3D`, `Raycast3DAll`, `OverlapSphere3D`, `OverlapBox3D`, `OverlapCapsule3D`, `SphereCast3D`, `BoxCast3D`, `CapsuleCast3D` (+ `...All`); `RaycastHit` (`Hit`, `EntityID`, `Point`, `Normal`, `Distance`) |
| Qatlamlar | `LayerMask({ ... })`, `LayerIndex(name)`; `CollisionLayers2D` (`Names`, `Masks`, `CanCollide`, `SetCollision`, `MakeMask`, `ApplyPreset`) |
| Materiallar | `PhysicsMaterialData`, `PhysicsCombine`, `PhysicsMaterial::ToJson` / `FromJson` / `Save` / `Load`, `PhysicsMaterialLibrary::Load` / `Resolve` |
| Sozlamalar | `SceneSettings::Gravity`, `Gravity3D`, `PhysicsStep` (`StepRate`, `MaxStepsPerFrame`), `Physics2D` (`SubSteps`, `VelocityIterations`, `PositionIterations`, `AllowSleep`, `TimeToSleep`), `Physics3D`; `PhysicsDefaults` (loyiha gravitatsiyasi) |
| Statistika | `Scene::GetPhysics2DStats()`, `GetPhysics3DStats()`, `GetPhysics2DStepsLastFrame()` |
| Debug | `DebugDraw::Line`, `WireSphere`, `WireCapsule`, `Circle`, `Text`, `GetSettings()`, `SetEnabled`, `IsShowingRaycasts`, `IsShowingContacts`; `PhysicsWorld3D::GetDebugContacts` |
