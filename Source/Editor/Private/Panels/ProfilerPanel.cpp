#include "ProfilerPanel.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Profiler.h"
#include "Shalltear/Core/UI/RenderList.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/ProfilerModules.h"

#include <algorithm>
#include <format>

namespace Shalltear {

	namespace {

		constexpr f32 REFRESH_SECONDS = 0.25f;
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		Color TextNormal() { return EditorTheme::GetColor(ColorToken::Text); }
		Color TextDim() { return EditorTheme::GetColor(ColorToken::TextDim); }

		UI::ElementHandle AddLine(UI::Context& context, UI::ElementHandle parent,
								  Handle<FontAsset> font, f32 sizePx, Color color, std::string_view text) {
			UI::ElementHandle line = context.CreateElement();
			context.AppendChild(parent, line);
			UI::Style style;
			style.margin = {0.0f, 0.0f, 0.0f, 3.0f};
			context.SetStyle(line, style);
			context.SetText(line, font, sizePx, color, text);
			return line;
		}

	} // anonymous namespace

	void ProfilerPanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};

		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Stretch;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		rootStyle.padding = UI::Edges::All(8.0f);
		rootStyle.overflow = UI::Overflow::Scroll;
		context.SetStyle(root, rootStyle);

		UI::ElementHandle header = context.CreateElement();
		context.AppendChild(root, header);
		UI::Style headerStyle;
		headerStyle.flexDirection = UI::FlexDirection::Row;
		headerStyle.alignItems = UI::Align::Center;
		context.SetStyle(header, headerStyle);
		AddLine(context, header, font, EditorTheme::GetMetric(MetricToken::Font), TextNormal(), "Frame Time");
		UI::Widgets::Spacer(context, header);
		UI::Widgets::Checkbox(context, header, paused, [this](bool value) {
			paused = value;
		});
		AddLine(context, header, font, EditorTheme::GetMetric(MetricToken::FontSmall), TextDim(), "pause");

		UI::ElementHandle graph = context.CreateElement();
		context.AppendChild(root, graph);
		UI::Style graphStyle;
		graphStyle.width = UI::Length::Percent(1.0f);
		graphStyle.height = UI::Length::Pixel(72.0f);
		graphStyle.margin = {0.0f, 4.0f, 0.0f, 8.0f};
		context.SetStyle(graph, graphStyle);
		context.AddClass(graph, EditorTheme::Class::Graph);
		context.Events(graph).customDraw =
			[this](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				const UI::Rect area = c.GetLayout(element).content;
				if (area.w <= 1.0f || area.h <= 1.0f) {
					return;
				}
				constexpr f32 TOP_SECONDS = 1.0f / 30.0f;
				f32 barWidth = area.w / static_cast<f32>(HISTORY);
				for (u32 i = 0; i < HISTORY; ++i) {
					f32 seconds = _frameSeconds[(_head + i) % HISTORY];
					if (seconds <= 0.0f) {
						continue;
					}
					f32 normalized = std::min(seconds / TOP_SECONDS, 1.0f);
					f32 barHeight = std::max(1.0f, normalized * area.h);
					Color color = EditorTheme::GetColor(seconds <= 1.0f / 55.0f	  ? ColorToken::StatusOk
														: seconds <= 1.0f / 28.0f ? ColorToken::StatusWarn
																				  : ColorToken::StatusError);
					paint.EmitQuad({area.x + static_cast<f32>(i) * barWidth,
									area.y + area.h - barHeight,
									std::max(1.0f, barWidth - 1.0f), barHeight},
								   color);
				}
				f32 budgetY = area.y + area.h * (1.0f - (1.0f / 60.0f) / TOP_SECONDS);
				paint.EmitQuad({area.x, budgetY, area.w, 1.0f}, EditorTheme::GetColor(ColorToken::GraphBudget));
			};

		_frameLabel = AddLine(context, root, font, EditorTheme::GetMetric(MetricToken::FontBody), TextNormal(), "");
		_renderLabel = AddLine(context, root, font, EditorTheme::GetMetric(MetricToken::FontBody), TextNormal(), "");
		_uiLabel = AddLine(context, root, font, EditorTheme::GetMetric(MetricToken::FontBody), TextNormal(), "");

		UI::ElementHandle zoneHeader = context.CreateElement();
		context.AppendChild(root, zoneHeader);
		UI::Style zoneHeaderStyle;
		zoneHeaderStyle.flexDirection = UI::FlexDirection::Row;
		zoneHeaderStyle.alignItems = UI::Align::Center;
		context.SetStyle(zoneHeader, zoneHeaderStyle);
		AddLine(context, zoneHeader, font, EditorTheme::GetMetric(MetricToken::Font), TextNormal(), "Zones");
		UI::Widgets::Spacer(context, zoneHeader);
		UI::Widgets::Checkbox(context, zoneHeader, Profiler::IsEnabled(), [](bool value) {
			Profiler::SetEnabled(value);
		});
		AddLine(context, zoneHeader, font, EditorTheme::GetMetric(MetricToken::FontSmall), TextDim(), "record");

		_zoneContainer = context.CreateElement();
		context.AppendChild(root, _zoneContainer);
		UI::Style zoneStyle;
		zoneStyle.flexDirection = UI::FlexDirection::Column;
		zoneStyle.margin = {0.0f, 0.0f, 0.0f, 8.0f};
		context.SetStyle(_zoneContainer, zoneStyle);

		AddLine(context, root, font, EditorTheme::GetMetric(MetricToken::Font), TextNormal(), "Memory");
		_memoryContainer = context.CreateElement();
		context.AppendChild(root, _memoryContainer);
		UI::Style memoryStyle;
		memoryStyle.flexDirection = UI::FlexDirection::Column;
		context.SetStyle(_memoryContainer, memoryStyle);

		_moduleContainer = context.CreateElement();
		context.AppendChild(root, _moduleContainer);
		UI::Style moduleStyle;
		moduleStyle.flexDirection = UI::FlexDirection::Column;
		context.SetStyle(_moduleContainer, moduleStyle);
		RebuildModules(context);

		RefreshStats(context);
	}

	void ProfilerPanel::RebuildModules(UI::Context& context) {
		_moduleVersion = ProfilerModules::GetVersion();
		_moduleHosts.clear();
		if (!context.IsAlive(_moduleContainer)) {
			return;
		}
		context.ClearChildren(_moduleContainer);
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
		ProfilerModules::ForEach([&](StringHash id, const ProfilerModule& module) {
			UI::ElementHandle heading = AddLine(context, _moduleContainer, font, EditorTheme::GetMetric(MetricToken::Font), TextNormal(), module.title);
			UI::Style headingStyle = context.GetStyle(heading);
			headingStyle.margin = {0.0f, 8.0f, 0.0f, 3.0f};
			context.SetStyle(heading, headingStyle);
			UI::ElementHandle host = context.CreateElement();
			context.AppendChild(_moduleContainer, host);
			UI::Style hostStyle;
			hostStyle.flexDirection = UI::FlexDirection::Column;
			context.SetStyle(host, hostStyle);
			_moduleHosts.push_back({id, host});
			module.build(context, host);
		});
	}

	void ProfilerPanel::RefreshStats(UI::Context& context) {
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};

		f32 sum = 0.0f, peak = 0.0f;
		u32 samples = 0;
		for (f32 seconds : _frameSeconds) {
			if (seconds > 0.0f) {
				sum += seconds;
				peak = std::max(peak, seconds);
				++samples;
			}
		}
		if (context.IsAlive(_frameLabel) && samples > 0) {
			f32 average = sum / static_cast<f32>(samples);
			context.SetTextString(_frameLabel,
								  std::format("{:.1f} FPS   avg {:.2f} ms   peak {:.2f} ms",
											  1.0f / average, average * 1000.0f, peak * 1000.0f));
		}

		if (context.IsAlive(_renderLabel)) {
			GraphicsStats stats = RenderSystem::GetStats();
			context.SetTextString(_renderLabel,
								  std::format("Render: {} draws, {} tris   VRAM {} / {} MiB   {} tex, {} buf, {} pso",
											  stats.drawCalls, stats.triangles,
											  stats.vramUsedBytes >> 20, stats.vramBudgetBytes >> 20,
											  stats.liveTextures, stats.liveBuffers, stats.livePipelines));
		}

		if (context.IsAlive(_uiLabel)) {
			UISystem::Stats stats = UISystem::GetStats();
			context.SetTextString(_uiLabel,
								  std::format("UI: {} draws, {} quads, {} contexts   Jobs: {} workers",
											  stats.drawCalls, stats.quads, stats.contexts,
											  JobSystem::GetWorkerCount()));
		}

		if (context.IsAlive(_zoneContainer)) {
			context.ClearChildren(_zoneContainer);
			if (!Profiler::IsEnabled()) {
				AddLine(context, _zoneContainer, font, EditorTheme::GetMetric(MetricToken::FontSmall), TextDim(),
						"not recording");
			} else {
				Profiler::FrameResults frame = Profiler::GetFrameResults();
				for (u32 i = 0; i < frame.count; ++i) {
					const Profiler::ZoneResult& zone = frame.zones[i];
					if (zone.calls == 0) {
						continue;
					}
					str indent(static_cast<size_t>(zone.depth) * 2, ' ');
					AddLine(context, _zoneContainer, font, EditorTheme::GetMetric(MetricToken::FontSmall), TextDim(),
							std::format("{}{:<22} {:>8.3f} ms   max {:>7.3f}   x{}",
										indent, zone.name, zone.milliseconds, zone.maxMilliseconds, zone.calls));
				}
				if (frame.droppedSites > 0 || frame.droppedDepth > 0) {
					AddLine(context, _zoneContainer, font, EditorTheme::GetMetric(MetricToken::FontSmall),
							EditorTheme::GetColor(ColorToken::StatusWarn),
							std::format("dropped: {} sites, {} entries past depth", frame.droppedSites, frame.droppedDepth));
				}
			}
		}

		if (context.IsAlive(_memoryContainer)) {
			context.ClearChildren(_memoryContainer);
			size_t total = 0;
			for (u32 tag = 0; tag < static_cast<u32>(MemTag::Count); ++tag) {
				size_t bytes = MemoryManager::GetBytesForTag(static_cast<MemTag>(tag));
				total += bytes;
				if (bytes == 0) {
					continue;
				}
				AddLine(context, _memoryContainer, font, EditorTheme::GetMetric(MetricToken::FontSmall), TextDim(),
						std::format("{:<10} {:>9.2f} KiB   {} allocs",
									MemoryManager::GetTagName(static_cast<MemTag>(tag)),
									static_cast<f64>(bytes) / 1024.0,
									MemoryManager::GetAllocationCountForTag(static_cast<MemTag>(tag))));
			}
			AddLine(context, _memoryContainer, font, EditorTheme::GetMetric(MetricToken::FontSmall), TextNormal(),
					std::format("{:<10} {:>9.2f} KiB", "TOTAL", static_cast<f64>(total) / 1024.0));
		}

		for (const ModuleHost& entry : _moduleHosts) {
			const ProfilerModule* module = ProfilerModules::Find(entry.id);
			if (module && context.IsAlive(entry.host)) {
				module->refresh(context, entry.host);
			}
		}
	}

	void ProfilerPanel::OnTick(f32 deltaSeconds) {
		if (!paused) {
			_frameSeconds[_head] = deltaSeconds;
			_head = (_head + 1) % HISTORY;
			ProfilerModules::ForEach([](StringHash, const ProfilerModule& module) {
				if (module.sample) {
					module.sample();
				}
			});
		}
		if (ProfilerModules::GetVersion() != _moduleVersion) {
			if (UI::Context* context = GetPanelContext()) {
				RebuildModules(*context);
			}
		}

		_refreshAccum += deltaSeconds;
		if (paused || _refreshAccum < REFRESH_SECONDS) {
			return;
		}
		_refreshAccum = 0.0f;

		if (UI::Context* context = GetPanelContext()) {
			RefreshStats(*context);
		}
	}

	EditorIcons::Icon ProfilerPanel::GetTabIcon() const {
		return EditorIcons::Glyph::PanelProfiler;
	}

} // namespace Shalltear
