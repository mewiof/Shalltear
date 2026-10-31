#include "SettingsPanel.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/SettingsOps.h"
#include "Shalltear/Editor/SettingsPresentation.h"

#include "InspectorPanel.h"

#include <cstring>

namespace Shalltear {

	SLT_REGISTER_TYPE(SettingsPanel)

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		Color TextNormal() { return EditorTheme::GetColor(ColorToken::Text); }
		Color TextDim() { return EditorTheme::GetColor(ColorToken::TextDim); }
		Color TextStrong() { return EditorTheme::GetColor(ColorToken::TextStrong); }
		f32 FieldFont() { return EditorTheme::GetMetric(MetricToken::FontBody); }
		f32 TinyFont() { return EditorTheme::GetMetric(MetricToken::FontTiny); }

		void AddPageNote(UI::Context& context, UI::ElementHandle parent, const str& text, Color color) {
			if (text.empty()) {
				return;
			}
			UI::ElementHandle note = context.CreateElement();
			context.AppendChild(parent, note);
			UI::Style style;
			style.padding = {8.0f, 0.0f, 8.0f, 6.0f};
			context.SetStyle(note, style);
			context.SetHoverable(note, false);
			context.SetText(note, UISystem::GetDefaultFont(), TinyFont(), color, text);
		}

		void CollectPickableScopes(SettingScope declared, Vector<SettingScope>& out) {
			out.clear();
			for (u32 index = 0; index <= static_cast<u32>(declared); ++index) {
				out.push_back(static_cast<SettingScope>(index));
			}
		}

	} // namespace

	void SettingsPanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Stretch;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(root, rootStyle);

		UI::ElementHandle searchRow = context.CreateElement();
		context.AppendChild(root, searchRow);
		UI::Style searchStyle;
		searchStyle.flexDirection = UI::FlexDirection::Row;
		searchStyle.alignItems = UI::Align::Center;
		searchStyle.padding = {8.0f, 6.0f, 8.0f, 6.0f};
		searchStyle.width = UI::Length::Percent(1.0f);
		searchStyle.flexShrink = 0.0f;
		context.SetStyle(searchRow, searchStyle);

		UI::ElementHandle searchLabel = context.CreateElement();
		context.AppendChild(searchRow, searchLabel);
		context.AddClass(searchLabel, EditorTheme::Class::FieldLabel);
		context.SetHoverable(searchLabel, false);
		context.SetText(searchLabel, UISystem::GetDefaultFont(), FieldFont(), TextDim(), "Search");

		UI::Widgets::TextFieldDesc searchDesc;
		searchDesc.text = _search;
		searchDesc.font = UISystem::GetDefaultFont();
		searchDesc.fontSizePx = FieldFont();
		searchDesc.onCommit = [this](std::string_view next) {
			if (_search != next) {
				_search = str(next);
				_forceRebuild = true;
			}
		};
		_searchField = UI::Widgets::TextField(context, searchRow, searchDesc);

		UI::ElementHandle body = context.CreateElement();
		context.AppendChild(root, body);
		UI::Style bodyStyle;
		bodyStyle.flexDirection = UI::FlexDirection::Row;
		bodyStyle.alignItems = UI::Align::Stretch;
		bodyStyle.width = UI::Length::Percent(1.0f);
		bodyStyle.flexGrow = 1.0f;
		context.SetStyle(body, bodyStyle);

		UI::ElementHandle treeColumn = context.CreateElement();
		context.AppendChild(body, treeColumn);
		UI::Style treeColumnStyle;
		treeColumnStyle.flexDirection = UI::FlexDirection::Column;
		treeColumnStyle.width = UI::Length::Pixel(180.0f);
		treeColumnStyle.flexShrink = 0.0f;
		context.SetStyle(treeColumn, treeColumnStyle);

		ListViewDesc categories;
		categories.layout = ListLayout::Rows;
		categories.mode = ListSelectionMode::Single;
		categories.onSelectionChanged = [this]() { TakeSelectionFromList(); };
		categories.onBuildTrailing = [this](UI::Context& context, UI::ElementHandle host) {
			BuildTreeEmptyState(context, host);
		};
		_tree.Build(this, context, treeColumn, std::move(categories));

		UI::Widgets::ScrollViewParts page = UI::Widgets::ScrollView(context, body);
		UI::Style pageStyle = context.GetStyle(page.root);
		pageStyle.flexGrow = 1.0f;
		context.SetStyle(page.root, pageStyle);
		_pageViewport = page.viewport;

		RebuildAll(context);
		_lastVersion = ComputeVersion();
	}

	void SettingsPanel::OnTick(f32) {
		UI::Context* context = GetPanelContext();
		if (!context || !context->IsAlive(_pageViewport)) {
			return;
		}
		u64 version = ComputeVersion();
		if (version != _lastVersion || _forceRebuild) {
			_lastVersion = version;
			_forceRebuild = false;
			_pageDirty = false;
			RebuildAll(*context);
			return;
		}
		if (_pageDirty || ComputeShownSignature() != _shownSignature) {
			_pageDirty = false;
			context->ClearChildren(_pageViewport);
			_rows.clear();
			BuildPage(*context);
			return;
		}

		for (Row& row : _rows) {
			PropertyRows::Refresh(*context, row.scalar);
			SettingFieldState state;
			if (!Settings::GetFieldState(row.categoryPath, row.fieldName, state)) {
				continue;
			}
			str badge = str(SettingsOps::SourceBadge(state));
			if (context->GetTextString(row.sourceBadge) != badge) {
				context->SetTextString(row.sourceBadge, badge);
			}
			if (context->IsAlive(row.revertButton)) {
				context->SetHoverable(row.revertButton, state.isOverride);
				if (state.isOverride) {
					context->RemoveClass(row.revertButton, EditorTheme::Class::Disabled);
				} else {
					context->AddClass(row.revertButton, EditorTheme::Class::Disabled);
				}
			}
		}
	}

	u64 SettingsPanel::ComputeVersion() const {
		Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
		Settings::GetCategories(categories);

		u64 hash = 1469598103934665603ull;
		auto mix = [&hash](u64 value) {
			hash ^= value;
			hash *= 1099511628211ull;
		};
		mix(categories.size());
		for (const SettingsCategoryInfo& info : categories) {
			mix(StringHash(info.path).GetValue());
			mix(info.type ? info.type->fieldCount : 0);
		}
		mix(StringHash(selectedPath).GetValue());
		mix(StringHash(_search).GetValue());
		return hash;
	}

	u64 SettingsPanel::ComputeShownSignature() const {
		Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
		Settings::GetCategories(categories);

		u64 hash = 1469598103934665603ull;
		for (const SettingsCategoryInfo& info : categories) {
			if (info.path != selectedPath || !info.type) {
				continue;
			}
			for (u32 index = 0; index < info.type->fieldCount; ++index) {
				if (SettingsOps::IsFieldShown(info, info.type->fields[index])) {
					hash ^= index + 1;
					hash *= 1099511628211ull;
				}
			}
		}
		return hash;
	}

	void SettingsPanel::RebuildAll(UI::Context& context) {
		context.ClearChildren(_pageViewport);
		_rows.clear();
		BuildTree(context);
		BuildPage(context);
	}

	u64 SettingsPanel::RowIdOf(std::string_view path) {
		return StringHash(path).GetValue();
	}

	void SettingsPanel::TakeSelectionFromList() {
		std::span<const u64> selected = _tree.GetSelected();
		u64 id = selected.empty() ? 0 : selected.back();
		Vector<SettingsOps::TreeNode> nodes{GetMemoryResource(MemTag::Editor)};
		SettingsOps::BuildTree(_search, nodes);
		str picked;
		for (const SettingsOps::TreeNode& node : nodes) {
			if (node.declared && RowIdOf(node.path) == id) {
				picked = node.path;
				break;
			}
		}
		if (picked == selectedPath) {
			return;
		}
		selectedPath = std::move(picked);
		_lastVersion = ComputeVersion();
		_pageDirty = true;
	}

	void SettingsPanel::BuildTree(UI::Context& context) {
		Vector<SettingsOps::TreeNode> nodes{GetMemoryResource(MemTag::Editor)};
		SettingsOps::BuildTree(_search, nodes);

		_treeEmpty = nodes.empty();
		Vector<ListRow> listRows{GetMemoryResource(MemTag::Editor)};
		for (const SettingsOps::TreeNode& node : nodes) {
			ListRow row;
			row.id = RowIdOf(node.path);
			row.selectable = node.declared;
			row.depth = node.depth;
			row.label = node.label;
			row.onDecorate = [declared = node.declared,
							  tooltip = str(node.tooltip)](UI::Context& context, const UI::Widgets::TreeRowParts& parts) {
				if (!declared) {
					PropertyRows::MakeDisabled(context, parts.row);
				}
				if (!tooltip.empty()) {
					context.SetTooltip(parts.row, tooltip);
				}
			};
			listRows.push_back(std::move(row));
		}
		_tree.SetRows(listRows);

		Vector<u64> ids{GetMemoryResource(MemTag::Editor)};
		if (!selectedPath.empty()) {
			ids.push_back(RowIdOf(selectedPath));
		}
		_tree.SetSelection(ids);
	}

	void SettingsPanel::BuildTreeEmptyState(UI::Context& context, UI::ElementHandle host) {
		if (!_treeEmpty) {
			return;
		}
		UI::ElementHandle empty = context.CreateElement();
		context.AppendChild(host, empty);
		UI::Style style;
		style.padding = {8.0f, 8.0f, 8.0f, 8.0f};
		context.SetStyle(empty, style);
		context.SetText(empty, UISystem::GetDefaultFont(), FieldFont(), TextDim(),
						_search.empty() ? "No settings." : "Nothing matches.");
	}

	void SettingsPanel::BuildPage(UI::Context& context) {
		_shownSignature = ComputeShownSignature();

		Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
		Settings::GetCategories(categories);

		const SettingsCategoryInfo* selected = nullptr;
		for (const SettingsCategoryInfo& info : categories) {
			if (info.path == selectedPath) {
				selected = &info;
				break;
			}
		}

		if (!selected) {
			UI::ElementHandle empty = context.CreateElement();
			context.AppendChild(_pageViewport, empty);
			UI::Style style;
			style.padding = {12.0f, 12.0f, 12.0f, 12.0f};
			context.SetStyle(empty, style);
			context.SetText(empty, UISystem::GetDefaultFont(), FieldFont(), TextDim(),
							selectedPath.empty() ? "Select a category." : "This category is no longer available.");
			return;
		}

		const SettingsCategoryPresentation* presentation = SettingsPresentation::FindCategory(selected->path);
		str title = presentation && !presentation->label.empty() ? presentation->label : str(selected->path);
		AddInspectorHeader(context, _pageViewport, title, TextStrong());

		str blurbText = presentation ? presentation->tooltip : str();
		if (!selected->overridable) {
			if (!blurbText.empty()) {
				blurbText += "  ";
			}
			blurbText += str(SettingsOps::ScopeDescription(selected->scope));
		}
		AddPageNote(context, _pageViewport, blurbText, TextDim());

		AddPageNote(context, _pageViewport, str(SettingsOps::AudienceLine(selected->path)), TextNormal());

		if (SettingsOps::AnyFieldTakesEffectOnRestart(*selected)) {
			AddPageNote(context, _pageViewport, str(SettingsOps::RestartLine(selected->path)), TextNormal());
		}

		if (selected->overridable) {
			AddPageNote(context, _pageViewport, str(SettingsOps::ScopePrecedenceHint()), TextDim());
		}

		if (!selected->type) {
			return;
		}
		for (u32 index = 0; index < selected->type->fieldCount; ++index) {
			if (SettingsOps::IsFieldShown(*selected, selected->type->fields[index])) {
				BuildFieldRow(context, _pageViewport, *selected, selected->type->fields[index]);
			}
		}
	}

	void SettingsPanel::BuildFieldRow(UI::Context& context, UI::ElementHandle parent,
									  const SettingsCategoryInfo& category, const FieldInfo& field) {
		if (!field.displayName || !field.addrFn) {
			return;
		}
		const str categoryPath = str(category.path);
		const str fieldName = str(field.displayName);

		SettingFieldState state;
		if (!Settings::GetFieldState(categoryPath, fieldName, state)) {
			if ((field.flags & FieldFlagTransient) && (field.flags & FieldFlagReadOnly)) {
				BuildReportRow(context, parent, category, field);
			}
			return;
		}

		UI::ElementHandle row = BuildLabelledRow(context, parent, categoryPath, field);

		Row built;
		built.categoryPath = categoryPath;
		built.fieldName = fieldName;
		built.overridable = category.overridable;

		void* live = category.live;
		PropertyRows::ValueAccess access;
		access.address = [live, addrFn = field.addrFn]() -> void* { return addrFn(live); };
		access.commit = [categoryPath, fieldName, live, addrFn = field.addrFn,
						 fieldType = field.fieldType, size = field.size](const PropertyRows::CommittedValue& value) {
			void* address = addrFn(live);
			switch (value.kind) {
			case PropertyRows::CommittedValue::Kind::Bool:
				*static_cast<bool*>(address) = value.boolean;
				break;
			case PropertyRows::CommittedValue::Kind::Text:
				*static_cast<str*>(address) = str(value.text);
				break;
			case PropertyRows::CommittedValue::Kind::RawBits:
				if (size <= 8) {
					std::memcpy(address, &value.rawBits, size);
				}
				break;
			case PropertyRows::CommittedValue::Kind::Number:
				PropertyRows::WriteNumber(fieldType, address, value.number);
				break;
			}
			Settings::NotifyChanged(categoryPath, fieldName);
			Settings::Save();
		};
		built.scalar = PropertyRows::Build(context, row, FieldRef{category.type ? category.type->name : StringHash{}, field.name},
										   std::move(access));
		if (built.scalar.kind == PropertyRows::ScalarRow::Kind::None) {
			UI::ElementHandle missing = context.CreateElement();
			context.AppendChild(row, missing);
			context.SetHoverable(missing, false);
			context.SetText(missing, UISystem::GetDefaultFont(), FieldFont(), TextDim(), "(no drawer)");
			return;
		}

		if (std::string_view when = SettingsOps::FieldMutabilityBadge(field); !when.empty()) {
			UI::ElementHandle mutability = context.CreateElement();
			context.AppendChild(row, mutability);
			UI::Style mutabilityStyle;
			mutabilityStyle.padding = {8.0f, 0.0f, 0.0f, 0.0f};
			mutabilityStyle.flexShrink = 0.0f;
			mutabilityStyle.textOverflow = UI::TextOverflow::Clip;
			context.SetStyle(mutability, mutabilityStyle);
			context.SetHoverable(mutability, true);
			context.SetText(mutability, UISystem::GetDefaultFont(), TinyFont(), TextDim(), when);
			MutabilityClass when_ = MutabilityClass::Hot;
			const bool restarts = Settings::GetFieldMutability(&field, when_) && when_ == MutabilityClass::Restart;
			context.SetTooltip(mutability, str(restarts ? SettingsOps::RestartLine(categoryPath) : SettingsOps::FieldMutabilityLine(field)));
		}

		UI::ElementHandle badge = context.CreateElement();
		context.AppendChild(row, badge);
		UI::Style badgeStyle;
		badgeStyle.padding = {8.0f, 0.0f, 4.0f, 0.0f};
		badgeStyle.flexShrink = 0.0f;
		badgeStyle.textOverflow = UI::TextOverflow::Clip;
		context.SetStyle(badge, badgeStyle);
		context.SetHoverable(badge, true);
		context.SetText(badge, UISystem::GetDefaultFont(), TinyFont(), TextDim(), SettingsOps::SourceBadge(state));
		context.SetTooltip(badge, SettingsOps::SourceBadgeDescription(state));
		built.sourceBadge = badge;

		if (category.overridable) {
			Vector<SettingScope> pickable{GetMemoryResource(MemTag::Editor)};
			CollectPickableScopes(category.scope, pickable);
			Vector<std::string_view> labels{GetMemoryResource(MemTag::Editor)};
			u32 selectedIndex = 0;
			for (size_t index = 0; index < pickable.size(); ++index) {
				labels.push_back(SettingsOps::ScopeName(pickable[index]));
				if (pickable[index] == state.editsLandIn) {
					selectedIndex = static_cast<u32>(index);
				}
			}
			UI::Widgets::DropdownDesc desc;
			desc.items = {labels.data(), labels.size()};
			desc.selected = selectedIndex;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = TinyFont();
			desc.onSelect = [this, categoryPath, fieldName, declared = category.scope](u32 index) {
				if (index > static_cast<u32>(declared)) {
					return;
				}
				Settings::SetFieldEditScope(categoryPath, fieldName, static_cast<SettingScope>(index));
				_forceRebuild = true;
			};
			built.scopePicker = UI::Widgets::Dropdown(context, row, desc);
			str pickerHelp = "Where changes to this setting are saved.\n";
			pickerHelp += str(SettingsOps::ScopePrecedenceHint());
			pickerHelp += "\nSaving to a broader scope removes the value from narrower ones.";
			context.SetTooltip(built.scopePicker.root, pickerHelp);
		}

		UI::Widgets::ButtonDesc revert;
		revert.label = "\xE2\x86\xBA";
		revert.font = UISystem::GetDefaultFont();
		revert.fontSizePx = TinyFont();
		revert.onClick = [categoryPath, fieldName]() {
			if (!Settings::RevertField(categoryPath, fieldName)) {
				return;
			}
			Settings::NotifyChanged(categoryPath, fieldName);
			Settings::Save();
		};
		built.revertButton = UI::Widgets::Button(context, row, revert);
		context.SetTooltip(built.revertButton, "Reset to the default value.");
		if (!state.isOverride) {
			PropertyRows::MakeDisabled(context, built.revertButton);
		}

		_rows.push_back(std::move(built));
	}

	UI::ElementHandle SettingsPanel::BuildLabelledRow(UI::Context& context, UI::ElementHandle parent,
													  std::string_view categoryPath, const FieldInfo& field) {
		UI::ElementHandle row = context.CreateElement();
		context.AppendChild(parent, row);
		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		rowStyle.alignItems = UI::Align::Center;
		rowStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		rowStyle.width = UI::Length::Percent(1.0f);
		context.SetStyle(row, rowStyle);

		UI::ElementHandle label = context.CreateElement();
		context.AppendChild(row, label);
		context.AddClass(label, EditorTheme::Class::FieldLabel);
		UI::Style labelStyle;
		labelStyle.width = UI::Length::Pixel(150.0f);
		labelStyle.flexShrink = 0.0f;
		labelStyle.textOverflow = UI::TextOverflow::Ellipsis;
		context.SetStyle(label, labelStyle);
		context.SetHoverable(label, false);
		context.SetText(label, UISystem::GetDefaultFont(), FieldFont(), TextNormal(),
						SettingsOps::FieldLabelFor(categoryPath, field));

		str tooltip = SettingsOps::FieldTooltipFor(categoryPath, field);
		if (!tooltip.empty()) {
			context.SetTooltip(row, tooltip);
		}
		return row;
	}

	void SettingsPanel::BuildReportRow(UI::Context& context, UI::ElementHandle parent,
									   const SettingsCategoryInfo& category, const FieldInfo& field) {
		UI::ElementHandle row = BuildLabelledRow(context, parent, category.path, field);

		void* live = category.live;
		PropertyRows::ValueAccess access;
		access.address = [live, addrFn = field.addrFn]() -> void* { return addrFn(live); };
		access.readOnly = true;

		Row built;
		built.categoryPath = str(category.path);
		built.fieldName = str(field.displayName);
		built.scalar = PropertyRows::Build(context, row, FieldRef{category.type ? category.type->name : StringHash{}, field.name},
										   std::move(access));
		_rows.push_back(std::move(built));
	}

	EditorIcons::Icon SettingsPanel::GetTabIcon() const {
		return EditorIcons::Glyph::PanelSettings;
	}

} // namespace Shalltear
