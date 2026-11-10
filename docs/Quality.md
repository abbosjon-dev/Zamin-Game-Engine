# Sifat hisoboti

Ikki o'lchov: avval loyihaning **yakuniy regressiyasi** (M25, 500-vazifa, 2026-11-09), so'ng M22 "Sifat va testlar"
milestone'i oxiridagi batafsil hisobot (sanitizerlar, qamrov, formatlash statistikasi). Hamma raqamlar shu kompyuterda
haqiqatan o'lchangan (4 vCPU, 15 GB RAM, Ubuntu 24.04, GCC 13.3, Clang 18.1, Mesa llvmpipe - GPU yo'q). Testlar va
ularni ishga tushirish: [Testing.md](Testing.md); ishlash tezligi: [Performance.md](Performance.md).

## Yakuniy regressiya (M25, 2026-11-09)

Hamma 500 vazifadan keyin (`c2b82dd` + hot reload tuzatishi). Har build noldan yoki to'liq sozlangan holda; ctest
to'plamlari Debug build'da, bir vaqtda boshqa build'lar ham ishlab turgan (shuning uchun vaqtlar odatdagidan uzun).

| Tekshiruv | Natija |
|---|---|
| GCC 13 Debug (`build/`) | build: **0** ogohlantirish; `ZaminTests` **651 test, 0 xato** (53 s) |
| GCC 13 Release (alohida build papkasi, to'liq: engine, editor, player, testlar, benchmark, 9 o'yin moduli) | build: **0** ogohlantirish; `ZaminTests` **651 / 0** (12 s) |
| Clang 18 Debug (`build-clang`, to'liq: 437 qadam) | build: **0** ogohlantirish; `ZaminTests` **651 / 0** (38 s) |
| ASan + UBSan (GCC Debug, `Tests/sanitize.sh address,undefined`) | `ZaminTests` **651 / 0, 0 sanitizer xabari** (198 s) |
| Rendering etalonlari (`Tests/render_tests.sh`) | **12 / 12** - GCC Debug, GCC Release va Clang build'larida |
| Hujjatlar (`DocsCheck`, `DocsSnippets`) | 0 muammo (29 hujjat, 35 rasm); hujjat kodi: 25 skript klassi, 12 fayl kompilyatsiya qilindi |
| Benchmark smoke (`BenchEcsSmoke`, `BenchSerializeSmoke`) | o'tdi |
| Editor smoke testi (`EditorSmoke`) | Showcase3D **221 / 221**, Platformer2D **263 / 263** qadam, 0 xato, 0 OpenGL bayrog'i, 0 ImGui tekshiruvi |
| Skript hot reload (`ScriptHotReload`) | o'tdi (21 s) - birinchi ishga tushirishda yiqildi, tuzatildi (pastda) |
| Eksport testi (`ExportTest`) | o'tdi (137 s) |
| Namuna o'yinlar (`ctest -L samples`: `SampleAssets` + 9 o'yin) | **10 / 10** o'tdi |
| Eksport qilingan namunalar (`SampleExports`) | **9 / 9** namuna eksport qilindi va eksport qilingan papkadan o'ynaldi (6 ta bot demo, 3 ta smoke; 891 s) |
| Fizika determinizmi (Box2D, Jolt) | `ZaminTests` ichida, hamma build'da o'tdi |
| Formatlash (`Tests/format_check.sh --base 14e8435`) | o'zgargan qatorlarda 2 bo'lak tuzatildi, endi toza |
| Kod hajmi | 528 C++ fayl (vendor'siz): engine 385 fayl / 122 600 qator, editor 57 900, testlar 53 800, o'yinlar 14 900 |

**Regressiyada topilgan xato.** `ScriptHotReload` og'ir yuklama ostida (bir vaqtda ikki build) yiqildi: editor modul
faylini linker hali yozib bo'lmasdan o'qidi - `Skript moduli yuklanmadi: ... file too short`, keyingi so'rovda
yuklandi, lekin logdagi xato testni yiqitdi. Fayl hajmi va vaqti ikki so'rov (0.4 s) davomida o'zgarmasligi yetmas
ekan: linker shuncha to'xtab qolishi mumkin. Endi editor ELF sarlavhasidagi bo'limlar jadvali (linker oxirida yozadi)
fayl ichida ekanini ham tekshiradi (`IsElfFileComplete`, `EditorLayer.cpp`); qayta ishga tushirishda test o'tdi.

## M22 hisoboti: qisqacha

| Ko'rsatkich | Natija |
|---|---|
| Unit testlar (`ZaminTests`) | **627 test, 0 xato** - GCC Debug 39 s, ASan 107 s, Clang UBSan 48 s |
| Rendering etalon testlari | **12 / 12** o'tdi (llvmpipe, 480x270, chegara 0.5 % piksel) |
| Editor smoke testi | Showcase3D **221 / 221** qadam, Platformer2D **263 / 263** qadam, 0 xato, 0 OpenGL xatosi, 0 ImGui tekshiruvi |
| Skript hot reload testi | o'tdi (19 s): maydon qiymatlari saqlanadi (`Speed=7.5`, `Count=3`), yangi maydon `Extra=42`, yangi kod ishlaydi, logda xato yo'q |
| Eksport testi va o'yin build'lari | **3 / 3** o'yin eksport qilindi, tekshirildi va paketlandi (11-14 MB `.tar.gz`) |
| Fizika determinizmi | Box2D va Jolt: 4 ishga tushirish (ikki marta, saqlash / yuklashdan keyin, teskari tartibda qurilgan) bit darajasida bir xil |
| AddressSanitizer + LeakSanitizer | **0 xabar**: unit testlar, 3 o'yin, editor, editor smoke testi (484 qadam) |
| UndefinedBehaviorSanitizer | **0 xabar** bizning kodda: GCC (avvalgi o'lchov) va Clang (bu o'lchov) |
| Kod qamrovi (gcov) | engine **82.6 %** qator (unit testlar) / **89.3 %** (`--gui` bilan); editor 43.6 %; hammasi 70.0 % |
| Ogohlantirishlar (`-Wall -Wextra -Wpedantic`) | **0** - GCC Debug / Release, Clang Release va Clang Debug (UBSan) build'larida |
| Formatlash (`.clang-format`) | kod bazaning 3.11 % qatori o'zgarardi; 491 dan 209 fayl umuman o'zgarmasdi |
| Kod hajmi | 491 C++ fayl, ~233 500 qator (vendor'siz): engine 117 700, editor 57 300, testlar 51 900 |

## Testlar

| Qatlam | Soni | Vaqt | Izoh |
|---|---|---|---|
| Unit testlar | 627 test, 92 bo'lim | 39 s (Debug) | GPU'siz; eng sekinlari: `PhysicsDeterminism2D`, `PhysicsDeterminism3D`, `SerializationFuzz` |
| Rendering etalonlari | 12 holat | 2 s (xvfb, llvmpipe) | `Tests/Reference/*.png` |
| Editor smoke testi | 2 loyiha, 484 qadam, 1 434 kadr | 163 s (106 + 57 s) | har oyna, sahifa, menyu, popup, inspektor |
| Hot reload testi | 1 loyiha, 2 versiya | 19 s (editor kompilyatsiyasi 7.9 s) | `Tests/Fixtures/HotReload` |
| Eksport testi | 3 o'yin x 5 bosqich | 84-109 s har o'yin | paket ochilib, yana ishga tushiriladi |
| ctest | 6 test (`gui` label - 3, `bench` - 2) | | `ctest -LE gui` CI'da |
| Benchmark smoke testlari | 2 | soniyalar | `ctest -L bench` |

Unit testlar vaqti taqsimoti (Debug): 244 test 1 ms dan tez, 217 - 1-10 ms, 119 - 10-100 ms, 38 - 0.1-1 s, 9 - 1 s
dan sekin (ular umumiy vaqtning 60 %).

## Sanitizerlar

| Build | Nima ishga tushirildi | Natija | Vaqt |
|---|---|---|---|
| ASan + LSan (GCC Debug, `build-asan`) | `ZaminTests` | 627 test, 0 xato, 0 xabar | 107 s |
| | `ZaminPlayer` Platformer2D / Showcase3D / Adventure3D (60 kadr, xvfb) | 0 xabar | 7 / 17 / 16 s |
| | `ZaminEditor` Showcase3D (60 kadr) | 0 xabar | 16 s |
| | editor smoke testi (Showcase3D 221 qadam, Platformer2D 263 qadam) | 0 xato, 0 xabar | 205 s |
| UBSan (Clang Debug, `build-ubsan`) | `ZaminTests` | 627 test, 0 xato, 0 xabar | 48 s |
| UBSan (GCC), alohida LSan | `ZaminTests` (avvalgi o'lchov, 421-423) | 0 xabar | |
| TSan | faqat sozlanishi tekshirilgan | ishga tushirilmagan | |

Bostirish fayllari (`Tests/sanitizers/`), har biri izohi bilan - hammasi uchinchi tomon kodi:

| Fayl | Yozuvlar | Nima |
|---|---|---|
| `lsan.supp` | 3 | miniaudio PulseAudio backend'i (server bo'lmaganda kontekst bo'shatilmaydi), uning JACK tekshiruvi, Mesa llvmpipe'ning `glGenerateMipmap` dan keyingi keshi |
| `ubsan.supp` | 1 | miniaudio'ning MurmurHash3'i manfiy indeks bilan ko'rsatkich qo'shadi (`pointer-overflow`) - Clang UBSan topdi |
| `asan.supp`, `tsan.supp` | 0 | |

Milestone davomida sanitizerlar topgan va tuzatilgan xatolar:

1. **ASan, use-after-free** - mixer guruhini (bir nechta bola guruh / tovush bilan) o'chirish va `AudioEngine::Shutdown`:
   miniaudio'ning `ma_node_uninit` faqat birinchi kiruvchi tugunni ajratardi. `DetachAllInputs` bilan tuzatildi,
   `AudioMixerGroupTeardown` testi.
2. **UBSan** - test enum'ida yaroqsiz qiymat (enum oralig'idan tashqari); [Scripting.md](Scripting.md#4-inspektor-maydonlari-zm_field) da `ZM_FIELD_ENUM` qoidasi.
3. **Clang UBSan, pointer-overflow** (bu o'lchov) - miniaudio implementatsiyasi engine'ning `AudioEngine.cpp` ichida
   kompilyatsiya qilinardi, shuning uchun uchinchi tomon kodining xabari ham bizning kodimizniki kabi fatal edi (testlar
   150-testda to'xtardi). Implementatsiya o'z translation unit'iga ko'chirildi (`Zamin/src/Zamin/Audio/MiniaudioImpl.cpp`,
   konfiguratsiya - `Miniaudio.h`): sanitizer build'da u vendor kutubxonalar kabi tiklanadigan (recoverable), xabar
   izohi bilan `ubsan.supp` da; engine'ning qolgan kodida har xabar hamon fatal.

## Kod qamrovi

`Tests/coverage.sh` (GCC, `--coverage -O0`), 2026-11-08 (batafsil: [Coverage.md](Coverage.md)):

| Qism | Unit testlar | `--gui` bilan (player, editor smoke) |
|---|---|---|
| Engine (Zamin), 59 655 qator | **82.6 %** qator / 86.3 % funksiya | **89.3 %** / 92.4 % |
| Editor, 38 277 qator | - | 43.6 % / 57.5 % |
| Player | - | 33.5 % / 50.0 % |
| Namuna o'yinlar | - | 27.3 % / 29.3 % |
| Hammasi, 101 402 qator | 82.6 % | 70.0 % / 82.6 % |

Eng kam qamralgan engine modullari (unit testlar): `Zamin/Platform` 8.2 % (oyna, OpenGL - GPU kerak; `--gui` bilan
59.0 %), `Zamin/Renderer` 56.9 % (85.3 %), `Zamin/Scripting` 67.8 % (modul yuklash - hot reload testi qamraydi).

## Ogohlantirishlar

Bizning target'lar (engine, editor, player, testlar, benchmarklar, namuna o'yin modullari) `-Wall -Wextra -Wpedantic`
bilan, vendor sarlavhalari `-isystem` orqali (bizning `Zamin/src` hech qayerda tizim papkasi emas - aks holda engine
sarlavhalaridagi ogohlantirishlar engine'ning o'z fayllarida yashirinardi; `cmake/Dependencies.cmake` buni tekshiradi);
`-Wno-missing-field-initializers` faqat (sozlamalar struct'larining birinchi maydonlarini sanash ataylab qilingan, ~70 joy).

| Build | Qadam | Ogohlantirish |
|---|---|---|
| GCC 13 Debug (`build/`) | to'liq | 0 |
| GCC 13 Debug (`build/`), `Zamin/src` endi engine fayllarida ham `-I` (avval ImGui orqali `-isystem` edi) | to'liq qayta build (498 fayl) | 0 |
| Clang 18 Release, xuddi shu holatda | engine (`libZamin`, 193 fayl) | 0 |
| GCC 13 Debug + ASan (`build-asan`, bu o'lchov) | 517 (engine, editor, player, testlar, 3 modul, vendor) | 0 |
| Clang 18 Debug + UBSan (`build-ubsan`, bu o'lchov) | 432 (engine, testlar, vendor) | 0 |
| GCC Release, Clang Release (424-vazifa o'lchovi) | to'liq | 0 |

## Formatlash

`.clang-format` mavjud uslubni tasvirlaydi (4 bo'shliq, Allman qavslar, `namespace Zamin {`, ustun chegarasi yo'q).
`Tests/format_check.sh --stats` (clang-format 18.1.3): 491 fayl, 233 505 qatordan **7 264 qator (3.11 %)**
o'zgarardi, **209 fayl** umuman o'zgarmasdi. Farq turlari (`Tests/format_stats.py` taxminiy ajratadi):

| Tur | Qator | Sabab |
|---|---|---|
| bir qatorli `{ ...; ...; }` bloklar | 1 547 | clang-format bir qatorda bir nechta ifodani hech qachon qoldirmaydi |
| bir qatorli qisqa enum'lar | 1 171 | qavslar yangi qatorda bo'lganda clang-format 18 har enum'ni yoyadi |
| bir qatorda bir nechta ifoda (`a.X = 1; a.Y = 2;`) | 843 | (yuqoridagi kabi) |
| qo'lda bo'lingan `{...}` ro'yxatlar | 829 | ustun chegarasiz clang-format ro'yxat ichidagi bo'linishlarni saqlamaydi |
| ?: zanjirlari | 786 | zanjirdagi har shartni o'z qatoriga tekislaydi |
| bir qatorda bir nechta element (enumerator, ro'yxat) | 679 | har elementni o'z qatoriga qo'yadi |
| case qatorlarini qo'lda tekislash | 280 | tekislash usuli fayldan faylga farq qiladi |
| boshqa (bo'linish, chekinish) | 1 129 | qisman haqiqiy nomuvofiqliklar (noto'g'ri chekinish) |

Shuning uchun kod baza qayta formatlanmaydi va CI'dagi tekshiruv faqat o'zgargan qatorlarda, ogohlantiruvchi.
Sozlash jarayonida ColumnLimit 120 / 140 / 160 sinab ko'rildi (21.6 / 17.3 / 16.8 % farq), `0` eng yaqini;
`LambdaBodyIndentation: OuterScope` tanlandi - standart qiymat bilan clang-format 18 bir nechta ko'p qatorli lambda
oladigan chaqiruvlarda yopuvchi qavsni oxirgi ifoda qatoriga tortardi (`...; }, [this]() {` - 93 qator, 6 fayl).

## Benchmarklar (Release boshlang'ich natijalari)

Batafsil: [Benchmarks.md](Benchmarks.md). Rendering - llvmpipe (dasturiy rasterizator), shuning uchun mutlaq ms
haqiqiy GPU'dagidan ancha katta; raqamlar o'zgarishlarni solishtirish uchun.

| Benchmark | Natija |
|---|---|
| 10 000 sprite | 24.5 ms / kadr (GPU bilan), 3 draw call |
| 1 000 mesh, instancing bilan / siz | 136.5 / 141.7 ms, 46 / 2 027 draw call |
| ECS: 10 000 `CreateEntity` | 2.29 ms (229 ns / ta) |
| ECS: `Scene::Copy` 10 000 obyekt (editor Play) | 6.93 ms |
| ECS: 1 000 skript `OnUpdate` | 0.308 ms / kadr |
| Serializatsiya: 10 000 obyektli 2D sahna, JSON saqlash / yuklash | 119 / 176 ms (8.8 MB) |
| Serializatsiya: binar kesh bilan yuklash | 99 ms (3.2 MB) |
| Eksport qilingan o'yin (Debug engine, llvmpipe, 120 kadr) | Platformer2D, Showcase3D, Adventure3D - `benchmark.json` artefaktda |

Benchmarklar topgan va tuzatilgan muammolar: skriptlarning `Destroy` navbati kvadratik edi (10 000 ta - 525 ms, endi
87 ms Debug'da; `DeferredDestroyOnceEachAndLinear`).

## Milestone'da topilgan boshqa xatolar

| Vosita | Xato | Tuzatish |
|---|---|---|
| Fizika determinizm testi | jismlar / bo'g'inlar komponent hovuzi tartibida yaratilardi: editor Play va eksport qilingan o'yin boshqacha simulyatsiya qilardi | sahna tartibida yaratish, `Scene::Copy` ierarxiya tartibida |
| Clang Release build | particle'larning tasodifiy sonlari GCC / Clang'da boshqa tartibda; FMA qisqartirish farqi; xotira testi optimallashtirilardi | ifodalar ajratildi, `-ffp-contract=off`, volatile yozuv |
| Clang build | testlar umuman kompilyatsiya bo'lmasdi (`std::pair` to'liqmas tur bilan) | `Member` struct |
| Hot reload testi | linker yozayotgan modul yuklanardi; quvurga log to'liq buferlanardi; `/tmp/zamin-modules` da 423 MB nusxa | barqarorlikni kutish, qator buferi, `RemoveStaleModuleCopies` |
| Kod qamrovi | editor smoke testi Inspektor'ni hech chizmasdi (tab yashirin) | tab oldinga chiqariladi: Editor/Panels qamrovi 21.2 → 41.6 % |
| Editor smoke testi | (topilgan xatolar yo'q - ataylab kiritilgan log xatosi, GL xatosi va ImGui `End()` xatosi ushlandi) | |

## CI

`.github/workflows/linux.yml` - 4 job: `build` (GCC va Clang, Release: unit testlar, rendering, editor smoke, hot
reload, eksport + o'yin build'lari, skrinshotlar), `sanitizers` (`asan`, `ubsan-clang`), `format`, `coverage`. Har
qadam mahalliy kompyuterda ham ishga tushirilgan skriptni chaqiradi. Artefaktlar: unit test natijalari (JSON, JUnit),
rendering rasmlari va farqlari, editor smoke loglari va rasmlari, eksport loglari va skrinshotlari, **o'yin
build'lari** (`player-builds-<cc>`: 3 ta `.tar.gz` + `.sha256`, 14 kun), sanitizer loglari, qamrov hisoboti.

Diqqat: workflow GitHub Actions'da shu ish davomida ishga tushirilmagan - YAML Python bilan tekshirilgan va har qadam
buyrug'i mahalliy kompyuterda bajarilgan.

## Ma'lum kamchiliklar va keyingi qadamlar

- **ThreadSanitizer** hali ishga tushirilmagan (job tizimi, audio oqimi, asset yuklash oqimlari).
- **Editor qamrovi** 43.6 %: ko'p panellarning tugma bosish / sudrash yo'llari faqat qo'lda sinaladi; editor smoke
  testi oynalarni ochadi, lekin ular bilan o'zaro ta'sir qilmaydi.
- Rendering faqat Mesa llvmpipe'da sinaladi (haqiqiy GPU drayverlari, boshqa OpenGL implementatsiyalari yo'q);
  Windows / macOS build'lari yo'q.
- `clang-tidy` / statik tahlil hali qo'llanmagan.
- `Zamin/Events` qamrovi past (18.8 %, kichik modul) va `Zamin/Scripting` 67.8 %.
- Formatlash tekshiruvi ogohlantiruvchi: clang-format uy uslubining ba'zi odatlarini ifodalay olmaydi.

## Qayta o'lchash

```bash
./build/bin/ZaminTests --timing --slowest 10
Tests/render_tests.sh && Tests/editor_smoke.sh && Tests/hot_reload_test.sh && Tests/package_games.sh
Tests/sanitize.sh address --smoke --editor-smoke --clean && Tests/sanitize.sh undefined --compiler clang --clean
Tests/coverage.sh --gui --clean
Tests/format_check.sh --stats --top 20
Tests/bench.sh --compare; Tests/render_bench.sh
```


## Dastlabki auditdagi yetti band

1. Scene save xatosi dirty/history/autosave holatini tozalamaydi; eksport saqlanmagan sahna bilan davom etmaydi.
2. Scene va project konfiguratsiyasi atomik yoziladi, flush/close xatolari tekshiriladi; almashtirish xatosida eski fayl saqlanadi.
3. Editor skript build'i `RunProcess(argv)` orqali, shell'siz ishga tushadi. Papka nomidagi `$()` yoki backtick bajarilmaydi.
4. Noto'liq yoki takrorlangan shader stage'lari nazorat qilinadi: log va bo'sh shader, `std::out_of_range` emas.
5. Undo uchun standart 128 MiB saqlanadigan ma'lumot chegarasi bor. Scene nusxalari, prefab fayl tahrirlari,
   nomlar va tanlovlar hisoblanadi; allocator overhead'i va vaqtinchalik serializer xotirasi bu limitga kirmaydi.
   Joriy holatning o'zi limitdan katta bo'lsa, u yagona holat sifatida qoladi. Saqlangan eski snapshot weak reference bilan eslanadi.
6. Editor xizmatlari alohida kutubxonaga, katta inspektor/build modullari va editor unit testlari alohida fayllarga ajratildi.
   Monolit `EditorLayer.cpp` va qolgan asosiy test faylini keyingi o'zgarishlarda yana maydalash mumkin; bu refaktor butun kod bazasini qayta yozmaydi.
7. Standart CPU profili x86-64/SSE2. AVX2/FMA faqat aniq opt-in bilan; [talablar](BuildAndExport.md#cpu-profili-va-editor-xizmatlarisiz-runtime).

Regressiyalar: `SafetyTests.cpp`, `EditorServicesTests.cpp`, `TerrainTests.cpp`. `ZAMIN_EDITOR_TERRAIN=test`
haqiqiy editor ichida save xatosidan keyin dirty, history va autosave saqlanishini ham tekshiradi.


Tekshiruv natijalari (2026-11-09): 645/645 unit test, 12/12 render holati, editor smoke 265/265
(OpenGL/ImGui xatosi 0), docs check 0 muammo. `ZAMIN_EDITOR_TERRAIN=test` save xatosida dirty/history/autosave
saqlanishini tasdiqladi. ImGui'siz `ZaminStandalone` kutubxonasida haqiqiy Player 271 obyektli map'ni ochdi.
Hot reload bo'shliq, apostrof va `$()` bor papkada ikki skript versiyasi bilan o'tdi; shell marker fayli yaralmadi.
QEMU x86-64/SSE2 (SSE3/SSE4/AVX/AVX2/FMA/F16C/BMI/ABM/POPCNT o'chirilgan) ostida Jolt terrain collision
regressiyasi o'tdi. Loglar va JSON/JUnit hisobotlar `build-review/Audit/` da.


### GPU terrain PBR — 2026-11-10

696/696 unit, 12/12 reference render va OpenGL 4.1 pixel-oracle terrain sinovi o'tdi. Soya atlasi resize'idan oldin caller framebuffer saqlanadi; float paint normallashishi takroriy validation'da control upload chaqirmaydi. Editor to'rtta albedo/normal/ORM qatlamini yukladi, 271 obyektli map ImGui'siz runtime'da ishladi. Export reachability barcha 12 source xaritasini oldi; Player screenshot va ko'chirilgan papkada ishga tushish tekshiruvlaridan o'tdi. Native SDK sample modullari qayta yig'ildi. Hisobotlar `build-review/TerrainPBR/` da; GPU drayverlar matritsasi va katta dunyo performance acceptance hali ochiq.


### SDK ABI, GPU profiling va foliage authoring — 2026-11-10

707/707 unit va 12/12 reference render regressiyasi o'tdi. Native fixture mos/mos emas/belgisiz/strip qilingan modullarni tekshirdi; mos kelmagan modul konstruktorlari bajarilmadi, failed dlopen oldingi klass registry'sini saqladi. Ko'chirilgan SDK configure bo'ldi; uning headeri yoki toolchain'i o'zgartirilganda aniq xato bilan rad etildi. Editor hot reload yangi maydon default'i va oldingi qiymatlarni saqladi. Native Platformer2D eksporti 26 klass bilan ishladi, screenshot va relocation o'tdi; mos kelmagan modul bilan eksport `Succeeded=false` chiqardi.

GL 4.1 GPU timestamp suite 34 completed sample, 1 pending, 0 dropped qayd etdi; bu software Mesa correctness dalili. Foliage API: region/offset, world normal (nonuniform parent), lock/hidden, invalid/runtime holat, hierarchy/save/copy testlari. Haqiqiy editorda 89 ildiz reproject va Undo/Redo o'tdi. Root/module/diagnostika/profiler screenshot va loglari `build-review/SDKQuality/` da. Alohida GPU foliage culling/LOD va physical GPU byudjeti keyingi paketda.


## Upstream integratsiyasi — 2026-11-10

GitHub `main` dagi `7f71a6c` gacha bo'lgan 13 commit lokal terrain, foliage, visual graph, C++ muharriri va SDK/ABI ishlari bilan merge qilindi. Asl tarix va ikkala yo'nalish saqlandi. Terrain shader'i ham upstream `ZaminFrame` UBO'sidan foydalanadi; upstream ELF linker-completion tekshiruvi ko'chirilgan build/hot-reload kodiga qo'shildi. Yangi UI kalitlari tarjima coverage'iga kiritildi. Shader link xatosida nol program'dan detach qilish bartaraf etildi; keyingi terrain draw va toza GL error state regressiyada tekshirildi. Visual graph GUI testi sekin software render'da pointer va tugma holatlari kuzatilishi uchun vaqt ajratadi; tugun, Undo/Redo va typed wire assert'lari saqlangan.

Linux Release / GCC 13.3 / Mesa llvmpipe tekshiruvi:

- 730/730 unit; barcha 8 ta GUI'siz CTest (hujjat/snippet, bench smoke, sample assets, native ABI va SDK manifest) o'tdi.
- 12/12 reference render; GL 4.3 foliage compute va GL 4.1 CPU fallback, 4096 instance / besh kamera / asosiy tasvirlarda 0 pixel farqi.
- GL 4.1 terrain PBR/cache/reload/shadow resize va shader link failure recovery; GPU profiler va UI'siz runtime.
- Showcase3D 224/224 va Platformer2D 266/266 editor qadami: 0 GL/ImGui xatosi.
- Visual graph haqiqiy node/wire/Undo/Redo GUI testi va C++ muharriri typing/save/completion/build/error navigation/recovery testi.
- Haqiqiy native hot reload v1→v2: maydonlar saqlandi va sahna fayli o'zgarmadi.
- FoliageWorkshop va Platformer2D eksportlari: Player, screenshot, relocation; native eksportda 26 skript klassi yuklandi.

Dalillar lokal `build-review/IntegrationReview/` da; bu natijalar physical GPU yoki boshqa OS/drayverlar uchun frame-time acceptance o'rnini bosmaydi.


### CI compiler/sanitizer kuzatuvi — 2026-11-10

`69140f2` push'idan keyingi Actions `42877104904` lokal GCC tekshiruvlari qamramagan
holatlarni ko'rsatdi: Clang sanitizer DSO link'idagi `--no-undefined` va yangi native
modulning standart GCC bilan konfiguratsiyasi. Sanitizer DSO'larida faqat Clang runtime
reference'lariga mos siyosat ishlatiladi; oddiy build'ning no-undefined himoyasi saqlanadi.
`SanitizerSharedLink` Clang ASan/UBSan/combined executable'da ishga tushish va real overflow
aniqlanishini tasdiqladi. Production export command'i haqiqiy Clang SDK konfiguratsiyasi bilan
kompilyatorni tanladi, explicit noto'g'ri GCC override esa ABI gate tomonidan rad etildi.

ASan artefaktidagi 128-byte leak stack'i GLFW `extensionSupportedGLX` orqali
`glXQueryExtensionsString` ga boradi (Mesa `25.2.8-0ubuntu0.24.04.4`). Faqat shu vendor
query uchun LSan istisnosi kiritildi; Zamin/GlfwWindow yoki barcha Mesa allocation'lari
istisno qilinmaydi. Lokal `.2` Mesa paketidagi minimal GLFW probe bu leak'ni chiqarmadi;
shu istisno ostida ataylab qoldirilgan 713-byte application allocation baribir LSan
hisobotiga tushdi. `63a6f67` uchun [Actions 42882285887](https://github.com/abbosjon-dev/Zamin-Game-Engine/actions/runs/42882285887) da
Clang Release (render/editor/hot reload/export bilan), Clang UBSan, GCC ASan (player/editor va barcha
panel smoke bilan), coverage va format job'lari muvaffaqiyatli tugadi. To‘liq matritsaning natijasi shu run sahifasida.


## World-cell manifest v1 — 2026-11-10

P3 ning birinchi qismi: 2D/3D uchun umumiy `.zworld` metadata, double bounds, qatlam koordinatalari,
barqaror UUID va deterministik dependency tartibi. Fayl/katak/dependency/nesting chegaralari, takroriy JSON
kalitlari, cycle va traversal/symlink tekshiruvi; parse xatosida oldingi obyekt saqlanadi, save atomik.
Bu bosqich entity/GPU faollashtirish, runtime streaming, editor paneli yoki eksport integratsiyasini o‘z ichiga olmaydi.

Tekshiruvlar:

- Asosiy Release build: engine, barcha targetlar va `ZaminStandalone` muvaffaqiyatli build qilindi.
- Umumiy unit suite: **735/735**; GUI'siz CTest: **9/9**, shu jumladan docs/snippet, SDK/ABI va sanitizer link regressiyasi.
- Beshta yangi manifest testi GCC va Clang 18 standalone probe'da o'tdi; Clang `-Wall -Wextra -Wpedantic -Werror` bilan kompilyatsiya qilindi.
- Xuddi shu besh test ASan+UBSan bilan instrumentlangan yangi manifest/parser kodida o'tdi; 0 sanitizer xabari. Probe mavjud, instrumentlanmagan engine kutubxonasining UUID/FileSystem funksiyalariga link qilindi; bu butun engine sanitizer build'i emas.
- 4096 katakli teskari tartibdagi dependency zanjiri, limitdan katta/buzilgan kirish, atomik fayl IO, symlink escape, worker parse va xatoda oldingi holatni saqlash tekshirildi.
- Haqiqiy UI'siz runtime kutubxonasiga link qilingan probe: roundtrip va failed-load preservation o'tdi.

Dalillar: lokal `build-review/WorldManifestReview/`; shu yerda mavjud 4096 instance'li FoliageWorkshop editorining
screenshot'i ham bor. Manifest metadata qismi UI ko'rinishini o'zgartirmaydi. Navbat: bounded async IO/decode,
versiyali cancel/unload va main-thread activation/residency byudjetlari.
