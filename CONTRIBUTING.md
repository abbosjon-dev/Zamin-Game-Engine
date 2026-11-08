# Zamin Engine'ga hissa qo'shish

Zamin'ga o'zgarish kiritmoqchi bo'lsangiz — xato tuzatish, yangi imkoniyat, namuna yoki hujjat — bu qo'llanma
build, kod uslubi, testlar, matnlar tili, hujjatlar va commit qoidalarini tushuntiradi. Ichki tuzilma:
[docs/Architecture.md](docs/Architecture.md); barcha hujjatlar: [docs/README.md](docs/README.md).

Mundarija:

1. [Build va ishga tushirish](#1-build-va-ishga-tushirish)
2. [Kod uslubi](#2-kod-uslubi)
3. [Matnlar: o'zbekcha, `Tr()` va lokalizatsiya](#3-matnlar-ozbekcha-tr-va-lokalizatsiya)
4. [Testlar](#4-testlar)
5. [Sanitizerlar, qamrov, benchmarklar](#5-sanitizerlar-qamrov-benchmarklar)
6. [Yangi komponent yoki skript API](#6-yangi-komponent-yoki-skript-api)
7. [Hujjatlar va rasmlar](#7-hujjatlar-va-rasmlar)
8. [Commit qoidalari](#8-commit-qoidalari)
9. [Vazifalar ro'yxati va CHANGELOG](#9-vazifalar-royxati-va-changelog)
10. [Pull request oldidan](#10-pull-request-oldidan)

---

## 1. Build va ishga tushirish

Talablar va boshqa distributivlar: [BuildAndExport.md](docs/BuildAndExport.md#1-talablar). Ishlab chiqish uchun Debug
build qulay (assert'lar yoqiq):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # yoki: cmake --preset debug
cmake --build build 2>&1 | grep -E "error|warning"       # bo'sh chiqishi kerak
./build/bin/ZaminTests                                   # oxirgi qator: "N test, 0 xato"
```

- Manba fayllar glob bilan yig'iladi: **yangi** `.cpp` / `.h` qo'shgandan keyin `cmake -S . -B build` ni qayta
  ishga tushiring.
- Namuna o'yinlar (`Projects/Platformer2D`, `Showcase3D`, `Adventure3D`) asosiy build'ning bir qismi: dvigatel API'si
  o'zgarsa ularning skriptlari ham kompilyatsiya qilinadi — ular ham buzilmasligi kerak.
- Presetlar (`CMakePresets.json`): `debug`, `release`, `clang`, `asan`, `ubsan`, `lsan`, `tsan`, `coverage`.
  CMake variantlari: [BuildAndExport.md, 3-bo'lim](docs/BuildAndExport.md#3-cmake-variantlari).
- Editor va player'ni displeysiz tekshirish (headless, `xvfb-run`; editor va player avtomatlashtirish o'zgaruvchilari
  — [Editor.md, 15-bo'lim](docs/Editor.md#15-avtomatlashtirish-skrinshotlar-ci) va
  [Runtime.md, 14-bo'lim](docs/Runtime.md#14-avtomatlashtirish-uchun-muhit-ozgaruvchilari)):

```bash
ZAMIN_MAX_FRAMES=60 ZAMIN_SCREENSHOT=/tmp/editor.png xvfb-run -a -s "-screen 0 1600x900x24" \
    ./build/bin/ZaminEditor Projects/Showcase3D 2>&1 | grep -iE "error|assert"
ZAMIN_MAX_FRAMES=60 ZAMIN_SCREENSHOT=/tmp/game.png xvfb-run -a -s "-screen 0 1600x900x24" \
    ./build/bin/ZaminPlayer Projects/Adventure3D 2>&1 | grep -iE "error|assert"
```

## 2. Kod uslubi

Uslub `.clang-format` faylida (clang-format 17+): u **mavjud** uslubni tasvirlaydi, kod bazasini qayta formatlash
uchun emas.

- C++20, hamma narsa `namespace Zamin` ichida; 4 bo'shliq, tab yo'q; qavslar yangi qatorda (Allman) —
  `namespace Zamin {` bundan mustasno, ichi chekinish bilan.
- `Type* name`, `Type& name`; `template<typename T>`; `{ 1.0f, 2.0f }` (qavs ichida bo'shliq).
- Ustun chegarasi yo'q: qo'lda bo'lingan qatorlar saqlanadi (odatda ~120–160 ustun); include tartibi o'zgartirilmaydi.
- Nomlar: klasslar, funksiyalar, komponent maydonlari — `PascalCase` (`TransformComponent::Translation`,
  `Scene::OnUpdateRuntime`); a'zo o'zgaruvchilar — `m_` (`m_ActiveScene`), statik — `s_`; makroslar — `ZM_`.
- `Ref<T>` = `std::shared_ptr<T>`, `Scope<T>` = `std::unique_ptr<T>` (`Core/Base.h`).
- Log: dvigatelda `ZM_CORE_INFO` / `ZM_CORE_WARN` / `ZM_CORE_ERROR`, editor, player va o'yinlarda `ZM_INFO` /
  `ZM_WARN` / `ZM_ERROR` — `std::format` sintaksisi bilan (`ZM_WARN("Fayl topilmadi: {}", path)`). Tekshiruvlar:
  `ZM_ASSERT` / `ZM_CORE_ASSERT` (faqat assert'lar yoqiq build'da) va `ZM_VERIFY` (doim bajariladi) — xabar bilan
  ([Core.md, 12-bo'lim](docs/Core.md#12-assert-va-verify)).
- Kod izohlari — **inglizcha**. Ommaviy header'larda Doxygen izohlari (`///`, `///<`): yangi ommaviy klass yoki
  funksiyaga qisqa, aniq izoh yozing — `cmake --build build --target docs` ogohlantirishsiz bo'lishi kerak
  (`build/docs/doxygen-warnings.log`).
- Build **ogohlantirishsiz**: bizning target'lar `-Wall -Wextra -Wpedantic` bilan GCC va Clang'da
  (`zamin_target_settings`, `cmake/ZaminGame.cmake`).

Faqat o'zingiz o'zgartirgan qatorlarni tekshiring:

```bash
Tests/format_check.sh                 # commit qilinmagan o'zgarishlar (HEAD ga nisbatan)
Tests/format_check.sh --base main     # tarmoq main'dan ajralgandan beri
Tests/format_check.sh --fix --base main
```

Yangi fayllar git'ga qo'shilgan bo'lishi kerak (`git add` yoki `git add -N`). `vendor/` hech qachon formatlanmaydi.
clang-format ifodalay olmaydigan uy odatlari bor — bir qatordagi bir nechta qisqa ifoda (`a.X = 1; a.Y = 2;`), bir
qatorli qisqa `enum` — shunday qatordagi taklifni e'tiborsiz qoldirish mumkin; shuning uchun CI'dagi `format` job
faqat ogohlantiradi. Butun kod bazaning farqi: `Tests/format_check.sh --stats` ([Quality.md](docs/Quality.md#formatlash)).

## 3. Matnlar: o'zbekcha, `Tr()` va lokalizatsiya

- Foydalanuvchi ko'radigan **hamma** matn — editor interfeysi, log va xato xabarlari, player oynalari — o'zbek tilida,
  lotin yozuvida (`o'` / `g'` va tutuq belgisi oddiy `'` bilan). Kod izohlari va identifikatorlar — inglizcha.
- **Editor matnlari** `Tr()` orqali o'tadi (`Zamin/Editor/EditorLanguage.h`): o'zbekcha matnning o'zi kalit, inglizcha
  tarjimasi `Zamin/src/Zamin/Editor/EditorLanguage.cpp` dagi `s_English` jadvalida. Boshidagi ikonka (`ICON_FA_...`),
  oxiridagi `...` / `:` va ImGui id'si (`##id`, `###id`) tarjimaga kirmaydi:

  ```cpp
  if (ImGui::MenuItem(Tr(ICON_FA_GEAR "  Loyiha sozlamalari...")))   // jadvalda: { "Loyiha sozlamalari", "Project settings" }
      m_ShowProjectSettings = true;
  ImGui::Begin(Tr(ICON_FA_STOPWATCH "  Profiler###Profiler"));       // ### - til o'zgarsa ham oyna id'si bir xil
  ```

  `UI::PropertyLabel`, `UI::PropertyHeader` kabi yordamchilar (`Editor/src/UI.h`) `Tr()` ni o'zi chaqiradi. Bir xil
  o'zbekcha so'z ikki ma'noda bo'lsa — `EditorText::Choose("Bekor qilish", "Undo")`. Jadvalda tarjimasi yo'q matn
  o'zbekcha ko'rinadi (xato emas), lekin menyu, oyna sarlavhasi va umumiy tugmalar uchun tarjima qo'shing. Inglizcha
  interfeysni tekshirish: `ZAMIN_EDITOR_LANGUAGE=en`. `EditorLanguageStrings` testi jadvalda takroriy yoki bo'sh
  yozuv yo'qligini tekshiradi.
- **Log xabarlari** tarjima qilinmaydi — to'g'ridan-to'g'ri o'zbekcha: `ZM_CORE_ERROR("Sahna yuklanmadi: {}", path)`.
  [FAQ.md](docs/FAQ.md) dagi xabarni o'zgartirsangiz, FAQ'ni ham yangilang (`DocsCheck` tekshiradi).
- **O'yin matnlari** (o'yinchi ko'radigani) — dvigatel emas, o'yinning ishi: `Assets/Localization/<til>.json` va
  skriptdagi lokalizatsiya API ([ProjectSettings.md](docs/ProjectSettings.md), [Scripting.md](docs/Scripting.md)).

## 4. Testlar

| Nima o'zgardi | Nimani ishga tushirish |
|---|---|
| Har qanday o'zgarish | `cmake --build build` (ogohlantirishsiz) va `./build/bin/ZaminTests` (`N test, 0 xato`) |
| Dvigatel mantig'i | Yangi yoki kengaytirilgan unit test `Tests/ZaminTests.cpp` da |
| Hujjatlar, komponentlar, skript API, player variantlari | `ctest --test-dir build -L docs` |
| Rendering | `Tests/render_tests.sh` (ko'rinish ataylab o'zgargan bo'lsa `--update`) |
| Editor | Editor smoke testi: `Tests/editor_smoke.sh` (yoki `ctest --test-dir build -L gui`) |
| Skript moduli, eksport | `Tests/hot_reload_test.sh`, `Tests/export_test.sh` |

Unit testlar headless (GPU va oyna yo'q) — **tekstura, mesh, shrift yoki shader yaratmang**. Asosiy kamerasi yo'q
sahna chizilmaydi, shuning uchun uning runtime yangilanishini testda bemalol chaqirish mumkin. Test yozilishi:

```cpp
TEST(MyFeatureRoundTrip)
{
    Ref<Scene> scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Quti");
    e.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
    CHECK(e.GetComponent<TransformComponent>().Translation.y == 2.0f);
    CHECK_NEAR(e.GetComponent<TransformComponent>().Translation.z, 3.0f, 1e-5f);
}
```

- Testni mavzusiga mos bo'lim izohi (`// ---------- ...`) ostiga qo'ying, keyin ro'yxatni yangilang:
  `Tests/test_catalog.py --update docs/Testing.md` (`DocsCheck` eskirgan ro'yxatni topadi).
- Tuzatilgan har bir xatoga uni qaytarmaydigan test yozing ([Testing.md](docs/Testing.md#topilgan-xatolarni-qoriqlovchi-testlar)).
- Bitta testni ishga tushirish: `./build/bin/ZaminTests MyFeature*` (glob, `-` bilan chiqarib tashlash, `--list`,
  `--timing`) — [Testing.md, 1-bo'lim](docs/Testing.md#1-unit-testlar---zamintests).

## 5. Sanitizerlar, qamrov, benchmarklar

- Xotira bilan ishlaydigan, ko'p oqimli yoki fizika / audio kodini o'zgartirsangiz — sanitizerlar:
  `Tests/sanitize.sh address --smoke --editor-smoke` (ASan + LSan) va `Tests/sanitize.sh undefined --compiler clang`.
  Ular alohida `build-asan` / `build-ubsan` papkalarida build qiladi. Bizning koddagi xato hech qachon
  `Tests/sanitizers/*.supp` ga yozilmaydi — tuzatiladi; u yerda faqat uchinchi tomon kodining shovqini (izohi bilan).
- Kod qamrovi: `Tests/coverage.sh [--gui] [--html]` ([Coverage.md](docs/Coverage.md)).
- Tezlikka ta'sir qiladigan o'zgarishda: `Tests/bench.sh --compare` (Release boshlang'ich natijasi bilan) va kerak
  bo'lsa `Tests/render_bench.sh` ([Benchmarks.md](docs/Benchmarks.md)).

## 6. Yangi komponent yoki skript API

Yangi komponent — [Architecture.md, 17-bo'lim](docs/Architecture.md#17-yangi-komponent-qoshish) dagi ro'yxat:
`Components.h` (struct + `AllComponents`), `ComponentRegistry`, `SceneSerializer.cpp` (yozish va o'qish),
`SceneHierarchyPanel.cpp` (`AddSection<T>` inspektor bo'limi va `DisplayAddComponentEntry<T>`), runtime maydonlari
bo'lsa `Scene.cpp` dagi `ResetRuntimeData` ning ikkala varianti, unit test, so'ng hujjat:
[Components.md](docs/Components.md) da `### NomComponent` bo'limi va qo'lda yozilgan tavsif, keyin
`python3 Tests/docs_components.py --update docs/Components.md` (maydonlar jadvali koddan yaratiladi).

`ScriptableEntity` ga yangi ommaviy funksiya — [Scripting.md](docs/Scripting.md) da tilga oling (API ma'lumotnomasi
jadvali). Player'ga yangi buyruq qatori varianti — [Runtime.md](docs/Runtime.md) da; yangi `ZAMIN_*` CMake varianti —
[BuildAndExport.md](docs/BuildAndExport.md) da. Bularni `DocsCheck` tekshiradi.

## 7. Hujjatlar va rasmlar

- Hujjatlar `docs/` da, **o'zbekcha**, fayl nomlari PascalCase (`Scripting.md`, `BuildAndExport.md`). Yangi hujjatni
  [docs/README.md](docs/README.md) indeksiga qo'shing. Mavjud hujjatni takrorlamang — havola bering.
- Har bir klass, funksiya, maydon, makros, menyu yo'li, muhit o'zgaruvchisi va fayl nomi kodda **haqiqatan bor**
  bo'lishi kerak — yozishdan oldin `grep` bilan tekshiring. Kod misollari ishlaydigan bo'lsin: hujjatdagi butun skript
  klasslari (`class X : public ScriptableEntity`) `DocsSnippets` testida kompilyatsiya qilinadi.
- Tekshiruvlar: `ctest --test-dir build -L docs` — `DocsCheck` (`Tests/docs_check.py`: havolalar va `#langar`lar,
  komponentlar, skript API, player variantlari, FAQ xabarlari, CMake variantlari, testlar katalogi, CHANGELOG,
  indeks, rasmlar) va `DocsSnippets` (`Tests/docs_snippets.py`).
- **Rasmlar** `docs/images/` da: haqiqiy editor / player skrinshotlari (headless, yuqoridagi buyruqlar va
  `ZAMIN_EDITOR_*` o'zgaruvchilari bilan), kengligi **1280 px** dan oshmaydi, har biri **400 KB** gacha (`DocsCheck`
  tekshiradi), har biri biror hujjatda ishlatilgan. Tavsiya etilgan aylantirish:

  ```bash
  convert shot.png -resize '1280x>' -strip -interlace JPEG -quality 85 docs/images/mavzu-nima.jpg
  ```

  Rasm ostiga kursiv izoh yozing: qaysi loyiha / sahna, nima ko'rinyapti.

## 8. Commit qoidalari

- Commit sarlavhasi **o'zbekcha**, qisqa xulosa; milestone raqami bilan boshlanadi
  ([docs/TASKS.md](docs/TASKS.md)): `M22: 436-440 bajarildi - .clang-format, CI sanitizers job ...`. Ish tugamagan
  bo'lsa — `M22 (jarayonda): ...`, oldingi ishni tuzatsa — `M22 tuzatishlar: ...`. `Tests/changelog.py` commit'larni
  shu prefiks bo'yicha milestone'larga ajratadi (prefikssiz commit oldingisiga qo'shiladi).
- Har bir commit build qilinadi va testlardan o'tadi (bisect uchun); build natijalari (`build/`,
  `Projects/*/Binaries/`, `Projects/*/.zamin/`) commit qilinmaydi — `.gitignore` ularni chetlatadi.
- Bir commit — bitta mantiqiy o'zgarish (katta milestone bir nechta commit'ga bo'linadi). Tarix qayta yozilmaydi
  (`push --force` yo'q).
- Kod bilan birga uning testi va hujjati bitta commit'da.

## 9. Vazifalar ro'yxati va CHANGELOG

- Vazifa bajarilib **tekshirilgandan** keyin [docs/TASKS.md](docs/TASKS.md) da `- [ ] NNN.` ni `- [x] NNN.` ga
  o'zgartiring. Qisman bajarilganini belgilamang.
- [CHANGELOG.md](CHANGELOG.md) — milestone'lar bo'yicha, eng yangisi tepada. Milestone'ning birinchi vazifasi
  bajarilganda unga `## Mnn Nomi` bo'limini qo'shing va asosiy o'zgarishlarni qisqa ro'yxat qilib yozing. Bo'lim
  ostidagi birinchi qator git tarixidan olinadi:

  ```bash
  python3 Tests/changelog.py               # har milestone uchun to'g'ri qator (sana, vazifalar, commit'lar)
  python3 Tests/changelog.py --commits M23 # milestone commit'lari - xulosa yozish uchun
  python3 Tests/changelog.py --check       # CHANGELOG.md tarixga mosmi (DocsCheck ham shuni qiladi)
  ```

  Eng so'nggi milestone qatorida faqat birinchi commit tekshiriladi (u har commit bilan o'sadi); keyingi milestone
  boshlanganda oldingisining qatorini to'liq (commit'lar soni va oralig'i) yangilang.

## 10. Pull request oldidan

- [ ] `cmake --build build` — xato va ogohlantirishsiz (GCC; imkon bo'lsa Clang ham: `cmake --preset clang`).
- [ ] `./build/bin/ZaminTests` — `0 xato`; `ctest --test-dir build -LE gui` — hammasi o'tdi.
- [ ] Editor, rendering yoki player o'zgargan bo'lsa — headless smoke (1-bo'lim) yoki `ctest --test-dir build -L gui`.
- [ ] `Tests/format_check.sh --base main` — yangi qatorlar uslubda.
- [ ] Yangi foydalanuvchi matnlari o'zbekcha, editor matnlari `Tr()` bilan (va tarjimasi jadvalda).
- [ ] Hujjatlar yangilandi (`ctest --test-dir build -L docs`), kerak bo'lsa rasm; TASKS.md va CHANGELOG.md.

CI (`.github/workflows/linux.yml`) har push va pull request'da GCC va Clang build'ini, unit testlarni, rendering,
editor smoke, hot reload va eksport testlarini, sanitizer, format va coverage job'larini ishga tushiradi
([Testing.md](docs/Testing.md#ci-githubworkflowslinuxyml)).
