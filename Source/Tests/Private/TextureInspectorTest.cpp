#include "TestsPCH.h"

#include "TestImages.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <vector>

using namespace Shalltear;

namespace {

	class TextureInspectorFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		Handle<EditorPanel> _inspector;
		Asset* _texture = nullptr;
		Guid _guid;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() / ("shalltear_textureinspector_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorAssetOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();
			PropertyDrawers::Initialize();

			std::vector<u8> rgba(8 * 8 * 4, 255);
			TestImages::WriteTga(_root / "Assets" / "Bricks.tga", 8, 8, rgba.data());
			const Path path("project://Assets/Bricks.tga");
			_guid = AssetDatabase::EnsureAssetIdentity(path);
			ASSERT_TRUE(_guid.IsValid());
			JSONValue sprite = JSONValue::MakeObject();
			sprite.AddMember("id", JSONValue::MakeString("b1"));
			sprite.AddMember("name", JSONValue::MakeString("Brick"));
			JSONValue rect = JSONValue::MakeArray();
			for (i32 value : {0, 0, 4, 4}) {
				rect.Push(JSONValue::MakeNumber(value));
			}
			sprite.AddMember("rect", std::move(rect));
			JSONValue sprites = JSONValue::MakeArray();
			sprites.Push(std::move(sprite));
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("compression", JSONValue::MakeString("none"));
			settings.AddMember("sprites", std::move(sprites));
			ASSERT_TRUE(AssetDatabase::WriteImporterSettings(path, settings));
			_texture = AssetDatabase::LoadAssetByGuid(_guid);
			ASSERT_NE(_texture, nullptr);
		}

		void TearDown() override {
			if (_texture) {
				AssetDatabase::UnloadAsset(_texture);
			}
			PanelManager::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			EditorAssetOps::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		TextureAsset& Texture() const { return *static_cast<TextureAsset*>(_texture); }

		void Build(UI::Context& context, UI::ElementHandle& root) {
			EditorSelection::SelectAsset(_guid);
			_inspector = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")));
			ASSERT_TRUE(_inspector.IsValid());
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			_inspector.Get()->BuildContent(context, root);
			context.Update();
		}

		void Rebuild(UI::Context& context, UI::ElementHandle root) {
			PanelManager::TickAll(1.0f / 60.0f);
			context.ClearChildren(root);
			_inspector.Get()->BuildContent(context, root);
			context.Update();
		}

		static UI::ElementHandle Row(UI::Context& context, UI::ElementHandle root, std::string_view label) {
			const UI::Element* text = context.GetElement(UITest::FindByText(context, root, label));
			return text ? context.MakeHandle(text->parent) : UI::ElementHandle{};
		}

		static UI::ElementHandle Button(UI::Context& context, UI::ElementHandle root, std::string_view label) {
			return Row(context, root, label);
		}

		static void Pick(UI::Context& context, UI::ElementHandle root, std::string_view label, u32 item) {
			UI::ElementHandle dropdown = UITest::FindByClass(context, Row(context, root, label), StringHash("slt-dropdown"), 0);
			ASSERT_TRUE(dropdown.IsValid()) << label;
			UITest::ClickElement(context, dropdown);
			ASSERT_EQ(context.GetOpenPopupCount(), 1u);
			UITest::ClickElement(context, UITest::NthChildOf(context, context.GetOpenPopup(0), item));
		}

		i64 SavedNumber(std::string_view key) const {
			const str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://Assets/Bricks.tga"))), MemTag::Editor);
			LinearAllocator arena(text.size() + 64, MemTag::Editor);
			const JSONValue root = JSONParse(text, arena);
			return root["importer"]["settings"][key].AsInt(-1);
		}

		str Saved(std::string_view key) const {
			const str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://Assets/Bricks.tga"))), MemTag::Editor);
			LinearAllocator arena(text.size() + 64, MemTag::Editor);
			const JSONValue root = JSONParse(text, arena);
			const JSONValue& value = root["importer"]["settings"][key];
			return value.IsArray() ? str("array") : str(value.AsString());
		}
	};

} // namespace

TEST_F(TextureInspectorFixture, FilterModeAppliesThroughMeta) {
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {500, 1200};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	Build(context, root);

	for (const char* label : {"Texture Type", "Texture Shape", "sRGB (Color Texture)", "Generate Mipmaps", "Wrap Mode", "Filter Mode", "Aniso Level",
							  "Compression"}) {
		EXPECT_TRUE(Row(context, root, label).IsValid()) << label;
	}
	EXPECT_TRUE(context.HasClass(Button(context, root, "Apply"), EditorTheme::Class::Disabled));
	EXPECT_TRUE(UITest::FindByClass(context, Row(context, root, "Aniso Level"), StringHash("slt-slider-knob"), 0).IsValid());

	Pick(context, root, "Filter Mode", 0);
	Rebuild(context, root);
	const UI::ElementHandle aniso = Row(context, root, "Aniso Level");
	EXPECT_FALSE(UITest::FindByClass(context, aniso, StringHash("slt-slider-knob"), 0).IsValid());
	EXPECT_TRUE(UITest::FindByText(context, aniso, "16").IsValid());
	EXPECT_FALSE(context.HasClass(Button(context, root, "Apply"), EditorTheme::Class::Disabled));

	UITest::ClickElement(context, Button(context, root, "Apply"));
	Rebuild(context, root);
	EXPECT_EQ(Saved("filter"), "point");
	EXPECT_EQ(Saved("compression"), "none");
	EXPECT_EQ(Saved("sprites"), "array");
	EXPECT_EQ(Texture().settings.filter, TextureFilter::Point);
	EXPECT_EQ(Texture().sprites.size(), 1u);
	EXPECT_TRUE(context.HasClass(Button(context, root, "Apply"), EditorTheme::Class::Disabled));
}

TEST_F(TextureInspectorFixture, AnisoLevelRevertsThenApplies) {
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {500, 1200};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	Build(context, root);

	auto knob = [&]() { return UITest::FindByClass(context, Row(context, root, "Aniso Level"), StringHash("slt-slider-knob"), 0); };
	ASSERT_TRUE(knob().IsValid());
	UITest::DragElementBy(context, knob(), -600, 0);
	EXPECT_FALSE(context.HasClass(Button(context, root, "Revert"), EditorTheme::Class::Disabled));
	UITest::ClickElement(context, Button(context, root, "Revert"));
	Rebuild(context, root);
	EXPECT_TRUE(context.HasClass(Button(context, root, "Apply"), EditorTheme::Class::Disabled));
	EXPECT_EQ(SavedNumber("anisoLevel"), -1);

	UITest::DragElementBy(context, knob(), -600, 0);
	UITest::ClickElement(context, Button(context, root, "Apply"));
	Rebuild(context, root);
	EXPECT_EQ(SavedNumber("anisoLevel"), 0);
	EXPECT_EQ(Texture().settings.anisoLevel, 0);
	EXPECT_EQ(Saved("filter"), "trilinear");
}

TEST_F(TextureInspectorFixture, ReimportElsewhereRefreshesDraft) {
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {500, 1200};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	Build(context, root);
	EXPECT_TRUE(UITest::FindByText(context, Row(context, root, "Texture Type"), "Default").IsValid());
	EXPECT_TRUE(Row(context, root, "sRGB (Color Texture)").IsValid());

	const Path path("project://Assets/Bricks.tga");
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("compression", JSONValue::MakeString("none"));
	settings.AddMember("textureType", JSONValue::MakeString("normalMap"));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(path, settings));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(path));
	ASSERT_EQ(Texture().settings.type, TextureType::NormalMap);

	Rebuild(context, root);
	EXPECT_TRUE(UITest::FindByText(context, Row(context, root, "Texture Type"), "Normal Map").IsValid());
	EXPECT_FALSE(Row(context, root, "sRGB (Color Texture)").IsValid());
	EXPECT_TRUE(context.HasClass(Button(context, root, "Apply"), EditorTheme::Class::Disabled));
}
