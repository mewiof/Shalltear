#include "AudioEditor.h"

#include "AudioEditorPreferences.h"
#include "AudioOutputs.h"
#include "AudioWaveform.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioClip.h"
#include "Shalltear/Audio/AudioContainer.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/SettingsPresentation.h"

#include <algorithm>
#include <format>
#include <memory>

namespace Shalltear::AudioEditor {

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		bool AutoPlay() {
			const AudioEditorPreferences* preferences = Preferences();
			return preferences && preferences->autoPlay;
		}

		bool Loop() {
			const AudioEditorPreferences* preferences = Preferences();
			return preferences && preferences->loop;
		}

		struct Preview {
			Guid guid;
			AudioVoice voice;
			Handle<Asset> held;
			u64 frames = 0;
		};

		Preview* _preview = nullptr;

		template <typename T>
		bool Is(const Asset* asset) {
			return asset && TypeRegistry::IsDerivedFrom(asset->GetType(), T::GetReflectType());
		}

		void SeekPreview(const Guid& guid, u64 sample) {
			if (IsPreviewing(guid)) {
				Audio::SetTimeSamples(_preview->voice, sample);
			} else {
				PlayPreview(guid, sample);
			}
		}

		f32 TextPx() { return EditorTheme::GetMetric(MetricToken::Font); }
		Color TextColor() { return EditorTheme::GetColor(ColorToken::Text); }

		void SetOn(UI::Context& context, UI::ElementHandle button, bool on) {
			if (!context.IsAlive(button)) {
				return;
			}
			if (on) {
				context.AddClass(button, EditorTheme::Class::ToolButtonOn);
			} else {
				context.RemoveClass(button, EditorTheme::Class::ToolButtonOn);
			}
		}

		EditorIcons::IconButtonParts ToolIcon(UI::Context& context, UI::ElementHandle header, EditorIcons::Icon icon, std::string_view word,
											  std::function<void()> onClick) {
			EditorIcons::IconButtonParts parts = EditorIcons::IconButton(context, header, icon, word, word, std::move(onClick), TextPx());
			context.AddClass(parts.button, EditorTheme::Class::ToolButton);
			context.AddClass(parts.button, EditorTheme::Class::IconButton);
			return parts;
		}

		struct Transport {
			UI::ElementHandle play;
			UI::ElementHandle playFace;
			UI::ElementHandle autoPlay;
			UI::ElementHandle loop;
			bool playing = false;
			bool autoPlayOn = false;
			bool loopOn = false;
		};

		std::shared_ptr<Transport> BuildPlay(UI::Context& context, UI::ElementHandle header, const Guid& guid) {
			auto transport = std::make_shared<Transport>();
			EditorIcons::IconButtonParts play = ToolIcon(context, header, EditorIcons::Glyph::Play, "Play", [guid]() {
				if (IsPreviewing(guid)) {
					StopPreview();
				} else {
					PlayPreview(guid, 0);
				}
			});
			transport->play = play.button;
			transport->playFace = play.face;
			return transport;
		}

		void RefreshPlay(UI::Context& context, Transport& transport, const Guid& guid) {
			const bool playing = IsPreviewing(guid);
			if (playing == transport.playing || !context.IsAlive(transport.playFace)) {
				return;
			}
			transport.playing = playing;
			EditorIcons::SetGlyphOrText(context, transport.playFace, playing ? EditorIcons::Glyph::Stop : EditorIcons::Glyph::Play,
										playing ? "Stop" : "Play", TextColor(), TextPx(), TextPx());
			context.SetTooltip(transport.play, playing ? "Stop" : "Play");
		}

		AssetPreviewPaneRefreshFn BuildClipTransport(UI::Context& context, UI::ElementHandle header, const Guid& guid) {
			std::shared_ptr<Transport> transport = BuildPlay(context, header, guid);
			UI::Context* contextPtr = &context;

			transport->autoPlayOn = AutoPlay();
			transport->autoPlay = ToolIcon(context, header, EditorIcons::Glyph::AutoPlay, "Auto Play", [contextPtr, transport]() {
									  if (AudioEditorPreferences* preferences = Preferences()) {
										  preferences->autoPlay = !preferences->autoPlay;
										  Settings::Save();
										  transport->autoPlayOn = preferences->autoPlay;
										  SetOn(*contextPtr, transport->autoPlay, preferences->autoPlay);
									  }
								  }).button;
			SetOn(context, transport->autoPlay, transport->autoPlayOn);

			transport->loopOn = Loop();
			transport->loop = ToolIcon(context, header, EditorIcons::Glyph::Loop, "Loop", [contextPtr, transport]() {
								  if (AudioEditorPreferences* preferences = Preferences()) {
									  preferences->loop = !preferences->loop;
									  Settings::Save();
									  transport->loopOn = preferences->loop;
									  SetOn(*contextPtr, transport->loop, preferences->loop);
									  if (_preview) {
										  Audio::SetLoop(_preview->voice, preferences->loop);
									  }
								  }
							  }).button;
			SetOn(context, transport->loop, transport->loopOn);

			return [transport, guid](UI::Context& refreshed) {
				RefreshPlay(refreshed, *transport, guid);
				if (AutoPlay() != transport->autoPlayOn) {
					transport->autoPlayOn = !transport->autoPlayOn;
					SetOn(refreshed, transport->autoPlay, transport->autoPlayOn);
				}
				if (Loop() != transport->loopOn) {
					transport->loopOn = !transport->loopOn;
					SetOn(refreshed, transport->loop, transport->loopOn);
				}
			};
		}

		AssetPreviewPaneRefreshFn BuildContainerTransport(UI::Context& context, UI::ElementHandle header, const Guid& guid, bool skip) {
			std::shared_ptr<Transport> transport = BuildPlay(context, header, guid);
			if (skip) {
				ToolIcon(context, header, EditorIcons::Glyph::Step, "Skip", [guid]() { PlayPreview(guid, 0); });
			}
			return [transport, guid](UI::Context& refreshed) { RefreshPlay(refreshed, *transport, guid); };
		}

		str Describe(const AudioClip& clip) {
			constexpr std::string_view FORMATS[] = {"PCM", "QOA", "Opus"};
			const size_t format = static_cast<size_t>(clip.GetCompressionFormat());
			const std::string_view formatName = format < std::size(FORMATS) ? FORMATS[format] : std::string_view("?");
			const u32 channels = clip.GetChannels();
			const str layout = channels == 1 ? str("Mono") : channels == 2 ? str("Stereo")
																		   : std::format("{} channels", channels);
			const u64 millis = static_cast<u64>(std::llround(static_cast<f64>(clip.GetLength()) * 1000.0));
			return std::format("{}, {} Hz, {}, {:02}:{:02}.{:03}", formatName, clip.GetFrequency(), layout, millis / 60000,
							   (millis / 1000) % 60, millis % 1000);
		}

		struct WaveformView {
			Guid guid;
			u64 frames = 0;
			Vector<f32> low{GetMemoryResource(MemTag::Editor)};
			Vector<f32> high{GetMemoryResource(MemTag::Editor)};
		};

		u64 FrameUnder(UI::Context& context, UI::ElementHandle wave, const WaveformView& view, f32 pointX) {
			const UI::Rect area = context.GetLayout(wave).content;
			if (area.w <= 0.0f || view.frames == 0) {
				return 0;
			}
			const f32 share = std::clamp((pointX - area.x) / area.w, 0.0f, 1.0f);
			return std::min(static_cast<u64>(static_cast<f64>(share) * static_cast<f64>(view.frames)), view.frames - 1);
		}

		void PaintWaveform(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint, WaveformView& view) {
			const UI::Rect area = context.GetLayout(element).content;
			if (area.w < 2.0f || area.h < 2.0f) {
				return;
			}
			const AudioImpl::AudioEnvelope* envelope = PeekWaveform(view.guid);
			const u32 channels = envelope && envelope->channels > 0 ? envelope->channels : 1;
			const f32 lane = area.h / static_cast<f32>(channels);
			const u32 columns = static_cast<u32>(area.w);
			view.low.resize(columns);
			view.high.resize(columns);
			const u32 read = envelope && envelope->frames > 0 ? static_cast<u32>(static_cast<u64>(columns) * envelope->framesRead / envelope->frames) : 0;
			const Color axis = EditorTheme::GetColor(ColorToken::GridLine);
			const Color ink = EditorTheme::GetColor(ColorToken::AudioWaveform);
			for (u32 channel = 0; channel < channels; ++channel) {
				const f32 mid = area.y + lane * (static_cast<f32>(channel) + 0.5f);
				paint.EmitQuad({area.x, mid, area.w, 1.0f}, axis);
				if (!envelope) {
					continue;
				}
				AudioImpl::GetEnvelopeColumns(*envelope, channel, columns, view.low.data(), view.high.data());
				for (u32 column = 0; column < read; ++column) {
					const f32 top = mid - view.high[column] * lane * 0.5f;
					const f32 bottom = mid - view.low[column] * lane * 0.5f;
					paint.EmitQuad({area.x + static_cast<f32>(column), top, 1.0f, std::max(1.0f, bottom - top)}, ink);
				}
			}
			const f32 progress = GetPreviewProgress(view.guid);
			if (progress >= 0.0f) {
				const f32 width = std::max(1.0f, context.GetContentScale());
				paint.EmitQuad({area.x + progress * area.w, area.y, width, area.h}, EditorTheme::GetColor(ColorToken::Accent));
			}
		}

		AssetPreviewPaneRefreshFn BuildWaveform(UI::Context& context, UI::ElementHandle well, const Guid& guid) {
			Asset* asset = AssetDatabase::LoadAssetByGuid(guid);
			const AudioClip* clip = Is<AudioClip>(asset) ? static_cast<const AudioClip*>(asset) : nullptr;
			auto view = std::make_shared<WaveformView>();
			view->guid = guid;
			view->frames = clip ? clip->GetSamples() : 0;
			const str line = clip ? Describe(*clip) : str();
			FindWaveform(guid, asset);
			if (asset) {
				AssetDatabase::UnloadAsset(asset);
			}

			UI::ElementHandle column = context.CreateElement();
			context.AppendChild(well, column);
			UI::Style columnStyle;
			columnStyle.flexDirection = UI::FlexDirection::Column;
			columnStyle.width = UI::Length::Percent(1.0f);
			columnStyle.height = UI::Length::Percent(1.0f);
			context.SetStyle(column, columnStyle);

			UI::ElementHandle info = context.CreateElement();
			context.AppendChild(column, info);
			context.AddClass(info, EditorTheme::Class::RowText);
			UI::Style infoStyle;
			infoStyle.padding = {6.0f, 3.0f, 6.0f, 1.0f};
			context.SetStyle(info, infoStyle);
			context.SetHoverable(info, false);
			context.SetText(info, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
							EditorTheme::GetColor(ColorToken::TextDim), line);

			UI::ElementHandle wave = context.CreateElement();
			context.AppendChild(column, wave);
			context.AddClass(wave, EditorTheme::Class::Clickable);
			UI::Style waveStyle;
			waveStyle.width = UI::Length::Percent(1.0f);
			waveStyle.flexGrow = 1.0f;
			context.SetStyle(wave, waveStyle);
			context.Events(wave).customDraw = [view](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				PaintWaveform(c, element, paint, *view);
			};
			UI::Context* contextPtr = &context;
			context.Events(wave).onMouseDown = [contextPtr, wave, view]() {
				SeekPreview(view->guid, FrameUnder(*contextPtr, wave, *view, contextPtr->GetPointerPosition().x));
			};
			context.Events(wave).onPressDrag = [contextPtr, wave, view](Vec2 pointPx) {
				SeekPreview(view->guid, FrameUnder(*contextPtr, wave, *view, pointPx.x));
			};
			return {};
		}

	} // anonymous namespace

	bool PlayPreview(const Guid& guid, u64 startSample) {
		StopPreview();
		const AudioBus output = AudioImpl::GetDeviceOutput();
		if (!output.IsValid()) {
			return false;
		}
		Asset* asset = AssetDatabase::LoadAssetByGuid(guid);
		if (!Is<AudioResource>(asset)) {
			if (asset) {
				AssetDatabase::UnloadAsset(asset);
			}
			return false;
		}
		AudioPlayParams params;
		params.output = output;
		params.loop = Loop();
		params.startSample = startSample;
		params.priority = 0;
		const AudioVoice voice = Audio::Play(Handle<AudioResource>(static_cast<AudioResource*>(asset)), params);
		if (!voice.IsValid()) {
			AssetDatabase::UnloadAsset(asset);
			return false;
		}
		if (!_preview) {
			_preview = New<Preview>(MemTag::Editor);
		}
		_preview->guid = guid;
		_preview->voice = voice;
		_preview->held = Handle<Asset>(asset);
		_preview->frames = Is<AudioClip>(asset) ? static_cast<const AudioClip*>(asset)->GetSamples() : 0;
		return true;
	}

	void StopPreview() {
		if (!_preview) {
			return;
		}
		Audio::Stop(_preview->voice);
		if (Asset* asset = _preview->held.Get()) {
			AssetDatabase::UnloadAsset(asset);
		}
		*_preview = Preview{};
	}

	bool IsPreviewing(const Guid& guid) {
		return _preview && guid.IsValid() && _preview->guid == guid && Audio::IsPlaying(_preview->voice);
	}

	f32 GetPreviewProgress(const Guid& guid) {
		if (!IsPreviewing(guid) || _preview->frames == 0) {
			return -1.0f;
		}
		const u64 at = Audio::GetTimeSamples(_preview->voice) % _preview->frames;
		return static_cast<f32>(static_cast<f64>(at) / static_cast<f64>(_preview->frames));
	}

	void UpdatePreview() {
		if (_preview && _preview->held.IsValid() && !Audio::IsPlaying(_preview->voice)) {
			StopPreview();
		}
	}

	void ReleasePreview() {
		StopPreview();
		if (_preview) {
			Delete(_preview);
			_preview = nullptr;
		}
	}

	void RegisterPreviewPanes(const void* owner) {
		SettingsFieldPresentation autoPlay;
		autoPlay.label = "Preview Auto Play";
		autoPlay.tooltip = "Play an audio clip as soon as it is selected.";
		SettingsPresentation::RegisterField(PREFERENCES, "autoPlay", std::move(autoPlay), owner);
		SettingsFieldPresentation loop;
		loop.label = "Preview Loop";
		loop.tooltip = "Loop an audio clip's preview.";
		SettingsPresentation::RegisterField(PREFERENCES, "loop", std::move(loop), owner);
		Preferences();

		const auto hide = [](const Guid& guid) {
			if (_preview && _preview->guid == guid) {
				StopPreview();
			}
		};

		AssetPreviewPaneDesc clip;
		clip.onShow = [](const Guid& guid) {
			if (AutoPlay()) {
				PlayPreview(guid, 0);
			}
		};
		clip.onHide = hide;
		clip.buildHeader = &BuildClipTransport;
		clip.buildBody = &BuildWaveform;
		EditorPreviews::RegisterPane(AudioClip::GetReflectType()->name, std::move(clip), owner);

		const TypeInfo* containers[] = {AudioRandomContainer::GetReflectType(), AudioLayeredContainer::GetReflectType(),
										AudioInteractiveContainer::GetReflectType()};
		for (const TypeInfo* type : containers) {
			const bool skip = type == AudioRandomContainer::GetReflectType();
			AssetPreviewPaneDesc container;
			container.onHide = hide;
			container.buildHeader = [skip](UI::Context& context, UI::ElementHandle header, const Guid& guid) {
				return BuildContainerTransport(context, header, guid, skip);
			};
			EditorPreviews::RegisterPane(type->name, std::move(container), owner);
		}
	}

} // namespace Shalltear::AudioEditor
