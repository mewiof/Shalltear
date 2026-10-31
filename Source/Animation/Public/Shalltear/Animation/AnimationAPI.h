#pragma once

#include "Shalltear/Core/Export.h"

#ifdef SHALLTEAR_SHARED
#ifdef SHALLTEAR_ANIMATION_BUILD
#define SHALLTEAR_ANIMATION_API SLT_API_EXPORT
#else
#define SHALLTEAR_ANIMATION_API SLT_API_IMPORT
#endif
#else
#define SHALLTEAR_ANIMATION_API
#endif
