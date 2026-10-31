#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <chrono>
#include <format>

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr u32 ROWS_ONE = 1;
	constexpr u32 ROWS_MANY = 300;
	constexpr u32 ROWS_MOST = 1200;

	struct PhaseCost {
		u64 recalcs = 0;
		u64 ruleTests = 0;
		u64 textWalks = 0;
		u64 linkOps = 0;
		u64 linkWork = 0;
		f64 microseconds = 0.0;

		PhaseCost operator+(const PhaseCost& other) const {
			return {recalcs + other.recalcs, ruleTests + other.ruleTests, textWalks + other.textWalks,
					linkOps + other.linkOps, linkWork + other.linkWork, microseconds + other.microseconds};
		}
		PhaseCost operator-(const PhaseCost& other) const {
			return {recalcs - other.recalcs, ruleTests - other.ruleTests, textWalks - other.textWalks,
					linkOps - other.linkOps, linkWork - other.linkWork, microseconds - other.microseconds};
		}
	};

	struct RowCost {
		u32 rows = 0;
		u64 elements = 0;
		PhaseCost build;
		PhaseCost settle;
		PhaseCost Total() const { return build + settle; }
	};

	f64 Marginal(f64 low, f64 high, u32 lowRows, u32 highRows) {
		return (high - low) / static_cast<f64>(highRows - lowRows);
	}
	f64 MarginalOf(const RowCost& low, const RowCost& high, u64 PhaseCost::* field) {
		return Marginal(static_cast<f64>(low.Total().*field), static_cast<f64>(high.Total().*field),
						low.rows, high.rows);
	}

	u64 CountAlive(UI::Context& context, UI::ElementHandle from) {
		if (!context.IsAlive(from)) {
			return 0;
		}
		u64 count = 1;
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			count += CountAlive(context, handle);
			const UI::Element* pod = context.GetElement(handle);
			child = pod ? pod->nextSibling : UI::INVALID_UI_INDEX;
		}
		return count;
	}

	void ReportPhase(const char* what, const char* phase, u32 rows, const PhaseCost& cost) {
		f64 sheet = cost.recalcs == 0 ? 0.0 : static_cast<f64>(cost.ruleTests) / static_cast<f64>(cost.recalcs);
		std::cout << std::format("[M40] {:<9} {:<6} rows={:<5} recalcs={:<6} ruleTests={:<9} textWalks={:<5}"
								 " linkOps={:<6} linkWork={:<6} sheet={:.0f} | {:.3f} ms, {:.1f} us/row",
								 what, phase, rows, cost.recalcs, cost.ruleTests, cost.textWalks, cost.linkOps,
								 cost.linkWork, sheet, cost.microseconds / 1000.0,
								 cost.microseconds / static_cast<f64>(rows))
				  << std::endl;
	}

	void Report(const char* what, const RowCost& cost) {
		std::cout << std::format("[M40] {:<9} rows={:<5} elements={}", what, cost.rows, cost.elements) << std::endl;
		ReportPhase(what, "build", cost.rows, cost.build);
		ReportPhase(what, "settle", cost.rows, cost.settle);
		ReportPhase(what, "TOTAL", cost.rows, cost.Total());
	}

	void ReportMarginal(const char* what, const RowCost& low, const RowCost& high) {
		std::cout << std::format("[M40] {} MARGINAL {}->{} rows, build+settle: elements={:.2f} recalcs={:.2f}"
								 " ruleTests={:.1f} textWalks={:.2f} linkWork={:.2f} | {:.1f} us/row",
								 what, low.rows, high.rows,
								 Marginal(static_cast<f64>(low.elements), static_cast<f64>(high.elements),
										  low.rows, high.rows),
								 MarginalOf(low, high, &PhaseCost::recalcs),
								 MarginalOf(low, high, &PhaseCost::ruleTests),
								 MarginalOf(low, high, &PhaseCost::textWalks),
								 MarginalOf(low, high, &PhaseCost::linkWork),
								 Marginal(low.Total().microseconds, high.Total().microseconds, low.rows, high.rows))
				  << std::endl;
	}

	class EditorRowCostTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			SceneAnnotations::Initialize();
			PanelManager::Initialize();
		}
		void TearDown() override {
			PanelManager::Terminate();
			SceneAnnotations::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
		}

		static PhaseCost Mark(UI::Context& context, const CountingFont& fonts) {
			return {context.GetStyleStats().recalcs, context.GetStyleStats().ruleTests, fonts.Walks(),
					context.GetTreeStats().linkOps, context.GetTreeStats().linkWork, 0.0};
		}
		static f64 Micros(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to) {
			return std::chrono::duration<f64, std::micro>(to - from).count();
		}

		RowCost MeasureHierarchy(u32 rows) {
			RowCost cost;
			cost.rows = rows;

			Handle<Scene> scene = SceneSystem::CreateScene("M40");
			for (u32 i = 0; i < rows; ++i) {
				SceneSystem::CreateObject(std::format("Object {}", i), scene);
			}

			Handle<EditorPanel> panel =
				PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("HierarchyPanel")));
			if (!panel.IsValid()) {
				ADD_FAILURE() << "Hierarchy panel type not registered";
				return cost;
			}

			CountingFont fonts;
			UI::ContextDesc desc;
			desc.sizePx = {600, 800};
			desc.contentScale = 1.0f;
			UI::Context context(desc, &fonts);
			EditorTheme::ApplyToContext(context);

			UI::ElementHandle root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);

			context.ResetStyleStats();
			context.ResetTreeStats();
			fonts.Reset();

			auto begin = std::chrono::steady_clock::now();
			panel.Get()->BuildContent(context, root);
			auto built = std::chrono::steady_clock::now();
			PhaseCost atBuild = Mark(context, fonts);
			context.Update();
			auto settled = std::chrono::steady_clock::now();
			PhaseCost atSettle = Mark(context, fonts);

			cost.elements = CountAlive(context, root);
			cost.build = atBuild;
			cost.build.microseconds = Micros(begin, built);
			cost.settle = atSettle - atBuild;
			cost.settle.microseconds = Micros(built, settled);

			PanelManager::ClosePanel(panel);
			SceneSystem::UnloadScene(scene);
			SceneSystem::EndFrame();
			return cost;
		}

		RowCost MeasureListView(u32 rows, u32 extraRules = 0, bool extrasMatch = false) {
			RowCost cost;
			cost.rows = rows;

			CountingFont fonts;
			UI::ContextDesc desc;
			desc.sizePx = {600, 800};
			desc.contentScale = 1.0f;
			UI::Context context(desc, &fonts);
			EditorTheme::ApplyToContext(context);
			if (extraRules > 0) {
				std::string sheet;
				for (u32 i = 0; i < extraRules; ++i) {
					if (extrasMatch) {
						sheet += "* { flex-grow: 0; }\n";
					} else {
						sheet += std::format(".m40-unmatched-{} {{ flex-grow: 0; }}\n", i);
					}
				}
				context.LoadStyleSheet(sheet);
			}

			UI::ElementHandle root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);

			EditorListView list;
			ListViewDesc listDesc;
			listDesc.layout = ListLayout::Rows;
			list.Build(nullptr, context, root, listDesc);

			Vector<ListRow> built{GetMemoryResource(MemTag::Editor)};
			built.reserve(rows);
			for (u32 i = 0; i < rows; ++i) {
				ListRow row;
				row.id = i + 1;
				row.depth = 1;
				row.label = std::format("Object {}", i);
				built.push_back(std::move(row));
			}

			context.ResetStyleStats();
			context.ResetTreeStats();
			fonts.Reset();

			auto begin = std::chrono::steady_clock::now();
			list.SetRows(built);
			auto made = std::chrono::steady_clock::now();
			PhaseCost atBuild = Mark(context, fonts);
			context.Update();
			auto settled = std::chrono::steady_clock::now();
			PhaseCost atSettle = Mark(context, fonts);

			cost.elements = CountAlive(context, root);
			cost.build = atBuild;
			cost.build.microseconds = Micros(begin, made);
			cost.settle = atSettle - atBuild;
			cost.settle.microseconds = Micros(made, settled);
			return cost;
		}
	};

	constexpr f64 FLATNESS_TOLERANCE = 1.10;

	void ExpectFlat(const char* what, const char* field, f64 first, f64 second) {
		EXPECT_LE(second, first * FLATNESS_TOLERANCE) << what << ": " << field << ' ' << first << " to " << second;
	}

	void ExpectEveryCountFlat(const char* what, const RowCost& one, const RowCost& many, const RowCost& most) {
		ASSERT_GT(many.elements, one.elements);
		ASSERT_GT(many.Total().recalcs, one.Total().recalcs);
		ASSERT_GT(most.rows, many.rows);

		EXPECT_EQ(most.elements, many.elements);

		ExpectFlat(what, "elements",
				   Marginal(static_cast<f64>(one.elements), static_cast<f64>(many.elements), one.rows, many.rows),
				   Marginal(static_cast<f64>(many.elements), static_cast<f64>(most.elements), many.rows, most.rows));
		auto flat = [&](const char* field, u64 PhaseCost::* member) {
			ExpectFlat(what, field, MarginalOf(one, many, member), MarginalOf(many, most, member));
		};
		flat("recalcs", &PhaseCost::recalcs);
		flat("ruleTests", &PhaseCost::ruleTests);
		flat("textWalks", &PhaseCost::textWalks);
		flat("linkWork", &PhaseCost::linkWork);
	}

} // namespace

TEST_F(EditorRowCostTest, HierarchyRowCostIsFlat) {
	RowCost one = MeasureHierarchy(ROWS_ONE);
	RowCost many = MeasureHierarchy(ROWS_MANY);
	RowCost most = MeasureHierarchy(ROWS_MOST);

	Report("hierarchy", one);
	Report("hierarchy", many);
	Report("hierarchy", most);
	ReportMarginal("hierarchy", one, many);
	ReportMarginal("hierarchy", many, most);

	ExpectEveryCountFlat("hierarchy", one, many, most);
}

TEST_F(EditorRowCostTest, ListViewRowCostIsFlat) {
	RowCost one = MeasureListView(ROWS_ONE);
	RowCost many = MeasureListView(ROWS_MANY);
	RowCost most = MeasureListView(ROWS_MOST);

	Report("listview", one);
	Report("listview", many);
	Report("listview", most);
	ReportMarginal("listview", one, many);
	ReportMarginal("listview", many, most);

	ExpectEveryCountFlat("listview", one, many, most);
}

TEST_F(EditorRowCostTest, RowStylesComposedOnRead) {
	RowCost one = MeasureHierarchy(ROWS_ONE);
	RowCost many = MeasureHierarchy(ROWS_MANY);
	Report("hierarchy", many);

	constexpr u64 SPACER_RESTYLES = 2;
	auto expectComposedOnce = [SPACER_RESTYLES](const char* what, const RowCost& cost) {
		EXPECT_LE(cost.Total().recalcs, cost.elements + SPACER_RESTYLES);
	};
	ASSERT_GT(many.elements, one.elements);
	expectComposedOnce("hierarchy one", one);
	expectComposedOnce("hierarchy many", many);

	EXPECT_EQ(many.build.recalcs, 0u);
	EXPECT_GT(many.settle.recalcs, 0u);
}

TEST_F(EditorRowCostTest, SettleReadsSheetBuildShapesNothing) {
	RowCost cost = MeasureHierarchy(ROWS_MANY);

	EXPECT_EQ(cost.build.textWalks, 0u);
	EXPECT_GT(cost.settle.textWalks, 0u);

	EXPECT_GT(cost.Total().ruleTests, cost.Total().recalcs * 10);
}

TEST_F(EditorRowCostTest, RowCostDependsOnSheet) {
	RowCost plain = MeasureListView(ROWS_MANY);
	ASSERT_GT(plain.Total().recalcs, 0u);
	const u64 sheetRules = plain.Total().ruleTests / plain.Total().recalcs;

	RowCost skipped = MeasureListView(ROWS_MANY, static_cast<u32>(sheetRules), false);
	RowCost applied = MeasureListView(ROWS_MANY, static_cast<u32>(sheetRules), true);

	Report("plain", plain);
	Report("rejected", skipped);
	Report("applied", applied);

	for (const RowCost& widened : {skipped, applied}) {
		EXPECT_EQ(widened.elements, plain.elements);
		EXPECT_EQ(widened.Total().recalcs, plain.Total().recalcs);
		EXPECT_EQ(widened.Total().linkOps, plain.Total().linkOps);
		EXPECT_EQ(widened.Total().ruleTests, plain.Total().recalcs * (sheetRules * 2));
	}
}
