#pragma once

#if defined(NDEBUG)
#define VE_BUILD_DEBUG 0
#define VE_BUILD_RELEASE 1
#else
#define VE_BUILD_DEBUG 1
#define VE_BUILD_RELEASE 0
#endif
