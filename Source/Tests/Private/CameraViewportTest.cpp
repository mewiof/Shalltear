#include "TestsPCH.h"

#include "Graphics/CameraViewport.h"
#include "Graphics/GraphicsSettings.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;
using GraphicsInternal::ViewportPixels;

TEST(CameraViewportTest, DefaultRectCoversTarget) {
	Camera camera;
	EXPECT_EQ(camera.GetViewportRect(), RectF(0.0f, 0.0f, 1.0f, 1.0f));
	EXPECT_EQ(ViewportPixels(camera.GetViewportRect(), {320, 180}), RectI(0, 0, 320, 180));
}

TEST(CameraViewportTest, OriginIsBottomLeft) {
	EXPECT_EQ(ViewportPixels(RectF(0.0f, 0.0f, 0.5f, 0.5f), {200, 100}), RectI(0, 50, 100, 50));
	EXPECT_EQ(ViewportPixels(RectF(0.5f, 0.5f, 0.5f, 0.5f), {200, 100}), RectI(100, 0, 100, 50));
	EXPECT_EQ(ViewportPixels(RectF(0.25f, 0.0f, 0.5f, 1.0f), {200, 100}), RectI(50, 0, 100, 100));
}

TEST(CameraViewportTest, NeighborsShareAnEdgeOnOddTargets) {
	const RectI left = ViewportPixels(RectF(0.0f, 0.0f, 0.5f, 1.0f), {255, 99});
	const RectI right = ViewportPixels(RectF(0.5f, 0.0f, 0.5f, 1.0f), {255, 99});
	EXPECT_EQ(left.Left(), 0);
	EXPECT_EQ(left.Right(), right.Left());
	EXPECT_EQ(right.Right(), 255);
	const RectI bottom = ViewportPixels(RectF(0.0f, 0.0f, 1.0f, 1.0f / 3.0f), {255, 99});
	const RectI top = ViewportPixels(RectF(0.0f, 1.0f / 3.0f, 1.0f, 2.0f / 3.0f), {255, 99});
	EXPECT_EQ(top.Top(), 0);
	EXPECT_EQ(top.Bottom(), bottom.Top());
	EXPECT_EQ(bottom.Bottom(), 99);
}

TEST(CameraViewportTest, RectIsClampedToTarget) {
	EXPECT_EQ(ViewportPixels(RectF(-0.25f, 0.5f, 0.5f, 1.0f), {200, 100}), RectI(0, 0, 50, 50));
	EXPECT_EQ(ViewportPixels(RectF(0.75f, -1.0f, 1.0f, 3.0f), {200, 100}), RectI(150, 0, 50, 100));
	EXPECT_EQ(ViewportPixels(RectF(1.5f, 0.0f, 0.5f, 1.0f), {200, 100}).size.x, 0);
	EXPECT_EQ(ViewportPixels(RectF(0.2f, 0.2f, -0.1f, 0.5f), {200, 100}).size.x, 0);
	EXPECT_EQ(ViewportPixels(RectF(0.2f, 0.2f, 0.5f, 0.0f), {200, 100}).size.y, 0);
}

namespace {

	std::filesystem::path FindRepoDir(const char* relative) {
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

	constexpr Color RED{1.0f, 0.0f, 0.0f, 1.0f};
	constexpr Color BLUE{0.0f, 0.0f, 1.0f, 1.0f};

	class CameraViewportFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mountedEngine = false;
		bool _mountedProject = false;
		Handle<Scene> _scene;

		void InitializeAt(i32 samples) {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_viewport_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");

			SettingsPaths paths;
			paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			Settings::Initialize(paths);

			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
			FileSystem::Mount("project", Path(_root.generic_string()));
			_mountedProject = true;
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			_scene = SceneSystem::CreateScene("Viewports");

			if (!RenderSystem::IsRenderingAvailable()) {
				return;
			}
			const Guid profileId =
				AssetDatabase::CreateAsset(Path("project://Assets/Viewports.slprofile"), StringHash("GraphicsProfile"));
			ASSERT_TRUE(profileId.IsValid());
			auto* profile = static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(profileId));
			ASSERT_NE(profile, nullptr);
			profile->msaa = samples;
			GraphicsSettings* settings =
				Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
			ASSERT_NE(settings, nullptr);
			settings->qualityProfiles = profileId.ToString();
			settings->qualityLevel = 0;
			Settings::NotifyChanged("Graphics", "qualityLevel");
			RenderSystem::RenderAll();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			if (_mountedProject) {
				FileSystem::Unmount("project");
			}
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static Handle<Window> HiddenWindow(Vec2I size) {
			WindowDesc desc;
			desc.title = "CameraViewportTest";
			desc.clientSize = size;
			desc.flags = WindowFlags::Hidden;
			return WindowSystem::Create(desc);
		}

		Handle<Camera> AddCamera(const char* name, const Color& clear, const RectF& rect, i32 priority = 0) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			Handle<Camera> camera = object->AddComponent<Camera>();
			camera->SetClearColor(clear);
			camera->SetViewportRect(rect);
			camera->SetPriority(priority);
			return camera;
		}

		static void RenderFrames() {
			for (int frame = 0; frame < 3; ++frame) {
				WindowSystem::PumpEvents();
				SceneSystem::UpdateAll();
				SceneSystem::EndFrame();
				RenderSystem::RenderAll();
			}
		}

		struct Radiance {
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};

			Vec3 At(i32 x, i32 y) const {
				const u16* halves = reinterpret_cast<const u16*>(pixels.data());
				const size_t at = (static_cast<size_t>(y) * size.x + x) * 4;
				return {Math::HalfToFloat(halves[at]), Math::HalfToFloat(halves[at + 1]), Math::HalfToFloat(halves[at + 2])};
			}
		};

		static Radiance ReadWindow(Handle<Window> window) {
			Radiance out;
			EXPECT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, out.pixels, out.size));
			return out;
		}

		static bool IsRed(const Vec3& c) { return c.x > 0.5f && c.y < 0.1f && c.z < 0.1f; }
		static bool IsBlue(const Vec3& c) { return c.z > 0.5f && c.x < 0.1f && c.y < 0.1f; }
		static bool IsBlack(const Vec3& c) { return c.x < 0.01f && c.y < 0.01f && c.z < 0.01f; }

		void ExpectSplitScreen() {
			Handle<Window> window = HiddenWindow({256, 128});
			ASSERT_TRUE(window.IsValid());
			AddCamera("Left", RED, RectF(0.0f, 0.0f, 0.5f, 1.0f));
			AddCamera("Right", BLUE, RectF(0.5f, 0.0f, 0.5f, 1.0f));
			RenderFrames();

			const Radiance image = ReadWindow(window);
			ASSERT_EQ(image.size, Vec2I(256, 128));
			for (i32 y : {0, 64, 127}) {
				EXPECT_TRUE(IsRed(image.At(0, y)));
				EXPECT_TRUE(IsRed(image.At(127, y)));
				EXPECT_TRUE(IsBlue(image.At(128, y)));
				EXPECT_TRUE(IsBlue(image.At(255, y)));
			}
		}
	};

} // anonymous namespace

TEST_F(CameraViewportFixture, SplitScreenFillsEachHalf) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ExpectSplitScreen();
}

TEST_F(CameraViewportFixture, SplitScreenAt4xFillsEachHalf) {
	InitializeAt(4);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (RenderSystem::GetSampleCount() != 4) {
		GTEST_SKIP() << "adapter doesn't offer 4x on the scene chain";
	}
	ExpectSplitScreen();
}

TEST_F(CameraViewportFixture, AreaOutsideEveryRectIsBlack) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> window = HiddenWindow({200, 100});
	ASSERT_TRUE(window.IsValid());
	Handle<Camera> camera = AddCamera("Preview", RED, RectF(0.0f, 0.0f, 1.0f, 1.0f));
	RenderFrames();
	ASSERT_TRUE(IsRed(ReadWindow(window).At(0, 50)));

	camera->SetViewportRect(RectF(0.3f, 0.0f, 0.7f, 1.0f));
	RenderFrames();
	const Radiance image = ReadWindow(window);
	ASSERT_EQ(image.size, Vec2I(200, 100));
	EXPECT_TRUE(IsBlack(image.At(0, 50)));
	EXPECT_TRUE(IsBlack(image.At(59, 50)));
	EXPECT_TRUE(IsRed(image.At(60, 50)));
	EXPECT_TRUE(IsRed(image.At(199, 0)));
}

TEST_F(CameraViewportFixture, HigherPriorityInsetReplacesItsRect) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> window = HiddenWindow({200, 100});
	ASSERT_TRUE(window.IsValid());
	AddCamera("Inset", BLUE, RectF(0.5f, 0.5f, 0.5f, 0.5f), 1);
	AddCamera("Main", RED, RectF(0.0f, 0.0f, 1.0f, 1.0f), 0);
	RenderFrames();

	const Radiance image = ReadWindow(window);
	ASSERT_EQ(image.size, Vec2I(200, 100));
	EXPECT_TRUE(IsBlue(image.At(100, 0)));
	EXPECT_TRUE(IsBlue(image.At(199, 49)));
	EXPECT_TRUE(IsRed(image.At(99, 0)));
	EXPECT_TRUE(IsRed(image.At(199, 50)));
	EXPECT_TRUE(IsRed(image.At(0, 99)));
}

TEST_F(CameraViewportFixture, PartialRectCostsOneDraw) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> window = HiddenWindow({128, 128});
	ASSERT_TRUE(window.IsValid());
	Handle<Camera> camera = AddCamera("Camera", RED, RectF(0.0f, 0.0f, 1.0f, 1.0f));
	RenderFrames();
	const u32 fullDraws = RenderSystem::GetStats().drawCalls;
	ASSERT_GT(fullDraws, 0u);

	camera->SetViewportRect(RectF(0.25f, 0.25f, 0.5f, 0.5f));
	RenderFrames();
	EXPECT_EQ(RenderSystem::GetStats().drawCalls, fullDraws + 1);

	camera->SetViewportRect(RectF(0.0f, 0.0f, 1.0f, 1.0f));
	RenderFrames();
	EXPECT_EQ(RenderSystem::GetStats().drawCalls, fullDraws);
}

TEST_F(CameraViewportFixture, EmptyRectDrawsNothing) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> window = HiddenWindow({128, 128});
	ASSERT_TRUE(window.IsValid());
	AddCamera("Camera", RED, RectF(0.5f, 0.0f, 0.0f, 1.0f));
	RenderFrames();
	EXPECT_EQ(RenderSystem::GetStats().drawCalls, 0u);
}

TEST_F(CameraViewportFixture, RectSetsTheAspect) {
	InitializeAt(1);
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

	Handle<Window> window = HiddenWindow({256, 256});
	ASSERT_TRUE(window.IsValid());
	Handle<Camera> camera = AddCamera("Camera", Color::Black(), RectF(0.0f, 0.0f, 0.5f, 1.0f));
	camera->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, -3.0f));
	Handle<Light> light = SceneSystem::CreateObject("Sun", _scene)->AddComponent<Light>();
	light->SetIntensity(2.0f);
	Handle<MeshRenderer> renderer = SceneSystem::CreateObject("Cube", _scene)->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(mesh));
	renderer->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(materialAsset)));
	RenderFrames();

	const Radiance image = ReadWindow(window);
	ASSERT_EQ(image.size, Vec2I(256, 256));
	i32 left = image.size.x;
	i32 right = -1;
	i32 top = image.size.y;
	i32 bottom = -1;
	for (i32 y = 0; y < image.size.y; ++y) {
		for (i32 x = 0; x < image.size.x; ++x) {
			if (IsBlack(image.At(x, y))) {
				continue;
			}
			left = Math::Min(left, x);
			right = Math::Max(right, x);
			top = Math::Min(top, y);
			bottom = Math::Max(bottom, y);
		}
	}
	ASSERT_GE(right, left);
	EXPECT_LT(right, 128);
	EXPECT_NEAR(static_cast<f32>(left + right) * 0.5f, 63.5f, 1.0f);
	EXPECT_NEAR(static_cast<f32>(top + bottom) * 0.5f, 127.5f, 1.0f);
	const i32 width = right - left + 1;
	const i32 height = bottom - top + 1;
	EXPECT_GT(height, 60);
	EXPECT_NEAR(width, height, 2);

	SceneSystem::UnloadScene(_scene);
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(meshAsset);
}

TEST_F(CameraViewportFixture, SubmittedCamerasComposeIntoTarget) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	const ViewTargetId target = RenderSystem::CreateViewTarget({128, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const Handle<Camera> cameras[] = {
		AddCamera("Right", BLUE, RectF(0.5f, 0.0f, 0.5f, 1.0f), 1),
		AddCamera("Left", RED, RectF(0.0f, 0.0f, 0.5f, 1.0f), 0),
	};
	for (int frame = 0; frame < 3; ++frame) {
		SceneSystem::EndFrame();
		RenderSystem::SubmitCameras(target, cameras);
		RenderSystem::RenderAll();
	}

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	ASSERT_EQ(size, Vec2I(128, 64));
	auto texel = [&](i32 x, i32 y) {
		const size_t at = (static_cast<size_t>(y) * size.x + x) * 4;
		return Vec3(pixels[at], pixels[at + 1], pixels[at + 2]);
	};
	for (i32 y : {0, 32, 63}) {
		EXPECT_GT(texel(63, y).x, 128.0f);
		EXPECT_LT(texel(63, y).z, 16.0f);
		EXPECT_GT(texel(64, y).z, 128.0f);
		EXPECT_LT(texel(64, y).x, 16.0f);
	}
	RenderSystem::DestroyViewTarget(target);
}

TEST_F(CameraViewportFixture, LatestSubmissionPerTargetRenders) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	const ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const Handle<Camera> red[] = {AddCamera("Red", RED, RectF(0.0f, 0.0f, 1.0f, 1.0f))};
	const Handle<Camera> blue[] = {AddCamera("Blue", BLUE, RectF(0.0f, 0.0f, 1.0f, 1.0f))};
	for (int frame = 0; frame < 3; ++frame) {
		SceneSystem::EndFrame();
		RenderSystem::SubmitCameras(target, blue);
		RenderSystem::RenderAll();
	}
	const u32 oneSubmission = RenderSystem::GetStats().drawCalls;
	ASSERT_GT(oneSubmission, 0u);

	for (int frame = 0; frame < 3; ++frame) {
		SceneSystem::EndFrame();
		RenderSystem::SubmitCameras(target, blue);
		RenderSystem::SubmitCameras(target, red);
		RenderSystem::RenderAll();
	}
	EXPECT_EQ(RenderSystem::GetStats().drawCalls, oneSubmission);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	ASSERT_EQ(size, Vec2I(64, 64));
	const size_t center = (static_cast<size_t>(32) * size.x + 32) * 4;
	EXPECT_GT(pixels[center], 128);
	EXPECT_LT(pixels[center + 2], 16);
	RenderSystem::DestroyViewTarget(target);
}

TEST_F(CameraViewportFixture, SubmittedCameraDyingClearsTarget) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	const ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const Handle<Camera> cameras[] = {AddCamera("Doomed", RED, RectF(0.0f, 0.0f, 1.0f, 1.0f))};
	auto renderFrames = [&]() {
		for (int frame = 0; frame < 3; ++frame) {
			SceneSystem::EndFrame();
			RenderSystem::SubmitCameras(target, cameras);
			RenderSystem::RenderAll();
		}
	};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	const size_t center = (static_cast<size_t>(32) * 64 + 32) * 4;

	renderFrames();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	ASSERT_EQ(size, Vec2I(64, 64));
	EXPECT_GT(pixels[center], 128);

	SceneSystem::Destroy(cameras[0]->GetOwner());
	renderFrames();
	EXPECT_FALSE(cameras[0].IsValid());
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	EXPECT_LT(pixels[center], 16);
	EXPECT_LT(pixels[center + 2], 16);
	RenderSystem::DestroyViewTarget(target);
}
