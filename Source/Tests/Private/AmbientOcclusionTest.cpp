#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Core/Serialization/JSON.h"

#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PackageOps.h"

#ifndef SHALLTEAR_SHARED
#include "Shalltear/PostProcessing/AmbientOcclusionPass.h"

#include "AmbientOcclusionShader.h"
#endif

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace Ops = Shalltear::EditorViewportOps;

#ifndef SHALLTEAR_SHARED

SLT_DECLARE_LINKED_PACKAGE(PostProcessing)

namespace {

	std::filesystem::path FindRepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 8 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	class AmbientOcclusionRenderFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mounted = false;
		bool _projectMounted = false;
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Vec2I _size{192, 192};
		Handle<Scene> _scene;
		char _token = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_ao_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");

			SettingsPaths paths;
			paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			Settings::Initialize(paths);

			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			FileSystem::Mount("project", Path(_root.generic_string()));
			_projectMounted = true;

			AssetDatabase::Initialize();
			SLT_CALL_LINKED_PACKAGE(PostProcessing, &_token);
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			if (!RenderSystem::IsRenderingAvailable()) {
				return;
			}
			_target = RenderSystem::CreateViewTarget(_size);
			_scene = SceneSystem::CreateScene("AmbientOcclusion");
		}

		void TearDown() override {
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			if (_target != INVALID_VIEW_TARGET) {
				RenderSystem::DestroyViewTarget(_target);
			}
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
			if (_projectMounted) {
				FileSystem::Unmount("project");
			}
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool Ready() const { return RenderSystem::IsRenderingAvailable() && _target != INVALID_VIEW_TARGET; }

		bool CompilerAvailable() {
			FileSystem::WriteTextFile(Path((_root / "CompilerProbe.hlsl").generic_string()),
									  "struct VSOutput { float4 positionCS : SV_Position; };\n"
									  "VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
									  "\tVSOutput output; output.positionCS = float4(float(vertexId), 0, 0, 1); return output;\n"
									  "}\n"
									  "float4 PSMain(VSOutput input) : SV_Target { return float4(1, 0, 1, 1); }\n");
			ShaderAsset probe;
			return probe.Import(FileSystem::Resolve(Path("project://CompilerProbe.hlsl")));
		}

		bool BuildScene() {
			Asset* litShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
			Asset* cubeMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
			if (!litShader || !cubeMesh) {
				return false;
			}
			FileSystem::WriteTextFile(Path((_root / "Matte.mat").generic_string()),
									  str("{\n\t\"shader\": \"") + litShader->assetId.ToString() + "\"\n}\n");
			auto* material = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("project://Matte.mat")));
			if (!material) {
				return false;
			}
			material->SetColor("baseColor", Color(1.0f, 1.0f, 1.0f, 1.0f));
			material->SetFloat("roughness", 1.0f);
			material->SetFloat("metallic", 0.0f);

			Handle<SceneObject> floor = SceneSystem::CreateObject("Floor", _scene);
			floor->SetLocalPosition(Vec3(0.0f, -0.5f, 0.0f));
			floor->SetLocalScale(Vec3(40.0f, 1.0f, 40.0f));
			Handle<MeshRenderer> floorRenderer = floor->AddComponent<MeshRenderer>();
			floorRenderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cubeMesh)));
			floorRenderer->SetMaterial(0, Handle<MaterialAsset>(material));

			Handle<SceneObject> box = SceneSystem::CreateObject("Box", _scene);
			box->SetLocalPosition(Vec3(0.0f, 1.0f, 0.0f));
			box->SetLocalScale(Vec3(2.0f, 2.0f, 2.0f));
			Handle<MeshRenderer> boxRenderer = box->AddComponent<MeshRenderer>();
			boxRenderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cubeMesh)));
			boxRenderer->SetMaterial(0, Handle<MaterialAsset>(material));

			Handle<SceneObject> environmentObject = SceneSystem::CreateObject("Environment", _scene);
			Handle<Environment> environment = environmentObject->AddComponent<Environment>();
			environment->SetSkyMode(SkyMode::None);
			environment->SetAmbientMode(AmbientMode::Color);
			environment->SetAmbientColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
			environment->SetAmbientIntensity(0.35f);

			SceneSystem::EndFrame();
			return true;
		}

		Vec3 CameraPosition() const { return Vec3(0.0f, 3.0f, 7.0f); }
		Mat4 View() const { return Mat4::LookAt(CameraPosition(), Vec3(0.0f, 0.5f, 0.0f)); }
		Mat4 Projection() const {
			return Mat4::PerspectiveReverseZ(75.0f, static_cast<f32>(_size.x) / static_cast<f32>(_size.y),
											 Ops::EDITOR_NEAR_PLANE, Ops::EDITOR_FAR_PLANE);
		}

		bool AddEmissiveMarker() {
			Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			Asset* cubeMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
			if (!unlitShader || !cubeMesh) {
				return false;
			}
			FileSystem::WriteTextFile(Path((_root / "Marker.mat").generic_string()),
									  str("{\n\t\"shader\": \"") + unlitShader->assetId.ToString() + "\"\n}\n");
			auto* material = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("project://Marker.mat")));
			if (!material) {
				return false;
			}
			material->SetColor("baseColor", Color(1.0f, 1.0f, 1.0f, 1.0f));
			Handle<SceneObject> marker = SceneSystem::CreateObject("Marker", _scene);
			marker->SetLocalPosition(Vec3(0.0f, 0.05f, 1.25f));
			marker->SetLocalScale(Vec3(0.5f, 0.1f, 0.5f));
			Handle<MeshRenderer> renderer = marker->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cubeMesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			SceneSystem::EndFrame();
			return true;
		}

		static constexpr Vec3 MARKER_POSITION{0.0f, 0.1f, 1.25f};

		bool AddFloatingSlab() {
			Asset* cubeMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
			auto* material = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("project://Matte.mat")));
			if (!cubeMesh || !material) {
				return false;
			}
			Handle<SceneObject> slab = SceneSystem::CreateObject("Slab", _scene);
			slab->SetLocalPosition(SLAB_POSITION);
			slab->SetLocalScale(Vec3(0.5f, 2.0f, 0.5f));
			Handle<MeshRenderer> renderer = slab->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cubeMesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			SceneSystem::EndFrame();
			return true;
		}

		static constexpr Vec3 SLAB_POSITION{2.2f, 3.0f, 3.0f};

		bool Render(std::span<const Handle<RenderPass>> rows, Vector<u8>& outPixels, Vec2I& outSize) {
			ViewDesc desc;
			desc.cameraPosition = CameraPosition();
			desc.view = View();
			desc.projection = Projection();
			desc.clearColor = Color(0.0f, 0.0f, 0.0f);
			desc.passes = rows;
			RenderSystem::SubmitView(_target, desc);
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTarget(_target, outPixels, outSize);
		}

		bool Pixel(const Vec3& world, Vec2I& outPixel) const {
			const Mat4 viewProjection = Projection() * View();
			const Vec4 clip = viewProjection * Vec4(world.x, world.y, world.z, 1.0f);
			if (clip.w <= 0.0f) {
				return false;
			}
			const f32 ndcX = clip.x / clip.w;
			const f32 ndcY = clip.y / clip.w;
			outPixel = Vec2I{static_cast<i32>((ndcX * 0.5f + 0.5f) * static_cast<f32>(_size.x)),
							 static_cast<i32>((0.5f - ndcY * 0.5f) * static_cast<f32>(_size.y))};
			return outPixel.x >= 0 && outPixel.y >= 0 && outPixel.x < _size.x && outPixel.y < _size.y;
		}

		static const u8* Texel(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
			return pixels.data() + (static_cast<size_t>(y) * size.x + x) * 4;
		}

		static f32 BlockMean(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y, i32 half) {
			f32 total = 0.0f;
			i32 count = 0;
			for (i32 sy = y - half; sy <= y + half; ++sy) {
				for (i32 sx = x - half; sx <= x + half; ++sx) {
					if (sx < 0 || sy < 0 || sx >= size.x || sy >= size.y) {
						continue;
					}
					total += static_cast<f32>(Texel(pixels, size, sx, sy)[0]);
					++count;
				}
			}
			return count > 0 ? total / static_cast<f32>(count) : 0.0f;
		}
	};

} // namespace

TEST_F(AmbientOcclusionRenderFixture, CreaseDarkensAndOpenFloorDoesNot) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(BuildScene());

	AmbientOcclusionPass off;
	off.intensity = 0.0f;
	const Handle<RenderPass> offRows[] = {Handle<RenderPass>(&off)};
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render(offRows, reference, referenceSize));

	AmbientOcclusionPass on;
	on.radius = 1.0f;
	const Handle<RenderPass> onRows[] = {Handle<RenderPass>(&on)};
	Vector<u8> occluded{GetMemoryResource(MemTag::Graphics)};
	Vec2I occludedSize{};
	ASSERT_TRUE(Render(onRows, occluded, occludedSize));
	ASSERT_EQ(referenceSize, occludedSize);

	Vec2I creasePixel{};
	Vec2I flatPixel{};
	ASSERT_TRUE(Pixel(Vec3(0.0f, 0.0f, 1.1f), creasePixel));
	ASSERT_TRUE(Pixel(Vec3(0.0f, 0.0f, 3.5f), flatPixel));

	const f32 creaseReference = BlockMean(reference, referenceSize, creasePixel.x, creasePixel.y, 2);
	const f32 flatReference = BlockMean(reference, referenceSize, flatPixel.x, flatPixel.y, 2);
	const f32 creaseOccluded = BlockMean(occluded, occludedSize, creasePixel.x, creasePixel.y, 2);
	const f32 flatOccluded = BlockMean(occluded, occludedSize, flatPixel.x, flatPixel.y, 2);

	ASSERT_GT(creaseReference, 8.0f);
	ASSERT_GT(flatReference, 8.0f);

	EXPECT_LT(creaseOccluded / creaseReference, 0.93f)
		<< "crease " << creaseReference << " -> " << creaseOccluded;

	EXPECT_GT(flatOccluded / flatReference, 0.99f)
		<< "flat " << flatReference << " -> " << flatOccluded;
}

TEST_F(AmbientOcclusionRenderFixture, HalfResSilhouetteInventsNoOcclusion) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(BuildScene());
	ASSERT_TRUE(AddFloatingSlab());

	AmbientOcclusionPass off;
	off.intensity = 0.0f;
	const Handle<RenderPass> offRows[] = {Handle<RenderPass>(&off)};
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(offRows, reference, size));

	Vec2I slabPixel{};
	ASSERT_TRUE(Pixel(SLAB_POSITION, slabPixel));

	const i32 half = 22;

	for (i32 arm = 0; arm < 2; ++arm) {
		const bool downsample = arm == 0;
		AmbientOcclusionPass on;
		on.radius = 1.0f;
		on.downsample = downsample;
		const Handle<RenderPass> onRows[] = {Handle<RenderPass>(&on)};
		Vector<u8> occluded{GetMemoryResource(MemTag::Graphics)};
		Vec2I occludedSize{};
		ASSERT_TRUE(Render(onRows, occluded, occludedSize));
		ASSERT_EQ(size, occludedSize);

		i32 worst = 0;
		i32 lit = 0;
		Vec2I worstAt{};
		for (i32 y = slabPixel.y - half; y <= slabPixel.y + half; ++y) {
			for (i32 x = slabPixel.x - half; x <= slabPixel.x + half; ++x) {
				if (x < 0 || y < 0 || x >= size.x || y >= size.y) {
					continue;
				}
				const i32 base = Texel(reference, size, x, y)[0];
				if (base < 8) {
					continue;
				}
				++lit;
				worst = Math::Max(worst, base - static_cast<i32>(Texel(occluded, size, x, y)[0]));
			}
		}
		ASSERT_GT(lit, 400) << (downsample ? "half" : "full");

		EXPECT_LE(worst, 4);
	}
}

TEST_F(AmbientOcclusionRenderFixture, TwoIdenticalFramesReadIdentical) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(BuildScene());

	AmbientOcclusionPass row;
	row.intensity = 1.0f;
	row.radius = 1.0f;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};

	Vector<u8> first{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> second{GetMemoryResource(MemTag::Graphics)};
	Vec2I firstSize{};
	Vec2I secondSize{};
	ASSERT_TRUE(Render(rows, first, firstSize));
	ASSERT_TRUE(Render(rows, second, secondSize));
	ASSERT_EQ(firstSize, secondSize);
	ASSERT_EQ(first.size(), second.size());
	EXPECT_EQ(std::memcmp(first.data(), second.data(), first.size()), 0);
}

TEST_F(AmbientOcclusionRenderFixture, EmptyRowLeavesFrameUntouched) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(BuildScene());

	Vector<u8> withoutRow{GetMemoryResource(MemTag::Graphics)};
	Vec2I withoutSize{};
	ASSERT_TRUE(Render({}, withoutRow, withoutSize));

	AmbientOcclusionPass off;
	off.intensity = 0.0f;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&off)};
	Vector<u8> withRow{GetMemoryResource(MemTag::Graphics)};
	Vec2I withSize{};
	ASSERT_TRUE(Render(rows, withRow, withSize));

	ASSERT_EQ(withoutSize, withSize);
	ASSERT_EQ(withoutRow.size(), withRow.size());
	EXPECT_EQ(std::memcmp(withoutRow.data(), withRow.data(), withRow.size()), 0);
}

TEST_F(AmbientOcclusionRenderFixture, AfterOpaqueDarkensEmissionDefaultDoesNot) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(BuildScene());
	ASSERT_TRUE(AddEmissiveMarker());

	Vec2I markerPixel{};
	ASSERT_TRUE(Pixel(MARKER_POSITION, markerPixel));

	AmbientOcclusionPass off;
	off.intensity = 0.0f;
	const Handle<RenderPass> offRows[] = {Handle<RenderPass>(&off)};
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render(offRows, reference, referenceSize));

	AmbientOcclusionPass produced;
	produced.radius = 1.0f;
	const Handle<RenderPass> producedRows[] = {Handle<RenderPass>(&produced)};
	Vector<u8> viaShading{GetMemoryResource(MemTag::Graphics)};
	Vec2I viaShadingSize{};
	ASSERT_TRUE(Render(producedRows, viaShading, viaShadingSize));

	AmbientOcclusionPass composited;
	composited.radius = 1.0f;
	composited.afterOpaque = true;
	composited.point = PassPoint::AfterOpaque;
	const Handle<RenderPass> compositedRows[] = {Handle<RenderPass>(&composited)};
	Vector<u8> viaComposite{GetMemoryResource(MemTag::Graphics)};
	Vec2I viaCompositeSize{};
	ASSERT_TRUE(Render(compositedRows, viaComposite, viaCompositeSize));

	const f32 base = BlockMean(reference, referenceSize, markerPixel.x, markerPixel.y, 1);
	const f32 shaded = BlockMean(viaShading, viaShadingSize, markerPixel.x, markerPixel.y, 1);
	const f32 pasted = BlockMean(viaComposite, viaCompositeSize, markerPixel.x, markerPixel.y, 1);
	ASSERT_GT(base, 200.0f);

	EXPECT_GT(shaded / base, 0.99f) << "emissive " << base << " -> " << shaded;
	EXPECT_LT(pasted / base, 0.97f) << "emissive " << base << " -> " << pasted;
}

TEST(AmbientOcclusionPlanTest, HalfChainMakesFullExtentTarget) {
	AmbientOcclusionPass row;
	row.intensity = 1.0f;
	row.downsample = true;

	PassPlan plan(Vec2I{256, 256}, Vec3{});
	row.DeclarePlan(plan);

	ASSERT_EQ(plan.GetTargetCount(), 4u);
	ASSERT_EQ(plan.GetStepCount(), 4u);
	for (u32 i = 0; i < 3; ++i) {
		EXPECT_EQ(plan.GetTarget(i).divisor, 2) << "target " << i;
		EXPECT_EQ(plan.GetTarget(i).format, RHI::Format::R8Unorm) << "target " << i;
	}
	EXPECT_EQ(plan.GetStep(0).reads[0], PASS_NO_READ);
	EXPECT_EQ(plan.GetStep(1).reads[0], plan.GetStep(0).target);
	EXPECT_EQ(plan.GetStep(2).reads[0], plan.GetStep(1).target);
	EXPECT_EQ(plan.GetStep(3).reads[0], plan.GetStep(2).target);
	EXPECT_EQ(plan.GetTarget(plan.GetStep(3).target).divisor, 1);
	EXPECT_EQ(plan.GetProducedTarget(PassProduces::AmbientOcclusion), plan.GetStep(3).target);
}

TEST(AmbientOcclusionPlanTest, AfterOpaqueCompositesInsteadOfProducing) {
	AmbientOcclusionPass row;
	row.afterOpaque = true;
	row.point = PassPoint::AfterOpaque;

	PassPlan plan(Vec2I{256, 256}, Vec3{});
	row.DeclarePlan(plan);

	ASSERT_EQ(plan.GetTargetCount(), 4u);
	ASSERT_EQ(plan.GetStepCount(), 5u);
	EXPECT_EQ(plan.GetStep(4).target, PASS_SCENE_TARGET);
	EXPECT_EQ(plan.GetStep(4).reads[0], plan.GetStep(3).target);
	EXPECT_EQ(plan.GetTarget(plan.GetStep(3).target).divisor, 1);
	EXPECT_EQ(plan.GetProduced(), PassProduces::None);
}

TEST(AmbientOcclusionPlanTest, AfterOpaqueFullResCompositesLastBlur) {
	AmbientOcclusionPass row;
	row.afterOpaque = true;
	row.downsample = false;
	row.point = PassPoint::AfterOpaque;

	PassPlan plan(Vec2I{256, 256}, Vec3{});
	row.DeclarePlan(plan);

	ASSERT_EQ(plan.GetTargetCount(), 3u);
	ASSERT_EQ(plan.GetStepCount(), 4u);
	EXPECT_EQ(plan.GetStep(3).target, PASS_SCENE_TARGET);
	EXPECT_EQ(plan.GetStep(3).reads[0], plan.GetStep(2).target);
}

TEST(AmbientOcclusionPlanTest, AfterOpaqueAsksForSceneColor) {
	AmbientOcclusionPass row;
	EXPECT_FALSE(HasFlag(row.GetRequests(), PassRequests::SceneColor));
	row.afterOpaque = true;
	EXPECT_TRUE(HasFlag(row.GetRequests(), PassRequests::SceneColor));
	EXPECT_TRUE(HasFlag(row.GetRequests(), PassRequests::SceneDepth));
	EXPECT_TRUE(HasFlag(row.GetRequests(), PassRequests::SceneNormals));
}

TEST(AmbientOcclusionPlanTest, AfterOpaqueAtEarlyPointFallsBack) {
	AmbientOcclusionPass row;
	row.afterOpaque = true;
	row.point = PassPoint::AfterPrepass;

	PassPlan plan(Vec2I{256, 256}, Vec3{});
	row.DeclarePlan(plan);

	ASSERT_EQ(plan.GetStepCount(), 4u);
	EXPECT_EQ(plan.GetProducedTarget(PassProduces::AmbientOcclusion), plan.GetStep(3).target);
	EXPECT_EQ(plan.GetTarget(plan.GetStep(3).target).divisor, 1);
}

TEST(AmbientOcclusionPlanTest, DownsampleOffFullExtentNoUpsample) {
	AmbientOcclusionPass row;
	row.downsample = false;

	PassPlan plan(Vec2I{256, 256}, Vec3{});
	row.DeclarePlan(plan);

	ASSERT_EQ(plan.GetTargetCount(), 3u);
	ASSERT_EQ(plan.GetStepCount(), 3u);
	for (u32 i = 0; i < plan.GetTargetCount(); ++i) {
		EXPECT_EQ(plan.GetTarget(i).divisor, 1) << "target " << i;
	}
	EXPECT_EQ(plan.GetProducedTarget(PassProduces::AmbientOcclusion), plan.GetStep(2).target);
}

TEST(AmbientOcclusionPlanTest, ZeroIntensityDeclaresNothing) {
	AmbientOcclusionPass row;
	row.intensity = 0.0f;

	PassPlan plan(Vec2I{256, 256}, Vec3{});
	row.DeclarePlan(plan);

	EXPECT_EQ(plan.GetTargetCount(), 0u);
	EXPECT_EQ(plan.GetStepCount(), 0u);
	EXPECT_EQ(plan.GetProduced(), PassProduces::None);
}

TEST(AmbientOcclusionPlanTest, RowRequestsDepthAndNormalsWhateverSettings) {
	AmbientOcclusionPass row;
	const PassRequests expected = PassRequests::SceneDepth | PassRequests::SceneNormals;
	EXPECT_EQ(row.GetRequests(), expected);
	row.intensity = 0.0f;
	EXPECT_EQ(row.GetRequests(), expected);
}

#endif

namespace {

	constexpr const char* AO_RETIRED_PACKAGE_ID = "com.shalltear.ambientocclusion";
	constexpr const char* POST_PACKAGE_ID = "dev.shalltear.postprocessing";
	constexpr const char* POST_PACKAGE_TARGET = "PostProcessing";
	constexpr const char* AO_SHADER_TEXT = "7f4c2a91d3e85b06-c19a6d47f2085e3b";
	constexpr const char* AO_SHADER_PATH = "Shaders/AmbientOcclusion.hlsl";

	std::filesystem::path FindEngineAssetsDir() {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 12 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / "EngineAssets";
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

} // namespace

TEST(AmbientOcclusionManifestTest, RowShipsInsidePostProcessingPackage) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);

	EXPECT_EQ(PackageOps::FindPackage(catalogue, AO_RETIRED_PACKAGE_ID), nullptr);

	const PackageOps::PackageManifest* post = PackageOps::FindPackage(catalogue, POST_PACKAGE_ID);
	ASSERT_NE(post, nullptr);
	EXPECT_EQ(post->kind, PackageOps::PackageKind::Engine);
	EXPECT_EQ(PackageOps::GetPackageBinaryName(*post), POST_PACKAGE_TARGET);
	EXPECT_FALSE(post->stage.has_value());
}

TEST(AmbientOcclusionManifestTest, BuiltInAndOffByDefault) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);
	ASSERT_NE(PackageOps::FindPackage(catalogue, POST_PACKAGE_ID), nullptr);

	Vector<PackageOps::ResolvedPackage> resolution;
	PackageOps::ResolveEnabledSet(catalogue, {}, resolution);
	EXPECT_FALSE(PackageOps::IsEnabled(resolution, POST_PACKAGE_ID));

	Vector<str> stored;
	stored.emplace_back(POST_PACKAGE_ID);
	Vector<PackageOps::ResolvedPackage> enabledResolution;
	PackageOps::ResolveEnabledSet(catalogue, stored, enabledResolution);
	EXPECT_TRUE(PackageOps::IsEnabled(enabledResolution, POST_PACKAGE_ID));
}

TEST(AmbientOcclusionManifestTest, ShadersCommittedGuidIsOneRowResolves) {
	std::filesystem::path engineAssets = FindEngineAssetsDir();
	ASSERT_FALSE(engineAssets.empty());

	std::filesystem::path source = engineAssets / AO_SHADER_PATH;
	ASSERT_TRUE(std::filesystem::exists(source)) << "EngineAssets/" << AO_SHADER_PATH;

	str text = FileSystem::ReadTextFile(Path((source.generic_string() + ".meta")), MemTag::Asset);
	ASSERT_FALSE(text.empty());
	LinearAllocator arena(text.size() + 64, MemTag::Asset);
	JSONValue root = JSONParse(text, arena);
	const Guid onDisk = Guid::FromString(root["assetId"].AsString());

	const Guid expected = Guid::FromString(AO_SHADER_TEXT);
	ASSERT_TRUE(expected.IsValid());
	EXPECT_EQ(onDisk, expected);

#ifndef SHALLTEAR_SHARED
	EXPECT_EQ(AmbientOcclusionImpl::AMBIENT_OCCLUSION_SHADER, onDisk);
#endif
}
