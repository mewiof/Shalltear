#include "AnimationEditor.h"
#include "ModelAnimationSettings.h"

#include "Shalltear/Animation/RetargetProfile.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include <array>
#include <functional>

namespace Shalltear::AnimationEditor {

	namespace {

		using namespace UI;
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		struct Draft {
			Guid asset;
			ModelAnimationSettings settings;
		};
		Draft* _draft = nullptr;

		Draft& DraftFor(const Guid& guid, const Path& modelPath) {
			if (!_draft) {
				_draft = New<Draft>(MemTag::Editor);
			}
			if (_draft->asset != guid) {
				_draft->asset = guid;
				_draft->settings = LoadModelAnimationSettings(modelPath);
			}
			return *_draft;
		}

		ElementHandle AddLine(Context& context, ElementHandle parent, ColorToken color, MetricToken size, std::string_view text) {
			ElementHandle line = context.CreateElement();
			context.AppendChild(parent, line);
			context.SetText(line, UISystem::GetDefaultFont(), EditorTheme::GetMetric(size),
							EditorTheme::GetColor(color), text);
			return line;
		}

		ElementHandle AddRow(Context& context, ElementHandle parent) {
			ElementHandle row = context.CreateElement();
			context.AppendChild(parent, row);
			Style style;
			style.flexDirection = FlexDirection::Row;
			style.width = Length::Percent(1.0f);
			style.margin = {0.0f, 2.0f, 0.0f, 2.0f};
			context.SetStyle(row, style);
			return row;
		}

		ElementHandle AddLabel(Context& context, ElementHandle parent, std::string_view text, f32 widthPx) {
			ElementHandle label = context.CreateElement();
			context.AppendChild(parent, label);
			Style style;
			style.width = Length::Pixel(widthPx);
			style.flexShrink = 0.0f;
			context.SetStyle(label, style);
			context.SetText(label, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontBody),
							EditorTheme::GetColor(ColorToken::WidgetTextDim), text);
			return label;
		}

		bool AppliesToAnimatedModel(const Guid& guid, Asset& asset) {
			(void)asset;
			Vector<AssetProduct> products{GetMemoryResource(MemTag::Editor)};
			AssetDatabase::GetProducts(guid, products);
			const StringHash clipType = AnimationClip::GetReflectType()->name;
			const StringHash skeletonType = SkeletonAsset::GetReflectType()->name;
			for (const AssetProduct& product : products) {
				if (product.type == clipType || product.type == skeletonType) {
					return true;
				}
			}
			return false;
		}

		void DrawClipRow(Context& context, ElementHandle parent, Draft& draft, size_t index, bool canMirror,
						 const std::function<void()>& rebuild) {
			ModelClipRange& range = draft.settings.clips[index];

			ElementHandle row = AddRow(context, parent);

			Widgets::TextFieldDesc name;
			name.text = range.name;
			name.font = UISystem::GetDefaultFont();
			name.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
			name.onCommit = [index](std::string_view text) {
				if (_draft && index < _draft->settings.clips.size()) {
					_draft->settings.clips[index].name = str(text);
				}
			};
			ElementHandle nameField = Widgets::TextField(context, row, name);
			{
				Style style;
				style.flexGrow = 1.0f;
				context.SetStyle(nameField, style);
			}

			AddLabel(context, row, "Start", 34.0f);
			Widgets::NumberFieldDesc start;
			start.value = range.start;
			start.font = UISystem::GetDefaultFont();
			start.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
			start.onChange = [index](f64 value) {
				if (_draft && index < _draft->settings.clips.size()) {
					_draft->settings.clips[index].start = static_cast<f32>(value);
				}
			};
			Widgets::NumberField(context, row, start);

			AddLabel(context, row, "End", 28.0f);
			Widgets::NumberFieldDesc end;
			end.value = range.end;
			end.font = UISystem::GetDefaultFont();
			end.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
			end.onChange = [index](f64 value) {
				if (_draft && index < _draft->settings.clips.size()) {
					_draft->settings.clips[index].end = static_cast<f32>(value);
				}
			};
			Widgets::NumberField(context, row, end);

			Widgets::ButtonDesc remove;
			remove.label = "-";
			remove.font = UISystem::GetDefaultFont();
			remove.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
			remove.onClick = [index, rebuild]() {
				if (_draft && index < _draft->settings.clips.size()) {
					_draft->settings.clips.erase(_draft->settings.clips.begin() + static_cast<i64>(index));
				}
				rebuild();
			};
			Widgets::Button(context, row, remove);

			ElementHandle properties = AddRow(context, parent);
			{
				Style style;
				style.padding = {14.0f, 0.0f, 0.0f, 0.0f};
				context.SetStyle(properties, style);
			}
			AddLabel(context, properties, "Loop", 34.0f);

			static constexpr std::array<std::string_view, 3> LOOPS{"None", "Loop", "PingPong"};
			Widgets::DropdownDesc loop;
			loop.items = std::span<const std::string_view>(LOOPS.data(), LOOPS.size());
			loop.selected = static_cast<u32>(range.loop);
			loop.font = UISystem::GetDefaultFont();
			loop.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
			loop.onSelect = [index](u32 value) {
				if (_draft && index < _draft->settings.clips.size()) {
					_draft->settings.clips[index].loop = static_cast<AnimationLoopMode>(value);
				}
			};
			Widgets::Dropdown(context, properties, loop);

			if (canMirror) {
				AddLabel(context, properties, "Mirror", 44.0f);
				Widgets::Checkbox(context, properties, range.mirror, [index](bool value) {
					if (_draft && index < _draft->settings.clips.size()) {
						_draft->settings.clips[index].mirror = value;
					}
				});
			}
		}

		void DrawAnimationSection(Context& context, ElementHandle parent, const Guid& guid, Asset& asset) {
			(void)asset;
			const Path modelPath = AssetDatabase::LookupPath(guid);
			Draft& draft = DraftFor(guid, modelPath);
			std::function<void()> rebuild = PropertyDrawers::GetInspectorRebuilder();

			Vector<AssetProduct> products{GetMemoryResource(MemTag::Editor)};
			AssetDatabase::GetProducts(guid, products);
			const StringHash clipType = AnimationClip::GetReflectType()->name;
			const StringHash profileType = RetargetProfile::GetReflectType()->name;
			u32 clipCount = 0;
			bool canMirror = false;
			for (const AssetProduct& product : products) {
				if (product.type == clipType) {
					++clipCount;
				} else if (product.type == profileType) {
					canMirror = true;
				}
			}

			if (draft.settings.clips.empty()) {
				AddLine(context, parent, ColorToken::WidgetTextDim, MetricToken::FontSmall,
						std::format("{} clip(s), one per take in the file. Add a range to split them.", clipCount));
			} else {
				AddLine(context, parent, ColorToken::WidgetTextDim, MetricToken::FontSmall,
						"Added ranges replace the file's takes. Times are in seconds.");
			}

			for (size_t i = 0; i < draft.settings.clips.size(); ++i) {
				DrawClipRow(context, parent, draft, i, canMirror, rebuild);
			}

			ElementHandle actions = AddRow(context, parent);

			Widgets::ButtonDesc add;
			add.label = "+";
			add.font = UISystem::GetDefaultFont();
			add.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
			add.onClick = [rebuild]() {
				if (!_draft) {
					return;
				}
				ModelClipRange range;
				const f32 begin = _draft->settings.clips.empty() ? 0.0f : _draft->settings.clips.back().end;
				range.name = std::format("Clip{}", _draft->settings.clips.size());
				range.start = begin;
				range.end = begin + 1.0f;
				_draft->settings.clips.push_back(std::move(range));
				rebuild();
			};
			Widgets::Button(context, actions, add);

			Widgets::ButtonDesc apply;
			apply.label = "Apply";
			apply.font = UISystem::GetDefaultFont();
			apply.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
			apply.onClick = [modelPath, rebuild]() {
				if (!_draft) {
					return;
				}
				rebuild();
				ApplyModelAnimationSettings(modelPath, _draft->settings);
				_draft->asset = Guid{};
			};
			Widgets::Button(context, actions, apply);

			Widgets::ButtonDesc revert;
			revert.label = "Revert";
			revert.font = UISystem::GetDefaultFont();
			revert.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
			revert.onClick = [rebuild]() {
				if (_draft) {
					_draft->asset = Guid{};
				}
				rebuild();
			};
			Widgets::Button(context, actions, revert);
		}

	} // anonymous namespace

	void RegisterModelAnimationInspector(const void* owner) {
		AssetInspectorSectionDesc desc;
		desc.label = "Animation";
		desc.order = 100;
		desc.applies = &AppliesToAnimatedModel;
		desc.draw = &DrawAnimationSection;
		if (!PropertyDrawers::RegisterAssetInspectorSection(StringHash("PrefabAsset"), std::move(desc), owner)) {
			SLT_LOG_ERROR("Animation", "couldn't register the Animation tab");
		}
	}

	void ShutdownModelAnimationInspector() {
		if (_draft) {
			Delete(_draft);
			_draft = nullptr;
		}
	}

} // namespace Shalltear::AnimationEditor
