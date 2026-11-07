#include "TestsPCH.h"

#include "ShadingHarness.h"
#include "TestImages.h"

#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <string>
#include <vector>

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	constexpr const char* BLENDED_SURFACE =
		"#pragma shalltear_template unlit\n"
		"#pragma shalltear_blend alpha\n"
		"#pragma shalltear_depth test_only\n"
		"#pragma shalltear_cull off\n"
		"#pragma shalltear_default tint 1 1 1 0.5\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = tint.rgb;\n"
		"\tsurface.alpha = tint.a;\n"
		"}\n";

	constexpr const char* CHECKER_SURFACE =
		"#pragma shalltear_template unlit\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 unused; };\n"
		"[[vk::binding(1, 1)]] Texture2D checker;\n"
		"[[vk::binding(2, 1)]] SamplerState checkerSampler;\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = checker.Sample(checkerSampler, input.uv).rgb;\n"
		"}\n";

	constexpr const char* CUBE_SURFACE =
		"#pragma shalltear_template unlit\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 unused; };\n"
		"[[vk::binding(1, 1)]] TextureCube probe;\n"
		"[[vk::binding(2, 1)]] SamplerState probeSampler;\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = probe.SampleLevel(probeSampler, float3(0.0, 1.0, 0.0), 0.0).rgb;\n"
		"}\n";

	constexpr const char* PASS_INPUT_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull off\n"
		"#include \"ShalltearCommon.hlsli\"\n"
		"[[vk::push_constant]] ShalltearDrawHeader gDraw;\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"[[vk::binding(0, 2)]] Texture2D sltPassInput0;\n"
		"[[vk::binding(1, 2)]] SamplerState sltSceneSampler;\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tShalltearPerDraw draw = ShalltearLoadPerDraw(gDraw.drawAddress);\n"
		"\tShalltearVertex vertex = ShalltearLoadVertex(draw.vertexAddress, vertexId);\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = mul(sltViewProjection, ShalltearTransformPosition(draw, vertex.position));\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return sltPassInput0.Sample(sltSceneSampler, float2(0.5, 0.5)) + tint; }\n";

	struct Frame {
		Vector<u8> pixels;
		Vec2I size{};
		GraphicsStats stats;
	};

	u32 DifferingBytes(const Frame& a, const Frame& b) {
		if (a.pixels.size() != b.pixels.size()) {
			return ~0u;
		}
		u32 count = 0;
		for (size_t i = 0; i < a.pixels.size(); ++i) {
			count += a.pixels[i] != b.pixels[i];
		}
		return count;
	}

	size_t Occurrences(const str& haystack, std::string_view needle) {
		size_t count = 0;
		for (size_t at = haystack.find(needle); at != str::npos; at = haystack.find(needle, at + needle.size())) {
			++count;
		}
		return count;
	}

	std::string ColorBody(const char* name, f32 r, f32 g, f32 b, f32 a) {
		return std::string("\t\"colors\": { \"") + name + "\": [" + std::to_string(r) + ", " + std::to_string(g) + ", " + std::to_string(b) + ", " +
			   std::to_string(a) + "] }";
	}

	class ParallelExtractionFixture : public MaterialReadbackFixture {
	protected:
		Handle<Scene> _scene;
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Handle<MeshAsset> _cube;
		Handle<MeshAsset> _sphere;
		Handle<MeshAsset> _quad;
		std::vector<Handle<SceneObject>> _parents;
		std::shared_ptr<ShaderDiagnosticSink> _sink;
		bool _jobs = false;

		void TearDown() override {
			if (_target != INVALID_VIEW_TARGET) {
				RenderSystem::DestroyViewTarget(_target);
			}
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			MaterialReadbackFixture::TearDown();
			FinishLog();
			StopJobs();
		}

		bool Ready() {
			if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
				return false;
			}
			_cube = Handle<MeshAsset>(static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)))));
			_sphere = Handle<MeshAsset>(static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Sphere)))));
			_quad = Handle<MeshAsset>(static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)))));
			_scene = SceneSystem::CreateScene("ParallelExtraction");
			_target = RenderSystem::CreateViewTarget({192, 192});

			Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", _scene);
			sun->SetLocalRotation(Quat::FromEuler(Vec3(50.0f, 30.0f, 0.0f)));
			Handle<Light> light = sun->AddComponent<Light>();
			light->SetLightType(LightType::Directional);
			light->SetCastShadows(true);
			return _cube.IsValid() && _sphere.IsValid() && _quad.IsValid() && _target != INVALID_VIEW_TARGET;
		}

		void StartJobs() {
			if (!_jobs) {
				JobSystem::Initialize();
				_jobs = true;
			}
		}

		void StopJobs() {
			if (_jobs) {
				JobSystem::Terminate();
				_jobs = false;
			}
		}

		bool StartLog() {
			StartJobs();
			_sink = std::make_shared<ShaderDiagnosticSink>();
			LogManager::Initialize({false, false});
			LogManager::SetGlobalLevel(LogLevel::Trace);
			LogManager::AddSink(_sink);
			return ArmDiagnosticSink(_sink);
		}

		str FinishLog() {
			if (!_sink) {
				return {};
			}
			LogManager::Terminate();
			const str captured = _sink->Joined();
			_sink.reset();
			return captured;
		}

		ShaderAsset* AddShader(const char* fileName, const char* source) {
			WriteProjectFile(fileName, source);
			auto* shader = static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + fileName)));
			return shader && !shader->variants.empty() ? shader : nullptr;
		}

		Guid LitShader() {
			Asset* shader = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
			return shader ? shader->assetId : Guid{};
		}

		Handle<MeshRenderer> Add(const Handle<MeshAsset>& mesh, MaterialAsset* material, const Vec3& position, const Vec3& scale,
								 Handle<SceneObject> parent = {}) {
			Handle<SceneObject> object = SceneSystem::CreateObject("Prop", _scene, parent);
			object->SetLocalPosition(position);
			object->SetLocalScale(scale);
			Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
			renderer->SetMesh(mesh);
			if (material) {
				renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			}
			return renderer;
		}

		ViewDesc View() const {
			ViewDesc desc;
			const Vec3 eye(0.0f, 20.0f, 24.0f);
			desc.view = Mat4::LookAt(eye, Vec3::Zero());
			desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 200.0f);
			desc.cameraPosition = eye;
			desc.clearColor = Color::Black();
			desc.tonemap = false;
			return desc;
		}

		Frame Render() {
			SceneSystem::EndFrame();
			for (const Handle<SceneObject>& parent : _parents) {
				parent->SetLocalPosition(parent->GetLocalPosition());
			}
			Frame frame;
			RenderSystem::SubmitView(_target, View());
			RenderSystem::RenderAll();
			frame.stats = RenderSystem::GetStats();
			EXPECT_TRUE(RenderSystem::ReadbackViewTarget(_target, frame.pixels, frame.size));
			return frame;
		}
	};

} // namespace

TEST_F(ParallelExtractionFixture, LanesDrawSameImageAsOneLane) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	const Guid lit = LitShader();
	ASSERT_TRUE(lit.IsValid());
	std::vector<MaterialAsset*> opaque;
	for (u32 i = 0; i < 5; ++i) {
		opaque.push_back(LoadMaterial(("Lit" + std::to_string(i) + ".mat").c_str(), lit,
									  ColorBody("baseColor", 0.2f * static_cast<f32>(i + 1), 0.9f - 0.15f * static_cast<f32>(i), 0.5f, 1.0f)));
		ASSERT_NE(opaque.back(), nullptr);
	}
	MaterialAsset* missing = LoadMaterial("Missing.mat", Guid{}, {});
	ShaderAsset* blendedShader = AddShader("Blended.hlsl", BLENDED_SURFACE);
	ASSERT_NE(blendedShader, nullptr);
	std::vector<MaterialAsset*> blended;
	for (u32 i = 0; i < 24; ++i) {
		const f32 t = static_cast<f32>(i) / 23.0f;
		blended.push_back(LoadMaterial(("Blend" + std::to_string(i) + ".mat").c_str(), blendedShader->assetId, ColorBody("tint", t, 1.0f - t, (i % 3) * 0.5f, 0.5f)));
		ASSERT_NE(blended.back(), nullptr);
	}

	for (u32 p = 0; p < 4; ++p) {
		Handle<SceneObject> parent = SceneSystem::CreateObject("Parent", _scene);
		parent->SetLocalPosition(Vec3(p % 2 ? 6.0f : -6.0f, 0.0f, p < 2 ? 6.0f : -6.0f));
		parent->SetLocalRotation(Quat::FromEuler(Vec3(0.0f, 15.0f * static_cast<f32>(p), 0.0f)));
		_parents.push_back(parent);
	}
	u32 nextBlended = 0;
	for (u32 k = 0; k < 2400; ++k) {
		const f32 x = static_cast<f32>(k % 25) * 0.45f - 5.4f;
		const f32 z = static_cast<f32>((k / 25) % 24) * 0.45f - 5.2f;
		MaterialAsset* material = k % 97 == 0 ? missing : k % 89 == 0 ? nullptr
																	  : opaque[k % opaque.size()];
		Add(k % 2 ? _sphere : _cube, material, Vec3(x, 0.2f, z), Vec3(k % 7 == 0 ? -0.35f : 0.35f, 0.35f, 0.35f), _parents[k / 600]);
		if (k % 100 == 0 && nextBlended < blended.size()) {
			Add(_quad, blended[nextBlended++], Vec3(0.0f, 3.0f, 0.0f), Vec3(8.0f));
		}
	}
	ASSERT_EQ(nextBlended, 24u);

	const Frame one = Render();
	StartJobs();
	const Frame lanes = Render();

	EXPECT_EQ(one.stats.extractionLanes, 1u);
	EXPECT_GE(lanes.stats.extractionLanes, 2u);
	EXPECT_GT(one.stats.drawCallsSaved, 0u);
	EXPECT_GT(one.stats.shadowMapsRendered, 0u);
	EXPECT_EQ(lanes.stats.drawCalls, one.stats.drawCalls);
	EXPECT_EQ(lanes.stats.drawCallsSaved, one.stats.drawCallsSaved);
	EXPECT_EQ(lanes.stats.triangles, one.stats.triangles);
	EXPECT_EQ(lanes.stats.shadowMapsRendered, one.stats.shadowMapsRendered);
	ASSERT_FALSE(one.pixels.empty());
	const size_t center = (static_cast<size_t>(one.size.y / 2) * one.size.x + one.size.x / 2) * 4;
	EXPECT_GT(one.pixels[center] + one.pixels[center + 1] + one.pixels[center + 2], 30);
	EXPECT_EQ(DifferingBytes(one, lanes), 0u);
}

TEST_F(ParallelExtractionFixture, LaneRefusalReportedOnce) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("PassInput.hlsl", PASS_INPUT_SHADER);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* refused = LoadMaterial("PassInput.mat", shader->assetId, ColorBody("tint", 0.0f, 1.0f, 0.0f, 1.0f));
	ASSERT_NE(refused, nullptr);
	for (u32 k = 0; k < 1536; ++k) {
		Add(_cube, refused, Vec3(static_cast<f32>(k % 32) * 0.5f - 8.0f, 0.2f, static_cast<f32>(k / 32) * 0.5f - 12.0f), Vec3(0.3f));
	}

	ASSERT_TRUE(StartLog());
	Frame last;
	for (int frame = 0; frame < 3; ++frame) {
		last = Render();
	}
	const str captured = FinishLog();

	EXPECT_GE(last.stats.extractionLanes, 2u);
	EXPECT_GT(last.stats.drawCalls, 0u);
	EXPECT_EQ(Occurrences(captured, "can't be drawn on a mesh"), 1u) << captured;
	EXPECT_NE(captured.find("PassInput.mat"), str::npos) << captured;
}

TEST_F(ParallelExtractionFixture, LaneMissesResolvedOnMainThread) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	constexpr u16 CHECKER = 4;
	std::vector<u8> rgba(static_cast<size_t>(CHECKER) * CHECKER * 4);
	for (u32 y = 0; y < CHECKER; ++y) {
		for (u32 x = 0; x < CHECKER; ++x) {
			u8* texel = &rgba[(static_cast<size_t>(y) * CHECKER + x) * 4];
			texel[0] = texel[1] = texel[2] = (x + y) % 2 ? 255 : 0;
			texel[3] = 255;
		}
	}
	TestImages::WriteTga(_root / "Checker.tga", CHECKER, CHECKER, rgba.data());
	const Guid checker = AssetDatabase::EnsureAssetIdentity(Path("project://Checker.tga"));
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("sRGB", JSONValue::MakeBool(false));
	settings.AddMember("compression", JSONValue::MakeString("none"));
	settings.AddMember("generateMips", JSONValue::MakeBool(false));
	settings.AddMember("filter", JSONValue::MakeString("point"));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://Checker.tga"), settings));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Checker.tga")));
	Asset* checkerAsset = AssetDatabase::LoadAssetByGuid(checker);
	ASSERT_NE(checkerAsset, nullptr);

	ShaderAsset* checkerShader = AddShader("Checker.hlsl", CHECKER_SURFACE);
	ShaderAsset* cubeShader = AddShader("Cube.hlsl", CUBE_SURFACE);
	ASSERT_NE(checkerShader, nullptr);
	ASSERT_NE(cubeShader, nullptr);
	MaterialAsset* checkerMaterial = LoadMaterial("Checker.mat", checkerShader->assetId, "\t\"textures\": { \"checker\": \"" + checker.ToString() + "\" }");
	MaterialAsset* cubeMaterial = LoadMaterial("Cube.mat", cubeShader->assetId, {});
	MaterialAsset* plain = LoadMaterial("Plain.mat", LitShader(), {});
	ASSERT_NE(checkerMaterial, nullptr);
	ASSERT_NE(cubeMaterial, nullptr);
	ASSERT_NE(plain, nullptr);

	std::vector<Handle<MeshRenderer>> checkers;
	std::vector<Handle<MeshRenderer>> cubes;
	for (u32 k = 0; k < 1536; ++k) {
		Add(_cube, plain, Vec3(static_cast<f32>(k % 32) * 0.5f - 8.0f, 0.0f, static_cast<f32>(k / 32) * 0.5f + 40.0f), Vec3(0.3f));
		if (k % 192 == 0) {
			const f32 slot = static_cast<f32>(checkers.size());
			const bool first = checkers.empty();
			checkers.push_back(Add(_quad, plain, first ? Vec3::Zero() : Vec3(-13.0f + 2.0f * slot, 9.0f, -2.0f), Vec3(first ? 10.0f : 1.5f)));
			cubes.push_back(Add(_quad, plain, Vec3(-12.0f + 2.0f * slot, 11.0f, -4.0f), Vec3(1.5f)));
			checkers.back()->SetCastShadows(ShadowCasting::Off);
			cubes.back()->SetCastShadows(ShadowCasting::Off);
		}
	}
	checkers.front()->GetOwner()->SetLocalRotation(Quat::FromEuler(Vec3(-34.0f, 0.0f, 0.0f)));

	StartJobs();
	const Frame before = Render();
	EXPECT_GE(before.stats.extractionLanes, 2u);
	for (const Handle<MeshRenderer>& renderer : checkers) {
		renderer->SetMaterial(0, Handle<MaterialAsset>(checkerMaterial));
	}
	for (const Handle<MeshRenderer>& renderer : cubes) {
		renderer->SetMaterial(0, Handle<MaterialAsset>(cubeMaterial));
	}
	const Frame cold = Render();
	EXPECT_EQ(cold.stats.liveSamplers, before.stats.liveSamplers + 1);
	EXPECT_EQ(cold.stats.liveTextures, before.stats.liveTextures + 1);

	u32 midGray = 0;
	u32 sampled = 0;
	for (i32 y = cold.size.y / 2 - 24; y < cold.size.y / 2 + 24; ++y) {
		for (i32 x = cold.size.x / 2 - 24; x < cold.size.x / 2 + 24; ++x) {
			const u8 value = cold.pixels[(static_cast<size_t>(y) * cold.size.x + x) * 4];
			midGray += value > 40 && value < 215;
			++sampled;
		}
	}
	EXPECT_LT(midGray, sampled / 50);

	StopJobs();
	const Frame warm = Render();
	EXPECT_EQ(warm.stats.extractionLanes, 1u);
	EXPECT_EQ(DifferingBytes(cold, warm), 0u);
}
