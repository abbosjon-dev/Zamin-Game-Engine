# O'zgarishlar tarixi (CHANGELOG)

Zamin Engine'dagi o'zgarishlar [vazifalar rejasining](docs/TASKS.md) milestone'lari bo'yicha: har milestone — 20 ta
vazifa, eng yangisi tepada. Manba — git tarixi: `Tests/changelog.py` commit'larni milestone'larga ajratadi
(`Tests/changelog.py --commits M05` — bitta milestone'ning hamma commit'lari), har bo'lim ostidagi birinchi qator
(sana, vazifalar, commit'lar soni va oralig'i) shu skript chiqargani bilan bir xil bo'lishi kerak — buni
`Tests/docs_check.py` (ctest `DocsCheck`) tekshiradi. Commit sarlavhasi `Mnn` bilan boshlanmasa, u oldingi commit'ning
milestone'iga tegishli; 500 vazifalik rejadan oldingi commit'lar — **M00**, dvigatelning dastlabki versiyasi.

Dvigatel versiyasi: **0.1.0** (`project(Zamin VERSION ...)`, hali reliz qilinmagan). Rejadagi milestone — M25
"Optimallashtirish va sayqal" — [docs/TASKS.md](docs/TASKS.md) da. Yangi milestone'ning
vazifalari bajarila boshlaganda unga bo'lim qo'shing ([CONTRIBUTING.md](CONTRIBUTING.md#9-vazifalar-royxati-va-changelog)).

## M24 Namuna o'yinlar

2026-11-08 · vazifalar 461–480 · `5623496` dan boshlab (so'nggi milestone)

- Yangi namuna loyihalar (har biri o'z `.zproject`, `Assets/`, C++ skriptlar moduli `Source/` bilan, ildiz build'da):
  **Neon Maydon** (`Projects/TopDownShooter`: twin-stick kema, siltanish, 5 xil dushman, to'lqinlar, hisob va kombo,
  bonuslar), **Zamin Breakout** (`Projects/Breakout`: raketka, Box2D to'p fizikasi, oddiy / qattiq / po'lat / oltin /
  bomba g'ishtlar, 6 ta bonus, 6 ta daraja) va **FPS Arena** (`Projects/FPSArena`: sichqoncha bilan qarash, yugurish
  va chidamlilik, sakrash, cho'kkalash, sakratgich, parkur yo'lagi va aylana rekordi).
- Har bir o'yinda: bosh menyu (orqada bot o'ynaydi), pauza menyusi, o'yin tugashi oynasi, SaveData rekordi, loyiha
  input action'lari (klaviatura + sichqoncha + gamepad), o'zbekcha / inglizcha matnlar (lokalizatsiya) va demo rejimi
  (`ZAMIN_DEMO=1` — bot o'ynaydi, jurnalga `Demo: ...` qatorlari yoziladi).
- **FPS Arena**ga raycast bilan otish qo'shildi (Blaster: o'q-dori, qayta o'qlash, tepki, nur izi, uchqunlar), nishonlar
  (turg'un, sirpanuvchi, chiquvchi, uchar dron) va 60 soniyalik "Nishon otish" rejimi (markaz x2, seriya, aniqlik, rekord).
- Yana uchta namuna: **Omborchi** (`Projects/Sokoban`: 3D Sokoban, 10 ta daraja, orqaga qaytarish, darajalar menyusi,
  BFS yechuvchi bot), **Ipak Yo'li** (`Projects/EndlessRunner`: 3D cheksiz yugurish - uch yo'lak, sakrash, sirpanish,
  protsedurali to'siqlar, tangalar, magnit va qalqon, oshib boruvchi tezlik) va **Vodiy Poygasi** (`Projects/Racing`:
  arkada mashina fizikasi - rul, ilashish va drift, aylanma trassa, nazorat nuqtalari, aylana vaqti, 3 ta bot raqib).
- Editor launcher'i namuna o'yinlarni rasmli kartalar bilan ko'rsatadi: `.zproject`ning yangi `Description` va
  `Thumbnail` kalitlari (Project settings > General; `ProjectSerializer::ReadSummary`), rasmlar o'yinlarning haqiqiy
  headless skrinshotlaridan `Tools/samples/make_thumbnails.py` bilan olinadi.
- Assetlarning hammasi kod bilan yaratiladi: `Tools/samples/gen_*.py` (umumiy `samplekit.py`) teksturalar, sprite
  atlaslari, normal map'lar, `.obj` trassa meshlari, WAV tovushlar va musiqani, sahna va loyiha fayllarini yozadi.

## M23 Hujjatlar

2026-11-08 · vazifalar 441–460 · 10 ta commit (`a95a849` … `0ecc1ee`)

- `docs/` fayllari bitta nomlash tartibiga o'tdi (PascalCase: `AI.md`, `Animation.md`, `Core.md`, `Prefabs.md`,
  `ProjectSettings.md`, `Runtime.md`, `Scenes.md`, `Testing.md` ...), barcha havolalar va koddagi izohlar yangilandi.
- Yangi qo'llanmalar: [Scripting](docs/Scripting.md) (`ScriptableEntity`ning barcha ommaviy funksiyalari),
  [Components](docs/Components.md) (79 komponent; maydonlar jadvali `Tests/docs_components.py` bilan manbadan
  yaratiladi), [Editor](docs/Editor.md), [2D](docs/Tutorial-2D.md) va [3D](docs/Tutorial-3D.md) birinchi o'yin
  qo'llanmalari (editorda bajarib, player va eksportda tekshirilgan), [Physics](docs/Physics.md),
  [Rendering](docs/Rendering.md), [UI](docs/UI.md), [Audio](docs/Audio.md), [Architecture](docs/Architecture.md),
  [BuildAndExport](docs/BuildAndExport.md), [FAQ](docs/FAQ.md); [Animation](docs/Animation.md) kengaytirildi.
- API ma'lumotnomasi: `Doxyfile` (natija `build/docs`, CMake'da `docs` target'i) va asosiy header'larda Doxygen
  izohlari (`Application`, `Entity`, `Scene`, `ScriptableEntity`, `SceneManager`, `Renderer2D` / `Renderer3D`,
  `AudioEngine`, `PhysicsWorld3D`, `Project` va asosiy komponentlar).
- Hujjatlar tekshiruvlari: `Tests/docs_check.py` (ctest `DocsCheck`: havolalar va langarlar, komponentlar, skript API,
  player variantlari, FAQ'dagi xabarlar manbada, CMake variantlari, CHANGELOG, hujjatlar indeksi, rasmlar hajmi) va
  `Tests/docs_snippets.py` (ctest `DocsSnippets`: hujjatlardagi to'liq skript klasslari kompilyatsiya bo'ladi).
- Tekshiruvlar topgan xatolar tuzatildi: OGG dekoderi yo'qligi (hujjatlar va `AudioSourceComponent::Path` izohi),
  noto'g'ri namuna sahna nomi, skriptlarda yo'q `GetTransform()`, ogohlantirish beradigan misollar.
- [CONTRIBUTING.md](CONTRIBUTING.md), shu CHANGELOG, [hujjatlar indeksi](docs/README.md), README qisqa bosh sahifa
  bo'ldi (to'liq imkoniyatlar ro'yxati [docs/Features.md](docs/Features.md) ga ko'chdi), qo'llanmalarga headless
  editor / player skrinshotlari.

## M22 Sifat va testlar

2026-11-08 · vazifalar 421–440 · 16 ta commit (`f5c652c` … `9cdfff1`)

- Sanitizer build'lar: `ZAMIN_SANITIZE=address|undefined|leak|thread`, `CMakePresets.json` presetlari,
  `Tests/sanitize.sh` (unit testlar, `--smoke` bilan 3 o'yin va editor). ASan audio mixer'da heap-use-after-free
  topdi (guruh o'chirilganda bolalari), Clang UBSan — miniaudio implementatsiyasi o'z TU'siga ko'chirildi.
- Bizning target'lar `-Wall -Wextra -Wpedantic` bilan GCC (Debug / Release) va Clang'da ogohlantirishsiz.
- Rendering regressiya testi CI'da va mahalliy skript bilan (`Tests/render_tests.sh`), editor smoke testi
  (`ZAMIN_EDITOR_OPEN_ALL_PANELS`), skript hot reload testi, eksport testi (eksport qilingan o'yin skrinshot bilan),
  fizika determinizm testi (Box2D va Jolt bit-bit bir xil), render benchmark (10 000 sprite, 1 000 mesh).
- Test framework: nom bo'yicha filtr (glob, `-` bilan chiqarib tashlash), `--list`, vaqt hisoboti, JSON va JUnit
  hisobotlari, `--fail-fast`. `ZaminBench --ecs` va kengaytirilgan `--serialize`, `Tests/bench.sh --compare`
  (`DestroyEntityDeferred` navbatidagi kvadratik qidiruv tuzatildi).
- Kod qamrovi (`ZAMIN_COVERAGE`, `Tests/coverage.sh`, modullar bo'yicha hisobot), `.clang-format` va
  `Tests/format_check.sh` (faqat o'zgargan qatorlar), CI'da `sanitizers`, `format`, `coverage` job'lari va o'yin
  build'lari artefakti (`Tests/package_games.sh`).
- [docs/Testing.md](docs/Testing.md) (barcha testlar, regressiya testlari katalogi) va
  [docs/Quality.md](docs/Quality.md) (o'lchangan natijalar).
- Tuzatishlar: `Scene::Copy` komponentlarni ierarxiya tartibida nusxalaydi (editor Play'dagi skriptlar tartibi
  eksport qilingan o'yindagidek), o'chib qolgan jarayonlarning skript moduli nusxalari tozalanadi.

## M21 Prefab va serializatsiya II

2026-11-07 – 2026-11-08 · vazifalar 401–420 · 22 ta commit (`1d18e96` … `bf6c809`)

- Prefab nusxalari prefabga bog'langan (`PrefabLinkComponent`): override'lar inspektorda belgilanadi, Apply / Revert
  har darajada, bog'lanishni uzish, ichma-ich prefablar (`PrefabDatabase`, `PrefabUtility`, `PrefabOverrides`).
- Prefab rejimi (alohida tahrirlash: yo'l, Saqlash / Yopish, ichma-ich darajalar), prefab variantlari, kontent
  brauzerda prefab thumbnail'lari, bir nechta prefabni birga sudrab qo'yish.
- `EntityRef` havolalari nusxalash / prefab'da yangi nusxaga moslashadi; sahna formati v5 va migratsiya zanjiri;
  buzilgan sahnani qisman tiklash (`SceneRecovery`).
- Serializatsiya fuzz testi (`SerializationFuzz`), `PrefabCache` (prefab fayli bir marta o'qiladi) va
  `ZaminBench --serialize --prefabs`.
- Prefab hot reload (tashqarida o'zgargan fayl nusxalarga o'tadi), clipboard formati prefab bilan bitta, eksportda
  prefab tekshiruvi (`PrefabExport::Prepare`, R1–R10 qoidalari), [docs/Prefabs.md](docs/Prefabs.md).

## M20 Navigatsiya va AI

2026-11-07 · vazifalar 381–400 · 18 ta commit (`01667d6` … `764a9ab`)

- AI moduli: NavGrid (katak narxi, balandlik, eroziya) va A* (binar heap, qisman yo'llar), yo'lni silliqlash;
  `NavGrid2DComponent` (tilemap va collider'lardan), `NavGrid3DComponent` (nurlar bilan pol), `NavAgentComponent`.
- Dinamik to'siqlar (`NavObstacleComponent`, qayta yo'l), steering xatti-harakatlari va nurlar bilan to'siqdan
  qochish, AI holat mashinasi, `SightSensorComponent` (ko'rish konusi va to'siqlar), `PatrolPathComponent`,
  `FormationComponent` (qator, kolonna, pona, doira), minimal behavior tree.
- Navigatsiya debug chizish (editorda Gizmo'lar > Navigatsiya, player'da `--debug-nav`), nav yo'llari vazifalar
  tizimida parallel, `ZaminBench --nav`, bake natijasini `.znav` faylida saqlash.
- Namunalar: AI3D, Squad3D (Showcase3D), Chase2D (Platformer2D), Adventure3D orol qo'riqchisi;
  [docs/AI.md](docs/AI.md).

## M19 Matematika va core

2026-11-07 · vazifalar 361–380 · 12 ta commit (`ffc14bd` … `70bbb3b`)

- Shovqin moduli (Perlin, Simplex, Value, fBm, ridged, turbulence), `AnimationCurve` turi (skript maydoni, sahna
  fayli, inspektor), spline'lar (`SplineComponent`, `SplineFollowerComponent`, editor gizmo'si), rang utilitalari.
- Matematika yordamchilari (`Remap`, `InverseLerp`, `SmoothDamp`, `MoveTowards`, burchaklar) va kvaternionlar
  (`LookRotation`, `FromToRotation`, `AngleAxis` ...), `Math/Geometry.h` (AABB, sfera, nur, tekislik, kesishishlar).
- Vazifalar tizimi (`JobSystem`, `ParallelFor`; `--jobs`, `ZAMIN_JOBS`) — particle'lar va animator pozalari ishchi
  oqimlarda; xotira statistikasi (`MemoryStats`).
- Log darajalari (har logger alohida, `--log-level`, `ZAMIN_LOG_LEVEL`), xabarli assert'lar (`ZM_ASSERT`,
  `ZM_VERIFY`), `Stopwatch` / `ScopedTimer`, `StringUtils`, fayl tizimi yordamchilari, `Scene::ScreenPointToRay` va
  skriptda `GetMouseRay`; Splines namuna sahnasi, [docs/Core.md](docs/Core.md).

## M18 Runtime player

2026-11-07 · vazifalar 341–360 · 12 ta commit (`1fb9950` … `16cedfc`)

- Player oyna sozlamalari ustunlik tartibi bilan (buyruq qatori > o'yinchi saqlagani > loyiha;
  `PlayerLaunchSettings::Resolve`), F11 / Alt+Enter to'liq ekran, FPS cheklovi (`FramePacer`, `--fps`), splash ekran,
  buyruq qatori (`--help`).
- Player log fayli (`LogFile`, `latest.log`, `--log-file`), crash handler (`CrashHandler`: signal'lar, hisobot,
  oyna), skriptlar uchun `Cursor` API, oyna o'lchami o'zgarishi va letterbox, fokus yo'qolganda pauza, gamma va
  yorqinlik.
- Eksport: ishlatilmagan assetlar chiqarib tashlanadi (`ExportAssets::Plan`), skript moduli Release'da va strip
  qilingan, ko'chiriladigan `.desktop` va 16…256 px ikonkalar, eksport hisoboti oynasi (`export-report.json` / `.txt`).
- Headless ishlash testi (`--benchmark N`), avtomatik eksport testi, o'yin menyusi namunasi
  (`Prefabs/GameMenu.zprefab`), versiya ma'lumoti, [docs/Runtime.md](docs/Runtime.md).

## M17 Loyiha sozlamalari

2026-11-07 · vazifalar 321–340 · 10 ta commit (`d845846` … `d72dd56`)

- Loyiha sozlamalari bo'limlarga ajratilgan oyna (qidiruv bilan): input xaritasi, teglar va qatlamlar, fizika
  qatlamlari presetlari, sifat darajalari (Low … Ultra), player oynasi, o'yin ikonkasi, kompaniya / mahsulot / versiya,
  boshlang'ich va build sahnalar, versiyali `.zproject`.
- Grafik kontekst sozlamalari (OpenGL versiyasi, MSAA, sRGB, debug kontekst; `--gl`, `--msaa`, `--gl-debug`,
  `--srgb`), vaqt sozlamalari va `Time` API, standart gravitatsiya, audio standartlari.
- Lokalizatsiya: `Assets/Localization/<til>.json` (ichma-ich kalitlar, ko'plik shakllari), skript API, matn
  komponentida kalit, editorda jadval; Platformer2D uch tilda.
- Editor interfeysi tili (o'zbek / ingliz, `EditorText::Tr`), [docs/ProjectSettings.md](docs/ProjectSettings.md).

## M16 Editor II

2026-11-06 – 2026-11-07 · vazifalar 301–320 · 9 ta commit (`4195bb4` … `85f8c51`)

- O'yin rejimida editor rang tusi, game view o'lchami preset'lari (letterbox), konsolda qidiruv (matn / regex) va bir
  xil xabarlarni guruhlash, Sozlamalar oynasi (asosiy rang, shrift o'lchami, kamera standartlari).
- Klaviatura yorliqlari jadvali (qayta bog'lash, to'qnashuvlar), avtomatik saqlash va tiklash, so'nggi sahnalar,
  undo tarixi oynasi va tanlovni undo qilish.
- Oyna joylashuvlari (Standart, 2D, Animatsiya, 4 ko'rinish va o'zingiznikilar), qo'shimcha sahna ko'rinishlari,
  ortografik o'q ko'rinishlari (numpad), editor kamerasi sozlamalari, viewport'da obyekt nomlari.
- Sahna statistikasi oynasi, kodni tashqi muharrirda ochish, kompilyatsiya xatolari oynasi (GCC / Clang / linker /
  CMake chiqishi ajratiladi), bo'sh turganda kadrlarni kamaytirish.

## M15 Sahna boshqaruvi

2026-11-06 · vazifalar 281–300 · 15 ta commit (`8e40f55` … `58cc9bc`)

- `SceneManager`: additive sahnalar, `DontDestroyOnLoad`, fonda yuklash (`LoadSceneAsync`), so'nish bilan o'tishlar;
  build sahnalar ro'yxati va indeks bo'yicha yuklash (`LoadNextScene`, `ReloadScene`), player `--scene <indeks>`.
- Sahna hodisalari (`OnSceneLoaded` / `OnSceneUnloaded` / `OnActiveSceneChanged`), nom / teg indekslari, yuklash
  xatolari oynasi, barqaror fayl tartibi, sahna formati versiyasi va migratsiya.
- Sahna metadata'si (ta'rif, muallif, teglar, rasm) va editorda Sahnalar oynasi; global holat (`GameState`) va
  servislar (`Services`); yuklash vaqti bosqichlari bo'yicha profiler'da.
- Katta sahnalar uchun tezroq serializatsiya va binar kesh (`.zsceneb`), sub-sahnalar (`SubSceneComponent`);
  Platformer2D'da menyu → yuklanish ekrani → daraja oqimi, [docs/Scenes.md](docs/Scenes.md).

## M14 Particle II

2026-11-06 · vazifalar 261–280 · 7 ta commit (`e09b523` … `9abd564`)

- Particle simulyatsiyasi alohida modulda (`ParticleSystem`): ko'p nuqtali rang gradienti, o'lcham egri chizig'i,
  umr bo'yi tezlik, tekstura sheet animatsiyasi, konus / qirra shakllari.
- Mesh sirtidan chiqarish, sub-emitter'lar, to'qnashuv, shovqin (turbulence), izlar (trails).
- GPU instancing, shaffof zarralarni saralash, prewarm, portlashlar jadvali, simulyatsiya tezligi / davomiyligi /
  kechikishi / to'xtash amali.
- Tayyor effektlar kutubxonasi (`.zparticle`), kamera atrofidagi ob-havo (yomg'ir, qor), inspektor modullari;
  ParticlesII–IV namuna sahnalari.

## M13 Animatsiya II

2026-11-06 · vazifalar 241–260 · 16 ta commit (`009d765` … `6c68a90`)

- Property animatsiya kliplari (`.zanim`) va `AnimationPlayerComponent`, animatsiya event'lari (`OnAnimationEvent`),
  root motion; Animatsiya oynasi (timeline, dope sheet, egri chiziqlar, yozish rejimi).
- Animator: 1D blend tree, qatlamlar (avatar maska, override / additive), Animator grafi oynasi, inspektorda jonli
  holat; sprite animatsiya kliplari (`.zsanim`).
- Skelet klipini editorda ko'rib chiqish (scrub), `BoneAttachmentComponent`, `FootstepsComponent`, kliplarni kesish
  va bo'lish, glTF morph target'lar, bir glTF'da bir nechta skin.
- Animatsiya keshlash (poza keshi, ekrandan tashqarida kesish), testlar, Adventure3D darvoza namunasi,
  [docs/Animation.md](docs/Animation.md).

## M12 UI tizimi

2026-11-06 · vazifalar 221–240 · 14 ta commit (`59d32a6` … `dbc2359`)

- UI vidjetlari: Image, Slider, Toggle, Progress Bar, Input Field; UI ierarxiyasi, stretch langarlar va pivot, Canvas
  masshtabi, layout guruhlari (qator / ustun / panjara).
- UI hodisalari (`OnPointerEnter` / `Exit` / `Down` / `Up` / `Click`), klaviatura va gamepad navigatsiyasi.
- Matn: o'lcham bo'yicha shrift atlaslari, tekislash, so'z bo'yicha o'rash, sig'maslik rejimlari; UI animatsiyalari
  (`TweenUI`, `UIScaleTo`, `UIFadeTo` ...).
- Editorda UI to'rtburchagi bo'yicha tanlash va rect gizmo'si; UI Mask va Scroll View; pauza menyusi namunasi va o'yin
  vaqti masshtabi.

## M11 Audio

2026-11-06 · vazifalar 201–220 · 13 ta commit (`b7e1206` … `adeed7d`)

- Audio mixer guruhlari (Master / Music / SFX / UI va loyihaniki: fader, mute, solo, daraja o'lchagich), Audio Mixer
  oynasi, `AudioListenerComponent`, musiqa crossfade, streaming.
- Balandlik / ton variatsiyasi, ovozlar limiti (ustuvorlik), Audio Source skript API, Doppler effekti, audio clip
  import sozlamalari, so'nish (fade), filtrlar.
- Audio profiler, asset inspektorida to'lqin shakli (zoom, scrub, takrorlash oralig'i), 3D ovoz uchun yo'nalish
  konusi, `AmbientZoneComponent`, `OnSoundFinished` callback'i; Audio namuna sahnasi.

## M10 3D fizika II

2026-11-05 – 2026-11-06 · vazifalar 181–200 · 15 ta commit (`c92e002` … `a06d581`)

- 3D joint'lar: Fixed, Hinge, Distance, Slider, Cone (uzilish va `OnJointBreak3D`), langarlarni viewport'da tahrirlash.
- Mesh Collider 3D, 3D to'qnashuv qatlamlari (2D bilan umumiy matritsa), overlap va shape cast so'rovlari
  (`Raycast3DAll`, `SphereCast3D` ...), CCD, 3D uxlash API, 3D fizik materiallar (`.zphysmat`).
- Fizika debug ko'rinishi: kontakt nuqtalari (normal, chuqurlik, impuls), joint'lar, tezliklar.
- Namunalar: Queries3D, Physics3D, Ragdoll3D, PhysicsDebug3D, Joints3D.

## M09 2D fizika II

2026-11-05 · vazifalar 161–180 · 16 ta commit (`4208631` … `262f0a0`)

- 2D fizika `PhysicsWorld2D` ga ajratildi; 2D joint'lar (Distance, Revolute, Prismatic, Weld, Wheel), uzilish va
  `OnJointBreak`.
- Polygon / Edge Collider 2D (viewport'da nuqtalarni tahrirlash), fizik material asset (`.zphysmat`), 2D to'qnashuv
  qatlamlari, Platform / Area / Buoyancy Effector 2D.
- `OverlapCircle2D` / `OverlapBox2D` / `OverlapPoint2D`, `Raycast2DAll`; Box2D sub-qadamlari va uxlash sozlamalari.
- Namunalar: Joints2D, Shapes2D, Effectors2D, Ragdoll2D.

## M08 2D yaxshilanishlar

2026-11-05 · vazifalar 141–160 · 17 ta commit (`4c05130` … `c1a688c`)

- Sprite muharriri va sprite atlas (nomlangan kadrlar `.zmeta`da), 9-slice va plitkali sprite'lar, saralash
  qatlamlari, parallax.
- `Camera2DFollowComponent` va kamera silkinishi (`ShakeCamera`), tilemap qatlamlari, auto-tile qoidalari, chizish
  asboblari va palitra, plitka xususiyatlari (collision, bir tomonlama, animatsiyali plitkalar).
- Sprite chaqnashi va konturi, rang tint animatsiyasi (`TintAnimationComponent`), 2D yorug'lik (`Light2DComponent`),
  pixel-perfect kamera, Y bo'yicha saralash; Tilemap2D va TopDown2D namunalari.

## M07 Debug draw va vizualizatsiya

2026-11-05 · vazifalar 121–140 · 8 ta commit (`5deeafd` … `6418441`)

- `DebugDraw` API (chiziq, nur, o'q, quti, sfera, aylana, yo'l, matn; rang, davomiylik, chuqurlik testi).
- Editor gizmo'lari: kamera frustumi, yorug'lik yo'nalishi, 3D audio masofalari, gizmo o'lchami, shader asosidagi
  tanlash konturi, chegaralar (bounding box), skelet suyaklari, verteks normallari.
- Viewport statistika overlay'i (FPS grafigi, kadr vaqti, draw call'lar, culling).

## M06 Rendering I

2026-11-05 · vazifalar 101–120 · 7 ta commit (`a9e3ae6` … `df7b5ae`)

- Spot yorug'lik, 32 tagacha nuqtali + spot yorug'lik, FXAA, render masshtabi, avto ekspozitsiya.
- Wireframe va debug ko'rinishlar (faqat yorug'lik, normallar, albedo, roughness, metallic), mesh va 2D frustum
  culling, GPU instancing, render statistikasi, soya sifati sozlamalari.
- Bir nechta quyosh, osmon gradienti va quyosh diski, xromatik aberratsiya, plyonka donasi, bloom rangi va linza
  changi; rendering regressiya testi (`ZaminRenderTests`).

## M05 Materiallar (asset)

2026-11-05 · vazifalar 081–100 · 9 ta commit (`ebb68c7` … `7210b63`)

- Material assetlari (`.zmat`: shader, aralashtirish, alfa chegarasi, ikki tomonlama, PBR qiymatlari, teksturalar
  UUID bilan), `MaterialAsset` va `MaterialLibrary` (kesh, joyida qayta yuklash).
- Sirt turlari: shaffof (orqadan oldinga saralash), cutout, ikki tomonlama; shaffoflar ustuvorligi.
- Materialni viewport / ierarxiyaga sudrab qo'yish, kontent brauzerda yangi material, glTF materiallarini `.zmat`ga
  eksport, skriptda material nusxalari (`GetMaterial` / `GetSharedMaterial`), standart materiallar kutubxonasi.

## M04 Asset tizimi

2026-11-05 · vazifalar 061–080 · 16 ta commit (`847d99e` … `c8a6ee0`)

- Asset moduli: UUID'li `AssetHandle`, `.zmeta` fayllar (UUID, tur, import sozlamalari), `AssetRegistry`, fayl
  kuzatuvchi (inotify) va rasm / shrift / HDRI / model / materialni joyida qayta yuklash.
- Sahna va prefab fayllarida asset havolalari UUID bilan (ko'chirilgan fayl topiladi), registry keshi
  (`.zamin/AssetRegistry.json`, eksportda `AssetRegistry.zreg`).
- Asset inspektori (rasm, model, audio ko'rinishi va import sozlamalari), ishlatilgan joylar indeksi, ishlatilmagan
  assetlar, o'chirishdan oldin ogohlantirish, import xatolari (`AssetImportLog`).
- Kontent brauzerda to'liq yo'l bo'yicha qidiruv va thumbnail keshi.

## M03 Faol/nofaol obyektlar

2026-11-04 – 2026-11-05 · vazifalar 041–060 · 5 ta commit (`3c48c13` … `b8f593a`)

- `TagComponent::Active` va ierarxiya bo'yicha faollik: nofaol obyekt chizilmaydi, fizikadan chiqariladi, skripti
  `OnDisable` oladi; skriptlarda `SetActive`, `IsActive`, `IsActiveInHierarchy`, `IsActiveAndEnabled`.
- Nofaol obyektlarda particle, audio, Animator, UI tugma, kamera, yorug'lik, tilemap collision va Character
  Controller to'xtaydi; prefab nusxalari faollik holatini saqlaydi; editorda `Alt+Shift+A`.

## M02 Editor UX I

2026-11-04 · vazifalar 021–040 · 5 ta commit (`224ea5a` … `42d17b3`)

- Ko'p obyekt tanlash (`Ctrl` / `Shift`), ierarxiyada sudrab tartiblash, Transform menyusi (tiklash / nusxalash /
  joylash).
- Snapping va grid sozlamalari, kamera xatcho'plari (`Ctrl+1..5` / `1..5`), `Shift+F`, `Alt`+sudrash bilan nusxa.
- Inspektorda komponentlar tartibi va nusxalash / joylash, ierarxiyada ko'z va qulf (`H` / `Alt+H`).
- Kontent brauzer: papkaga sudrab ko'chirish (havolalar yangilanadi), ro'yxat ko'rinishi, tur filtri, tashqaridan
  import; status bar.

## M01 Skript va gameplay API

2026-11-04 · vazifalar 001–020 · 6 ta commit (`12d1b95` … `dda0c61`)

- 500 vazifalik reja ([docs/TASKS.md](docs/TASKS.md)); `EntityRef` maydonlari va inspektori, taymerlar (`Invoke`,
  `InvokeRepeating`) va tween'lar.
- Input harakatlari xaritasi (loyiha sozlamalarida), `Input::IsActionPressed` / `IsActionJustPressed` / `GetAxis`,
  kadrlar orasidagi tugma holati, `SaveData`.
- `Random` API (PCG32), teglar, xabarlar va event bus (`SendMessage`, `Broadcast`, `Emit`), ierarxiya so'rovlari,
  `FindEntity` keshi.
- Skript hayot sikli (`OnEnable` / `OnStart` / `OnDisable`), `OnTriggerBegin` / `OnTriggerEnd`, maydon atributlari
  (`ZM_FIELD_ENUM`, `ZM_FIELD_COLOR`, `ZM_FIELD_RANGE`, `.Tooltip`), yangi skript shablonlari.

## M00 Dastlabki versiya

2026-11-04 · 13 ta commit (`62eaca8` … `1c8492e`)

- Linux uchun birinchi to'liq versiya: editor, 2D / 3D renderer, 2D fizika (Box2D), audio, C++ skriptlar va hot
  reload, eksport.
- Ierarxiya, prefab'lar, Undo / Redo, 3D fizika (Jolt), sprite animatsiya; particle tizimi, post-processing (bloom),
  ekran UI va tugmalar.
- HDR pipeline, glTF modellar, normal map'lar, tilemap, 3D ovoz; skelet animatsiya, Animator, CPU profiler.
- Character Controller 3D, animatsiya crossfade va Adventure3D o'yini; cascaded shadow maps va tuman, SSAO, animatsiya
  holat mashinasi, HDRI osmon va IBL.
