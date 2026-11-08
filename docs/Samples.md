# Namuna o'yinlar galereyasi

Zamin Engine bilan to'qqizta namuna o'yin keladi (`Projects/`). Oltitasi M24 bosqichida yozilgan kichik, lekin to'liq
o'yinlar: bosh menyu, pauza, natijalar oynasi, rekordlar, klaviatura va gamepad, o'zbek va ingliz tili, demo bot.
Gameplay'ning hammasi C++ skriptlarda (`Projects/<O'yin>/Source/`), rasm, model va ovozlar esa koddan yaratilgan
(`Tools/samples/gen_<o'yin>.py`, [Tools/samples/README.md](../Tools/samples/README.md)). Pastdagi rasmlar haqiqiy
o'yindan, `ZaminPlayer` da headless olingan (bot o'ynayapti).

- [Olti yangi o'yin](#olti-yangi-oyin) · [Eski namunalar](#eski-namunalar)
- [Hammasiga umumiy](#hammasiga-umumiy): [menyular](#menyular), [rekordlar (SaveData)](#rekordlar-savedata),
  [boshqaruv: input action'lar va gamepad](#boshqaruv-input-actionlar-va-gamepad), [demo rejim](#demo-rejim)
- [Testlar](#testlar) · [Eksport qilib tekshirish](#eksport-qilib-tekshirish) · [Ishlash tezligi](#ishlash-tezligi)
- [Rasmlarni yangilash](#rasmlarni-yangilash)

Ishga tushirish: editor launcher'ida **Namuna o'yinlar** kartasini bosing yoki player bilan:

```bash
./build/bin/ZaminPlayer Projects/Breakout              # o'yin: bosh menyudan
ZAMIN_DEMO=1 ./build/bin/ZaminPlayer Projects/Racing   # demo: bot o'ynaydi
```

## Olti yangi o'yin

| | |
|---|---|
| ![Neon Maydon](images/sample-topdownshooter.jpg) | ![Zamin Breakout](images/sample-breakout.jpg) |
| **[Neon Maydon](../Projects/TopDownShooter/README.md)** — 2D twin-stick shooter | **[Zamin Breakout](../Projects/Breakout/README.md)** — 2D g'isht sindirish |
| ![FPS Arena](images/sample-fpsarena.jpg) | ![Omborchi](images/sample-sokoban.jpg) |
| **[FPS Arena](../Projects/FPSArena/README.md)** — 3D birinchi shaxs, parkur va nishon otish | **[Omborchi](../Projects/Sokoban/README.md)** — 3D Sokoban, 10 daraja |
| ![Ipak Yo'li](images/sample-endlessrunner.jpg) | ![Vodiy Poygasi](images/sample-racing.jpg) |
| **[Ipak Yo'li](../Projects/EndlessRunner/README.md)** — 3D cheksiz yugurish | **[Vodiy Poygasi](../Projects/Racing/README.md)** — 3D poyga |

### Neon Maydon (`Projects/TopDownShooter`)

Neon maydonda kema: chap stik / WASD bilan yurasiz, o'ng stik / sichqoncha bilan nishonga olasiz. Dushmanlar
to'lqin-to'lqin bo'lib portallardan chiqadi (besh xil: quvuvchi, to'da, otuvchi, hujumchi, har 5-to'lqinda tank),
ketma-ket o'ldirishlar kombo ko'paytuvchisini oshiradi, bonuslar — jon, tez otish, uchtalik o'q.
**Ko'rsatadi:** 2D yorug'lik va neon pol (Lit sprite'lar), bloom, zarralar va chaqnash yorug'liklari, kamera
silkinishi, `Instantiate` bilan shablonlardan dushman va o'qlar, 2D fizika qatlamlari, UI tween'lar.
**Rekord:** eng katta hisob va eng uzoq to'lqin.

### Zamin Breakout (`Projects/Breakout`)

Raketka bilan to'pni qaytarib g'ishtlarni sindirasiz: besh xil g'isht (oddiy, qattiq, po'lat, oltin, bomba), olti
bonus (keng raketka, uchta to'p, sekin to'p, olov to'pi, lazer, +1 jon), olti daraja. **Ko'rsatadi:** Box2D (tez
to'p — `Bullet`, to'qnashuv callback'lari, lazer uchun `Raycast2D`), darajani matndan qurish, zarralar, bloom.
**Rekord:** eng katta hisob va eng uzoq daraja.

### FPS Arena (`Projects/FPSArena`)

Quyosh botayotgan arenada birinchi shaxs: sichqoncha / o'ng stik bilan qarash, yugurish (chidamlilik bilan), sakrash
(coyote time), cho'kkalash, sakratgichlar. Ikki rejim: parkur (oltita nuqta, aylana vaqti) va 60 soniyalik nishon
otish (raycast qurol, to'rt xil nishon, seriya ko'paytuvchisi). **Ko'rsatadi:** Character Controller 3D, kursor
qulfi, `Raycast` bilan otish, soyalar, SSAO, tuman, normal xaritali PBR materiallar. **Rekord:** eng yaxshi aylana
vaqti va nishon otish ochkosi.

### Omborchi (`Projects/Sokoban`)

Kichik robot qutilarni yorqin doiralarga itaradi: 10 ta daraja, orqaga qaytarish (undo), darajani qaytadan boshlash,
darajalar menyusi. Demo bot har darajani BFS yechuvchi bilan yechadi. **Ko'rsatadi:** o'yin qoidalari dvigateldan
alohida (`Board.cpp`) — headless test qilinadi, katakma-katak silliq harakat (tween), konfetti zarralari, 3D yorug'lik
va soyalar. **Rekord:** har daraja uchun eng kam qadam.

### Ipak Yo'li (`Projects/EndlessRunner`)

Ipak yo'li bo'ylab savdogar yuguradi: uch yo'lak, sakrash, sirpanish, besh xil to'siq naqshi, tangalar, magnit va
qalqon. Tezlik masofa bilan oshadi, dunyo oldinda protsedurali quriladi. **Ko'rsatadi:** cheksiz yo'l (UV
siljishi), obyektlarni qayta ishlatish, kichik qadamli simulyatsiya (sekin kadrda ham to'siqdan o'tib ketmaydi),
tuman va kun yorug'ligi. **Rekord:** eng katta ochko va eng uzoq masofa.

### Vodiy Poygasi (`Projects/Racing`)

Tog' vodiysidagi 750 metrlik aylanma trassada uchta bot raqib bilan uch aylana: start svetofori, 8 ta nazorat
nuqtasi, aylana vaqti, o'rinlar. Arkada mashina fizikasi: tezlanish, tezlikka bog'liq rul, yon ilashish, qo'l
tormozida drift, o'tda sekinlashish. **Ko'rsatadi:** generator yasagan `.obj` trassa (Catmull-Rom spline), mashinalar
to'qnashuvi, g'ildirak tutuni, dvigatel va sirpanish ovozi (balandligi tezlikka qarab). **Rekord:** eng yaxshi
aylana vaqti.

## Eski namunalar

| Platformer2D | Showcase3D | Adventure3D |
|---|---|---|
| ![Platformer2D](images/game-platformer2d.jpg) | ![Showcase3D](images/game-showcase3d.jpg) | ![Adventure3D](images/game-adventure3d.jpg) |
| [2D platformer](../Projects/Platformer2D/README.md): tilemap, parallaks, 2D yorug'lik, menyu va yuklanish ekrani | [3D ko'rgazma](../Projects/Showcase3D/README.md): PBR, soyalar, post-processing, fizika, zarralar | [3D sarguzasht](../Projects/Adventure3D/README.md): orol, uchinchi shaxs qahramon, NPC'lar, animatsiya |

## Hammasiga umumiy

### Menyular

Har o'yinda bosh menyu (orqasida bot o'ynaydi), pauza menyusi (Esc / Start) va natijalar oynasi bor; hammasi UI
komponentlaridan ([UI.md](UI.md)), klaviatura, sichqoncha va gamepad bilan boshqariladi (UI navigatsiyasi). Menyular
ochilganda jurnalga `Menyu: ...` yoziladi — testlar shu qatorlarni tekshiradi.

### Rekordlar (SaveData)

Har o'yin o'z rekordini [`SaveData::SubmitRecord`](Scripting.md#18-saqlash-lokalizatsiya-log) bilan saqlaydi:
natija avvalgisidan yaxshi bo'lsa yoziladi (vaqt va qadamlar uchun `RecordOrder::Lower`). Rekord o'yin tugaganda,
pauzadan bosh menyuga chiqilganda, qaytadan boshlanganda va o'yin yopilganda hisoblanadi; bosh menyu orqasidagi bot
rekord qo'ymaydi. Ishga tushganda o'yin `Rekordlar: ...`, yangi rekordda `Yangi rekord: ...` deb yozadi; bosh menyu va
HUD rekordni ko'rsatadi, natijalar oynasi esa "YANGI REKORD!" deydi.

| O'yin | SaveData kalitlari | Qaysi biri yaxshi |
|---|---|---|
| Neon Maydon | `Neon.BestScore`, `Neon.BestWave` | katta |
| Zamin Breakout | `Breakout.BestScore`, `Breakout.BestLevel` | katta |
| FPS Arena | `FPSArena.BestLap` / `FPSArena.BestRange` (+ `FPSArena.Sensitivity` sozlamasi) | kichik vaqt / katta ochko |
| Omborchi | `Sokoban.Best.<daraja>` (+ `Sokoban.Level` — keyingi daraja) | kam qadam |
| Ipak Yo'li | `EndlessRunner.Best`, `EndlessRunner.BestDistance` | katta |
| Vodiy Poygasi | `Racing.BestLap` (har aylana tugaganda) | kichik vaqt |

Fayl: `~/.local/share/Zamin Studio/<Mahsulot>/savedata.json`. `ZAMIN_DEMO_RECORDS=1` bilan demo botning natijasi ham
rekord bo'ladi — testlar rekord yozilib, keyingi ishga tushirishda o'qilishini shunday tekshiradi.

### Boshqaruv: input action'lar va gamepad

O'yinlar tugmalarni to'g'ridan-to'g'ri o'qimaydi: hamma narsa loyihaning input xaritasidagi harakat, o'q va
vektorlardan (`Input::IsActionPressed("Fire")`, `Input::GetVector("Move")`; Loyiha sozlamalari → Input). Har harakatning
klaviatura / sichqoncha va gamepad bog'lanishi bor; tugmani qayta bog'lash uchun kodni o'zgartirish shart emas.
Bosh menyudagi boshqaruv yozuvi ham xaritadan olinadi (matn komponentining **Boshqaruv ishoralari**, `InputHints`):
klaviatura bilan "WASD - yurish", gamepad qo'lga olinishi bilan "chap stik / D-pad - yurish" ko'rinadi
([UI.md](UI.md#6-matn)).

| O'yin | Harakatlar (klaviatura · gamepad) | O'qlar / vektorlar |
|---|---|---|
| Neon Maydon | `Fire` LKM, J · RT, RB; `Dash` Space, Shift · A, LB; `Pause` Esc, P · Start | `Move` WASD · chap stik, D-pad; `Aim` strelkalar · o'ng stik |
| Zamin Breakout | `Action` Space, LKM, Up, W · A, RT; `Pause` Esc, P · Start | `MoveX` A/D, strelkalar · chap stik, D-pad |
| FPS Arena | `Jump` Space · A; `Sprint` Shift · L3; `Crouch` C, Ctrl · B; `Fire` LKM · RT, RB; `Reload` R · X; `Pause` Esc, P · Start | `Move` WASD, strelkalar · chap stik; `Look` sichqoncha · o'ng stik |
| Omborchi | `Undo` Z, Backspace · X; `Restart` R · Y; `Pause` Esc, P · Start | `Move` WASD, strelkalar · chap stik, D-pad |
| Ipak Yo'li | `Left` / `Right` A/D, strelkalar · D-pad, LB/RB; `Jump` Space, W, Up · A; `Slide` S, Down · B; `Pause` Esc, P · Start | `Steer` · chap stik |
| Vodiy Poygasi | `Handbrake` Space · X, A; `Reset` R · Y; `Pause` Esc, P · Start | `Steer` A/D, strelkalar · chap stik; `Throttle` W, Up · RT; `Brake` S, Down · LT |

`Tests/samples_assets.py` (ctest `SampleAssets`) xaritani tekshiradi: har harakatda ikkala qurilma bor, skriptlar
o'qiydigan har nom xaritada bor va xaritadagi har nom o'qiladi, skriptlar klaviatura yoki gamepad'ni to'g'ridan-to'g'ri
so'ramaydi (faqat sichqoncha — nishon / qarash uchun), menyudagi ishora faqat xaritadagi nomlarni ishlatadi.

### Demo rejim

`ZAMIN_DEMO=1` (yoki o'yin skriptining `AutoDemo` maydoni) — bot o'ynaydi va jurnalga `Demo: ...` qatorlarini yozadi.
FPS Arena'da `ZAMIN_DEMO=range` — nishon otish raundi; Breakout va Omborchi'da `ZAMIN_START_LEVEL=<n>` — shu
darajadan. Kadr vaqti qat'iy bo'lsa (`ZAMIN_CAPTURE_FPS=20`), bot har kompyuterda bir xil o'ynaydi.

## Testlar

`Tests/samples_test.py` (ctest `Sample<O'yin>`, label `samples`) har o'yinni player'da headless ishga tushiradi
([Testing.md](Testing.md)):

| Holat | Nima tekshiriladi |
|---|---|
| `demo` | bot o'ynaydi: ochko, to'lqin / daraja / aylana / masofa o'sadi (`Demo: ...` qatorlari) |
| rekord (`demo` dan keyin) | demo natijasi `Yangi rekord: ...` bo'lib SaveData fayliga yoziladi; o'sha HOME bilan ikkinchi ishga tushirish `Rekordlar: ...` da xuddi shu qiymatni o'qiydi va bosh menyuni ko'rsatadi |
| `menus` | klaviatura: bosh menyu → o'yin → pauza → davom → pauza → bosh menyu (ba'zilarida natijalar oynasi ham) |
| `gamepad` | faqat gamepad (simulyatsiya: `ZAMIN_PLAYER_INPUT` `pad=A`, `stick=LeftX,0.8`): A bilan o'yin, o'yinning o'z boshqaruvi (stik, tugmalar, triggerlar), Start bilan pauza, D-pad + A bilan bosh menyu; ishoralar gamepad'ga o'tadi (`Kiritish qurilmasi: gamepad`) va o'yinda natija bor (o'q otildi, g'isht sindi, daraja yechildi, masofa) |
| `smoke` | eski namunalar ishga tushadi, rasm chiqaradi |

```bash
python3 Tests/samples_test.py --game Racing                # bitta o'yinning hamma holatlari
python3 Tests/samples_test.py --kind gamepad               # olti o'yin gamepad bilan
ctest --test-dir build -L samples                          # hammasi (sekin: ~25 daqiqa, llvmpipe)
```

## Eksport qilib tekshirish

`Tests/samples_export.py` (ctest `SampleExports`, label `samples;slow`) to'qqiz namunaning har birini editor bilan
eksport qiladi va eksport qilingan o'yinni o'yinchi qanday olsa shunday tekshiradi: `Tests/export_test.sh`
(Release skript moduli, eksport papkasi tekshiruvi, `<O'yin>.sh --version`, vaqtli ishga tushirish, skrinshot,
boshqa papkaga ko'chirish), keyin eksportdagi `<O'yin>.sh` bilan `samples_test.py --exported` holatlari — olti yangi
o'yinning bot demosi (rekord bilan) va eski namunalarning smoke'i.

<!-- exports -->

## Ishlash tezligi

<!-- bench -->

## Rasmlarni yangilash

- `python3 Tools/samples/make_gallery.py` — shu sahifadagi o'yin rasmlari (`docs/images/sample-*.jpg`, 960x540);
- `python3 Tools/samples/make_thumbnails.py` — launcher kartalaridagi rasmlar (`Projects/*/Thumbnail.jpg`);
- `python3 Tools/samples/gen_<o'yin>.py` — o'yinning assetlari, sahnasi va loyiha fayli (generator o'zgarsa;
  `Tests/samples_assets.py --regen` natija bir xil ekanini tekshiradi).
