// VisualWorkshop's native methods: C++ that the graph Scripts/NativeMethods.zgraph calls through Native.Call /
// Native.Pure nodes (docs/VisualScripting.md, "Native methods"). The rest of the sample is graphs only.
#include <Zamin/Zamin.h>

#include <cmath>
#include <string>

using namespace Zamin;

// The medal a score earns; graphs see the enum Medal with the same entries.
enum class Medal { Bronze, Silver, Gold };
ZM_SCRIPT_ENUM(Medal, "Bronze", "Silver", "Gold")

// The script on the "Workshop stats" entity. Bonus is an inspector field; the graph calls the methods.
class WorkshopStats : public ScriptableEntity
{
public:
    float Bonus = 1.5f;
    int Awards = 0;

    // An execution method: counts an award and returns the new count.
    int Award(const std::string& reason)
    {
        ++Awards;
        ZM_INFO("WorkshopStats: award {} ({})", Awards, reason);
        return Awards;
    }
    // Pure methods (data nodes): the score with the bonus in whole points, and its medal.
    float Score(float base, int multiplier) const { return std::round(base * float(multiplier) * Bonus); }
    Medal MedalFor(float score) const { return score >= 50.0f ? Medal::Gold : score >= 30.0f ? Medal::Silver : Medal::Bronze; }
};

ZM_REGISTER_SCRIPT(WorkshopStats,
    ZM_FIELD_RANGE(WorkshopStats, Bonus, 0, 10),
    ZM_METHOD(WorkshopStats, Award, "Reason").Returns("Awards").Tooltip("Counts an award; returns the number of awards"),
    ZM_PURE_METHOD(WorkshopStats, Score, "Base", "Multiplier").Tooltip("Base x Multiplier x Bonus, rounded"),
    ZM_PURE_METHOD(WorkshopStats, MedalFor, "Score").Tooltip("Gold from 50, Silver from 30"))

// A free function: graphs call it without a Target entity.
static float Hypot(float x, float y) { return std::sqrt(x * x + y * y); }

ZM_REGISTER_FUNCTIONS(WorkshopMath, ZM_PURE_FUNCTION(Hypot, "X", "Y").Tooltip("The length of (X, Y)"))
