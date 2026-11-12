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
}
