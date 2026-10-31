#include "TestsPCH.h"

#include "EditorDockRig.h"
#include "UITestUtil.h"

#include "Shalltear/Editor/EditorConsole.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"

#include <format>

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr u32 ROWS_BELOW_CAP = 400;

	class ConsoleCostTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			EditorSelection::Initialize();
			PanelManager::Initialize();
			EditorConsole::Initialize();
			EditorConsole::Clear();
		}
		void TearDown() override {
			EditorConsole::Terminate();
			PanelManager::Terminate();
			EditorSelection::Terminate();
		}
	};

	UI::ContextDesc MakeContextDesc() {
		UI::ContextDesc desc;
		desc.sizePx = {900, 500};
		desc.contentScale = 1.0f;
		return desc;
	}

	struct BareScrollFixture {
		CountingFont font;
		UI::Context ctx;
		UI::Widgets::ScrollViewParts scroll;
		u32 rows = 0;

		BareScrollFixture() : ctx(MakeContextDesc(), &font) {
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.alignItems = UI::Align::Stretch;
			ctx.SetStyle(ctx.GetRoot(), rootStyle);

			scroll = UI::Widgets::ScrollView(ctx, ctx.GetRoot());
			UI::Style scrollStyle = ctx.GetStyle(scroll.root);
			scrollStyle.flexGrow = 1.0f;
			ctx.SetStyle(scroll.root, scrollStyle);
		}

		void AddRow() {
			UI::Style blockStyle;
			blockStyle.flexDirection = UI::FlexDirection::Column;
			blockStyle.width = UI::Length::Percent(1.0f);
			UI::ElementHandle block = MakeChild(ctx, scroll.viewport, blockStyle);

			UI::Style rowStyle;
			rowStyle.flexDirection = UI::FlexDirection::Row;
			rowStyle.width = UI::Length::Percent(1.0f);
			UI::ElementHandle row = MakeChild(ctx, block, rowStyle);

			UI::Style tagStyle;
			tagStyle.width = UI::Length::Pixel(20.0f);
			ctx.SetText(MakeChild(ctx, row, tagStyle), {}, 12.0f, Color::White(), "I");

			UI::Style messageStyle;
			messageStyle.flexGrow = 1.0f;
			ctx.SetText(MakeChild(ctx, row, messageStyle), {}, 12.0f, Color::White(),
						std::format("[MyProject] Direction {}", rows++));
		}

		u32 ShapesPerAppendedRow(u32 rowsOnScreen, u32 frames) {
			while (rows < rowsOnScreen) {
				AddRow();
			}
			ctx.Update();
			u32 before = font.Walks();
			for (u32 frame = 0; frame < frames; ++frame) {
				AddRow();
				ctx.Update();
			}
			return (font.Walks() - before) / frames;
		}
	};

	bool RowIsOnScreen(const UI::Context& context, UI::ElementHandle viewport, UI::ElementHandle row) {
		if (!context.IsAlive(row) || !context.IsAlive(viewport)) {
			return false;
		}
		const UI::Rect view = context.GetLayout(viewport).borderBox;
		const UI::Rect box = context.GetLayout(row).borderBox;
		return box.h > 0.0f && box.y >= view.y - 0.5f && box.y + box.h <= view.y + view.h + 0.5f;
	}

	u32 CountElements(const UI::Context& context, UI::ElementHandle element) {
		const UI::Element* pod = context.GetElement(element);
		if (!pod) {
			return 0;
		}
		u32 count = 1;
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			count += CountElements(context, handle);
			child = context.GetElement(handle)->nextSibling;
		}
		return count;
	}

	struct ConsoleFixture {
		CountingFont font;
		UI::Context ctx;
		Handle<EditorPanel> panel;
		UI::ElementHandle root;
		u32 pushed = 0;

		explicit ConsoleFixture(f32 hostWidthPx = 0.0f, f32 contentScale = 1.0f) : ctx(MakeDesc(contentScale), &font) {
			panel = PanelManager::OpenPanel(TypeRegistry::FindType("ConsolePanel"_hash));
			UI::ElementHandle parent = ctx.GetRoot();
			if (hostWidthPx > 0.0f) {
				UI::Style hostStyle;
				hostStyle.flexDirection = UI::FlexDirection::Column;
				hostStyle.width = UI::Length::Pixel(hostWidthPx);
				hostStyle.height = UI::Length::Percent(1.0f);
				parent = MakeChild(ctx, parent, hostStyle);
			}
			root = ctx.CreateElement();
			ctx.AppendChild(parent, root);
			panel.Get()->BuildContent(ctx, root);
			ctx.Update();
		}
		~ConsoleFixture() {
			PanelManager::ClosePanel(panel);
			PanelManager::TickAll(0.0f);
		}

		static UI::ContextDesc MakeDesc(f32 contentScale) {
			UI::ContextDesc desc;
			desc.sizePx = {900, 500};
			desc.contentScale = contentScale;
			return desc;
		}

		void PushOne() {
			EditorConsole::Push(LogLevel::Info, "MyProject", std::format("Direction {}", pushed++), ConsoleOrigin{});
		}
		void PushMany(u32 count) {
			for (u32 i = 0; i < count; ++i) {
				PushOne();
			}
		}

		void Frame() {
			PanelManager::TickAll(0.016f);
			ctx.Update();
		}

		u32 Elements() const {
			return CountElements(ctx, root);
		}

		u32 ElementsPerAppendedEntry(u32 rowsOnScreen, u32 frames) {
			PushMany(rowsOnScreen);
			SettleTail();
			u32 before = Elements();
			for (u32 frame = 0; frame < frames; ++frame) {
				PushOne();
				Frame();
			}
			return (Elements() - before) / frames;
		}

		void SettleTail() {
			for (u32 frame = 0; frame < 3; ++frame) {
				Frame();
			}
		}

		u32 ShapesPerAppendedEntry(u32 rowsOnScreen, u32 frames) {
			PushMany(rowsOnScreen);
			SettleTail();
			u32 before = font.Walks();
			for (u32 frame = 0; frame < frames; ++frame) {
				PushOne();
				Frame();
			}
			return (font.Walks() - before) / frames;
		}

		void CollectAlive(UI::ElementHandle element, Vector<UI::ElementHandle>& out) const {
			const UI::Element* pod = ctx.GetElement(element);
			if (!pod) {
				return;
			}
			for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = ctx.MakeHandle(child);
				out.push_back(handle);
				CollectAlive(handle, out);
				child = ctx.GetElement(handle)->nextSibling;
			}
		}

		static void CollectText(const UI::Context& context, UI::ElementHandle element, Vector<str>& out) {
			str text = context.GetTextString(element);
			if (!text.empty()) {
				out.push_back(text);
			}
			const UI::Element* pod = context.GetElement(element);
			if (!pod) {
				return;
			}
			for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				CollectText(context, handle, out);
				child = context.GetElement(handle)->nextSibling;
			}
		}

		UI::ElementHandle Viewport() {
			return FindByClass(ctx, root, StringHash("slt-scroll-viewport"), 0);
		}
		UI::ElementHandle AutoscrollBox() {
			return FindByClass(ctx, root, StringHash("slt-checkbox"), 0);
		}
		UI::ElementHandle FirstRowTag() {
			UI::ElementHandle viewport = Viewport();
			const UI::Element* pod = ctx.GetElement(viewport);
			for (u32 child = pod ? pod->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle block = ctx.MakeHandle(child);
				if (ctx.GetElement(block)->firstChild != UI::INVALID_UI_INDEX) {
					return NthChildOf(ctx, NthChildOf(ctx, block, 0), 0);
				}
				child = ctx.GetElement(block)->nextSibling;
			}
			return {};
		}
		UI::ElementHandle Toolbar() { return NthChildOf(ctx, root, 0); }

		void ToggleKind(u32 kindIndex) {
			UI::ElementHandle button = FindByClass(ctx, root, StringHash("ed-tool-button"), kindIndex);
			ASSERT_TRUE(ctx.IsAlive(button));
			ClickElement(ctx, button);
			ctx.Update();
		}
		f32 ScrollY() {
			const UI::Element* viewport = ctx.GetElement(Viewport());
			return viewport ? viewport->scrollOffset.y : -1.0f;
		}
		void ScrollTo(f32 y) {
			ctx.SetScrollOffset(Viewport(), {0.0f, y});
			Frame();
			Frame();
		}
		f32 MaxScrollY() {
			const UI::Element* viewport = ctx.GetElement(Viewport());
			return viewport ? viewport->maxScroll.y : -1.0f;
		}
		struct RowPlacement {
			u32 realized = 0;
			u32 inView = 0;
		};
		RowPlacement PlaceRows() {
			RowPlacement placement;
			UI::ElementHandle viewport = Viewport();
			const UI::Rect view = ctx.GetLayout(viewport).borderBox;
			const UI::Element* pod = ctx.GetElement(viewport);
			for (u32 child = pod ? pod->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle block = ctx.MakeHandle(child);
				if (ctx.GetElement(block)->firstChild != UI::INVALID_UI_INDEX) {
					++placement.realized;
					const UI::Rect box = ctx.GetLayout(block).borderBox;
					if (box.y < view.y + view.h && view.y < box.y + box.h) {
						++placement.inView;
					}
				}
				child = ctx.GetElement(block)->nextSibling;
			}
			return placement;
		}
		bool Autoscroll() {
			Variant value;
			return GetField(panel.Get(), panel.Get()->GetType(), "autoscroll"_hash, &value) && value.As<bool>();
		}
		bool BoxIsLit() {
			const UI::Element* box = ctx.GetElement(AutoscrollBox());
			if (!box || box->firstChild == UI::INVALID_UI_INDEX) {
				return false;
			}
			const UI::Element* mark = ctx.GetElement(ctx.MakeHandle(box->firstChild));
			return mark && HasFlag(mark->flags, UI::ElementFlags::Visible);
		}
		void WheelOverList(f32 notches) {
			UI::Rect box = ctx.GetLayout(Viewport()).borderBox;
			Vec2I at{static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)};
			InputEvent move{};
			move.type = InputEventType::MouseMove;
			move.mousePosPx = at;
			ctx.InjectInput(move);
			InputEvent wheel{};
			wheel.type = InputEventType::MouseWheel;
			wheel.mousePosPx = at;
			wheel.wheel = {0.0f, notches};
			ctx.InjectInput(wheel);
			ctx.Update();
		}

		bool ShowsMessage(std::string_view needle) const {
			Vector<str> texts{GetMemoryResource(MemTag::Editor)};
			CollectText(ctx, root, texts);
			for (const str& text : texts) {
				if (text.find(needle) != str::npos) {
					return true;
				}
			}
			return false;
		}

		UI::ElementHandle FindText(UI::ElementHandle element, std::string_view needle) const {
			if (ctx.GetTextString(element).find(needle) != str::npos) {
				return element;
			}
			const UI::Element* pod = ctx.GetElement(element);
			if (!pod) {
				return {};
			}
			for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = ctx.MakeHandle(child);
				UI::ElementHandle found = FindText(handle, needle);
				if (ctx.IsAlive(found)) {
					return found;
				}
				child = ctx.GetElement(handle)->nextSibling;
			}
			return {};
		}
	};

} // anonymous namespace

TEST_F(ConsoleCostTest, IdleConsoleWithFullBacklogIsFree) {
	ConsoleFixture fixture;
	fixture.PushMany(ROWS_BELOW_CAP);
	fixture.Frame();
	fixture.Frame();

	u32 shapesBefore = fixture.font.Walks();
	fixture.ctx.ResetLayoutStats();
	constexpr u32 FRAMES = 10;
	for (u32 frame = 0; frame < FRAMES; ++frame) {
		fixture.Frame();
	}

	EXPECT_EQ(fixture.font.Walks(), shapesBefore);
	EXPECT_EQ(fixture.ctx.GetLayoutStats().measureWork, 0u);
}

TEST_F(ConsoleCostTest, AnAppendPreservesEveryExistingRowElement) {
	ConsoleFixture fixture;
	fixture.PushMany(50);
	fixture.SettleTail();

	Vector<UI::ElementHandle> watched{GetMemoryResource(MemTag::Editor)};
	for (u32 i = 45; i < 50; ++i) {
		UI::ElementHandle row = fixture.FindText(fixture.root, std::format("Direction {}", i));
		ASSERT_TRUE(fixture.ctx.IsAlive(row)) << "Direction " << i;
		watched.push_back(row);
	}

	for (u32 frame = 0; frame < 10; ++frame) {
		fixture.PushOne();
		fixture.Frame();
	}

	for (u32 i = 0; i < watched.size(); ++i) {
		EXPECT_TRUE(fixture.ctx.IsAlive(watched[i]));
		if (fixture.ctx.IsAlive(watched[i])) {
			EXPECT_NE(fixture.ctx.GetTextString(watched[i]).find(std::format("Direction {}", 45 + i)), str::npos);
		}
	}
}

TEST_F(ConsoleCostTest, FullBacklogAppendSkipsSiblingSearch) {
	auto linkWorkForBacklog = [](u32 backlog) {
		EditorConsole::Clear();
		ConsoleFixture fixture;
		fixture.PushMany(backlog);
		fixture.SettleTail();
		EXPECT_GT(fixture.Elements(), 50u);
		fixture.ctx.ResetTreeStats();
		for (u32 frame = 0; frame < 10; ++frame) {
			fixture.PushOne();
			fixture.Frame();
		}
		return fixture.ctx.GetTreeStats();
	};

	UI::TreeStats small = linkWorkForBacklog(100);
	UI::TreeStats large = linkWorkForBacklog(ROWS_BELOW_CAP);

	EXPECT_EQ(large.linkWork, small.linkWork);
	EXPECT_GT(large.linkOps, 0u);
}

TEST_F(ConsoleCostTest, AppendShapingMatchesScrollView) {
	u32 bare = 0;
	{
		BareScrollFixture reference;
		bare = reference.ShapesPerAppendedRow(ROWS_BELOW_CAP, 10);
	}
	u32 console = 0;
	{
		ConsoleFixture fixture;
		console = fixture.ShapesPerAppendedEntry(ROWS_BELOW_CAP, 10);
	}

	constexpr u32 COUNTS_LABEL_SHAPES = 2;
	EXPECT_LE(console, bare + COUNTS_LABEL_SHAPES);
}

TEST_F(ConsoleCostTest, CostFlatUnderLogStream) {
	ConsoleFixture fixture;
	u32 early = fixture.ShapesPerAppendedEntry(ROWS_BELOW_CAP, 20);
	u32 late = fixture.ShapesPerAppendedEntry(0, 400);

	EXPECT_LE(late, early + early / 4) << early << " to " << late;

	u32 atCap = fixture.Elements();
	for (u32 frame = 0; frame < 50; ++frame) {
		fixture.PushOne();
		fixture.Frame();
	}
	EXPECT_EQ(fixture.Elements(), atCap) << atCap << " -> " << fixture.Elements();
}

TEST_F(ConsoleCostTest, AnAppendedEntryActuallyAppears) {
	ConsoleFixture fixture;
	fixture.PushMany(20);
	fixture.Frame();
	ASSERT_TRUE(fixture.ShowsMessage("Direction 19"));

	fixture.PushOne();
	fixture.Frame();
	EXPECT_TRUE(fixture.ShowsMessage("Direction 20"));
	EXPECT_TRUE(fixture.ShowsMessage("Direction 0"));
}

TEST_F(ConsoleCostTest, RealizedRowsDontGrowWithBacklog) {
	u32 few = 0;
	{
		ConsoleFixture fixture;
		fixture.PushMany(100);
		fixture.SettleTail();
		few = fixture.Elements();
	}
	EditorConsole::Clear();
	u32 many = 0;
	{
		ConsoleFixture fixture;
		fixture.PushMany(ROWS_BELOW_CAP);
		fixture.SettleTail();
		many = fixture.Elements();
	}

	EXPECT_EQ(many, few);
	EXPECT_GT(few, 20u);
}

TEST_F(ConsoleCostTest, ScrolledAwayRowRedraws) {
	ConsoleFixture fixture;
	fixture.PushMany(400);
	fixture.SettleTail();
	ASSERT_TRUE(fixture.ShowsMessage("Direction 399"));
	ASSERT_FALSE(fixture.ShowsMessage("Direction 0"));

	fixture.ScrollTo(0.0f);
	EXPECT_TRUE(fixture.ShowsMessage("Direction 0"));

	fixture.ScrollTo(fixture.MaxScrollY());
	EXPECT_TRUE(fixture.ShowsMessage("Direction 399"));
}

TEST_F(ConsoleCostTest, RowCapEvictsOldestNotNewest) {
	ConsoleFixture fixture;
	fixture.PushMany(700);
	fixture.SettleTail();

	EXPECT_TRUE(fixture.ShowsMessage("Direction 699"));

	fixture.ScrollTo(0.0f);
	EXPECT_FALSE(fixture.ShowsMessage("Direction 0"));
	EXPECT_TRUE(fixture.ShowsMessage("Direction 200"));
}

TEST_F(ConsoleCostTest, RowCapEvictsDripAndBurst) {
	ConsoleFixture fixture;
	fixture.PushMany(490);
	fixture.SettleTail();
	for (u32 frame = 0; frame < 60; ++frame) {
		fixture.PushOne();
		fixture.Frame();
	}

	EXPECT_FALSE(fixture.ShowsMessage("Direction 0"));
	EXPECT_TRUE(fixture.ShowsMessage("Direction 549"));
}

TEST_F(ConsoleCostTest, ChangingLevelFilterStillRerealizesRows) {
	ConsoleFixture fixture;
	fixture.PushMany(10);
	for (u32 i = 0; i < 5; ++i) {
		EditorConsole::Push(LogLevel::Error, "MyProject", std::format("Boom {}", i), ConsoleOrigin{});
	}
	fixture.Frame();
	ASSERT_TRUE(fixture.ShowsMessage("Direction 0"));
	ASSERT_TRUE(fixture.ShowsMessage("Boom 0"));

	fixture.ToggleKind(1);

	const TypeInfo* type = fixture.panel.Get()->GetType();
	Variant shown;
	ASSERT_TRUE(GetField(fixture.panel.Get(), type, "showMessages"_hash, &shown));
	ASSERT_FALSE(shown.As<bool>());

	fixture.Frame();
	EXPECT_FALSE(fixture.ShowsMessage("Direction 0"));
	EXPECT_TRUE(fixture.ShowsMessage("Boom 0"));

	fixture.PushOne();
	fixture.Frame();
	EXPECT_FALSE(fixture.ShowsMessage("Direction 10"));
}

TEST_F(ConsoleCostTest, DroppedEntryIsAccounted) {
	ConsoleFixture fixture;
	fixture.PushMany(4200);
	fixture.SettleTail();

	EXPECT_GT(EditorConsole::GetDroppedCount(), 0u);
	EXPECT_TRUE(fixture.ShowsMessage("Direction 4199"));
}

TEST_F(ConsoleCostTest, TailFollowsNewestRowAfterBurst) {
	ConsoleFixture fixture;
	fixture.PushMany(200);
	fixture.Frame();
	fixture.Frame();

	ASSERT_GT(fixture.MaxScrollY(), 0.0f);
	EXPECT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());
	EXPECT_TRUE(fixture.Autoscroll());
}

TEST_F(ConsoleCostTest, ScrollingUpOutOfTailClearsBox) {
	ConsoleFixture fixture;
	fixture.PushMany(200);
	fixture.Frame();
	fixture.Frame();
	ASSERT_TRUE(fixture.Autoscroll());

	fixture.WheelOverList(3.0f);
	fixture.Frame();
	EXPECT_FALSE(fixture.Autoscroll());
	EXPECT_FALSE(fixture.BoxIsLit());

	f32 parked = fixture.ScrollY();
	fixture.PushMany(20);
	fixture.Frame();
	fixture.Frame();
	EXPECT_FLOAT_EQ(fixture.ScrollY(), parked);
}

TEST_F(ConsoleCostTest, ScrollingBackDownOntoEndResumesTail) {
	ConsoleFixture fixture;
	fixture.PushMany(200);
	fixture.SettleTail();
	fixture.WheelOverList(3.0f);
	fixture.Frame();
	ASSERT_FALSE(fixture.Autoscroll());
	fixture.PushMany(20);
	fixture.Frame();
	fixture.Frame();
	ASSERT_LT(fixture.ScrollY(), fixture.MaxScrollY());

	fixture.WheelOverList(-1000.0f);
	fixture.Frame();
	EXPECT_TRUE(fixture.Autoscroll());
	EXPECT_TRUE(fixture.BoxIsLit());

	fixture.PushMany(20);
	fixture.Frame();
	fixture.Frame();
	EXPECT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());
}

TEST_F(ConsoleCostTest, UntickedTailHoldsUntilScroll) {
	ConsoleFixture fixture;
	fixture.PushMany(200);
	fixture.SettleTail();
	ASSERT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());

	ClickElement(fixture.ctx, fixture.AutoscrollBox());
	ASSERT_FALSE(fixture.Autoscroll());
	fixture.Frame();
	fixture.WheelOverList(-3.0f);
	fixture.Frame();
	fixture.Frame();
	EXPECT_FALSE(fixture.Autoscroll());

	fixture.PushMany(20);
	fixture.Frame();
	fixture.Frame();
	EXPECT_LT(fixture.ScrollY(), fixture.MaxScrollY());
	EXPECT_FALSE(fixture.Autoscroll());

	fixture.WheelOverList(-1000.0f);
	fixture.Frame();
	EXPECT_TRUE(fixture.Autoscroll());
}

TEST_F(ConsoleCostTest, ClosingStoppingRowResumesTail) {
	ConsoleFixture fixture;
	for (u32 i = 0; i < 200; ++i) {
		EditorConsole::Push(LogLevel::Info, "MyProject", std::format("Direction {}", i), ConsoleOrigin{"Steering.cpp", i + 1, 0});
	}
	fixture.SettleTail();
	ASSERT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());

	UI::ElementHandle message = fixture.FindText(fixture.root, "Direction 199");
	ASSERT_TRUE(fixture.ctx.IsAlive(message));
	fixture.ctx.Events(fixture.ctx.MakeHandle(fixture.ctx.GetElement(message)->parent)).onDoubleClick = []() {};

	ClickElement(fixture.ctx, message);
	fixture.Frame();
	ASSERT_FALSE(fixture.Autoscroll());
	ASSERT_LT(fixture.ScrollY(), fixture.MaxScrollY());

	ClickElement(fixture.ctx, message);
	fixture.Frame();
	EXPECT_TRUE(fixture.Autoscroll());
	EXPECT_TRUE(fixture.BoxIsLit());

	fixture.PushMany(10);
	fixture.Frame();
	fixture.Frame();
	EXPECT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());

	ClickElement(fixture.ctx, fixture.AutoscrollBox());
	ASSERT_FALSE(fixture.Autoscroll());
	fixture.Frame();
	ClickElement(fixture.ctx, message);
	fixture.Frame();
	ClickElement(fixture.ctx, message);
	fixture.Frame();
	ASSERT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());
	EXPECT_FALSE(fixture.Autoscroll());
}

TEST_F(ConsoleCostTest, TickingBoxResumesTailWithNothingArriving) {
	ConsoleFixture fixture;
	fixture.PushMany(200);
	fixture.Frame();
	fixture.Frame();
	fixture.WheelOverList(3.0f);
	fixture.Frame();
	ASSERT_FALSE(fixture.Autoscroll());
	ASSERT_LT(fixture.ScrollY(), fixture.MaxScrollY());

	ClickElement(fixture.ctx, fixture.AutoscrollBox());
	ASSERT_TRUE(fixture.Autoscroll());
	fixture.Frame();
	EXPECT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());
}

TEST_F(ConsoleCostTest, TheTailSurvivesTheListGettingShorter) {
	ConsoleFixture fixture;
	fixture.PushMany(200);
	for (u32 i = 0; i < 3; ++i) {
		EditorConsole::Push(LogLevel::Error, "MyProject", std::format("Boom {}", i), ConsoleOrigin{});
	}
	fixture.Frame();
	fixture.Frame();
	ASSERT_TRUE(fixture.Autoscroll());
	ASSERT_GT(fixture.MaxScrollY(), 0.0f);

	fixture.ToggleKind(1);
	fixture.Frame();
	fixture.Frame();
	EXPECT_TRUE(fixture.Autoscroll());
	EXPECT_TRUE(fixture.BoxIsLit());
}

TEST_F(ConsoleCostTest, HidingKindShowsRemainingRows) {
	ConsoleFixture fixture;
	fixture.PushMany(36);
	for (u32 i = 0; i < 4; ++i) {
		EditorConsole::Push(LogLevel::Warn, "MyProject", std::format("Careful {}", i), ConsoleOrigin{});
	}
	for (u32 i = 0; i < 3; ++i) {
		EditorConsole::Push(LogLevel::Error, "MyProject", std::format("Boom {}", i), ConsoleOrigin{});
	}
	fixture.SettleTail();
	ASSERT_GT(fixture.ScrollY(), 0.0f);

	fixture.ToggleKind(1);
	fixture.Frame();

	const ConsoleFixture::RowPlacement placement = fixture.PlaceRows();
	ASSERT_EQ(placement.realized, 7u);
	EXPECT_EQ(placement.inView, placement.realized);
}

TEST_F(ConsoleCostTest, ExpansionSettlesInFrameOfClick) {
	for (const char* target : {"Direction 199", "Direction 190"}) {
		SCOPED_TRACE(target);
		EditorConsole::Clear();
		ConsoleFixture fixture;
		for (u32 i = 0; i < 200; ++i) {
			EditorConsole::Push(LogLevel::Info, "MyProject", std::format("Direction {}", i),
								ConsoleOrigin{"Steering.cpp", i + 1, 0});
		}
		fixture.SettleTail();
		ASSERT_TRUE(fixture.Autoscroll());
		ASSERT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());

		UI::ElementHandle message = fixture.FindText(fixture.root, target);
		ASSERT_TRUE(fixture.ctx.IsAlive(message));
		const f32 maxBefore = fixture.MaxScrollY();
		const f32 rowY = fixture.ctx.GetLayout(message).borderBox.y;

		ClickElement(fixture.ctx, message);
		const str line = std::format("Steering.cpp:{}", std::atoi(target + 10) + 1);
		ASSERT_TRUE(fixture.ctx.IsAlive(FindByTextContaining(fixture.ctx, fixture.root, line)));
		ASSERT_GT(fixture.MaxScrollY(), maxBefore);
		EXPECT_TRUE(fixture.ctx.IsAlive(message));
		EXPECT_FALSE(fixture.Autoscroll());
		EXPECT_FALSE(fixture.BoxIsLit());
		EXPECT_FLOAT_EQ(fixture.ctx.GetLayout(message).borderBox.y, rowY);

		Vector<UI::ElementHandle> before{GetMemoryResource(MemTag::Editor)};
		fixture.CollectAlive(fixture.Viewport(), before);
		for (u32 frame = 0; frame < 4; ++frame) {
			fixture.Frame();
		}
		EXPECT_FLOAT_EQ(fixture.ctx.GetLayout(message).borderBox.y, rowY);
		u32 rebuilt = 0;
		for (UI::ElementHandle element : before) {
			rebuilt += fixture.ctx.IsAlive(element) ? 0u : 1u;
		}
		EXPECT_EQ(rebuilt, 0u);

		fixture.PushMany(5);
		fixture.Frame();
		fixture.Frame();
		EXPECT_FLOAT_EQ(fixture.ctx.GetLayout(message).borderBox.y, rowY);
	}
}

TEST_F(ConsoleCostTest, DoubleClickNewestDiagnosticOpens) {
	for (const char* openFirst : {"", "after expression", "steering went wide"}) {
		SCOPED_TRACE(*openFirst ? openFirst : "nothing open");
		EditorConsole::Clear();
		ConsoleFixture fixture;
		fixture.PushMany(200);
		EditorConsole::Push(LogLevel::Error, "Compile", "use of undeclared identifier 'undeclaredThing'",
							ConsoleOrigin{"C:/Project/Source/Coin.cpp", 7, 15});
		EditorConsole::Push(LogLevel::Info, "MyProject", "steering went wide", ConsoleOrigin{"Steering.cpp", 3, 0});
		EditorConsole::Push(LogLevel::Error, "Compile", "expected ';' after expression",
							ConsoleOrigin{"C:/Project/Source/Coin.cpp", 9, 2});
		fixture.SettleTail();
		ASSERT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());
		if (*openFirst) {
			ClickElement(fixture.ctx, fixture.FindText(fixture.root, openFirst));
			fixture.SettleTail();
			const UI::Rect pushed = fixture.ctx.GetLayout(fixture.FindText(fixture.root, "after expression")).borderBox;
			const UI::Rect frame = fixture.ctx.GetLayout(fixture.Viewport()).borderBox;
			if (pushed.y + pushed.h > frame.y + frame.h) {
				fixture.ScrollTo(fixture.ScrollY() + (pushed.y + pushed.h) - (frame.y + frame.h));
			}
		}

		UI::ElementHandle message = fixture.FindText(fixture.root, "after expression");
		ASSERT_TRUE(fixture.ctx.IsAlive(message));
		const UI::Rect view = fixture.ctx.GetLayout(fixture.Viewport()).borderBox;
		const UI::Rect seen = fixture.ctx.GetLayout(message).borderBox;
		ASSERT_LE(seen.y + seen.h, view.y + view.h);

		const UI::Rect box = fixture.ctx.GetLayout(message).borderBox;
		const Vec2I at{static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)};
		auto clickAt = [&]() {
			for (InputEventType type : {InputEventType::MouseMove, InputEventType::MouseDown, InputEventType::MouseUp}) {
				InputEvent event{};
				event.type = type;
				event.button = MouseButton::Left;
				event.mousePosPx = at;
				fixture.ctx.InjectInput(event);
			}
			fixture.ctx.Update();
		};
		const UI::ElementHandle row = fixture.ctx.MakeHandle(fixture.ctx.GetElement(message)->parent);
		u32 opened = 0;
		ASSERT_TRUE(static_cast<bool>(fixture.ctx.Events(row).onDoubleClick));
		fixture.ctx.Events(row).onDoubleClick = [&opened]() { ++opened; };

		clickAt();
		for (u32 frame = 0; frame < 3; ++frame) {
			fixture.Frame();
		}
		ASSERT_TRUE(fixture.ctx.IsAlive(row));
		clickAt();
		EXPECT_EQ(opened, 1u);
	}
}

TEST_F(ConsoleCostTest, TailSwitchLandsOnNewest) {
	for (const f32 scale : {1.0f, 1.25f}) {
		SCOPED_TRACE(std::format("content scale {}", scale));
		EditorConsole::Clear();
		ConsoleFixture fixture(0.0f, scale);
		for (u32 i = 0; i < 300; ++i) {
			EditorConsole::Push(LogLevel::Trace, "MyProject", std::format("Chatter {}", i), ConsoleOrigin{});
			if (i % 5 == 4) {
				EditorConsole::Push(LogLevel::Info, "MyProject", std::format("Direction {}", i / 5), ConsoleOrigin{});
			}
		}
		fixture.SettleTail();
		ASSERT_TRUE(fixture.Autoscroll());

		for (const char* direction : {"hiding Trace", "showing Trace"}) {
			SCOPED_TRACE(direction);
			const f32 extentBefore = fixture.MaxScrollY();
			fixture.ToggleKind(0);
			fixture.Frame();
			ASSERT_NE(fixture.MaxScrollY(), extentBefore);
			ASSERT_GT(fixture.MaxScrollY(), 0.0f);

			EXPECT_TRUE(RowIsOnScreen(fixture.ctx, fixture.Viewport(), fixture.FindText(fixture.root, "Direction 59")));
			EXPECT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());
			const f32 landed = fixture.ScrollY();
			for (u32 frame = 1; frame <= 3; ++frame) {
				fixture.Frame();
				EXPECT_FLOAT_EQ(fixture.ScrollY(), landed) << frame;
			}
			EXPECT_TRUE(fixture.Autoscroll());
		}
	}
}

TEST_F(ConsoleCostTest, FrontedConsoleOpensOnNewest) {
	for (const bool everInFront : {true, false}) {
		SCOPED_TRACE(everInFront ? "tab was in front before" : "backlog behind another tab");
		EditorConsole::Clear();
		EditorTest::DockedPanelRig rig("ConsolePanel", {900, 500});
		auto push = [](u32 count) {
			for (u32 i = 0; i < count; ++i) {
				EditorConsole::Push(LogLevel::Info, "MyProject", std::format("Direction {}", i), ConsoleOrigin{});
			}
		};
		auto viewport = [&rig]() { return FindByClass(rig.context, rig.Root(), StringHash("slt-scroll-viewport"), 0); };
		auto scrollY = [&rig, &viewport]() { return rig.context.GetElement(viewport())->scrollOffset.y; };
		auto newestOnScreen = [&rig, &viewport]() {
			return RowIsOnScreen(rig.context, viewport(), FindByTextContaining(rig.context, rig.Root(), "Direction 299"));
		};
		if (everInFront) {
			push(300);
			for (u32 frame = 0; frame < 3; ++frame) {
				rig.Frame();
			}
			ASSERT_TRUE(newestOnScreen());
			rig.ShowTab(EditorTest::DockedPanelRig::OTHER_TAB);
		} else {
			rig.ShowTab(EditorTest::DockedPanelRig::OTHER_TAB);
			push(300);
			rig.Frame();
			rig.Frame();
		}
		ASSERT_FALSE(rig.context.IsAlive(viewport()));
		rig.ShowTab(EditorTest::DockedPanelRig::PANEL_TAB);

		ASSERT_TRUE(rig.context.IsAlive(viewport()));
		ASSERT_FALSE(rig.context.IsAlive(FindByClass(rig.context, rig.Root(), StringHash("slt-scroll-viewport"), 1)));
		ASSERT_GT(rig.context.GetElement(viewport())->maxScroll.y, 0.0f);
		EXPECT_TRUE(newestOnScreen());
		EXPECT_FLOAT_EQ(scrollY(), rig.context.GetElement(viewport())->maxScroll.y);
		const f32 landed = scrollY();
		rig.Frame();
		rig.Frame();
		EXPECT_FLOAT_EQ(scrollY(), landed);
		EXPECT_TRUE(newestOnScreen());

		PanelManager::ClosePanel(rig.panel);
		PanelManager::TickAll(0.0f);
	}
}

TEST_F(ConsoleCostTest, BacklogOpensOnNewestRow) {
	for (u32 i = 0; i < 300; ++i) {
		EditorConsole::Push(LogLevel::Info, "MyProject", std::format("Direction {}", i), ConsoleOrigin{});
	}
	ConsoleFixture fixture;
	fixture.pushed = 300;

	EXPECT_FALSE(fixture.ShowsMessage("No logs"));
	EXPECT_TRUE(RowIsOnScreen(fixture.ctx, fixture.Viewport(), fixture.FindText(fixture.root, "Direction 299")));
	ASSERT_GT(fixture.MaxScrollY(), 0.0f);
	EXPECT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());

	fixture.PushMany(100);
	fixture.Frame();
	fixture.Frame();
	EXPECT_TRUE(fixture.Autoscroll());
	EXPECT_FLOAT_EQ(fixture.ScrollY(), fixture.MaxScrollY());
	EXPECT_TRUE(RowIsOnScreen(fixture.ctx, fixture.Viewport(), fixture.FindText(fixture.root, "Direction 399")));
	const f32 landed = fixture.ScrollY();
	fixture.Frame();
	fixture.Frame();
	EXPECT_FLOAT_EQ(fixture.ScrollY(), landed);
}

TEST_F(ConsoleCostTest, No2ControlsOnToolbarShareEdge) {
	ConsoleFixture fixture;
	UI::ElementHandle toolbar = NthChildOf(fixture.ctx, fixture.root, 0);
	ASSERT_TRUE(fixture.ctx.IsAlive(toolbar));

	auto controlsOf = [&fixture](UI::ElementHandle parent) {
		Vector<UI::ElementHandle> controls{GetMemoryResource(MemTag::Editor)};
		const UI::Element* row = fixture.ctx.GetElement(parent);
		for (u32 child = row ? row->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = fixture.ctx.MakeHandle(child);
			if (fixture.ctx.GetComputedStyle(handle).flexGrow <= 0.0f &&
				fixture.ctx.GetLayout(handle).borderBox.w > 0.0f) {
				controls.push_back(handle);
			}
			child = fixture.ctx.GetElement(handle)->nextSibling;
		}
		return controls;
	};
	auto expectHeldOff = [&fixture](const Vector<UI::ElementHandle>& controls, const char* where) {
		for (u32 i = 1; i < controls.size(); ++i) {
			const UI::Rect& previous = fixture.ctx.GetLayout(controls[i - 1]).borderBox;
			const UI::Rect& current = fixture.ctx.GetLayout(controls[i]).borderBox;
			EXPECT_GT(current.x, previous.x + previous.w);
		}
	};

	Vector<UI::ElementHandle> controls = controlsOf(toolbar);
	ASSERT_GE(controls.size(), 4u);
	expectHeldOff(controls, "toolbar");

	UI::ElementHandle well;
	for (UI::ElementHandle control : controls) {
		if (fixture.ctx.HasClass(control, EditorTheme::Class::SwitchGroup)) {
			well = control;
		}
	}
	ASSERT_TRUE(fixture.ctx.IsAlive(well));
	Vector<UI::ElementHandle> switches = controlsOf(well);
	ASSERT_EQ(switches.size(), 4u);
	expectHeldOff(switches, "level switch");
}

TEST_F(ConsoleCostTest, LevelColumnAndToolbarNeverShrink) {
	ConsoleFixture wide;
	wide.PushMany(20);
	wide.Frame();
	ASSERT_TRUE(wide.ctx.IsAlive(wide.FirstRowTag()));
	f32 restingTag = wide.ctx.GetLayout(wide.FirstRowTag()).borderBox.w;
	f32 restingBox = wide.ctx.GetLayout(wide.AutoscrollBox()).borderBox.w;
	ASSERT_GT(restingTag, 0.0f);
	ASSERT_GT(restingBox, 0.0f);

	ConsoleFixture narrow(260.0f);
	narrow.PushMany(20);
	narrow.Frame();
	ASSERT_TRUE(narrow.ctx.IsAlive(narrow.FirstRowTag()));

	EXPECT_FLOAT_EQ(narrow.ctx.GetLayout(narrow.FirstRowTag()).borderBox.w, restingTag);
	EXPECT_FLOAT_EQ(narrow.ctx.GetLayout(narrow.AutoscrollBox()).borderBox.w, restingBox);
}

TEST_F(ConsoleCostTest, ToolbarKeepsHeightWhenListIsLong) {
	ConsoleFixture empty;
	empty.Frame();
	f32 restingHeight = empty.ctx.GetLayout(empty.Toolbar()).borderBox.h;
	ASSERT_GT(restingHeight, 0.0f);

	ConsoleFixture full;
	full.PushMany(ROWS_BELOW_CAP);
	full.Frame();
	full.Frame();
	ASSERT_GT(full.MaxScrollY(), 0.0f);

	EXPECT_FLOAT_EQ(full.ctx.GetLayout(full.Toolbar()).borderBox.h, restingHeight);
}

TEST(ConsoleCostResidual, AppendingToScrollViewCostsAppendedRow) {
	BareScrollFixture fixture;
	for (u32 i = 0; i < ROWS_BELOW_CAP; ++i) {
		fixture.AddRow();
	}
	fixture.ctx.Update();

	u32 base = fixture.font.Walks();
	fixture.ctx.ResetLayoutStats();
	for (u32 i = 0; i < 10; ++i) {
		fixture.ctx.Update();
	}
	EXPECT_EQ(fixture.font.Walks(), base);
	EXPECT_EQ(fixture.ctx.GetLayoutStats().measureWork, 0u);

	fixture.ctx.ResetLayoutStats();
	u32 perAppend = fixture.ShapesPerAppendedRow(ROWS_BELOW_CAP, 10);
	u64 measurePerAppend = fixture.ctx.GetLayoutStats().measureWork / 10;

	EXPECT_LE(perAppend, 3u);
	EXPECT_LE(measurePerAppend, 20u);

	BareScrollFixture quarter;
	for (u32 i = 0; i < ROWS_BELOW_CAP / 4; ++i) {
		quarter.AddRow();
	}
	quarter.ctx.Update();
	quarter.ctx.ResetLayoutStats();
	u32 quarterPerAppend = quarter.ShapesPerAppendedRow(ROWS_BELOW_CAP / 4, 10);

	EXPECT_EQ(perAppend, quarterPerAppend);
	EXPECT_EQ(measurePerAppend, quarter.ctx.GetLayoutStats().measureWork / 10);
}
