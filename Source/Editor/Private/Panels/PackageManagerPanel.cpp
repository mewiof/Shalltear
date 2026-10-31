#include "PackageManagerPanel.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"

#include "PackageHost.h"

#include <algorithm>
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

		f32 RowFont() { return EditorTheme::GetMetric(MetricToken::FontRow); }
		f32 BodyFont() { return EditorTheme::GetMetric(MetricToken::FontBody); }
		f32 SmallFont() { return EditorTheme::GetMetric(MetricToken::FontSmall); }
		f32 HeadingFont() { return EditorTheme::GetMetric(MetricToken::FontHeading); }
		f32 RowPadX() { return EditorTheme::GetMetric(MetricToken::RowPaddingX); }

		constexpr f32 LIST_WIDTH = 190.0f;
		constexpr f32 DETAIL_MIN_WIDTH = 250.0f;

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

		UI::ElementHandle Section(UI::Context& context, UI::ElementHandle parent, std::string_view title) {
			UI::ElementHandle card = context.CreateElement();
			context.AppendChild(parent, card);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Column;
			style.width = UI::Length::Percent(1.0f);
			style.margin = {0.0f, 0.0f, 0.0f, 6.0f};
			context.SetStyle(card, style);
			context.AddClass(card, EditorTheme::Class::SectionCard);

			UI::ElementHandle header = Label(context, card, title, BodyFont(), TextStrong());
			context.AddClass(header, EditorTheme::Class::SectionHeader);
			return card;
		}

		std::string_view KindLabel(PackageOps::PackageKind kind) {
			switch (kind) {
			case PackageOps::PackageKind::Engine:
				return "Built-in feature";
			case PackageOps::PackageKind::Tool:
				return "Built-in tool";
			case PackageOps::PackageKind::Package:
				return "Package";
			case PackageOps::PackageKind::Game:
				break;
			}
			return "Project module";
		}

		str JoinNames(std::span<const str> names) {
			str out;
			for (size_t index = 0; index < names.size(); ++index) {
				if (index > 0) {
					out += (index + 1 == names.size()) ? " and " : ", ";
				}
				out += names[index];
			}
			return out;
		}

	} // anonymous namespace

	void PackageManagerPanel::Refresh() {
		_catalogue.clear();
		_resolution.clear();
		_rows.clear();
		_diagnostics.clear();

		PackageOps::LoadBuiltInCatalogue(_catalogue, _diagnostics);
		Vector<str> stored{GetMemoryResource(MemTag::Editor)};
		PackageOps::ReadEnabledSet(stored);
		PackageOps::ResolveEnabledSet(_catalogue, stored, _resolution);
		PackageOps::BuildPackageRows(_catalogue, _resolution, _rows);

		if (!selectedPackage.empty() && !Selected()) {
			selectedPackage.clear();
		}
		if (selectedPackage.empty() && !_rows.empty()) {
			selectedPackage = _rows.front().name;
		}
	}

	str PackageManagerPanel::DisplayNameOf(std::string_view name) const {
		const PackageOps::PackageManifest* manifest = PackageOps::FindPackage(_catalogue, name);
		if (manifest && !manifest->displayName.empty()) {
			return manifest->displayName;
		}
		return str(name);
	}

	str PackageManagerPanel::JoinDisplayNames(std::span<const str> names) const {
		Vector<str> display{GetMemoryResource(MemTag::Editor)};
		for (const str& name : names) {
			display.push_back(DisplayNameOf(name));
		}
		return JoinNames(display);
	}

	const PackageOps::PackageRow* PackageManagerPanel::Selected() const {
		for (const PackageOps::PackageRow& row : _rows) {
			if (row.name == selectedPackage) {
				return &row;
			}
		}
		return nullptr;
	}

	void PackageManagerPanel::Toggle(UI::Context& context, const str& name, bool enable) {
		Vector<str> blockers{GetMemoryResource(MemTag::Editor)};
		if (!PackageOps::SetPackageEnabled(_catalogue, name, enable, blockers)) {
			SLT_LOG_INFO("Editor", "package {} can't be {}: {} {} it",
						 name, enable ? "enabled" : "disabled",
						 JoinNames(blockers), blockers.size() == 1 ? "requires" : "require");
			_dirty = true;
			(void)context;
			return;
		}
		_dirty = true;
	}

	void PackageManagerPanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		UI::Style rootStyle = context.GetStyle(root);
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Stretch;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(root, rootStyle);

		_diagnosticStrip = context.CreateElement();
		context.AppendChild(root, _diagnosticStrip);
		UI::Style stripStyle;
		stripStyle.flexDirection = UI::FlexDirection::Column;
		stripStyle.width = UI::Length::Percent(1.0f);
		context.SetStyle(_diagnosticStrip, stripStyle);

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

		ListViewDesc catalogue;
		catalogue.layout = ListLayout::Rows;
		catalogue.mode = ListSelectionMode::Single;
		catalogue.onSelectionChanged = [this]() { TakeSelectionFromList(); };
		_list.Build(this, context, listColumn, std::move(catalogue));

		UI::Widgets::ScrollViewParts detailScroll = UI::Widgets::ScrollView(context, body);
		UI::Style detailScrollStyle = context.GetStyle(detailScroll.root);
		detailScrollStyle.flexGrow = 1.0f;
		detailScrollStyle.width = UI::Length::Pixel(0.0f);
		detailScrollStyle.minWidth = UI::Length::Pixel(DETAIL_MIN_WIDTH);
		context.SetStyle(detailScroll.root, detailScrollStyle);
		_detailViewport = detailScroll.viewport;

		UI::ElementHandle footer = Row(context, root);
		UI::Style footerStyle = context.GetStyle(footer);
		footerStyle.padding = {RowPadX(), 5.0f, RowPadX(), 5.0f};
		context.SetStyle(footer, footerStyle);
		context.AddClass(footer, EditorTheme::Class::StatusBar);
		_footerLabel = Label(context, footer, "", SmallFont(), TextFaint());
		UI::Style footerLabelStyle = context.GetStyle(_footerLabel);
		footerLabelStyle.textOverflow = UI::TextOverflow::Ellipsis;
		footerLabelStyle.flexShrink = 1.0f;
		context.SetStyle(_footerLabel, footerLabelStyle);
		_lastFooter.clear();

		Refresh();
		_dirty = false;
		RebuildList(context);
		RebuildDetail(context);
		RefreshFooter(context);
	}

	void PackageManagerPanel::OnOpen() {
		SettingsEvents::Changed.Subscribe(EventBus::GetGlobal(), Handle<PackageManagerPanel>(this),
										  &PackageManagerPanel::OnEnabledSetChanged);
		_dirty = true;
	}

	void PackageManagerPanel::OnEnabledSetChanged(const SettingChangedEvent& event) {
		if (event.categoryPath != PackageOps::SettingsCategoryPath) {
			return;
		}
		_dirty = true;
	}

	void PackageManagerPanel::OnTick(f32) {
		UI::Context* context = GetPanelContext();
		if (!context || !context->IsAlive(_detailViewport)) {
			return;
		}
		if (_dirty) {
			_dirty = false;
			_detailDirty = false;
			Refresh();
			RebuildList(*context);
			RebuildDetail(*context);
		} else if (_detailDirty) {
			_detailDirty = false;
			RebuildDetail(*context);
		}
		RefreshFooter(*context);
	}

	void PackageManagerPanel::BuildGroupHeader(UI::Context& context, UI::ElementHandle host, std::string_view title) {
		UI::ElementHandle header = Label(context, host, title, SmallFont(), TextFaint());
		UI::Style style = context.GetStyle(header);
		style.padding = {RowPadX(), 6.0f, RowPadX(), 2.0f};
		context.SetStyle(header, style);
	}

	void PackageManagerPanel::DecorateRow(UI::Context& context, const PackageOps::PackageRow& row,
										  const UI::Widgets::TreeRowParts& parts) {
		context.SetTooltip(parts.row, row.displayName + "\n" + row.name);
		str name = row.name;

		UI::Widgets::Spacer(context, parts.row);
		UI::Widgets::CheckboxParts box = UI::Widgets::Checkbox(
			context, parts.row, row.enabled, [this, name](bool enable) { Toggle(*GetPanelContext(), name, enable); });

		if (!row.canToggle) {
			context.AddClass(box.box, EditorTheme::Class::Disabled);
			context.SetHoverable(box.box, false);
			str why = row.required ? str("Built in and always enabled.") : "Required by " + JoinDisplayNames(row.blockedBy);
			context.SetTooltip(box.box, why);
		}
	}

	void PackageManagerPanel::BuildEmptyState(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle card = Section(context, parent, "No packages");
		Label(context, card, "No packages are installed.", BodyFont(), TextDim());
	}

	u64 PackageManagerPanel::RowIdOf(std::string_view name) {
		return StringHash(name).GetValue();
	}

	str PackageManagerPanel::NameOfRow(u64 id) const {
		for (const PackageOps::PackageRow& row : _rows) {
			if (RowIdOf(row.name) == id) {
				return row.name;
			}
		}
		return {};
	}

	void PackageManagerPanel::TakeSelectionFromList() {
		std::span<const u64> selected = _list.GetSelected();
		str picked = selected.empty() ? str{} : NameOfRow(selected.back());
		if (picked == selectedPackage) {
			return;
		}
		selectedPackage = std::move(picked);
		_detailDirty = true;
	}

	void PackageManagerPanel::PushSelectionToList() {
		u64 id = selectedPackage.empty() ? 0 : RowIdOf(selectedPackage);
		Vector<u64> ids{GetMemoryResource(MemTag::Editor)};
		if (id != 0) {
			ids.push_back(id);
		}
		_list.SetSelection(ids);
	}

	void PackageManagerPanel::RebuildList(UI::Context& context) {
		if (context.IsAlive(_diagnosticStrip)) {
			context.ClearChildren(_diagnosticStrip);
			for (const str& diagnostic : _diagnostics) {
				UI::ElementHandle line = Label(context, _diagnosticStrip, diagnostic, SmallFont(), StatusWarn());
				UI::Style style = context.GetStyle(line);
				style.padding = {RowPadX(), 3.0f, RowPadX(), 3.0f};
				context.SetStyle(line, style);
			}
		}

		Vector<ListRow> listRows{GetMemoryResource(MemTag::Editor)};
		bool builtInHeader = false;
		bool packageHeader = false;
		for (size_t index = 0; index < _rows.size(); ++index) {
			const PackageOps::PackageRow& row = _rows[index];
			bool builtIn = row.kind != PackageOps::PackageKind::Package;
			std::string_view heading;
			if (builtIn && !builtInHeader) {
				builtInHeader = true;
				heading = "Built-in";
			} else if (!builtIn && !packageHeader) {
				packageHeader = true;
				heading = "Packages";
			}

			ListRow desc;
			desc.id = RowIdOf(row.name);
			desc.label = row.displayName;
			if (!heading.empty()) {
				desc.onBuildLeading = [this, title = str(heading)](UI::Context& context, UI::ElementHandle host) {
					BuildGroupHeader(context, host, title);
				};
			}
			desc.onDecorate = [this, index](UI::Context& context, const UI::Widgets::TreeRowParts& parts) {
				DecorateRow(context, _rows[index], parts);
			};
			listRows.push_back(std::move(desc));
		}
		_list.SetRows(listRows);
		PushSelectionToList();
	}

	void PackageManagerPanel::RebuildDetail(UI::Context& context) {
		if (!context.IsAlive(_detailViewport)) {
			return;
		}
		context.ClearChildren(_detailViewport);

		const PackageOps::PackageRow* row = Selected();
		if (!row) {
			BuildEmptyState(context, _detailViewport);
			return;
		}

		UI::ElementHandle identity = Section(context, _detailViewport, row->displayName);
		Label(context, identity, row->name, SmallFont(), TextFaint());
		Label(context, identity, std::format("Version {} · {}", row->version, KindLabel(row->kind)),
			  SmallFont(), TextDim());
		if (!row->description.empty()) {
			UI::ElementHandle description = Label(context, identity, row->description, BodyFont(), TextNormal());
			UI::Style style = context.GetStyle(description);
			style.margin = {0.0f, 6.0f, 0.0f, 0.0f};
			context.SetStyle(description, style);
		}

		UI::ElementHandle status = Section(context, _detailViewport, "Status");
		UI::ElementHandle statusRow = Row(context, status);
		Label(context, statusRow, row->enabled ? "Enabled" : "Disabled", BodyFont(),
			  row->enabled ? StatusOk() : TextDim());

		if (row->required) {
			Label(context, status, "Built in. Always enabled in the editor, not included in built games.", SmallFont(), TextDim());
		} else if (!row->blockedBy.empty()) {
			Label(context, status,
				  std::format("Required by {}. Disable {} first.", JoinDisplayNames(row->blockedBy),
							  row->blockedBy.size() == 1 ? "it" : "them"),
				  SmallFont(), StatusWarn());
		} else if (row->enabled && !row->requested) {
			Label(context, status, "Enabled because another enabled package depends on it.",
				  SmallFont(), TextDim());
		}
		if (row->kind == PackageOps::PackageKind::Tool && !row->required) {
			Label(context, status, "Editor only. Not included in built games.", SmallFont(), TextDim());
		}

		const PackageOps::PackageManifest* manifest = PackageOps::FindPackage(_catalogue, row->name);
		if (manifest && !manifest->dependencies.empty()) {
			UI::ElementHandle card = Section(context, _detailViewport, "Dependencies");
			Label(context, card, "Enabling this package enables these too.", SmallFont(), TextFaint());
			for (const PackageOps::PackageDependency& dependency : manifest->dependencies) {
				const PackageOps::PackageManifest* target = PackageOps::FindPackage(_catalogue, dependency.name);
				std::string_view label = (target && !target->displayName.empty())
											 ? std::string_view(target->displayName)
											 : std::string_view(dependency.name);
				Label(context, card, std::format("{}  {}", label, dependency.version), SmallFont(), TextDim());
			}
		}
	}

	void PackageManagerPanel::RefreshFooter(UI::Context& context) {
		if (!context.IsAlive(_footerLabel)) {
			return;
		}

		str message;
		const PackageHost* host = PackageHost::GetActive();
		if (host) {
			Vector<str> current{GetMemoryResource(MemTag::Editor)};
			for (const PackageOps::ResolvedPackage& entry : _resolution) {
				current.push_back(entry.name);
			}
			const Vector<str>& session = host->GetSessionEnabledNames();
			bool moved = current.size() != session.size() ||
						 !std::equal(current.begin(), current.end(), session.begin());
			if (moved) {
				message = "Restart the editor to apply package changes (File > Restart Editor).";
			}
		}

		if (message == _lastFooter) {
			return;
		}
		_lastFooter = message;
		context.SetTextString(_footerLabel, message);
		context.SetTooltip(_footerLabel, message);
	}

	EditorIcons::Icon PackageManagerPanel::GetTabIcon() const {
		return EditorIcons::Glyph::PanelPackages;
	}

} // namespace Shalltear
