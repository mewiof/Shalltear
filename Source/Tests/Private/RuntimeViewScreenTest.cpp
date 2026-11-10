#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/UIDebugOps.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "TestPlatform.h"

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

	struct OpenView {
		Handle<EditorPanel> panel;
		UI::ElementHandle root;
	};

	class RuntimeViewScreenFixture : public ::testing::Test {
	protected:
		bool _mountedEngine = false;
		Handle<Window> _window;
		UI::Context* _context = nullptr;

		void SetUp() override {
			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			SceneSystem::Initialize();

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			EditorSelection::Initialize();
			EditorSceneOps::Initialize();
			EditorGizmos::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();

			WindowDesc desc;
			desc.title = "RuntimeViewScreenTest";
			desc.clientSize = {640, 480};
			desc.flags = WindowFlags::Hidden;
			_window = WindowSystem::Create(desc);
			ASSERT_TRUE(_window.IsValid());
			_context = UISystem::CreateContext(_window);
			ASSERT_NE(_context, nullptr);

			UI::Style columnStyle;
			columnStyle.flexDirection = UI::FlexDirection::Column;
			columnStyle.width = UI::Length::Percent(1.0f);
			columnStyle.height = UI::Length::Percent(1.0f);
			_context->SetStyle(_context->GetRoot(), columnStyle);
			UIScreenDesc nowhere;
			nowhere.target = UIScreenDesc::Target::None;
			UISystem::SetScreen(nowhere);
			SetPlayState(PlayModeState::Playing);
		}

		void TearDown() override {
			PanelManager::Terminate();
			UndoSystem::Terminate();
			EditorGizmos::Terminate();
			EditorSceneOps::Terminate();
			EditorSelection::Terminate();
			SceneSystem::Terminate();
			UISystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
		}

		OpenView Open(f32 grow = 1.0f) {
			OpenView view;
			const TypeInfo* type = TypeRegistry::FindType(StringHash("RuntimeViewPanel"));
			if (!type) {
				return view;
			}
			view.panel = PanelManager::OpenPanel(type);
			if (!view.panel.IsValid()) {
				return view;
			}
			UI::ElementHandle slot = _context->CreateElement();
			_context->AppendChild(_context->GetRoot(), slot);
			UI::Style slotStyle;
			slotStyle.width = UI::Length::Percent(1.0f);
			slotStyle.height = UI::Length::Pixel(0.0f);
			slotStyle.flexGrow = grow;
			slotStyle.flexShrink = 1.0f;
			_context->SetStyle(slot, slotStyle);
			view.root = _context->CreateElement();
			_context->AppendChild(slot, view.root);
			view.panel.Get()->BuildContent(*_context, view.root);
			return view;
		}

		UI::ElementHandle ViewportOf(const OpenView& view) const {
			return UITest::FindByClass(*_context, view.root, EditorTheme::Class::ViewportPlaceholder, 0);
		}
		UI::Rect RectOf(const OpenView& view) const { return _context->GetLayout(ViewportOf(view)).content; }
		Vec2I ScreenSizeOf(const OpenView& view) const { return EditorViewportOps::ViewTargetSizeFor(RectOf(view)); }

		void Frame() {
			Time::BeginFrame(1.0 / 60.0);
			WindowSystem::PumpEvents();
			UIScreenDesc nowhere;
			nowhere.target = UIScreenDesc::Target::None;
			UISystem::SetScreen(nowhere);
			PanelManager::TickAll(1.0f / 60.0f);
			UISystem::Update();
			PanelManager::SubmitViews(1.0f / 60.0f);
			SceneSystem::EndFrame();
		}

		static void SetPlayState(PlayModeState state) {
			EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(), {state});
		}

		void PostClick(i32 x, i32 y) {
			TestPlatform::PostMouseMove(_window, {x, y});
			TestPlatform::PostMouseButton(_window, MouseButton::Left, true, {x, y});
			TestPlatform::PostMouseButton(_window, MouseButton::Left, false, {x, y});
		}
		void PostKey(Key key) {
			TestPlatform::PostKey(_window, key, true);
			TestPlatform::PostKey(_window, key, false);
		}
		void PostKeyTap() { PostKey(Key::G); }
	};

	struct Probe {
		UISurfaceId surface = INVALID_UI_SURFACE;
		UI::ElementHandle element;
		u32 clicks = 0;
		u32 keys = 0;
	};

	void MakeProbe(Probe& probe, f32 x, f32 y, f32 w, f32 h) {
		UIScreenSurfaceDesc desc;
		desc.name = "project://Assets/UI/Hud.slxml";
		probe.surface = UISystem::CreateScreenSurface(desc);
		UI::Context* context = UISystem::GetSurfaceContext(probe.surface);
		probe.element = context->CreateElement();
		context->AppendChild(context->GetRoot(), probe.element);
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.left = UI::Length::Pixel(x);
		style.top = UI::Length::Pixel(y);
		style.width = UI::Length::Pixel(w);
		style.height = UI::Length::Pixel(h);
		style.background = UI::Brush::SolidColor(Color::Red());
		context->SetStyle(probe.element, style);
		context->SetFocusable(probe.element, true);
		context->Events(probe.element).onClick = [&probe]() { ++probe.clicks; };
		context->Events(probe.element).onKeyDown = [&probe](Key, KeyMod) {
			++probe.keys;
			return true;
		};
	}

	Vec2I SizeOf(const Probe& probe) { return UISystem::GetSurfaceContext(probe.surface)->GetSize(); }

	i32 X(const UI::Rect& rect, f32 dx) { return static_cast<i32>(rect.x + dx); }
	i32 Y(const UI::Rect& rect, f32 dy) { return static_cast<i32>(rect.y + dy); }

} // anonymous namespace

TEST_F(RuntimeViewScreenFixture, ViewClaimsScreenSize) {
	OpenView view = Open();
	ASSERT_TRUE(view.panel.IsValid());
	Frame();
	Frame();
	ASSERT_TRUE(_context->IsAlive(ViewportOf(view)));
	const Vec2I size = ScreenSizeOf(view);
	ASSERT_GT(size.x, 0);
	ASSERT_GT(size.y, 0);
	EXPECT_EQ(size.x, 640);
	EXPECT_LT(size.y, 480);

	Probe probe;
	MakeProbe(probe, 0.0f, 0.0f, 10.0f, 10.0f);
	Frame();
	EXPECT_EQ(SizeOf(probe), size);
	EXPECT_FLOAT_EQ(UISystem::GetSurfaceContext(probe.surface)->GetContentScale(), _context->GetContentScale());

	PanelManager::CloseAllPanels();
	Frame();
	Frame();
	EXPECT_EQ(SizeOf(probe), size);
	UISystem::DestroySurface(probe.surface);
}

TEST_F(RuntimeViewScreenFixture, FirstViewClaimsThenHandsOver) {
	OpenView first = Open(2.0f);
	OpenView second = Open(1.0f);
	ASSERT_TRUE(first.panel.IsValid());
	ASSERT_TRUE(second.panel.IsValid());
	Frame();
	Frame();
	const Vec2I firstSize = ScreenSizeOf(first);
	const Vec2I secondSize = ScreenSizeOf(second);
	ASSERT_NE(firstSize, secondSize);
	ASSERT_GT(secondSize.y, 0);

	Probe probe;
	MakeProbe(probe, 0.0f, 0.0f, 10.0f, 10.0f);
	Frame();
	EXPECT_EQ(SizeOf(probe), firstSize);

	PanelManager::ClosePanel(first.panel);
	Frame();
	Frame();
	EXPECT_EQ(SizeOf(probe), secondSize);
	UISystem::DestroySurface(probe.surface);
}

TEST_F(RuntimeViewScreenFixture, InsidePressReachesScreen) {
	OpenView view = Open();
	ASSERT_TRUE(view.panel.IsValid());
	Frame();
	Frame();
	const UI::Rect rect = RectOf(view);
	ASSERT_GT(rect.w, 100.0f);
	ASSERT_GT(rect.h, 100.0f);

	Probe probe;
	MakeProbe(probe, 0.0f, 0.0f, 100.0f, 100.0f);
	Frame();
	ASSERT_EQ(SizeOf(probe), ScreenSizeOf(view));

	PostClick(X(rect, 40.0f), Y(rect, 40.0f));
	Frame();
	EXPECT_EQ(probe.clicks, 1u);

	PostClick(X(rect, 40.0f), Y(rect, -4.0f));
	Frame();
	EXPECT_EQ(probe.clicks, 1u);

	PostClick(X(rect, rect.w - 20.0f), Y(rect, rect.h - 20.0f));
	Frame();
	EXPECT_EQ(probe.clicks, 1u);
	UISystem::DestroySurface(probe.surface);
}

TEST_F(RuntimeViewScreenFixture, KeysNeedViewportFocus) {
	OpenView view = Open();
	ASSERT_TRUE(view.panel.IsValid());
	Frame();
	Frame();
	const UI::Rect rect = RectOf(view);
	Probe probe;
	MakeProbe(probe, 0.0f, 0.0f, 100.0f, 100.0f);
	Frame();

	PostClick(X(rect, 40.0f), Y(rect, 40.0f));
	Frame();
	ASSERT_EQ(probe.clicks, 1u);
	ASSERT_TRUE(_context->GetFocused() == ViewportOf(view));
	PostKeyTap();
	Frame();
	EXPECT_EQ(probe.keys, 1u);

	_context->ClearFocus();
	Frame();
	ASSERT_FALSE(_context->GetFocused() == ViewportOf(view));
	PostKeyTap();
	Frame();
	EXPECT_EQ(probe.keys, 1u);
	UISystem::DestroySurface(probe.surface);
}

TEST_F(RuntimeViewScreenFixture, PressForwardsUntilRelease) {
	OpenView view = Open();
	ASSERT_TRUE(view.panel.IsValid());
	Frame();
	Frame();
	const UI::Rect rect = RectOf(view);
	Probe probe;
	MakeProbe(probe, 0.0f, 0.0f, 100.0f, 100.0f);
	Frame();
	UI::Context* context = UISystem::GetSurfaceContext(probe.surface);

	TestPlatform::PostMouseMove(_window, {X(rect, 40.0f), Y(rect, 40.0f)});
	TestPlatform::PostMouseButton(_window, MouseButton::Left, true, {X(rect, 40.0f), Y(rect, 40.0f)});
	Frame();
	EXPECT_TRUE(context->WantsMouse());
	TestPlatform::PostMouseMove(_window, {X(rect, 40.0f), Y(rect, -8.0f)});
	Frame();
	EXPECT_LT(context->GetPointerPosition().y, 0.0f);
	TestPlatform::PostMouseButton(_window, MouseButton::Left, false, {X(rect, 40.0f), Y(rect, -8.0f)});
	Frame();
	EXPECT_FALSE(context->WantsMouse());
	EXPECT_EQ(probe.clicks, 0u);
	UISystem::DestroySurface(probe.surface);
}

TEST_F(RuntimeViewScreenFixture, ViewLossEndsPress) {
	OpenView view = Open();
	ASSERT_TRUE(view.panel.IsValid());
	Frame();
	Frame();
	const UI::Rect rect = RectOf(view);
	Probe probe;
	MakeProbe(probe, 0.0f, 0.0f, 100.0f, 100.0f);
	Frame();
	UI::Context* game = UISystem::GetSurfaceContext(probe.surface);

	TestPlatform::PostMouseMove(_window, {X(rect, 40.0f), Y(rect, 40.0f)});
	TestPlatform::PostMouseButton(_window, MouseButton::Left, true, {X(rect, 40.0f), Y(rect, 40.0f)});
	Frame();
	ASSERT_TRUE(game->WantsMouse());

	UI::ElementHandle slot = _context->MakeHandle(_context->GetElement(view.root)->parent);
	UI::Style collapsed = _context->GetStyle(slot);
	const f32 grow = collapsed.flexGrow;
	collapsed.flexGrow = 0.0f;
	_context->SetStyle(slot, collapsed);
	Frame();
	Frame();
	ASSERT_FALSE(EditorViewportOps::ViewportIsOnScreen(RectOf(view)));
	EXPECT_FALSE(game->WantsMouse());

	collapsed.flexGrow = grow;
	_context->SetStyle(slot, collapsed);
	Frame();
	Frame();
	ASSERT_TRUE(EditorViewportOps::ViewportIsOnScreen(RectOf(view)));
	TestPlatform::PostMouseButton(_window, MouseButton::Left, false, {X(rect, 40.0f), Y(rect, 40.0f)});
	Frame();
	EXPECT_EQ(probe.clicks, 0u);
	UISystem::DestroySurface(probe.surface);
}

TEST_F(RuntimeViewScreenFixture, GameHearsOnlyWhilePlaying) {
	OpenView view = Open();
	ASSERT_TRUE(view.panel.IsValid());
	Frame();
	Frame();
	const UI::Rect rect = RectOf(view);
	Probe probe;
	MakeProbe(probe, 0.0f, 0.0f, 100.0f, 100.0f);
	Frame();

	for (PlayModeState state : {PlayModeState::Editing, PlayModeState::Paused}) {
		const char* name = state == PlayModeState::Editing ? "editing" : "paused";
		SetPlayState(state);
		PostClick(X(rect, 40.0f), Y(rect, 40.0f));
		Frame();
		ASSERT_TRUE(_context->GetFocused() == ViewportOf(view)) << name;
		PostKeyTap();
		Frame();
		EXPECT_EQ(probe.clicks, 0u) << name;
		EXPECT_EQ(probe.keys, 0u) << name;
	}

	SetPlayState(PlayModeState::Playing);
	PostClick(X(rect, 40.0f), Y(rect, 40.0f));
	Frame();
	EXPECT_EQ(probe.clicks, 1u);
	PostKeyTap();
	Frame();
	EXPECT_EQ(probe.keys, 1u);
	UISystem::DestroySurface(probe.surface);
}

TEST_F(RuntimeViewScreenFixture, PauseEndsPress) {
	OpenView view = Open();
	ASSERT_TRUE(view.panel.IsValid());
	Frame();
	Frame();
	const UI::Rect rect = RectOf(view);
	Probe probe;
	MakeProbe(probe, 0.0f, 0.0f, 100.0f, 100.0f);
	Frame();
	UI::Context* game = UISystem::GetSurfaceContext(probe.surface);

	TestPlatform::PostMouseMove(_window, {X(rect, 40.0f), Y(rect, 40.0f)});
	TestPlatform::PostMouseButton(_window, MouseButton::Left, true, {X(rect, 40.0f), Y(rect, 40.0f)});
	Frame();
	ASSERT_TRUE(game->WantsMouse());

	SetPlayState(PlayModeState::Paused);
	Frame();
	EXPECT_FALSE(game->WantsMouse());

	SetPlayState(PlayModeState::Playing);
	Frame();
	TestPlatform::PostMouseButton(_window, MouseButton::Left, false, {X(rect, 40.0f), Y(rect, 40.0f)});
	Frame();
	EXPECT_EQ(probe.clicks, 0u);
	UISystem::DestroySurface(probe.surface);
}

TEST_F(RuntimeViewScreenFixture, EscapeReachesGame) {
	OpenView view = Open();
	ASSERT_TRUE(view.panel.IsValid());
	Frame();
	Frame();
	const UI::Rect rect = RectOf(view);
	Probe probe;
	MakeProbe(probe, 0.0f, 0.0f, 100.0f, 100.0f);
	Frame();

	PostClick(X(rect, 40.0f), Y(rect, 40.0f));
	Frame();
	ASSERT_EQ(probe.clicks, 1u);
	ASSERT_TRUE(_context->GetFocused() == ViewportOf(view));

	PostKey(Key::Escape);
	Frame();
	EXPECT_EQ(probe.keys, 1u);
	EXPECT_TRUE(_context->GetFocused() == ViewportOf(view));

	PostKeyTap();
	Frame();
	EXPECT_EQ(probe.keys, 2u);
	UISystem::DestroySurface(probe.surface);
}

TEST_F(RuntimeViewScreenFixture, EscapeStillClearsFocusForEditorsElements) {
	OpenView view = Open();
	ASSERT_TRUE(view.panel.IsValid());
	Frame();
	Frame();

	UI::ElementHandle other = _context->CreateElement();
	_context->AppendChild(_context->GetRoot(), other);
	_context->SetFocusable(other, true);
	_context->SetFocus(other);
	Frame();
	ASSERT_TRUE(_context->GetFocused() == other);

	PostKey(Key::Escape);
	Frame();
	EXPECT_FALSE(_context->GetFocused() == other);
}

TEST_F(RuntimeViewScreenFixture, EscapeKeepsSecondViewFocus) {
	OpenView first = Open(2.0f);
	OpenView second = Open(1.0f);
	ASSERT_TRUE(first.panel.IsValid());
	ASSERT_TRUE(second.panel.IsValid());
	Frame();
	Frame();
	const UI::Rect rect = RectOf(second);
	ASSERT_GT(rect.w, 20.0f);
	ASSERT_GT(rect.h, 20.0f);

	PostClick(X(rect, 10.0f), Y(rect, 10.0f));
	Frame();
	ASSERT_TRUE(_context->GetFocused() == ViewportOf(second));

	PostKey(Key::Escape);
	Frame();
	EXPECT_TRUE(_context->GetFocused() == ViewportOf(second));
}

TEST_F(RuntimeViewScreenFixture, DebuggerNamesScreenSurfaceForDocument) {
	OpenView view = Open();
	ASSERT_TRUE(view.panel.IsValid());
	Frame();
	Frame();
	Probe probe;
	MakeProbe(probe, 0.0f, 0.0f, 10.0f, 10.0f);
	Frame();
	Vector<UIDebugOps::Target> targets{GetMemoryResource(MemTag::Debug)};
	UIDebugOps::EnumerateTargets(targets);
	bool listed = false;
	for (const UIDebugOps::Target& target : targets) {
		if (target.kind == UIDebugOps::Target::Kind::Surface && target.id == probe.surface) {
			listed = true;
			EXPECT_NE(target.label.find("Hud.slxml"), str::npos) << target.label;
		}
	}
	EXPECT_TRUE(listed);
	UISystem::DestroySurface(probe.surface);
}
