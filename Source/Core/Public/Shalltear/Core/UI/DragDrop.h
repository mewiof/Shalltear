#pragma once

#include "Shalltear/Core/Reflection/Variant.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear::UI {

	struct DragPayload {
		StringHash type{};
		Variant data;
		str display;
	};

} // namespace Shalltear::UI
