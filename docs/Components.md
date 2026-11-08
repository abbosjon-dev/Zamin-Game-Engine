# Zamin Engine: komponentlar ma'lumotnomasi

Sahnadagi har bir obyekt (entity) — bir nechta komponentdan iborat: joylashuv (`TransformComponent`), tasvir
(`SpriteRendererComponent`, `MeshRendererComponent` ...), fizika, audio, UI, skript. Bu hujjat dvigateldagi **hamma**
komponentlarni sanaydi: nima qiladi, editorda qayerdan qo'shiladi, sahna faylida qaysi kalit bilan yoziladi, qanday
maydonlari bor va skriptdan qanday ishlatiladi.

Har bo'limning `<!-- gen:... -->` belgilari orasidagi qismi (inspektor nomi, menyu yo'li, sahna kaliti, maydonlar
jadvali) `Tests/docs_components.py` bilan **manba koddan yaratiladi**: `Zamin/src/Zamin/Scene/Components.h`
(`AllComponents` ro'yxati va strukturalar), `Editor/src/Panels/SceneHierarchyPanel.cpp` (inspektor bo'limlari va
**+ Komponent** menyusi) va `Zamin/src/Zamin/Scene/SceneSerializer.cpp` (sahna kalitlari). Komponent o'zgarsa
`Tests/docs_components.py --update docs/Components.md` ni ishga tushiring; `Tests/docs_check.py` (ctest: `DocsCheck`)
bloklar eskirganini yoki yangi komponent hujjatlanmaganini aniqlaydi. Tavsiflar qo'lda yozilgan.

Jadvallarda: **Standart** — koddagi boshlang'ich qiymat; _(faqat o'yin paytida)_ — sahnaga saqlanmaydigan, o'yin
davomida dvigatel yoki skript to'ldiradigan maydon (skriptdan o'qish uchun). Burchaklar `TransformComponent`'da
radianda, inspektorda gradusda ko'rsatiladi.

Skriptdan foydalanish: `GetComponent<T>()`, `TryGetComponent<T>()`, `HasComponent<T>()`, `AddComponent<T>()`
([Scripting.md](Scripting.md#7-obyektlar-komponentlar-ierarxiya)). Editorda: obyektni tanlang → **Inspektor** →
**+ Komponent** (qidiruv maydoni bor); komponent sarlavhasidagi tishli g'ildirak menyusi — **Komponentni
nusxalash**, joylash, **Yuqoriga / Pastga ko'chirish**, **Komponentni olib tashlash**.

Mundarija:

1. [Asosiy](#1-asosiy) — ID, nom, Transform, ierarxiya, teglar
2. [Render](#2-render) — sprite, doira, matn, mesh, animatsiya, kamera
3. [Yorug'lik](#3-yoruglik)
4. [Fizika 2D](#4-fizika-2d)
5. [Fizika 3D](#5-fizika-3d)
6. [Navigatsiya va AI](#6-navigatsiya-va-ai)
7. [Effektlar va 2D yordamchilar](#7-effektlar-va-2d-yordamchilar)
8. [UI (ekran interfeysi)](#8-ui-ekran-interfeysi)
9. [Audio](#9-audio)
10. [Skript, sub-sahna va editor ma'lumotlari](#10-skript-sub-sahna-va-editor-malumotlari)

---

## 1. Asosiy

### IDComponent

Obyektning o'zgarmas noyob identifikatori (`UUID`, 64 bit). Sahna faylida `"Entity"`, havolalar (`EntityRef`,
ota-ona, joint'ning ulangan jismi) shu raqam orqali saqlanadi. Skriptda: `GetEntity().GetUUID()`,
`GetScene().GetEntityByUUID(id)`. Inspektor sarlavhasida `ID: ...` bo'lib ko'rinadi.

<!-- gen:IDComponent -->
_har bir obyektda bor; sahna faylida: `"Entity"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ID` | `UUID` | — |
<!-- /gen:IDComponent -->

### TagComponent

Obyektning **nomi** (`Tag`, ierarxiyada ko'rinadigan) va o'z **faollik** belgisi (`Active`). Obyekt o'yinda faqat o'zi va
hamma ota-onalari faol bo'lsa ishtirok etadi. Belgini `SetActive` bilan o'zgartiring (skript `SetActive(false)`,
`entity.SetActive(...)`, `Scene::SetActive`) — shunda skriptlar darhol `OnDisable` / `OnEnable` oladi; to'g'ridan-to'g'ri
yozish keyingi kadrda seziladi. Inspektorda: sarlavhadagi belgilash katakchasi va nom maydoni. Nomni o'zgartirish:
`SetName("...")` (`FindEntity` keshi yangilanadi). O'yin teglari bilan adashtirmang — ular `TagsComponent`.

<!-- gen:TagComponent -->
_har bir obyektda bor; sahna faylida: `"Tag"` (nom) va `"Active"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Tag` | `std::string` | — |
| `Active` | `bool` | `true` |
<!-- /gen:TagComponent -->

### TransformComponent

Obyektning joyi (`Translation`), aylanishi (`Rotation` — Euler burchaklar, radian) va masshtabi (`Scale`). Bolaning
transform'i otasiga nisbatan. Inspektorda **Pozitsiya**, **Aylanish** (gradus), **Masshtab**; viewport'da gizmo bilan
(`W` / `E` / `R`). Yordamchilar: `GetTransform()` (matritsa), `GetForward()` (-Z), `GetRight()` (+X), `GetUp()` (+Y).
Dunyo koordinatasi: skriptda `GetWorldPosition()`, `GetScene().GetWorldPosition(entity)`. Static va Kinematic 2D
jismlar transformga ergashadi (skript ularni transform bilan surishi mumkin), Dynamic jismning joyini esa fizika
yozadi — uni ko'chirish uchun `SetPosition2D` / `SetPosition3D`.

```cpp
Transform().Rotation += Speed * (float)ts;   // Rotator (yangi loyiha shabloni)
```

<!-- gen:TransformComponent -->
_Inspektorda: **Transform**; har bir obyektda bor; sahna faylida: `"Transform"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Translation` | `glm::vec3` | `{ 0.0f, 0.0f, 0.0f }` |
| `Rotation` | `glm::vec3` | `{ 0.0f, 0.0f, 0.0f }` |
| `Scale` | `glm::vec3` | `{ 1.0f, 1.0f, 1.0f }` |
<!-- /gen:TransformComponent -->

### RelationshipComponent

Ota-ona / bola ierarxiyasi: `Parent` (0 — ildiz) va `Children`. Ierarxiya panelida obyektni boshqasiga sudrab bola
qilinadi; skriptda `SetParent(parent, keepWorldTransform)`, `GetParent()`, `GetChildren()` ...
([Scripting.md](Scripting.md#7-obyektlar-komponentlar-ierarxiya)). Sahna faylida faqat `"Parent"` yoziladi, bolalar
ro'yxati yuklashda tiklanadi.

<!-- gen:RelationshipComponent -->
_har bir obyektda bor; sahna faylida: `"Parent"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Parent` | `UUID` | `0` |
| `Children` | `std::vector<UUID>` | — |
<!-- /gen:RelationshipComponent -->

### TagsComponent

O'yin teglari (`"Player"`, `"Enemy"`, `"Coin"` ...) — obyektlarni guruhlash va qidirish uchun. Inspektordagi
**Teglar** bo'limida yozib (Enter) yoki loyihaning teg ro'yxatidan tanlab qo'shiladi (ro'yxat: **Loyiha sozlamalari →
Teglar**). Ierarxiya qidiruvida `#Enemy` deb yozilsa, shu tegli obyektlar chiqadi. Skriptda: `HasTag`, `AddTag`,
`RemoveTag`, `FindEntitiesWithTag`, `FindEntityWithTag`, `other.HasTag("Player")` (indekslangan qidiruv).

<!-- gen:TagsComponent -->
_Inspektorda: **Teglar**; qo'shish: **+ Komponent → Boshqa → Teglar (Tags)**; sahna faylida: `"Tags"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Tags` | `std::vector<std::string>` | — |
<!-- /gen:TagsComponent -->

## 2. Render

### SpriteRendererComponent

2D to'rtburchak tasvir: rang (`Color`) va tekstura (`TexturePath`, `Assets` ga nisbatan) yoki teksturaning nomlangan
kadri (`SpriteName`, **Sprite muharriri** da kesilgan). `DrawMode`: `Simple` (birlik kvadrat, masshtab bilan),
`Sliced` (9-slice: burchaklar `SliceBorder` bo'yicha o'z o'lchamida) va `Tiled` (`Size` ni to'ldiradi). `PixelArt` —
aniq piksellar (nearest), `FlipX` / `FlipY`, `Lit` — 2D yorug'lik (`Light2DComponent`) ta'sir qiladi. 2D renderlar
saralash qatlami (`SortingLayer`, **Loyiha sozlamalari → Saralash qatlamlari**) va `SortingOrder` bo'yicha orqadan oldinga
chiziladi. Effektlar: urilish chaqnashi (`FlashColor` / `FlashAmount`, skriptda `Flash()`) va kontur
(`OutlineColor` / `OutlineThickness`, `SetOutline()`). UI Transform bilan ekranda chiziladi. Yaratish: **Obyekt →
2D → Sprite**.

<!-- gen:SpriteRendererComponent -->
_Inspektorda: **Sprite Renderer**; qo'shish: **+ Komponent → Render → Sprite Renderer**; sahna faylida: `"SpriteRenderer"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Color` | `glm::vec4` | `{ 1.0f, 1.0f, 1.0f, 1.0f }` |
| `Texture` | `Ref<Texture2D>` | — |
| `TexturePath` | `std::string` | — |
| `SpriteName` | `std::string` | — |
| `TilingFactor` | `float` | `1.0f` |
| `SortingLayer` | `uint32_t` | `0` |
| `SortingOrder` | `int` | `0` |
| `PixelArt` | `bool` | `false` |
| `FlipX` | `bool` | `false` |
| `FlipY` | `bool` | `false` |
| `Lit` | `bool` | `false` |
| `DrawMode` | `SpriteDrawMode` | `SpriteDrawMode::Simple` |
| `Size` | `glm::vec2` | `{ 1.0f }` |
| `SliceBorder` | `glm::vec4` | `{ 0.0f }` |
| `PixelsPerUnit` | `float` | `100.0f` |
| `FlashColor` | `glm::vec4` | `{ 1.0f }` |
| `FlashAmount` | `float` | `0.0f` |
| `OutlineColor` | `glm::vec4` | `{ 1.0f, 1.0f, 1.0f, 1.0f }` |
| `OutlineThickness` | `float` | `0.0f` |
| `FlashPlayingColor` _(faqat o'yin paytida)_ | `glm::vec4` | `{ 1.0f }` |
| `FlashTime` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `FlashDuration` _(faqat o'yin paytida)_ | `float` | `0.0f` |
<!-- /gen:SpriteRendererComponent -->

### SpriteAnimationComponent

Sprite Renderer teksturasida kadrlar ijro etadi: tekstura panjarasi kataklari (`Columns` x `Rows`, chapdan o'ngga,
yuqoridan pastga raqamlangan; `StartFrame`, `FrameCount`, `FPS`, `Loop`) yoki sprite varag'ining nomlangan kadrlari
(`Frames`). `Clips` — `.zsanim` sprite kliplari: skript `PlaySpriteClip("HeroSide")` bilan nomi bo'yicha qo'yadi;
klip kadrlar, tezlik, takrorlanish va event'larni beradi, birinchisi o'yin boshida (`PlayOnStart`). Skriptdan yana:
`GetComponent<SpriteAnimationComponent>().Play(start, count, fps, loop)` (Platformer2D qahramoni) va
`PlaySpriteSequence("run")`. Batafsil: [Animation.md](Animation.md).

<!-- gen:SpriteAnimationComponent -->
_Inspektorda: **Sprite Animation**; qo'shish: **+ Komponent → Render → Sprite Animation**; sahna faylida: `"SpriteAnimation"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Columns` | `int` | `4` |
| `Rows` | `int` | `1` |
| `StartFrame` | `int` | `0` |
| `FrameCount` | `int` | `4` |
| `FPS` | `float` | `10.0f` |
| `Loop` | `bool` | `true` |
| `Playing` | `bool` | `true` |
| `Frames` | `std::vector<std::string>` | — |
| `Clips` | `std::vector<std::string>` | — |
| `PlayOnStart` | `bool` | `true` |
| `Time` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `CurrentClip` _(faqat o'yin paytida)_ | `int` | `-1` |
| `ClipAssets` _(faqat o'yin paytida)_ | `std::vector<Ref<SpriteClipAsset>>` | — |
| `LoadedClips` _(faqat o'yin paytida)_ | `std::vector<std::string>` | — |
| `AppliedRevision` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `LastFrame` _(faqat o'yin paytida)_ | `int` | `-1` |
| `Started` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:SpriteAnimationComponent -->

### TintAnimationComponent

2D render (Sprite, Circle, Text, Tilemap) va Light 2D rangini gradient bo'yicha animatsiya qiladi: chiziladigan rang =
render rangi x gradient rangi (`Keys`, `Duration` ga nisbatan 0..1). `Mode`: `Once`, `Loop`, `PingPong`; `Cycles > 0` —
shuncha marta; `StartOffset` — nusxalar bir xil vaqtda miltillamasligi uchun. Render'ning o'z `Color` i o'zgarmaydi.
Skriptda: `PlayTint()`, `PlayTint(color, duration, mode, cycles)`, `StopTint()`, `IsTintPlaying()`.

<!-- gen:TintAnimationComponent -->
_Inspektorda: **Tint Animation**; qo'shish: **+ Komponent → Render → Tint Animation (rang)**; sahna faylida: `"TintAnimation"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Keys` | `std::vector<TintKey>` | `{ TintKey{ 0.0f, glm::vec4(1.0f) }, TintKey{ 1.0f, glm::vec4(1.0f, 0.35f, 0.3f, 1.0f) } }` |
| `Duration` | `float` | `1.0f` |
| `Mode` | `TintPlayMode` | `TintPlayMode::PingPong` |
| `Easing` | `Ease` | `Ease::InOutQuad` |
| `Cycles` | `int` | `0` |
| `StartOffset` | `float` | `0.0f` |
| `PlayOnStart` | `bool` | `true` |
| `Playing` _(faqat o'yin paytida)_ | `bool` | `false` |
| `EditorPreview` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Elapsed` _(faqat o'yin paytida)_ | `float` | `0.0f` |
<!-- /gen:TintAnimationComponent -->

### CircleRendererComponent

2D doira yoki halqa: `Color`, `Thickness` (1 — to'la doira, kichikroq — halqa), `Fade` (chetining yumshoqligi),
saralash qatlami va tartibi. Yaratish: **Obyekt → 2D → Doira** yoki **Fizik shar**. [2D qo'llanmada](Tutorial-2D.md)
tangalar shu komponent bilan chiziladi.

<!-- gen:CircleRendererComponent -->
_Inspektorda: **Circle Renderer**; qo'shish: **+ Komponent → Render → Circle Renderer**; sahna faylida: `"CircleRenderer"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Color` | `glm::vec4` | `{ 1.0f, 1.0f, 1.0f, 1.0f }` |
| `Thickness` | `float` | `1.0f` |
| `Fade` | `float` | `0.005f` |
| `SortingLayer` | `uint32_t` | `0` |
| `SortingOrder` | `int` | `0` |
<!-- /gen:CircleRendererComponent -->

### TextComponent

Matn: `Text` (yoki `LocalizationKey` — o'yin tilida avtomatik), rang, harf / qator oralig'i, tekislash (`AlignH`,
`AlignV`), so'z bo'yicha ko'chirish (`WordWrap`), sig'masa (`Overflow`), shrift (`FontPath`, bo'sh — standart Inter).
UI Transform bilan ekran to'rtburchagida (shrift o'lchami — UI Transform'ning `FontSize`, bo'lmasa to'rtburchak
balandligi), aks holda dunyoda (1 birlik = shrift o'lchami). Yaratish: **Obyekt → 2D → Matn** yoki **Obyekt → UI
(ekran) → Matn**. Skriptda `TryGetComponent<TextComponent>()->Text = ...` (ScoreHud, GameManager).

<!-- gen:TextComponent -->
_Inspektorda: **Text**; qo'shish: **+ Komponent → Render → Text**; sahna faylida: `"Text"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Text` | `std::string` | `"Matn"` |
| `LocalizationKey` | `std::string` | — |
| `Color` | `glm::vec4` | `{ 1.0f }` |
| `Kerning` | `float` | `0.0f` |
| `LineSpacing` | `float` | `0.0f` |
| `AlignH` | `TextAlignH` | `TextAlignH::Left` |
| `AlignV` | `TextAlignV` | `TextAlignV::Middle` |
| `WordWrap` | `bool` | `false` |
| `Overflow` | `TextOverflow` | `TextOverflow::Overflow` |
| `MaxWidth` | `float` | `0.0f` |
| `FontPath` | `std::string` | — |
| `FontAsset` | `Ref<Font>` | — |
| `SortingLayer` | `uint32_t` | `0` |
| `SortingOrder` | `int` | `1000` |
| `LocalizedText` _(faqat o'yin paytida)_ | `std::string` | — |
| `LocalizedFor` _(faqat o'yin paytida)_ | `std::string` | — |
| `LocalizedVersion` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `HasLocalizedText` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:TextComponent -->

### MeshRendererComponent

3D mesh: primitiv (`Type`: `Cube`, `Sphere`, `Plane`, `Cylinder`, `Cone`) yoki model (`Model`, `ModelPath` —
`.gltf` / `.glb` / `.obj`). Material: asset (`MaterialPath`, `.zmat`; modelning har bo'lagi uchun
`SubmeshMaterials`) yoki ichki material (`Mat`: albedo, metallic, roughness, emission, teksturalar ...).
`CastShadows` — soya tashlaydi. `MorphWeights` — glTF blend shape og'irliklari (`SetMorphWeight("Smile", 0.8f)`).
Skriptda: `GetMaterial()` — obyektning o'z material nusxasi, `GetSharedMaterial()`, `SetSharedMaterial(path)`
([Scripting.md](Scripting.md#14-grafika-rang-va-effektlar)). Yaratish: **Obyekt → 3D → Kub / Shar / Tekislik / Silindr
/ Konus / Model (glTF / OBJ)**.

<!-- gen:MeshRendererComponent -->
_Inspektorda: **Mesh Renderer**; qo'shish: **+ Komponent → Render → Mesh Renderer**; sahna faylida: `"MeshRenderer"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Type` | `PrimitiveMesh` | `PrimitiveMesh::Cube` |
| `ModelPath` | `std::string` | — |
| `MeshAsset` _(faqat o'yin paytida)_ | `Ref<Mesh>` | — |
| `ModelAsset` _(faqat o'yin paytida)_ | `Ref<Model>` | — |
| `MaterialPath` | `std::string` | — |
| `SubmeshMaterials` | `std::vector<std::string>` | — |
| `MaterialRef` _(faqat o'yin paytida)_ | `Ref<MaterialAsset>` | — |
| `SubmeshMaterialRefs` _(faqat o'yin paytida)_ | `std::vector<Ref<MaterialAsset>>` | — |
| `MaterialInstance` _(faqat o'yin paytida)_ | `Ref<MaterialAsset>` | — |
| `SubmeshMaterialInstances` _(faqat o'yin paytida)_ | `std::vector<Ref<MaterialAsset>>` | — |
| `Mat` | `Material` | — |
| `AlbedoPath` | `std::string` | — |
| `NormalPath` | `std::string` | — |
| `MetalRoughPath` | `std::string` | — |
| `EmissivePath` | `std::string` | — |
| `CastShadows` | `bool` | `true` |
| `MorphWeights` | `std::vector<float>` | — |
| `MorphMeshes` _(faqat o'yin paytida)_ | `std::vector<Ref<Mesh>>` | — |
| `MorphMeshWeights` _(faqat o'yin paytida)_ | `std::vector<std::vector<float>>` | — |
| `MorphMeshSources` _(faqat o'yin paytida)_ | `std::vector<const Mesh*>` | — |
| `MorphMeshOwner` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
<!-- /gen:MeshRendererComponent -->

### AnimatorComponent

Teriga bog'langan (skinned) glTF modelning skeletal animatsiyalarini ijro etadi. Ikki usul: to'g'ridan-to'g'ri
`Play("Walk", loop, speed, fade)`, yoki holat mashinasi — `Parameters` (float, bool, trigger), `States` (klip yoki 1D
blend tree), `Transitions` (shartlar, exit time) — skript faqat parametr beradi (`SetFloat("Speed", v)`, `SetBool`,
`SetTrigger`). Qatlamlar (`Layers`, avatar niqobi bilan), root motion (`ApplyRootMotion`), event'lar (`Events`),
ko'rinmaganda hisoblamaslik (`Culling`) va poza keshi (`CachePoses`). Grafni **Oyna → Animator grafi** da tahrirlash
mumkin. Batafsil: [Animation.md](Animation.md); namuna: `Projects/Adventure3D` (Robot).

<!-- gen:AnimatorComponent -->
_Inspektorda: **Animator**; qo'shish: **+ Komponent → Render → Animator (skeletal)**; sahna faylida: `"Animator"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Clip` | `std::string` | — |
| `Speed` | `float` | `1.0f` |
| `Loop` | `bool` | `true` |
| `Playing` | `bool` | `true` |
| `PlayInEditor` | `bool` | `true` |
| `DefaultFade` | `float` | `0.2f` |
| `Time` | `float` | `0.0f` |
| `BlendTree` _(faqat o'yin paytida)_ | `int` | `-1` |
| `JointMatrices` _(faqat o'yin paytida)_ | `std::vector<glm::mat4>` | — |
| `BlendWeights` _(faqat o'yin paytida)_ | `std::vector<float>` | — |
| `PreviousClip` _(faqat o'yin paytida)_ | `std::string` | — |
| `PreviousBlendTree` _(faqat o'yin paytida)_ | `int` | `-1` |
| `PreviousState` _(faqat o'yin paytida)_ | `int` | `-1` |
| `PreviousTime` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `PreviousSpeed` _(faqat o'yin paytida)_ | `float` | `1.0f` |
| `PreviousLoop` _(faqat o'yin paytida)_ | `bool` | `true` |
| `FadeDuration` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `FadeElapsed` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `MorphWeights` _(faqat o'yin paytida)_ | `std::vector<float>` | — |
| `EditorPreview` | `EditorPreviewState` | — |
| `Events` | `std::vector<AnimatorEvent>` | — |
| `EventsAtStart` _(faqat o'yin paytida)_ | `bool` | `true` |
| `ApplyRootMotion` | `bool` | `false` |
| `RootMotionBone` | `std::string` | — |
| `Culling` | `AnimatorCulling` | `AnimatorCulling::AlwaysAnimate` |
| `CachePoses` | `bool` | `false` |
| `PoseCacheRate` | `float` | `30.0f` |
| `VisibleFrame` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Culled` _(faqat o'yin paytida)_ | `bool` | `false` |
| `PoseSignature` _(faqat o'yin paytida)_ | `std::vector<uint32_t>` | — |
| `RootMotionVelocity` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `RootMotionTurnRate` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Layers` | `std::vector<AnimatorLayer>` | — |
| `Parameters` | `std::vector<AnimatorParameter>` | — |
| `States` | `std::vector<AnimatorState>` | — |
| `Transitions` | `std::vector<AnimatorTransition>` | — |
| `DefaultState` | `int` | `0` |
| `CurrentState` _(faqat o'yin paytida)_ | `int` | `-1` |
| `AnyStateGraphPosition` | `glm::vec2` | `{ 0.0f }` |
<!-- /gen:AnimatorComponent -->

### AnimationPlayerComponent

Property animatsiya kliplarini (`.zanim`, **Oyna → Animatsiya** timeline'ida yasaladi) obyekt va uning bolalarida
ijro etadi: transform, rang, yorug'lik, UI va skript `float` maydonlari, event'lar bilan. Birinchi klip o'yin bilan
boshlanadi (`PlayOnStart`); `Wrap` — klip oxirida nima bo'lishi. Skriptda: `PlayAnimation("DoorOpen")`,
`PlayAnimation("DoorOpen", -1.0f)` (orqaga), `StopAnimation()` yoki
`GetComponent<AnimationPlayerComponent>().Play(...)`. Namuna: Adventure3D darvozasi
(`Animations/DarvozaOchilishi.zanim`). Batafsil: [Animation.md](Animation.md).

<!-- gen:AnimationPlayerComponent -->
_Inspektorda: **Animation Player**; qo'shish: **+ Komponent → Render → Animation Player (.zanim kliplar)**; sahna faylida: `"AnimationPlayer"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Clips` | `std::vector<std::string>` | — |
| `Speed` | `float` | `1.0f` |
| `PlayOnStart` | `bool` | `true` |
| `Wrap` | `AnimationPlayerWrap` | `AnimationPlayerWrap::Clip` |
| `ClipAssets` _(faqat o'yin paytida)_ | `std::vector<Ref<PropertyClipAsset>>` | — |
| `LoadedClips` _(faqat o'yin paytida)_ | `std::vector<std::string>` | — |
| `Current` _(faqat o'yin paytida)_ | `int` | `-1` |
| `Cursor` _(faqat o'yin paytida)_ | `ClipCursor` | — |
| `PlaySpeed` _(faqat o'yin paytida)_ | `float` | `1.0f` |
| `Playing` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Started` _(faqat o'yin paytida)_ | `bool` | `false` |
| `PoseDirty` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:AnimationPlayerComponent -->

### CameraComponent

Kamera: `Camera` (`SceneCamera` — proyeksiya **Perspektiv** yoki **Ortografik**, ko'rish burchagi / ortografik
o'lcham, yaqin va uzoq tekisliklar), `Primary` — o'yin shu kameradan ko'rsatiladi (bitta asosiy kamera bo'lishi
kerak; asosiy kamerasiz sahna chizilmaydi), `FixedAspectRatio`, `ClearColor` (fon rangi, osmon chizilmaganda),
`Shake` (silkinish sozlamalari: skriptda `ShakeCamera()`), `PixelPerfect` (2D piksel-art uchun). Yaratish: **Obyekt →
Kamera → Perspektiv (3D)** yoki **Ortografik (2D)**.

<!-- gen:CameraComponent -->
_Inspektorda: **Camera**; qo'shish: **+ Komponent → Render → Camera**; sahna faylida: `"Camera"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Camera` | `SceneCamera` | — |
| `Primary` | `bool` | `true` |
| `FixedAspectRatio` | `bool` | `false` |
| `ClearColor` | `glm::vec4` | `{ 0.08f, 0.09f, 0.11f, 1.0f }` |
| `Shake` | `CameraShake` | — |
| `PixelPerfect` | `PixelPerfectSettings` | — |
<!-- /gen:CameraComponent -->

## 3. Yorug'lik

### DirectionalLightComponent

Quyosh kabi parallel nurlar: obyektning oldinga (-Z) yo'nalishi bo'yicha. `Color`, `Intensity`, `CastShadows`
(cascaded soyalar; sifati — **Sahna sozlamalari**). Bir nechta quyosh bo'lishi mumkin. Yaratish: **Obyekt → Yorug'lik →
Quyosh (yo'naltirilgan)**.

<!-- gen:DirectionalLightComponent -->
_Inspektorda: **Directional Light**; qo'shish: **+ Komponent → Yorug'lik → Directional Light**; sahna faylida: `"DirectionalLight"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Color` | `glm::vec3` | `{ 1.0f, 0.96f, 0.9f }` |
| `Intensity` | `float` | `3.0f` |
| `CastShadows` | `bool` | `true` |
<!-- /gen:DirectionalLightComponent -->

### PointLightComponent

Nuqtadan har tomonga tarqaladigan yorug'lik (lampa, olov, kristall): `Color`, `Intensity`, `Radius` (ta'sir
masofasi). Yaratish: **Obyekt → Yorug'lik → Nuqtali**.

<!-- gen:PointLightComponent -->
_Inspektorda: **Point Light**; qo'shish: **+ Komponent → Yorug'lik → Point Light**; sahna faylida: `"PointLight"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Color` | `glm::vec3` | `{ 1.0f }` |
| `Intensity` | `float` | `5.0f` |
| `Radius` | `float` | `10.0f` |
<!-- /gen:PointLightComponent -->

### SpotLightComponent

Obyekt joyidan oldinga (-Z) qaragan konus nur (fonar, sahna chirog'i): `InnerAngle` ichida to'liq yorug', `OuterAngle`
gacha silliq so'nadi (gradus, o'qdan); `Range` — masofa. Yaratish: **Obyekt → Yorug'lik → Spot (konus)** (4 m balandda,
pastga qaragan).

<!-- gen:SpotLightComponent -->
_Inspektorda: **Spot Light**; qo'shish: **+ Komponent → Yorug'lik → Spot Light**; sahna faylida: `"SpotLight"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Color` | `glm::vec3` | `{ 1.0f }` |
| `Intensity` | `float` | `20.0f` |
| `Range` | `float` | `15.0f` |
| `InnerAngle` | `float` | `20.0f` |
| `OuterAngle` | `float` | `30.0f` |
<!-- /gen:SpotLightComponent -->

### Light2DComponent

2D yorug'lik (faqat `Lit` yoqilgan sprite / tilemap'larga ta'sir qiladi; sahnaning 2D atrof yorug'ligi — **Sahna
sozlamalari**): `Type` — `Point` yoki `Spot` (`SpotAngle`, `SpotSoftness`), `Radius`, `InnerRadius`, `Falloff`, miltillash
(`Flicker`, `FlickerSpeed` — olov uchun).

<!-- gen:Light2DComponent -->
_Inspektorda: **Light 2D**; qo'shish: **+ Komponent → Yorug'lik → Light 2D**; sahna faylida: `"Light2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Type` | `Light2DType` | `Light2DType::Point` |
| `Color` | `glm::vec3` | `{ 1.0f, 0.85f, 0.6f }` |
| `Intensity` | `float` | `1.0f` |
| `Radius` | `float` | `4.0f` |
| `InnerRadius` | `float` | `0.0f` |
| `Falloff` | `float` | `2.0f` |
| `SpotAngle` | `float` | `60.0f` |
| `SpotSoftness` | `float` | `0.3f` |
| `Flicker` | `float` | `0.0f` |
| `FlickerSpeed` | `float` | `8.0f` |
<!-- /gen:Light2DComponent -->

## 4. Fizika 2D

2D fizika — Box2D. Jism (`Rigidbody2DComponent`) va bir yoki bir nechta collider bir obyektda bo'ladi; jismsiz collider
fizik dunyoga qo'shilmaydi. Tortishish va qadam sozlamalari — **Sahna sozlamalari → Fizika 2D**, loyiha standarti —
**Loyiha sozlamalari** ([ProjectSettings.md](ProjectSettings.md)). Collider'larning umumiy maydonlari: `Density`,
`Friction`, `Restitution` (sakrovchanlik), `RestitutionThreshold`, `IsSensor` (inspektorda **Trigger (sensor)** —
to'qnashmaydi, `OnTriggerBegin` / `OnTriggerEnd` beradi), `MaterialPath` (`.zphysmat` fizik material — bo'lsa yuqoridagi
qiymatlar o'rniga), `Layer` (-1 — jismning qatlami).

### Rigidbody2DComponent

2D jism. `Type` (inspektorda **Turi**): `Static` (qimirlamaydi: yer, devor), `Dynamic` (kuch va tortishish ta'sir
qiladi), `Kinematic` (faqat tezlik / skript bilan harakatlanadi, ustidagilarni olib yuradi). `FixedRotation`
(**Aylanmasin** — qahramonlar uchun), `GravityScale`, so'nish, `Bullet` (tez obyektlar devordan o'tib ketmasligi uchun),
`Layer` (to'qnashuv qatlami, **Loyiha sozlamalari → Fizika qatlamlari**), `Sleeping` (uxlash rejimi). Skriptda:
`GetVelocity`, `SetVelocity`, `ApplyImpulse`, `ApplyForce`, `SetPosition2D`, `IsSleeping`, `WakeUp`. Yaratish: **Obyekt
→ 2D → Fizik quti / Fizik shar** (Sprite/Circle + Rigidbody 2D + collider).

<!-- gen:Rigidbody2DComponent -->
_Inspektorda: **Rigidbody 2D**; qo'shish: **+ Komponent → Fizika 2D → Rigidbody 2D**; sahna faylida: `"Rigidbody2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Type` | `BodyType` | `BodyType::Dynamic` |
| `FixedRotation` | `bool` | `false` |
| `GravityScale` | `float` | `1.0f` |
| `LinearDamping` | `float` | `0.0f` |
| `AngularDamping` | `float` | `0.01f` |
| `Bullet` | `bool` | `false` |
| `Layer` | `int` | `0` |
| `Sleeping` | `SleepMode` | `SleepMode::StartAwake` |
| `RuntimeBody` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
<!-- /gen:Rigidbody2DComponent -->

### BoxCollider2DComponent

To'rtburchak collider. `Size` — **yarim** o'lcham (inspektorda **Yarim o'lcham**; transform masshtabiga
ko'paytiriladi: masshtab 1 va `Size` 0.5 — 1x1 kvadrat), `Offset`. `OneWay` — bir tomonlama platforma (pastdan sakrab
o'tiladi, ustiga qo'niladi; `DropThroughPlatforms()` bilan tushib ketiladi).

<!-- gen:BoxCollider2DComponent -->
_Inspektorda: **Box Collider 2D**; qo'shish: **+ Komponent → Fizika 2D → Box Collider 2D**; sahna faylida: `"BoxCollider2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Offset` | `glm::vec2` | `{ 0.0f, 0.0f }` |
| `Size` | `glm::vec2` | `{ 0.5f, 0.5f }` |
| `Density` | `float` | `1.0f` |
| `Friction` | `float` | `0.5f` |
| `Restitution` | `float` | `0.0f` |
| `RestitutionThreshold` | `float` | `0.5f` |
| `IsSensor` | `bool` | `false` |
| `MaterialPath` | `std::string` | — |
| `Layer` | `int` | `-1` |
| `OneWay` | `bool` | `false` |
| `RuntimeFixture` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
<!-- /gen:BoxCollider2DComponent -->

### CircleCollider2DComponent

Doira collider: `Radius` (masshtabga ko'paytiriladi), `Offset`. Platformer2D'dagi tangalar: Static jism + sensor doira.

<!-- gen:CircleCollider2DComponent -->
_Inspektorda: **Circle Collider 2D**; qo'shish: **+ Komponent → Fizika 2D → Circle Collider 2D**; sahna faylida: `"CircleCollider2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Offset` | `glm::vec2` | `{ 0.0f, 0.0f }` |
| `Radius` | `float` | `0.5f` |
| `Density` | `float` | `1.0f` |
| `Friction` | `float` | `0.5f` |
| `Restitution` | `float` | `0.0f` |
| `RestitutionThreshold` | `float` | `0.5f` |
| `IsSensor` | `bool` | `false` |
| `MaterialPath` | `std::string` | — |
| `Layer` | `int` | `-1` |
| `RuntimeFixture` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
<!-- /gen:CircleCollider2DComponent -->

### PolygonCollider2DComponent

Istalgan ko'pburchak (`Points`, obyekt koordinatalarida). Botiq yoki uzun shakl avtomatik ravishda 8 burchakli
qavariq bo'laklarga ajratiladi; o'zini kesadigan kontur qavariq qobiq bo'lib ishlaydi. Nuqtalar viewport'da
tahrirlanadi; sprite'dan kontur olish ham mumkin.

<!-- gen:PolygonCollider2DComponent -->
_Inspektorda: **Polygon Collider 2D**; qo'shish: **+ Komponent → Fizika 2D → Polygon Collider 2D (ko'pburchak)**; sahna faylida: `"PolygonCollider2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Offset` | `glm::vec2` | `{ 0.0f, 0.0f }` |
| `Points` | `std::vector<glm::vec2>` | `{ { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.2f }, { 0.0f, 0.5f }, { -0.5f, 0.2f } }` |
| `Density` | `float` | `1.0f` |
| `Friction` | `float` | `0.5f` |
| `Restitution` | `float` | `0.0f` |
| `RestitutionThreshold` | `float` | `0.5f` |
| `IsSensor` | `bool` | `false` |
| `MaterialPath` | `std::string` | — |
| `Layer` | `int` | `-1` |
| `RuntimeFixture` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
| `RuntimePieces` _(faqat o'yin paytida)_ | `int` | `0` |
<!-- /gen:PolygonCollider2DComponent -->

### EdgeCollider2DComponent

Nuqtalar orqali o'tadigan chiziq (ochiq zanjir) yoki yopiq halqa (`Loop`): yer, qiyaliklar, devorlar. Yuzasi yo'q
(massasi yo'q). `OneSided` — faqat bir tomondan to'xtatadigan silliq zanjir (burchaklarda ilinmaydi).

<!-- gen:EdgeCollider2DComponent -->
_Inspektorda: **Edge Collider 2D**; qo'shish: **+ Komponent → Fizika 2D → Edge Collider 2D (chiziq/zanjir)**; sahna faylida: `"EdgeCollider2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Offset` | `glm::vec2` | `{ 0.0f, 0.0f }` |
| `Points` | `std::vector<glm::vec2>` | `{ { -1.0f, 0.0f }, { 1.0f, 0.0f } }` |
| `Loop` | `bool` | `false` |
| `OneSided` | `bool` | `false` |
| `Density` | `float` | `1.0f` |
| `Friction` | `float` | `0.5f` |
| `Restitution` | `float` | `0.0f` |
| `RestitutionThreshold` | `float` | `0.5f` |
| `IsSensor` | `bool` | `false` |
| `MaterialPath` | `std::string` | — |
| `Layer` | `int` | `-1` |
| `RuntimeFixture` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
<!-- /gen:EdgeCollider2DComponent -->

### PlatformEffector2DComponent

Obyekt collider'larini (yoki tilemap plitkalarini) platformaga aylantiradi: `OneWay` — pastdan / yondan o'tiladi,
ustiga qo'niladi (`SurfaceArc` gradus ichida to'qnashadi; 180 — klassik bir tomonlama platforma), `RotationOffset`,
yon tomondagi ishqalanish / sakrash (`UseSideFriction`, `UseSideBounce`, `SideArc`). Namuna:
`Projects/Platformer2D/Assets/Scenes/Effectors2D.zscene`.

<!-- gen:PlatformEffector2DComponent -->
_Inspektorda: **Platform Effector 2D**; qo'shish: **+ Komponent → Fizika 2D → Platform Effector 2D (bir tomonlama)**; sahna faylida: `"PlatformEffector2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `OneWay` | `bool` | `true` |
| `SurfaceArc` | `float` | `180.0f` |
| `RotationOffset` | `float` | `0.0f` |
| `UseSideFriction` | `bool` | `false` |
| `UseSideBounce` | `bool` | `false` |
| `SideArc` | `float` | `1.0f` |
<!-- /gen:PlatformEffector2DComponent -->

### AreaEffector2DComponent

Collider'lari ichidagi dinamik jismlarni itaradi (shamol, havo oqimi, suv oqimi): har qadamda `ForceMagnitude` kuch
`ForceAngle` yo'nalishida (`UseGlobalAngle` — dunyo burchagi), `ForceVariation` — tasodifiy qo'shimcha, `Drag` /
`AngularDrag` — sekinlatish, `ColliderMask` — qaysi qatlamlarga ta'sir qiladi. Odatda Static jismdagi trigger collider
bilan.

<!-- gen:AreaEffector2DComponent -->
_Inspektorda: **Area Effector 2D**; qo'shish: **+ Komponent → Fizika 2D → Area Effector 2D (shamol)**; sahna faylida: `"AreaEffector2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ForceAngle` | `float` | `90.0f` |
| `UseGlobalAngle` | `bool` | `false` |
| `ForceMagnitude` | `float` | `10.0f` |
| `ForceVariation` | `float` | `0.0f` |
| `ScaleWithMass` | `bool` | `false` |
| `Drag` | `float` | `0.0f` |
| `AngularDrag` | `float` | `0.0f` |
| `ColliderMask` | `uint16_t` | `0xFFFF` |
| `AffectedBodies` _(faqat o'yin paytida)_ | `int` | `0` |
<!-- /gen:AreaEffector2DComponent -->

### BuoyancyEffector2DComponent

Suyuqlik (suv): collider ichida `SurfaceLevel` gacha. Ichidagi jismni siqib chiqargan suyuqlik og'irligicha yuqoriga
itaradi (`Density` dan yengil collider suzadi), `LinearDrag` / `AngularDrag` bilan sekinlatadi, `FlowAngle` /
`FlowMagnitude` oqimi bilan olib ketadi. Collider trigger bo'lishi kerak (inspektor ogohlantiradi).

<!-- gen:BuoyancyEffector2DComponent -->
_Inspektorda: **Buoyancy Effector 2D**; qo'shish: **+ Komponent → Fizika 2D → Buoyancy Effector 2D (suv)**; sahna faylida: `"BuoyancyEffector2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `SurfaceLevel` | `float` | `0.5f` |
| `Density` | `float` | `2.0f` |
| `LinearDrag` | `float` | `1.5f` |
| `AngularDrag` | `float` | `1.0f` |
| `FlowAngle` | `float` | `0.0f` |
| `FlowMagnitude` | `float` | `0.0f` |
| `FlowVariation` | `float` | `0.0f` |
| `ColliderMask` | `uint16_t` | `0xFFFF` |
| `AffectedBodies` _(faqat o'yin paytida)_ | `int` | `0` |
| `SubmergedArea` _(faqat o'yin paytida)_ | `float` | `0.0f` |
<!-- /gen:BuoyancyEffector2DComponent -->

### DistanceJoint2DComponent

Ikki anchor orasidagi masofani saqlaydi: qattiq tayoq, arqon (`MaxDistanceOnly`) yoki prujina (`Frequency`,
`DampingRatio`). Barcha 2D joint'larning umumiy maydonlari: `ConnectedBody` (bo'sh — dunyodagi nuqtaga),
`Anchor` / `ConnectedAnchor`, `AutoConfigureConnectedAnchor`, `CollideConnected`, `BreakForce` / `BreakTorque` (0 —
uzilmaydi; uzilsa skript `OnJointBreak` oladi va `Broken` yoqiladi), o'yin paytidagi `ReactionForce` /
`ReactionTorque`. Namuna: `Projects/Platformer2D/Assets/Scenes/Joints2D.zscene`.

<!-- gen:DistanceJoint2DComponent -->
_Inspektorda: **Distance Joint 2D**; qo'shish: **+ Komponent → Fizika 2D → Distance Joint 2D (masofa)**; sahna faylida: `"DistanceJoint2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ConnectedBody` | `EntityRef` | — |
| `Anchor` | `glm::vec2` | `{ 0.0f }` |
| `ConnectedAnchor` | `glm::vec2` | `{ 0.0f }` |
| `AutoConfigureConnectedAnchor` | `bool` | `true` |
| `CollideConnected` | `bool` | `false` |
| `BreakForce` | `float` | `0.0f` |
| `BreakTorque` | `float` | `0.0f` |
| `RuntimeJoint` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
| `RuntimeConnected` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Broken` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ReactionForce` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `ReactionTorque` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `AutoConfigureDistance` | `bool` | `true` |
| `Distance` | `float` | `1.0f` |
| `MaxDistanceOnly` | `bool` | `false` |
| `Frequency` | `float` | `0.0f` |
| `DampingRatio` | `float` | `0.0f` |
| `CurrentDistance` _(faqat o'yin paytida)_ | `float` | `0.0f` |

_Jadval boshidagi maydonlar `Joint2DComponentBase` dan meros._
<!-- /gen:DistanceJoint2DComponent -->

### RevoluteJoint2DComponent

Sharnir: jismlar umumiy nuqta atrofida aylanadi; chegaralar (`EnableLimit`, `LowerAngle`, `UpperAngle`) va motor
(`EnableMotor`, `MotorSpeed`, `MaxMotorTorque`). O'yin paytida `JointAngle`, `JointSpeed`.

<!-- gen:RevoluteJoint2DComponent -->
_Inspektorda: **Revolute Joint 2D**; qo'shish: **+ Komponent → Fizika 2D → Revolute Joint 2D (sharnir)**; sahna faylida: `"RevoluteJoint2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ConnectedBody` | `EntityRef` | — |
| `Anchor` | `glm::vec2` | `{ 0.0f }` |
| `ConnectedAnchor` | `glm::vec2` | `{ 0.0f }` |
| `AutoConfigureConnectedAnchor` | `bool` | `true` |
| `CollideConnected` | `bool` | `false` |
| `BreakForce` | `float` | `0.0f` |
| `BreakTorque` | `float` | `0.0f` |
| `RuntimeJoint` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
| `RuntimeConnected` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Broken` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ReactionForce` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `ReactionTorque` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `EnableLimit` | `bool` | `false` |
| `LowerAngle` | `float` | `-45.0f` |
| `UpperAngle` | `float` | `45.0f` |
| `EnableMotor` | `bool` | `false` |
| `MotorSpeed` | `float` | `0.0f` |
| `MaxMotorTorque` | `float` | `10.0f` |
| `JointAngle` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `JointSpeed` _(faqat o'yin paytida)_ | `float` | `0.0f` |

_Jadval boshidagi maydonlar `Joint2DComponentBase` dan meros._
<!-- /gen:RevoluteJoint2DComponent -->

### PrismaticJoint2DComponent

Sirpanish: jism ulangan jismga nisbatan faqat `Axis` bo'ylab, aylanmasdan siljiydi; chegara va motor bilan (lift,
porshen).

<!-- gen:PrismaticJoint2DComponent -->
_Inspektorda: **Prismatic Joint 2D**; qo'shish: **+ Komponent → Fizika 2D → Prismatic Joint 2D (sirpanish)**; sahna faylida: `"PrismaticJoint2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ConnectedBody` | `EntityRef` | — |
| `Anchor` | `glm::vec2` | `{ 0.0f }` |
| `ConnectedAnchor` | `glm::vec2` | `{ 0.0f }` |
| `AutoConfigureConnectedAnchor` | `bool` | `true` |
| `CollideConnected` | `bool` | `false` |
| `BreakForce` | `float` | `0.0f` |
| `BreakTorque` | `float` | `0.0f` |
| `RuntimeJoint` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
| `RuntimeConnected` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Broken` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ReactionForce` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `ReactionTorque` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Axis` | `glm::vec2` | `{ 1.0f, 0.0f }` |
| `EnableLimit` | `bool` | `false` |
| `LowerTranslation` | `float` | `-1.0f` |
| `UpperTranslation` | `float` | `1.0f` |
| `EnableMotor` | `bool` | `false` |
| `MotorSpeed` | `float` | `0.0f` |
| `MaxMotorForce` | `float` | `10.0f` |
| `JointTranslation` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `JointSpeed` _(faqat o'yin paytida)_ | `float` | `0.0f` |

_Jadval boshidagi maydonlar `Joint2DComponentBase` dan meros._
<!-- /gen:PrismaticJoint2DComponent -->

### WeldJoint2DComponent

Jismlarni payvandlaydi (qattiq, yoki `Frequency` bilan burchak atrofida prujinali).

<!-- gen:WeldJoint2DComponent -->
_Inspektorda: **Weld Joint 2D**; qo'shish: **+ Komponent → Fizika 2D → Weld Joint 2D (payvand)**; sahna faylida: `"WeldJoint2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ConnectedBody` | `EntityRef` | — |
| `Anchor` | `glm::vec2` | `{ 0.0f }` |
| `ConnectedAnchor` | `glm::vec2` | `{ 0.0f }` |
| `AutoConfigureConnectedAnchor` | `bool` | `true` |
| `CollideConnected` | `bool` | `false` |
| `BreakForce` | `float` | `0.0f` |
| `BreakTorque` | `float` | `0.0f` |
| `RuntimeJoint` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
| `RuntimeConnected` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Broken` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ReactionForce` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `ReactionTorque` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Frequency` | `float` | `0.0f` |
| `DampingRatio` | `float` | `0.0f` |

_Jadval boshidagi maydonlar `Joint2DComponentBase` dan meros._
<!-- /gen:WeldJoint2DComponent -->

### WheelJoint2DComponent

G'ildirak: bu jism (g'ildirak) anchor atrofida erkin aylanadi va ulangan jismda (shassi) `Axis` bo'ylab amortizator
prujinasi bilan turadi; motor g'ildirakni aylantiradi.

<!-- gen:WheelJoint2DComponent -->
_Inspektorda: **Wheel Joint 2D**; qo'shish: **+ Komponent → Fizika 2D → Wheel Joint 2D (g'ildirak)**; sahna faylida: `"WheelJoint2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ConnectedBody` | `EntityRef` | — |
| `Anchor` | `glm::vec2` | `{ 0.0f }` |
| `ConnectedAnchor` | `glm::vec2` | `{ 0.0f }` |
| `AutoConfigureConnectedAnchor` | `bool` | `true` |
| `CollideConnected` | `bool` | `false` |
| `BreakForce` | `float` | `0.0f` |
| `BreakTorque` | `float` | `0.0f` |
| `RuntimeJoint` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
| `RuntimeConnected` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Broken` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ReactionForce` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `ReactionTorque` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Axis` | `glm::vec2` | `{ 0.0f, 1.0f }` |
| `Frequency` | `float` | `4.0f` |
| `DampingRatio` | `float` | `0.7f` |
| `EnableLimit` | `bool` | `false` |
| `LowerTranslation` | `float` | `-0.25f` |
| `UpperTranslation` | `float` | `0.25f` |
| `EnableMotor` | `bool` | `false` |
| `MotorSpeed` | `float` | `0.0f` |
| `MaxMotorTorque` | `float` | `10.0f` |
| `JointTranslation` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `JointSpeed` _(faqat o'yin paytida)_ | `float` | `0.0f` |

_Jadval boshidagi maydonlar `Joint2DComponentBase` dan meros._
<!-- /gen:WheelJoint2DComponent -->

## 5. Fizika 3D

3D fizika — Jolt. Collider faqat `Rigidbody3DComponent` bor obyektda fizik dunyoga qo'shiladi (trigger ham:
masalan Static jism + sensor collider). Collider'larning umumiy maydonlari: `Offset`, `Friction`, `Restitution`,
`IsSensor` (**Trigger (sensor)**), `MaterialPath` (`.zphysmat`). O'lchamlar transform masshtabiga ko'paytiriladi.
Qahramon uchun jism o'rniga `CharacterController3DComponent`.

### Rigidbody3DComponent

3D jism: `Type` — `Static`, `Dynamic`, `Kinematic`; `Mass`, so'nish, `GravityFactor`, `LockRotation` (tik turadi),
`Layer`, `ContinuousCollision` (tez kichik jismlar uchun), `Sleeping`. Skriptda: `GetVelocity3D`, `SetVelocity3D`,
`ApplyImpulse3D`, `ApplyForce3D`, `ApplyTorque3D`, `SetPosition3D` ... Yaratish: **Obyekt → 3D → Fizik kub / Fizik shar /
Fizik yer (statik)**.

<!-- gen:Rigidbody3DComponent -->
_Inspektorda: **Rigidbody 3D**; qo'shish: **+ Komponent → Fizika 3D → Rigidbody 3D**; sahna faylida: `"Rigidbody3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Type` | `BodyType` | `BodyType::Dynamic` |
| `Mass` | `float` | `1.0f` |
| `LinearDamping` | `float` | `0.05f` |
| `AngularDamping` | `float` | `0.05f` |
| `GravityFactor` | `float` | `1.0f` |
| `LockRotation` | `bool` | `false` |
| `Layer` | `int` | `0` |
| `ContinuousCollision` | `bool` | `false` |
| `Sleeping` | `SleepMode` | `SleepMode::StartAwake` |
| `RuntimeBodyID` _(faqat o'yin paytida)_ | `uint32_t` | `0xffffffff` |
<!-- /gen:Rigidbody3DComponent -->

### BoxCollider3DComponent

Quti collider: `HalfExtents` — yarim o'lchamlar (inspektorda **Yarim o'lcham**).

<!-- gen:BoxCollider3DComponent -->
_Inspektorda: **Box Collider 3D**; qo'shish: **+ Komponent → Fizika 3D → Box Collider 3D**; sahna faylida: `"BoxCollider3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `HalfExtents` | `glm::vec3` | `{ 0.5f, 0.5f, 0.5f }` |
| `Offset` | `glm::vec3` | `{ 0.0f, 0.0f, 0.0f }` |
| `Friction` | `float` | `0.5f` |
| `Restitution` | `float` | `0.0f` |
| `IsSensor` | `bool` | `false` |
| `MaterialPath` | `std::string` | — |
<!-- /gen:BoxCollider3DComponent -->

### SphereCollider3DComponent

Shar collider: `Radius`. [3D qo'llanmadagi](Tutorial-3D.md) kristallar: Static jism + sensor shar.

<!-- gen:SphereCollider3DComponent -->
_Inspektorda: **Sphere Collider 3D**; qo'shish: **+ Komponent → Fizika 3D → Sphere Collider 3D**; sahna faylida: `"SphereCollider3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Radius` | `float` | `0.5f` |
| `Offset` | `glm::vec3` | `{ 0.0f, 0.0f, 0.0f }` |
| `Friction` | `float` | `0.5f` |
| `Restitution` | `float` | `0.0f` |
| `IsSensor` | `bool` | `false` |
| `MaterialPath` | `std::string` | — |
<!-- /gen:SphereCollider3DComponent -->

### CapsuleCollider3DComponent

Kapsula (Y o'qi bo'ylab): `Radius`, `HalfHeight` (silindr qismining yarim balandligi).

<!-- gen:CapsuleCollider3DComponent -->
_Inspektorda: **Capsule Collider 3D**; qo'shish: **+ Komponent → Fizika 3D → Capsule Collider 3D**; sahna faylida: `"CapsuleCollider3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Radius` | `float` | `0.5f` |
| `HalfHeight` | `float` | `0.5f` |
| `Offset` | `glm::vec3` | `{ 0.0f, 0.0f, 0.0f }` |
| `Friction` | `float` | `0.5f` |
| `Restitution` | `float` | `0.0f` |
| `IsSensor` | `bool` | `false` |
| `MaterialPath` | `std::string` | — |
<!-- /gen:CapsuleCollider3DComponent -->

### ConvexHullCollider3DComponent

Mesh shaklidagi qavariq collider: obyekt Mesh Renderer'ining (yoki `ModelPath` modelining) qavariq qobig'i,
ko'pi bilan `MaxVertices` burchak. Chuqurlik va teshiklar to'ladi (kosa — yaxlit blok). Dinamik jismlar uchun mos.

<!-- gen:ConvexHullCollider3DComponent -->
_Inspektorda: **Convex Hull Collider 3D**; qo'shish: **+ Komponent → Fizika 3D → Convex Hull Collider 3D (modeldan)**; sahna faylida: `"ConvexHullCollider3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ModelPath` | `std::string` | — |
| `MaxVertices` | `int` | `32` |
| `Offset` | `glm::vec3` | `{ 0.0f, 0.0f, 0.0f }` |
| `Friction` | `float` | `0.5f` |
| `Restitution` | `float` | `0.0f` |
| `IsSensor` | `bool` | `false` |
| `MaterialPath` | `std::string` | — |
<!-- /gen:ConvexHullCollider3DComponent -->

### MeshCollider3DComponent

Meshning o'zi (hamma uchburchaklari) collider: zinapoya, ark, tunnel, notekis yer. Faqat Static va Kinematic jismlar
uchun (dinamik jism uni hisobga olmaydi). Uchburchaklar old tomonidan qattiq.

<!-- gen:MeshCollider3DComponent -->
_Inspektorda: **Mesh Collider 3D**; qo'shish: **+ Komponent → Fizika 3D → Mesh Collider 3D (statik, modeldan)**; sahna faylida: `"MeshCollider3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ModelPath` | `std::string` | — |
| `Offset` | `glm::vec3` | `{ 0.0f, 0.0f, 0.0f }` |
| `Friction` | `float` | `0.5f` |
| `Restitution` | `float` | `0.0f` |
| `IsSensor` | `bool` | `false` |
| `MaterialPath` | `std::string` | — |
<!-- /gen:MeshCollider3DComponent -->

### FixedJoint3DComponent

Jismlarni bir-biriga mahkamlaydi: birga harakatlanadi (uzilguncha). Barcha 3D joint'larning umumiy maydonlari
(`Joint3DComponentBase`): `ConnectedBody`, `Anchor`, `ConnectedAnchor`, `AutoConfigureConnectedAnchor`,
`CollideConnected`, `BreakForce`, `BreakTorque`; o'yin paytida `Broken`, `ReactionForce`, `ReactionTorque`. Uzilganda
skript `OnJointBreak3D` oladi.

<!-- gen:FixedJoint3DComponent -->
_Inspektorda: **Fixed Joint 3D**; qo'shish: **+ Komponent → Fizika 3D → Fixed Joint 3D (mahkam)**; sahna faylida: `"FixedJoint3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ConnectedBody` | `EntityRef` | — |
| `Anchor` | `glm::vec3` | `{ 0.0f }` |
| `ConnectedAnchor` | `glm::vec3` | `{ 0.0f }` |
| `AutoConfigureConnectedAnchor` | `bool` | `true` |
| `CollideConnected` | `bool` | `false` |
| `BreakForce` | `float` | `0.0f` |
| `BreakTorque` | `float` | `0.0f` |
| `RuntimeJoint` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `RuntimeConnected` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Broken` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ReactionForce` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `ReactionTorque` _(faqat o'yin paytida)_ | `float` | `0.0f` |

_Barcha maydonlar `Joint3DComponentBase` dan meros._
<!-- /gen:FixedJoint3DComponent -->

### HingeJoint3DComponent

Sharnir (eshik, g'ildirak, tirsak): jism faqat `Axis` atrofida aylanadi; chegaralar, motor (`MotorSpeed`,
`MaxMotorTorque`) yoki burchakka tortadigan prujina (`EnableSpring`, `SpringTargetAngle`, `SpringFrequency`,
`SpringDamping`). O'yin paytida `JointAngle`, `JointSpeed`.

<!-- gen:HingeJoint3DComponent -->
_Inspektorda: **Hinge Joint 3D**; qo'shish: **+ Komponent → Fizika 3D → Hinge Joint 3D (sharnir)**; sahna faylida: `"HingeJoint3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ConnectedBody` | `EntityRef` | — |
| `Anchor` | `glm::vec3` | `{ 0.0f }` |
| `ConnectedAnchor` | `glm::vec3` | `{ 0.0f }` |
| `AutoConfigureConnectedAnchor` | `bool` | `true` |
| `CollideConnected` | `bool` | `false` |
| `BreakForce` | `float` | `0.0f` |
| `BreakTorque` | `float` | `0.0f` |
| `RuntimeJoint` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `RuntimeConnected` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Broken` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ReactionForce` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `ReactionTorque` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Axis` | `glm::vec3` | `{ 0.0f, 1.0f, 0.0f }` |
| `EnableLimit` | `bool` | `false` |
| `LowerAngle` | `float` | `-45.0f` |
| `UpperAngle` | `float` | `45.0f` |
| `EnableMotor` | `bool` | `false` |
| `MotorSpeed` | `float` | `0.0f` |
| `MaxMotorTorque` | `float` | `10.0f` |
| `EnableSpring` | `bool` | `false` |
| `SpringTargetAngle` | `float` | `0.0f` |
| `SpringFrequency` | `float` | `2.0f` |
| `SpringDamping` | `float` | `0.5f` |
| `JointAngle` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `JointSpeed` _(faqat o'yin paytida)_ | `float` | `0.0f` |

_Jadval boshidagi maydonlar `Joint3DComponentBase` dan meros._
<!-- /gen:HingeJoint3DComponent -->

### DistanceJoint3DComponent

Anchor'lar orasini `MinDistance`..`MaxDistance` da saqlaydi: tayoq (teng), arqon (`MaxDistanceOnly`) yoki oraliq;
`Frequency` — prujinali uchlar.

<!-- gen:DistanceJoint3DComponent -->
_Inspektorda: **Distance Joint 3D**; qo'shish: **+ Komponent → Fizika 3D → Distance Joint 3D (masofa)**; sahna faylida: `"DistanceJoint3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ConnectedBody` | `EntityRef` | — |
| `Anchor` | `glm::vec3` | `{ 0.0f }` |
| `ConnectedAnchor` | `glm::vec3` | `{ 0.0f }` |
| `AutoConfigureConnectedAnchor` | `bool` | `true` |
| `CollideConnected` | `bool` | `false` |
| `BreakForce` | `float` | `0.0f` |
| `BreakTorque` | `float` | `0.0f` |
| `RuntimeJoint` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `RuntimeConnected` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Broken` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ReactionForce` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `ReactionTorque` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `AutoConfigureDistance` | `bool` | `true` |
| `MinDistance` | `float` | `1.0f` |
| `MaxDistance` | `float` | `1.0f` |
| `MaxDistanceOnly` | `bool` | `false` |
| `Frequency` | `float` | `0.0f` |
| `DampingRatio` | `float` | `0.0f` |
| `CurrentDistance` _(faqat o'yin paytida)_ | `float` | `0.0f` |

_Jadval boshidagi maydonlar `Joint3DComponentBase` dan meros._
<!-- /gen:DistanceJoint3DComponent -->

### SliderJoint3DComponent

Sirpanish: jism faqat `Axis` bo'ylab, aylanmasdan siljiydi; chegara va motor bilan. O'yin paytida `JointTranslation`.

<!-- gen:SliderJoint3DComponent -->
_Inspektorda: **Slider Joint 3D**; qo'shish: **+ Komponent → Fizika 3D → Slider Joint 3D (sirpanish)**; sahna faylida: `"SliderJoint3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ConnectedBody` | `EntityRef` | — |
| `Anchor` | `glm::vec3` | `{ 0.0f }` |
| `ConnectedAnchor` | `glm::vec3` | `{ 0.0f }` |
| `AutoConfigureConnectedAnchor` | `bool` | `true` |
| `CollideConnected` | `bool` | `false` |
| `BreakForce` | `float` | `0.0f` |
| `BreakTorque` | `float` | `0.0f` |
| `RuntimeJoint` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `RuntimeConnected` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Broken` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ReactionForce` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `ReactionTorque` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Axis` | `glm::vec3` | `{ 1.0f, 0.0f, 0.0f }` |
| `EnableLimit` | `bool` | `false` |
| `LowerTranslation` | `float` | `-1.0f` |
| `UpperTranslation` | `float` | `1.0f` |
| `EnableMotor` | `bool` | `false` |
| `MotorSpeed` | `float` | `0.0f` |
| `MaxMotorForce` | `float` | `10.0f` |
| `JointTranslation` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `JointSpeed` _(faqat o'yin paytida)_ | `float` | `0.0f` |

_Jadval boshidagi maydonlar `Joint3DComponentBase` dan meros._
<!-- /gen:SliderJoint3DComponent -->

### ConeJoint3DComponent

Shar-sharnir (yelka, son, zanjirdagi chiroq): jism o'qini `SwingLimit` gradusli konus ichida tebrata oladi va
`TwistLower`..`TwistUpper` oralig'ida buraladi; `FrictionTorque` — ragdoll a'zolari silkinib ketmasligi uchun.

<!-- gen:ConeJoint3DComponent -->
_Inspektorda: **Cone Joint 3D**; qo'shish: **+ Komponent → Fizika 3D → Cone Joint 3D (yelka, son: konus)**; sahna faylida: `"ConeJoint3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ConnectedBody` | `EntityRef` | — |
| `Anchor` | `glm::vec3` | `{ 0.0f }` |
| `ConnectedAnchor` | `glm::vec3` | `{ 0.0f }` |
| `AutoConfigureConnectedAnchor` | `bool` | `true` |
| `CollideConnected` | `bool` | `false` |
| `BreakForce` | `float` | `0.0f` |
| `BreakTorque` | `float` | `0.0f` |
| `RuntimeJoint` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `RuntimeConnected` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Broken` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ReactionForce` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `ReactionTorque` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Axis` | `glm::vec3` | `{ 0.0f, 1.0f, 0.0f }` |
| `SwingLimit` | `float` | `45.0f` |
| `TwistLower` | `float` | `-30.0f` |
| `TwistUpper` | `float` | `30.0f` |
| `FrictionTorque` | `float` | `0.0f` |
| `SwingAngle` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `TwistAngle` _(faqat o'yin paytida)_ | `float` | `0.0f` |

_Jadval boshidagi maydonlar `Joint3DComponentBase` dan meros._
<!-- /gen:ConeJoint3DComponent -->

### CharacterController3DComponent

Kapsula qahramon (Jolt CharacterVirtual): qiyalik (`MaxSlope`) va zinapoyalar (`StepHeight`) bo'ylab yuradi, dunyo bilan
to'qnashadi, tortishishni o'zi qo'llaydi. Obyekt pozitsiyasi — **oyoq** nuqtasi. Skript gorizontal tezlikni `Move()`
bilan beradi va `Jump(speed)` chaqiradi; `Grounded`, `Velocity` o'qiladi. Dinamik jismlarni itaradi (`PushBodies`,
`PushForce`), sensor collider'larga kirsa `OnTriggerBegin` keladi. Yaratish: **Obyekt → 3D → Qahramon (Character
Controller)**. Namunalar: [Tutorial-3D.md](Tutorial-3D.md), `Projects/Adventure3D/Source/Player.cpp`.

<!-- gen:CharacterController3DComponent -->
_Inspektorda: **Character Controller 3D**; qo'shish: **+ Komponent → Fizika 3D → Character Controller 3D**; sahna faylida: `"CharacterController3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Radius` | `float` | `0.35f` |
| `Height` | `float` | `1.8f` |
| `MaxSlope` | `float` | `50.0f` |
| `StepHeight` | `float` | `0.35f` |
| `Mass` | `float` | `70.0f` |
| `GravityScale` | `float` | `1.0f` |
| `StickToFloor` | `bool` | `true` |
| `Layer` | `int` | `0` |
| `PushBodies` | `bool` | `true` |
| `PushForce` | `float` | `100.0f` |
| `MoveVelocity` | `glm::vec3` | `{ 0.0f }` |
| `VerticalVelocity` | `float` | `0.0f` |
| `Grounded` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Velocity` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `RuntimeID` _(faqat o'yin paytida)_ | `uint32_t` | `0xffffffff` |
<!-- /gen:CharacterController3DComponent -->

## 6. Navigatsiya va AI

Batafsil va misollar: [AI.md](AI.md).

### NavGrid2DComponent

2D daraja ustidagi yo'l topish grid'i: to'qnashadigan plitkalar (`UseTilemaps`, `OneWayBlocks`) va statik 2D
collider'lar (`UseColliders`, `LayerMask`) ostidagi kataklar yopiq, `AgentRadius` gacha atrofi ham. Maydon — manbalar
chegarasi (`AutoBounds`) yoki `Size` + `Offset`; `CellSize`. O'yin boshida bake qilinadi, manbalar o'zgarsa qayta
(`AutoRebuild`).

<!-- gen:NavGrid2DComponent -->
_Inspektorda: **NavGrid 2D**; qo'shish: **+ Komponent → Navigatsiya va AI → NavGrid 2D (tilemap / collider'lardan yo'l grid'i)**; sahna faylida: `"NavGrid2D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `AutoBounds` | `bool` | `true` |
| `Offset` | `glm::vec2` | `{ 0.0f }` |
| `Size` | `glm::vec2` | `{ 20.0f, 20.0f }` |
| `CellSize` | `float` | `0.5f` |
| `UseTilemaps` | `bool` | `true` |
| `OneWayBlocks` | `bool` | `false` |
| `Tilemap` | `EntityRef` | — |
| `UseColliders` | `bool` | `true` |
| `LayerMask` | `uint16_t` | `0xFFFF` |
| `AgentRadius` | `float` | `0.25f` |
| `Paths` | `NavGridPathSettings` | — |
| `AutoRebuild` | `bool` | `false` |
| `Grid` _(faqat o'yin paytida)_ | `NavGrid` | — |
| `Baked` _(faqat o'yin paytida)_ | `bool` | `false` |
| `SourceHash` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `RebuildTimer` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `BakeCount` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `BakeMilliseconds` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `FromBakeFile` _(faqat o'yin paytida)_ | `bool` | `false` |
| `StartBakePending` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:NavGrid2DComponent -->

### NavGrid3DComponent

3D daraja grid'i (XZ tekisligida kataklar, har birida bitta pol balandligi): har katak markazidan pastga statik 3D
collider'larga nur tashlab yasaladi. `MaxSlope`, `StepHeight`, `AgentRadius`, `AgentHeight`, `LayerMask`. Bitta qavat
(navmesh emas).

<!-- gen:NavGrid3DComponent -->
_Inspektorda: **NavGrid 3D**; qo'shish: **+ Komponent → Navigatsiya va AI → NavGrid 3D (pol bo'yicha yo'l grid'i)**; sahna faylida: `"NavGrid3D"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Offset` | `glm::vec3` | `{ 0.0f }` |
| `Size` | `glm::vec3` | `{ 20.0f, 10.0f, 20.0f }` |
| `CellSize` | `float` | `0.5f` |
| `AgentRadius` | `float` | `0.4f` |
| `AgentHeight` | `float` | `1.8f` |
| `MaxSlope` | `float` | `45.0f` |
| `StepHeight` | `float` | `0.4f` |
| `LayerMask` | `uint16_t` | `0xFFFF` |
| `Paths` | `NavGridPathSettings` | — |
| `Grid` _(faqat o'yin paytida)_ | `NavGrid` | — |
| `Baked` _(faqat o'yin paytida)_ | `bool` | `false` |
| `BakeCount` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `BakeMilliseconds` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `BakeBodies` _(faqat o'yin paytida)_ | `int` | `0` |
| `FromBakeFile` _(faqat o'yin paytida)_ | `bool` | `false` |
| `StartBakePending` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:NavGrid3DComponent -->

### NavAgentComponent

Obyektni NavGrid 2D / 3D dagi yo'l bo'ylab yurgizadi: `SetDestination(point)` yoki `Target` (ortidan yuradigan obyekt),
`Speed`, `Acceleration`, `AngularSpeed`, `StoppingDistance`, yo'lni silliqlash (`SmoothPath`, `SplinePath`),
to'siqlardan qochish (`Avoidance`, his qiluvchi nurlar). Character Controller 3D, Rigidbody 2D / 3D yoki transformni
harakatlantiradi.

<!-- gen:NavAgentComponent -->
_Inspektorda: **Nav Agent**; qo'shish: **+ Komponent → Navigatsiya va AI → Nav Agent (yo'l topib yuradi)**; sahna faylida: `"NavAgent"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Grid` | `EntityRef` | — |
| `Target` | `EntityRef` | — |
| `Speed` | `float` | `3.5f` |
| `Acceleration` | `float` | `12.0f` |
| `AngularSpeed` | `float` | `540.0f` |
| `StoppingDistance` | `float` | `0.1f` |
| `Radius` | `float` | `0.3f` |
| `LookAhead` | `float` | `0.5f` |
| `RepathInterval` | `float` | `0.5f` |
| `BaseOffset` | `float` | `0.0f` |
| `AutoBraking` | `bool` | `true` |
| `SmoothPath` | `bool` | `true` |
| `SplinePath` | `bool` | `false` |
| `UpdateRotation` | `bool` | `true` |
| `Avoidance` | `bool` | `false` |
| `FeelerLength` | `float` | `1.5f` |
| `FeelerAngle` | `float` | `35.0f` |
| `FeelerHeight` | `float` | `0.5f` |
| `AvoidanceWeight` | `float` | `1.0f` |
| `AvoidanceMask` | `uint16_t` | `0xFFFF` |
| `AvoidStatic` | `bool` | `false` |
| `Destination` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `HasDestination` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Arrived` _(faqat o'yin paytida)_ | `bool` | `false` |
| `RepathRequested` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Path` | `std::vector<glm::vec3>` | — |
| `PathIndex` _(faqat o'yin paytida)_ | `int` | `0` |
| `PathStatus` _(faqat o'yin paytida)_ | `NavPathStatus` | `NavPathStatus::None` |
| `Velocity` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `Remaining` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `RepathTimer` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `GridRevision` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `CarveRevision` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `PathRequests` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `BlockedRepaths` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `FeelerOrigin` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `FeelerEnds` _(faqat o'yin paytida)_ | `std::array<glm::vec3, 3>` | `{}` |
| `FeelerHits` _(faqat o'yin paytida)_ | `uint8_t` | `0` |
| `FeelersActive` _(faqat o'yin paytida)_ | `bool` | `false` |
| `AvoidancePush` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `LastDeltaTime` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `AvoidSide` _(faqat o'yin paytida)_ | `int` | `0` |
| `AvoidClearTime` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `PausedTarget` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
<!-- /gen:NavAgentComponent -->

### NavObstacleComponent

Harakatlanuvchi yoki vaqtinchalik to'siq (eshik, itariladigan quti): o'yin paytida o'z shakli (`Shape`: quti yoki
doira, `Center`, `Size`, `Radius`) ostidagi kataklarni grid'dan o'yib oladi; `MoveThreshold` dan ko'p siljisa qayta
o'yadi; `CarveOnlyStationary` — faqat to'xtab turganda.

<!-- gen:NavObstacleComponent -->
_Inspektorda: **Nav Obstacle**; qo'shish: **+ Komponent → Navigatsiya va AI → Nav Obstacle (harakatlanuvchi to'siq)**; sahna faylida: `"NavObstacle"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Shape` | `ShapeType` | `ShapeType::Box` |
| `Center` | `glm::vec3` | `{ 0.0f }` |
| `Size` | `glm::vec3` | `{ 1.0f, 1.0f, 1.0f }` |
| `Radius` | `float` | `0.5f` |
| `Grid` | `EntityRef` | — |
| `MoveThreshold` | `float` | `0.1f` |
| `CarveOnlyStationary` | `bool` | `false` |
| `StationaryTime` | `float` | `0.5f` |
| `Carvings` _(faqat o'yin paytida)_ | `std::vector<Carving>` | — |
| `CarvedPoints` _(faqat o'yin paytida)_ | `std::vector<glm::vec3>` | — |
| `LastPoints` _(faqat o'yin paytida)_ | `std::vector<glm::vec3>` | — |
| `StillTime` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `GridSignature` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `CarveCount` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
<!-- /gen:NavObstacleComponent -->

### SightSensorComponent

AI ko'zi: `ViewDistance` ichida, oldinga qaragan `FieldOfView` konusida (yoki `NearDistance` dan yaqinda) nishonni
ko'radi — nur `BlockingMask` qatlamlaridagi to'siqqa urilmasa. Nishon — `Target` yoki `TargetTag` tegli obyektlar.
`CheckInterval` da tekshiriladi, `MemoryTime` eslab qoladi. Skriptda: `CanSeeTarget()`, `GetSeenTarget()`,
`OnTargetSpotted` / `OnTargetLost`.

<!-- gen:SightSensorComponent -->
_Inspektorda: **Sight Sensor**; qo'shish: **+ Komponent → Navigatsiya va AI → Sight Sensor (ko'rish maydoni)**; sahna faylida: `"SightSensor"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ViewDistance` | `float` | `10.0f` |
| `FieldOfView` | `float` | `110.0f` |
| `NearDistance` | `float` | `1.0f` |
| `EyeHeight` | `float` | `1.6f` |
| `Target` | `EntityRef` | — |
| `TargetTag` | `std::string` | `"Player"` |
| `BlockingMask` | `uint16_t` | `0xFFFF` |
| `CheckInterval` | `float` | `0.1f` |
| `MemoryTime` | `float` | `3.0f` |
| `CanSee` _(faqat o'yin paytida)_ | `bool` | `false` |
| `SeenTarget` _(faqat o'yin paytida)_ | `UUID` | `0` |
| `LastSeenPosition` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `HasSeen` _(faqat o'yin paytida)_ | `bool` | `false` |
| `TimeSinceSeen` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `CheckTimer` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Checks` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `VisibleCount` _(faqat o'yin paytida)_ | `int` | `0` |
<!-- /gen:SightSensorComponent -->

### PatrolPathComponent

AI patrul yo'li: o'z nuqtalari (`Points`), `Route` obyektining bolalari yoki Spline nuqtalari. `Mode`: `Loop`,
`PingPong`, `Random`, `Once`; har nuqtada `WaitTime` kutadi. Nav Agent bilan grid bo'ylab, bo'lmasa `Speed` bilan to'g'ri
yuradi. Skriptda: `PausePatrol()`, `ResumePatrol()`, `OnPatrolPointReached(i)`.

<!-- gen:PatrolPathComponent -->
_Inspektorda: **Patrol Path**; qo'shish: **+ Komponent → Navigatsiya va AI → Patrol Path (nuqtalar bo'ylab patrul)**; sahna faylida: `"PatrolPath"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Source` | `PointSource` | `PointSource::Points` |
| `Points` | `std::vector<Waypoint>` | — |
| `RelativeToStart` | `bool` | `false` |
| `Route` | `EntityRef` | — |
| `SplineSpacing` | `float` | `0.0f` |
| `Mode` | `PatrolMode` | `PatrolMode::Loop` |
| `WaitTime` | `float` | `1.0f` |
| `WaitVariance` | `float` | `0.0f` |
| `ArriveDistance` | `float` | `0.25f` |
| `Speed` | `float` | `2.0f` |
| `FaceMovement` | `bool` | `true` |
| `StartAtNearest` | `bool` | `true` |
| `PlayOnStart` | `bool` | `true` |
| `Current` _(faqat o'yin paytida)_ | `int` | `-1` |
| `Direction` _(faqat o'yin paytida)_ | `int` | `1` |
| `Started` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Patrolling` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Waiting` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Finished` _(faqat o'yin paytida)_ | `bool` | `false` |
| `DestinationIssued` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ResumeNearest` _(faqat o'yin paytida)_ | `bool` | `false` |
| `PatrolStateSet` _(faqat o'yin paytida)_ | `bool` | `false` |
| `WaitTimer` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `StuckTimer` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `BestDistance` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `StartPosition` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `Target` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `IssuedTarget` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `Arrivals` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `Laps` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `Skipped` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `RandomState` _(faqat o'yin paytida)_ | `uint32_t` | `0x9E3779B9u` |
<!-- /gen:PatrolPathComponent -->

### FormationComponent

Birga yuradigan guruh (yetakchida): `Members` (va `MemberTag` tegli obyektlar) yetakchi atrofidagi joylarda turadi —
`Shape`: `Line`, `Column`, `Wedge`, `Circle`; `Spacing`, `WedgeAngle`. Skriptda `SetFormationShape(...)`.

<!-- gen:FormationComponent -->
_Inspektorda: **Formation**; qo'shish: **+ Komponent → Navigatsiya va AI → Formation (guruh shakli: qator, pona, doira)**; sahna faylida: `"Formation"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Shape` | `FormationShape` | `FormationShape::Wedge` |
| `Spacing` | `float` | `1.5f` |
| `WedgeAngle` | `float` | `90.0f` |
| `Members` | `std::vector<EntityRef>` | — |
| `MemberTag` | `std::string` | — |
| `AssignNearest` | `bool` | `true` |
| `FaceHeading` | `bool` | `true` |
| `KeepOnGrid` | `bool` | `true` |
| `UpdateInterval` | `float` | `0.25f` |
| `SlotTolerance` | `float` | `0.35f` |
| `Speed` | `float` | `3.0f` |
| `SlotMembers` _(faqat o'yin paytida)_ | `std::vector<UUID>` | — |
| `Slots` _(faqat o'yin paytida)_ | `std::vector<glm::vec3>` | — |
| `Issued` _(faqat o'yin paytida)_ | `std::vector<glm::vec3>` | — |
| `Heading` _(faqat o'yin paytida)_ | `glm::vec2` | `{ 0.0f }` |
| `AssignedHeading` _(faqat o'yin paytida)_ | `glm::vec2` | `{ 0.0f }` |
| `LastLeaderPosition` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `AssignedShape` _(faqat o'yin paytida)_ | `FormationShape` | `FormationShape::Wedge` |
| `MemberSignature` _(faqat o'yin paytida)_ | `uint64_t` | `0` |
| `Started` _(faqat o'yin paytida)_ | `bool` | `false` |
| `LeaderMoving` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Timer` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `InPlace` _(faqat o'yin paytida)_ | `int` | `0` |
| `Assignments` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
<!-- /gen:FormationComponent -->

## 7. Effektlar va 2D yordamchilar

### TilemapComponent

Tileset teksturasidan plitkalar panjarasi (obyekt pozitsiyasi — pastki chap burchak): `TilesetPath`, `TilesetColumns` x
`TilesetRows`, `Width` x `Height`, `TileSize`, qatlamlar (`Layers`), rang, saralash, `Lit`. `Collision` — to'qnashadigan
plitkalar 2D collider bo'ladi (plitka xususiyatlari: Solid / OneWay). Editorda **Plitka palitrasi** bilan chiziladi.
Yaratish: **Obyekt → 2D → Tilemap**. Namuna: `Projects/Platformer2D/Assets/Scenes/Tilemap2D.zscene`.

<!-- gen:TilemapComponent -->
_Inspektorda: **Tilemap**; qo'shish: **+ Komponent → Effektlar va UI → Tilemap**; sahna faylida: `"Tilemap"`._

| Maydon | Tur | Standart |
|---|---|---|
| `TilesetPath` | `std::string` | — |
| `Tileset` | `Ref<Texture2D>` | — |
| `PixelArt` | `bool` | `true` |
| `TilesetColumns` | `int` | `8` |
| `TilesetRows` | `int` | `4` |
| `Width` | `int` | `32` |
| `Height` | `int` | `12` |
| `TileSize` | `float` | `1.0f` |
| `Layers` | `std::vector<TilemapLayer>` | `std::vector<TilemapLayer>(1)` |
| `Color` | `glm::vec4` | `{ 1.0f }` |
| `SortingLayer` | `uint32_t` | `0` |
| `SortingOrder` | `int` | `-1` |
| `Lit` | `bool` | `false` |
| `Collision` | `bool` | `true` |
| `Friction` | `float` | `0.6f` |
| `Restitution` | `float` | `0.0f` |
| `MaterialPath` | `std::string` | — |
| `CollisionLayer` | `int` | `0` |
| `RuntimeBody` _(faqat o'yin paytida)_ | `void*` | `nullptr` |
<!-- /gen:TilemapComponent -->

### ParallaxComponent

Obyektni (bolalari bilan) o'yin kamerasi bilan birga siljitadi: `Factor` 0 — oddiy obyekt, 1 — ekranga mahkam
(cheksiz uzoq), 0.5 — kameradan ikki baravar sekin (uzoq fon), manfiy — oldingi plan. `RepeatX` / `RepeatY` — sprite'ni
`Spacing` oraliqda takrorlab ekranni to'ldiradi. Transform o'zgarmaydi; editorda **Ko'rinish → Parallax (editor
kamerasi bilan)** oldindan ko'rsatadi.

<!-- gen:ParallaxComponent -->
_Inspektorda: **Parallax**; qo'shish: **+ Komponent → Effektlar va UI → Parallax**; sahna faylida: `"Parallax"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Factor` | `glm::vec2` | `{ 0.5f, 0.5f }` |
| `RepeatX` | `bool` | `false` |
| `RepeatY` | `bool` | `false` |
| `Spacing` | `glm::vec2` | `{ 0.0f }` |
<!-- /gen:ParallaxComponent -->

### Camera2DFollowComponent

Obyektni (odatda kamerani) `Target` ortidan yurgizadi (har kadr fizika va `OnLateUpdate` dan keyin, faqat x/y):
`Offset`, `Damping` (har o'q bo'yicha yetib olish vaqti; 0 — mahkam), `DeadZone` (kamera qimirlamaydigan quti),
`LookAhead` (harakat yo'nalishiga oldinga qarash), `UseBounds` + `BoundsMin` / `BoundsMax` (daraja chegarasi),
`SnapOnStart`. Inspektorda **Nishon** maydoniga obyektni sudrab qo'ying; skriptda `SetCameraTarget(entity)`.
[2D qo'llanmada](Tutorial-2D.md) ishlatiladi.

<!-- gen:Camera2DFollowComponent -->
_Inspektorda: **Camera 2D Follow**; qo'shish: **+ Komponent → Effektlar va UI → Camera 2D Follow**; sahna faylida: `"Camera2DFollow"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Target` | `EntityRef` | — |
| `Offset` | `glm::vec2` | `{ 0.0f, 1.0f }` |
| `Damping` | `glm::vec2` | `{ 0.2f, 0.3f }` |
| `DeadZone` | `glm::vec2` | `{ 1.0f, 1.5f }` |
| `LookAhead` | `glm::vec2` | `{ 1.5f, 0.0f }` |
| `LookAheadDamping` | `float` | `0.6f` |
| `LookAheadMinSpeed` | `float` | `0.5f` |
| `UseBounds` | `bool` | `false` |
| `BoundsMin` | `glm::vec2` | `{ -20.0f, -10.0f }` |
| `BoundsMax` | `glm::vec2` | `{ 20.0f, 10.0f }` |
| `SnapOnStart` | `bool` | `true` |
| `Initialized` _(faqat o'yin paytida)_ | `bool` | `false` |
| `SnapRequested` _(faqat o'yin paytida)_ | `bool` | `false` |
| `LastTarget` _(faqat o'yin paytida)_ | `glm::vec2` | `{ 0.0f }` |
| `LookAheadDirection` _(faqat o'yin paytida)_ | `glm::vec2` | `{ 0.0f }` |
| `LookAheadOffset` _(faqat o'yin paytida)_ | `glm::vec2` | `{ 0.0f }` |
<!-- /gen:Camera2DFollowComponent -->

### SplineComponent

Obyekt koordinatalaridagi nuqtalar orqali yo'l: `Path` — Catmull-Rom (har nuqtadan silliq), Bezier (har nuqtada ikki
tutqich) yoki to'g'ri chiziqlar; ochiq yoki yopiq. Viewport'da tahrirlanadi (inspektorda **Nuqtalarni tahrirlash**).
Skriptlar `Scene::GetSplinePosition` / `GetSplineTangent` bilan o'qiydi. Batafsil: [Animation.md](Animation.md).

<!-- gen:SplineComponent -->
_Inspektorda: **Spline**; qo'shish: **+ Komponent → Effektlar va UI → Spline (yo'l: Catmull-Rom, Bezier)**; sahna faylida: `"Spline"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Path` | `Zamin::Spline` | `DefaultPath()` |
| `Color` | `glm::vec4` | `{ 0.35f, 0.85f, 1.0f, 1.0f }` |
<!-- /gen:SplineComponent -->

### SplineFollowerComponent

Obyektni Spline yo'li (`Path`) bo'ylab `Speed` tezlikda harakatlantiradi: `Mode` — oxirigacha bir marta, aylanib
(`Loop`) yoki borib-qaytib (`PingPong`); `Rotation` — yo'l bo'ylab burilish; `Offset`. Kinematik jism tezlik bilan
suriladi (ustidagini olib yuradi).

<!-- gen:SplineFollowerComponent -->
_Inspektorda: **Spline Follower**; qo'shish: **+ Komponent → Effektlar va UI → Spline Follower (yo'l bo'ylab harakat)**; sahna faylida: `"SplineFollower"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Path` | `EntityRef` | — |
| `Speed` | `float` | `2.0f` |
| `Mode` | `FollowMode` | `FollowMode::Loop` |
| `StartPosition` | `float` | `0.0f` |
| `Rotation` | `RotationMode` | `RotationMode::None` |
| `Offset` | `glm::vec3` | `{ 0.0f }` |
| `PlayOnStart` | `bool` | `true` |
| `Distance` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Direction` _(faqat o'yin paytida)_ | `float` | `1.0f` |
| `Playing` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Finished` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Initialized` _(faqat o'yin paytida)_ | `bool` | `false` |
| `PlayStateSet` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:SplineFollowerComponent -->

### ParticleSystemComponent

Particle emitter: emissiya (`EmissionRate`, `Burst`, `Bursts`), shakl (`Shape`: nuqta, doira, shar, quti, konus,
chiziq, mesh sirti), umr, tezlik, yo'nalish, tortishish, boshlang'ich / oxirgi rang va o'lcham, render (doira yoki
tekstura, additiv, cho'zilgan, billboard, saralash), `Duration`, `Looping`, `StartDelay`, `Prewarm`, `SimulationSpeed`,
`StopActionType` (tugaganda o'chirish / yo'q qilish). Qo'shimcha modullar (rang / o'lcham / tezlik egri chiziqlari,
shovqin, to'qnashuv, sub-emitter, tekstura varag'i, iz) yoqilmaguncha ishlamaydi. Tayyor effektlar: **Obyekt →
Effektlar** (`.zparticle` presetlar). Skriptda: `EmitParticles(n)`, `OnParticlesFinished()`.

<!-- gen:ParticleSystemComponent -->
_Inspektorda: **Particle System**; qo'shish: **+ Komponent → Effektlar va UI → Particle System**; sahna faylida: `"ParticleSystem"`._

| Maydon | Tur | Standart |
|---|---|---|
| `PlayOnStart` | `bool` | `true` |
| `Playing` | `bool` | `true` |
| `Looping` | `bool` | `true` |
| `PlayInEditor` | `bool` | `true` |
| `Duration` | `float` | `5.0f` |
| `StartDelay` | `float` | `0.0f` |
| `Prewarm` | `bool` | `false` |
| `SimulationSpeed` | `float` | `1.0f` |
| `StopActionType` | `StopAction` | `StopAction::None` |
| `EmissionRate` | `float` | `30.0f` |
| `Burst` | `int` | `0` |
| `Bursts` | `std::vector<EmissionBurst>` | — |
| `EmissionEnabled` | `bool` | `true` |
| `MaxParticles` | `int` | `1000` |
| `Shape` | `EmitterShape` | `EmitterShape::Point` |
| `ShapeEnabled` | `bool` | `true` |
| `ShapeRadius` | `float` | `0.5f` |
| `ShapeExtents` | `glm::vec3` | `{ 0.5f }` |
| `ShapeThickness` | `float` | `1.0f` |
| `ConeAngle` | `float` | `25.0f` |
| `ConeLength` | `float` | `1.0f` |
| `ConeFrom` | `ConeEmitFrom` | `ConeEmitFrom::Base` |
| `ConeFlat` | `bool` | `false` |
| `EdgeLength` | `float` | `1.0f` |
| `AlignToShape` | `bool` | `false` |
| `MeshSource` | `MeshShapeSource` | `MeshShapeSource::Primitive` |
| `MeshPrimitive` | `PrimitiveMesh` | `PrimitiveMesh::Sphere` |
| `MeshModelPath` | `std::string` | — |
| `MeshMode` | `MeshEmitMode` | `MeshEmitMode::Triangle` |
| `MeshAlongNormal` | `bool` | `true` |
| `MeshNormalOffset` | `float` | `0.0f` |
| `LifetimeMin` | `float` | `0.8f` |
| `LifetimeMax` | `float` | `1.6f` |
| `SpeedMin` | `float` | `1.0f` |
| `SpeedMax` | `float` | `3.0f` |
| `Direction` | `glm::vec3` | `{ 0.0f, 1.0f, 0.0f }` |
| `Spread` | `float` | `25.0f` |
| `Gravity` | `glm::vec3` | `{ 0.0f, 0.0f, 0.0f }` |
| `Drag` | `float` | `0.0f` |
| `AngularVelocityMax` | `float` | `0.0f` |
| `StartColor` | `glm::vec4` | `{ 1.0f, 0.85f, 0.35f, 1.0f }` |
| `EndColor` | `glm::vec4` | `{ 1.0f, 0.25f, 0.05f, 0.0f }` |
| `StartSize` | `float` | `0.35f` |
| `EndSize` | `float` | `0.05f` |
| `RendererEnabled` | `bool` | `true` |
| `RenderAsCircle` | `bool` | `true` |
| `Additive` | `bool` | `true` |
| `Stretched` | `bool` | `false` |
| `StretchLength` | `float` | `1.0f` |
| `StretchSpeed` | `float` | `0.05f` |
| `Space` | `SimulationSpace` | `SimulationSpace::World` |
| `CustomSpace` | `EntityRef` | — |
| `Billboard` | `bool` | `true` |
| `Sorting` | `SortMode` | `SortMode::Auto` |
| `TexturePath` | `std::string` | — |
| `Texture` | `Ref<Texture2D>` | — |
| `SortingLayer` | `uint32_t` | `0` |
| `SortingOrder` | `int` | `0` |
| `Particles` _(faqat o'yin paytida)_ | `std::vector<Particle>` | — |
| `EmitAccumulator` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `BurstDone` _(faqat o'yin paytida)_ | `bool` | `false` |
| `RandomState` _(faqat o'yin paytida)_ | `uint32_t` | `0x9E3779B9u` |
| `Time` | `float` | `0.0f` |
| `PlayTime` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Prewarmed` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Finished` _(faqat o'yin paytida)_ | `bool` | `false` |
| `FinishPending` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Paused` _(faqat o'yin paytida)_ | `bool` | `false` |
| `FollowActive` _(faqat o'yin paytida)_ | `bool` | `false` |
| `FollowPosition` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `Events` | `std::vector<ParticleEvent>` | — |
| `TrailPool` _(faqat o'yin paytida)_ | `ParticleTrailPool` | — |
| `MeshRuntime` _(faqat o'yin paytida)_ | `Ref<const ParticleEmitterMesh>` | — |
| `MeshRuntimeKey` _(faqat o'yin paytida)_ | `std::string` | — |
<!-- /gen:ParticleSystemComponent -->

## 8. UI (ekran interfeysi)

UI Transform bor obyektlar dunyoda emas, ekranda sahna ustida chiziladi (Sprite, Circle, Text va UI vidjetlari).
Tayyor elementlar: **Obyekt → UI (ekran)** (Matn, Rasm, Tugma, Slayder, Belgilash, Progress bar, Matn maydoni, Canvas,
Menyu, Scroll view). Skript hodisalari va animatsiyalari: [Scripting.md](Scripting.md#15-ui).

### UITransformComponent

Ekran to'rtburchagi (RectTransform modeli), otasining UI to'rtburchagida (bo'lmasa butun ekranda) joylashadi:
`AnchorMin` / `AnchorMax` (0..1, teng — nuqtaga mahkam; har xil — ota bilan cho'ziladi; inspektordagi **Langar
(anchor)** presetlari), `Pivot`, `Position` (langardan pivotgacha, UI birliklarida, yuqoriga +), `Size`
(inspektorda **X | Kenglik**, **Y | Balandlik**), `FontSize` (**Shrift o'lchami**), `Order` (aka-ukalari orasida chizilish
tartibi), ko'rinish uchun `Scale`, `VisualOffset`, `Alpha` (joylashuvga ta'sir qilmaydi — UI tween'lari shularni
o'zgartiradi).

<!-- gen:UITransformComponent -->
_Inspektorda: **UI Transform**; qo'shish: **+ Komponent → Effektlar va UI → UI Transform**; sahna faylida: `"UITransform"`._

| Maydon | Tur | Standart |
|---|---|---|
| `AnchorMin` | `glm::vec2` | `{ 0.0f, 1.0f }` |
| `AnchorMax` | `glm::vec2` | `{ 0.0f, 1.0f }` |
| `Pivot` | `glm::vec2` | `{ 0.5f, 0.5f }` |
| `Position` | `glm::vec2` | `{ 100.0f, -60.0f }` |
| `Size` | `glm::vec2` | `{ 200.0f, 60.0f }` |
| `FontSize` | `float` | `0.0f` |
| `Order` | `int` | `0` |
| `Scale` | `glm::vec2` | `{ 1.0f }` |
| `VisualOffset` | `glm::vec2` | `{ 0.0f }` |
| `Alpha` | `float` | `1.0f` |
<!-- /gen:UITransformComponent -->

### UICanvasComponent

Ostidagi UI'ni ekranga moslab masshtablaydi: `ScaleMode` — `ScaleWithScreenSize` (`ReferenceResolution` uchun
yasalgan, ekran bilan kattalashadi / kichrayadi; `MatchMode`, `MatchWidthOrHeight`) yoki `ConstantPixelSize`
(`ScaleFactor`). Faqat eng tashqi canvas hisobga olinadi.

<!-- gen:UICanvasComponent -->
_Inspektorda: **UI Canvas**; qo'shish: **+ Komponent → Effektlar va UI → UI Canvas (ekranga moslashuvchi masshtab)**; sahna faylida: `"UICanvas"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ScaleMode` | `UIScaleMode` | `UIScaleMode::ScaleWithScreenSize` |
| `ScaleFactor` | `float` | `1.0f` |
| `ReferenceResolution` | `glm::vec2` | `{ 1920.0f, 1080.0f }` |
| `MatchMode` | `UIScreenMatchMode` | `UIScreenMatchMode::MatchWidthOrHeight` |
| `MatchWidthOrHeight` | `float` | `0.5f` |
| `CurrentScale` _(faqat o'yin paytida)_ | `float` | `1.0f` |
<!-- /gen:UICanvasComponent -->

### UILayoutGroupComponent

UI bolalarini qatorga (`Horizontal`), ustunga (`Vertical`) yoki panjaraga (`Grid`) teradi, ierarxiya tartibida:
`Padding`, `Spacing`, `ChildAlignment`, bolalar o'lchamini boshqarish (`ControlChildWidth/Height`,
`ChildForceExpandWidth/Height`), panjara uchun `CellSize`, `GridSpacing`, `Constraint`. `FitWidth` / `FitHeight` —
guruh o'zi kontentiga moslashadi (elementlar bilan o'sadigan ro'yxat).

<!-- gen:UILayoutGroupComponent -->
_Inspektorda: **UI Layout Group**; qo'shish: **+ Komponent → Effektlar va UI → UI Layout Group (qator, ustun, panjara)**; sahna faylida: `"UILayoutGroup"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Type` | `UILayoutType` | `UILayoutType::Vertical` |
| `Padding` | `glm::vec4` | `{ 0.0f }` |
| `Spacing` | `float` | `8.0f` |
| `ChildAlignment` | `int` | `0` |
| `ReverseArrangement` | `bool` | `false` |
| `ControlChildWidth` | `bool` | `false` |
| `ControlChildHeight` | `bool` | `false` |
| `ChildForceExpandWidth` | `bool` | `false` |
| `ChildForceExpandHeight` | `bool` | `false` |
| `CellSize` | `glm::vec2` | `{ 100.0f, 100.0f }` |
| `GridSpacing` | `glm::vec2` | `{ 8.0f, 8.0f }` |
| `StartCorner` | `int` | `0` |
| `StartAxisVertical` | `bool` | `false` |
| `Constraint` | `UIGridConstraint` | `UIGridConstraint::Flexible` |
| `ConstraintCount` | `int` | `2` |
| `FitWidth` | `bool` | `false` |
| `FitHeight` | `bool` | `false` |

_Barcha maydonlar `UILayoutGroupSettings` dan meros._
<!-- /gen:UILayoutGroupComponent -->

### UILayoutElementComponent

Layout guruhi bolasi nima so'raydi (-1 — berilmagan): `MinSize`, `PreferredSize`, `FlexibleSize` (qolgan joydan
ulushi); `IgnoreLayout` — guruh uni o'z UI Transform'i joyida qoldiradi.

<!-- gen:UILayoutElementComponent -->
_Inspektorda: **UI Layout Element**; qo'shish: **+ Komponent → Effektlar va UI → UI Layout Element (min / afzal o'lcham)**; sahna faylida: `"UILayoutElement"`._

| Maydon | Tur | Standart |
|---|---|---|
| `IgnoreLayout` | `bool` | `false` |
| `MinSize` | `glm::vec2` | `{ -1.0f }` |
| `PreferredSize` | `glm::vec2` | `{ -1.0f }` |
| `FlexibleSize` | `glm::vec2` | `{ -1.0f }` |
<!-- /gen:UILayoutElementComponent -->

### UINavigationComponent

Klaviatura / gamepad bilan bu vidjetdan qayerga o'tiladi: `NavigationMode` — `Automatic` (bosilgan yo'nalishdagi eng
yaqini), `Horizontal`, `Vertical`, `Explicit` (`Up`, `Down`, `Left`, `Right` havolalari), `None`. `SelectOnEnable` —
faollashganda tanlanadi (menyuning birinchi tugmasi); `ShowHighlight`, `HighlightColor`.

<!-- gen:UINavigationComponent -->
_Inspektorda: **UI Navigation**; qo'shish: **+ Komponent → Effektlar va UI → UI Navigation (klaviatura / gamepad)**; sahna faylida: `"UINavigation"`._

| Maydon | Tur | Standart |
|---|---|---|
| `NavigationMode` | `Mode` | `Mode::Automatic` |
| `Up` | `EntityRef` | — |
| `Down` | `EntityRef` | — |
| `Left` | `EntityRef` | — |
| `Right` | `EntityRef` | — |
| `SelectOnEnable` | `bool` | `false` |
| `ShowHighlight` | `bool` | `true` |
| `HighlightColor` | `glm::vec4` | `{ 1.0f, 0.80f, 0.28f, 1.0f }` |
| `WasActive` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:UINavigationComponent -->

### UIAnimationComponent

UI elementining animatsiyalari: `Show` (faollashganda kirib kelish preseti: so'nish, kattalashish, yondan sirpanib),
`Hide` (`HideUI` chaqirilganda, keyin o'chadi), `ShowAtStart`, `Duration`, `ShowDelay`, easing, `SlideDistance`;
tugma / slayder ustida sichqoncha bo'lsa `HoverScale`, bosilganda `PressScale`. Skriptda `ShowUI(panel)` /
`HideUI(panel)`.

<!-- gen:UIAnimationComponent -->
_Inspektorda: **UI Animation**; qo'shish: **+ Komponent → Effektlar va UI → UI Animation (ko'rinish, yashirish, bosilish)**; sahna faylida: `"UIAnimation"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Show` | `UIAnimPreset` | `UIAnimPreset::Pop` |
| `Hide` | `UIAnimPreset` | `UIAnimPreset::Fade` |
| `ShowAtStart` | `bool` | `true` |
| `Duration` | `float` | `0.25f` |
| `ShowDelay` | `float` | `0.0f` |
| `ShowEase` | `Ease` | `Ease::OutBack` |
| `HideEase` | `Ease` | `Ease::InQuad` |
| `SlideDistance` | `float` | `160.0f` |
| `HoverScale` | `float` | `1.0f` |
| `PressScale` | `float` | `1.0f` |
| `FeedbackDuration` | `float` | `0.08f` |
| `WasActive` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Hiding` _(faqat o'yin paytida)_ | `bool` | `false` |
| `RestSaved` _(faqat o'yin paytida)_ | `bool` | `false` |
| `RestLook` _(faqat o'yin paytida)_ | `UIAnimMath::Look` | — |
| `FeedbackScale` _(faqat o'yin paytida)_ | `float` | `1.0f` |
| `FeedbackFrom` _(faqat o'yin paytida)_ | `float` | `1.0f` |
| `FeedbackTo` _(faqat o'yin paytida)_ | `float` | `1.0f` |
| `FeedbackTime` _(faqat o'yin paytida)_ | `float` | `1.0f` |
<!-- /gen:UIAnimationComponent -->

### UIButtonComponent

Bosiladigan to'rtburchak: holat ranglari (`NormalColor`, `HoverColor`, `PressedColor`), `Interactable`. Bosilganda
obyekt skripti `OnClick()` oladi, hodisalar shinasiga `"UIClick"` chiqadi va `ClickMessage` berilgan bo'lsa shu xabar
`ClickTarget` ga (bo'sh — hammaga) yuboriladi. Tanlangan tugma Enter / gamepad A bilan bosiladi.

<!-- gen:UIButtonComponent -->
_Inspektorda: **UI Button**; qo'shish: **+ Komponent → Effektlar va UI → UI Button**; sahna faylida: `"UIButton"`._

| Maydon | Tur | Standart |
|---|---|---|
| `NormalColor` | `glm::vec4` | `{ 0.16f, 0.62f, 0.50f, 1.0f }` |
| `HoverColor` | `glm::vec4` | `{ 0.22f, 0.74f, 0.60f, 1.0f }` |
| `PressedColor` | `glm::vec4` | `{ 0.10f, 0.45f, 0.36f, 1.0f }` |
| `Interactable` | `bool` | `true` |
| `ClickMessage` | `std::string` | — |
| `ClickTarget` | `EntityRef` | — |
| `Hovered` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Pressed` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Clicked` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:UIButtonComponent -->

### UIImageComponent

UI rasmi: tekstura yoki uning kadri (bo'sh — oddiy rang), `Color`. `Type`: `Simple` (`PreserveAspect`), `Sliced`
(9-slice), `Tiled`, `Filled` (`FillAmount` 0..1, `FillMethod`: gorizontal, vertikal, radial — HP bar, qayta tiklanish
taymeri). `RaycastTarget` — sichqoncha shu yerda to'xtaydi (ostidagi vidjetlar bosilmaydi).

<!-- gen:UIImageComponent -->
_Inspektorda: **UI Image**; qo'shish: **+ Komponent → Effektlar va UI → UI Image (rasm, 9-slice, to'ldirish)**; sahna faylida: `"UIImage"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Color` | `glm::vec4` | `{ 1.0f }` |
| `Texture` | `Ref<Texture2D>` | — |
| `TexturePath` | `std::string` | — |
| `SpriteName` | `std::string` | — |
| `Type` | `UIImageType` | `UIImageType::Simple` |
| `PreserveAspect` | `bool` | `false` |
| `SliceBorder` | `glm::vec4` | `{ 0.0f }` |
| `BorderScale` | `float` | `1.0f` |
| `FillMethod` | `UIFillMethod` | `UIFillMethod::Horizontal` |
| `FillOrigin` | `int` | `0` |
| `FillAmount` | `float` | `1.0f` |
| `FillClockwise` | `bool` | `true` |
| `PixelArt` | `bool` | `false` |
| `RaycastTarget` | `bool` | `true` |
<!-- /gen:UIImageComponent -->

### UISliderComponent

Sudrab tanlanadigan qiymat: `MinValue`..`MaxValue`, `Value`, `WholeNumbers`, `Direction`, ko'rinish (trek, to'ldirish,
tutqich ranglari va o'lchamlari). O'yinchi o'zgartirsa skript `OnValueChanged(value)` oladi va `"UISliderChanged"`
chiqadi; koddan: `Scene::SetSliderValue`.

<!-- gen:UISliderComponent -->
_Inspektorda: **UI Slider**; qo'shish: **+ Komponent → Effektlar va UI → UI Slider**; sahna faylida: `"UISlider"`._

| Maydon | Tur | Standart |
|---|---|---|
| `MinValue` | `float` | `0.0f` |
| `MaxValue` | `float` | `1.0f` |
| `Value` | `float` | `0.5f` |
| `WholeNumbers` | `bool` | `false` |
| `Direction` | `UIDirection` | `UIDirection::LeftToRight` |
| `Interactable` | `bool` | `true` |
| `TrackThickness` | `float` | `8.0f` |
| `HandleSize` | `float` | `0.0f` |
| `Rounded` | `bool` | `true` |
| `TrackColor` | `glm::vec4` | `{ 0.12f, 0.13f, 0.16f, 1.0f }` |
| `FillColor` | `glm::vec4` | `{ 0.16f, 0.62f, 0.50f, 1.0f }` |
| `HandleColor` | `glm::vec4` | `{ 0.92f, 0.94f, 0.96f, 1.0f }` |
| `Hovered` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Dragging` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ValueChanged` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:UISliderComponent -->

### UIToggleComponent

Yoqish / o'chirish: belgilash katakchasi yoki surma kalit (`Style`), yorlig'i — obyektning Text komponenti. Bir
`Group` dagilar radio tugmalar (bittasi yoqiq; `AllowSwitchOff`). O'zgarsa `OnToggleChanged(isOn)`,
`"UIToggleChanged"`; koddan `Scene::SetToggle`.

<!-- gen:UIToggleComponent -->
_Inspektorda: **UI Toggle**; qo'shish: **+ Komponent → Effektlar va UI → UI Toggle (belgilash)**; sahna faylida: `"UIToggle"`._

| Maydon | Tur | Standart |
|---|---|---|
| `IsOn` | `bool` | `false` |
| `Interactable` | `bool` | `true` |
| `Style` | `UIToggleStyle` | `UIToggleStyle::Checkbox` |
| `Mark` | `UIToggleMark` | `UIToggleMark::Check` |
| `BoxSize` | `float` | `0.0f` |
| `LabelSpacing` | `float` | `10.0f` |
| `Group` | `std::string` | — |
| `AllowSwitchOff` | `bool` | `false` |
| `BoxColor` | `glm::vec4` | `{ 0.12f, 0.13f, 0.16f, 1.0f }` |
| `OnColor` | `glm::vec4` | `{ 0.16f, 0.62f, 0.50f, 1.0f }` |
| `MarkColor` | `glm::vec4` | `{ 1.0f }` |
| `Hovered` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Pressed` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ValueChanged` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Transition` | `float` | `0.0f` |
<!-- /gen:UIToggleComponent -->

### UIProgressBarComponent

`Value` (0..1) gacha to'ldirilgan chiziq, `Direction` bo'yicha: fon va to'ldirish ranglari, `UseLowColor` + `LowColor`
(bo'shaganda qizarib boradigan HP bar), `SmoothSpeed` (silliq yetib borish), `ShowPercentage`.

<!-- gen:UIProgressBarComponent -->
_Inspektorda: **UI Progress Bar**; qo'shish: **+ Komponent → Effektlar va UI → UI Progress Bar**; sahna faylida: `"UIProgressBar"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Value` | `float` | `0.5f` |
| `Direction` | `UIDirection` | `UIDirection::LeftToRight` |
| `BackgroundColor` | `glm::vec4` | `{ 0.12f, 0.13f, 0.16f, 1.0f }` |
| `FillColor` | `glm::vec4` | `{ 0.16f, 0.62f, 0.50f, 1.0f }` |
| `UseLowColor` | `bool` | `false` |
| `LowColor` | `glm::vec4` | `{ 0.86f, 0.24f, 0.20f, 1.0f }` |
| `Padding` | `float` | `3.0f` |
| `SmoothSpeed` | `float` | `0.0f` |
| `ShowPercentage` | `bool` | `false` |
| `TextColor` | `glm::vec4` | `{ 1.0f }` |
| `DisplayValue` | `float` | `0.5f` |
<!-- /gen:UIProgressBarComponent -->

### UIInputFieldComponent

Bir qatorli matn maydoni: kursor, tanlash, `Ctrl+A/C/X/V`, Enter — yuborish, Escape — bekor qilish; UTF-8 (oʻ, gʻ,
kirill). `Placeholder`, `ContentType` (`Integer`, `Decimal`, `Alphanumeric`, `Password`), `CharacterLimit`, `ReadOnly`,
ranglar. Skriptda `OnTextChanged`, `OnTextSubmit`, `OnTextEndEdit`.

<!-- gen:UIInputFieldComponent -->
_Inspektorda: **UI Input Field**; qo'shish: **+ Komponent → Effektlar va UI → UI Input Field (matn maydoni)**; sahna faylida: `"UIInputField"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Text` | `std::string` | — |
| `Placeholder` | `std::string` | `"Matn kiriting..."` |
| `ContentType` | `UIContentType` | `UIContentType::Standard` |
| `CharacterLimit` | `int` | `0` |
| `Interactable` | `bool` | `true` |
| `ReadOnly` | `bool` | `false` |
| `SelectAllOnFocus` | `bool` | `false` |
| `FontPath` | `std::string` | — |
| `FontAsset` | `Ref<Font>` | — |
| `Padding` | `float` | `10.0f` |
| `BackgroundColor` | `glm::vec4` | `{ 0.09f, 0.10f, 0.12f, 1.0f }` |
| `OutlineColor` | `glm::vec4` | `{ 0.28f, 0.30f, 0.35f, 1.0f }` |
| `FocusColor` | `glm::vec4` | `{ 0.16f, 0.62f, 0.50f, 1.0f }` |
| `TextColor` | `glm::vec4` | `{ 0.95f, 0.96f, 0.98f, 1.0f }` |
| `PlaceholderColor` | `glm::vec4` | `{ 0.50f, 0.53f, 0.58f, 1.0f }` |
| `CaretColor` | `glm::vec4` | `{ 1.0f }` |
| `SelectionColor` | `glm::vec4` | `{ 0.22f, 0.46f, 0.86f, 0.55f }` |
| `CaretBlinkRate` | `float` | `0.53f` |
| `Focused` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Hovered` _(faqat o'yin paytida)_ | `bool` | `false` |
| `TextChanged` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Submitted` _(faqat o'yin paytida)_ | `bool` | `false` |
| `Caret` _(faqat o'yin paytida)_ | `size_t` | `0` |
| `Anchor` _(faqat o'yin paytida)_ | `size_t` | `0` |
| `BlinkTime` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Scroll` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `TextBeforeEdit` _(faqat o'yin paytida)_ | `std::string` | — |
| `Selecting` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:UIInputFieldComponent -->

### UIMaskComponent

UI bolalarini o'z to'rtburchagi ichida kesadi (chiziladi va sichqonchaga javob beradi faqat ichkarida), `Padding`
bilan; ichma-ich niqoblar kesishadi. `ShowGraphic` o'chiq — niqobning o'z rasmi chizilmaydi, faqat kesadi.

<!-- gen:UIMaskComponent -->
_Inspektorda: **UI Mask**; qo'shish: **+ Komponent → Effektlar va UI → UI Mask (bolalarini to'rtburchakda kesadi)**; sahna faylida: `"UIMask"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ShowGraphic` | `bool` | `true` |
| `Padding` | `glm::vec4` | `{ 0.0f }` |
<!-- /gen:UIMaskComponent -->

### UIScrollViewComponent

Aylantiriladigan ko'rinish: `Content` (ichidagi ro'yxat yoki xarita) g'ildirak, sudrash, scrollbar'lar va navigatsiya
bilan suriladi. `Horizontal` / `Vertical`, `Movement` (elastik, cheklangan, cheksiz), `Elasticity`, `Inertia`,
`DecelerationRate`, `ScrollSensitivity`, `Scrollbars`. Skriptda `OnScrollChanged(pos)`.

<!-- gen:UIScrollViewComponent -->
_Inspektorda: **UI Scroll View**; qo'shish: **+ Komponent → Effektlar va UI → UI Scroll View (aylantiriladigan ro'yxat)**; sahna faylida: `"UIScrollView"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Content` | `EntityRef` | — |
| `Viewport` | `EntityRef` | — |
| `Horizontal` | `bool` | `false` |
| `Vertical` | `bool` | `true` |
| `Movement` | `UIScrollMovement` | `UIScrollMovement::Elastic` |
| `Elasticity` | `float` | `0.1f` |
| `Inertia` | `bool` | `true` |
| `DecelerationRate` | `float` | `0.135f` |
| `ScrollSensitivity` | `float` | `40.0f` |
| `Draggable` | `bool` | `true` |
| `Scrollbars` | `UIScrollbarVisibility` | `UIScrollbarVisibility::AutoHide` |
| `ScrollbarWidth` | `float` | `10.0f` |
| `ScrollbarInset` | `float` | `3.0f` |
| `TrackColor` | `glm::vec4` | `{ 0.0f, 0.0f, 0.0f, 0.28f }` |
| `HandleColor` | `glm::vec4` | `{ 0.78f, 0.80f, 0.86f, 0.85f }` |
| `NormalizedPosition` _(faqat o'yin paytida)_ | `glm::vec2` | `{ 0.0f, 1.0f }` |
| `Velocity` _(faqat o'yin paytida)_ | `glm::vec2` | `{ 0.0f }` |
| `Dragging` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ScrollChanged` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ShowHorizontalBar` _(faqat o'yin paytida)_ | `bool` | `false` |
| `ShowVerticalBar` _(faqat o'yin paytida)_ | `bool` | `false` |
| `HoveredBar` _(faqat o'yin paytida)_ | `int` | `-1` |
| `DraggedBar` _(faqat o'yin paytida)_ | `int` | `-1` |
| `Revealing` _(faqat o'yin paytida)_ | `bool` | `false` |
| `RevealTarget` _(faqat o'yin paytida)_ | `glm::vec2` | `{ 0.0f }` |
| `PositionKnown` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:UIScrollViewComponent -->

## 9. Audio

### AudioSourceComponent

Obyektning ovozi: `Path` (`.wav`, `.mp3`, `.flac`, `.ogg`, `Assets` ga nisbatan), `Volume`, `Pitch`, `Loop`,
`PlayOnStart`, `Spatial` (3D: tinglovchiga nisbatan ovoz va panorama; `MinDistance`, `MaxDistance`), `Output` (mixer
guruhi: SFX, Music, UI ... — **Oyna → Audio Mixer**), `Stream`, tasodifiy oraliqlar (`VolumeRandom`, `PitchRandom`),
ovozlar limiti (`Priority`, `MaxInstances`, `Steal`), Doppler, `FadeIn` / `FadeOut`, filtr, yo'nalish konusi
(`Cone`). Inspektorda to'lqin shakli va oldindan eshitish. Skriptda `PlayAudio()`, `StopAudio()`, `FadeAudio()` ...
Yaratish: **Obyekt → Ovoz manbai**.

<!-- gen:AudioSourceComponent -->
_Inspektorda: **Audio Source**; qo'shish: **+ Komponent → Boshqa → Audio Source**; sahna faylida: `"AudioSource"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Path` | `std::string` | — |
| `Volume` | `float` | `1.0f` |
| `Pitch` | `float` | `1.0f` |
| `Loop` | `bool` | `false` |
| `PlayOnStart` | `bool` | `true` |
| `Spatial` | `bool` | `false` |
| `MinDistance` | `float` | `1.0f` |
| `MaxDistance` | `float` | `30.0f` |
| `Output` | `std::string` | `"SFX"` |
| `Stream` | `bool` | `false` |
| `VolumeRandom` | `glm::vec2` | `{ 1.0f, 1.0f }` |
| `PitchRandom` | `glm::vec2` | `{ 1.0f, 1.0f }` |
| `Priority` | `int` | `128` |
| `MaxInstances` | `int` | `0` |
| `Steal` | `AudioStealPolicy` | `AudioStealPolicy::Oldest` |
| `DopplerFactor` | `float` | `1.0f` |
| `FadeIn` | `float` | `0.0f` |
| `FadeOut` | `float` | `0.0f` |
| `Filter` | `AudioFilterSettings` | — |
| `Cone` | `AudioConeSettings` | — |
| `RuntimeHandle` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `Velocity` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `LastPosition` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `HasLastPosition` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:AudioSourceComponent -->

### AudioListenerComponent

3D ovozlar qayerdan eshitiladi (asosiy kamera o'rniga; masalan uchinchi shaxs o'yinida qahramon boshi). Bir nechta
bo'lsa, eng katta `Priority` li faol tinglovchi; hech biri bo'lmasa — asosiy kamera.

<!-- gen:AudioListenerComponent -->
_Inspektorda: **Audio Listener**; qo'shish: **+ Komponent → Boshqa → Audio Listener (tinglovchi)**; sahna faylida: `"AudioListener"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Enabled` | `bool` | `true` |
| `Priority` | `int` | `0` |
| `TrackVelocity` | `bool` | `false` |
| `Velocity` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `LastPosition` _(faqat o'yin paytida)_ | `glm::vec3` | `{ 0.0f }` |
| `HasLastPosition` _(faqat o'yin paytida)_ | `bool` | `false` |
<!-- /gen:AudioListenerComponent -->

### AmbientZoneComponent

Hajm ichida hamma joyda eshitiladigan takroriy fon ovozi (o'rmon, g'or, yomg'ir, daryo bo'yi): `Shape` — quti
(`Size`) yoki shar (`Radius`); tinglovchi yaqinlashganda `BlendDistance` masofada asta kuchayadi, `FadeTime` o'zgarishlarni
silliqlaydi. Eshitilganda boshlanadi, so'nganda to'xtaydi.

<!-- gen:AmbientZoneComponent -->
_Inspektorda: **Ambient Zone**; qo'shish: **+ Komponent → Boshqa → Ambient Zone (hajmdagi fon ovozi)**; sahna faylida: `"AmbientZone"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Path` | `std::string` | — |
| `Volume` | `float` | `1.0f` |
| `Pitch` | `float` | `1.0f` |
| `Output` | `std::string` | `"SFX"` |
| `Stream` | `bool` | `false` |
| `Priority` | `int` | `128` |
| `Shape` | `ZoneShape` | `ZoneShape::Box` |
| `Size` | `glm::vec3` | `{ 10.0f, 4.0f, 10.0f }` |
| `Radius` | `float` | `5.0f` |
| `BlendDistance` | `float` | `4.0f` |
| `FadeTime` | `float` | `0.5f` |
| `RuntimeHandle` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
| `Target` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Level` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `Distance` _(faqat o'yin paytida)_ | `float` | `0.0f` |
| `RetryIn` _(faqat o'yin paytida)_ | `float` | `0.0f` |
<!-- /gen:AmbientZoneComponent -->

### FootstepsComponent

Animatsiya event'idan qadam tovushi: har `EventName` event'ida (`Animator`, Animation Player yoki sprite klipi)
`Sounds` dan tasodifiy biri chalinadi (ketma-ket bir xili emas), qahramon tezligiga qarab balandroq / yuqoriroq
(`SpeedParameter` — Animator float parametri, `ReferenceSpeed`, `SpeedPitch`, `SpeedVolume`), `MinInterval` dan yaqin
qadamlar bitta. Namuna: Adventure3D qahramoni (`Audio/Step1..4.wav`).

<!-- gen:FootstepsComponent -->
_Inspektorda: **Footsteps**; qo'shish: **+ Komponent → Boshqa → Footsteps (animatsiya event'idan qadam tovushi)**; sahna faylida: `"Footsteps"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Sounds` | `std::vector<std::string>` | — |
| `EventName` | `std::string` | `"Footstep"` |
| `Volume` | `float` | `0.8f` |
| `VolumeRandom` | `glm::vec2` | `{ 0.85f, 1.0f }` |
| `PitchRandom` | `glm::vec2` | `{ 0.92f, 1.08f }` |
| `SpeedParameter` | `std::string` | `"Speed"` |
| `ReferenceSpeed` | `float` | `3.0f` |
| `SpeedPitch` | `float` | `0.12f` |
| `SpeedVolume` | `float` | `0.35f` |
| `MinSpeed` | `float` | `0.3f` |
| `MinInterval` | `float` | `0.12f` |
| `Spatial` | `bool` | `true` |
| `MinDistance` | `float` | `1.0f` |
| `MaxDistance` | `float` | `25.0f` |
| `Output` | `std::string` | `"SFX"` |
| `LastSound` _(faqat o'yin paytida)_ | `int` | `-1` |
| `LastStepTime` _(faqat o'yin paytida)_ | `double` | `-1.0e9` |
| `RandomState` _(faqat o'yin paytida)_ | `uint32_t` | `0x9E3779B9u` |
| `StepCount` _(faqat o'yin paytida)_ | `uint32_t` | `0` |
<!-- /gen:FootstepsComponent -->

## 10. Skript, sub-sahna va editor ma'lumotlari

### NativeScriptComponent

C++ skript: `ClassName` (`ZM_REGISTER_SCRIPT` dagi nom; inspektorda **Klass**), `Fields` (inspektorda berilgan maydon
qiymatlari, sahnada saqlanadi), `Enabled` (**Faol**). O'yin paytida `Instance` — ishlayotgan skript nusxasi
(`GetScript<T>(entity)` shu orqali). Qo'shish: **+ Komponent → Boshqa → C++ Script**. To'liq qo'llanma:
[Scripting.md](Scripting.md).

<!-- gen:NativeScriptComponent -->
_Inspektorda: **C++ Skript**; qo'shish: **+ Komponent → Boshqa → C++ Script**; sahna faylida: `"NativeScript"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ClassName` | `std::string` | — |
| `Fields` | `std::unordered_map<std::string, ScriptFieldValue>` | — |
| `Enabled` | `bool` | `true` |
| `Instance` _(faqat o'yin paytida)_ | `ScriptableEntity*` | `nullptr` |
<!-- /gen:NativeScriptComponent -->

### BoneAttachmentComponent

Obyektni skinned modelning suyagiga biriktiradi (qo'ldagi qilich, boshdagi shapka): `Target` — modeli bor obyekt
(bo'sh — eng yaqin ota-ona), `Bone` — suyak nomi, `InheritBoneScale`. Suyak obyektning otasi o'rnini egallaydi:
Transform — suyak fazosidagi siljish. Batafsil: [Animation.md](Animation.md).

<!-- gen:BoneAttachmentComponent -->
_Inspektorda: **Bone Attachment**; qo'shish: **+ Komponent → Render → Bone Attachment (suyakka biriktirish)**; sahna faylida: `"BoneAttachment"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Target` | `EntityRef` | — |
| `Bone` | `std::string` | — |
| `InheritBoneScale` | `bool` | `false` |
| `CachedJoint` _(faqat o'yin paytida)_ | `int` | `-1` |
<!-- /gen:BoneAttachmentComponent -->

### SubSceneComponent

Sahnaning alohida faylda saqlanadigan qismi (darajaning xonasi, umumiy yoritish): o'yin paytida `ScenePath` sahnasi
shu obyekt transform'i bilan qo'shimcha (additive) yuklanadi; `LoadOnStart` o'chiq bo'lsa skript `LoadSubScene()`
chaqirguncha kutadi. `ApplyTransform`, `ShowInEditor` (editorda faqat ko'rish uchun chiziladi). Inspektorda
**Sub-sahna**. Batafsil: [Scenes.md](Scenes.md).

<!-- gen:SubSceneComponent -->
_Inspektorda: **Sub-sahna**; qo'shish: **+ Komponent → Boshqa → Sub-sahna (boshqa sahna fayli shu joyda)**; sahna faylida: `"SubScene"`._

| Maydon | Tur | Standart |
|---|---|---|
| `ScenePath` | `std::string` | — |
| `LoadOnStart` | `bool` | `true` |
| `ApplyTransform` | `bool` | `true` |
| `ShowInEditor` | `bool` | `true` |
<!-- /gen:SubSceneComponent -->

### EditorMetadataComponent

Faqat editor uchun: ierarxiyadagi ko'z (`Hidden` — editorda chizilmaydi) va qulf (`Locked` — viewport'da tanlanmaydi),
inspektordagi komponentlar tartibi (`ComponentOrder`), prefab nusxasi ildizi (`Prefab` — prefab fayli yo'li). O'yin
uni e'tiborga olmaydi. Sahna faylida `"Editor"` kaliti ostida (faqat biror narsa berilganda).

<!-- gen:EditorMetadataComponent -->
_inspektor menyusida yo'q (editor / tizim o'zi qo'shadi); sahna faylida: `"Editor"`._

| Maydon | Tur | Standart |
|---|---|---|
| `Hidden` | `bool` | `false` |
| `Locked` | `bool` | `false` |
| `ComponentOrder` | `std::vector<std::string>` | — |
| `Prefab` | `std::string` | — |
<!-- /gen:EditorMetadataComponent -->

### PrefabLinkComponent

Faqat editor uchun: prefab nusxasidagi obyekt prefab faylidagi qaysi obyekt ekani (`PrefabEntity`), nusxadagi
o'zgarishlar (`Overrides`), olib tashlangan komponentlar / obyektlar. Apply / Revert va prefab o'zgarganda sinxronlash
shu ma'lumot bilan ishlaydi; player uni o'qimaydi, eksport olib tashlaydi. Batafsil: [Prefabs.md](Prefabs.md).

<!-- gen:PrefabLinkComponent -->
_inspektor menyusida yo'q (editor / tizim o'zi qo'shadi); sahna faylida: `"Editor"`._

| Maydon | Tur | Standart |
|---|---|---|
| `PrefabEntity` | `uint64_t` | `0` |
| `Overrides` | `std::vector<std::string>` | — |
| `RemovedComponents` | `std::vector<std::string>` | — |
| `PrefabUUID` | `UUID` | `0` |
| `PrefabHash` | `uint64_t` | `0` |
| `RemovedEntities` | `std::vector<uint64_t>` | — |
<!-- /gen:PrefabLinkComponent -->
