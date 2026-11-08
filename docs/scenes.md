# Zamin Engine: sahna boshqaruvi

Bu hujjat o'yinning sahnalarini boshqarishni tushuntiradi: sahna yuklash va almashtirish, bir vaqtda bir nechta
sahna (additive), sahnalar almashganda saqlanadigan obyektlar (DontDestroyOnLoad), fonda yuklash va yuklanish
ekrani, so'nib-ochilish o'tishlari, build sahnalar ro'yxati, sahna hodisalari, tez qidiruv, yuklash xatolari,
sahna fayli formati (barqaror tartib, versiya va migratsiya, metadata), global o'yin holati, sub-sahnalar,
yuklash vaqtini o'lchash va katta sahnalarni tez saqlash / yuklash. Har bo'limda C++ skript misollari bor
(`#include <Zamin/Zamin.h>`, `using namespace Zamin;`).

Mundarija:

1. [Asosiy tushunchalar](#1-asosiy-tushunchalar)
2. [Sahnani yuklash](#2-sahnani-yuklash)
3. [Bir nechta sahna (additive)](#3-bir-nechta-sahna-additive)
4. [DontDestroyOnLoad: saqlanadigan obyektlar](#4-dontdestroyonload-saqlanadigan-obyektlar)
5. [Fonda yuklash va yuklanish ekrani](#5-fonda-yuklash-va-yuklanish-ekrani)
6. [Sahna o'tishlari (so'nish)](#6-sahna-otishlari-sonish)
7. [Build sahnalar ro'yxati va indekslar](#7-build-sahnalar-royxati-va-indekslar)
8. [Sahna hodisalari](#8-sahna-hodisalari)
9. [Sahna ichida qidirish](#9-sahna-ichida-qidirish)
10. [Yuklash xatolari](#10-yuklash-xatolari)
11. [Sahna fayli: tartib, versiya, metadata](#11-sahna-fayli-tartib-versiya-metadata)
12. [Global o'yin holati: servislar va GameState](#12-global-oyin-holati-servislar-va-gamestate)
13. [Sub-sahnalar](#13-sub-sahnalar)
14. [Yuklash vaqti profiler'da](#14-yuklash-vaqti-profilerda)
15. [Katta sahnalar: tez saqlash va yuklash, binar kesh](#15-katta-sahnalar-tez-saqlash-va-yuklash-binar-kesh)
16. [Editorda sahnalar](#16-editorda-sahnalar)
17. [Namuna: Platformer2D sahnalar oqimi](#17-namuna-platformer2d-sahnalar-oqimi)
18. [Testlar va benchmark](#18-testlar-va-benchmark)
19. [Cheklovlar](#19-cheklovlar)
20. [Qisqa API ma'lumotnomasi](#20-qisqa-api-malumotnomasi)

---

## 1. Asosiy tushunchalar

O'yin sahnalarini **`SceneManager`** boshqaradi. Uni o'yinni yurgizadigan dastur yaratadi: editorning Play rejimi
va `ZaminPlayer`. Skriptlar unga `GetSceneManager()` orqali yoki to'g'ridan-to'g'ri yordamchilar (`LoadScene`,
`DontDestroyOnLoad` ...) orqali murojaat qiladi.

| Tushuncha | Ma'nosi |
|---|---|
| **Faol sahna** (active) | Yangi obyektlar shu sahnaga tegishli; uning kamerasi o'yinni chizadi, sozlamalari (osmon, post-processing) rasmniki |
| **Yuklangan sahnalar** | Faol sahna va uning yoniga qo'shimcha (Additive) yuklanganlar, yuklanish tartibida |
| **DontDestroyOnLoad sahnasi** | Sahna almashganda saqlanadigan obyektlar uchun alohida, doimiy sahna |
| **Sub-sahna** | Boshqa sahna faylidan shu sahna ichidagi obyekt joyiga qo'yiladigan bo'lak (13-bo'lim) |
| **So'rov** | `LoadScene`, `UnloadScene`, `DontDestroyOnLoad` darhol emas, **kadr oxirida** bajariladi |

Har yuklangan sahnaning o'z obyektlari, skriptlari va fizika dunyolari bor; ularning hammasi bitta kadrda
yangilanadi va **bitta kamera** orqali birga chiziladi. Barcha sahnalar bitta hodisalar shinasi (event bus) va
bitta o'yin soatidan (vaqt masshtabi, pauza) foydalanadi.

Kadr tartibi:

1. Fonda yuklanayotgan sahnalar asosiy oqim ulushini oladi (`UpdateAsyncLoads`, ~4 ms).
2. So'nish o'tishi oldinga suriladi.
3. Har sahna yangilanadi: avval DontDestroyOnLoad sahnasi, keyin yuklanganlar tartibida.
4. Hammasi chiziladi.
5. Kadr oxirida (`ProcessRequests`) shu kadrda so'ralgan narsalar bajariladi: ko'chirishlar, yopishlar,
   yuklashlar, fonda tayyor bo'lgan sahnalarning kirishi, hodisalar.

Shu sababli skript `LoadScene` chaqirgandan keyin ham joriy kadr oxirigacha eski sahnada ishlayveradi.

---

## 2. Sahnani yuklash

Yo'llar `Assets` papkasiga nisbatan yoziladi. Yo'l o'rniga build ro'yxatidagi sahna **nomi** (`"Level2"`) yoki
**indeksi** (`2`) ham beriladi.

```cpp
class LevelExit : public ScriptableEntity
{
public:
    void OnTriggerBegin(Entity other) override
    {
        if (!other.HasTag("Player"))
            return;
        LoadScene("Scenes/Level2.zscene");                           // Single: hamma sahna almashadi
        // LoadScene("Level2");                                      // build ro'yxatidagi nomi
        // LoadScene(3);                                             // build indeksi
        // LoadScene("Scenes/Level2.zscene", SceneTransition::Fade(0.5f)); // qora rangga so'nib
    }
};
ZM_REGISTER_SCRIPT(LevelExit)
```

Rejimlar (`LoadSceneMode`):

- **`Single`** (standart) — barcha yuklangan sahnalar yopiladi va yangisi faol bo'ladi. DontDestroyOnLoad
  sahnasidagi obyektlar qoladi. O'yin soati yana 1 ga qaytadi.
- **`Additive`** — yangi sahna mavjudlari yoniga qo'shiladi, faol sahna o'zgarmaydi.

Boshqa yordamchilar: `UnloadScene("HUD")` (nomi yoki yo'li bilan), `LoadNextScene()`, `ReloadScene()` (daraja
qayta boshlanadi). Fayl o'qilmasa, o'yin turgan joyida qoladi va xato hisobotga yoziladi (10-bo'lim).

Eski kod (`Scene::RequestLoadScene`) ham ishlayveradi: `SceneManager` uni ham kadr oxirida bajaradi.

---

## 3. Bir nechta sahna (additive)

```cpp
// Daraja boshlanganda HUD sahnasi uning yoniga yuklanadi.
void OnStart() override
{
    LoadScene("Scenes/HUD.zscene", LoadSceneMode::Additive);
}
```

Qo'shimcha sahnalar:

- o'z obyektlari, skriptlari va 2D/3D fizika dunyolari bilan ishlaydi;
- faol sahnaning birlamchi kamerasi orqali chiziladi (osmonsiz, ustiga qo'shiladi); faol sahnada kamera
  bo'lmasa, kamerasi bor birinchi yuklangan sahna chizadi;
- o'yin soatiga qo'shiladi (sekinlashtirilgan o'yinda qo'shimcha sahna ham sekin yuradi).

Sahnalar o'rtasida qidirish (`FindEntity`, `FindEntityWithTag`, `FindObjectsOfType`, `Resolve(EntityRef)`) avval
skriptning o'z sahnasida, keyin faol sahnada, keyin qolganlarida, oxirida DontDestroyOnLoad sahnasida qidiradi.

```cpp
SceneManager* scenes = GetSceneManager();
scenes->SetActiveScene(scenes->GetSceneByName("Level2"));     // faolni darhol almashtirish
for (const Ref<Scene>& scene : scenes->GetLoadedScenes())
    ZM_INFO("{} ({})", scene->GetName(), scenes->GetScenePath(scene.get()));
scenes->UnloadScene("HUD");                                   // kadr oxirida
```

Oxirgi yuklangan sahnani yopib bo'lmaydi (uning o'rniga boshqasini `Single` bilan yuklang). Faol sahna yopilsa,
keyingi yuklangan sahna faol bo'ladi.

---

## 4. DontDestroyOnLoad: saqlanadigan obyektlar

`DontDestroyOnLoad()` obyektni (bolalari bilan) kadr oxirida doimiy **DontDestroyOnLoad** sahnasiga ko'chiradi.
U har `Single` yuklashdan omon qoladi. Ko'chishda obyektning UUID'si, komponentlari va **ishlayotgan skript
nusxasi** saqlanadi: a'zo qiymatlari, taymerlar va obunalar qoladi, `OnDestroy` / `OnCreate` qayta chaqirilmaydi,
o'ynayotgan tovushlar uzilmaydi, fizika jismi yangi sahnaning dunyosida qayta quriladi.

```cpp
// Butun o'yin davomida yashaydigan menejer (musiqa, ochko).
class GameManager : public ScriptableEntity
{
public:
    void OnCreate() override
    {
        // Menyuga qaytilganda ikkinchi nusxa kerak emas.
        if (SceneManager* scenes = GetSceneManager())
            if (const Ref<Scene>& persistent = scenes->GetDontDestroyOnLoadScene();
                persistent && persistent->FindEntityByName(GetName()))
            {
                Destroy();
                return;
            }
        DontDestroyOnLoad();
        Audio::PlayMusic("Audio/theme.wav");
    }
};
ZM_REGISTER_SCRIPT(GameManager)
```

Bitta fayldan ikkinchi marta yuklangan obyektning UUID'si birinchisiniki bilan bir xil bo'ladi; doimiy sahnada
shu UUID bilan obyekt bo'lsa, ko'chirish rad etiladi (ogohlantirish bilan) va obyekt o'z sahnasida qoladi. Shuning
uchun yuqoridagi tekshiruv kerak.

Editorning Play rejimida ierarxiya har yuklangan sahnani alohida bo'lim sifatida ko'rsatadi: faoli yulduzcha bilan,
qolganlari (DontDestroyOnLoad ham) yig'ilgan bo'limlarda; ko'z tugmasi o'sha sahnani ko'rib chiqishga ochadi.

---

## 5. Fonda yuklash va yuklanish ekrani

`LoadSceneAsync` sahnani o'yin to'xtamasdan yuklaydi va `AsyncOperation` qaytaradi:

| Bosqich (`GetStage()`) | Qayerda | `GetProgress()` |
|---|---|---|
| `Reading` — fayl o'qiladi va JSON tahlil qilinadi | fon oqimi | 0 → 0.25 |
| `Decoding` — sahna nomlagan rasmlar oldindan dekodlanadi | fon oqimi | 0.25 → 0.5 |
| `Building` — obyektlar yaratiladi | asosiy oqim, har kadr ulush bilan | 0.5 → 0.7 |
| `Resolving` — teksturalar, meshlar, shriftlar GPU'ga yuklanadi | asosiy oqim, ulush bilan | 0.7 → 0.9 |
| `Ready` — tayyor, kirishga ruxsat kutilmoqda | — | 0.9 |
| `Done` / `Failed` | — | 1 |

`SetAllowSceneActivation(false)` bilan tayyor sahna ruxsat berilguncha kutadi (masalan, yuklanish ekrani
animatsiyasini tugatish uchun). Asosiy oqim ulushi `SceneManager::SetAsyncBudget(ms)` bilan sozlanadi (standart 4 ms).

```cpp
class LoadingScreen : public ScriptableEntity
{
public:
    std::string Target = "Scenes/Level1.zscene";
    EntityRef Bar;   // UI Progress Bar

    void OnStart() override
    {
        m_Load = LoadSceneAsync(Target, LoadSceneMode::Single, SceneTransition::Fade(0.4f));
        if (m_Load)
            m_Load->SetAllowSceneActivation(false); // ekran o'zi qaror qiladi
    }

    void OnUpdate(Timestep) override
    {
        if (!m_Load || m_Load->HasFailed())
            return; // m_Load->GetError() / GetReport() - nima bo'lgani
        if (Entity bar = Resolve(Bar))
            bar.GetComponent<UIProgressBarComponent>().Value = m_Load->GetProgress() / 0.9f;
        if (m_Load->IsReady())
            m_Load->SetAllowSceneActivation(true); // so'nish boshlanadi, almashuv uning orqasida
    }

private:
    Ref<AsyncOperation> m_Load;
};
ZM_REGISTER_SCRIPT(LoadingScreen, ZM_FIELD(LoadingScreen, Target), ZM_FIELD(LoadingScreen, Bar))
```

Fon oqimida faqat xavfsiz ishlar bajariladi: fayl o'qish, JSON tahlili, migratsiya va rasm dekodlash. Obyekt
yaratish va GPU yuklash asosiy oqimda, kadrlarga bo'lingan holda.

---

## 6. Sahna o'tishlari (so'nish)

```cpp
LoadScene("Scenes/Level2.zscene", SceneTransition::Fade(0.5f));                  // 0.5 s qoraga, 0.5 s ochilish
LoadScene("Scenes/Level2.zscene", SceneTransition::Fade(0.3f, 0.8f, { 1, 1, 1, 1 })); // oqqa, sekin ochilish
```

Ekran `FadeOut` sekundda rangga to'ladi (o'yin uning ostida davom etadi), almashuv **ekran to'liq yopilganida**
bo'ladi, keyin parda `FadeIn` sekundda ketadi. O'tish haqiqiy vaqtda yuradi (pauzadagi o'yinda ham). O'tishli
`LoadScene` sahnani fonda yuklaydi; yuklanish ekrani avvalgi o'tish hali ochilayotgan paytda o'z o'tishini
boshlashi mumkin. Parda post-processing'dan keyin, hamma narsaning ustidan chiziladi.

---

## 7. Build sahnalar ro'yxati va indekslar

**Loyiha sozlamalari → Build sahnalar** oynasida o'yinga kiradigan sahnalar tartib bilan beriladi: sudrab tartiblash,
yoqish / o'chirish, build indeksi ustuni, boshlang'ich sahna yulduzchasi, ochiq sahnani yoki hamma sahnalarni qo'shish,
kontent brauzeridan `.zscene` tashlash. Ro'yxat `.zproject` faylida yo'l, yoqilganlik va UUID bilan saqlanadi
(fayl nomi o'zgarsa ham UUID orqali topiladi).

- **Build indeksi** — yoqilgan sahnalar orasidagi tartib raqami, 0 dan.
- `LoadScene(int)`, `LoadSceneAsync(int)`, `LoadNextScene()`, `ReloadScene()`.
- `GetSceneBuildIndex()`, `GetActiveSceneBuildIndex()`, `GetBuildSceneCount()`.
- Eksport: ro'yxat bo'sh bo'lmasa, o'yinga faqat undagi sahnalar, boshlang'ich sahna va ular ishlatadigan
  **sub-sahnalar** kiradi.
- `ZaminPlayer --scene 2` (indeks), `--scene Level2` (nomi) yoki `--scene Scenes/Level2.zscene` (yo'l).

```cpp
// Oxirgi darajadan keyin menyuga qaytish.
if (!LoadNextScene(SceneTransition::Fade(0.5f)))
    LoadScene(0);
```

---

## 8. Sahna hodisalari

Har yuklangan sahnadagi va DontDestroyOnLoad sahnasidagi har ishlayotgan skript eshitadi:

```cpp
void OnSceneLoaded(const SceneEventInfo& scene) override;
void OnSceneUnloaded(const SceneEventInfo& scene) override;
void OnActiveSceneChanged(const SceneEventInfo& previous, const SceneEventInfo& current) override;
```

`SceneEventInfo`: `SceneRef` (yopilgan sahna chaqiruv paytida hali bor, keyin bo'shatiladi — ko'rsatkichni
saqlamang), `Name`, `Path`, `BuildIndex`, `Mode`. Hodisalar shinada ham chiqadi: `"SceneLoaded"`,
`"SceneUnloaded"` (payload `SceneEventInfo`) va `"ActiveSceneChanged"` (payload `ActiveSceneChange`).

| Nima bo'ldi | Hodisalar tartibi |
|---|---|
| `Single` yuklash | har yopilgan sahna uchun `OnSceneUnloaded` → `OnActiveSceneChanged` → `OnSceneLoaded` |
| `Additive` yuklash | `OnSceneLoaded` |
| Sahnani yopish | `OnSceneUnloaded` (+ faol bo'lsa `OnActiveSceneChanged`) |
| O'yin boshlanishi | `OnActiveSceneChanged` (sahnasizdan) → `OnSceneLoaded` |
| Sub-sahnalar | egasining `OnSceneLoaded` idan keyin har biri uchun `OnSceneLoaded` (Additive) |
| Stop | hodisa yo'q |

Yangi sahnaning o'z skriptlari ham `OnSceneLoaded` ni oladi: `OnCreate` dan keyin, `OnStart` dan oldin.

```cpp
void OnSceneLoaded(const SceneEventInfo& scene) override
{
    if (scene.BuildIndex == 3)
        Audio::CrossfadeTo("Audio/boss.ogg");
}
```

---

## 9. Sahna ichida qidirish

Qidiruvlar indekslangan va ularni har kadr chaqirish arzon:

| Chaqiruv | Murakkablik |
|---|---|
| `FindEntity(name)`, `FindEntities(name)` | O(1) xesh |
| `FindEntitiesWithTag(tag)`, `FindEntityWithTag(tag)` | O(k), k — tegli obyektlar |
| `Scene::CountEntitiesWithTag(tag)` | O(1) |
| `FindObjectsOfType<T>()`, `FindObjectOfType<T>()` | O(k), komponent yoki skript klassi (merosxo'rlari bilan) |
| `Scene::FindEntitiesWith<T>()`, `CountEntitiesWith<T>()` | O(k) |

Indekslar birinchi qidiruvda quriladi, keyin obyekt yaratish, o'chirish, `SetName`, `AddTag`, `RemoveTag` bilan
bittadan yangilanadi. Komponentni to'g'ridan-to'g'ri o'zgartirish keyingi kadr boshida bir marta tekshiriladi
(yoki darhol: `InvalidateNameCache` / `InvalidateTagIndex`).

```cpp
for (auto* enemy : FindObjectsOfType<EnemyAI>())
    enemy->Alert();
if (auto* hud = FindObjectOfType<Hud>())
    hud->SetScore(m_Score);
```

10 000 obyektda (Debug, `ZaminBench --queries`): `FindEntityByName` x1000 — 0.64 ms (to'liq aylanib chiqish
1429 ms), `CountEntitiesWithTag` x1000 — 0.13 ms (336 ms).

---

## 10. Yuklash xatolari

Sahna yuklanayotganda topilgan muammolar `SceneLoadReport` ga yig'iladi (serializer, fonda yuklash, `AsyncOperation`,
`SceneManager::GetLoadReports()`):

| Tur | Natija |
|---|---|
| Fayl topilmadi / o'qilmadi, JSON xatosi (qator, ustun va o'sha qator bilan), sahna fayli emas | **Fatal**: hech narsa yuklanmaydi, o'yin joyida qoladi |
| Obyekt emas yozuv | o'tkazib yuboriladi |
| O'qilmagan komponent | obyekt qolgan komponentlari bilan yuklanadi |
| Noma'lum komponent | o'tkazib yuboriladi (saqlanganda yo'qoladi) |
| Topilmagan asset fayli | obyekt va komponent bilan ogohlantirish |
| Takror UUID | ikkinchisiga yangi ID |
| Yo'q ota obyekt yoki halqa | obyekt ildiz bo'ladi |
| Yangiroq format versiyasi | tushunilgancha o'qiladi, ogohlantirish |

To'g'ri obyektlar har doim saqlanadi. Editorda viewport ustida yopiladigan banner va **"Sahna yuklash muammolari"**
oynasi (jadval, xato ustuni ostida belgi, obyektni tanlash, nusxa olish). Player birinchi sahna yuklanmasa
o'chib qolmaydi: xato ekranini ko'rsatadi (Esc — chiqish); o'yin davomidagi muvaffaqiyatsiz almashuv 6 sekund
qizil chiziq bilan ko'rsatiladi.

---

## 11. Sahna fayli: tartib, versiya, metadata

**Barqaror tartib.** `.zscene` faylida obyektlar UUID bo'yicha saralangan, kalitlar alifbo tartibida, ierarxiya esa
alohida yozilgan: `"RootOrder"` (ildiz obyektlar tartibi) va `"ChildOrder"` (bolalar UUID tartibida bo'lmasa).
Shuning uchun bir xil sahna har doim bir xil matn beradi va diff faqat o'zgargan joyni ko'rsatadi.

**Versiya va migratsiya.** Fayl `"Version": 4` bilan yoziladi. Eskiroq fayl yuklanganda hech narsa o'qilishidan
oldin migratsiyalar zanjiri ishlaydi (`SceneMigration`):

- **v0 → v1** (versiyasiz fayl): matn ko'rinishidagi ID'lar songa, `"Name"` → `"Tag"`.
- **v1 → v2**: Text `"Centered"` → `AlignH` / `AlignV`, UI `"Anchor"` → `AnchorMin` / `AnchorMax` / `Pivot`,
  `"Metadata"` qo'shiladi.
- **v2 → v3**: sub-sahna fayli `SubScene` ichida `"Scene"` / `"SceneUUID"` → `"Path"` / `"PathUUID"` (`"Scene"`
  nom kaliti edi, shuning uchun fayl ko'chirilganda yoki nomi o'zgarganda havola yangilanmasdi).
- **v3 → v4**: `"Settings"` dagi `"Gravity"` / `"Gravity3D"` faqat sahnaning o'z gravitatsiyasi bo'lsa yoziladi;
  dvigatelning standart qiymati ((0, -9.8) / (0, -9.81, 0)) olib tashlanadi - bunday sahna loyihaning
  gravitatsiyasiga ergashadi (Loyiha sozlamalari > Fizika). Fizika qadami (`"PhysicsStep"`) ham shunday: yo'q
  bo'lsa loyihaniki (Loyiha sozlamalari > Vaqt).

Har fayl versiyasi uchun migratsiya bir marta logga yoziladi. Fayl saqlanganda joriy formatga o'tadi.

**Metadata va rasm.** `SceneMetadata`: ta'rif, muallif, teglar (`Scene::GetMetadata()`). Editor sahnani saqlaganda
uning birlamchi kamerasidan rasm chizadi va yonidagi yashirin faylga yozadi (`.Level1.zscene.png`, 320×180) —
sub-sahnalari bilan birga. `SceneSerializer::ReadSceneInfo(file, info)` sahnani yuklamasdan nomi, versiyasi,
metadata, obyektlar soni, rasmi va ishlatadigan sub-sahnalarini o'qiydi (daraja tanlash menyusi uchun qulay).

---

## 12. Global o'yin holati: servislar va GameState

Ikkalasi ham har sahna yuklanishidan omon qoladi; editor ularni Play boshlanganda va to'xtaganda tozalaydi.

**`Services`** — har turdan bitta C++ obyekt:

```cpp
struct Inventory { std::vector<std::string> Items; };

auto& inventory = GetOrCreateService<Inventory>();    // Services::GetOrCreate<Inventory>()
inventory.Items.push_back("Kalit");
if (Inventory* inv = GetService<Inventory>()) { /* ... */ }
Services::Register<AudioDirector>(/* konstruktor argumentlari */);
Services::Unregister<AudioDirector>();
```

**`GameState`** — kalit–qiymat ombori (int, float, bool, matn):

```cpp
GameState::Increment("Coins");
GameState::SetString("Player.Name", "Ali");
GameState::SetPersistent("BestScore");     // Save() bilan SaveData'ga yoziladi, Load() bilan qaytadi
GameState::SetInt("BestScore", 120);
GameState::Save();
// O'zgarishni kuzatish: skript yo'q qilinganda kuzatuv ham tugaydi.
WatchGameState("Coins", [this](std::string_view) { RefreshHud(); });
```

Editorning **Sahnalar** oynasi Play paytida qiymatlarni (tahrirlash mumkin) va servislar ro'yxatini ko'rsatadi.

---

## 13. Sub-sahnalar

**Sub-sahna** (`SubSceneComponent`) — sahnaning bir bo'lagini alohida faylda saqlash: darajaning xonalari,
umumiy yoritish to'plami, bir nechta darajada ishlatiladigan bo'lak. Obyektga **Sub-sahna** komponentini qo'shing
va unga `.zscene` faylini bering (kontent brauzeridan tashlab).

| Maydon | Ma'nosi |
|---|---|
| `ScenePath` (**Sahna**) | qo'yiladigan sahna fayli |
| `LoadOnStart` (**Birga yuklash**) | sahna bilan birga yuklanadi; o'chirilgan bo'lsa — skript so'raganda |
| `ApplyTransform` (**Obyekt joyiga**) | ildiz obyektlari shu obyektning dunyo transformiga ko'chiriladi |
| `ShowInEditor` (**Editorda chizish**) | editorning sahna ko'rinishida joyida chiziladi |

**O'yinda:** sahna yuklanganda (Start, `Single`, `Additive`, fonda yuklash) uning faol sub-sahna obyektlari (o'zi ham,
ota-onalari ham yoqilgan — editor ko'rsatganidek) o'sha kadrning o'zida **qo'shimcha sahna** sifatida yuklanadi —
o'z obyektlari, skriptlari va fizika dunyosi bilan. O'chirilgan ota ostidagi obyekt sub-sahnasini faqat skript
`LoadSubScene()` chaqirganda olib keladi. Ildizlar obyekt transformi bo'yicha **bir marta**, yuklanishda joylanadi (fizika jismlari shu joyda yaratiladi);
obyektni keyin surish sub-sahnani surmaydi. Sub-sahnaning o'z sub-sahnalari ham yuklanadi (eng ko'pi bilan
8 qavat: asosiy sahnaning sub-sahnalari 1-qavat; zanjirda allaqachon bor fayl — halqa — o'tkazib yuboriladi). Egasi yopilganda uning sub-sahnalari ham
yopiladi; `Single` yuklash hammasini yopadi. Egasining skriptlari sub-sahnani `OnStart` da ko'radi
(`OnCreate` da hali yo'q).

```cpp
// Uchinchi xona o'yinchi yaqinlashganda ochiladi (komponentda "Birga yuklash" o'chirilgan).
class RoomTrigger : public ScriptableEntity
{
public:
    void OnTriggerBegin(Entity other) override
    {
        if (other.HasTag("Player") && !GetSubScene())
            LoadSubScene();               // kadr oxirida, shu obyekt joyiga
    }
    void OnTriggerEnd(Entity other) override
    {
        if (other.HasTag("Player"))
            UnloadSubScene();
    }
};
ZM_REGISTER_SCRIPT(RoomTrigger)
```

`SceneManager` darajasida: `GetSubScene(host)`, `GetSubSceneOwner(scene, &hostID)`, `IsSubScene(scene)`,
`LoadSubScene(host)`, `UnloadSubScene(host)`, `OffsetRoots(scene, matrix)`.

**Editorda:** sub-sahna obyekt joyida, **faqat ko'rish uchun** chiziladi (o'z yorug'liklari bilan, osmonsiz;
sichqoncha bilan tanlab bo'lmaydi — bosish uning orqasidagi narsaga o'tadi). Ierarxiyada obyekt ostida
sub-sahnaning obyektlari kulrang qatorlarda ko'rinadi; sarlavha qatorini ikki marta bosish yoki inspektordagi
qalam tugmasi faylni tahrirlash uchun ochadi (ochiq sahna avval saqlanadi). Fayl boshqa joyda o'zgarsa, ko'rinish
o'zi yangilanadi. Inspektor fayl tarkibini (obyektlar soni, ta'rif) ko'rsatadi; Play paytida "yuklangan /
yuklanmagan" holati va **Yuklash** / **Yopish** tugmalari chiqadi. Eksport build ro'yxatidagi sahnalar
ishlatadigan sub-sahna fayllarini ham o'yinga qo'shadi.

---

## 14. Yuklash vaqti profiler'da

Har sahna yuklanishi bosqichlar bo'yicha o'lchanadi (`SceneLoadTiming`):

| Bosqich | Nima |
|---|---|
| O'qish | faylni diskdan o'qish |
| JSON | matnni (yoki binar keshni) tahlil qilish |
| Migratsiya | eski versiyadan joriysiga o'tkazish |
| Obyektlar | obyekt va komponentlarni yaratish, ierarxiyani bog'lash |
| Rasmlar (fon) | fonda yuklashda rasmlarni oldindan dekodlash (asosiy oqimni to'xtatmaydi) |
| Assetlar | tekstura, mesh, shrift, animatsiyalarni yuklash (GPU) |
| Audio | sahna oldindan yuklaydigan ovoz kliplari |
| Fizika | 2D va 3D fizika dunyolari, jismlar, kolliderlar |
| Skriptlar | `OnCreate` va `OnEnable` |

Natija:

- **konsolga** yoziladi:
  `Sahna yuklash vaqti: Level1 (yagona): 12.40 ms, 97 obyekt - O'qish 0.10, JSON 0.84, ... Fizika 0.79, Skriptlar 0.17`;
- **profiler** daraxtida o'sha kadrning `Sahna yuklash` bo'limi ostida bosqichlar ko'rinadi (fonda yuklashda —
  `Sahna yuklash (fon)` ostida `Obyektlar` va `Assetlar`);
- editorning **Statistika** va **Profiler** oynalarida **"Sahna yuklash vaqti"** bo'limi: oxirgi yuklash rangli
  chiziq (bosqichlar ulushi, ustiga olib borilsa — bosqich nomi va vaqti), so'nggi 32 ta yuklash jadvali
  (bosilgani yuqorida batafsil), **Tozalash** va **Nusxa olish** tugmalari. Statistika overlay'ida oxirgi yuklash
  vaqti. Editorda sahna ochish (`editor`), Play boshlanishi (`boshlanish`), almashuvlar (`yagona`,
  `qo'shimcha`) va fonda yuklashlar (`fonda`: asosiy oqim vaqti, kadrlar soni va umumiy davomiyligi bilan)
  yoziladi;
- `ZAMIN_PLAYER_STATS=1` bilan player chiqishda barcha yuklashlarni logga yozadi.

```cpp
SceneLoadTiming last;
if (SceneLoadProfiler::GetLast(last))
    ZM_INFO("{} {:.1f} ms, eng sekini: {}", last.SceneName, last.TotalMilliseconds(),
            SceneLoadTiming::StageName(last.GetSlowestStage()));
// Fonda yuklash: op->GetTiming() (asosiy oqim vaqti - MainThreadMilliseconds(), kadrlar - Frames)
```

---

## 15. Katta sahnalar: tez saqlash va yuklash, binar kesh

Serializatsiya katta sahnalar uchun tezlashtirildi:

- sahna serializeri Debug yig'ishda ham `-O2` bilan kompilyatsiya qilinadi va kutubxonada birinchi bog'lanadi
  (JSON kutubxonasining umumiy kodi uning optimallashgan nusxasi bo'ladi) — CMake opsiyasi
  `ZAMIN_OPTIMIZE_SERIALIZER_IN_DEBUG` (standart yoqilgan; debugger'da qadam-baqadam yurish uchun o'chiring);
- kutubxona o'z funksiyalarini chaqiradi (`-Bsymbolic-functions`): optimallashmagan dastur nusxalari aralashmaydi;
- matn bitta buferga yoziladi, UUID bo'yicha saralash oldindan o'qilgan ID'lar ustida, asset yo'llari bir marta
  tekshiriladi (minglab sprite bir xil teksturani nomlaganda), katta `ChildOrder` ro'yxatlari xesh to'plamlar
  bilan, fayl bir o'qishda o'qiladi.

**Binar kesh (`.zsceneb`)** — sahnaning shu daraxti MessagePack ko'rinishida (kichik sarlavha bilan: `ZMSB`, kesh
formati, manba matn hajmi). U ~3 marta kichik va joriy formatda (migratsiyasiz). `SceneSerializer::Deserialize`
`Level1.zscene` o'rniga yonidagi `Level1.zsceneb` ni o'qiydi, agar u shu hajmdagi matndan yasalgan va undan eski
bo'lmasa; aks holda — matnni; buzilgan kesh ham matnga qaytadi. Fonda yuklash ham keshni ishlatadi. O'yin
eksportida **"Sahnalarni binar keshga ham yozish"** belgisi (standart yoqilgan) har sahna yoniga kesh yozadi.

```cpp
SceneSerializer::WriteBinaryCache("Game/Assets/Scenes/Level1.zscene");   // istalgan oqimda
std::vector<uint8_t> bytes = SceneSerializer(scene).SerializeToBinary();
SceneSerializer(other).DeserializeFromBinary(bytes);
SceneSerializer::SetUseBinaryCache(false);                              // keshni e'tiborsiz qoldirish
```

10 000 obyekt (sprite, jism, skript, matn, ierarxiya; 8.8 MB matn), `ZaminBench --serialize`, 3 marta mediana:

| | Debug, oldin | Debug, keyin | Release |
|---|---|---|---|
| Saqlash (matn) | 1346 ms | 163 ms | 144 ms |
| Yuklash (matn) | 2076 ms | 222 ms | 190 ms |
| Yuklash (binar kesh, 3.2 MB) | — | 140 ms | 114 ms |

Yuklashning qolgan qismi asosan obyektlar va komponentlarni yaratish (10 000 ta ~80 ms).

---

## 16. Editorda sahnalar

- **Sahnalar oynasi** — loyihaning barcha sahnalari kartalar yoki ro'yxat ko'rinishida: rasmi, build indeksi,
  boshlang'ich sahna belgisi, eski format ("v1") belgisi, teglari, ta'rifi. Qidiruv (nom, ta'rif, `#teg`),
  "faqat build" filtri; ochish, build ro'yxatiga qo'shish / olib tashlash, yoqish / o'chirish, boshlang'ich qilish,
  rasmlarni chizish, kontent brauzerida ko'rsatish. Play paytida yuklangan sahnalar (faoli, DontDestroyOnLoad,
  fonda yuklanayotganlar, o'tish), ularni faol qilish, yopish, ierarxiyada ko'rsatish, loyiha sahnasini o'yinga
  qo'shimcha yoki so'nib o'tish bilan yuklash, `GameState` va servislar.
- **"Play bilan"** — Play boshlanganda ochiq sahna yoniga qo'shimcha yuklanadigan sahnalar (shu editor sessiyasi uchun).
- **Stop** — o'yin sahna almashtirgan bo'lsa ham editor tahrirlash sahnasini, tanlovni, kamera ko'rinishini va
  2D rejimini qaytaradi ("Editor holati tiklandi").
- **Sahna sozlamalari → Ma'lumot** — ta'rif, muallif, teglar, format versiyasi, "Rasmni hozir chizish".
- **Sub-sahnalar** — 13-bo'lim; **yuklash vaqti** — 14-bo'lim; **yuklash xatolari** — 10-bo'lim.

Avtomatlashtirish (skrinshotlar, CI) uchun muhit o'zgaruvchilari: `ZAMIN_EDITOR_SCENE`, `ZAMIN_EDITOR_PLAY`,
`ZAMIN_EDITOR_PLAY_ADDITIVE`, `ZAMIN_EDITOR_PLAY_SCENE`, `ZAMIN_EDITOR_STOP_FRAME`, `ZAMIN_EDITOR_SCENES`,
`ZAMIN_EDITOR_LOAD_REPORT`, `ZAMIN_EDITOR_FOCUS`, `ZAMIN_EDITOR_STATS=load` (Statistika oynasi, yuklash vaqti
bo'limi ochiq).

---

## 17. Namuna: Platformer2D sahnalar oqimi

`Projects/Platformer2D` (`Source/SceneFlow.cpp`):

- **Menu.zscene** — `GameMenu` (`Prefabs/GameMenu.zprefab` nusxasi): "O'ynash" tugmasi yuklanish ekraniga so'nib o'tadi, "Sozlamalar" (ekran o'lchami, to'liq ekran, VSync, sifat, gamma, ovoz, til), "Chiqish" ([runtime.md](runtime.md#10-oyin-menyusi-namunasi)).
  `GameSession` shu yerda yaratiladi va `DontDestroyOnLoad` bilan butun o'yin davomida yashaydi: musiqani uzmaydi,
  tangalar va darajalarni `GameState` da, darajalar bo'yicha statistikani `SessionStats` servisida yig'adi,
  rekordni `SaveData` ga saqlaydi; HUD faqat darajalarda ko'rinadi (`OnActiveSceneChanged`).
- **Loading.zscene** — `LoadingScreen`: 1-daraja fonda yuklanadi, progress bar va bosqich matni, eng kamida
  1.6 s, keyin so'nib o'tadi.
- **SubScenes.zscene** — bitta `Parts/Camp.zscene` bo'lagi uch joyda: ikkitasi sahna bilan (biri qiya va
  kattalashtirilgan obyekt ostida), uchinchisini `DelayedSubScene` skripti 1.5 s dan keyin `LoadSubScene()` bilan
  yuklaydi. Har lagerning yashigi o'z sub-sahnasining fizika dunyosida tushadi.

```bash
./build/bin/ZaminPlayer Projects/Platformer2D --scene Menu        # menyu -> yuklanish -> 1-daraja
./build/bin/ZaminPlayer Projects/Platformer2D --scene SubScenes   # sub-sahnalar
```

---

## 18. Testlar va benchmark

`Tests/ZaminTests.cpp` (grafikasiz): `SceneManagerAdditiveScenes`, `SceneManagerDontDestroyOnLoad`,
`SceneManagerAsyncLoad`, `SceneManagerFadeTransition`, `BuildSceneList`, `SceneEventsOrder`,
`LoadSceneByBuildIndex`, `SceneQueryIndices`, `SceneLoadErrors`, `SceneSaveDeterministic`,
`SceneVersionMigration`, `SceneMetadataAndThumbnail`, `ServicesRegistry`, `GlobalGameState`,
`SceneLoadTimingBreakdown`, `SceneBinaryCache`, `LargeSceneSerialization`, `SubScenes` va hammasini birga
sinaydigan `SceneManagementFlow` (menyu → fonda so'nish bilan daraja → uning sub-sahnasi, doimiy menejer,
GameState, servislar, hodisalar, vaqt).

```bash
ZAMIN_TESTS=Scene ./build/bin/ZaminTests        # nomida "Scene" bor testlar
./build/bin/ZaminBench --serialize               # katta sahna saqlash / yuklash
./build/bin/ZaminBench --queries                 # qidiruv indekslari
./build/bin/ZaminBench --serialize --prefabs     # bog'langan prefab nusxalari (--instances N, --json fayl)
```

**Serializatsiya fuzz testi** (`SerializationFuzz`): tasodifiy JSON va namuna fayllarining (`Menu.zscene`,
`Level1.zscene`, barcha `.zprefab`) buzilgan nusxalari — kesilgan matn, turi almashgan qiymat, o'chirilgan kalit,
takrorlangan ID, ota obyekt halqasi, juda katta / manfiy sonlar, chuqur ichma-ichlik, buzilgan prefab bog'lanishlari —
har bir o'quvchidan o'tadi: sahna, binar kesh, tiklash (`RecoverFromString`), obyektlar to'plami (prefab, bufer),
migratsiya zanjiri, `SceneRecovery::Salvage`, prefab fayli o'zgarganda nusxalar sinxroni, prefab rejimi. Shartlar:
hech qanday istisno tashqariga chiqmaydi; ierarxiya o'zaro mos (ota obyekt farzandlari ro'yxatida, halqa yo'q);
kirishdagidan ko'p obyekt chiqmaydi; `Serialize(Load(Serialize(Load(x))))` = `Serialize(Load(x))`.
`ZAMIN_FUZZ_SEED` (seed chop etiladi), `ZAMIN_FUZZ_ITERATIONS` (standart 500, ~3 s). Xato topilgan kirish
`Tests/Fixtures/Fuzz/` ga yoziladi va keyingi har ishga tushishda birinchi tekshiriladi. Fuzz test topgan xato:
cheksiz float (`1e308` float maydonga o'qilganda) `null` bo'lib yozilar va qayta o'qilmas edi — endi eng katta float
qiymati bo'lib yoziladi (NaN: 0). 20 000 iteratsiya AddressSanitizer bilan toza o'tadi.

**Prefab benchmark** (`ZaminBench --serialize --prefabs`): 20 obyektli sintetik prefab (ichida boshqa prefab, ichki
havolalar), 1000 ta bog'langan nusxa (20 000 obyekt), o'ndan biri 3 ta override bilan; namunaviy `GameMenu`
(44 UI obyekt) ×100. Release, mediana:

| | Enemy ×1000 | GameMenu ×100 | Byudjet |
|---|---|---|---|
| `SyncScene`: hammasi yangi (hash tez yo'li) | 4.8 ms | 0.8 ms | ≤ 5 ms |
| `SyncScene`: majburiy qayta sinxron | 397 ms | 200 ms | ≤ 300 ms (20k obyekt) |
| `RecordOverrides`: bitta obyekt | 0.014 ms | 0.031 ms | ≤ 0.2 ms |
| Bog'lanish baytlari (fayldagi ulushi) | 8.7 % | 3.3 % | ≤ 10 % |
| Yuklashga qo'shimcha (JSON) | 10.8 % | 7.3 % | ≤ 5 % |
| Apply → hamma nusxalar | 462 ms | 218 ms | — |
| `Instantiate(path)` ×1000: har safar o'qib / `PrefabCache` | 283 / 111 ms | 1079 / 328 ms | — |

Majburiy qayta sinxron va yuklashdagi qo'shimcha byudjetdan oshadi: sinxron har nusxa obyektini prefab va nusxa JSON'i
sifatida solishtiradi (20 000 obyektni ikki marta yozish ~300 ms), bog'lanishlar esa har obyektga kichik
`"Editor"` obyektini qo'shadi. Ikkalasi faqat prefab o'zgarganda / sahna ochilganda ishlaydi; har undo / redo dan
keyingi tez yo'l byudjet ichida.

---

## 19. Cheklovlar

- Barcha sahnalarni bitta kamera chizadi; har sahna faqat o'z obyektlarini yoritadi.
- Turli sahnalardagi jismlar bir-biriga tegmaydi, raycast faqat o'z sahnasini ko'radi (sub-sahnalar ham).
- Fonda yuklashda glTF modellar va audio hali asosiy oqimda (kadrlarga bo'lingan bosqichda) yuklanadi.
- So'nish faqat rang orqali (cross-fade yo'q).
- Sub-sahna joyi yuklanishda bir marta qo'llanadi; egasi o'chirilsa, sub-sahna qoladi (uni `UnloadSubScene` bilan
  yoping). Editor sub-sahnaning o'z sub-sahnalarini ko'rsatmaydi (o'yinda ular yuklanadi).
- Binar kesh JSON tahlilini tezlashtiradi; obyektlarni yaratish vaqti o'zgarmaydi.
- Migratsiyalar faqat sahna fayllariga qo'llanadi (prefab va bufer matni eski kalitlarni o'zi o'qiydi).

---

## 20. Qisqa API ma'lumotnomasi

**ScriptableEntity:**

| Chaqiruv | Ma'nosi |
|---|---|
| `LoadScene(path / name / index, mode, transition)` | kadr oxirida yuklash |
| `LoadSceneAsync(path / index, mode, transition)` | fonda yuklash → `Ref<AsyncOperation>` |
| `UnloadScene(nameOrPath)` | yopish |
| `LoadNextScene(t)`, `ReloadScene(t)` | build ro'yxatidagi keyingisi / qayta |
| `GetSceneBuildIndex()`, `GetActiveSceneBuildIndex()`, `GetBuildSceneCount()` | build indekslari |
| `DontDestroyOnLoad()`, `DontDestroyOnLoad(entity)` | doimiy sahnaga ko'chirish |
| `LoadSubScene(host)`, `UnloadSubScene(host)`, `GetSubScene(host)` | sub-sahnalar (argumentsiz — o'zi) |
| `GetSceneManager()` | `SceneManager*` (Play/player'da) |
| `OnSceneLoaded`, `OnSceneUnloaded`, `OnActiveSceneChanged` | hodisalar |
| `FindEntity`, `FindEntitiesWithTag`, `FindObjectsOfType<T>` | barcha yuklangan sahnalar bo'ylab qidiruv |
| `GetService<T>()`, `GetOrCreateService<T>()`, `WatchGameState(key, fn)` | global holat |

**SceneManager:** `Start`, `Stop`, `OnUpdate`, `ProcessRequests`, `LoadScene`, `LoadSceneAsync`, `UnloadScene`,
`GetActiveScene`, `SetActiveScene`, `GetLoadedScenes`, `GetAllScenes`, `GetSceneByName`, `GetSceneByPath`,
`GetScenePath`, `GetBuildIndex`, `GetDontDestroyOnLoadScene`, `DontDestroyOnLoad`, `GetSubScene`,
`GetSubSceneOwner`, `LoadSubScene`, `UnloadSubScene`, `OffsetRoots`, `FindEntityByName`, `FindEntityByUUID`,
`FindEntitiesWithTag`, `FindObjectsOfType<T>`, `GetEventBus`, `SetTimeScale`, `SetGamePaused`,
`GetLoadReports` / `TakeLoadReports`, `IsTransitioning`, `GetTransitionAlpha`, `SetAsyncBudget`.

**AsyncOperation:** `GetProgress`, `GetStage`, `IsReady`, `IsDone`, `HasFailed`, `GetError`, `GetReport`,
`SetAllowSceneActivation`, `GetScene`, `GetTiming`.

**SceneSerializer:** `Serialize` / `Deserialize`, `SerializeToString` / `DeserializeFromString`,
`SerializeToBinary` / `DeserializeFromBinary`, `WriteBinaryCache`, `GetBinaryCachePath`, `SetUseBinaryCache`,
`ReadSceneInfo`, `GetLoadReport`, `GetMigration`, `GetTiming`. Bosqichma-bosqich yuklash: `SceneLoadJob`
(`Parse` / `ParseString` / `ParseBinary` istalgan oqimda, `Build(scene, budgetMs)` asosiy oqimda).

**Vaqt:** `SceneLoadTiming` (`Milliseconds[stage]`, `TotalMilliseconds`, `MainThreadMilliseconds`,
`GetSlowestStage`, `Format`), `SceneLoadProfiler` (`GetHistory`, `GetLast`, `Record`, `Clear`, `SetLogging`),
`Scene::GetRuntimeStartTiming()`.

**Global:** `Services` (`Register`, `Get`, `GetOrCreate`, `Unregister`, `Clear`), `GameState` (`Set*` / `Get*`,
`Increment`, `SetPersistent`, `Save`, `Load`, `AddListener`).
