# Ichki C++ kod muharriri

Zamin Editor ichida C++ skriptlarni tahrirlash, saqlash, kompilyatsiya qilish va xatodan manba qatoriga o'tish mumkin.
Visual Script va C++ bir loyihada ishlaydi; mavjud tashqi muharrir ham ixtiyoriy **External editor** tugmasida saqlangan.

## Ochish va yozish

**Oyna → Kod muharriri** yoki **Skriptlar → Kod muharriri** oynasini oching. Content Browser'da `.cpp` / `.h`
faylini ikki marta bosish, skript inspektoridagi manba tugmasi, asset inspektori va compiler diagnostikasi ham shu oynaga olib keladi.
Chap tomonda Source fayllari, o'rtada alohida tablar, o'ngda engine API va gameplay snippet'lari mavjud.
**New script** engine'ning mavjud C++ shablonlarini ochadi; yangi fayl darhol muharrirda ochiladi.

- C++ sintaksis ranglari, monospace shrift, qator raqamlari, avtomatik indent, bracket matching va whitespace ko'rinishi.
- Klaviatura/sichqon bilan tanlash, copy/cut/paste, Ctrl+Z / Ctrl+Y undo/redo; multicursor uchun Ctrl+click.
- Ctrl+S — joriy fayl; Ctrl+Shift+S — o'zgargan tablar; **Save all** ham mavjud.
- Ctrl+F / Ctrl+H — qidirish/almashtirish paneli; **Next**, case-sensitive tanlov va undo qilinadigan **Replace all**.
- **Go to line** — qatorga o'tish; compiler xatosini bosish qator/ustunni tanlaydi.
- Ctrl+Space — engine API nomlarini to'ldirish; yon paneldan API nomi yoki tayyor snippet kiritish mumkin.
- F7 / **Build + reload** — avval o'zgargan fayllarni saqlab, keyin mavjud CMake build va hot reload oqimini ishga tushiradi.

API ro'yxati `ScriptableEntity.h` dagi 265 ommaviy funksiya nomi va deklaratsiyasidan yaratilgan.
Bu kompilyator/LSP asosidagi semantic IntelliSense emas: o'z klasslaringizni tahlil qilish, overload resolution,
project-wide rename va live C++ type-check mavjud emas. Turlarni haqiqiy C++ kompilyatori tekshiradi.
Snippetni mos joyga joylashtiring va klass/maydon nomlarini o'zingizga moslang.

## Build va xatolar

Build oynasidagi xato/ogohlantirishni bosganda manba ichki muharrirda ochiladi. Faylning diagnostikalari uning ostidagi panelda ham chiqadi.
Build ketayotganida ikkinchi build boshlanmaydi. Save xatosi yoki diskdagi o'zgarish bilan konflikt bo'lsa build bekor qilinadi;
muharrirdagi matn saqlanib qoladi. Hot reload avvalgi engine lifecycle qoidalaridan foydalanadi va kerak bo'lsa Play'ni to'xtatadi.

Visual-only loyihada birinchi C++ build `Binaries/lib<Project>.so` modul yo'lini loyiha sozlamalariga yozadi;
agar sozlamalar saqlanmasa build boshlanmaydi. Build uchun CMake, C++ kompilyator va engine SDK kerak.
Tashqi o'yin loyihalariga beriladigan SDK include ro'yxati visual graph'ning JSON sarlavhalarini ham o'z ichiga oladi.

## Matnni himoyalash

Fayllar UTF-8 sifatida o'qiladi. BOM, CRLF va oxirgi yangi qator bor/yo'qligi saqlanadi; ichki bufer LF ishlatadi.
Binary/NUL, noto'g'ri UTF-8 va 1 MB dan katta fayl yoki tahrir rad etiladi. Bir vaqtda 16 fayl ochiladi.
Undo har tabda 256 yozuv / 4 MB amal matni bilan chegaralangan (bitta eng yangi katta amal saqlanadi).

Save vaqtinchalik fayl va rename bilan atomik bajariladi. Diskdagi fayl o'qilgan nusxaga solishtiriladi;
tashqarida o'zgarsa avtomatik bosib yozilmaydi. **Reload disk version** yoki alohida tasdiqlanadigan **Overwrite disk** tanlanadi.
Loyihadan tashqaridagi compiler/SDK fayllari read-only ochiladi. Save symlink orqali loyiha tashqarisiga chiqishni ham rad etadi.

O'zgargan matn taxminan 0.75 soniya tinch turgach `.zamin/code-recovery/` ichiga atomik tiklash nusxasi yoziladi.
Loyiha almashishi va editor yopilishida qolgan nusxalar yoziladi. Faylni qayta ochganda **Recover edits** yoki **Discard recovery** ko'rsatiladi.
Recovery eski disk nusxasini ham tutadi: crash'dan keyin tashqarida o'zgargan fayl bilan konflikt aniqlanishi davom etadi.
Save yoki saved holatga undo qilish eskirgan tiklash nusxasini olib tashlaydi.
Tab/oyna yopishda o'zgargan bufer uchun Save / Discard / Cancel tanlovi mavjud. OS majburiy kill qilsa oxirgi yozilgan recovery qoladi.
Diskdan o'chirilgan yoki ko'chirilgan asl faylni tiklash uchun recovery JSON nusxasidagi `Text` qiymati saqlanib qoladi; avtomatik file undelete yo'q.

## Tekshirish va komponentlar

`ZaminTests --filter CodeDocument` UTF-8/BOM/CRLF, konflikt, crash recovery, stale recovery, qidirish/almashtirish,
symlink chegarasi va yozish xatosida draft saqlanishini tekshiradi.

`python3 Tests/code_api_catalog.py --check` yordamchi API katalogining header bilan mosligini tekshiradi.
Yangilash: `python3 Tests/code_api_catalog.py`.

Kod oynasi MIT litsenziyali [ImGuiColorTextEdit](https://github.com/santaclose/ImGuiColorTextEdit) widgetidan foydalanadi;
aniq commit va lokal o'zgarishlar [vendor izohida](../vendor/ImGuiColorTextEdit/ZAMIN-NOTES.md), [litsenziya](../vendor/ImGuiColorTextEdit/LICENSE) saqlangan.
Widget faqat ZaminEditor'ga ulanadi; Player va UI'siz engine yangi kod-muharrir kutubxonasini yuklamaydi.
DejaVu Sans Mono fontining [litsenziyasi](../Resources/fonts/DejaVuSansMono-LICENSE.txt) font bilan birga tarqatiladi.

Haqiqiy oynadagi regressiya sinovi:

```bash
ZAMIN_BUILD_DIR=build-review LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=2 \
  xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' python3 Tests/code_editor_ui_test.py
```

Sinov xdotool va ImageMagick `import` vositasini talab qiladi. Vaqtinchalik loyihada haqiqiy C++ build bajaradi:
Ctrl+S, undo/redo, Ctrl+Space, ataylab compiler xatosi, diagnostika qator/ustuniga o'tish, tuzatish/hot reload va recovery tekshiriladi.
2026-11-10: 9 CodeDocument testi va barcha 691 unit test o'tdi; yuqoridagi GUI oqimi Linux/Mesa muhitida sinovdan o'tdi.
