#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/UIBuilderOps.h"
#include "Shalltear/Editor/UIDocumentOps.h"
#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;
using namespace UITest;

namespace {

	struct Parsed {
		UIDocumentAsset document;
		str error;
		bool ok = false;

		explicit Parsed(std::string_view source) { ok = document.Parse(source, error); }
	};

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

	u32 IndexOfId(const UIDocumentAsset& document, std::string_view id) {
		Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
		UIBuilderOps::BuildRows(document, rows);
		for (const UIBuilderOps::Row& row : rows) {
			if (row.node->id == id) {
				return row.index;
			}
		}
		return UIBuilderOps::INVALID_NODE;
	}

	UI::ElementHandle FirstDescendantWithText(UI::Context& context, UI::ElementHandle from, std::string_view text) {
		if (context.GetTextString(from) == text) {
			return from;
		}
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			if (UI::ElementHandle found = FirstDescendantWithText(context, handle, text); found.IsValid()) {
				return found;
			}
			child = context.GetElement(handle)->nextSibling;
		}
		return {};
	}

	u32 CountDescendantsWithText(UI::Context& context, UI::ElementHandle from, std::string_view text) {
		u32 count = context.GetTextString(from) == text ? 1u : 0u;
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			count += CountDescendantsWithText(context, handle, text);
			child = context.GetElement(handle)->nextSibling;
		}
		return count;
	}

	u32 CountRowsByClass(UI::Context& context, UI::ElementHandle from, StringHash klass) {
		u32 count = context.HasClass(from, klass) ? 1u : 0u;
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			count += CountRowsByClass(context, handle, klass);
			child = context.GetElement(handle)->nextSibling;
		}
		return count;
	}

	void CollectClass(UI::Context& context, UI::ElementHandle from, StringHash klass,
					  Vector<UI::ElementHandle>& out) {
		if (context.HasClass(from, klass)) {
			out.push_back(from);
		}
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			CollectClass(context, handle, klass, out);
			child = context.GetElement(handle)->nextSibling;
		}
	}

	UI::ElementHandle RowLabelled(UI::Context& context, UI::ElementHandle from, std::string_view label) {
		UI::ElementHandle text = FirstDescendantWithText(context, from, label);
		for (UI::ElementHandle walk = text; context.IsAlive(walk);) {
			if (context.HasClass(walk, StringHash("slt-tree-row"))) {
				return walk;
			}
			const UI::Element* element = context.GetElement(walk);
			if (!element || element->parent == UI::INVALID_UI_INDEX) {
				break;
			}
			walk = context.MakeHandle(element->parent);
		}
		return {};
	}

	bool RowIsSelected(UI::Context& context, UI::ElementHandle row) {
		return context.IsAlive(row) && context.HasClass(row, StringHash("slt-selected"));
	}

	InputEvent Mouse(InputEventType type, Vec2 point) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		return event;
	}

	void ClickAt(UI::Context& context, Vec2 point) {
		context.InjectInput(Mouse(InputEventType::MouseMove, point));
		context.InjectInput(Mouse(InputEventType::MouseDown, point));
		context.InjectInput(Mouse(InputEventType::MouseUp, point));
		context.Update();
	}

	void RightClickAt(UI::Context& context, Vec2 point) {
		InputEvent move = Mouse(InputEventType::MouseMove, point);
		context.InjectInput(move);
		InputEvent down = Mouse(InputEventType::MouseDown, point);
		down.button = MouseButton::Right;
		context.InjectInput(down);
		InputEvent up = Mouse(InputEventType::MouseUp, point);
		up.button = MouseButton::Right;
		context.InjectInput(up);
		context.Update();
	}

	UI::ElementHandle ParentOf(UI::Context& context, UI::ElementHandle handle) {
		const UI::Element* element = context.GetElement(handle);
		return element && element->parent != UI::INVALID_UI_INDEX ? context.MakeHandle(element->parent)
																  : UI::ElementHandle{};
	}

	UI::ElementHandle PaneNamed(UI::Context& context, UI::ElementHandle from, std::string_view title) {
		return ParentOf(context, FirstDescendantWithText(context, from, title));
	}

	Vec2 CenterOf(UI::Context& context, UI::ElementHandle handle) {
		const UI::Rect& box = context.GetLayout(handle).borderBox;
		return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	}

	UI::ElementHandle FirstByClass(UI::Context& context, UI::ElementHandle from, StringHash klass) {
		if (context.HasClass(from, klass)) {
			return from;
		}
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			if (UI::ElementHandle found = FirstByClass(context, handle, klass); found.IsValid()) {
				return found;
			}
			child = context.GetElement(handle)->nextSibling;
		}
		return {};
	}

	class UIBuilderOpsTest : public ::testing::Test {
	protected:
		void SetUp() override { UI::WidgetRegistry::Shutdown(); }
		void TearDown() override { UI::WidgetRegistry::Shutdown(); }
	};

	class UIBuilderPanelFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mountedEngine = false;
		Handle<Window> _window;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_uibuilder_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));

			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			PanelManager::Initialize();
			UndoSystem::Initialize();

			WindowDesc windowDesc;
			windowDesc.title = "UIBuilderTest";
			windowDesc.clientSize = {1200, 800};
			windowDesc.flags = WindowFlags::Hidden;
			_window = WindowSystem::Create(windowDesc);
			UISystem::CreateContext(_window);
		}

		UI::Context& PanelContext() { return *UISystem::GetContext(_window); }

		void Frame(EditorPanel& panel) {
			panel.OnTick(0.0f);
			UISystem::Update();
		}

		void TearDown() override {
			UndoSystem::Terminate();
			PanelManager::Terminate();
			UISystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void WriteDocument(const str& virtualPath, const str& body) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(resolved, str("<ui version=\"1\">") + body + str("</ui>"));
		}

		void WriteSheet(const str& virtualPath, const str& source) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(resolved, source);
		}

		Handle<EditorPanel> OpenBuilder() {
			return PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("UIBuilderPanel")));
		}

		UI::ElementHandle BuildPanelInto(UI::Context& context, EditorPanel& panel) {
			UI::ElementHandle root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			UI::Style fill;
			fill.width = UI::Length::Percent(1.0f);
			fill.height = UI::Length::Percent(1.0f);
			context.SetStyle(root, fill);
			panel.BuildContent(context, root);
			UISystem::Update();
			return root;
		}
	};

} // anonymous namespace

TEST_F(UIBuilderOpsTest, RowsAreTheDocumentTreeDepthFirst) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root">
		<panel id="a"><panel id="a1"/></panel>
		<panel id="b"/>
	</panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(parsed.document, rows);

	ASSERT_EQ(rows.size(), 4u);
	EXPECT_EQ(rows[0].node->id, "root");
	EXPECT_EQ(rows[0].depth, 0u);
	EXPECT_EQ(rows[1].node->id, "a");
	EXPECT_EQ(rows[1].depth, 1u);
	EXPECT_EQ(rows[2].node->id, "a1");
	EXPECT_EQ(rows[2].depth, 2u);
	EXPECT_EQ(rows[3].node->id, "b");
	EXPECT_EQ(rows[3].depth, 1u);

	for (u32 i = 0; i < rows.size(); ++i) {
		EXPECT_EQ(rows[i].index, i);
	}
}

TEST_F(UIBuilderOpsTest, NodeAtIsInverseOfRowsIndex) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root"><panel id="a"><panel id="a1"/></panel><panel id="b"/></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(parsed.document, rows);
	ASSERT_FALSE(rows.empty());

	for (const UIBuilderOps::Row& row : rows) {
		EXPECT_EQ(UIBuilderOps::NodeAt(parsed.document, row.index), row.node) << "index " << row.index;
	}
	EXPECT_EQ(UIBuilderOps::NodeAt(parsed.document, static_cast<u32>(rows.size())), nullptr);
	EXPECT_EQ(UIBuilderOps::NodeAt(parsed.document, UIBuilderOps::INVALID_NODE), nullptr);
}

TEST_F(UIBuilderOpsTest, RowReadsIdOrTag) {
	Parsed parsed(R"(<ui version="1">
	<panel id="hud"><label/></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(parsed.document, rows);
	ASSERT_EQ(rows.size(), 2u);
	EXPECT_EQ(UIBuilderOps::RowLabel(*rows[0].node), "#hud");
	EXPECT_EQ(UIBuilderOps::RowLabel(*rows[1].node), "label");
}

TEST_F(UIBuilderOpsTest, InstanceIsLeafRowNamingTemplate) {
	Parsed parsed(R"(<ui version="1">
	<template src="project://UI/Portrait.slxml" name="Portrait"/>
	<panel id="lobby"><instance template="Portrait" id="p1"/></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(parsed.document, rows);
	ASSERT_EQ(rows.size(), 2u);
	EXPECT_EQ(UIBuilderOps::RowLabel(*rows[1].node), "#p1 <Portrait>");

	UIDocumentAsset handBuilt;
	handBuilt.root.tag = "panel";
	UIDocumentNode instance;
	instance.tag = "instance";
	instance.templateName = "Portrait";
	UIDocumentNode ghost;
	ghost.tag = "panel";
	instance.children.push_back(ghost);
	handBuilt.root.children.push_back(instance);

	Vector<UIBuilderOps::Row> handRows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(handBuilt, handRows);
	EXPECT_EQ(handRows.size(), 2u);
}

TEST_F(UIBuilderOpsTest, LibraryGroupsEveryRegisteredWidgetByCategory) {
	Vector<UIBuilderOps::LibraryGroup> groups{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildLibrary(groups);

	u32 listed = 0;
	for (const UIBuilderOps::LibraryGroup& group : groups) {
		EXPECT_FALSE(group.widgets.empty());
		listed += static_cast<u32>(group.widgets.size());
	}
	EXPECT_EQ(listed, 4u);
	ASSERT_EQ(groups.size(), 2u);

	bool sawScrollViewUnderContainers = false;
	for (const UIBuilderOps::LibraryGroup& group : groups) {
		for (const UI::WidgetDesc* widget : group.widgets) {
			if (widget->tag == "scroll-view") {
				EXPECT_EQ(group.category, "Containers");
				sawScrollViewUnderContainers = true;
			} else {
				EXPECT_EQ(group.category, "Controls") << widget->tag;
			}
		}
	}
	EXPECT_TRUE(sawScrollViewUnderContainers);
}

TEST_F(UIBuilderOpsTest, ModuleWidgetJoinsLibrary) {
	static constexpr std::string_view GAUGE = "gauge";
	auto factory = [](UI::Context& context, UI::ElementHandle parent, const UI::WidgetArgs&) -> UI::WidgetResult {
		UI::ElementHandle element = context.CreateElement();
		context.AppendChild(parent, element);
		return {element, {}};
	};
	const void* owner = reinterpret_cast<const void*>(0x1234);

	UI::WidgetDesc gauge;
	gauge.tag = GAUGE;
	gauge.factory = factory;
	gauge.category = "Instruments";
	ASSERT_TRUE(UI::WidgetRegistry::Register(gauge, owner));

	Vector<const UI::WidgetDesc*> all{GetMemoryResource(MemTag::Debug)};
	UI::WidgetRegistry::GetAll(all);
	ASSERT_EQ(all.size(), 5u);
	EXPECT_EQ(all[0]->tag, "button");
	EXPECT_EQ(all[4]->tag, GAUGE);

	Vector<UIBuilderOps::LibraryGroup> groups{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildLibrary(groups);
	ASSERT_EQ(groups.size(), 3u);
	EXPECT_EQ(groups[2].category, "Instruments");

	UI::WidgetDesc replacement;
	replacement.tag = "button";
	replacement.factory = factory;
	replacement.category = "Controls";
	ASSERT_TRUE(UI::WidgetRegistry::Register(replacement, owner));

	UI::WidgetRegistry::GetAll(all);
	ASSERT_EQ(all.size(), 5u);
	EXPECT_EQ(all[0]->tag, "button");
	EXPECT_EQ(all[0]->factory, static_cast<UI::WidgetFactoryFn>(factory));

	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(owner), 2u);
	UI::WidgetRegistry::GetAll(all);
	ASSERT_EQ(all.size(), 4u);
	EXPECT_EQ(all[0]->tag, "button");
	EXPECT_NE(all[0]->factory, static_cast<UI::WidgetFactoryFn>(factory));
}

TEST_F(UIBuilderOpsTest, EveryNodeMapsToElementItBuilt) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root"><panel id="a"/><panel id="b"/></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {200, 200};
	UI::Context context(desc, &fonts);

	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
	UI::ElementHandle root = parsed.document.Instantiate(context, context.GetRoot(), {}, mapping);
	ASSERT_TRUE(context.IsAlive(root));
	ASSERT_EQ(mapping.size(), 3u);

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(parsed.document, rows);
	ASSERT_EQ(rows.size(), 3u);
	for (const UIBuilderOps::Row& row : rows) {
		UI::ElementHandle element = UIBuilderOps::ElementForNode(mapping, row.node);
		EXPECT_TRUE(context.IsAlive(element)) << UIBuilderOps::RowLabel(*row.node);
		EXPECT_EQ(UIBuilderOps::NodeForElement(context, parsed.document, mapping, element), row.index) << UIBuilderOps::RowLabel(*row.node);
	}
	EXPECT_EQ(UIBuilderOps::ElementForNode(mapping, rows[0].node), root);
}

TEST_F(UIBuilderOpsTest, InstantiateClearsMapping) {
	Parsed parsed(R"(<ui version="1"><panel id="root"><panel id="a"/></panel></ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {200, 200};
	UI::Context context(desc, &fonts);

	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
	UI::ElementHandle first = parsed.document.Instantiate(context, context.GetRoot(), {}, mapping);
	ASSERT_EQ(mapping.size(), 2u);
	context.DestroyElement(first);

	parsed.document.Instantiate(context, context.GetRoot(), {}, mapping);
	EXPECT_EQ(mapping.size(), 2u);
	for (const UIInstantiateMapping& entry : mapping) {
		EXPECT_TRUE(context.IsAlive(entry.element));
	}
}

TEST_F(UIBuilderOpsTest, WidgetsInnerElementResolvesToWidgetsNode) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root"><button id="pause" text="Pause"/></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {200, 200};
	UI::Context context(desc, &fonts);

	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
	parsed.document.Instantiate(context, context.GetRoot(), {}, mapping);

	UI::ElementHandle button = UIBuilderOps::ElementForNode(mapping, UIBuilderOps::NodeAt(parsed.document, 1));
	ASSERT_TRUE(context.IsAlive(button));
	const UI::Element* record = context.GetElement(button);
	ASSERT_NE(record->firstChild, UI::INVALID_UI_INDEX);
	UI::ElementHandle inner = context.MakeHandle(record->firstChild);

	EXPECT_EQ(UIBuilderOps::NodeForElement(context, parsed.document, mapping, inner), 1u);
}

TEST_F(UIBuilderOpsTest, UnbuiltElementHasNoNode) {
	Parsed parsed(R"(<ui version="1"><panel id="root"/></ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {200, 200};
	UI::Context context(desc, &fonts);

	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
	parsed.document.Instantiate(context, context.GetRoot(), {}, mapping);

	UI::ElementHandle stranger = context.CreateElement();
	context.AppendChild(context.GetRoot(), stranger);
	EXPECT_EQ(UIBuilderOps::NodeForElement(context, parsed.document, mapping, stranger), UIBuilderOps::INVALID_NODE);
	EXPECT_EQ(UIBuilderOps::NodeForElement(context, parsed.document, mapping, context.GetRoot()),
			  UIBuilderOps::INVALID_NODE);
	EXPECT_EQ(UIBuilderOps::NodeForElement(context, parsed.document, mapping, {}), UIBuilderOps::INVALID_NODE);
}

TEST_F(UIBuilderOpsTest, TemplateResolvesToOpenInstance) {
	Parsed inner(R"(<ui version="1"><panel id="portrait"><panel id="face"/></panel></ui>)");
	ASSERT_TRUE(inner.ok) << inner.error;

	Parsed outer(R"(<ui version="1">
	<template src="project://UI/Portrait.slxml" name="Portrait"/>
	<panel id="lobby"><instance template="Portrait" id="p1"/></panel>
</ui>)");
	ASSERT_TRUE(outer.ok) << outer.error;
	ASSERT_EQ(outer.document.templates.size(), 1u);
	outer.document.templates[0].document = Handle<UIDocumentAsset>(&inner.document);

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {200, 200};
	UI::Context context(desc, &fonts);

	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
	outer.document.Instantiate(context, context.GetRoot(), {}, mapping);

	u32 instanceIndex = IndexOfId(outer.document, "p1");
	ASSERT_NE(instanceIndex, UIBuilderOps::INVALID_NODE);
	UI::ElementHandle composed = UIBuilderOps::ElementForNode(mapping, UIBuilderOps::NodeAt(inner.document, 0));
	ASSERT_TRUE(context.IsAlive(composed));
	EXPECT_EQ(UIBuilderOps::NodeForElement(context, outer.document, mapping, composed), instanceIndex);

	UI::ElementHandle face = UIBuilderOps::ElementForNode(mapping, UIBuilderOps::NodeAt(inner.document, 1));
	ASSERT_TRUE(context.IsAlive(face));
	EXPECT_EQ(UIBuilderOps::NodeForElement(context, outer.document, mapping, face), instanceIndex);
}

TEST_F(UIBuilderPanelFixture, RowsFlushBandsTakeNoSpace) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root"><button id="a" text="A"/><button id="b" text="B"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle first = RowLabelled(context, root, "#a");
	UI::ElementHandle second = RowLabelled(context, root, "#b");
	ASSERT_TRUE(context.IsAlive(first));
	ASSERT_TRUE(context.IsAlive(second));

	const UI::Rect firstBox = context.GetLayout(first).borderBox;
	const UI::Rect secondBox = context.GetLayout(second).borderBox;
	EXPECT_NEAR(secondBox.y, firstBox.y + firstBox.h, 0.5f) << (secondBox.y - firstBox.y - firstBox.h);

	Vector<UI::ElementHandle> strips{GetMemoryResource(MemTag::Editor)};
	CollectClass(context, root, EditorTheme::Class::InsertStrip, strips);
	ASSERT_FALSE(strips.empty());

	for (UI::ElementHandle strip : strips) {
		EXPECT_FALSE(context.GetComputedStyle(strip).pointerEvents);
	}
}

TEST_F(UIBuilderPanelFixture, OpeningADocumentFillsTheHierarchy) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root"><panel id="a"/><button id="go" text="Go"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());

	UI::Context& context = PanelContext();
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::Style fill;
	fill.width = UI::Length::Percent(1.0f);
	fill.height = UI::Length::Percent(1.0f);
	context.SetStyle(root, fill);
	panel.Get()->BuildContent(context, root);
	UISystem::Update();

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	EXPECT_EQ(CountRowsByClass(context, root, StringHash("slt-tree-row")), 3u);
	EXPECT_TRUE(RowLabelled(context, root, "#root").IsValid());
	EXPECT_TRUE(RowLabelled(context, root, "#a").IsValid());
	EXPECT_TRUE(RowLabelled(context, root, "#go").IsValid());
}

TEST_F(UIBuilderPanelFixture, StatusBarSurvivesRebuild) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));

	UI::Context& context = PanelContext();
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	UISystem::Update();
	EXPECT_TRUE(FirstDescendantWithText(context, root, "project://Assets/Screen.slxml").IsValid());

	UI::ElementHandle rebuilt = context.CreateElement();
	context.AppendChild(context.GetRoot(), rebuilt);
	panel.Get()->BuildContent(context, rebuilt);
	UISystem::Update();
	EXPECT_TRUE(FirstDescendantWithText(context, rebuilt, "project://Assets/Screen.slxml").IsValid());
	EXPECT_FALSE(FirstDescendantWithText(context, rebuilt, "no document open").IsValid());
}

TEST_F(UIBuilderPanelFixture, PreviewIsSecondContext) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="a"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());

	UI::Context& context = PanelContext();
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::Style fill;
	fill.width = UI::Length::Percent(1.0f);
	fill.height = UI::Length::Percent(1.0f);
	context.SetStyle(root, fill);
	panel.Get()->BuildContent(context, root);
	UISystem::Update();

	u32 before = UISystem::GetStats().contexts;

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Frame(*panel.Get());

	EXPECT_EQ(UISystem::GetStats().contexts, before + 1);
}

TEST_F(UIBuilderPanelFixture, ClickingViewportSelectsElementUnderIt) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root" style="flex-direction: column; width: 100%; height: 100%;">
	<panel id="top" style="width: 100%; height: 50%;"/>
	<panel id="bottom" style="width: 100%; height: 50%;"/>
</panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());

	UI::Context& context = PanelContext();
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::Style fill;
	fill.width = UI::Length::Percent(1.0f);
	fill.height = UI::Length::Percent(1.0f);
	context.SetStyle(root, fill);
	panel.Get()->BuildContent(context, root);
	UISystem::Update();

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Frame(*panel.Get());

	UI::ElementHandle viewport = FirstByClass(context, root, EditorTheme::Class::ViewportPlaceholder);
	ASSERT_TRUE(context.IsAlive(viewport));
	UI::Rect area = UI::SnapRectToPixels(context.GetLayout(viewport).content);
	ASSERT_GT(area.w, 100.0f);
	ASSERT_GT(area.h, 100.0f);

	const Vec2 centre{area.x + area.w * 0.5f, area.y + area.h * 0.5f};

	ClickAt(context, {centre.x, centre.y - 2.0f});
	EXPECT_TRUE(RowIsSelected(context, RowLabelled(context, root, "#top")));
	EXPECT_FALSE(RowIsSelected(context, RowLabelled(context, root, "#bottom")));

	ClickAt(context, {centre.x, centre.y + 2.0f});
	EXPECT_TRUE(RowIsSelected(context, RowLabelled(context, root, "#bottom")));
	EXPECT_FALSE(RowIsSelected(context, RowLabelled(context, root, "#top")));

	ClickAt(context, {area.x + 1.0f, area.y + 1.0f});
	EXPECT_TRUE(RowIsSelected(context, RowLabelled(context, root, "#bottom")));
}

TEST_F(UIBuilderPanelFixture, ClickingEmptyCanvasClearsTheSelection) {
	WriteDocument("project://Assets/Half.slxml",
				  R"(<panel id="root" style="width: 100%; height: 50%;"><panel id="child" style="width: 100%; height: 100%;"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());

	UI::Context& context = PanelContext();
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::Style fill;
	fill.width = UI::Length::Percent(1.0f);
	fill.height = UI::Length::Percent(1.0f);
	context.SetStyle(root, fill);
	panel.Get()->BuildContent(context, root);
	UISystem::Update();

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Half.slxml"));
	Frame(*panel.Get());
	Frame(*panel.Get());

	UI::ElementHandle viewport = FirstByClass(context, root, EditorTheme::Class::ViewportPlaceholder);
	ASSERT_TRUE(context.IsAlive(viewport));
	UI::Rect area = UI::SnapRectToPixels(context.GetLayout(viewport).content);
	const Vec2 centre{area.x + area.w * 0.5f, area.y + area.h * 0.5f};

	ClickAt(context, {centre.x, centre.y - 2.0f});
	ASSERT_TRUE(RowIsSelected(context, RowLabelled(context, root, "#child")));

	ClickAt(context, {centre.x, centre.y + 2.0f});
	EXPECT_FALSE(RowIsSelected(context, RowLabelled(context, root, "#child")));
	EXPECT_FALSE(RowIsSelected(context, RowLabelled(context, root, "#root")));
}

TEST_F(UIBuilderPanelFixture, ClickingAHierarchyRowSelectsItsNode) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="a"/><panel id="b"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());

	UI::Context& context = PanelContext();
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::Style fill;
	fill.width = UI::Length::Percent(1.0f);
	fill.height = UI::Length::Percent(1.0f);
	context.SetStyle(root, fill);
	panel.Get()->BuildContent(context, root);
	UISystem::Update();

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle rowB = RowLabelled(context, root, "#b");
	ASSERT_TRUE(context.IsAlive(rowB));
	EXPECT_FALSE(RowIsSelected(context, rowB));

	ClickAt(context, CenterOf(context, rowB));
	EXPECT_TRUE(RowIsSelected(context, RowLabelled(context, root, "#b")));
	EXPECT_FALSE(RowIsSelected(context, RowLabelled(context, root, "#a")));
}

TEST_F(UIBuilderPanelFixture, TheLibraryPaneListsTheRegistrysWidgets) {
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());

	UI::Context& context = PanelContext();
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::Style fill;
	fill.width = UI::Length::Percent(1.0f);
	fill.height = UI::Length::Percent(1.0f);
	context.SetStyle(root, fill);
	panel.Get()->BuildContent(context, root);
	UISystem::Update();

	for (std::string_view tag : {"button", "checkbox", "slider", "scroll-view"}) {
		EXPECT_TRUE(FirstDescendantWithText(context, root, tag).IsValid()) << tag << ">";
	}
	EXPECT_TRUE(FirstDescendantWithText(context, root, "Containers").IsValid());
	EXPECT_TRUE(FirstDescendantWithText(context, root, "Controls").IsValid());
}

TEST_F(UIBuilderPanelFixture, TwoBuildersHoldSeparateDocuments) {
	WriteDocument("project://Assets/A.slxml", R"(<panel id="alpha"/>)");
	WriteDocument("project://Assets/B.slxml", R"(<panel id="beta"/>)");

	Handle<EditorPanel> first = OpenBuilder();
	Handle<EditorPanel> second = OpenBuilder();
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	ASSERT_NE(first.Get(), second.Get());

	UI::Context& context = PanelContext();
	UI::ElementHandle rootA = context.CreateElement();
	UI::ElementHandle rootB = context.CreateElement();
	context.AppendChild(context.GetRoot(), rootA);
	context.AppendChild(context.GetRoot(), rootB);
	first.Get()->BuildContent(context, rootA);
	second.Get()->BuildContent(context, rootB);
	UISystem::Update();

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/A.slxml"));
	Frame(*first.Get());
	EXPECT_TRUE(RowLabelled(context, rootA, "#alpha").IsValid());
	EXPECT_FALSE(RowLabelled(context, rootB, "#alpha").IsValid());

	PanelManager::ClosePanel(first);
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/B.slxml"));
	Frame(*second.Get());
	EXPECT_TRUE(RowLabelled(context, rootB, "#beta").IsValid());
}

TEST_F(UIBuilderPanelFixture, DocumentSheetNeverReachesEDITORSContext) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/Doc.slss")),
							  ".slt-tree-row { width: 321px; }");
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<sheet src="project://Assets/Doc.slss"/><panel id="root" class="slt-tree-row"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());

	UI::Context& context = PanelContext();
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::Style fill;
	fill.width = UI::Length::Percent(1.0f);
	fill.height = UI::Length::Percent(1.0f);
	context.SetStyle(root, fill);
	panel.Get()->BuildContent(context, root);
	UISystem::Update();
	u32 before = UISystem::GetStats().contexts;

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Frame(*panel.Get());

	ASSERT_EQ(UISystem::GetStats().contexts, before + 1);

	UI::ElementHandle row = RowLabelled(context, root, "#root");
	ASSERT_TRUE(context.IsAlive(row));
	EXPECT_NE(context.GetComputedStyle(row).width.value, 321.0f);
}

TEST_F(UIBuilderPanelFixture, ClosingABuilderReleasesItsPreviewContext) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());

	UI::Context& context = PanelContext();
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::Style fill;
	fill.width = UI::Length::Percent(1.0f);
	fill.height = UI::Length::Percent(1.0f);
	context.SetStyle(root, fill);
	panel.Get()->BuildContent(context, root);
	UISystem::Update();

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Frame(*panel.Get());
	u32 withBuilder = UISystem::GetStats().contexts;
	ASSERT_GE(withBuilder, 1u);

	PanelManager::ClosePanel(panel);
	UISystem::Update();
	EXPECT_LT(UISystem::GetStats().contexts, withBuilder);
}

TEST_F(UIBuilderPanelFixture, DocumentRendersPixelsIntoSurface) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root" style="width: 32px; height: 32px; background-color: #ff0000;"/>)");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	ASSERT_EQ(asset->GetAssetTypeId(), StringHash("UIDocumentAsset"));

	UISurfaceId surface = UISystem::CreateSurface({32, 32});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* preview = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(preview, nullptr);

	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
	static_cast<UIDocumentAsset*>(asset)->Instantiate(*preview, preview->GetRoot(), {}, mapping);
	ASSERT_EQ(mapping.size(), 1u);

	UISystem::Update();
	RenderSystem::RenderAll();

	RHI::TextureHandle texture = UISystem::GetSurfaceTexture(surface);
	ASSERT_TRUE(texture.IsValid());
	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	pixels.resize(32 * 32 * 4);
	RenderSystem::GetDevice()->ReadbackTexture(texture, {pixels.data(), pixels.size()});

	size_t center = (16 * 32 + 16) * 4;
	EXPECT_EQ(pixels[center + 0], 255);
	EXPECT_EQ(pixels[center + 1], 0);
	EXPECT_EQ(pixels[center + 2], 0);
	EXPECT_EQ(pixels[center + 3], 255);

	UISystem::DestroySurface(surface);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderOpsTest, OnlyAuthoredRowsOverridden) {
	Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations("width: 10px; opacity: 0.25", declarations);

	Vector<UIBuilderOps::PropertyRow> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildPropertyRows(declarations, rows);

	ASSERT_EQ(rows.size(), UI::GetStylePropertyNames().size());

	u32 overridden = 0;
	for (const UIBuilderOps::PropertyRow& row : rows) {
		if (row.overridden) {
			++overridden;
		}
		if (row.name == "width") {
			EXPECT_TRUE(row.overridden);
			EXPECT_EQ(row.value, "10px");
		}
		if (row.name == "opacity") {
			EXPECT_TRUE(row.overridden);
			EXPECT_EQ(row.value, "0.25");
		}
		if (row.name == "height") {
			EXPECT_FALSE(row.overridden);
			EXPECT_TRUE(row.value.empty());
		}
	}
	EXPECT_EQ(overridden, 2u);
}

TEST_F(UIBuilderOpsTest, RepeatedPropertyReportsOneThatWins) {
	Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations("width: 1px; width: 2px", declarations);
	ASSERT_EQ(declarations.size(), 2u);

	Vector<UIBuilderOps::PropertyRow> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildPropertyRows(declarations, rows);
	for (const UIBuilderOps::PropertyRow& row : rows) {
		if (row.name == "width") {
			EXPECT_EQ(row.value, "2px");
		}
	}

	UI::StyleSheet sheet;
	sheet.Parse("*{width: 1px; width: 2px;}");
	ASSERT_EQ(sheet.GetRules().size(), 1u);
	UI::Style style;
	UI::StyleSheet::ApplyRule(style, sheet.GetRules()[0]);
	EXPECT_FLOAT_EQ(style.width.value, 2.0f);
}

TEST_F(UIBuilderOpsTest, UnsetKeepsRestOfSource) {
	constexpr std::string_view SOURCE = ".row {\n\tpadding: 8px;\n\tcolor: red;\n\twidth: 4px;\n}\n";
	Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(SOURCE, rules);
	ASSERT_EQ(rules.size(), 1u);
	ASSERT_EQ(rules[0].declarations.size(), 3u);

	std::string_view runs[] = {rules[0].declarations[1].text};
	str result = UIBuilderOps::EraseRuns(SOURCE, runs);

	EXPECT_EQ(result, ".row {\n\tpadding: 8px;\n\twidth: 4px;\n}\n");
}

TEST_F(UIBuilderOpsTest, UnsettingFromFragmentLeavesNoOrphanSemicolon) {
	constexpr std::string_view FRAGMENT = "width: 10px; color: red; height: 4px";
	Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations(FRAGMENT, declarations);
	ASSERT_EQ(declarations.size(), 3u);

	std::string_view runs[] = {declarations[1].text};
	str result = UIBuilderOps::EraseRuns(FRAGMENT, runs);
	EXPECT_EQ(result, "width: 10px; height: 4px");

	Vector<UI::StyleDeclarationSource> after{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations(result, after);
	ASSERT_EQ(after.size(), 2u);
	EXPECT_EQ(after[0].name, "width");
	EXPECT_EQ(after[1].name, "height");
}

TEST_F(UIBuilderOpsTest, UnsetAllTakesEveryRun) {
	constexpr std::string_view FRAGMENT = "width: 10px; color: red; height: 4px";
	Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations(FRAGMENT, declarations);
	ASSERT_EQ(declarations.size(), 3u);

	std::string_view runs[] = {declarations[2].text, declarations[0].text, declarations[1].text};
	EXPECT_TRUE(UIBuilderOps::EraseRuns(FRAGMENT, runs).empty());

	constexpr std::string_view OTHER = "width: 10px";
	std::string_view foreign[] = {OTHER};
	EXPECT_EQ(UIBuilderOps::EraseRuns(FRAGMENT, foreign), FRAGMENT);
}

TEST_F(UIBuilderOpsTest, UnsetRevertsToSheetStyle) {
	constexpr std::string_view WITH =
		R"(<ui version="1"><panel id="a" class="row" style="opacity: 0.25; letter-spacing: 3px"/></ui>)";
	constexpr std::string_view WITHOUT = R"(<ui version="1"><panel id="a" class="row"/></ui>)";

	auto computed = [](std::string_view source) {
		UIDocumentAsset document;
		str error;
		EXPECT_TRUE(document.Parse(source, error)) << error;
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		UI::Context context(desc, nullptr);
		context.LoadStyleSheet(".row { opacity: 0.75; }");
		UIInstantiateDesc instantiate;
		UI::ElementHandle root = document.Instantiate(context, context.GetRoot(), instantiate);
		context.Update();
		return context.GetComputedStyle(root);
	};

	const UI::Style set = computed(WITH);
	EXPECT_FLOAT_EQ(set.letterSpacing, 3.0f);

	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(WITH, error)) << error;
	Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations(document.root.style, declarations);
	ASSERT_EQ(declarations.size(), 2u);
	Vector<std::string_view> runs{GetMemoryResource(MemTag::Debug)};
	for (const UI::StyleDeclarationSource& declaration : declarations) {
		runs.push_back(declaration.text);
	}
	document.root.style = UIBuilderOps::EraseRuns(document.root.style, runs);

	UI::ContextDesc desc;
	desc.sizePx = {400, 300};
	UI::Context context(desc, nullptr);
	context.LoadStyleSheet(".row { opacity: 0.75; }");
	UIInstantiateDesc instantiate;
	UI::ElementHandle root = document.Instantiate(context, context.GetRoot(), instantiate);
	context.Update();

	EXPECT_EQ(context.GetComputedStyle(root), computed(WITHOUT));
	EXPECT_FLOAT_EQ(context.GetComputedStyle(root).opacity, 0.75f);
}

namespace {

	struct WiredSheet {
		UIDocumentAsset* document = nullptr;
		StyleSheetAsset* sheet = nullptr;

		WiredSheet(std::string_view documentSource, std::string_view sheetSource) {
			document = New<UIDocumentAsset>(MemTag::Asset);
			sheet = New<StyleSheetAsset>(MemTag::Asset);
			str error;
			EXPECT_TRUE(document->Parse(documentSource, error)) << error;
			EXPECT_TRUE(sheet->Parse(sheetSource, error)) << error;
			EXPECT_FALSE(document->sheets.empty());
			if (!document->sheets.empty()) {
				document->sheets[0].sheet = Handle<StyleSheetAsset>(sheet);
			}
		}
		~WiredSheet() {
			Delete(document);
			Delete(sheet);
		}
	};

} // anonymous namespace

TEST_F(UIBuilderOpsTest, SheetRowsInSourceOrder) {
	WiredSheet wired(R"(<ui version="1"><sheet src="project://UI/Main.slss"/><panel id="root"/></ui>)",
					 "#hud { width: 1px; }\n.row { width: 2px; }\npanel { width: 3px; }\n");

	Vector<UIBuilderOps::SheetRow> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildSheetRows(*wired.document, rows);

	ASSERT_EQ(rows.size(), 4u);
	EXPECT_EQ(rows[0].rule, UIBuilderOps::INVALID_NODE);
	EXPECT_EQ(rows[0].path, "project://UI/Main.slss");

	EXPECT_EQ(rows[1].selector, "#hud");
	EXPECT_EQ(rows[2].selector, ".row");
	EXPECT_EQ(rows[3].selector, "panel");

	UI::StyleSheet parsed;
	parsed.Parse(wired.sheet->source);
	ASSERT_EQ(parsed.GetRules().size(), 3u);
	EXPECT_LT(parsed.GetRules()[0].specificity, parsed.GetRules()[2].specificity);
}

TEST_F(UIBuilderOpsTest, PseudoStateIsToken) {
	WiredSheet wired(R"(<ui version="1"><sheet src="s"/><panel id="root"/></ui>)",
					 ".main-menu-button { width: 1px; }\n.main-menu-button:hover { width: 2px; }\n");

	Vector<UIBuilderOps::SheetRow> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildSheetRows(*wired.document, rows);
	ASSERT_EQ(rows.size(), 3u);

	EXPECT_EQ(rows[1].selector, ".main-menu-button");
	EXPECT_TRUE(rows[1].pseudo.empty());
	EXPECT_EQ(rows[2].selector, ".main-menu-button");
	EXPECT_EQ(rows[2].pseudo, ":hover");
}

TEST_F(UIBuilderOpsTest, SheetThatNeverResolvedStillGetsRow) {
	UIDocumentAsset* document = New<UIDocumentAsset>(MemTag::Asset);
	str error;
	ASSERT_TRUE(document->Parse(R"(<ui version="1"><sheet src="project://UI/Gone.slss"/><panel id="root"/></ui>)", error))
		<< error;

	Vector<UIBuilderOps::SheetRow> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildSheetRows(*document, rows);

	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(rows[0].path, "project://UI/Gone.slss");
	EXPECT_EQ(rows[0].rule, UIBuilderOps::INVALID_NODE);
	Delete(document);
}

TEST_F(UIBuilderOpsTest, MatchingSelectorsInCascadeOrder) {
	WiredSheet wired(
		R"(<ui version="1"><sheet src="s"/><panel id="hud" class="row card"/></ui>)",
		"panel { width: 1px; }\n#hud { width: 2px; }\n.row { width: 3px; }\n.nope { width: 4px; }\nlabel { width: 5px; }\n.card:hover { width: 6px; }\n");

	Vector<UIBuilderOps::MatchingSelector> matching{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildMatchingSelectors(*wired.document, wired.document->root, matching);

	ASSERT_EQ(matching.size(), 4u);
	for (const UIBuilderOps::MatchingSelector& match : matching) {
		EXPECT_NE(match.selector, ".nope");
		EXPECT_NE(match.selector, "label");
		EXPECT_EQ(match.sheet, "s");
	}

	EXPECT_EQ(matching[0].selector, "panel");
	EXPECT_EQ(matching[3].selector, "#hud");
	for (size_t i = 1; i < matching.size(); ++i) {
		EXPECT_LE(matching[i - 1].specificity, matching[i].specificity);
	}

	bool sawHover = false;
	for (const UIBuilderOps::MatchingSelector& match : matching) {
		sawHover = sawHover || match.selector == ".card:hover";
	}
	EXPECT_TRUE(sawHover);
}

TEST_F(UIBuilderPanelFixture, ElementShowsStylesAndSelectors) {
	WriteSheet("project://Assets/Main.slss", ".row { opacity: 0.5; }\n.absent { opacity: 0.25; }\n");
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<sheet src="project://Assets/Main.slss"/><panel id="root" class="row" style="width: 42px"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle row = RowLabelled(context, root, "#root");
	ASSERT_TRUE(context.IsAlive(row));
	ClickAt(context, CenterOf(context, row));

	UI::ElementHandle inspector = PaneNamed(context, root, "Inspector");
	ASSERT_TRUE(context.IsAlive(inspector));

	EXPECT_TRUE(FirstDescendantWithText(context, inspector, "width").IsValid());
	EXPECT_TRUE(FirstDescendantWithText(context, inspector, "42px").IsValid());
	EXPECT_TRUE(FirstDescendantWithText(context, inspector, "letter-spacing").IsValid());

	EXPECT_TRUE(FirstDescendantWithText(context, inspector, "Matching Selectors").IsValid());
	EXPECT_TRUE(FirstDescendantWithText(context, inspector, ".row").IsValid());
	EXPECT_FALSE(FirstDescendantWithText(context, inspector, ".absent").IsValid());
}

TEST_F(UIBuilderPanelFixture, SelectorReplacesElementColumn) {
	WriteSheet("project://Assets/Main.slss", ".row { opacity: 0.5; }\n");
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<sheet src="project://Assets/Main.slss"/><panel id="root" class="row" style="width: 42px"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle inspector = PaneNamed(context, root, "Inspector");
	ASSERT_TRUE(context.IsAlive(inspector));
	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#root")));
	ASSERT_TRUE(FirstDescendantWithText(context, inspector, "42px").IsValid());

	UI::ElementHandle selector = RowLabelled(context, root, ".row");
	ASSERT_TRUE(context.IsAlive(selector));
	ClickAt(context, CenterOf(context, selector));

	EXPECT_FALSE(FirstDescendantWithText(context, inspector, "42px").IsValid());
	EXPECT_TRUE(FirstDescendantWithText(context, inspector, "0.5").IsValid());
	EXPECT_FALSE(RowIsSelected(context, RowLabelled(context, root, "#root")));

	context.ClearChildren(context.GetRoot());
	UI::ElementHandle rebuilt = BuildPanelInto(context, *panel.Get());
	EXPECT_FALSE(RowIsSelected(context, RowLabelled(context, rebuilt, "#root")));
	EXPECT_TRUE(FirstDescendantWithText(context, PaneNamed(context, rebuilt, "Inspector"), ".row").IsValid());
}

TEST_F(UIBuilderPanelFixture, CanvasIsAuthoredSize) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root" style="width: 100%; height: 100%;"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Frame(*panel.Get());

	UI::ElementHandle viewport = FirstByClass(context, root, EditorTheme::Class::ViewportPlaceholder);
	ASSERT_TRUE(context.IsAlive(viewport));
	const UI::Rect area = UI::SnapRectToPixels(context.GetLayout(viewport).content);
	ASSERT_GT(area.w, 100.0f);

	UI::Context* preview = nullptr;
	for (UISurfaceId id = 1; id < 64 && !preview; ++id) {
		if (UI::Context* candidate = UISystem::GetSurfaceContext(id)) {
			preview = candidate;
		}
	}
	ASSERT_NE(preview, nullptr);
	const f32 logicalWidth = static_cast<f32>(preview->GetSize().x) / preview->GetContentScale();
	EXPECT_NEAR(logicalWidth, 1920.0f, 2.0f);

	EXPECT_LE(static_cast<f32>(preview->GetSize().x), area.w + 1.0f);
	EXPECT_LE(static_cast<f32>(preview->GetSize().y), area.h + 1.0f);
	EXPECT_LT(preview->GetSize().x, 1920);
}

TEST_F(UIBuilderPanelFixture, UnsetEditsDocumentUndoable) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root" style="width: 42px; opacity: 0.5"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);
	ASSERT_EQ(document->root.style, "width: 42px; opacity: 0.5");

	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#root")));

	UI::ElementHandle widthRow = FirstDescendantWithText(context, root, "width");
	ASSERT_TRUE(context.IsAlive(widthRow));
	RightClickAt(context, CenterOf(context, ParentOf(context, widthRow)));
	UI::ElementHandle item = FirstDescendantWithText(context, context.GetRoot(), "Unset width");
	ASSERT_TRUE(context.IsAlive(item));
	ClickAt(context, CenterOf(context, item));

	EXPECT_EQ(document->root.style, "opacity: 0.5");

	ASSERT_TRUE(UndoSystem::CanUndo());
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Unset width");
	UndoSystem::Undo();
	EXPECT_EQ(document->root.style, "width: 42px; opacity: 0.5");

	Frame(*panel.Get());
	EXPECT_TRUE(FirstDescendantWithText(context, PaneNamed(context, root, "Inspector"), "42px").IsValid());

	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, OpenRefreshesEveryPane) {
	WriteSheet("project://Assets/Main.slss", ".card { padding: 8px; }\n");
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<sheet src="project://Assets/Main.slss"/><panel id="root" class="card"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());

	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	UISystem::Update();

	UI::ElementHandle sheets = PaneNamed(context, root, "StyleSheets");
	ASSERT_TRUE(context.IsAlive(sheets));
	EXPECT_TRUE(FirstDescendantWithText(context, sheets, "project://Assets/Main.slss").IsValid());
	EXPECT_TRUE(FirstDescendantWithText(context, sheets, ".card").IsValid());

	UI::ElementHandle inspector = PaneNamed(context, root, "Inspector");
	ASSERT_TRUE(context.IsAlive(inspector));
	EXPECT_FALSE(FirstDescendantWithText(context, inspector, "no document open").IsValid());
	EXPECT_TRUE(FirstDescendantWithText(context, inspector, "Canvas").IsValid());
	EXPECT_TRUE(FirstDescendantWithText(context, inspector, "1920 x 1080 px").IsValid());

	ASSERT_FALSE(UIBuilderOps::OpenDocumentInBuilder(""));
	UISystem::Update();
	EXPECT_FALSE(FirstDescendantWithText(context, PaneNamed(context, root, "StyleSheets"), ".card").IsValid());
	EXPECT_TRUE(FirstDescendantWithText(context, PaneNamed(context, root, "Inspector"), "no document open").IsValid());
}

TEST_F(UIBuilderOpsTest, UnloadableEditRefused) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root">
		<label id="words">Score</label>
	</panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	const str before = parsed.document.Write();
	const u32 words = IndexOfId(parsed.document, "words");

	str error;
	EXPECT_FALSE(UIDocumentOps::Insert(parsed.document, words, 0, UIDocumentOps::MakeElement("panel"), error));
	EXPECT_FALSE(error.empty());
	EXPECT_EQ(parsed.document.Write(), before);

	error.clear();
	EXPECT_FALSE(UIDocumentOps::Remove(parsed.document, 0, error));
	EXPECT_FALSE(error.empty());
	EXPECT_EQ(parsed.document.Write(), before);
}

TEST_F(UIBuilderOpsTest, UnknownRuleEditRefused) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root"><button id="b" text="Pause"/></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;
	const u32 button = IndexOfId(parsed.document, "b");
	const u32 root = 0;
	const str before = parsed.document.Write();

	std::string_view four[] = {"a", "b", "c", "d"};
	str error;
	EXPECT_TRUE(UIDocumentOps::SetClasses(parsed.document, root, four, error)) << error;

	error.clear();
	EXPECT_FALSE(UIDocumentOps::SetClasses(parsed.document, button, four, error));
	EXPECT_FALSE(error.empty());
	(void)before;
}

TEST_F(UIBuilderOpsTest, MovingASubtreeInsideItselfIsRefused) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root">
		<panel id="outer"><panel id="inner"/></panel>
	</panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;
	const u32 outer = IndexOfId(parsed.document, "outer");
	const u32 inner = IndexOfId(parsed.document, "inner");
	const str before = parsed.document.Write();

	str error;
	EXPECT_FALSE(UIDocumentOps::Move(parsed.document, outer, inner, 0, error));
	EXPECT_FALSE(error.empty());
	EXPECT_EQ(parsed.document.Write(), before);
}

TEST_F(UIBuilderOpsTest, SiblingReorderAccountsForRemoval) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root"><panel id="a"/><panel id="b"/><panel id="c"/></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;
	const u32 a = IndexOfId(parsed.document, "a");

	str error;
	ASSERT_TRUE(UIDocumentOps::Move(parsed.document, a, 0, 2, error)) << error;

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(parsed.document, rows);
	ASSERT_EQ(rows.size(), 4u);
	EXPECT_EQ(rows[1].node->id, "b");
	EXPECT_EQ(rows[2].node->id, "a");
	EXPECT_EQ(rows[3].node->id, "c");

	const u32 moved = IndexOfId(parsed.document, "a");
	ASSERT_TRUE(UIDocumentOps::Move(parsed.document, moved, 0, 3, error)) << error;
	rows.clear();
	UIBuilderOps::BuildRows(parsed.document, rows);
	ASSERT_EQ(rows.size(), 4u);
	EXPECT_EQ(rows[1].node->id, "b");
	EXPECT_EQ(rows[2].node->id, "c");
	EXPECT_EQ(rows[3].node->id, "a");
}

TEST_F(UIBuilderOpsTest, MovedRunKeepsTreeOrder) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root"><panel id="a"/><panel id="b"/><panel id="c"/><panel id="d"/></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;
	const u32 run[] = {IndexOfId(parsed.document, "c"), IndexOfId(parsed.document, "a")};

	str error;
	ASSERT_TRUE(UIDocumentOps::MoveNodes(parsed.document, run, 0, 3, error)) << error;
	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(parsed.document, rows);
	ASSERT_EQ(rows.size(), 5u);
	EXPECT_EQ(rows[1].node->id, "b");
	EXPECT_EQ(rows[2].node->id, "a");
	EXPECT_EQ(rows[3].node->id, "c");
	EXPECT_EQ(rows[4].node->id, "d");
}

TEST_F(UIBuilderOpsTest, MovedRunKeepsNesting) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root">
		<panel id="x"><panel id="x1"><panel id="deep"/></panel><panel id="x2"/></panel>
		<panel id="y"><panel id="y1"/></panel>
		<panel id="host"><panel id="h1"/></panel>
	</panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;
	const u32 run[] = {IndexOfId(parsed.document, "y1"), IndexOfId(parsed.document, "deep"),
					   IndexOfId(parsed.document, "x1")};

	str error;
	ASSERT_TRUE(UIDocumentOps::MoveNodes(parsed.document, run, IndexOfId(parsed.document, "host"), 0, error)) << error;
	const u32 host = IndexOfId(parsed.document, "host");
	ASSERT_EQ(UIDocumentOps::GetChildCount(parsed.document, host), 3u);
	EXPECT_EQ(UIDocumentOps::ChildAt(parsed.document, host, 0), IndexOfId(parsed.document, "x1"));
	EXPECT_EQ(UIDocumentOps::ChildAt(parsed.document, host, 1), IndexOfId(parsed.document, "y1"));
	EXPECT_EQ(UIDocumentOps::ChildAt(parsed.document, host, 2), IndexOfId(parsed.document, "h1"));
	EXPECT_EQ(UIDocumentOps::LocationOf(parsed.document, IndexOfId(parsed.document, "deep")).parent,
			  IndexOfId(parsed.document, "x1"));
	EXPECT_EQ(UIDocumentOps::GetChildCount(parsed.document, IndexOfId(parsed.document, "x")), 1u);
	EXPECT_EQ(UIDocumentOps::GetChildCount(parsed.document, IndexOfId(parsed.document, "y")), 0u);
}

TEST_F(UIBuilderOpsTest, SelfNestingRunRefused) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root"><panel id="a"/><panel id="outer"><panel id="inner"/></panel></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;
	const u32 run[] = {IndexOfId(parsed.document, "a"), IndexOfId(parsed.document, "outer")};
	const str before = parsed.document.Write();

	str error;
	EXPECT_FALSE(UIDocumentOps::MoveNodes(parsed.document, run, IndexOfId(parsed.document, "inner"), 0, error));
	EXPECT_FALSE(error.empty());
	EXPECT_EQ(parsed.document.Write(), before);
}

TEST_F(UIBuilderOpsTest, DuplicateCopiesWholeSubtreeVerbatimIdIncluded) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root"><panel id="card" class="x" style="width: 3px"><panel id="deep"/></panel></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;
	const u32 card = IndexOfId(parsed.document, "card");

	str error;
	ASSERT_TRUE(UIDocumentOps::Duplicate(parsed.document, card, error)) << error;

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(parsed.document, rows);
	ASSERT_EQ(rows.size(), 5u);
	EXPECT_EQ(rows[3].node->id, "card");
	EXPECT_EQ(rows[3].node->classCount, 1u);
	EXPECT_EQ(rows[3].node->style, "width: 3px");
	EXPECT_EQ(rows[4].depth, rows[2].depth);
	EXPECT_EQ(rows[4].node->id, "deep");
}

TEST_F(UIBuilderOpsTest, MakeElementCarriesEveryAttributeWidgetDeclares) {
	UIDocumentNode plain = UIDocumentOps::MakeElement("panel");
	EXPECT_EQ(plain.tag, "panel");
	EXPECT_TRUE(plain.attributes.empty());

	UIDocumentNode slider = UIDocumentOps::MakeElement("slider");
	const UI::WidgetDesc* desc = UI::WidgetRegistry::Find("slider");
	ASSERT_NE(desc, nullptr);
	ASSERT_EQ(slider.attributes.size(), desc->attributes.size());
	for (size_t i = 0; i < slider.attributes.size(); ++i) {
		EXPECT_EQ(slider.attributes[i].name, desc->attributes[i].name);
	}

	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(R"(<ui version="1"><panel id="root"/></ui>)", error)) << error;
	ASSERT_TRUE(UIDocumentOps::Insert(document, 0, 0, slider, error)) << error;
}

TEST_F(UIBuilderOpsTest, UniqueIdAvoidsExisting) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root"><panel id="element"/><panel id="element-2"/></panel>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;
	EXPECT_EQ(UIDocumentOps::UniqueId(parsed.document, "element"), "element-3");
	EXPECT_EQ(UIDocumentOps::UniqueId(parsed.document, "fresh"), "fresh");
}

TEST_F(UIBuilderOpsTest, SetClassesHandlesAliasing) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root" class="alpha beta"/>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;
	const UIDocumentNode* node = UIBuilderOps::NodeAt(parsed.document, 0);
	ASSERT_NE(node, nullptr);

	Vector<std::string_view> views{GetMemoryResource(MemTag::Debug)};
	for (u8 i = 0; i < node->classCount; ++i) {
		views.push_back(node->classes[i]);
	}
	views.push_back("gamma");

	str error;
	ASSERT_TRUE(UIDocumentOps::SetClasses(parsed.document, 0, views, error)) << error;
	const UIDocumentNode* after = UIBuilderOps::NodeAt(parsed.document, 0);
	ASSERT_EQ(after->classCount, 3u);
	EXPECT_EQ(after->classes[0], "alpha");
	EXPECT_EQ(after->classes[1], "beta");
	EXPECT_EQ(after->classes[2], "gamma");
}

TEST_F(UIBuilderOpsTest, SetRewritesOneRun) {
	constexpr std::string_view SOURCE = ".row {\n\twidth: 10px; /* keep me */\n\tcolor: red;\n}\n";
	Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(SOURCE, rules);
	ASSERT_EQ(rules.size(), 1u);

	str written = UIBuilderOps::SetDeclaration(SOURCE, rules[0].body, rules[0].declarations, "width", "22px",
											   UIBuilderOps::DeclarationLayout::Block);
	EXPECT_EQ(written, ".row {\n\twidth: 22px; /* keep me */\n\tcolor: red;\n}\n");
}

TEST_F(UIBuilderOpsTest, SettingRepeatedPropertyRewritesRunThatWins) {
	constexpr std::string_view SOURCE = ".row {\n\twidth: 10px;\n\tcolor: red;\n\twidth: 30px;\n}\n";
	Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(SOURCE, rules);
	ASSERT_EQ(rules.size(), 1u);

	str written = UIBuilderOps::SetDeclaration(SOURCE, rules[0].body, rules[0].declarations, "width", "22px",
											   UIBuilderOps::DeclarationLayout::Block);
	EXPECT_EQ(written, ".row {\n\twidth: 10px;\n\tcolor: red;\n\twidth: 22px;\n}\n");

	Vector<UI::StyleRuleSource> reread{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(written, reread);
	ASSERT_EQ(reread.size(), 1u) << written;
	Vector<UIBuilderOps::PropertyRow> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildPropertyRows(reread[0].declarations, rows);
	for (const UIBuilderOps::PropertyRow& row : rows) {
		if (row.name == "width") {
			EXPECT_EQ(row.value, "22px");
		}
	}
}

TEST_F(UIBuilderOpsTest, SetOnEmptyRuleLandsInBraces) {
	constexpr std::string_view SOURCE = ".before { color: red; }\n\n.empty {\n}\n";
	Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(SOURCE, rules);
	ASSERT_EQ(rules.size(), 2u);
	ASSERT_TRUE(rules[1].declarations.empty());

	str written = UIBuilderOps::SetDeclaration(SOURCE, rules[1].body, rules[1].declarations, "width", "5px",
											   UIBuilderOps::DeclarationLayout::Block);

	Vector<UI::StyleRuleSource> reread{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(written, reread);
	ASSERT_EQ(reread.size(), 2u) << written;
	EXPECT_EQ(reread[0].declarations.size(), 1u) << written;
	ASSERT_EQ(reread[1].declarations.size(), 1u) << written;
	EXPECT_EQ(reread[1].selector, ".empty");
	EXPECT_EQ(reread[1].declarations[0].name, "width");
	EXPECT_EQ(reread[1].declarations[0].value, "5px");
}

TEST_F(UIBuilderOpsTest, InlineFragmentGrowsAtEnd) {
	constexpr std::string_view FRAGMENT = "width: 1px";
	Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations(FRAGMENT, declarations);

	str written = UIBuilderOps::SetDeclaration(FRAGMENT, FRAGMENT, declarations, "height", "2px",
											   UIBuilderOps::DeclarationLayout::Inline);
	EXPECT_EQ(written, "width: 1px; height: 2px;");
	EXPECT_EQ(written.find('\n'), str::npos);

	Vector<UI::StyleDeclarationSource> reread{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations(written, reread);
	ASSERT_EQ(reread.size(), 2u) << written;
	EXPECT_EQ(reread[0].name, "width");
	EXPECT_EQ(reread[1].name, "height");
	EXPECT_EQ(reread[1].value, "2px");
}

TEST_F(UIBuilderOpsTest, EmptyInlineTakesFirstDeclaration) {
	constexpr std::string_view FRAGMENT = "";
	Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations(FRAGMENT, declarations);
	ASSERT_TRUE(declarations.empty());

	str written = UIBuilderOps::SetDeclaration(FRAGMENT, FRAGMENT, declarations, "width", "3px",
											   UIBuilderOps::DeclarationLayout::Inline);
	EXPECT_EQ(written, "width: 3px;");
}

TEST_F(UIBuilderOpsTest, InlineStyleWritesNoNewline) {
	Parsed parsed(R"(<ui version="1">
	<panel id="root" style="width: 1px"/>
</ui>)");
	ASSERT_TRUE(parsed.ok) << parsed.error;
	const u32 root = IndexOfId(parsed.document, "root");

	const UIDocumentNode* node = UIBuilderOps::NodeAt(parsed.document, root);
	ASSERT_NE(node, nullptr);
	Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations(node->style, declarations);

	str written = UIBuilderOps::SetDeclaration(node->style, node->style, declarations, "height", "2px",
											   UIBuilderOps::DeclarationLayout::Inline);
	str error;
	ASSERT_TRUE(UIDocumentOps::SetStyle(parsed.document, root, written, error)) << error;

	const str document = parsed.document.Write();
	EXPECT_EQ(document.find("&#10;"), str::npos) << document;
	EXPECT_NE(document.find("width: 1px; height: 2px;"), str::npos) << document;
}

TEST_F(UIBuilderOpsTest, AppendedRuleCarriesAuthoredRunsVerbatim) {
	constexpr std::string_view FRAGMENT = "background-color: var(--slt-accent); width: 4px";
	Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations(FRAGMENT, declarations);
	Vector<std::string_view> runs{GetMemoryResource(MemTag::Debug)};
	for (const UI::StyleDeclarationSource& declaration : declarations) {
		runs.push_back(declaration.text);
	}

	str written = UIBuilderOps::AppendRule(".card { color: red; }\n", ".moved", runs);
	Vector<UI::StyleRuleSource> reread{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(written, reread);
	ASSERT_EQ(reread.size(), 2u) << written;
	EXPECT_EQ(reread[0].declarations.size(), 1u);
	ASSERT_EQ(reread[1].declarations.size(), 2u) << written;
	EXPECT_EQ(reread[1].selector, ".moved");
	EXPECT_EQ(reread[1].declarations[0].value, "var(--slt-accent)");

	EXPECT_EQ(written, ".card { color: red; }\n\n.moved {\n\tbackground-color: var(--slt-accent);\n\twidth: 4px;\n}\n");
}

namespace {

	str RenderFingerprint(UIDocumentAsset& document, UI::Context& context) {
		context.ClearChildren(context.GetRoot());
		Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
		UIInstantiateDesc desc;
		document.Instantiate(context, context.GetRoot(), desc, mapping);
		context.Update();

		str out;
		std::function<void(UI::ElementHandle, u32)> walk = [&](UI::ElementHandle handle, u32 depth) {
			const UI::Style& style = context.GetComputedStyle(handle);
			out += str(depth, ' ');
			out += std::to_string(style.width.value) + "|" + std::to_string(static_cast<i32>(style.width.unit)) + "|";
			out += std::to_string(style.opacity) + "|";
			out += str(context.GetTextString(handle));
			out += "\n";
			const UI::Element* element = context.GetElement(handle);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle childHandle = context.MakeHandle(child);
				walk(childHandle, depth + 1);
				child = context.GetElement(childHandle)->nextSibling;
			}
		};
		walk(context.GetRoot(), 0);
		return out;
	}

	void WriteFile(std::string_view virtualPath, std::string_view body) {
		Path resolved = FileSystem::Resolve(Path(str(virtualPath)));
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
		FileSystem::WriteTextFile(resolved, str(body));
	}

} // anonymous namespace

TEST_F(UIBuilderPanelFixture, UnpackingInstanceRendersSameUIItComposed) {
	WriteFile("project://Assets/Portrait.slxml",
			  R"(<ui version="1"><panel id="portrait" class="card" style="width: 40px"><panel id="who">Bob</panel></panel></ui>)");
	WriteFile("project://Assets/Lobby.slxml", R"(<ui version="1">
	<template src="project://Assets/Portrait.slxml" name="Portrait"/>
	<panel id="lobby">
		<instance template="Portrait" id="p1" class="mine" style="opacity: 0.5">
			<attribute-overrides element-id="who">Alice</attribute-overrides>
		</instance>
	</panel>
</ui>)");

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Lobby.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	UI::Context oracle(UI::ContextDesc{{400, 400}, 1.0f}, nullptr);
	const str packed = RenderFingerprint(*document, oracle);
	EXPECT_NE(packed.find("Alice"), str::npos);

	const u32 instance = IndexOfId(*document, "p1");
	ASSERT_NE(instance, UIBuilderOps::INVALID_NODE);
	str error;
	ASSERT_TRUE(UIDocumentOps::Unpack(*document, instance, false, error)) << error;

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(*document, rows);
	for (const UIBuilderOps::Row& row : rows) {
		EXPECT_TRUE(row.node->templateName.empty());
	}
	EXPECT_TRUE(document->templates.empty());

	UI::Context after(UI::ContextDesc{{400, 400}, 1.0f}, nullptr);
	EXPECT_EQ(RenderFingerprint(*document, after), packed);

	UIDocumentAsset probe;
	str parseError;
	EXPECT_TRUE(probe.Parse(document->Write(), parseError)) << parseError;
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, UnpackKeepsUnresolvedOverride) {
	WriteFile("project://Assets/Leaf.slxml",
			  R"(<ui version="1"><panel id="leafRoot"><panel id="deep">default</panel></panel></ui>)");
	WriteFile("project://Assets/Middle.slxml", R"(<ui version="1">
	<template src="project://Assets/Leaf.slxml" name="Leaf"/>
	<panel id="middleRoot"><instance template="Leaf" id="leaf"/></panel>
</ui>)");
	WriteFile("project://Assets/Outer.slxml", R"(<ui version="1">
	<template src="project://Assets/Middle.slxml" name="Middle"/>
	<panel id="outer"><instance template="Middle" id="m">
		<attribute-overrides element-id="deep">CHANGED</attribute-overrides>
	</instance></panel>
</ui>)");

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Outer.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	UI::Context oracle(UI::ContextDesc{{400, 400}, 1.0f}, nullptr);
	const str packed = RenderFingerprint(*document, oracle);
	ASSERT_NE(packed.find("CHANGED"), str::npos);

	const u32 instance = IndexOfId(*document, "m");
	ASSERT_NE(instance, UIBuilderOps::INVALID_NODE);
	str error;
	ASSERT_TRUE(UIDocumentOps::Unpack(*document, instance, false, error)) << error;

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(*document, rows);
	bool sawInstance = false;
	for (const UIBuilderOps::Row& row : rows) {
		sawInstance = sawInstance || !row.node->templateName.empty();
	}
	EXPECT_TRUE(sawInstance);

	UI::Context after(UI::ContextDesc{{400, 400}, 1.0f}, nullptr);
	EXPECT_EQ(RenderFingerprint(*document, after), packed);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, FullUnpackLeavesNoInstance) {
	WriteFile("project://Assets/Leaf.slxml",
			  R"(<ui version="1"><panel id="leafRoot"><panel id="deep">default</panel></panel></ui>)");
	WriteFile("project://Assets/Middle.slxml", R"(<ui version="1">
	<template src="project://Assets/Leaf.slxml" name="Leaf"/>
	<panel id="middleRoot"><instance template="Leaf" id="leaf"/></panel>
</ui>)");
	WriteFile("project://Assets/Outer.slxml", R"(<ui version="1">
	<template src="project://Assets/Middle.slxml" name="Middle"/>
	<panel id="outer"><instance template="Middle" id="m">
		<attribute-overrides element-id="deep">CHANGED</attribute-overrides>
	</instance></panel>
</ui>)");

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Outer.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);
	UI::Context oracle(UI::ContextDesc{{400, 400}, 1.0f}, nullptr);
	const str packed = RenderFingerprint(*document, oracle);

	str error;
	ASSERT_TRUE(UIDocumentOps::Unpack(*document, IndexOfId(*document, "m"), true, error)) << error;

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(*document, rows);
	for (const UIBuilderOps::Row& row : rows) {
		EXPECT_TRUE(row.node->templateName.empty());
	}
	EXPECT_TRUE(document->templates.empty());

	UI::Context after(UI::ContextDesc{{400, 400}, 1.0f}, nullptr);
	EXPECT_EQ(RenderFingerprint(*document, after), packed);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, TemplateRoundTripsSubtree) {
	WriteFile("project://Assets/Screen.slxml",
			  R"(<ui version="1"><panel id="screen"><panel id="card" class="c" style="width: 7px"><panel id="in">hi</panel></panel></panel></ui>)");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	UI::Context oracle(UI::ContextDesc{{400, 400}, 1.0f}, nullptr);
	const str before = RenderFingerprint(*document, oracle);

	const u32 card = IndexOfId(*document, "card");
	str error;
	ASSERT_TRUE(UIDocumentOps::CreateTemplate(*document, card, "project://Assets/Card.slxml", error)) << error;

	EXPECT_TRUE(FileSystem::Exists(FileSystem::Resolve(Path("project://Assets/Card.slxml"))));
	ASSERT_EQ(document->templates.size(), 1u);
	EXPECT_EQ(document->templates[0].name, "Card");

	UI::Context composed(UI::ContextDesc{{400, 400}, 1.0f}, nullptr);
	EXPECT_EQ(RenderFingerprint(*document, composed), before);

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(*document, rows);
	u32 instance = UIBuilderOps::INVALID_NODE;
	for (const UIBuilderOps::Row& row : rows) {
		if (!row.node->templateName.empty()) {
			instance = row.index;
		}
	}
	ASSERT_NE(instance, UIBuilderOps::INVALID_NODE);
	ASSERT_TRUE(UIDocumentOps::Unpack(*document, instance, false, error)) << error;
	UI::Context unpacked(UI::ContextDesc{{400, 400}, 1.0f}, nullptr);
	EXPECT_EQ(RenderFingerprint(*document, unpacked), before);
	AssetDatabase::UnloadAsset(asset);
}

namespace {

	void TypeInto(UI::Context& context, UI::ElementHandle field, std::string_view text) {
		ClickAt(context, CenterOf(context, field));
		InputEvent selectAll{};
		selectAll.type = InputEventType::KeyDown;
		selectAll.key = Key::A;
		selectAll.mods = KeyMod::Ctrl;
		context.InjectInput(selectAll);
		for (char c : text) {
			InputEvent typed{};
			typed.type = InputEventType::Text;
			typed.codepoint = static_cast<char32_t>(c);
			context.InjectInput(typed);
		}
		InputEvent commit{};
		commit.type = InputEventType::KeyDown;
		commit.key = Key::Enter;
		context.InjectInput(commit);
		context.Update();
	}

	UI::ElementHandle FieldOnRow(UI::Context& context, UI::ElementHandle from, std::string_view label) {
		UI::ElementHandle row = ParentOf(context, ParentOf(context, FirstDescendantWithText(context, from, label)));
		return context.IsAlive(row) ? FirstByClass(context, row, StringHash("slt-textfield")) : UI::ElementHandle{};
	}

	UI::DragPayload NodePayload(u64 panelId, std::initializer_list<u32> nodes) {
		UI::DragPayload payload;
		payload.type = "UIBuilderNode"_hash;
		UIBuilderNodeDragData data;
		data.panelId = panelId;
		data.nodes.assign(nodes.begin(), nodes.end());
		payload.data = Variant(std::move(data));
		payload.display = "row";
		return payload;
	}

} // anonymous namespace

TEST_F(UIBuilderPanelFixture, RowMenuDeleteUndoable) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="gone"/><panel id="kept"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);
	ASSERT_NE(IndexOfId(*document, "gone"), UIBuilderOps::INVALID_NODE);

	RightClickAt(context, CenterOf(context, RowLabelled(context, root, "#gone")));
	UI::ElementHandle item = FirstDescendantWithText(context, context.GetRoot(), "Delete");
	ASSERT_TRUE(context.IsAlive(item));
	ClickAt(context, CenterOf(context, item));

	EXPECT_EQ(IndexOfId(*document, "gone"), UIBuilderOps::INVALID_NODE);
	EXPECT_NE(IndexOfId(*document, "kept"), UIBuilderOps::INVALID_NODE);

	ASSERT_TRUE(UndoSystem::CanUndo());
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Delete Element");
	UndoSystem::Undo();
	EXPECT_NE(IndexOfId(*document, "gone"), UIBuilderOps::INVALID_NODE);

	Frame(*panel.Get());
	EXPECT_TRUE(RowLabelled(context, root, "#gone").IsValid());
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, DropOnArmedBandReordersSiblings) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root"><panel id="a"/><panel id="b"/><panel id="c"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);
	const u32 moved = IndexOfId(*document, "c");
	ASSERT_EQ(UIDocumentOps::LocationOf(*document, moved).childIndex, 2u);

	context.SetHoverSuppressed(true);
	Frame(*panel.Get());

	UI::ElementHandle rowB = RowLabelled(context, root, "#b");
	ASSERT_TRUE(context.IsAlive(rowB));
	const UI::Rect box = context.GetLayout(rowB).borderBox;
	Vec2 onBand{box.x + box.w * 0.5f, box.y - 1.0f};
	ASSERT_TRUE(context.HasClass(context.HitTest(onBand), EditorTheme::Class::InsertStrip));

	ASSERT_TRUE(context.DeliverDrop(NodePayload(panel.Get()->GetInstanceId(), {moved}), onBand));

	const u32 nowAt = IndexOfId(*document, "c");
	ASSERT_NE(nowAt, UIBuilderOps::INVALID_NODE);
	EXPECT_EQ(UIDocumentOps::LocationOf(*document, nowAt).childIndex, 1u);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, DroppingARowOntoAnotherReparentsIt) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="moved"/><panel id="host"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);
	const u32 moved = IndexOfId(*document, "moved");
	ASSERT_EQ(UIDocumentOps::LocationOf(*document, moved).parent, 0u);

	UI::ElementHandle hostRow = RowLabelled(context, root, "#host");
	ASSERT_TRUE(context.IsAlive(hostRow));
	ASSERT_TRUE(context.DeliverDrop(NodePayload(panel.Get()->GetInstanceId(), {moved}), CenterOf(context, hostRow)));

	const u32 host = IndexOfId(*document, "host");
	const u32 nowAt = IndexOfId(*document, "moved");
	ASSERT_NE(nowAt, UIBuilderOps::INVALID_NODE);
	EXPECT_EQ(UIDocumentOps::LocationOf(*document, nowAt).parent, host);
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Move Element");
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, ForeignBuilderDragRefused) {
	WriteDocument("project://Assets/A.slxml", R"(<panel id="rootA"><panel id="a1"/></panel>)");
	WriteDocument("project://Assets/B.slxml", R"(<panel id="rootB"><panel id="b1"/></panel>)");

	Handle<EditorPanel> holder = OpenBuilder();
	Handle<EditorPanel> bystander = OpenBuilder();
	ASSERT_TRUE(holder.IsValid() && bystander.IsValid());
	ASSERT_NE(holder.Get(), bystander.Get());

	UI::Context& context = PanelContext();
	UI::ElementHandle rootHolder = BuildPanelInto(context, *holder.Get());
	UI::ElementHandle rootBystander = BuildPanelInto(context, *bystander.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/B.slxml"));
	Frame(*holder.Get());
	Frame(*bystander.Get());
	UISystem::Update();

	Asset* assetB = AssetDatabase::LoadAsset(Path("project://Assets/B.slxml"));
	ASSERT_NE(assetB, nullptr);
	UIDocumentAsset* documentB = static_cast<UIDocumentAsset*>(assetB);
	const str before = documentB->Write();

	const u32 b1 = IndexOfId(*documentB, "b1");
	ASSERT_NE(b1, UIBuilderOps::INVALID_NODE);

	UI::ElementHandle rootRow = RowLabelled(context, rootHolder, "#rootB");
	ASSERT_TRUE(context.IsAlive(rootRow));

	EXPECT_FALSE(context.DeliverDrop(NodePayload(bystander.Get()->GetInstanceId(), {b1}), CenterOf(context, rootRow)));
	EXPECT_EQ(documentB->Write(), before);

	EXPECT_TRUE(context.DeliverDrop(NodePayload(holder.Get()->GetInstanceId(), {b1}), CenterOf(context, rootRow)));
	(void)rootBystander;
	AssetDatabase::UnloadAsset(assetB);
}

TEST_F(UIBuilderPanelFixture, DroppingALibraryTagCreatesThatWidget) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	UI::DragPayload payload;
	payload.type = "UIWidgetTag"_hash;
	payload.data = Variant(str("button"));
	payload.display = "<button>";
	ASSERT_TRUE(context.DeliverDrop(payload, CenterOf(context, RowLabelled(context, root, "#root"))));

	Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
	UIBuilderOps::BuildRows(*document, rows);
	ASSERT_EQ(rows.size(), 2u);
	EXPECT_EQ(rows[1].node->tag, "button");
	EXPECT_FALSE(rows[1].node->attributes.empty());
	UIDocumentAsset probe;
	str error;
	EXPECT_TRUE(probe.Parse(document->Write(), error)) << error;
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, IdFieldRenamesElement) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#root")));
	UI::ElementHandle inspector = PaneNamed(context, root, "Inspector");
	UI::ElementHandle field = FieldOnRow(context, inspector, "id");
	ASSERT_TRUE(context.IsAlive(field));
	TypeInto(context, field, "hud");

	EXPECT_EQ(document->root.id, "hud");
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Set Id");
	UndoSystem::Undo();
	EXPECT_EQ(document->root.id, "root");
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, EmptySelectorTakesProperty) {
	WriteSheet("project://Assets/Main.slss", ".card {\n}\n");
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root" class="card"/>)");
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/Screen.slxml")),
							  str("<ui version=\"1\">\n\t<sheet src=\"project://Assets/Main.slss\"/>\n"
								  "\t<panel id=\"root\" class=\"card\"/>\n</ui>"));

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle selectorRow = RowLabelled(context, PaneNamed(context, root, "StyleSheets"), ".card");
	ASSERT_TRUE(context.IsAlive(selectorRow));
	ClickAt(context, CenterOf(context, selectorRow));

	UI::ElementHandle inspector = PaneNamed(context, root, "Inspector");
	UI::ElementHandle field = FieldOnRow(context, inspector, "width");
	ASSERT_TRUE(context.IsAlive(field));
	TypeInto(context, field, "13px");

	Asset* sheetAsset = AssetDatabase::LoadAsset(Path("project://Assets/Main.slss"));
	ASSERT_NE(sheetAsset, nullptr);
	StyleSheetAsset* sheet = static_cast<StyleSheetAsset*>(sheetAsset);
	Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(sheet->source, rules);
	ASSERT_EQ(rules.size(), 1u) << sheet->source;
	ASSERT_EQ(rules[0].declarations.size(), 1u) << sheet->source;
	EXPECT_EQ(rules[0].declarations[0].name, "width");
	EXPECT_EQ(rules[0].declarations[0].value, "13px");
	AssetDatabase::UnloadAsset(sheetAsset);
}

TEST_F(UIBuilderPanelFixture, ExtractMovesStylesToClass) {
	WriteSheet("project://Assets/Main.slss", ".other { color: red; }\n");
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/Screen.slxml")),
							  str("<ui version=\"1\">\n\t<sheet src=\"project://Assets/Main.slss\"/>\n"
								  "\t<panel id=\"card\" style=\"width: 9px; opacity: 0.25\"/>\n</ui>"));

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#card")));
	UI::ElementHandle button =
		FirstDescendantWithText(context, PaneNamed(context, root, "Inspector"), "Extract Inlined Styles to New Class");
	ASSERT_TRUE(context.IsAlive(button));
	ClickAt(context, CenterOf(context, button));

	EXPECT_TRUE(document->root.style.empty()) << document->root.style;
	ASSERT_EQ(document->root.classCount, 1u);
	EXPECT_EQ(document->root.classes[0], "card");

	Asset* sheetAsset = AssetDatabase::LoadAsset(Path("project://Assets/Main.slss"));
	ASSERT_NE(sheetAsset, nullptr);
	StyleSheetAsset* sheet = static_cast<StyleSheetAsset*>(sheetAsset);
	Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(sheet->source, rules);
	ASSERT_EQ(rules.size(), 2u) << sheet->source;
	EXPECT_EQ(rules[1].selector, ".card");
	ASSERT_EQ(rules[1].declarations.size(), 2u) << sheet->source;
	EXPECT_EQ(rules[1].declarations[0].value, "9px");

	ASSERT_TRUE(UndoSystem::CanUndo());
	UndoSystem::Undo();
	EXPECT_EQ(document->root.style, "width: 9px; opacity: 0.25");
	EXPECT_EQ(document->root.classCount, 0u);
	Vector<UI::StyleRuleSource> reverted{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(sheet->source, reverted);
	EXPECT_EQ(reverted.size(), 1u) << sheet->source;

	AssetDatabase::UnloadAsset(sheetAsset);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, ExtractDocumentRefusesLeavesSheetAlone) {
	WriteSheet("project://Assets/Main.slss", ".other { color: red; }\n");
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/Screen.slxml")),
							  str("<ui version=\"1\">\n\t<sheet src=\"project://Assets/Main.slss\"/>\n"
								  "\t<panel id=\"root\"><button id=\"go\" class=\"a b c\" text=\"Go\" "
								  "style=\"width: 9px\"/></panel>\n</ui>"));

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);
	Asset* sheetAsset = AssetDatabase::LoadAsset(Path("project://Assets/Main.slss"));
	ASSERT_NE(sheetAsset, nullptr);
	StyleSheetAsset* sheet = static_cast<StyleSheetAsset*>(sheetAsset);
	const str sheetBefore = sheet->source;

	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#go")));
	UI::ElementHandle button =
		FirstDescendantWithText(context, PaneNamed(context, root, "Inspector"), "Extract Inlined Styles to New Class");
	ASSERT_TRUE(context.IsAlive(button));
	ClickAt(context, CenterOf(context, button));

	const UIDocumentNode* go = UIBuilderOps::NodeAt(*document, IndexOfId(*document, "go"));
	ASSERT_NE(go, nullptr);
	EXPECT_EQ(go->classCount, 3u);
	EXPECT_EQ(go->style, "width: 9px");
	EXPECT_EQ(sheet->source, sheetBefore);

	AssetDatabase::UnloadAsset(sheetAsset);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, AddingSelectorAppendsRuleToFirstSheet) {
	WriteSheet("project://Assets/Main.slss", ".card { color: red; }\n");
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/Screen.slxml")),
							  str("<ui version=\"1\">\n\t<sheet src=\"project://Assets/Main.slss\"/>\n"
								  "\t<panel id=\"root\"/>\n</ui>"));

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle field = FirstByClass(context, PaneNamed(context, root, "StyleSheets"), StringHash("slt-textfield"));
	ASSERT_TRUE(context.IsAlive(field));
	TypeInto(context, field, ".fresh");

	Asset* sheetAsset = AssetDatabase::LoadAsset(Path("project://Assets/Main.slss"));
	ASSERT_NE(sheetAsset, nullptr);
	StyleSheetAsset* sheet = static_cast<StyleSheetAsset*>(sheetAsset);
	Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(sheet->source, rules);
	ASSERT_EQ(rules.size(), 2u) << sheet->source;
	EXPECT_EQ(rules[1].selector, ".fresh");
	EXPECT_TRUE(rules[1].declarations.empty());

	Frame(*panel.Get());
	EXPECT_TRUE(FirstDescendantWithText(context, PaneNamed(context, root, "StyleSheets"), ".fresh").IsValid());
	AssetDatabase::UnloadAsset(sheetAsset);
}

TEST_F(UIBuilderPanelFixture, DuplicateSelectorRefused) {
	WriteSheet("project://Assets/Main.slss", ".card { color: red; }\n");
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/Screen.slxml")),
							  str("<ui version=\"1\">\n\t<sheet src=\"project://Assets/Main.slss\"/>\n"
								  "\t<panel id=\"root\"/>\n</ui>"));

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle field = FirstByClass(context, PaneNamed(context, root, "StyleSheets"), StringHash("slt-textfield"));
	ASSERT_TRUE(context.IsAlive(field));
	TypeInto(context, field, " .card ");

	Asset* sheetAsset = AssetDatabase::LoadAsset(Path("project://Assets/Main.slss"));
	ASSERT_NE(sheetAsset, nullptr);
	StyleSheetAsset* sheet = static_cast<StyleSheetAsset*>(sheetAsset);
	Vector<UI::StyleRuleSource> rules{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(sheet->source, rules);
	EXPECT_EQ(rules.size(), 1u) << sheet->source;
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);

	TypeInto(context, field, ".other");
	rules.clear();
	UI::StyleSheet::SplitSource(sheet->source, rules);
	EXPECT_EQ(rules.size(), 2u) << sheet->source;
	AssetDatabase::UnloadAsset(sheetAsset);
}

TEST_F(UIBuilderPanelFixture, StructuralEditDropsSelection) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root"><panel id="host"/><panel id="moved"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#host")));
	ASSERT_TRUE(FirstDescendantWithText(context, PaneNamed(context, root, "Inspector"), "Attributes").IsValid());
	UI::ElementHandle idField = FieldOnRow(context, PaneNamed(context, root, "Inspector"), "id");
	ASSERT_TRUE(context.IsAlive(idField));
	TypeInto(context, idField, "renamed");
	ASSERT_EQ(IndexOfId(*document, "renamed"), 1u);
	EXPECT_TRUE(FirstDescendantWithText(context, PaneNamed(context, root, "Inspector"), "Attributes").IsValid());

	const u32 moved = IndexOfId(*document, "moved");
	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#moved")));
	ASSERT_TRUE(FirstDescendantWithText(context, PaneNamed(context, root, "Inspector"), "Attributes").IsValid());
	ASSERT_TRUE(context.DeliverDrop(NodePayload(panel.Get()->GetInstanceId(), {moved}),
									CenterOf(context, RowLabelled(context, root, "#renamed"))));
	Frame(*panel.Get());

	UI::ElementHandle inspector = PaneNamed(context, root, "Inspector");
	EXPECT_FALSE(FirstDescendantWithText(context, inspector, "Attributes").IsValid());
	EXPECT_TRUE(FirstDescendantWithText(context, inspector, "Canvas").IsValid());
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, StructuralEditReExpands) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root"><panel id="branch"><panel id="leaf"/></panel></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle hierarchy = PaneNamed(context, root, "Hierarchy");
	ASSERT_TRUE(FirstDescendantWithText(context, hierarchy, "#leaf").IsValid());

	UI::ElementHandle branch = RowLabelled(context, root, "#branch");
	ASSERT_TRUE(context.IsAlive(branch));
	UI::ElementHandle arrow = FirstByClass(context, branch, StringHash("slt-tree-arrow"));
	ASSERT_TRUE(context.IsAlive(arrow));
	ClickAt(context, CenterOf(context, arrow));

	EXPECT_FALSE(FirstDescendantWithText(context, PaneNamed(context, root, "Hierarchy"), "#leaf").IsValid());

	RightClickAt(context, CenterOf(context, RowLabelled(context, root, "#branch")));
	UI::ElementHandle item = FirstDescendantWithText(context, context.GetRoot(), "Duplicate");
	ASSERT_TRUE(context.IsAlive(item));
	ClickAt(context, CenterOf(context, item));
	Frame(*panel.Get());

	EXPECT_EQ(CountDescendantsWithText(context, PaneNamed(context, root, "Hierarchy"), "#leaf"), 2u);
}

TEST_F(UIBuilderPanelFixture, PreviewRoutesHover) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root" style="width: 100%; height: 100%"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Frame(*panel.Get());

	UI::ElementHandle viewport = FirstByClass(context, root, StringHash("ed-viewport-placeholder"));
	ASSERT_TRUE(context.IsAlive(viewport));
	const Vec2 middle = CenterOf(context, viewport);

	context.InjectInput(Mouse(InputEventType::MouseMove, middle));
	context.Update();
	Frame(*panel.Get());

	UI::Context* preview = nullptr;
	UISystem::ForEachContext([](Handle<Window>, UI::Context&) {});
	for (UISurfaceId id = 1; id < 8 && !preview; ++id) {
		if (UI::Context* candidate = UISystem::GetSurfaceContext(id)) {
			preview = candidate;
		}
	}
	ASSERT_NE(preview, nullptr);
	EXPECT_FALSE(preview->IsAlive(preview->GetHovered()));

	UI::ElementHandle toggle = FirstDescendantWithText(context, root, "Preview");
	ASSERT_TRUE(context.IsAlive(toggle));
	ClickAt(context, CenterOf(context, toggle));
	Frame(*panel.Get());

	context.InjectInput(Mouse(InputEventType::MouseMove, middle));
	context.Update();
	Frame(*panel.Get());

	UI::ElementHandle hovered = preview->GetHovered();
	EXPECT_TRUE(preview->IsAlive(hovered));

	context.InjectInput(Mouse(InputEventType::MouseMove, {1.0f, 1.0f}));
	context.Update();
	Frame(*panel.Get());
	EXPECT_FALSE(preview->IsAlive(preview->GetHovered()));
}

TEST_F(UIBuilderPanelFixture, NameChipTracksScaledElement) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root" style="flex-direction: row">)"
				  R"(<panel id="a" style="width: 200px; height: 40px"/>)"
				  R"(<panel id="b" style="width: 200px; height: 40px"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	context.SetContentScale(2.0f);
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Frame(*panel.Get());
	ASSERT_FLOAT_EQ(context.GetContentScale(), 2.0f);

	UI::ElementHandle viewport = FirstByClass(context, root, StringHash("ed-viewport-placeholder"));
	ASSERT_TRUE(context.IsAlive(viewport));
	UI::ElementHandle canvasHost = ParentOf(context, viewport);
	ASSERT_TRUE(context.IsAlive(canvasHost));

	UI::Context* preview = nullptr;
	for (UISurfaceId id = 1; id < 8 && !preview; ++id) {
		if (UI::Context* candidate = UISystem::GetSurfaceContext(id)) {
			preview = candidate;
		}
	}
	ASSERT_NE(preview, nullptr);

	UI::ElementHandle documentRoot = preview->MakeHandle(preview->GetElement(preview->GetRoot())->firstChild);
	ASSERT_TRUE(preview->IsAlive(documentRoot));
	UI::ElementHandle first = preview->MakeHandle(preview->GetElement(documentRoot)->firstChild);
	ASSERT_TRUE(preview->IsAlive(first));
	UI::ElementHandle second = preview->MakeHandle(preview->GetElement(first)->nextSibling);
	ASSERT_TRUE(preview->IsAlive(second));
	const f32 elementDelta = preview->GetLayout(second).borderBox.x - preview->GetLayout(first).borderBox.x;
	ASSERT_GT(elementDelta, 1.0f);

	auto chipXAfterSelecting = [&](std::string_view label) {
		ClickAt(context, CenterOf(context, RowLabelled(context, root, label)));
		Frame(*panel.Get());
		Frame(*panel.Get());
		UI::ElementHandle chip = ParentOf(context, FirstDescendantWithText(context, canvasHost, label));
		EXPECT_TRUE(context.IsAlive(chip)) << label;
		return context.IsAlive(chip) ? context.GetLayout(chip).borderBox.x : 0.0f;
	};

	const f32 chipA = chipXAfterSelecting("#a");
	const f32 chipB = chipXAfterSelecting("#b");
	EXPECT_NEAR(chipB - chipA, elementDelta, 1.0f);
}

TEST_F(UIBuilderPanelFixture, TextPanesShowWrittenDocument) {
	WriteSheet("project://Assets/Main.slss", ".card { color: red; }\n");
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/Screen.slxml")),
							  str("<ui version=\"1\">\n\t<sheet src=\"project://Assets/Main.slss\"/>\n"
								  "\t<panel id=\"root\"/>\n</ui>"));

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle layoutPane = FirstDescendantWithText(context, root, "Layout document");
	ASSERT_TRUE(context.IsAlive(layoutPane));
	UI::ElementHandle paneRow = ParentOf(context, ParentOf(context, ParentOf(context, layoutPane)));
	EXPECT_FALSE(HasFlag(context.GetElement(paneRow)->flags, UI::ElementFlags::Visible));

	UI::ElementHandle toggle = FirstDescendantWithText(context, root, ">  Text preview");
	ASSERT_TRUE(context.IsAlive(toggle));
	ClickAt(context, CenterOf(context, toggle));
	UISystem::Update();
	EXPECT_TRUE(HasFlag(context.GetElement(paneRow)->flags, UI::ElementFlags::Visible));

	UI::ElementHandle body = ParentOf(context, FirstDescendantWithText(context, root, "Layout document"));
	str joined;
	std::function<void(UI::ElementHandle)> walk = [&](UI::ElementHandle handle) {
		joined += context.GetTextString(handle);
		const UI::Element* element = context.GetElement(handle);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle childHandle = context.MakeHandle(child);
			walk(childHandle);
			child = context.GetElement(childHandle)->nextSibling;
		}
	};
	walk(body);
	EXPECT_NE(joined.find("id=\"root\""), str::npos);
	EXPECT_NE(joined.find("  1  "), str::npos);
}

TEST_F(UIBuilderPanelFixture, OuterBandResizesColumn) {
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	Frame(*panel.Get());

	UI::ElementHandle outerSplit = context.MakeHandle(context.GetElement(root)->firstChild);
	ASSERT_TRUE(context.IsAlive(outerSplit));
	UI::ElementHandle leftColumn = context.MakeHandle(context.GetElement(outerSplit)->firstChild);
	ASSERT_TRUE(context.IsAlive(leftColumn));
	UI::ElementHandle band = context.MakeHandle(context.GetElement(leftColumn)->nextSibling);
	ASSERT_TRUE(context.IsAlive(band));
	ASSERT_TRUE(context.HasClass(band, StringHash("slt-splitter")));
	const UI::Rect box = context.GetLayout(band).borderBox;
	const f32 startWidth = context.GetLayout(leftColumn).borderBox.w;
	ASSERT_GT(startWidth, 0.0f);

	Vec2 grab{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	context.InjectInput(Mouse(InputEventType::MouseMove, grab));
	context.InjectInput(Mouse(InputEventType::MouseDown, grab));
	context.Update();
	context.InjectInput(Mouse(InputEventType::MouseMove, {grab.x + 70.0f, grab.y}));
	context.Update();

	EXPECT_NEAR(context.GetLayout(leftColumn).borderBox.w, startWidth + 70.0f, 1.0f);

	auto shareOf = [&context](UI::ElementHandle splitRoot) {
		UI::ElementHandle first = context.MakeHandle(context.GetElement(splitRoot)->firstChild);
		f32 total = context.GetLayout(splitRoot).content.w;
		return total > 0.0f ? context.GetLayout(first).borderBox.w / total : 0.0f;
	};
	const f32 draggedShare = shareOf(outerSplit);
	EXPECT_GT(draggedShare, 0.2f);

	UI::ElementHandle second = BuildPanelInto(context, *panel.Get());
	Frame(*panel.Get());
	UI::ElementHandle rebuiltOuter = context.MakeHandle(context.GetElement(second)->firstChild);
	ASSERT_TRUE(context.IsAlive(rebuiltOuter));
	EXPECT_NEAR(shareOf(rebuiltOuter), draggedShare, 0.01f);
}

TEST_F(UIBuilderPanelFixture, LeftPanesShareDraggableBand) {
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	Frame(*panel.Get());

	UI::ElementHandle leftColumn = context.MakeHandle(context.GetElement(context.MakeHandle(context.GetElement(root)->firstChild))->firstChild);
	UI::ElementHandle sheetsSplit = context.MakeHandle(context.GetElement(leftColumn)->firstChild);
	ASSERT_TRUE(context.IsAlive(sheetsSplit));
	UI::ElementHandle sheetsPane = context.MakeHandle(context.GetElement(sheetsSplit)->firstChild);
	UI::ElementHandle band = context.MakeHandle(context.GetElement(sheetsPane)->nextSibling);
	ASSERT_TRUE(context.HasClass(band, StringHash("slt-splitter")));

	const UI::Rect box = context.GetLayout(band).borderBox;
	EXPECT_GT(box.w, box.h);

	UI::ElementHandle header = FirstDescendantWithText(context, sheetsPane, "StyleSheets");
	ASSERT_TRUE(context.IsAlive(header));
	EXPECT_NEAR(context.GetLayout(header).borderBox.w, context.GetLayout(sheetsPane).borderBox.w, 1.0f);

	const f32 startHeight = context.GetLayout(sheetsPane).borderBox.h;
	ASSERT_GT(startHeight, 0.0f);
	Vec2 grab{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	context.InjectInput(Mouse(InputEventType::MouseMove, grab));
	context.InjectInput(Mouse(InputEventType::MouseDown, grab));
	context.Update();
	context.InjectInput(Mouse(InputEventType::MouseMove, {grab.x, grab.y + 50.0f}));
	context.Update();

	EXPECT_NEAR(context.GetLayout(sheetsPane).borderBox.h, startHeight + 50.0f, 1.0f);
}

TEST_F(UIBuilderPanelFixture, ToolbarControlsDontOverlap) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"/>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle file = FirstDescendantWithText(context, root, "File");
	ASSERT_TRUE(context.IsAlive(file));
	UI::ElementHandle bar = ParentOf(context, file);
	ASSERT_TRUE(context.IsAlive(bar));
	ASSERT_TRUE(FirstDescendantWithText(context, bar, "Preview").IsValid());

	UI::ElementHandle status = FirstDescendantWithText(context, bar, "project://Assets/Screen.slxml");
	ASSERT_TRUE(context.IsAlive(status));
	EXPECT_GT(context.GetLayout(status).borderBox.w, 0.0f);

	const UI::Element* barElement = context.GetElement(bar);
	ASSERT_NE(barElement, nullptr);
	f32 reached = context.GetLayout(bar).content.x;
	u32 controls = 0;
	for (u32 child = barElement->firstChild; child != UI::INVALID_UI_INDEX;) {
		UI::ElementHandle handle = context.MakeHandle(child);
		const UI::Rect& box = context.GetLayout(handle).borderBox;
		EXPECT_GE(box.x + 0.5f, reached) << "toolbar control " << controls;
		reached = box.x + box.w;
		++controls;
		child = context.GetElement(handle)->nextSibling;
	}
	EXPECT_GT(controls, 3u);
	const UI::Rect& content = context.GetLayout(bar).content;
	EXPECT_LE(reached, content.x + content.w + 0.5f);

	UI::ElementHandle narrow = context.CreateElement();
	context.AppendChild(context.GetRoot(), narrow);
	UI::Style narrowStyle;
	narrowStyle.width = UI::Length::Pixel(360.0f);
	narrowStyle.height = UI::Length::Percent(1.0f);
	context.SetStyle(narrow, narrowStyle);
	panel.Get()->BuildContent(context, narrow);
	UISystem::Update();

	UI::ElementHandle narrowBar = ParentOf(context, FirstDescendantWithText(context, narrow, "File"));
	ASSERT_TRUE(context.IsAlive(narrowBar));
	const UI::Element* narrowElement = context.GetElement(narrowBar);
	ASSERT_NE(narrowElement, nullptr);
	const UI::Rect& narrowContent = context.GetLayout(narrowBar).content;
	f32 narrowReached = narrowContent.x;
	for (u32 child = narrowElement->firstChild; child != UI::INVALID_UI_INDEX;) {
		UI::ElementHandle handle = context.MakeHandle(child);
		const UI::Rect& box = context.GetLayout(handle).borderBox;
		EXPECT_GE(box.x + 0.5f, narrowReached);
		narrowReached = box.x + box.w;
		child = context.GetElement(handle)->nextSibling;
	}
	EXPECT_LE(narrowReached, narrowContent.x + narrowContent.w + 0.5f);
}

TEST_F(UIBuilderPanelFixture, SavingWritesDocumentAndSheetsItEdited) {
	WriteSheet("project://Assets/Main.slss", ".card { color: red; }\n");
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/Screen.slxml")),
							  str("<ui version=\"1\">\n\t<sheet src=\"project://Assets/Main.slss\"/>\n"
								  "\t<panel id=\"root\"/>\n</ui>"));

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#root")));
	UI::ElementHandle field = FieldOnRow(context, PaneNamed(context, root, "Inspector"), "id");
	ASSERT_TRUE(context.IsAlive(field));
	TypeInto(context, field, "hud");
	ASSERT_EQ(document->root.id, "hud");

	UI::ElementHandle selectorField =
		FirstByClass(context, PaneNamed(context, root, "StyleSheets"), StringHash("slt-textfield"));
	ASSERT_TRUE(context.IsAlive(selectorField));
	TypeInto(context, selectorField, ".fresh");

	str onDisk;
	onDisk = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/Screen.slxml")));
	EXPECT_EQ(onDisk.find("hud"), str::npos);
	str sheetOnDisk = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/Main.slss")));
	EXPECT_EQ(sheetOnDisk.find(".fresh"), str::npos);

	UI::ElementHandle file = FirstDescendantWithText(context, root, "File");
	ASSERT_TRUE(context.IsAlive(file));
	ClickAt(context, CenterOf(context, file));
	UI::ElementHandle save = FirstDescendantWithText(context, context.GetRoot(), "Save");
	ASSERT_TRUE(context.IsAlive(save));
	ClickAt(context, CenterOf(context, save));

	onDisk = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/Screen.slxml")));
	EXPECT_NE(onDisk.find("id=\"hud\""), str::npos) << onDisk;
	sheetOnDisk = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/Main.slss")));
	EXPECT_NE(sheetOnDisk.find(".fresh"), str::npos) << sheetOnDisk;

	UIDocumentAsset probe;
	str error;
	ASSERT_TRUE(probe.Parse(onDisk, error)) << error;
	EXPECT_EQ(probe.Write(), document->Write());
	AssetDatabase::UnloadAsset(asset);
}

namespace {

	class UIBuilderReloadFixture : public UIBuilderPanelFixture {
	protected:
		void SetUp() override {
			UIBuilderPanelFixture::SetUp();
			EditorAssetOps::Initialize();
		}
		void TearDown() override {
			EditorAssetOps::Terminate();
			UIBuilderPanelFixture::TearDown();
		}
	};

} // anonymous namespace

TEST_F(UIBuilderReloadFixture, EditStaysUnsavedUntilSave) {
	WriteSheet("project://Assets/Main.slss", ".card { color: red; }\n");
	WriteDocument("project://Assets/Screen.slxml", R"(<sheet src="project://Assets/Main.slss"/><panel id="root"/>)");
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);
	ASSERT_FALSE(document->sheets.empty());
	StyleSheetAsset* sheet = document->sheets[0].sheet.Get();
	ASSERT_NE(sheet, nullptr);
	ASSERT_FALSE(EditorAssetOps::IsDirty(Handle<Asset>(document)));

	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#root")));
	TypeInto(context, FieldOnRow(context, PaneNamed(context, root, "Inspector"), "id"), "hud");
	ASSERT_EQ(document->root.id, "hud");
	EXPECT_TRUE(EditorAssetOps::IsDirty(Handle<Asset>(document)));
	EXPECT_GT(document->version, 0u);
	TypeInto(context, FirstByClass(context, PaneNamed(context, root, "StyleSheets"), StringHash("slt-textfield")), ".fresh");
	EXPECT_TRUE(EditorAssetOps::IsDirty(Handle<Asset>(sheet)));
	EXPECT_GT(sheet->version, 0u);

	ClickAt(context, CenterOf(context, FirstDescendantWithText(context, root, "File")));
	ClickAt(context, CenterOf(context, FirstDescendantWithText(context, context.GetRoot(), "Save")));
	EXPECT_FALSE(EditorAssetOps::IsDirty(Handle<Asset>(document)));
	EXPECT_FALSE(EditorAssetOps::IsDirty(Handle<Asset>(sheet)));

	UndoSystem::Undo();
	EXPECT_TRUE(EditorAssetOps::IsDirty(Handle<Asset>(sheet)));
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderReloadFixture, RefillRebuildsAndDropsSelection) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="a"/><panel id="b"/></panel>)");
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#b")));
	Frame(*panel.Get());
	ASSERT_TRUE(RowIsSelected(context, RowLabelled(context, root, "#b")));

	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="fresh"/><panel id="b2"/></panel>)");
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Assets/Screen.slxml")));
	Frame(*panel.Get());
	Frame(*panel.Get());

	EXPECT_TRUE(context.IsAlive(RowLabelled(context, root, "#fresh")));
	EXPECT_FALSE(context.IsAlive(RowLabelled(context, root, "#a")));
	ASSERT_TRUE(context.IsAlive(RowLabelled(context, root, "#b2")));
	EXPECT_FALSE(RowIsSelected(context, RowLabelled(context, root, "#b2")));
}

TEST_F(UIBuilderReloadFixture, RevertWorksWhileHeld) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="a"/></panel>)");
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Asset* held = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(held, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(held);

	ClickAt(context, CenterOf(context, RowLabelled(context, root, "#root")));
	TypeInto(context, FieldOnRow(context, PaneNamed(context, root, "Inspector"), "id"), "edited");
	ASSERT_EQ(document->root.id, "edited");
	ASSERT_TRUE(EditorAssetOps::IsDirty(Handle<Asset>(document)));

	ClickAt(context, CenterOf(context, FirstDescendantWithText(context, root, "File")));
	UI::ElementHandle revert = FirstDescendantWithText(context, context.GetRoot(), "Revert to Saved");
	ASSERT_TRUE(context.IsAlive(revert));
	ClickAt(context, CenterOf(context, revert));
	Frame(*panel.Get());

	EXPECT_EQ(document->root.id, "root");
	EXPECT_FALSE(EditorAssetOps::IsDirty(Handle<Asset>(document)));
	EXPECT_TRUE(context.IsAlive(RowLabelled(context, root, "#root")));
	AssetDatabase::UnloadAsset(held);
}

namespace {

	const UIDocumentNode* LastOfRoot(const UIDocumentAsset& document) {
		return document.root.children.empty() ? nullptr : &document.root.children.back();
	}

} // anonymous namespace

TEST_F(UIBuilderPanelFixture, TileDoubleClickAppends) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="a"/></panel>)");
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	UI::ElementHandle tile = FirstDescendantWithText(context, PaneNamed(context, root, "Library"), "button");
	ASSERT_TRUE(context.IsAlive(tile));
	const Vec2 at = CenterOf(context, tile);
	ClickAt(context, at);
	ClickAt(context, at);
	Frame(*panel.Get());

	ASSERT_EQ(document->root.children.size(), 2u);
	EXPECT_EQ(LastOfRoot(*document)->tag, "button");
	UI::ElementHandle row = RowLabelled(context, PaneNamed(context, root, "Hierarchy"), "button");
	ASSERT_TRUE(context.IsAlive(row));
	EXPECT_TRUE(RowIsSelected(context, row));

	UndoSystem::Undo();
	EXPECT_EQ(document->root.children.size(), 1u);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, LibraryTilesAddAppendsItToRoot) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"/>)");
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	UI::ElementHandle tile = FirstDescendantWithText(context, PaneNamed(context, root, "Library"), "slider");
	ASSERT_TRUE(context.IsAlive(tile));
	RightClickAt(context, CenterOf(context, tile));
	UI::ElementHandle add = FirstDescendantWithText(context, context.GetRoot(), "Add");
	ASSERT_TRUE(context.IsAlive(add));
	ClickAt(context, CenterOf(context, add));
	Frame(*panel.Get());

	ASSERT_EQ(document->root.children.size(), 1u);
	EXPECT_EQ(LastOfRoot(*document)->tag, "slider");
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, DroppedLibraryTileIsElementSelected) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="host"/></panel>)");
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::DragPayload payload;
	payload.type = "UIWidgetTag"_hash;
	payload.data = Variant(str("checkbox"));
	ASSERT_TRUE(context.DeliverDrop(payload, CenterOf(context, RowLabelled(context, root, "#host"))));
	Frame(*panel.Get());

	UI::ElementHandle row = RowLabelled(context, PaneNamed(context, root, "Hierarchy"), "checkbox");
	ASSERT_TRUE(context.IsAlive(row));
	EXPECT_TRUE(RowIsSelected(context, row));
}

TEST_F(UIBuilderPanelFixture, AddChildOffersLibrary) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="host"/><panel id="other"/></panel>)");
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/Screen.slxml"));
	ASSERT_NE(asset, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(asset);

	RightClickAt(context, CenterOf(context, RowLabelled(context, root, "#host")));
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	UI::ElementHandle addRow = FirstDescendantWithText(context, context.GetOpenPopup(0), "Add Child Element");
	ASSERT_TRUE(context.IsAlive(addRow));
	context.InjectInput(Mouse(InputEventType::MouseMove, CenterOf(context, addRow)));
	context.Update();
	ASSERT_EQ(context.GetOpenPopupCount(), 2u);
	UI::ElementHandle submenu = context.GetOpenPopup(1);
	EXPECT_TRUE(context.IsAlive(FirstDescendantWithText(context, submenu, "Element")));
	UI::ElementHandle button = FirstDescendantWithText(context, submenu, "button");
	ASSERT_TRUE(context.IsAlive(button));
	ClickAt(context, CenterOf(context, button));
	Frame(*panel.Get());

	const u32 host = UIDocumentOps::ChildAt(*document, 0, 0);
	ASSERT_EQ(UIDocumentOps::GetChildCount(*document, host), 1u);
	EXPECT_EQ(document->root.children[0].children[0].tag, "button");
	EXPECT_EQ(UIDocumentOps::GetChildCount(*document, UIDocumentOps::ChildAt(*document, 0, 1)), 0u);
	EXPECT_TRUE(RowIsSelected(context, RowLabelled(context, PaneNamed(context, root, "Hierarchy"), "button")));
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(UIBuilderPanelFixture, ScrollViewsFillPanes) {
	WriteDocument("project://Assets/Short.slxml", R"(<panel id="root"><button id="go" text="Go"/></panel>)");

	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Short.slxml"));
	Frame(*panel.Get());

	u32 checked = 0;
	for (u32 i = 0; i < 16; ++i) {
		UI::ElementHandle scrollRoot = FindByClass(context, root, StringHash("slt-scroll-root"), i);
		if (!context.IsAlive(scrollRoot)) {
			break;
		}
		const UI::Element* pod = context.GetElement(scrollRoot);
		ASSERT_NE(pod->parent, UI::INVALID_UI_INDEX);
		const UI::Rect host = context.GetLayout(context.MakeHandle(pod->parent)).content;
		if (host.h <= 0.0f) {
			continue;
		}
		const UI::Rect box = context.GetLayout(scrollRoot).borderBox;
		EXPECT_NEAR(box.y + box.h, host.y + host.h, 1.0f);
		++checked;
	}
	EXPECT_EQ(checked, 4u);
}

namespace {
	constexpr UI::Rect Pane{0.0f, 0.0f, 800.0f, 600.0f};
	constexpr i32 CANVAS_W = 1920;
	constexpr i32 CANVAS_H = 1080;

	Vec2 ToScreen(const UIBuilderOps::CanvasView& view, Vec2 canvasPoint) {
		const UI::Rect rect = UIBuilderOps::CanvasRectFor(Pane, CANVAS_W, CANVAS_H, view);
		return {rect.x + canvasPoint.x * view.zoom, rect.y + canvasPoint.y * view.zoom};
	}
} // namespace

TEST(UIBuilderCanvasTest, ZoomToFitFitsCanvas) {
	const f32 fit = UIBuilderOps::ZoomToFit(Pane, CANVAS_W, CANVAS_H);
	const UI::Rect rect = UIBuilderOps::CanvasRectFor(Pane, CANVAS_W, CANVAS_H, {fit, {}});
	EXPECT_LE(rect.w, Pane.w + 0.01f);
	EXPECT_LE(rect.h, Pane.h + 0.01f);
	EXPECT_NEAR(rect.w, Pane.w, 0.01f);
}

TEST(UIBuilderCanvasTest, WheelNotchKeepsCanvasPointUnderCursor) {
	const UIBuilderOps::CanvasView before{0.4f, {}};
	const Vec2 cursor{610.0f, 120.0f};
	const UI::Rect rect = UIBuilderOps::CanvasRectFor(Pane, CANVAS_W, CANVAS_H, before);
	const Vec2 anchor{(cursor.x - rect.x) / before.zoom, (cursor.y - rect.y) / before.zoom};

	const UIBuilderOps::CanvasView after =
		UIBuilderOps::ZoomAtPoint(Pane, CANVAS_W, CANVAS_H, before, cursor, 1.0f);
	ASSERT_GT(after.zoom, before.zoom);

	const Vec2 moved = ToScreen(after, anchor);
	EXPECT_NEAR(moved.x, cursor.x, 0.01f);
	EXPECT_NEAR(moved.y, cursor.y, 0.01f);
}

TEST(UIBuilderCanvasTest, ZoomingOutKeepsTheSamePointToo) {
	const UIBuilderOps::CanvasView before{1.0f, {40.0f, -25.0f}};
	const Vec2 cursor{200.0f, 450.0f};
	const UI::Rect rect = UIBuilderOps::CanvasRectFor(Pane, CANVAS_W, CANVAS_H, before);
	const Vec2 anchor{(cursor.x - rect.x) / before.zoom, (cursor.y - rect.y) / before.zoom};

	const UIBuilderOps::CanvasView after =
		UIBuilderOps::ZoomAtPoint(Pane, CANVAS_W, CANVAS_H, before, cursor, -1.0f);
	ASSERT_LT(after.zoom, before.zoom);

	const Vec2 moved = ToScreen(after, anchor);
	EXPECT_NEAR(moved.x, cursor.x, 0.01f);
	EXPECT_NEAR(moved.y, cursor.y, 0.01f);
}

TEST(UIBuilderCanvasTest, ManyNotchesDoNotDriftTheAnchor) {
	UIBuilderOps::CanvasView view{0.3f, {}};
	const Vec2 cursor{123.0f, 456.0f};
	const UI::Rect rect = UIBuilderOps::CanvasRectFor(Pane, CANVAS_W, CANVAS_H, view);
	const Vec2 anchor{(cursor.x - rect.x) / view.zoom, (cursor.y - rect.y) / view.zoom};

	for (u32 i = 0; i < 8; ++i) {
		view = UIBuilderOps::ZoomAtPoint(Pane, CANVAS_W, CANVAS_H, view, cursor, 1.0f);
	}
	const Vec2 moved = ToScreen(view, anchor);
	EXPECT_NEAR(moved.x, cursor.x, 0.5f);
	EXPECT_NEAR(moved.y, cursor.y, 0.5f);
}

TEST(UIBuilderCanvasTest, WheelZoomMatchesToolbar) {
	UIBuilderOps::CanvasView view{1.0f, {}};
	for (u32 i = 0; i < 64; ++i) {
		view = UIBuilderOps::ZoomAtPoint(Pane, CANVAS_W, CANVAS_H, view, {400.0f, 300.0f}, 1.0f);
	}
	EXPECT_FLOAT_EQ(view.zoom, UIBuilderOps::MAX_CANVAS_ZOOM);

	for (u32 i = 0; i < 256; ++i) {
		view = UIBuilderOps::ZoomAtPoint(Pane, CANVAS_W, CANVAS_H, view, {400.0f, 300.0f}, -1.0f);
	}
	EXPECT_FLOAT_EQ(view.zoom, UIBuilderOps::MIN_CANVAS_ZOOM);
}

TEST(UIBuilderCanvasTest, PanMovesCanvasByCursorTravel) {
	const UIBuilderOps::CanvasView before{1.0f, {}};
	const UI::Rect was = UIBuilderOps::CanvasRectFor(Pane, CANVAS_W, CANVAS_H, before);
	const UIBuilderOps::CanvasView after =
		UIBuilderOps::PanBy(Pane, CANVAS_W, CANVAS_H, before, {30.0f, -20.0f});
	const UI::Rect now = UIBuilderOps::CanvasRectFor(Pane, CANVAS_W, CANVAS_H, after);

	EXPECT_FLOAT_EQ(now.x - was.x, 30.0f);
	EXPECT_FLOAT_EQ(now.y - was.y, -20.0f);
	EXPECT_FLOAT_EQ(now.w, was.w);
}

TEST(UIBuilderCanvasTest, CanvasCannotBePannedOutOfReach) {
	for (f32 zoom : {0.25f, 1.0f, 2.0f}) {
		for (f32 sign : {1.0f, -1.0f}) {
			UIBuilderOps::CanvasView view{zoom, {}};
			for (u32 i = 0; i < 40; ++i) {
				view = UIBuilderOps::PanBy(Pane, CANVAS_W, CANVAS_H, view, {500.0f * sign, 500.0f * sign});
			}
			const UI::Rect rect = UIBuilderOps::CanvasRectFor(Pane, CANVAS_W, CANVAS_H, view);
			EXPECT_GT(rect.x + rect.w, Pane.x) << "zoom " << zoom << " sign " << sign;
			EXPECT_LT(rect.x, Pane.x + Pane.w) << "zoom " << zoom << " sign " << sign;
			EXPECT_GT(rect.y + rect.h, Pane.y) << "zoom " << zoom << " sign " << sign;
			EXPECT_LT(rect.y, Pane.y + Pane.h) << "zoom " << zoom << " sign " << sign;
		}
	}
}

TEST(UIBuilderCanvasTest, ZoomedInCanvasCanShowCorner) {
	UIBuilderOps::CanvasView view{2.0f, {}};
	for (u32 i = 0; i < 20; ++i) {
		view = UIBuilderOps::PanBy(Pane, CANVAS_W, CANVAS_H, view, {400.0f, 400.0f});
	}
	const UI::Rect rect = UIBuilderOps::CanvasRectFor(Pane, CANVAS_W, CANVAS_H, view);
	EXPECT_GT(rect.x, Pane.x - 1.0f);
	EXPECT_GT(rect.y, Pane.y - 1.0f);
	EXPECT_GT(rect.x + rect.w * 0.5f, Pane.x + Pane.w);
}

TEST(UIBuilderCanvasTest, ZeroPanIsCenteringBuilderHadBefore) {
	const UIBuilderOps::CanvasView view{0.25f, {}};
	const UI::Rect rect = UIBuilderOps::CanvasRectFor(Pane, CANVAS_W, CANVAS_H, view);
	EXPECT_FLOAT_EQ(rect.w, static_cast<f32>(CANVAS_W) * 0.25f);
	EXPECT_FLOAT_EQ(rect.x, Pane.x + (Pane.w - rect.w) * 0.5f);
	EXPECT_FLOAT_EQ(rect.y, Pane.y + (Pane.h - rect.h) * 0.5f);
}

TEST(UIBuilderCanvasTest, DegenerateCanvasOrPaneAnswersWithoutDividing) {
	const UIBuilderOps::CanvasView view{1.0f, {}};
	const UI::Rect empty{0.0f, 0.0f, 0.0f, 0.0f};
	EXPECT_FLOAT_EQ(UIBuilderOps::ZoomToFit(empty, CANVAS_W, CANVAS_H), 1.0f);
	EXPECT_FLOAT_EQ(UIBuilderOps::ZoomToFit(Pane, 0, 0), 1.0f);
	const UI::Rect rect = UIBuilderOps::CanvasRectFor(Pane, 0, 0, view);
	EXPECT_FLOAT_EQ(rect.w, 0.0f);
	EXPECT_FLOAT_EQ(rect.h, 0.0f);
	const UIBuilderOps::CanvasView after = UIBuilderOps::ZoomAtPoint(Pane, 0, 0, view, {10.0f, 10.0f}, 1.0f);
	EXPECT_FLOAT_EQ(after.zoom, 1.0f);
	EXPECT_FLOAT_EQ(after.pan.x, 0.0f);
	EXPECT_FLOAT_EQ(after.pan.y, 0.0f);
}

namespace {
	using UIBuilderOps::CodeLanguage;
	using UIBuilderOps::CodeToken;

	Vector<UIBuilderOps::CodeLine> Tokenize(std::string_view source, CodeLanguage language) {
		Vector<UIBuilderOps::CodeLine> out{GetMemoryResource(MemTag::Editor)};
		UIBuilderOps::TokenizeCode(source, language, out);
		return out;
	}

	CodeToken KindOf(const Vector<UIBuilderOps::CodeLine>& lines, std::string_view needle) {
		for (const UIBuilderOps::CodeLine& line : lines) {
			for (const UIBuilderOps::CodeSpan& span : line.spans) {
				if (span.text.find(needle) != std::string_view::npos) {
					return span.kind;
				}
			}
		}
		return CodeToken::Text;
	}

	u32 SpanCount(const Vector<UIBuilderOps::CodeLine>& lines) {
		u32 total = 0;
		for (const UIBuilderOps::CodeLine& line : lines) {
			total += static_cast<u32>(line.spans.size());
		}
		return total;
	}
} // namespace

TEST(UIBuilderCodeTest, MarkupSeparatesTagsAttributesAndValues) {
	const Vector<UIBuilderOps::CodeLine> lines =
		Tokenize("<Element id=\"page\" class=\"row\"/>", CodeLanguage::Markup);
	EXPECT_EQ(KindOf(lines, "Element"), CodeToken::Tag);
	EXPECT_EQ(KindOf(lines, "id"), CodeToken::Attribute);
	EXPECT_EQ(KindOf(lines, "\"page\""), CodeToken::Value);
	EXPECT_EQ(KindOf(lines, "class"), CodeToken::Attribute);
	EXPECT_EQ(KindOf(lines, "<"), CodeToken::Punctuation);
}

TEST(UIBuilderCodeTest, TagThenAttributes) {
	const Vector<UIBuilderOps::CodeLine> lines = Tokenize("<row row=\"row\"/>", CodeLanguage::Markup);
	ASSERT_FALSE(lines.empty());
	const Vector<UIBuilderOps::CodeSpan>& spans = lines[0].spans;
	ASSERT_GE(spans.size(), 4u);
	EXPECT_EQ(spans[1].kind, CodeToken::Tag);
	EXPECT_EQ(spans[2].kind, CodeToken::Attribute);
	EXPECT_EQ(spans[spans.size() - 2].kind, CodeToken::Value);
}

TEST(UIBuilderCodeTest, StyleSeparatesSelectorsPropertiesAndValues) {
	const Vector<UIBuilderOps::CodeLine> lines =
		Tokenize(".row:hover {\n\tbackground-color: #ff0000;\n}\n", CodeLanguage::Style);
	EXPECT_EQ(KindOf(lines, ".row"), CodeToken::Tag);
	EXPECT_EQ(KindOf(lines, "background-color"), CodeToken::Attribute);
	EXPECT_EQ(KindOf(lines, "#ff0000"), CodeToken::Value);
}

TEST(UIBuilderCodeTest, ACommentSpanningLinesStaysAComment) {
	const Vector<UIBuilderOps::CodeLine> markup =
		Tokenize("<a/>\n<!-- one\n   two -->\n<b/>\n", CodeLanguage::Markup);
	EXPECT_EQ(KindOf(markup, "one"), CodeToken::Comment);
	EXPECT_EQ(KindOf(markup, "two"), CodeToken::Comment);
	EXPECT_EQ(KindOf(markup, "b"), CodeToken::Tag);

	const Vector<UIBuilderOps::CodeLine> style = Tokenize("/* one\n   two */\n.r { a: b; }\n", CodeLanguage::Style);
	EXPECT_EQ(KindOf(style, "two"), CodeToken::Comment);
	EXPECT_EQ(KindOf(style, ".r"), CodeToken::Tag);
}

TEST(UIBuilderCodeTest, LineNumbersAreContiguousAndCountBlanks) {
	const Vector<UIBuilderOps::CodeLine> lines = Tokenize("<a/>\n\n<b/>\n", CodeLanguage::Markup);
	ASSERT_GE(lines.size(), 3u);
	for (size_t i = 0; i < lines.size(); ++i) {
		EXPECT_EQ(lines[i].number, static_cast<u32>(i) + 1) << "line " << i;
	}
	EXPECT_TRUE(lines[1].spans.empty());
}

TEST(UIBuilderCodeTest, AdjacentRunsOfOneKindAreMerged) {
	const Vector<UIBuilderOps::CodeLine> lines = Tokenize("<a b=\"1\" c=\"2\" d=\"3\"/>", CodeLanguage::Markup);
	ASSERT_FALSE(lines.empty());
	for (size_t i = 1; i < lines[0].spans.size(); ++i) {
		EXPECT_NE(lines[0].spans[i].kind, lines[0].spans[i - 1].kind);
	}
}

TEST(UIBuilderCodeTest, EveryByteOfTheSourceSurvivesTokenizing) {
	constexpr std::string_view SOURCE =
		"<ui version=\"1\">\n\t<!-- hi -->\n\t<Element id=\"a\" style=\"width: 10px;\"/>\n</ui>\n";
	const Vector<UIBuilderOps::CodeLine> lines = Tokenize(SOURCE, CodeLanguage::Markup);
	str rebuilt;
	for (size_t i = 0; i < lines.size(); ++i) {
		for (const UIBuilderOps::CodeSpan& span : lines[i].spans) {
			rebuilt += str(span.text);
		}
		if (i + 1 < lines.size()) {
			rebuilt += "\n";
		}
	}
	EXPECT_EQ(rebuilt, str(SOURCE));
}

TEST(UIBuilderCodeTest, UnterminatedRunsEndAtSource) {
	EXPECT_EQ(KindOf(Tokenize("<!-- never closed", CodeLanguage::Markup), "never"), CodeToken::Comment);
	EXPECT_EQ(KindOf(Tokenize("<a b=\"unclosed", CodeLanguage::Markup), "unclosed"), CodeToken::Value);
	EXPECT_EQ(KindOf(Tokenize("/* never closed", CodeLanguage::Style), "never"), CodeToken::Comment);
	EXPECT_EQ(Tokenize("", CodeLanguage::Markup).size(), 1u);
}

TEST(UIBuilderCodeTest, RealisticDocumentElementCost) {
	str source = "<ui version=\"1\">\n";
	constexpr u32 NODES = 200;
	for (u32 i = 0; i < NODES; ++i) {
		source += "\t<Element id=\"node";
		source += std::to_string(i);
		source += "\" class=\"row\" style=\"width: 100px; height: 20px;\"/>\n";
	}
	source += "</ui>\n";

	const Vector<UIBuilderOps::CodeLine> lines = Tokenize(source, CodeLanguage::Markup);
	const u32 spans = SpanCount(lines);
	const u32 rows = static_cast<u32>(lines.size());
	const u32 elements = spans + rows * 2;
	EXPECT_LT(spans / rows, 14u) << (spans / rows);
	EXPECT_LT(elements, 4000u) << spans << " spans + " << rows << ' ' << rows << "-line document";
}

namespace {
	void CollectTextRuns(UI::Context& context, UI::ElementHandle element, Vector<str>& out) {
		if (!context.IsAlive(element)) {
			return;
		}
		str text = context.GetTextString(element);
		if (!text.empty()) {
			out.push_back(text);
		}
		const UI::Element* pod = context.GetElement(element);
		for (u32 child = pod ? pod->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			CollectTextRuns(context, handle, out);
			const UI::Element* record = context.GetElement(handle);
			child = record ? record->nextSibling : UI::INVALID_UI_INDEX;
		}
	}

	UI::ElementHandle TextPreviewBar(UI::Context& context, UI::ElementHandle root) {
		UI::ElementHandle bar = FindByText(context, root, "v  Text preview");
		return context.IsAlive(bar) ? bar : FindByText(context, root, ">  Text preview");
	}
} // namespace

TEST_F(UIBuilderPanelFixture, TextPreviewUsesColoredRuns) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><button id="go" text="Go"/></panel>)");
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	UI::ElementHandle bar = TextPreviewBar(context, root);
	ASSERT_TRUE(context.IsAlive(bar));
	ClickElement(context, bar);
	Frame(*panel.Get());

	Vector<str> runs{GetMemoryResource(MemTag::Editor)};
	CollectTextRuns(context, root, runs);

	bool sawBareTag = false;
	bool sawGutter = false;
	for (const str& run : runs) {
		sawBareTag = sawBareTag || run == "panel " || run == "panel";
		sawGutter = sawGutter || run == "  1  ";
	}
	EXPECT_TRUE(sawBareTag);
	EXPECT_TRUE(sawGutter);
}

TEST_F(UIBuilderPanelFixture, ACollapsedTextPreviewBuildsNothing) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><button id="go" text="Go"/></panel>)");
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	Vector<str> collapsed{GetMemoryResource(MemTag::Editor)};
	CollectTextRuns(context, root, collapsed);

	UI::ElementHandle bar = TextPreviewBar(context, root);
	ASSERT_TRUE(context.IsAlive(bar));
	ClickElement(context, bar);
	Frame(*panel.Get());
	Vector<str> opened{GetMemoryResource(MemTag::Editor)};
	CollectTextRuns(context, root, opened);

	ASSERT_GT(opened.size(), collapsed.size() + 8u);

	ClickElement(context, bar);
	Frame(*panel.Get());
	Vector<str> reclosed{GetMemoryResource(MemTag::Editor)};
	CollectTextRuns(context, root, reclosed);
	EXPECT_EQ(reclosed.size(), collapsed.size());
}

TEST_F(UIBuilderPanelFixture, WheelZoomRefreshesInspectorsZoomRow) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"/>)");
	Handle<EditorPanel> panel = OpenBuilder();
	ASSERT_TRUE(panel.IsValid());
	UI::Context& context = PanelContext();
	UI::ElementHandle root = BuildPanelInto(context, *panel.Get());
	ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder("project://Assets/Screen.slxml"));
	Frame(*panel.Get());

	auto zoomRowText = [&]() {
		Vector<str> runs{GetMemoryResource(MemTag::Editor)};
		CollectTextRuns(context, root, runs);
		for (size_t i = 0; i + 1 < runs.size(); ++i) {
			if (runs[i] == "zoom") {
				return runs[i + 1];
			}
		}
		return str();
	};
	const str before = zoomRowText();
	ASSERT_FALSE(before.empty());

	UI::ElementHandle viewport = FindByClass(context, root, EditorTheme::Class::ViewportPlaceholder, 0);
	ASSERT_TRUE(context.IsAlive(viewport));
	const UI::Rect box = context.GetLayout(viewport).borderBox;
	ASSERT_GT(box.w, 0.0f);

	InputEvent move{};
	move.type = InputEventType::MouseMove;
	move.mousePosPx = {static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)};
	context.InjectInput(move);
	InputEvent wheel{};
	wheel.type = InputEventType::MouseWheel;
	wheel.wheel = {0.0f, 1.0f};
	context.InjectInput(wheel);
	context.Update();
	Frame(*panel.Get());

	EXPECT_NE(zoomRowText(), before) << before;
}
