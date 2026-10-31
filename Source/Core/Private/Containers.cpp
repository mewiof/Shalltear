#include "Shalltear/Core/Containers.h"

#include "Module/EngineStateTable.h"

#include <iterator>

namespace Shalltear {

	void* TaggedMemoryResource::do_allocate(size_t bytes, size_t alignment) {
		return Shalltear::Allocate(bytes, _tag, alignment);
	}

	void TaggedMemoryResource::do_deallocate(void* p, size_t bytes, size_t alignment) {
		(void)bytes;
		(void)alignment;
		Shalltear::Free(p);
	}

	bool TaggedMemoryResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
		auto* tagged = dynamic_cast<const TaggedMemoryResource*>(&other);
		return tagged != nullptr && tagged->_tag == _tag;
	}

	namespace {

		TaggedMemoryResource* _boundResources = nullptr;

		TaggedMemoryResource* GetResourceTable() noexcept {
			if (_boundResources)
				return _boundResources;

			// leaked, static teardown can still deallocate
			static TaggedMemoryResource* const local = new TaggedMemoryResource[static_cast<size_t>(MemTag::Count)]{
				TaggedMemoryResource(MemTag::Unknown),
				TaggedMemoryResource(MemTag::Core),
				TaggedMemoryResource(MemTag::Editor),
				TaggedMemoryResource(MemTag::Graphics),
				TaggedMemoryResource(MemTag::UI),
				TaggedMemoryResource(MemTag::Physics),
				TaggedMemoryResource(MemTag::Audio),
				TaggedMemoryResource(MemTag::Scripting),
				TaggedMemoryResource(MemTag::Game),
				TaggedMemoryResource(MemTag::Scene),
				TaggedMemoryResource(MemTag::Platform),
				TaggedMemoryResource(MemTag::String),
				TaggedMemoryResource(MemTag::Container),
				TaggedMemoryResource(MemTag::Asset),
				TaggedMemoryResource(MemTag::Debug),
			};
			static_assert(static_cast<size_t>(MemTag::Count) == 15,
						  "MemTag changed: update the TaggedMemoryResource table in Containers.cpp");
			return local;
		}

	} // anonymous namespace

	std::pmr::memory_resource* GetMemoryResource(MemTag tag) noexcept {
		size_t idx = static_cast<size_t>(tag);
		if (idx >= static_cast<size_t>(MemTag::Count)) {
			idx = static_cast<size_t>(MemTag::Unknown);
		}
		return &GetResourceTable()[idx];
	}

	namespace ModuleInternal {

		void CaptureContainersEngineState(EngineStateTable& table) {
			table.memoryResources = GetResourceTable();
		}

		void BindContainersEngineState(const EngineStateTable& table) {
			_boundResources = static_cast<TaggedMemoryResource*>(table.memoryResources);
		}

	} // namespace ModuleInternal

} // namespace Shalltear
