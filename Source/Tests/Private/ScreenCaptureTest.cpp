#include "TestsPCH.h"

#include "Checkout.h"

#include "Platform/XDGUserDirs.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/ImageEncoding.h"
#include "Shalltear/Core/Graphics/ScreenCapture.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window.h"

#if !defined(SHALLTEAR_SHARED)
#include "Graphics/FrameCapture.h"
#endif

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>

using namespace Shalltear;

namespace {

	namespace fs = std::filesystem;

	fs::path TempRoot(const char* stem) {
		static std::atomic<u64> counter{0};
		const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
		fs::path root = fs::temp_directory_path() / (std::string(stem) + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
		fs::create_directories(root);
		return root;
	}

	bool DecodePNG(const fs::path& file, TextureAsset& out) {
		out.settings.generateMips = false;
		out.settings.shape = TextureShape::Texture2D;
		out.settings.compression = TextureCompression::None;
		return out.Import(Path(file.generic_string()));
	}

} // anonymous namespace

TEST(ImageEncodingTest, PNGRoundTripsOddSize) {
	constexpr Vec2I Size{37, 19};
	Vector<u8> rgba{GetMemoryResource(MemTag::Graphics)};
	u32 state = 99u;
	for (i32 i = 0; i < Size.x * Size.y * 4; ++i) {
		state = state * 1664525u + 1013904223u;
		rgba.push_back(static_cast<u8>(state >> 24));
	}
	Vector<u8> png{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(ImageEncoding::EncodePNG(rgba, Size, png));

	const fs::path root = TempRoot("shalltear_png_");
	{
		std::ofstream out(root / "Noise.png", std::ios::binary);
		out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
	}
	TextureAsset decoded;
	ASSERT_TRUE(DecodePNG(root / "Noise.png", decoded));
	EXPECT_EQ(decoded.size, Size);
	ASSERT_EQ(decoded.pixels.size(), rgba.size());
	EXPECT_TRUE(std::equal(rgba.begin(), rgba.end(), decoded.pixels.begin()));

	Vector<u8> refused{GetMemoryResource(MemTag::Graphics)};
	EXPECT_FALSE(ImageEncoding::EncodePNG({rgba.data(), rgba.size() - 4}, Size, refused));
	EXPECT_FALSE(ImageEncoding::EncodePNG(rgba, Vec2I(0, 19), refused));
	EXPECT_TRUE(refused.empty());
	std::error_code ec;
	fs::remove_all(root, ec);
}

TEST(XDGUserDirsTest, PicturesFromUserDirs) {
	constexpr std::string_view Config =
		"# written by xdg-user-dirs-update\n"
		"XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
		"  XDG_PICTURES_DIR=\"$HOME/Bilder\"\r\n"
		"XDG_VIDEOS_DIR=\"/media/videos\"\n";
	EXPECT_EQ(Platform::ParseXDGUserDir(Config, "XDG_PICTURES_DIR", "/home/ann"), "/home/ann/Bilder");
	EXPECT_EQ(Platform::ParseXDGUserDir(Config, "XDG_VIDEOS_DIR", "/home/ann"), "/media/videos");
	EXPECT_EQ(Platform::ParseXDGUserDir(Config, "XDG_MUSIC_DIR", "/home/ann"), "");
	EXPECT_EQ(Platform::ParseXDGUserDir(Config, "XDG_PICTURES", "/home/ann"), "");
	EXPECT_EQ(Platform::ParseXDGUserDir("XDG_PICTURES_DIR=Pictures\n", "XDG_PICTURES_DIR", "/home/ann"), "");
	EXPECT_EQ(Platform::ParseXDGUserDir("XDG_PICTURES_DIR=\"relative\"", "XDG_PICTURES_DIR", "/home/ann"), "");
	EXPECT_EQ(Platform::ParseXDGUserDir(Config, "XDG_PICTURES_DIR", ""), "");
}

TEST(FileSystemTest, PicturesDirectoryKnown) {
	const Path pictures = FileSystem::GetUserPicturesDirectory();
	ASSERT_FALSE(pictures.ToString().empty());
}

namespace {

	constexpr Color Red{1.0f, 0.0f, 0.0f, 1.0f};
	constexpr Color Blue{0.0f, 0.0f, 1.0f, 1.0f};

	class ScreenCaptureFixture : public ::testing::Test {
	protected:
		fs::path _root;
		bool _mountedEngine = false;
		Handle<Scene> _scene;

		void SetUp() override {
			_root = TempRoot("shalltear_capture_");
			SettingsPaths paths;
			paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			Settings::Initialize(paths);
			const fs::path engineAssets = Checkout::Find("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			_scene = SceneSystem::CreateScene("Capture");
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			Settings::Terminate();
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		Handle<Camera> AddCamera(const char* name, const Color& clear, const RectF& rect, i32 priority = 0) {
			Handle<Camera> camera = SceneSystem::CreateObject(name, _scene)->AddComponent<Camera>();
			camera->SetClearColor(clear);
			camera->SetBackgroundType(BackgroundType::SolidColor);
			camera->SetViewportRect(rect);
			camera->SetPriority(priority);
			return camera;
		}

		static void Frame() {
			WindowSystem::PumpEvents();
			SceneSystem::UpdateAll();
			FrameStages::Run(FrameStage::EndFrame, FrameStageArgs{});
			RenderSystem::RenderAll();
		}

		static Vec3 Texel(const Vector<u8>& rgba, Vec2I size, i32 x, i32 y) {
			const size_t at = (static_cast<size_t>(y) * size.x + x) * 4;
			return Vec3(rgba[at], rgba[at + 1], rgba[at + 2]);
		}
	};

} // anonymous namespace

TEST_F(ScreenCaptureFixture, CameraCaptureWritesPNGLater) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	const Handle<Camera> camera = AddCamera("Red", Red, RectF(0.0f, 0.0f, 1.0f, 1.0f));
	const fs::path file = _root / "Shots" / "Red.png";

	std::optional<ScreenCaptureResult> result;
	ASSERT_TRUE(ScreenCapture::CaptureCamera(camera, {80, 40}, file.generic_string(),
											 [&result](const ScreenCaptureResult& done) { result = done; }));
	Frame();
	EXPECT_FALSE(result.has_value());
	for (int frame = 0; frame < 120 && !result; ++frame) {
		Frame();
	}
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->succeeded);
	EXPECT_EQ(result->size, Vec2I(80, 40));
	EXPECT_EQ(result->path, file.generic_string());

	TextureAsset decoded;
	ASSERT_TRUE(DecodePNG(file, decoded));
	ASSERT_EQ(decoded.size, Vec2I(80, 40));
	for (i32 y : {0, 20, 39}) {
		for (i32 x : {0, 40, 79}) {
			EXPECT_GT(Texel(decoded.pixels, decoded.size, x, y).x, 150.0f);
			EXPECT_LT(Texel(decoded.pixels, decoded.size, x, y).z, 30.0f);
		}
	}
	EXPECT_EQ(decoded.pixels[3], 255);
}

TEST_F(ScreenCaptureFixture, ImageUnloadDropsPendingCallback) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	const Handle<Camera> camera = AddCamera("Red", Red, RectF(0.0f, 0.0f, 1.0f, 1.0f));
	const fs::path file = _root / "Dropped.png";
	bool called = false;
	ASSERT_TRUE(ScreenCapture::CaptureCamera(camera, {16, 16}, file.generic_string(),
											 [&called](const ScreenCaptureResult&) { called = true; }));
	Frame();
	ModuleSystem::Initialize();
	EXPECT_FALSE(ModuleSystem::IsLoaded(ModuleSystem::Load(Path("ShalltearHotReloadModuleAbi" SLT_TEST_DLL_SUFFIX))));
	ModuleSystem::Terminate();
	for (int frame = 0; frame < 120 && !fs::exists(file); ++frame) {
		Frame();
	}
	for (int frame = 0; frame < 8; ++frame) {
		Frame();
	}
	EXPECT_TRUE(fs::exists(file));
	EXPECT_FALSE(called);
}

TEST_F(ScreenCaptureFixture, RefusesWhatCannotCapture) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	const Handle<Camera> camera = AddCamera("Camera", Red, RectF(0.0f, 0.0f, 1.0f, 1.0f));
	EXPECT_FALSE(ScreenCapture::CaptureCamera(camera, {0, 40}, (_root / "a.png").generic_string()));
	EXPECT_FALSE(ScreenCapture::CaptureCamera({}, {40, 40}, (_root / "a.png").generic_string()));
	EXPECT_FALSE(ScreenCapture::CaptureCamera(camera, {40, 40}, ""));
	EXPECT_FALSE(ScreenCapture::CaptureScreenshot((_root / "a.png").generic_string()));
}

#if !defined(SHALLTEAR_SHARED)

TEST_F(ScreenCaptureFixture, WindowCaptureIsFrameAsShown) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	WindowDesc desc;
	desc.title = "ScreenCaptureTest";
	desc.clientSize = {96, 64};
	desc.flags = WindowFlags::Hidden;
	const Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());
	AddCamera("Left", Red, RectF(0.0f, 0.0f, 0.5f, 1.0f));
	AddCamera("Right", Blue, RectF(0.5f, 0.0f, 0.5f, 1.0f));
	Frame();

	bool delivered = false;
	bool captured = false;
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	GraphicsInternal::RequestWindowCapture(window, [&](bool ok, Vector<u8>&& rgba8, Vec2I at) -> GraphicsInternal::CaptureFollowUp {
		delivered = true;
		captured = ok;
		pixels = std::move(rgba8);
		size = at;
		return {};
	});
	for (int frame = 0; frame < 10 && !delivered; ++frame) {
		Frame();
	}
	ASSERT_TRUE(delivered);
	ASSERT_TRUE(captured);
	ASSERT_EQ(size, window->GetClientSize());
	for (i32 y : {0, size.y / 2, size.y - 1}) {
		EXPECT_GT(Texel(pixels, size, 1, y).x, 150.0f);
		EXPECT_LT(Texel(pixels, size, 1, y).z, 30.0f);
		EXPECT_GT(Texel(pixels, size, size.x - 2, y).z, 150.0f);
		EXPECT_LT(Texel(pixels, size, size.x - 2, y).x, 30.0f);
	}
	EXPECT_EQ(pixels[3], 255);
}

TEST_F(ScreenCaptureFixture, SceneCaptureComposesEveryCamera) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	AddCamera("Left", Red, RectF(0.0f, 0.0f, 0.5f, 1.0f));
	AddCamera("Right", Blue, RectF(0.5f, 0.0f, 0.5f, 1.0f));
	Frame();

	bool delivered = false;
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	GraphicsInternal::RequestSceneCapture({64, 32}, [&](bool ok, Vector<u8>&& rgba8, Vec2I at) -> GraphicsInternal::CaptureFollowUp {
		delivered = ok;
		pixels = std::move(rgba8);
		size = at;
		return {};
	});
	for (int frame = 0; frame < 10 && !delivered; ++frame) {
		Frame();
	}
	ASSERT_TRUE(delivered);
	ASSERT_EQ(size, Vec2I(64, 32));
	EXPECT_GT(Texel(pixels, size, 5, 16).x, 150.0f);
	EXPECT_LT(Texel(pixels, size, 5, 16).z, 30.0f);
	EXPECT_GT(Texel(pixels, size, 60, 16).z, 150.0f);
	EXPECT_LT(Texel(pixels, size, 60, 16).x, 30.0f);
}

#endif
