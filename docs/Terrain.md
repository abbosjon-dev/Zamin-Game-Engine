# 3D map va terrain tayyorlash

Terrain tizimi 3D xarita muallifligi uchun: balandlik maydoni, cho'tkalar, to'rtta tekstura qatlami, prefab/daraxt/tosh ekish, saqlash va statik Jolt collision. U 2D Tilemap o'rnini almashtirmaydi; 2D xaritalar uchun Tilemap va Plitka palitrasi ishlaydi.

## Boshlash

1. **Oyna → Map tayyorlash (Terrain)** ni oching. Yoki **Obyekt → 3D → Terrain / map** yarating.
2. O'lchamni metrda va nuqtalar sonini tanlang: 33, 65, 129 yoki 257. **Yangi terrain qo'shish** sahnaga yangi yer qo'shadi; mavjud xarita o'chirilmaydi.
3. Boshlang'ich balandlik 8 metr. Balandliklar 0–128 metr oralig'ida: pasaytirish uchun ham joy bor.
4. Terrain oynasidan faol yerni tanlang. **Tanlangan yerni ko'rsatish** kamerani unga qaratadi.
5. **Cho'tka faol** ni yoqing. Viewport'dagi sariq halqa ta'sir radiusini ko'rsatadi. Transform gizmo chizishga xalaqit bermasligi uchun o'chadi; `W/E/R` bilan uni yana tanlash mumkin.

## Relyef cho'tkalari

- **Ko'tarish / Pasaytirish** — radius ichida yumshoq chet bilan balandlikni o'zgartiradi. Kuch vaqtga bog'liq, tezlik FPS'dan mustaqil.
- **Tekislash** — tanlangan maqsad balandligiga olib keladi.
- **Silliqlash** — qo'shni nuqtalarning o'rtachasi bilan o'tkir qirralarni yumshatadi. Bir bosqichdagi hisob oldingi maydondan olinadi.
- **Shift** — ko'tarish/pasaytirishni almashtiradi; **Ctrl + LMB** — tekislash balandligini sirtdan oladi.
- **Alt / RMB** — odatdagi kamera boshqaruvi. Cho'tka kamera harakatida ishlamaydi.

Bir marta bosib-sudrash bitta undo qadami. `Ctrl+Z` va `Ctrl+Y` relyef, bo'yoq, daraxtlar va prefablarni qaytaradi. Chizish faqat Edit rejimida, ko'rinadigan va qulflanmagan terrain'da ishlaydi. Qo'shimcha yerni buzmaslik uchun faqat faol terrain tahrirlanadi.

## Teksturalar va yo'llar

To'rtta qatlam: **maysa**, **tuproq/yo'l**, **qoya**, **qum**. Har bir qatlamning rang teksturasi, rang ko'paytiruvchisi, normal xaritasi, ORM xaritasi va mahalliy metrga nisbatan takrorlanishi bor. Rasmlar loyihaning `Assets` papkasida bo'lishi kerak; tashqariga chiqadigan yo'l va symlink qabul qilinmaydi. Rasmsiz qatlam tekis rangdan foydalanadi.

**Yer qatlamlari** oynasida normal kuchi `0..4`, dag'allik `0.04..1`, metall va occlusion `0..1` sozlanadi. ORM kanallari: **R — ambient occlusion, G — roughness, B — metallic**. Scalar qiymatlar tegishli xarita kanali bilan ko'paytiriladi. Normal xaritalar tangent-space, musbat Y formatida; ORM va normal rangga aylantirilmaydi. Rang teksturasining sRGB import belgisi hisobga olinadi. Occlusion faqat muhit yoritishi va aksiga ta'sir qiladi.

**Tekstura bo'yash** tanlangan qatlam og'irligini oshiradi, qolganlarini kamaytiradi; jami og'irlik 1. GPU ranglarni linear fazoda aralashtiradi, normal va PBR qatlamlarini har fragmentda hisoblaydi. UV butun terrain bo'ylab yagona; LOD bo'laklari orasida tekstura koordinatasi uzilmaydi. Manba teksturalari o'z import o'lchami, mipmap va sampling sozlamalarini ishlatadi. Runtime endi 512×512 baked albedo bilan chegaralanmaydi; eski `GetAlbedo()` faqat legacy preview uchun saqlangan.

Control xaritasi terrain resolution'iga teng `RGBA32F`: to'rtta og'irlik, linear sampling, clamp va texel markaziga mos UV. 65×65 uchun 66 KiB, maksimal 257×257 uchun taxminan 1032 KiB. Bu faqat control texture hajmi; manba teksturalari, GPU mipmap va CPU nusxalari alohida xotira talab qiladi. O'zgarmagan control qayta yuklanmaydi; sculpt va PBR parametrlarini o'zgartirish uni yuklamaydi. Paint geometriyani qayta qurmaydi. Oynada control hajmi, upload soni va yuklanmagan xaritalar soni ko'rinadi.

Yo'q yoki xavfsiz bo'lmagan xarita o'rniga rang/scalar va tekis normal ishlatiladi. To'rt qatlam uchun 12 manba sampler, bitta control, umumiy shadow va environment bilan jami 15 fragment texture unit ishlatiladi (OpenGL 4.1 minimumi 16). Terrain hozir alohida draw call bilan chiziladi; GPU instancing, texture arrays, height/parallax blend va avtomatik shader-layer kengaytirish hali qo'shilmagan.

## Daraxt, tosh va prefab ekish

**Obyekt ekish** rejimida:

- ichki daraxt va tosh, yoki o'z `.zprefab` faylingiz;
- zichlik, minimal oraliq, tasodifiy masshtab va burilish;
- maksimal qiyalik va sirt normaliga mos burish;
- bir bosishda 128 ta, bir terrain'da 5000 tagacha ekilgan ildiz obyekt.

Ichki daraxt/toshlar collision bilan yaratiladi. Prefab o'z komponentlarini saqlaydi. Obyektlar terrain'ning bolalari bo'ladi va `TerrainPainted` tegi bilan belgilanadi. **Obyekt o'chirish** faqat shu terrain'ga ekilgan obyektlarga ta'sir qiladi; qo'lda qo'yilgan obyektlar o'chirilmaydi.

**Ekish va relyefga moslash** bo'limidagi tugma ekilgan ildizlarni joriy terrain sirtiga tushiradi. **Sculpt bilan avtomatik moslash** yoqilsa, balandlik cho'tkasi ta'sir qilgan hudud va normal chegarasidagi ildizlar shu Undo qadami ichida yangilanadi. Generator/import'dan keyin qo'lda moslash tugmasidan foydalaning. Kattaroq yoki batafsil daraxt uchun o'z modelingizdan prefab yarating.

## Generator, heightmap va suv

**Relyef generatori** seed, amplituda va chastota bo'yicha takrorlanadigan uch oktavali relyef beradi. Generatsiya balandliklarni yangilaydi, material bo'yog'ini saqlaydi; Undo bor.

- PNG import: 8 yoki 16-bit kulrang; 16 megapikselgacha. Rasm terrain nuqtalari soniga mos qayta namunalashtiriladi.
- `.r16` import/export: kvadrat, unsigned 16-bit, little-endian. Qiymatlar `0..HeightLimit` metrga moslanadi; resolution faylda yozilmaydi, kvadrat tomoni hajmidan olinadi.
- PNG/R16 birinchi qatori terrain'ning `Z=0` chetiga mos.
- `.zscene` ichida haqiqiy float balandliklar va qatlam og'irliklari saqlanadi; R16 export taxminan `HeightLimit / 65535` metr aniqlikda.
- **Map yordami → Suv tekisligi qo'shish** shaffof ko'k plane qo'shadi; balandligini Transform'da o'zgartiring. Bu suv fizikasi yoki to'lqin simulyatsiyasi emas.

Tekstura faylini tashqi dasturda o'zgartirsangiz, **Yer qatlamlari → Teksturalarni qayta yuklash** tugmasini bosing. Manba rasmlar `TextureLibrary` orqali umumiy keshlanadi va mavjud asset hot reload yo'lidan foydalanadi. Qayta yuklash geometriyani buzmaydi. Buzilgan manba qayta importda rad etilsa oldingi ishlaydigan tekstura saqlanadi; oynada xabar ko'rsatiladi.

## Saqlash va o'yinda ishlatish

`Ctrl+S` bilan odatdagi `.zscene` fayliga saqlang. Tekstura fayllari loyihada qolishi kerak; sahnada ularning asset havolalari yoziladi. Terrain ma'lumoti tashqi cache'ga bog'lanmagan.

Terrain yaratishda Static Rigidbody 3D va Mesh Collider 3D ham qo'shiladi. Play/Simulate paytida haqiqiy terrain uchburchaklari Jolt collider'iga aylanadi: qahramon va jismlar tepalik ustida yurishi mumkin. `Collision` bayrog'i terrain collision'ini o'chiradi. Dinamik terrain jismi qo'llanmaydi.

Play nusxasi va Duplicate bir-biridan mustaqil balandlik/bo'yoq va mesh cache'lariga ega. Editor relyefni o'yin rejimida o'zgartirmaydi. Runtime paytida relyefni o'zgartirish va collision'ini jonli rebuild qilish, cheksiz world streaming va yo'l spline generatsiyasi bu versiyada yo'q.

Katta map uchun bir nechta terrain bo'lagidan foydalaning. Chegaralar avtomatik tikilmaydi; bir xil balandlik chetlarini yoki bir xil heightmap manbasini qo'llang. Mualliflikda unit scale va musbat scale tavsiya qilinadi.

## Tekshirish

```bash
cmake --build build-review --target ZaminTests ZaminEditor
./build-review/bin/ZaminTests 'Terrain*'
```

Balandlik, smoothing/flatten, bo'yoq normallashishi, raycast/uchburchak sirt mosligi, seed, serializer/copy, R16 va Jolt'da tushayotgan jism tekshiriladi. Editor avtomatsiyasi: `ZAMIN_EDITOR_TERRAIN=demo`; `ZAMIN_EDITOR_TERRAIN=test` haqiqiy Undo/Redo va scatter erase/Undo tarixini ham tekshiradi; ixtiyoriy `ZAMIN_EDITOR_TERRAIN_SAVE=/absolute/path/Map.zscene` sahna nusxasini saqlaydi. Avtomatsiyani o'zgarishi mumkin bo'lgan test loyiha nusxasida ishga tushiring.


## Render bo'laklari va masofaga bog'liq LOD

Terrain 32×32 katakli render bo'laklariga ajratiladi. Perspektiv kamera uzoqlashganda nuqtalar qadami
1/2/4/8/16/32 ga o'tadi. Qo'shni bo'laklar bir darajadan ko'p farq qilmaydi; mayda bo'lak cheti qo'shnining aynan o'sha nuqtalari bilan
birlashtiriladi; chekka halqa uchburchaklari mos ravishda qayta tuziladi. Bu yoriqni yopish uchun yashiruvchi devor (skirt) ishlatmaydi.
15% hysteresis kamera chegara atrofida tebranganda qayta qurishni kamaytiradi. Ortografik kamera to'liq detalda.

**Terrain unumdorligi (LOD)** bo'limida LOD'ni yoqish/o'chirish, detal masofasi va oxirgi chizilgan ko'rinishning
bo'lak/uchburchak/qayta qurilgan bo'lak hisoblagichlari bor. Katta masofa koeffitsiyenti ko'proq detalni saqlaydi.
Hisoblagich rendererga topshirilgan geometriyani ko'rsatadi; haqiqiy GPU frame time yoki ekranda ko'ringan
bo'laklar soni sifatida talqin qilinmaydi. Qo'shimcha scene view oxirgi ko'rinish statistikasi bo'lishi mumkin.

Sculpt o'zgargan height nuqtalari va normal hosilalari ta'sir qiladigan bo'laklarni qayta quradi. Bo'yoq faqat
GPU control xaritasini yangilaydi. Har bo'lak uchun bitta faol GPU mesh keshlanadi; LOD variantlari cheksiz yig'ilmaydi.
Collision, heightmap va brush raycast to'liq heightfield'dan ishlaydi. LOD fizikani soddalashtirmaydi.

Hozir bu masofaviy LOD; screen-space error, geomorph, disk streaming va avtomatik
qo'shni terrain tile biriktirish keyingi bosqichlarda. GPU splat/PBR qatlamlari mavjud.

CPU regressiyalari: `TerrainLODTests.cpp`. Haqiqiy GL cache/rebuild sinovi uchun `ZAMIN_EDITOR_TERRAIN=lod-test`
257×257 nuqtali, 256×256 metr xarita yaratadi va far/near/orthographic/sculpt/paint cache tekshiruvini bajaradi.
Ixtiyoriy `ZAMIN_EDITOR_TERRAIN_SAVE` test sahnasini saqlaydi. Ishlatish uchun alohida bo'sh test loyiha tanlang.


P1 tekshiruvi (2026-11-10): 656 unit test, 12 render holati va editorning 265 qadami xatosiz o'tdi.
257×257 maydon 64 render bo'lagidan iborat: to'liq geometriya 131072, 4096 metr masofadagi LOD 128
uchburchak. Oddiy editor ko'rinishidagi screenshot 23056 uchburchakni ko'rsatadi. Bular geometriya
hisoblagichlari; FPS alohida backend va apparatga bog'liq. Haqiqiy GL tekshiruvi o'zgarmagan mesh uchun
0, kichik sculpt uchun 1/64, paint uchun 0 GPU mesh rebuild bo'lishini tasdiqladi. Ortografik ko'rinish
131072 uchburchakni saqladi. Burchaklar/manifold qirralar va bir katak kengligidagi yakuniy bo'laklar ham sinaldi.


GPU material regressiyasi (GL kontekst kerak):

```bash
cmake --build build-review --target ZaminTerrainSurfaceTests -j3
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a ./build-review/bin/ZaminTerrainSurfaceTests
```

Test rang/ORM/normal/AO natijalarini oldindan hisoblangan pixel qiymatlari bilan solishtiradi; picking, wireframe, native mesh bilan binding, UV tiling, source reload/recovery, LOD bo'laklari UV mosligi, shadow atlas resize, control cache/resize va xavfsiz yo'llarni ham tekshiradi. CPU testlari yangi maydonlar, nusxa, eski sahna default'lari va buzilgan PBR qiymatlarini qamrab oladi. Native C++ o'yin modullari engine SDK tuzilmalari o'zgarganda qayta build qilinishi kerak; SDK ABI preflight mos kelmagan modullarni bajarilishidan oldin rad etadi; [SDK mosligi](Scripting.md#native-sdk--abi-mosligi).

`ZAMIN_EDITOR_TERRAIN=material-test` MapWorkshop nusxasidagi mavjud terrain'ni tanlaydi, 12 ta PBR xaritasi yuklanganini tekshiradi va terrain oynasini ochadi. Yangi relyef yaratmaydi. Float og'irliklar validation'da kichik yaxlitlash xatosi uchun qayta bo'linmaydi: aks holda ayrim og'irliklar ikki qiymat orasida tebranib, har kadrda control upload chaqirishi mumkin. GPU sampling'dan keyin og'irliklar yana normallanadi; bu holat CPU va GL regressiyasida sinaladi.


P2 tekshiruvi (2026-11-10): Release build va UI-free standalone yig'ildi; 696/696 unit, 12/12 render regressiyasi, GL 4.1 terrain pixel/cache suite, editor `material-test` va MapWorkshop eksport sinovlari o'tdi. Export ichida barcha 12 PBR xaritasi bor; Player screenshot va papka ko'chirilgandan keyin ham ishga tushdi. Editor control xaritasi 67600 byte, 1 upload, 0 missing map bilan ochildi. Loglar va rasmlar `build-review/TerrainPBR/` da. Bular Mesa llvmpipe correctness sinovlari; physical GPU frame-time bahosi emas.


## Foliage sirtga qayta joylash

`TerrainFoliage::Reproject` faqat terrain'ning bevosita bolasi bo'lgan `TerrainPainted` ildizlarini Edit rejimida o'zgartiradi. X/Z, masshtab va prefab bolalari saqlanadi; sirt normaliga moslash ixtiyoriy. Height offset terrain lokal koordinatasida. Qulflangan/yashirilgan, xarita tashqarisidagi va boshqa qo'lda joylangan obyektlar o'tkaziladi. Har chaqiruvda ko'pi bilan 5000 ildiz o'zgartiriladi. Region va offset parametrlari API'da ham mavjud; gameplay/Simulate vaqtida qayta joylash rad etiladi.

Tugma va brush bilan avtomatik moslash Undo/Redo tarixiga kiradi. Avtomatik moslash checkbox'i, normalga moslash va offset bu versiyada editor sessiyasi sozlamalari; o'zgargan transformlar sahna/prefab orqali saqlanadi. Scene instancing hisoblagichi butun sahnadagi mesh instancing'ni ko'rsatadi. Bu entity mualliflik yo'li packed foliage'dan alohida; HLOD va streaming keyingi ishlar.

`ZAMIN_EDITOR_TERRAIN=foliage-test` MapWorkshop nusxasida balandlikni o'zgartiradi, ildizlarni moslaydi va haqiqiy editor Undo/Redo holatini tekshiradi. Asl loyihada ishlatmang; test nusxasi uchun mo'ljallangan.

## Packed foliage va ikki model LOD

`Render → Packed foliage` bitta entity ichida 65536 tagacha statik instance saqlaydi. Inspector'da yaqin/uzoq primitive yoki model, tint, LOD va ko'rish masofasi, hysteresis, backend va soya sozlanadi. Grid yaratish va bevosita parent terrain sirtiga joylash tugmalari mavjud. Instance transformlari scene/prefab/copy/undo orqali saqlanadi; model yo'llari asset UUID bilan yoziladi. Terrain brush'ning eski entity-reproject checkbox'i packed instance'larni avtomatik yangilamaydi: ularni alohida tugma bilan moslang.

OpenGL 4.3 mavjud bo'lsa compute shader frustum/masofa tekshiradi, yaqin/uzoq SSBO'ga ko'rinadigan instance'larni yig'adi va indirect draw buyruqlarini GPU'da tayyorlaydi. OpenGL 4.1 yoki CPU backend shu qoidalarni CPU'da bajarib, instanced draw ishlatadi. LOD markazgacha masofa bilan tanlanadi; masofa culling'i bounds radiusini ham hisobga oladi. Hysteresis chegarada tebranishni kamaytiradi. Kamera o'zgarganda statik source transformlar qayta yuklanmaydi; CPU backend ko'rinadigan instance buferlarini yangilaydi.

GPU visibility hisoblagichlari uch asinxron fence slotidan oxirgi tayyor natijani ko'rsatadi. Renderer ularni kutmaydi; `DebugVisibleCounts` faqat sinov/asbob uchun blocking oracle. Inspector'dagi CPU xotira bahosi batch array'larini, GPU bahosi instance/SSBO/command buffer'larini qamraydi; komponent nusxasi, model teksturalari, drayver va umumiy process RSS'i bundan alohida. Umumiy renderer mesh/triangle hisoblagichi GPU packed visibility'ni sinxron o'qimaydi: bu ma'lumotni foliage panelidan ko'ring.

V1 statik opaque/cutout mesh va har LOD uchun 128 tagacha model part'ini qabul qiladi; skinned, morph yoki transparent part bo'lsa butun model rad etiladi. Bitta batch'da winding bir xil bo'lishi kerak. Har instance uchun collider/script entity yaratilmaydi; picking ID va ko'rinadigan selection konturi butun komponentga tegishli. Occluded packed silhouette chizilmaydi. Soya yo'li barcha instance'larni yaqin LOD bilan konservativ chizadi; katta o'rmon uchun shadow toggle va byudjetga e'tibor bering. GPU shadow culling, billboards, HLOD, disk streaming va individual instance brush keyingi bosqich.

Qayta bajariladigan GL oracle:

```bash
cmake --build build-review --target ZaminFoliageRenderTests -j3
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a ./build-review/bin/ZaminFoliageRenderTests --require-gpu
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 MESA_GL_VERSION_OVERRIDE=4.1 MESA_GLSL_VERSION_OVERRIDE=410 xvfb-run -a ./build-review/bin/ZaminFoliageRenderTests
```

Oracle 4096 instance, beshta kamera holati va CPU/GPU piksel/son mosligini, hysteresis, source upload cache, picking, wire/shaded-wire, shadow va backend almashtirishni tekshiradi. Bu Mesa correctness sinovi; physical GPU tezligi uchun dalil emas.

V1 tekshiruvi: 711/711 unit, 12/12 reference render, GL 4.3/4.1 foliage oracle, SDK compatibility va UI'siz runtime o'tdi. `FoliageWorkshop` qayta ochildi, eksport qilindi va boshqa papkadan ishga tushdi. 4096 instance'li nazorat ko'rinishida 491520 o'rniga 54756 triangle chizildi; kamera input upload'ini ko'paytirmadi. Bu geometriya va correctness o'lchovi; physical GPU frame-time acceptance hali alohida. Dalillar `build-review/PackedFoliage/` da.


## World-cell manifest — streaming uchun metadata asosi

`Zamin/Scene/WorldManifest.h` dagi `WorldManifest` 2D va 3D xaritalarni umumiy formatda tavsiflaydi. XY kataklar 2D uchun, XZ yoki XYZ esa 3D uchun ishlatiladi. `.zworld` JSON faylining `Format` qiymati `ZaminWorld`, `Version` qiymati `1`; `Name`, `Origin`, `CellSize` va `Cells` majburiy. Har katakda 16 hex belgili, nol bo‘lmagan `ID`, butun sonli uchta `Coordinate`, `Layer`, asset ildiziga nisbatan `.zscene` `Scene` yo‘li, `CPUBytes`, `GPUBytes` va `Dependencies` UUID ro‘yxati bor.

Bir qatlamda koordinatalar takrorlanmaydi; boshqa qatlamlar bir joyda ustma-ust turishi mumkin. `DependencyOrder` bog‘liqliklarni avval, tayyor kataklarni esa manifestdagi indeks tartibida qaytaradi. Yo‘q, takroriy, o‘ziga bog‘langan va siklli dependency rad etiladi. `Bounds` double aniqlikdagi metadata AABB beradi; rendererning floating-origin tizimi hali yo‘q. `DeclaredMemory` faqat muallif bergan CPU/GPU taxminlarini jamlaydi; haqiqiy residency yoki VRAM o‘lchovi emas.

`Deserialize` va `Load` xato paytida oldingi manifestni saqlaydi; `Serialize` ham chiqish matnini xatoda almashtirmaydi. `Save` avval tekshiradi, keyin faylni atomik almashtiradi. Parse alohida worker thread’da mustaqil obyektga bajarilishi mumkin; bir obyektning o‘ziga parallel yozish uchun tashqi sinxronlash kerak. Chegaralar: 8 MiB fayl, 32 JSON nesting darajasi, 4096 katak, har katakka 64 dependency, koordinata o‘qlarida ±1 000 000, har CPU/GPU taxmini uchun 1 TiB. JSON kaliti takrorlansa ham rad etiladi.

`ResolveScene(assets, cell)` traversal, mutlaq yo‘l va diskdagi symlink orqali ildizdan chiqishni tekshiradi. Yo‘q scene uchun xavfsiz yo‘l qaytishi mumkin: keyingi loader IO xatosini alohida qaytarishi shart. Bu tekshiruv va kelajakdagi fayl ochish orasida disk o‘zgarishini bloklamaydi. Manifest yuklash hech qanday entity, collider, script yoki GPU resurs yaratmaydi. Hozircha editor paneli, AssetRegistry/export ulanishi, runtime activation/unload byudjeti va residency boshqaruvi navbatda; `.zworld` mavjud sahnalarni avtomatik stream qilmaydi.


## WorldCellLoader — worker’da CPU staging

`Zamin/Scene/WorldCellLoader.h` dagi `WorldCellLoader::Create(manifest, assets, limits, error)` manifestni tekshiradi, Linux asset katalogini FD bilan mahkamlaydi va 1–4 ta xususiy worker ochadi. `JobSystem` o‘chirilgan bo‘lsa ham disk o‘qish va parse owner thread’da bajarilmaydi. `Create` va barcha API chaqiruvlari bitta owner thread’ga tegishli; boshqa threaddan chaqirish `logic_error` beradi. Konstruktsiya xatosida `nullptr` va izoh qaytadi.

`SetWanted(cellIDs, error)` kamera/qatlam tanlagan kataklar ro‘yxatini ularning to‘liq dependency closure’i bilan almashtiradi. Noma’lum UUID yoki sig‘magan closure oldingi tanlovni o‘zgartirmaydi. Yangi so‘rov `Queued → Reading → Ready` yo‘lidan o‘tadi; xato `Failed` holatiga olib boradi va dependants ham aniq xato oladi. Parse dependency `Ready` yoki `Transferred` bo‘lgandan keyin boshlanadi; bu sahnalar faollashtirilgan degani emas.

Har yangi so‘rovda monoton avlod raqami bor. Katakni tanlovdan chiqarish staged ma’lumotni tashlaydi yoki worker’ni bekor qilishga belgilaydi; qayta qo‘shilganda yangi avlod yaratiladi. Eski worker tugasa, uning natijasi yangi so‘rovga aralashmaydi. Bir xil tanlov avlodlarni saqlaydi; muvaffaqiyatsiz katakni qayta urinish uchun chiqarib, yana qo‘shing. Faol o‘yin entity’larini unload qilish bu API vazifasiga hali kirmaydi.

Har kadrda `Poll(maxCompletions)` ni chaqiring: u tayyor natijalardan ko‘pi bilan shu sondagini e’lon qiladi, tugamagan worker’ni kutmaydi va sig‘gan dependency-ready ishlarni navbatga qo‘yadi. `GetStatus(id)` holat/avlod/xato/fayl baytlarini, `GetStats()` navbat va reservation hisoblarini beradi. `TakeReady(id, generation)` faqat mos tayyor natijani `unique_ptr<SceneLoadJob>` sifatida uzatadi; shu payt holat `Transferred` bo‘ladi va loader reservation’i bo‘shaydi. Bu holat activation emas. Qabul qilingan job va uning xotirasiga endi caller javobgar; undan foydalanish/yo‘q qilish uchun `SceneSerializer.h` ni include qiling. Keyingi `Poll` bo‘shagan sig‘imga ish qabul qiladi.

Default: 2 worker, 128 wanted katak (dependency’lar bilan), har fayl 16 MiB, 64 MiB source reservation, 262144 JSON key/value node va 32768 entity. Har queued/running/ready job uchun maksimal fayl hajmi oldindan reservation qilinadi; bekor qilingan tugamagan ish ham to‘liq hisobda qoladi. **Source reservation haqiqiy RAM/VRAM limiti emas**: DOM, parser scratch, migratsiya va caller’ga uzatilgan job’lar undan tashqarida. Qattiq konfiguratsiya chegaralari: 64 MiB/fayl, 512 MiB source reservation, ko‘pi bilan 64 reservation slot, 1 000 000 node va 100 000 entity. Bo‘sh sig‘im bo‘lmasa navbat kutadi.

Worker `openat`/`O_NOFOLLOW` bilan har child segmentda symlink’ni rad etadi (ildiz ichiga qaragan symlink ham qabul qilinmaydi). `O_NONBLOCK` va `fstat` FIFO/device/directory’ni sahna sifatida o‘qishdan saqlaydi; fayl uzunligi yozilayotgan vaqtida o‘zgarsa, natija rad etiladi. Asset ildizi boshqa katalog bilan almashtirilsa ham mavjud loader avval ochilgan katalogga qaraydi. Faqat `.zscene` JSON matni: `Scene`, `Version`, `Entities` majburiy, 64 nesting darajasi va duplicate-key tekshiruvi bor. 0 dan hozirgi scene versiyasigacha migratsiya worker’da bajariladi, kelajak versiyasi rad etiladi. Binary cache va `.znav` sidecar hali ulanmagan.

O‘qish 64 KiB bo‘laklarda, SAX tekshiruvi node chegaralarida cancellation’ni ko‘radi; keyingi DOM parse/migratsiya bosqichini o‘rtasidan to‘xtatmaydi, natijasini tugaganda tashlaydi. Destruktor worker’larni cancel qilib join qiladi; kernel ichida to‘xtab qolgan filesystem operatsiyasi darhol uzilishi kafolatlanmaydi. `Poll` son bo‘yicha cheklangan, DOM bo‘shatish yoki keyingi `SceneLoadJob::Build` uchun qattiq millisekund deadline yo‘q. Build faqat main thread’da, mos faol project asset kontekstida bajarilishi kerak. Texture decode/upload, physics/script lifecycle, avtomatik kamera tanlovi, activation/unload va residency integratsiyasi keyingi bosqich.


## Yirik runtime paketi: camera-driven 2D/3D streaming

`Project.StreamingWorld` — `Assets` ichidagi `.zworld` yo‘li. Play/Player boshlanganda `SceneManager`
manifest va uning `Streaming` siyosatini o‘qiydi. **Oyna → World Streaming** panelida manifest/policy
saqlanadi va loyiha bilan bog‘lanadi. O‘yin paytida panel active/loading/build/failed kataklarni, griddagi
kuzatuvchini, kirish reservation va declared byudjetlarni ko‘rsatadi; retry, qayta ulash va o‘chirish bor.
Manifest `WorldStreaming::SaveDefinition` orqali policy bilan saqlanadi; oddiy `WorldManifest::Save`
faqat metadatani saqlaydi. Katak qo‘shish/scene/layer/coordinate/CPU/GPU taxminlarini tahrirlash mumkin.

Tanlov AABB masofasi bilan bajariladi: yangi katak `LoadRadius` ichida, oldingi tanlangan katak esa
`UnloadRadius` tashqarisiga chiqquncha ushlanadi. `AxisMask` XY=3, XZ=5, XYZ=7; faol `Layers`
ro‘yxatidan dependency closure olinadi. Eng yaqin kataklar avval; precomputed dependency bitmask’lari
har kadrda qayta graph traversal qilishdan saqlaydi. Closure sig‘masa u qisman faollashtirilmaydi.

Policy `MaxResidentCells`, `MaxResidentEntities`, `MaxEntitiesPerCell` va declared CPU/GPU reservation’ni
cheklaydi. Eski faol kataklar unload limiti bilan hali turar ekan, yangi kataklar qolgan sig‘imdagina
qabul qilinadi. Bu declared ko‘rsatkichlar haqiqiy process RSS/VRAM emas; umumiy asset keshlarining
residency/eviction siyosati va physical GPU acceptance hali alohida ish. Source IO reservation ham
alohida hisoblanadi. Nol estimate muallif resurs bahosi bermaganini anglatadi.

Main thread’da scene build bo‘laklab, asset resolve entity soni va `MainThreadMilliseconds` soft byudjeti
bilan bajariladi. Kichik byudjetda ham kamida bitta build/resolve quantum bajarilib, starvation oldi olinadi. `MaxBuildingCells`, `MaxResolveEntitiesPerFrame`, `MaxActivationsPerFrame` va
`MaxUnloadsPerFrame` bir frame’dagi ishni cheklaydi. Bitta entity/asset operatsiyasi, hierarchy linking,
physics/script activation yoki unload callback o‘rtasidan uzilmaydi; bu hard frame deadline emas.
Async asset mode o‘chiq bo‘lsa resolve’dagi mavjud texture/model IO sinxron bo‘lishi mumkin.

Katakdagi barcha entity’lar `Scene::ImportStagedCell` orqali anchor sahnaga birga ko‘chiriladi.
Barcha nusxalar va hierarchy mavjud bo‘lgandan keyin physics/script/effects ishga tushadi; shu sabab
anchor o‘yinchisi bilan streamed polning 2D/3D collision olami bir xil. UUID va ichki reference’lar
saqlanadi, to‘qnashuv mutatsiyadan oldin rad etiladi. Copy/start xatosida import qilingan obyektlar
rollback qilinadi; foydalanuvchi C++ callback’ining tashqi side-effect’larini rollback qilish kafolatlanmaydi.
Katak ichida authored transform local; root’ga `Origin + Coordinate * CellSize` qo‘shiladi.

`SceneManager::ProcessRequests` oxirida activation/unload bajariladi; callback ichida streaming
host API’larini chaqirmang. Dependency kataklar avval Active bo‘ladi, unload esa teskari dependency
tartibida. Unload owned UUID’larni anchor’dan yo‘q qiladi; body va script lifecycle odatdagi entity destroy
yo‘lidan o‘tadi. `WorldCellLoaded` / `WorldCellUnloaded` bus payload’i `WorldStreaming::CellEvent`.
Anchor alohida unload’dan himoyalangan. Stop world obyektlarini olib tashlaydi; qayta Start duplikat
qoldirmaydi. Single scene load world’ni tozalaydi; boshqa level uchun yana host orqali ulash mumkin.

`Projects/WorldWorkshop3D` — 36 ta katak; `Projects/WorldWorkshop2D` — 24 ta katak. Play va WASD bilan
kamerani yuring. `ZAMIN_WORLD_AUTOTOUR=1` har 120 frame’da kamerani yangi hudud, bo‘sh hudud va boshlang‘ich
nuqtaga olib boradi; oynaning sarlavhasi va logda active/load/unload statistikasi bor. Geometriya,
ranglar va physics probe tashqi assetlarsiz authored primitive’lardan tuzilgan.

V1 chegaralari: scene fayllari kataklar uchun alohida, entity UUID’lar barcha yuklangan kontentda noyob;
SubScene komponenti katak ichida rad etiladi. ±1 000 000 unit’dan katta runtime offset floating origin
kelguncha rad etiladi. Qatlam/policy o‘zgarishlari Save + runtime reload bilan qo‘llanadi. Scene fayli
rename qilinganda manifest yo‘lini panelda yangilang. Binary world sidecar, HLOD, polygon navmesh,
hard RSS/VRAM cap va runtime o‘zgargan katak holatini diskka saqlash hali yo‘q.

Eksport configured manifestni va uning scene/asset closure’ini build ro‘yxatiga qo‘shadi; unused asset
filtri shu closure’ni saqlaydi. Missing/unsafe manifest yoki scene eksport preflight’da xato hisoblanadi.


### World terrain va chegaralar

`TerrainTools::GenerateWorldTile` (`Scene/TerrainTiles.h`) bir xil seed/base/amplitude/frequency bilan
absolyut X/Z koordinatalarni namunalaydi. Qo‘shni kataklarda umumiy nuqtalarning balandligi bir xil;
world Y offset local balandlikdan ayiriladi. Natija height limit’dan tashqarida bo‘lsa hech narsa o‘zgarmaydi.
`StitchTiles` faqat translation qilingan, to‘liq umumiy chegarali ikki terrain uchun ishlaydi.
Resolution intervallari butun nisbatda bo‘lishi kerak; mayda chegaralar umumiy qo‘pol polyline’ga moslanadi.
Blend rows ichki qatorlarga tuzatishni asta kamaytiradi. Bu C0 geometriya uzluksizligi; shading normal’lari
kataklar orasida avtomatik umumlashtirilmaydi. Bir katakning boshqa chegarasini keyin o‘zgartirish avvalgi
corner’ga ta’sir qilishi mumkin; katta tile set uchun bir xil world generator’dan foydalaning.

`LockBorderLOD` saqlanadi/copy qilinadi va tashqi patch’larni to‘liq aniqlikda ushlab turadi; ichki LOD
masofaga moslashadi. Kichik, bitta patch’li katak shu sabab to‘liq detalda qoladi. Terrain panelida world
parametrlar, generator va qo‘shni stitch amallari mavjud; edit sahna Undo tarixi orqali qaytariladi.

### Residency va kuzatilgan xotira bosimi

World policy `CacheEntriesPerKind` va `MaxCacheEvictionsPerFrame` orqali model → material → mesh → texture
ketma-ketligida bo‘sh keshni tozalaydi. Avtomatik va qo‘lda trim bir world tick ichida umumiy eviction allowance’dan foydalanadi; thumbnail/reimport olib tashlagan entry’larning LRU metadata’si ham tozalanadi. Har library ichida eski cache acquisition avval chiqariladi; mesh
library OBJ va primitive guruhlarini alohida yuritadi, OBJ oldin tekshiriladi. Jonli `shared_ptr` egalari,
background model/texture va queued mesh upload’lar pin. GPU destructor asosiy oqimda ishlaydi; texture
cache mutex’i ostida GPU resurslari yo‘q qilinmaydi. Thumbnail `GetLoadSerial/ReleaseUnused` semantikasi
LRU sanog‘idan alohida saqlangan. Prefab/audio/font/shader keshlariga bu siyosat tatbiq etilmaydi.

`ObservedRSSLimitBytes` — Linux `/proc` RSS signali; `RequestedGPULimitBytes` — tracked texture,
render target va mesh allocation yig‘indisi. Nol threshold signalni o‘chiradi. Ular physical VRAM o‘lchovi
yoki hard allocation cap emas. Limitga yetganda bo‘sh kesh trim qilinadi, yangi admission/build/resolve
to‘xtaydi; allaqachon jonli obyektlar saqlanadi, keraksiz kataklar esa avval unload qilinadi.
`PressureResumeRatio` low watermark’dan pastga tushganda ish davom etadi (threshold eng yaqin butun baytga yaxlitlanadi, kamida 1). RSS eng ko‘pi yarim soniya
oralig‘ida o‘qiladi; malloc bo‘shatilgan sahifalarni OS’ga darhol qaytarmasligi mumkin. Pin qilingan yoki
staged obyektlar sabab bosim uzoq qolsa kamera/policy/kontentni o‘zgartirish kerak. Kesh scan va bitta
resurs destructor’i preempt qilinmaydi; eviction soni chegaralangan, hard frame deadline kafolati yo‘q.
Editor RSS/GPU histories, pinned/loading/evicted, pressure frames va qo‘lda trim’ni ko‘rsatadi.

### Kataklararo grid navigation

`NavigationMode` 0 (off), 2 (XY), 3 (XZ plus heights); `NavigationMaxCells` union grid xotirasini cheklaydi
(default 262144, policy maximum 1048576). Faqat active streamed kataklardagi active, baked NavGrid’lar
`BuildNavMosaic` bilan birlashtiriladi. Cell size, grid alignment va climb qoidasi bir xil bo‘lishi kerak;
overlap/mixed resolution/budget xatosi panelda ko‘rinadi va eski mosaic o‘chirilib, stale yo‘l saqlanmaydi.
Yuklanmagan hudud blocked; cost, floor height va source carved walkability ko‘chiriladi. Source revision,
carve revision, serial yoki katak to‘plami o‘zgarsa mosaic qayta quriladi; avtomatik agentlar replan qiladi.
Explicit authored grid havolasi ustun qoladi. `GetNavigationEntity(dimension)` faqat world anchor’ga tegishli.
World close/Stop mosaic entity’ni ham yo‘q qiladi. Bu bounded, sinxron grid rebuild; incremental polygon
navmesh, ko‘p qavatli floor va cross-resolution portal tizimi alohida keyingi ishlar.

## World authoring va checkpoint workflow

**Oyna → World Streaming → Yangi 2D / 3D world yaratish** yangi bundle yaratadi: anchor kamera,
3D sun, cell scene’lari, metadata va manifest. 1–256 katak, cell o‘lchami 8–256, terrain resolution
9/17/33/65. Navigation cell size dunyo katagi tomonlarini butun bo‘lishi kerak. Terrain global
koordinatalarda bir xil seed bilan yaratiladi. 2D namuna ground, static obstacle va XY navigation beradi.

`WorldGridSettings::TemplateScene` ixtiyoriy, Assets ichidagi cell-local `.zscene`. Template worker
loader’ning byte/node/symlink himoyasidan o‘tadi; camera va SubScene rad etiladi. Hierarchy va komponentlar
nusxalanib, har cell uchun UUID va ichki entity reference’lar remap qilinadi. Template ishlatilganda
uning terrain/nav kontenti saqlanadi; generator uni avtomatik qayta yaratmaydi.

`CreateWorldGrid` CPU authoring amali: yangi, private staging papkada hamma fayl tayyorlanadi, so‘ng
pinned Assets FD orqali Linux `renameat2(RENAME_NOREPLACE)` bilan birga e’lon qilinadi. Mavjud bundle,
traversal nom, root symlink va noto‘g‘ri parametr rad etiladi. Xatoda staging o‘chiriladi, natija/config
saqlanadi. Editor’da **Loyihaga ulash va anchor ochish** config’ni saqlaydi va anchor’ni ochadi. Alohida
cell scene’ni ochib Play qilishda konfiguratsiyadagi butun world boshlanmaydi; avtomatik world faqat
start/anchor scene uchun ulanadi. Template va grid yaratish explicit, sinxron authoring operatsiyalari;
256 ta katta cell UI’ni vaqtincha band qilishi mumkin.

### Checkpoint holati

`WorldCheckpoint` har cell’ning to‘liq serialized scene nusxasini saqlaydi. Yo‘q entry — authored source;
bo‘sh `Entities` — o‘chirilgan cell kontenti. World manifest identity formatlashdan mustaqil canonical
JSON sifatida to‘liq taqqoslanadi. Boshqa world, duplicate key/ID, noto‘g‘ri hierarchy, SubScene,
symlink/FIFO, buzilgan yoki ortiqcha katta fayl rad etiladi. Limitlar: 8 MiB/cell, 64 MiB jami source,
160 MiB envelope, 32768 entity/cell va 64 JSON depth; runtime policy bundan ham kam entity ruxsat berishi mumkin.
Bu serialized byte limitlar, process RSS hard cap emas. Bitta scene JSON SAX node limiti ham amal qiladi.

`WorldStreaming` host API’lari (callback’dan tashqarida, frame oralig‘ida):

- `SetCaptureOnUnload(true)`: ketayotgan cell’ni RAM snapshotga oladi. Avtomatik disk IO qilmaydi.
- `CaptureCell(id, error)`: bitta active cell; `SaveCheckpoint(file, error)`: active cell’lar + oldingi
  captured holatni explicit sinxron atomik yozish. Saqlash xatosida oldingi disk fayli qoladi.
- `RequestRestoreCheckpoint(file, error)`: fayl va barcha komponentlarni staging scene’da preflight qiladi;
  valid bo‘lgach eski cell’larni frame bo‘yicha unload qilib, snapshotlarni qayta admit qiladi. Yangi IO/parse
  worker’da; eski generatsiyalar cancel/discard qilinadi. Invalid preflight jonli world’ni o‘zgartirmaydi.
- `RequestResetToAuthored(error)`: shu bosqichli retire/load orqali RAM snapshotlarni bo‘shatadi;
  disk checkpoint o‘chirilmaydi.
- `GetCellEntities`, `AdoptEntity`, `RemoveCellEntity`: spawned subtree’ni explicit ownership’ga olish
  va o‘chirish. Boshqa cell’dan entity o‘g‘irlash, quota oshishi yoki aralash ownership rad etiladi.

Root transform checkpoint’da cell-local koordinataga qaytadi; child-local transform va UUID saqlanadi.
O‘chirilgan entity qaytib paydo bo‘lmaydi, adopted actor diskdan ham qaytadi. Capture’da tashqi parent
va ownership’siz child rad etiladi: avval subtree’ni adopt/detach qiling. Automatic capture xatosi cell’ni
jim yo‘q qilish o‘rniga unload’ni to‘xtatadi va panelda sababni ko‘rsatadi. Ownership spatial migration
emas: harakatlanuvchi persistent player’ni anchor’da qoldiring yoki ownership’ni o‘zingiz boshqaring.

Snapshot transform/terrain va serializer saqlaydigan komponent/stored script field’larni qaytaradi.
Arbitrary C++ instance heap, physics velocity/contact replay, timer va tashqi servis holati avtomatik
saqlanmaydi. Derived nav/physics/GPU state activation’da qayta quriladi. Snapshot restore eski asset
versiyasini qaytarmaydi; assetlar joriy loyihadan olinadi. Authoring world fayllari o‘zgarmaydi.

### Script-safe command va fon yozish

Script callback’idan host mutatsiya API’si o‘rniga `WorldStreaming::Enqueue(Command, error)` ishlating.
`CommandKind`: Capture, Save, Restore, Reset, Adopt, Remove. Queue engine-owned data saqlaydi;
hot reload’dan keyin bajariladigan plugin closure saqlamaydi. Serial qaytadi (0 — rad etildi), jami
64 pending, bir tick’da ko‘pi bilan 4 ta boshlanadi. Save IO tugamaguncha keyingi command kutadi.
Restore/Reset command natijasi validated request qabul qilinganini bildiradi; qayta activation odatdagi cell bosqichlarida davom etadi. Actor tayyorligini ham tekshiring.
`WorldCommandCompleted` payload — `CommandResult` (Serial, Kind, Success, Error); stats pending,
completed va failed sonlarini ko‘rsatadi.

Queued Save active cell’larni main thread’da serialized snapshotga oladi, disk serialization/atomic
write’ni `WorldCheckpointWriter`ning yagona jthread’iga beradi. Worker hech qachon ECS, GPU yoki script
instance’ga tegmaydi. Snapshot shared immutable source’lardan iborat; keyingi gameplay edit diskka ketayotgan
nusxani o‘zgartirmaydi. Bir write in flight; destructor boshlangan yozishni join qiladi. IO xatosi oldingi
file’ni saqlaydi va completion’da qaytadi. Capture va restore preflight CPU ishi hali preempt qilinmaydi;
source hajmi/komponent og‘irligi sabab hard frame deadline kafolati yo‘q.

WorldWorkshop2D/3D: **F5** — save queue, **F9** — restore queue. Default file user data’dagi loyiha
papkasida `world-checkpoint.json`; `ZAMIN_WORLD_CHECKPOINT_FILE` test/tool override. Ikki demo unload’da
RAM capture’ni yoqadi. World panelida checkpoint yo‘li, source hajmi, command status, save/restore/reset,
adopt UUID va per-cell runtime entity transform/delete boshqaruvi bor. Disk slotni almashtirish explicit.

`Tests/world_checkpoint_test.py` isolated loyihalarda ikkala Player’da edit/delete/spawn, unload/return,
disk restore oracle’larini, keyin editor wizard, config link, generated world Play va actor restore’ni
haqiqiy GL bilan tekshiradi. `build-review/CheckpointPackage/` bu sessiya dalillarini saqlaydi.
