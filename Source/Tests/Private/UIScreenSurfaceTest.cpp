#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/Window.h"
#include "Window/WindowInternal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <filesystem>

using namespace Shalltear;

namespace {

	std::filesystem::path FindRepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	class UIScreenFixture : public ::testing::Test {
	protected:
		bool _mounted = false;
		Vector<UIPanelSettings*> _settings{GetMemoryResource(MemTag::Debug)};

		void SetUp() override {
			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
		}

		void TearDown() override {
			UISystem::Terminate();
			for (UIPanelSettings* settings : _settings) {
				Delete(settings);
			}
			_settings.clear();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
		}

		Handle<UIPanelSettings> MakeSettings() {
			UIPanelSettings* settings = New<UIPanelSettings>(MemTag::Asset);
			_settings.push_back(settings);
			return Handle<UIPanelSettings>(settings);
		}

		static void UseTextureScreen(Vec2I sizePx, f32 dpiScale = 1.0f) {
			UIScreenDesc screen;
			screen.target = UIScreenDesc::Target::Texture;
			screen.sizePx = sizePx;
			screen.dpiScale = dpiScale;
			UISystem::SetScreen(screen);
		}

		static UISurfaceId Screen(Handle<UIPanelSettings> settings = {}, f32 sortOrder = 0.0f, const char* name = "") {
			UIScreenSurfaceDesc desc;
			desc.settings = settings;
			desc.sortOrder = sortOrder;
			desc.name = name;
			return UISystem::CreateScreenSurface(desc);
		}

		Handle<Window> MakeHiddenWindow() {
			WindowDesc desc;
			desc.title = "UIScreenSurfaceTest";
			desc.clientSize = {320, 240};
			desc.flags = WindowFlags::Hidden;
			return WindowSystem::Create(desc);
		}
	};

	struct Probe {
		UI::ElementHandle element;
		u32 clicks = 0;
		u32 keys = 0;
	};

	void Box(UI::Context& context, Probe& probe, f32 x, f32 y, f32 w, f32 h) {
		probe.element = context.CreateElement();
		context.AppendChild(context.GetRoot(), probe.element);
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.left = UI::Length::Pixel(x);
		style.top = UI::Length::Pixel(y);
		style.width = UI::Length::Pixel(w);
		style.height = UI::Length::Pixel(h);
		style.background = UI::Brush::SolidColor(Color::Red());
		context.SetStyle(probe.element, style);
		context.SetFocusable(probe.element, true);
		context.Events(probe.element).onClick = [&probe]() { ++probe.clicks; };
		context.Events(probe.element).onKeyDown = [&probe](Key, KeyMod) {
			++probe.keys;
			return true;
		};
	}

	InputEvent MouseMove(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseMove;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent MouseAt(InputEventType type, i32 x, i32 y) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent KeyPress(Key key) {
		InputEvent event{};
		event.type = InputEventType::KeyDown;
		event.key = key;
		return event;
	}

	void Inject(const InputEvent& event) {
		UISystem::InjectScreenInput(event);
		UISystem::Update();
	}

	void Click(i32 x, i32 y) {
		Inject(MouseMove(x, y));
		Inject(MouseAt(InputEventType::MouseDown, x, y));
		Inject(MouseAt(InputEventType::MouseUp, x, y));
	}

	Vector<UISurfaceId> ScreenOrder() {
		Vector<UISurfaceId> order{GetMemoryResource(MemTag::Debug)};
		UISystem::ForEachScreenSurface([&order](UISurfaceId id, UI::Context&) { order.push_back(id); });
		return order;
	}

	struct RGBA {
		u8 r = 0, g = 0, b = 0, a = 0;
		bool operator==(const RGBA&) const = default;
	};

	RGBA TexelAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return {pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
	}

	bool ReadbackSurface(UISurfaceId surface, Vector<u8>& outRGBA8, Vec2I& outSize) {
		RHI::TextureHandle texture = UISystem::GetSurfaceTexture(surface);
		UI::Context* context = UISystem::GetSurfaceContext(surface);
		RHI::Device* device = RenderSystem::GetDevice();
		if (!texture.IsValid() || !context || !device) {
			return false;
		}
		outSize = context->GetSize();
		outRGBA8.resize(static_cast<size_t>(outSize.x) * outSize.y * 4);
		device->ReadbackTexture(texture, {outRGBA8.data(), outRGBA8.size()});
		return true;
	}

	UI::ElementHandle FillWith(UI::Context& context, Color color) {
		UI::ElementHandle element = context.CreateElement();
		context.AppendChild(context.GetRoot(), element);
		UI::Style style;
		style.width = UI::Length::Percent(1.0f);
		style.height = UI::Length::Percent(1.0f);
		style.background = UI::Brush::SolidColor(color);
		context.SetStyle(element, style);
		return element;
	}

} // anonymous namespace

TEST_F(UIScreenFixture, ScreenSurfaceSizeAndScale) {
	UseTextureScreen({640, 360}, 1.25f);

	UISurfaceId plain = Screen();
	ASSERT_NE(plain, INVALID_UI_SURFACE);
	EXPECT_EQ(UISystem::GetSurfaceContext(plain)->GetSize(), (Vec2I{640, 360}));
	EXPECT_FLOAT_EQ(UISystem::GetSurfaceContext(plain)->GetContentScale(), 1.25f);

	Handle<UIPanelSettings> pixel = MakeSettings();
	pixel->scaleMode = UIPanelScaleMode::ConstantPixelSize;
	pixel->scale = 2.0f;
	UISurfaceId fixed = Screen(pixel);

	Handle<UIPanelSettings> fit = MakeSettings();
	fit->scaleMode = UIPanelScaleMode::ScaleWithScreenSize;
	fit->referenceResolution = {320, 180};
	UISurfaceId scaled = Screen(fit);

	UISystem::Update();
	EXPECT_EQ(UISystem::GetSurfaceContext(fixed)->GetSize(), (Vec2I{640, 360}));
	EXPECT_FLOAT_EQ(UISystem::GetSurfaceContext(fixed)->GetContentScale(), 2.0f);
	EXPECT_EQ(UISystem::GetSurfaceContext(scaled)->GetSize(), (Vec2I{640, 360}));
	EXPECT_FLOAT_EQ(UISystem::GetSurfaceContext(scaled)->GetContentScale(), 2.0f);

	UseTextureScreen({320, 180}, 1.0f);
	UISystem::Update();
	EXPECT_EQ(UISystem::GetSurfaceContext(plain)->GetSize(), (Vec2I{320, 180}));
	EXPECT_FLOAT_EQ(UISystem::GetSurfaceContext(plain)->GetContentScale(), 1.0f);
	EXPECT_FLOAT_EQ(UISystem::GetSurfaceContext(fixed)->GetContentScale(), 2.0f);
	EXPECT_FLOAT_EQ(UISystem::GetSurfaceContext(scaled)->GetContentScale(), 1.0f);

	UISurfaceId own = UISystem::CreateSurface({50, 40}, 3.0f);
	UISystem::Update();
	EXPECT_EQ(UISystem::GetSurfaceContext(own)->GetSize(), (Vec2I{50, 40}));
	EXPECT_FLOAT_EQ(UISystem::GetSurfaceContext(own)->GetContentScale(), 3.0f);
	EXPECT_EQ(ScreenOrder().size(), 3u);

	UISystem::DestroySurface(plain);
	UISystem::DestroySurface(fixed);
	UISystem::DestroySurface(scaled);
	UISystem::DestroySurface(own);
}

TEST_F(UIScreenFixture, AFullBleedChildFillsTheScreen) {
	UseTextureScreen({400, 300});
	UISurfaceId surface = Screen();
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	UI::ElementHandle fill = FillWith(*context, Color::Red());
	UISystem::Update();
	const UI::Rect box = context->GetLayout(fill).borderBox;
	EXPECT_FLOAT_EQ(box.w, 400.0f);
	EXPECT_FLOAT_EQ(box.h, 300.0f);
	UISystem::DestroySurface(surface);
}

TEST_F(UIScreenFixture, AbsentScreenLeavesSurfacesBare) {
	UIScreenDesc none;
	none.target = UIScreenDesc::Target::None;
	UISystem::SetScreen(none);
	UISurfaceId surface = Screen();
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UISystem::Update();
	EXPECT_FALSE(UISystem::GetSurfaceTexture(surface).IsValid());
	RenderSystem::RenderAll();
	UseTextureScreen({200, 100});
	UISystem::Update();
	EXPECT_EQ(UISystem::GetSurfaceContext(surface)->GetSize(), (Vec2I{200, 100}));
	UISystem::DestroySurface(surface);
}

TEST_F(UIScreenFixture, OnlyScreenSurfacesNamed) {
	UseTextureScreen({64, 64});
	UISurfaceId named = Screen({}, 0.0f, "project://Assets/UI/Hud.slxml");
	UISurfaceId plain = UISystem::CreateSurface({8, 8});
	EXPECT_EQ(UISystem::GetSurfaceName(named), "project://Assets/UI/Hud.slxml");
	EXPECT_TRUE(UISystem::GetSurfaceName(plain).empty());
	EXPECT_TRUE(UISystem::GetSurfaceName(INVALID_UI_SURFACE).empty());
	UISystem::DestroySurface(named);
	UISystem::DestroySurface(plain);
	EXPECT_TRUE(UISystem::GetSurfaceName(named).empty());
}

TEST_F(UIScreenFixture, ScreenSurfaceOrder) {
	UseTextureScreen({64, 64});
	UISurfaceId a = Screen({}, 1.0f);
	UISurfaceId b = Screen({}, 0.0f);
	{
		Vector<UISurfaceId> order = ScreenOrder();
		ASSERT_EQ(order.size(), 2u);
		EXPECT_EQ(order[0], b);
		EXPECT_EQ(order[1], a);
	}
	Handle<UIPanelSettings> under = MakeSettings();
	under->sortOrder = -1.0f;
	UISurfaceId c = Screen(under, 99.0f);
	{
		Vector<UISurfaceId> order = ScreenOrder();
		ASSERT_EQ(order.size(), 3u);
		EXPECT_EQ(order[0], c);
		EXPECT_EQ(order[1], b);
		EXPECT_EQ(order[2], a);
	}
	UIScreenSurfaceDesc raised;
	raised.sortOrder = 5.0f;
	UISystem::SetScreenSurfaceDesc(b, raised);
	{
		Vector<UISurfaceId> order = ScreenOrder();
		EXPECT_EQ(order[0], c);
		EXPECT_EQ(order[1], a);
		EXPECT_EQ(order[2], b);
	}
	UISurfaceId d = Screen({}, 1.0f);
	{
		Vector<UISurfaceId> order = ScreenOrder();
		ASSERT_EQ(order.size(), 4u);
		EXPECT_EQ(order[1], a);
		EXPECT_EQ(order[2], d);
	}
	under->sortOrder = 10.0f;
	{
		Vector<UISurfaceId> order = ScreenOrder();
		EXPECT_EQ(order[3], c);
	}
	UISystem::DestroySurface(a);
	UISystem::DestroySurface(b);
	UISystem::DestroySurface(c);
	UISystem::DestroySurface(d);
	EXPECT_TRUE(ScreenOrder().empty());
}

TEST_F(UIScreenFixture, PressLandsOnTopmostSurface) {
	UseTextureScreen({200, 200});
	UISurfaceId bottom = Screen({}, 0.0f);
	UISurfaceId top = Screen({}, 1.0f);
	Probe below;
	Probe above;
	Box(*UISystem::GetSurfaceContext(bottom), below, 100.0f, 0.0f, 100.0f, 100.0f);
	Box(*UISystem::GetSurfaceContext(top), above, 0.0f, 0.0f, 50.0f, 50.0f);
	UISystem::Update();

	Click(25, 25);
	EXPECT_EQ(above.clicks, 1u);
	EXPECT_EQ(below.clicks, 0u);

	Click(150, 50);
	EXPECT_EQ(above.clicks, 1u);
	EXPECT_EQ(below.clicks, 1u);

	Click(75, 150);
	EXPECT_EQ(above.clicks, 1u);
	EXPECT_EQ(below.clicks, 1u);

	UISystem::DestroySurface(top);
	UISystem::DestroySurface(bottom);
}

TEST_F(UIScreenFixture, MoveOverTopSurfaceLeavesOneBelow) {
	UseTextureScreen({200, 200});
	UISurfaceId bottom = Screen({}, 0.0f);
	UISurfaceId top = Screen({}, 1.0f);
	UI::Context* belowContext = UISystem::GetSurfaceContext(bottom);
	UI::Context* aboveContext = UISystem::GetSurfaceContext(top);
	Probe below;
	Probe above;
	Box(*belowContext, below, 0.0f, 0.0f, 200.0f, 200.0f);
	Box(*aboveContext, above, 0.0f, 0.0f, 50.0f, 50.0f);
	UISystem::Update();

	Inject(MouseMove(150, 150));
	EXPECT_TRUE(belowContext->GetHovered() == below.element);
	EXPECT_FALSE(aboveContext->GetHovered().IsValid());

	Inject(MouseMove(25, 25));
	EXPECT_TRUE(aboveContext->GetHovered() == above.element);
	EXPECT_FALSE(belowContext->GetHovered().IsValid());
	EXPECT_TRUE(UISystem::GetSurfaceContext(top)->WantsMouse());
	EXPECT_FALSE(UISystem::GetSurfaceContext(bottom)->WantsMouse());

	UISystem::DestroySurface(top);
	UISystem::DestroySurface(bottom);
}

TEST_F(UIScreenFixture, ScreenWantsMouseOverUIOrHeldPress) {
	UseTextureScreen({200, 200});
	UISurfaceId surface = Screen();
	Probe probe;
	Box(*UISystem::GetSurfaceContext(surface), probe, 0.0f, 0.0f, 50.0f, 50.0f);
	UISystem::Update();

	Inject(MouseMove(150, 150));
	EXPECT_FALSE(UISystem::ScreenWantsMouse());

	Inject(MouseMove(25, 25));
	EXPECT_TRUE(UISystem::ScreenWantsMouse());

	Inject(MouseAt(InputEventType::MouseDown, 25, 25));
	Inject(MouseMove(150, 150));
	EXPECT_TRUE(UISystem::ScreenWantsMouse());

	Inject(MouseAt(InputEventType::MouseUp, 150, 150));
	Inject(MouseMove(160, 160));
	EXPECT_FALSE(UISystem::ScreenWantsMouse());

	UISystem::DestroySurface(surface);
}

TEST_F(UIScreenFixture, PressCapturesGestureUntilRelease) {
	UseTextureScreen({200, 200});
	UISurfaceId bottom = Screen({}, 0.0f);
	UISurfaceId top = Screen({}, 1.0f);
	UI::Context* belowContext = UISystem::GetSurfaceContext(bottom);
	UI::Context* aboveContext = UISystem::GetSurfaceContext(top);
	Probe below;
	Probe above;
	Box(*belowContext, below, 100.0f, 0.0f, 100.0f, 100.0f);
	Box(*aboveContext, above, 0.0f, 0.0f, 50.0f, 50.0f);
	UISystem::Update();

	Inject(MouseMove(150, 50));
	Inject(MouseAt(InputEventType::MouseDown, 150, 50));
	Inject(MouseMove(25, 25));
	EXPECT_EQ(belowContext->GetPointerPosition(), (Vec2{25.0f, 25.0f}));
	EXPECT_FALSE(aboveContext->GetHovered().IsValid());
	Inject(MouseAt(InputEventType::MouseUp, 25, 25));
	EXPECT_EQ(below.clicks, 0u);
	EXPECT_EQ(above.clicks, 0u);
	Inject(MouseMove(25, 25));
	EXPECT_TRUE(aboveContext->GetHovered() == above.element);

	UISystem::DestroySurface(top);
	UISystem::DestroySurface(bottom);
}

TEST_F(UIScreenFixture, KeysFollowLastPress) {
	UseTextureScreen({200, 200});
	UISurfaceId bottom = Screen({}, 0.0f);
	UISurfaceId top = Screen({}, 1.0f);
	Probe below;
	Probe above;
	Box(*UISystem::GetSurfaceContext(bottom), below, 100.0f, 0.0f, 100.0f, 100.0f);
	Box(*UISystem::GetSurfaceContext(top), above, 0.0f, 0.0f, 50.0f, 50.0f);
	UISystem::Update();

	Inject(KeyPress(Key::G));
	EXPECT_EQ(below.keys, 0u);
	EXPECT_EQ(above.keys, 0u);

	Click(150, 50);
	Inject(KeyPress(Key::G));
	EXPECT_EQ(below.keys, 1u);
	EXPECT_EQ(above.keys, 0u);

	Click(25, 25);
	Inject(KeyPress(Key::G));
	EXPECT_EQ(below.keys, 1u);
	EXPECT_EQ(above.keys, 1u);

	Click(75, 150);
	Inject(KeyPress(Key::G));
	EXPECT_EQ(below.keys, 1u);
	EXPECT_EQ(above.keys, 1u);

	UISystem::DestroySurface(top);
	UISystem::DestroySurface(bottom);
}

TEST_F(UIScreenFixture, DestroyingSurfaceHoldingPressEndsGesture) {
	UseTextureScreen({200, 200});
	UISurfaceId bottom = Screen({}, 0.0f);
	UISurfaceId top = Screen({}, 1.0f);
	Probe below;
	Probe above;
	Box(*UISystem::GetSurfaceContext(bottom), below, 0.0f, 0.0f, 200.0f, 200.0f);
	Box(*UISystem::GetSurfaceContext(top), above, 0.0f, 0.0f, 50.0f, 50.0f);
	UISystem::Update();

	Inject(MouseMove(150, 150));
	Inject(MouseAt(InputEventType::MouseDown, 150, 150));
	UISystem::DestroySurface(bottom);
	Inject(MouseMove(25, 25));
	EXPECT_TRUE(UISystem::GetSurfaceContext(top)->GetHovered() == above.element);
	Inject(MouseAt(InputEventType::MouseUp, 25, 25));
	UISystem::DestroySurface(top);
}

#ifdef _WIN32

TEST_F(UIScreenFixture, WindowInputReachesScreen) {
	Handle<Window> window = MakeHiddenWindow();
	ASSERT_TRUE(window.IsValid());
	ASSERT_TRUE(window == WindowSystem::GetPrimaryWindow());
	UISurfaceId surface = Screen();
	Probe probe;
	Box(*UISystem::GetSurfaceContext(surface), probe, 0.0f, 0.0f, 100.0f, 100.0f);
	WindowSystem::PumpEvents();
	UISystem::Update();
	EXPECT_EQ(UISystem::GetSurfaceContext(surface)->GetSize(), window->GetClientSize());
	EXPECT_FLOAT_EQ(UISystem::GetSurfaceContext(surface)->GetContentScale(), window->GetContentScale());
	EXPECT_FALSE(UISystem::GetSurfaceTexture(surface).IsValid());

	HWND hwnd = static_cast<HWND>(window->GetNativeHandle().hwnd);
	::PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(50, 50));
	::PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(50, 50));
	WindowSystem::PumpEvents();
	UISystem::Update();
	EXPECT_TRUE(UISystem::WantsMouse(window));

	::PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(50, 50));
	WindowSystem::PumpEvents();
	UISystem::Update();
	EXPECT_EQ(probe.clicks, 1u);

	UI::Context* chrome = UISystem::CreateContext(window);
	ASSERT_NE(chrome, nullptr);
	Probe shield;
	Box(*chrome, shield, 0.0f, 0.0f, 100.0f, 100.0f);
	WindowSystem::PumpEvents();
	UISystem::Update();
	::PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(50, 50));
	::PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(50, 50));
	::PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(50, 50));
	WindowSystem::PumpEvents();
	UISystem::Update();
	EXPECT_EQ(shield.clicks, 1u);
	EXPECT_EQ(probe.clicks, 1u);

	UISystem::DestroySurface(surface);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(UIScreenFixture, RenderSizeMapsClicks) {
	Handle<Window> window = MakeHiddenWindow();
	ASSERT_TRUE(window.IsValid());
	ASSERT_EQ(window->GetClientSize(), Vec2I(320, 240));
	WindowInternal::SetRenderSize(*window.Get(), {160, 60});
	UISurfaceId surface = Screen();
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	Probe probe;
	Box(*context, probe, 0.0f, 0.0f, 100.0f, 100.0f);
	WindowSystem::PumpEvents();
	UISystem::Update();
	EXPECT_EQ(context->GetSize(), Vec2I(160, 60));
	EXPECT_FLOAT_EQ(context->GetContentScale(), window->GetContentScale() * 0.5f);
	const UI::Rect box = context->GetLayout(probe.element).borderBox;
	ASSERT_GT(box.h, 25.0f);
	ASSERT_LT(box.h, 80.0f);

	HWND hwnd = static_cast<HWND>(window->GetNativeHandle().hwnd);
	auto click = [&](i32 x, i32 y) {
		::PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
		::PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
		WindowSystem::PumpEvents();
		UISystem::Update();
		::PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
		WindowSystem::PumpEvents();
		UISystem::Update();
	};
	click(60, 100);
	EXPECT_EQ(probe.clicks, 1u);
	click(60, 20);
	EXPECT_EQ(probe.clicks, 1u);

	UISystem::DestroySurface(surface);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(UIScreenFixture, TextureScreenTakesNothingFromWindow) {
	Handle<Window> window = MakeHiddenWindow();
	ASSERT_TRUE(window.IsValid());
	UseTextureScreen({320, 240});
	UISurfaceId surface = Screen();
	Probe probe;
	Box(*UISystem::GetSurfaceContext(surface), probe, 0.0f, 0.0f, 100.0f, 100.0f);
	WindowSystem::PumpEvents();
	UISystem::Update();

	HWND hwnd = static_cast<HWND>(window->GetNativeHandle().hwnd);
	::PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(50, 50));
	::PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(50, 50));
	::PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(50, 50));
	WindowSystem::PumpEvents();
	UISystem::Update();
	EXPECT_EQ(probe.clicks, 0u);
	EXPECT_FALSE(UISystem::WantsMouse(window));

	UISystem::DestroySurface(surface);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

#endif

TEST_F(UIScreenFixture, YieldedCursorModeLasts) {
	Handle<Window> window = MakeHiddenWindow();
	ASSERT_TRUE(window.IsValid());
	UI::Context* context = UISystem::CreateContext(window);
	ASSERT_NE(context, nullptr);

	window->SetCursorMode(CursorMode::Hidden);
	UISystem::Update();
	EXPECT_EQ(window->GetCursorMode(), CursorMode::Normal);

	UISystem::SetCursorModeYielded(true);
	window->SetCursorMode(CursorMode::Hidden);
	UISystem::Update();
	EXPECT_EQ(window->GetCursorMode(), CursorMode::Hidden);
	UISystem::Update();
	UISystem::Update();
	EXPECT_EQ(window->GetCursorMode(), CursorMode::Hidden);

	UISystem::SetCursorModeYielded(false);
	UISystem::Update();
	EXPECT_EQ(window->GetCursorMode(), CursorMode::Normal);

	UISystem::DestroyContext(window);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(UIScreenFixture, ALiveClaimOutranksTheYield) {
	Handle<Window> window = MakeHiddenWindow();
	ASSERT_TRUE(window.IsValid());
	UI::Context* context = UISystem::CreateContext(window);
	ASSERT_NE(context, nullptr);
	Probe gesture;
	Box(*context, gesture, 0.0f, 0.0f, 50.0f, 50.0f);

	const u32 claim = context->ClaimPointer(gesture.element, CursorMode::Wrapped);
	ASSERT_NE(claim, 0u);
	UISystem::SetCursorModeYielded(true);
	UISystem::Update();
	EXPECT_EQ(window->GetCursorMode(), CursorMode::Wrapped);

	context->ReleasePointer(claim);
	UISystem::Update();
	EXPECT_EQ(window->GetCursorMode(), CursorMode::Normal);

	window->SetCursorMode(CursorMode::Hidden);
	UISystem::Update();
	EXPECT_EQ(window->GetCursorMode(), CursorMode::Hidden);

	UISystem::SetCursorModeYielded(false);
	UISystem::DestroyContext(window);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(UIScreenFixture, DeadClaimRestoresUnderYield) {
	Handle<Window> window = MakeHiddenWindow();
	ASSERT_TRUE(window.IsValid());
	UI::Context* context = UISystem::CreateContext(window);
	ASSERT_NE(context, nullptr);
	Probe gesture;
	Box(*context, gesture, 0.0f, 0.0f, 50.0f, 50.0f);

	ASSERT_NE(context->ClaimPointer(gesture.element, CursorMode::Wrapped), 0u);
	UISystem::SetCursorModeYielded(true);
	UISystem::Update();
	ASSERT_EQ(window->GetCursorMode(), CursorMode::Wrapped);

	context->DestroyElement(gesture.element);
	UISystem::Update();
	UISystem::Update();
	EXPECT_EQ(window->GetCursorMode(), CursorMode::Normal);

	UISystem::SetCursorModeYielded(false);
	UISystem::DestroyContext(window);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(UIScreenFixture, TextureScreenRendersToTexture) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UseTextureScreen({16, 16});
	UISurfaceId surface = Screen();
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	FillWith(*UISystem::GetSurfaceContext(surface), Color::Red());
	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	EXPECT_EQ(size, (Vec2I{16, 16}));
	EXPECT_EQ(TexelAt(pixels, size, 8, 8), (RGBA{255, 0, 0, 255}));

	UIScreenDesc none;
	none.target = UIScreenDesc::Target::None;
	UISystem::SetScreen(none);
	UISystem::Update();
	EXPECT_FALSE(UISystem::GetSurfaceTexture(surface).IsValid());
	UISystem::DestroySurface(surface);
}

#ifdef _WIN32

TEST_F(UIScreenFixture, WindowScreenPaintsWindow) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> window = MakeHiddenWindow();
	ASSERT_TRUE(window.IsValid());
	UISurfaceId surface = Screen();
	FillWith(*UISystem::GetSurfaceContext(surface), Color::Red());
	WindowSystem::PumpEvents();
	UISystem::Update();
	RenderSystem::RenderAll();
	EXPECT_GE(UISystem::GetStats().quads, 1u);

	UIScreenDesc none;
	none.target = UIScreenDesc::Target::None;
	UISystem::SetScreen(none);
	WindowSystem::PumpEvents();
	UISystem::Update();
	RenderSystem::RenderAll();
	EXPECT_EQ(UISystem::GetStats().quads, 0u);

	UISystem::DestroySurface(surface);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

#endif
