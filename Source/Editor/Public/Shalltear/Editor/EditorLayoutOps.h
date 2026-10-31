#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/Window/Monitor.h"
#include "Shalltear/Core/Window/Window.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear {

	struct TypeInfo;

	namespace EditorLayoutOps {

		inline constexpr Vec2I MinRestoredClientSize = {640, 400};

		inline constexpr Vec2I MinVisiblePx = {160, 48};

		SHALLTEAR_EDITOR_API bool IsPlacementVisible(const WindowPlacement& placement,
													 std::span<const MonitorInfo> monitors);

		SHALLTEAR_EDITOR_API WindowPlacement ResolvePlacement(const WindowPlacement& saved,
															  std::span<const MonitorInfo> monitors,
															  Vec2I minClientSize = MinRestoredClientSize);

		SHALLTEAR_EDITOR_API void BuildDefaultLayout(Handle<Window> window);

		SHALLTEAR_EDITOR_API void ResetLayout(Handle<Window> window);

		SHALLTEAR_EDITOR_API str CaptureLayout(Handle<Window> mainWindow);

		SHALLTEAR_EDITOR_API bool ApplyLayout(Handle<Window> mainWindow, std::string_view document);

		SHALLTEAR_EDITOR_API void InitializeLayoutStore(const Path& directory);

		SHALLTEAR_EDITOR_API void ShutdownLayoutStore();

		inline constexpr std::string_view DefaultLayoutName = "Default";
		SHALLTEAR_EDITOR_API void GetLayoutNames(Vector<str>& out);

		SHALLTEAR_EDITOR_API str GetActiveLayout();

		SHALLTEAR_EDITOR_API void SaveActiveLayout(Handle<Window> mainWindow);

		SHALLTEAR_EDITOR_API bool SwitchLayout(Handle<Window> mainWindow, std::string_view name);

		SHALLTEAR_EDITOR_API bool SaveLayoutAs(Handle<Window> mainWindow, std::string_view name);

		SHALLTEAR_EDITOR_API bool RenameLayout(std::string_view from, std::string_view to);
		SHALLTEAR_EDITOR_API bool DeleteLayout(std::string_view name);

		SHALLTEAR_EDITOR_API void BringPanelsForward(const TypeInfo* panelType, Vector<StringHash>& outPrevious);

		SHALLTEAR_EDITOR_API void RestoreForwardPanels(std::span<const StringHash> panels);

	} // namespace EditorLayoutOps

} // namespace Shalltear
