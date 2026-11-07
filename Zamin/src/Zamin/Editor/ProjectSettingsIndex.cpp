#include "Zamin/Editor/ProjectSettingsIndex.h"

#include "Zamin/ImGui/Icons.h"

#include <algorithm>
#include <unordered_map>

namespace Zamin {

    namespace {

        // Lower-case ASCII, the Uzbek apostrophes as '.
        std::string Fold(std::string_view in)
        {
            std::string out;
            out.reserve(in.size());
            for (size_t i = 0; i < in.size(); i++)
            {
                const unsigned char c = (unsigned char)in[i];
                if (c == 0xCA && i + 1 < in.size() && (unsigned char)in[i + 1] == 0xBB) // U+02BB
                {
                    out += '\'';
                    i += 1;
                    continue;
                }
                if (c == 0xE2 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0x80 && (unsigned char)in[i + 2] == 0x99) // U+2019
                {
                    out += '\'';
                    i += 2;
                    continue;
                }
                out += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
            return out;
        }

        std::vector<ProjectSettingsCategory> MakeCategories()
        {
            return {
                { "General", ICON_FA_SLIDERS, "Umumiy", "Loyiha nomi va papkalari.",
                  { "Loyiha nomi", "Nomi", "Papka", "Loyiha fayli", "Assets papkasi", "Skriptlar papkasi", "Skript moduli", "Boshlang'ich sahna" },
                  { "Project.Name", "Project.AssetDirectory", "Project.SourceDirectory", "Project.ScriptModulePath" } },
                { "Player", ICON_FA_WINDOW_MAXIMIZE, "Player",
                  "O'yin player'da qanday ishga tushadi: kompaniya, mahsulot nomi va versiya (SaveData papkasi, "
                  "Application::GetVersion), oyna (o'lcham, to'liq ekran, VSync, sarlavha, FPS cheklovi, fonda ishlash) va ikonka.",
                  { "Kompaniya", "Mahsulot nomi", "Versiya", "Semantik versiya", "SaveData papkasi", "Oyna o'lchami", "Ruxsat (resolution)",
                    "Oyna rejimi", "To'liq ekran", "Chegarasiz", "O'lchamini o'zgartirish", "VSync", "Oyna sarlavhasi",
                    "Sarlavhada versiya", "FPS cheklovi", "Fonda ishlash", "Ikonka", "O'yin ikonkasi", ".desktop" },
                  { "Player" } },
                { "Quality", ICON_FA_GAUGE_HIGH, "Sifat",
                  "Sifat darajalari: har bir daraja sahnalar so'ragan soya, effekt va render masshtabini cheklaydi. O'yin standart "
                  "darajada boshlanadi (player: --quality yoki o'yinchi tanlagani).",
                  { "Sifat darajasi", "Standart daraja", "Editorda ko'rish", "Soyalar", "Soya atlasi", "Soya filtri", "Kaskadlar",
                    "Soya masofasi", "SSAO", "Bloom", "FXAA", "Avto ekspozitsiya", "Render masshtabi", "Anizotropik filtr",
                    "Tekstura sifati", "Low", "Medium", "High", "Ultra" },
                  { "Quality" } },
                { "Graphics", ICON_FA_MICROCHIP, "Grafika",
                  "Player oynasining OpenGL konteksti: versiya (4.1 / 4.3 / 4.5 / 4.6 core), debug konteksti, oyna buferining MSAA va "
                  "sRGB'si. Oyna yaratilganda qo'llanadi; editor ishlayotgan kontekstni ham ko'rsatadi.",
                  { "OpenGL versiyasi", "Grafik API", "Kontekst", "Debug konteksti", "MSAA", "Multisampling", "sRGB framebuffer",
                    "Joriy kontekst", "Ishlab chiqaruvchi", "Videokarta", "GLSL", "Tekstura o'lchami", "Anizotropiya" },
                  { "Graphics" } },
                { "Time", ICON_FA_CLOCK, "Vaqt",
                  "O'yin vaqti: fizikaning qat'iy qadami va kadrdagi qadamlar (o'zinikini bermagan sahnalar uchun), o'yin boshlanadigan "
                  "vaqt masshtabi (sekinlashtirish) va eng uzun kadr. Skriptlar: Time::SetTimeScale, Time::GetDeltaTime ...",
                  { "Qat'iy qadam", "Fixed timestep", "Qadam chastotasi", "Kadrda ko'pi bilan", "Start tezligi", "Vaqt masshtabi", "Time scale",
                    "Sekinlashtirish", "Slow motion", "Eng uzun kadr", "Max delta", "FixedDeltaTime", "DeltaTime" },
                  { "Time" } },
                { "Physics", ICON_FA_TABLE_CELLS, "Fizika",
                  "Standart gravitatsiya (o'zinikini bermagan sahnalar uchun, 2D va 3D), to'qnashuv qatlamlari (2D va 3D uchun bir xil) "
                  "va qaysi juftliklar to'qnashishi; tayyor presetlar.",
                  { "Gravitatsiya", "Gravitatsiya (2D)", "Gravitatsiya (3D)", "Standart gravitatsiya", "Fizika qatlamlari", "To'qnashuv qatlamlari",
                    "To'qnashuv matritsasi", "Qatlam nomi", "Presetlar", "Platformer 2D", "3D aksiya", "Raycast", "LayerMask", "Collision" },
                  { "Physics", "CollisionLayers2D" } },
                { "Audio", ICON_FA_VOLUME_HIGH, "Audio",
                  "Umumiy (Master) ovoz, mikser guruhlarining boshlang'ich ovozi, ovoz dvigatelining standartlari (oqim, ovozlar soni, "
                  "Doppler) va 3D ovozlar: standart masofalar va masofa modeli (Audio Mixer oynasi bilan bir xil).",
                  { "Ovoz", "Umumiy ovoz", "Master ovozi", "Mikser guruhlari", "Master", "Music", "SFX", "Ovoz balandligi", "Ovozsiz",
                    "Oqim chegarasi", "Bir vaqtda ovozlar", "Doppler", "3D ovoz", "Min masofa", "Max masofa", "Masofa modeli",
                    "Pasayish (rolloff)", "Egri chiziq" },
                  { "Audio" } },
                { "Input", ICON_FA_GAMEPAD, "Input",
                  "Nomli harakatlar, o'qlar va 2D vektorlar: klaviatura, sichqoncha va gamepad bog'lanishlari.",
                  { "Harakatlar", "O'qlar", "Vektorlar", "Bog'lanish", "Tugmani bosing", "Gamepad", "Gamepad o'qi", "Deadzone",
                    "Teskari", "Takroriy bog'lanishlar", "Composite", "Move" },
                  { "InputActions" } },
                { "TagsAndLayers", ICON_FA_TAGS, "Teglar va qatlamlar",
                  "Gameplay teglari va 2D saralash qatlamlari: sahnada va loyihada ishlatilishi, qayta nomlash.",
                  { "Teglar", "Teg", "Saralash qatlamlari", "Y bo'yicha saralash", "Qayta nomlash", "Loyihada ishlatilishi" },
                  { "Tags", "SortingLayers" } },
                { "BuildScenes", ICON_FA_LIST_OL, "Build sahnalar", "O'yin quriladigan sahnalar, ularning build indekslari va boshlang'ich sahna.",
                  { "Build sahnalar", "Build indeksi", "Boshlang'ich sahna", "LoadScene" },
                  { "BuildScenes", "Project.StartScene", "Project.StartSceneUUID" } },
                { "Debug", ICON_FA_BUG, "Debug", "O'yindagi debug chizish (skriptlarning DebugDraw'i va fizika ko'rinishlari).",
                  { "Debug chizish", "Raycastlar", "Raycast vaqti", "Kontakt nuqtalari", "Joint'lar", "Fizika qiymatlari", "Personaj tezligi",
                    "Jismlar tezligi", "Chiziq qalinligi", "Matn o'lchami" },
                  { "DebugDraw" } },
                { "SaveData", ICON_FA_FLOPPY_DISK, "Saqlangan ma'lumotlar",
                  "O'yin SaveData bilan saqlagan qiymatlar (o'yinlar orasida qoladi; loyiha faylida emas).",
                  { "SaveData", "PlayerPrefs", "Kalit", "Qiymat", "Saqlangan qiymatlar" },
                  {} },
            };
        }

    }

    namespace ProjectSettingsIndex {

        const std::vector<ProjectSettingsCategory>& GetCategories()
        {
            static const std::vector<ProjectSettingsCategory> s_Categories = MakeCategories();
            return s_Categories;
        }

        int Find(std::string_view id)
        {
            const std::vector<ProjectSettingsCategory>& categories = GetCategories();
            for (size_t i = 0; i < categories.size(); i++)
                if (Fold(categories[i].Id) == Fold(id))
                    return (int)i;
            return -1;
        }

        bool TextContains(std::string_view text, std::string_view query)
        {
            if (query.empty())
                return true;
            return Fold(text).find(Fold(query)) != std::string::npos;
        }

        bool Matches(const ProjectSettingsCategory& category, std::string_view query, std::vector<std::string>* matches)
        {
            if (matches)
                matches->clear();
            if (query.empty())
                return true;
            bool any = TextContains(category.Title, query);
            for (const std::string& keyword : category.Keywords)
                if (TextContains(keyword, query))
                {
                    any = true;
                    if (matches)
                        matches->push_back(keyword);
                }
            return any;
        }

        std::vector<std::string> DiffSections(const std::vector<std::pair<std::string, std::string>>& a,
                                              const std::vector<std::pair<std::string, std::string>>& b)
        {
            std::unordered_map<std::string, const std::string*> left;
            for (const auto& [key, text] : a)
                left[key] = &text;
            std::vector<std::string> changed;
            std::unordered_map<std::string, bool> seen;
            for (const auto& [key, text] : b)
            {
                seen[key] = true;
                auto it = left.find(key);
                if (it == left.end() || *it->second != text)
                    changed.push_back(key);
            }
            for (const auto& [key, text] : a)
                if (!seen.count(key))
                    changed.push_back(key);
            return changed;
        }

        bool IsChanged(const ProjectSettingsCategory& category, const std::vector<std::string>& changed)
        {
            for (const std::string& section : category.Sections)
                if (std::find(changed.begin(), changed.end(), section) != changed.end())
                    return true;
            return false;
        }

    }

}
