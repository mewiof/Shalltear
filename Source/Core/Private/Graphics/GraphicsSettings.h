#pragma once

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class VSyncMode : u8 {
		WindowHint = 0,
		Off,
		On
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::VSyncMode)
SLT_ENUM_VALUE(WindowHint)
SLT_ENUM_VALUE(Off)
SLT_ENUM_VALUE(On)
SLT_END_ENUM()

namespace Shalltear {

	struct GraphicsSettings {
		SLT_REFLECT_VALUE(GraphicsSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_SETTINGS_CATEGORY("Graphics")
		SLT_REFLECT_FIELD(vsync, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))
		SLT_REFLECT_FIELD(adapter, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(validation, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(physicalLightUnits, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(qualityProfiles, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary), SLT_ASSET_REFS)
		SLT_REFLECT_FIELD(qualityLevel, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))
		SLT_END_REFLECT()

		VSyncMode vsync = VSyncMode::WindowHint;

		i32 adapter = -1;

#if defined(SHALLTEAR_CONFIG_RELEASE)
		bool validation = false;
#else
		bool validation = true;
#endif

		// lux for directional, lumens for point/spot
		bool physicalLightUnits = false;

		// semicolon separated guids
		str qualityProfiles;

		i32 qualityLevel = -1;
	};

} // namespace Shalltear
