#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class MaterialRenderStateFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_matstate_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
		}

		void TearDown() override {
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Path File(const char* name) const { return Path((_root / name).generic_string()); }

		str ReadBack(const char* name) const { return FileSystem::ReadTextFile(File(name), MemTag::Asset); }

		void RoundTrip(const MaterialAsset& source, MaterialAsset& out, const char* name) {
			ASSERT_TRUE(source.Export(File(name)));
			ASSERT_TRUE(out.Import(File(name)));
		}
	};

	void SeedMaterial(MaterialAsset& material) {
		material.shaderGuid = Guid(0x1122334455667788ull, 0x99aabbccddeeff00ull);
		material.SetColor("baseColor", Color(0.5f, 0.25f, 0.75f, 1.0f));
	}

} // anonymous namespace

TEST_F(MaterialRenderStateFixture, OverridesRoundTripDocument) {
	MaterialAsset source;
	SeedMaterial(source);
	source.blend = RHI::BlendMode::Premultiplied;
	source.cull = RHI::CullMode::Off;
	source.SetAlphaClipThreshold(0.375f);
	source.sortingPriority = -12;

	MaterialAsset loaded;
	RoundTrip(source, loaded, "Overrides.mat");

	ASSERT_TRUE(loaded.blend.has_value());
	EXPECT_EQ(*loaded.blend, RHI::BlendMode::Premultiplied);
	ASSERT_TRUE(loaded.cull.has_value());
	EXPECT_EQ(*loaded.cull, RHI::CullMode::Off);
	EXPECT_FLOAT_EQ(loaded.alphaClipThreshold, 0.375f);
	EXPECT_EQ(loaded.sortingPriority, -12);
	EXPECT_EQ(loaded.GetColor(StringHash("baseColor")), Color(0.5f, 0.25f, 0.75f, 1.0f));
}

TEST_F(MaterialRenderStateFixture, EveryBlendAndCullValueSurvivesSpelling) {
	for (u8 i = 0; i < static_cast<u8>(RHI::BlendMode::Count); ++i) {
		MaterialAsset source;
		SeedMaterial(source);
		source.blend = static_cast<RHI::BlendMode>(i);
		MaterialAsset loaded;
		RoundTrip(source, loaded, "Blend.mat");
		ASSERT_TRUE(loaded.blend.has_value()) << "blend ordinal " << int(i);
		EXPECT_EQ(static_cast<u8>(*loaded.blend), i);
	}
	for (u8 i = 0; i < static_cast<u8>(RHI::CullMode::Count); ++i) {
		MaterialAsset source;
		SeedMaterial(source);
		source.cull = static_cast<RHI::CullMode>(i);
		MaterialAsset loaded;
		RoundTrip(source, loaded, "Cull.mat");
		ASSERT_TRUE(loaded.cull.has_value()) << "cull ordinal " << int(i);
		EXPECT_EQ(static_cast<u8>(*loaded.cull), i);
	}
}

TEST_F(MaterialRenderStateFixture, NoOverridesWriteNoKeys) {
	MaterialAsset plain;
	SeedMaterial(plain);
	ASSERT_TRUE(plain.Export(File("Plain.mat")));
	str text = ReadBack("Plain.mat");
	EXPECT_EQ(text.find("blend"), str::npos) << text;
	EXPECT_EQ(text.find("cull"), str::npos) << text;
	EXPECT_EQ(text.find("alphaClip"), str::npos) << text;
	EXPECT_EQ(text.find("sortingPriority"), str::npos) << text;

	MaterialAsset loaded;
	ASSERT_TRUE(loaded.Import(File("Plain.mat")));
	EXPECT_FALSE(loaded.blend.has_value());
	EXPECT_FALSE(loaded.cull.has_value());
	EXPECT_FLOAT_EQ(loaded.alphaClipThreshold, 0.0f);
	EXPECT_EQ(loaded.sortingPriority, 0);
}

TEST_F(MaterialRenderStateFixture, AnUnknownNameLeavesTheOverrideAbsent) {
	const str document =
		str("{\n\t\"shader\": \"") + Guid(1, 2).ToString() + "\",\n"
															 "\t\"blend\": \"glowy\",\n"
															 "\t\"cull\": \"sideways\"\n}\n";
	ASSERT_TRUE(FileSystem::WriteTextFile(File("Typo.mat"), document));

	MaterialAsset loaded;
	loaded.blend = RHI::BlendMode::Multiply;
	loaded.cull = RHI::CullMode::Front;
	ASSERT_TRUE(loaded.Import(File("Typo.mat")));
	EXPECT_FALSE(loaded.blend.has_value());
	EXPECT_FALSE(loaded.cull.has_value());
}

TEST_F(MaterialRenderStateFixture, OverridesRoundTripCook) {
	MaterialAsset source;
	SeedMaterial(source);
	source.blend = RHI::BlendMode::Multiply;
	source.cull = RHI::CullMode::Front;
	source.SetAlphaClipThreshold(0.5f);
	source.sortingPriority = 7;

	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Asset)};
	PakStringPool pool;
	CookWriter descriptor(descriptorBytes, pool);
	CookWriter payload(payloadBytes, pool);
	ASSERT_TRUE(source.Cook(descriptor, payload));
	EXPECT_TRUE(payloadBytes.empty());

	Vector<u8> stringBytes{GetMemoryResource(MemTag::Asset)};
	PakWriteStringBlob(stringBytes, pool.Texts());
	PakStringTable table;
	ASSERT_TRUE(PakReadStringBlob(stringBytes.data(), stringBytes.size(), table));

	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size(), table);
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size(), table);
	MaterialAsset loaded;
	ASSERT_TRUE(loaded.LoadCooked(descriptorReader, payloadReader));

	ASSERT_TRUE(loaded.blend.has_value());
	EXPECT_EQ(*loaded.blend, RHI::BlendMode::Multiply);
	ASSERT_TRUE(loaded.cull.has_value());
	EXPECT_EQ(*loaded.cull, RHI::CullMode::Front);
	EXPECT_FLOAT_EQ(loaded.alphaClipThreshold, 0.5f);
	EXPECT_EQ(loaded.sortingPriority, 7);
	EXPECT_NE(loaded.keywordHash, 0ull);

	MaterialAsset plain;
	SeedMaterial(plain);
	Vector<u8> plainBytes{GetMemoryResource(MemTag::Asset)};
	Vector<u8> plainPayload{GetMemoryResource(MemTag::Asset)};
	PakStringPool plainPool;
	CookWriter plainDescriptor(plainBytes, plainPool);
	CookWriter plainPayloadWriter(plainPayload, plainPool);
	ASSERT_TRUE(plain.Cook(plainDescriptor, plainPayloadWriter));
	Vector<u8> plainStrings{GetMemoryResource(MemTag::Asset)};
	PakWriteStringBlob(plainStrings, plainPool.Texts());
	PakStringTable plainTable;
	ASSERT_TRUE(PakReadStringBlob(plainStrings.data(), plainStrings.size(), plainTable));
	CookReader plainReader(plainBytes.data(), plainBytes.size(), plainTable);
	CookReader plainPayloadReader(plainPayload.data(), plainPayload.size(), plainTable);
	MaterialAsset plainLoaded;
	plainLoaded.blend = RHI::BlendMode::Additive;
	ASSERT_TRUE(plainLoaded.LoadCooked(plainReader, plainPayloadReader));
	EXPECT_FALSE(plainLoaded.blend.has_value());
	EXPECT_FALSE(plainLoaded.cull.has_value());
}

TEST(MaterialRenderStateTest, OverrideReplacesShaderValue) {
	RHI::ShaderRenderState declared;
	declared.blend = RHI::BlendMode::Additive;
	declared.cull = RHI::CullMode::Off;
	declared.topology = RHI::PrimitiveTopology::LineList;
	declared.fill = RHI::FillMode::Wireframe;
	declared.depthBiasConstant = 1.5f;
	declared.depthBiasSlope = -2.0f;

	MaterialAsset plain;
	RHI::ShaderRenderState inherited = plain.ResolveRenderState(declared);
	EXPECT_EQ(inherited.blend, RHI::BlendMode::Additive);
	EXPECT_EQ(inherited.cull, RHI::CullMode::Off);
	EXPECT_EQ(inherited.topology, RHI::PrimitiveTopology::LineList);
	EXPECT_EQ(inherited.fill, RHI::FillMode::Wireframe);
	EXPECT_FLOAT_EQ(inherited.depthBiasConstant, 1.5f);
	EXPECT_FLOAT_EQ(inherited.depthBiasSlope, -2.0f);

	MaterialAsset overridden;
	overridden.blend = RHI::BlendMode::Opaque;
	overridden.cull = RHI::CullMode::Front;
	RHI::ShaderRenderState resolved = overridden.ResolveRenderState(declared);
	EXPECT_EQ(resolved.blend, RHI::BlendMode::Opaque);
	EXPECT_EQ(resolved.cull, RHI::CullMode::Front);
	EXPECT_EQ(resolved.topology, RHI::PrimitiveTopology::LineList);
}

TEST(MaterialRenderStateTest, BlendedSurfaceSkipsDepth) {
	RHI::ShaderRenderState opaqueShader;

	MaterialAsset plain;
	EXPECT_EQ(plain.ResolveRenderState(opaqueShader).depth, RHI::DepthMode::TestWrite);

	for (RHI::BlendMode mode : {RHI::BlendMode::Alpha, RHI::BlendMode::Additive,
								RHI::BlendMode::Premultiplied, RHI::BlendMode::Multiply}) {
		MaterialAsset blended;
		blended.blend = mode;
		EXPECT_EQ(blended.ResolveRenderState(opaqueShader).depth, RHI::DepthMode::TestOnly) << "blend ordinal " << int(static_cast<u8>(mode));

		RHI::ShaderRenderState blendedShader;
		blendedShader.blend = mode;
		EXPECT_EQ(plain.ResolveRenderState(blendedShader).depth, RHI::DepthMode::TestOnly) << int(static_cast<u8>(mode));
	}

	RHI::ShaderRenderState noDepth;
	noDepth.depth = RHI::DepthMode::Off;
	MaterialAsset blended;
	blended.blend = RHI::BlendMode::Alpha;
	EXPECT_EQ(blended.ResolveRenderState(noDepth).depth, RHI::DepthMode::Off);
}

TEST(MaterialRenderStateTest, ThresholdMovesKeywordHash) {
	const str authored[] = {str("USE_A")};
	MaterialAsset material;
	material.SetKeywords({authored, 1});
	const u64 unclipped = material.keywordHash;
	EXPECT_EQ(unclipped, ShaderAsset::KeywordSetHash({authored, 1}));

	material.SetAlphaClipThreshold(0.5f);
	const str clipped[] = {str(ShaderAlphaClipKeyword), str("USE_A")};
	EXPECT_NE(material.keywordHash, unclipped);
	EXPECT_EQ(material.keywordHash, ShaderAsset::KeywordSetHash({clipped, 2}));

	ASSERT_EQ(material.keywords.size(), 1u);
	EXPECT_EQ(material.keywords[0], "USE_A");

	material.SetAlphaClipThreshold(0.0f);
	EXPECT_EQ(material.keywordHash, unclipped);

	MaterialAsset bare;
	const u64 empty = bare.keywordHash;
	bare.SetAlphaClipThreshold(0.25f);
	EXPECT_NE(bare.keywordHash, empty);
	const str clipOnly[] = {str(ShaderAlphaClipKeyword)};
	EXPECT_EQ(bare.keywordHash, ShaderAsset::KeywordSetHash({clipOnly, 1}));
}

TEST_F(MaterialRenderStateFixture, ThresholdReadFromDocumentSelectsClippedVariant) {
	MaterialAsset source;
	SeedMaterial(source);
	source.SetAlphaClipThreshold(0.5f);
	MaterialAsset loaded;
	RoundTrip(source, loaded, "Clip.mat");

	const str clipOnly[] = {str(ShaderAlphaClipKeyword)};
	EXPECT_EQ(loaded.keywordHash, ShaderAsset::KeywordSetHash({clipOnly, 1}));
}
