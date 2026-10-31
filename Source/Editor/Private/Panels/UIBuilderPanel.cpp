#include "UIBuilderPanel.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Input/InputEvent.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/RenderList.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorFonts.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorInputOps.h"
#include "Shalltear/Editor/EditorSelectionOps.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/UIDebugOps.h"
#include "Shalltear/Editor/UIDocumentOps.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "PanelHeader.h"
#include "UIBuilderChrome.h"
#include "UIBuilderCommands.h"

#include <algorithm>
#include <span>

namespace Shalltear {

	SLT_REGISTER_TYPE(UIBuilderPanel)

	namespace {
		using EditorTheme::MetricToken;
		using namespace UIBuilderChrome;

		constexpr f32 SELECTION_OUTLINE_PX = 2.0f;
		constexpr std::string_view NoDocumentText = "no document open";
		constexpr i32 MIN_SURFACE_PX = 8;
		constexpr f32 SIDE_COLUMN_MIN_PX = 160.0f;
		constexpr f32 VIEWPORT_MIN_PX = 200.0f;
		constexpr f32 CENTER_MIN_PX = VIEWPORT_MIN_PX + SIDE_COLUMN_MIN_PX;
		constexpr f32 STACKED_PANE_MIN_PX = 60.0f;
		constexpr f32 ZOOM_STOPS[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
		constexpr f32 TEXT_PANE_FRACTION = 0.28f;
		constexpr f32 TEXT_PANE_MIN_PX = 120.0f;

		struct CanvasPreset {
			std::string_view label;
			i32 width;
			i32 height;
		};
		constexpr CanvasPreset CanvasPresets[] = {
			{"1920 x 1080", 1920, 1080},
			{"1280 x 720", 1280, 720},
			{"2560 x 1440", 2560, 1440},
			{"3840 x 2160", 3840, 2160},
			{"1080 x 1920", 1080, 1920},
			{"800 x 600", 800, 600},
		};

		UI::ElementHandle FillScrollView(UI::Context& context, UI::ElementHandle host) {
			UI::Widgets::ScrollViewParts scroll = UI::Widgets::ScrollView(context, host);
			UI::Style style = context.GetStyle(scroll.root);
			style.flexGrow = 1.0f;
			style.height = UI::Length::Pixel(0.0f);
			context.SetStyle(scroll.root, style);
			return scroll.viewport;
		}

		UI::Style FillColumn(f32 grow) {
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Column;
			style.alignItems = UI::Align::Stretch;
			style.width = UI::Length::Percent(1.0f);
			style.height = UI::Length::Pixel(0.0f);
			style.flexGrow = grow;
			style.overflow = UI::Overflow::Hidden;
			return style;
		}

		bool PayloadNodes(const UI::DragPayload& payload, u64& outPanel, std::span<const u32>& outNodes) {
			if (payload.type != "UIBuilderNode"_hash || !payload.data.Is<UIBuilderNodeDragData>()) {
				return false;
			}
			const UIBuilderNodeDragData& data = payload.data.As<UIBuilderNodeDragData>();
			outPanel = data.panelId;
			outNodes = {data.nodes.data(), data.nodes.size()};
			return !outNodes.empty();
		}

		bool PayloadWidgetTag(const UI::DragPayload& payload, str& outTag) {
			if (payload.type != "UIWidgetTag"_hash || !payload.data.Is<str>()) {
				return false;
			}
			outTag = payload.data.As<str>();
			return true;
		}

		constexpr u32 TAB_WIDTH = 4;

		str ExpandTabs(std::string_view text) {
			str out;
			out.reserve(text.size());
			for (char c : text) {
				if (c == '\t') {
					out.append(TAB_WIDTH, ' ');
				} else if (c != '\r') {
					out += c;
				}
			}
			return out;
		}

		Color SyntaxColor(UIBuilderOps::CodeToken kind) {
			using UIBuilderOps::CodeToken;
			switch (kind) {
			case CodeToken::Punctuation:
				return EditorTheme::GetColor(EditorTheme::ColorToken::SyntaxPunct);
			case CodeToken::Tag:
				return EditorTheme::GetColor(EditorTheme::ColorToken::SyntaxTag);
			case CodeToken::Attribute:
				return EditorTheme::GetColor(EditorTheme::ColorToken::SyntaxAttr);
			case CodeToken::Value:
				return EditorTheme::GetColor(EditorTheme::ColorToken::SyntaxValue);
			case CodeToken::Comment:
				return EditorTheme::GetColor(EditorTheme::ColorToken::SyntaxComment);
			case CodeToken::Text:
				break;
			}
			return EditorTheme::GetColor(EditorTheme::ColorToken::Text);
		}
	} // anonymous namespace

	void UIBuilderPanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		_contentRoot = root;
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};

		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Row;
		rootStyle.alignItems = UI::Align::Stretch;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(root, rootStyle);

		UI::Widgets::SplitDesc outer;
		outer.sizing = UI::Widgets::SplitSizing::Ratio;
		outer.position = _leftColumnRatio;
		outer.minFirstPx = SIDE_COLUMN_MIN_PX;
		outer.minSecondPx = CENTER_MIN_PX;
		outer.onPositionChanged = [this](f32 share) { _leftColumnRatio = share; };
		UI::Widgets::SplitParts outerPanes = UI::Widgets::Split(context, root, outer);

		UI::Widgets::SplitDesc inner;
		inner.sizing = UI::Widgets::SplitSizing::Ratio;
		inner.position = _centerRatio;
		inner.minFirstPx = VIEWPORT_MIN_PX;
		inner.minSecondPx = SIDE_COLUMN_MIN_PX;
		inner.onPositionChanged = [this](f32 share) { _centerRatio = share; };
		UI::Widgets::SplitParts innerPanes = UI::Widgets::Split(context, outerPanes.second, inner);

		UI::ElementHandle left = outerPanes.first;

		UI::Widgets::SplitDesc sheetsSplit;
		sheetsSplit.axis = UI::FlexDirection::Column;
		sheetsSplit.sizing = UI::Widgets::SplitSizing::Ratio;
		sheetsSplit.position = _sheetsRatio;
		sheetsSplit.minFirstPx = STACKED_PANE_MIN_PX;
		sheetsSplit.minSecondPx = STACKED_PANE_MIN_PX * 2.0f;
		sheetsSplit.onPositionChanged = [this](f32 share) { _sheetsRatio = share; };
		UI::Widgets::SplitParts sheetsPanes = UI::Widgets::Split(context, left, sheetsSplit);

		UI::Widgets::SplitDesc treeSplit;
		treeSplit.axis = UI::FlexDirection::Column;
		treeSplit.sizing = UI::Widgets::SplitSizing::Ratio;
		treeSplit.position = _hierarchyRatio;
		treeSplit.minFirstPx = STACKED_PANE_MIN_PX;
		treeSplit.minSecondPx = STACKED_PANE_MIN_PX;
		treeSplit.onPositionChanged = [this](f32 share) { _hierarchyRatio = share; };
		UI::Widgets::SplitParts treePanes = UI::Widgets::Split(context, sheetsPanes.second, treeSplit);

		PaneHeader(context, sheetsPanes.first, "StyleSheets", font);
		BuildSheetsPane(context, sheetsPanes.first);
		PaneHeader(context, treePanes.first, "Hierarchy", font);
		BuildHierarchyPane(context, treePanes.first);
		PaneHeader(context, treePanes.second, "Library", font);
		BuildLibraryPane(context, treePanes.second);

		BuildViewportPane(context, innerPanes.first);

		UI::ElementHandle right = innerPanes.second;
		PaneHeader(context, right, "Inspector", font);
		BuildInspectorPane(context, right);

		RebuildPanes(context);
	}

	void UIBuilderPanel::BuildSheetsPane(UI::Context& context, UI::ElementHandle parent) {
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};

		UI::ElementHandle host = context.CreateElement();
		context.AppendChild(parent, host);
		context.SetStyle(host, FillColumn(1.0f));

		UI::ElementHandle bar = context.CreateElement();
		context.AppendChild(host, bar);
		UI::Style barStyle;
		barStyle.flexDirection = UI::FlexDirection::Row;
		barStyle.alignItems = UI::Align::Center;
		barStyle.padding = {4.0f, 2.0f, 4.0f, 2.0f};
		context.SetStyle(bar, barStyle);

		UI::ElementHandle fieldHost = context.CreateElement();
		context.AppendChild(bar, fieldHost);
		UI::Style fieldStyle;
		fieldStyle.flexGrow = 1.0f;
		fieldStyle.width = UI::Length::Pixel(0.0f);
		context.SetStyle(fieldHost, fieldStyle);

		UI::Widgets::TextFieldDesc selectorField;
		selectorField.text = "";
		selectorField.font = font;
		selectorField.fontSizePx = RowFont();
		selectorField.onCommit = [this](std::string_view selector) {
			AddSelector(selector);
		};
		UI::ElementHandle field = UI::Widgets::TextField(context, fieldHost, selectorField);
		context.SetTooltip(field, "add a selector to the first sheet this document declares (.class, #id or a tag)");

		ListViewDesc rules;
		rules.layout = ListLayout::Rows;
		rules.mode = ListSelectionMode::Multiple;
		rules.onSelectionChanged = [this]() { PublishRuleSelectionFromList(); };
		_rulesList.Build(this, context, host, std::move(rules));
	}

	void UIBuilderPanel::BuildInspectorPane(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle host = context.CreateElement();
		context.AppendChild(parent, host);
		context.SetStyle(host, FillColumn(1.0f));

		_inspectorParent = FillScrollView(context, host);
	}

	void UIBuilderPanel::BuildHierarchyPane(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle host = context.CreateElement();
		context.AppendChild(parent, host);
		context.SetStyle(host, FillColumn(1.0f));

		ListViewDesc hierarchy;
		hierarchy.layout = ListLayout::Rows;
		hierarchy.mode = ListSelectionMode::Multiple;
		hierarchy.onSelectionChanged = [this]() { PublishHierarchySelectionFromList(); };
		hierarchy.onDelete = [this]() { DeleteSelectedNodes(); };
		hierarchy.onDuplicate = [this]() { DuplicateSelectedNodes(); };
		hierarchy.onBuildTrailing = [this](UI::Context& context, UI::ElementHandle host2) { _gaps.DrainInto(context, host2); };
		_hierarchyList.Build(this, context, host, std::move(hierarchy));
	}

	void UIBuilderPanel::BuildLibraryPane(UI::Context& context, UI::ElementHandle parent) {
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};

		UI::ElementHandle host = context.CreateElement();
		context.AppendChild(parent, host);
		context.SetStyle(host, FillColumn(1.0f));

		UI::ElementHandle viewport = FillScrollView(context, host);

		Vector<UIBuilderOps::LibraryGroup> groups{GetMemoryResource(MemTag::Editor)};
		UIBuilderOps::BuildLibrary(groups);

		for (const UIBuilderOps::LibraryGroup& group : groups) {
			PaneHeader(context, viewport, group.category.empty() ? "Uncategorized" : group.category, font);

			UI::ElementHandle tiles = context.CreateElement();
			context.AppendChild(viewport, tiles);
			UI::Style tilesStyle;
			tilesStyle.flexDirection = UI::FlexDirection::Column;
			tilesStyle.alignItems = UI::Align::Stretch;
			tilesStyle.padding = {4.0f, 4.0f, 4.0f, 4.0f};
			context.SetStyle(tiles, tilesStyle);

			for (const UI::WidgetDesc* widget : group.widgets) {
				UI::ElementHandle tile = context.CreateElement();
				context.AppendChild(tiles, tile);
				context.AddClass(tile, EditorTheme::Class::SectionCard);
				context.AddClass(tile, EditorTheme::Class::Clickable);
				UI::Style tileStyle;
				tileStyle.padding = {6.0f, 4.0f, 6.0f, 4.0f};
				tileStyle.margin = {0.0f, 0.0f, 0.0f, 3.0f};
				context.SetStyle(tile, tileStyle);
				context.SetText(tile, font, RowFont(), Text(), widget->tag);
				context.SetTooltip(tile, str("<") + str(widget->tag) +
											 ">. Drag onto the canvas or the Hierarchy (or double-click to add it)");

				str tag{widget->tag};

				context.Events(tile).onDoubleClick = [this, tag]() { AppendFromLibrary(tag); };
				context.Events(tile).onRightClick = [this, tag]() {
					UI::Context* c = GetPanelContext();
					if (!c) {
						return;
					}
					Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
					UI::Widgets::MenuItem& addItem = items.emplace_back();
					addItem.label = "Add";
					addItem.enabled = _document.IsValid();
					addItem.onSelect = [this, tag]() { AppendFromLibrary(tag); };
					UI::Widgets::OpenMenu(*c, c->GetPointerPosition(), items,
										  UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{}, RowFont());
				};
				context.Events(tile).onPressDrag = [tag](Vec2) {
					if (UISystem::IsDragPayloadActive()) {
						return;
					}
					UI::DragPayload payload;
					payload.type = "UIWidgetTag"_hash;
					payload.data = Variant(tag);
					payload.display = str("<") + tag + ">";
					UISystem::BeginDragPayload(payload);
				};
			}
		}
	}

	void UIBuilderPanel::BuildViewportPane(UI::Context& context, UI::ElementHandle parent) {
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};

		UI::ElementHandle column = parent;

		UI::ElementHandle bar = context.CreateElement();
		context.AppendChild(column, bar);
		context.AddClass(bar, EditorTheme::Class::ChromeBar);
		UI::Style barStyle;
		barStyle.padding = {8.0f, 3.0f, 8.0f, 3.0f};
		barStyle.alignItems = UI::Align::Center;
		context.SetStyle(bar, barStyle);

		UI::ElementHandle fileMenu = context.CreateElement();
		context.AppendChild(bar, fileMenu);
		context.AddClass(fileMenu, EditorTheme::Class::SectionCard);
		context.AddClass(fileMenu, EditorTheme::Class::Clickable);
		UI::Style fileStyle;
		fileStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		fileStyle.flexShrink = 0.0f;
		context.SetStyle(fileMenu, fileStyle);
		context.SetText(fileMenu, font, RowFont(), Text(), "File");
		context.Events(fileMenu).onClick = [this]() { OpenFileMenu(); };

		_statusLabel = context.CreateElement();
		context.AppendChild(bar, _statusLabel);
		UI::Style statusStyle;
		statusStyle.margin = {8.0f, 0.0f, 0.0f, 0.0f};
		statusStyle.flexGrow = 1.0f;
		statusStyle.flexShrink = 1.0f;
		statusStyle.width = UI::Length::Pixel(0.0f);
		statusStyle.textOverflow = UI::TextOverflow::Ellipsis;
		context.SetStyle(_statusLabel, statusStyle);
		context.SetText(_statusLabel, font, RowFont(), TextDim(),
						_status.empty() ? NoDocumentText : std::string_view(_status));

		BuildCanvasToolbar(context, bar);

		UI::ElementHandle canvasHost = context.CreateElement();
		context.AppendChild(column, canvasHost);
		UI::Style hostStyle;
		hostStyle.width = UI::Length::Percent(1.0f);
		hostStyle.height = UI::Length::Pixel(0.0f);
		hostStyle.flexGrow = 1.0f;
		hostStyle.overflow = UI::Overflow::Hidden;
		context.SetStyle(canvasHost, hostStyle);

		_viewport = context.CreateElement();
		context.AppendChild(canvasHost, _viewport);
		UI::Style viewportStyle;
		viewportStyle.width = UI::Length::Percent(1.0f);
		viewportStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(_viewport, viewportStyle);
		context.AddClass(_viewport, EditorTheme::Class::ViewportPlaceholder);
		context.SetFocusable(_viewport, true);

		context.Events(_viewport).customDraw =
			[this](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				const UI::Rect area = UI::SnapRectToPixels(CanvasRect(c.GetLayout(element).content));
				if (area.w <= 1.0f || area.h <= 1.0f) {
					return;
				}
				RHI::TextureHandle texture = UISystem::GetSurfaceTexture(_surface);
				if (texture.IsValid()) {
					paint.EmitTexturedQuad(area, texture, {0.0f, 0.0f, 1.0f, 1.0f}, Color::White(),
										   UI::SamplerKind::LinearClamp);
				}

				UI::Context* preview = UISystem::GetSurfaceContext(_surface);
				if (_preview || !preview || _selected == UIBuilderOps::INVALID_NODE) {
					return;
				}
				UIDocumentAsset* document = _document.Get();
				if (!document) {
					return;
				}
				UI::ElementHandle selected =
					UIBuilderOps::ElementForNode(_mapping, UIBuilderOps::NodeAt(*document, _selected));
				if (!preview->IsAlive(selected)) {
					return;
				}
				UI::Rect box = preview->GetLayout(selected).borderBox;
				box.x += area.x;
				box.y += area.y;
				paint.EmitRing(UI::SnapRectToPixels(box),
							   {SELECTION_OUTLINE_PX, SELECTION_OUTLINE_PX, SELECTION_OUTLINE_PX, SELECTION_OUTLINE_PX},
							   Accent());
			};

		context.Events(_viewport).onClick = [this]() {
			if (_preview) {
				return;
			}
			UI::Context* c = GetPanelContext();
			if (c) {
				PickAt(*c, c->GetPointerPosition());
			}
		};
		context.Events(_viewport).onMouseDown = [this]() {
			if (UI::Context* c = GetPanelContext()) {
				RoutePointerToPreview(*c, InputEventType::MouseDown);
			}
		};
		context.Events(_viewport).onMouseUp = [this]() {
			if (UI::Context* c = GetPanelContext()) {
				RoutePointerToPreview(*c, InputEventType::MouseUp);
			}
		};

		context.Events(_viewport).onWheel = [this](Vec2 notches) {
			UI::Context* c = GetPanelContext();
			if (!c || !c->IsAlive(_viewport)) {
				return false;
			}
			if (_preview) {
				RoutePointerToPreview(*c, InputEventType::MouseWheel, notches);
				return true;
			}
			const UI::Rect content = c->GetLayout(_viewport).content;
			LeaveFit(content);
			ApplyView(UIBuilderOps::ZoomAtPoint(content, _canvasWidth, _canvasHeight, CurrentView(content),
												c->GetPointerPosition(), notches.y));
			return true;
		};

		context.Events(_viewport).onMiddleDrag = [this](Vec2 delta) {
			UI::Context* c = GetPanelContext();
			if (!c || _preview || !c->IsAlive(_viewport)) {
				return;
			}
			const UI::Rect content = c->GetLayout(_viewport).content;
			LeaveFit(content);
			ApplyView(UIBuilderOps::PanBy(content, _canvasWidth, _canvasHeight, CurrentView(content), delta));
		};

		context.Events(_viewport).onDragOver = [this](const UI::DragPayload& payload) {
			if (_preview) {
				return false;
			}
			UI::Context* c = GetPanelContext();
			UIDocumentAsset* document = c ? _document.Get() : nullptr;
			if (!document) {
				return false;
			}
			str tag;
			u64 panel = 0;
			std::span<const u32> nodes;
			return PayloadWidgetTag(payload, tag) || (PayloadNodes(payload, panel, nodes) && panel == GetInstanceId());
		};
		context.Events(_viewport).onDrop = [this](const UI::DragPayload& payload, Vec2) {
			UI::Context* c = GetPanelContext();
			UIDocumentAsset* document = c ? _document.Get() : nullptr;
			if (_preview || !document) {
				return;
			}
			u32 parent = 0;
			UI::Context* preview = UISystem::GetSurfaceContext(_surface);
			if (preview && c->IsAlive(_viewport)) {
				const UI::Rect area = UI::SnapRectToPixels(CanvasRect(c->GetLayout(_viewport).content));
				Vec2 point = c->GetPointerPosition();
				Vec2 local{point.x - area.x, point.y - area.y};
				if (local.x >= 0.0f && local.y >= 0.0f && local.x < area.w && local.y < area.h) {
					u32 hit = UIBuilderOps::NodeForElement(*preview, *document, _mapping, preview->HitTest(local));
					if (hit != UIBuilderOps::INVALID_NODE && UIDocumentOps::CanTakeChildren(*document, hit)) {
						parent = hit;
					}
				}
			}
			DeliverDrop(payload, parent, UIDocumentOps::GetChildCount(*document, parent));
		};

		_nameChip = context.CreateElement();
		context.AppendChild(canvasHost, _nameChip);
		context.AddClass(_nameChip, EditorTheme::Class::SectionCard);
		UI::Style chipStyle;
		chipStyle.position = UI::Position::Absolute;
		chipStyle.flexDirection = UI::FlexDirection::Row;
		chipStyle.alignItems = UI::Align::Center;
		chipStyle.padding = {5.0f, 1.0f, 5.0f, 1.0f};
		chipStyle.left = UI::Length::Pixel(0.0f);
		chipStyle.top = UI::Length::Pixel(0.0f);
		context.SetStyle(_nameChip, chipStyle);
		context.SetVisible(_nameChip, false);

		UI::ElementHandle chipLabel = context.CreateElement();
		context.AppendChild(_nameChip, chipLabel);
		context.SetHoverable(chipLabel, false);
		context.SetText(chipLabel, font, EditorTheme::GetMetric(MetricToken::FontTiny), Accent(), "");

		struct AlignButton {
			EditorIcons::Icon icon;
			std::string_view fallback;
			std::string_view value;
			std::string_view tip;
		};
		const AlignButton alignButtons[] = {
			{EditorIcons::Glyph::AlignStart, "|<", "flex-start", "align-self: flex-start"},
			{EditorIcons::Glyph::AlignCenter, "><", "center", "align-self: center"},
			{EditorIcons::Glyph::AlignEnd, ">|", "flex-end", "align-self: flex-end"},
			{EditorIcons::Glyph::AlignStretch, "<>", "stretch", "align-self: stretch"},
		};
		for (const AlignButton& button : alignButtons) {
			UI::ElementHandle element = context.CreateElement();
			context.AppendChild(_nameChip, element);
			context.AddClass(element, EditorTheme::Class::Clickable);
			UI::Style buttonStyle;
			buttonStyle.margin = {4.0f, 0.0f, 0.0f, 0.0f};
			context.SetStyle(element, buttonStyle);
			EditorIcons::SetGlyphOrText(context, element, button.icon, button.fallback, TextDim(),
										EditorIcons::GutterSizePx(), EditorTheme::GetMetric(MetricToken::FontTiny));
			context.SetTooltip(element, button.tip);
			context.Events(element).onClick = [this, value = button.value]() {
				WriteDeclaration("align-self", value);
			};
		}

		BuildTextPanes(context, column);
	}

	void UIBuilderPanel::BuildTextPanes(UI::Context& context, UI::ElementHandle parent) {
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};

		UI::ElementHandle bar = context.CreateElement();
		context.AppendChild(parent, bar);
		context.AddClass(bar, EditorTheme::Class::ChromeBar);
		context.AddClass(bar, EditorTheme::Class::Clickable);
		UI::Style barStyle;
		barStyle.flexDirection = UI::FlexDirection::Row;
		barStyle.alignItems = UI::Align::Center;
		barStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		context.SetStyle(bar, barStyle);
		context.SetText(bar, font, RowFont(), TextDim(), _textPanesOpen ? "v  Text preview" : ">  Text preview");
		context.SetTooltip(bar, "the documents as they would be written to disk");

		UI::ElementHandle row = context.CreateElement();
		context.AppendChild(parent, row);
		context.SetVisible(row, _textPanesOpen);
		context.Events(bar).onClick = [this, bar, row]() {
			_textPanesOpen = !_textPanesOpen;
			if (UI::Context* c = GetPanelContext()) {
				c->SetVisible(row, _textPanesOpen);
				c->SetTextString(bar, _textPanesOpen ? "v  Text preview" : ">  Text preview");
				RebuildTextPanes(*c);
			}
		};

		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		rowStyle.alignItems = UI::Align::Stretch;
		rowStyle.width = UI::Length::Percent(1.0f);
		rowStyle.height = UI::Length::Percent(TEXT_PANE_FRACTION);
		rowStyle.overflow = UI::Overflow::Hidden;
		context.SetStyle(row, rowStyle);

		UI::Widgets::SplitDesc split;
		split.sizing = UI::Widgets::SplitSizing::Ratio;
		split.position = _textPaneRatio;
		split.minFirstPx = TEXT_PANE_MIN_PX;
		split.minSecondPx = TEXT_PANE_MIN_PX;
		split.onPositionChanged = [this](f32 ratio) { _textPaneRatio = ratio; };
		UI::Widgets::SplitParts panes = UI::Widgets::Split(context, row, split);

		auto pane = [&](UI::ElementHandle host, std::string_view title) {
			PaneHeader(context, host, title, font);
			return FillScrollView(context, host);
		};
		_layoutTextParent = pane(panes.first, "Layout document");
		_styleTextParent = pane(panes.second, "Style document");
	}

	void UIBuilderPanel::OpenFileMenu() {
		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};

		UI::Widgets::MenuItem save;
		save.label = "Save";
		save.enabled = _document.IsValid();
		save.onSelect = [this]() { SaveDocument(); };
		items.push_back(std::move(save));

		UI::Widgets::MenuItem reload;
		reload.label = "Revert to Saved";
		reload.separatorBefore = true;
		reload.enabled = _document.IsValid() && !_documentPath.empty();
		reload.onSelect = [this]() {
			UIDocumentAsset* document = _document.Get();
			if (!document) {
				return;
			}
			for (const UIDocumentSheet& sheet : document->sheets) {
				if (StyleSheetAsset* asset = sheet.sheet.Get()) {
					EditorAssetOps::RevertToFile(Handle<Asset>(asset));
				}
			}
			if (!EditorAssetOps::RevertToFile(Handle<Asset>(document))) {
				if (UI::Context* c = GetPanelContext()) {
					_status = str("'") + _documentPath + "' couldn't be read back. Nothing was reverted";
					SetStatus(*c, _status);
				}
			}
		};
		items.push_back(std::move(reload));

		UI::Widgets::OpenMenu(*context, context->GetPointerPosition(), items,
							  UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{}, RowFont());
	}

	void UIBuilderPanel::BuildCanvasToolbar(UI::Context& context, UI::ElementHandle parent) {
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
		const f32 fontSize = RowFont();

		std::string_view canvasItems[std::size(CanvasPresets)];
		UI::Widgets::DropdownDesc canvasDesc;
		for (u32 i = 0; i < std::size(CanvasPresets); ++i) {
			canvasItems[i] = CanvasPresets[i].label;
			if (CanvasPresets[i].width == _canvasWidth && CanvasPresets[i].height == _canvasHeight) {
				canvasDesc.selected = i;
			}
		}
		canvasDesc.items = canvasItems;
		canvasDesc.font = font;
		canvasDesc.fontSizePx = fontSize;
		canvasDesc.onSelect = [this](u32 index) {
			if (index >= std::size(CanvasPresets)) {
				return;
			}
			_canvasWidth = CanvasPresets[index].width;
			_canvasHeight = CanvasPresets[index].height;
			if (UI::Context* c = GetPanelContext()) {
				SyncSurfaceSize(*c);
				RepopulatePreview();
				RebuildInspector(*c);
			}
		};
		UI::Widgets::Dropdown(context, parent, canvasDesc);

		str zoomLabels[std::size(ZOOM_STOPS)];
		std::string_view zoomItems[std::size(ZOOM_STOPS) + 1];
		zoomItems[0] = "Fit";
		UI::Widgets::DropdownDesc zoomDesc;
		for (u32 i = 0; i < std::size(ZOOM_STOPS); ++i) {
			zoomLabels[i] = std::to_string(static_cast<i32>(ZOOM_STOPS[i] * 100.0f + 0.5f)) + "%";
			zoomItems[i + 1] = zoomLabels[i];
			if (!_fitCanvas && ZOOM_STOPS[i] == _zoom) {
				zoomDesc.selected = i + 1;
			}
		}
		zoomDesc.items = zoomItems;
		zoomDesc.font = font;
		zoomDesc.fontSizePx = fontSize;
		zoomDesc.onSelect = [this](u32 index) {
			if (index > std::size(ZOOM_STOPS)) {
				return;
			}
			_fitCanvas = index == 0;
			if (!_fitCanvas) {
				_zoom = ZOOM_STOPS[index - 1];
			}
			_pan = {};
			if (UI::Context* c = GetPanelContext()) {
				SyncSurfaceSize(*c);
				RebuildInspector(*c);
			}
		};
		UI::Widgets::Dropdown(context, parent, zoomDesc);

		UI::ElementHandle preview = context.CreateElement();
		context.AppendChild(parent, preview);
		context.AddClass(preview, EditorTheme::Class::SectionCard);
		context.AddClass(preview, EditorTheme::Class::Clickable);
		if (_preview) {
			context.AddClass(preview, StringHash("slt-selected"));
		}
		UI::Style previewStyle;
		previewStyle.margin = {6.0f, 0.0f, 0.0f, 0.0f};
		previewStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		previewStyle.flexShrink = 0.0f;
		previewStyle.textOverflow = UI::TextOverflow::Clip;
		context.SetStyle(preview, previewStyle);
		context.SetText(preview, font, fontSize, _preview ? Accent() : Text(), "Preview");
		context.SetTooltip(preview, _preview ? "back to authoring: click selects, drag reparents"
											 : "runtime semantics: input reaches the UI and :hover runs");
		context.Events(preview).onClick = [this, preview, font, fontSize]() {
			_preview = !_preview;
			UI::Context* c = GetPanelContext();
			if (!c) {
				return;
			}
			if (_preview) {
				c->AddClass(preview, StringHash("slt-selected"));
				c->SetVisible(_nameChip, false);
			} else {
				c->RemoveClass(preview, StringHash("slt-selected"));
			}
			c->SetText(preview, font, fontSize, _preview ? Accent() : Text(), "Preview");
			c->SetTooltip(preview, _preview ? "back to authoring: click selects, drag reparents"
											: "runtime semantics: input reaches the UI and :hover runs");
			_previewPointerInside = false;
			RepopulatePreview();
			RebuildPanes(*c);
		};

		PanelHeader::OverflowButton(context, parent, [this](Vector<UI::Widgets::MenuItem>& items) {
			UI::Widgets::MenuItem debugger;
			debugger.label = "UI Debugger";
			debugger.enabled = _surface != INVALID_UI_SURFACE;
			debugger.onSelect = [this]() {
				UIDebugOps::Target target;
				target.kind = UIDebugOps::Target::Kind::Surface;
				target.id = _surface;
				UIDebugOps::OpenDebuggerOn(target);
			};
			items.push_back(std::move(debugger));
		});
	}

	bool UIBuilderPanel::OpenDocument(std::string_view virtualPath) {
		if (_document.IsValid()) {
			AssetDatabase::UnloadAsset(_document.Get());
			_document = {};
		}
		_documentPath = str(virtualPath);
		_selected = UIBuilderOps::INVALID_NODE;
		_populated = nullptr;
		_mapping.clear();
		_collapsed.clear();

		bool opened = false;
		str status{NoDocumentText};
		if (!virtualPath.empty()) {
			Asset* asset = AssetDatabase::LoadAsset(Path(str(virtualPath)));
			if (asset && asset->GetAssetTypeId() == StringHash("UIDocumentAsset")) {
				_document = Handle<UIDocumentAsset>(static_cast<UIDocumentAsset*>(asset));
				status = str(virtualPath);
				opened = true;
			} else {
				if (asset) {
					AssetDatabase::UnloadAsset(asset);
				}
				status = str("could not open '") + str(virtualPath) + "' as a layout document";
				SLT_LOG_WARN("Editor", "UIBuilder: {}", status);
			}
		}

		_status = status;
		_subject = UIBuilderOps::Subject::None;
		_selectedRule = UIBuilderOps::INVALID_NODE;
		if (UI::Context* context = GetPanelContext()) {
			SetStatus(*context, _status);
			RebuildPanes(*context);
		}
		RepopulatePreview();
		return opened;
	}

	bool UIBuilderPanel::ShowSheetRule(std::string_view sheetVirtualPath, u32 rule, std::string_view selector) {
		const UIDocumentAsset* document = _document.Get();
		if (!document) {
			return false;
		}
		u32 sheetIndex = UIBuilderOps::INVALID_NODE;
		for (u32 index = 0; index < static_cast<u32>(document->sheets.size()); ++index) {
			const UIDocumentSheet& declared = document->sheets[index];
			const StyleSheetAsset* asset = declared.sheet.Get();
			std::string_view key = (asset && !asset->virtualPath.empty()) ? std::string_view(asset->virtualPath)
																		  : std::string_view(declared.src);
			if (key == sheetVirtualPath) {
				sheetIndex = index;
				break;
			}
		}
		if (sheetIndex == UIBuilderOps::INVALID_NODE) {
			return false;
		}

		UIBuilderOps::BuildSheetRows(*document, _sheetRows);
		u32 found = UIBuilderOps::INVALID_NODE;
		u32 byText = UIBuilderOps::INVALID_NODE;
		for (const UIBuilderOps::SheetRow& row : _sheetRows) {
			if (row.sheet != sheetIndex || row.rule == UIBuilderOps::INVALID_NODE) {
				continue;
			}
			const bool sameText = selector.starts_with(row.selector) && selector.size() == row.selector.size() + row.pseudo.size() && selector.ends_with(row.pseudo);
			if (row.rule == rule && sameText) {
				found = row.rule;
				break;
			}
			if (sameText && byText == UIBuilderOps::INVALID_NODE) {
				byText = row.rule;
			}
		}
		if (found == UIBuilderOps::INVALID_NODE) {
			found = byText;
		}
		if (found == UIBuilderOps::INVALID_NODE) {
			return false;
		}

		_subject = UIBuilderOps::Subject::Selector;
		_selectedSheet = sheetIndex;
		_selectedRule = found;
		_selected = UIBuilderOps::INVALID_NODE;
		_selectedNodes.clear();
		if (UI::Context* context = GetPanelContext()) {
			PushHierarchySelectionToList();
			RebuildPanes(*context);
		}
		return true;
	}

	void UIBuilderPanel::RebuildPanes(UI::Context& context) {
		RebuildRows(context);
		RebuildSheetRows(context);
		RebuildInspector(context);
		RebuildTextPanes(context);
	}

	void UIBuilderPanel::RepopulatePreview() {
		UI::Context* preview = UISystem::GetSurfaceContext(_surface);
		if (!preview) {
			_populated = nullptr;
			return;
		}
		preview->ClearChildren(preview->GetRoot());
		_mapping.clear();

		UIDocumentAsset* document = _document.Get();
		_populated = document;
		_populatedContent = document ? document->GetContentVersion() : 0;
		if (!document) {
			return;
		}
		UIInstantiateDesc desc;
		desc.font = UISystem::GetDefaultFont();
		desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
		desc.textColor = Text();
		document->Instantiate(*preview, preview->GetRoot(), desc, _mapping);
	}

	bool UIBuilderPanel::EditDocument(std::string_view label, EditShape shape,
									  const std::function<bool(UIDocumentAsset&, str&)>& mutate) {
		UIDocumentAsset* document = _document.Get();
		UI::Context* context = GetPanelContext();
		if (!document || !context) {
			return false;
		}
		UIDocumentOps::Snapshot before;
		UIDocumentOps::Capture(*document, before);

		str error;
		if (!mutate(*document, error)) {
			UIDocumentOps::Restore(*document, before);
			if (!error.empty()) {
				SetStatus(*context, error);
				_status = error;
				SLT_LOG_WARN("Editor", "UIBuilder: {}", error);
			}
			return false;
		}

		UIDocumentOps::Snapshot after;
		UIDocumentOps::Capture(*document, after);
		UndoSystem::Push(MakeUnique<UISetDocumentCommand>(_document, std::move(before), std::move(after), label));

		if (shape == EditShape::Structure) {
			_collapsed.clear();
			SelectNode(UIBuilderOps::INVALID_NODE);
		} else if (_selected != UIBuilderOps::INVALID_NODE && !UIBuilderOps::NodeAt(*document, _selected)) {
			SelectNode(UIBuilderOps::INVALID_NODE);
		}
		_status = _documentPath.empty() ? str(NoDocumentText) : _documentPath;
		SetStatus(*context, _status);
		RepopulatePreview();
		RebuildPanes(*context);
		_undoDepth = UndoSystem::GetUndoDepth();
		_redoDepth = UndoSystem::GetRedoDepth();
		return true;
	}

	bool UIBuilderPanel::EditSheet(Handle<StyleSheetAsset> sheet, std::string_view label, std::string_view source) {
		StyleSheetAsset* resolved = sheet.Get();
		UI::Context* context = GetPanelContext();
		if (!resolved || !context) {
			return false;
		}
		UndoSystem::Push(MakeUnique<UISetSheetSourceCommand>(sheet, resolved->source, str(source), label));
		if (UI::Context* preview = UISystem::GetSurfaceContext(_surface)) {
			preview->ClearStyleSheets();
		}
		RepopulatePreview();
		RebuildPanes(*context);
		_undoDepth = UndoSystem::GetUndoDepth();
		_redoDepth = UndoSystem::GetRedoDepth();
		return true;
	}

	void UIBuilderPanel::SaveDocument() {
		UIDocumentAsset* document = _document.Get();
		UI::Context* context = GetPanelContext();
		if (!document || !context) {
			return;
		}
		for (const UIDocumentSheet& sheet : document->sheets) {
			if (StyleSheetAsset* asset = sheet.sheet.Get(); asset && !asset->virtualPath.empty()) {
				EditorAssetOps::SaveAsset(Handle<Asset>(asset));
			}
		}
		_status = UIDocumentOps::Save(*document) ? _documentPath + "  - saved"
												 : str("could not save '") + _documentPath + "'";
		SetStatus(*context, _status);
	}

	void UIBuilderPanel::AddSelector(std::string_view selector) {
		UIDocumentAsset* document = _document.Get();
		while (!selector.empty() && (selector.front() == ' ' || selector.front() == '\t')) {
			selector.remove_prefix(1);
		}
		while (!selector.empty() && (selector.back() == ' ' || selector.back() == '\t')) {
			selector.remove_suffix(1);
		}
		if (!document || selector.empty()) {
			return;
		}
		if (document->sheets.empty()) {
			if (UI::Context* context = GetPanelContext()) {
				_status = "this document declares no <sheet> (nowhere to put a selector)";
				SetStatus(*context, _status);
			}
			return;
		}
		Handle<StyleSheetAsset> sheet = document->sheets[0].sheet;
		StyleSheetAsset* resolved = sheet.Get();
		if (!resolved) {
			return;
		}
		Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Editor)};
		UI::StyleSheet::SplitSource(resolved->source, rules);
		for (const UI::StyleRuleSource& rule : rules) {
			if (rule.selector == selector) {
				if (UI::Context* context = GetPanelContext()) {
					_status = str("'") + str(selector) + "' is already a rule in this sheet";
					SetStatus(*context, _status);
				}
				return;
			}
		}
		EditSheet(sheet, str("Add ") + str(selector),
				  UIBuilderOps::AppendRule(resolved->source, selector, {}));
	}

	bool UIBuilderPanel::IsCollapsed(u32 node) const {
		return std::find(_collapsed.begin(), _collapsed.end(), node) != _collapsed.end();
	}

	void UIBuilderPanel::RebuildRows(UI::Context& context) {
		_rows.clear();
		_gaps.BeginBuild(context);
		Vector<ListRow> listRows{GetMemoryResource(MemTag::Editor)};

		UIDocumentAsset* document = _document.Get();
		if (!document) {
			_hierarchyList.SetRows(listRows);
			return;
		}
		UIBuilderOps::BuildRows(*document, _rows);

		u32 hideBelow = 0xFFFFFFFFu;
		u32 previousDepth = 0;
		u32 lastParent = 0;
		for (size_t i = 0; i < _rows.size(); ++i) {
			const UIBuilderOps::Row& row = _rows[i];
			if (row.depth <= hideBelow) {
				hideBelow = 0xFFFFFFFFu;
			}
			if (row.depth > hideBelow) {
				continue;
			}

			UIDocumentOps::Location location = UIDocumentOps::LocationOf(*document, row.index);
			if (location.parent != UIBuilderOps::INVALID_NODE) {
				BuildInsertStrip(context, location.parent, location.childIndex);
				lastParent = location.parent;
			}

			const bool hasChildren =
				row.node->templateName.empty() && !row.node->children.empty();
			const bool collapsed = IsCollapsed(row.index);

			ListRow desc;
			desc.id = NodeRowId(row.index);
			desc.depth = row.depth;
			desc.hasChildren = hasChildren;
			desc.expanded = !collapsed;
			desc.label = UIBuilderOps::RowLabel(*row.node);
			desc.onBuildLeading = _gaps.Take();
			if (hasChildren) {
				desc.onToggleExpand = [this, index = row.index](bool) {
					auto at = std::find(_collapsed.begin(), _collapsed.end(), index);
					if (at == _collapsed.end()) {
						_collapsed.push_back(index);
					} else {
						_collapsed.erase(at);
					}
					if (UI::Context* c = GetPanelContext()) {
						RebuildRows(*c);
					}
				};
			}
			desc.onRightClick = [this, index = row.index](UI::Context& context) {
				OpenNodeMenu(context, index);
			};
			if (location.parent != UIBuilderOps::INVALID_NODE) {
				desc.onDragRow = [this, index = row.index](Vec2) {
					if (UISystem::IsDragPayloadActive()) {
						return;
					}
					UIDocumentAsset* live = _document.Get();
					const UIDocumentNode* node = live ? UIBuilderOps::NodeAt(*live, index) : nullptr;
					if (!node) {
						return;
					}
					Vector<u64> selection{GetMemoryResource(MemTag::Editor)};
					selection.assign(_selectedNodes.begin(), _selectedNodes.end());
					Vector<u64> targets{GetMemoryResource(MemTag::Editor)};
					EditorSelectionOps::ResolveTargets(selection, index, targets);
					Vector<u32> run{GetMemoryResource(MemTag::Editor)};
					run.assign(targets.begin(), targets.end());
					UIBuilderNodeDragData data;
					data.panelId = GetInstanceId();
					UIDocumentOps::GetOutermost(*live, run, data.nodes);
					if (data.nodes.empty()) {
						return;
					}
					UI::DragPayload payload;
					payload.type = "UIBuilderNode"_hash;
					payload.display = data.nodes.size() == 1
										  ? UIBuilderOps::RowLabel(*UIBuilderOps::NodeAt(*live, data.nodes.front()))
										  : str("<Multiple>");
					payload.data = Variant(std::move(data));
					UISystem::BeginDragPayload(payload);
				};
			}
			desc.onDragOver = [this, index = row.index](const UI::DragPayload& payload) {
				return AcceptsDrop(payload, index);
			};
			desc.onDrop = [this, index = row.index](const UI::DragPayload& payload, Vec2) {
				UIDocumentAsset* live = _document.Get();
				DeliverDrop(payload, index, live ? UIDocumentOps::GetChildCount(*live, index) : 0u);
			};

			EditorIcons::Icon icon = row.node->templateName.empty() ? EditorIcons::Glyph::UIElement
																	: EditorIcons::Glyph::AssetPrefab;
			desc.onDecorate = [icon, hasChildren, collapsed](UI::Context& rowContext, const UI::Widgets::TreeRowParts& parts) {
				EditorIcons::SetRowGutter(rowContext, parts, hasChildren, !collapsed, TextDim(), icon, TextDim());
			};
			listRows.push_back(std::move(desc));

			if (collapsed) {
				hideBelow = row.depth;
			}
			previousDepth = row.depth;
		}
		(void)previousDepth;

		if (!_rows.empty() && lastParent != UIBuilderOps::INVALID_NODE) {
			BuildInsertStrip(context, lastParent, UIDocumentOps::GetChildCount(*document, lastParent));
		}

		_hierarchyList.SetRows(listRows);
		PushHierarchySelectionToList();
	}

	void UIBuilderPanel::BuildInsertStrip(UI::Context& context, u32 parent, u32 childIndex) {
		EditorInsertStripDesc desc;
		desc.onDragOver = [this, parent](const UI::DragPayload& payload) {
			return AcceptsDrop(payload, parent);
		};
		desc.onDrop = [this, parent, childIndex](const UI::DragPayload& payload, Vec2) {
			DeliverDrop(payload, parent, childIndex);
		};
		_gaps.PendStrip(std::move(desc));
	}

	bool UIBuilderPanel::AcceptsDrop(const UI::DragPayload& payload, u32 parent) const {
		const UIDocumentAsset* document = _document.Get();
		if (!document || !UIDocumentOps::CanTakeChildren(*document, parent)) {
			return false;
		}
		str tag;
		if (PayloadWidgetTag(payload, tag)) {
			return true;
		}
		u64 panel = 0;
		std::span<const u32> nodes;
		if (!PayloadNodes(payload, panel, nodes)) {
			return false;
		}
		return panel == GetInstanceId() && std::none_of(nodes.begin(), nodes.end(), [&](u32 node) {
				   return UIDocumentOps::IsInSubtree(*document, node, parent);
			   });
	}

	void UIBuilderPanel::DeliverDrop(const UI::DragPayload& payload, u32 parent, u32 childIndex) {
		str tag;
		if (PayloadWidgetTag(payload, tag)) {
			AddElement(UIDocumentOps::MakeElement(tag), parent, childIndex, str("Add <") + tag + ">");
			return;
		}
		u64 panel = 0;
		std::span<const u32> nodes;
		if (!PayloadNodes(payload, panel, nodes) || panel != GetInstanceId()) {
			return;
		}
		str label = nodes.size() > 1 ? std::format("Move {} Elements", nodes.size()) : str("Move Element");
		EditDocument(label, EditShape::Structure, [&](UIDocumentAsset& document, str& error) {
			return UIDocumentOps::MoveNodes(document, nodes, parent, childIndex, error);
		});
	}

	void UIBuilderPanel::AddElement(const UIDocumentNode& node, u32 parent, u32 childIndex, std::string_view label) {
		UIDocumentAsset* document = _document.Get();
		if (!document) {
			return;
		}
		const u32 at = std::min(childIndex, UIDocumentOps::GetChildCount(*document, parent));
		const bool added = EditDocument(label, EditShape::Structure, [&](UIDocumentAsset& live, str& error) {
			return UIDocumentOps::Insert(live, parent, childIndex, node, error);
		});
		if (added) {
			SelectNode(UIDocumentOps::ChildAt(*document, parent, at));
		}
	}

	void UIBuilderPanel::AppendFromLibrary(std::string_view tag) {
		UIDocumentAsset* document = _document.Get();
		if (!document) {
			return;
		}
		AddElement(UIDocumentOps::MakeElement(tag), 0, UIDocumentOps::GetChildCount(*document, 0),
				   str("Add <") + str(tag) + ">");
	}

	void UIBuilderPanel::OpenNodeMenu(UI::Context& context, u32 node) {
		UIDocumentAsset* document = _document.Get();
		const UIDocumentNode* resolved = document ? UIBuilderOps::NodeAt(*document, node) : nullptr;
		if (!resolved) {
			return;
		}
		const bool isInstance = !resolved->templateName.empty();
		const bool isRoot = UIDocumentOps::LocationOf(*document, node).parent == UIBuilderOps::INVALID_NODE;

		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};

		UI::Widgets::MenuItem add;
		add.label = "Add Child Element";
		add.enabled = UIDocumentOps::CanTakeChildren(*document, node);
		UI::Widgets::MenuItem& plain = add.children.emplace_back();
		plain.label = "Element";
		plain.onSelect = [this, node]() {
			UIDocumentAsset* live = _document.Get();
			if (!live) {
				return;
			}
			UIDocumentNode child = UIDocumentOps::MakeElement("panel");
			child.id = UIDocumentOps::UniqueId(*live, "element");
			AddElement(child, node, UIDocumentOps::GetChildCount(*live, node), "Add Element");
		};
		Vector<UIBuilderOps::LibraryGroup> groups{GetMemoryResource(MemTag::Editor)};
		UIBuilderOps::BuildLibrary(groups);
		for (const UIBuilderOps::LibraryGroup& group : groups) {
			bool firstOfGroup = true;
			for (const UI::WidgetDesc* widget : group.widgets) {
				UI::Widgets::MenuItem& entry = add.children.emplace_back();
				entry.label = str(widget->tag);
				entry.separatorBefore = firstOfGroup;
				firstOfGroup = false;
				entry.onSelect = [this, node, tag = str(widget->tag)]() {
					if (UIDocumentAsset* live = _document.Get()) {
						AddElement(UIDocumentOps::MakeElement(tag), node, UIDocumentOps::GetChildCount(*live, node),
								   str("Add <") + tag + ">");
					}
				};
			}
		}
		items.push_back(std::move(add));

		UI::Widgets::MenuItem duplicate;
		duplicate.label = "Duplicate";
		duplicate.shortcut = EditorInputOps::ChordLabel(EditorInputOps::Shortcut::DuplicateSelection);
		duplicate.enabled = !isRoot;
		duplicate.onSelect = [this, node]() {
			EditDocument("Duplicate Element", EditShape::Structure,
						 [&](UIDocumentAsset& live, str& error) { return UIDocumentOps::Duplicate(live, node, error); });
		};
		items.push_back(std::move(duplicate));

		UI::Widgets::MenuItem remove;
		remove.label = "Delete";
		remove.shortcut = EditorInputOps::ChordLabel(EditorInputOps::Shortcut::DeleteSelection);
		remove.enabled = !isRoot;
		remove.onSelect = [this, node]() {
			EditDocument("Delete Element", EditShape::Structure,
						 [&](UIDocumentAsset& live, str& error) { return UIDocumentOps::Remove(live, node, error); });
		};
		items.push_back(std::move(remove));

		UI::Widgets::MenuItem createTemplate;
		createTemplate.label = "Create Template";
		createTemplate.separatorBefore = true;
		createTemplate.enabled = !isRoot && !_documentPath.empty();
		createTemplate.onSelect = [this, node]() {
			UIDocumentAsset* live = _document.Get();
			const UIDocumentNode* target = live ? UIBuilderOps::NodeAt(*live, node) : nullptr;
			if (!target) {
				return;
			}
			str directory = Path(_documentPath).GetDirectory();
			str base = target->id.empty() ? target->tag : target->id;
			str path = directory + "/" + base + ".slxml";
			for (u32 suffix = 2; FileSystem::Exists(FileSystem::Resolve(Path(path))); ++suffix) {
				path = directory + "/" + base + "-" + std::to_string(suffix) + ".slxml";
			}
			EditDocument("Create Template", EditShape::Structure, [&](UIDocumentAsset& document, str& error) {
				return UIDocumentOps::CreateTemplate(document, node, path, error);
			});
		};
		items.push_back(std::move(createTemplate));

		UI::Widgets::MenuItem unpack;
		unpack.label = "Unpack Instance";
		unpack.enabled = isInstance;
		unpack.onSelect = [this, node]() {
			EditDocument("Unpack Instance", EditShape::Structure,
						 [&](UIDocumentAsset& live, str& error) { return UIDocumentOps::Unpack(live, node, false, error); });
		};
		items.push_back(std::move(unpack));

		UI::Widgets::MenuItem unpackAll;
		unpackAll.label = "Unpack Instance Completely";
		unpackAll.enabled = isInstance;
		unpackAll.onSelect = [this, node]() {
			EditDocument("Unpack Instance Completely", EditShape::Structure,
						 [&](UIDocumentAsset& live, str& error) { return UIDocumentOps::Unpack(live, node, true, error); });
		};
		items.push_back(std::move(unpackAll));

		if (isInstance) {
			UI::Widgets::MenuItem open;
			open.label = "Open in UI Builder";
			open.separatorBefore = true;
			for (const UIDocumentTemplate& declared : document->templates) {
				if (declared.name == resolved->templateName) {
					str src = declared.src;
					open.onSelect = [src]() { UIBuilderOps::OpenDocumentInBuilder(src); };
				}
			}
			open.enabled = static_cast<bool>(open.onSelect);
			items.push_back(std::move(open));
		}

		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items,
							  UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{}, RowFont());
	}

	u64 UIBuilderPanel::RuleRowId(u32 sheet, u32 rule) {
		return (static_cast<u64>(sheet + 1) << 32) | (rule + 1);
	}

	u64 UIBuilderPanel::SheetRowId(u32 sheet) {
		return static_cast<u64>(sheet + 1) << 32;
	}

	void UIBuilderPanel::RebuildSheetRows(UI::Context& context) {
		_sheetRows.clear();
		Vector<ListRow> listRows{GetMemoryResource(MemTag::Editor)};

		UIDocumentAsset* document = _document.Get();
		if (!document) {
			_rulesList.SetRows(listRows);
			return;
		}
		UIBuilderOps::BuildSheetRows(*document, _sheetRows);

		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
		for (u32 i = 0; i < static_cast<u32>(_sheetRows.size()); ++i) {
			const UIBuilderOps::SheetRow& row = _sheetRows[i];
			const bool isSheet = row.rule == UIBuilderOps::INVALID_NODE;

			ListRow desc;
			desc.id = isSheet ? SheetRowId(row.sheet) : RuleRowId(row.sheet, row.rule);
			desc.selectable = !isSheet;
			desc.depth = isSheet ? 0u : 1u;
			desc.label = str{isSheet ? row.path : row.selector};
			desc.onDecorate = [isSheet, font, pseudo = str(row.pseudo)](
								  UI::Context& rowContext, const UI::Widgets::TreeRowParts& parts) {
				EditorIcons::SetRowGutter(rowContext, parts, false, false, TextDim(),
										  isSheet ? EditorIcons::Glyph::AssetGeneric : EditorIcons::Glyph::UIElement,
										  TextDim());

				if (pseudo.empty()) {
					return;
				}
				UI::ElementHandle token = rowContext.CreateElement();
				rowContext.AppendChild(parts.row, token);
				rowContext.AddClass(token, EditorTheme::Class::SectionCard);
				UI::Style tokenStyle;
				tokenStyle.padding = {4.0f, 0.0f, 4.0f, 0.0f};
				tokenStyle.margin = {4.0f, 0.0f, 0.0f, 0.0f};
				rowContext.SetStyle(token, tokenStyle);
				rowContext.SetHoverable(token, false);
				rowContext.SetText(token, font, EditorTheme::GetMetric(MetricToken::FontTiny), Accent(), pseudo);
			};
			listRows.push_back(std::move(desc));
		}
		_rulesList.SetRows(listRows);
		PushRuleSelectionToList();
	}

	void UIBuilderPanel::RebuildTextPanes(UI::Context& context) {
		if (!_textPanesOpen) {
			if (context.IsAlive(_layoutTextParent)) {
				context.ClearChildren(_layoutTextParent);
			}
			if (context.IsAlive(_styleTextParent)) {
				context.ClearChildren(_styleTextParent);
			}
			_codeLines.clear();
			return;
		}
		auto fill = [&](UI::ElementHandle parent, std::string_view source, std::string_view empty,
						UIBuilderOps::CodeLanguage language) {
			if (!context.IsAlive(parent)) {
				return;
			}
			context.ClearChildren(parent);
			Handle<FontAsset> proportional = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
			Handle<FontAsset> font = EditorFonts::GetMonospace();
			if (!font.IsValid()) {
				font = proportional;
			}
			const f32 size = EditorTheme::GetMetric(MetricToken::FontTiny);

			UI::ElementHandle body = context.CreateElement();
			context.AppendChild(parent, body);
			UI::Style bodyStyle;
			bodyStyle.flexDirection = UI::FlexDirection::Column;
			bodyStyle.alignItems = UI::Align::Start;
			bodyStyle.padding = {6.0f, 4.0f, 6.0f, 4.0f};
			context.SetStyle(body, bodyStyle);
			context.SetHoverable(body, false);

			if (source.empty()) {
				context.SetText(body, proportional, size, TextDim(), empty);
				return;
			}

			UIBuilderOps::TokenizeCode(source, language, _codeLines);
			for (const UIBuilderOps::CodeLine& line : _codeLines) {
				UI::ElementHandle row = context.CreateElement();
				context.AppendChild(body, row);
				UI::Style rowStyle;
				rowStyle.flexDirection = UI::FlexDirection::Row;
				rowStyle.alignItems = UI::Align::Start;
				context.SetStyle(row, rowStyle);
				context.SetHoverable(row, false);

				UI::ElementHandle gutter = context.CreateElement();
				context.AppendChild(row, gutter);
				context.SetHoverable(gutter, false);
				str number = std::to_string(line.number);
				while (number.size() < 3) {
					number = " " + number;
				}
				number += "  ";
				context.SetText(gutter, font, size, TextFaint(), number);

				for (const UIBuilderOps::CodeSpan& span : line.spans) {
					UI::ElementHandle run = context.CreateElement();
					context.AppendChild(row, run);
					context.SetHoverable(run, false);
					context.SetText(run, font, size, SyntaxColor(span.kind), ExpandTabs(span.text));
				}
			}
		};

		UIDocumentAsset* document = _document.Get();
		fill(_layoutTextParent, document ? document->Write() : str(), NoDocumentText,
			 UIBuilderOps::CodeLanguage::Markup);

		const StyleSheetAsset* sheet =
			document && !document->sheets.empty() ? document->sheets[_selectedSheet < document->sheets.size()
																		 ? _selectedSheet
																		 : 0]
														.sheet.Get()
												  : nullptr;
		fill(_styleTextParent, sheet ? std::string_view(sheet->source) : std::string_view(),
			 "this document declares no <sheet>", UIBuilderOps::CodeLanguage::Style);
	}

	void UIBuilderPanel::PublishRuleSelectionFromList() {
		std::span<const u64> selected = _rulesList.GetSelected();
		if (selected.empty()) {
			return;
		}
		u64 active = selected.back();
		_subject = UIBuilderOps::Subject::Selector;
		_selectedSheet = static_cast<u32>(active >> 32) - 1;
		_selectedRule = static_cast<u32>(active & 0xFFFFFFFFull) - 1;
		if (UI::Context* context = GetPanelContext()) {
			_selected = UIBuilderOps::INVALID_NODE;
			_selectedNodes.clear();
			PushHierarchySelectionToList();
			RebuildInspector(*context);
			RebuildTextPanes(*context);
		}
	}

	void UIBuilderPanel::PushRuleSelectionToList() {
		Vector<u64> ids{GetMemoryResource(MemTag::Editor)};
		if (_subject == UIBuilderOps::Subject::Selector && _selectedRule != UIBuilderOps::INVALID_NODE) {
			ids.push_back(RuleRowId(_selectedSheet, _selectedRule));
		}
		_rulesList.SetSelection(ids);
	}

	Handle<StyleSheetAsset> UIBuilderPanel::SelectedSheet() const {
		const UIDocumentAsset* document = _document.Get();
		if (!document || _selectedSheet >= document->sheets.size()) {
			return {};
		}
		return document->sheets[_selectedSheet].sheet;
	}

	void UIBuilderPanel::SelectNode(u32 index) {
		_selectedNodes.clear();
		if (index != UIBuilderOps::INVALID_NODE) {
			_selectedNodes.push_back(index);
		}
		_selected = index;
		_subject = index == UIBuilderOps::INVALID_NODE ? UIBuilderOps::Subject::None : UIBuilderOps::Subject::Element;
		_selectedRule = UIBuilderOps::INVALID_NODE;
		if (UI::Context* context = GetPanelContext()) {
			PushRuleSelectionToList();
			RebuildInspector(*context);
			PushHierarchySelectionToList();
			SyncNameChip(*context);
		}
	}

	void UIBuilderPanel::PublishHierarchySelectionFromList() {
		std::span<const u64> selected = _hierarchyList.GetSelected();
		_selectedNodes.clear();
		_selectedNodes.reserve(selected.size());
		for (u64 id : selected) {
			_selectedNodes.push_back(NodeOfRow(id));
		}
		_selected = _selectedNodes.empty() ? UIBuilderOps::INVALID_NODE : _selectedNodes.back();
		_subject = _selectedNodes.empty() ? UIBuilderOps::Subject::None : UIBuilderOps::Subject::Element;
		_selectedRule = UIBuilderOps::INVALID_NODE;
		if (UI::Context* context = GetPanelContext()) {
			PushRuleSelectionToList();
			RebuildInspector(*context);
			SyncNameChip(*context);
		}
	}

	void UIBuilderPanel::PushHierarchySelectionToList() {
		Vector<u64> ids{GetMemoryResource(MemTag::Editor)};
		ids.reserve(_selectedNodes.size());
		for (u32 node : _selectedNodes) {
			ids.push_back(NodeRowId(node));
		}
		_hierarchyList.SetSelection(ids);
	}

	void UIBuilderPanel::DeleteSelectedNodes() {
		Vector<u32> doomed{GetMemoryResource(MemTag::Editor)};
		doomed.assign(_selectedNodes.begin(), _selectedNodes.end());
		if (doomed.empty()) {
			return;
		}
		std::sort(doomed.begin(), doomed.end(), std::greater<u32>());
		str label = doomed.size() > 1 ? std::format("Delete {} Elements", doomed.size()) : str("Delete Element");
		EditDocument(label, EditShape::Structure, [&](UIDocumentAsset& live, str& error) {
			bool any = false;
			for (u32 node : doomed) {
				str ignored;
				any = UIDocumentOps::Remove(live, node, ignored) || any;
			}
			if (!any) {
				error = "a document has 1 root element. This one can't be deleted";
			}
			return any;
		});
	}

	void UIBuilderPanel::DuplicateSelectedNodes() {
		Vector<u32> sources{GetMemoryResource(MemTag::Editor)};
		sources.assign(_selectedNodes.begin(), _selectedNodes.end());
		if (sources.empty()) {
			return;
		}
		std::sort(sources.begin(), sources.end(), std::greater<u32>());
		str label = sources.size() > 1 ? std::format("Duplicate {} Elements", sources.size())
									   : str("Duplicate Element");
		EditDocument(label, EditShape::Structure, [&](UIDocumentAsset& live, str& error) {
			bool any = false;
			for (u32 node : sources) {
				str ignored;
				any = UIDocumentOps::Duplicate(live, node, ignored) || any;
			}
			if (!any) {
				error = "a document has 1 root element. This one can't be duplicated";
			}
			return any;
		});
	}

	void UIBuilderPanel::PickAt(UI::Context& context, Vec2 contextPx) {
		UI::Context* preview = UISystem::GetSurfaceContext(_surface);
		UIDocumentAsset* document = _document.Get();
		if (!preview || !document || !context.IsAlive(_viewport)) {
			return;
		}
		const UI::Rect area = UI::SnapRectToPixels(CanvasRect(context.GetLayout(_viewport).content));
		if (area.w <= 0.0f || area.h <= 0.0f) {
			return;
		}
		Vec2 local{contextPx.x - area.x, contextPx.y - area.y};
		if (local.x < 0.0f || local.y < 0.0f || local.x >= area.w || local.y >= area.h) {
			return;
		}
		UI::ElementHandle hit = preview->HitTest(local);
		SelectNode(UIBuilderOps::NodeForElement(*preview, *document, _mapping, hit));
	}

	void UIBuilderPanel::RoutePointerToPreview(UI::Context& context, InputEventType type, Vec2 wheel) {
		UI::Context* preview = UISystem::GetSurfaceContext(_surface);
		if (!_preview || !preview || !context.IsAlive(_viewport)) {
			return;
		}
		const UI::Rect area = UI::SnapRectToPixels(CanvasRect(context.GetLayout(_viewport).content));
		if (area.w <= 0.0f || area.h <= 0.0f) {
			return;
		}
		const Vec2 pointer = context.GetPointerPosition();
		const Vec2 local{pointer.x - area.x, pointer.y - area.y};
		const bool inside = local.x >= 0.0f && local.y >= 0.0f && local.x < area.w && local.y < area.h;

		if (!inside) {
			if (_previewPointerInside) {
				InputEvent leave{};
				leave.type = InputEventType::MouseLeave;
				preview->InjectInput(leave);
				preview->Update();
				_previewPointerInside = false;
			}
			return;
		}
		_previewPointerInside = true;

		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mods = context.GetPointerMods();
		event.mousePosPx = {static_cast<i32>(local.x), static_cast<i32>(local.y)};
		event.wheel = wheel;
		preview->InjectInput(event);
		preview->Update();
	}

	void UIBuilderPanel::SyncNameChip(UI::Context& context) {
		if (!context.IsAlive(_nameChip)) {
			return;
		}
		UI::Context* preview = UISystem::GetSurfaceContext(_surface);
		UIDocumentAsset* document = _document.Get();
		const UIDocumentNode* node = document ? UIBuilderOps::NodeAt(*document, _selected) : nullptr;
		if (_preview || !preview || !node || !context.IsAlive(_viewport)) {
			context.SetVisible(_nameChip, false);
			return;
		}
		UI::ElementHandle element = UIBuilderOps::ElementForNode(_mapping, node);
		if (!preview->IsAlive(element)) {
			context.SetVisible(_nameChip, false);
			return;
		}

		const UI::Rect viewport = context.GetLayout(_viewport).content;
		const UI::Rect area = UI::SnapRectToPixels(CanvasRect(viewport));
		const UI::Rect box = preview->GetLayout(element).borderBox;
		const UI::Rect host = context.GetLayout(context.MakeHandle(context.GetElement(_viewport)->parent)).content;

		const f32 scale = std::max(0.001f, context.GetContentScale());
		const f32 left = (area.x + box.x - host.x) / scale;
		const f32 top = (area.y + box.y - host.y) / scale;

		UI::Style style = context.GetStyle(_nameChip);
		style.left = UI::Length::Pixel(left);
		const f32 above = top - 18.0f;
		style.top = UI::Length::Pixel(above >= 0.0f ? above : top + 2.0f);
		context.SetStyle(_nameChip, style);
		context.SetVisible(_nameChip, true);

		const UI::Element* chip = context.GetElement(_nameChip);
		if (chip && chip->firstChild != UI::INVALID_UI_INDEX) {
			context.SetTextString(context.MakeHandle(chip->firstChild), UIBuilderOps::RowLabel(*node));
		}
	}

	f32 UIBuilderPanel::EffectiveZoom(const UI::Rect& viewport) const {
		if (_fitCanvas) {
			return UIBuilderOps::ZoomToFit(viewport, _canvasWidth, _canvasHeight);
		}
		return std::clamp(_zoom, UIBuilderOps::MIN_CANVAS_ZOOM, UIBuilderOps::MAX_CANVAS_ZOOM);
	}

	UIBuilderOps::CanvasView UIBuilderPanel::CurrentView(const UI::Rect& viewport) const {
		return {EffectiveZoom(viewport), _fitCanvas ? Vec2{} : _pan};
	}

	UI::Rect UIBuilderPanel::CanvasRect(const UI::Rect& viewport) const {
		return UIBuilderOps::CanvasRectFor(viewport, _canvasWidth, _canvasHeight, CurrentView(viewport));
	}

	void UIBuilderPanel::LeaveFit(const UI::Rect& viewport) {
		if (!_fitCanvas) {
			return;
		}
		_zoom = UIBuilderOps::ZoomToFit(viewport, _canvasWidth, _canvasHeight);
		_pan = {};
		_fitCanvas = false;
	}

	void UIBuilderPanel::ApplyView(const UIBuilderOps::CanvasView& view) {
		_zoom = view.zoom;
		_pan = view.pan;
		if (UI::Context* context = GetPanelContext()) {
			SyncSurfaceSize(*context);
			RebuildInspector(*context);
		}
	}

	void UIBuilderPanel::SyncSurfaceSize(UI::Context& context) {
		if (!context.IsAlive(_viewport)) {
			return;
		}
		const UI::Rect content = context.GetLayout(_viewport).content;
		if (!EditorViewportOps::ViewportIsOnScreen(content)) {
			return;
		}
		const f32 zoom = EffectiveZoom(content);
		const UI::Rect canvas = CanvasRect(content);
		Vec2I wanted = EditorViewportOps::ViewTargetSizeFor(canvas);
		if (wanted.x < MIN_SURFACE_PX || wanted.y < MIN_SURFACE_PX) {
			return;
		}
		const f32 surfaceScale = zoom;

		if (_surface == INVALID_UI_SURFACE) {
			_surface = UISystem::CreateSurface(wanted, surfaceScale);
			_surfaceSize = wanted;
			_surfaceScale = surfaceScale;
			RepopulatePreview();
			return;
		}
		if (wanted != _surfaceSize || surfaceScale != _surfaceScale) {
			UISystem::ResizeSurface(_surface, wanted, surfaceScale);
			_surfaceSize = wanted;
			_surfaceScale = surfaceScale;
		}
	}

	void UIBuilderPanel::ReleaseSurface() {
		if (_surface != INVALID_UI_SURFACE) {
			UISystem::DestroySurface(_surface);
			_surface = INVALID_UI_SURFACE;
		}
		_surfaceSize = {};
		_mapping.clear();
		_populated = nullptr;
	}

	void UIBuilderPanel::OnTick(f32) {
		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		_gaps.SyncHits(*context);
		if (!_documentPath.empty() && !_document.IsValid()) {
			str wanted = _documentPath;
			OpenDocument(wanted);
			if (!_document.IsValid()) {
				_documentPath.clear();
			}
		}

		SyncSurfaceSize(*context);

		if (_populated != _document.Get()) {
			RepopulatePreview();
			RebuildPanes(*context);
		}

		const u32 undoDepth = UndoSystem::GetUndoDepth();
		const u32 redoDepth = UndoSystem::GetRedoDepth();
		if (undoDepth != _undoDepth || redoDepth != _redoDepth) {
			_undoDepth = undoDepth;
			_redoDepth = redoDepth;
			if (UI::Context* preview = UISystem::GetSurfaceContext(_surface)) {
				preview->ClearStyleSheets();
			}
			_collapsed.clear();
			RepopulatePreview();
			RebuildPanes(*context);
		}

		if (const UIDocumentAsset* document = _document.Get();
			document && _populated == document && document->GetContentVersion() != _populatedContent) {
			if (UI::Context* preview = UISystem::GetSurfaceContext(_surface)) {
				preview->ClearStyleSheets();
			}
			_collapsed.clear();
			SelectNode(UIBuilderOps::INVALID_NODE);
			RepopulatePreview();
			RebuildPanes(*context);
		}

		if (_preview) {
			RoutePointerToPreview(*context, InputEventType::MouseMove);
		}
		SyncNameChip(*context);
	}

	void UIBuilderPanel::OnClose() {
		ReleaseSurface();
		if (_document.IsValid()) {
			AssetDatabase::UnloadAsset(_document.Get());
			_document = {};
		}
		_rows.clear();
		_sheetRows.clear();
		_collapsed.clear();
	}

	void UIBuilderPanel::SetStatus(UI::Context& context, std::string_view text) {
		if (context.IsAlive(_statusLabel)) {
			context.SetTextString(_statusLabel, text);
		}
	}

	EditorIcons::Icon UIBuilderPanel::GetTabIcon() const {
		return EditorIcons::Glyph::UIElement;
	}

} // namespace Shalltear
