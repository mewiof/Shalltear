#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"

using namespace Shalltear;
using namespace UITest;

TEST(CursorAffordanceTest, PressableCoreWidgetsDeclareCursor) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {900, 700};
	UI::Context context(desc, &fonts);

	UI::ElementHandle root = context.GetRoot();
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
	slider.showValueField = true;
	slider.onChange = [](f32) {};
	UI::Widgets::Slider(context, root, slider);

	UI::Widgets::TextFieldDesc text;
	text.text = "text";
	text.font = font;
	text.onCommit = [](std::string_view) {};
	UI::Widgets::TextField(context, root, text);

	UI::Widgets::NumberFieldDesc number;
	number.label = "X";
	number.font = font;
	number.onChange = [](f64) {};
	UI::Widgets::NumberField(context, root, number);

	UI::Widgets::VectorFieldDesc vector;
	vector.componentCount = 3;
	vector.font = font;
	vector.onChange = [](u32, f64) {};
	UI::Widgets::VectorField(context, root, vector);

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

	UI::Widgets::ScrollView(context, root);

	UI::Widgets::TreeRowDesc row;
	row.label = "Row";
	row.font = font;
	row.hasChildren = true;
	row.onSelect = [](KeyMod) {};
	row.onToggleExpand = []() {};
	UI::Widgets::TreeRow(context, root, row);

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

	Vector<str> offenders{GetMemoryResource(MemTag::Core)};
	CollectCursorOffenders(context, context.GetRoot(), offenders);
	EXPECT_TRUE(offenders.empty()) << ReportCursorOffenders(offenders);
}

TEST(CursorAffordanceTest, OpenMenusRowsDeclareCursorToo) {
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
	Vector<str> offenders{GetMemoryResource(MemTag::Core)};
	for (u32 i = 0; i < context.GetOpenPopupCount(); ++i) {
		CollectCursorOffenders(context, context.GetOpenPopup(i), offenders);
	}
	EXPECT_TRUE(offenders.empty()) << ReportCursorOffenders(offenders);
}

TEST(CursorAffordanceTest, PressableEditorPanelsDeclareCursor) {
	TypeRegistry::InitializePrimitives();
	RegisterBuiltInPanels();
	SceneSystem::Initialize();
	EditorSelection::Initialize();
	PanelManager::Initialize();

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
	GTEST_SKIP();
#endif

	Vector<str> offenders{GetMemoryResource(MemTag::Editor)};
	for (const TypeInfo* type : types) {
		const str typeName = PanelManager::GetPanelTypeName(type);
		Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
		ASSERT_TRUE(panel.IsValid()) << typeName;

		MockFontSystem fonts;
		UI::ContextDesc desc;
		desc.sizePx = {900, 700};
		UI::Context context(desc, &fonts);
		EditorTheme::ApplyToContext(context);

		UI::ElementHandle host = context.CreateElement();
		context.AppendChild(context.GetRoot(), host);
		panel->BuildContent(context, host);
		context.Update();

		Vector<str> mine{GetMemoryResource(MemTag::Editor)};
		CollectCursorOffenders(context, host, mine);
		for (const str& one : mine) {
			offenders.push_back(typeName + ": " + one);
		}
		PanelManager::ClosePanel(panel);
		PanelManager::TickAll(0.0f);
	}
	EXPECT_TRUE(offenders.empty()) << ReportCursorOffenders(offenders);

	PanelManager::Terminate();
	EditorSelection::Terminate();
	SceneSystem::Terminate();
}
