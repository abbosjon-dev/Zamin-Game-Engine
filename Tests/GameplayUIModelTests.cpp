#include "TestAssertions.h"
#include <Zamin/Gameplay/UIModel.h>
#include <limits>
using namespace Zamin::Gameplay;
TEST(GameUIModelStackPoliciesRestoreAfterNestedPop)
{
    MenuStack s;
    std::string error;
    CHECK(s.Push({ "Bag", false, true }, error));
    CHECK(!s.Pauses() && s.Blocks());
    CHECK(s.Push({ "Pause", true, true }, error));
    CHECK(s.Top() == "Pause" && s.Pauses());
    CHECK(s.Pop()->Name == "Pause");
    CHECK(s.Top() == "Bag" && !s.Pauses());
    CHECK(s.Pop()->Name == "Bag");
    CHECK(!s.Blocks() && !s.Pop());
}
TEST(GameUIModelDuplicateAndInvalidPushDoNotMutateStack)
{
    MenuStack s;
    std::string error;
    CHECK(s.Push({ "Menu" }, error));
    CHECK(!s.Push({ "Menu" }, error));
    CHECK(!s.Push({ "../Bad" }, error));
    CHECK(s.Entries().size() == 1 && s.Top() == "Menu");
}
TEST(GameUIModelStackDepthIsEightAndRemovalPreservesOrder)
{
    MenuStack s;
    std::string error;
    for (int i = 0; i < 8; ++i) CHECK(s.Push({ "Panel" + std::to_string(i), false, false }, error));
    CHECK(!s.Push({ "Overflow" }, error));
    CHECK(s.Remove("Panel3"));
    CHECK(s.Top() == "Panel7");
    CHECK(!s.Remove("Missing"));
    CHECK(s.Push({ "New", true, true }, error));
    CHECK(s.Pauses() && s.Blocks());
    s.Clear();
    CHECK(s.Entries().empty());
}
TEST(GameUIModelInventoryPagesAreSortedAndClampBothEnds)
{
    Bag b;
    b.Items = { { "Sword", 1 }, { "Apple", 2 }, { "Key", 3 }, { "Coin", 4 }, { "Shield", 5 } };
    auto p = PresentInventory(b, 0, 2);
    CHECK(p.Pages == 3 && p.Items[0].Key == "Apple" && p.Items[1].Key == "Coin");
    p = PresentInventory(b, 99, 2);
    CHECK(p.Page == 2 && p.Items.size() == 1 && p.Items[0].Key == "Sword");
    CHECK(PresentInventory(b, -5, 2).Page == 0);
}
TEST(GameUIModelEmptyBagHasOneEmptyPage)
{
    auto p = PresentInventory({}, 17, 4);
    CHECK(p.Page == 0 && p.Pages == 1 && p.Items.empty());
}
TEST(GameUIModelInventoryPageReflowsWhenContentsShrink)
{
    Bag b;
    for (int i = 0; i < 64; ++i) b.Items["Item" + std::to_string(i)] = 1;
    b.Slots = 64;
    auto p = PresentInventory(b, 15, 4);
    CHECK(p.Page == 15 && p.Pages == 16);
    b.Items = { { "Only", 1 } };
    p = PresentInventory(b, p.Page, 4);
    CHECK(p.Page == 0 && p.Pages == 1 && p.Items[0].Key == "Only");
}
TEST(GameUIModelRejectsInvalidRowsBagCapacityAndCounts)
{
    auto bad = [](Bag b, int rows) {try{PresentInventory(b,0,rows);return false;}catch(const std::exception&){return true;} };
    CHECK(bad({}, 0));
    CHECK(bad({}, 17));
    Bag b;
    b.Items["Coin"] = 0;
    CHECK(bad(b, 4));
    b.Items["Coin"] = 1000;
    CHECK(bad(b, 4));
    b.Items = { { "Bad/Item", 1 } };
    CHECK(bad(b, 4));
    b.Items = { { "A", 1 }, { "B", 1 } };
    b.Slots = 1;
    CHECK(bad(b, 4));
}
TEST(GameUIModelHUDFractionAndRatioUseCanonicalValues)
{
    auto p = PresentHUD(75, 100, "HP ", true);
    CHECK(p.Text == "HP 75 / 100");
    CHECK_NEAR(p.Fraction, .75f, 1e-6f);
    CHECK(PresentHUD(120, 100, "", false).Fraction == 1);
    CHECK(PresentHUD(0, 0, "Shield ", true).Fraction == 0);
}
TEST(GameUIModelHUDFormatsCountersWithoutScientificNotation)
{
    CHECK(PresentHUD(4294967295., 4294967295., "Deaths ", false).Text == "Deaths 4294967295");
    CHECK(PresentHUD(2.6, 10, "", false).Text == "3");
}
TEST(GameUIModelHUDRejectsNonfiniteNegativeAndOversizedPrefix)
{
    auto bad = [](double a, double b, std::string s) {try{PresentHUD(a,b,s,true);return false;}catch(const std::exception&){return true;} };
    CHECK(bad(-1, 100, ""));
    CHECK(bad(1, -1, ""));
    CHECK(bad(std::numeric_limits<double>::quiet_NaN(), 100, ""));
    CHECK(bad(1, std::numeric_limits<double>::infinity(), ""));
    CHECK(bad(1, 2, std::string(65, 'x')));
}
