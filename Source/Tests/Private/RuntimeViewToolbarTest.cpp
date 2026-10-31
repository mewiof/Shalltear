#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/RuntimeViewToolbar.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	class RuntimeViewToolbarFixture : public ::testing::Test {
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
			RuntimeViewToolbar::Terminate();
		}

		static void Tick(UI::Context& context) {
			PanelManager::TickAll(1.0f / 60.0f);
			context.Update();
		}
	};

} // namespace

TEST_F(RuntimeViewToolbarFixture, PackageToggleFollowsOwner) {
	int moduleImage = 0;
	bool muted = false;
	RuntimeViewToggle mute;
	mute.icon = EditorIcons::Glyph::MuteAudio;
	mute.label = "Mute";
	mute.tooltip = "Mute Audio";
	mute.isOn = [&muted]() { return muted; };
	mute.set = [&muted](bool on) { muted = on; };
	ASSERT_TRUE(RuntimeViewToolbar::RegisterToggle(StringHash("Mute"), mute, &moduleImage));

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("RuntimeViewPanel")));
	ASSERT_TRUE(panel.IsValid());
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	const auto button = [&](const EditorIcons::Icon& icon) {
		return UITest::ControlWearing(context, root, EditorIcons::GetGlyph(icon), EditorTheme::Class::ToolButton);
	};
	ASSERT_TRUE(button(EditorIcons::Glyph::MuteAudio).IsValid());
	EXPECT_FALSE(context.HasClass(button(EditorIcons::Glyph::MuteAudio), EditorTheme::Class::ToolButtonOn));

	bool looping = false;
	RuntimeViewToggle loop;
	loop.icon = EditorIcons::Glyph::Loop;
	loop.label = "Loop";
	loop.isOn = [&looping]() { return looping; };
	loop.set = [&looping](bool on) { looping = on; };
	ASSERT_TRUE(RuntimeViewToolbar::RegisterToggle(StringHash("Loop"), loop));
	Tick(context);
	EXPECT_TRUE(button(EditorIcons::Glyph::Loop).IsValid());

	UITest::ClickElement(context, button(EditorIcons::Glyph::MuteAudio));
	EXPECT_TRUE(muted);
	Tick(context);
	EXPECT_TRUE(context.HasClass(button(EditorIcons::Glyph::MuteAudio), EditorTheme::Class::ToolButtonOn));

	muted = false;
	Tick(context);
	EXPECT_FALSE(context.HasClass(button(EditorIcons::Glyph::MuteAudio), EditorTheme::Class::ToolButtonOn));

	EXPECT_EQ(RuntimeViewToolbar::UnregisterOwnedBy(&moduleImage), 1u);
	Tick(context);
	EXPECT_FALSE(button(EditorIcons::Glyph::MuteAudio).IsValid());
	EXPECT_TRUE(button(EditorIcons::Glyph::Loop).IsValid());
	PanelManager::ClosePanel(panel);
}
