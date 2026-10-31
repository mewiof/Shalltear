#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Memory.h"

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	constexpr const char* FLAT_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tVSOutput output;\n"
		"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
		"\toutput.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return float4(0.2, 0.4, 0.6, 1.0); }\n";

	class FlatRow : public RenderPass {
	public:
		Handle<ShaderAsset> shader;
		bool drew = false;

		void Record(PassContext& context) override { drew = context.DrawProcedural(shader, {}); }
	};

	constexpr u32 ROUNDS = 150;
	constexpr u32 ALLOWED_GROWTH = 100;

	class PipelineCacheFixture : public MaterialReadbackFixture {
	protected:
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
			WriteProjectFile("Flat.hlsl", FLAT_SHADER);
			_target = RenderSystem::CreateViewTarget({16, 16});
			return _target != INVALID_VIEW_TARGET;
		}

		ShaderAsset* ImportFlat() {
			ShaderAsset* shader = New<ShaderAsset>(MemTag::Asset);
			if (!shader->Import(FileSystem::Resolve(Path("project://Flat.hlsl")))) {
				Delete(shader);
				return nullptr;
			}
			return shader;
		}

		bool Draw(FlatRow& row) {
			const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
			ViewDesc desc = QuadView();
			desc.passes = rows;
			row.drew = false;
			RenderSystem::SubmitView(_target, desc);
			RenderSystem::RenderAll();
			return row.drew;
		}

		static u32 LivePipelines() { return RenderSystem::GetDevice()->GetLivePipelineCount(); }
	};

} // namespace

TEST_F(PipelineCacheFixture, ADestroyedShadersPipelinesAreReleased) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	FlatRow row;
	ShaderAsset* first = ImportFlat();
	ASSERT_NE(first, nullptr);
	row.shader = Handle<ShaderAsset>(first);
	ASSERT_TRUE(Draw(row));
	Delete(first);
	const u32 before = LivePipelines();

	for (u32 round = 0; round < ROUNDS; ++round) {
		ShaderAsset* shader = ImportFlat();
		ASSERT_NE(shader, nullptr) << "round " << round;
		row.shader = Handle<ShaderAsset>(shader);
		ASSERT_TRUE(Draw(row)) << "round " << round;
		Delete(shader);
	}
	const u32 after = LivePipelines();
	EXPECT_LT(after, before + ALLOWED_GROWTH) << before << " -> " << after;
}

TEST_F(PipelineCacheFixture, ASupersededVersionsPipelinesAreReleased) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	FlatRow row;
	ShaderAsset* shader = ImportFlat();
	ASSERT_NE(shader, nullptr);
	row.shader = Handle<ShaderAsset>(shader);
	ASSERT_TRUE(Draw(row));
	const u32 before = LivePipelines();

	for (u32 round = 0; round < ROUNDS; ++round) {
		++shader->version;
		ASSERT_TRUE(Draw(row)) << "round " << round;
	}
	const u32 after = LivePipelines();
	EXPECT_LT(after, before + ALLOWED_GROWTH) << before << " -> " << after;

	EXPECT_TRUE(Draw(row));
	Delete(shader);
}
