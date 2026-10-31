#pragma once

#include "Shalltear/Core/Export.h"

#ifdef SHALLTEAR_SHARED
#ifdef SHALLTEAR_GRAPHS_BUILD
#define SHALLTEAR_GRAPHS_API SLT_API_EXPORT
#else
#define SHALLTEAR_GRAPHS_API SLT_API_IMPORT
#endif
#else
#define SHALLTEAR_GRAPHS_API
#endif
