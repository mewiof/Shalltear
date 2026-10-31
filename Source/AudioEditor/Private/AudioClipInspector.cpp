#include "AudioEditor.h"

#include "AudioClipAccess.h"
#include "AudioClipData.h"
#include "AudioImportOps.h"

#include "Shalltear/Audio/AudioClip.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>

namespace Shalltear::AudioEditor {

	namespace {

		using namespace UI;
		using AudioImpl::AudioImportSettings;
		using AudioImpl::AudioSampleRateSetting;
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		struct Draft {
			Guid asset;
			AudioImportSettings settings;
			AudioImportSettings saved;
		};
		Draft* _draft = nullptr;

		AudioImportSettings ReadSaved(const Path& clipPath) {
			AudioImportSettings settings;
			const str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(clipPath)), MemTag::Editor);
			if (!text.empty()) {
				LinearAllocator arena(text.size() + 64, MemTag::Editor);
				const JSONValue root = JSONParse(text, arena);
				AudioImpl::ReadImportSettings(root["importer"]["settings"], settings);
			}
			return settings;
		}

		Draft& DraftFor(const Guid& guid, const Path& clipPath) {
			if (!_draft) {
				_draft = New<Draft>(MemTag::Editor);
			}
			if (_draft->asset != guid) {
				_draft->asset = guid;
				_draft->saved = ReadSaved(clipPath);
				_draft->settings = _draft->saved;
			}
			return *_draft;
		}

		bool Same(const AudioImportSettings& a, const AudioImportSettings& b) {
			return a.forceToMono == b.forceToMono && a.normalize == b.normalize && a.loadType == b.loadType &&
				   a.compressionFormat == b.compressionFormat && a.sampleRateSetting == b.sampleRateSetting &&
				   a.sampleRateOverride == b.sampleRateOverride && a.quality == b.quality;
		}

		bool Apply(const Path& clipPath, const AudioImportSettings& settings) {
			const str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(clipPath)), MemTag::Editor);
			LinearAllocator arena(text.size() + 64, MemTag::Editor);
			const JSONValue existing = text.empty() ? JSONValue() : JSONParse(text, arena);
			JSONValue merged = JSONValue::MakeObject();
			AudioImpl::MergeImportSettings(existing["importer"]["settings"], settings, merged);
			if (!AssetDatabase::WriteImporterSettings(clipPath, merged)) {
				return false;
			}
			if (!AssetDatabase::ReimportAsset(clipPath)) {
				SLT_LOG_WARN("Audio", "'{}': settings saved, the clip couldn't be re-imported. Close what uses it and re-import",
							 clipPath.ToString());
				return false;
			}
			return true;
		}

		f32 BodyPx() { return EditorTheme::GetMetric(MetricToken::FontBody); }

		ElementHandle AddRow(Context& context, ElementHandle parent) {
			ElementHandle row = context.CreateElement();
			context.AppendChild(parent, row);
			Style style;
			style.flexDirection = FlexDirection::Row;
			style.alignItems = Align::Center;
			style.width = Length::Percent(1.0f);
			style.padding = {8.0f, 0.0f, 8.0f, 0.0f};
			style.margin = {0.0f, 2.0f, 0.0f, 2.0f};
			context.SetStyle(row, style);
			return row;
		}

		ElementHandle AddLabeledRow(Context& context, ElementHandle parent, std::string_view text, bool indented = false) {
			ElementHandle row = AddRow(context, parent);
			ElementHandle label = context.CreateElement();
			context.AppendChild(row, label);
			Style style;
			style.width = Length::Pixel(140.0f);
			style.flexShrink = 0.0f;
			style.padding = {indented ? 14.0f : 0.0f, 0.0f, 0.0f, 0.0f};
			context.SetStyle(label, style);
			context.SetText(label, UISystem::GetDefaultFont(), BodyPx(), EditorTheme::GetColor(ColorToken::WidgetTextDim), text);
			return row;
		}

		ElementHandle AddLine(Context& context, ElementHandle parent, std::string_view text) {
			ElementHandle line = context.CreateElement();
			context.AppendChild(parent, line);
			Style style;
			style.margin = {8.0f, 6.0f, 8.0f, 2.0f};
			context.SetStyle(line, style);
			context.SetText(line, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
							EditorTheme::GetColor(ColorToken::WidgetTextDim), text);
			return line;
		}

		void AddDropdown(Context& context, ElementHandle row, std::span<const std::string_view> items, u32 selected, std::function<void(u32)> onSelect) {
			Widgets::DropdownDesc desc;
			desc.items = items;
			desc.selected = selected;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = BodyPx();
			desc.onSelect = std::move(onSelect);
			Widgets::Dropdown(context, row, desc);
		}

		str Bytes(u64 bytes) {
			if (bytes >= 1024ull * 1024ull) {
				return std::format("{:.1f} MB", static_cast<f64>(bytes) / (1024.0 * 1024.0));
			}
			if (bytes >= 1024ull) {
				return std::format("{:.1f} KB", static_cast<f64>(bytes) / 1024.0);
			}
			return std::format("{} B", bytes);
		}

		u64 ImportedBytes(const AudioClip& clip) {
			const AudioImpl::AudioClipData* cooked = AudioImpl::AudioClipAccess::GetCooked(clip);
			if (!cooked) {
				return 0;
			}
			const u8* bytes = nullptr;
			u64 size = 0;
			AudioImpl::GetPayload(*cooked, bytes, size);
			return bytes ? size : cooked->source.size;
		}

		void ShowActions(Context& context, const std::array<ElementHandle, 2>& actions) {
			const bool live = _draft && !Same(_draft->settings, _draft->saved);
			for (ElementHandle button : actions) {
				if (!context.IsAlive(button)) {
					continue;
				}
				if (live) {
					context.RemoveClass(button, EditorTheme::Class::Disabled);
				} else {
					context.AddClass(button, EditorTheme::Class::Disabled);
				}
				context.SetHoverable(button, live);
			}
		}

		void DrawClipInspector(Context& context, ElementHandle parent, const Guid& guid, Asset& asset) {
			const Path clipPath = AssetDatabase::LookupPath(guid);
			Draft& draft = DraftFor(guid, clipPath);
			const AudioImportSettings& shown = draft.settings;
			const std::function<void()> rebuild = PropertyDrawers::GetInspectorRebuilder();
			auto change = [rebuild](auto&& edit) {
				return [rebuild, edit](auto value) {
					if (_draft) {
						edit(_draft->settings, value);
					}
					rebuild();
				};
			};
			const auto actions = std::make_shared<std::array<ElementHandle, 2>>();
			Context* const contextPtr = &context;
			auto tweak = [actions, contextPtr](auto&& edit) {
				return [actions, contextPtr, edit](auto value) {
					if (_draft) {
						edit(_draft->settings, value);
					}
					ShowActions(*contextPtr, *actions);
				};
			};

			ElementHandle mono = AddLabeledRow(context, parent, "Force To Mono");
			Widgets::Checkbox(context, mono, shown.forceToMono, change([](AudioImportSettings& s, bool value) { s.forceToMono = value; }));
			if (shown.forceToMono) {
				ElementHandle normalize = AddLabeledRow(context, parent, "Normalize", true);
				Widgets::Checkbox(context, normalize, shown.normalize, change([](AudioImportSettings& s, bool value) { s.normalize = value; }));
			}

			static constexpr std::array<std::string_view, 3> LOAD_TYPES{"Decompress On Load", "Compressed In Memory", "Streaming"};
			AddDropdown(context, AddLabeledRow(context, parent, "Load Type"), LOAD_TYPES, static_cast<u32>(shown.loadType),
						change([](AudioImportSettings& s, u32 value) { s.loadType = static_cast<AudioClipLoadType>(value); }));

			static constexpr std::array<std::string_view, 3> FORMATS{"PCM", "QOA", "Opus"};
			AddDropdown(context, AddLabeledRow(context, parent, "Compression Format"), FORMATS, static_cast<u32>(shown.compressionFormat),
						change([](AudioImportSettings& s, u32 value) { s.compressionFormat = static_cast<AudioCompressionFormat>(value); }));

			if (shown.compressionFormat == AudioCompressionFormat::Opus) {
				ElementHandle quality = AddLabeledRow(context, parent, "Quality");
				Widgets::SliderDesc desc;
				desc.minValue = 0.0f;
				desc.maxValue = 100.0f;
				desc.value = shown.quality * 100.0f;
				desc.showValueField = true;
				desc.decimals = 0;
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = BodyPx();
				desc.onChange = tweak([](AudioImportSettings& s, f32 value) { s.quality = std::clamp(value / 100.0f, 0.0f, 1.0f); });
				Widgets::SliderParts slider = Widgets::Slider(context, quality, desc);
				Style grow = context.GetStyle(slider.root);
				grow.flexGrow = 1.0f;
				context.SetStyle(slider.root, grow);
			}

			static constexpr std::array<std::string_view, 3> RATE_SETTINGS{"Preserve Sample Rate", "Optimize Sample Rate", "Override Sample Rate"};
			AddDropdown(context, AddLabeledRow(context, parent, "Sample Rate Setting"), RATE_SETTINGS, static_cast<u32>(shown.sampleRateSetting),
						change([](AudioImportSettings& s, u32 value) { s.sampleRateSetting = static_cast<AudioSampleRateSetting>(value); }));
			if (shown.sampleRateSetting == AudioSampleRateSetting::OverrideSampleRate) {
				const std::span<const u32> rates = AudioImpl::GetStandardSampleRates();
				Vector<str> names{GetMemoryResource(MemTag::Editor)};
				Vector<std::string_view> views{GetMemoryResource(MemTag::Editor)};
				names.reserve(rates.size());
				u32 selected = 0;
				for (size_t index = 0; index < rates.size(); ++index) {
					names.push_back(std::format("{} Hz", rates[index]));
					selected = rates[index] == shown.sampleRateOverride ? static_cast<u32>(index) : selected;
				}
				for (const str& name : names) {
					views.push_back(name);
				}
				AddDropdown(context, AddLabeledRow(context, parent, "Sample Rate", true), views, selected,
							tweak([rates](AudioImportSettings& s, u32 value) {
								if (value < rates.size()) {
									s.sampleRateOverride = rates[value];
								}
							}));
			}

			const AudioClip& clip = static_cast<const AudioClip&>(asset);
			const u64 original = static_cast<u64>(FileSystem::GetFileSize(FileSystem::Resolve(clipPath)));
			const u64 imported = ImportedBytes(clip);
			const f64 ratio = original > 0 ? 100.0 * static_cast<f64>(imported) / static_cast<f64>(original) : 0.0;
			AddLine(context, parent, std::format("Original Size: {}   Imported Size: {}   Ratio: {:.2f}%", Bytes(original), Bytes(imported), ratio));

			ElementHandle row = AddRow(context, parent);
			Style end;
			end.flexDirection = FlexDirection::Row;
			end.justifyContent = Align::End;
			end.width = Length::Percent(1.0f);
			end.padding = {8.0f, 0.0f, 8.0f, 0.0f};
			end.margin = {0.0f, 6.0f, 0.0f, 2.0f};
			context.SetStyle(row, end);

			Widgets::ButtonDesc revert;
			revert.label = "Revert";
			revert.font = UISystem::GetDefaultFont();
			revert.fontSizePx = BodyPx();
			revert.onClick = [rebuild]() {
				if (!_draft || Same(_draft->settings, _draft->saved)) {
					return;
				}
				_draft->asset = Guid{};
				rebuild();
			};
			(*actions)[0] = Widgets::Button(context, row, revert);

			Widgets::ButtonDesc apply;
			apply.label = "Apply";
			apply.font = UISystem::GetDefaultFont();
			apply.fontSizePx = BodyPx();
			apply.onClick = [guid, clipPath, rebuild]() {
				if (!_draft || Same(_draft->settings, _draft->saved)) {
					return;
				}
				StopPreview();
				rebuild();
				const AudioImportSettings settings = _draft->settings;
				Apply(clipPath, settings);
				ForgetWaveform(guid);
				_draft->asset = Guid{};
			};
			(*actions)[1] = Widgets::Button(context, row, apply);
			ShowActions(context, *actions);
		}

	} // anonymous namespace

	void RegisterClipInspector(const void* owner) {
		if (!PropertyDrawers::RegisterAssetInspector(AudioClip::GetReflectType()->name, &DrawClipInspector, owner)) {
			SLT_LOG_ERROR("Audio", "couldn't register the clip inspector");
		}
	}

	void ReleaseClipInspector() {
		if (_draft) {
			Delete(_draft);
			_draft = nullptr;
		}
	}

} // namespace Shalltear::AudioEditor
