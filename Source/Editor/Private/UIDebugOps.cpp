#include "Shalltear/Editor/UIDebugOps.h"

#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/Font.h"
#include "Shalltear/Core/UI/VectorIcon.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Core/Window/Window.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"

#include "Panels/UIDebuggerPanel.h"
#include "StyleSelectorMatch.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Shalltear::UIDebugOps {

	namespace {

		struct BuiltInSheet {
			std::string_view name;
			std::string_view (*source)();
		};

		std::span<const BuiltInSheet> GetBuiltInSheets() {
			static const BuiltInSheet sheets[] = {
				{"slt-widgets", &UI::Widgets::GetDefaultSkinSource},
				{"slt-dock", &UI::DockManager::GetDefaultSkinSource},
				{"ed-skin", &EditorTheme::GetSheetSource},
			};
			return sheets;
		}

		inline constexpr u64 PATH_FNV_BASIS = 14695981039346656037ull;
		inline constexpr u64 PATH_FNV_PRIME = 1099511628211ull;

		u64 FoldIndex(u64 seed, u32 index) {
			for (u32 byte = 0; byte < 4; ++byte) {
				seed ^= static_cast<u64>((index >> (byte * 8)) & 0xFFu);
				seed *= PATH_FNV_PRIME;
			}
			return seed;
		}

		bool ContainsFolded(std::string_view haystack, std::string_view needle) {
			if (needle.empty()) {
				return true;
			}
			if (needle.size() > haystack.size()) {
				return false;
			}
			auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c; };
			for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
				size_t i = 0;
				for (; i < needle.size(); ++i) {
					if (lower(haystack[start + i]) != lower(needle[i])) {
						break;
					}
				}
				if (i == needle.size()) {
					return true;
				}
			}
			return false;
		}

		u32 ClassesOf(const UI::Element& element, StringHash (&out)[UI::MAX_ELEMENT_CLASSES]) {
			u32 count = element.classCount < UI::MAX_ELEMENT_CLASSES ? element.classCount : UI::MAX_ELEMENT_CLASSES;
			for (u32 i = 0; i < count; ++i) {
				out[i] = element.classes[i];
			}
			return count;
		}

		u8 CurrentPseudo(const UI::Element& element) {
			if (HasFlag(element.flags, UI::ElementFlags::DropHover)) {
				return UI::PSEUDO_DROP;
			}
			if (HasFlag(element.flags, UI::ElementFlags::Focused)) {
				return UI::PSEUDO_FOCUS;
			}
			if (HasFlag(element.flags, UI::ElementFlags::Pressed)) {
				return UI::PSEUDO_ACTIVE;
			}
			if (HasFlag(element.flags, UI::ElementFlags::Hovered)) {
				return UI::PSEUDO_HOVER;
			}
			return UI::PSEUDO_NONE;
		}

		bool CarriesMarker(const UI::Context& context, UI::ElementHandle element) {
			return context.HasClass(element, StringHash(MarkerClass));
		}

		constexpr std::string_view NoneText = "\xE2\x80\x94";

		str YesNo(bool value) { return str(value ? "yes" : "no"); }

		UI::Edges EdgesBetween(const UI::Rect& outer, const UI::Rect& inner) {
			return {inner.x - outer.x, inner.y - outer.y, (outer.x + outer.w) - (inner.x + inner.w),
					(outer.y + outer.h) - (inner.y + inner.h)};
		}

		str Point(Vec2 value) { return FormatNumber(value.x) + ", " + FormatNumber(value.y); }

		str HexColor(Color color) {
			Color32 quantized = color.ToColor32();
			constexpr char DIGITS[] = "0123456789abcdef";
			const u8 channels[4] = {quantized.r, quantized.g, quantized.b, quantized.a};
			str text = "#";
			for (u32 i = 0; i < 4; ++i) {
				text += DIGITS[channels[i] >> 4];
				text += DIGITS[channels[i] & 0xF];
			}
			return text;
		}

		str HexU64(u64 value) { return value == 0 ? str("0") : std::format("0x{:x}", value); }

		std::string_view CursorName(SystemCursor cursor) {
			switch (cursor) {
			case SystemCursor::IBeam:
				return "i-beam";
			case SystemCursor::Crosshair:
				return "crosshair";
			case SystemCursor::Hand:
				return "hand";
			case SystemCursor::ResizeEW:
				return "resize-ew";
			case SystemCursor::ResizeNS:
				return "resize-ns";
			case SystemCursor::ResizeNWSE:
				return "resize-nwse";
			case SystemCursor::ResizeNESW:
				return "resize-nesw";
			case SystemCursor::ResizeAll:
				return "resize-all";
			case SystemCursor::NotAllowed:
				return "not-allowed";
			default:
				return "arrow";
			}
		}

	} // anonymous namespace

	NameDictionary::NameDictionary() = default;

	void NameDictionary::Add(std::string_view name) {
		if (name.empty()) {
			return;
		}
		_names.try_emplace(StringHash(name), name);
	}

	void NameDictionary::AddSheet(StringHash tag, std::string_view name, std::string_view source) {
		_sheets.push_back(Sheet{tag,
								String{name, GetMemoryResource(MemTag::Editor)},
								String{source, GetMemoryResource(MemTag::Editor)}});
		if (source.empty()) {
			return;
		}
		Add(name);
		Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Editor)};
		UI::StyleSheet::SplitSource(_sheets.back().source, rules);
		for (const UI::StyleRuleSource& rule : rules) {
			std::string_view selector = rule.selector;
			if (size_t colon = selector.find(':'); colon != std::string_view::npos) {
				selector = selector.substr(0, colon);
			}
			size_t cursor = 0;
			while (cursor < selector.size()) {
				size_t next = selector.find_first_of(".#", cursor + 1);
				std::string_view part = selector.substr(cursor, next == std::string_view::npos ? next : next - cursor);
				if (!part.empty() && part != "*") {
					Add(part[0] == '.' || part[0] == '#' ? part.substr(1) : part);
				}
				if (next == std::string_view::npos) {
					break;
				}
				cursor = next;
			}
			for (const UI::StyleDeclarationSource& declaration : rule.declarations) {
				Add(declaration.name);
			}
		}
	}

	void NameDictionary::AddCandidateSheet(std::string_view name, std::string_view source) {
		if (name.empty()) {
			return;
		}
		StringHash tag{name};
		for (Sheet& candidate : _candidates) {
			if (candidate.tag == tag) {
				candidate.name.assign(name);
				candidate.source.assign(source);
				return;
			}
		}
		_candidates.push_back(Sheet{tag,
									String{name, GetMemoryResource(MemTag::Editor)},
									String{source, GetMemoryResource(MemTag::Editor)}});
	}

	void NameDictionary::AddName(std::string_view name) {
		if (name.empty()) {
			return;
		}
		for (const String& typed : _typed) {
			if (typed == name) {
				return;
			}
		}
		_typed.push_back(String{name, GetMemoryResource(MemTag::Editor)});
		Add(name);
	}

	void NameDictionary::Rebuild(const UI::Context& context) {
		_names.clear();
		_sheets.clear();

		for (const String& typed : _typed) {
			Add(typed);
		}
		for (std::string_view name : UI::GetStylePropertyNames()) {
			Add(name);
		}
		for (const UI::StyleVarDesc& desc : UI::GetDefaultStyleVars()) {
			Add(desc.name);
		}
		for (u32 i = 0; i < EditorTheme::COLOR_TOKEN_COUNT; ++i) {
			Add(EditorTheme::GetVarName(static_cast<EditorTheme::ColorToken>(i)));
		}
		for (u32 i = 0; i < EditorTheme::METRIC_TOKEN_COUNT; ++i) {
			Add(EditorTheme::GetVarName(static_cast<EditorTheme::MetricToken>(i)));
		}
		Vector<const UI::WidgetDesc*> widgets{GetMemoryResource(MemTag::Editor)};
		UI::WidgetRegistry::GetAll(widgets);
		for (const UI::WidgetDesc* desc : widgets) {
			if (!desc) {
				continue;
			}
			Add(desc->tag);
			for (std::string_view klass : desc->builtInClasses) {
				Add(klass);
			}
		}
		for (StringHash tag : context.GetLoadedSheetTags()) {
			bool resolved = false;
			for (const BuiltInSheet& builtIn : GetBuiltInSheets()) {
				if (StringHash(builtIn.name) == tag) {
					AddSheet(tag, builtIn.name, builtIn.source());
					resolved = true;
					break;
				}
			}
			for (const Sheet& candidate : _candidates) {
				if (resolved) {
					break;
				}
				if (candidate.tag == tag) {
					AddSheet(tag, candidate.name, candidate.source);
					resolved = true;
				}
			}
			if (!resolved) {
				AddSheet(tag, {}, {});
			}
		}
	}

	std::string_view NameDictionary::Lookup(StringHash hash) const {
		auto it = _names.find(hash);
		return it == _names.end() ? std::string_view{} : std::string_view(it->second);
	}

	str NameDictionary::Render(StringHash hash) const {
		std::string_view name = Lookup(hash);
		if (!name.empty()) {
			return str(name);
		}
		if (hash.GetValue() == 0) {
			return str();
		}
		return std::format("#0x{:08x}\xE2\x80\xA6", static_cast<u32>(hash.GetValue() >> 32));
	}

	u32 NameDictionary::GetCount() const {
		return static_cast<u32>(_names.size());
	}

	std::span<const NameDictionary::Sheet> NameDictionary::GetSheets() const {
		return _sheets;
	}

	NameDictionary::Residue NameDictionary::MeasureResidue(const UI::Context& context) const {
		Residue residue;
		Snapshot snapshot;
		BuildSnapshot(context, snapshot);
		for (const Row& row : snapshot.rows) {
			const UI::Element* element = context.GetElement(row.element);
			if (!element) {
				continue;
			}
			if (element->id.GetValue() != 0) {
				++residue.ids;
				residue.idsUnresolved += Lookup(element->id).empty() ? 1u : 0u;
			}
			if (element->typeTag.GetValue() != 0) {
				++residue.tags;
				residue.tagsUnresolved += Lookup(element->typeTag).empty() ? 1u : 0u;
			}
			for (u8 i = 0; i < element->classCount; ++i) {
				++residue.classes;
				residue.classesUnresolved += Lookup(element->classes[i]).empty() ? 1u : 0u;
			}
		}
		residue.total = residue.ids + residue.tags + residue.classes;
		residue.unresolved = residue.idsUnresolved + residue.tagsUnresolved + residue.classesUnresolved;
		return residue;
	}

	void EnumerateTargets(Vector<Target>& out) {
		out.clear();
		UISystem::ForEachContext([&out](Handle<Window> window, UI::Context&) {
			Window* resolved = window.Get();
			Target target;
			target.kind = Target::Kind::Window;
			target.id = window.GetInstanceId();
			target.label = resolved ? resolved->GetTitle() : str("Window");
			if (target.label.empty()) {
				target.label = "Window";
			}
			out.push_back(std::move(target));
		});
		Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
		PanelManager::GetOpenPanels(panels);

		u32 ordinal = 0;
		UISystem::ForEachSurface([&out, &ordinal, &panels](UISurfaceId id, UI::Context& context) {
			++ordinal;
			Target target;
			target.kind = Target::Kind::Surface;
			target.id = id;
			str owner(UISystem::GetSurfaceName(id));
			if (owner.empty()) {
				for (const Handle<EditorPanel>& panel : panels) {
					const EditorPanel* resolved = panel.Get();
					if (resolved && resolved->GetPresentedSurface() == id) {
						owner = resolved->GetTitle();
						break;
					}
				}
			}
			Vec2I size = context.GetSize();
			const str extent = " (" + std::to_string(size.x) + "x" + std::to_string(size.y) + ")";
			target.label = owner.empty() ? "Surface #" + std::to_string(ordinal) + extent : owner + extent;
			out.push_back(std::move(target));
		});
	}

	Target TargetForContext(const UI::Context& context) {
		Target found;
		found.id = 0;
		Vector<Target> targets{GetMemoryResource(MemTag::Editor)};
		EnumerateTargets(targets);
		for (Target& candidate : targets) {
			if (ResolveTarget(candidate) == &context) {
				found = std::move(candidate);
				break;
			}
		}
		return found;
	}

	bool OpenDebuggerOn(const Target& target) {
		if (!PanelManager::FocusPanelOfType(StringHash("UIDebuggerPanel"))) {
			return false;
		}
		Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
		PanelManager::GetOpenPanels(panels);
		for (const Handle<EditorPanel>& panel : panels) {
			if (UIDebuggerPanel* debugger = dynamic_cast<UIDebuggerPanel*>(panel.Get())) {
				debugger->ShowTarget(target);
				return true;
			}
		}
		return false;
	}

	UI::Context* ResolveTarget(const Target& target) {
		if (target.kind == Target::Kind::Surface) {
			return UISystem::GetSurfaceContext(static_cast<UISurfaceId>(target.id));
		}
		UI::Context* found = nullptr;
		UISystem::ForEachContext([&found, id = target.id](Handle<Window> window, UI::Context& context) {
			if (!found && window.GetInstanceId() == id) {
				found = &context;
			}
		});
		return found;
	}

	void BuildSnapshot(const UI::Context& context, Snapshot& out) {
		out.rows.clear();
		out.revision = context.GetTreeRevision();

		struct Frame {
			UI::ElementHandle element;
			u32 depth;
			u64 key;
		};
		Vector<Frame> stack{GetMemoryResource(MemTag::Editor)};
		stack.push_back({context.GetRoot(), 0, PATH_FNV_BASIS});
		while (!stack.empty()) {
			Frame frame = stack.back();
			stack.pop_back();
			const UI::Element* element = context.GetElement(frame.element);
			if (!element || CarriesMarker(context, frame.element)) {
				continue;
			}
			u32 childCount = 0;
			for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
				++childCount;
				child = context.GetElement(context.MakeHandle(child))->nextSibling;
			}
			out.rows.push_back(Row{frame.element, frame.depth, childCount, frame.key});

			size_t first = stack.size();
			u32 position = 0;
			for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX; ++position) {
				UI::ElementHandle handle = context.MakeHandle(child);
				stack.push_back({handle, frame.depth + 1, FoldIndex(frame.key, position)});
				child = context.GetElement(handle)->nextSibling;
			}
			std::reverse(stack.begin() + static_cast<ptrdiff_t>(first), stack.end());
		}
	}

	u64 PathKey(std::span<const u32> path) {
		u64 key = PATH_FNV_BASIS;
		for (u32 index : path) {
			key = FoldIndex(key, index);
		}
		return key;
	}

	bool ExpandAncestors(std::span<const u32> path, Vector<u64>& collapsed) {
		if (collapsed.empty()) {
			return false;
		}
		bool moved = false;
		u64 key = PATH_FNV_BASIS;
		for (size_t depth = 0; depth < path.size(); ++depth) {
			auto at = std::find(collapsed.begin(), collapsed.end(), key);
			if (at != collapsed.end()) {
				collapsed.erase(at);
				moved = true;
			}
			key = FoldIndex(key, path[depth]);
		}
		return moved;
	}

	void BuildTreeView(const UI::Context& context, const Snapshot& snapshot, const NameDictionary& names,
					   const TreeViewDesc& desc, Vector<TreeRow>& out) {
		out.clear();
		if (snapshot.rows.empty()) {
			return;
		}
		Vector<str> labels{GetMemoryResource(MemTag::Editor)};
		labels.reserve(snapshot.rows.size());
		for (const Row& row : snapshot.rows) {
			labels.push_back(RowLabel(context, row.element, names));
		}

		auto isCollapsed = [&desc](u64 key) {
			return std::find(desc.collapsed.begin(), desc.collapsed.end(), key) != desc.collapsed.end();
		};
		auto emit = [&out, &snapshot, &labels](size_t index, bool expanded, bool matched) {
			const Row& row = snapshot.rows[index];
			TreeRow drawn;
			drawn.element = row.element;
			drawn.key = row.key;
			drawn.depth = row.depth;
			drawn.hasChildren = row.childCount != 0;
			drawn.expanded = expanded;
			drawn.matched = matched;
			drawn.label = labels[index];
			out.push_back(std::move(drawn));
		};

		if (desc.query.empty()) {
			u32 hideBelow = 0xFFFFFFFFu;
			for (size_t i = 0; i < snapshot.rows.size(); ++i) {
				const Row& row = snapshot.rows[i];
				if (row.depth <= hideBelow) {
					hideBelow = 0xFFFFFFFFu;
				}
				if (row.depth > hideBelow) {
					continue;
				}
				const bool collapsed = row.childCount != 0 && isCollapsed(row.key);
				emit(i, !collapsed, false);
				if (collapsed) {
					hideBelow = row.depth;
				}
			}
			return;
		}

		Vector<u8> keep{GetMemoryResource(MemTag::Editor)};
		Vector<u8> matched{GetMemoryResource(MemTag::Editor)};
		keep.assign(snapshot.rows.size(), 0);
		matched.assign(snapshot.rows.size(), 0);
		Vector<u8> keptBelow{GetMemoryResource(MemTag::Editor)};
		for (size_t i = snapshot.rows.size(); i-- > 0;) {
			const Row& row = snapshot.rows[i];
			if (row.depth + 2 > keptBelow.size()) {
				keptBelow.resize(row.depth + 2, 0);
			}
			const bool hit = ContainsFolded(labels[i], desc.query);
			matched[i] = hit ? 1u : 0u;
			keep[i] = (hit || keptBelow[row.depth + 1] != 0) ? 1u : 0u;
			std::fill(keptBelow.begin() + static_cast<ptrdiff_t>(row.depth) + 1, keptBelow.end(), 0u);
			if (keep[i] != 0) {
				keptBelow[row.depth] = 1u;
			}
		}
		for (size_t i = 0; i < snapshot.rows.size(); ++i) {
			if (keep[i] != 0) {
				emit(i, true, matched[i] != 0);
			}
		}
	}

	str RowLabel(const UI::Context& context, UI::ElementHandle element, const NameDictionary& names) {
		const UI::Element* pod = context.GetElement(element);
		if (!pod) {
			return str("<dead>");
		}
		str label;
		if (pod->id.GetValue() != 0) {
			label = "#" + names.Render(pod->id);
		} else if (pod->typeTag.GetValue() != 0) {
			label = names.Render(pod->typeTag);
		} else {
			label = "Element";
		}
		for (u8 i = 0; i < pod->classCount; ++i) {
			label += " ." + names.Render(pod->classes[i]);
		}
		return label;
	}

	void PathTo(const UI::Context& context, UI::ElementHandle element, Vector<u32>& out) {
		out.clear();
		UI::ElementHandle root = context.GetRoot();
		const UI::Element* pod = context.GetElement(element);
		if (!pod || element == root) {
			return;
		}
		while (pod && pod->parent != UI::INVALID_UI_INDEX) {
			UI::ElementHandle parent = context.MakeHandle(pod->parent);
			const UI::Element* parentPod = context.GetElement(parent);
			if (!parentPod) {
				break;
			}
			u32 position = 0;
			for (u32 child = parentPod->firstChild; child != UI::INVALID_UI_INDEX; ++position) {
				if (context.MakeHandle(child) == element) {
					break;
				}
				child = context.GetElement(context.MakeHandle(child))->nextSibling;
			}
			out.push_back(position);
			element = parent;
			pod = parentPod;
		}
		if (element != root) {
			out.clear();
			return;
		}
		std::reverse(out.begin(), out.end());
	}

	UI::ElementHandle Resolve(const UI::Context& context, std::span<const u32> path) {
		UI::ElementHandle current = context.GetRoot();
		for (u32 step : path) {
			const UI::Element* pod = context.GetElement(current);
			if (!pod) {
				return {};
			}
			u32 child = pod->firstChild;
			for (u32 i = 0; i < step && child != UI::INVALID_UI_INDEX; ++i) {
				child = context.GetElement(context.MakeHandle(child))->nextSibling;
			}
			if (child == UI::INVALID_UI_INDEX) {
				return {};
			}
			current = context.MakeHandle(child);
		}
		return current;
	}

	ElementIdentity IdentityOf(const UI::Context& context, UI::ElementHandle element) {
		ElementIdentity identity;
		const UI::Element* pod = context.GetElement(element);
		if (!pod) {
			return identity;
		}
		identity.id = pod->id;
		identity.typeTag = pod->typeTag;
		for (u32 i = 0; i < UI::MAX_ELEMENT_CLASSES; ++i) {
			identity.classes[i] = pod->classes[i];
		}
		identity.valid = true;
		return identity;
	}

	UI::ElementHandle ResolveSame(const UI::Context& context, std::span<const u32> path,
								  const ElementIdentity& identity) {
		if (!identity.valid) {
			return {};
		}
		UI::ElementHandle handle = Resolve(context, path);
		const UI::Element* pod = context.GetElement(handle);
		if (!pod || pod->id != identity.id || pod->typeTag != identity.typeTag) {
			return {};
		}
		bool used[UI::MAX_ELEMENT_CLASSES] = {};
		for (u32 i = 0; i < UI::MAX_ELEMENT_CLASSES; ++i) {
			bool found = false;
			for (u32 j = 0; j < UI::MAX_ELEMENT_CLASSES && !found; ++j) {
				if (!used[j] && pod->classes[j] == identity.classes[i]) {
					used[j] = true;
					found = true;
				}
			}
			if (!found) {
				return {};
			}
		}
		return handle;
	}

	str FormatNumber(f32 value) {
		str text = std::format("{:.2f}", value);
		if (text.find('.') == str::npos) {
			return text;
		}
		while (!text.empty() && text.back() == '0') {
			text.pop_back();
		}
		if (!text.empty() && text.back() == '.') {
			text.pop_back();
		}
		return text;
	}

	void BuildGeometry(const UI::Context& context, UI::ElementHandle element, Geometry& out) {
		out = Geometry{};
		const UI::Element* pod = context.GetElement(element);
		if (!pod) {
			return;
		}
		const UI::LayoutData& box = context.GetLayout(element);
		out.worldBound = box.borderBox;

		out.margin = EdgesBetween(box.marginBox, box.borderBox);
		out.border = EdgesBetween(box.borderBox, box.paddingBox);
		out.padding = EdgesBetween(box.paddingBox, box.content);

		out.contentRect = {box.content.x - box.borderBox.x, box.content.y - box.borderBox.y, box.content.w,
						   box.content.h};

		out.layout = box.borderBox;
		if (pod->parent != UI::INVALID_UI_INDEX) {
			const UI::Rect& parentContent = context.GetLayout(context.MakeHandle(pod->parent)).content;
			out.layout.x -= parentContent.x;
			out.layout.y -= parentContent.y;
		}

		out.worldClip = box.borderBox;
		for (u32 up = pod->parent; up != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(up);
			const UI::Element* ancestor = context.GetElement(handle);
			if (!ancestor) {
				break;
			}
			if (context.GetComputedStyle(handle).overflow != UI::Overflow::Visible) {
				out.worldClip = UI::RectIntersect(out.worldClip, context.GetLayout(handle).borderBox);
			}
			up = ancestor->parent;
		}
		out.clipped = out.worldClip != box.borderBox;
	}

	UI::ElementHandle PickAt(const UI::Context& context, Vec2 pointPx) {
		UI::ElementHandle hit = context.HitTest(pointPx, UI::HitTestMode::Precise);
		if (!hit.IsValid()) {
			return {};
		}
		UI::ElementHandle answer = hit;
		for (UI::ElementHandle walk = hit; walk.IsValid();) {
			const UI::Element* pod = context.GetElement(walk);
			if (!pod) {
				break;
			}
			UI::ElementHandle parent =
				pod->parent != UI::INVALID_UI_INDEX ? context.MakeHandle(pod->parent) : UI::ElementHandle{};
			if (CarriesMarker(context, walk)) {
				answer = parent;
			}
			walk = parent;
		}
		return answer;
	}

	void BuildHighlight(const UI::Context& context, UI::ElementHandle element, Highlight& out) {
		out = Highlight{};
		if (!context.GetElement(element)) {
			return;
		}
		Geometry geometry;
		BuildGeometry(context, element, geometry);
		const UI::LayoutData& box = context.GetLayout(element);
		const f32 scale = std::max(context.GetContentScale(), 0.0001f);
		auto logicalRect = [scale](const UI::Rect& rect) {
			return UI::Rect{rect.x / scale, rect.y / scale, rect.w / scale, rect.h / scale};
		};
		auto logicalEdges = [scale](const UI::Edges& edges) {
			return UI::Edges{edges.left / scale, edges.top / scale, edges.right / scale, edges.bottom / scale};
		};
		out.bands[static_cast<size_t>(HighlightRing::Margin)] = {logicalRect(box.marginBox),
																 logicalEdges(geometry.margin)};
		out.bands[static_cast<size_t>(HighlightRing::Border)] = {logicalRect(box.borderBox),
																 logicalEdges(geometry.border)};
		out.bands[static_cast<size_t>(HighlightRing::Padding)] = {logicalRect(box.paddingBox),
																  logicalEdges(geometry.padding)};
		out.bands[static_cast<size_t>(HighlightRing::Content)] = {logicalRect(box.content), {}};
	}

	TextOverlayStats BuildTextBoxes(const UI::Context& context, UI::ElementHandle element, TextBoxKinds kinds,
									Vector<TextBox>& out) {
		TextOverlayStats stats;
		if (kinds == TextBoxKinds::None) {
			return stats;
		}
		const UI::Context::ShapedTextView view = context.GetShapedText(element);
		if (!view.run) {
			return stats;
		}
		const UI::ShapedRun& run = *view.run;
		++stats.runs;
		const f32 dx = view.originPx.x;
		const f32 dy = view.originPx.y;
		const size_t before = out.size();

		if (HasFlag(kinds, TextBoxKinds::Characters)) {
			for (const UI::ShapedGlyph& glyph : run.glyphs) {
				out.push_back(TextBox{{glyph.rect.x + dx, glyph.rect.y + dy, glyph.rect.w, glyph.rect.h},
									  TextBoxKinds::Characters});
			}
		}

		const f32 lineHeight = run.lineHeight > 0.0f ? run.lineHeight : run.extent.y;

		if (HasFlag(kinds, TextBoxKinds::Words)) {
			if (run.caretStops.size() > 1) {
				const str text = context.GetTextString(element);
				auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
				size_t start = 0;
				while (start < text.size()) {
					if (isSpace(text[start])) {
						++start;
						continue;
					}
					size_t end = start;
					while (end < text.size() && !isSpace(text[end])) {
						++end;
					}
					const UI::CaretStop first = run.StopForByte(static_cast<u32>(start));
					UI::CaretStop last = run.StopForByte(static_cast<u32>(end));
					for (const UI::CaretStop& stop : run.caretStops) {
						if (stop.byteOffset == end && stop.line == first.line) {
							last = stop;
						}
					}
					if (first.line == last.line && last.x > first.x) {
						const f32 y = dy + static_cast<f32>(first.line) * lineHeight;
						out.push_back(TextBox{{first.x + dx, y, last.x - first.x, lineHeight}, TextBoxKinds::Words});
					}
					start = end;
				}
			} else {
				++stats.wordlessRuns;
			}
		}

		if (HasFlag(kinds, TextBoxKinds::Lines) && lineHeight > 0.0f) {
			const u32 lineCount = std::max(1u, static_cast<u32>(std::lround(run.extent.y / lineHeight)));
			for (u32 line = 0; line < lineCount; ++line) {
				const f32 top = static_cast<f32>(line) * lineHeight;
				f32 left = 0.0f;
				f32 right = 0.0f;
				bool any = false;
				for (const UI::ShapedGlyph& glyph : run.glyphs) {
					const f32 centre = glyph.rect.y + glyph.rect.h * 0.5f;
					if (centre < top || centre >= top + lineHeight) {
						continue;
					}
					if (!any) {
						left = glyph.rect.x;
						right = glyph.rect.x + glyph.rect.w;
						any = true;
					} else {
						left = std::min(left, glyph.rect.x);
						right = std::max(right, glyph.rect.x + glyph.rect.w);
					}
				}
				if (any) {
					out.push_back(TextBox{{left + dx, top + dy, right - left, lineHeight}, TextBoxKinds::Lines});
				}
			}
		}

		stats.boxes = static_cast<u32>(out.size() - before);
		return stats;
	}

	TextOverlayStats CollectTextBoxes(const UI::Context& context, TextBoxKinds kinds, Vector<TextBox>& out) {
		out.clear();
		TextOverlayStats stats;
		if (kinds == TextBoxKinds::None) {
			return stats;
		}
		Vector<UI::ElementHandle> stack{GetMemoryResource(MemTag::Editor)};
		stack.push_back(context.GetRoot());
		while (!stack.empty()) {
			UI::ElementHandle handle = stack.back();
			stack.pop_back();
			const UI::Element* element = context.GetElement(handle);
			if (!element || CarriesMarker(context, handle)) {
				continue;
			}
			const TextOverlayStats one = BuildTextBoxes(context, handle, kinds, out);
			stats.runs += one.runs;
			stats.boxes += one.boxes;
			stats.wordlessRuns += one.wordlessRuns;
			for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle childHandle = context.MakeHandle(child);
				stack.push_back(childHandle);
				child = context.GetElement(childHandle)->nextSibling;
			}
		}
		return stats;
	}

	void BuildAtlasPages(Vector<AtlasPageRow>& out) {
		out.clear();
		AtlasPageRow glyphs{"Glyphs", {}};
		if (UI::IFontSystem* fonts = UISystem::GetFontSystem(); fonts && fonts->GetAtlasPage(glyphs.page)) {
			out.push_back(glyphs);
		}
		AtlasPageRow icons{"Icons", {}};
		if (UI::IVectorIconSystem* iconSystem = UISystem::GetVectorIconSystem();
			iconSystem && iconSystem->GetAtlasPage(icons.page)) {
			out.push_back(icons);
		}
	}

	void BuildAttributes(const UI::Context& context, UI::ElementHandle element, const NameDictionary& names,
						 Vector<AttributeRow>& out) {
		out.clear();
		const UI::Element* pod = context.GetElement(element);
		if (!pod) {
			return;
		}
		const UI::Style& style = context.GetComputedStyle(element);

		using Edit = AttributeRow::Edit;
		auto row = [&out](std::string_view name, str value, Edit edit = Edit::None) {
			out.push_back(AttributeRow{name, std::move(value), edit});
		};
		auto orNone = [](str value) { return value.empty() ? str(NoneText) : value; };

		row("id", pod->id == StringHash{} ? str() : names.Render(pod->id), Edit::Id);
		row("type tag", pod->typeTag == StringHash{} ? str() : names.Render(pod->typeTag), Edit::TypeTag);
		str classes;
		for (u8 i = 0; i < pod->classCount; ++i) {
			classes += (classes.empty() ? "." : " .") + names.Render(pod->classes[i]);
		}
		row("classes", std::move(classes), Edit::Classes);

		row("visible", YesNo(HasFlag(pod->flags, UI::ElementFlags::Visible)), Edit::Visible);
		row("hoverable", YesNo(HasFlag(pod->flags, UI::ElementFlags::Hoverable)), Edit::Hoverable);
		row("focusable", YesNo(HasFlag(pod->flags, UI::ElementFlags::Focusable)), Edit::Focusable);
		str state;
		auto flag = [&state, pod](UI::ElementFlags bit, std::string_view name) {
			if (HasFlag(pod->flags, bit)) {
				state += state.empty() ? str(name) : " " + str(name);
			}
		};
		flag(UI::ElementFlags::Pressed, "pressed");
		flag(UI::ElementFlags::Hovered, "hovered");
		flag(UI::ElementFlags::Focused, "focused");
		flag(UI::ElementFlags::DropHover, "drop-hover");
		row("state", orNone(std::move(state)));

		row("pointer events", YesNo(style.pointerEvents));
		row("cursor", str(CursorName(style.cursor)));
		row("tint", HexColor(pod->tint), Edit::Tint);
		row("z index", std::to_string(style.zIndex));
		row("user tag", HexU64(pod->userTag), Edit::UserTag);
		row("scroll offset", Point(pod->scrollOffset), Edit::ScrollOffset);
		row("max scroll", Point(pod->maxScroll));

		str content;
		auto has = [&content](bool present, std::string_view name) {
			if (present) {
				content += content.empty() ? str(name) : " " + str(name);
			}
		};
		has(pod->textIndex != UI::INVALID_UI_INDEX, "text");
		has(context.HasIcon(element), "icon");
		has(context.HasTextInput(element), "text-input");
		has(context.HasEvents(element), "events");
		row("content", orNone(std::move(content)));

		if (pod->textIndex != UI::INVALID_UI_INDEX) {
			row("text", context.GetTextString(element), Edit::Text);
		}
	}

	void BuildSheets(const NameDictionary& names, Vector<SheetRow>& out) {
		out.clear();
		Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Editor)};
		for (const NameDictionary::Sheet& sheet : names.GetSheets()) {
			SheetRow row;
			row.tag = sheet.tag;
			row.name = sheet.name;
			row.readable = !sheet.source.empty();
			if (row.readable) {
				UI::StyleSheet::SplitSource(sheet.source, rules);
				row.ruleCount = static_cast<u32>(rules.size());
			}
			out.push_back(std::move(row));
		}
	}

	void BuildMatches(const UI::Context& context, UI::ElementHandle element, const NameDictionary& names,
					  Vector<Match>& out) {
		out.clear();
		const UI::Element* pod = context.GetElement(element);
		if (!pod) {
			return;
		}
		StringHash classes[UI::MAX_ELEMENT_CLASSES];
		u32 classCount = ClassesOf(*pod, classes);
		u8 pseudo = CurrentPseudo(*pod);

		Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Editor)};
		for (const NameDictionary::Sheet& sheet : names.GetSheets()) {
			if (sheet.source.empty()) {
				continue;
			}
			UI::StyleSheet::SplitSource(sheet.source, rules);
			for (u32 index = 0; index < static_cast<u32>(rules.size()); ++index) {
				const UI::StyleRuleSource& rule = rules[index];
				EditorInternal::ParsedSelector parsed = EditorInternal::ParseSelectorText(rule.selector);
				if (!EditorInternal::SelectorMatchesIdentity(parsed, pod->typeTag, pod->id,
															 std::span<const StringHash>(classes, classCount))) {
					continue;
				}
				out.push_back(Match{std::string_view(sheet.name), rule.selector, rule.body, index, parsed.specificity,
									parsed.pseudo, parsed.pseudo == UI::PSEUDO_NONE || parsed.pseudo == pseudo});
			}
		}
		std::stable_sort(out.begin(), out.end(), [](const Match& a, const Match& b) {
			return a.specificity < b.specificity;
		});
	}

	void BuildProperties(const UI::Context& context, UI::ElementHandle element, const NameDictionary& names,
						 Vector<PropertyRow>& out) {
		out.clear();
		if (!context.GetElement(element)) {
			return;
		}
		const UI::Style& inlineStyle = context.GetStyle(element);
		const UI::Style& computed = context.GetComputedStyle(element);
		const UI::Style defaults{};

		Vector<Match> matches{GetMemoryResource(MemTag::Editor)};
		BuildMatches(context, element, names, matches);
		struct Contribution {
			std::string_view sheet;
			Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Editor)};
		};
		Vector<Contribution> contributions{GetMemoryResource(MemTag::Editor)};
		for (const Match& match : matches) {
			if (!match.applies) {
				continue;
			}
			contributions.push_back(Contribution{match.sheet, Vector<UI::StyleDeclarationSource>{GetMemoryResource(MemTag::Editor)}});
			UI::StyleSheet::SplitDeclarations(match.body, contributions.back().declarations);
		}

		char computedText[UI::MAX_STYLE_VALUE_TEXT];
		char inlineText[UI::MAX_STYLE_VALUE_TEXT];
		char defaultText[UI::MAX_STYLE_VALUE_TEXT];
		for (std::string_view name : UI::GetStylePropertyNames()) {
			StringHash hash{name};
			PropertyRow row;
			row.name = name;
			std::string_view computedValue = UI::ReadStyleProperty(computed, hash, computedText, sizeof(computedText));
			std::string_view inlineValue = UI::ReadStyleProperty(inlineStyle, hash, inlineText, sizeof(inlineText));
			std::string_view defaultValue = UI::ReadStyleProperty(defaults, hash, defaultText, sizeof(defaultText));
			row.value = str(computedValue);

			if (computedValue != inlineValue) {
				row.source = ValueSource::Sheet;
			} else if (inlineValue != defaultValue) {
				row.source = ValueSource::Inline;
			} else {
				row.source = ValueSource::Default;
			}

			if (row.source == ValueSource::Sheet) {
				for (const Contribution& contribution : contributions) {
					for (const UI::StyleDeclarationSource& declaration : contribution.declarations) {
						if (declaration.name == name) {
							row.sheet = contribution.sheet;
						}
					}
				}
			}
			out.push_back(std::move(row));
		}
	}

	namespace {

		void WriteSession(UI::Context& context, UI::ElementHandle element, StyleEdits& edits) {
			UI::Style style = edits.captured;
			for (const StyleEdits::Declaration& declaration : edits.declarations) {
				UI::ApplyStyleDeclaration(style, declaration.name, declaration.value, &context.GetStyleVars());
			}
			edits.written = style;
			context.SetStyle(element, style);
		}

		bool NeedsRecapture(const UI::Context& context, UI::ElementHandle element, const StyleEdits& edits) {
			return !edits.armed || context.GetStyle(element) != edits.written;
		}

		void Recapture(const UI::Context& context, UI::ElementHandle element, StyleEdits& edits) {
			edits.captured = context.GetStyle(element);
			edits.written = edits.captured;
			edits.declarations.clear();
			edits.armed = true;
		}

	} // namespace

	bool SetProperty(UI::Context& context, UI::ElementHandle element, StyleEdits& edits, StringHash name,
					 std::string_view value) {
		if (!context.GetElement(element)) {
			return false;
		}
		UI::Style probe;
		if (!UI::ApplyStyleDeclaration(probe, name, value, &context.GetStyleVars())) {
			return false;
		}
		if (NeedsRecapture(context, element, edits)) {
			Recapture(context, element, edits);
		}
		bool replaced = false;
		for (StyleEdits::Declaration& declaration : edits.declarations) {
			if (declaration.name == name) {
				declaration.value.assign(value);
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			edits.declarations.push_back(StyleEdits::Declaration{name, str(value)});
		}
		WriteSession(context, element, edits);
		return true;
	}

	bool CanRevert(const UI::Context& context, UI::ElementHandle element, const StyleEdits& edits, StringHash name) {
		return context.GetElement(element) && edits.armed && context.GetStyle(element) == edits.written && edits.IsEdited(name);
	}

	bool RevertProperty(UI::Context& context, UI::ElementHandle element, StyleEdits& edits, StringHash name) {
		if (!CanRevert(context, element, edits, name)) {
			return false;
		}
		for (size_t i = 0; i < edits.declarations.size(); ++i) {
			if (edits.declarations[i].name != name) {
				continue;
			}
			edits.declarations.erase(edits.declarations.begin() + static_cast<ptrdiff_t>(i));
			WriteSession(context, element, edits);
			return true;
		}
		return false;
	}

	void SetClasses(UI::Context& context, UI::ElementHandle element, std::string_view text, NameDictionary& names) {
		const UI::Element* pod = context.GetElement(element);
		if (!pod) {
			return;
		}
		StringHash wanted[UI::MAX_ELEMENT_CLASSES];
		u32 wantedCount = 0;
		size_t cursor = 0;
		while (cursor < text.size() && wantedCount < UI::MAX_ELEMENT_CLASSES) {
			size_t begin = text.find_first_not_of(" \t", cursor);
			if (begin == std::string_view::npos) {
				break;
			}
			size_t end = text.find_first_of(" \t", begin);
			std::string_view name = text.substr(begin, end == std::string_view::npos ? end : end - begin);
			cursor = end == std::string_view::npos ? text.size() : end;
			if (!name.empty() && name.front() == '.') {
				name.remove_prefix(1);
			}
			if (name.empty()) {
				continue;
			}
			names.AddName(name);
			wanted[wantedCount++] = StringHash(name);
		}

		StringHash carried[UI::MAX_ELEMENT_CLASSES];
		const u32 carriedCount = ClassesOf(*pod, carried);
		for (u32 i = 0; i < carriedCount; ++i) {
			bool keep = false;
			for (u32 j = 0; j < wantedCount; ++j) {
				keep = keep || carried[i] == wanted[j];
			}
			if (!keep) {
				context.RemoveClass(element, carried[i]);
			}
		}
		for (u32 j = 0; j < wantedCount; ++j) {
			context.AddClass(element, wanted[j]);
		}
	}

} // namespace Shalltear::UIDebugOps
