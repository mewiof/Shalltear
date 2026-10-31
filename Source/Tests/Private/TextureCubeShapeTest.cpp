#include "TestsPCH.h"

#include "ShadingHarness.h"
#include "TestImages.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;
using ShadingTest::WriteRadianceHDR;

namespace {

	class TextureCubeShapeTest : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_cubeshape_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
		}

		void TearDown() override {
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		TextureAsset* Import(const std::filesystem::path& file, JSONValue settings = JSONValue::MakeObject()) {
			TextureAssetImporter importer;
			return static_cast<TextureAsset*>(importer.ImportWithSettings(Path(file.generic_string()), Path("project://Strip"), settings));
		}
	};

} // anonymous namespace

TEST_F(TextureCubeShapeTest, LinearStripImportsAsCube) {
	constexpr i32 side = 320;
	WriteRadianceHDR(_root / "Strip.hdr", side * 6, side, [](f32, f32) { return Vec3(0.5f, 0.5f, 0.5f); });
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("convolveIBL", JSONValue::MakeBool(false));
	settings.AddMember("compression", JSONValue::MakeString("none"));
	TextureAsset* texture = Import(_root / "Strip.hdr", std::move(settings));
	ASSERT_NE(texture, nullptr);
	EXPECT_TRUE(texture->isCube);
	EXPECT_EQ(texture->size.x, side);
	EXPECT_EQ(texture->size.y, side);
	Delete(texture);
}

TEST_F(TextureCubeShapeTest, LdrStripStaysTexture) {
	u8 image[96 * 16 * 4] = {};
	TestImages::WriteTga(_root / "Sheet.tga", 96, 16, image);
	TextureAsset* texture = Import(_root / "Sheet.tga");
	ASSERT_NE(texture, nullptr);
	EXPECT_FALSE(texture->isCube);
	EXPECT_EQ(texture->size.x, 96);
	Delete(texture);
}

TEST_F(TextureCubeShapeTest, UnconvolvedCubeHasOneLevel) {
	WriteRadianceHDR(_root / "Sky.hdr", 128, 64, [](f32 u, f32) { return Vec3(0.2f + u, 0.5f, 0.8f); });
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("convolveIBL", JSONValue::MakeBool(false));
	TextureAsset* texture = Import(_root / "Sky.hdr", std::move(settings));
	ASSERT_NE(texture, nullptr);
	ASSERT_TRUE(texture->isCube);
	ASSERT_EQ(texture->size.x, 32);
	EXPECT_EQ(texture->storedMipLevels, 1u);
	EXPECT_EQ(texture->pixels.size(), static_cast<size_t>(RHI::FormatLevelBytes(texture->format, {32, 32}) * 6));

	PakStringPool strings;
	Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
	{
		CookWriter descriptorWriter(descriptor, strings);
		CookWriter payloadWriter(payload, strings);
		ASSERT_TRUE(texture->Cook(descriptorWriter, payloadWriter));
	}
	TextureAsset restored;
	CookReader descriptorReader(descriptor.data(), descriptor.size());
	CookReader payloadReader(payload.data(), payload.size());
	EXPECT_TRUE(restored.LoadCooked(descriptorReader, payloadReader));
	EXPECT_TRUE(restored.isCube);
	EXPECT_EQ(restored.storedMipLevels, 1u);
	Delete(texture);
}
