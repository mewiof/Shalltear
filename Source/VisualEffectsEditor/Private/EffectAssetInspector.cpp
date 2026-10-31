#include "EffectAssetInspector.h"

#include "EffectAssetSettings.h"
#include "GraphWindowParts.h"
#include "NodeShaderDocuments.h"
#include "EffectGraphWindow.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/GraphDocument.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include "Shalltear/VisualEffects/VisualEffectAsset.h"

namespace Shalltear::VisualEffectsEditor::EffectAssetInspector {

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		struct Held {
			const void* owner = nullptr;
			Guid guid{};
			bool open = false;
			u32 depth = 0;
		};
		Held* _held = nullptr;

		Held& State() {
			if (!_held) {
				_held = New<Held>(MemTag::Editor);
			}
			return *_held;
		}

		void Persist(const Guid& guid) {
			if (!EffectGraphWindow::HoldsGraph(guid) && NodeShaderDocuments::HasUnsavedChanges(guid) && !NodeShaderDocuments::Save(guid)) {
				SLT_LOG_WARN("VisualEffects", "the effect graph's file couldn't be written (edit stays unsaved)");
			}
		}

		void Changed(const Guid& guid) { Persist(guid); }

		void Let(Held& held) {
			if (held.open) {
				NodeShaderDocuments::Close(held.guid);
			}
			held.open = false;
			held.depth = 0;
			held.guid = {};
		}

		void Draw(UI::Context& context, UI::ElementHandle parent, const Guid& guid, Asset&) {
			Held& held = State();
			if (held.open && held.guid != guid) {
				Let(held);
			}
			if (!held.open) {
				str error;
				if (!NodeShaderDocuments::Open(guid, held.owner, error)) {
					GraphWindowParts::AddText(context, parent, ColorToken::WidgetTextDim, MetricToken::FontRow, error);
					return;
				}
				held.guid = guid;
				held.open = true;
			}
			const GraphDocument* document = NodeShaderDocuments::Get(guid);
			if (!document) {
				return;
			}
			GraphWindowParts::Editor editor;
			editor.edit = [guid](std::string_view label, const std::function<bool(GraphDocument&)>& op) {
				GraphDocument* edited = NodeShaderDocuments::Get(guid);
				if (!edited) {
					return false;
				}
				NodeShaderDocuments::BeginEdit(guid, label);
				const bool changed = op(*edited);
				NodeShaderDocuments::EndEdit(guid);
				if (changed && State().depth == 0) {
					Changed(guid);
				}
				return changed;
			};
			editor.begin = [guid](std::string_view label) {
				++State().depth;
				NodeShaderDocuments::BeginEdit(guid, label);
			};
			editor.end = [guid]() {
				NodeShaderDocuments::EndEdit(guid);
				Held& state = State();
				state.depth = state.depth > 0 ? state.depth - 1 : 0;
				if (state.depth == 0) {
					Changed(guid);
				}
			};
			editor.document = [guid]() { return NodeShaderDocuments::Get(guid); };
			EffectAssetSettings::Build(context, parent, *document, editor);

			const std::function<void()> rebuild = PropertyDrawers::GetInspectorRebuilder();
			const u64 drawn = NodeShaderDocuments::GetRevision(guid);
			const UI::ElementHandle follower = context.CreateElement();
			context.AppendChild(parent, follower);
			UI::Style hidden;
			hidden.display = UI::Display::None;
			context.SetStyle(follower, hidden);
			PropertyDrawers::SetRowRefresh(
				context, follower,
				[guid, drawn, rebuild](UI::Context&) {
					if (State().depth == 0 && NodeShaderDocuments::GetRevision(guid) != drawn) {
						rebuild();
					}
				},
				held.owner);
		}

	} // anonymous namespace

	void Register(const void* owner) {
		State().owner = owner;
		if (!PropertyDrawers::RegisterAssetInspector(VisualEffectAsset::GetReflectType()->name, &Draw, owner)) {
			SLT_LOG_ERROR("VisualEffects", "couldn't register the effect asset's Inspector");
		}
	}

	void Tick() {
		if (_held && _held->open && _held->depth == 0) {
			Persist(_held->guid);
		}
	}

	void Release() {
		if (_held) {
			Let(*_held);
			Delete(_held);
			_held = nullptr;
		}
	}

} // namespace Shalltear::VisualEffectsEditor::EffectAssetInspector
