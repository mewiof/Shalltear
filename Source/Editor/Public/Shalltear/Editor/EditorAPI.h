#pragma once

#include "Shalltear/Core/Export.h"

#ifdef SHALLTEAR_SHARED
#ifdef SHALLTEAR_EDITOR_BUILD
#define SHALLTEAR_EDITOR_API SLT_API_EXPORT
#else
#define SHALLTEAR_EDITOR_API SLT_API_IMPORT
#endif
#else
#define SHALLTEAR_EDITOR_API
#endif
