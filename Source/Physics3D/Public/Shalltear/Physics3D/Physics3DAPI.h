#pragma once

#include "Shalltear/Core/Export.h"

#ifdef SHALLTEAR_SHARED
#ifdef SHALLTEAR_PHYSICS3D_BUILD
#define SHALLTEAR_PHYSICS3D_API SLT_API_EXPORT
#else
#define SHALLTEAR_PHYSICS3D_API SLT_API_IMPORT
#endif
#else
#define SHALLTEAR_PHYSICS3D_API
#endif
