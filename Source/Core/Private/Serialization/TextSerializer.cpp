#include "Shalltear/Core/Serialization/TextSerializer.h"

#include "SerializerInternal.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

namespace Shalltear {

	str TextSerializer::Serialize(const void* instance, const TypeInfo* type) {
		if (!instance || !type) {
			return "null";
		}
		SerializerInternal::StringPool pool;
		JSONValue root = SerializerInternal::TextBuildObject(instance, type, pool, 0);
		return JSONSerialize(root, true);
	}

	bool TextSerializer::Deserialize(void* instance, const TypeInfo* type, std::string_view text) {
		if (!instance || !type) {
			return false;
		}

		LinearAllocator arena(text.size() + 64, MemTag::Core);
		JSONValue root = JSONParse(text, arena);
		if (!root.IsObject()) {
			return false;
		}
		return SerializerInternal::TextReadObject(instance, type, root, 0);
	}

} // namespace Shalltear
