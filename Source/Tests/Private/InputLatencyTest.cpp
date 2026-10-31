#include "TestsPCH.h"

#include "ProductLookup.h"

#include "PassProbes.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/EditorGizmoOps.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "EditorConfig.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr u32 NEVER = 0xFFFFFFFFu;

	constexpr u32 PRESENT_LIMIT = 8;

	class FrameDriver {
	public:
		using Stage = std::function<void(f32)>;

		Stage panelTick;
		Stage uiUpdate;
		Stage panelSubmit;
		Stage publish;

		void Stamp(const InputEvent& event) { _queued.push_back(event); }

		const std::vector<InputEvent>& Pumped() const { return _pumped; }

		Vec2 MousePos() const { return _mousePos; }
		bool LeftDown() const { return _leftDown; }
		bool LeftPressed() const { return _leftPressed; }

		void Frame(f32 deltaSeconds = 1.0f / 60.0f) {
			_pumped.swap(_queued);
			_queued.clear();
			_leftPressed = false;
			for (const InputEvent& event : _pumped) {
				if (event.type == InputEventType::MouseMove || event.type == InputEventType::MouseDown ||
					event.type == InputEventType::MouseUp) {
					_mousePos = {static_cast<f32>(event.mousePosPx.x), static_cast<f32>(event.mousePosPx.y)};
				}
				if (event.type == InputEventType::MouseDown && event.button == MouseButton::Left) {
					_leftDown = true;
					_leftPressed = true;
				}
				if (event.type == InputEventType::MouseUp && event.button == MouseButton::Left) {
					_leftDown = false;
				}
			}

			if (panelTick) {
				panelTick(deltaSeconds);
			}
			if (uiUpdate) {
				uiUpdate(deltaSeconds);
			}
			if (panelSubmit) {
				panelSubmit(deltaSeconds);
			}
			if (publish) {
				publish(deltaSeconds);
			}
		}
	private:
		std::vector<InputEvent> _queued;
		std::vector<InputEvent> _pumped;
		Vec2 _mousePos{};
		bool _leftDown = false;
		bool _leftPressed = false;
	};

	template <typename Sample>
	u32 PresentsUntilChanged(FrameDriver& driver, Sample sample, u32 limit = PRESENT_LIMIT) {
		auto before = sample();
		for (u32 elapsed = 0; elapsed < limit; ++elapsed) {
			driver.Frame();
			if (!(sample() == before)) {
				return elapsed;
			}
		}
		return NEVER;
	}

	InputEvent MouseMove(f32 x, f32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseMove;
		event.mousePosPx = {static_cast<i32>(x), static_cast<i32>(y)};
		return event;
	}

	InputEvent MouseDown(f32 x, f32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseDown;
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(x), static_cast<i32>(y)};
		return event;
	}

	InputEvent MouseUp(f32 x, f32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseUp;
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(x), static_cast<i32>(y)};
		return event;
	}

	struct DockShell {
		enum class Order {
			BuildBetweenRouteAndSettle,
			BuildBeforeRoute,
		};

		MockFontSystem font;
		UI::Context ctx;
		UI::DockManager dock{ctx, Handle<FontAsset>{}};
		UI::RenderList list;
		UI::ElementHandle chrome;
		UI::ElementHandle host;
		FrameDriver driver;
		Vec2I pendingResize{};

		Order order = Order::BuildBetweenRouteAndSettle;

		explicit DockShell(Order stageOrder = Order::BuildBetweenRouteAndSettle) : ctx(MakeDesc(), &font), order(stageOrder) {
			UI::Style column;
			column.flexDirection = UI::FlexDirection::Column;
			column.width = UI::Length::Percent(1.0f);
			column.height = UI::Length::Percent(1.0f);
			ctx.SetStyle(ctx.GetRoot(), column);

			chrome = ctx.CreateElement();
			ctx.AppendChild(ctx.GetRoot(), chrome);
			UI::Style chromeStyle;
			chromeStyle.width = UI::Length::Percent(1.0f);
			chromeStyle.height = UI::Length::Pixel(40.0f);
			ctx.SetStyle(chrome, chromeStyle);

			host = ctx.CreateElement();
			ctx.AppendChild(ctx.GetRoot(), host);
			UI::Style hostStyle;
			hostStyle.width = UI::Length::Percent(1.0f);
			hostStyle.flexGrow = 1.0f;
			ctx.SetStyle(host, hostStyle);

			driver.uiUpdate = [this](f32) {
				if (pendingResize.x > 0) {
					ctx.Resize(pendingResize);
					pendingResize = {};
				}
				for (const InputEvent& event : driver.Pumped()) {
					ctx.InjectInput(event);
				}
				if (order == Order::BuildBetweenRouteAndSettle) {
					ctx.Update();
				}
				Vec2 mouse = driver.MousePos();
				dock.UpdateInteraction(mouse, Vec2I{static_cast<i32>(mouse.x), static_cast<i32>(mouse.y)},
									   driver.LeftDown(), driver.LeftPressed());
				dock.Build(host);
				ctx.Update();
			};
			driver.publish = [this](f32) { ctx.BuildRenderList(list); };
		}

		void Settle() {
			driver.Frame();
			driver.Frame();
		}

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {1600, 900};
			desc.contentScale = 1.0f;
			return desc;
		}

		static void Fill(UI::Context& c, UI::ElementHandle parent, Color color) {
			UI::ElementHandle body = c.CreateElement();
			c.AppendChild(parent, body);
			UI::Style style;
			style.width = UI::Length::Percent(1.0f);
			style.height = UI::Length::Percent(1.0f);
			style.background = UI::Brush::SolidColor(color);
			c.SetStyle(body, style);
		}

		UI::Rect FindFill(Color color) const {
			for (const UI::UIQuad& quad : list.quads) {
				if (NearlyEqual(quad.color0, color)) {
					return quad.rect;
				}
			}
			return {};
		}

		std::vector<UI::Rect> FindAll(Color color) const {
			std::vector<UI::Rect> found;
			for (const UI::UIQuad& quad : list.quads) {
				if (NearlyEqual(quad.color0, color)) {
					found.push_back(quad.rect);
				}
			}
			return found;
		}

		static bool NearlyEqual(const Color& a, const Color& b) {
			auto close = [](f32 x, f32 y) { return std::fabs(x - y) < 0.01f; };
			return close(a.r, b.r) && close(a.g, b.g) && close(a.b, b.b) && close(a.a, b.a);
		}

		UI::ElementHandle FindByClass(StringHash klass) const {
			return FindByClassFrom(ctx.GetRoot(), klass);
		}

		std::vector<UI::ElementHandle> FindEveryByClass(StringHash klass) const {
			std::vector<UI::ElementHandle> found;
			CollectByClass(ctx.GetRoot(), klass, found);
			return found;
		}

		void CollectByClass(UI::ElementHandle root, StringHash klass, std::vector<UI::ElementHandle>& out) const {
			const UI::Element* pod = ctx.GetElement(root);
			if (!pod) {
				return;
			}
			for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = ctx.MakeHandle(child);
				if (ctx.HasClass(handle, klass)) {
					out.push_back(handle);
				}
				CollectByClass(handle, klass, out);
				child = ctx.GetElement(handle)->nextSibling;
			}
		}

		UI::ElementHandle FindByClassFrom(UI::ElementHandle root, StringHash klass) const {
			const UI::Element* pod = ctx.GetElement(root);
			if (!pod) {
				return {};
			}
			for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = ctx.MakeHandle(child);
				if (ctx.HasClass(handle, klass)) {
					return handle;
				}
				if (UI::ElementHandle found = FindByClassFrom(handle, klass); found.IsValid()) {
					return found;
				}
				child = ctx.GetElement(handle)->nextSibling;
			}
			return {};
		}

		UI::Rect LayoutOf(UI::ElementHandle handle) const { return ctx.GetLayout(handle).borderBox; }
	};

	struct DockFixture : DockShell {
		static constexpr StringHash LeftPanel = "panel.left"_hash;
		static constexpr StringHash RightPanel = "panel.right"_hash;
		static constexpr StringHash ExtraPanel = "panel.extra"_hash;

		static Color LeftFill() { return Color(0.9f, 0.1f, 0.1f, 1.0f); }
		static Color RightFill() { return Color(0.1f, 0.9f, 0.1f, 1.0f); }
		static Color ExtraFill() { return Color(0.1f, 0.1f, 0.9f, 1.0f); }

		explicit DockFixture(Order stageOrder = Order::BuildBetweenRouteAndSettle) : DockShell(stageOrder) {
			dock.RegisterPanel(LeftPanel, "Left", [](UI::Context& c, UI::ElementHandle parent) {
				Fill(c, parent, LeftFill());
			});
			dock.RegisterPanel(RightPanel, "Right", [](UI::Context& c, UI::ElementHandle parent) {
				Fill(c, parent, RightFill());
			});
			dock.RegisterPanel(ExtraPanel, "Extra", [](UI::Context& c, UI::ElementHandle parent) {
				Fill(c, parent, ExtraFill());
			});

			dock.DockPanel(LeftPanel, dock.GetRootNode(), UI::DockZone::Center);
			dock.DockPanel(RightPanel, dock.GetRootNode(), UI::DockZone::Right);
			dock.DockPanel(ExtraPanel, dock.GetRootNode(), UI::DockZone::Center);

			Settle();
		}
	};

	bool SameRect(const UI::Rect& a, const UI::Rect& b) {
		auto close = [](f32 x, f32 y) { return std::fabs(x - y) < 0.5f; };
		return close(a.x, b.x) && close(a.y, b.y) && close(a.w, b.w) && close(a.h, b.h);
	}

	void ReadHover(DockFixture& fixture, u32& presents) {
		UI::ElementHandle tab = fixture.FindByClass("dock-tab"_hash);
		ASSERT_TRUE(tab.IsValid());
		UI::Rect tabBox = fixture.LayoutOf(tab);
		ASSERT_GT(tabBox.w, 0.0f);

		fixture.driver.Stamp(MouseMove(1590.0f, 890.0f));
		fixture.driver.Frame();
		fixture.driver.Frame();

		Color hover = fixture.ctx.GetStyleVarColor("--slt-tab-hover"_hash, Color(0.0f, 0.0f, 0.0f, 0.0f));
		auto hoverQuads = [&fixture, hover]() { return fixture.FindAll(hover).size(); };
		ASSERT_EQ(hoverQuads(), 0u);

		fixture.driver.Stamp(MouseMove(tabBox.x + tabBox.w * 0.5f, tabBox.y + tabBox.h * 0.5f));
		presents = PresentsUntilChanged(fixture.driver, hoverQuads);
	}

	void ReadSplitterDrag(DockFixture& fixture, u32& presents) {
		UI::ElementHandle splitter = fixture.FindByClass("dock-splitter"_hash);
		ASSERT_TRUE(splitter.IsValid());
		UI::Rect splitterBox = fixture.LayoutOf(splitter);
		ASSERT_GT(splitterBox.h, 0.0f);

		f32 grabX = splitterBox.x + splitterBox.w * 0.5f;
		f32 grabY = splitterBox.y + splitterBox.h * 0.5f;

		fixture.driver.Stamp(MouseDown(grabX, grabY));
		fixture.driver.Frame();
		fixture.driver.Frame();

		UI::Rect before = fixture.FindFill(DockFixture::RightFill());
		ASSERT_GT(before.w, 0.0f);

		auto rightWidth = [&fixture]() { return std::lround(fixture.FindFill(DockFixture::RightFill()).w); };
		fixture.driver.Stamp(MouseMove(grabX + 60.0f, grabY));
		presents = PresentsUntilChanged(fixture.driver, rightWidth);
	}

	void ReadTabClick(DockFixture& fixture, u32& presents) {
		std::vector<UI::ElementHandle> tabs = fixture.FindEveryByClass("dock-tab"_hash);
		ASSERT_GE(tabs.size(), 2u);

		ASSERT_GT(fixture.FindFill(DockFixture::ExtraFill()).w, 0.0f);
		ASSERT_EQ(fixture.FindFill(DockFixture::LeftFill()).w, 0.0f);

		UI::Rect tabBox = fixture.LayoutOf(tabs[0]);
		f32 clickX = tabBox.x + tabBox.w * 0.5f;
		f32 clickY = tabBox.y + tabBox.h * 0.5f;

		auto leftIsPainted = [&fixture]() { return fixture.FindFill(DockFixture::LeftFill()).w > 0.0f; };
		fixture.driver.Stamp(MouseMove(clickX, clickY));
		fixture.driver.Frame();
		fixture.driver.Stamp(MouseDown(clickX, clickY));

		presents = PresentsUntilChanged(fixture.driver, leftIsPainted);
	}

	void ReadDropZoneResize(DockFixture& fixture, u32& presents) {
		std::vector<UI::ElementHandle> tabs = fixture.FindEveryByClass("dock-tab"_hash);
		ASSERT_GE(tabs.size(), 3u);

		UI::Rect tabBox = fixture.LayoutOf(tabs.back());
		f32 startX = tabBox.x + tabBox.w * 0.5f;
		f32 startY = tabBox.y + tabBox.h * 0.5f;

		fixture.driver.Stamp(MouseMove(startX, startY));
		fixture.driver.Frame();
		fixture.driver.Stamp(MouseDown(startX, startY));
		fixture.driver.Frame();
		fixture.driver.Stamp(MouseMove(startX + 40.0f, startY + 200.0f));
		fixture.driver.Frame();
		fixture.driver.Frame();

		Color trigger = fixture.ctx.GetStyleVarColor("--slt-zone-trigger"_hash, Color(0.0f, 0.0f, 0.0f, 0.0f));
		auto triggerPlacement = [&fixture, trigger]() {
			i64 placement = 0;
			for (const UI::Rect& rect : fixture.FindAll(trigger)) {
				placement = placement * 31 + std::lround(rect.x);
				placement = placement * 31 + std::lround(rect.y);
			}
			return placement;
		};
		ASSERT_NE(triggerPlacement(), 0);

		fixture.pendingResize = {1200, 900};
		presents = PresentsUntilChanged(fixture.driver, triggerPlacement);
	}

} // anonymous namespace

TEST(InputLatencyTest, HoverResolvesSameFrame) {
	DockFixture fixture;
	u32 presents = NEVER;
	ReadHover(fixture, presents);
	EXPECT_EQ(presents, 0u);
}

TEST(InputLatencyTest, SplitterDragMovesPanelItResizes) {
	DockFixture fixture;
	u32 presents = NEVER;
	ReadSplitterDrag(fixture, presents);
	EXPECT_NE(presents, NEVER);
	EXPECT_EQ(presents, 0u) << presents << " presents";
}

TEST(InputLatencyTest, TabClickSwitchesPanelSameFrame) {
	DockFixture fixture;
	u32 presents = NEVER;
	ReadTabClick(fixture, presents);
	EXPECT_NE(presents, NEVER);
	EXPECT_EQ(presents, 0u) << presents << " presents";
}

TEST(InputLatencyTest, EarlyDockMakesTabClickLate) {
	DockFixture fixture(DockFixture::Order::BuildBeforeRoute);
	u32 presents = NEVER;
	ReadTabClick(fixture, presents);
	EXPECT_EQ(presents, 1u);
}

TEST(InputLatencyTest, DropZoneOverlayFollowsResizeSameFrame) {
	DockFixture fixture;
	u32 presents = NEVER;
	ReadDropZoneResize(fixture, presents);
	EXPECT_NE(presents, NEVER);
	EXPECT_EQ(presents, 0u) << presents << " presents";
}

TEST(InputLatencyTest, EarlyDockMakesOverlayLate) {
	DockFixture fixture(DockFixture::Order::BuildBeforeRoute);
	u32 presents = NEVER;
	ReadDropZoneResize(fixture, presents);
	EXPECT_EQ(presents, 1u);
}

TEST(InputLatencyTest, HoverHighlightSurvivesRebuildTabClickCauses) {
	DockFixture fixture;
	std::vector<UI::ElementHandle> tabs = fixture.FindEveryByClass("dock-tab"_hash);
	ASSERT_GE(tabs.size(), 2u);

	UI::Rect tabBox = fixture.LayoutOf(tabs[0]);
	f32 clickX = tabBox.x + tabBox.w * 0.5f;
	f32 clickY = tabBox.y + tabBox.h * 0.5f;

	Color hover = fixture.ctx.GetStyleVarColor("--slt-tab-hover"_hash, Color(0.0f, 0.0f, 0.0f, 0.0f));
	auto hoverQuads = [&fixture, hover]() { return fixture.FindAll(hover).size(); };

	fixture.driver.Stamp(MouseMove(clickX, clickY));
	fixture.driver.Frame();
	fixture.driver.Frame();
	ASSERT_GT(hoverQuads(), 0u);

	fixture.driver.Stamp(MouseDown(clickX, clickY));
	for (u32 frame = 0; frame < 4; ++frame) {
		fixture.driver.Frame();
		EXPECT_GT(hoverQuads(), 0u);
	}
}

TEST(InputLatencyTest, IdleFrameStillCostsNoLayoutPass) {
	DockFixture fixture;
	fixture.driver.Frame();
	fixture.driver.Frame();

	fixture.ctx.ResetLayoutStats();
	fixture.driver.Frame();
	EXPECT_EQ(fixture.ctx.GetLayoutStats().passes, 0u);
}

namespace {

	u64 PassesForATabClick(DockFixture& fixture, u32 frames) {
		std::vector<UI::ElementHandle> tabs = fixture.FindEveryByClass("dock-tab"_hash);
		EXPECT_GE(tabs.size(), 2u);
		if (tabs.size() < 2) {
			return 0;
		}
		UI::Rect tabBox = fixture.LayoutOf(tabs[0]);
		f32 x = tabBox.x + tabBox.w * 0.5f;
		f32 y = tabBox.y + tabBox.h * 0.5f;

		fixture.driver.Stamp(MouseMove(x, y));
		fixture.driver.Frame();
		fixture.driver.Frame();

		fixture.ctx.ResetLayoutStats();
		fixture.driver.Stamp(MouseDown(x, y));
		for (u32 frame = 0; frame < frames; ++frame) {
			fixture.driver.Frame();
		}
		return fixture.ctx.GetLayoutStats().passes;
	}

} // namespace

TEST(InputLatencyTest, TabClickLayoutPassesBounded) {
	DockFixture built;
	DockFixture before(DockFixture::Order::BuildBeforeRoute);
	u64 builtPasses = PassesForATabClick(built, 3);
	u64 beforePasses = PassesForATabClick(before, 3);

	EXPECT_LE(builtPasses, beforePasses);
}

namespace {

	struct DockRebuildFixture : DockShell {
		static constexpr u32 PANEL_COUNT = 6;
		static constexpr u32 CLICKED_LEAF_PANELS[] = {0, 5};

		enum class Shape {
			Editor,
			ClickedLeafOnly,
		};

		std::array<u32, PANEL_COUNT> bodySize{};
		std::array<u32, PANEL_COUNT> builds{};
		std::array<u32, PANEL_COUNT> bodyElements{};

		static StringHash PanelId(u32 index) {
			static constexpr StringHash IDS[PANEL_COUNT] = {
				"rebuild.panel0"_hash, "rebuild.panel1"_hash, "rebuild.panel2"_hash,
				"rebuild.panel3"_hash, "rebuild.panel4"_hash, "rebuild.panel5"_hash};
			return IDS[index];
		}

		explicit DockRebuildFixture(u32 clickedLeafBody = 1, u32 otherBody = 1, Shape shape = Shape::Editor) {
			for (u32 i = 0; i < PANEL_COUNT; ++i) {
				bool clicked = (i == CLICKED_LEAF_PANELS[0] || i == CLICKED_LEAF_PANELS[1]);
				bodySize[i] = clicked ? clickedLeafBody : otherBody;
			}

			static constexpr const char* TITLES[PANEL_COUNT] = {"P0", "P1", "P2", "P3", "P4", "P5"};
			for (u32 i = 0; i < PANEL_COUNT; ++i) {
				dock.RegisterPanel(PanelId(i), TITLES[i], [this, i](UI::Context& c, UI::ElementHandle parent) {
					++builds[i];
					for (u32 n = 0; n < bodySize[i]; ++n) {
						UI::ElementHandle body = c.CreateElement();
						c.AppendChild(parent, body);
						UI::Style style;
						style.width = UI::Length::Percent(1.0f);
						style.height = UI::Length::Pixel(4.0f);
						c.SetStyle(body, style);
						++bodyElements[i];
					}
				});
			}

			dock.DockPanel(PanelId(0), dock.GetRootNode(), UI::DockZone::Center);
			if (shape == Shape::Editor) {
				dock.DockPanel(PanelId(1), dock.FindNodeOfPanel(PanelId(0)), UI::DockZone::Right);
				dock.DockPanel(PanelId(2), dock.FindNodeOfPanel(PanelId(1)), UI::DockZone::Bottom);
				dock.DockPanel(PanelId(3), dock.FindNodeOfPanel(PanelId(0)), UI::DockZone::Left);
				dock.DockPanel(PanelId(4), dock.FindNodeOfPanel(PanelId(0)), UI::DockZone::Bottom);
			}
			dock.DockPanel(PanelId(5), dock.FindNodeOfPanel(PanelId(0)), UI::DockZone::Center);

			Settle();
		}

		void ResetCounters() {
			builds = {};
			bodyElements = {};
			ctx.ResetTreeStats();
			ctx.ResetLayoutStats();
		}

		u32 TotalBuilds() const {
			u32 total = 0;
			for (u32 count : builds) {
				total += count;
			}
			return total;
		}

		u32 UntouchedBuilds() const {
			u32 total = 0;
			for (u32 i = 0; i < PANEL_COUNT; ++i) {
				if (i != CLICKED_LEAF_PANELS[0] && i != CLICKED_LEAF_PANELS[1]) {
					total += builds[i];
				}
			}
			return total;
		}

		u32 TotalBodyElements() const {
			u32 total = 0;
			for (u32 count : bodyElements) {
				total += count;
			}
			return total;
		}
	};

	struct RebuildCost {
		u32 totalBuilds = 0;
		u32 untouchedBuilds = 0;
		u32 bodyElements = 0;
		u64 linkOps = 0;
		u64 measureCalls = 0;
	};

	RebuildCost CostOfATabClick(DockRebuildFixture& fixture, u32 tabIndex) {
		std::vector<UI::ElementHandle> tabs = fixture.FindEveryByClass("dock-tab"_hash);
		EXPECT_GE(tabs.size(), 2u);
		if (tabIndex >= tabs.size()) {
			return {};
		}
		UI::Rect box = fixture.LayoutOf(tabs[tabIndex]);
		f32 x = box.x + box.w * 0.5f;
		f32 y = box.y + box.h * 0.5f;

		fixture.driver.Stamp(MouseMove(x, y));
		fixture.driver.Frame();
		fixture.driver.Frame();

		fixture.ResetCounters();
		fixture.driver.Stamp(MouseDown(x, y));
		fixture.driver.Frame();

		RebuildCost cost;
		cost.totalBuilds = fixture.TotalBuilds();
		cost.untouchedBuilds = fixture.UntouchedBuilds();
		cost.bodyElements = fixture.TotalBodyElements();
		cost.linkOps = fixture.ctx.GetTreeStats().linkOps;
		cost.measureCalls = fixture.ctx.GetLayoutStats().measureCalls;
		return cost;
	}

	u32 InactiveTabOfClickedLeaf(DockRebuildFixture& fixture) {
		std::vector<UI::ElementHandle> tabs = fixture.FindEveryByClass("dock-tab"_hash);
		std::vector<UI::ElementHandle> active = fixture.FindEveryByClass("dock-active-tab"_hash);
		for (u32 i = 0; i < tabs.size(); ++i) {
			bool isActive = std::find(active.begin(), active.end(), tabs[i]) != active.end();
			if (!isActive && fixture.ctx.HasClass(tabs[i], "dock-tab"_hash)) {
				return i;
			}
		}
		return 0;
	}

} // namespace

TEST(InputLatencyTest, ActiveTabClickRebuildsNothing) {
	DockRebuildFixture fixture;
	std::vector<UI::ElementHandle> active = fixture.FindEveryByClass("dock-active-tab"_hash);
	ASSERT_FALSE(active.empty());

	std::vector<UI::ElementHandle> tabs = fixture.FindEveryByClass("dock-tab"_hash);
	auto found = std::find(tabs.begin(), tabs.end(), active[0]);
	ASSERT_NE(found, tabs.end());
	RebuildCost cost = CostOfATabClick(fixture, static_cast<u32>(found - tabs.begin()));

	EXPECT_EQ(cost.totalBuilds, 0u);
	EXPECT_EQ(cost.linkOps, 0u) << cost.linkOps;
}

TEST(InputLatencyTest, TabClickRebuildsOnlyLeafItChanged) {
	DockRebuildFixture fixture;
	RebuildCost cost = CostOfATabClick(fixture, InactiveTabOfClickedLeaf(fixture));

	DockRebuildFixture alone(1, 1, DockRebuildFixture::Shape::ClickedLeafOnly);
	RebuildCost share = CostOfATabClick(alone, InactiveTabOfClickedLeaf(alone));

	EXPECT_EQ(cost.totalBuilds, 1u);
	EXPECT_EQ(cost.untouchedBuilds, 0u);
	EXPECT_EQ(share.totalBuilds, 1u);

	EXPECT_EQ(cost.linkOps, share.linkOps);
	EXPECT_EQ(cost.linkOps, 9u) << cost.linkOps << " elements";
	EXPECT_GT(cost.linkOps, static_cast<u64>(cost.bodyElements));
}

TEST(InputLatencyTest, TabClickCostIgnoresOtherPanels) {
	static constexpr u32 SMALL = 1;
	static constexpr u32 LARGE = 16;

	DockRebuildFixture small(1, SMALL);
	DockRebuildFixture large(1, LARGE);
	RebuildCost smallCost = CostOfATabClick(small, InactiveTabOfClickedLeaf(small));
	RebuildCost largeCost = CostOfATabClick(large, InactiveTabOfClickedLeaf(large));

	ASSERT_EQ(smallCost.untouchedBuilds, largeCost.untouchedBuilds);

	EXPECT_EQ(largeCost.linkOps, smallCost.linkOps);
}

namespace {

	struct PublishedQuad {
		UI::Rect rect;
		Color color;
		u32 flags = 0;
		bool operator==(const PublishedQuad&) const = default;
	};

	std::vector<PublishedQuad> Published(const DockShell& shell) {
		std::vector<PublishedQuad> out;
		out.reserve(shell.list.quads.size());
		for (const UI::UIQuad& quad : shell.list.quads) {
			out.push_back({quad.rect, quad.color0, quad.flags});
		}
		return out;
	}

	void ExpectRebuildIsSound(DockShell& shell, const char* gesture) {
		std::vector<PublishedQuad> incremental = Published(shell);
		ASSERT_FALSE(incremental.empty()) << gesture;

		Vector<UI::DockNodeDesc> layout{GetMemoryResource(MemTag::UI)};
		shell.dock.SaveLayout(layout);
		ASSERT_TRUE(shell.dock.RestoreLayout({layout.data(), layout.size()})) << gesture;
		shell.driver.Frame();
		shell.driver.Frame();

		EXPECT_EQ(incremental, Published(shell));
	}

	void ClickTheInactiveTab(DockShell& shell) {
		std::vector<UI::ElementHandle> tabs = shell.FindEveryByClass("dock-tab"_hash);
		std::vector<UI::ElementHandle> active = shell.FindEveryByClass("dock-active-tab"_hash);
		for (UI::ElementHandle tab : tabs) {
			if (std::find(active.begin(), active.end(), tab) != active.end()) {
				continue;
			}
			UI::Rect box = shell.LayoutOf(tab);
			shell.driver.Stamp(MouseDown(box.x + box.w * 0.5f, box.y + box.h * 0.5f));
			shell.driver.Frame();
			shell.driver.Frame();
			return;
		}
		ADD_FAILURE() << "no inactive tab to click";
	}

} // namespace

TEST(InputLatencyTest, TabClickPublishesWhatFullRebuildPublishes) {
	DockFixture fixture;
	ClickTheInactiveTab(fixture);
	ExpectRebuildIsSound(fixture, "a routed tab click");
}

TEST(InputLatencyTest, ActivatingPanelPublishesWhatFullRebuildPublishes) {
	DockFixture fixture;
	ASSERT_TRUE(fixture.dock.ActivatePanel(DockFixture::ExtraPanel));
	fixture.driver.Frame();
	fixture.driver.Frame();
	ExpectRebuildIsSound(fixture, "ActivatePanel");
}

TEST(InputLatencyTest, UndockingPanelPublishesWhatFullRebuildPublishes) {
	DockFixture fixture;
	fixture.dock.UndockPanel(DockFixture::ExtraPanel);
	fixture.driver.Frame();
	fixture.driver.Frame();
	ExpectRebuildIsSound(fixture, "UndockPanel");
}

TEST(InputLatencyTest, DockingMatchesFullRebuild) {
	DockFixture fixture;
	fixture.dock.DockPanel(DockFixture::ExtraPanel, fixture.dock.FindNodeOfPanel(DockFixture::RightPanel),
						   UI::DockZone::Center);
	fixture.driver.Frame();
	fixture.driver.Frame();
	ExpectRebuildIsSound(fixture, "DockPanel(Center) across leaves");
}

TEST(InputLatencyTest, SplittingLeafPublishesWhatFullRebuildPublishes) {
	DockFixture fixture;
	fixture.dock.DockPanel(DockFixture::ExtraPanel, fixture.dock.FindNodeOfPanel(DockFixture::RightPanel),
						   UI::DockZone::Bottom);
	fixture.driver.Frame();
	fixture.driver.Frame();
	ExpectRebuildIsSound(fixture, "DockPanel(Bottom) splitting a leaf");
}

TEST(InputLatencyTest, LeafCollapseMatchesFullRebuild) {
	DockFixture fixture;
	fixture.dock.UndockPanel(DockFixture::RightPanel);
	fixture.driver.Frame();
	fixture.driver.Frame();
	ExpectRebuildIsSound(fixture, "undocking a leaf's only panel");
}

TEST(InputLatencyTest, TabClickKeepsLeafElements) {
	DockFixture fixture;

	std::vector<UI::ElementHandle> before = fixture.FindEveryByClass("dock-tab"_hash);
	ASSERT_GE(before.size(), 3u);

	UI::Rect rightBefore = fixture.FindFill(DockFixture::RightFill());
	ASSERT_GT(rightBefore.w, 0.0f);

	std::vector<UI::ElementHandle> untouched;
	for (UI::ElementHandle tab : before) {
		UI::Rect box = fixture.LayoutOf(tab);
		if (box.x >= rightBefore.x) {
			untouched.push_back(tab);
		}
	}
	ASSERT_FALSE(untouched.empty());

	ClickTheInactiveTab(fixture);

	for (UI::ElementHandle tab : untouched) {
		EXPECT_TRUE(fixture.ctx.IsAlive(tab));
	}
	std::vector<UI::ElementHandle> after = fixture.FindEveryByClass("dock-tab"_hash);
	u32 survivors = 0;
	for (UI::ElementHandle tab : after) {
		survivors += std::find(before.begin(), before.end(), tab) != before.end() ? 1u : 0u;
	}
	EXPECT_LT(survivors, after.size());
}

namespace {

	std::filesystem::path RepoDir(const char* relative) {
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

	class SceneViewLatencyFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mountedEngine = false;
		bool _mountedProject = false;
		MockFontSystem _font;
		std::unique_ptr<UI::Context> _ctx;
		UI::ElementHandle _panelRoot;
		Handle<EditorPanel> _panel;
		Handle<Scene> _scene;
		Handle<SceneObject> _object;
		FrameDriver _driver;
		Vector<u8> _pixels{GetMemoryResource(MemTag::Graphics)};
		Asset* _meshAsset = nullptr;
		Asset* _materialAsset = nullptr;

		static constexpr ViewTargetId PANEL_TARGET = 1;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_latency_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			_mountedProject = true;
			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}

			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			EditorSelection::Initialize();
			EditorSceneOps::Initialize();
			EditorGizmos::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();

			UI::ContextDesc desc;
			desc.sizePx = {640, 480};
			desc.contentScale = 1.0f;
			_ctx = std::make_unique<UI::Context>(desc, &_font);

			UI::Style rootStyle;
			rootStyle.width = UI::Length::Percent(1.0f);
			rootStyle.height = UI::Length::Percent(1.0f);
			_ctx->SetStyle(_ctx->GetRoot(), rootStyle);
			_panelRoot = _ctx->CreateElement();
			_ctx->AppendChild(_ctx->GetRoot(), _panelRoot);
			_ctx->SetStyle(_panelRoot, rootStyle);

			_scene = SceneSystem::CreateScene("Latency");

			_driver.panelTick = [this](f32 dt) {
				SceneSystem::BeginFrame();
				PanelManager::TickAll(dt);
			};
			_driver.uiUpdate = [this](f32) {
				for (const InputEvent& event : _driver.Pumped()) {
					_ctx->InjectInput(event);
				}
				_ctx->Update();
			};
			_driver.panelSubmit = [](f32 dt) { PanelManager::SubmitViews(dt); };
			_driver.publish = [this](f32) {
				SceneSystem::EndFrame();
				RenderSystem::RenderAll();
			};
		}

		void TearDown() override {
			PanelManager::Terminate();
			if (_materialAsset) {
				AssetDatabase::UnloadAsset(_materialAsset);
			}
			if (_meshAsset) {
				AssetDatabase::UnloadAsset(_meshAsset);
			}
			UndoSystem::Terminate();
			EditorGizmos::Terminate();
			EditorSceneOps::Terminate();
			EditorSelection::Terminate();
			_ctx.reset();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			if (_mountedProject) {
				FileSystem::Unmount("project");
			}
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool OpenSceneView() { return OpenPanel("SceneViewPanel"_hash); }

		bool OpenPanel(StringHash typeName) {
			const TypeInfo* type = TypeRegistry::FindType(typeName);
			if (!type) {
				return false;
			}
			_panel = PanelManager::OpenPanel(type);
			if (!_panel.IsValid()) {
				return false;
			}
			_panel.Get()->BuildContent(*_ctx, _panelRoot);
			for (int frame = 0; frame < 4; ++frame) {
				_driver.Frame();
			}
			return RenderSystem::GetViewTargetSize(PANEL_TARGET).x > 0;
		}

		UI::Rect ViewportRect() const {
			UI::ElementHandle element = FindClass(_panelRoot, EditorTheme::Class::ViewportPlaceholder);
			return element.IsValid() ? _ctx->GetLayout(element).content : UI::Rect{};
		}

		UI::ElementHandle FindClass(UI::ElementHandle root, StringHash klass) const {
			const UI::Element* pod = _ctx->GetElement(root);
			if (!pod) {
				return {};
			}
			for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = _ctx->MakeHandle(child);
				if (_ctx->HasClass(handle, klass)) {
					return handle;
				}
				if (UI::ElementHandle found = FindClass(handle, klass); found.IsValid()) {
					return found;
				}
				child = _ctx->GetElement(handle)->nextSibling;
			}
			return {};
		}

		template <typename T>
		T PanelField(const char* name) const {
			const FieldInfo* field = FindField(_panel.Get()->GetType(), StringHash(name));
			Variant value;
			if (field) {
				field->getFn(_panel.Get(), &value);
			}
			return value.Is<T>() ? value.As<T>() : T{};
		}

		template <typename T>
		bool SetPanelField(const char* name, const T& value) {
			const FieldInfo* field = FindField(_panel.Get()->GetType(), StringHash(name));
			if (!field || !field->setFn) {
				return false;
			}
			Variant in(value);
			field->setFn(_panel.Get(), &in);
			return true;
		}

		u64 ViewChecksum() {
			Vec2I size{};
			if (!RenderSystem::ReadbackViewTarget(PANEL_TARGET, _pixels, size)) {
				return 0;
			}
			u64 hash = 1469598103934665603ull;
			for (u8 byte : _pixels) {
				hash = (hash ^ byte) * 1099511628211ull;
			}
			return hash;
		}

		bool GiveTheObjectAMesh() {
			std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / "cube.glb", _root / "cube.glb",
									   std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				return false;
			}
			Asset* model = AssetDatabase::LoadAsset(Path("project://cube.glb"));
			if (!model) {
				return false;
			}
			AssetDatabase::UnloadAsset(model);
			_meshAsset = ProductLookup::Load(Path("project://cube.glb"), "Meshes/Cube");
			if (!_meshAsset) {
				return false;
			}
			MeshAsset* mesh = static_cast<MeshAsset*>(_meshAsset);
			if (mesh->defaultMaterials.empty()) {
				return false;
			}
			_materialAsset = AssetDatabase::LoadAssetByGuid(mesh->defaultMaterials[0]);
			if (!_materialAsset) {
				return false;
			}
			Handle<MeshRenderer> renderer = _object.Get()->AddComponent<MeshRenderer>();
			renderer.Get()->SetMesh(Handle<MeshAsset>(mesh));
			renderer.Get()->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(_materialAsset)));

			Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", _scene);
			sun.Get()->SetLocalRotation(Quat::FromEuler(Vec3(35.0f, -25.0f, 0.0f)));
			sun.Get()->AddComponent<Light>();
			return true;
		}

		Vec2 ViewportSize() const {
			UI::Rect viewport = ViewportRect();
			return {viewport.w, viewport.h};
		}

		EditorViewportOps::Viewpoint PanelViewpoint() const {
			UI::Rect viewport = ViewportRect();
			EditorViewportOps::Viewpoint viewpoint;
			viewpoint.position = PanelField<Vec3>("cameraPosition");
			viewpoint.rotation = EditorViewportOps::BuildCameraRotation(PanelField<f32>("cameraYawDeg"),
																		PanelField<f32>("cameraPitchDeg"));
			viewpoint.aspect = viewport.h > 0.0f ? viewport.w / viewport.h : 1.0f;
			return viewpoint;
		}

		Mat4 PanelViewProjection() const {
			const EditorViewportOps::Viewpoint viewpoint = PanelViewpoint();
			return EditorViewportOps::BuildProjection(viewpoint) * EditorViewportOps::BuildViewMatrix(viewpoint);
		}

		Ray PanelRayAt(const Vec2& localPx) const {
			const EditorViewportOps::Viewpoint viewpoint = PanelViewpoint();
			return EditorViewportOps::BuildViewportRay(EditorViewportOps::BuildViewMatrix(viewpoint),
													   EditorViewportOps::BuildProjection(viewpoint),
													   localPx, ViewportSize());
		}

		bool ViewportPointFor(const Vec3& world, Vec2& outContextPx, Vec2& outLocalPx) const {
			UI::Rect viewport = ViewportRect();
			if (viewport.w <= 0.0f || viewport.h <= 0.0f) {
				return false;
			}
			if (!EditorViewportOps::ProjectToViewport(PanelViewProjection(), world, ViewportSize(), outLocalPx)) {
				return false;
			}
			outContextPx = {viewport.x + outLocalPx.x, viewport.y + outLocalPx.y};
			return true;
		}

		bool GizmoShaftPoint(Vec2& out) const {
			UI::Rect viewport = ViewportRect();
			if (viewport.w <= 0.0f || viewport.h <= 0.0f) {
				return false;
			}
			Vector<Handle<SceneObject>> targets{GetMemoryResource(MemTag::Editor)};
			EditorViewportOps::GatherDragTargets(targets);
			if (targets.empty()) {
				return false;
			}
			Vec3 origin = EditorGizmoOps::PivotFor(targets);
			f32 scale = EditorGizmoOps::HandleScaleAt(PanelViewpoint(), origin);
			Vec2 local{};
			return ViewportPointFor(origin + Vec3(scale * 0.6f, 0.0f, 0.0f), out, local);
		}

		bool GrabTheGizmo(Vec2& grab) {
			EditorSelection::Select(Handle<Object>(_object.Get()));
			return GrabTheGizmoAsSelected(grab);
		}

		bool GrabTheGizmoAsSelected(Vec2& grab) {
			_driver.Frame();
			_driver.Frame();
			if (!GizmoShaftPoint(grab)) {
				return false;
			}
			_driver.Stamp(MouseMove(grab.x, grab.y));
			_driver.Frame();
			_driver.Stamp(MouseDown(grab.x, grab.y));
			_driver.Frame();
			_driver.Frame();
			return true;
		}
	};

} // anonymous namespace

TEST_F(SceneViewLatencyFixture, SubmittedViewAnswersDragThatMovedIt) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	_object = SceneSystem::CreateObject("Target", _scene);
	ASSERT_TRUE(_object.IsValid());
	Vec2 grab{};
	ASSERT_TRUE(GrabTheGizmo(grab));

	Vec3 startPosition = _object.Get()->GetWorldPosition();
	auto checksum = [this]() { return ViewChecksum(); };
	ASSERT_NE(checksum(), 0ull);

	_driver.Stamp(MouseMove(grab.x + 40.0f, grab.y));
	u32 presents = PresentsUntilChanged(_driver, checksum);
	EXPECT_NE(_object.Get()->GetWorldPosition().x, startPosition.x);
	EXPECT_NE(presents, NEVER);
	EXPECT_EQ(presents, 0u);
}

TEST_F(SceneViewLatencyFixture, EarlySubmitLagsPresent) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	_driver.panelTick = [](f32 dt) {
		SceneSystem::BeginFrame();
		PanelManager::TickAll(dt);
		PanelManager::SubmitViews(dt);
	};
	_driver.panelSubmit = nullptr;

	ASSERT_TRUE(OpenSceneView());
	_object = SceneSystem::CreateObject("Target", _scene);
	ASSERT_TRUE(_object.IsValid());
	Vec2 grab{};
	ASSERT_TRUE(GrabTheGizmo(grab));

	Vec3 startPosition = _object.Get()->GetWorldPosition();
	auto checksum = [this]() { return ViewChecksum(); };
	ASSERT_NE(checksum(), 0ull);

	_driver.Stamp(MouseMove(grab.x + 40.0f, grab.y));
	u32 presents = PresentsUntilChanged(_driver, checksum);
	EXPECT_NE(_object.Get()->GetWorldPosition().x, startPosition.x);
	EXPECT_EQ(presents, 1u);
}

TEST_F(SceneViewLatencyFixture, DragMovesEveryObjectInSelection) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	_object = SceneSystem::CreateObject("Target", _scene);
	Handle<SceneObject> second = SceneSystem::CreateObject("Second", _scene);
	Handle<SceneObject> third = SceneSystem::CreateObject("Third", _scene);
	ASSERT_TRUE(_object.IsValid() && second.IsValid() && third.IsValid());
	second.Get()->SetLocalPosition(Vec3(2.0f, 0.0f, 0.0f));
	third.Get()->SetLocalPosition(Vec3(-1.0f, 1.5f, 0.0f));

	EditorSelection::Select(Handle<Object>(_object.Get()));
	EditorSelection::AddToSelection(Handle<Object>(second.Get()));
	EditorSelection::AddToSelection(Handle<Object>(third.Get()));

	const Vec3 startFirst = _object.Get()->GetWorldPosition();
	const Vec3 startSecond = second.Get()->GetWorldPosition();
	const Vec3 startThird = third.Get()->GetWorldPosition();

	Vec2 grab{};
	ASSERT_TRUE(GrabTheGizmoAsSelected(grab));
	_driver.Stamp(MouseMove(grab.x + 40.0f, grab.y));
	_driver.Frame();
	_driver.Frame();

	const Vec3 movedFirst = _object.Get()->GetWorldPosition() - startFirst;
	const Vec3 movedSecond = second.Get()->GetWorldPosition() - startSecond;
	const Vec3 movedThird = third.Get()->GetWorldPosition() - startThird;

	EXPECT_GT(std::abs(movedFirst.x), 0.01f);
	EXPECT_NEAR(movedSecond.x, movedFirst.x, 1.0e-3f);
	EXPECT_NEAR(movedThird.x, movedFirst.x, 1.0e-3f);
	EXPECT_NEAR(movedSecond.y, movedFirst.y, 1.0e-3f);
	EXPECT_NEAR(movedThird.y, movedFirst.y, 1.0e-3f);

	_driver.Stamp(MouseUp(grab.x + 40.0f, grab.y));
	_driver.Frame();
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 1u);
	EXPECT_TRUE(UndoSystem::Undo());
	EXPECT_NEAR(_object.Get()->GetWorldPosition().x, startFirst.x, 1.0e-3f);
	EXPECT_NEAR(second.Get()->GetWorldPosition().x, startSecond.x, 1.0e-3f);
	EXPECT_NEAR(third.Get()->GetWorldPosition().x, startThird.x, 1.0e-3f);
}

TEST_F(SceneViewLatencyFixture, ClickingALightsIconSelectsIt) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	_object = SceneSystem::CreateObject("Lamp", _scene);
	ASSERT_TRUE(_object.IsValid());
	_object.Get()->AddComponent<Light>();
	EditorSelection::Clear();
	_driver.Frame();
	_driver.Frame();

	Vec2 contextPx{};
	Vec2 localPx{};
	ASSERT_TRUE(ViewportPointFor(_object.Get()->GetWorldPosition(), contextPx, localPx));

	_driver.Stamp(MouseMove(contextPx.x, contextPx.y));
	_driver.Frame();
	_driver.Stamp(MouseDown(contextPx.x, contextPx.y));
	_driver.Frame();
	_driver.Stamp(MouseUp(contextPx.x, contextPx.y));
	_driver.Frame();

	EXPECT_TRUE(EditorSelection::IsSelected(Handle<Object>(_object.Get())));
}

TEST_F(SceneViewLatencyFixture, ClickingMeshSelectsItWhenViewAnswers) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	Asset* cubeAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(cubeAsset, nullptr);
	_object = SceneSystem::CreateObject("Box", _scene);
	ASSERT_TRUE(_object.IsValid());
	_object.Get()->AddComponent<MeshRenderer>()->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cubeAsset)));
	EditorSelection::Clear();
	_driver.Frame();
	_driver.Frame();

	Vec2 contextPx{};
	Vec2 localPx{};
	ASSERT_TRUE(ViewportPointFor(_object.Get()->GetWorldPosition(), contextPx, localPx));
	auto click = [&](Vec2 at) {
		_driver.Stamp(MouseMove(at.x, at.y));
		_driver.Frame();
		_driver.Stamp(MouseDown(at.x, at.y));
		_driver.Frame();
		_driver.Stamp(MouseUp(at.x, at.y));
		_driver.Frame();
	};
	auto selected = [&]() { return EditorSelection::IsSelected(Handle<Object>(_object.Get())); };

	click(contextPx);
	EXPECT_FALSE(selected());
	for (u32 frame = 0; frame < RHI::FRAMES_IN_FLIGHT + 1 && !selected(); ++frame) {
		_driver.Frame();
	}
	EXPECT_TRUE(selected());

	Vec2 emptyPx{};
	ASSERT_TRUE(ViewportPointFor(Vec3(2.5f, 1.5f, 0.0f), emptyPx, localPx));
	click(emptyPx);
	for (u32 frame = 0; frame < RHI::FRAMES_IN_FLIGHT + 1 && selected(); ++frame) {
		_driver.Frame();
	}
	EXPECT_FALSE(selected());
}

TEST_F(SceneViewLatencyFixture, IconlessClickSelectsNothing) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	_object = SceneSystem::CreateObject("Empty", _scene);
	ASSERT_TRUE(_object.IsValid());
	EditorSelection::Clear();
	_driver.Frame();
	_driver.Frame();

	Vec2 contextPx{};
	Vec2 localPx{};
	ASSERT_TRUE(ViewportPointFor(_object.Get()->GetWorldPosition(), contextPx, localPx));

	_driver.Stamp(MouseMove(contextPx.x, contextPx.y));
	_driver.Frame();
	_driver.Stamp(MouseDown(contextPx.x, contextPx.y));
	_driver.Frame();
	_driver.Stamp(MouseUp(contextPx.x, contextPx.y));
	_driver.Frame();

	EXPECT_FALSE(EditorSelection::IsSelected(Handle<Object>(_object.Get())));
}

TEST_F(SceneViewLatencyFixture, GizmosOffRemovesIconTargets) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	_object = SceneSystem::CreateObject("Lamp", _scene);
	ASSERT_TRUE(_object.IsValid());
	_object.Get()->AddComponent<Light>();
	_driver.Frame();
	_driver.Frame();

	Vec2 contextPx{};
	Vec2 localPx{};
	ASSERT_TRUE(ViewportPointFor(_object.Get()->GetWorldPosition(), contextPx, localPx));

	auto clickTheIcon = [&]() {
		EditorSelection::Clear();
		_driver.Stamp(MouseMove(contextPx.x, contextPx.y));
		_driver.Frame();
		_driver.Stamp(MouseDown(contextPx.x, contextPx.y));
		_driver.Frame();
		_driver.Stamp(MouseUp(contextPx.x, contextPx.y));
		_driver.Frame();
		return EditorSelection::IsSelected(Handle<Object>(_object.Get()));
	};

	EXPECT_TRUE(clickTheIcon());

	ASSERT_TRUE(SetPanelField("showGizmos", false));
	_driver.Frame();
	_driver.Frame();
	EXPECT_FALSE(clickTheIcon());

	ASSERT_TRUE(SetPanelField("showGizmos", true));
	_driver.Frame();
	_driver.Frame();
	EXPECT_TRUE(clickTheIcon());
}

TEST_F(SceneViewLatencyFixture, OutlineSwitchReachesAllViews) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	SettingsPaths paths;
	paths.project = Path((_root / "ProjectSettings.ini").generic_string());
	paths.user = Path((_root / "UserSettings.ini").generic_string());
	paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
	Settings::Initialize(paths);
	auto* preferences = Settings::DeclareCategory<EditorConfig::GeneralPreferences>("Editor/General", SettingScope::User);
	ASSERT_NE(preferences, nullptr);
	ASSERT_TRUE(preferences->showSelectionOutline);

	UI::Style row;
	row.flexDirection = UI::FlexDirection::Row;
	row.width = UI::Length::Percent(1.0f);
	row.height = UI::Length::Percent(1.0f);
	_ctx->SetStyle(_panelRoot, row);
	Handle<EditorPanel> views[2];
	for (Handle<EditorPanel>& view : views) {
		UI::ElementHandle half = _ctx->CreateElement();
		_ctx->AppendChild(_panelRoot, half);
		UI::Style halfStyle;
		halfStyle.width = UI::Length::Percent(0.5f);
		halfStyle.height = UI::Length::Percent(1.0f);
		_ctx->SetStyle(half, halfStyle);
		view = PanelManager::OpenPanel(TypeRegistry::FindType("SceneViewPanel"_hash));
		ASSERT_TRUE(view.IsValid());
		view.Get()->BuildContent(*_ctx, half);
	}
	Asset* cubeAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(cubeAsset, nullptr);
	_object = SceneSystem::CreateObject("Box", _scene);
	_object.Get()->AddComponent<MeshRenderer>()->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cubeAsset)));
	EditorSelection::Clear();
	for (int frame = 0; frame < 4; ++frame) {
		_driver.Frame();
	}
	constexpr ViewTargetId TARGETS[2] = {1, 2};
	ASSERT_GT(RenderSystem::GetViewTargetSize(TARGETS[0]).x, 0);
	ASSERT_GT(RenderSystem::GetViewTargetSize(TARGETS[1]).x, 0);

	const Color32 outline = EditorTheme::GetColor32(EditorTheme::ColorToken::SelectionOutline);
	auto outlined = [&](ViewTargetId target) {
		Vec2I size{};
		EXPECT_TRUE(RenderSystem::ReadbackViewTarget(target, _pixels, size));
		u32 count = 0;
		for (size_t at = 0; at + 3 < _pixels.size(); at += 4) {
			const bool near = std::abs(_pixels[at] - outline.r) < 24 && std::abs(_pixels[at + 1] - outline.g) < 24 &&
							  std::abs(_pixels[at + 2] - outline.b) < 24;
			count += near ? 1u : 0u;
		}
		return count;
	};
	auto settle = [&]() {
		for (u32 frame = 0; frame < RHI::FRAMES_IN_FLIGHT + 1; ++frame) {
			_driver.Frame();
		}
	};
	settle();
	ASSERT_EQ(outlined(TARGETS[0]), 0u);
	ASSERT_EQ(outlined(TARGETS[1]), 0u);

	EditorSelection::Select(Handle<Object>(_object.Get()));
	settle();
	EXPECT_GT(outlined(TARGETS[0]), 0u);
	EXPECT_GT(outlined(TARGETS[1]), 0u);

	auto pickSelectionOutline = [&]() {
		ClickElement(*_ctx, FindByClass(*_ctx, _panelRoot, EditorTheme::Class::ToolButton, 7));
		_ctx->Update();
		UI::ElementHandle item = FindByText(*_ctx, _ctx->GetRoot(), "Selection Outline");
		ASSERT_TRUE(item.IsValid());
		ClickElement(*_ctx, item);
	};
	pickSelectionOutline();
	EXPECT_FALSE(preferences->showSelectionOutline);
	settle();
	EXPECT_EQ(outlined(TARGETS[0]), 0u);
	EXPECT_EQ(outlined(TARGETS[1]), 0u);

	pickSelectionOutline();
	settle();
	EXPECT_GT(outlined(TARGETS[0]), 0u);
	EXPECT_GT(outlined(TARGETS[1]), 0u);

	Variant off(false);
	ASSERT_TRUE(SetField(views[0].Get(), views[0].Get()->GetType(), "showGizmos"_hash, &off));
	settle();
	EXPECT_GT(outlined(TARGETS[0]), 0u);

	for (Handle<EditorPanel>& view : views) {
		PanelManager::ClosePanel(view);
	}
	PanelManager::TickAll(0.0f);
	Settings::Terminate();
}

TEST_F(SceneViewLatencyFixture, MeshDragIsNotLate) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	_object = SceneSystem::CreateObject("Target", _scene);
	ASSERT_TRUE(_object.IsValid());
	if (!GiveTheObjectAMesh()) {
		GTEST_SKIP() << "cube.glb import failed (dxcompiler.dll missing?)";
	}
	Vec2 grab{};
	ASSERT_TRUE(GrabTheGizmo(grab));

	Vec3 startPosition = _object.Get()->GetWorldPosition();
	auto checksum = [this]() { return ViewChecksum(); };
	ASSERT_NE(checksum(), 0ull);

	_driver.Stamp(MouseMove(grab.x + 40.0f, grab.y));
	u32 presents = PresentsUntilChanged(_driver, checksum);
	EXPECT_NE(_object.Get()->GetWorldPosition().x, startPosition.x);
	EXPECT_EQ(presents, 0u);
}

TEST_F(SceneViewLatencyFixture, StagingPrefabTakesLevelOutOfViewport) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	auto settle = [this]() {
		for (int frame = 0; frame < 3; ++frame) {
			_driver.Frame();
		}
	};
	auto snapshot = [this]() {
		Vec2I size{};
		Vector<u8> out{GetMemoryResource(MemTag::Graphics)};
		EXPECT_TRUE(RenderSystem::ReadbackViewTarget(PANEL_TARGET, out, size));
		return out;
	};
	auto differingPixels = [](const Vector<u8>& a, const Vector<u8>& b) {
		EXPECT_EQ(a.size(), b.size());
		u32 count = 0;
		for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
			i32 dr = static_cast<i32>(a[i]) - static_cast<i32>(b[i]);
			i32 dg = static_cast<i32>(a[i + 1]) - static_cast<i32>(b[i + 1]);
			i32 db = static_cast<i32>(a[i + 2]) - static_cast<i32>(b[i + 2]);
			count += (std::abs(dr) + std::abs(dg) + std::abs(db) > 24) ? 1u : 0u;
		}
		return count;
	};

	settle();
	Vector<u8> emptyWorld = snapshot();

	_object = SceneSystem::CreateObject("Target", _scene);
	ASSERT_TRUE(_object.IsValid());
	if (!GiveTheObjectAMesh()) {
		GTEST_SKIP() << "fixture cube didn't import (no dxcompiler.dll?)";
	}
	settle();
	Vector<u8> withLevel = snapshot();
	const u32 levelPixels = differingPixels(withLevel, emptyWorld);
	ASSERT_GT(levelPixels, 200u);

	Handle<SceneObject> bare = SceneSystem::CreateObject("Bare", _scene);
	Guid guid = SceneSystem::SavePrefab(bare, Path("project://Bare.prefab"));
	ASSERT_TRUE(guid.IsValid());
	SceneSystem::Destroy(bare);
	SceneSystem::EndFrame();

	ASSERT_TRUE(EditorSceneOps::OpenPrefab(guid).IsValid());
	settle();
	EXPECT_EQ(differingPixels(snapshot(), emptyWorld), 0u);

	ASSERT_TRUE(EditorSceneOps::LeavePrefabStage());
	settle();
	EXPECT_EQ(differingPixels(snapshot(), withLevel), 0u);
}

TEST_F(SceneViewLatencyFixture, RuntimeViewDrawsCamerasPassRows) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Asset* gridShader = AssetDatabase::LoadAsset(Path("engine://Shaders/EditorGrid.hlsl"));
	if (!gridShader) {
		GTEST_SKIP() << "probe shader didn't import (no dxcompiler.dll?)";
	}

	const bool restoreWindows = RenderSystem::GetSceneCamerasRenderToWindows();
	RenderSystem::SetSceneCamerasRenderToWindows(false);

	Handle<SceneObject> eye = SceneSystem::CreateObject("Camera", _scene);
	ASSERT_TRUE(eye.IsValid());
	eye.Get()->SetLocalPosition(Vec3(0.0f, 3.0f, -6.0f));
	Handle<Camera> camera = eye.Get()->AddComponent<Camera>();
	ASSERT_TRUE(camera.IsValid());

	ASSERT_TRUE(OpenPanel("RuntimeViewPanel"_hash));

	auto settle = [this]() {
		for (int frame = 0; frame < 3; ++frame) {
			_driver.Frame();
		}
	};
	auto snapshot = [this]() {
		Vec2I size{};
		Vector<u8> out{GetMemoryResource(MemTag::Graphics)};
		EXPECT_TRUE(RenderSystem::ReadbackViewTarget(PANEL_TARGET, out, size));
		return out;
	};
	auto litPixels = [](const Vector<u8>& a, const Vector<u8>& b) {
		u32 count = 0;
		for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
			const i32 dr = static_cast<i32>(a[i]) - static_cast<i32>(b[i]);
			const i32 dg = static_cast<i32>(a[i + 1]) - static_cast<i32>(b[i + 1]);
			const i32 db = static_cast<i32>(a[i + 2]) - static_cast<i32>(b[i + 2]);
			count += (std::abs(dr) + std::abs(dg) + std::abs(db) > 24) ? 1u : 0u;
		}
		return count;
	};

	settle();
	const Vector<u8> withoutRow = snapshot();

	PassProbes::DrawingProbePass row;
	row.point = PassPoint::AfterOpaque;
	row.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(gridShader));
	const Mat4 view = eye.Get()->GetWorldMatrix().Inverse();
	const Mat4 projection = camera.Get()->BuildProjection(1.0f);
	PassProbes::GridPush push = PassProbes::MakeVisiblePush(view, projection, eye.Get()->GetWorldPosition());
	row.bytes = {reinterpret_cast<const u8*>(&push), PassProbes::GRID_PUSH_BYTES};

	camera.Get()->SetPass(0, Handle<RenderPass>(&row));
	settle();
	const Vector<u8> withRow = snapshot();
	EXPECT_TRUE(row.drew);
	EXPECT_GT(litPixels(withRow, withoutRow), 200u);

	camera.Get()->SetPass(0, Handle<RenderPass>{});
	settle();
	EXPECT_EQ(litPixels(snapshot(), withoutRow), 0u);

	RenderSystem::SetSceneCamerasRenderToWindows(restoreWindows);
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

	class HeldVertexSnapKey {
	public:
		HeldVertexSnapKey() {
			WindowDesc desc;
			desc.title = "VertexSnapKey";
			desc.clientSize = {64, 64};
			desc.flags = WindowFlags::Hidden;
			_window = WindowSystem::Create(desc);
			if (_window.IsValid()) {
				::PostMessageW(Hwnd(), WM_KEYDOWN, 'V', KeyLParam(false));
				WindowSystem::PumpEvents();
			}
		}
		~HeldVertexSnapKey() {
			if (_window.IsValid()) {
				::PostMessageW(Hwnd(), WM_KEYUP, 'V', KeyLParam(true));
				WindowSystem::PumpEvents();
			}
		}

		HeldVertexSnapKey(const HeldVertexSnapKey&) = delete;
		HeldVertexSnapKey& operator=(const HeldVertexSnapKey&) = delete;

		bool Held() const { return Input::IsKeyDown(Key::V); }
	private:
		HWND Hwnd() const { return static_cast<HWND>(_window->GetNativeHandle().hwnd); }

		static LPARAM KeyLParam(bool up) {
			LPARAM lParam = 1;
			lParam |= static_cast<LPARAM>(0x2F) << 16;
			if (up) {
				lParam |= static_cast<LPARAM>(1) << 30;
				lParam |= static_cast<LPARAM>(1) << 31;
			}
			return lParam;
		}

		Handle<Window> _window;
	};

} // anonymous namespace

TEST_F(SceneViewLatencyFixture, HoldingVDragsVertexOntoVertex) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	_object = SceneSystem::CreateObject("Dragged", _scene);
	ASSERT_TRUE(_object.IsValid());
	ASSERT_TRUE(GiveTheObjectAMesh());

	Handle<SceneObject> other = SceneSystem::CreateObject("Other", _scene);
	ASSERT_TRUE(other.IsValid());
	other.Get()->SetLocalPosition(Vec3(3.0f, 1.0f, 0.0f));
	other.Get()->AddComponent<MeshRenderer>().Get()->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(_meshAsset)));

	EditorSelection::Select(Handle<Object>(_object.Get()));

	HeldVertexSnapKey held;
	ASSERT_TRUE(held.Held());

	Vec2 anchorContextPx{};
	Vec2 anchorLocalPx{};
	ASSERT_TRUE(ViewportPointFor(_object.Get()->GetWorldPosition(), anchorContextPx, anchorLocalPx));
	_driver.Stamp(MouseMove(anchorContextPx.x, anchorContextPx.y));
	_driver.Frame();

	Vector<Handle<SceneObject>> targets{GetMemoryResource(MemTag::Editor)};
	EditorViewportOps::GatherDragTargets(targets);
	Vec3 anchor{};
	ASSERT_TRUE(EditorViewportOps::FindNearestVertexIn(targets, PanelViewProjection(), ViewportSize(),
													   anchorLocalPx, anchor));

	const Vec3 startPosition = _object.Get()->GetWorldPosition();
	_driver.Stamp(MouseDown(anchorContextPx.x, anchorContextPx.y));
	_driver.Frame();

	Vec2 dropContextPx{};
	Vec2 dropLocalPx{};
	ASSERT_TRUE(ViewportPointFor(other.Get()->GetWorldPosition(), dropContextPx, dropLocalPx));
	_driver.Stamp(MouseMove(dropContextPx.x, dropContextPx.y));
	_driver.Frame();
	_driver.Frame();

	Vec3 snapped{};
	ASSERT_TRUE(EditorViewportOps::FindNearestVertexUnderCursor(
		targets, PanelRayAt(dropLocalPx), PanelViewProjection(), ViewportSize(), dropLocalPx, snapped));

	const Vec3 moved = _object.Get()->GetWorldPosition() - startPosition;
	const Vec3 expected = snapped - anchor;
	EXPECT_GT(expected.Length(), 0.5f);
	EXPECT_NEAR(moved.x, expected.x, 1.0e-3f);
	EXPECT_NEAR(moved.y, expected.y, 1.0e-3f);
	EXPECT_NEAR(moved.z, expected.z, 1.0e-3f);

	_driver.Stamp(MouseUp(dropContextPx.x, dropContextPx.y));
	_driver.Frame();
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 1u);
	EXPECT_TRUE(UndoSystem::Undo());
	EXPECT_NEAR(_object.Get()->GetWorldPosition().x, startPosition.x, 1.0e-3f);
	EXPECT_NEAR(_object.Get()->GetWorldPosition().y, startPosition.y, 1.0e-3f);
	EXPECT_NEAR(_object.Get()->GetWorldPosition().z, startPosition.z, 1.0e-3f);
}

TEST_F(SceneViewLatencyFixture, SameGestureWithoutVDoesNotSnap) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	_object = SceneSystem::CreateObject("Dragged", _scene);
	ASSERT_TRUE(_object.IsValid());
	ASSERT_TRUE(GiveTheObjectAMesh());

	Handle<SceneObject> other = SceneSystem::CreateObject("Other", _scene);
	ASSERT_TRUE(other.IsValid());
	other.Get()->SetLocalPosition(Vec3(3.0f, 1.0f, 0.0f));
	other.Get()->AddComponent<MeshRenderer>().Get()->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(_meshAsset)));

	EditorSelection::Select(Handle<Object>(_object.Get()));
	ASSERT_FALSE(Input::IsKeyDown(Key::V));

	Vec2 pressContextPx{};
	Vec2 pressLocalPx{};
	ASSERT_TRUE(ViewportPointFor(_object.Get()->GetWorldPosition(), pressContextPx, pressLocalPx));

	Vector<Handle<SceneObject>> targets{GetMemoryResource(MemTag::Editor)};
	EditorViewportOps::GatherDragTargets(targets);
	Vec3 anchor{};
	ASSERT_TRUE(EditorViewportOps::FindNearestVertexIn(targets, PanelViewProjection(), ViewportSize(),
													   pressLocalPx, anchor));
	const Vec3 startPosition = _object.Get()->GetWorldPosition();

	_driver.Stamp(MouseMove(pressContextPx.x, pressContextPx.y));
	_driver.Frame();
	_driver.Stamp(MouseDown(pressContextPx.x, pressContextPx.y));
	_driver.Frame();

	Vec2 dropContextPx{};
	Vec2 dropLocalPx{};
	ASSERT_TRUE(ViewportPointFor(other.Get()->GetWorldPosition(), dropContextPx, dropLocalPx));
	_driver.Stamp(MouseMove(dropContextPx.x, dropContextPx.y));
	_driver.Frame();
	_driver.Frame();

	Vec3 snapped{};
	ASSERT_TRUE(EditorViewportOps::FindNearestVertexUnderCursor(
		targets, PanelRayAt(dropLocalPx), PanelViewProjection(), ViewportSize(), dropLocalPx, snapped));

	const Vec3 moved = _object.Get()->GetWorldPosition() - startPosition;
	EXPECT_GT(moved.Length(), 0.1f);
	EXPECT_GT((moved - (snapped - anchor)).Length(), 0.25f);
}

TEST_F(SceneViewLatencyFixture, HoldingVMarksTheVertexOnScreen) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	_object = SceneSystem::CreateObject("Dragged", _scene);
	ASSERT_TRUE(_object.IsValid());
	ASSERT_TRUE(GiveTheObjectAMesh());
	EditorSelection::Select(Handle<Object>(_object.Get()));

	auto settle = [this]() {
		for (int frame = 0; frame < 3; ++frame) {
			_driver.Frame();
		}
	};
	auto snapshot = [this]() {
		Vec2I size{};
		Vector<u8> out{GetMemoryResource(MemTag::Graphics)};
		EXPECT_TRUE(RenderSystem::ReadbackViewTarget(PANEL_TARGET, out, size));
		return out;
	};
	auto changedNear = [](const Vector<u8>& a, const Vector<u8>& b, const Vec2& centerPx, const Vec2I& size,
						  f32 radius) {
		EXPECT_EQ(a.size(), b.size());
		u32 count = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				const f32 dx = static_cast<f32>(x) - centerPx.x;
				const f32 dy = static_cast<f32>(y) - centerPx.y;
				if (dx * dx + dy * dy > radius * radius) {
					continue;
				}
				const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(size.x) + static_cast<size_t>(x)) * 4;
				if (i + 3 >= a.size() || i + 3 >= b.size()) {
					continue;
				}
				const i32 dr = static_cast<i32>(a[i]) - static_cast<i32>(b[i]);
				const i32 dg = static_cast<i32>(a[i + 1]) - static_cast<i32>(b[i + 1]);
				const i32 db = static_cast<i32>(a[i + 2]) - static_cast<i32>(b[i + 2]);
				count += (std::abs(dr) + std::abs(dg) + std::abs(db) > 24) ? 1u : 0u;
			}
		}
		return count;
	};

	Vec2 contextPx{};
	Vec2 localPx{};
	ASSERT_TRUE(ViewportPointFor(_object.Get()->GetWorldPosition(), contextPx, localPx));
	_driver.Stamp(MouseMove(contextPx.x, contextPx.y));
	settle();
	const Vector<u8> before = snapshot();

	Vec2I targetSize = RenderSystem::GetViewTargetSize(PANEL_TARGET);
	ASSERT_GT(targetSize.x, 0);

	Vector<Handle<SceneObject>> targets{GetMemoryResource(MemTag::Editor)};
	EditorViewportOps::GatherDragTargets(targets);
	Vec3 anchor{};
	ASSERT_TRUE(EditorViewportOps::FindNearestVertexIn(targets, PanelViewProjection(), ViewportSize(), localPx,
													   anchor));
	Vec2 anchorPx{};
	ASSERT_TRUE(EditorViewportOps::ProjectToViewport(PanelViewProjection(), anchor, ViewportSize(), anchorPx));
	ASSERT_GT((anchorPx - localPx).Length(), 12.0f);

	settle();
	EXPECT_EQ(changedNear(snapshot(), before, anchorPx, targetSize, 10.0f), 0u);

	{
		HeldVertexSnapKey held;
		ASSERT_TRUE(held.Held());
		settle();
		EXPECT_GT(changedNear(snapshot(), before, anchorPx, targetSize, 10.0f), 4u);
	}

	settle();
	EXPECT_EQ(changedNear(snapshot(), before, anchorPx, targetSize, 10.0f), 0u);
}

TEST_F(SceneViewLatencyFixture, VOutsideViewportMarksNothing) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(OpenSceneView());

	_object = SceneSystem::CreateObject("Dragged", _scene);
	ASSERT_TRUE(_object.IsValid());
	ASSERT_TRUE(GiveTheObjectAMesh());
	EditorSelection::Select(Handle<Object>(_object.Get()));

	auto settle = [this]() {
		for (int frame = 0; frame < 3; ++frame) {
			_driver.Frame();
		}
	};
	auto snapshot = [this]() {
		Vec2I size{};
		Vector<u8> out{GetMemoryResource(MemTag::Graphics)};
		EXPECT_TRUE(RenderSystem::ReadbackViewTarget(PANEL_TARGET, out, size));
		return out;
	};
	auto changedAnywhere = [](const Vector<u8>& a, const Vector<u8>& b) {
		EXPECT_EQ(a.size(), b.size());
		u32 count = 0;
		for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
			const i32 dr = static_cast<i32>(a[i]) - static_cast<i32>(b[i]);
			const i32 dg = static_cast<i32>(a[i + 1]) - static_cast<i32>(b[i + 1]);
			const i32 db = static_cast<i32>(a[i + 2]) - static_cast<i32>(b[i + 2]);
			count += (std::abs(dr) + std::abs(dg) + std::abs(db) > 24) ? 1u : 0u;
		}
		return count;
	};

	Vec2 contextPx{};
	Vec2 localPx{};
	ASSERT_TRUE(ViewportPointFor(_object.Get()->GetWorldPosition(), contextPx, localPx));
	const Vec2 origin{contextPx.x - localPx.x, contextPx.y - localPx.y};
	ASSERT_GE(origin.y, 4.0f);

	_driver.Stamp(MouseMove(contextPx.x, origin.y - 2.0f));
	settle();
	const Vector<u8> before = snapshot();

	settle();
	ASSERT_EQ(changedAnywhere(snapshot(), before), 0u);

	{
		HeldVertexSnapKey held;
		ASSERT_TRUE(held.Held());
		settle();
		EXPECT_EQ(changedAnywhere(snapshot(), before), 0u);
	}
}

#endif
