#include "AudioEditor.h"

#include "AudioEditorPreferences.h"
#include "AudioProfilerOps.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/ProfilerModules.h"
#include "Shalltear/Editor/SettingsPresentation.h"

#include <format>

namespace Shalltear::AudioEditor {

	namespace {

		namespace Ops = AudioImpl::AudioProfilerOps;
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		constexpr f64 WINDOW_SECONDS = 0.2;

		struct Column {
			const char* title;
			f32 width;
		};
		constexpr Column COLUMNS[] = {
			{"Object", 120.0f},
			{"Asset", 120.0f},
			{"Volume", 52.0f},
			{"Audibility", 64.0f},
			{"Group", 120.0f},
			{"Priority", 50.0f},
			{"Plays", 40.0f},
			{"3D", 30.0f},
			{"Paused", 46.0f},
			{"Muted", 44.0f},
			{"Virtual", 46.0f},
			{"OneShot", 54.0f},
			{"Looped", 48.0f},
			{"Distance", 56.0f},
			{"MinDist", 52.0f},
			{"MaxDist", 52.0f},
			{"Time", 52.0f},
			{"Duration", 56.0f},
			{"Stream", 64.0f},
		};
		constexpr u32 COLUMN_COUNT = static_cast<u32>(std::size(COLUMNS));

		struct ProfilerState {
			HashMap<u64, u32> plays{GetMemoryResource(MemTag::Audio)};
			u64 lastSequence = 0;
			PlayModeState mode = PlayModeState::Editing;
			Ops::Counts before;
			Ops::Counts latest;
			Vector<Ops::Voice> voices{GetMemoryResource(MemTag::Audio)};
		};
		ProfilerState* _state = nullptr;

		ProfilerState& State() {
			if (!_state) {
				_state = New<ProfilerState>(MemTag::Audio);
				Ops::ReadCounts(_state->latest);
				_state->before = _state->latest;
			}
			return *_state;
		}

		bool ResetsOnPlay() {
			const AudioEditorPreferences* preferences = Preferences();
			return !preferences || preferences->resetPlayCountOnPlay;
		}

		void SetResetsOnPlay(bool reset) {
			AudioEditorPreferences* preferences = Preferences();
			if (!preferences || preferences->resetPlayCountOnPlay == reset) {
				return;
			}
			preferences->resetPlayCountOnPlay = reset;
			Settings::NotifyChanged(PREFERENCES, "resetPlayCountOnPlay");
			Settings::Save();
		}

		Handle<FontAsset> Font() { return UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{}; }

		UI::ElementHandle FirstChild(const UI::Context& context, UI::ElementHandle parent) {
			const UI::Element* pod = context.GetElement(parent);
			return pod ? context.MakeHandle(pod->firstChild) : UI::ElementHandle{};
		}

		UI::ElementHandle NextSibling(const UI::Context& context, UI::ElementHandle element) {
			const UI::Element* pod = context.GetElement(element);
			return pod ? context.MakeHandle(pod->nextSibling) : UI::ElementHandle{};
		}

		UI::ElementHandle Line(UI::Context& context, UI::ElementHandle parent, f32 sizePx, Color color, std::string_view text) {
			UI::ElementHandle line = context.CreateElement();
			context.AppendChild(parent, line);
			context.SetHoverable(line, false);
			context.SetText(line, Font(), sizePx, color, text);
			return line;
		}

		UI::ElementHandle Row(UI::Context& context, UI::ElementHandle table, Color color) {
			UI::ElementHandle row = context.CreateElement();
			context.AppendChild(table, row);
			context.SetHoverable(row, false);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.width = UI::Length::Percent(1.0f);
			context.SetStyle(row, style);
			for (const Column& column : COLUMNS) {
				UI::ElementHandle cell = Line(context, row, EditorTheme::GetMetric(MetricToken::FontSmall), color, "");
				UI::Style cellStyle;
				cellStyle.width = UI::Length::Pixel(column.width);
				cellStyle.minWidth = UI::Length::Pixel(0.0f);
				cellStyle.textOverflow = UI::TextOverflow::Ellipsis;
				context.SetStyle(cell, cellStyle);
			}
			return row;
		}

		const char* YesNo(bool value) { return value ? "YES" : "NO"; }

		void Cells(const Ops::Voice& voice, u32 plays, str (&out)[COLUMN_COUNT]) {
			u32 at = 0;
			out[at++] = voice.object;
			out[at++] = voice.asset;
			out[at++] = std::format("{:.3f}", voice.volume);
			out[at++] = std::format("{:.3f}", voice.audibility);
			out[at++] = voice.group;
			out[at++] = std::format("{}", voice.priority);
			out[at++] = plays > 0 ? std::format("{}", plays) : str();
			out[at++] = YesNo(voice.is3D);
			out[at++] = YesNo(voice.paused);
			out[at++] = YesNo(voice.muted);
			out[at++] = YesNo(voice.isVirtual);
			out[at++] = YesNo(voice.oneShot);
			out[at++] = YesNo(voice.looped);
			out[at++] = std::format("{:.2f}", voice.distance);
			out[at++] = voice.is3D ? std::format("{:.2f}", voice.minDistance) : str();
			out[at++] = voice.is3D ? std::format("{:.2f}", voice.maxDistance) : str();
			out[at++] = std::format("{:.2f}", voice.time);
			out[at++] = voice.duration > 0.0 ? std::format("{:.2f}", voice.duration) : str();
			out[at++] = voice.streamed ? std::format("{} fr", voice.streamedAhead) : str();
		}

		void Build(UI::Context& context, UI::ElementHandle host) {
			UI::ElementHandle options = context.CreateElement();
			context.AppendChild(host, options);
			UI::Style optionsStyle;
			optionsStyle.flexDirection = UI::FlexDirection::Row;
			optionsStyle.alignItems = UI::Align::Center;
			context.SetStyle(options, optionsStyle);
			UI::Widgets::Checkbox(context, options, ResetsOnPlay(), [](bool value) {
				SetResetsOnPlay(value);
			});
			Line(context, options, EditorTheme::GetMetric(MetricToken::FontSmall), EditorTheme::GetColor(ColorToken::TextDim), "Reset play count on play");

			UI::ElementHandle summary = context.CreateElement();
			context.AppendChild(host, summary);
			UI::Style summaryStyle;
			summaryStyle.flexDirection = UI::FlexDirection::Column;
			context.SetStyle(summary, summaryStyle);

			UI::ElementHandle table = context.CreateElement();
			context.AppendChild(host, table);
			UI::Style tableStyle;
			tableStyle.flexDirection = UI::FlexDirection::Column;
			context.SetStyle(table, tableStyle);
			UI::ElementHandle header = Row(context, table, EditorTheme::GetColor(ColorToken::TextDim));
			UI::ElementHandle cell = FirstChild(context, header);
			for (const Column& column : COLUMNS) {
				context.SetTextString(cell, column.title);
				cell = NextSibling(context, cell);
			}
		}

		void Refresh(UI::Context& context, UI::ElementHandle host) {
			ProfilerState& state = State();
			Ops::Counts now;
			Ops::ReadCounts(now);
			const f64 elapsed = static_cast<f64>(now.ticks - state.latest.ticks) / static_cast<f64>(Time::GetTicksPerSecond());
			if (elapsed >= WINDOW_SECONDS) {
				state.before = state.latest;
				state.latest = now;
			}
			Ops::Summary summary;
			Ops::Summarize(state.before, state.latest, summary);
			summary.counts = now;

			const UI::ElementHandle options = FirstChild(context, host);
			const UI::ElementHandle lines = NextSibling(context, options);
			const UI::ElementHandle table = NextSibling(context, lines);
			if (!context.IsAlive(lines) || !context.IsAlive(table)) {
				return;
			}

			context.ClearChildren(lines);
			const f32 size = EditorTheme::GetMetric(MetricToken::FontSmall);
			const Color text = EditorTheme::GetColor(ColorToken::Text);
			const Color dim = EditorTheme::GetColor(ColorToken::TextDim);
			Line(context, lines, size, text,
				 std::format("Audio Sources: {} total, {} playing, {} paused", summary.sources, summary.playingSources, summary.pausedSources));
			Line(context, lines, size, text,
				 std::format("Voices: {} ({} real of {}, {} virtual, {} streaming)", summary.voices, summary.realVoices, summary.maxRealVoices,
							 summary.virtualVoices, summary.streams));
			Line(context, lines, size, text,
				 std::format("Audio CPU: {:.1f}% of real time   {:.3f} ms a block   worst {:.3f} ms", summary.renderShare * 100.0,
							 summary.renderMeanMs, summary.renderWorstMs));
			Line(context, lines, size, text, std::format("Audio Memory: {:.1f} KiB", static_cast<f64>(summary.memoryBytes) / 1024.0));
			const Ops::Counts& counts = summary.counts;
			const bool trouble = counts.overruns + counts.streamUnderruns + counts.streamFailures + counts.decoderShortfalls + counts.droppedEvents > 0;
			Line(context, lines, size, trouble ? EditorTheme::GetColor(ColorToken::StatusWarn) : dim,
				 std::format("Underruns: {}   overruns {}   stream failures {}   decoder shortfalls {}   late starts {}   dropped events {}",
							 counts.streamUnderruns, counts.overruns, counts.streamFailures, counts.decoderShortfalls, counts.lateStarts,
							 counts.droppedEvents));
			Line(context, lines, size, dim,
				 std::format("Clock: {:.3f} s   device {:.3f} s   {} Hz, {} frames a period   restarts {}, stops {}, reroutes {}, interruptions {}",
							 summary.time, summary.deviceTime, summary.sampleRate, summary.periodFrames, counts.restarts, counts.stops, counts.reroutes,
							 counts.interruptions));

			Ops::CollectVoices(state.voices);
			u32 have = 0;
			UI::ElementHandle row = NextSibling(context, FirstChild(context, table));
			for (; context.IsAlive(row) && have < state.voices.size(); ++have) {
				row = NextSibling(context, row);
			}
			while (context.IsAlive(row)) {
				const UI::ElementHandle next = NextSibling(context, row);
				context.DestroyElement(row);
				row = next;
			}
			for (; have < state.voices.size(); ++have) {
				Row(context, table, text);
			}
			row = NextSibling(context, FirstChild(context, table));
			str cells[COLUMN_COUNT];
			for (const Ops::Voice& voice : state.voices) {
				const auto found = state.plays.find(voice.assetId);
				Cells(voice, found == state.plays.end() || voice.assetId == 0 ? 0u : found->second, cells);
				UI::ElementHandle cell = FirstChild(context, row);
				for (const str& value : cells) {
					if (context.GetTextString(cell) != value) {
						context.SetTextString(cell, value);
					}
					cell = NextSibling(context, cell);
				}
				row = NextSibling(context, row);
			}
		}

		void Sample() {
			ProfilerState& state = State();
			const PlayModeState mode = GetPlayModeState();
			if (mode != state.mode) {
				if (state.mode == PlayModeState::Editing && ResetsOnPlay()) {
					state.plays.clear();
				}
				state.mode = mode;
			}
			state.lastSequence = Ops::CountPlays(state.lastSequence, [&state](u64 assetId) { ++state.plays[assetId]; });
		}

	} // anonymous namespace

	void RegisterProfilerModule(const void* owner) {
		ProfilerModule module;
		module.title = "Audio";
		module.build = &Build;
		module.refresh = &Refresh;
		module.sample = &Sample;
		if (!ProfilerModules::Register(StringHash("Audio"), std::move(module), owner)) {
			SLT_LOG_ERROR("Audio", "couldn't register the Profiler window's Audio module");
		}
		SettingsFieldPresentation words;
		words.label = "Reset Play Count On Play";
		words.tooltip = "Count the Audio profiler's plays from 0 each time Play is pressed.";
		words.keywords = "profiler plays count reset";
		SettingsPresentation::RegisterField(PREFERENCES, "resetPlayCountOnPlay", std::move(words), owner);
	}

	void ReleaseProfilerModule() {
		if (_state) {
			Delete(_state);
			_state = nullptr;
		}
	}

} // namespace Shalltear::AudioEditor
