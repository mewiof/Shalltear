#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/ShaderReflection.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>

using namespace Shalltear;

namespace {

	class ShaderInterfaceFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_matlayout_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
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
	};

	constexpr const char* MATERIAL_SHADER = R"(
[[vk::binding(0, 0)]] cbuffer ViewData { float4x4 viewProjection; };
[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 baseColor;
	float specular;
	float shininess;
	float2 sltReserved;
};
[[vk::binding(1, 1)]] Texture2D baseColorTexture;
[[vk::binding(2, 1)]] SamplerState sltBaseColorSampler;
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = mul(viewProjection, float4(float(vertexId), 0, 0, 1));
	return output;
}
float4 PSMain(VSOutput input) : SV_Target {
	return baseColorTexture.Sample(sltBaseColorSampler, float2(0, 0)) * baseColor * specular * shininess;
}
)";

	constexpr const char* EVERY_KIND_SHADER = R"(
[[vk::binding(0, 0)]] cbuffer ViewData { float4x4 viewProjection; };
[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 baseColor;
	float2 tiling;
	float3 emissive;
	int mode;
	bool useDetail;
	float roughness;
};
[[vk::binding(1, 1)]] Texture2D baseColorTexture;
[[vk::binding(2, 1)]] SamplerState baseColorSampler;
[[vk::binding(3, 1)]] Texture2D normalTexture;
[[vk::binding(4, 1)]] SamplerState normalSampler;
[[vk::binding(5, 1)]] Texture2D emissiveTexture;
[[vk::binding(6, 1)]] SamplerState emissiveSampler;
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = mul(viewProjection, float4(float(vertexId), 0, 0, 1));
	return output;
}
float4 PSMain(VSOutput input) : SV_Target {
	float4 sum = baseColorTexture.Sample(baseColorSampler, tiling)
			   + normalTexture.Sample(normalSampler, tiling)
			   + emissiveTexture.Sample(emissiveSampler, tiling);
	return sum * baseColor * float4(emissive, roughness) * float(mode) * (useDetail ? 1.0 : 0.5);
}
)";

	const UniformMember* MemberNamed(const UniformBlockLayout& layout, std::string_view name) {
		for (const UniformMember& member : layout.members) {
			if (member.name == name) {
				return &member;
			}
		}
		return nullptr;
	}

} // namespace

TEST_F(ShaderInterfaceFixture, BlockMembersHaveNameTypeOffset) {
	ShaderAsset asset;
	if (!asset.Import(Write("material.hlsl", MATERIAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}

	const UniformBlockLayout* layout = asset.GetMaterialParamLayout();
	ASSERT_NE(layout, nullptr);
	EXPECT_EQ(layout->set, MATERIAL_DESCRIPTOR_SET);
	EXPECT_EQ(layout->binding, 0u);

	ASSERT_GE(layout->members.size(), 3u);
	EXPECT_EQ(layout->members[0].name, "baseColor");
	EXPECT_EQ(layout->members[0].type, UniformMemberType::Vec4);
	EXPECT_EQ(layout->members[0].offset, 0u);
	EXPECT_EQ(layout->members[1].name, "specular");
	EXPECT_EQ(layout->members[1].type, UniformMemberType::Float);
	EXPECT_EQ(layout->members[1].offset, 16u);
	EXPECT_EQ(layout->members[2].name, "shininess");
	EXPECT_EQ(layout->members[2].type, UniformMemberType::Float);
	EXPECT_EQ(layout->members[2].offset, 20u);
}

TEST_F(ShaderInterfaceFixture, TextureSlotsAreDiscoverableByName) {
	ShaderAsset asset;
	if (!asset.Import(Write("material.hlsl", MATERIAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	Vector<str> slots{GetMemoryResource(MemTag::Asset)};
	asset.GetMaterialTextureSlots(slots);
	ASSERT_EQ(slots.size(), 1u);
	EXPECT_EQ(slots[0], "baseColorTexture");
}

TEST_F(ShaderInterfaceFixture, ViewSetNotMaterialSet) {
	ShaderAsset asset;
	if (!asset.Import(Write("material.hlsl", MATERIAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_GE(asset.uniformBlocks.size(), 2u);
	EXPECT_EQ(asset.GetMaterialParamLayout()->set, MATERIAL_DESCRIPTOR_SET);
}

TEST_F(ShaderInterfaceFixture, PackedBlockMatchesFixedStructItReplaced) {
	ShaderAsset asset;
	if (!asset.Import(Write("material.hlsl", MATERIAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const UniformBlockLayout* layout = asset.GetMaterialParamLayout();
	ASSERT_NE(layout, nullptr);

	MaterialAsset material;
	material.SetColor("baseColor", Color{0.25f, 0.5f, 0.75f, 1.0f});
	material.SetFloat("specular", 0.375f);
	material.SetFloat("shininess", 64.0f);

	u8 packed[64] = {};
	ASSERT_LE(layout->sizeBytes, sizeof(packed));
	material.PackBlock(*layout, packed);

	struct LegacyMaterialUniforms {
		Color baseColorFactor;
		Vec4 specularParams;
	};
	static_assert(sizeof(LegacyMaterialUniforms) == 32);
	LegacyMaterialUniforms legacy{Color{0.25f, 0.5f, 0.75f, 1.0f}, Vec4{0.375f, 64.0f, 0.0f, 0.0f}};

	EXPECT_EQ(layout->sizeBytes, 32u);
	EXPECT_EQ(std::memcmp(packed, &legacy, sizeof(legacy)), 0);
}

TEST_F(ShaderInterfaceFixture, SyncAddsMissingKeepsAuthored) {
	ShaderAsset asset;
	if (!asset.Import(Write("material.hlsl", MATERIAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Vector<str> slots{GetMemoryResource(MemTag::Asset)};
	asset.GetMaterialTextureSlots(slots);

	MaterialAsset material;
	material.SetFloat("shininess", 96.0f);

	u32 added = material.SyncToLayout(asset.GetMaterialParamLayout(), {slots.data(), slots.size()});
	EXPECT_EQ(added, 4u);

	EXPECT_FLOAT_EQ(material.GetFloat("shininess", -1.0f), 96.0f);
	EXPECT_TRUE(material.HasColor(StringHash("baseColor")));
	EXPECT_TRUE(material.HasFloat(StringHash("specular")));
	EXPECT_TRUE(material.HasTexture(StringHash("baseColorTexture")));

	EXPECT_EQ(material.SyncToLayout(asset.GetMaterialParamLayout(), {slots.data(), slots.size()}), 0u);
}

TEST_F(ShaderInterfaceFixture, KeywordFollowsMemberOnlyWhereBindingsApply) {
	ShaderAsset probe;
	if (!probe.Import(Write("probe.hlsl", MATERIAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ShaderAsset asset;
	const str source = "#pragma shalltear_keyword MOSS mossOn\n"
					   "#pragma shalltear_keyword DUST dustOn\n"
					   "#pragma shalltear_default mossOn 1\n"
					   "#pragma shalltear_default dustOn 0\n"
					   "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { int mossOn; int dustOn; float2 pad; };\n"
					   "struct VSOutput { float4 positionCS : SV_Position; };\n"
					   "VSOutput VSMain(uint vertexId : SV_VertexID) { VSOutput output; output.positionCS = float4(0, 0, 0, 1); return output; }\n"
					   "float4 PSMain(VSOutput input) : SV_Target { return float4(mossOn, dustOn, 0, 1); }\n";
	ASSERT_TRUE(asset.Import(Write("toggles.hlsl", source)));
	ASSERT_EQ(asset.keywordBindings.size(), 2u);
	auto sync = [&](MaterialAsset& material) {
		return material.SyncToLayout(asset.GetMaterialParamLayout(), {}, {asset.memberDefaults.data(), asset.memberDefaults.size()});
	};
	auto apply = [&](MaterialAsset& material) {
		material.ApplyKeywordBindings({asset.keywordBindings.data(), asset.keywordBindings.size()});
	};
	auto enabled = [](const MaterialAsset& material, std::string_view keyword) {
		return std::find(material.keywords.begin(), material.keywords.end(), keyword) != material.keywords.end();
	};

	MaterialAsset viewed;
	sync(viewed);
	ASSERT_NE(viewed.FindParam(StringHash("mossOn")), nullptr);
	EXPECT_TRUE(viewed.keywords.empty());

	MaterialAsset fresh;
	sync(fresh);
	apply(fresh);
	EXPECT_TRUE(enabled(fresh, "MOSS"));
	EXPECT_FALSE(enabled(fresh, "DUST"));

	MaterialAsset authored;
	authored.SetInt("mossOn", 0);
	authored.SetInt("dustOn", 1);
	const str held[] = {"MOSS"};
	authored.SetKeywords(held);
	sync(authored);
	apply(authored);
	EXPECT_FALSE(enabled(authored, "MOSS"));
	EXPECT_TRUE(enabled(authored, "DUST"));

	MaterialAsset bare;
	bare.SetKeywords(held);
	apply(bare);
	EXPECT_TRUE(enabled(bare, "MOSS"));
}

TEST_F(ShaderInterfaceFixture, SyncSeedsDeclaredDefaults) {
	ShaderAsset asset;
	str source = str(
					 "#pragma shalltear_default baseColor 1 1 1 1\n"
					 "#pragma shalltear_default specular 0.5\n"
					 "#pragma shalltear_default shininess 32\n") +
				 MATERIAL_SHADER;
	if (!asset.Import(Write("defaults.hlsl", source))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_EQ(asset.memberDefaults.size(), 3u);

	Vector<str> slots{GetMemoryResource(MemTag::Asset)};
	asset.GetMaterialTextureSlots(slots);

	MaterialAsset material;
	material.SetFloat("shininess", 96.0f);
	material.SyncToLayout(asset.GetMaterialParamLayout(), {slots.data(), slots.size()},
						  {asset.memberDefaults.data(), asset.memberDefaults.size()});

	EXPECT_EQ(material.GetColor(StringHash("baseColor")), Color::White());
	EXPECT_FLOAT_EQ(material.GetFloat(StringHash("specular"), -1.0f), 0.5f);
	EXPECT_FLOAT_EQ(material.GetFloat(StringHash("shininess"), -1.0f), 96.0f);
	EXPECT_TRUE(material.FindParam(StringHash("sltReserved")) != nullptr);

	MaterialAsset unseeded;
	unseeded.SyncToLayout(asset.GetMaterialParamLayout(), {slots.data(), slots.size()});
	EXPECT_FLOAT_EQ(unseeded.GetFloat(StringHash("specular"), -1.0f), 0.0f);
}

TEST_F(ShaderInterfaceFixture, SyncReseedsRetypedParamFromDeclaredDefault) {
	ShaderAsset asset;
	str source = str("#pragma shalltear_default specular 0.75\n") + MATERIAL_SHADER;
	if (!asset.Import(Write("retype.hlsl", source))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Vector<str> slots{GetMemoryResource(MemTag::Asset)};
	asset.GetMaterialTextureSlots(slots);

	MaterialAsset material;
	material.SetVec3("specular", Vec3(9.0f, 9.0f, 9.0f));
	material.SyncToLayout(asset.GetMaterialParamLayout(), {slots.data(), slots.size()},
						  {asset.memberDefaults.data(), asset.memberDefaults.size()});
	EXPECT_FLOAT_EQ(material.GetFloat(StringHash("specular"), -1.0f), 0.75f);
}

TEST_F(ShaderInterfaceFixture, UnmatchedDefaultDropped) {
	ShaderAsset asset;
	str source = str(
					 "#pragma shalltear_default baseColor 1 1\n"
					 "#pragma shalltear_default noSuchMember 1\n"
					 "#pragma shalltear_default sltReserved 0.25 0.5\n") +
				 MATERIAL_SHADER;
	if (!asset.Import(Write("mismatch.hlsl", source))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_EQ(asset.memberDefaults.size(), 1u);
	EXPECT_EQ(asset.memberDefaults[0].name, "sltReserved");
}

TEST_F(ShaderInterfaceFixture, SyncNeverDeletesParamsShaderDroppedS) {
	ShaderAsset asset;
	if (!asset.Import(Write("material.hlsl", MATERIAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Vector<str> slots{GetMemoryResource(MemTag::Asset)};
	asset.GetMaterialTextureSlots(slots);

	MaterialAsset material;
	material.SetColor("legacyTint", Color::Magenta());
	material.SyncToLayout(asset.GetMaterialParamLayout(), {slots.data(), slots.size()});

	EXPECT_TRUE(material.HasColor(StringHash("legacyTint")));
	EXPECT_TRUE(MaterialAsset::IsOrphanParam("legacyTint", asset.GetMaterialParamLayout(), {slots.data(), slots.size()}));
	EXPECT_FALSE(MaterialAsset::IsOrphanParam("baseColor", asset.GetMaterialParamLayout(), {slots.data(), slots.size()}));
	EXPECT_FALSE(MaterialAsset::IsOrphanParam("baseColorTexture", asset.GetMaterialParamLayout(), {slots.data(), slots.size()}));
}

TEST(MaterialLayoutTest, UnsupportedMemberTypesDegrade) {
	EXPECT_EQ(UniformMemberTypeName(UniformMemberType::Vec4), "float4");
	EXPECT_EQ(UniformMemberTypeName(UniformMemberType::Unsupported), "unsupported");

	MaterialAsset material;
	UniformBlockLayout layout;
	UniformMember exotic;
	exotic.name = "someMatrix";
	exotic.type = UniformMemberType::Unsupported;
	exotic.offset = 0;
	exotic.sizeBytes = 64;
	layout.members.push_back(exotic);
	layout.sizeBytes = 64;

	EXPECT_EQ(material.SyncToLayout(&layout, {}), 0u);
	EXPECT_FALSE(MaterialAsset::IsOrphanParam("someMatrix", &layout, {}));
}

TEST_F(ShaderInterfaceFixture, EveryReflectedMemberKindPacksAtOffset) {
	ShaderAsset asset;
	if (!asset.Import(Write("everykind.hlsl", EVERY_KIND_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const UniformBlockLayout* layout = asset.GetMaterialParamLayout();
	ASSERT_NE(layout, nullptr);

	MaterialAsset material;
	material.SetColor("baseColor", Color{0.125f, 0.25f, 0.375f, 0.5f});
	material.SetVec2("tiling", Vec2{2.0f, 3.0f});
	material.SetVec3("emissive", Vec3{4.0f, 5.0f, 6.0f});
	material.SetInt("mode", 7);
	const UniformMember* useDetail = MemberNamed(*layout, "useDetail");
	ASSERT_NE(useDetail, nullptr);
	EXPECT_EQ(useDetail->type, UniformMemberType::Int);
	material.SetInt("useDetail", 1);
	material.SetFloat("roughness", 0.75f);

	u8 packed[256] = {};
	ASSERT_LE(layout->sizeBytes, sizeof(packed));
	material.PackBlock(*layout, packed);

	auto floatAt = [&](std::string_view name, u32 component) {
		const UniformMember* member = MemberNamed(*layout, name);
		EXPECT_NE(member, nullptr) << name;
		f32 value = 0.0f;
		if (member) {
			std::memcpy(&value, packed + member->offset + component * sizeof(f32), sizeof(f32));
		}
		return value;
	};
	auto intAt = [&](std::string_view name) {
		const UniformMember* member = MemberNamed(*layout, name);
		EXPECT_NE(member, nullptr) << name;
		i32 value = 0;
		if (member) {
			std::memcpy(&value, packed + member->offset, sizeof(i32));
		}
		return value;
	};

	EXPECT_FLOAT_EQ(floatAt("baseColor", 3), 0.5f);
	EXPECT_FLOAT_EQ(floatAt("tiling", 0), 2.0f);
	EXPECT_FLOAT_EQ(floatAt("tiling", 1), 3.0f);
	EXPECT_FLOAT_EQ(floatAt("emissive", 0), 4.0f);
	EXPECT_FLOAT_EQ(floatAt("emissive", 2), 6.0f);
	EXPECT_EQ(intAt("mode"), 7);
	EXPECT_EQ(intAt("useDetail"), 1);
	EXPECT_FLOAT_EQ(floatAt("roughness", 0), 0.75f);
}

TEST_F(ShaderInterfaceFixture, WrongKindParamPacksZero) {
	ShaderAsset asset;
	if (!asset.Import(Write("everykind.hlsl", EVERY_KIND_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const UniformBlockLayout* layout = asset.GetMaterialParamLayout();
	ASSERT_NE(layout, nullptr);

	MaterialAsset material;
	material.SetFloat("mode", 9.0f);

	u8 packed[256] = {};
	ASSERT_LE(layout->sizeBytes, sizeof(packed));
	material.PackBlock(*layout, packed);

	const UniformMember* member = MemberNamed(*layout, "mode");
	ASSERT_NE(member, nullptr);
	i32 value = -1;
	std::memcpy(&value, packed + member->offset, sizeof(i32));
	EXPECT_EQ(value, 0);

	Vector<str> slots{GetMemoryResource(MemTag::Asset)};
	asset.GetMaterialTextureSlots(slots);
	material.SyncToLayout(layout, {slots.data(), slots.size()});
	const MaterialAsset::Param* retyped = material.FindParam(StringHash("mode"));
	ASSERT_NE(retyped, nullptr);
	EXPECT_EQ(retyped->kind, UniformMemberType::Int);
}

TEST_F(ShaderInterfaceFixture, TextureSlotsReflectedAndSynced) {
	ShaderAsset asset;
	if (!asset.Import(Write("everykind.hlsl", EVERY_KIND_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Vector<str> slots{GetMemoryResource(MemTag::Asset)};
	asset.GetMaterialTextureSlots(slots);
	ASSERT_EQ(slots.size(), 3u);
	EXPECT_EQ(slots[0], "baseColorTexture");
	EXPECT_EQ(slots[1], "normalTexture");
	EXPECT_EQ(slots[2], "emissiveTexture");

	MaterialAsset material;
	material.SyncToLayout(asset.GetMaterialParamLayout(), {slots.data(), slots.size()});
	EXPECT_EQ(material.textures.size(), 3u);
	for (const str& slot : slots) {
		EXPECT_TRUE(material.HasTexture(StringHash(std::string_view{slot}))) << slot;
	}
}

TEST_F(ShaderInterfaceFixture, TooFewSamplersRefused) {
	constexpr const char* MISMATCHED = R"(
[[vk::binding(0, 0)]] cbuffer ViewData { float4x4 viewProjection; };
[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 baseColor; };
[[vk::binding(1, 1)]] Texture2D a;
[[vk::binding(2, 1)]] Texture2D b;
[[vk::binding(3, 1)]] SamplerState only;
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = mul(viewProjection, float4(float(vertexId), 0, 0, 1));
	return output;
}
float4 PSMain(VSOutput input) : SV_Target {
	return (a.Sample(only, float2(0, 0)) + b.Sample(only, float2(0, 0))) * baseColor;
}
)";
	ShaderAsset control;
	if (!control.Import(Write("everykind.hlsl", EVERY_KIND_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ShaderAsset asset;
	EXPECT_FALSE(asset.Import(Write("mismatched.hlsl", MISMATCHED)));
}

TEST(MaterialLayoutTest, LitExposesTheAuthoringVocabulary) {
	std::filesystem::path dir = std::filesystem::current_path();
	std::filesystem::path engineAssets;
	for (u32 i = 0; i < 8; ++i) {
		if (std::filesystem::exists(dir / "EngineAssets" / "Shaders" / "Lit.hlsl")) {
			engineAssets = dir / "EngineAssets";
			break;
		}
		if (!dir.has_parent_path() || dir.parent_path() == dir) {
			break;
		}
		dir = dir.parent_path();
	}
	if (engineAssets.empty()) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	FileSystem::Mount("engine", Path(engineAssets.generic_string()));
	ShaderAsset asset;
	bool imported = asset.Import(Path((engineAssets / "Shaders" / "Lit.hlsl").generic_string()));
	FileSystem::Unmount("engine");
	if (!imported) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	const UniformBlockLayout* layout = asset.GetMaterialParamLayout();
	ASSERT_NE(layout, nullptr);
	EXPECT_EQ(layout->sizeBytes, 48u);

	struct Expected {
		const char* name;
		UniformMemberType type;
		u32 offset;
	};
	const Expected expected[] = {
		{"baseColor", UniformMemberType::Vec4, 0},
		{"metallic", UniformMemberType::Float, 16},
		{"roughness", UniformMemberType::Float, 20},
		{"reflectance", UniformMemberType::Float, 24},
		{"normalScale", UniformMemberType::Float, 28},
		{"emissive", UniformMemberType::Vec3, 32},
		{"occlusionStrength", UniformMemberType::Float, 44},
	};
	ASSERT_EQ(layout->members.size(), std::size(expected));
	for (size_t i = 0; i < std::size(expected); ++i) {
		EXPECT_EQ(layout->members[i].name, expected[i].name) << "member " << i;
		EXPECT_EQ(layout->members[i].type, expected[i].type) << expected[i].name;
		EXPECT_EQ(layout->members[i].offset, expected[i].offset) << expected[i].name;
	}

	EXPECT_TRUE(asset.keywordBindings.empty());

	ASSERT_EQ(asset.memberDefaults.size(), std::size(expected));
	auto declaredValue = [&](const char* name) {
		for (const ShaderMemberDefault& declared : asset.memberDefaults) {
			if (declared.name == name) {
				return declared.numbers[0];
			}
		}
		ADD_FAILURE() << "no declared default for " << name;
		return 0.0f;
	};
	EXPECT_NEAR(declaredValue("metallic"), 0.0f, 1e-6f);
	EXPECT_NEAR(declaredValue("roughness"), 0.5f, 1e-6f);
	EXPECT_NEAR(declaredValue("reflectance"), 0.5f, 1e-6f);
	EXPECT_NEAR(declaredValue("normalScale"), 1.0f, 1e-6f);
	EXPECT_NEAR(declaredValue("occlusionStrength"), 1.0f, 1e-6f);

	const char* ranged[] = {"metallic", "roughness", "reflectance", "occlusionStrength", "normalScale"};
	for (const char* name : ranged) {
		const ShaderMemberRange* range = asset.FindMemberRange(name);
		ASSERT_NE(range, nullptr) << name;
		EXPECT_FLOAT_EQ(range->minValue, 0.0f) << name;
	}
	EXPECT_EQ(asset.FindMemberHint("baseColor"), ShaderHint::Color);
	EXPECT_EQ(asset.FindMemberHint("emissive"), ShaderHint::HDR);

	ASSERT_EQ(asset.memberGroups.size(), 3u);
	EXPECT_EQ(asset.memberGroups[0].name, "Surface Inputs");
	EXPECT_EQ(asset.memberGroups[1].name, "Detail Inputs");
	EXPECT_EQ(asset.memberGroups[2].name, "Emission");
	EXPECT_EQ(asset.memberGroups[0].members.size(), 6u);
	EXPECT_EQ(asset.memberGroups[1].members.size(), 4u);
	EXPECT_EQ(asset.memberGroups[2].members.size(), 2u);
}

TEST(MaterialKeywordTest, KeywordHashMatchesShader) {
	MaterialAsset material;
	EXPECT_EQ(material.keywordHash, ShaderAsset::KeywordSetHash({}));

	Vector<str> enabled{GetMemoryResource(MemTag::Asset)};
	enabled.push_back(str("USE_B"));
	enabled.push_back(str("USE_A"));
	enabled.push_back(str("USE_A"));
	material.SetKeywords({enabled.data(), enabled.size()});

	ASSERT_EQ(material.keywords.size(), 2u);
	EXPECT_EQ(material.keywords[0], "USE_A");
	EXPECT_EQ(material.keywords[1], "USE_B");
	EXPECT_EQ(material.keywordHash, ShaderAsset::KeywordSetHash({material.keywords.data(), material.keywords.size()}));

	material.SetKeywords({});
	EXPECT_TRUE(material.keywords.empty());
	EXPECT_EQ(material.keywordHash, ShaderAsset::KeywordSetHash({}));
}

TEST_F(ShaderInterfaceFixture, KeywordSetRoundTrips) {
	MaterialAsset plain;
	plain.SetColor("baseColor", Color::White());
	Path plainPath = Write("plain.mat", str(""));
	ASSERT_TRUE(plain.Export(plainPath));
	str plainText = FileSystem::ReadTextFile(plainPath, MemTag::Asset);
	EXPECT_EQ(plainText.find("keywords"), str::npos);

	MaterialAsset featured;
	featured.SetColor("baseColor", Color::White());
	Vector<str> enabled{GetMemoryResource(MemTag::Asset)};
	enabled.push_back(str("USE_NORMAL_MAP"));
	enabled.push_back(str("ALPHA_CLIP"));
	featured.SetKeywords({enabled.data(), enabled.size()});
	Path featuredPath = Write("featured.mat", str(""));
	ASSERT_TRUE(featured.Export(featuredPath));

	MaterialAsset reloaded;
	ASSERT_TRUE(reloaded.Import(featuredPath));
	ASSERT_EQ(reloaded.keywords.size(), 2u);
	EXPECT_EQ(reloaded.keywords[0], "ALPHA_CLIP");
	EXPECT_EQ(reloaded.keywords[1], "USE_NORMAL_MAP");
	EXPECT_EQ(reloaded.keywordHash, featured.keywordHash);

	Path again = Write("again.mat", str(""));
	ASSERT_TRUE(reloaded.Export(again));
	EXPECT_EQ(FileSystem::ReadTextFile(again, MemTag::Asset), FileSystem::ReadTextFile(featuredPath, MemTag::Asset));
}

TEST_F(ShaderInterfaceFixture, UnmatchedAnnotationDropped) {
	ShaderAsset asset;
	str source = str(
					 "#pragma shalltear_range specular 0 1\n"
					 "#pragma shalltear_range baseColor 0 1\n"
					 "#pragma shalltear_range noSuchMember 0 1\n"
					 "#pragma shalltear_hint baseColor color\n"
					 "#pragma shalltear_hint specular color\n"
					 "#pragma shalltear_keyword RIM specular\n"
					 "#pragma shalltear_keyword GLOW baseColor\n") +
				 MATERIAL_SHADER;
	if (!asset.Import(Write("annotations.hlsl", source))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	ASSERT_EQ(asset.memberRanges.size(), 1u);
	EXPECT_EQ(asset.memberRanges[0].name, "specular");
	ASSERT_EQ(asset.memberHints.size(), 1u);
	EXPECT_EQ(asset.memberHints[0].name, "baseColor");

	ASSERT_EQ(asset.keywords.size(), 2u);
	ASSERT_EQ(asset.keywordBindings.size(), 1u);
	EXPECT_EQ(asset.keywordBindings[0].keyword, "RIM");
	EXPECT_EQ(asset.FindKeywordMember("GLOW"), "");
}

TEST_F(ShaderInterfaceFixture, KeywordGroupTurnsOnEntryMemberHolds) {
	ShaderAsset probe;
	if (!probe.Import(Write("probe.hlsl", MATERIAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ShaderAsset asset;
	const str source = "#pragma shalltear_keyword MODE_A mode 0 \"Alpha One\"\n"
					   "#pragma shalltear_keyword MODE_B mode 1 Beta\n"
					   "#pragma shalltear_keyword MODE_C mode 2\n"
					   "#pragma shalltear_default mode 1\n"
					   "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { int mode; float3 pad; };\n"
					   "struct VSOutput { float4 positionCS : SV_Position; };\n"
					   "VSOutput VSMain(uint vertexId : SV_VertexID) { VSOutput output; output.positionCS = float4(0, 0, 0, 1); return output; }\n"
					   "float4 PSMain(VSOutput input) : SV_Target { return float4(mode, 0, 0, 1); }\n";
	ASSERT_TRUE(asset.Import(Write("group.hlsl", source)));
	ASSERT_EQ(asset.keywordBindings.size(), 3u);
	EXPECT_EQ(asset.keywordBindings[0].value, 0);
	EXPECT_EQ(asset.keywordBindings[0].entry, "Alpha One");
	EXPECT_EQ(asset.keywordBindings[1].entry, "Beta");
	EXPECT_EQ(asset.keywordBindings[2].value, 2);
	EXPECT_TRUE(asset.keywordBindings[2].entry.empty());

	auto keywordsOf = [&](i32 mode) {
		MaterialAsset material;
		material.SyncToLayout(asset.GetMaterialParamLayout(), {}, {asset.memberDefaults.data(), asset.memberDefaults.size()});
		if (mode >= 0) {
			material.SetInt("mode", mode);
		}
		const str every[] = {"MODE_A", "MODE_B", "MODE_C"};
		material.SetKeywords(every);
		material.ApplyKeywordBindings({asset.keywordBindings.data(), asset.keywordBindings.size()});
		return material.keywords;
	};
	EXPECT_EQ(keywordsOf(-1), (Vector<str>{std::initializer_list<str>{"MODE_B"}, GetMemoryResource(MemTag::Asset)}));
	EXPECT_EQ(keywordsOf(2), (Vector<str>{std::initializer_list<str>{"MODE_C"}, GetMemoryResource(MemTag::Asset)}));
	EXPECT_EQ(keywordsOf(0), (Vector<str>{std::initializer_list<str>{"MODE_A"}, GetMemoryResource(MemTag::Asset)}));
}

TEST_F(ShaderInterfaceFixture, PartialKeywordGroupDropped) {
	ShaderAsset asset;
	const str source = str("#pragma shalltear_keyword GAP_A specular 0\n"
						   "#pragma shalltear_keyword GAP_B specular 2\n"
						   "#pragma shalltear_keyword MIX_A shininess 0\n"
						   "#pragma shalltear_keyword MIX_B shininess 1\n"
						   "#pragma shalltear_keyword MIX_T shininess\n"
						   "#pragma shalltear_keyword BAD specular x\n") +
					   MATERIAL_SHADER;
	if (!asset.Import(Write("broken-groups.hlsl", source))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	EXPECT_TRUE(asset.keywordBindings.empty());
	EXPECT_EQ(std::count(asset.keywords.begin(), asset.keywords.end(), "GAP_A"), 1);
	EXPECT_EQ(std::count(asset.keywords.begin(), asset.keywords.end(), "MIX_B"), 1);
	EXPECT_EQ(std::count(asset.keywords.begin(), asset.keywords.end(), "BAD"), 0);

	ShaderAsset lone;
	ASSERT_TRUE(lone.Import(Write("lone-entry.hlsl", str("#pragma shalltear_keyword ONE_A specular 0\n") + MATERIAL_SHADER)));
	EXPECT_TRUE(lone.keywordBindings.empty());
	EXPECT_EQ(lone.keywords.size(), 1u);
}

TEST_F(ShaderInterfaceFixture, GroupDropsMissingMember) {
	ShaderAsset asset;
	str source = str(
					 "#pragma shalltear_group Surface baseColor gone baseColorTexture\n"
					 "#pragma shalltear_group Empty alsoGone\n") +
				 MATERIAL_SHADER;
	if (!asset.Import(Write("groups.hlsl", source))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	ASSERT_EQ(asset.memberGroups.size(), 1u);
	EXPECT_EQ(asset.memberGroups[0].name, "Surface");
	ASSERT_EQ(asset.memberGroups[0].members.size(), 2u);
	EXPECT_EQ(asset.memberGroups[0].members[0], "baseColor");
	EXPECT_EQ(asset.memberGroups[0].members[1], "baseColorTexture");
}

TEST_F(ShaderInterfaceFixture, AnnotationsValidatedAgainstBlock) {
	ShaderAsset asset;
	str source = str(
		"#pragma shalltear_label baseColor \"Base Tint\"\n"
		"#pragma shalltear_label baseColorTexture Map\n"
		"#pragma shalltear_label gone \"Nothing\"\n"
		"#pragma shalltear_label specular\n"
		"#pragma shalltear_hint useRim toggle\n"
		"#pragma shalltear_hint specular toggle\n"
		"#pragma shalltear_hint baseColorTexture hidden\n"
		"#pragma shalltear_hint baseColor color\n"
		"#pragma shalltear_hint baseColor hidden\n"
		"#pragma shalltear_hint baseColor sideways\n"
		R"(
[[vk::binding(0, 0)]] cbuffer ViewData { float4x4 viewProjection; };
[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 baseColor;
	float specular;
	int useRim;
	float2 sltReserved;
};
[[vk::binding(1, 1)]] Texture2D baseColorTexture;
[[vk::binding(2, 1)]] SamplerState baseColorSampler;
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = mul(viewProjection, float4(float(vertexId), 0, 0, 1));
	return output;
}
float4 PSMain(VSOutput input) : SV_Target {
	return baseColorTexture.Sample(baseColorSampler, float2(0, 0)) * baseColor * specular * (float)useRim;
}
)");
	if (!asset.Import(Write("labels.hlsl", source))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	ASSERT_EQ(asset.memberLabels.size(), 2u);
	EXPECT_EQ(asset.FindMemberLabel("baseColor"), "Base Tint");
	EXPECT_EQ(asset.FindMemberLabel("baseColorTexture"), "Map");
	EXPECT_EQ(asset.FindMemberLabel("specular"), "");

	EXPECT_EQ(asset.FindMemberHint("useRim"), ShaderHint::Toggle);
	EXPECT_EQ(asset.FindMemberHint("specular"), ShaderHint::None);
	EXPECT_EQ(asset.FindMemberHint("baseColorTexture"), ShaderHint::Hidden);
	EXPECT_EQ(asset.FindMemberHint("baseColor"), ShaderHint::Color);
	EXPECT_EQ(asset.memberHints.size(), 3u);
}

TEST(ShaderAdoptTest, AdoptCarriesImportedFields) {
	ShaderAsset scratch;
	ShaderVariant& base = scratch.variants.emplace_back();
	base.keywordHash = 0;
	base.vertexSpirv.push_back(0x11);
	base.pixelSpirv.push_back(0x22);
	scratch.keywords.push_back("RIM_LIGHT");
	scratch.bindings.push_back({1, 0, RHI::DescriptorType::UniformBuffer, RHI::ShaderStage::Pixel});
	scratch.pushConstantSize = 24;
	UniformBlockLayout& block = scratch.uniformBlocks.emplace_back();
	block.set = MATERIAL_DESCRIPTOR_SET;
	block.sizeBytes = 16;
	block.members.push_back({"tint", UniformMemberType::Vec4, 0, 16});
	scratch.resources.push_back({1, 1, RHI::DescriptorType::SampledTexture, str("tintTexture")});
	scratch.renderState.blend = RHI::BlendMode::Additive;
	scratch.renderState.cull = RHI::CullMode::Off;
	scratch.integrator = ShaderTemplate::Unlit;
	scratch.memberDefaults.push_back({"tint", {1.0f, 0.0f, 0.0f, 1.0f}, 4});
	scratch.textureDefaults.push_back({"tintTexture", ShaderTextureFallback::Black});
	scratch.memberRanges.push_back({"tint", 0.0f, 2.0f});
	scratch.memberHints.push_back({"tint", ShaderHint::HDR});
	ShaderMemberGroup group;
	group.name = "Surface Inputs";
	group.members.push_back("tint");
	scratch.memberGroups.push_back(std::move(group));
	scratch.keywordBindings.push_back({"RIM_LIGHT", "tint"});
	scratch.memberLabels.push_back({"tint", "Rim Tint"});
	scratch.dependencies.push_back({"C:/proj/rim.hlsl", 0xFEED});
	scratch.skipOptimizations = true;

	ShaderAsset live;
	live.variants.emplace_back();
	live.keywords.push_back("STALE");
	live.pushConstantSize = 4;
	live.uniformBlocks.emplace_back().sizeBytes = 999;
	live.renderState.blend = RHI::BlendMode::Opaque;
	live.integrator = ShaderTemplate::Lit;
	live.memberDefaults.push_back({"gone", {}, 1});
	live.textureDefaults.push_back({"gone", ShaderTextureFallback::White});
	live.memberRanges.push_back({"gone", 0.0f, 1.0f});
	live.memberHints.push_back({"gone", ShaderHint::Color});
	live.keywordBindings.push_back({"STALE", "gone"});
	live.memberLabels.push_back({"gone", "Gone"});
	live.dependencies.push_back({"C:/proj/old.hlsl", 0xDEAD});
	live.version = 7;

	live.AdoptImportedPayload(scratch);

	ASSERT_EQ(live.variants.size(), 1u);
	ASSERT_EQ(live.variants[0].pixelSpirv.size(), 1u);
	EXPECT_EQ(live.variants[0].pixelSpirv[0], 0x22);
	ASSERT_EQ(live.keywords.size(), 1u);
	EXPECT_EQ(live.keywords[0], "RIM_LIGHT");
	ASSERT_EQ(live.bindings.size(), 1u);
	EXPECT_EQ(live.pushConstantSize, 24u);
	ASSERT_EQ(live.uniformBlocks.size(), 1u);
	EXPECT_EQ(live.uniformBlocks[0].sizeBytes, 16u);
	ASSERT_EQ(live.resources.size(), 1u);
	EXPECT_EQ(live.resources[0].name, "tintTexture");
	EXPECT_EQ(live.renderState.blend, RHI::BlendMode::Additive);
	EXPECT_EQ(live.renderState.cull, RHI::CullMode::Off);
	EXPECT_EQ(live.integrator, ShaderTemplate::Unlit);
	EXPECT_TRUE(live.skipOptimizations);
	ASSERT_EQ(live.memberDefaults.size(), 1u);
	EXPECT_EQ(live.memberDefaults[0].name, "tint");
	ASSERT_EQ(live.textureDefaults.size(), 1u);
	EXPECT_EQ(live.textureDefaults[0].fallback, ShaderTextureFallback::Black);
	ASSERT_NE(live.FindMemberRange("tint"), nullptr);
	EXPECT_FLOAT_EQ(live.FindMemberRange("tint")->maxValue, 2.0f);
	EXPECT_EQ(live.FindMemberHint("tint"), ShaderHint::HDR);
	ASSERT_EQ(live.memberGroups.size(), 1u);
	EXPECT_EQ(live.memberGroups[0].name, "Surface Inputs");
	EXPECT_EQ(live.FindKeywordMember("RIM_LIGHT"), "tint");
	EXPECT_EQ(live.FindMemberLabel("tint"), "Rim Tint");
	EXPECT_EQ(live.FindMemberRange("gone"), nullptr);
	EXPECT_EQ(live.FindMemberHint("gone"), ShaderHint::None);
	EXPECT_EQ(live.FindMemberLabel("gone"), "");
	ASSERT_EQ(live.dependencies.size(), 1u);
	EXPECT_EQ(live.dependencies[0].contentHash, 0xFEEDu);

	EXPECT_EQ(live.version, 7u);
}

TEST_F(ShaderInterfaceFixture, MeasureVariantCompileCostOnTheToggle) {
	ShaderAsset asset;
	str source = str("#pragma shalltear_keyword RIM\n") + MATERIAL_SHADER;
	if (!asset.Import(Write("cost.hlsl", source))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const str enabled[] = {str("RIM")};
	auto begin = std::chrono::steady_clock::now();
	u32 first = asset.GetOrCompileVariant({enabled, 1});
	auto mid = std::chrono::steady_clock::now();
	u32 again = asset.GetOrCompileVariant({enabled, 1});
	auto end = std::chrono::steady_clock::now();
	EXPECT_NE(first, SHADER_BASE_VARIANT);
	EXPECT_EQ(first, again);
	GTEST_LOG_(INFO) << "MEASURE first compile "
					 << std::chrono::duration_cast<std::chrono::microseconds>(mid - begin).count() / 1000.0
					 << " ms, already-compiled lookup "
					 << std::chrono::duration_cast<std::chrono::microseconds>(end - mid).count() / 1000.0 << " ms";
}
