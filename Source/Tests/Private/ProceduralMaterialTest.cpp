#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"

#include <bit>
#include <fstream>
#include <span>

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	void WriteSolidTGA(const std::filesystem::path& path, u8 r, u8 g, u8 b) {
		constexpr u16 SIDE = 4;
		u8 header[18] = {};
		header[2] = 2;
		header[12] = static_cast<u8>(SIDE & 0xFF);
		header[13] = static_cast<u8>(SIDE >> 8);
		header[14] = static_cast<u8>(SIDE & 0xFF);
		header[15] = static_cast<u8>(SIDE >> 8);
		header[16] = 24;
		std::ofstream file(path, std::ios::binary);
		file.write(reinterpret_cast<const char*>(header), sizeof(header));
		for (int i = 0; i < SIDE * SIDE; ++i) {
			const u8 bgr[3] = {b, g, r};
			file.write(reinterpret_cast<const char*>(bgr), 3);
		}
	}

	constexpr const char* ATLAS_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"#pragma shalltear_default tint 1 1 1 1\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"[[vk::binding(1, 1)]] Texture2D atlasTexture;\n"
		"[[vk::binding(2, 1)]] SamplerState atlasSampler;\n"
		"struct VSOutput { float4 positionCS : SV_Position; float2 uv : TEXCOORD0; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tVSOutput output;\n"
		"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
		"\toutput.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
		"\toutput.uv = corner;\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target {\n"
		"\treturn float4(atlasTexture.Sample(atlasSampler, input.uv).rgb * tint.rgb, 1);\n"
		"}\n";

	constexpr const char* BAND_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"#pragma shalltear_default tint 1 1 1 1\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {\n"
		"\tVSOutput output;\n"
		"\tfloat x = (vertexId == 1 || vertexId == 3 || vertexId == 4) ? 1.0 : 0.0;\n"
		"\tfloat y = (vertexId == 2 || vertexId == 4 || vertexId == 5) ? 1.0 : 0.0;\n"
		"\tfloat bands = 4.0;\n"
		"\tfloat y0 = -1.0 + (float(instanceId) / bands) * 2.0;\n"
		"\tfloat y1 = -1.0 + ((float(instanceId) + 1.0) / bands) * 2.0;\n"
		"\toutput.positionCS = float4(x * 2.0 - 1.0, lerp(y0, y1, y), 0.0, 1.0);\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return float4(tint.rgb, 1); }\n";

	constexpr const char* OFFSET_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"#pragma shalltear_default tint 1 1 1 1\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"struct ShalltearOffsetPush { float2 offset; };\n"
		"[[vk::push_constant]] ShalltearOffsetPush gPush;\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tVSOutput output;\n"
		"\tfloat x = (vertexId == 1 || vertexId == 3 || vertexId == 4) ? 1.0 : 0.0;\n"
		"\tfloat y = (vertexId == 2 || vertexId == 4 || vertexId == 5) ? 1.0 : 0.0;\n"
		"\toutput.positionCS = float4(x - 1.0 + gPush.offset.x, y - 1.0 + gPush.offset.y, 0.0, 1.0);\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return float4(tint.rgb, 1); }\n";

	struct OffsetPush {
		f32 x = 0.0f;
		f32 y = 0.0f;
	};

	class MaterialDrawRow : public RenderPass {
	public:
		Handle<MaterialAsset> material;
		std::span<const u8> push;
		u32 vertexCount = 3;
		u32 instanceCount = 1;
		bool drew = false;

		void Record(PassContext& context) override {
			drew = context.DrawProcedural(material, push, vertexCount, instanceCount);
		}
	};

	class ProceduralMaterialFixture : public MaterialReadbackFixture {
	protected:
		static constexpr Vec2I SIZE{64, 64};
		ViewTargetId _target = INVALID_VIEW_TARGET;

		void TearDown() override {
			if (_target != INVALID_VIEW_TARGET) {
				RenderSystem::DestroyViewTarget(_target);
				_target = INVALID_VIEW_TARGET;
			}
			MaterialReadbackFixture::TearDown();
		}

		bool Ready() {
			if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
				return false;
			}
			_target = RenderSystem::CreateViewTarget(SIZE);
			return _target != INVALID_VIEW_TARGET;
		}

		Guid AddShader(const char* fileName, const char* source) {
			WriteProjectFile(fileName, source);
			Asset* asset = AssetDatabase::LoadAsset(Path(str("project://") + fileName));
			return asset ? asset->assetId : Guid{};
		}

		bool Render(MaterialDrawRow& row, Vector<u8>& pixels, Vec2I& size) {
			const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
			ViewDesc desc = QuadView();
			desc.clearColor = Color::Black();
			desc.passes = rows;
			RenderSystem::SubmitView(_target, desc);
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTarget(_target, pixels, size);
		}

		static Color32 TexelAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
			const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
			return Color32(pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]);
		}
	};

} // namespace

TEST_F(ProceduralMaterialFixture, MaterialReachesProceduralDraw) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	WriteSolidTGA(_root / "Atlas.tga", 255, 255, 0);
	Asset* textureAsset = AssetDatabase::LoadAsset(Path("project://Atlas.tga"));
	ASSERT_NE(textureAsset, nullptr);
	const Guid shader = AddShader("Atlas.hlsl", ATLAS_SHADER);
	ASSERT_TRUE(shader.IsValid());

	MaterialAsset* material = LoadMaterial("Atlas.mat", shader,
										   "\t\"colors\": { \"tint\": [1, 0, 1, 1] },\n"
										   "\t\"textures\": { \"atlasTexture\": \"" +
											   textureAsset->assetId.ToString() + "\" }");
	ASSERT_NE(material, nullptr);

	MaterialDrawRow row;
	row.material = Handle<MaterialAsset>(material);
	row.vertexCount = 3;
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(Render(row, pixels, size));
	EXPECT_TRUE(row.drew);

	const Color32 texel = TexelAt(pixels, size, size.x / 2, size.y / 2);
	EXPECT_GT(texel.r, 200);
	EXPECT_LT(texel.g, 55);
	EXPECT_LT(texel.b, 55);
}

TEST_F(ProceduralMaterialFixture, PushAndMaterialShareDraw) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	const Guid shader = AddShader("Offset.hlsl", OFFSET_SHADER);
	ASSERT_TRUE(shader.IsValid());
	MaterialAsset* material = LoadMaterial("Offset.mat", shader, "\t\"colors\": { \"tint\": [0, 1, 0, 1] }");
	ASSERT_NE(material, nullptr);

	OffsetPush push{0.0f, 0.0f};
	const auto* bytes = reinterpret_cast<const u8*>(&push);
	MaterialDrawRow row;
	row.material = Handle<MaterialAsset>(material);
	row.vertexCount = 6;
	row.push = {bytes, sizeof(OffsetPush)};

	auto paintedQuadrants = [&]() {
		Vector<u8> pixels;
		Vec2I size{};
		EXPECT_TRUE(Render(row, pixels, size));
		u32 mask = 0;
		for (int quadrant = 0; quadrant < 4; ++quadrant) {
			const i32 x = (quadrant & 1) ? (size.x * 3) / 4 : size.x / 4;
			const i32 y = (quadrant & 2) ? (size.y * 3) / 4 : size.y / 4;
			if (TexelAt(pixels, size, x, y).g > 200) {
				mask |= 1u << quadrant;
			}
		}
		return mask;
	};

	const u32 before = paintedQuadrants();
	EXPECT_TRUE(row.drew);
	push = OffsetPush{1.0f, 1.0f};
	const u32 after = paintedQuadrants();

	EXPECT_EQ(std::popcount(before), 1);
	EXPECT_EQ(std::popcount(after), 1);
	EXPECT_NE(before, after);
}

TEST_F(ProceduralMaterialFixture, InstanceCountDecidesBands) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	const Guid shader = AddShader("Bands.hlsl", BAND_SHADER);
	ASSERT_TRUE(shader.IsValid());
	MaterialAsset* material = LoadMaterial("Bands.mat", shader, "\t\"colors\": { \"tint\": [0, 1, 0, 1] }");
	ASSERT_NE(material, nullptr);

	MaterialDrawRow row;
	row.material = Handle<MaterialAsset>(material);
	row.vertexCount = 6;

	auto paintedBands = [&](u32 instances) {
		row.instanceCount = instances;
		Vector<u8> pixels;
		Vec2I size{};
		EXPECT_TRUE(Render(row, pixels, size));
		int painted = 0;
		for (int band = 0; band < 4; ++band) {
			const i32 y = static_cast<i32>((band * size.y) / 4 + size.y / 8);
			if (TexelAt(pixels, size, size.x / 2, y).g > 200) {
				++painted;
			}
		}
		return painted;
	};

	EXPECT_EQ(paintedBands(1), 1);
	EXPECT_EQ(paintedBands(4), 4);
}

TEST_F(ProceduralMaterialFixture, ZeroOfEitherCountDrawsNothing) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	const Guid shader = AddShader("Bands.hlsl", BAND_SHADER);
	ASSERT_TRUE(shader.IsValid());
	MaterialAsset* material = LoadMaterial("Bands.mat", shader, "\t\"colors\": { \"tint\": [0, 1, 0, 1] }");
	ASSERT_NE(material, nullptr);

	Vector<u8> pixels;
	Vec2I size{};
	MaterialDrawRow row;
	row.material = Handle<MaterialAsset>(material);

	row.vertexCount = 0;
	row.instanceCount = 1;
	ASSERT_TRUE(Render(row, pixels, size));
	EXPECT_FALSE(row.drew);

	row.vertexCount = 6;
	row.instanceCount = 0;
	ASSERT_TRUE(Render(row, pixels, size));
	EXPECT_FALSE(row.drew);
}

TEST_F(ProceduralMaterialFixture, MaterialDeclaringPerViewResourcesIsRefused) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlit, nullptr);
	MaterialAsset* material = LoadMaterial("Templated.mat", unlit->assetId, {});
	ASSERT_NE(material, nullptr);

	MaterialDrawRow row;
	row.material = Handle<MaterialAsset>(material);
	row.vertexCount = 3;
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(Render(row, pixels, size));
	EXPECT_FALSE(row.drew);
}

TEST_F(ProceduralMaterialFixture, OversizedPushRejected) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	const Guid shader = AddShader("Offset.hlsl", OFFSET_SHADER);
	ASSERT_TRUE(shader.IsValid());
	MaterialAsset* material = LoadMaterial("Offset.mat", shader, "\t\"colors\": { \"tint\": [0, 1, 0, 1] }");
	ASSERT_NE(material, nullptr);

	const f32 tooMany[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	MaterialDrawRow row;
	row.material = Handle<MaterialAsset>(material);
	row.vertexCount = 6;
	row.push = {reinterpret_cast<const u8*>(tooMany), sizeof(tooMany)};

	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(Render(row, pixels, size));
	EXPECT_FALSE(row.drew);
}
