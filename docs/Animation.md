# Zamin Engine: animatsiya

Bu hujjat Zamin'dagi animatsiya tizimini tushuntiradi: property kliplar va Animatsiya oynasi (timeline), skelet
animatsiyasi va Animator (holat mashinasi, blend tree, qatlamlar), event'lar, root motion, suyakka biriktirish,
morph target'lar, sprite animatsiyasi va kliplar, tint (rang) animatsiyasi, skript tween'lari, AnimationCurve va
spline yo'llar, samaradorlik (keshlash, kesish), testlar va ko'p uchraydigan muammolar. Har bo'limda C++ skript
misollari bor (`#include <Zamin/Zamin.h>`, `using namespace Zamin;`). Komponentlarning to'liq maydonlari —
[Components.md](Components.md#2-render); UI animatsiyalari — [UI.md](UI.md#10-ui-animatsiyalari); particle'larning
tekstura sheet (flipbook) moduli — [Rendering.md](Rendering.md#13-particle-tizimi).

![Animatsiya oynasi: darvoza klipi](images/animation-timeline-editor.jpg)

*Adventure3D `Darvoza` tanlangan: pastda **Animatsiya** oynasi (treklar, kalitlar, event'lar qatori, klip sozlamalari),
inspektorda Animation Player va `ProximityDoor` skripti.*

Mundarija:

1. [Ikki xil animatsiya](#1-ikki-xil-animatsiya)
2. [Property kliplar (.zanim)](#2-property-kliplar-zanim)
3. [Animatsiya oynasi (timeline)](#3-animatsiya-oynasi-timeline)
4. [Animation Player va skriptlar](#4-animation-player-va-skriptlar)
5. [Skelet animatsiyasi va Animator](#5-skelet-animatsiyasi-va-animator)
6. [Holat mashinasi va Animator grafi](#6-holat-mashinasi-va-animator-grafi)
7. [Blend tree 1D](#7-blend-tree-1d)
8. [Qatlamlar va avatar maska](#8-qatlamlar-va-avatar-maska)
9. [Event'lar](#9-eventlar)
10. [Root motion](#10-root-motion)
11. [Suyakka biriktirish](#11-suyakka-biriktirish)
12. [Morph target'lar (blend shape'lar)](#12-morph-targetlar-blend-shapelar)
13. [glTF kliplarini kesish va bo'lish](#13-gltf-kliplarini-kesish-va-bolish)
14. [Bir nechta skin](#14-bir-nechta-skin)
15. [Sprite animatsiyasi va kliplar (.zsanim)](#15-sprite-animatsiyasi-va-kliplar-zsanim)
16. [Samaradorlik: keshlash va kesish](#16-samaradorlik-keshlash-va-kesish)
17. [Namuna: yaqinlashganda ochiladigan darvoza](#17-namuna-yaqinlashganda-ochiladigan-darvoza)
18. [AnimationCurve: skriptdagi egri chiziq](#18-animationcurve-skriptdagi-egri-chiziq)
19. [Spline yo'llar va Spline Follower](#19-spline-yollar-va-spline-follower)
20. [Tint animatsiyasi (rang)](#20-tint-animatsiyasi-rang)
21. [Skript tween'lari](#21-skript-tweenlari)
22. [UI animatsiyalari](#22-ui-animatsiyalari)
23. [Namuna sahnalar](#23-namuna-sahnalar)
24. [Ko'p uchraydigan muammolar](#24-kop-uchraydigan-muammolar)
25. [Testlar](#25-testlar)
26. [Qisqa API ma'lumotnomasi](#26-qisqa-api-malumotnomasi)

---

## 1. Ikki xil animatsiya

| | Property animatsiya | Skelet animatsiyasi |
|---|---|---|
| Nima o'zgaradi | Obyekt va bolalarining xususiyatlari: transform, rang, yorug'lik, UI, skript maydonlari | glTF modelning suyaklari (GPU skinning) va blend shape'lari |
| Klip qayerda | `.zanim` fayl (Animatsiya oynasida yaratiladi) | glTF fayl ichida (`.gltf` / `.glb`) |
| Komponent | **Animation Player** | **Animator** (Mesh Renderer'dagi model bilan) |
| Misol | eshik, mayoq, ko'tarilgan platforma, UI panel | yurish / yugurish, qo'l silkitish, yuz ifodasi |

Ikkalasida ham **event'lar** bor (klipning ma'lum nuqtasida skriptga xabar), ikkalasi ham editorda **play rejimisiz**
ko'rib chiqiladi (Animatsiya oynasida vaqtni surib).

Uchinchisi — 2D uchun **sprite animatsiyasi** (sprite sheet kadrlari, `.zsanim` kliplar), 15-bo'limda.

**Qaysi vositani tanlash:**

| Vazifa | Vosita | Bo'lim |
|---|---|---|
| Eshik, lift, mayoq, sahnadagi "kesma" — oldindan chizilgan harakat | Animation Player + `.zanim` | [2](#2-property-kliplar-zanim)-[4](#4-animation-player-va-skriptlar) |
| Personaj yurishi, yugurishi, sakrashi (glTF) | Animator (+ holat mashinasi, blend tree) | [5](#5-skelet-animatsiyasi-va-animator)-[8](#8-qatlamlar-va-avatar-maska) |
| 2D personaj kadrlari | Sprite Animation (+ `.zsanim`) | [15](#15-sprite-animatsiyasi-va-kliplar-zsanim) |
| Miltillash, rang pulsatsiyasi (2D) | Tint Animation | [20](#20-tint-animatsiyasi-rang) |
| Koddan "shu yerga 0.3 s da bor", "so'n" | `TweenPosition`, `TweenScale`, `TweenColor`, `FadeTo`, `Tween` | [21](#21-skript-tweenlari) |
| Menyu paydo bo'lishi, tugma "sakrashi" | UI Animation, `UIScaleTo` ... | [22](#22-ui-animatsiyalari) |
| Yo'l bo'ylab yurish (platforma, poyezd) | Spline + Spline Follower | [19](#19-spline-yollar-va-spline-follower) |
| Skript qiymati vaqt bo'yicha (sakrash balandligi) | `AnimationCurve` | [18](#18-animationcurve-skriptdagi-egri-chiziq) |

---

## 2. Property kliplar (.zanim)

Klip — **treklar** ro'yxati. Har trek bitta obyektning bitta xususiyatini animatsiya qiladi:

- **Yo'l** (`Path`) — Animation Player turgan obyektdan boshlab bolalar nomlari, `/` bilan: `""` — obyektning o'zi,
  `"Oshiq"` — uning `Oshiq` nomli bolasi, `"Eshik/Oshiq/Tutqich"` — chuqurroq. Shu sababli bitta klip bir xil tuzilishdagi
  har qanday obyektda ishlaydi (prefab nusxalari ham).
- **Xususiyat** (`Property`):

  | Fayldagi nomi | Nima | Komponentlar |
  |---|---|---|
  | `Transform.Position` | lokal joylashuv | 3 |
  | `Transform.Rotation` | lokal burilish (**gradus**) | 3 |
  | `Transform.Scale` | masshtab | 3 |
  | `SpriteRenderer.Color`, `CircleRenderer.Color`, `Text.Color`, `MeshRenderer.Color` | rang (RGBA) | 4 |
  | `Light.Color`, `Light.Intensity` | obyektdagi birinchi yorug'lik (nuqtali, spot, directional yoki 2D) | 3 / 1 |
  | `UITransform.Position`, `UITransform.Size`, `UITransform.Scale`, `UITransform.Alpha` | UI elementi | 2 / 2 / 2 / 1 |
  | `UIImage.Color` | UI rasm rangi | 4 |
  | `Script.Float` | skriptning float maydoni (`ZM_FIELD`, trekning `Field`i — maydon nomi) | 1 |

  `MeshRenderer.Color` material asset ishlatilsa shu renderer'ning o'z nusxasiga yoziladi (boshqa obyektlar o'zgarmaydi).

- **Kalitlar** (`Keys`): vaqt (soniya), qiymat va keyingi kalitgacha **interpolyatsiya**:
  - `Step` — keyingi kalitgacha qiymat o'zgarmaydi (chiroq yoqildi / o'chdi);
  - `Linear` — to'g'ri chiziq;
  - `Bezier` — silliq egri chiziq. Tangentlar: **Auto** (avtomatik silliq: cho'qqida tekis — oshib ketmaydi, chetlarda
    sekinlashib boshlanadi / tugaydi, o'suvchi qatorda monoton), **Flat** (tekis) yoki **Free** (qo'lda: `InTangent` /
    `OutTangent` — soniyasiga qiymat). Ikki kalit orasidagi egri chiziq — `a`, `a + a.Out·dt/3`, `b − b.In·dt/3`, `b`
    nuqtali kubik Bezier.

Klipning o'zi: davomiyligi (`Duration`), kadr tezligi (`FrameRate` — faqat timeline kadrlari uchun), tugash rejimi
(`Wrap`: `Once` — bir marta va oxirgi qiymatlarda qoladi, `Loop` — takror, `PingPong` — borib-qaytish) va event'lar.

Fayl ko'rinishi (qisqartirilgan; odatda Animatsiya oynasi yozadi):

```json
{
  "Version": 1, "Duration": 1.6, "FrameRate": 30, "Wrap": "Once",
  "Tracks": [
    { "Path": "ChapOshiq", "Property": "Transform.Rotation",
      "Keys": [ { "Time": 0,   "Value": [0, 0, 0],   "Interpolation": "Bezier" },
                { "Time": 1.3, "Value": [0, 104, 0], "Interpolation": "Bezier" },
                { "Time": 1.6, "Value": [0, 100, 0], "Interpolation": "Bezier" } ] },
    { "Path": "Chiroq", "Property": "Light.Color",
      "Keys": [ { "Time": 0,   "Value": [1, 0.15, 0.1], "Interpolation": "Step" },
                { "Time": 1.3, "Value": [0.2, 1, 0.35], "Interpolation": "Step" } ] }
  ],
  "Events": [ { "Time": 0.05, "Name": "Gichirlash", "Parameter": "ochilish" } ]
}
```

Kliplar kontent brauzerda **Animation** asset turi sifatida ko'rinadi (o'ng tugma → **Yangi animatsiya klipi**), fayl
tashqarida o'zgartirilsa o'ynab turgan obyektlarda darhol yangilanadi.

---

## 3. Animatsiya oynasi (timeline)

**Oyna > Animatsiya**. Animation Player'li obyekt tanlansa uning klipi ochiladi (`.zanim` ni kontent brauzerda ikki marta
bosish ham shu).

**Birinchi klip — qadam-baqadam** (masalan ko'tariladigan platforma):

1. Obyektni tanlang, **Oyna > Animatsiya**. Unda Animation Player bo'lmasa oynadagi **"Platforma" ga Animation Player va yangi
   klip** tugmasi komponentni qo'shadi va `Assets/Animations/Platforma.zanim` ni yaratadi (yoki kontent brauzerda o'ng tugma →
   **Yangi animatsiya klipi** va uni Animation Player'ning `Clips` ro'yxatiga qo'shing).
2. **+ Trek qo'shish** → obyekt (o'zi yoki bolasi) → `Transform.Position`.
3. **Yozish** (●) ni yoqing. Vaqt chizg'ichida 0 ni bosing — platforma joyida turgan holatda kalit qo'ying (ikki marta
   bosish); vaqtni 2 s ga suring va platformani gizmo bilan yuqoriga ko'taring — kalit o'zi qo'yiladi.
4. Klip sozlamalarida tugash rejimini `PingPong` qiling, ▶ — sahna ko'rinishida play rejimisiz ko'rib chiqing.
5. `Ctrl+S` (klipni saqlash). Animation Player'da **Avto boshlash** yoqiq bo'lsa o'yinda o'zi o'ynaydi; skriptdan —
   `PlayAnimation("Platforma")` (4-bo'lim).

Kinematik jismni (Rigidbody 2D / 3D `Kinematic`) shunday animatsiya qilsangiz ustida turgan qahramon birga ko'tariladi
([Physics.md](Physics.md#1-asosiy-tushunchalar)).

- **Treklar ro'yxati**: `+ Trek qo'shish` — obyekt (yo'l avtomatik) va uning xususiyatlari ro'yxatidan.
- **Dope sheet**: kalitlar romblar; sudrab ko'chirish, ikki marta bosish — yangi kalit, `Delete` — o'chirish, o'ng tugma —
  interpolyatsiya va tangent turi. Yuqorida **event'lar qatori** (event qo'shish, sudrash, nomlash).
- **Egri chiziqlar** (toolbar tugmasi): tanlangan trekning har komponenti alohida chiziq, kalitlar va Bezier tutqichlari
  sudraladi.
- **Vaqt chizg'ichi**: bosib / sudrab **scrub**; g'ildirak — masshtab, surish; **kadrlarga yopishish**.
- **Play rejimisiz ko'rib chiqish**: vaqt surilganda yoki ▶ bosilganda sahna ko'rinishida klip qo'llanadi. Oyna yopilsa,
  sahna saqlansa, undo qilinsa yoki Play bosilsa — sahnaning o'z qiymatlari qaytadi (ko'rib chiqish sahnani buzmaydi).
- **Yozish** (●): yoqilganda inspektor yoki gizmo bilan qilingan har o'zgarish joriy vaqtda kalit bo'ladi (xususiyatning
  treki bo'lmasa — yangi trek qo'shiladi). Obyektni sudrab, vaqtni surib, yana sudrab — animatsiya tayyor.
- Oynaning o'z undo/redo'si bor (oyna fokusda: `Ctrl+Z` / `Ctrl+Y`), `Ctrl+S` — klipni saqlash.

**Skelet rejimi**: Animator'li (Animation Player'siz) obyekt tanlansa oyna glTF kliplarini ko'rsatadi — klip tanlash,
scrub (sahnada model shu pozada), o'ynatish, takror, tezlik; klip harakatlantiradigan suyaklar va blend shape'lar qatorlari
(qator bosilsa — shu vaqtdagi qiymatlari); **Animator event'larini** shu yerda qo'shish va sudrash (sahnaning undo'si bilan).

---

## 4. Animation Player va skriptlar

**Animation Player** komponenti: kliplar ro'yxati (`Clips`, Assets'ga nisbatan yo'llar), tezlik (`Speed`), **Avto
boshlash** (`PlayOnStart` — birinchi klip o'yin boshlanganda), tugash rejimi (`Wrap`: klipdagidek yoki Once / Loop /
PingPong). To'xtab turgan player hech narsa yozmaydi — skript o'sha qiymatlarni bemalol o'zgartiradi.

```cpp
class Lift : public ScriptableEntity
{
public:
    void OnUpdate(Timestep) override
    {
        if (Input::IsKeyPressed(Key::E) && !IsAnimationPlaying())
            PlayAnimation("LiftUp");               // boshidan oldinga
        if (Input::IsKeyPressed(Key::Q) && !IsAnimationPlaying())
            PlayAnimation("LiftUp", -1.0f);        // oxiridan orqaga
    }
};
ZM_REGISTER_SCRIPT(Lift)
```

Komponentning o'zi bilan ko'proq imkoniyat:

```cpp
auto& player = GetComponent<AnimationPlayerComponent>();
player.Play("Pulse");                 // nom (fayl nomi .zanim'siz) yoki yo'l
player.PlayFrom("DoorOpen", player.GetTime(), -1.0f); // turgan joyidan orqaga (yarim ochiq eshik yopiladi)
player.Stop();                        // joyida to'xtaydi, qiymatlar qoladi
player.Resume();
player.SetTime(0.5f);                 // to'xtab turganda ham: keyingi yangilanishda yoziladi
float t = player.GetNormalizedTime(); // 0..1
```

`PlayFrom` — `Play` dan farqli, klipni boshidan emas, berilgan vaqtdan boshlaydi va aynan shu vaqtdagi event'larni
qayta chaqirmaydi (u yerda allaqachon turibdi).

Player fizika va `OnUpdate` dan keyin, `OnLateUpdate` dan oldin yangilanadi: kamera kuzatuvchi skriptlar animatsiya qilingan
joyni ko'radi.

---

## 5. Skelet animatsiyasi va Animator

Mesh Renderer'da skinned glTF model va **Animator** komponenti bo'lsa model animatsiya qilinadi (GPU skinning, 80 tagacha
suyak bir skin'da, soyalar ham).

- `Clip` — o'ynaydigan klip (`""` — birinchisi), `Speed`, `Loop`, `Playing`;
- **Editorda ko'rsatish** (`PlayInEditor`) — edit rejimida ham o'ynaydi;
- **O'tish vaqti** (`DefaultFade`) — `Play` bergan crossfade davomiyligi.

```cpp
auto& anim = GetComponent<AnimatorComponent>();
anim.Play("Run", true, 1.0f, 0.25f);   // klip, loop, tezlik, crossfade (soniya; < 0 — DefaultFade, 0 — darhol)
anim.Speed = 1.5f;
float n = anim.GetNormalizedTime(GetComponent<MeshRendererComponent>().ModelAsset.get()); // 1 = bir sikl
```

Kliplar orasida **crossfade**: oldingi klip vaqti davom etadi va pozalar silliq (smoothstep) aralashadi.

---

## 6. Holat mashinasi va Animator grafi

Animator'da holatlar bo'lsa klipni **holat mashinasi** tanlaydi:

- **Parametrlar** — `Float`, `Bool`, `Trigger` (shart bajarilganda o'zi o'chadi);
- **Holatlar** — klip (yoki blend tree), tezlik (ixtiyoriy tezlik parametri bilan ko'paytiriladi), takror;
- **O'tishlar** — `From` → `To` (`From = -1`: **har qanday holatdan**), crossfade (`Fade`), shartlar (`Greater`, `Less`,
  `IsTrue`, `IsFalse`, `Triggered`) va ixtiyoriy **exit time** (normalizatsiyalangan vaqt, masalan `1.0` — klip tugagach).

```cpp
void OnUpdate(Timestep) override
{
    auto& anim = GetComponent<AnimatorComponent>();
    anim.SetFloat("Speed", glm::length(m_Velocity));
    anim.SetBool("Grounded", cc.Grounded);
    if (Input::IsActionJustPressed("Attack"))
        anim.SetTrigger("Attack");
    if (std::string(anim.GetCurrentStateName()) == "Dead")
        return;
}
// To'g'ridan-to'g'ri holatga: anim.SetState("Jump", 0.1f);
```

**Oyna > Animator grafi** (yoki inspektordagi tugma): holatlar node'lar (boshlang'ich holat to'q sariq), o'tishlar strelkalar,
"Har qanday holat" node'i. Node'larni sudrash (joylari sahnada saqlanadi), **Shift + sudrash** bilan o'tish chizish, bosib
tanlash va o'ng panelda tahrirlash (shartlar, fade, exit time, blend motion'lari), `Delete`, g'ildirak — masshtab, o'rta /
o'ng tugma — surish; hammasi undo bilan. O'yin davomida jonli: hozirgi holat va vaqti, o'tish strelkasi to'q sariq, blend
og'irliklari. Inspektorda **Holat (jonli)** bloki — normalizatsiyalangan vaqt, o'tish jarayoni, og'irliklar va o'yin
davomida o'zgartirsa bo'ladigan parametrlar (trigger tugmasi bilan).

---

## 7. Blend tree 1D

Holat turi **Blend tree 1D**: float parametr (masalan `Speed`) bo'yicha bir nechta klip aralashadi. Har motion — klip,
**chegara** (threshold) va tezlik:

| Motion | Chegara |
|---|---|
| Idle | 0 |
| Walk | 3 |
| Run | 6.5 |

`Speed = 4.75` bo'lsa Walk va Run teng (0.5 / 0.5) aralashadi; eng pastdan pastda / eng yuqoridan yuqorida — o'sha klip
yolg'iz. Chegaralar istalgan tartibda bo'lishi mumkin.

Hamma kliplar **bitta normalizatsiyalangan vaqtda** o'ynaydi: Walk 1 s, Run 0.7 s bo'lsa ham ularning qadamlari fazada
(oyoq bir vaqtda yerga tegadi). Sikl davomiyligi — kliplar davomiyliklarining og'irliklar bo'yicha o'rtachasi. Blend tree
boshqa holatlarga crossfade, exit time, root motion (og'irliklar bo'yicha) va event'lar (eng og'ir klipdan) bilan ishlaydi.

Chegaralarni yurish tezligiga moslang (Adventure3D robotida 3 va 6.5 — skriptdagi `WalkSpeed` / `RunSpeed`): oyoqlar
yerda sirpanmaydi.

```cpp
anim.SetFloat("Speed", speed);   // shuning o'zi yetarli
// Og'irliklar (inspektor, debug): anim.BlendWeights[i] — GetBlendTreeState()->Motions[i] uchun
```

---

## 8. Qatlamlar va avatar maska

**Qatlam** — asosiy animatsiya ustidan o'z klipini tananing bir qismida o'ynaydi (yuradi va qo'l silkitadi):

- `Clip`, `Speed`, `Loop`, `Weight` (0..1; ixtiyoriy `WeightParameter` — float parametr ham ko'paytiradi);
- `Blend`: **Override** (maskadagi suyaklar shu klip pozasiga og'irlik bo'yicha o'tadi) yoki **Additive** (klipning o'z
  birinchi kadriga nisbatan o'zgarishi qo'shiladi — bosh irg'ash, nafas olish);
- `MaskBones` — **avatar maska**: sanab o'tilgan suyaklar va ulardan pastdagi hammasi (masalan `Spine` — ustki tana).
  Bo'sh — butun tana.

```cpp
auto& anim = GetComponent<AnimatorComponent>();
anim.PlayLayer("UpperBody", "Wave");            // qatlamda klip (crossfade bilan; bo'sh qatlam asta paydo bo'ladi)
anim.SetLayerWeight("UpperBody", 0.0f, 0.3f);   // 0.3 s da o'chadi
float w = anim.GetLayerWeight("UpperBody");
```

Qatlamlar sahnada saqlanadi, o'yin boshlanganda o'z og'irliklaridan boshlaydi. Qatlam klipining event'lari ham keladi
(faqat shu klip nomi yozilganlari).

---

## 9. Event'lar

Event — klipning bir nuqtasi: **nom** va ixtiyoriy **parametr** matni. O'sha nuqtadan o'tilganda skript xabar oladi.

- Property kliplarda: vaqt **soniyada**, Animatsiya oynasining event'lar qatorida.
- glTF kliplarda: Animator > **Hodisalar** (yoki skelet rejimidagi timeline): klip nomi (`""` — har qanday klip) va vaqt
  **0..1** (klip boshidan oxirigacha).

Kafolatlar: har event o'tilganda **aynan bir marta** chaqiriladi — takrorlanganda (loop) har siklda, borib-qaytganda ikkala
yo'nalishda, orqaga o'ynaganda ham. Bir kadr butun sikldan uzun bo'lsa (qotish) ham har event bir martadan ortiq emas. Klip
tugagan nuqtaga tushgan qadam oxirgi event'ni chaqiradi, keyingi siklning boshi keyingi qadamda keladi. Editorda (play
rejimisiz) event'lar chaqirilmaydi.

Skriptda qabul qilish — ikki yo'l:

```cpp
class Hero : public ScriptableEntity
{
public:
    void OnCreate() override
    {
        // 1) Nom bo'yicha usul
        AddAnimationEventHandler("Hit", [this](const AnimationEventInfo& e) { DealDamage(e.Parameter); });
    }
    // 2) Hammasi bitta joyda
    void OnAnimationEvent(const AnimationEventInfo& e) override
    {
        ZM_INFO("{} klipida '{}' (t={:.2f}, skelet: {})", e.Clip, e.Name, e.Time, e.Skeletal);
    }

private:
    void DealDamage(const std::string& weapon) { ZM_INFO("Zarba: {}", weapon); }
};
```

Event'lar sahnaning event bus'iga ham `"AnimationEvent"` nomi bilan chiqadi (boshqa obyektlar `Subscribe` qilishi mumkin).

**Qadam tovushlari** — `Footsteps` komponenti event'larni o'zi tinglaydi (kod kerak emas): `EventName` (odatda
`Footstep`) kelganda ovozlar ro'yxatidan tasodifiysi (ketma-ket takrorlanmaydi), tasodifiy balandlik / ton, Animator'ning
tezlik parametri bo'yicha (`ReferenceSpeed`da — 1; tezroq — balandroq va yuqoriroq), `MinSpeed`dan sekinda jim, yaqin
event'lar (`MinInterval`) bitta qadam, 3D ovoz va mixer guruhi. Walk / Run kliplarining 0 va 0.5 nuqtalariga `Footstep`
event'i qo'ying — tamom (Adventure3D roboti shunday).

---

## 10. Root motion

Ko'p kliplar (mocap yurishi) ildiz suyakni oldinga siljitadi: Animator bunday klipni joyida o'ynasa model obyektdan
uzoqlashib ketadi va keyin orqaga sakraydi. **Root motion** (Animator > Root motion > **Qo'llash**) ildiz suyakning XZ
yurishini va Y atrofida burilishini olib, **obyektni** siljitadi; poza joyida qoladi (balandligi — sakrash, chayqalish —
qoladi).

- Ildiz suyak — avtomatik (klip siljitadigan eng yuqori suyak, odatda Hips / Root) yoki tanlanadi (`RootMotionBone`).
- **Character Controller 3D** bo'lsa u yuradi (to'qnashuvlar, zinapoyalar bilan), bo'lmasa transform siljiydi.
- Loop'lar, crossfade va blend tree (og'irliklar bo'yicha) hisobga olinadi; editor ko'rinishida model joyida turadi.
- Skript o'qishi mumkin: `anim.RootMotionVelocity` (m/s), `anim.RootMotionTurnRate` (radian/s).

```cpp
auto& anim = GetComponent<AnimatorComponent>();
anim.ApplyRootMotion = true;
anim.RootMotionBone = "Hips";
```

---

## 11. Suyakka biriktirish

**Bone Attachment** komponenti obyektni skinned modelning suyagiga ergashtiradi — qo'lda qilich, boshda shlyapa, belda
fonar:

- `Target` — Animator'li model obyekti (bo'sh — eng yaqin skinned ota-ona);
- `Bone` — suyak nomi (inspektorda ro'yxatdan);
- `InheritBoneScale` — suyak masshtabini ham olish.

Suyak (Animator pozasi bilan, editorda ham) obyekt otasining o'rnini egallaydi, obyektning Transform'i esa **suyakka
nisbatan siljish** bo'ladi (gizmo ham suyak fazosida ishlaydi). "Suyakka qo'yish (offset 0)" tugmasi siljish va burilishni nolga qaytaradi.
Joylashuv har kadr animatsiyadan keyin olinadi, shuning uchun fizika va kamera to'g'ri joyni ko'radi.

```cpp
glm::mat4 hand = GetScene().GetWorldTransform(sword);   // suyakdagi obyektning dunyo transformi
```

---

## 12. Morph target'lar (blend shape'lar)

glTF morph target'lari o'qiladi: har target'ning joy va normal siljishlari, nomlari va boshlang'ich og'irliklari. Har
og'irlik 0..1 — mesh shu target tomon o'zgaradi (yuz ifodasi, shishish, tikanlar).

- Mesh Renderer inspektorida har target uchun slayder; og'irliklar sahnada saqlanadi (`MorphWeights`).
- glTF'dagi `weights` animatsiya kanallari Animator kliplarida o'ynaydi (crossfade va blend tree bilan aralashadi):
  klip animatsiya qiladigan target'lar klipdan, qolganlari Mesh Renderer'dan.
- Og'irliklar CPU'da qo'llanadi: og'irliklari modelnikidan farq qiladigan har obyekt o'z mesh nusxasini oladi va
  og'irliklar o'zgarganda qayta yoziladi (skinning bilan ham ishlaydi).

```cpp
auto& mr = GetComponent<MeshRendererComponent>();
mr.SetMorphWeight("Smile", 0.8f);        // nom yoki "Guruh/Target"
mr.SetMorphWeight(2, 0.5f);              // yoki indeks
float s = mr.GetMorphWeight("Smile");
mr.ResetMorphWeights();                  // modelning o'z og'irliklari
```

---

## 13. glTF kliplarini kesish va bo'lish

Model `.zmeta` faylining import sozlamalarida (asset inspektori > **Kliplar**) fayldagi klipdan qism kesib olinadi:

```json
"Import": {
    "Clips": [
        { "Source": "Take001", "Name": "Walk", "Start": 0.0, "End": 1.2 },
        { "Source": "Take001", "Name": "Run",  "Start": 1.2, "End": 2.0 },
        { "Source": "Wave", "Start": 0.25 }
    ],
    "KeepSourceClips": false,
    "ClipFPS": 30
}
```

- Boshqa nom bilan — alohida klip (bitta uzun yozuvdan Walk / Run / Jump); nomsiz yoki manba nomi bilan — manba kesiladi.
- `End` < 0 yoki yo'q — klip oxirigacha. `KeepSourceClips: false` — bo'lingan manbaning o'zi qolmaydi.
- `ClipFPS` — inspektor vaqtlarni kadrlarda ko'rsatishi uchun.

Inspektorda har qator uchun oraliq chizig'i (chetlari sudraladi), soniya va kadr maydonlari, ogohlantirishlar va
qo'llashdan oldin ko'rib chiqish bor.

---

## 14. Bir nechta skin

Bitta glTF faylda bir nechta skin bo'lishi mumkin: har skinned mesh tuguni o'z skin'i bilan — tana va kiyim alohida
skin'larda, bir faylda ikki personaj, yoki ikki armaturaning suyaklariga bog'langan arqon. Zamin hammasini yuklaydi:

- Modelning **skeleti** — barcha skin'larning suyaklari (har tugun bir marta, ota-onalar oldin). Turli joylashgan
  armaturalarning tugunlari ham skeletga qo'shiladi — har qism faylda turgan joyida. Bitta Animator hammasini bir pozada
  animatsiya qiladi.
- Har **skin** o'z suyaklari ro'yxatini va inverse bind matritsalarini saqlaydi (`Model::GetSkins()`). Skeletniki bilan bir
  xil bo'lsa mesh to'g'ridan-to'g'ri skelet matritsalari bilan chiziladi, farq qilsa (mesh boshqa fazoda bog'langan) —
  o'z **palitrasi** bilan (`GetSkinMatrices`). Bitta skin'da 80 tagacha suyak (GPU chegarasi) — butun model emas.
- Bone Attachment, root motion, editordagi skelet gizmosi barcha skin'lar suyaklari bilan ishlaydi.

Namuna: Showcase3D `Models/TentaclePair.gltf` (`Scenes/Olomon.zscene` oldida): ikki armatura (biri teskari burilgan)
va ikkala uchini bog'lagan oltin arqon — uch skin, bitta Animator.

---

## 15. Sprite animatsiyasi va kliplar (.zsanim)

2D uchun: **Sprite Animation** komponenti (`SpriteAnimationComponent`, *+ Komponent → Render → Sprite Animation*) Sprite
Renderer teksturasining kadrlarini FPS bilan o'ynaydi. Kadrlar uch xil beriladi:

- **Grid** — tekstura `Columns` x `Rows` kataklarga bo'linadi (chapdan o'ngga, yuqoridan pastga raqamlanadi), `StartFrame`
  dan `FrameCount` ta kadr, `FPS`, `Loop`;
- **Nomlangan kadrlar** — `Frames` ro'yxati (sprite sheet'dagi nomlar, **Oyna > Sprite muharriri** da kesiladi; inspektorda
  `run_0, run_1, ...` ketma-ketliklari bitta tanlov bilan);
- **Kliplar** — `Clips` ro'yxatidagi `.zsanim` assetlar (pastda).

Skriptdan:

```cpp
// Platformer2D PlayerController: sprite sheet panjarasidagi holatlar.
if (auto* anim = TryGetComponent<SpriteAnimationComponent>())
{
    if (m_GroundContacts == 0)             anim->Play(8, 1, 1.0f, true);   // sakrash: 8-kadr
    else if (std::abs(velocity.x) > 0.5f)  anim->Play(4, 4, 12.0f, true);  // yugurish: 4..7, 12 kadr/s
    else                                   anim->Play(0, 2, 3.0f, true);   // turish: 0..1
}
if (auto* sprite = TryGetComponent<SpriteRendererComponent>(); sprite && dir != 0.0f)
    sprite->FlipX = dir < 0.0f;                                            // chapga qaraganda aks

PlaySpriteSequence("run", 12.0f);   // nomlangan "run_0", "run_1", ... (Sprite Animation bo'lmasa qo'shiladi)
SetSprite("idle");                  // bitta nomlangan kadr ("" - butun rasm)
anim->PlayFrames({ "hit_0", "hit_1" }, 15.0f, false);
```

`Play` shu kadrlar allaqachon o'ynayotgan bo'lsa ularni qayta boshlamaydi — har kadr chaqirish xavfsiz.

Kliplar asset sifatida — `.zsanim`:

```json
{ "Version": 1, "Texture": "Textures/hero.png", "FPS": 10.0, "Loop": true,
  "Frames": [ "run_0", "run_1", "run_2", "run_3" ],
  "Events": [ { "Frame": 1, "Name": "Step" }, { "Frame": 3, "Name": "Step" } ] }
```

Komponentning **Kliplar** ro'yxati (birinchisi `PlayOnStart` bilan boshlanadi); o'ynayotgan klip renderer teksturasini ham
qo'yadi. Kadr event'lari har siklda bir marta (katta kadrda ham).

```cpp
PlaySpriteClip("HeroSide");          // o'ynab turgan bo'lsa davom etadi
PlaySpriteClip("HeroSide", true);    // boshidan
std::string current = GetSpriteClip();
```

Kontent brauzer: **Yangi sprite animatsiya klipi**; inspektorda "Shu kadrlardan klip"; asset inspektorida o'ynaydigan
ko'rinish, kadrlar tasmasi va event'lar.

---

## 16. Samaradorlik: keshlash va kesish

Ko'p animatsiyali personaj bo'lsa skelet pozalarini hisoblash qimmatlashadi. Animator > **Samaradorlik**:

**Bir xil kirishlar — hisob yo'q** (doim ishlaydi, sozlash kerak emas). Poza nimadan yasalgani (kliplar, vaqtlar, og'irliklar,
crossfade, qatlamlar, morph og'irliklari, root motion suyagi) o'tgan kadrdagidek bo'lsa — pauza, tezlik 0, tugagan
(takrorlanmaydigan) klip — oldingi poza qoladi.

**Ko'rinmasa** (`Culling`): model hech bir ko'rinishda (o'yin kamerasi, editorning sahna ko'rinishi — qaysi biri bo'lsa ham)
ko'rinmagan bo'lsa:

| Rejim | Nima bo'ladi |
|---|---|
| **Doim** (`AlwaysAnimate`) | odatdagidek |
| **Pozasiz** (`CullPose`) | vaqt, holat mashinasi, event'lar (qadam tovushlari!) va root motion davom etadi, skelet pozasi hisoblanmaydi; ko'ringan kadridan keyin yana pozalanadi |
| **To'liq** (`CullCompletely`) | hech narsa yangilanmaydi — ko'ringanda turgan joyidan davom etadi |

Chegaralar — modelning oxirgi pozasidagi skinned chegaralari (biroz kattalashtirilgan). Yangi animator kamida bir marta
pozalanadi. Sahna umuman chizilmasa (headless) hech narsa kesilmaydi. Eslatma: faqat soyasi ko'rinib turgan model ham
kesiladi.

**Poza keshi** (`CachePoses`, `PoseCacheRate` kadr/s, 30 standart): klip oldindan shu tezlikda namuna olinib saqlanadi
(modelning har klipi va tezligi uchun bir marta, uning shu tezlikdagi barcha animatorlari ulashadi; bir klipga 8 tagacha
turli tezlik, undan ko'pi eng uzoq so'ralmaganining o'rniga); har kadr ikki saqlangan kadr aralashtiriladi —
har kanal kalitlarini qidirish yo'q. Ko'p suyakli, ko'p kalitli kliplarda arzonroq; saqlangan kadrlar orasida biroz
kamroq aniq, **step kalitlar aniq qoladi** (bunday suyaklar klipning o'zidan o'qiladi). Debug build'da 100 ta 60 suyakli
animator (10 s, 30 kalit/s): klipdan ~40 ms → keshdan ~24 ms; yarmi pauzada bo'lsa ~19 ms.

**O'lchash**: Statistika oynasi (Oyna > Statistika) > **Animatsiya** — animatorlar soni va vaqti, pozalanganlar,
o'zgarmaganlar, kesilganlar, keshdan / klipdan namunalar, kesh xotirasi. Profiler daraxtida `Animatsiya` va uning ichida
`Pozalar` (chaqiruvlar soni — pozalangan animatorlar). Skriptdan:

```cpp
const Scene::AnimationStats& a = GetScene().GetAnimationStats();
ZM_INFO("{} animator, {} pozalangan, {} kesilgan, {:.2f} ms", a.Animators, a.Posed, a.Culled, a.Milliseconds);
```

```cpp
auto& anim = GetComponent<AnimatorComponent>();
anim.Culling = AnimatorCulling::CullPose;
anim.CachePoses = true;
anim.PoseCacheRate = 30.0f;
```

Namuna: Showcase3D `Scenes/Olomon.zscene` — 168 ta tentakl (keshlangan, `CullPose`, ba'zilari pauzada), kamera chapga-o'ngga
buriladi; `AnimationStatsDemo` skripti raqamlarni sahnada yozib turadi.

---

## 17. Namuna: yaqinlashganda ochiladigan darvoza

Adventure3D `Scenes/Island.zscene` — orqadagi ark ostidagi ikki tabaqali darvoza (`Darvoza` obyekti):

```
Darvoza                (Animation Player: Animations/DarvozaOchilishi.zanim, Avto boshlash o'chiq; ProximityDoor skripti)
├── ChapOshiq          (chap oshiq — klip shuni buradi)
│   └── ChapTabaqa     (taxta: kinematik Rigidbody 3D + Box Collider — qahramonni to'sadi va itaradi)
├── OngOshiq
│   └── OngTabaqa
└── Chiroq             (sfera + nuqtali yorug'lik: qizil -> sariq -> yashil)
```

Klip (1.6 s, `Once`): oshiqlar Bezier bilan ±104° gacha buriladi va 100° ga qaytib o'tiradi; chiroq rangi `Step`
kalitlar bilan, yorug'lik kuchi `Linear`. Event'lar:

| Vaqt | Nom | Parametr | Qachon eshitiladi |
|---|---|---|---|
| 0.0 | `Taqillash` | | yopilib bo'lganda (orqaga o'ynab 0 ga yetganda) |
| 0.05 | `Gichirlash` | `ochilish` | ochila boshlaganda |
| 1.5 | `Gichirlash` | `yopilish` | yopila boshlaganda (oxiridan orqaga) |
| 1.6 | `Ochildi` | | ochilib bo'lganda |

Event har ikki yo'nalishda o'tilganda keladi, shuning uchun parametr qaysi yo'nalishga tegishli ekanini aytadi. Skript
(`Projects/Adventure3D/Source/World.cpp`, qisqartirilgan):

```cpp
class ProximityDoor : public ScriptableEntity
{
public:
    std::string Player = "Player";
    std::string Clip = "DarvozaOchilishi";
    float OpenRadius = 4.5f, CloseRadius = 6.5f, CloseDelay = 1.0f;
    std::string CreakSound = "Audio/DoorCreak.wav", ThudSound = "Audio/DoorThud.wav";

    void OnCreate() override
    {
        AddAnimationEventHandler("Gichirlash", [this](const AnimationEventInfo& e) {
            if ((e.Parameter == "ochilish") == m_Opening)   // shu yo'nalishniki
                Sound(CreakSound, 0.9f, { 0.9f, 1.1f });
        });
        AddAnimationEventHandler("Taqillash", [this](const AnimationEventInfo&) {
            if (!m_Opening)
                Sound(ThudSound, 1.0f, { 0.95f, 1.05f });
        });
    }

    void OnUpdate(Timestep ts) override
    {
        Entity player = FindEntity(Player);
        auto* anim = TryGetComponent<AnimationPlayerComponent>();
        if (!player || !anim) return;
        glm::vec3 d = GetScene().GetWorldPosition(player) - GetWorldPosition();
        d.y = 0.0f;
        const float distance = glm::length(d);
        if (!m_Opening && distance < OpenRadius)
            Turn(*anim, true);
        else if (m_Opening && distance > CloseRadius)
        {
            if ((m_Away += ts) >= CloseDelay) Turn(*anim, false);
        }
        else
            m_Away = 0.0f;
    }

private:
    void Turn(AnimationPlayerComponent& anim, bool open)
    {
        m_Opening = open;
        m_Away = 0.0f;
        // Turgan joyidan: yarim ochilgan darvoza shu yerdan yopiladi (oxiriga sakramaydi).
        const float from = anim.Current >= 0 ? anim.GetTime() : (open ? 0.0f : anim.GetDuration());
        anim.PlayFrom(Clip, from, open ? 1.0f : -1.0f);
    }
    void Sound(const std::string& path, float volume, glm::vec2 pitch)
    {
        SoundParams p;
        p.Volume = volume;
        p.PitchRandom = pitch;           // har safar biroz boshqacha
        p.Spatial = true;
        p.Position = GetWorldPosition() + glm::vec3(0.0f, 1.8f, 0.0f);
        PlaySound(path, p);
    }
    bool m_Opening = false;
    float m_Away = 0.0f;
};
ZM_REGISTER_SCRIPT(ProximityDoor, ZM_FIELD(ProximityDoor, OpenRadius), ZM_FIELD(ProximityDoor, CloseRadius) /* ... */)
```

Klipni editorda ko'rish: `Darvoza` ni tanlang, **Oyna > Animatsiya** — vaqtni surib darvozaning ochilishini play
rejimisiz ko'rasiz; event'lar qatorida to'rtta event.

---

## 18. AnimationCurve: skriptdagi egri chiziq

`AnimationCurve` (`Zamin/Math/AnimationCurve.h`) - vaqt bo'yicha bitta son: kalitlar (vaqt, qiymat, kirish / chiqish
tangentlari), ular orasida pog'onali / chiziqli / silliq (Bezier) o'tish va birinchi kalitdan oldin / oxirgisidan
keyin nima bo'lishi (`PreWrap` / `PostWrap`: `Once` - chetki qiymat, `Loop` - takrorlash, `PingPong` - borib-qaytish).
Kalitlar va arifmetika property kliplar va particle egri chiziqlari bilan bir xil (`Math/CurveKey.h`: `CurveKeys`).

Skript maydoni sifatida inspektorda egri chiziq muharriri bilan tahrirlanadi (kalitni sudrash, ikki marta bosish -
yangi kalit, o'ng tugma - interpolatsiya / tayyor shakllar, pastda - chetlardagi xatti-harakat) va sahna bilan saqlanadi:

```cpp
class Hopper : public ScriptableEntity
{
public:
    AnimationCurve Jump = AnimationCurve::EaseInOut(0.0f, 0.0f, 0.4f, 2.0f);

    void OnUpdate(Timestep ts) override
    {
        m_Time += (float)ts;
        Transform().Translation.y = m_Ground + Jump.Evaluate(m_Time);
    }
private:
    float m_Ground = 0.0f, m_Time = 0.0f;
};
// ZM_FIELD_RANGE: muharrir kamida shu qiymatlarni ko'rsatadi.
ZM_REGISTER_SCRIPT(Hopper, ZM_FIELD_RANGE(Hopper, Jump, 0.0f, 2.0f))
```

Kodda: `AddKey(t, v)`, `AddKey(t)` (egri chiziq qiymati bilan), `RemoveKey`, `MoveKey`, `SetTangents` (qo'lda),
`SmoothTangents()` / `FlattenTangents()`, `SetInterpolation`, `GetStartTime()` / `GetEndTime()`, `GetMin()` / `GetMax()`,
tayyorlari: `Constant`, `Linear`, `EaseInOut`. Namuna: Showcase3D `Splines` sahnasi, `CurveBob` skripti.

## 19. Spline yo'llar va Spline Follower

**Spline** komponenti obyektning o'z koordinatalaridagi nuqtalar orqali yo'l: Catmull-Rom (har bir nuqtadan silliq
o'tadi; `Alpha` 0.5 - centripetal, yaqin nuqtalarda halqa va o'tkir uch bo'lmaydi), Bezier (har nuqtaning ikki
tutqichi) yoki chiziqli; ochiq yoki yopiq (halqa). Inspektorda "Nuqtalarni tahrirlash" - nuqtalar viewportda
sudraladi (3D: kameraga qaragan tekislikda, tanlangan nuqtada o'qli gizmo), chiziqqa bosish - yangi nuqta, bo'sh joyga
ikki marta bosish - oxiriga nuqta, Ctrl + bosish / Delete - o'chirish, Shift - 0.1 qadam; Bezier'da tanlangan
nuqtaning tutqichlari (Ctrl bilan alohida). Yo'llar editorda doim chiziladi (Ko'rinish > Gizmolar > "Spline yo'llar").

**Spline Follower** obyektni yo'l bo'ylab `Speed` (dunyo birligi / s) tezlikda yurgizadi: `Bir marta`, `Aylanma`
yoki `Borib-qaytish`; `Burilish` - yo'l bo'ylab qarash (3D: -Z oldinga; 2D: +X yo'l bo'ylab), `Siljish` - yo'lning o'z
o'qlarida (x o'ngga, y yuqoriga, z yo'l bo'ylab), `Boshlanish` - yo'lning qaysi ulushidan. Kinematik jism tezlik bilan
suriladi - ustida turgan o'yinchini olib yuradi (Platformer2D `Level2` dagi yoy bo'ylab yuradigan platforma).

```cpp
auto& follower = GetComponent<SplineFollowerComponent>();
follower.Speed = 4.0f;
follower.Pause();          // Play(), Restart()
float progress = GetScene().GetSplineFollowerProgress(GetEntity());   // 0..1

// Yo'ldan namuna olish (dunyo koordinatalari; masofa - yo'lning o'z birliklarida):
Entity rail = FindEntity("Poyga yo'li");
float d = GetScene().GetSplineClosestDistance(rail, playerPosition);  // o'yinchiga eng yaqin nuqta
glm::vec3 p = GetScene().GetSplinePosition(rail, d);
glm::vec3 dir = GetScene().GetSplineTangent(rail, d);
```

Matematikasi `Zamin/Math/Spline.h` da: `Spline::Evaluate(u)` / `EvaluateTangent(u)`, `SplineLengthTable` (masofa <->
parametr), `FindClosest`, `InsertPoint` (Bezier shakli saqlanadi), `AutoTangents` (Catmull-Rom shaklidagi tutqichlar).

## 20. Tint animatsiyasi (rang)

**Tint Animation** (`TintAnimationComponent`, *+ Komponent → Render → Tint Animation (rang)*) 2D renderer (Sprite, Circle,
Text, Tilemap) va Light 2D rangini gradient bo'yicha o'zgartiradi: chiziladigan rang = renderer rangi x gradient rangi
(renderer'ning o'z `Color` i o'zgarmaydi).

| Maydon | Ma'nosi |
|---|---|
| `Keys` (`TintKey`: vaqt 0..1 + rang) | gradient |
| `Duration` | bir o'tish, soniya |
| `Mode` (`TintPlayMode`): `Once`, `Loop`, `PingPong` | tugash rejimi |
| `Easing` (`Ease`) | vaqt egri chizig'i |
| `Cycles` | Loop / PingPong: shuncha sikl (0 — cheksiz) |
| `StartOffset` | boshlanish siljishi — bir xil nusxalar bir vaqtda miltillamasin |
| `PlayOnStart` | o'yin boshida |

Inspektorda gradient chizig'i va "Sahnada ko'rish". Skriptdan:

```cpp
PlayTint();                                                   // komponentning o'zini qayta boshlash
PlayTint({ 1.0f, 1.0f, 1.0f, 0.2f }, 0.08f, TintPlayMode::PingPong, 6);   // 6 marta miltillash (komponent bo'lmasa qo'shiladi)
StopTint();                                                   // joyida to'xtaydi; StopTint(true) - birinchi rangga
bool on = IsTintPlaying();
```

Qisqa bir martalik chaqnash uchun `Flash(color, seconds)` qulayroq ([Rendering.md](Rendering.md#14-skriptdan-rang-effektlar-kamera)).
Namuna: Platformer2D `Scenes/TopDown2D.zscene` (fonarlar, gulxan, kristallar).

---

## 21. Skript tween'lari

Kod bilan qisqa silliq o'zgarishlar — animatsiya asseti kerak emas. Hammasi o'yin vaqtida (`SetTimeScale` ga ergashadi,
pauzada kutadi; `SetTimersUnscaled(true)` — haqiqiy vaqtda), skript o'chirilsa to'xtab turadi:

```cpp
TweenPosition(GetWorldPosition() + glm::vec3(0.0f, 2.0f, 0.0f), 0.5f, Ease::OutBack);   // 0.5 s da 2 birlik yuqoriga
TweenScale(glm::vec3(1.3f), 0.2f);                                                    // standart Ease::OutBack
TweenColor({ 1.0f, 0.3f, 0.3f, 1.0f }, 0.25f);                                        // 2D renderer rangi
FadeTo(0.0f, 0.5f, Ease::OutQuad, [this] { Destroy(); });                             // so'nib yo'qolish

// Umumiy: t (0..1, easing bilan) har kadr keladi.
const float from = Transform().Rotation.y;
Tween(1.0f, [this, from](float t) { Transform().Rotation.y = from + t * glm::two_pi<float>(); },
      Ease::InOutCubic, [] { ZM_INFO("Aylanib bo'ldi"); });

CancelTweens();             // hammasi joyida to'xtaydi
bool busy = IsTweening();
```

Easing turlari (`Ease`, `Zamin/Math/Easing.h`): `Linear`, `InQuad`, `OutQuad`, `InOutQuad`, `OutCubic`, `InOutCubic`, `OutBack`,
`OutElastic`, `OutBounce`; qiymatning o'zi — `ApplyEase(ease, t)`. Tween dinamik jismni emas, transformni o'zgartiradi —
fizik jismlar uchun tezlik yoki kinematik tur ishlating. Taymerlar va tween'lar haqida: [Scripting.md](Scripting.md#10-vaqt-taymerlar-tween).

---

## 22. UI animatsiyalari

Ekran UI uchun alohida, **UI vaqtida** (o'yin pauzasida ham) ishlaydigan tween'lar (`UIScaleTo`, `UIFadeTo`, `UIMoveTo`,
`UIPunchScale`, `Scene::TweenUI`) va **UI Animation** komponenti (paydo bo'lish / yashirish presetlari, ustiga kelganda va
bosilganda masshtab) — [UI.md](UI.md#10-ui-animatsiyalari). UI xususiyatlari (`UITransform.Position`, `Size`, `Scale`, `Alpha`,
`UIImage.Color`) property kliplarda ham animatsiya qilinadi (2-bo'lim).

---

## 23. Namuna sahnalar

| Loyiha / sahna | Nima ko'rsatadi | Skriptlar |
|---|---|---|
| Showcase3D `Scenes/Animation.zscene` | property kliplar (mayoq, eshik), glTF Animator event'lari | `AnimationEventDemo` |
| Showcase3D `Scenes/Animator.zscene` | blend tree, "Har qanday holat" trigger'i, maskali qatlam, morph target'lar, suyakka biriktirish | `AnimatorDemo`, `BlendShapeDemo` |
| Showcase3D `Scenes/Olomon.zscene` | 168 ta animator: poza keshi, kesish | `AnimationStatsDemo`, `SweepCamera` |
| Showcase3D `Scenes/Splines.zscene` | spline yo'llar, AnimationCurve | `CurveBob`, `SplineMagnet` |
| Adventure3D `Scenes/Island.zscene` | robot (Idle / Walk / Run blend tree, qadam tovushlari, qo'ldagi fonar), darvoza | `ThirdPersonController`, `ProximityDoor` |
| Platformer2D `Scenes/Level1.zscene` | grid sprite animatsiyasi | `PlayerController` |
| Platformer2D `Scenes/TopDown2D.zscene` | `.zsanim` kliplar, tint animatsiyalar | `TopDownController` |

---

## 24. Ko'p uchraydigan muammolar

| Belgi | Sabab va yechim |
|---|---|
| Animation Player o'ynamaydi | `Clips` bo'sh yoki klip topilmadi (Animatsiya oynasi: "Klip yuklanmadi"), **Avto boshlash** o'chiq va skript `PlayAnimation` chaqirmagan, yoki obyekt nofaol |
| Trek ishlamaydi (qiymat o'zgarmaydi) | Trekning yo'li (`Path`) bola nomiga mos emas — bola qayta nomlangan; Animatsiya oynasida trek qizil ko'rinadi |
| Animator modelni harakatlantirmaydi | Mesh Renderer'dagi model skinned emas yoki klip nomi noto'g'ri (`""` — birinchisi); holat mashinasida boshlang'ich holat yo'q |
| Oyoqlar yerda sirpanadi | Blend tree chegaralarini yurish tezligiga moslang (`Speed` parametri skript tezligi bilan bir birlikda) |
| Model obyektdan uzoqlashib, keyin orqaga sakraydi | Klipda ildiz siljishi bor — **Root motion > Qo'llash** (10-bo'lim) |
| Event ikki marta keladi | Bir event ikki klipda (crossfade) — `Footsteps` `MinInterval` bilan birlashtiradi; o'zingizniki uchun vaqtni tekshiring |
| Editorda animatsiya ko'rinmaydi | Animator: **Editorda ko'rsatish** (`PlayInEditor`); property klip: Animatsiya oynasida vaqtni suring |
| Ko'p personaj sekin | `Culling = CullPose`, `CachePoses` (16-bo'lim); Statistika oynasi > Animatsiya |

---

## 25. Testlar

```bash
./build/bin/ZaminTests "*Anim*:*Tint*:*Tween*:*Skin*:*Morph*:*Bone*:*Footstep*:*Clip*:*Blend*:*Pose*:*Spline*:*Curve*"
```

Asosiylari: `PropertyClipInterpolation`, `PropertyClipFileRoundtrip`, `PropertyClipAnimatesEveryProperty`,
`AnimationPlayerPlaysClips`, `AnimationPlayerPlayFromTurnsRound`, `AnimationEventsFireOnce`, `AnimationEventsExactlyOnceInScene`,
`SkeletalPoseEvaluation`, `AnimatorStateMachine`, `BlendTree1DWeights`, `AnimatorBlendTreeSyncsClips`, `AnimatorLayersMasks`,
`RootMotionExtraction`, `BoneAttachmentFollowsJoint`, `MorphTargetsApplyWeights`, `MultipleSkinsInOneGLTF`,
`AnimationClipTrimming`, `AnimationPoseCache`, `AnimatorSkipsUnchangedAndCulledPoses`, `SpriteAnimationFrames`,
`SpriteClipAssetsPlayWithEvents`, `TintGradientAndPhases`, `TintAnimationInScene`, `TweensRunAndChain`,
`FootstepsFromAnimationEvents`, `AnimationCurveEvaluateAndWrap`, `SplineEvaluation`. Katalog: [Testing.md](Testing.md).

---

## 26. Qisqa API ma'lumotnomasi

| Nima | API |
|---|---|
| Property klip o'ynatish | `PlayAnimation("Klip", speed)`, `StopAnimation()`, `IsAnimationPlaying()` |
| Animation Player | `Play(name, speed)`, `PlayFrom(name, seconds, speed)`, `Stop()`, `Resume()`, `SetTime(s)`, `GetTime()`, `GetDuration()`, `GetNormalizedTime()`, `GetCurrentClipName()` |
| Animator klipi | `Play("Run", loop, speed, fade)`, `Speed`, `Playing`, `GetNormalizedTime(model)`, `IsFading()`, `GetFadeProgress()` |
| Holat mashinasi | `SetFloat`, `SetBool`, `SetTrigger`, `GetFloat`, `GetBool`, `SetState("Jump", fade)`, `GetCurrentStateName()` |
| Blend tree | `GetBlendTreeState()`, `BlendWeights` |
| Qatlamlar | `PlayLayer(layer, clip, loop, speed, fade)`, `SetLayerWeight(layer, w, fade)`, `GetLayerWeight(layer)` |
| Event'lar | `OnAnimationEvent(const AnimationEventInfo&)` (`Name`, `Parameter`, `Clip`, `Time`, `Source`, `Skeletal`), `AddAnimationEventHandler(name, fn)`, event bus: `"AnimationEvent"` |
| Root motion | `ApplyRootMotion`, `RootMotionBone`, `RootMotionVelocity`, `RootMotionTurnRate` |
| Morph target'lar | `MeshRendererComponent::SetMorphWeight(name / index, w)`, `GetMorphWeight`, `ResetMorphWeights()` |
| Sprite animatsiya | `SpriteAnimationComponent::Play(start, count, fps, loop)`, `PlayFrames(frames, fps, loop)`; `PlaySpriteSequence(prefix, fps, loop)`, `SetSprite(name)` |
| Sprite kliplar | `PlaySpriteClip(name, restart)`, `GetSpriteClip()` |
| Tint | `TintAnimationComponent` (`Keys`, `Duration`, `Mode`, `Easing`, `Cycles`, `StartOffset`), `PlayTint`, `StopTint`, `IsTintPlaying` |
| Tween'lar | `Tween(duration, fn, ease, onComplete)`, `TweenPosition`, `TweenScale`, `TweenColor`, `FadeTo`, `CancelTweens`, `IsTweening`; `Ease`, `ApplyEase` |
| Samaradorlik | `AnimatorComponent::Culling`, `CachePoses`, `PoseCacheRate`, `Culled`; `Scene::GetAnimationStats()` |
| Egri chiziq va yo'l | `AnimationCurve::Evaluate`, `AddKey`, `SmoothTangents`; `SplineFollowerComponent::Play` / `Pause` / `Restart`; `Scene::GetSplinePosition`, `GetSplineTangent`, `GetSplineLength`, `GetSplineClosestDistance`, `GetSplineFollowerProgress` |
| Past daraja | `SampleLocalPose`, `BlendLocalPose`, `AddLocalPose`, `ComposeJointMatrices`, `ComputeBlend1DWeights`, `BuildJointMask`, `ExtractRootMotion`, `TrimClip`, `BakeClip` / `SampleBakedPose`, `GetSkinMatrices` (`Zamin/Renderer/Animation.h`, `Model.h`) |
