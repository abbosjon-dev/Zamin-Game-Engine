#include "Zamin/Physics/CollisionLayers.h"

#include <utility>

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


    void CollisionLayers2D::SetRow(int layer, const std::vector<int>& others, bool collide)
    {
        for (int other : others)
            SetCollision(layer, other, collide);
    }

    int CollisionLayers2D::CountCollidingPairs(const std::vector<int>& layers) const
    {
        int count = 0;
        for (size_t i = 0; i < layers.size(); i++)
            for (size_t j = i; j < layers.size(); j++)
                count += CanCollide(layers[i], layers[j]) ? 1 : 0;
        return count;
    }

    namespace {
        constexpr CollisionLayerPreset s_Presets[] = {
            { "Hammasi to'qnashadi", "Har bir qatlam har biri bilan to'qnashadi (yangi loyiha). Nomlar o'zgarmaydi.", false },
            { "Faqat o'z qatlami bilan", "Har bir qatlam faqat o'zi va Default bilan to'qnashadi. Nomlar o'zgarmaydi.", false },
            { "Hech biri", "Hech qanday juftlik to'qnashmaydi (keyin kerakli juftliklarni yoqing). Nomlar o'zgarmaydi.", false },
            { "Platformer 2D",
              "Default, Player, Enemy, Ground, Pickup, Projectile, Trigger: o'yinchi yer, dushman, buyum va triggerlarga; dushman yer va "
              "o'qlarga; o'q yerga tegadi. Bir xil qatlamdagi o'yinchilar / dushmanlar / o'qlar bir-biridan o'tadi.",
              true },
            { "3D aksiya",
              "Default, Player, Enemy, World, Projectile, Pickup, Ragdoll, Trigger: o'yinchi dunyo, dushman, buyum va triggerlarga; "
              "dushmanlar bir-biriga, dunyoga va o'qlarga; ragdoll faqat dunyoga va o'ziga tegadi.",
              true },
        };

        void SetNames(CollisionLayers2D& layers, std::initializer_list<const char*> names)
        {
            layers.Names.fill(std::string());
            int i = 0;
            for (const char* name : names)
                layers.Names[(size_t)i++] = name;
        }

        // Only these pairs (and Default with everything, unnamed layers with everything) collide.
        void SetPairs(CollisionLayers2D& layers, std::initializer_list<std::pair<int, int>> pairs)
        {
            const std::vector<int> named = layers.GetNamedLayers();
            for (int a : named)
                for (int b : named)
                    layers.SetCollision(a, b, a == 0 || b == 0);
            for (const auto& [a, b] : pairs)
                layers.SetCollision(a, b, true);
        }
    }

    std::span<const CollisionLayerPreset> CollisionLayers2D::GetPresets() { return s_Presets; }

    bool CollisionLayers2D::ApplyPreset(int index)
    {
        switch (index)
        {
            case 0:
                Masks.fill(AllLayers);
                return true;
            case 1:
                for (int a = 0; a < Count; a++)
                    Masks[(size_t)a] = (uint16_t)((1u << a) | 1u);
                Masks[0] = AllLayers;
                return true;
            case 2:
                Masks.fill(0);
                return true;
            case 3:
            {
                enum { Default, Player, Enemy, Ground, Pickup, Projectile, Trigger };
                Masks.fill(AllLayers);
                SetNames(*this, { "Default", "Player", "Enemy", "Ground", "Pickup", "Projectile", "Trigger" });
                SetPairs(*this, { { Player, Ground }, { Player, Enemy }, { Player, Pickup }, { Player, Trigger },
                                  { Enemy, Ground }, { Enemy, Projectile }, { Projectile, Ground } });
                return true;
            }
            case 4:
            {
                enum { Default, Player, Enemy, World, Projectile, Pickup, Ragdoll, Trigger };
                Masks.fill(AllLayers);
                SetNames(*this, { "Default", "Player", "Enemy", "World", "Projectile", "Pickup", "Ragdoll", "Trigger" });
                SetPairs(*this, { { Player, World }, { Player, Enemy }, { Player, Pickup }, { Player, Trigger },
                                  { Enemy, Enemy }, { Enemy, World }, { Enemy, Projectile }, { Projectile, World },
                                  { Pickup, World }, { Ragdoll, World }, { Ragdoll, Ragdoll } });
                return true;
            }
            default:
                return false;
        }
    }

}
