# Zamin Engine — 500 ta vazifa rejasi

Dastlabki M01–M25 milestone'lari 20 tadan vazifa tutadi. `[x]` — bajarilgan, `[ ]` — rejada.
Quyida yangi 2D/3D mahsulot bosqichlari ham alohida yuritiladi.

## Faol yo'nalish: professional 2D va 3D engine

2026-11-09: maqsad — 2D va 3D birinchi darajali bo'lgan umumiy engine. Unreal darajasidagi kenglik,
ishonchlilik va ishlab chiqish qulayligi uzoq muddatli nishon; hozir bu tenglikka erishilgani da'vo qilinmaydi.
Quyidagi bosqichlar mahsulotga tayyor natija va tekshiruv mezoni bilan yuritiladi. Pastdagi dastlabki 500 vazifa
saqlanadi; ulardagi belgi yangi imkoniyatning ishlayotganiga dalil o'rnini bosmaydi.

| Bosqich | Natija | Tugallash mezoni | Holat |
|---|---|---|---|
| P0 Ishonchli asos | Atomik save, tiklash, xavfsiz build, chegaralangan Undo, CPU profili, ajratilgan runtime | Save xatosida ma'lumot yo'qolmasligi, zararli papka nomi bajarilmasligi, UI'siz Player, SSE2 sinovi | Auditdagi 7 band bajarilgan; boshlang'ich dalil 645 unit / 12 render / 265 editor qadam |
| P1 2D/3D katta xarita render asosi | Terrain patch LOD, yoriqsiz qo'shni chegaralar, qisman GPU mesh yangilash; tilemap ierarxik culling | Geometriya kamayishi, LOD o'tish/hysteresis, collision aniqligi, 2D ko'rinish va chizish tartibi oracle bilan mos | Bajarildi (2026-11-10): 656 unit / 12 render / 265 editor qadam; o‘lchovlar quyida |
| P2 Kontent sifati va mualliflik | Terrain GPU qatlamlari/PBR, normal/roughness, atlas/cook, foliage, 2D palette/animation qulayligi | Haqiqiy assetli 2D/3D sahnalar, import/export/undo roundtrip, tekstura xotira byudjeti | GPU terrain v1 yakunlandi; qolgan kontent ishlari rejada |
| P3 Dunyo va asset streaming | Disk kataklari, async load/unload, data layers, HLOD, floating origin, asset residency | Tez harakatda/cancel/reload'da yo'qolmagan obyektlar, chegaralangan RAM/VRAM, main thread'da og'ir IO yo'q | Runtime v2: LRU cleanup, observed pressure gate, terrain tiles va grid mosaic bajarildi; HLOD/floating origin/hard timing rejada |
| P4 Rendererning keyingi avlodi | GPU timer v1 mavjud; render graph, Vulkan, temporal AA/upscaling, katta sahna visibility va yoritish | Drayver/physical GPU matritsasi, HDR/alpha/picking/shadow regressiyalari, reference hardware byudjeti | Rejada; virtual geometry va dinamik GI — alohida tadqiqot |
| P5 Gameplay, animatsiya, fizika va UI | Mavjud 2D/3D tizimlarini production darajasida birlashtirish, katta kontent va gameplay workflow | Ikki tugallangan vertical slice, saqlash/yuklash, controller, animatsiya/fizika stress, UI input/scale testlari | Visual Script va C++ muharriri v1 qo'shildi; qolgan ishlar rejada |
| P6 Tarmoq va kengaytirish | Server authority, replication, latency compensation, plagin SDK/ABI, xavfsiz hot reload | Yo'qotilgan paket, kechikish, reconnect, versiya mos kelmasligi va plagin unload sinovlari | Rejada |
| P7 Platforma, release va mahsulot | Linux/Windows, keyin macOS; cook/package/update/crash telemetry; editor/launcher yagona auth | Toza mashinada install/export/run, talablar va litsenziyalar, imzolangan release va rollback sinovi | Rejada |
| P8 Yakuniy sifat darvozasi | Hujjatlar, ko'chirish yo'li, ikki namunaviy o'yin va profiling hisobotlari | Uzoq sessiya, resurs sizishi, recovery/fuzz, reference sahnalarda barqaror frame time va foydalanish sinovi | Rejada |

### Yakunlangan katta paket — dynamic obstacle va bounded recovery

- [x] Same-frame world atlas publication after carving; synthetic mosaic excluded from carve feedback.
- [x] Recovery watchdog: real progress, pending/portal pause, bounded retry/cooldown/exhaustion and reset.
- [x] Serialize/copy/reset, inspector and Blueprint recovery diagnostics/manual retry.
- [x] Live barrier 2D/3D demos, portal/async/physics regression, sanitizer, screenshots and native exports.
- [x] CI failure audit, verified evidence, upstream check and main push.

Mahalliy dalillar (2026-11-11): 880/880 unit (19 yangi), 138/138 focused, 11/11 headless CTest,
12/12 render, 490/490 editor smoke, OpenGL 4.1 residency GPU. Clang 18 syntax-only -Werror: 14 ta TU.
NavRecovery leaf ASan/UBSan: 10/10. Recovery, source carving, world movement va world service TU’lari
hamda focused testlar instrumentlangan owner probe: 138/138; qolgan shared engine normal build,
bu butun engine sanitizer run’i emas. 2D/3D Player 420 kadrdan; editor 160 kadrdan, 8-node/9-link/
4-variable graph va yuqorida jonli Exhausted/Phase/Retries. Ikkala native export 420 kadr, oracle,
PNG va relocation’dan o‘tdi. Oldingi NavAgent/portal Player regressiyasi 420, crowd 450 kadrdan
ikkala o‘lchamda o‘tdi. Dalillar: `build-review/RecoveryNavigationPackage/`.

763a7b0 remote coverage dependency bosqichida Azure va official Ubuntu mirror DNS xatosi bilan
to‘xtagan; bu C++ test failure’i emas. Installer cached paketlarni qayta o‘rnatmaydi, ikkala DNS
ishlamasa tezda aniq xabar beradi va butun update/install command’lari timeout bilan chegaralangan.
Mock regression real apt/sudo yoki host mirror fayllarini o‘zgartirmaydi. Yangi remote CI push’dan
keyin alohida kuzatiladi; tashqi DNS ishlashiga kod kafolat bermaydi.

Chegaralar: opt-in recovery; physics-only to‘siq olib tashlansa explicit retry, NavObstacle/topology
o‘zgarsa avtomatik reset. Atlas build owner’da, hard frame deadline/hardware profiling yo‘q.
Keyingi paket: animation locomotion va gameplay vertical slice; katta scene uchun profiler bilan
aniq acceptance, streaming/query vaqtlarini o‘lchash.

### Yakunlangan katta paket — world smoothing va crowd steering

- [x] Sparse atlas segment clearance: holes, corners, floor/climb, bounded work.
- [x] Worker-side bounded smoothing; weighted costs and explicit portal endpoints preserved.
- [x] Deterministic snapshot crowd steering, spatial broadphase, nearest-neighbour and candidate caps.
- [x] World NavAgent integration, conservative movement clipping, serialized settings, inspector and graph telemetry.
- [x] 2D/3D crowd demos, regression/sanitizer/runtime/export screenshots, upstream check and main push.

Mahalliy dalillar (2026-11-11): 861/861 unit (22 yangi), 119/119 focused, 10/10 headless CTest,
12/12 render va 490/490 editor smoke qadam; OpenGL 4.1 residency GPU. Clang 18 syntax-only -Werror:
14 ta TU. Crowd/atlas/segment/grid leaf ASan/UBSan: 17/17, butun engine sanitizer run’i emas.
2D/3D crowd Player 450 kadrdan va editor 130 kadrdan: 4 agent, ikki manzil, Stop/reverse, graph telemetry,
separation; 5-node/6-link/3-variable graph. Ikkala native export 450 kadr, oracle, PNG va relocation’dan o‘tdi.
Oldingi NavAgent/portal/unload/return Player regressiyasi ikkala o‘lchamda 420 kadrdan o‘tdi.
Dalillar: `build-review/CrowdNavigationPackage/`. Software Mesa tekshiruvi; hardware performance o‘lchanmagan.

51eead3 remote CI dependency bosqichida Azure Ubuntu mirror DNS xatosi sabab to‘xtagan, C++ tekshiruvi
o‘sha run’da bajarilmagan. Shared bounded apt retry/timeout va official archive fallback qo‘shildi;
workflow YAML/bash syntax tekshirildi. Yangi remote CI holati push’dan keyin kuzatiladi.

Chegaralar: conservative swept rectangle va bounded string pulling, world spline emas. Crowd velocity
sampling, ORCA yoki collision kafolati emas; physics yakuniy vakolatga ega. Dense overflow stop/diagnostic
bilan ko‘rinadi. Keyingi paket: dynamic obstacle/repath recovery, ko‘p agentli profiling va animation locomotion.

### Yakunlangan katta paket — bounded async world navigation

- [x] CPU-only worker queue, immutable snapshot lifetime, FIFO start/publication budgets.
- [x] Active cancellation, replacement, retained snapshot/result byte budgets, bounded shutdown.
- [x] NavAgent async admission, pending stop, stale destination/start/revision rejection, cancel/unload/destruction.
- [x] Policy serialization, editor queue diagnostics, real 2D/3D graph/controller demos.
- [x] Concurrency/regression, sanitizer, screenshots, native export va main push.


Mahalliy dalillar (2026-11-11): 839/839 unit (19 yangi), 97/97 yo‘naltirilgan test,
10/10 headless CTest, 12/12 render, 490/490 editor smoke qadam, OpenGL 4.1 residency GPU.
Clang 18 syntax-only -Werror: 9 ta o‘zgargan TU. Queue/atlas/grid va queue testlari to‘liq
instrumentlangan ASan/UBSan leaf probe: 10/10; bu butun engine sanitizer run’i emas.
2D/3D Player 420 kadrdan, editor 120 kadrdan 13-node/16-link/3-variable graph bilan o‘tdi.
Ikkala native export 420 kadr, async agent oracle, screenshot va relocation’dan o‘tdi.
Oldingi 2c8585c CI barcha tekshiruvlarda yashil. Dalillar: `build-review/AsyncNavigationPackage/`.

Chegaralar: scratch allocator/RSS va qattiq frame-time deadline bu payload byudjetlariga kirmaydi;
atlas build owner thread’da. Harakat sequential corner tracking; local avoidance, yo‘lni silliqlash,
jump/elevator animatsiyasi va umumiy job scheduler keyingi paketlarga qoladi.
Keyingi paket: collision saqlangan holda route smoothing va ko‘p agentli local avoidance.

### Yakunlangan katta paket — NavAgent, controller va Visual Graph traversal

- [x] NavAgent opt-in sparse atlas, bounded query, actual-position corner tracking, stale replan.
- [x] Character controller collision, safe exit occupancy, revision/link completion token va cancel.
- [x] Serialize/copy/reset, C++ API, inspector runtime holati.
- [x] Visual Graph world destination, traversal state/event, complete/cancel va success outputs.
- [x] 2D/3D haqiqiy agent/graph demo, runtime screenshot, regression va native export.
- [x] Test dalillari, hujjatlar, upstream merge tekshiruvi va main push.

7d12f4a remote CI: format, GCC, Clang, ASan, UBSan va coverage to‘liq yashil.

Mahalliy dalillar (2026-11-11): 820/820 unit (10 yangi), 10/10 headless CTest, 12/12 render,
490/490 editor smoke qadam, GL 4.1 residency GPU; Clang 18 syntax-only -Werror 12 fayl.
Agent/VM TU va testlar instrumentlangan targeted ASan/UBSan probe: 78/78; shared engine normal
build, bu to‘liq engine sanitizer run’i emas. Fixed capture 30 bilan 2D/3D Player 420 kadrdan uchta
haqiqiy traversal/arrival, disable/unload/return; har editor 120 kadrdan 10-node/14-link graph preview.
Ikkala native export ham 420 kadr, agent/graph oracle, screenshot va relocation’dan o‘tdi.
Tarjima lug‘atidagi yangi inspector keys va float overflow oldidan mode mutatsiyasi regressiyasi tuzatildi.
Dalillar: `build-review/AgentTraversalPackage/`. Yangi remote CI push’dan keyin alohida kuzatiladi.

Scope: opt-in sequential world queries, per-query expansion cap; corner route. Character exit
occupancy checked; Rigidbody traversal completion, world local avoidance/smoothing, jump/elevator
animation va async query scheduling keyingi ishlar. Transform/controller BaseOffset farqi hisobga olingan.


### Yakunlangan katta paket — sparse navigation va traversal workflow

- [x] Faqat yuklangan tile hajmiga bog‘liq immutable atlas; bounded va deterministik route query.
- [x] Ikki/uch o‘lcham: tile chegarasi, floor/climb, cost, carve va partial path qoidalari.
- [x] Yo‘nalishli, narxli, o‘chiriladigan traversal link; yetishmayotgan endpoint xavfsiz inactive.
- [x] Route follower: link oldida to‘xtash, explicit completion, stale topology’da stop/replan.
- [x] World runtime query/API, editor route diagnostikasi, haqiqiy 2D/3D demo va screenshot.
- [x] Regression, build, export, hujjatlar va main push.

Oldingi 73a683c remote CI: GCC, Clang, ASan, UBSan, coverage va format to‘liq muvaffaqiyatli.

Dalillar (2026-11-11): 810/810 unit (18 yangi), 10/10 headless CTest, 12/12 reference render,
490/490 editor smoke qadam, GL 4.1 residency GPU oracle; Clang 18 syntax-only -Werror 7 fayl.
Atlas/grid/pathfinder/mosaic/follower uchun alohida instrumentlangan ASan/UBSan 16/16 test;
bu to‘liq engine sanitizer run’i emas. 2D/3D Player 420 kadrdan portal/disable/stale/unload/return,
ikkala editor 120 kadrdan route preview. Oldingi camera tour’lar 660 kadrdan (har birida 6 complete
cross-cell path), checkpoint 540 kadrdan edit/delete/adopt/unload/return/restore. Ikkala native export
420 kadrdan navigation oracle, screenshot va relocation’dan o‘tdi. Dalillar: `build-review/NavigationPackage/`.

Snapshot build sinxron va bounded; incremental bake hali keyingi ish. Traversal follower kinematic;
character collision/animation caller’da, eski NavAgent dense mosaic’dan foydalanadi. Keyingi navigation
integratsiyasi: NavAgent/character controller va Visual Graph traversal tugunlari. Polygon navmesh alohida.


### Yakunlangan yirik paket — world authoring va checkpoint workflow

- [x] Xavfsiz checkpoint format: bounded cell snapshots, world identity, atomic save, buzilgan faylda mutatsiya yo‘q.
- [x] Runtime: unload/return state, deleted/spawned entity, ownership API, root local/world koordinatalari.
- [x] Asinxron restore: snapshot JSON worker’da parse, eski avlodni cancel, bosqichma-bosqich world reset.
- [x] Mualliflik: yangi 2D/3D grid bundle, terrain/nav/camera, unique UUID, no-overwrite transactional publish.
- [x] Editor: world wizard, runtime entity edit/adopt/delete, checkpoint save/restore/reset va status.
- [x] Demo: F5/F9, qayta kirishda state va disk restore oracle, haqiqiy screenshot.
- [x] Regressiyalar, ASan stack tahlili, export, hujjatlar, main push.

Serialized component state saqlanadi; arbitrary C++ stack/heap va fizik replay checkpoint emas.

Dalillar (2026-11-11): 792/792 unit (20 yangi), 10/10 headless CTest, 12/12 render reference,
490/490 editor smoke qadam. 2D/3D Player’da 540 kadrdan edit/delete/adopt/unload/return/disk restore oracle;
ikkala editor’da 6-cell wizard, no-overwrite, project link, generated Play va actor restore; generated 3D
loyiha native export/run/screenshot/relocation. Checkpoint store/writer uchun alohida instrumentlangan
ASan/UBSan probe’da 6/6 test (to‘liq engine sanitizer run’i emas), Clang 18 syntax-only -Werror 13 fayl.
Oldingi CI 625d750 GCC/Clang/UBSan/coverage’dan o‘tgan; ASan’dagi Mesa 2×56-byte drawArrays JIT retention
artifact stack bo‘yicha tor vendor suppression bilan ajratildi. Yangi remote CI push’dan so‘ng alohida kuzatiladi.
Dalillar: `build-review/CheckpointPackage/`. Serialized component snapshot arbitrary C++ heap/physics replay emas;
restore validation va capture CPU qismi hali hard frame deadline bermaydi.

### Yakunlangan katta paket — residency, terrain va navigation integratsiyasi

- [x] CI: barcha CMake module output joylarida ishlaydigan camera tour; vendor leak istisnosini tekshirish.
- [x] Resurslar: texture/model/material/mesh LRU, jonli reference va async load pin, chegaralangan tozalash.
- [x] Xotira: haqiqiy RSS va tracked GPU allocation alohida; high/low watermark bilan yangi admission nazorati.
- [x] Terrain: world-space generator, turli resolution chegaralarini ulash va tashqi patch LOD himoyasi.
- [x] Navigation: yuklangan katak gridlarini bounded mosaic orqali ulash, teshik va balandlik qoidalari.
- [x] Editor/runtime: cache va pressure diagnostikasi, qo‘lda trim, yangilangan demo va haqiqiy screenshot.
- [x] Sinov: cancellation/pinning/pressure/seam/path regressiyalari, build/export, main push.

Bu paket physical VRAM hard cap, polygon navmesh yoki Unreal bilan tenglik da’vosi emas.

Tekshiruv (2026-11-10): 772/772 unit, barcha 10 headless CTest, 12/12 render reference;
OpenGL 4.1 real texture/model/material/mesh residency va 32 cycle; 3D/2D camera tour 660 kadrdan,
har birida 6 ta cross-cell complete path; terrain pair editor Undo/Redo; 490/490 editor smoke qadam;
ikki demo native export, screenshot va relocation. Clang 18 syntax-only `-Werror` tekshiruvi ham o‘tdi.
Qayta yuklashdagi asset resolve cursor tiklanmasligi tuzatildi; entity soni 3→1→4→2→0→3 bilan sinaldi.
Qo‘lda va avtomatik trim bitta world tick allowance’ini bo‘lishadi; thumbnail/reimport LRU metadata’si ham tozalanadi.
Dalillar: `build-review/ResidencyPackage/Final/`. Remote compiler/ASan matritsasi push’dan keyingi CI’da alohida tekshiriladi.

Keyingi P3 ishlar: incremental nav bake, terrain normal seam, HLOD, floating origin va versioned gameplay/physics persistence. Serialized cell checkpoint workflow bajarildi.

### Yakunlangan paket — packed foliage GPU culling va LOD

- [x] Bitta komponentda 65536 tagacha statik instance, ikki model/mesh LOD, tint va ko'rish masofasi.
- [x] OpenGL 4.3 compute + SSBO compaction + indirect draw; OpenGL 4.1 CPU culling/instancing fallback.
- [x] LOD hysteresis, konservativ birlashgan bounds, kamera harakatida statik input qayta yuklanmasligi.
- [x] Asinxron GPU visibility hisoblari va CPU/GPU buffer hajmi; rendering yo'lida blocking readback yo'q.
- [x] Scene/prefab/copy/undo serialization, model UUID havolalari, inspektor va parent terrain sirtiga joylash.
- [x] CPU/GPU image/count oracle, shadow/picking/wire va ikkala capability holatini yakuniy tekshirish.
- [x] Zich demo sahna, screenshot, eksport va regressiyalar.

V1 chegaralari: statik opaque/cutout meshlar, 128 part/LOD; bitta komponent picking ID. Soya yaqin LOD'dagi barcha instance'larni konservativ chizadi; alohida GPU shadow culling yo'q. Ko'rinadigan selection konturi ID buferidan olinadi; packed occluded silhouette chizilmaydi. Collider/gameplay entity'lari har instance uchun yaratilmaydi. HLOD/streaming alohida bosqich.

Tekshiruv (2026-11-10): 711/711 unit, 12/12 reference render; GL 4.3 compute va GL 4.1 CPU oracle (4096 instance, besh kamera, asosiy tasvirlarda 0 pixel farqi), multi-part draw, shadow/picking/wire va backend switch o'tdi. Birinchi nazorat ko'rinishida 491520 o'rniga 54756 triangle; kamera source upload sonini o'zgartirmadi. FoliageWorkshop 4096 instance/5 entity bilan save/reload, eksport, Player screenshot va relocation'dan o'tdi; ikki OBJ va 12 PBR xaritasi paketga kirdi. Legacy terrain reproject packed root'ni siljitmasligi CPU regressiyasida tekshirildi. SDK 2 test va UI'siz runtime o'tdi. Dalillar: `build-review/PackedFoliage/`. Mesa llvmpipe natijalari physical GPU tezlik bahosi emas.

### Yakunlangan yirik paket — ishlaydigan 2D/3D world streaming

Bir paketning natijasi: kamera bo‘yicha yuklanadigan kataklar, umumiy sahnada fizika/script lifecycle,
editor nazorati va eksport qilinadigan 2D/3D demo. Quyidagi bandlar mahalliy test dalili bilan yopildi; remote sanitizer/compiler matritsasi push’dan keyin alohida tekshiriladi.

- [x] Runtime: radius/hysteresis, layer/dependency closure, slot/entity/declared memory admission.
- [x] Bulk import: barcha obyektlar tayyor bo‘lgach runtime state boshlanadi; umumiy 2D/3D fizika, unload va rollback.
- [x] Host integratsiyasi: Project.StreamingWorld, SceneManager frame chegaralari, Stop/Single/restart xavfsizligi.
- [x] Editor: world manifest/policy tahriri, holatlar gridi, runtime statistika, retry/preview.
- [x] 2D va 3D ko‘p katakli namuna, kamera yurishi va boundary/cancel qaytish regressiyasi.
- [x] Export: manifest va unga tegishli scene/asset closure, toza joyga ko‘chirilgan Player sinovi.
- [x] CPU/GL/editor/runtime/SDK testlari, screenshot va yakuniy hisobot; barchasi bitta paket sifatida main'ga push.

### Keyingi paket — dunyo kataklari va streaming asosi

- [x] 2D tilemap va 3D terrain uchun umumiy world-cell manifest, barqaror UUID va dependency chegaralari. `.zworld` v1 metadata, bounded parse, atomik save va xavfsiz scene yo‘llari; editor/runtime streaming hali ulanmagan.
- [x] Main thread'dan tashqarida bounded scene IO/parse; avlodli so‘rov va CPU staging cancel/discard. `WorldCellLoader` v1: dependency closure, bounded input admission va symlink-safe worker IO. Aktiv sahna unload, texture decode va renderer/residency integratsiyasi keyingi bandlarda.
- [x] Main thread’da soft build/resolve va activation/unload count byudjeti; umumiy sahnada physics/script lifecycle.
- [ ] Texture/model decode/upload uchun qat’iy resurs/fayl byudjetlari; hard frame-time acceptance.
- [ ] RAM/VRAM residency limiti; tez kamera harakati, qaytish, bekor qilish va yo'q/buzilgan katak regressiyalari.
- [ ] Ko'p katakli demo, save/reload/export, qayta bajariladigan kamera yo'li va RSS/frame-time hisobotlari.

Packed foliage uchun qoladigan acceptance: GPU shadow culling, individual instance authoring/Undo, billboards/HLOD, physical GPU va OS/driver matritsasi. Mavjud v1 bularni tugallangan deb belgilamaydi.


### SDK himoyasi, profiling va foliage muallifligi v1 — bajarilgan paket

1. **SDK/modul mosligi:** SDK headerlari va toolchain fingerprint'i, modul ichidagi ELF ABI record, `dlopen`dan oldingi bounded parser. Belgisiz, buzilgan yoki mos kelmagan modul rad etiladi; ishlab turgan modul saqlanadi. SDK headeri o'zgargan bo'lsa avval engine qayta build qilinadi.
2. **Regressiya va editor diagnostikasi:** mos/noto'g'ri/eski/strip qilingan ELF, malformed chegaralari, constructor bajarilmasligi, ishlayotgan modulni saqlash, SDK orqali build/hot reload va eksport. Editor screenshot bilan holat ko'rsatiladi.
3. **Profiling:** mavjud CPU/frame/memory hisoblagichlarini tekshirish; GPU timer query va frame-time ko'rinishi; takrorlanadigan 2D/3D benchmark sahnalari. VPS correctness va physical GPU performance dalillari alohida yuritiladi.
4. **Foliage:** mavjud instancing yo'lini o'lchash; zichlik/LOD/culling va terrain sculpt'dan keyin obyektlarni sirtga qayta joylash. Kichik va zich map sinovlari, save/load/undo va screenshot.
5. **Yakun:** tegishli CPU/GL/SDK/editor/export testlari; hujjatlar va natijalarni yangilash. Keyingi paketlar streaming, gameplay authoring va ikki tugallangan o'yin mezonlari bo'yicha davom etadi.

- [x] SDK fingerprint va bounded ELF preflight.
- [x] Xato reload'da ishlayotgan modulni/klass registry'sini saqlash va aniq diagnostika.
- [x] Real SDK/modul/hot reload/export regressiyasi va screenshot.
- [x] Asinxron GPU timeline profiling v1; physical GPU acceptance hali ochiq.
- [x] Foliage sirtga moslash v1: manual, brush bilan avtomatik, offset/normal, lock/hidden, Undo/Redo.
- [ ] Alohida GPU foliage culling/LOD/HLOD va zich sahna benchmark'i.

Tekshiruv (2026-11-10): 707/707 unit, 12/12 reference render, GL 4.1 terrain va GPU timer sinovlari; SDK native load + ko'chirilgan SDK manifest testlari; haqiqiy editor hot reload; 26 skript klassli Platformer2D eksport/screenshot/relocation; mos bo'lmagan modul eksportini rad etish; UI'siz runtime. Foliage editori 89 ildizni yangiladi, Undo/Redo o'tdi. GUI'dagi native legacy nomzodning konstruktori bajarilmadi; o'zgarmagan nomzod doimiy qayta urinishga tushmadi. Hisobotlar `build-review/SDKQuality/` da.

**SDK paketidan keyingi ish:** packed foliage GPU/CPU culling va model LOD v1 yuqoridagi paketda bajarildi. Dense scene eksporti va correctness oracle'lari shu yerda qayd etiladi. Physical GPU frame-time/RSS byudjeti hali alohida acceptance talab qiladi; undan keyingi dunyo bosqichi async tile streaming.

### P1 — yakunlangan ish paketi

- [x] 32×32 katakli terrain render bo'laklari va masofaga bog'liq LOD.
- [x] Qo'shni bo'laklar farqi ko'pi bilan bir daraja; mayda chetlar yirik chetga geometrik moslanadi.
- [x] LOD chegarasida 15% hysteresis; ortografik ko'rinishda to'liq detal.
- [x] O'zgargan balandlik va normal ta'sir qiladigan bo'laklargina qayta quriladi.
- [x] Bo'yash geometriyani qayta qurmaydi; collision/raycast to'liq heightfield aniqligida qoladi.
- [x] LOD sozlamalari sahna/prefab/copy/undo orqali saqlanadi; editor haqiqiy hisoblagichlarni ko'rsatadi.
- [x] Katta 2D tilemap visibility hududlarni ierarxik tekshiradi va oldingi row-major tartibni saqlaydi.
- [x] Aylangan, akslantirilgan va perspektiv 2D holatlar to'liq tekshiruv oracle'i bilan mos.
- [x] CPU algoritm testlari, haqiqiy GL cache/sculpt testi, save/reload, render va editor regressiyasi.
- [x] Geometriya va AABB tekshiruvi soni bilan qayta bajariladigan o'lchov; FPS bilan aralashtirilmaydi.

### Visual Script — foydalanuvchi so'rovi, 2026-11-10

- [x] `.zgraph` format, typed katalog, tekshiruv va indekslangan bounded VM.
- [x] Node/pin/wire muharriri, qidiruv, pan/zoom/fit, xato diagnostikasi, atomik save, 64 holatli undo/redo.
- [x] Lifecycle, collision/trigger/UI/AI hodisalari; input, transform, 2D/3D fizika, prefab/audio/animation/UI amallari.
- [x] O'zgaruvchilar, nusxa overrides, shart/sequence/loop/break/delay va nomlangan funksiyalar.
- [x] Native host, aks ettirilgan C++ maydon ko'prigi, messages; C++ modulisiz ishlash.
- [x] Breakpoint, pause/step/continue, watch va cheklangan execution trace.
- [x] Asset registry / content browser / inspector / reference scan va rename integratsiyasi.
- [x] `VisualWorkshop` 2D/3D namunasi va [qo'llanma](VisualScripting.md).
- [ ] Parametr/return qiymatli funksiyalar, array/struct/enum/curve pinlari, macro/collapse va inheritance.
- [ ] RPC/replication tugunlari, to'liq native metod reflection, bytecode/native cook va katta graph profiling.

### Ichki C++ kod muharriri — 2026-11-10

- [x] Editor ichida tablar, source ro'yxati, C++ ranglari, monospace font, qator raqamlari va auto-indent.
- [x] Find/replace, undo/redo, 265 API nom/signatura katalogi, Ctrl+Space va gameplay snippet'lari.
- [x] Inspektor/content browser/compiler manbasini ichki oynada ochish; read-only SDK fayllari.
- [x] Builddan oldin save-all, F7 build/hot reload, error line/column navigatsiyasi.
- [x] Atomik save, tashqi o'zgarish konflikti, UTF-8/BOM/CRLF, bounded undo va crash recovery.
- [x] 9 fayl-himoya testi, 691 umumiy unit, haqiqiy keyboard/build/error/recovery GUI sinovi.
- [ ] clangd/LSP orqali semantic completion, go-to-definition, project rename va live type diagnostics.

### P2 — GPU terrain materiallari, 2026-11-10

- [x] 512×512 runtime bake o'rniga GPU'da to'rtta linear albedo qatlami va resolution'ga mos RGBA32F control.
- [x] Har qatlamga tangent normal, ORM, normal strength, roughness/metallic/occlusion va metrga bog'liq UV.
- [x] Mavjud lighting/shadow/fog/debug/picking/wire yo'llariga ulash; normal mesh materiallari alohida qoladi.
- [x] TextureLibrary import/cache/reload, xavfsiz asset yo'llari, yo'q xarita fallback'i va control hisoblagichlari.
- [x] Serializer asset handle'lari, legacy default'lar va editor boshqaruvlari.
- [x] 696/696 CPU, 12/12 render, GL 4.1 pixel/cache sinovlari, editor va MapWorkshop export/relocation tekshiruvi.
- [x] Packed foliage instancing/culling va ikki model LOD v1 (yuqoridagi paket).
- [ ] Atlas/cook, terrain tile streaming va 2D kontent workflow ishlari.

P2 dalillari: 12 ta source PBR teksturasi export reachability orqali paketga kirdi; eksport qilingan Player 12 o'lchov kadri, screenshot va boshqa papkaga ko'chirish tekshiruvidan o'tdi. Editor 67600 byte control, 1 upload, 0 missing map ko'rsatdi. GL testi LOD UV mosligi, normal/ORM/AO, picking/wire, native mesh binding, qayta import, xavfsiz yo'llar va 120 validation kadrida upload bo'lmasligini tasdiqladi. Shadow atlas resize'da render target yo'qolishi va paint float tebranishi tuzatildi. CI'da GPU test qadami qo'shildi; bu sessiyada testlar lokal VPS/Mesa llvmpipe'da bajarildi. Physical GPU, Windows/macOS va to'liq professional parity hali tasdiqlanmagan.

### Unity / Unreal / Godot sifatiga chiqish mezonlari

Bu engine'lar bilan tenglik bitta imkoniyat yoki kod hajmi bilan belgilanmaydi. Har yo'nalishda foydalanuvchi bajara oladigan ish, takrorlanadigan test va ochiq cheklov ko'rsatiladi; tasdiqlanmagan foiz berilmaydi.

| Yo'nalish | Zamin'dagi holat | Qabul qilish uchun keyingi dalil |
|---|---|---|
| 2D | Tilemap, sprite, animatsiya, Box2D, UI va katta map visibility mavjud | Tugallangan 2D o'yin; atlas/residency, input/UI va stress profili |
| 3D xarita | Heightfield authoring, collision, patch LOD; GPU PBR qatlamlari GL/editor/export sinovlaridan o'tdi | Haqiqiy assetli map, foliage/streaming va physical GPU frame-time byudjeti |
| Gameplay authoring | C++ muharriri va typed visual graph v1 mavjud | Funksiya argument/return, murakkab type'lar, LSP, katta graph/debug workflow |
| Dunyo | Render bo'laklari va bounded world-cell manifest mavjud; disk streaming rejalashtirilgan | Chegaralangan RAM/VRAM bilan async load/unload/cancel va katta dunyo testi |
| Jamoaviy tarmoq | Multiplayer production acceptance hali bajarilmagan | Authoritative replication, latency/loss/reconnect testlari, xavfsizlik va kuzatuv |
| Mahsulot sifati | Linux testlar va eksport workflow mavjud | Native SDK ABI gate mavjud; OS/driver matritsasi, crash/recovery va uzoq sessiya |

Taqqoslash mezonlari uchun rasmiy manbalar: [Unity Terrain Layer](https://docs.unity.com/en-us/engine/6000.6/manual/creating-environments/script-terrain/terrain-textures/class-terrain-layer), [Unreal Landscape Materials](https://dev.epicgames.com/documentation/unreal-engine/landscape-materials-in-unreal-engine), [Godot MultiMesh](https://docs.godotengine.org/en/stable/classes/class_multimesh.html). Hozirgi GPU terrain bosqichi bu mahsulotlarning barcha imkoniyatlariga tenglik da'vosi emas.

### P2–P8 uchun zarur ishlar

**2D:** tile render cache va dirty regions, katta atlaslar va texture residency, animated tile yangilanishi,
terrain/auto-tile chegaralari, collision chunk rebake, navigation, sprite/animation/UI profiling. Mavjud culling,
layer, auto-tile, Box2D, UI va animatsiya imkoniyatlari qayta yozilmaydi; kamchiliklari dalil bilan kuchaytiriladi.

**3D terrain va foliage:** mavjud GPU qatlamlarini katta assetlarda tekshirish, texture arrays/material variantlari, screen-space
error LOD va o'tishlarni silliqlash, terrain tile biriktirish, import/export formatlari, road/spline asboblari,
GPU instancing/culling, zichlik maskalari, asset LOD/HLOD, tahrirdan keyin foliage'ni sirtga qayta joylash,
async collider cooking va navmesh yangilash. Voxel/cave — heightfield'dan alohida tizim.

**Umumiy kontent:** qayta importda havolalarni saqlash, cook va deterministik cache kalitlari, buzilgan fayllar,
compressed paketlar, locale/shader variantlari, asset pipeline rollback'i va versiyalangan serialization; SDK ABI gate v1 qo'shildi.
Engine komponentlari o'zgarsa eski C++ modullarni tekshirish/qayta build qilish avtomatik va tushunarli bo'lishi kerak.

**Gameplay:** character/controller, kamera, input remap/gamepad, AI/navigation, animation state/transition,
root motion/IK/retarget workflow, audio mixer/streaming, timeline/VFX va saqlash tizimlarini bir o'yin
jarayonida sinash. Mavjud funksiyalar uchun 'bor' degan belgi production sifatiga tenglashtirilmaydi.

**Multiplayer:** authoritative server, bandwidth budget, snapshot/interpolation, prediction/reconciliation,
physics ownership, replication relevancy, matchmaking/session, host migration talabini alohida belgilash,
reconnect va hisob xizmatlari. Avval transport va threat model tanlanadi; client server vakolatini bermaydi.

**Editor/launcher auth:** oldingi talab bo'yicha Zamin yagona hisobi bilan majburiy login rejalashtirilgan.
Native client ichiga server sirini joylamaslik, browser-based PKCE, OS secret store, sessiya bekor qilish va
xizmat uzilgandagi xatti-harakat aniq belgilanadi. Bu engine yadrosining render/fizika sikliga kiritilmaydi.

**Performance va release:** 2D/3D uchun seed/scene/settings versiyasi biriktirilgan benchmarklar; CPU/GPU frame time,
p95/p99, RAM/VRAM, yuklanish va draw/triangle hisoblagichlari. 60 FPS/16.7 ms maqsadi reference physical GPU
va sahna byudjeti tanlangandan keyin tekshiriladi. VPS llvmpipe sinovi haqiqiy GPU unumdorligi dalili emas.
Har bir bosqich correctness, sanitizer/recovery, regressiya, eksport va hujjat darvozasidan o'tadi.

Unreal'dagi terrain komponentlari va ularning LOD birligi — [Epic Landscape texnik qo'llanmasi](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-technical-guide-in-unreal-engine)
bo'yicha taqqoslash manbasi. Disk kataklarini manbaga yaqinlik bo'yicha yuklash tushunchasi —
[Epic World Partition qo'llanmasi](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine).
Zamin P1 render bo'laklari disk streaming'i emas; u P3 da alohida bajariladi.

---

## M01 Skript va gameplay API

- [x] 001. EntityRef maydoni uchun inspektor (ierarxiyadan sudrab qo'yish, tozalash)
- [x] 002. EntityRef serializatsiya testi
- [x] 003. Invoke/InvokeRepeating/CancelInvokes testi
- [x] 004. Tween va easing funksiyalari testi
- [x] 005. Input action xaritasi (loyiha sozlamalarida: tugma/gamepad/sichqoncha bog'lash)
- [x] 006. Input::IsActionPressed / IsActionJustPressed / IsActionJustReleased
- [x] 007. Input::GetAxis (manfiy/musbat tugma + gamepad o'qi, deadzone)
- [x] 008. Klaviatura va sichqoncha uchun JustPressed/JustReleased (kadrlar orasidagi holat)
- [x] 009. SaveData (PlayerPrefs): Int/Float/String/Bool saqlash, fayl ~/.local/share/zamin/<loyiha>
- [x] 010. SaveData testi
- [x] 011. Random API (seed, Range, Float, Int, Bool, InsideCircle, OnSphere)
- [x] 012. Teglar (Tag) tizimi: FindEntitiesWithTag, HasTag
- [x] 013. Xabarlar/hodisalar: SendMessage(name), Broadcast, skriptlararo event bus
- [x] 014. GetComponentInChildren / GetComponentInParent / GetChildren
- [x] 015. FindEntity bo'yicha keshlash (nom->entity tez qidirish)
- [x] 016. Script lifecycle: OnEnable/OnDisable/OnStart (birinchi Update'dan oldin)
- [x] 017. OnTriggerBegin/OnTriggerEnd (sensorlar uchun alohida callback)
- [x] 018. Skript maydonlari: enum va ranglar (glm::vec4 rang sifatida) uchun ZM_FIELD_COLOR
- [x] 019. Skript maydonlari uchun tooltip/diapazon atributlari (ZM_FIELD_RANGE)
- [x] 020. Yangi skript shabloni: hamma callback va misollar bilan

## M02 Editor UX I

- [x] 021. Ko'p obyekt tanlash (Ctrl/Shift bilan) ierarxiyada
- [x] 022. Ko'p tanlangan obyektlarni birga o'chirish/nusxalash
- [x] 023. Ko'p tanlanganlarni gizmo bilan birga ko'chirish
- [x] 024. Ierarxiyada drag-drop bilan tartibni o'zgartirish (siblings orasiga qo'yish)
- [x] 025. Transform kontekst menyusi: reset, copy, paste
- [x] 026. Snapping sozlamalari (qadam: ko'chirish/aylantirish/masshtab) toolbar'da
- [x] 027. Grid sozlamalari (o'lcham, rang, ko'rinish)
- [x] 028. Kamera xatcho'plari (Ctrl+1..5 saqlash, 1..5 qaytish)
- [x] 029. Hammasini ko'rsatish (Frame All) - butun sahnaga fokus
- [x] 030. Viewport'da obyektni nusxalash (Ctrl+D bilan sudrash Alt+drag)
- [x] 031. Inspektorda komponentlarni tartiblash (yuqori/past)
- [x] 032. Komponentni nusxalash va boshqa obyektga joylash
- [x] 033. Inspektorda komponent yig'ib qo'yish holatini eslab qolish
- [x] 034. Ierarxiyada ko'rinish (ko'z) va qulf tugmalari
- [x] 035. Qulflangan obyektlar viewport'da tanlanmaydi
- [x] 036. Kontent brauzerda fayllarni sudrab papkaga ko'chirish
- [x] 037. Kontent brauzerda ro'yxat ko'rinishi (list view)
- [x] 038. Kontent brauzerda fayl turlari bo'yicha filtr
- [x] 039. Tashqaridan faylni oynaga tashlab Assets'ga import qilish
- [x] 040. Status bar'da tanlangan obyekt ma'lumoti

## M03 Faol/nofaol obyektlar

- [x] 041. TagComponent'ga Active bayrog'i
- [x] 042. Ierarxiya bo'yicha faollik (ota nofaol -> bolalar ham)
- [x] 043. Nofaol obyektlar chizilmaydi
- [x] 044. Nofaol obyektlar fizikada ishtirok etmaydi (body o'chiriladi)
- [x] 045. Nofaol obyektlarning skriptlari Update olmaydi
- [x] 046. SetActive() skript API
- [x] 047. OnEnable/OnDisable chaqiruvlari
- [x] 048. Inspektorda Active checkbox
- [x] 049. Ierarxiyada nofaol obyektlar xira ko'rinadi
- [x] 050. Serializatsiya va testlar
- [x] 051. Particle tizimi nofaol bo'lsa to'xtaydi
- [x] 052. Audio manbalar nofaol bo'lsa to'xtaydi
- [x] 053. Animator nofaol bo'lsa yangilanmaydi
- [x] 054. UI elementlar nofaol bo'lsa ko'rinmaydi va bosilmaydi
- [x] 055. Kameralar nofaol bo'lsa asosiy kamera hisoblanmaydi
- [x] 056. Yorug'liklar nofaol bo'lsa hisobga olinmaydi
- [x] 057. Tilemap collision nofaol bo'lsa o'chadi
- [x] 058. Character controller nofaol bo'lsa o'chadi
- [x] 059. Prefab instansiyalar faollik holatini saqlaydi
- [x] 060. Faollik testlari (ierarxiya, fizika, skript)

## M04 Asset tizimi

- [x] 061. AssetHandle (UUID) va AssetRegistry
- [x] 062. .zmeta fayllar (har bir asset uchun UUID va import sozlamalari)
- [x] 063. Asset turlarini aniqlash (texture, model, audio, scene, prefab, font, hdr, material)
- [x] 064. Fayl kuzatuvchi (file watcher) - o'zgargan assetlarni qayta yuklash
- [x] 065. Teksturalarni qayta yuklash (hot reload) editor'da
- [x] 066. Modellarni qayta yuklash
- [x] 067. Tekstura import sozlamalari: filtr, wrap, mipmap, sRGB
- [x] 068. Tekstura import sozlamalari inspektori
- [x] 069. Model import sozlamalari: masshtab, yuqori o'q
- [x] 070. Asset nomi o'zgarganda havolalarni yangilash (UUID orqali)
- [x] 071. Kontent brauzerda asset tanlanganda inspektorda ko'rsatish
- [x] 072. Tekstura preview inspektorda (o'lcham, format)
- [x] 073. Model preview (submesh, uchburchak, animatsiyalar ro'yxati)
- [x] 074. Audio asset preview (davomiylik, ijro tugmasi)
- [x] 075. Ishlatilmagan assetlarni topish
- [x] 076. Assetni o'chirishdan oldin ishlatilayotganini ogohlantirish
- [x] 077. Assetlarni papkalar bo'yicha qidirish (to'liq yo'l)
- [x] 078. Asset registry serializatsiyasi va testlari
- [x] 079. Kontent brauzerda thumbnail keshlash
- [x] 080. Asset import xatolarini konsolga chiqarish

## M05 Materiallar (asset)

- [x] 081. .zmat material asset formati
- [x] 082. Material asset yuklash/saqlash
- [x] 083. MeshRenderer'da material asset havolasi
- [x] 084. Material muharriri inspektorda
- [x] 085. Material preview sharchasi (render-to-texture)
- [x] 086. Shaffof (alpha blend) rejim
- [x] 087. Alpha cutout rejimi (threshold)
- [x] 088. Ikki tomonlama (double-sided) bayroq
- [x] 089. Unlit shader rejimi
- [x] 090. Tekstura tiling va offset (vec2)
- [x] 091. Materialni kontent brauzerdan mesh'ga sudrab qo'yish
- [x] 092. Yangi material yaratish (kontent brauzer menyusi)
- [x] 093. Material o'zgarishlari hot reload
- [x] 094. glTF materiallarini .zmat ga eksport qilish
- [x] 095. Material instansiyalari (skriptdan rangni o'zgartirish, umumiy assetni buzmasdan)
- [x] 096. Shaffof obyektlarni orqadan oldinga saralash
- [x] 097. Emissive intensivlik va rang materialda
- [x] 098. Material serializatsiya testlari
- [x] 099. Standart materiallar kutubxonasi (metall, plastik, yog'och, shisha)
- [x] 100. Material asset uchun thumbnail

## M06 Rendering I

- [x] 101. Spot yorug'lik (konus, ichki/tashqi burchak)
- [x] 102. Spot yorug'lik inspektori va wireframe ko'rinishi
- [x] 103. Point light wireframe radius ko'rinishi editorda
- [x] 104. Yorug'liklar sonini oshirish (32) va eng yaqinlarini tanlash (ataylab: masofaning o'zi emas, kameraga ta'siri bo'yicha — kuchi x (radius/masofa)^2, teng ta'sirda eng yaqini; README'da)
- [x] 105. FXAA anti-aliasing
- [x] 106. Render scale (ichki o'lcham koeffitsienti)
- [x] 107. Gamma/eksposure avtomatik (auto exposure, oddiy)
- [x] 108. Wireframe rejimi (editor)
- [x] 109. Faqat yorug'lik/normal/albedo debug ko'rinishlari
- [x] 110. Frustum culling (meshlar uchun)
- [x] 111. Frustum culling (2D sprite'lar uchun)
- [x] 112. Mesh instancing (bir xil mesh + material)
- [x] 113. Render statistikasi: culled soni
- [x] 114. Soyalar sifati sozlamalari (atlas o'lchami, PCF)
- [x] 115. Shadow bias sozlamalari sahna sozlamalarida
- [x] 116. Directional light soyasi bilan bir nechta quyosh (faqat biri soya)
- [x] 117. Sky gradient + quyosh diskini sozlash
- [x] 118. Chromatic aberration va film grain effekti
- [x] 119. Bloom'ni yorug'lik bo'yicha tint qilish
- [x] 120. Rendering regressiya testi (headless skrinshot solishtirish)

## M07 Debug draw va vizualizatsiya

- [x] 121. DebugDraw API: Line, Box, Sphere, Arrow, Text (skriptlardan)
- [x] 122. DebugDraw davomiyligi (duration) bilan
- [x] 123. DebugDraw faqat editor/Development rejimda
- [x] 124. Raycast natijasini vizualizatsiya qilish
- [x] 125. Fizika kontakt nuqtalarini ko'rsatish
- [x] 126. Kamera frustumini editorda chizish
- [x] 127. Yorug'lik yo'nalishini o'q bilan ko'rsatish
- [x] 128. Audio manba radiusini ko'rsatish
- [x] 129. Character controller tezlik vektori
- [x] 130. Navigatsiya yo'llarini chizish (keyingi milestone bilan)
- [x] 131. Gizmo o'lchami sozlamasi
- [x] 132. Selection outline (shader asosida, sillik kontur)
- [x] 133. Hover qilingan obyekt kontur bilan ajratiladi
- [x] 134. Bounding box ko'rsatish rejimi
- [x] 135. Skelet suyaklarini chizish (animatsiya debug)
- [x] 136. Normal vektorlarini ko'rsatish (debug)
- [x] 137. FPS grafigi overlay viewport'da
- [x] 138. Draw call va uchburchaklar overlay
- [x] 139. Profiler'da ichki bo'limlar (ierarxik)
- [x] 140. Debug draw testlari

## M08 2D yaxshilanishlar

- [x] 141. Sprite sheet slicer (grid bo'yicha kesish, editor oynasi)
- [x] 142. Sprite atlas asset (nomlangan kadrlar)
- [x] 143. 9-slice sprite rendering
- [x] 144. Sorting layer'lar (nomlangan qatlamlar)
- [x] 145. Parallax qatlamlar komponenti
- [x] 146. 2D kamera chegaralari (bounds) va silliq kuzatish komponenti
- [x] 147. Kamera silkinishi (camera shake) API
- [x] 148. Tilemap: bir nechta qatlam
- [x] 149. Tilemap: auto-tile qoidalari (4/8 qo'shni)
- [x] 150. Tilemap: to'rtburchak va to'ldirish (flood fill) asboblari
- [x] 151. Tilemap: plitka bo'yicha collision bayroqlari
- [x] 152. Tilemap: one-way platformalar
- [x] 153. Tilemap: animatsiyali plitkalar
- [x] 154. Tilemap: kattalashtirish/kichraytirishda palitra
- [x] 155. Sprite outline/flash effekti (hit flash)
- [x] 156. Sprite rang tint animatsiyasi
- [x] 157. 2D yorug'lik (oddiy point light 2D, additive)
- [x] 158. Pixel-perfect kamera rejimi
- [x] 159. Sprite'larni Y bo'yicha saralash (top-down o'yinlar)
- [x] 160. 2D testlari

## M09 2D fizika II

- [x] 161. Distance joint 2D
- [x] 162. Revolute (hinge) joint 2D (motor, limit)
- [x] 163. Prismatic joint 2D
- [x] 164. Weld joint 2D
- [x] 165. Wheel joint 2D
- [x] 166. Polygon collider 2D (vertex muharriri)
- [x] 167. Edge/Chain collider 2D
- [x] 168. Physics material (ishqalanish/sakrash) asset
- [x] 169. Collision qatlamlari va maskalar (2D)
- [x] 170. One-way platform effektori
- [x] 171. Area effector (shamol/suv)
- [x] 172. Collision filtrlash testi
- [x] 173. OverlapCircle/OverlapBox so'rovlari 2D
- [x] 174. RaycastAll 2D
- [x] 175. Joint'lar uchun inspektor va wireframe
- [x] 176. Joint'lar serializatsiyasi
- [x] 177. Fizik ragdoll namunasi (2D)
- [x] 178. Box2D sub-step sozlamalari
- [x] 179. 2D fizika uchun sleeping sozlamasi
- [x] 180. 2D fizika testlari

## M10 3D fizika II

- [x] 181. Fixed joint 3D
- [x] 182. Hinge joint 3D (motor, limit)
- [x] 183. Distance joint 3D
- [x] 184. Slider joint 3D
- [x] 185. Convex hull collider (modeldan)
- [x] 186. Statik triangle mesh collider (modeldan)
- [x] 187. Collision qatlamlari va maskalar (3D)
- [x] 188. OverlapSphere/OverlapBox so'rovlari 3D
- [x] 189. SphereCast/ShapeCast 3D
- [x] 190. RaycastAll 3D
- [x] 191. Continuous collision detection (CCD) bayrog'i
- [x] 192. Sleeping/uyg'otish API
- [x] 193. Physics material 3D
- [x] 194. Trigger callback'lari 3D (OnTriggerBegin)
- [x] 195. Character controller dinamik jismlarni itarishi (kuch sozlamasi)
- [x] 196. Joint'lar inspektori va wireframe
- [x] 197. Joint'lar serializatsiyasi
- [x] 198. 3D ragdoll (oddiy) namunasi
- [x] 199. Fizika debug: contact nuqtalari
- [x] 200. 3D fizika testlari

## M11 Audio

- [x] 201. Audio mixer guruhlari (Master, Music, SFX, UI)
- [x] 202. Guruh balandligi va mute
- [x] 203. AudioListener komponenti (kamera o'rniga)
- [x] 204. Musiqa crossfade API
- [x] 205. Musiqani streaming (katta fayllar uchun)
- [x] 206. Ovoz pitch tasodifiyligi (random range)
- [x] 207. Bir vaqtda o'ynaydigan ovozlar limiti
- [x] 208. AudioSource: Play/Stop/Pause skript API
- [x] 209. AudioSource holatini inspektorda ko'rsatish
- [x] 210. Doppler effekti (oddiy)
- [x] 211. Audio clip asset (davomiylik, format)
- [x] 212. Ovoz so'nishi (fade in/out)
- [x] 213. Ovoz effektlari: low-pass filter
- [x] 214. Ovoz sozlamalari SaveData bilan saqlanadi
- [x] 215. Audio testlari (ovoz qurilmasisiz)
- [x] 216. Audio profiler (faol ovozlar soni)
- [x] 217. Editorda audio preview to'lqin shakli
- [x] 218. 3D ovoz uchun yo'nalishli konus
- [x] 219. Ambient zona (hajm ichida ovoz)
- [x] 220. Audio hodisalar (OnSoundFinished)

## M12 UI tizimi

- [x] 221. UI Image komponenti (sprite, 9-slice)
- [x] 222. UI Slider
- [x] 223. UI Toggle (checkbox)
- [x] 224. UI Progress bar
- [x] 225. UI matn kiritish (input field)
- [x] 226. Vertikal/gorizontal layout guruhlari
- [x] 227. Stretch anchor'lar (chapdan-o'ngga cho'zilish)
- [x] 228. Ekran o'lchamiga moslashuvchi masshtab (reference resolution)
- [x] 229. UI hodisalari: OnHover/OnPress/OnRelease
- [x] 230. UI uchun klaviatura/gamepad navigatsiyasi
- [x] 231. UI animatsiyalari (tween bilan)
- [x] 232. Bir nechta shrift o'lchamlari (atlas sifati)
- [x] 233. Matnni tekislash (chap/markaz/o'ng, yuqori/o'rta/past)
- [x] 234. Matn o'rash (word wrap) kenglik bo'yicha
- [x] 235. UI editorda tanlash va ko'chirish (2D gizmo)
- [x] 236. UI ierarxiyasi (ota UITransform'ga nisbatan)
- [x] 237. UI mask/clip (scroll view)
- [x] 238. Scroll view
- [x] 239. Dialog/menyu namunasi (pause menu)
- [x] 240. UI testlari

## M13 Animatsiya II

- [x] 241. Property animatsiya kliplari (.zanim): transform/rang kalit kadrlari
- [x] 242. Animatsiya timeline muharriri (editor oynasi)
- [x] 243. Kalit kadrlar interpolyatsiyasi (linear, step, bezier)
- [x] 244. Animatsiya event'lari (vaqtda skript callback)
- [x] 245. Root motion (glTF)
- [x] 246. Blend tree 1D (tezlik bo'yicha walk/run aralashtirish)
- [x] 247. Animatsiya qatlamlari (ustki tana) - oddiy
- [x] 248. Sprite animatsiya kliplari asset sifatida
- [x] 249. Animator parametrlarini inspektorda runtime ko'rsatish
- [x] 250. Holat mashinasi uchun vizual graf (node'lar)
- [x] 251. Animatsiya preview editorda (scrub)
- [x] 252. Skelet: suyakka obyekt biriktirish (attachment socket)
- [x] 253. Animatsiya tezligi bo'yicha qadam tovushlari event bilan
- [x] 254. Animatsiya kliplarini qisqartirish (start/end)
- [x] 255. glTF morph target'lar (blend shapes) - oddiy
- [x] 256. Animatsiya keshlash (performance)
- [x] 257. Bir nechta skin qo'llab-quvvatlash
- [x] 258. Animatsiya testlari (property clip, event)
- [x] 259. Animatsiya namunasi (eshik ochilishi)
- [x] 260. Animatsiya hujjatlari

## M14 Particle II

- [x] 261. Rang gradienti (ko'p nuqtali)
- [x] 262. O'lcham egri chizig'i (curve)
- [x] 263. Tezlik umr bo'yi o'zgarishi
- [x] 264. Tekstura sheet animatsiyasi (flipbook)
- [x] 265. Konus emitter shakli
- [x] 266. Mesh sirtidan emitter
- [x] 267. Sub-emitter (o'lganda portlash)
- [x] 268. Yer tekisligi bilan to'qnashuv (bounce)
- [x] 269. Shovqin (noise) turbulentlik
- [x] 270. Trail (iz) rendering
- [x] 271. Particle'larni GPU'ga ko'proq batch qilish
- [x] 272. Particle sorting (kameraga masofa)
- [x] 273. Prewarm (boshidan to'ldirilgan holat)
- [x] 274. Emissiya portlashlari jadvali (bursts list)
- [x] 275. Simulyatsiya tezligi va local/world
- [x] 276. Particle preset'lari kutubxonasi (olov, tutun, uchqun, yomg'ir, qor)
- [x] 277. Yomg'ir va qor effektlari (kamera atrofida)
- [x] 278. Particle inspektorida bo'limlar
- [x] 279. Particle serializatsiya testlari
- [x] 280. Particle performance testi

## M15 Sahna boshqaruvi

- [x] 281. Additive sahnalar (bir vaqtda bir nechta)
- [x] 282. DontDestroyOnLoad (persistent obyektlar)
- [x] 283. Asinxron sahna yuklash (loading screen)
- [x] 284. Sahna o'tishlari (fade in/out)
- [x] 285. Build sahnalar ro'yxati (loyiha sozlamalarida)
- [x] 286. LoadScene indeks bo'yicha
- [x] 287. Sahna yuklanganda/yopilganda hodisalar
- [x] 288. Sahna ichida qidirish API'lari tezlashtirish
- [x] 289. Sahnani yuklashda xatolarni ko'rsatish
- [x] 290. Sahnalarni solishtirish (diff) uchun barqaror UUID tartibi
- [x] 291. Sahna metadata (ta'rif, thumbnail)
- [x] 292. Editor: sahnalar ro'yxati paneli
- [x] 293. Editor: o'yin rejimida sahna almashganda editor holatini tiklash
- [x] 294. Global o'yin holati (singleton servislar)
- [x] 295. Sahna versiya migratsiyasi
- [x] 296. Sahna yuklash vaqtini profiler'da ko'rsatish
- [x] 297. Katta sahnalar uchun tezroq serializatsiya
- [x] 298. Sahnani bo'laklarga bo'lish (sub-scenes) - oddiy
- [x] 299. Sahna boshqaruvi testlari
- [x] 300. Sahna boshqaruvi hujjatlari

## M16 Editor II

- [x] 301. O'yin rejimida editor rang tusi (tint)
- [x] 302. Game view rezolyutsiya preset'lari (16:9, 4:3, 1080p)
- [x] 303. Konsolda matn bo'yicha filtr
- [x] 304. Konsolda bir xil xabarlarni guruhlash (collapse)
- [x] 305. Sozlamalar oynasi (Preferences): tema rangi, shrift o'lchami
- [x] 306. Klaviatura yorliqlari ro'yxati oynasi
- [x] 307. Avtomatik saqlash (autosave) va tiklash
- [x] 308. So'nggi sahnalar ro'yxati
- [x] 309. Undo tarixi paneli
- [x] 310. Selection uchun undo
- [x] 311. Editor layout'larini saqlash/yuklash
- [x] 312. Bir nechta viewport (yon tomondan/yuqoridan ko'rinish)
- [x] 313. Ortografik ko'rinishlar (Top/Front/Right) tugmalari
- [x] 314. Editor kamerasi tezligi sozlamasi va FOV
- [x] 315. Viewport'da obyekt nomlarini ko'rsatish
- [x] 316. Sahna statistikasi paneli (komponentlar soni)
- [x] 317. Editor ichida skript fayllarni ochish (tashqi muharrir)
- [x] 318. Kompilyatsiya xatolarini fayl/qator bilan ko'rsatish
- [x] 319. Editor yangilanishini kamaytirish (idle bo'lganda FPS cheklash)
- [x] 320. Editor II testlari (headless)

## M17 Loyiha sozlamalari

- [x] 321. Loyiha sozlamalari oynasi
- [x] 322. Input xaritasi muharriri
- [x] 323. Teglar va qatlamlar muharriri
- [x] 324. Fizika qatlamlari matritsasi
- [x] 325. Sifat sozlamalari (soya, SSAO, bloom default)
- [x] 326. Player oyna sozlamalari (o'lcham, fullscreen, vsync, sarlavha)
- [x] 327. O'yin ikonkasi
- [x] 328. Boshlang'ich sahna va build sahnalari
- [x] 329. Kompaniya/o'yin nomi va versiya
- [x] 330. Loyiha sozlamalari serializatsiyasi
- [x] 331. Loyiha sozlamalari testlari
- [x] 332. Grafik API va kontekst sozlamalari
- [x] 333. Vaqt sozlamalari (fixed timestep, time scale)
- [x] 334. Time.timeScale skript API (sekinlashtirish)
- [x] 335. Gravitatsiya standart qiymatlari loyiha bo'yicha
- [x] 336. Audio standart sozlamalari
- [x] 337. Lokalizatsiya: til fayllari (uz, ru, en)
- [x] 338. Lokalizatsiya skript API (Localize("key"))
- [x] 339. Editor tilini tanlash (o'zbek/ingliz)
- [x] 340. Loyiha sozlamalari hujjatlari

## M18 Runtime player

- [x] 341. Player oyna sozlamalarini loyihadan o'qish
- [x] 342. F11 bilan fullscreen almashtirish
- [x] 343. FPS cheklovi
- [x] 344. Splash ekran (Zamin logotipi)
- [x] 345. Player command line: --windowed, --resolution
- [x] 346. Player log faylga yozish
- [x] 347. Crash bo'lganda xabar oynasi
- [x] 348. Player'da kursorni yashirish/bloklash API
- [x] 349. Ekran o'lchami o'zgarishiga moslashish
- [x] 350. Eksportda ishlatilmagan assetlarni chiqarib tashlash
- [x] 351. Eksportda skript modulini optimizatsiya (Release)
- [x] 352. Eksport: .desktop fayli va ikonka
- [x] 353. Eksport hisobotini ko'rsatish (hajm, fayllar)
- [x] 354. Player'da pauza (fokus yo'qolganda)
- [x] 355. Gamma va yorqinlik sozlamasi player'da
- [x] 356. Player ishlash testi (headless)
- [x] 357. Eksport testi (avtomatik)
- [x] 358. Player uchun minimal o'yin menyusi namunasi
- [x] 359. Player versiya ma'lumoti
- [x] 360. Runtime hujjatlari

## M19 Matematika va core

- [x] 361. Perlin/Simplex shovqin funksiyalari
- [x] 362. AnimationCurve turi (kalit nuqtalar, baholash)
- [x] 363. Spline yo'llar (Catmull-Rom) va komponent
- [x] 364. Spline bo'ylab harakat skripti
- [x] 365. Rang utilitalari (HSV, lerp, hex)
- [x] 366. Math utilitalari (Remap, SmoothDamp, MoveTowards, Approximately)
- [x] 367. Quaternion yordamchilari (LookRotation, Slerp)
- [x] 368. Job system / thread pool
- [x] 369. Parallel for (particle va animatsiya uchun)
- [x] 370. Xotira statistikasi profiler'da
- [x] 371. Log fayl va log darajasi sozlamasi
- [x] 372. Assert'larni yaxshilash (xabar bilan)
- [x] 373. Timer va Stopwatch utilitalari
- [x] 374. String utilitalari (split, trim, format)
- [x] 375. UUID generatsiya testi kuchaytirish
- [x] 376. Fayl tizimi yordamchilari (yo'l normalizatsiya)
- [x] 377. Bounding volume yordamchilari (AABB, sfera, ray kesishuvi)
- [x] 378. Geometriya testlari (ray-AABB, ray-sfera)
- [x] 379. Math testlari
- [x] 380. Core hujjatlari

## M20 Navigatsiya va AI

- [x] 381. Grid asosidagi A* pathfinding (2D)
- [x] 382. Pathfinding uchun grid'ni tilemap'dan yaratish
- [x] 383. NavGrid 3D (yer tekisligi bo'yicha, to'siqlarni aniqlash)
- [x] 384. NavAgent komponenti (yo'l bo'ylab yurish)
- [x] 385. Yo'lni silliqlash (string pulling)
- [x] 386. Dinamik to'siqlar (grid yangilash)
- [x] 387. Steering: seek, flee, arrive, wander
- [x] 388. Obstacle avoidance (raycast bilan)
- [x] 389. AI holat mashinasi yordamchi klassi
- [x] 390. Ko'rish maydoni (line of sight) tekshiruvi
- [x] 391. Patrol yo'llari (waypoints) komponenti
- [x] 392. Navigatsiya debug chizish
- [x] 393. Navigatsiya testlari
- [x] 394. AI namunasi: dushman ta'qib qiladi (2D)
- [x] 395. AI namunasi: 3D NPC patrul
- [x] 396. Formation/guruh harakati (oddiy)
- [x] 397. Behavior tree (minimal) yordamchisi
- [x] 398. Navigatsiya performance testi
- [x] 399. Navigatsiya serializatsiyasi
- [x] 400. AI hujjatlari

## M21 Prefab va serializatsiya II

- [x] 401. Prefab instansiyalari asl prefabga bog'lanishi (link)
- [x] 402. Prefab o'zgarishlarini instansiyalarga qo'llash (Apply)
- [x] 403. Instansiyani asl holatiga qaytarish (Revert)
- [x] 404. Prefab override'larini ko'rsatish inspektorda
- [x] 405. Ichma-ich prefab'lar (nested)
- [x] 406. Prefab muharriri rejimi (alohida ochish)
- [x] 407. Prefab instansiyalarda EntityRef'larni qayta xaritalash
- [x] 408. Dublikat qilishda EntityRef'larni ichki xaritalash
- [x] 409. Sahna fayli versiyasi va migratsiya
- [x] 410. Buzilgan sahna faylini tiklash (qisman yuklash)
- [x] 411. Serializatsiya fuzz testi (tasodifiy JSON)
- [x] 412. Katta sahnalar serializatsiya benchmark
- [x] 413. Prefab variantlari (asosiy prefabdan meros)
- [x] 414. Prefab thumbnail
- [x] 415. Prefab'ni kontent brauzerdan sahnaga ko'p marta sudrash
- [x] 416. Prefab o'zgarishi hot reload
- [x] 417. Prefab serializatsiya testlari
- [x] 418. Clipboard formatini prefab bilan birlashtirish
- [x] 419. Sahna eksportida prefab'larni tekshirish
- [x] 420. Prefab hujjatlari

## M22 Sifat va testlar

- [x] 421. AddressSanitizer bilan build va testlar
- [x] 422. UndefinedBehaviorSanitizer bilan testlar
- [x] 423. Xotira oqishini tekshirish (LeakSanitizer)
- [x] 424. Ogohlantirishlarni tozalash (-Wall -Wextra -Wpedantic)
- [x] 425. Renderer uchun headless skrinshot testlari CI'da
- [x] 426. Editor smoke test (barcha panellar ochiladi)
- [x] 427. Skript hot reload avtomatik testi
- [x] 428. Eksport avtomatik testi
- [x] 429. Fizika determinizm testi
- [x] 430. Performance benchmark (10k sprite, 1k mesh)
- [x] 431. ECS benchmark
- [x] 432. Serializatsiya benchmark
- [x] 433. Test framework: test nomlari bo'yicha filtr
- [x] 434. Test framework: vaqt hisoboti
- [x] 435. Code coverage hisoboti (gcov)
- [x] 436. clang-format konfiguratsiyasi
- [x] 437. CI: sanitizer job
- [x] 438. CI: artefaktlar (player build)
- [x] 439. Regressiya testlari ro'yxati hujjati
- [x] 440. Sifat hisoboti

## M23 Hujjatlar

- [x] 441. docs/Scripting.md - C++ skriptlar qo'llanmasi
- [x] 442. docs/Components.md - barcha komponentlar ma'lumotnomasi
- [x] 443. docs/Editor.md - editor qo'llanmasi
- [x] 444. docs/Tutorial-2D.md - birinchi 2D o'yin
- [x] 445. docs/Tutorial-3D.md - birinchi 3D o'yin
- [x] 446. docs/Physics.md
- [x] 447. docs/Rendering.md
- [x] 448. docs/Animation.md
- [x] 449. docs/UI.md
- [x] 450. docs/Audio.md
- [x] 451. docs/Architecture.md - ichki arxitektura
- [x] 452. docs/BuildAndExport.md
- [x] 453. Doxygen konfiguratsiyasi
- [x] 454. API izohlarini header'larda to'ldirish
- [x] 455. FAQ va muammolarni hal qilish
- [x] 456. Hissa qo'shish qo'llanmasi (CONTRIBUTING)
- [x] 457. CHANGELOG
- [x] 458. Rasmli qo'llanmalar (skrinshotlar)
- [x] 459. README yangilash
- [x] 460. Hujjatlar indeksi

## M24 Namuna o'yinlar

- [x] 461. Top-down shooter (2D): o'yinchi, o'q otish, dushmanlar
- [x] 462. Top-down shooter: to'lqinlar va hisob
- [x] 463. Breakout (2D): raketka, to'p, g'ishtlar
- [x] 464. Breakout: bonuslar va darajalar
- [x] 465. 3D FPS controller namunasi (sichqoncha bilan qarash)
- [x] 466. FPS: nishonlar va otish (raycast)
- [x] 467. Puzzle o'yin (match-3 yoki sokoban)
- [x] 468. Endless runner (3D)
- [x] 469. Racing (oddiy mashina fizikasi)
- [x] 470. Namunalarni launcher'da ko'rsatish (rasm bilan)
- [x] 471. Har bir namuna uchun README
- [x] 472. Namunalar uchun headless testlar
- [x] 473. Namuna assetlari (procedural)
- [x] 474. Namunalarda ovozlar (procedural wav)
- [x] 475. Namunalarda UI menyular
- [x] 476. Namunalarda SaveData (rekord)
- [x] 477. Namunalarda input action'lar
- [x] 478. Namunalarni eksport qilib tekshirish
- [x] 479. Namunalar galereyasi (docs)
- [x] 480. Namunalar performance tekshiruvi

## M25 Optimallashtirish va sayqal

- [x] 481. Renderer2D batching optimallashtirish (kamroq flush)
- [x] 482. Kadrlararo ajratmalarni kamaytirish (per-frame allocations)
- [x] 483. World transform keshlash (dirty flag)
- [x] 484. ECS view'larni keshlash
- [x] 485. Font atlas keshlash va kerning
- [x] 486. Tekstura yuklashni fon oqimida (async)
- [x] 487. Model yuklashni fon oqimida
- [x] 488. Shader'larni keshlash (program binary)
- [x] 489. Uniform'larni UBO'ga ko'chirish
- [x] 490. Shadow pass'da culling
- [x] 491. Particle simulyatsiyasini parallellashtirish
- [x] 492. Animatsiya baholashni parallellashtirish
- [x] 493. Editor UI performance (katta ierarxiyalar - clipper)
- [x] 494. Kontent brauzer katta papkalar (clipper)
- [x] 495. Start vaqtini qisqartirish
- [x] 496. Xotira ishlatilishini kamaytirish
- [x] 497. Barcha namunalar 60 FPS tekshiruvi
- [x] 498. Profiler orqali bottleneck hisobot
- [x] 499. Oxirgi sayqal: UI matnlari va tarjimalar
- [x] 500. Yakuniy regressiya va barcha testlar
