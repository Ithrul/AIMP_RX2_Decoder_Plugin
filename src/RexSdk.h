#pragma once

// Force the expected platform and loader flags for the REX SDK even when
// IntelliSense or build tooling miss the CMake definitions.
#ifndef REX_WINDOWS
#define REX_WINDOWS 1
#endif

#ifndef REX_DLL_LOADER
#define REX_DLL_LOADER 1
#endif

#include "REX.h"
