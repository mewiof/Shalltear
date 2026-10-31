#include "TestsPCH.h"
#include "UITestUtil.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/GraphBlackboard.h"
#include "Shalltear/Editor/PanelManager.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	struct Owner {
		struct Item {
			u64 id = 0;
			str name;
		};
		Vector<Item> items{GetMemoryResource(MemTag::Editor)};
		u64 nextId = 100;
		bool dirty = true;

		u32 adds = 0;
		u32 addedKind = 0;
		u32 renames = 0;
		u32 moves = 0;
		u32 movedTo = 0;
		Vector<u64> deleted{GetMemoryResource(MemTag::Editor)};
		u32 deletes = 0;
		Vector<u64> selected{GetMemoryResource(MemTag::Editor)};
		bool oneKind = false;

		str Order() const {
			str out;
			for (const Item& item : items) {
				out += item.name + " ";
			}
			return out;
		}

		i32 IndexOf(u64 id) const {
			for (size_t index = 0; index < items.size(); ++index) {
				if (items[index].id == id) {
					return static_cast<i32>(index);
				}
			}
			return -1;
		}

		GraphBlackboardDesc Desc() {
			GraphBlackboardDesc desc;
			desc.title = "Properties";
			desc.kinds.emplace_back("Float");
			if (!oneKind) {
				desc.kinds.emplace_back("Int");
			}
			desc.onAdd = [this](u32 kind) -> u64 {
				++adds;
				addedKind = kind;
				items.push_back(Item{nextId, str("New ") + (kind == 0 ? "Float" : "Int")});
				dirty = true;
				return nextId++;
			};
			desc.onRename = [this](u64 id, std::string_view name) -> u64 {
				++renames;
				const i32 index = IndexOf(id);
				if (index < 0) {
					return 0;
				}
				items[static_cast<size_t>(index)].name = str(name);
				dirty = true;
				return id;
			};
			desc.onMove = [this](u64 id, u32 index) -> u64 {
				++moves;
				movedTo = index;
				const i32 from = IndexOf(id);
				if (from < 0) {
					return 0;
				}
				Item item = items[static_cast<size_t>(from)];
				items.erase(items.begin() + from);
				items.insert(items.begin() + index, item);
				dirty = true;
				return id;
			};
			desc.onDelete = [this](std::span<const u64> ids) {
				++deletes;
				deleted.assign(ids.begin(), ids.end());
				for (u64 id : ids) {
					const i32 index = IndexOf(id);
					if (index >= 0) {
						items.erase(items.begin() + index);
					}
				}
				dirty = true;
			};
			desc.onSelectionChanged = [this](std::span<const u64> ids) { selected.assign(ids.begin(), ids.end()); };
			return desc;
		}
	};

	class BoardTestPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(BoardTestPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Board Test")
		SLT_END_REFLECT()

		Owner owner;
		GraphBlackboard board;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override {
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Column;
			style.width = UI::Length::Pixel(260.0f);
			style.height = UI::Length::Pixel(400.0f);
			style.overflow = UI::Overflow::Scroll;
			context.SetStyle(root, style);
			UI::ElementHandle host = context.CreateElement();
			context.AppendChild(root, host);
			UI::Style column;
			column.flexDirection = UI::FlexDirection::Column;
			column.width = UI::Length::Percent(1.0f);
			context.SetStyle(host, column);
			board.Build(this, context, host, owner.Desc());
		}
	};

	class GraphBlackboardTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			BoardTestPanel::GetReflectType();
			EditorSelection::Initialize();
			PanelManager::Initialize();
		}
		void TearDown() override {
			PanelManager::Terminate();
			EditorSelection::Terminate();
		}
	};

	struct BoardHarness {
		MockFontSystem fonts;
		UI::Context context;
		Handle<EditorPanel> panel;
		Owner& owner;
		GraphBlackboard& board;
		UI::ElementHandle root;

		static BoardTestPanel& Of(const Handle<EditorPanel>& panel) { return *static_cast<BoardTestPanel*>(panel.Get()); }

		explicit BoardHarness(std::initializer_list<const char*> names, bool oneKind = false)
			: context(Desc(), &fonts), panel(PanelManager::OpenPanel(BoardTestPanel::GetReflectType())),
			  owner(Of(panel).owner), board(Of(panel).board) {
			EditorTheme::ApplyToContext(context);
			owner.oneKind = oneKind;
			for (const char* name : names) {
				owner.items.push_back(Owner::Item{owner.nextId++, str(name)});
			}
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel->BuildContent(context, root);
			Tick();
		}
		static UI::ContextDesc Desc() {
			UI::ContextDesc desc;
			desc.sizePx = {600, 500};
			return desc;
		}

		void Tick() {
			if (owner.dirty) {
				owner.dirty = false;
				Vector<GraphBlackboardItem> items{GetMemoryResource(MemTag::Editor)};
				for (const Owner::Item& item : owner.items) {
					GraphBlackboardItem shown;
					shown.id = item.id;
					shown.name = item.name;
					items.push_back(std::move(shown));
				}
				board.SetItems(items);
			}
			context.Update();
			board.Tick(0.016f);
			context.Update();
		}

		UI::ElementHandle Row(std::string_view name) { return FindByText(context, root, name); }

		void Press(Key key, KeyMod mods = KeyMod::None) {
			InputEvent down{};
			down.type = InputEventType::KeyDown;
			down.key = key;
			down.mods = mods;
			context.InjectInput(down);
			context.Update();
			InputEvent up = down;
			up.type = InputEventType::KeyUp;
			context.InjectInput(up);
			context.Update();
		}

		void Type(std::string_view text) {
			for (const char c : text) {
				InputEvent event{};
				event.type = InputEventType::Text;
				event.codepoint = static_cast<char32_t>(c);
				context.InjectInput(event);
				context.Update();
			}
		}

		UI::ElementHandle Field() {
			const UI::ElementHandle focused = context.GetFocused();
			return context.IsAlive(focused) && context.HasTextInput(focused) ? focused : UI::ElementHandle{};
		}

		UI::ElementHandle Plus() {
			const UI::Element* title = context.GetElement(FindByText(context, root, "Properties"));
			if (!title || title->nextSibling == UI::INVALID_UI_INDEX) {
				return {};
			}
			return context.MakeHandle(title->nextSibling);
		}

		UI::ElementHandle Band(u32 n) { return FindByClass(context, root, EditorTheme::Class::InsertStrip, n); }

		Vec2 Center(UI::ElementHandle element) {
			const UI::Rect box = context.GetLayout(element).borderBox;
			return Vec2(box.x + box.w * 0.5f, box.y + box.h * 0.5f);
		}

		void Inject(InputEventType type, Vec2 at, KeyMod mods = KeyMod::None) {
			InputEvent event{};
			event.type = type;
			event.button = MouseButton::Left;
			event.mousePosPx = Vec2I(static_cast<i32>(at.x), static_cast<i32>(at.y));
			event.mods = mods;
			context.InjectInput(event);
			context.Update();
		}

		void Click(std::string_view name, KeyMod mods = KeyMod::None) {
			const Vec2 at = Center(Row(name));
			Inject(InputEventType::MouseMove, at, mods);
			Inject(InputEventType::MouseDown, at, mods);
			Inject(InputEventType::MouseUp, at, mods);
		}

		UI::DragPayload DragRow(std::string_view name) {
			Click(name);
			const Vec2 from = Center(Row(name));
			UISystem::Initialize();
			Inject(InputEventType::MouseDown, from);
			for (i32 step = 1; step <= 3; ++step) {
				Inject(InputEventType::MouseMove, Vec2(from.x, from.y + 8.0f * static_cast<f32>(step)));
			}
			const UI::DragPayload* payload = UISystem::GetDragPayload();
			UI::DragPayload copy = payload ? *payload : UI::DragPayload{};
			UISystem::CancelDragPayload();
			UISystem::Terminate();
			Inject(InputEventType::MouseUp, Vec2(from.x, from.y + 24.0f));
			return copy;
		}

		void Arm(bool armed) {
			context.SetHoverSuppressed(armed);
			Tick();
		}
	};

} // anonymous namespace

TEST_F(GraphBlackboardTest, PlusMenuAddsNamingItem) {
	BoardHarness harness({"Speed", "Grounded"});
	ASSERT_TRUE(harness.context.IsAlive(harness.Plus()));
	ClickElement(harness.context, harness.Plus());
	UI::ElementHandle kind = FindByText(harness.context, harness.context.GetRoot(), "Int");
	ASSERT_TRUE(harness.context.IsAlive(kind));
	ClickElement(harness.context, kind);
	EXPECT_EQ(harness.owner.adds, 1u);
	EXPECT_EQ(harness.owner.addedKind, 1u);

	harness.Tick();
	harness.Tick();
	const UI::ElementHandle field = harness.Field();
	ASSERT_TRUE(harness.context.IsAlive(field));
	EXPECT_EQ(harness.context.GetTextString(field), "New Int");

	harness.Type("Health");
	harness.Press(Key::Enter);
	harness.Tick();
	harness.Tick();
	EXPECT_EQ(harness.owner.renames, 1u);
	EXPECT_EQ(harness.owner.Order(), "Speed Grounded Health ");
	EXPECT_TRUE(harness.context.IsAlive(harness.Row("Health")));
	ASSERT_EQ(harness.owner.selected.size(), 1u);
	EXPECT_EQ(harness.owner.selected[0], harness.owner.items.back().id);

	ASSERT_TRUE(harness.panel->OnCommand(PanelCommand::Delete));
	ASSERT_EQ(harness.owner.deletes, 1u);
	ASSERT_EQ(harness.owner.deleted.size(), 1u);
	EXPECT_EQ(harness.owner.items.size(), 2u);
	EXPECT_EQ(harness.owner.Order(), "Speed Grounded ");
}

TEST_F(GraphBlackboardTest, SingleKindBoardAddsDirectly) {
	BoardHarness harness({"Speed"}, true);
	ASSERT_TRUE(harness.context.IsAlive(harness.Plus()));
	ClickElement(harness.context, harness.Plus());
	EXPECT_EQ(harness.owner.adds, 1u);
	EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, harness.context.GetRoot(), "Float")));

	harness.Tick();
	harness.Tick();
	const UI::ElementHandle field = harness.Field();
	ASSERT_TRUE(harness.context.IsAlive(field));
	EXPECT_EQ(harness.context.GetTextString(field), "New Float");
}

TEST_F(GraphBlackboardTest, DoubleClickRenamesEscapeCancels) {
	BoardHarness harness({"Speed", "Grounded"});
	DoubleClickElement(harness.context, harness.Row("Grounded"));
	harness.Tick();
	const UI::ElementHandle field = harness.Field();
	ASSERT_TRUE(harness.context.IsAlive(field));
	EXPECT_EQ(harness.context.GetTextString(field), "Grounded");

	harness.Type("Airborne");
	harness.Press(Key::Escape);
	harness.Tick();
	harness.Tick();
	EXPECT_EQ(harness.owner.renames, 0u);
	EXPECT_FALSE(harness.context.IsAlive(harness.Field()));
	EXPECT_TRUE(harness.context.IsAlive(harness.Row("Grounded")));

	harness.Click("Speed");
	ASSERT_TRUE(harness.panel->OnCommand(PanelCommand::Rename));
	harness.Tick();
	ASSERT_TRUE(harness.context.IsAlive(harness.Field()));
	EXPECT_EQ(harness.context.GetTextString(harness.Field()), "Speed");
}

TEST_F(GraphBlackboardTest, LoneItemNameOpens) {
	BoardHarness harness({"Speed"});
	DoubleClickElement(harness.context, harness.Row("Speed"));
	harness.Tick();
	ASSERT_TRUE(harness.context.IsAlive(harness.Field()));
	EXPECT_EQ(harness.context.GetTextString(harness.Field()), "Speed");
	harness.Type("Velocity");
	harness.Press(Key::Enter);
	harness.Tick();
	harness.Tick();
	EXPECT_EQ(harness.owner.Order(), "Velocity ");
	EXPECT_TRUE(harness.context.IsAlive(harness.Row("Velocity")));
}

TEST_F(GraphBlackboardTest, DeletedItemTakesNameField) {
	BoardHarness harness({"Speed", "Grounded"});
	DoubleClickElement(harness.context, harness.Row("Grounded"));
	harness.Tick();
	ASSERT_TRUE(harness.context.IsAlive(harness.Field()));

	harness.owner.items.erase(harness.owner.items.begin() + 1);
	harness.owner.dirty = true;
	harness.Tick();
	EXPECT_FALSE(harness.context.IsAlive(harness.Field()));
	EXPECT_FALSE(harness.context.IsAlive(harness.Row("Grounded")));
	harness.Type("x");
	harness.Press(Key::Enter);
	harness.Tick();
	EXPECT_EQ(harness.owner.renames, 0u);
}

TEST_F(GraphBlackboardTest, ItemDragMovesKeepsSelection) {
	BoardHarness harness({"A", "B", "C"});
	const UI::DragPayload payload = harness.DragRow("A");
	ASSERT_TRUE(payload.type.GetValue() != 0);

	harness.Arm(true);
	EXPECT_FALSE(harness.context.DeliverDrop(payload, harness.Center(harness.Band(0))));
	EXPECT_FALSE(harness.context.DeliverDrop(payload, harness.Center(harness.Band(1))));
	EXPECT_EQ(harness.owner.moves, 0u);

	ASSERT_TRUE(harness.context.DeliverDrop(payload, harness.Center(harness.Band(2))));
	EXPECT_EQ(harness.owner.moves, 1u);
	EXPECT_EQ(harness.owner.movedTo, 1u);
	harness.Arm(false);
	harness.Tick();
	EXPECT_EQ(harness.owner.Order(), "B A C ");

	const UI::DragPayload again = harness.DragRow("B");
	harness.Arm(true);
	ASSERT_TRUE(harness.context.DeliverDrop(again, harness.Center(harness.Band(3))));
	harness.Arm(false);
	harness.Tick();
	EXPECT_EQ(harness.owner.Order(), "A C B ");
	EXPECT_EQ(harness.owner.movedTo, 2u);

	const UI::DragPayload last = harness.DragRow("B");
	harness.Arm(true);
	ASSERT_TRUE(harness.context.DeliverDrop(last, harness.Center(harness.Band(0))));
	harness.Arm(false);
	harness.Tick();
	EXPECT_EQ(harness.owner.Order(), "B A C ");
	EXPECT_EQ(harness.owner.movedTo, 0u);

	ASSERT_TRUE(harness.panel->OnCommand(PanelCommand::Delete));
	ASSERT_EQ(harness.owner.deleted.size(), 1u);
	EXPECT_EQ(harness.owner.Order(), "A C ");
}

TEST_F(GraphBlackboardTest, ItemDraggedFromAnotherBoardIsRefused) {
	BoardHarness harness({"A", "B", "C"});
	Owner other;
	for (const Owner::Item& item : harness.owner.items) {
		other.items.push_back(Owner::Item{item.id, item.name + "'"});
	}
	UI::ElementHandle second = harness.context.CreateElement();
	harness.context.AppendChild(harness.context.GetRoot(), second);
	UI::Style style;
	style.flexDirection = UI::FlexDirection::Column;
	style.width = UI::Length::Pixel(260.0f);
	style.height = UI::Length::Pixel(300.0f);
	harness.context.SetStyle(second, style);
	GraphBlackboard board;
	board.Build(nullptr, harness.context, second, other.Desc());
	Vector<GraphBlackboardItem> items{GetMemoryResource(MemTag::Editor)};
	for (const Owner::Item& item : other.items) {
		GraphBlackboardItem shown;
		shown.id = item.id;
		shown.name = item.name;
		items.push_back(std::move(shown));
	}
	board.SetItems(items);
	harness.context.Update();

	const UI::DragPayload payload = harness.DragRow("A");
	ASSERT_TRUE(payload.type.GetValue() != 0);
	harness.context.SetHoverSuppressed(true);
	board.Tick(0.016f);
	harness.Tick();
	const UI::ElementHandle foreign = FindByClass(harness.context, second, EditorTheme::Class::InsertStrip, 3);
	ASSERT_TRUE(harness.context.IsAlive(foreign));
	EXPECT_FALSE(harness.context.DeliverDrop(payload, harness.Center(foreign)));
	EXPECT_EQ(other.moves, 0u);
	EXPECT_EQ(harness.owner.moves, 0u);
}

TEST_F(GraphBlackboardTest, RowMenuDeletesSelectionOrRowAlone) {
	BoardHarness harness({"A", "B", "C"});
	harness.Click("A");
	harness.Click("B", KeyMod::Ctrl);

	RightClickElement(harness.context, harness.Row("C"));
	ClickElement(harness.context, FindByText(harness.context, harness.context.GetRoot(), "Delete"));
	ASSERT_EQ(harness.owner.deleted.size(), 1u);
	harness.Tick();
	EXPECT_EQ(harness.owner.Order(), "A B ");

	RightClickElement(harness.context, harness.Row("A"));
	ClickElement(harness.context, FindByText(harness.context, harness.context.GetRoot(), "Delete"));
	EXPECT_EQ(harness.owner.deleted.size(), 2u);
	harness.Tick();
	EXPECT_EQ(harness.owner.Order(), "");
}
