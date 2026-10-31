#include "Shalltear/Core/Graphics/MaterialAsset.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringHash.h"

#include <algorithm>
#include <cmath>

namespace Shalltear {

	void MaterialAsset::ApplyKeywordBindings(std::span<const ShaderKeywordBinding> bindings) {
		Vector<str> next{keywords.begin(), keywords.end(), GetMemoryResource(MemTag::Asset)};
		bool changed = false;
		for (const ShaderKeywordBinding& binding : bindings) {
			const Param* member = FindParam(StringHash(std::string_view{binding.member}));
			if (!member) {
				continue;
			}
			const f32 holds = member->kind == UniformMemberType::Int ? static_cast<f32>(member->integer) : member->numbers[0];
			const bool on = binding.value < 0 ? holds != 0.0f : std::lround(holds) == binding.value;
			auto held = std::find(next.begin(), next.end(), binding.keyword);
			if (on && held == next.end()) {
				next.push_back(binding.keyword);
				changed = true;
			} else if (!on && held != next.end()) {
				next.erase(held);
				changed = true;
			}
		}
		if (changed) {
			SetKeywords({next.data(), next.size()});
		}
	}

} // namespace Shalltear
