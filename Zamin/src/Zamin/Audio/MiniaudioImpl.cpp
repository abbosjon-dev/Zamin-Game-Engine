// miniaudio's implementation (third-party code) in a translation unit of its own. A sanitizer build compiles it
// like the vendor libraries (Zamin/CMakeLists.txt): its UndefinedBehaviorSanitizer reports stay recoverable, so
// Tests/sanitizers/ubsan.supp can list miniaudio's own with the reason, while every report in the engine's code
// stays fatal.
#define MINIAUDIO_IMPLEMENTATION
#include "Zamin/Audio/Miniaudio.h"
