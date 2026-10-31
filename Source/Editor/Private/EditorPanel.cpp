#include "Shalltear/Editor/EditorPanel.h"

#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/PanelManager.h"

#include <algorithm>

namespace Shalltear {

	void EditorPanel::BuildContent(UI::Context& context, UI::ElementHandle root) {
		_lastBuildContext = &context;
		OnBuildContent(context, root);
	}

	UI::Context* EditorPanel::GetPanelContext() const {
		if (!UISystem::IsInitialized()) {
			return _lastBuildContext;
		}
		return UISystem::IsContextLive(_lastBuildContext) ? _lastBuildContext : nullptr;
	}

	void EditorPanel::RegisterList(EditorListView* list) {
		if (!list || std::find(_lists.begin(), _lists.end(), list) != _lists.end()) {
			return;
		}
		_lists.push_back(list);
	}

	void EditorPanel::UnregisterList(EditorListView* list) {
		auto found = std::find(_lists.begin(), _lists.end(), list);
		if (found != _lists.end()) {
			_lists.erase(found);
		}
	}

	bool EditorPanel::OnCommand(PanelCommand command) {
		for (EditorListView* list : _lists) {
			if (list->HasFocus()) {
				return list->Command(command);
			}
		}
		return false;
	}

	void EditorPanel::FocusList(EditorListView& list) {
		if (std::find(_lists.begin(), _lists.end(), &list) == _lists.end()) {
			return;
		}
		list.TakeFocus();
		PanelManager::FocusPanel(Handle<EditorPanel>(this));
	}

	str EditorPanel::GetTitleBase() const {
		const TypeInfo* type = GetType();
		if (const AttributeInfo* attribute = FindTypeAttribute(type, "EditorPanelName"_hash)) {
			if (attribute->value.Is<str>()) {
				return attribute->value.As<str>();
			}
		}
		return "Panel";
	}

	EditorIcons::Icon EditorPanel::GetTabIcon() const {
		return {};
	}

	str EditorPanel::GetSaveChangesMessage() const {
		return GetTitle() + " has unsaved changes.";
	}

	str EditorPanel::GetTitle() const {
		str base = GetTitleBase();
		if (_instanceSerial > 1) {
			return base + " (" + std::to_string(_instanceSerial) + ")";
		}
		return base;
	}

} // namespace Shalltear
