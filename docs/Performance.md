# Ishlash tezligi: bottleneck hisoboti

M25 (vazifalar 481–500) yakunidagi holat: vaqt qayerga ketadi, nima tuzatildi va nima qoldi. Hamma raqamlar
o'lchangan (profiler yozuvlari, benchmarklar); oldin / keyin jadvallari va ularni qanday qaytarish —
[Benchmarks.md](Benchmarks.md), namunalar jadvali — [Samples.md](Samples.md#ishlash-tezligi).

**Muhit:** 4 vCPU Intel Xeon @ 2.10 GHz, GPU yo'q — Mesa 25.2.8 **llvmpipe** (LLVM 20.1.2, dasturiy rasterizator),
xvfb, GCC 13.3, **Release** build, oyna 1280x720. llvmpipe'da "GPU" ishi ham shu to'rt yadroda bajariladi: piksel va
vertex shader'lar, soya xaritalari, post-processing. Shuning uchun quyida ikki xil vaqt alohida ko'rsatiladi —
dvigatelning o'z CPU ishi (o'yin kodi, sahna, chizish buyruqlarini yuborish) va rasterizatsiya (haqiqiy GPU'da u
videokartada bo'lardi).

## 1. Qanday o'lchandi

| Manba | Nimani beradi |
|---|---|
| Profiler (`ZM_PROFILE_SCOPE`, `Profiler::FormatTree`) | kadrning daraxti: har qism vaqti, o'z vaqti, chaqiruvlar soni; editorda Profiler oynasi |
| `ZAMIN_PLAYER_STATS=1` | player chiqishda: oxirgi kadrning profili, render statistikasi, sahna, xotira |
| `ZaminPlayer --benchmark` | kadr vaqtlari (o'rtacha, p95, p99) va **eng sekin kadrning profili** |
| `ZAMIN_STARTUP_PROFILE=1` | ishga tushish daraxti (oyna, renderer, audio, loyiha, sahna) va birinchi kadrning daraxti |
| `ZAMIN_EDITOR_PANEL_TIMES=1`, `ZAMIN_EDITOR_STATS=1` | editor panellarining vaqti, editor xotirasi |
| `Tests/samples_bench.py` | to'qqiz namuna bot o'ynayotganda (yuqoridagilarni yig'adi) |
| `Tests/startup_bench.py` | ishga tushish vaqti, ikki build solishtirish |
| `ZaminBench` (`--render`, `--ecs`, `--serialize`, `--fonts`, `--animators`, `--nav`) | ajratilgan holatlar: 10 000 sprite, 1 000 mesh, ECS, serializatsiya, shriftlar ... |

## 2. Eng og'ir joylar (kadr)

Namunalarning oxirgi kadrlari profilidan (1280x720, 300 kadr o'rtachasi; `Tests/samples_bench.py`, 2026-11-09):

| Namuna | Kadr | Post-processing (SSAO / Bloom) | Sahnani chizish | O'yin kodi (CPU) |
|---|---:|---|---:|---:|
| Adventure3D | 152.9 ms | 125.7 ms (109.0 / 12.9) | 18.1 ms | 0.50 ms |
| EndlessRunner | 145.2 ms | 86.8 ms (62.5 / 11.8) | 38.2 ms | 0.11 ms |
| FPSArena | 131.6 ms | 99.2 ms (73.7 / 12.5) | 21.2 ms | 0.40 ms |
| Sokoban | 131.5 ms | 93.8 ms (69.4 / 12.2) | 12.9 ms | 0.08 ms |
| Racing | 109.4 ms | 73.3 ms (47.8 / 12.6) | 25.5 ms | 0.16 ms |
| Showcase3D | 108.4 ms | 80.3 ms (62.9 / 13.2) | 13.6 ms | 0.39 ms |
| TopDownShooter (2D) | 62.4 ms | 33.0 ms (- / 29.4) | 1.0 ms | 0.23 ms |
| Breakout (2D) | 48.9 ms | 27.7 ms (- / 24.0) | 1.4 ms | 0.18 ms |
| Platformer2D (2D) | 12.5 ms | 4.1 ms | 0.2 ms | 0.04 ms |

1. **Piksellarni bo'yash (llvmpipe) — kadrning 80–95 %.** llvmpipe chizishni kechiktiradi va rasm birinchi
   o'qilganda bajaradi: 3D sahnaning PBR yoritilishi, soyalari va shaffof qismlari "SSAO" ichida (u chuqurlikni
   birinchi o'qiydi), 2D sahnaniki (har piksel uchun 20+ ta 2D chiroq) "Bloom" ichida ko'rinadi.
2. **Sahnani chizish 13–38 ms** — shundan dvigatelning o'z ishi kam: chizish buyruqlarining CPU vaqti (instancing,
   batching, culling, uniform buferlar) ZaminBench'da o'lchangan; qolgani — llvmpipe'ning vertex ishi va soya
   xaritalari (2048 px, kaskadlar — oyna o'lchamiga bog'liq emas). Eng ko'pi Ipak Yo'li (EndlessRunner):
   155 284 uchburchak.
3. **O'yin kodi** (skriptlar, fizika, particle'lar, animatsiya, audio, AI) hamma namunada 0.04–0.55 ms.

**CPU vaqti (vazifa 497).** Rasterizatsiyani deyarli olib tashlash uchun namunalar 284x160 oynada o'lchandi (3D
namunalar `--quality Low` bilan ham: SSAO yo'q, soya xaritalari kichik): qolgan kadr vaqti — dvigatelning CPU ishi
va llvmpipe'ning vertex ishi birga, ya'ni dvigatel ishining yuqori chegarasi. 2D: 1.9–9.3 ms, 3D: 8.5–12.7 ms —
16.6 ms (60 FPS) ichida; EndlessRunner 26.6 ms edi (155K uchburchakning vertex ishi) - primitivlar LOD'i bilan
15.1-15.5 ms (67K uchburchak; p95 hali 19-20 ms). Jadval:
[Samples.md](Samples.md#m25-tekshiruvi-2026-11-09).

**Eng sekin kadrlar.** TopDownShooter'da birinchi o'yinda bitta ~0.8 s lik kadr bor: katta matn (to'lqin banneri)
birinchi chizilganda shu o'lchamning shrift atlasi rasterlanadi (~600 glif). Shrift keshi (485) uni diskka yozadi:
ikkinchi ishga tushirishda eng sekin kadr 866 -> 148 ms. `samples_bench` har o'yinni yangi HOME bilan
ishga tushiradi, shuning uchun u har safar ko'rinadi.

## 3. Eng og'ir joylar (ishga tushish)

`ZAMIN_STARTUP_PROFILE=1`, Release, Showcase3D (oxirgi o'lchov):

| Qism | Editor | Player |
|---|---:|---:|
| Oyna va GL konteksti | 52–68 ms | 55–68 ms |
| `Renderer::Init` (shundan `Renderer3D::Init`: 25 ta shader dasturi binary keshdan) | 97–122 ms (63) | 97–110 ms (63–70) |
| Audio | 9–10 ms | 9 ms |
| Loyiha + boshlang'ich sahna (assetlar: mesh / material, shriftlar) | 64–90 ms | 50–60 ms |
| Birinchi kadr (shundan SSAO — drayver shader variantlarini birinchi chizishda kompilyatsiya qiladi) | 380–420 ms (84–107) | 220–276 ms (80–90) |

## 4. M25 da nima tuzatildi

| Vazifa | Joy | Oldin -> keyin |
|---|---|---|
| 481 | 2D batching (`BatchOrder`, kamroq flush) | `mixed2d`: 670 -> 65 draw call, CPU 16.0 -> 10.1 ms |
| 482 | kadrlararo ajratmalar | 10k sprite: 10 023 -> 2, 1k mesh: 1 504 -> 6 ajratma / kadr |
| 483, 484 | dunyo transformi keshi, komponent pool'lari keshi | `GetWorldTransform` 221 -> 33 ns; pool o'qish 6.6 -> 1.7 ns |
| 485 | shrift atlaslari diskda, glif jadvali, kerning (ixtiyoriy) | atlas 45 ms rasterlash -> 2.4 ms o'qish |
| 486, 487 | tekstura va modellarni fonda yuklash | Showcase3D: bitta kadrda 103.8 ms -> 9 kadrda eng uzuni 6.9 ms |
| 488 | shader program binary keshi | shader'lar 253 -> 74–91 ms (Mesa keshi o'chiq); 72 -> 64 ms (yoqiq) |
| 489 | kadr uniform buferi (std140) | ~150–200 `glUniform` -> 1 bufer yuklash (llvmpipe'da tezlik sezilmaydi) |
| 490 | soya o'tishida kesish | 1 000 mesh: soya chizish 1 024 -> 323, CPU 156.5 -> 126.0 ms |
| 491, 492 | parallel particle va animatsiya | 49K particle: 12.2 -> 4.0 ms; animator ko'rinishi 0.196 -> 0.114 ms |
| 493, 494 | editor: `ImGuiListClipper` | 10 000 obyekt + 5 000 fayl: editor kadri 42.7 -> 24.7 ms |
| 495 | ishga tushish | editor (Showcase3D) 1 094 -> 750 ms; ImGui atlasi 236 -> 60–70 ms; Sahnalar paneli 71 -> 25 ms |
| 496 | xotira | editor RAM 451 -> 325 MB (Showcase3D), 367 -> 242 MB (Platformer2D) |

Hamma optimallashtirishda rasm o'zgarmadi (`ZaminRenderTests` 12/12), fizika determinizmi testlari o'tadi, parallel
yo'llar bitta oqimli natija bilan bit-bitgacha solishtiriladi.

## 5. Qolgan cheklovlar

1. **Rasterizatsiya (GPU yo'q).** llvmpipe'da 3D namunalar 7–10 FPS, 2D'lar 16–80 FPS. Eng qimmati SSAO o'tishi
   (ichida sahnaning o'zi bo'yaladi). Yechimlar: haqiqiy GPU; SSAO ni yarim o'lchamda hisoblash; sifat darajasi
   Low (SSAO o'chiq) — 3D namunalar 284x160 da 100+ FPS.
2. **EndlessRunner geometriyasi.** 155K uchburchak: sifat Low va kichik oynada ham 22.7-26.6 ms edi. Tuzatildi:
   ekranda kichik shar / silindr / konus segmentlari yarmi yoki choragi bo'lgan nusxasini chizadi
   (`MeshLibrary::GetPrimitiveLOD`) - 67K uchburchak, o'rtacha 15.1-15.5 ms. Qolgani: p95 19-20 ms (bo'laklar
   paydo bo'lgan kadrlar); uzoqdagi bo'laklarni masofa bo'yicha kesish keyingi qadam.
3. **Yangi o'lchamdagi shrift atlasi.** Birinchi ishga tushirishda katta matnning birinchi chizilishi ~0.8 s
   (keyingi safarlar diskdagi keshdan). Yechim: o'lchamli atlaslarni ishchi oqimda tayyorlash yoki glif to'plamini
   matnga qarab cheklash.
4. **Ishga tushish (player ~0.35–0.55 s).** GL konteksti, shader dasturlari (binary keshdan ham llvmpipe kodni qayta
   yaratadi) va birinchi kadrdagi drayver kompilyatsiyasi — dvigatel tashqarisida. Haqiqiy GPU drayverlarining o'z
   shader keshi bor. ImGui atlasi hali har safar rasterlanadi (~60 ms) — diskda keshlanishi mumkin.
5. **Vaqtinchalik xotira.** Player'larda birinchi kadr paytida ~80 MB vaqtinchalik o'sish (ikkinchi kadrda
   qaytadi), manbai aniqlanmadi.
6. **ECS.** Har kadrda 500 obyekt yaratib / o'chiradigan holat hali 1 518 ajratma qiladi (`ZaminBench --ecs`).

## 6. O'zingiz o'lchash

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DZAMIN_GAME_MODULE_OUTPUT_DIR=$PWD/build-release/GameModules
cmake --build build-release
ZAMIN_BUILD_DIR=build-release python3 Tests/samples_bench.py               # namunalar (kadr, profil, o'yin kodi)
ZAMIN_BUILD_DIR=build-release python3 Tests/samples_bench.py --resolution 284x160   # rasterizatsiyasiz (CPU)
ZAMIN_BUILD_DIR=build-release python3 Tests/startup_bench.py               # ishga tushish vaqti
ZAMIN_STARTUP_PROFILE=1 ZAMIN_MAX_FRAMES=2 build-release/bin/ZaminPlayer Projects/Showcase3D   # ishga tushish profili
```
