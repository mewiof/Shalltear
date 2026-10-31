#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>
#include <string_view>

namespace Shalltear {

	struct GraphSearchEntry {
		str name;
		str category;
		str synonyms;
		str detail;
		u64 key = 0;
	};

	struct GraphSearchSpan {
		u32 begin = 0;
		u32 end = 0;
	};

	namespace GraphSearchOps {

		SHALLTEAR_EDITOR_API void Rank(std::string_view query, std::span<const GraphSearchEntry> library, Vector<u32>& out);

		SHALLTEAR_EDITOR_API void Highlights(std::string_view query, std::string_view name, Vector<GraphSearchSpan>& out);

		SHALLTEAR_EDITOR_API str Completion(std::string_view query, std::string_view name);

	} // namespace GraphSearchOps

} // namespace Shalltear
