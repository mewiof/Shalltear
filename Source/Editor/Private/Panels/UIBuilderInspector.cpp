#include "UIBuilderPanel.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/UIDocumentOps.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "UIBuilderChrome.h"
#include "UIBuilderCommands.h"

namespace Shalltear {

	namespace {

		using namespace UIBuilderChrome;

		constexpr std::string_view NoDocumentText = "no document open";

		Handle<FontAsset> PanelFont() {
			return UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
		}
	} // anonymous namespace

	void UIBuilderPanel::RebuildInspector(UI::Context& context) {
		if (!context.IsAlive(_inspectorParent)) {
			return;
		}
		context.ClearChildren(_inspectorParent);

		UIDocumentAsset* document = _document.Get();

		if (_subject == UIBuilderOps::Subject::Selector && SelectedSheet().IsValid()) {
			BuildSelectorInspector(context, _inspectorParent);
			return;
		}
		if (_subject == UIBuilderOps::Subject::Element && document) {
			if (const UIDocumentNode* node = UIBuilderOps::NodeAt(*document, _selected)) {
				BuildElementInspector(context, _inspectorParent, *node);
				return;
			}
		}
		if (document) {
			BuildCanvasInspector(context, _inspectorParent);
			return;
		}
		UI::ElementHandle empty = context.CreateElement();
		context.AppendChild(_inspectorParent, empty);
		UI::Style style;
		style.padding = {8.0f, 8.0f, 8.0f, 8.0f};
		context.SetStyle(empty, style);
		context.SetText(empty, PanelFont(), RowFont(), TextDim(), NoDocumentText);
	}

	void UIBuilderPanel::BuildElementInspector(UI::Context& context, UI::ElementHandle parent,
											   const UIDocumentNode& node) {
		Handle<FontAsset> font = PanelFont();
		const u32 index = _selected;

		SectionTitle(context, parent, UIBuilderOps::RowLabel(node), font);

		SectionTitle(context, parent, "Attributes", font);
		LabeledRow(context, parent, "tag", node.tag, font);
		FieldRow(context, parent, "id", node.id, font, [this, index](std::string_view value) {
			EditDocument("Set Id", EditShape::Fields, [&](UIDocumentAsset& document, str& error) {
				return UIDocumentOps::SetId(document, index, value, error);
			});
		});
		if (node.children.empty() && node.templateName.empty() && !UI::WidgetRegistry::Find(node.tag)) {
			FieldRow(context, parent, "text", node.text, font, [this, index](std::string_view value) {
				EditDocument("Set Text", EditShape::Fields, [&](UIDocumentAsset& document, str& error) {
					return UIDocumentOps::SetText(document, index, value, error);
				});
			});
		}
		for (const UIDocumentAttribute& attribute : node.attributes) {
			FieldRow(context, parent, attribute.name, attribute.value, font,
					 [this, index, name = attribute.name](std::string_view value) {
						 EditDocument(str("Set ") + name, EditShape::Fields, [&](UIDocumentAsset& document, str& error) {
							 return UIDocumentOps::SetAttribute(document, index, name, value, error);
						 });
					 });
		}

		str classes;
		for (u8 i = 0; i < node.classCount; ++i) {
			classes += (i == 0 ? "." : " .") + node.classes[i];
		}
		SectionTitle(context, parent,
					 str("Style Class List  (") + std::to_string(node.classCount) + " / " +
						 std::to_string(UI::MAX_ELEMENT_CLASSES) + ")",
					 font);
		FieldRow(context, parent, "class", classes, font, [this, index](std::string_view value) {
			Vector<str> parsed{GetMemoryResource(MemTag::Editor)};
			str current;
			for (char c : value) {
				if (c == ' ' || c == '\t' || c == '.') {
					if (!current.empty()) {
						parsed.push_back(current);
						current.clear();
					}
					continue;
				}
				current += c;
			}
			if (!current.empty()) {
				parsed.push_back(current);
			}
			Vector<std::string_view> views{GetMemoryResource(MemTag::Editor)};
			for (const str& name : parsed) {
				views.push_back(name);
			}
			EditDocument("Set Classes", EditShape::Fields, [&](UIDocumentAsset& document, str& error) {
				return UIDocumentOps::SetClasses(document, index, views, error);
			});
		});

		Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Editor)};
		UI::StyleSheet::SplitDeclarations(node.style, declarations);

		if (!declarations.empty() && !_document.Get()->sheets.empty() && node.classCount < UI::MAX_ELEMENT_CLASSES) {
			UI::ElementHandle extract = context.CreateElement();
			context.AppendChild(parent, extract);
			context.AddClass(extract, EditorTheme::Class::SectionCard);
			context.AddClass(extract, EditorTheme::Class::Clickable);
			UI::Style style;
			style.padding = {8.0f, 3.0f, 8.0f, 3.0f};
			style.margin = {8.0f, 2.0f, 8.0f, 2.0f};
			context.SetStyle(extract, style);
			context.SetText(extract, font, RowFont(), Text(), "Extract Inlined Styles to New Class");
			context.SetTooltip(extract, "moves these declarations into a new rule in the first sheet + adds its class here");
			context.Events(extract).onClick = [this]() { ExtractInlinedStyles(); };
		}
		BuildPropertySection(context, parent, "Inlined Styles", node.style, node.style, declarations);

		UIDocumentAsset* document = _document.Get();
		Vector<UIBuilderOps::MatchingSelector> matching{GetMemoryResource(MemTag::Editor)};
		if (document) {
			UIBuilderOps::BuildMatchingSelectors(*document, node, matching);
		}
		SectionTitle(context, parent, "Matching Selectors", font);
		if (matching.empty()) {
			LabeledRow(context, parent, "", "no rule reaches this element", font);
		}
		for (const UIBuilderOps::MatchingSelector& match : matching) {
			UI::ElementHandle row = LabeledRow(context, parent, match.selector, match.sheet, font);
			context.SetTooltip(row, str("specificity ") + std::to_string(match.specificity) +
										". Rules apply in this order, last one wins");
		}
	}

	void UIBuilderPanel::BuildSelectorInspector(UI::Context& context, UI::ElementHandle parent) {
		Handle<FontAsset> font = PanelFont();
		StyleSheetAsset* sheet = SelectedSheet().Get();
		if (!sheet) {
			return;
		}
		Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Editor)};
		UI::StyleSheet::SplitSource(sheet->source, rules);
		if (_selectedRule >= rules.size()) {
			SectionTitle(context, parent, "selector no longer in this sheet", font);
			return;
		}
		SectionTitle(context, parent, rules[_selectedRule].selector, font);
		const UIDocumentAsset* document = _document.Get();
		LabeledRow(context, parent, "sheet",
				   document && _selectedSheet < document->sheets.size() ? document->sheets[_selectedSheet].src : "", font);
		BuildPropertySection(context, parent, "Style Properties", sheet->source, rules[_selectedRule].body,
							 rules[_selectedRule].declarations);
	}

	void UIBuilderPanel::BuildCanvasInspector(UI::Context& context, UI::ElementHandle parent) {
		Handle<FontAsset> font = PanelFont();

		SectionTitle(context, parent, "Canvas", font);
		LabeledRow(context, parent, "document", _documentPath, font);
		LabeledRow(context, parent, "size", std::to_string(_canvasWidth) + " x " + std::to_string(_canvasHeight) + " px",
				   font);
		f32 zoom = _zoom;
		if (UI::Context* c = GetPanelContext(); c && c->IsAlive(_viewport)) {
			zoom = EffectiveZoom(c->GetLayout(_viewport).content);
		}
		LabeledRow(context, parent, "zoom",
				   std::to_string(static_cast<i32>(zoom * 100.0f + 0.5f)) + "%" + (_fitCanvas ? "  (Fit)" : ""), font);
		LabeledRow(context, parent, "mode", _preview ? "Preview (runtime input)" : "Authoring", font);

		SectionTitle(context, parent, "Style Sheets", font);
		const UIDocumentAsset* document = _document.Get();
		if (document && document->sheets.empty()) {
			LabeledRow(context, parent, "", "this document declares none", font);
		}
		for (const UIDocumentSheet& sheet : document ? document->sheets : Vector<UIDocumentSheet>{}) {
			LabeledRow(context, parent, sheet.src, sheet.sheet.IsValid() ? "loaded" : "did not load", font);
		}
	}

	void UIBuilderPanel::BuildPropertySection(UI::Context& context, UI::ElementHandle parent, std::string_view title,
											  std::string_view source, std::string_view region,
											  std::span<const UI::StyleDeclarationSource> declarations) {
		Handle<FontAsset> font = PanelFont();

		UI::ElementHandle header = SectionTitle(context, parent, title, font);
		Vector<std::string_view> all{GetMemoryResource(MemTag::Editor)};
		for (const UI::StyleDeclarationSource& declaration : declarations) {
			all.push_back(declaration.text);
		}
		context.SetTooltip(header, "right-click for Unset all");
		context.Events(header).onRightClick = [this, all = Vector<std::string_view>(all)]() mutable {
			UI::Context* c = GetPanelContext();
			if (!c || all.empty()) {
				return;
			}
			Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
			UI::Widgets::MenuItem unsetAll;
			unsetAll.label = "Unset all";
			unsetAll.onSelect = [this, all]() {
				UnsetOnSelection(all, "Unset all");
			};
			items.push_back(std::move(unsetAll));
			UI::Widgets::OpenMenu(*c, c->GetPointerPosition(), items, PanelFont(), RowFont());
		};

		Vector<UIBuilderOps::PropertyRow> rows{GetMemoryResource(MemTag::Editor)};
		UIBuilderOps::BuildPropertyRows(declarations, rows);
		for (const UIBuilderOps::PropertyRow& row : rows) {
			UI::ElementHandle element =
				FieldRow(context, parent, row.name, row.value, font,
						 [this, property = str(row.name)](std::string_view value) { WriteDeclaration(property, value); });
			if (row.overridden) {
				MarkRowOverridden(context, element, font, row.name);
			}
			context.SetTooltip(element, str("style property: ") + str(row.name) +
											(row.overridden ? "  (set here, right-click to Unset)" : "  (not set)"));
			if (!row.overridden) {
				continue;
			}
			std::string_view run;
			for (const UI::StyleDeclarationSource& declaration : declarations) {
				if (declaration.name == row.name) {
					run = declaration.text;
				}
			}
			context.Events(element).onRightClick = [this, property = row.name, run]() {
				UI::Context* c = GetPanelContext();
				if (!c) {
					return;
				}
				Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
				UI::Widgets::MenuItem unset;
				unset.label = str("Unset ") + str(property);
				unset.onSelect = [this, property, run]() {
					std::string_view runs[] = {run};
					UnsetOnSelection(runs, str("Unset ") + str(property));
				};
				items.push_back(std::move(unset));
				UI::Widgets::OpenMenu(*c, c->GetPointerPosition(), items, PanelFont(), RowFont());
			};
		}
		(void)source;
		(void)region;
	}

	void UIBuilderPanel::WriteDeclaration(std::string_view property, std::string_view value) {
		UIDocumentAsset* document = _document.Get();
		if (!document) {
			return;
		}
		if (_subject == UIBuilderOps::Subject::Selector) {
			StyleSheetAsset* sheet = SelectedSheet().Get();
			if (!sheet) {
				return;
			}
			Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Editor)};
			UI::StyleSheet::SplitSource(sheet->source, rules);
			if (_selectedRule >= rules.size()) {
				return;
			}
			if (value.empty()) {
				for (const UI::StyleDeclarationSource& declaration : rules[_selectedRule].declarations) {
					if (declaration.name == property) {
						std::string_view runs[] = {declaration.text};
						UnsetOnSelection(runs, str("Unset ") + str(property));
						return;
					}
				}
				return;
			}
			EditSheet(SelectedSheet(), str("Set ") + str(property),
					  UIBuilderOps::SetDeclaration(sheet->source, rules[_selectedRule].body,
												   rules[_selectedRule].declarations, property, value,
												   UIBuilderOps::DeclarationLayout::Block));
			return;
		}

		const UIDocumentNode* node = UIBuilderOps::NodeAt(*document, _selected);
		if (!node) {
			return;
		}
		Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Editor)};
		UI::StyleSheet::SplitDeclarations(node->style, declarations);
		if (value.empty()) {
			for (const UI::StyleDeclarationSource& declaration : declarations) {
				if (declaration.name == property) {
					std::string_view runs[] = {declaration.text};
					UnsetOnSelection(runs, str("Unset ") + str(property));
					return;
				}
			}
			return;
		}
		str written = UIBuilderOps::SetDeclaration(node->style, node->style, declarations, property, value,
												   UIBuilderOps::DeclarationLayout::Inline);
		const u32 index = _selected;
		EditDocument(str("Set ") + str(property), EditShape::Fields, [&](UIDocumentAsset& live, str& error) {
			return UIDocumentOps::SetStyle(live, index, written, error);
		});
	}

	void UIBuilderPanel::UnsetOnSelection(std::span<const std::string_view> runs, std::string_view label) {
		UI::Context* context = GetPanelContext();
		if (runs.empty() || !context) {
			return;
		}
		if (_subject == UIBuilderOps::Subject::Selector) {
			StyleSheetAsset* sheet = SelectedSheet().Get();
			if (!sheet) {
				return;
			}
			EditSheet(SelectedSheet(), label, UIBuilderOps::EraseRuns(sheet->source, runs));
			return;
		}
		if (_subject != UIBuilderOps::Subject::Element) {
			return;
		}
		UIDocumentAsset* document = _document.Get();
		const UIDocumentNode* node = document ? UIBuilderOps::NodeAt(*document, _selected) : nullptr;
		if (!node) {
			return;
		}
		str written = UIBuilderOps::EraseRuns(node->style, runs);
		const u32 index = _selected;
		EditDocument(label, EditShape::Fields, [&](UIDocumentAsset& live, str& error) {
			return UIDocumentOps::SetStyle(live, index, written, error);
		});
	}

	void UIBuilderPanel::ExtractInlinedStyles() {
		UIDocumentAsset* document = _document.Get();
		const UIDocumentNode* node = document ? UIBuilderOps::NodeAt(*document, _selected) : nullptr;
		if (!node || document->sheets.empty() || node->style.empty()) {
			return;
		}
		Handle<StyleSheetAsset> sheetHandle = document->sheets[0].sheet;
		StyleSheetAsset* sheet = sheetHandle.Get();
		if (!sheet || node->classCount >= UI::MAX_ELEMENT_CLASSES) {
			return;
		}
		Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Editor)};
		UI::StyleSheet::SplitDeclarations(node->style, declarations);
		if (declarations.empty()) {
			return;
		}

		str base = node->id.empty() ? node->tag : node->id;
		Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Editor)};
		UI::StyleSheet::SplitSource(sheet->source, rules);
		auto taken = [&rules](std::string_view selector) {
			for (const UI::StyleRuleSource& rule : rules) {
				if (rule.selector == selector) {
					return true;
				}
			}
			return false;
		};
		str name = base;
		for (u32 suffix = 2; taken(str(".") + name); ++suffix) {
			name = base + "-" + std::to_string(suffix);
		}

		Vector<str> owned{GetMemoryResource(MemTag::Editor)};
		for (const UI::StyleDeclarationSource& declaration : declarations) {
			owned.push_back(str(declaration.text));
		}
		Vector<std::string_view> runs{GetMemoryResource(MemTag::Editor)};
		for (const str& run : owned) {
			runs.push_back(run);
		}
		Vector<str> wanted{GetMemoryResource(MemTag::Editor)};
		for (u8 i = 0; i < node->classCount; ++i) {
			wanted.push_back(node->classes[i]);
		}
		wanted.push_back(name);
		Vector<std::string_view> views{GetMemoryResource(MemTag::Editor)};
		for (const str& entry : wanted) {
			views.push_back(entry);
		}

		UndoSystem::BeginGroup("Extract Inlined Styles");
		const u32 index = _selected;
		const bool moved = EditDocument("Extract Inlined Styles", EditShape::Fields,
										[&](UIDocumentAsset& live, str& error) {
											return UIDocumentOps::SetClasses(live, index, views, error) &&
												   UIDocumentOps::SetStyle(live, index, "", error);
										});
		if (moved) {
			EditSheet(sheetHandle, "Extract Inlined Styles",
					  UIBuilderOps::AppendRule(sheet->source, str(".") + name, runs));
		}
		UndoSystem::EndGroup();
	}

} // namespace Shalltear
