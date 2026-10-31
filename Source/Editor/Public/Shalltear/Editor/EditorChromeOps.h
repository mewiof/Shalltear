#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/Window/Window.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditorConsole.h"
#include "Shalltear/Editor/EditorIcons.h"

#include <functional>

namespace Shalltear::EditorChromeOps {

	inline constexpr StringHash CaptionClass = StringHash("ed-caption");

	inline constexpr StringHash CaptionStripClass = StringHash("ed-caption-strip");

	SHALLTEAR_EDITOR_API HitTestResult HitTestChrome(const UI::Context& context, Vec2I clientPosPx);

	SHALLTEAR_EDITOR_API HitTestResult ChromeHitTest(Window& window, Vec2I clientPosPx, void* user);

	SHALLTEAR_EDITOR_API EditorIcons::IconButtonParts CaptionButton(
		UI::Context& context, UI::ElementHandle parent, EditorIcons::Icon icon,
		std::string_view fallbackLabel, std::string_view tooltip, Color restColor,
		bool isClose, std::function<void()> onClick);

	struct WindowControlParts {
		UI::ElementHandle maximizeFace{};
	};
	SHALLTEAR_EDITOR_API WindowControlParts BuildWindowControls(
		UI::Context& context, UI::ElementHandle parent, Handle<Window> window,
		std::function<void()> onClose);

	SHALLTEAR_EDITOR_API void SetMaximizeGlyph(UI::Context& context, UI::ElementHandle face, bool maximized);

	struct ConsoleNoticeParts {
		UI::ElementHandle root;
		UI::ElementHandle icon;
		UI::ElementHandle message;
		UI::ElementHandle errors;
		UI::ElementHandle warnings;
	};

	SHALLTEAR_EDITOR_API ConsoleNoticeParts BuildConsoleNotice(
		UI::Context& context, UI::ElementHandle parent, std::function<void()> onActivate);

	struct ConsoleNoticeState {
		bool hasEntry = false;
		str message;
		LogLevel level = LogLevel::Info;
		str errors;
		str warnings;
		str tooltip;
	};

	SHALLTEAR_EDITOR_API ConsoleNoticeState DescribeConsoleNotice(const ConsoleSummary& summary);

	SHALLTEAR_EDITOR_API void SetConsoleNotice(UI::Context& context, const ConsoleNoticeParts& parts,
											   const ConsoleSummary& summary);

	struct FloatingChromeParts {
		UI::ElementHandle dockHost{};
		UI::ElementHandle controls{};
		UI::ElementHandle maximizeFace{};
	};
	SHALLTEAR_EDITOR_API FloatingChromeParts BuildFloatingChrome(
		UI::Context& context, Handle<Window> window, std::function<void()> onClose);

} // namespace Shalltear::EditorChromeOps
