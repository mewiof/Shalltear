#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/ProfilerModules.h"

#include <format>
#include <set>

using namespace Shalltear;
using namespace UITest;

namespace {

	class ProfilerModulesFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			EditorSelection::Initialize();
			PanelManager::Initialize();
		}

		void TearDown() override {
			PanelManager::Terminate();
			EditorSelection::Terminate();
			ProfilerModules::Terminate();
		}

		static void Tick(UI::Context& context) {
			PanelManager::TickAll(0.3f);
			context.Update();
		}

		static UI::ElementHandle OpenWindow(UI::Context& context, Handle<EditorPanel>& panel) {
			panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("ProfilerPanel")));
			if (!panel.IsValid()) {
				return {};
			}
			UI::ElementHandle root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel.Get()->BuildContent(context, root);
			context.Update();
			return root;
		}

		static UI::ElementHandle HostUnder(UI::Context& context, UI::ElementHandle root, std::string_view title) {
			const UI::Element* heading = context.GetElement(FindByText(context, root, title));
			return heading ? context.MakeHandle(heading->nextSibling) : UI::ElementHandle{};
		}
	};

	struct Calls {
		u32 builds = 0;
		u32 refreshes = 0;
		u32 samples = 0;
		std::set<u32> refreshedHosts;
	};

	ProfilerModule Recording(const char* title, Calls& calls) {
		ProfilerModule module;
		module.title = title;
		module.build = [&calls](UI::Context& context, UI::ElementHandle host) {
			++calls.builds;
			UI::ElementHandle line = context.CreateElement();
			context.AppendChild(host, line);
			context.SetText(line, {}, 12.0f, Color::White(), "waiting");
		};
		module.refresh = [&calls](UI::Context& context, UI::ElementHandle host) {
			++calls.refreshes;
			calls.refreshedHosts.insert(host.index);
			const UI::Element* pod = context.GetElement(host);
			if (pod) {
				context.SetTextString(context.MakeHandle(pod->firstChild), std::format("refreshed {}", host.index));
			}
		};
		module.sample = [&calls]() { ++calls.samples; };
		return module;
	}

} // namespace

TEST_F(ProfilerModulesFixture, WindowsRunEveryModuleSection) {
	int moduleImage = 0;
	Calls first;
	ASSERT_TRUE(ProfilerModules::Register(StringHash("First"), Recording("First Module", first), &moduleImage));

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 1600};
	UI::Context context(desc, &fonts);
	Handle<EditorPanel> left;
	Handle<EditorPanel> right;
	const UI::ElementHandle leftRoot = OpenWindow(context, left);
	const UI::ElementHandle rightRoot = OpenWindow(context, right);
	ASSERT_TRUE(leftRoot.IsValid() && rightRoot.IsValid());

	const UI::ElementHandle leftHost = HostUnder(context, leftRoot, "First Module");
	const UI::ElementHandle rightHost = HostUnder(context, rightRoot, "First Module");
	ASSERT_TRUE(leftHost.IsValid());
	ASSERT_TRUE(rightHost.IsValid());
	EXPECT_EQ(first.builds, 2u);

	Tick(context);
	EXPECT_EQ(first.refreshedHosts.size(), 2u);
	EXPECT_TRUE(first.refreshedHosts.contains(leftHost.index));
	EXPECT_TRUE(first.refreshedHosts.contains(rightHost.index));
	EXPECT_TRUE(FindByText(context, leftRoot, std::format("refreshed {}", leftHost.index)).IsValid());
	EXPECT_FALSE(FindByText(context, leftRoot, std::format("refreshed {}", rightHost.index)).IsValid());

	u32 samples = first.samples;
	Tick(context);
	EXPECT_EQ(first.samples, samples + 2u);
	const FieldInfo* paused = FindField(TypeRegistry::FindType(StringHash("ProfilerPanel")), StringHash("paused"));
	ASSERT_NE(paused, nullptr);
	*static_cast<bool*>(paused->addrFn(right.Get())) = true;
	samples = first.samples;
	const u32 refreshes = first.refreshes;
	Tick(context);
	EXPECT_EQ(first.samples, samples + 1u);
	EXPECT_EQ(first.refreshes, refreshes + 1u);

	Calls late;
	ASSERT_TRUE(ProfilerModules::Register(StringHash("Late"), Recording("Late Module", late)));
	Tick(context);
	const UI::ElementHandle lateHost = HostUnder(context, leftRoot, "Late Module");
	EXPECT_TRUE(lateHost.IsValid());
	EXPECT_TRUE(HostUnder(context, rightRoot, "Late Module").IsValid());
	EXPECT_EQ(late.builds, 2u);
	Tick(context);
	EXPECT_EQ(late.refreshedHosts, std::set<u32>{lateHost.index});

	EXPECT_EQ(ProfilerModules::UnregisterOwnedBy(&moduleImage), 1u);
	const u32 builds = first.builds;
	samples = first.samples;
	Tick(context);
	Tick(context);
	EXPECT_FALSE(FindByText(context, leftRoot, "First Module").IsValid());
	EXPECT_FALSE(FindByText(context, rightRoot, "First Module").IsValid());
	EXPECT_TRUE(HostUnder(context, leftRoot, "Late Module").IsValid());
	EXPECT_EQ(first.builds, builds);
	EXPECT_EQ(first.samples, samples);

	PanelManager::ClosePanel(left);
	PanelManager::ClosePanel(right);
}

TEST_F(ProfilerModulesFixture, EmptyModuleRefusedIdReplaced) {
	ProfilerModule empty;
	empty.title = "Empty";
	EXPECT_FALSE(ProfilerModules::Register(StringHash("Empty"), empty));
	EXPECT_EQ(ProfilerModules::Find(StringHash("Empty")), nullptr);

	Calls a;
	Calls b;
	Calls c;
	ASSERT_TRUE(ProfilerModules::Register(StringHash("A"), Recording("A", a)));
	ASSERT_TRUE(ProfilerModules::Register(StringHash("B"), Recording("B", b)));
	ASSERT_TRUE(ProfilerModules::Register(StringHash("A"), Recording("A again", c)));
	std::vector<str> order;
	ProfilerModules::ForEach([&order](StringHash, const ProfilerModule& module) { order.push_back(module.title); });
	EXPECT_EQ(order, (std::vector<str>{"A again", "B"}));
}
