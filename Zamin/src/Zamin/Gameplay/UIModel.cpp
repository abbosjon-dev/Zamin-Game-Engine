#include "UIModel.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace Zamin::Gameplay {
    bool ValidUIName(const std::string& s) { return !s.empty() && s.size() <= 32 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos; }
    bool MenuStack::Contains(const std::string& name) const
    {
        return std::any_of(m_Entries.begin(), m_Entries.end(), [&](auto& e) { return e.Name == name; });
    }
    bool MenuStack::Push(MenuEntry entry, std::string& error)
    {
        error.clear();
        if (!ValidUIName(entry.Name) || m_Entries.size() >= 8 || Contains(entry.Name))
        {
            error = "UI stack requires unique names and has depth limit 8";
            return false;
        }
        m_Entries.push_back(std::move(entry));
        return true;
    }
    std::optional<MenuEntry> MenuStack::Pop()
    {
        if (m_Entries.empty()) return {};
        auto last = std::move(m_Entries.back());
        m_Entries.pop_back();
        return last;
    }
    bool MenuStack::Remove(const std::string& name)
    {
        return std::erase_if(m_Entries, [&](auto& e) { return e.Name == name; }) > 0;
    }
    bool MenuStack::Pauses() const
    {
        return std::any_of(m_Entries.begin(), m_Entries.end(), [](auto& e) { return e.Pause; });
    }
    bool MenuStack::Blocks() const
    {
        return std::any_of(m_Entries.begin(), m_Entries.end(), [](auto& e) { return e.Block; });
    }
    InventoryPage PresentInventory(const Bag& bag, int page, int rows)
    {
        std::string error;
        Bag check = bag;
        if (!check.Configure(bag.Slots, bag.Stack, false, error) || rows < 1 || rows > 16) throw std::runtime_error("Invalid inventory presentation limits");
        for (auto& [key, count] : bag.Items)
            if (!ValidItem(key) || count < 1 || count > bag.Stack) throw std::runtime_error("Invalid inventory presentation item");
        InventoryPage out;
        out.Pages = std::max(1, (int(bag.Items.size()) + rows - 1) / rows);
        out.Page = std::clamp(page, 0, out.Pages - 1);
        int index = 0;
        for (auto& [key, count] : bag.Items)
        {
            if (index >= out.Page * rows && index < (out.Page + 1) * rows) out.Items.push_back({ key, count });
            ++index;
        }
        return out;
    }
    HUDPresentation PresentHUD(double value, double maximum, const std::string& prefix, bool ratio)
    {
        if (!std::isfinite(value) || !std::isfinite(maximum) || value < 0 || maximum < 0 || value > 1e12 || maximum > 1e12 || prefix.size() > 64) throw std::runtime_error("Invalid HUD presentation");
        std::ostringstream text;
        text << prefix << std::fixed << std::setprecision(0) << value;
        if (ratio) text << " / " << maximum;
        return { text.str(), maximum > 0 ? float(std::clamp(value / maximum, 0.0, 1.0)) : 0 };
    }
}
