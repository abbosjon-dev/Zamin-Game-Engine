// A second compiled file of the ZaminABIStale probe module, stamped as if it had been compiled against other engine
// headers (Tests/CMakeLists.txt overrides ZAMIN_MODULE_TU_STAMP for this file): an object file that was not rebuilt
// after an engine header change. The engine must refuse the module before any of its constructors run.
#include <Zamin/Scripting/ScriptableEntity.h>
int ZaminABIStaleHelper() { return 3; }
