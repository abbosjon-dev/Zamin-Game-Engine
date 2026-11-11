# Visual Script: kodsiz gameplay

Zamin Visual Script — engine ichidagi `.zgraph` node muharriri va chegaralangan bajarish tizimi.
2D va 3D loyihalar uchun bir xil ishlaydi. Graph asosidagi loyiha C++ kompilyatori yoki o'yin `.so` modulini talab qilmaydi.
Gameplay graph authoring va bajarish bo‘limi typed funksiyalar, kolleksiyalar, debug va compiler-free eksport bilan ishlaydi. Unreal Blueprints bilan to‘liq tenglik da’vo qilinmaydi.

## Boshlash

1. Content Browser bo'sh joyida o'ng tugma → **Yangi visual skript (.zgraph)**. Yoki **Oyna → Visual Script** oynasida `Scripts/NewGraph.zgraph` yo'lini kiriting va **New** bosing.
2. Chapdagi qidiruv orqali node qo'shing. O'ngdagi panelda uning qiymatlarini sozlang.
3. Chiqish pinini, keyin kirish pinini bosing. Faqat bir xil turdagi pinlar ulanadi. Oq pinlar bajarish tartibi; rangli pinlar ma'lumotdir. Bir nechta harakat uchun **Sequence** ishlating.
4. **Save** qiling. Ierarxiyada obyektni tanlang va **Attach saved graph** bosing. Bu sahna o'zgarishi: sahnani ham saqlang.
5. **Play** bosing. **Instance overrides** orqali bitta graph nusxalarining tezligi, rangi va boshqa o'zgaruvchilarini alohida sozlash mumkin; o'yin paytida muharrir tahriri yopiladi.

O'rta sichqon tugmasi — pan; g'ildirak — zoom; **Fit graph** — hamma node'ni ko'rinishga sig'dirish.
Node sarlavhasini tortish — joylashuv; pin ustida o'ng tugma — simlarni uzish.
Ctrl-click — ko‘p node tanlash; Ctrl+A — hammasi; Ctrl+C/V — nusxa olish/qo‘yish; Ctrl+D — ko‘paytirish; Delete — tanlanganlarni o‘chirish. Guruh sarlavhasini tortish tanlanganlarni birga ko‘chiradi. Copy/Paste/Duplicate tugmalari ham mavjud. Clipboard shu editor sessiyasida saqlanadi; ichki simlar va zarur signaturalar ko‘chadi. Nom to‘qnashsa yangi nom beriladi, yaroqsiz paste graphni o‘zgartirmaydi. Tanlangan node **Delete node** orqali ham o‘chadi. Graphning o'z **Undo/Redo** tarixi 64 holat / 32 MB bilan chegaralangan.
Saqlanmagan graph ustiga boshqa fayl ochilmaydi; **Save** yoki **Discard edits** bilan davom etiladi.
Fayl atomik yoziladi; yozish xatosida tahrirlar oynada qoladi.

## Ishlaydigan imkoniyatlar

- Hayot sikli: Create, Enable, Start, Disable, Destroy, Update, LateUpdate.
- Collision/trigger, UI pointer/click/select/input, animatsiya, audio tugashi, AI target/patrol, scene va language hodisalari.
- Number, Bool, String, Vec2/3/4, Entity qiymatlar; qat'iy turli pinlar; graph o'zgaruvchilari va nusxa uchun overrides.
- Branch, Sequence, For/Index/Completed, While, Break, Delay.
- Typed Function.Entry/Call/Return: parametrlar, bir nechta natija, har chaqiruvga alohida lokal o‘zgaruvchilar, nested/delayed chaqiruvlar. Eski Function/Call graphlari ham ishlaydi.
- Bool/Number/String/Vec2/Vec3/Vec4/Entity massivlari: Empty, Append, Set, Remove, Get, Length, Contains, Find, Slice, Concat va snapshot ForEach.
- Event.Custom + EmitEvent: graph nusxasi ichida nomlangan hodisa, Number/Text/Bool/Vector3/Other payload. Boshqa obyektga xabar uchun SendMessage ishlatiladi.
- Matematik, mantiqiy va vektor amallari, matn birlashtirish, raqamni matnga aylantirish.
- Input axis/action, transform, entity qidirish/yaratish/o'chirish, active holati va taglar.
- 2D/3D raycast (Hit, Entity, Point, Normal, Distance); navigatsiya destination/stop/arrival.
- Animator typed parametr/damping, state/transition, 1D/2D blend og‘irligi, root motion va layer boshqaruvi ([animatsiya qo‘llanmasi](Animation.md#73-modelsiz-2d-holatlar-va-blueprint)).
- Scene yuklash; Number/Bool/String global GameState va uning save/load amallari.
- 2D/3D velocity, force, impulse, 3D torque; prefab, audio, animation, sprite rang, UI matni, xabarlar.
- `ReadField.<Type>` / `WriteField.<Type>` boshqa obyektning ro'yxatdan o'tgan C++ maydonlari bilan ishlaydi. Number Float/Int ga moslanadi; Int kasr va overflow'ni rad etadi; maydonning Range cheklovi saqlanadi.

`Update.Delta` — soniyalardagi vaqt; aylanish qiymatlari radianlarda. `Translate` hamda `SetPosition` fizik jismlar bilan ham sinxronlanadi.
SetScale render transformini o'zgartiradi; runtime collider geometriyasini qayta yasamaydi.
`Target` bo'sh turganda **Self** ishlatiladi. **None** yoki qidiruvdan qaytgan nol UUID o'z obyektiga aylantirilmaydi: noto'g'ri target diagnostika beradi.
Entity qiymatiga ierarxiyadan obyekt tortib tashlash mumkin.

## Funksiya va kolleksiyalarni yozish

Properties ichidagi **Typed functions → Function name → Create function** typed kirish/qaytish juftligini yaratadi.
Funksiya daraxtida Inputs/Outputs/Locals turini, nomini va boshlang‘ich qiymatini tahrirlang; **Add call node** bilan chaqiring.
Entry parametr chiqishlarini tana amallariga, natijani Return kirishiga ulang. Return faqat o‘z funksiyasida ishlaydi.
Natijali funksiya bajarilish yo‘li Return’siz tugasa, VM aniq xato bilan to‘xtaydi; natijasiz funksiya erkin tugashi mumkin.
Local.Get/Set faqat shu chaqiruvning qiymatiga tegadi; rekursiya va parallel Delay vazifalari ularni ulashmaydi.
Funksiya/graph o‘zgaruvchisini Rename qilish tegishli node nomlarini ham yangilaydi. Pin nomi/turi o‘zgarsa
mos kelmay qolgan simlar/literallar olib tashlanadi; Undo ularni tiklaydi. Event ma’lumotini funksiyaga argument orqali bering.

Massiv literalida Add/Remove, 16 elementli sahifalar va **Paste array JSON** bor. `Array.Get/Set/Remove` indeksi
0 dan boshlanadi; kasr/manfiy/chegaradan tashqari indeks bajarilish xatosidir. Slice Start/Count butun va chegarada bo‘lishi kerak.
Find topilmasa -1 qaytaradi. ForEach kirish massivining nusxasi ustida ishlaydi: tana ichida global massivni o‘zgartirish
joriy aylanish tarkibini o‘zgartirmaydi. Body’dan qaytgach keyingi element, so‘ng Completed ishlaydi; Break eng ichki loopni tugatadi.
Break typed funksiya chegarasidan tashqaridagi caller loopga o‘ta olmaydi.
EmitEvent joriy zanjirdan keyin navbatdagi VM bajarish davrida yetkaziladi; rekursiv synchronous event stack yaratmaydi.

## Bajarish qoidalari va debug

Ma'lumot pinlari kerak bo'lgan paytda hisoblanadi: loop ichidagi Get yangilangan o'zgaruvchini ko'radi.
Sequence shoxlari tartib bilan bajariladi; shox ichidagi Delay tugagach keyingisi boshlanadi.
For Count butun 0..100000 qiymat bo'lishi kerak. While sharti har aylanishda qayta hisoblanadi.
Break ichki loopni tugatadi va uning Completed chiqishiga o'tadi.
Delay kutayotgan hodisaning ma'lumotlari va bajarish stekini saqlaydi. Entity/script nofaol bo'lsa kutish ham to'xtaydi.
Bir hodisani qayta-qayta boshlash alohida bajarish vazifalarini yaratadi; har frame uzun Delay boshlash o'rniga buni Start yoki tegishli input hodisasidan boshlang.

Play paytida tanlangan obyektning graph oynasida **Enable breakpoints**, **Pause**, **Continue**, **Step**, o‘zgaruvchilar va oxirgi 128 node izi mavjud. Calls chaqiruv zanjiri, Function frame argument/lokallari va Retained data hajmi ko‘rsatiladi.
Breakpoint graph nusxasini to'xtatadi; boshqa obyektlar va o'yin simulyatsiyasi davom etadi. O'yinning umumiy Pause boshqaruvi alohida.
Player'da breakpoints avtomatik o'chirilgan; editor debug belgisi buildga majburiy to'xtash kiritmaydi.

Bajarish 10000 ko'rsatma bilan chegaralanadi; cheksiz loop o'rniga node raqami bilan xato chiqadi.
Ma'lumot chuqurligi 128, bajarish steki 1024, bir vaqtdagi vazifalar 128, o'zaro graph chaqiruv chuqurligi 32 bilan cheklangan.
Graph 4096 node / 8192 sim / 256 o‘zgaruvchi / 64 funksiya / 4 MB faylgacha; matn qiymati 64 KB gacha.
Har funksiyada 32 kirish, 32 natija va 32 lokal; defaultlar jami 64 KiB, typed chaqiruv chuqurligi 32.
Massiv 1024 element va hisoblangan 64 KiB payload bilan chegaralangan. Event 32 kalit va 64 KiB.
VM saqlab turgan qiymatlar/vazifa yozuvlari jami 8 MiB bilan chegaralangan; bu umumiy process RSS o‘lchovi emas.
Noma'lum node, yo'qolgan pin, tur xatosi, ikki manbali ma'lumot kirishi, takrorlangan ID, data cycle va nolga bo'lish rad etiladi.
Xato shu graph nusxasini to'xtatadi va konsol/debug panelga chiqadi.

## Saqlash, C++ va eksport

Graph engine'ning mavjud `NativeScriptComponent` lifecycle'ida ichki `Zamin.VisualGraph` hosti orqali ishlaydi.
Shuning uchun bitta obyektning bitta skript slotiga C++ yoki visual graph ulanadi. C++ skript bor obyektga ulash tugmasi almashtirishni aniq ko'rsatadi.
Ikkisini birlashtirish uchun alohida controller obyektlari, xabarlar va aks ettirilgan maydonlardan foydalaning.
C++ metodlari avtomatik nodega aylanmaydi: faqat katalogdagi API va ro'yxatga olingan maydonlar mavjud.
Curve/Enum/custom struct pinlari, macro/collapse, graph inheritance, network RPC va bytecode/native code generation alohida rivojlantirish bosqichlaridir. Typed funksiyalar va yuqoridagi yetti massiv turi ushbu bo‘limga kiradi.

Graph assets registryda ro'yxatlanadi, Content Browser'da ochiladi, asset inspector diagnostika ko'rsatadi.
Graph ichidagi asset yo'llari reference scan va rename bilan yuritiladi. Graph o'zgaruvchisini override qiladigan asset yo'lini
ichki JSON matniga yashirish o'rniga graph node'ining Path qiymatida saqlash tavsiya qilinadi: nested override matni reference scanner tomonidan tahlil qilinmaydi.
Play'dan chiqqach graph tahriri qayta ochiladi. Saqlangan graph keyingi Play'da yuklanadi; Play ichida faylni jonli almashtirish yo'q.
C++ hot reload graph o'zgaruvchilarini saqlaydi, lekin boshqa native taymerlar kabi kutayotgan bajarishlarni qayta boshlaydi.

## Namunalar va tekshiruv

[VisualWorkshop](../Projects/VisualWorkshop/VisualWorkshop.zproject) C++ modulisiz ikkita sahnaga ega:

- `Visual2D`: WASD/arrow input → 2D velocity; triggerga tegish rangni vaqtincha o'zgartiradi.
- `Visual3D`: ikkita kub bir graph bilan, turli Speed override’lari orqali teskari tezliklarda aylanadi.
- Ikkala sahnada `BlueprintGameplay.zgraph`: massiv → ForEach → typed Award(Amount, Factor) → lokal/Delay/Return → Total → Finished event → Print. Multiplier override sabab 2D jami 12, 3D jami 18.

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

## World navigation va traversal

- **SetWorldDestination**: NavAgent'ni world atlas rejimiga o‘tkazadi; Position world Vec3,
  Success Complete/Partial route topilganini bildiradi. Failed uchun false. Target default Self.
- **NavTraversalState** (pure): Pending, Link/Revision/Ticket (aniq String), Entry/Exit (Vec3), Status.
  UUID'lar Number emas: 2^53 dan katta qiymatlar ham yaxlitlanmaydi.
- **Event.NavTraversalRequested**: Self agent entry'ga kelganda bir marta, har yangi ticket uchun;
  Link, Revision, Ticket, Entry, Exit payload. VM paused bo‘lsa event mavjud queue qoidalariga amal qiladi.
- **CompleteNavTraversal**: event'dan Link/Revision/Ticket va Target; Success bool. Xarita o‘zgarishi,
  cancel, band controller exit yoki noto‘g‘ri token false beradi. Keyin Branch bilan retry/cancel tanlanadi.
- **CancelNavTraversal**: pending so‘rovni bekor qilib agentni to‘xtatadi; Success bool.

Delay task event tokenlarini o‘zi bilan saqlaydi. Kechikishdan keyin NavTraversalState'dan yangi token
olib eski action'ga qo‘llash o‘rniga event pinlarini complete'ga ulang. Bu explicit teleport handshake;
character yurishi fizikaga bog‘liq, controller exit band bo‘lsa ko‘chmaydi. Rigidbody traversal completion
hozir qo‘llanmaydi. Namuna: WorldWorkshop2D/3D `Scripts/WorldTraversal.zgraph`, B tugmasi bilan.

Async world policy'da SetWorldDestination true worker request qabul qilinganini bildiradi, path status None
bo‘lishi mumkin. **NavQueryPending** pure bool accepted query tayyor bo‘lguncha true; portal Pending alohida.
Backpressure'da destination intent retry qilinadi, StopMoving bilan bekor qilinadi. Sample graph Searching
variable'ni shu node bilan yangilaydi. Natija revision/goal/start/entity generation tekshiruvlaridan keyin qo‘llanadi.

`NavSteeringState` — pure Navigation node. `TargetEntity` (bo‘sh: o‘zi), outputs `Preferred`,
`Velocity` (Vector3), `Neighbors` (Number), `Blocked`, `Limited` (Bool). NavAgent bo‘lmasa
zero/false qaytaradi. Velocity world clearance’dan keyingi harakat buyrug‘i; fizik actual velocity
kerak bo‘lsa body/controller query ishlatiladi. `CrowdMonitor.zgraph` namunasida Event.Update
uchta variable’ni yangilaydi; ko‘rsatkichlar oxirgi navigatsiya update’iga tegishli.

`NavRecoveryState`: pure node, optional TargetEntity, outputs `Phase` (String), `Retries`,
`StalledSeconds`, `Cooldown` (Number), `Exhausted` (Bool). Agent yo‘q: Idle/0/false.
`RetryNavigation`: exec, optional TargetEntity, Then/Success. Active destination’li world agentning
watchdog’ini reset qilib, re-path so‘raydi va pending traversal token’ini bekor qiladi; yangi
query keyingi owner nav update’ida boshlanadi. `RecoveryMonitor.zgraph` 8 node / 9 link bilan
holatni o‘qish va Message orqali retry’ni ko‘rsatadi.

Typed funksiyalar/kolleksiyalar modeli va VM testlari: `ZaminTests --filter Blueprint`.
Real 2D/3D Player/editor screenshotlari va modulsiz eksport tekshiruvi:

```bash
python3 Tests/blueprint_workflow_test.py --build build-review --exports
```

Eksport tekshiruvi ikkala sahnada haqiqiy score loglarini, PNG, eksport hisoboti va ko‘chirilgan launcher versiyasini tekshiradi.
Sahna lifecycle testi Total qiymati va vazifalar tugashini VM ichidan ham tekshiradi.
