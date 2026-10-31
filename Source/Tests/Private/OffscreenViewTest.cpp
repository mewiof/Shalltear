#include "TestsPCH.h"

#include <algorithm>
#include <array>

#include "Graphics/GraphicsSettings.h"
#include "Graphics/ShadowCascades.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/ProjectOps.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

using namespace Shalltear;

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

	class OffscreenViewFixture : public ::testing::Test {
	protected:
		bool _mounted = false;

		void SetUp() override {
			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
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
			}
		}
	};

	class MSAAViewFixture : public ::testing::Test {
	protected:
		bool _mounted = false;
		bool _projectMounted = false;
		std::filesystem::path _root;

		void InitializeAt(i32 samples) {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_msaa_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
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
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();

			if (!RenderSystem::IsRenderingAvailable()) {
				return;
			}
			const Guid profileId =
				AssetDatabase::CreateAsset(Path("project://Assets/Msaa.slprofile"), StringHash("GraphicsProfile"));
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
	};

	struct RGBA {
		u8 r = 0, g = 0, b = 0, a = 0;
		bool operator==(const RGBA&) const = default;
	};

	RGBA TexelAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return {pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
	}

	RGBA BrightestTexel(const Vector<u8>& pixels, Vec2I size) {
		RGBA best;
		u32 bestSum = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				RGBA texel = TexelAt(pixels, size, x, y);
				u32 sum = static_cast<u32>(texel.r) + texel.g + texel.b;
				if (sum > bestSum) {
					bestSum = sum;
					best = texel;
				}
			}
		}
		return best;
	}

	u32 CountDistinct(const Vector<u8>& pixels) {
		HashSet<u32> distinct{GetMemoryResource(MemTag::Graphics)};
		const u32* words = reinterpret_cast<const u32*>(pixels.data());
		for (size_t i = 0, n = pixels.size() / 4; i < n; ++i) {
			distinct.insert(words[i]);
		}
		return static_cast<u32>(distinct.size());
	}

	Mat4 LookForwardView(Vec3 eye) {
		return Mat4::TRS(eye, Quat::Identity(), Vec3::One()).Inverse();
	}

} // anonymous namespace

TEST(OffscreenViewHeadlessTest, APIIsInertWithoutARenderer) {
	ASSERT_FALSE(RenderSystem::IsInitialized());
	EXPECT_EQ(RenderSystem::CreateViewTarget({64, 64}), INVALID_VIEW_TARGET);
	EXPECT_FALSE(RenderSystem::GetViewTargetTexture(1).IsValid());
	EXPECT_EQ(RenderSystem::GetViewTargetSize(1), Vec2I{});
	RenderSystem::ResizeViewTarget(1, {32, 32});
	RenderSystem::DestroyViewTarget(1);
	RenderSystem::SubmitView(1, ViewDesc{});
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	EXPECT_FALSE(RenderSystem::ReadbackViewTarget(1, pixels, size));
}

TEST_F(OffscreenViewFixture, TargetLifetimeAndSizing) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ViewTargetId a = RenderSystem::CreateViewTarget({128, 64});
	ViewTargetId b = RenderSystem::CreateViewTarget({32, 32});
	ASSERT_NE(a, INVALID_VIEW_TARGET);
	ASSERT_NE(b, INVALID_VIEW_TARGET);
	EXPECT_NE(a, b);
	EXPECT_EQ(RenderSystem::GetViewTargetSize(a), (Vec2I{128, 64}));
	EXPECT_TRUE(RenderSystem::GetViewTargetTexture(a).IsValid());

	RenderSystem::ResizeViewTarget(a, {96, 96});
	EXPECT_EQ(RenderSystem::GetViewTargetSize(a), (Vec2I{96, 96}));
	RHI::TextureHandle before = RenderSystem::GetViewTargetTexture(a);
	RenderSystem::ResizeViewTarget(a, {96, 96});
	EXPECT_EQ(RenderSystem::GetViewTargetTexture(a), before);

	RenderSystem::DestroyViewTarget(a);
	EXPECT_FALSE(RenderSystem::GetViewTargetTexture(a).IsValid());
	EXPECT_EQ(RenderSystem::GetViewTargetSize(a), Vec2I{});
	RenderSystem::DestroyViewTarget(a);
	EXPECT_TRUE(RenderSystem::GetViewTargetTexture(b).IsValid());
	RenderSystem::DestroyViewTarget(b);
}

TEST_F(OffscreenViewFixture, SceneCameraWindowToggleRoundTrips) {
	EXPECT_TRUE(RenderSystem::GetSceneCamerasRenderToWindows());
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	EXPECT_FALSE(RenderSystem::GetSceneCamerasRenderToWindows());
	RenderSystem::SetSceneCamerasRenderToWindows(true);
	EXPECT_TRUE(RenderSystem::GetSceneCamerasRenderToWindows());
}

TEST_F(OffscreenViewFixture, ClearColorLandsInTheSampleableResult) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -5.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -5.0f);
	desc.clearColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	ASSERT_EQ(size, (Vec2I{64, 64}));
	RGBA corner = TexelAt(pixels, size, 1, 1);
	EXPECT_LE(corner.r, 4);
	EXPECT_LE(corner.g, 4);
	EXPECT_LE(corner.b, 4);
	EXPECT_EQ(corner.a, 255);
	EXPECT_EQ(CountDistinct(pixels), 1u);

	RenderSystem::DestroyViewTarget(target);
}

TEST_F(OffscreenViewFixture, DebugLinesChangePixelsAndLandUpright) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	const DebugLine lines[] = {
		{Vec3(-2.0f, 1.0f, 0.0f), Vec3(2.0f, 1.0f, 0.0f), Color32(255, 0, 0, 255)},
	};

	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -5.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -5.0f);
	desc.clearColor = Color::Black();
	desc.lines = {lines, 1};
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	ASSERT_EQ(size, (Vec2I{64, 64}));
	EXPECT_GT(CountDistinct(pixels), 1u);

	i32 topRedRows = 0;
	i32 bottomRedRows = 0;
	for (i32 y = 0; y < size.y; ++y) {
		bool rowHasRed = false;
		for (i32 x = 0; x < size.x; ++x) {
			RGBA texel = TexelAt(pixels, size, x, y);
			if (texel.r > 100 && texel.g < 80 && texel.b < 80) {
				rowHasRed = true;
				break;
			}
		}
		if (rowHasRed) {
			(y < size.y / 2 ? topRedRows : bottomRedRows) += 1;
		}
	}
	EXPECT_GT(topRedRows, 0);
	EXPECT_EQ(bottomRedRows, 0);

	RenderSystem::DestroyViewTarget(target);
}

TEST_F(OffscreenViewFixture, OverlayLinesIgnoreDepth) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ViewTargetId target = RenderSystem::CreateViewTarget({48, 48});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	const DebugLine overlay[] = {
		{Vec3(-2.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f), Color32(0, 255, 0, 255)},
	};
	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -5.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -5.0f);
	desc.clearColor = Color::Black();
	desc.overlayLines = {overlay, 1};
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	bool sawGreen = false;
	for (i32 x = 0; x < size.x && !sawGreen; ++x) {
		for (i32 y = 0; y < size.y; ++y) {
			RGBA texel = TexelAt(pixels, size, x, y);
			if (texel.g > 100 && texel.r < 80) {
				sawGreen = true;
				break;
			}
		}
	}
	EXPECT_TRUE(sawGreen);

	RenderSystem::DestroyViewTarget(target);
}

TEST_F(OffscreenViewFixture, ViewsRenderWithoutAnyWindow) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	ViewTargetId target = RenderSystem::CreateViewTarget({32, 32});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -5.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.clearColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RGBA texel = TexelAt(pixels, size, 16, 16);
	EXPECT_GT(texel.r, 200);
	EXPECT_GT(texel.g, 200);
	EXPECT_GT(texel.b, 200);

	RenderSystem::DestroyViewTarget(target);
	RenderSystem::SetSceneCamerasRenderToWindows(true);
}

TEST_F(OffscreenViewFixture, StaleSubmissionsDoNotAccumulate) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ViewTargetId target = RenderSystem::CreateViewTarget({32, 32});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -5.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.clearColor = Color::Black();
	desc.clearColor = Color(1.0f, 0.0f, 0.0f, 1.0f);
	RenderSystem::SubmitView(target, desc);
	desc.clearColor = Color(0.0f, 0.0f, 1.0f, 1.0f);
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RGBA texel = TexelAt(pixels, size, 16, 16);
	EXPECT_GT(texel.b, 200);
	EXPECT_LT(texel.r, 60);

	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	EXPECT_GT(TexelAt(pixels, size, 16, 16).b, 200);

	RenderSystem::DestroyViewTarget(target);
}

namespace {

	class ValidationErrorSink : public ILogSink {
	public:
		static constexpr std::string_view Sentinel = "offscreen-view-test-sentinel";

		void ReceiveLog(const LogEvent& event) override {
			if (event.level < LogLevel::Error) {
				return;
			}
			bool watched = event.category == "Vulkan" ||
						   (event.category == "Graphics" && event.message.find("not ShaderResource") != str::npos);
			if (!watched) {
				return;
			}
			std::lock_guard lock(_mutex);
			if (event.message.find(Sentinel) != str::npos) {
				_sentinelSeen = true;
				return;
			}
			++_errors;
			if (_first.empty()) {
				_first = event.message;
			}
		}
		void Flush() override {}

		bool SentinelSeen() const {
			std::lock_guard lock(_mutex);
			return _sentinelSeen;
		}
		u32 Errors() const {
			std::lock_guard lock(_mutex);
			return _errors;
		}
		str FirstMessage() const {
			std::lock_guard lock(_mutex);
			return _first;
		}
	private:
		mutable std::mutex _mutex;
		u32 _errors = 0;
		bool _sentinelSeen = false;
		str _first;
	};

	class ScopedValidationWatch {
	public:
		ScopedValidationWatch() : _sink(std::make_shared<ValidationErrorSink>()) {
			_ownsJobSystem = JobSystem::GetWorkerCount() == 0;
			if (_ownsJobSystem) {
				JobSystem::Initialize();
			}
			LogManager::Initialize({false, false});
			LogManager::SetGlobalLevel(LogLevel::Trace);
			LogManager::AddSink(_sink);
		}
		~ScopedValidationWatch() {
			LogManager::RemoveSink(_sink);
			LogManager::Terminate();
			if (_ownsJobSystem) {
				JobSystem::Terminate();
			}
		}

		bool Settle(f64 timeoutSeconds = 5.0) {
			SLT_LOG_ERROR("Vulkan", "{}", ValidationErrorSink::Sentinel);
			auto deadline = std::chrono::steady_clock::now() +
							std::chrono::duration<f64>(timeoutSeconds);
			while (std::chrono::steady_clock::now() < deadline) {
				if (_sink->SentinelSeen()) {
					return true;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			return false;
		}

		u32 Errors() const { return _sink->Errors(); }
		str FirstMessage() const { return _sink->FirstMessage(); }
	private:
		std::shared_ptr<ValidationErrorSink> _sink;
		bool _ownsJobSystem = false;
	};

	void SubmitAndRender(ViewTargetId target, Color clearColor) {
		ViewDesc desc;
		desc.view = LookForwardView(Vec3(0.0f, 0.0f, -5.0f));
		desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
		desc.cameraPosition = Vec3(0.0f, 0.0f, -5.0f);
		desc.clearColor = clearColor;
		RenderSystem::SubmitView(target, desc);
		RenderSystem::RenderAll();
	}

} // anonymous namespace

TEST_F(OffscreenViewFixture, ResizeKeepsInFlightTextures) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ScopedValidationWatch watch;

	ViewTargetId target = RenderSystem::CreateViewTarget({96, 96});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	constexpr i32 STEPS = 6;
	for (i32 i = 0; i < STEPS; ++i) {
		SubmitAndRender(target, Color(0.0f, 0.0f, 1.0f, 1.0f));
		RenderSystem::ResizeViewTarget(target, {96 + (i + 1) * 8, 96 + (i + 1) * 8});
	}
	SubmitAndRender(target, Color(0.0f, 0.0f, 1.0f, 1.0f));

	ASSERT_TRUE(watch.Settle());
	EXPECT_EQ(watch.Errors(), 0u) << "validation complained: " << watch.FirstMessage();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	EXPECT_EQ(size.x, 96 + STEPS * 8);
	EXPECT_GT(TexelAt(pixels, size, size.x / 2, size.y / 2).b, 200);

	RenderSystem::DestroyViewTarget(target);
}

TEST_F(OffscreenViewFixture, DestroyKeepsInFlightTextures) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ScopedValidationWatch watch;

	for (i32 i = 0; i < 4; ++i) {
		ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
		ASSERT_NE(target, INVALID_VIEW_TARGET);
		SubmitAndRender(target, Color(1.0f, 0.0f, 0.0f, 1.0f));
		RenderSystem::DestroyViewTarget(target);
	}
	RenderSystem::RenderAll();
	RenderSystem::RenderAll();

	ASSERT_TRUE(watch.Settle());
	EXPECT_EQ(watch.Errors(), 0u) << "validation complained: " << watch.FirstMessage();
}

TEST_F(OffscreenViewFixture, ColdTargetBecomesSampleableWithNothingSubmitted) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	ASSERT_NE(device, nullptr);

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	RHI::TextureHandle color = RenderSystem::GetViewTargetTexture(target);
	ASSERT_TRUE(color.IsValid());
	EXPECT_EQ(device->GetTextureState(color), RHI::ResourceState::Undefined);

	RenderSystem::RenderAll();
	EXPECT_EQ(device->GetTextureState(color), RHI::ResourceState::ShaderResource);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	ASSERT_EQ(size, (Vec2I{64, 64}));
	EXPECT_EQ(CountDistinct(pixels), 1u);
	EXPECT_EQ(TexelAt(pixels, size, 7, 11), (RGBA{0, 0, 0, 255}));

	RenderSystem::ResizeViewTarget(target, {80, 48});
	RHI::TextureHandle resized = RenderSystem::GetViewTargetTexture(target);
	ASSERT_TRUE(resized.IsValid());
	EXPECT_EQ(device->GetTextureState(resized), RHI::ResourceState::Undefined);
	RenderSystem::RenderAll();
	EXPECT_EQ(device->GetTextureState(resized), RHI::ResourceState::ShaderResource);
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	ASSERT_EQ(size, (Vec2I{80, 48}));
	EXPECT_EQ(CountDistinct(pixels), 1u);

	SubmitAndRender(target, Color(0.0f, 0.0f, 1.0f, 1.0f));
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	EXPECT_GT(TexelAt(pixels, size, size.x / 2, size.y / 2).b, 200);

	RenderSystem::DestroyViewTarget(target);
}

namespace {

	class ViewTargetSamplingFixture : public ::testing::Test {
	protected:
		bool _mounted = false;

		void SetUp() override {
			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
		}

		void TearDown() override {
			UISystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
		}
	};

} // anonymous namespace

TEST_F(ViewTargetSamplingFixture, PaintingANeverSubmittedTargetIsClean) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ScopedValidationWatch watch;

	WindowDesc windowDesc;
	windowDesc.title = "ViewTargetSamplingTest";
	windowDesc.clientSize = {320, 240};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());
	UI::Context* ctx = UISystem::CreateContext(window);
	ASSERT_NE(ctx, nullptr);

	RenderSystem::SetSceneCamerasRenderToWindows(false);
	ViewTargetId target = RenderSystem::CreateViewTarget({160, 120});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	UI::ElementHandle host = ctx->CreateElement();
	ctx->AppendChild(ctx->GetRoot(), host);
	UI::Style style;
	style.width = UI::Length::Percent(1.0f);
	style.height = UI::Length::Percent(1.0f);
	ctx->SetStyle(host, style);
	ctx->Events(host).customDraw = [target](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
		RHI::TextureHandle texture = RenderSystem::GetViewTargetTexture(target);
		if (!texture.IsValid()) {
			return;
		}
		paint.EmitTexturedQuad(c.GetLayout(element).content, texture, {0.0f, 0.0f, 1.0f, 1.0f},
							   Color::White(), UI::SamplerKind::LinearClamp);
	};

	for (int frame = 0; frame < 2; ++frame) {
		WindowSystem::PumpEvents();
		UISystem::Update();
		RenderSystem::RenderAll();
	}
	RenderSystem::ResizeViewTarget(target, {192, 96});
	for (int frame = 0; frame < 2; ++frame) {
		WindowSystem::PumpEvents();
		UISystem::Update();
		RenderSystem::RenderAll();
	}

	UISystem::Stats stats = UISystem::GetStats();
	ASSERT_GT(stats.quads, 0u);
	if (stats.drawCalls == 0) {
		GTEST_SKIP() << "engine://Shaders/UI.hlsl unavailable (dxcompiler not staged)";
	}

	ASSERT_TRUE(watch.Settle());
	EXPECT_EQ(watch.Errors(), 0u) << watch.FirstMessage();

	RenderSystem::DestroyViewTarget(target);
	RenderSystem::SetSceneCamerasRenderToWindows(true);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(OffscreenViewFixture, PresentPassEncodesLinearLightToSRGB) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	ViewTargetId target = RenderSystem::CreateViewTarget({32, 32});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -5.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.clearColor = Color(0.2140f, 0.2140f, 0.2140f, 1.0f);
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RGBA texel = TexelAt(pixels, size, 16, 16);
	EXPECT_NEAR(texel.r, 128, 1);
	EXPECT_NEAR(texel.g, 128, 1);
	EXPECT_NEAR(texel.b, 128, 1);

	RenderSystem::DestroyViewTarget(target);
	RenderSystem::SetSceneCamerasRenderToWindows(true);
}

namespace {

	Handle<SceneObject> AddCube(Handle<Scene> scene, f32 x) {
		Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
		Asset* materialAsset = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
		if (!meshAsset || !materialAsset) {
			return {};
		}
		Handle<SceneObject> object = SceneSystem::CreateObject("Cube", scene);
		Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
		renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(meshAsset)));
		renderer->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(materialAsset)));
		object->SetLocalPosition(Vec3(x, 0.0f, 0.0f));
		return object;
	}

	void CountHalves(const Vector<u8>& pixels, Vec2I size, u32& outLeft, u32& outRight) {
		outLeft = 0;
		outRight = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				RGBA texel = TexelAt(pixels, size, x, y);
				if (texel.r > 24 || texel.g > 24 || texel.b > 24) {
					(x < size.x / 2 ? outLeft : outRight) += 1;
				}
			}
		}
	}

} // anonymous namespace

TEST_F(OffscreenViewFixture, ViewDrawsOnlyScenesItNames) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> level = SceneSystem::CreateScene("Level");
	Handle<Scene> stage = SceneSystem::CreateScene("Stage");
	if (!AddCube(level, -1.6f).IsValid() || !AddCube(stage, 1.6f).IsValid()) {
		GTEST_SKIP();
	}
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -5.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -5.0f);
	desc.clearColor = Color::Black();

	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> both{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, both, size));
	u32 bothLeft = 0;
	u32 bothRight = 0;
	CountHalves(both, size, bothLeft, bothRight);
	ASSERT_GT(bothLeft, 40u);
	ASSERT_GT(bothRight, 40u);

	const Handle<Scene> only[] = {stage};
	desc.scenes = {only, 1};
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> staged{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, staged, size));
	u32 stagedLeft = 0;
	u32 stagedRight = 0;
	CountHalves(staged, size, stagedLeft, stagedRight);
	EXPECT_EQ(stagedLeft, 0u) << stagedLeft << " pixels";
	EXPECT_NEAR(static_cast<f32>(stagedRight), static_cast<f32>(bothRight), 2.0f);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(stage);
	SceneSystem::UnloadScene(level);
}

TEST_F(OffscreenViewFixture, DeadFilterSceneDrawsAll) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> level = SceneSystem::CreateScene("Level");
	if (!AddCube(level, 0.0f).IsValid()) {
		GTEST_SKIP();
	}
	Handle<Scene> doomed = SceneSystem::CreateScene("Doomed");
	SceneSystem::EndFrame();
	SceneSystem::UnloadScene(doomed);
	SceneSystem::EndFrame();
	ASSERT_FALSE(doomed.IsValid());

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	const Handle<Scene> only[] = {doomed};
	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -5.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -5.0f);
	desc.clearColor = Color::Black();
	desc.scenes = {only, 1};
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	u32 left = 0;
	u32 right = 0;
	CountHalves(pixels, size, left, right);
	EXPECT_GT(left + right, 40u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(level);
}

namespace {

	void AddSun(Handle<Scene> scene, f32 intensity = 3.0f) {
		Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", scene);
		sun->SetLocalRotation(Quat::FromEuler(Vec3(20.0f, 0.0f, 0.0f)));
		sun->AddComponent<Light>()->SetIntensity(intensity);
	}

	ViewDesc CubeView() {
		ViewDesc desc;
		desc.view = LookForwardView(Vec3(0.0f, 0.0f, -5.0f));
		desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
		desc.cameraPosition = Vec3(0.0f, 0.0f, -5.0f);
		desc.clearColor = Color::Black();
		return desc;
	}

	bool RenderAndRead(ViewTargetId target, const ViewDesc& desc, Vector<u8>& outPixels, Vec2I& outSize) {
		RenderSystem::SubmitView(target, desc);
		RenderSystem::RenderAll();
		return RenderSystem::ReadbackViewTarget(target, outPixels, outSize);
	}

} // anonymous namespace

TEST_F(OffscreenViewFixture, IsolatedSceneLeavesWorldView) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> level = SceneSystem::CreateScene("Level");
	if (!AddCube(level, -1.6f).IsValid()) {
		GTEST_SKIP();
	}
	AddSun(level);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc = CubeView();

	Vector<u8> before{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderAndRead(target, desc, before, size));
	u32 left = 0;
	u32 right = 0;
	CountHalves(before, size, left, right);
	ASSERT_GT(left, 40u);
	ASSERT_EQ(right, 0u);

	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	ASSERT_TRUE(preview.IsValid());
	ASSERT_TRUE(AddCube(preview, 1.6f).IsValid());
	AddSun(preview, 12.0f);
	SceneSystem::EndFrame();
	ASSERT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);

	Vector<u8> after{GetMemoryResource(MemTag::Graphics)};
	Vec2I afterSize{};
	ASSERT_TRUE(RenderAndRead(target, desc, after, afterSize));
	EXPECT_EQ(afterSize, size);
	EXPECT_EQ(after, before);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(preview);
	SceneSystem::UnloadScene(level);
}

TEST_F(OffscreenViewFixture, IsolatedViewDrawsOnlyItsScene) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> level = SceneSystem::CreateScene("Level");
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	if (!AddCube(level, -1.6f).IsValid() || !AddCube(preview, 1.6f).IsValid()) {
		GTEST_SKIP();
	}
	AddSun(level);
	AddSun(preview);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const Handle<Scene> only[] = {preview};
	ViewDesc desc = CubeView();
	desc.scenes = {only, 1};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderAndRead(target, desc, pixels, size));
	u32 left = 0;
	u32 right = 0;
	CountHalves(pixels, size, left, right);
	EXPECT_GT(right, 40u);
	EXPECT_EQ(left, 0u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(preview);
	SceneSystem::UnloadScene(level);
}

TEST_F(OffscreenViewFixture, WorldLightSkipsIsolatedView) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> level = SceneSystem::CreateScene("Level");
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	if (!AddCube(preview, 0.0f).IsValid()) {
		GTEST_SKIP();
	}
	AddSun(level, 20.0f);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const Handle<Scene> only[] = {preview};
	ViewDesc desc = CubeView();
	desc.scenes = {only, 1};

	Vector<u8> unlit{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderAndRead(target, desc, unlit, size));
	RGBA ambientOnly = BrightestTexel(unlit, size);

	AddSun(preview, 20.0f);
	SceneSystem::EndFrame();
	Vector<u8> lit{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(RenderAndRead(target, desc, lit, size));
	RGBA ownRig = BrightestTexel(lit, size);

	EXPECT_GT(ownRig.g, ambientOnly.g + 20u);
	EXPECT_LT(ambientOnly.g, 96u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(preview);
	SceneSystem::UnloadScene(level);
}

TEST_F(OffscreenViewFixture, IsolatedScenesStaySeparate) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> first = SceneSystem::CreateIsolatedScene("First");
	Handle<Scene> second = SceneSystem::CreateIsolatedScene("Second");
	if (!AddCube(first, -1.6f).IsValid() || !AddCube(second, 1.6f).IsValid()) {
		GTEST_SKIP();
	}
	AddSun(second, 20.0f);
	SceneSystem::EndFrame();
	ASSERT_EQ(SceneSystem::GetIsolatedSceneCount(), 2u);

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const Handle<Scene> onlyFirst[] = {first};
	ViewDesc desc = CubeView();
	desc.scenes = {onlyFirst, 1};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderAndRead(target, desc, pixels, size));
	u32 left = 0;
	u32 right = 0;
	CountHalves(pixels, size, left, right);
	EXPECT_GT(left, 40u);
	EXPECT_EQ(right, 0u);
	EXPECT_LT(BrightestTexel(pixels, size).g, 96u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(second);
	SceneSystem::UnloadScene(first);
}

TEST_F(OffscreenViewFixture, MixedSceneViewRefused) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> level = SceneSystem::CreateScene("Level");
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	if (!AddCube(level, -1.6f).IsValid() || !AddCube(preview, 1.6f).IsValid()) {
		GTEST_SKIP();
	}
	AddSun(level);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	const Handle<Scene> worldOnly[] = {level};
	ViewDesc desc = CubeView();
	desc.scenes = {worldOnly, 1};
	Vector<u8> accepted{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderAndRead(target, desc, accepted, size));
	ASSERT_GT(RenderSystem::GetStats().drawCalls, 0u);

	const Handle<Scene> mixed[] = {level, preview};
	desc.scenes = {mixed, 2};
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	EXPECT_EQ(RenderSystem::GetStats().drawCalls, 0u);

	Vector<u8> afterRefusal{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, afterRefusal, size));
	EXPECT_EQ(afterRefusal, accepted);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(preview);
	SceneSystem::UnloadScene(level);
}

namespace {

	bool AddTurnedCube(Handle<Scene> scene) {
		Handle<SceneObject> cube = AddCube(scene, 0.0f);
		if (!cube.IsValid()) {
			return false;
		}
		cube->SetLocalRotation(Quat::FromEuler(Vec3(28.0f, 35.0f, 0.0f)));
		return true;
	}

	ViewDesc CloseCubeView() {
		ViewDesc desc;
		desc.view = LookForwardView(Vec3(0.0f, 0.0f, -2.0f));
		desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
		desc.cameraPosition = Vec3(0.0f, 0.0f, -2.0f);
		desc.clearColor = Color::Black();
		return desc;
	}

	u32 CountLit(const Vector<u8>& pixels, Vec2I size) {
		u32 lit = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				RGBA texel = TexelAt(pixels, size, x, y);
				if (texel.r > 24 || texel.g > 24 || texel.b > 24) {
					++lit;
				}
			}
		}
		return lit;
	}

	u32 CountWire(const Vector<u8>& pixels, Vec2I size) {
		u32 wire = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				RGBA texel = TexelAt(pixels, size, x, y);
				if (texel.r > 180 && texel.g < 80 && texel.b < 80) {
					++wire;
				}
			}
		}
		return wire;
	}

	u32 CountWireOverSurface(const Vector<u8>& wirePixels, const Vector<u8>& shadedPixels, Vec2I size) {
		u32 over = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				RGBA wire = TexelAt(wirePixels, size, x, y);
				RGBA shaded = TexelAt(shadedPixels, size, x, y);
				bool isWire = wire.r > 180 && wire.g < 80 && wire.b < 80;
				bool onSurface = shaded.r > 24 || shaded.g > 24 || shaded.b > 24;
				if (isWire && onSurface) {
					++over;
				}
			}
		}
		return over;
	}

	bool RenderFill(ViewTargetId target, ViewFill fill, Vector<u8>& outPixels, Vec2I& outSize) {
		ViewDesc desc = CloseCubeView();
		desc.fill = fill;
		desc.wireColor = Color32{255, 0, 0, 255};
		RenderSystem::SubmitView(target, desc);
		RenderSystem::RenderAll();
		return RenderSystem::ReadbackViewTarget(target, outPixels, outSize);
	}

} // anonymous namespace

TEST_F(OffscreenViewFixture, WireframeDrawsEdgesInsteadOfTheSurface) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> level = SceneSystem::CreateScene("Level");
	if (!AddTurnedCube(level)) {
		GTEST_SKIP();
	}
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	Vector<u8> shaded{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderFill(target, ViewFill::Solid, shaded, size));
	u32 shadedLit = CountLit(shaded, size);
	ASSERT_GT(shadedLit, 1000u);

	Vector<u8> wire{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(RenderFill(target, ViewFill::Wireframe, wire, size));
	u32 wireLit = CountLit(wire, size);
	EXPECT_GT(wireLit, 0u);
	EXPECT_LT(wireLit, shadedLit / 2) << "wireframe covered " << wireLit << ' ' << shadedLit;
	EXPECT_GT(CountWire(wire, size), 0u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(level);
}

TEST_F(OffscreenViewFixture, ShadedWireframeOverSurface) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> level = SceneSystem::CreateScene("Level");
	if (!AddTurnedCube(level)) {
		GTEST_SKIP();
	}
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	Vector<u8> shaded{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderFill(target, ViewFill::Solid, shaded, size));
	u32 shadedLit = CountLit(shaded, size);
	ASSERT_GT(shadedLit, 1000u);
	ASSERT_EQ(CountWire(shaded, size), 0u);

	Vector<u8> both{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(RenderFill(target, ViewFill::SolidAndWireframe, both, size));
	EXPECT_GE(CountLit(both, size), shadedLit);
	EXPECT_GT(CountWireOverSurface(both, shaded, size), 450u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(level);
}

TEST_F(OffscreenViewFixture, SolidIsDefaultNoWire) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	EXPECT_EQ(ViewDesc{}.fill, ViewFill::Solid);

	Handle<Scene> level = SceneSystem::CreateScene("Level");
	if (!AddTurnedCube(level)) {
		GTEST_SKIP();
	}
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	ViewDesc desc = CloseCubeView();
	desc.wireColor = Color32{255, 0, 0, 255};
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	EXPECT_GT(CountLit(pixels, size), 1000u);
	EXPECT_EQ(CountWire(pixels, size), 0u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(level);
}

TEST_F(OffscreenViewFixture, RenderersDrawAtOwnTransforms) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> level = SceneSystem::CreateScene("Level");
	if (!AddCube(level, -1.6f).IsValid() || !AddCube(level, 1.6f).IsValid()) {
		GTEST_SKIP();
	}
	AddSun(level);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderAndRead(target, CubeView(), pixels, size));
	u32 left = 0;
	u32 right = 0;
	CountHalves(pixels, size, left, right);
	EXPECT_GT(left, 40u);
	EXPECT_GT(right, 40u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(level);
}

TEST_F(OffscreenViewFixture, WirePassIndexesPerDraw) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Scene> level = SceneSystem::CreateScene("Level");
	if (!AddCube(level, -1.6f).IsValid() || !AddCube(level, 1.6f).IsValid()) {
		GTEST_SKIP();
	}
	AddSun(level);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	ViewDesc desc = CubeView();
	desc.fill = ViewFill::Wireframe;
	desc.wireColor = Color32{255, 0, 0, 255};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderAndRead(target, desc, pixels, size));

	ASSERT_GT(CountWire(pixels, size), 0u);
	u32 left = 0;
	u32 right = 0;
	CountHalves(pixels, size, left, right);
	EXPECT_GT(left, 0u);
	EXPECT_GT(right, 0u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(level);
}

namespace {

	class CreatedShaderFixture : public OffscreenViewFixture {
	protected:
		std::filesystem::path _project;
		bool _mountedProject = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_project = std::filesystem::temp_directory_path() /
					   ("shalltear_createdshader_" + std::to_string(stamp) + "_" +
						std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_project);
			FileSystem::Mount("project", Path(_project.generic_string()));
			_mountedProject = true;
			OffscreenViewFixture::SetUp();
		}

		void TearDown() override {
			OffscreenViewFixture::TearDown();
			if (_mountedProject) {
				FileSystem::Unmount("project");
			}
			std::error_code ec;
			std::filesystem::remove_all(_project, ec);
		}
	};

} // anonymous namespace

TEST_F(CreatedShaderFixture, CreatedShaderRendersWithMaterialsColor) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Guid shaderGuid = AssetDatabase::CreateAsset(Path("project://New Shader.hlsl"), StringHash("ShaderAsset"));
	ASSERT_TRUE(shaderGuid.IsValid());
	Guid materialGuid = AssetDatabase::CreateAsset(Path("project://New Material.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(materialGuid.IsValid());

	Asset* shaderAsset = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(shaderAsset, nullptr);
	ShaderAsset* shader = static_cast<ShaderAsset*>(shaderAsset);
	if (shader->variants.empty()) {
		AssetDatabase::UnloadAsset(shaderAsset);
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}

	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(materialGuid);
	ASSERT_NE(materialAsset, nullptr);
	MaterialAsset* material = static_cast<MaterialAsset*>(materialAsset);
	material->shaderGuid = shaderGuid;
	Asset* materialsShaderRef = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(materialsShaderRef, nullptr);
	material->shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(materialsShaderRef));
	Vector<str> textureSlots{GetMemoryResource(MemTag::Asset)};
	shader->GetMaterialTextureSlots(textureSlots);
	material->SyncToLayout(shader->GetMaterialParamLayout(), {textureSlots.data(), textureSlots.size()});
	material->SetColor("baseColor", Color(0.0f, 1.0f, 0.0f, 1.0f));

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<Scene> scene = SceneSystem::CreateScene("Created");
	Handle<SceneObject> object = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(meshAsset)));
	renderer->SetMaterial(0, Handle<MaterialAsset>(material));

	Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", scene);
	sun->SetLocalRotation(Quat::FromEuler(Vec3(25.0f, 0.0f, 0.0f)));
	sun->AddComponent<Light>();
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -3.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -3.0f);
	desc.clearColor = Color::Black();
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RGBA brightest = BrightestTexel(pixels, size);

	EXPECT_GT(brightest.g, 40u);
	EXPECT_GT(brightest.g, brightest.r * 2u);
	EXPECT_GT(brightest.g, brightest.b * 2u);

	RenderSystem::DestroyViewTarget(target);
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(shaderAsset);
}

TEST_F(CreatedShaderFixture, MixedMaterialBindsAndRenders) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	constexpr const char* THREE_TEXTURE_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull back\n"
		"#include \"ShalltearCommon.hlsli\"\n"
		"[[vk::push_constant]] ShalltearDrawHeader gDraw;\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {\n"
		"\tfloat3 emissive;\n"
		"\tint mode;\n"
		"};\n"
		"[[vk::binding(1, 1)]] Texture2D texA;\n"
		"[[vk::binding(2, 1)]] SamplerState sampA;\n"
		"[[vk::binding(3, 1)]] Texture2D texB;\n"
		"[[vk::binding(4, 1)]] SamplerState sampB;\n"
		"[[vk::binding(5, 1)]] Texture2D texC;\n"
		"[[vk::binding(6, 1)]] SamplerState sampC;\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tShalltearPerDraw draw = ShalltearLoadPerDraw(gDraw.drawAddress);\n"
		"\tShalltearVertex vertex = ShalltearLoadVertex(draw.vertexAddress, vertexId);\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = mul(sltViewProjection, ShalltearTransformPosition(draw, vertex.position));\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target {\n"
		"\tfloat2 uv = float2(0, 0);\n"
		"\tfloat3 sampled = texA.Sample(sampA, uv).rgb * texB.Sample(sampB, uv).rgb * texC.Sample(sampC, uv).rgb;\n"
		"\treturn float4(sampled * emissive * float(mode), 1.0);\n"
		"}\n";

	std::filesystem::path shaderFile = _project / "ThreeTex.hlsl";
	FileSystem::WriteTextFile(Path(shaderFile.generic_string()), str(THREE_TEXTURE_SHADER));
	Guid shaderGuid = AssetDatabase::EnsureAssetIdentity(Path("project://ThreeTex.hlsl"));
	ASSERT_TRUE(shaderGuid.IsValid());
	Asset* shaderAsset = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(shaderAsset, nullptr);
	ShaderAsset* shader = static_cast<ShaderAsset*>(shaderAsset);
	if (shader->variants.empty()) {
		AssetDatabase::UnloadAsset(shaderAsset);
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}

	Vector<str> textureSlots{GetMemoryResource(MemTag::Asset)};
	shader->GetMaterialTextureSlots(textureSlots);
	ASSERT_EQ(textureSlots.size(), 3u);

	Guid materialGuid = AssetDatabase::CreateAsset(Path("project://ThreeTex.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(materialGuid.IsValid());
	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(materialGuid);
	ASSERT_NE(materialAsset, nullptr);
	MaterialAsset* material = static_cast<MaterialAsset*>(materialAsset);
	material->shaderGuid = shaderGuid;
	Asset* materialsShaderRef = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(materialsShaderRef, nullptr);
	material->shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(materialsShaderRef));
	material->SyncToLayout(shader->GetMaterialParamLayout(), {textureSlots.data(), textureSlots.size()});
	material->SetVec3("emissive", Vec3(0.1f, 0.4f, 0.05f));
	material->SetInt("mode", 2);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<Scene> scene = SceneSystem::CreateScene("ThreeTex");
	Handle<SceneObject> object = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(meshAsset)));
	renderer->SetMaterial(0, Handle<MaterialAsset>(material));
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -3.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -3.0f);
	desc.clearColor = Color::Black();
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RGBA brightest = BrightestTexel(pixels, size);

	EXPECT_GT(brightest.g, 40u);
	EXPECT_GT(brightest.g, brightest.r);
	EXPECT_GT(brightest.r, brightest.b);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(shaderAsset);
}

TEST_F(CreatedShaderFixture, BlockEditReloadsLayout) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	auto shaderSource = [](const char* leadingMember) {
		return str("#pragma shalltear_blend opaque\n"
				   "#pragma shalltear_depth test_write\n"
				   "#pragma shalltear_cull back\n"
				   "#include \"ShalltearCommon.hlsli\"\n"
				   "[[vk::push_constant]] ShalltearDrawHeader gDraw;\n"
				   "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {\n") +
			   leadingMember +
			   "\tfloat4 baseColor;\n"
			   "};\n"
			   "[[vk::binding(1, 1)]] Texture2D baseColorTexture;\n"
			   "[[vk::binding(2, 1)]] SamplerState baseColorSampler;\n"
			   "struct VSOutput { float4 positionCS : SV_Position; };\n"
			   "VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
			   "\tShalltearPerDraw draw = ShalltearLoadPerDraw(gDraw.drawAddress);\n"
			   "\tShalltearVertex vertex = ShalltearLoadVertex(draw.vertexAddress, vertexId);\n"
			   "\tVSOutput output;\n"
			   "\toutput.positionCS = mul(sltViewProjection, ShalltearTransformPosition(draw, vertex.position));\n"
			   "\treturn output;\n"
			   "}\n"
			   "float4 PSMain(VSOutput input) : SV_Target {\n"
			   "\treturn baseColorTexture.Sample(baseColorSampler, float2(0, 0)) * baseColor;\n"
			   "}\n";
	};

	std::filesystem::path shaderFile = _project / "Reloading.hlsl";
	FileSystem::WriteTextFile(Path(shaderFile.generic_string()), shaderSource(""));
	Guid shaderGuid = AssetDatabase::EnsureAssetIdentity(Path("project://Reloading.hlsl"));
	ASSERT_TRUE(shaderGuid.IsValid());
	Asset* shaderAsset = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(shaderAsset, nullptr);
	ShaderAsset* shader = static_cast<ShaderAsset*>(shaderAsset);
	if (shader->variants.empty()) {
		AssetDatabase::UnloadAsset(shaderAsset);
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	const UniformBlockLayout* before = shader->GetMaterialParamLayout();
	ASSERT_NE(before, nullptr);
	ASSERT_EQ(before->members.size(), 1u);
	ASSERT_EQ(before->members[0].offset, 0u);

	Guid materialGuid = AssetDatabase::CreateAsset(Path("project://Reloading.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(materialGuid.IsValid());
	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(materialGuid);
	ASSERT_NE(materialAsset, nullptr);
	MaterialAsset* material = static_cast<MaterialAsset*>(materialAsset);
	material->shaderGuid = shaderGuid;
	Asset* materialsShaderRef = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(materialsShaderRef, nullptr);
	material->shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(materialsShaderRef));
	material->SetColor("baseColor", Color(0.0f, 1.0f, 0.0f, 1.0f));

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<Scene> scene = SceneSystem::CreateScene("Reloading");
	Handle<SceneObject> object = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(meshAsset)));
	renderer->SetMaterial(0, Handle<MaterialAsset>(material));
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -3.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -3.0f);
	desc.clearColor = Color::Black();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	ASSERT_GT(BrightestTexel(pixels, size).g, 40u);

	FileSystem::WriteTextFile(Path(shaderFile.generic_string()), shaderSource("\tfloat4 leading;\n"));
	RenderSystem::NotifyShaderFileChanged(str(shaderFile.generic_string()));
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	const UniformBlockLayout* after = shader->GetMaterialParamLayout();
	ASSERT_NE(after, nullptr);
	ASSERT_EQ(after->members.size(), 2u);
	EXPECT_EQ(after->members[1].name, "baseColor");
	EXPECT_EQ(after->members[1].offset, 16u);

	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RGBA reloaded = BrightestTexel(pixels, size);
	EXPECT_GT(reloaded.g, 40u);
	EXPECT_GT(reloaded.g, reloaded.r * 2u);
	EXPECT_GT(reloaded.g, reloaded.b * 2u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(shaderAsset);
}

namespace {

	str TwoKeywordShader() {
		return str("#pragma shalltear_blend opaque\n"
				   "#pragma shalltear_depth test_write\n"
				   "#pragma shalltear_cull back\n"
				   "#pragma shalltear_keyword TINT_GREEN\n"
				   "#pragma shalltear_keyword TINT_BLUE\n"
				   "#include \"ShalltearCommon.hlsli\"\n"
				   "[[vk::push_constant]] ShalltearDrawHeader gDraw;\n"
				   "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {\n"
				   "\tfloat4 baseColor;\n"
				   "};\n"
				   "struct VSOutput { float4 positionCS : SV_Position; };\n"
				   "VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
				   "\tShalltearPerDraw draw = ShalltearLoadPerDraw(gDraw.drawAddress);\n"
				   "\tShalltearVertex vertex = ShalltearLoadVertex(draw.vertexAddress, vertexId);\n"
				   "\tVSOutput output;\n"
				   "\toutput.positionCS = mul(sltViewProjection, ShalltearTransformPosition(draw, vertex.position));\n"
				   "\treturn output;\n"
				   "}\n"
				   "float4 PSMain(VSOutput input) : SV_Target {\n"
				   "\tfloat3 tint = float3(1, 0, 0);\n"
				   "#ifdef TINT_GREEN\n"
				   "\ttint = float3(0, 1, 0);\n"
				   "#endif\n"
				   "#ifdef TINT_BLUE\n"
				   "\ttint = float3(0, 0, 1);\n"
				   "#endif\n"
				   "\treturn float4(tint * baseColor.rgb, 1.0);\n"
				   "}\n");
	}

} // namespace

TEST_F(CreatedShaderFixture, CompilesOnlyUsedCombination) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	std::filesystem::path shaderFile = _project / "Keywords.hlsl";
	FileSystem::WriteTextFile(Path(shaderFile.generic_string()), TwoKeywordShader());
	Guid shaderGuid = AssetDatabase::EnsureAssetIdentity(Path("project://Keywords.hlsl"));
	ASSERT_TRUE(shaderGuid.IsValid());
	Asset* shaderAsset = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(shaderAsset, nullptr);
	ShaderAsset* shader = static_cast<ShaderAsset*>(shaderAsset);
	if (shader->variants.empty()) {
		AssetDatabase::UnloadAsset(shaderAsset);
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	ASSERT_EQ(shader->keywords.size(), 2u);
	EXPECT_EQ(shader->variants.size(), 1u);

	Guid materialGuid = AssetDatabase::CreateAsset(Path("project://Keywords.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(materialGuid.IsValid());
	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(materialGuid);
	ASSERT_NE(materialAsset, nullptr);
	MaterialAsset* material = static_cast<MaterialAsset*>(materialAsset);
	material->shaderGuid = shaderGuid;
	Asset* materialsShaderRef = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(materialsShaderRef, nullptr);
	material->shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(materialsShaderRef));
	material->SyncToLayout(shader->GetMaterialParamLayout(), {});
	material->SetColor("baseColor", Color::White());

	Vector<str> enabled{GetMemoryResource(MemTag::Asset)};
	enabled.push_back(str("TINT_GREEN"));
	material->SetKeywords({enabled.data(), enabled.size()});
	shader->GetOrCompileVariant({material->keywords.data(), material->keywords.size()});

	EXPECT_EQ(shader->variants.size(), 2u);
	EXPECT_EQ(shader->FindVariant(material->keywordHash), 1u);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<Scene> scene = SceneSystem::CreateScene("Keywords");
	Handle<SceneObject> object = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(meshAsset)));
	renderer->SetMaterial(0, Handle<MaterialAsset>(material));
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -3.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -3.0f);
	desc.clearColor = Color::Black();
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RGBA tinted = BrightestTexel(pixels, size);
	EXPECT_GT(tinted.g, 40u);
	EXPECT_GT(tinted.g, tinted.r * 2u);
	EXPECT_GT(tinted.g, tinted.b * 2u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(shaderAsset);
}

TEST_F(CreatedShaderFixture, EditRecompilesUsedVariants) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	std::filesystem::path shaderFile = _project / "ReloadKeywords.hlsl";
	FileSystem::WriteTextFile(Path(shaderFile.generic_string()), TwoKeywordShader());
	Guid shaderGuid = AssetDatabase::EnsureAssetIdentity(Path("project://ReloadKeywords.hlsl"));
	ASSERT_TRUE(shaderGuid.IsValid());
	Asset* shaderAsset = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(shaderAsset, nullptr);
	ShaderAsset* shader = static_cast<ShaderAsset*>(shaderAsset);
	if (shader->variants.empty()) {
		AssetDatabase::UnloadAsset(shaderAsset);
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}

	Guid materialGuid = AssetDatabase::CreateAsset(Path("project://ReloadKeywords.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(materialGuid.IsValid());
	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(materialGuid);
	ASSERT_NE(materialAsset, nullptr);
	MaterialAsset* material = static_cast<MaterialAsset*>(materialAsset);
	material->shaderGuid = shaderGuid;
	Asset* materialsShaderRef = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(materialsShaderRef, nullptr);
	material->shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(materialsShaderRef));
	material->SyncToLayout(shader->GetMaterialParamLayout(), {});
	material->SetColor("baseColor", Color::White());
	Vector<str> enabled{GetMemoryResource(MemTag::Asset)};
	enabled.push_back(str("TINT_GREEN"));
	material->SetKeywords({enabled.data(), enabled.size()});
	ASSERT_EQ(shader->GetOrCompileVariant({material->keywords.data(), material->keywords.size()}), 1u);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<Scene> scene = SceneSystem::CreateScene("ReloadKeywords");
	Handle<SceneObject> object = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(meshAsset)));
	renderer->SetMaterial(0, Handle<MaterialAsset>(material));
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -3.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -3.0f);
	desc.clearColor = Color::Black();
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RGBA before = BrightestTexel(pixels, size);
	ASSERT_GT(before.g, before.r * 2u);

	str edited = TwoKeywordShader();
	size_t arm = edited.find("\ttint = float3(0, 1, 0);\n");
	ASSERT_NE(arm, str::npos);
	edited.replace(arm, str("\ttint = float3(0, 1, 0);\n").size(), "\ttint = float3(1, 0, 1);\n");
	FileSystem::WriteTextFile(Path(shaderFile.generic_string()), edited);
	RenderSystem::NotifyShaderFileChanged(str(shaderFile.generic_string()));
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	EXPECT_EQ(shader->variants.size(), 2u);
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RGBA after = BrightestTexel(pixels, size);
	EXPECT_GT(after.r, 40u);
	EXPECT_GT(after.r, after.g);
	EXPECT_GT(after.b, after.g);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(shaderAsset);
}

namespace {

	u32 BlendedTexelsAlongADiagonalLine(u32& outDistinct) {
		ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
		EXPECT_NE(target, INVALID_VIEW_TARGET);

		const DebugLine lines[] = {
			{Vec3(-1.5f, -1.5f, 0.0f), Vec3(1.5f, 1.5f, 0.0f), Color32(255, 255, 255, 255)},
		};
		ViewDesc desc;
		desc.view = LookForwardView(Vec3(0.0f, 0.0f, -4.0f));
		desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
		desc.cameraPosition = Vec3(0.0f, 0.0f, -4.0f);
		desc.clearColor = Color::Black();
		desc.lines = lines;
		RenderSystem::SubmitView(target, desc);
		RenderSystem::RenderAll();

		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		EXPECT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
		outDistinct = CountDistinct(pixels);

		u32 blended = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				u8 r = TexelAt(pixels, size, x, y).r;
				if (r != 0 && r != 255) {
					++blended;
				}
			}
		}
		RenderSystem::DestroyViewTarget(target);
		return blended;
	}

} // anonymous namespace

TEST_F(MSAAViewFixture, SettingOneStaysSingleSampled) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	u32 distinct = 0;
	EXPECT_EQ(BlendedTexelsAlongADiagonalLine(distinct), 0u);
}

TEST_F(MSAAViewFixture, Setting4AntialiasesEdge) {
	InitializeAt(4);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (RenderSystem::GetSampleCount() != 4) {
		GTEST_SKIP() << "adapter doesn't offer 4x on the scene chain";
	}
	u32 distinct = 0;
	u32 blended = BlendedTexelsAlongADiagonalLine(distinct);
	EXPECT_GT(blended, 0u);
	EXPECT_GT(distinct, 2u);
}

TEST_F(MSAAViewFixture, PipelinesCompiledOncePerCount) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}

	const Guid low = AssetDatabase::CreateAsset(Path("project://Assets/Low.slprofile"), StringHash("GraphicsProfile"));
	const Guid high = AssetDatabase::CreateAsset(Path("project://Assets/High.slprofile"), StringHash("GraphicsProfile"));
	ASSERT_TRUE(low.IsValid() && high.IsValid());
	static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(low))->msaa = 1;
	static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(high))->msaa = 4;

	GraphicsSettings* settings =
		Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
	ASSERT_NE(settings, nullptr);
	settings->qualityProfiles = low.ToString() + ";" + high.ToString();
	settings->qualityLevel = 0;
	Settings::NotifyChanged("Graphics", "qualityProfiles");

	u32 ignored = 0;
	auto renderAtLevel = [&](i32 level) -> u32 {
		EXPECT_TRUE(Graphics::SetQualityLevel(level));
		BlendedTexelsAlongADiagonalLine(ignored);
		return RenderSystem::GetStats().livePipelines;
	};

	const u32 afterLow = renderAtLevel(0);
	EXPECT_GT(afterLow, 0u);
	ASSERT_NE(RenderSystem::GetActiveProfile(), nullptr);
	ASSERT_EQ(RenderSystem::GetActiveProfile()->msaa, 1);
	ASSERT_EQ(RenderSystem::GetSampleCount(), 1u);

	const u32 afterHigh = renderAtLevel(1);
	ASSERT_EQ(RenderSystem::GetActiveProfile()->msaa, 4);
	if (RenderSystem::GetSampleCount() != 4) {
		GTEST_SKIP() << "adapter clamped 4x down";
	}
	const u32 compiledForHigh = afterHigh - afterLow;
	EXPECT_GT(compiledForHigh, 0u);

	EXPECT_EQ(renderAtLevel(0), afterHigh);
	EXPECT_EQ(RenderSystem::GetSampleCount(), 1u);

	EXPECT_EQ(renderAtLevel(1), afterHigh);
	EXPECT_EQ(RenderSystem::GetSampleCount(), 4u);
}

TEST_F(MSAAViewFixture, RenderScaleShrinksSceneChain) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	const Guid id = AssetDatabase::CreateAsset(Path("project://Assets/Scale.slprofile"), StringHash("GraphicsProfile"));
	ASSERT_TRUE(id.IsValid());
	auto* profile = static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(id));
	ASSERT_NE(profile, nullptr);

	GraphicsSettings* settings =
		Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
	ASSERT_NE(settings, nullptr);
	settings->qualityProfiles = id.ToString();
	settings->qualityLevel = 0;
	Settings::NotifyChanged("Graphics", "qualityLevel");

	u32 distinctAtFull = 0;
	profile->renderScale = 1.0f;
	const u32 blendedAtFull = BlendedTexelsAlongADiagonalLine(distinctAtFull);
	const u32 texturesAtFull = RenderSystem::GetStats().liveTextures;
	ASSERT_NE(RenderSystem::GetActiveProfile(), nullptr);
	ASSERT_FLOAT_EQ(RenderSystem::GetActiveProfile()->renderScale, 1.0f);
	ASSERT_EQ(RenderSystem::GetSampleCount(), 1u);
	ASSERT_EQ(blendedAtFull, 0u);

	u32 distinctAtHalf = 0;
	profile->renderScale = 0.5f;
	const u32 blendedAtHalf = BlendedTexelsAlongADiagonalLine(distinctAtHalf);
	const u32 texturesAtHalf = RenderSystem::GetStats().liveTextures;
	ASSERT_FLOAT_EQ(RenderSystem::GetActiveProfile()->renderScale, 0.5f);

	EXPECT_GT(blendedAtHalf, 0u);
	EXPECT_GT(distinctAtHalf, distinctAtFull);
	EXPECT_GT(texturesAtHalf, texturesAtFull);

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -4.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -4.0f);
	desc.clearColor = Color::Black();
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	EXPECT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	EXPECT_EQ(size, (Vec2I{64, 64}));
	RenderSystem::DestroyViewTarget(target);
}

TEST_F(MSAAViewFixture, SmallerShadowAtlasIsRecreatedNotIgnored) {
	InitializeAt(1);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	const Guid id = AssetDatabase::CreateAsset(Path("project://Assets/Atlas.slprofile"), StringHash("GraphicsProfile"));
	ASSERT_TRUE(id.IsValid());
	auto* profile = static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(id));
	ASSERT_NE(profile, nullptr);
	profile->shadowAtlasSize = 512;

	GraphicsSettings* settings =
		Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
	ASSERT_NE(settings, nullptr);
	settings->qualityProfiles = id.ToString();
	settings->qualityLevel = 0;
	Settings::NotifyChanged("Graphics", "qualityLevel");

	u32 ignored = 0;
	BlendedTexelsAlongADiagonalLine(ignored);
	ASSERT_NE(RenderSystem::GetActiveProfile(), nullptr);
	EXPECT_EQ(RenderSystem::GetActiveProfile()->shadowAtlasSize, 512);

	profile->shadowAtlasSize = 300;
	BlendedTexelsAlongADiagonalLine(ignored);
	EXPECT_EQ(profile->shadowAtlasSize, 300);

	EXPECT_TRUE(RenderSystem::IsRenderingAvailable());
}

TEST_F(MSAAViewFixture, UnofferedCountIsClampedDownNotRefused) {
	InitializeAt(64);
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	u32 resolved = RenderSystem::GetSampleCount();
	EXPECT_GE(resolved, 1u);
	EXPECT_LT(resolved, 64u);
	EXPECT_EQ(resolved & (resolved - 1), 0u);
}

namespace {

	class ShadowViewFixture : public ::testing::Test {
	protected:
		bool _mounted = false;
		bool _projectMounted = false;
		bool _up = false;
		std::filesystem::path _root;
		Guid _profileId;

		void InitializeWith() {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_shadow_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
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
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			_up = true;
		}

		template <typename Tune>
		void UseProfile(Tune&& tune) {
			if (!_profileId.IsValid()) {
				_profileId = AssetDatabase::CreateAsset(Path("project://Assets/Test.slprofile"), StringHash("GraphicsProfile"));
				ASSERT_TRUE(_profileId.IsValid());
			}
			Asset* asset = AssetDatabase::LoadAssetByGuid(_profileId);
			ASSERT_NE(asset, nullptr);
			tune(*static_cast<GraphicsProfile*>(asset));

			GraphicsSettings* settings =
				Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
			ASSERT_NE(settings, nullptr);
			settings->qualityProfiles = _profileId.ToString();
			settings->qualityLevel = 0;
			Settings::NotifyChanged("Graphics", "qualityLevel");
		}

		void ShutdownSystems() {
			if (!_up) {
				return;
			}
			_up = false;
			_profileId = {};
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
				_mounted = false;
			}
			if (_projectMounted) {
				FileSystem::Unmount("project");
				_projectMounted = false;
			}
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void TearDown() override { ShutdownSystems(); }

		Handle<Light> BuildOccludedScene(Handle<Scene> scene, Handle<MeshAsset> cube) {
			Handle<SceneObject> ground = SceneSystem::CreateObject("Ground", scene);
			ground->SetLocalPosition(Vec3(0.3f, -1.0f, 0.7f));
			ground->SetLocalScale(Vec3(40.0f, 0.4f, 40.0f));
			ground->AddComponent<MeshRenderer>()->SetMesh(cube);

			Handle<SceneObject> block = SceneSystem::CreateObject("Occluder", scene);
			block->SetLocalPosition(Vec3(1.1f, 3.4f, -0.6f));
			block->SetLocalScale(Vec3(3.0f, 3.0f, 3.0f));
			block->AddComponent<MeshRenderer>()->SetMesh(cube);

			Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", scene);
			sun->SetLocalRotation(Quat::FromEuler(Vec3(52.0f, 37.0f, 11.0f)));
			Handle<Light> light = sun->AddComponent<Light>();
			light->SetIntensity(2.0f);
			return light;
		}

		void RenderTo(ViewTargetId target, Vector<u8>& pixels, Vec2I& size) {
			RenderFrom(target, Vec3(-6.0f, 11.0f, -15.0f), Vec3(0.5f, 0.0f, 0.0f), pixels, size);
		}

		void SubmitFrom(ViewTargetId target, const Vec3& eye, bool retainCascadeFit) {
			ViewDesc desc;
			desc.view = Mat4::LookAt(eye, Vec3::Zero());
			desc.projection = Mat4::PerspectiveReverseZ(55.0f, 1.0f, 0.1f, 500.0f);
			desc.cameraPosition = eye;
			desc.clearColor = Color::Black();
			desc.retainCascadeFit = retainCascadeFit;
			RenderSystem::SubmitView(target, desc);
		}

		void SubmitFrozen(ViewTargetId target, const Vec3& eye, const Vec3& fitEye) {
			const Mat4 fitView = Mat4::LookAt(fitEye, Vec3::Zero());
			const Mat4 fitProjection = Mat4::PerspectiveReverseZ(55.0f, 1.0f, 0.1f, 500.0f);
			ViewDesc desc;
			desc.view = Mat4::LookAt(eye, Vec3::Zero());
			desc.projection = Mat4::PerspectiveReverseZ(55.0f, 1.0f, 0.1f, 500.0f);
			desc.cameraPosition = eye;
			desc.clearColor = Color::Black();
			desc.retainCascadeFit = true;
			desc.shadowFitView = &fitView;
			desc.shadowFitProjection = &fitProjection;
			RenderSystem::SubmitView(target, desc);
		}

		void SubmitOverlay(ViewTargetId target, DebugOverlay overlay, DebugView view = DebugView::None) {
			const Vec3 eye(-6.0f, 11.0f, -15.0f);
			ViewDesc desc;
			desc.view = Mat4::LookAt(eye, Vec3(0.5f, 0.0f, 0.0f));
			desc.projection = Mat4::PerspectiveReverseZ(55.0f, 1.0f, 0.1f, 500.0f);
			desc.cameraPosition = eye;
			desc.clearColor = Color::Black();
			desc.debugOverlay = overlay;
			desc.debugView = view;
			RenderSystem::SubmitView(target, desc);
		}

		u32 FitOf(ViewTargetId target, Mat4& outFirst) {
			CascadeFit fits[MAX_SHADOW_CASCADES];
			const u32 count = RenderSystem::GetCascadeFit(target, {fits, std::size(fits)});
			if (count > 0) {
				outFirst = fits[0].viewProjection;
			}
			return count;
		}

		void RenderFrom(ViewTargetId target, const Vec3& eye, const Vec3& lookAt, Vector<u8>& pixels, Vec2I& size) {
			ViewDesc desc;
			desc.view = Mat4::LookAt(eye, lookAt);
			desc.projection = Mat4::PerspectiveReverseZ(55.0f, 1.0f, 0.1f, 500.0f);
			desc.cameraPosition = eye;
			desc.clearColor = Color::Black();
			RenderSystem::SubmitView(target, desc);
			RenderSystem::RenderAll();
			ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
		}

		Handle<Light> BuildPunctualRig(Handle<Scene> scene, Handle<MeshAsset> cube,
									   const Vec3& axis, LightType type) {
			Handle<SceneObject> lightObject = SceneSystem::CreateObject("Punctual", scene);
			lightObject->SetLocalPosition(Vec3::Zero());
			lightObject->SetLocalRotation(Quat::LookRotation(axis.Normalized()));
			Handle<Light> light = lightObject->AddComponent<Light>();
			light->SetLightType(type);
			light->SetRange(60.0f);
			light->SetIntensity(500.0f);
			light->SetSpotAngleDeg(70.0f);

			Handle<SceneObject> receiver = SceneSystem::CreateObject("Receiver", scene);
			receiver->SetLocalPosition(axis * 12.0f);
			receiver->SetLocalScale(Vec3(9.0f, 9.0f, 9.0f));
			receiver->AddComponent<MeshRenderer>()->SetMesh(cube);

			Handle<SceneObject> blocker = SceneSystem::CreateObject("Blocker", scene);
			blocker->SetLocalPosition(axis * 3.5f);
			blocker->SetLocalScale(Vec3(1.6f, 1.6f, 1.6f));
			blocker->AddComponent<MeshRenderer>()->SetMesh(cube);
			return light;
		}
	};

	Vec3 PerpendicularTo(const Vec3& axis) {
		const Vec3 seed = Math::Abs(axis.Normalized().y) > 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
		return Vec3::Cross(axis, seed).Normalized();
	}

	u32 Luma(const RGBA& texel) {
		return static_cast<u32>(texel.r) + texel.g + texel.b;
	}

} // anonymous namespace

TEST_F(ShadowViewFixture, OccluderOnlyDarkens) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));

	Handle<Scene> scene = SceneSystem::CreateScene("Shadows");
	Handle<Light> sun = BuildOccludedScene(scene, cube);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	Vector<u8> shadowed{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> unshadowed{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> again{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderTo(target, shadowed, size);
	if (RenderSystem::GetStats().shadowLights == 0) {
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}

	sun->SetCastShadows(false);
	RenderTo(target, unshadowed, size);
	sun->SetCastShadows(true);
	RenderTo(target, again, size);

	ASSERT_EQ(shadowed.size(), unshadowed.size());
	u32 darker = 0;
	u32 brighter = 0;
	u32 identical = 0;
	for (i32 y = 0; y < size.y; ++y) {
		for (i32 x = 0; x < size.x; ++x) {
			const u32 withShadow = Luma(TexelAt(shadowed, size, x, y));
			const u32 without = Luma(TexelAt(unshadowed, size, x, y));
			if (withShadow + 3 < without) {
				++darker;
			} else if (withShadow > without + 3) {
				++brighter;
			} else {
				++identical;
			}
		}
	}
	EXPECT_GT(darker, 50u);
	EXPECT_EQ(brighter, 0u) << brighter;
	EXPECT_GT(identical, 100u);
	EXPECT_EQ(shadowed, again);

	GraphicsStats stats = RenderSystem::GetStats();
	EXPECT_EQ(stats.shadowLights, 1u);
	EXPECT_EQ(stats.shadowMapsRendered, 4u) << stats.shadowMapsRendered << " maps rendered";

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}

TEST_F(ShadowViewFixture, NonCasterRendersNoMaps) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<Scene> scene = SceneSystem::CreateScene("Shadows");
	Handle<Light> sun = BuildOccludedScene(scene, Handle<MeshAsset>(static_cast<MeshAsset*>(meshAsset)));
	sun->SetCastShadows(false);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderTo(target, pixels, size);

	GraphicsStats stats = RenderSystem::GetStats();
	EXPECT_EQ(stats.shadowLights, 0u);
	EXPECT_EQ(stats.shadowMapsRendered, 0u);

	sun->SetCastShadows(true);
	RenderTo(target, pixels, size);
	stats = RenderSystem::GetStats();
	EXPECT_EQ(stats.shadowLights, 1u);
	EXPECT_EQ(stats.shadowMapsRendered, 4u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}

namespace {

	constexpr const char* CASCADE_PROBE_SOURCE =
		"#pragma shalltear_template lit\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 baseColor; };\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tuint code = ShalltearCascadeOf(ShalltearViewDepth(input.positionWS)) + 1;\n"
		"\tsurface.baseColor = baseColor.rgb;\n"
		"\tsurface.emissive = float3((code & 1) ? 1.0 : 0.0, (code & 2) ? 1.0 : 0.0, (code & 4) ? 1.0 : 0.0);\n"
		"}\n";

	u32 DecodeCascadeCode(const RGBA& texel) {
		if (Luma(texel) < 120) {
			return 0;
		}
		return (texel.r > 150 ? 1u : 0u) | (texel.g > 150 ? 2u : 0u) | (texel.b > 150 ? 4u : 0u);
	}

} // anonymous namespace

TEST_F(ShadowViewFixture, BoundaryTexelUsesSplitCascade) {
	constexpr f32 PROBE_SHADOW_DISTANCE = 40.0f;
	constexpr u32 PROBE_CASCADES = 4;
	constexpr f32 PROBE_NEAR = 0.1f;
	InitializeWith();
	f32 first = 0.0f;
	UseProfile([&first](GraphicsProfile& profile) {
		profile.shadowDistance = PROBE_SHADOW_DISTANCE;
		first = profile.shadowFirstCascadeDistance;
	});
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	std::filesystem::path project = _root / "Assets";
	std::filesystem::create_directories(project);
	FileSystem::Mount("project", Path(project.generic_string()));

	Guid shaderGuid = AssetDatabase::CreateAsset(Path("project://CascadeProbe.hlsl"), StringHash("ShaderAsset"));
	Guid materialGuid = AssetDatabase::CreateAsset(Path("project://CascadeProbe.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(shaderGuid.IsValid());
	ASSERT_TRUE(materialGuid.IsValid());
	Asset* shaderAsset = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(shaderAsset, nullptr);
	ShaderAsset* shader = static_cast<ShaderAsset*>(shaderAsset);
	if (shader->variants.empty()) {
		AssetDatabase::UnloadAsset(shaderAsset);
		FileSystem::Unmount("project");
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	FileSystem::WriteTextFile(Path("project://CascadeProbe.hlsl"), str(CASCADE_PROBE_SOURCE));
	ASSERT_TRUE(shader->Import(FileSystem::Resolve(Path("project://CascadeProbe.hlsl"))));

	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(materialGuid);
	ASSERT_NE(materialAsset, nullptr);
	MaterialAsset* material = static_cast<MaterialAsset*>(materialAsset);
	material->shaderGuid = shaderGuid;
	material->shader = Handle<ShaderAsset>(shader);
	Vector<str> textureSlots{GetMemoryResource(MemTag::Asset)};
	shader->GetMaterialTextureSlots(textureSlots);
	material->SyncToLayout(shader->GetMaterialParamLayout(), {textureSlots.data(), textureSlots.size()});

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));

	Handle<Scene> scene = SceneSystem::CreateScene("Cascades");
	Handle<SceneObject> marker = SceneSystem::CreateObject("Marker", scene);
	Handle<MeshRenderer> markerRenderer = marker->AddComponent<MeshRenderer>();
	markerRenderer->SetMesh(cube);
	markerRenderer->SetMaterial(0, Handle<MaterialAsset>(material));

	Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", scene);
	sun->SetLocalRotation(Quat::FromEuler(Vec3(52.0f, 37.0f, 11.0f)));
	sun->AddComponent<Light>();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	auto renderAtDepth = [&](f32 depth) {
		marker->SetLocalPosition(Vec3(0.0f, 0.0f, depth));
		marker->SetLocalScale(Vec3(depth * 0.12f, depth * 0.12f, 0.02f));
		SceneSystem::EndFrame();

		ViewDesc desc;
		desc.view = Mat4::LookAt(Vec3::Zero(), Vec3(0.0f, 0.0f, 1.0f));
		desc.projection = Mat4::PerspectiveReverseZ(55.0f, 1.0f, PROBE_NEAR, 500.0f);
		desc.cameraPosition = Vec3::Zero();
		desc.clearColor = Color::Black();
		RenderSystem::SubmitView(target, desc);
		RenderSystem::RenderAll();
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		RenderSystem::ReadbackViewTarget(target, pixels, size);
		return DecodeCascadeCode(BrightestTexel(pixels, size));
	};

	const GraphicsInternal::CascadeSplits splits =
		GraphicsInternal::SolveCascadeSplits(PROBE_NEAR, PROBE_SHADOW_DISTANCE, PROBE_CASCADES, first);
	ASSERT_EQ(splits.count, PROBE_CASCADES);

	Vector<f32> depths{GetMemoryResource(MemTag::Graphics)};
	for (u32 i = 0; i < splits.count; ++i) {
		depths.push_back((splits.depths[i] + splits.depths[i + 1]) * 0.5f);
	}
	for (u32 boundary = 1; boundary < splits.count; ++boundary) {
		depths.push_back(splits.depths[boundary] * 0.97f);
		depths.push_back(splits.depths[boundary] * 1.03f);
	}

	for (f32 depth : depths) {
		const u32 expected = GraphicsInternal::SelectCascade(splits, depth) + 1;
		const u32 actual = renderAtDepth(depth);
		EXPECT_EQ(actual, expected);
	}

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(shaderAsset);
	FileSystem::Unmount("project");
}

TEST_F(ShadowViewFixture, HighTierCascade0UnderCentimetre) {
	InitializeWith();
	str projectPath;
	ASSERT_TRUE(ProjectOps::CreateProject(_root.generic_string(), "Probe", projectPath));
	GraphicsProfile high;
	ASSERT_TRUE(high.Import(Path((std::filesystem::path(projectPath) / "Assets" / "Settings" / "3 High.slprofile").generic_string())));
	UseProfile([&high](GraphicsProfile& profile) {
		profile.shadowDistance = high.shadowDistance;
		profile.shadowCascades = high.shadowCascades;
		profile.shadowFirstCascadeDistance = high.shadowFirstCascadeDistance;
		profile.shadowAtlasSize = high.shadowAtlasSize;
		profile.shadowPcfTaps = high.shadowPcfTaps;
	});
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* cubeAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	Asset* planeAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Plane.primitive"));
	Asset* materialAsset = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
	ASSERT_NE(cubeAsset, nullptr);
	ASSERT_NE(planeAsset, nullptr);
	ASSERT_NE(materialAsset, nullptr);
	const Handle<MaterialAsset> material(static_cast<MaterialAsset*>(materialAsset));

	Handle<Scene> scene = SceneSystem::CreateScene("Report");
	EditorSceneOps::PopulateNewScene(scene);
	Handle<Camera> camera;
	for (u32 index = 0; index < scene->GetRootCount(); ++index) {
		if (Handle<SceneObject> root = scene->GetRoot(index); root.IsValid() && root->GetComponent<Camera>().IsValid()) {
			camera = root->GetComponent<Camera>();
		}
	}
	ASSERT_TRUE(camera.IsValid());

	Handle<SceneObject> plane = SceneSystem::CreateObject("Plane", scene);
	Handle<MeshRenderer> planeRenderer = plane->AddComponent<MeshRenderer>();
	planeRenderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(planeAsset)));
	planeRenderer->SetMaterial(0, material);
	Handle<SceneObject> cube = SceneSystem::CreateObject("Cube", scene);
	cube->SetLocalPosition(Vec3(0.0f, 0.5f, 0.0f));
	Handle<MeshRenderer> cubeRenderer = cube->AddComponent<MeshRenderer>();
	cubeRenderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cubeAsset)));
	cubeRenderer->SetMaterial(0, material);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({1280, 720});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const Vec3 eye(0.0f, 6.0f, 0.001f);
	ViewDesc desc;
	desc.view = Mat4::LookAt(eye, Vec3::Zero());
	desc.projection = camera->BuildProjection(1280.0f / 720.0f);
	desc.cameraPosition = eye;
	desc.clearColor = Color::Black();
	desc.retainCascadeFit = true;
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	CascadeFit fits[MAX_SHADOW_CASCADES];
	const u32 count = RenderSystem::GetCascadeFit(target, {fits, std::size(fits)});
	ASSERT_EQ(count, static_cast<u32>(high.shadowCascades));
	EXPECT_LE(fits[0].worldTexelSize, 0.01f) << fits[0].farDepth << " m";
	EXPECT_FLOAT_EQ(fits[count - 1].farDepth, high.shadowDistance);

	u32 receiver = 0;
	while (receiver + 1 < count && fits[receiver].farDepth < 6.0f) {
		++receiver;
	}
	EXPECT_LT(fits[receiver].worldTexelSize, 0.025f);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(planeAsset);
	AssetDatabase::UnloadAsset(cubeAsset);
}

TEST_F(ShadowViewFixture, Cascade0EndsWhereProfileSays) {
	constexpr f32 DISTANCE = 60.0f;
	constexpr f32 FIRST = 5.0f;
	InitializeWith();
	UseProfile([](GraphicsProfile& profile) {
		profile.shadowDistance = DISTANCE;
		profile.shadowFirstCascadeDistance = FIRST;
	});
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<Scene> scene = SceneSystem::CreateScene("Counts");
	BuildOccludedScene(scene, Handle<MeshAsset>(static_cast<MeshAsset*>(meshAsset)));
	SceneSystem::EndFrame();
	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	for (i32 count = 1; count <= static_cast<i32>(MAX_SHADOW_CASCADES); ++count) {
		UseProfile([count](GraphicsProfile& profile) { profile.shadowCascades = count; });
		SubmitFrom(target, Vec3(-6.0f, 11.0f, -15.0f), true);
		RenderSystem::RenderAll();

		CascadeFit fits[MAX_SHADOW_CASCADES];
		ASSERT_EQ(RenderSystem::GetCascadeFit(target, {fits, std::size(fits)}), static_cast<u32>(count));
		EXPECT_FLOAT_EQ(fits[count - 1].farDepth, DISTANCE) << count << " cascades";
		if (count == 1) {
			continue;
		}
		EXPECT_NEAR(fits[0].farDepth, FIRST, 1e-3f) << count << " cascades";
		const f32 ratio = std::pow(DISTANCE / FIRST, 1.0f / static_cast<f32>(count - 1));
		for (i32 i = 0; i + 1 < count; ++i) {
			EXPECT_NEAR(fits[i + 1].farDepth / fits[i].farDepth, ratio, 1e-3f) << "cascade " << i + 1 << " of " << count;
		}
	}

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	AssetDatabase::UnloadAsset(meshAsset);
}

TEST_F(ShadowViewFixture, OldProfileKeepsNearFieldSharp) {
	constexpr const char* SAVED_BEFORE_X54 =
		"{\n"
		"\t\"_type\": \"GraphicsProfile\",\n"
		"\t\"assetId\": \"0000000000000000-0000000000000000\",\n"
		"\t\"virtualPath\": \"\",\n"
		"\t\"msaa\": 4,\n"
		"\t\"renderScale\": 1,\n"
		"\t\"tonemap\": \"Neutral\",\n"
		"\t\"lightsPerView\": 256,\n"
		"\t\"lightsPerCluster\": 32,\n"
		"\t\"shadowDistance\": 150,\n"
		"\t\"shadowCascades\": 4,\n"
		"\t\"shadowCascadeLambda\": 0.5,\n"
		"\t\"shadowPcfTaps\": 3,\n"
		"\t\"shadowAtlasSize\": 2048,\n"
		"\t\"transparentReceiveShadows\": true,\n"
		"\t\"reflectionProbeResolution\": 128\n"
		"}\n";
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);
	ASSERT_TRUE(FileSystem::WriteTextFile(Path("project://Assets/Saved.slprofile"), str(SAVED_BEFORE_X54)));
	AssetDatabase::ScanProjectDirectory();
	const Guid saved = AssetDatabase::LookupGuid(Path("project://Assets/Saved.slprofile"));
	ASSERT_TRUE(saved.IsValid());
	GraphicsSettings* settings =
		Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
	ASSERT_NE(settings, nullptr);
	settings->qualityProfiles = saved.ToString();
	settings->qualityLevel = 0;
	Settings::NotifyChanged("Graphics", "qualityLevel");

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<Scene> scene = SceneSystem::CreateScene("Saved");
	BuildOccludedScene(scene, Handle<MeshAsset>(static_cast<MeshAsset*>(meshAsset)));
	SceneSystem::EndFrame();
	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	SubmitFrom(target, Vec3(-6.0f, 11.0f, -15.0f), true);
	RenderSystem::RenderAll();

	const f32 ratio = std::pow(150.0f / 4.0f, 1.0f / 3.0f);
	CascadeFit fits[MAX_SHADOW_CASCADES];
	ASSERT_EQ(RenderSystem::GetCascadeFit(target, {fits, std::size(fits)}), 4u);
	EXPECT_NEAR(fits[0].farDepth, 4.0f, 1e-3f);
	EXPECT_NEAR(fits[1].farDepth, 4.0f * ratio, 1e-3f);
	EXPECT_NEAR(fits[2].farDepth, 4.0f * ratio * ratio, 1e-2f);
	EXPECT_FLOAT_EQ(fits[3].farDepth, 150.0f);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	AssetDatabase::UnloadAsset(meshAsset);
}

namespace {

	f32 SRGBToLinear(u8 value) {
		const f32 c = static_cast<f32>(value) / 255.0f;
		return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
	}

	class ShadowFilterFixture : public ShadowViewFixture {
	protected:
		static constexpr i32 PIXELS = 512;
		static constexpr i32 ATLAS = 512;
		static constexpr f32 EYE_HEIGHT = 5.0f;

		Handle<Scene> _scene;
		Handle<Light> _light;
		Asset* _cube = nullptr;
		Asset* _material = nullptr;
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Vec3 _center;
		f32 _window = 1.0f;
		Vector<u8> _unshadowed{GetMemoryResource(MemTag::Graphics)};
		Vector<u8> _ambient{GetMemoryResource(MemTag::Graphics)};

		void TearDown() override {
			if (_target != INVALID_VIEW_TARGET) {
				RenderSystem::DestroyViewTarget(_target);
			}
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
			}
			if (_material) {
				AssetDatabase::UnloadAsset(_material);
			}
			if (_cube) {
				AssetDatabase::UnloadAsset(_cube);
			}
			ShadowViewFixture::TearDown();
		}

		void BuildRig(const Vec3& travel, f32 distance) {
			InitializeWith();
			UseProfile([distance](GraphicsProfile& profile) {
				profile.shadowCascades = 1;
				profile.shadowDistance = distance;
				profile.shadowAtlasSize = ATLAS;
			});
			if (!RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
			ASSERT_TRUE(_mounted);
			_cube = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
			_material = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
			ASSERT_NE(_cube, nullptr);
			ASSERT_NE(_material, nullptr);
			_scene = SceneSystem::CreateScene("Filter");
			Box("Ground", Vec3(0.0f, -0.1f, 0.0f), Vec3(8.0f, 0.2f, 8.0f));
			Handle<SceneObject> sun = SceneSystem::CreateObject("Light", _scene);
			sun->SetLocalRotation(Quat::LookRotation(travel));
			_light = sun->AddComponent<Light>();
			_light->SetIntensity(1.5f);
			_target = RenderSystem::CreateViewTarget({PIXELS, PIXELS});
			ASSERT_NE(_target, INVALID_VIEW_TARGET);
		}

		Handle<SceneObject> Box(const char* name, const Vec3& position, const Vec3& scale) {
			Handle<SceneObject> box = SceneSystem::CreateObject(name, _scene);
			box->SetLocalPosition(position);
			box->SetLocalScale(scale);
			Handle<MeshRenderer> renderer = box->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(_cube)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(_material)));
			return box;
		}

		Mat4 ViewMatrix() const { return Mat4::LookAt(_center + Vec3(0.0f, EYE_HEIGHT, 0.0f), _center, Vec3(0.0f, 0.0f, 1.0f)); }
		Mat4 Projection() const {
			const f32 half = _window * 0.5f;
			return Mat4::OrthographicReverseZ(-half, half, -half, half, 0.1f, 20.0f);
		}

		void Render(Vector<u8>& pixels) {
			ViewDesc desc;
			desc.view = ViewMatrix();
			desc.projection = Projection();
			desc.cameraPosition = _center + Vec3(0.0f, EYE_HEIGHT, 0.0f);
			desc.clearColor = Color::Black();
			desc.retainCascadeFit = true;
			RenderSystem::SubmitView(_target, desc);
			RenderSystem::RenderAll();
			Vec2I size{};
			ASSERT_TRUE(RenderSystem::ReadbackViewTarget(_target, pixels, size));
			ASSERT_EQ(size.x, PIXELS);
			ASSERT_EQ(size.y, PIXELS);
		}

		void RenderReferences() {
			_light->SetCastShadows(false);
			Render(_unshadowed);
			_light->SetCastShadows(true);
			const f32 intensity = _light->GetIntensity();
			_light->SetIntensity(0.0f);
			Render(_ambient);
			_light->SetIntensity(intensity);
			u8 brightest = 0;
			for (size_t i = 0; i < _unshadowed.size(); i += 4) {
				brightest = Math::Max(brightest, Math::Max(_unshadowed[i], Math::Max(_unshadowed[i + 1], _unshadowed[i + 2])));
			}
			ASSERT_LT(brightest, 250);
			ASSERT_GT(brightest, 100);
		}

		void ShadowFactor(Vector<f32>& out) {
			Vector<u8> shadowed{GetMemoryResource(MemTag::Graphics)};
			Render(shadowed);
			ASSERT_EQ(shadowed.size(), _unshadowed.size());
			out.assign(static_cast<size_t>(PIXELS) * PIXELS, 1.0f);
			for (size_t i = 0; i < out.size(); ++i) {
				f32 light = 0.0f;
				f32 received = 0.0f;
				for (size_t c = 0; c < 3; ++c) {
					const f32 ambient = SRGBToLinear(_ambient[i * 4 + c]);
					light += SRGBToLinear(_unshadowed[i * 4 + c]) - ambient;
					received += SRGBToLinear(shadowed[i * 4 + c]) - ambient;
				}
				out[i] = light > 1e-3f ? received / light : 1.0f;
			}
		}

		Vec3 GroundAt(f32 x, f32 y) const {
			const f32 ndcX = (x + 0.5f) / static_cast<f32>(PIXELS) * 2.0f - 1.0f;
			const f32 ndcY = 1.0f - (y + 0.5f) / static_cast<f32>(PIXELS) * 2.0f;
			const Vec4 world = (Projection() * ViewMatrix()).Inverse() * Vec4(ndcX, ndcY, 0.5f, 1.0f);
			return Vec3(world.x / world.w, 0.0f, world.z / world.w);
		}

		Vec2 AtlasTexel(const Mat4& cascade, const Vec3& point) const {
			const Vec4 clip = cascade * Vec4(point.x, point.y, point.z, 1.0f);
			return Vec2((clip.x / clip.w * 0.5f + 0.5f) * static_cast<f32>(ATLAS),
						(0.5f - clip.y / clip.w * 0.5f) * static_cast<f32>(ATLAS));
		}

		Mat4 Cascade() {
			CascadeFit fits[MAX_SHADOW_CASCADES];
			EXPECT_EQ(RenderSystem::GetCascadeFit(_target, {fits, std::size(fits)}), 1u);
			return fits[0].viewProjection;
		}

		void Row(const Vector<f32>& factor, i32 y, std::vector<Vec2>& out) const {
			out.clear();
			for (i32 x = 0; x < PIXELS; ++x) {
				out.emplace_back(GroundAt(static_cast<f32>(x), static_cast<f32>(y)).x,
								 factor[static_cast<size_t>(y) * PIXELS + x]);
			}
			std::sort(out.begin(), out.end(), [](const Vec2& a, const Vec2& b) { return a.x < b.x; });
		}

		static f32 HalfShadowX(const std::vector<Vec2>& row, f32 from = -1e9f) {
			for (size_t i = 0; i + 1 < row.size(); ++i) {
				if (row[i].x > from && row[i].y < 0.5f && row[i + 1].y >= 0.5f) {
					const f32 t = (0.5f - row[i].y) / (row[i + 1].y - row[i].y);
					return row[i].x + t * (row[i + 1].x - row[i].x);
				}
			}
			return std::numeric_limits<f32>::quiet_NaN();
		}

		void ExpectNoSelfShadowAtEveryWidth() {
			for (i32 width : {1, 3, 5, 7}) {
				UseProfile([width](GraphicsProfile& profile) { profile.shadowPcfTaps = width; });
				Vector<f32> factor{GetMemoryResource(MemTag::Graphics)};
				ASSERT_NO_FATAL_FAILURE(ShadowFactor(factor));
				f32 darkest = 1.0f;
				u32 dark = 0;
				for (f32 value : factor) {
					darkest = Math::Min(darkest, value);
					dark += value < 0.97f ? 1u : 0u;
				}
				EXPECT_EQ(dark, 0u) << "width " << width << ' ' << darkest;
			}
		}
	};

	Vec3 Travel(f32 tilt, f32 heading) {
		const f32 t = tilt * Math::DEG_TO_RAD;
		const f32 h = heading * Math::DEG_TO_RAD;
		return Vec3(std::cos(h) * std::sin(t), -std::cos(t), std::sin(h) * std::sin(t));
	}

	Vec3 EdgeTravel() { return Travel(30.0f, 25.0f); }

	constexpr f32 SLAB_HEIGHT = 1.5f;
	constexpr f32 SLAB_THICKNESS = 0.05f;
	constexpr f32 SLAB_EDGE = -0.2f;

	Vec3 SlabPosition(f32 shift) { return Vec3(SLAB_EDGE - 2.0f + shift, SLAB_HEIGHT + SLAB_THICKNESS * 0.5f, 0.0f); }

	f32 EdgeShadowX() {
		const Vec3 travel = EdgeTravel();
		return SLAB_EDGE + (SLAB_HEIGHT + SLAB_THICKNESS) * travel.x / -travel.y;
	}

} // anonymous namespace

TEST_F(ShadowFilterFixture, PenumbraMonotoneAtEveryWidth) {
	ASSERT_NO_FATAL_FAILURE(BuildRig(EdgeTravel(), 8.0f));
	if (IsSkipped()) {
		return;
	}
	Box("Slab", SlabPosition(0.0f), Vec3(4.0f, SLAB_THICKNESS, 8.0f));
	SceneSystem::EndFrame();
	_center = Vec3(EdgeShadowX(), 0.0f, 0.0f);
	ASSERT_NO_FATAL_FAILURE(RenderReferences());

	for (i32 width : {1, 3, 5, 7}) {
		UseProfile([width](GraphicsProfile& profile) { profile.shadowPcfTaps = width; });
		Vector<f32> factor{GetMemoryResource(MemTag::Graphics)};
		ASSERT_NO_FATAL_FAILURE(ShadowFactor(factor));

		std::vector<Vec2> row;
		f32 worstStepBack = 0.0f;
		u32 crossed = 0;
		for (i32 y = 0; y < PIXELS; ++y) {
			Row(factor, y, row);
			for (size_t i = 0; i + 1 < row.size(); ++i) {
				worstStepBack = Math::Max(worstStepBack, row[i].y - row[i + 1].y);
			}
			crossed += std::isnan(HalfShadowX(row)) ? 0u : 1u;
		}
		EXPECT_EQ(crossed, static_cast<u32>(PIXELS)) << "width " << width;
		EXPECT_LT(worstStepBack, 0.02f) << "width " << width;
	}
}

TEST_F(ShadowFilterFixture, QuarterTexelCasterMoveTracks) {
	ASSERT_NO_FATAL_FAILURE(BuildRig(EdgeTravel(), 8.0f));
	if (IsSkipped()) {
		return;
	}
	Handle<SceneObject> slab = Box("Slab", SlabPosition(0.0f), Vec3(4.0f, SLAB_THICKNESS, 8.0f));
	SceneSystem::EndFrame();
	_center = Vec3(EdgeShadowX(), 0.0f, 0.0f);
	ASSERT_NO_FATAL_FAILURE(RenderReferences());

	const Mat4 cascade = Cascade();
	const f32 top = SLAB_HEIGHT + SLAB_THICKNESS;
	const Vec2 a = AtlasTexel(cascade, Vec3(SLAB_EDGE, top, 0.0f));
	const Vec2 b = AtlasTexel(cascade, Vec3(SLAB_EDGE, top, 1.0f));
	const Vec2 along = (b - a).Normalized();
	const Vec2 normal(-along.y, along.x);
	const f32 texelsPerMeter = Math::Abs(Vec2::Dot(AtlasTexel(cascade, Vec3(SLAB_EDGE + 0.01f, top, 0.0f)) - a, normal)) / 0.01f;
	const f32 quarter = 0.25f / texelsPerMeter;
	const f32 slant = std::atan2(Math::Min(Math::Abs(along.x), Math::Abs(along.y)), Math::Max(Math::Abs(along.x), Math::Abs(along.y)));
	EXPECT_GT(slant * Math::RAD_TO_DEG, 10.0f);

	constexpr i32 STEPS = 8;
	f32 previousError = 1.0f;
	for (i32 width : {1, 3, 5, 7}) {
		UseProfile([width](GraphicsProfile& profile) { profile.shadowPcfTaps = width; });
		std::vector<std::vector<f32>> crossings(STEPS + 1);
		for (i32 step = 0; step <= STEPS; ++step) {
			slab->SetLocalPosition(SlabPosition(quarter * static_cast<f32>(step)));
			SceneSystem::EndFrame();
			Vector<f32> factor{GetMemoryResource(MemTag::Graphics)};
			ASSERT_NO_FATAL_FAILURE(ShadowFactor(factor));
			std::vector<Vec2> row;
			for (i32 y = 0; y < PIXELS; ++y) {
				Row(factor, y, row);
				crossings[step].push_back(HalfShadowX(row));
			}
		}

		f64 sum = 0.0;
		f64 squares = 0.0;
		f32 least = 1.0f;
		u32 measured = 0;
		u32 stalled = 0;
		for (i32 step = 0; step < STEPS; ++step) {
			for (i32 y = 0; y < PIXELS; ++y) {
				const f32 moved = (crossings[step + 1][y] - crossings[step][y]) * texelsPerMeter;
				ASSERT_FALSE(std::isnan(moved)) << "width " << width << ", row " << y;
				least = Math::Min(least, moved);
				stalled += moved < 0.05f ? 1u : 0u;
				sum += moved;
				squares += (moved - 0.25) * (moved - 0.25);
				++measured;
			}
		}
		const f32 error = static_cast<f32>(std::sqrt(squares / measured));
		EXPECT_NEAR(sum / measured, 0.25, 0.01) << "width " << width;
		EXPECT_LT(error, previousError);
		previousError = error;
		if (width == 1) {
			EXPECT_GT(stalled, measured / 4) << stalled << " of " << measured << " steps";
		}
		if (width == 7) {
			EXPECT_GT(least, 0.05f);
		}
	}
	slab->SetLocalPosition(SlabPosition(0.0f));
}

TEST_F(ShadowFilterFixture, SlantedReceiverReadsUnshadowedAtEveryWidth) {
	ASSERT_NO_FATAL_FAILURE(BuildRig(Travel(70.0f, 25.0f), 8.0f));
	if (IsSkipped()) {
		return;
	}
	SceneSystem::EndFrame();
	_center = Vec3::Zero();
	ASSERT_NO_FATAL_FAILURE(RenderReferences());
	ASSERT_NO_FATAL_FAILURE(ExpectNoSelfShadowAtEveryWidth());
	EXPECT_EQ(RenderSystem::GetStats().shadowMapsRendered, 1u);
}

TEST_F(ShadowFilterFixture, SlantedReceiverUnshadowed) {
	ASSERT_NO_FATAL_FAILURE(BuildRig(Travel(70.0f, 25.0f), 8.0f));
	if (IsSkipped()) {
		return;
	}
	Handle<SceneObject> owner = _light->GetOwner();
	owner->SetLocalPosition(Vec3(-3.0f, 2.0f, 0.0f));
	owner->SetLocalRotation(Quat::LookRotation(Vec3(3.0f, -2.0f, 0.0f)));
	_light->SetLightType(LightType::Spot);
	_light->SetRange(20.0f);
	_light->SetSpotAngleDeg(60.0f);
	_light->SetIntensity(12.0f);
	_light->SetCastShadows(true);
	SceneSystem::EndFrame();
	_center = Vec3::Zero();
	ASSERT_NO_FATAL_FAILURE(RenderReferences());
	ASSERT_NO_FATAL_FAILURE(ExpectNoSelfShadowAtEveryWidth());
	EXPECT_EQ(RenderSystem::GetStats().shadowMapsRendered, 1u);
}

TEST_F(ShadowFilterFixture, CubeShadowMeetsBase) {
	const Vec3 travel = Travel(45.0f, 25.0f);
	ASSERT_NO_FATAL_FAILURE(BuildRig(travel, 8.0f));
	if (IsSkipped()) {
		return;
	}
	Box("Cube", Vec3(0.0f, 0.5f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
	SceneSystem::EndFrame();
	_window = 1.2f;
	_center = Vec3(0.95f, 0.0f, 0.2f);
	ASSERT_NO_FATAL_FAILURE(RenderReferences());

	const f32 farEdge = 0.5f + travel.x / -travel.y;
	const Mat4 cascade = Cascade();
	const f32 groundTexel = 0.01f / (AtlasTexel(cascade, Vec3(0.51f, 0.0f, 0.2f)) - AtlasTexel(cascade, Vec3(0.5f, 0.0f, 0.2f))).Length();

	f32 firstShift = 0.0f;
	for (i32 width : {1, 3, 5, 7}) {
		UseProfile([width](GraphicsProfile& profile) { profile.shadowPcfTaps = width; });
		Vector<f32> factor{GetMemoryResource(MemTag::Graphics)};
		ASSERT_NO_FATAL_FAILURE(ShadowFactor(factor));
		f32 base = 0.0f;
		f64 shift = 0.0;
		u32 rows = 0;
		std::vector<Vec2> row;
		for (i32 y = 0; y < PIXELS; ++y) {
			const f32 z = GroundAt(0.0f, static_cast<f32>(y)).z;
			if (z < 0.05f || z > 0.35f) {
				continue;
			}
			Row(factor, y, row);
			for (const Vec2& pixel : row) {
				if (pixel.x > 0.5f && pixel.x <= 0.5f + groundTexel) {
					base = Math::Max(base, pixel.y);
				}
			}
			const f32 crossing = HalfShadowX(row, 0.9f);
			ASSERT_FALSE(std::isnan(crossing)) << "width " << width;
			shift += crossing - farEdge;
			++rows;
		}
		ASSERT_GT(rows, 50u);
		const f32 meanShift = static_cast<f32>(shift / rows) / groundTexel;
		EXPECT_LT(base, 0.5f) << "width " << width;
		EXPECT_LT(Math::Abs(meanShift), 1.0f) << "width " << width << ' ' << meanShift;
		if (width == 1) {
			firstShift = meanShift;
		}
		EXPECT_NEAR(meanShift, firstShift, 0.1f) << "width " << width;
	}
}

namespace {

	class GraphicsErrorSink : public ILogSink {
	public:
		static constexpr const char* SENTINEL = "shadow-log-gate-sentinel";

		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard<std::mutex> lock(_mutex);
			if (event.message.find(SENTINEL) != str::npos) {
				_armed = true;
				return;
			}
			if (!_armed) {
				return;
			}
			const bool vulkanWarning = event.category == "Vulkan" && event.level >= LogLevel::Warn;
			if (event.level < LogLevel::Error && !vulkanWarning) {
				return;
			}
			_messages.push_back(str(event.message));
		}
		void Flush() override {}

		bool Armed() const {
			std::lock_guard<std::mutex> lock(_mutex);
			return _armed;
		}

		str Joined() const {
			std::lock_guard<std::mutex> lock(_mutex);
			str all;
			for (const str& message : _messages) {
				all += message;
				all += '\n';
			}
			return all;
		}
	private:
		mutable std::mutex _mutex;
		std::vector<str> _messages;
		bool _armed = false;
	};

} // anonymous namespace

TEST_F(ShadowViewFixture, AShadowedFrameLogsNoGraphicsError) {
	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<GraphicsErrorSink>();
	LogManager::AddSink(sink);
	bool armed = false;
	for (int attempt = 0; attempt < 200 && !sink->Armed(); ++attempt) {
		SLT_LOG_INFO("Graphics", "{}", GraphicsErrorSink::SENTINEL);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	armed = sink->Armed();

	InitializeWith();
	bool available = RenderSystem::IsRenderingAvailable();
	u32 shadowLights = 0;
	u32 shadowMaps = 0;
	u32 punctualOnlyMaps = 0;
	if (available && _mounted) {
		Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
		if (meshAsset) {
			Handle<Scene> scene = SceneSystem::CreateScene("Shadows");
			Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));
			Handle<Light> sun = BuildOccludedScene(scene, cube);
			Handle<SceneObject> spotObject = SceneSystem::CreateObject("Spot", scene);
			spotObject->SetLocalPosition(Vec3(-4.3f, 6.1f, 2.7f));
			spotObject->SetLocalRotation(Quat::LookRotation(Vec3(0.31f, -0.88f, -0.36f).Normalized()));
			Handle<Light> spot = spotObject->AddComponent<Light>();
			spot->SetLightType(LightType::Spot);
			spot->SetRange(40.0f);
			spot->SetIntensity(300.0f);

			Handle<SceneObject> pointObject = SceneSystem::CreateObject("Point", scene);
			pointObject->SetLocalPosition(Vec3(3.7f, 4.2f, -2.1f));
			Handle<Light> point = pointObject->AddComponent<Light>();
			point->SetLightType(LightType::Point);
			point->SetRange(35.0f);
			point->SetIntensity(300.0f);
			SceneSystem::EndFrame();
			ViewTargetId target = RenderSystem::CreateViewTarget({96, 96});
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			for (int frame = 0; frame < 3; ++frame) {
				RenderTo(target, pixels, size);
			}
			shadowLights = RenderSystem::GetStats().shadowLights;
			shadowMaps = RenderSystem::GetStats().shadowMapsRendered;

			sun->SetCastShadows(false);
			for (int frame = 0; frame < 3; ++frame) {
				RenderTo(target, pixels, size);
			}
			punctualOnlyMaps = RenderSystem::GetStats().shadowMapsRendered;
			RenderSystem::DestroyViewTarget(target);
			SceneSystem::UnloadScene(scene);
		}
	}

	LogManager::Terminate();
	str captured = sink->Joined();
	sink.reset();
	JobSystem::Terminate();

	if (!available) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(armed);
	EXPECT_EQ(shadowLights, 3u);
	EXPECT_EQ(shadowMaps, 11u);
	EXPECT_EQ(punctualOnlyMaps, 7u);
	EXPECT_TRUE(captured.empty()) << captured;
}

TEST_F(CreatedShaderFixture, ResourcelessShaderDraws) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Guid shaderGuid = AssetDatabase::CreateAsset(Path("project://Blockless.hlsl"), StringHash("ShaderAsset"));
	Guid materialGuid = AssetDatabase::CreateAsset(Path("project://Blockless.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(shaderGuid.IsValid());
	ASSERT_TRUE(materialGuid.IsValid());
	Asset* shaderAsset = AssetDatabase::LoadAssetByGuid(shaderGuid);
	ASSERT_NE(shaderAsset, nullptr);
	ShaderAsset* shader = static_cast<ShaderAsset*>(shaderAsset);
	if (shader->variants.empty()) {
		AssetDatabase::UnloadAsset(shaderAsset);
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}

	FileSystem::WriteTextFile(Path("project://Blockless.hlsl"),
							  str("#pragma shalltear_template lit\n"
								  "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
								  "\tsurface.baseColor = float3(0.0, 0.0, 0.0);\n"
								  "\tsurface.emissive = float3(0.0, 0.9, 0.0);\n"
								  "}\n"));
	ASSERT_TRUE(shader->Import(FileSystem::Resolve(Path("project://Blockless.hlsl"))));
	EXPECT_EQ(shader->GetMaterialParamLayout(), nullptr);

	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(materialGuid);
	ASSERT_NE(materialAsset, nullptr);
	MaterialAsset* material = static_cast<MaterialAsset*>(materialAsset);
	material->shaderGuid = shaderGuid;
	material->shader = Handle<ShaderAsset>(shader);
	Vector<str> textureSlots{GetMemoryResource(MemTag::Asset)};
	shader->GetMaterialTextureSlots(textureSlots);
	EXPECT_TRUE(textureSlots.empty());
	material->SyncToLayout(shader->GetMaterialParamLayout(), {textureSlots.data(), textureSlots.size()});

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<Scene> scene = SceneSystem::CreateScene("Blockless");
	Handle<SceneObject> object = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(meshAsset)));
	renderer->SetMaterial(0, Handle<MaterialAsset>(material));
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc;
	desc.view = LookForwardView(Vec3(0.0f, 0.0f, -3.0f));
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = Vec3(0.0f, 0.0f, -3.0f);
	desc.clearColor = Color::Black();
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RGBA brightest = BrightestTexel(pixels, size);
	EXPECT_GT(brightest.g, 100u);
	EXPECT_GT(brightest.g, brightest.r * 2u);
	EXPECT_GT(brightest.g, brightest.b * 2u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(shaderAsset);
}

namespace {

	struct ShadowDelta {
		u32 darker = 0;
		u32 brighter = 0;
		u32 identical = 0;
	};

	ShadowDelta CompareFrames(const Vector<u8>& withShadow, const Vector<u8>& without, Vec2I size) {
		ShadowDelta delta;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				const u32 shadowed = Luma(TexelAt(withShadow, size, x, y));
				const u32 lit = Luma(TexelAt(without, size, x, y));
				if (shadowed + 3 < lit) {
					++delta.darker;
				} else if (shadowed > lit + 3) {
					++delta.brighter;
				} else {
					++delta.identical;
				}
			}
		}
		return delta;
	}

} // anonymous namespace

TEST_F(ShadowViewFixture, SpotOccluderOnlyDarkens) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));

	const Vec3 axis = Vec3(0.43f, -0.72f, 0.55f).Normalized();
	Handle<Scene> scene = SceneSystem::CreateScene("SpotShadows");
	Handle<Light> spot = BuildPunctualRig(scene, cube, axis, LightType::Spot);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const Vec3 eye = axis * 2.0f + PerpendicularTo(axis) * 8.0f;
	const Vec3 lookAt = axis * 7.5f;

	Vector<u8> shadowed{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> unshadowed{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> again{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderFrom(target, eye, lookAt, shadowed, size);
	if (RenderSystem::GetStats().shadowLights == 0) {
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}
	EXPECT_EQ(RenderSystem::GetStats().shadowLights, 1u);
	EXPECT_EQ(RenderSystem::GetStats().shadowMapsRendered, 1u);
	EXPECT_EQ(RenderSystem::GetStats().shadowLightsDropped, 0u);

	spot->SetCastShadows(false);
	RenderFrom(target, eye, lookAt, unshadowed, size);
	EXPECT_EQ(RenderSystem::GetStats().shadowMapsRendered, 0u);
	spot->SetCastShadows(true);
	RenderFrom(target, eye, lookAt, again, size);

	ASSERT_EQ(shadowed.size(), unshadowed.size());
	const ShadowDelta delta = CompareFrames(shadowed, unshadowed, size);
	EXPECT_GT(delta.darker, 50u);
	EXPECT_EQ(delta.brighter, 0u) << delta.brighter;
	EXPECT_GT(delta.identical, 100u);
	EXPECT_EQ(shadowed, again);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}

TEST_F(ShadowViewFixture, PointOccluderDarkensAllFaces) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));

	const std::array<Vec3, 6> directions{
		Vec3(1.0f, 0.21f, -0.17f), Vec3(-1.0f, -0.13f, 0.24f),
		Vec3(0.19f, 1.0f, 0.23f), Vec3(-0.22f, -1.0f, 0.16f),
		Vec3(0.18f, -0.24f, 1.0f), Vec3(-0.15f, 0.2f, -1.0f)};

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	bool skipped = false;
	for (u32 i = 0; i < directions.size(); ++i) {
		const Vec3 axis = directions[i].Normalized();
		Handle<Scene> scene = SceneSystem::CreateScene("PointShadows");
		Handle<Light> point = BuildPunctualRig(scene, cube, axis, LightType::Point);
		SceneSystem::EndFrame();

		const Vec3 eye = axis * 2.0f + PerpendicularTo(axis) * 8.0f;
		const Vec3 lookAt = axis * 7.5f;
		Vector<u8> shadowed{GetMemoryResource(MemTag::Graphics)};
		Vector<u8> unshadowed{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		RenderFrom(target, eye, lookAt, shadowed, size);
		if (RenderSystem::GetStats().shadowLights == 0) {
			SceneSystem::UnloadScene(scene);
			skipped = true;
			break;
		}
		EXPECT_EQ(RenderSystem::GetStats().shadowMapsRendered, 6u) << "direction " << i;

		point->SetCastShadows(false);
		RenderFrom(target, eye, lookAt, unshadowed, size);
		SceneSystem::UnloadScene(scene);

		ASSERT_EQ(shadowed.size(), unshadowed.size());
		const ShadowDelta delta = CompareFrames(shadowed, unshadowed, size);
		EXPECT_GT(delta.darker, 50u);
		EXPECT_EQ(delta.brighter, 0u) << "face " << i;
		EXPECT_GT(delta.identical, 100u) << "face " << i << " changed everywhere";
	}
	RenderSystem::DestroyViewTarget(target);
	if (skipped) {
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}
}

TEST_F(ShadowViewFixture, FullAtlasDropsLeastImportant) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));

	Handle<Scene> scene = SceneSystem::CreateScene("Crowded");
	Handle<SceneObject> ground = SceneSystem::CreateObject("Ground", scene);
	ground->SetLocalPosition(Vec3(0.3f, -2.0f, 0.7f));
	ground->SetLocalScale(Vec3(60.0f, 0.5f, 60.0f));
	ground->AddComponent<MeshRenderer>()->SetMesh(cube);

	constexpr u32 LIGHT_COUNT = 5;
	Vector<Handle<Light>> lights{GetMemoryResource(MemTag::Graphics)};
	for (u32 i = 0; i < LIGHT_COUNT; ++i) {
		const f32 x = -14.0f + static_cast<f32>(i) * 7.0f;
		Handle<SceneObject> lightObject = SceneSystem::CreateObject("Spot", scene);
		lightObject->SetLocalPosition(Vec3(x, 7.0f, 0.4f));
		lightObject->SetLocalRotation(Quat::LookRotation(Vec3(0.06f, -1.0f, 0.09f).Normalized()));
		Handle<Light> light = lightObject->AddComponent<Light>();
		light->SetLightType(LightType::Spot);
		light->SetSpotAngleDeg(60.0f);
		light->SetRange(70.0f);
		light->SetIntensity(200.0f + static_cast<f32>(i) * 200.0f);
		lights.push_back(light);

		Handle<SceneObject> blocker = SceneSystem::CreateObject("Blocker", scene);
		blocker->SetLocalPosition(Vec3(x + 0.3f, 3.0f, 0.6f));
		blocker->SetLocalScale(Vec3(1.4f, 1.4f, 1.4f));
		blocker->AddComponent<MeshRenderer>()->SetMesh(cube);
	}
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({160, 160});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const Vec3 eye(1.0f, 9.0f, -21.0f);
	const Vec3 lookAt(0.0f, -1.5f, 0.5f);

	Vector<u8> all{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> withoutDimmest{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> withoutBrightest{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderFrom(target, eye, lookAt, all, size);
	GraphicsStats stats = RenderSystem::GetStats();
	if (stats.shadowLights == 0) {
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}
	EXPECT_EQ(stats.shadowLights, LIGHT_COUNT);
	EXPECT_EQ(stats.shadowLightsDropped, 1u);
	EXPECT_EQ(stats.shadowMapsRendered, LIGHT_COUNT - 1);

	lights[0]->SetCastShadows(false);
	RenderFrom(target, eye, lookAt, withoutDimmest, size);
	EXPECT_EQ(RenderSystem::GetStats().shadowLightsDropped, 0u);
	EXPECT_EQ(all, withoutDimmest);
	lights[0]->SetCastShadows(true);

	lights[LIGHT_COUNT - 1]->SetCastShadows(false);
	RenderFrom(target, eye, lookAt, withoutBrightest, size);
	EXPECT_NE(all, withoutBrightest);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}

TEST_F(ShadowViewFixture, TransparentReceiveSettingApplies) {
	Vector<u8> receiving{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> notReceiving{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	bool available = true;
	bool imported = true;

	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	Asset* shaderAsset = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	if (!meshAsset || !shaderAsset || static_cast<ShaderAsset*>(shaderAsset)->variants.empty()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));

	Guid materialGuid = AssetDatabase::CreateAsset(Path("project://Assets/Blended.mat"), StringHash("MaterialAsset"));
	MaterialAsset* blended = static_cast<MaterialAsset*>(AssetDatabase::LoadAssetByGuid(materialGuid));
	ASSERT_NE(blended, nullptr);
	blended->shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(shaderAsset));
	blended->blend = RHI::BlendMode::Alpha;

	Handle<Scene> scene = SceneSystem::CreateScene("TransparentReceive");
	Handle<SceneObject> ground = SceneSystem::CreateObject("Ground", scene);
	ground->SetLocalPosition(Vec3(0.3f, -1.0f, 0.7f));
	ground->SetLocalScale(Vec3(40.0f, 0.4f, 40.0f));
	MeshRenderer* groundRenderer = ground->AddComponent<MeshRenderer>().Get();
	groundRenderer->SetMesh(cube);
	groundRenderer->SetMaterial(0, Handle<MaterialAsset>(blended));

	Handle<SceneObject> block = SceneSystem::CreateObject("Occluder", scene);
	block->SetLocalPosition(Vec3(1.1f, 3.4f, -0.6f));
	block->SetLocalScale(Vec3(3.0f, 3.0f, 3.0f));
	block->AddComponent<MeshRenderer>()->SetMesh(cube);

	Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", scene);
	sun->SetLocalRotation(Quat::FromEuler(Vec3(52.0f, 37.0f, 11.0f)));
	sun->AddComponent<Light>()->SetIntensity(2.0f);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	auto renderUnder = [&](bool receive, Vector<u8>& out) {
		UseProfile([receive](GraphicsProfile& profile) { profile.transparentReceiveShadows = receive; });
		RenderTo(target, out, size);
		if (RenderSystem::GetStats().shadowLights == 0) {
			imported = false;
		}
	};

	renderUnder(true, receiving);
	renderUnder(false, notReceiving);
	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	(void)available;
	if (!imported) {
		GTEST_SKIP();
	}
	ASSERT_EQ(receiving.size(), notReceiving.size());

	const ShadowDelta delta = CompareFrames(receiving, notReceiving, size);
	EXPECT_GT(delta.darker, 50u);
	EXPECT_EQ(delta.brighter, 0u);
	EXPECT_GT(delta.identical, 100u);
}

TEST_F(ShadowViewFixture, CascadeFitKeptWhileAsked) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));
	Handle<Scene> scene = SceneSystem::CreateScene("CascadeFit");
	Handle<Light> sun = BuildOccludedScene(scene, cube);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({96, 96});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	Mat4 first;
	SubmitFrom(target, Vec3(-6.0f, 11.0f, -15.0f), false);
	RenderSystem::RenderAll();
	if (RenderSystem::GetStats().shadowLights == 0) {
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}
	EXPECT_EQ(FitOf(target, first), 0u);

	SubmitFrom(target, Vec3(-6.0f, 11.0f, -15.0f), true);
	RenderSystem::RenderAll();
	const u32 kept = FitOf(target, first);
	EXPECT_EQ(kept, 4u) << kept << " were kept";

	SubmitFrom(target, Vec3(-6.0f, 11.0f, -15.0f), false);
	RenderSystem::RenderAll();
	EXPECT_EQ(FitOf(target, first), 0u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}

TEST_F(ShadowViewFixture, ViewsKeepSeparateCascadeFits) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));
	Handle<Scene> scene = SceneSystem::CreateScene("CascadeFitTwice");
	BuildOccludedScene(scene, cube);
	SceneSystem::EndFrame();

	ViewTargetId near = RenderSystem::CreateViewTarget({96, 96});
	ViewTargetId far = RenderSystem::CreateViewTarget({96, 96});
	ASSERT_NE(near, INVALID_VIEW_TARGET);
	ASSERT_NE(far, INVALID_VIEW_TARGET);

	SubmitFrom(near, Vec3(-6.0f, 11.0f, -15.0f), true);
	SubmitFrom(far, Vec3(70.0f, 40.0f, 90.0f), true);
	RenderSystem::RenderAll();
	if (RenderSystem::GetStats().shadowLights == 0) {
		RenderSystem::DestroyViewTarget(near);
		RenderSystem::DestroyViewTarget(far);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}

	Mat4 nearFit;
	Mat4 farFit;
	ASSERT_EQ(FitOf(near, nearFit), 4u);
	ASSERT_EQ(FitOf(far, farFit), 4u);
	EXPECT_NE(nearFit, farFit);

	RenderSystem::DestroyViewTarget(near);
	RenderSystem::DestroyViewTarget(far);
	SceneSystem::UnloadScene(scene);
}

TEST_F(ShadowViewFixture, CasterlessViewDropsFit) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));
	Handle<Scene> scene = SceneSystem::CreateScene("CascadeFitNoCaster");
	Handle<Light> sun = BuildOccludedScene(scene, cube);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({96, 96});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	Mat4 fit;
	SubmitFrom(target, Vec3(-6.0f, 11.0f, -15.0f), true);
	RenderSystem::RenderAll();
	if (FitOf(target, fit) == 0u) {
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}

	sun->SetCastShadows(false);
	SubmitFrom(target, Vec3(-6.0f, 11.0f, -15.0f), true);
	RenderSystem::RenderAll();
	EXPECT_EQ(FitOf(target, fit), 0u);

	sun->SetCastShadows(true);
	SubmitFrom(target, Vec3(-6.0f, 11.0f, -15.0f), true);
	RenderSystem::RenderAll();
	EXPECT_EQ(FitOf(target, fit), 4u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}

TEST_F(ShadowViewFixture, FrozenFitStaysPut) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));
	Handle<Scene> scene = SceneSystem::CreateScene("FrozenFit");
	BuildOccludedScene(scene, cube);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({96, 96});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	const Vec3 FIT_EYE(-6.0f, 11.0f, -15.0f);
	Mat4 frozenFirst;
	SubmitFrozen(target, FIT_EYE, FIT_EYE);
	RenderSystem::RenderAll();
	if (FitOf(target, frozenFirst) == 0u) {
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}

	Mat4 frozenAfterFlight;
	SubmitFrozen(target, Vec3(70.0f, 40.0f, 90.0f), FIT_EYE);
	RenderSystem::RenderAll();
	ASSERT_EQ(FitOf(target, frozenAfterFlight), 4u);
	EXPECT_EQ(frozenFirst, frozenAfterFlight);

	Mat4 unfrozen;
	SubmitFrom(target, Vec3(70.0f, 40.0f, 90.0f), true);
	RenderSystem::RenderAll();
	ASSERT_EQ(FitOf(target, unfrozen), 4u);
	EXPECT_NE(unfrozen, frozenFirst);

	Mat4 again;
	SubmitFrozen(target, Vec3(70.0f, 40.0f, 90.0f), FIT_EYE);
	RenderSystem::RenderAll();
	ASSERT_EQ(FitOf(target, again), 4u);
	EXPECT_EQ(again, frozenFirst);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}

TEST_F(ShadowViewFixture, FreezeIsPerView) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));
	Handle<Scene> scene = SceneSystem::CreateScene("FrozenBeside");
	BuildOccludedScene(scene, cube);
	SceneSystem::EndFrame();

	ViewTargetId frozen = RenderSystem::CreateViewTarget({96, 96});
	ViewTargetId live = RenderSystem::CreateViewTarget({96, 96});
	ASSERT_NE(frozen, INVALID_VIEW_TARGET);
	ASSERT_NE(live, INVALID_VIEW_TARGET);

	const Vec3 PINNED(-6.0f, 11.0f, -15.0f);
	const Vec3 FLOWN(70.0f, 40.0f, 90.0f);

	SubmitFrozen(frozen, FLOWN, PINNED);
	SubmitFrom(live, FLOWN, true);
	RenderSystem::RenderAll();
	Mat4 frozenFit;
	Mat4 liveFit;
	if (FitOf(frozen, frozenFit) == 0u) {
		RenderSystem::DestroyViewTarget(frozen);
		RenderSystem::DestroyViewTarget(live);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}
	ASSERT_EQ(FitOf(live, liveFit), 4u);
	EXPECT_NE(frozenFit, liveFit);

	Mat4 aloneFit;
	SubmitFrom(live, FLOWN, true);
	RenderSystem::RenderAll();
	ASSERT_EQ(FitOf(live, aloneFit), 4u);
	EXPECT_EQ(aloneFit, liveFit);

	RenderSystem::DestroyViewTarget(frozen);
	RenderSystem::DestroyViewTarget(live);
	SceneSystem::UnloadScene(scene);
}

TEST_F(ShadowViewFixture, HalfFitPairRefused) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));
	Handle<Scene> scene = SceneSystem::CreateScene("HalfFit");
	BuildOccludedScene(scene, cube);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({96, 96});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	const Vec3 FLOWN(70.0f, 40.0f, 90.0f);
	Mat4 unfrozen;
	SubmitFrom(target, FLOWN, true);
	RenderSystem::RenderAll();
	if (FitOf(target, unfrozen) == 0u) {
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}

	const Mat4 halfView = Mat4::LookAt(Vec3(-6.0f, 11.0f, -15.0f), Vec3::Zero());
	ViewDesc desc;
	desc.view = Mat4::LookAt(FLOWN, Vec3::Zero());
	desc.projection = Mat4::PerspectiveReverseZ(55.0f, 1.0f, 0.1f, 500.0f);
	desc.cameraPosition = FLOWN;
	desc.clearColor = Color::Black();
	desc.retainCascadeFit = true;
	desc.shadowFitView = &halfView;
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Mat4 half;
	ASSERT_EQ(FitOf(target, half), 4u);
	EXPECT_EQ(half, unfrozen);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}

namespace {

	struct QuadrantDelta {
		u32 topLeft = 0;
		u32 topRight = 0;
		u32 bottomLeft = 0;
		u32 bottomRight = 0;
		u32 Total() const { return topLeft + topRight + bottomLeft + bottomRight; }
	};

	QuadrantDelta CompareQuadrants(const Vector<u8>& a, const Vector<u8>& b, Vec2I size) {
		QuadrantDelta delta;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				const RGBA left = TexelAt(a, size, x, y);
				const RGBA right = TexelAt(b, size, x, y);
				if (left.r == right.r && left.g == right.g && left.b == right.b) {
					continue;
				}
				const bool top = y < size.y / 2;
				const bool leftHalf = x < size.x / 2;
				++(top ? (leftHalf ? delta.topLeft : delta.topRight)
					   : (leftHalf ? delta.bottomLeft : delta.bottomRight));
			}
		}
		return delta;
	}

} // anonymous namespace

TEST_F(ShadowViewFixture, AtlasOverlayStaysInCorner) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));
	Handle<Scene> scene = SceneSystem::CreateScene("AtlasOverlay");
	BuildOccludedScene(scene, cube);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	Vector<u8> plain{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> overlaid{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderTo(target, plain, size);
	if (RenderSystem::GetStats().shadowLights == 0) {
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}

	SubmitOverlay(target, DebugOverlay::DirectionalAtlas);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, overlaid, size));

	const QuadrantDelta delta = CompareQuadrants(plain, overlaid, size);
	EXPECT_GT(delta.Total(), 100u);
	EXPECT_EQ(delta.bottomRight, 0u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}

TEST_F(ShadowViewFixture, TheTwoAtlasOverlaysShowDifferentImages) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));
	Handle<Scene> scene = SceneSystem::CreateScene("AtlasOverlayModes");
	BuildOccludedScene(scene, cube);
	BuildPunctualRig(scene, cube, Vec3(0.0f, -1.0f, 0.0f), LightType::Spot);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	Vector<u8> directional{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> punctual{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> plain{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderTo(target, plain, size);
	if (RenderSystem::GetStats().shadowLights == 0) {
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}

	SubmitOverlay(target, DebugOverlay::DirectionalAtlas);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, directional, size));
	SubmitOverlay(target, DebugOverlay::PunctualAtlas);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, punctual, size));

	EXPECT_GT(CompareQuadrants(plain, directional, size).Total(), 100u);
	EXPECT_GT(CompareQuadrants(plain, punctual, size).Total(), 100u);
	EXPECT_GT(CompareQuadrants(directional, punctual, size).Total(), 0u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}

TEST_F(ShadowViewFixture, AtlasOverlaySurvivesDebugViewUnderIt) {
	InitializeWith();
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_mounted);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(meshAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(meshAsset));
	Handle<Scene> scene = SceneSystem::CreateScene("AtlasOverlayWithMode");
	BuildOccludedScene(scene, cube);
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	Vector<u8> tintOnly{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> tintAndAtlas{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	SubmitOverlay(target, DebugOverlay::None, DebugView::Cascades);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, tintOnly, size));
	if (RenderSystem::GetStats().shadowLights == 0) {
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		GTEST_SKIP() << "no shadowing light extracted (dxcompiler.dll unavailable?)";
	}

	SubmitOverlay(target, DebugOverlay::DirectionalAtlas, DebugView::Cascades);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, tintAndAtlas, size));

	const QuadrantDelta delta = CompareQuadrants(tintOnly, tintAndAtlas, size);
	EXPECT_GT(delta.Total(), 100u);
	EXPECT_EQ(delta.bottomRight, 0u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
}
