#include "TestAssertions.h"
#include <Zamin/Gameplay/QuestModel.h>
using namespace Zamin::Gameplay;
namespace {
    QuestSpec Spec()
    {
        QuestSpec s;
        s.Objectives = { { "Visit", "Visit checkpoint", "", QuestKind::Manual, 2 }, { "Runes", "Recover runes", "Rune", QuestKind::Collect, 3 } };
        return s;
    }
}
TEST(QuestModelDefinitionLimitsAndUniqueObjectiveKeys)
{
    auto s = Spec();
    std::string e;
    CHECK(ValidateQuest(s, e));
    s.Objectives[1].Key = "Visit";
    CHECK(!ValidateQuest(s, e));
    s = Spec();
    s.Objectives[1].Goal = 0;
    CHECK(!ValidateQuest(s, e));
    s = Spec();
    s.Key = "../bad";
    CHECK(!ValidateQuest(s, e));
    s = Spec();
    s.Rewards["Coin"] = 0;
    CHECK(!ValidateQuest(s, e));
}
TEST(QuestModelStartIsExplicitAndCannotRestart)
{
    auto s = Spec();
    auto p = NewQuestProgress(s);
    std::string e;
    CHECK(p.Mode == QuestMode::Locked);
    CHECK(!AdvanceQuest(s, p, "Visit", 1, "Zone1", true, e));
    CHECK(StartQuest(s, p, e));
    CHECK(!StartQuest(s, p, e));
    CHECK(p.Mode == QuestMode::Active);
}
TEST(QuestModelManualReceiptIsDeduplicatedWithoutMutation)
{
    auto s = Spec();
    auto p = NewQuestProgress(s);
    std::string e;
    CHECK(StartQuest(s, p, e));
    CHECK(AdvanceQuest(s, p, "Visit", 1, "Zone1", true, e));
    CHECK(!AdvanceQuest(s, p, "Visit", 1, "Zone1", true, e));
    CHECK(p.Counts[0] == 1 && p.Receipts.size() == 1);
    CHECK(AdvanceQuest(s, p, "Visit", 99, "Zone2", true, e));
    CHECK(p.Counts[0] == 2);
}
TEST(QuestModelManualCannotAdvanceAutomaticObjectives)
{
    auto s = Spec();
    auto p = NewQuestProgress(s);
    std::string e;
    CHECK(StartQuest(s, p, e));
    CHECK(!AdvanceQuest(s, p, "Runes", 1, "Loot1", true, e));
    CHECK(p.Counts[1] == 0 && p.Receipts.empty());
}
TEST(QuestModelReadyRequiresAllObjectivesAndClampsCounts)
{
    auto s = Spec();
    auto p = NewQuestProgress(s);
    std::string e;
    CHECK(StartQuest(s, p, e));
    CHECK(AdvanceQuest(s, p, "Visit", 2, "Both", true, e));
    CHECK(p.Mode == QuestMode::Active);
    CHECK(AdvanceQuest(s, p, "Runes", 999, "", false, e));
    CHECK(p.Counts[1] == 3 && p.Mode == QuestMode::Ready);
    CHECK(!AdvanceQuest(s, p, "Runes", 1, "", false, e));
}
TEST(QuestModelFractionWeightsObjectivesEqually)
{
    auto s = Spec();
    auto p = NewQuestProgress(s);
    std::string e;
    CHECK(StartQuest(s, p, e));
    CHECK(AdvanceQuest(s, p, "Visit", 1, "Zone1", true, e));
    CHECK_NEAR(QuestFraction(s, p), .25f, 1e-6);
    CHECK(AdvanceQuest(s, p, "Runes", 3, "", false, e));
    CHECK_NEAR(QuestFraction(s, p), .75f, 1e-6);
}
TEST(QuestModelFailureIsTerminalAndKeepsEvidence)
{
    auto s = Spec();
    auto p = NewQuestProgress(s);
    std::string e;
    CHECK(!FailQuest(p, e));
    CHECK(StartQuest(s, p, e));
    CHECK(AdvanceQuest(s, p, "Visit", 1, "Zone1", true, e));
    CHECK(FailQuest(p, e));
    CHECK(p.Mode == QuestMode::Failed && p.Counts[0] == 1);
    CHECK(!StartQuest(s, p, e));
    CHECK(!AdvanceQuest(s, p, "Visit", 1, "Zone2", true, e));
}
TEST(QuestModelReceiptCapacityIsBoundedAndTransactional)
{
    QuestSpec s;
    s.Objectives = { { "Visit", "Visits", "", QuestKind::Manual, 100 } };
    auto p = NewQuestProgress(s);
    std::string e;
    CHECK(StartQuest(s, p, e));
    for (int i = 0; i < 64; ++i) CHECK(AdvanceQuest(s, p, "Visit", 1, "Zone" + std::to_string(i), true, e));
    CHECK(!AdvanceQuest(s, p, "Visit", 1, "Overflow", true, e));
    CHECK(p.Counts[0] == 64 && p.Receipts.size() == 64);
}
TEST(QuestModelForgedModesCountsAndReceiptsAreRejected)
{
    auto s = Spec();
    auto p = NewQuestProgress(s);
    std::string e;
    p.Counts[0] = 1;
    CHECK(!ValidateQuestProgress(s, p, e));
    p = NewQuestProgress(s);
    p.Mode = QuestMode::Claimed;
    CHECK(!ValidateQuestProgress(s, p, e));
    p.Mode = QuestMode::Active;
    p.Counts[0] = 3;
    CHECK(!ValidateQuestProgress(s, p, e));
    p = NewQuestProgress(s);
    p.Receipts.insert("bad/receipt");
    CHECK(!ValidateQuestProgress(s, p, e));
}
TEST(QuestModelUnknownObjectiveAndInvalidAmountCannotConsumeReceipt)
{
    auto s = Spec();
    auto p = NewQuestProgress(s);
    std::string e;
    CHECK(StartQuest(s, p, e));
    CHECK(!AdvanceQuest(s, p, "Missing", 1, "Receipt", true, e));
    CHECK(!AdvanceQuest(s, p, "Visit", 0, "Receipt", true, e));
    CHECK(!AdvanceQuest(s, p, "Visit", 1000001, "Receipt", true, e));
    CHECK(p.Receipts.empty() && p.Counts[0] == 0);
}
TEST(QuestModelPrerequisiteDefinitionValidationRejectsDuplicatesAndSelf)
{
    auto s = Spec();
    std::string e;
    s.Requires = { s.Key };
    CHECK(!ValidateQuest(s, e));
    s.Requires = { "First", "First" };
    CHECK(!ValidateQuest(s, e));
    s.Requires = { "First" };
    CHECK(ValidateQuest(s, e));
}
TEST(QuestModelLevelCurveBoundariesAndCapAreDeterministic)
{
    CHECK(QuestLevel(0).Level == 1);
    CHECK(QuestLevel(99).Current == 99);
    CHECK(QuestLevel(100).Level == 2);
    CHECK(QuestLevel(299).Level == 2);
    CHECK(QuestLevel(300).Level == 3);
    CHECK(QuestLevel(495000).Level == 100);
    CHECK(QuestLevel(1000000000).Needed == 0);
    CHECK(QuestLevel(1000000000).Fraction == 1);
}
