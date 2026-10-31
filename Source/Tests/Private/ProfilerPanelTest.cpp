#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Profiler.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	class ProfilerPanelFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			PanelManager::Initialize();
			Profiler::SetEnabled(false);
			Profiler::NextFrame();
		}

		void TearDown() override {
			Profiler::SetEnabled(false);
			Profiler::NextFrame();
			PanelManager::Terminate();
		}

		static Handle<EditorPanel> OpenPanel() {
			return PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("ProfilerPanel")));
		}

		struct Harness {
			MockFontSystem fonts;
			UI::Context context;
			UI::ElementHandle root;
			Handle<EditorPanel> panel;

			explicit Harness(Handle<EditorPanel> opened)
				: context(MakeDesc(), &fonts), panel(opened) {
				EditorTheme::ApplyToContext(context);
				root = context.CreateElement();
				context.AppendChild(context.GetRoot(), root);
				panel.Get()->BuildContent(context, root);
				context.Update();
			}

			static UI::ContextDesc MakeDesc() {
				UI::ContextDesc desc;
				desc.sizePx = {900, 700};
				return desc;
			}
		};

		static void CollectTexts(UI::Context& context, UI::ElementHandle from, Vector<str>& out) {
			str text = context.GetTextString(from);
			if (!text.empty()) {
				out.push_back(std::move(text));
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				CollectTexts(context, handle, out);
				child = context.GetElement(handle)->nextSibling;
			}
		}

		static bool SaysSomethingContaining(UI::Context& context, UI::ElementHandle from, std::string_view fragment) {
			Vector<str> texts{GetMemoryResource(MemTag::Editor)};
			CollectTexts(context, from, texts);
			for (const str& text : texts) {
				if (text.find(fragment) != str::npos) {
					return true;
				}
			}
			return false;
		}

		static UI::ElementHandle FirstCheckbox(UI::Context& context, UI::ElementHandle from) {
			if (context.HasClass(from, StringHash("slt-checkbox"))) {
				return from;
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				UI::ElementHandle found = FirstCheckbox(context, handle);
				if (found.IsValid()) {
					return found;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return {};
		}

		static UI::ElementHandle RecordToggle(UI::Context& context, UI::ElementHandle from) {
			Vector<UI::ElementHandle> boxes{GetMemoryResource(MemTag::Editor)};
			CollectCheckboxes(context, from, boxes);
			return boxes.size() >= 2 ? boxes[1] : UI::ElementHandle{};
		}

		static void CollectCheckboxes(UI::Context& context, UI::ElementHandle from, Vector<UI::ElementHandle>& out) {
			if (context.HasClass(from, StringHash("slt-checkbox"))) {
				out.push_back(from);
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				CollectCheckboxes(context, handle, out);
				child = context.GetElement(handle)->nextSibling;
			}
		}
	};

} // anonymous namespace

TEST_F(ProfilerPanelFixture, RecordingOffSaysSo) {
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness{panel};

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "not recording"));
}

TEST_F(ProfilerPanelFixture, ClickingRecordEnablesTheProcessWideProfiler) {
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness{panel};
	ASSERT_FALSE(Profiler::IsEnabled());

	UI::ElementHandle toggle = RecordToggle(harness.context, harness.root);
	ASSERT_TRUE(harness.context.IsAlive(toggle));
	const UI::Rect box = harness.context.GetLayout(toggle).borderBox;
	ASSERT_GT(box.w, 0.0f);
	ASSERT_GT(box.h, 0.0f);

	ClickElement(harness.context, toggle);
	EXPECT_TRUE(Profiler::IsEnabled());
}

TEST_F(ProfilerPanelFixture, RecordedFrameBecomesRows) {
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());

	Profiler::SetEnabled(true);
	const u32 site = Profiler::RegisterSite("Panel.Probe");
	Profiler::Enter(site);
	Profiler::Leave();
	Profiler::Enter(site);
	Profiler::Leave();
	Profiler::NextFrame();

	Harness harness{panel};
	panel.Get()->OnTick(1.0f);
	harness.context.Update();

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "Panel.Probe"));
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "x2"));
}

TEST_F(ProfilerPanelFixture, PanelsShareSwitchAndFrame) {
	Handle<EditorPanel> first = OpenPanel();
	Handle<EditorPanel> second = OpenPanel();
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	ASSERT_NE(first.GetInstanceId(), second.GetInstanceId());

	Harness firstHarness{first};
	UI::ElementHandle toggle = RecordToggle(firstHarness.context, firstHarness.root);
	ASSERT_TRUE(firstHarness.context.IsAlive(toggle));
	ClickElement(firstHarness.context, toggle);
	ASSERT_TRUE(Profiler::IsEnabled());

	const u32 site = Profiler::RegisterSite("Panel.Shared");
	Profiler::Enter(site);
	Profiler::Leave();
	Profiler::NextFrame();

	Harness secondHarness{second};
	second.Get()->OnTick(1.0f);
	secondHarness.context.Update();
	EXPECT_FALSE(SaysSomethingContaining(secondHarness.context, secondHarness.root, "not recording"));
	EXPECT_TRUE(SaysSomethingContaining(secondHarness.context, secondHarness.root, "Panel.Shared"));
}
