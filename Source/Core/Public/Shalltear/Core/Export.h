#pragma once

#include "Shalltear/Core/Platform.h"

#if defined(SLT_PLATFORM_WINDOWS)
#define SLT_API_EXPORT __declspec(dllexport)
#define SLT_API_IMPORT __declspec(dllimport)
#else
#define SLT_API_EXPORT __attribute__((visibility("default")))
#define SLT_API_IMPORT
#endif
