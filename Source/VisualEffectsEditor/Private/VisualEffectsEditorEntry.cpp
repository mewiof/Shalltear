#include "VisualEffectsEditor.h"

#include "EffectAsset.h"
#include "EffectControls.h"
#include "EffectAssetInspector.h"
#include "EffectLiveCompile.h"
#include "EffectPass.h"
#include "EffectGraphWindow.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorAssetWatch.h"
#include "Shalltear/Editor/EditorCommands.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/SceneAssetDrops.h"
#include "Shalltear/Editor/SceneViewEffects.h"
#include "Shalltear/Editor/SceneViewOverlays.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "Shalltear/VisualEffects/VisualEffect.h"
#include "Shalltear/VisualEffects/VisualEffectBinder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>

namespace Shalltear::VisualEffectsEditor {

	using namespace VisualEffectsImpl;
	using EditorTheme::ColorToken;
	using EditorTheme::MetricToken;

	namespace {

		constexpr f64 QUIET_SECONDS = 0.3;

		const char STAGE_TOKEN = 0;

		struct EditorState {
			bool staged = false;
			Vector<std::pair<Path, f64>> pending{GetMemoryResource(MemTag::Editor)};
			bool showBounds = true;
		};

		EditorState* _state = nullptr;

		EditorState& State() {
			if (!_state) {
				_state = New<EditorState>(MemTag::Editor);
			}
			return *_state;
		}

		void Reimport(const Path& path) {
			const Guid guid = AssetDatabase::LookupGuid(path);
			Asset* loaded = guid.IsValid() ? AssetDatabase::FindLoadedByGuid(guid) : nullptr;
			if (!loaded || !TypeRegistry::IsDerivedFrom(loaded->GetType(), VisualEffectAsset::GetReflectType())) {
				return;
			}
			IAssetImporter* importer = AssetImporterRegistry::FindByExtension(".sleffect");
			Asset* compiled = importer ? importer->ImportWithSettings(FileSystem::Resolve(path), path, JSONValue{}) : nullptr;
			if (!compiled) {
				SLT_LOG_WARN("VisualEffects", "'{}' didn't compile (its effects play on as last compiled)", path.ToString());
				return;
			}
			EffectAssetAccess::Replace(static_cast<VisualEffectAsset&>(*loaded), static_cast<VisualEffectAsset&>(*compiled));
			Delete(compiled);
			EffectLiveCompiles::Forget(guid);
			SLT_LOG_INFO("VisualEffects", "'{}' recompiled (effects playing it start over)", path.ToString());
		}

		str PathKey(std::string_view path) {
			str key = std::filesystem::path(path).lexically_normal().generic_string();
			std::transform(key.begin(), key.end(), key.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			return key;
		}

		void DependentsOf(const Path& changed, Vector<Path>& out) {
			const str key = PathKey(FileSystem::Resolve(changed).ToString());
			Vector<Handle<VisualEffect>> effects{GetMemoryResource(MemTag::Editor)};
			GetEffects(effects);
			for (const Handle<VisualEffect>& handle : effects) {
				const VisualEffect* effect = handle.Get();
				const VisualEffectAsset* asset = effect ? effect->asset.Get() : nullptr;
				if (!asset) {
					continue;
				}
				const Vector<EffectDependency>& read = EffectAssetAccess::Dependencies(*asset);
				if (std::none_of(read.begin(), read.end(), [&](const EffectDependency& dependency) { return PathKey(dependency.physicalPath) == key; })) {
					continue;
				}
				const Path source = AssetDatabase::LookupPath(asset->assetId);
				if (!source.ToString().empty() && std::find(out.begin(), out.end(), source) == out.end()) {
					out.push_back(source);
				}
			}
		}

		void HotReload() {
			EditorState& state = State();
			const f64 now = Time::GetRealtimeSinceStartup();
			const auto wait = [&](const Path& path) {
				auto held = std::find_if(state.pending.begin(), state.pending.end(), [&](const auto& entry) { return entry.first == path; });
				if (held != state.pending.end()) {
					held->second = now;
				} else {
					state.pending.push_back({path, now});
				}
			};
			Vector<Path> dependents{GetMemoryResource(MemTag::Editor)};
			for (const Path& changed : EditorAssetWatch::GetChangedThisFrame()) {
				if (changed.GetExtension() == ".sleffect") {
					wait(changed);
					continue;
				}
				dependents.clear();
				DependentsOf(changed, dependents);
				for (const Path& dependent : dependents) {
					if (!EffectGraphWindow::HoldsGraph(AssetDatabase::LookupGuid(dependent))) {
						wait(dependent);
					}
				}
			}
			Vector<Path> ready{GetMemoryResource(MemTag::Editor)};
			std::erase_if(state.pending, [&](const auto& entry) {
				if (now - entry.second < QUIET_SECONDS) {
					return false;
				}
				ready.push_back(entry.first);
				return true;
			});
			for (const Path& path : ready) {
				Reimport(path);
			}
		}

		void EndFrameStage(const FrameStageArgs&, void*) {
			HotReload();
			EffectLiveCompiles::Tick();
			EffectAssetInspector::Tick();
			if (PanelManager::GetPlayModeState() != PlayModeState::Editing) {
				return;
			}
			UpdateBinders(Time::GetUnscaledDeltaTime());
			if (SceneViewEffects::IsShown(EFFECTS_CATEGORY)) {
				AdvanceEffects(TickEffectClock(Time::GetUnscaledDeltaTime(), Time::GetUnscaledDeltaTime()));
			}
		}

		VisualEffect* Resolve(const EditTarget& target) {
			Object* object = target.Resolve();
			return object && TypeRegistry::IsDerivedFrom(object->GetType(), VisualEffect::GetReflectType()) ? static_cast<VisualEffect*>(object)
																											: nullptr;
		}

		const VisualEffectPropertyOverride* Find(const VisualEffect& effect, std::string_view property) {
			for (const VisualEffectPropertyOverride& entry : effect.properties) {
				if (entry.name == property) {
					return &entry;
				}
			}
			return nullptr;
		}

		Vec4 Default(const EffectProperty& property) {
			return Vec4(property.defaultValue[0], property.defaultValue[1], property.defaultValue[2], property.defaultValue[3]);
		}

		Vec4 Shown(const VisualEffect& effect, const EffectProperty& property) {
			const VisualEffectPropertyOverride* entry = Find(effect, property.name);
			return entry && entry->overridden ? entry->value : Default(property);
		}

		void Edit(const EditTarget& target, const EffectProperty& property, const std::function<void(VisualEffectPropertyOverride&)>& change) {
			VisualEffect* effect = Resolve(target);
			if (!effect) {
				return;
			}
			const StringHash field("properties");
			const str before = CaptureFieldState(*effect, field);
			auto entry = std::find_if(effect->properties.begin(), effect->properties.end(),
									  [&](const VisualEffectPropertyOverride& held) { return held.name == property.name; });
			if (entry == effect->properties.end()) {
				effect->properties.push_back({property.name, Default(property), false});
				entry = effect->properties.end() - 1;
			}
			change(*entry);
			const str after = CaptureFieldState(*effect, field);
			if (after != before) {
				UndoSystem::Push(MakeUnique<SetFieldStateCommand>(target, field, before, after, "Set Effect Property"));
			}
		}

		void SetValue(const EditTarget& target, const EffectProperty& property, const Vec4& value) {
			Edit(target, property, [value](VisualEffectPropertyOverride& entry) {
				entry.value = value;
				entry.overridden = true;
			});
		}

		struct PropertyLine {
			EffectProperty property;
			UI::Widgets::CheckboxParts tick;
			UI::Widgets::NumberFieldParts number;
			UI::Widgets::CheckboxParts flag;
			UI::Widgets::ColorFieldParts color;
			UI::Widgets::VectorFieldParts vector;
		};

		struct Sheet {
			EditTarget target;
			u64 asset = 0;
			u32 generation = 0;
			std::function<void()> rebuild;
			Vector<PropertyLine> lines{GetMemoryResource(MemTag::Editor)};
		};

		bool DrawnFrom(const Sheet& sheet, const VisualEffect& effect) {
			const VisualEffectAsset* asset = effect.asset.Get();
			return (asset ? asset->GetInstanceId() : 0u) == sheet.asset && (!asset || EffectAssetAccess::Generation(*asset) == sheet.generation);
		}

		void Grow(UI::Context& context, UI::ElementHandle element) {
			UI::Style style = context.GetStyle(element);
			style.flexGrow = 1.0f;
			context.SetStyle(element, style);
		}

		UI::ElementHandle AddText(UI::Context& context, UI::ElementHandle parent, std::string_view text, ColorToken color) {
			UI::ElementHandle element = context.CreateElement();
			context.AppendChild(parent, element);
			context.AddClass(element, EditorTheme::Class::FieldLabel);
			context.SetText(element, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontBody), EditorTheme::GetColor(color), text);
			context.SetHoverable(element, false);
			return element;
		}

		void Refresh(UI::Context& context, const Sheet& sheet) {
			const VisualEffect* effect = Resolve(sheet.target);
			if (!effect) {
				return;
			}
			if (!DrawnFrom(sheet, *effect)) {
				sheet.rebuild();
				return;
			}
			for (const PropertyLine& line : sheet.lines) {
				const VisualEffectPropertyOverride* entry = Find(*effect, line.property.name);
				UI::Widgets::SetCheckboxValue(context, line.tick, entry && entry->overridden);
				const Vec4 value = Shown(*effect, line.property);
				switch (line.property.type) {
				case EffectPropertyType::Float:
					UI::Widgets::SetNumberFieldValue(context, line.number, value.x, UI::Widgets::DEFAULT_DECIMALS);
					break;
				case EffectPropertyType::Integer:
					UI::Widgets::SetNumberFieldValue(context, line.number, value.x, 0);
					break;
				case EffectPropertyType::Boolean:
					UI::Widgets::SetCheckboxValue(context, line.flag, value.x != 0.0f);
					break;
				case EffectPropertyType::Color:
					UI::Widgets::SetColorSwatchFieldValue(context, line.color, Color(value.x, value.y, value.z, value.w));
					break;
				default: {
					const f64 components[4] = {value.x, value.y, value.z, value.w};
					UI::Widgets::SetVectorFieldValues(context, line.vector, line.property.components, components, UI::Widgets::DEFAULT_DECIMALS);
					break;
				}
				}
			}
		}

		PropertyLine AddLine(UI::Context& context, UI::ElementHandle list, const VisualEffect& effect, const EditTarget& target,
							 const EffectProperty& property) {
			PropertyLine line;
			line.property = property;
			UI::ElementHandle row = context.CreateElement();
			context.AppendChild(list, row);
			UI::Style rowStyle;
			rowStyle.flexDirection = UI::FlexDirection::Row;
			rowStyle.alignItems = UI::Align::Center;
			context.SetStyle(row, rowStyle);

			const VisualEffectPropertyOverride* held = Find(effect, property.name);
			line.tick = UI::Widgets::Checkbox(context, row, held && held->overridden, [target, property](bool on) {
				Edit(target, property, [on](VisualEffectPropertyOverride& entry) { entry.overridden = on; });
			});
			const UI::ElementHandle name = AddText(context, row, property.name, ColorToken::Text);
			UI::Style nameStyle = context.GetStyle(name);
			nameStyle.width = UI::Length::Percent(0.4f);
			nameStyle.flexShrink = 1.0f;
			nameStyle.margin.left = EditorTheme::GetMetric(MetricToken::IconGap);
			context.SetStyle(name, nameStyle);

			const Vec4 shown = Shown(effect, property);
			const Handle<FontAsset> font = UISystem::GetDefaultFont();
			const f32 size = EditorTheme::GetMetric(MetricToken::FontBody);
			switch (property.type) {
			case EffectPropertyType::Float:
			case EffectPropertyType::Integer: {
				const bool integral = property.type == EffectPropertyType::Integer;
				UI::Widgets::NumberFieldDesc desc;
				desc.value = shown.x;
				desc.decimals = integral ? 0 : UI::Widgets::DEFAULT_DECIMALS;
				desc.font = font;
				desc.fontSizePx = size;
				desc.onChange = [target, property, integral](f64 value) {
					SetValue(target, property, Vec4(static_cast<f32>(integral ? std::round(value) : value), 0.0f, 0.0f, 0.0f));
				};
				desc.onScrubBegin = []() { UndoSystem::BeginGroup("Set Effect Property"); };
				desc.onScrubEnd = []() { UndoSystem::EndGroup(); };
				line.number = UI::Widgets::NumberField(context, row, desc);
				Grow(context, line.number.root);
				break;
			}
			case EffectPropertyType::Boolean:
				line.flag = UI::Widgets::Checkbox(context, row, shown.x != 0.0f,
												  [target, property](bool value) { SetValue(target, property, Vec4(value ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f)); });
				break;
			case EffectPropertyType::Color:
				line.color = UI::Widgets::ColorSwatchField(
					context, row, Color(shown.x, shown.y, shown.z, shown.w), font, size,
					[target, property](Color value) { SetValue(target, property, Vec4(value.r, value.g, value.b, value.a)); },
					[]() { UndoSystem::BeginGroup("Set Effect Property"); }, []() { UndoSystem::EndGroup(); });
				Grow(context, line.color.root);
				break;
			default: {
				UI::Widgets::VectorFieldDesc desc;
				desc.componentCount = property.components;
				desc.values[0] = shown.x;
				desc.values[1] = shown.y;
				desc.values[2] = shown.z;
				desc.values[3] = shown.w;
				desc.font = font;
				desc.fontSizePx = size;
				desc.onChange = [target, property](u32 component, f64 value) {
					const VisualEffect* live = Resolve(target);
					if (!live) {
						return;
					}
					Vec4 next = Shown(*live, property);
					f32* fields[4] = {&next.x, &next.y, &next.z, &next.w};
					*fields[std::min(component, 3u)] = static_cast<f32>(value);
					SetValue(target, property, next);
				};
				desc.onEditBegin = []() { UndoSystem::BeginGroup("Set Effect Property"); };
				desc.onEditEnd = []() { UndoSystem::EndGroup(); };
				line.vector = UI::Widgets::VectorField(context, row, desc);
				Grow(context, line.vector.root);
				break;
			}
			}
			return line;
		}

		void DrawSheet(UI::Context& context, UI::ElementHandle row, const EditTarget& target, const void* owner) {
			const VisualEffect* effect = Resolve(target);
			if (!effect) {
				return;
			}
			UI::Style rowStyle = context.GetStyle(row);
			rowStyle.flexDirection = UI::FlexDirection::Column;
			rowStyle.alignItems = UI::Align::Stretch;
			context.SetStyle(row, rowStyle);
			UI::ElementHandle list = context.CreateElement();
			context.AppendChild(row, list);
			UI::Style listStyle;
			listStyle.flexDirection = UI::FlexDirection::Column;
			listStyle.padding.left = EditorTheme::GetMetric(MetricToken::IconGutter) + EditorTheme::GetMetric(MetricToken::IconGap);
			context.SetStyle(list, listStyle);

			auto sheet = std::make_shared<Sheet>();
			sheet->target = target;
			sheet->rebuild = PropertyDrawers::GetInspectorRebuilder();
			const VisualEffectAsset* asset = effect->asset.Get();
			sheet->asset = asset ? asset->GetInstanceId() : 0u;
			sheet->generation = asset ? EffectAssetAccess::Generation(*asset) : 0u;
			if (!asset) {
				AddText(context, list, "None", ColorToken::TextDim);
			} else if (EffectAssetAccess::Description(*asset).properties.empty()) {
				AddText(context, list, "The effect exposes no properties", ColorToken::TextDim);
			} else {
				for (const EffectProperty& property : EffectAssetAccess::Description(*asset).properties) {
					sheet->lines.push_back(AddLine(context, list, *effect, target, property));
				}
			}
			PropertyDrawers::SetRowRefresh(context, row, [sheet](UI::Context& refreshed) { Refresh(refreshed, *sheet); }, owner);
		}

		void DrawBounds(GizmoDrawContext& context, Handle<Component> component) {
			const Component* held = component.Get();
			if (!context.selected || !context.lines || !State().showBounds || !held ||
				!TypeRegistry::IsDerivedFrom(held->GetType(), VisualEffect::GetReflectType())) {
				return;
			}
			AABB box;
			if (EffectControls::WorldBounds(*static_cast<const VisualEffect*>(held), box)) {
				EditorViewportOps::AppendBoxWireframe(*context.lines, box, EditorTheme::GetColor32(ColorToken::GizmoEffectBounds));
			}
		}

		void DrawRecordedBounds(GizmoDrawContext& context) {
			if (!context.lines) {
				return;
			}
			Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
			PanelManager::GetOpenPanels(panels);
			for (const Handle<EditorPanel>& panel : panels) {
				EditorPanel* live = panel.Get();
				if (live && live->GetType() == EffectGraphWindow::GetReflectType()) {
					static_cast<EffectGraphWindow*>(live)->AppendRecordedBounds(*context.lines);
				}
			}
		}

		template <typename Fn>
		void ForEachWindow(Fn&& fn) {
			Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
			PanelManager::GetOpenPanels(panels);
			for (const Handle<EditorPanel>& panel : panels) {
				EditorPanel* live = panel.Get();
				if (live && live->GetType() == EffectGraphWindow::GetReflectType() && fn(*static_cast<EffectGraphWindow*>(live))) {
					return;
				}
			}
		}

		void DrawShapes(GizmoDrawContext& context) {
			if (!context.overlayLines) {
				return;
			}
			Vector<SceneHandle> unused{GetMemoryResource(MemTag::Editor)};
			ForEachWindow([&](EffectGraphWindow& window) {
				window.AppendShape(*context.overlayLines, unused);
				return false;
			});
		}

		void OfferShapes(Vector<SceneHandle>& out) {
			Vector<DebugLine> unused{GetMemoryResource(MemTag::Editor)};
			ForEachWindow([&](EffectGraphWindow& window) {
				window.AppendShape(unused, out);
				return false;
			});
		}

		void WriteShape(const SceneHandle& dragged) {
			ForEachWindow([&](EffectGraphWindow& window) { return window.WriteShape(dragged); });
		}

		SceneViewOverlays::RefreshFn BuildPlayControls(UI::Context& context, UI::ElementHandle body, std::span<const Handle<Component>> components) {
			Vector<Handle<VisualEffect>> selected{GetMemoryResource(MemTag::Editor)};
			for (const Handle<Component>& component : components) {
				selected.push_back(Handle<VisualEffect>(static_cast<VisualEffect*>(component.Get())));
			}
			const EffectControls::Targets targets = [selected](Vector<Handle<VisualEffect>>& out) { out.assign(selected.begin(), selected.end()); };
			const EffectControls::Refresh controls = EffectControls::BuildControls(context, body, targets);

			auto row = [&]() {
				UI::ElementHandle line = context.CreateElement();
				context.AppendChild(body, line);
				UI::Style style;
				style.flexDirection = UI::FlexDirection::Row;
				style.alignItems = UI::Align::Center;
				context.SetStyle(line, style);
				return line;
			};
			const UI::ElementHandle boundsRow = row();
			const UI::Widgets::CheckboxParts bounds = UI::Widgets::Checkbox(context, boundsRow, State().showBounds, [](bool on) { State().showBounds = on; });
			UI::ElementHandle label = AddText(context, boundsRow, "Show Bounds", ColorToken::Text);
			UI::Style gap = context.GetStyle(label);
			gap.margin.left = EditorTheme::GetMetric(MetricToken::IconGap);
			context.SetStyle(label, gap);

			const UI::ElementHandle events = row();
			for (const auto& [text, send] : {std::pair<std::string_view, void (VisualEffect::*)()>{"Play()", &VisualEffect::Play},
											 std::pair<std::string_view, void (VisualEffect::*)()>{"Stop()", &VisualEffect::Stop}}) {
				UI::Widgets::ButtonDesc desc;
				desc.label = text;
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				desc.onClick = [selected, send]() {
					for (const Handle<VisualEffect>& handle : selected) {
						if (VisualEffect* effect = handle.Get()) {
							(effect->*send)();
						}
					}
				};
				UI::Widgets::Button(context, events, desc);
			}
			return [controls, bounds](UI::Context& refreshed) {
				controls(refreshed);
				UI::Widgets::SetCheckboxValue(refreshed, bounds, State().showBounds);
			};
		}

	} // anonymous namespace

	void RegisterEditorExtensions(const void* owner) {
		const StringHash hidden[] = {VisualEffect::GetReflectType()->name};
		SceneViewEffects::Register(EFFECTS_CATEGORY, owner, hidden);
		SceneAssetDrops::Register(VisualEffectAsset::GetReflectType()->name, VisualEffect::GetReflectType()->name, StringHash("asset"), owner);
		EditorGizmos::RegisterComponentGizmo(VisualEffect::GetReflectType()->name, &DrawBounds, owner);
		EditorGizmos::RegisterSceneGizmo(StringHash("VisualEffects.RecordedBounds"), &DrawRecordedBounds, owner);
		EditorGizmos::RegisterSceneGizmo(StringHash("VisualEffects.BlockShape"), &DrawShapes, owner);
		EditorGizmos::RegisterSceneHandles(StringHash("VisualEffects.BlockShape"), &OfferShapes, &WriteShape, owner);
		EffectAssetInspector::Register(owner);
		SceneViewOverlays::Register("Play Controls", VisualEffect::GetReflectType()->name, &BuildPlayControls, owner);
		EffectGraphWindowRegistry::RegisterOpener(owner);
		PropertyDrawers::RegisterFieldDrawerFor(
			VisualEffect::GetReflectType()->name, StringHash("properties"),
			[owner](UI::Context& context, UI::ElementHandle row, FieldRef, EditTarget target) { DrawSheet(context, row, target, owner); }, owner);
		ComponentMenu::Register(VisualEffectBinder::GetReflectType()->name, "Visual Effects", owner);
		EditorState& state = State();
		if (!state.staged) {
			state.staged = FrameStages::Register(FrameStage::EndFrame, FrameStagePosition::Early, &EndFrameStage, nullptr, &STAGE_TOKEN);
		}
	}

	void ShutdownEditor() {
		FrameStages::UnregisterOwnedBy(&STAGE_TOKEN);
		EffectAssetInspector::Release();
		EffectLiveCompiles::Terminate();
		if (_state) {
			Delete(_state);
			_state = nullptr;
		}
	}

} // namespace Shalltear::VisualEffectsEditor

SLT_IMPLEMENT_EDITOR_EXTENSIONS(&Shalltear::VisualEffectsEditor::RegisterEditorExtensions)
