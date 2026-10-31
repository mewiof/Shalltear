#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UIDocument.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr const char* V1_DLL = "ShalltearHotReloadModuleV1.dll";
	constexpr const char* V2_DLL = "ShalltearHotReloadModuleV2.dll";

	constexpr const char* PROBE_TAG = "hot-reload-gauge";
	constexpr const char* COVERED_TAG = "button";
	constexpr u64 V1_MAGIC = 1;
	constexpr u64 V2_MAGIC = 2;

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
	}

	class UIWidgetModuleFixture : public ::testing::Test {
	protected:
		ModuleHandle _module;

		void SetUp() override {
			UI::WidgetRegistry::Shutdown();
			TypeRegistry::InitializePrimitives();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			_module = ModuleSystem::Load(Path(V1_DLL));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
		}

		void TearDown() override {
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			UI::WidgetRegistry::Shutdown();
		}

		const void* ModuleBase() const { return ModuleSystem::GetNativeHandle(_module); }

		static u64 MarkOf(std::string_view body, UI::Context& ctx) {
			UIDocumentAsset document;
			str error;
			if (!document.Parse(str("<ui version=\"1\">") + str(body) + str("</ui>"), error)) {
				ADD_FAILURE() << "document refused: " << error;
				return 0;
			}
			UI::ElementHandle element = document.Instantiate(ctx, ctx.GetRoot());
			const UI::Element* built = ctx.GetElement(element);
			return built ? built->userTag : 0;
		}
	};

} // anonymous namespace

TEST_F(UIWidgetModuleFixture, ModuleRegistersWidgetThroughOneEntryPoint) {
	const UI::WidgetDesc* gauge = UI::WidgetRegistry::Find(PROBE_TAG);
	ASSERT_NE(gauge, nullptr);
	ASSERT_EQ(gauge->attributes.size(), 2u);
	EXPECT_EQ(gauge->attributes[0].name, "label");
	EXPECT_EQ(gauge->attributes[1].type, UI::WidgetAttributeType::Number);
	ASSERT_EQ(gauge->builtInClasses.size(), 1u);
	EXPECT_EQ(gauge->builtInClasses[0], "probe-gauge");

	EXPECT_NE(UI::WidgetRegistry::Find("checkbox"), nullptr);
	EXPECT_NE(UI::WidgetRegistry::Find("slider"), nullptr);
}

TEST_F(UIWidgetModuleFixture, DocumentUsesModuleWidget) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);

	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(
		R"(<ui version="1"><hot-reload-gauge id="hp" label="Health" ratio="0.5"><panel id="inner"/></hot-reload-gauge></ui>)",
		error))
		<< error;

	UI::ElementHandle root = document.Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(root));
	EXPECT_EQ(ctx.GetElement(root)->userTag, V1_MAGIC);
	EXPECT_TRUE(ctx.HasClass(root, "probe-gauge"_hash));
	EXPECT_EQ(ctx.GetElement(root)->id, "hp"_hash);

	UI::ElementHandle fill = ctx.MakeHandle(ctx.GetElement(root)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(fill));
	EXPECT_EQ(ctx.GetElement(fill)->userTag, 50u);

	UI::ElementHandle inner = ctx.MakeHandle(ctx.GetElement(fill)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(inner));
	EXPECT_EQ(ctx.GetElement(inner)->id, "inner"_hash);

	UIDocumentAsset refused;
	str refusal;
	EXPECT_FALSE(refused.Parse(R"(<ui version="1"><hot-reload-gauge ratio="lots"/></ui>)", refusal));
	EXPECT_NE(refusal.find("lots"), str::npos) << refusal;
	EXPECT_FALSE(refused.Parse(R"(<ui version="1"><hot-reload-gauge tint="red"/></ui>)", refusal));
	EXPECT_NE(refusal.find("tint"), str::npos) << refusal;
}

TEST_F(UIWidgetModuleFixture, ReloadingReplacesRegistrationWithNewImages) {
	{
		MockFontSystem fonts;
		UI::Context ctx(Desc(), &fonts);
		EXPECT_EQ(MarkOf(R"(<hot-reload-gauge/>)", ctx), V1_MAGIC);
	}

	Path path{V2_DLL};
	ASSERT_TRUE(ModuleSystem::Reload(_module, &path));

	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	EXPECT_EQ(MarkOf(R"(<hot-reload-gauge/>)", ctx), V2_MAGIC);
	EXPECT_EQ(MarkOf(R"(<button text="x"/>)", ctx), V2_MAGIC);

	const UI::WidgetDesc* gauge = UI::WidgetRegistry::Find(PROBE_TAG);
	ASSERT_NE(gauge, nullptr);
	EXPECT_EQ(gauge->attributes.size(), 2u);
}

TEST_F(UIWidgetModuleFixture, ReloadCyclesLeaveNoResidue) {
	for (int cycle = 0; cycle < 3; ++cycle) {
		Path v2{V2_DLL};
		ASSERT_TRUE(ModuleSystem::Reload(_module, &v2)) << "cycle " << cycle;
		Path v1{V1_DLL};
		ASSERT_TRUE(ModuleSystem::Reload(_module, &v1)) << "cycle " << cycle;

		MockFontSystem fonts;
		UI::Context ctx(Desc(), &fonts);
		EXPECT_EQ(MarkOf(R"(<hot-reload-gauge/>)", ctx), V1_MAGIC) << "cycle " << cycle;
	}

	EXPECT_EQ(UI::WidgetRegistry::UnregisterOwnedBy(ModuleBase()), 2u);
}

TEST_F(UIWidgetModuleFixture, UnloadRestoresCoveredBuiltIn) {
	{
		MockFontSystem fonts;
		UI::Context ctx(Desc(), &fonts);
		EXPECT_EQ(MarkOf(R"(<button text="Pause"/>)", ctx), V1_MAGIC);
	}

	ASSERT_TRUE(ModuleSystem::Unload(_module));

	EXPECT_EQ(UI::WidgetRegistry::Find(PROBE_TAG), nullptr);

	const UI::WidgetDesc* button = UI::WidgetRegistry::Find(COVERED_TAG);
	ASSERT_NE(button, nullptr);
	ASSERT_EQ(button->builtInClasses.size(), 1u);
	EXPECT_EQ(button->builtInClasses[0], "slt-button");

	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(R"(<ui version="1"><button text="Pause"/></ui>)", error)) << error;
	UI::ElementHandle element = document.Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(element));
	EXPECT_EQ(ctx.GetElement(element)->userTag, 0u);
	EXPECT_TRUE(ctx.HasClass(element, "slt-button"_hash));

	str refusal;
	EXPECT_FALSE(document.Parse(R"(<ui version="1"><hot-reload-gauge ratio="0.5"/></ui>)", refusal));
	EXPECT_NE(refusal.find("ratio"), str::npos) << refusal;
}

TEST_F(UIWidgetModuleFixture, DocumentInstantiatedBeforeUnloadKeepsElementsAfterwards) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(R"(<ui version="1"><hot-reload-gauge id="hp" ratio="0.25"/></ui>)", error)) << error;
	UI::ElementHandle live = document.Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(live));

	ASSERT_TRUE(ModuleSystem::Unload(_module));

	EXPECT_TRUE(ctx.IsAlive(live));
	EXPECT_EQ(ctx.GetElement(live)->id, "hp"_hash);
	EXPECT_TRUE(ctx.HasClass(live, "probe-gauge"_hash));
	ctx.Update();
	ctx.DestroyElement(live);
	EXPECT_FALSE(ctx.IsAlive(live));
}
