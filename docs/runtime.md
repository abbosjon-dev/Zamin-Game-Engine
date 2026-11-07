# Zamin Engine: runtime player va eksport

Bu qo'llanma o'yinni editordan tashqarida ishga tushiradigan **Zamin Player** (`ZaminPlayer`) haqida: uni qanday
ishga tushirish, buyruq qatori, sozlamalar qayerdan olinishi, log va crash hisobotlari, versiya ma'lumoti, kursor
va ekran API'lari, o'yin menyusi namunasi, eksport qilingan o'yin papkasi, hamda playerni avtomatik sinash
(ishlash testi va eksport testi). Loyiha sozlamalarining o'zi (oyna, splash, sifat, til ...) batafsil
[project-settings.md](project-settings.md) da, sahnalar oqimi [scenes.md](scenes.md) da.

## Mundarija

1. [Player nima va qanday ishga tushadi](#1-player-nima-va-qanday-ishga-tushadi)
2. [Ishga tushish ketma-ketligi](#2-ishga-tushish-ketma-ketligi)
3. [Sozlamalar va ustunlik tartibi](#3-sozlamalar-va-ustunlik-tartibi)
4. [Buyruq qatori](#4-buyruq-qatori)
5. [O'yin ichidagi tugmalar va oyna](#5-oyin-ichidagi-tugmalar-va-oyna)
6. [Log fayllari](#6-log-fayllari)
7. [Crash hisobotlari](#7-crash-hisobotlari)
8. [Versiya ma'lumoti](#8-versiya-malumoti)
9. [Skriptlar uchun: ekran va kursor API](#9-skriptlar-uchun-ekran-va-kursor-api)
10. [O'yin menyusi namunasi](#10-oyin-menyusi-namunasi)
11. [Eksport](#11-eksport)
12. [Ishlash testi (headless)](#12-ishlash-testi-headless)
13. [Eksport testi (avtomatik)](#13-eksport-testi-avtomatik)
14. [Avtomatlashtirish uchun muhit o'zgaruvchilari](#14-avtomatlashtirish-uchun-muhit-ozgaruvchilari)
15. [Muammolar va yechimlar](#15-muammolar-va-yechimlar)
16. [Testlar](#16-testlar)
17. [Qisqa API ma'lumotnomasi](#17-qisqa-api-malumotnomasi)

---

## 1. Player nima va qanday ishga tushadi

`ZaminPlayer` - o'yinning o'zi: editorsiz, ImGui'siz, faqat o'yin oynasi. U `.zproject` faylini ochadi, skript
modulini (`lib<O'yin>.so`) yuklaydi va boshlang'ich sahnani `SceneManager` orqali ishga tushiradi (qo'shimcha
sahnalar, `DontDestroyOnLoad`, fonda yuklash, o'tishlar - [scenes.md](scenes.md)).

```bash
# Ishlab chiqish paytida: loyiha fayli yoki papkasi
./build/bin/ZaminPlayer Projects/Platformer2D
./build/bin/ZaminPlayer Projects/Platformer2D/Platformer2D.zproject --scene Menu --windowed

# Eksport qilingan o'yin: ishga tushirish skripti (player yonidagi Game/*.zproject ni ochadi)
./Platformer2D.sh
./Platformer2D.sh --fullscreen --lang en
```

Loyiha ko'rsatilmasa player o'z yonidagi `Game/` papkasidagi (keyin o'z papkasidagi) birinchi `.zproject` ni
qidiradi - eksport qilingan o'yin shunday ishlaydi. Loyiha topilmasa xabar bilan chiqadi (chiqish kodi 1).

Boshlang'ich sahna: `--scene`, bo'lmasa loyihaning `StartScene`, u topilmasa build ro'yxatidagi birinchi yoqilgan
sahna. Birinchi sahna yuklanmasa oyna yopilmaydi: **xato ekrani** nima buzilganini ko'rsatadi (Esc - chiqish),
o'yin davomida boshqa sahnaga o'tib bo'lmasa yuqorida qizil polosa bir necha soniya turadi.

## 2. Ishga tushish ketma-ketligi

1. **Buyruq qatori** o'qiladi (`PlayerCommandLine::Parse`). `--help` yoki `--version` bo'lsa matn chiqariladi va
   player oynasiz tugaydi.
2. **Loyiha fayli** o'qiladi (oyna sozlamalari uchun) va o'yin saqlagan **ekran sozlamalari** (SaveData) - oyna
   yaratilishidan oldin, shuning uchun to'liq ekranda yopilgan o'yin darhol to'liq ekranda ochiladi.
3. **Log fayli** ochiladi (6-bo'lim) va **crash handler** o'rnatiladi (7-bo'lim).
4. Oyna har bir qiymat uchun eng kuchli manbadan yaratiladi (`PlayerLaunchSettings::Resolve`, 3-bo'lim); log'da har
   qiymat qayerdan kelgani yoziladi.
5. Loyiha ochiladi: ikonka, sifat darajasi, til, ovoz, gamma / yorqinlik, debug chizish, skript moduli.
6. **Splash ekran** (Zamin logotipi yoki o'yinning rasmi, ~2 s) - shu payt birinchi sahna fonda yuklanadi. Tugma
   bosilsa (sahna tayyor bo'lsa) o'tkazib yuboriladi. `--no-splash` yoki loyiha sozlamasi o'chiradi.
7. O'yin boshlanadi; birinchi kadrlari splash rangidan ochiladi.

Log boshida doim:

```
Zamin Engine 0.1.0 (kommit 40a7394f2c, build 2026-11-07, Release, GCC 13.3.0) ishga tushmoqda (Zamin Player)
Log fayli: /home/ali/.local/share/Zamin Studio/Zamin Platformer/logs/player-2026-11-07_14-03-12.log
Oyna sozlamalari: 1600x900 (saqlangan), oynali (loyiha), VSync: ha (loyiha), FPS: cheklovsiz (loyiha), monitor: asosiy (loyiha), splash: ha (loyiha)
Zamin Platformer 0.4.0 (Zamin Studio)
Skript moduli yuklandi: libPlatformer2D.so (25 ta skript klassi)
```

## 3. Sozlamalar va ustunlik tartibi

Har bir sozlama alohida, eng kuchli manbadan olinadi:

1. **buyruq qatori** (`--resolution`, `--fullscreen`, `--fps`, `--quality`, `--lang`, `--gamma` ... - 4-bo'lim);
2. **o'yinchi saqlagan tanlov** - o'yinning sozlamalar menyusi saqlagani (SaveData);
3. **loyiha sozlamalari** (Project settings > Player, Graphics, Quality, Localization).

| Sozlama | Saqlaydigan API | SaveData kalitlari |
|---|---|---|
| Ekran o'lchami | `Display::SetResolution` + `Display::SaveSettings` | `Zamin.Display.Width`, `.Height` |
| To'liq ekran, VSync | `Display::SetFullscreen` / `SetVSync` + `SaveSettings` (F11 ham saqlaydi) | `Zamin.Display.Fullscreen`, `.VSync` |
| Gamma, yorqinlik | `Display::SetGamma` / `SetBrightness` + `SaveSettings` | `Zamin.Display.Gamma`, `.Brightness` |
| Sifat darajasi | `Quality::SetLevel` + `Quality::SaveSettings` | `Zamin.Quality` |
| Ovoz guruhlari | `Audio::SetGroupVolume` + `Audio::SaveSettings` | `Zamin.Audio` |
| Til | `Localization::SetLanguage` + `Localization::SaveSettings` | `Zamin.Localization` |

SaveData fayli: `~/.local/share/<kompaniya>/<o'yin>/savedata.json` (kompaniya bo'sh bo'lsa
`~/.local/share/zamin/<o'yin>/`; `$XDG_DATA_HOME` hisobga olinadi). Uni o'chirish - hamma tanlovni unutish.
Ekran o'lchami faqat o'yin uni tanlaganda (`SetResolution`) saqlanadi: o'yinchi oynani sichqoncha bilan
cho'zgani keyingi safar o'lcham bo'lib qolmaydi. Sozlamalar menyusisiz o'yinlar avvalgidek ishlaydi - saqlangan
narsa yo'q, loyiha sozlamalari ishlatiladi.

## 4. Buyruq qatori

```
ZaminPlayer [O'yin.zproject | loyiha papkasi] [variantlar]
```

Qiymatli variantlar `--fps 60` yoki `--fps=60` ko'rinishida yoziladi. Tushunilmagan variant yoki noto'g'ri qiymat
log'ga ogohlantirish bo'lib yoziladi, qolganlari ishlatiladi. `ZaminPlayer --help` - hammasi o'zbekcha.

| Variant | Ma'nosi |
|---|---|
| `--windowed`, `--fullscreen`, `--borderless` | Oyna rejimi (oxirgisi yutadi) |
| `--resolution KxB`, `--width K`, `--height B` | Oyna o'lchami (`--fullscreen` bilan monitor video rejimi ham) |
| `--monitor N` | Qaysi monitorda ochiladi (0 - asosiy) |
| `--vsync`, `--no-vsync` | Vertikal sinxronizatsiya |
| `--fps N` | Kadr chastotasi cheklovi (0 - cheklovsiz) |
| `--jobs N` | Vazifalar tizimining ishchi oqimlari (standart: protsessor oqimlari - 1; 0 - o'chiq, hamma ish asosiy oqimda). Mustaqil particle tizimlari va animator pozalari parallel hisoblanadi |
| `--scene <yo'l / indeks / nom>` | Boshlang'ich sahna: `Scenes/Level2.zscene`, `2` (build indeksi) yoki `Level2` |
| `--quality <nom / indeks>` | Sifat darajasi (`Low`, `High` ...) |
| `--lang <kod>`, `--language <kod>` | Til (`uz`, `ru`, `en` ...) |
| `--no-splash` | Splash ekransiz |
| `--gamma G`, `--brightness B` | Tasvir gammasi (0.5 ... 2.0) va yorqinligi (0.5 ... 1.5) |
| `--gl 4.1/4.3/4.5/4.6`, `--msaa N`, `--gl-debug`, `--no-gl-debug`, `--srgb`, `--no-srgb` | OpenGL konteksti |
| `--no-debug-draw`, `--debug-raycasts`, `--debug-contacts`, `--debug-contact-details`, `--debug-joints`, `--debug-velocities` | Debug chizish (Shipping build'da yo'q) |
| `--log-file <fayl>`, `--no-log-file` | Log faqat shu faylga / log faylsiz |
| `--no-crash-handler`, `--no-crash-dialog` | Crash hisobotisiz (debugger, core dump) / hisobot bor, oyna yo'q |
| `--benchmark N` (+ `--benchmark-warmup`, `--benchmark-out`, `--benchmark-max-avg`, `--benchmark-max-p95`) | Ishlash testi (12-bo'lim) |
| `-v`, `--version` | O'yin va dvigatel versiyasi (8-bo'lim) |
| `-h`, `--help` | Yordam |

## 5. O'yin ichidagi tugmalar va oyna

| Tugma | Nima qiladi |
|---|---|
| **F11**, **Alt+Enter** | To'liq ekran <-> oyna (avvalgi o'lcham va joyiga qaytadi; tanlov saqlanadi). Input xaritasidagi `ToggleFullscreen` harakati ham. Loyihada o'chirsa bo'ladi (`FullscreenToggle`) |
| **F10** | FPS va audio profili log'ga |
| **Esc** | Xato ekranida - chiqish; o'yinlarda odatda pauza menyusi |

- **Oyna o'lchami o'zgarsa** kamera, UI masshtabi (canvas scaler) va render nishonlari har kadr yangi o'lchamga
  moslashadi. Loyihada **qat'iy ekran nisbati** yoqilgan bo'lsa o'yin tasviri shu nisbatda, chetlarida polosalar
  bilan (sichqoncha tasvir ichida hisoblanadi).
- **Kichraytirilgan oyna** - o'yin yangilanmaydi va chizilmaydi, CPU band emas; qaytganda vaqt sakramaydi.
- **Fonda ishlash o'chiq** (`RunInBackground: false`) bo'lsa oyna fokusni yo'qotganda o'yin to'xtaydi (vaqt,
  fizika, skriptlar, ovoz) va oxirgi kadr xira, "Pauza" yozuvi bilan ko'rsatiladi.
- **FPS cheklovi** (`TargetFrameRate` / `--fps`) VSync bilan birga ishlaydi: monitor chastotasini bo'ladigan
  cheklovni VSync o'zi ushlaydi, boshqalarini aniq jadvalli kutish (uyqu + qisqa aylanish).

Batafsil: [project-settings.md, 4-bo'lim](project-settings.md#4-player-oyna-nom-versiya-ikonka).

## 6. Log fayllari

Har ishga tushish o'z log faylini yozadi:

```
~/.local/share/<kompaniya>/<o'yin>/logs/player-<sana_vaqt>.log    (eng oxirgi 5 tasi saqlanadi)
~/.local/share/<kompaniya>/<o'yin>/logs/latest.log                 (doim oxirgi ishga tushish)
```

Fayl boshida o'yin nomi va versiyasi, dvigatel build'i (kommit, sana), boshlanish vaqti, loyiha va buyruq qatori
yoziladi; fayl ochilishidan oldingi xabarlar ham kiradi. Har satr darhol diskka yoziladi - crash bo'lsa ham log
to'liq qoladi. Satr ko'rinishi: `[14:03:12] ERROR APP: ...` (darajalar: `TRACE`, `INFO`, `WARN`, `ERROR`,
`CRITICAL`; `ZAMIN` - dvigatel, `APP` - o'yin skriptlari). `--log-file <yo'l>` faqat shu faylga yozadi,
`--no-log-file` log faylsiz (faqat terminal).

## 7. Crash hisobotlari

O'yin qulasa (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS yoki ushlanmagan C++ istisnosi) log papkasiga
`crash-<sana_vaqt>.txt` yoziladi:

```
=== Zamin crash hisoboti ===
Dastur: Zamin Player
O'yin: Zamin Platformer 0.4.0
Dvigatel: Zamin Engine 0.1.0 (kommit 40a7394f2c, build 2026-11-07, Release, GCC 13.3.0)
Sabab: SIGSEGV (segmentation fault) - manzil 0x8
Backtrace: ...
Oxirgi log satrlari: ...
Yuklangan modullar: ...
```

Keyin alohida jarayonda xabar oynasi ochiladi: `zenity`, `kdialog`, bo'lmasa playerning o'z oynasi
(`ZaminPlayer --crash-dialog <hisobot>`: sabab, hisobot yo'li, "Papkani ochish", "Yo'lni nusxalash", "Yopish"),
oxiri `xmessage`. O'yinchidan shu faylni yuborishni so'rang - unda kommit bor, xato aynan qaysi koddan ekanini
bilasiz. Signal handler faqat xavfsiz (async-signal-safe) chaqiruvlardan foydalanadi; dastur odatdagi signal
chiqish kodi bilan tugaydi. `--no-crash-handler` - debugger / core dump uchun, `--no-crash-dialog` - oynasiz.
Avtomatik ishga tushishlarda (`ZAMIN_MAX_FRAMES`, `--benchmark`) oyna ko'rsatilmaydi.

## 8. Versiya ma'lumoti

```
$ ./Platformer2D.sh --version
Zamin Platformer 0.4.0 - Zamin Studio
Zamin Engine 0.1.0
  Kommit:  40a7394f2c (2026-11-07)
  Build:   2026-11-07, Release, GCC 13.3.0
```

- **O'yin versiyasi** - Project settings > Player > Versiya (`1.4.2`, semver), nomi - Mahsulot nomi.
- **Dvigatel build'i** (`BuildInfo`) build paytida yoziladi (`cmake/GenerateBuildInfo.cmake`): git kommiti (10 ta
  belgi), kommitdagi o'zgarishlar (`-dirty`, "[o'zgarishlar bilan]"), kommit sanasi, build sanasi (UTC, kun;
  `SOURCE_DATE_EPOCH` bo'lsa o'sha), build turi va kompilyator. Git'siz build'da kommit "noma'lum". Fayl faqat
  biror narsa o'zgarganda qayta yoziladi - dvigatel har build'da qayta kompilyatsiya qilinmaydi.
- Qayerda ko'rinadi: `--version`, har log faylining boshi, crash hisoboti, editorning Yordam > "Zamin haqida"
  oynasi ("Versiyani nusxalash" tugmasi bilan), Platformer2D sozlamalar menyusining burchagi va pauza menyusi
  burchagi (`VersionInfo::FormatShort()`: `Zamin Platformer v0.4.0  |  Zamin Engine 0.1.0 (40a7394)`).

Skriptdan:

```cpp
VersionInfo v = VersionInfo::ForActiveProject();
SetText(VersionLabel, v.FormatShort());                    // menyu burchagi uchun bir qator
ZM_INFO("{}", BuildInfo::Get().Describe());                // "Zamin Engine 0.1.0 (kommit ..., build ...)"
Application::GetVersion(); Application::GetProductName();  // o'yin versiyasi va nomi
```

## 9. Skriptlar uchun: ekran va kursor API

### Ekran (`Display`)

Player o'z oynasini `Display` ga bog'laydi; o'yinning sozlamalar menyusi shu orqali oynani o'zgartiradi. Editorning
Play rejimida oyna bog'lanmagan: qiymatlar faqat saqlanadi (editor oynasi o'zgarmaydi), gamma / yorqinlik esa
o'yin ko'rinishida ishlaydi. Har Play player kabi boshlanadi (`Display::ResetOptions`: loyihaning oyna rejimi va
VSync'i, tanlangan o'lcham yo'q, gamma 1; keyin saqlanganlar yuklanadi) va Stop'da shu holatga qaytadi - bir
yurgizishda o'zgartirilgan narsa keyingisiga o'tmaydi. Play'dagi o'lchamlar ro'yxati editor monitoriniki (ekrandan
katta o'lcham taklif qilinmaydi). Player oynasi ochilganda saqlangan / so'ralgan oyna o'lchami monitordan katta
bo'lsa, nisbati saqlangan holda monitorga sig'diriladi.

```cpp
// Ekran o'lchami: monitorning video rejimlari (oynasiz - editor monitori yoki 1280x720 ... 2560x1440), joriy
// o'lcham doim ro'yxatda.
std::vector<ScreenResolution> sizes = Display::GetResolutions();
Display::SetResolution(1920, 1080);     // oynali rejimda oyna o'lchami, eksklyuziv to'liq ekranda video rejim
ScreenResolution r = Display::GetResolution();

Display::SetFullscreen(true);           // loyihaning to'liq ekran turi (eksklyuziv yoki chegarasiz)
Display::SetVSync(false);
Display::SetGamma(1.2f);                // 0.5 ... 2.0
Display::SetBrightness(0.9f);           // 0.5 ... 1.5
Display::SaveSettings();                // keyingi ishga tushishda shular bilan ochiladi
// "Standart" tugmasi: keyingi ishga tushish qanday ochilsa (loyihaning oyna rejimi, VSync), saqlanganlar o'chadi.
Display::ResetOptions(Project::GetPlayerSettings());
Display::ClearSavedSettings();
```

### Kursor (`Cursor`)

```cpp
Cursor::SetLockMode(CursorLockMode::Locked);    // yashirin, joyida: Input::GetMouseDelta() bilan sichqoncha-qarash
Cursor::SetLockMode(CursorLockMode::Confined);  // oyna ichida qoladi
Cursor::SetVisible(false);
Cursor::SetImage("Textures/crosshair.png", { 16, 16 });   // o'z rasmi (hotspot); Cursor::ClearImage()
Cursor::SetShape(CursorShape::Hand);                      // tizim shakllari
Cursor::SetReleaseWhenPaused(true);                       // pauzada bo'sh va ko'rinadigan (standart)
Cursor::Reset();
```

Istaklar har kadr oynaga qo'llanadi; oyna fokusni yo'qotsa, o'yin pauzada bo'lsa (`Time::SetPaused`) yoki Play
to'xtasa tizim kursori qaytadi. Editorda o'yin ko'rinishi fokusda bo'lganda ishlaydi, **Esc** kursorni editorga
qaytaradi.

## 10. O'yin menyusi namunasi

Platformer2D'da qayta ishlatiladigan menyu bor: **`Prefabs/GameMenu.zprefab`** va uning skripti
**`GameMenu`** (`Projects/Platformer2D/Source/GameMenu.cpp`). `Menu.zscene` (bosh menyu) shu prefabning nusxasi.

- **Asosiy panel**: o'yin nomi, "O'ynash" (`PlayScene` ga so'nib o'tadi), "Sozlamalar", "Chiqish"; ekran burchagida
  o'yin versiyasi.
- **Sozlamalar paneli**: ekran o'lchami, to'liq ekran, VSync, grafika sifati, gamma, ovoz (umumiy, musiqa,
  effektlar), til; "Orqaga" (saqlaydi), "Standart" (saqlanganlarni o'chiradi). Panel burchagida o'yin va dvigatel
  versiyasi.
- **Boshqaruv**: strelkalar / D-pad - tanlash, Enter / A - bosish, Esc / B - orqaga. O'lcham, sifat va til
  tugmalari bosilganda keyingisiga o'tadi, tanlangan bo'lsa chap / o'ng oldingi / keyingisini tanlaydi.
- **Saqlash**: "Orqaga" yoki menyu yopilganda `Display` / `Audio` / `Quality::SaveSettings`, til tanlanganda
  `Localization::SaveSettings` - player keyingi safar shu sozlamalar bilan ochiladi (3-bo'lim).

**O'z o'yiningizga qo'shish**: prefabni Kontent panelidan sahnaga sudrang (yoki skriptdan
`Instantiate("Prefabs/GameMenu.zprefab", glm::vec3(0.0f))`), inspektorda `GameMenu` maydonlarini sozlang:

| Maydon | Ma'nosi |
|---|---|
| `PauseMenu` | O'chiq: bosh menyu (doim ko'rinadi). Yoqiq: pauza menyusi - Esc / Start o'yinni to'xtatib ochadi, "O'ynash" o'rnida "Davom etish" |
| `PlayScene` | "O'ynash" ochadigan sahna (yo'l yoki build sahna nomi) |
| `FadeOut`, `FadeIn` | O'tishdagi qorayish / ochilish (sekund) |
| Panellar, tugmalar, slayderlar | Prefab ichidagi obyektlar (o'zgartirmasangiz bo'ladi) |

Menyu matnlari lokalizatsiya kalitlari bilan (`menu.play`, `menu.main_title`, `menu.nav_hint`, `pause.*`,
`settings.*` - jumladan `settings.resolution` = `"Ekran o'lchami: {0}"`, `settings.quality` =
`"Grafika sifati: {0}"`); boshqa o'yinga ko'chirganda shu kalitlarni til fayllaringizga qo'shing
(`Projects/Platformer2D/Assets/Localization/*.json` dan nusxa oling). 1-3 darajalardagi pauza menyusi
(`PauseMenu`, M12) ham saqlanib qolgan - ular ovoz, ekran, gamma, til sozlamalari va burchakda versiyani ko'rsatadi.

## 11. Eksport

Editorda **Fayl > O'yinni build qilish...** o'yinni papkaga eksport qiladi:

```
<papka>/
  Platformer2D.sh              ishga tushirish skripti (o'z papkasiga o'tadi, ZaminPlayer Game/Platformer2D.zproject)
  ZaminPlayer, libZamin.so     player va dvigatel (strip qilingan)
  Resources/                   dvigatel resurslari (shriftlar)
  Game/Platformer2D.zproject   loyiha fayli
  Game/Assets/                 faqat ishlatiladigan assetlar (+ .zmeta, sahnalarning binar keshi .zsceneb)
  Game/AssetRegistry.zreg      asset registri (faqat eksport qilinganlar)
  Game/Binaries/libPlatformer2D.so  skript moduli (Release, -O2, strip)
  Platformer2D.desktop         ko'chiriladigan menyu yozuvi (o'z yonidagi skriptni ishga tushiradi)
  Platformer2D.png, icons/hicolor/<o'lcham>/apps/zamin-platformer2d.png   ikonka (16 ... 256 px)
  install-desktop-entry.sh     o'yinni ilovalar menyusiga o'rnatadi (--uninstall - olib tashlaydi)
  export-report.json, export-report.txt   eksport hisoboti
  excluded-assets.txt          chiqarib tashlangan assetlar (bo'lsa)
```

- **Ishlatilmagan assetlar chiqarib tashlanadi**: build sahnalari va ular orqali kerak bo'lgan hamma narsa
  (prefablar, materiallar, modellar, kliplar), loyiha fayli va C++ kodidagi `"..."` satrlarida tilga olingan fayllar,
  `Resources/`, til fayllari va "Doim kiritish" ro'yxati. Batafsil: [project-settings.md, 12-bo'lim](project-settings.md#12-build-sahnalar).
- **Skript moduli Release'da** alohida build papkasida (`<loyiha>/Build/ExportRelease`) qayta kompilyatsiya
  qilinadi; binarlar `strip` qilinadi (~218 MB -> ~37 MB). Ikkalasini o'chirsa bo'ladi.
- **Eksport o'zini tekshiradi** (`ExportFiles::CheckExportFolder`): ishga tushirish skripti (bajarish huquqi bilan),
  player, dvigatel, `Resources/`, loyiha fayli, uning boshlang'ich va build sahnalari, asset registri, skript moduli,
  `.desktop` va o'rnatish skripti joyida bo'lishi kerak - yo'qlari hisobotga xato bo'lib yoziladi. Skript moduli
  topilmasa (kompilyatsiya qilinmagan) o'yin skriptlarsiz eksport qilinadi - bu xato emas, ogohlantirish.
- **Hisobot oynasi** eksport tugaganda ochiladi: jami hajm, turlar bo'yicha hajm, eng katta fayllar, chiqarib
  tashlangan assetlar, ogohlantirish va xatolar (Fayl > "Eksport hisoboti" - qayta ochish).
- Eksport qilingan papkani istalgan joyga ko'chirish, arxivlash mumkin: skript va `.desktop` o'z joyidan ishlaydi.
  `.desktop` ikonkani faqat to'liq yo'l bilan ko'rsata oladi (standart nisbiy yo'lni qabul qilmaydi): o'yin yangi
  joydan birinchi marta ishga tushganda launcher (`<O'yin>.sh`) yonidagi `.desktop`ning `Icon=` qatorini shu papkaga
  to'g'rilaydi. Ilovalar menyusiga qo'shish uchun yangi joyda `./install-desktop-entry.sh` ni ishga tushiring.

## 12. Ishlash testi (headless)

`--benchmark N` - o'yin o'rniga uning boshlang'ich sahnasini (yoki `--scene`) **N kadr o'lchash**: splash, FPS
cheklovi va VSync o'chiriladi (buyruq qatorida `--vsync` / `--fps` berilmasa), fonda ham ishlaydi; avval
`--benchmark-warmup` (standart 60) kadr isitiladi (shaderlar, keshlar, sahnaning birinchi kadrlari), keyin har kadr
vaqti yoziladi. Natija log'da va `--benchmark-out` JSON faylida, chiqish kodi: **0** - o'tdi, **3** - chegaradan
oshdi (`--benchmark-max-avg MS`, `--benchmark-max-p95 MS`), **2** - ishlamadi (sahna yuklanmadi, player N kadr
o'lchanmasdan yopildi - oyna yopildi, o'yin `QuitGame` qildi, `ZAMIN_MAX_FRAMES` - yoki `--benchmark-out` fayli
yozilmadi). Test oxirigacha bormasa ham natija (o'lchangan kadrlar bilan, "o'tmadi") log'ga va faylga yoziladi.

```bash
xvfb-run -a ./build/bin/ZaminPlayer Projects/Showcase3D --benchmark 300 --benchmark-out bench.json --resolution 1280x720
```

```
Ishlash testi: Zamin Showcase 3D 1.2.0 - Scenes/Showcase.zscene (1280x720, llvmpipe (LLVM 20.1.2, 256 bits))
  300 kadr (60 ta isitish kadridan keyin), 21.70 s; yuklanish 0.10 s
  Kadr:      o'rtacha 72.35 ms (13.8 FPS), mediana 70.45, p95 87.21, p99 92.70, min 67.13, max 92.83 ms
  Yangilash: o'rtacha 65.82 ms (15.2 FPS), ... (o'yin yangilanishi va chizish chaqiruvlari, swap'siz)
```

- **Kadr** - bir kadr boshidan keyingisining boshigacha (yangilash, chizish, swap); **Yangilash** - playerning o'z
  yangilash va chizish chaqiruvlari (swap'siz). Persentillar "eng yaqin rang" bo'yicha: p95 - kadrlarning 95% i
  undan sekin bo'lmagan eng kichik vaqt (`FrameTimeStats`).
- JSON: `Game`, `GameVersion`, `Scene`, `Engine` (build), `Renderer` (GPU), `Width`, `Height`, `Warmup`, `Seconds`,
  `LoadSeconds`, `Frame` / `Update` (`Count`, `Average`, `Median`, `P95`, `P99`, `Min`, `Max`, `StdDev`,
  `AverageFPS`, `Low1PercentFPS`), `Limits`, `Passed`, `Failures`, `Frames` (har bir kadr, ms).
- **Namuna o'yinlar uchun skript**: `Tests/player_bench.sh` (yoki `cmake --build build --target player_bench`) uchala
  namunani displeysiz (xvfb) o'lchab jadval chiqaradi, hisobotlar `build/PlayerBench/` da:

```
Loyiha          o'rt.ms   med.ms   p95 ms   p99 ms      FPS  natija
Platformer2D      8.962    8.925    9.383    9.492  111.582  o'tdi
Showcase3D       79.034   77.437    89.79  104.076   12.653  o'tdi
Adventure3D     107.708  107.109  112.551   123.94    9.284  o'tdi
```

  Variantlar: `--frames N`, `--warmup N`, `--project <papka>` (bir necha marta), `--scene`, `--resolution`,
  `--max-avg MS`, `--max-p95 MS`, `--out <papka>`; qolganlari playerga (`--quality Low` ...). Har ishga tushish toza
  HOME bilan (saqlangan sozlamalar ta'sir qilmaydi). Xvfb'da GPU yo'q (llvmpipe - protsessorda chizish): raqamlarni
  bir mashinadagi o'zgarishlarni solishtirish uchun ishlating, haqiqiy GPU'dagi FPS bilan emas.

## 13. Eksport testi (avtomatik)

`Tests/export_test.sh` (yoki `cmake --build build --target export_test`) eksportni boshidan oxirigacha tekshiradi:

1. editor Platformer2D'ni vaqtinchalik papkaga eksport qiladi (`ZAMIN_EDITOR_EXPORT`, xvfb) - chiqish kodi 0,
   `export-report.json` da `"Succeeded": true`, eksportning o'z tekshiruvi xatosiz;
2. `<O'yin>.sh --version` o'yin nomi va dvigatel versiyasini aytadi;
3. eksport qilingan o'yin ishga tushirish skripti orqali boshlang'ich sahnasini `--benchmark` bilan ishlatadi -
   chiqish kodi 0, logida `ERROR` / `CRITICAL` yo'q, skript moduli yuklangan, natija fayli bor;
4. papka boshqa joyga ko'chirilgandan keyin ham ishga tushadi.

```
Eksport testi: Platformer2D (Zamin Platformer) -> /tmp/zamin-export-test.XXXX/export
  [OK]   editor eksportni tugatdi (chiqish kodi 0)
  [OK]   export-report.json: muvaffaqiyatli (34 qatorli hisobot)
  [OK]   --version: Zamin Platformer 0.4.0 - Zamin Studio Zamin Engine 0.1.0
  [OK]   eksport qilingan o'yin 120 kadr ishladi (chiqish kodi 0)
  [OK]   o'yin logida xato yo'q (30 qator)
  [OK]   Skript moduli yuklandi: libPlatformer2D.so (25 ta skript klassi)
  [OK]   boshqa papkaga ko'chirilgandan keyin ham ishga tushadi
Eksport testi: hammasi o'tdi
```

Variantlar: `--project Projects/Adventure3D`, `--frames N`, `--keep` (vaqtinchalik papka qoladi), `--work <papka>`.
Chiqish kodi: 0 - hammasi o'tdi, 1 - xato bor (papka saqlanadi), 2 - editor / player topilmadi. Loyihaning o'zi
o'zgarmaydi (Release modul `<loyiha>/Build/ExportRelease` da, editor ham shunday qiladi).

## 14. Avtomatlashtirish uchun muhit o'zgaruvchilari

| O'zgaruvchi | Kim | Ma'nosi |
|---|---|---|
| `ZAMIN_MAX_FRAMES=N` | hammasi | N kadrdan keyin yopiladi; FPS cheklanmaydi, splash va crash oynasi yo'q |
| `ZAMIN_SCREENSHOT=fayl.png` | hammasi | Oxirgi kadr rasmi |
| `ZAMIN_PLAYER_INPUT="<kadr>:<amal>;..."` | player | Tugmalar (`Down`, `Enter` ...), `mouse=x,y`, `click`, `down` / `up`, `scroll=dy` |
| `ZAMIN_PLAYER_STATS=1` | player | Chiqishda render statistikasi, kadr vaqtlari, profiler, audio, sahna yuklash vaqtlari |
| `ZAMIN_PLAYER_SPLASH=1` | player | Avtomatik ishga tushishda ham splash |
| `ZAMIN_FOCUS_LOSS_TEST=<kadr>[:<soni>]` | hammasi | Fokus yo'qolishini taqlid qiladi (fonda pauza) |
| `ZAMIN_JOBS=N` | hammasi | Vazifalar tizimining ishchi oqimlari (`--jobs`dan ustun; 0 yoki `off` - o'chiq) |
| `ZAMIN_EDITOR_STATS=memory` / `jobs` | editor | Statistika panelida Xotira / Vazifalar tizimi bo'limlari ochiq; chiqishda xotira va vazifalar log'ga |
| `ZAMIN_PLAYER_CRASH_TEST=<kadr>:<segv / abort / fpe / exception>` | player | Ataylab crash |
| `ZAMIN_CRASH_DIALOG=1` | player | Avtomatik ishga tushishda ham crash oynasi |
| `ZAMIN_PLAYER_NO_CULLING=1`, `ZAMIN_PLAYER_NO_INSTANCING=1` | player | Frustum culling / instancing o'chiq (solishtirish) |
| `ZAMIN_EDITOR_EXPORT=<papka>` | editor | 5-kadrda o'yinni eksport qiladi va tugashini kutadi |
| `ZAMIN_EDITOR_EXPORT_REPORT=<papka>` | editor | Avvalgi eksport hisobotini oynada ochadi |
| `ZAMIN_EDITOR_ABOUT=1` | editor | "Zamin haqida" oynasi (versiya, kommit, build) |

## 15. Muammolar va yechimlar

| Muammo | Yechim |
|---|---|
| O'yin juda kichik / katta oynada ochiladi | Saqlangan o'lcham: sozlamalar menyusidan tanlang, `--resolution` bering yoki `savedata.json` dagi `Zamin.Display.*` ni o'chiring |
| Qora ekran yoki "Sahna yuklanmadi" | Xato ekrani va log nima buzilganini aytadi; `--scene` bilan boshqa sahnani sinab ko'ring |
| Skriptlar ishlamaydi | Log'da "Skript moduli yuklandi" bormi? Modul eksportda bo'lmasa eksport tekshiruvi xato beradi; loyihaning `ScriptModulePath` ini tekshiring |
| O'yin qulaydi | `crash-*.txt` (log papkasida): sabab, backtrace, kommit; debugger uchun `--no-crash-handler` |
| Kadrlar notekis | `--benchmark` yoki `ZAMIN_PLAYER_STATS=1`; VSync va `--fps` birga (5-bo'lim) |
| Ilovalar menyusida ikonka yo'q / eski joyni ko'rsatadi | Papkani ko'chirgandan keyin `./install-desktop-entry.sh` (papkadagi `.desktop` ikonkasini o'yinning birinchi ishga tushishi o'zi to'g'rilaydi) |
| Qaysi versiya ishlayapti? | `./O'yin.sh --version`, log faylining boshi yoki sozlamalar menyusining burchagi |

## 16. Testlar

`./build/bin/ZaminTests` (headless) M18 bo'yicha:

| Test | Nimani |
|---|---|
| `PlayerCommandLineParser`, `PlayerLaunchSettingsPrecedence` | Buyruq qatori, `--help`, ustunlik tartibi |
| `PlayerSettingsFullscreenToggleAndSplash`, `SplashTimelineAndLogo`, `FramePacingPlanAndPacer` | F11, splash, FPS cheklovi |
| `PlayerLogFileRotation`, `CrashHandlerReports`, `CursorApiForScripts`, `PlayerViewportFixedAspect` | Log, crash, kursor, ekran nisbati |
| `ExportExcludesUnusedAssets`, `ExportReleaseModuleAndStrip`, `ExportDesktopEntryRelocatableAndIcons`, `ExportReportSizesAndFiles` | Eksport |
| `PauseInBackgroundAudioOutput`, `DisplayGammaAndBrightness` | Fonda pauza, gamma / yorqinlik |
| `PlayerBenchmarkStatsAndLimits` | Persentillar, isitish, chegaralar, JSON, `--benchmark` variantlari |
| `ExportFolderSelfCheck` | Eksport papkasining o'z tekshiruvi |
| `BuildInfoAndVersionText` | `BuildInfo`, `VersionInfo` matnlari, `--version` |
| `DisplayResolutionSettings` | O'lchamlar ro'yxati, saqlash, ustunlik (buyruq qatori > saqlangan > loyiha) |
| `GameMenuSampleScene` | Platformer2D bosh menyusi va prefab: navigatsiya, o'lcham / sifat / gamma / ovoz / til, saqlash, "O'ynash", pauza rejimi |

Displey kerak bo'lganlari (ctest'da emas): `Tests/render_tests.sh` (rendering regressiyasi),
`Tests/player_bench.sh` (ishlash testi), `Tests/export_test.sh` (eksport testi).

## 17. Qisqa API ma'lumotnomasi

```cpp
// Versiya
BuildInfo::Get();                       // EngineVersion, Commit, Dirty, CommitDate, BuildDate, BuildType, Compiler, Shipping
BuildInfo::Get().Describe();            // "Zamin Engine 0.1.0 (kommit ..., build ..., Release, GCC ...)"
VersionInfo::ForActiveProject().Format();       // --version matni
VersionInfo::ForActiveProject().FormatShort();  // menyu burchagi uchun bir qator
Application::GetVersion(); Application::GetProductName(); Application::GetCompanyName(); Application::GetEngineVersion();

// Ekran
Display::GetResolutions(); Display::SetResolution(w, h); Display::GetResolution();
Display::SetFullscreen(bool); Display::SetVSync(bool); Display::SetGamma(f); Display::SetBrightness(f);
Display::SaveSettings(); Display::LoadSettings(); Display::GetSavedSettings(); Display::ClearSavedSettings();

// Kursor
Cursor::SetVisible(bool); Cursor::SetLockMode(CursorLockMode); Cursor::SetImage(path, hotspot); Cursor::ClearImage();
Cursor::SetShape(CursorShape); Cursor::SetReleaseWhenPaused(bool); Cursor::Reset();

// Player (dvigatel tomoni)
PlayerCommandLine::Parse(argc, argv); PlayerCommandLine::GetHelpText();
PlayerLaunchSettings::Resolve(config, Display::GetSavedSettings(), commandLine);
PlayerBenchmark bench(settings); bench.AddFrame(frameMs, updateMs); PlayerBenchmark::WriteJson(bench.GetResult(), path);
FrameTimeStats::Compute(ms); FrameTimeStats::Percentile(sorted, 95.0);
Application::Get().SetExitCode(code);

// Log va crash
LogFile::OpenRotating(...); LogFile::GetPath(); CrashHandler::Install(settings);

// Eksport
ExportFiles::CheckExportFolder(papka, "O'yin"); ExportReport::Read(papka, report); ExportAssets::PlanForActiveProject();
```
