#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ShaderReflection.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>

using namespace Shalltear;

namespace {

	constexpr std::string_view RetiredStandardLit = "5f787b0c8a4e9a5d-03e3437f042bfc8d";
	constexpr std::string_view StandardLit = "5d0eee565c0eb506-d813bbcaa86ba3a8";

	class MaterialMigrationFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_matmig_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
		}

		void TearDown() override {
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Path Write(const char* name, const str& content) {
			std::filesystem::path file = _root / name;
			FileSystem::WriteTextFile(Path(file.generic_string()), content);
			return Path(file.generic_string());
		}

		str Read(const Path& file) { return FileSystem::ReadTextFile(file, MemTag::Asset); }

		static str V1Document(std::string_view shader, std::string_view floats, std::string_view textures = "{}") {
			return std::format(R"({{
	"shader": "{}",
	"floats": {},
	"colors": {{
		"baseColor": [ 0.25, 0.5, 0.75, 1 ]
	}},
	"textures": {}
}})",
							   shader, floats, textures);
		}
	};

	TEST_F(MaterialMigrationFixture, V1MaterialMigratesToLit) {
		Path file = Write("hand.mat", V1Document(RetiredStandardLit, R"({ "specular": 0.5, "shininess": 32 })",
												 R"({ "baseColorTexture": "1122334455667788-99aabbccddeeff00" })"));
		MaterialAsset material;
		ASSERT_TRUE(material.Import(file));

		EXPECT_EQ(material.shaderGuid, Guid::FromString(StandardLit));

		Color baseColor = material.GetColor(StringHash("baseColor"));
		EXPECT_NEAR(baseColor.r, 0.25f, 1e-6f);
		EXPECT_NEAR(baseColor.g, 0.5f, 1e-6f);
		EXPECT_NEAR(baseColor.b, 0.75f, 1e-6f);
		EXPECT_TRUE(material.HasTexture(StringHash("baseColorTexture")));

		EXPECT_NEAR(material.GetFloat(StringHash("roughness")), 0.8f, 1e-6f);

		EXPECT_TRUE(material.HasFloat(StringHash("metallic")));
		EXPECT_NEAR(material.GetFloat(StringHash("metallic")), 0.0f, 1e-6f);

		EXPECT_FALSE(material.HasFloat(StringHash("specular")));
		EXPECT_FALSE(material.HasFloat(StringHash("shininess")));
	}

	TEST_F(MaterialMigrationFixture, RoughnessInversionIsExactAcrossShininessRange) {
		struct Row {
			const char* shininess;
			f32 roughness;
			const char* why;
		};
		const Row rows[] = {
			{"8", 1.0f, "floor (fully rough)"},
			{"32", 0.8f, "v1 default"},
			{"68", 0.5f, "midpoint"},
			{"128", 0.0f, "ceiling (mirror)"},
			{"400", 0.0f, "above ceiling, clamped"},
			{"0", 1.0f, "below floor, clamped"},
		};
		for (const Row& row : rows) {
			Path file = Write("row.mat", V1Document(RetiredStandardLit,
													std::format(R"({{ "specular": 0.5, "shininess": {} }})", row.shininess)));
			MaterialAsset material;
			ASSERT_TRUE(material.Import(file)) << row.shininess;
			EXPECT_NEAR(material.GetFloat(StringHash("roughness")), row.roughness, 1e-6f)
				<< "shininess " << row.shininess << ", " << row.why;
		}
	}

	TEST_F(MaterialMigrationFixture, MissingShininessTakesV1Default) {
		Path file = Write("sparse.mat", V1Document(RetiredStandardLit, R"({ "specular": 0.5 })"));
		MaterialAsset material;
		ASSERT_TRUE(material.Import(file));
		EXPECT_NEAR(material.GetFloat(StringHash("roughness")), 0.8f, 1e-6f);
	}

	TEST_F(MaterialMigrationFixture, MaterialOnAnyOtherShaderIsUntouched) {
		const str before = V1Document("0123456789abcdef-fedcba9876543210", R"({ "specular": 0.25, "shininess": 64 })");
		Path file = Write("project.mat", before);
		MaterialAsset material;
		ASSERT_TRUE(material.Import(file));

		EXPECT_EQ(material.shaderGuid, Guid::FromString("0123456789abcdef-fedcba9876543210"));
		EXPECT_NEAR(material.GetFloat(StringHash("shininess")), 64.0f, 1e-6f);
		EXPECT_NEAR(material.GetFloat(StringHash("specular")), 0.25f, 1e-6f);
		EXPECT_FALSE(material.HasFloat(StringHash("roughness")));
		EXPECT_EQ(Read(file), before);
	}

	TEST_F(MaterialMigrationFixture, MigrationIsIdempotent) {
		Path file = Write("twice.mat", V1Document(RetiredStandardLit, R"({ "specular": 0.5, "shininess": 32 })"));

		MaterialAsset first;
		ASSERT_TRUE(first.Import(file));
		const str afterFirst = Read(file);
		EXPECT_NE(afterFirst.find(StandardLit), str::npos);
		EXPECT_EQ(afterFirst.find("shininess"), str::npos);
		EXPECT_EQ(afterFirst.find("specular"), str::npos);

		MaterialAsset second;
		ASSERT_TRUE(second.Import(file));
		EXPECT_EQ(Read(file), afterFirst);
		EXPECT_NEAR(second.GetFloat(StringHash("roughness")), 0.8f, 1e-6f);
		EXPECT_EQ(second.shaderGuid, Guid::FromString(StandardLit));
	}

	UniformBlockLayout LitBlock() {
		UniformBlockLayout layout;
		layout.set = 1;
		layout.binding = 0;
		layout.sizeBytes = 48;
		layout.members.push_back({"baseColor", UniformMemberType::Vec4, 0, 16});
		layout.members.push_back({"metallic", UniformMemberType::Float, 16, 4});
		layout.members.push_back({"roughness", UniformMemberType::Float, 20, 4});
		layout.members.push_back({"reflectance", UniformMemberType::Float, 24, 4});
		layout.members.push_back({"normalScale", UniformMemberType::Float, 28, 4});
		layout.members.push_back({"emissive", UniformMemberType::Vec3, 32, 12});
		layout.members.push_back({"occlusionStrength", UniformMemberType::Float, 44, 4});
		return layout;
	}

	Vector<ShaderMemberDefault> LitDefaults() {
		Vector<ShaderMemberDefault> defaults{GetMemoryResource(MemTag::Asset)};
		defaults.push_back({"baseColor", {1.0f, 1.0f, 1.0f, 1.0f}, 4});
		defaults.push_back({"metallic", {0.0f}, 1});
		defaults.push_back({"roughness", {0.5f}, 1});
		defaults.push_back({"reflectance", {0.5f}, 1});
		defaults.push_back({"normalScale", {1.0f}, 1});
		defaults.push_back({"emissive", {0.0f, 0.0f, 0.0f}, 3});
		defaults.push_back({"occlusionStrength", {1.0f}, 1});
		return defaults;
	}

	f32 FloatAt(const u8* block, u32 offset) {
		f32 value = 0.0f;
		std::memcpy(&value, block + offset, sizeof(value));
		return value;
	}

	TEST_F(MaterialMigrationFixture, MigratedMaterialPacksPredictedLitBlock) {
		Path file = Write("packed.mat", V1Document(RetiredStandardLit, R"({ "specular": 0.5, "shininess": 32 })"));
		MaterialAsset material;
		ASSERT_TRUE(material.Import(file));

		const UniformBlockLayout layout = LitBlock();
		const Vector<ShaderMemberDefault> defaults = LitDefaults();
		u8 block[48] = {};
		std::memset(block, 0xCD, sizeof(block));
		material.PackBlock(layout, {block, sizeof(block)}, {defaults.data(), defaults.size()});

		EXPECT_NEAR(FloatAt(block, 0), 0.25f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 4), 0.5f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 8), 0.75f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 12), 1.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 16), 0.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 20), 0.8f, 1e-6f);

		EXPECT_NEAR(FloatAt(block, 24), 0.5f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 28), 1.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 32), 0.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 36), 0.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 40), 0.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 44), 1.0f, 1e-6f);
	}

	TEST_F(MaterialMigrationFixture, RowlessMaterialPacksDeclaration) {
		MaterialAsset material;

		const UniformBlockLayout layout = LitBlock();
		const Vector<ShaderMemberDefault> defaults = LitDefaults();
		u8 block[48] = {};
		std::memset(block, 0xCD, sizeof(block));
		material.PackBlock(layout, {block, sizeof(block)}, {defaults.data(), defaults.size()});

		EXPECT_NEAR(FloatAt(block, 0), 1.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 12), 1.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 16), 0.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 20), 0.5f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 24), 0.5f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 44), 1.0f, 1e-6f);
	}

	TEST_F(MaterialMigrationFixture, AuthoredValueBeatsShadersDeclaredDefault) {
		MaterialAsset material;
		material.SetFloat("reflectance", 0.1f);
		material.SetFloat("occlusionStrength", 0.0f);

		const UniformBlockLayout layout = LitBlock();
		const Vector<ShaderMemberDefault> defaults = LitDefaults();
		u8 block[48] = {};
		material.PackBlock(layout, {block, sizeof(block)}, {defaults.data(), defaults.size()});

		EXPECT_NEAR(FloatAt(block, 24), 0.1f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 44), 0.0f, 1e-6f);
	}

	TEST_F(MaterialMigrationFixture, AbsentMemberStays0) {
		MaterialAsset material;
		material.SetFloat("metallic", 1.0f);

		const UniformBlockLayout layout = LitBlock();
		u8 block[48] = {};
		std::memset(block, 0xCD, sizeof(block));
		material.PackBlock(layout, {block, sizeof(block)});

		EXPECT_NEAR(FloatAt(block, 16), 1.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 24), 0.0f, 1e-6f);
		EXPECT_NEAR(FloatAt(block, 44), 0.0f, 1e-6f);
	}

} // namespace
