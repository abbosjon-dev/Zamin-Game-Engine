#include <Zamin/Scripting/ScriptableEntity.h>
#include <Zamin/Scripting/ScriptEngine.h>
#include <cstdio>
#include <cstdlib>
#ifndef PROBE_VALUE
    #define PROBE_VALUE 17
#endif
namespace {
    void Trace(const char* action)
    {
        if (auto* path = std::getenv("ZAMIN_ABI_CONSTRUCTOR_TRACE"))
            if (auto* f = std::fopen(path, "ab"))
            {
                std::fprintf(f, "%s:%d\n", action, PROBE_VALUE);
                std::fclose(f);
            }
    }
    struct TraceLifetime
    {
        TraceLifetime() { Trace("ctor"); }
        ~TraceLifetime() { Trace("dtor"); }
    } trace;
}
class ABIProbe : public Zamin::ScriptableEntity
{
public:
    float Value = PROBE_VALUE;
};
ZM_REGISTER_SCRIPT(ABIProbe, ZM_FIELD(ABIProbe, Value))
#ifdef PROBE_UNRESOLVED
extern "C" int ZaminABIMissingDependency();
extern "C" int ZaminABIUnresolvedCall() { return ZaminABIMissingDependency(); }
#endif
