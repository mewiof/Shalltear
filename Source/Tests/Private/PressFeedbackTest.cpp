#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorConsole.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"

using namespace Shalltear;
using namespace UITest;

TEST(PressFeedbackTest, CoreWidgetsShowPressAtOnce) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 1000};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.GetRoot();
	UI::Style column;
	column.flexDirection = UI::FlexDirection::Column;
	column.alignItems = UI::Align::Start;
	context.SetStyle(root, column);
	Handle<FontAsset> font;

	UI::Widgets::ButtonDesc button;
	button.label = "Button";
	button.font = font;
	button.onClick = []() {};
	UI::Widgets::Button(context, root, button);

	UI::Widgets::Checkbox(context, root, false, [](bool) {});
	UI::Widgets::Toggle(context, root, false, [](bool) {});

	UI::Widgets::SliderDesc slider;
	slider.font = font;
	slider.onChange = [](f32) {};
	UI::Widgets::Slider(context, root, slider);

	UI::Widgets::NumberFieldDesc number;
	number.label = "X";
	number.font = font;
	number.onChange = [](f64) {};
	UI::Widgets::NumberField(context, root, number);

	UI::Widgets::ColorSwatchField(context, root, Color::White(), font, 12.0f, [](Color) {});

	const std::string_view items[] = {"One", "Two"};
	UI::Widgets::DropdownDesc dropdown;
	dropdown.items = {items, 2};
	dropdown.font = font;
	dropdown.onSelect = [](u32) {};
	UI::Widgets::Dropdown(context, root, dropdown);

	UI::Widgets::MaskDropdownDesc mask;
	mask.items = {items, 2};
	mask.font = font;
	mask.onChange = [](u64) {};
	UI::Widgets::MaskDropdown(context, root, mask);

	UI::Widgets::TreeRowDesc row;
	row.label = "Row";
	row.font = font;
	row.hasChildren = true;
	row.onSelect = [](KeyMod) {};
	row.onToggleExpand = []() {};
	UI::Widgets::TreeRow(context, root, row);

	UI::Widgets::TreeRowDesc selected = row;
	selected.label = "Selected";
	selected.selected = true;
	UI::Widgets::TreeRow(context, root, selected);

	UI::Widgets::MenuBarItemDesc menu;
	menu.label = "File";
	menu.fill = [](Vector<UI::Widgets::MenuItem>& out) {
		UI::Widgets::MenuItem item;
		item.label = "Item";
		out.push_back(std::move(item));
	};
	const UI::Widgets::MenuBarItemDesc menus[] = {menu};
	UI::Widgets::MenuBar(context, root, {menus, 1}, font, 12.0f);
	context.Update();

	PressWalk walk;
	WalkThePress(context, root, walk);
	EXPECT_EQ(walk.unreachable, 0u);
	EXPECT_TRUE(walk.Inspected(StringHash("slt-tree-row")));
	EXPECT_TRUE(walk.Inspected(StringHash("slt-checkbox")));
	EXPECT_TRUE(walk.Inspected(StringHash("slt-toggle")));
	EXPECT_TRUE(walk.offenders.empty()) << walk.Report();
}

TEST(PressFeedbackTest, AnOpenMenusRowsShowThePress) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 700};
	UI::Context context(desc, &fonts);

	Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Core)};
	for (std::string_view label : {"Open", "Close"}) {
		UI::Widgets::MenuItem item;
		item.label = str(label);
		item.onSelect = []() {};
		items.push_back(std::move(item));
	}
	UI::Widgets::OpenMenu(context, {10.0f, 10.0f}, {items.data(), items.size()}, Handle<FontAsset>{}, 12.0f);
	context.Update();
	ASSERT_GT(context.GetOpenPopupCount(), 0u);

	PressWalk walk;
	UI::ElementHandle content = context.GetOpenPopup(0);
	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Core)};
	for (u32 i = 0; i < 2; ++i) {
		rows.push_back(FindByClass(context, content, StringHash("slt-menu-item"), i));
	}
	for (UI::ElementHandle one : rows) {
		ASSERT_TRUE(context.IsAlive(one));
		PaintSnapshot hovered{GetMemoryResource(MemTag::Core)};
		PaintSnapshot pressed{GetMemoryResource(MemTag::Core)};
		const UI::Rect box = context.GetLayout(one).borderBox;
		const Vec2 point{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
		InputEvent event{};
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		event.type = InputEventType::MouseMove;
		context.InjectInput(event);
		context.Update();
		SnapshotPaint(context, one, hovered);
		event.type = InputEventType::MouseDown;
		context.InjectInput(event);
		context.Update();
		SnapshotPaint(context, one, pressed);
		EXPECT_TRUE(PaintDiffers(hovered, pressed));
		event.type = InputEventType::MouseUp;
		event.mousePosPx = {-5000, -5000};
		context.InjectInput(event);
		context.Update();
	}
}

TEST(PressFeedbackTest, DockTabShowsPressAtOnce) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {1200, 700};
	UI::Context context(desc, &fonts);
	UI::DockManager dock{context, Handle<FontAsset>{}};
	for (StringHash panel : {"panel.a"_hash, "panel.b"_hash}) {
		dock.RegisterPanel(panel, "Panel", [](UI::Context& c, UI::ElementHandle parent) {
			UI::ElementHandle body = c.CreateElement();
			c.AppendChild(parent, body);
		});
		dock.DockPanel(panel, dock.GetRootNode(), UI::DockZone::Center);
	}
	auto frame = [&]() {
		context.Update();
		dock.Build(context.GetRoot());
		context.Update();
	};
	dock.ActivatePanel("panel.a"_hash);
	dock.SetFocusedPanel("panel.a"_hash);
	frame();
	frame();

	PressWalk walk;
	WalkThePress(context, context.GetRoot(), walk, frame);
	EXPECT_TRUE(walk.Inspected(StringHash("dock-active-tab")));
	EXPECT_TRUE(walk.offenders.empty()) << walk.Report();
}

TEST(PressFeedbackTest, PanelsShowPressAtOnce) {
	TypeRegistry::InitializePrimitives();
	RegisterBuiltInPanels();
	SceneSystem::Initialize();
	EditorSelection::Initialize();
	PanelManager::Initialize();
	EditorConsole::Initialize();

	Handle<Scene> scene = SceneSystem::CreateScene("Pressed");
	Handle<SceneObject> parent = SceneSystem::CreateObject("Parent", scene);
	Handle<SceneObject> child = SceneSystem::CreateObject("Child", scene);
	child.Get()->SetParent(parent);
	Handle<SceneObject> off = SceneSystem::CreateObject("Off", scene);
	off.Get()->SetActive(false);
	EditorSelection::Select(Handle<Object>(parent.Get()));
	SLT_LOG_INFO("PressFeedbackTest", "an info line");
	SLT_LOG_WARN("PressFeedbackTest", "a warning line");
	SLT_LOG_ERROR("PressFeedbackTest", "an error line");

	Vector<const TypeInfo*> types{GetMemoryResource(MemTag::Editor)};
	PanelManager::GetPanelTypes(types);
	ASSERT_FALSE(types.empty());
#ifdef SHALLTEAR_SHARED
	const TypeInfo* known = TypeRegistry::FindType(StringHash("HierarchyPanel"));
	ASSERT_NE(known, nullptr);
	const void* editorImage = known->ownerModule;
	ASSERT_NE(editorImage, nullptr);
	std::erase_if(types, [editorImage](const TypeInfo* type) { return type->ownerModule != editorImage; });
	ASSERT_GT(types.size(), 3u);
#else
	EditorConsole::Terminate();
	PanelManager::Terminate();
	EditorSelection::Terminate();
	SceneSystem::Terminate();
	GTEST_SKIP();
#endif

	PressWalk walk;
	for (const TypeInfo* type : types) {
		const str typeName = PanelManager::GetPanelTypeName(type);
		Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
		ASSERT_TRUE(panel.IsValid()) << typeName;

		MockFontSystem fonts;
		UI::ContextDesc desc;
		desc.sizePx = {900, 900};
		UI::Context context(desc, &fonts);
		EditorTheme::ApplyToContext(context);

		UI::ElementHandle host = context.CreateElement();
		UI::Style fill;
		fill.width = UI::Length::Percent(1.0f);
		fill.height = UI::Length::Percent(1.0f);
		fill.flexDirection = UI::FlexDirection::Column;
		context.SetStyle(host, fill);
		context.AppendChild(context.GetRoot(), host);
		panel->BuildContent(context, host);
		context.Update();
		PanelManager::TickAll(0.0f);
		context.Update();

		PressWalk mine;
		WalkThePress(context, host, mine);
		walk.inspected += mine.inspected;
		walk.inspectedClasses.insert(walk.inspectedClasses.end(), mine.inspectedClasses.begin(), mine.inspectedClasses.end());
		for (const str& one : mine.offenders) {
			walk.offenders.push_back(typeName + ": " + one);
		}
		PanelManager::ClosePanel(panel);
		PanelManager::TickAll(0.0f);
	}
	EXPECT_TRUE(walk.Inspected(StringHash("slt-tree-row")));
	EXPECT_TRUE(walk.Inspected(StringHash("ed-tool-button")));
	EXPECT_TRUE(walk.offenders.empty()) << walk.Report();

	EditorConsole::Terminate();
	PanelManager::Terminate();
	EditorSelection::Terminate();
	SceneSystem::Terminate();
}
