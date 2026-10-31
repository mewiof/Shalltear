#include "TestsPCH.h"

#include "ProductLookup.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Window.h"
#include "Window/WindowInternal.h"

#include <filesystem>

using namespace Shalltear;

namespace {

	std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
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

	using Math::HalfToFloat;

} // anonymous namespace

TEST(RenderSystemHeadlessTest, ComponentsRegisterWithoutARenderer) {
	ASSERT_FALSE(RenderSystem::IsInitialized());
	SceneSystem::Initialize();
	{
		Handle<Scene> scene = SceneSystem::CreateScene("Headless");
		Handle<SceneObject> object = SceneSystem::CreateObject("Rig", scene);
		Handle<Camera> camera = object.Get()->AddComponent<Camera>();
		Handle<Light> light = object.Get()->AddComponent<Light>();
		Handle<MeshRenderer> renderer = object.Get()->AddComponent<MeshRenderer>();
		EXPECT_TRUE(camera.IsValid());
		EXPECT_TRUE(light.IsValid());
		EXPECT_TRUE(renderer.IsValid());

		camera.Get()->SetFieldOfViewDeg(75.0f);
		light.Get()->SetLightType(LightType::Spot);
		light.Get()->SetSpotAngleDeg(30.0f);

		SceneSystem::Destroy(object);
		SceneSystem::EndFrame();
		SceneSystem::UnloadScene(scene);
		SceneSystem::EndFrame();
	}
	SceneSystem::Terminate();
}

TEST(RenderSystemHeadlessTest, CameraBuildsReverseZProjection) {
	Camera camera;
	camera.SetFieldOfViewDeg(90.0f);
	camera.SetNearPlane(0.5f);
	camera.SetFarPlane(100.0f);
	Mat4 projection = camera.BuildProjection(1.0f);

	Vec3 nearPoint = projection.ProjectPoint(Vec3(0.0f, 0.0f, 0.5f));
	Vec3 farPoint = projection.ProjectPoint(Vec3(0.0f, 0.0f, 100.0f));
	EXPECT_NEAR(nearPoint.z, 1.0f, 1e-5f);
	EXPECT_NEAR(farPoint.z, 0.0f, 1e-5f);

	Frustum frustum = Frustum::FromMatrix(projection);
	EXPECT_TRUE(frustum.Contains(Vec3(0.0f, 0.0f, 10.0f)));
	EXPECT_FALSE(frustum.Contains(Vec3(0.0f, 0.0f, -1.0f)));
	EXPECT_FALSE(frustum.Contains(Vec3(0.0f, 0.0f, 200.0f)));
	EXPECT_FALSE(frustum.Contains(Vec3(50.0f, 0.0f, 10.0f)));
}

TEST(RenderSystemHeadlessTest, SlotsGrowWithoutCap) {
	MeshRenderer renderer;
	EXPECT_TRUE(renderer.GetMaterialCount() == 0);
	EXPECT_FALSE(renderer.GetMaterial(0).IsValid());

	MaterialAsset material;
	Handle<MaterialAsset> handle(&material);

	renderer.SetMaterial(3, handle);
	ASSERT_EQ(renderer.GetMaterialCount(), 4u);
	EXPECT_EQ(renderer.GetMaterial(3).GetInstanceId(), material.GetInstanceId());
	EXPECT_FALSE(renderer.GetMaterial(0).IsValid());
	EXPECT_FALSE(renderer.GetMaterial(2).IsValid());

	renderer.SetMaterial(24, handle);
	ASSERT_EQ(renderer.GetMaterialCount(), 25u);
	EXPECT_EQ(renderer.GetMaterial(24).GetInstanceId(), material.GetInstanceId());
	EXPECT_EQ(renderer.GetMaterial(3).GetInstanceId(), material.GetInstanceId());

	EXPECT_FALSE(renderer.GetMaterial(25).IsValid());
	EXPECT_FALSE(renderer.GetMaterial(100000).IsValid());
}

TEST(RenderSystemHeadlessTest, MaterialListBeats8Slots) {
	EXPECT_EQ(sizeof(Handle<MaterialAsset>), 8u);

	constexpr size_t OLD_INLINE_BYTES = 8 * sizeof(Handle<MaterialAsset>);
	constexpr size_t NEW_INLINE_BYTES = sizeof(Vector<Handle<MaterialAsset>>);
	EXPECT_EQ(OLD_INLINE_BYTES, 64u);
	EXPECT_LT(NEW_INLINE_BYTES, OLD_INLINE_BYTES);

	const size_t base = MemoryManager::GetBytesForTag(MemTag::Scene);
	MeshRenderer unbound;
	const size_t unboundBytes = MemoryManager::GetBytesForTag(MemTag::Scene) - base;
	EXPECT_EQ(unbound.GetMaterialCount(), 0u);

	const size_t beforeBound = MemoryManager::GetBytesForTag(MemTag::Scene);
	MeshRenderer bound;
	MaterialAsset material2;
	bound.SetMaterial(0, Handle<MaterialAsset>(&material2));
	const size_t boundBytes = MemoryManager::GetBytesForTag(MemTag::Scene) - beforeBound;

	EXPECT_LT(unboundBytes, boundBytes);
}

TEST(RenderSystemHeadlessTest, StatsAreZeroWhenDown) {
	ASSERT_FALSE(RenderSystem::IsInitialized());
	EXPECT_FALSE(RenderSystem::IsRenderingAvailable());
	EXPECT_EQ(RenderSystem::GetDevice(), nullptr);
	GraphicsStats stats = RenderSystem::GetStats();
	EXPECT_EQ(stats.drawCalls, 0u);
	EXPECT_EQ(stats.vramUsedBytes, 0u);
}

namespace {

	class RenderExitCriterionFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mounted = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_render_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
			}
			_mounted = true;

			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
				FileSystem::Unmount("project");
			}
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool StageFixture(const char* name) {
			std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / name, _root / name,
									   std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}
	};

} // anonymous namespace

TEST_F(RenderExitCriterionFixture, TangentlessMeshLit) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(StageFixture("cube.glb"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://cube.glb"));
	if (!model) {
		GTEST_SKIP() << "cube.glb import failed (dxcompiler.dll missing?)";
	}
	AssetDatabase::UnloadAsset(model);
	Asset* meshAsset = ProductLookup::Load(Path("project://cube.glb"), "Meshes/Cube");
	ASSERT_NE(meshAsset, nullptr);
	MeshAsset* mesh = static_cast<MeshAsset*>(meshAsset);
	ASSERT_FALSE(mesh->vertices.empty());
	EXPECT_FLOAT_EQ(mesh->vertices[0].tangent.LengthSquared(), 0.0f);

	ASSERT_FALSE(mesh->defaultMaterials.empty());
	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(mesh->defaultMaterials[0]);
	ASSERT_NE(materialAsset, nullptr);

	Handle<Scene> scene = SceneSystem::CreateScene("Tangents");
	Handle<SceneObject> cubeObject = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = cubeObject->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(mesh));
	renderer->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(materialAsset)));

	Handle<SceneObject> lightObject = SceneSystem::CreateObject("Sun", scene);
	lightObject->SetLocalRotation(Quat::Identity());
	Handle<Light> light = lightObject->AddComponent<Light>();
	light->SetIntensity(2.0f);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc;
	desc.view = Mat4::LookAt(Vec3(0.0f, 0.0f, -3.0f), Vec3(0.0f, 0.0f, 0.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -3.0f);
	desc.clearColor = Color::Black();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	auto centerLuma = [&]() {
		RenderSystem::SubmitView(target, desc);
		RenderSystem::RenderAll();
		EXPECT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
		const size_t index = (static_cast<size_t>(size.y / 2) * size.x + size.x / 2) * 4;
		return static_cast<f32>(pixels[index]) + pixels[index + 1] + pixels[index + 2];
	};

	const f32 lit = centerLuma();
	light->SetEnabled(false);
	SceneSystem::EndFrame();
	const f32 unlit = centerLuma();

	EXPECT_GT(lit, unlit + 30.0f);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(RenderExitCriterionFixture, TexturedLitGLTFMeshRendersFromScene) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(StageFixture("cube.glb"));

	Asset* model = AssetDatabase::LoadAsset(Path("project://cube.glb"));
	if (!model) {
		GTEST_SKIP() << "cube.glb import failed (dxcompiler.dll missing?)";
	}
	AssetDatabase::UnloadAsset(model);
	Asset* meshAsset = ProductLookup::Load(Path("project://cube.glb"), "Meshes/Cube");
	ASSERT_NE(meshAsset, nullptr);
	MeshAsset* mesh = static_cast<MeshAsset*>(meshAsset);
	ASSERT_EQ(mesh->submeshes.size(), 1u);
	EXPECT_NE(mesh->vertexBufferAddress, 0u);
	ASSERT_FALSE(mesh->defaultMaterials.empty());

	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(mesh->defaultMaterials[0]);
	ASSERT_NE(materialAsset, nullptr);
	Handle<MaterialAsset> material(static_cast<MaterialAsset*>(materialAsset));
	ASSERT_TRUE(static_cast<MaterialAsset*>(materialAsset)->shader.IsValid());

	WindowDesc windowDesc;
	windowDesc.title = "RenderSystemTest";
	windowDesc.clientSize = {256, 256};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("ExitCriterion");
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	cameraObject.Get()->SetLocalPosition(Vec3(0.0f, 0.0f, -3.0f));
	Handle<Camera> camera = cameraObject.Get()->AddComponent<Camera>();
	camera.Get()->SetTargetWindow(window);
	camera.Get()->SetClearColor(Color::Black());

	Handle<SceneObject> lightObject = SceneSystem::CreateObject("Sun", scene);
	lightObject.Get()->SetLocalRotation(Quat::FromEuler(Vec3(35.0f, -25.0f, 0.0f)));
	Handle<Light> light = lightObject.Get()->AddComponent<Light>();
	light.Get()->SetIntensity(1.5f);

	Handle<SceneObject> cubeObject = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = cubeObject.Get()->AddComponent<MeshRenderer>();
	renderer.Get()->SetMesh(Handle<MeshAsset>(mesh));
	renderer.Get()->SetMaterial(0, material);

	for (int frame = 0; frame < 3; ++frame) {
		WindowSystem::PumpEvents();
		SceneSystem::UpdateAll();
		SceneSystem::EndFrame();
		RenderSystem::RenderAll();
	}

	GraphicsStats stats = RenderSystem::GetStats();
	EXPECT_GE(stats.drawCalls, 2u);
	EXPECT_GE(stats.triangles, 12u);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, pixels, size));
	ASSERT_GT(size.x, 0);
	const u16* halves = reinterpret_cast<const u16*>(pixels.data());
	auto texel = [&](i32 x, i32 y, u32 channel) {
		return HalfToFloat(halves[(static_cast<size_t>(y) * size.x + x) * 4 + channel]);
	};
	i32 centerX = size.x / 2;
	i32 centerY = size.y / 2;
	f32 centerLuma = texel(centerX, centerY, 0) + texel(centerX, centerY, 1) + texel(centerX, centerY, 2);
	EXPECT_GT(centerLuma, 0.05f);
	f32 cornerLuma = texel(2, 2, 0) + texel(2, 2, 1) + texel(2, 2, 2);
	EXPECT_LT(cornerLuma, 0.01f);
	bool sawRedCell = false;
	for (i32 x = size.x / 4; x < (3 * size.x) / 4; ++x) {
		f32 r = texel(x, centerY, 0);
		f32 g = texel(x, centerY, 1);
		if (r > 0.02f && r > g * 2.0f) {
			sawRedCell = true;
			break;
		}
	}
	EXPECT_TRUE(sawRedCell);

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();

	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(meshAsset);

	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(RenderExitCriterionFixture, CubePrimitiveShowsNearFaceNotInside) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}

	Asset* meshAsset = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
	ASSERT_NE(meshAsset, nullptr);
	MeshAsset* mesh = static_cast<MeshAsset*>(meshAsset);
	ASSERT_FALSE(mesh->defaultMaterials.empty());
	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(mesh->defaultMaterials[0]);
	if (!materialAsset) {
		AssetDatabase::UnloadAsset(meshAsset);
		GTEST_SKIP() << "default material unavailable (dxcompiler.dll missing?)";
	}
	Handle<MaterialAsset> material(static_cast<MaterialAsset*>(materialAsset));

	WindowDesc windowDesc;
	windowDesc.title = "CubeWinding";
	windowDesc.clientSize = {256, 256};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("CubeWinding");
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	cameraObject.Get()->SetLocalPosition(Vec3(0.0f, 0.0f, -3.0f));
	Handle<Camera> camera = cameraObject.Get()->AddComponent<Camera>();
	camera.Get()->SetTargetWindow(window);
	camera.Get()->SetClearColor(Color::Black());

	Handle<SceneObject> lightObject = SceneSystem::CreateObject("Sun", scene);
	lightObject.Get()->SetLocalRotation(Quat::FromEuler(Vec3(0.0f, 0.0f, 0.0f)));
	Handle<Light> light = lightObject.Get()->AddComponent<Light>();
	light.Get()->SetIntensity(2.0f);

	Handle<SceneObject> cubeObject = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = cubeObject.Get()->AddComponent<MeshRenderer>();
	renderer.Get()->SetMesh(Handle<MeshAsset>(mesh));
	renderer.Get()->SetMaterial(0, material);

	for (int frame = 0; frame < 3; ++frame) {
		WindowSystem::PumpEvents();
		SceneSystem::UpdateAll();
		SceneSystem::EndFrame();
		RenderSystem::RenderAll();
	}

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, pixels, size));
	ASSERT_GT(size.x, 0);
	const u16* halves = reinterpret_cast<const u16*>(pixels.data());
	auto texel = [&](i32 x, i32 y, u32 channel) {
		return HalfToFloat(halves[(static_cast<size_t>(y) * size.x + x) * 4 + channel]);
	};
	i32 centerX = size.x / 2;
	i32 centerY = size.y / 2;
	f32 centerLuma = texel(centerX, centerY, 0) + texel(centerX, centerY, 1) + texel(centerX, centerY, 2);
	EXPECT_GT(centerLuma, 1.0f);
	f32 cornerLuma = texel(2, 2, 0) + texel(2, 2, 1) + texel(2, 2, 2);
	EXPECT_LT(cornerLuma, 0.01f);

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(meshAsset);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(RenderExitCriterionFixture, GetCamerasAnswersWorldScenesEnabledCameras) {
	Handle<Scene> world = SceneSystem::CreateScene("World");
	EXPECT_TRUE(RenderSystem::GetCameras().empty());

	Handle<Camera> first = SceneSystem::CreateObject("A", world)->AddComponent<Camera>();
	Handle<Camera> second = SceneSystem::CreateObject("B", world)->AddComponent<Camera>();
	ASSERT_EQ(RenderSystem::GetCameras().size(), 2u);

	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	Handle<Camera> hidden = SceneSystem::CreateObject("Hidden", preview)->AddComponent<Camera>();
	EXPECT_TRUE(hidden.IsValid());
	EXPECT_EQ(RenderSystem::GetCameras().size(), 2u);

	for (Handle<Camera> camera : RenderSystem::GetCameras()) {
		EXPECT_TRUE(camera.IsValid());
	}

	second->SetEnabled(false);
	EXPECT_EQ(RenderSystem::GetCameras().size(), 1u);
	EXPECT_EQ(RenderSystem::GetCameras()[0].GetInstanceId(), first.GetInstanceId());

	SceneSystem::UnloadScene(preview);
	SceneSystem::UnloadScene(world);
	SceneSystem::EndFrame();
	EXPECT_TRUE(RenderSystem::GetCameras().empty());
}

TEST_F(RenderExitCriterionFixture, TargetlessCameraUsesPrimary) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}

	WindowDesc desc;
	desc.title = "RenderSystemTest";
	desc.clientSize = {128, 128};
	desc.flags = WindowFlags::Hidden;
	Handle<Window> primary = WindowSystem::Create(desc);
	Handle<Window> secondary = WindowSystem::Create(desc);
	ASSERT_TRUE(primary.IsValid());
	ASSERT_TRUE(secondary.IsValid());
	ASSERT_EQ(WindowSystem::GetPrimaryWindow(), primary);

	Handle<Scene> scene = SceneSystem::CreateScene("PrimaryWindow");
	Handle<Camera> camera = SceneSystem::CreateObject("Camera", scene).Get()->AddComponent<Camera>();
	camera.Get()->SetClearColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
	ASSERT_FALSE(camera.Get()->GetTargetWindow().IsValid());

	for (int frame = 0; frame < 3; ++frame) {
		WindowSystem::PumpEvents();
		SceneSystem::UpdateAll();
		SceneSystem::EndFrame();
		RenderSystem::RenderAll();
	}

	auto centerIsRed = [](const Vector<u8>& pixels, Vec2I size) {
		const u16* halves = reinterpret_cast<const u16*>(pixels.data());
		size_t at = (static_cast<size_t>(size.y / 2) * size.x + size.x / 2) * 4;
		return HalfToFloat(halves[at]) > 0.5f && HalfToFloat(halves[at + 1]) < 0.5f &&
			   HalfToFloat(halves[at + 2]) < 0.5f;
	};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(primary, pixels, size));
	ASSERT_GT(size.x, 0);
	EXPECT_TRUE(centerIsRed(pixels, size));

	Vector<u8> otherPixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I otherSize{};
	if (RenderSystem::ReadbackWindowSceneColor(secondary, otherPixels, otherSize) && otherSize.x > 0) {
		EXPECT_FALSE(centerIsRed(otherPixels, otherSize));
	}
}

TEST_F(RenderExitCriterionFixture, RenderSizeLetterboxes) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	WindowDesc desc;
	desc.title = "RenderSystemTest";
	desc.clientSize = {256, 256};
	desc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("RenderSize");
	Handle<Camera> camera = SceneSystem::CreateObject("Camera", scene).Get()->AddComponent<Camera>();
	camera.Get()->SetClearColor(Color(0.0f, 0.0f, 1.0f, 1.0f));

	auto renderFrames = []() {
		for (int frame = 0; frame < 3; ++frame) {
			WindowSystem::PumpEvents();
			SceneSystem::UpdateAll();
			SceneSystem::EndFrame();
			RenderSystem::RenderAll();
		}
	};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};

	renderFrames();
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, pixels, size));
	EXPECT_EQ(size, Vec2I(256, 256));
	const u32 nativeDraws = RenderSystem::GetStats().drawCalls;

	WindowInternal::SetRenderSize(*window.Get(), {160, 90});
	renderFrames();
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, pixels, size));
	EXPECT_EQ(size, Vec2I(160, 90));
	EXPECT_EQ(RenderSystem::GetStats().drawCalls, nativeDraws + 1);

	WindowInternal::SetRenderSize(*window.Get(), {});
	renderFrames();
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, pixels, size));
	EXPECT_EQ(size, Vec2I(256, 256));
	EXPECT_EQ(RenderSystem::GetStats().drawCalls, nativeDraws);
}

TEST_F(RenderExitCriterionFixture, RenderAllWithoutCamerasIsNoOp) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RenderSystem::RenderAll();
	EXPECT_EQ(RenderSystem::GetStats().drawCalls, 0u);
}

TEST_F(RenderExitCriterionFixture, IsolatedCameraRendersNoWindow) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	WindowDesc desc;
	desc.title = "RenderSystemTest";
	desc.clientSize = {128, 128};
	desc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());

	auto driveThreeFrames = [] {
		for (int frame = 0; frame < 3; ++frame) {
			WindowSystem::PumpEvents();
			SceneSystem::UpdateAll();
			SceneSystem::EndFrame();
			RenderSystem::RenderAll();
		}
		return RenderSystem::GetStats().drawCalls;
	};

	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	Handle<Camera> hidden = SceneSystem::CreateObject("Camera", preview)->AddComponent<Camera>();
	hidden->SetClearColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_EQ(driveThreeFrames(), 0u);

	SceneSystem::UnloadScene(preview);
	SceneSystem::EndFrame();
	Handle<Scene> world = SceneSystem::CreateScene("World");
	Handle<Camera> shown = SceneSystem::CreateObject("Camera", world)->AddComponent<Camera>();
	shown->SetClearColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_GT(driveThreeFrames(), 0u);
}
