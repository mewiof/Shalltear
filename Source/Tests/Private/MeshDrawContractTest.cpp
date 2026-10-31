#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Jobs/JobSystem.h"

#include <algorithm>
#include <memory>
#include <string_view>

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	constexpr const char* GREEN_TINT = "\t\"colors\": { \"tint\": [0, 1, 0, 1] }";

	constexpr const char* PASS_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"struct PassPush { float4x4 placement; float4 origin; };\n"
		"[[vk::push_constant]] PassPush gPass;\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = mul(gPass.placement, float4(corner * 2.0 - 1.0, 0.0, 1.0)) + gPass.origin;\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return float4(tint.rgb, 1); }\n";

	constexpr const char* NO_VIEW_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"struct OffsetPush { float2 offset; float2 unused; };\n"
		"[[vk::push_constant]] OffsetPush gPush;\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = float4(corner * 2.0 - 1.0 + gPush.offset, 0.0, 1.0);\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return float4(tint.rgb, 1); }\n";

	constexpr const char* CONTRACT_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull off\n"
		"#include \"ShalltearCommon.hlsli\"\n"
		"[[vk::push_constant]] ShalltearDrawHeader gDraw;\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tShalltearPerDraw draw = ShalltearLoadPerDraw(gDraw.drawAddress);\n"
		"\tShalltearVertex vertex = ShalltearLoadVertex(draw.vertexAddress, vertexId);\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = mul(sltViewProjection, ShalltearTransformPosition(draw, vertex.position));\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return float4(tint.rgb, 1); }\n";

	constexpr const char* WIDE_PUSH_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull off\n"
		"#include \"ShalltearCommon.hlsli\"\n"
		"struct WidePush { uint64_t drawAddress; float2 pad; float4 color; };\n"
		"[[vk::push_constant]] WidePush gPush;\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tShalltearPerDraw draw = ShalltearLoadPerDraw(gPush.drawAddress);\n"
		"\tShalltearVertex vertex = ShalltearLoadVertex(draw.vertexAddress, vertexId);\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = mul(sltViewProjection, ShalltearTransformPosition(draw, vertex.position));\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return float4(tint.rgb * gPush.color.rgb, 1); }\n";

	constexpr const char* PASS_INPUT_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull off\n"
		"#include \"ShalltearCommon.hlsli\"\n"
		"[[vk::push_constant]] ShalltearDrawHeader gDraw;\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"[[vk::binding(0, 2)]] Texture2D sltSceneColor;\n"
		"[[vk::binding(1, 2)]] SamplerState sltSceneSampler;\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tShalltearPerDraw draw = ShalltearLoadPerDraw(gDraw.drawAddress);\n"
		"\tShalltearVertex vertex = ShalltearLoadVertex(draw.vertexAddress, vertexId);\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = mul(sltViewProjection, ShalltearTransformPosition(draw, vertex.position));\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target {\n"
		"\tfloat4 color = float4(tint.rgb, 1);\n"
		"\tif (tint.a < 0.0) {\n"
		"\t\tcolor = sltSceneColor.Sample(sltSceneSampler, float2(0.5, 0.5));\n"
		"\t}\n"
		"\treturn color;\n"
		"}\n";

	bool DeclaresTheViewBlock(const ShaderAsset& shader) {
		return std::any_of(shader.bindings.begin(), shader.bindings.end(),
						   [](const RHI::DescriptorBindingDesc& b) { return b.set == 0 && b.binding == 0; });
	}

	bool DeclaresPassInputs(const ShaderAsset& shader) {
		return std::any_of(shader.bindings.begin(), shader.bindings.end(),
						   [](const RHI::DescriptorBindingDesc& b) { return b.set == 2; });
	}

	size_t Occurrences(const str& haystack, std::string_view needle) {
		size_t count = 0;
		for (size_t at = haystack.find(needle); at != str::npos; at = haystack.find(needle, at + needle.size())) {
			++count;
		}
		return count;
	}

	constexpr std::string_view REFUSAL = "can't be drawn on a mesh";

	class ScopedLogCapture {
	public:
		ScopedLogCapture() : _sink(std::make_shared<ShaderDiagnosticSink>()) {
			JobSystem::Initialize();
			LogManager::Initialize({false, false});
			LogManager::SetGlobalLevel(LogLevel::Trace);
			LogManager::AddSink(_sink);
			_armed = ArmDiagnosticSink(_sink);
		}
		~ScopedLogCapture() { Finish(); }

		bool Armed() const { return _armed; }

		str Finish() {
			if (!_finished) {
				_finished = true;
				LogManager::Terminate();
				_captured = _sink->Joined();
				_sink.reset();
				JobSystem::Terminate();
			}
			return _captured;
		}
	private:
		std::shared_ptr<ShaderDiagnosticSink> _sink;
		str _captured;
		bool _armed = false;
		bool _finished = false;
	};

	class MeshDrawContractFixture : public MaterialReadbackFixture {
	protected:
		ShadingStage _stage;
		MaterialAsset* _missing = nullptr;

		void TearDown() override {
			TearDownStage(_stage);
			MaterialReadbackFixture::TearDown();
		}

		bool Ready() {
			if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
				return false;
			}
			_missing = LoadMaterial("Missing.mat", Guid{}, {});
			return _missing && BuildStage(_missing, _stage);
		}

		ShaderAsset* AddShader(const char* fileName, const char* source) {
			WriteProjectFile(fileName, source);
			auto* shader = static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + fileName)));
			return shader && !shader->variants.empty() ? shader : nullptr;
		}

		void Edit(const char* fileName, const char* source) {
			WriteProjectFile(fileName, source);
			RenderSystem::NotifyShaderFileChanged(str((_root / fileName).generic_string()));
		}

		Vec3 DrawnWith(MaterialAsset* material) {
			_stage.quad->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(material));
			Vector<u8> pixels;
			Vec2I size{};
			if (!RenderStage(_stage, pixels, size) || pixels.empty()) {
				ADD_FAILURE() << "frame readback failed";
				return {};
			}
			return CenterTexel(pixels, size);
		}

		Vec3 ErrorMaterial() {
			const Vec3 reference = DrawnWith(_missing);
			EXPECT_GT(reference.x + reference.y + reference.z, 30.0f);
			return reference;
		}

		static void ExpectTheErrorMaterial(const Vec3& texel, const Vec3& reference, const char* why) {
			EXPECT_NEAR(texel.x, reference.x, 2.0f) << why;
			EXPECT_NEAR(texel.y, reference.y, 2.0f) << why;
			EXPECT_NEAR(texel.z, reference.z, 2.0f) << why;
		}

		static void ExpectGreen(const Vec3& texel, const char* why) {
			EXPECT_GT(texel.y, 200.0f) << why;
			EXPECT_LT(texel.x, 55.0f) << why;
			EXPECT_LT(texel.z, 55.0f) << why;
		}
	};

} // namespace

TEST_F(MeshDrawContractFixture, PassShaderOnMeshIsError) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Pass.hlsl", PASS_SHADER);
	ASSERT_NE(shader, nullptr);
	ASSERT_FALSE(DeclaresTheViewBlock(*shader));
	ASSERT_GT(shader->pushConstantSize, 16u);
	MaterialAsset* material = LoadMaterial("Pass.mat", shader->assetId, GREEN_TINT);
	ASSERT_NE(material, nullptr);

	const Vec3 reference = ErrorMaterial();
	ExpectTheErrorMaterial(DrawnWith(material), reference, "a material written for a pass drew on a mesh as itself");
}

TEST_F(MeshDrawContractFixture, ViewlessShaderRefused) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("NoView.hlsl", NO_VIEW_SHADER);
	ASSERT_NE(shader, nullptr);
	ASSERT_FALSE(DeclaresTheViewBlock(*shader));
	ASSERT_EQ(shader->pushConstantSize, 16u);
	ASSERT_FALSE(DeclaresPassInputs(*shader));
	MaterialAsset* material = LoadMaterial("NoView.mat", shader->assetId, GREEN_TINT);
	ASSERT_NE(material, nullptr);

	const Vec3 reference = ErrorMaterial();
	ExpectTheErrorMaterial(DrawnWith(material), reference, "a shader with no view block drew on a mesh");
}

TEST_F(MeshDrawContractFixture, WidePushBlockRefused) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("WidePush.hlsl", WIDE_PUSH_SHADER);
	ASSERT_NE(shader, nullptr);
	ASSERT_TRUE(DeclaresTheViewBlock(*shader));
	ASSERT_GT(shader->pushConstantSize, 16u);
	ASSERT_FALSE(DeclaresPassInputs(*shader));
	MaterialAsset* material = LoadMaterial("WidePush.mat", shader->assetId, GREEN_TINT);
	ASSERT_NE(material, nullptr);

	const Vec3 reference = ErrorMaterial();
	ExpectTheErrorMaterial(DrawnWith(material), reference, "a shader reading push bytes past the header drew on a mesh");
}

TEST_F(MeshDrawContractFixture, AShaderReadingPassInputsIsRefused) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("PassInput.hlsl", PASS_INPUT_SHADER);
	ASSERT_NE(shader, nullptr);
	ASSERT_TRUE(DeclaresTheViewBlock(*shader));
	ASSERT_EQ(shader->pushConstantSize, 16u);
	ASSERT_TRUE(DeclaresPassInputs(*shader));
	MaterialAsset* material = LoadMaterial("PassInput.mat", shader->assetId, GREEN_TINT);
	ASSERT_NE(material, nullptr);

	const Vec3 reference = ErrorMaterial();
	ExpectTheErrorMaterial(DrawnWith(material), reference, "a shader reading a pass input drew on a mesh with the set unbound");
}

TEST_F(MeshDrawContractFixture, ContractRawShaderDraws) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Contract.hlsl", CONTRACT_SHADER);
	ASSERT_NE(shader, nullptr);
	ASSERT_TRUE(DeclaresTheViewBlock(*shader));
	ASSERT_EQ(shader->pushConstantSize, 16u);
	ASSERT_FALSE(DeclaresPassInputs(*shader));
	MaterialAsset* material = LoadMaterial("Contract.mat", shader->assetId, GREEN_TINT);
	ASSERT_NE(material, nullptr);

	ExpectGreen(DrawnWith(material), "a raw shader that binds what a mesh draw binds was refused");
}

TEST_F(MeshDrawContractFixture, RefusalReportedOnce) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Pass.hlsl", PASS_SHADER);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* material = LoadMaterial("Pass.mat", shader->assetId, GREEN_TINT);
	ASSERT_NE(material, nullptr);

	ScopedLogCapture capture;
	ASSERT_TRUE(capture.Armed());
	for (int frame = 0; frame < 3; ++frame) {
		DrawnWith(material);
	}
	const str captured = capture.Finish();

	EXPECT_EQ(Occurrences(captured, REFUSAL), 1u) << captured;
	EXPECT_NE(captured.find("Pass.mat"), str::npos) << captured;
	EXPECT_NE(captured.find("Pass.hlsl"), str::npos) << captured;
}

TEST_F(MeshDrawContractFixture, StillRefusedEditReported) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Stubborn.hlsl", NO_VIEW_SHADER);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* material = LoadMaterial("Stubborn.mat", shader->assetId, GREEN_TINT);
	ASSERT_NE(material, nullptr);

	ScopedLogCapture capture;
	ASSERT_TRUE(capture.Armed());
	DrawnWith(material);
	const u32 before = shader->version;
	Edit("Stubborn.hlsl", PASS_SHADER);
	DrawnWith(material);
	DrawnWith(material);
	const u32 after = shader->version;
	const str captured = capture.Finish();

	ASSERT_GT(after, before);
	EXPECT_EQ(Occurrences(captured, REFUSAL), 2u) << captured;
}

TEST_F(MeshDrawContractFixture, FixedShaderReloadsAndDraws) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Fixed.hlsl", NO_VIEW_SHADER);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* material = LoadMaterial("Fixed.mat", shader->assetId, GREEN_TINT);
	ASSERT_NE(material, nullptr);

	const Vec3 reference = ErrorMaterial();
	ExpectTheErrorMaterial(DrawnWith(material), reference, "the shader was never refused (nothing to recover from)");

	Edit("Fixed.hlsl", CONTRACT_SHADER);
	ExpectGreen(DrawnWith(material), "the fixed shader still draws as the error material (the edit reloaded nothing)");
}
