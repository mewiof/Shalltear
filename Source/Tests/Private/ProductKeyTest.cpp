#include "TestsPCH.h"

#include "ModelImport.h"

using namespace Shalltear;

namespace {

	std::vector<str> KeysFor(std::string_view kind, std::initializer_list<std::string_view> names) {
		Vector<str> taken{GetMemoryResource(MemTag::Asset)};
		std::vector<str> keys;
		for (std::string_view name : names) {
			keys.push_back(ProductKey(kind, name, taken));
		}
		return keys;
	}

} // namespace

TEST(ProductKeyTest, UnambiguousFileGetsPlainNames) {
	EXPECT_EQ(KeysFor("Meshes", {"Body", "Visor"}), (std::vector<str>{"Meshes/Body", "Meshes/Visor"}));
}

TEST(ProductKeyTest, AReorderMovesNoKey) {
	std::vector<str> before = KeysFor("Meshes", {"Body", "Visor"});
	std::vector<str> after = KeysFor("Meshes", {"Visor", "Body"});
	ASSERT_EQ(before.size(), 2u);
	ASSERT_EQ(after.size(), 2u);
	EXPECT_EQ(before[0], after[1]);
	EXPECT_EQ(before[1], after[0]);
}

TEST(ProductKeyTest, ARenameIsANewKey) {
	EXPECT_NE(KeysFor("Meshes", {"Body"})[0], KeysFor("Meshes", {"Torso"})[0]);
}

TEST(ProductKeyTest, DuplicateNameTakesNextOrdinal) {
	EXPECT_EQ(KeysFor("Materials", {"Metal", "Metal", "Metal"}),
			  (std::vector<str>{"Materials/Metal", "Materials/Metal#2", "Materials/Metal#3"}));
}

TEST(ProductKeyTest, AuthoredHashCannotCollideWithOrdinal) {
	std::vector<str> keys = KeysFor("Meshes", {"Barrel#2", "Barrel", "Barrel"});
	ASSERT_EQ(keys.size(), 3u);
	EXPECT_EQ(keys[0], "Meshes/Barrel_2");
	EXPECT_EQ(keys[1], "Meshes/Barrel");
	EXPECT_EQ(keys[2], "Meshes/Barrel#2");
	EXPECT_NE(keys[0], keys[2]);
}

TEST(ProductKeyTest, ANameNeedingSanitizingIsSanitized) {
	EXPECT_EQ(KeysFor("Materials", {"Chrome / Trim (worn)"})[0], "Materials/Chrome___Trim__worn_");
}

TEST(ProductKeyTest, EmptyNameStillAddressesDistinctProduct) {
	std::vector<str> keys = KeysFor("Meshes", {"", ""});
	ASSERT_EQ(keys.size(), 2u);
	EXPECT_NE(keys[0], keys[1]);
	EXPECT_TRUE(keys[0].starts_with("Meshes/unnamed"));
}

TEST(ProductKeyTest, KindsWithSameNameDontCollide) {
	Vector<str> taken{GetMemoryResource(MemTag::Asset)};
	str mesh = ProductKey(ProductKindMeshes, "Glass", taken);
	str material = ProductKey(ProductKindMaterials, "Glass", taken);
	EXPECT_EQ(mesh, "Meshes/Glass");
	EXPECT_EQ(material, "Materials/Glass");
}
