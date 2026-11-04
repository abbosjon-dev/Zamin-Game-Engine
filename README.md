# 🌍 Zamin Engine

**Zamin** — C++20 da yozilgan 2D/3D o'yin dvigateli (game engine). Editor'i Unity/Unreal uslubida, o'yin mantiqi esa **C++ skriptlarda** yoziladi: editor skriptlarni o'zi kompilyatsiya qiladi va o'yinni to'xtatmasdan qayta yuklaydi (hot reload).

Hozircha **Linux** qo'llab-quvvatlanadi. Windows, macOS, Android va iOS keyingi bosqichlarda qo'shiladi (arxitektura bunga tayyorlangan, pastga qarang).

![Zamin Editor](docs/images/editor.png)

| 2D Platformer (namuna) | 3D Showcase (namuna) |
|---|---|
| ![Platformer](docs/images/platformer2d.png) | ![Showcase](docs/images/showcase3d.png) |

**Adventure3D** — uchinchi shaxs o'yini: animatsiyali robot, character controller, kristallar yig'ish:

![Adventure3D](docs/images/adventure3d.png)

![2D editor](docs/images/editor2d.png)

---

## ✨ Imkoniyatlar

**Editor**
- Docking interfeys: Viewport, Ierarxiya, Inspektor, Kontent brauzer, Konsol, Sahna sozlamalari, Statistika
- **Undo/Redo** (`Ctrl+Z` / `Ctrl+Y`), **Copy/Paste** (`Ctrl+C` / `Ctrl+V`), nusxa olish (`Ctrl+D`)
- **Parent/child ierarxiya**: daraxt ko'rinishi, drag & drop bilan ota-ona belgilash, bola obyekt yaratish
- **Prefab'lar** (`.zprefab`): obyektni prefab qilib saqlash, sahnaga/ierarxiyaga sudrab qo'yish
- 2D sahnalar uchun avtomatik ortografik kamera, 3D collider'larni wireframe ko'rinishda chizish
- Zamonaviy qorong'i tema, Inter shrifti, Font Awesome ikonkalari, o'zbekcha interfeys
- Gizmo'lar (ko'chirish / aylantirish / masshtab, `Ctrl` bilan snapping), sichqoncha bilan obyekt tanlash (picking)
- **Play / Simulate / Pause / Step** — o'yinni editor ichida ishga tushirish (sahna nusxasida, tahrir buzilmaydi)
- Editor kamerasi: Unity kabi uchish (RMB + WASD) va Maya kabi orbit (Alt + sichqoncha)
- 2D/3D rejim, infinite grid, fizik collider'larni ko'rsatish, kamera/yorug'lik ikonkalari
- Kontent brauzerdan drag & drop (tekstura → sprite, `.obj` → model, `.zscene` → sahnani ochish)
- C++ skript maydonlari (`ZM_FIELD`) inspektorda tahrirlanadi, o'yin paytida jonli o'zgartiriladi
- Loyiha launcher'i: yangi loyiha (shablondan), so'nggi loyihalar, namuna o'yinlar
- **Profiler**: har bir tizim (skriptlar, fizika, animatsiya, render, post-processing, UI) uchun CPU vaqti
- **Maximize on Play**: o'yin rejimida viewport butun oynaga yoyiladi
- **O'yinni build qilish**: bitta tugma bilan mustaqil papkaga eksport (`./MeningOyinim.sh`)

**Dvigatel**
- ECS arxitektura (EnTT), UUID, JSON sahna formati (`.zscene`)
- **Renderer** (OpenGL 4.1 core, RHI abstraksiya qatlami orqasida):
  - 2D batch renderer: sprite, tekstura, doira, chiziq, **matn** (UTF-8, lotin/kirill, `oʻ gʻ`)
  - 3D PBR (metallic/roughness), yo'naltirilgan quyosh + 16 ta nuqtali yorug'lik, gradient osmon, atrof-muhit aksi
  - **Cascaded Shadow Maps** (3 kaskad, 4096 atlas, PCF, texel snapping), **tuman** (masofa + balandlik)
  - Primitivlar (kub, shar, tekislik, silindr, konus), `.obj` va `.gltf/.glb` modellar
  - **HDR pipeline**: sahna float bufferga chiziladi, tonemapping (ACES / Reinhard / yo'q), haqiqiy HDR bloom
  - Material xaritalari: **normal map**, metallic/roughness map, emissive map
  - **glTF 2.0** modellar (`.gltf` / `.glb`): bir nechta mesh, PBR materiallar, ichki (embedded) teksturalar
  - **Skeletal animatsiya**: glTF skin'lari, suyaklar, animatsiya kliplari (GPU skinning, soyalar bilan), `Animator` komponenti,
    kliplar orasida **silliq o'tish** (crossfade): `anim.Play("Run", true, 1.0f, 0.2f)`
  - **Post-processing**: bloom, vinyetka, kontrast/to'yinganlik/rang tuslash
  - **Particle tizimi**: olov, uchqun, tutun (additive blending, billboard, editor'da jonli preview)
  - **Sprite animatsiya** (sprite sheet), pixel-art filtr, FlipX/FlipY
- **2D fizika** (Box2D): dinamik/statik/kinematik jismlar, box/circle collider, trigger'lar, to'qnashuv callback'lari
- **3D fizika** (Jolt Physics): Rigidbody3D, Box/Sphere/Capsule collider, trigger'lar, to'qnashuvlar
- **Character Controller 3D** (Jolt CharacterVirtual): qiyaliklar, zinapoyalar, yerni aniqlash, sakrash, harakatlanuvchi platformalarda yurish
- **Raycast** 2D va 3D
- **Tilemap**: tileset'dan plitkali darajalar, editor'da chizish (palitra), avtomatik birlashtirilgan collider'lar
- **3D (spatial) ovoz**: tinglovchi — asosiy kamera, masofaga qarab so'nish
- **Ekran UI** (HUD): ekranga bog'langan matn/rasm/tugmalar, 9 ta langar (anchor), `OnClick()`
- **Gamepad** (Xbox/PlayStation) qo'llab-quvvatlash
- Skriptlardan **sahna almashtirish** (`LoadScene`), prefab'larni `Instantiate` qilish
- **Audio** (miniaudio): wav/mp3/flac/ogg
- **C++ skriptlar**: `ScriptableEntity` + `ZM_REGISTER_SCRIPT`, o'yin moduli `.so` sifatida yuklanadi, hot reload
- Zamin Player — eksport qilingan o'yinlarni ishga tushiruvchi runtime

---

## 🔧 Build qilish (Linux)

### Talablar
- C++20 kompilyator (GCC 13+ yoki Clang 17+), CMake 3.20+, Ninja (ixtiyoriy)
- OpenGL 4.1+ drayveri
- X11/Wayland dev paketlari

Ubuntu / Debian:
```bash
sudo apt install build-essential cmake ninja-build git \
    libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols zenity
```
Fedora:
```bash
sudo dnf install gcc-c++ cmake ninja-build mesa-libGL-devel libX11-devel libXrandr-devel \
    libXinerama-devel libXcursor-devel libXi-devel wayland-devel libxkbcommon-devel zenity
```
Arch:
```bash
sudo pacman -S base-devel cmake ninja mesa libx11 libxrandr libxinerama libxcursor libxi wayland libxkbcommon zenity
```

> `zenity` (yoki `kdialog`) — fayl ochish/saqlash dialoglari uchun.

### Build
```bash
git clone https://github.com/abbosjon-dev/zamin-game-engine.git
cd zamin-game-engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Boshqa barcha kutubxonalar (GLFW, GLM, EnTT, ImGui, ImGuizmo, Box2D, Jolt, nlohmann/json, stb, miniaudio, tinyobjloader) CMake orqali avtomatik yuklab olinadi (versiyalar qat'iy belgilangan).

### Ishga tushirish
```bash
./build/bin/ZaminEditor                          # launcher ochiladi
./build/bin/ZaminEditor Projects/Platformer2D    # namuna 2D o'yin
./build/bin/ZaminEditor Projects/Showcase3D      # namuna 3D sahna
./build/bin/ZaminEditor Projects/Adventure3D     # uchinchi shaxs 3D o'yin
./build/bin/ZaminPlayer Projects/Platformer2D    # o'yinni editor'siz ishga tushirish
ctest --test-dir build                           # unit testlar
```

---

## 🎮 Birinchi o'yin

1. `ZaminEditor` ni oching → **Yangi loyiha** → nom va joy kiriting → **Yaratish**.
   Loyiha shablondan yaratiladi va C++ skriptlari avtomatik kompilyatsiya qilinadi.
2. Ierarxiyada `+` → obyekt yarating (masalan *2D → Fizik quti*).
3. Inspektorda **+ Komponent → C++ Script** → klassni tanlang.
4. **▶ O'ynash** (`Ctrl+P`).
5. **Fayl → O'yinni build qilish** — tayyor o'yin papkasi va `.sh` launcher hosil bo'ladi.

### Loyiha tuzilmasi
```
MeningOyinim/
├── MeningOyinim.zproject     # loyiha sozlamalari
├── CMakeLists.txt            # skript modulini build qilish (editor yaratadi)
├── Source/                   # C++ o'yin skriptlari
│   └── Scripts.cpp
├── Assets/                   # sahnalar, teksturalar, ovozlar, modellar
│   └── Scenes/Main.zscene
└── Binaries/libMeningOyinim.so   # kompilyatsiya qilingan skriptlar (avtomatik)
```

---

## 🧑‍💻 C++ skript yozish

```cpp
#include <Zamin/Zamin.h>
using namespace Zamin;

class Player : public ScriptableEntity
{
public:
    float Speed = 6.0f;          // inspektorda ko'rinadi
    float JumpImpulse = 9.0f;

    void OnCreate() override { ZM_INFO("Salom, {}!", GetName()); }

    void OnUpdate(Timestep ts) override
    {
        glm::vec2 v = GetVelocity();                 // Rigidbody2D
        v.x = 0.0f;
        if (Input::IsKeyPressed(Key::A)) v.x = -Speed;
        if (Input::IsKeyPressed(Key::D)) v.x =  Speed;
        SetVelocity(v);

        if (Input::IsKeyPressed(Key::Space))
            ApplyImpulse({ 0.0f, JumpImpulse });
    }

    void OnCollisionBegin(Entity other) override
    {
        if (other.GetName() == "Coin")
        {
            PlaySound("Audio/coin.wav");
            Destroy(other);                          // kadr oxirida o'chiriladi
        }
    }
};

ZM_REGISTER_SCRIPT(Player, ZM_FIELD(Player, Speed), ZM_FIELD(Player, JumpImpulse))
```

| Callback | Qachon |
|---|---|
| `OnCreate()` | O'yin boshlanganda / obyekt yaratilganda |
| `OnUpdate(ts)` | Har kadrda (fizikadan oldin) |
| `OnLateUpdate(ts)` | Har kadrda (fizikadan keyin — kamera kuzatish uchun qulay) |
| `OnCollisionBegin/End(other)` | 2D to'qnashuv / trigger |
| `OnDestroy()` | Obyekt o'chirilganda yoki o'yin to'xtaganda |
| `OnClick()` | Shu obyektdagi UI tugma bosilganda |

Foydali API:

| Soha | Funksiyalar |
|---|---|
| Obyektlar | `Transform()`, `GetComponent<T>()`, `AddComponent<T>()`, `FindEntity("Nom")`, `Destroy(e)`, `GetScript<T>(entity)` |
| Spawn | `Instantiate(entity, pos)`, `Instantiate("Prefabs/Ball.zprefab", pos)` |
| Ierarxiya | `GetParent()`, `SetParent(e)`, `GetWorldPosition()` |
| 2D fizika | `GetVelocity/SetVelocity/ApplyImpulse/ApplyForce/SetPosition2D`, `Raycast2D(origin, dir, dist)` |
| 3D fizika | `GetVelocity3D/SetVelocity3D/ApplyImpulse3D/ApplyForce3D/ApplyTorque3D/SetPosition3D`, `Raycast3D(...)` |
| Qahramon | `GetComponent<CharacterController3DComponent>()` → `.Move(velocity)`, `.Jump(speed)`, `.Grounded` |
| Animatsiya | `GetComponent<AnimatorComponent>().Play("Walk", loop, speed, fade)` |
| Input | `Input::IsKeyPressed`, `Input::IsMouseButtonPressed`, `Input::IsGamepadButtonPressed`, `Input::GetGamepadAxis`, `GetMousePosition()`, `GetMouseWorldPosition()`, `ScreenToWorld()` |
| Boshqa | `LoadScene("Scenes/Level2.zscene")`, `EmitParticles(n)`, `PlaySound(path)`, `PlaySoundAt(path, pos)`, `GetTime()`, `ZM_INFO/ZM_WARN/ZM_ERROR` (editor konsoliga) |

`ZM_FIELD` turlari: `float`, `int`, `bool`, `glm::vec2`, `glm::vec3`, `glm::vec4` (rang), `std::string`.

Skriptni o'zgartirgach: **Skriptlar → Kompilyatsiya** (`Ctrl+B`) yoki loyihani tashqarida build qiling — editor `.so` o'zgarganini sezib, uni avtomatik qayta yuklaydi.

---

## ⌨️ Editor boshqaruvi

| Tugma | Amal |
|---|---|
| RMB + WASD / Q E | Kamerada uchish (Shift — tez, g'ildirak — tezlik) |
| Alt + LMB / MMB / RMB | Orbit / surish / zoom |
| G'ildirak | Yaqinlashtirish |
| Q / W / E / R | Tanlash / Ko'chirish / Aylantirish / Masshtab |
| F | Tanlangan obyektga fokus |
| Ctrl (gizmo bilan) | Snapping |
| Ctrl+P | O'ynash / To'xtatish |
| Ctrl+S / Ctrl+Shift+S | Saqlash / Boshqacha saqlash |
| Ctrl+O / Ctrl+Shift+O | Sahna / Loyiha ochish |
| Ctrl+N | Yangi sahna |
| Ctrl+D / Delete / F2 | Nusxa / O'chirish / Nomini o'zgartirish |
| Ctrl+Z / Ctrl+Y | Bekor qilish / Qaytarish |
| Ctrl+C / Ctrl+V | Nusxalash / Joylash |
| Ctrl+B | C++ skriptlarni kompilyatsiya qilish |

---

## 🏗️ Arxitektura

```
Zamin/src/Zamin/
├── Core/        Application, Window, Layer, Input, Log, UUID, Timestep
├── Events/      Oyna, klaviatura, sichqoncha hodisalari
├── Renderer/    RHI interfeyslari (Buffer, Shader, Texture, Framebuffer, RendererAPI),
│                Renderer2D, Renderer3D, PostProcess, Font, Mesh, EditorCamera
├── Scene/       Scene (EnTT), Entity, Components, SceneSerializer, SceneCamera
├── Scripting/   ScriptableEntity, ScriptRegistry, ScriptEngine (.so yuklash, hot reload)
├── Physics/     PhysicsWorld3D (Jolt), raycast turlari
├── Audio/       AudioEngine (miniaudio)
├── Project/     Project, shablonlar, build fayllari
├── ImGui/       ImGuiLayer, tema, ikonkalar
└── Platform/
    ├── GLFW/    Oyna va input (Linux, Windows, macOS uchun umumiy)
    ├── OpenGL/  RHI ning OpenGL implementatsiyasi
    └── Linux/   Fayl dialoglari, jarayonlar
Editor/          Zamin Editor (ImGui)
Runtime/         Zamin Player (eksport qilingan o'yinlar uchun)
Projects/        Namuna o'yinlar (Platformer2D, Showcase3D, Adventure3D)
Tests/           Headless unit testlar
```

- `libZamin.so` — dvigatel **shared library**: editor, player va o'yin skript modullari bitta nusxadan foydalanadi.
- Renderer kodi faqat `RendererAPI`/`Shader`/`Texture`/... interfeyslari bilan ishlaydi — yangi backend (Vulkan, Metal, GLES) qo'shish uchun faqat `Platform/<API>/` implementatsiyasi yoziladi.
- Platformaga xos kod `Platform/` ichida izolyatsiya qilingan.

---

## 🗺️ Yo'l xaritasi

- [x] Linux: editor, 2D/3D renderer, 2D fizika, audio, C++ skriptlar + hot reload, eksport
- [x] Undo/Redo, obyektlar ierarxiyasi (parent/child), prefab'lar, copy/paste
- [x] 3D fizika (Jolt), raycast'lar
- [x] Sprite animatsiya, particle tizimi, ekran UI, gamepad
- [x] Post-processing (bloom, vinyetka, rang sozlamalari)
- [x] Tilemap (editor'da chizish), 3D ovoz
- [x] glTF import, HDR pipeline, normal/metal-roughness/emissive map'lar
- [x] Skeletal animatsiya (glTF skins), Animator, CPU profiler
- [x] Animatsiyalar orasida silliq o'tish (crossfade), Character Controller 3D
- [ ] Animatsiya holat mashinasi (vizual), root motion
- [x] Cascaded shadow maps, tuman
- [ ] SSAO, IBL (cubemap)
- [ ] **Windows** (MSVC, `.dll` skript moduli)
- [ ] **macOS** (Metal backend)
- [ ] **Android** (OpenGL ES / Vulkan)
- [ ] **iOS** (Metal)

---

## 📦 Uchinchi tomon kutubxonalari

GLFW (zlib), glad (MIT/Public), GLM (MIT), EnTT (MIT), Dear ImGui (MIT), ImGuizmo (MIT), Box2D (MIT), Jolt Physics (MIT), nlohmann/json (MIT), cgltf (MIT), stb (Public Domain/MIT), miniaudio (Public Domain/MIT-0), tinyobjloader (MIT). Shriftlar: Inter (SIL OFL 1.1), Font Awesome Free (SIL OFL 1.1) — litsenziyalar `Resources/fonts/` ichida.

## Litsenziya

MIT — [LICENSE](LICENSE).
