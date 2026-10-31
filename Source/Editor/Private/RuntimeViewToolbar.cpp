#include "Shalltear/Editor/RuntimeViewToolbar.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct Entry {
			StringHash id;
			RuntimeViewToggle toggle;
			const void* owner = nullptr;
		};

		struct ToolbarState {
			Vector<Entry> entries{GetMemoryResource(MemTag::Editor)};
			u64 version = 1;
		};
		ToolbarState* _state = nullptr;

		ToolbarState& State() {
			if (!_state) {
				_state = New<ToolbarState>(MemTag::Editor);
			}
			return *_state;
		}

	} // anonymous namespace

	namespace RuntimeViewToolbar {

		bool RegisterToggle(StringHash id, RuntimeViewToggle toggle, const void* owner) {
			if (!toggle.isOn || !toggle.set) {
				SLT_LOG_WARN("Editor", "ignoring a Runtime View toggle with no state to read or write");
				return false;
			}
			ToolbarState& state = State();
			++state.version;
			for (Entry& entry : state.entries) {
				if (entry.id == id) {
					entry.toggle = std::move(toggle);
					entry.owner = owner;
					return true;
				}
			}
			state.entries.push_back({id, std::move(toggle), owner});
			return true;
		}

		u32 UnregisterOwnedBy(const void* owner) {
			if (!_state || !owner) {
				return 0;
			}
			const u32 removed = static_cast<u32>(std::erase_if(_state->entries, [owner](const Entry& entry) { return entry.owner == owner; }));
			if (removed > 0) {
				++_state->version;
			}
			return removed;
		}

		void ForEachToggle(const std::function<void(StringHash id, const RuntimeViewToggle& toggle)>& visit) {
			if (!_state) {
				return;
			}
			for (const Entry& entry : _state->entries) {
				visit(entry.id, entry.toggle);
			}
		}

		const RuntimeViewToggle* FindToggle(StringHash id) {
			if (!_state) {
				return nullptr;
			}
			for (const Entry& entry : _state->entries) {
				if (entry.id == id) {
					return &entry.toggle;
				}
			}
			return nullptr;
		}

		u64 GetVersion() { return _state ? _state->version : 0; }

		void Terminate() {
			if (_state) {
				Delete(_state);
				_state = nullptr;
			}
		}

	} // namespace RuntimeViewToolbar

} // namespace Shalltear
