#include "ShadingHarness.h"

#include "Shalltear/Core/Jobs/JobSystem.h"

#include <set>

using namespace ShadingTest;

namespace {

	constexpr const char* DISPLACED_SHADER = "#define SHALLTEAR_VERTEX_FUNCTION\n"
											 "#pragma shalltear_template lit\n"
											 "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 offset; };\n"
											 "void ShalltearVertexFunction(ShalltearVertexInput input, inout ShalltearVertexOutput vertex) {\n"
											 "\tvertex.positionOS = input.positionOS + offset.xyz;\n"
											 "}\n"
											 "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {}\n";

	constexpr const char* PLAIN_SHADER = "#pragma shalltear_template lit\n"
										 "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 offset; };\n"
										 "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {}\n";

	Vec3 Texel(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]), static_cast<f32>(pixels[index + 2]));
	}

	bool Covered(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return pixels[index] != 0 || pixels[index + 1] != 0 || pixels[index + 2] != 0;
	}

	u32 DifferingTexels(const Vector<u8>& a, const Vector<u8>& b) {
		u32 differing = 0;
		for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
			differing += (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ? 1u : 0u;
		}
		return differing;
	}

	class VertexFunctionFixture : public MaterialReadbackFixture {
	protected:
		bool RenderShadowScene(MaterialAsset* occluder, Vec3 occluderPosition, Vector<u8>& pixels, Vec2I& size, u32& shadowLights) {
			Asset* cubeAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
			if (!cubeAsset || !occluder) {
				return false;
			}
			Handle<MeshAsset> cube(static_cast<MeshAsset*>(cubeAsset));
			Handle<Scene> scene = SceneSystem::CreateScene("Displaced");
			Handle<SceneObject> ground = SceneSystem::CreateObject("Ground", scene);
			ground->SetLocalPosition(Vec3(0.0f, -1.0f, 0.0f));
			ground->SetLocalScale(Vec3(32.0f, 0.5f, 32.0f));
			ground->AddComponent<MeshRenderer>()->SetMesh(cube);

			Handle<SceneObject> block = SceneSystem::CreateObject("Occluder", scene);
			block->SetLocalPosition(occluderPosition);
			Handle<MeshRenderer> renderer = block->AddComponent<MeshRenderer>();
			renderer->SetMesh(cube);
			renderer->SetMaterial(0, Handle<MaterialAsset>(occluder));

			Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", scene);
			sun->SetLocalRotation(Quat::FromEuler(Vec3(52.0f, 37.0f, 11.0f)));
			Handle<Light> light = sun->AddComponent<Light>();
			light->SetIntensity(2.0f);
			SceneSystem::EndFrame();

			ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
			if (target == INVALID_VIEW_TARGET) {
				return false;
			}
			ViewDesc desc;
			desc.view = Mat4::LookAt(Vec3(-4.0f, 9.0f, -10.0f), Vec3(1.0f, 0.0f, 0.0f));
			desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
			desc.cameraPosition = Vec3(-4.0f, 9.0f, -10.0f);
			desc.clearColor = Color::Black();
			RenderSystem::SubmitView(target, desc);
			RenderSystem::RenderAll();
			const bool read = RenderSystem::ReadbackViewTarget(target, pixels, size);
			shadowLights = RenderSystem::GetStats().shadowLights;
			RenderSystem::DestroyViewTarget(target);
			SceneSystem::UnloadScene(scene);
			SceneSystem::EndFrame();
			return read;
		}

		void RequireDeviceAndCompiler() {
			if (!RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
			if (!CompilerAvailable()) {
				GTEST_SKIP() << "dxcompiler.dll unavailable";
			}
		}
	};

} // namespace

TEST_F(VertexFunctionFixture, DisplacedSurfaceAndShadowAgree) {
	RequireDeviceAndCompiler();
	if (IsSkipped()) {
		return;
	}
	WriteProjectFile("Displaced.hlsl", DISPLACED_SHADER);
	WriteProjectFile("Plain.hlsl", PLAIN_SHADER);
	Asset* displacedShader = AssetDatabase::LoadAsset(Path("project://Displaced.hlsl"));
	Asset* plainShader = AssetDatabase::LoadAsset(Path("project://Plain.hlsl"));
	ASSERT_NE(displacedShader, nullptr);
	ASSERT_NE(plainShader, nullptr);

	MaterialAsset* displaced = LoadMaterial("Displaced.mat", displacedShader->assetId, "\t\"colors\": { \"offset\": [2, 0, 0, 0] }");
	MaterialAsset* resting = LoadMaterial("Resting.mat", displacedShader->assetId, "\t\"colors\": { \"offset\": [0, 0, 0, 0] }");
	MaterialAsset* plain = LoadMaterial("Plain.mat", plainShader->assetId, "");
	ASSERT_NE(displaced, nullptr);
	ASSERT_NE(resting, nullptr);
	ASSERT_NE(plain, nullptr);

	const Vec3 rest(1.0f, 2.0f, -0.5f);
	const Vec3 moved(3.0f, 2.0f, -0.5f);
	Vector<u8> byFunction{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> byTransform{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> atRest{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> noFunction{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	u32 shadowLights = 0;
	ASSERT_TRUE(RenderShadowScene(displaced, rest, byFunction, size, shadowLights));
	ASSERT_GT(shadowLights, 0u);
	ASSERT_TRUE(RenderShadowScene(plain, moved, byTransform, size, shadowLights));
	ASSERT_TRUE(RenderShadowScene(resting, rest, atRest, size, shadowLights));
	ASSERT_TRUE(RenderShadowScene(plain, rest, noFunction, size, shadowLights));

	EXPECT_EQ(DifferingTexels(byFunction, byTransform), 0u);
	EXPECT_GT(DifferingTexels(byFunction, atRest), 100u);
	EXPECT_EQ(DifferingTexels(atRest, noFunction), 0u);
}

TEST_F(VertexFunctionFixture, SlotsInterpolateAsNamed) {
	RequireDeviceAndCompiler();
	if (IsSkipped()) {
		return;
	}
	WriteProjectFile("Modes.hlsl", "#define SHALLTEAR_VERTEX_FUNCTION\n"
								   "#define SHALLTEAR_INTERPOLATORS_LINEAR 1\n"
								   "#define SHALLTEAR_INTERPOLATORS_NO_PERSPECTIVE 1\n"
								   "#pragma shalltear_template unlit\n"
								   "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
								   "void ShalltearVertexFunction(ShalltearVertexInput input, inout ShalltearVertexOutput vertex) {\n"
								   "\tvertex.interpolators[0] = float4(input.uv, 0.0, 0.0);\n"
								   "\tvertex.interpolators[1] = float4(input.uv, 0.0, 0.0);\n"
								   "}\n"
								   "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
								   "\tsurface.baseColor = float3(input.interpolators[0].x, input.uv.x, input.interpolators[1].x) * tint.rgb;\n"
								   "}\n");
	Asset* shader = AssetDatabase::LoadAsset(Path("project://Modes.hlsl"));
	ASSERT_NE(shader, nullptr);
	MaterialAsset* material = LoadMaterial("Modes.mat", shader->assetId, "\t\"colors\": { \"tint\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	auto measure = [&](u32& covered, u32& linearOff, u32& noPerspectiveOff) {
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(RenderStage(stage, pixels, size));
		covered = linearOff = noPerspectiveOff = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				if (!Covered(pixels, size, x, y)) {
					continue;
				}
				++covered;
				const Vec3 texel = Texel(pixels, size, x, y);
				linearOff += std::abs(texel.x - texel.y) > 1.0f ? 1u : 0u;
				noPerspectiveOff += std::abs(texel.z - texel.y) > 3.0f ? 1u : 0u;
			}
		}
	};

	u32 covered = 0;
	u32 linearOff = 0;
	u32 noPerspectiveOff = 0;
	measure(covered, linearOff, noPerspectiveOff);
	ASSERT_GT(covered, 100u);
	EXPECT_EQ(linearOff, 0u);
	EXPECT_EQ(noPerspectiveOff, 0u);

	stage.quad->SetLocalRotation(Quat::AngleAxis(60.0f, Vec3::Up()));
	SceneSystem::EndFrame();
	measure(covered, linearOff, noPerspectiveOff);
	ASSERT_GT(covered, 50u);
	EXPECT_EQ(linearOff, 0u);
	EXPECT_GT(noPerspectiveOff, covered / 10);

	TearDownStage(stage);
}

TEST_F(VertexFunctionFixture, FlatSlotHoldsOneValue) {
	RequireDeviceAndCompiler();
	if (IsSkipped()) {
		return;
	}
	auto shaderFor = [](const char* mode) {
		return str("#define SHALLTEAR_VERTEX_FUNCTION\n#define ") + mode +
			   " 1\n"
			   "#pragma shalltear_template unlit\n"
			   "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
			   "void ShalltearVertexFunction(ShalltearVertexInput input, inout ShalltearVertexOutput vertex) {\n"
			   "\tvertex.interpolators[0] = float4(0.2 + 0.2 * float(input.vertexId), 0.0, 0.0, 0.0);\n"
			   "}\n"
			   "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
			   "\tsurface.baseColor = input.interpolators[0].xxx * tint.rgb;\n"
			   "}\n";
	};
	WriteProjectFile("Flat.hlsl", shaderFor("SHALLTEAR_INTERPOLATORS_FLAT"));
	WriteProjectFile("Ramp.hlsl", shaderFor("SHALLTEAR_INTERPOLATORS_LINEAR"));
	Asset* flatShader = AssetDatabase::LoadAsset(Path("project://Flat.hlsl"));
	Asset* rampShader = AssetDatabase::LoadAsset(Path("project://Ramp.hlsl"));
	ASSERT_NE(flatShader, nullptr);
	ASSERT_NE(rampShader, nullptr);

	auto distinctValues = [&](Asset* shader, const char* fileName) {
		std::set<u32> values;
		MaterialAsset* material = LoadMaterial(fileName, shader->assetId, "\t\"colors\": { \"tint\": [1, 1, 1, 1] }");
		EXPECT_NE(material, nullptr);
		ShadingStage stage;
		EXPECT_TRUE(BuildStage(material, stage));
		stage.sun->SetEnabled(false);
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		EXPECT_TRUE(RenderStage(stage, pixels, size));
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				if (Covered(pixels, size, x, y)) {
					values.insert(static_cast<u32>(Texel(pixels, size, x, y).x));
				}
			}
		}
		TearDownStage(stage);
		return values.size();
	};

	const size_t flat = distinctValues(flatShader, "Flat.mat");
	const size_t ramp = distinctValues(rampShader, "Ramp.mat");
	EXPECT_GE(flat, 1u);
	EXPECT_LE(flat, 2u);
	EXPECT_GT(ramp, 8u);
}

TEST_F(MaterialShadingFixture, UnsupportedInterpolatorRefused) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Orphan.hlsl", "#define SHALLTEAR_INTERPOLATORS_LINEAR 1\n"
									"#pragma shalltear_template unlit\n"
									"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {}\n");
	WriteProjectFile("TooMany.hlsl", "#define SHALLTEAR_VERTEX_FUNCTION\n"
									 "#define SHALLTEAR_INTERPOLATORS_LINEAR 9\n"
									 "#define SHALLTEAR_INTERPOLATORS_FLAT 8\n"
									 "#pragma shalltear_template unlit\n"
									 "void ShalltearVertexFunction(ShalltearVertexInput input, inout ShalltearVertexOutput vertex) {}\n"
									 "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {}\n");

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<ShaderDiagnosticSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));

	ShaderAsset orphan;
	const bool orphanImported = orphan.Import(FileSystem::Resolve(Path("project://Orphan.hlsl")));
	ShaderAsset tooMany;
	const bool tooManyImported = tooMany.Import(FileSystem::Resolve(Path("project://TooMany.hlsl")));

	LogManager::Terminate();
	str captured = sink->Joined();
	sink.reset();
	JobSystem::Terminate();

	EXPECT_FALSE(orphanImported);
	EXPECT_FALSE(tooManyImported);
	EXPECT_NE(captured.find("no SHALLTEAR_VERTEX_FUNCTION to write them"), str::npos) << captured;
	EXPECT_NE(captured.find("more custom interpolators than SHALLTEAR_MAX_CUSTOM_INTERPOLATORS"), str::npos) << captured;
}
