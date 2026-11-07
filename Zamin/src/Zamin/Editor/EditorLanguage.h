#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // The language of the editor's own UI (Tahrirlash > Sozlamalar > Til): its menus, window titles,
    // common buttons and the main panels. Uzbek is the source language; the English texts come from a
    // built-in table keyed by the Uzbek ones (a text without an English entry stays Uzbek).
    enum class EditorLanguage { Uzbek = 0, English, Count };

    namespace EditorText {

        void SetLanguage(EditorLanguage language);
        EditorLanguage GetLanguage();
        // "uz" / "en".
        const char* GetLanguageCode(EditorLanguage language);
        // The language's own name ("O'zbekcha", "English").
        const char* GetLanguageName(EditorLanguage language);
        // "en" / "english" -> English; anything else -> Uzbek.
        EditorLanguage ParseLanguage(std::string_view code);

        // A UI text in the editor's language. `uzbek` is the Uzbek text, which is also the key: the
        // English one when the editor is in English and the table has it, else `uzbek` itself. An icon
        // at the start (Font Awesome), a "##id" / "###id" end and a trailing "..." or ":" stay as they
        // are, so ImGui ids made with "###" do not change with the language. A translation stays valid
        // until the language changes; a text the table does not have comes back as the same pointer.
        const char* Tr(const char* uzbek);
        std::string Tr(std::string_view uzbek);

        // The table has an English text for this Uzbek one (its core: without the icon / id / "...").
        bool HasTranslation(std::string_view uzbek);
        // How many texts the English table has.
        size_t GetTranslationCount();
        // The Uzbek keys of the table (tests: no key twice, no empty translation).
        std::vector<std::string> GetTranslationKeys();
        // The text split as Tr sees it: the icon / spaces before, the text, the "..." / ":" / id after.
        struct Parts
        {
            std::string_view Prefix, Core, Suffix;
        };
        Parts Split(std::string_view text);

        // A text the table cannot key because the same Uzbek words mean different things in
        // different places ("Bekor qilish": Cancel / Undo): the one for the current language.
        inline const char* Choose(const char* uzbek, const char* english)
        {
            return GetLanguage() == EditorLanguage::English ? english : uzbek;
        }
    }

    // Short form for the editor's code: Tr(ICON_FA_GEAR "  Loyiha sozlamalari###ProjectSettings").
    inline const char* Tr(const char* uzbek) { return EditorText::Tr(uzbek); }
    inline std::string Tr(std::string_view uzbek) { return EditorText::Tr(uzbek); }
    inline std::string Tr(const std::string& uzbek) { return EditorText::Tr(std::string_view(uzbek)); }

}
