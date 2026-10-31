#include "Shalltear/Editor/SceneViewOverlays.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		struct Entry {
			str title;
			StringHash component;
			SceneViewOverlays::BuildFn build;
			const void* owner = nullptr;
		};

		struct LiveCard {
			const void* view = nullptr;
			const void* owner = nullptr;
			SceneViewOverlays::RefreshFn refresh;
		};

		struct OverlaysState {
			Vector<Entry> entries{GetMemoryResource(MemTag::Editor)};
			Vector<LiveCard> cards{GetMemoryResource(MemTag::Editor)};
			u64 generation = 1;
		};

		OverlaysState* _state = nullptr;

		OverlaysState& State() {
			if (!_state) {
				_state = New<OverlaysState>(MemTag::Editor);
			}
			return *_state;
		}

		void Matching(std::span<const Handle<Object>> selection, const TypeInfo* type, Vector<Handle<Component>>& out) {
			out.clear();
			if (!type) {
				return;
			}
			auto take = [&](Component* component) {
				if (component && TypeRegistry::IsDerivedFrom(component->GetType(), type)) {
					out.push_back(Handle<Component>(component));
				}
			};
			for (const Handle<Object>& held : selection) {
				Object* object = held.Get();
				if (!object) {
					continue;
				}
				if (TypeRegistry::IsDerivedFrom(object->GetType(), SceneObject::GetReflectType())) {
					const SceneObject& owner = *static_cast<SceneObject*>(object);
					for (u32 index = 0; index < owner.GetComponentCount(); ++index) {
						take(owner.GetComponentAt(index).Get());
					}
				} else if (TypeRegistry::IsDerivedFrom(object->GetType(), Component::GetReflectType())) {
					take(static_cast<Component*>(object));
				}
			}
		}

		u64 Mix(u64 hash, u64 value) { return (hash ^ value) * 0x100000001b3ull; }

	} // anonymous namespace

	namespace SceneViewOverlays {

		bool Register(std::string_view title, StringHash component, BuildFn build, const void* owner) {
			if (!owner || !build) {
				SLT_LOG_WARN("Editor", "ignoring Scene View overlay '{}': no owner to sweep it with, or nothing to build", title);
				return false;
			}
			OverlaysState& state = State();
			if (std::any_of(state.entries.begin(), state.entries.end(), [title](const Entry& entry) { return entry.title == title; })) {
				SLT_LOG_WARN("Editor", "ignoring Scene View overlay '{}': it's registered already", title);
				return false;
			}
			state.entries.push_back({str(title), component, std::move(build), owner});
			++state.generation;
			return true;
		}

		u32 UnregisterOwnedBy(const void* owner) {
			if (!_state || !owner) {
				return 0;
			}
			u32 removed = static_cast<u32>(std::erase_if(_state->entries, [owner](const Entry& entry) { return entry.owner == owner; }));
			removed += static_cast<u32>(std::erase_if(_state->cards, [owner](const LiveCard& card) { return card.owner == owner; }));
			if (removed > 0) {
				++_state->generation;
			}
			return removed;
		}

		u64 KeyFor(std::span<const Handle<Object>> selection) {
			if (!_state) {
				return 0;
			}
			u64 key = Mix(0xcbf29ce484222325ull, _state->generation);
			Vector<Handle<Component>> matched{GetMemoryResource(MemTag::Editor)};
			for (u32 index = 0; index < _state->entries.size(); ++index) {
				Matching(selection, TypeRegistry::FindType(_state->entries[index].component), matched);
				for (const Handle<Component>& component : matched) {
					key = Mix(Mix(key, index + 1u), component.GetInstanceId());
				}
			}
			return key;
		}

		u32 Build(UI::Context& context, UI::ElementHandle parent, std::span<const Handle<Object>> selection, const void* view) {
			Release(view);
			if (!_state) {
				return 0;
			}
			const Vector<Entry> entries = _state->entries;
			Vector<Handle<Component>> matched{GetMemoryResource(MemTag::Editor)};
			u32 built = 0;
			for (const Entry& entry : entries) {
				Matching(selection, TypeRegistry::FindType(entry.component), matched);
				if (matched.empty()) {
					continue;
				}
				UI::ElementHandle card = context.CreateElement();
				context.AppendChild(parent, card);
				context.AddClass(card, EditorTheme::Class::SceneOverlayCard);
				UI::ElementHandle title = context.CreateElement();
				context.AppendChild(card, title);
				context.SetHoverable(title, false);
				context.SetText(title, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall), EditorTheme::GetColor(ColorToken::TextStrong),
								entry.title);
				UI::ElementHandle body = context.CreateElement();
				context.AppendChild(card, body);
				UI::Style column;
				column.flexDirection = UI::FlexDirection::Column;
				context.SetStyle(body, column);
				RefreshFn refresh = entry.build(context, body, {matched.data(), matched.size()});
				if (refresh && _state) {
					_state->cards.push_back({view, entry.owner, std::move(refresh)});
				}
				++built;
			}
			return built;
		}

		void Refresh(UI::Context& context, const void* view) {
			if (!_state) {
				return;
			}
			Vector<RefreshFn> refreshes{GetMemoryResource(MemTag::Editor)};
			for (const LiveCard& card : _state->cards) {
				if (card.view == view) {
					refreshes.push_back(card.refresh);
				}
			}
			for (const RefreshFn& refresh : refreshes) {
				refresh(context);
			}
		}

		void Release(const void* view) {
			if (_state) {
				std::erase_if(_state->cards, [view](const LiveCard& card) { return card.view == view; });
			}
		}

		void Terminate() {
			if (_state) {
				Delete(_state);
				_state = nullptr;
			}
		}

	} // namespace SceneViewOverlays

} // namespace Shalltear
