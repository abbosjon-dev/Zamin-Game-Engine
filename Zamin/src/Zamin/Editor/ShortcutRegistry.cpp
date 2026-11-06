#include "Zamin/Editor/ShortcutRegistry.h"
#include "Zamin/Editor/ConsoleFilter.h"
#include "Zamin/Core/InputActions.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <format>

namespace Zamin {

    namespace {
        // Keys shown other than by their file names (punctuation as the character, short words).
        struct KeyLabel { Key Code; const char* Label; };
        constexpr KeyLabel s_KeyLabels[] = {
            { Key::Comma, "," }, { Key::Period, "." }, { Key::Slash, "/" }, { Key::Semicolon, ";" },
            { Key::Apostrophe, "'" }, { Key::Minus, "-" }, { Key::Equal, "=" }, { Key::LeftBracket, "[" },
            { Key::RightBracket, "]" }, { Key::Backslash, "\\" }, { Key::GraveAccent, "`" },
            { Key::Escape, "Esc" }, { Key::Delete, "Del" }, { Key::Insert, "Ins" }, { Key::Backspace, "Backspace" },
            { Key::PageUp, "PgUp" }, { Key::PageDown, "PgDn" },
            { Key::Left, "\xe2\x86\x90" }, { Key::Right, "\xe2\x86\x92" }, { Key::Up, "\xe2\x86\x91" }, { Key::Down, "\xe2\x86\x93" },
            { Key::KP0, "Num 0" }, { Key::KP1, "Num 1" }, { Key::KP2, "Num 2" }, { Key::KP3, "Num 3" }, { Key::KP4, "Num 4" },
            { Key::KP5, "Num 5" }, { Key::KP6, "Num 6" }, { Key::KP7, "Num 7" }, { Key::KP8, "Num 8" }, { Key::KP9, "Num 9" },
            { Key::KPDecimal, "Num ." }, { Key::KPDivide, "Num /" }, { Key::KPMultiply, "Num *" }, { Key::KPSubtract, "Num -" },
            { Key::KPAdd, "Num +" }, { Key::KPEnter, "Num Enter" }, { Key::KPEqual, "Num =" },
        };

        std::string Lower(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = (char)std::tolower((unsigned char)c);
            return out;
        }

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && std::isspace((unsigned char)text.front())) text.remove_prefix(1);
            while (!text.empty() && std::isspace((unsigned char)text.back())) text.remove_suffix(1);
            return text;
        }

        bool KeyFromAnyName(std::string_view name, Key& out)
        {
            if (KeyFromString(name, out))
                return true;
            for (const KeyLabel& label : s_KeyLabels)
                if (name == label.Label)
                {
                    out = label.Code;
                    return true;
                }
            // Letter keys in any case ("s" for S).
            if (name.size() == 1 && std::isalpha((unsigned char)name[0]))
            {
                out = (Key)((int)Key::A + (std::toupper((unsigned char)name[0]) - 'A'));
                return true;
            }
            return false;
        }

        void UpdateDisplay(ShortcutInfo& s)
        {
            s.Display = s.Fixed && !s.FixedText.empty() ? s.FixedText : s.Chord.GetDisplayName();
        }
    }

    // ------------------------------------------------------------------ chords
    bool KeyChord::IsModifierKey(Key key)
    {
        switch (key)
        {
            case Key::LeftShift: case Key::RightShift: case Key::LeftControl: case Key::RightControl:
            case Key::LeftAlt: case Key::RightAlt: case Key::LeftSuper: case Key::RightSuper:
                return true;
            default:
                return false;
        }
    }

    std::string KeyChord::ToString() const
    {
        if (IsEmpty())
            return {};
        std::string text;
        if (Ctrl) text += "Ctrl+";
        if (Shift) text += "Shift+";
        if (Alt) text += "Alt+";
        const char* name = KeyToString(KeyCode);
        text += *name ? std::string(name) : std::format("Key{}", (int)KeyCode);
        return text;
    }

    std::string KeyChord::GetDisplayName() const
    {
        if (IsEmpty())
            return {};
        std::string text;
        if (Ctrl) text += "Ctrl+";
        if (Shift) text += "Shift+";
        if (Alt) text += "Alt+";
        for (const KeyLabel& label : s_KeyLabels)
            if (label.Code == KeyCode)
                return text + label.Label;
        const char* name = KeyToString(KeyCode);
        return text + (*name ? std::string(name) : std::format("#{}", (int)KeyCode));
    }

    bool KeyChord::Parse(std::string_view text, KeyChord& out)
    {
        text = Trim(text);
        if (text.empty())
        {
            out = KeyChord();
            return true;
        }
        KeyChord chord;
        // Modifiers end with '+'; the key is what is left ("Ctrl++" would be Ctrl and '+').
        while (true)
        {
            const size_t plus = text.find('+');
            if (plus == std::string_view::npos || plus + 1 >= text.size())
                break;
            const std::string modifier = Lower(Trim(text.substr(0, plus)));
            if (modifier == "ctrl" || modifier == "control") chord.Ctrl = true;
            else if (modifier == "shift") chord.Shift = true;
            else if (modifier == "alt") chord.Alt = true;
            else break; // not a modifier: part of the key's name ("Num +" has none before it)
            text = Trim(text.substr(plus + 1));
        }
        Key key;
        if (!KeyFromAnyName(text, key) || IsModifierKey(key))
            return false;
        chord.KeyCode = key;
        out = chord;
        return true;
    }

    // ------------------------------------------------------------------ scopes
    uint32_t GetScopeSituations(ShortcutScope scope)
    {
        using namespace ShortcutSituation;
        // The scene view is all of its modes; each mode only itself.
        constexpr uint32_t sceneView = SceneView | ViewportTools | ViewportTile | ViewportHandles;
        switch (scope)
        {
            case ShortcutScope::Global:         return All;
            case ShortcutScope::Selection:      return Hierarchy | sceneView;
            case ShortcutScope::SceneView:      return sceneView;
            case ShortcutScope::ViewportTools:  return ViewportTools;
            case ShortcutScope::TilePaint:      return ViewportTile;
            case ShortcutScope::HandleEdit:     return ViewportHandles;
            case ShortcutScope::Play:           return Playing;
            case ShortcutScope::Hierarchy:      return Hierarchy;
            case ShortcutScope::ContentBrowser: return ContentBrowser;
            case ShortcutScope::Animation:      return Animation;
            case ShortcutScope::AnimatorGraph:  return AnimatorGraph;
            case ShortcutScope::SpriteEditor:   return SpriteEditor;
            case ShortcutScope::Console:        return Console;
            case ShortcutScope::Camera:         return sceneView;
            case ShortcutScope::Count:          break;
        }
        return 0;
    }

    const char* GetScopeName(ShortcutScope scope)
    {
        switch (scope)
        {
            case ShortcutScope::Global:         return "Hamma joyda";
            case ShortcutScope::Selection:      return "Ierarxiya / viewport";
            case ShortcutScope::SceneView:      return "Viewport";
            case ShortcutScope::ViewportTools:  return "Viewport (tahrirlash)";
            case ShortcutScope::TilePaint:      return "Plitka chizish";
            case ShortcutScope::HandleEdit:     return "Nuqtalar / langarlar";
            case ShortcutScope::Play:           return "O'yin rejimi";
            case ShortcutScope::Hierarchy:      return "Ierarxiya";
            case ShortcutScope::ContentBrowser: return "Kontent";
            case ShortcutScope::Animation:      return "Animatsiya oynasi";
            case ShortcutScope::AnimatorGraph:  return "Animator grafi";
            case ShortcutScope::SpriteEditor:   return "Sprite muharriri";
            case ShortcutScope::Console:        return "Konsol";
            case ShortcutScope::Camera:         return "Viewport kamerasi";
            case ShortcutScope::Count:          break;
        }
        return "";
    }

    bool ShortcutRegistry::ScopesConflict(ShortcutScope a, ShortcutScope b)
    {
        const uint32_t sa = GetScopeSituations(a), sb = GetScopeSituations(b);
        if ((sa & sb) == 0)
            return false;
        if (sa == sb)
            return true;
        // One inside the other: the narrower one wins there (not a conflict).
        return (sa & sb) != sa && (sa & sb) != sb;
    }

    // ------------------------------------------------------------------ registry
    ShortcutInfo& ShortcutRegistry::Register(std::string id, std::string category, std::string name, KeyChord chord, ShortcutScope scope)
    {
        ShortcutInfo* info = FindMutable(id);
        if (!info)
        {
            m_Shortcuts.emplace_back();
            info = &m_Shortcuts.back();
        }
        *info = ShortcutInfo{};
        info->Id = std::move(id);
        info->Category = std::move(category);
        info->Name = std::move(name);
        info->Scope = scope;
        info->Default = info->Chord = chord;
        UpdateDisplay(*info);
        return *info;
    }

    ShortcutInfo& ShortcutRegistry::RegisterFixed(std::string id, std::string category, std::string name, KeyChord chord,
                                                  ShortcutScope scope, std::string text)
    {
        ShortcutInfo& info = Register(std::move(id), std::move(category), std::move(name), chord, scope);
        info.Fixed = true;
        info.FixedText = std::move(text);
        UpdateDisplay(info);
        return info;
    }

    bool ShortcutRegistry::Bind(std::string_view id, std::function<void()> action)
    {
        ShortcutInfo* info = FindMutable(id);
        if (!info || info->Fixed)
            return false;
        info->Action = std::move(action);
        return true;
    }

    ShortcutInfo* ShortcutRegistry::FindMutable(std::string_view id)
    {
        for (ShortcutInfo& s : m_Shortcuts)
            if (s.Id == id)
                return &s;
        return nullptr;
    }

    const ShortcutInfo* ShortcutRegistry::Find(std::string_view id) const
    {
        for (const ShortcutInfo& s : m_Shortcuts)
            if (s.Id == id)
                return &s;
        return nullptr;
    }

    const char* ShortcutRegistry::GetDisplay(std::string_view id) const
    {
        const ShortcutInfo* info = Find(id);
        return info ? info->Display.c_str() : "";
    }

    const ShortcutInfo* ShortcutRegistry::Match(const KeyChord& chord, uint32_t situations) const
    {
        if (chord.IsEmpty())
            return nullptr;
        const ShortcutInfo* best = nullptr;
        int bestWidth = 0;
        for (const ShortcutInfo& s : m_Shortcuts)
        {
            if (s.Chord.IsEmpty() || s.Chord != chord)
                continue;
            const uint32_t covers = GetScopeSituations(s.Scope);
            if ((covers & situations) == 0)
                continue;
            const int width = std::popcount(covers);
            if (!best || width < bestWidth)
            {
                best = &s;
                bestWidth = width;
            }
        }
        return best;
    }

    bool ShortcutRegistry::Rebind(std::string_view id, const KeyChord& chord)
    {
        ShortcutInfo* info = FindMutable(id);
        if (!info || info->Fixed || KeyChord::IsModifierKey(chord.KeyCode))
            return false;
        info->Chord = chord;
        UpdateDisplay(*info);
        return true;
    }

    bool ShortcutRegistry::ResetToDefault(std::string_view id)
    {
        ShortcutInfo* info = FindMutable(id);
        if (!info || info->Fixed)
            return false;
        info->Chord = info->Default;
        UpdateDisplay(*info);
        return true;
    }

    void ShortcutRegistry::ResetAll()
    {
        for (ShortcutInfo& s : m_Shortcuts)
        {
            s.Chord = s.Default;
            UpdateDisplay(s);
        }
    }

    std::vector<const ShortcutInfo*> ShortcutRegistry::FindConflicts(const KeyChord& chord, ShortcutScope scope, std::string_view except) const
    {
        std::vector<const ShortcutInfo*> out;
        if (chord.IsEmpty())
            return out;
        for (const ShortcutInfo& s : m_Shortcuts)
            if (s.Id != except && !s.Chord.IsEmpty() && s.Chord == chord && ScopesConflict(scope, s.Scope))
                out.push_back(&s);
        return out;
    }

    std::vector<const ShortcutInfo*> ShortcutRegistry::FindConflicts(std::string_view id) const
    {
        const ShortcutInfo* info = Find(id);
        return info ? FindConflicts(info->Chord, info->Scope, info->Id) : std::vector<const ShortcutInfo*>{};
    }

    size_t ShortcutRegistry::CountConflicts() const
    {
        size_t count = 0;
        for (const ShortcutInfo& s : m_Shortcuts)
            if (!FindConflicts(s.Chord, s.Scope, s.Id).empty())
                count++;
        return count;
    }

    size_t ShortcutRegistry::CountModified() const
    {
        return (size_t)std::count_if(m_Shortcuts.begin(), m_Shortcuts.end(), [](const ShortcutInfo& s) { return s.IsModified(); });
    }

    bool ShortcutRegistry::MatchesSearch(const ShortcutInfo& s, std::string_view query)
    {
        query = Trim(query);
        while (!query.empty())
        {
            const size_t space = query.find(' ');
            const std::string_view word = query.substr(0, space);
            query = space == std::string_view::npos ? std::string_view() : Trim(query.substr(space + 1));
            if (word.empty())
                continue;
            const bool found = ContainsIgnoreCase(s.Name, word) || ContainsIgnoreCase(s.Category, word) || ContainsIgnoreCase(s.Id, word) ||
                               ContainsIgnoreCase(s.Display, word) || ContainsIgnoreCase(GetScopeName(s.Scope), word);
            if (!found)
                return false;
        }
        return true;
    }

    std::map<std::string, std::string> ShortcutRegistry::GetOverrides() const
    {
        std::map<std::string, std::string> out;
        for (const ShortcutInfo& s : m_Shortcuts)
            if (s.IsModified())
                out[s.Id] = s.Chord.ToString();
        return out;
    }

    size_t ShortcutRegistry::ApplyOverrides(const std::map<std::string, std::string>& overrides)
    {
        ResetAll();
        size_t applied = 0;
        for (const auto& [id, text] : overrides)
        {
            KeyChord chord;
            if (KeyChord::Parse(text, chord) && Rebind(id, chord))
                applied++;
        }
        return applied;
    }

    std::vector<std::string> ShortcutRegistry::GetCategories() const
    {
        std::vector<std::string> out;
        for (const ShortcutInfo& s : m_Shortcuts)
            if (std::find(out.begin(), out.end(), s.Category) == out.end())
                out.push_back(s.Category);
        return out;
    }

    // ------------------------------------------------------------------ the editor's shortcuts
    std::string EditorShortcut::BookmarkSave(int slot) { return std::format("view.bookmark_save_{}", slot); }
    std::string EditorShortcut::BookmarkRecall(int slot) { return std::format("view.bookmark_recall_{}", slot); }

    void RegisterEditorShortcuts(ShortcutRegistry& r)
    {
        using S = ShortcutScope;
        namespace E = EditorShortcut;
        constexpr bool ctrl = true, shift = true, alt = true;

        const char* file = "Fayl";
        r.Register(E::NewScene, file, "Yangi sahna", { Key::N, ctrl });
        r.Register(E::OpenScene, file, "Sahnani ochish", { Key::O, ctrl });
        r.Register(E::SaveScene, file, "Saqlash", { Key::S, ctrl });
        r.Register(E::SaveSceneAs, file, "Boshqacha saqlash", { Key::S, ctrl, shift });
        r.Register(E::OpenProject, file, "Loyihani ochish", { Key::O, ctrl, shift });

        const char* edit = "Tahrirlash";
        r.Register(E::Undo, edit, "Bekor qilish (Undo)", { Key::Z, ctrl });
        r.Register(E::Redo, edit, "Qaytarish (Redo)", { Key::Y, ctrl });
        r.Register(E::RedoAlt, edit, "Qaytarish (muqobil)", { Key::Z, ctrl, shift });
        r.Register(E::UndoHistory, edit, "Undo tarixi oynasi", { Key::H, ctrl });
        r.Register(E::Copy, edit, "Nusxalash", { Key::C, ctrl });
        r.Register(E::Paste, edit, "Joylash", { Key::V, ctrl });
        r.Register(E::Duplicate, edit, "Nusxa olish (dublikat)", { Key::D, ctrl });
        r.Register(E::Preferences, edit, "Sozlamalar oynasi", { Key::Comma, ctrl });

        const char* selection = "Tanlov";
        r.Register(E::Delete, selection, "O'chirish", { Key::Delete }, S::Selection);
        r.Register(E::SelectAll, selection, "Hammasini tanlash", { Key::A, ctrl }, S::Selection);
        r.Register(E::ToggleActive, selection, "Faol / nofaol qilish", { Key::A, false, shift, alt }, S::Selection);
        r.Register(E::ToggleHidden, selection, "Editorda yashirish / ko'rsatish", { Key::H }, S::Selection);
        r.Register(E::ShowAllHidden, selection, "Hammasini ko'rsatish", { Key::H, false, false, alt }, S::Selection);

        const char* play = "O'yin";
        r.Register(E::PlayToggle, play, "O'ynash / to'xtatish", { Key::P, ctrl });
        r.Register(E::PlayStop, play, "O'yinni to'xtatish", { Key::Escape, false, shift }, S::Play);
        r.Register(E::BuildScripts, play, "Skriptlarni kompilyatsiya qilish", { Key::B, ctrl });

        const char* tools = "Asboblar (viewport)";
        r.Register(E::ToolNone, tools, "Tanlash (gizmo yo'q)", { Key::Q }, S::ViewportTools);
        r.Register(E::ToolMove, tools, "Siljitish", { Key::W }, S::ViewportTools);
        r.Register(E::ToolRotate, tools, "Aylantirish", { Key::E }, S::ViewportTools);
        r.Register(E::ToolScale, tools, "Masshtab", { Key::R }, S::ViewportTools);

        const char* view = "Kamera (viewport)";
        r.Register(E::FrameSelection, view, "Tanlovga fokus", { Key::F }, S::SceneView);
        r.Register(E::FrameAll, view, "Butun sahnani ko'rsatish", { Key::F, false, shift }, S::SceneView);
        for (int slot = 1; slot <= E::BookmarkSlots; slot++)
        {
            const Key digit = (Key)((int)Key::D0 + slot);
            r.Register(E::BookmarkRecall(slot), view, std::format("{}-xatcho'pga qaytish", slot), { digit }, S::SceneView);
            r.Register(E::BookmarkSave(slot), view, std::format("{}-xatcho'pni saqlash", slot), { digit, ctrl }, S::SceneView);
        }
        r.RegisterFixed("camera.fly", view, "Uchish (Q / E - pastga / yuqoriga, Shift - tezroq)", {}, S::Camera, "O'ng tugma + WASD");
        r.RegisterFixed("camera.fly_speed", view, "Uchish tezligi", {}, S::Camera, "O'ng tugma + g'ildirak");
        r.RegisterFixed("camera.orbit", view, "Aylanib qarash (orbit)", {}, S::Camera, "Alt + chap tugma");
        r.RegisterFixed("camera.pan", view, "Surish (pan)", {}, S::Camera, "O'rta tugma");
        r.RegisterFixed("camera.zoom", view, "Yaqinlashish / uzoqlashish", {}, S::Camera, "G'ildirak, Alt + o'ng tugma");

        const char* tiles = "Plitka chizish";
        r.Register(E::TileBrush, tiles, "Mo'yqalam", { Key::B }, S::TilePaint);
        r.Register(E::TileRectangle, tiles, "To'rtburchak", { Key::R }, S::TilePaint);
        r.Register(E::TileFill, tiles, "To'ldirish", { Key::G }, S::TilePaint);
        r.Register(E::TileEraser, tiles, "O'chirg'ich", { Key::E }, S::TilePaint);
        r.Register(E::TilePicker, tiles, "Plitkani olish (pipetka)", { Key::I }, S::TilePaint);
        r.Register(E::TileExit, tiles, "Chizishdan chiqish", { Key::Escape }, S::TilePaint);
        r.Register(E::TileExitNone, tiles, "Chiqish (gizmo yo'q)", { Key::Q }, S::TilePaint);
        r.Register(E::TileExitMove, tiles, "Chiqish (siljitish)", { Key::W }, S::TilePaint);
        r.RegisterFixed("tiles.erase_hold", tiles, "Vaqtincha o'chirish", {}, S::TilePaint, "Shift + chap tugma");

        const char* handles = "Nuqtalar va langarlar";
        r.Register(E::HandlesExit, handles, "Tahrirlashdan chiqish", { Key::Escape }, S::HandleEdit);
        r.Register(E::HandlesExitNone, handles, "Chiqish (gizmo yo'q)", { Key::Q }, S::HandleEdit);
        r.Register(E::HandlesExitMove, handles, "Chiqish (siljitish)", { Key::W }, S::HandleEdit);
        r.RegisterFixed("handles.delete_point", handles, "Nuqtani o'chirish", { Key::Delete }, S::HandleEdit);
        r.RegisterFixed("handles.remove_click", handles, "Nuqtani olib tashlash", {}, S::HandleEdit, "Ctrl + chap tugma");

        const char* windows = "Oynalar";
        r.Register(E::ShortcutsWindow, windows, "Klaviatura yorliqlari", { Key::F1 });
        r.RegisterFixed("hierarchy.rename", windows, "Ierarxiya: nomini o'zgartirish", { Key::F2 }, S::Hierarchy);
        r.RegisterFixed("content.rename", windows, "Kontent: nomini o'zgartirish", { Key::F2 }, S::ContentBrowser);
        r.RegisterFixed("content.delete", windows, "Kontent: o'chirish", { Key::Delete }, S::ContentBrowser);
        r.RegisterFixed("content.select_all", windows, "Kontent: hammasini tanlash", { Key::A, ctrl }, S::ContentBrowser);
        r.RegisterFixed("content.search", windows, "Kontent: qidiruv", { Key::F, ctrl }, S::ContentBrowser);
        r.RegisterFixed("content.open", windows, "Kontent: ochish", { Key::Enter }, S::ContentBrowser);
        r.RegisterFixed("content.up", windows, "Kontent: yuqori papka", { Key::Backspace }, S::ContentBrowser);
        r.RegisterFixed("content.deselect", windows, "Kontent: tanlovni bekor qilish", { Key::Escape }, S::ContentBrowser);
        r.RegisterFixed("console.copy", windows, "Konsol: xabarni nusxalash", { Key::C, ctrl }, S::Console);
        r.RegisterFixed("animation.play", windows, "Animatsiya: ijro / pauza", { Key::Space }, S::Animation);
        r.RegisterFixed("animation.key", windows, "Animatsiya: kalit qo'shish", { Key::K }, S::Animation);
        r.RegisterFixed("animation.delete", windows, "Animatsiya: kalitlarni o'chirish", { Key::Delete }, S::Animation);
        r.RegisterFixed("animation.frame", windows, "Animatsiya: hammasini ko'rsatish", { Key::F }, S::Animation);
        r.RegisterFixed("animation.start", windows, "Animatsiya: boshiga", { Key::Home }, S::Animation);
        r.RegisterFixed("animation.end", windows, "Animatsiya: oxiriga", { Key::End }, S::Animation);
        r.RegisterFixed("animation.prev", windows, "Animatsiya: oldingi kadr", { Key::Left }, S::Animation);
        r.RegisterFixed("animation.next", windows, "Animatsiya: keyingi kadr", { Key::Right }, S::Animation);
        r.RegisterFixed("animator.delete", windows, "Animator grafi: o'chirish", { Key::Delete }, S::AnimatorGraph);
        r.RegisterFixed("animator.frame", windows, "Animator grafi: hammasini ko'rsatish", { Key::F }, S::AnimatorGraph);
        r.RegisterFixed("animator.cancel", windows, "Animator grafi: bog'lashni bekor qilish", { Key::Escape }, S::AnimatorGraph);
        r.RegisterFixed("sprite.delete", windows, "Sprite muharriri: kadrni o'chirish", { Key::Delete }, S::SpriteEditor);
        r.RegisterFixed("sprite.frame", windows, "Sprite muharriri: hammasini ko'rsatish", { Key::F }, S::SpriteEditor);
    }

}
