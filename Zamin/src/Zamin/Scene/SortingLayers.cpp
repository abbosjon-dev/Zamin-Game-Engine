#include "Zamin/Scene/SortingLayers.h"

#include <algorithm>

namespace Zamin {

    static std::string_view Trim(std::string_view s)
    {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
            s.remove_suffix(1);
        return s;
    }

    SortingLayerList::SortingLayerList()
    {
        m_Layers.push_back({ DefaultID, std::string(DefaultName) });
    }

    int SortingLayerList::GetIndex(uint32_t id) const
    {
        int fallback = 0;
        for (size_t i = 0; i < m_Layers.size(); i++)
        {
            if (m_Layers[i].ID == id)
                return (int)i;
            if (m_Layers[i].ID == DefaultID)
                fallback = (int)i;
        }
        return fallback;
    }

    const SortingLayer* SortingLayerList::Find(uint32_t id) const
    {
        for (const SortingLayer& l : m_Layers)
            if (l.ID == id)
                return &l;
        return nullptr;
    }

    const SortingLayer* SortingLayerList::FindByName(std::string_view name) const
    {
        for (const SortingLayer& l : m_Layers)
            if (l.Name == name)
                return &l;
        return nullptr;
    }

    const std::string& SortingLayerList::GetName(uint32_t id) const
    {
        if (const SortingLayer* l = Find(id))
            return l->Name;
        if (const SortingLayer* l = Find(DefaultID))
            return l->Name;
        static const std::string s_Default(DefaultName);
        return s_Default;
    }

    bool SortingLayerList::IsYSorted(uint32_t id) const
    {
        if (const SortingLayer* l = Find(id))
            return l->YSort;
        const SortingLayer* fallback = Find(DefaultID);
        return fallback && fallback->YSort;
    }

    bool SortingLayerList::SetYSort(uint32_t id, bool ySort)
    {
        auto it = std::find_if(m_Layers.begin(), m_Layers.end(), [&](const SortingLayer& l) { return l.ID == id; });
        if (it == m_Layers.end())
            return false;
        it->YSort = ySort;
        return true;
    }

    uint32_t SortingLayerList::Add(std::string_view name)
    {
        name = Trim(name);
        if (name.empty() || FindByName(name) || m_Layers.size() >= MaxLayers)
            return 0;
        uint32_t id = m_NextID++;
        m_Layers.push_back({ id, std::string(name) });
        return id;
    }

    bool SortingLayerList::Rename(uint32_t id, std::string_view name)
    {
        name = Trim(name);
        auto it = std::find_if(m_Layers.begin(), m_Layers.end(), [&](const SortingLayer& l) { return l.ID == id; });
        if (it == m_Layers.end() || name.empty())
            return false;
        if (const SortingLayer* other = FindByName(name); other && other->ID != id)
            return false;
        it->Name = std::string(name);
        return true;
    }

    bool SortingLayerList::Remove(uint32_t id)
    {
        if (id == DefaultID)
            return false;
        auto it = std::find_if(m_Layers.begin(), m_Layers.end(), [&](const SortingLayer& l) { return l.ID == id; });
        if (it == m_Layers.end())
            return false;
        m_Layers.erase(it);
        return true;
    }

    bool SortingLayerList::Move(size_t from, size_t to)
    {
        if (from >= m_Layers.size() || to >= m_Layers.size())
            return false;
        if (from == to)
            return true;
        SortingLayer layer = m_Layers[from];
        m_Layers.erase(m_Layers.begin() + (std::ptrdiff_t)from);
        m_Layers.insert(m_Layers.begin() + (std::ptrdiff_t)to, std::move(layer));
        return true;
    }

    uint32_t SortingLayerList::Resolve(uint32_t id, std::string_view name) const
    {
        const SortingLayer* byID = Find(id);
        // Same ID, same name (or no name stored): that layer.
        if (byID && (name.empty() || byID->Name == name))
            return id;
        // The name decides when the ID means another layer here.
        if (!name.empty())
            if (const SortingLayer* byName = FindByName(name))
                return byName->ID;
        // Unknown here: the ID is kept as it is (drawn on Default, saved back unchanged).
        return id;
    }

    void SortingLayerList::Assign(const std::vector<SortingLayer>& layers)
    {
        m_Layers.clear();
        m_NextID = 1;
        for (const SortingLayer& l : layers)
        {
            std::string_view name = Trim(l.Name);
            if (name.empty() || Find(l.ID) || FindByName(name) || m_Layers.size() >= MaxLayers)
                continue;
            m_Layers.push_back({ l.ID, std::string(name), l.YSort });
            m_NextID = std::max(m_NextID, l.ID + 1);
        }
        if (!Find(DefaultID))
        {
            // The name may be taken by a layer with another ID: that one keeps it.
            std::string name(DefaultName);
            for (int n = 2; FindByName(name); n++)
                name = std::string(DefaultName) + " " + std::to_string(n);
            m_Layers.insert(m_Layers.begin(), { DefaultID, name });
        }
    }

}
