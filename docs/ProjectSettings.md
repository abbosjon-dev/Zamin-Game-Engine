# Zamin Engine: loyiha sozlamalari

Bu hujjat o'yin loyihasining sozlamalarini tushuntiradi: `.zproject` fayli va editordagi **Loyiha sozlamalari**
oynasi, player oynasi va ikonka, kompaniya / mahsulot nomi va versiya, sifat darajalari, grafik kontekst, vaqt
(qat'iy qadam, vaqt masshtabi va `Time::` skript API), standart gravitatsiya, audio standartlari, input,
teglar va qatlamlar, build sahnalar, lokalizatsiya (til fayllari va `Localize("key")`), debug chizish va
saqlangan ma'lumotlar. Oxirida editorning o'z tili (o'zbek / ingliz), fayl formati va player buyruq qatori
bor. Skript misollari C++ (`#include <Zamin/Zamin.h>`, `using namespace Zamin;`).

Mundarija:

1. [Asosiy tushunchalar](#1-asosiy-tushunchalar)
2. [Loyiha sozlamalari oynasi](#2-loyiha-sozlamalari-oynasi)
3. [Umumiy](#3-umumiy)
4. [Player: oyna, nom, versiya, ikonka](#4-player-oyna-nom-versiya-ikonka)
5. [Sifat darajalari](#5-sifat-darajalari)
6. [Grafika: OpenGL konteksti](#6-grafika-opengl-konteksti)
7. [Vaqt va Time API](#7-vaqt-va-time-api)
8. [Fizika: gravitatsiya va to'qnashuv qatlamlari](#8-fizika-gravitatsiya-va-toqnashuv-qatlamlari)
9. [Audio standartlari](#9-audio-standartlari)
10. [Input](#10-input)
11. [Teglar va qatlamlar](#11-teglar-va-qatlamlar)
12. [Build sahnalar](#12-build-sahnalar)
13. [Lokalizatsiya](#13-lokalizatsiya)
14. [Debug chizish](#14-debug-chizish)
15. [Saqlangan ma'lumotlar (SaveData)](#15-saqlangan-malumotlar-savedata)
16. [Editor tili (o'zbek / ingliz)](#16-editor-tili-ozbek--ingliz)
17. [.zproject fayli: format, versiya, migratsiya](#17-zproject-fayli-format-versiya-migratsiya)
18. [Player buyruq qatori](#18-player-buyruq-qatori)
19. [Testlar](#19-testlar)
20. [Qisqa API ma'lumotnomasi](#20-qisqa-api-malumotnomasi)

---

## 1. Asosiy tushunchalar

Har bir o'yin - bu **loyiha**: `<Nom>.zproject` fayli, `Assets/` (sahnalar, teksturalar, ovozlar, til fayllari),
`Source/` (C++ skriptlar) va skript moduli (`Binaries/lib<Nom>.so`). Loyiha sozlamalari `.zproject` faylida
saqlanadi va **hamma sahnalar uchun umumiy**: oyna, sifat, vaqt, gravitatsiya, ovoz, tillar ...

| Daraja | Qayerda | Misol |
|---|---|---|
| **Loyiha** | `.zproject`, *Oyna > Loyiha sozlamalari* | O'yin oynasi 1280x720, 60 Hz fizika, standart til `uz` |
| **Sahna** | `.zscene`, *Sahna sozlamalari* | Shu sahnaning osmoni, soyalari, o'z gravitatsiyasi |
| **O'yinchi tanlovi** | `SaveData` (o'yinlar orasida qoladi) | O'yinchi menyuda tanlagan sifat, til, to'liq ekran |
| **Buyruq qatori** | `ZaminPlayer --...` | `--quality Low`, `--lang ru`, `--windowed` |

Ustunlik tartibi (pastdagisi ustun): loyiha sozlamasi -> o'yinchi saqlagan tanlov -> buyruq qatori. Sahna o'z
qiymatini bermagan joyda (masalan gravitatsiya, fizika qadami "Loyihadan") loyihaniki ishlatiladi.

Loyiha ochilganda (`Project::Load`) sozlamalar darhol qo'llanadi: audio mikser (`AudioEngine::SetMixerSettings`),
sifat darajalari (`Quality::SetSettings`), til fayllari (`Project::ApplyLocalization`). Player oynasi va grafik
kontekst esa `ZaminPlayer` oynani yaratganda qo'llanadi. Sozlamasi yo'q (eski) loyihalar dvigatelning standart
qiymatlarini oladi - ular o'zini avvalgidek tutadi.

---

## 2. Loyiha sozlamalari oynasi

*Oyna > Loyiha sozlamalari* (yoki *Fayl > Loyiha sozlamalari...*). Oyna bo'limlarga ajratilgan:

| Bo'lim | Nima bor |
|---|---|
| **Umumiy** | Loyiha nomi, papkalar, skript moduli, boshlang'ich sahna |
| **Player** | Kompaniya, mahsulot nomi, versiya, oyna, FPS cheklovi, ikonka |
| **Sifat** | Sifat darajalari (Low ... Ultra) va standart daraja |
| **Grafika** | OpenGL versiyasi, debug konteksti, MSAA, sRGB; ishlayotgan kontekst ma'lumoti |
| **Vaqt** | Qat'iy qadam, kadrdagi qadamlar, boshlang'ich vaqt masshtabi, eng uzun kadr |
| **Fizika** | Standart gravitatsiya (2D / 3D), to'qnashuv qatlamlari va matritsasi, presetlar |
| **Audio** | Master ovozi, mikser guruhlari, ovoz dvigateli, 3D ovoz standartlari |
| **Input** | Harakatlar, o'qlar, 2D vektorlar |
| **Teglar va qatlamlar** | Gameplay teglari, 2D saralash qatlamlari |
| **Build sahnalar** | O'yin quriladigan sahnalar ro'yxati va build indekslari |
| **Lokalizatsiya** | Standart til, taklif qilinadigan tillar, til fayllari papkasi, tizim tili |
| **Debug** | O'yindagi debug chizish |
| **Saqlangan ma'lumotlar** | `SaveData` qiymatlarini ko'rish va tahrirlash (loyiha faylida emas) |

- **Qidiruv** (chap yuqorida): "soya", "VSync", "gravitatsiya" ... - mos bo'limlar qoladi, topilgan yorliqlar
  ajratib ko'rsatiladi.
- O'zgargan bo'lim nomi yonida `*` turadi. **Saqlash** `.zproject`ni yozadi, **Bekor qilish** hamma saqlanmagan
  o'zgarishlarni qaytaradi; bo'limning o'zini ham standartga qaytarish mumkin.
- Ko'p o'zgarishlar darhol ko'rinadi (sifat darajasi, audio, til), oyna va grafik kontekst esa player keyingi
  safar ishga tushganda qo'llanadi.

---

## 3. Umumiy

| Maydon | `.zproject` kaliti | Ma'nosi |
|---|---|---|
| Nomi | `Project.Name` | Loyiha nomi (mahsulot nomi bo'sh bo'lsa o'yin nomi ham shu) |
| Assets papkasi | `Project.AssetDirectory` | Assetlar papkasi (standart `Assets`) |
| Skriptlar papkasi | `Project.SourceDirectory` | C++ skriptlar (standart `Source`) |
| Skript moduli | `Project.ScriptModulePath` | Kompilyatsiya qilingan modul (`Binaries/lib<Nom>.so`) |
| Boshlang'ich sahna | `Project.StartScene`, `StartSceneUUID` | O'yin shu sahnadan boshlanadi |

Boshlang'ich sahna UUID bilan ham saqlanadi: fayl editor yopiq paytda ko'chirilsa yoki qayta nomlansa ham
topiladi. Sahna yo'q bo'lsa o'yin birinchi yoqilgan build sahnadan boshlanadi (12-bo'lim).

---

## 4. Player: oyna, nom, versiya, ikonka

`.zproject`ning `"Player"` bo'limi. `ZaminPlayer` va eksport qilingan o'yin oynani shu sozlamalar bilan yaratadi.

### Nom va versiya

| Maydon | Kalit | Ma'nosi |
|---|---|---|
| Kompaniya | `Company` | Studiya / muallif. SaveData papkasi: `~/.local/share/<kompaniya>/<mahsulot>` |
| Mahsulot nomi | `Product` | O'yinchilar ko'radigan nom (oyna sarlavhasi, SaveData papkasi). Bo'sh: loyiha nomi |
| Versiya | `Version` | Semantik versiya: `1.4.2`, `2.0.0-beta.1` (standart `1.0.0`) |

Kompaniya bo'sh bo'lsa SaveData `~/.local/share/zamin/<mahsulot>` papkasida turadi. Skriptlardan:

```cpp
ZM_INFO("{} {} ({})", Application::GetProductName(), Application::GetVersion(), Application::GetCompanyName());
```

### Oyna

| Maydon | Kalit | Ma'nosi |
|---|---|---|
| Oyna o'lchami | `Width`, `Height` | Oynali rejimdagi o'lcham (160 ... 7680). To'liq ekran monitornikini oladi |
| Oyna rejimi | `WindowMode` | `Windowed` (oynali), `Fullscreen` (to'liq ekran), `Borderless` (chegarasiz, ekran o'lchamida) |
| O'lchamini o'zgartirish | `Resizable` | O'yinchi oynani cho'za oladimi |
| VSync | `VSync` | Kadrlar monitor chastotasiga moslanadi |
| Oyna sarlavhasi | `Title` | Bo'sh: mahsulot nomi |
| Sarlavhada versiya | `VersionInTitle` | `"Mening o'yinim 1.4.2"` |
| FPS cheklovi | `TargetFrameRate` | Soniyadagi eng ko'p kadr (10 ... 1000); `0` - cheklovsiz (VSync boshqaradi) |
| Fonda ishlash | `RunInBackground` | `false`: oyna fokusni yo'qotsa o'yin pauzada (pastda) |
| F11 / Alt+Enter | `FullscreenToggle` | O'yin ichida to'liq ekran <-> oyna (standart: yoqiq) |

Standart: 1280x720, oynali, o'lchami o'zgaradi, VSync yoqiq, cheklovsiz, fonda ishlaydi - player avval ham
shunday edi.

O'yin ichidagi sozlamalar menyusi buni o'zgartira oladi va tanlovni saqlaydi (keyingi ishga tushishda
loyiha sozlamasidan ustun):

```cpp
Display::SetFullscreen(!Display::IsFullscreen());
Display::SetVSync(false);
Display::SetResolution(1920, 1080);   // Display::GetResolutions(): monitorning rejimlari
Display::SaveSettings();     // SaveData: "Zamin.Display.*"
```

**Ustunlik tartibi** (har bir qiymat alohida, `PlayerLaunchSettings::Resolve`):

1. buyruq qatori (`--fullscreen`, `--borderless`, `--windowed`, `--resolution`, `--monitor`, `--vsync` / `--no-vsync`,
   `--fps`, `--no-splash` - 18-bo'lim);
2. o'yinchi saqlagan tanlov (`Display::SaveSettings`: ekran o'lchami, to'liq ekran, VSync; sifat va til ham o'zinikini saqlaydi);
3. loyiha sozlamalari (shu bo'lim).

Player saqlangan tanlovni oyna yaratilishidan **oldin** o'qiydi: o'yin to'liq ekranda yopilgan bo'lsa, keyingi safar
darhol to'liq ekranda ochiladi (oynali -> to'liq ekran sakrashisiz). Masalan `--no-vsync` faqat VSync'ni o'zgartiradi:
saqlangan to'liq ekran tanlovi kuchda qoladi. Log'da har bir qiymat qayerdan kelgani yoziladi:

```
Oyna sozlamalari: 1600x900 (buyruq qatori), to'liq ekran (saqlangan), VSync: ha (loyiha), FPS: 60 (loyiha), monitor: asosiy (loyiha), splash: ha (loyiha)
```

**F11 / Alt+Enter**: o'yin ichida to'liq ekran (loyihaning turi: eksklyuziv yoki chegarasiz) va oyna orasida
almashtiradi. Oyna avvalgi o'lchami va joyiga qaytadi; to'liq ekran oyna turgan monitorda ochiladi (ikkinchi monitorga
surilgan oyna o'sha yerda to'liq ekran bo'ladi). Tanlov `Display::SaveSettings` kabi saqlanadi. Input xaritasida
`ToggleFullscreen` nomli harakat bo'lsa, u ham almashtiradi (masalan gamepad tugmasi). O'chirish: `FullscreenToggle: false`.

**FPS cheklovi va VSync**: cheklov (`TargetFrameRate` yoki `--fps N`) VSync bilan birga shunday ishlaydi
(`FramePacingPlan`):

| Holat | Nima ushlab turadi |
|---|---|
| Cheklov yo'q | VSync (yoqiq bo'lsa) yoki hech narsa |
| VSync + monitor chastotasini bo'ladigan cheklov (60 Hz da 30 / 20 / 15, 144 Hz da 72) | VSync har kadrda 2 / 3 / 4 yangilanish kutadi - kadrlar mutlaqo tekis |
| VSync + monitor chastotasidan katta yoki teng cheklov | VSync o'zi |
| VSync + boshqa cheklov (60 Hz da 50), VSync o'chiq, monitor chastotasi noma'lum | Kutish (uyqu + qisqa aylanish) |

Kutish jadval bo'yicha: kadrlar boshi bir-biridan aniq bir davr uzoqlikda (`FramePacer`), shuning uchun bitta uyqu
biroz kech uyg'onsa ham o'rtacha chastota aniq; davrdan uzoq to'xtash (yuklanish) jadvalni qayta boshlaydi.
`ZAMIN_PLAYER_STATS=1` chiqishda haqiqiy kadr vaqtlarini yozadi (o'rtacha, min, max, eng sekin 1%).

**Fonda pauza** (`RunInBackground: false`): oyna fokusni yo'qotsa (boshqa oyna oldinga chiqdi, Alt+Tab) o'yin
butunlay to'xtaydi - skriptlar, fizika, animatsiyalar, o'yin vaqti (`Time`) yangilanmaydi, audio chiqishi pauzada
(har bir tovush va musiqa joyida turadi, ovoz eshitilmaydi). Ekranda o'yinning oxirgi kadri xiralashgan holda
va o'rtada "Pauza" yozuvi (ostida "O'yin oynasi fokusda emas - davom etish uchun oynani tanlang"); o'yinning til
fayllarida `player.paused` / `player.paused_hint` kalitlari bo'lsa, ular ishlatiladi (Platformer2D: uz, ru, en).
Pauzada CPU band emas: player voqealarni kutadi, rasmni boshida, oyna o'lchami o'zgarganda va har 0.5 s da qayta
chizadi. Oyna yana fokusga kelsa o'yin to'xtagan joyidan davom etadi (uzun kadr sakrashisiz). Log:
`O'yin to'xtatildi (pauza): oyna fokusda emas` / `O'yin davom etmoqda: oyna yana fokusda`. Avtomatlashtirishda
(screenshot, CI) `ZAMIN_FOCUS_LOSS_TEST=<kadr>[:<kadrlar soni>]` shu kadrdan fokus yo'qolganini taqlid qiladi.

**Gamma va yorqinlik** (o'yinchi sozlamasi): player tayyor kadrni (sahna, UI, o'tishlar) ekranga oxirgi o'tishda
(`DisplayPass`) gamma va yorqinlik bilan chiqaradi: `ekran = (rang x Yorqinlik) ^ (1 / Gamma)`. Gamma 0.5 ... 2.0
(1 - o'zgarishsiz, kattasi - o'rta tonlar yorqinroq, qora va oq o'zgarmaydi), yorqinlik 0.5 ... 1.5 (butun tasvir
ko'paytiriladi). O'yin menyusi o'zgartiradi va saqlaydi:

```cpp
Display::SetGamma(1.2f);        // keyingi kadrdan
Display::SetBrightness(0.9f);
Display::SaveSettings();        // SaveData: "Zamin.Display.Gamma", "Zamin.Display.Brightness"
```

Player boshida saqlanganini oladi; `--gamma G` / `--brightness B` undan ustun. Ikkalasi 1 bo'lsa (va o'yin fonda
ishlasa) kadr to'g'ridan-to'g'ri ekranga chiziladi - avvalgidek, qo'shimcha o'tishsiz. Editorning Play rejimi ham
o'yin tasvirini shu sozlama bilan ko'rsatadi (editor gizmolari o'zgarmaydi); Play boshlanganda saqlangan qiymatlar
olinadi, Stop'da tiklanadi. Platformer2D pauza menyusining Sozlamalar > Ekran bo'limida "Gamma" va "Yorqinlik"
slayderlari bor.

### Splash ekran

Player birinchi sahnadan oldin Zamin logotipini (logotip dvigatelning o'zida chiziladi, fayl kerak emas) yoki o'yinning
o'z rasmini ko'rsatadi: paydo bo'ladi (0.5 s), turadi va so'nadi (0.5 s). Shu vaqtda birinchi sahna **fonda yuklanadi**
(`SceneManager::LoadSceneAsync`, keyin `SceneManager::Start(op)`); yuklanish splash'dan uzoq davom etsa, logotip
progress chizig'i bilan kutib turadi. Splash'dan keyin o'yinning birinchi kadri fon rangidan ochiladi.

| Maydon | Kalit (`"Splash": { ... }`) | Ma'nosi |
|---|---|---|
| Splash ko'rsatish | `Enabled` | Standart: yoqiq. Buyruq qatori: `--no-splash` |
| Davomiyligi | `Duration` | Soniya (0.5 ... 10), paydo bo'lish va so'nish bilan; standart 2 |
| Tugma bilan o'tkazish | `Skippable` | Istalgan tugma / sichqoncha / gamepad tugmasi tugatadi (sahna yuklangach) |
| O'z rasmi | `Image` | PNG / JPG (assetlarga nisbatan); bo'sh: Zamin logotipi |
| Fon rangi | `Background` | `[r, g, b]` (0 ... 1) |

Avtomatlashtirilgan ishga tushishlarda (`ZAMIN_MAX_FRAMES`: skrinshotlar, CI) splash ko'rsatilmaydi;
`ZAMIN_PLAYER_SPLASH=1` uni majburan yoqadi.

### Ekran nisbati (letterbox)

Bitta ekran shakli uchun qilingan o'yin (`"Aspect": { ... }`). Yoqilganda o'yin tasviri `Width : Height`
nisbatida qoladi: oyna kengroq bo'lsa yon tomonlarda, balandroq bo'lsa tepa va pastda `BarColor` rangli
polosalar. Sahnalar kamerasi, UI (canvas masshtabi), sichqoncha (`GetMousePosition`, UI bosishlari) va render
nishonlari tasvir o'lchamida ishlaydi; oyna o'lchami o'zgarsa (tortish, F11) hammasi har kadrda moslashadi va
yangi o'lcham bir marta log'ga yoziladi. O'chiq (standart): tasvir butun oynani egallaydi. Editorda Play ham
shu nisbatda ko'rsatiladi (o'yin ko'rinishi "Erkin" bo'lsa). Sozlamalar oynasi turli oynalarda (o'yin oynasi,
2560x1080, 1024x768) qanday ko'rinishini chizib beradi.

| Maydon | Kalit | Ma'nosi |
|---|---|---|
| Qat'iy nisbat | `Fixed` | Standart: o'chiq |
| Nisbat | `Width`, `Height` | Tomonlari 1 ... 64 (16:9, 4:3, 21:9 ...) |
| Polosalar rangi | `BarColor` | `[r, g, b]` (0 ... 1), standart qora |

Oyna kichraytirilganda (minimize) player va editor yangilash va chizishni to'xtatadi: sikl faqat
hodisalarni kutadi (protsessor band bo'lmaydi), o'yin vaqti o'tmaydi; oyna qaytganda davom etadi.

### Log

Player logi (`"Log": { ... }`, o'zgartirilmagan bo'lsa faylga yozilmaydi). Darajadan pastdagi xabarlar
yozilmaydi; `ZAMIN_LOG_LEVEL` va `--log-level` bulardan ustun ([Runtime.md, 6-bo'lim](Runtime.md#6-log-fayllari)).

| Maydon | Kalit | Ma'nosi |
|---|---|---|
| Dvigatel | `Engine` | Dvigatel xabarlari (`ZM_CORE_*`): `trace` ... `critical`, `off`; standart `trace` |
| O'yin skriptlari | `App` | O'yin xabarlari (`ZM_*`); standart `trace` |
| Log fayli | `WriteFile` | `player-<sana>.log` va `latest.log` yoziladimi (standart: ha; `--log-file` baribir yozadi) |
| Faylga yoziladigan | `File` | Faylga faqat shu darajadan boshlab (terminalda hammasi); standart `trace` |
| Saqlanadigan fayllar | `Keep` | Nechta eski log qoladi (1 ... 100, standart 5) |

### Ikonka

`Icon` - assetlar ichidagi PNG (masalan `Textures/GameIcon.png`). Player oynaga 16, 32, 48, 64, 128, 256 px
o'lchamlarini (rasmdan katta bo'lmaganlarini) qo'yadi; kvadrat bo'lmagan rasm shaffof kvadrat o'rtasiga
joylanadi. Sozlamalar oynasi ikonkani ko'rsatadi va ogohlantiradi (kvadrat emas, 64 px dan kichik ...).
Eksportda ikonka uch xil yoziladi:

- `<o'yin>.png` - asl rasm, launcher yonida;
- `icons/hicolor/<o'lcham>x<o'lcham>/apps/zamin-<o'yin>.png` - ikonka mavzusining tuzilishida 16, 32, 48, 64, 128,
  256 px (rasmdan katta bo'lmaganlari, `ExportFiles::WriteIconSet`);
- `<o'yin>.desktop` - papka qayerga ko'chirilsa ham ishlaydigan desktop fayli: uning `Exec`i o'zi turgan papkadagi
  launcher'ni ishga tushiradi (`sh -c "..." sh %k`, `%k` - desktop faylining o'z yo'li), `Path` yo'q. Faqat `Icon`
  to'liq yo'l bo'lishi shart (desktop fayllari nisbiy ikonka yo'lini olmaydi) - u eksport qilingan joyni ko'rsatadi.

Dasturlar menyusiga qo'shish: papkada `install-desktop-entry.sh` ni ishga tushiring (ko'chirilgan, arxivdan ochilgan
yoki boshqa kompyuterdagi papkada ham). Skript:

- `~/.local/share/applications/zamin-<o'yin>.desktop` (`$XDG_DATA_HOME/applications`) ni papkaning hozirgi to'liq
  yo'llari bilan yozadi;
- ikonkalarni hamma o'lchamda `~/.local/share/icons/hicolor/<o'lcham>/apps/` ga o'rnatadi va menyu yozuvi ikonkani
  nomi bilan oladi (`Icon=zamin-<o'yin>`: menyu, panel, Alt+Tab o'ziga kerakli o'lchamni topadi); ikonka to'plami
  bo'lmasa - launcher yonidagi PNG;
- launcher yonidagi `<o'yin>.desktop` ni yangi joy uchun qayta yozadi (ikonka yo'li yangilanadi).

`install-desktop-entry.sh --uninstall` menyu yozuvini va o'rnatilgan ikonkalarni olib tashlaydi.

---

## 5. Sifat darajalari

Sifat darajasi sahna **so'ragan** narsani cheklaydi: har sahna o'z soyalari, effektlari va render masshtabini
saqlaydi, faol daraja esa ulardan "ko'pi bilan" qancha ishlatilishini aytadi. Hamma narsaga ruxsat beruvchi
daraja (standart "Ultra") sahnani o'zgartirmaydi.

| Maydon | JSON kaliti | Ma'nosi |
|---|---|---|
| Soyalar | `Shadows` | O'chiq: quyosh umuman soya tashlamaydi |
| Soya atlasi (eng katta) | `ShadowAtlas` | 1024 / 2048 / 4096 |
| Soya filtri (eng qimmati) | `ShadowFilter` | `Hard` < `PCF3x3` < `Poisson` < `PCF5x5` |
| Kaskadlar (eng ko'pi) | `ShadowCascades` | 1 ... 4 |
| Soya masofasi | `ShadowDistance` | Sahna soya masofasiga ko'paytiriladi |
| SSAO, Bloom, FXAA, Avto ekspozitsiya | `SSAO`, `Bloom`, `FXAA`, `AutoExposure` | O'chiq: sahna yoqqan bo'lsa ham ishlamaydi |
| Render masshtabi | `RenderScale` | Sahna render masshtabiga ko'paytiriladi (0.25 ... 2) |
| Anizotropik filtr | `Anisotropy` | 1 (o'chiq), 2, 4, 8, 16 - darajaning o'ziniki |
| Tekstura sifati | `TextureQuality` | 0 to'liq, 1 yarim, 2 chorak, 3 sakkizdan bir o'lcham |

Standart darajalar:

| Daraja | Soya atlasi | Filtr | Kaskad | Soya masofasi | SSAO | Bloom / FXAA / Avto eksp. | Render masshtabi | Tekstura |
|---|---|---|---|---|---|---|---|---|
| Low | 1024 | Hard | 1 | x0.5 | yo'q | yo'q | x0.75 | yarim |
| Medium | 2048 | PCF3x3 | 2 | x0.75 | yo'q | ha | x1 | to'liq |
| High | 4096 | PCF3x3 | 3 | x1 | ha | ha | x1 | to'liq |
| Ultra | cheksiz | cheksiz | cheksiz | x1 | ha | ha | x1 | to'liq |

Darajalarni qo'shish, nusxalash, qayta nomlash va o'chirish mumkin (16 tagacha); **Standart daraja** o'yin
boshlanadigan darajadir. Editor ko'rinishi uchun ham daraja tanlanadi (*Sifat* bo'limida "Editorda ko'rish"),
*Sahna sozlamalari* esa joriy daraja sahnada nimani cheklayotganini yozib ko'rsatadi.

O'yindagi sozlamalar menyusi:

```cpp
// "Grafika: Low / Medium / High / Ultra" tugmasi
int next = (Quality::GetLevel() + 1) % Quality::GetLevelCount();
Quality::SetLevel(next);
Quality::SaveSettings();          // SaveData: "Zamin.Quality.Level" = daraja nomi
ZM_INFO("Sifat: {}", Quality::GetLevelName());
```

Player ishga tushganda tartib: `--quality <nom | indeks>` -> o'yinchi saqlagan daraja -> loyihaning standart
darajasi.

---

## 6. Grafika: OpenGL konteksti

`"Graphics"` bo'limi - player oynasining OpenGL konteksti va oyna buferi:

| Maydon | Kalit | Ma'nosi |
|---|---|---|
| OpenGL versiyasi | `OpenGL` | `4.1` (standart; dvigatel shuni talab qiladi, macOS'dagi eng kattasi), `4.3`, `4.5`, `4.6` core |
| Debug konteksti | `DebugContext` | `Auto` (faqat Debug build'da), `On` (Release'da ham, sekinroq), `Off` |
| MSAA (oyna) | `MSAA` | Oyna buferining namunalari: 0, 2, 4, 8, 16 |
| sRGB framebuffer | `SRGBFramebuffer` | Oxirgi rasm apparat sRGB kodlashi bilan (shader gamma 2.2 o'rniga) |

- Yangi versiya faqat drayverdan ko'proq narsa so'raydi; dvigatelning o'z chizishi bir xil.
- MSAA ekranga to'g'ridan-to'g'ri chiziladigan narsalarni (pixel-perfect o'yin ustidagi UI, debug chiziqlar)
  silliqlaydi; 3D rasmni FXAA silliqlaydi (sifat darajasi).
- Drayver so'ralgan kontekstni bera olmasa player standartga (4.1 core) qaytadi va logga yozadi.
- Editorning o'z konteksti doim 4.1 core; *Grafika* bo'limi ishlayotgan kontekstni ko'rsatadi: ishlab
  chiqaruvchi, renderer, versiya, MSAA, sRGB, eng katta tekstura va anizotropiya.
- Buyruq qatori: `--gl 4.6`, `--msaa 4`, `--gl-debug` / `--no-gl-debug`, `--srgb` / `--no-srgb`.

---

## 7. Vaqt va Time API

`"Time"` bo'limi:

| Maydon | Kalit | Standart | Ma'nosi |
|---|---|---|---|
| Qat'iy qadam | `FixedStepRate` | 60 | Fizika (2D va 3D) soniyada necha qadam (20 ... 240 Hz; presetlar 50, 60, 100, 120, 144, 240) |
| Kadrdagi qadamlar | `MaxStepsPerFrame` | 5 | Bitta kadr eng ko'pi bilan nechta qadam oladi; sekin kadrda qolgani tashlanadi (o'yin orqada qolib ketmaydi, sekinlashadi) |
| Boshlang'ich vaqt masshtabi | `DefaultTimeScale` | 1 | O'yin boshlanadigan soat tezligi (0 ... 10) |
| Eng uzun kadr | `MaxDeltaTime` | 0.1 s | Bundan uzun kadr (qotish, oynani sudrash) shuncha deb hisoblanadi - o'yin sakramaydi |

Qat'iy qadam va kadrdagi qadamlar o'zinikini bermagan sahnalar uchun ("Loyihadan"); sahna *Sahna sozlamalari >
Fizika* da o'z qiymatini qo'yishi mumkin.

### Time:: skript API

Bitta statik API - qayerdan chaqirilsa ham o'yin soati:

| Funksiya | Ma'nosi |
|---|---|
| `Time::GetDeltaTime()` | Shu kadrning o'yin vaqti (masshtablangan; pauzada 0) |
| `Time::GetUnscaledDeltaTime()` | Shu kadrning haqiqiy vaqti (eng ko'pi `MaxDeltaTime`) |
| `Time::GetFixedDeltaTime()` | Fizika qadami (1 / qadam chastotasi) |
| `Time::GetTime()` / `GetUnscaledTime()` | O'yin boshlangandan beri o'yin / haqiqiy vaqt |
| `Time::GetTimeSinceStartup()` | Dastur boshlangandan beri haqiqiy soniyalar |
| `Time::GetFrameCount()` | Dastur boshlangandan beri kadrlar |
| `Time::SetTimeScale(s)` / `GetTimeScale()` | Soat tezligi: 1 oddiy, 0.5 yarim, 0 to'xtagan |
| `Time::SetPaused(b)` / `IsPaused()` | Masshtab 0 va avvalgisiga qaytish (pauza menyusi) |
| `Time::GetSettings()` | Loyihaning vaqt sozlamalari |

Vaqt masshtabiga hamma yuklangan sahnalar ergashadi: fizika, animatsiyalar, particle'lar, taymer va
tween'lar. UI esa haqiqiy vaqtda ishlaydi (pauza menyusi pauzada ham jonli).

```cpp
// Sekinlashtirish (Adventure3D: T tugmasi)
void OnUpdate(Timestep) override
{
    if (Input::IsKeyJustPressed(Key::T))
        Time::SetTimeScale(Time::GetTimeScale() < 1.0f ? 1.0f : 0.25f);
}
```

Skriptning taymerlari va tween'lari haqiqiy vaqtda yursin desangiz (sekinlashtirish effektining o'z
so'nishi, menyu animatsiyasi) - `SetTimersUnscaled(true)`.

---

## 8. Fizika: gravitatsiya va to'qnashuv qatlamlari

### Standart gravitatsiya

`"Physics"` bo'limi: `Gravity2D` (standart `[0, -9.8]`) va `Gravity3D` (standart `[0, -9.81, 0]`). Bu qiymatni
o'z gravitatsiyasini bermagan hamma sahnalar oladi: yangi sahnalar va dvigatel standartini saqlagan eski
sahnalar (sahna formati v4 da ular "Loyihadan" ga o'tkazilgan). Sahna *Sahna sozlamalari > Fizika* da o'zinikini
qo'ya oladi. Presetlar: Yer (9.81), Oy (1.62), Mars (3.71), Yupiter (24.79), Platformer (20), Vaznsizlik (0).
Har komponent eng ko'pi +-1000 m/s².

### To'qnashuv qatlamlari

16 ta nomli qatlam va qaysi juftlar to'qnashishini ko'rsatuvchi matritsa (2D va 3D uchun bir xil,
`"CollisionLayers2D"`). Tayyor presetlar (masalan "Platformer 2D", "3D aksiya") matritsani bir bosishda to'ldiradi;
qatorni nusxalash / qo'yish mumkin. Kollayder komponentlari qatlamni nomi bilan tanlaydi, `Raycast` va
so'rovlar `LayerMask` oladi.

---

## 9. Audio standartlari

*Audio* bo'limi va *Audio Mixer* oynasi bitta sozlamani (`"Audio"`) tahrirlaydi.

| Maydon | Kalit | Ma'nosi |
|---|---|---|
| Master ovozi | `Groups[Master].Volume` | O'yinning umumiy ovozi: hamma ovozlar Master orqali o'tadi |
| Mikser guruhlari | `Groups` | Music, SFX, UI ... - har birining boshlang'ich ovozi, ota guruhi, filtrlari |
| Oqim chegarasi (MB) | `StreamThresholdMB` | Bundan katta fayllar xotiraga to'liq yuklanmaydi, oqim bilan o'qiladi |
| Bir vaqtda ovozlar | `MaxVoices` | Eng ko'pi bilan nechta ovoz chalinadi (ortig'i - eng kam muhimi o'rnini beradi) |
| Doppler | `DopplerFactor` | Harakatlanayotgan 3D ovozlarning Doppler effekti: 1 tabiiy, 0 o'chiq |

### 3D ovoz standartlari

`"Audio"` ichidagi `"Spatial"` (faqat standartdan farq qilsa yoziladi):

| Maydon | Kalit | Standart | Ma'nosi |
|---|---|---|---|
| Min masofa | `MinDistance` | 1 m | Bundan yaqinda ovoz to'liq balandlikda |
| Max masofa | `MaxDistance` | 30 m | Bundan uzoqda ovoz boshqa pasaymaydi (chiziqli modelda - eshitilmaydi) |
| Masofa modeli | `Rolloff` | `Inverse` | Ovoz masofa bilan qanday pasayadi (pastda) |
| Pasayish (rolloff) | `RolloffFactor` | 1 | Qanchalik tez pasayadi: 1 model o'zi, 0 umuman pasaymaydi (0 ... 10) |

Masofa modellari (`d` - masofa, `min`/`max` oralig'ida qisiladi, `k` - pasayish koeffitsiyenti):

| Model | Kuchaytirish | Qachon |
|---|---|---|
| `Inverse` (teskari) | `min / (min + k (d - min))` | Tabiatdagidek: yaqinda tez, keyin sekin pasayadi |
| `Linear` (chiziqli) | `1 - k (d - min) / (max - min)` | Max masofada jim bo'lishi kerak bo'lsa |
| `Exponential` | `(d / min) ^ -k` | Teskaridan tezroq pasayadi |
| `None` (masofasiz) | `1` | Hamma joyda bir xil (faqat chap / o'ng) |

Sozlamalar oynasi egri chiziqni chizadi (ustiga olib borilsa shu masofadagi foiz va dB). Masofa modeli hozir
chalinayotgan 3D ovozlarga ham darhol ta'sir qiladi. Yangi **Audio Source** va **Footsteps** komponentlari
loyihaning min / max masofasi bilan yaratiladi; inspektor ularning ostida loyiha qiymatlarini ko'rsatadi va
"Standart" tugmasi bilan qaytaradi. Skriptlarda:

```cpp
// Masofalarsiz: loyiha standarti
PlaySoundAt("Audio/explosion.wav", GetTransform().Translation);

// Kodda o'qish
const AudioSpatialSettings& spatial = Audio::GetSpatialDefaults();
float gain = spatial.ComputeGain(12.0f);   // 12 m dagi kuchaytirish (0..1)
```

`SoundParams`ning `MinDistance` / `MaxDistance` maydonlari standart `-1` - "loyihaniki" degani.

---

## 10. Input

`"InputActions"`: nomli **harakatlar** (`Jump`: `Key:Space`, `Gamepad:A` ...), **o'qlar** (`Horizontal`: musbat /
manfiy tugmalar, gamepad o'qi, deadzone, teskari) va **2D vektorlar** (`Move` = `Horizontal` + `Vertical`, `Clamp`
yoki `Normalize` rejimi, radial deadzone). Tahrirlovchida bog'lanish qo'shish uchun "Tugmani bosing..." rejimi bor
(klaviatura, sichqoncha yoki gamepad), takroriy bog'lanishlar ogohlantiriladi, filtr bilan qidiriladi.

```cpp
if (Input::IsActionJustPressed("Jump")) Jump();
glm::vec2 move = Input::GetVector("Move");
```

---

## 11. Teglar va qatlamlar

- **Teglar** (`"Tags"`): inspektorda taklif qilinadigan gameplay teglari. Bo'lim har tegning loyihadagi
  ishlatilishini ko'rsatadi; qayta nomlash hamma sahna fayllarida o'zgartiradi. Obyektlar yoki fayllar o'zgargan
  qayta nomlash `.zproject` ning teglar ro'yxatiga ham darhol yoziladi (boshqa saqlanmagan sozlamalar saqlanmagan
  holicha qoladi): "Bekor qilish" eski nomni qaytarmaydi, aks holda sahnalardagi teg ro'yxatda bo'lmay qolardi.
- **Saralash qatlamlari** (`"SortingLayers"`): 2D chizish tartibi (orqadan oldinga), Y bo'yicha saralash.

---

## 12. Build sahnalar

`"BuildScenes"`: o'yin quriladigan sahnalar tartib bilan. Yoqilganlari build indeksini oladi (0, 1, 2 ...;
`LoadScene(int)`), eksport faqat ularni (va boshlang'ich sahnani) oladi. Oyna muammolarni ko'rsatadi:
boshlang'ich sahna yo'q / ro'yxatda emas / o'chirilgan, fayl topilmadi, takroriy sahna. Batafsil:
[docs/Scenes.md, 7-bo'lim](Scenes.md#7-build-sahnalar-royxati-va-indekslar).

### Eksport: ishlatilmagan assetlar

Eksport (`File > O'yinni build qilish`) Assets papkasidan faqat o'yin ishlatadiganlarini oladi
(`ExportAssets::Plan`, `AssetReferenceIndex`):

- build sahnalari (ro'yxat bo'sh bo'lsa - hamma sahnalar), boshlang'ich sahna va ularning sub-sahnalari;
- ular orqali kerak bo'lganlar: prefab, material, model (va uning `.bin` / rasm fayllari), sprite va xossa
  animatsiya kliplari (`.zsanim`, `.zanim`), particle preset, shrift, tovush ...;
- loyiha fayli nomlaganlar (ikonka, splash rasmi) va C++ skriptlardagi satrlar (`"Audio/jump.wav"`);
- doim: `Resources/` papkasi, til fayllari papkasi (`Localization/`) va "Doim kiritish" ro'yxati.

Build ro'yxatida yo'q sahna har doim chiqarib tashlanadi. Hech qaysi turga kirmaydigan va hech kim nomlamagan
fayllar (masalan `Data/levels.json`) kiritiladi - skript ularni o'zi o'qishi mumkin. Chiqarib tashlanganlar
log'ga va eksport papkasidagi `excluded-assets.txt` ga yoziladi (yo'l, hajm, sabab); eksport oynasi va
Project settings > Build sahnalar > Eksport keyingi eksport nimani olishini ko'rsatadi (ro'yxatdagi assetni
o'ng tugma bilan "Doim kiritish"ga qo'shish mumkin). O'yin registriga (`AssetRegistry.zreg`) ham faqat
kiritilganlar yoziladi.

```json
"Export": {
    "ExcludeUnusedAssets": true,
    "AlwaysInclude": [ "Data", "Levels/*.zscene" ]
}
```

Skript asset nomini o'zi yasasa (`"Levels/Level" + n + ".zscene"`), uni "Doim kiritish"ga qo'shing (`*`
istalgan qismga mos keladi). `ExcludeUnusedAssets: false` - hamma fayl kiradi (build ro'yxatidan tashqari
sahnalardan boshqa). Bo'lim standartdan farq qilganda yoziladi.

### Eksport: Release skript moduli va strip

Eksport C++ skriptlarni o'zi qayta kompilyatsiya qiladi (`"ReleaseScriptModule": true`, standart): Release
rejimida, `-O2 -DNDEBUG` bilan, alohida build papkasida (`<loyiha>/Build/ExportRelease`, `ExportModuleBuild`).
Modul o'sha papkaning `bin/` iga yoziladi (`cmake/ZaminGame.cmake`: `ZAMIN_GAME_MODULE_OUTPUT_DIR`), shuning uchun
editorning hot reload moduli (`Binaries/lib<o'yin>.so`) o'zgarmaydi. Loyihada `CMakeLists.txt` bo'lmasa, u
yoziladi. Kompilyatsiya bo'lmasa (cmake / kompilyator yo'q, kodda xato) editor moduli nusxalanadi va hisobotda
ogohlantirish chiqadi.

So'ng eksport qilingan binarlar - `ZaminPlayer`, `libZamin.so` va skript moduli - `strip --strip-unneeded` dan
o'tadi (`"StripBinaries": true`, standart; `BinaryStrip`): debug ma'lumotlari va lokal simvollar olib tashlanadi,
dinamik simvollar qoladi (modul yuklanadi, crash hisobotida eksport qilingan funksiya nomlari ko'rinadi). Debug
build qilingan engine'da bu eksport hajmini bir necha marta kamaytiradi. `strip` (binutils) topilmasa, binarlar
o'zgarmaydi va hisobotda ogohlantirish chiqadi. Ikkala variant eksport oynasida ham bor.

```json
"Export": {
    "ReleaseScriptModule": true,
    "StripBinaries": true
}
```

### Eksport hisoboti

Eksport tugagach editor "Eksport hisoboti" oynasini ochadi (`File > Eksport hisoboti` - qayta ochish) va hisobotni
o'yin papkasiga yozadi: `export-report.json` (vositalar uchun) va `export-report.txt` (o'qish uchun),
`ExportReport`:

- o'yin, versiya, papka, sana, eksport qancha vaqt olgani, natija (muvaffaqiyatli / ogohlantirishlar / xatolar);
- jami hajm va fayllar soni;
- turlar bo'yicha hajm va ulush: binarlar, teksturalar, modellar (`.gltf`, `.glb`, `.bin`), audio, sahnalar
  (`.zscene`, `.zsceneb`, prefablar), shriftlar, ma'lumotlar (material, klip, `.zmeta`, loyiha fayli, JSON),
  boshqa (launcher, desktop fayli, ikonkalar);
- eng katta 15 ta fayl;
- chiqarib tashlangan assetlar (yo'l, hajm, sabab);
- skript moduli qanday olingani (Release kompilyatsiya vaqti bilan / editor moduli), strip qancha kamaytirgani;
- ogohlantirishlar va xatolar (build sahnalari muammolari, o'qilmagan ikonka, nusxalash xatolari ...).

Oynada papkani ochish, `.txt` faylini ochish va hisobot matnini nusxalash tugmalari bor. Avtomatlashtirish:
`ZAMIN_EDITOR_EXPORT=<papka>` eksportni tugashini kutadi (hisobot oynasi ochiladi), `ZAMIN_EDITOR_EXPORT_REPORT=<papka>`
oldingi eksportning hisobotini ochadi.

---

## 13. Lokalizatsiya

### Til fayllari

Har til - `Assets/Localization/<kod>.json` fayli (papka sozlamada o'zgaradi). Ichma-ich obyektlar nuqtali
kalitlarga aylanadi, `"$name"` - tilning o'z nomi, faqat ko'plik shakllaridan iborat obyekt - bitta ko'plik
yozuvi:

```json
{
    "$name": "Русский",
    "menu": {
        "start": "Начать",
        "options": { "audio": "Звук" }
    },
    "hud": {
        "score": "Очки: {0}",
        "coins": { "one": "{0} монета", "few": "{0} монеты", "many": "{0} монет", "other": "{0} монеты" }
    }
}
```

Bu yerda kalitlar: `menu.start`, `menu.options.audio`, `hud.score`, `hud.coins`. Yangi loyiha `uz.json` va
`en.json` bilan yaratiladi.

### Sozlamalar (`"Localization"`)

| Maydon | Kalit | Ma'nosi |
|---|---|---|
| Standart til | `DefaultLanguage` | O'yin shu tilda boshlanadi; boshqa tilda yo'q yoki bo'sh kalitlar shu tildan olinadi (standart `uz`) |
| Tillar ro'yxati | `Languages` | Taklif qilinadigan tillar, til menyusidagi tartibda. Bo'sh: papkadagi hamma fayllar (standart til birinchi) |
| Til fayllari papkasi | `Directory` | Assets ichidagi papka (standart `Localization`) |
| Tizim tilida boshlash | `UseSystemLanguage` | O'yin kompyuter tilida (`$LC_ALL` / `$LC_MESSAGES` / `$LANG`) boshlanadi, agar u taklif qilinsa |

Til kodi: 2-8 belgi, harf, raqam, `-` yoki `_` (`uz`, `ru`, `en`, `pt-BR`). Bo'lim har tilning faylini,
yetishmayotgan tarjimalar sonini ko'rsatadi, yo'q faylni yaratadi, til qo'shadi; ro'yxatdagi tilni bosish uni
editorda ko'rish tili qiladi (sahnadagi kalitli matnlar shu tilda ko'rinadi).

### Skript API

```cpp
Localize("menu.start");                    // "Boshlash" / "Начать" / "Start"
Localize("hud.score", score);              // "Ochko: {0}" -> "Ochko: 120"
Localize("hud.time", minutes, seconds);    // "{0}:{1:02}" - std::format belgilari ishlaydi
LocalizePlural("hud.coins", coins);        // ko'plik shakli soniga qarab, {0} = son

Localization::SetLanguage("ru");           // taklif qilinmagan til: false, hech narsa o'zgarmaydi
Localization::NextLanguage();              // keyingi til (til tugmasi)
Localization::SaveSettings();              // o'yinchi tanlovi: SaveData "Zamin.Language"
Localization::GetLanguage();               // "ru"
Localization::GetLanguageName("ru");       // "Русский"
Localization::GetLanguages();              // { "uz", "ru", "en" }
```

- **Zaxira**: joriy tilda kalit yo'q yoki bo'sh bo'lsa standart tilniki olinadi; hech qayerda bo'lmasa kalitning
  o'zi qaytadi va bir marta ogohlantiriladi (`Localization::GetMissingKeys()` ro'yxati; lokalizatsiya jadvali
  ularni "qo'shish" tugmasi bilan ko'rsatadi).
- **Formatlash**: `{0}`, `{1}` ... std::format qoidalari bilan (`{0:.1f}`, `{1:>4}`). Argumentsiz `Localize` matnni
  o'zgartirmaydi (qavslarni qochirish shart emas); buzilgan naqsh matnni o'zgarishsiz qaytaradi.
- **Ko'plik**: CLDR qoidalari (o'zbek / ingliz: one, other; rus: one, few, many, other; yapon, xitoy: faqat other).

### Text komponenti va til almashishi

Text komponentining **Lokalizatsiya kaliti** maydoni (inspektorda kalitlar ro'yxatidan tanlanadi): matn
joriy tildagi satr bilan ko'rsatiladi va til o'zgarsa o'zi yangilanadi. Kalit hech bir tilda bo'lmasa
komponentning o'z `Text`i ko'rinadi (inspektor ogohlantiradi).

Skript o'zi yasagan matnlar uchun `OnLanguageChanged` chaqiriladi (keyingi kadrda, `OnUpdate`dan oldin):

```cpp
class Hud : public ScriptableEntity
{
public:
    void OnCreate() override { Refresh(); }
    void OnLanguageChanged(const std::string& language) override { Refresh(); }

private:
    void Refresh()
    {
        if (auto* text = TryGetComponent<TextComponent>())
            text->Text = Localize("hud.coins_title") + ": " + LocalizePlural("hud.coins", m_Coins);
    }
    int m_Coins = 0;
};
```

Player tilni shunday tanlaydi: `--lang <kod>` -> o'yinchi saqlagan til -> tizim tili (yoqilgan bo'lsa) ->
standart til. Editorning Play rejimi o'yin ichida til almashsa, To'xtatishda editor ko'rish tiliga qaytadi.

### Lokalizatsiya jadvali

*Oyna > Lokalizatsiya*: hamma til fayllari bitta jadvalda - har kalit bir qator, har til bir ustun.
Yetishmayotgan tarjimalar belgilanadi va sanaladi ("Faqat yetishmayotganlar" filtri), `{0}` belgilari standart
tildagidan farq qilsa ogohlantiriladi, ko'plik shakllari alohida tahrirlanadi. Kalit qo'shish, qayta nomlash,
o'chirish (hamma tillardan), `Localize("...")` ni nusxalash bor. **Saqlash** har `<kod>.json`ni yozadi va
tillarni qayta yuklaydi; fayllar editor tashqarisida o'zgarsa ham editor ularni o'zi qayta o'qiydi.

Namuna: **Platformer2D** menyu, HUD, yuklanish ekrani va pauza menyusi uch tilda (uz, ru, en); menyuda `L`
tugmasi va pauza menyusidagi til tugmasi tilni almashtiradi va tanlovni saqlaydi.

---

## 14. Debug chizish

`"DebugDraw"`: o'yinda (player) skriptlarning `DebugDraw` shakllari va fizika ko'rinishlari: raycastlar
(`RaycastDuration` soniya ko'rinadi), kontakt nuqtalari va ularning tafsilotlari, joint'lar, personaj va jismlar
tezligi (`VelocityScale`), chiziq qalinligi, matn o'lchami. Buyruq qatori: `--no-debug-draw`, `--debug-raycasts`,
`--debug-contacts`, `--debug-contact-details`, `--debug-joints`, `--debug-velocities`, `--debug-nav` (navigatsiya: grid kataklari,
agentlar yo'li, ko'rish konuslari, patrul yo'llari; `--debug-nav-cells` - yuriladigan kataklar ham). Shipping build'larda debug
chizish umuman yo'q; editor viewport'ining o'z tugmalari bor.

---

## 15. Saqlangan ma'lumotlar (SaveData)

*Saqlangan ma'lumotlar* bo'limi o'yin `SaveData` bilan saqlagan qiymatlarni (o'yinlar orasida qoladi) ko'rsatadi,
tahrirlaydi, yangi qiymat qo'shadi va tozalaydi. Ular loyiha faylida emas, foydalanuvchi papkasida
(`~/.local/share/<kompaniya>/<mahsulot>`). Dvigatelning o'z kalitlari: `Zamin.Display.*` (to'liq ekran, VSync),
`Zamin.Quality.Level`, `Zamin.Language`.

---

## 16. Editor tili (o'zbek / ingliz)

Editorning o'z interfeysi ikki tilda: **o'zbek** (standart) va **ingliz**. *Tahrirlash > Sozlamalar > Ko'rinish >
Editor tili* - darhol almashadi va `~/.config/zamin/preferences.json` ga (`"Language": "uz" | "en"`) yoziladi.

Tarjima qilinadi: asosiy menyular (Fayl, Tahrirlash, Ko'rinish, Obyekt, Skriptlar, Oyna, Yordam), hamma oyna
sarlavhalari va dialoglar, tugmalar, belgilash katakchalari, tablar va bo'lim sarlavhalari, qidiruv maydonlari,
holat paneli, loyiha sozlamalari yorliqlari. Uzun izohlar (tooltip'lar), log xabarlari va o'yinning o'z matnlari
o'zbekcha qoladi. O'yinning tili (13-bo'lim) editor tilidan mustaqil.

Avtomatlashtirish / skrinshotlar uchun: `ZAMIN_EDITOR_LANGUAGE=en ZaminEditor ...` (faylga yozilmaydi).

Dasturchilar uchun: matnlar o'zbekcha yoziladi va `Tr()` bilan o'raladi; inglizchasi
`Zamin/src/Zamin/Editor/EditorLanguage.cpp` jadvalidan olinadi (kalit - o'zbekcha matn):

```cpp
if (ImGui::BeginMenu(Tr("Fayl")))                                   // "File"
ImGui::Begin(Tr(ICON_FA_GEAR "  Loyiha sozlamalari###ProjectSettings"));  // ikonka, "###id" saqlanadi
ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Sahnani ochish..."));     // "..." va ":" ham saqlanadi
std::format("{}: {}", EditorText::Choose("Bekor qilish", "Undo"), name); // bir so'z - ikki ma'no
```

`UI::PropertyLabel` va `UI::PropertyHeader` yorliqlarni o'zi tarjima qiladi. Oyna sarlavhalarida `###id`
ishlatiladi - til almashganda oynaning joyi va joylashuvi (layout) saqlanadi. Jadvalda yo'q matn o'zgarishsiz
ko'rinadi.

---

## 17. .zproject fayli: format, versiya, migratsiya

Fayl JSON, bo'limlar qat'iy tartibda: `Version`, `Project`, `Player`, `BuildScenes`, `InputActions`, `Tags`,
`SortingLayers`, `CollisionLayers2D`, `Physics`, `Time`, `Audio`, `Quality`, `Graphics`, `Localization`,
`Export`, `DebugDraw`. Standart qiymatdagi bo'limlar (`Physics`, `Time`, `Quality`, `Graphics`, `Localization`,
`Export`, audio `Spatial`) yozilmaydi - eski fayllar o'zgarmaydi.

- **Versiya** (`"Version": 2`): eski fayl (v0 - tekis, v1 - `"Version"`siz) o'qilganda migratsiyalar zanjiri bilan
  joriy versiyaga keltiriladi; diskdagi fayl loyiha saqlanmaguncha o'zgarmaydi. Log nima o'zgarganini yozadi.
- **Noma'lum kalitlar** (asboblar bo'limlari, yangiroq dvigatel kalitlari) saqlanadi va loyiha saqlanganda
  joyida qayta yoziladi; *Umumiy* bo'limi ularni ko'rsatadi.

Qisqartirilgan misol (Platformer2D asosida; `Physics`, `Time`, `Quality`, `Graphics` qiymatlari namuna uchun):

```json
{
    "Version": 2,
    "Project": {
        "Name": "Platformer2D",
        "StartScene": "Scenes/Level1.zscene",
        "AssetDirectory": "Assets",
        "SourceDirectory": "Source",
        "ScriptModulePath": "Binaries/libPlatformer2D.so"
    },
    "Player": {
        "Company": "Zamin Studio",
        "Product": "Zamin Platformer",
        "Version": "0.4.0",
        "Width": 1280, "Height": 720,
        "WindowMode": "Windowed",
        "Resizable": true, "VSync": true,
        "Title": "", "VersionInTitle": false,
        "TargetFrameRate": 0, "RunInBackground": true,
        "Icon": "Textures/GameIcon.png"
    },
    "BuildScenes": [ { "Path": "Scenes/Menu.zscene", "Enabled": true, "UUID": "3c8e51a07d24f9b6" } ],
    "Physics": { "Gravity2D": [0.0, -20.0], "Gravity3D": [0.0, -9.81, 0.0] },
    "Time": { "FixedStepRate": 60, "MaxStepsPerFrame": 5, "DefaultTimeScale": 1.0, "MaxDeltaTime": 0.1 },
    "Audio": {
        "StreamThresholdMB": 4.0, "MaxVoices": 64, "DopplerFactor": 1.0,
        "Groups": [ { "Name": "Master", "Volume": 1.0 }, { "Name": "Music", "Volume": 0.7, "Parent": "Master" } ],
        "Spatial": { "MinDistance": 2.0, "MaxDistance": 50.0, "Rolloff": "Linear", "RolloffFactor": 1.0 }
    },
    "Quality": { "Default": "High", "Levels": [ { "Name": "Low", "Shadows": true, "ShadowAtlas": 1024 } ] },
    "Graphics": { "OpenGL": "4.6", "DebugContext": "Auto", "MSAA": 4, "SRGBFramebuffer": false },
    "Localization": { "DefaultLanguage": "uz", "Languages": ["uz", "ru", "en"], "Directory": "Localization" }
}
```

---

## 18. Player buyruq qatori

Player, eksport, log, crash, versiya, ishlash va eksport testlari haqida to'liq qo'llanma: [Runtime.md](Runtime.md).

```
ZaminPlayer [O'yin.zproject | loyiha papkasi] [variantlar]     # hammasi: ZaminPlayer --help
```

Qiymatli variantlar ikki xil yoziladi: `--fps 60` yoki `--fps=60`. Tushunilmagan variant yoki noto'g'ri qiymat
ogohlantirish bilan log'ga yoziladi, qolganlari ishlatiladi.

| Variant | Ma'nosi | Nimadan ustun |
|---|---|---|
| `--windowed`, `--fullscreen`, `--borderless` | Oyna rejimi (oxirgisi yutadi) | Saqlangan `Display` tanlovi, loyiha |
| `--resolution KxB` (`--width K`, `--height B`) | Oyna o'lchami; `--fullscreen` bilan video rejim ham | Saqlangan o'lcham, loyiha |
| `--monitor N` | Qaysi monitorda ochiladi (0 - asosiy; log monitorlar ro'yxatini yozadi) | Asosiy monitor |
| `--vsync`, `--no-vsync` | Vertikal sinxronizatsiya | Saqlangan tanlov, loyiha |
| `--fps N` | Kadr chastotasi cheklovi (0 - cheklovsiz, 10 ... 1000) | Loyiha |
| `--scene <yo'l / indeks / nom>` | Boshlang'ich sahna | Loyihaning boshlang'ich sahnasi |
| `--quality <nom / indeks>` | Sifat darajasi | O'yinchi saqlagan daraja, loyiha standarti |
| `--lang <kod>` (`--language`) | Til; yo'q til - ogohlantirish, joriy til qoladi | Saqlangan til, tizim tili, standart til |
| `--no-splash` | Splash ekransiz | Loyiha |
| `--gamma G`, `--brightness B` | Tasvir gammasi (0.5 ... 2.0) va yorqinligi (0.5 ... 1.5); 1 - o'zgarishsiz | Saqlangan tanlov |
| `--gl`, `--msaa`, `--gl-debug` / `--no-gl-debug`, `--srgb` / `--no-srgb` | OpenGL konteksti | Grafika sozlamalari |
| `--no-debug-draw`, `--debug-raycasts`, `--debug-contacts`, `--debug-contact-details`, `--debug-joints`, `--debug-velocities`, `--debug-nav`, `--debug-nav-cells` | Debug chizish | Loyihaning debug sozlamalari |
| `--log-file <fayl>`, `--no-log-file` | Log faqat shu faylga / log faylsiz | Standart log papkasi |
| `--no-crash-handler`, `--no-crash-dialog` | Crash hisobotisiz (debugger, core dump) / hisobot bor, oyna yo'q | - |
| `--benchmark N` (`--benchmark-warmup`, `--benchmark-out`, `--benchmark-max-avg`, `--benchmark-max-p95`) | Ishlash testi: N kadr o'lchanadi (o'rtacha, mediana, p95, p99), JSON, chiqish kodi ([Runtime.md](Runtime.md#12-ishlash-testi-headless)) | Splash, FPS cheklovi, VSync |
| `-v`, `--version` | O'yin va dvigatel versiyasi (kommit, build sanasi) - oynasiz | - |
| `-h`, `--help` | Hamma variantlar va ustunlik tartibi | - |

O'yin ichida: **F11** / **Alt+Enter** - to'liq ekran <-> oyna, **F10** - FPS va audio profili log'ga.

### Log fayllari

Player har ishga tushganda logni `~/.local/share/<kompaniya>/<o'yin>/logs/player-<sana_vaqt>.log` ga yozadi
(kompaniya bo'sh bo'lsa `~/.local/share/zamin/<o'yin>/logs`; `$XDG_DATA_HOME` hisobga olinadi). Eng oxirgi
5 tasi (Player > Log > Saqlanadigan fayllar) saqlanadi, eskilari o'chiriladi; `latest.log` - doim oxirgi ishga tushish. Fayl boshida o'yin nomi,
versiyasi, dvigatel versiyasi, boshlanish vaqti va buyruq qatori yoziladi; fayl ochilishidan oldingi xabarlar
ham (ishga tushish) yoziladi. Har satr darhol diskka yoziladi - crash bo'lsa ham log to'liq qoladi. Nechta fayl
saqlanishi, log fayli yozilishi va darajalar: Player > Log (yuqorida).
API: `LogFile::OpenRotating`, `LogFile::Open`, `LogFile::GetPath`, `LogFile::GetDefaultDirectory`.

### Crash hisobotlari

`CrashHandler` SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS signallarini va ushlanmagan C++ istisnolarini
ushlaydi va log papkasiga `crash-<sana_vaqt>.txt` yozadi: o'yin va versiya, sabab (signal, kodi va manzili
yoki istisno turi va matni), backtrace, oxirgi 48 log satri, yuklangan modullar. Log fayliga va terminalga
qisqa xabar yoziladi. Keyin xabar oynasi ochiladi (alohida jarayon, o'yin darhol yopiladi): `zenity`,
`kdialog`, bo'lmasa playerning o'z oynasi (`ZaminPlayer --crash-dialog <hisobot>`: sabab, hisobot yo'li,
"Papkani ochish", "Yo'lni nusxalash", "Yopish"), oxiri `xmessage`. Oyna backtrace'dagi C++ nomlarini
o'qiladigan qiladi. Signal handler faqat async-signal-safe chaqiruvlardan foydalanadi (hamma narsa oldindan
tayyorlanadi; ikkinchi crash darhol tugatadi). Signal so'ng odatdagidek dasturni tugatadi (chiqish kodi, core
dump o'zgarmaydi). Avtomatik ishga tushishlarda (`ZAMIN_MAX_FRAMES`) oyna ko'rsatilmaydi
(`ZAMIN_CRASH_DIALOG=1` majburlaydi); `ZAMIN_PLAYER_CRASH_TEST=<kadr>:<segv|abort|fpe|exception>` ataylab crash
qiladi (sinov uchun).

### Kursor (skript API)

```cpp
Cursor::SetLockMode(CursorLockMode::Locked);   // yashirin, joyida: Input::GetMouseDelta() bilan sichqoncha-qarash
Cursor::SetLockMode(CursorLockMode::Confined); // oyna ichida qoladi
Cursor::SetVisible(false);
Cursor::SetImage("Textures/crosshair.png", { 16, 16 });   // o'z rasmi (hotspot), Cursor::ClearImage()
Cursor::SetShape(CursorShape::Hand);                      // tizim shakllari
Cursor::SetReleaseWhenPaused(true);                       // Time::SetPaused paytida bo'sh va ko'rinadigan (standart)
```

Istaklar saqlanadi va har kadr oynaga qo'llanadi: oyna fokusni yo'qotsa, o'yin pauzada bo'lsa va Play
to'xtaganda tizim kursori qaytadi. Editorda o'yin ko'rinishi fokusda bo'lganda ishlaydi; **Esc** kursorni
editorga qaytaradi (o'yin ko'rinishini bosish - yana o'yinga). Adventure3D kamerasi o'ng tugma bilan
aylantirganda kursorni bloklaydi.

---

## 19. Testlar

`Tests/ZaminTests.cpp` (`./build/bin/ZaminTests`) loyiha sozlamalarini tekshiradi, jumladan:

| Test | Nimani |
|---|---|
| `ProjectSettingsFullRoundTrip` | Hamma bo'limlar yozilib, qayta o'qilganda bir xil |
| `PlayerSettingsSerialization`, `PlayerCompanySaveDataAndVersion`, `GameIconImagesAndDesktopEntry` | Oyna, nom / versiya, SaveData papkasi, ikonka va .desktop |
| `PlayerCommandLineParser`, `PlayerLaunchSettingsPrecedence` | Buyruq qatori, `--help`, ustunlik tartibi (buyruq qatori > saqlangan > loyiha) |
| `PlayerSettingsFullscreenToggleAndSplash`, `SplashTimelineAndLogo`, `SceneManagerStartFromBackgroundLoad` | F11, splash sozlamalari, vaqt jadvali, logotip, fonda yuklangan birinchi sahna |
| `FramePacingPlanAndPacer`, `InputAnyKeyAndMonitorPlacement` | FPS cheklovi va VSync, kadr jadvali, "istalgan tugma", monitor tanlash |
| `QualityLevelLimits`, `QualitySettingsJson`, `QualityRuntimeLevel` | Darajalar, cheklashlar, JSON, saqlangan tanlov |
| `ProjectFileMigrationAndUnknownKeys` | v0 / v1 -> v2 migratsiya, noma'lum kalitlar saqlanishi |
| `GraphicsContextSettings` | Versiyalar, MSAA, debug konteksti, JSON |
| `TimeSettingsAndClock`, `TimeApiTimeScaleAndUnscaledTimers` | Qadam, masshtab, `Time::` API |
| `ProjectDefaultGravityAndStep` | Standart gravitatsiya, "Loyihadan" sahnalar |
| `AudioSpatialDefaults` | Masofa modellari, `Spatial` JSON, yangi manbalar standarti |
| `LocalizationStringFiles` | Til fayli: ichma-ich kalitlar, ko'plik, `$name`, yozish / o'qish |
| `LocalizationLookupFormatPlural` | Zaxira til, `{0}` formatlash, ko'plik, yo'q kalitlar ro'yxati, til almashishi, tizim tili, saqlangan tanlov |
| `LocalizationTableEditingAndFiles` | Jadval: kalit / til qo'shish, qayta nomlash, yetishmayotganlar, `{0}` mosligi, fayllar |
| `LocalizedTextComponentAndScripts` | Text komponenti kaliti til bilan yangilanadi, `OnLanguageChanged` |
| `ProjectLocalizationSettings` | `"Localization"` bo'limi va loyiha bilan yuklanishi |
| `EditorLanguageStrings` | Editor tili jadvali: tarjima, ikonka / `###id` saqlanishi, `Choose`, preferences |
| `PlayerLogFileRotation` | Log fayl nomi, oxirgi 5 tasi, `latest.log`, `--log-file`, ochilishdan oldingi xabarlar |
| `CrashHandlerReports` | Hisobot matni, demangle, o'qish, oyna buyruqlari; bola jarayonda haqiqiy SIGSEGV, istisno, abort |
| `CursorApiForScripts` | Kursor holatlari (fokus, pauza), rasm, `Reset`, bloklashda sichqoncha siljishi |
| `PlayerViewportFixedAspect` | Ekran nisbati: polosalar, sichqoncha, `"Aspect"` JSON, UI masshtabi oyna bilan |
| `ExportExcludesUnusedAssets` | Eksportga kiradigan / kirmaydigan assetlar, sabablar, `"Export"` bo'limi, namuna loyihalar |
| `ExportReleaseModuleAndStrip` | Release modul buyruqlari (`-O2`, alohida build va chiqish papkasi), haqiqiy kichik modul build'i, `strip` |
| `ExportDesktopEntryRelocatableAndIcons` | Ko'chiriladigan `.desktop` (`%k`), ko'chirilgan papkadan ishga tushirish, ikonka o'lchamlari, o'rnatish skripti |
| `ExportReportSizesAndFiles` | Hisobot: turlar, eng katta fayllar, chiqarib tashlanganlar, JSON / matn, yozish / o'qish |
| `PauseInBackgroundAudioOutput` | Audio chiqishi pauzasi: tovush joyida turadi, keyin davom etadi |
| `DisplayGammaAndBrightness` | Gamma / yorqinlik formulasi, diapazonlar, saqlash, `--gamma` / `--brightness` |
| `DisplayResolutionSettings` | Ekran o'lchamlari ro'yxati, saqlash, ustunlik (buyruq qatori > saqlangan > loyiha) |
| `PlayerBenchmarkStatsAndLimits`, `ExportFolderSelfCheck`, `BuildInfoAndVersionText`, `GameMenuSampleScene` | Ishlash testi, eksport tekshiruvi, versiya, o'yin menyusi ([Runtime.md](Runtime.md#16-testlar)) |

---

## 20. Qisqa API ma'lumotnomasi

```cpp
// Loyiha
Ref<Project> project = Project::GetActive();
ProjectConfig& config = project->GetConfig();
Project::GetPlayerSettings(); Project::GetGraphicsSettings(); Project::GetTimeSettings(); Project::GetPhysicsDefaults();
Project::GetProductName(); Project::GetIconPath(); Project::GetLocalizationDirectory();
Application::GetVersion(); Application::GetProductName(); Application::GetCompanyName();

// Oyna
Display::SetFullscreen(bool); Display::IsFullscreen(); Display::SetVSync(bool); Display::SaveSettings(); Display::LoadSettings();
Display::SetGamma(float); Display::SetBrightness(float); Display::GetAdjustment();   // player tasviri (0.5 ... 2.0 / 0.5 ... 1.5)
Display::GetResolutions(); Display::SetResolution(w, h); Display::GetResolution();    // ekran o'lchami

// Versiya
BuildInfo::Get().Describe(); VersionInfo::ForActiveProject().Format(); VersionInfo::ForActiveProject().FormatShort();

// Eksport (editor)
ExportModuleBuild; BinaryStrip::Strip(file); ExportReport::Read(papka, report); ExportFiles::WriteIconSet(rasm, papka, id);

// Sifat
Quality::GetLevel(); Quality::GetLevelName(); Quality::GetLevelNames(); Quality::SetLevel(int | "High");
Quality::SaveSettings(); Quality::LoadSettings();

// Vaqt
Time::GetDeltaTime(); Time::GetUnscaledDeltaTime(); Time::GetFixedDeltaTime(); Time::GetTime(); Time::GetUnscaledTime();
Time::GetTimeSinceStartup(); Time::GetFrameCount(); Time::SetTimeScale(float); Time::GetTimeScale(); Time::SetPaused(bool);
SetTimersUnscaled(bool);   // ScriptableEntity: taymer / tween'lar haqiqiy vaqtda

// Audio
Audio::GetSpatialDefaults();          // AudioSpatialSettings: MinDistance, MaxDistance, Rolloff, RolloffFactor, ComputeGain(d)
AudioEngine::GetMixerSettings();

// Lokalizatsiya
Localize("key", args...); LocalizePlural("key", count, args...);
Localization::SetLanguage("ru"); Localization::NextLanguage(); Localization::GetLanguage(); Localization::GetLanguages();
Localization::GetLanguageName("ru"); Localization::Has("key"); Localization::GetMissingKeys();
Localization::SaveSettings(); Localization::LoadSettings();
virtual void OnLanguageChanged(const std::string& language);   // ScriptableEntity

// Editor tili (editor kodi)
Tr("Fayl"); EditorText::SetLanguage(EditorLanguage::English); EditorText::Choose("Bekor qilish", "Undo");
```
