#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Memory.h"

#include "Module/EngineStateTable.h"

#include <atomic>
#include <mutex>

namespace Shalltear {

	constexpr u32 CHUNK_SHIFT = 16;
	constexpr u32 CHUNK_SIZE = 1 << CHUNK_SHIFT;
	constexpr u32 CHUNK_MASK = CHUNK_SIZE - 1;
	constexpr u32 MAX_CHUNKS = 1024;
	constexpr u32 INVALID_INDEX = 0xFFFFFFFF;

	struct Slot {
		std::atomic<Object*> obj{nullptr};
		std::atomic<u32> generation{1};
		u32 nextFree{INVALID_INDEX};
	};

	struct Chunk {
		Slot slots[CHUNK_SIZE];
	};

	struct RegistryData {
		std::atomic<Chunk*> chunks[MAX_CHUNKS]{};
		std::mutex allocMutex;

		u32 freeHead{INVALID_INDEX};
		std::atomic<u32> capacity{1};

		~RegistryData() {
			for (u32 i = 0; i < MAX_CHUNKS; ++i) {
				Chunk* chunk = chunks[i].load(std::memory_order_relaxed);
				if (chunk) {
					Shalltear::Delete(chunk);
				}
			}
		}
	};

	static RegistryData* _boundRegistry = nullptr;

	static RegistryData& GetRegistry() {
		if (_boundRegistry)
			return *_boundRegistry;
		static RegistryData data;
		return data;
	}

	Object::Object() {
		_instanceId = ObjectRegistry::Register(this);
	}

	Object::~Object() {
		ObjectRegistry::Unregister(_instanceId);
	}

	u64 ObjectRegistry::Register(Object* obj) {
		if (!obj)
			return 0;

		auto& data = GetRegistry();
		u32 index = INVALID_INDEX;

		{
			std::lock_guard<std::mutex> lock(data.allocMutex);

			if (data.freeHead != INVALID_INDEX) {
				index = data.freeHead;
				u32 chunkIdx = index >> CHUNK_SHIFT;
				u32 localIdx = index & CHUNK_MASK;

				data.freeHead = data.chunks[chunkIdx].load(std::memory_order_relaxed)->slots[localIdx].nextFree;
			} else {
				u32 currentCap = data.capacity.load(std::memory_order_relaxed);
				SLT_CORE_ASSERT_MSG(currentCap < (MAX_CHUNKS * CHUNK_SIZE), "ObjectRegistry maximum capacity exceeded!");
				index = currentCap;

				u32 chunkIdx = index >> CHUNK_SHIFT;

				if (!data.chunks[chunkIdx].load(std::memory_order_relaxed)) {
					Chunk* newChunk = Shalltear::New<Chunk>(MemTag::Core);
					data.chunks[chunkIdx].store(newChunk, std::memory_order_release);
				}

				data.capacity.store(currentCap + 1, std::memory_order_release);
			}
		}

		u32 chunkIdx = index >> CHUNK_SHIFT;
		u32 localIdx = index & CHUNK_MASK;

		Chunk* chunk = data.chunks[chunkIdx].load(std::memory_order_acquire);
		Slot& slot = chunk->slots[localIdx];

		slot.obj.store(obj, std::memory_order_release);
		u32 generation = slot.generation.load(std::memory_order_relaxed);

		return (static_cast<u64>(generation) << 32) | index;
	}

	void ObjectRegistry::Unregister(u64 id) {
		if (id == 0)
			return;

		u32 index = static_cast<u32>(id & 0xFFFFFFFF);
		u32 expectedGeneration = static_cast<u32>(id >> 32);

		auto& data = GetRegistry();

		if (index >= data.capacity.load(std::memory_order_acquire))
			return;

		u32 chunkIdx = index >> CHUNK_SHIFT;
		u32 localIdx = index & CHUNK_MASK;

		Chunk* chunk = data.chunks[chunkIdx].load(std::memory_order_acquire);
		if (!chunk)
			return;

		Slot& slot = chunk->slots[localIdx];

		if (!slot.generation.compare_exchange_strong(expectedGeneration, expectedGeneration + 1, std::memory_order_acq_rel))
			return;

		slot.obj.store(nullptr, std::memory_order_release);

		{
			std::lock_guard<std::mutex> lock(data.allocMutex);
			slot.nextFree = data.freeHead;
			data.freeHead = index;
		}
	}

	Object* ObjectRegistry::Resolve(u64 id) {
		if (id == 0)
			return nullptr;

		u32 index = static_cast<u32>(id & 0xFFFFFFFF);
		u32 generation = static_cast<u32>(id >> 32);

		auto& data = GetRegistry();

		if (index >= data.capacity.load(std::memory_order_acquire))
			return nullptr;

		u32 chunkIdx = index >> CHUNK_SHIFT;
		u32 localIdx = index & CHUNK_MASK;

		Chunk* chunk = data.chunks[chunkIdx].load(std::memory_order_acquire);
		if (!chunk)
			return nullptr;

		Slot& slot = chunk->slots[localIdx];

		if (slot.generation.load(std::memory_order_acquire) != generation)
			return nullptr;

		Object* obj = slot.obj.load(std::memory_order_acquire);

		if (slot.generation.load(std::memory_order_acquire) != generation)
			return nullptr;

		return obj;
	}

	void ObjectRegistry::ForEachLiveObject(void (*visitor)(Object*, u64, void*), void* user) {
		if (!visitor)
			return;

		auto& data = GetRegistry();

		u32 capacity = data.capacity.load(std::memory_order_acquire);
		for (u32 index = 1; index < capacity; ++index) {
			Chunk* chunk = data.chunks[index >> CHUNK_SHIFT].load(std::memory_order_acquire);
			if (!chunk)
				continue;

			Slot& slot = chunk->slots[index & CHUNK_MASK];
			Object* obj = slot.obj.load(std::memory_order_acquire);
			if (!obj)
				continue;

			u32 generation = slot.generation.load(std::memory_order_acquire);
			visitor(obj, (static_cast<u64>(generation) << 32) | index, user);
		}
	}

	namespace {

		Slot* FindSlot(RegistryData& data, u64 id) {
			if (id == 0)
				return nullptr;

			u32 index = static_cast<u32>(id & 0xFFFFFFFF);
			if (index >= data.capacity.load(std::memory_order_acquire))
				return nullptr;

			Chunk* chunk = data.chunks[index >> CHUNK_SHIFT].load(std::memory_order_acquire);
			if (!chunk)
				return nullptr;

			return &chunk->slots[index & CHUNK_MASK];
		}

	} // anonymous namespace

	bool ObjectRegistry::DetachPreservingSlot(Object& object) {
		u64 id = object._instanceId;
		auto& data = GetRegistry();

		Slot* slot = FindSlot(data, id);
		if (!slot)
			return false;

		u32 expectedGeneration = static_cast<u32>(id >> 32);
		if (slot->generation.load(std::memory_order_acquire) != expectedGeneration)
			return false;
		if (slot->obj.load(std::memory_order_acquire) != &object)
			return false;

		slot->obj.store(nullptr, std::memory_order_release);

		object._instanceId = 0;
		return true;
	}

	bool ObjectRegistry::RebindPreservedSlot(Object& replacement, u64 preservedId) {
		auto& data = GetRegistry();

		Slot* slot = FindSlot(data, preservedId);
		if (!slot)
			return false;

		u32 expectedGeneration = static_cast<u32>(preservedId >> 32);
		if (slot->generation.load(std::memory_order_acquire) != expectedGeneration)
			return false;
		if (slot->obj.load(std::memory_order_acquire) != nullptr)
			return false;

		Unregister(replacement._instanceId);
		slot->obj.store(&replacement, std::memory_order_release);
		replacement._instanceId = preservedId;
		return true;
	}

	void ObjectRegistry::ReleaseReservedSlot(u64 id) {
		SLT_CORE_ASSERT_MSG(Resolve(id) == nullptr,
							"ReleaseReservedSlot on a live slot (this id was never detached)");
		Unregister(id);
	}

	namespace ModuleInternal {

		void CaptureObjectRegistryEngineState(EngineStateTable& table) {
			table.objectRegistry = &GetRegistry();
		}

		void BindObjectRegistryEngineState(const EngineStateTable& table) {
			_boundRegistry = static_cast<RegistryData*>(table.objectRegistry);
		}

	} // namespace ModuleInternal

} //namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::Object)
