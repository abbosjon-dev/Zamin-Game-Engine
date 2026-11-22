#include "SaveModel.h"
#include <cmath>
#include <set>
#include <stdexcept>
namespace Zamin::Gameplay {
    namespace {
        using V = Visual::Value;
        constexpr size_t Limit = 2 * 1024 * 1024;
        void Require(bool condition, const char* error)
        {
            if (!condition) throw std::runtime_error(error);
        }
        void Keys(const V& v, std::initializer_list<const char*> names, std::initializer_list<const char*> optional = {})
        {
            size_t present = 0;
            for (auto name : optional) present += v.is_object() && v.contains(name);
            Require(v.is_object() && v.size() == names.size() + present, "Invalid save object fields");
            for (auto name : names) Require(v.contains(name), "Missing save field");
        }
        double Number(const V& v, double low, double high)
        {
            Require(v.is_number(), "Save number expected");
            double n = v.get<double>();
            Require(std::isfinite(n) && n >= low && n <= high, "Save number outside limits");
            return n;
        }
        uint32_t Counter(const V& v)
        {
            auto n = Number(v, 0, UINT32_MAX);
            Require(std::floor(n) == n, "Save integer expected");
            return uint32_t(n);
        }
        bool Bool(const V& v)
        {
            Require(v.is_boolean(), "Save boolean expected");
            return v.get<bool>();
        }
        std::string Text(const V& v, size_t limit)
        {
            Require(v.is_string(), "Save string expected");
            auto s = v.get<std::string>();
            Require(s.size() <= limit, "Save string limit");
            return s;
        }
        V Vec(glm::vec3 p) { return V::array({ p.x, p.y, p.z }); }
        glm::vec3 Vec(const V& v, double limit)
        {
            Require(v.is_array() && v.size() == 3, "Save vector expected");
            return { float(Number(v[0], -limit, limit)), float(Number(v[1], -limit, limit)), float(Number(v[2], -limit, limit)) };
        }
        V DVec(const glm::dvec3& p) { return V::array({ p.x, p.y, p.z }); }
        glm::dvec3 DVec(const V& v, double limit)
        {
            Require(v.is_array() && v.size() == 3, "Save vector expected");
            return { Number(v[0], -limit, limit), Number(v[1], -limit, limit), Number(v[2], -limit, limit) };
        }
        uint32_t Small(const V& v, uint32_t high)
        {
            auto n = Counter(v);
            Require(n <= high, "Save integer outside limits");
            return n;
        }
        std::string Reference(const V& v)
        {
            auto s = Text(v, 64);
            Require(ValidSaveReference(s), "Invalid save reference");
            return s;
        }
        V EffectsValue(const std::vector<SavedEffect>& effects)
        {
            V out = V::array();
            for (auto& e : effects) out.push_back({ { "Key", e.Key }, { "Source", e.Source }, { "Team", e.Team }, { "FriendlyFire", e.FriendlyFire }, { "Stacks", e.Stacks }, { "Remaining", e.Remaining }, { "Tick", e.Tick }, { "Magnitude", e.Magnitude }, { "Sequence", e.Sequence } });
            return out;
        }
        std::vector<SavedEffect> ReadEffects(const V& v)
        {
            Require(v.is_array() && v.size() <= 16, "Saved effect limit 16");
            std::vector<SavedEffect> out;
            for (auto& item : v)
            {
                Keys(item, { "Key", "Source", "Team", "FriendlyFire", "Stacks", "Remaining", "Tick", "Magnitude", "Sequence" });
                SavedEffect e;
                e.Key = Text(item.at("Key"), 64);
                Require(ValidItem(e.Key), "Invalid saved effect key");
                e.Source = Reference(item.at("Source"));
                e.Team = Small(item.at("Team"), 65535);
                e.FriendlyFire = Bool(item.at("FriendlyFire"));
                e.Stacks = Small(item.at("Stacks"), 16);
                Require(e.Stacks >= 1, "Saved effect stacks 1..16");
                // The longest instance: 600 s x 16 stacks (Extend); remaining time is strictly positive.
                e.Remaining = Number(item.at("Remaining"), 0, 9600);
                Require(e.Remaining > 0, "Saved effect has expired");
                // A tick due on a step boundary may leave the phase a rounding error above the period (at most 60 s):
                // the same 1e-9 the effect model allows.
                e.Tick = Number(item.at("Tick"), 0, 60 + 1e-9);
                e.Magnitude = Number(item.at("Magnitude"), 0, 100000);
                auto& sequence = item.at("Sequence");
                Require(sequence.is_number_unsigned() && sequence.get<uint64_t>() >= 1 && sequence.get<uint64_t>() <= (uint64_t(1) << 53), "Invalid saved effect sequence");
                e.Sequence = sequence.get<uint64_t>();
                Require(out.empty() || out.back().Sequence < e.Sequence, "Saved effects must be in application order");
                out.push_back(std::move(e));
            }
            return out;
        }
        V CombatValue(const SavedCombat& c)
        {
            V projectiles = V::array();
            for (auto& p : c.Projectiles)
            {
                projectiles.push_back({ { "Slot", p.Slot }, { "Generation", p.Generation }, { "Key", p.Key }, { "Effect", p.Effect }, { "Owner", p.Owner }, { "Team", p.Team }, { "Target", p.Target }, { "Position", DVec(p.Position) }, { "Velocity", DVec(p.Velocity) }, { "Muzzle", DVec(p.Muzzle) }, { "Age", p.Age }, { "Travelled", p.Travelled }, { "Damage", p.Damage }, { "Attack", p.Attack }, { "PierceLeft", p.PierceLeft }, { "Hits", p.Hits }, { "Phase", p.Phase } });
                // Written only when set, so documents without a gone owner keep their exact bytes.
                if (!p.FormerOwner.empty()) projectiles.back()["FormerOwner"] = p.FormerOwner;
            }
            return { { "Signature", c.Signature }, { "Projectiles", projectiles } };
        }
        SavedCombat ReadCombat(const V& v)
        {
            Keys(v, { "Signature", "Projectiles" });
            SavedCombat c;
            c.Signature = Text(v.at("Signature"), 65536);
            auto& projectiles = v.at("Projectiles");
            Require(projectiles.is_array() && projectiles.size() <= 128, "Saved projectile limit 128");
            std::set<uint32_t> slots;
            for (auto& item : projectiles)
            {
                Keys(item, { "Slot", "Generation", "Key", "Effect", "Owner", "Team", "Target", "Position", "Velocity", "Muzzle", "Age", "Travelled", "Damage", "Attack", "PierceLeft", "Hits", "Phase" }, { "FormerOwner" });
                SavedProjectile p;
                p.Slot = Small(item.at("Slot"), 127);
                Require(slots.insert(p.Slot).second, "Duplicate saved projectile slot");
                p.Generation = Small(item.at("Generation"), 0xFFFFFF);
                Require(p.Generation >= 1, "Invalid saved projectile generation");
                p.Key = Text(item.at("Key"), 64);
                p.Effect = Text(item.at("Effect"), 64);
                Require(ValidItem(p.Key) && (p.Effect.empty() || ValidItem(p.Effect)), "Invalid saved projectile key");
                p.Owner = Reference(item.at("Owner"));
                if (item.contains("FormerOwner"))
                {
                    p.FormerOwner = Reference(item.at("FormerOwner"));
                    Require(!p.FormerOwner.empty() && p.Owner.empty(), "Invalid saved former projectile owner");
                }
                p.Target = Reference(item.at("Target"));
                p.Team = Small(item.at("Team"), 65535);
                p.Position = DVec(item.at("Position"), 1e9);
                p.Velocity = DVec(item.at("Velocity"), 1e6);
                p.Muzzle = DVec(item.at("Muzzle"), 100);
                p.Age = Number(item.at("Age"), 0, 31);
                p.Travelled = Number(item.at("Travelled"), 0, 1e9);
                p.Damage = Number(item.at("Damage"), 0, 1e6);
                p.Attack = Number(item.at("Attack"), 0, 1e6);
                p.PierceLeft = Small(item.at("PierceLeft"), 7);
                p.Phase = Small(item.at("Phase"), 1);
                auto& hits = item.at("Hits");
                Require(hits.is_array() && hits.size() <= 8, "Saved projectile hit limit 8");
                std::set<std::string> seen;
                for (auto& hit : hits)
                {
                    auto reference = Reference(hit);
                    Require(reference.empty() || seen.insert(reference).second, "Duplicate saved projectile hit");
                    p.Hits.push_back(std::move(reference));
                }
                c.Projectiles.push_back(std::move(p));
            }
            return c;
        }
        std::string Hash(const std::string& bytes)
        {
            uint64_t h = 14695981039346656037ull;
            for (unsigned char c : bytes)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            return std::to_string(h);
        }
        V LoadoutValue(const std::optional<LoadoutState>& state)
        {
            if (!state) return nullptr;
            V equipment = V::array({ "", "", "", "" });
            for (auto& [slot, key] : state->Equipped)
            {
                Require(int(slot) >= 0 && int(slot) < 4, "Invalid saved equipment slot");
                equipment[int(slot)] = key;
            }
            V abilities = V::object();
            for (auto& [key, p] : state->Abilities) abilities[key] = { { "Remaining", p.Remaining }, { "Uses", p.Uses }, { "Unlocked", p.Unlocked } };
            return { { "Energy", state->Energy }, { "Equipped", equipment }, { "Abilities", abilities } };
        }
        std::optional<LoadoutState> ReadLoadout(const V& v)
        {
            if (v.is_null()) return {};
            Keys(v, { "Energy", "Equipped", "Abilities" });
            LoadoutState s;
            s.Energy = Number(v.at("Energy"), 0, 100000);
            auto& equipment = v.at("Equipped");
            Require(equipment.is_array() && equipment.size() == 4, "Saved equipment requires four slots");
            std::set<std::string> seen;
            for (size_t i = 0; i < 4; ++i)
            {
                auto key = Text(equipment[i], 64);
                if (!key.empty())
                {
                    Require(ValidItem(key) && seen.insert(key).second, "Invalid/duplicate equipped item");
                    s.Equipped.emplace(EquipSlot(i), key);
                }
            }
            auto& abilities = v.at("Abilities");
            Require(abilities.is_object() && abilities.size() <= 16, "Saved ability limit 16");
            for (auto i = abilities.begin(); i != abilities.end(); ++i)
            {
                Require(ValidItem(i.key()), "Invalid ability key");
                Keys(i.value(), { "Remaining", "Uses", "Unlocked" });
                s.Abilities.emplace(i.key(), AbilityProgress{ Number(i.value().at("Remaining"), 0, 300), Counter(i.value().at("Uses")), Bool(i.value().at("Unlocked")) });
            }
            return s;
        }
        V QuestBook(const QuestBookSave& b)
        {
            V entries = V::object();
            for (auto& [key, p] : b.Quests) entries[key] = { { "Mode", int(p.Mode) }, { "Counts", p.Counts }, { "Receipts", p.Receipts } };
            return { { "XP", b.XP }, { "Entries", entries } };
        }
        QuestBookSave QuestBook(const V& v)
        {
            Keys(v, { "XP", "Entries" });
            QuestBookSave b;
            b.XP = Counter(v.at("XP"));
            Require(b.XP <= 1000000000, "Saved quest XP limit");
            auto& entries = v.at("Entries");
            Require(entries.is_object() && entries.size() <= 16, "Saved quest limit 16");
            for (auto i = entries.begin(); i != entries.end(); ++i)
            {
                Require(ValidQuestKey(i.key()), "Invalid saved quest key");
                auto& item = i.value();
                Keys(item, { "Mode", "Counts", "Receipts" });
                QuestProgress p;
                auto mode = Counter(item.at("Mode"));
                Require(mode <= 4, "Invalid saved quest mode");
                p.Mode = QuestMode(mode);
                auto& counts = item.at("Counts");
                auto& receipts = item.at("Receipts");
                Require(counts.is_array() && !counts.empty() && counts.size() <= 8 && receipts.is_array() && receipts.size() <= 64, "Saved quest objective/receipt limits");
                for (auto& n : counts)
                {
                    auto count = Counter(n);
                    Require(count <= 1000000, "Saved quest count limit");
                    p.Counts.push_back(count);
                }
                for (auto& r : receipts)
                {
                    auto receipt = Text(r, 64);
                    Require(ValidQuestKey(receipt) && p.Receipts.insert(receipt).second, "Invalid/duplicate saved quest receipt");
                }
                if (p.Mode == QuestMode::Locked) Require(std::all_of(p.Counts.begin(), p.Counts.end(), [](auto n) { return n == 0; }) && p.Receipts.empty(), "Locked quest has progress");
                b.Quests.emplace(i.key(), std::move(p));
            }
            Require(!b.Quests.empty() || b.XP == 0, "Saved XP without quests");
            return b;
        }
        V AI(const AISnapshot& a)
        {
            auto& b = a.Brain;
            return { { "Mode", int(b.Mode) }, { "Memory", b.Remember }, { "Wait", b.WaitLeft }, { "Waypoint", b.Waypoint }, { "Arrivals", b.Arrivals }, { "Returns", b.Returns }, { "Direction", b.Direction }, { "Waiting", b.Waiting }, { "Home", Vec(a.Home) }, { "Goal", Vec(a.Goal) }, { "LastSeen", Vec(a.LastSeen) }, { "Facing", Vec(a.Facing) }, { "Enabled", a.Enabled }, { "Sightings", a.Sightings }, { "Losses", a.Losses }, { "Attacks", a.Attacks }, { "Kills", a.Kills }, { "Transitions", a.Transitions }, { "PathRequests", a.PathRequests } };
        }
        AISnapshot AI(const V& v)
        {
            Keys(v, { "Mode", "Memory", "Wait", "Waypoint", "Arrivals", "Returns", "Direction", "Waiting", "Home", "Goal", "LastSeen", "Facing", "Enabled", "Sightings", "Losses", "Attacks", "Kills", "Transitions", "PathRequests" });
            AISnapshot a;
            auto& b = a.Brain;
            auto mode = Counter(v.at("Mode"));
            Require(mode <= uint32_t(BrainMode::Disabled), "Invalid AI save mode");
            b.Mode = BrainMode(mode);
            b.Remember = float(Number(v.at("Memory"), 0, 60));
            b.WaitLeft = float(Number(v.at("Wait"), 0, 60));
            b.Waypoint = Counter(v.at("Waypoint"));
            Require(b.Waypoint < 64, "AI save waypoint limit");
            b.Arrivals = Counter(v.at("Arrivals"));
            b.Returns = Counter(v.at("Returns"));
            auto dir = Number(v.at("Direction"), -1, 1);
            Require(dir == -1 || dir == 1, "Invalid AI direction");
            b.Direction = int(dir);
            b.Waiting = Bool(v.at("Waiting"));
            a.Home = Vec(v.at("Home"), 1e9);
            a.Goal = Vec(v.at("Goal"), 1e9);
            a.LastSeen = Vec(v.at("LastSeen"), 1e9);
            a.Facing = Vec(v.at("Facing"), 1e9);
            Require(glm::length(a.Facing) > 1e-5f, "Invalid AI save facing");
            a.Enabled = Bool(v.at("Enabled"));
            a.Sightings = Counter(v.at("Sightings"));
            a.Losses = Counter(v.at("Losses"));
            a.Attacks = Counter(v.at("Attacks"));
            a.Kills = Counter(v.at("Kills"));
            a.Transitions = Counter(v.at("Transitions"));
            a.PathRequests = Counter(v.at("PathRequests"));
            return a;
        }
        V EntityValue(const SavedEntity& e, int version)
        {
            V items = V::object();
            for (auto& [key, count] : e.Inventory.Items) items[key] = count;
            V value = { { "Key", e.Key }, { "Signature", e.Signature }, { "Target", e.Target }, { "Present", e.Present }, { "Active", e.Active }, { "Actor", e.HasActor }, { "HasHealth", e.HasHealth }, { "Interaction", e.HasInteraction }, { "HasAI", e.HasAI }, { "Position", Vec(e.Position) }, { "Rotation", Vec(e.Rotation) }, { "Scale", Vec(e.Scale) }, { "Health", e.Health }, { "Shield", e.Shield }, { "Grace", e.Grace }, { "Respawn", e.Respawn }, { "Attack", e.Attack }, { "Cooldown", e.InteractionCooldown }, { "Deaths", e.Deaths }, { "Uses", e.Uses }, { "Dead", e.Dead }, { "Slots", e.Inventory.Slots }, { "Stack", e.Inventory.Stack }, { "Items", items }, { "AI", AI(e.AI) } };
            if (version >= 2) value["Quests"] = QuestBook(e.QuestBook);
            else Require(e.QuestBook.Quests.empty() && e.QuestBook.XP == 0, "v1 cannot encode quest progress");
            if (version >= 3) value["Loadout"] = LoadoutValue(e.Loadout);
            else Require(!e.Loadout, "Legacy save cannot encode loadout");
            if (version >= 4) value["Effects"] = EffectsValue(e.Effects);
            else Require(e.Effects.empty(), "Legacy save cannot encode status effects");
            return value;
        }
        SavedEntity EntityValue(const V& v, int version)
        {
            auto fields = v;
            if (version >= 2) fields.erase("Quests");
            if (version >= 3) fields.erase("Loadout");
            if (version >= 4) fields.erase("Effects");
            Keys(fields, { "Key", "Signature", "Target", "Present", "Active", "Actor", "HasHealth", "Interaction", "HasAI", "Position", "Rotation", "Scale", "Health", "Shield", "Grace", "Respawn", "Attack", "Cooldown", "Deaths", "Uses", "Dead", "Slots", "Stack", "Items", "AI" });
            SavedEntity e;
            e.Key = Text(v.at("Key"), 64);
            Require(ValidSaveKey(e.Key), "Invalid entity save key");
            e.Signature = Text(v.at("Signature"), 32768);
            e.Target = Text(v.at("Target"), 64);
            Require(e.Target.empty() || ValidSaveKey(e.Target), "Invalid target save key");
            e.Present = Bool(v.at("Present"));
            e.Active = Bool(v.at("Active"));
            e.HasActor = Bool(v.at("Actor"));
            e.HasHealth = Bool(v.at("HasHealth"));
            e.HasInteraction = Bool(v.at("Interaction"));
            e.HasAI = Bool(v.at("HasAI"));
            e.Position = Vec(v.at("Position"), 1e9);
            e.Rotation = Vec(v.at("Rotation"), 10000);
            e.Scale = Vec(v.at("Scale"), 10000);
            for (int i = 0; i < 3; ++i) Require(std::abs(e.Scale[i]) >= .0001f, "Singular save scale");
            e.Health = Number(v.at("Health"), 0, 1e6);
            e.Shield = Number(v.at("Shield"), 0, 1e6);
            e.Grace = Number(v.at("Grace"), 0, 60);
            e.Respawn = Number(v.at("Respawn"), 0, 60);
            e.Attack = Number(v.at("Attack"), 0, 60);
            e.InteractionCooldown = Number(v.at("Cooldown"), 0, 60);
            e.Deaths = Counter(v.at("Deaths"));
            e.Uses = Counter(v.at("Uses"));
            e.Dead = Bool(v.at("Dead"));
            auto slots = Counter(v.at("Slots")), stack = Counter(v.at("Stack"));
            Require(slots <= 64 && stack <= 999999, "Saved bag limits");
            std::string error;
            Require(e.Inventory.Configure(int(slots), int(stack), true, error), "Invalid saved inventory configuration");
            auto& items = v.at("Items");
            Require(items.is_object() && items.size() <= size_t(e.Inventory.Slots), "Saved inventory capacity");
            for (auto it = items.begin(); it != items.end(); ++it)
            {
                auto count = Counter(it.value());
                Require(ValidItem(it.key()) && count > 0 && count <= uint32_t(e.Inventory.Stack), "Invalid saved item");
                e.Inventory.Items[it.key()] = int(count);
            }
            e.AI = AI(v.at("AI"));
            if (version >= 2) e.QuestBook = QuestBook(v.at("Quests"));
            if (version >= 3) e.Loadout = ReadLoadout(v.at("Loadout"));
            if (version >= 4) e.Effects = ReadEffects(v.at("Effects"));
            Require(!e.Loadout || (e.Present && e.HasHealth), "Saved loadout requires present health actor");
            // Death clears every status effect: only present, living health actors carry them.
            Require(e.Effects.empty() || (e.Present && e.HasHealth && !e.Dead), "Saved effects require a living health actor");
            Require(e.Present || e.QuestBook.Quests.empty(), "Absent slot has quest progress");
            Require(!e.HasHealth || e.HasActor, "Health without saved actor");
            Require(!e.HasAI || e.HasHealth, "AI without saved health");
            Require(!e.HasHealth || (e.Dead ? e.Health == 0 : e.Health > 0), "Inconsistent saved death");
            Require(e.Present || (!e.HasActor && !e.HasInteraction && !e.HasAI && e.Target.empty()), "Invalid absent entity payload");
            Require(e.HasAI || e.Target.empty(), "Target without saved AI");
            return e;
        }
        GameSave Payload(const V& payload, int version)
        {
            if (version >= 4) Keys(payload, { "Scene", "Schema", "Entities", "Combat" });
            else Keys(payload, { "Scene", "Schema", "Entities" });
            GameSave result;
            result.Version = version;
            result.Scene = Text(payload.at("Scene"), 128);
            result.Schema = Text(payload.at("Schema"), 64);
            Require(!result.Scene.empty() && ValidSaveKey(result.Schema), "Invalid gameplay save identity");
            auto& entities = payload.at("Entities");
            Require(entities.is_array() && !entities.empty() && entities.size() <= 256, "Save entity limit 1..256");
            std::set<std::string> keys;
            for (auto& v : entities)
            {
                auto e = EntityValue(v, version);
                Require(keys.insert(e.Key).second, "Duplicate entity save key");
                result.Entities.push_back(std::move(e));
            }
            for (auto& e : result.Entities) Require(e.Target.empty() || keys.contains(e.Target), "Unbound saved AI target");
            if (version >= 4) result.Combat = ReadCombat(payload.at("Combat"));
            return result;
        }
    }
    bool ValidSaveKey(const std::string& key) { return !key.empty() && key.size() <= 64 && key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos; }
    bool ValidSaveReference(const std::string& reference)
    {
        if (reference.empty() || ValidSaveKey(reference)) return true;
        if (!reference.starts_with("uuid:") || reference.size() < 6 || reference.size() > 25) return false;
        const auto digits = std::string_view(reference).substr(5);
        if (digits.find_first_not_of("0123456789") != std::string_view::npos || (digits.size() > 1 && digits[0] == '0')) return false;
        // At most UINT64_MAX and never zero (0 names no entity).
        return digits != "0" && (digits.size() < 20 || digits <= "18446744073709551615");
    }
    std::string EncodeGameSave(const GameSave& save)
    {
        Require(save.Version >= 1 && save.Version <= 4, "Unsupported gameplay save version");
        Require(bool(save.Combat) == (save.Version >= 4), "Only save v4 encodes combat state");
        V entities = V::array();
        for (auto& e : save.Entities) entities.push_back(EntityValue(e, save.Version));
        V payload = { { "Scene", save.Scene }, { "Schema", save.Schema }, { "Entities", entities } };
        if (save.Combat) payload["Combat"] = CombatValue(*save.Combat);
        Payload(payload, save.Version);
        auto bytes = V{ { "Format", "Zamin.GameSave" }, { "Version", save.Version }, { "Payload", payload }, { "Checksum", Hash(payload.dump()) } }.dump();
        Require(bytes.size() <= Limit, "Gameplay save exceeds 2 MiB");
        return bytes;
    }
    bool DecodeGameSave(std::string_view bytes, GameSave& save, std::string& error)
    {
        error.clear();
        try
        {
            Require(bytes.size() <= Limit, "Gameplay save exceeds 2 MiB");
            std::vector<std::set<std::string>> keys;
            auto value = V::parse(bytes, [&](int depth, V::parse_event_t event, V& parsed) {Require(depth<=16,"Gameplay save nesting limit");if(event==V::parse_event_t::object_start)keys.emplace_back();if(event==V::parse_event_t::key)Require(!keys.empty()&&keys.back().insert(parsed.get<std::string>()).second,"Duplicate JSON save key");if(event==V::parse_event_t::object_end)keys.pop_back();return true; });
            Keys(value, { "Format", "Version", "Payload", "Checksum" });
            Require(value.at("Format") == "Zamin.GameSave" && (value.at("Version") == 1 || value.at("Version") == 2 || value.at("Version") == 3 || value.at("Version") == 4), "Unsupported gameplay save format/version");
            Require(Text(value.at("Checksum"), 20) == Hash(value.at("Payload").dump()), "Gameplay save checksum mismatch");
            auto next = Payload(value.at("Payload"), value.at("Version").get<int>());
            save = std::move(next);
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
}
