#pragma once
#include "RulesModel.h"
#include <optional>
#include <vector>
namespace Zamin::Gameplay {
    struct MenuEntry
    {
        std::string Name;
        bool Pause = true, Block = true;
    };
    class MenuStack
    {
    public:
        bool Push(MenuEntry, std::string& error);
        std::optional<MenuEntry> Pop();
        bool Remove(const std::string&);
        void Clear() { m_Entries.clear(); }
        const std::vector<MenuEntry>& Entries() const { return m_Entries; }
        bool Pauses() const;
        bool Blocks() const;
        bool Contains(const std::string&) const;
        std::string Top() const { return m_Entries.empty() ? "" : m_Entries.back().Name; }
    private:
        std::vector<MenuEntry> m_Entries;
    };
    bool ValidUIName(const std::string&);
    struct InventoryItem
    {
        std::string Key;
        int Count = 0;
    };
    struct InventoryPage
    {
        int Page = 0, Pages = 1;
        std::vector<InventoryItem> Items;
    };
    InventoryPage PresentInventory(const Bag&, int page, int rows);
    struct HUDPresentation
    {
        std::string Text;
        float Fraction = 0;
    };
    HUDPresentation PresentHUD(double value, double maximum, const std::string& prefix, bool ratio);
    // An ability slot: Prefix when ready, Prefix + ceil(remaining) while cooling down; bar = 1 - remaining/cooldown.
    // A locked ability shows Prefix + "--" with an empty bar.
    HUDPresentation PresentAbility(double remaining, double cooldown, bool unlocked, const std::string& prefix);
    // A progression badge: Prefix + level; bar = current / needed (0 when nothing is needed).
    HUDPresentation PresentLevel(uint32_t level, uint32_t current, uint32_t needed, const std::string& prefix);
    // A combat feed row: "-18  Bolt" (damage) or "+5  Mend" (heal/shield); the amount is rounded to a whole number.
    std::string PresentCombatRow(bool damage, double amount, const std::string& key);
    // A feed row's opacity: 1 when new, fading linearly to 0 at `lifetime` (> 0) seconds.
    float CombatRowAlpha(double age, double lifetime);
}
