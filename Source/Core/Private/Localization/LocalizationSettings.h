#pragma once

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct LocalizationSettings {
		SLT_REFLECT_VALUE(LocalizationSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_SETTINGS_CATEGORY("Localization")
		SLT_REFLECT_FIELD(defaultLanguage, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_END_REFLECT()

		str defaultLanguage = "en";
	};

} // namespace Shalltear
