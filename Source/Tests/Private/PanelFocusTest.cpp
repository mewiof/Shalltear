#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/PanelManager.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	class ListHostPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(ListHostPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("List Host Probe")
		SLT_END_REFLECT()

		u32 deletes = 0;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override {
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.width = UI::Length::Percent(1.0f);
			rootStyle.height = UI::Length::Percent(1.0f);
			context.SetStyle(root, rootStyle);

			ListViewDesc desc;
			desc.layout = ListLayout::Rows;
			desc.onDelete = [this]() { ++deletes; };
			list.Build(this, context, root, std::move(desc));

			Vector<ListRow> rows{GetMemoryResource(MemTag::Debug)};
			for (u64 id = 1; id <= 3; ++id) {
				ListRow row;
				row.id = id;
				row.label = "Row " + std::to_string(id);
				rows.push_back(std::move(row));
			}
			list.SetRows(rows);
		}

		EditorListView list;
	};

	class PanelFocusTest : public ::testing::Test {
	protected:
		MockFontSystem _fonts;

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			ListHostPanel::GetReflectType();
			PanelManager::Initialize();
		}
		void TearDown() override { PanelManager::Terminate(); }

		ListHostPanel* Open(UI::Context& context) {
			Handle<EditorPanel> handle = PanelManager::OpenPanel(ListHostPanel::GetReflectType());
			EditorPanel* panel = handle.Get();
			if (!panel) {
				return nullptr;
			}
			UI::ElementHandle root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel->BuildContent(context, root);
			context.Update();
			return static_cast<ListHostPanel*>(panel);
		}

		static UI::ContextDesc Desc() {
			UI::ContextDesc desc;
			desc.sizePx = {400, 300};
			desc.contentScale = 1.0f;
			return desc;
		}
	};

} // anonymous namespace

TEST_F(PanelFocusTest, ProducedRowWaitsForKeyboard) {
	UI::Context context{Desc(), &_fonts};
	ListHostPanel* panel = Open(context);
	ASSERT_NE(panel, nullptr);

	context.ClearFocus();
	EXPECT_FALSE(panel->list.HasFocus());
	EXPECT_FALSE(panel->OnCommand(PanelCommand::Delete));
	EXPECT_EQ(panel->deletes, 0u);
}

TEST_F(PanelFocusTest, FocusListTakesKeyboard) {
	UI::Context context{Desc(), &_fonts};
	ListHostPanel* panel = Open(context);
	ASSERT_NE(panel, nullptr);
	context.ClearFocus();

	panel->FocusList(panel->list);

	EXPECT_TRUE(panel->list.HasFocus());
	EXPECT_TRUE(panel->OnCommand(PanelCommand::Delete));
	EXPECT_EQ(panel->deletes, 1u);
}

TEST_F(PanelFocusTest, FocusListRefusesForeignList) {
	UI::Context context{Desc(), &_fonts};
	ListHostPanel* mine = Open(context);
	ListHostPanel* theirs = Open(context);
	ASSERT_NE(mine, nullptr);
	ASSERT_NE(theirs, nullptr);
	context.ClearFocus();

	mine->FocusList(theirs->list);

	EXPECT_FALSE(theirs->list.HasFocus());
	EXPECT_FALSE(mine->list.HasFocus());
}

TEST_F(PanelFocusTest, FocusListFollowsListAcrossRebuild) {
	UI::Context context{Desc(), &_fonts};
	ListHostPanel* panel = Open(context);
	ASSERT_NE(panel, nullptr);

	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel->BuildContent(context, root);
	context.Update();
	context.ClearFocus();

	panel->FocusList(panel->list);
	EXPECT_TRUE(panel->list.HasFocus());
	EXPECT_TRUE(panel->OnCommand(PanelCommand::Delete));
}
