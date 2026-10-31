#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Memory.h"

#include <functional>
#include <memory_resource>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Shalltear {

	class SHALLTEAR_CORE_API TaggedMemoryResource final : public std::pmr::memory_resource {
	public:
		explicit TaggedMemoryResource(MemTag tag) noexcept : _tag(tag) {}

		MemTag GetTag() const noexcept { return _tag; }
	private:
		void* do_allocate(size_t bytes, size_t alignment) override;
		void do_deallocate(void* p, size_t bytes, size_t alignment) override;
		bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

		MemTag _tag;
	};

	SHALLTEAR_CORE_API std::pmr::memory_resource* GetMemoryResource(MemTag tag) noexcept;

	template <typename T>
	using Vector = std::pmr::vector<T>;

	template <typename K, typename V, typename Hash = std::hash<K>, typename Eq = std::equal_to<K>>
	using HashMap = std::pmr::unordered_map<K, V, Hash, Eq>;

	template <typename K, typename Hash = std::hash<K>, typename Eq = std::equal_to<K>>
	using HashSet = std::pmr::unordered_set<K, Hash, Eq>;

	using String = std::pmr::string;

} // namespace Shalltear
