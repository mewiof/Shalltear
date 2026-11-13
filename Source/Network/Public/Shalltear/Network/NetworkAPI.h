#pragma once

#include "Shalltear/Core/Export.h"

#ifdef SHALLTEAR_SHARED
#ifdef SHALLTEAR_NETWORK_BUILD
#define SHALLTEAR_NETWORK_API SLT_API_EXPORT
#else
#define SHALLTEAR_NETWORK_API SLT_API_IMPORT
#endif
#else
#define SHALLTEAR_NETWORK_API
#endif
