# WorldGallery3D

24 ta katak (6×4), har birida o'z ground texture'i va teksturali monument modeli bor streaming demo:
world streaming'ning **asset byudjeti**, **rezidentlik limitlari** va **takrorlanadigan kamera yo'li** shu
yerda ko'rinadi. Loyihada skript moduli yo'q — hammasi ma'lumot: `Worlds/Main.zworld`, `Worlds/Gallery.ztour`,
24 ta katak sahnasi, 32 ta PNG, 8 ta OBJ va 8 ta `.zmat`.

Loyihani editor'da oching va **Play** bosing; **Window → World Streaming** panelida kataklar gridi,
*Asset streaming and residency limits* bo'limi (tayyorlangan fayllar, yuklash navbati, kadr cho'qqisi,
asosiy oqimdagi decode/parse soni, limit hisoblagichlari) va siyosat maydonlari bor.

## Kamera yo'li va hisobot

```bash
ZAMIN_WORLD_TOUR=Worlds/Gallery.ztour ZAMIN_WORLD_REPORT=/tmp/gallery.json \
ZAMIN_MAX_FRAMES=1350 build/bin/ZaminPlayer Projects/WorldGallery3D
```

`Gallery.ztour` — kadr raqami bo'yicha kamera pozitsiyalari (vaqtga bog'liq emas): 240 kadr isinish, so'ng
1080 kadr — uch tomon bo'ylab sekin yurish, har 6 kadrda burchakdan burchakka sakrash (tez kamera, yuklashlar
bekor bo'ladi), bo'sh hudud (hamma katak unload) va boshlang'ich joyga qaytish. Hisobot har kadr uchun
kadr vaqti, streaming va upload ishi (ms), yuborilgan baytlar, RSS, so'ralgan GPU baytlari va faol
kataklar sonini, yakunida p50/p95/p99/max ni beradi. Ikki ishga tushirish bir xil kamera yo'li va bir xil
"tinch" nuqtalardagi faol kataklarni beradi.

## Byudjetlar (`Main.zworld` → `Streaming`)

| Maydon | Qiymat | Ma'nosi |
|---|---|---|
| `AssetStreaming` | `true` | texture decode / model parse — worker'larda, GPU upload — byudjet bilan |
| `MaxAssetUploadBytesPerFrame` | 2 MiB | bir kadrda GPU'ga yuboriladigan baytlar (texture qatorlab yuklanadi) |
| `MaxAssetUploadsPerFrame` | 8 | bir kadrdagi upload chaqiruvlari |
| `MaxAssetReleaseBytesPerFrame` | 16 MiB | kesh tozalashda bir kadrda bo'shatiladigan GPU baytlari |
| `MaxAssetFileBytes` / `MaxAssetBytesPerCell` / `MaxStagedAssetBytes` | 32 / 128 / 256 MiB | fayl, katak va jami tayyorlangan baytlar |
| `EvictOnPressure` | `true` | RSS/GPU limiti yoqilsa, uzoq kataklar chiqariladi (limitlar bu demoda 0 = o'chiq) |

Limitlarni sinash uchun panelda `Observed RSS limit bytes` yoki `Requested GPU limit bytes` ni kiriting,
saqlang va runtime'ni qayta ulang.

## Qayta yaratish

```bash
python3 Tools/make_world_gallery.py Projects/WorldGallery3D                      # shu loyiha (bit-bit bir xil)
python3 Tools/make_world_gallery.py /tmp/heavy --texture-size 2048 --model-detail 3   # og'ir nusxa (testlar)
python3 Tools/make_world_gallery.py /tmp/legacy --legacy                         # AssetStreaming o'chiq
```

Mavjud `.zmeta` fayllar saqlanadi (UUID'lar o'zgarmaydi). Sinovlar: `ctest -R WorldAssetStreaming`
(`Tests/world_asset_streaming_test.py --stage budget|residency|demo`).

Chegaralar: so'ralgan GPU baytlari va kuzatilgan RSS — jismoniy VRAM emas; terrain qatlam texture'lari,
shriftlar va animatsiya kliplari asset byudjetiga kirmaydi (ular avvalgidek yuklanadi).
