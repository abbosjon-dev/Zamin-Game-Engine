# Zamin Engine: prefab'lar

Bu hujjat prefab'larni tushuntiradi: prefab fayli va uning nusxalari, nusxa prefabga qanday bog'langani, nusxadagi
o'zgarishlar (override), Apply / Revert / bog'lanishni uzish, ichma-ich prefab'lar va variantlar, prefab rejimi,
obyektlar orasidagi havolalar, prefab fayli tashqarida o'zgarganda (hot reload), undo, nusxalash buferi, fayl
formati va versiyalar, buzilgan fayllarni tiklash, eksportdagi tekshiruv, skriptlardan `Instantiate`, ishlash
tezligi va cheklovlar. Kod misollari `#include <Zamin/Zamin.h>`, `using namespace Zamin;` bilan.

Mundarija:

1. [Asosiy tushunchalar](#1-asosiy-tushunchalar)
2. [Prefab yaratish va sahnaga qo'yish](#2-prefab-yaratish-va-sahnaga-qoyish)
3. [Nusxadagi o'zgarishlar (override)](#3-nusxadagi-ozgarishlar-override)
4. [Apply, Revert, bog'lanishni uzish](#4-apply-revert-boglanishni-uzish)
5. [Ichma-ich prefab'lar](#5-ichma-ich-prefablar)
6. [Variantlar](#6-variantlar)
7. [Prefab rejimi](#7-prefab-rejimi)
8. [Obyektlar orasidagi havolalar (EntityRef)](#8-obyektlar-orasidagi-havolalar-entityref)
9. [Prefab fayli o'zgarganda (hot reload)](#9-prefab-fayli-ozgarganda-hot-reload)
10. [Undo va prefab fayllari](#10-undo-va-prefab-fayllari)
11. [Nusxalash buferi (clipboard)](#11-nusxalash-buferi-clipboard)
12. [Fayl formati](#12-fayl-formati)
13. [Versiya va migratsiya](#13-versiya-va-migratsiya)
14. [Buzilgan fayllarni tiklash](#14-buzilgan-fayllarni-tiklash)
15. [Eksportda prefab'larni tekshirish](#15-eksportda-prefablarni-tekshirish)
16. [Skriptlardan: Instantiate va kesh](#16-skriptlardan-instantiate-va-kesh)
17. [Ishlash tezligi](#17-ishlash-tezligi)
18. [Testlar](#18-testlar)
19. [Cheklovlar](#19-cheklovlar)
20. [Qisqa API ma'lumotnomasi](#20-qisqa-api-malumotnomasi)

---

## 1. Asosiy tushunchalar

**Prefab** (`.zprefab`) — obyektlar daraxtining fayli: bitta ildiz obyekt va uning bolalari, hamma komponentlari
bilan. U sahna faylining bir bo'lagi kabi yoziladi (§12) va bir necha joyda ishlatiladi: editorda sahnaga sudrab
qo'yiladi, o'yinda skript `Instantiate("Prefabs/Enemy.zprefab")` bilan yaratadi.

**Nusxa (instance)** — sahnadagi prefabdan yaratilgan obyektlar. Editorda nusxa prefabga **bog'langan** bo'ladi:

- nusxa **ildizi** prefab yo'lini biladi (`EditorMetadataComponent::Prefab`, `Scene::GetPrefabSource`);
- nusxaning har bir obyekti prefabdagi qaysi obyekt ekanini biladi (`PrefabLinkComponent::PrefabEntity` — prefab
  faylidagi `"Entity"` ID'si);
- har obyekt prefabdan nimasi bilan farq qilishini saqlaydi (override yo'llari, §3).

Bog'lanish faqat editorniki: o'yin (`ZaminPlayer`) uni o'qimaydi (`SceneSerializer::SetReadPrefabLinks(false)`),
eksport esa nusxa fayllaridan olib tashlaydi (§15). Sahnada nusxaning hamma ma'lumoti baribir to'liq yoziladi —
o'yin prefab faylini qidirmaydi.

Prefab o'zgarsa, nusxalar unga ergashadi: nusxa o'zgartirmagan hamma narsa yangi qiymatni oladi, nusxa
o'zgartirgan qiymatlar esa o'zgarmaydi. Bu **sinxronlash** (`PrefabUtility::SyncInstance`) sahna ochilganda,
undo/redo dan keyin, prefab rejimi yopilganda va prefab fayli tashqarida o'zgarganda ishlaydi.

**Nusxaga tegishli qiymatlar** hech qachon override bo'lmaydi, prefabdan kelmaydi va Apply ularni yozmaydi —
nusxa sahnada qayerda turgani:

| Qiymat | Nega |
|---|---|
| ildizning `Transform/Translation`, `Transform/Rotation` | har nusxa o'z joyida |
| ildizning `UITransform/Position` | UI nusxaning joyi |
| ildizning nomi (`Tag`) | "Enemy (2)" kabi nomlar |
| ildizning ota obyekti va ichidagi o'rni | ierarxiyadagi joyi |

Ildizning `Scale` va `Active` qiymatlari esa oddiy override'lar.

---

## 2. Prefab yaratish va sahnaga qo'yish

**Saqlash.** Ierarxiyada obyektni o'ng tugma → *Prefab sifatida saqlash* (yoki Obyekt menyusi). Fayl
`Assets/Prefabs/<Nom>.zprefab` ga yoziladi va darhol `.zmeta` oladi. Shu nomli boshqa prefab bo'lsa, so'raladi:
*Almashtirish* / *Yangi nom ("Enemy (1)")* / *Bekor qilish*. Saqlangan obyekt shu prefabning **birinchi nusxasiga**
aylanadi (o'z UUID'lari prefab ID'lari bo'ladi, o'zgarishi yo'q). Allaqachon boshqa prefab nusxasi bo'lgan obyekt
saqlansa, so'raladi: *Variant* (§6) yoki *Asl prefab* (mustaqil prefab). O'sha prefabning o'z nusxasi saqlansa —
bu Apply (§4).

**Qo'yish.** Kontent brauzerdan prefabni viewport'ga yoki ierarxiyaga sudrang:

- 3D: sichqoncha ostidagi mesh / collider ustiga yoki y=0 tekisligiga (grid snapping yoqilgan bo'lsa grid'ga);
  2D: z=0; UI prefab: sahnaning birinchi Canvas'iga;
- bir nechta tanlangan prefab bir qatorga, kamera o'ng tomoni bo'ylab teng oraliq bilan qo'yiladi;
- **Ctrl** bosib qo'yish — prefabda saqlangan joyda;
- **Shift** — *prefab qo'yish rejimi*: har bosishda yana bitta nusxa, Esc yoki o'ng tugma bilan chiqiladi;
- ildiz nomlari takrorlanmaydi ("Enemy (2)"), har tashlash bitta undo qadami.

Nusxa ierarxiyada **ko'k** nom va quti belgisi bilan ko'rinadi, ichidagi prefab nusxasi ildizi kichik quti
belgisi, variant nusxasi ikki varaq belgisi bilan. O'zgargan obyekt qatori oxirida nuqta, nusxaga qo'shilgan
obyekt oldida yashil "+", prefabi topilmagan / buzilgan nusxada qizil ogohlantirish bor.

---

## 3. Nusxadagi o'zgarishlar (override)

Nusxa obyektining prefabdan farqi **yo'llar** ro'yxati sifatida saqlanadi (`PrefabLinkComponent::Overrides`):

| Yo'l | Ma'nosi |
|---|---|
| `Tag`, `Active` | obyekt nomi / faolligi (ildizdan boshqa obyektda) |
| `Parent` | nusxa ichida boshqa ota obyektga o'tkazilgan |
| `ChildOrder` | prefab bolalari boshqa tartibda |
| `Rigidbody2D` | butun komponent nusxaga qo'shilgan |
| `SpriteRenderer/Color` | bitta qiymat (obyektlar ichiga kiriladi; massivlar va sonlar bitta qiymat) |
| `NativeScript/Fields/Speed` | skriptning bitta maydoni |

Nomdagi `/` va `~` JSON Pointer kabi yoziladi (`~1`, `~0`). Olib tashlangan prefab komponentlari alohida
ro'yxatda (`RemovedComponents`), nusxada o'chirilgan prefab obyektlari ildizda (`RemovedEntities`, faqat eng
yuqoridagisi). Prefab qiymatiga qaytarilgan qiymat ro'yxatdan chiqadi.

Ro'yxatlar faqat undo qadami yozilganda yangilanadi (tahrir tugaganda, `CommitUndo`): o'sha qadam tekkan
nusxalar prefab bilan solishtiriladi. Saqlangan ro'yxat kerak, chunki prefab qiymatiga **teng** bo'lgan, lekin
nusxa ataylab o'zgartirgan qiymat prefab o'zgarganda ham nusxada qolishi kerak.

**Inspektorda:**

- nom ostida prefab paneli: *Prefab: Enemy.zprefab* (variant bo'lsa asosi ham), [Ochish], [Tanlash],
  *O'zgarishlar (N)* ro'yxati (obyekt va komponent bo'yicha, har qatorda Qo'llash / Qaytarish), [Qo'llash],
  [Qaytarish];
- o'zgargan komponent sarlavhasi chap tomonida chiziq (ustiga kelsa — yo'llar), qo'shilgan komponentda "+",
  olib tashlangan komponent xira sarlavha bilan: *Qaytarish* / *Olib tashlashni qo'llash*;
- komponent ⚙ menyusida *Komponentni prefabga qo'llash* va *Komponentni asliga qaytarish*;
- o'zgargan qiymat qatorida belgi; o'ng tugma: *Prefabga qo'llash*, *Asliga qaytarish*, ichma-ich prefab
  qiymatini *Qo'llash: Weapon.zprefab*. Skript maydonlari ham belgilanadi.

---

## 4. Apply, Revert, bog'lanishni uzish

**Apply (qo'llash)** nusxaning o'zgarishini prefab fayliga yozadi, keyin prefabdan yasalgan boshqa prefab'lar va
sahnadagi boshqa nusxalar unga ergashadi. Darajalari (`PrefabScope`):

| Daraja | Nima yoziladi |
|---|---|
| `Field` | bitta qiymat (`SpriteRenderer/Color`) |
| `Component` | bitta komponent (qo'shilgani ham, olib tashlangani ham) |
| `Entity` | obyektning hamma o'zgarishi; nusxaga qo'shilgan obyekt — prefabga yangi obyekt bo'lib |
| `Instance` | butun nusxa |

Faqat tegilgan qiymatlar faylga yoziladi (fayldagi boshqa matn o'zgarmaydi), prefab ID'lari saqlanadi. Nusxadan
tashqaridagi obyektga havola qiluvchi qiymat yozilmaydi — u nusxaning o'zgarishi bo'lib qoladi (§8). Prefab
sikli hosil qiladigan Apply rad etiladi. Yangiroq formatdagi yoki o'qishda xato bergan prefab fayliga yozilmaydi.

**Revert (asliga qaytarish)** qiymatlarni prefabdagidek qiladi — o'sha darajalarda. *Hammasini qaytarish* nusxaga
qo'shilgan obyektlarni o'chiradi (oldin so'raladi) va o'chirilgan prefab obyektlarini qayta yaratadi.
*O'chirilganlarni tiklash* faqat o'chirilgan obyektlarni qaytaradi, boshqa hech narsaga tegmaydi.

**Bog'lanishni uzish** nusxani oddiy obyektlarga aylantiradi (prefab o'zgarsa ham o'zgarmaydi). *To'liq ajratish*
ichidagi boshqa prefab nusxalarini ham ajratadi.

```cpp
// Editor kodi yoki test (o'yin vaqtida emas):
PrefabDatabase db;                                       // faol loyihaning Assets papkasi
Entity enemy = PrefabUtility::InstantiateLinked(*scene, db, "Prefabs/Enemy.zprefab");
enemy.GetComponent<SpriteRendererComponent>().Color = { 0, 1, 0, 1 };
PrefabUtility::RecordOverrides(enemy, db);               // editor buni har undo qadamida qiladi
PrefabApplyResult r = PrefabUtility::Apply(enemy, PrefabScope::Field, "SpriteRenderer/Color", db);
// r.Written: yozilgan fayllar, r.Skipped: nima yozilmadi va nega, r.Files: undo uchun
PrefabUtility::Revert(enemy, PrefabScope::Instance, {}, db);
```

---

## 5. Ichma-ich prefab'lar

Prefab ichida boshqa prefab nusxasi bo'lishi mumkin (Enemy ichida Weapon). Qoidalar:

- **Bitta qatlam har faylda.** Sahnadagi Enemy nusxasining hamma obyekti, Weapon'dan kelganlari ham, Enemy
  nusxasiga tegishli va Enemy ID'larini oladi. Weapon qatlami Enemy faylining o'zida saqlanadi.
- **Fayllar tekis.** Enemy fayli Weapon obyektlarini to'liq o'z ichida saqlaydi — o'yin va `Instantiate` hech
  qanday zanjirni hal qilmaydi.
- **Xotirada yangilanadi.** Weapon o'zgarib, Enemy fayli diskda eskirib qolsa ham, editor Enemy'ni o'qiyotganda
  ichidagi Weapon'ni yangilaydi (`PrefabDatabase` shablonlari). Diskdagi Enemy fayli editor qilgan o'zgarishlarda
  (Apply, prefab rejimida saqlash, hot reload) va eksport nusxasida yangilanadi.
- **Qaysi prefabga qo'llash.** Weapon'dan kelgan qiymatni o'ng tugma bilan *Qo'llash: Weapon.zprefab* — qiymat
  Weapon fayliga yoziladi, Enemy faylidagi shu qiymatning o'zgarishi o'chiriladi (Enemy uni meros oladi) va Enemy
  qayta yoziladi.
- **Sikl va chuqurlik.** Prefab o'zini (yoki o'zini ishlatadigan prefabni) o'z ichiga olmaydi — bunday Apply rad
  etiladi, faylda bo'lsa xabar beriladi. Chuqurlik chegarasi 16 daraja (`PrefabDatabase::MaxDepth`).

---

## 6. Variantlar

**Variant** — boshqa prefabdan (asosidan) meros oluvchi prefab. Uning ildizi asosning nusxasi (o'z faylida ildiz
bog'lanishi yozilgan), shuning uchun asos o'zgarsa variant ham o'zgaradi — variant o'zgartirmagan hamma narsa.
Variantdan variant ham bo'ladi (zanjir, 16 darajagacha). Variant fayli ham tekis: o'yin uni oddiy prefab kabi
o'qiydi.

- *Variant yaratish* (kontent brauzer, asset inspektori, ierarxiya Prefab menyusi): `Enemy Variant.zprefab`
  yaratiladi va prefab rejimida ochiladi.
- *Variant sifatida saqlash* (nusxa ustida): nusxaning o'zgarishlari yangi variantga yoziladi, nusxa variantning
  birinchi nusxasi bo'ladi (o'zgarishi qolmaydi).
- Kontent brauzerda variant ikki varaq belgisi bilan, inspektorda *Variant zanjiri* ko'rinadi.

```cpp
std::string variant = PrefabUtility::CreateVariant(db, "Prefabs/Enemy.zprefab");      // "Prefabs/Enemy Variant.zprefab"
std::vector<std::string> chain = PrefabUtility::GetVariantChain(db, variant);         // { variant, "Prefabs/Enemy.zprefab" }
```

---

## 7. Prefab rejimi

Prefabni alohida, o'z sahnasidek tahrirlash: kontent brauzerda ikki marta bosish, ierarxiya / inspektor *Prefab
rejimida ochish*, asset inspektori. Ochiq sahna (tarixi, tanlovi, kamerasi, saqlanmagan holati) kutib turadi.

- Viewport ustida yo'l: `◀ Sahna "Level1" › Enemy.zprefab › Weapon.zprefab` — [Saqlash] (Ctrl+S) va [Yopish].
  Ichidagi prefab nusxasi ildiziga ikki marta bosish bir daraja chuqurroq ochadi.
- Obyektlar fayldagi ID'larni UUID sifatida oladi — o'zgarishsiz saqlash aynan o'sha baytlarni yozadi.
- Saqlashda bitta ildiz shart (bir nechta bo'lsa *Yangi ildizga o'rash* taklif qilinadi), sikl va yangiroq format
  rad etiladi. Saqlangandan keyin bu prefabdan yasalgan prefab'lar ham yangilanadi.
- Yopilganda pastdagi sahna nusxalari yangilanadi — bitta undo qadami ("Prefab tahrirlandi: Enemy"), u fayl
  yozuvlarini ham olib yuradi.
- Saqlanmagan tahrir bilan chiqishda so'raladi: *Saqlash* / *Saqlamaslik* / *Bekor qilish*.
- Fayl tashqarida o'zgarsa: tahrir bo'lmasa qayta o'qiladi, bo'lsa banner chiqadi.
- Prefab rejimida o'yin ishga tushmaydi, avtomatik saqlash o'chiq.

---

## 8. Obyektlar orasidagi havolalar (EntityRef)

Komponentlardagi `EntityRef` maydonlari (Camera2DFollow nishoni, NavAgent, joint'lar `ConnectedBody`, UI
navigatsiya, skriptning Entity maydonlari ...) obyektni UUID bo'yicha topadi. Ularning hammasini bitta joy biladi:
`Scene::ForEachEntityRef(entity, visit)` (ro'yxati: `Scene::GetEntityRefSites()`; test komponentlar faylidagi har
`EntityRef` a'zosi shu ro'yxatda ekanini tekshiradi).

| Amal | To'plam ichidagi havola | Tashqaridagi havola |
|---|---|---|
| Qo'yish, joylash, skript `Instantiate` | yangi UUID'ga o'tadi | yozilganicha qoladi |
| Sinxron, Revert | prefab ID → nusxa UUID | nusxa o'zgartirgan qiymat sahna UUID'ini saqlaydi |
| Apply | nusxa UUID → prefab ID (ichma-ich qismlarda ham) | yozilmaydi, xabar beriladi, nusxaning o'zgarishi bo'lib qoladi |
| Prefab rejimi | ID'lar allaqachon prefabniki | inspektorda uzilgan zanjir belgisi: "Qo'llashda yozilmaydi (prefabdan tashqarida)" |
| Dublikat (Ctrl+D) | nusxa ichida xaritalanadi (bitta nusxada ham) | qoladi |

`Scene::SavePrefab` prefabdan tashqariga ishora qiluvchi har havolani konsolda ogohlantiradi.

---

## 9. Prefab fayli o'zgarganda (hot reload)

Prefab fayli editordan tashqarida o'zgarsa (matn muharriri, git), editor buni fayl kuzatuvchisi orqali ko'radi:

1. Har o'zgargan `.zprefab` matni o'qiladi va hash'lanadi (`PrefabDatabase::CheckFile`): vaqti va hajmi bir xil
   qolgan o'zgarish ham topiladi; matni o'zgarmagan "tegish" va editorning o'z yozuvlari o'zgarish emas.
2. Shu prefabni ichida ishlatadigan (yoki uning varianti bo'lgan) prefab fayllari tartib bilan — har biri
   o'zi qurilgan prefab'lardan keyin — diskda yangilanadi (faqat matni o'zgarsa; konsolda ro'yxat).
3. Ochiq sahnadagi shu prefab'larning nusxalari yangilanadi (o'zgarishlari saqlanadi). Bu alohida undo qadami
   emas: joriy holat yangi matnni oladi, sahna saqlanmagan deb belgilanadi va xabar chiqadi
   ("Prefab yangilandi: Enemy (3 nusxa)").
4. O'yin (Play / Simulate) paytida hech narsa o'zgarmaydi — nusxalar o'yin to'xtagach yangilanadi. Ishlayotgan
   o'yin skriptlari esa yangi faylni yaratadi (`PrefabCache` fayl vaqtini tekshiradi).
5. Prefab rejimida ochiq prefab: tahrir bo'lmasa qayta o'qiladi, bo'lsa banner.
6. O'chirilgan prefab: nusxalar ma'lumotini saqlaydi va "topilmadi" deb ko'rinadi. Yarim yozilgan fayl o'qilmaydi
   — nusxalar tegilmaydi, keyingi o'zgarishda qayta urinadi.
7. Tashqarida ko'chirilgan prefab: havolalar `.zmeta` UUID'i orqali yangilanadi.

```cpp
// Fayl kuzatuvchisi aytgan prefab'lar (editor buni o'zi qiladi):
PrefabUtility::ReloadResult r = PrefabUtility::ReloadFiles(db, { "Prefabs/Weapon.zprefab" });
// r.Changed, r.Removed, r.Ignored (o'z yozuvlari), r.Rewritten (Enemy, Squad ...), r.Affected
PrefabUtility::SyncScene(*scene, db, &r.Affected);
```

---

## 10. Undo va prefab fayllari

Sahnadagi amallar (qo'yish, Revert, bog'lanishni uzish) oddiy undo qadamlari. Fayl yozadigan amallar — Apply,
prefab sifatida saqlash, variant yaratish, prefab rejimini yopish — **bitta qadam** bo'lib, fayl yozuvlarini ham
olib yuradi (`UndoHistory` `FileEdit`: oldingi va keyingi matn). Undo fayllarga eski matnni qaytaradi (yangi
yaratilgan faylni o'chiradi), redo yangisini yozadi. Fayl orada tashqarida o'zgargan bo'lsa, konsolda
ogohlantirish chiqadi va ustiga yoziladi. Har undo/redo dan keyin nusxalar jimgina prefab'lar bilan
solishtiriladi (hash orqali tez).

---

## 11. Nusxalash buferi (clipboard)

Bufer matni — prefab fayli bilan **bir xil hujjat** (bitta yozuvchi, bitta o'quvchi), `"Kind": "Clipboard"` bilan,
va faqat buferda bo'ladiganlar:

- ierarxiyaning yashirish / qulf belgilari;
- butun nusxalangan prefab nusxalarining bog'lanishi — joylangan nusxa prefabga bog'langan bo'lib qoladi (yangi
  UUID'lar bilan); nusxaning bir qismi (masalan bitta bolasi) oddiy obyektlar bo'lib joylanadi;
- `"World"`: ota obyekti bo'lgan nusxalangan ildizning dunyodagi joyi — u boshqa ota ostiga joylansa ham o'z
  joyida qoladi (o'sha ota ostiga qaytsa — qiymatlari aynan o'zi);
- `"Origin"`: kontent brauzerda prefab nusxalanganda (*Nusxalash (sahnaga joylash uchun)* yoki Ctrl+C) — joylash
  shu prefabning bog'langan nusxasini yaratadi (fayl ko'chirilgan bo'lsa UUID orqali topiladi; topilmasa matndagi
  obyektlar joylanadi).

Joylash (Ctrl+V) obyektlarni **tanlangan obyektning yoniga** (uning ota obyekti ostiga, undan keyin) qo'yadi;
tanlov bo'lmasa — sahnaning yuqori darajasiga. Eski (M20, versiyasiz) bufer matni va prefab fayli matni
avvalgidek joylanadi.

```cpp
std::string text = EntityClipboard::Copy(*scene, { enemy });
EntityClipboard::PasteResult pasted = EntityClipboard::Paste(*scene, &db, text, /*yoniga:*/ selected);
std::string asset = EntityClipboard::CopyPrefab(db, "Prefabs/Enemy.zprefab");   // "Origin" bilan
```

---

## 12. Fayl formati

**Prefab fayli.** Obyektlar daraxt tartibida (ota bolalaridan oldin), ID'lar prefabniki:

```json
{ "ZaminEntities": 1, "Kind": "Prefab", "Version": 5, "Entities": [
  { "Entity": 1, "Tag": "Enemy", "Transform": { ... }, "Rigidbody2D": { ... } },
  { "Entity": 7, "Parent": 1, "Tag": "Weapon", "SpriteRenderer": { ... },
    "Editor": { "Prefab": "Prefabs/Weapon.zprefab", "PrefabUUID": "a41f...", "PrefabEntity": 1,
                "PrefabHash": "5f0c...", "Overrides": [ "SpriteRenderer/Color" ] } },
  { "Entity": 8, "Parent": 7, "Tag": "Muzzle", "Editor": { "PrefabEntity": 2 } } ] }
```

Ichma-ich nusxa (7, 8) to'liq yozilgan va o'z bog'lanish qatlamiga ega. Variantda bog'lanish ildizda bo'ladi.

**Sahnadagi nusxa.** Ma'lumot to'liq, bog'lanish `"Editor"` kaliti ostida:

```json
{ "Entity": 7815000000000101, "Tag": "Bosh menyu", "Transform": { ... },
  "Editor": { "Prefab": "Prefabs/GameMenu.zprefab", "PrefabUUID": "e23d68ebead3b2a7", "PrefabEntity": 1,
              "PrefabHash": "77b20c41aa03b7d4", "Overrides": [ "NativeScript/Fields/Speed" ], "RemovedEntities": [ 17 ] } },
{ "Entity": 7815000000000102, "Parent": 7815000000000101, "Tag": "Overlay",
  "Editor": { "PrefabEntity": 2, "Overrides": [ "UIImage/Color" ], "RemovedComponents": [ "UIAnimation" ] } },
{ "Entity": 991, "Parent": 7815000000000102, "Tag": "Qo'shilgan" }
```

| Kalit (`"Editor"` ichida) | Qayerda | Ma'nosi |
|---|---|---|
| `Prefab`, `PrefabUUID` | nusxa ildizi | prefab yo'li va `.zmeta` UUID'i (fayl ko'chirilsa topiladi) |
| `PrefabEntity` | har bog'langan obyekt | prefabdagi ID; yo'q — nusxaga qo'shilgan obyekt |
| `PrefabHash` | ildiz | oxirgi sinxrondagi prefab hash'i (prefab + ichidagi prefab'lar); teng bo'lsa sinxron kerak emas |
| `Overrides`, `RemovedComponents` | obyekt | §3 |
| `RemovedEntities` | ildiz | nusxada o'chirilgan prefab obyektlari |

Faqat `Prefab` yozilgan ildiz — eski (M21 dan oldingi) nusxa: sahna ochilganda u prefabga bog'lanadi (ildiz
ildizga, bolalar nomi bo'yicha tartib bilan; ma'lumot o'zgarmaydi, farqlar override bo'ladi). Masalan Platformer2D
`Menu.zscene`: "Eski prefab nusxasi bog'landi: Bosh menyu (44/44, 0 override)", sahna saqlanmagan deb belgilanmaydi.

---

## 13. Versiya va migratsiya

Sahna, prefab va bufer bitta versiya zanjiriga ega (`SceneMigration::CurrentVersion = 5`). **v4 → v5**:
`Editor.Prefab` yo'lidagi `\` → `/` (nusxa bog'lanishlari shu versiyada qo'shildi). Versiyasiz prefab yoki bufer
v0 hisoblanadi va butun zanjirdan o'tadi (namuna fayllarida bu hech narsani o'zgartirmaydi; hech narsa
o'zgarmagan migratsiya logga yozilmaydi). Yangiroq versiyali fayl tushunilgancha o'qiladi, lekin ustiga hech
qachon yozilmaydi (Apply, prefab rejimi, hot reload ham).

---

## 14. Buzilgan fayllarni tiklash

- Prefab va bufer o'qilganda har obyekt alohida o'qiladi: buzilgan yozuv o'tkazib yuboriladi, o'qilmagan
  komponent tashlab ketiladi, noma'lum kalitlar va takroriy ID'lar xabar qilinadi, tashqaridagi ota yoki ota
  halqasi obyektni ildizga aylantiradi.
- JSON'i buzilgan sahna ochilganda editor so'raydi: *Qisman tiklash?* Rozi bo'linsa fayl
  `<nom>.zscene.corrupt-<vaqt>.bak` ga nusxalanadi, o'qiladigan qismi yuklanadi va *Prefab nusxalarini tiklash*
  taklif qilinadi (nusxalarning yo'qolgan obyektlari prefabdan qayta yaratiladi).
- Kodda: `SceneSerializer::RecoverFromString`, `EntitySetReadOptions::Recover`.

---

## 15. Eksportda prefab'larni tekshirish

Eksport (Fayl → O'yinni build qilish) fayllarni yozadigan oqim boshlanishidan oldin asosiy oqimda
`PrefabExport::Prepare` ni ishlatadi. Xuddi shu tekshiruv: **Fayl → Prefab'larni tekshirish** (yoki build oynasidagi tugma) — natija
oynada, har qator faylga olib boradi.

| Qoida | Nima | Darajasi |
|---|---|---|
| R1 | o'yin nomlagan prefab yo'q (sahna / prefab skript maydoni, C++ satri: `Instantiate` bo'sh qaytaradi) | xato |
| R2 | prefab o'qilmaydi yoki obyekti yo'q (qisman o'qilsa — ogohlantirish) | xato |
| R3 | prefab yangiroq formatda | xato |
| R4 | ID yo'q / takrorlanadi, bir nechta ildiz, ota halqasi / tashqaridagi ota | ogohlantirish |
| R5 | noma'lum komponent kalitlari | ogohlantirish |
| R6 | prefabdan tashqaridagi obyektga havolalar (joint'lar nomi bilan) | ogohlantirish |
| R7 | prefab ishlatadigan asset topilmadi | ogohlantirish |
| R8 | ichma-ich / variant / sahnadagi nusxaning prefabi yo'q (tayyor ma'lumoti baribir eksport qilinadi) | ogohlantirish |
| R9 | prefab'lar sikli | xato |
| R10 | prefabda yo'q qismning eskirgan o'zgarishi; bog'lanmagan eski nusxa | ogohlantirish |

Loyiha sozlamalari → Build sahnalar → Eksport → *Prefab ogohlantirishlari - xato* yoqilsa, ogohlantirishlar ham
eksportni muvaffaqiyatsiz qiladi (`.zproject` → `"Export": { "PrefabWarningsAsErrors": true }`).

**Eksport nusxasi.** Prefab bog'lanishlari editorniki, shuning uchun eksport qilingan `.zscene` / `.zprefab`
fayllarida ular bo'lmaydi: bog'lanishi bor har fayl o'rniga tozalangan matn yoziladi. Prefabidan eskirgan
nusxalar (`PrefabHash` farq qiladi) shu nusxada prefab'ning hozirgi holatiga keltiriladi (o'zgarishlari bilan);
ichma-ich prefab'li prefab tekis holatda yoziladi. Loyihadagi asl fayllar o'zgarmaydi. Binar kesh (`.zsceneb`)
tozalangan matndan yoziladi. Namuna loyihalar 0 xato, 0 ogohlantirish beradi; Platformer2D'ning eksport
qilingan `Menu.zscene` faylida bog'lanish kalitlari yo'q.

```cpp
AssetReferenceIndex index;
ExportAssetPlan plan = ExportAssets::PlanForActiveProject(nullptr, nullptr, &index);
PrefabExportResult check = PrefabExport::Prepare(plan, db, &index);
// check.Issues (Rule, File, Message, Level), check.Baked (fayl -> eksport matni), check.Summary()
```

---

## 16. Skriptlardan: Instantiate va kesh

O'yinda prefab skriptdan yaratiladi:

```cpp
class Spawner : public ScriptableEntity
{
public:
    std::string Prefab = "Prefabs/Ball.zprefab";
    void OnUpdate(Timestep) override
    {
        if (Input::IsKeyPressed(Key::Space))
            if (Entity ball = Instantiate(Prefab, GetWorldPosition() + glm::vec3(0, 2, 0)))
                ball.GetComponent<TransformComponent>().Scale = glm::vec3(0.5f);
    }
};
ZM_REGISTER_SCRIPT(Spawner, ZM_FIELD(Spawner, Prefab))
```

`Instantiate(path)` va `Scene::InstantiatePrefab` oddiy nusxa yaratadi (bog'lanishsiz), ichidagi havolalar yangi
obyektlarga o'tadi. Fayl bir marta o'qiladi va tahlil qilinadi (`PrefabCache`): keyingi har chaqiruv tayyor
JSON'dan yaratadi. Editorda kesh fayl vaqti va hajmini tekshiradi (o'zgargan fayl qayta o'qiladi), player faylni
faqat birinchi marta tekshiradi. O'qilmagan fayl xatosi bir marta aytiladi va keshga tushmaydi.

---

## 17. Ishlash tezligi

`ZaminBench --serialize --prefabs [--instances N] [--json fayl]` (Release, mediana; batafsil:
[scenes.md](scenes.md#18-testlar-va-benchmark)):

| O'lchov | Natija | Byudjet |
|---|---|---|
| 1000 nusxa yangiligini tekshirish (hash tez yo'li) | 4.8 ms | ≤ 5 ms |
| Bitta obyekt o'zgarishlarini yozish | 0.014 ms | ≤ 0.2 ms |
| Bog'lanish baytlari (fayldagi ulushi) | 8.7 % | ≤ 10 % |
| Majburiy qayta sinxron, 20 000 obyekt | 397 ms | ≤ 300 ms (oshadi) |
| Yuklashga qo'shimcha (JSON) | 10.8 % / 7.3 % (GameMenu) | ≤ 5 % (oshadi) |
| `Instantiate(path)` ×1000: keshsiz / `PrefabCache` | 283 / 111 ms | — |

Majburiy sinxron va yuklashdagi qo'shimcha faqat prefab o'zgarganda / sahna ochilganda ishlaydi; o'yin
(player) bog'lanishlarni o'qimaydi va eksport ularni olib tashlaydi, shuning uchun o'yinga ta'siri yo'q.

---

## 18. Testlar

`./build/bin/ZaminTests` (filtr: `ZAMIN_TESTS=Prefab`):

| Test | Nimani tekshiradi |
|---|---|
| `PrefabLinkSerialization` | bog'lanish kalitlari sahna va buferda; eski fayl o'qiladi; player bog'lanishni o'qimaydi |
| `PrefabInstantiateLinked` | bog'langan nusxa, ID'lar, hash; bog'lanmaydigan prefab |
| `PrefabLegacyLinkMenuScene` | Platformer2D `Menu.zscene`: 44/44, 0 override, fayllar baytma-bayt o'zgarmaydi |
| `PrefabOverrideDetection`, `PrefabApplyPropagate`, `PrefabRevertLevels`, `PrefabSyncStructural` | override'lar, Apply, Revert, tuzilma sinxroni |
| `PrefabNested`, `PrefabVariants` | ichma-ich prefab'lar, sikl, variant zanjiri |
| `PrefabUndoFileEdits` | Apply undo / redo fayllari bilan |
| `PrefabEntityRefRemap`, `EntityRefVisitorCoverage` | havolalar jadvali (§8) |
| `EntitySetVersionMigration`, `EntitySetPartialLoad`, `SceneSalvage`, `SerializationFuzz` | versiya, qisman o'qish, tiklash, fuzz |
| `PrefabStageRoundTrip` | prefab rejimi: o'zgarishsiz saqlash baytma-bayt bir xil |
| `PrefabCacheReads`, `LargeSceneLinkedInstances`, `PrefabPlacementAndThumbs` | kesh, katta sahna, joylashtirish |
| `PrefabHotReloadSync` | hot reload: vaqt/hajm/hash o'zgarishi, tartib, o'z yozuvlari, takrorlash xavfsiz, o'chirilgan fayl |
| `ClipboardPrefabFormat` | bufer: Kind/Version, bog'langan joylash, qism, World, Origin, eski matn |
| `PrefabExportValidation` | R1-R10 har biri uchun fixture, eksport nusxasi = asl sahna minus bog'lanishlar, namuna loyihalar 0 xato |
| `PrefabFieldLabelsExist` | inspektor qator belgilari jadvalidagi har yo'l komponent JSON'ida bor |

Eksport: `Tests/export_test.sh` (Platformer2D eksport qilinadi va ishga tushiriladi).

---

## 19. Cheklovlar

- Massivlar bitta qiymat: nusxa massivning bitta elementini o'zgartirsa, butun massiv override bo'ladi.
- Ochilmagan sahnalar avtomatik qayta yozilmaydi: ular ochilganda (yoki eksport nusxasida) yangilanadi.
- Prefabda bitta ildiz bo'lishi kerak (eski ko'p ildizli prefab nusxasi bog'lanmagan holda qo'yiladi).
- Ichma-ich prefab yoki asosga *butun nusxa* yoki *butun obyekt* Apply qilinmaydi (faqat bitta qiymat yoki
  komponent); butun nusxani variant / ichki prefabning o'z prefab rejimida qo'llang.
- Undo/redo orasida prefab fayli tashqarida o'zgargan bo'lsa, ogohlantirish beriladi va fayl ustiga yoziladi.
- UI prefab o'z Canvas'isiz bo'lsa, sahnaning birinchi Canvas'iga qo'yiladi (sichqoncha ostidagisiga emas).
- Kontent brauzerdan bir vaqtda bitta prefab buferga nusxalanadi (`Origin` bitta yo'l).

---

## 20. Qisqa API ma'lumotnomasi

| API | Vazifasi |
|---|---|
| `Scene::InstantiatePrefab(path)`, `ScriptableEntity::Instantiate(path[, pos])` | o'yindagi oddiy nusxa (`PrefabCache`) |
| `Scene::SavePrefab(root, path)` | daraxtni prefab fayliga (atomik), tashqi havolalar ogohlantiriladi |
| `PrefabDatabase` | editor shablonlari: `Get`, `ResolvedHash`, `GetDependents`, `DependsOn`, `Write`, `CheckFile`, `Invalidate` |
| `PrefabUtility::InstantiateLinked / LinkSource / LinkLegacyInstances / NormalizeLinks` | bog'lanish |
| `PrefabUtility::RecordOverrides / GetOverrides / FindOrphanOverrides` | o'zgarishlar |
| `PrefabUtility::SyncInstance / SyncScene / SyncPrefabFile / ReloadFiles` | prefabga ergashish, hot reload |
| `PrefabUtility::Apply / Revert / RestoreRemoved / Unpack / GetApplyTargets` | §4 |
| `PrefabUtility::CreateVariant / SaveAsVariant / GetVariantBase / GetVariantChain` | §6 |
| `PrefabUtility::LoadStage / SerializeStage / SaveStage` | §7 |
| `EntityClipboard::Copy / CopyPrefab / Paste / GetOrigin` | §11 |
| `SceneSerializer::SerializeEntities / DeserializeEntities` (`EntitySetWriteOptions` / `EntitySetReadOptions`) | prefab / bufer hujjati |
| `PrefabExport::Prepare / StripLinks`, `ExportSettings::PrefabWarningsAsErrors` | §15 |
| `PrefabCache::Instantiate / Invalidate / SetCheckForChanges` | §16 |
