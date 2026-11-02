#include "TestsPCH.h"

#include "TestImages.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/SpriteAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <filesystem>
#include <vector>

using namespace Shalltear;

namespace {

	struct SpriteRow {
		const char* id;
		const char* name;
		RectI rect;
		SpriteBorder border{};
	};

	class SpriteAssetFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_sprite_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			std::vector<u8> pixels(16 * 8 * 4, 255);
			TestImages::WriteTga(_root / "Atlas.tga", 16, 8, pixels.data());
			ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(AtlasPath()).IsValid());
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static Path AtlasPath() { return Path("project://Atlas.tga"); }

		static void WriteSprites(std::initializer_list<SpriteRow> rows) {
			JSONValue list = JSONValue::MakeArray();
			for (const SpriteRow& row : rows) {
				JSONValue entry = JSONValue::MakeObject();
				if (row.id[0] != '\0') {
					entry.AddMember("id", JSONValue::MakeString(row.id));
				}
				entry.AddMember("name", JSONValue::MakeString(row.name));
				JSONValue rect = JSONValue::MakeArray();
				for (i32 value : {row.rect.position.x, row.rect.position.y, row.rect.size.x, row.rect.size.y}) {
					rect.Push(JSONValue::MakeNumber(value));
				}
				entry.AddMember("rect", rect);
				JSONValue border = JSONValue::MakeArray();
				for (i32 value : {row.border.left, row.border.top, row.border.right, row.border.bottom}) {
					border.Push(JSONValue::MakeNumber(value));
				}
				entry.AddMember("border", border);
				list.Push(entry);
			}
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("compression", JSONValue::MakeString("none"));
			settings.AddMember("sprites", list);
			ASSERT_TRUE(AssetDatabase::WriteImporterSettings(AtlasPath(), settings));
		}

		static Guid AtlasGuid() { return AssetDatabase::LookupGuid(AtlasPath()); }

		static AssetProduct ProductOf(std::string_view key) {
			Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
			AssetDatabase::GetProducts(AtlasGuid(), products);
			for (const AssetProduct& product : products) {
				if (product.key == key) {
					return product;
				}
			}
			return {};
		}

		static SpriteAsset* LoadSprite(const Guid& guid) {
			Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
			if (!loaded || loaded->GetAssetTypeId() != SpriteAsset::GetReflectType()->name) {
				if (loaded) {
					AssetDatabase::UnloadAsset(loaded);
				}
				return nullptr;
			}
			return static_cast<SpriteAsset*>(loaded);
		}

		static TextureAsset* ImportAtlas() {
			Asset* loaded = AssetDatabase::LoadAsset(AtlasPath());
			return loaded && loaded->GetAssetTypeId() == TextureAsset::GetReflectType()->name ? static_cast<TextureAsset*>(loaded) : nullptr;
		}
	};

} // anonymous namespace

TEST_F(SpriteAssetFixture, SpritesBecomeNamedProducts) {
	WriteSprites({{"a1", "Heart", RectI(0, 0, 8, 8), {2, 2, 2, 2}}, {"", "Star", RectI(8, 0, 8, 4)}});
	TextureAsset* atlas = ImportAtlas();
	ASSERT_NE(atlas, nullptr);
	ASSERT_EQ(atlas->sprites.size(), 2u);

	const AssetProduct heart = ProductOf("Sprites/a1");
	const AssetProduct star = ProductOf("Sprites/Star");
	ASSERT_TRUE(heart.guid.IsValid());
	ASSERT_TRUE(star.guid.IsValid());
	EXPECT_EQ(heart.name, "Heart");
	EXPECT_EQ(heart.type, SpriteAsset::GetReflectType()->name);

	SpriteAsset* sprite = LoadSprite(star.guid);
	ASSERT_NE(sprite, nullptr);
	EXPECT_EQ(sprite->GetTexture(), atlas);
	ASSERT_NE(sprite->GetDefinition(), nullptr);
	EXPECT_EQ(sprite->GetDefinition()->rect, RectI(8, 0, 8, 4));

	SpriteAsset* bordered = LoadSprite(heart.guid);
	ASSERT_NE(bordered, nullptr);
	ASSERT_NE(bordered->GetDefinition(), nullptr);
	EXPECT_EQ(bordered->GetDefinition()->border, (SpriteBorder{2, 2, 2, 2}));

	AssetDatabase::UnloadAsset(bordered);
	AssetDatabase::UnloadAsset(sprite);
	AssetDatabase::UnloadAsset(atlas);
}

TEST_F(SpriteAssetFixture, RenameKeepsGuid) {
	WriteSprites({{"a1", "Heart", RectI(0, 0, 8, 8)}});
	TextureAsset* atlas = ImportAtlas();
	ASSERT_NE(atlas, nullptr);
	const Guid before = ProductOf("Sprites/a1").guid;
	AssetDatabase::UnloadAsset(atlas);
	AssetDatabase::CollectGarbage();

	WriteSprites({{"a1", "Life", RectI(0, 0, 8, 8)}});
	ASSERT_TRUE(AssetDatabase::ReimportAsset(AtlasPath()));

	const AssetProduct after = ProductOf("Sprites/a1");
	EXPECT_EQ(after.guid, before);
	EXPECT_EQ(after.name, "Life");
}

TEST_F(SpriteAssetFixture, HeldSpriteSeesEditedRect) {
	WriteSprites({{"a1", "Heart", RectI(0, 0, 8, 8)}});
	TextureAsset* atlas = ImportAtlas();
	ASSERT_NE(atlas, nullptr);
	SpriteAsset* sprite = LoadSprite(ProductOf("Sprites/a1").guid);
	ASSERT_NE(sprite, nullptr);
	AssetDatabase::UnloadAsset(atlas);
	ASSERT_EQ(sprite->GetTexture(), atlas);

	WriteSprites({{"a1", "Heart", RectI(4, 2, 6, 6), {1, 1, 1, 1}}});
	ASSERT_TRUE(AssetDatabase::ReimportAsset(AtlasPath()));

	ASSERT_NE(sprite->GetDefinition(), nullptr);
	EXPECT_EQ(sprite->GetDefinition()->rect, RectI(4, 2, 6, 6));
	EXPECT_EQ(sprite->GetDefinition()->border, (SpriteBorder{1, 1, 1, 1}));
	AssetDatabase::UnloadAsset(sprite);
}

TEST_F(SpriteAssetFixture, RemovedSpriteDefinesNothing) {
	WriteSprites({{"a1", "Heart", RectI(0, 0, 8, 8)}, {"b2", "Star", RectI(8, 0, 8, 8)}});
	TextureAsset* atlas = ImportAtlas();
	ASSERT_NE(atlas, nullptr);
	SpriteAsset* sprite = LoadSprite(ProductOf("Sprites/b2").guid);
	ASSERT_NE(sprite, nullptr);
	ASSERT_NE(sprite->GetDefinition(), nullptr);
	AssetDatabase::UnloadAsset(atlas);

	WriteSprites({{"a1", "Heart", RectI(0, 0, 8, 8)}});
	ASSERT_TRUE(AssetDatabase::ReimportAsset(AtlasPath()));

	EXPECT_EQ(sprite->GetDefinition(), nullptr);
	ASSERT_NE(sprite->GetTexture(), nullptr);
	EXPECT_NE(sprite->GetTexture()->FindSprite("a1"), nullptr);
	AssetDatabase::UnloadAsset(sprite);
}

TEST_F(SpriteAssetFixture, InvalidRowsSkipped) {
	WriteSprites({{"ok", "Fine", RectI(0, 0, 4, 4)},
				  {"out", "Outside", RectI(12, 0, 8, 8)},
				  {"empty", "Empty", RectI(0, 0, 0, 4)},
				  {"fat", "Fat", RectI(0, 0, 4, 4), {3, 0, 2, 0}},
				  {"ok", "Again", RectI(4, 4, 4, 4)}});
	TextureAsset* atlas = ImportAtlas();
	ASSERT_NE(atlas, nullptr);
	ASSERT_EQ(atlas->sprites.size(), 1u);
	EXPECT_EQ(atlas->sprites[0].name, "Fine");
	EXPECT_TRUE(ProductOf("Sprites/ok").guid.IsValid());
	EXPECT_FALSE(ProductOf("Sprites/out").guid.IsValid());
	EXPECT_FALSE(ProductOf("Sprites/fat").guid.IsValid());
	AssetDatabase::UnloadAsset(atlas);
}

TEST_F(SpriteAssetFixture, SpriteHoldsTextureResident) {
	WriteSprites({{"a1", "Heart", RectI(0, 0, 8, 8)}});
	TextureAsset* atlas = ImportAtlas();
	ASSERT_NE(atlas, nullptr);
	const Guid spriteGuid = ProductOf("Sprites/a1").guid;
	AssetDatabase::UnloadAsset(atlas);
	AssetDatabase::CollectGarbage();
	ASSERT_EQ(AssetDatabase::FindLoadedByGuid(AtlasGuid()), nullptr);

	SpriteAsset* sprite = LoadSprite(spriteGuid);
	ASSERT_NE(sprite, nullptr);
	ASSERT_NE(sprite->GetTexture(), nullptr);
	AssetDatabase::CollectGarbage();
	EXPECT_NE(AssetDatabase::FindLoadedByGuid(AtlasGuid()), nullptr);

	AssetDatabase::UnloadAsset(sprite);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(spriteGuid), nullptr);
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(AtlasGuid()), nullptr);
}

TEST_F(SpriteAssetFixture, TextureWithoutSpritesHasNoProducts) {
	TextureAsset* atlas = ImportAtlas();
	ASSERT_NE(atlas, nullptr);
	EXPECT_TRUE(atlas->sprites.empty());
	Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
	AssetDatabase::GetProducts(AtlasGuid(), products);
	EXPECT_TRUE(products.empty());
	AssetDatabase::UnloadAsset(atlas);
}
