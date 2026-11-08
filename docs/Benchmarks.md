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
| `ZaminBench --animators N` | skinned animator'lar pozasi |
| `ZaminBench --queries` | sahna qidiruvlari (nom, teg, komponent indekslari) |
| `ZaminBench --serialize --prefabs` | bog'langan prefab nusxalari (saqlash, yuklash, sinxronlash, Apply; `Tests/Baselines/prefabs-release.json`) |
| `ZaminBench --nav` | navigatsiya (bake, yo'l qidirish, Nav Agent'lar) |
| `Tests/player_bench.sh` | namuna o'yinlarning boshlang'ich sahnalari ZaminPlayer'da (ms / kadr, p95, p99) |
