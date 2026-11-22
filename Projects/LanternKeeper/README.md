# Chiroqbon

Ko'hna qo'rg'on afsonasi: Soya Bek Tong chiroqlarini o'g'irladi va qo'rg'on abadiy shom ichida qoldi. Yosh chiroqbon
Nilufar bog'lardagi qaroqchilardan birinchi chiroqni, qo'rg'on ostidagi yertoladan qolgan ikkitasini qaytarib oladi,
Soya Bekni yengadi va mehrobda Tong chirog'ini yoqadi.

Zamin Engine'ning kompilyatorsiz 2D vertical slice'i: bosh menyu, ikki daraja (Bog'lar, Yertola), qahramon motori,
zarba va sehrlar (olov o'qi, yorug' to'lqin, tiklanish, ichimlik), AI dushmanlar (qaroqchilar, kamonchilar, skeletlar,
ruhlar, boss), vazifalar kitobi (tajriba, darajalar, ochiladigan qobiliyatlar), jihozlar va inventar, HUD, pauza,
daraja yakuni, g'alaba va mag'lubiyat ekranlari, musiqa va ovozlar. Hammasi `Tools/samples/gen_lantern_keeper.py`
generatoridan.

| Klaviatura | Gamepad | Harakat |
|---|---|---|
| WASD / strelkalar | chap stik | Yurish (Shift / LT — tezroq) |
| J / LKM | X | Zarba |
| K / PKM | RB | Olov o'qi (Bolt, yondiradi) |
| L | Y | Yorug' to'lqin (Nova, 2-darajadan) |
| H | LB | Tiklanish (Mend, 3-darajadan) |
| Q | D-pad past | Ichimlik |
| E | A | Ishlatish (sandiq, ziyoratgoh, darvoza, mehrob) |
| I / Tab | Back | Inventar (Kiyish, Ichish) |
| Esc / P | Start | Pauza (Davom etish, Inventar, Saqlab bosh menyuga) |

Ziyoratgohlar o'yinni saqlaydi va sog'liqni tiklaydi; uch marta yiqilsangiz oxirgi saqlashdan davom etasiz.
Birinchi darajadan ikkinchisiga qahramon holati (sog'liq, sumka, jihozlar, vazifalar, tajriba) ko'chirish hujjati
bilan o'tadi. Demo: `ZAMIN_DEMO=1 ./build/bin/ZaminPlayer Projects/LanternKeeper` — o'yinni bot o'ynaydi.
Batafsil: [docs/VerticalSlices.md](../../docs/VerticalSlices.md).
