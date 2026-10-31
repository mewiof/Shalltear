#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UIDocument.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
	}

	constexpr u64 FIXTURE_MARK = 0xF1;
	constexpr u64 OVERRIDE_MARK = 0xF2;

	constexpr std::string_view ProbeClasses[] = {"fixture-widget"};
	constexpr UI::WidgetAttribute ProbeAttributes[] = {
		{"caption", UI::WidgetAttributeType::String},
		{"enabled", UI::WidgetAttributeType::Bool},
		{"amount", UI::WidgetAttributeType::Number},
	};

	UI::WidgetResult MakeProbe(UI::Context& context, UI::ElementHandle parent, const UI::WidgetArgs& args) {
		UI::ElementHandle root = context.CreateElement();
		context.AppendChild(parent, root);
		context.AddClass(root, StringHash("fixture-widget"));
		context.SetUserTag(root, FIXTURE_MARK + (args.GetBool("enabled") ? 1u : 0u));
		return {root, {}};
	}

	UI::WidgetResult MakeOverride(UI::Context& context, UI::ElementHandle parent, const UI::WidgetArgs&) {
		UI::ElementHandle root = context.CreateElement();
		context.AppendChild(parent, root);
		context.SetUserTag(root, OVERRIDE_MARK);
		return {root, {}};
	}

	class UIWidgetRegistryFixture : public ::testing::Test {
	protected:
		void SetUp() override { UI::WidgetRegistry::Shutdown(); }
		void TearDown() override { UI::WidgetRegistry::Shutdown(); }

		static const void* FakeOwner() {
			static int owner = 0;
			return &owner;
		}
	};

} // anonymous namespace

TEST_F(UIWidgetRegistryFixture, BuiltInsAreFallback) {
	const UI::WidgetDesc* button = UI::WidgetRegistry::Find("button");
	ASSERT_NE(button, nullptr);
	ASSERT_EQ(button->attributes.size(), 1u);
	EXPECT_EQ(button->attributes[0].name, "text");
	EXPECT_EQ(button->attributes[0].type, UI::WidgetAttributeType::String);
	ASSERT_EQ(button->builtInClasses.size(), 1u);
	EXPECT_EQ(button->builtInClasses[0], "slt-button");

	EXPECT_NE(UI::WidgetRegistry::Find("checkbox"), nullptr);
	EXPECT_NE(UI::WidgetRegistry::Find("slider"), nullptr);
	EXPECT_NE(UI::WidgetRegistry::Find("scroll-view"), nullptr);

	EXPECT_EQ(UI::WidgetRegistry::Find("panel"), nullptr);
	EXPECT_EQ(UI::WidgetRegistry::Find(""), nullptr);

	UI::WidgetRegistry::Shutdown();
	EXPECT_NE(UI::WidgetRegistry::Find("button"), nullptr);
}

TEST_F(UIWidgetRegistryFixture, BuiltInNeverStored) {
	EXPECT_NE(UI::WidgetRegistry::Find("button"), nullptr);
	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(FakeOwner()), 0u);

	const UI::WidgetDesc* before = UI::WidgetRegistry::Find("button");
	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"button", &MakeOverride, {}, {}}, FakeOwner()));
	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(FakeOwner()), 1u);
	EXPECT_EQ(UI::WidgetRegistry::Find("button"), before);
}

TEST_F(UIWidgetRegistryFixture, DeclaredClassesMatchFactory) {
	struct Case {
		const char* tag;
		const char* declaration;
	};
	for (const Case& probe : {Case{"button", "slt-button"}, Case{"checkbox", "slt-checkbox"},
							  Case{"slider", "slt-slider"}, Case{"scroll-view", "slt-scroll-root"}}) {
		const UI::WidgetDesc* desc = UI::WidgetRegistry::Find(probe.tag);
		ASSERT_NE(desc, nullptr) << probe.tag;
		ASSERT_EQ(desc->builtInClasses.size(), 1u) << probe.tag;
		EXPECT_EQ(desc->builtInClasses[0], probe.declaration) << probe.tag;

		MockFontSystem fonts;
		UI::Context ctx(Desc(), &fonts);
		UIInstantiateDesc instantiate;
		UI::WidgetAttributeValue none[1]{};
		UI::WidgetResult built = desc->factory(ctx, ctx.GetRoot(), UI::WidgetArgs{instantiate, std::span<const UI::WidgetAttributeValue>(none, 0)});
		ASSERT_TRUE(ctx.IsAlive(built.element)) << probe.tag;
		EXPECT_TRUE(ctx.HasClass(built.element, StringHash(probe.declaration)));
	}
}

TEST_F(UIWidgetRegistryFixture, AttributeLiteralsAreValidatedAgainstDeclaredType) {
	using Type = UI::WidgetAttributeType;

	EXPECT_TRUE(UI::WidgetRegistry::IsValidLiteral(Type::String, ""));
	EXPECT_TRUE(UI::WidgetRegistry::IsValidLiteral(Type::String, "anything at all"));

	EXPECT_TRUE(UI::WidgetRegistry::IsValidLiteral(Type::Bool, "true"));
	EXPECT_TRUE(UI::WidgetRegistry::IsValidLiteral(Type::Bool, "false"));
	EXPECT_FALSE(UI::WidgetRegistry::IsValidLiteral(Type::Bool, "True"));
	EXPECT_FALSE(UI::WidgetRegistry::IsValidLiteral(Type::Bool, "1"));
	EXPECT_FALSE(UI::WidgetRegistry::IsValidLiteral(Type::Bool, ""));

	EXPECT_TRUE(UI::WidgetRegistry::IsValidLiteral(Type::Number, "12"));
	EXPECT_TRUE(UI::WidgetRegistry::IsValidLiteral(Type::Number, "-0.75"));
	EXPECT_FALSE(UI::WidgetRegistry::IsValidLiteral(Type::Number, "12abc"));
	EXPECT_FALSE(UI::WidgetRegistry::IsValidLiteral(Type::Number, "12px"));
	EXPECT_FALSE(UI::WidgetRegistry::IsValidLiteral(Type::Number, ""));
}

TEST_F(UIWidgetRegistryFixture, OmittedAttributeIsWidgetsDefaultNotZero) {
	UIInstantiateDesc instantiate;
	const UI::WidgetAttributeValue authored[] = {{"caption", "hello"}};
	UI::WidgetArgs args{instantiate, authored};

	EXPECT_EQ(args.Get("caption"), "hello");
	EXPECT_EQ(args.Get("missing", "fallback"), "fallback");
	EXPECT_EQ(args.GetBool("enabled", true), true);
	EXPECT_EQ(args.GetNumber("amount", 7.5), 7.5);
}

TEST_F(UIWidgetRegistryFixture, RegisteringAddsAndReplacesAndRefusesUnusable) {
	EXPECT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"probe", &MakeProbe, ProbeAttributes, ProbeClasses}, FakeOwner()));
	const UI::WidgetDesc* found = UI::WidgetRegistry::Find("probe");
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->attributes.size(), 3u);

	EXPECT_FALSE(UI::WidgetRegistry::Register(UI::WidgetDesc{"nofactory", nullptr, {}, {}}, FakeOwner()));
	EXPECT_EQ(UI::WidgetRegistry::Find("nofactory"), nullptr);
	EXPECT_FALSE(UI::WidgetRegistry::Register(UI::WidgetDesc{"", &MakeProbe, {}, {}}, FakeOwner()));

	EXPECT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"probe", &MakeOverride, {}, {}}, FakeOwner()));
	EXPECT_EQ(UI::WidgetRegistry::Find("probe")->factory, &MakeOverride);
	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(FakeOwner()), 1u);
}

TEST_F(UIWidgetRegistryFixture, SweepRestoresBuiltIn) {
	const UI::WidgetDesc* original = UI::WidgetRegistry::Find("button");
	ASSERT_NE(original, nullptr);
	UI::WidgetFactoryFn builtInFactory = original->factory;

	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"button", &MakeOverride, {}, {}}, FakeOwner()));
	EXPECT_EQ(UI::WidgetRegistry::Find("button")->factory, &MakeOverride);

	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(FakeOwner()), 1u);
	const UI::WidgetDesc* restored = UI::WidgetRegistry::Find("button");
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->factory, builtInFactory);
	ASSERT_EQ(restored->attributes.size(), 1u);
	EXPECT_EQ(restored->attributes[0].name, "text");
}

TEST_F(UIWidgetRegistryFixture, SweepTakesOwnerRows) {
	static int otherOwnerStorage = 0;
	const void* otherOwner = &otherOwnerStorage;

	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"probe", &MakeProbe, ProbeAttributes, ProbeClasses}, FakeOwner()));
	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"button", &MakeOverride, {}, {}}, FakeOwner()));
	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"other", &MakeOverride, {}, {}}, otherOwner));

	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(FakeOwner()), 2u);
	EXPECT_EQ(UI::WidgetRegistry::Find("probe"), nullptr);
	EXPECT_NE(UI::WidgetRegistry::Find("button"), nullptr);
	EXPECT_NE(UI::WidgetRegistry::Find("other"), nullptr);
	EXPECT_NE(UI::WidgetRegistry::Find("checkbox"), nullptr);

	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(nullptr), 0u);
	EXPECT_NE(UI::WidgetRegistry::Find("button"), nullptr);
	EXPECT_NE(UI::WidgetRegistry::Find("slider"), nullptr);

	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(otherOwner), 1u);
	EXPECT_EQ(UI::WidgetRegistry::Find("other"), nullptr);
}

TEST_F(UIWidgetRegistryFixture, HostReclaimSurvivesSweep) {
	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"gauge", &MakeProbe, ProbeAttributes, ProbeClasses}, nullptr));
	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"gauge", &MakeOverride, {}, {}}, FakeOwner()));
	EXPECT_EQ(UI::WidgetRegistry::Find("gauge")->factory, &MakeOverride);

	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"gauge", &MakeProbe, ProbeAttributes, ProbeClasses}, nullptr));
	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(FakeOwner()), 0u);
	ASSERT_NE(UI::WidgetRegistry::Find("gauge"), nullptr);
	EXPECT_EQ(UI::WidgetRegistry::Find("gauge")->factory, &MakeProbe);
}

TEST_F(UIWidgetRegistryFixture, ModuleReturnsHostWidget) {
	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"gauge", &MakeProbe, ProbeAttributes, ProbeClasses}, nullptr));
	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"gauge", &MakeOverride, {}, {}}, FakeOwner()));

	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(FakeOwner()), 1u);
	const UI::WidgetDesc* restored = UI::WidgetRegistry::Find("gauge");
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->factory, &MakeProbe);
	EXPECT_EQ(restored->attributes.size(), 3u);
}
