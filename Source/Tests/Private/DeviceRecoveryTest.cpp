#include "TestsPCH.h"

#include "DeviceLossHarness.h"
#include "ProductLookup.h"

#include "Graphics/PresentPolicy.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/SkinnedMeshRenderer.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/Window.h"

#include <atomic>
#include <cstring>

using namespace Shalltear;
using DeviceLossTest::LogSink;
using DeviceLossTest::ScopedLog;
using DeviceLossTest::ScopedMissingDriver;

namespace {

	class DeviceEventCounter : public Object {
	public:
		u32 restored = 0;
		bool lostWhenRestored = true;

		DeviceEventCounter() {
			GraphicsEvents::DeviceRestored.Subscribe(EventBus::GetGlobal(), Handle<DeviceEventCounter>(this), &DeviceEventCounter::OnRestored);
		}
		~DeviceEventCounter() override { EventBus::GetGlobal().UnsubscribeAll(GetInstanceId()); }

		void OnRestored(const GraphicsDeviceRestoredEvent&) {
			++restored;
			lostWhenRestored = RenderSystem::GetDevice()->IsLost();
		}
	};

	class DeviceRecoveryFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		Vector<MeshAsset*> _ownedMeshes{GetMemoryResource(MemTag::Asset)};

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_recovery_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = DeviceLossTest::RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
			}

			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			for (MeshAsset* mesh : _ownedMeshes) {
				mesh->NotifyUnloaded();
				Delete(mesh);
			}
			UISystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			FileSystem::Unmount("engine");
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool StageFixture(const char* name) {
			std::filesystem::path fixtures = DeviceLossTest::RepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / name, _root / name, std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}

		Handle<Window> HiddenWindow(Vec2I size) {
			WindowDesc desc;
			desc.title = "DeviceRecoveryTest";
			desc.clientSize = size;
			desc.flags = WindowFlags::Hidden;
			return WindowSystem::Create(desc);
		}

		MeshAsset* MakeMorphedQuad() {
			MeshAsset* mesh = New<MeshAsset>(MemTag::Asset);
			const Vec3 corners[4] = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.5f, 0.5f, 0.0f}, {-0.5f, 0.5f, 0.0f}};
			for (const Vec3& corner : corners) {
				MeshVertex vertex;
				vertex.position = corner;
				vertex.normal = Vec3(0.0f, 0.0f, -1.0f);
				vertex.tangent = Vec3(1.0f, 0.0f, 0.0f);
				mesh->vertices.push_back(vertex);
				mesh->bounds.Encapsulate(corner);
			}
			mesh->indices.assign({0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2});
			SubMesh whole;
			whole.indexCount = static_cast<u32>(mesh->indices.size());
			whole.bounds = mesh->bounds;
			mesh->submeshes.push_back(whole);
			MeshBlendShape shape;
			shape.name = "Stretch";
			MeshBlendShapeFrame frame;
			for (u32 vertex : {2u, 3u}) {
				frame.vertices.push_back(vertex);
				frame.positionDeltas.push_back(Vec3(0.0f, 0.75f, 0.0f));
			}
			shape.frames.push_back(std::move(frame));
			mesh->blendShapes.push_back(std::move(shape));
			mesh->NotifyLoaded();
			_ownedMeshes.push_back(mesh);
			return mesh;
		}
	};

	struct Frame {
		Vector<u8> window{GetMemoryResource(MemTag::Graphics)};
		Vector<u8> view{GetMemoryResource(MemTag::Graphics)};
		Vec2I windowSize{};
		Vec2I viewSize{};
	};

	u64 DifferingBytes(const Vector<u8>& a, const Vector<u8>& b) {
		if (a.size() != b.size()) {
			return ~0ull;
		}
		u64 differing = 0;
		for (size_t i = 0; i < a.size(); ++i) {
			differing += a[i] != b[i] ? 1u : 0u;
		}
		return differing;
	}

	u32 DistinctTexels(const Vector<u8>& rgba8) {
		HashSet<u32> distinct{GetMemoryResource(MemTag::Graphics)};
		const u32* words = reinterpret_cast<const u32*>(rgba8.data());
		for (size_t i = 0, n = rgba8.size() / 4; i < n; ++i) {
			distinct.insert(words[i]);
		}
		return static_cast<u32>(distinct.size());
	}

	Mat4 ViewFrom(Vec3 eye) {
		return Mat4::TRS(eye, Quat::Identity(), Vec3::One()).Inverse();
	}

} // anonymous namespace

class DeviceRecoverySceneTest : public DeviceRecoveryFixture, public ::testing::WithParamInterface<SkyMode> {};

TEST_P(DeviceRecoverySceneTest, FrameMatchesAfterEachRebuild) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	RHI::BufferDesc ballastDesc;
	ballastDesc.size = 3ull * 1024 * 1024 + 4096;
	ballastDesc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::DeviceAddress;
	RHI::BufferHandle ballast = device->CreateBufferResource(ballastDesc);
	ASSERT_TRUE(ballast.IsValid());
	ASSERT_TRUE(StageFixture("cube.glb"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://cube.glb"));
	if (!model) {
		GTEST_SKIP() << "cube.glb import failed (dxcompiler.dll missing?)";
	}
	AssetDatabase::UnloadAsset(model);
	MeshAsset* texturedMesh = static_cast<MeshAsset*>(ProductLookup::Load(Path("project://cube.glb"), "Meshes/Cube"));
	ASSERT_NE(texturedMesh, nullptr);
	ASSERT_FALSE(texturedMesh->defaultMaterials.empty());
	MaterialAsset* texturedMaterial = static_cast<MaterialAsset*>(AssetDatabase::LoadAssetByGuid(texturedMesh->defaultMaterials[0]));
	MeshAsset* plainMesh = static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive")));
	MaterialAsset* plainMaterial = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat")));
	ASSERT_NE(texturedMaterial, nullptr);
	ASSERT_NE(plainMesh, nullptr);
	ASSERT_NE(plainMaterial, nullptr);

	Handle<Window> window = HiddenWindow({256, 256});
	ASSERT_TRUE(window.IsValid());
	Handle<Scene> scene = SceneSystem::CreateScene("Recovery");
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	cameraObject->SetLocalPosition(Vec3(0.0f, 0.4f, -4.0f));
	Handle<Camera> camera = cameraObject->AddComponent<Camera>();
	camera->SetTargetWindow(window);
	camera->SetClearColor(Color(0.1f, 0.2f, 0.3f, 1.0f));

	Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", scene);
	sun->SetLocalRotation(Quat::FromEuler(Vec3(50.0f, -30.0f, 0.0f)));
	sun->AddComponent<Light>()->SetIntensity(2.0f);

	Handle<Environment> environment = SceneSystem::CreateObject("Sky", scene)->AddComponent<Environment>();
	environment->SetSkyMode(GetParam());
	MaterialAsset* skyMaterial = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("engine://Materials/DefaultSky.mat")));
	ASSERT_NE(skyMaterial, nullptr);
	environment->SetSkyMaterial(Handle<MaterialAsset>(skyMaterial));

	Handle<SceneObject> cube = SceneSystem::CreateObject("Cube", scene);
	cube->SetLocalRotation(Quat::FromEuler(Vec3(20.0f, 35.0f, 0.0f)));
	Handle<MeshRenderer> cubeRenderer = cube->AddComponent<MeshRenderer>();
	cubeRenderer->SetMesh(Handle<MeshAsset>(texturedMesh));
	cubeRenderer->SetMaterial(0, Handle<MaterialAsset>(texturedMaterial));

	Handle<SceneObject> ground = SceneSystem::CreateObject("Ground", scene);
	ground->SetLocalPosition(Vec3(0.0f, -1.2f, 0.0f));
	ground->SetLocalScale(Vec3(8.0f, 0.2f, 8.0f));
	Handle<MeshRenderer> groundRenderer = ground->AddComponent<MeshRenderer>();
	groundRenderer->SetMesh(Handle<MeshAsset>(plainMesh));
	groundRenderer->SetMaterial(0, Handle<MaterialAsset>(plainMaterial));

	MeshAsset* morphMesh = MakeMorphedQuad();
	Handle<SceneObject> banner = SceneSystem::CreateObject("Banner", scene);
	banner->SetLocalPosition(Vec3(-1.6f, 0.0f, 0.0f));
	Handle<SkinnedMeshRenderer> bannerRenderer = banner->AddComponent<SkinnedMeshRenderer>();
	bannerRenderer->SetMesh(Handle<MeshAsset>(morphMesh));
	bannerRenderer->SetMaterial(0, Handle<MaterialAsset>(plainMaterial));
	bannerRenderer->SetBlendShapeWeightCount(1);
	bannerRenderer->SetBlendShapeWeight(0, 100.0f);

	const ViewTargetId target = RenderSystem::CreateViewTarget({96, 96});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const RHI::TextureHandle targetTexture = RenderSystem::GetViewTargetTexture(target);

	auto render = [&](int frames) {
		for (int frame = 0; frame < frames; ++frame) {
			WindowSystem::PumpEvents();
			SceneSystem::UpdateAll();
			SceneSystem::EndFrame();
			ViewDesc desc;
			desc.view = ViewFrom(Vec3(1.0f, 0.5f, -5.0f));
			desc.projection = Mat4::PerspectiveReverseZ(55.0f, 1.0f, 0.1f, 100.0f);
			desc.cameraPosition = Vec3(1.0f, 0.5f, -5.0f);
			desc.clearColor = Color(0.3f, 0.1f, 0.2f, 1.0f);
			RenderSystem::SubmitView(target, desc);
			RenderSystem::RenderAll();
		}
	};
	auto capture = [&](Frame& out) {
		return RenderSystem::ReadbackWindowSceneColor(window, out.window, out.windowSize) &&
			   RenderSystem::ReadbackViewTarget(target, out.view, out.viewSize);
	};

	constexpr int SETTLE_FRAMES = 8;
	render(SETTLE_FRAMES);
	Frame before;
	ASSERT_TRUE(capture(before));
	ASSERT_GT(DistinctTexels(before.view), 16u);
	ASSERT_EQ(GraphicsInternal::GetLastFramePresents().windows, 1u);
	const u32 drawsBefore = RenderSystem::GetStats().drawCalls;
	ASSERT_GT(drawsBefore, 3u);

	DeviceEventCounter events;
	ScopedLog log;
	constexpr u32 ROUNDS = 3;
	for (u32 round = 0; round < ROUNDS; ++round) {
		const u64 addressBefore = texturedMesh->vertexBufferAddress;
		device->SimulateLoss();
		ASSERT_TRUE(device->IsLost());
		if (ballast.IsValid()) {
			device->DestroyResource(ballast);
			ballast = {};
		}
		render(1);
		ASSERT_FALSE(device->IsLost()) << round;
		if (round == 0) {
			ASSERT_NE(texturedMesh->vertexBufferAddress, addressBefore);
		}
		EXPECT_EQ(events.restored, round + 1);
		EXPECT_FALSE(events.lostWhenRestored);
		EXPECT_EQ(GraphicsInternal::GetLastFramePresents().windows, 1u) << round;
		EXPECT_TRUE(RenderSystem::IsViewTargetDrawn(target));
		EXPECT_EQ(RenderSystem::GetViewTargetTexture(target), targetTexture);

		render(SETTLE_FRAMES - 1);
		Frame after;
		ASSERT_TRUE(capture(after));
		EXPECT_EQ(DifferingBytes(before.window, after.window), 0u) << round;
		EXPECT_EQ(DifferingBytes(before.view, after.view), 0u) << round;
		EXPECT_EQ(RenderSystem::GetStats().drawCalls, drawsBefore) << round;
	}

	EXPECT_EQ(texturedMesh->vertexBufferAddress, device->GetBufferDeviceAddress(texturedMesh->vertexBuffer));
	EXPECT_EQ(texturedMesh->indexBufferAddress, device->GetBufferDeviceAddress(texturedMesh->indexBuffer));
	EXPECT_NE(morphMesh->vertexBufferAddress, 0u);

	const LogSink& seen = log.Delivered();
	EXPECT_EQ(seen.Count("device loss simulated"), ROUNDS);
	EXPECT_EQ(seen.Count("device rebuilt"), ROUNDS);
	EXPECT_EQ(seen.CountAtLeast(LogLevel::Error), 0u) << seen.Joined();
	EXPECT_EQ(seen.Count("stale"), 0u) << seen.Joined();
	EXPECT_EQ(seen.Count("from before device rebuild"), 0u) << seen.Joined();

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	AssetDatabase::UnloadAsset(skyMaterial);
	AssetDatabase::UnloadAsset(texturedMaterial);
	AssetDatabase::UnloadAsset(texturedMesh);
	AssetDatabase::UnloadAsset(plainMaterial);
	AssetDatabase::UnloadAsset(plainMesh);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

INSTANTIATE_TEST_SUITE_P(Skies, DeviceRecoverySceneTest, ::testing::Values(SkyMode::Gradient, SkyMode::Material, SkyMode::Color));

TEST_F(DeviceRecoveryFixture, TextureAssetGetsPixelsBack) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	constexpr i32 SIZE = 8;
	TextureAsset texture;
	texture.size = {SIZE, SIZE};
	texture.format = RHI::Format::RGBA8Unorm;
	texture.settings.generateMips = false;
	texture.pixels.resize(static_cast<size_t>(SIZE) * SIZE * 4);
	for (size_t i = 0; i < texture.pixels.size(); ++i) {
		texture.pixels[i] = static_cast<u8>(i * 13 + 5);
	}
	texture.NotifyLoaded();
	ASSERT_TRUE(texture.texture.IsValid());
	const RHI::TextureHandle handle = texture.texture;
	const u32 slot = device->GetTableIndex(handle);

	device->SimulateLoss();
	TextureAsset late;
	late.size = {SIZE, SIZE};
	late.format = RHI::Format::RGBA8Unorm;
	late.settings.generateMips = false;
	late.pixels = texture.pixels;
	late.NotifyLoaded();
	ASSERT_TRUE(late.texture.IsValid());
	RenderSystem::RenderAll();
	ASSERT_FALSE(device->IsLost());

	EXPECT_EQ(texture.texture, handle);
	EXPECT_EQ(device->GetTableIndex(handle), slot);
	for (const TextureAsset* asset : {&texture, &late}) {
		Vector<u8> read{GetMemoryResource(MemTag::Graphics)};
		read.assign(asset->pixels.size(), 0);
		device->ReadbackTexture(asset->texture, {read.data(), read.size()});
		EXPECT_EQ(std::memcmp(read.data(), asset->pixels.data(), read.size()), 0);
	}

	texture.NotifyUnloaded();
	late.NotifyUnloaded();
}

TEST_F(DeviceRecoveryFixture, ViewTargetReportsLostImage) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	const ViewTargetId target = RenderSystem::CreateViewTarget({32, 32});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	EXPECT_FALSE(RenderSystem::IsViewTargetDrawn(target));
	EXPECT_FALSE(RenderSystem::IsViewTargetDrawn(INVALID_VIEW_TARGET));

	auto submit = [&](Color clear) {
		ViewDesc desc;
		desc.view = ViewFrom(Vec3(0.0f, 0.0f, -5.0f));
		desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
		desc.clearColor = clear;
		RenderSystem::SubmitView(target, desc);
	};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};

	RenderSystem::RenderAll();
	EXPECT_FALSE(RenderSystem::IsViewTargetDrawn(target));
	EXPECT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	submit(Color(0.0f, 0.0f, 1.0f, 1.0f));
	RenderSystem::RenderAll();
	EXPECT_TRUE(RenderSystem::IsViewTargetDrawn(target));
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	EXPECT_GT(pixels[2], 200);

	device->SimulateLoss();
	EXPECT_FALSE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RenderSystem::RenderAll();
	ASSERT_FALSE(device->IsLost());
	EXPECT_FALSE(RenderSystem::IsViewTargetDrawn(target));
	EXPECT_FALSE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RenderSystem::RenderAll();
	EXPECT_FALSE(RenderSystem::ReadbackViewTarget(target, pixels, size));

	submit(Color(0.0f, 1.0f, 0.0f, 1.0f));
	RenderSystem::RenderAll();
	EXPECT_TRUE(RenderSystem::IsViewTargetDrawn(target));
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	EXPECT_GT(pixels[1], 200);
	EXPECT_LT(pixels[2], 50);

	RenderSystem::ResizeViewTarget(target, {48, 16});
	EXPECT_FALSE(RenderSystem::IsViewTargetDrawn(target));

	RenderSystem::DestroyViewTarget(target);
	RenderSystem::SetSceneCamerasRenderToWindows(true);
}

TEST_F(DeviceRecoveryFixture, FramesWaitForDriverThenResume) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	MeshAsset* cubeMesh = static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive")));
	MaterialAsset* material = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat")));
	ASSERT_NE(cubeMesh, nullptr);
	ASSERT_NE(material, nullptr);
	Handle<Scene> scene = SceneSystem::CreateScene("Blind");
	SceneSystem::CreateObject("Sun", scene)->AddComponent<Light>();
	const ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	auto addCube = [&](f32 x) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Cube", scene);
		object->SetLocalPosition(Vec3(x, 0.0f, 0.0f));
		Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
		renderer->SetMesh(Handle<MeshAsset>(cubeMesh));
		renderer->SetMaterial(0, Handle<MaterialAsset>(material));
	};
	auto render = [&]() {
		SceneSystem::UpdateAll();
		SceneSystem::EndFrame();
		ViewDesc desc;
		desc.view = ViewFrom(Vec3(0.0f, 0.0f, -6.0f));
		desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
		desc.cameraPosition = Vec3(0.0f, 0.0f, -6.0f);
		RenderSystem::SubmitView(target, desc);
		RenderSystem::RenderAll();
	};
	auto litTexels = [&](bool leftHalf) {
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		if (!RenderSystem::ReadbackViewTarget(target, pixels, size)) {
			return ~0u;
		}
		u32 lit = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = leftHalf ? 0 : size.x / 2; x < (leftHalf ? size.x / 2 : size.x); ++x) {
				const u8* texel = pixels.data() + (static_cast<size_t>(y) * size.x + x) * 4;
				lit += texel[0] > 24 || texel[1] > 24 || texel[2] > 24 ? 1u : 0u;
			}
		}
		return lit;
	};

	addCube(-1.5f);
	render();
	const u32 leftBefore = litTexels(true);
	ASSERT_GT(leftBefore, 40u);
	ASSERT_EQ(litTexels(false), 0u);

	DeviceEventCounter events;
	ScopedLog log;
	ScopedMissingDriver missing;
	device->SimulateLoss();
	render();
	if (!device->IsLost()) {
		GTEST_SKIP() << "Vulkan loader ignores driver overrides in this process";
	}
	MeshAsset* sphere = static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path("engine://Meshes/Sphere.primitive")));
	ASSERT_NE(sphere, nullptr);
	addCube(1.5f);
	const auto blindStart = std::chrono::steady_clock::now();
	constexpr int BLIND_FRAMES = 200;
	for (int frame = 0; frame < BLIND_FRAMES; ++frame) {
		render();
	}
	const f64 blindMs = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - blindStart).count();
	EXPECT_TRUE(device->IsLost());
	EXPECT_EQ(events.restored, 0u);
	EXPECT_EQ(litTexels(true), ~0u);
	EXPECT_LT(blindMs / BLIND_FRAMES, 5.0);

	missing.Restore();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (device->IsLost() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		render();
	}
	ASSERT_FALSE(device->IsLost());
	render();
	EXPECT_EQ(events.restored, 1u);
	EXPECT_EQ(litTexels(true), leftBefore);
	EXPECT_GT(litTexels(false), 40u);
	EXPECT_NE(sphere->vertexBufferAddress, 0u);

	const LogSink& seen = log.Delivered();
	EXPECT_EQ(seen.Count("couldn't rebuild device"), 1u) << seen.Joined();
	EXPECT_EQ(seen.CountAtLeast(LogLevel::Error), 0u) << seen.Joined();

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	AssetDatabase::UnloadAsset(sphere);
	AssetDatabase::UnloadAsset(material);
	AssetDatabase::UnloadAsset(cubeMesh);
	RenderSystem::SetSceneCamerasRenderToWindows(true);
}

TEST_F(DeviceRecoveryFixture, UITextMatchesOnFirstFrameBack) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	const Handle<FontAsset> font = UISystem::GetDefaultFont();
	if (!font.IsValid()) {
		GTEST_SKIP() << "engine font not mounted";
	}
	const UISurfaceId surface = UISystem::CreateSurface({192, 64});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	UI::ElementHandle panel = context->CreateElement();
	context->AppendChild(context->GetRoot(), panel);
	UI::Style style;
	style.width = UI::Length::Percent(1.0f);
	style.height = UI::Length::Percent(1.0f);
	style.background = UI::Brush::SolidColor(Color(0.1f, 0.3f, 0.2f, 1.0f));
	context->SetStyle(panel, style);
	UI::ElementHandle label = context->CreateElement();
	context->AppendChild(panel, label);
	context->SetText(label, font, 18.0f, Color::White(), "Device 0123 lost & found");

	auto frame = [&]() {
		WindowSystem::PumpEvents();
		FrameStages::Run(FrameStage::UIUpdate, FrameStageArgs{});
		RenderSystem::RenderAll();
	};
	auto capture = [&](Vector<u8>& out) {
		const RHI::TextureHandle texture = UISystem::GetSurfaceTexture(surface);
		out.assign(static_cast<size_t>(192) * 64 * 4, 0);
		device->ReadbackTexture(texture, {out.data(), out.size()});
	};

	frame();
	frame();
	if (UISystem::GetStats().drawCalls == 0) {
		UISystem::DestroySurface(surface);
		GTEST_SKIP() << "engine://Shaders/UI.hlsl unavailable (dxcompiler not staged)";
	}
	Vector<u8> before{GetMemoryResource(MemTag::Graphics)};
	capture(before);
	ASSERT_GT(DistinctTexels(before), 4u);

	ScopedLog log;
	for (u32 round = 0; round < 2; ++round) {
		device->SimulateLoss();
		frame();
		ASSERT_FALSE(device->IsLost());
		Vector<u8> after{GetMemoryResource(MemTag::Graphics)};
		capture(after);
		EXPECT_EQ(DifferingBytes(before, after), 0u) << round;
		frame();
		capture(after);
		EXPECT_EQ(DifferingBytes(before, after), 0u) << round;
	}
	const LogSink& seen = log.Delivered();
	EXPECT_EQ(seen.CountAtLeast(LogLevel::Error), 0u) << seen.Joined();
	EXPECT_EQ(seen.Count("from before device rebuild"), 0u);

	UISystem::DestroySurface(surface);
}
