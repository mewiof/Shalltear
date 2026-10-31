#include "Shalltear/Editor/PropertyDrawers.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include "Panels/InspectorPanel.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct FieldDrawerEntry {
			PropertyDrawerFn drawer;
			const void* owner = nullptr;
		};
		struct InspectorEntry {
			ComponentInspectorFn inspector;
			const void* owner = nullptr;
		};

		struct AssetInspectorEntry {
			AssetInspectorFn inspector;
			const void* owner = nullptr;
		};

		struct RowEntry {
			const UI::Context* context = nullptr;
			UI::ElementHandle row;
			PropertyDrawerRefreshFn refresh;
			const void* owner = nullptr;
		};

		struct DrawerState {
			HashMap<StringHash, FieldDrawerEntry> fieldDrawers{GetMemoryResource(MemTag::Editor)};
			HashMap<u64, FieldDrawerEntry> namedFieldDrawers{GetMemoryResource(MemTag::Editor)};
			HashMap<StringHash, InspectorEntry> inspectors{GetMemoryResource(MemTag::Editor)};
			HashMap<StringHash, AssetInspectorEntry> assetInspectors{GetMemoryResource(MemTag::Editor)};

			struct AssetSectionEntry {
				StringHash assetType;
				str label;
				i32 order = 0;
				AssetInspectorSectionAppliesFn applies;
				AssetInspectorSectionFn draw;
				const void* owner = nullptr;
			};
			Vector<AssetSectionEntry> assetSections{GetMemoryResource(MemTag::Editor)};
			Vector<RowEntry> rows{GetMemoryResource(MemTag::Editor)};

			bool refreshing = false;
		};

		DrawerState* _state = nullptr;

		u64 NamedFieldKey(StringHash declaringTypeName, StringHash fieldName) {
			constexpr u64 PRIME = 0x100000001b3ull;
			u64 key = declaringTypeName.GetValue();
			key = (key ^ fieldName.GetValue()) * PRIME;
			return key;
		}

	} // anonymous namespace

	void PropertyDrawers::Initialize() {
		if (!_state) {
			_state = New<DrawerState>(MemTag::Editor);
		}
	}

	void PropertyDrawers::Terminate() {
		if (_state) {
			Delete(_state);
			_state = nullptr;
		}
	}

	bool PropertyDrawers::RegisterFieldDrawer(StringHash fieldTypeName, PropertyDrawerFn drawer, const void* owner) {
		if (!_state || !drawer) {
			SLT_LOG_WARN("Editor", "a field drawer was registered before PropertyDrawers::Initialize (discarded)");
			return false;
		}
		_state->fieldDrawers[fieldTypeName] = {std::move(drawer), owner};
		return true;
	}

	bool PropertyDrawers::RegisterFieldDrawerFor(StringHash declaringTypeName, StringHash fieldName,
												 PropertyDrawerFn drawer, const void* owner) {
		if (!_state || !drawer) {
			SLT_LOG_WARN("Editor", "a field drawer was registered before PropertyDrawers::Initialize (discarded)");
			return false;
		}
		_state->namedFieldDrawers[NamedFieldKey(declaringTypeName, fieldName)] = {std::move(drawer), owner};
		return true;
	}

	bool PropertyDrawers::RegisterComponentInspector(StringHash componentTypeName, ComponentInspectorFn inspector, const void* owner) {
		if (!_state || !inspector) {
			SLT_LOG_WARN("Editor", "a component inspector was registered before PropertyDrawers::Initialize (discarded)");
			return false;
		}
		_state->inspectors[componentTypeName] = {std::move(inspector), owner};
		return true;
	}

	bool PropertyDrawers::RegisterAssetInspector(StringHash assetTypeName, AssetInspectorFn inspector, const void* owner) {
		if (!_state || !inspector) {
			SLT_LOG_WARN("Editor", "an asset inspector was registered before PropertyDrawers::Initialize (discarded)");
			return false;
		}
		_state->assetInspectors[assetTypeName] = {std::move(inspector), owner};
		return true;
	}

	bool PropertyDrawers::RegisterAssetInspectorSection(StringHash assetTypeName, AssetInspectorSectionDesc desc,
														const void* owner) {
		if (!_state || !desc.draw || desc.label.empty()) {
			SLT_LOG_WARN("Editor", "discarded an asset inspector section registered before PropertyDrawers::Initialize, or with no label or body");
			return false;
		}
		_state->assetSections.push_back(DrawerState::AssetSectionEntry{
			assetTypeName, str(desc.label), desc.order, std::move(desc.applies), std::move(desc.draw), owner});
		return true;
	}

	void PropertyDrawers::GetAssetInspectorSections(StringHash assetTypeName, Vector<AssetInspectorSection>& out) {
		out.clear();
		if (!_state) {
			return;
		}
		for (const DrawerState::AssetSectionEntry& entry : _state->assetSections) {
			if (entry.assetType == assetTypeName) {
				out.push_back(AssetInspectorSection{entry.label, entry.order, entry.applies, entry.draw});
			}
		}
		std::stable_sort(out.begin(), out.end(), [](const AssetInspectorSection& a, const AssetInspectorSection& b) {
			return a.order < b.order;
		});
	}

	std::function<void()> PropertyDrawers::GetInspectorRebuilder() {
		InspectorPanel* building = InspectorPanel::GetBuildingPanel();
		if (!building) {
			return []() {};
		}
		Handle<EditorPanel> panel(static_cast<EditorPanel*>(building));
		return [panel]() {
			if (EditorPanel* live = panel.Get()) {
				static_cast<InspectorPanel*>(live)->ReleaseDrawnAssetAndRebuild();
			}
		};
	}

	const PropertyDrawerFn* PropertyDrawers::FindFieldDrawer(StringHash fieldTypeName) {
		if (!_state) {
			return nullptr;
		}
		auto it = _state->fieldDrawers.find(fieldTypeName);
		return it != _state->fieldDrawers.end() ? &it->second.drawer : nullptr;
	}

	const PropertyDrawerFn* PropertyDrawers::FindFieldDrawerFor(StringHash declaringTypeName, StringHash fieldName) {
		if (!_state) {
			return nullptr;
		}
		auto it = _state->namedFieldDrawers.find(NamedFieldKey(declaringTypeName, fieldName));
		return it != _state->namedFieldDrawers.end() ? &it->second.drawer : nullptr;
	}

	const ComponentInspectorFn* PropertyDrawers::FindComponentInspector(StringHash componentTypeName) {
		if (!_state) {
			return nullptr;
		}
		auto it = _state->inspectors.find(componentTypeName);
		return it != _state->inspectors.end() ? &it->second.inspector : nullptr;
	}

	const AssetInspectorFn* PropertyDrawers::FindAssetInspector(StringHash assetTypeName) {
		if (!_state) {
			return nullptr;
		}
		auto it = _state->assetInspectors.find(assetTypeName);
		return it != _state->assetInspectors.end() ? &it->second.inspector : nullptr;
	}

	bool PropertyDrawers::SetRowRefresh(UI::Context& context, UI::ElementHandle row,
										PropertyDrawerRefreshFn refresh, const void* owner) {
		if (!_state || !refresh || !row.IsValid()) {
			return false;
		}
		for (RowEntry& entry : _state->rows) {
			if (entry.context == &context && entry.row == row) {
				entry.refresh = std::move(refresh);
				entry.owner = owner;
				return true;
			}
		}
		if (_state->refreshing) {
			SLT_LOG_WARN("Editor", "discarded a custom row registered inside a row refresh. Register it from the drawer");
			return false;
		}
		_state->rows.push_back({&context, row, std::move(refresh), owner});
		return true;
	}

	void PropertyDrawers::RefreshRows(UI::Context& context) {
		if (!_state || _state->refreshing) {
			return;
		}
		_state->refreshing = true;
		for (size_t i = 0; i < _state->rows.size(); ++i) {
			RowEntry& entry = _state->rows[i];
			if (entry.context != &context || !entry.refresh) {
				continue;
			}
			if (!context.IsAlive(entry.row)) {
				continue;
			}
			entry.refresh(context);
		}
		_state->refreshing = false;
		std::erase_if(_state->rows, [&context](const RowEntry& entry) {
			return entry.context == &context && !context.IsAlive(entry.row);
		});
	}

	bool PropertyDrawers::DropRow(UI::Context& context, UI::ElementHandle row) {
		if (!_state || _state->refreshing) {
			return false;
		}
		return std::erase_if(_state->rows, [&context, row](const RowEntry& entry) {
				   return entry.context == &context && entry.row == row;
			   }) > 0;
	}

	u32 PropertyDrawers::UnregisterOwnedBy(const void* owner) {
		if (!_state || !owner) {
			return 0;
		}
		u32 removed = 0;
		removed += static_cast<u32>(std::erase_if(_state->fieldDrawers, [owner](const auto& pair) {
			return pair.second.owner == owner;
		}));
		removed += static_cast<u32>(std::erase_if(_state->namedFieldDrawers, [owner](const auto& pair) {
			return pair.second.owner == owner;
		}));
		removed += static_cast<u32>(std::erase_if(_state->inspectors, [owner](const auto& pair) {
			return pair.second.owner == owner;
		}));
		removed += static_cast<u32>(std::erase_if(_state->assetInspectors, [owner](const auto& pair) {
			return pair.second.owner == owner;
		}));
		removed += static_cast<u32>(std::erase_if(_state->assetSections, [owner](const DrawerState::AssetSectionEntry& entry) {
			return entry.owner == owner;
		}));
		removed += static_cast<u32>(std::erase_if(_state->rows, [owner](const RowEntry& entry) {
			return entry.owner == owner;
		}));
		return removed;
	}

} // namespace Shalltear
