# Benchmarklar va ularning boshlang'ich natijalari

Dvigatelning tezligini o'lchaydigan dasturlar va o'lchangan **boshlang'ich natija** (baseline): keyingi o'zgarish
tezlikni qanchalik o'zgartirganini shu raqamlar bilan solishtiring. Raqamlar bir xil kompyuter, bir xil build turi va
bir xil sozlamalarda olinganda solishtiriladi.

## Rendering: 10 000 sprite, 1 000 mesh (`ZaminBench --render`)

`Tests/RenderBench.cpp`, ishga tushirish: `Tests/render_bench.sh` (yoki `cmake --build build --target render_bench`).
GL konteksti kerak: displey bo'lmasa qat'iy 1600x900x24 xvfb ekrani, va har doim Mesa'ning dasturiy rasterizatori
(llvmpipe) - raqamlar kompyuterlar orasida va shu jadval bilan solishtirilsin (GPU bilan: `ZAMIN_RENDER_BENCH_GPU=1`).
Sahnalar o'yin (player) kabi chiziladi: asosiy kamera, sahna kadri (`OnUpdateRuntime`) chiziqli HDR nishonga, keyin
post-processing oynaga. Har holat 20 kadr qizdiriladi, keyin 120 kadr o'lchanadi:

| Holat | Nima chiziladi |
|---|---|
| `sprites` | 10 000 sprite (choragi 4 xil teksturadan biri bilan), ortografik kamera |
| `meshes` | 1 000 primitiv (kub, shar, silindr, konus; 8 material) yer ustida, soyali quyosh va nuqtali chiroq, perspektiv kamera; GPU instancing bilan (odatdagidek) |
| `meshes-noinst` | o'sha meshlar instancing'siz (instancing nima tejashini ko'rsatadi) |
| `combined` | spritelar (orqadagi devorda) va meshlar bitta sahnada |

**CPU** - kadr boshidan hamma buyruq yuborilguncha; **Kadr (GPU bilan)** - `glFinish` gacha (GPU ham tugatgan).
`--json` har holat raqamlarini yozadi (`render-bench.json`), `--max-avg <ms>` chegaradan sekin holatda skriptni
yiqitadi, `--shots <papka>` har holatning oxirgi kadrini PNG qilib saqlaydi.

### Boshlang'ich natija (2026-11-08)

Muhit: 4 vCPU Intel Xeon @ 2.10 GHz, Mesa 25.2.8 llvmpipe (LLVM 20.1.2, 256 bit), xvfb 1600x900x24, oyna 1280x720,
GCC 13.3, **Release** build (`-DCMAKE_BUILD_TYPE=Release`), 120 kadr.

| Holat | Obyekt | CPU ms (o'rtacha / mediana / p95) | Kadr ms, GPU bilan (o'rtacha / mediana / p95 / max) | Chizildi |
|---|---:|---|---|---|
| sprites | 10 001 | 18.72 / 18.21 / 21.51 | 24.51 / 23.92 / 27.68 / 31.40 | 3 draw call, 10 000 kvad |
| meshes | 1 004 | 130.30 / 128.23 / 152.16 | 136.54 / 134.36 / 158.17 / 170.86 | 46 draw call, 1 001 mesh (1 000 instans), 649K uchburchak |
| meshes-noinst | 1 004 | 135.24 / 130.99 / 156.46 | 141.68 / 137.29 / 163.78 / 187.51 | 2 027 draw call, 1 001 mesh, 649K uchburchak |
| combined | 11 004 | 138.78 / 135.09 / 169.44 | 145.10 / 141.27 / 176.07 / 183.91 | 47 draw call, 8 500 kvad, 1 001 mesh (1 000 instans) |

Debug build'da (o'sha kompyuter, 20 kadr) taxminan: sprites 75 ms, meshes 150 ms, combined 209 ms - spritelarda
Debug ~3 marta sekin (CPU tomonidagi batching), meshlarda farq kichik (vaqtni rasterizatsiya oladi).

Qanday o'qish kerak:

- **10 000 sprite 3 ta draw call'da** chiziladi (Renderer2D batching: bir batch'da ko'p kvad, teksturalar slotlarda) -
  Release'da kadr ~25 ms dasturiy rasterizator bilan; haqiqiy GPU'da bu bir necha ms.
- **Instancing** draw call'larni 2 027 dan 46 ga tushiradi (soya kaskadlari bilan birga). llvmpipe'da vaqt deyarli bir
  xil (u yerda uchburchaklarni CPU rasterlaydi - 649K uchburchak, soya kaskadlarining chizishlari ham kiradi); haqiqiy
  GPU'da draw call soni CPU vaqtini ko'proq belgilaydi.
- `combined` holatda 1 500 sprite kamera ko'rinishidan tashqarida (frustum culling): 8 500 kvad chizildi.
- Natija mashinaga bog'liq: boshqa kompyuterda avval shu jadvalni o'sha yerda qayta oling, keyin o'zgarishni
  solishtiring. Kichik farqlar (±5-10%) shovqin; p95 va max ayniqsa xvfb ostida tebranadi.

## ECS: yaratish, aylanib chiqish, o'chirish (`ZaminBench --ecs`)

`Tests/EcsBench.cpp`, ishga tushirish: `ZaminBench --ecs [--entities N] [--frames N] [--json fayl]` yoki
`Tests/bench.sh --only ecs`. Headless (oyna, GPU kerak emas). N obyekt (standart 10 000) sahnaning o'z API'si orqali
(editor, yuklovchi va skriptlar ishlatadigan yo'l) yaratiladi, komponent oladi, aylanib chiqiladi, ierarxiyaga
qo'yiladi, qidiriladi, nusxalanadi va o'chiriladi; taqqoslash uchun yonida yalang'och EnTT registry (nom, UUID
xaritasi, sahna tartibi, ierarxiya yo'q). Oxirida shu o'lchamdagi sahnaning o'yin kadrlari (`UpdateRuntime`,
kamerasiz, ya'ni faqat yangilash): bo'sh, N/10 skript bilan, va har kadr 500 obyekt yaratilib 500 tasi o'chiriladigan holat. Har
raqam 3 marta o'lchanadi (har safar yangi sahna) va mediana olinadi; **ns / ta** - bitta obyekt (yoki komponent)
uchun.

### Boshlang'ich natija (2026-11-08)

Muhit: 4 vCPU Intel Xeon @ 2.10 GHz, GCC 13.3, **Release** build, 10 000 obyekt (`Tests/Baselines/ecs-release.json`).

| Amal | ms | ns / ta |
|---|---:|---:|
| EnTT: `create` + `emplace<Transform>` (yarmiga Sprite) | 0.74 | 74 |
| EnTT: `view<Transform>` har biriga yozish | 0.008 | 0.8 |
| EnTT: `destroy` har biri alohida | 0.47 | 47 |
| `CreateEntity(nom)` (ID, Transform, Tag, Relationship, UUID xaritasi) | 2.29 | 229 |
| `CreateEntityWithUUID` (yuklovchi yo'li) | 1.50 | 150 |
| `AddComponent<SpriteRenderer>` | 0.95 | 95 |
| `RemoveComponent<SpriteRenderer>` | 0.44 | 44 |
| `GetAllEntitiesWith<Transform>` / `<Transform, Sprite>` | 0.062 / 0.060 | 6.2 / 6.0 |
| `Entity::GetComponent<Transform>` sahna tartibida / tasodifiy | 0.066 / 0.132 | 6.6 / 13 |
| `GetEntityByUUID` tasodifiy | 0.23 | 23 |
| `SetParent` (dunyo transformi saqlanadi) | 3.83 | 425 |
| `GetWorldTransform` (chuqurlik 2) | 2.17 | 217 |
| `DuplicateEntity` (100 guruh x 10 obyekt) | 1.62 | 1 622 |
| `Scene::Copy` (editor Play nusxasi) | 6.93 | 693 |
| `DestroyEntity` eng yangisidan | 4.40 | 440 |
| `DestroyEntity` tasodifiy tartibda | 323 | 32 338 |
| `DestroyEntity` eng eskisidan (1 000 ta, sahnada 10 000) | 99 | 99 351 |
| `DestroyEntities(hammasi)` - bitta to'plam | 6.93 | 693 |
| `DestroyEntityDeferred` hammasi + kadr oxiridagi bo'shatish | 6.63 | 663 |
| Kadr: 10 000 sprite, ierarxiya (fizikasiz, skriptsiz) | 0.165 / kadr | 17 |
| Kadr: 1 000 skript `OnUpdate` | 0.308 / kadr | 308 |
| Kadr: 500 `Instantiate` + 500 Destroy (10 000 obyektli sahnada) | 1.60 / kadr | 1 597 |

Debug build'da (o'sha kompyuter): `CreateEntity` 1 918 ns, `Scene::Copy` 3 343 ns, bo'sh kadr 0.72 ms, 1 000 skript
2.75 ms, 500 + 500 kadr 25.7 ms (shundan `Instantiate` 39 µs / obyekt - Release'da 2 µs: har bir nusxa ~90 komponent
turini tekshiradi, optimallashtirilmagan kodda har tekshiruv EnTT pool qidiruvi).

Qanday o'qish kerak:

- **Sahna API'sining narxi** yalang'och EnTT'dan 2-3 marta ko'p yaratishda (nom, UUID xaritasi, 4 ta majburiy
  komponent) va aylanishda bir xil (`GetAllEntitiesWith` - o'sha EnTT view).
- **Sahna tartibi saqlanadi** (fayl, ierarxiya va fizika determinizmi shu tartibga tayanadi): bitta eski obyektni
  `DestroyEntity` bilan o'chirish undan keyingi hamma obyektlarni bir o'ringa suradi - O(N). 10 000 obyektli sahnada
  eng eski obyekt ~0.1 ms. Ko'p obyekt o'chirganda **`DestroyEntities`** (editor) yoki skriptlarning **`Destroy`**
  (`DestroyEntityDeferred`, kadr oxirida bitta to'plam) ishlating - ikkalasi ham bitta o'tish (0.7 µs / obyekt).
- **Topilgan va tuzatilgan xato:** `DestroyEntityDeferred` takrorlanishni butun navbatdan qidirib tekshirardi - bir
  kadrda minglab `Destroy` kvadratik edi (10 000 obyekt: Debug'da 525 ms, endi 87 ms; Release 6.6 ms). Endi
  to'plam (`m_DestroyQueued`); `DeferredDestroyOnceEachAndLinear` testi buni (va har obyekt bir marta o'chishini)
  tekshiradi - eski kod bilan test yiqiladi.

## Serializatsiya: katta sahnalar, nusxalash, komponent narxi (`ZaminBench --serialize`)

`Tests/SerializeBench.cpp`: `ZaminBench --serialize [--entities N] [--content 2d|3d|all] [--json fayl]` yoki
`Tests/bench.sh --only serialize`. Platformer2D loyihasida (yo'llar uning assetlariga) headless:

- **2D sahna** (N obyekt: spritelar, 2D jismlar, maydonli skriptlar, matnlar, teglar, ierarxiya) va **3D sahna**
  (materialli meshlar, 3D jismlar, yorug'liklar, particle'lar, ovozlar, skriptlar, ierarxiya): JSON matn
  (`SerializeToString`, faylga yozish, `Deserialize` bosqichlari bilan - `SceneLoadTiming`) va binar kesh
  (`.zsceneb`, MessagePack), MB va MB/s. O'qilgan sahna qayta saqlanganda bir xil matn berishi tekshiriladi (farq
  bo'lsa dastur 1 kodi bilan tugaydi);
- **nusxalash / joylash**: 1 000 obyektli tanlov (`SerializeEntities`, yangi UUID'lar bilan `DeserializeEntities`);
- **komponent narxi**: har komponent uchun 1 000 obyekt (faqat shu komponent bilan) saqlanadi va yuklanadi, bo'sh
  obyekt narxi ayiriladi - bitta komponent qancha ns va bayt qo'shadi.

Bog'langan prefab nusxalari alohida: `ZaminBench --serialize --prefabs` (`Tests/PrefabBench.cpp`,
[Prefabs.md](Prefabs.md#17-performance)).

### Boshlang'ich natija (2026-11-08)

Muhit: yuqoridagidek, **Release**, 10 000 obyekt (`Tests/Baselines/serialize-release.json`). Eslatma:
`SceneSerializer.cpp` Debug build'da ham `-O2` bilan kompilyatsiya qilinadi, shuning uchun Debug raqamlari yaqin.

| Amal | 2D sahna | 3D sahna |
|---|---:|---:|
| JSON hajmi / binar kesh hajmi | 8.8 MB / 3.2 MB | 12.7 MB / 4.9 MB |
| Saqlash: JSON matn | 119 ms (74 MB/s) | 204 ms (62 MB/s) |
| Saqlash: binar kesh | 96 ms | 172 ms |
| Yuklash: JSON (tahlil + obyektlar) | 176 ms (107 + 67) | 257 ms (169 + 82) |
| Yuklash: binar kesh (tahlil + obyektlar) | 99 ms (38 + 59) | 134 ms (62 + 70) |
| Nusxalash / joylash, 1 000 obyekt (0.85 MB) | 8.3 ms / 13.3 ms | |

Bitta komponent narxi (saqlash / yuklash ns, JSON bayt; bo'sh obyekt - butun obyekt):

| Komponent | Saqlash ns | Yuklash ns | Bayt |
|---|---:|---:|---:|
| bo'sh obyekt (ID, Tag, Transform, Relationship) | 2 931 | 6 100 | 364 |
| SpriteRenderer | 3 463 | 4 231 | 331 |
| CircleRenderer | 1 980 | 2 660 | 214 |
| Text | 3 016 | 3 649 | 271 |
| MeshRenderer (primitiv, ichki material) | 8 981 | 9 976 | 776 |
| Camera | 4 470 | 5 903 | 489 |
| PointLight | 1 451 | 1 899 | 154 |
| Rigidbody2D / BoxCollider2D | 2 365 / 2 967 | 2 667 / 3 876 | 226 / 292 |
| PolygonCollider2D (5 nuqta) | 4 486 | 6 466 | 594 |
| Rigidbody3D / BoxCollider3D | 2 493 / 2 411 | 2 964 / 3 311 | 237 / 268 |
| NativeScript (3 maydon) | 3 652 | 5 393 | 356 |
| AudioSource | 3 402 | 5 132 | 270 |
| ParticleSystem (standart modullar) | 13 188 | 18 644 | 1 168 |
| Animator | 3 740 | 4 142 | 293 |
| UITransform + UIButton | 6 906 | 10 664 | 839 |

Qanday o'qish kerak: yuklash vaqtining ~60 % i JSON tahlili (binar kesh uni ~3 marta qisqartiradi, shuning uchun
o'yin `.zsceneb` dan yuklaydi); qolgani obyektlarni yaratish. Eng qimmat komponentlar - ParticleSystem (barcha
modullar sozlamalari) va MeshRenderer (material); obyektning o'zi (4 majburiy komponent) yuklashda eng katta ulush.

## M25 optimallashtirish: oldin va keyin

Muhit: yuqoridagi kompyuter, **Release** (`build-m25rel`: `cmake -S . -B build-m25rel -G Ninja -DCMAKE_BUILD_TYPE=Release`;
asosiy `build/` dan boshqa build papkalari o'yin modullarini endi o'zining `GameModules/` iga yozadi va o'z dasturlari
ularni o'sha yerdan yuklaydi - `Projects/*/Binaries` dagi asosiy build modullari ustiga yozilmaydi), llvmpipe, xvfb. "Oldin" - M25 boshidagi commit (`37d2916`)
build'i, "keyin" - 481-487 vazifalaridan keyingi build; ikkalasi bir xil benchmark kodi bilan (oldingi build'ning
`ZaminBench` va `libZamin.so` nusxasi saqlab qo'yilgan edi). llvmpipe ostidagi og'ir 3D holatlar ±10 % tebranadi, shuning
uchun vaqtlar juft-juft (oldin, keyin, oldin, keyin) o'lchandi. Yangi o'lchovlar:

- `ZaminBench --render`: har holat uchun **ajratma / kadr** (kadr yuborilguncha `operator new` chaqiruvlari,
  `Tests/AllocCounter.h`; `ZAMIN_ALLOC_TRACE=<n>` ularning chaqiruv steklarini chiqaradi), 2D batch'lar nega erta
  chizilgani (`Flush2DTextures` / `Flush2DKind` / `Flush2DState`) va `BatchOrder` ko'chirgan chizishlar; yangi
  `mixed2d` holati (1 000 katak: 24 teksturali sprite, doira, matn navbatma-navbat, chuqurlik bo'yicha aralashgan) va
  uning `BatchOrder` siz nusxasi `mixed2d-sorted`; `--case loading` - fon yuklash.
- `ZaminBench --ecs`: UpdateRuntime kadrlarining ajratmalari, `GetWorldTransform` qayta so'ralganda va ildizlar
  siljiganda, komponent pool'idan to'g'ridan-to'g'ri o'qish.
- `ZaminBench --fonts` (`Tests/FontBench.cpp`): shrift atlasini rasterlash va diskdagi keshdan o'qish, glif qidirish,
  kerning bilan va kerningsiz matn joylash.

### 2D batching (481)

`mixed2d`, bitta build, `BatchOrder` o'chiq / yoqiq:

| | Draw call | Batch bo'linishi (tur / tekstura / holat) | CPU ms (o'rtacha) | Kadr ms, GPU bilan |
|---|---:|---|---:|---:|
| `BatchOrder` siz (saralangan tartib) | 670 | 667 / 0 / 0 | 15.98 | 22.95 |
| `BatchOrder` bilan | **65** | 62 / 0 / 0 (431 chizish ko'chirildi) | **10.14** | **17.03** |

Rasm o'zgarmaydi: faqat ekranda ustma-ust tushmaydigan chizishlar suriladi (`ZaminRenderTests` 12/12, farq 0);
24 teksturali sprite qatori bo'lganda (eski `mixed2d` varianti) 22 ta "17-tekstura" bo'linishi teksturaga guruhlash bilan
yo'qoladi. `sprites` (10 000 sprite, 3 draw call) o'zgarmadi - u yerda batch allaqachon bitta edi.

### Kadrlararo ajratmalar (482)

| Holat | Oldin (ajratma / kadr) | Keyin | Nima o'zgardi |
|---|---:|---:|---|
| `sprites` (10 000) | 10 023 | **2** | dunyo transformlari kadr uchun `unordered_map` da emas, sahna keshida; 2D ro'yxati saqlanadi |
| `meshes` (1 000, instancing) | 1 504 | **6** | instancing guruhlari tekis ro'yxatda (`FlatBatches`), `EndScene` ro'yxatlari saqlanadi |
| `meshes-noinst` | 7 105 | **6** | + shader uniform nomlari `std::string_view` (16+ belgili nom har chaqiruvda `std::string` edi) |
| `combined` | 11 522 | **7** | yuqoridagilarning hammasi |
| `mixed2d` (eski variant) | 2 998 | **2** | + matn joylash keshi (`TextComponent::GetWorldLayout`), Renderer2D uniform nomlari |
| ECS: bo'sh kadr / 1 000 skript / 500+500 obyekt | 0 / 11 / 1 518 | 0 / 11 / 1 518 | (UpdateRuntime: obyekt yaratish va o'chirish o'z tabiati bilan ajratadi) |

Vaqt (juft o'lchov): `meshes-noinst` CPU 149.0 / 149.6 -> 141.4 / 142.7 ms (~5 %); `sprites` 25.0 / 25.2 -> 24.6 / 25.3 ms
(shovqin ichida - u yerda vaqtni rasterizatsiya oladi). Qaytmasligi uchun `SteadyFramesMakeNoHeapAllocations` testi:
qizdirilgan sahna kadri (300 obyekt, ierarxiya) `UpdateRuntime` + `GetWorldTransform` da birorta ham ajratma qilmaydi.

### Dunyo transformi keshi va pool'lar (483, 484)

| Amal (10 000 obyekt, chuqurlik 2) | Oldin ns / ta | Keyin ns / ta |
|---|---:|---:|
| `GetWorldTransform` birinchi marta | 221 | 125 |
| `GetWorldTransform` yana, hech narsa qimirlamagan | 221 (har safar hisoblanardi) | **33** |
| guruh ildizlari siljigandan keyin | 221 | 114 |
| `Entity::GetComponent<Transform>` (har safar pool qidiruvi) / pool bir marta olinib | 6.6 | **1.7** |

Kesh obyektning Translation / Rotation / Scale qiymatini, ota-onasini va ota-onasining versiyasini solishtiradi - skript
`Transform()` ga to'g'ridan-to'g'ri yozsa ham, ota-ona almashsa ham, obyekt o'chib uning o'rni qayta ishlatilsa ham to'g'ri
(`WorldTransformCacheFollowsChanges`). Suyakka biriktirilganlar (animator pozasi) va ishchi oqimlar keshsiz hisoblaydi.
Render tsikllari (`RenderScene` 2D qismi) komponent pool'larini bir marta oladi (`Scene::HotStorages`).

### Shriftlar (485)

| | Oldin | Keyin |
|---|---:|---:|
| Asosiy atlas (2048 px, 600+ glif, 2x2) | 45.0 ms rasterlash | **2.4 ms** keshdan o'qish (1.87 MB fayl) |
| Glif qidirish | 4.46 ns (hash xarita) | 3.46 ns (to'g'ridan-to'g'ri jadval; Debug: 65.6 -> 13.0) |
| Matn joylash, 24 px, so'z o'rash (kerning'siz) | 16.6 ns / belgi | 11.5 ns / belgi |
| Matn joylash, 24 px, so'z o'rash, kerning bilan | - (kerning yo'q edi) | 15.7 ns / belgi (juftlik keshi) |
| "AVATAR To'p Yer" kengligi, 24 px | 171.5 px | 161.2 px kerning yoqilganda (AV -0.062 em) |

Ikki matn joylash qatori bir xil yangi Release build'da `ZaminBench --fonts` bilan qayta o'lchangan: kerning'ning
o'zi belgiga ~37 % qo'shadi (11.5 -> 15.7 ns), kerning'siz yangi yo'l eskisidan ~30 % tez (avvalgi jadvaldagi
"15.9 ns kerning bilan" eski build'ning kerning'siz 16.6 ns i bilan solishtirilgan edi - bu kerning narxini
yashirardi). Kerning odatda o'chiq - render rasmlari o'zgarmadi; shrift `.zmeta` sida `"Import": { "Kerning": true }` yoqadi.

### Fon yuklash (486, 487)

`ZaminBench --render --case loading`: Showcase3D'ning 27 rasmi va 4 modeli (bo'sh keshdan):

| | Asosiy oqim band | Eng uzun kadr | Hammasi tayyor |
|---|---:|---:|---:|
| Darhol (`Load`) | 103.8 ms | 103.8 ms (bitta kadr) | 103.8 ms |
| Fonda (`LoadAsync` + `AsyncLoader::Update(4 ms)`) | 39.1 ms (9 kadrga bo'lingan) | **6.9 ms** | **50.3 ms** |

Dekodlash va glTF tahlili 3 ishchi oqimda parallel ketadi; asosiy oqimda faqat GPU'ga yuklash qoladi.

### Shader keshi (488)

488-494 uchun "oldin" - shu vazifalardan oldingi commit (`c253510`) ning Release build'i, "keyin" - ulardan keyingisi;
ikkalasi ham `build-m25rel` sozlamalari bilan, bir kompyuterda ketma-ket.

Showcase3D player'i ishga tushganda dvigatelning 25 ta shader dasturi (`Renderer::Init` logidagi `Shader'lar: ...`
qatori; ishga tushishning qolgan qismi - sahna, teksturalar - o'zgarmagan). "Oldin" - keshsiz (`ZAMIN_SHADER_CACHE=0`,
eski yo'l bilan bir xil: har safar kompilyatsiya + link):

| Mesa'ning o'z disk keshi | Oldin (kompilyatsiya) | Keyin, birinchi ishga tushish (kompilyatsiya + saqlash) | Keyin, keyingilari (binary'dan) |
|---|---:|---:|---:|
| o'chiq (`MESA_SHADER_CACHE_DISABLE=true`) | 253 ms | 263 ms | **74-91 ms** |
| yoqiq (odatiy) | 72 ms | 69 ms | 64-66 ms |

llvmpipe binary formatini beradi (`GL_NUM_PROGRAM_BINARY_FORMATS` = 1), lekin dasturni binary'dan olganda ham LLVM
kodini qayta yasaydi - shuning uchun 3 baravar, 10 baravar emas; Mesa o'z keshi yoqiq bo'lsa u allaqachon ko'p ishni
tejaydi. Haqiqiy GPU drayverlarida (o'z keshi bo'lmasa yoki tozalanganda) foyda shu "o'chiq" qatoriga yaqin. 25 ta fayl
548 KB. Formati yo'q drayver uchun mantiq `ShaderCacheStoresProgramBinaries` testida tekshiriladi.

### Kadr uniform buferi (489)

Mesh shader'larining kadr qiymatlari: oldin har bir mesh dasturiga (oddiy, skinned, instanced) 49-66 ta `glUniform*`
chaqiruvi (IBL bo'lsa 9 ta `u_SH[i]` nomi har kadr `std::string` bilan yasalardi) - kadrga 147-198 chaqiruv; keyin bitta
`glBufferSubData` (ishlatilgan qismi: `meshes` holatida 1 904 bayt) + har dasturga bitta `u_DebugView`. Sampler
uniformlari (`u_AlbedoMap` ...) `Init` da bir marta. Vaqt farqi llvmpipe shovqini ichida (bu chaqiruvlar kadrga bir
marta, chizishga emas); rasm bir xil (`ZaminRenderTests` 12/12, farqlar avvalgidek).

### Soya o'tishida kesish (490)

`ZaminBench --render --case meshes` (1 000 mesh, 4 kaskad, quyosh soyasi):

| | Soya chizishlari | Kesilgan (shundan "ko'rinmas soya") | Draw call (instancing'siz holat) | CPU ms (`meshes`, o'rtacha) |
|---|---:|---:|---:|---:|
| Oldin (`c253510` build'i: faqat kaskad hajmi bo'yicha) | 1 024 | 1 979 (-) | 2 027 | 156.5 |
| Keyin, `meshes-allshadow` (o'sha build, ko'rinish bo'yicha kesish o'chiq) | 1 024 | 1 979 (0) | - | 146.5 |
| Keyin | **323** | 2 680 (**701**) | **1 326** | **126.0** |

Kaskadlar bo'yicha (keyin): 1 / 1 / 321 / 0 tashlovchi (oldin 1 / 22 / 1001 / 0). Rasm o'zgarmaydi: test konservativ
(normal siljishi va filtr uchun zaxira bilan) - render testlari 12/12, farq oldingidek.

### Parallel particle va animatsiya (491, 492)

Ilgari (369) mustaqil **tizimlar** ishchi oqimlarga taqsimlanardi - bitta katta tizim baribir bitta oqimda edi. Endi
tizim ichidagi particle'lar ham 2 048 talik bo'laklarda (4 096 dan boshlab; dunyo to'qnashuvi, to'qnashuv hodisalari
va izlarsiz). Bitta tizim, ~49 000 particle, shovqin 2 oktava; `ZaminBench` standart holda bitta oqimda ishlaydi
(`--jobs 0`), ishchi oqimlar soni `--jobs` bilan beriladi:

```bash
build-m25rel/bin/ZaminBench --case "Bitta katta tizim"            # 1 oqim ("bitta oqim")
build-m25rel/bin/ZaminBench --case "Bitta katta tizim" --jobs 3   # 3 ishchi + asosiy oqim
```

| | 1 oqim (`--jobs 0`, oldingi xatti-harakat) | 3 ishchi + asosiy oqim (`--jobs 3`) |
|---|---:|---:|
| Kadr, o'rtacha (bo'sh kompyuter) | 12.2 ms | **4.0 ms** (3.1x) |
| Qayta o'lchash (kompyuter yuklangan, reviewer) | 11.8-12.3 ms | eng tezi 3.50 ms, o'rtacha 7.64 ms |

Parallel holat boshqa jarayonlar bilan yadrolarni bo'lishadi: kompyuter band bo'lsa o'rtacha vaqt eng tezidan
ancha uzoqlashadi (bitta oqim deyarli o'zgarmaydi).

(Oldingi build'da bu holat yo'q edi; bitta tizim u yerda `parallelSystems.size() > 1` sharti bilan doim bitta oqimda
ishlardi - "1 oqim" ustuni shu yo'l.) Ko'p tizimli holatlar o'zgarmadi (`Shovqin`: 3.46 -> 3.43 ms, `Oddiy` 0.17 ms).

Animatsiya: pozalar (369) avvalgidek ishchi oqimlarda (`--animators 300`: 3.2 ms -> 1.0-1.2 ms, 3 ishchi bilan, oldin
ham, keyin ham). Yangi - animatorlarning ko'rinishini belgilash (`MarkVisibleAnimators`: har skinned mesh qutisini
har suyak bilan siljitish) ham parallel; 300 ta 40 suyakli skinned mesh, CullPose: **0.196 -> 0.114 ms**. Ish kichik
bo'lsa (suyak qutilari < 2 048) bitta oqimda qoladi - aks holda parallel ishning o'zi qimmatroq (meshsiz rig'da
0.031 ms bitta oqimda, 0.066 ms parallel bo'lardi). Natija bit-bitgacha bir xil: `LargeParticleSystemSteppedInPiecesMatchesSerial`,
`AnimatorVisibilityMarkedInParallelMatchesSerial`.

### Editor: katta ierarxiya va papka (493, 494)

10 000 obyektli sahna (1 000 guruh x 9 bola, hammasi ochiq) va 5 000 faylli papka, Release editor, 1600x900, xvfb:

| | Ierarxiya ro'yxati, ms / kadr | Kontent brauzer katakchalari, ms / kadr | Editor kadri (status satri) |
|---|---:|---:|---:|
| Oldin (`c253510` editori) | - | - | 42.7 ms (23 FPS) |
| Clipper'siz (`ZAMIN_EDITOR_NO_CLIPPER=1`, hammasi chiziladi) | 10.8 (10 000 qator) | 7.8 (5 000 katak) | 44.0 ms |
| Keyin (`ImGuiListClipper`) | **0.85** (27 qator chiziladi) | **0.06** (18 katak) | **24.7 ms (41 FPS)** |

Ierarxiyaning qolgan 0.85 ms i - 10 000 qatorli tekis ro'yxatni yig'ish (ochiq / yopiq holat, Shift bilan oraliq
tanlash uchun hamma qator); qolgan kadr vaqti llvmpipe'ning viewport chizishi. O'lchov: `ZAMIN_EDITOR_PANEL_TIMES=1`.

### Ishga tushish vaqti (495)

Yangi o'lchov: `StartupTimer` har dasturda dvigatel kutubxonasi yuklangandan birinchi kadr ekranga chiqquncha vaqtni
logga yozadi (`Ishga tushish: birinchi kadr 670 ms (dvigatel 182, layer'lar 88, birinchi kadr 422 ms; jarayon boshidan
~680 ms), RAM ...`), editorning Statistics panelda ham ko'rinadi; `ZAMIN_STARTUP_PROFILE=1` ishga tushish va birinchi
kadrning profiler daraxtini chiqaradi. `Tests/startup_bench.py` editor va player'ni bir kadr bilan 5 marta ochadi
(oldin bir marta isitadi: shader / shrift keshlari) va medianani beradi; "jarayon" - tashqaridan o'lchangan exec ->
chiqish vaqti (yopilish ham ichida), uni har qanday build beradi, shuning uchun "oldin" (`14e8435`, 488-494 dan
keyingi build, nusxasi saqlab qo'yilgan) bilan shu ustun solishtiriladi. Release, llvmpipe, `ZAMIN_MAX_FRAMES=1`
(avtomatik rejim: fon yuklash o'chiq).

| Dastur, loyiha | Jarayon, oldin (mediana) | Jarayon, keyin (mediana) | Birinchi kadr, keyin (dvigatel / layer'lar / 1-kadr) |
|---|---:|---:|---|
| Editor, Showcase3D | 1 094 ms | **750 ms** | 670 ms (182 / 88 / 422) |
| Editor, Platformer2D | 783 ms | **540 ms** | 469 ms (189 / 66 / 208) |
| Player, Showcase3D | 587 ms | 578 ms | 538 ms (217 / 70 / 276) |
| Player, Adventure3D | 561 ms | 589 ms | 551 ms (225 / 66 / 243) |
| Player, Platformer2D | 365 ms | 382 ms | 345 ms (220 / 60 / 60) |

Editorni interaktiv holatdagidek (`ZAMIN_ASYNC_LOAD=1`, sahna rasmlari fonda) ochganda birinchi kadr: Showcase3D
648 ms, Platformer2D 456 ms. Player qatorlari o'zgarmadi (farq shovqin ichida, ±30 ms) - ularning vaqti quyida
"qolganlar"da.

Profil (Showcase3D editori, oldin) nimani ko'rsatdi va nima qilindi:

| Joy | Oldin | Keyin | Nima qilindi |
|---|---:|---:|---|
| ImGui shrift atlasi (birinchi kadr, "UI (ImGui)" o'zi) | 236 ms | 60-70 ms | atlasga faqat editor ishlatadigan 225 ta Font Awesome ikonkasi (Icons.h dan, `IconRanges.inl`, `Tools/gen_icon_ranges.py`) - oldin to'rt shriftning har biriga butun blok (~1 400 glif); faqat ikonka chiziladigan katta shriftga matndan faqat ASCII |
| Sahnalar paneli, sahna fayllari | 27 ms | 6 ms | har `.zscene` ning JSON'i ishchi oqimlarda parallel o'qiladi (`JobSystem::ParallelFor`) |
| Sahnalar paneli, kartalar (rasm) | 44 ms | 19 ms* | sahna rasmlari ishchi oqimda ochiladi, GPU'ga tayyor bo'lganda (fon yuklash yoqiq bo'lsa) |
| Player: boshlang'ich sahna rasmlari | ketma-ket | parallel | `SceneManager::LoadNow` sahna nomlagan rasmlarni oldindan parallel o'qiydi (`TextureLibrary::Predecode`), keyin tartib bilan yuklaydi |

\* ishchi oqimlar bilan bitta 4 yadroli CPU'da raqobatlashgani uchun asosiy oqim vaqti to'liq yo'qolmaydi.

Qolganlar (o'lchangan, tuzatilmagan): oyna va GL konteksti 52-68 ms, `Renderer3D::Init` 63-70 ms (25 ta shader
dasturi binary keshdan - llvmpipe kodni baribir qayta yaratadi), birinchi kadrda SSAO ~80 ms (drayver shader
variantlarini birinchi chizishda kompilyatsiya qiladi), shriftlar 17-43 ms (2048x2048 atlasni GPU'ga yuborish va
mipmap'lari). Batafsil: [Performance.md](Performance.md).

### Xotira (496)

RAM - jarayonning RSS (`/proc/self/statm`), "eng ko'pi" - VmHWM; "dvigatel" - modullar o'zi hisoblagan xotira
(`MemoryStats`: teksturalar, render buferlari, mesh buferlari ...; llvmpipe'da GPU xotirasi ham RAM'da). Editor:
`ZAMIN_EDITOR_STATS=1`, 30 kadr; player: `ZAMIN_PLAYER_STATS=1` (yangi: chiqishda "Xotira: ..." qatori), 60 kadr.

| Dastur, loyiha | RAM oldin (eng ko'pi) | RAM keyin (eng ko'pi) | Dvigatel hisoblagani |
|---|---:|---:|---|
| Editor, Showcase3D | 451 MB (467 MB) | **325 MB (341 MB)** | 40 MB (teksturalar 29, render buferlari 10.8) |
| Editor, Platformer2D | 367 MB (447 MB) | **242 MB (322 MB)** | 30 MB (teksturalar 19, render buferlari 10.8) |
| Player, Showcase3D | - | 262 MB (277 MB) | 32 MB (teksturalar 22, render buferlari 10.6) |
| Player, Adventure3D | - | 183 MB (263 MB) | 18 MB (teksturalar 7.5, render buferlari 10.6) |
| Player, Platformer2D | - | 159 MB (239 MB) | 20 MB (teksturalar 9.4, render buferlari 10.6) |

Editor ~125 MB kam ishlatadi: ImGui atlasi endi 2048x2048 (ikonkalar va katta shrift qisqargani uchun; oldingisi
ancha katta edi, uning alpha + RGBA nusxalari CPU'da va teksturasi llvmpipe'da RAM'da turardi) va atlasning CPU
nusxalari teksturaga yuborilgach bo'shatiladi (`ImFontAtlas::ClearTexData`, logda "Editor shrift atlasi: 2048x2048
(4 ta shrift); CPU nusxasi bo'shatildi"). Player'da ImGui yo'q: uning xotirasi o'zgarmadi; teksturalar CPU nusxasini
saqlamaydi (yuborilgach bo'shatiladi), shriftlarda faqat TTF fayli qoladi (o'lchamli atlaslar va kerning uchun).
Player'larda birinchi kadr paytida ~80 MB vaqtinchalik o'sish bor (eng ko'pi - hozirgisi; ikkinchi kadrdayoq
qaytadi), manbai aniqlanmadi (kadr ichida, ehtimol drayver).

## Gameplay combat (`ZaminBench --combat`)

`Tests/CombatBench.cpp` har kadrda faqat `Gameplay::TickCombat` ni o'lchaydi (60 Hz qadam; kadr byudjetlari
`Scene::UpdateRuntime` dagidek oldin tiklanadi) va uning heap ajratmalarini `AllocCounter` bilan sanaydi. 120 (effektlarda
60) qizdirish kadridan keyin 600 kadr o'lchanadi:

- **Combat2D / Combat3D** — 128 snaryad (pool to'la), har kadrda 32 tagacha qayta otish (spawn byudjeti); 32 otuvchi
  (1-jamoa) 32 yo'lak bo'ylab 32 nishonga (2-jamoa) otadi, 32 devor har ikkinchi yo'lakni to'sadi. Har 60 Hz qadamda
  128 sweep: analitik aktor testi (8 192 / qadam) va 2D'da 3 tadan Box2D nuri, 3D'da Jolt sphere cast.
- **CombatEffects** — 64 aktor × 16 effekt = 1 024 nusxa (sahna chegarasi; dizayndagi 512 × 16 = 8 192 bu chegaradan
  oshadi), har biri 0.10–0.25 s davr bilan zarar tiki: ~105 tik / qadam, snaryadsiz.

Muhit: 4 vCPU Intel Xeon @ 2.10 GHz, GCC 13.3, **Release** (`build-release`), 2026-11-21. Beshta ishga tushirish
(uchtasi qo'lda, `--update-baseline` va `--compare`):

| Ssenariy | O'rtacha `TickCombat` | p95 | Eng sekin kadr | Ajratma / kadr | Maqsad (o'rtacha) |
|---|---:|---:|---:|---:|---|
| Combat2D (Box2D) | 0.069–0.106 ms | 0.092–0.150 ms | 0.19–0.99 ms | 9.26 | ≤ 0.5 ms — bajarildi |
| Combat3D (Jolt) | 0.079–0.101 ms | 0.218–0.270 ms | 0.27–0.40 ms | 12.15 | ≤ 1.0 ms — bajarildi |
| CombatEffects (1 024 nusxa) | 0.047–0.060 ms | 0.063–0.081 ms | 0.11–0.19 ms | 21.05 | ≤ 1.0 ms — bajarildi |

Ajratmalar combat kodining o'zidan emas (`ZAMIN_ALLOC_TRACE` bilan tekshirildi): 2D/3D'da hammasi fizika
so'rovlarining natija vektorlari (`Scene::Raycast2DAll` / `SphereCast3DAll` — nur biror narsaga tekkandagina ajratadi),
effektlarda esa `RuleWorld` voqea navbatining `std::deque` bloklari. Debug buildda (taqqoslash uchun, 120 kadr)
o'rtacha 0.72 / 0.95 / 0.39 ms. Boshlang'ich natija: `Tests/Baselines/combat-release.json`;
`ZAMIN_BUILD_DIR=build-release Tests/bench.sh --only combat --compare` (kadr ms uchun shovqin chegarasi 0.2 ms —
bu raqamlar undan kichik, shuning uchun faqat katta sekinlashish ushlanadi). `ctest -L bench` da `BenchCombatSmoke`
(`--combat --frames 5`).

## Vertical slice stress darvozalari: fizika, animatsiya, o'yin ichida (`--physics`, `--animation`, `--slice-stress`)

P5 vertical slice'larining (Lantern Keeper 2D, Beacon Pass 3D — [VerticalSlices.md](VerticalSlices.md#gates)) stress
darvozalari. Uchala benchmark ham headless, natijasi JSON, va invariantlardan biri buzilsa 3 kodi bilan tugaydi (raqam
qanchalik yaxshi bo'lmasin):

- **`ZaminBench --physics`** (`Tests/PhysicsBench.cpp`) — har ssenariy ikki marta simulyatsiya qilinadi va holat
  hash'lari solishtiriladi. *Physics2D*: 60×40 m tilemap maydon, 2 048 dinamik doira/quti, `Gameplay::StepMotor` bilan
  yuradigan 64 aktor (nuqtadan nuqtaga), ikki jamoa orasida doim uchib turgan 128 `CombatWorld` snaryadi (Box2D
  nurlari). Invariantlar: maydondan chiqqan jism 0, NaN 0, tezlik ≤ 40 m/s. *Physics3D*: devorli chuqur, ikki rampa,
  65×65 relyef, 0.25 m zinapoya va 35° qiyalik; 512 jism (quti, shar, kapsula), 64 `CharacterController3D` aylana
  yo'llarda (zinapoya va qiyalik ustidan), har kadrda 128 sphere cast. Invariantlar: oyoq yerdan −0.05 m dan pastga
  tushmaydi, y < −10 yo'q, yurish mumkin bo'lgan yerda buyurilgan masofaning ≥ 80 % i bosiladi, jismlar chuqurda qoladi.
- **`ZaminBench --animation`** (`Tests/AnimationBench.cpp`; dizayndagi yozuv `--animators N --model ...` /
  `--animators N --sprite-clips` ham ishlaydi). *AnimationHumanoids*: 256 odam (`BeaconPass` ning `Hero.glb` modeli,
  21 suyak, Dovon sahnasidagi haqiqiy controller: Locomotion 1D blend + UpperBody/Hit qatlamlari + event'lar), tezlik
  barcha chegaralardan o'tib turadi, har 1.7 s trigger, yarmi ekrandan tashqarida (`CullPose`). Bitta oqim va 3 ishchi
  oqim (`JobSystem`). Invariantlar: blend og'irliklari yig'indisi 1 ± 1e-5, NaN/inf matritsa yo'q, culled animator
  pozalanmaydi, 0 va 3 ishchidagi poza hash'i bir xil, event'lar 20/30/60/144 Hz da har animator uchun bir xil soni
  (oynaning oxirida ±1), uch zarba = uch Hit event. *AnimationSprites*: 1 024 modelsiz animator (`DriveSpriteClips`,
  2D blend tree: Idle + 4 yo'nalish, yo'nalish doim aylanadi). Invariantlar: kadr = StartFrame + floor(faza × soni)
  aniq, yo'nalish almashganda faza uzilmaydi, klip event'lari to'rt tezlikda bir xil.
- **`ZaminBench --slice-stress`** (`Tests/SliceStressBench.cpp`) — slice darajasi o'zining haqiqiy graflari (qahramon,
  daraja boshqaruvchisi, dushmanlar), gameplay dunyolari va fizikasi bilan, chizishsiz; o'lchanadigani har kadrdagi
  `Scene::UpdateRuntime` (skriptlar, AI, fizika, animatsiya, combat, qoidalar, UI bog'lanishlari). *SliceStress2D*
  (Orchards): dushmanlar sahnaning 32 AI miyasi chegarasigacha ko'paytiriladi, 200 quti (Box2D), qahramonga 64 tagacha
  uchayotgan snaryad, 256 qo'shimcha sprite Animator, 48 Light 2D. *SliceStress3D* (Dovon): 24 animatsiyali qorovul
  (skinned, haqiqiy locomotion controller), qahramon, 64 snaryad, lager atrofida 200 jism (Jolt). Qahramon har kadrda
  davolanadi (jang to'xtamaydi). Invariantlar: graf xatosi va "Visual graph" / instruksiya byudjeti xabari yo'q, har
  miya sog'lig'iga mos tirik yoki o'lik, snaryadlar soni yetadi.

Muhit: 4 vCPU Intel Xeon @ 2.10 GHz, GCC 13.3, **Release** (`build-release`), 2026-11-22. Beshta ishga tushirish
(uchtasi qo'lda, `--update-baseline` va `--compare`), oraliq — eng kichik va eng katta natija:

| Ssenariy | O'rtacha | p95 | Ajratma / kadr | Maqsad (dizayn) |
|---|---:|---:|---:|---|
| Physics2D — fizika qadami ("Fizika" profiler bo'limi) | 1.56–2.15 ms | 2.3–3.1 ms | 128.5 | o'rtacha ≤ 4, p95 ≤ 6 ms — bajarildi |
| Physics2D — butun kadr (`UpdateRuntime`) | 2.25–3.04 ms | 3.18–4.20 ms | | |
| Physics3D — qadam + 64 Character Controller | 1.51–1.80 ms | 2.00–2.17 ms | 82.6 | o'rtacha ≤ 8, p95 ≤ 12 ms — bajarildi |
| Physics3D — 128 sphere cast | 0.23–0.28 ms | | | |
| AnimationHumanoids, 3 ishchi (butun kadr) | 0.57–0.82 ms | 0.66–1.10 ms | 15.2 | o'rtacha ≤ 1.5, p95 ≤ 2.0 ms — bajarildi |
| AnimationHumanoids, bitta oqim (butun kadr) | 0.99–1.73 ms | 1.23–2.07 ms | **0.0** | o'rtacha ≤ 4.0 ms, ajratma 0 — bajarildi |
| AnimationSprites, bitta oqim — Animator yangilash | 0.30–0.48 ms | 0.38–0.61 ms | **0.0** | o'rtacha ≤ 0.6 ms, ajratma 0 — bajarildi |
| AnimationSprites, bitta oqim (butun kadr) | 0.39–0.66 ms | 0.51–0.85 ms | 0.0 | |
| SliceStress2D (Orchards, o'yin kodi) | 2.80–3.42 ms | 3.00–4.71 ms | | o'rtacha ≤ 6, p95 ≤ 10 ms — bajarildi |
| SliceStress3D (Dovon, o'yin kodi) | 3.89–5.45 ms | 5.41–6.37 ms | | o'rtacha ≤ 8, p95 ≤ 12 ms — bajarildi |

Invariantlar har beshta ishga tushirishda o'tdi: 2D maydondan chiqqan 0, eng katta tezlik 6.88 m/s; 3D oyoq-yer
orasi eng kami −0.0002 m, botgan 0, yurilgan masofa eng kami 90.7 % (o'rtacha 96.4 %), chuqurdan chiqqan jism 0; ikki
simulyatsiya hash'i teng (2D `70e0fe7db44ee00b`, 3D `6f7e6ad276c04e79`). Animatsiya: blend og'irlik xatosi eng ko'pi
2.98e-08, 0 / 3 ishchi poza hash'i `8eb468b84863e185` ikkalasida, event'lar 20/30/60/144 Hz da 440/432/440/440
(odamlar) va 960 × 4 (sprite'lar) — animator bo'yicha mos kelmagan 0; sprite kadr/faza buzilishi 0 (600 kadrda 14 223
klip almashishi). Slice stress: 2D 32 miya (32 tirik), o'rtacha 50 snaryad uchib turadi; 3D 27 miya, o'rtacha 58
snaryad; graf xatosi 0.

Ajratmalar: animator yangilash bitta oqimda endi ajratmasiz — oldin 24 odamga 223 / kadr va 64 sprite'ga 870 / kadr
edi (har kadr yangidan yaratiladigan vazifa ro'yxatlari, poza signaturasi, event vektorlari, klip nomi satrlari;
`thread_local` kadr xotirasiga o'tkazildi, `ZAMIN_ALLOC_TRACE` bilan tekshirildi). 3 ishchidagi 15.2 / kadr —
`JobSystem::ParallelFor` ning o'zi (har chaqiruvda vazifa obyektlari). Fizikadagi ajratmalar Box2D/Jolt kontakt
natijalari va nur so'rovlarining vektorlari; dizayn ularga byudjet qo'ymagan, raqam boshlang'ich natijada qayd etilgan.

Boshlang'ich natijalar: `Tests/Baselines/physics-release.json`, `animation-release.json`, `slicestress-release.json`;
`ZAMIN_BUILD_DIR=build-release Tests/bench.sh --only physics,animation,slicestress --compare`. `ctest -L bench` da
`BenchPhysicsSmoke` (`--physics --bodies 200 --actors 12 --frames 40`), `BenchAnimationSmoke` (`--animation --humanoids 24
--sprites 64 --frames 30 --jobs 2`) va `BenchSliceStressSmoke` (`--slice-stress --frames 20`) — invariantlar bilan. Dizayndan farq: o'yin ichidagi stress (A3, P3) Player
`--benchmark` va alohida `Stress` sahnalari o'rniga headless `--slice-stress` da — haqiqiy darajalar nusxasi ustida
(slice assetlari 6 MiB byudjetga yaqin, llvmpipe chizishi o'yin kodining vaqtini ko'mib yuboradi).

## Vertical slice'lar Player'da: profiling va regressiya darvozasi (`Tests/slice_bench.py`)

Ikkala slice'ning har sahnasi (Menu, Orchards, Yertola, Dovon) **Release** `ZaminPlayer --benchmark 300
--benchmark-warmup 90` da, qat'iy o'yin soati bilan (`ZAMIN_CAPTURE_FPS=30`), darajalarda demo pilot o'ynaydi
(`ZAMIN_DEMO=1`), menyular o'yinchi ko'rgandek (demosiz). Ikki oyna: 284x160 `--quality Low` (darvoza: o'yin kodi) va
1280x720 (faqat baseline bilan solishtiriladi — u llvmpipe rasterizatorining ishi). *O'yin kodi* — Player benchmark
hisobotidagi yangi `GameCode`: har o'lchangan kadrda profilerning "Layer'lar (update)" bo'limi, undan chizish
("Sahnalar (chizish)", "Post-processing") ayirilgan (skriptlar/graflar, gameplay, fizika, AI, animatsiya, particle'lar,
audio). Har qator bitta HOME bilan 3 marta ishlaydi: birinchisi — birinchi sessiya (Mesa shader variantlarini shunda
kompilyatsiya qiladi va `~/.cache` da saqlaydi; o'sha ishning eng sekin kadri "birinchi sessiya" ustunida), darvoza va
baseline har raqamning eng yaxshisini oladi.

Muhit: 4 vCPU Intel Xeon @ 2.10 GHz, GCC 13.3, Release (`build-release`), Mesa llvmpipe, 2026-11-22:

| Sahna | Oyna | Obyekt (faol) | Draw call | Uchburchak / quad | Kadr o'rtacha | p95 | Eng sekin (birinchi sessiya) | O'yin kodi | p95 | RSS |
|---|---|---:|---:|---|---:|---:|---:|---:|---:|---:|
| Lantern Keeper / Menu | 284x160 Low | 57 (38) | 18 | 0 / 875 | 3.48 ms | 4.17 | 6 (7) | 0.32 ms | 0.40 | 197 MiB |
| Lantern Keeper / Orchards | 284x160 Low | 211 (158) | 30 | 0 / 1 183 | 5.44 ms | 6.32 | 12 (117) | 2.02 ms | 2.41 | 205 MiB |
| Lantern Keeper / Yertola | 284x160 Low | 224 (162) | 33 | 0 / 860 | 5.67 ms | 6.92 | 11 (1 869) | 2.03 ms | 2.49 | 211 MiB |
| Beacon Pass / Menu | 284x160 Low | 278 (259) | 130 | 34 286 / 266 | 22.22 ms | 26.19 | 34 (34) | 0.53 ms | 0.65 | 227 MiB |
| Beacon Pass / Dovon | 284x160 Low | 377 (330) | 210 | 74 516 / 342 | 28.16 ms | 36.06 | 51 (345) | 3.50 ms | 4.40 | 236 MiB |
| Lantern Keeper / Menu | 1280x720 | 57 (38) | 31 | 0 / 875 | 54.93 ms | 63.78 | 76 (98) | 0.51 ms | 0.64 | 225 MiB |
| Lantern Keeper / Orchards | 1280x720 | 211 (158) | 43 | 0 / 1 183 | 55.71 ms | 67.06 | 76 (126) | 2.44 ms | 3.06 | 234 MiB |
| Lantern Keeper / Yertola | 1280x720 | 224 (162) | 46 | 0 / 860 | 56.06 ms | 65.18 | 85 (1 951) | 2.50 ms | 3.27 | 236 MiB |
| Beacon Pass / Menu | 1280x720 | 278 (259) | 191 | 34 286 / 265 | 208.88 ms | 250.81 | 305 (305) | 0.68 ms | 0.86 | 317 MiB |
| Beacon Pass / Dovon | 1280x720 | 377 (330) | 295 | 74 560 / 342 | 227.13 ms | 266.12 | 328 (576) | 3.73 ms | 4.74 | 261 MiB |

Darvozalar (kichik oyna): o'yin kodi o'rtacha ≤ 3 ms (2D) / ≤ 4 ms (3D) va p95 ≤ 6 ms — bajarildi (eng ko'pi 2.03 /
3.50 ms, p95 4.40 ms); kadr o'rtacha ≤ 16.6 ms (2D) — bajarildi (≤ 5.67 ms); eng sekin kadr ≤ 1000 ms — bajarildi (≤ 51
ms). Dizayndan farq: 3D uchun kadr chegarasi 33.3 ms (30 FPS) — llvmpipe Dovon'ning ~74 500 uchburchagini ikki marta
(soya xaritasi va asosiy o'tish) protsessorda chizadi, bu 284x160 da ham ~17 ms ("Sahnalar (chizish)") + 7 ms
post-processing; o'yin kodi esa 3.5 ms. Birinchi sessiyadagi 1.9 s lik kadr (Yertola, birinchi jangda) — llvmpipe'ning
yangi zarra/2D blend holati uchun shader JIT'i: shu HOME bilan ikkinchi ishga tushirishda yo'q (eng sekini 11 ms).
1280x720 qatorlari (va kichik oynaning hammasi) `Tests/Baselines/slices-release.json` ga yozilgan; `--compare` har
raqamni `bench_compare.py --tolerance 15` bilan solishtiradi. Tekshiruv: 2 ishga tushirishli qayta o'lchash 40 raqamdan
3 tasini (1280x720 da Lantern Keeper Menu p95 +15.3 %, Yertola p95 +19.7 % va o'yin kodi +17 %) sekinlashgan deb belgiladi —
o'sha qatorlar 3 ishga tushirish bilan −8.1 … +3.4 % (16 raqam, 0 sekinlashish): 720p da llvmpipe oqimlari o'yin bilan 4
yadroni bo'lishadi. Shuning uchun standart `--runs 3` va 720p dagi p95 lar `Noisy` (ro'yxatda, hisoblanmaydi). Ajratmalar (`ZAMIN_ALLOC_TRACE`) Player'da sanalmaydi —
o'rniga RSS va engine hisoblagan xotira (`ZAMIN_SOAK_STATS`) qayd etiladi.

Soak (`--no-bench --soak 18000`, attract loop, 284x160 Low, har 30 kadrda namuna): Lantern Keeper 3 g'alaba, menyuga 3
qaytish — har safar 57 obyekt, RAM 302.9 MiB, engine 99.2 MiB (birinchi aylanadan keyingi o'sish 0.0 MiB); Dovon
mash'ali 7 g'alaba — har qaytishda 278 obyekt, RAM 269.4 → 275.5 → 278.8 MiB va keyin tekis (o'sish 9.5 MiB ≤ 32),
engine 39.0 MiB; logda xato yo'q.

## Natijalarni saqlash va solishtirish (`Tests/bench.sh`)

`Tests/bench.sh` headless benchmarklarni (ECS, serializatsiya, prefablar, combat, fizika, animatsiya, slice stress) ishga tushiradi va natijalarni
`<build>/Bench/*.json` (+ to'liq matn `*.txt`) ga yozadi; `bench` CMake target'i ham shu. `--compare` ularni
`Tests/Baselines/<nom>-<build turi>.json` bilan `Tests/bench_compare.py` orqali solishtiradi: biror raqam
`--tolerance` (standart 25 %) dan va birlikning shovqin chegarasidan (ms 0.5, kadr ms 0.2, ns 100, bayt 16, % 2)
ko'proq yomonlashsa skript 1 kodi bilan tugaydi. Fayl I/O qatorlari (`Noisy`) ko'rsatiladi, lekin hisobga olinmaydi.
Bitta o'lchov band kompyuterda (boshqa jarayon, disk keshi) bir nechta raqamni 25 % dan ko'proq sekinlashtirishi
mumkin (bu kompyuterda bir ishga tushirishda 79 tadan 20 tasi, keyingisida 1 tasi - kod o'zgarmagan): shuning uchun
sekinroq ko'ringan benchmark yana ishga tushiriladi (`--repeat`, standart jami 3 marta) va har raqamning eng yaxshi
natijasi solishtiriladi - haqiqiy sekinlashish har ishga tushirishda ko'rinadi. `--repeat 1` - bitta o'lchov.
`--update-baseline` natijalarni yangi boshlang'ich natija qiladi. Boshlang'ich natijalar shu sahifadagi kompyuterda
olingan: boshqa kompyuterda avval `--update-baseline` bilan o'sha yerda yarating, keyin o'zgarishni solishtiring.

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-release --target ZaminBench
ZAMIN_BUILD_DIR=build-release Tests/bench.sh --compare      # o'zgarish tezlikni yomonlashtirdimi? (shubhalisi 3 marta)
ZAMIN_BUILD_DIR=build-release Tests/bench.sh --update-baseline
```

`ctest -L bench` benchmarklarni kichik o'lchamda (1 000 obyekt) ishga tushiradi - ular ishlashda davom etishini
(va `--serialize` o'z tekshiruvini) CI'ning tezkor testlari ichida tekshiradi; raqamlar u yerda solishtirilmaydi.

## Boshqa benchmarklar

| Dastur | Nima o'lchaydi |
|---|---|
| `ZaminBench` (`particle_bench` target) | particle tizimlari (~50 000 particle), `--jobs N`, `--profile` |
| `ZaminBench --animators N` | skinned animator'lar pozasi, so'ng Animator culling bilan ko'rinishni belgilash (`MarkVisibleAnimators`) |
| `ZaminBench --queries` | sahna qidiruvlari (nom, teg, komponent indekslari) |
| `ZaminBench --serialize --prefabs` | bog'langan prefab nusxalari (saqlash, yuklash, sinxronlash, Apply; `Tests/Baselines/prefabs-release.json`) |
| `ZaminBench --nav` | navigatsiya (bake, yo'l qidirish, Nav Agent'lar) |
| `ZaminBench --fonts` | shrift atlasi: rasterlash va diskdagi kesh, glif qidirish, kerning bilan matn joylash |
| `ZaminBench --combat` | gameplay snaryadlari (2D nurlar / 3D sphere cast) va effekt kitoblari, `TickCombat` ms / kadr va ajratmalar (yuqorida) |
| `ZaminBench --physics` / `--animation` / `--slice-stress` | vertical slice stress darvozalari: Box2D/Jolt + motor/Character Controller, odam va sprite animator olomoni, slice darajalari to'lib-toshganda o'yin kodi (yuqorida) |
| `Tests/startup_bench.py` | editor va player ishga tushish vaqti: birinchi kadrgacha (dvigatel / layer'lar / 1-kadr) va jarayon vaqti, ikki build solishtirish (`--bin`, `--module-dir`) |
| `Tests/player_bench.sh` | namuna o'yinlarning boshlang'ich sahnalari ZaminPlayer'da (ms / kadr, p95, p99) |
| `Tests/samples_bench.py` | to'qqiz namuna o'yin bot o'ynayotganda: kadr vaqti, eng sekin kadr va uning profili, obyektlar soni, o'yin kodining CPU vaqti, byudjetlar (`--check`) — natija va topilgan muammolar: [Samples.md](Samples.md#performance) |


## P1 xarita render workload'i (2026-11-10)

- Terrain: 257×257 nuqta, 256×256 m, 64 ta 32×32 katakli bo'lak; to'liq 131072, uzoq LOD 128 uchburchak.
- 2D: 4096×4096 katak, 65536 render bo'lagi, 24×24 birlik ortografik ko'rinish; oldingi 65536 leaf test o'rniga
  119 hierarchy/leaf bounds testi, 4 ko'ringan bo'lak. Yopiq xarita bir root testida chiqarildi.
- To'g'rilik: 656 unit / 12 render / 265 editor qadam, transform/reference va manifold testlari, GL cache tekshiruvi.

Player o'lchovi: bir xil saqlangan terrain, kamera `(128,4096,128)`, far clip 10000, 1280×720, VSync o'chirilgan,
10 warmup + 60 o'lchangan kadr, har variant 3 marta. Backend — Mesa llvmpipe, 2 worker, VPS software rendering.
Uch run o'rtachalarining mediani: **LOD on 65.074 ms**, **LOD off 127.637 ms**. Off run'lari 100.319–132.567 ms
oralig'ida o'zgargan; bu virtual muhitdagi cheklangan o'lchov. Haqiqiy GPU yoki barcha sahnalar uchun tezlik
va'dasi emas. To'liq frame va percentile hisobotlari `build-review/Professional/lod-benchmark.json` va
`lod-on-*.json` / `lod-off-*.json` da. Generic benchmark CLI — [Runtime](Runtime.md); CPU workload
regressiyalari `TerrainLODTests` va `TilemapVisibilityTests` fayllarida.
