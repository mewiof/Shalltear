#include "PhysicsCategories.h"

#include <algorithm>

namespace Shalltear {

	bool PhysicsCategoryVocabulary::Rebuild(std::string_view declaration) {
		Vector<StringHash> rebuilt{GetMemoryResource(MemTag::Physics)};
		u32 overflow = 0;

		ForEachCategoryName(declaration, [&](std::string_view name) {
			const StringHash hash(name);
			if (std::find(rebuilt.begin(), rebuilt.end(), hash) != rebuilt.end()) {
				return;
			}
			rebuilt.push_back(hash);
		});

		std::sort(rebuilt.begin(), rebuilt.end(), [](StringHash a, StringHash b) {
			return a.GetValue() < b.GetValue();
		});

		if (rebuilt.size() > PHYSICS_CATEGORY_CAPACITY) {
			overflow = static_cast<u32>(rebuilt.size()) - PHYSICS_CATEGORY_CAPACITY;
			rebuilt.resize(PHYSICS_CATEGORY_CAPACITY);
		}

		const bool changed = rebuilt != _names || overflow != _overflowCount;
		_names = std::move(rebuilt);
		_overflowCount = overflow;
		return changed;
	}

	i32 PhysicsCategoryVocabulary::IndexOf(StringHash name) const {
		auto it = std::lower_bound(_names.begin(), _names.end(), name, [](StringHash a, StringHash b) {
			return a.GetValue() < b.GetValue();
		});
		if (it == _names.end() || *it != name) {
			return -1;
		}
		return static_cast<i32>(it - _names.begin());
	}

	u64 PhysicsCategoryVocabulary::Resolve(std::string_view names, u32& unknownCount) const {
		u64 bits = 0;
		ForEachCategoryName(names, [&](std::string_view name) {
			const i32 index = IndexOf(StringHash(name));
			if (index < 0) {
				++unknownCount;
				return;
			}
			bits |= 1ull << static_cast<u32>(index);
		});
		return bits;
	}

	PhysicsCategoryFilter ResolveCategoryFilter(const PhysicsCategoryVocabulary& vocabulary,
												std::string_view categories,
												PhysicsCategoryMatch mode,
												std::string_view collidesWith,
												u32& unknownCount) {
		PhysicsCategoryFilter filter;
		filter.categoryBits = vocabulary.Resolve(categories, unknownCount);

		const u64 named = vocabulary.Resolve(collidesWith, unknownCount);
		filter.maskBits = mode == PhysicsCategoryMatch::Only ? named : ~named;
		return filter;
	}

	PhysicsCategoryFilter ResolveQueryFilter(const PhysicsCategoryVocabulary& vocabulary,
											 std::string_view categories,
											 PhysicsCategoryMatch mode,
											 std::string_view collidesWith,
											 u32& unknownCount) {
		PhysicsCategoryFilter filter = ResolveCategoryFilter(vocabulary, categories, mode, collidesWith, unknownCount);

		bool named = false;
		ForEachCategoryName(categories, [&](std::string_view) { named = true; });
		if (!named) {
			filter.categoryBits = ~0ull;
		}
		return filter;
	}

} // namespace Shalltear
