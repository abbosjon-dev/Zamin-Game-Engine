#pragma once

// miniaudio with the engine's configuration: every translation unit that includes it must see the same macros
// (they change which declarations - and struct members - exist). The implementation is compiled once, in
// MiniaudioImpl.cpp.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>
