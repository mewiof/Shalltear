#include "Shalltear/Editor/EditorSelection.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include "Shalltear/Editor/EditorEvents.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct SelectionState {
			Vector<Handle<Object>> objects{GetMemoryResource(MemTag::Editor)};
			Handle<Object> active;
			Vector<Guid> assets{GetMemoryResource(MemTag::Editor)};
			Guid activeAsset{};
		};

		SelectionState* _state = nullptr;

		void PublishChanged() {
			EditorEvents::SelectionChanged.Publish(EventBus::GetGlobal(), {});
		}

		void Compact() {
			SelectionState& state = *_state;
			std::erase_if(state.objects, [](const Handle<Object>& handle) {
				return !handle.IsValid();
			});
			if (!state.active.IsValid()) {
				state.active = state.objects.empty() ? Handle<Object>{} : state.objects.back();
			}
		}

		bool Contains(const Handle<Object>& object) {
			for (const Handle<Object>& handle : _state->objects) {
				if (handle == object) {
					return true;
				}
			}
			return false;
		}

	} // anonymous namespace

	void EditorSelection::Initialize() {
		if (_state) {
			return;
		}
		_state = New<SelectionState>(MemTag::Editor);
	}

	void EditorSelection::Terminate() {
		if (!_state) {
			return;
		}
		Delete(_state);
		_state = nullptr;
	}

	void EditorSelection::Select(Handle<Object> object) {
		if (!_state) {
			return;
		}
		SelectionState& state = *_state;
		if (!object.IsValid()) {
			Clear();
			return;
		}
		Compact();
		bool unchanged = state.objects.size() == 1 && state.objects[0] == object && state.assets.empty();
		if (unchanged) {
			state.active = object;
			return;
		}
		state.objects.clear();
		state.objects.push_back(object);
		state.active = object;
		state.assets.clear();
		state.activeAsset = {};
		PublishChanged();
	}

	void EditorSelection::AddToSelection(Handle<Object> object) {
		if (!_state || !object.IsValid()) {
			return;
		}
		SelectionState& state = *_state;
		Compact();
		bool assetCleared = !state.assets.empty();
		state.assets.clear();
		state.activeAsset = {};
		if (Contains(object)) {
			state.active = object;
			if (assetCleared) {
				PublishChanged();
			}
			return;
		}
		state.objects.push_back(object);
		state.active = object;
		PublishChanged();
	}

	void EditorSelection::RemoveFromSelection(Handle<Object> object) {
		if (!_state) {
			return;
		}
		SelectionState& state = *_state;
		Compact();
		if (!Contains(object)) {
			return;
		}
		std::erase(state.objects, object);
		if (state.active == object) {
			state.active = state.objects.empty() ? Handle<Object>{} : state.objects.back();
		}
		PublishChanged();
	}

	void EditorSelection::ToggleInSelection(Handle<Object> object) {
		if (!_state || !object.IsValid()) {
			return;
		}
		if (Contains(object)) {
			RemoveFromSelection(object);
		} else {
			AddToSelection(object);
		}
	}

	void EditorSelection::Clear() {
		if (!_state) {
			return;
		}
		SelectionState& state = *_state;
		Compact();
		if (state.objects.empty() && state.assets.empty()) {
			return;
		}
		state.objects.clear();
		state.active = {};
		state.assets.clear();
		state.activeAsset = {};
		PublishChanged();
	}

	std::span<const Handle<Object>> EditorSelection::GetSelection() {
		if (!_state) {
			return {};
		}
		Compact();
		return {_state->objects.data(), _state->objects.size()};
	}

	Handle<Object> EditorSelection::GetActive() {
		if (!_state) {
			return {};
		}
		Compact();
		return _state->active;
	}

	bool EditorSelection::IsSelected(Handle<Object> object) {
		if (!_state) {
			return false;
		}
		Compact();
		return Contains(object);
	}

	u32 EditorSelection::GetSelectionCount() {
		if (!_state) {
			return 0;
		}
		Compact();
		return static_cast<u32>(_state->objects.size());
	}

	void EditorSelection::SelectAsset(const Guid& assetGuid) {
		SelectAssets({&assetGuid, 1});
	}

	void EditorSelection::SelectAssets(std::span<const Guid> assetGuids) {
		if (!_state) {
			return;
		}
		SelectionState& state = *_state;

		Vector<Guid> incoming{GetMemoryResource(MemTag::Editor)};
		incoming.reserve(assetGuids.size());
		for (const Guid& guid : assetGuids) {
			if (guid == Guid{} || std::find(incoming.begin(), incoming.end(), guid) != incoming.end()) {
				continue;
			}
			incoming.push_back(guid);
		}
		if (incoming.empty()) {
			Clear();
			return;
		}
		Compact();
		if (state.objects.empty() && state.assets.size() == incoming.size() &&
			std::equal(state.assets.begin(), state.assets.end(), incoming.begin())) {
			state.activeAsset = incoming.back();
			return;
		}
		state.objects.clear();
		state.active = {};
		state.assets = std::move(incoming);
		state.activeAsset = state.assets.back();
		PublishChanged();
	}

	std::span<const Guid> EditorSelection::GetSelectedAssets() {
		if (!_state) {
			return {};
		}
		return {_state->assets.data(), _state->assets.size()};
	}

	const Guid& EditorSelection::GetSelectedAsset() {
		static constexpr Guid NullGuid{};
		return _state ? _state->activeAsset : NullGuid;
	}

	bool EditorSelection::HasAssetSelection() {
		return _state && !_state->assets.empty();
	}

	u32 EditorSelection::GetAssetSelectionCount() {
		return _state ? static_cast<u32>(_state->assets.size()) : 0;
	}

} // namespace Shalltear
