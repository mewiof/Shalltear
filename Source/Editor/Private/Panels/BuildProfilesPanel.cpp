#include "BuildProfilesPanel.h"

#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/BuildTarget.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorInputOps.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/ProjectOps.h"

#include "BuildFlows.h"
#include "SceneFileFlows.h"

#include <format>

namespace Shalltear {

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		Color TextNormal() { return EditorTheme::GetColor(ColorToken::Text); }
		Color TextDim() { return EditorTheme::GetColor(ColorToken::TextDim); }
		Color TextFaint() { return EditorTheme::GetColor(ColorToken::TextFaint); }
		Color TextStrong() { return EditorTheme::GetColor(ColorToken::TextStrong); }
		Color StatusOk() { return EditorTheme::GetColor(ColorToken::StatusOk); }
		Color StatusWarn() { return EditorTheme::GetColor(ColorToken::StatusWarn); }
		Color StatusError() { return EditorTheme::GetColor(ColorToken::StatusError); }

		f32 RowFont() { return EditorTheme::GetMetric(MetricToken::FontRow); }
		f32 BodyFont() { return EditorTheme::GetMetric(MetricToken::FontBody); }
		f32 SmallFont() { return EditorTheme::GetMetric(MetricToken::FontSmall); }
		f32 HeadingFont() { return EditorTheme::GetMetric(MetricToken::FontHeading); }
		f32 RowPadX() { return EditorTheme::GetMetric(MetricToken::RowPaddingX); }

		constexpr f32 LIST_WIDTH = 262.0f;
		constexpr f32 FIELD_LABEL_WIDTH = 118.0f;

		Handle<FontAsset> Font() {
			return UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
		}

		UI::ElementHandle Row(UI::Context& context, UI::ElementHandle parent, UI::Align align = UI::Align::Center) {
			UI::ElementHandle row = context.CreateElement();
			context.AppendChild(parent, row);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.alignItems = align;
			style.width = UI::Length::Percent(1.0f);
			context.SetStyle(row, style);
			return row;
		}

		UI::ElementHandle Label(UI::Context& context, UI::ElementHandle parent, std::string_view text,
								f32 sizePx, Color color) {
			UI::ElementHandle label = context.CreateElement();
			context.AppendChild(parent, label);
			context.SetHoverable(label, false);
			context.SetText(label, Font(), sizePx, color, text);
			return label;
		}

		UI::ElementHandle Button(UI::Context& context, UI::ElementHandle parent, std::string_view label,
								 std::function<void()> onClick) {
			UI::Widgets::ButtonDesc desc;
			desc.label = label;
			desc.font = Font();
			desc.fontSizePx = BodyFont();
			desc.onClick = std::move(onClick);
			UI::ElementHandle button = UI::Widgets::Button(context, parent, desc);
			UI::Style style = context.GetStyle(button);
			style.margin = {0.0f, 0.0f, 4.0f, 0.0f};
			context.SetStyle(button, style);
			return button;
		}

		UI::ElementHandle GlyphButton(UI::Context& context, UI::ElementHandle parent, EditorIcons::Icon icon,
									  std::string_view fallback, std::string_view tooltip,
									  std::function<void()> onClick) {
			EditorIcons::IconButtonParts parts =
				EditorIcons::IconButton(context, parent, icon, fallback, tooltip, std::move(onClick), BodyFont());
			UI::Style style = context.GetStyle(parts.button);
			style.margin = {0.0f, 0.0f, 4.0f, 0.0f};
			context.SetStyle(parts.button, style);
			return parts.button;
		}

		UI::ElementHandle Section(UI::Context& context, UI::ElementHandle parent, std::string_view title) {
			UI::ElementHandle card = context.CreateElement();
			context.AppendChild(parent, card);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Column;
			style.width = UI::Length::Percent(1.0f);
			style.margin = {0.0f, 0.0f, 0.0f, 6.0f};
			context.SetStyle(card, style);
			context.AddClass(card, EditorTheme::Class::SectionCard);

			UI::ElementHandle header = context.CreateElement();
			context.AppendChild(card, header);
			UI::Style headerStyle;
			headerStyle.padding = {8.0f, 4.0f, 8.0f, 4.0f};
			context.SetStyle(header, headerStyle);
			context.AddClass(header, EditorTheme::Class::SectionHeader);
			context.SetHoverable(header, false);
			context.SetText(header, Font(), RowFont(), TextStrong(), title);
			return card;
		}

		bool IsListType(BuildSettingType type) {
			return type == BuildSettingType::StringList || type == BuildSettingType::SceneList;
		}

		str FormatBytes(u64 bytes) {
			if (bytes >= 1024ull * 1024ull) {
				return std::format("{:.1f} MiB", static_cast<f64>(bytes) / (1024.0 * 1024.0));
			}
			return std::format("{} B", bytes);
		}

		bool IsDrawnOutsideBuildOptions(std::string_view key) {
			return key == "outputPath" || key == "executableName" || key == "version" ||
				   key == "buildNumber" || key == "scenes" || key == "includeInBuildAll";
		}

	} // anonymous namespace

	void BuildProfilesPanel::OnOpen() {
		_dirty = true;
	}

	void BuildProfilesPanel::Refresh() {
		_projectPath = ProjectOps::GetMountedProjectPath();
		_views.clear();
		if (_projectPath.empty()) {
			return;
		}

		Vector<str> roster{GetMemoryResource(MemTag::Editor)};
		BuildProfileOps::ListProfiles(_projectPath, roster);

		Config results;
		results.Load(Path(_projectPath + "/Library/BuildResults.ini"));

		for (const str& name : roster) {
			ProfileView view;
			view.name = name;

			BuildProfile stored;
			BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(_projectPath, name), stored);
			view.targetId = stored.target;

			Vector<BuildSettingDesc> coreSchema{GetMemoryResource(MemTag::Editor)};
			BuildProfileOps::GetCoreSettings(coreSchema);
			ResolvedProfile probe;
			BuildProfileOps::ResolveProfile(_projectPath, name, coreSchema, probe);
			str targetId = probe.target.empty() ? BuildTargetRegistry::GetDefaultTargetId() : probe.target;

			BuildTargetRegistry::GetSchemaFor(targetId, view.schema);
			BuildProfileOps::ResolveProfile(_projectPath, name, view.schema, view.resolved);

			const IBuildTarget* target = BuildTargetRegistry::Find(targetId);
			view.targetKnown = target != nullptr;
			view.targetDisplay = target ? str(target->GetDisplayName()) : targetId;
			view.available = target ? target->IsAvailable(view.unavailableReason) : false;
			if (!target) {
				view.unavailableReason = "no build target named '" + targetId + "' is registered on this machine";
			}

			const ResolvedSetting* included = view.resolved.Find("includeInBuildAll");
			view.includeInBuildAll = !included || included->value != "false";

			BuildProfileOps::ValidateProfile(_projectPath, view.resolved, view.schema, view.issues);
			if (target) {
				target->Validate(_projectPath, view.resolved, view.issues);
			} else {
				view.issues.push_back(BuildIssue{BuildIssueSeverity::Warning, name, {}, view.unavailableReason});
			}
			for (const BuildIssue& issue : view.issues) {
				if (issue.severity == BuildIssueSeverity::Error) {
					++view.errors;
				} else {
					++view.warnings;
				}
			}

			view.lastResult = results.GetString(name, "result");
			view.lastReason = results.GetString(name, "reason");
			view.lastOutput = results.GetString(name, "outputPath");
			view.lastTimeUnix = results.GetI64(name, "timeUnix", 0);
			view.lastDurationSeconds = results.GetF64(name, "durationSeconds", 0.0);
			view.lastArtifactBytes = static_cast<u64>(results.GetI64(name, "artifactBytes", 0));
			view.lastBuildNumber = static_cast<u32>(results.GetI64(name, "buildNumber", 0));

			_views.push_back(std::move(view));
		}

		if (Selected() == nullptr) {
			selectedProfile = _views.empty() ? str{} : _views.front().name;
		}
	}

	const BuildProfilesPanel::ProfileView* BuildProfilesPanel::Selected() const {
		for (const ProfileView& view : _views) {
			if (view.name == selectedProfile) {
				return &view;
			}
		}
		return nullptr;
	}

	str BuildProfilesPanel::GetSharedSelection() {
		Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
		PanelManager::GetOpenPanels(panels);
		const TypeInfo* type = BuildProfilesPanel::GetReflectType();
		for (Handle<EditorPanel> handle : panels) {
			EditorPanel* panel = handle.Get();
			if (panel && panel->GetType() == type) {
				return static_cast<BuildProfilesPanel*>(panel)->selectedProfile;
			}
		}
		return {};
	}

	void BuildProfilesPanel::WriteSetting(std::string_view key, std::string_view value) {
		if (selectedProfile.empty()) {
			return;
		}
		str path = BuildProfileOps::GetProfilePath(_projectPath, selectedProfile);
		BuildProfile profile;
		if (!BuildProfileOps::LoadProfile(path, profile)) {
			return;
		}
		profile.SetString(key, value);
		BuildProfileOps::SaveProfile(path, profile);
		_dirty = true;
	}

	void BuildProfilesPanel::WriteList(std::string_view key, const Vector<str>& items) {
		if (selectedProfile.empty()) {
			return;
		}
		str path = BuildProfileOps::GetProfilePath(_projectPath, selectedProfile);
		BuildProfile profile;
		if (!BuildProfileOps::LoadProfile(path, profile)) {
			return;
		}
		profile.SetList(key, items);
		BuildProfileOps::SaveProfile(path, profile);
		_dirty = true;
	}

	void BuildProfilesPanel::ClearSetting(std::string_view key) {
		if (selectedProfile.empty()) {
			return;
		}
		str path = BuildProfileOps::GetProfilePath(_projectPath, selectedProfile);
		BuildProfile profile;
		if (!BuildProfileOps::LoadProfile(path, profile)) {
			return;
		}
		if (profile.Remove(key)) {
			BuildProfileOps::SaveProfile(path, profile);
		}
		_dirty = true;
	}

	void BuildProfilesPanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		Refresh();
		_dirty = false;

		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Stretch;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(root, rootStyle);

		UI::ElementHandle body = context.CreateElement();
		context.AppendChild(root, body);
		UI::Style bodyStyle;
		bodyStyle.flexDirection = UI::FlexDirection::Row;
		bodyStyle.alignItems = UI::Align::Stretch;
		bodyStyle.height = UI::Length::Pixel(0.0f);
		bodyStyle.flexGrow = 1.0f;
		context.SetStyle(body, bodyStyle);

		UI::ElementHandle listColumn = context.CreateElement();
		context.AppendChild(body, listColumn);
		UI::Style listStyle;
		listStyle.flexDirection = UI::FlexDirection::Column;
		listStyle.width = UI::Length::Pixel(LIST_WIDTH);
		listStyle.flexShrink = 1.0f;
		listStyle.minWidth = UI::Length::Pixel(120.0f);
		context.SetStyle(listColumn, listStyle);

		UI::ElementHandle toolbar = Row(context, listColumn);
		UI::Style toolbarStyle = context.GetStyle(toolbar);
		toolbarStyle.padding = {RowPadX(), 4.0f, RowPadX(), 4.0f};
		context.SetStyle(toolbar, toolbarStyle);

		UI::Context* contextPtr = &context;
		GlyphButton(context, toolbar, EditorIcons::Glyph::Add, "+",
					"New profile, inheriting the selected one", [this, contextPtr]() {
						str parent = selectedProfile;
						SceneFileFlows::OpenTextInputPopup(*contextPtr, "New Build Profile", "New Profile", [this, parent](const str& name) {
												   if (BuildProfileOps::CreateProfile(_projectPath, name, parent)) {
													   selectedProfile = name;
													   _dirty = true;
												   } }, "Create");
					});
		GlyphButton(context, toolbar, EditorIcons::Glyph::Duplicate, "Duplicate",
					"Copy the selected profile (including its overrides)", [this, contextPtr]() {
						DuplicateProfileFlow(*contextPtr, selectedProfile);
					});
		GlyphButton(context, toolbar, EditorIcons::Glyph::Delete, "Delete",
					"Delete the selected profile", [this, contextPtr]() {
						DeleteProfileFlow(*contextPtr, selectedProfile);
					});
		UI::ElementHandle toolbar2 = Row(context, listColumn);
		UI::Style toolbar2Style = context.GetStyle(toolbar2);
		toolbar2Style.padding = {RowPadX(), 0.0f, RowPadX(), 4.0f};
		context.SetStyle(toolbar2, toolbar2Style);

		GlyphButton(context, toolbar2, EditorIcons::Glyph::Import, "Import...",
					"Copy a .buildprofile into this project", [this, contextPtr]() {
						SceneFileFlows::OpenTextInputPopup(*contextPtr, "Import a .buildprofile", _projectPath + "/", [this](const str& source) {
												   str imported;
												   Vector<BuildIssue> issues{GetMemoryResource(MemTag::Editor)};
												   if (BuildProfileOps::ImportProfile(_projectPath, source, imported, issues)) {
													   selectedProfile = imported;
												   }
												   for (const BuildIssue& issue : issues) {
													   SLT_LOG_WARN("Build", "{}", issue.message);
												   }
												   _dirty = true; }, "Import");
					});
		GlyphButton(context, toolbar2, EditorIcons::Glyph::Export, "Export...",
					"Write the selected profile out as a file", [this, contextPtr]() {
						if (selectedProfile.empty()) {
							return;
						}
						str name = selectedProfile;
						SceneFileFlows::OpenTextInputPopup(*contextPtr, "Export '" + name + "' to", _projectPath + "/" + name + ".buildprofile", [this, name](const str& destination) { BuildProfileOps::ExportProfile(_projectPath, name, destination); }, "Export");
					});
		UI::Widgets::Spacer(context, toolbar2);
		GlyphButton(context, toolbar2, EditorIcons::Glyph::MoveUp, "^",
					"Move up (Build All runs the list in order)", [this]() { MoveSelected(-1); });
		GlyphButton(context, toolbar2, EditorIcons::Glyph::MoveDown, "v",
					"Move down", [this]() { MoveSelected(1); });

		ListViewDesc roster;
		roster.layout = ListLayout::Rows;
		roster.mode = ListSelectionMode::Single;
		roster.onSelectionChanged = [this]() { TakeSelectionFromList(); };
		roster.onDelete = [this, contextPtr]() { DeleteProfileFlow(*contextPtr, selectedProfile); };
		roster.onDuplicate = [this, contextPtr]() { DuplicateProfileFlow(*contextPtr, selectedProfile); };
		roster.onRename = [this, contextPtr](u64 row) {
			str name = NameOfRow(row);
			if (name.empty()) {
				return false;
			}
			RenameProfileFlow(*contextPtr, name);
			return true;
		};
		roster.onBuildTrailing = [this](UI::Context& context, UI::ElementHandle host) {
			BuildListEmptyState(context, host);
		};
		_list.Build(this, context, listColumn, std::move(roster));

		UI::Widgets::ScrollViewParts paneScroll = UI::Widgets::ScrollView(context, body);
		UI::Style paneScrollStyle = context.GetStyle(paneScroll.root);
		paneScrollStyle.flexGrow = 1.0f;
		paneScrollStyle.width = UI::Length::Pixel(0.0f);
		context.SetStyle(paneScroll.root, paneScrollStyle);
		_paneViewport = paneScroll.viewport;

		UI::ElementHandle footer = Row(context, root);
		UI::Style footerStyle = context.GetStyle(footer);
		footerStyle.padding = {RowPadX(), 5.0f, RowPadX(), 5.0f};
		context.SetStyle(footer, footerStyle);
		context.AddClass(footer, EditorTheme::Class::StatusBar);

		_stepLabel = Label(context, footer, "", BodyFont(), TextDim());
		UI::Widgets::Spacer(context, footer);
		_resultLabel = Label(context, footer, "", SmallFont(), TextFaint());
		UI::Style resultStyle = context.GetStyle(_resultLabel);
		resultStyle.margin = {0.0f, 0.0f, 8.0f, 0.0f};
		context.SetStyle(_resultLabel, resultStyle);

		_buildAllButton = Button(context, footer, "Build All", [this, contextPtr]() {
			RequestBuild(*contextPtr, true, false);
		});
		context.SetTooltip(_buildAllButton, "Build all included profiles (in list order)");
		_buildButton = Button(context, footer, "Build", [this, contextPtr]() {
			if (BuildPipeline* pipeline = BuildPipeline::GetActive(); pipeline && pipeline->IsBusy()) {
				pipeline->Cancel();
				return;
			}
			RequestBuild(*contextPtr, false, false);
		});
		_runButton = Button(context, footer, "Build And Run", [this, contextPtr]() {
			RequestBuild(*contextPtr, false, true);
		});
		context.SetTooltip(_runButton, EditorInputOps::ChordLabel(EditorInputOps::Shortcut::BuildAndRun));

		RebuildList(context);
		RebuildPane(context);
		RefreshFooter(context);
	}

	void BuildProfilesPanel::OnTick(f32) {
		UI::Context* context = GetPanelContext();
		if (!context || !context->IsAlive(_paneViewport)) {
			return;
		}
		if (_dirty) {
			_dirty = false;
			_paneDirty = false;
			Refresh();
			RebuildList(*context);
			RebuildPane(*context);
		} else if (_paneDirty) {
			_paneDirty = false;
			RebuildPane(*context);
		}
		RefreshFooter(*context);
	}

	u64 BuildProfilesPanel::RowIdOf(std::string_view name) {
		return StringHash(name).GetValue();
	}

	str BuildProfilesPanel::NameOfRow(u64 id) const {
		for (const ProfileView& view : _views) {
			if (RowIdOf(view.name) == id) {
				return view.name;
			}
		}
		return {};
	}

	void BuildProfilesPanel::TakeSelectionFromList() {
		std::span<const u64> selected = _list.GetSelected();
		str picked = selected.empty() ? str{} : NameOfRow(selected.back());
		if (picked == selectedProfile) {
			return;
		}
		selectedProfile = std::move(picked);
		_paneDirty = true;
	}

	void BuildProfilesPanel::PushSelectionToList() {
		Vector<u64> ids{GetMemoryResource(MemTag::Editor)};
		if (!selectedProfile.empty()) {
			ids.push_back(RowIdOf(selectedProfile));
		}
		_list.SetSelection(ids);
	}

	void BuildProfilesPanel::RebuildList(UI::Context& context) {
		Vector<ListRow> listRows{GetMemoryResource(MemTag::Editor)};
		for (size_t index = 0; index < _views.size(); ++index) {
			const ProfileView& view = _views[index];
			ListRow row;
			row.id = RowIdOf(view.name);
			row.label = view.name;
			str name = view.name;
			row.onRightClick = [this, name](UI::Context& context) { OpenRowMenu(context, name); };
			row.onDecorate = [this, index](UI::Context& context, const UI::Widgets::TreeRowParts& parts) {
				DecorateRow(context, _views[index], parts);
			};
			listRows.push_back(std::move(row));
		}
		_list.SetRows(listRows);
		PushSelectionToList();
	}

	void BuildProfilesPanel::DecorateRow(UI::Context& context, const ProfileView& view,
										 const UI::Widgets::TreeRowParts& parts) {
		bool included = view.includeInBuildAll;
		str name = view.name;
		if (context.IsAlive(parts.arrow)) {
			UI::Style eyeStyle = context.GetStyle(parts.arrow);
			eyeStyle.width = UI::Length::Pixel(EditorTheme::GetMetric(MetricToken::TreeGutter));
			eyeStyle.flexShrink = 0.0f;
			eyeStyle.cursor = SystemCursor::Hand;
			context.SetStyle(parts.arrow, eyeStyle);
			EditorIcons::SetGlyphOrText(context, parts.arrow,
										included ? EditorIcons::Glyph::Eye : EditorIcons::Glyph::EyeSlash,
										included ? "*" : "-", included ? TextDim() : TextFaint(),
										EditorIcons::GutterSizePx(), RowFont());
			context.SetTooltip(parts.arrow, included ? "Included in Build All" : "Excluded from Build All");
			context.SetHoverable(parts.arrow, true);
			context.Events(parts.arrow).onClick = [this, name, included]() {
				selectedProfile = name;
				WriteSetting("includeInBuildAll", included ? "false" : "true");
			};
		}

		UI::Style labelStyle = context.GetStyle(parts.labelElement);
		labelStyle.flexGrow = 1.0f;
		labelStyle.margin = {4.0f, 0.0f, 0.0f, 0.0f};
		context.SetStyle(parts.labelElement, labelStyle);
		context.AddClass(parts.labelElement, EditorTheme::Class::RowText);
		if (!included) {
			context.SetText(parts.labelElement, Font(), RowFont(), TextFaint(), view.name);
		}

		str chipText = view.errors > 0	   ? std::format("{} err", view.errors)
					   : view.warnings > 0 ? std::format("{} warn", view.warnings)
										   : str("ok");
		Color chipColor = view.errors > 0 ? StatusError() : (view.warnings > 0 ? StatusWarn() : StatusOk());
		Label(context, parts.row, chipText, SmallFont(), chipColor);
	}

	void BuildProfilesPanel::BuildListEmptyState(UI::Context& context, UI::ElementHandle host) {
		if (!_views.empty()) {
			return;
		}
		Label(context, host, _projectPath.empty() ? "no project" : "no profiles", SmallFont(), TextFaint());
	}

	void BuildProfilesPanel::DeleteProfileFlow(UI::Context& context, const str& name) {
		if (name.empty()) {
			return;
		}
		str doomed = name;
		Vector<str> children{GetMemoryResource(MemTag::Editor)};
		BuildProfileOps::GetProfileChildren(_projectPath, doomed, children);
		Vector<str> details{GetMemoryResource(MemTag::Editor)};
		for (const str& child : children) {
			details.push_back("'" + child + "' will inherit its parent instead");
		}
		SceneFileFlows::OpenDetailedConfirmPopup(
			context, "Delete build profile '" + doomed + "'?", {details.data(), details.size()},
			"Delete", [this, doomed]() {
				if (BuildProfileOps::DeleteProfile(_projectPath, doomed)) {
					selectedProfile.clear();
					_dirty = true;
				}
			},
			"", {});
	}

	void BuildProfilesPanel::DuplicateProfileFlow(UI::Context& context, const str& name) {
		if (name.empty()) {
			return;
		}
		str source = name;
		SceneFileFlows::OpenTextInputPopup(context, "Duplicate '" + source + "'", source + " Copy", [this, source](const str& copy) {
											   if (BuildProfileOps::DuplicateProfile(_projectPath, source, copy)) {
												   selectedProfile = copy;
												   _dirty = true;
											   } }, "Duplicate");
	}

	void BuildProfilesPanel::RenameProfileFlow(UI::Context& context, const str& name) {
		if (name.empty()) {
			return;
		}
		str source = name;
		SceneFileFlows::OpenTextInputPopup(context, "Rename '" + source + "'", source, [this, source](const str& renamed) {
											   if (BuildProfileOps::RenameProfile(_projectPath, source, renamed)) {
												   selectedProfile = renamed;
												   _dirty = true;
											   } }, "Rename");
	}

	void BuildProfilesPanel::MoveSelected(i32 delta) {
		Vector<str> roster{GetMemoryResource(MemTag::Editor)};
		BuildProfileOps::ListProfiles(_projectPath, roster);
		for (size_t i = 0; i < roster.size(); ++i) {
			if (roster[i] != selectedProfile) {
				continue;
			}
			i64 target = static_cast<i64>(i) + delta;
			if (target < 0 || target >= static_cast<i64>(roster.size())) {
				return;
			}
			std::swap(roster[i], roster[static_cast<size_t>(target)]);
			BuildProfileOps::WriteProfileRoster(_projectPath, roster);
			_dirty = true;
			return;
		}
	}

	void BuildProfilesPanel::OpenRowMenu(UI::Context& context, const str& name) {
		UI::Context* contextPtr = &context;
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		items.push_back({"Build", "", true, false, [this, contextPtr]() { RequestBuild(*contextPtr, false, false); }});
		items.push_back({"Build And Run", EditorInputOps::ChordLabel(EditorInputOps::Shortcut::BuildAndRun), true, false, [this, contextPtr]() {
							 RequestBuild(*contextPtr, false, true);
						 }});
		items.push_back({"Rename...", EditorInputOps::ChordLabel(EditorInputOps::Shortcut::RenameSelection), true, true,
						 [this, contextPtr, name]() { RenameProfileFlow(*contextPtr, name); }});
		items.push_back({"Duplicate...", EditorInputOps::ChordLabel(EditorInputOps::Shortcut::DuplicateSelection), true,
						 false, [this, contextPtr, name]() { DuplicateProfileFlow(*contextPtr, name); }});
		items.push_back({"Export...", "", true, true, [this, contextPtr, name]() {
							 SceneFileFlows::OpenTextInputPopup(*contextPtr, "Export '" + name + "' to", _projectPath + "/" + name + ".buildprofile", [this, name](const str& destination) { BuildProfileOps::ExportProfile(_projectPath, name, destination); }, "Export");
						 }});
		items.push_back({"Reveal Output Folder", "", true, false, [this, name]() {
							 const ProfileView* view = Selected();
							 str output = view && !view->lastOutput.empty()
											  ? view->lastOutput
											  : BuildPipeline::GetStagingDirectory(_projectPath,
																				   view ? view->resolved : ResolvedProfile{},
																				   {});
							 if (!AssetBrowserOps::RevealInExplorer(Path(output))) {
								 SLT_LOG_INFO("Build", "'{}' hasn't been built yet ({} doesn't exist)", name, output);
							 }
						 }});
		items.push_back({"Delete", EditorInputOps::ChordLabel(EditorInputOps::Shortcut::DeleteSelection), true, true,
						 [this, contextPtr, name]() { DeleteProfileFlow(*contextPtr, name); }});
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), {items.data(), items.size()}, Font(), BodyFont());
	}

	void BuildProfilesPanel::RebuildPane(UI::Context& context) {
		if (!context.IsAlive(_paneViewport)) {
			return;
		}
		context.ClearChildren(_paneViewport);

		const ProfileView* view = Selected();
		if (!view) {
			BuildEmptyState(context, _paneViewport);
			return;
		}
		BuildGeneralSection(context, _paneViewport, *view);
		BuildOptionsSection(context, _paneViewport, *view);
		BuildScenesSection(context, _paneViewport, *view);
		BuildValidationSection(context, _paneViewport, *view);
		BuildStepsSection(context, _paneViewport);
		BuildLastResultSection(context, _paneViewport, *view);
	}

	void BuildProfilesPanel::BuildEmptyState(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle card = Section(context, parent, "Build Profiles");
		UI::ElementHandle body = context.CreateElement();
		context.AppendChild(card, body);
		UI::Style bodyStyle;
		bodyStyle.flexDirection = UI::FlexDirection::Column;
		bodyStyle.padding = {8.0f, 4.0f, 8.0f, 8.0f};
		context.SetStyle(body, bodyStyle);

		if (_projectPath.empty()) {
			Label(context, body, "No project is open.", BodyFont(), TextDim());
			return;
		}
		Label(context, body, "This project has no build profiles.", BodyFont(), TextDim());
		Label(context, body, "Profile is a file in BuildProfiles/ (it belongs in version control).",
			  SmallFont(), TextFaint());
		UI::ElementHandle buttons = Row(context, body, UI::Align::Start);
		UI::Style buttonsStyle = context.GetStyle(buttons);
		buttonsStyle.margin = {0.0f, 8.0f, 0.0f, 0.0f};
		context.SetStyle(buttons, buttonsStyle);
		Button(context, buttons, "Create Default Profile", [this]() {
			if (BuildProfileOps::CreateProfile(_projectPath, "Default")) {
				selectedProfile = "Default";
				_dirty = true;
			}
		});
		UI::Context* contextPtr = &context;
		Button(context, buttons, "Import...", [this, contextPtr]() {
			SceneFileFlows::OpenTextInputPopup(*contextPtr, "Import a .buildprofile", _projectPath + "/", [this](const str& source) {
												   str imported;
												   Vector<BuildIssue> issues{GetMemoryResource(MemTag::Editor)};
												   if (BuildProfileOps::ImportProfile(_projectPath, source, imported, issues)) {
													   selectedProfile = imported;
												   }
												   for (const BuildIssue& issue : issues) {
													   SLT_LOG_WARN("Build", "{}", issue.message);
												   }
												   _dirty = true; }, "Import");
		});
	}

	UI::ElementHandle BuildProfilesPanel::BuildFieldRow(UI::Context& context, UI::ElementHandle parent,
														const ProfileView& view, const BuildSettingDesc& desc,
														const ResolvedSetting& setting) {
		UI::ElementHandle row = Row(context, parent);
		UI::Style rowStyle = context.GetStyle(row);
		rowStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		context.SetStyle(row, rowStyle);

		bool overridden = setting.source == view.name;
		UI::ElementHandle label = context.CreateElement();
		context.AppendChild(row, label);
		context.AddClass(label, EditorTheme::Class::FieldLabel);
		UI::Style labelStyle;
		labelStyle.width = UI::Length::Pixel(FIELD_LABEL_WIDTH);
		labelStyle.flexShrink = 0.0f;
		context.SetStyle(label, labelStyle);
		context.SetHoverable(label, false);
		context.SetText(label, Font(), BodyFont(), overridden ? TextNormal() : TextDim(), desc.label);
		str tooltip = desc.help;
		if (!overridden) {
			tooltip += setting.source.empty() ? "\n(the default, no profile in this chain sets it)"
											  : "\n(inherited from '" + setting.source + "')";
		}
		context.SetTooltip(label, tooltip);

		UI::ElementHandle slot = context.CreateElement();
		context.AppendChild(row, slot);
		UI::Style slotStyle;
		slotStyle.flexDirection = UI::FlexDirection::Row;
		slotStyle.alignItems = UI::Align::Center;
		slotStyle.flexGrow = 1.0f;
		context.SetStyle(slot, slotStyle);

		UI::ElementHandle marker = context.CreateElement();
		context.AppendChild(row, marker);
		UI::Style markerStyle;
		markerStyle.width = UI::Length::Pixel(76.0f);
		markerStyle.flexShrink = 1.0f;
		markerStyle.margin = {6.0f, 0.0f, 0.0f, 0.0f};
		context.SetStyle(marker, markerStyle);
		if (overridden) {
			str key = str(desc.key);
			UI::ElementHandle revert = Button(context, marker, "Revert", [this, key]() { ClearSetting(key); });
			context.SetTooltip(revert, "Remove this key from '" + view.name + "' and inherit it again");
		} else if (!setting.source.empty()) {
			UI::ElementHandle from = Label(context, marker, "from " + setting.source, SmallFont(), TextFaint());
			context.AddClass(from, EditorTheme::Class::RowText);
			context.SetHoverable(from, false);
		}
		return slot;
	}

	void BuildProfilesPanel::AddExpandedPreview(UI::Context& context, UI::ElementHandle parent,
												const ResolvedSetting& setting) {
		if (setting.rawValue.find("${") == str::npos) {
			return;
		}
		str text;
		if (setting.value != setting.rawValue) {
			text = "-> " + setting.value;
		} else if (setting.rawValue.find("${AUTO}") != str::npos) {
			text = "-> the next build counter value, taken when the build starts";
		} else {
			text = "-> not resolved on this machine. See Validation";
		}
		UI::ElementHandle row = Row(context, parent);
		UI::Style style = context.GetStyle(row);
		style.padding = {8.0f + FIELD_LABEL_WIDTH, 0.0f, 8.0f, 3.0f};
		context.SetStyle(row, style);
		Label(context, row, text, SmallFont(), TextFaint());
	}

	void BuildProfilesPanel::BuildGeneralSection(UI::Context& context, UI::ElementHandle parent,
												 const ProfileView& view) {
		UI::ElementHandle card = Section(context, parent, "General");
		UI::Context* contextPtr = &context;

		UI::ElementHandle nameRow = Row(context, card);
		UI::Style nameRowStyle = context.GetStyle(nameRow);
		nameRowStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		context.SetStyle(nameRow, nameRowStyle);
		UI::ElementHandle nameLabel = context.CreateElement();
		context.AppendChild(nameRow, nameLabel);
		context.AddClass(nameLabel, EditorTheme::Class::FieldLabel);
		UI::Style nameLabelStyle;
		nameLabelStyle.width = UI::Length::Pixel(FIELD_LABEL_WIDTH);
		nameLabelStyle.flexShrink = 0.0f;
		context.SetStyle(nameLabel, nameLabelStyle);
		context.SetHoverable(nameLabel, false);
		context.SetText(nameLabel, Font(), BodyFont(), TextNormal(), "Name");
		Label(context, nameRow, view.name, BodyFont(), TextNormal());
		UI::Widgets::Spacer(context, nameRow);
		str current = view.name;
		Button(context, nameRow, "Rename...", [this, contextPtr, current]() {
			SceneFileFlows::OpenTextInputPopup(*contextPtr, "Rename '" + current + "'", current, [this, current](const str& renamed) {
												   if (BuildProfileOps::RenameProfile(_projectPath, current, renamed)) {
													   selectedProfile = renamed;
													   _dirty = true;
												   } }, "Rename");
		});

		UI::ElementHandle inheritsRow = Row(context, card);
		UI::Style inheritsRowStyle = context.GetStyle(inheritsRow);
		inheritsRowStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		context.SetStyle(inheritsRow, inheritsRowStyle);
		UI::ElementHandle inheritsLabel = context.CreateElement();
		context.AppendChild(inheritsRow, inheritsLabel);
		context.AddClass(inheritsLabel, EditorTheme::Class::FieldLabel);
		UI::Style inheritsLabelStyle;
		inheritsLabelStyle.width = UI::Length::Pixel(FIELD_LABEL_WIDTH);
		inheritsLabelStyle.flexShrink = 0.0f;
		context.SetStyle(inheritsLabel, inheritsLabelStyle);
		context.SetHoverable(inheritsLabel, false);
		context.SetText(inheritsLabel, Font(), BodyFont(), TextNormal(), "Inherits");

		BuildProfile stored;
		BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(_projectPath, view.name), stored);

		Vector<str> parentNames{GetMemoryResource(MemTag::Editor)};
		parentNames.push_back("(none)");
		Vector<std::string_view> parentItems{GetMemoryResource(MemTag::Editor)};
		u32 selectedParent = 0;
		for (const ProfileView& candidate : _views) {
			if (candidate.name == view.name) {
				continue;
			}
			bool wouldCycle = false;
			for (const str& link : candidate.resolved.chain) {
				wouldCycle = wouldCycle || link == view.name;
			}
			if (wouldCycle) {
				continue;
			}
			if (candidate.name == stored.inherits) {
				selectedParent = static_cast<u32>(parentNames.size());
			}
			parentNames.push_back(candidate.name);
		}
		for (const str& entry : parentNames) {
			parentItems.push_back(entry);
		}
		UI::Widgets::DropdownDesc inherits;
		inherits.items = {parentItems.data(), parentItems.size()};
		inherits.selected = selectedParent;
		inherits.font = Font();
		inherits.fontSizePx = BodyFont();
		inherits.onSelect = [this, parentNames](u32 index) {
			str path = BuildProfileOps::GetProfilePath(_projectPath, selectedProfile);
			BuildProfile profile;
			if (!BuildProfileOps::LoadProfile(path, profile)) {
				return;
			}
			profile.inherits = index == 0 || index >= parentNames.size() ? str{} : parentNames[index];
			BuildProfileOps::SaveProfile(path, profile);
			_dirty = true;
		};
		UI::Widgets::Dropdown(context, inheritsRow, inherits);

		Vector<str> targetIds{GetMemoryResource(MemTag::Editor)};
		BuildTargetRegistry::GetTargetIds(targetIds);
		Vector<str> targetLabels{GetMemoryResource(MemTag::Editor)};
		Vector<std::string_view> targetItems{GetMemoryResource(MemTag::Editor)};
		u32 selectedTarget = 0;
		str resolvedTarget = view.resolved.target.empty() ? BuildTargetRegistry::GetDefaultTargetId()
														  : view.resolved.target;
		for (const str& id : targetIds) {
			const IBuildTarget* target = BuildTargetRegistry::Find(id);
			if (id == resolvedTarget) {
				selectedTarget = static_cast<u32>(targetLabels.size());
			}
			targetLabels.push_back(target ? str(target->GetDisplayName()) : id);
		}
		if (!view.targetKnown && !resolvedTarget.empty()) {
			selectedTarget = static_cast<u32>(targetLabels.size());
			targetIds.push_back(resolvedTarget);
			targetLabels.push_back(resolvedTarget + " (not on this machine)");
		}
		for (const str& entry : targetLabels) {
			targetItems.push_back(entry);
		}

		UI::ElementHandle targetRow = Row(context, card);
		UI::Style targetRowStyle = context.GetStyle(targetRow);
		targetRowStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		context.SetStyle(targetRow, targetRowStyle);
		UI::ElementHandle targetLabel = context.CreateElement();
		context.AppendChild(targetRow, targetLabel);
		context.AddClass(targetLabel, EditorTheme::Class::FieldLabel);
		UI::Style targetLabelStyle;
		targetLabelStyle.width = UI::Length::Pixel(FIELD_LABEL_WIDTH);
		targetLabelStyle.flexShrink = 0.0f;
		context.SetStyle(targetLabel, targetLabelStyle);
		context.SetHoverable(targetLabel, false);
		bool targetOverridden = view.resolved.targetSource == view.name;
		context.SetText(targetLabel, Font(), BodyFont(), targetOverridden ? TextNormal() : TextDim(), "Target");
		UI::Widgets::DropdownDesc targetDropdown;
		targetDropdown.items = {targetItems.data(), targetItems.size()};
		targetDropdown.selected = selectedTarget;
		targetDropdown.font = Font();
		targetDropdown.fontSizePx = BodyFont();
		targetDropdown.onSelect = [this, targetIds](u32 index) {
			if (index >= targetIds.size()) {
				return;
			}
			str path = BuildProfileOps::GetProfilePath(_projectPath, selectedProfile);
			BuildProfile profile;
			if (!BuildProfileOps::LoadProfile(path, profile)) {
				return;
			}
			profile.target = targetIds[index];
			BuildProfileOps::SaveProfile(path, profile);
			_dirty = true;
		};
		UI::Widgets::Dropdown(context, targetRow, targetDropdown);
		if (!targetOverridden && !view.resolved.targetSource.empty()) {
			UI::Widgets::Spacer(context, targetRow);
			Label(context, targetRow, "from " + view.resolved.targetSource, SmallFont(), TextFaint());
		}
		if (!view.available) {
			Label(context, card, view.unavailableReason, SmallFont(), StatusWarn());
		}

		for (const BuildSettingDesc& desc : view.schema) {
			if (desc.key != "outputPath" && desc.key != "executableName" && desc.key != "version" &&
				desc.key != "buildNumber") {
				continue;
			}
			const ResolvedSetting* setting = view.resolved.Find(desc.key);
			if (!setting) {
				continue;
			}
			UI::ElementHandle slot = BuildFieldRow(context, card, view, desc, *setting);
			UI::Widgets::TextFieldDesc field;
			field.text = setting->rawValue;
			field.font = Font();
			field.fontSizePx = BodyFont();
			str key = str(desc.key);
			field.onCommit = [this, key](std::string_view text) { WriteSetting(key, text); };
			UI::ElementHandle element = UI::Widgets::TextField(context, slot, field);
			UI::Style elementStyle = context.GetStyle(element);
			elementStyle.flexGrow = 1.0f;
			context.SetStyle(element, elementStyle);
			AddExpandedPreview(context, card, *setting);
		}
	}

	void BuildProfilesPanel::BuildOptionsSection(UI::Context& context, UI::ElementHandle parent,
												 const ProfileView& view) {
		UI::ElementHandle card = Section(context, parent, "Build Options");
		for (const BuildSettingDesc& desc : view.schema) {
			if (IsDrawnOutsideBuildOptions(desc.key)) {
				continue;
			}
			const ResolvedSetting* setting = view.resolved.Find(desc.key);
			if (!setting) {
				continue;
			}
			str key = str(desc.key);

			if (IsListType(desc.type)) {
				UI::ElementHandle slot = BuildFieldRow(context, card, view, desc, *setting);
				Vector<str> own{GetMemoryResource(MemTag::Editor)};
				str inheritedText;
				for (size_t i = 0; i < setting->rawItems.size(); ++i) {
					str source = i < setting->itemSources.size() ? setting->itemSources[i] : str{};
					if (desc.merge == BuildMergePolicy::Union && source != view.name) {
						inheritedText += inheritedText.empty() ? str{} : str(" ");
						inheritedText += setting->rawItems[i] + " (" + (source.empty() ? str("default") : source) + ")";
						continue;
					}
					own.push_back(setting->rawItems[i]);
				}
				str joined;
				for (const str& entry : own) {
					joined += joined.empty() ? str{} : str(";");
					joined += entry;
				}
				UI::Widgets::TextFieldDesc field;
				field.text = joined;
				field.font = Font();
				field.fontSizePx = BodyFont();
				field.onCommit = [this, key](std::string_view text) {
					Vector<str> items{GetMemoryResource(MemTag::Editor)};
					size_t start = 0;
					while (start <= text.size()) {
						size_t end = text.find(';', start);
						if (end == std::string_view::npos) {
							end = text.size();
						}
						std::string_view piece = text.substr(start, end - start);
						if (!piece.empty()) {
							items.push_back(str(piece));
						}
						start = end + 1;
					}
					WriteList(key, items);
				};
				UI::ElementHandle element = UI::Widgets::TextField(context, slot, field);
				UI::Style elementStyle = context.GetStyle(element);
				elementStyle.flexGrow = 1.0f;
				context.SetStyle(element, elementStyle);
				if (!inheritedText.empty()) {
					UI::ElementHandle chips = Row(context, card);
					UI::Style chipsStyle = context.GetStyle(chips);
					chipsStyle.padding = {8.0f + FIELD_LABEL_WIDTH, 0.0f, 8.0f, 3.0f};
					context.SetStyle(chips, chipsStyle);
					Label(context, chips, "inherited: " + inheritedText, SmallFont(), TextFaint());
				}
				continue;
			}

			if (desc.type == BuildSettingType::Bool) {
				UI::ElementHandle slot = BuildFieldRow(context, card, view, desc, *setting);
				UI::Widgets::Checkbox(context, slot, setting->value == "true", [this, key](bool value) {
					WriteSetting(key, value ? "true" : "false");
				});
				continue;
			}

			if (desc.type == BuildSettingType::Enum && !desc.enumValues.empty()) {
				UI::ElementHandle slot = BuildFieldRow(context, card, view, desc, *setting);
				Vector<std::string_view> items{GetMemoryResource(MemTag::Editor)};
				u32 selected = 0;
				for (size_t i = 0; i < desc.enumValues.size(); ++i) {
					if (desc.enumValues[i] == setting->value) {
						selected = static_cast<u32>(i);
					}
					items.push_back(desc.enumValues[i]);
				}
				UI::Widgets::DropdownDesc dropdown;
				dropdown.items = {items.data(), items.size()};
				dropdown.selected = selected;
				dropdown.font = Font();
				dropdown.fontSizePx = BodyFont();
				Vector<str> values{GetMemoryResource(MemTag::Editor)};
				for (const str& value : desc.enumValues) {
					values.push_back(value);
				}
				dropdown.onSelect = [this, key, values](u32 index) {
					if (index < values.size()) {
						WriteSetting(key, values[index]);
					}
				};
				UI::Widgets::Dropdown(context, slot, dropdown);
				continue;
			}

			UI::ElementHandle slot = BuildFieldRow(context, card, view, desc, *setting);
			UI::Widgets::TextFieldDesc field;
			field.text = setting->rawValue;
			field.font = Font();
			field.fontSizePx = BodyFont();
			field.onCommit = [this, key](std::string_view text) { WriteSetting(key, text); };
			UI::ElementHandle element = UI::Widgets::TextField(context, slot, field);
			UI::Style elementStyle = context.GetStyle(element);
			elementStyle.flexGrow = 1.0f;
			context.SetStyle(element, elementStyle);
			AddExpandedPreview(context, card, *setting);
		}
	}

	void BuildProfilesPanel::BuildScenesSection(UI::Context& context, UI::ElementHandle parent,
												const ProfileView& view) {
		UI::ElementHandle card = Section(context, parent, "Scenes");
		const ResolvedSetting* setting = view.resolved.Find("scenes");
		if (!setting) {
			return;
		}
		bool overridden = setting->source == view.name;

		UI::ElementHandle status = Row(context, card);
		UI::Style statusStyle = context.GetStyle(status);
		statusStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		context.SetStyle(status, statusStyle);
		Vector<str> inProject{GetMemoryResource(MemTag::Editor)};
		BuildProfileOps::GetProjectScenes(_projectPath, inProject);
		Label(context, status,
			  std::format("{} scene(s) in this project, {} in this build", inProject.size(), setting->items.size()),
			  SmallFont(), TextFaint());
		UI::Widgets::Spacer(context, status);
		if (overridden) {
			str key = "scenes";
			Button(context, status, "Revert", [this, key]() { ClearSetting(key); });
		} else if (!setting->source.empty()) {
			Label(context, status, "from " + setting->source, SmallFont(), TextFaint());
		}

		Vector<str> items{GetMemoryResource(MemTag::Editor)};
		for (const str& entry : setting->rawItems) {
			items.push_back(entry);
		}
		for (size_t i = 0; i < items.size(); ++i) {
			UI::ElementHandle row = Row(context, card);
			UI::Style rowStyle = context.GetStyle(row);
			rowStyle.padding = {8.0f, 1.0f, 8.0f, 1.0f};
			context.SetStyle(row, rowStyle);
			Label(context, row, i == 0 ? "boot" : std::format("{}", i), SmallFont(),
				  i == 0 ? StatusOk() : TextFaint());
			UI::ElementHandle label = Label(context, row, items[i], BodyFont(), TextNormal());
			UI::Style labelStyle = context.GetStyle(label);
			labelStyle.flexGrow = 1.0f;
			labelStyle.margin = {6.0f, 0.0f, 0.0f, 0.0f};
			context.SetStyle(label, labelStyle);
			context.AddClass(label, EditorTheme::Class::RowText);

			Vector<str> reordered = items;
			if (i > 0) {
				std::swap(reordered[i], reordered[i - 1]);
				GlyphButton(context, row, EditorIcons::Glyph::MoveUp, "^",
							"Move '" + items[i] + "' earlier (idx 0 boots)",
							[this, reordered]() { WriteList("scenes", reordered); });
			}
			if (i + 1 < items.size()) {
				Vector<str> down = items;
				std::swap(down[i], down[i + 1]);
				GlyphButton(context, row, EditorIcons::Glyph::MoveDown, "v",
							"Move '" + items[i] + "' later",
							[this, down]() { WriteList("scenes", down); });
			}
			Vector<str> without{GetMemoryResource(MemTag::Editor)};
			for (size_t j = 0; j < items.size(); ++j) {
				if (j != i) {
					without.push_back(items[j]);
				}
			}
			GlyphButton(context, row, EditorIcons::Glyph::Remove, "-",
						"Remove '" + items[i] + "' from this profile's scene list",
						[this, without]() { WriteList("scenes", without); });
		}

		UI::ElementHandle buttons = Row(context, card, UI::Align::Start);
		UI::Style buttonsStyle = context.GetStyle(buttons);
		buttonsStyle.padding = {8.0f, 4.0f, 8.0f, 6.0f};
		context.SetStyle(buttons, buttonsStyle);
		UI::Context* contextPtr = &context;
		Button(context, buttons, "Add Scene...", [this, contextPtr, items]() {
			SceneFileFlows::OpenTextInputPopup(*contextPtr, "Add a scene to this build", "Assets/Scenes/", [this, items](const str& path) {
												   Vector<str> next = items;
												   next.push_back(path);
												   WriteList("scenes", next); }, "Add");
		});
		Button(context, buttons, "Add Open Scenes", [this, items]() {
			Vector<str> next = items;
			Vector<Handle<Scene>> open{GetMemoryResource(MemTag::Editor)};
			EditorSceneOps::GetStageScenes(open);
			str prefix = _projectPath + "/";
			for (const Handle<Scene>& scene : open) {
				Path scenePath = EditorSceneOps::GetScenePath(scene);
				str physical = FileSystem::Resolve(scenePath).ToString();
				if (physical.size() > prefix.size() && physical.compare(0, prefix.size(), prefix) == 0) {
					physical = physical.substr(prefix.size());
				}
				if (physical.empty() || physical.find("://") != str::npos) {
					continue;
				}
				bool already = false;
				for (const str& entry : next) {
					already = already || entry == physical;
				}
				if (!already) {
					next.push_back(physical);
				}
			}
			if (next.size() != items.size()) {
				WriteList("scenes", next);
			}
		});
	}

	void BuildProfilesPanel::BuildStepsSection(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle card = Section(context, parent, "Steps");
		Vector<BuildStepDesc> steps{GetMemoryResource(MemTag::Editor)};
		BuildStepRegistry::GetSteps(steps);
		for (size_t i = 0; i < steps.size(); ++i) {
			UI::ElementHandle row = Row(context, card);
			UI::Style rowStyle = context.GetStyle(row);
			rowStyle.padding = {8.0f, 1.0f, 8.0f, 1.0f};
			context.SetStyle(row, rowStyle);
			Label(context, row, std::format("{}. {}", i + 1, steps[i].name), SmallFont(), TextDim());
			if (steps[i].owner != nullptr) {
				UI::Widgets::Spacer(context, row);
				Label(context, row, "from a module", SmallFont(), TextFaint());
			}
		}
	}

	void BuildProfilesPanel::BuildValidationSection(UI::Context& context, UI::ElementHandle parent,
													const ProfileView& view) {
		UI::ElementHandle card = Section(context, parent, "Validation");
		if (view.issues.empty()) {
			UI::ElementHandle row = Row(context, card);
			UI::Style rowStyle = context.GetStyle(row);
			rowStyle.padding = {8.0f, 2.0f, 8.0f, 6.0f};
			context.SetStyle(row, rowStyle);
			Label(context, row, "Nothing to report.", SmallFont(), StatusOk());
			return;
		}
		for (const BuildIssue& issue : view.issues) {
			UI::ElementHandle row = Row(context, card, UI::Align::Start);
			UI::Style rowStyle = context.GetStyle(row);
			rowStyle.padding = {8.0f, 1.0f, 8.0f, 1.0f};
			context.SetStyle(row, rowStyle);
			bool error = issue.severity == BuildIssueSeverity::Error;
			Label(context, row, error ? "error" : "warning", SmallFont(), error ? StatusError() : StatusWarn());
			UI::ElementHandle text = Label(context, row,
										   issue.key.empty() ? issue.message : issue.key + ": " + issue.message,
										   SmallFont(), TextDim());
			UI::Style textStyle = context.GetStyle(text);
			textStyle.flexGrow = 1.0f;
			textStyle.margin = {6.0f, 0.0f, 0.0f, 0.0f};
			context.SetStyle(text, textStyle);
		}
	}

	void BuildProfilesPanel::BuildLastResultSection(UI::Context& context, UI::ElementHandle parent,
													const ProfileView& view) {
		UI::ElementHandle card = Section(context, parent, "Last Result");
		UI::ElementHandle row = Row(context, card, UI::Align::Start);
		UI::Style rowStyle = context.GetStyle(row);
		rowStyle.padding = {8.0f, 2.0f, 8.0f, 6.0f};
		context.SetStyle(row, rowStyle);

		if (view.lastResult.empty()) {
			Label(context, row, "This profile has not been built on this machine.", SmallFont(), TextFaint());
			return;
		}
		Color color = view.lastResult == "Succeeded" ? StatusOk()
					  : view.lastResult == "Skipped" ? StatusWarn()
													 : StatusError();
		Label(context, row, view.lastResult, SmallFont(), color);
		str detail = std::format("build {}, {:.1f}s, {}", view.lastBuildNumber, view.lastDurationSeconds,
								 FormatBytes(view.lastArtifactBytes));
		if (!view.lastReason.empty()) {
			detail += " - " + view.lastReason;
		}
		UI::ElementHandle text = Label(context, row, detail, SmallFont(), TextDim());
		UI::Style textStyle = context.GetStyle(text);
		textStyle.flexGrow = 1.0f;
		textStyle.margin = {6.0f, 0.0f, 0.0f, 0.0f};
		context.SetStyle(text, textStyle);
		if (!view.lastOutput.empty()) {
			str output = view.lastOutput;
			Button(context, row, "Reveal", [output]() { AssetBrowserOps::RevealInExplorer(Path(output)); });
		}
	}

	void BuildProfilesPanel::RequestBuild(UI::Context& context, bool all, bool run) {
		Vector<str> profiles{GetMemoryResource(MemTag::Editor)};
		if (all) {
			BuildProfileOps::ListBuildAllProfiles(_projectPath, profiles);
			if (profiles.empty()) {
				SLT_LOG_WARN("Build", "Build All: no profile in this project is included");
				return;
			}
		} else if (!selectedProfile.empty()) {
			profiles.push_back(selectedProfile);
		} else {
			return;
		}
		BuildFlows::RequestBuild(context, profiles, run);
	}

	void BuildProfilesPanel::RefreshFooter(UI::Context& context) {
		BuildPipeline* pipeline = BuildPipeline::GetActive();
		BuildState state = pipeline ? pipeline->GetState() : BuildState::Idle;
		str status = pipeline ? pipeline->GetStatusLabel() : str{};

		if (state != _lastDrawnState && !(pipeline && pipeline->IsBusy())) {
			_dirty = true;
		}
		if (state == _lastDrawnState && status == _lastDrawnStatus) {
			return;
		}
		_lastDrawnState = state;
		_lastDrawnStatus = status;

		if (context.IsAlive(_stepLabel)) {
			str line = status;
			if (line.empty()) {
				line = state == BuildState::Succeeded	? "build succeeded"
					   : state == BuildState::Failed	? "build FAILED"
					   : state == BuildState::Cancelled ? "build canceled"
														: str{};
			} else if (pipeline && !pipeline->GetCurrentProfileName().empty()) {
				line = pipeline->GetCurrentProfileName() + " - " + line;
			}
			context.SetTextString(_stepLabel, line);
		}
		if (context.IsAlive(_resultLabel) && pipeline) {
			const BuildResult& result = pipeline->GetResult();
			str summary;
			if (result.profiles.size() > 1) {
				summary = std::format("{} of {} profile(s) failed", result.FailureCount(), result.profiles.size());
			}
			context.SetTextString(_resultLabel, summary);
		}
		if (context.IsAlive(_buildButton)) {
			bool busy = pipeline && pipeline->IsBusy();
			const UI::Element* pod = context.GetElement(_buildButton);
			if (pod && pod->firstChild != UI::INVALID_UI_INDEX) {
				context.SetTextString(context.MakeHandle(pod->firstChild), busy ? "Cancel" : "Build");
			}
			for (UI::ElementHandle button : {_buildAllButton, _runButton}) {
				if (!context.IsAlive(button)) {
					continue;
				}
				if (busy) {
					context.AddClass(button, EditorTheme::Class::Disabled);
				} else {
					context.RemoveClass(button, EditorTheme::Class::Disabled);
				}
				context.SetHoverable(button, !busy);
			}
		}
	}

	EditorIcons::Icon BuildProfilesPanel::GetTabIcon() const {
		return EditorIcons::Glyph::PanelBuild;
	}

} // namespace Shalltear
