# Zamin Engine — 500 ta vazifa rejasi

Har bir milestone 20 ta vazifadan iborat. `[x]` — bajarilgan, `[ ]` — rejada.

## M01 Skript va gameplay API

- [x] 001. EntityRef maydoni uchun inspektor (ierarxiyadan sudrab qo'yish, tozalash)
- [x] 002. EntityRef serializatsiya testi
- [x] 003. Invoke/InvokeRepeating/CancelInvokes testi
- [x] 004. Tween va easing funksiyalari testi
- [x] 005. Input action xaritasi (loyiha sozlamalarida: tugma/gamepad/sichqoncha bog'lash)
- [x] 006. Input::IsActionPressed / IsActionJustPressed / IsActionJustReleased
- [x] 007. Input::GetAxis (manfiy/musbat tugma + gamepad o'qi, deadzone)
- [x] 008. Klaviatura va sichqoncha uchun JustPressed/JustReleased (kadrlar orasidagi holat)
- [x] 009. SaveData (PlayerPrefs): Int/Float/String/Bool saqlash, fayl ~/.local/share/zamin/<loyiha>
- [x] 010. SaveData testi
- [x] 011. Random API (seed, Range, Float, Int, Bool, InsideCircle, OnSphere)
- [x] 012. Teglar (Tag) tizimi: FindEntitiesWithTag, HasTag
- [x] 013. Xabarlar/hodisalar: SendMessage(name), Broadcast, skriptlararo event bus
- [x] 014. GetComponentInChildren / GetComponentInParent / GetChildren
- [x] 015. FindEntity bo'yicha keshlash (nom->entity tez qidirish)
- [x] 016. Script lifecycle: OnEnable/OnDisable/OnStart (birinchi Update'dan oldin)
- [x] 017. OnTriggerBegin/OnTriggerEnd (sensorlar uchun alohida callback)
- [x] 018. Skript maydonlari: enum va ranglar (glm::vec4 rang sifatida) uchun ZM_FIELD_COLOR
- [x] 019. Skript maydonlari uchun tooltip/diapazon atributlari (ZM_FIELD_RANGE)
- [x] 020. Yangi skript shabloni: hamma callback va misollar bilan

## M02 Editor UX I

- [x] 021. Ko'p obyekt tanlash (Ctrl/Shift bilan) ierarxiyada
- [x] 022. Ko'p tanlangan obyektlarni birga o'chirish/nusxalash
- [x] 023. Ko'p tanlanganlarni gizmo bilan birga ko'chirish
- [x] 024. Ierarxiyada drag-drop bilan tartibni o'zgartirish (siblings orasiga qo'yish)
- [x] 025. Transform kontekst menyusi: reset, copy, paste
- [x] 026. Snapping sozlamalari (qadam: ko'chirish/aylantirish/masshtab) toolbar'da
- [x] 027. Grid sozlamalari (o'lcham, rang, ko'rinish)
- [x] 028. Kamera xatcho'plari (Ctrl+1..5 saqlash, 1..5 qaytish)
- [x] 029. Hammasini ko'rsatish (Frame All) - butun sahnaga fokus
- [x] 030. Viewport'da obyektni nusxalash (Ctrl+D bilan sudrash Alt+drag)
- [x] 031. Inspektorda komponentlarni tartiblash (yuqori/past)
- [x] 032. Komponentni nusxalash va boshqa obyektga joylash
- [x] 033. Inspektorda komponent yig'ib qo'yish holatini eslab qolish
- [x] 034. Ierarxiyada ko'rinish (ko'z) va qulf tugmalari
- [x] 035. Qulflangan obyektlar viewport'da tanlanmaydi
- [x] 036. Kontent brauzerda fayllarni sudrab papkaga ko'chirish
- [x] 037. Kontent brauzerda ro'yxat ko'rinishi (list view)
- [x] 038. Kontent brauzerda fayl turlari bo'yicha filtr
- [x] 039. Tashqaridan faylni oynaga tashlab Assets'ga import qilish
- [x] 040. Status bar'da tanlangan obyekt ma'lumoti

## M03 Faol/nofaol obyektlar

- [x] 041. TagComponent'ga Active bayrog'i
- [x] 042. Ierarxiya bo'yicha faollik (ota nofaol -> bolalar ham)
- [x] 043. Nofaol obyektlar chizilmaydi
- [x] 044. Nofaol obyektlar fizikada ishtirok etmaydi (body o'chiriladi)
- [x] 045. Nofaol obyektlarning skriptlari Update olmaydi
- [x] 046. SetActive() skript API
- [x] 047. OnEnable/OnDisable chaqiruvlari
- [x] 048. Inspektorda Active checkbox
- [x] 049. Ierarxiyada nofaol obyektlar xira ko'rinadi
- [x] 050. Serializatsiya va testlar
- [x] 051. Particle tizimi nofaol bo'lsa to'xtaydi
- [x] 052. Audio manbalar nofaol bo'lsa to'xtaydi
- [x] 053. Animator nofaol bo'lsa yangilanmaydi
- [x] 054. UI elementlar nofaol bo'lsa ko'rinmaydi va bosilmaydi
- [x] 055. Kameralar nofaol bo'lsa asosiy kamera hisoblanmaydi
- [x] 056. Yorug'liklar nofaol bo'lsa hisobga olinmaydi
- [x] 057. Tilemap collision nofaol bo'lsa o'chadi
- [x] 058. Character controller nofaol bo'lsa o'chadi
- [x] 059. Prefab instansiyalar faollik holatini saqlaydi
- [x] 060. Faollik testlari (ierarxiya, fizika, skript)

## M04 Asset tizimi

- [x] 061. AssetHandle (UUID) va AssetRegistry
- [x] 062. .zmeta fayllar (har bir asset uchun UUID va import sozlamalari)
- [x] 063. Asset turlarini aniqlash (texture, model, audio, scene, prefab, font, hdr, material)
- [x] 064. Fayl kuzatuvchi (file watcher) - o'zgargan assetlarni qayta yuklash
- [x] 065. Teksturalarni qayta yuklash (hot reload) editor'da
- [x] 066. Modellarni qayta yuklash
- [x] 067. Tekstura import sozlamalari: filtr, wrap, mipmap, sRGB
- [x] 068. Tekstura import sozlamalari inspektori
- [x] 069. Model import sozlamalari: masshtab, yuqori o'q
- [x] 070. Asset nomi o'zgarganda havolalarni yangilash (UUID orqali)
- [x] 071. Kontent brauzerda asset tanlanganda inspektorda ko'rsatish
- [x] 072. Tekstura preview inspektorda (o'lcham, format)
- [x] 073. Model preview (submesh, uchburchak, animatsiyalar ro'yxati)
- [x] 074. Audio asset preview (davomiylik, ijro tugmasi)
- [x] 075. Ishlatilmagan assetlarni topish
- [x] 076. Assetni o'chirishdan oldin ishlatilayotganini ogohlantirish
- [x] 077. Assetlarni papkalar bo'yicha qidirish (to'liq yo'l)
- [x] 078. Asset registry serializatsiyasi va testlari
- [x] 079. Kontent brauzerda thumbnail keshlash
- [x] 080. Asset import xatolarini konsolga chiqarish

## M05 Materiallar (asset)

- [x] 081. .zmat material asset formati
- [x] 082. Material asset yuklash/saqlash
- [x] 083. MeshRenderer'da material asset havolasi
- [x] 084. Material muharriri inspektorda
- [x] 085. Material preview sharchasi (render-to-texture)
- [x] 086. Shaffof (alpha blend) rejim
- [x] 087. Alpha cutout rejimi (threshold)
- [x] 088. Ikki tomonlama (double-sided) bayroq
- [x] 089. Unlit shader rejimi
- [x] 090. Tekstura tiling va offset (vec2)
- [x] 091. Materialni kontent brauzerdan mesh'ga sudrab qo'yish
- [x] 092. Yangi material yaratish (kontent brauzer menyusi)
- [x] 093. Material o'zgarishlari hot reload
- [x] 094. glTF materiallarini .zmat ga eksport qilish
- [x] 095. Material instansiyalari (skriptdan rangni o'zgartirish, umumiy assetni buzmasdan)
- [x] 096. Shaffof obyektlarni orqadan oldinga saralash
- [x] 097. Emissive intensivlik va rang materialda
- [x] 098. Material serializatsiya testlari
- [x] 099. Standart materiallar kutubxonasi (metall, plastik, yog'och, shisha)
- [x] 100. Material asset uchun thumbnail

## M06 Rendering I

- [x] 101. Spot yorug'lik (konus, ichki/tashqi burchak)
- [x] 102. Spot yorug'lik inspektori va wireframe ko'rinishi
- [x] 103. Point light wireframe radius ko'rinishi editorda
- [x] 104. Yorug'liklar sonini oshirish (32) va eng yaqinlarini tanlash (ataylab: masofaning o'zi emas, kameraga ta'siri bo'yicha — kuchi x (radius/masofa)^2, teng ta'sirda eng yaqini; README'da)
- [x] 105. FXAA anti-aliasing
- [x] 106. Render scale (ichki o'lcham koeffitsienti)
- [x] 107. Gamma/eksposure avtomatik (auto exposure, oddiy)
- [x] 108. Wireframe rejimi (editor)
- [x] 109. Faqat yorug'lik/normal/albedo debug ko'rinishlari
- [x] 110. Frustum culling (meshlar uchun)
- [x] 111. Frustum culling (2D sprite'lar uchun)
- [x] 112. Mesh instancing (bir xil mesh + material)
- [x] 113. Render statistikasi: culled soni
- [x] 114. Soyalar sifati sozlamalari (atlas o'lchami, PCF)
- [x] 115. Shadow bias sozlamalari sahna sozlamalarida
- [x] 116. Directional light soyasi bilan bir nechta quyosh (faqat biri soya)
- [x] 117. Sky gradient + quyosh diskini sozlash
- [x] 118. Chromatic aberration va film grain effekti
- [x] 119. Bloom'ni yorug'lik bo'yicha tint qilish
- [x] 120. Rendering regressiya testi (headless skrinshot solishtirish)

## M07 Debug draw va vizualizatsiya

- [x] 121. DebugDraw API: Line, Box, Sphere, Arrow, Text (skriptlardan)
- [x] 122. DebugDraw davomiyligi (duration) bilan
- [x] 123. DebugDraw faqat editor/Development rejimda
- [x] 124. Raycast natijasini vizualizatsiya qilish
- [x] 125. Fizika kontakt nuqtalarini ko'rsatish
- [x] 126. Kamera frustumini editorda chizish
- [x] 127. Yorug'lik yo'nalishini o'q bilan ko'rsatish
- [x] 128. Audio manba radiusini ko'rsatish
- [x] 129. Character controller tezlik vektori
- [x] 130. Navigatsiya yo'llarini chizish (keyingi milestone bilan)
- [x] 131. Gizmo o'lchami sozlamasi
- [x] 132. Selection outline (shader asosida, sillik kontur)
- [x] 133. Hover qilingan obyekt kontur bilan ajratiladi
- [x] 134. Bounding box ko'rsatish rejimi
- [x] 135. Skelet suyaklarini chizish (animatsiya debug)
- [x] 136. Normal vektorlarini ko'rsatish (debug)
- [x] 137. FPS grafigi overlay viewport'da
- [x] 138. Draw call va uchburchaklar overlay
- [x] 139. Profiler'da ichki bo'limlar (ierarxik)
- [x] 140. Debug draw testlari

## M08 2D yaxshilanishlar

- [x] 141. Sprite sheet slicer (grid bo'yicha kesish, editor oynasi)
- [x] 142. Sprite atlas asset (nomlangan kadrlar)
- [x] 143. 9-slice sprite rendering
- [x] 144. Sorting layer'lar (nomlangan qatlamlar)
- [x] 145. Parallax qatlamlar komponenti
- [x] 146. 2D kamera chegaralari (bounds) va silliq kuzatish komponenti
- [x] 147. Kamera silkinishi (camera shake) API
- [x] 148. Tilemap: bir nechta qatlam
- [x] 149. Tilemap: auto-tile qoidalari (4/8 qo'shni)
- [x] 150. Tilemap: to'rtburchak va to'ldirish (flood fill) asboblari
- [x] 151. Tilemap: plitka bo'yicha collision bayroqlari
- [x] 152. Tilemap: one-way platformalar
- [x] 153. Tilemap: animatsiyali plitkalar
- [x] 154. Tilemap: kattalashtirish/kichraytirishda palitra
- [x] 155. Sprite outline/flash effekti (hit flash)
- [x] 156. Sprite rang tint animatsiyasi
- [x] 157. 2D yorug'lik (oddiy point light 2D, additive)
- [x] 158. Pixel-perfect kamera rejimi
- [x] 159. Sprite'larni Y bo'yicha saralash (top-down o'yinlar)
- [x] 160. 2D testlari

## M09 2D fizika II

- [x] 161. Distance joint 2D
- [x] 162. Revolute (hinge) joint 2D (motor, limit)
- [x] 163. Prismatic joint 2D
- [x] 164. Weld joint 2D
- [x] 165. Wheel joint 2D
- [x] 166. Polygon collider 2D (vertex muharriri)
- [x] 167. Edge/Chain collider 2D
- [x] 168. Physics material (ishqalanish/sakrash) asset
- [x] 169. Collision qatlamlari va maskalar (2D)
- [x] 170. One-way platform effektori
- [x] 171. Area effector (shamol/suv)
- [x] 172. Collision filtrlash testi
- [x] 173. OverlapCircle/OverlapBox so'rovlari 2D
- [x] 174. RaycastAll 2D
- [x] 175. Joint'lar uchun inspektor va wireframe
- [x] 176. Joint'lar serializatsiyasi
- [x] 177. Fizik ragdoll namunasi (2D)
- [x] 178. Box2D sub-step sozlamalari
- [x] 179. 2D fizika uchun sleeping sozlamasi
- [x] 180. 2D fizika testlari

## M10 3D fizika II

- [x] 181. Fixed joint 3D
- [x] 182. Hinge joint 3D (motor, limit)
- [x] 183. Distance joint 3D
- [x] 184. Slider joint 3D
- [x] 185. Convex hull collider (modeldan)
- [x] 186. Statik triangle mesh collider (modeldan)
- [x] 187. Collision qatlamlari va maskalar (3D)
- [x] 188. OverlapSphere/OverlapBox so'rovlari 3D
- [x] 189. SphereCast/ShapeCast 3D
- [x] 190. RaycastAll 3D
- [x] 191. Continuous collision detection (CCD) bayrog'i
- [x] 192. Sleeping/uyg'otish API
- [x] 193. Physics material 3D
- [x] 194. Trigger callback'lari 3D (OnTriggerBegin)
- [x] 195. Character controller dinamik jismlarni itarishi (kuch sozlamasi)
- [x] 196. Joint'lar inspektori va wireframe
- [x] 197. Joint'lar serializatsiyasi
- [x] 198. 3D ragdoll (oddiy) namunasi
- [x] 199. Fizika debug: contact nuqtalari
- [x] 200. 3D fizika testlari

## M11 Audio

- [x] 201. Audio mixer guruhlari (Master, Music, SFX, UI)
- [x] 202. Guruh balandligi va mute
- [x] 203. AudioListener komponenti (kamera o'rniga)
- [x] 204. Musiqa crossfade API
- [x] 205. Musiqani streaming (katta fayllar uchun)
- [x] 206. Ovoz pitch tasodifiyligi (random range)
- [x] 207. Bir vaqtda o'ynaydigan ovozlar limiti
- [x] 208. AudioSource: Play/Stop/Pause skript API
- [x] 209. AudioSource holatini inspektorda ko'rsatish
- [x] 210. Doppler effekti (oddiy)
- [x] 211. Audio clip asset (davomiylik, format)
- [x] 212. Ovoz so'nishi (fade in/out)
- [x] 213. Ovoz effektlari: low-pass filter
- [x] 214. Ovoz sozlamalari SaveData bilan saqlanadi
- [x] 215. Audio testlari (ovoz qurilmasisiz)
- [x] 216. Audio profiler (faol ovozlar soni)
- [x] 217. Editorda audio preview to'lqin shakli
- [x] 218. 3D ovoz uchun yo'nalishli konus
- [x] 219. Ambient zona (hajm ichida ovoz)
- [x] 220. Audio hodisalar (OnSoundFinished)

## M12 UI tizimi

- [x] 221. UI Image komponenti (sprite, 9-slice)
- [x] 222. UI Slider
- [x] 223. UI Toggle (checkbox)
- [x] 224. UI Progress bar
- [x] 225. UI matn kiritish (input field)
- [x] 226. Vertikal/gorizontal layout guruhlari
- [x] 227. Stretch anchor'lar (chapdan-o'ngga cho'zilish)
- [x] 228. Ekran o'lchamiga moslashuvchi masshtab (reference resolution)
- [x] 229. UI hodisalari: OnHover/OnPress/OnRelease
- [x] 230. UI uchun klaviatura/gamepad navigatsiyasi
- [x] 231. UI animatsiyalari (tween bilan)
- [x] 232. Bir nechta shrift o'lchamlari (atlas sifati)
- [x] 233. Matnni tekislash (chap/markaz/o'ng, yuqori/o'rta/past)
- [x] 234. Matn o'rash (word wrap) kenglik bo'yicha
- [x] 235. UI editorda tanlash va ko'chirish (2D gizmo)
- [x] 236. UI ierarxiyasi (ota UITransform'ga nisbatan)
- [x] 237. UI mask/clip (scroll view)
- [x] 238. Scroll view
- [x] 239. Dialog/menyu namunasi (pause menu)
- [x] 240. UI testlari

## M13 Animatsiya II

- [x] 241. Property animatsiya kliplari (.zanim): transform/rang kalit kadrlari
- [x] 242. Animatsiya timeline muharriri (editor oynasi)
- [x] 243. Kalit kadrlar interpolyatsiyasi (linear, step, bezier)
- [x] 244. Animatsiya event'lari (vaqtda skript callback)
- [x] 245. Root motion (glTF)
- [x] 246. Blend tree 1D (tezlik bo'yicha walk/run aralashtirish)
- [x] 247. Animatsiya qatlamlari (ustki tana) - oddiy
- [x] 248. Sprite animatsiya kliplari asset sifatida
- [x] 249. Animator parametrlarini inspektorda runtime ko'rsatish
- [x] 250. Holat mashinasi uchun vizual graf (node'lar)
- [x] 251. Animatsiya preview editorda (scrub)
- [x] 252. Skelet: suyakka obyekt biriktirish (attachment socket)
- [x] 253. Animatsiya tezligi bo'yicha qadam tovushlari event bilan
- [x] 254. Animatsiya kliplarini qisqartirish (start/end)
- [x] 255. glTF morph target'lar (blend shapes) - oddiy
- [x] 256. Animatsiya keshlash (performance)
- [x] 257. Bir nechta skin qo'llab-quvvatlash
- [x] 258. Animatsiya testlari (property clip, event)
- [x] 259. Animatsiya namunasi (eshik ochilishi)
- [x] 260. Animatsiya hujjatlari

## M14 Particle II

- [x] 261. Rang gradienti (ko'p nuqtali)
- [x] 262. O'lcham egri chizig'i (curve)
- [x] 263. Tezlik umr bo'yi o'zgarishi
- [x] 264. Tekstura sheet animatsiyasi (flipbook)
- [x] 265. Konus emitter shakli
- [x] 266. Mesh sirtidan emitter
- [x] 267. Sub-emitter (o'lganda portlash)
- [x] 268. Yer tekisligi bilan to'qnashuv (bounce)
- [x] 269. Shovqin (noise) turbulentlik
- [x] 270. Trail (iz) rendering
- [x] 271. Particle'larni GPU'ga ko'proq batch qilish
- [x] 272. Particle sorting (kameraga masofa)
- [x] 273. Prewarm (boshidan to'ldirilgan holat)
- [x] 274. Emissiya portlashlari jadvali (bursts list)
- [x] 275. Simulyatsiya tezligi va local/world
- [x] 276. Particle preset'lari kutubxonasi (olov, tutun, uchqun, yomg'ir, qor)
- [x] 277. Yomg'ir va qor effektlari (kamera atrofida)
- [x] 278. Particle inspektorida bo'limlar
- [x] 279. Particle serializatsiya testlari
- [x] 280. Particle performance testi

## M15 Sahna boshqaruvi

- [x] 281. Additive sahnalar (bir vaqtda bir nechta)
- [x] 282. DontDestroyOnLoad (persistent obyektlar)
- [x] 283. Asinxron sahna yuklash (loading screen)
- [x] 284. Sahna o'tishlari (fade in/out)
- [x] 285. Build sahnalar ro'yxati (loyiha sozlamalarida)
- [x] 286. LoadScene indeks bo'yicha
- [x] 287. Sahna yuklanganda/yopilganda hodisalar
- [x] 288. Sahna ichida qidirish API'lari tezlashtirish
- [x] 289. Sahnani yuklashda xatolarni ko'rsatish
- [x] 290. Sahnalarni solishtirish (diff) uchun barqaror UUID tartibi
- [x] 291. Sahna metadata (ta'rif, thumbnail)
- [x] 292. Editor: sahnalar ro'yxati paneli
- [x] 293. Editor: o'yin rejimida sahna almashganda editor holatini tiklash
- [x] 294. Global o'yin holati (singleton servislar)
- [x] 295. Sahna versiya migratsiyasi
- [x] 296. Sahna yuklash vaqtini profiler'da ko'rsatish
- [x] 297. Katta sahnalar uchun tezroq serializatsiya
- [x] 298. Sahnani bo'laklarga bo'lish (sub-scenes) - oddiy
- [x] 299. Sahna boshqaruvi testlari
- [x] 300. Sahna boshqaruvi hujjatlari

## M16 Editor II

- [x] 301. O'yin rejimida editor rang tusi (tint)
- [x] 302. Game view rezolyutsiya preset'lari (16:9, 4:3, 1080p)
- [x] 303. Konsolda matn bo'yicha filtr
- [x] 304. Konsolda bir xil xabarlarni guruhlash (collapse)
- [x] 305. Sozlamalar oynasi (Preferences): tema rangi, shrift o'lchami
- [x] 306. Klaviatura yorliqlari ro'yxati oynasi
- [x] 307. Avtomatik saqlash (autosave) va tiklash
- [x] 308. So'nggi sahnalar ro'yxati
- [x] 309. Undo tarixi paneli
- [x] 310. Selection uchun undo
- [x] 311. Editor layout'larini saqlash/yuklash
- [x] 312. Bir nechta viewport (yon tomondan/yuqoridan ko'rinish)
- [x] 313. Ortografik ko'rinishlar (Top/Front/Right) tugmalari
- [x] 314. Editor kamerasi tezligi sozlamasi va FOV
- [x] 315. Viewport'da obyekt nomlarini ko'rsatish
- [x] 316. Sahna statistikasi paneli (komponentlar soni)
- [x] 317. Editor ichida skript fayllarni ochish (tashqi muharrir)
- [x] 318. Kompilyatsiya xatolarini fayl/qator bilan ko'rsatish
- [x] 319. Editor yangilanishini kamaytirish (idle bo'lganda FPS cheklash)
- [x] 320. Editor II testlari (headless)

## M17 Loyiha sozlamalari

- [x] 321. Loyiha sozlamalari oynasi
- [x] 322. Input xaritasi muharriri
- [x] 323. Teglar va qatlamlar muharriri
- [x] 324. Fizika qatlamlari matritsasi
- [x] 325. Sifat sozlamalari (soya, SSAO, bloom default)
- [x] 326. Player oyna sozlamalari (o'lcham, fullscreen, vsync, sarlavha)
- [x] 327. O'yin ikonkasi
- [x] 328. Boshlang'ich sahna va build sahnalari
- [x] 329. Kompaniya/o'yin nomi va versiya
- [x] 330. Loyiha sozlamalari serializatsiyasi
- [x] 331. Loyiha sozlamalari testlari
- [x] 332. Grafik API va kontekst sozlamalari
- [x] 333. Vaqt sozlamalari (fixed timestep, time scale)
- [x] 334. Time.timeScale skript API (sekinlashtirish)
- [x] 335. Gravitatsiya standart qiymatlari loyiha bo'yicha
- [x] 336. Audio standart sozlamalari
- [x] 337. Lokalizatsiya: til fayllari (uz, ru, en)
- [x] 338. Lokalizatsiya skript API (Localize("key"))
- [x] 339. Editor tilini tanlash (o'zbek/ingliz)
- [x] 340. Loyiha sozlamalari hujjatlari

## M18 Runtime player

- [x] 341. Player oyna sozlamalarini loyihadan o'qish
- [x] 342. F11 bilan fullscreen almashtirish
- [x] 343. FPS cheklovi
- [x] 344. Splash ekran (Zamin logotipi)
- [x] 345. Player command line: --windowed, --resolution
- [x] 346. Player log faylga yozish
- [x] 347. Crash bo'lganda xabar oynasi
- [x] 348. Player'da kursorni yashirish/bloklash API
- [x] 349. Ekran o'lchami o'zgarishiga moslashish
- [x] 350. Eksportda ishlatilmagan assetlarni chiqarib tashlash
- [x] 351. Eksportda skript modulini optimizatsiya (Release)
- [x] 352. Eksport: .desktop fayli va ikonka
- [x] 353. Eksport hisobotini ko'rsatish (hajm, fayllar)
- [x] 354. Player'da pauza (fokus yo'qolganda)
- [x] 355. Gamma va yorqinlik sozlamasi player'da
- [x] 356. Player ishlash testi (headless)
- [x] 357. Eksport testi (avtomatik)
- [x] 358. Player uchun minimal o'yin menyusi namunasi
- [x] 359. Player versiya ma'lumoti
- [x] 360. Runtime hujjatlari

## M19 Matematika va core

- [x] 361. Perlin/Simplex shovqin funksiyalari
- [x] 362. AnimationCurve turi (kalit nuqtalar, baholash)
- [x] 363. Spline yo'llar (Catmull-Rom) va komponent
- [x] 364. Spline bo'ylab harakat skripti
- [x] 365. Rang utilitalari (HSV, lerp, hex)
- [x] 366. Math utilitalari (Remap, SmoothDamp, MoveTowards, Approximately)
- [x] 367. Quaternion yordamchilari (LookRotation, Slerp)
- [ ] 368. Job system / thread pool
- [ ] 369. Parallel for (particle va animatsiya uchun)
- [ ] 370. Xotira statistikasi profiler'da
- [ ] 371. Log fayl va log darajasi sozlamasi
- [ ] 372. Assert'larni yaxshilash (xabar bilan)
- [ ] 373. Timer va Stopwatch utilitalari
- [ ] 374. String utilitalari (split, trim, format)
- [ ] 375. UUID generatsiya testi kuchaytirish
- [ ] 376. Fayl tizimi yordamchilari (yo'l normalizatsiya)
- [ ] 377. Bounding volume yordamchilari (AABB, sfera, ray kesishuvi)
- [ ] 378. Geometriya testlari (ray-AABB, ray-sfera)
- [ ] 379. Math testlari
- [ ] 380. Core hujjatlari

## M20 Navigatsiya va AI

- [ ] 381. Grid asosidagi A* pathfinding (2D)
- [ ] 382. Pathfinding uchun grid'ni tilemap'dan yaratish
- [ ] 383. NavGrid 3D (yer tekisligi bo'yicha, to'siqlarni aniqlash)
- [ ] 384. NavAgent komponenti (yo'l bo'ylab yurish)
- [ ] 385. Yo'lni silliqlash (string pulling)
- [ ] 386. Dinamik to'siqlar (grid yangilash)
- [ ] 387. Steering: seek, flee, arrive, wander
- [ ] 388. Obstacle avoidance (raycast bilan)
- [ ] 389. AI holat mashinasi yordamchi klassi
- [ ] 390. Ko'rish maydoni (line of sight) tekshiruvi
- [ ] 391. Patrol yo'llari (waypoints) komponenti
- [ ] 392. Navigatsiya debug chizish
- [ ] 393. Navigatsiya testlari
- [ ] 394. AI namunasi: dushman ta'qib qiladi (2D)
- [ ] 395. AI namunasi: 3D NPC patrul
- [ ] 396. Formation/guruh harakati (oddiy)
- [ ] 397. Behavior tree (minimal) yordamchisi
- [ ] 398. Navigatsiya performance testi
- [ ] 399. Navigatsiya serializatsiyasi
- [ ] 400. AI hujjatlari

## M21 Prefab va serializatsiya II

- [ ] 401. Prefab instansiyalari asl prefabga bog'lanishi (link)
- [ ] 402. Prefab o'zgarishlarini instansiyalarga qo'llash (Apply)
- [ ] 403. Instansiyani asl holatiga qaytarish (Revert)
- [ ] 404. Prefab override'larini ko'rsatish inspektorda
- [ ] 405. Ichma-ich prefab'lar (nested)
- [ ] 406. Prefab muharriri rejimi (alohida ochish)
- [ ] 407. Prefab instansiyalarda EntityRef'larni qayta xaritalash
- [ ] 408. Dublikat qilishda EntityRef'larni ichki xaritalash
- [ ] 409. Sahna fayli versiyasi va migratsiya
- [ ] 410. Buzilgan sahna faylini tiklash (qisman yuklash)
- [ ] 411. Serializatsiya fuzz testi (tasodifiy JSON)
- [ ] 412. Katta sahnalar serializatsiya benchmark
- [ ] 413. Prefab variantlari (asosiy prefabdan meros)
- [ ] 414. Prefab thumbnail
- [ ] 415. Prefab'ni kontent brauzerdan sahnaga ko'p marta sudrash
- [ ] 416. Prefab o'zgarishi hot reload
- [ ] 417. Prefab serializatsiya testlari
- [ ] 418. Clipboard formatini prefab bilan birlashtirish
- [ ] 419. Sahna eksportida prefab'larni tekshirish
- [ ] 420. Prefab hujjatlari

## M22 Sifat va testlar

- [ ] 421. AddressSanitizer bilan build va testlar
- [ ] 422. UndefinedBehaviorSanitizer bilan testlar
- [ ] 423. Xotira oqishini tekshirish (LeakSanitizer)
- [ ] 424. Ogohlantirishlarni tozalash (-Wall -Wextra -Wpedantic)
- [ ] 425. Renderer uchun headless skrinshot testlari CI'da
- [ ] 426. Editor smoke test (barcha panellar ochiladi)
- [ ] 427. Skript hot reload avtomatik testi
- [ ] 428. Eksport avtomatik testi
- [ ] 429. Fizika determinizm testi
- [ ] 430. Performance benchmark (10k sprite, 1k mesh)
- [ ] 431. ECS benchmark
- [ ] 432. Serializatsiya benchmark
- [ ] 433. Test framework: test nomlari bo'yicha filtr
- [ ] 434. Test framework: vaqt hisoboti
- [ ] 435. Code coverage hisoboti (gcov)
- [ ] 436. clang-format konfiguratsiyasi
- [ ] 437. CI: sanitizer job
- [ ] 438. CI: artefaktlar (player build)
- [ ] 439. Regressiya testlari ro'yxati hujjati
- [ ] 440. Sifat hisoboti

## M23 Hujjatlar

- [ ] 441. docs/Scripting.md - C++ skriptlar qo'llanmasi
- [ ] 442. docs/Components.md - barcha komponentlar ma'lumotnomasi
- [ ] 443. docs/Editor.md - editor qo'llanmasi
- [ ] 444. docs/Tutorial-2D.md - birinchi 2D o'yin
- [ ] 445. docs/Tutorial-3D.md - birinchi 3D o'yin
- [ ] 446. docs/Physics.md
- [ ] 447. docs/Rendering.md
- [ ] 448. docs/Animation.md
- [ ] 449. docs/UI.md
- [ ] 450. docs/Audio.md
- [ ] 451. docs/Architecture.md - ichki arxitektura
- [ ] 452. docs/BuildAndExport.md
- [ ] 453. Doxygen konfiguratsiyasi
- [ ] 454. API izohlarini header'larda to'ldirish
- [ ] 455. FAQ va muammolarni hal qilish
- [ ] 456. Hissa qo'shish qo'llanmasi (CONTRIBUTING)
- [ ] 457. CHANGELOG
- [ ] 458. Rasmli qo'llanmalar (skrinshotlar)
- [ ] 459. README yangilash
- [ ] 460. Hujjatlar indeksi

## M24 Namuna o'yinlar

- [ ] 461. Top-down shooter (2D): o'yinchi, o'q otish, dushmanlar
- [ ] 462. Top-down shooter: to'lqinlar va hisob
- [ ] 463. Breakout (2D): raketka, to'p, g'ishtlar
- [ ] 464. Breakout: bonuslar va darajalar
- [ ] 465. 3D FPS controller namunasi (sichqoncha bilan qarash)
- [ ] 466. FPS: nishonlar va otish (raycast)
- [ ] 467. Puzzle o'yin (match-3 yoki sokoban)
- [ ] 468. Endless runner (3D)
- [ ] 469. Racing (oddiy mashina fizikasi)
- [ ] 470. Namunalarni launcher'da ko'rsatish (rasm bilan)
- [ ] 471. Har bir namuna uchun README
- [ ] 472. Namunalar uchun headless testlar
- [ ] 473. Namuna assetlari (procedural)
- [ ] 474. Namunalarda ovozlar (procedural wav)
- [ ] 475. Namunalarda UI menyular
- [ ] 476. Namunalarda SaveData (rekord)
- [ ] 477. Namunalarda input action'lar
- [ ] 478. Namunalarni eksport qilib tekshirish
- [ ] 479. Namunalar galereyasi (docs)
- [ ] 480. Namunalar performance tekshiruvi

## M25 Optimallashtirish va sayqal

- [ ] 481. Renderer2D batching optimallashtirish (kamroq flush)
- [ ] 482. Kadrlararo ajratmalarni kamaytirish (per-frame allocations)
- [ ] 483. World transform keshlash (dirty flag)
- [ ] 484. ECS view'larni keshlash
- [ ] 485. Font atlas keshlash va kerning
- [ ] 486. Tekstura yuklashni fon oqimida (async)
- [ ] 487. Model yuklashni fon oqimida
- [ ] 488. Shader'larni keshlash (program binary)
- [ ] 489. Uniform'larni UBO'ga ko'chirish
- [ ] 490. Shadow pass'da culling
- [ ] 491. Particle simulyatsiyasini parallellashtirish
- [ ] 492. Animatsiya baholashni parallellashtirish
- [ ] 493. Editor UI performance (katta ierarxiyalar - clipper)
- [ ] 494. Kontent brauzer katta papkalar (clipper)
- [ ] 495. Start vaqtini qisqartirish
- [ ] 496. Xotira ishlatilishini kamaytirish
- [ ] 497. Barcha namunalar 60 FPS tekshiruvi
- [ ] 498. Profiler orqali bottleneck hisobot
- [ ] 499. Oxirgi sayqal: UI matnlari va tarjimalar
- [ ] 500. Yakuniy regressiya va barcha testlar
