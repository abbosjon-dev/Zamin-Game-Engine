# 🌍 Zamin Engine

**Zamin** — C++20 da yozilgan 2D/3D o'yin dvigateli (game engine). Editor'i Unity/Unreal uslubida, o'yin mantiqi esa **C++ skriptlarda** yoziladi: editor skriptlarni o'zi kompilyatsiya qiladi va o'yinni to'xtatmasdan qayta yuklaydi (hot reload).

Hozircha **Linux** qo'llab-quvvatlanadi. Windows, macOS, Android va iOS keyingi bosqichlarda qo'shiladi (arxitektura bunga tayyorlangan, pastga qarang).

![Zamin Editor](docs/images/editor.png)

| 2D Platformer (namuna) | 3D Showcase (namuna) |
|---|---|
| ![Platformer](docs/images/platformer2d.png) | ![Showcase](docs/images/showcase3d.png) |

---

## ✨ Imkoniyatlar

**Editor**
- Docking interfeys: Viewport, Ierarxiya, Inspektor, Kontent brauzer, Konsol, Sahna sozlamalari, Statistika
- Zamonaviy qorong'i tema, Inter shrifti, Font Awesome ikonkalari, o'zbekcha interfeys
- Gizmo'lar (ko'chirish / aylantirish / masshtab, `Ctrl` bilan snapping), sichqoncha bilan obyekt tanlash (picking)
- **Play / Simulate / Pause / Step** — o'yinni editor ichida ishga tushirish (sahna nusxasida, tahrir buzilmaydi)
- Editor kamerasi: Unity kabi uchish (RMB + WASD) va Maya kabi orbit (Alt + sichqoncha)
- 2D/3D rejim, infinite grid, fizik collider'larni ko'rsatish, kamera/yorug'lik ikonkalari
- Kontent brauzerdan drag & drop (tekstura → sprite, `.obj` → model, `.zscene` → sahnani ochish)
- C++ skript maydonlari (`ZM_FIELD`) inspektorda tahrirlanadi, o'yin paytida jonli o'zgartiriladi
- Loyiha launcher'i: yangi loyiha (shablondan), so'nggi loyihalar, namuna o'yinlar
- **O'yinni build qilish**: bitta tugma bilan mustaqil papkaga eksport (`./MeningOyinim.sh`)

**Dvigatel**
- ECS arxitektura (EnTT), UUID, JSON sahna formati (`.zscene`)
- **Renderer** (OpenGL 4.1 core, RHI abstraksiya qatlami orqasida):
  - 2D batch renderer: sprite, tekstura, doira, chiziq, **matn** (UTF-8, lotin/kirill, `oʻ gʻ`)
  - 3D PBR (metallic/roughness), yo'naltirilgan quyosh + 16 ta nuqtali yorug'lik, **soyalar** (PCF), gradient osmon, atrof-muhit aksi, ACES tonemapping
  - Primitivlar (kub, shar, tekislik, silindr, konus) va `.obj` modellar
- **2D fizika** (Box2D): dinamik/statik/kinematik jismlar, box/circle collider, trigger'lar, to'qnashuv callback'lari
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
Boshqa barcha kutubxonalar (GLFW, GLM, EnTT, ImGui, ImGuizmo, Box2D, nlohmann/json, stb, miniaudio, tinyobjloader) CMake orqali avtomatik yuklab olinadi (versiyalar qat'iy belgilangan).

### Ishga tushirish
```bash
./build/bin/ZaminEditor                          # launcher ochiladi
./build/bin/ZaminEditor Projects/Platformer2D    # namuna 2D o'yin
./build/bin/ZaminEditor Projects/Showcase3D      # namuna 3D sahna
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

Foydali API: `Transform()`, `GetComponent<T>()`, `AddComponent<T>()`, `FindEntity("Nom")`, `Instantiate(prefab, pos)`, `Destroy(e)`, `GetVelocity/SetVelocity/ApplyImpulse/ApplyForce`, `PlaySound(path)`, `GetTime()`, `GetScript<T>(entity)`, `Input::IsKeyPressed`, `Input::GetMousePosition`, `ZM_INFO/ZM_WARN/ZM_ERROR` (editor konsoliga chiqadi).

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
| Ctrl+B | C++ skriptlarni kompilyatsiya qilish |

---

## 🏗️ Arxitektura

```
Zamin/src/Zamin/
├── Core/        Application, Window, Layer, Input, Log, UUID, Timestep
├── Events/      Oyna, klaviatura, sichqoncha hodisalari
├── Renderer/    RHI interfeyslari (Buffer, Shader, Texture, Framebuffer, RendererAPI),
│                Renderer2D, Renderer3D, Font, Mesh, EditorCamera
├── Scene/       Scene (EnTT), Entity, Components, SceneSerializer, SceneCamera
├── Scripting/   ScriptableEntity, ScriptRegistry, ScriptEngine (.so yuklash, hot reload)
├── Audio/       AudioEngine (miniaudio)
├── Project/     Project, shablonlar, build fayllari
├── ImGui/       ImGuiLayer, tema, ikonkalar
└── Platform/
    ├── GLFW/    Oyna va input (Linux, Windows, macOS uchun umumiy)
    ├── OpenGL/  RHI ning OpenGL implementatsiyasi
    └── Linux/   Fayl dialoglari, jarayonlar
Editor/          Zamin Editor (ImGui)
Runtime/         Zamin Player (eksport qilingan o'yinlar uchun)
Projects/        Namuna o'yinlar (Platformer2D, Showcase3D)
Tests/           Headless unit testlar
```

- `libZamin.so` — dvigatel **shared library**: editor, player va o'yin skript modullari bitta nusxadan foydalanadi.
- Renderer kodi faqat `RendererAPI`/`Shader`/`Texture`/... interfeyslari bilan ishlaydi — yangi backend (Vulkan, Metal, GLES) qo'shish uchun faqat `Platform/<API>/` implementatsiyasi yoziladi.
- Platformaga xos kod `Platform/` ichida izolyatsiya qilingan.

---

## 🗺️ Yo'l xaritasi

- [x] Linux: editor, 2D/3D renderer, 2D fizika, audio, C++ skriptlar + hot reload, eksport
- [ ] Undo/Redo, obyektlar ierarxiyasi (parent/child), prefab'lar
- [ ] 3D fizika (Jolt), skeletal animatsiya, glTF import
- [ ] Sprite animatsiya / sprite sheet editori, tilemap
- [ ] Post-processing (bloom, SSAO), HDR pipeline, IBL
- [ ] **Windows** (MSVC, `.dll` skript moduli)
- [ ] **macOS** (Metal backend)
- [ ] **Android** (OpenGL ES / Vulkan)
- [ ] **iOS** (Metal)

---

## 📦 Uchinchi tomon kutubxonalari

GLFW (zlib), glad (MIT/Public), GLM (MIT), EnTT (MIT), Dear ImGui (MIT), ImGuizmo (MIT), Box2D (MIT), nlohmann/json (MIT), stb (Public Domain/MIT), miniaudio (Public Domain/MIT-0), tinyobjloader (MIT). Shriftlar: Inter (SIL OFL 1.1), Font Awesome Free (SIL OFL 1.1) — litsenziyalar `Resources/fonts/` ichida.

## Litsenziya

MIT — [LICENSE](LICENSE).
