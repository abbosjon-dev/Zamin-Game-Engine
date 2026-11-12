# Zamin Engine hujjatlari

Zamin Engine'ning barcha hujjatlari bir joyda. Hammasi o'zbek tilida; kod misollari dvigatelning haqiqiy API'si va
namuna o'yinlardan (`Projects/Platformer2D`, `Projects/Showcase3D`, `Projects/Adventure3D`) olingan va
`ctest --test-dir build -L docs` bilan kodga mosligi tekshiriladi ([Testing.md](Testing.md#10-kompilyatsiya-tekshiruvlari)).
Bosh sahifa — [README](../README.md).

**Qayerdan boshlash:**

- **Dvigatelni endi ko'ryapsizmi?** [README](../README.md) → [build](BuildAndExport.md#2-dvigatelni-build-qilish) →
  [birinchi 2D o'yin](Tutorial-2D.md) yoki [birinchi 3D o'yin](Tutorial-3D.md) → [Editor](Editor.md).
- **O'yin yozyapsizmi?** [C++ skriptlar](Scripting.md) va [komponentlar](Components.md), keyin kerakli tizim qo'llanmasi
  (pastdagi jadval); muammo bo'lsa — [FAQ](FAQ.md).
- **Dvigatelning o'zini o'zgartirmoqchimisiz?** [Arxitektura](Architecture.md) →
  [hissa qo'shish qoidalari](../CONTRIBUTING.md) → [testlar](Testing.md).

## Boshlash

| Hujjat | Nima haqida |
|---|---|
| [README](../README.md) | Bosh sahifa: imkoniyatlar qisqacha, skrinshotlar, tez boshlash |
| [Tutorial-2D.md](Tutorial-2D.md) | Birinchi 2D o'yin "Tangalar": qahramon, tangalar, hisob — editorda qadam-baqadam, to'liq kod bilan |
| [Tutorial-3D.md](Tutorial-3D.md) | Birinchi 3D o'yin "Kristallar": yurish va sakrash, kuzatuvchi kamera, kristallar, qayta boshlash |
| [Editor.md](Editor.md) | Editor: launcher, oynalar, menyular, viewport, inspektor, kontent brauzer, Play rejimi, yorliqlar, avtomatlashtirish |
| [FAQ.md](FAQ.md) | Ko'p beriladigan savollar va muammolar — dvigatelning haqiqiy xabarlari bilan |
| [Features.md](Features.md) | Imkoniyatlarning to'liq, batafsil ro'yxati (editor, dvigatel), editor boshqaruvi, yo'l xaritasi |
| [Samples.md](Samples.md) | Namuna o'yinlar galereyasi: to'qqiz o'yin rasmlari, nimalarni ko'rsatadi, rekordlar, boshqaruv va gamepad, testlar, eksport, ishlash tezligi |

## O'yin yaratish

- [Terrain.md](Terrain.md): 3D map, relyef cho'tkalari, teksturalar, o'rmon/prefab ekish va heightmap.


| Hujjat | Nima haqida |
|---|---|
| [Scripting.md](Scripting.md) | C++ skriptlar: `ScriptableEntity`, `ZM_FIELD` maydonlari, hayot sikli, callback'lar, API ma'lumotnomasi, hot reload |
| [Components.md](Components.md) | Barcha komponentlar ma'lumotnomasi: vazifasi, menyu yo'li, sahna kaliti, maydonlar jadvali |
| [Rendering.md](Rendering.md) | Kameralar, 2D (sprite, atlas, tilemap, 2D yorug'lik), 3D (mesh, glTF, materiallar, yorug'lik, soyalar, osmon), post-processing, particle'lar |
| [Physics.md](Physics.md) | 2D (Box2D) va 3D (Jolt) fizika: jismlar, collider'lar, joint'lar, effektorlar, Character Controller, so'rovlar, qatlamlar |
| [GameplayQuest.md](GameplayQuest.md) | Objective/prerequisite, atomic rewards/XP, quest HUD and v1/v2 saved progression |
| [GameplayUI.md](GameplayUI.md) | Live HUD, inventory pages, modal focus/pause ownership and keyboard/gamepad workflows |
| [GameplaySave.md](GameplaySave.md) | Stable-key health/inventory/interaction/AI saves, atomic slots, backup recovery and fresh-process prefab resume |
| [GameplayAI.md](GameplayAI.md) | Patrol/chase/investigate/attack/return, hostile LOS, nav recovery and 2D/3D NPC prefab courses |
| [GameplayRules.md](GameplayRules.md) | Shared health/shield, melee, death/respawn, inventory, transactional interaction and combat courses |
| [Gameplay.md](Gameplay.md) | 2D/3D motor, follow/orbit camera, actual velocity Animator binding, persistent checkpoints and playable courses |
| [Animation.md](Animation.md) | Property kliplar va timeline, skelet animatsiya, Animator, 1D/2D blend, Blueprint boshqaruvi, qatlamlar, root motion, sprite kliplar, tween'lar |
| [UI.md](UI.md) | Ekran UI: langarlar va layout, vidjetlar, matn, hodisalar, navigatsiya, UI animatsiyalari, pauza menyusi |
| [Audio.md](Audio.md) | Audio Source va bir martalik ovozlar, 3D ovoz, mixer guruhlari, musiqa, filtrlar, ambient zonalar |
| [Scenes.md](Scenes.md) | Sahnalarni yuklash, additive va fonda yuklash, o'tishlar, build ro'yxati, hodisalar, global holat, sub-sahnalar |
| [Prefabs.md](Prefabs.md) | Prefab'lar: bog'langan nusxalar, override'lar, Apply / Revert, ichma-ich prefab'lar, variantlar, prefab rejimi |
| [AI.md](AI.md) | Navigatsiya va AI: NavGrid, A*, Nav Agent, to'siqlar, steering, ko'rish, patrul, formatsiya, behavior tree |
| [ProjectSettings.md](ProjectSettings.md) | Loyiha sozlamalari: player oynasi, sifat darajalari, grafika, vaqt, fizika, audio, input, teglar, lokalizatsiya |
| [Core.md](Core.md) | Matematika va yordamchilar: geometriya, shovqin, spline, ranglar, Random, vazifalar tizimi, log, assert, fayl tizimi |
| [Runtime.md](Runtime.md) | Zamin Player: ishga tushish, buyruq qatori, sozlamalar ustunligi, log va crash fayllari, o'yin menyusi namunasi |

## Build, eksport va tarqatish

| Hujjat | Nima haqida |
|---|---|
| [BuildAndExport.md](BuildAndExport.md) | Talablar, CMake variantlari va presetlar, o'yin modulini build qilish, Doxygen, eksport, paketlash, tarqatish |
| [Runtime.md, 11-bo'lim](Runtime.md#11-eksport) | Eksport qilingan o'yin player tomonidan |

API ma'lumotnomasi (Doxygen, header'lardagi izohlardan): `cmake --build build --target docs` →
`build/docs/html/index.html` ([BuildAndExport.md, 7-bo'lim](BuildAndExport.md#7-api-malumotnomasi-doxygen)).

## Ichki tuzilma va hissa qo'shish

| Hujjat | Nima haqida |
|---|---|
| [Architecture.md](Architecture.md) | Target'lar, kod tuzilmasi, asosiy sikl, sahna kadri, skript moduli, renderer, fizika, audio, oqimlar, yangi komponent qo'shish |
| [CONTRIBUTING.md](../CONTRIBUTING.md) | Build, kod uslubi, o'zbekcha matnlar va `Tr()`, testlar, sanitizerlar, hujjatlar va rasmlar, commit qoidalari |

## Sifat va testlar

| Hujjat | Nima haqida |
|---|---|
| [Testing.md](Testing.md) | Barcha testlar va ularni qachon ishga tushirish, regressiya testlari, CI, unit testlar katalogi |
| [Quality.md](Quality.md) | Sifat hisoboti: testlar, sanitizerlar, qamrov, ogohlantirishlar, formatlash va benchmarklarning o'lchangan natijalari |
| [Coverage.md](Coverage.md) | Kod qamrovi (gcov): build, hisobot, modullar bo'yicha natijalar |
| [Benchmarks.md](Benchmarks.md) | Benchmark dasturlari va Release boshlang'ich natijalari, solishtirish |
| [Performance.md](Performance.md) | Ishlash tezligi hisoboti: profiler yozuvlaridan eng og'ir joylar, M25 da tuzatilganlar, qolgan cheklovlar |

## Loyiha

| Hujjat | Nima haqida |
|---|---|
| [TASKS.md](TASKS.md) | 500 ta vazifa rejasi (25 milestone), bajarilganlari belgilangan |
| [CHANGELOG.md](../CHANGELOG.md) | O'zgarishlar tarixi milestone'lar bo'yicha (git tarixidan) |
| [LICENSE](../LICENSE) | MIT litsenziyasi |

## Skrinshotlar

Qo'llanmalardagi rasmlar — editor va player'dan headless olingan haqiqiy skrinshotlar (qanday olinishi:
[CONTRIBUTING.md, 7-bo'lim](../CONTRIBUTING.md#7-hujjatlar-va-rasmlar)). Rasm ustiga bosilsa to'liq o'lchamda ochiladi.

| | | |
|---|---|---|
| [<img src="images/editor-new-project.jpg" width="300" alt="Yangi loyiha">](Editor.md#1-ishga-tushirish-va-launcher)<br>[Editor: yangi loyiha](Editor.md#1-ishga-tushirish-va-launcher) | [<img src="images/editor-asset-inspector.jpg" width="300" alt="Asset inspektori">](Editor.md#7-kontent-brauzer)<br>[Editor: asset inspektori](Editor.md#7-kontent-brauzer) | [<img src="images/scripting-inspector-fields.jpg" width="300" alt="Skript maydonlari">](Scripting.md#4-inspektor-maydonlari-zm_field)<br>[Skript maydonlari](Scripting.md#4-inspektor-maydonlari-zm_field) |
| [<img src="images/scripting-new-script-dialog.jpg" width="300" alt="Yangi skript">](Scripting.md#2-birinchi-skript)<br>[Yangi C++ skript](Scripting.md#2-birinchi-skript) | [<img src="images/components-inspector.jpg" width="300" alt="Komponentlar">](Components.md)<br>[Komponentlar inspektorda](Components.md) | [<img src="images/tutorial-2d-game.jpg" width="300" alt="2D qo'llanma">](Tutorial-2D.md)<br>[2D qo'llanma o'yini](Tutorial-2D.md) |
| [<img src="images/tutorial-3d-game.jpg" width="300" alt="3D qo'llanma">](Tutorial-3D.md)<br>[3D qo'llanma o'yini](Tutorial-3D.md) | [<img src="images/rendering-material-gallery.jpg" width="300" alt="Materiallar">](Rendering.md)<br>[Rendering: materiallar](Rendering.md) | [<img src="images/physics-debug-editor.jpg" width="300" alt="Fizika debug">](Physics.md)<br>[Fizika debug ko'rinishi](Physics.md) |
| [<img src="images/animation-timeline-editor.jpg" width="300" alt="Animatsiya oynasi">](Animation.md)<br>[Animatsiya oynasi](Animation.md) | [<img src="images/ui-layout-editor.jpg" width="300" alt="UI">](UI.md)<br>[UI joylashuvi](UI.md) | [<img src="images/audio-mixer-editor.jpg" width="300" alt="Audio mixer">](Audio.md)<br>[Audio mixer](Audio.md) |
| [<img src="images/scenes-panel-editor.jpg" width="300" alt="Sahnalar oynasi">](Scenes.md#16-editorda-sahnalar)<br>[Sahnalar oynasi](Scenes.md#16-editorda-sahnalar) | [<img src="images/prefab-mode-editor.jpg" width="300" alt="Prefab rejimi">](Prefabs.md#7-prefab-rejimi)<br>[Prefab rejimi](Prefabs.md#7-prefab-rejimi) | [<img src="images/ai-navigation-play.jpg" width="300" alt="Navigatsiya">](AI.md#14-debug-chizish)<br>[Navigatsiya gizmo'si](AI.md#14-debug-chizish) |
| [<img src="images/project-settings-input.jpg" width="300" alt="Loyiha sozlamalari">](ProjectSettings.md#2-loyiha-sozlamalari-oynasi)<br>[Loyiha sozlamalari: Input](ProjectSettings.md#2-loyiha-sozlamalari-oynasi) | [<img src="images/core-splines-editor.jpg" width="300" alt="Spline">](Core.md#6-animationcurve-easing-va-spline)<br>[Spline'lar](Core.md#6-animationcurve-easing-va-spline) | [<img src="images/runtime-game-menu.jpg" width="300" alt="O'yin menyusi">](Runtime.md#10-oyin-menyusi-namunasi)<br>[O'yin menyusi (player)](Runtime.md#10-oyin-menyusi-namunasi) |
| [<img src="images/profiler-play-editor.jpg" width="300" alt="Profiler">](Architecture.md#3-dastur-hayoti-va-asosiy-sikl)<br>[Profiler va asosiy sikl](Architecture.md#3-dastur-hayoti-va-asosiy-sikl) | [<img src="images/export-report-editor.jpg" width="300" alt="Eksport hisoboti">](BuildAndExport.md)<br>[Eksport hisoboti](BuildAndExport.md) | [<img src="images/compile-errors-editor.jpg" width="300" alt="Kompilyatsiya xatolari">](FAQ.md)<br>[Kompilyatsiya xatolari](FAQ.md) |

- [Visual Script — kodsiz 2D/3D gameplay](VisualScripting.md)

- [Ichki C++ kod muharriri](CodeEditor.md)
