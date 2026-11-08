# Kod qamrovi (code coverage, gcov)

Testlar dvigatel kodining qaysi qismini haqiqatan ishga tushirishini ko'rsatadi: qaysi modul yaxshi tekshirilgan, qaysi
fayllarga test umuman tegmaydi. Raqam o'zi maqsad emas - u testlar yetmagan joylarni topish uchun.

## Ishga tushirish

```bash
Tests/coverage.sh                    # build-cov (coverage build), ZaminTests, hisobot: build-cov/Coverage
Tests/coverage.sh --gui              # + ZaminPlayer (3 namuna o'yin, headless) va editor smoke testi (xvfb)
Tests/coverage.sh --html             # + har fayl uchun HTML hisobot (gcovr o'rnatilgan bo'lsa: pip install gcovr)
Tests/coverage.sh --filter "Prefab*" # faqat ba'zi testlar (ZaminTests --filter)
Tests/coverage.sh --min-engine 55    # engine qatorlar qamrovi 55 % dan past bo'lsa 1 kodi bilan tugaydi
Tests/coverage.sh --report-only      # faqat hisobot, oxirgi ishga tushirish qoldirgan sanoqlardan
Tests/coverage.sh --clean --out DIR  # hisobot DIR ga, build papkasi (~1 GB) oxirida o'chiriladi
```

Skript nima qiladi:

1. **Coverage build** - alohida papkada (`build-cov`, asosiy `build/` ga tegmaydi): `-DZAMIN_COVERAGE=ON`
   (`cmake/Coverage.cmake`, `CMakePresets.json` dagi `coverage` preset). Faqat bizning target'lar (engine, editor,
   player, testlar, benchmarklar, namuna o'yin modullari - `zamin_target_settings`) `--coverage -O0
   -fprofile-update=atomic` bilan kompilyatsiya qilinadi; vendor kutubxonalar (Box2D, Jolt, ImGui, ...) yo'q, shuning
   uchun hisobot faqat bizning kodimiz haqida. `-O0` - qator sanoqlari manbaga mos (particle fayllari ham, odatdagi
   Debug'dagi `-O2` siz); atomik sanoqlar - vazifalar tizimining ishchi oqimlari ham engine kodini bajaradi. O'yin
   modullari sanitizer build'dagidek `build-cov/GameModules` ga yoziladi.
2. Har ishga tushishda eski `.gcda` sanoqlari o'chiriladi, keyin **`ZaminTests`** (barcha testlar) ishlaydi;
   `--gui` bilan player va editor ham.
3. **`Tests/coverage_report.py`** (faqat standart kutubxona) har `.gcda` uchun `gcov --json-format` ni parallel
   ishga tushiradi, bir xil faylning barcha translation unit'lardagi natijalarini birlashtiradi (bir sarlavha
   faylning qatori biror `.cpp` uni bajargan bo'lsa bajarilgan), repozitoriyadan tashqaridagi va `vendor/`, build
   papkalaridagi fayllarni tashlaydi va **modullar bo'yicha** jamlaydi: engine papkalari (`Zamin/Core`,
   `Zamin/Scene`, `Zamin/Renderer`, ...), `Editor`, `Editor/Panels`, `Player`, namuna o'yinlar (`Namuna/<loyiha>`).
   Testlarning o'z kodi faqat `--include-tests` bilan.
4. Natija: `coverage-summary.md` (Markdown jadval - CI uni ish sahifasiga chiqaradi), `coverage-summary.html` (o'sha
   jadval ustunchalar bilan, bitta sahifa - gcovr kerak emas), `coverage.json` (modullar, fayllar, har faylning
   bajarilmagan qatorlari), `--html` bilan `html/index.html` (gcovr: har fayl qatorma-qator, tarmoqlar ham).

Clang build uchun: `Tests/coverage.sh --compiler clang` (gcov o'rniga `llvm-cov gcov`).

**CI:** `.github/workflows/linux.yml` dagi `coverage` job `Tests/coverage.sh --html --out coverage-report` ni
ishga tushiradi, jadvalni ish sahifasiga yozadi va hisobotni `coverage-report` artefakti qilib yuklaydi.

## Natijalar (2026-11-08)

Muhit: GCC 13.3, Debug + `--coverage -O0`, 4 vCPU. Ikki o'lchov: **unit testlar** (`Tests/coverage.sh`, CI'dagi
`coverage` job) - faqat `ZaminTests` (627 test, hammasi o'tdi), editor / player build qilinmaydi; **`--gui` bilan** -
unit testlar + ZaminPlayer (Platformer2D, Showcase3D, Adventure3D; 120 kadrdan, headless) + editor smoke testi
(Showcase3D 221 qadam, Platformer2D 263 qadam, 0 xato). Qator - manbadagi bajariladigan qator bir marta (bir necha
`.cpp` ichida ishlatilgan sarlavha ham), funksiya - manbadagi bitta funksiya.

| Modul | Fayllar | Qatorlar | Unit testlar: qatorlar / funksiyalar | `--gui` bilan: qatorlar / funksiyalar |
|---|---:|---:|---:|---:|
| Zamin/AI | 21 | 2 422 | 88.6 % / 93.5 % | 89.2 % / 92.6 % |
| Zamin/Asset | 18 | 2 657 | 91.4 % / 97.4 % | 92.5 % / 99.6 % |
| Zamin/Audio | 8 | 2 077 | 93.6 % / 95.1 % | 93.7 % / 95.8 % |
| Zamin/Core | 48 | 3 953 | 81.6 % / 88.6 % | 87.5 % / 94.3 % |
| Zamin/Debug | 3 | 848 | 72.3 % / 91.8 % | 88.3 % / 98.4 % |
| Zamin/Editor | 31 | 2 967 | 93.3 % / 97.8 % | 93.9 % / 99.4 % |
| Zamin/Events | 1 | 49 | 18.8 % / 13.6 % | 49.0 % / 44.4 % |
| Zamin/ImGui | 3 | 197 | 0.0 % / 0.0 % | 89.3 % / 81.8 % |
| Zamin/Localization | 2 | 653 | 91.1 % / 97.8 % | 91.4 % / 97.8 % |
| Zamin/Math | 19 | 1 986 | 95.2 % / 97.7 % | 94.8 % / 95.8 % |
| Zamin/Physics | 14 | 3 617 | 92.4 % / 95.2 % | 92.5 % / 95.5 % |
| Zamin/Platform | 10 | 1 423 | 8.2 % / 11.3 % | 59.0 % / 71.0 % |
| Zamin/Project | 16 | 3 236 | 93.8 % / 99.3 % | 93.9 % / 99.3 % |
| Zamin/Renderer | 51 | 6 847 | 56.9 % / 67.8 % | 85.3 % / 90.4 % |
| Zamin/Scene | 60 | 23 463 | 87.7 % / 93.2 % | 90.6 % / 95.2 % |
| Zamin/Scripting | 9 | 1 414 | 67.8 % / 64.7 % | 69.2 % / 66.4 % |
| Zamin/UI | 9 | 956 | 92.5 % / 99.1 % | 94.8 % / 100.0 % |
| Zamin/Utils | 6 | 890 | 93.3 % / 99.2 % | 93.3 % / 99.2 % |
| Editor | 38 | 17 268 | - (build qilinmaydi) | 46.0 % / 56.4 % |
| Editor/Panels | 36 | 21 009 | - (build qilinmaydi) | 41.6 % / 58.6 % |
| Player | 1 | 862 | - (build qilinmaydi) | 33.5 % / 50.0 % |
| Namuna/Adventure3D | 3 | 308 | - (build qilinmaydi) | 60.1 % / 61.1 % |
| Namuna/Platformer2D | 14 | 1 126 | - (build qilinmaydi) | 41.5 % / 40.9 % |
| Namuna/Showcase3D | 5 | 1 174 | - (build qilinmaydi) | 5.1 % / 8.1 % |
| **Engine (Zamin) jami** | 329 | 59 655 | **82.6 %** / 86.3 % | **89.3 %** / 92.4 % |
| **Editor jami** | 74 | 38 277 | - | **43.6 %** / 57.5 % |
| **Player** | 1 | 862 | - | **33.5 %** / 50.0 % |
| **Namuna o'yinlar jami** | 22 | 2 608 | - | **27.3 %** / 29.3 % |
| **Hammasi** | 426 | 101 402 | **82.6 %** / 86.3 % | **70.0 %** / 82.6 % |

Jami: **engine qatorlarining 82.6 % i unit testlarning o'zi bilan**, player va editor ishlaganda **89.3 %**;
`--gui` bilan butun kod (engine + editor + player + namuna o'yinlar, 101 402 qator) - **70.0 %**.

### Nima ko'rinadi

- **Yaxshi qamralgan** (90 % dan yuqori): Scene (sahna, serializatsiya, prefablar - eng katta modul, 23 463 qator),
  Physics, Audio, Asset, Project, Math, UI, Utils, Localization. Ular asosan unit testlar bilan tekshiriladi.
- **Faqat GUI bilan qamraladi:** Renderer (unit testlarda 56.9 % - testlar GPU'siz; player va editor bilan 85.3 %),
  Platform (OpenGL, GLFW oynasi: 8.2 % -> 59.0 %), ImGui qatlami (0 -> 89.3 %). Rendering regressiya testi
  (`ZaminRenderTests`) bu hisobotga kirmaydi - u ham shu kodni ishlatadi.
- **Kam qamralgan:** Scripting (69 %) - `ScriptableEntity` API'sining ko'p yordamchi funksiyalari faqat skriptlardan
  chaqiriladi va testlarda bir qismi ishlatiladi; Events (49 %) - kichik, faqat oyna hodisalari; editor (43.6 %) -
  smoke test har oynani ochadi, lekin ko'p tugma / menyu amallari bosilmaydi (masalan `AnimationTimelinePanel` 10 %,
  `EditorPrefabs.cpp` 3.9 %, `UICurves.cpp` 0 %). Namuna o'yinlar: Showcase3D 5.1 % - player faqat boshlang'ich sahnani
  120 kadr o'ynaydi, qolgan namoyish sahnalari skriptlari ishga tushmaydi.
- **Qamrov topgan kamchilik (tuzatildi):** birinchi `--gui` o'lchovida `SceneHierarchyPanel.cpp` 4.5 %, asset
  inspektori 5.6 % edi - smoke testi har obyektni tanlar va har komponentni qo'shar edi, lekin Inspektor oynasining
  tab'i oldingi qadamlarda ochilgan oynalar ortida qolib, umuman chizilmasdi (yashirin tab hech narsa chizmaydi). Endi
  shu qadamlarda Inspektor oldinga chiqariladi (`EditorSmokeTest.cpp`): Editor/Panels 21.2 % -> 41.6 %, editor jami
  32.1 % -> 43.6 %, va har komponent inspektori xatosiz chizilishi haqiqatan tekshiriladi.
- **Instrumentatsiya ta'siri:** coverage build'da `JobSystemSpeedupAndStats` testi tezlanishni o'lcholmaydi (gcov
  sanoqlari atomik va hamma oqimlar bir xil sanoqlarni yangilaydi: 4 oqim 1 oqimdan sekinroq chiqdi). Shu build'da
  (`ZAMIN_COVERAGE_BUILD`) faqat tezlanish tekshiruvi o'tkazib yuboriladi; natijalar va statistika tekshiriladi.
  Testlar coverage bilan ~3.5 marta sekin (136 s, odatdagi Debug'da 39 s).

### Boshqa vositalar bilan farq

`--html` (gcovr) har bir fayl qatorma-qator ko'rsatadi; uning jami foizi biroz past chiqadi (masalan 62.8 % va 65.6 %
o'sha ma'lumotdan), chunki gcovr shablon va lambda nusxalarini alohida qator sifatida sanaydi. Modullar jadvali har
manba qatorini bir marta sanaydi (lcov kabi) - solishtirishda bir xil vositadan foydalaning.
