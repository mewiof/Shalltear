#include "Shalltear/Editor/SceneViewEffects.h"

#include "Panels/SceneViewPanel.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Editor/PanelManager.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct Entry {
			str label;
			const void* owner = nullptr;
			Vector<StringHash> renderers{GetMemoryResource(MemTag::Editor)};
		};

		struct EffectsState {
			Vector<Entry> entries{GetMemoryResource(MemTag::Editor)};
		};

		EffectsState* _state = nullptr;

		EffectsState& State() {
			if (!_state) {
				_state = New<EffectsState>(MemTag::Editor);
			}
			return *_state;
		}

	} // anonymous namespace

	namespace SceneViewEffects {

		bool Register(std::string_view label, const void* owner, std::span<const StringHash> renderers) {
			if (!owner) {
				SLT_LOG_WARN("Editor", "ignoring Scene View effects category '{}': no owner to sweep it with", label);
				return false;
			}
			EffectsState& state = State();
			if (std::any_of(state.entries.begin(), state.entries.end(), [label](const Entry& entry) { return entry.label == label; })) {
				SLT_LOG_WARN("Editor", "ignoring Scene View effects category '{}': it's registered already", label);
				return false;
			}
			Entry& entry = state.entries.emplace_back();
			entry.label = str(label);
			entry.owner = owner;
			entry.renderers.assign(renderers.begin(), renderers.end());
			return true;
		}

		u32 UnregisterOwnedBy(const void* owner) {
			if (!_state || !owner) {
				return 0;
			}
			return static_cast<u32>(std::erase_if(_state->entries, [owner](const Entry& entry) { return entry.owner == owner; }));
		}

		void GetLabels(Vector<str>& out) {
			out.clear();
			if (_state) {
				for (const Entry& entry : _state->entries) {
					out.push_back(entry.label);
				}
			}
		}

		bool IsShown(std::string_view label) {
			if (!_state || std::none_of(_state->entries.begin(), _state->entries.end(), [label](const Entry& entry) { return entry.label == label; })) {
				return false;
			}
			Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
			PanelManager::GetOpenPanels(panels);
			for (const Handle<EditorPanel>& handle : panels) {
				EditorPanel* panel = handle.Get();
				if (panel && TypeRegistry::IsDerivedFrom(panel->GetType(), SceneViewPanel::GetReflectType()) &&
					static_cast<const SceneViewPanel*>(panel)->ShowsEffect(label)) {
					return true;
				}
			}
			return false;
		}

		void GetHiddenRenderers(const std::function<bool(std::string_view)>& shows, Vector<StringHash>& out) {
			out.clear();
			if (!_state) {
				return;
			}
			for (const Entry& entry : _state->entries) {
				if (!entry.renderers.empty() && !shows(entry.label)) {
					out.insert(out.end(), entry.renderers.begin(), entry.renderers.end());
				}
			}
		}

		void Terminate() {
			if (_state) {
				Delete(_state);
				_state = nullptr;
			}
		}

	} // namespace SceneViewEffects

} // namespace Shalltear
