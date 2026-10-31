#include "Shalltear/Editor/EditorChromeOps.h"

#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <format>

namespace Shalltear::EditorChromeOps {

	namespace {

		f32 CaptionTextPx() {
			return EditorTheme::GetMetric(EditorTheme::MetricToken::Font);
		}

		Color WindowControlRest() {
			return EditorTheme::GetColor(EditorTheme::ColorToken::TextDim);
		}

		f32 StatusTextPx() {
			return EditorTheme::GetMetric(EditorTheme::MetricToken::FontBody);
		}

		std::string_view FirstLine(std::string_view message) {
			size_t breakAt = message.find('\n');
			std::string_view line = breakAt == std::string_view::npos ? message : message.substr(0, breakAt);
			if (!line.empty() && line.back() == '\r') {
				line.remove_suffix(1);
			}
			return line;
		}

		str NoticeLine(const ConsoleEntry& entry) {
			std::string_view line = FirstLine(entry.message);
			return entry.category.empty() ? str(line) : std::format("[{}] {}", entry.category, line);
		}

		str CountLabel(u32 count, std::string_view noun) {
			if (count == 0) {
				return {};
			}
			if (count > 999) {
				return std::format("999+ {}s", noun);
			}
			return std::format("{} {}{}", count, noun, count == 1 ? "" : "s");
		}

	} // anonymous namespace

	HitTestResult HitTestChrome(const UI::Context& context, Vec2I clientPosPx) {
		UI::ElementHandle hit = context.HitTest(
			Vec2{static_cast<f32>(clientPosPx.x), static_cast<f32>(clientPosPx.y)});
		if (!hit.IsValid()) {
			return HitTestResult::Client;
		}
		if (context.HasClass(hit, StringHash(UI::Widgets::EdgeControlClass))) {
			return HitTestResult::Content;
		}
		bool caption = context.HasClass(hit, CaptionClass) || context.HasClass(hit, CaptionStripClass);
		return caption ? HitTestResult::Caption : HitTestResult::Client;
	}

	HitTestResult ChromeHitTest(Window& window, Vec2I clientPosPx, void*) {
		UI::Context* context = UISystem::GetContext(Handle<Window>(&window));
		return context ? HitTestChrome(*context, clientPosPx) : HitTestResult::Client;
	}

	EditorIcons::IconButtonParts CaptionButton(UI::Context& context, UI::ElementHandle parent,
											   EditorIcons::Icon icon, std::string_view fallbackLabel,
											   std::string_view tooltip, Color restColor,
											   bool isClose, std::function<void()> onClick) {
		EditorIcons::IconButtonParts parts = EditorIcons::IconButton(
			context, parent, icon, fallbackLabel, tooltip, std::move(onClick), CaptionTextPx());
		EditorIcons::SetGlyphOrText(context, parts.face, icon, fallbackLabel, restColor,
									CaptionTextPx(), CaptionTextPx());
		context.AddClass(parts.button, EditorTheme::Class::ToolButton);
		if (isClose) {
			context.AddClass(parts.button, EditorTheme::Class::CaptionClose);
		}
		return parts;
	}

	WindowControlParts BuildWindowControls(UI::Context& context, UI::ElementHandle parent,
										   Handle<Window> window, std::function<void()> onClose) {
		WindowControlParts parts;

		CaptionButton(context, parent, EditorIcons::Glyph::WindowMinimize, "Minimize", "Minimize",
					  WindowControlRest(), false, [window]() {
						  if (Window* target = window.Get()) {
							  target->Minimize();
						  }
					  });

		parts.maximizeFace = CaptionButton(context, parent, EditorIcons::Glyph::WindowMaximize,
										   "Maximize", "Maximize", WindowControlRest(), false, [window]() {
											   Window* target = window.Get();
											   if (!target) {
												   return;
											   }
											   if (target->IsMaximized()) {
												   target->Restore();
											   } else {
												   target->Maximize();
											   }
										   })
								 .face;

		CaptionButton(context, parent, EditorIcons::Glyph::WindowClose, "Close", "Close",
					  WindowControlRest(), true, std::move(onClose));
		return parts;
	}

	void SetMaximizeGlyph(UI::Context& context, UI::ElementHandle face, bool maximized) {
		if (!face.IsValid()) {
			return;
		}
		EditorIcons::SetGlyphOrText(context, face,
									maximized ? EditorIcons::Glyph::WindowRestore
											  : EditorIcons::Glyph::WindowMaximize,
									maximized ? "Restore" : "Maximize",
									WindowControlRest(),
									CaptionTextPx(), CaptionTextPx());
	}

	ConsoleNoticeParts BuildConsoleNotice(UI::Context& context, UI::ElementHandle parent,
										  std::function<void()> onActivate) {
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
		Color rest = EditorTheme::GetColor(EditorTheme::ColorToken::TextDim);

		ConsoleNoticeParts parts;
		parts.root = context.CreateElement();
		context.AppendChild(parent, parts.root);
		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Row;
		rootStyle.alignItems = UI::Align::Center;
		rootStyle.flexGrow = 1.0f;
		rootStyle.minWidth = UI::Length::Pixel(0.0f);
		context.SetStyle(parts.root, rootStyle);
		context.AddClass(parts.root, EditorTheme::Class::StatusNotice);
		context.Events(parts.root).onClick = std::move(onActivate);

		parts.icon = context.CreateElement();
		context.AppendChild(parts.root, parts.icon);
		UI::Style iconStyle;
		iconStyle.flexShrink = 0.0f;
		iconStyle.pointerEvents = false;
		iconStyle.margin = {0.0f, 0.0f, EditorIcons::GutterGapPx(), 0.0f};
		context.SetStyle(parts.icon, iconStyle);
		context.SetText(parts.icon, font, StatusTextPx(), rest, "");

		parts.message = context.CreateElement();
		context.AppendChild(parts.root, parts.message);
		UI::Style messageStyle;
		messageStyle.flexGrow = 1.0f;
		messageStyle.minWidth = UI::Length::Pixel(0.0f);
		messageStyle.pointerEvents = false;
		context.SetStyle(parts.message, messageStyle);
		context.AddClass(parts.message, EditorTheme::Class::RowText);
		context.SetText(parts.message, font, StatusTextPx(), rest, "");

		auto addCount = [&](Color color) {
			UI::ElementHandle count = context.CreateElement();
			context.AppendChild(parts.root, count);
			UI::Style countStyle;
			countStyle.flexShrink = 0.0f;
			countStyle.pointerEvents = false;
			countStyle.margin = {EditorIcons::GutterGapPx(), 0.0f, 0.0f, 0.0f};
			context.SetStyle(count, countStyle);
			context.SetText(count, font, StatusTextPx(), color, "");
			return count;
		};
		parts.errors = addCount(EditorTheme::GetColor(EditorTheme::ColorToken::StatusError));
		parts.warnings = addCount(EditorTheme::GetColor(EditorTheme::ColorToken::StatusWarn));
		return parts;
	}

	ConsoleNoticeState DescribeConsoleNotice(const ConsoleSummary& summary) {
		const ConsoleEntry& shown = summary.newest;

		ConsoleNoticeState state;
		state.hasEntry = shown.sequence != 0;
		state.level = shown.level;
		state.errors = CountLabel(summary.unseenErrors, "error");
		state.warnings = CountLabel(summary.unseenWarnings, "warning");
		if (!state.hasEntry) {
			return state;
		}

		state.message = NoticeLine(shown);
		state.tooltip = state.message;
		const ConsoleEntry& unseen = summary.newestUnseenError;
		if (unseen.sequence != 0 && unseen.sequence != shown.sequence) {
			state.tooltip += "\n\nnewest error: ";
			state.tooltip += NoticeLine(unseen);
		}
		state.tooltip += "\n\nclick to open the Console";
		return state;
	}

	void SetConsoleNotice(UI::Context& context, const ConsoleNoticeParts& parts,
						  const ConsoleSummary& summary) {
		const ConsoleNoticeState state = DescribeConsoleNotice(summary);
		const bool hasEntry = state.hasEntry;
		const EditorIcons::LogLevelFace face = EditorIcons::FaceForLogLevel(state.level);

		if (context.IsAlive(parts.icon)) {
			if (hasEntry) {
				EditorIcons::SetGlyphOrText(context, parts.icon, face.icon, face.tag, face.color,
											StatusTextPx(), StatusTextPx());
			} else {
				EditorIcons::SetGlyph(context, parts.icon, EditorIcons::Icon{}, face.color, StatusTextPx());
			}
		}

		if (context.IsAlive(parts.message)) {
			Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
			context.SetText(parts.message, font, StatusTextPx(), face.color, state.message);
		}

		if (context.IsAlive(parts.errors)) {
			context.SetTextString(parts.errors, state.errors);
		}
		if (context.IsAlive(parts.warnings)) {
			context.SetTextString(parts.warnings, state.warnings);
		}

		if (context.IsAlive(parts.root)) {
			UI::Style rootStyle = context.GetStyle(parts.root);
			if (rootStyle.pointerEvents != hasEntry) {
				rootStyle.pointerEvents = hasEntry;
				context.SetStyle(parts.root, rootStyle);
			}
			context.SetTooltip(parts.root, state.tooltip);
		}
	}

	FloatingChromeParts BuildFloatingChrome(UI::Context& context, Handle<Window> window,
											std::function<void()> onClose) {
		FloatingChromeParts parts;

		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Stretch;
		context.SetStyle(context.GetRoot(), rootStyle);

		parts.dockHost = context.CreateElement();
		context.AppendChild(context.GetRoot(), parts.dockHost);
		UI::Style hostStyle;
		hostStyle.height = UI::Length::Pixel(0.0f);
		hostStyle.flexGrow = 1.0f;
		context.SetStyle(parts.dockHost, hostStyle);

		parts.controls = context.CreateElement();
		context.AppendChild(context.GetRoot(), parts.controls);
		UI::Style controlsStyle;
		controlsStyle.position = UI::Position::Absolute;
		controlsStyle.top = UI::Length::Pixel(0.0f);
		controlsStyle.right = UI::Length::Pixel(0.0f);
		controlsStyle.flexDirection = UI::FlexDirection::Row;
		controlsStyle.alignItems = UI::Align::Center;
		controlsStyle.height = UI::Length::Pixel(EditorTheme::GetMetric(EditorTheme::MetricToken::CaptionStripHeight));
		context.SetStyle(parts.controls, controlsStyle);

		parts.maximizeFace =
			BuildWindowControls(context, parts.controls, window, std::move(onClose)).maximizeFace;
		return parts;
	}

} // namespace Shalltear::EditorChromeOps
