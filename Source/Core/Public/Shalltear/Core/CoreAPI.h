#pragma once

#include "Shalltear/Core/Export.h"

#ifdef SHALLTEAR_SHARED
#ifdef SHALLTEAR_CORE_BUILD
#define SHALLTEAR_CORE_API SLT_API_EXPORT
#else
#define SHALLTEAR_CORE_API SLT_API_IMPORT
#endif
#else
#define SHALLTEAR_CORE_API
#endif
