#pragma once

#include "Shalltear/Core/Export.h"

#ifdef SHALLTEAR_SHARED
#ifdef SHALLTEAR_AUDIO_BUILD
#define SHALLTEAR_AUDIO_API SLT_API_EXPORT
#else
#define SHALLTEAR_AUDIO_API SLT_API_IMPORT
#endif
#else
#define SHALLTEAR_AUDIO_API
#endif
