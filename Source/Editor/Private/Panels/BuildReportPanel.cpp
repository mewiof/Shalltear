#include "BuildReportPanel.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/ProjectOps.h"

#include <algorithm>
#include <filesystem>
#include <format>

namespace Shalltear {

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		Color TextNormal() { return EditorTheme::GetColor(ColorToken::Text); }
		Color TextDim() { return EditorTheme::GetColor(ColorToken::TextDim); }
		Color TextFaint() { return EditorTheme::GetColor(ColorToken::TextFaint); }
		Color StatusOk() { return EditorTheme::GetColor(ColorToken::StatusOk); }
		Color StatusWarn() { return EditorTheme::GetColor(ColorToken::StatusWarn); }

		f32 RowFont() { return EditorTheme::GetMetric(MetricToken::FontRow); }
		f32 BodyFont() { return EditorTheme::GetMetric(MetricToken::FontBody); }
		f32 SmallFont() { return EditorTheme::GetMetric(MetricToken::FontSmall); }

		constexpr f64 REPORT_POLL_SECONDS = 0.5;

		constexpr f32 BYTES_COLUMN = 92.0f;
		constexpr f32 TYPE_COLUMN = 116.0f;
		constexpr f32 CHAIN_INDENT = 18.0f;

		Handle<FontAsset> Font() {
			return UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
		}

		UI::ElementHandle Row(UI::Context& context, UI::ElementHandle parent) {
			UI::ElementHandle row = context.CreateElement();
			context.AppendChild(parent, row);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.alignItems = UI::Align::Center;
			style.width = UI::Length::Percent(1.0f);
			context.SetStyle(row, style);
			return row;
		}

		UI::ElementHandle Label(UI::Context& context, UI::ElementHandle parent, std::string_view text, f32 sizePx,
								Color color, f32 fixedWidth = 0.0f) {
			UI::ElementHandle label = context.CreateElement();
			context.AppendChild(parent, label);
			context.SetHoverable(label, false);
			if (fixedWidth > 0.0f) {
				UI::Style style;
				style.width = UI::Length::Pixel(fixedWidth);
				style.textOverflow = UI::TextOverflow::Ellipsis;
				context.SetStyle(label, style);
			}
			context.SetText(label, Font(), sizePx, color, text);
			return label;
		}

		str FormatBytes(u64 bytes) {
			if (bytes >= 1024ull * 1024ull) {
				return std::format("{:.2f} MiB", static_cast<f64>(bytes) / (1024.0 * 1024.0));
			}
			if (bytes >= 1024ull) {
				return std::format("{:.1f} KiB", static_cast<f64>(bytes) / 1024.0);
			}
			return std::format("{} B", bytes);
		}

		const char* CodecName(u8 codec) {
			switch (static_cast<PakCodec>(codec)) {
			case PakCodec::None:
				return "stored";
			case PakCodec::LZ4:
				return "lz4";
			default:
				return "?";
			}
		}

		u64 StoredBytes(const CookOps::CookReportAsset& line) {
			return line.descriptorBytes + line.payloadStored;
		}

	} // anonymous namespace

	i64 BuildReportPanel::ReportStamp() const {
		if (_projectPath.empty() || selectedProfile.empty()) {
			return 0;
		}
		std::error_code ec;
		std::filesystem::path path{BuildPipeline::GetCookReportPath(_projectPath, selectedProfile)};
		auto written = std::filesystem::last_write_time(path, ec);
		return ec ? 0 : written.time_since_epoch().count();
	}

	void BuildReportPanel::OnOpen() {
		Refresh();
		_dirty = true;
	}

	void BuildReportPanel::OnTick(f32 deltaSeconds) {
		_pollCountdown -= deltaSeconds;
		if (_pollCountdown <= 0.0) {
			_pollCountdown = REPORT_POLL_SECONDS;
			if (!_projectPath.empty() && !selectedProfile.empty() && ReportStamp() != _reportStamp) {
				Refresh();
				_dirty = true;
			}
		}

		UI::Context* context = GetPanelContext();
		if (!context || !context->IsAlive(_bodyViewport)) {
			return;
		}
		if (_dirty) {
			RebuildBody(*context);
		}
	}

	void BuildReportPanel::Refresh() {
		_projectPath = ProjectOps::GetMountedProjectPath();
		_profiles.clear();
		_report = CookOps::CookReport();
		_loaded = false;
		_loadError.clear();
		_expanded.clear();
		if (_projectPath.empty()) {
			return;
		}

		BuildProfileOps::ListProfiles(_projectPath, _profiles);
		if (selectedProfile.empty() ||
			std::find(_profiles.begin(), _profiles.end(), selectedProfile) == _profiles.end()) {
			selectedProfile = _profiles.empty() ? str() : _profiles.front();
		}
		if (selectedProfile.empty()) {
			return;
		}

		_reportStamp = ReportStamp();

		str path = BuildPipeline::GetCookReportPath(_projectPath, selectedProfile);
		str json = FileSystem::ReadTextFile(Path(path), MemTag::Editor);
		if (json.empty()) {
			return;
		}
		_loaded = CookOps::ReadReportJSON(json, _report, _loadError);
	}

	void BuildReportPanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Stretch;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		rootStyle.padding = UI::Edges::All(8.0f);
		rootStyle.overflow = UI::Overflow::Scroll;
		context.SetStyle(root, rootStyle);

		_bodyViewport = context.CreateElement();
		context.AppendChild(root, _bodyViewport);
		UI::Style bodyStyle;
		bodyStyle.flexDirection = UI::FlexDirection::Column;
		bodyStyle.alignItems = UI::Align::Stretch;
		bodyStyle.width = UI::Length::Percent(1.0f);
		context.SetStyle(_bodyViewport, bodyStyle);
		RebuildBody(context);
	}

	void BuildReportPanel::RebuildBody(UI::Context& context) {
		_dirty = false;
		context.ClearChildren(_bodyViewport);
		UI::ElementHandle root = _bodyViewport;

		if (_projectPath.empty()) {
			Label(context, root, "No project is open.", BodyFont(), TextDim());
			return;
		}

		UI::ElementHandle header = Row(context, root);
		Label(context, header, "Profile", SmallFont(), TextDim());
		if (!_profiles.empty()) {
			Vector<std::string_view> items{GetMemoryResource(MemTag::Editor)};
			u32 selected = 0;
			for (u32 i = 0; i < static_cast<u32>(_profiles.size()); ++i) {
				items.push_back(_profiles[i]);
				if (_profiles[i] == selectedProfile) {
					selected = i;
				}
			}
			UI::Widgets::DropdownDesc desc;
			desc.items = {items.data(), items.size()};
			desc.selected = selected;
			desc.font = Font();
			desc.fontSizePx = BodyFont();
			Vector<str> roster = _profiles;
			desc.onSelect = [this, roster](u32 index) {
				if (index < roster.size()) {
					selectedProfile = roster[index];
					Refresh();
					_dirty = true;
				}
			};
			UI::Widgets::Dropdown(context, header, desc);
		}
		UI::Widgets::Spacer(context, header);
		{
			UI::Widgets::ButtonDesc desc;
			desc.label = "Refresh";
			desc.font = Font();
			desc.fontSizePx = BodyFont();
			desc.onClick = [this]() {
				Refresh();
				_dirty = true;
			};
			UI::Widgets::Button(context, header, desc);
		}

		if (!_loadError.empty()) {
			Label(context, root, _loadError, BodyFont(),
				  EditorTheme::GetColor(ColorToken::StatusError));
			return;
		}
		if (!_loaded) {
			Label(context, root,
				  "This profile hasn't been built yet. Press Build.",
				  BodyFont(), TextDim());
			return;
		}

		BuildSummary(context, root);
		BuildAssetList(context, root);
	}

	void BuildReportPanel::BuildSummary(UI::Context& context, UI::ElementHandle parent) {
		Label(context, parent, _report.packagePath, SmallFont(), TextFaint());

		UI::ElementHandle sizes = Row(context, parent);
		Label(context, sizes, std::format("{} B on disk", _report.fileBytes), BodyFont(), TextNormal());
		Label(context, sizes,
			  std::format("   TOC {} B resident   payload {} B   {} region(s), {} page(s)", _report.tocBytes,
						  _report.payloadBytes, _report.regionCount, _report.pageCount),
			  SmallFont(), TextDim());

		u32 cooked = static_cast<u32>(_report.assets.size());
		u32 notReached = _report.candidateCount > cooked ? _report.candidateCount - cooked : 0;
		UI::ElementHandle counts = Row(context, parent);
		Label(context, counts, std::format("{} asset(s) cooked", cooked), BodyFont(), TextNormal());
		Label(context, counts,
			  std::format("   of {} in the project   {} not reached", _report.candidateCount, notReached),
			  SmallFont(), notReached > 0 ? StatusWarn() : TextDim());

		UI::ElementHandle keys = Row(context, parent);
		Label(context, keys,
			  std::format("compression: {}   debug names: {}",
						  _report.compression == static_cast<u8>(CookOps::CookCompression::LZ4HC) ? "LZ4HC"
						  : _report.compression == static_cast<u8>(CookOps::CookCompression::LZ4) ? "LZ4"
																								  : "none",
						  _report.debugNamesShipped ? "shipped" : "stripped"),
			  SmallFont(), TextDim());
	}

	void BuildReportPanel::BuildAssetList(UI::Context& context, UI::ElementHandle parent) {
		Vector<u32> order{GetMemoryResource(MemTag::Editor)};
		order.reserve(_report.assets.size());
		for (u32 i = 0; i < static_cast<u32>(_report.assets.size()); ++i) {
			order.push_back(i);
		}
		const CookOps::CookReport& report = _report;
		if (sortByBytes) {
			std::stable_sort(order.begin(), order.end(), [&report](u32 a, u32 b) {
				return StoredBytes(report.assets[a]) > StoredBytes(report.assets[b]);
			});
		} else {
			std::stable_sort(order.begin(), order.end(), [&report](u32 a, u32 b) {
				return report.assets[a].name < report.assets[b].name;
			});
		}

		UI::ElementHandle sortRow = Row(context, parent);
		UI::Widgets::Checkbox(context, sortRow, sortByBytes, [this](bool value) {
			sortByBytes = value;
			_dirty = true;
		});
		Label(context, sortRow, "largest first", SmallFont(), TextDim());
		UI::Widgets::Spacer(context, sortRow);
		Label(context, sortRow, "click a row for why it is here", SmallFont(), TextFaint());

		for (u32 index : order) {
			const CookOps::CookReportAsset& line = _report.assets[index];
			UI::ElementHandle row = Row(context, parent);
			context.AddClass(row, StringHash("slt-tree-row"));

			context.Events(row).onClick = [this, index]() {
				auto at = std::find(_expanded.begin(), _expanded.end(), index);
				if (at == _expanded.end()) {
					_expanded.push_back(index);
				} else {
					_expanded.erase(at);
				}
				_dirty = true;
			};

			Label(context, row, std::format("{} B", StoredBytes(line)), RowFont(),
				  line.isRoot ? StatusOk() : TextNormal(), BYTES_COLUMN);
			Label(context, row, line.typeName, RowFont(), TextDim(), TYPE_COLUMN);
			Label(context, row, line.name.empty() ? line.guid.ToString() : line.name, RowFont(), TextNormal());
			UI::Widgets::Spacer(context, row);
			Label(context, row,
				  line.payloadRaw == 0
					  ? str("descriptor only")
					  : std::format("{} raw, {}", FormatBytes(line.payloadRaw), CodecName(line.codec)),
				  SmallFont(), TextFaint());

			if (std::find(_expanded.begin(), _expanded.end(), index) != _expanded.end()) {
				BuildWhyRow(context, parent, line);
			}
		}
	}

	void BuildReportPanel::BuildWhyRow(UI::Context& context, UI::ElementHandle parent,
									   const CookOps::CookReportAsset& line) {
		UI::ElementHandle why = context.CreateElement();
		context.AppendChild(parent, why);
		UI::Style style;
		style.flexDirection = UI::FlexDirection::Column;
		style.width = UI::Length::Percent(1.0f);
		style.padding = {CHAIN_INDENT, 2.0f, 0.0f, 6.0f};
		context.SetStyle(why, style);

		if (line.isRoot) {
			Label(context, why,
				  "a root (the profile listed this scene, or Core lists it as a built-in)", SmallFont(),
				  StatusOk());
		} else {
			str chain;
			for (u32 step : line.rootPath) {
				const CookOps::CookReportAsset& hop = _report.assets[step];
				chain += (hop.name.empty() ? hop.guid.ToString() : hop.name);
				chain += "  ->  ";
			}
			chain += line.name.empty() ? line.guid.ToString() : line.name;
			Label(context, why, chain, SmallFont(), TextDim());
		}

		if (!line.pulledInBy.empty()) {
			str referrers = std::format("referenced by {}: ", line.pulledInBy.size());
			for (size_t i = 0; i < line.pulledInBy.size(); ++i) {
				const CookOps::CookReportAsset& from = _report.assets[line.pulledInBy[i]];
				referrers += (from.name.empty() ? from.guid.ToString() : from.name);
				if (i + 1 < line.pulledInBy.size()) {
					referrers += ", ";
				}
			}
			Label(context, why, referrers, SmallFont(), TextFaint());
		}
		if (!line.dependsOn.empty()) {
			Label(context, why, std::format("pulls in {} asset(s)", line.dependsOn.size()), SmallFont(),
				  TextFaint());
		}
		Label(context, why, line.guid.ToString(), SmallFont(), TextFaint());
	}

	EditorIcons::Icon BuildReportPanel::GetTabIcon() const {
		return EditorIcons::Glyph::PanelReport;
	}

} // namespace Shalltear
