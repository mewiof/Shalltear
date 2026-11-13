#include "TestsPCH.h"

#include "ShadingHarness.h"
#include "TestImages.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/GraphicsProfile.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Settings.h"

#include "Graphics/GraphicsSettings.h"
#include "Graphics/TextureSampling.h"

#include <utility>
#include <vector>

using namespace Shalltear;
using ShadingTest::CenterTexel;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::ShadingStage;

TEST(TextureSamplingTest, SettingsRoundTripThroughMeta) {
	TextureAsset::Settings written;
	written.sRGB = false;
	written.generateMips = false;
	written.filter = TextureFilter::Bilinear;
	written.addressMode = RHI::AddressMode::MirroredRepeat;
	written.anisoLevel = 5;
	written.shape = TextureShape::Texture2DArray;
	written.cubeFaceSize = 64;
	written.convolveIBL = false;
	written.columns = 3;
	written.rows = 2;
	written.type = TextureType::NormalMap;
	written.compression = TextureCompression::None;
	written.pixelsPerUnit = 32.5f;

	JSONValue meta = JSONValue::MakeObject();
	TextureAssetImporter::WriteSettings(written, meta);
	EXPECT_EQ(meta["filter"].AsString(), "bilinear");
	EXPECT_EQ(meta["anisoLevel"].AsInt(), 5);
	EXPECT_EQ(meta["pixelsPerUnit"].AsNumber(), 32.5);

	TextureAsset::Settings read;
	TextureAssetImporter::ReadSettings(meta, read);
	EXPECT_EQ(read, written);
}

TEST(TextureSamplingTest, OlderFilterNamesStillRead) {
	const std::pair<const char*, TextureFilter> names[] = {{"nearest", TextureFilter::Point},
														   {"linear", TextureFilter::Trilinear},
														   {"point", TextureFilter::Point},
														   {"bilinear", TextureFilter::Bilinear},
														   {"trilinear", TextureFilter::Trilinear}};
	for (const auto& [name, expected] : names) {
		JSONValue meta = JSONValue::MakeObject();
		meta.AddMember("filter", JSONValue::MakeString(name));
		TextureAsset::Settings read;
		read.filter = expected == TextureFilter::Bilinear ? TextureFilter::Point : TextureFilter::Bilinear;
		TextureAssetImporter::ReadSettings(meta, read);
		EXPECT_EQ(read.filter, expected) << name;
	}

	TextureAsset::Settings point;
	point.filter = TextureFilter::Point;
	JSONValue written = JSONValue::MakeObject();
	TextureAssetImporter::WriteSettings(point, written);
	EXPECT_EQ(written["filter"].AsString(), "point");
	JSONValue defaults = JSONValue::MakeObject();
	TextureAssetImporter::WriteSettings(TextureAsset::Settings{}, defaults);
	EXPECT_EQ(defaults["filter"].AsString(), "trilinear");
	EXPECT_EQ(defaults["anisoLevel"].AsInt(), 16);
}

TEST(TextureSamplingTest, UnknownValueChangesNothing) {
	TextureAsset::Settings read;
	read.filter = TextureFilter::Point;
	read.anisoLevel = 2;
	JSONValue meta = JSONValue::MakeObject();
	meta.AddMember("filter", JSONValue::MakeString("anisotropic"));
	TextureAssetImporter::ReadSettings(meta, read);
	EXPECT_EQ(read.filter, TextureFilter::Point);
	EXPECT_EQ(read.anisoLevel, 2);

	meta.AddMember("anisoLevel", JSONValue::MakeNumber(40.0));
	TextureAssetImporter::ReadSettings(meta, read);
	EXPECT_EQ(read.anisoLevel, 16);
}

TEST(TextureSamplingTest, PixelsPerUnitMustBePositive) {
	auto read = [](JSONValue value) {
		JSONValue meta = JSONValue::MakeObject();
		meta.AddMember("pixelsPerUnit", std::move(value));
		TextureAsset::Settings settings;
		settings.pixelsPerUnit = 64.0f;
		TextureAssetImporter::ReadSettings(meta, settings);
		return settings.pixelsPerUnit;
	};
	EXPECT_EQ(read(JSONValue::MakeNumber(200.0)), 200.0f);
	EXPECT_EQ(read(JSONValue::MakeNumber(0.001)), 0.01f);
	EXPECT_EQ(read(JSONValue::MakeNumber(0.0)), 64.0f);
	EXPECT_EQ(read(JSONValue::MakeNumber(-16.0)), 64.0f);
	EXPECT_EQ(read(JSONValue::MakeString("200")), 64.0f);

	JSONValue defaults = JSONValue::MakeObject();
	TextureAssetImporter::WriteSettings(TextureAsset::Settings{}, defaults);
	EXPECT_EQ(defaults["pixelsPerUnit"].AsNumber(), 100.0);
}

TEST(TextureSamplingTest, ProfileModeCapsTextureLevel) {
	using GraphicsInternal::ResolveTextureAnisotropy;
	struct Row {
		u8 level;
		TextureAnisotropy mode;
		i32 maximum;
		u8 expected;
	};
	const Row rows[] = {{16, TextureAnisotropy::PerTexture, 16, 16}, {16, TextureAnisotropy::PerTexture, 4, 4}, {2, TextureAnisotropy::PerTexture, 16, 2}, {1, TextureAnisotropy::PerTexture, 16, 1}, {0, TextureAnisotropy::PerTexture, 16, 1}, {1, TextureAnisotropy::ForcedOn, 8, 8}, {12, TextureAnisotropy::ForcedOn, 8, 8}, {0, TextureAnisotropy::ForcedOn, 16, 1}, {16, TextureAnisotropy::Disabled, 16, 1}, {16, TextureAnisotropy::PerTexture, 40, 16}, {16, TextureAnisotropy::PerTexture, 0, 1}};
	for (const Row& row : rows) {
		TextureAsset::Settings settings;
		settings.anisoLevel = row.level;
		EXPECT_EQ(ResolveTextureAnisotropy(settings, false, row.mode, row.maximum), row.expected)
			<< "level " << static_cast<u32>(row.level) << " mode " << static_cast<u32>(row.mode) << " max " << row.maximum;
	}

	TextureAsset::Settings point;
	point.filter = TextureFilter::Point;
	EXPECT_EQ(ResolveTextureAnisotropy(point, false, TextureAnisotropy::ForcedOn, 16), 1);
	TextureAsset::Settings bilinear;
	bilinear.filter = TextureFilter::Bilinear;
	EXPECT_EQ(ResolveTextureAnisotropy(bilinear, false, TextureAnisotropy::PerTexture, 16), 16);
	EXPECT_EQ(ResolveTextureAnisotropy(bilinear, true, TextureAnisotropy::ForcedOn, 16), 1);
}

TEST(TextureSamplingTest, FilterPicksMinAndMipFilter) {
	struct Row {
		TextureFilter filter;
		RHI::FilterMode texels;
		RHI::FilterMode mips;
	};
	const Row rows[] = {{TextureFilter::Point, RHI::FilterMode::Nearest, RHI::FilterMode::Nearest},
						{TextureFilter::Bilinear, RHI::FilterMode::Linear, RHI::FilterMode::Nearest},
						{TextureFilter::Trilinear, RHI::FilterMode::Linear, RHI::FilterMode::Linear}};
	for (const Row& row : rows) {
		TextureAsset::Settings settings;
		settings.filter = row.filter;
		settings.addressMode = RHI::AddressMode::ClampToEdge;
		const RHI::SamplerDesc desc = GraphicsInternal::TextureSamplerDesc(settings, false, TextureAnisotropy::PerTexture, 16);
		EXPECT_EQ(desc.filter, row.texels);
		EXPECT_EQ(desc.mipFilter, row.mips);
		EXPECT_EQ(desc.addressMode, RHI::AddressMode::ClampToEdge);
	}
}

namespace {

	constexpr i32 STRIPES_SIZE = 64;

	constexpr const char* FOOTPRINT_SHADER = "#pragma shalltear_template unlit\n"
											 "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float sampleU; float sampleV; float lod; float ratio; };\n"
											 "[[vk::binding(1, 1)]] Texture2D stripes;\n"
											 "[[vk::binding(2, 1)]] SamplerState stripesSampler;\n"
											 "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
											 "\tfloat minor = exp2(lod) / 64.0;\n"
											 "\tsurface.baseColor = stripes.SampleGrad(stripesSampler, float2(sampleU, sampleV), float2(minor, 0.0),\n"
											 "\t\tfloat2(0.0, minor * ratio)).rgb;\n"
											 "}\n";

	constexpr const char* PAIR_SHADER = "#pragma shalltear_template unlit\n"
										"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 unused; };\n"
										"[[vk::binding(1, 1)]] Texture2D first;\n"
										"[[vk::binding(2, 1)]] SamplerState firstSampler;\n"
										"[[vk::binding(3, 1)]] Texture2D second;\n"
										"[[vk::binding(4, 1)]] SamplerState secondSampler;\n"
										"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
										"\tsurface.baseColor = 0.5 * (first.Sample(firstSampler, input.uv).rgb + second.Sample(secondSampler, input.uv).rgb);\n"
										"}\n";

	f32 Brightness(const Vec3& texel) { return texel.x; }

	class TextureSamplingFixture : public MaterialReadbackFixture {
	protected:
		GraphicsProfile* _profile = nullptr;

		str ProjectSettingsDocument() const override { return "[Graphics]\n"; }

		bool UseProfile() {
			const Guid id = AssetDatabase::CreateAsset(Path("project://Sampling.slprofile"), StringHash("GraphicsProfile"));
			_profile = id.IsValid() ? static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(id)) : nullptr;
			GraphicsSettings* settings = Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
			if (!_profile || !settings) {
				return false;
			}
			settings->qualityProfiles = id.ToString();
			settings->qualityLevel = 0;
			Settings::NotifyChanged("Graphics", "qualityLevel");
			return true;
		}

		void TearDown() override {
			if (_profile) {
				AssetDatabase::UnloadAsset(_profile);
				_profile = nullptr;
			}
			MaterialReadbackFixture::TearDown();
		}

		Guid Stripes(const char* file, bool alongRows) {
			std::vector<u8> rgba(static_cast<size_t>(STRIPES_SIZE) * STRIPES_SIZE * 4);
			for (i32 y = 0; y < STRIPES_SIZE; ++y) {
				for (i32 x = 0; x < STRIPES_SIZE; ++x) {
					const u8 value = ((alongRows ? y : x) % 2 == 0) ? 255 : 0;
					u8* texel = &rgba[(static_cast<size_t>(y) * STRIPES_SIZE + x) * 4];
					texel[0] = texel[1] = texel[2] = value;
					texel[3] = 255;
				}
			}
			TestImages::WriteTGA(_root / file, STRIPES_SIZE, STRIPES_SIZE, rgba.data());
			return AssetDatabase::EnsureAssetIdentity(Path(str("project://") + file));
		}

		static bool Configure(const char* file, const char* filter, bool mips, u8 anisoLevel) {
			const Path path(str("project://") + file);
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("sRGB", JSONValue::MakeBool(false));
			settings.AddMember("compression", JSONValue::MakeString("none"));
			settings.AddMember("generateMips", JSONValue::MakeBool(mips));
			settings.AddMember("filter", JSONValue::MakeString(filter));
			settings.AddMember("anisoLevel", JSONValue::MakeNumber(anisoLevel));
			return AssetDatabase::WriteImporterSettings(path, settings) && AssetDatabase::ReimportAsset(path);
		}

		MaterialAsset* FootprintMaterial(const char* name, const Guid& texture, f32 u, f32 v, f32 lod, f32 ratio) {
			WriteProjectFile("Footprint.hlsl", FOOTPRINT_SHADER);
			Asset* shader = AssetDatabase::LoadAsset(Path("project://Footprint.hlsl"));
			if (!shader) {
				return nullptr;
			}
			const str body = "\t\"textures\": { \"stripes\": \"" + texture.ToString() + "\" },\n\t\"floats\": { \"sampleU\": " + std::to_string(u) +
							 ", \"sampleV\": " + std::to_string(v) + ", \"lod\": " + std::to_string(lod) + ", \"ratio\": " + std::to_string(ratio) + " }";
			return LoadMaterial(name, shader->assetId, body);
		}

		f32 Render(const ShadingStage& stage) {
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			return RenderStage(stage, pixels, size) ? Brightness(CenterTexel(pixels, size)) : -1.0f;
		}
	};

	constexpr f32 WHITE = 250.0f;
	constexpr f32 BLACK = 5.0f;

	f32 TexelCenter(i32 texel) { return (static_cast<f32>(texel) + 0.5f) / static_cast<f32>(STRIPES_SIZE); }

} // anonymous namespace

TEST_F(TextureSamplingFixture, LevelAndProfileShapeGrazingFootprint) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(UseProfile());
	const Guid stripes = Stripes("Columns.tga", false);
	ASSERT_TRUE(Configure("Columns.tga", "trilinear", true, 16));
	MaterialAsset* material = FootprintMaterial("Grazing.mat", stripes, TexelCenter(8), TexelCenter(32), 0.0f, 8.0f);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	const f32 sharp = Render(stage);
	EXPECT_GT(sharp, WHITE);

	ASSERT_TRUE(Configure("Columns.tga", "trilinear", true, 1));
	const f32 blurred = Render(stage);
	EXPECT_GT(blurred, BLACK + 40.0f);
	EXPECT_LT(blurred, WHITE - 20.0f);

	_profile->anisotropicFiltering = TextureAnisotropy::ForcedOn;
	EXPECT_NEAR(Render(stage), sharp, 2.0f);

	ASSERT_TRUE(Configure("Columns.tga", "trilinear", true, 0));
	EXPECT_NEAR(Render(stage), blurred, 2.0f);

	ASSERT_TRUE(Configure("Columns.tga", "trilinear", true, 16));
	_profile->anisotropicFiltering = TextureAnisotropy::Disabled;
	EXPECT_NEAR(Render(stage), blurred, 2.0f);

	_profile->anisotropicFiltering = TextureAnisotropy::PerTexture;
	_profile->maxAnisotropy = 4;
	EXPECT_LT(Render(stage), WHITE - 20.0f);
	_profile->maxAnisotropy = 16;
	EXPECT_NEAR(Render(stage), sharp, 2.0f);

	TearDownStage(stage);
}

TEST_F(TextureSamplingFixture, PointSamplesOneTexelAtGrazingAngle) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(UseProfile());
	const Guid stripes = Stripes("Rows.tga", true);
	ASSERT_TRUE(Configure("Rows.tga", "point", false, 16));
	MaterialAsset* material = FootprintMaterial("Point.mat", stripes, TexelCenter(32), TexelCenter(8), 0.0f, 8.0f);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	_profile->anisotropicFiltering = TextureAnisotropy::ForcedOn;
	const f32 texel = Render(stage);
	EXPECT_TRUE(texel < BLACK || texel > WHITE) << texel;

	ASSERT_TRUE(Configure("Rows.tga", "trilinear", false, 16));
	const f32 filtered = Render(stage);
	EXPECT_GT(filtered, BLACK + 40.0f);
	EXPECT_LT(filtered, WHITE - 20.0f);

	TearDownStage(stage);
}

TEST_F(TextureSamplingFixture, BilinearPicksOneMip) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(UseProfile());
	const Guid stripes = Stripes("Mips.tga", false);
	ASSERT_TRUE(Configure("Mips.tga", "trilinear", true, 1));
	MaterialAsset* material = FootprintMaterial("Mips.mat", stripes, TexelCenter(8), TexelCenter(32), 0.0f, 1.0f);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	const f32 top = Render(stage);
	material->SetFloat("lod", 1.0f);
	const f32 next = Render(stage);
	ASSERT_GT(top, WHITE);
	ASSERT_LT(next, WHITE - 40.0f);

	for (f32 lod : {0.45f, 0.55f}) {
		material->SetFloat("lod", lod);
		const f32 blended = Render(stage);
		EXPECT_GT(blended, next + 10.0f) << lod;
		EXPECT_LT(blended, top - 10.0f) << lod;
	}

	ASSERT_TRUE(Configure("Mips.tga", "bilinear", true, 1));
	material->SetFloat("lod", 0.45f);
	EXPECT_NEAR(Render(stage), top, 2.0f);
	material->SetFloat("lod", 0.55f);
	EXPECT_NEAR(Render(stage), next, 2.0f);

	TearDownStage(stage);
}

TEST_F(TextureSamplingFixture, SamplersSharedPerDescription) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(UseProfile());
	WriteProjectFile("Pair.hlsl", PAIR_SHADER);
	Asset* shader = AssetDatabase::LoadAsset(Path("project://Pair.hlsl"));
	ASSERT_NE(shader, nullptr);
	MaterialAsset* material = LoadMaterial("Pair.mat", shader->assetId, "");
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	ASSERT_GE(Render(stage), 0.0f);
	const u32 before = RenderSystem::GetStats().liveSamplers;

	const Guid first = Stripes("First.tga", false);
	const Guid second = Stripes("Second.tga", true);
	const Guid third = Stripes("Third.tga", false);
	ASSERT_TRUE(Configure("First.tga", "trilinear", true, 16));
	ASSERT_TRUE(Configure("Second.tga", "trilinear", true, 16));
	ASSERT_TRUE(Configure("Third.tga", "point", true, 16));
	Asset* loaded[] = {AssetDatabase::LoadAssetByGuid(first), AssetDatabase::LoadAssetByGuid(second), AssetDatabase::LoadAssetByGuid(third)};
	for (Asset* asset : loaded) {
		ASSERT_NE(asset, nullptr);
	}
	EXPECT_EQ(RenderSystem::GetStats().liveSamplers, before);

	material->SetTexture("first", first);
	material->SetTexture("second", second);
	ASSERT_GE(Render(stage), 0.0f);
	EXPECT_EQ(RenderSystem::GetStats().liveSamplers, before + 1);

	material->SetTexture("second", third);
	ASSERT_GE(Render(stage), 0.0f);
	EXPECT_EQ(RenderSystem::GetStats().liveSamplers, before + 2);

	_profile->maxAnisotropy = 4;
	ASSERT_GE(Render(stage), 0.0f);
	EXPECT_EQ(RenderSystem::GetStats().liveSamplers, before + 3);

	TearDownStage(stage);
	for (Asset* asset : loaded) {
		AssetDatabase::UnloadAsset(asset);
	}
}
