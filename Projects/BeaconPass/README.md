# Dovon mash'ali

Karvon yo'li afsonasi: bu kecha dovondan karvon o'tadi, lekin zo'ravon Qora qorovul tog' boshidagi mash'alni o'chirib
qo'ydi. Chopar Temur vodiydagi qarorgohdan karvonsaroy orqali minoraga ko'tariladi, uchta olov toshini yig'adi, Qora
qorovulni yengadi va qorong'i tushmasdan mash'alni qayta yoqadi.

Zamin Engine'ning kompilyatorsiz 3D vertical slice'i: bosh menyu, bitta to'liq daraja (Dovon: qarorgoh, karvonsaroy,
otxona, minora oldidagi maydon), uchinchi shaxs qahramoni (character controller + orbit kamera), skinned odam modeli va
Animator lokomotsiyasi (turish/yurish/yugurish blend'i, sakrash va qo'nish, yuqori tana qatlamida zarba va sehr, zarba
olganda additiv qatlam, o'lim), animatsiya hodisalariga bog'langan zarba va olov o'qi, Zilzila (yerga urish), AI
qorovullar, kamonchilar va boss, to'rt vazifali kitob (tajriba, darajalar), jihozlar, ziyoratgohlarda saqlash, g'alaba
va mag'lubiyat ekranlari, musiqa va ovozlar. Hammasi `Tools/samples/gen_beacon_pass.py` generatoridan.

| Klaviatura / sichqoncha | Gamepad | Harakat |
|---|---|---|
| WASD / strelkalar | chap stik | Yugurish (chap Shift ushlab — sekin yurish) |
| Sichqoncha | o'ng stik | Kamera |
| Space | A | Sakrash |
| LKM / J | X | Zarba (Shamshir bilan kuchliroq) |
| PKM / K | RB | Olov o'qi (Bolt, yondiradi) |
| Q | Y | Zilzila (2-darajadan, atrofdagilarni urib, gangitadi) |
| R | D-pad past | Ichimlik |
| E | B | Ishlatish (sandiq, darvoza, ziyoratgoh, mash'al) |
| I / Tab | Back | Inventar |
| Esc / P | Start | Pauza (Davom etish, Saqlab bosh menyuga) |

Vazifalar: **Dovon yo'li** (darvoza qorovullarini yengib, karvonsaroy darvozasini ochish), **Olov toshlari** (uchta tosh),
**Qora qorovul** (boss), **Mash'al** (minora tepasidagi mash'alni yoqish) — jami 800 tajriba, 4-daraja.
Ziyoratgohlar o'yinni saqlaydi va sog'liqni tiklaydi; uch marta yiqilsangiz mag'lubiyat ekranidan oxirgi saqlashdan
davom etasiz. Demo: `ZAMIN_DEMO=1 ./build/bin/ZaminPlayer Projects/BeaconPass` — o'yinni bot o'ynaydi.
Batafsil: [docs/VerticalSlices.md](../../docs/VerticalSlices.md).
