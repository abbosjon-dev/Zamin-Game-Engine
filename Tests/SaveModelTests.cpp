#include "TestAssertions.h"
#include <Zamin/Gameplay/SaveModel.h>
#include <limits>
using namespace Zamin::Gameplay;
namespace {
    GameSave Example()
    {
        GameSave save{ "SaveLab", "V1", {} };
        SavedEntity hero;
        hero.Key = "Player";
        hero.Signature = "authored";
        hero.HasActor = hero.HasHealth = true;
        hero.Health = 73;
        hero.Shield = 4;
        hero.Grace = .25;
        hero.Inventory.Items["Key"] = 2;
        SavedEntity npc;
        npc.Key = "NPC";
        npc.Signature = "brain";
        npc.HasActor = npc.HasHealth = npc.HasAI = true;
        npc.Health = 30;
        npc.Target = "Player";
        npc.AI.Brain.Mode = BrainMode::Chase;
        npc.AI.Home = { -3, 0, 0 };
        npc.AI.Brain.Arrivals = 4;
        save.Entities = { hero, npc };
        return save;
    }
    bool Bad(GameSave save)
    {
        try
        {
            EncodeGameSave(save);
            return false;
        }
        catch (const std::exception&)
        {
            return true;
        }
    }
}
TEST(GameSaveModelRoundtripPreservesHealthItemsAndAI)
{
    auto save = Example();
    std::string error;
    GameSave copy;
    CHECK(DecodeGameSave(EncodeGameSave(save), copy, error));
    CHECK(copy.Entities.size() == 2);
    CHECK(copy.Entities[0].Health == 73);
    CHECK(copy.Entities[0].Inventory.Count("Key") == 2);
    CHECK(copy.Entities[1].Target == "Player");
    CHECK(copy.Entities[1].AI.Brain.Mode == BrainMode::Chase);
    CHECK(copy.Entities[1].AI.Brain.Arrivals == 4);
}
TEST(GameSaveModelEncodingIsDeterministic)
{
    auto save = Example();
    auto bytes = EncodeGameSave(save);
    GameSave copy;
    std::string error;
    CHECK(DecodeGameSave(bytes, copy, error));
    CHECK(bytes == EncodeGameSave(copy));
}
TEST(GameSaveModelCorruptionChecksumAndTruncationAreRejectedTransactionally)
{
    auto bytes = EncodeGameSave(Example());
    auto copy = Example();
    copy.Schema = "Sentinel";
    std::string error;
    auto pos = bytes.find("authored");
    bytes[pos] = 'X';
    CHECK(!DecodeGameSave(bytes, copy, error));
    CHECK(error.find("checksum") != std::string::npos);
    CHECK(copy.Schema == "Sentinel");
    CHECK(!DecodeGameSave(bytes.substr(0, bytes.size() / 2), copy, error));
    CHECK(copy.Schema == "Sentinel");
}
TEST(GameSaveModelUnknownVersionAndFormatAreRejected)
{
    auto value = Zamin::Visual::Value::parse(EncodeGameSave(Example()));
    GameSave out;
    std::string error;
    value["Version"] = 2;
    CHECK(!DecodeGameSave(value.dump(), out, error));
    value["Version"] = 1;
    value["Format"] = "Other";
    CHECK(!DecodeGameSave(value.dump(), out, error));
}
TEST(GameSaveModelDuplicateJSONKeysAreRejected)
{
    GameSave out;
    std::string error;
    CHECK(!DecodeGameSave("{\"Version\":1,\"Version\":1}", out, error));
    CHECK(error.find("Duplicate JSON") != std::string::npos);
}
TEST(GameSaveModelDepthAndByteLimitsAreBounded)
{
    GameSave out;
    std::string error;
    CHECK(!DecodeGameSave(std::string(2 * 1024 * 1024 + 1, ' '), out, error));
    CHECK(error.find("2 MiB") != std::string::npos);
    CHECK(!DecodeGameSave(std::string(20, '[') + "0" + std::string(20, ']'), out, error));
    CHECK(error.find("nesting") != std::string::npos);
}
TEST(GameSaveModelRejectsUnsafeKeysAndDuplicateBindings)
{
    auto save = Example();
    save.Entities[0].Key = "../escape";
    CHECK(Bad(save));
    save = Example();
    save.Entities[1].Key = "Player";
    CHECK(Bad(save));
    save = Example();
    save.Schema = "a/b";
    CHECK(Bad(save));
}
TEST(GameSaveModelEntityCountAndIdentityLimits)
{
    auto save = Example();
    save.Entities.clear();
    CHECK(Bad(save));
    save = Example();
    save.Scene = std::string(129, 'x');
    CHECK(Bad(save));
    save = Example();
    for (int i = 0; i < 255; ++i)
    {
        SavedEntity e;
        e.Key = "Item" + std::to_string(i);
        save.Entities.push_back(e);
    }
    CHECK(Bad(save));
}
TEST(GameSaveModelAbsentTombstoneIsValidButCannotHideActorPayload)
{
    auto save = Example();
    SavedEntity dead;
    dead.Key = "Gone";
    dead.Present = false;
    save.Entities.push_back(dead);
    CHECK(!Bad(save));
    save.Entities.back().HasActor = true;
    CHECK(Bad(save));
}
TEST(GameSaveModelHealthDeathConsistencyAndFiniteLimits)
{
    auto save = Example();
    save.Entities[0].Health = 0;
    CHECK(Bad(save));
    save.Entities[0].Dead = true;
    CHECK(!Bad(save));
    save.Entities[0].Shield = -1;
    CHECK(Bad(save));
    save = Example();
    save.Entities[0].Grace = std::numeric_limits<double>::infinity();
    CHECK(Bad(save));
}
TEST(GameSaveModelInventoryRejectsOverflowInvalidItemsAndZeroStacks)
{
    auto save = Example();
    save.Entities[0].Inventory.Items["Key"] = 0;
    CHECK(Bad(save));
    save = Example();
    save.Entities[0].Inventory.Items["Bad/Key"] = 1;
    CHECK(Bad(save));
    save = Example();
    save.Entities[0].Inventory.Items["Key"] = 1000;
    CHECK(Bad(save));
    save = Example();
    save.Entities[0].Inventory.Slots = 65;
    CHECK(Bad(save));
}
TEST(GameSaveModelSingularTransformsAndNaNAreRejected)
{
    auto save = Example();
    save.Entities[0].Scale.x = 0;
    CHECK(Bad(save));
    save = Example();
    save.Entities[0].Position.z = std::numeric_limits<float>::quiet_NaN();
    CHECK(Bad(save));
    save = Example();
    save.Entities[0].Position.x = 2e9;
    CHECK(Bad(save));
}
TEST(GameSaveModelAITargetMustBelongToTheSnapshot)
{
    auto save = Example();
    save.Entities[1].Target = "Missing";
    CHECK(Bad(save));
    save = Example();
    save.Entities[0].Target = "NPC";
    CHECK(Bad(save));
}
TEST(GameSaveModelAIStateAndCounterRangesAreChecked)
{
    auto save = Example();
    save.Entities[1].AI.Brain.Direction = 0;
    CHECK(Bad(save));
    save = Example();
    save.Entities[1].AI.Brain.Waypoint = 64;
    CHECK(Bad(save));
    save = Example();
    save.Entities[1].AI.Brain.Mode = BrainMode(99);
    CHECK(Bad(save));
    save = Example();
    save.Entities[1].AI.Facing = {};
    CHECK(Bad(save));
}
