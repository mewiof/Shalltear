#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorPanel.h"

#include <functional>
#include <string_view>

namespace Shalltear {

	class Window;

	class SHALLTEAR_EDITOR_API PanelManager {
	public:
		static void Initialize();
		static void Terminate();

		static void SetDefaultWindow(Handle<Window> window);

		static Handle<EditorPanel> OpenPanel(const TypeInfo* type, Handle<Window> targetWindow = {});

		static Handle<EditorPanel> RestorePanel(const TypeInfo* type, u32 serial, std::string_view stateJSON);

		static void ClosePanel(Handle<EditorPanel> panel);
		static void CloseAllPanels();
		static void CloseByDockId(StringHash dockId);
		static void ConfirmUnsavedPanelsThen(UI::Context& context, std::function<void()> proceed);

		static Handle<EditorPanel> FindPanelByDockId(StringHash dockId);
		static Handle<EditorPanel> GetFocusedPanel();
		static PlayModeState GetPlayModeState();
		static void GetOpenPanels(Vector<Handle<EditorPanel>>& out);
		static u32 GetOpenPanelCount();
		static void GetPanelTypes(Vector<const TypeInfo*>& out);
		static str GetPanelTypeName(const TypeInfo* type);
		static str GetPanelTypeCategory(const TypeInfo* type);

		static bool FocusPanel(Handle<EditorPanel> panel);

		static bool RevealPanel(Handle<EditorPanel> panel);
		static bool FocusPanelOfType(StringHash typeName);
		static StringHash MakeDockId(const TypeInfo* type, u32 serial);

		static void TickAll(f32 deltaSeconds);

		static void SubmitViews(f32 deltaSeconds);

		static u32 CaptureModulePanels(const void* moduleBase);
		static u32 RestoreModulePanels();
		static u32 GetCapturedPanelCount();
	private:
		static Handle<EditorPanel> OpenInternal(const TypeInfo* type, u32 serial, std::string_view stateJSON,
												Handle<Window> targetWindow, bool autoDock);
	};

	SHALLTEAR_EDITOR_API void RegisterBuiltInPanels();

} // namespace Shalltear
