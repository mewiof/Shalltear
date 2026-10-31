#include "TestsPCH.h"

#include "Shalltear/Core/UI/VectorAsset.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Editor/EditorConsole.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/SceneViewEffects.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "EditorConfig.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace Shalltear;
using namespace UITest;

namespace {

	class EditorPanelsTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			EditorSelection::Initialize();
			PanelManager::Initialize();
		}
		void TearDown() override {
			PanelManager::Terminate();
			EditorSelection::Terminate();
		}

		static const TypeInfo* FindPanelType(const char* name) {
			return TypeRegistry::FindType(StringHash(name));
		}

		static u32 CountSubtree(UI::Context& context, UI::ElementHandle root) {
			u32 count = 0;
			const UI::Element* pod = context.GetElement(root);
			if (!pod) {
				return 0;
			}
			for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				count += 1 + CountSubtree(context, handle);
				child = context.GetElement(handle)->nextSibling;
			}
			return count;
		}

		static void BuildTwice(EditorPanel& panel, u32 (&outCounts)[2]) {
			MockFontSystem fonts;
			UI::ContextDesc desc;
			desc.sizePx = {800, 600};
			UI::Context context(desc, &fonts);
			for (int build = 0; build < 2; ++build) {
				UI::ElementHandle root = context.CreateElement();
				context.AppendChild(context.GetRoot(), root);
				panel.BuildContent(context, root);
				context.Update();
				outCounts[build] = CountSubtree(context, root);
			}
		}

		static Vec2 CenterOf(UI::Context& context, UI::ElementHandle handle) {
			const UI::Rect& box = context.GetLayout(handle).borderBox;
			return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
		}

		static void PressKey(UI::Context& context, Key key) {
			InputEvent event{};
			event.type = InputEventType::KeyDown;
			event.key = key;
			context.InjectInput(event);
			context.Update();
		}
	};

} // anonymous namespace

TEST_F(EditorPanelsTest, BuiltInTypesRegisterAndEnumerate) {
	const TypeInfo* console = FindPanelType("ConsolePanel");
	const TypeInfo* profiler = FindPanelType("ProfilerPanel");
	ASSERT_NE(console, nullptr);
	ASSERT_NE(profiler, nullptr);
	EXPECT_EQ(PanelManager::GetPanelTypeName(console), "Console");
	EXPECT_EQ(PanelManager::GetPanelTypeName(profiler), "Profiler");

	Vector<const TypeInfo*> types{GetMemoryResource(MemTag::Editor)};
	PanelManager::GetPanelTypes(types);
	bool foundConsole = false, foundProfiler = false;
	for (const TypeInfo* type : types) {
		foundConsole |= type == console;
		foundProfiler |= type == profiler;
	}
	EXPECT_TRUE(foundConsole);
	EXPECT_TRUE(foundProfiler);
}

TEST_F(EditorPanelsTest, ConsolePanelBuildsHeadlessAndIdempotent) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	ASSERT_TRUE(panel.IsValid());

	u32 counts[2] = {};
	BuildTwice(*panel.Get(), counts);
	EXPECT_GT(counts[0], 10u);
	EXPECT_EQ(counts[0], counts[1]);
}

TEST_F(EditorPanelsTest, ProfilerPanelBuildsHeadlessAndIdempotent) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ProfilerPanel"));
	ASSERT_TRUE(panel.IsValid());

	u32 counts[2] = {};
	BuildTwice(*panel.Get(), counts);
	EXPECT_GT(counts[0], 6u);
	EXPECT_EQ(counts[0], counts[1]);
}

TEST_F(EditorPanelsTest, PanelsTickWithoutServicesOrUI) {
	PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	PanelManager::OpenPanel(FindPanelType("ProfilerPanel"));
	for (int i = 0; i < 3; ++i) {
		PanelManager::TickAll(0.016f);
		PanelManager::SubmitViews(0.016f);
	}
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 2u);
}

TEST_F(EditorPanelsTest, PersistedSchemaFieldsExist) {
	const TypeInfo* console = FindPanelType("ConsolePanel");
	EXPECT_NE(FindField(console, "showTrace"_hash), nullptr);
	EXPECT_NE(FindField(console, "showMessages"_hash), nullptr);
	EXPECT_NE(FindField(console, "showWarnings"_hash), nullptr);
	EXPECT_NE(FindField(console, "showErrors"_hash), nullptr);
	EXPECT_NE(FindField(console, "textFilter"_hash), nullptr);
	EXPECT_NE(FindField(FindPanelType("ProfilerPanel"), "paused"_hash), nullptr);
}

TEST_F(EditorPanelsTest, ConsoleOpensFollowingTail) {
	const TypeInfo* console = FindPanelType("ConsolePanel");
	const FieldInfo* tail = FindField(console, "autoscroll"_hash);
	ASSERT_NE(tail, nullptr);
	EXPECT_TRUE(tail->flags & FieldFlagTransient);

	Handle<EditorPanel> panel = PanelManager::OpenPanel(console);
	ASSERT_TRUE(panel.IsValid());
	Variant value;
	ASSERT_TRUE(GetField(panel.Get(), console, "autoscroll"_hash, &value));
	EXPECT_TRUE(value.As<bool>());

	str state = TextSerializer::Serialize(panel.Get(), console);
	EXPECT_EQ(state.find("autoscroll"), str::npos) << state;

	str stale = R"({"_type":"ConsolePanel","levelMask":62,"showWarnings":false,"textFilter":"","autoscroll":false})";
	ASSERT_TRUE(TextSerializer::Deserialize(panel.Get(), console, stale));
	ASSERT_TRUE(GetField(panel.Get(), console, "autoscroll"_hash, &value));
	EXPECT_TRUE(value.As<bool>());
	ASSERT_TRUE(GetField(panel.Get(), console, "showWarnings"_hash, &value));
	EXPECT_FALSE(value.As<bool>());
	ASSERT_TRUE(GetField(panel.Get(), console, "showMessages"_hash, &value));
	EXPECT_TRUE(value.As<bool>());
}

TEST_F(EditorPanelsTest, ViewportTypesRegisterAndName) {
	const TypeInfo* sceneView = FindPanelType("SceneViewPanel");
	const TypeInfo* runtimeView = FindPanelType("RuntimeViewPanel");
	ASSERT_NE(sceneView, nullptr);
	ASSERT_NE(runtimeView, nullptr);
	EXPECT_EQ(PanelManager::GetPanelTypeName(sceneView), "Scene View");
	EXPECT_EQ(PanelManager::GetPanelTypeName(runtimeView), "Runtime View");
}

TEST_F(EditorPanelsTest, SceneViewBuildsHeadlessAndIdempotent) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(panel.IsValid());

	u32 counts[2] = {};
	BuildTwice(*panel.Get(), counts);
	EXPECT_GT(counts[0], 6u);
	EXPECT_EQ(counts[0], counts[1]);
}

TEST_F(EditorPanelsTest, RuntimeViewBuildsHeadlessAndIdempotent) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("RuntimeViewPanel"));
	ASSERT_TRUE(panel.IsValid());

	u32 counts[2] = {};
	BuildTwice(*panel.Get(), counts);
	EXPECT_GE(counts[0], 3u);
	EXPECT_EQ(counts[0], counts[1]);
}

TEST_F(EditorPanelsTest, BothViewportToolbarsArePanelHeaderStrip) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);

	auto buildToolbar = [&](const char* typeName) {
		Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType(typeName));
		EXPECT_TRUE(panel.IsValid());
		UI::ElementHandle root = context.CreateElement();
		context.AppendChild(context.GetRoot(), root);
		panel.Get()->BuildContent(context, root);
		context.Update();
		return NthChildOf(context, root, 0);
	};

	UI::ElementHandle sceneBar = buildToolbar("SceneViewPanel");
	UI::ElementHandle runtimeBar = buildToolbar("RuntimeViewPanel");
	ASSERT_TRUE(context.IsAlive(sceneBar));
	ASSERT_TRUE(context.IsAlive(runtimeBar));

	for (UI::ElementHandle bar : {sceneBar, runtimeBar}) {
		EXPECT_TRUE(context.HasClass(bar, EditorTheme::Class::PanelHeader));
		EXPECT_FALSE(context.HasClass(bar, EditorTheme::Class::ChromeBar));
	}
}

TEST_F(EditorPanelsTest, RuntimeViewLabelRightOnSwitch) {
	SceneSystem::Initialize();
	Handle<Scene> scene = SceneSystem::CreateScene("Level");
	Handle<EditorPanel> runtimeView = PanelManager::OpenPanel(FindPanelType("RuntimeViewPanel"));
	Handle<EditorPanel> sceneView = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(runtimeView.IsValid());
	ASSERT_TRUE(sceneView.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {1200, 800};
	UI::Context context(desc, &fonts);
	UI::DockManager dock{context, Handle<FontAsset>{}};
	for (const Handle<EditorPanel>& panel : {runtimeView, sceneView}) {
		dock.RegisterPanel(panel.Get()->GetDockId(), PanelManager::GetPanelTypeName(panel.Get()->GetType()),
						   [panel](UI::Context& c, UI::ElementHandle parent) {
							   if (EditorPanel* live = panel.Get()) {
								   live->BuildContent(c, parent);
							   }
						   });
		dock.DockPanel(panel.Get()->GetDockId(), dock.GetRootNode(), UI::DockZone::Center);
	}
	Vec2 mouse{};
	auto frame = [&]() {
		dock.UpdateInteraction(mouse, {0, 0}, false, false);
		dock.Build(context.GetRoot());
		context.Update();
	};
	auto pressTab = [&](std::string_view title) {
		UI::ElementHandle label = UITest::FindByText(context, context.GetRoot(), title);
		ASSERT_TRUE(context.IsAlive(label)) << title;
		const UI::Rect& box = context.GetLayout(label).borderBox;
		mouse = {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
		InputEvent event{};
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(mouse.x), static_cast<i32>(mouse.y)};
		event.type = InputEventType::MouseMove;
		context.InjectInput(event);
		event.type = InputEventType::MouseDown;
		context.InjectInput(event);
		context.Update();
		event.type = InputEventType::MouseUp;
		context.InjectInput(event);
		context.Update();
	};
	auto saysNoCamera = [&context]() {
		return context.IsAlive(UITest::FindByText(context, context.GetRoot(), "no camera in the loaded scenes"));
	};
	frame();
	frame();

	pressTab("Runtime View");
	frame();
	ASSERT_TRUE(saysNoCamera());

	pressTab("Scene View");
	frame();
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Main Camera", scene);
	cameraObject.Get()->AddComponent<Camera>();
	SceneSystem::EndFrame();
	pressTab("Runtime View");
	frame();
	EXPECT_FALSE(saysNoCamera());

	SceneSystem::Destroy(cameraObject);
	SceneSystem::EndFrame();
	runtimeView.Get()->OnTick(0.016f);
	context.Update();
	EXPECT_TRUE(saysNoCamera());

	PanelManager::ClosePanel(sceneView);
	PanelManager::ClosePanel(runtimeView);
	PanelManager::TickAll(0.016f);
	SceneSystem::Terminate();
}

TEST_F(EditorPanelsTest, BrowserHeaderSpansPanel) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);

	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("AssetBrowserPanel"));
	ASSERT_TRUE(panel.IsValid());
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	UI::ElementHandle headerHost = NthChildOf(context, root, 0);
	ASSERT_TRUE(context.IsAlive(headerHost));
	UI::ElementHandle header = NthChildOf(context, headerHost, 0);
	ASSERT_TRUE(context.IsAlive(header));
	EXPECT_TRUE(context.HasClass(header, EditorTheme::Class::PanelHeader));

	UI::ElementHandle split = NthChildOf(context, root, 1);
	ASSERT_TRUE(context.IsAlive(split));
	UI::ElementHandle folderPane = NthChildOf(context, split, 0);
	ASSERT_TRUE(context.IsAlive(folderPane));

	const UI::Rect& rootBox = context.GetLayout(root).content;
	const UI::Rect& headerBox = context.GetLayout(header).borderBox;
	const UI::Rect& folderBox = context.GetLayout(folderPane).borderBox;
	ASSERT_GT(folderBox.w, 0.0f);
	EXPECT_NEAR(headerBox.x, rootBox.x, 0.5f);
	EXPECT_GT(headerBox.x + headerBox.w, folderBox.x + folderBox.w);

	const UI::Rect& hostBox = context.GetLayout(headerHost).borderBox;
	const UI::Rect& splitBox = context.GetLayout(split).borderBox;
	EXPECT_NEAR(hostBox.h + splitBox.h, rootBox.h, 1.0f);

	const u32 headerChildren = ChildCountOf(context, header);
	ASSERT_GT(headerChildren, 1u);
	UI::ElementHandle magnifier = NthChildOf(context, header, headerChildren - 1);
	ASSERT_TRUE(context.IsAlive(magnifier));
	ClickElement(context, magnifier);

	UI::ElementHandle rebuilt = NthChildOf(context, headerHost, 0);
	ASSERT_TRUE(context.IsAlive(rebuilt));
	EXPECT_TRUE(context.HasClass(rebuilt, EditorTheme::Class::PanelHeader));
	EXPECT_TRUE(context.IsAlive(FindByClass(context, rebuilt, StringHash("slt-textfield"), 0)));
	const UI::Rect& rebuiltBox = context.GetLayout(rebuilt).borderBox;
	EXPECT_NEAR(rebuiltBox.x, rootBox.x, 0.5f);
	EXPECT_GT(rebuiltBox.x + rebuiltBox.w, folderBox.x + folderBox.w);
}

TEST_F(EditorPanelsTest, ViewportPanelsTickWithoutADevice) {
	PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	PanelManager::OpenPanel(FindPanelType("RuntimeViewPanel"));
	for (int i = 0; i < 3; ++i) {
		PanelManager::TickAll(0.016f);
		PanelManager::SubmitViews(0.016f);
	}
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 2u);
}

TEST_F(EditorPanelsTest, SceneViewCameraStateIsPersistenceSchema) {
	const TypeInfo* sceneView = FindPanelType("SceneViewPanel");
	EXPECT_NE(FindField(sceneView, "cameraPosition"_hash), nullptr);
	EXPECT_NE(FindField(sceneView, "cameraYawDeg"_hash), nullptr);
	EXPECT_NE(FindField(sceneView, "cameraPitchDeg"_hash), nullptr);
	EXPECT_NE(FindField(sceneView, "flySpeed"_hash), nullptr);
	EXPECT_NE(FindField(sceneView, "showGrid"_hash), nullptr);
	EXPECT_NE(FindField(sceneView, "gizmoMode"_hash), nullptr);
	EXPECT_NE(FindField(sceneView, "shadingMode"_hash), nullptr);
}

TEST_F(EditorPanelsTest, SceneViewShadingDropdownWritesPersistedMode) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(panel.IsValid());
	const FieldInfo* field = FindField(panel->GetType(), "shadingMode"_hash);
	ASSERT_NE(field, nullptr);

	auto readMode = [&]() {
		Variant value;
		field->getFn(panel.Get(), &value);
		const i32* mode = value.TryGet<i32>();
		return mode ? *mode : -1;
	};
	ASSERT_EQ(readMode(), 0);

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel->BuildContent(context, root);
	context.Update();

	UI::ElementHandle dropdown = FindByClass(context, root, StringHash("slt-dropdown"), 0);
	ASSERT_TRUE(dropdown.IsValid());

	auto pick = [&](u32 row) {
		ClickElement(context, dropdown);
		ASSERT_EQ(context.GetOpenPopupCount(), 1u);
		UI::ElementHandle item = FindByClass(context, context.GetOpenPopup(0), StringHash("slt-menu-item"), row);
		ASSERT_TRUE(item.IsValid()) << row;
		ClickElement(context, item);
	};

	pick(1);
	EXPECT_EQ(readMode(), 1);
	EXPECT_EQ(EditorViewportOps::GetShadingMode(readMode()).fill, ViewFill::Wireframe);
	EXPECT_EQ(EditorViewportOps::GetShadingMode(readMode()).debugView, DebugView::None);

	pick(3);
	EXPECT_EQ(readMode(), 3);
	EXPECT_EQ(EditorViewportOps::GetShadingMode(readMode()).debugView, DebugView::Normals);
	EXPECT_EQ(EditorViewportOps::GetShadingMode(readMode()).fill, ViewFill::Solid);
}

TEST_F(EditorPanelsTest, ShadingMenuKeeps3Indices) {
	ASSERT_GE(EditorViewportOps::GetShadingModeCount(), 5u);
	EXPECT_EQ(EditorViewportOps::GetShadingMode(0).fill, ViewFill::Solid);
	EXPECT_EQ(EditorViewportOps::GetShadingMode(1).fill, ViewFill::Wireframe);
	EXPECT_EQ(EditorViewportOps::GetShadingMode(2).fill, ViewFill::SolidAndWireframe);
	for (i32 index = 0; index < 3; ++index) {
		EXPECT_EQ(EditorViewportOps::GetShadingMode(index).debugView, DebugView::None);
	}
	for (u32 index = 0; index < EditorViewportOps::GetShadingModeCount(); ++index) {
		EXPECT_FALSE(EditorViewportOps::GetShadingMode(static_cast<i32>(index)).label.empty()) << index;
	}

	EXPECT_EQ(EditorViewportOps::GetShadingMode(-1).fill, ViewFill::Solid);
	EXPECT_EQ(EditorViewportOps::GetShadingMode(-1).debugView, DebugView::None);
	const i32 pastEnd = static_cast<i32>(EditorViewportOps::GetShadingModeCount());
	EXPECT_EQ(EditorViewportOps::GetShadingMode(pastEnd).fill, ViewFill::Solid);
	EXPECT_EQ(EditorViewportOps::GetShadingMode(pastEnd).debugView, DebugView::None);
}

TEST_F(EditorPanelsTest, ToolbarControlsDontWrap) {
	const FieldInfo* field = FindField(FindPanelType("SceneViewPanel"), "shadingMode"_hash);
	ASSERT_NE(field, nullptr);

	f32 barHeight = 0.0f;
	auto measure = [&](f32 widthPx, f32& outButton, f32& outDropdown) {
		Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
		Variant value(static_cast<i32>(ViewFill::SolidAndWireframe));
		field->setFn(panel.Get(), &value);

		MockFontSystem fonts;
		UI::ContextDesc desc;
		desc.sizePx = {static_cast<i32>(widthPx), 400};
		UI::Context context(desc, &fonts);
		EditorTheme::ApplyToContext(context);
		UI::ElementHandle root = context.CreateElement();
		context.AppendChild(context.GetRoot(), root);
		panel->BuildContent(context, root);
		context.Update();

		const UI::Element* pod = context.GetElement(root);
		UI::ElementHandle bar = context.MakeHandle(pod->firstChild);
		outButton = context.GetLayout(FindByClass(context, bar, EditorTheme::Class::ToolButton, 0)).borderBox.w;
		outDropdown = context.GetLayout(FindByClass(context, bar, StringHash("slt-dropdown"), 0)).borderBox.w;
		barHeight = context.GetLayout(bar).borderBox.h;
		PanelManager::ClosePanel(panel);
	};

	f32 roomyButton = 0.0f;
	f32 roomyDropdown = 0.0f;
	measure(4000.0f, roomyButton, roomyDropdown);
	ASSERT_GT(roomyButton, 0.0f);
	ASSERT_GT(roomyDropdown, 0.0f);
	const f32 roomyBar = barHeight;
	ASSERT_GT(roomyBar, 0.0f);

	f32 crampedButton = 0.0f;
	f32 crampedDropdown = 0.0f;
	measure(520.0f, crampedButton, crampedDropdown);
	EXPECT_FLOAT_EQ(crampedButton, roomyButton);
	EXPECT_FLOAT_EQ(crampedDropdown, roomyDropdown);

	EXPECT_FLOAT_EQ(barHeight, roomyBar);
}

TEST_F(EditorPanelsTest, SceneViewCameraRoundTripsThroughRestore) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(panel.IsValid());
	const TypeInfo* type = panel.Get()->GetType();

	Variant yawIn(123.5f);
	SetField(panel.Get(), type, "cameraYawDeg"_hash, &yawIn);
	Variant speedIn(42.0f);
	SetField(panel.Get(), type, "flySpeed"_hash, &speedIn);
	Variant gridIn(false);
	SetField(panel.Get(), type, "showGrid"_hash, &gridIn);
	Variant effectsIn(false);
	SetField(panel.Get(), type, "showEffects"_hash, &effectsIn);
	Variant refreshIn(true);
	SetField(panel.Get(), type, "alwaysRefresh"_hash, &refreshIn);
	Variant postIn(false);
	SetField(panel.Get(), type, "showPostProcessing"_hash, &postIn);
	Variant modeIn(static_cast<i32>(2));
	SetField(panel.Get(), type, "gizmoMode"_hash, &modeIn);
	Variant positionIn(Vec3(1.0f, 2.0f, 3.0f));
	SetField(panel.Get(), type, "cameraPosition"_hash, &positionIn);
	str state = TextSerializer::Serialize(panel.Get(), type);
	u32 serial = panel.Get()->GetInstanceSerial();
	PanelManager::ClosePanel(panel);
	PanelManager::TickAll(0.0f);

	Handle<EditorPanel> restored = PanelManager::RestorePanel(type, serial, state);
	ASSERT_TRUE(restored.IsValid());
	EXPECT_EQ(restored.Get()->GetInstanceSerial(), serial);
	Variant yaw, speed, grid, effects, refresh, post, mode, position;
	ASSERT_TRUE(GetField(restored.Get(), type, "cameraYawDeg"_hash, &yaw));
	ASSERT_TRUE(GetField(restored.Get(), type, "flySpeed"_hash, &speed));
	ASSERT_TRUE(GetField(restored.Get(), type, "showGrid"_hash, &grid));
	ASSERT_TRUE(GetField(restored.Get(), type, "showEffects"_hash, &effects));
	ASSERT_TRUE(GetField(restored.Get(), type, "alwaysRefresh"_hash, &refresh));
	ASSERT_TRUE(GetField(restored.Get(), type, "showPostProcessing"_hash, &post));
	ASSERT_TRUE(GetField(restored.Get(), type, "gizmoMode"_hash, &mode));
	ASSERT_TRUE(GetField(restored.Get(), type, "cameraPosition"_hash, &position));
	EXPECT_FLOAT_EQ(yaw.As<f32>(), 123.5f);
	EXPECT_FLOAT_EQ(speed.As<f32>(), 42.0f);
	EXPECT_FALSE(grid.As<bool>());
	EXPECT_FALSE(effects.As<bool>());
	EXPECT_TRUE(refresh.As<bool>());
	EXPECT_FALSE(post.As<bool>());
	EXPECT_EQ(mode.As<i32>(), 2);
	EXPECT_EQ(position.As<Vec3>(), Vec3(1.0f, 2.0f, 3.0f));
}

#ifdef _WIN32
TEST_F(EditorPanelsTest, LookOwnsPointerUntilRelease) {
	WindowSystem::Initialize();
	Handle<Window> window = WindowSystem::Create([] {
		WindowDesc desc;
		desc.title = "SceneViewLook";
		desc.clientSize = {320, 240};
		desc.flags = WindowFlags::Hidden;
		return desc;
	}());
	ASSERT_TRUE(window.IsValid());
	HWND hwnd = static_cast<HWND>(window->GetNativeHandle().hwnd);

	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(panel.IsValid());
	MockFontSystem fonts;
	UI::ContextDesc contextDesc;
	contextDesc.sizePx = {800, 600};
	UI::Context context(contextDesc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	UI::ElementHandle viewport = FindByClass(context, root, EditorTheme::Class::ViewportPlaceholder, 0);
	ASSERT_TRUE(context.IsAlive(viewport));
	ClickElement(context, viewport);
	context.SetFocus(viewport);
	ASSERT_TRUE(context.GetFocused().IsValid());
	Vec2 inside = CenterOf(context, viewport);
	InputEvent move{};
	move.type = InputEventType::MouseMove;
	move.mousePosPx = {static_cast<i32>(inside.x), static_cast<i32>(inside.y)};
	context.InjectInput(move);
	context.Update();
	EXPECT_EQ(context.GetPointerClaimMode(), CursorMode::Normal);

	::PostMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	ASSERT_TRUE(Input::WasMouseButtonPressed(MouseButton::Right));
	panel.Get()->OnSubmitView(0.016f);

	EXPECT_EQ(context.GetPointerClaimMode(), CursorMode::Wrapped);
	EXPECT_FALSE(context.GetFocused().IsValid());

	::PostMessageW(hwnd, WM_MOUSEMOVE, MK_RBUTTON, MAKELPARAM(40, 40));
	WindowSystem::PumpEvents();
	panel.Get()->OnSubmitView(0.016f);
	EXPECT_EQ(context.GetPointerClaimMode(), CursorMode::Wrapped);

	::PostMessageW(hwnd, WM_RBUTTONUP, 0, MAKELPARAM(40, 40));
	WindowSystem::PumpEvents();
	ASSERT_FALSE(Input::IsMouseButtonDown(MouseButton::Right));
	panel.Get()->OnSubmitView(0.016f);
	EXPECT_EQ(context.GetPointerClaimMode(), CursorMode::Normal);

	EXPECT_EQ(context.GetFocused(), viewport);
	const TypeInfo* type = panel.Get()->GetType();
	Variant mode;
	ASSERT_TRUE(GetField(panel.Get(), type, "gizmoMode"_hash, &mode));
	ASSERT_EQ(mode.As<i32>(), 0);
	PressKey(context, Key::E);
	ASSERT_TRUE(GetField(panel.Get(), type, "gizmoMode"_hash, &mode));
	EXPECT_EQ(mode.As<i32>(), 1);

	WindowSystem::Terminate();
}

TEST_F(EditorPanelsTest, LookBlockedByPopup) {
	WindowSystem::Initialize();
	Handle<Window> window = WindowSystem::Create([] {
		WindowDesc desc;
		desc.title = "SceneViewLookModal";
		desc.clientSize = {320, 240};
		desc.flags = WindowFlags::Hidden;
		return desc;
	}());
	ASSERT_TRUE(window.IsValid());
	HWND hwnd = static_cast<HWND>(window->GetNativeHandle().hwnd);

	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(panel.IsValid());
	const TypeInfo* type = panel.Get()->GetType();
	MockFontSystem fonts;
	UI::ContextDesc contextDesc;
	contextDesc.sizePx = {800, 600};
	UI::Context context(contextDesc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	UI::ElementHandle viewport = FindByClass(context, root, EditorTheme::Class::ViewportPlaceholder, 0);
	ASSERT_TRUE(context.IsAlive(viewport));
	Vec2 inside = CenterOf(context, viewport);
	InputEvent move{};
	move.type = InputEventType::MouseMove;
	move.mousePosPx = {static_cast<i32>(inside.x), static_cast<i32>(inside.y)};
	context.InjectInput(move);
	context.Update();

	UI::ElementHandle card = context.CreateElement();
	context.OpenPopup(card, {}, UI::PopupFlags::NoOutsideDismiss | UI::PopupFlags::Center | UI::PopupFlags::Scrim);
	context.Update();

	Variant yawBefore;
	ASSERT_TRUE(GetField(panel.Get(), type, "cameraYawDeg"_hash, &yawBefore));

	::PostMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	ASSERT_TRUE(Input::WasMouseButtonPressed(MouseButton::Right));
	panel.Get()->OnSubmitView(0.016f);
	EXPECT_EQ(context.GetPointerClaimMode(), CursorMode::Normal);

	::PostMessageW(hwnd, WM_MOUSEMOVE, MK_RBUTTON, MAKELPARAM(120, 90));
	WindowSystem::PumpEvents();
	panel.Get()->OnSubmitView(0.016f);
	Variant yawUnderDialog;
	ASSERT_TRUE(GetField(panel.Get(), type, "cameraYawDeg"_hash, &yawUnderDialog));
	EXPECT_FLOAT_EQ(yawUnderDialog.As<f32>(), yawBefore.As<f32>());

	::PostMessageW(hwnd, WM_RBUTTONUP, 0, MAKELPARAM(120, 90));
	WindowSystem::PumpEvents();
	panel.Get()->OnSubmitView(0.016f);
	context.ClosePopup(card);
	context.Update();

	::PostMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	panel.Get()->OnSubmitView(0.016f);
	EXPECT_EQ(context.GetPointerClaimMode(), CursorMode::Wrapped);

	::PostMessageW(hwnd, WM_RBUTTONUP, 0, MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	panel.Get()->OnSubmitView(0.016f);
	WindowSystem::Terminate();
}
#endif

TEST_F(EditorPanelsTest, SpaceToggleByButtonOrHotkey) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(panel.IsValid());
	const TypeInfo* type = panel.Get()->GetType();

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	Variant space;
	ASSERT_TRUE(GetField(panel.Get(), type, "gizmoSpace"_hash, &space));
	ASSERT_EQ(space.As<i32>(), 0);

	UI::ElementHandle strip = FindByClass(context, root, EditorTheme::Class::PanelHeader, 0);
	ASSERT_TRUE(strip.IsValid());
	auto toolButton = [&](u32 index) {
		return FindByClass(context, strip, EditorTheme::Class::ToolButton, index);
	};
	ASSERT_EQ(CountByClass(context, strip, EditorTheme::Class::ToolButton), 11u);
	auto facePicture = [&](UI::ElementHandle button) {
		const UI::Element* pod = context.GetElement(button);
		return pod && pod->firstChild != UI::INVALID_UI_INDEX
				   ? context.GetIconAsset(context.MakeHandle(pod->firstChild))
				   : Handle<VectorAsset>{};
	};

	const Handle<VectorAsset> localFace = facePicture(toolButton(3));
	ASSERT_TRUE(localFace.IsValid());

	ClickElement(context, toolButton(3));
	panel.Get()->OnTick(0.0f);
	context.Update();
	ASSERT_TRUE(GetField(panel.Get(), type, "gizmoSpace"_hash, &space));
	EXPECT_EQ(space.As<i32>(), 1);
	EXPECT_NE(facePicture(toolButton(3)), localFace);

	UI::ElementHandle viewport = FindByClass(context, root, EditorTheme::Class::ViewportPlaceholder, 0);
	ASSERT_TRUE(context.IsAlive(viewport));
	ClickElement(context, viewport);
	PressKey(context, Key::X);
	ASSERT_TRUE(GetField(panel.Get(), type, "gizmoSpace"_hash, &space));
	EXPECT_EQ(space.As<i32>(), 0);

	panel.Get()->OnTick(0.0f);
	context.Update();
	EXPECT_EQ(facePicture(toolButton(3)), localFace);
}

TEST_F(EditorPanelsTest, OneTransformToolChecked) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	auto checkedTool = [&]() {
		i32 found = -1;
		for (u32 i = 0; i < 3; ++i) {
			UI::ElementHandle button = FindByClass(context, root, EditorTheme::Class::ToolButton, i);
			if (context.IsAlive(button) && context.HasClass(button, EditorTheme::Class::ToolButtonOn)) {
				EXPECT_EQ(found, -1);
				found = static_cast<i32>(i);
			}
		}
		return found;
	};
	EXPECT_EQ(checkedTool(), 0);

	UI::ElementHandle viewport = FindByClass(context, root, EditorTheme::Class::ViewportPlaceholder, 0);
	ASSERT_TRUE(context.IsAlive(viewport));
	ClickElement(context, viewport);
	PressKey(context, Key::E);
	panel.Get()->OnTick(0.0f);
	context.Update();
	EXPECT_EQ(checkedTool(), 1);

	ClickElement(context, FindByClass(context, root, EditorTheme::Class::ToolButton, 2));
	panel.Get()->OnTick(0.0f);
	context.Update();
	EXPECT_EQ(checkedTool(), 2);
}

TEST_F(EditorPanelsTest, SceneViewGridButtonWritesPersistedField) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(panel.IsValid());
	const TypeInfo* type = panel.Get()->GetType();

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	auto gridButton = [&]() { return FindByClass(context, root, EditorTheme::Class::ToolButton, 5); };
	Variant grid;
	ASSERT_TRUE(GetField(panel.Get(), type, "showGrid"_hash, &grid));
	ASSERT_TRUE(grid.As<bool>());
	EXPECT_TRUE(context.HasClass(gridButton(), EditorTheme::Class::ToolButtonOn));

	ClickElement(context, gridButton());
	panel.Get()->OnTick(0.0f);
	context.Update();
	ASSERT_TRUE(GetField(panel.Get(), type, "showGrid"_hash, &grid));
	EXPECT_FALSE(grid.As<bool>());
	EXPECT_FALSE(context.HasClass(gridButton(), EditorTheme::Class::ToolButtonOn));
}

TEST_F(EditorPanelsTest, SceneViewEffectsButtonWritesPersistedField) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(panel.IsValid());
	const TypeInfo* type = panel.Get()->GetType();

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	auto effectsButton = [&]() { return FindByClass(context, root, EditorTheme::Class::ToolButton, 8); };
	Variant effects;
	ASSERT_TRUE(GetField(panel.Get(), type, "showEffects"_hash, &effects));
	ASSERT_TRUE(effects.As<bool>());
	EXPECT_TRUE(context.HasClass(effectsButton(), EditorTheme::Class::ToolButtonOn));

	ClickElement(context, effectsButton());
	panel.Get()->OnTick(0.0f);
	context.Update();
	ASSERT_TRUE(GetField(panel.Get(), type, "showEffects"_hash, &effects));
	EXPECT_FALSE(effects.As<bool>());
	EXPECT_FALSE(context.HasClass(effectsButton(), EditorTheme::Class::ToolButtonOn));

	ClickElement(context, effectsButton());
	panel.Get()->OnTick(0.0f);
	context.Update();
	ASSERT_TRUE(GetField(panel.Get(), type, "showEffects"_hash, &effects));
	EXPECT_TRUE(effects.As<bool>());
}

TEST_F(EditorPanelsTest, EffectsDropdownSwitchesCategories) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(panel.IsValid());
	const TypeInfo* type = panel.Get()->GetType();

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	auto field = [&](StringHash name) {
		Variant value;
		EXPECT_TRUE(GetField(panel.Get(), type, name, &value));
		return value.As<bool>();
	};
	ASSERT_FALSE(field("alwaysRefresh"_hash));
	ASSERT_TRUE(field("showPostProcessing"_hash));

	auto pick = [&](std::string_view row) {
		ClickElement(context, FindByClass(context, root, EditorTheme::Class::ToolButton, 9));
		context.Update();
		UI::ElementHandle item = FindByText(context, context.GetRoot(), row);
		EXPECT_TRUE(item.IsValid()) << row << " row";
		if (item.IsValid()) {
			ClickElement(context, item);
		}
		panel.Get()->OnTick(0.0f);
		context.Update();
	};
	pick("Always Refresh");
	EXPECT_TRUE(field("alwaysRefresh"_hash));
	EXPECT_TRUE(field("showPostProcessing"_hash));
	EXPECT_TRUE(field("showEffects"_hash));

	pick("Post Processing");
	EXPECT_FALSE(field("showPostProcessing"_hash));
	EXPECT_TRUE(field("alwaysRefresh"_hash));

	pick("Always Refresh");
	EXPECT_FALSE(field("alwaysRefresh"_hash));
}

TEST_F(EditorPanelsTest, GizmosMenuSwitchesOutline) {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / ("shalltear_gizmos_menu_" + std::to_string(::GetCurrentProcessId()));
	std::filesystem::create_directories(root);
	SettingsPaths paths;
	paths.project = Path((root / "ProjectSettings.ini").generic_string());
	paths.user = Path((root / "UserSettings.ini").generic_string());
	paths.userProject = Path((root / "UserProjectSettings.ini").generic_string());
	Settings::Initialize(paths);
	auto outlineShown = []() {
		const auto* live = Settings::DeclareCategory<EditorConfig::GeneralPreferences>("Editor/General", SettingScope::User);
		return live && live->showSelectionOutline;
	};
	ASSERT_TRUE(outlineShown());
	{
		Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
		ASSERT_TRUE(panel.IsValid());
		const TypeInfo* type = panel.Get()->GetType();
		MockFontSystem fonts;
		UI::ContextDesc desc;
		desc.sizePx = {800, 600};
		UI::Context context(desc, &fonts);
		UI::ElementHandle viewRoot = context.CreateElement();
		context.AppendChild(context.GetRoot(), viewRoot);
		panel.Get()->BuildContent(context, viewRoot);
		context.Update();

		auto pick = [&]() {
			ClickElement(context, FindByClass(context, viewRoot, EditorTheme::Class::ToolButton, 7));
			context.Update();
			UI::ElementHandle item = FindByText(context, context.GetRoot(), "Selection Outline");
			EXPECT_TRUE(item.IsValid());
			if (item.IsValid()) {
				ClickElement(context, item);
			}
			panel.Get()->OnTick(0.0f);
			context.Update();
		};
		auto gizmosShown = [&]() {
			Variant value;
			EXPECT_TRUE(GetField(panel.Get(), type, "showGizmos"_hash, &value));
			return value.As<bool>();
		};
		pick();
		EXPECT_FALSE(outlineShown());
		EXPECT_TRUE(gizmosShown());
		pick();
		EXPECT_TRUE(outlineShown());
		pick();
		ASSERT_FALSE(outlineShown());
		PanelManager::ClosePanel(panel);
		PanelManager::TickAll(0.0f);
	}
	Settings::Terminate();
	Settings::Initialize(paths);
	EXPECT_FALSE(outlineShown());
	Settings::Terminate();
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
}

TEST_F(EditorPanelsTest, EffectsDropdownPerViewPackages) {
	char owner = 0;
	ASSERT_TRUE(SceneViewEffects::Register("Test Effects", &owner));
	EXPECT_FALSE(SceneViewEffects::Register("Test Effects", &owner));
	EXPECT_FALSE(SceneViewEffects::Register("Unowned Effects", nullptr));
	EXPECT_FALSE(SceneViewEffects::IsShown("Test Effects"));
	EXPECT_FALSE(SceneViewEffects::IsShown("Never Registered"));

	Handle<EditorPanel> first = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(first.IsValid());
	const TypeInfo* type = first.Get()->GetType();
	EXPECT_TRUE(SceneViewEffects::IsShown("Test Effects"));

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	first.Get()->BuildContent(context, root);
	context.Update();
	const auto pick = [&](std::string_view label) {
		ClickElement(context, FindByClass(context, root, EditorTheme::Class::ToolButton, 9));
		context.Update();
		UI::ElementHandle item = FindByText(context, context.GetRoot(), label);
		const bool found = item.IsValid();
		if (found) {
			ClickElement(context, item);
		} else {
			context.CloseAllPopups();
		}
		first.Get()->OnTick(0.0f);
		context.Update();
		return found;
	};
	ASSERT_TRUE(pick("Test Effects"));
	EXPECT_FALSE(SceneViewEffects::IsShown("Test Effects"));
	Variant master;
	ASSERT_TRUE(GetField(first.Get(), type, "showEffects"_hash, &master));
	EXPECT_TRUE(master.As<bool>());

	Handle<EditorPanel> second = PanelManager::OpenPanel(FindPanelType("SceneViewPanel"));
	ASSERT_TRUE(second.IsValid());
	EXPECT_TRUE(SceneViewEffects::IsShown("Test Effects"));
	Variant off(false);
	ASSERT_TRUE(SetField(second.Get(), type, "showEffects"_hash, &off));
	EXPECT_FALSE(SceneViewEffects::IsShown("Test Effects"));

	const str state = TextSerializer::Serialize(first.Get(), type);
	const u32 serial = first.Get()->GetInstanceSerial();
	PanelManager::ClosePanel(first);
	PanelManager::TickAll(0.0f);
	ASSERT_TRUE(SetField(second.Get(), type, "showEffects"_hash, &master));
	Handle<EditorPanel> restored = PanelManager::RestorePanel(type, serial, state);
	ASSERT_TRUE(restored.IsValid());
	PanelManager::ClosePanel(second);
	PanelManager::TickAll(0.0f);
	EXPECT_FALSE(SceneViewEffects::IsShown("Test Effects"));

	EXPECT_EQ(SceneViewEffects::UnregisterOwnedBy(&owner), 1u);
	Vector<str> labels{GetMemoryResource(MemTag::Editor)};
	SceneViewEffects::GetLabels(labels);
	EXPECT_TRUE(labels.empty());
	SceneViewEffects::Terminate();
}

TEST_F(EditorPanelsTest, HiddenCategorySkipsRenderers) {
	char owner = 0;
	const StringHash renderers[] = {StringHash("FirstEffect"), StringHash("SecondEffect")};
	ASSERT_TRUE(SceneViewEffects::Register("Hiding Effects", &owner, renderers));
	ASSERT_TRUE(SceneViewEffects::Register("Plain Effects", &owner));
	Vector<StringHash> hidden{GetMemoryResource(MemTag::Editor)};

	SceneViewEffects::GetHiddenRenderers([](std::string_view) { return true; }, hidden);
	EXPECT_TRUE(hidden.empty());
	SceneViewEffects::GetHiddenRenderers([](std::string_view label) { return label != "Hiding Effects"; }, hidden);
	ASSERT_EQ(hidden.size(), 2u);
	EXPECT_EQ(hidden[0], renderers[0]);
	EXPECT_EQ(hidden[1], renderers[1]);
	SceneViewEffects::GetHiddenRenderers([](std::string_view label) { return label != "Plain Effects"; }, hidden);
	EXPECT_TRUE(hidden.empty());

	EXPECT_EQ(SceneViewEffects::UnregisterOwnedBy(&owner), 2u);
	SceneViewEffects::GetHiddenRenderers([](std::string_view) { return false; }, hidden);
	EXPECT_TRUE(hidden.empty());
	SceneViewEffects::Terminate();
}

TEST_F(EditorPanelsTest, VisibleConsoleClearsNotice) {
	EditorConsole::Initialize();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);

	EditorConsole::Push(LogLevel::Error, "PanelTest", "arrived before the panel built", ConsoleOrigin{});
	PanelManager::TickAll(0.016f);
	EXPECT_EQ(EditorConsole::GetSummary(LogLevel::Info).unseenErrors, 1u);

	panel.Get()->BuildContent(context, root);
	context.Update();
	PanelManager::TickAll(0.016f);
	EXPECT_EQ(EditorConsole::GetSummary(LogLevel::Info).unseenErrors, 0u);

	context.ClearChildren(root);
	context.Update();
	EditorConsole::Push(LogLevel::Error, "PanelTest", "arrived while the tab was behind", ConsoleOrigin{});
	PanelManager::TickAll(0.016f);
	ConsoleSummary hidden = EditorConsole::GetSummary(LogLevel::Info);
	EXPECT_EQ(hidden.unseenErrors, 1u);
	EXPECT_EQ(hidden.newestUnseenError.message, "arrived while the tab was behind");

	panel.Get()->BuildContent(context, root);
	context.Update();
	PanelManager::TickAll(0.016f);
	EXPECT_EQ(EditorConsole::GetSummary(LogLevel::Info).unseenErrors, 0u);

	EditorConsole::Terminate();
}

namespace {

	str SwitchCount(UI::Context& context, UI::ElementHandle button) {
		const UI::Element* pod = context.GetElement(button);
		if (!pod || pod->lastChild == UI::INVALID_UI_INDEX) {
			return {};
		}
		return str(context.GetTextString(context.MakeHandle(pod->lastChild)));
	}

	bool IsLit(UI::Context& context, UI::ElementHandle button) {
		return context.HasClass(button, EditorTheme::Class::ToolButtonOn);
	}

} // anonymous namespace

TEST_F(EditorPanelsTest, ConsoleSwitchesCountHidden) {
	EditorConsole::Initialize();
	EditorConsole::Clear();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	ASSERT_TRUE(panel.IsValid());
	const TypeInfo* type = panel.Get()->GetType();

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);

	const char* words[] = {"a trace", "a debug", "an info", "a warning", "an error", "a fatal"};
	for (u32 level = 0; level < 6; ++level) {
		EditorConsole::Push(static_cast<LogLevel>(level), "PanelTest", words[level], ConsoleOrigin{});
	}
	panel.Get()->BuildContent(context, root);
	context.Update();
	PanelManager::TickAll(0.016f);
	context.Update();
	auto shows = [&](const char* word) {
		return context.IsAlive(FindByText(context, root, str("[PanelTest] ") + word));
	};
	for (const char* word : words) {
		EXPECT_TRUE(shows(word)) << word << "'";
	}

	UI::ElementHandle trace = FindByClass(context, root, EditorTheme::Class::ToolButton, 0);
	UI::ElementHandle messages = FindByClass(context, root, EditorTheme::Class::ToolButton, 1);
	UI::ElementHandle warnings = FindByClass(context, root, EditorTheme::Class::ToolButton, 2);
	UI::ElementHandle errors = FindByClass(context, root, EditorTheme::Class::ToolButton, 3);
	ASSERT_TRUE(context.IsAlive(errors));
	EXPECT_EQ(SwitchCount(context, trace), "2");
	EXPECT_EQ(SwitchCount(context, messages), "1");
	EXPECT_EQ(SwitchCount(context, warnings), "1");
	EXPECT_EQ(SwitchCount(context, errors), "2");
	EXPECT_TRUE(IsLit(context, trace) && IsLit(context, messages) && IsLit(context, warnings) && IsLit(context, errors));

	ClickElement(context, warnings);
	PanelManager::TickAll(0.016f);
	context.Update();
	Variant value;
	ASSERT_TRUE(GetField(panel.Get(), type, "showWarnings"_hash, &value));
	EXPECT_FALSE(value.As<bool>());
	EXPECT_FALSE(shows("a warning"));
	EXPECT_TRUE(shows("an error"));
	EXPECT_FALSE(IsLit(context, warnings));
	EXPECT_EQ(SwitchCount(context, warnings), "1");

	ClickElement(context, messages);
	PanelManager::TickAll(0.016f);
	context.Update();
	EXPECT_FALSE(shows("an info"));
	EXPECT_TRUE(shows("a trace"));
	EXPECT_TRUE(shows("a fatal"));

	ClickElement(context, warnings);
	PanelManager::TickAll(0.016f);
	context.Update();
	EXPECT_TRUE(shows("a warning"));
	EXPECT_TRUE(IsLit(context, warnings));

	EditorConsole::Terminate();
}

TEST_F(EditorPanelsTest, OffConsoleSwitchStandsOnSwitchesWell) {
	EditorConsole::Initialize();
	EditorConsole::Clear();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 600};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();
	PanelManager::TickAll(0.016f);
	context.Update();

	UI::ElementHandle warnings = FindByClass(context, root, EditorTheme::Class::ToolButton, 2);
	ASSERT_TRUE(context.IsAlive(warnings));
	ClickElement(context, warnings);
	PanelManager::TickAll(0.016f);
	context.Update();
	ASSERT_FALSE(IsLit(context, warnings));

	const UI::ElementHandle well = context.MakeHandle(context.GetElement(warnings)->parent);
	EXPECT_TRUE(context.HasClass(well, EditorTheme::Class::SwitchGroup));
	EXPECT_GT(context.GetComputedStyle(well).background.solid.a, 0.0f);
	for (u32 index = 0; index < 4; ++index) {
		UI::ElementHandle button = FindByClass(context, root, EditorTheme::Class::ToolButton, index);
		ASSERT_TRUE(context.IsAlive(button));
		EXPECT_EQ(context.MakeHandle(context.GetElement(button)->parent), well) << "switch " << index;
	}

	EditorConsole::Terminate();
}

TEST_F(EditorPanelsTest, TraceSwitchHidesTraceAndDebug) {
	EditorConsole::Initialize();
	EditorConsole::Clear();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);

	EditorConsole::Push(LogLevel::Trace, "PanelTest", "a trace", ConsoleOrigin{});
	EditorConsole::Push(LogLevel::Debug, "PanelTest", "a debug", ConsoleOrigin{});
	EditorConsole::Push(LogLevel::Info, "PanelTest", "an info", ConsoleOrigin{});
	panel.Get()->BuildContent(context, root);
	context.Update();
	PanelManager::TickAll(0.016f);
	context.Update();
	auto shows = [&](const char* word) {
		return context.IsAlive(FindByText(context, root, str("[PanelTest] ") + word));
	};
	ASSERT_TRUE(shows("a trace") && shows("a debug") && shows("an info"));

	UI::ElementHandle trace = FindByClass(context, root, EditorTheme::Class::ToolButton, 0);
	ClickElement(context, trace);
	PanelManager::TickAll(0.016f);
	context.Update();
	EXPECT_FALSE(shows("a trace"));
	EXPECT_FALSE(shows("a debug"));
	EXPECT_TRUE(shows("an info"));
	EXPECT_FALSE(IsLit(context, trace));
	EXPECT_EQ(SwitchCount(context, trace), "2");

	Variant value;
	ASSERT_TRUE(GetField(panel.Get(), panel.Get()->GetType(), "showTrace"_hash, &value));
	EXPECT_FALSE(value.As<bool>());

	EditorConsole::Terminate();
}

TEST_F(EditorPanelsTest, ConsoleCountCapsAt999Plus) {
	EditorConsole::Initialize();
	EditorConsole::Clear();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	for (u32 i = 0; i < 999; ++i) {
		EditorConsole::Push(LogLevel::Info, "PanelTest", "line", ConsoleOrigin{});
	}
	PanelManager::TickAll(0.016f);
	context.Update();
	UI::ElementHandle messages = FindByClass(context, root, EditorTheme::Class::ToolButton, 1);
	EXPECT_EQ(SwitchCount(context, messages), "999");

	EditorConsole::Push(LogLevel::Info, "PanelTest", "one more", ConsoleOrigin{});
	PanelManager::TickAll(0.016f);
	context.Update();
	EXPECT_EQ(SwitchCount(context, messages), "999+");

	EditorConsole::Terminate();
}

TEST_F(EditorPanelsTest, EmptyConsoleSaysWhichEmptyItIs) {
	EditorConsole::Initialize();
	EditorConsole::Clear();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();
	auto frame = [&]() {
		context.Update();
		PanelManager::TickAll(0.016f);
		context.Update();
	};
	UI::ElementHandle scroll = FindByClass(context, root, StringHash("slt-scroll-root"), 0);
	ASSERT_TRUE(context.IsAlive(scroll));
	auto listShown = [&]() { return context.GetComputedStyle(scroll).display == UI::Display::Flex; };
	auto says = [&](std::string_view title) {
		UI::ElementHandle found = FindByText(context, root, title);
		if (!context.IsAlive(found)) {
			return false;
		}
		UI::ElementHandle host = context.MakeHandle(context.GetElement(found)->parent);
		return context.GetComputedStyle(host).display == UI::Display::Flex;
	};

	EXPECT_TRUE(says("No logs"));
	EXPECT_FALSE(listShown());

	EditorConsole::Push(LogLevel::Info, "PanelTest", "the first line", ConsoleOrigin{});
	frame();
	EXPECT_TRUE(listShown());
	EXPECT_FALSE(says("No logs"));
	EXPECT_TRUE(context.IsAlive(FindByText(context, root, "[PanelTest] the first line")));

	UI::ElementHandle field = FindByClass(context, root, StringHash("slt-textfield"), 0);
	ASSERT_TRUE(context.IsAlive(field));
	ClickElement(context, field);
	for (char32_t c : std::u32string_view(U"zzz")) {
		InputEvent typed{};
		typed.type = InputEventType::Text;
		typed.codepoint = c;
		context.InjectInput(typed);
	}
	auto press = [&](Key key) {
		InputEvent event{};
		event.type = InputEventType::KeyDown;
		event.key = key;
		context.InjectInput(event);
	};
	press(Key::Enter);
	frame();
	EXPECT_TRUE(says("No results for \"zzz\""));
	EXPECT_FALSE(listShown());

	ClickElement(context, FindByClass(context, root, StringHash("slt-textfield"), 0));
	for (u32 i = 0; i < 3; ++i) {
		press(Key::Backspace);
	}
	press(Key::Enter);
	frame();
	ASSERT_TRUE(listShown());
	ClickElement(context, FindByClass(context, root, EditorTheme::Class::ToolButton, 1));
	frame();
	EXPECT_TRUE(says("All logs are hidden"));
	EXPECT_FALSE(listShown());

	ClickElement(context, FindByClass(context, root, EditorTheme::Class::ToolButton, 1));
	frame();
	EXPECT_TRUE(listShown());

	EditorConsole::Terminate();
}

TEST_F(EditorPanelsTest, ConsoleRowIsExpandedByClickingMessage) {
	EditorConsole::Initialize();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 600};
	desc.doubleClickSeconds = -1.0f;
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);

	EditorConsole::Push(LogLevel::Info, "PanelTest", "a line worth opening", ConsoleOrigin{"Fixture.cpp", 42, 7});
	EditorConsole::Push(LogLevel::Info, "PanelTest", "another line", ConsoleOrigin{"Other.cpp", 3, 1});
	panel.Get()->BuildContent(context, root);
	context.Update();
	PanelManager::TickAll(0.016f);
	context.Update();

	UI::ElementHandle message = FindByText(context, root, "[PanelTest] a line worth opening");
	ASSERT_TRUE(context.IsAlive(message));
	const str source = "Fixture.cpp:42:7   (double-click to open)";
	ASSERT_FALSE(context.IsAlive(FindByText(context, root, source)));

	ClickElement(context, message);
	EXPECT_TRUE(context.IsAlive(FindByText(context, root, source)));
	EXPECT_TRUE(context.IsAlive(message));

	const str other = "Other.cpp:3:1   (double-click to open)";
	ClickElement(context, FindByText(context, root, "[PanelTest] another line"));
	EXPECT_TRUE(context.IsAlive(FindByText(context, root, other)));
	EXPECT_TRUE(context.IsAlive(FindByText(context, root, source)));

	ClickElement(context, FindByText(context, root, "[PanelTest] a line worth opening"));
	EXPECT_FALSE(context.IsAlive(FindByText(context, root, source)));
	EXPECT_TRUE(context.IsAlive(FindByText(context, root, other)));

	EditorConsole::Terminate();
}

TEST_F(EditorPanelsTest, DiagnosticOpensOnSlowDoubleClick) {
	EditorConsole::Initialize();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);

	EditorConsole::Push(LogLevel::Error, "Build", "expected ';' after expression", ConsoleOrigin{"Player.cpp", 12, 5});
	panel.Get()->BuildContent(context, root);
	context.Update();
	PanelManager::TickAll(0.016f);
	context.Update();

	UI::ElementHandle message = FindByText(context, root, "[Build] expected ';' after expression");
	ASSERT_TRUE(context.IsAlive(message));
	const UI::ElementHandle row = context.MakeHandle(context.GetElement(message)->parent);

	ClickElement(context, message);
	for (u32 frame = 0; frame < 3; ++frame) {
		PanelManager::TickAll(0.016f);
		context.Update();
	}
	ASSERT_TRUE(context.IsAlive(row));
	ASSERT_TRUE(static_cast<bool>(context.Events(row).onDoubleClick));
	u32 opened = 0;
	context.Events(row).onDoubleClick = [&opened]() { ++opened; };

	ClickElement(context, message);
	EXPECT_EQ(opened, 1u);

	EditorConsole::Terminate();
}

TEST_F(EditorPanelsTest, ExpandedRowShowsKnownLocation) {
	EditorConsole::Initialize();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(FindPanelType("ConsolePanel"));
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);

	EditorConsole::Push(LogLevel::Error, "PanelTest", "nowhere in particular", ConsoleOrigin{});
	EditorConsole::Push(LogLevel::Warn, "PanelTest", "a line and no column", ConsoleOrigin{"CMakeLists.txt", 82, 0});
	panel.Get()->BuildContent(context, root);
	context.Update();
	PanelManager::TickAll(0.016f);
	context.Update();

	auto expand = [&](std::string_view text) {
		UI::ElementHandle message = FindByText(context, root, str("[PanelTest] ") + str(text));
		ASSERT_TRUE(context.IsAlive(message)) << text;
		ClickElement(context, message);
		PanelManager::TickAll(0.016f);
		context.Update();
	};

	expand("nowhere in particular");
	EXPECT_FALSE(context.IsAlive(FindByTextContaining(context, root, ":0 ()")));

	expand("a line and no column");
	EXPECT_TRUE(context.IsAlive(FindByText(context, root, "CMakeLists.txt:82   (double-click to open)")));
	EXPECT_FALSE(context.IsAlive(FindByTextContaining(context, root, "CMakeLists.txt:82:0")));

	EditorConsole::Terminate();
}
