#include "Shalltear/Editor/UIBuilderOps.h"

#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"

#include "Shalltear/Editor/PanelManager.h"

#include "Panels/UIBuilderPanel.h"
#include "StyleSelectorMatch.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace Shalltear::UIBuilderOps {

	namespace {

		template <typename Fn>
		bool WalkNodes(const UIDocumentNode& node, u32 depth, u32& index, const Fn& visit) {
			if (!visit(node, depth, index)) {
				return false;
			}
			++index;
			if (!node.templateName.empty()) {
				return true;
			}
			for (const UIDocumentNode& child : node.children) {
				if (!WalkNodes(child, depth + 1, index, visit)) {
					return false;
				}
			}
			return true;
		}

		bool HasRoot(const UIDocumentAsset& document) {
			return !document.root.tag.empty() || !document.root.templateName.empty();
		}

		u32 IndexOfNode(const UIDocumentAsset& document, const UIDocumentNode* node) {
			if (!node || !HasRoot(document)) {
				return INVALID_NODE;
			}
			u32 found = INVALID_NODE;
			u32 walked = 0;
			WalkNodes(document.root, 0, walked, [&found, node](const UIDocumentNode& candidate, u32, u32 nodeIndex) {
				if (&candidate == node) {
					found = nodeIndex;
					return false;
				}
				return true;
			});
			return found;
		}

	} // anonymous namespace

	bool OpenDocumentInBuilder(std::string_view virtualPath) {
		if (!PanelManager::FocusPanelOfType(StringHash("UIBuilderPanel"))) {
			return false;
		}
		Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
		PanelManager::GetOpenPanels(panels);
		for (const Handle<EditorPanel>& panel : panels) {
			if (UIBuilderPanel* builder = dynamic_cast<UIBuilderPanel*>(panel.Get())) {
				return builder->OpenDocument(virtualPath);
			}
		}
		return false;
	}

	bool ShowSheetRuleInBuilder(std::string_view sheetVirtualPath, u32 rule, std::string_view selector) {
		Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
		PanelManager::GetOpenPanels(panels);
		for (const Handle<EditorPanel>& panel : panels) {
			UIBuilderPanel* builder = dynamic_cast<UIBuilderPanel*>(panel.Get());
			if (builder && builder->ShowSheetRule(sheetVirtualPath, rule, selector)) {
				PanelManager::FocusPanel(panel);
				return true;
			}
		}
		return false;
	}

	void BuildRows(const UIDocumentAsset& document, Vector<Row>& out) {
		out.clear();
		if (!HasRoot(document)) {
			return;
		}
		u32 index = 0;
		WalkNodes(document.root, 0, index, [&out](const UIDocumentNode& node, u32 depth, u32 nodeIndex) {
			out.push_back(Row{nodeIndex, depth, &node});
			return true;
		});
	}

	const UIDocumentNode* NodeAt(const UIDocumentAsset& document, u32 index) {
		if (index == INVALID_NODE || !HasRoot(document)) {
			return nullptr;
		}
		const UIDocumentNode* found = nullptr;
		u32 walked = 0;
		WalkNodes(document.root, 0, walked, [&found, index](const UIDocumentNode& node, u32, u32 nodeIndex) {
			if (nodeIndex == index) {
				found = &node;
				return false;
			}
			return true;
		});
		return found;
	}

	str RowLabel(const UIDocumentNode& node) {
		if (!node.templateName.empty()) {
			return node.id.empty() ? str("<") + str(node.templateName) + ">"
								   : str("#") + str(node.id) + " <" + str(node.templateName) + ">";
		}
		if (!node.id.empty()) {
			return str("#") + str(node.id);
		}
		return str(node.tag);
	}

	UI::ElementHandle ElementForNode(std::span<const UIInstantiateMapping> mapping, const UIDocumentNode* node) {
		if (!node) {
			return {};
		}
		for (const UIInstantiateMapping& entry : mapping) {
			if (entry.node == node) {
				return entry.element;
			}
		}
		return {};
	}

	u32 NodeForElement(const UI::Context& context, const UIDocumentAsset& document,
					   std::span<const UIInstantiateMapping> mapping, UI::ElementHandle element) {
		for (UI::ElementHandle walk = element; context.IsAlive(walk);) {
			for (const UIInstantiateMapping& entry : mapping) {
				if (entry.element != walk || entry.document != &document) {
					continue;
				}
				return IndexOfNode(document, entry.node);
			}
			const UI::Element* record = context.GetElement(walk);
			if (!record || record->parent == UI::INVALID_UI_INDEX) {
				break;
			}
			walk = context.MakeHandle(record->parent);
		}
		return INVALID_NODE;
	}

	void BuildLibrary(Vector<LibraryGroup>& out) {
		out.clear();
		Vector<const UI::WidgetDesc*> widgets{GetMemoryResource(MemTag::Editor)};
		UI::WidgetRegistry::GetAll(widgets);
		for (const UI::WidgetDesc* widget : widgets) {
			LibraryGroup* group = nullptr;
			for (LibraryGroup& candidate : out) {
				if (candidate.category == widget->category) {
					group = &candidate;
					break;
				}
			}
			if (!group) {
				out.push_back(LibraryGroup{widget->category, Vector<const UI::WidgetDesc*>{GetMemoryResource(MemTag::Editor)}});
				group = &out.back();
			}
			group->widgets.push_back(widget);
		}
	}

	void BuildSheetRows(const UIDocumentAsset& document, Vector<SheetRow>& out) {
		out.clear();
		Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Editor)};
		for (u32 index = 0; index < static_cast<u32>(document.sheets.size()); ++index) {
			const UIDocumentSheet& sheet = document.sheets[index];
			out.push_back(SheetRow{index, INVALID_NODE, sheet.src, {}, {}});

			const StyleSheetAsset* asset = sheet.sheet.Get();
			if (!asset) {
				continue;
			}
			UI::StyleSheet::SplitSource(asset->source, rules);
			for (u32 rule = 0; rule < static_cast<u32>(rules.size()); ++rule) {
				std::string_view selector = rules[rule].selector;
				std::string_view pseudo;
				if (size_t colon = selector.find(':'); colon != std::string_view::npos) {
					pseudo = selector.substr(colon);
					selector = selector.substr(0, colon);
				}
				out.push_back(SheetRow{index, rule, sheet.src, selector, pseudo});
			}
		}
	}

	void BuildPropertyRows(std::span<const UI::StyleDeclarationSource> declarations, Vector<PropertyRow>& out) {
		out.clear();
		for (std::string_view name : UI::GetStylePropertyNames()) {
			PropertyRow row{name, {}, false};
			for (const UI::StyleDeclarationSource& declaration : declarations) {
				if (declaration.name == name) {
					row.value = declaration.value;
					row.overridden = true;
				}
			}
			out.push_back(row);
		}
	}

	void BuildMatchingSelectors(const UIDocumentAsset& document, const UIDocumentNode& node,
								Vector<MatchingSelector>& out) {
		out.clear();
		Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Editor)};
		for (const UIDocumentSheet& sheet : document.sheets) {
			const StyleSheetAsset* asset = sheet.sheet.Get();
			if (!asset) {
				continue;
			}
			UI::StyleSheet::SplitSource(asset->source, rules);
			StringHash classes[UI::MAX_ELEMENT_CLASSES];
			for (u8 i = 0; i < node.classCount; ++i) {
				classes[i] = StringHash(node.classes[i]);
			}
			for (const UI::StyleRuleSource& rule : rules) {
				EditorInternal::ParsedSelector parsed = EditorInternal::ParseSelectorText(rule.selector);
				if (!EditorInternal::SelectorMatchesIdentity(parsed, StringHash(node.tag), StringHash(node.id),
															 std::span<const StringHash>(classes, node.classCount))) {
					continue;
				}
				out.push_back(MatchingSelector{sheet.src, rule.selector, parsed.specificity});
			}
		}
		std::stable_sort(out.begin(), out.end(), [](const MatchingSelector& a, const MatchingSelector& b) {
			return a.specificity < b.specificity;
		});
	}

	str EraseRuns(std::string_view source, std::span<const std::string_view> runs) {
		Vector<size_t> starts{GetMemoryResource(MemTag::Editor)};
		for (std::string_view run : runs) {
			if (run.empty() || run.data() < source.data() || run.data() + run.size() > source.data() + source.size()) {
				continue;
			}
			starts.push_back(static_cast<size_t>(run.data() - source.data()));
		}
		std::sort(starts.begin(), starts.end(), std::greater<size_t>());

		str result{source};
		size_t previous = str::npos;
		for (size_t start : starts) {
			if (start == previous) {
				continue;
			}
			previous = start;
			size_t length = 0;
			for (std::string_view run : runs) {
				if (!run.empty() && static_cast<size_t>(run.data() - source.data()) == start) {
					length = run.size();
					break;
				}
			}
			size_t end = start + length;
			while (end < result.size() && (result[end] == ' ' || result[end] == '\t')) {
				++end;
			}
			size_t lineStart = start;
			while (lineStart > 0 && (result[lineStart - 1] == ' ' || result[lineStart - 1] == '\t')) {
				--lineStart;
			}
			const bool startsTheLine = lineStart == 0 || result[lineStart - 1] == '\n';
			const bool endsTheLine = end >= result.size() || result[end] == '\n' || result[end] == '\r';
			if (startsTheLine && endsTheLine) {
				start = lineStart;
				if (end < result.size() && result[end] == '\r') {
					++end;
				}
				if (end < result.size() && result[end] == '\n') {
					++end;
				}
			}
			result.erase(start, end - start);
		}
		return result;
	}

	str SetDeclaration(std::string_view source, std::string_view region,
					   std::span<const UI::StyleDeclarationSource> declarations, std::string_view property,
					   std::string_view value, DeclarationLayout layout) {
		auto inside = [source](std::string_view span) {
			return !span.empty() && span.data() >= source.data() &&
				   span.data() + span.size() <= source.data() + source.size();
		};

		const UI::StyleDeclarationSource* existing = nullptr;
		for (const UI::StyleDeclarationSource& declaration : declarations) {
			if (declaration.name == property && inside(declaration.text)) {
				existing = &declaration;
			}
		}

		str result{source};
		if (existing) {
			const size_t start = static_cast<size_t>(existing->text.data() - source.data());
			const bool terminated = !existing->text.empty() && existing->text.back() == ';';
			str written = str(property) + ": " + str(value) + (terminated ? ";" : "");
			result.replace(start, existing->text.size(), written);
			return result;
		}

		size_t at = result.size();
		if (region.data() >= source.data() && region.data() + region.size() <= source.data() + source.size()) {
			at = static_cast<size_t>(region.data() + region.size() - source.data());
		}
		str separator;
		for (size_t i = at; i-- > 0;) {
			const char c = result[i];
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
				continue;
			}
			if (c != ';' && c != '{' && c != '}') {
				separator = ";";
			}
			break;
		}
		const str declaration = str(property) + ": " + str(value) + ";";

		if (layout == DeclarationLayout::Inline) {
			str lead;
			if (!separator.empty() || (at > 0 && result[at - 1] != ' ' && result[at - 1] != '\t')) {
				lead = " ";
			}
			result.insert(at, separator + lead + declaration);
			return result;
		}

		size_t lineStart = result.rfind('\n', at == 0 ? 0 : at - 1);
		lineStart = lineStart == str::npos ? 0 : lineStart + 1;
		str indent;
		for (size_t i = lineStart; i < at && (result[i] == ' ' || result[i] == '\t'); ++i) {
			indent += result[i];
		}
		const bool onItsOwnLine = at > 0 && result.find_first_not_of(" \t", lineStart) >= at;
		result.insert(at, separator + (onItsOwnLine ? declaration + "\n" + indent : "\n" + indent + declaration));
		return result;
	}

	str AppendRule(std::string_view source, std::string_view selector, std::span<const std::string_view> runs) {
		str result{source};
		if (!result.empty() && result.back() != '\n') {
			result += "\n";
		}
		result += "\n";
		result += str(selector);
		result += " {\n";
		for (std::string_view run : runs) {
			str line{run};
			while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
				line.pop_back();
			}
			if (!line.empty() && line.back() != ';') {
				line += ";";
			}
			result += "\t";
			result += line;
			result += "\n";
		}
		result += "}\n";
		return result;
	}

	namespace {

		struct LineBuilder {
			Vector<CodeLine>& out;
			u32 number = 1;

			CodeLine& Line() {
				if (out.empty() || out.back().number != number) {
					out.push_back(CodeLine{number, Vector<CodeSpan>{GetMemoryResource(MemTag::Editor)}});
				}
				return out.back();
			}

			void Emit(std::string_view text, CodeToken kind) {
				if (text.empty()) {
					return;
				}
				CodeLine& line = Line();
				if (!line.spans.empty() && line.spans.back().kind == kind) {
					CodeSpan& back = line.spans.back();
					if (back.text.data() + back.text.size() == text.data()) {
						back.text = std::string_view(back.text.data(), back.text.size() + text.size());
						return;
					}
				}
				line.spans.push_back(CodeSpan{text, kind});
			}

			void EndLine() {
				Line();
				++number;
			}
		};

		bool IsNameChar(char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
				   c == '.' || c == '#' || c == '%';
		}

		void Tokenize(std::string_view source, CodeLanguage language, LineBuilder& build) {
			bool inTag = false;
			bool inBlock = false;
			bool afterName = false;
			size_t at = 0;

			auto emitThrough = [&](size_t from, size_t to, CodeToken kind) {
				size_t start = from;
				for (size_t i = from; i < to; ++i) {
					if (source[i] == '\n') {
						build.Emit(source.substr(start, i - start), kind);
						build.EndLine();
						start = i + 1;
					}
				}
				build.Emit(source.substr(start, to - start), kind);
			};

			while (at < source.size()) {
				const char c = source[at];

				if (c == '\n') {
					build.EndLine();
					++at;
					continue;
				}

				if (language == CodeLanguage::Markup && source.compare(at, 4, "<!--") == 0) {
					size_t end = source.find("-->", at + 4);
					end = end == std::string_view::npos ? source.size() : end + 3;
					emitThrough(at, end, CodeToken::Comment);
					at = end;
					continue;
				}
				if (language == CodeLanguage::Style && source.compare(at, 2, "/*") == 0) {
					size_t end = source.find("*/", at + 2);
					end = end == std::string_view::npos ? source.size() : end + 2;
					emitThrough(at, end, CodeToken::Comment);
					at = end;
					continue;
				}

				if (c == '"' || c == '\'') {
					size_t end = at + 1;
					while (end < source.size() && source[end] != c) {
						++end;
					}
					end = end < source.size() ? end + 1 : source.size();
					emitThrough(at, end, CodeToken::Value);
					at = end;
					continue;
				}

				if (language == CodeLanguage::Markup) {
					if (c == '<') {
						inTag = true;
						afterName = false;
						size_t end = at + 1;
						if (end < source.size() && source[end] == '/') {
							++end;
						}
						build.Emit(source.substr(at, end - at), CodeToken::Punctuation);
						at = end;
						continue;
					}
					if (c == '>' || c == '/' || c == '=') {
						inTag = c == '>' ? false : inTag;
						build.Emit(source.substr(at, 1), CodeToken::Punctuation);
						++at;
						continue;
					}
				} else {
					if (c == '{') {
						inBlock = true;
						afterName = false;
						build.Emit(source.substr(at, 1), CodeToken::Punctuation);
						++at;
						continue;
					}
					if (c == '}') {
						inBlock = false;
						afterName = false;
						build.Emit(source.substr(at, 1), CodeToken::Punctuation);
						++at;
						continue;
					}
					if (c == ':' && inBlock) {
						afterName = true;
						build.Emit(source.substr(at, 1), CodeToken::Punctuation);
						++at;
						continue;
					}
					if (c == ';') {
						afterName = false;
						build.Emit(source.substr(at, 1), CodeToken::Punctuation);
						++at;
						continue;
					}
				}

				if (IsNameChar(c)) {
					size_t end = at;
					while (end < source.size() && IsNameChar(source[end])) {
						++end;
					}
					CodeToken kind;
					if (language == CodeLanguage::Markup) {
						kind = inTag ? (afterName ? CodeToken::Attribute : CodeToken::Tag) : CodeToken::Text;
						if (inTag) {
							afterName = true;
						}
					} else {
						kind = inBlock ? (afterName ? CodeToken::Value : CodeToken::Attribute) : CodeToken::Tag;
					}
					build.Emit(source.substr(at, end - at), kind);
					at = end;
					continue;
				}

				size_t end = at;
				while (end < source.size() && source[end] != '\n' && !IsNameChar(source[end]) && source[end] != '"' &&
					   source[end] != '\'' && source[end] != '<' && source[end] != '>' && source[end] != '=' &&
					   source[end] != '/' && source[end] != '{' && source[end] != '}' && source[end] != ';' &&
					   source[end] != ':') {
					++end;
				}
				if (end == at) {
					++end;
				}
				CodeLine& line = build.Line();
				const CodeToken inherit = line.spans.empty() ? CodeToken::Text : line.spans.back().kind;
				build.Emit(source.substr(at, end - at), inherit);
				at = end;
			}
			build.Line();
		}

	} // namespace

	void TokenizeCode(std::string_view source, CodeLanguage language, Vector<CodeLine>& out) {
		out.clear();
		LineBuilder build{out};
		Tokenize(source, language, build);
	}

	namespace {

		bool CanvasIsReal(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight) {
			return canvasWidth > 0 && canvasHeight > 0 && viewport.w > 0.0f && viewport.h > 0.0f;
		}

		Vec2 CenteredOrigin(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight, f32 zoom) {
			const f32 width = static_cast<f32>(canvasWidth) * zoom;
			const f32 height = static_cast<f32>(canvasHeight) * zoom;
			return {viewport.x + (viewport.w - width) * 0.5f, viewport.y + (viewport.h - height) * 0.5f};
		}

	} // namespace

	UI::Rect CanvasRectFor(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight, const CanvasView& view) {
		if (canvasWidth <= 0 || canvasHeight <= 0) {
			return {viewport.x, viewport.y, 0.0f, 0.0f};
		}
		const f32 zoom = std::clamp(view.zoom, MIN_CANVAS_ZOOM, MAX_CANVAS_ZOOM);
		const f32 width = static_cast<f32>(canvasWidth) * zoom;
		const f32 height = static_cast<f32>(canvasHeight) * zoom;
		const Vec2 origin = CenteredOrigin(viewport, canvasWidth, canvasHeight, zoom);
		return {origin.x + view.pan.x, origin.y + view.pan.y, width, height};
	}

	f32 ZoomToFit(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight) {
		if (!CanvasIsReal(viewport, canvasWidth, canvasHeight)) {
			return 1.0f;
		}
		const f32 fit = std::min(viewport.w / static_cast<f32>(canvasWidth), viewport.h / static_cast<f32>(canvasHeight));
		return std::max(fit, MIN_CANVAS_ZOOM);
	}

	CanvasView ZoomAtPoint(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight, const CanvasView& view,
						   Vec2 point, f32 notches) {
		if (!CanvasIsReal(viewport, canvasWidth, canvasHeight) || notches == 0.0f) {
			return view;
		}
		const f32 before = std::clamp(view.zoom, MIN_CANVAS_ZOOM, MAX_CANVAS_ZOOM);
		constexpr f32 ZOOM_PER_NOTCH = 1.2f;
		const f32 after = std::clamp(before * std::pow(ZOOM_PER_NOTCH, notches), MIN_CANVAS_ZOOM, MAX_CANVAS_ZOOM);
		if (after == before) {
			return view;
		}

		const UI::Rect rect = CanvasRectFor(viewport, canvasWidth, canvasHeight, {before, view.pan});
		const Vec2 canvasPoint{(point.x - rect.x) / before, (point.y - rect.y) / before};
		const Vec2 origin = CenteredOrigin(viewport, canvasWidth, canvasHeight, after);
		CanvasView out;
		out.zoom = after;
		out.pan = {point.x - canvasPoint.x * after - origin.x, point.y - canvasPoint.y * after - origin.y};
		return ClampView(viewport, canvasWidth, canvasHeight, out);
	}

	CanvasView PanBy(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight, const CanvasView& view, Vec2 deltaPx) {
		CanvasView out = view;
		out.pan = {view.pan.x + deltaPx.x, view.pan.y + deltaPx.y};
		return ClampView(viewport, canvasWidth, canvasHeight, out);
	}

	CanvasView ClampView(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight, const CanvasView& view) {
		CanvasView out = view;
		out.zoom = std::clamp(view.zoom, MIN_CANVAS_ZOOM, MAX_CANVAS_ZOOM);
		if (!CanvasIsReal(viewport, canvasWidth, canvasHeight)) {
			return out;
		}
		constexpr f32 EDGE_KEEP = 48.0f;
		const f32 width = static_cast<f32>(canvasWidth) * out.zoom;
		const f32 height = static_cast<f32>(canvasHeight) * out.zoom;
		const f32 limitX = std::max((viewport.w + width) * 0.5f - EDGE_KEEP, 0.0f);
		const f32 limitY = std::max((viewport.h + height) * 0.5f - EDGE_KEEP, 0.0f);
		out.pan = {std::clamp(out.pan.x, -limitX, limitX), std::clamp(out.pan.y, -limitY, limitY)};
		return out;
	}

} // namespace Shalltear::UIBuilderOps
