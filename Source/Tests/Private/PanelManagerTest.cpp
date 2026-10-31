#include "TestsPCH.h"

#include "Shalltear/Core/Serialization/TextSerializer.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PanelManager.h"

using namespace Shalltear;

namespace {

	std::vector<str> _panelLog;

	class PmTestPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(PmTestPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Test Panel")
		SLT_REFLECT_FIELD(stateNumber)
		SLT_END_REFLECT()

		PmTestPanel() = default;

		i32 stateNumber = 7;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override {
			for (int i = 0; i < 2; ++i) {
				UI::ElementHandle child = context.CreateElement();
				context.AppendChild(root, child);
				context.SetTypeTag(child, "pm-test-content"_hash);
			}
		}
		void OnTick(f32) override {
			_panelLog.push_back("tick:" + std::to_string(GetInstanceSerial()));
			tickSawContext = GetPanelContext() != nullptr;
		}
		bool tickSawContext = false;
		void OnSubmitView(f32) override { _panelLog.push_back("submit:" + std::to_string(GetInstanceSerial())); }
		void OnOpen() override { _panelLog.push_back("open:" + std::to_string(stateNumber)); }
		void OnClose() override { _panelLog.push_back("close:" + std::to_string(GetInstanceSerial())); }
		void OnSelectionChanged() override { _panelLog.push_back("sel:" + std::to_string(GetInstanceSerial())); }
	};

	class PmOtherPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(PmOtherPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Other")
		SLT_END_REFLECT()

		PmOtherPanel() = default;

		void OnBuildContent(UI::Context&, UI::ElementHandle) override {}
	};

	class PanelManagerTest : public ::testing::Test {
	protected:
		void SetUp() override {
			_panelLog.clear();
			TypeRegistry::InitializePrimitives();
			PmTestPanel::GetReflectType();
			PmOtherPanel::GetReflectType();
			EditorSelection::Initialize();
			PanelManager::Initialize();
		}
		void TearDown() override {
			PanelManager::Terminate();
			EditorSelection::Terminate();
		}

		static bool LogContains(const str& entry) {
			for (const str& line : _panelLog) {
				if (line == entry) {
					return true;
				}
			}
			return false;
		}
	};

} // anonymous namespace

TEST_F(PanelManagerTest, OpenAssignsSerialsAndNumberedTitles) {
	Handle<EditorPanel> first = PanelManager::OpenPanel(PmTestPanel::GetReflectType());
	Handle<EditorPanel> second = PanelManager::OpenPanel(PmTestPanel::GetReflectType());
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());

	EXPECT_EQ(first.Get()->GetInstanceSerial(), 1u);
	EXPECT_EQ(second.Get()->GetInstanceSerial(), 2u);
	EXPECT_EQ(first.Get()->GetTitle(), "Test Panel");
	EXPECT_EQ(second.Get()->GetTitle(), "Test Panel (2)");
	EXPECT_NE(first.Get()->GetDockId(), second.Get()->GetDockId());
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 2u);
	EXPECT_TRUE(LogContains("open:7"));
}

TEST_F(PanelManagerTest, DockIdsAreReproducible) {
	const TypeInfo* type = PmTestPanel::GetReflectType();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
	StringHash originalId = panel.Get()->GetDockId();
	EXPECT_EQ(originalId, PanelManager::MakeDockId(type, 1));

	PanelManager::ClosePanel(panel);
	PanelManager::TickAll(0.016f);

	Handle<EditorPanel> reopened = PanelManager::OpenPanel(type);
	EXPECT_EQ(reopened.Get()->GetDockId(), originalId);
	EXPECT_NE(PanelManager::MakeDockId(type, 2), originalId);
	EXPECT_NE(PanelManager::MakeDockId(PmOtherPanel::GetReflectType(), 1), originalId);
}

TEST_F(PanelManagerTest, LowestFreeSerialIsReused) {
	const TypeInfo* type = PmTestPanel::GetReflectType();
	PanelManager::OpenPanel(type);
	Handle<EditorPanel> second = PanelManager::OpenPanel(type);
	PanelManager::OpenPanel(type);

	PanelManager::ClosePanel(second);
	PanelManager::TickAll(0.016f);

	Handle<EditorPanel> reopened = PanelManager::OpenPanel(type);
	EXPECT_EQ(reopened.Get()->GetInstanceSerial(), 2u);
}

TEST_F(PanelManagerTest, CloseFiresOnCloseAndDefersDestruction) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(PmTestPanel::GetReflectType());
	PanelManager::ClosePanel(panel);

	EXPECT_TRUE(LogContains("close:1"));
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 0u);
	EXPECT_TRUE(panel.IsValid());
	PanelManager::TickAll(0.016f);
	EXPECT_FALSE(panel.IsValid());
}

TEST_F(PanelManagerTest, PanelTypesAreConcreteOnly) {
	Vector<const TypeInfo*> types{GetMemoryResource(MemTag::Editor)};
	PanelManager::GetPanelTypes(types);

	bool foundTest = false, foundOther = false, foundBase = false;
	for (const TypeInfo* type : types) {
		if (type == PmTestPanel::GetReflectType()) {
			foundTest = true;
		}
		if (type == PmOtherPanel::GetReflectType()) {
			foundOther = true;
		}
		if (type == EditorPanel::GetReflectType()) {
			foundBase = true;
		}
	}
	EXPECT_TRUE(foundTest);
	EXPECT_TRUE(foundOther);
	EXPECT_FALSE(foundBase);

	EXPECT_EQ(PanelManager::GetPanelTypeName(PmTestPanel::GetReflectType()), "Test Panel");
	EXPECT_EQ(PanelManager::GetPanelTypeName(PmOtherPanel::GetReflectType()), "Other");
}

TEST_F(PanelManagerTest, TickAllTicksEveryOpenPanel) {
	PanelManager::OpenPanel(PmTestPanel::GetReflectType());
	PanelManager::OpenPanel(PmTestPanel::GetReflectType());
	PanelManager::TickAll(0.016f);
	EXPECT_TRUE(LogContains("tick:1"));
	EXPECT_TRUE(LogContains("tick:2"));
}

TEST_F(PanelManagerTest, EveryOpenPanelIsTickedThenSubmitted) {
	PanelManager::OpenPanel(PmTestPanel::GetReflectType());
	PanelManager::OpenPanel(PmTestPanel::GetReflectType());
	_panelLog.clear();

	PanelManager::TickAll(0.016f);
	PanelManager::SubmitViews(0.016f);

	ASSERT_EQ(_panelLog.size(), 4u);
	EXPECT_EQ(_panelLog[0], "tick:1");
	EXPECT_EQ(_panelLog[1], "tick:2");
	EXPECT_EQ(_panelLog[2], "submit:1");
	EXPECT_EQ(_panelLog[3], "submit:2");
}

TEST_F(PanelManagerTest, ClosedPanelNotSubmitted) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(PmTestPanel::GetReflectType());
	PanelManager::TickAll(0.016f);
	PanelManager::ClosePanel(panel);
	_panelLog.clear();

	PanelManager::SubmitViews(0.016f);
	EXPECT_FALSE(LogContains("submit:1"));
	EXPECT_TRUE(panel.IsValid());
}

TEST_F(PanelManagerTest, SelectionChangesForwardToOpenPanels) {
	PanelManager::OpenPanel(PmTestPanel::GetReflectType());
	Object selected;
	EditorSelection::Select(Handle<Object>(&selected));
	EXPECT_TRUE(LogContains("sel:1"));
}

TEST_F(PanelManagerTest, RestorePanelAppliesStateBeforeOnOpen) {
	const TypeInfo* type = PmTestPanel::GetReflectType();

	Handle<EditorPanel> original = PanelManager::OpenPanel(type);
	static_cast<PmTestPanel*>(original.Get())->stateNumber = 42;
	str stateJSON = TextSerializer::Serialize(original.Get(), type);
	PanelManager::ClosePanel(original);
	PanelManager::TickAll(0.016f);
	_panelLog.clear();

	Handle<EditorPanel> restored = PanelManager::RestorePanel(type, 5, stateJSON);
	ASSERT_TRUE(restored.IsValid());
	EXPECT_EQ(restored.Get()->GetInstanceSerial(), 5u);
	EXPECT_EQ(static_cast<PmTestPanel*>(restored.Get())->stateNumber, 42);
	EXPECT_TRUE(LogContains("open:42"));

	EXPECT_FALSE(PanelManager::RestorePanel(type, 5, {}).IsValid());
}

TEST_F(PanelManagerTest, FindPanelByDockIdResolvesInstances) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(PmTestPanel::GetReflectType());
	StringHash dockId = panel.Get()->GetDockId();
	EXPECT_EQ(PanelManager::FindPanelByDockId(dockId), panel);
	EXPECT_FALSE(PanelManager::FindPanelByDockId(StringHash("nonsense")).IsValid());
}

TEST_F(PanelManagerTest, BuildContentIsRebuildIdempotent) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(PmTestPanel::GetReflectType());

	UI::ContextDesc desc;
	desc.sizePx = {400, 300};
	UI::Context context(desc, nullptr);

	u32 counts[2] = {};
	for (int build = 0; build < 2; ++build) {
		UI::ElementHandle root = context.CreateElement();
		context.AppendChild(context.GetRoot(), root);
		panel.Get()->BuildContent(context, root);
		u32 count = 0;
		const UI::Element* pod = context.GetElement(root);
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
			++count;
			child = context.GetElement(context.MakeHandle(child))->nextSibling;
		}
		counts[build] = count;
	}
	EXPECT_EQ(counts[0], 2u);
	EXPECT_EQ(counts[0], counts[1]);
}

TEST_F(PanelManagerTest, TickReachesBuildContext) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("PmTestPanel")));
	ASSERT_TRUE(panel.IsValid());
	PmTestPanel* concrete = static_cast<PmTestPanel*>(panel.Get());

	PanelManager::TickAll(0.016f);
	EXPECT_FALSE(concrete->tickSawContext);

	UI::Context context({{400, 300}, 1.0f}, nullptr);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);

	PanelManager::TickAll(0.016f);
	EXPECT_TRUE(concrete->tickSawContext);
}
