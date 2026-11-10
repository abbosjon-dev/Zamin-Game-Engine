# Visual Script: kodsiz gameplay

Zamin Visual Script — engine ichidagi `.zgraph` node muharriri va chegaralangan bajarish tizimi.
2D va 3D loyihalar uchun bir xil ishlaydi. Graph asosidagi loyiha C++ kompilyatori yoki o'yin `.so` modulini talab qilmaydi.
Bu birinchi ishlaydigan versiya; Unreal Blueprints bilan to'liq tenglik da'vo qilinmaydi.

## Boshlash

1. Content Browser bo'sh joyida o'ng tugma → **Yangi visual skript (.zgraph)**. Yoki **Oyna → Visual Script** oynasida `Scripts/NewGraph.zgraph` yo'lini kiriting va **New** bosing.
2. Chapdagi qidiruv orqali node qo'shing. O'ngdagi panelda uning qiymatlarini sozlang.
3. Chiqish pinini, keyin kirish pinini bosing. Faqat bir xil turdagi pinlar ulanadi. Oq pinlar bajarish tartibi; rangli pinlar ma'lumotdir. Bir nechta harakat uchun **Sequence** ishlating.
4. **Save** qiling. Ierarxiyada obyektni tanlang va **Attach saved graph** bosing. Bu sahna o'zgarishi: sahnani ham saqlang.
5. **Play** bosing. **Instance overrides** orqali bitta graph nusxalarining tezligi, rangi va boshqa o'zgaruvchilarini alohida sozlash mumkin; o'yin paytida muharrir tahriri yopiladi.

O'rta sichqon tugmasi — pan; g'ildirak — zoom; **Fit graph** — hamma node'ni ko'rinishga sig'dirish.
Node sarlavhasini tortish — joylashuv; pin ustida o'ng tugma — simlarni uzish.
Tanlangan node **Delete node** orqali o'chadi. Graphning o'z **Undo/Redo** tarixi 64 holat / 32 MB bilan chegaralangan.
Saqlanmagan graph ustiga boshqa fayl ochilmaydi; **Save** yoki **Discard edits** bilan davom etiladi.
Fayl atomik yoziladi; yozish xatosida tahrirlar oynada qoladi.

## Ishlaydigan imkoniyatlar

- Hayot sikli: Create, Enable, Start, Disable, Destroy, Update, LateUpdate.
- Collision/trigger, UI pointer/click/select/input, animatsiya, audio tugashi, AI target/patrol, scene va language hodisalari.
- Number, Bool, String, Vec2/3/4, Entity qiymatlar; qat'iy turli pinlar; graph o'zgaruvchilari va nusxa uchun overrides.
- Branch, Sequence, For/Index/Completed, While, Break, Delay.
- Nomlangan Function/Call: graph o'zgaruvchilaridan foydalanadigan, qayta ishlatiladigan bajarish zanjirlari. Delay ichida ham chaqiruvning davom etish joyi saqlanadi.
- Matematik, mantiqiy va vektor amallari, matn birlashtirish, raqamni matnga aylantirish.
- Input axis/action, transform, entity qidirish/yaratish/o'chirish, active holati va taglar.
- 2D/3D raycast (Hit, Entity, Point, Normal, Distance); navigatsiya destination/stop/arrival.
- Scene yuklash; Number/Bool/String global GameState va uning save/load amallari.
- 2D/3D velocity, force, impulse, 3D torque; prefab, audio, animation, sprite rang, UI matni, xabarlar.
- `ReadField.<Type>` / `WriteField.<Type>` boshqa obyektning ro'yxatdan o'tgan C++ maydonlari bilan ishlaydi. Number Float/Int ga moslanadi; Int kasr va overflow'ni rad etadi; maydonning Range cheklovi saqlanadi.

`Update.Delta` — soniyalardagi vaqt; aylanish qiymatlari radianlarda. `Translate` hamda `SetPosition` fizik jismlar bilan ham sinxronlanadi.
SetScale render transformini o'zgartiradi; runtime collider geometriyasini qayta yasamaydi.
`Target` bo'sh turganda **Self** ishlatiladi. **None** yoki qidiruvdan qaytgan nol UUID o'z obyektiga aylantirilmaydi: noto'g'ri target diagnostika beradi.
Entity qiymatiga ierarxiyadan obyekt tortib tashlash mumkin.

## Bajarish qoidalari va debug

Ma'lumot pinlari kerak bo'lgan paytda hisoblanadi: loop ichidagi Get yangilangan o'zgaruvchini ko'radi.
Sequence shoxlari tartib bilan bajariladi; shox ichidagi Delay tugagach keyingisi boshlanadi.
For Count butun 0..100000 qiymat bo'lishi kerak. While sharti har aylanishda qayta hisoblanadi.
Break ichki loopni tugatadi va uning Completed chiqishiga o'tadi.
Delay kutayotgan hodisaning ma'lumotlari va bajarish stekini saqlaydi. Entity/script nofaol bo'lsa kutish ham to'xtaydi.
Bir hodisani qayta-qayta boshlash alohida bajarish vazifalarini yaratadi; har frame uzun Delay boshlash o'rniga buni Start yoki tegishli input hodisasidan boshlang.

Play paytida tanlangan obyektning graph oynasida **Enable breakpoints**, **Pause**, **Continue**, **Step**, o'zgaruvchilar va oxirgi 128 node izi mavjud.
Breakpoint graph nusxasini to'xtatadi; boshqa obyektlar va o'yin simulyatsiyasi davom etadi. O'yinning umumiy Pause boshqaruvi alohida.
Player'da breakpoints avtomatik o'chirilgan; editor debug belgisi buildga majburiy to'xtash kiritmaydi.

Bajarish 10000 ko'rsatma bilan chegaralanadi; cheksiz loop o'rniga node raqami bilan xato chiqadi.
Ma'lumot chuqurligi 128, bajarish steki 1024, bir vaqtdagi vazifalar 128, o'zaro graph chaqiruv chuqurligi 32 bilan cheklangan.
Graph 4096 node / 8192 sim / 256 o'zgaruvchi / 4 MB faylgacha; matn qiymati 64 KB gacha.
Noma'lum node, yo'qolgan pin, tur xatosi, ikki manbali ma'lumot kirishi, takrorlangan ID, data cycle va nolga bo'lish rad etiladi.
Xato shu graph nusxasini to'xtatadi va konsol/debug panelga chiqadi.

## Saqlash, C++ va eksport

Graph engine'ning mavjud `NativeScriptComponent` lifecycle'ida ichki `Zamin.VisualGraph` hosti orqali ishlaydi.
Shuning uchun bitta obyektning bitta skript slotiga C++ yoki visual graph ulanadi. C++ skript bor obyektga ulash tugmasi almashtirishni aniq ko'rsatadi.
Ikkisini birlashtirish uchun alohida controller obyektlari, xabarlar va aks ettirilgan maydonlardan foydalaning.
C++ metodlari avtomatik nodega aylanmaydi: faqat katalogdagi API va ro'yxatga olingan maydonlar mavjud.
Bu versiyada Curve/Enum/array/struct pinlari, parametrli/return qiymatli funksiyalar, macro/collapse, inheritance, RPC va bytecode/native code generation yo'q.

Graph assets registryda ro'yxatlanadi, Content Browser'da ochiladi, asset inspector diagnostika ko'rsatadi.
Graph ichidagi asset yo'llari reference scan va rename bilan yuritiladi. Graph o'zgaruvchisini override qiladigan asset yo'lini
ichki JSON matniga yashirish o'rniga graph node'ining Path qiymatida saqlash tavsiya qilinadi: nested override matni reference scanner tomonidan tahlil qilinmaydi.
Play'dan chiqqach graph tahriri qayta ochiladi. Saqlangan graph keyingi Play'da yuklanadi; Play ichida faylni jonli almashtirish yo'q.
C++ hot reload graph o'zgaruvchilarini saqlaydi, lekin boshqa native taymerlar kabi kutayotgan bajarishlarni qayta boshlaydi.

## Namunalar va tekshiruv

[VisualWorkshop](../Projects/VisualWorkshop/VisualWorkshop.zproject) C++ modulisiz ikkita sahnaga ega:

- `Visual2D`: WASD/arrow input → 2D velocity; triggerga tegish rangni vaqtincha o'zgartiradi.
- `Visual3D`: ikkita kub bir graph bilan, turli Speed override'lari orqali teskari tezliklarda aylanadi.

`ZaminTests --filter VisualGraph` VM, tur/format/cycle tekshiruvi, delay/loop/function davom etishi, budget, debug, izolyatsiya,
C++ maydon ko'prigi, haqiqiy sahna lifecycle va null target himoyasini bajaradi.

Haqiqiy ImGui sichqon regressiyasi (node qo'shish, typed sim, rad etish, save/undo/redo):

```bash
ZAMIN_BUILD_DIR=build-review LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=2 \
  xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' python3 Tests/visual_graph_editor_test.py
```

Sinov `xdotool` va ImageMagick `import` vositasini talab qiladi; asl namunani o'zgartirmaydi.

2026-11-10 tekshiruvi: 173 node ta'rifi; 26 ta VisualGraph sinovi (boshqa yuklangan sahna obyektiga murojaat ham kiradi),
682 umumiy unit test, 12 render holati va ikki loyiha bo'yicha 223 + 265 editor qadami xatosiz o'tdi.
Haqiqiy X11/ImGui amallarida node qo'shish, save, undo/redo, typed sim va noto'g'ri simni rad etish tekshirildi.
UI'siz Player'da 3D aylanish, 2D fizik trigger → graph → GameState → disk save zanjiri ishladi.
Bu sinovlar Linux/Mesa dasturiy rendering muhitida bajarildi; boshqa platformalar yoki fizik GPU sertifikati emas.
