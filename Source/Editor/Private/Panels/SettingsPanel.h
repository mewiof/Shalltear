#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/PropertyRows.h"

namespace Shalltear {

	class SettingsPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(SettingsPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Settings")
		SLT_EDITOR_PANEL_CATEGORY("Configuration")
		SLT_REFLECT_FIELD(selectedPath)
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		SettingsPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;

		str selectedPath;
	private:
		void RebuildAll(UI::Context& context);
		void BuildTree(UI::Context& context);
		void BuildTreeEmptyState(UI::Context& context, UI::ElementHandle host);
		void BuildPage(UI::Context& context);
		void BuildFieldRow(UI::Context& context, UI::ElementHandle parent,
						   const SettingsCategoryInfo& category, const FieldInfo& field);
		void BuildReportRow(UI::Context& context, UI::ElementHandle parent,
							const SettingsCategoryInfo& category, const FieldInfo& field);
		UI::ElementHandle BuildLabelledRow(UI::Context& context, UI::ElementHandle parent,
										   std::string_view categoryPath, const FieldInfo& field);
		u64 ComputeVersion() const;
		u64 ComputeShownSignature() const;

		static u64 RowIdOf(std::string_view path);
		void TakeSelectionFromList();

		struct Row {
			str categoryPath;
			str fieldName;
			PropertyRows::ScalarRow scalar;
			UI::ElementHandle sourceBadge;
			UI::ElementHandle revertButton;
			UI::Widgets::DropdownParts scopePicker;
			bool overridable = false;
		};

		UI::ElementHandle _searchField;
		UI::ElementHandle _pageViewport;
		EditorListView _tree;
		Vector<Row> _rows{GetMemoryResource(MemTag::Editor)};
		str _search;
		u64 _lastVersion = 0;
		bool _forceRebuild = false;
		bool _pageDirty = false;
		u64 _shownSignature = 0;
		bool _treeEmpty = false;
	};

} // namespace Shalltear
