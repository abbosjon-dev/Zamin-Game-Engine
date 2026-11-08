# 🌍 Zamin Engine

**Zamin** — C++20 da yozilgan 2D/3D o'yin dvigateli (game engine). Editor'i Unity/Unreal uslubida, o'yin mantiqi esa **C++ skriptlarda** yoziladi: editor skriptlarni o'zi kompilyatsiya qiladi va o'yinni to'xtatmasdan qayta yuklaydi (hot reload).

Hozircha **Linux** qo'llab-quvvatlanadi. Windows, macOS, Android va iOS keyingi bosqichlarda qo'shiladi (arxitektura bunga tayyorlangan, pastga qarang).

![Zamin Editor](docs/images/editor.png)

| 2D Platformer (namuna) | 3D Showcase (namuna) |
|---|---|
| ![Platformer](docs/images/platformer2d.png) | ![Showcase](docs/images/showcase3d.png) |

**Adventure3D** — uchinchi shaxs o'yini: animatsiyali robot, character controller, kristallar yig'ish:

![Adventure3D](docs/images/adventure3d.png)

![2D editor](docs/images/editor2d.png)

---

## ✨ Imkoniyatlar

**Editor**
- Docking interfeys: Viewport, Ierarxiya, Inspektor, Kontent brauzer, Konsol, Sahna sozlamalari, Statistika
- **Undo/Redo** (`Ctrl+Z` / `Ctrl+Y`), **Copy/Paste** (`Ctrl+C` / `Ctrl+V`), nusxa olish (`Ctrl+D`)
- **Parent/child ierarxiya**: daraxt ko'rinishi, drag & drop bilan ota-ona belgilash va tartibni o'zgartirish (qator chetiga — oldiga/ortiga, o'rtasiga — ichiga), bola obyekt yaratish
- **Ko'p obyekt tanlash**: `Ctrl`+bosish (qo'shish/olib tashlash), `Shift`+bosish (oraliq), `Ctrl+A`; tanlanganlarni birga o'chirish, nusxalash, sudrash va gizmo bilan ko'chirish/aylantirish/masshtablash (pivot: asosiy obyekt yoki tanlov markazi)
- Inspektorda Transform menyusi (⚙ yoki o'ng tugma): tiklash, nusxalash, joylash (hammasi yoki faqat pozitsiya/aylanish/masshtab) — barcha tanlanganlarga
- **Komponentlar tartibi**: komponent menyusida (⚙ / o'ng tugma) yuqoriga / pastga / eng yuqoriga / eng pastga yoki sarlavhani sudrab qo'yish; tartib har bir obyekt uchun sahnada saqlanadi (Transform doim tepada)
- **Komponentni nusxalash va joylash**: ⚙ → *Komponentni nusxalash*, boshqa obyektda ⚙ → *Qiymatlarni joylash* yoki **+ Komponent → Buferdan joylash** (yo'q bo'lsa qo'shiladi) — barcha tanlanganlarga, tizim buferi orqali boshqa editor oynasiga ham
- Inspektor komponent sarlavhalarining yig'ilgan/ochiq holati eslab qolinadi (komponent turi bo'yicha, skriptlar klass bo'yicha; keyingi ishga tushirishda ham), ⊟/⊞ tugmasi hammasini yig'adi/ochadi
- Ierarxiyada **ko'z** va **qulf** tugmalari: yashirilgan obyekt (va bolalari) editor sahnasida chizilmaydi — o'yinda (Play, Player) odatdagidek ko'rinadi; qulflangan obyekt viewport'da tanlanmaydi va gizmo bilan surilmaydi (ierarxiya/inspektorda tahrirlash mumkin). `H` — tanlanganlarni yashirish/ko'rsatish, `Alt+H` — hammasini ko'rsatish; holatlar sahna faylida saqlanadi (prefab'ga yozilmaydi)
- **Faol/nofaol obyektlar** (`TagComponent::Active`, o'yin holati — editordagi ko'zdan farqli): nofaol obyekt va uning butun ierarxiyasi chizilmaydi (mesh, sprite, matn, tilemap, particle, UI, yorug'lik), fizikada qatnashmaydi (2D/3D body, tilemap collision va qahramon olib tashlanadi, qayta yoqilganda joriy transformda yaratiladi; ichida turgan tomon `OnTriggerEnd`/`OnCollisionEnd` oladi), skripti `OnDisable` oladi va `OnUpdate`/`OnLateUpdate`/taymerlar to'xtaydi (yoqilganda `OnEnable`). Shuningdek: particle tizimi to'xtaydi (zarralar tozalanadi, `EmitParticles` ham ishlamaydi; qayta yoqilganda boshidan, burst bilan boshlanadi), audio manba to'xtaydi (qayta yoqilganda "Boshida ijro" belgilangani boshidan chalinadi), Animator va Sprite Animation yangilanmaydi (vaqt, poza va holat mashinasi joyida turadi, keyin davom etadi), UI tugmalar ko'rinmaydi va bosilmaydi (bosib turilgan tugma bekor qilinadi, sichqoncha ostidagi tugmaga o'tadi), kamera asosiy hisoblanmaydi (kameralarni obyektlarini yoqib/o'chirib almashtirish mumkin; tinglovchi va `ScreenToWorld` ham faol kameradan olinadi), yorug'lik hisobga olinmaydi (yoritmaydi, soya tushirmaydi, quyosh o'rnini yoki 32 ta nuqtali/spot yorug'lik o'rnidan birini egallamaydi — keyingi faol yorug'lik oladi; quyosh — sahna tartibidagi birinchi faol Directional Light, boshqalari ishlatilmaydi (inspektorda aytiladi); 32 tadan ko'p bo'lsa kameraga eng ko'p ta'sir qiladiganlari ishlatiladi; `GetScene().CollectLights(...)`, statistika oynasida ham ko'rinadi), tilemap collision o'chadi (qayta yoqilganda joriy plitkalar va transformdan qayta quriladi), Character Controller 3D dunyodan chiqariladi (gravitatsiya yo'q, `Grounded` false, `Jump` ishlamaydi; qayta yoqilganda joriy joyida, tinch holatda yaratiladi). Olib tashlangan jism ustida uxlab yotgan jismlar uyg'onadi va tushadi, qayta paydo bo'lgan jism ichidagilari ham uyg'onadi. Inspektorda bunday komponentlar ostida ⏻ izoh chiqadi. Holat sahna faylida (`"Active": false`, faqat o'chiq bo'lsa), nusxalarda va prefab'larda saqlanadi: prefab nusxasining har bir obyekti saqlangandagidek faol/nofaol boshlanadi; nofaol shablondan `Instantiate` qilingan nusxa ham nofaol (`SetActive(nusxa, true)` bilan yoqiladi), nofaol guruh ichidagi faol shablon nusxasi esa ildiz obyekt sifatida (shablon turgan joyda) faol yaratiladi. Inspektorda nom yonidagi **belgi** (bir nechta tanlangan bo'lsa hammasiga; o'yin davomida ham ishlaydi) yoki `Alt+Shift+A` bilan yoqiladi/o'chiriladi; ierarxiyada nofaol obyektlar (va ota-onasi nofaol bolalar) xira ko'rinadi, o'zi o'chirilganlarda ⏻ belgisi turadi
- **Prefab'lar** (`.zprefab`): obyektni prefab qilib saqlash, sahnaga/ierarxiyaga sudrab qo'yish (bir nechtasi birga, sichqoncha
  ostiga, Shift — qo'yish rejimi); nusxalar prefabga **bog'langan** (o'zgarishlar inspektorda belgilanadi, Apply / Revert har
  darajada, bog'lanishni uzish), ichma-ich prefab'lar va **variantlar**, **prefab rejimi** (alohida tahrirlash), prefab fayli
  tashqarida o'zgarsa nusxalar yangilanadi (hot reload), undo prefab fayllari bilan, nusxalash buferi bog'lanishni saqlaydi,
  kontent brauzerda prefab rasmlari, eksportda tekshiruv (R1-R10) — batafsil: [docs/Prefabs.md](docs/Prefabs.md)
- 2D sahnalar uchun avtomatik ortografik kamera, 3D collider'larni wireframe ko'rinishda chizish
- Zamonaviy qorong'i tema, Inter shrifti, Font Awesome ikonkalari, o'zbekcha interfeys
- Gizmo'lar (ko'chirish / aylantirish / masshtab), sichqoncha bilan obyekt tanlash (picking)
- **Snapping** toolbar'da (magnit): ko'chirish / aylantirish / masshtab qadamlari va tayyor qiymatlar; `Ctrl` bosib turish vaqtincha teskari qiladi
- **Grid sozlamalari**: katak o'lchami, katta chiziqlar oralig'i, ranglar, o'qlar, ko'rinish masofasi (toolbar'dagi ▾ yoki o'ng tugma)
- **Kamera xatcho'plari**: `Ctrl+1..5` ko'rinishni saqlaydi, `1..5` unga silliq qaytaradi (har bir sahna uchun alohida)
- **Hammasini ko'rsatish** (`Shift+F`) va tanlanganlarni ko'rsatish (`F`): kamera obyektlarning chegaralariga moslanadi
- Viewport'da **nusxalab sudrash**: `Alt` + gizmo'ni sudrash nusxasini sudraydi, sudrash paytida `Ctrl+D` shu joyda nusxa qoldiradi
- Viewport sozlamalari (snapping, grid, xatcho'plar, inspektor sarlavhalari, kontent brauzer ko'rinishi va filtri) foydalanuvchi uchun `~/.config/zamin/editor-view.json` faylida saqlanadi
- **Play / Simulate / Pause / Step** — o'yinni editor ichida ishga tushirish (sahna nusxasida, tahrir buzilmaydi)
- **Sozlamalar oynasi** (`Tahrirlash > Sozlamalar`, `Ctrl+,`): temaning asosiy rangi (8 ta tayyor rang yoki istalgani), shrift o'lchami (12–22 px, butun UI shu nisbatda o'zgaradi, shrift atlasi qayta quriladi), o'yin rejimi rang tusi, konsol va editor kamerasi standartlari (FOV, near/far, uchish tezligi) — `~/.config/zamin/preferences.json`da saqlanadi
- **O'yin rejimida rang tusi**: Play paytida oyna fonlari, sarlavhalar, tablar, menyu va status bar tanlangan rangga ko'paytiriladi (qizg'ish, ko'kish, yashil ... yoki istalgan; kuchi, simulyatsiyada ham) — o'yin rejimidagi o'zgarishlar yo'qolishini unutmaslik uchun
- **O'yin oynasi o'lchami** (Play paytida viewport toolbar'ida yoki `Ko'rinish` menyusida): Erkin, 16:9, 16:10, 4:3, 21:9, 1920x1080, 1280x720 yoki boshqa (piksel yoki nisbat) — rasm atrofida qora chiziqlar bilan joylanadi, sig'magan katta o'lcham kichraytirib ko'rsatiladi (o'yin baribir to'liq o'lchamda chiziladi), sichqoncha o'yin piksellariga to'g'ri o'tkaziladi
- **Konsol**: matn bo'yicha qidiruv (katta-kichik harfsiz yoki farqli, regex `.*` — juda uzun xabarning birinchi 16 KB qismi bo'yicha), daraja tugmalari (topilganlar soni bilan), bir xil xabarlarni **guruhlash** (soni nishonchada), vaqtni yoqish/o'chirish, Play'da tozalash, xabarni bosib tanlash (to'liq matni pastda), `Ctrl+C` / o'ng tugma bilan nusxalash, "shu matn bo'yicha filtrlash"
- **Klaviatura yorliqlari** (`Yordam > Klaviatura yorliqlari`, `F1`): editorning hamma yorliqlari bitta jadvalda (fayl, tahrirlash, tanlov, o'yin, viewport asboblari, kamera, plitka chizish, panellarning o'z tugmalari) — guruhlab ko'rsatiladi, nomi / tugmasi / joyi bo'yicha qidiriladi yoki tugmani bosib topiladi; tugmani bosib **qayta bog'lash** (yoki tugmasiz qoldirish), to'qnashuvlar qizil belgi bilan ko'rsatiladi, menyulardagi yorliqlar ham shu jadvaldan olinadi — o'zgarishlar `preferences.json`da saqlanadi
- **Avtomatik saqlash**: saqlanmagan o'zgarishlar har N daqiqada (sozlanadi, 1–60) `<loyiha>/.zamin/autosave/` ga nusxa sifatida yoziladi — sahna faylining o'zi o'zgarmaydi; editor kutilmaganda yopilsa (loyiha qulf fayli qoladi), sahna keyingi ochilganda nusxa fayldan yangiroq bo'lsa **tiklash** taklif qilinadi (tiklangandan keyin undo fayldagi holatga qaytaradi); *Keyinroq* nusxani chetga oladi (`<nusxa>.kept`) — bu sessiyadagi avtosaqlash va sahnani saqlash unga tegmaydi, sahna keyingi ochilganda yana so'raladi
- **So'nggi sahnalar**: `Fayl > So'nggi sahnalar` (shu loyihaning sahnalari, ochig'i belgilangan) va boshlang'ich oynada barcha loyihalarning so'nggi sahnalari (bir bosishda loyiha ham ochiladi); so'nggi loyihalar ham — `~/.config/zamin/recent-*.json`da saqlanadi, ko'chirilgan sahnalar ro'yxatda ham yangilanadi
- **Undo tarixi** (`Ctrl+H`): har qadam nomi bilan ("Transform.Translation o'zgardi (Player)", "Komponent qo'shildi: Rigidbody2D (Player)", "Nomi o'zgardi: A → B", "Nusxa olindi: ...") — nom sahna suratlarining farqidan avtomatik topiladi; qadamni bosib o'sha holatga o'tish, saqlangan holat belgisi, qaytariladigan qadamlar xira; **tanlov ham undo qilinadi** (har tanlash o'z qadami, `Ctrl+Z` oldingi tanlovni qaytaradi; o'chirilsa ham o'zgarishning undo'si o'sha paytdagi tanlovni tiklaydi); undo bilan faylga teng holatga qaytilsa sahna "saqlangan" bo'ladi
- **Oyna joylashuvlari** (`Oyna > Joylashuv`): editorning o'z joylashuvlari — *Standart*, *2D* (ierarxiya ostida plitka palitrasi, viewport 2D rejimda), *Animatsiya* (pastda katta Animatsiya va Animator grafi), *4 ko'rinish* (viewport + yuqoridan / old / o'ng ortografik ko'rinishlar, 2x2, viewport kattaroq; ierarxiya inspektor ustida) — va o'zingiz saqlagan joylashuvlar (oynalarning joyi va qaysilari ochiqligi, `~/.config/zamin/layouts/*.zlayout`): saqlash, qo'llash, nomini o'zgartirish, yangilash, o'chirish (`Joylashuvlar` oynasi); `Joylashuvni tiklash` standartga qaytaradi
- **Bir nechta sahna ko'rinishi** (`Oyna > Ko'rinishlar`): viewport'dan tashqari yana 3 ta ko'rinish oynasi, har biri o'z kamerasi bilan (standart: yuqoridan, old, o'ng — ortografik): grid, tanlov konturi, transform gizmo, ikonkalar, bosib tanlash, o'yin rejimida ham sahnani o'z kamerasidan ko'rsatadi; ochilganda viewport yoniga joylanadi, kameralar keyingi sessiyada ham saqlanadi
- **Ortografik o'q ko'rinishlari**: har ko'rinish burchagidagi o'q navigatori (X / Y / Z doirachalarini bosish — shu tomondan ko'rinish, markazi — perspektiv / ortografik), toolbar menyusi va numpad tugmalari (`Num 7` yuqoridan, `Num 1` old, `Num 3` o'ng, `Ctrl` bilan teskari tomon, `Num 5` proyeksiya, `Num 0` erkin perspektiv) — kamera silliq aylanib boradi; o'q ko'rinishini aylantirish (Alt + chap tugma) perspektivga qaytaradi, gridning tekisligi ko'rinishga qarab tanlanadi (XZ / XY / YZ)
- **Kamera sozlamalari** (viewport toolbaridagi kamera tugmasi, `Ko'rinish > Editor kamerasi`): uchish tezligi (o'ng tugma bosilganda g'ildirak bilan ham — hamma ko'rinishlarga va keyingi sessiyaga saqlanadi), tezlanish (uzoq uchganda tezlik oshib boradi, ko'pi bilan 4x), sichqoncha sezgirligi, Y o'qini teskari qilish, FOV, near / far
- **Obyekt nomlari viewport'da** (toolbardagi teg tugmasi, `Ko'rinish > Obyekt nomlari`): tanlanganlar yoki hamma obyektlar nomi ustida — kameradan uzoqlashganda xiralashadi (masofa sozlanadi), ustma-ust tushganda tanlangan / sichqoncha ostidagi / eng yaqini qoladi, nomni bosib obyektni tanlash; o'lcham, fon, soni sozlanadi, xohlasa o'yin rejimida ham
- **Sahna statistikasi** (`Oyna > Sahna statistikasi`): obyektlar (faol / nofaol, ildizlar, ierarxiya chuqurligi), komponentlar turi bo'yicha, C++ skriptlar klassi bo'yicha, prefab nusxalari (editorda prefabdan qo'yilgan obyektlar buni eslab qoladi), sahna nomlagan assetlar (rasmlar, modellar, audio, materiallar va ularning rasmlari) — o'lchami, vertex / indeks soni, davomiyligi va **taxminiy xotirasi** (tekstura mip'lari bilan, mesh buferlari, ochilgan tovush); jadvallar istalgan ustun bo'yicha saralanadi, qatorni bosish shu obyektlarni tanlaydi (`Ctrl` — qo'shadi, ikki marta — kamera boradi)
- **Kodni tashqi muharrirda ochish**: skript inspektoridagi `✎` tugmasi (klass `Source/` papkasidan `ZM_REGISTER_SCRIPT(Klass)` bo'yicha topiladi va e'lon qilingan qatorda ochiladi), kontent brauzerda `.cpp` / `.h` ni ikki marta bosish yoki "Tashqi muharrirda ochish", `Skriptlar` menyusidagi klasslar, sahna statistikasidagi skriptlar; buyruq `Sozlamalar > Tashqi muharrir`da (`{file}`, `{line}`, `{col}`, `{dir}` o'rinbosarlari; standart: `$VISUAL` / `$EDITOR` — vim / nano terminal oynasida —, bo'lmasa `code -g {file}:{line}:{col}`, bo'lmasa `xdg-open`), dastur editorni kutdirmay alohida ishga tushadi
- **Kompilyatsiya xatolari** (`Oyna > Kompilyatsiya`, `Skriptlar > Kompilyatsiya natijalari`): skriptlar build'ining chiqishi qatorma-qator ajratiladi — GCC / Clang xato va ogohlantirishlari fayl, qator, ustun bilan, ularning izohlari (`note:`, "required from here"), kod satri va xato joyi belgisi, kontekst ("In member function", include zanjiri), linker xatolari (`undefined reference`, lld'ning `>>> referenced by`), CMake xatolari; takrorlanganlari bir marta; xato / ogohlantirish / izoh filtrlari, qidiruv, to'liq chiqish tabi; ikki marta bosish faylni tashqi muharrirda kerakli qatorda ochadi; build muvaffaqiyatsiz bo'lsa oyna o'zi ochiladi, holat panelida xato / ogohlantirish soni
- **Bo'sh turganda tejash** (`Sozlamalar > Ishlash`): editor oynasi orqada bo'lsa (standart 10 FPS) yoki hech narsa bo'lmayotganda — kiritish yo'q, o'yin / simulyatsiya ishlamayapti, animatsiya yoki particle ko'rib chiqishi ijro etilmayapti, kamera uchmayapti, kompilyatsiya / yuklash / rasmlar yo'q (standart 1 s dan keyin 30 FPS) — kadr oynaning hodisalarini kutadi (`glfwWaitEventsTimeout`): sichqoncha yoki klaviatura darhol to'liq tezlikka qaytaradi; holat panelida barg belgisi bilan `bo'sh turibdi: 30 FPS`; o'chirib qo'yish mumkin; `ZAMIN_MAX_FRAMES` bilan avtomatik ishga tushirishlar hech qachon sekinlashmaydi (`ZAMIN_ALLOW_THROTTLE=1` — sinash uchun). Headless'da 20 s bo'sh turgan editor (2 FPS ga sozlanganda): 54 s → 10.5 s protsessor vaqti
- Editor kamerasi: Unity kabi uchish (RMB + WASD) va Maya kabi orbit (Alt + sichqoncha)
- 2D/3D rejim, infinite grid, fizik collider'larni ko'rsatish, kamera/yorug'lik ikonkalari
- Kontent brauzerdan drag & drop (tekstura → sprite, model → model obyekti, ovoz → Audio Source, prefab, `.zscene` → sahnani ochish)
- **Kontent brauzer**: katakchalar yoki **ro'yxat** ko'rinishi (nomi / turi / hajmi / o'zgartirilgan sanasi bo'yicha saralash), **fayl turi filtri** (sahna, prefab, rasm, model, audio, shrift, HDRI, C++ ...; barcha ichki papkalardan qidiradi), **qidiruv to'liq yo'l bo'yicha** — standart holatda butun loyihada (yoki faqat ochiq papkada; `Ctrl+F`): papka nomlari ham hisobga olinadi (`textures` — `Textures` papkasidagi hammasi, `Textures/` — faqat ichidagilar; bosh `Assets/` va `Source/` papkalari faqat `/` bilan yozilganda hisobga olinadi — `a` yoki `set` butun loyihani emas, `assets/tex` esa kerakli joyni topadi), `tex/coin` — papkalar va nom shu tartibda, `*.png` — `*` istalgan matn, `t:rasm`, `t:model`, `t:audio` — fayl turi; bir nechta so'z — hammasi mos kelishi kerak; natijalar joylashgan papkasi bilan ko'rsatiladi. **Thumbnaillar**: rasm, HDRI (tonemap qilingan), shrift ("Aa") va **model** (3D ko'rinishda chizilgan) eskizlari — fon oqimida tayyorlanadi, GPU'ga kadrga bir nechtadan yuklanadi (katta papka editorni qotirmaydi), `.zamin/Thumbnails` papkasida keshlanadi (loyiha qayta ochilganda darhol chiqadi) va fayl o'zgarganda yangilanadi. `Ctrl`/`Shift` bilan ko'p fayl tanlash; fayllarni **papkaga sudrab ko'chirish** (papka katakchasi/qatori yoki yuqoridagi yo'l bo'laklariga) va nomini o'zgartirish — sahna/prefab'lardagi yo'llar, glTF modellardagi nisbiy fayl nomlari, ochiq sahna, undo tarixi va boshlang'ich sahna avtomatik yangilanadi (C++ kodidagi eski yo'llar haqida ogohlantiradi)
- **Tashqaridan import**: fayl yoki papkani editor oynasiga tashlang — `Assets`ga nusxalanadi (kontent brauzer ustiga — ochiq papkaga, aks holda turiga qarab `Textures/`, `Models/`, `Audio/` ...; C++ fayllar `Source/`ga); glTF bog'liq fayllari bilan o'z papkasiga ko'chadi; viewport ustiga tashlansa sahnaga obyekt ham qo'shiladi
- **Asset tizimi**: har bir asset (sahna, prefab, rasm, model, audio, shrift, HDRI, material) yonida `<fayl>.zmeta` fayli — doimiy **UUID**, turi va import sozlamalari; editor yetishmaganlarini loyiha ochilganda yaratadi (player faqat o'qiydi), kontent brauzerda ko'rinmaydi va asset bilan birga ko'chadi / nomlanadi / o'chiriladi; nusxalangan `.zmeta` yangi UUID oladi. Kontent brauzerda sichqoncha ustida UUID ko'rinadi
- **UUID havolalar**: sahna / prefab fayllarida har bir asset yo'li yonida uning UUID'si ham yoziladi (`"Texture"` + `"TextureUUID"`, loyiha faylida `StartSceneUUID`); fayl editor yopiq paytda tashqarida qayta nomlansa yoki ko'chirilsa (`.zmeta` bilan), yuklashda yangi yo'l UUID orqali topiladi (konsolga yoziladi, sahna "o'zgargan" deb belgilanadi). Editor ochiq paytda tashqarida ko'chirilgan assetlar aniqlanadi va sahna / prefab / glTF fayllaridagi havolalar darhol yangilanadi
- **Asset inspektori va import sozlamalari**: kontent brauzerda fayl tanlansa Inspektor o'sha assetni ko'rsatadi (yo'l, UUID, hajm, sana; papka — fayllar soni, hajmi va turlari; bir nechta tanlansa — umumiy ma'lumot). **Rasm**: ko'rinish (R / G / B / A kanallari kulrangda, RGB — shaffofliksiz, 1:1 ... 8:1 kattalashtirish, sichqoncha ostidagi piksel qiymati), o'lcham, fayldagi kanallar va bit chuqurligi, GPU formati, mipmap darajalari, taxminiy video xotira. **Model**: aylanadigan 3D ko'rinish (sudrash — aylantirish, g'ildirak — yaqinlashtirish), animatsiyalar ro'yxati (davomiylik, kanallar; tanlab o'ynatish va vaqt slayderi), submeshlar (uchlar, uchburchaklar, material rangi va xaritalari), skelet bo'g'inlari, fayldagi materiallar va bog'liq fayllar (topilmaganlari qizil). **Audio**: to'lqin shakli — g'ildirak bilan kattalashtirish (sichqoncha atrofida), Shift + g'ildirak / o'rta tugma bilan surish, pastda butun ovoz chizig'i (ko'rinish oynasi bilan), vaqt o'lchagichi, bosib / sudrab **scrub** (shu joydan ijro), ijro kursori (kattalashtirilganda kursorga ergashadi), **takrorlash oralig'i** (Shift + sudrash bilan belgilanadi, chetlari sudraladi, o'ng tugma menyusi); ijro / to'xtatish, takrorlash, ovoz balandligi; format, davomiylik, kanallar, chastota, bitreyt. **Sahna / prefab**: obyektlar soni, ildiz obyektlar, komponentlar; **shrift**: atlas; **C++ fayl**: qatorlar va ro'yxatdan o'tgan skript klasslari. Har bir asset uchun **Ishlatilgan joylar** (qaysi sahna, prefab, model, loyiha fayli yoki C++ kod — yo'l, UUID, model fayli yoki kod orqali; bosilsa kontent brauzerda ochiladi) va sahna / prefab / model uchun **Ishlatadigan assetlar** (buzilgan havolalar bilan). Qo'llanmagan import sozlamalari bilan boshqa narsa tanlansa, ularni qo'llash yoki tashlab yuborish so'raladi. **Rasm**: filtr (silliq / piksel), wrap (takrorlash / chetda / ko'zgu), mipmap, sRGB (o'chirilsa — chiziqli ma'lumot, masalan maska); **model**: masshtab (sm -> m), yuqori o'q (Y / Z — skelet va animatsiyalar bilan aylantiriladi), UV aylantirish; **audio**: yuklash usuli (avtomatik / xotiraga / oqim), oldindan yuklash, faylning balandligi, takrorlash oralig'i (`LoopStart` / `LoopEnd`: takrorlanadigan ovoz avval boshidan oraliq oxirigacha chalinadi, keyin oraliq qayta-qayta — intro + loop; musiqa ham; `AudioEngine::SetLoopRegion`). **Qo'llash** — `.zmeta`ga yoziladi va asset darhol qayta import qilinadi (sahnadagi obyektlar o'sha zahoti yangilanadi), **Bekor qilish** — tahrirni tashlaydi. Player ham shu sozlamalar bilan yuklaydi
- **O'chirishdan oldin ogohlantirish**: ishlatilayotgan asset (yoki shunday assetli papka) o'chirilmoqchi bo'lsa, tasdiqlash oynasida qaysi sahna, prefab, model yoki C++ fayl uni ishlatishi ro'yxati chiqadi (havolalar soni bilan; birga o'chadigan fayllar hisobga olinmaydi; bosilsa o'sha fayl ko'rsatiladi) va tugma **Baribir o'chirish** bo'ladi
- **Import xatolari**: yuklanmagan asset (topilmagan, buzilgan yoki qo'llab-quvvatlanmaydigan rasm, model, shrift, HDRI, audio, sahna, prefab) konsolga **bir marta** — yo'li va sababi bilan (`Rasm yuklanmadi: 'Textures/x.png' - noma'lum rasm formati (stb: ...)`) yoziladi, har kadr yoki har yuklashda takrorlanmaydi (sabab o'zgarsa, fayl o'zgarsa yoki muvaffaqiyatli yuklansa — yana); o'zgarmagan buzilgan fayl qayta-qayta dekodlanmaydi. Kontent brauzerda bunday faylda qizil belgi va pastda **N ta import xatosi** ro'yxati (bosilsa fayl ochiladi), Inspektorda sabab va **Qayta urinish** tugmasi
- **Asset registry keshi**: registry (UUID, tur, yo'l, fayl hajmi va vaqti, import sozlamalari) bitta JSON hujjat sifatida `.zamin/AssetRegistry.json`ga saqlanadi. Editor yopiq paytda tashqarida ko'chirilgan yoki nomi o'zgargan fayllar keyingi ochilishda taniladi — hatto `.zmeta`siz ko'chirilgan bo'lsa ham (tur, hajm va vaqt bo'yicha, faqat bir xil mos kelsa) UUID va sozlamalari tiklanadi, eski `.zmeta` olib tashlanadi va sahna / prefab / glTF fayllaridagi havolalar yangilanadi. Editor ishlayotganda shell'da `.zmeta`siz qayta nomlangan fayl ham shunday taniladi. **O'yinni build qilish** `AssetRegistry.zreg` faylini yozadi — eksport qilingan o'yin har bir `.zmeta`ni o'qimasdan registry'ni shundan oladi (fayllar o'zgargan bo'lsa papkani o'qiydi). `.zamin/` papkasi versiya nazoratiga qo'shilmaydi
- **Ishlatilmagan assetlar** (Oyna menyusi yoki kontent brauzerda o'ng tugma): hech bir sahna, prefab, material, loyiha fayli, `.gltf` model yoki C++ koddagi satr (`"Prefabs/Ball.zprefab"`) havola qilmaydigan assetlar — o'lchami bilan; ochiq sahnaning saqlanmagan holati ham hisobga olinadi. **Chuqur tahlil**: faqat boshlang'ich sahna va koddan (sahnalar, prefablar, modellar orqali) yetib boriladigan assetlar ishlatilgan hisoblanadi. Tur filtri, qidiruv, belgilanganlarni tasdiq bilan o'chirish (`.zmeta` bilan) va **buzilgan havolalar** ro'yxati (mavjud bo'lmagan fayllarga havolalar)
- **Fayl kuzatuvchi va qayta yuklash**: `Assets` papkasi kuzatiladi (Linux'da inotify, aks holda har soniyada tekshiruv); tashqi dasturda o'zgartirilgan **rasm, model (glTF / OBJ, `.bin` buferi ham), material (`.zmat`), shrift va HDRI** fayllar editor'da darhol (o'sha obyektning o'zida — sprite, tilemap, model va uning teksturalari, skelet va animatsiyalar, osmon/yorug'lik, eskizlar) yangilanadi; buzilgan fayl xatosi konsolga chiqadi va avvalgi rasm qoladi; yangi / o'chirilgan fayllar ro'yxatga avtomatik tushadi. O'ng tugma → **Qayta yuklash** — qo'lda
- **Status bar**: tanlangan obyekt nomi, joylashuvi, komponentlar va bolalar soni, yashirin/qulf belgisi (bosish — kamerani yo'naltirish, sichqoncha ustida — batafsil, o'ng tugma — nom/UUID/yo'lni nusxalash); ko'p tanlovda soni va markazi
- C++ skript maydonlari (`ZM_FIELD`) inspektorda tahrirlanadi, o'yin paytida jonli o'zgartiriladi
- Loyiha launcher'i: yangi loyiha (shablondan), so'nggi loyihalar, namuna o'yinlar
- **Profiler** (ierarxik): har bir tizim (skriptlar, fizika, animatsiya, render, post-processing, UI) uchun CPU vaqti.
  Ichma-ich `ZM_PROFILE_SCOPE` bo'limlari daraxt bo'lib yig'iladi: Statistika panelida yoki Oyna > Profiler oynasida
  har bo'limning jami vaqti, o'z vaqti (ichki bo'limlarsiz), chaqiruvlar soni va kadrdagi ulushi; o'rtacha (silliqlangan)
  yoki oxirgi kadr, **Muzlatish** bilan bir kadrni ushlab ko'rish. Bir xil nomli bo'lim bir ota ostida bitta tugun,
  boshqa ota ostida alohida. `Profiler::Find("Kadr/Layer'lar (update)/Sahna")`, `GetHotspots`, `FormatTree`;
  `ZAMIN_EDITOR_STATS=1` / `ZAMIN_PLAYER_STATS=1` daraxtni logga ham yozadi
- **Statistika overlay** (viewport ustida, faqat editor; toolbar'dagi spidometr tugmasi yoki Ko'rinish > Statistika
  overlay): FPS va kadr vaqti, so'nggi 240 kadr grafigi (maqsad FPS chiziqlari, ranglar: yashil - sig'di, sariq - ikki
  baravarigacha, qizil - sekinroq), eng sekin 1% kadrlar, draw call'lar (sahna va editor alohida), uchburchaklar,
  meshlar / instanced, soyalar, 2D obyektlar, culling kesganlari (ulushi bilan) va eng sekin profiler bo'limlari.
  Burchagi, foni, bo'limlari va maqsad FPS sozlanadi (o'ng tugma yoki menyu), editor sozlamalarida saqlanadi
- **Maximize on Play**: o'yin rejimida viewport butun oynaga yoyiladi
- **O'yinni build qilish**: bitta tugma bilan mustaqil papkaga eksport (`./MeningOyinim.sh`): C++ skriptlar alohida
  build papkasida Release (`-O2`) da qayta kompilyatsiya qilinadi, binarlar `strip` qilinadi (Debug engine bilan
  ~218 MB -> 37 MB), ishlatilmagan assetlar chiqarib tashlanadi; papka qayerga ko'chirilsa ham ishlaydigan `.desktop`
  fayli va 16 ... 256 px ikonkalar (`install-desktop-entry.sh` menyuga va ikonka mavzusiga o'rnatadi); tugagach
  **eksport hisoboti** oynasi (jami hajm, turlar bo'yicha ulush, eng katta fayllar, chiqarib tashlanganlar,
  ogohlantirishlar; `export-report.json` / `.txt`)

**Dvigatel**
- ECS arxitektura (EnTT), UUID, JSON sahna formati (`.zscene`)
- **Renderer** (OpenGL 4.1 core, RHI abstraksiya qatlami orqasida):
  - 2D batch renderer: sprite, tekstura, doira, chiziq, **matn** (UTF-8, lotin/kirill, `oʻ gʻ`)
  - **Sprite atlas va Sprite muharriri** (Oyna > Sprite muharriri, yoki rasm inspektoridagi / Sprite Renderer'dagi ✂
    tugma): teksturani grid bo'yicha kesish (katak o'lchami yoki ustun/qator soni, siljish, oraliq, bo'sh kataklarni
    tashlash, nom prefiksi), rasm ustida sichqoncha bilan yangi kadr chizish, har bir kadrning nomi, to'rtburchagi,
    pivot'i va 9-slice chegaralari; zoom (g'ildirak) va surish. Kadrlar rasmning `.zmeta` faylida (`"Sprites"`) saqlanadi —
    rasm o'zi atlas bo'ladi: Sprite Renderer kadrni nomi bilan ko'rsatadi (`SpriteName`, bo'sh — butun rasm), Sprite
    Animation nomlangan kadrlar ro'yxatini o'ynaydi (`Frames`, inspektorda `run_0, run_1, ...` ketma-ketliklari bitta
    tanlov bilan; skriptdan `PlaySpriteSequence("run")`, `SetSprite("idle")`), eksport qilingan o'yinga registry bilan o'tadi
  - **Sprite animatsiya kliplari** (`.zsanim` asset): rasm, sprite sheet kadrlari yoki grid, FPS, takrorlash va **kadr
    event'lari** (`OnAnimationEvent`, masalan qadam "Step"). Sprite Animation'ning **Kliplar** ro'yxati — birinchisi o'yin
    boshida o'ynaydi va editorda ko'rinadi, skript `PlaySpriteClip("HeroSide")`; asset inspektorida jonli ko'rinish (kadrlar
    qatori, oldinga / orqaga), tahrirlar darhol hamma foydalanuvchilarga o'tadi va saqlanadi. Kontent: o'ng tugma → Yangi
    sprite animatsiya klipi (tanlangan rasmdan); inspektorda "Shu kadrlardan klip". Namuna: TopDown2D qahramoni va gulxani
  - **9-slice va plitkali sprite'lar**: Sprite Renderer'ning chizish rejimi Oddiy / 9-slice (Sliced) / Plitka (Tiled) —
    `Size` (birlik) to'rtburchagini burchaklari cho'zilmay (chegara px / `PixelsPerUnit`) to'ldiradi, Tiled'da chetlar va
    o'rta takrorlanadi; chegaralar komponentda yoki kadrda. Ekran UI'sida ham (panel, tugma — chegara ekran pikselida).
    Oddiy rejimdan o'tilganda masshtab avtomatik `Size`'ga ko'chadi (burchaklar cho'zilmasin)
  - **Saralash qatlamlari** (Loyiha sozlamalari > Saralash qatlamlari): nomlangan qatlamlar ro'yxati (tartibi — chizish
    tartibi), sprite, doira, matn, tilemap va particle'larda qatlam + qatlamdagi tartib; chizish tartibi: qatlam, tartib,
    keyin kameradan uzoqlik. Obyektlar qatlamni ID bilan eslaydi (qayta nomlash/surish buzmaydi, boshqa loyihadan kelgan
    sahna nom bo'yicha topadi). **Y bo'yicha saralash** (qatlamning "Y" belgisi, top-down o'yinlar): qatlam ichida avval Y —
    yuqoriroqdagi orqada; saralash nuqtasi pivot (oyoqlar), shu qatlamdagi ota-obyekt ostidagi qismlar (soya, qalpoq)
    otaning Y'i bilan birga, o'zaro tartib bo'yicha chiziladi
  - **Parallax** komponenti: obyekt (bolalari bilan) o'yin kamerasi bilan `Factor` (0 — oddiy, 1 — ekranda qotgan, manfiy —
    old plan) bo'yicha siljiydi, `RepeatX`/`RepeatY` bilan sprite ko'rinadigan joyni cheksiz to'ldiradi. Transform
    o'zgarmaydi (fizika va skriptlarga ta'sir qilmaydi); editorda Ko'rinish > Parallax bilan editor kamerasida ham.
    Namuna: Platformer2D `Scenes/Showcase2D.zscene`
  - **Camera 2D Follow** komponenti: obyekt (odatda kamera) nishon ortidan fizika va skriptlardan keyin silliq yuradi —
    `Offset`, x/y bo'yicha alohida silliqlik (soniyada, kadr tezligiga bog'liq emas), o'lik zona (nishon uning ichida yursa
    kamera qimirlamaydi), harakat yo'nalishi bo'yicha oldinga qarash (`LookAhead`), dunyo chegaralari (kameraning butun
    ko'rinishi to'rtburchakdan chiqmaydi), boshida nishonga sakrash va `Snap()`. Editorda tanlanganda o'lik zona va
    chegaralar chiziladi; skriptdan `SetCameraTarget(entity)`
  - **Kamera silkinishi**: `ShakeCamera(kuch, davomiylik, chastota)` (skriptdan yoki `Scene::ShakeCamera`) — trauma
    modeli: kuch qo'shiladi (eng ko'pi 1), davomiylik ichida so'nadi, siljish = kuch^daraja × `MaxOffset` / `MaxAngle`
    (kamera komponentida sozlanadi), silliq shovqin bo'yicha; faqat tasvir silkinadi — transform, kuzatish, fizika
    o'zgarmaydi; bir xil seed bilan aynan takrorlanadi. Inspektorda o'yin paytida "Sinash" tugmasi
  - **Hit flash va kontur** (Sprite Renderer > Effektlar): `FlashAmount` rasmni `FlashColor` rangiga aralashtiradi,
    skriptdan `Flash(rang, soniya)` — darhol to'liq, so'ng so'nib boradi (pauzada kutadi); `OutlineThickness` (tekstura
    pikseli) — rasmning ko'rinadigan piksellari atrofida `OutlineColor` chegarasi (shader qo'shni alfalarni tekshiradi,
    quad kontur kengligicha kattalashadi, atlas kadridan tashqariga chiqmaydi; bitta bo'lak bo'lib chiziladigan sprite'larda:
    Simple, chegarasiz Sliced/Tiled ham), `SetOutline(rang, qalinlik)`.
    Ekran UI sprite'larida ham ishlaydi; sozlanmagan sprite'lar avvalgidek chiziladi
  - **Tint animatsiyasi** (Tint Animation komponenti): renderer rangi (Sprite, Circle, Text, Tilemap va Light 2D) x gradiyent
    rangi — kalitlar (vaqt 0..1 + rang), davomiylik, Bir marta / Takror / Borib-qaytish, easing, sikllar soni, boshlanish
    siljishi; renderer'ning o'z rangi o'zgarmaydi. Inspektorda gradiyent chizig'i va "Sahnada ko'rish". Skriptdan:
    `PlayTint(rang, soniya, rejim, sikllar)` (masalan miltillash), `StopTint()`, `TweenColor(rang, soniya)`, `FadeTo(alfa, soniya)`
  - **2D yorug'lik** (Light 2D komponenti): nuqta yoki konus (obyektning +X o'qi bo'ylab), rang, kuch, radius, ichki radius,
    so'nish darajasi, miltillash (mash'al); sahnaning 2D muhit yorug'ligi (Sahna sozlamalari > 2D yorug'lik). Faqat "Lit"
    belgilangan sprite va tilemap'lar yoritiladi (shader ichida, eng yaqin 32 ta yorug'lik qo'shiladi) — eski sahnalar
    o'zgarmaydi. Editorda tanlanganda radius/konus chiziladi
  - **Pixel-perfect kamera** (ortografik kamera > Pixel-perfect): piksel/birlik va asosiy o'lcham (masalan 320x180) —
    kattalashtirish shu o'lcham sig'adigan eng katta butun son; "past o'lchamda chizish" — sahna art o'lchamida chiziladi
    va aniq piksellar bilan kattalashtiriladi (UI esa ekran o'lchamida, tiniq); kamera (ixtiyoriy sprite'lar ham) piksel
    to'rida. Namuna: Platformer2D `Scenes/TopDown2D.zscene` — tungi qishloq: Y bo'yicha saralash, fonarlar, gulxan va
    kristallar yorug'ligi, tint animatsiyalar, auto-tile yo'lak, 9-slice HUD, pixel-perfect kamera (WASD)
  - 3D PBR (metallic/roughness), yo'naltirilgan quyosh, gradient osmon, atrof-muhit aksi
  - **Bir nechta quyosh**: barcha faol Directional Light'lar (bir vaqtda 4 tagacha) sahnani yoritadi, soyani esa faqat
    bittasi beradi — sahna tartibida "Soyalar" yoqilgan birinchisi (asosiy quyosh; osmondagi quyosh diski ham unga
    ergashadi). Inspektorda har bir quyoshning roli yoziladi ("Soya bermaydi: soyani '...' beradi"), Statistika panelida
    asosiy quyosh va soyasiz quyoshlar soni
  - **Osmon gradienti va quyosh diski** (HDRI bo'lmaganda; Sahna panelining "Osmon gradienti va quyosh" bo'limi, sahnada
    saqlanadi): zenit / ufq / yer ranglari, zenit egriligi (tepa rangi qanchalik pastga tushadi), yerga o'tish (ufq
    chizig'ining kengligi), quyosh diski (o'lchami gradusda, yorqinligi, nur gardishi) — asosiy quyosh yo'nalishida va
    rangida. Gradient atrof yorug'lik va akslarda ham ishlatiladi. Namuna: `Showcase3D` ichidagi `Scenes/Sunset.zscene`
    (3 ta quyosh, quyosh botishi osmoni, bloom rangi, linza changi, xromatik aberratsiya, plyonka donasi)
  - **Nuqtali va spot yorug'liklar** — birgalikda 32 tagacha: **Spot Light** (obyektning -Z o'qi bo'ylab konus, ichki /
    tashqi burchak orasida yumshoq so'nish, masofa bo'yicha nuqtali yorug'lik kabi so'nish). Ko'proq bo'lsa kameraga eng
    ko'p ta'sir qiladiganlari tanlanadi (kuchi x rangi, kamera yorug'lik radiusidan tashqarida bo'lsa x (radius /
    masofa)^2; teng ta'sirda eng yaqini) — shuning uchun oddiy "eng yaqinlari"dan farqli, uzoqdagi kuchli yorug'lik
    yaqindagi kuchsizidan ustun bo'lishi mumkin, kamerani o'z radiusi ichida qamragan yorug'lik esa to'liq kuchi bilan.
    Editorda tanlangan nuqtali yorug'likning radiusi (3 ta aylana) va spot konusi (ichki konus xiraroq) ko'rinadi,
    viewport'da spot belgisi bor. Namuna: `Projects/Showcase3D` ichidagi `Scenes/Lights.zscene` (29 ta yorug'lik)
  - **HDRI osmon + IBL**: `.hdr` panorama — diffuz yorug'lik SH9 orqali, aks (specular) mip darajalari bo'yicha
  - **Cascaded Shadow Maps** (texel snapping), **tuman** (masofa + balandlik). Soya sifati sahna sozlamalarida (Sahna
    panelining "Soyalar" bo'limi, sahna faylida saqlanadi): atlas o'lchami (1024 / 2048 / 4096), filtr (o'chiq, PCF 3x3,
    PCF 5x5, Poisson 16 nuqta), kaskadlar soni (1-4, bitta kaskad butun atlasni oladi) va taqsimoti (teng .. logarifmik),
    soya masofasi, chuqurlik bias va normal bias. Standart qiymatlar (4096, PCF 3x3, 3 kaskad) avvalgi ko'rinishni saqlaydi
  - Primitivlar (kub, shar, tekislik, silindr, konus), `.obj` va `.gltf/.glb` modellar
  - **HDR pipeline**: sahna float bufferga chiziladi, tonemapping (ACES / Reinhard / yo'q), haqiqiy HDR bloom
  - Material xaritalari: **normal map**, metallic/roughness map, emissive map; tekstura **takrorlash va siljish** (vec2)
  - **Material assetlari (`.zmat`)**: JSON fayl — shader (Lit / Unlit), aralashtirish rejimi (Opaque / Cutout / Transparent),
    alfa chegarasi, ikki tomonlama, rang, metallik, g'adir-budurlik, nurlanish (rang + kuch), normal / metal-roughness /
    nurlanish teksturalari (loyiha yo'li + UUID), takrorlash va siljish. Bitta fayl — bitta umumiy obyekt (`MaterialLibrary`):
    Mesh Renderer'da **Material asset** maydoni (primitiv va modelning barcha qismlari, yoki har bir submesh uchun alohida)
    ichki materialni almashtiradi; eski sahnalar avvalgidek yuklanadi. Fayl yoki uning teksturalari o'zgarsa joyida qayta
    yuklanadi. Inspektorda **material muharriri**: aylanadigan preview (shar / kub / tekislik / silindr, osmon gradienti
    yoki sahna HDRI'si bilan), o'zgarishlar sahnada darhol ko'rinadi, **Saqlash** / **Bekor qilish**, saqlanmagan belgisi.
    Ichki materialni bir tugma bilan `.zmat`ga aylantirish mumkin
  - **Sirt turlari** (material asset va ichki materialda): **Opaque** — alfa e'tiborga olinmaydi; **Cutout** — alfasi
    (rang x tekstura) chegaradan past piksellar chizilmaydi, soyada ham shu teshiklar bo'ladi (barglar, panjaralar);
    **Transparent** — shaffof sirtlar opaque sahna va osmondan keyin, avval materialning **ustuvorligi** (Sort Priority,
    -100..100: kattasi boshqalar ustida chiziladi), keyin chegaralari markazining kameradan uzoqligi bo'yicha (ortografik
    kamerada — ko'rish yo'nalishi bo'yicha) orqadan oldinga saralanib, chuqurlik yozmasdan premultiplied alfa bilan
    chiziladi, soya tashlamaydi. Ko'rinish menyusidagi **Shaffoflar tartibi** viewport'da har bir shaffof sirt ustiga uning
    chizilish raqami, uzoqligi va ustuvorligini yozadi (o'yin rejimida ham), Statistika panelida — shaffof meshlar soni; **Aksni saqlash** yoqilsa aks va yaltirashlar
    alfa bilan susaymaydi (shisha), o'chirilsa butun sirt so'nadi. **Ikki tomonlama**: ikkala tomon chiziladi, orqa tomon
    teskari normal bilan yoritiladi (ikki tomonlama shaffof sirt — avval ichki, keyin tashqi tomoni); bayroq o'chiq bo'lsa
    orqa tomonlar kesiladi (manfiy masshtabli ko'zgu obyektlarda ham to'g'ri). **Unlit**: faqat rang x tekstura + nurlanish,
    yorug'lik va soya ta'sir qilmaydi. glTF `alphaMode` (MASK / BLEND), `alphaCutoff`, `doubleSided` va
    `KHR_materials_unlit` o'qiladi; eski sahnalarda alfasi 1 dan kichik rang avtomatik Transparent bo'ladi. Namuna:
    `Projects/Showcase3D` ichidagi `Scenes/MaterialGallery.zscene`
  - **Materiallar bilan ishlash**: kontent brauzerda o'ng tugma — **Yangi material** (standart `.zmat` takrorlanmas nom bilan
    yaratiladi, tanlanadi va inspektorda ochiladi). `.zmat`ni viewport'dagi mesh ustiga sudrash — qo'yishdan oldin material
    o'sha meshda ko'rinadi (ko'k ramka va yorliq), qo'yilsa Mesh Renderer'ga yoziladi (bitta undo qadami); ko'p qismli
    modelga **Shift** bilan qo'yilsa qaysi qism (submesh) olishini so'raydi. Ierarxiya qatoriga yoki Material asset maydoniga
    ham tashlash mumkin (faqat mos fayl qabul qilinadi). Model inspektorida **.zmat ga eksport qilish**: glTF materiallari
    `<model>_Materials` papkasiga `.zmat` bo'lib yoziladi, fayl ichidagi (.glb / data URI) rasmlar PNG/JPG bo'lib chiqariladi;
    Mesh Renderer'dagi **Modeldan .zmat yaratib biriktirish** ularni har bir qismga biriktiradi
  - **Material instansiyalari (skript)**: `GetMaterial()->SetColor(...)` — obyektning materialdan o'z nusxasi (birinchi
    chaqiriqda yaratiladi), o'zgarishi faqat shu obyektda ko'rinadi, umumiy `.zmat` va boshqa obyektlar o'zgarmaydi; o'yin
    to'xtaganda yo'qoladi. `GetMaterial(qism)`, `GetSharedMaterial()` (umumiy asset — o'zgartirilsa hammasi o'zgaradi, editorda
    o'yin to'xtaganda asl holiga qaytadi), `SetSharedMaterial("Materials/Red.zmat")`, `ResetMaterial()`. Tezkor tahrirlar:
    `SetColor`, `SetOpacity`, `SetEmission`, `SetMetallic`, `SetRoughness`, `SetTiling`, `SetOffset`, `SetAlbedoMap`.
    `SetEmissionColor`, `SetEmissionStrength`, `SetSortPriority`.
    Namuna: Showcase3D minorasidagi har bir g'isht `Clay.zmat`ning o'z nusxasini qavatiga qarab bo'yaydi
  - **Nurlanish (emissive)**: material rangi x kuchi (x nurlanish teksturasi) — HDR: 1 dan katta natija bloom yoqilgan sahnada
    porlaydi. Inspektorda bitta qatorda rang, kuch va **HDR / LDR** belgisi; nurlanishi o'chiq materialga nurlanish teksturasi
    berilsa nurlanish oq x 1 bo'ladi (aks holda tekstura ko'rinmasdi). Material preview va thumbnaillarda ham bloom bor;
    glTF `emissiveFactor` va `KHR_materials_emissive_strength` o'qiladi
  - **Standart materiallar kutubxonasi** (`MaterialPresets`): metall, cho'tkalangan metall, oltin, mis, plastik, rezina,
    yog'och, shisha, keramika, beton, neon, suv. Yog'och, beton va cho'tkalangan metall teksturalari protsedural
    (CPU'da, choksiz takrorlanadi) va loyihada bir marta `Assets/Materials/Standart/Textures/` ga PNG bo'lib yoziladi - har
    papkadagi nusxalar shu bitta teksturani ishlatadi. Kontent brauzerda
    **Yangi material** menyusidan tanlanganining nusxasi shu papkada yaratiladi; Mesh Renderer'dagi **Standart material...**
    loyihadagi umumiy nusxasini (`Assets/Materials/Standart/`) biriktiradi. Namuna: `MaterialGallery` sahnasidagi sharlar qatori
  - **Material thumbnaillari**: kontent brauzerda va Material asset maydonlarida `.zmat` shar ustida (yoritilgan, shaffof /
    kesilganlari shaxmat fon bilan, nurlilari bloom bilan) ko'rinadi; teksturasi o'zgarsa ham yangilanadi, ochiq material
    tahrir qilinayotganda jonli kuzatiladi, diskdagi keshga faqat fayldagiga mos holati yoziladi
  - **glTF 2.0** modellar (`.gltf` / `.glb`): bir nechta mesh, PBR materiallar, ichki (embedded) teksturalar
  - **Skeletal animatsiya**: glTF skin'lari, suyaklar, animatsiya kliplari (GPU skinning, soyalar bilan), `Animator` komponenti,
    kliplar orasida **silliq o'tish** (crossfade): `anim.Play("Run", true, 1.0f, 0.2f)`; **event'lar** (klipning 0..1 nuqtasi,
    nom va parametr — skriptga `OnAnimationEvent`), **root motion** (ildiz suyakning XZ yurishi va Y burilishi obyektni yoki
    Character Controller 3D'ni siljitadi, poza joyida qoladi; suyak avtomatik yoki tanlanadi)
  - **Blend tree 1D**: holat turi "Blend tree 1D" — float parametr (masalan `Speed`) chegaralari bo'yicha kliplar
    aralashadi (Idle 0 / Walk 3 / Run 6.5), hammasi bitta normalizatsiyalangan vaqtda o'ynaydi (oyoqlar fazada qoladi),
    sikl davomiyligi og'irliklar bo'yicha; boshqa holatlarga crossfade, exit time, root motion va event'lar (eng og'ir klipdan)
    bilan. **Qatlamlar** (Animator > Qatlamlar): o'z klipi, og'irligi (parametr bilan ham), **avatar maska** (suyak va undan
    pastdagilar, masalan `Spine` — ustki tana), Override yoki Additive; skript: `PlayLayer("UpperBody", "Wave")`,
    `SetLayerWeight("UpperBody", 0.0f, 0.3f)`, `SetState("Jump")`
  - **Animator grafi** (Oyna > Animator grafi, yoki inspektordagi tugma): holatlar node'lar (klip / blend tree, boshlang'ich —
    to'q sariq), o'tishlar strelkalar, "Har qanday holat" node'i; node'larni sudrash (joylari sahnada saqlanadi), Shift +
    sudrash yoki menyu bilan o'tish chizish, strelka / node bosilsa o'ng panelda tahrirlash (shartlar, fade, exit time, blend
    motion'lari), Delete, g'ildirak — masshtab, o'rta / o'ng tugma — surish, undo. O'yin davomida jonli: hozirgi holat va
    uning vaqti, o'tish strelkasi, blend og'irliklari; inspektorda ham **jonli holat** (normalizatsiyalangan vaqt, o'tish
    jarayoni, og'irliklar, qatlamlar) va parametrlarni o'zgartirish (trigger tugmasi). Namuna: Showcase3D
    `Scenes/Animator.zscene` (blend tree + Any State trigger, Bone2 maskali qatlam), Adventure3D robot (Idle/Walk/Run blend tree)
  - **Skelet kliplarini editorda ko'rib chiqish** (Animatsiya oynasi, skelet rejimi — Animator'li obyekt tanlanganda):
    modelning glTF kliplari, vaqt chizg'ichini bosib / sudrab **scrub** (sahnada o'yin rejimisiz ko'rinadi), o'ynatish /
    to'xtatish, takror, tezlik, kadrlarga yopishish, masshtab / surish; klip harakatlantiradigan suyaklar va blend shape'lar
    kalitlari bilan qatorlar (qator bosilsa — shu vaqtdagi T/R/S yoki og'irliklar); **Animator event'lari** shu klip uchun
    vaqt chizig'ida qo'shiladi (ikki marta bosish, qo'ng'iroqcha tugmasi), sudraladi, nomlanadi, o'chiriladi (undo bilan)
  - **Suyakka biriktirish** (`Bone Attachment` komponenti): obyekt skinned modelning suyagiga ergashadi (qo'lda qilich, boshda
    shlyapa) — suyak Animator pozasi bilan (editorda ham) obyekt otasining o'rnini egallaydi, Transform suyakka nisbatan
    siljish bo'ladi (gizmo ham suyak fazosida); model obyekti tanlanmasa eng yaqin skinned ota, suyak masshtabi ixtiyoriy
  - **Qadam tovushlari** (`Footsteps` komponenti): animatsiya event'i (odatda `Footstep`) kelganda ovozlardan tasodifiysi
    (ketma-ket takrorlanmaydi) — tezlik (Animator float parametri) bo'yicha balandroq va yuqoriroq tonda, tasodifiy
    balandlik / ton oralig'i, sekinda jim, yaqin event'lar bitta qadam, 3D ovoz, mixer guruhi
  - **Kliplarni kesish va bo'lish** (model `.zmeta` import sozlamalari, asset inspektorida): fayldagi klipning boshlanish /
    oxiri (soniya va kadr, chetlari sudraladigan oraliq chizig'i) — shu nomli bo'lsa klip kesiladi, boshqa nom bilan alohida
    klip bo'ladi (bitta uzun yozuvdan Walk / Run / Jump); qo'llashdan oldin ko'rib chiqish, asl kliplarni saqlash tanlovi
  - **glTF morph target'lar** (blend shape'lar): target'larning joy / normal siljishlari va boshlang'ich og'irliklari
    o'qiladi, CPU'da qo'llanadi (har obyekt o'z og'irliklari bilan o'z mesh nusxasini oladi, skinning bilan ham), glTF
    `weights` animatsiya kanallari Animator kliplarida o'ynaydi (crossfade, blend tree bilan); Mesh Renderer inspektorida
    slayderlar, skript: `GetComponent<MeshRendererComponent>().SetMorphWeight("Smile", 0.8f)`. Namunalar: Showcase3D
    `Scenes/Animator.zscene` (morph'li `Blob.gltf`: Animator klipi va skript, tentakl uchidagi kristal — `Bone3` ga
    biriktirilgan), Adventure3D robot (qo'lidagi fonar, Walk/Run qadam event'lari va `Footsteps`), `Tentacle.gltf` (`WaveHalf`
    kesilgan klip)
  - **Property animatsiya** (`.zanim` kliplar, **Animation Player** komponenti): obyekt va uning bolalarining (nom yo'li bilan,
    `"Eshik/Oshiq"`) transformi, Sprite / Circle / Text / Mesh rangi, yorug'lik rangi va kuchi, UI joylashuvi / o'lchami /
    masshtabi / shaffofligi, UI rasm rangi va skriptning float maydonlari; kalitlar **Step / Linear / Bezier** (avtomatik
    silliq tangentlar — cho'qqida oshib ketmaydi, tekis yoki qo'lda), klip **bir marta / takror / borib-qaytish**, tezlik
    (manfiy — orqaga), **event'lar** (har biri o'tilganda aynan bir marta, katta kadrda ham). Skript: `PlayAnimation("EshikOchilishi")`,
    `PlayAnimation("EshikOchilishi", -1.0f)` (yopish), `StopAnimation()`, `IsAnimationPlaying()`, komponentda `SetTime`,
    `GetNormalizedTime`; `OnAnimationEvent(e)` yoki `AddAnimationEventHandler("Pulse", fn)`, event bus'da `"AnimationEvent"`
  - **Animatsiya oynasi** (Oyna > Animatsiya, timeline): treklar ro'yxati (+ Trek qo'shish: obyekt va xususiyat), **dope sheet**
    (kalitlar romb, sudrab ko'chirish, ikki marta bosish — kalit, Delete, o'ng tugma — interpolyatsiya / tangent) va **egri
    chiziqlar** (tanlangan trekning komponentlari, kalit nuqtalari va Bezier tutqichlari sudraladi), event'lar qatori, vaqt
    chizg'ichi (scrub), g'ildirak bilan masshtab va surish, kadrlarga yopishish, klip davomiyligi / fps / tugash rejimi.
    **Play rejimisiz ko'rib chiqish**: vaqt surilganda yoki ▶ bosilganda sahna ko'rinishida klip qo'llanadi, oyna yopilganda
    (yoki saqlash / undo / Play) sahnaning o'z qiymatlari qaytadi. **Yozish** (●): inspektor yoki gizmo bilan qilingan
    o'zgarishlar joriy vaqtda kalit bo'ladi (yangi xususiyat — yangi trek). O'z undo/redo'si (oyna fokusda: Ctrl+Z / Ctrl+Y),
    Ctrl+S — klipni saqlash. Kontent: o'ng tugma → Yangi animatsiya klipi, `.zanim` ikki marta bosilsa oynada ochiladi.
    Namuna: Showcase3D `Scenes/Animation.zscene` (mayoq, eshik, glTF Animator event'lari); Adventure3D ark ostidagi
    **darvoza** — qahramon yaqinlashsa ochiladi, uzoqlashsa yopiladi (`AnimationPlayerComponent::PlayFrom` — turgan joyidan
    orqaga), klip event'lari g'ichirlash va taqillash ovozlarini chaladi (`ProximityDoor` skripti)
  - **Bir nechta skin** (glTF): har skinned mesh o'z skin'i bilan (tana va kiyim, bir faylda ikki personaj, ikki armaturaga
    bog'langan arqon) — barcha skin suyaklari bitta skeletda (bitta Animator), har skin o'z suyaklari / inverse bind'lari bilan
    (farq qilsa o'z palitrasi, har skin'da 80 tagacha suyak). Namuna: Showcase3D `Models/TentaclePair.gltf` (uch skin)
  - **Animatsiya samaradorligi** (Animator > Samaradorlik): kirishlari o'zgarmagan poza qayta hisoblanmaydi (pauza, tezlik 0,
    tugagan klip); **ko'rinmasa** — hech bir kamera ko'rmagan model pozalanmaydi (vaqt, event'lar, root motion davom etadi) yoki
    umuman to'xtaydi; **pozalarni keshlash** (klip belgilangan kadr/s da oldindan namuna olinadi, model animatorlari ulashadi,
    step kalitlar aniq). Statistika oynasida **Animatsiya** bo'limi, profiler'da `Animatsiya/Pozalar`, `Scene::GetAnimationStats()`.
    Namuna: Showcase3D `Scenes/Olomon.zscene` (168 tentakl, buriladigan kamera)
  - Batafsil qo'llanma: [docs/Animation.md](docs/Animation.md) — kliplar, timeline, Animator, blend tree, qatlamlar, event'lar,
    root motion, biriktirish, morph target'lar, samaradorlik, kod misollari bilan
  - **Post-processing**: SSAO (ambient occlusion), bloom (**rangi** — porlash shu rangga ko'paytiriladi; **linza changi** —
    yorqin yorug'likning keng porlashi protsedural chang/dog' teksturasini yoritadi), vinyetka, kontrast/to'yinganlik/rang
    tuslash, **xromatik aberratsiya** (chetlarga tomon qizil va ko'k siljiydi), **plyonka donasi** (har kadr o'zgaradigan
    shovqin, o'rta tonlarda kuchli, dona o'lchami bilan; FXAA yoqilgan bo'lsa undan keyin qo'shiladi) — yangi effektlar
    standart holatda o'chiq,
    **FXAA** anti-aliasing (tonemapping va rang sozlamalaridan keyin; Sahna panelida yoqiladi, subpiksel kuchi bilan),
    **avto ekspozitsiya** (ko'z moslashishi: o'rtacha log yorqinlik GPU'da kichraytirish zanjiri bilan o'lchanadi, vaqt
    bo'yicha moslashish tezligi, min/max EV, kompensatsiya; standart holatda o'chiq - qo'lda ekspozitsiya)
  - **Render masshtabi** (0.5 - 2.0): sahna tasviri ko'rinish o'lchamining shu barobarida chiziladi va chiziqli filtr bilan
    kattalashtiriladi / kichraytiriladi (editor viewport'i va player; Sahna panelining "Render" bo'limi)
  - **Frustum culling**: kamera ko'rmaydigan meshlar dunyo AABB'lari bo'yicha chizilmaydi (model qismlari alohida,
    skinned meshlar uchun barcha suyaklar bo'yicha kafolatli chegaralar), soyalarda har bir kaskad o'z hajmi bo'yicha;
    2D'da sprite, doira, matn (glif chegaralari bo'yicha), zarrachalar tizimi va tilemap'ning 16x16 plitkali bo'laklari
  - **GPU instancing**: bir xil mesh + material (shader oladigan barcha qiymatlar teng, skinned emas, opaque / cutout)
    bitta `glDrawElementsInstanced` chaqiruvida - transform, normal matritsa va entity ID har nusxa uchun atribut; soya
    kaskadlarida ham bir xil meshlar birlashtiriladi. Tasvir va tanlash (picking) buferi o'zgarmaydi
  - **Render statistikasi** (Statistika paneli): chizilgan / kesilgan / instanced meshlar va chaqiruvlar, soya chizishlari,
    2D obyektlar va tile bo'laklari. `ZAMIN_EDITOR_STATS=1` (editor) / `ZAMIN_PLAYER_STATS=1` (player) chiqishda oxirgi
    kadr statistikasini logga yozadi; `ZAMIN_EDITOR_NO_CULLING`, `ZAMIN_EDITOR_NO_INSTANCING` (player'da
    `ZAMIN_PLAYER_NO_...`) solishtirish uchun o'chiradi
  - **Editor ko'rinish rejimlari** (viewport toolbar'i): wireframe (faqat qirralar yoki sirt + qirralar) va debug
    ko'rinishlar - faqat yorug'lik, normallar, albedo, roughness, metallic (qiymat ko'rinishlari tonemappingsiz)
  - **Particle tizimi**: olov, uchqun, tutun (additive blending, billboard, editor'da jonli preview). Simulyatsiya sahnadan
    alohida (`ParticleSimulation`, `Scene/ParticleSystem.h`). **Asosiy sozlamalar**: sikl davomiyligi, takrorlash,
    **oldindan to'ldirish** (prewarm - takrorlanadigan tizim boshidanoq bir sikl ishlagandek to'la), boshlanish
    kechikishi, **simulyatsiya (vaqt) tezligi**, fazo - dunyo / lokal / **boshqa obyekt** (masalan kema: uchqunlar
    kema bilan siljiydi, aylanayotgan minora bilan emas), **tugaganda** (takrorlanmaydigan tizim siklini o'ynab
    bo'lgach va bola obyektlardagi sub-emitter shablonlarining particle'lari ham tugagach - mushakning oxirgi portlashi
    kesilmaydi: obyekt o'chiriladi / yo'q qilinadi; skript `OnParticlesFinished()` va "ParticlesFinished" hodisasini
    oladi), **boshida ijro** (o'chiq: skript `Play()` chaqirguncha jim; inspektor / viewport'dagi Ijro / To'xtatish
    faqat ko'rib chiqish uchun, sahnaga saqlanmaydi). Emissiya: soniyasiga N, boshida (kechikishdan keyin) bir martalik
    portlash va **portlashlar jadvali** (har siklda: vaqt, soni,
    sikllar - cheksiz ham, oraliq, ehtimollik). Chizish: 3D billboardlar **GPU instancing** bilan - har tizim bitta
    chizish buyrug'ida (`ParticleRenderer`; 2D tekis particle'lar sprite'lar bilan 2D partiyada), shaffof (additive
    emas) tizimlar kameradan uzoqlik bo'yicha **orqadan oldinga saralanadi** (yoki yoshi bo'yicha / saralanmaydi);
    statistikada particle'lar soni va instanced chaqiruvlar. Emitter shakllari: nuqta, doira, sfera, quti, **konus**
    (asos radiusi, burchak, asosdan / hajmidan, 2D tekis yelpig'ich), **chiziq** (2D, lokal X bo'ylab), **mesh sirti**
    (primitiv, model fayli yoki obyektning Mesh Renderer'i; uchburchaklar yuzasiga qarab / qirralar / uchlar, normal
    bo'ylab yoki Yo'nalish bo'ylab); qalinlik (faqat cheti / sirti) va "markazdan tashqariga" yo'nalish. Ixtiyoriy
    modullar (inspektorda yoqiladigan bo'limlar):
    **Rang (umr bo'yi)** - ko'p nuqtali gradient (ikki gradient orasida tasodifiy), **O'lcham (umr bo'yi)** - egri chiziq
    (animatsiya kalitlari: pog'onali / chiziqli / Bezier; ikki egri chiziq orasida), **Tezlik (umr bo'yi)** - chiziqli
    (egri chiziq bilan, emitter yoki dunyo o'qlarida), orbita, radial, tezlik ko'paytuvchisi, **Tezlik chegarasi**
    (so'ndirish bilan), **Shovqin** - silliq 3D gradient shovqin maydoni (o'qlar bo'yicha kuch, egri chiziq, chastota,
    siljish tezligi, oktavalar, seed - deterministik), **To'qnashuv** - yer tekisligi (normal + masofa yoki obyekt orqali)
    yoki 3D / 2D fizika colliderlari (raycast, qatlamlar, o'yin / simulyatsiyada): sakrash, so'ndirish, umr yo'qotish,
    o'ldirish tezligi, radius; sirtda tinch yotadi, **Sub-emitterlar** - particle tug'ilganda / o'lganda / urilganda /
    umr bo'yi boshqa obyektning Particle System'i o'sha joyda particle chiqaradi (soni, ehtimollik, tezlik / rang /
    o'lcham merosi; "Yangi shablon" tugmasi bola obyekt yaratadi) - mushak, yomg'ir sachrashi, **Tekstura sheet
    (flipbook)** - ustun x qator panjara yoki sprite kadrlari (.zmeta, prefiks), umr bo'yi (sikllar) yoki FPS bilan,
    boshlang'ich / tasodifiy kadr, tasodifiy qator, **Iz (trail)** - kameraga qaragan lenta (ulushi, umri, nuqta
    oralig'i, kenglik egri chizig'i, rang gradienti, particle rangida). Viewport'da emitter shakli (mesh qirralari ham)
    va to'qnashuv tekisligi gizmo bilan (Gizmolar > Particle shakli). Namunalar: Showcase3D `Scenes/Particles.zscene`,
    `Scenes/ParticlesII.zscene` (mesh, shovqin, to'qnashuv + sachrash, spiral izlar, mushaklar), `Scenes/ParticlesIII.zscene`
    (prewarm bilan / siz tutun, portlashlar jadvali favvorasi, kechikish bilan halqalar to'lqini, 9000 particle'li
    instanced galaktika sekin harakatda, kema fazosidagi uchqunlar, o'zini yo'q qiladigan bir martalik mushak)
  - **Particle preset'lari kutubxonasi**: 9 ta tayyor effekt (olov, tutun, uchqunlar, yomg'ir, qor, sehr, portlash,
    chang, mushak) - inspektordagi **Preset** menyusidan, Ierarxiya > Yaratish > Effektlar'dan yoki `ParticlePresets`
    API'dan; tizim (sub-emitter shablonlari bilan) **`.zparticle`** fayl sifatida saqlanadi (Assets/Particles, "Preset
    sifatida saqlash") va menyudan, Kontent oynasidan viewport / ierarxiya / Preset tugmasiga tashlab qo'llanadi;
    shablonlar bola obyekt bo'lib yaratiladi, sahnaga tegishli havolalar (boshqa obyekt fazosi, tekislik obyekti)
    saqlanmaydi. **Ob-havo (Kamera atrofida moduli)**: emitter asosiy kamera bilan (editorda - sahna ko'rinishi
    kamerasi bilan) yuradi, kamera ortda qoldirgan particle'lar quti bo'ylab oldinga o'raladi - yomg'ir tomchilari
    yerga tegib sachraydi (to'qnashuv sub-emitteri), qor shovqin bilan aylanib tushadi; kameraga juda yaqin particle'lar
    so'nadi ("Yaqinda so'nish", standart 4 birlik - ob'ektiv yonidagi tomchi ekran bo'ylab chiziq bo'lmaydi). Chizish moduli: **tezlik
    bo'ylab cho'zilgan billboardlar** (yomg'ir chiziqlari, uchqunlar). Inspektor Unity'dagidek modullarga bo'lingan
    (Asosiy, Emissiya, Shakl, Kamera atrofida, Tezlik, Tezlik chegarasi, Rang, O'lcham, Shovqin, To'qnashuv,
    Sub-emitterlar, Tekstura sheet, Iz, Chizish) - har birida yoqish katagi, "faqat yoqilganlar" filtri va "Modul
    qo'shish"; Ijro / Pauza (qotib qoladi) / Qayta / To'xtatish tugmalari inspektorda va tanlangan tizim uchun viewport
    burchagidagi panelda (particle soni, sub-emitterlar bilan, sikl vaqti). Namuna: Showcase3D
    `Scenes/ParticlesIV.zscene` (preset'lar qatori, loyihaning `Particles/Gulxan.zparticle` preset'i, kamera atrofidagi
    yomg'ir, o'chiq qor). **Performance**: `build/bin/ZaminBench` (yoki `particle_bench` target) ~50 000 particle'ni
    headless simulyatsiya qilib har holat uchun ms / kadr chiqaradi (`--profile` - profiler bo'limlari); particle
    simulyatsiyasi Debug build'da ham `-O2` bilan kompilyatsiya qilinadi (`ZAMIN_OPTIMIZE_PARTICLES_IN_DEBUG`)
  - **Sprite animatsiya** (sprite sheet), pixel-art filtr, FlipX/FlipY
- **2D fizika** (Box2D, `PhysicsWorld2D`): dinamik/statik/kinematik jismlar, box/circle collider, trigger'lar, to'qnashuv callback'lari
  - **Polygon Collider 2D**: istalgan ko'pburchak (obyekt masshtabi bilan); botiq yoki 8 tadan ko'p burchakli shakl
    quloq kesish (ear clipping) bilan uchburchaklarga bo'linib, ko'pi bilan 8 burchakli qavariq bo'laklarga qayta
    birlashtiriladi (Hertel-Mehlhorn, `Polygon2D::Decompose`), o'zini kesib o'tgani qavariq qobiq bilan. Inspektorda
    nuqtalar ro'yxati va holat (nechta bo'lak), **viewport'da tahrirlash** ("Nuqtalarni tahrirlash": sudrash, chiziqqa
    bosish - yangi nuqta, Ctrl + bosish / Delete - o'chirish, Shift - 0.1 qadam, Esc - tugatish; har bir harakat bitta
    Ctrl+Z) va **"Spraytga moslash"** (sprayt kadrining shaffof bo'lmagan qismi chetidan ko'pi bilan 12 nuqtali kontur)
  - **Edge Collider 2D**: nuqtalar orqali chiziq yoki yopiq halqa (yer, qiyaliklar); ikki tomonlama yoki **bir tomonlama
    silliq zanjir** (Box2D chain: bo'g'inlarga ilinmaydi; qattiq tomoni nuqtalar yo'nalishining chap tomonida, viewport'da
    chiziqchalar bilan ko'rsatiladi). Nuqtalar xuddi shunday viewport'da tahrirlanadi
  - **Fizik material** (`.zphysmat` asset, Kontent > "Yangi fizik material": muz, rezina, metall...): ishqalanish,
    sakrash, sakrash chegarasi, zichlik va **qo'shilish usullari** (O'rtacha / Eng kichigi / Ko'paytma / Eng kattasi /
    Box2D standarti). Box / Circle / Polygon / Edge collider va Tilemap tanlaydi (bo'sh - o'z qiymatlari); inspektorda
    tahrirlanadi, fayl o'zgarsa o'yin davomida ham darhol qo'llanadi (hot reload)
  - **To'qnashuv qatlamlari**: loyihada 16 ta nomli qatlam va qaysi juftliklar to'qnashishi (Loyiha sozlamalari >
    Fizika qatlamlari: burchakli sarlavhali matritsa). Rigidbody 2D, har bir kollider (o'z qatlami yoki jismniki) va
    Tilemap qatlam tanlaydi (Box2D category/mask); `Raycast2D(..., LayerMask({ "Ground" }))`. 3D fizika ham shu
    qatlamlardan foydalanadi
  - **Platform Effector 2D**: obyektning barcha kolliderlari (yoki tilemap plitkalari) platforma - bir tomonlama, sirt
    yoyi (180 - oddiy, kichigi burchaklardan ham o'tkazadi, kattasi yondan ham ushlaydi), burilish, yon tomonda
    ishqalanish / sakrashni o'chirish (devorga yopishib qolmaslik). Box Collider 2D'ning "Bir tomonlama" belgisi avvalgidek
    ishlaydi (bir bosishda effektorga o'tkaziladi). Namuna: Platformer2D `Scenes/Shapes2D.zscene` (tepalikli Edge yer,
    botiq tosh ko'prik, yog'ayotgan polygon toshlar va kristallar, muz qiyalik, rezina to'shak, bir tomonlama
    platformalar, faqat yerga tegadigan "Ghost" qatlamidagi sharlar)
  - **Joint'lar**: Distance (qattiq tayoq, arqon - "faqat maksimal", prujina - chastota/so'nish), Revolute (sharnir:
    burchak chegarasi, motor), Prismatic (o'q bo'ylab sirpanish: chegara, motor), Weld (payvand, qattiq yoki yumshoq),
    Wheel (g'ildirak: osma prujinasi, chegara, motor). Umumiy: ulangan jism (boshqa obyektning Rigidbody 2D yoki
    Tilemap'i; bo'sh - dunyodagi nuqta), langar / ulangan langar (avto), bir-biri bilan to'qnashuv, uzilish kuchi /
    momenti (`OnJointBreak(joint)`, komponentda `Broken`; `Broken = false` qayta yaratadi). Jism nofaol bo'lsa yoki
    o'chirilsa joint yo'qoladi, ikkala jism qaytganda qayta yaratiladi; o'yin davomida motor/chegara/prujina o'zgarishi
    darhol, `ConnectedBody` almashsa joint qayta quriladi. Inspektorda holat (kuch, moment, burchak, siljish), viewport'da
    langarlar, burchak yoylari va o'qlar (Gizmolar > 2D joint'lar: tanlanganda / hammasi). Duplikat va prefab'da ulangan
    jism nusxaga qayta bog'lanadi; prefab'dan tashqaridagi jismga ulangan joint saqlashda, sahnada yo'q jismga ulangani
    o'yin boshida / paydo bo'lganda ogohlantiradi (inspektorda ham ko'rinadi). **Viewport'da tahrirlash** ("Langarlarni tahrirlash"): langar (doira), ulangan
    langar (romb), sirpanish / osma o'qi (kvadrat) va chegaralar (sharnirning burchaklari yoy ustida, sirpanish
    oralig'i o'q yonida) sudraladi, Shift - 0.1 birlik / 5° qadam, Esc - tugatish; har harakat bitta Ctrl+Z. Ulangan
    langarni "Langar turgan joyga" bir bosishda qo'yish. Namuna: Platformer2D `Scenes/Joints2D.zscene` (ko'prik, lift,
    mayatnik, arqon, prujina, uziladigan chiroq, yumshoq minora, motorli mashina - A / D)
  - **Area Effector 2D** (shamol, oqim): kolliderlari (odatda trigger) ichidagi dinamik jismlarni har fizika qadamida
    itaradi - yo'nalish (obyekt bilan buriladi yoki dunyoda), kuch (N yoki "massaga ko'paytirish" bilan N/kg - hamma
    jism bir xil tezlanadi), tasodifiy shiddat, chiziqli / burchak qarshiligi, qatlam maskasi. Trigger bo'lmagan
    kollider unga tegib turganlarni itaradi
  - **Buoyancy Effector 2D** (suv): kollider ichi sirtgacha suyuqlik; jism siqib chiqargan suyuqlik og'irligi (zichlik x
    sirt ostidagi yuza x gravitatsiya) botgan qismining markaziga yuqoriga qo'yiladi - zichligi kichik kollider suzadi va
    to'g'rilanadi, kattasi cho'kadi. Qarshilik va oqim (daryo) botgan qismiga qarab, qatlam maskasi. Viewport'da shamol
    o'qlari, suv sathi va oqim yo'nalishi; inspektorda nechta jismga ta'sir qilayotgani
  - **So'rovlar**: `Raycast2DAll` (nur yo'lidagi har bir obyekt, eng yaqini birinchi), `OverlapCircle2D` /
    `OverlapBox2D` (burchak bilan) / `OverlapPoint2D` - aniq shakl tekshiruvi, har obyekt bir marta, markazga yaqini
    birinchi; hammasi qatlam maskasi va `includeTriggers` bilan. Ko'rinish > Debug chizish > Raycastlar yoqilsa nur
    (tartib raqamlari bilan) va so'rov shakli ko'rinadi. Namuna: Platformer2D `Scenes/Effectors2D.zscene` (hovuz va
    unda suzayotgan qutilar, yog'och, to'p va cho'kkan toshlar; shamol ustuni va yon shamol; Raycast2DAll skaner nuri;
    sichqoncha bilan portlash - OverlapCircle2D)
  - **Simulyatsiya sozlamalari** (Sahna > Fizika): qadam chastotasi (2D va 3D, standart 60 Hz), kadrda ko'pi bilan
    qadam, Box2D **sub-qadamlari** (har qadam shuncha kichik qadamga bo'linadi: og'ir yuk osilgan zanjir va ragdoll'lar
    cho'zilmaydi; effektorlar, joint uzilishi va skriptning `ApplyForce` kuchi har sub-qadamda - har kadr berilgan kuch
    istalgan FPS, qadam chastotasi va sub-qadamlar sonida bir xil ta'sir qiladi), tezlik / pozitsiya iteratsiyalari. O'yin davomida
    jismlar, uyg'oq / uxlayotganlar, joint'lar, kontaktlar va kadrdagi Box2D qadamlari soni ko'rinadi
  - **Uxlash**: qimirlamay qolgan jism (unga tegib / ulanib turganlar bilan birga, Box2D orollari kabi) hisoblanmaydi -
    sahnada yoqish / o'chirish va qancha vaqtdan keyin (Box2D'niki 0.5 s, boshqasini dvigatel o'zi sanaydi); har
    Rigidbody 2D: "Uyg'oq boshlanadi", "Uxlab boshlanadi" (urilguncha kutib turadigan minora) yoki "Hech uxlamaydi".
    Inspektorda holati va "Uyg'otish" tugmasi, colliderlar ko'rinishida uxlayotganlar ko'kish-kulrang; skriptda
    `IsSleeping()`, `WakeUp()` (jism yana to'liq vaqt uyg'oq turadi), `PutToSleep()`; `SetPosition2D` va kuch ham
    uyg'otadi. Namuna: Platformer2D `Scenes/Ragdoll2D.zscene` - sichqoncha bilan
    (yoki R, o'zi ham har 2 soniyada) `Prefabs/Ragdoll.zprefab` tashlanadi: 11 bo'lak, chegarali va ishqalanishli 10 ta
    sharnir, tana qismlari "Ragdoll", qo'l-oyoqlar "RagdollLimb" qatlamida (o'zaro to'qnashmaydi); zinapoya, chegarali
    ko'prik, motorli rotor, arg'imchoq, uxlab turgan quti minorasi va zanjirli mayatnik; 2 sub-qadam
  - **Determinizm** (2D va 3D): bir xil sahna har safar bit-bit bir xil simulyatsiya beradi - ikki marta, faylga saqlab
    o'qilgandan keyin va obyektlari boshqa tartibda yaratilgan bo'lsa ham (`PhysicsDeterminism2D/3D` testlari). Jismlar,
    joint'lar va personajlar sahna tartibida yaratiladi, editorning Play nusxasi (`Scene::Copy`) ierarxiya tartibida -
    fayldan o'qilgan sahna kabi: editorda o'ynalgan va eksport qilingan o'yin bir xil harakat qiladi
- **3D fizika** (Jolt Physics): Rigidbody3D, Box/Sphere/Capsule collider, trigger'lar, to'qnashuvlar
  - **Convex Hull Collider 3D**: obyektning Mesh Renderer'idagi model / primitiv (yoki tanlangan `.gltf/.glb/.obj`)
    vertekslaridan qavariq qobiq, ko'pi bilan "Maks. burchaklar" (4..256) - ko'proq bo'lsa eng chetdagi nuqtalar
    olinadi (soddalashtirilgan qobiq). Qobiq har model va chegara uchun bir marta quriladi (`CollisionShapes3D`,
    GPU'siz o'qiladi) va fayl yoki uning `.zmeta` import sozlamalari o'zgarsa qayta quriladi; transform masshtabi bilan
    ko'payadi. Inspektorda burchak / yuz soni, viewport'da qobiq qirralari (Fizik colliderlar)
  - **Joint'lar 3D** (Jolt constraint'lar): Fixed (mahkam), Hinge (sharnir: o'q, chegaralar -180..0 / 0..180, motor -
    tezlik va maks. moment, prujina - nishon burchakka tortadi), Distance (minimal / maksimal masofa, arqon - "faqat
    maksimal", yumshoq uchlar - chastota / so'nish), Slider (o'q bo'ylab sirpanish, chegaralar, motor), Cone (sharsimon
    sharnir - yelka / son: konus burchagi, buralish chegaralari, ishqalanish momenti; Jolt SwingTwist). 2D joint'lar
    kabi: ulangan jism (bo'sh - dunyodagi nuqta), langar / avto ulangan langar (obyektning o'z koordinatalarida,
    masshtab bilan), "Bir-biri bilan to'qnashsin" (o'chiq - ulangan jismlar bir-biridan o'tadi), uzilish kuchi / momenti
    (`OnJointBreak3D(joint)`, `Broken`), jism nofaol / o'chirilganda yo'qolib qaytishi, o'yin davomida sozlamalar
    darhol, duplikat / nusxalash / prefab'da (`.zprefab` fayli ham) qayta bog'lanish - nusxa o'z jismlariga ulanadi,
    tashqaridagi jismga havola saqlanadi; serializatsiya UUID orqali, sahnada yo'q ulangan jism uchun bitta
    ogohlantirish. Bo'sh yoki faqat 2D sahnaga 3D prefab tashlansa Jolt o'sha paytda ishga tushadi. Inspektorda holat
    (kuch, moment, burchak, siljish, masofa, og'ish / buralish), viewport'da langarlar, sharnir o'qi va burchak yoyi,
    sirpanish chegaralari, masofa tayog'i, konus (Gizmolar > Joint'lar); "Langarlarni tahrirlash" - langar, ulangan
    langar, o'q va chegaralarni viewport'da sudrash (tanlangan nuqtada ko'chirish gizmosi). Namuna: Showcase3D
    `Scenes/Joints3D.zscene` (prujinali eshik, sharlar zanjiri, lift - `SliderShuttle` skripti, motorli tegirmon
    parragi, payvandlangan L, qiyalikdan dumalaydigan konus / silindr / kristall ziyoratgoh qobiqlari)
  - **3D ragdoll** namunasi: Showcase3D `Scenes/Ragdoll3D.zscene` - `Prefabs/Ragdoll3D.zprefab` (11 bo'lak: yelka, son,
    bel va bo'yinda Cone Joint 3D, tirsak va tizzada chegarali Hinge Joint 3D, bo'g'imlar bir-biri bilan
    to'qnashmaydi) zinadan yumalab tushadi; `RagdollSpawner3D` skripti - R (yana 3 ta), sichqoncha / F (kameradan
    otish), o'zi ham har 3 soniyada; qutilar, bochkalar, arg'imchoq va qum xalta
  - **Mesh Collider 3D** (Jolt MeshShape): modelning (yoki Mesh Renderer'dagi model / primitivning) o'z uchburchaklari -
    botiq shakllar (zinapoya, ark, tunnel, yarim quvur, notekis yer) chizilgan joyida to'qnashadi. Faqat statik va
    kinematik jismlar uchun (dinamik jism uni hisobga olmaydi - ogohlantirish, inspektorda ham); uchburchaklar old
    tomonidan qattiq. Uchburchaklar har model uchun bir marta o'qiladi, Jolt daraxti esa barcha dunyolar uchun umumiy va
    Play / Stop'dan keyin qayta qurilmaydi; burchaklar joyi bo'yicha birlashtiriladi (choklarda sakramaydi). Inspektorda
    uchburchak / nuqta soni, viewport'da qirralari (bir tekislikdagi ichki qirralarsiz)
  - **To'qnashuv qatlamlari 3D**: 2D bilan bir xil 16 ta nomli qatlam va matritsa (Loyiha sozlamalari > Fizika
    qatlamlari). Rigidbody 3D va Character Controller 3D qatlam tanlaydi (Jolt ObjectLayer); o'yin davomida qatlam ham,
    matritsa ham darhol qo'llanadi
  - **3D so'rovlar** (qatlam niqobi va "trigger'lar ham" belgisi bilan; nur / shakl to'qnashuvchi ichidan boshlansa uni
    urmaydi - o'z tanasi o'tkazib yuboriladi): `Raycast3D`, `Raycast3DAll` (har obyekt bir marta, yaqinidan),
    `OverlapSphere3D` / `OverlapBox3D` / `OverlapCapsule3D` (obyektlar, markazga yaqinidan), `SphereCast3D` /
    `BoxCast3D` / `CapsuleCast3D` va ularning `...All` turlari ("qalin nur": shakl qancha yurgani, tegish nuqtasi,
    normal). "Raycastlar" debug chizishda shakl, yo'l va tegishlar ko'rinadi (`DebugDraw::WireCapsule` ham bor).
    Namuna: Showcase3D `Scenes/Queries3D.zscene` (yarim quvurdagi sharlar, Arvoh sharlar o'tib ketadigan panjara,
    `SphereSweep3D`, `OverlapGlow3D`, `LaserAll3D` skriptlari)
  - **Uzluksiz to'qnashuv (CCD)**: Rigidbody 3D > "Uzluksiz (CCD)" (dinamik jismlar, Jolt LinearCast) - tez jism (o'q,
    otilgan to'p) har qadamda yo'li bo'ylab tekshiriladi va yupqa devordan o'tib ketmaydi; o'yin davomida ham yoqiladi
  - **Uxlash 3D**: Sahna sozlamalari > Fizika > Jolt (3D) - yoqish / o'chirish va qancha vaqtdan keyin (Jolt'niki
    0.5 s; o'chirilsa hamma jism uyg'onadi), har Rigidbody 3D: "Uyg'oq boshlanadi", "Uxlab boshlanadi" (urilguncha
    kutadigan to'plam) yoki "Hech uxlamaydi". Inspektorda holati va "Uyg'otish" / "Uxlatish" tugmasi, sahna
    sozlamalarida uyg'oq / uxlayotgan jismlar soni, colliderlar ko'rinishida uxlayotganlar ko'kish-kulrang; skriptda
    `IsSleeping3D()`, `WakeUp3D()`, `PutToSleep3D()` (va `(other)` turlari). Uxlab qolgan jismlarning kontaktlari
    saqlanadi (Box2D kabi): uxlagan quti trigger plitasidan yoki yerdan "chiqib ketmaydi"
  - **Fizik material 3D**: 3D colliderlar ham 2D'dagi `.zphysmat` asset'ini oladi - ishqalanish, sakrash, sakrash
    chegarasi va qo'shish usullari (Eng kichigi, Ko'paytma...; har kollider o'zinikini, bir jismdagi kolliderlar ham
    alohida), fayl o'zgarsa o'yin davomida ham yangilanadi. Zichlik 3D'da ishlatilmaydi (massa - Rigidbody 3D'da)
  - **Trigger'lar 3D**: `IsSensor` kolliderlar `OnTriggerBegin` / `OnTriggerEnd` beradi (2D bilan bir xil API, har
    obyekt jufti bir marta). Bir jismda trigger va qattiq kolliderlar birga bo'lishi mumkin (quti + uning atrofidagi
    "radar" sferasi): trigger qismi urilmaydi, qahramonni to'xtatmaydi, so'rovlar uni faqat "trigger'lar ham" bilan
    ko'radi; kinematik trigger uxlab yotgan jismni ham sezadi
- **Character Controller 3D** (Jolt CharacterVirtual): qiyaliklar, zinapoyalar, yerni aniqlash, sakrash, harakatlanuvchi platformalarda yurish;
  yurib borib tekkan dinamik jismlarni **itaradi** ("Jismlarni itaradi", "Itarish kuchi" - ko'pi bilan shuncha N;
  standart 100 N, og'ir jism uchun ko'proq). Namuna: Showcase3D `Scenes/Physics3D.zscene` - muz / oddiy / rezina
  qutilar qiyalikda va trigger plitasi, uxlab turgan piramida va uni urib yuboradigan supurgich, CCD yoqiq / o'chiq
  tez sharlar yupqa oynaga (`Launcher3D`), qutilarni itarib yuradigan qahramon va trigger sferali "radar" quti
  (`SleepTint3D`, `Sweeper3D`, `Respawn3D`, `TriggerGlow3D` skriptlari)
- **Raycast** 2D va 3D (`Raycast2DAll` / `Raycast3DAll`, overlap va shape cast so'rovlari)
- **Debug chizish** (`DebugDraw`, `Zamin/Debug/DebugDraw.h`): skriptlardan va dvigateldan dunyo fazosida chiziq, nur, o'q,
  quti (AABB va burilgan), sfera, aylana, krest, nuqta, o'qlar, yo'l (path) va ekranga qaragan matn - rang, davomiylik
  (0 = bir kadr, > 0 = o'yin vaqtida soniyalar; pauzada to'xtaydi - oxirgi o'yin kadrining shakllari ekranda qoladi,
  pauza paytida boshqa kod chizgan bir kadrlik shakllar esa yig'ilib qolmaydi) va chuqurlik testi bilan. Sahna chizilgandan keyin
  post-processingdan o'tgan tasvir ustiga (aniq ranglar, silliq qalin chiziqlar) sahna chuqurligiga qarab chiziladi.
  Fizika ko'rinishlari: **raycast so'rovlari** (nur, tegish nuqtasi, normal; tegmasa qizil), **kontakt nuqtalari**
  (2D, 3D, personajlar - yashil; sariq normal, shakllar qancha kirgani asl o'lchamda qizil chiziq, 3D jism kuchli
  urilganda bir lahza halqa, ikkala jism uxlasa xira), **qiymat yozuvlari** (har tegib turgan juftlik ustida:
  chuqurlik mm, 3D zarba tezligi va taxminiy impulsi - tezlik x juftlikning normal bo'yicha effektiv massasi, 2D'da
  Box2D impulsi; joint'larda kuch, moment, uzilish chegarasidan foizi), **joint'lar** (2D va 3D: ikkala langar va ular
  orasidagi chiziq - sharnir bo'shasa qizil, jismlar markazigacha chiziqlar, sharnir / sirpanish / konus o'qi; rangi
  uzilishga qanchalik yaqin - yashil, sariq, qizil, uzilmaydigani ko'k; uzilgani qizil X) va **tezlik vektorlari**
  (3D personajlarning haqiqiy tezligi o'q va m/s bilan, devor to'sganda skript so'ragan tezlik xira; dinamik 2D/3D
  jismlar). `PhysicsWorld3D::GetDebugContacts` kontakt ma'lumotini (normal, chuqurlik, zarba, impuls, yoshi, uxlash,
  ikkala obyekt) testlar va vositalar uchun ham beradi. Namuna: Showcase3D `Scenes/PhysicsDebug3D.zscene` (ikki
  chiroq - biri tebranadi, biri chegarasiga yaqin; shar urilib uziladigan javon; tinch quti, uxlagan minora, sakraydigan
  shar; `PhysicsDebugView3D` skripti - C / J / V tugmalari). Navigatsiya yo'llari uchun
  `DebugDraw::NavigationPath(points)`: bo'laklar, yo'nalish uchlari, nuqtalar, boshi yashil va oxirida halqa.
  Editor'da viewport toolbar'idagi hasharot (bug) tugmasi va uning menyusi, player'da loyiha sozlamalari
  (Umumiy > Debug chizish) va `--no-debug-draw` / `--debug-raycasts` / `--debug-contacts` / `--debug-contact-details` /
  `--debug-joints` / `--debug-velocities` bayroqlari.
  Skript ularni o'yin vaqtida o'zgartira oladi (`DebugDraw::SetEnabled(false)`, `DebugDraw::GetSettings().ShowRaycasts = true`):
  editor'da o'yin viewport sozlamalari bilan boshlanadi, skript o'zgarishi o'yin to'xtaguncha (yoki menyuda biror narsa
  o'zgartirilguncha) saqlanadi.
  Shipping build'da (`-DZAMIN_SHIPPING=ON`; yoki `-DZAMIN_DEBUG_DRAW=OFF`) barcha chaqiruvlar bo'sh inline funksiyalar -
  skriptlarda qoldirsa bo'ladi. Namuna: `Projects/Showcase3D` > `Scenes/DebugDraw.zscene`
- **Editor gizmolari** (faqat editor viewport'ida, ko'z tugmasining menyusi yoki Ko'rinish > Gizmolar; har biri
  o'chiq / tanlanganda / hammasida, sozlamalar saqlanadi): **kamera frustumi** (perspektiv - 10 birlikkacha aniq piramida,
  far tekisligigacha xira; ortografik - quti; yuqori tomoni uchburchak), **yorug'lik yo'nalishi** (directional: halqa, nurlar
  va o'q; spot: o'qi bo'ylab o'q; yorug'lik rangida), **audio radiusi** (3D manbalar: Min va Max masofa sferalari),
  **chegaralar (bounding box)** - lokal (obyekt bilan aylanadigan quti), dunyo AABB (mesh'lar uchun frustum culling
  tekshiradigan chegara, skinned model'niki pozasi bilan) yoki ikkalasi, **skelet** (skinned model'lar: ota bo'g'indan
  bo'g'ingacha suyaklar va bo'g'inlar joriy animatsiya pozasida, mesh ustidan; ixtiyoriy bo'g'in nomlari), **normallar**
  (mesh va model vertekslarining normallari - har verteksdan chiziq, skinned model'larda joriy pozada; uzunligi, faqat
  kameraga qaraganlar, yo'nalish bo'yicha rang sozlanadi; kadrda ko'pi bilan 60000 ta, ko'p bo'lsa vertekslar namunasi.
  Dvigatelda: `Mesh::GetGeometry()` (vertekslar GPU'dan bir marta o'qiladi) va `AppendNormalLines`)
- **Tanlash konturi** (shader asosida, faqat editor): tanlangan obyektlar chizgan piksellar atrofida bir necha piksel
  qalinlikdagi silliq (anti-aliasing) kontur - mesh, model (skinned ham), sprite, doira, matn, tilemap. Viewport'ning
  entity ID rasmi ko'rinadigan piksellarni, tanlangan mesh'larning qaytadan chizilgan siluetlari esa butun shaklni beradi:
  boshqa obyekt orqasida qolgan qirralar xira chiziladi. Asosiy tanlangan obyekt to'q sariq, ko'p tanlovning qolganlari
  ochroq, bolalari xiraroq; sichqoncha ostidagi (bosilsa tanlanadigan) obyekt ingichka havorang kontur bilan ajratiladi.
  Qalinlik, ranglar, yashiringan qismlar, bolalar va hover sozlanadi (ko'z menyusi > Tanlash ko'rinishi); eski uslub -
  "Quti" (mesh chegaralari sim quti bilan) ham tanlanadi
- **Gizmo o'lchami**: ko'chirish / aylantirish / masshtab gizmosining ekrandagi o'lchami, chiziqlari qalinligi va viewport
  ikonkalari o'lchami (tanlash asboblari yonidagi menyu yoki Ko'rinish > Gizmo o'lchami; editor sozlamalarida saqlanadi)
- **Tilemap**: tileset'dan plitkali darajalar, editor'da chizish (palitra), avtomatik birlashtirilgan collider'lar
  - **Qatlamlar**: har bir qatlamning nomi, ko'rinishi, collision bayrog'i (faqat shu qatlamlar qattiq), tartibi (tilemap
    tartibiga qo'shiladi — masalan, o'yinchi oldidagi old plan) va shaffofligi; inspektorda qatlamlar ro'yxati (qo'shish,
    nusxa, surish, o'chirish), bo'yash faol qatlamga tushadi. Eski sahna fayllari 0-qatlam bo'lib ochiladi
  - **Auto-tile qoidalari**: 4 qo'shni (16 plitka) va 8 qo'shni blob (47 plitka) — katak qo'shnilariga qarab to'g'ri
    plitkani oladi, qo'shnilari ham yangilanadi. Qoidalar tileset'ning `.zmeta` faylida (`"AutoTiles"`) saqlanadi va
    inspektorda tuziladi: variantlar to'ri (qo'shnilik naqshi + plitka), palitradan bosib tayinlash, ketma-ket to'ldirish
  - **Asboblar**: mo'yqalam (B), to'rtburchak (R), to'ldirish (G, 4 qo'shnili flood fill), o'chirg'ich (E), tomizg'ich (I);
    Shift — o'chirish, auto-tile qoidasi bilan ham ishlaydi; viewport'da natija oldindan ko'rinadi (auto-tile variantlari
    bilan), har bir chiziq/to'rtburchak bitta Ctrl+Z qadami. Namuna: Platformer2D `Scenes/Tilemap2D.zscene`
  - **Plitka xususiyatlari** (tileset'ning `.zmeta` faylida, `"TileProperties"`; inspektorda tanlangan plitka uchun):
    collision turi — qattiq (standart), yo'q (o't, gul, suv: collision qatlamida ham o'tib ketiladi) yoki **bir tomonlama**
    (pastdan sakrab o'tiladi, ustiga qo'niladi); birlashtirilgan collider'lar shunga qarab quriladi. **Animatsiyali
    plitkalar**: plitka → kadrlar ro'yxati + FPS (palitradan bosib yoki tanlangan plitkalardan), editorda ham o'ynaydi
  - **Bir tomonlama platformalar**: plitkalar va Box Collider 2D'dagi "Bir tomonlama" belgisi (Box2D PreSolve: yuqoriga
    harakatlanayotgan yoki sirtdan pastdagi jism o'tib ketadi, tez tushayotgani ham qo'nadi); skriptlarga to'qnashuv faqat
    platforma ushlab turganda keladi; `DropThroughPlatforms(soniya)` — pastga tushib ketish (pastga + sakrash)
  - **Palitra**: sichqoncha g'ildiragi bilan kattalashtirish (kursor atrofida), surish (scroll, o'rta/o'ng tugma), bosib
    plitka yoki **sudrab bir nechta plitkali mo'yqalam** tanlash (mo'yqalam, to'rtburchak va to'ldirish uni choksiz
    takrorlaydi); belgilar (collision yo'q, bir tomonlama, animatsiya). Inspektor kengligida yoki alohida **Oyna > Plitka
    palitrasi** oynasida. Fizik colliderlar ko'rinishida tilemap qutilari (bir tomonlamalari pushti, strelka bilan)
- **3D (spatial) ovoz**: masofaga qarab so'nish; tinglovchi — **Audio Listener** komponenti (bir nechtasidan faol va eng katta
  ustuvorlikdagisi; ixtiyoriy tezlik o'lchash — Doppler uchun), u bo'lmasa asosiy kamera
- **Audio mixer**: guruhlar (bus) — Master > Music, SFX, UI va loyihaning o'z guruhlari (ota guruh bilan, masalan
  Ambience → SFX); har birida fader (dB), mute, solo va post-fader **daraja o'lchagich** (stereo, peak hold). **Oyna > Audio
  Mixer**: kanal tasmalari, guruh qo'shish / o'chirish / ota guruhini o'zgartirish (o'ng tugma), hozir chalinayotgan
  musiqa (pauza, to'xtatish), streaming chegarasi. Tahrir rejimida — loyiha standarti (`.zproject` "Audio"), o'yin
  davomida — faqat shu o'yin uchun ("Standart qilib saqlash" bilan loyihaga). Audio Source'ning **Chiqish guruhi**
  (inspektorda guruh tanlash + mixerdan keyingi dB) va **Streaming** belgisi; loyiha chegarasidan (4 MB) katta fayllar
  avtomatik diskdan oqim bilan o'qiladi
- **Musiqa API** (`Audio::PlayMusic(path, fade)`, `Audio::CrossfadeTo(path, sekund)`, `Audio::StopMusic(fade)`, pauza,
  musiqa balandligi): Music guruhida, har doim oqim bilan, teng quvvatli crossfade; musiqa o'yinga tegishli — `LoadScene`dan
  keyin ham davom etadi (editor o'yin to'xtaganda to'xtatadi va mixerni loyiha standartiga qaytaradi). Namuna: Showcase3D
  `Scenes/Audio.zscene` — tinglovchi shar tinch (yashil) va xavfli (qizil) zonalar orasida yuradi, `MusicDirector` skripti
  zonaga qarab musiqani crossfade qiladi (1 / 2 — trek, M — Music mute, P — pauza, S — to'xtatish), ziyoratgoh ovozi
  Ambience guruhida, signal UI guruhida
- **Ovoz variatsiyasi, ovozlar limiti, Doppler**: Audio Source (va `SoundParams`) har ijroda **tasodifiy balandlik / ton
  oralig'ini** oladi (masalan ton 0.9–1.1, `AudioEngine::SetRandomSeed` bilan takrorlanuvchi). Loyiha bo'yicha bir vaqtda
  ijro etiladigan **ovozlar limiti** (standart 64, musiqa hisobga kirmaydi): limit to'lsa yangi ovoz ustuvorligi (0–255)
  pastrog'ining o'rnini oladi, tenglarda — manbaning qoidasi (eng eskisi / eng past eshitilgani to'xtaydi yoki yangisi ijro
  etilmaydi); bitta faylning bir vaqtdagi nusxalari soni ham cheklanadi (masalan 20 ta dushman o'kirsa ham 3 tasi).
  **Doppler**: 3D manbaning tezligi harakatidan o'lchanadi (teleport hisobga olinmaydi), tinglovchinikini Audio Listener
  beradi; manbaning va loyihaning koeffitsiyenti (Audio Mixer > Sozlamalar), ton silliq o'zgaradi va 0.5–2 oralig'ida qoladi.
  Inspektorda o'yin davomida **jonli holat**: ijro / pauza / to'xtagan (limit sababli ham), vaqt chizig'i (bosib — o'sha
  joyga), eshitilish dB (balandlik x tasodifiy x guruh x masofa), hozirgi ton (tasodifiy va Doppler bilan), tezlik;
  Qayta / Pauza / To'xtatish tugmalari; inspektordagi o'zgarishlar darhol eshitiladi. Namuna: `Scenes/Audio.zscene` —
  uchuvchi dron (Doppler x2) va shamol qo'ng'iroqchalari (tasodifiy ton, bir vaqtda 3 tasi; H — ziyoratgoh g'uvillashini
  pauza / davom)
- **Audio clip asset**: ovoz faylining import sozlamalari `.zmeta`da (asset inspektorida tahrirlanadi, Qo'llash bilan
  saqlanadi): **yuklash usuli** (avtomatik — hajmiga qarab / xotiraga yechish / diskdan oqim), **oldindan yuklash** (sahna
  boshlanganda yoki birinchi ijroda yechilib xotirada qoladi — birinchi ijro kutmaydi, qayta-qayta chalinadigan ovoz har
  safar yechilmaydi; `Audio::Preload` / `Unload`) va faylning o'z **balandligi** (dB). Inspektor fayl qanday o'qilishini
  (oqim yoki xotiraga, chegara bilan) va yechilgan ovoz xotirada qancha joy olishini ko'rsatadi; davomiylik, format,
  kanallar, chastota bir marta o'qiladi (`Audio::GetClipLength("Audio/x.wav")`, `Audio::GetClipInfo`). Fayl o'zgarsa
  oldindan yuklangani qayta yechiladi
- **So'nish (fade)**: Audio Source'ning **FadeIn / FadeOut** (sekund) — ijro jimdan asta boshlanadi, to'xtatilganda
  (StopAudio, obyekt o'chirilsa yoki yo'q qilinsa) birdan uzilmay so'nadi (manba darhol bo'shaydi, ovoz o'zi so'nib
  tugaydi; limit to'lsa birinchi bo'lib o'sha o'rin beradi). Skriptda `PlayAudio(1.5f)`, `StopAudio(2.0f)`,
  `FadeAudio(0.3f, 0.5f)` (masalan dialog paytida pasaytirish), `GetAudioFade()`; `SoundParams::FadeIn`,
  `AudioEngine::Stop(handle, sekund)`, `Audio::StopSound(handle, sekund)`. Inspektorda jonli so'nish darajasi
- **Low-pass / high-pass filtr**: har bir Audio Source'da va har bir mixer guruhida (12 dB/oktava, kesish chastotasi
  o'zgarsa ~30 ms da silliq siljiydi): devor ortidagi yoki suv ostidagi bo'g'iq ovoz, radio / telefon ovozi. Audio Mixer
  tasmasidagi filtr tugmasi (`LP 450`), skriptda `SetAudioLowPass(600.0f)` / `SetAudioHighPass(...)` (0 — o'chirish),
  `Audio::SetGroupLowPass("SFX", 500.0f)`; guruh filtri loyiha mixeri bilan `.zproject`ga saqlanadi
- **O'yinchi ovoz sozlamalari**: `Audio::SaveSettings()` guruhlar balandligi / mute va musiqa balandligini **SaveData**'ga
  (loyiha standartidan alohida) saqlaydi; o'yin boshlanganda (player va editorda Play) avtomatik yuklanadi
  (`Audio::LoadSettings`, `HasSavedSettings`, `ClearSavedSettings`). Namuna: `Scenes/Audio.zscene` — **radio** (FadeIn /
  FadeOut, high-pass 700 Hz + low-pass 3.5 kHz; R — yoqish / o'chirish, T — filtr, G — pasaytirish), **suv havzasi**
  (tinglovchi kirsa SFX va Music guruhlari low-pass bilan bo'g'iladi), **sozlamalar** (- / = Music balandligi, N — SFX
  mute, 9 — saqlash, 0 — tozalash)
- **Yo'nalish konusi (3D)**: Audio Source'da **Yo'nalish konusi** — ichki burchak (to'liq balandlik), tashqi burchak va
  orqadagi balandlik (`AudioConeSettings`, miniaudio `ma_sound_set_cone`): ovoz obyektning oldi tomoniga (-Z) qarab
  baland, orqasida past — karnay, televizor, gapirayotgan odam; obyekt burilsa ovoz ham buriladi. Sahnada tanlangan
  manbaning konusi (ichki — to'q, tashqi — xira) va yo'nalish o'qi chiziladi, o'yin davomida inspektorda konus
  ko'paytuvchisi ko'rinadi. API: `SoundParams::Cone` / `Direction`, `AudioEngine::SetSoundCone`, `SetSoundDirection`,
  `ComputeConeGain`. Namuna: `Scenes/Audio.zscene` — ustundagi **karnay** (-X tomonga qaragan): tinglovchi uning oldidan
  o'tganda baland, orqasiga o'tganda past eshitiladi
- **Ambient zona** (`AmbientZoneComponent`, "Ambient Zone"): hajm ichida eshitiladigan takrorlanuvchi fon ovozi — o'rmon,
  g'or, yomg'ir, olomon. Quti (obyekt masshtabi va aylanishi bilan) yoki shar; tinglovchi ichida bo'lsa ovoz to'liq va
  joylashuvsiz, chetdan tashqarida **aralashish masofasida** silliq so'nadi, **so'nish vaqti** sakrashlarni (teleport,
  zona yoqilishi / o'chirilishi) silliqlaydi; ovoz eshitila boshlaganda ishga tushadi, so'nganda to'xtaydi (ovozlar
  limitini egallamaydi). Chiqish guruhi, ustuvorlik, streaming; bir nechta zona birga eshitiladi. Sahnada hajm va
  aralashish chegarasi chiziladi, inspektorda jonli daraja. API: `Scene::GetAmbientZoneLevel(entity)`,
  `Scene::GetAmbientZoneWeight(...)`. Namuna: `Scenes/Audio.zscene` — yashil zona ustidagi **o'rmon shovqini** (shamol va
  qushlar, Ambience guruhida)
- **Audio hodisalar**: `OnSoundFinished(const SoundFinished& sound)` — obyektning ovozi o'zi tugaganda (Audio Source yoki
  skriptning `PlaySound` ovozi, takrorlanmaydigan; ovozlar limiti to'xtatsa `Stolen`), asosiy oqimda yetkaziladi;
  boshqa skriptlar `Subscribe("SoundFinished", ...)` bilan eshitadi. `PlaySound` / `PlaySoundAt` endi handle qaytaradi.
  Dvigatelda: `AudioEngine::TakeFinishedSounds()` (`Finished` / `Stolen`). Namuna: `Scenes/Audio.zscene` — **karnay**
  e'loni tugaganda `PaAnnouncer` skripti tanaffusdan keyin keyingisini chaladi
- **Audio profiler**: Statistika oynasida faol ovozlar soni / limit (rang: to'lishiga qarab), so'nggi 240 kadr grafigi
  (limit chizig'i bilan), cho'qqi, ovoz turlari (ijroda / pauza / so'nmoqda, 3D, oqim, filtrli, musiqa, ko'rib chiqish,
  oldindan yuklangan fayllar) va **mixer guruhlari bo'yicha** jadval (o'zida / bola guruhlari bilan, limit sababli
  to'xtatilgan / ijro etilmagan, daraja); viewport statistika overlay'ida **Audio** bo'limi (o'ng tugma > Audio).
  Hisoblagichlar Play boshlanganda nollanadi. API: `AudioEngine::GetProfile()`, `ResetProfileCounters()`,
  `GetVoiceHistory()`; player `ZAMIN_PLAYER_STATS=1` bilan chiqishda va F10 bilan audio profilini log'ga yozadi
- **Ekran UI** (HUD): ekranga bog'langan matn/rasm/tugmalar, `OnClick()`
  - **UI ierarxiyasi** (RectTransform modeli): UI obyektning bolasi ota to'rtburchagiga nisbatan joylashadi (oddiy obyekt
    ostidagisi — ekranga). **Langar min/max** (0..1): bitta nuqtaga bog'lash (9 ta tayyor nuqta) yoki **cho'zish** —
    chapdan o'ngga, pastdan yuqoriga, butun ota (yuqori/pastki panel, fon); cho'zilgan o'qda inspektor Chap | O'ng
    (Past | Yuqori) chekkalarini ko'rsatadi; **pivot** (o'z to'rtburchagining nuqtasi). Langar yoki pivot o'zgarsa obyekt
    joyida qoladi (Shift bilan — langarga joylashadi). Bolalar otasidan keyin (ustida) chiziladi, aka-ukalar orasida —
    Tartib bo'yicha. Eski sahnalarning 9 nuqtali langari o'zi o'giriladi. API: `GetScene().GetUIRect(e)`,
    `GetUIScreenRect`, `GetUIParentRect`, `SetUIRect`, `SetUIAnchors(e, min, max, joyidaQolsin)`, `SetUIPivot`
  - **UI Canvas** — ekranga moslashuvchi masshtab: UI mo'ljal o'lcham (masalan 1920 x 1080) uchun yasaladi va ekran
    bilan birga kattalashadi / kichrayadi (kenglik yoki balandlikka moslash, ikkalasi orasi — logarifmik; "kengaytirish" —
    doim sig'adi, "qisqartirish" — doim to'ldiradi) yoki doimiy piksel koeffitsiyenti. Butun pastki daraxtga ta'sir qiladi
    (ichki canvas tashqisinikini oladi); `GetScene().GetUIScale(e)`. Obyekt > UI (ekran) > Canvas
  - **Layout guruhlari** (UI Layout Group): bolalarni ierarxiya tartibida **qator** (gorizontal), **ustun** (vertikal)
    yoki **panjara** (grid) qilib joylaydi: ichki chekka, oraliq, tekislash (9 nuqta), teskari tartib, bolalar
    kengligi/balandligini boshqarish va ortgan joyni bo'lish; panjara — katak o'lchami, oraliq, boshlanish burchagi,
    avval qator/ustun, ustunlar yoki qatorlar soni. "Mazmunga mos" — guruh o'lchami bolalariga qarab o'zgaradi (o'sib
    boruvchi ro'yxat). Ichma-ich guruhlar (ro'yxat ichida qator) afzal o'lchamni bolalaridan oladi. **UI Layout
    Element** — minimal / afzal / moslashuvchan o'lcham yoki guruhdan chiqarish. Nofaol bolalar tashlab ketiladi
  - **UI hodisalari**: `OnPointerEnter` / `OnPointerExit` (sichqoncha ustiga keldi / ketdi — bolasi ustida bo'lsa ota
    UI obyektga ham), `OnPointerDown` / `OnPointerUp` (`event.Inside`) / `OnPointerClick`; event bus: `"UIPointerEnter"` ...
    `"UIPointerClick"` (payload `UIPointerEvent`: `Target`, `Position`, `ScreenPosition`), `"UIClick"`. UI Button'ning
    **"Xabar nomi"** — bosilganda shu nomdagi skript xabari qabul qiluvchiga (bo'sh — `Broadcast`) yuboriladi:
    `AddMessageHandler("Resume", ...)`
  - **Klaviatura / gamepad navigatsiyasi**: tanlangan vidjetdan strelkalar / D-pad / chap stik (ushlab turilsa
    takrorlanadi) eng yaqin vidjetga o'tadi, gorizontal slayder chap/o'ng bilan qiymatini o'zgartiradi; Tab / Shift+Tab —
    keyingi/oldingi (matn maydoniga kirsa yozish boshlanadi), Enter / A — bosish (tugma, belgilash, maydonni tahrirlash),
    Esc / B — `OnCancel()`. Tanlangan vidjet ramka bilan ko'rinadi. **UI Navigation** komponenti: avtomatik / faqat
    gorizontal / faqat vertikal / aniq (har yo'nalishga vidjet) / yo'q, "faollashganda tanlash" (menyuning birinchi
    tugmasi), ramka rangi. `OnSelect()` / `OnDeselect()`, `GetScene().SetSelectedUI(e)`, `NavigateUI`, `SubmitUI`,
    `CancelUI`. Hech narsa tanlanmagan bo'lsa strelkalar o'yinniki (sichqoncha bosgan vidjet tanlanadi). Namuna:
    Platformer2D `Scenes/UILayout.zscene` (`UILayoutDemo`); player'da `ZAMIN_PLAYER_INPUT="10:Down;18:Enter;24:mouse=x,y"`
    bilan avtomatik kiritish
  - **UI animatsiyalari** (tween): UI Transform'ning **ko'rinishi** — masshtab (pivot atrofida), siljish, shaffoflik —
    layout'ni o'zgartirmaydi, UI bolalari ham birga kattalashadi / suriladi / so'nadi, sichqoncha chizilgan joyga tegadi
    (shaffoflik 0 — bosilmaydi). `GetScene().TweenUI(e, UITweenProperty::Alpha, glm::vec4(0.0f), 0.3f, { .Easing = ...,
    .Delay = ..., .Loop = UITweenLoop::PingPong, .Loops = 4, .From = ..., .OnComplete = ... })` — masshtab / alpha /
    siljish / pozitsiya / o'lcham / rasm rangi; bitta xususiyatga bitta tween; UI vaqtida (o'yin pauzada ham ishlaydi).
    Skriptda: `UIScaleTo`, `UIFadeTo`, `UIMoveTo`, `UIPunchScale` (hisoblagich "sakraydi"), `CancelUITweens`,
    `ShowUI(panel)` / `HideUI(panel)`. **UI Animation** komponenti: faollashganda (boshlanishda ham) **ko'rinish**
    animatsiyasi — paydo bo'lish, kattalashib chiqish (pop), chapdan / o'ngdan / yuqoridan / pastdan surilib chiqish,
    kechikish (ketma-ket chiquvchi menyu), egri chiziq; **yashirish** (`HideUI` o'ynab bo'lgach o'chiradi); tugma /
    belgilash / slayder / matn maydoni ustiga kelganda kattalashadi, bosilganda kichrayadi (bosilish javobi; bosilgan
    joy sichqoncha ostidan qochmaydi)
  - **Matn**: to'rtburchak ichida **tekislash** — chap / markaz / o'ng va yuqori / o'rta / past (dunyo matni obyekt nuqtasi
    atrofida, "tayanch" — birinchi qator chizig'i nuqtada); **so'z bo'yicha o'rash** (UTF-8: oʻ, gʻ, sh, kirill — bitta
    belgi, apostrof so'z ichida qoladi; uzun so'z sig'magan joyidan, defisdan keyin ham bo'linadi; dunyo matni —
    "Kenglik" bo'yicha); sig'maganda: **chiqib ketadi**, **kesiladi** (butun belgi va qatorlar) yoki **uch nuqta** (…);
    qator oralig'i. Shriftlar har bir o'lcham uchun alohida atlasda (ekrandagi o'lchamiga yaqin — kichik matn ham, katta
    sarlavha ham tiniq; UI matni butun piksellarga joylanadi). `TextLayout::Layout(...)` — sof hisob (testlar uchun ham).
    Eski sahnalarning "Centered" belgisi o'zi o'giriladi
  - **Editorda**: viewport'da UI obyektlari to'rtburchagi bo'yicha tanlanadi (shaffof joyidan ham; 2D va 3D sahnalarda —
    UI ekran ustida; Ko'rinish menyusida o'chiriladi), sichqoncha ostidagisi ingichka ramka bilan ko'rinadi. Tanlangan UI
    obyektning **to'rtburchak gizmo'si**: ichidan sudrash — ko'chirish (ota chetlari / markaziga yopishadi), 8 ta
    tutqich — o'lchamni o'zgartirish (Shift — nisbatni saqlash, Alt — markaz atrofida, magnit / Ctrl — 10 birlik qadam),
    pivot halqasi, ota to'rtburchagidagi **langarlar** (uchburchaklar, sudrab o'zgartiriladi, 0 / 0.5 / 1 ga yopishadi)
    va ulardan burchaklarga chiziqlar, o'lcham yozuvi; Esc — bekor qilish, Ctrl+Z — qaytarish. Layout guruhi
    joylashtirgan obyektda tutqichlar yo'q (yozuvi aytadi). `ZAMIN_EDITOR_UI_DRAG="bottom-right:60,-30"` — avtomatik sudrash
- **UI vidjetlari** (Obyekt > UI (ekran) menyusi, Komponent qo'shish > Effektlar va UI):
  - **UI Image** — tekstura yoki uning sprite kadri (atlas), rang; turi: oddiy (nisbatni saqlash bilan), **9-slice**
    (burchaklar ekran pikselida, chegara masshtabi), plitka, **to'ldirilgan** (gorizontal / vertikal / radial 360°,
    boshlanish tomoni, soat yo'nalishi, `FillAmount` — chiziqlar, taymerlar, qayta tiklanish belgilari); "Sichqonchani
    to'sadi" — ostidagi tugma/slayderlar bosilmaydi (dialog foni). UI Button bilan rangi tugma holatiga bo'yaladi
  - **UI Slider** — min/max/qiymat, butun sonlar, 4 yo'nalish, yo'lak + to'ldirish + tutqich (yumaloq yoki to'rtburchak),
    bosilgan joyga sakraydi, ushlab turilganda sichqonchaga ergashadi; `OnValueChanged(qiymat)`, `GetScene().SetSliderValue(e, v)`
  - **UI Toggle** — belgilash qutisi (galochka / kvadrat / nuqta) yoki **switch** (tugmacha silliq suriladi), yozuvi —
    obyektning Text komponenti; **guruhlar** (radio tugmalar: bittasi yoqiq, "o'chirishga ruxsat"); `OnToggleChanged(yoqiq)`,
    `GetScene().SetToggle(e, true)`
  - **UI Progress Bar** — qiymat 0..1, 4 yo'nalish, fon/to'ldirish ranglari, qiymatga qarab rang (sog'liq: qizil -> yashil),
    silliq ergashish (tezlik), foiz yozuvi
  - **UI Input Field** — bir qatorli matn maydoni: bosilganda tahrirlanadi (kursor sichqoncha joyiga), sudrab / Shift +
    strelkalar / ikki marta bosish bilan tanlash, Ctrl+A/C/X/V (tizim bufer), Home/End, Ctrl + strelka/Backspace — so'z
    bo'yicha, Enter — yuborish, Escape — bekor qilish (oldingi matn), boshqa joy bosilsa tahrir tugaydi; **UTF-8**
    (oʻ, gʻ, sh, kirill harflari bitta belgi), belgilar chegarasi, mazmun turi (oddiy / butun son / o'nlik son / harf va
    raqam / parol — nuqtalar bilan, buferga nusxalanmaydi), bo'sh paytdagi yozuv, faqat o'qish, kursor miltillashi,
    uzun matn surilib kursor ko'rinib turadi (maydon chetida kesiladi). `OnTextChanged`, `OnTextSubmit`, `OnTextEndEdit`,
    `GetScene().SetInputText` / `FocusInputField`. Editorda o'yin ko'rinishi fokusda bo'lganda yoziladi (editor
    yorliqlari o'sha payt ishlamaydi)
  - Hammasi event bus'ga ham chiqadi: `"UISliderChanged"` (float), `"UIToggleChanged"` (bool), `"UITextChanged"`,
    `"UITextSubmitted"`, `"UITextEndEdit"` (std::string), jo'natuvchi — vidjet obyekti. Nofaol / "Faol" belgisi o'chiq
    vidjet bosilmaydi. Matn kiritish uchun `Input::GetTextInput()` (yozilgan belgilar va avto-takrorli tugmalar) va
    `Input::GetClipboardText()` / `SetClipboardText()`. Namuna: Platformer2D `Scenes/UIWidgets.zscene` (`UIWidgetsDemo`)
  - **UI Mask** — obyektning butun UI ierarxiyasi faqat uning to'rtburchagi ichida chiziladi va sichqoncha ham faqat
    shu yerda topadi (chekka ichkariga / tashqariga suriladi); maska ichidagi maska ikkalasi kesishgan joyni ko'rsatadi;
    "O'z rasmini ko'rsatish" o'chiq bo'lsa faqat kesadi. To'liq yashiringan vidjetga navigatsiya o'tmaydi.
    `GetScene().GetUIClipRect(e, rect)` (ekran pikselida, scissor bilan)
  - **UI Scroll View** — kontent (masalan balandligi qatorlariga moslashuvchi ro'yxat) ko'rinish ichida suriladi:
    sichqoncha **g'ildiragi**, **sudrash** (tugma ustidan boshlansa ham — u holda tugma bosilmaydi), **scrollbar'lar**
    (o'zi chizadi: tutqichni sudrash, yo'lakni bosish; doim / kerak bo'lganda / yo'q), **inersiya** (sudrab qo'yib
    yuborilganda sekinlashib davom etadi), **elastik** chekka (tortilganda qarshilik qiladi va qaytadi) yoki chetda
    to'xtash; gorizontal / vertikal; viewport (bo'sh — o'zi, scrollbar'lar joyisiz; bolalari shu joyga joylashadi).
    Klaviatura / gamepad bilan tanlangan vidjet ko'rinadigan joyga **o'zi aylanadi**. `OnScrollChanged(pos)` (x 0 chap ..
    1 o'ng, y 1 tepa .. 0 past), `"UIScrollChanged"`; `GetScene().SetScrollPosition` / `ScrollBy` / `ScrollIntoView` /
    `GetScrollPosition`. Obyekt > UI (ekran) > Scroll view (ro'yxat bilan)
  - **Pauza menyusi** namunasi (Platformer2D 1-3 darajalar, `PauseMenu` skripti): Esc / gamepad Start — o'yin to'xtaydi
    va menyu ochiladi (Davom etish / Sozlamalar / Chiqish), musiqa bo'g'iqlashadi; Sozlamalar — scroll view ichida ovoz
    guruhlari (Master, Music, SFX, UI) slayderlari mikserga ulangan, **to'liq ekran** va **VSync** switch'lari, "Standart";
    hammasi SaveData'da saqlanadi va keyingi safar qaytadi. Strelkalar / D-pad tanlaydi, Enter / A bosadi, Esc / B orqaga
- **O'yin vaqti**: `SetTimeScale(0.5f)` — sekin harakat (OnUpdate vaqti, taymerlar, fizika, animatsiyalar shunga
  ergashadi), `PauseGame()` / `ResumeGame()` / `IsGamePaused()` — vaqt 0: o'yindan hech narsa qimirlamaydi, faqat
  `SetUpdateWhenPaused(true)` qilgan skriptlar (pauza menyusi) `OnUpdate` oladi; UI (vidjetlar, UI tween'lari,
  navigatsiya) doim haqiqiy vaqtda ishlaydi. `GetUnscaledDeltaTime()`, `QuitGame()` (player yopiladi, editorda Play
  to'xtaydi). **Display**: `Display::SetFullscreen` / `SetVSync` (player oynasiga qo'llanadi, editorda faqat eslab
  qoladi), `Display::SaveSettings()` / `LoadSettings()` (SaveData; player oynani saqlangan tanlov bilan yaratadi, buyruq
  qatori ustun; F11 / Alt+Enter to'liq ekranni almashtiradi). Player avtomatlashtirish: `ZAMIN_PLAYER_INPUT` da `down` / `up` (sudrash) va `scroll=dy`
- **Gamepad** (Xbox/PlayStation) qo'llab-quvvatlash
- Skriptlardan **sahna almashtirish** (`LoadScene`), prefab'larni `Instantiate` qilish
- **Sahna boshqaruvi** (`SceneManager`, editorning o'yin rejimi va player): bir vaqtda bir nechta sahna — **additive**
  yuklash (`LoadScene(path, LoadSceneMode::Additive)`, `UnloadScene`, `SetActiveScene`; har birining o'z obyektlari,
  skriptlari va fizika dunyosi, hammasi bitta kadrda ishlaydi va faol sahna kamerasi orqali birga chiziladi, hodisalar
  shinasi va o'yin soati umumiy; `FindEntity` / `Resolve(EntityRef)` boshqa yuklangan sahnalarda ham qidiradi);
  **`DontDestroyOnLoad(entity)`** — obyekt (bolalari bilan) doimiy sahnaga o'tadi: skript nusxasi, qiymatlari,
  obunalari va ijro etilayotgan ovozi saqlanadi (editor ierarxiyasida alohida bo'lim); **fonda yuklash**
  (`LoadSceneAsync` → `AsyncOperation`: fayl o'qish, JSON va rasmlarni ochish ishchi oqimda, obyektlar va GPU yuklash
  asosiy oqimda har kadr bir bo'lak, `GetProgress()`, `SetAllowSceneActivation`); **o'tishlar**
  (`SceneTransition::Fade(0.5f)` — rangga so'nib, almashib, ochiladi); **build sahnalar ro'yxati** (Loyiha sozlamalari →
  Build sahnalar: tartib, yoqish/o'chirish, sudrash, ochiq sahnani qo'shish, boshlang'ich sahna; `LoadScene(1)` yoki
  `LoadScene("Level2")`, `LoadNextScene()`, `ReloadScene()`, `LoadSceneAsync(2)`, `GetSceneBuildIndex()`; player:
  `--scene 2` yoki `--scene Level2`; eksportga faqat ro'yxatdagi sahnalar kiradi); **sahna hodisalari** —
  `OnSceneLoaded` / `OnSceneUnloaded` / `OnActiveSceneChanged` skript callback'lari va shinada `"SceneLoaded"`,
  `"SceneUnloaded"`, `"ActiveSceneChanged"` (`SceneEventInfo`: nom, yo'l, build indeksi, rejim); **tez qidiruv** —
  nom va teg indekslari (birinchi qidiruvda quriladi, keyin obyekt yaratish/o'chirish/nomlashda bittalab yangilanadi:
  10 000 obyektda `FindEntityByName` skandan ~2000x tez, `CountEntitiesWithTag` O(1)), komponent turi bo'yicha
  `FindEntitiesWith<T>()`, `FindObjectsOfType<T>()` (komponentlar yoki skript klasslari, barcha yuklangan sahnalarda;
  `ZaminBench --queries`); **yuklash xatolari** (`SceneLoadReport`) — JSON xatosi qator/ustun va fayl qatori bilan,
  noma'lum komponent, o'qilmagan komponent (obyekt qolgan komponentlari bilan yuklanadi), topilmagan asset, takroriy ID,
  ota obyekti yo'q / halqa: konsolda, editorda viewport ustidagi banner va "Sahna yuklash muammolari" oynasi, player'da
  o'yin yopilish o'rniga xato ekrani; **barqaror fayl tartibi** — `.zscene` obyektlari UUID bo'yicha saralangan,
  ierarxiya tartibi `RootOrder` / `ChildOrder` da: bir xil sahna doim bir xil matn (diff'lar faqat o'zgarganini
  ko'rsatadi), yuklashda obyektlar ierarxiya tartibida yaratiladi; **sahna metadata** — ta'rif, muallif, teglar
  (`Scene::GetMetadata()`, faylda `"Metadata"`; Sahna oynasining "Ma'lumot" bo'limida tahrirlanadi) va **sahna rasmi**
  (har saqlashda boshlang'ich kameradan 720p da chizilib 320x180 ga kichraytiriladi, sahna yonida yashirin
  `.Level1.zscene.png` fayl: alohida asset emas, sahna bilan ko'chadi / nusxalanadi / o'chadi; kontent brauzer, inspektor
  va Sahnalar oynasida ko'rinadi; `SceneSerializer::ReadSceneInfo` — sahnani yuklamay nomi, versiyasi, metadata va obyektlar
  soni); **"Sahnalar" oynasi** (Oyna → Sahnalar) — loyihaning barcha sahnalari rasmli kartalar yoki ro'yxat ko'rinishida:
  build indeksi, boshlang'ich sahna, eski format belgisi, qidiruv (nom, ta'rif, `#teg`), ochish, build ro'yxatiga
  qo'shish/olib tashlash, boshlang'ich qilish, rasmlarni chizish, "Play bilan" qo'shimcha (additive) yuklash; o'yin
  rejimida — yuklangan sahnalar (faol ★, DontDestroyOnLoad, fonda yuklanayotganlar), faol qilish, yopish, ierarxiyada
  ko'rsatish, o'yinga sahnani additiv yoki almashtirib yuklash va global holat (GameState qiymatlari jonli tahrirlanadi,
  servislar ro'yxati); **o'yin rejimidan chiqishda tiklash** — o'yin boshqa sahnaga o'tgan bo'lsa Stop editorni asl sahna,
  tanlov va kamera ko'rinishiga qaytaradi; **global o'yin holati** — `Services::Register<T>()` / `Get<T>()` /
  `GetOrCreate<T>()` (C++ servislar turi bo'yicha, sahnalardan tashqarida) va `GameState::SetInt / GetInt / Increment /
  SetString ...` kalit-qiymat ombori, `WatchGameState(key, fn)` (skript bilan tugaydi), doimiy kalitlar
  `GameState::SetPersistent` + `Save()` / `Load()` (SaveData); editor har Play boshida va oxirida tozalaydi;
  **sahna formati versiyasi va migratsiya** — `"Version"` (hozir 2), eski fayllar yuklashda `vN → vN+1` qadamlar
  zanjiri bilan yangilanadi (`SceneMigration`, konsolda bir marta yoziladi, saqlanganda yangi formatda), yangiroq
  dvigatel fayli ogohlantirish bilan o'qiladi; **yuklash vaqti** bosqichlar bo'yicha (o'qish, JSON, migratsiya,
  obyektlar, fonda rasmlar, assetlar, audio, fizika, skriptlar) — konsolda, profiler'da (`Sahna yuklash` bo'limi) va
  Statistika / Profiler oynalarining "Sahna yuklash vaqti" bo'limida (rangli chiziq, so'nggi 32 ta yuklash;
  `SceneLoadProfiler`, `AsyncOperation::GetTiming()`); **katta sahnalar uchun tez serializatsiya** — 10 000 obyekt
  Debug'da saqlash 1346 → 163 ms, yuklash 2076 → 222 ms (`ZaminBench --serialize`), ixtiyoriy **binar kesh**
  `.zsceneb` (MessagePack, ~3 marta kichik, yuklash 140 ms; eksportda har sahna yoniga yoziladi, player uni avtomatik
  o'qiydi); **sub-sahnalar** — `SubSceneComponent` boshqa `.zscene` faylini obyekt joyiga qo'shimcha sahna sifatida
  yuklaydi (transform bilan, ichma-ich, halqadan himoyalangan, `LoadSubScene()` / `UnloadSubScene()`), editorda joyida
  faqat ko'rish uchun chiziladi va ierarxiyada obyekt ostida ko'rinadi, eksport ularni ham kiritadi. Namuna:
  Platformer2D `Scenes/Menu.zscene` → `Scenes/Loading.zscene`
  (progress bar) → 1-daraja, `GameSession` (DontDestroyOnLoad: musiqa; tangalar, darajalar va rekord `GameState` da,
  darajalar statistikasi `SessionStats` servisida; HUD `WatchGameState` bilan yangilanadi); `Scenes/SubScenes.zscene`
  (bitta `Parts/Camp.zscene` bo'lagi uch joyda, uchinchisi skript bilan keyinroq)
  - Prefab'lar: [docs/Prefabs.md](docs/Prefabs.md) — nusxa va bog'lanish, override'lar, Apply / Revert, ichma-ich prefab'lar,
    variantlar, prefab rejimi, havolalar, hot reload, undo, bufer, fayl formati va versiya, tiklash, eksport qoidalari R1-R10
  - Batafsil qo'llanma: [docs/Scenes.md](docs/Scenes.md) — yuklash rejimlari, DontDestroyOnLoad, fonda yuklash, o'tishlar,
    build ro'yxati, hodisalar, qidiruv, xatolar, fayl formati, global holat, sub-sahnalar, profiler, binar kesh, kod misollari bilan
- **Loyiha sozlamalari** (Oyna → Loyiha sozlamalari, bo'limlar va qidiruv): player oynasi, ikonka, kompaniya / mahsulot / versiya,
  sifat darajalari (Low..Ultra), OpenGL konteksti, vaqt (`Time::SetTimeScale`), standart gravitatsiya, audio standartlari (3D masofa
  modeli), lokalizatsiya (`Assets/Localization/<til>.json`, `Localize("key")`, Text komponenti kaliti), editor tili (o'zbek / ingliz)
  - Batafsil qo'llanma: [docs/ProjectSettings.md](docs/ProjectSettings.md) — har sozlama, `.zproject` formati, player buyruq qatori, kod misollari
- **Player** (`ZaminPlayer`): oyna sozlamalari ustunlik tartibi bilan (buyruq qatori > o'yinchi saqlagani > loyiha), F11 / Alt+Enter
  bilan to'liq ekran (oyna avvalgi joyiga qaytadi, oyna turgan monitorda), FPS cheklovi (`--fps`, VSync bilan kelishilgan: 30 FPS
  60 Hz da VSync intervali 2, boshqalari uyqu + aylanish jadvali), Zamin logotipli splash ekran (birinchi sahna fonda yuklanadi,
  tugma bilan o'tkaziladi, o'z rasmi / fon rangi), `--windowed` / `--borderless` / `--resolution` / `--monitor` / `--no-splash` / `--help`;
  fonda ishlash o'chiq bo'lsa oyna fokusni yo'qotganda pauza (vaqt, fizika, audio to'xtaydi; oxirgi kadr xiralashib
  "Pauza" yozuvi), o'yinchining gamma va yorqinlik sozlamasi (oxirgi o'tish, `Display::SetGamma` / `SetBrightness`,
  saqlanadi, `--gamma` / `--brightness`; Platformer2D pauza menyusida slayderlar); `--version` (o'yin versiyasi va dvigatel
  build'i: git kommiti, sana - `BuildInfo`, log va crash hisobotlarida ham), `--benchmark N` (headless ishlash testi: o'rtacha,
  p95, p99 ms, JSON, chegaralar - `Tests/player_bench.sh`), avtomatik eksport testi (`Tests/export_test.sh`: editor eksporti,
  eksport qilingan o'yin xvfb'da), qayta ishlatiladigan o'yin menyusi (`Prefabs/GameMenu.zprefab`: O'ynash / Sozlamalar -
  ekran o'lchami, to'liq ekran, VSync, sifat, gamma, ovoz, til / Chiqish)
  - Batafsil qo'llanma: [docs/Runtime.md](docs/Runtime.md) — player, buyruq qatori, sozlamalar ustunligi, eksport, log, crash
    hisobotlari, versiya, ekran va kursor API, o'yin menyusi, ishlash va eksport testlari
- **Matematika va core**: son utilitalari (`Remap`, `SmoothDamp`, `MoveTowards`, burchaklar), kvaternionlar
  (`LookRotation`, `Slerp`, `RotateTowards`), geometriya (`AABB`, `Sphere`, `Ray`, `Plane`, `Frustum`; ray-AABB /
  sfera / tekislik / uchburchak, kesishuvlar, eng yaqin nuqtalar; skriptda `GetMouseRay()`), Perlin / Simplex shovqin
  (fBm, ridged), `AnimationCurve`, spline'lar, ranglar (HSV/HSL, hex, sRGB), vazifalar tizimi (`JobSystem::ParallelFor`),
  xotira statistikasi, log darajalari, xabarli assert'lar, `Stopwatch` / `Timer`, `StringUtils`, fayl yo'llari
  (`NormalizePath`, `SafeJoin`, vaqtinchalik papkalar)
  - Batafsil qo'llanma: [docs/Core.md](docs/Core.md) — har modul API'si misollar bilan, Showcase3D'dagi `Geometry` sahnasi
- **Navigatsiya va AI**: NavGrid 2D (tilemap / collider'lardan) va 3D (nurlar bilan pol), A* (binar heap, qisman yo'llar),
  string pulling va spline silliqlash, Nav Agent (`SetDestination`, nishon ortidan, nurlar bilan to'siqdan qochish),
  Nav Obstacle (grid'ni dinamik o'yish, qayta yo'l), steering (seek / flee / arrive / wander / pursue / evade / suruv),
  Sight Sensor (ko'rish konusi va chizig'i, `OnTargetSpotted`), Patrol Path, Formation (qator / kolonna / pona / doira),
  `StateMachine` va behavior tree (`BehaviorTreeBuilder`, inspektorda jonli daraxt), navigatsiya gizmo'si va
  `--debug-nav`, vazifalar tizimida guruhlangan yo'l qidirish (`ZaminBench --nav`), bake'ni `.znav` faylida saqlash
  - Batafsil qo'llanma: [docs/AI.md](docs/AI.md) — har tizim misollar bilan, namuna sahnalar (Chase2D, AI3D, Squad3D, Island)
- **Audio** (miniaudio): wav/mp3/flac/ogg
- **C++ skriptlar**: `ScriptableEntity` + `ZM_REGISTER_SCRIPT`, o'yin moduli `.so` sifatida yuklanadi, hot reload
- Zamin Player — eksport qilingan o'yinlarni ishga tushiruvchi runtime

---

## 🔧 Build qilish (Linux)

### Talablar
- C++20 kompilyator (GCC 13+ yoki Clang 17+), CMake 3.20+, Ninja (ixtiyoriy)
- OpenGL 4.1+ drayveri
- X11/Wayland dev paketlari

Ubuntu / Debian:
```bash
sudo apt install build-essential cmake ninja-build git \
    libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols zenity
```
Fedora:
```bash
sudo dnf install gcc-c++ cmake ninja-build mesa-libGL-devel libX11-devel libXrandr-devel \
    libXinerama-devel libXcursor-devel libXi-devel wayland-devel libxkbcommon-devel zenity
```
Arch:
```bash
sudo pacman -S base-devel cmake ninja mesa libx11 libxrandr libxinerama libxcursor libxi wayland libxkbcommon zenity
```

> `zenity` (yoki `kdialog`) — fayl ochish/saqlash dialoglari uchun.

### Build
```bash
git clone https://github.com/abbosjon-dev/zamin-game-engine.git
cd zamin-game-engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Boshqa barcha kutubxonalar (GLFW, GLM, EnTT, ImGui, ImGuizmo, Box2D, Jolt, nlohmann/json, stb, miniaudio, tinyobjloader) CMake orqali avtomatik yuklab olinadi (versiyalar qat'iy belgilangan).

### Ishga tushirish
```bash
./build/bin/ZaminEditor                          # launcher ochiladi
./build/bin/ZaminEditor Projects/Platformer2D    # namuna 2D o'yin
./build/bin/ZaminEditor Projects/Showcase3D      # namuna 3D sahna
./build/bin/ZaminEditor Projects/Adventure3D     # uchinchi shaxs 3D o'yin
./build/bin/ZaminPlayer Projects/Platformer2D    # o'yinni editor'siz ishga tushirish
./build/bin/ZaminPlayer Projects/Showcase3D --scene Scenes/DebugDraw.zscene --debug-raycasts --debug-contacts --debug-velocities
./build/bin/ZaminPlayer Projects/Adventure3D --borderless --monitor 1 --fps 60   # hamma variantlar: --help
ctest --test-dir build -LE gui                   # unit testlar (tezkorlari) + benchmarklar kichik o'lchamda (-L bench)
./build/bin/ZaminTests --filter "Prefab*" --timing   # nom bo'yicha filtr (glob, '-' chiqarib tashlaydi), vaqt hisoboti
./build/bin/ZaminTests --list                    # testlar nomlari; --help: --slowest N, --fail-fast, --report, --junit
ctest --test-dir build -L gui                    # editor smoke, skript hot reload, eksport testlari (bir necha daqiqa)
Tests/render_tests.sh                            # rendering regressiya testi (display kerak; yo'q bo'lsa xvfb-run)
Tests/editor_smoke.sh --shots                    # editorning barcha oyna/menyu/popup'lari (xvfb ostida)
Tests/hot_reload_test.sh                         # skript modulini qayta kompilyatsiya + hot reload
Tests/export_test.sh                             # eksport + eksport qilingan o'yin headless (skrinshot bilan)
Tests/package_games.sh --out dist                # uchala namuna o'yin: eksport + tekshiruv + <Nom>-<versiya>-linux-x86_64.tar.gz
Tests/render_bench.sh                            # 10 000 sprite, 1 000 mesh: ms / kadr (docs/Benchmarks.md)
Tests/bench.sh --compare                         # ECS, serializatsiya, prefab benchmarklari + boshlang'ich natija bilan solishtirish
Tests/coverage.sh [--gui] [--html]               # kod qamrovi (gcov, build-cov): modullar bo'yicha hisobot (docs/Coverage.md)
Tests/sanitize.sh address                        # unit testlar AddressSanitizer + LeakSanitizer bilan (build-asan)
Tests/sanitize.sh address --smoke --editor-smoke # + 3 o'yin, editor va editor smoke testi ASan ostida (CI'dagi asan job)
Tests/sanitize.sh undefined --compiler clang     # UndefinedBehaviorSanitizer bilan, Clang (build-ubsan; CI'dagi ubsan-clang)
Tests/format_check.sh [--base main] [--fix]      # clang-format faqat o'zgargan qatorlarda (.clang-format - mavjud uslub)
```

Barcha testlar, ular nimani qo'riqlashi va qachon ishga tushirilishi - [docs/Testing.md](docs/Testing.md) (unit testlar
ro'yxati bilan); o'lchangan natijalar (testlar soni, qamrov, sanitizerlar, benchmarklar, ogohlantirishlar) -
[docs/Quality.md](docs/Quality.md).

**Kod uslubi** (`.clang-format`, clang-format 17+): mavjud uslubni tasvirlaydi - 4 bo'shliq, qavslar yangi qatorda
(`namespace Zamin {` bundan mustasno, ichi chekinish bilan), `Type* name`, `template<typename T>`, `{ 1.0f, 2.0f }`, ustun
chegarasi yo'q (qo'lda bo'lingan qatorlar saqlanadi), include tartibi o'zgarmaydi. Kod bazasi butunlay qayta
formatlanmaydi: `Tests/format_check.sh` faqat o'zgargan qatorlarni tekshiradi (`--fix` - joyida tuzatadi, `--files` -
butun fayl), `--stats` butun kod baza konfiguratsiyadan qanchalik farq qilishini o'lchaydi (3.1 % qator; farqning
asosiy qismi - clang-format ifodalay olmaydigan odatlar: bir qatorda bir nechta qisqa ifoda, bir qatorli qisqa enum).
`vendor/` hech qachon formatlanmaydi. CI'dagi `format` job ogohlantiruvchi.

**CI artefaktlari:** har push / pull request'da `build` job uchala namuna o'yinni eksport qiladi, o'yinchi oladigan
holatda tekshiradi va **o'yin build'i** sifatida yuklaydi (`player-builds-gcc`, `player-builds-clang`: `.tar.gz` +
`.sha256`, 14 kun) - paket ochilib, `<O'yin>.sh` bilan darhol ishga tushadi. `sanitizers` job ASan (testlar, 3 o'yin,
editor, editor smoke testi) va Clang UBSan bilan; loglari `sanitizer-asan`, `sanitizer-ubsan-clang` artefaktlarida.

**Unit test framework** (`Tests/TestFramework.h`): `ZaminTests [namuna ...]` - `*`/`?` bilan namuna butun nomga
glob (`Prefab*`), ularsiz nomning bir qismi (`ZAMIN_TESTS` muhit o'zgaruvchisi kabi), katta-kichik harf farqsiz; bir
nechtasi `:` yoki `,` bilan, `-` bilan boshlangani chiqarib tashlaydi (`"Scene*:-*Fuzz*"`). `--list` faqat nomlarni
chiqaradi. Har test qatorida uning vaqti; `--timing` / `--slowest N` vaqt hisoboti (eng sekin testlar, ulushi,
taqsimot), `--report r.json` va `--junit r.xml` natijalarni faylga yozadi (ctest ularni `build/TestResults` ga
yozadi, CI artefakt qilib yuklaydi), `--fail-fast` birinchi xatoda to'xtaydi. Testdan chiqib ketgan istisno testni
yiqitadi, qolganlari ishlayveradi. Oxirgi qator har doim `N test, M xato`.

**Benchmarklar** ([docs/Benchmarks.md](docs/Benchmarks.md)): `ZaminBench --ecs` (sahna API'si va EnTT: yaratish,
komponentlar, view'lar, qidiruv, ierarxiya, `Scene::Copy`, o'chirish turlari, o'yin kadrlari), `ZaminBench --serialize`
(2D va 3D katta sahnalar JSON / binar kesh, nusxalash / joylash, har komponent narxi), `--serialize --prefabs`,
`--render`. `Tests/bench.sh` ularni JSON natijalari bilan ishga tushiradi va `--compare` bilan `Tests/Baselines/` dagi
Release boshlang'ich natijasi bilan solishtiradi (`Tests/bench_compare.py`). ECS benchmark topgan xato tuzatildi:
skriptlarning `Destroy` navbati takrorlanishni butun navbatdan qidirardi (bir kadrda minglab o'chirish kvadratik edi).

**Kod qamrovi** ([docs/Coverage.md](docs/Coverage.md)): `-DZAMIN_COVERAGE=ON` (`cmake/Coverage.cmake`, `coverage`
preset) faqat bizning target'larni `--coverage -O0` bilan build qiladi; `Tests/coverage.sh` alohida `build-cov` papkasida
build qiladi, unit testlarni (`--gui` bilan player va editor smoke testini ham) ishga tushiradi va
`Tests/coverage_report.py` gcov ma'lumotlarini modullar bo'yicha jamlaydi (qatorlar, funksiyalar, eng kam qamralgan
fayllar; `--html` - gcovr bilan har fayl). CI'da alohida `coverage` job.

**Rendering regressiya testi** (`ZaminRenderTests`, `Tests/RenderTests.cpp`): bir nechta etalon sahna (PBR + soyalar +
nuqtali/spot yorug'lik, materiallar, bir nechta quyosh va osmon, barcha post effektlar, debug ko'rinish, 2D, Showcase3D va
Platformer2D namunalari, editor tanlash konturi) 480x270 o'lchamda offscreen chiziladi va `Tests/Reference/*.png` bilan solishtiriladi: kanal farqi
`--threshold` (standart 8) dan katta piksellar `--max-percent` (standart 0.5%) dan oshsa test yiqiladi va
`build/RenderTests/` ga yangi rasm hamda farq rasmi (`<nom>.diff.png`, farqlar qizil) yoziladi. Ko'rinish ataylab
o'zgartirilganda etalonlar `Tests/render_tests.sh --update` yoki `cmake --build build --target update_render_references`
bilan qayta yaratiladi; `--filter`, `--list` ham bor. `render_tests` CMake target'i testni ishga tushiradi. Unit testlar
(`ZaminTests`) GPU'siz qoladi. Skript mahalliy kompyuterda ham, CI'da ham bir xil ishlaydi: har doim Mesa'ning dasturiy
rasterizatori (llvmpipe — etalonlar shu bilan olingan; GPU'li kompyuterda ham), display bo'lmasa qat'iy 1280x720x24 xvfb
ekrani. Har ishga tushishda `build/RenderTests/report.json` (har holat raqamlari, GL renderer) va `summary.md` (Markdown
jadval — GitHub Actions uni ish sahifasiga chiqaradi) yoziladi; yiqilgan holat uchun `<nom>.compare.png` (etalon | yangi |
farq yonma-yon) ham. `--save-all` o'tgan holatlar rasmlarini ham yozadi (CI artefaktlari: `render-tests-gcc`,
`render-tests-clang`). Boshqa build (masalan Clang): `ZAMIN_BUILD_DIR=build-clang Tests/render_tests.sh`.

**Editor smoke testi** (`ZAMIN_EDITOR_OPEN_ALL_PANELS=1`, `Editor/src/EditorSmokeTest.cpp`, `Tests/editor_smoke.sh`):
editor har bir oynani (alohida va hammasini birga), katta oynalarning har sahifasini (loyiha sozlamalari kategoriyalari,
editor sozlamalari, sahna statistikasi, sahna sozlamalari bo'limlari), har bir asosiy menyu va ichki menyuni, viewport
popup'larini, so'rov oynalarini (Zamin haqida, launcher, yangi skript, o'yinni build qilish, joylashuvni saqlash, kontent
brauzerining yaratish / nomini o'zgartirish / o'chirish so'rovlari), sahnadagi har bir obyektni tanlab, har bir komponent
inspektorini (vaqtinchalik obyektga navbat bilan qo'shib), har bir asset turining inspektorini, sprite muharririni, prefab
rejimini, tayyor joylashuvlarni, Play / Pauza / Simulate'ni bir necha kadrdan ochadi. Shu orada logga yozilgan xatolar
(OpenGL debug chiqishi — `ZAMIN_GL_DEBUG=1` debug kontekst so'raydi —, ImGui tekshiruvlari va editorning o'z xatolari) va
OpenGL xato bayroqlari (har kadr `glGetError`) sanaladi: biror xato bo'lsa editor 1 kodi bilan yopiladi. ImGui'ning
`IM_ASSERT` tekshiruvlari har bir build'da saqlanadi (`Zamin/ImGui/ImGuiConfig.h`): bajarilmagani logga xato bo'lib
yoziladi (Debug'da dastur to'xtaydi), shuning uchun Release CI'da ham ushlanadi. Loyiha nusxasi bilan ishlaydi; loyihaga
hech narsa yozilmaydi. **Hot reload testi** (`Tests/hot_reload_test.sh`, `Tests/Fixtures/HotReload`): editor yo'q modulni
o'zi kompilyatsiya qilib o'ynaydi, test manba kodini o'zgartirib modulni editor ishlab turganda tashqarida qayta build
qiladi, editor uni qayta yuklaydi va yana o'ynaydi — yangi kod sahnadagi maydon qiymatlari bilan ishlaydi. Tashqarida
yozilayotgan modul endi u o'zgarmay qolgandan keyingina yuklanadi (linker uni joyida yozadi). **Eksport testi** eksport
qilingan o'yinni skrinshot bilan ham ishga tushiradi (`Tests/png_check.py`: o'lcham, bir xil rang emasligi). Uchala test
`ctest -L gui` bilan va CI'da alohida qadamlar sifatida (artefaktlari bilan) ishlaydi.

**Sanitizer build'lar** (`-DZAMIN_SANITIZE=address|undefined|leak|thread`, `cmake/Sanitizers.cmake`; `CMakePresets.json`da
`asan`, `ubsan`, `lsan`, `tsan` presetlari): bayroqlar build'ning barcha target'lariga (vendor kutubxonalarga ham) va
`ZaminConfig.cmake` orqali tashqarida build qilinadigan o'yin modullariga beriladi; bizning koddagi har qanday UBSan
xabari fatal (`-fno-sanitize-recover=all`). Sanitizer build namuna o'yinlar modullarini loyihaning `Binaries` papkasiga
emas, o'zining `GameModules` papkasiga yozadi va build'ning o'z dasturlari (`<build>/bin`) o'sha yerdan yuklaydi
(`ZAMIN_SCRIPT_MODULE_DIR=<papka>` bilan istalgan build'da); editor build qiladigan skriptlar ham u yerga yoziladi,
eksport qilingan o'yin esa o'z modulini yuklaydi. Coverage build (`-DZAMIN_COVERAGE=ON`) ham xuddi shunday. `Tests/sanitize.sh <address|undefined|leak|thread> [--smoke] [--compiler clang] [--clean]` alohida papkada
build qiladi, `ZaminTests`ni (`--smoke` bilan player va editor'ni ham xvfb ostida) ishga tushiradi va testlar o'tsa ham
har qanday sanitizer xabarida yiqiladi. Uchinchi tomon kodining shovqini `Tests/sanitizers/*.supp` fayllarida
(izohlari bilan); bizning koddagi xato hech qachon u yerga yozilmaydi — tuzatiladi.

**Ogohlantirishlar:** bizning target'lar (engine, editor, player, testlar va benchmarklar, namuna o'yin modullari)
`-Wall -Wextra -Wpedantic` bilan build qilinadi (`zamin_target_settings`, `cmake/ZaminGame.cmake`), vendor sarlavhalari
`-isystem` orqali ulanadi. Build GCC va Clang bilan ogohlantirishsiz. `-ffp-contract=off`: GCC va Clang build'lari bir xil
float natija beradi (FMA qisqartirish yo'q — Jolt barcha bog'langan target'larga `-mfma` beradi; Clang avval qisqartirardi).

---

## 🎮 Birinchi o'yin

1. `ZaminEditor` ni oching → **Yangi loyiha** → nom va joy kiriting → **Yaratish**.
   Loyiha shablondan yaratiladi va C++ skriptlari avtomatik kompilyatsiya qilinadi.
2. Ierarxiyada `+` → obyekt yarating (masalan *2D → Fizik quti*).
3. Inspektorda **+ Komponent → C++ Script** → klassni tanlang.
4. **▶ O'ynash** (`Ctrl+P`).
5. **Fayl → O'yinni build qilish** — tayyor o'yin papkasi va `.sh` launcher hosil bo'ladi.

### Loyiha tuzilmasi
```
MeningOyinim/
├── MeningOyinim.zproject     # loyiha sozlamalari
├── CMakeLists.txt            # skript modulini build qilish (editor yaratadi)
├── Source/                   # C++ o'yin skriptlari
│   └── Scripts.cpp
├── Assets/                   # sahnalar, teksturalar, ovozlar, modellar
│   ├── Scenes/Main.zscene
│   └── Scenes/Main.zscene.zmeta  # asset UUID'si va import sozlamalari (versiya nazoratiga qo'shing)
├── .zamin/                   # editor keshi: AssetRegistry.json, Thumbnails/ (qayta yasaladi, .gitignore'da)
└── Binaries/libMeningOyinim.so   # kompilyatsiya qilingan skriptlar (avtomatik)
```

---

## 🧑‍💻 C++ skript yozish

```cpp
#include <Zamin/Zamin.h>
using namespace Zamin;

class Player : public ScriptableEntity
{
public:
    float Speed = 6.0f;          // inspektorda ko'rinadi
    float JumpImpulse = 9.0f;

    void OnCreate() override { ZM_INFO("Salom, {}!", GetName()); }

    void OnUpdate(Timestep ts) override
    {
        glm::vec2 v = GetVelocity();                 // Rigidbody2D
        v.x = Input::GetAxis("Horizontal") * Speed;  // A/D, strelkalar, gamepad
        SetVelocity(v);

        if (Input::IsActionJustPressed("Jump"))      // Space / gamepad A, bir marta
            ApplyImpulse({ 0.0f, JumpImpulse });
    }

    void OnTriggerBegin(Entity other) override       // tanganing collider'i - sensor
    {
        if (other.HasTag("Coin"))
        {
            SaveData::SetInt("Coins", SaveData::GetInt("Coins") + 1); // o'yinlar orasida saqlanadi
            PlaySound("Audio/coin.wav");
            Destroy(other);                          // kadr oxirida o'chiriladi
        }
    }
};

ZM_REGISTER_SCRIPT(Player,
    ZM_FIELD_RANGE(Player, Speed, 0.0f, 20.0f).Tooltip("Yurish tezligi"),
    ZM_FIELD(Player, JumpImpulse))
```

| Callback | Qachon |
|---|---|
| `OnCreate()` | Skript yaratilganda (o'yin boshlanganda / obyekt paydo bo'lganda). Sahnadagi hamma `OnCreate`'lar `OnEnable`'lardan oldin |
| `OnEnable()` | Skript yoqilganda: `OnCreate`'dan keyin va har safar `SetEnabled(true)` bo'lganda |
| `OnStart()` | Bir marta, birinchi `OnUpdate`'dan oldin (hamma skriptlar yaratilgan — boshqa obyektlarni qidirish uchun qulay) |
| `OnUpdate(ts)` | Har kadrda (fizikadan oldin), skript yoqilgan bo'lsa |
| `OnLateUpdate(ts)` | Har kadrda (fizikadan keyin — kamera kuzatish uchun qulay) |
| `OnCollisionBegin/End(other)` | Qattiq to'qnashuv (2D va 3D, ikkala collider ham sensor emas) |
| `OnTriggerBegin/End(other)` | Sensor (trigger) collider'ga kirish / chiqish (2D, 3D va Character Controller) |
| `OnDisable()` | Skript o'chirilganda (`SetEnabled(false)`) va `OnDestroy`'dan oldin |
| `OnDestroy()` | Obyekt o'chirilganda yoki o'yin to'xtaganda |
| `OnJointBreak(joint)` | Shu obyektning 2D joint'i uzilganda (`joint.Type`, `joint.ConnectedBody`, `joint.Force`, `joint.Torque`) |
| `OnJointBreak3D(joint)` | Shu obyektning 3D joint'i uzilganda (`joint.Type` - `Joint3DType::Fixed/Hinge/Distance/Slider`, `joint.ConnectedBody`, `joint.Force`, `joint.Torque`) |
| `OnSoundFinished(sound)` | Shu obyektning ovozi o'zi tugaganda: Audio Source'ning takrorlanmaydigan ovozi yoki skriptning `PlaySound` / `PlaySoundAt` ovozi oxirigacha chalinsa, yoki ovozlar limiti uni to'xtatsa (`sound.Stolen`); `sound.Handle`, `sound.Clip`, `sound.FromAudioSource`. Asosiy oqimda, kadr skriptlaridan keyin; `StopAudio` bilan to'xtatilganda kelmaydi. Sahna event bus'ida ham `"SoundFinished"` (payload `SoundFinished`) |
| `OnClick()` | Shu obyektdagi UI tugma bosilganda (sichqoncha, yoki tanlangan paytda Enter / gamepad A) |
| `OnPointerEnter/Exit(e)` | Sichqoncha shu UI obyekt (yoki uning UI bolasi) ustiga keldi / ketdi (`e.Target`, `e.Position`) |
| `OnPointerDown/Up(e)` / `OnPointerClick(e)` | Shu UI obyektda sichqoncha bosildi / qo'yib yuborildi (`e.Inside`) / bosib qo'yildi |
| `OnSelect()` / `OnDeselect()` / `OnCancel()` | Klaviatura / gamepad navigatsiyasi: vidjet tanlandi / tanlov ketdi / Esc yoki B |
| `OnValueChanged(value)` | Shu obyektdagi UI Slider o'yinchi tomonidan surilganda (yangi qiymat) |
| `OnToggleChanged(isOn)` | Shu obyektdagi UI Toggle bosilganda (yoki guruhdagi boshqasi yoqilib, bu o'chganda) |
| `OnTextChanged(text)` / `OnTextSubmit(text)` / `OnTextEndEdit(text)` | Shu obyektdagi UI Input Field: matn o'zgardi / Enter bosildi / tahrir tugadi (Enter, Escape, boshqa joy bosildi) |
| `OnScrollChanged(pos)` | Shu obyektdagi UI Scroll View kontenti surildi (g'ildirak, sudrash, scrollbar, inersiya, navigatsiya, kod); `pos` - x 0..1 chapdan, y 1..0 tepadan |
| `OnMessage(msg)` | Shu obyektga xabar kelganda (`SendMessage`, `Broadcast` ...) |

Tartib: `OnCreate → OnEnable → OnStart → (OnUpdate → OnLateUpdate)… → OnDisable → OnDestroy`. O'chirilgan skript (inspektorda **Faol** belgisi yoki `SetEnabled(false)`) `OnUpdate`/`OnLateUpdate` olmaydi va taymer/tween'lari to'xtab turadi; yoqilganda davom etadi (`OnStart` qayta chaqirilmaydi). To'qnashuv, trigger, xabar va `OnClick` o'chirilgan skriptga ham keladi — u o'zini javoban yoqishi mumkin. To'qnashuv/trigger har bir obyekt jufti uchun bir marta keladi (bir nechta collider yoki tilemap katakchalari bo'lsa ham); obyekt ichida turgan paytda o'chirilsa, qolgan tomon `OnTriggerEnd`/`OnCollisionEnd` oladi.

Obyekt nofaol bo'lsa (o'zi yoki ota-onalaridan biri: skriptda `SetActive(false)` yoki `SetActive(FindEntity("Eshik"), false)`, `entity.SetActive(false)`, `GetScene().SetActive(entity, false)`), skript ham o'chgan hisoblanadi: `OnDisable` darhol (`SetActive` qaytishidan oldin) keladi, `OnUpdate`/`OnLateUpdate`/taymerlar to'xtaydi, obyekt qayta faol bo'lganda `OnEnable` keladi (`OnStart` qayta chaqirilmaydi). Butun ierarxiya uchun tartib: avval ota, keyin bolalar; `OnDisable` paytida bolalarning fizik body'lari hali bor, `OnEnable` paytida esa allaqachon qayta yaratilgan. O'yin boshida nofaol bo'lgan (yoki nofaol shablondan `Instantiate` qilingan) obyekt skripti faqat `OnCreate` oladi — `OnEnable`/`OnStart` u faol bo'lganda chaqiriladi. Skriptning o'z `Enabled` belgisi o'zgarmaydi; `IsActiveAndEnabled()` ikkalasini birga tekshiradi. Xabarlar (`SendMessage`, `Broadcast`, event bus) nofaol obyekt skriptiga ham yetib boradi — u o'zini javoban qayta yoqishi mumkin; `OnClick` esa kelmaydi (nofaol tugma bosilmaydi). Obyekt o'chganda uning ovozi `OnDisable`dan keyin to'xtaydi, yoqilganda esa `OnEnable`dan oldin boshlanadi.

Foydali API:

| Soha | Funksiyalar |
|---|---|
| Obyektlar | `Transform()`, `GetComponent<T>()`, `AddComponent<T>()`, `FindEntity("Nom")`, `FindEntities("Nom")`, `SetName("Nom")`, `Destroy(e)`, `GetScript<T>(entity)` |
| Teglar | `HasTag("Enemy")`, `AddTag(...)`, `RemoveTag(...)`, `FindEntitiesWithTag("Enemy")`, `FindEntityWithTag(...)`, `entity.HasTag(...)` |
| Spawn | `Instantiate(entity, pos)`, `Instantiate("Prefabs/Ball.zprefab", pos)` |
| Ierarxiya | `GetParent()`, `SetParent(e)`, `GetWorldPosition()`, `GetChildren()`, `GetChild(i)`, `GetChildCount()`, `FindChild("Nom")`, `GetDescendants()`, `GetRoot()` |
| Bola/ota komponentlar | `GetComponentInChildren<T>()`, `GetComponentsInChildren<T>()`, `GetComponentInParent<T>()`, `GetComponentsInParent<T>()`, `FindInChildren<T>()`, `FindInParent<T>()` — `T` skript klassi ham bo'lishi mumkin |
| Xabarlar | `SendMessage(e, "Damage", 25.0f)`, `SendMessageToChildren(...)`, `SendMessageUpwards(...)`, `Broadcast("GameOver")`, `AddMessageHandler("Damage", fn)`, `msg.Get<float>()`, `msg.Sender` |
| Hodisalar (event bus) | `Subscribe("CoinCollected", fn)`, `Unsubscribe(id)`, `Emit("CoinCollected", qiymat)`, `EmitDeferred(...)` (kadr oxirida) |
| Tasodifiy sonlar | `Random::Range(1.0f, 5.0f)`, `Random::Int(1, 6)` (ikkala chegara ham kiradi), `Random::Float()`, `Random::Bool(0.3f)`, `Random::InsideCircle(r)`, `Random::OnSphere(r)`, `Random::InsideSphere(r)`, `Random::Rotation()`, `Random::Pick(v)`, `Random::Shuffle(v)`, `Random::SetSeed(42)`, alohida oqim: `RandomGenerator rng(seed)` |
| 2D fizika | `GetVelocity/SetVelocity/ApplyImpulse/ApplyForce/SetPosition2D`, `ApplyImpulse(other, impulse)`, `Raycast2D(origin, dir, dist, LayerMask({ "Ground" }))`, `Raycast2DAll(origin, dir, dist, mask, includeTriggers)` (hamma urilishlar, yaqini birinchi), `OverlapCircle2D(center, r, mask)` / `OverlapBox2D(center, halfExtents, angle, mask)` / `OverlapPoint2D(p, mask)` → `std::vector<Entity>`, `LayerIndex("Player")`, uxlash: `IsSleeping()` / `WakeUp()` / `PutToSleep()` (`IsSleeping(other)`, `WakeUp(other)`); joint'lar: `GetComponent<RevoluteJoint2DComponent>()` → `.MotorSpeed`, `.EnableLimit`, `.JointAngle`, `.ReactionForce`, `.Broken` ... |
| 3D fizika | `GetVelocity3D/SetVelocity3D/ApplyImpulse3D/ApplyForce3D/ApplyTorque3D/SetPosition3D`, `Raycast3D(...)`, uxlash: `IsSleeping3D()` / `WakeUp3D()` / `PutToSleep3D()` (`(other)` turlari ham); joint'lar: `GetComponent<HingeJoint3DComponent>()` → `.MotorSpeed`, `.EnableSpring`, `.SpringTargetAngle`, `.JointAngle`, `.ReactionForce`, `.Broken`; `SliderJoint3DComponent` → `.MotorSpeed`, `.JointTranslation` ... |
| Qahramon | `GetComponent<CharacterController3DComponent>()` → `.Move(velocity)`, `.Jump(speed)`, `.Grounded`, `.Velocity` (oxirgi fizika qadamidagi haqiqiy tezlik) |
| Animatsiya | `GetComponent<AnimatorComponent>().Play("Walk", loop, speed, fade)` yoki holat mashinasi: `SetFloat("Speed", v)` (blend tree ham), `SetBool`, `SetTrigger`, `SetState("Jump")`, `GetNormalizedTime(model)`; qatlamlar: `PlayLayer("UpperBody", "Wave")`, `SetLayerWeight("UpperBody", w, fade)`; `.ApplyRootMotion`, `.RootMotionVelocity`; blend shape'lar: `GetComponent<MeshRendererComponent>().SetMorphWeight("Smile", 0.8f)`, `GetMorphWeight("Smile")` |
| Sprite klip | `PlaySpriteClip("HeroSide")`, `GetSpriteClip()`; kadr event'lari `OnAnimationEvent(e)` orqali |
| Property animatsiya | `PlayAnimation("Klip", speed)` (speed < 0 — oxiridan orqaga), `StopAnimation()`, `IsAnimationPlaying()`; `GetComponent<AnimationPlayerComponent>().SetTime(t)`, `.PlayFrom("Klip", t, -1.0f)` (turgan joyidan orqaga); event'lar: `OnAnimationEvent(const AnimationEventInfo& e)` (`e.Name`, `e.Parameter`, `e.Clip`, `e.Skeletal`), `AddAnimationEventHandler("Footstep", fn)` |
| Input harakatlari | `Input::IsActionPressed("Fire")`, `IsActionJustPressed("Jump")`, `IsActionJustReleased(...)`, `GetAxis("Horizontal")`, `GetAxis2D("Horizontal", "Vertical")` |
| Input (qurilma) | `Input::IsKeyPressed / IsKeyJustPressed / IsKeyJustReleased`, `IsMouseButtonPressed / ...JustPressed / ...JustReleased`, `GetMouseDelta()`, `GetMouseScrollDelta()`, `IsGamepadButtonPressed / ...JustPressed`, `GetGamepadAxis`, `GetMousePosition()`, `GetMouseWorldPosition()`, `ScreenToWorld()` |
| Saqlash | `SaveData::SetInt/SetFloat/SetString/SetBool(kalit, qiymat)`, `GetInt/GetFloat/GetString/GetBool(kalit, standart)`, `HasKey`, `DeleteKey`, `DeleteAll`, `Save()` |
| Hayot sikli | `SetEnabled(bool)`, `IsEnabled()`, `HasStarted()`, `GetScene().SetScriptEnabled(entity, bool)` |
| Faollik | `SetActive(bool)`, `SetActive(entity, bool)`, `IsActive()` (o'z belgisi), `IsActiveInHierarchy()` (ota-onalari bilan), `IsActiveAndEnabled()`; `entity.SetActive(bool)`, `entity.IsActive()`, `entity.IsActiveInHierarchy()`, `GetScene().SetActive(entity, bool)` |
| Taymerlar | `Invoke(sekund, fn)`, `InvokeRepeating(kechikish, interval, fn)`, `CancelInvokes()`, `IsInvoking()` |
| Tween | `Tween(davomiylik, [](float t){...}, Ease::OutBack, onComplete)`, `TweenPosition(target, sek, ease, onComplete)`, `TweenScale(...)`, `CancelTweens()`, `ApplyEase(ease, t)` |
| UI animatsiya | `UIScaleTo({1.1f, 1.1f}, 0.2f)`, `UIFadeTo(0.0f, 0.3f, Ease::OutQuad, onComplete)`, `UIMoveTo({0, 40}, 0.4f)`, `UIPunchScale(0.2f)`, `TweenUI(UITweenProperty::Color, rang, sek, { .Loop = UITweenLoop::PingPong })`, `CancelUITweens()`, `ShowUI(panel)`, `HideUI(panel)` |
| Havolalar | `EntityRef Target;` maydoni + `Resolve(Target)` → `Entity` |
| Audio manba | `PlayAudio()` (obyektning `AudioSourceComponent`'ini boshidan chaladi; obyekt nofaol bo'lsa yoki ovozlar limiti rad etsa `false`), `StopAudio()`, `PauseAudio()` / `ResumeAudio()`, `IsAudioPlaying()` / `IsAudioPaused()`, `SetAudioVolume(v)`, `SetAudioPitch(p)`, `GetAudioTime()` / `SetAudioTime(s)`, `GetAudioLength()`; so'nish bilan — `PlayAudio(fadeIn)`, `StopAudio(fadeOut)`, `FadeAudio(volume, sekund)`, `GetAudioFade()`; filtr — `SetAudioLowPass(hz)`, `SetAudioHighPass(hz)`; boshqa obyektniki — `PauseAudio(entity)` va h.k.; `PlaySound(path, SoundParams)` — tasodifiy oraliq, ustuvorlik, nusxalar limiti, 3D o'rni, FadeIn, filtr bilan |
| Musiqa va mixer | `Audio::PlayMusic("Audio/theme.ogg", 1.0f)`, `Audio::CrossfadeTo("Audio/boss.ogg", 2.0f)`, `Audio::StopMusic(fade)`, `PauseMusic()` / `ResumeMusic()`, `GetMusic()`, `SetMusicVolume(v)`; `Audio::SetGroupVolume("SFX", 0.5f)`, `SetGroupMuted("Music", true)`, `SetGroupLowPass("SFX", 500)`, `ResetMixer()`; `Audio::SaveSettings()` / `LoadSettings()` — o'yinchi sozlamalari (SaveData); `Audio::GetClipLength(path)`, `Audio::Preload(path)`; `PlaySound(path, vol, pitch, "UI")` — guruh bilan |
| Debug chizish | `DebugDraw::Line(a, b, DebugColor::Red)`, `Ray(o, dir)`, `Arrow(a, b)`, `Box(center, half)` / `Box(center, half, quat)` / `Box(worldTransform)`, `Bounds(aabb)`, `Rect(c, size)`, `WireSphere(c, r)`, `Circle(c, normal, r)`, `Cross(p, size)`, `Point(p)`, `Axes(transform)`, `Path(points)`, `NavigationPath(points)`, `Text(p, "matn")` — oxirgi parametrlar: rang, `duration` (soniya, 0 = bir kadr), `depthTest` |
| Sahnalar | `LoadScene("Scenes/Level2.zscene")`, `LoadScene(path, LoadSceneMode::Additive)`, `LoadScene(path, SceneTransition::Fade(0.5f))`, `LoadScene(2)` (build indeksi), `auto op = LoadSceneAsync(path)` → `op->GetProgress()`, `op->SetAllowSceneActivation(false)`, `op->IsReady()`; `UnloadScene("HUD")`, `DontDestroyOnLoad()` / `DontDestroyOnLoad(entity)`, `LoadSubScene()` / `UnloadSubScene()` / `GetSubScene()`, `GetSceneManager()` (`GetActiveScene`, `GetLoadedScenes`, `FindEntityByName`) |
| Global holat | `GameState::Increment("Coins")`, `GameState::GetInt("Coins")`, `GameState::SetBool("Door.Open", true)`, `WatchGameState("Coins", [this](std::string_view) { ... })`, `GameState::SetPersistent("Best")` + `GameState::Save()` / `Load()`; servislar: `Services::Register<Inventory>()`, `GetService<Inventory>()`, `GetOrCreateService<QuestLog>()` |
| Boshqa | `EmitParticles(n)`, `OnParticlesFinished()`, `PlaySound(path)`, `PlaySoundAt(path, pos)`, `GetTime()`, `ZM_INFO/ZM_WARN/ZM_ERROR` (editor konsoliga) |

`ZM_FIELD` turlari: `float`, `int`, `bool`, `glm::vec2`, `glm::vec3`, `glm::vec4`, `std::string`, istalgan `enum` / `enum class` (har qanday asosiy tur: `uint8_t` ... `int64_t`), `EntityRef` (boshqa obyektga havola: inspektorda ierarxiyadan tortib tashlanadi yoki ro'yxatdan tanlanadi; nusxalash/prefab'da ichki havolalar yangi nusxaga moslashadi).

Maydon atributlari (faqat inspektordagi ko'rinishni o'zgartiradi):

```cpp
// Enemy klassi ichida:
enum class State { Idle, Patrol, Chase };
State Mode = State::Idle;
glm::vec4 Tint{ 1.0f };
float Speed = 5.0f;
int Health = 100;
std::string Notes;

ZM_REGISTER_SCRIPT(Enemy,
    ZM_FIELD_ENUM(Enemy, Mode, "Kutish", "Patrul", "Quvish"),   // ochiladigan ro'yxat ({"Boss", 10} - aniq qiymat)
    ZM_FIELD_COLOR(Enemy, Tint),                                 // rang tanlagich (vec3/vec4); ZM_FIELD_COLOR_HDR - 1 dan katta
    ZM_FIELD_RANGE(Enemy, Speed, 0, 20).Tooltip("Tezlik (m/s)"), // slayder [0, 20] + yordam matni
    ZM_FIELD_TOOLTIP(Enemy, Health, "Boshlang'ich jon"),
    ZM_FIELD(Enemy, Notes).Header("Boshqa").Label("Izohlar").Multiline())
```

Atributlar zanjir qilib qo'shiladi: `.Range(min, max)`, `.Tooltip("...")`, `.Header("Bo'lim")`, `.Label("Nom")`, `.Color(hdr)`, `.Multiline()`, `.Options({...})`. `ZM_FIELD_ENUM` `int` maydon uchun ham ishlaydi. Aniq qiymatli variant (`{"Boss", 10}`) enum'ning o'z qiymati bo'lishi kerak (yoki enum'ga tur bering: `enum class Mode : int`) — asosiy turi berilmagan enum faqat o'z qiymatlari oralig'idagi sonni saqlay oladi, boshqasi aniqlanmagan xatti-harakat (UBSan topadi). Maydon turi `int` ↔ `enum` o'zgarsa, sahnadagi qiymat saqlanib qoladi. Oddiy `glm::vec4` endi 4 ta son sifatida ko'rinadi — rang uchun `ZM_FIELD_COLOR` ishlating.

Yangi skript: **Skriptlar → Yangi C++ skript** (yoki Kontent brauzerida o'ng tugma). **To'liq** shablonda hamma callback'lar misollar bilan va har xil maydon turlari bor, **Minimal** — faqat `OnCreate`/`OnUpdate`. Oyna klass nomini darhol tekshiradi: C++ kalit so'zlari va `using namespace Zamin` bilan to'qnashadigan nomlar (`Camera`, `Entity`, `Input` ...) qabul qilinmaydi — masalan `CameraController` deb nomlang.

Input harakatlari (Jump, Fire, Horizontal, Vertical ...) **Fayl → Loyiha sozlamalari → Input** oynasida tugma, sichqoncha yoki gamepad'ga bog'lanadi va `.zproject` faylida saqlanadi. Oynada har bir harakat/o'qning jonli holati ko'rinadi — tugmani bosib sinab ko'rish mumkin. Qurilma holati har kadr boshida bir marta olinadi: `JustPressed` / `JustReleased` aynan bitta kadr davomida `true` bo'ladi (ikki kadr orasidagi juda tez bosish ham yo'qolmaydi). O'q qiymati: manfiy/musbat tugmalar (-1/+1) va gamepad o'qi (deadzone'dan keyin 0..1 ga qayta masshtablanadi) — kattarog'i olinadi; gamepad bog'lanishlari ulangan barcha gamepad'larni tinglaydi.

`SaveData` qiymatlari `~/.local/share/zamin/<loyiha>/savedata.json` faylida saqlanadi (`$XDG_DATA_HOME` bo'lsa o'sha joyda). Fayl o'yin to'xtaganda, sahna almashganda va dastur yopilganda avtomatik yoziladi (`SaveData::Save()` bilan darhol ham yozish mumkin). Editor'da **Loyiha sozlamalari → Saqlangan ma'lumotlar** oynasida qiymatlarni ko'rish, tahrirlash va o'chirish mumkin.

Teglar obyektlarni guruhlaydi: inspektorda **Komponent → Teglar** qo'shib, teg yozing (yoki ro'yxatdan tanlang). Loyihaning teg ro'yxati **Loyiha sozlamalari → Teglar** oynasida (qo'shish, qayta nomlash, sahnadagi foydalanish soni). Ierarxiya qidiruvida `#Enemy` deb yozsangiz, shu tegli obyektlar chiqadi. `FindEntity("Nom")` keshlangan: har kadrda chaqirish ham arzon (nomni `SetName` bilan o'zgartiring; `TagComponent::Tag` ga to'g'ridan-to'g'ri yozish keyingi kadrda seziladi).

Xabarlar va hodisalar:

```cpp
// Dushman: zarbani qabul qiladi
void OnCreate() override
{
    AddMessageHandler("Damage", [this](const ScriptMessage& m) { Health -= m.Get<float>(); });
    Subscribe("GameOver", [this](const ScriptMessage&) { Destroy(); }); // skript bilan birga o'chadi
}
// O'yinchi
SendMessage(FindEntityWithTag("Enemy"), "Damage", 25.0f);
Emit("CoinCollected");            // obuna bo'lganlarning hammasi darhol oladi
EmitDeferred("LevelDone", 2);     // shu kadrning OnLateUpdate'idan keyin
```

`Random` — PCG32 generatori: bir xil `seed` har qanday kompyuterda bir xil ketma-ketlik beradi (darajalarni qayta yaratish, replay uchun qulay).

Skriptni o'zgartirgach: **Skriptlar → Kompilyatsiya** (`Ctrl+B`) yoki loyihani tashqarida build qiling — editor `.so` o'zgarganini sezib, uni avtomatik qayta yuklaydi.

---

## ⌨️ Editor boshqaruvi

| Tugma | Amal |
|---|---|
| RMB + WASD / Q E | Kamerada uchish (Shift — tez, g'ildirak — tezlik) |
| Alt + LMB / MMB / RMB | Orbit / surish / zoom |
| G'ildirak | Yaqinlashtirish |
| Q / W / E / R | Tanlash / Ko'chirish / Aylantirish / Masshtab |
| F / Shift+F | Tanlanganlarga fokus / Butun sahnani ko'rsatish |
| Ctrl+1..5 / 1..5 | Kamera xatcho'pini saqlash / unga qaytish |
| Ctrl (gizmo bilan) | Snapping'ni vaqtincha yoqish/o'chirish (toolbar'dagi magnitga teskari) |
| Alt + gizmo'ni sudrash | Nusxasini sudrash (asl obyekt joyida qoladi) |
| Ctrl+D (sudrash paytida) | Shu joyda nusxa qoldirib, sudrashni davom ettirish |
| Ctrl+P | O'ynash / To'xtatish |
| Ctrl+S / Ctrl+Shift+S | Saqlash / Boshqacha saqlash |
| Ctrl+O / Ctrl+Shift+O | Sahna / Loyiha ochish |
| Ctrl+N | Yangi sahna |
| Ctrl+D / Delete / F2 | Nusxa / O'chirish / Nomini o'zgartirish (Ctrl+D va Delete — barcha tanlanganlarga) |
| Ctrl / Shift + bosish, Ctrl+A | Tanlovga qo'shish / oraliqni tanlash, hammasini tanlash |
| H / Alt+H | Tanlanganlarni editorda yashirish/ko'rsatish / Hammasini ko'rsatish |
| Alt+Shift+A | Tanlanganlarni nofaol qilish / faollashtirish (o'yin holati; Play paytida ierarxiyada ham) |
| Ctrl+Z / Ctrl+Y | Bekor qilish / Qaytarish |
| Ctrl+C / Ctrl+V | Nusxalash / Joylash |
| Ctrl+B | C++ skriptlarni kompilyatsiya qilish |
| Kontent brauzerda: Delete / F2 / Enter / Backspace | O'chirish / Nomini o'zgartirish / Ochish / Yuqori papka (`Ctrl+A` — hammasini tanlash, `Ctrl` + g'ildirak — katakcha o'lchami) |

---

## 🏗️ Arxitektura

```
Zamin/src/Zamin/
├── Core/        Application, Window, Layer, Input (+ harakatlar), SaveData, Log, UUID, Timestep
├── Events/      Oyna, klaviatura, sichqoncha hodisalari
├── Renderer/    RHI interfeyslari (Buffer, Shader, Texture, Framebuffer, RendererAPI),
│                Renderer2D, Renderer3D, PostProcess, Font, Mesh, EditorCamera
├── Scene/       Scene (EnTT), Entity, Components, SceneSerializer, SceneCamera
├── Scripting/   ScriptableEntity, ScriptRegistry, ScriptEngine (.so yuklash, hot reload), EventBus
├── Math/        Math (transform), Random (PCG32)
├── Physics/     PhysicsWorld2D (Box2D: jismlar, joint'lar, platformalar, material aralashtirish, effektorlar,
│                overlap / raycast-all so'rovlari, uxlash orollari, statistika), Polygon2D (qavariq bo'laklarga ajratish, kesish, sprayt konturi),
│                PhysicsMaterial (.zphysmat), CollisionLayers, PhysicsWorld3D (Jolt: jismlar, joint'lar, qahramonlar),
│                CollisionShapes3D (meshdan qavariq qobiq, kesh)
├── Audio/       AudioEngine (miniaudio: mixer guruhlari va daraja o'lchagichlari, musiqa crossfade, streaming, so'nish,
│                oldindan yuklash), AudioMixer (guruhlar daraxti, mute / solo hisobi, o'yinchi sozlamalari), AudioFilter
│                (low-pass / high-pass biquad), AudioClip (fayl ma'lumoti, import sozlamalari), Audio (o'yin API'si)
├── Project/     Project, shablonlar, build fayllari
├── ImGui/       ImGuiLayer, tema, ikonkalar
└── Platform/
    ├── GLFW/    Oyna va input (Linux, Windows, macOS uchun umumiy)
    ├── OpenGL/  RHI ning OpenGL implementatsiyasi
    └── Linux/   Fayl dialoglari, jarayonlar
Editor/          Zamin Editor (ImGui)
Runtime/         Zamin Player (eksport qilingan o'yinlar uchun)
Projects/        Namuna o'yinlar (Platformer2D, Showcase3D, Adventure3D)
Tests/           Headless unit testlar
```

- `libZamin.so` — dvigatel **shared library**: editor, player va o'yin skript modullari bitta nusxadan foydalanadi.
- Renderer kodi faqat `RendererAPI`/`Shader`/`Texture`/... interfeyslari bilan ishlaydi — yangi backend (Vulkan, Metal, GLES) qo'shish uchun faqat `Platform/<API>/` implementatsiyasi yoziladi.
- Platformaga xos kod `Platform/` ichida izolyatsiya qilingan.

---

## 🗺️ Yo'l xaritasi

- [x] Linux: editor, 2D/3D renderer, 2D fizika, audio, C++ skriptlar + hot reload, eksport
- [x] Undo/Redo, obyektlar ierarxiyasi (parent/child), prefab'lar, copy/paste
- [x] 3D fizika (Jolt), raycast'lar
- [x] Sprite animatsiya, particle tizimi, ekran UI, gamepad
- [x] Post-processing (bloom, vinyetka, rang sozlamalari)
- [x] Tilemap (editor'da chizish), 3D ovoz
- [x] glTF import, HDR pipeline, normal/metal-roughness/emissive map'lar
- [x] Skeletal animatsiya (glTF skins), Animator, CPU profiler
- [x] Animatsiyalar orasida silliq o'tish (crossfade), Character Controller 3D
- [x] Animatsiya holat mashinasi (parametrlar, holatlar, shartli o'tishlar, exit time, trigger'lar)
- [x] Root motion, animatsiya event'lari, property animatsiya va timeline, blend tree, qatlamlar
- [x] Cascaded shadow maps, tuman
- [x] SSAO
- [x] HDRI osmon va IBL (SH9 + prefiltered mip)
- [ ] **Windows** (MSVC, `.dll` skript moduli)
- [ ] **macOS** (Metal backend)
- [ ] **Android** (OpenGL ES / Vulkan)
- [ ] **iOS** (Metal)

---

## 📦 Uchinchi tomon kutubxonalari

GLFW (zlib), glad (MIT/Public), GLM (MIT), EnTT (MIT), Dear ImGui (MIT), ImGuizmo (MIT), Box2D (MIT), Jolt Physics (MIT), nlohmann/json (MIT), cgltf (MIT), stb (Public Domain/MIT), miniaudio (Public Domain/MIT-0), tinyobjloader (MIT). Shriftlar: Inter (SIL OFL 1.1), Font Awesome Free (SIL OFL 1.1) — litsenziyalar `Resources/fonts/` ichida.

## Litsenziya

MIT — [LICENSE](LICENSE).
