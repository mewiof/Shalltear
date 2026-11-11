#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/FullScreenPass.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ObjectPass.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Core/Math/HalfFloat.h"
#include "PassProbes.h"
#include "ShadingHarness.h"

#include <chrono>
#include <cmath>
#include <filesystem>

using namespace Shalltear;
using namespace PassProbes;
using ShadingTest::RepoDir;
using Shalltear::Math::HalfToFloat;

namespace {

	class CameraPassRowFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mounted = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_camrow_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
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

		f32 PaintedFraction(Handle<Window> window) {
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			if (!RenderSystem::ReadbackWindowSceneColor(window, pixels, size) || size.x <= 0 || size.y <= 0) {
				return -1.0f;
			}
			const u16* halves = reinterpret_cast<const u16*>(pixels.data());
			u32 painted = 0;
			u32 sampled = 0;
			for (i32 y = 0; y < size.y; ++y) {
				for (i32 x = 0; x < size.x; ++x) {
					const size_t base = (static_cast<size_t>(y) * size.x + x) * 4;
					const f32 luminance = HalfToFloat(halves[base]) + HalfToFloat(halves[base + 1]) +
										  HalfToFloat(halves[base + 2]);
					++sampled;
					if (luminance > 0.01f) {
						++painted;
					}
				}
			}
			return sampled == 0 ? -1.0f : static_cast<f32>(painted) / static_cast<f32>(sampled);
		}

		void DriveFrames(u32 count) {
			for (u32 frame = 0; frame < count; ++frame) {
				WindowSystem::PumpEvents();
				SceneSystem::UpdateAll();
				SceneSystem::EndFrame();
				RenderSystem::RenderAll();
			}
		}
	};

} // anonymous namespace

TEST_F(CameraPassRowFixture, CameraRowSurvivesLaterPass) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Asset* shaderAsset = AssetDatabase::LoadAsset(Path("engine://Shaders/EditorGrid.hlsl"));
	if (!shaderAsset) {
		GTEST_SKIP() << "EditorGrid.hlsl unavailable (dxcompiler.dll not staged?)";
	}
	Handle<ShaderAsset> shader(static_cast<ShaderAsset*>(shaderAsset));

	WindowDesc windowDesc;
	windowDesc.title = "CameraPassRowTest";
	windowDesc.clientSize = {256, 256};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("CameraRows");
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	cameraObject.Get()->SetLocalPosition(Vec3(0.0f, 3.0f, -6.0f));
	cameraObject.Get()->SetLocalRotation(Quat::FromEuler(Vec3(15.0f, 0.0f, 0.0f)));
	Handle<Camera> camera = cameraObject.Get()->AddComponent<Camera>();
	camera.Get()->SetTargetWindow(window);
	camera.Get()->SetClearColor(Color::Black());

	DrawingProbePass* row = New<DrawingProbePass>(MemTag::Asset);
	ASSERT_NE(row, nullptr);
	row->shader = shader;
	GridPush push = MakeVisiblePush(cameraObject.Get()->GetWorldMatrix().Inverse(),
									camera.Get()->BuildProjection(1.0f),
									cameraObject.Get()->GetWorldPosition());
	row->bytes = {reinterpret_cast<const u8*>(&push), PassProbes::GRID_PUSH_BYTES};
	camera.Get()->SetPass(0, Handle<RenderPass>(row));
	ASSERT_EQ(camera.Get()->GetPassCount(), 1u);

	row->point = PassPoint::BeforeOpaque;
	DriveFrames(3);
	const f32 before = PaintedFraction(window);
	ASSERT_GE(before, 0.0f);

	row->point = PassPoint::AfterOpaque;
	DriveFrames(3);
	const f32 after = PaintedFraction(window);
	ASSERT_GE(after, 0.0f);

	EXPECT_GT(after, before + 0.05f);
	EXPECT_LT(before, 0.02f);
	EXPECT_GT(after, 0.10f);

	camera.Get()->SetPass(0, {});
	Delete(row);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(shaderAsset);
}

TEST_F(CameraPassRowFixture, FrameLevelPointOnCameraRefused) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Asset* shaderAsset = AssetDatabase::LoadAsset(Path("engine://Shaders/EditorGrid.hlsl"));
	if (!shaderAsset) {
		GTEST_SKIP() << "EditorGrid.hlsl unavailable (dxcompiler.dll not staged?)";
	}
	Handle<ShaderAsset> shader(static_cast<ShaderAsset*>(shaderAsset));

	WindowDesc windowDesc;
	windowDesc.title = "CameraPassRowTestRefusal";
	windowDesc.clientSize = {256, 256};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("CameraRowRefusal");
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	cameraObject.Get()->SetLocalPosition(Vec3(0.0f, 3.0f, -6.0f));
	cameraObject.Get()->SetLocalRotation(Quat::FromEuler(Vec3(15.0f, 0.0f, 0.0f)));
	Handle<Camera> camera = cameraObject.Get()->AddComponent<Camera>();
	camera.Get()->SetTargetWindow(window);
	camera.Get()->SetClearColor(Color::Black());

	DrawingProbePass* row = New<DrawingProbePass>(MemTag::Asset);
	ASSERT_NE(row, nullptr);
	row->shader = shader;
	GridPush push = MakeVisiblePush(cameraObject.Get()->GetWorldMatrix().Inverse(),
									camera.Get()->BuildProjection(1.0f),
									cameraObject.Get()->GetWorldPosition());
	row->bytes = {reinterpret_cast<const u8*>(&push), PassProbes::GRID_PUSH_BYTES};
	camera.Get()->SetPass(0, Handle<RenderPass>(row));

	row->point = PassPoint::Skinning;
	row->drew = false;
	DriveFrames(3);
	EXPECT_FALSE(row->drew);
	const f32 refused = PaintedFraction(window);
	ASSERT_GE(refused, 0.0f);
	EXPECT_LT(refused, 0.02f);

	row->point = PassPoint::AfterOpaque;
	row->drew = false;
	DriveFrames(3);
	EXPECT_TRUE(row->drew);
	EXPECT_GT(PaintedFraction(window), 0.10f);

	camera.Get()->SetPass(0, {});
	Delete(row);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(shaderAsset);
}

TEST_F(CameraPassRowFixture, CullingMaskDropsUnnamedLayers) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	if (!unlit) {
		GTEST_SKIP() << "Unlit.hlsl unavailable (dxcompiler.dll not staged?)";
	}
	ASSERT_TRUE(FileSystem::WriteTextFile(
		FileSystem::Resolve(Path("project://MaskQuad.mat")),
		"{\n\t\"shader\": \"" + unlit->assetId.ToString() + "\",\n\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }\n}\n"));
	Asset* white = AssetDatabase::LoadAsset(Path("project://MaskQuad.mat"));
	Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
	ASSERT_NE(white, nullptr);
	ASSERT_NE(quadMesh, nullptr);

	WindowDesc windowDesc;
	windowDesc.title = "CameraCullingMask";
	windowDesc.clientSize = {128, 128};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	constexpr u32 HAND_LAYER = 1u << 1;
	Handle<Scene> scene = SceneSystem::CreateScene("CullingMask");
	Handle<SceneObject> quad = SceneSystem::CreateObject("Quad", scene);
	Handle<MeshRenderer> renderer = quad.Get()->AddComponent<MeshRenderer>();
	renderer.Get()->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
	renderer.Get()->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(white)));
	renderer.Get()->SetRenderingLayerMask(HAND_LAYER);
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	cameraObject.Get()->SetLocalPosition(Vec3(0.0f, 0.0f, 3.0f));
	cameraObject.Get()->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
	Handle<Camera> camera = cameraObject.Get()->AddComponent<Camera>();
	camera.Get()->SetTargetWindow(window);
	camera.Get()->SetClearColor(Color::Black());

	camera.Get()->SetCullingMask(~HAND_LAYER);
	DriveFrames(3);
	const f32 culled = PaintedFraction(window);
	camera.Get()->SetCullingMask(~0u);
	DriveFrames(3);
	const f32 drawn = PaintedFraction(window);

	ASSERT_GE(culled, 0.0f);
	EXPECT_EQ(culled, 0.0f);
	EXPECT_GT(drawn, 0.02f);

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(CameraPassRowFixture, ColorRowReadsSkinnedScene) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))) {
		GTEST_SKIP() << "Unlit.hlsl unavailable (dxcompiler.dll not staged?)";
	}
	ASSERT_TRUE(FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Rotate.hlsl")), CHANNEL_ROTATE_SHADER));
	Asset* rotate = AssetDatabase::LoadAsset(Path("project://Rotate.hlsl"));
	ASSERT_NE(rotate, nullptr);
	ASSERT_TRUE(FileSystem::WriteTextFile(
		FileSystem::Resolve(Path("project://Rotate.mat")),
		"{\n\t\"shader\": \"" + rotate->assetId.ToString() + "\",\n\t\"colors\": { \"tint\": [1, 1, 1, 1] }\n}\n"));
	Asset* material = AssetDatabase::LoadAsset(Path("project://Rotate.mat"));
	ASSERT_NE(material, nullptr);

	WindowDesc windowDesc;
	windowDesc.title = "CameraRowSkinned";
	windowDesc.clientSize = {128, 128};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("CameraRowSkinned");
	ASSERT_TRUE(AddSkinnedMesh(RepoDir("Source/Tests/Fixtures/Graphics"), _root, scene, Color(0.8f, 0.4f, 0.1f)).IsValid());
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	cameraObject.Get()->SetLocalPosition(Vec3(0.0f, 1.0f, 4.0f));
	cameraObject.Get()->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
	Handle<Camera> camera = cameraObject.Get()->AddComponent<Camera>();
	camera.Get()->SetTargetWindow(window);
	camera.Get()->SetClearColor(Color::Black());

	DriveFrames(3);
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, reference, referenceSize));
	ASSERT_GT(PaintedFraction(window), 0.02f);

	FullScreenPass* row = New<FullScreenPass>(MemTag::Asset);
	ASSERT_NE(row, nullptr);
	row->point = PassPoint::Post;
	row->material = Handle<MaterialAsset>(static_cast<MaterialAsset*>(material));
	camera.Get()->SetPass(0, Handle<RenderPass>(row));
	DriveFrames(3);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, pixels, size));
	ASSERT_EQ(size, referenceSize);

	const u16* before = reinterpret_cast<const u16*>(reference.data());
	const u16* after = reinterpret_cast<const u16*>(pixels.data());
	u32 mismatched = 0;
	for (size_t texel = 0; texel < static_cast<size_t>(size.x) * size.y; ++texel) {
		for (size_t channel = 0; channel < 3; ++channel) {
			if (std::abs(HalfToFloat(after[texel * 4 + channel]) - HalfToFloat(before[texel * 4 + (channel + 1) % 3])) > 1e-3f) {
				++mismatched;
				break;
			}
		}
	}
	EXPECT_EQ(mismatched, 0u);
	EXPECT_GT(PaintedFraction(window), 0.02f);

	camera.Get()->SetPass(0, {});
	Delete(row);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(CameraPassRowFixture, ColorRowReadsEarlierColorRow) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))) {
		GTEST_SKIP() << "Unlit.hlsl unavailable (dxcompiler.dll not staged?)";
	}
	ASSERT_TRUE(FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Rotate.hlsl")), CHANNEL_ROTATE_SHADER));
	Asset* rotate = AssetDatabase::LoadAsset(Path("project://Rotate.hlsl"));
	ASSERT_NE(rotate, nullptr);
	ASSERT_TRUE(FileSystem::WriteTextFile(
		FileSystem::Resolve(Path("project://Rotate.mat")),
		"{\n\t\"shader\": \"" + rotate->assetId.ToString() + "\",\n\t\"colors\": { \"tint\": [1, 1, 1, 1] }\n}\n"));
	Asset* material = AssetDatabase::LoadAsset(Path("project://Rotate.mat"));
	ASSERT_NE(material, nullptr);

	WindowDesc windowDesc;
	windowDesc.title = "CameraRowChain";
	windowDesc.clientSize = {128, 128};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("CameraRowChain");
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	Handle<Camera> camera = cameraObject.Get()->AddComponent<Camera>();
	camera.Get()->SetTargetWindow(window);
	camera.Get()->SetClearColor(Color(1.0f, 0.0f, 0.0f));

	DriveFrames(3);
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, reference, referenceSize));

	FullScreenPass* first = New<FullScreenPass>(MemTag::Asset);
	FullScreenPass* second = New<FullScreenPass>(MemTag::Asset);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	first->point = PassPoint::BeforePost;
	first->material = Handle<MaterialAsset>(static_cast<MaterialAsset*>(material));
	second->point = PassPoint::Post;
	second->material = Handle<MaterialAsset>(static_cast<MaterialAsset*>(material));
	camera.Get()->SetPass(0, Handle<RenderPass>(first));
	camera.Get()->SetPass(1, Handle<RenderPass>(second));
	DriveFrames(3);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, pixels, size));
	ASSERT_EQ(size, referenceSize);

	const u16* before = reinterpret_cast<const u16*>(reference.data());
	const u16* after = reinterpret_cast<const u16*>(pixels.data());
	const f32 red = HalfToFloat(before[0]);
	EXPECT_GT(red, 0.5f);
	u32 mismatched = 0;
	for (size_t texel = 0; texel < static_cast<size_t>(size.x) * size.y; ++texel) {
		if (std::abs(HalfToFloat(after[texel * 4]) - HalfToFloat(before[texel * 4 + 2])) > 1e-3f ||
			std::abs(HalfToFloat(after[texel * 4 + 1]) - HalfToFloat(before[texel * 4])) > 1e-3f ||
			std::abs(HalfToFloat(after[texel * 4 + 2]) - HalfToFloat(before[texel * 4 + 1])) > 1e-3f) {
			++mismatched;
		}
	}
	EXPECT_EQ(mismatched, 0u);
	EXPECT_NEAR(HalfToFloat(after[1]), red, 1e-3f);

	camera.Get()->SetPass(0, {});
	camera.Get()->SetPass(1, {});
	Delete(first);
	Delete(second);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}
