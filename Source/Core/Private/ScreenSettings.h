#pragma once

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Screen.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct PlayerSettings {
		SLT_REFLECT_VALUE(PlayerSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_SETTINGS_CATEGORY("Player")
		SLT_REFLECT_FIELD(fullScreenMode, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(defaultIsNativeResolution, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(defaultScreenWidth, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(defaultScreenHeight, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(resizableWindow, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(allowFullscreenSwitch, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_END_REFLECT()

		FullScreenMode fullScreenMode = FullScreenMode::FullScreenWindow;
		bool defaultIsNativeResolution = true;
		i32 defaultScreenWidth = 1024;
		i32 defaultScreenHeight = 768;
		bool resizableWindow = false;
		bool allowFullscreenSwitch = true;
	};

	struct ScreenSettings {
		SLT_REFLECT_VALUE(ScreenSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_SETTINGS_CATEGORY("Screen")
		SLT_REFLECT_FIELD(saved, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(fullScreenMode, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(nativeResolution, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(width, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(height, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(hasPosition, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(positionX, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(positionY, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_REFLECT_FIELD(projectDefaults, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_END_REFLECT()

		bool saved = false;
		FullScreenMode fullScreenMode = FullScreenMode::FullScreenWindow;
		bool nativeResolution = true;
		i32 width = 0;
		i32 height = 0;
		bool hasPosition = false;
		i32 positionX = 0;
		i32 positionY = 0;
		str projectDefaults;
	};

} // namespace Shalltear
