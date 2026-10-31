#include "UIDebuggerPanel.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/UIBuilderOps.h"

#include <charconv>
#include <format>
#include <functional>

namespace Shalltear {

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		constexpr f32 ATLAS_PREVIEW_PX = 256.0f;

		Color Text() { return EditorTheme::GetColor(ColorToken::Text); }
		Color TextDim() { return EditorTheme::GetColor(ColorToken::TextDim); }
		Color TextStrong() { return EditorTheme::GetColor(ColorToken::TextStrong); }
		Color TextFaint() { return EditorTheme::GetColor(ColorToken::TextFaint); }
		Color Accent() { return EditorTheme::GetColor(ColorToken::Accent); }
		Color Warn() { return EditorTheme::GetColor(ColorToken::StatusWarn); }
		f32 RowFont() { return EditorTheme::GetMetric(MetricToken::FontSmall); }
		f32 TinyFont() { return EditorTheme::GetMetric(MetricToken::FontTiny); }
		f32 RowPadX() { return EditorTheme::GetMetric(MetricToken::RowPaddingX); }
		f32 IconGutter() { return EditorTheme::GetMetric(MetricToken::IconGutter); }

		constexpr std::string_view NoneText = "\xE2\x80\x94";

		constexpr std::string_view SheetExtension = ".slss";

		constexpr std::string_view DocumentExtension = ".slxml";

		using UIDebugOps::FormatNumber;

		str RectText(const UI::Rect& rect) {
			return "x:" + FormatNumber(rect.x) + "  y:" + FormatNumber(rect.y) + "  w:" + FormatNumber(rect.w) + "  h:" + FormatNumber(rect.h);
		}

		UI::ElementHandle Row(UI::Context& context, UI::ElementHandle parent) {
			UI::ElementHandle row = context.CreateElement();
			context.AppendChild(parent, row);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.alignItems = UI::Align::Center;
			style.width = UI::Length::Percent(1.0f);
			style.padding = {RowPadX(), 1.0f, RowPadX(), 1.0f};
			context.SetStyle(row, style);
			return row;
		}

		UI::ElementHandle Label(UI::Context& context, UI::ElementHandle parent, std::string_view text, Color color,
								f32 sizePx, f32 grow) {
			UI::ElementHandle label = context.CreateElement();
			context.AppendChild(parent, label);
			UI::Style style;
			style.width = UI::Length::Pixel(0.0f);
			style.flexGrow = grow;
			style.flexShrink = 1.0f;
			context.SetStyle(label, style);
			context.AddClass(label, EditorTheme::Class::RowText);
			context.SetText(label, UISystem::GetDefaultFont(), sizePx, color, text);
			return label;
		}

		void PairRow(UI::Context& context, UI::ElementHandle parent, std::string_view name, std::string_view value,
					 Color valueColor) {
			UI::ElementHandle row = Row(context, parent);
			Label(context, row, name, TextDim(), RowFont(), 1.0f);
			Label(context, row, value, valueColor, RowFont(), 1.6f);
		}

		UI::ElementHandle ValueField(UI::Context& context, UI::ElementHandle parent, std::string_view text,
									 std::function<void(std::string_view)> onCommit) {
			UI::Widgets::TextFieldDesc desc;
			desc.text = text;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = RowFont();
			desc.onCommit = std::move(onCommit);
			UI::ElementHandle field = UI::Widgets::TextField(context, parent, desc);
			context.AddClass(field, EditorTheme::Class::UIDebugValue);
			return field;
		}

		void RevertButton(UI::Context& context, UI::ElementHandle parent, std::function<void()> onClick) {
			UI::ElementHandle button = context.CreateElement();
			context.AppendChild(parent, button);
			context.AddClass(button, StringHash("slt-button"));
			context.AddClass(button, EditorTheme::Class::ToolButton);
			context.AddClass(button, EditorTheme::Class::IconButton);
			EditorIcons::SetGlyphOrText(context, button, EditorIcons::Glyph::Reload, "<", TextDim(), IconGutter(),
										RowFont());
			UI::Style style = context.GetStyle(button);
			style.flexShrink = 0.0f;
			context.SetStyle(button, style);
			context.SetTooltip(button, "Revert to the value this element had when it was selected");
			context.Events(button).onClick = std::move(onClick);
		}

		bool ParseHexU64(std::string_view text, u64& out) {
			if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
				text.remove_prefix(2);
			}
			if (text.empty()) {
				return false;
			}
			out = 0;
			return std::from_chars(text.data(), text.data() + text.size(), out, 16).ec == std::errc{};
		}

		UI::ElementHandle Band(UI::Context& context, UI::ElementHandle parent, std::string_view title,
							   const UI::Edges& edges) {
			UI::ElementHandle box = context.CreateElement();
			context.AppendChild(parent, box);
			context.AddClass(box, EditorTheme::Class::UIDebugBand);
			UI::Style boxStyle;
			boxStyle.flexDirection = UI::FlexDirection::Column;
			boxStyle.alignItems = UI::Align::Stretch;
			boxStyle.flexGrow = 1.0f;
			boxStyle.padding = UI::Edges::All(2.0f);
			context.SetStyle(box, boxStyle);

			auto edge = [&context](UI::ElementHandle host, f32 value, UI::Align align, f32 grow) {
				UI::ElementHandle label = context.CreateElement();
				context.AppendChild(host, label);
				UI::Style style;
				style.flexGrow = grow;
				style.flexShrink = 1.0f;
				style.textAlign = align;
				context.SetStyle(label, style);
				context.AddClass(label, EditorTheme::Class::RowText);
				context.SetText(label, UISystem::GetDefaultFont(), TinyFont(), TextDim(), FormatNumber(value));
				return label;
			};

			UI::ElementHandle top = context.CreateElement();
			context.AppendChild(box, top);
			UI::Style topStyle;
			topStyle.flexDirection = UI::FlexDirection::Row;
			topStyle.alignItems = UI::Align::Center;
			context.SetStyle(top, topStyle);
			Label(context, top, title, TextFaint(), TinyFont(), 1.0f);
			edge(top, edges.top, UI::Align::Center, 1.0f);

			UI::ElementHandle middle = context.CreateElement();
			context.AppendChild(box, middle);
			UI::Style middleStyle;
			middleStyle.flexDirection = UI::FlexDirection::Row;
			middleStyle.alignItems = UI::Align::Center;
			context.SetStyle(middle, middleStyle);
			edge(middle, edges.left, UI::Align::Start, 0.0f);
			UI::ElementHandle hole = context.CreateElement();
			context.AppendChild(middle, hole);
			UI::Style holeStyle;
			holeStyle.flexDirection = UI::FlexDirection::Column;
			holeStyle.alignItems = UI::Align::Stretch;
			holeStyle.flexGrow = 1.0f;
			holeStyle.flexShrink = 1.0f;
			holeStyle.margin = UI::Edges::All(2.0f);
			context.SetStyle(hole, holeStyle);
			edge(middle, edges.right, UI::Align::End, 0.0f);

			UI::ElementHandle bottom = context.CreateElement();
			context.AppendChild(box, bottom);
			UI::Style bottomStyle;
			bottomStyle.flexDirection = UI::FlexDirection::Row;
			context.SetStyle(bottom, bottomStyle);
			edge(bottom, edges.bottom, UI::Align::Center, 1.0f);
			return hole;
		}

	} // anonymous namespace

	UIDebuggerPanel::InspectorKey UIDebuggerPanel::ReadInspectorKey() const {
		InspectorKey key;
		key.collapsedSections = _collapsedSections;
		key.showAllStyles = _showAllStyles;
		key.atlasOpen = _atlasOpen;
		key.atlasPage = _atlasPage;
		if (_atlasOpen) {
			Vector<UIDebugOps::AtlasPageRow> pages{GetMemoryResource(MemTag::Editor)};
			UIDebugOps::BuildAtlasPages(pages);
			if (_atlasPage < pages.size()) {
				key.atlasEntries = pages[_atlasPage].page.entries;
			}
		}
		UI::Context* target = LiveTarget();
		if (!target || !_hasSelection) {
			return key;
		}
		UI::ElementHandle element = UIDebugOps::ResolveSame(*target, _selectedPath, _selectedIdentity);
		const UI::Element* pod = target->GetElement(element);
		if (!pod) {
			return key;
		}
		key.element = element;
		key.resolved = true;
		UIDebugOps::BuildGeometry(*target, element, key.geometry);
		key.flags = pod->flags;
		key.id = pod->id;
		key.typeTag = pod->typeTag;
		key.classCount = pod->classCount;
		for (u8 i = 0; i < pod->classCount && i < UI::MAX_ELEMENT_CLASSES; ++i) {
			key.classes[i] = pod->classes[i];
		}
		key.tint = pod->tint;
		key.userTag = pod->userTag;
		key.scrollOffset = pod->scrollOffset;
		key.maxScroll = pod->maxScroll;
		const UI::Style& style = target->GetComputedStyle(element);
		key.zIndex = style.zIndex;
		key.cursor = style.cursor;
		key.pointerEvents = style.pointerEvents;
		return key;
	}

	bool UIDebuggerPanel::IsSectionCollapsed(Section section) const {
		return (_collapsedSections & (1u << static_cast<u32>(section))) != 0;
	}

	void UIDebuggerPanel::ToggleSection(Section section) {
		_collapsedSections ^= (1u << static_cast<u32>(section));
		_inspectorBuilt = false;
	}

	UI::ElementHandle UIDebuggerPanel::BuildSection(UI::Context& context, Section section, std::string_view title) {
		const bool collapsed = IsSectionCollapsed(section);

		UI::ElementHandle header = context.CreateElement();
		context.AppendChild(_inspectorHost, header);
		context.AddClass(header, EditorTheme::Class::SectionHeader);
		UI::Style headerStyle;
		headerStyle.flexDirection = UI::FlexDirection::Row;
		headerStyle.alignItems = UI::Align::Center;
		headerStyle.width = UI::Length::Percent(1.0f);
		headerStyle.padding = {RowPadX(), 3.0f, RowPadX(), 3.0f};
		headerStyle.cursor = SystemCursor::Hand;
		context.SetStyle(header, headerStyle);

		UI::ElementHandle caret = context.CreateElement();
		context.AppendChild(header, caret);
		EditorIcons::SetGlyphOrText(context, caret,
									collapsed ? EditorIcons::Glyph::CaretRight : EditorIcons::Glyph::CaretDown,
									collapsed ? ">" : "v", TextDim(), IconGutter(), RowFont());
		UI::Style caretStyle = context.GetStyle(caret);
		caretStyle.flexShrink = 0.0f;
		context.SetStyle(caret, caretStyle);
		context.SetHoverable(caret, false);

		UI::ElementHandle label = context.CreateElement();
		context.AppendChild(header, label);
		UI::Style labelStyle;
		labelStyle.flexGrow = 1.0f;
		labelStyle.flexShrink = 1.0f;
		context.SetStyle(label, labelStyle);
		context.AddClass(label, EditorTheme::Class::RowText);
		context.SetText(label, UISystem::GetDefaultFont(), RowFont(), TextStrong(), title);
		context.SetHoverable(label, false);

		context.Events(header).onClick = [this, section]() { ToggleSection(section); };

		if (collapsed) {
			return {};
		}
		UI::ElementHandle body = context.CreateElement();
		context.AppendChild(_inspectorHost, body);
		UI::Style bodyStyle;
		bodyStyle.flexDirection = UI::FlexDirection::Column;
		bodyStyle.alignItems = UI::Align::Stretch;
		bodyStyle.width = UI::Length::Percent(1.0f);
		bodyStyle.padding = {0.0f, 2.0f, 0.0f, 4.0f};
		context.SetStyle(body, bodyStyle);
		return body;
	}

	namespace {

		void FeedDocumentNames(const UIDocumentNode& node, UIDebugOps::NameDictionary& names) {
			if (!node.id.empty()) {
				names.AddName(node.id);
			}
			for (u8 i = 0; i < node.classCount; ++i) {
				names.AddName(node.classes[i]);
			}
			for (const UIDocumentNode& child : node.children) {
				FeedDocumentNames(child, names);
			}
		}

	} // anonymous namespace

	void UIDebuggerPanel::FeedProjectNames() {
		Vector<AssetBrowserOps::Entry> entries{GetMemoryResource(MemTag::Editor)};
		if (!AssetBrowserOps::ScanTree(entries)) {
			return;
		}
		for (const AssetBrowserOps::Entry& entry : entries) {
			if (entry.IsDirectory()) {
				continue;
			}
			if (entry.name.ends_with(SheetExtension)) {
				const str virtualPath = "project://" + entry.relPath;
				Asset* asset = AssetDatabase::LoadAsset(Path(virtualPath));
				if (asset && asset->GetAssetTypeId() == StringHash("StyleSheetAsset")) {
					_names.AddCandidateSheet(virtualPath, static_cast<StyleSheetAsset*>(asset)->source);
				}
				continue;
			}
			if (!entry.name.ends_with(DocumentExtension)) {
				continue;
			}
			Asset* asset = AssetDatabase::LoadAsset(Path("project://" + entry.relPath));
			if (asset && asset->GetAssetTypeId() == StringHash("UIDocumentAsset")) {
				FeedDocumentNames(static_cast<UIDocumentAsset*>(asset)->root, _names);
			}
		}
	}

	void UIDebuggerPanel::BuildAtlasView(UI::Context& context) {
		UIDebugOps::BuildAtlasPages(_atlasPages);
		if (_atlasPages.empty()) {
			Label(context, _inspectorHost, UIDebugOps::NoAtlasText, TextDim(), RowFont(), 0.0f);
			return;
		}
		if (_atlasPage >= _atlasPages.size()) {
			_atlasPage = 0;
		}
		const UIDebugOps::AtlasPageRow& row = _atlasPages[_atlasPage];

		UI::ElementHandle head = Row(context, _inspectorHost);
		UI::ElementHandle selector = context.CreateElement();
		context.AppendChild(head, selector);
		context.AddClass(selector, StringHash("slt-button"));
		context.AddClass(selector, EditorTheme::Class::ToolButton);
		context.SetText(selector, UISystem::GetDefaultFont(), RowFont(), TextStrong(), str(row.name));
		context.Events(selector).onClick = [this, selector]() {
			if (UI::Context* live = GetPanelContext()) {
				OpenAtlasPageMenu(*live, selector);
			}
		};

		PairRow(context, _inspectorHost, "Size",
				std::to_string(row.page.size.x) + " \xC3\x97 " + std::to_string(row.page.size.y), Text());
		PairRow(context, _inspectorHost, "Format",
				row.page.format == UI::AtlasFormat::RGBA8 ? str("RGBA8 premultiplied") : str("R8 coverage"), Text());
		PairRow(context, _inspectorHost, "Resident", std::to_string(row.page.entries), Text());
		PairRow(context, _inspectorHost, "Full", row.page.full ? str("yes") : str("no"),
				row.page.full ? EditorTheme::GetColor(ColorToken::StatusError) : Text());

		UI::ElementHandle canvas = context.CreateElement();
		context.AppendChild(_inspectorHost, canvas);
		UI::Style canvasStyle;
		canvasStyle.width = UI::Length::Percent(1.0f);
		canvasStyle.height = UI::Length::Pixel(ATLAS_PREVIEW_PX);
		canvasStyle.flexShrink = 0.0f;
		canvasStyle.margin = UI::Edges::All(RowPadX());
		canvasStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::Well));
		context.SetStyle(canvas, canvasStyle);
		const UI::AtlasPage page = row.page;
		context.Events(canvas).customDraw = [page](UI::Context& ctx, UI::ElementHandle self, UI::PaintScope& paint) {
			const UI::Rect box = ctx.GetLayout(self).content;
			if (box.w <= 0.0f || box.h <= 0.0f || page.size.x <= 0 || page.size.y <= 0) {
				return;
			}
			const f32 scale = std::min(box.w / static_cast<f32>(page.size.x), box.h / static_cast<f32>(page.size.y));
			const f32 w = static_cast<f32>(page.size.x) * scale;
			const f32 h = static_cast<f32>(page.size.y) * scale;
			const UI::Rect fitted{box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, w, h};
			const bool coverage = page.format == UI::AtlasFormat::R8;
			paint.EmitTexturedQuad(fitted, page.texture, {0.0f, 0.0f, 1.0f, 1.0f},
								   coverage ? EditorTheme::GetColor(ColorToken::Text) : Color::White(),
								   UI::SamplerKind::NearestClamp, {},
								   coverage ? UI::TexelEncoding::Coverage : UI::TexelEncoding::Premultiplied);
		};
	}

	void UIDebuggerPanel::BuildInspector(UI::Context& context) {
		if (!context.IsAlive(_inspectorHost)) {
			return;
		}
		context.ClearChildren(_inspectorHost);
		_inspectorKey = ReadInspectorKey();
		_inspectorBuilt = true;

		if (_atlasOpen) {
			BuildAtlasView(context);
			return;
		}

		UI::Context* target = LiveTarget();
		if (!target) {
			Label(context, _inspectorHost, "No live target", TextDim(), RowFont(), 0.0f);
			return;
		}
		if (!_hasSelection) {
			Label(context, _inspectorHost, "Select an element", TextDim(), RowFont(), 0.0f);
			return;
		}
		if (!_inspectorKey.resolved) {
			Label(context, _inspectorHost, "The selected element is gone", TextDim(), RowFont(), 0.0f);
			return;
		}
		const UI::ElementHandle element = _inspectorKey.element;
		const UIDebugOps::Geometry& geometry = _inspectorKey.geometry;

		UI::ElementHandle title = Row(context, _inspectorHost);
		Label(context, title, UIDebugOps::RowLabel(*target, element, _names), TextStrong(), RowFont(), 1.0f);

		if (UI::ElementHandle body = BuildSection(context, Section::Layout, "Layout"); body.IsValid()) {
			UI::ElementHandle frame = context.CreateElement();
			context.AppendChild(body, frame);
			UI::Style frameStyle;
			frameStyle.flexDirection = UI::FlexDirection::Column;
			frameStyle.margin = {RowPadX(), 2.0f, RowPadX(), 6.0f};
			context.SetStyle(frame, frameStyle);

			UI::ElementHandle inMargin = Band(context, frame, "margin", geometry.margin);
			UI::ElementHandle inBorder = Band(context, inMargin, "border", geometry.border);
			UI::ElementHandle inPadding = Band(context, inBorder, "padding", geometry.padding);
			UI::ElementHandle size = context.CreateElement();
			context.AppendChild(inPadding, size);
			UI::Style sizeStyle;
			sizeStyle.flexDirection = UI::FlexDirection::Row;
			sizeStyle.justifyContent = UI::Align::Center;
			sizeStyle.padding = UI::Edges::All(2.0f);
			context.SetStyle(size, sizeStyle);
			Label(context, size, FormatNumber(geometry.contentRect.w) + " \xC3\x97 " + FormatNumber(geometry.contentRect.h),
				  Text(), TinyFont(), 0.0f);

			PairRow(context, body, "World Bound", RectText(geometry.worldBound), Text());
			PairRow(context, body, "World Clip", RectText(geometry.worldClip),
					geometry.clipped ? Accent() : Text());
			PairRow(context, body, "Content Rect", RectText(geometry.contentRect), Text());
			PairRow(context, body, "Layout", RectText(geometry.layout), Text());
		}

		if (UI::ElementHandle body = BuildSection(context, Section::Stylesheets, "Stylesheets"); body.IsValid()) {
			UIDebugOps::BuildSheets(_names, _sheets);
			if (_sheets.empty()) {
				Label(context, body, NoneText, TextDim(), RowFont(), 0.0f);
			}
			for (const UIDebugOps::SheetRow& sheet : _sheets) {
				const str name = sheet.readable ? str(sheet.name) : ("#" + std::format("{:08x}", sheet.tag.GetValue()));
				const str detail = sheet.readable ? (std::to_string(sheet.ruleCount) + " rules") : str("unreadable");
				PairRow(context, body, name, detail, sheet.readable ? Text() : TextFaint());
			}
		}

		if (UI::ElementHandle body = BuildSection(context, Section::MatchingSelectors, "Matching Selectors");
			body.IsValid()) {
			UIDebugOps::BuildMatches(*target, element, _names, _matches);
			if (_matches.empty()) {
				Label(context, body, NoneText, TextDim(), RowFont(), 0.0f);
			}
			for (const UIDebugOps::Match& match : _matches) {
				UI::ElementHandle row = Row(context, body);
				Label(context, row, match.selector, match.applies ? Text() : TextDim(), RowFont(), 2.0f);
				Label(context, row, match.sheet, TextFaint(), TinyFont(), 1.0f);
				Label(context, row, std::to_string(match.specificity), TextFaint(), TinyFont(), 0.4f);
				if (!match.applies) {
					context.AddClass(row, EditorTheme::Class::RowOff);
				}
				if (!AssetDatabase::IsAssetPath(Path(str(match.sheet)))) {
					continue;
				}
				context.SetTooltip(row, "show this rule in the UI Builder");
				context.SetHoverable(row, true);
				context.AddClass(row, EditorTheme::Class::UIDebugLink);
				for (u32 child = context.GetElement(row)->firstChild; child != UI::INVALID_UI_INDEX;) {
					UI::ElementHandle cell = context.MakeHandle(child);
					const UI::Element* pod = context.GetElement(cell);
					UI::Style cellStyle = context.GetStyle(cell);
					cellStyle.pointerEvents = false;
					context.SetStyle(cell, cellStyle);
					child = pod->nextSibling;
				}
				context.Events(row).onClick = [this, sheet = str(match.sheet), rule = match.rule,
											   selector = str(match.selector)]() {
					if (!UIBuilderOps::ShowSheetRuleInBuilder(sheet, rule, selector)) {
						_linkStatus = "open a document that declares " + sheet + " to edit its rules";
						_headerDirty = true;
					}
				};
			}
		}

		if (UI::ElementHandle body = BuildSection(context, Section::Attributes, "Attributes"); body.IsValid()) {
			UIDebugOps::BuildAttributes(*target, element, _names, _attributes);
			for (const UIDebugOps::AttributeRow& attribute : _attributes) {
				if (attribute.edit == UIDebugOps::AttributeRow::Edit::None) {
					PairRow(context, body, attribute.name, attribute.value, Text());
					continue;
				}
				BuildAttributeRow(context, body, attribute, element);
			}
		}

		if (UI::ElementHandle body = BuildSection(context, Section::Styles, "Styles"); body.IsValid()) {
			UIDebugOps::BuildProperties(*target, element, _names, _properties);

			UI::ElementHandle toggleRow = Row(context, body);
			UI::Widgets::CheckboxParts check =
				UI::Widgets::Checkbox(context, toggleRow, _showAllStyles, [this](bool on) {
					_showAllStyles = on;
					_inspectorBuilt = false;
				});
			UI::Style checkStyle = context.GetStyle(check.box);
			checkStyle.flexShrink = 0.0f;
			checkStyle.margin = {0.0f, 0.0f, EditorTheme::GetMetric(MetricToken::IconGap), 0.0f};
			context.SetStyle(check.box, checkStyle);
			Label(context, toggleRow, "Show all", TextDim(), RowFont(), 1.0f);

			Label(context, body, "Edits write the inline style and are lost when the owning panel rebuilds",
				  TextFaint(), TinyFont(), 0.0f);

			u32 shown = 0;
			for (const UIDebugOps::PropertyRow& property : _properties) {
				const StringHash name{property.name};
				const bool edited = UIDebugOps::CanRevert(*target, element, _edits, name);
				if (!_showAllStyles && property.source == UIDebugOps::ValueSource::Default && !edited) {
					continue;
				}
				++shown;
				UI::ElementHandle row = Row(context, body);
				Label(context, row, property.name, TextDim(), RowFont(), 1.2f);
				UI::ElementHandle field =
					ValueField(context, row, property.value,
							   [this, name](std::string_view text) { CommitProperty(name, text); });
				if (name == _focusRow) {
					context.SetFocus(field);
				}
				if (edited) {
					RevertButton(context, row, [this, name]() { RevertOneProperty(name); });
				}

				std::string_view source;
				Color sourceColor = TextFaint();
				switch (property.source) {
				case UIDebugOps::ValueSource::Inline:
					source = "inline";
					sourceColor = Accent();
					break;
				case UIDebugOps::ValueSource::Sheet:
					source = property.sheet.empty() ? std::string_view("sheet") : property.sheet;
					sourceColor = TextDim();
					break;
				default:
					source = "default";
					break;
				}
				Label(context, row, source, sourceColor, TinyFont(), 1.0f);
				if (edited && property.source == UIDebugOps::ValueSource::Sheet) {
					Label(context, row, "overridden", Warn(), TinyFont(), 0.8f);
				}
			}
			if (shown == 0) {
				Label(context, body, "Nothing is set on this element", TextDim(), RowFont(), 0.0f);
			}
		}

		_focusRow = StringHash{};
	}

	void UIDebuggerPanel::BuildAttributeRow(UI::Context& context, UI::ElementHandle parent,
											const UIDebugOps::AttributeRow& attribute, UI::ElementHandle element) {
		using Edit = UIDebugOps::AttributeRow::Edit;
		UI::ElementHandle row = Row(context, parent);
		Label(context, row, attribute.name, TextDim(), RowFont(), 1.0f);
		const StringHash key{attribute.name};

		auto field = [&](std::function<void(UI::Context&, std::string_view)> apply) {
			UI::ElementHandle built = ValueField(context, row, attribute.value,
												 [this, element, apply = std::move(apply), key](std::string_view text) {
													 if (UI::Context* live = LiveTarget();
														 live && live->GetElement(element)) {
														 apply(*live, text);
													 }
													 AfterEdit(key);
												 });
			if (key == _focusRow) {
				context.SetFocus(built);
			}
		};
		auto check = [&](bool value, std::function<void(UI::Context&, bool)> apply) {
			UI::Widgets::CheckboxParts parts =
				UI::Widgets::Checkbox(context, row, value, [this, element, apply = std::move(apply), key](bool on) {
					if (UI::Context* live = LiveTarget(); live && live->GetElement(element)) {
						apply(*live, on);
					}
					AfterEdit(key);
				});
			UI::Style style = context.GetStyle(parts.box);
			style.flexShrink = 0.0f;
			context.SetStyle(parts.box, style);
		};

		switch (attribute.edit) {
		case Edit::Id:
			field([this, element](UI::Context& live, std::string_view text) {
				live.SetId(element, text.empty() ? StringHash{} : StringHash(text));
				_names.AddName(text);
			});
			break;
		case Edit::TypeTag:
			field([this, element](UI::Context& live, std::string_view text) {
				live.SetTypeTag(element, text.empty() ? StringHash{} : StringHash(text));
				_names.AddName(text);
			});
			break;
		case Edit::Classes:
			field([this, element](UI::Context& live, std::string_view text) {
				UIDebugOps::SetClasses(live, element, text, _names);
			});
			break;
		case Edit::Visible:
			check(HasFlag(_inspectorKey.flags, UI::ElementFlags::Visible),
				  [element](UI::Context& live, bool on) { live.SetVisible(element, on); });
			break;
		case Edit::Hoverable:
			check(HasFlag(_inspectorKey.flags, UI::ElementFlags::Hoverable),
				  [element](UI::Context& live, bool on) { live.SetHoverable(element, on); });
			break;
		case Edit::Focusable:
			check(HasFlag(_inspectorKey.flags, UI::ElementFlags::Focusable),
				  [element](UI::Context& live, bool on) { live.SetFocusable(element, on); });
			break;
		case Edit::Tint:
			field([element](UI::Context& live, std::string_view text) {
				if (Color parsed; Color::TryParseHex(text, parsed)) {
					live.SetTint(element, parsed);
				}
			});
			break;
		case Edit::UserTag:
			field([element](UI::Context& live, std::string_view text) {
				if (u64 parsed = 0; ParseHexU64(text, parsed)) {
					live.SetUserTag(element, parsed);
				}
			});
			break;
		case Edit::ScrollOffset:
			BuildScrollOffsetRow(context, row, element);
			break;
		case Edit::Text:
			field([element](UI::Context& live, std::string_view text) { live.SetTextString(element, text); });
			break;
		default:
			break;
		}
	}

	void UIDebuggerPanel::BuildScrollOffsetRow(UI::Context& context, UI::ElementHandle row, UI::ElementHandle element) {
		UI::Widgets::VectorFieldDesc desc;
		desc.componentCount = 2;
		desc.values[0] = _inspectorKey.scrollOffset.x;
		desc.values[1] = _inspectorKey.scrollOffset.y;
		desc.decimals = 2;
		desc.font = UISystem::GetDefaultFont();
		desc.fontSizePx = RowFont();
		desc.onEditBegin = [this]() { _editGesture = true; };
		desc.onEditEnd = [this]() {
			_editGesture = false;
			_inspectorBuilt = false;
		};
		desc.onChange = [this, element](u32 component, f64 value) {
			UI::Context* live = LiveTarget();
			const UI::Element* pod = live ? live->GetElement(element) : nullptr;
			if (!pod) {
				return;
			}
			Vec2 offset = pod->scrollOffset;
			(component == 0 ? offset.x : offset.y) = static_cast<f32>(value);
			live->SetScrollOffset(element, offset);
		};
		UI::Widgets::VectorFieldParts parts = UI::Widgets::VectorField(context, row, desc);
		UI::Style style = context.GetStyle(parts.root);
		style.flexGrow = 1.6f;
		style.flexShrink = 1.0f;
		context.SetStyle(parts.root, style);
	}

	void UIDebuggerPanel::CommitProperty(StringHash name, std::string_view text) {
		UI::Context* target = LiveTarget();
		if (!target || !_inspectorKey.resolved) {
			return;
		}
		UIDebugOps::SetProperty(*target, _inspectorKey.element, _edits, name, text);
		AfterEdit(name);
	}

	void UIDebuggerPanel::RevertOneProperty(StringHash name) {
		UI::Context* target = LiveTarget();
		if (!target || !_inspectorKey.resolved) {
			return;
		}
		UIDebugOps::RevertProperty(*target, _inspectorKey.element, _edits, name);
		AfterEdit(StringHash{});
	}

	void UIDebuggerPanel::AfterEdit(StringHash row) {
		_focusRow = row;
		if (_hasSelection) {
			if (UI::Context* live = LiveTarget()) {
				if (UI::ElementHandle element = UIDebugOps::Resolve(*live, _selectedPath); element.IsValid()) {
					_selectedIdentity = UIDebugOps::IdentityOf(*live, element);
				}
			}
		}
		_inspectorBuilt = false;
	}

} // namespace Shalltear
