#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ProceduralRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"

#include <filesystem>
#include <string>

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	constexpr const char* POSITION_SURFACE =
		"#pragma shalltear_template unlit\n"
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull back\n"
		"#pragma shalltear_default tint 1 1 1 1\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = saturate(float3(input.objectToWorld._m03 * 0.1 + 0.5, input.objectToWorld._m23 * 0.1 + 0.5,\n"
		"\t\t\t\t\t\t\t\t\t\t  0.5 + input.positionWS.z + input.objectToWorld._m23 * -1.0)) * tint.rgb;\n"
		"\tsurface.alpha = tint.a;\n"
		"}\n";

	constexpr const char* BLENDED_SURFACE =
		"#pragma shalltear_template unlit\n"
		"#pragma shalltear_blend alpha\n"
		"#pragma shalltear_depth test_only\n"
		"#pragma shalltear_cull back\n"
		"#pragma shalltear_default tint 1 1 1 0.5\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = saturate(float3(input.objectToWorld._m03 * 0.5 + 0.2, 0.3, 0.6 + input.objectToWorld._m23 * -0.1));\n"
		"\tsurface.alpha = tint.a;\n"
		"}\n";

	constexpr const char* DEPTH_READER =
		"#pragma shalltear_template unlit\n"
		"#pragma shalltear_blend alpha\n"
		"#pragma shalltear_depth test_only\n"
		"#pragma shalltear_cull off\n"
		"#pragma shalltear_default tint 1 1 1 1\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"[[vk::binding(0, 2)]] Texture2D sltSceneDepth;\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tfloat raw = sltSceneDepth.Load(int3(int2(input.positionSS), 0)).r;\n"
		"\tfloat4 scene = mul(ShalltearInverseProjectionMatrix(), float4(0.0, 0.0, raw, 1.0));\n"
		"\tsurface.baseColor = float3(saturate((abs(scene.z / scene.w) - 9.0) * 0.5), 0.5, 0.5) * tint.rgb;\n"
		"\tsurface.alpha = tint.a;\n"
		"}\n";

	std::string RawShader(bool readsInstance) {
		std::string source =
			"#pragma shalltear_blend opaque\n"
			"#pragma shalltear_depth test_write\n"
			"#pragma shalltear_cull back\n"
			"#include \"ShalltearCommon.hlsli\"\n"
			"[[vk::push_constant]] ShalltearDrawHeader gDraw;\n"
			"struct VSOutput { float4 positionCS : SV_Position; float3 color : COLOR0; };\n";
		source += readsInstance ? "VSOutput VSMain(uint vertexId : SV_VertexID, uint instance : SV_InstanceID) {\n"
								  "\tShalltearPerDraw draw = ShalltearLoadPerDraw(ShalltearInstanceDrawAddress(gDraw.drawAddress, instance));\n"
								: "VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
								  "\tShalltearPerDraw draw = ShalltearLoadPerDraw(gDraw.drawAddress);\n";
		source += "\tShalltearVertex vertex = ShalltearLoadVertex(draw.vertexAddress, vertexId);\n"
				  "\tVSOutput output;\n"
				  "\toutput.positionCS = mul(sltViewProjection, ShalltearTransformPosition(draw, vertex.position));\n"
				  "\toutput.color = float3(draw.world3.x * 0.1 + 0.5, 0.5, 0.25);\n"
				  "\treturn output;\n"
				  "}\n"
				  "float4 PSMain(VSOutput input) : SV_Target { return float4(input.color, 1); }\n";
		return source;
	}

	class InstancingSquare : public ProceduralRenderer {
	public:
		SLT_REFLECT_COMPONENT(InstancingSquare, ProceduralRenderer)
		SLT_END_REFLECT()

		Handle<MaterialAsset> material;
		Vec3 center{};
		f32 half = 1.0f;

		void Declare(ProceduralPlan& plan) override {
			plan.SetBounds(AABB::FromCenterExtents(center, Vec3(half, half, 0.01f)));
			plan.AddItem(material, center, ShadowCasting::Off);
		}

		void Record(ProceduralContext& context) override {
			const Vec3 corners[4] = {center + Vec3(-half, -half, 0.0f), center + Vec3(half, -half, 0.0f), center + Vec3(half, half, 0.0f),
									 center + Vec3(-half, half, 0.0f)};
			const u32 order[6] = {0, 1, 2, 0, 2, 3};
			Vector<MeshVertex> triangles{GetMemoryResource(MemTag::Scene)};
			for (u32 corner : order) {
				MeshVertex vertex;
				vertex.position = corners[corner];
				vertex.normal = Vec3(0.0f, 0.0f, 1.0f);
				vertex.tangent = Vec3(1.0f, 0.0f, 0.0f);
				triangles.push_back(vertex);
			}
			context.DrawTriangles(triangles, false);
		}
	};

	SLT_REGISTER_TYPE(InstancingSquare)

	constexpr f32 ROW_X[] = {-3.0f, -1.5f, 0.0f, 1.5f, 3.0f};
	constexpr bool ROW_MIRRORED[] = {false, true, false, true, false};
	constexpr u32 ROW_COUNT = static_cast<u32>(std::size(ROW_X));

	struct Frame {
		Vector<u8> pixels;
		Vec2I size{};
		GraphicsStats stats;
	};

	class InstancingFixture : public MaterialReadbackFixture {
	protected:
		Handle<Scene> _scene;
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Handle<MeshAsset> _cube;
		Handle<MeshAsset> _plane;
		Vector<Handle<MeshRenderer>> _renderers;

		void TearDown() override {
			if (_target != INVALID_VIEW_TARGET) {
				RenderSystem::DestroyViewTarget(_target);
			}
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			MaterialReadbackFixture::TearDown();
		}

		bool Ready() {
			if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
				return false;
			}
			_cube = Handle<MeshAsset>(static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)))));
			_plane = Handle<MeshAsset>(static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Plane)))));
			_scene = SceneSystem::CreateScene("Instancing");
			_target = RenderSystem::CreateViewTarget({128, 128});
			return _cube.IsValid() && _plane.IsValid() && _target != INVALID_VIEW_TARGET;
		}

		ShaderAsset* AddShader(const char* fileName, const std::string& source) {
			WriteProjectFile(fileName, str(source.c_str()));
			auto* shader = static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + fileName)));
			return shader && !shader->variants.empty() ? shader : nullptr;
		}

		MaterialAsset* Copy(const Guid& shader, u32 index) {
			const std::string name = "Copy" + std::to_string(index) + ".mat";
			return LoadMaterial(name.c_str(), shader, {});
		}

		Handle<MeshRenderer> AddRenderer(const Handle<MeshAsset>& mesh, const Vec3& position, const Vec3& scale, bool tracked = true) {
			Handle<SceneObject> object = SceneSystem::CreateObject("Object", _scene);
			object->SetLocalPosition(position);
			object->SetLocalScale(scale);
			Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
			renderer->SetMesh(mesh);
			if (tracked) {
				_renderers.push_back(renderer);
			}
			return renderer;
		}

		void AddRow() {
			for (u32 i = 0; i < ROW_COUNT; ++i) {
				AddRenderer(_cube, Vec3(ROW_X[i], 0.0f, 0.0f), Vec3(ROW_MIRRORED[i] ? -1.0f : 1.0f, 1.0f, 1.0f));
			}
		}

		void UseShared(MaterialAsset* material) {
			for (const Handle<MeshRenderer>& renderer : _renderers) {
				renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			}
		}

		void UseCopies(const Guid& shader) {
			for (u32 i = 0; i < static_cast<u32>(_renderers.size()); ++i) {
				_renderers[i]->SetMaterial(0, Handle<MaterialAsset>(Copy(shader, i)));
			}
		}

		ViewDesc RowView() const {
			ViewDesc desc;
			const Vec3 eye(0.0f, 0.0f, 10.0f);
			desc.view = Mat4::LookAt(eye, Vec3::Zero());
			desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
			desc.cameraPosition = eye;
			desc.clearColor = Color::Black();
			desc.tonemap = false;
			return desc;
		}

		Frame Render(const ViewDesc& desc) {
			SceneSystem::EndFrame();
			Frame frame;
			RenderSystem::SubmitView(_target, desc);
			RenderSystem::RenderAll();
			frame.stats = RenderSystem::GetStats();
			EXPECT_TRUE(RenderSystem::ReadbackViewTarget(_target, frame.pixels, frame.size));
			return frame;
		}

		static Vec2I Project(const ViewDesc& desc, const Vec3& world, Vec2I size) {
			const Vec4 clip = (desc.projection * desc.view) * Vec4(world, 1.0f);
			return Vec2I{static_cast<i32>((clip.x / clip.w + 1.0f) * 0.5f * static_cast<f32>(size.x)),
						 static_cast<i32>((1.0f - clip.y / clip.w) * 0.5f * static_cast<f32>(size.y))};
		}

		static Vec3 Texel(const Frame& frame, Vec2I at) {
			const size_t index = (static_cast<size_t>(at.y) * frame.size.x + at.x) * 4;
			return Vec3(static_cast<f32>(frame.pixels[index]), static_cast<f32>(frame.pixels[index + 1]),
						static_cast<f32>(frame.pixels[index + 2]));
		}

		static u32 Darkened(const Frame& frame, const Frame& unshadowed) {
			u32 count = 0;
			for (size_t i = 0; i + 1 < frame.pixels.size() && i + 1 < unshadowed.pixels.size(); i += 4) {
				if (frame.pixels[i + 1] + 8 < unshadowed.pixels[i + 1]) {
					++count;
				}
			}
			return count;
		}

		void ExpectRowAtOwnTransforms(const Frame& frame, f32 nearBlue) {
			const ViewDesc desc = RowView();
			f32 previousRed = -1.0f;
			for (u32 i = 0; i < ROW_COUNT; ++i) {
				const Vec3 texel = Texel(frame, Project(desc, Vec3(ROW_X[i], 0.0f, 0.5f), frame.size));
				EXPECT_GT(texel.x, previousRed + 4.0f) << i;
				EXPECT_NEAR(texel.z, nearBlue, 3.0f) << i;
				previousRed = texel.x;
			}
		}
	};

} // namespace

TEST_F(InstancingFixture, IdenticalDrawsMergeWithSameImage) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Position.hlsl", POSITION_SURFACE);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* shared = LoadMaterial("Shared.mat", shader->assetId, {});
	ASSERT_NE(shared, nullptr);
	AddRow();

	UseShared(shared);
	const Frame merged = Render(RowView());
	UseCopies(shader->assetId);
	const Frame separate = Render(RowView());

	EXPECT_EQ(merged.stats.drawCallsSaved, ROW_COUNT - 2);
	EXPECT_EQ(separate.stats.drawCallsSaved, 0u);
	EXPECT_EQ(separate.stats.drawCalls, merged.stats.drawCalls + ROW_COUNT - 2);
	EXPECT_EQ(merged.stats.triangles, separate.stats.triangles);
	ASSERT_EQ(merged.pixels.size(), separate.pixels.size());
	EXPECT_TRUE(merged.pixels == separate.pixels);
	ExpectRowAtOwnTransforms(merged, 255.0f);
}

TEST_F(InstancingFixture, MixedDepthsSortIntoMeshRuns) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Position.hlsl", POSITION_SURFACE);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* shared = LoadMaterial("Shared.mat", shader->assetId, {});
	ASSERT_NE(shared, nullptr);
	const Handle<MeshAsset> sphere(static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Sphere)))));
	ASSERT_TRUE(sphere.IsValid());
	for (u32 i = 0; i < ROW_COUNT; ++i) {
		AddRenderer(i % 2 == 0 ? _cube : sphere, Vec3(ROW_X[i], 0.0f, -0.2f * static_cast<f32>(i)), Vec3(1.0f, 1.0f, 1.0f));
	}

	UseShared(shared);
	const Frame merged = Render(RowView());
	UseCopies(shader->assetId);
	const Frame separate = Render(RowView());

	EXPECT_EQ(merged.stats.drawCallsSaved, ROW_COUNT - 2);
	EXPECT_EQ(separate.stats.drawCallsSaved, 0u);
	EXPECT_TRUE(merged.pixels == separate.pixels);
}

TEST_F(InstancingFixture, DistanceBandsSplitRuns) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Position.hlsl", POSITION_SURFACE);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* shared = LoadMaterial("Shared.mat", shader->assetId, {});
	ASSERT_NE(shared, nullptr);
	const Handle<MeshAsset> sphere(static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Sphere)))));
	ASSERT_TRUE(sphere.IsValid());
	for (f32 z : {0.0f, -30.0f}) {
		AddRenderer(_cube, Vec3(0.0f, 0.0f, z), Vec3(1.0f, 1.0f, 1.0f));
		AddRenderer(sphere, Vec3(1.75f, 0.0f, z), Vec3(1.0f, 1.0f, 1.0f));
		AddRenderer(_cube, Vec3(-2.5f, 0.0f, z), Vec3(1.0f, 1.0f, 1.0f));
	}

	UseShared(shared);
	const Frame merged = Render(RowView());
	UseCopies(shader->assetId);
	const Frame separate = Render(RowView());

	EXPECT_EQ(merged.stats.drawCallsSaved, 2u);
	EXPECT_TRUE(merged.pixels == separate.pixels);
}

TEST_F(InstancingFixture, PrepassDrawsMergedDepths) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Position.hlsl", POSITION_SURFACE);
	ShaderAsset* reader = AddShader("Reader.hlsl", DEPTH_READER);
	ASSERT_NE(shader, nullptr);
	ASSERT_NE(reader, nullptr);
	MaterialAsset* shared = LoadMaterial("Shared.mat", shader->assetId, {});
	MaterialAsset* readerMaterial = LoadMaterial("Reader.mat", reader->assetId, {});
	ASSERT_NE(shared, nullptr);
	ASSERT_NE(readerMaterial, nullptr);
	const Handle<MeshAsset> quad(static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)))));
	ASSERT_TRUE(quad.IsValid());
	for (u32 i = 0; i < ROW_COUNT; ++i) {
		AddRenderer(_cube, Vec3(ROW_X[i], 0.0f, -0.2f * static_cast<f32>(i)), Vec3(1.0f, 1.0f, 1.0f));
	}
	AddRenderer(quad, Vec3(0.0f, 0.0f, 2.0f), Vec3(12.0f, 12.0f, 1.0f), false)->SetMaterial(0, Handle<MaterialAsset>(readerMaterial));

	UseShared(shared);
	const Frame merged = Render(RowView());
	UseCopies(shader->assetId);
	const Frame separate = Render(RowView());

	EXPECT_EQ(merged.stats.drawCallsSaved, 2u * (ROW_COUNT - 1));
	EXPECT_EQ(separate.stats.drawCallsSaved, 0u);
	EXPECT_TRUE(merged.pixels == separate.pixels);
	const ViewDesc desc = RowView();
	f32 previousRed = -1.0f;
	for (u32 i = 0; i < ROW_COUNT; ++i) {
		const Vec3 texel = Texel(merged, Project(desc, Vec3(ROW_X[i], 0.0f, 0.5f - 0.2f * static_cast<f32>(i)), merged.size));
		EXPECT_GT(texel.x, previousRed + 4.0f) << i;
		previousRed = texel.x;
	}
}

TEST_F(InstancingFixture, RawShaderMergesOnlyReadingInstance) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* single = AddShader("Single.hlsl", RawShader(false));
	ShaderAsset* instanced = AddShader("Instanced.hlsl", RawShader(true));
	ASSERT_NE(single, nullptr);
	ASSERT_NE(instanced, nullptr);
	MaterialAsset* singleMaterial = LoadMaterial("Single.mat", single->assetId, {});
	MaterialAsset* instancedMaterial = LoadMaterial("Instanced.mat", instanced->assetId, {});
	ASSERT_NE(singleMaterial, nullptr);
	ASSERT_NE(instancedMaterial, nullptr);
	AddRow();

	UseShared(singleMaterial);
	const Frame perObject = Render(RowView());
	UseShared(instancedMaterial);
	const Frame merged = Render(RowView());

	EXPECT_EQ(perObject.stats.drawCallsSaved, 0u);
	EXPECT_EQ(merged.stats.drawCallsSaved, ROW_COUNT - 2);
	EXPECT_TRUE(perObject.pixels == merged.pixels);
	ExpectRowAtOwnTransforms(perObject, ExpectedTexel(Vec3(0.25f)).z);
	ExpectRowAtOwnTransforms(merged, ExpectedTexel(Vec3(0.25f)).z);
}

TEST_F(InstancingFixture, BlendedRunDrawsBackToFront) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Blended.hlsl", BLENDED_SURFACE);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* shared = LoadMaterial("Shared.mat", shader->assetId, {});
	ASSERT_NE(shared, nullptr);
	for (i32 i = 0; i < 3; ++i) {
		AddRenderer(_cube, Vec3(0.3f * static_cast<f32>(i), 0.0f, 2.0f * static_cast<f32>(i - 1)), Vec3(1.0f, 1.0f, 1.0f));
	}

	UseShared(shared);
	const Frame merged = Render(RowView());
	UseCopies(shader->assetId);
	const Frame separate = Render(RowView());

	EXPECT_EQ(merged.stats.drawCallsSaved, 2u);
	EXPECT_EQ(separate.stats.drawCallsSaved, 0u);
	EXPECT_TRUE(merged.pixels == separate.pixels);

	const ViewDesc desc = RowView();
	const Vec3 back(0.2f, 0.3f, 0.8f);
	const Vec3 middle(0.35f, 0.3f, 0.6f);
	const Vec3 front(0.5f, 0.3f, 0.4f);
	const Vec3 alone = Texel(merged, Project(desc, Vec3(-0.4f, 0.0f, -1.5f), merged.size));
	const Vec3 stacked = Texel(merged, Project(desc, Vec3(0.4f, 0.0f, 2.5f), merged.size));
	const Vec3 aloneExpected = ExpectedTexel(back * 0.5f);
	const Vec3 stackedExpected = ExpectedTexel(front * 0.5f + middle * 0.25f);
	EXPECT_NEAR(alone.x, aloneExpected.x, 3.0f);
	EXPECT_NEAR(alone.z, aloneExpected.z, 3.0f);
	EXPECT_NEAR(stacked.x, stackedExpected.x, 3.0f);
	EXPECT_NEAR(stacked.z, stackedExpected.z, 3.0f);
}

TEST_F(InstancingFixture, ProceduralBetweenBlendedSplitsRun) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	ShaderAsset* shader = AddShader("Blended.hlsl", BLENDED_SURFACE);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* shared = LoadMaterial("Shared.mat", shader->assetId, {});
	ASSERT_NE(shared, nullptr);
	AddRenderer(_cube, Vec3(0.0f, 0.0f, -2.0f), Vec3(1.0f, 1.0f, 1.0f));
	AddRenderer(_cube, Vec3(0.6f, 0.0f, 2.0f), Vec3(1.0f, 1.0f, 1.0f));
	UseShared(shared);
	EXPECT_EQ(Render(RowView()).stats.drawCallsSaved, 1u);

	Handle<SceneObject> squareObject = SceneSystem::CreateObject("Square", _scene);
	Handle<InstancingSquare> square = squareObject->AddComponent<InstancingSquare>();
	square->material = Handle<MaterialAsset>(shared);
	square->center = Vec3(0.3f, 0.0f, 0.0f);
	const Frame merged = Render(RowView());
	UseCopies(shader->assetId);
	const Frame separate = Render(RowView());

	EXPECT_EQ(merged.stats.drawCallsSaved, 0u);
	EXPECT_TRUE(merged.pixels == separate.pixels);
	const Vec3 front(0.5f, 0.3f, 0.4f);
	const Vec3 between(0.2f, 0.3f, 0.6f);
	const Vec3 stacked = Texel(merged, Project(RowView(), Vec3(0.4f, 0.0f, 2.5f), merged.size));
	const Vec3 expected = ExpectedTexel(front * 0.5f + between * 0.25f);
	EXPECT_NEAR(stacked.x, expected.x, 3.0f);
	EXPECT_NEAR(stacked.z, expected.z, 3.0f);
}

TEST_F(InstancingFixture, MergedCastersCastSameShadows) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	const Guid lit = AssetDatabase::LookupGuid(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_TRUE(lit.IsValid());
	MaterialAsset* shared = LoadMaterial("Shared.mat", lit, {});
	ASSERT_NE(shared, nullptr);
	Handle<SceneObject> sunObject = SceneSystem::CreateObject("Sun", _scene);
	sunObject->SetLocalRotation(Quat::FromEuler(Vec3(60.0f, 20.0f, 0.0f)));
	Handle<Light> sun = sunObject->AddComponent<Light>();
	sun->SetIntensity(2.0f);
	for (u32 i = 0; i < ROW_COUNT; ++i) {
		AddRenderer(_cube, Vec3(ROW_X[i], 1.5f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
	}
	Handle<MeshRenderer> ground = AddRenderer(_plane, Vec3::Zero(), Vec3(1.0f, 1.0f, 1.0f));

	ViewDesc desc;
	const Vec3 eye(0.0f, 9.0f, 9.0f);
	desc.view = Mat4::LookAt(eye, Vec3::Zero());
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = eye;
	desc.clearColor = Color::Black();

	UseShared(shared);
	const Frame merged = Render(desc);
	if (merged.stats.shadowMapsRendered == 0) {
		GTEST_SKIP() << "no shadowing light extracted";
	}
	UseCopies(lit);
	ground->SetMaterial(0, Handle<MaterialAsset>(shared));
	const Frame separate = Render(desc);
	sun->SetCastShadows(false);
	const Frame unshadowed = Render(desc);

	EXPECT_GE(merged.stats.drawCallsSaved, 2u * (ROW_COUNT - 1));
	EXPECT_EQ(separate.stats.drawCallsSaved, 0u);
	EXPECT_TRUE(merged.pixels == separate.pixels);
	EXPECT_GT(Darkened(merged, unshadowed), 200u);
}

TEST_F(InstancingFixture, CasterRunsStayInsideMap) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	const Guid lit = AssetDatabase::LookupGuid(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_TRUE(lit.IsValid());
	MaterialAsset* shared = LoadMaterial("Shared.mat", lit, {});
	ASSERT_NE(shared, nullptr);
	for (f32 x : {1.0f, 7.0f}) {
		Handle<SceneObject> lampObject = SceneSystem::CreateObject("Lamp", _scene);
		lampObject->SetLocalPosition(Vec3(x, 6.0f, 0.0f));
		lampObject->SetLocalRotation(Quat::LookRotation(Vec3(0.0f, -1.0f, 0.0001f)));
		Handle<Light> lamp = lampObject->AddComponent<Light>();
		lamp->SetLightType(LightType::Spot);
		lamp->SetRange(10.0f);
		lamp->SetSpotAngleDeg(40.0f);
		lamp->SetIntensity(400.0f);
	}
	for (u32 i = 0; i < ROW_COUNT; ++i) {
		AddRenderer(_cube, Vec3(2.0f * static_cast<f32>(i), 0.5f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
	}
	MaterialAsset* groundMaterial = LoadMaterial("Ground.mat", lit, {});
	ASSERT_NE(groundMaterial, nullptr);
	AddRenderer(_plane, Vec3(4.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), false)->SetMaterial(0, Handle<MaterialAsset>(groundMaterial));

	ViewDesc desc;
	const Vec3 eye(-8.0f, 6.0f, 0.0f);
	desc.view = Mat4::LookAt(eye, Vec3(4.0f, 0.0f, 0.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = eye;
	desc.clearColor = Color::Black();

	UseShared(shared);
	const Frame merged = Render(desc);
	if (merged.stats.shadowMapsRendered < 2) {
		GTEST_SKIP() << "fewer than 2 shadowing lamps extracted";
	}
	UseCopies(lit);
	const Frame separate = Render(desc);

	EXPECT_EQ(merged.stats.drawCallsSaved, ROW_COUNT - 1 + 2u);
	EXPECT_EQ(separate.stats.drawCallsSaved, 0u);
	EXPECT_TRUE(merged.pixels == separate.pixels);
}

TEST_F(InstancingFixture, TwoSidedCastersMergeApart) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	const Guid lit = AssetDatabase::LookupGuid(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_TRUE(lit.IsValid());
	MaterialAsset* shared = LoadMaterial("Shared.mat", lit, {});
	ASSERT_NE(shared, nullptr);
	const Handle<MeshAsset> quad(static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)))));
	ASSERT_TRUE(quad.IsValid());
	Handle<SceneObject> sunObject = SceneSystem::CreateObject("Sun", _scene);
	sunObject->SetLocalRotation(Quat::FromEuler(Vec3(60.0f, 20.0f, 0.0f)));
	Handle<Light> sun = sunObject->AddComponent<Light>();
	sun->SetIntensity(2.0f);
	for (u32 i = 0; i < ROW_COUNT; ++i) {
		Handle<MeshRenderer> renderer = AddRenderer(quad, Vec3(ROW_X[i] * 0.5f, 1.5f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
		renderer->GetOwner()->SetLocalRotation(Quat::FromEuler(Vec3(90.0f, 0.0f, 0.0f)));
	}
	Handle<MeshRenderer> ground = AddRenderer(_plane, Vec3::Zero(), Vec3(1.0f, 1.0f, 1.0f));
	auto cast = [&](bool alternate, ShadowCasting casting) {
		for (u32 i = 0; i < ROW_COUNT; ++i) {
			_renderers[i]->SetCastShadows(alternate && i % 2 == 1 ? ShadowCasting::TwoSided : casting);
		}
	};

	ViewDesc desc;
	const Vec3 eye(0.0f, 9.0f, 9.0f);
	desc.view = Mat4::LookAt(eye, Vec3::Zero());
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = eye;
	desc.clearColor = Color::Black();

	UseShared(shared);
	cast(true, ShadowCasting::On);
	const Frame mixed = Render(desc);
	if (mixed.stats.shadowMapsRendered == 0) {
		GTEST_SKIP() << "no shadowing light extracted";
	}
	UseCopies(lit);
	ground->SetMaterial(0, Handle<MaterialAsset>(shared));
	const Frame separate = Render(desc);
	UseShared(shared);
	cast(false, ShadowCasting::On);
	const Frame oneSided = Render(desc);
	cast(false, ShadowCasting::TwoSided);
	const Frame twoSided = Render(desc);
	sun->SetCastShadows(false);
	const Frame unshadowed = Render(desc);

	EXPECT_TRUE(mixed.pixels == separate.pixels);
	const u32 maps = oneSided.stats.drawCallsSaved - mixed.stats.drawCallsSaved;
	EXPECT_GE(maps, 1u);
	EXPECT_EQ(mixed.stats.drawCallsSaved, ROW_COUNT - 1 + (ROW_COUNT - 2) * maps);
	EXPECT_EQ(twoSided.stats.drawCallsSaved, oneSided.stats.drawCallsSaved);
	const u32 mixedShadow = Darkened(mixed, unshadowed);
	EXPECT_LT(Darkened(oneSided, unshadowed), mixedShadow);
	EXPECT_LT(mixedShadow, Darkened(twoSided, unshadowed));
}

TEST_F(InstancingFixture, SkinnedRunsDeformPerRenderer) {
	if (!Ready()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	std::error_code ec;
	std::filesystem::copy_file(RepoDir("Source/Tests/Fixtures/Graphics") / "SimpleSkin.gltf", _root / "SimpleSkin.gltf",
							   std::filesystem::copy_options::overwrite_existing, ec);
	ASSERT_FALSE(ec);
	Asset* model = AssetDatabase::LoadAsset(Path("project://SimpleSkin.gltf"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);
	Handle<SceneObject> left = SceneSystem::Instantiate(modelGuid, _scene);
	Handle<SceneObject> right = SceneSystem::Instantiate(modelGuid, _scene);
	ASSERT_TRUE(left.IsValid());
	ASSERT_TRUE(right.IsValid());
	left->SetLocalPosition(Vec3(-1.5f, 0.0f, 0.0f));
	right->SetLocalPosition(Vec3(1.5f, 0.0f, 0.0f));
	Handle<Skeleton> skeletons[2] = {left->GetComponent<Skeleton>(), right->GetComponent<Skeleton>()};
	ASSERT_TRUE(skeletons[0].IsValid());
	ASSERT_TRUE(skeletons[1].IsValid());
	ViewTargetId wide = RenderSystem::CreateViewTarget({128, 64});
	ASSERT_NE(wide, INVALID_VIEW_TARGET);
	ViewDesc desc;
	desc.view = Mat4::LookAt(Vec3(0.0f, 1.0f, 4.0f), Vec3(0.0f, 1.0f, 0.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 2.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 1.0f, 4.0f);
	desc.clearColor = Color::Black();

	auto topCovered = [](const Frame& frame, bool rightHalf) {
		u32 count = 0;
		for (i32 y = 0; y < frame.size.y / 2; ++y) {
			for (i32 x = rightHalf ? frame.size.x / 2 : 0; x < (rightHalf ? frame.size.x : frame.size.x / 2); ++x) {
				const size_t index = (static_cast<size_t>(y) * frame.size.x + x) * 4;
				if (frame.pixels[index] > 8 || frame.pixels[index + 1] > 8 || frame.pixels[index + 2] > 8) {
					++count;
				}
			}
		}
		return count;
	};
	auto render = [&]() {
		SceneSystem::EndFrame();
		Frame frame;
		RenderSystem::SubmitView(wide, desc);
		RenderSystem::RenderAll();
		frame.stats = RenderSystem::GetStats();
		EXPECT_TRUE(RenderSystem::ReadbackViewTarget(wide, frame.pixels, frame.size));
		return frame;
	};
	render();
	ASSERT_GT(skeletons[0]->GetLocalPose().size(), 1u);
	const BoneTransform bind = skeletons[0]->GetLocalPose()[1];
	auto renderPosed = [&](u32 posed) {
		for (u32 i = 0; i < 2; ++i) {
			BoneTransform bone = bind;
			if (i == posed) {
				bone.position = Vec3(0.0f, 6.0f, 0.0f);
			}
			skeletons[i]->SetBoneLocalPose(1, bone);
			skeletons[i]->UpdatePose();
		}
		return render();
	};

	const Frame rightPosed = renderPosed(1);
	const Frame leftPosed = renderPosed(0);
	RenderSystem::DestroyViewTarget(wide);

	EXPECT_GE(rightPosed.stats.drawCallsSaved, 1u);
	EXPECT_GE(leftPosed.stats.drawCallsSaved, 1u);
	const f32 posedRight = static_cast<f32>(topCovered(rightPosed, true));
	const f32 restLeft = static_cast<f32>(topCovered(rightPosed, false));
	const f32 posedLeft = static_cast<f32>(topCovered(leftPosed, false));
	const f32 restRight = static_cast<f32>(topCovered(leftPosed, true));
	EXPECT_GT(std::abs(posedRight - restLeft), 50.0f);
	EXPECT_NEAR(posedRight, posedLeft, 8.0f);
	EXPECT_NEAR(restLeft, restRight, 8.0f);
}
