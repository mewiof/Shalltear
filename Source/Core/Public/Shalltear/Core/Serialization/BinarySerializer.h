#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	constexpr u32 SLT_MAX_SERIALIZATION_DEPTH = 32;

	class SHALLTEAR_CORE_API BinarySerializer {
	public:
		static Buffer Serialize(const void* instance, const TypeInfo* type);

		static bool Deserialize(void* instance, const TypeInfo* type, const Buffer& buffer);
	};

} // namespace Shalltear
