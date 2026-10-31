#include "Shalltear/Editor/ProfilerModules.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct Entry {
			StringHash id;
			ProfilerModule module;
			const void* owner = nullptr;
		};

		struct ModulesState {
			Vector<Entry> entries{GetMemoryResource(MemTag::Editor)};
			u64 version = 1;
		};
		ModulesState* _state = nullptr;

		ModulesState& State() {
			if (!_state) {
				_state = New<ModulesState>(MemTag::Editor);
			}
			return *_state;
		}

	} // anonymous namespace

	namespace ProfilerModules {

		bool Register(StringHash id, ProfilerModule module, const void* owner) {
			if (!module.build || !module.refresh) {
				SLT_LOG_WARN("Editor", "ignoring a Profiler module '{}' with nothing to build or refresh", module.title);
				return false;
			}
			ModulesState& state = State();
			++state.version;
			for (Entry& entry : state.entries) {
				if (entry.id == id) {
					entry.module = std::move(module);
					entry.owner = owner;
					return true;
				}
			}
			state.entries.push_back({id, std::move(module), owner});
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

		void ForEach(const std::function<void(StringHash id, const ProfilerModule& module)>& visit) {
			if (!_state) {
				return;
			}
			for (const Entry& entry : _state->entries) {
				visit(entry.id, entry.module);
			}
		}

		const ProfilerModule* Find(StringHash id) {
			if (!_state) {
				return nullptr;
			}
			for (const Entry& entry : _state->entries) {
				if (entry.id == id) {
					return &entry.module;
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

	} // namespace ProfilerModules

} // namespace Shalltear
