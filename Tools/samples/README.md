# Namuna o'yinlarning generatorlari (`Tools/samples`)

M24 dagi oltita namuna o'yinda (`Projects/TopDownShooter`, `Breakout`, `FPSArena`, `Sokoban`, `EndlessRunner`,
`Racing`) qo'lda chizilgan rasm ham, yozib olingan ovoz ham yo'q. Teksturalar, sprite atlaslar, normal map'lar,
`.obj` mesh'lar, WAV ovozlar, sahna (`.zscene`), `.zmeta` fayllar, `.zproject` va modulning `CMakeLists.txt` fayli
shu papkadagi Python skriptlari bilan yaratiladi. Repozitoriyda retsept (skript) va undan chiqqan kichik fayllar
saqlanadi. C++ skriptlar (`Projects/<O'yin>/Source`) esa qo'lda yoziladi va generatorlar ularga tegmaydi.

| Fayl | Nima qiladi |
|---|---|
| `samplekit.py` | Umumiy vositalar: PNG va WAV yozish (`zlib`, `wave` modullari), barqaror UUID'li `.zmeta`, signed-distance shakllar (yumshoq chet, nur sochish), tikiladigan qiymat shovqini, balandlikdan normal map; ovoz uchun oscillator'lar, ADSR, shovqin, filtrlar, notalar; `SceneBuilder` (engine o'qiydigan `.zscene` JSON) va komponent yordamchilari; `write_project` |
| `gen_topdownshooter.py` | Neon Maydon: neon sprite atlas, pol va kosmos foni, 15 ta ovoz, sahna |
| `gen_breakout.py` | Zamin Breakout: g'isht va bonus atlasi, maydon va fon, 15 ta ovoz, sahna |
| `gen_fpsarena.py` | FPS Arena: beton, metall va quti teksturalari normal map'lari bilan, 22 ta ovoz, arena sahnasi |
| `gen_sokoban.py` | Omborchi: g'isht, quti, pol teksturalari, 10 ta ovoz, sahna; `Levels.cpp` dagi har bir darajani yechib ko'radi |
| `gen_endlessrunner.py` | Ipak Yo'li: yo'l, g'isht, qum, koshin, gilam teksturalari, 11 ta ovoz, sahna |
| `gen_racing.py` | Vodiy Poygasi: Catmull-Rom trassa va undan yasalgan `road.obj`, `kerbs.obj`, `barriers.obj`, teksturalar, 10 ta ovoz, sahna |
| `make_thumbnails.py` | Launcher rasmlari (`Projects/*/Thumbnail.jpg`): o'yinlarning headless skrinshotlaridan |

## Qayta yaratish

```bash
python3 Tools/samples/gen_racing.py            # bitta o'yin (numpy kerak)
for g in topdownshooter breakout fpsarena sokoban endlessrunner racing; do python3 Tools/samples/gen_$g.py; done
ZAMIN_SAMPLES_OUT=/tmp/out python3 Tools/samples/gen_breakout.py   # repozitoriyga emas, /tmp/out/Breakout ga
python3 Tools/samples/make_thumbnails.py Racing                     # launcher rasmi (build, xvfb-run, Pillow kerak)
```

Generatorlar deterministik: tasodifiy sonlarning urug'i qat'iy, UUID'lar asset yo'lidan hosil qilinadi. Shuning
uchun bir xil skript har safar baytma-bayt bir xil fayllarni yozadi. Sahnalar engine'ning JSON formatida, faqat
raqamlar ro'yxati bir qatorda yoziladi (`dumps_compact`). Natijada fayl ~20% kichikroq bo'ladi, engine uni oddiy
JSON kabi o'qiydi.

## Ovozlar qanday yasaladi

Har bir ovoz numpy massivida bir necha qatlamdan quriladi:

- oscillator (sinus, kvadrat, arra, uchburchak) tovush balandligi o'zgarib turadi (masalan, otish 1500 Hz dan
  420 Hz gacha tushadi);
- shovqin past yoki yuqori chastota filtridan o'tkaziladi (portlashda filtr chastotasi 5 kHz dan 180 Hz gacha
  pasayadi);
- konvert: tez ko'tarilish va eksponensial so'nish;
- musiqa notalardan yig'iladi: baraban, xet, bas va kuy takti bo'yicha joylanadi, oxiri boshiga qo'shiladi, shuning
  uchun takrorlanganda chok sezilmaydi.

`write_wav` ovozni 22 050 Hz, mono, 16-bit qilib yozadi, cho'qqisini 0.89 ga normallashtiradi va boshi-oxiriga 3 ms
so'nish qo'yadi (chertish eshitilmaydi). Effekt ovozlar 0.05–1.5 s, musiqa (FPS Arena'da muhit shovqini) 15–17 s
davom etadi.

## Budjet va tekshiruv

`Tests/samples_assets.py` (ctest: `SampleAssets`) har o'yin uchun quyidagilarni tekshiradi:

- `Assets/` hajmi 2 MiB dan oshmaydi (hozir 1.2–1.9 MiB);
- tekstura 512 KB va 2048 px dan, ovoz 800 KB va 20 s dan, sahna 700 KB dan katta emas;
- har asset `.zmeta` ga ega;
- WAV fayllar jim emas va kesilmagan;
- sahnalar va skriptlar nomlagan har bir fayl mavjud, har bir ovoz va tekstura biror joyda ishlatiladi.

`--regen` varianti generatorlarni vaqtinchalik papkaga ishga tushiradi va natijani repozitoriydagi fayllar bilan
solishtiradi. Generatorni o'zgartirsangiz, uni qayta ishga tushiring va yangi fayllarni ham commit qiling.
