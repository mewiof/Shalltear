#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	class SHALLTEAR_CORE_API TextSerializer {
	public:
		static str Serialize(const void* instance, const TypeInfo* type);

		static bool Deserialize(void* instance, const TypeInfo* type, std::string_view text);
	};

} // namespace Shalltear
