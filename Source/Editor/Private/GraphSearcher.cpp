#include "GraphSearcher.h"

#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <algorithm>
#include <format>
#include <memory>

namespace Shalltear::GraphSearcher {

	using EditorTheme::ColorToken;
	using EditorTheme::MetricToken;

	namespace {

		constexpr u32 ROW_CAP = 100;

		struct Row {
			bool category = false;
			u32 entry = 0;
			str path;
			u32 depth = 0;
			bool expanded = false;
		};

		struct State {
			UI::Context* context = nullptr;
			UI::ElementHandle card;
			UI::ElementHandle field;
			UI::ElementHandle scroller;
			UI::ElementHandle list;
			Vector<GraphSearchEntry> library{GetMemoryResource(MemTag::Editor)};
			std::function<void(const GraphSearchEntry&)> onPick;
			str query;
			Vector<str> expanded{GetMemoryResource(MemTag::Editor)};
			Vector<Row> rows{GetMemoryResource(MemTag::Editor)};
			Vector<UI::ElementHandle> rowElements{GetMemoryResource(MemTag::Editor)};
			i32 cursor = -1;
		};

		void LightName(UI::Context& context, const UI::Widgets::TreeRowParts& parts, std::string_view query,
					   std::string_view name, Handle<FontAsset> font, f32 size) {
			Vector<GraphSearchSpan> spans{GetMemoryResource(MemTag::Editor)};
			GraphSearchOps::Highlights(query, name, spans);
			if (spans.empty()) {
				return;
			}
			context.SetTextString(parts.labelElement, name.substr(0, spans.front().begin));
			const UI::ElementHandle row = parts.row;
			const Color plain = EditorTheme::GetColor(ColorToken::WidgetText);
			const Color lit = EditorTheme::GetColor(ColorToken::GraphSearchMatch);
			auto piece = [&](u32 begin, u32 end, Color color) {
				if (end <= begin) {
					return;
				}
				UI::ElementHandle element = context.CreateElement();
				context.AppendChild(row, element);
				context.SetHoverable(element, false);
				UI::Style style;
				style.flexShrink = 0.0f;
				context.SetStyle(element, style);
				context.SetText(element, font, size, color, name.substr(begin, end - begin));
			};
			u32 at = spans.front().begin;
			for (const GraphSearchSpan& span : spans) {
				piece(at, span.begin, plain);
				piece(span.begin, span.end, lit);
				at = span.end;
			}
			piece(at, static_cast<u32>(name.size()), plain);
		}

		bool IsExpanded(const State& state, std::string_view path) {
			return std::find(state.expanded.begin(), state.expanded.end(), path) != state.expanded.end();
		}

		std::string_view Segment(std::string_view path, u32 depth) {
			for (u32 level = 0; level < depth; ++level) {
				const size_t slash = path.find('/');
				if (slash == std::string_view::npos) {
					return {};
				}
				path.remove_prefix(slash + 1);
			}
			const size_t slash = path.find('/');
			return slash == std::string_view::npos ? path : path.substr(0, slash);
		}

		std::string_view Prefix(std::string_view path, u32 depth) {
			size_t at = 0;
			for (u32 level = 0; level < depth; ++level) {
				const size_t slash = path.find('/', at);
				if (slash == std::string_view::npos) {
					return path;
				}
				at = slash + 1;
			}
			return path.substr(0, at == 0 ? 0 : at - 1);
		}

		u32 Depth(std::string_view path) {
			return path.empty() ? 0u : static_cast<u32>(std::count(path.begin(), path.end(), '/')) + 1u;
		}

		void AppendTree(State& state, std::string_view parent, u32 depth) {
			Vector<str> children{GetMemoryResource(MemTag::Editor)};
			for (const GraphSearchEntry& entry : state.library) {
				const std::string_view category = entry.category;
				if (Depth(category) <= depth || Prefix(category, depth) != parent) {
					continue;
				}
				const str child(Prefix(category, depth + 1));
				if (std::find(children.begin(), children.end(), child) == children.end()) {
					children.push_back(child);
				}
			}
			for (const str& child : children) {
				Row row;
				row.category = true;
				row.path = child;
				row.depth = depth;
				row.expanded = IsExpanded(state, child);
				state.rows.push_back(row);
				if (row.expanded) {
					AppendTree(state, child, depth + 1);
				}
			}
			for (u32 index = 0; index < static_cast<u32>(state.library.size()); ++index) {
				if (state.library[index].category == parent) {
					Row row;
					row.entry = index;
					row.depth = depth;
					state.rows.push_back(row);
				}
			}
		}

		void Rebuild(const std::shared_ptr<State>& state, std::string_view keep = {});

		void Pick(const std::shared_ptr<State>& state, u32 entry) {
			if (entry >= state->library.size()) {
				return;
			}
			const GraphSearchEntry picked = state->library[entry];
			const std::function<void(const GraphSearchEntry&)> onPick = state->onPick;
			state->context->ClosePopup(state->card);
			if (onPick) {
				onPick(picked);
			}
		}

		void Toggle(const std::shared_ptr<State>& state, const str& path) {
			auto found = std::find(state->expanded.begin(), state->expanded.end(), path);
			if (found == state->expanded.end()) {
				state->expanded.push_back(path);
			} else {
				state->expanded.erase(found);
			}
			Rebuild(state, path);
		}

		void ShowCursor(const std::shared_ptr<State>& state) {
			UI::Context& context = *state->context;
			if (!context.IsAlive(state->list)) {
				return;
			}
			const u64 tag = state->cursor >= 0 ? static_cast<u64>(state->cursor) + 1u : 0u;
			UI::Widgets::SetTreeSelection(context, state->list, std::span<const u64>(&tag, tag != 0 ? 1 : 0));
			if (state->cursor >= 0 && static_cast<size_t>(state->cursor) < state->rowElements.size()) {
				context.ScrollIntoView(state->rowElements[static_cast<size_t>(state->cursor)]);
			}
		}

		void Rebuild(const std::shared_ptr<State>& state, std::string_view keep) {
			UI::Context& context = *state->context;
			if (!context.IsAlive(state->scroller)) {
				return;
			}
			if (context.IsAlive(state->list)) {
				UI::Style gone;
				gone.display = UI::Display::None;
				context.SetStyle(state->list, gone);
				context.DestroyElementNextUpdate(state->list);
			}
			state->list = context.CreateElement();
			context.AppendChild(state->scroller, state->list);
			UI::Style listStyle;
			listStyle.flexDirection = UI::FlexDirection::Column;
			listStyle.width = UI::Length::Percent(1.0f);
			context.SetStyle(state->list, listStyle);

			state->rows.clear();
			state->rowElements.clear();
			u32 withheld = 0;
			const bool browsing = state->query.empty();
			if (browsing) {
				AppendTree(*state, {}, 0);
			} else {
				Vector<u32> ranked{GetMemoryResource(MemTag::Editor)};
				GraphSearchOps::Rank(state->query, state->library, ranked);
				if (!ranked.empty()) {
					const str completion = GraphSearchOps::Completion(state->query, state->library[ranked.front()].name);
					context.SetTextInputCompletion(state->field, completion, EditorTheme::GetColor(ColorToken::GraphSearchCompletion));
				}
				for (const u32 index : ranked) {
					if (state->rows.size() >= ROW_CAP) {
						++withheld;
						continue;
					}
					Row row;
					row.entry = index;
					state->rows.push_back(row);
				}
			}

			const Handle<FontAsset> font = UISystem::GetDefaultFont();
			const f32 size = EditorTheme::GetMetric(MetricToken::FontRow);
			const Color dim = EditorTheme::GetColor(ColorToken::TextDim);
			for (size_t index = 0; index < state->rows.size(); ++index) {
				const Row& row = state->rows[index];
				UI::Widgets::TreeRowDesc desc;
				desc.depth = row.depth;
				desc.hasChildren = row.category;
				desc.expanded = row.expanded;
				desc.font = font;
				desc.fontSizePx = size;
				desc.userTag = static_cast<u64>(index) + 1u;
				std::string_view label;
				if (row.category) {
					label = Segment(row.path, row.depth);
					desc.onSelect = [state, path = row.path](KeyMod) { Toggle(state, path); };
					desc.onToggleExpand = [state, path = row.path]() { Toggle(state, path); };
				} else {
					label = state->library[row.entry].name;
					desc.onSelect = [state, entry = row.entry](KeyMod) { Pick(state, entry); };
				}
				desc.label = label;
				UI::Widgets::TreeRowParts parts = UI::Widgets::TreeRow(context, state->list, desc);
				if (browsing) {
					EditorIcons::SetRowGutter(context, parts, row.category, row.expanded, dim);
				} else {
					LightName(context, parts, state->query, label, font, size);
				}
				if (!row.category) {
					const GraphSearchEntry& entry = state->library[row.entry];
					auto addDim = [&](std::string_view text) {
						UI::ElementHandle extra = context.CreateElement();
						context.AppendChild(parts.row, extra);
						context.SetHoverable(extra, false);
						UI::Style style;
						style.margin = {6.0f, 0.0f, 0.0f, 0.0f};
						style.flexShrink = 0.0f;
						context.SetStyle(extra, style);
						context.SetText(extra, font, size, dim, text);
					};
					if (!entry.detail.empty()) {
						addDim(entry.detail);
					}
					if (!browsing && !entry.category.empty()) {
						addDim(entry.category);
					}
				}
				state->rowElements.push_back(parts.row);
			}
			if (withheld > 0) {
				UI::ElementHandle more = context.CreateElement();
				context.AppendChild(state->list, more);
				context.SetHoverable(more, false);
				context.SetText(more, font, size, dim, std::format("... {} more (keep typing to narrow)", withheld));
			}
			if (state->rows.empty()) {
				UI::ElementHandle none = context.CreateElement();
				context.AppendChild(state->list, none);
				context.SetHoverable(none, false);
				context.SetText(none, font, size, dim, browsing ? "(nothing to create here)" : "(no matches)");
			}

			state->cursor = state->rows.empty() ? -1 : 0;
			if (!keep.empty()) {
				for (size_t index = 0; index < state->rows.size(); ++index) {
					if (state->rows[index].category && state->rows[index].path == keep) {
						state->cursor = static_cast<i32>(index);
					}
				}
				const u64 tag = state->cursor >= 0 ? static_cast<u64>(state->cursor) + 1u : 0u;
				UI::Widgets::SetTreeSelection(context, state->list, std::span<const u64>(&tag, tag != 0 ? 1 : 0));
				return;
			}
			context.SetScrollOffset(state->scroller, Vec2{});
			ShowCursor(state);
		}

		void MoveCursor(const std::shared_ptr<State>& state, i32 step) {
			if (state->rows.empty()) {
				return;
			}
			const i32 last = static_cast<i32>(state->rows.size()) - 1;
			state->cursor = std::clamp(state->cursor + step, 0, last);
			ShowCursor(state);
		}

		bool OnKey(const std::shared_ptr<State>& state, Key key) {
			const bool onRow = state->cursor >= 0 && static_cast<size_t>(state->cursor) < state->rows.size();
			switch (key) {
			case Key::Escape:
				state->context->ClosePopup(state->card);
				return true;
			case Key::Up:
				MoveCursor(state, -1);
				return true;
			case Key::Down:
				MoveCursor(state, 1);
				return true;
			case Key::Enter:
			case Key::KpEnter:
				if (onRow) {
					const Row row = state->rows[static_cast<size_t>(state->cursor)];
					if (row.category) {
						Toggle(state, row.path);
					} else {
						Pick(state, row.entry);
					}
				}
				return true;
			case Key::Right:
			case Key::Left: {
				if (!state->query.empty() || !onRow) {
					return false;
				}
				const Row row = state->rows[static_cast<size_t>(state->cursor)];
				if (row.category && row.expanded != (key == Key::Right)) {
					Toggle(state, row.path);
				}
				return true;
			}
			default:
				return false;
			}
		}

	} // anonymous namespace

	UI::ElementHandle Open(UI::Context& context, Desc desc) {
		auto state = std::make_shared<State>();
		state->context = &context;
		state->library = std::move(desc.library);
		state->onPick = std::move(desc.onPick);

		state->card = context.CreateElement();
		UI::Style cardStyle;
		cardStyle.flexDirection = UI::FlexDirection::Column;
		cardStyle.minWidth = UI::Length::Pixel(260.0f);
		context.SetStyle(state->card, cardStyle);
		context.AddClass(state->card, EditorTheme::Class::PopupCardCompact);
		UI::Context* contextPtr = &context;
		const UI::ElementHandle card = state->card;

		UI::Widgets::TextFieldDesc search;
		search.font = UISystem::GetDefaultFont();
		search.fontSizePx = EditorTheme::GetMetric(MetricToken::FontRow);
		state->field = UI::Widgets::TextField(context, state->card, search);
		UI::ElementEvents& fieldEvents = context.Events(state->field);
		fieldEvents.onTextFilter = [contextPtr, field = state->field](char32_t c) {
			return c != U' ' || !contextPtr->GetTextString(field).empty();
		};
		fieldEvents.onTextChange = [state](std::string_view text) {
			state->query = str(text);
			Rebuild(state);
		};
		fieldEvents.onKeyDown = [state](Key key, KeyMod) { return OnKey(state, key); };

		state->scroller = context.CreateElement();
		context.AppendChild(state->card, state->scroller);
		UI::Style scrollStyle;
		scrollStyle.flexDirection = UI::FlexDirection::Column;
		scrollStyle.maxHeight = UI::Length::Pixel(320.0f);
		scrollStyle.overflow = UI::Overflow::Scroll;
		scrollStyle.margin = {0.0f, 4.0f, 0.0f, 0.0f};
		context.SetStyle(state->scroller, scrollStyle);

		const UI::ElementHandle previous = context.GetFocused();
		context.Events(card).onPopupClosed = [contextPtr, card, previous]() {
			contextPtr->DestroyElementNextUpdate(card);
			if (contextPtr->IsAlive(previous)) {
				contextPtr->SetFocus(previous);
			}
		};

		Rebuild(state);
		context.OpenPopup(card, desc.at);
		context.SetFocus(state->field);
		return card;
	}

} // namespace Shalltear::GraphSearcher
