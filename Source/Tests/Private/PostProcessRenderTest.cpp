#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/EditorViewportOps.h"

#include "ZoneProbe.h"

#ifndef SHALLTEAR_SHARED
#include "Shalltear/PostProcessing/PostProcessPass.h"
#include "Shalltear/PostProcessing/PostProcessProfile.h"
#include "Shalltear/PostProcessing/PostProcessVolume.h"
#endif

#include <atomic>
#include <chrono>
#include <cmath>
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

	f32 ByteToLinear(u8 value) {
		const f32 encoded = static_cast<f32>(value) / 255.0f;
		return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
	}

	f32 LinearToByte(f32 linear) {
		linear = linear < 0.0f ? 0.0f : (linear > 1.0f ? 1.0f : linear);
		const f32 encoded = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
		return encoded * 255.0f;
	}

	class PostProcessRenderFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mounted = false;
		bool _projectMounted = false;
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Vec2I _size{128, 128};
		Handle<Scene> _scene;
		char _token = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_pprender_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
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
			_scene = SceneSystem::CreateScene("PostProcessRender");
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

		PostProcessProfile* MakeProfile(const char* fileName) {
			const Guid id = AssetDatabase::CreateAsset(Path(str("project://Assets/") + fileName),
													   StringHash("PostProcessProfile"));
			if (!id.IsValid()) {
				return nullptr;
			}
			return static_cast<PostProcessProfile*>(AssetDatabase::LoadAssetByGuid(id));
		}

		Handle<PostProcessVolume> AddVolume(PostProcessProfile* profile) {
			Handle<SceneObject> object = SceneSystem::CreateObject("Volume", _scene);
			Handle<PostProcessVolume> volume = object->AddComponent<PostProcessVolume>();
			volume->profile = Handle<PostProcessProfile>(profile);
			SceneSystem::EndFrame();
			return volume;
		}

		bool AddBrightQuad(f32 intensity) {
			Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
			if (!unlitShader || !quadMesh) {
				return false;
			}
			FileSystem::WriteTextFile(Path((_root / "Bright.mat").generic_string()),
									  str("{\n\t\"shader\": \"") + unlitShader->assetId.ToString() + "\"\n}\n");
			auto* material = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("project://Bright.mat")));
			if (!material) {
				return false;
			}
			material->SetColor("baseColor", Color(intensity, intensity, intensity, 1.0f));
			Handle<SceneObject> object = SceneSystem::CreateObject("Bright", _scene);
			object->SetLocalScale(Vec3(0.6f, 0.6f, 0.6f));
			Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			SceneSystem::EndFrame();
			return true;
		}

		bool Render(std::span<const Handle<RenderPass>> rows, Vector<u8>& outPixels, Vec2I& outSize,
					Color clear = Color(0.6f, 0.3f, 0.1f)) {
			ViewDesc desc;
			desc.cameraPosition = Vec3(0.0f, 3.0f, 6.0f);
			desc.view = Mat4::LookAt(desc.cameraPosition, Vec3(0.0f, 0.0f, 0.0f));
			desc.projection = Mat4::PerspectiveReverseZ(60.0f, static_cast<f32>(_size.x) / static_cast<f32>(_size.y),
														Ops::EDITOR_NEAR_PLANE, Ops::EDITOR_FAR_PLANE);
			desc.clearColor = clear;
			desc.passes = rows;
			RenderSystem::SubmitView(_target, desc);
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTarget(_target, outPixels, outSize);
		}

		static const u8* Texel(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
			return pixels.data() + (static_cast<size_t>(y) * size.x + x) * 4;
		}
	};

} // namespace

TEST_F(PostProcessRenderFixture, GlobalVolumesColorFilterMultipliesFrame) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));

	PostProcessProfile* profile = MakeProfile("Filter.slpp");
	ASSERT_NE(profile, nullptr);
	profile->colorGrading = true;
	profile->colorFilter = Color(1.0f, 0.5f, 0.0f);
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_EQ(size, referenceSize);

	const u8* before = Texel(reference, referenceSize, size.x / 2, size.y / 2);
	const u8* after = Texel(pixels, size, size.x / 2, size.y / 2);
	const f32 filter[3] = {1.0f, 0.5f, 0.0f};
	for (i32 channel = 0; channel < 3; ++channel) {
		const f32 expected = LinearToByte(ByteToLinear(before[channel]) * filter[channel]);
		EXPECT_NEAR(static_cast<f32>(after[channel]), expected, 2.0f) << "channel " << channel << ' ' << static_cast<i32>(before[channel]) << " and wrote " << static_cast<i32>(after[channel]) << ", predicted " << expected;
	}

	EXPECT_LE(after[2], 2);
	EXPECT_GT(before[2], 10);
}

TEST_F(PostProcessRenderFixture, PostExposureScalesFrameByStops) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));

	PostProcessProfile* profile = MakeProfile("Exposure.slpp");
	ASSERT_NE(profile, nullptr);
	profile->colorGrading = true;
	profile->postExposure = -1.0f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_EQ(size, referenceSize);

	const u8* before = Texel(reference, referenceSize, size.x / 2, size.y / 2);
	const u8* after = Texel(pixels, size, size.x / 2, size.y / 2);
	for (i32 channel = 0; channel < 3; ++channel) {
		const f32 expected = LinearToByte(ByteToLinear(before[channel]) * 0.5f);
		EXPECT_NEAR(static_cast<f32>(after[channel]), expected, 2.0f) << "channel " << channel;
	}
	EXPECT_GT(before[0] - after[0], 20);
}

TEST_F(PostProcessRenderFixture, LeftVolumeLeavesFrame) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));

	PostProcessProfile* profile = MakeProfile("Distant.slpp");
	ASSERT_NE(profile, nullptr);
	profile->colorGrading = true;
	profile->colorFilter = Color(0.0f, 0.0f, 0.0f);
	Handle<PostProcessVolume> volume = AddVolume(profile);
	volume->isGlobal = false;
	volume->size = Vec3(2.0f, 2.0f, 2.0f);
	volume->blendDistance = 1.0f;
	volume->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, -500.0f));
	SceneSystem::EndFrame();

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_EQ(size, referenceSize);

	const u8* before = Texel(reference, referenceSize, size.x / 2, size.y / 2);
	const u8* after = Texel(pixels, size, size.x / 2, size.y / 2);
	EXPECT_EQ(after[0], before[0]);
	EXPECT_EQ(after[1], before[1]);
	EXPECT_EQ(after[2], before[2]);
	EXPECT_GT(before[0], 10);

	volume->GetOwner()->SetLocalPosition(Vec3(0.0f, 3.0f, 6.0f));
	SceneSystem::EndFrame();
	Vector<u8> inside{GetMemoryResource(MemTag::Graphics)};
	Vec2I insideSize{};
	ASSERT_TRUE(Render(rows, inside, insideSize));
	const u8* within = Texel(inside, insideSize, size.x / 2, size.y / 2);
	EXPECT_LE(within[0], 2);
	EXPECT_LE(within[1], 2);
	EXPECT_LE(within[2], 2);
}

TEST_F(PostProcessRenderFixture, TheVignetteDarkensOutwardFromItsCentre) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));

	PostProcessProfile* profile = MakeProfile("Vignette.slpp");
	ASSERT_NE(profile, nullptr);
	profile->vignette = true;
	profile->vignetteIntensity = 1.0f;
	profile->vignetteSmoothness = 1.0f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_EQ(size, referenceSize);

	const u8* centreBefore = Texel(reference, referenceSize, size.x / 2, size.y / 2);
	const u8* centre = Texel(pixels, size, size.x / 2, size.y / 2);
	const u8* corner = Texel(pixels, size, 1, 1);

	EXPECT_NEAR(static_cast<f32>(centre[0]), static_cast<f32>(centreBefore[0]), 2.0f);
	EXPECT_LT(corner[0], centre[0] - 10);
}

TEST_F(PostProcessRenderFixture, BloomSpreadsLocally) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const Color clear = Color(0.0f, 0.0f, 0.0f);
	ASSERT_TRUE(AddBrightQuad(12.0f));

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize, clear));

	PostProcessProfile* profile = MakeProfile("Bloom.slpp");
	ASSERT_NE(profile, nullptr);
	profile->bloom = true;
	profile->bloomIntensity = 1.0f;
	profile->bloomThreshold = 0.9f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size, clear));
	ASSERT_EQ(size, referenceSize);

	i32 edgeY = -1;
	for (i32 y = 0; y < size.y; ++y) {
		if (Texel(reference, referenceSize, size.x / 2, y)[0] > 200) {
			edgeY = y;
		}
	}
	ASSERT_GT(edgeY, 0);
	ASSERT_LT(edgeY + 6, size.y);

	const i32 nearY = edgeY + 4;
	const u8 nearBefore = Texel(reference, referenceSize, size.x / 2, nearY)[0];
	const u8 nearAfter = Texel(pixels, size, size.x / 2, nearY)[0];
	const u8 cornerBefore = Texel(reference, referenceSize, 1, size.y - 2)[0];
	const u8 cornerAfter = Texel(pixels, size, 1, size.y - 2)[0];

	ASSERT_LT(nearBefore, 40);
	EXPECT_GT(nearAfter, nearBefore + 12) << static_cast<i32>(nearBefore) << " -> " << static_cast<i32>(nearAfter);
	EXPECT_LT(cornerAfter - cornerBefore, nearAfter - nearBefore);
}

TEST_F(PostProcessRenderFixture, ZeroBloomLeavesFrame) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const Color clear = Color(0.0f, 0.0f, 0.0f);
	ASSERT_TRUE(AddBrightQuad(12.0f));

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize, clear));

	PostProcessProfile* profile = MakeProfile("BloomOff.slpp");
	ASSERT_NE(profile, nullptr);
	profile->bloom = true;
	profile->bloomIntensity = 0.0f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size, clear));
	ASSERT_EQ(size, referenceSize);
	EXPECT_EQ(pixels, reference);
}

TEST_F(PostProcessRenderFixture, ThresholdAboveEverythingLeavesSurroundDark) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const Color clear = Color(0.0f, 0.0f, 0.0f);
	ASSERT_TRUE(AddBrightQuad(4.0f));

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize, clear));

	PostProcessProfile* profile = MakeProfile("BloomHigh.slpp");
	ASSERT_NE(profile, nullptr);
	profile->bloom = true;
	profile->bloomIntensity = 1.0f;
	profile->bloomThreshold = 2.8f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size, clear));
	ASSERT_EQ(size, referenceSize);

	i32 edgeY = -1;
	for (i32 y = 0; y < size.y; ++y) {
		if (Texel(reference, referenceSize, size.x / 2, y)[0] > 200) {
			edgeY = y;
		}
	}
	ASSERT_GT(edgeY, 0);
	ASSERT_LT(edgeY + 6, size.y);
	const u8 before = Texel(reference, referenceSize, size.x / 2, edgeY + 4)[0];
	const u8 after = Texel(pixels, size, size.x / 2, edgeY + 4)[0];
	EXPECT_LE(after, before + 2) << static_cast<i32>(before) << " -> " << static_cast<i32>(after);
}

TEST_F(PostProcessRenderFixture, ViewsResolveOwnVolumes) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const ViewTargetId second = RenderSystem::CreateViewTarget(_size);
	ASSERT_NE(second, INVALID_VIEW_TARGET);

	PostProcessProfile* profile = MakeProfile("TwoViews.slpp");
	ASSERT_NE(profile, nullptr);
	profile->colorGrading = true;
	profile->colorFilter = Color(1.0f, 0.0f, 0.0f);
	Handle<PostProcessVolume> volume = AddVolume(profile);
	volume->isGlobal = false;
	volume->size = Vec3(2.0f, 2.0f, 2.0f);
	volume->blendDistance = 0.0f;
	volume->GetOwner()->SetLocalPosition(Vec3(0.0f, 3.0f, 6.0f));
	SceneSystem::EndFrame();

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	const Mat4 projection = Mat4::PerspectiveReverseZ(60.0f, static_cast<f32>(_size.x) / static_cast<f32>(_size.y),
													  Ops::EDITOR_NEAR_PLANE, Ops::EDITOR_FAR_PLANE);
	const auto submit = [&](ViewTargetId target, const Vec3& eye) {
		ViewDesc desc;
		desc.cameraPosition = eye;
		desc.view = Mat4::LookAt(eye, Vec3(0.0f, 0.0f, 0.0f));
		desc.projection = projection;
		desc.clearColor = Color(0.6f, 0.3f, 0.1f);
		desc.passes = rows;
		RenderSystem::SubmitView(target, desc);
	};
	submit(_target, Vec3(0.0f, 3.0f, 6.0f));
	submit(second, Vec3(0.0f, 60.0f, 120.0f));
	RenderSystem::RenderAll();

	Vector<u8> inside{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> outside{GetMemoryResource(MemTag::Graphics)};
	Vec2I insideSize{}, outsideSize{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(_target, inside, insideSize));
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(second, outside, outsideSize));
	RenderSystem::DestroyViewTarget(second);
	ASSERT_EQ(insideSize, outsideSize);

	const u8* graded = Texel(inside, insideSize, insideSize.x / 2, insideSize.y / 2);
	const u8* plain = Texel(outside, outsideSize, outsideSize.x / 2, outsideSize.y / 2);
	EXPECT_LE(graded[1], 2);
	EXPECT_GT(plain[1], 10);
}

TEST_F(PostProcessRenderFixture, PyramidLevelsPooled) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const Color clear = Color(0.0f, 0.0f, 0.0f);
	ASSERT_TRUE(AddBrightQuad(12.0f));

	PostProcessProfile* profile = MakeProfile("Levels.slpp");
	ASSERT_NE(profile, nullptr);
	profile->bloom = true;
	profile->bloomIntensity = 1.0f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};

	{
		ZoneProbe::ZoneScope zones;
		ASSERT_TRUE(Render(rows, pixels, size, clear));
		Profiler::NextFrame();
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Row"), 12u);
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Scene"), 1u);
	}

	profile->bloomIntensity = 0.0f;
	{
		ZoneProbe::ZoneScope zones;
		ASSERT_TRUE(Render(rows, pixels, size, clear));
		Profiler::NextFrame();
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Row"), 1u);
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Scene"), 1u);
	}

	profile->bloomIntensity = 1.0f;
	ASSERT_TRUE(Render(rows, pixels, size, clear));
	const u32 settled = RenderSystem::GetStats().liveTextures;
	for (i32 frame = 0; frame < 8; ++frame) {
		ASSERT_TRUE(Render(rows, pixels, size, clear));
	}
	EXPECT_EQ(RenderSystem::GetStats().liveTextures, settled);
#endif
}

#endif
