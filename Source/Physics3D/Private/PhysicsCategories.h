#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Physics3D/PhysicsCategoryMatch.h"

#include <string_view>

namespace Shalltear {

	template <typename Visit>
	void ForEachCategoryName(std::string_view list, Visit&& visit) {
		size_t start = 0;
		while (start <= list.size()) {
			size_t end = list.find(',', start);
			if (end == std::string_view::npos) {
				end = list.size();
			}
			std::string_view name = list.substr(start, end - start);
			while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
				name.remove_prefix(1);
			}
			while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
				name.remove_suffix(1);
			}
			if (!name.empty()) {
				visit(name);
			}
			if (end == list.size()) {
				return;
			}
			start = end + 1;
		}
	}

	// comma + space, must match the reader
	inline str JoinCategoryNames(const Vector<str>& names) {
		str text;
		for (const str& name : names) {
			if (name.empty()) {
				continue;
			}
			if (!text.empty()) {
				text += ", ";
			}
			text += name;
		}
		return text;
	}

	inline constexpr u32 PHYSICS_CATEGORY_OVERFLOW_BIT = 63;
	inline constexpr u32 PHYSICS_CATEGORY_CAPACITY = PHYSICS_CATEGORY_OVERFLOW_BIT;

	class PhysicsCategoryVocabulary {
	public:
		PhysicsCategoryVocabulary() = default;

		bool Rebuild(std::string_view declaration);

		i32 IndexOf(StringHash name) const;

		u64 Resolve(std::string_view names, u32& unknownCount) const;

		u32 Size() const { return static_cast<u32>(_names.size()); }

		u32 OverflowCount() const { return _overflowCount; }
	private:
		Vector<StringHash> _names{GetMemoryResource(MemTag::Physics)};
		u32 _overflowCount = 0;
	};

	struct PhysicsCategoryFilter {
		u64 categoryBits = 0;
		u64 maskBits = ~0ull;

		i32 groupIndex = 0;
	};

	PhysicsCategoryFilter ResolveCategoryFilter(const PhysicsCategoryVocabulary& vocabulary,
												std::string_view categories,
												PhysicsCategoryMatch mode,
												std::string_view collidesWith,
												u32& unknownCount);

	PhysicsCategoryFilter ResolveQueryFilter(const PhysicsCategoryVocabulary& vocabulary,
											 std::string_view categories,
											 PhysicsCategoryMatch mode,
											 std::string_view collidesWith,
											 u32& unknownCount);

} // namespace Shalltear
