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
[Prefabs.md](Prefabs.md#17-ishlash-tezligi)).

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

Muhit: yuqoridagi kompyuter, **Release** (`build-m25rel`), llvmpipe, xvfb. "Oldin" - M25 boshidagi commit (`37d2916`)
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
| Matn joylash, 24 px, so'z o'rash | 16.6 ns / belgi | 15.9 ns / belgi kerning bilan (juftlik keshi) |
| "AVATAR To'p Yer" kengligi, 24 px | 171.5 px | 161.2 px kerning yoqilganda (AV -0.062 em) |

Kerning odatda o'chiq - render rasmlari o'zgarmadi; shrift `.zmeta` sida `"Import": { "Kerning": true }` yoqadi.

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
va izlarsiz). `ZaminBench --case "Bitta katta tizim"` (bitta tizim, ~49 000 particle, shovqin 2 oktava):

| | 1 oqim (oldingi xatti-harakat) | 3 ishchi + asosiy oqim |
|---|---:|---:|
| Kadr, o'rtacha | 12.2 ms | **4.0 ms** (3.1x) |

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

## Natijalarni saqlash va solishtirish (`Tests/bench.sh`)

`Tests/bench.sh` headless benchmarklarni (ECS, serializatsiya, prefablar) ishga tushiradi va natijalarni
`<build>/Bench/*.json` (+ to'liq matn `*.txt`) ga yozadi; `bench` CMake target'i ham shu. `--compare` ularni
`Tests/Baselines/<nom>-<build turi>.json` bilan `Tests/bench_compare.py` orqali solishtiradi: biror raqam
`--tolerance` (standart 25 %) dan va birlikning shovqin chegarasidan (ms 0.5, kadr ms 0.2, ns 100, bayt 16, % 2)
ko'proq yomonlashsa skript 1 kodi bilan tugaydi. Fayl I/O qatorlari (`Noisy`) ko'rsatiladi, lekin hisobga olinmaydi.
`--update-baseline` natijalarni yangi boshlang'ich natija qiladi. Boshlang'ich natijalar shu sahifadagi kompyuterda
olingan: boshqa kompyuterda avval `--update-baseline` bilan o'sha yerda yarating, keyin o'zgarishni solishtiring.

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-release --target ZaminBench
ZAMIN_BUILD_DIR=build-release Tests/bench.sh --compare      # o'zgarish tezlikni yomonlashtirdimi?
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
| `Tests/player_bench.sh` | namuna o'yinlarning boshlang'ich sahnalari ZaminPlayer'da (ms / kadr, p95, p99) |
| `Tests/samples_bench.py` | to'qqiz namuna o'yin bot o'ynayotganda: kadr vaqti, eng sekin kadr va uning profili, obyektlar soni, o'yin kodining CPU vaqti, byudjetlar (`--check`) — natija va topilgan muammolar: [Samples.md](Samples.md#ishlash-tezligi) |
