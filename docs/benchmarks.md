# Benchmarklar va ularning boshlang'ich natijalari

Dvigatelning tezligini o'lchaydigan dasturlar va o'lchangan **boshlang'ich natija** (baseline): keyingi o'zgarish
tezlikni qanchalik o'zgartirganini shu raqamlar bilan solishtiring. Raqamlar bir xil kompyuter, bir xil build turi va
bir xil sozlamalarda olinganda solishtiriladi.

## Rendering: 10 000 sprite, 1 000 mesh (`ZaminBench --render`)

`Tests/RenderBench.cpp`, ishga tushirish: `Tests/render_bench.sh` (yoki `cmake --build build --target render_bench`).
GL konteksti kerak: displey bo'lmasa qat'iy 1600x900x24 xvfb ekrani, va har doim Mesa'ning dasturiy rasterizatori
(llvmpipe) - raqamlar kompyuterlar orasida va shu jadval bilan solishtirilsin (GPU bilan: `ZAMIN_RENDER_BENCH_GPU=1`).
Sahnalar o'yin (player) kabi chiziladi: asosiy kamera, sahna kadri (`OnUpdateRuntime`) chiziqli HDR nishonga, keyin
post-processing oynaga. Har holat 20 kadr qizdiriladi, keyin 120 kadr o'lchanadi:

| Holat | Nima chiziladi |
|---|---|
| `sprites` | 10 000 sprite (choragi 4 xil teksturadan biri bilan), ortografik kamera |
| `meshes` | 1 000 primitiv (kub, shar, silindr, konus; 8 material) yer ustida, soyali quyosh va nuqtali chiroq, perspektiv kamera; GPU instancing bilan (odatdagidek) |
| `meshes-noinst` | o'sha meshlar instancing'siz (instancing nima tejashini ko'rsatadi) |
| `combined` | spritelar (orqadagi devorda) va meshlar bitta sahnada |

**CPU** - kadr boshidan hamma buyruq yuborilguncha; **Kadr (GPU bilan)** - `glFinish` gacha (GPU ham tugatgan).
`--json` har holat raqamlarini yozadi (`render-bench.json`), `--max-avg <ms>` chegaradan sekin holatda skriptni
yiqitadi, `--shots <papka>` har holatning oxirgi kadrini PNG qilib saqlaydi.

### Boshlang'ich natija (2026-11-08)

Muhit: 4 vCPU Intel Xeon @ 2.10 GHz, Mesa 25.2.8 llvmpipe (LLVM 20.1.2, 256 bit), xvfb 1600x900x24, oyna 1280x720,
GCC 13.3, **Release** build (`-DCMAKE_BUILD_TYPE=Release`), 120 kadr.

| Holat | Obyekt | CPU ms (o'rtacha / mediana / p95) | Kadr ms, GPU bilan (o'rtacha / mediana / p95 / max) | Chizildi |
|---|---:|---|---|---|
| sprites | 10 001 | 18.72 / 18.21 / 21.51 | 24.51 / 23.92 / 27.68 / 31.40 | 3 draw call, 10 000 kvad |
| meshes | 1 004 | 130.30 / 128.23 / 152.16 | 136.54 / 134.36 / 158.17 / 170.86 | 46 draw call, 1 001 mesh (1 000 instans), 649K uchburchak |
| meshes-noinst | 1 004 | 135.24 / 130.99 / 156.46 | 141.68 / 137.29 / 163.78 / 187.51 | 2 027 draw call, 1 001 mesh, 649K uchburchak |
| combined | 11 004 | 138.78 / 135.09 / 169.44 | 145.10 / 141.27 / 176.07 / 183.91 | 47 draw call, 8 500 kvad, 1 001 mesh (1 000 instans) |

Debug build'da (o'sha kompyuter, 20 kadr) taxminan: sprites 75 ms, meshes 150 ms, combined 209 ms - spritelarda
Debug ~3 marta sekin (CPU tomonidagi batching), meshlarda farq kichik (vaqtni rasterizatsiya oladi).

Qanday o'qish kerak:

- **10 000 sprite 3 ta draw call'da** chiziladi (Renderer2D batching: bir batch'da ko'p kvad, teksturalar slotlarda) -
  Release'da kadr ~25 ms dasturiy rasterizator bilan; haqiqiy GPU'da bu bir necha ms.
- **Instancing** draw call'larni 2 027 dan 46 ga tushiradi (soya kaskadlari bilan birga). llvmpipe'da vaqt deyarli bir
  xil (u yerda uchburchaklarni CPU rasterlaydi - 649K uchburchak, soya kaskadlarining chizishlari ham kiradi); haqiqiy
  GPU'da draw call soni CPU vaqtini ko'proq belgilaydi.
- `combined` holatda 1 500 sprite kamera ko'rinishidan tashqarida (frustum culling): 8 500 kvad chizildi.
- Natija mashinaga bog'liq: boshqa kompyuterda avval shu jadvalni o'sha yerda qayta oling, keyin o'zgarishni
  solishtiring. Kichik farqlar (±5-10%) shovqin; p95 va max ayniqsa xvfb ostida tebranadi.

## Boshqa benchmarklar

| Dastur | Nima o'lchaydi |
|---|---|
| `ZaminBench` (`particle_bench` target) | particle tizimlari (~50 000 particle), `--jobs N`, `--profile` |
| `ZaminBench --animators N` | skinned animator'lar pozasi |
| `ZaminBench --queries` | sahna qidiruvlari (nom, teg, komponent indekslari) |
| `ZaminBench --serialize` | katta sahnani (10 000 obyekt) saqlash / yuklash, JSON va binar kesh |
| `ZaminBench --serialize --prefabs` | bog'langan prefab nusxalari (saqlash, yuklash, sinxronlash, Apply) |
| `ZaminBench --nav` | navigatsiya (bake, yo'l qidirish, Nav Agent'lar) |
| `Tests/player_bench.sh` | namuna o'yinlarning boshlang'ich sahnalari ZaminPlayer'da (ms / kadr, p95, p99) |
