# Testlar va regressiya testlari

Zamin Engine'dagi barcha avtomatik tekshiruvlar, ular nimani qo'riqlashi va qanday ishga tushirilishi. Har bir test
mahalliy kompyuterda ham, CI'da ham (`.github/workflows/linux.yml`) **bir xil skript** bilan ishlaydi: CI qadamlari
faqat `Tests/*.sh` skriptlarini chaqiradi. Raqamlar va o'lchangan natijalar: [Quality.md](Quality.md).

## Qachon nimani ishga tushirish

| O'zgarish | Ishga tushiring | Vaqt (Debug, 4 vCPU) |
|---|---|---|
| Har qanday o'zgarish (commit oldidan) | `cmake --build build && ./build/bin/ZaminTests` (oxiri `N test, 0 xato`) | ~40 s |
| Faqat bir qism | `./build/bin/ZaminTests "Prefab*"`, `--filter "Scene*:-*Fuzz*"`, `--list` | soniyalar |
| Rendering, shader, post effektlar | `Tests/render_tests.sh` | soniyalar |
| Editor UI (panel, menyu, popup, inspektor) | `Tests/editor_smoke.sh` | ~3 daqiqa |
| Skript tizimi, modul yuklash | `Tests/hot_reload_test.sh` | ~20 s |
| Eksport, player, ishga tushirish skripti | `Tests/export_test.sh` (bitta o'yin), `Tests/package_games.sh` (uchala o'yin + paketlar) | 1-5 daqiqa |
| Namuna o'yin (skript, sahna, generator) yoki ular ishlatadigan engine qismi | `Tests/samples_test.py --game <O'yin>` (hammasi: argumentsiz), `Tests/samples_assets.py --regen` | 1-5 daqiqa bir o'yinga, ~30 daqiqa hammasi; ~20 s |
| Xotira, ko'rsatkichlar, egalik | `Tests/sanitize.sh address --smoke` (`--editor-smoke`), `Tests/sanitize.sh undefined` | 20-40 daqiqa (build bilan) |
| Tezlikka ta'sir qiladigan o'zgarish | `Tests/bench.sh --compare`, `Tests/render_bench.sh`, `Tests/player_bench.sh` | 1-5 daqiqa (Release) |
| Yangi kod, testlar yetarlimi | `Tests/coverage.sh` (`--gui`) | ~25 daqiqa |
| Formatlash | `Tests/format_check.sh` (o'zgargan qatorlar) | soniyalar |
| Hujjatlar, komponent, skript API'si yoki player varianti | `Tests/docs_check.py` (komponent o'zgarsa avval `Tests/docs_components.py --update docs/Components.md`), `Tests/docs_snippets.py` | ~1 s, ~5 s |
| Hammasi (GUI testlari bilan) | `ctest --test-dir build` (`-LE gui` - tezkorlari, `-L gui` - editor/eksport) | ~10 daqiqa |

Displey bo'lmasa (SSH, CI) GUI testlari o'zlari `xvfb-run` ni ishlatadi (`sudo apt-get install xvfb`).

## 1. Unit testlar - `ZaminTests`

`Tests/ZaminTests.cpp` (+ `Tests/TestFramework.h/.cpp`): **631 test**, GPU va oynasiz (headless) ishlaydi - engine
kutubxonasi (`libZamin.so`) bilan bog'langan oddiy dastur. Sahna, ECS, serializatsiya, prefab'lar, skriptlar va
ularning maydonlari, 2D/3D fizika, animatsiya, particle'lar, UI, audio, navigatsiya va AI, loyiha sozlamalari, player
sozlamalari, editor mantiqi (panelsiz qismlari), matematika va yordamchi kutubxonalar. To'liq ro'yxat - quyida,
[ilovada](#ilova-barcha-unit-testlar).

```bash
./build/bin/ZaminTests                         # hammasi; oxirgi qator: "628 test, 0 xato"
./build/bin/ZaminTests Prefab*                 # glob (katta-kichik harf farqsiz); *, ? siz - nomning bir qismi
./build/bin/ZaminTests --filter "Scene*:-*Fuzz*"   # bir nechta namuna (: yoki ,), '-' bilan chiqarib tashlash
./build/bin/ZaminTests Prefab* -Physics*       # xuddi shunday alohida argumentlar bilan ("-x" kabi bitta harf - parametr)
./build/bin/ZaminTests --list                  # faqat nomlar
./build/bin/ZaminTests --timing --slowest 20   # vaqt hisoboti: jami, o'rtacha, mediana, eng sekinlari, taqsimot
./build/bin/ZaminTests --report r.json --junit r.xml --fail-fast
ZAMIN_TESTS=Physics ./build/bin/ZaminTests     # muhit o'zgaruvchisi bilan filtr (eski usul)
```

Yangi test yozish:

```cpp
TEST(MyFeatureWorks)
{
    Ref<Scene> scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("A");
    CHECK(e.HasComponent<TransformComponent>());
    CHECK_NEAR(e.GetTransform().Scale.x, 1.0f, 1e-6f);
}
```

- Testlar GPU'siz: tekstura, mesh, shrift, shader yaratmang. Asosiy kamerasi (`Primary`) bo'lmagan sahna chizilmaydi,
  shuning uchun `OnUpdateRuntime` (`UpdateRuntime` + `RenderRuntime`) headless xavfsiz. Tahrir rejimi kadri
  `OnUpdateEditor` editor kamerasidan chizadi; testlarda uning chizmaydigan yarmi `UpdateEditorAnimation` ishlatiladi.
- `CHECK` xatosi testni yiqitadi, qolgan testlar ishlayveradi; testdan chiqib ketgan istisno ham testni yiqitadi.
- Tasodifiy sonlar uchun belgilangan seed; vaqtga bog'liq tekshiruvlar (tezlanish, chiziqli o'sish) ehtiyot chegarasi
  bilan va coverage build'da (`ZAMIN_COVERAGE_BUILD`) o'tkazib yuboriladi.
- Fayl yozadigan testlar `std::filesystem::temp_directory_path()` ostida o'z papkasini ishlatadi va uni o'chiradi.
- Namuna loyihalarni (`Projects/*`) o'qiydigan testlar bor (`ZAMIN_SOURCE_DIR`): namunalar assetlari yaroqli qolishi kerak.
- Yangi test qo'shilgandan keyin ro'yxatni yangilang: `Tests/test_catalog.py --update docs/Testing.md`.

`ctest` `ZaminTests` ni `--timing --report build/TestResults/ZaminTests.json --junit build/TestResults/ZaminTests.xml`
bilan ishga tushiradi; CI bu fayllarni `unit-tests-<kompilyator>` artefakti qilib yuklaydi.

## 2. Rendering regressiya testi - `ZaminRenderTests`

`Tests/RenderTests.cpp`: etalon sahnalar 480x270 o'lchamda offscreen chiziladi va `Tests/Reference/*.png` bilan
solishtiriladi. Piksel kanali farqi `--threshold` (8) dan katta piksellar `--max-percent` (0.5 %) dan ko'p bo'lsa holat
yiqiladi va `build/RenderTests/` ga yangi rasm, farq rasmi (`<nom>.diff.png`) va `<nom>.compare.png` (etalon | yangi |
farq) yoziladi; har ishga tushishda `report.json` va `summary.md`.

| Holat | Nimani qo'riqlaydi |
|---|---|
| `Lit3D` | PBR primitivlar, quyosh soyasi (kaskadlar), nuqtali va spot yorug'lik, gradient osmon |
| `Lit3D_NoCulling` | frustum culling va instancing o'chiq - `Lit3D` bilan bir xil chiqishi kerak |
| `Materials` | shaffof (saralangan), emissive + bloom, unlit, ikki tomonlama, instanced qator, tuman |
| `Sky` | bir nechta quyosh (faqat biri soya beradi), osmon gradienti egriligi, katta quyosh diski |
| `PostEffects` | SSAO, bloom rangi va linza changi, xromatik aberratsiya, vinyetka, plyonka donasi, FXAA |
| `DebugNormals` | debug ko'rinish: normallar |
| `Sprites2D` | Renderer2D: sprite'lar (aylangan, shaffof, saralangan), doira va halqalar, matn |
| `Particles` | additive doiralar, shaffof quadlar, flipbook, lokal fazo, izlar (GPU instancing) |
| `Particles_NoInstancing` | instancing o'chiq - `Particles` bilan bir xil chiqishi kerak |
| `Showcase3D` | Showcase3D namunasi: modellar, teksturalar, HDRI, skinned mesh |
| `Platformer2D` | Platformer2D namunasi: tilemap, sprite'lar, matn |
| `SelectionOutline` | editor tanlash konturi: skinned model, to'silgan obyekt, ko'p tanlov, hover |

```bash
Tests/render_tests.sh                    # hammasi (llvmpipe, displey bo'lmasa 1280x720x24 xvfb)
Tests/render_tests.sh --filter Sky --save-all --out /tmp/rt
Tests/render_tests.sh --update           # ko'rinish ataylab o'zgarganda etalonlarni qayta yozish
ZAMIN_BUILD_DIR=build-clang Tests/render_tests.sh
```

Skript har doim Mesa'ning dasturiy rasterizatorini (llvmpipe) ishlatadi - etalonlar shu bilan olingan. CMake
target'lari: `render_tests`, `update_render_references`.

## 3. Editor smoke testi - barcha oynalar

`ZAMIN_EDITOR_OPEN_ALL_PANELS=1` (`Editor/src/EditorSmokeTest.cpp`), skript: `Tests/editor_smoke.sh`. Editor namuna
loyihaning nusxasida har bir oynani (alohida va hammasini birga), har bir sozlamalar sahifasini, har bir menyu va ichki
menyuni, viewport popup'larini, so'rov oynalarini, har bir asset turining inspektorini, sahnadagi har bir obyektni va
vaqtinchalik obyektga navbat bilan qo'shilgan har bir komponent inspektorini, sprite muharririni, prefab rejimini,
tayyor joylashuvlarni, Play / Pauza / Stop / Simulate'ni bir necha kadrdan ochadi (Showcase3D - 221 qadam,
Platformer2D - 263 qadam). Logdagi xato (OpenGL debug chiqishi ham, `ZAMIN_GL_DEBUG=1`), OpenGL xato bayrog'i,
bajarilmagan ImGui tekshiruvi (`IM_ASSERT` har bir build'da yoqiq) yoki oxirgi qadamga yetmagan ishga tushish testni
yiqitadi (chiqish kodi 1).

```bash
Tests/editor_smoke.sh                        # Showcase3D va Platformer2D
Tests/editor_smoke.sh Projects/Adventure3D --shots --out /tmp/smoke
```

## 4. Skript hot reload testi

`Tests/hot_reload_test.sh` (`Tests/Fixtures/HotReload`, hook `ZAMIN_EDITOR_HOT_RELOAD_TEST`): editor yo'q modulni o'zi
build qilib 1-versiyani o'ynaydi; test manbani o'zgartirib, modulni editor ishlab turganda tashqarida qayta build
qiladi; editor yangi modulni yuklaydi va 2-versiyani o'ynaydi. Tekshiriladi: sahnadagi maydon qiymatlari saqlanadi
(`Speed=7.5`, `Label`, `Count`), yangi maydon standart qiymatini oladi (`Extra=42`), yangi kod ishlaydi, logda xato
yo'q, sahna fayli o'zgarmagan. Jarayon ichidagi varianti - `ScriptReloadKeepsFieldValues` unit testi.

## 5. Eksport testi va o'yin build'lari

`Tests/export_test.sh` namuna o'yinni editor bilan eksport qiladi (`ZAMIN_EDITOR_EXPORT`, xvfb) va o'yinchi oladigan
holatda tekshiradi: `export-report.json` muvaffaqiyatli; `<O'yin>.sh --version`; eksport qilingan o'yin `--benchmark`
bilan N kadr (chiqish kodi 0, logda xato yo'q, skript moduli yuklandi); headless skrinshot bilan yana bir bor
(`Tests/png_check.py`: 1280x720, bir xil rang emas); boshqa papkaga ko'chirilgandan keyin ham ishga tushadi va
`.desktop` ikonkasi yangi joyni ko'rsatadi.

`Tests/package_games.sh` buni uchala namuna o'yin (Platformer2D, Showcase3D, Adventure3D) uchun bajaradi va faqat
hamma tekshiruvi o'tgan eksportni **o'yin build'i** sifatida paketlaydi: `<Nom>-<versiya>-linux-x86_64.tar.gz` +
`.sha256`, `packages.md`, `summary.md`. Paket yana ochilib, ishga tushirish skripti u yerdan chaqiriladi (tar.gz
fayllarning bajariladigan bitlarini saqlaydi; CI artefaktining zip'i ularni yo'qotardi). CI bu paketlarni
`player-builds-<kompilyator>` artefakti qilib yuklaydi (14 kun saqlanadi).

```bash
Tests/export_test.sh                                   # Platformer2D
Tests/export_test.sh --project Projects/Adventure3D --frames 60 --keep
Tests/package_games.sh --out dist                      # uchala o'yin -> dist/*.tar.gz
```

## 5a. Namuna o'yinlar testi

`Tests/samples_test.py` to'qqizala namunani `ZaminPlayer` da headless ishga tushiradi (displey bo'lmasa xvfb) va
logiga qarab tekshiradi:

- **demo**: oltita M24 o'yini `ZAMIN_DEMO=1` bilan ishga tushadi va ularni bot o'ynaydi. Jurnaldagi `Demo: ...`
  qatorlari natija bo'lishini ko'rsatishi kerak:
  - Neon Maydon: hisob, o'ldirilgan dushmanlar, 2-to'lqin, kombo;
  - Breakout: 1-daraja tugab, 2-daraja boshlanadi (`level6` holati: `ZAMIN_START_LEVEL=6` bilan oxirgi daraja);
  - FPS Arena: parkur aylanasi, nazorat nuqtalari, urilgan nishonlar;
  - Omborchi: kamida 3 ta yechilgan daraja va undo;
  - Ipak Yo'li: kamida 400 m masofa, bot hech narsaga urilmaydi;
  - Vodiy Poygasi: birinchi aylana tugaydi.
- **menus**: bot yo'q, tugmalarni `ZAMIN_PLAYER_INPUT` bosadi. Bosh menyuda Enter ("O'ynash" tanlangan bo'ladi),
  so'ng Esc — pauza, yana Esc — o'yin davom etadi, keyin pauza menyusidan "Bosh menyu". Omborchi va Ipak Yo'li
  holatlarida natija oynasi ham ochiladi. Omborchi'ning `menuwait` holati bosh menyuda 10 s kutadi (orqadagi bot
  keyingi darajaga o'tadi), so'ng "O'ynash" o'yinchining 1-darajasini ochishini va qadam animatsiyasi paytida tez
  bosilgan ikkinchi qadam yo'qolmasligini tekshiradi. O'yinlar har panel uchun jurnalga `Menyu: bosh menyu` / `pauza` /
  `pauza yopildi` / `natijalar` yozadi, test shu qatorlarning tartibini tekshiradi. Platformer2D'da `Menu.zscene` dan
  yuklanish ekrani orqali 1-darajaga o'tiladi.
- **smoke**: uchta eski namuna (Platformer2D, Showcase3D, Adventure3D) ishga tushadi va sahnasi yuklanadi.
- **rekord** (har `demo` dan keyin): demo `ZAMIN_DEMO_RECORDS=1` bilan o'ynaydi, ya'ni botning natijasi ham rekord
  bo'ladi. Jurnalda `Yangi rekord: ...` bo'lishi va o'yinning SaveData fayli (`savedata.json`, ishga tushirishning
  `HOME` ida) rekord kalitlarini saqlashi kerak. So'ng o'sha `HOME` bilan qisqa ikkinchi ishga tushirish (botsiz)
  `Rekordlar: ...` qatorida xuddi shu qiymatni o'qiydi va bosh menyuni ochadi (`--no-records` — o'tkazib yuborish).
- **gamepad**: oltita o'yin faqat simulyatsiya qilingan gamepad bilan (`ZAMIN_PLAYER_INPUT`: `pad=A`, `padhold=` /
  `padrelease=`, `stick=LeftX,0.8`). Bosh menyuda A, o'yinning o'z boshqaruvi (stiklar, tugmalar, triggerlar), Start —
  pauza va qaytish, D-pad pastga + A — bosh menyu. Ishoralar gamepad'ga o'tishi (`Kiritish qurilmasi: gamepad`,
  klaviaturaga qaytmasdan) va o'yinda natija bo'lishi tekshiriladi: Neon Maydon'da RT bilan o'q otilgan, Breakout'da
  g'isht singan, Omborchi'da 1-daraja D-pad bilan yechilgan, Ipak Yo'li'da masofa — rekord bo'lib yozilgan.

Har bir ishga tushirishda yana quyidagilar tekshiriladi:

- chiqish kodi 0;
- logda `ERROR` / `CRITICAL` qatori yo'q;
- skript moduli yuklangan;
- oxirgi kadr rasmi bir xil rangdan iborat emas (`Tests/png_check.py`).

O'yin soati qat'iy: `ZAMIN_CAPTURE_FPS=20` (`--capture-fps`) bilan har kadr aniq 1/20 s
(`Time::SetCaptureFrameRate`), shuning uchun N kadr har kompyuterda bir xil o'yin vaqtiga teng. Botlar
urug'langan tasodifiy sonlar bilan o'ynaydi, demak natija sekin yoki tez kompyuterda bir xil chiqadi. Har ishga
tushirish yangi `HOME` bilan bo'ladi (saqlangan rekord va sozlamalar yo'q). Hisobot `report.json` ga yoziladi,
`--out` papkasida loglar va rasmlar qoladi.

`Tests/samples_assets.py` (displeysiz, ~1 s) oltita M24 o'yinining protsedurali assetlarini tekshiradi:

- `Assets/` hajmi 2 MiB dan oshmaydi;
- har asset `.zmeta` ga ega, UUID'lar takrorlanmaydi;
- PNG va WAV fayllar to'g'ri: 16-bit PCM, jim emas, kesilmagan;
- sahna, loyiha fayli va skriptlar nomlagan har bir fayl mavjud, har ovoz va tekstura biror joyda ishlatiladi.

`--regen` bilan har generator (`Tools/samples/gen_*.py`) vaqtinchalik papkaga (`ZAMIN_SAMPLES_OUT`) ishga tushiriladi
va natija repozitoriydagi fayllar bilan baytma-bayt solishtiriladi. Bu assetlar haqiqatan generatordan chiqqanini
va qayta hosil qilinishini isbotlaydi (numpy kerak).

```bash
Tests/samples_test.py                              # hammasi (~20 daqiqa, llvmpipe)
Tests/samples_test.py --game Racing --kind menus   # bitta o'yinning bitta holati
Tests/samples_test.py --list                       # holatlar va tekshiruvlar
Tests/samples_test.py --out build/SampleTests --frames-scale 2
Tests/samples_assets.py --regen --verbose          # assetlar jadvali + generatorlar bilan solishtirish
Tests/samples_export.py --game Breakout            # eksport qilib, eksportdagi o'yinni o'ynatib tekshirish
ZAMIN_BUILD_DIR=build-release Tests/samples_bench.py   # ishlash tezligi (Release, llvmpipe)
```

`Tests/samples_assets.py` input xaritasini ham tekshiradi: har harakatning klaviatura / sichqoncha va gamepad
bog'lanishi bor, skriptlar o'qiydigan har harakat / o'q / vektor xaritada bor va xaritadagi har biri o'qiladi,
skriptlar klaviatura yoki gamepad'ni to'g'ridan-to'g'ri so'ramaydi, bosh menyudagi boshqaruv ishorasi faqat
xaritadagi nomlarni ishlatadi.

`Tests/samples_export.py` (ctest `SampleExports`) to'qqiz namunani editor bilan eksport qiladi (`Tests/export_test.sh`)
va eksport qilingan o'yinlarni (`<eksport>/<O'yin>.sh`) `samples_test.py --exported` bilan o'ynatadi: olti o'yinning
demo + rekord holatlari va eski namunalarning smoke'i. `Tests/samples_bench.py` har namunaning kadr vaqtini, sahnadagi
obyektlar sonini va eng qimmat qismlarini o'lchaydi ([Samples.md](Samples.md#ishlash-tezligi)).

## 6. Sanitizerlar

`Tests/sanitize.sh <address|undefined|leak|thread>` alohida papkada (`build-asan`, `build-ubsan`, ...) sanitizer bilan
build qiladi (`-DZAMIN_SANITIZE=...`, `cmake/Sanitizers.cmake`) va `ZaminTests` ni ishga tushiradi; testlar o'tsa ham
har qanday sanitizer xabarida yiqiladi. Uchinchi tomon kodining shovqini izohlari bilan `Tests/sanitizers/*.supp` da.

```bash
Tests/sanitize.sh address --smoke --editor-smoke   # ASan + LSan: testlar, 3 o'yin, editor, editor smoke testi
Tests/sanitize.sh undefined --compiler clang       # UBSan (bizning koddagi har xabar fatal: -fno-sanitize-recover=all), Clang
Tests/sanitize.sh leak                             # alohida LeakSanitizer
Tests/sanitize.sh address --filter Prefab --clean  # faqat ba'zi testlar; build papkasi oxirida o'chiriladi
```

`<log papka>/summary.md` har bir ishga tushirishni (natija, vaqt, log) sanaydi. CI'da `sanitizers` job: `asan`
(`--smoke --editor-smoke`) va `ubsan-clang`. Clang bilan sanitizer runtime'lari alohida paket:
`sudo apt-get install libclang-rt-18-dev` (skript u yo'qligini aytadi).

Uchinchi tomon kodining implementatsiyasi bizning target ichida kompilyatsiya qilinsa (masalan miniaudio -
`Zamin/src/Zamin/Audio/MiniaudioImpl.cpp`), u o'z translation unit'ida turadi va sanitizer build'da vendor kutubxona kabi
tiklanadigan (`-fsanitize-recover=all`, `Zamin/CMakeLists.txt`) - shunda uning xabari `ubsan.supp` da izohi bilan
bostiriladi, engine kodining har xabari esa fatal qoladi.

## 7. Benchmarklar regressiya sifatida

| Buyruq | Nima | Chegara |
|---|---|---|
| `ctest --test-dir build -L bench` | `BenchEcsSmoke`, `BenchSerializeSmoke`: kichik o'lchamda, o'qib olingan sahna saqlangani bilan bir xilmi | xato kodi |
| `Tests/bench.sh --compare` | ECS, serializatsiya, prefab benchmarklari; `Tests/Baselines/*-release.json` bilan solishtirish (`Tests/bench_compare.py`) | sezilarli sekinlashish |
| `Tests/render_bench.sh --max-avg 200` | 10 000 sprite, 1 000 mesh (instancing bilan / siz), ikkalasi | ms / kadr |
| `Tests/player_bench.sh --max-avg 50 --max-p95 80` | namuna o'yinlarning boshlang'ich sahnalari ZaminPlayer'da | ms / kadr |

Boshlang'ich natijalar: [Benchmarks.md](Benchmarks.md).

## 8. Kod qamrovi

`Tests/coverage.sh [--gui] [--html]` (`-DZAMIN_COVERAGE=ON`, `build-cov`), hisobot modullar bo'yicha
(`Tests/coverage_report.py`): [Coverage.md](Coverage.md).

## 9. Formatlash - `clang-format`

`.clang-format` mavjud kod uslubini tasvirlaydi (4 bo'shliq, qavslar yangi qatorda, `namespace Zamin {` ichi
chekinish bilan, ustun chegarasi yo'q - qo'lda bo'lingan qatorlar saqlanadi). Kod bazasi butunlay qayta
formatlanmaydi: `Tests/format_check.sh` faqat o'zgargan qatorlarni tekshiradi (`git clang-format`).

```bash
Tests/format_check.sh                     # commit qilinmagan o'zgarishlar (HEAD ga nisbatan)
Tests/format_check.sh --base main         # tarmoq main'dan ajralgandan beri
Tests/format_check.sh --fix --base main   # o'sha qatorlarni joyida formatlash
Tests/format_check.sh --files A.cpp       # butun fayl
Tests/format_check.sh --stats --top 20    # butun kod baza .clang-format dan qanchalik farq qiladi (Tests/format_stats.py)
```

CI'dagi `format` job ogohlantiruvchi (har bo'lakka annotatsiya, farq ish sahifasida) va `.clang-format` ning o'zini
qo'riqlaydi: butun kod bazada o'zgaradigan qatorlar 3.5 % dan oshmasligi kerak (`--stats --max-percent 3.5`).

## 10. Kompilyatsiya tekshiruvlari

- **Ogohlantirishlar:** bizning target'lar (engine, editor, player, testlar va benchmarklar, namuna o'yin modullari)
  `-Wall -Wextra -Wpedantic` bilan (`zamin_target_settings`, `cmake/ZaminGame.cmake`), vendor sarlavhalari `-isystem`
  orqali ulanadi; build GCC va Clang bilan ogohlantirishsiz bo'lishi shart. `-ffp-contract=off`: GCC va Clang build'lari
  bir xil float natija beradi (FMA qisqartirish yo'q — Jolt barcha bog'langan target'larga `-mfma` beradi; Clang avval
  qisqartirardi).
- **Skript shablonlari:** `ZaminScriptTemplateGen` editorning "Yangi skript" shablonlarini yaratadi va ular engine bilan
  birga kompilyatsiya qilinadi (`ZaminScriptTemplateCheck`) - shablon buzilsa build yiqiladi.
- **Shipping DebugDraw:** `Tests/DebugDrawShippingCheck.cpp` DebugDraw API'sini `ZM_DEBUG_DRAW 0` bilan kompilyatsiya
  qiladi (Shipping build'dagi bo'sh funksiyalar) va `ZaminTests` uni chaqiradi.
- **Namuna o'yinlar** (`Projects/*`) asosiy build'ning bir qismi: engine API'si o'zgarsa ular ham kompilyatsiya qilinadi.
- **Hujjatlar** (`Tests/docs_check.py`, ctest: `DocsCheck`): Markdown fayllardagi har bir nisbiy havola va rasm
  mavjud (`#bo'lim` havolalari — GitHub sarlavha langarlari bilan), `AllComponents` dagi har bir komponent
  [Components.md](Components.md) da hujjatlangan va uning manbadan yaratilgan jadvallari yangi
  (`Tests/docs_components.py`), `ScriptableEntity` ning har bir ommaviy funksiyasi [Scripting.md](Scripting.md) da
  tilga olingan, player `--help` dagi har bir variant [Runtime.md](Runtime.md) yoki
  [BuildAndExport.md](BuildAndExport.md) da bor, [FAQ.md](FAQ.md) da keltirilgan har bir log / UI xabari (`` > `...` ``
  ko'rinishidagi qator; `<...>` - formatlanadigan qiymat) manba kodda aynan shunday yozilgan, har bir `ZAMIN_*` CMake
  varianti [BuildAndExport.md](BuildAndExport.md) da hujjatlangan va `Doxyfile` dagi `PROJECT_NUMBER` dvigatel
  versiyasiga teng. Shuningdek: shu hujjat oxiridagi unit testlar katalogi yangi (`Tests/test_catalog.py`),
  [CHANGELOG.md](../CHANGELOG.md) git tarixiga mos — tarixdagi va vazifasi bajarilgan har bir milestone'ning bo'limi
  bor, bo'lim ostidagi qator (sana, vazifalar, commit'lar soni va oralig'i) `Tests/changelog.py` chiqargani bilan bir
  xil, tilga olingan har bir commit mavjud —, [hujjatlar indeksi](README.md) `docs/` dagi har bir hujjatga va
  `CONTRIBUTING.md`, `CHANGELOG.md` ga havola beradi, `docs/images/` dagi har bir rasm biror hujjatda ishlatilgan,
  400 KB dan va 1280 px kenglikdan oshmaydi.
- **Hujjatlardagi kod** (`Tests/docs_snippets.py`, ctest: `DocsSnippets`): `README.md` va `docs/*.md` dagi butun skript
  klasslari (`class X : public ScriptableEntity` ... `};` bo'lgan ```` ```cpp ```` bloklari) namuna o'yinlar skriptlari
  bilan bir xil kompilyator va bayroqlar bilan (`build/compile_commands.json`) `-fsyntax-only` kompilyatsiya qilinadi;
  ogohlantirish ham xato hisoblanadi, xabar Markdown fayl va qatoriga ko'rsatiladi. Kod bo'laklari (butun klass
  emas) tekshirilmaydi.

## ctest

| Test | Label | Nima |
|---|---|---|
| `ZaminTests` | - | unit testlar (vaqt hisoboti, JSON va JUnit natijalari `build/TestResults` da) |
| `DocsCheck` | `docs` | `Tests/docs_check.py` - hujjatlar kod bilan mosligi (10-bo'lim) |
| `DocsSnippets` | `docs` | `Tests/docs_snippets.py` - hujjatlardagi skript klasslari kompilyatsiya bo'ladi |
| `BenchEcsSmoke`, `BenchSerializeSmoke` | `bench` | benchmarklar kichik o'lchamda |
| `EditorSmoke` | `gui;slow` | `Tests/editor_smoke.sh` |
| `ScriptHotReload` | `gui;slow` | `Tests/hot_reload_test.sh` |
| `ExportTest` | `gui;slow` | `Tests/export_test.sh` (Platformer2D) |
| `SampleAssets` | `samples` | `Tests/samples_assets.py` - namuna o'yinlarning protsedurali assetlari |
| `Sample<O'yin>` (9 ta: `SampleTopDownShooter` ... `SampleAdventure3D`) | `samples;gui;slow` | `Tests/samples_test.py --game <O'yin>` (loglar va rasmlar `build/SampleTests/<O'yin>`) |
| `SampleExports` | `samples;gui;slow` | `Tests/samples_export.py` - har namuna eksport qilinadi va eksportdagi o'yin headless o'ynatiladi (`build/SampleExports`) |

`ctest --test-dir build -LE gui` - tezkorlari (CI'dagi "Unit tests" qadami), `ctest --test-dir build -L gui` - GUI
testlari (ketma-ket, displey yoki xvfb kerak), `ctest --test-dir build -L samples` - namuna o'yinlar (yoki
`cmake --build build --target samples_test`).

## CI (`.github/workflows/linux.yml`)

| Job / qadam | Skript | Artefakt |
|---|---|---|
| `build` (gcc, clang; Release): Unit tests | `ctest -LE gui` | `unit-tests-<cc>` |
| `build`: Rendering regressiya testi | `Tests/render_tests.sh --save-all` | `render-tests-<cc>` |
| `build`: Editor smoke testi | `Tests/editor_smoke.sh --shots` | `editor-smoke-<cc>` |
| `build`: Skript hot reload testi | `Tests/hot_reload_test.sh` | - |
| `build`: Eksport testi + o'yin build'lari | `Tests/package_games.sh --out player-builds` | `export-test-<cc>`, `player-builds-<cc>` |
| `build`: headless skrinshotlar | `ZaminPlayer` / `ZaminEditor` (`ZAMIN_MAX_FRAMES`, `ZAMIN_SCREENSHOT`) | `screenshots-<cc>` |
| `sanitizers` (`asan`, `ubsan-clang`) | `Tests/sanitize.sh address --smoke --editor-smoke`, `Tests/sanitize.sh undefined --compiler clang` | `sanitizer-<nom>` |
| `format` | `Tests/format_check.sh --base <oldingi commit> --github`, `--stats --max-percent 3.5` | - |
| `coverage` | `Tests/coverage.sh --html` | `coverage-report` |

## Topilgan xatolarni qo'riqlovchi testlar

Har biri bir marta topilgan (ko'pincha shu milestone'dagi vositalar bilan) va tuzatilgan xatoni qaytib kelishdan
saqlaydi: tuzatish olib tashlansa test yiqiladi.

| Test | Xato | Qanday topilgan |
|---|---|---|
| `AudioMixerGroupTeardown` | bir nechta bola guruhli mixer guruhi o'chirilganda miniaudio faqat birinchi kiruvchi tugunni ajratardi, qolganlari o'chirilgan xotirani o'qirdi (`AudioEngine::Shutdown` da yiqilish) | AddressSanitizer |
| `PhysicsDeterminism2D`, `PhysicsDeterminism3D` | Box2D / Jolt jismlari komponent hovuzi tartibida yaratilardi: editorda o'ynalgan sahna va fayldan yuklangan (eksport qilingan) xuddi shu sahna boshqacha simulyatsiya qilinardi | determinizm testi (bit darajasida taqqoslash) |
| `PlayCopyRunsScriptsInFileOrder`, `PhysicsDeterminism2D/3D` (o'yindagidek, nusxasiz) | `Scene::Copy` komponent hovuzlarini view orqali (eng yangisidan) ko'chirardi: editorda Play bosilganda skriptlar (`OnCreate` ... `OnLateUpdate`), animator va agentlar eksport qilingan o'yindagiga teskari tartibda ishlardi | review (testlar fayldan o'qilgan sahnani ham nusxa orqali o'ynardi) |
| `DeferredDestroyOnceEachAndLinear` | `DestroyEntityDeferred` takrorni butun navbatdan qidirardi: bir kadrda minglab `Destroy` kvadratik (10 000 ta - 525 ms) | ECS benchmark |
| `ScriptReloadKeepsFieldValues` | hot reload'dan keyin sahnadagi skript maydonlari qiymatlari saqlanishi | hot reload testi |
| `StaleModuleCopiesRemoved` | yiqilgan / to'xtatilgan jarayonlar skript moduli nusxalari `/tmp/zamin-modules` da to'planardi (423 MB) | hot reload testi |
| `ParticleSimulationUnchanged` | GCC va Clang bir emitter uchun tasodifiy sonlarni boshqa tartibda olardi (bitta ifodadagi bir nechta chaqiriq) | Clang Release build |
| `Kinematic3DFollowsTransformAtLowFPS` | kadr boshiga bir nechta fizika qadami bo'lganda kinematik jism tezligi noto'g'ri | regressiya |
| `DestroyKeepsOtherScriptsAlive` | EnTT o'chirilgan komponent o'rniga oxirgisini ko'chiradi - boshqa obyektning skripti yo'qolardi | regressiya |
| `DestroyEntityReentrant` | `OnDestroy` ichidan o'chirish eskirgan ko'rsatkich bilan ishlardi | regressiya |
| `EntitySetPartialLoad`, `SceneSalvage` | bitta buzilgan yozuv butun sahnani yo'qotardi; yozish o'rtasida uzilgan fayl | buzilgan fayllar |
| `SerializationFuzz` | tasodifiy / buzilgan JSON (cheksiz float'lar - `Tests/Fixtures/Fuzz`) yuklovchini yiqitmasligi | fuzz test |
| `SceneSaveDeterministic`, `SceneCopyIsIndependent` | sahna fayli har saqlashda bir xil; Play nusxasi asl sahnaga ta'sir qilmaydi | regressiya |
| `MemoryStatsProcess` | Clang sinovdagi ajratilgan xotirani optimallashtirib yuborardi (volatile yozuv) | Clang Release build |
| `CrashHandlerReports` | backtrace 0-kadrni taxmin qilardi - sanitizer build'da noto'g'ri | sanitizer build |

GUI testlari ham topgan xatolar: editor tashqarida build qilinayotgan modulni linker hali yozayotganda yuklardi (endi
vaqti va hajmi bir so'rov davomida o'zgarmaguncha kutadi - hot reload testi); terminal logi quvurga to'liq
buferlanardi va yiqilishda yo'qolardi (endi qator bo'yicha); editor smoke testi Inspektor tab'ini ko'rmasdi (kod
qamrovi topdi).

## Ilova: barcha unit testlar

Bo'lim nomi - `Tests/ZaminTests.cpp` dagi bo'lim sarlavhasi (kod izohi, ingliz tilida).

<!-- test-catalog:begin -->
_`Tests/test_catalog.py --update docs/Testing.md` yaratgan: 634 ta unit test, 93 ta bo'lim (`Tests/ZaminTests.cpp` dagi tartibda). Bitta testni ishga tushirish: `./build/bin/ZaminTests <Nom>`._

| # | Bo'lim | Testlar | Nomlari |
|---|---|---|---|
| 1 | the framework itself (TestFramework.h) | 3 | `TestFrameworkPatterns`, `TestFrameworkOptions`, `TestFrameworkReports` |
| 2 | Asosiy (sahna, skriptlar, fizika, prefab, particle) | 25 | `UUIDsAreUnique`, `UUIDTextAndHashing`, `EntityLifecycle`, `SerializerRoundTrip`, `SceneCopyIsIndependent`, `ScriptLifecycleAndFields`, `PhysicsGravityAndCollision`, `InstantiateAndDeferredDestroy`, `DecomposeTransform`, `ProjectTemplate`, `HierarchyWorldTransforms`, `PrefabsAndClipboard`, `SpriteAnimationFrames`, `Physics3D`, `Raycast2DAndSceneRequests`, `Kinematic3DFollowsTransformAtLowFPS`, `TilemapCollisionAndResize`, `ParticlesSimulate`, `ParticleSimulationUnchanged`, `ParticleGradientStops`, `ParticleCurvesAndSize`, `ParticleVelocityOverLifetime`, `ParticleTextureSheetFrames`, `ParticleConeAndEdgeShapes`, `ParticleModulesSerialization` |
| 3 | M14 Particle II: mesh shape, sub-emitters, collisions, noise, trails | 29 | `ParticleMeshEmitterSampling`, `ParticleSubEmitters`, `ParticleCollisionPlane`, `ParticleCollisionWorld`, `ParticleNoise`, `ParticleTrails`, `ParticleModulesSerializationII`, `ParticleBurstsList`, `ParticleDurationDelayAndSpeed`, `ParticlePrewarm`, `ParticleSortOrder`, `ParticleSimulationSpaces`, `ParticleStopActions`, `ParticleStopActionWaitsForSubEmitters`, `ParticleStartBurstWaitsForDelay`, `ParticlePlayOnStart`, `ParticleMainSerialization`, `ParticleModuleSwitchesAndPlayback`, `ParticleNearFadeAndStretchedBounds`, `ParticleFollowCameraWeather`, `ParticleBuiltinPresets`, `ParticlePresetFiles`, `ParticleSerializationEverything`, `ParticleSampleScenesRoundTrip`, `ParticlePerformance`, `SkeletalPoseEvaluation`, `CharacterController3D`, `AnimatorStateMachine`, `IrradianceSH` |
| 4 | EntityRef fields | 2 | `EntityRefSerialization`, `EntityRefRemapOnCopy` |
| 5 | timers (Invoke) | 1 | `InvokeTimers` |
| 6 | tweens / easing | 2 | `EasingFunctions`, `TweensRunAndChain` |
| 7 | input action map | 13 | `InputActionMapDefaults`, `InputBindingNames`, `InputActionMapSerialization`, `ProjectInputMapRoundTrip`, `InputKeyAndMouseFrames`, `InputBindingHints`, `InputActiveDeviceAndTextHints`, `InputSimulatedKeys`, `InputActionQueries`, `InputAxes`, `SaveDataValuesAndFile`, `SaveDataSubmitRecord`, `SaveDataProjectLocation` |
| 8 | M01: Random | 3 | `RandomDeterminism`, `RandomRanges`, `RandomGeometry` |
| 9 | M01: tags | 2 | `TagsQueries`, `ProjectTagList` |
| 10 | M01: messages / events | 3 | `ScriptMessages`, `EventBusBasics`, `EventBusInScene` |
| 11 | M01: hierarchy queries | 1 | `HierarchyQueries` |
| 12 | M01: name cache | 1 | `FindEntityNameCache` |
| 13 | script lifecycle (OnEnable / OnStart / OnDisable) | 3 | `ScriptLifecycleOrder`, `ScriptLifecycleEdgeCases`, `DestroyKeepsOtherScriptsAlive` |
| 14 | triggers (OnTriggerBegin / OnTriggerEnd) | 3 | `TriggerCallbacks2D`, `TriggerEndWhenDestroyed`, `TriggerCallbacks3D` |
| 15 | script field attributes (enum, color, range, tooltip) | 1 | `ScriptFieldEnumColorRange` |
| 16 | new-script templates | 1 | `ScriptTemplates` |
| 17 | script component removed / replaced at runtime | 2 | `ScriptComponentRemovedAtRuntime`, `DestroyEntityReentrant` |
| 18 | M02: editor UX (hierarchy order, multi-selection) | 7 | `HierarchySiblingOrder`, `SceneOrderStableOnDestroy`, `HierarchyOrderSerialization`, `MultiEntityDuplicateDestroy`, `EntitySelectionModel`, `GroupTransformEditing`, `TransformClipboardText` |
| 19 | M02: viewport (snap, grid, camera) | 8 | `EditorSnapSettings`, `EditorViewSettingsJson`, `EditorCameraBookmarkTransition`, `EditorCameraFrameBounds`, `NameLookupFollowsSceneOrder`, `DestroyKeepsSceneOrder`, `SceneBoundsForFraming`, `GizmoDuplicateWhileDragging` |
| 20 | M02: inspector / hierarchy (31-35) | 5 | `ComponentKeysMatchSceneFiles`, `InspectorComponentOrder`, `ComponentClipboardCopyPaste`, `EditorHiddenAndLockedFlags`, `InspectorFoldStateJson` |
| 21 | content browser / assets | 7 | `AssetTypesAndFilter`, `AssetListingAndSorting`, `AssetJsonReferenceRewrite`, `AssetMoveUpdatesReferences`, `AssetMoveKeepsNamesAndTags`, `AssetMoveUpdatesParticlePresets`, `AssetImportFromOutside` |
| 22 | asset registry, .zmeta, file watcher | 20 | `AssetTypeDetection`, `AssetHandleText`, `AssetMetaFileRoundTrip`, `AssetRegistryScanAndLookup`, `AssetRegistryFollowsFileChanges`, `AssetHotReloadAppliesChanges`, `ProjectAssetRegistry`, `AssetRegistrySnapshots`, `ProjectAssetRegistryCache`, `FileChangeQueueDebounce`, `FileWatcherSeesChanges`, `TextureImportSettingsInMeta`, `ModelImportSettingsInMeta`, `ModelImportTransformSkinning`, `ImportSettingsOfProjectAssets`, `AssetReferencesFollowUUIDs`, `AssetMovesOutsideEditorAreDetected`, `AssetSearchByPath`, `ContentBrowserSettingsJson`, `SelectionSummaryForStatusBar` |
| 23 | active / inactive entities (M03) | 11 | `ActiveFlagAndHierarchy`, `InactiveEntitiesAreNotDrawn`, `InactiveEntitiesLeavePhysics2D`, `InactiveEntitiesEndContacts`, `InactiveEntitiesLeavePhysics3D`, `InactiveEntityScripts`, `ScriptsSwitchingEntitiesOff`, `ScriptActiveApi`, `ActiveOnEnableOnDisable`, `ActiveStateSerialization`, `SelectionSummaryInactive` |
| 24 | M03: what else an inactive entity stops | 37 | `InactiveParticlesStop`, `InactiveAudioSourcesStop`, `AudioMixerGroupTeardown`, `AudioMixerGroupMath`, `AudioMixerGroupsDeviceless`, `AudioMusicCrossfade`, `AudioListenerSelection`, `ProjectAudioSettings`, `AudioRandomVariation`, `AudioVoiceLimit`, `AudioSourceScriptControl`, `AudioDoppler`, `AudioFilterMath`, `AudioFiltersDeviceless`, `AudioFades`, `AudioClipImportSettings`, `AudioPlayerSettingsSaveLoad`, `AudioProfiler`, `AudioWaveformViewAndLoopRegion`, `AudioDirectionalCone`, `AudioAmbientZones`, `AudioOnSoundFinished`, `InactiveAnimatorsPause`, `InactiveUIButtons`, `InactiveCamerasDoNotCount`, `InactiveLightsDoNotCount`, `SpotLightComponentRoundTrip`, `SpotLightCone`, `FxaaSceneSetting`, `LocalLightSelection`, `InactiveTilemapCollision`, `InactiveCharacterControllers`, `CharacterContactsSwitchedOffAndOnInOneFrame`, `ContactsOfBodiesReplacedDuringDispatch`, `PrefabInstancesKeepActiveState`, `InstantiateFromTransformedInactiveGroup`, `ActivityHierarchyPhysicsScripts` |
| 25 | asset inspector / references | 7 | `AssetInfoImages`, `AssetInfoAudio`, `AssetInfoModels`, `AssetReferenceIndexScans`, `AssetImportLogReportsOnce`, `AssetDeletionWarnings`, `AssetThumbnailsAndDiskCache` |
| 26 | materials (.zmat) | 10 | `MaterialFormatRoundtrip`, `MaterialAssetLibraryAndReload`, `MeshRendererMaterialAssets`, `TransparentSortBackToFront`, `MaterialSurfaceStates`, `PrimitiveWindingFacesOutward`, `GLTFMaterialModes`, `InlineMaterialModesRoundtrip`, `MaterialInstancesCopyOnWrite`, `GLTFMaterialExport` |
| 27 | M05: tasks 096-100 | 22 | `TransparentSortPriority`, `MaterialEmission`, `MaterialSerializationFields`, `MaterialSerializationRobust`, `MaterialSceneSerialization`, `MaterialPresetLibrary`, `MaterialThumbnailStamp`, `RenderScaleResolution`, `RenderScaleSceneSetting`, `AutoExposureMath`, `AutoExposureSceneSetting`, `FrustumCullingVolume`, `CullingBounds`, `EditorDebugViews`, `Culling2DBounds`, `MeshInstancingBatches`, `ShadowQualitySettings`, `ShadowSettingsSceneSetting`, `RenderStatsSummary`, `MultipleDirectionalLights`, `SkySettingsSceneSetting`, `LensEffectsSceneSetting` |
| 28 | debug drawing (M07) | 19 | `DebugDrawShapes`, `DebugDrawDurations`, `DebugDrawFollowsGameTime`, `DebugDrawRaycasts`, `DebugDrawContacts`, `DebugDrawShippingIsEmpty`, `DebugDrawSettingsSaved`, `DebugDrawVelocities`, `DebugDrawNavigationPath`, `FrustumCornersCamera`, `EditorGizmoSettingsSaved`, `TransformGizmoSettingsSaved`, `SelectionOutlineSettingsSaved`, `EditorBoundsAndSkeletonGizmosSaved`, `SelectionOutlineLookup`, `SelectionOutlineCoverage`, `SkeletonJointTransforms`, `EntityLocalBounds`, `ImageCompare` |
| 29 | M07: profiler tree, overlay, normals, queue | 7 | `ProfilerHierarchy`, `ProfilerScopedTimersAndAverages`, `FrameTimeHistoryStats`, `StatsOverlaySettingsSaved`, `VertexNormalLines`, `DebugDrawPausedFramesDoNotPileUp`, `DebugDrawQueueLimitsAndShortDurations` |
| 30 | M08: 2D (sprite sheets, 9-slice, sorting layers, parallax) | 13 | `SpriteSheetSliceGrid`, `SpriteFrameUVAndRegions`, `SpriteSheetStorageAndSequences`, `SpriteSheetFromAssetRegistry`, `SpriteGeometryNineSlice`, `SpriteGeometryTiled`, `SpriteRendererRectAndBounds`, `SortingLayerListEdits`, `SortKey2DOrder`, `SortingLayersInProjectFile`, `Scene2DFieldsSerialized`, `ParallaxOffsetAndRepeat`, `ParallaxCopiedWithScene` |
| 31 | M08: 2D camera follow and shake | 5 | `Camera2DFollowMath`, `Camera2DFollowStep`, `Camera2DFollowInScene`, `CameraShakeTrauma`, `CameraShakeInScene` |
| 32 | M08: tilemap layers, auto-tiles, tools | 16 | `TilemapLayersEdit`, `TilemapLayersSerializedAndSolid`, `AutoTileMasks`, `AutoTilePaintEdges4`, `AutoTilePaintBlob8AndFallback`, `AutoTileSetStorage`, `TilemapFloodFillRectAndLine`, `TilemapSampleAutoTilesConsistent`, `TilesetPropertiesStorage`, `TilemapCollisionBoxes`, `OneWayPlatformDecision`, `OneWayBoxColliderInScene`, `TilemapTilePropertiesInScene`, `TileBrushStamp`, `SpriteFlashAndOutline`, `SpriteOutlineDrawModes` |
| 33 | M08: tint animation, 2D lights, pixel perfect, Y-sort | 8 | `TintGradientAndPhases`, `TintAnimationInScene`, `TintAnimationOnSpawnedEntities`, `Light2DMathAndGathering`, `PixelPerfectCamera`, `PixelPerfectCameraOddSizes`, `YSortedSortingLayers`, `TopDown2DSampleScene` |
| 34 | 2D joints (M09) | 9 | `DistanceJoint2DRodRopeAndWorldAnchor`, `DistanceJoint2DSpring`, `RevoluteJoint2DLimitAndMotor`, `PrismaticJoint2DAxisLimitAndMotor`, `WeldJoint2DRigidAndSoft`, `WheelJoint2DCarDrives`, `Joint2DBreakForceAndScriptCallback`, `Joint2DFollowsBodiesInAndOut`, `Joint2DSerializeAndRemap` |
| 35 | 2D colliders, materials, layers, platforms (M09) | 8 | `Polygon2DDecomposition`, `Polygon2DTraceAlphaOutline`, `PolygonCollider2DInScene`, `EdgeCollider2DInScene`, `PhysicsMaterialCombineAndJson`, `PhysicsMaterialAssetOnColliders`, `CollisionLayers2DMatrixAndFiltering`, `PlatformEffector2DInScene` |
| 36 | M09: effectors, queries, filtering (171-175) | 6 | `Polygon2DClipping`, `AreaEffector2DPushesBodies`, `BuoyancyEffector2DFloats`, `Overlap2DQueries`, `Raycast2DAllOrdering`, `CollisionFiltering2D` |
| 37 | 2D physics settings, sleeping, joint prefabs, ragdoll (M09) | 6 | `Physics2DStepSettingsAndSubSteps`, `Physics2DSleeping`, `Physics2DScriptForcesAcrossSteps`, `Physics2DOwnSleepTimingWakeAndSwitch`, `Joint2DPrefabFileAndSpawn`, `Ragdoll2DSampleScene` |
| 38 | 3D joints and convex hulls (M10) | 14 | `FixedJoint3DHoldsAndWelds`, `HingeJoint3DPendulumLimitsMotorSpring`, `HingeJoint3DSlowSpringDoesNotSleepShort`, `Joint3DStartAsleepBodiesStayAsleep`, `DistanceJoint3DRodRopeAndSpring`, `SliderJoint3DAxisLimitsAndMotor`, `Joint3DBreakForceAndScriptCallback`, `Joint3DFollowsBodiesInAndOut`, `Joint3DCollideConnected`, `ConeJoint3DSwingTwistAndFriction`, `Joint3DSerializeAndRemap`, `ConvexHullBuildAndCache`, `Joints3DSampleScene`, `Ragdoll3DSampleScene` |
| 39 | M10: mesh colliders, layers, 3D queries | 6 | `MeshCollider3DStaticAndCached`, `CollisionLayers3DMatrixAndFiltering`, `Overlap3DQueries`, `ShapeCast3DQueries`, `Raycast3DAllOrdering`, `Queries3DSampleScene` |
| 40 | 3D physics: CCD, sleeping, materials, triggers, pushing (M10) | 7 | `ContinuousCollision3DStopsFastBodies`, `Physics3DSleepingAndWakeUp`, `PhysicsMaterial3DOnColliders`, `Triggers3DMixedCollidersAndCharacters`, `CharacterController3DPushesBodies`, `Triggers3DContactsOfSleepingBodies`, `Physics3DSampleScene` |
| 41 | 3D physics II: debug contacts, joint debug, joint prefabs, script queries (M10) | 6 | `Physics3DDebugContactData`, `DebugDrawJoints`, `Joint3DPrefabFileAndSpawn`, `ScriptQueries3D`, `PhysicsDebug3DSampleScene`, `AudioSampleScene` |
| 42 | M12: UI widgets (image, slider, toggle, progress bar, input field) | 11 | `UIImageFillGeometry`, `UISliderValueMapping`, `UISliderDragging`, `UIToggleClicksAndGroups`, `UIImageBlocksTheMouse`, `UIProgressBarFollowsValue`, `TextEditUTF8`, `UIInputFieldEditing`, `UIWidgetSerialization`, `TextInputEventStream`, `UIWidgetsSampleScene` |
| 43 | M12: UI layout, canvas, layout groups, events, navigation (226-230, 236) | 11 | `UIRectLayoutMath`, `UICanvasScaleMath`, `UIHierarchyLayout`, `UICanvasScalesTheUI`, `UILayoutGroupMath`, `UILayoutGroupsInTheScene`, `UIPointerEventsReachScripts`, `UIKeyboardAndGamepadNavigation`, `UINavigationIntoInputFields`, `UILayoutSerialization`, `UILayoutSampleScene` |
| 44 | M12: text layout, font atlases | 5 | `TextLayoutAlignment`, `TextLayoutWordWrap`, `TextLayoutOverflowModes`, `TextComponentAlignmentSerialization`, `FontAtlasSizeLadder` |
| 45 | M12: UI animations | 5 | `UIAnimationMath`, `UILookScalesAndFadesWithChildren`, `UITweensRunOnUITime`, `UIAnimationShowHideAndFeedback`, `UIAnimationSerialization` |
| 46 | M12: editor UI picking (rect) and its coordinates | 1 | `UIRectPickingForTheEditor` |
| 47 | M12: masks and scroll views | 7 | `UIScrollMath`, `UIMaskClipsDrawingAndPicking`, `UIScrollViewLayoutAndWheel`, `UIScrollViewDragging`, `UIScrollViewScrollbarAndNavigation`, `UIScrollViewWithAViewport`, `UIScrollViewCopiesAndClones` |
| 48 | M12: game time (pause menus), display options | 6 | `GameTimeScaleAndPause`, `DisplaySettingsAreSaved`, `PauseMenuSampleScene`, `UIEventOrderInScrollViews`, `UIScrollViewDragOfItsBackgroundIsNoClick`, `UIScrollViewHorizontalInACanvas` |
| 49 | property animation (M13) | 9 | `PropertyClipInterpolation`, `PropertyClipFileRoundtrip`, `AnimationEventsFireOnce`, `AnimationPlayerPlaysClips`, `RootMotionExtraction`, `BlendTree1DWeights`, `AnimatorBlendTreeSyncsClips`, `AnimatorLayersMasks`, `SpriteClipAssetsPlayWithEvents` |
| 50 | M13: clip trimming / sub-clips (254) | 2 | `AnimationClipTrimming`, `ModelClipSettingsSplitClips` |
| 51 | M13: glTF morph targets (255) | 2 | `MorphTargetsApplyWeights`, `MorphWeightsFromClipsAndScripts` |
| 52 | M13: skeletal scrub preview (251) | 1 | `AnimatorEditorPreviewScrub` |
| 53 | M13: bone attachments (252) | 1 | `BoneAttachmentFollowsJoint` |
| 54 | M13: footsteps from animation events (253) | 1 | `FootstepsFromAnimationEvents` |
| 55 | M13: multiple skins, pose cache, culling | 4 | `MultipleSkinsInOneGLTF`, `AnimationPoseCache`, `SkinJointInfluencesFitPalette`, `AnimatorSkipsUnchangedAndCulledPoses` |
| 56 | M13: animation tests (258) - every property a clip animates, events through the scene | 3 | `PropertyClipAnimatesEveryProperty`, `AnimationEventsExactlyOnceInScene`, `AnimationPlayerPlayFromTurnsRound` |
| 57 | M15: scene management | 6 | `SceneManagerAdditiveScenes`, `SceneManagerDontDestroyOnLoad`, `SceneManagerAsyncLoad`, `SceneManagerStartFromBackgroundLoad`, `SceneManagerFadeTransition`, `BuildSceneList` |
| 58 | M15: scene events, build index, queries, load errors, stable files | 15 | `SceneEventsOrder`, `LoadSceneByBuildIndex`, `SceneQueryIndices`, `SceneLoadErrors`, `SceneSaveDeterministic`, `SceneVersionMigration`, `SceneMetadataAndThumbnail`, `ServicesRegistry`, `GlobalGameState`, `SceneLoadTimingBreakdown`, `SceneBinaryCache`, `LargeSceneSerialization`, `SubScenes`, `SubSceneFileMoveUpdatesReference`, `ReadTextFileFolder` |
| 59 | M15: scene management end to end | 1 | `SceneManagementFlow` |
| 60 | M16 Editor II | 15 | `ConsoleFilterAndCollapse`, `GameViewResolutionLayout`, `EditorPreferencesSettings`, `ShortcutRegistryLookup`, `RecentFilesMRU`, `EditorAutosaveRecovery`, `UndoHistoryNamingAndJumps`, `EditorLayoutLibraryFiles`, `SceneViewAxesAndCamera`, `EntityLabelsLayoutAndFade`, `SceneStatisticsCounting`, `ExternalEditorCommands`, `CompilerOutputParsing`, `IdleThrottleDecision`, `EditorIIPureLogic` |
| 61 | M17: project settings (quality, input, layers) | 21 | `QualityLevelLimits`, `QualitySettingsJson`, `QualityRuntimeLevel`, `ProjectQualityAndSections`, `InputVectorsComposite`, `InputBindingConflicts`, `CollisionLayerPresets`, `SceneFileTagReferences`, `ProjectSettingsIndexSearch`, `SemanticVersionParseCompare`, `PlayerSettingsSerialization`, `PlayerCommandLineParser`, `PlayerLaunchSettingsPrecedence`, `PlayerSettingsFullscreenToggleAndSplash`, `FramePacingPlanAndPacer`, `SplashTimelineAndLogo`, `InputAnyKeyAndMonitorPlacement`, `PlayerCompanySaveDataAndVersion`, `GameIconImagesAndDesktopEntry`, `ProjectFileMigrationAndUnknownKeys`, `BuildSceneChecksAndStartScene` |
| 62 | M17: graphics context, time, default gravity | 7 | `GraphicsContextSettings`, `TimeSettingsAndClock`, `TimeCaptureFrameRate`, `TimeApiTimeScaleAndUnscaledTimers`, `ProjectDescriptionThumbnailAndSummary`, `ProjectDefaultGravityAndStep`, `ProjectSettingsFullRoundTrip` |
| 63 | M17: audio defaults, localization, editor language | 7 | `AudioSpatialDefaults`, `LocalizationStringFiles`, `LocalizationLookupFormatPlural`, `LocalizationTableEditingAndFiles`, `LocalizedTextComponentAndScripts`, `ProjectLocalizationSettings`, `EditorLanguageStrings` |
| 64 | M18: the player's log file and crash reports | 2 | `PlayerLogFileRotation`, `CrashHandlerReports` |
| 65 | M18: the cursor API for scripts | 1 | `CursorApiForScripts` |
| 66 | M18: window size changes, a fixed aspect ratio (letterbox) | 1 | `PlayerViewportFixedAspect` |
| 67 | M18: the export leaves out unused assets | 11 | `ExportExcludesUnusedAssets`, `ExportReleaseModuleAndStrip`, `ExportDesktopEntryRelocatableAndIcons`, `ExportReportSizesAndFiles`, `PauseInBackgroundAudioOutput`, `DisplayGammaAndBrightness`, `PlayerBenchmarkStatsAndLimits`, `BuildInfoAndVersionText`, `ExportFolderSelfCheck`, `DisplayResolutionSettings`, `GameMenuSampleScene` |
| 68 | M19: noise | 2 | `NoiseRangeContinuityDeterminism`, `NoiseFractalsAndTiling` |
| 69 | M19: AnimationCurve | 2 | `AnimationCurveEvaluateAndWrap`, `AnimationCurveScriptField` |
| 70 | M19: splines | 5 | `SplineEvaluation`, `SplineArcLengthAndClosest`, `SplineComponentsSerializeAndFollow`, `SplineFollowerRuntimeStateClearedOnCopy`, `SplineFollowerMovesKinematicBodies` |
| 71 | M19: colours | 2 | `ColorConversions`, `SplineSampleScenes` |
| 72 | M19: math utilities (366) | 4 | `MathUtilsRanges`, `MathUtilsWrapping`, `MathUtilsMoveTowards`, `MathUtilsSmoothDamp` |
| 73 | M19: quaternion helpers (367) | 3 | `QuaternionEulerConvention`, `QuaternionLookAndFromTo`, `QuaternionSlerpRotateTowards` |
| 74 | M19: job system (368) | 4 | `JobSystemInlineWithoutWorkers`, `JobSystemParallelForCorrectness`, `JobSystemDependencies`, `JobSystemSpeedupAndStats` |
| 75 | M19: parallel particles and animators (369) | 2 | `JobSystemParticlesMatchSerial`, `JobSystemAnimatorsMatchSerial` |
| 76 | M19: memory statistics (370) | 3 | `MemoryStatsCategories`, `MemoryStatsProcess`, `MemoryStatsScenesAndPoseCaches` |
| 77 | M19: StringUtils | 2 | `StringUtilsBasics`, `StringUtilsUtf8` |
| 78 | M19: Stopwatch, ScopedLogTimer, Timer | 1 | `StopwatchAndTimers` |
| 79 | M19: log levels per logger, the log file's level, --log-level | 1 | `LogLevelsPerLogger` |
| 80 | M19: asserts with messages | 9 | `AssertsWithMessages`, `FileSystemLexicalPaths`, `FileSystemDiskPathsAndFolders`, `GeometryBoundingVolumes`, `GeometryRayAABBAndSphere`, `GeometryRayPlaneAndTriangle`, `GeometryOverlapsAndClosestPoints`, `MathPropertiesRandomized`, `GeometrySampleScene` |
| 81 | navigation (M20) | 16 | `NavGridBasics`, `NavAStarOptimalAgainstDijkstra`, `NavAStarCornersAndNeighbours`, `NavPartialPaths`, `NavLineOfSightAndErosion`, `NavStringPullingAndSpline`, `NavGridBuilderShapes`, `NavGridFromTilemap`, `NavGrid2DBakeInScene`, `NavGrid3DBakeOnColliders`, `NavAgent2DWalksToDestination`, `NavAgentFollowsTargetAndRepaths`, `NavAgent3DWithCharacterController`, `NavComponentsSerializationAndCopies`, `NavScriptApi`, `NavSampleScene3D` |
| 82 | M20: dynamic obstacles (386) | 3 | `NavGridCarving`, `NavObstacleCarvesAndAgentsRepath`, `NavObstacle3DOnFloor` |
| 83 | M20: steering (387) | 1 | `SteeringBehaviours` |
| 84 | M20: obstacle avoidance (388) | 2 | `SteeringFeelers`, `NavAgentAvoidsMovingBodies` |
| 85 | M20: AI state machine (389) | 1 | `AIStateMachineTransitions` |
| 86 | M20: sight (390) | 4 | `SightConeMath`, `SightSensor2DWithWalls`, `SightSensor3DWithWalls`, `SightScriptCallbacksAndAIComponentsSerialization` |
| 87 | M20: patrol routes, navigation debug drawing | 21 | `NavRandomGridsSmoothingAndCornerRules`, `PatrolPathModesAndWaits`, `PatrolPathWithNavAgent`, `PatrolPathChildrenAndSplineSources`, `PatrolScriptApiEventsAndSerialization`, `NavigationDebugDrawing`, `PatrolSampleScenes`, `BehaviorTreeComposites`, `BehaviorTreeDecoratorsParallelAndBlackboard`, `FormationSlotsAndAssignment`, `FormationMembersFollowTheLeader`, `FormationWithNavAgentsOnGrid`, `FormationSerializationAndCopies`, `FormationSampleScene`, `NavBakeFileFormat`, `NavBakeSavedWithScene`, `NavigationComponentsRoundTrip`, `NavAgentTargetPauseAndResume`, `NavGridStartBakesLateAndSpawned`, `NavSerializationAndFingerprintEdgeCases`, `FormationMembersKeepUpWhileWalking` |
| 88 | M21: prefab links, overrides, apply / revert, nesting | 9 | `PrefabLinkSerialization`, `PrefabInstantiateLinked`, `PrefabLegacyLinkMenuScene`, `PrefabOverrideDetection`, `PrefabApplyPropagate`, `PrefabRevertLevels`, `PrefabSyncStructural`, `PrefabNested`, `PrefabUndoFileEdits` |
| 89 | M21 batch B: prefab mode, references, versions, recovery | 6 | `EntityRefVisitorCoverage`, `PrefabEntityRefRemap`, `EntitySetVersionMigration`, `EntitySetPartialLoad`, `SceneSalvage`, `PrefabStageRoundTrip` |
| 90 | serialization fuzz (411) | 10 | `SerializationFuzz`, `PrefabCacheReads`, `LargeSceneLinkedInstances`, `PrefabVariants`, `PrefabPlacementAndThumbs`, `PrefabHotReloadSync`, `ClipboardPrefabFormat`, `PrefabExportValidation`, `PrefabFieldLabelsExist`, `PrefabDamagedFileNotWritten` |
| 91 | physics determinism (Box2D, Jolt) | 2 | `PhysicsDeterminism2D`, `PhysicsDeterminism3D` |
| 92 | editor Play copy (the exported game's order) | 1 | `PlayCopyRunsScriptsInFileOrder` |
| 93 | script hot reload (in-process) | 3 | `ScriptReloadKeepsFieldValues`, `StaleModuleCopiesRemoved`, `DeferredDestroyOnceEachAndLinear` |
<!-- test-catalog:end -->
