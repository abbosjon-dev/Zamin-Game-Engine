# Zamin Engine — 500 ta vazifa rejasi

Dastlabki M01–M25 milestone'lari 20 tadan vazifa tutadi. `[x]` — bajarilgan, `[ ]` — rejada.
Quyida yangi 2D/3D mahsulot bosqichlari ham alohida yuritiladi.

## Faol yo'nalish: professional 2D va 3D engine

2026-11-09: maqsad — 2D va 3D birinchi darajali bo'lgan umumiy engine. Unreal darajasidagi kenglik,
ishonchlilik va ishlab chiqish qulayligi uzoq muddatli nishon; hozir bu tenglikka erishilgani da'vo qilinmaydi.
Quyidagi bosqichlar mahsulotga tayyor natija va tekshiruv mezoni bilan yuritiladi. Pastdagi dastlabki 500 vazifa
saqlanadi; ulardagi belgi yangi imkoniyatning ishlayotganiga dalil o'rnini bosmaydi.

| Bosqich | Natija | Tugallash mezoni | Holat |
|---|---|---|---|
| P0 Ishonchli asos | Atomik save, tiklash, xavfsiz build, chegaralangan Undo, CPU profili, ajratilgan runtime | Save xatosida ma'lumot yo'qolmasligi, zararli papka nomi bajarilmasligi, UI'siz Player, SSE2 sinovi | Auditdagi 7 band bajarilgan; boshlang'ich dalil 645 unit / 12 render / 265 editor qadam |
| P1 2D/3D katta xarita render asosi | Terrain patch LOD, yoriqsiz qo'shni chegaralar, qisman GPU mesh yangilash; tilemap ierarxik culling | Geometriya kamayishi, LOD o'tish/hysteresis, collision aniqligi, 2D ko'rinish va chizish tartibi oracle bilan mos | Bajarildi (2026-11-10): 656 unit / 12 render / 265 editor qadam; o‘lchovlar quyida |
| P2 Kontent sifati va mualliflik | Terrain GPU qatlamlari/PBR, normal/roughness, atlas/cook, foliage, 2D palette/animation qulayligi | Haqiqiy assetli 2D/3D sahnalar, import/export/undo roundtrip, tekstura xotira byudjeti | GPU terrain v1 yakunlandi; qolgan kontent ishlari rejada |
| P3 Dunyo va asset streaming | Disk kataklari, async load/unload, data layers, HLOD, floating origin, asset residency | Tez harakatda/cancel/reload'da yo'qolmagan obyektlar, chegaralangan RAM/VRAM, main thread'da og'ir IO yo'q | Runtime v2: LRU cleanup, observed pressure gate, terrain tiles va grid mosaic bajarildi; HLOD/floating origin/hard timing rejada |
| P4 Rendererning keyingi avlodi | GPU timer v1 mavjud; render graph, Vulkan, temporal AA/upscaling, katta sahna visibility va yoritish | Drayver/physical GPU matritsasi, HDR/alpha/picking/shadow regressiyalari, reference hardware byudjeti | Rejada; virtual geometry va dinamik GI — alohida tadqiqot |
| P5 Gameplay, animatsiya, fizika va UI | Mavjud 2D/3D tizimlarini production darajasida birlashtirish, katta kontent va gameplay workflow | Ikki tugallangan vertical slice, saqlash/yuklash, controller, animatsiya/fizika stress, UI input/scale testlari | Motor/combat/AI/save/UI, quest, equipment/abilities, projectiles/effektlar va Animator 1D/2D zanjiri yakunlandi (2026-11-22); ikki to‘liq vertical slice (Lantern Keeper 2D, Beacon Pass 3D) bilan stress, UI input/scale, yangi jarayonda davom etish, ko‘chirilgan eksport va profiling darvozalari ham bajarildi (2026-11-22) |
| P6 Tarmoq va kengaytirish | Server authority, replication, latency compensation, plagin SDK/ABI, xavfsiz hot reload | Yo'qotilgan paket, kechikish, reconnect, versiya mos kelmasligi va plagin unload sinovlari | Rejada |
| P7 Platforma, release va mahsulot | Linux/Windows, keyin macOS; cook/package/update/crash telemetry; editor/launcher yagona auth | Toza mashinada install/export/run, talablar va litsenziyalar, imzolangan release va rollback sinovi | Rejada |
| P8 Yakuniy sifat darvozasi | Hujjatlar, ko'chirish yo'li, ikki namunaviy o'yin va profiling hisobotlari | Uzoq sessiya, resurs sizishi, recovery/fuzz, reference sahnalarda barqaror frame time va foydalanish sinovi | Rejada |

### Yakunlangan bo‘lim — quest/progression

- [x] Bounded objectives, prerequisites, receipts and progression model.
- [x] Native/Blueprint automatic gameplay notices, atomic bag/XP rewards and HUD.
- [x] Save v2 quest snapshots, v1 migration and transactional restore.
- [x] 2D/3D playable quests, fresh-process resume, Editor/export/screenshots.
- [x] Unit, sanitizer, regression, documentation and main push.

Dalillar (2026-11-12): 1199/1199 unit (39 yangi), 11/11 headless CTest, 12/12 render.
Pure QuestModel ASan/UBSan/LSan: 12/12. QuestModel/World/Graph, SaveModel/World,
GameplayRules/Runtime instrumentlangan 7 TU owner probe: 39/39, LeakSanitizer bilan.
Qolgan shared engine, Scene/serializer/physics/VM normal build; bu butun engine sanitizer run’i emas.
Clang 18 syntax-only -Werror: 12 ta TU. Haqiqiy ImGui Configure/Claim/BindHUD add/save/Undo.
2D/3D Player va Editor uch fazali acceptance/resume/no-replay hamda compiler-free eksportning
write/resume/real relocated no-replay kursi: 14/14 workflow. Canonical holat: XP=200,
Coin=5, Badge=1, ikki quest Claimed. Generator deterministik; Python/format/docs tekshirildi.
WorldCrowdHeadOn testining atlas readiness sharti birinchi katakdan barcha 300 katakka
aniqlashtirildi; 10/10 mustaqil run o‘tdi. Hisobot va rasmlar: `build-review/GameplayQuest/`.
Software Mesa; physical GPU byudjeti o‘lchanmagan. [Chegaralar](GameplayQuest.md).

### Yakunlangan bo‘lim — equipment, stats va abilities

- [x] Item definitions, 4 equipment slots, deterministic stat modifiers va bag capacity rollback.
- [x] Damage/heal/shield abilities, ingredients, energy/regen, cooldowns, unlocks va Blueprint API.
- [x] Canonical-state HUD, v3 save/migration, transactional restore va actor lifecycle.
- [x] 2D/3D acceptance, fresh-process resume, relocated export va main push.

Dalillar: 1225/1225 unit (26 yangi), 11/11 headless CTest. Pure LoadoutModel/RulesModel
ASan/UBSan/LSan: 6/6. LoadoutModel/World/Graph instrumentlangan 3 TU owner probe:
26/26, LeakSanitizer bilan; qolgan SaveWorld/Scene/VM/physics/shared engine normal build.
Bu butun engine sanitizer run’i emas. Clang 18 syntax-only -Werror: 7 TU.
Haqiqiy ImGui LoadoutConfigure/EquipmentEquip/AbilityCast add/save/Undo o‘tdi.
2D/3D Player va Editor acceptance/resume/no-replay hamda compiler-free eksportning
write/resume/real relocated no-replay kursi: 14/14 workflow. Canonical holat:
Sword/Plate equipped, Attack=20, Armor=100, Power=10, Energy=40, Bolt Uses=2,
Potion=1, Coin=5, XP=150; saved first-cast cooldown musbat. Generator deterministik;
Python/format/docs tekshirildi. Software Mesa, hardware GPU byudjeti o‘lchanmagan.
Renderer o‘zgarmagani uchun eski 12 render holati qayta ishga tushirilmadi.
[API va chegaralar](GameplayLoadout.md). Hisobotlar: `build-review/GameplayLoadout/`.

### Yakunlangan bo‘lim — projectiles va gameplay effects

- [x] Bounded 2D/3D projectile lifecycle, collision/hit ownership, pooling va lifetime.
- [x] Area damage/support, timed status effects va stacking/expiration qoidalari.
- [x] Ability/effect authoring, HUD feedback va persistence compatibility.
- [x] O‘ynaladigan 2D/3D combat slice, profiling, export va regression darvozalari.

Dalillar (1-band, 2026-11-21): 1257/1257 unit (32 yangi: 12 `ProjectileModel*`, 20 `CombatWorld*`/`CombatRules*`),
11/11 headless CTest (DocsCheck/DocsSnippets bilan). Pure ProjectileModel/RulesModel ASan/UBSan/LSan: 32/32.
CombatWorld, CombatGraph, ProjectileModel, GameplayRules, RulesModel, LoadoutWorld va SaveWorld instrumentlangan
7 TU owner probe: 33/33, LeakSanitizer bilan (32 test + Scene hook'lari libZamin ichida `-Bsymbolic-functions`
bilan bog‘langani uchun step/sweep/vizual yo‘lini instrumentlangan nusxada yurgizuvchi 1 ta scratch test);
Scene/serializer/physics/VM va qolgan shared engine normal build — bu butun engine sanitizer run’i emas.
Clang 18 syntax-only -Werror: 10 TU. Mutatsiya tekshiruvi: world kanalidagi actor filtri o‘chirilsa 3 test,
segment oxiridagi (T = 1) devor tuzatishi qaytarilsa yangi chegara testi, dead/inactive skip o‘chirilsa 1 test
yiqiladi — shu tekshiruv Box2D float chegarasida devordan o‘tib ketish xatosini topdi va u tuzatildi.
Determinizm: ikki yangi sahnada bir xil event-log hash; 1/30, 1/60, 1/144 s va jitter kadrlarda bir xil
natija (7 hit, 3 s gacha hammasi tugaydi). 500 m/s snaryad 20 Hz’da 0.05 m nishonni o‘tkazib yubormaydi.
LoadoutWorkshop Player workflow (2D/3D × 3 faza): 6/6, save hali v3. Scratch demo’da haqiqiy Player va
Editor Play: 11/128 uchayotgan snaryad pool vizuallari bilan, GL/graph xatosi yo‘q. Saqlash vaqtinchalik
siyosati: Restore uchayotgan snaryadlarni bekor qiladi, save versiyasi o‘zgarmadi. Renderer va editor UI
o‘zgarmagani uchun render reference va ImGui editor testlari ishga tushirilmadi; Release bench 4-bandda.
[API va chegaralar](GameplayCombat.md).

Dalillar (2-band, 2026-11-21): 1288/1288 unit (31 yangi: 11 `EffectModel*`/`AreaModel*`, 20 `CombatEffect*`,
`CombatArea*`, `CombatProjectile*`, `CombatDetonation*`), 11/11 headless CTest, `ctest -L docs` 2/2. Pure
EffectModel/ProjectileModel/RulesModel/LoadoutModel ASan/UBSan/LSan: 49/49. CombatWorld, CombatGraph,
EffectModel, ProjectileModel, GameplayRules, RulesModel, LoadoutWorld, LoadoutModel va SaveWorld instrumentlangan
9 TU owner probe: 84/84, LeakSanitizer bilan (83 test + effekt fazasi, hudud LoS va o‘limda tozalashni
instrumentlangan nusxada `CombatWorld::Tick` orqali yurgizuvchi 1 ta scratch test); Scene/serializer/physics/VM
va qolgan shared engine normal build — bu butun engine sanitizer run’i emas. Clang 18 syntax-only -Werror: 15 TU
(12 engine + 3 test). Mutatsiya tekshiruvi: Refresh’da tick fazasini qayta boshlash, o‘limda effektlarni
tozalamaslik, LoS’da health aktorlarni to‘siq deb hisoblash, stun paytida attack cooldown sarflash va
16/step detonatsiya byudjetini olib tashlash — har biri mo‘ljallangan testlarni yiqitdi. Tick aniqligi: 6 ta
davomiylik/davr jufti × 20, 30, 50, 60, 120, 144, 240 Hz (42 holat) — tick soni va expiry qadami aniq (3 s/1 s
= 3 tick). Determinizm: barcha effekt/hudud yo‘llari bilan ssenariy ikki yangi sahnada va 1/30, 1/60, 1/144 s
hamda jitter kadrlarda bir xil hash; 7 s ichida hammasi tugaydi (0 snaryad, 0 effekt, ittifoqchi 91 HP = 70 +
Mend 5 + 4 × Regen 4). Scratch 2D demo (Player 76-kadr, Editor Play 90-kadr, 30 fps): Sentry 400 dan 319 HP —
2 bomba × (8 + Attack 10) + Nova (12 + 10) × 0.894 falloff + 3 ta Zahar tick × 2 bilan mos; devor ortidagi aktor
60/60 (LoS), Frost Extend 6 s bilan cheklangan, stun 4 s, pool vizuallari Editor ierarxiyasida, GL/graph xatosi
yo‘q. Regressiya (Player): LoadoutWorkshop 6/6 (save hali v3), SaveWorkshop 6/6, AIWorkshop 2/2,
RulesWorkshop 2/2. Saqlash vaqtinchalik siyosati: effektlar saqlanmaydi, Restore ularni olib tashlaydi; Capture
energiyani effekt bonusisiz sig‘imgacha yozadi (effektsiz holatda baytma-bayt o‘zgarishsiz), save versiyasi
o‘zgarmadi. Renderer o‘zgarmagani va editor UI qo‘shilmagani uchun render reference, ImGui editor testlari va
Editor workflow fazalari ishga tushirilmadi; Release bench 4-bandda. [Hudud va effektlar](GameplayCombat.md#areas).

Dalillar (3-band, 2026-11-21): 1307/1307 unit (19 yangi: 2 `GameSaveLegacy*`, 3 `GameSaveModelV4*`, 6 `CombatSave*`,
4 `CombatAbility*`, 2 `CombatHUD*`, `GameUIModelCombatRowsRoundAndFade`, `VisualGraphPinChoicesAreTranslated`;
oraliq save siyosatini tekshirgan 2 test v4 semantikasiga o‘tkazildi, `Version 4` rad testi endi 5 va 0 ni rad
etadi), 11/11 headless CTest, `ctest -L docs` 2/2. Avval, o‘zgarmagan daraxtda `Tests/Fixtures/GameSaves` ga v1/v2/v3
va haqiqiy LoadoutWorkshop faza-0 `loadout-v3.json` yozildi: to‘rttasi ham baytma-bayt qayta kodlanadi; haqiqiy
fayl headless Loadout2D sahnasiga combat bilan ham, combatsiz ham tiklanadi (Sword/Plate, Energy 65, Bolt Uses 1)
va yangi Player jarayonida faza 1 `LOADOUT_RESUMED casts=2 xp=150 coin=5` beradi, qayta yozilgan save hali v3.
Save v4 eng katta hujjati (256 obyekt × 16 effekt, 64 belgili kalitlar, 128 snaryad × 8 hit, 65 536 belgili
signature) 1 524 542 bayt — 2 MiB ichida; 38 struct va 12 JSON buzilishi har biri rad etildi. Determinizm (c):
saqlab, yangi sahnaga tiklab 15 + 150 qadamdan keyin snaryad pozitsiyasi/tezligi/yoshi, sog‘liq va effekt
qatorlari (qolgan vaqt, tik fazasi, Sequence) aynan teng. Pure ProjectileModel/EffectModel/RulesModel/
LoadoutModel/SaveModel/QuestModel/UIModel ASan/UBSan/LSan: 85/85. CombatWorld, CombatGraph, ProjectileModel,
EffectModel, SaveModel, SaveWorld, LoadoutWorld, LoadoutModel, LoadoutGraph, GameplayRules, RulesModel,
GameplayUI, GameplayUIGraph va UIModel instrumentlangan 14 TU owner probe: 94/94, LeakSanitizer bilan (93 test +
HUD/feed refresh, `CombatWorld::Tick` va v4 Capture/Restore'ni instrumentlangan nusxada yurgizuvchi 1 ta scratch
test; libZamin bilan ataylab takrorlangan ta'riflar sabab ODR tekshiruvi o‘chirilgan); Scene/serializer/physics/VM
va qolgan shared engine normal build — bu butun engine sanitizer run’i emas. Clang 18 syntax-only -Werror: 18 TU
(13 engine/editor + 5 test). Mutatsiya tekshiruvi: effektlarni loadout'dan keyin bosqichlash, bog‘lanmagan ability
signature'iga ham dum qo‘shish, hit marker'da effekt tiklarini sanash, hudud ability'sini faqat Applied > 0 da
sarflash, tiklangan snaryad generatsiyasini 1 ga tushirish va eski save'da bog‘langan aktyor effektlarini
qoldirish — har biri mo‘ljallangan testlarni yiqitdi (projectile commit bayrog‘i mutatsiyasi ekvivalent chiqdi va
sanalmadi). Regressiya (Player): LoadoutWorkshop 6/6 (save hali v3), SaveWorkshop 6/6 (v1), QuestWorkshop 6/6
(v2), UIWorkshop 6/6. Scratch 2D demo (30 fps): faza 0 Player'da 1.2 s da v4 yozildi — 3 ta uchayotgan snaryad,
Sentry 370/400 (400 − (12 + Power 10 + Attack 5) − 3 Zahar tiki), Dummy 93/120 (pierce), 3 effekt; yangi jarayonda
faza 1 `Jang holati tiklandi: 3 effekt, 3 snaryad`, tiklangan Bolt'lar urildi (10-kadrda Sentry 339/400, YONISH
x2/3, ZAHAR x1/3). HUD Kind 5/6 va 4 qatorli zarbalar lentasi Player kadrida ko‘rindi. Editor Play'da node
inspector "Jang holati" bloki 4/128 snaryad, 4/4 vizual, 2 qadam, 4/1024 effekt va Sentry effektlarini, tanlangan
EffectDefine node'i "Tick: Zarar" nomli tanlovini ko‘rsatdi; editor smoke (`ZAMIN_EDITOR_OPEN_ALL_PANELS`, graph
paneli va AbilityDefine node'i ochiq) 204/204 qadam, 0 xato. xdotool o‘rnatilmagan (o‘rnatishga ruxsat berilmadi),
shu sababli combo'ning haqiqiy ImGui add/save/Undo testi 4-bandga qoldi; renderer o‘zgarmagani uchun render
reference, workshop'larning Editor fazalari va Release bench ishga tushirilmadi.
[Save v4](GameplaySave.md#combat-state-save-v4), [ability linklari](GameplayCombat.md#ability-links).

Dalillar (4-band, 2026-11-21): 1309/1309 unit (2 yangi: `CombatWorkshopGraphsValidateAndRoundtrip`,
`CombatWorkshopOracleHeadless`), 12/12 headless CTest (yangi `BenchCombatSmoke` bilan), `ctest -L docs` 2/2, build
ogohlantirishsiz. Compiler-free `Projects/CombatWorkshop` (`Tools/samples/gen_combat_workshop.py`; qayta generatsiya
baytma-bayt bir xil, `git diff --exit-code` toza): 2D (Box2D, top-down) va 3D (Jolt, character controller) sahnalar,
6 effekt, Nova hududi (`Wall` qatlami bo‘yicha LoS), Bolt/Lance/Venom snaryadlari pool vizual prefablari bilan, 5 ta
o‘yinchi ability va Sentry'ning Spit snaryad ability'si, quest, 6 ta save bog‘lami, effekt/hit-marker HUD va zarbalar
lentasi; klaviatura va gamepad. Oracle qoidalardan hisoblangan va 2D/3D'da aynan bir xil chiqdi: faza 0 (2.45 s) —
Sentry 45 = 120 − 30 − 3 − 30 − 6 − 6 (YONISH x2), o‘yinchi 98 HP / 24 qalqon (Venom qalqonga, Zahar qalqondan o‘tib
1 tik), nishonlar 13/13/35 (Lance pierce ×3 + Nova faqat 3.5 m ichidagi ikkitasiga), devor ortidagi nishon 30 (LoS),
Frost Nova nishonlarida, Attack 10 (Haste), energiya 25; faza 1 (yangi jarayon) — saqlangan uchayotgan Bolt urildi,
Burn tiki Sentry'ni o‘ldirdi (bir marta), o‘yinchi 94 (qolgan 3 Zahar tiki), energiya 15, Bolt 3, XP 150, Coin 5;
faza 2 — o‘lik Sentry'ga Bolt va energiyasiz Nova hech narsani o‘zgartirmadi, takroriy mukofot yo‘q, effektlar 0.
Faza 0 save'i: v4, 1 ta uchayotgan Bolt, o‘yinchida Poison/Haste, Sentry'da Burn (90 HP); tiklash logi `Jang holati
tiklandi: 8 effekt, 1 snaryad`. `Tests/combat_workflow_test.py --exports`: 2D/3D × (3 Player faza klaviatura/gamepad
lentasi bilan + 3 Editor Play faza) + 2 × eksport write/resume + haqiqiy ko‘chirilgan no-replay = 14/14 workflow,
2 min 7 s; har birida markerlar, GL/graph xatosi yo‘qligi, PNG va save JSON tekshirildi (`build/GameplayCombat/`).
Mutatsiya: generatorda Nova LoS'ni o‘chirish, Zaharni qalqonga yuttirish, Sentry'ni tiklangan fazada otdirish,
Lance pierce'ni olib tashlash va F5'ni Bolt urilgandan keyinga surish — har biri headless oracle'ni yiqitdi (5/5,
4–14 tekshiruv). Release `ZaminBench --combat` (600 kadr, 5 ishga tushirish): o‘rtacha `TickCombat` 2D 0.069–0.106
ms (p95 0.092–0.150), 3D 0.079–0.101 ms (p95 0.218–0.270), 1024 effekt nusxasi 0.047–0.060 ms — maqsadlar (0.5 / 1.0
/ 1.0 ms) bajarildi; ajratmalar 9.26 / 12.15 / 21.05 kadrga, `ZAMIN_ALLOC_TRACE` bo‘yicha hammasi fizika so‘rov
vektorlari va `RuleWorld` voqea `std::deque`'idan, combat kodining o‘zi ajratmaydi; baseline
`Tests/Baselines/combat-release.json`, `bench.sh --only combat --compare` 0 sekinlashish. Dizayndagi 512 × 16 effekt
ssenariysi sahnaning 1024 nusxa chegarasidan oshgani uchun 64 × 16 = 1024 o‘lchandi. Pure ProjectileModel/EffectModel/
RulesModel/LoadoutModel/SaveModel/QuestModel/UIModel ASan/UBSan/LSan: 85/85. CombatWorld, CombatGraph, ProjectileModel,
EffectModel, GameplayRules, RulesModel, LoadoutWorld va LoadoutModel instrumentlangan 8 TU owner probe: 42/42
(`CombatWorkshop*`, `CombatWorld*`, `CombatEffect*`), LeakSanitizer bilan; xuddi shu TU'lar bilan sanitizer'li
`CombatBench` (to‘g‘ridan-to‘g‘ri chaqirilgan instrumentlangan `TickCombat`, 128 snaryad, 2D/3D va 1024 effekt, 3 × 300
kadr) xatosiz. libZamin `-Bsymbolic-functions` bilan bog‘langani uchun workshop graph yo‘li libZamin ichida
instrumentlanmagan; Scene/serializer/physics/VM va qolgan shared engine normal build — bu butun engine sanitizer
run’i emas. Clang 18 syntax-only -Werror: 3 TU (`CombatWorkshopTests`, `CombatBench`, `ParticleBench`); format
tekshiruvi va `py_compile` toza. Regressiya: LoadoutWorkshop 14/14 workflow (save hali v3), SaveWorkshop 6/6 (v1).
Editor: workflow'ning 6 ta Editor Play fazasi o‘tdi; QA hook'lari bilan Editor Play'da node inspector "Jang holati"
1/128 snaryad, 8/1024 effekt, Sentry'da Burn va ProjectileDefine'ning "Kind: Zarar" tanlovini ko‘rsatdi.
`Tests/combat_editor_test.py` yozildi, lekin xdotool o‘rnatilmagan (o‘rnatishga ruxsat berilmadi), shuning uchun
haqiqiy ImGui add/save/Undo testi ishga tushirilmadi; renderer o‘zgarmagani uchun render reference ham. Headless
oracle snaryad vizual prefablarisiz yuradi (prefab instansiyasi GL kontekstsiz shrift atlasini quradi); vizual pool
Player/Editor workflow'larida ishlaydi. Sentry'ning Spit chaqiruvi uning o‘z grafida emas, o‘yinchi grafida
(`Target=850`) — QA fazasi o‘sha yerda. [Playable slice](GameplayCombat.md#playable-slice-combatworkshop),
[profiling](GameplayCombat.md#profiling).

Dalillar (review tuzatishlari, 2026-11-22): 1315/1315 unit (6 yangi: `CombatImpactDetonationLineOfSight*`,
`CombatPureNodesNeverCreateTheCombatWorld`, `GameSaveModelV4TickPhase*`, `CombatSaveOwnerlessProjectile*`,
`CombatAbilityCastKeepsEnergy*`, `CombatHUDZeroTicks*`; `CombatSaveUnresolved*` endi sobiq egasining UUID'ini
kutadi), 12/12 headless CTest, `ctest -L docs` 2/2, build ogohlantirishsiz. Tuzatishlar: devorga urilgan
snaryadning hududi sirtdan 2 sm oldinda portlaydi (LoS nurlari urilgan devor ichidan boshlanmaydi); egasiz
snaryad saqlab-yuklangandan keyin ham sobiq egasining UUID'iga tegmaydi (v4'da ixtiyoriy `FormerOwner`, faqat
o‘rnatilganda yoziladi: usiz hujjat baytlari o‘zgarmaydi, eski v4 fayllar o‘qiladi) va uning hududi doim jamoa snapshot'i bo‘yicha
qabul qiladi; hech narsa o‘zgartirmagan tik (to‘la sog‘liqqa heal) hit halqasiga yozilmaydi, hit marker
butun halqadan eng yangi to‘g‘ridan-to‘g‘ri zarbani oladi; pure `ProjectileState`/`EffectState`/
`CombatModifiers` node'lari CombatWorld yaratmaydi; effekt MaxEnergy'ni kamaytirgan Cast energiyani sig‘imgacha
qisadi (o‘sha kadrdagi save endi tiklanadi); save kodeki tik fazasini modeldagi kabi 60 + 1e-9 gacha qabul qiladi.
Mutatsiya: oltita tuzatish (backoff, jamoa qabuli, sobiq ega, nol tik, energiya, tik epsiloni) qaytarilganda 6 test
(5 yangi + o‘zgartirilgan `CombatSaveUnresolved*`, 17 tekshiruv) yiqildi; pure node va hit marker tuzatishlari
qaytarib ko‘rilmadi. xdotool (1:3.20160805.1-5build1)
o‘rnatildi va `Tests/combat_editor_test.py` haqiqiy ImGui'da o‘tdi (3 min 17 s): uchta node add/save/Undo
hamda nomli tanlov combo'si orqali ProjectileDefine `Kind` → "Davolash" va EffectDefine `Tick` → "Zarar",
har biri save, Undo, Redo, yana Undo — saqlangan graph har qadamda tekshirildi; test CI'ga qo‘shildi
(combat workflow bilan). CombatWorkshop kadri: 2D ortografik o‘lcham 10 → 12, markaz y 1 → 2; 3D kamera
z 12 → 10.5 — Editor Play'da Sentry viewport toolbar'i ostidan chiqdi (2D/3D skrinshotlar).
`Tests/combat_workflow_test.py --exports` 14/14 (1 min 58 s); 3D erkin o‘yin (R, Space, L, N, M×2) lentasida "+0 Regen"
qatori yo‘q, "+2 Regen" bor. Regressiya (Player): LoadoutWorkshop 6/6, SaveWorkshop 6/6. Editor toolbar'i HUD
sarlavhasini yopishi barcha workshop'larda umumiy (LoadoutWorkshop'da ham) — alohida editor ishi.

### Keyingi paket — ikki tugallangan 2D/3D vertical slice va stress darvozalari

- [x] 2D vertical slice: menyu, kamida ikki daraja, motor/combat/AI/quest/loadout/HUD bitta o‘yin oqimida, yakun ekrani.
- [x] 3D vertical slice: character controller, kamera, Animator locomotion, combat/AI/quest va save bitta o‘yin oqimida.
- [x] Animatsiya/fizika stress hamda UI input/scale (klaviatura/gamepad, turli rezolyutsiya) darvozalari.
- [x] Fresh-process resume, relocated export, profiling va regression darvozalari ikkala slice uchun.

Dalillar (2D slice, 2026-11-22): *Lantern Keeper* (`Projects/LanternKeeper`, kompilyatorsiz, `ScriptModulePath ""`) —
`Menu`, `Orchards` (46×26), `Yertola` (44×30), g‘alaba va mag‘lubiyat ekranlari, boss Soya Bek;
[VerticalSlices.md](VerticalSlices.md). Engine kengaytmalari: transfer hujjati (`Zamin.GameplayTransfer` v1,
`SaveWorld::CaptureTransfer/RestoreTransfer`, `Gameplay.TransferWrite/Load/Inspect`), LoadScene fade, App/Audio/
Localization/Display/UIValue node'lari, `Gameplay.Steer` va vektor matematikasi, `FormatNumber`/`FormatTime`, HUD
kind 7–9 (energiya, qobiliyat, tajriba darajasi), AI `Strike=false` (delegatsiyalangan hujum), Animator
`DriveSpriteClips`; generator kitlari `Tools/samples/graphkit.py`, `slicekit.py`, `gen_lantern_keeper.py`.
1332/1332 unit (17 yangi: 4 transfer, 5 slice node/HUD, 1 AI, 3 sprite klip, 4 `LanternKeeper*`; to‘liq run 86 s),
12/12 headless CTest, `ctest -L docs` 2/2, build ogohlantirishsiz. Headless: oracle menyu → Orchards → transfer →
Yertola → g‘alaba 5022 kadrda (850 XP, 4-daraja, 5/5 vazifa, 19 tanga = 8 + 6 + vazifa mukofoti 5, Qilich/Tumor/Zirh
kiyilgan, Nova/Mend ochiq, boss o‘lik, uchayotgan snaryad 0); mag‘lubiyat rejasi 5263 kadr (3 o‘lim → mag‘lubiyat →
saqlashdan qayta yuklash → 1-daraja yana 200 XP/13 tanga bilan, mukofot takrorlanmaydi); determinizm 2252/2252 kadr,
23/23 trace qatori. `Tests/slice_workflow_test.py` (yangi CI qadami) 7/7 faza, 840 s: P0 klaviatura lentasi 24 s
(musiqa 0.55 → 0.85, `SLICE_SETTINGS`, yangi o‘yin, harakat/Bolt/zarba/inventar/pauza), P1 demo 231 s (1-daraja
75.0 s, 13 tanga, 200 XP; `Carry.json` v1 XP 200, `Run2.json` v4 Yertola), P2 yangi jarayonda Davom etish → boss →
g‘alaba 189 s (`time=166.3 xp=850 level=4 coins=19 deaths=0 quests=5/5`), P3 6 s (Davom etish yo‘q, "Eng yaxshi vaqt
02:46"), P4 mag‘lubiyat → qayta yuklash 300 s (`coins=13 xp=200`, qayta yuklashdan keyin pilot tiqilmaydi), E0
Editor Play Orchards 48 s, E1 Editor Play Yertola eski `Carry.json` yonida 41 s (shu qadamda tuzatildi: daraja
to‘g‘ridan-to‘g‘ri ochilganda eski transferni o‘qimaydi). Loyiha 192 fayl, 5.2 MB; graflar (node/link): Hero 560/678,
Game 620/732, Enemy 174/205, Menu 147/160 (chegara 2500/5000); generator ikki marta ishga tushirilganda fayllar bir
xil. Dizayndan farqlar: darajalar kichikroq (56×32/48×36 o‘rniga), o‘tlar 5 emas 3, dushmanlar kamroq (L1 4
qaroqchi, 2 kamonchi va boshliq; L2 4 skelet, 2 kamonchi, boss va 2 ruh), richag/panjara/tikan tuzoqlari yo‘q; bitta `Enemy`
grafi arxetip override'lari bilan, daraja direktori `Game` grafida; sozlamalar faqat menyuda (o‘yin ichida va UI
scale yo‘q). Gamepad lentasi, UI scale, relocated export va `samples_assets.py` graf rejimi 3–4-itemlarga qoldirildi.

Dalillar (3D slice, 2026-11-22): *Beacon Pass* (`Projects/BeaconPass`, kompilyatorsiz) — `Menu` (oqshomgi dovon
manzarasi, sekin chayqaluvchi kamera) va `Dovon` (120×120 m relyef, qarorgoh, karvonsaroy, otxona, minora maydoni,
mash'al; 374 obyekt); [VerticalSlices.md](VerticalSlices.md), README (uz), launcher rasmi. Character controller +
`Gameplay.Motor3D` (yugurish 5.4, yurish 1.8 m/s, sakrash), orbit kamera (sphere cast), `gltfkit.py` skinned odamlar
(21 bo‘g‘in; qahramon, qorovul, Qora qorovul) va Animator: Locomotion BlendTree1D (0 / 1.8 / 5.4 — klip uzunligi =
qadam / tezlik), Air/Land/Dead, `UpperBody` (Override) va `Hit` (Additive) qatlamlari, `Hit`/`Release`/`Footstep`
event'lari; zarba/olov o‘qi/Zilzila animatsiya event'ida, delegatsiyalangan qorovul zarbalari, kamonchilar, boss
shockwave'i, 4 vazifa (800 XP, 4-daraja), Shamshir/Qalqon bone attachment bilan, ziyoratgohlarda v4 saqlash.
Engine: `Animator.PlayLayer` `Restart` pini / `PlayLayer(..., restart)` (standart o‘chiq, eski graflar o‘zgarmaydi).
1339/1339 unit (7 yangi: 6 `BeaconPass*` + `AnimatorLayerRestartReplaysTheClipAndItsEvents`; to‘liq run 251 s,
`BeaconPass*` 133 s), 12/12 headless CTest, `ctest -L docs` 2/2, build ogohlantirishsiz. Headless: oracle menyu →
Dovon → g‘alaba 2275 kadrda (800 XP, 4-daraja, 4/4 vazifa, 3 tosh sarflangan, Shamshir+Qalqon kiyilgan, Zilzila ochiq,
Qora qorovul o‘lik, uchayotgan snaryad 0, `Run1.json` v4); lokomotsiya (1264 kadr): Walk/Run og‘irligi 1.00/1.00,
UpperBody 1.00 zarbada, Air → Land, Hurt; qadam oralig‘i walk 1.007 m (yarim qadam 1.000), run 1.950 m (1.950);
damplangan Speed xatosi 0.006 m/s (374 namuna, chegara 0.05); oyoq relyefdan pastga tushmaydi (min 0.000 m, 1264
namuna), kamera devor ichida 0/1264; mag‘lubiyat rejasi 4272 kadr (3 o‘lim, Dead holati → mag‘lubiyat → qayta yuklash →
`Pass` bir marta, 100 XP, 0 o‘lim); save/resume: hovli ziyoratgohi saqlashi (kadr 1294, qahramon (2.548, 3.001,
1.600), 100 XP, 2 tosh, 3 dushman, 5 ishlatilgan obyekt) yangi jarayonda <1 mm aniqlikda tiklanadi (sumka, vazifalar,
loadout ham), davomi 5/5 hodisa bir xil tartibda, 300/300 XP, 3/3 tosh, tog‘ ziyoratgohida bir xil joy (−5.898,
−26.402); Qora qorovul jangida uchayotgan Bolt bilan tezkor saqlash (yoshi 0.133 s) aynan tiklanadi va 6/6 kadr bir xil
yo‘l (eng katta farq 0.00000 m), 7-kadrda tegib, boss sog‘lig‘i 260.8/260.8; determinizm 2244/2244 kadr, 26/26 trace
qatori. `slice_workflow_test.py --game BeaconPass` (yangi CI qadami, 640×360 Low) 6/6 faza, 742 s: P0 klaviatura
lentasi 40 s (musiqa 0.55 → 0.85, yangi o‘yin, autosave, yugurish/sakrash/zarba/Bolt/sumka, pauza → `Menyu: Inventar`),
P1 demo 112 s (hovli ziyoratgohigacha: v4, 100 XP, 2 tosh, sog‘liq 120, combat bo‘limi bor), P2 yangi jarayonda Davom
etish → g‘alaba 88 s (`time=73.6 xp=800 level=4 stones=0 deaths=0 quests=4/4`), P3 7 s (Davom etish yo‘q), P4 339 s (3
o‘lim → mag‘lubiyat → *Oxirgi saqlashdan* → `Pass` bir marta), E0 Editor Play 155 s; Lantern Keeper fazalari umumlashgan
harness'da ham o‘tadi (P0 33 s, P3 11 s, E0 66 s). Loyiha 192 fayl, 6.3 MB (Assets 5.99 MiB; eng katta sahna 547 KB,
model 372 KB, tekstura 140 KB, ovoz 516 KB / 12 s); graflar (node/link): Hero 655/787, Game 487/580, Enemy 180/211,
Menu 155/168, Camera3D 38/46; generator ikki marta ishga tushirilganda fayllar bir xil. Shu qadamda tuzatildi:
ziyoratgoh dam olish nuqtasi (pilot to‘xtab, keyin ishlatadi), qahramon rule event'larini `LateUpdate`da o‘qiydi —
saqlash restore bilan bir fazada, tiklangan run saqlangan holatdan boshlanadi; resume'da yo‘nalish (yaw/facing)
tiklanadi; menyu kamerasi qarag‘aylar ichidan vodiy ustiga ko‘chirildi; `[Look]` maslahati
`[LookX|Sichqoncha]`. Dizayndan farqlar: Qora qorovul HP 280 (420 emas), qorovullar 4 (5 emas), relyef 120 m/81
(128/129 emas), `meshkit.py` OBJ kit yo‘q (karvonsaroy teksturali primitivlar + gltfkit buyumlaridan), `Pass` vazifasi
2 darvoza qorovuli + darvozani ochish (Manual receipt emas), sozlamalar faqat menyuda; restore jismlar va nav
agentlarini to‘xtatgani uchun (saqlash tezlik/yo‘l saqlamaydi) davom bir xil hodisa/natija beradi, kadrlar esa farq
qilishi mumkin. Gamepad lentasi, UI scale, relocated export va profiling 3–4-itemlarda.

Dalillar (stress va UI darvozalari, 2026-11-22): [VerticalSlices.md](VerticalSlices.md#gates),
[Benchmarks.md](Benchmarks.md). Stress (headless `ZaminBench`, Release, 4 vCPU Xeon 2.1 GHz, 5 ishga tushirish,
oraliq — eng kichik/eng katta): P1 `--physics --2d` (60×40 m tilemap, 2 048 jism, 64 `StepMotor` aktor, 128 snaryad)
fizika qadami o‘rtacha 1.56–2.15 ms, p95 2.3–3.1 (byudjet 4/6); P2 `--physics --3d` (chuqur, rampa, 0.25 m zinapoya,
35° qiyalik, 65×65 relyef, 512 jism, 64 Character Controller 3D, 128 sphere cast) qadam + CC 1.51–1.80 ms, p95
2.00–2.17 (8/12), cast'lar 0.23–0.28 ms; A1 `--animation` 256 odam (`Hero.glb`, 21 suyak, Dovon controller'i, yarmi
ekrandan tashqarida) 3 ishchida 0.57–0.82 ms, p95 0.66–1.10 (1.5/2.0), bitta oqimda 0.99–1.73 ms (4.0), ajratma
0.0 / kadr; A2 1 024 sprite animator (`DriveSpriteClips`, 2D blend yo‘nalishlari) Animator yangilash 0.30–0.48 ms (0.6),
ajratma 0.0; P3 `--slice-stress --2d` (Orchards: 32 miya, 200 quti, 64 snaryad, 256 sprite animator, 48 Light 2D) o‘yin
kodi 2.80–3.42 ms, p95 3.00–4.71 (6/10); A3 `--slice-stress --3d` (Dovon: 24 animatsiyali qorovul, jami 27 miya, 64
snaryad, 200 Jolt jism) 3.89–5.45 ms, p95 5.41–6.37 (8/12). Invariantlar har safar o‘tdi: tunnel/NaN 0, 2D eng katta
tezlik 6.88 m/s, CC oyog‘i relyefdan eng ko‘pi 0.0002 m pastda, buyurilgan yo‘lning ≥ 90.7 % i bosildi, ikki
simulyatsiya hash'i teng; blend og‘irlik xatosi ≤ 3e-8, 0/3 ishchi poza hash'i teng, event'lar 20/30/60/144 Hz da
animator bo‘yicha bir xil, sprite kadr/faza buzilishi 0 (14 223 klip almashishida), graf xatosi 0. Boshlang‘ich
natijalar `Tests/Baselines/{physics,animation,slicestress}-release.json`, `bench.sh --compare` ishlaydi (sekin
ko‘ringan animatsiya qayta o‘lchanib o‘tdi); `BenchPhysicsSmoke`/`BenchAnimationSmoke`/`BenchSliceStressSmoke`
`ctest -L bench` da. Shu qadamda tuzatildi: yo‘nalish almashganda sprite klip shu kadr event'ini ikkinchi marta
chaqirardi (`SpriteAnimationComponent::DrivenState`, `AnimatorSpriteClipsDirectionSwitchesFireEachStepOnce`); Animator
yangilanishi bitta oqimda endi ajratmasiz (oldin 24 odamga 223, 64 sprite'ga 870 ajratma / kadr; oxirgisi poza
signaturasining o‘sishi, `ZAMIN_ALLOC_TRACE` bilan topildi). UI: `Scene::AuditUI` + `CheckUIAudit` (`Margin`,
`Offscreen`, `Overlap`, `TextOverflow`, `MinFont` 12/14 px, `Focus`, `Unreachable`, `SideBand`), Player'da
`ZAMIN_UI_AUDIT`, `Scene/SceneManager::SetUIScaleMultiplier`, Blueprint `Display.SetUIScale`/`Display.UIScale`;
ikkala slice sozlamalarida *Interfeys o‘lchami* 80–120 % (`SLICE_SETTINGS ui=`, `Settings.UIScale` da saqlanadi, start'da
qo‘llanadi). Headless `SliceMenuLayout*`: 15 + 8 ekran × 6 o‘lcham (800×600 … 2560×1080) × 3 UI masshtabi = 270 + 144
audit, 13 932 + 5 472 element, 0 xato, eng kichik matn 9.6 px (80 % da). `Tests/slice_ui_test.py` (yangi CI qadami)
ikkala o‘yinda o‘tdi: U1 klaviatura turi 60 audit, 6 ekran, 0 xato, eng kichik matn 12.03 px (65/70 s); U2 yangi
jarayon 120 % bilan ochiladi, gamepad turi 80 % ga qaytaradi (60 audit, 59/64 s), sichqoncha audit aytgan joyda
*Sozlamalar* va *Orqaga*ni bosadi; U3 seed'li fuzz 2 000 kadr × klaviatura/gamepad: 534/504 (Lantern Keeper) va 546/450
(Beacon Pass) audit, eng ko‘pi 2 panel, invariant buzilishi 0, xato 0 (145–165 s); UW 2560×1080 menyu 6 audit.
Mutatsiyalar (Player): musiqa slayderi havolalari uzilganda U1 yiqildi (tur *Orqaga*ga yetmaydi, audit uch vidjet uchun
`Unreachable`), versiya qatori 25 → 18 birlik bo‘lganda U1 12 ta `MinFont` bilan yiqildi; headless
`SliceMenuLayoutCatchesABrokenLinkAndTinyText` ham shuni tekshiradi. Layout tuzatishlari: matnlar ≥ 25 birlik,
boshqaruv qatori ekran kengligining 78 % ida o‘raladi, 2D menyu xaritasi 21:9 chetini qoplaydi (oldin chapda qora
yo‘lak). 1345/1345 unit (6 yangi: 1 animator, 5 UI audit/layout; to‘liq run 282 s), 14/14 headless CTest
(`-LE gui`, ZaminTests alohida), `ctest -L docs` 2/2, build ogohlantirishsiz; `slice_workflow_test.py` P0+E0 ikkala
o‘yinda (Lantern Keeper 38 + 70 s, Beacon Pass 42 + 176 s); generatorlar ikki marta — fayllar bir xil. Dizayndan farqlar:
o‘yin ichidagi stress (A3, P3) Player `--benchmark` va alohida `Stress` sahnalari o‘rniga headless `--slice-stress` da
haqiqiy darajalar nusxasi ustida (assetlar 6 MiB byudjetda, llvmpipe chizishi o‘yin kodi vaqtini ko‘madi;
`slice_bench.py --stress` yo‘q); canvas'lar *Expand* emas, *Match 0.5* qoldi — layout'lar tuzatildi, shuning uchun
"Expand o‘chirilsa" mutatsiyasi o‘rniga kichik shrift va uzilgan havola mutatsiyalari; `MinFont` UI o‘lchami 100 % dan
kichik bo‘lsa mutanosib kamayadi; 3 ishchidagi 15.2 ajratma / kadr `JobSystem::ParallelFor` ning o‘zi; fizikada
ajratmalar (2D 128.5, 3D 82.6 / kadr) Box2D/Jolt so‘rov natijalari — byudjet qo‘yilmagan, qayd etildi.

Dalillar (resume, eksport, profiling va regressiya darvozalari, 2026-11-22): **Yangi jarayonda davom etish** —
headless `*ResumeMatrixHeadless` bitta oddiy o‘yindan har qadamda o‘ldirilgan jarayon qoldiradigan fayllarni saqlab,
har nusxada yangi jarayonni (`SaveData::Reload`, `GameState::Load`, demo menyuning *Davom etish*i) ishga tushiradi:
Lantern Keeper 8 holat (1-daraja chiqishida transfer yozilgan, `Run.Level` hali 1 → shrine saqlashidan, transfer bir xil
200 XP; GameState'dan keyin / autosave'dan keyin → 2-daraja transfer orqali 200 XP, 13 tanga; checksum buzilgan →
zaxira, *Zaxira nusxadan tiklandi*, `recovered=1`; asosiy va zaxira yirtilgan → `SLICE_IO_FAILED load`, ekranda
*Saqlash o‘qilmadi - daraja boshidan*, transfer ham yirtilsa toza 2-daraja), Beacon Pass 5 holat (yangi o‘yin va
autosave orasida, shrine saqlashi va GameState orasida — 100 XP, ikki tosh, holat < 1 mm; buzilgan → lager
autosave'i; yirtilgan → toza boshlash); hech bir mukofot ikki marta berilmaydi, pilot qadami (`Run.Step`) 0–2.
Mutatsiya: *Davom etish*ning `Run.Save == Run.Level` sharti olib tashlansa matritsa 3 marta yiqiladi.
**Player/Editor** (`Tests/slice_workflow_test.py --exports`, ikkala o‘yin, CI qadamlari `--exports` bilan): Lantern Keeper
P0 54 s, P1 417, C 51 (buzilgan → zaxira 200 XP; yirtilgan → transfer 200 XP + banner), P2 292, P3 8, P4 461, P6 50
(gamepad lentasi: D-pad musiqa 0.85, A yangi o‘yin, stik qahramonni 7.17 m yurgizdi, RB/X, Back/B sumka, Start →
*Saqlab, bosh menyuga* `SLICE_SAVED lvl=1 at=quit` v4, menyu *Davom etish*ni taklif qiladi), E0 80, E1 69, E2 54
(Editor Play'da menyu → yangi o‘yin → daraja), D 25 (Player 960x540 / 640x360 izi headless bilan 23/23 hodisa bir xil),
X 545 s (eksport 215 fayl 54.4 MiB; attract run 2-daraja autosave'idan keyin `ZAMIN_MAX_FRAMES` bilan to‘xtatildi
(200 XP), papka haqiqatan `mv` qilindi, yangi joydan yangi jarayon g‘alabagacha: 166.3 s, 850 XP, 4-daraja, 19 tanga,
0 o‘lim, 5/5 vazifa; uchinchi jarayonda *Davom etish* yo‘q, to‘rtinchisi yangi o‘yin 0 XP, rekord 166.3 saqlandi);
Beacon Pass P0 66, P1 178, C 58 (buzilgan → lager autosave'i; yirtilgan → toza boshlash + banner), P2 110, P3 8,
P4 398, P6 49 (stik 12.46 m, quit saqlashi v4), E0 176, E2 155, D 41 (headless'ning 26 hodisasidan birinchi 12 tasi
bir xil), X 337 s (209 fayl 55.5 MiB; hovli shrine saqlashidan keyin to‘xtatildi (100 XP), ko‘chirilgan joydan
g‘alaba: 73.6 s, 800 XP, 4-daraja, 4/4 vazifa, *Davom etish* yo‘q, yangi o‘yin 0 XP, rekord saqlandi). **Editor**:
`Tests/slice_editor_test.py` (yangi CI qadami) haqiqiy Blueprint oynasida `Gameplay.TransferWrite`, `TransferLoad`,
`Steer` qo‘shdi/saqladi/Undo qildi; LoadScene `FadeOut` 0.6 ga yozildi, UIBindHUD Kind combo'si 8 *Qobiliyat*ga
aylantirildi, AIConfigure `Strike` checkbox'i o‘chirildi — har biri saqlandi, Undo/Redo; `ZAMIN_EDITOR_GRAPH_QA`
(yangi hook) ikkala slice'ning 9 grafini oynada ochdi (Lantern Keeper Hero 560/678 node/bog‘lanish, Game 625/737; Dovon
mash'ali Hero 655/787, Game 490/583 …), hammasida 0 diagnostika, logda xato yo‘q; `Tests/editor_smoke.sh
Projects/LanternKeeper Projects/BeaconPass` (CI qadami) 243/243 va 268/268 qadam, 0 xato. **Profiling**
(`Tests/slice_bench.py`, Release, llvmpipe, 3 ishga tushirishning eng yaxshisi, `Tests/Baselines/slices-release.json`):
Player benchmark hisobotida yangi `GameCode` (update layer'i chizishsiz); 284x160 Low da o‘yin kodi Lantern Keeper Menu /
Orchards / Yertola 0.32 / 2.02 / 2.03 ms, Beacon Pass Menu / Dovon 0.53 / 3.50 ms, p95 eng ko‘pi 4.40 ms (darvoza
≤ 3 / ≤ 4 ms, p95 ≤ 6); kadr 2D da 3.48–5.67 ms (≤ 16.6), 3D da 22.2–28.2 ms (≤ 33.3); issiq sessiyada eng sekin kadr
≤ 51 ms (birinchi sessiyada llvmpipe shader JIT'i: Yertola 1.9 s, Dovon 345 ms — alohida ustunda); `--compare`
(`bench_compare.py --tolerance 15`): 2 ishga tushirishda 720p dagi 3/40 raqam shovqin bo‘lib chiqdi (qayta o‘lchashda
16 raqamda 0 sekinlashish) — shuning uchun standart 3 ishga tushirish va 720p p95 `Noisy`. **Soak** (`--soak 18000`,
attract loop, 10 o‘yin daqiqasi): Lantern Keeper 3 g‘alaba, har menyuga qaytishda 57 obyekt, RAM 302.9 MiB, engine 99.2 MiB
(o‘sish 0.0 MiB); Beacon Pass 7 g‘alaba, 278 obyekt, RAM 269.4 → 278.8 MiB va keyin tekis (o‘sish 9.5 ≤ 32 MiB),
engine 39.0 MiB; logda xato yo‘q. **Topilgan va tuzatilgan xatolar**: (1) 2-daraja o‘qilmagan saqlashdan keyin transfer
yuklash va autosave'ni muvaffaqiyatsiz yuklash bilan bir kadrda qilardi — 3 fayl amali, sahna byudjeti 2 — autosave
yiqilardi (`SLICE_IO_FAILED autosave`); endi bir kadr keyin; (2) daraja boshidagi saqlash demo pilotning `Run.Step`ini
o‘chirmasdi — ko‘chirilgan eksportda davom etgan attract run 2-darajada 1-darajaning qadamidan boshlab tiqilib qolardi
(`SLICE_BOT_STUCK step=31`); endi ikkala generatorda `Run.Step=0`; (3) o‘qilmagan saqlash jim edi — endi banner;
(4) kontent: Dovon'ning *Quyosh*ida noma'lum `rot` komponenti ogohlantirishi, Lantern Keeper Sprint'ida yaroqsiz
`Gamepad:LeftTrigger` (→ `GamepadAxis:LeftTrigger+`), banner'larda xom `[Slam]` / `[Nova]` (endi tugma nomi).
Galereya: `docs/Samples.md` ga ikki slice (`sample-lanternkeeper.jpg`, `sample-beaconpass.jpg`), launcher thumbnail'lari
yangilandi. 1347/1347 unit (2 yangi: ikkala slice'ning resume matritsasi; to‘liq run 320 s), 14/14 headless CTest (`-LE gui`, ZaminTests alohida), `ctest -L docs` 2/2, build ogohlantirishsiz; generatorlar ikki marta — fayllar bir xil. Regressiya (Player/Editor, `--exports`): Gameplay, Rules, AI, UI, Save, Quest, Loadout, Combat, Animator va Blueprint workflow'lari 10/10 (86 faza, har biri 95–202 s); kontent tuzatishlaridan keyin slice P0, P1, C, P6, D ikkala o‘yinda qayta o‘tdi. Dizayndan farqlar: alohida klaviatura fazasi (P5) yo‘q — P0 klaviatura bilan o‘ynaydi; E1 faqat
2D da; X da "o‘ldirish" `ZAMIN_MAX_FRAMES` bilan ikki saqlash orasida (yozish o‘rtasidagi kesishlar headless matritsada);
3D determinizm Player va headless izining 12 hodisalik umumiy boshi bilan; `slice_bench.py` CI'da emas (Release va tinch
mashina kerak), Player'da ajratmalar sanalmaydi (RSS va engine xotirasi qayd etiladi), 3D kadr chegarasi 33.3 ms
(llvmpipe), birinchi sessiya hitch'i darvozadan tashqarida alohida qayd etiladi; soak shrift atlaslari / VM hajmini
alohida o‘lchamaydi.

Dalillar (tuzatuvchi, sharhlovchi topilmalari): xom `{action:...}` tokenlari uchun `FindUnexpandedInputToken` va UI auditining `RawHint` qoidasi (ikkala o‘yinning Play testlarida darvoza); graf matnlari, banner'lar, HUD/statistika, daraja nomlari, buyum/vazifa/combat feed nomlari lokalizatsiya kalitlari orqali (`item.*`, `quest.*`, `combat.*`; Dovon'da endi "-2 Kuyish"), uz/en kalitlari `samples_assets.py` graf rejimida tekshiriladi; sozlamalar har yopilishda (Orqaga va Back/Escape) saqlanadi, til va to‘liq ekran `Localization.SaveSettings` / `Display.SaveSettings` bilan yangi jarayonda tiklanadi (`SliceMenuSettingsKeptOnEveryCloseAndLanguagePersists`, eski menyularda yiqiladi); qo‘riqxonada davolash saqlashdan oldin; daraja kartasi dunyoni to‘xtatadi, real vaqtda sanaydi, o‘lik qahramon bilan o‘tmaydi va `TransferWrite` xatosida 1 s dan keyin qayta urinadi (`LanternKeeperShrineSaveAndLevelExitCardHoldUp`); boss 2-fazada tiklanadi (`LanternKeeperBossFightResumesInPhaseTwo`, Dovon'da Enraged); kamera tayoqchasi tezlik × Delta, sumka ochiq bo‘lsa to‘xtaydi (`BeaconPassCameraStickTurnsAtARate`: 0.5 s da −1.200 rad 30 va 60 Hz'da, sumka ortida 0; eski graf −0.63 / −1.26); `Gameplay.Input` bo‘sh harakat nomlarini o‘qimaydi; VM har qanday engine exec natijasini saqlaydi. Vizual: Lantern Keeper'da 47 variantli blob yo‘l autotile, suv autotile, choksiz o‘t, `Bezak` qatlami; HUD energiya/XP chiziqlari kontrasti; Dovon'da tik qiyaliklar triplanar, cho‘qqilar radial mesh, o‘lim pozasi, tuman yengillashtirildi (0.0045). Oracle'lar `Tests/oracle.json` ga ko‘chdi (eksportga tushmaydi); `SLOW_TEST`: `ZaminTests --quick` 1341/1341, 61.7 s; `ZaminSliceTests` (`--slow`) 12/12, 326.6 s; `ctest -LE gui` 17/17, `ctest -L docs` 2/2; assetlar: Lantern Keeper 5.31 MiB, Beacon Pass 5.96 MiB; `slice_ui_test.py` U1/U2 ikkala o‘yinda (60 audit, 0 muammo); `slice_workflow_test.py` P0 va P6 ikkala o‘yinda o‘tdi (41–56 s).

### Yakunlangan bo‘lim — gameplay UI

- [x] Bounded modal stack, focus/pause ownership and input fencing.
- [x] HUD/rules bindings, deterministic inventory pages and live selection.
- [x] Blueprint/native API, lifecycle and real keyboard/gamepad isolation.
- [x] 2D/3D playable UI/save-resume workflow, scale and export screenshots.
- [x] Unit, sanitizer, regression, documentation and main push.

Dalillar (2026-11-12): 1160/1160 unit (42 yangi), 11/11 headless CTest, 12/12 render.
Pure UIModel/RulesModel ASan/UBSan/LSan: 10/10. UIModel, GameplayUI, GameplayUIGraph va
GameplayRuntime TU’lari instrumentlangan owner probe: 42/42, LeakSanitizer bilan; Scene,
SceneUI, SceneUIScroll, serializer, physics, VM va qolgan shared engine normal build.
Bu butun engine sanitizer run’i emas. Clang 18 syntax-only -Werror: 10 ta TU.
Real ImGui: UIConfigure/UIBindHUD/UIInventoryConfigure add/save/Undo. 2D/3D real keyboard/gamepad
Player replay (225 kadr), inventory capture (110), 1024x768 capture (110), Editor auto-play (225),
compiler-free export benchmark/screenshot (225): jami 10 workflow. Save’dan keyin HP/coin ataylab
kamayadi; Load native 75 HP / 8 coin qiymatini qaytarishi shart. Relocation --version smoke check.
Oxirgi item-tint sayqalidan keyin yana 4 ta inventory capture/oracle (1280x720 va 1024x768) o‘tdi.
Generator deterministik; hisobotlar/skrinshotlar: `build-review/GameplayUI/`. Software Mesa;
physical GPU byudjeti o‘lchanmagan. [Scope, vaqt va input shartlari](GameplayUI.md).

### Yakunlangan bo‘lim — gameplay save/load

- [x] Strict bounded versioned snapshot codec with integrity checks and stable entity keys.
- [x] Atomic slots, validated backup recovery, per-scene I/O and spawn budgets.
- [x] Health/bag/interaction/AI/pose restoration and persistent prefab-slot bindings.
- [x] 2D/3D fresh-process resume, real Blueprint authoring, Player/export/screenshots.
- [x] Native, sanitizer, regression, documentation and main push.

Dalillar (2026-11-12): 1118/1118 unit (41 yangi), 11/11 headless CTest, 12/12 render.
Pure SaveModel/RulesModel ASan/UBSan/LSan: 14/14. SaveModel, SaveWorld, SaveGraph va
GameplayRuntime TU’lari instrumentlangan owner probe: 41/41, LeakSanitizer bilan; Scene,
serializer, physics, VM va qolgan shared engine normal build. Bu butun engine sanitizer run’i emas.
Clang 18 syntax-only -Werror: 9 ta TU. Real ImGui: SaveBind/SaveWrite/SaveLoad add/save/Undo.
SaveWorkshop generatori deterministik, konfiguratsiya mosligi va root-only slot shartlari
[GameplaySave.md](GameplaySave.md) da. Hisobot/skrinshotlar: `build-review/GameplaySave/`.
2D/3D Player write/fresh-process resume/corrupt-primary backup, Editor write/resume va compiler-free
export write/resume/real relocated resume: 150 kadrdan, jami 12 workflow tekshiruvi.
Native HP/bag/used-door/AI arrivals/player pose/prefab-slot oracle’lari o‘tdi; relocation faqat
--version bilan cheklanmagan. Software Mesa; physical GPU vaqt byudjeti o‘lchanmagan.

### Yakunlangan bo‘lim — AI gameplay

- [x] Pure patrol/chase/investigate/attack/return FSM, memory/leash and attack hysteresis.
- [x] Shared hostile perception/explicit strike, LOS/team filtering and bounded scene budgets.
- [x] Native/Blueprint AI configuration/state/events, navigation ownership, recovery, cancellation and lifecycle.
- [x] 2D/3D NPC prefab/course, physical movement/combat, Editor/export and screenshot acceptance.
- [x] Unit, sanitizer, regression, documentation and main push.

Dalillar (2026-11-12): 1077/1077 unit (38 yangi), 11/11 headless CTest, 12/12 render.
Pure BrainModel ASan/UBSan/LSan: 14/14. BrainModel, GameplayAI, GameplayAIGraph, GameplayRules,
GameplayRuleGraph va GameplayRuntime TU’lari instrumentlangan owner probe: 24/24, LeakSanitizer
bilan. Scene, serializer, physics, VM va qolgan shared engine ushbu probe’da normal build;
bu butun engine sanitizer run’i emas. Clang 18 syntax-only -Werror: 11 ta TU.
2D/3D Player input replay va Editor auto-play: 420 kadrdan; compiler-free export benchmark
420 / screenshot 450 kadrdan, jami 6 workflow. Patrol/chase/attack/investigate/return/dead/spawn
va canonical HP/prefab-nav oracle’lari o‘tdi. Relocation launcher --version bilan tekshirildi.
Haqiqiy ImGui authoring: AIConfigure/AIState/Sense add/save/Undo. AIWorkshop generatori deterministik.
Hisobotlar va skrinshotlar: `build-review/AIGameplay/`. Software Mesa ishlatildi;
physical GPU byudjeti o‘lchanmagan. AI runtime-only, konfiguratsiya graph assetlarda saqlanadi.
[AI shartlari va cheklovlari](GameplayAI.md).

### Yakunlangan bo‘lim — health, combat, inventory va interaction

- [x] Scene-owned shared state, bounded transactional health/shield/damage/heal and cooldown clocks.
- [x] Death, checkpoint-aware respawn, motor gating and bounded rule event polling.
- [x] Bounded inventory, transactional key/reward/heal interactions, deterministic range/facing/LOS queries.
- [x] Blueprint/native APIs, melee attack selection and real 2D/3D combat/collectible courses.
- [x] Unit/lifecycle/physics, sanitizer, UI, Player/export, screenshots, docs and main push.

Dalillar (2026-11-11): 1039/1039 unit (51 yangi), 11/11 headless CTest, 12/12 render.
Health/inventory leaf ASan/UBSan/LSan: 20/20. RulesModel, GameplayRules, GameplayRuleGraph va
GameplayRuntime TU’lari instrumentlangan owner probe: 51/51, LeakSanitizer bilan; Scene,
serializer, physics, VM va qolgan shared engine bu mahalliy probe’da normal build.
Clang 18 syntax-only -Werror: 8 ta TU; bu butun engine sanitizer run’i emas.
Haqiqiy ImGui authoring: Damage/InteractionConfigure/Respawn add/save/Undo. 2D/3D Player input
replay va Editor (240 kadr), compiler-free export benchmark (240) / screenshot (270): jami 6 ta
workflow, death/respawn/key/kill/heal/unlock/coin/finish hamda canonical HP/bag/guard oracle’lari.
Relocation launcher --version orqali tekshirildi. Yangi pause-budget va skipped-dead-frame life
invalidation regressiyalari ham tekshirildi. Eski SceneLoadTimingBreakdown testining worker scheduling’ga
bog‘liq multi-frame taxmini activation gate bilan deterministik qilindi; 10/10 mustaqil run o‘tdi.
Hisobot va rasmlar: `build-review/GameplayRules/`. Software Mesa; physical GPU byudjeti o‘lchanmagan.
Graph configure input’lari saqlanadi; health/inventory holati runtime-only. [Shartlar](GameplayRules.md).

Gameplay save/load yuqoridagi paketda davom ettirildi. To‘liq ECS/VM/physics snapshot,
avtomatik ixtiyoriy obyekt resurrection va katta dunyoning async saqlashi alohida qoladi.

Gameplay UI, quest/progression, equipment/abilities va projectiles/gameplay effects yuqoridagi paketlarda
birlashtirildi. Keyingi yirik paket — ikki tugallangan 2D/3D vertical slice va stress darvozalari; acceptance
darvozalari yuqorida belgilangan. To‘liq ECS/physics snapshot, renderer va katta dunyo vazifalari umumiy rejada
saqlanadi.

### Yakunlangan bo‘lim — motor, kamera, locomotion va checkpoint

- [x] 2D/3D motor: input, acceleration/deceleration, analog clamp, jump buffer/coyote/cut va lifecycle stop.
- [x] 2D follow/3D orbit camera, damping, bounds, obstruction va restore snap.
- [x] Actual physics velocity → typed Animator binding, reusable Blueprint authoring.
- [x] Bounded checkpoint codec, atomic persistent save/load va physics-aware restore.
- [x] O‘ynaladigan 2D/3D namunalar, UI/runtime/export, regression/sanitizer/screenshots va main push.

Dalillar (2026-11-11): 988/988 unit (46 yangi), 11/11 headless CTest, 12/12 render.
Motor/camera/codec leaf ASan/UBSan/LSan: 22/22. Gameplay uchta TU, graph VM/model/function va
host instrumentlangan owner probe: 46/46, LeakSanitizer bilan. Scene/serializer/physics va qolgan
shared engine bu mahalliy probe’da normal build; bu butun engine sanitizer run’i emas.
Clang 18 syntax-only -Werror: 7 ta o‘zgargan TU. 2D/3D Player input replay (180 kadr), fresh-process
resume (60), Editor auto-play (180), compiler-free export benchmark (180) va screenshot run (210)
movement/jump/checkpoint/goal/restore oracle’laridan o‘tdi. Relocation launcher --version orqali tekshirildi.
Haqiqiy ImGui authoring: motor/camera/checkpoint node search, save va Undo. CI ushbu ikki workflow’ni
qaytaradi. Hisobot va rasmlar: `build-review/GameplayPackage/`; software Mesa, hardware byudjeti o‘lchanmagan.

Bu paket movement → physics → animation → camera → checkpoint zanjirini yakunlaydi.
Motorlar Rigidbody2D/CharacterController3D ustida ishlaydi; checkpoint actor transformini saqlaydi.
To‘liq inventory/ECS snapshot, humanoid retargeting va cinematic camera bu paketga kirmaydi;
checkpoint clearance to‘liq capsule-fit emas, markaziy probe. Shartlar [Gameplay.md](Gameplay.md) da.

Reusable health/damage/death/respawn, interaction, combat/collectible va Blueprint workflow
yuqoridagi gameplay rules paketida davom ettirildi.
P2 kontent va P3/P4 world/renderer vazifalari yuqoridagi umumiy rejada saqlanadi.

### Yakunlangan bo‘lim — Animator gameplay va 2D blend

- [x] Cartesian 2D blend solver, skeletal pose/root motion/event integration and synchronized phase.
- [x] Typed parameters/damping, priority and fade interruption policy, self restart and model-free state clocks.
- [x] Serialized configuration, validation, editor blend field/live telemetry and transition controls.
- [x] Blueprint Animator operations, real 2D/3D gameplay samples and export acceptance.
- [x] Regression, sanitizer, screenshots, documentation and main push.


Dalillar (2026-11-11): 942/942 unit (31 yangi), 11/11 headless CTest, 12/12 render.
CPU solver/controller ASan/UBSan: 20/20. Yangi blend/controller/Blueprint bridge hamda uchta VisualGraph
TU’si instrumentlangan owner probe: 31/31, LeakSanitizer bilan; Scene/serializer va qolgan shared engine
bu mahalliy probe’da normal build. Clang 18 syntax-only -Werror: 11 ta o‘zgargan TU.
Real ImGui: sample drag/save/Undo, 2D state yaratish/Undo. Player/editor 120 kadrdan ikkala sahnada:
Action ko‘rildi, Locomotion qaytdi, damping/blend weight0 = 0.63212055 va oldingi Blueprint total 12/18.
Ikkala eksportda haqiqiy gameplay logi, PNG va ko‘chirilgan launcher tekshirildi. Dalillar:
`build-review/AnimatorGameplay/`. Mesa software rasterizer; hardware performance o‘lchanmagan.
Editor smoke: 490/490 qadam (224 Showcase3D + 266 Platformer2D), 0 xato. Main push bilan yakunlandi.

Keyingi bo‘lim: input/controller/camera va haqiqiy velocity → Animator parametr bog‘lanishi bilan
2D/3D gameplay vertical slice, checkpoint/save hamda o‘ynash/eksport acceptance’i.

### Yakunlangan bo‘lim — Blueprint gameplay acceptance

- [x] Typed function signatures, parameters/results, isolated locals, nested/delayed calls and bounded recursion.
- [x] All seven typed arrays, safe operations and snapshot ForEach; literal/value/event memory limits.
- [x] Named local events, validated dynamic pins/scopes, runtime diagnostics and call stack.
- [x] Editor signature/array authoring, multi-select/copy/paste/duplicate/delete, rename and undo.
- [x] Compiler-free 2D/3D workshop, real UI/runtime/export oracles, sanitizer/regression, docs and main push.

Bu bo‘lim gameplay graph’ning author → validate → run/debug → export zanjirini yakunlaydi.
Network RPC, graph inheritance va native code generation P6/toolchain bosqichlariga tegishli;
ular shu acceptance uchun yashirin “qolgan ish” sifatida sanalmaydi.

Tekshiruv (2026-11-11): 911/911 unit (31 yangi), Blueprint focused 31/31, headless CTest 11/11.
Clang 18 syntax-only -Werror: 7 ta o‘zgargan TU. Graph VM/model/function va 30 ta CPU test
ASan/UBSan/LeakSanitizer bilan tekshirildi; leaf probe faqat fayl yozish uchun kichik stub ishlatadi,
butun engine va diskdagi atomik yozuv bu sanitizer probe’da instrumentlanmagan. Haqiqiy I/O va sahna
lifecycle normal unit to‘plamida tekshirildi. Dalillar: `build-review/BlueprintCompletion/`.
Real ImGui authoring testi typed wire/rejection, Undo/Redo, copy/paste/duplicate/delete va guruh tahririni tekshiradi.
2D/3D workflow compiler va native game modulisiz parametr/lokal/Delay/ForEach/custom event zanjirini
12/18 natija bilan bajaradi. CI ushbu workflow, eksport va editor authoring tekshiruvlarini qaytaradi.

Animatsiya state machine/blend va gameplay integratsiyasi yuqoridagi Animator paketida davom ettirildi.

### Yakunlangan katta paket — dynamic obstacle va bounded recovery

- [x] Same-frame world atlas publication after carving; synthetic mosaic excluded from carve feedback.
- [x] Recovery watchdog: real progress, pending/portal pause, bounded retry/cooldown/exhaustion and reset.
- [x] Serialize/copy/reset, inspector and Blueprint recovery diagnostics/manual retry.
- [x] Live barrier 2D/3D demos, portal/async/physics regression, sanitizer, screenshots and native exports.
- [x] CI failure audit, verified evidence, upstream check and main push.

Mahalliy dalillar (2026-11-11): 880/880 unit (19 yangi), 138/138 focused, 11/11 headless CTest,
12/12 render, 490/490 editor smoke, OpenGL 4.1 residency GPU. Clang 18 syntax-only -Werror: 14 ta TU.
NavRecovery leaf ASan/UBSan: 10/10. Recovery, source carving, world movement va world service TU’lari
hamda focused testlar instrumentlangan owner probe: 138/138; qolgan shared engine normal build,
bu butun engine sanitizer run’i emas. 2D/3D Player 420 kadrdan; editor 160 kadrdan, 8-node/9-link/
4-variable graph va yuqorida jonli Exhausted/Phase/Retries. Ikkala native export 420 kadr, oracle,
PNG va relocation’dan o‘tdi. Oldingi NavAgent/portal Player regressiyasi 420, crowd 450 kadrdan
ikkala o‘lchamda o‘tdi. Dalillar: `build-review/RecoveryNavigationPackage/`.

763a7b0 remote coverage dependency bosqichida Azure va official Ubuntu mirror DNS xatosi bilan
to‘xtagan; bu C++ test failure’i emas. Installer cached paketlarni qayta o‘rnatmaydi, ikkala DNS
ishlamasa tezda aniq xabar beradi va butun update/install command’lari timeout bilan chegaralangan.
Mock regression real apt/sudo yoki host mirror fayllarini o‘zgartirmaydi. Yangi remote CI push’dan
keyin alohida kuzatiladi; tashqi DNS ishlashiga kod kafolat bermaydi.

Chegaralar: opt-in recovery; physics-only to‘siq olib tashlansa explicit retry, NavObstacle/topology
o‘zgarsa avtomatik reset. Atlas build owner’da, hard frame deadline/hardware profiling yo‘q.
Keyingi paket: animation locomotion va gameplay vertical slice; katta scene uchun profiler bilan
aniq acceptance, streaming/query vaqtlarini o‘lchash.

### Yakunlangan katta paket — world smoothing va crowd steering

- [x] Sparse atlas segment clearance: holes, corners, floor/climb, bounded work.
- [x] Worker-side bounded smoothing; weighted costs and explicit portal endpoints preserved.
- [x] Deterministic snapshot crowd steering, spatial broadphase, nearest-neighbour and candidate caps.
- [x] World NavAgent integration, conservative movement clipping, serialized settings, inspector and graph telemetry.
- [x] 2D/3D crowd demos, regression/sanitizer/runtime/export screenshots, upstream check and main push.

Mahalliy dalillar (2026-11-11): 861/861 unit (22 yangi), 119/119 focused, 10/10 headless CTest,
12/12 render va 490/490 editor smoke qadam; OpenGL 4.1 residency GPU. Clang 18 syntax-only -Werror:
14 ta TU. Crowd/atlas/segment/grid leaf ASan/UBSan: 17/17, butun engine sanitizer run’i emas.
2D/3D crowd Player 450 kadrdan va editor 130 kadrdan: 4 agent, ikki manzil, Stop/reverse, graph telemetry,
separation; 5-node/6-link/3-variable graph. Ikkala native export 450 kadr, oracle, PNG va relocation’dan o‘tdi.
Oldingi NavAgent/portal/unload/return Player regressiyasi ikkala o‘lchamda 420 kadrdan o‘tdi.
Dalillar: `build-review/CrowdNavigationPackage/`. Software Mesa tekshiruvi; hardware performance o‘lchanmagan.

51eead3 remote CI dependency bosqichida Azure Ubuntu mirror DNS xatosi sabab to‘xtagan, C++ tekshiruvi
o‘sha run’da bajarilmagan. Shared bounded apt retry/timeout va official archive fallback qo‘shildi;
workflow YAML/bash syntax tekshirildi. Yangi remote CI holati push’dan keyin kuzatiladi.

Chegaralar: conservative swept rectangle va bounded string pulling, world spline emas. Crowd velocity
sampling, ORCA yoki collision kafolati emas; physics yakuniy vakolatga ega. Dense overflow stop/diagnostic
bilan ko‘rinadi. Keyingi paket: dynamic obstacle/repath recovery, ko‘p agentli profiling va animation locomotion.

### Yakunlangan katta paket — bounded async world navigation

- [x] CPU-only worker queue, immutable snapshot lifetime, FIFO start/publication budgets.
- [x] Active cancellation, replacement, retained snapshot/result byte budgets, bounded shutdown.
- [x] NavAgent async admission, pending stop, stale destination/start/revision rejection, cancel/unload/destruction.
- [x] Policy serialization, editor queue diagnostics, real 2D/3D graph/controller demos.
- [x] Concurrency/regression, sanitizer, screenshots, native export va main push.


Mahalliy dalillar (2026-11-11): 839/839 unit (19 yangi), 97/97 yo‘naltirilgan test,
10/10 headless CTest, 12/12 render, 490/490 editor smoke qadam, OpenGL 4.1 residency GPU.
Clang 18 syntax-only -Werror: 9 ta o‘zgargan TU. Queue/atlas/grid va queue testlari to‘liq
instrumentlangan ASan/UBSan leaf probe: 10/10; bu butun engine sanitizer run’i emas.
2D/3D Player 420 kadrdan, editor 120 kadrdan 13-node/16-link/3-variable graph bilan o‘tdi.
Ikkala native export 420 kadr, async agent oracle, screenshot va relocation’dan o‘tdi.
Oldingi 2c8585c CI barcha tekshiruvlarda yashil. Dalillar: `build-review/AsyncNavigationPackage/`.

Chegaralar: scratch allocator/RSS va qattiq frame-time deadline bu payload byudjetlariga kirmaydi;
atlas build owner thread’da. Harakat sequential corner tracking; local avoidance, yo‘lni silliqlash,
jump/elevator animatsiyasi va umumiy job scheduler keyingi paketlarga qoladi.
Keyingi paket: collision saqlangan holda route smoothing va ko‘p agentli local avoidance.

### Yakunlangan katta paket — NavAgent, controller va Visual Graph traversal

- [x] NavAgent opt-in sparse atlas, bounded query, actual-position corner tracking, stale replan.
- [x] Character controller collision, safe exit occupancy, revision/link completion token va cancel.
- [x] Serialize/copy/reset, C++ API, inspector runtime holati.
- [x] Visual Graph world destination, traversal state/event, complete/cancel va success outputs.
- [x] 2D/3D haqiqiy agent/graph demo, runtime screenshot, regression va native export.
- [x] Test dalillari, hujjatlar, upstream merge tekshiruvi va main push.

7d12f4a remote CI: format, GCC, Clang, ASan, UBSan va coverage to‘liq yashil.

Mahalliy dalillar (2026-11-11): 820/820 unit (10 yangi), 10/10 headless CTest, 12/12 render,
490/490 editor smoke qadam, GL 4.1 residency GPU; Clang 18 syntax-only -Werror 12 fayl.
Agent/VM TU va testlar instrumentlangan targeted ASan/UBSan probe: 78/78; shared engine normal
build, bu to‘liq engine sanitizer run’i emas. Fixed capture 30 bilan 2D/3D Player 420 kadrdan uchta
haqiqiy traversal/arrival, disable/unload/return; har editor 120 kadrdan 10-node/14-link graph preview.
Ikkala native export ham 420 kadr, agent/graph oracle, screenshot va relocation’dan o‘tdi.
Tarjima lug‘atidagi yangi inspector keys va float overflow oldidan mode mutatsiyasi regressiyasi tuzatildi.
Dalillar: `build-review/AgentTraversalPackage/`. Yangi remote CI push’dan keyin alohida kuzatiladi.

Scope: opt-in sequential world queries, per-query expansion cap; corner route. Character exit
occupancy checked; Rigidbody traversal completion, world local avoidance/smoothing, jump/elevator
animation va async query scheduling keyingi ishlar. Transform/controller BaseOffset farqi hisobga olingan.


### Yakunlangan katta paket — sparse navigation va traversal workflow

- [x] Faqat yuklangan tile hajmiga bog‘liq immutable atlas; bounded va deterministik route query.
- [x] Ikki/uch o‘lcham: tile chegarasi, floor/climb, cost, carve va partial path qoidalari.
- [x] Yo‘nalishli, narxli, o‘chiriladigan traversal link; yetishmayotgan endpoint xavfsiz inactive.
- [x] Route follower: link oldida to‘xtash, explicit completion, stale topology’da stop/replan.
- [x] World runtime query/API, editor route diagnostikasi, haqiqiy 2D/3D demo va screenshot.
- [x] Regression, build, export, hujjatlar va main push.

Oldingi 73a683c remote CI: GCC, Clang, ASan, UBSan, coverage va format to‘liq muvaffaqiyatli.

Dalillar (2026-11-11): 810/810 unit (18 yangi), 10/10 headless CTest, 12/12 reference render,
490/490 editor smoke qadam, GL 4.1 residency GPU oracle; Clang 18 syntax-only -Werror 7 fayl.
Atlas/grid/pathfinder/mosaic/follower uchun alohida instrumentlangan ASan/UBSan 16/16 test;
bu to‘liq engine sanitizer run’i emas. 2D/3D Player 420 kadrdan portal/disable/stale/unload/return,
ikkala editor 120 kadrdan route preview. Oldingi camera tour’lar 660 kadrdan (har birida 6 complete
cross-cell path), checkpoint 540 kadrdan edit/delete/adopt/unload/return/restore. Ikkala native export
420 kadrdan navigation oracle, screenshot va relocation’dan o‘tdi. Dalillar: `build-review/NavigationPackage/`.

Snapshot build sinxron va bounded; incremental bake hali keyingi ish. Traversal follower kinematic;
character collision/animation caller’da, eski NavAgent dense mosaic’dan foydalanadi. Keyingi navigation
integratsiyasi: NavAgent/character controller va Visual Graph traversal tugunlari. Polygon navmesh alohida.


### Yakunlangan yirik paket — world authoring va checkpoint workflow

- [x] Xavfsiz checkpoint format: bounded cell snapshots, world identity, atomic save, buzilgan faylda mutatsiya yo‘q.
- [x] Runtime: unload/return state, deleted/spawned entity, ownership API, root local/world koordinatalari.
- [x] Asinxron restore: snapshot JSON worker’da parse, eski avlodni cancel, bosqichma-bosqich world reset.
- [x] Mualliflik: yangi 2D/3D grid bundle, terrain/nav/camera, unique UUID, no-overwrite transactional publish.
- [x] Editor: world wizard, runtime entity edit/adopt/delete, checkpoint save/restore/reset va status.
- [x] Demo: F5/F9, qayta kirishda state va disk restore oracle, haqiqiy screenshot.
- [x] Regressiyalar, ASan stack tahlili, export, hujjatlar, main push.

Serialized component state saqlanadi; arbitrary C++ stack/heap va fizik replay checkpoint emas.

Dalillar (2026-11-11): 792/792 unit (20 yangi), 10/10 headless CTest, 12/12 render reference,
490/490 editor smoke qadam. 2D/3D Player’da 540 kadrdan edit/delete/adopt/unload/return/disk restore oracle;
ikkala editor’da 6-cell wizard, no-overwrite, project link, generated Play va actor restore; generated 3D
loyiha native export/run/screenshot/relocation. Checkpoint store/writer uchun alohida instrumentlangan
ASan/UBSan probe’da 6/6 test (to‘liq engine sanitizer run’i emas), Clang 18 syntax-only -Werror 13 fayl.
Oldingi CI 625d750 GCC/Clang/UBSan/coverage’dan o‘tgan; ASan’dagi Mesa 2×56-byte drawArrays JIT retention
artifact stack bo‘yicha tor vendor suppression bilan ajratildi. Yangi remote CI push’dan so‘ng alohida kuzatiladi.
Dalillar: `build-review/CheckpointPackage/`. Serialized component snapshot arbitrary C++ heap/physics replay emas;
restore validation va capture CPU qismi hali hard frame deadline bermaydi.

### Yakunlangan katta paket — residency, terrain va navigation integratsiyasi

- [x] CI: barcha CMake module output joylarida ishlaydigan camera tour; vendor leak istisnosini tekshirish.
- [x] Resurslar: texture/model/material/mesh LRU, jonli reference va async load pin, chegaralangan tozalash.
- [x] Xotira: haqiqiy RSS va tracked GPU allocation alohida; high/low watermark bilan yangi admission nazorati.
- [x] Terrain: world-space generator, turli resolution chegaralarini ulash va tashqi patch LOD himoyasi.
- [x] Navigation: yuklangan katak gridlarini bounded mosaic orqali ulash, teshik va balandlik qoidalari.
- [x] Editor/runtime: cache va pressure diagnostikasi, qo‘lda trim, yangilangan demo va haqiqiy screenshot.
- [x] Sinov: cancellation/pinning/pressure/seam/path regressiyalari, build/export, main push.

Bu paket physical VRAM hard cap, polygon navmesh yoki Unreal bilan tenglik da’vosi emas.

Tekshiruv (2026-11-10): 772/772 unit, barcha 10 headless CTest, 12/12 render reference;
OpenGL 4.1 real texture/model/material/mesh residency va 32 cycle; 3D/2D camera tour 660 kadrdan,
har birida 6 ta cross-cell complete path; terrain pair editor Undo/Redo; 490/490 editor smoke qadam;
ikki demo native export, screenshot va relocation. Clang 18 syntax-only `-Werror` tekshiruvi ham o‘tdi.
Qayta yuklashdagi asset resolve cursor tiklanmasligi tuzatildi; entity soni 3→1→4→2→0→3 bilan sinaldi.
Qo‘lda va avtomatik trim bitta world tick allowance’ini bo‘lishadi; thumbnail/reimport LRU metadata’si ham tozalanadi.
Dalillar: `build-review/ResidencyPackage/Final/`. Remote compiler/ASan matritsasi push’dan keyingi CI’da alohida tekshiriladi.

Keyingi P3 ishlar: incremental nav bake, terrain normal seam, HLOD, floating origin va versioned gameplay/physics persistence. Serialized cell checkpoint workflow bajarildi.

### Yakunlangan paket — packed foliage GPU culling va LOD

- [x] Bitta komponentda 65536 tagacha statik instance, ikki model/mesh LOD, tint va ko'rish masofasi.
- [x] OpenGL 4.3 compute + SSBO compaction + indirect draw; OpenGL 4.1 CPU culling/instancing fallback.
- [x] LOD hysteresis, konservativ birlashgan bounds, kamera harakatida statik input qayta yuklanmasligi.
- [x] Asinxron GPU visibility hisoblari va CPU/GPU buffer hajmi; rendering yo'lida blocking readback yo'q.
- [x] Scene/prefab/copy/undo serialization, model UUID havolalari, inspektor va parent terrain sirtiga joylash.
- [x] CPU/GPU image/count oracle, shadow/picking/wire va ikkala capability holatini yakuniy tekshirish.
- [x] Zich demo sahna, screenshot, eksport va regressiyalar.

V1 chegaralari: statik opaque/cutout meshlar, 128 part/LOD; bitta komponent picking ID. Soya yaqin LOD'dagi barcha instance'larni konservativ chizadi; alohida GPU shadow culling yo'q. Ko'rinadigan selection konturi ID buferidan olinadi; packed occluded silhouette chizilmaydi. Collider/gameplay entity'lari har instance uchun yaratilmaydi. HLOD/streaming alohida bosqich.

Tekshiruv (2026-11-10): 711/711 unit, 12/12 reference render; GL 4.3 compute va GL 4.1 CPU oracle (4096 instance, besh kamera, asosiy tasvirlarda 0 pixel farqi), multi-part draw, shadow/picking/wire va backend switch o'tdi. Birinchi nazorat ko'rinishida 491520 o'rniga 54756 triangle; kamera source upload sonini o'zgartirmadi. FoliageWorkshop 4096 instance/5 entity bilan save/reload, eksport, Player screenshot va relocation'dan o'tdi; ikki OBJ va 12 PBR xaritasi paketga kirdi. Legacy terrain reproject packed root'ni siljitmasligi CPU regressiyasida tekshirildi. SDK 2 test va UI'siz runtime o'tdi. Dalillar: `build-review/PackedFoliage/`. Mesa llvmpipe natijalari physical GPU tezlik bahosi emas.

### Yakunlangan yirik paket — ishlaydigan 2D/3D world streaming

Bir paketning natijasi: kamera bo‘yicha yuklanadigan kataklar, umumiy sahnada fizika/script lifecycle,
editor nazorati va eksport qilinadigan 2D/3D demo. Quyidagi bandlar mahalliy test dalili bilan yopildi; remote sanitizer/compiler matritsasi push’dan keyin alohida tekshiriladi.

- [x] Runtime: radius/hysteresis, layer/dependency closure, slot/entity/declared memory admission.
- [x] Bulk import: barcha obyektlar tayyor bo‘lgach runtime state boshlanadi; umumiy 2D/3D fizika, unload va rollback.
- [x] Host integratsiyasi: Project.StreamingWorld, SceneManager frame chegaralari, Stop/Single/restart xavfsizligi.
- [x] Editor: world manifest/policy tahriri, holatlar gridi, runtime statistika, retry/preview.
- [x] 2D va 3D ko‘p katakli namuna, kamera yurishi va boundary/cancel qaytish regressiyasi.
- [x] Export: manifest va unga tegishli scene/asset closure, toza joyga ko‘chirilgan Player sinovi.
- [x] CPU/GL/editor/runtime/SDK testlari, screenshot va yakuniy hisobot; barchasi bitta paket sifatida main'ga push.

### Yakunlangan paket — dunyo kataklari va streaming asosi

- [x] 2D tilemap va 3D terrain uchun umumiy world-cell manifest, barqaror UUID va dependency chegaralari. `.zworld` v1 metadata, bounded parse, atomik save va xavfsiz scene yo‘llari; editor/runtime streaming hali ulanmagan.
- [x] Main thread'dan tashqarida bounded scene IO/parse; avlodli so‘rov va CPU staging cancel/discard. `WorldCellLoader` v1: dependency closure, bounded input admission va symlink-safe worker IO. Aktiv sahna unload, texture decode va renderer/residency integratsiyasi keyingi bandlarda.
- [x] Main thread’da soft build/resolve va activation/unload count byudjeti; umumiy sahnada physics/script lifecycle.
- [x] Texture/model decode/upload uchun qat’iy resurs/fayl byudjetlari; hard frame-time acceptance. `Streaming.AssetStreaming` (default o‘chiq): decode/parse va mip zanjiri `WorldAssetStager` worker’larida, GPU upload `AsyncLoader` kadr byudjeti bilan qatorlab; katak hamma asset GPU’da bo‘lgach faollashadi.
- [x] RAM/VRAM residency limiti; tez kamera harakati, qaytish, bekor qilish va yo'q/buzilgan katak regressiyalari. `EvictOnPressure`: kuzatilgan RSS / so‘ralgan GPU baytlari limiti (jismoniy VRAM emas), o‘lchangan GPU hajmi bo‘yicha oldindan rad etish.
- [x] Ko'p katakli demo, save/reload/export, qayta bajariladigan kamera yo'li va RSS/frame-time hisobotlari. `Projects/WorldGallery3D`, `.ztour` + `ZAMIN_WORLD_TOUR` / `ZAMIN_WORLD_REPORT`, `Tools/make_world_gallery.py`.

Dalillar (2026-11-23, GCC Debug, Mesa llvmpipe 2 thread, 4 CPU, xvfb; batafsil — [Quality.md](Quality.md#the-world-asset-streaming-package--2026-11-23)):
1368/1368 unit (15 yangi; `--quick` 1356/1356), GUI’siz CTest 17/17, docs 2/2, world/residency GUI CTest 11/11
(uchta yangi `WorldAssetStreaming_*` bosqichi va kengaytirilgan `AssetResidencyGPU` bilan), 12/12 reference render,
EditorSmoke; build ogohlantirishsiz.
**Byudjet** (`ctest -R WorldAssetStreaming_budget`, 24 katak, 2048² texture = mip bilan 16 MiB, 5184 uchburchakli
model, 1080 kadrli tur, kadrga 2 MiB / 8 upload): asosiy oqimda 0 rasm decode / 0 model parse (eski yo‘lda 118 / 8);
har kadrda yuborilgan bayt ≤ 2 097 152 (cho‘qqi aynan shu), chaqiruv ≤ 2, byudjetdan katta upload 0, staging
cho‘qqisi 256 MiB = limit; bir kadrdagi streaming ishi (world update + upload) ko‘pi bilan **13.99 ms**, eski yo‘lda
88.66 ms (gate: ≤ 30 ms va eski yo‘lning yarmidan kam; eski yo‘l gate’dan o‘tmaydi — 108 kadr 30 ms dan yuqori);
kadr vaqti p50/p95/p99 32.79/44.57/51.53 ms, eski yo‘lda 35.57/68.17/103.11; medianadan 40 ms uzun 1 kadr
(streaming ishisiz) / eski yo‘lda 39 (38 tasi streaming bilan). 8 MiB katak byudjetida 17 staging nom bilan rad
etildi, 0 upload. GL: worker mip zanjiri `glGenerateMipmap` dan 0 farq, yarim yuklangan texture ko‘rinmaydi,
bekor qilingan upload GPU allocation’ini qaytaradi.
**Limitlar** (`ctest -R WorldAssetStreaming_residency`): ikki davrali limitsiz tur (2160 kadr, tez kamera,
bo‘sh hudud, qaytish) — 105 faollashtirish / 97 unload / 30 bekor qilish, 0 failed, bo‘sh hududda so‘ralgan GPU
342 → 342 MiB, davra cho‘qqisi 406 → 406 MiB, RSS cho‘qqisi 819 → 819 MiB; GPU limiti 325.8 MiB — cho‘qqi
325.6 MiB, 23 katak yuklanmasdan rad etildi, 2 tasi chiqarildi, 7 tagacha faol; RSS limiti 667 MiB — 115 bosim
kadri, heap 37 marta OS’ga qaytarildi, cho‘qqi 709 MiB (+42 MiB: kuzatuvga asoslangan, qat’iy shift emas),
9 tagacha faol; buzilgan world — yo‘q va buzilgan katak aynan 2 failed, buzilgan/yo‘q texture va buzilgan model
9 marta xabar qilindi, qolganlari faol; hamma turda asosiy oqimda 0 decode/parse.
**Demo** (`ctest -R WorldAssetStreaming_demo`): WorldGallery3D generator chiqishi bilan bayt-bayt bir xil; ikki
tur — bir xil kamera yo‘li va tinch nuqtalarda 8/4/5/0/8 faol katak, 79 faollashtirish / 71 unload; kadr vaqti
p50 66.86 / p95 98.11 / p99 111.84 ms, RSS 303 → 326 MiB, so‘ralgan GPU 100 → 110 MiB; editor world paneli
screenshot bilan; eksport 190 fayl / 52.4 MB, ko‘chirilgan papkada shu tur bir xil natija (p50 69.91 ms, RSS
cho‘qqisi 295 MiB, 0 o‘qilmagan fayl); save/reload va eksport rejasi unit testda (24 katak, 144 entity).
Dalillar: lokal `build/WorldAssetStreaming/Final/`.

Chegaralar (bajarilgan deb belgilanmaydi): jismoniy GPU va jismoniy VRAM o‘lchovi; llvmpipe tezlik bahosi emas.
Kadr vaqti gate’i streaming ishini chegaralaydi — renderer shader variantini birinchi marta kompilyatsiya qilgan
kadr (337 ms, streaming ishi 0.2 ms) undan tashqarida. GL obyektini yaratish/yo‘q qilish vaqti drayverniki
(llvmpipe’da bitta upload bo‘lagi 14 ms gacha, GPU limiti turida 20.5 ms). Terrain qatlam texture’lari, shriftlar,
animatsiya kliplari va model ichidagi rasmlar decode’i asset byudjetiga kirmaydi; mesh bo‘linmaydi (kadr
byudjetidan kattasi rad etiladi). RSS limiti hodisadan keyin ishlaydi. Remote compiler/sanitizer matritsasi
ishga tushirilmagan.

Packed foliage uchun qoladigan acceptance: GPU shadow culling, individual instance authoring/Undo, billboards/HLOD, physical GPU va OS/driver matritsasi. Mavjud v1 bularni tugallangan deb belgilamaydi.


### SDK himoyasi, profiling va foliage muallifligi v1 — bajarilgan paket

1. **SDK/modul mosligi:** SDK headerlari va toolchain fingerprint'i, modul ichidagi ELF ABI record, `dlopen`dan oldingi bounded parser. Belgisiz, buzilgan yoki mos kelmagan modul rad etiladi; ishlab turgan modul saqlanadi. SDK headeri o'zgargan bo'lsa avval engine qayta build qilinadi.
2. **Regressiya va editor diagnostikasi:** mos/noto'g'ri/eski/strip qilingan ELF, malformed chegaralari, constructor bajarilmasligi, ishlayotgan modulni saqlash, SDK orqali build/hot reload va eksport. Editor screenshot bilan holat ko'rsatiladi.
3. **Profiling:** mavjud CPU/frame/memory hisoblagichlarini tekshirish; GPU timer query va frame-time ko'rinishi; takrorlanadigan 2D/3D benchmark sahnalari. VPS correctness va physical GPU performance dalillari alohida yuritiladi.
4. **Foliage:** mavjud instancing yo'lini o'lchash; zichlik/LOD/culling va terrain sculpt'dan keyin obyektlarni sirtga qayta joylash. Kichik va zich map sinovlari, save/load/undo va screenshot.
5. **Yakun:** tegishli CPU/GL/SDK/editor/export testlari; hujjatlar va natijalarni yangilash. Keyingi paketlar streaming, gameplay authoring va ikki tugallangan o'yin mezonlari bo'yicha davom etadi.

- [x] SDK fingerprint va bounded ELF preflight.
- [x] Xato reload'da ishlayotgan modulni/klass registry'sini saqlash va aniq diagnostika.
- [x] Real SDK/modul/hot reload/export regressiyasi va screenshot.
- [x] Asinxron GPU timeline profiling v1; physical GPU acceptance hali ochiq.
- [x] Foliage sirtga moslash v1: manual, brush bilan avtomatik, offset/normal, lock/hidden, Undo/Redo.
- [ ] Alohida GPU foliage culling/LOD/HLOD va zich sahna benchmark'i.

Tekshiruv (2026-11-10): 707/707 unit, 12/12 reference render, GL 4.1 terrain va GPU timer sinovlari; SDK native load + ko'chirilgan SDK manifest testlari; haqiqiy editor hot reload; 26 skript klassli Platformer2D eksport/screenshot/relocation; mos bo'lmagan modul eksportini rad etish; UI'siz runtime. Foliage editori 89 ildizni yangiladi, Undo/Redo o'tdi. GUI'dagi native legacy nomzodning konstruktori bajarilmadi; o'zgarmagan nomzod doimiy qayta urinishga tushmadi. Hisobotlar `build-review/SDKQuality/` da.

**SDK paketidan keyingi ish:** packed foliage GPU/CPU culling va model LOD v1 yuqoridagi paketda bajarildi. Dense scene eksporti va correctness oracle'lari shu yerda qayd etiladi. Physical GPU frame-time/RSS byudjeti hali alohida acceptance talab qiladi; undan keyingi dunyo bosqichi async tile streaming.

### P1 — yakunlangan ish paketi

- [x] 32×32 katakli terrain render bo'laklari va masofaga bog'liq LOD.
- [x] Qo'shni bo'laklar farqi ko'pi bilan bir daraja; mayda chetlar yirik chetga geometrik moslanadi.
- [x] LOD chegarasida 15% hysteresis; ortografik ko'rinishda to'liq detal.
- [x] O'zgargan balandlik va normal ta'sir qiladigan bo'laklargina qayta quriladi.
- [x] Bo'yash geometriyani qayta qurmaydi; collision/raycast to'liq heightfield aniqligida qoladi.
- [x] LOD sozlamalari sahna/prefab/copy/undo orqali saqlanadi; editor haqiqiy hisoblagichlarni ko'rsatadi.
- [x] Katta 2D tilemap visibility hududlarni ierarxik tekshiradi va oldingi row-major tartibni saqlaydi.
- [x] Aylangan, akslantirilgan va perspektiv 2D holatlar to'liq tekshiruv oracle'i bilan mos.
- [x] CPU algoritm testlari, haqiqiy GL cache/sculpt testi, save/reload, render va editor regressiyasi.
- [x] Geometriya va AABB tekshiruvi soni bilan qayta bajariladigan o'lchov; FPS bilan aralashtirilmaydi.

### Visual Script — foydalanuvchi so'rovi, 2026-11-10

- [x] `.zgraph` format, typed katalog, tekshiruv va indekslangan bounded VM.
- [x] Node/pin/wire muharriri, qidiruv, pan/zoom/fit, xato diagnostikasi, atomik save, 64 holatli undo/redo.
- [x] Lifecycle, collision/trigger/UI/AI hodisalari; input, transform, 2D/3D fizika, prefab/audio/animation/UI amallari.
- [x] O'zgaruvchilar, nusxa overrides, shart/sequence/loop/break/delay va nomlangan funksiyalar.
- [x] Native host, aks ettirilgan C++ maydon ko'prigi, messages; C++ modulisiz ishlash.
- [x] Breakpoint, pause/step/continue, watch va cheklangan execution trace.
- [x] Asset registry / content browser / inspector / reference scan va rename integratsiyasi.
- [x] `VisualWorkshop` 2D/3D namunasi va [qo'llanma](VisualScripting.md).
- [ ] Parametr/return qiymatli funksiyalar, array/struct/enum/curve pinlari, macro/collapse va inheritance.
- [ ] RPC/replication tugunlari, to'liq native metod reflection, bytecode/native cook va katta graph profiling.

### Ichki C++ kod muharriri — 2026-11-10

- [x] Editor ichida tablar, source ro'yxati, C++ ranglari, monospace font, qator raqamlari va auto-indent.
- [x] Find/replace, undo/redo, 265 API nom/signatura katalogi, Ctrl+Space va gameplay snippet'lari.
- [x] Inspektor/content browser/compiler manbasini ichki oynada ochish; read-only SDK fayllari.
- [x] Builddan oldin save-all, F7 build/hot reload, error line/column navigatsiyasi.
- [x] Atomik save, tashqi o'zgarish konflikti, UTF-8/BOM/CRLF, bounded undo va crash recovery.
- [x] 9 fayl-himoya testi, 691 umumiy unit, haqiqiy keyboard/build/error/recovery GUI sinovi.
- [ ] clangd/LSP orqali semantic completion, go-to-definition, project rename va live type diagnostics.

### P2 — GPU terrain materiallari, 2026-11-10

- [x] 512×512 runtime bake o'rniga GPU'da to'rtta linear albedo qatlami va resolution'ga mos RGBA32F control.
- [x] Har qatlamga tangent normal, ORM, normal strength, roughness/metallic/occlusion va metrga bog'liq UV.
- [x] Mavjud lighting/shadow/fog/debug/picking/wire yo'llariga ulash; normal mesh materiallari alohida qoladi.
- [x] TextureLibrary import/cache/reload, xavfsiz asset yo'llari, yo'q xarita fallback'i va control hisoblagichlari.
- [x] Serializer asset handle'lari, legacy default'lar va editor boshqaruvlari.
- [x] 696/696 CPU, 12/12 render, GL 4.1 pixel/cache sinovlari, editor va MapWorkshop export/relocation tekshiruvi.
- [x] Packed foliage instancing/culling va ikki model LOD v1 (yuqoridagi paket).
- [ ] Atlas/cook, terrain tile streaming va 2D kontent workflow ishlari.

P2 dalillari: 12 ta source PBR teksturasi export reachability orqali paketga kirdi; eksport qilingan Player 12 o'lchov kadri, screenshot va boshqa papkaga ko'chirish tekshiruvidan o'tdi. Editor 67600 byte control, 1 upload, 0 missing map ko'rsatdi. GL testi LOD UV mosligi, normal/ORM/AO, picking/wire, native mesh binding, qayta import, xavfsiz yo'llar va 120 validation kadrida upload bo'lmasligini tasdiqladi. Shadow atlas resize'da render target yo'qolishi va paint float tebranishi tuzatildi. CI'da GPU test qadami qo'shildi; bu sessiyada testlar lokal VPS/Mesa llvmpipe'da bajarildi. Physical GPU, Windows/macOS va to'liq professional parity hali tasdiqlanmagan.

### Unity / Unreal / Godot sifatiga chiqish mezonlari

Bu engine'lar bilan tenglik bitta imkoniyat yoki kod hajmi bilan belgilanmaydi. Har yo'nalishda foydalanuvchi bajara oladigan ish, takrorlanadigan test va ochiq cheklov ko'rsatiladi; tasdiqlanmagan foiz berilmaydi.

| Yo'nalish | Zamin'dagi holat | Qabul qilish uchun keyingi dalil |
|---|---|---|
| 2D | Tilemap, sprite, animatsiya, Box2D, UI va katta map visibility mavjud | Tugallangan 2D o'yin; atlas/residency, input/UI va stress profili |
| 3D xarita | Heightfield authoring, collision, patch LOD; GPU PBR qatlamlari GL/editor/export sinovlaridan o'tdi | Haqiqiy assetli map, foliage/streaming va physical GPU frame-time byudjeti |
| Gameplay authoring | C++ muharriri va typed visual graph v1 mavjud | Funksiya argument/return, murakkab type'lar, LSP, katta graph/debug workflow |
| Dunyo | Render bo'laklari va bounded world-cell manifest mavjud; disk streaming rejalashtirilgan | Chegaralangan RAM/VRAM bilan async load/unload/cancel va katta dunyo testi |
| Jamoaviy tarmoq | Multiplayer production acceptance hali bajarilmagan | Authoritative replication, latency/loss/reconnect testlari, xavfsizlik va kuzatuv |
| Mahsulot sifati | Linux testlar va eksport workflow mavjud | Native SDK ABI gate mavjud; OS/driver matritsasi, crash/recovery va uzoq sessiya |

Taqqoslash mezonlari uchun rasmiy manbalar: [Unity Terrain Layer](https://docs.unity.com/en-us/engine/6000.6/manual/creating-environments/script-terrain/terrain-textures/class-terrain-layer), [Unreal Landscape Materials](https://dev.epicgames.com/documentation/unreal-engine/landscape-materials-in-unreal-engine), [Godot MultiMesh](https://docs.godotengine.org/en/stable/classes/class_multimesh.html). Hozirgi GPU terrain bosqichi bu mahsulotlarning barcha imkoniyatlariga tenglik da'vosi emas.

### P2–P8 uchun zarur ishlar

**2D:** tile render cache va dirty regions, katta atlaslar va texture residency, animated tile yangilanishi,
terrain/auto-tile chegaralari, collision chunk rebake, navigation, sprite/animation/UI profiling. Mavjud culling,
layer, auto-tile, Box2D, UI va animatsiya imkoniyatlari qayta yozilmaydi; kamchiliklari dalil bilan kuchaytiriladi.

**3D terrain va foliage:** mavjud GPU qatlamlarini katta assetlarda tekshirish, texture arrays/material variantlari, screen-space
error LOD va o'tishlarni silliqlash, terrain tile biriktirish, import/export formatlari, road/spline asboblari,
GPU instancing/culling, zichlik maskalari, asset LOD/HLOD, tahrirdan keyin foliage'ni sirtga qayta joylash,
async collider cooking va navmesh yangilash. Voxel/cave — heightfield'dan alohida tizim.

**Umumiy kontent:** qayta importda havolalarni saqlash, cook va deterministik cache kalitlari, buzilgan fayllar,
compressed paketlar, locale/shader variantlari, asset pipeline rollback'i va versiyalangan serialization; SDK ABI gate v1 qo'shildi.
Engine komponentlari o'zgarsa eski C++ modullarni tekshirish/qayta build qilish avtomatik va tushunarli bo'lishi kerak.

**Gameplay:** character/controller, kamera, input remap/gamepad, AI/navigation, animation state/transition,
root motion/IK/retarget workflow, audio mixer/streaming, timeline/VFX va saqlash tizimlarini bir o'yin
jarayonida sinash. Mavjud funksiyalar uchun 'bor' degan belgi production sifatiga tenglashtirilmaydi.

**Multiplayer:** authoritative server, bandwidth budget, snapshot/interpolation, prediction/reconciliation,
physics ownership, replication relevancy, matchmaking/session, host migration talabini alohida belgilash,
reconnect va hisob xizmatlari. Avval transport va threat model tanlanadi; client server vakolatini bermaydi.

**Editor/launcher auth:** oldingi talab bo'yicha Zamin yagona hisobi bilan majburiy login rejalashtirilgan.
Native client ichiga server sirini joylamaslik, browser-based PKCE, OS secret store, sessiya bekor qilish va
xizmat uzilgandagi xatti-harakat aniq belgilanadi. Bu engine yadrosining render/fizika sikliga kiritilmaydi.

**Performance va release:** 2D/3D uchun seed/scene/settings versiyasi biriktirilgan benchmarklar; CPU/GPU frame time,
p95/p99, RAM/VRAM, yuklanish va draw/triangle hisoblagichlari. 60 FPS/16.7 ms maqsadi reference physical GPU
va sahna byudjeti tanlangandan keyin tekshiriladi. VPS llvmpipe sinovi haqiqiy GPU unumdorligi dalili emas.
Har bir bosqich correctness, sanitizer/recovery, regressiya, eksport va hujjat darvozasidan o'tadi.

Unreal'dagi terrain komponentlari va ularning LOD birligi — [Epic Landscape texnik qo'llanmasi](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-technical-guide-in-unreal-engine)
bo'yicha taqqoslash manbasi. Disk kataklarini manbaga yaqinlik bo'yicha yuklash tushunchasi —
[Epic World Partition qo'llanmasi](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine).
Zamin P1 render bo'laklari disk streaming'i emas; u P3 da alohida bajariladi.

---

## M01 Scripting and gameplay API

- [x] 001. An inspector for the EntityRef field (drag in from the hierarchy, clear)
- [x] 002. An EntityRef serialization test
- [x] 003. An Invoke/InvokeRepeating/CancelInvokes test
- [x] 004. A test of Tween and the easing functions
- [x] 005. An input action map (in the project settings: binding keys/gamepad/mouse)
- [x] 006. Input::IsActionPressed / IsActionJustPressed / IsActionJustReleased
- [x] 007. Input::GetAxis (negative/positive key + gamepad axis, deadzone)
- [x] 008. JustPressed/JustReleased for the keyboard and mouse (the state between frames)
- [x] 009. SaveData (PlayerPrefs): saving Int/Float/String/Bool, the file ~/.local/share/zamin/<project>
- [x] 010. A SaveData test
- [x] 011. A Random API (seed, Range, Float, Int, Bool, InsideCircle, OnSphere)
- [x] 012. A tag (Tag) system: FindEntitiesWithTag, HasTag
- [x] 013. Messages/events: SendMessage(name), Broadcast, an event bus between scripts
- [x] 014. GetComponentInChildren / GetComponentInParent / GetChildren
- [x] 015. Caching for FindEntity (a fast name->entity lookup)
- [x] 016. Script lifecycle: OnEnable/OnDisable/OnStart (before the first Update)
- [x] 017. OnTriggerBegin/OnTriggerEnd (a separate callback for sensors)
- [x] 018. Script fields: enums, and ZM_FIELD_COLOR for colors (glm::vec4 as a color)
- [x] 019. Tooltip/range attributes for script fields (ZM_FIELD_RANGE)
- [x] 020. A new script template: with every callback and examples

## M02 Editor UX I

- [x] 021. Selecting several entities (with Ctrl/Shift) in the hierarchy
- [x] 022. Deleting/duplicating several selected entities together
- [x] 023. Moving several selected entities together with the gizmo
- [x] 024. Reordering by drag-drop in the hierarchy (dropping between siblings)
- [x] 025. A Transform context menu: reset, copy, paste
- [x] 026. Snapping settings (step: move/rotate/scale) in the toolbar
- [x] 027. Grid settings (size, color, visibility)
- [x] 028. Camera bookmarks (Ctrl+1..5 to save, 1..5 to return)
- [x] 029. Frame All - focus on the whole scene
- [x] 030. Duplicating an entity in the viewport (dragging with Ctrl+D, Alt+drag)
- [x] 031. Reordering components in the inspector (up/down)
- [x] 032. Copying a component and pasting it onto another entity
- [x] 033. Remembering the collapsed state of components in the inspector
- [x] 034. Visibility (eye) and lock buttons in the hierarchy
- [x] 035. Locked entities are not selected in the viewport
- [x] 036. Moving files into a folder by dragging in the content browser
- [x] 037. A list view in the content browser
- [x] 038. A filter by file type in the content browser
- [x] 039. Importing a file into Assets by dropping it onto the window from outside
- [x] 040. The selected entity's information in the status bar

## M03 Active/inactive entities

- [x] 041. An Active flag on TagComponent
- [x] 042. Activity through the hierarchy (inactive parent -> the children too)
- [x] 043. Inactive entities are not drawn
- [x] 044. Inactive entities take no part in physics (the body is removed)
- [x] 045. The scripts of inactive entities get no Update
- [x] 046. A SetActive() script API
- [x] 047. The OnEnable/OnDisable calls
- [x] 048. An Active checkbox in the inspector
- [x] 049. Inactive entities look dimmed in the hierarchy
- [x] 050. Serialization and tests
- [x] 051. The particle system stops when inactive
- [x] 052. Audio sources stop when inactive
- [x] 053. The Animator is not updated when inactive
- [x] 054. UI elements are not shown and cannot be pressed when inactive
- [x] 055. Inactive cameras do not count as the primary camera
- [x] 056. Inactive lights are not taken into account
- [x] 057. Tilemap collision turns off when inactive
- [x] 058. The character controller turns off when inactive
- [x] 059. Prefab instances keep their active state
- [x] 060. Activity tests (hierarchy, physics, scripts)

## M04 Asset system

- [x] 061. AssetHandle (UUID) and AssetRegistry
- [x] 062. .zmeta files (a UUID and import settings for every asset)
- [x] 063. Detecting asset types (texture, model, audio, scene, prefab, font, hdr, material)
- [x] 064. A file watcher - reloading changed assets
- [x] 065. Reloading textures (hot reload) in the editor
- [x] 066. Reloading models
- [x] 067. Texture import settings: filter, wrap, mipmap, sRGB
- [x] 068. An inspector for texture import settings
- [x] 069. Model import settings: scale, up axis
- [x] 070. Updating references when an asset is renamed (through the UUID)
- [x] 071. Showing an asset in the inspector when it is selected in the content browser
- [x] 072. A texture preview in the inspector (size, format)
- [x] 073. A model preview (submeshes, triangles, the list of animations)
- [x] 074. An audio asset preview (duration, a play button)
- [x] 075. Finding unused assets
- [x] 076. A warning that an asset is in use before it is deleted
- [x] 077. Searching assets by folder (the full path)
- [x] 078. Asset registry serialization and its tests
- [x] 079. Thumbnail caching in the content browser
- [x] 080. Printing asset import errors to the console

## M05 Materials (asset)

- [x] 081. The .zmat material asset format
- [x] 082. Loading/saving material assets
- [x] 083. A material asset reference in MeshRenderer
- [x] 084. A material editor in the inspector
- [x] 085. A material preview sphere (render-to-texture)
- [x] 086. A transparent (alpha blend) mode
- [x] 087. An alpha cutout mode (threshold)
- [x] 088. A double-sided flag
- [x] 089. An unlit shader mode
- [x] 090. Texture tiling and offset (vec2)
- [x] 091. Dragging a material from the content browser onto a mesh
- [x] 092. Creating a new material (the content browser menu)
- [x] 093. Hot reload of material changes
- [x] 094. Exporting glTF materials to .zmat
- [x] 095. Material instances (changing a color from a script without spoiling the shared asset)
- [x] 096. Sorting transparent objects back to front
- [x] 097. Emissive intensity and color in the material
- [x] 098. Material serialization tests
- [x] 099. A library of standard materials (metal, plastic, wood, glass)
- [x] 100. A thumbnail for material assets

## M06 Rendering I

- [x] 101. Spot lights (cone, inner/outer angle)
- [x] 102. A spot light inspector and wireframe view
- [x] 103. A wireframe radius view of point lights in the editor
- [x] 104. Raising the number of lights (32) and choosing the nearest ones (on purpose: not by distance alone but by the effect on the camera — intensity x (radius/distance)^2, the nearest one on an equal effect; in the README)
- [x] 105. FXAA anti-aliasing
- [x] 106. Render scale (an internal size factor)
- [x] 107. Automatic gamma/exposure (auto exposure, simple)
- [x] 108. A wireframe mode (editor)
- [x] 109. Lighting-only/normal/albedo debug views
- [x] 110. Frustum culling (for meshes)
- [x] 111. Frustum culling (for 2D sprites)
- [x] 112. Mesh instancing (the same mesh + material)
- [x] 113. Render statistics: the culled count
- [x] 114. Shadow quality settings (atlas size, PCF)
- [x] 115. Shadow bias settings in the scene settings
- [x] 116. Several suns with a directional light shadow (only one casts shadows)
- [x] 117. Setting up the sky gradient + the sun disc
- [x] 118. A chromatic aberration and film grain effect
- [x] 119. Tinting bloom by the light
- [x] 120. A rendering regression test (comparing headless screenshots)

## M07 Debug draw and visualization

- [x] 121. A DebugDraw API: Line, Box, Sphere, Arrow, Text (from scripts)
- [x] 122. DebugDraw with a duration
- [x] 123. DebugDraw only in the editor/Development mode
- [x] 124. Visualizing a raycast result
- [x] 125. Showing physics contact points
- [x] 126. Drawing the camera frustum in the editor
- [x] 127. Showing the light direction with an arrow
- [x] 128. Showing the audio source radius
- [x] 129. The character controller velocity vector
- [x] 130. Drawing navigation paths (with the next milestone)
- [x] 131. A gizmo size setting
- [x] 132. A selection outline (shader based, a smooth contour)
- [x] 133. The hovered entity is highlighted with an outline
- [x] 134. A bounding box display mode
- [x] 135. Drawing skeleton bones (animation debug)
- [x] 136. Showing normal vectors (debug)
- [x] 137. An FPS graph overlay in the viewport
- [x] 138. A draw call and triangle overlay
- [x] 139. Nested sections in the profiler (hierarchical)
- [x] 140. Debug draw tests

## M08 2D improvements

- [x] 141. A sprite sheet slicer (cutting by a grid, an editor window)
- [x] 142. A sprite atlas asset (named frames)
- [x] 143. 9-slice sprite rendering
- [x] 144. Sorting layers (named layers)
- [x] 145. A parallax layers component
- [x] 146. A 2D camera bounds and smooth follow component
- [x] 147. A camera shake API
- [x] 148. Tilemap: several layers
- [x] 149. Tilemap: auto-tile rules (4/8 neighbours)
- [x] 150. Tilemap: rectangle and flood fill tools
- [x] 151. Tilemap: per-tile collision flags
- [x] 152. Tilemap: one-way platforms
- [x] 153. Tilemap: animated tiles
- [x] 154. Tilemap: the palette when zooming in/out
- [x] 155. A sprite outline/flash effect (hit flash)
- [x] 156. Sprite color tint animation
- [x] 157. 2D lights (a simple 2D point light, additive)
- [x] 158. A pixel-perfect camera mode
- [x] 159. Sorting sprites by Y (top-down games)
- [x] 160. 2D tests

## M09 2D physics II

- [x] 161. Distance joint 2D
- [x] 162. Revolute (hinge) joint 2D (motor, limit)
- [x] 163. Prismatic joint 2D
- [x] 164. Weld joint 2D
- [x] 165. Wheel joint 2D
- [x] 166. Polygon collider 2D (a vertex editor)
- [x] 167. Edge/Chain collider 2D
- [x] 168. A physics material (friction/bounce) asset
- [x] 169. Collision layers and masks (2D)
- [x] 170. A one-way platform effector
- [x] 171. An area effector (wind/water)
- [x] 172. A collision filtering test
- [x] 173. OverlapCircle/OverlapBox queries 2D
- [x] 174. RaycastAll 2D
- [x] 175. An inspector and wireframe for joints
- [x] 176. Joint serialization
- [x] 177. A physical ragdoll sample (2D)
- [x] 178. Box2D sub-step settings
- [x] 179. A sleeping setting for 2D physics
- [x] 180. 2D physics tests

## M10 3D physics II

- [x] 181. Fixed joint 3D
- [x] 182. Hinge joint 3D (motor, limit)
- [x] 183. Distance joint 3D
- [x] 184. Slider joint 3D
- [x] 185. A convex hull collider (from a model)
- [x] 186. A static triangle mesh collider (from a model)
- [x] 187. Collision layers and masks (3D)
- [x] 188. OverlapSphere/OverlapBox queries 3D
- [x] 189. SphereCast/ShapeCast 3D
- [x] 190. RaycastAll 3D
- [x] 191. A continuous collision detection (CCD) flag
- [x] 192. A sleeping/wake-up API
- [x] 193. Physics material 3D
- [x] 194. Trigger callbacks 3D (OnTriggerBegin)
- [x] 195. The character controller pushes dynamic bodies (a force setting)
- [x] 196. A joint inspector and wireframe
- [x] 197. Joint serialization
- [x] 198. A 3D ragdoll (simple) sample
- [x] 199. Physics debug: contact points
- [x] 200. 3D physics tests

## M11 Audio

- [x] 201. Audio mixer groups (Master, Music, SFX, UI)
- [x] 202. Group volume and mute
- [x] 203. An AudioListener component (instead of the camera)
- [x] 204. A music crossfade API
- [x] 205. Music streaming (for large files)
- [x] 206. Sound pitch randomness (random range)
- [x] 207. A limit on sounds playing at the same time
- [x] 208. AudioSource: a Play/Stop/Pause script API
- [x] 209. Showing the AudioSource state in the inspector
- [x] 210. A Doppler effect (simple)
- [x] 211. An audio clip asset (duration, format)
- [x] 212. Sound fading (fade in/out)
- [x] 213. Sound effects: a low-pass filter
- [x] 214. Sound settings are saved with SaveData
- [x] 215. Audio tests (without a sound device)
- [x] 216. An audio profiler (the number of active sounds)
- [x] 217. An audio preview waveform in the editor
- [x] 218. A directional cone for 3D sound
- [x] 219. An ambient zone (sound inside a volume)
- [x] 220. Audio events (OnSoundFinished)

## M12 UI system

- [x] 221. A UI Image component (sprite, 9-slice)
- [x] 222. UI Slider
- [x] 223. UI Toggle (checkbox)
- [x] 224. UI Progress bar
- [x] 225. UI text input (input field)
- [x] 226. Vertical/horizontal layout groups
- [x] 227. Stretch anchors (stretching left to right)
- [x] 228. A scale that adapts to the screen size (reference resolution)
- [x] 229. UI events: OnHover/OnPress/OnRelease
- [x] 230. Keyboard/gamepad navigation for the UI
- [x] 231. UI animations (with tweens)
- [x] 232. Several font sizes (atlas quality)
- [x] 233. Text alignment (left/center/right, top/middle/bottom)
- [x] 234. Text wrapping (word wrap) by width
- [x] 235. Selecting and moving UI in the editor (a 2D gizmo)
- [x] 236. The UI hierarchy (relative to the parent UITransform)
- [x] 237. UI mask/clip (scroll view)
- [x] 238. Scroll view
- [x] 239. A dialog/menu sample (pause menu)
- [x] 240. UI tests

## M13 Animation II

- [x] 241. Property animation clips (.zanim): transform/color keyframes
- [x] 242. An animation timeline editor (an editor window)
- [x] 243. Keyframe interpolation (linear, step, bezier)
- [x] 244. Animation events (a script callback at a time)
- [x] 245. Root motion (glTF)
- [x] 246. Blend tree 1D (blending walk/run by speed)
- [x] 247. Animation layers (upper body) - simple
- [x] 248. Sprite animation clips as assets
- [x] 249. Showing the Animator parameters in the inspector at runtime
- [x] 250. A visual graph for the state machine (nodes)
- [x] 251. An animation preview in the editor (scrub)
- [x] 252. Skeleton: attaching an entity to a bone (attachment socket)
- [x] 253. Footstep sounds by animation speed, with an event
- [x] 254. Trimming animation clips (start/end)
- [x] 255. glTF morph targets (blend shapes) - simple
- [x] 256. Animation caching (performance)
- [x] 257. Support for several skins
- [x] 258. Animation tests (property clip, event)
- [x] 259. An animation sample (a door opening)
- [x] 260. Animation documentation

## M14 Particles II

- [x] 261. A color gradient (many points)
- [x] 262. A size curve
- [x] 263. Velocity over lifetime
- [x] 264. Texture sheet animation (flipbook)
- [x] 265. A cone emitter shape
- [x] 266. An emitter from a mesh surface
- [x] 267. Sub-emitters (an explosion on death)
- [x] 268. Collision with the ground plane (bounce)
- [x] 269. Noise turbulence
- [x] 270. Trail rendering
- [x] 271. More batching of particles on the GPU
- [x] 272. Particle sorting (distance to the camera)
- [x] 273. Prewarm (a state filled from the start)
- [x] 274. A table of emission bursts (bursts list)
- [x] 275. Simulation speed and local/world
- [x] 276. A library of particle presets (fire, smoke, sparks, rain, snow)
- [x] 277. Rain and snow effects (around the camera)
- [x] 278. Sections in the particle inspector
- [x] 279. Particle serialization tests
- [x] 280. A particle performance test

## M15 Scene management

- [x] 281. Additive scenes (several at the same time)
- [x] 282. DontDestroyOnLoad (persistent entities)
- [x] 283. Asynchronous scene loading (loading screen)
- [x] 284. Scene transitions (fade in/out)
- [x] 285. The list of build scenes (in the project settings)
- [x] 286. LoadScene by index
- [x] 287. Events when a scene is loaded/closed
- [x] 288. Speeding up the lookup APIs inside a scene
- [x] 289. Showing errors when a scene is loaded
- [x] 290. A stable UUID order for comparing scenes (diff)
- [x] 291. Scene metadata (description, thumbnail)
- [x] 292. Editor: a panel with the list of scenes
- [x] 293. Editor: restoring the editor state when the scene changes in play mode
- [x] 294. Global game state (singleton services)
- [x] 295. Scene version migration
- [x] 296. Showing the scene load time in the profiler
- [x] 297. Faster serialization for large scenes
- [x] 298. Splitting a scene into pieces (sub-scenes) - simple
- [x] 299. Scene management tests
- [x] 300. Scene management documentation

## M16 Editor II

- [x] 301. An editor color tint in play mode
- [x] 302. Game view resolution presets (16:9, 4:3, 1080p)
- [x] 303. A text filter in the console
- [x] 304. Grouping identical messages in the console (collapse)
- [x] 305. A settings window (Preferences): theme color, font size
- [x] 306. A window with the list of keyboard shortcuts
- [x] 307. Autosave and recovery
- [x] 308. A list of recent scenes
- [x] 309. An undo history panel
- [x] 310. Undo for the selection
- [x] 311. Saving/loading editor layouts
- [x] 312. Several viewports (side/top views)
- [x] 313. Buttons for the orthographic views (Top/Front/Right)
- [x] 314. An editor camera speed setting and FOV
- [x] 315. Showing entity names in the viewport
- [x] 316. A scene statistics panel (the number of components)
- [x] 317. Opening script files from the editor (an external editor)
- [x] 318. Showing compile errors with the file/line
- [x] 319. Fewer editor updates (an FPS limit when idle)
- [x] 320. Editor II tests (headless)

## M17 Project settings

- [x] 321. A project settings window
- [x] 322. An input map editor
- [x] 323. A tags and layers editor
- [x] 324. A physics layers matrix
- [x] 325. Quality settings (shadows, SSAO, bloom default)
- [x] 326. Player window settings (size, fullscreen, vsync, title)
- [x] 327. A game icon
- [x] 328. The start scene and the build scenes
- [x] 329. The company/game name and version
- [x] 330. Project settings serialization
- [x] 331. Project settings tests
- [x] 332. Graphics API and context settings
- [x] 333. Time settings (fixed timestep, time scale)
- [x] 334. A Time.timeScale script API (slow motion)
- [x] 335. Default gravity values per project
- [x] 336. Default audio settings
- [x] 337. Localization: language files (uz, ru, en)
- [x] 338. A localization script API (Localize("key"))
- [x] 339. Choosing the editor language (Uzbek/English)
- [x] 340. Project settings documentation

## M18 Runtime player

- [x] 341. Reading the player window settings from the project
- [x] 342. Toggling fullscreen with F11
- [x] 343. An FPS limit
- [x] 344. A splash screen (the Zamin logo)
- [x] 345. The player command line: --windowed, --resolution
- [x] 346. The player writes a log file
- [x] 347. A message window on a crash
- [x] 348. An API to hide/lock the cursor in the player
- [x] 349. Adapting to a change of the screen size
- [x] 350. Leaving unused assets out of the export
- [x] 351. Optimizing the script module in the export (Release)
- [x] 352. Export: a .desktop file and an icon
- [x] 353. Showing an export report (size, files)
- [x] 354. Pausing in the player (when the focus is lost)
- [x] 355. A gamma and brightness setting in the player
- [x] 356. A player run test (headless)
- [x] 357. An export test (automatic)
- [x] 358. A sample of a minimal game menu for the player
- [x] 359. Player version information
- [x] 360. Runtime documentation

## M19 Math and core

- [x] 361. Perlin/Simplex noise functions
- [x] 362. An AnimationCurve type (keys, evaluation)
- [x] 363. Spline paths (Catmull-Rom) and a component
- [x] 364. A script for moving along a spline
- [x] 365. Color utilities (HSV, lerp, hex)
- [x] 366. Math utilities (Remap, SmoothDamp, MoveTowards, Approximately)
- [x] 367. Quaternion helpers (LookRotation, Slerp)
- [x] 368. Job system / thread pool
- [x] 369. Parallel for (for particles and animation)
- [x] 370. Memory statistics in the profiler
- [x] 371. A log file and a log level setting
- [x] 372. Better asserts (with a message)
- [x] 373. Timer and Stopwatch utilities
- [x] 374. String utilities (split, trim, format)
- [x] 375. A stronger UUID generation test
- [x] 376. File system helpers (path normalization)
- [x] 377. Bounding volume helpers (AABB, sphere, ray intersection)
- [x] 378. Geometry tests (ray-AABB, ray-sphere)
- [x] 379. Math tests
- [x] 380. Core documentation

## M20 Navigation and AI

- [x] 381. Grid based A* pathfinding (2D)
- [x] 382. Building the pathfinding grid from a tilemap
- [x] 383. NavGrid 3D (along the ground plane, detecting obstacles)
- [x] 384. A NavAgent component (walking along a path)
- [x] 385. Path smoothing (string pulling)
- [x] 386. Dynamic obstacles (updating the grid)
- [x] 387. Steering: seek, flee, arrive, wander
- [x] 388. Obstacle avoidance (with raycasts)
- [x] 389. An AI state machine helper class
- [x] 390. A line of sight check
- [x] 391. A patrol path (waypoints) component
- [x] 392. Navigation debug drawing
- [x] 393. Navigation tests
- [x] 394. An AI sample: an enemy chases (2D)
- [x] 395. An AI sample: a 3D NPC on patrol
- [x] 396. Formation/group movement (simple)
- [x] 397. A behavior tree (minimal) helper
- [x] 398. A navigation performance test
- [x] 399. Navigation serialization
- [x] 400. AI documentation

## M21 Prefabs and serialization II

- [x] 401. Prefab instances are linked to the original prefab (link)
- [x] 402. Applying prefab changes to the instances (Apply)
- [x] 403. Returning an instance to its original state (Revert)
- [x] 404. Showing prefab overrides in the inspector
- [x] 405. Nested prefabs
- [x] 406. A prefab editing mode (opening it on its own)
- [x] 407. Remapping EntityRefs in prefab instances
- [x] 408. Remapping EntityRefs internally on duplication
- [x] 409. Scene file version and migration
- [x] 410. Recovering a broken scene file (partial loading)
- [x] 411. A serialization fuzz test (random JSON)
- [x] 412. A serialization benchmark of large scenes
- [x] 413. Prefab variants (inheriting from a base prefab)
- [x] 414. Prefab thumbnails
- [x] 415. Dragging a prefab from the content browser into the scene many times
- [x] 416. Hot reload of prefab changes
- [x] 417. Prefab serialization tests
- [x] 418. Merging the clipboard format with the prefab one
- [x] 419. Checking prefabs in the scene export
- [x] 420. Prefab documentation

## M22 Quality and tests

- [x] 421. A build and tests with AddressSanitizer
- [x] 422. Tests with UndefinedBehaviorSanitizer
- [x] 423. Checking for memory leaks (LeakSanitizer)
- [x] 424. Cleaning up warnings (-Wall -Wextra -Wpedantic)
- [x] 425. Headless screenshot tests for the renderer in CI
- [x] 426. An editor smoke test (every panel opens)
- [x] 427. An automatic test of script hot reload
- [x] 428. An automatic export test
- [x] 429. A physics determinism test
- [x] 430. A performance benchmark (10k sprites, 1k meshes)
- [x] 431. An ECS benchmark
- [x] 432. A serialization benchmark
- [x] 433. Test framework: a filter by test names
- [x] 434. Test framework: a timing report
- [x] 435. A code coverage report (gcov)
- [x] 436. A clang-format configuration
- [x] 437. CI: a sanitizer job
- [x] 438. CI: artifacts (the player build)
- [x] 439. A document with the list of regression tests
- [x] 440. A quality report

## M23 Documentation

- [x] 441. docs/Scripting.md - a guide to C++ scripts
- [x] 442. docs/Components.md - a reference of every component
- [x] 443. docs/Editor.md - an editor guide
- [x] 444. docs/Tutorial-2D.md - a first 2D game
- [x] 445. docs/Tutorial-3D.md - a first 3D game
- [x] 446. docs/Physics.md
- [x] 447. docs/Rendering.md
- [x] 448. docs/Animation.md
- [x] 449. docs/UI.md
- [x] 450. docs/Audio.md
- [x] 451. docs/Architecture.md - the internal architecture
- [x] 452. docs/BuildAndExport.md
- [x] 453. A Doxygen configuration
- [x] 454. Filling in the API comments in the headers
- [x] 455. FAQ and troubleshooting
- [x] 456. A contribution guide (CONTRIBUTING)
- [x] 457. CHANGELOG
- [x] 458. Illustrated guides (screenshots)
- [x] 459. Updating the README
- [x] 460. A documentation index

## M24 Sample games

- [x] 461. Top-down shooter (2D): the player, shooting, enemies
- [x] 462. Top-down shooter: waves and the score
- [x] 463. Breakout (2D): the paddle, the ball, the bricks
- [x] 464. Breakout: bonuses and levels
- [x] 465. A 3D FPS controller sample (mouse look)
- [x] 466. FPS: targets and shooting (raycast)
- [x] 467. A puzzle game (match-3 or sokoban)
- [x] 468. Endless runner (3D)
- [x] 469. Racing (simple car physics)
- [x] 470. Showing the samples in the launcher (with a picture)
- [x] 471. A README for every sample
- [x] 472. Headless tests for the samples
- [x] 473. Sample assets (procedural)
- [x] 474. Sounds in the samples (procedural wav)
- [x] 475. UI menus in the samples
- [x] 476. SaveData in the samples (the record)
- [x] 477. Input actions in the samples
- [x] 478. Exporting and checking the samples
- [x] 479. A gallery of the samples (docs)
- [x] 480. A performance check of the samples

## M25 Optimization and polish

- [x] 481. Renderer2D batching optimization (fewer flushes)
- [x] 482. Fewer per-frame allocations
- [x] 483. World transform caching (dirty flag)
- [x] 484. Caching ECS views
- [x] 485. Font atlas caching and kerning
- [x] 486. Texture loading on a background thread (async)
- [x] 487. Model loading on a background thread
- [x] 488. Shader caching (program binary)
- [x] 489. Moving uniforms to UBOs
- [x] 490. Culling in the shadow pass
- [x] 491. Parallelizing the particle simulation
- [x] 492. Parallelizing the animation evaluation
- [x] 493. Editor UI performance (large hierarchies - clipper)
- [x] 494. Large folders in the content browser (clipper)
- [x] 495. A shorter startup time
- [x] 496. Less memory use
- [x] 497. A 60 FPS check of every sample
- [x] 498. A bottleneck report from the profiler
- [x] 499. The final polish: UI texts and translations
- [x] 500. The final regression and all the tests
