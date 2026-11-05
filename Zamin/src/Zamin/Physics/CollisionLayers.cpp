#include "Zamin/Physics/CollisionLayers.h"

#include <nlohmann/json.hpp>

#include <format>

namespace Zamin {

    using json = nlohmann::json;

    std::string CollisionLayers2D::GetName(int layer) const
    {
        if (!IsValid(layer))
            return "?";
        const std::string& name = Names[(size_t)layer];
        return name.empty() ? std::format("Qatlam {}", layer) : name;
    }

    int CollisionLayers2D::Find(std::string_view name) const
    {
        for (int i = 0; i < Count; i++)
            if (!Names[(size_t)i].empty() && Names[(size_t)i] == name)
                return i;
        return -1;
    }

    std::vector<int> CollisionLayers2D::GetNamedLayers() const
    {
        std::vector<int> layers;
        for (int i = 0; i < Count; i++)
            if (i == 0 || !Names[(size_t)i].empty())
                layers.push_back(i);
        return layers;
    }

    void CollisionLayers2D::SetCollision(int a, int b, bool collide)
    {
        if (!IsValid(a) || !IsValid(b))
            return;
        if (collide)
        {
            Masks[(size_t)a] |= (uint16_t)(1u << b);
            Masks[(size_t)b] |= (uint16_t)(1u << a);
        }
        else
        {
            Masks[(size_t)a] &= (uint16_t)~(1u << b);
            Masks[(size_t)b] &= (uint16_t)~(1u << a);
        }
    }

    uint16_t CollisionLayers2D::MakeMask(std::initializer_list<std::string_view> names) const
    {
        uint16_t mask = 0;
        for (std::string_view name : names)
            if (int layer = Find(name); layer >= 0)
                mask |= GetCategoryBits(layer);
        return mask;
    }

    std::string CollisionLayers2D::ToJson() const
    {
        json names = json::array(), collisions = json::array();
        for (int i = 0; i < Count; i++)
        {
            names.push_back(Names[(size_t)i]);
            collisions.push_back(Masks[(size_t)i]);
        }
        return json{ { "Names", names }, { "Collisions", collisions } }.dump(4);
    }

    bool CollisionLayers2D::FromJson(std::string_view text, CollisionLayers2D& out)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        CollisionLayers2D layers;
        if (auto it = j.find("Names"); it != j.end() && it->is_array())
            for (size_t i = 0; i < it->size() && i < (size_t)Count; i++)
                if ((*it)[i].is_string())
                    layers.Names[i] = (*it)[i].get<std::string>();
        if (layers.Names[0].empty())
            layers.Names[0] = "Default";
        if (auto it = j.find("Collisions"); it != j.end() && it->is_array())
            for (size_t i = 0; i < it->size() && i < (size_t)Count; i++)
                if ((*it)[i].is_number_unsigned())
                    layers.Masks[i] = (uint16_t)((*it)[i].get<uint32_t>() & AllLayers);
        // Symmetric: both rows must let the pair collide.
        for (int a = 0; a < Count; a++)
            for (int b = a + 1; b < Count; b++)
                layers.SetCollision(a, b, layers.CanCollide(a, b) && layers.CanCollide(b, a));
        out = layers;
        return true;
    }

}
