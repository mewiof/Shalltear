#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/DragDrop.h"
#include "Shalltear/Core/Window/Window.h"

#include <functional>
#include <string_view>

namespace Shalltear {

	class FontAsset;
	class UIPanelSettings;
	class Window;
	struct InputEvent;
	namespace RHI {
		struct TextureHandle;
	}
	namespace UI {
		class Context;
		class IFontSystem;
		class IVectorIconSystem;
	} //namespace UI

	using UISurfaceId = u32;
	inline constexpr UISurfaceId INVALID_UI_SURFACE = 0;

	struct UIScreenDesc {
		enum class Target : u8 {
			Window,
			Texture,
			None
		};
		Target target = Target::Window;
		Vec2I sizePx{};
		f32 dpiScale = 1.0f;
	};

	struct UIScreenSurfaceDesc {
		Handle<UIPanelSettings> settings;
		f32 sortOrder = 0.0f;
		str name;
	};

	class SHALLTEAR_CORE_API UISystem {
	public:
		static void Initialize();
		static void Terminate();
		static bool IsInitialized();

		static UI::Context* CreateContext(Handle<Window> window);
		static void DestroyContext(Handle<Window> window);
		static UI::Context* GetContext(Handle<Window> window);
		static bool IsContextLive(const UI::Context* context);

		using ContextInitFn = std::function<void(UI::Context&)>;
		static void SetContextInitializer(ContextInitFn initializer);
		static void ForEachContext(const std::function<void(Handle<Window>, UI::Context&)>& visit);

		static UISurfaceId CreateSurface(Vec2I sizePx, f32 contentScale = 1.0f);
		static void DestroySurface(UISurfaceId surface);
		static void ResizeSurface(UISurfaceId surface, Vec2I sizePx, f32 contentScale);
		static UI::Context* GetSurfaceContext(UISurfaceId surface);
		static void ForEachSurface(const std::function<void(UISurfaceId, UI::Context&)>& visit);
		static RHI::TextureHandle GetSurfaceTexture(UISurfaceId surface);

		static void SetScreen(const UIScreenDesc& desc);
		static UISurfaceId CreateScreenSurface(const UIScreenSurfaceDesc& desc);
		static void SetScreenSurfaceDesc(UISurfaceId surface, const UIScreenSurfaceDesc& desc);
		static void ForEachScreenSurface(const std::function<void(UISurfaceId, UI::Context&)>& visit);
		static std::string_view GetSurfaceName(UISurfaceId surface);
		static void InjectScreenInput(const InputEvent& event);

		static UI::DockManager* CreateDockManager(Handle<Window> window);
		static UI::DockManager* GetDockManager(Handle<Window> window);

		static void RegisterPanel(StringHash id, std::string_view title, UI::DockBuildFn build, const UI::DockTabIcon& icon = {});
		static void SetPanelTitle(StringHash id, std::string_view title);
		// required before a panel instance dies
		static void UnregisterPanel(StringHash id);
		static void SetPanelCloseCallback(UI::DockPanelCloseFn callback);

		static void SetDockHost(Handle<Window> window, UI::ElementHandle host);
		struct FloatingDockWindowPolicy {
			WindowFlags extraFlags = WindowFlags::None;
			std::function<void(Handle<Window>)> onCreated;
		};
		static void SetFloatingDockWindowPolicy(FloatingDockWindowPolicy policy);

		// never call from UI callbacks (creates the window immediately)
		static Handle<Window> CreateFloatingDockWindow(Vec2I screenPosPx, Vec2I clientSizePx);
		static void GetDockWindows(Vector<Handle<Window>>& out);
		static bool IsFloatingDockWindow(Handle<Window> window);

		static UI::IFontSystem* GetFontSystem();
		static UI::IVectorIconSystem* GetVectorIconSystem();
		static Handle<FontAsset> GetDefaultFont();

		static void Update();

		static void SetCursorModeYielded(bool yielded);

		static bool WantsMouse(Handle<Window> window);
		static bool WantsKeyboard(Handle<Window> window);
		// screen surfaces hovered or pressed, player and Runtime View alike
		static bool ScreenWantsMouse();

		static void SetPanelDetachEnabled(bool enabled);

		static void BeginDragPayload(const UI::DragPayload& payload);
		static bool IsDragPayloadActive();
		static const UI::DragPayload* GetDragPayload();
		static void CancelDragPayload();

		struct Stats {
			u32 drawCalls = 0;
			u32 quads = 0;
			u32 contexts = 0;
		};
		static Stats GetStats();
	};

} // namespace Shalltear
