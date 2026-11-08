// The hot reload test's script (Tests/hot_reload_test.sh). The editor builds it as version 1 and plays the
// scene; the test then changes the next line to version 2 and builds the module again while the editor runs.
// The editor reloads it and plays again: the new code runs (it moves along Y, it has a new field) with the
// values the scene gave the fields before (Speed 7.5, Label "saqlangan", Count 3).
#define HOT_RELOAD_VERSION 1

#include <Zamin/Zamin.h>

#include <string>

using namespace Zamin;

class HotReloadProbe : public ScriptableEntity
{
public:
    float Speed = 1.0f;             // the scene: 7.5
    std::string Label = "standart"; // the scene: "saqlangan"
    int Count = 0;                  // the scene: 3
#if HOT_RELOAD_VERSION >= 2
    int Extra = 42;                 // new in version 2: not in the scene, its default
#endif

    void OnCreate() override
    {
#if HOT_RELOAD_VERSION >= 2
        ZM_INFO("HotReloadProbe v{}: Speed={} Label={} Count={} Extra={}", HOT_RELOAD_VERSION, Speed, Label, Count, Extra);
#else
        ZM_INFO("HotReloadProbe v{}: Speed={} Label={} Count={}", HOT_RELOAD_VERSION, Speed, Label, Count);
#endif
    }

    void OnUpdate(Timestep ts) override
    {
        // Version 1 moves along X, version 2 along Y.
        glm::vec3& position = GetComponent<TransformComponent>().Translation;
        if (HOT_RELOAD_VERSION >= 2)
            position.y += Speed * ts;
        else
            position.x += Speed * ts;
        if (++m_Frames == 20)
            ZM_INFO("HotReloadProbe v{}: 20 kadr, pozitsiya ({:.2f}, {:.2f})", HOT_RELOAD_VERSION, position.x, position.y);
    }

private:
    int m_Frames = 0;
};

#if HOT_RELOAD_VERSION >= 2
ZM_REGISTER_SCRIPT(HotReloadProbe, ZM_FIELD(HotReloadProbe, Speed), ZM_FIELD(HotReloadProbe, Label), ZM_FIELD(HotReloadProbe, Count),
                   ZM_FIELD(HotReloadProbe, Extra))
#else
ZM_REGISTER_SCRIPT(HotReloadProbe, ZM_FIELD(HotReloadProbe, Speed), ZM_FIELD(HotReloadProbe, Label), ZM_FIELD(HotReloadProbe, Count))
#endif
