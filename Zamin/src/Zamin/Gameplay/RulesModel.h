#pragma once
#include <cstdint>
#include <map>
#include <string>
namespace Zamin::Gameplay {
    struct HealthSettings
    {
        double Maximum = 100, Shield = 0, HitGrace = .2, RespawnDelay = .5;
        uint16_t Team = 0;
    };
    struct HealthState
    {
        HealthSettings Settings;
        double Current = 100, Shield = 0, ImmuneUntil = 0, DiedAt = 0;
        uint32_t Deaths = 0;
        bool Dead = false;
    };
    struct DamageResult
    {
        double Applied = 0, Health = 0, Shield = 0;
        bool Killed = false;
        std::string Reason;
    };
    bool ConfigureHealth(HealthState&, const HealthSettings&, std::string& error);
    bool DamageHealth(HealthState&, double amount, double now, bool bypassShield, bool bypassGrace, DamageResult&, std::string& error);
    double HealHealth(HealthState&, double amount, bool shield);
    bool ReviveHealth(HealthState&, double now, double grace, std::string& error);
    bool ValidItem(const std::string&);
    struct Bag
    {
        std::map<std::string, int> Items;
        int Slots = 32, Stack = 999;
        int Count(const std::string&) const;
        bool Configure(int slots, int stack, bool reset, std::string& error);
        // One transaction: require/optionally consume a key and award a reward. No partial mutation.
        bool Exchange(const std::string& required, int requiredCount, bool consume,
                      const std::string& reward, int rewardCount, std::string& error);
    };
}
