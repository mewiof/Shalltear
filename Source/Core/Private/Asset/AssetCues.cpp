#include "Shalltear/Core/Asset/AssetCues.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

namespace Shalltear {

	namespace {

		struct Entry {
			const TypeInfo* type = nullptr;
			AssetCueFn fn = nullptr;
			const void* owner = nullptr;
		};

		constexpr u32 CAPACITY = 16;
		Entry _entries[CAPACITY];
		u32 _count = 0;

	} // anonymous namespace

	namespace AssetCues {

		bool Register(const TypeInfo* type, AssetCueFn fn, const void* owner) {
			if (!type || !fn) {
				return false;
			}
			for (u32 index = 0; index < _count; ++index) {
				if (_entries[index].type == type) {
					_entries[index] = Entry{type, fn, owner};
					return true;
				}
			}
			if (_count == CAPACITY) {
				return false;
			}
			_entries[_count++] = Entry{type, fn, owner};
			return true;
		}

		u32 UnregisterOwnedBy(const void* owner) {
			if (!owner) {
				return 0;
			}
			u32 kept = 0;
			for (u32 index = 0; index < _count; ++index) {
				if (_entries[index].owner != owner) {
					_entries[kept++] = _entries[index];
				}
			}
			const u32 removed = _count - kept;
			for (u32 index = kept; index < _count; ++index) {
				_entries[index] = Entry{};
			}
			_count = kept;
			return removed;
		}

		namespace {

			const Entry* Nearest(const TypeInfo* type) {
				const Entry* best = nullptr;
				for (u32 index = 0; index < _count; ++index) {
					const Entry& entry = _entries[index];
					if (!TypeRegistry::IsDerivedFrom(type, entry.type)) {
						continue;
					}
					if (!best || TypeRegistry::IsDerivedFrom(entry.type, best->type)) {
						best = &entry;
					}
				}
				return best;
			}

		} // anonymous namespace

		bool CanCue(const TypeInfo* type) { return type && Nearest(type) != nullptr; }

		bool Cue(const AssetCue& cue) {
			const Asset* asset = cue.asset.Get();
			const Entry* entry = asset ? Nearest(asset->GetType()) : nullptr;
			return entry && entry->fn(cue);
		}

	} // namespace AssetCues

} // namespace Shalltear
