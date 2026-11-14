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

#include "PassProbes.h"
#include "ZoneProbe.h"

#ifndef SHALLTEAR_SHARED
#include "Shalltear/PostProcessing/PostProcessPass.h"
#include "Shalltear/PostProcessing/PostProcessProfile.h"
#include "Shalltear/PostProcessing/PostProcessVolume.h"

#include "PostProcessGrade.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

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

		virtual str ProjectSettingsDocument() const { return {}; }

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_pprender_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");

			SettingsPaths paths;
			paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			if (const str document = ProjectSettingsDocument(); !document.empty()) {
				std::ofstream(_root / "ProjectSettings.ini", std::ios::binary) << document;
			}
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

		bool AddBrightQuad(f32 intensity, const Vec3& position = Vec3(), const char* name = "Bright", f32 scale = 0.6f) {
			Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
			if (!unlitShader || !quadMesh) {
				return false;
			}
			const str fileName = str(name) + ".mat";
			FileSystem::WriteTextFile(Path((_root / fileName.c_str()).generic_string()),
									  str("{\n\t\"shader\": \"") + unlitShader->assetId.ToString() + "\"\n}\n");
			auto* material = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + fileName)));
			if (!material) {
				return false;
			}
			material->SetColor("baseColor", Color(intensity, intensity, intensity, 1.0f));
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			object->SetLocalPosition(position);
			object->SetLocalScale(Vec3(scale, scale, scale));
			Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			SceneSystem::EndFrame();
			return true;
		}

		f32 BloomShare(f32 scale) {
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			_scene = SceneSystem::CreateScene("Share");
			const str name = std::format("Source{}", static_cast<i32>(scale * 100.0f));
			Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			if (!AddBrightQuad(12.0f, Vec3(), name.c_str(), scale) || !Render({}, reference, size, Color(0.0f, 0.0f, 0.0f))) {
				return -1.0f;
			}
			PostProcessProfile* profile = MakeProfile((name + ".slpp").c_str());
			if (!profile) {
				return -1.0f;
			}
			profile->bloom = true;
			profile->bloomIntensity = 1.0f;
			profile->bloomThreshold = 0.9f;
			AddVolume(profile);
			PostProcessPass row;
			const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
			if (!Render(rows, pixels, size, Color(0.0f, 0.0f, 0.0f))) {
				return -1.0f;
			}
			f32 source = 0.0f;
			f32 spread = 0.0f;
			for (i32 y = 0; y < size.y; ++y) {
				for (i32 x = 0; x < size.x; ++x) {
					const u8 before = Texel(reference, size, x, y)[0];
					if (before > 200) {
						source += 1.0f;
					} else {
						spread += ByteToLinear(Texel(pixels, size, x, y)[0]) - ByteToLinear(before);
					}
				}
			}
			return source > 0.0f ? spread / source : -1.0f;
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

		static bool IsColor(const u8* texel, const Vec3& bytes) {
			return std::abs(texel[0] - bytes.x) <= 1.0f && std::abs(texel[1] - bytes.y) <= 1.0f && std::abs(texel[2] - bytes.z) <= 1.0f;
		}

		static bool FindColorTexel(const Vector<u8>& pixels, Vec2I size, const Vec3& bytes, Vec2I& out) {
			for (i32 y = 1; y + 1 < size.y; ++y) {
				for (i32 x = 1; x + 1 < size.x; ++x) {
					bool inside = true;
					for (i32 dy = -1; dy <= 1 && inside; ++dy) {
						for (i32 dx = -1; dx <= 1 && inside; ++dx) {
							inside = IsColor(Texel(pixels, size, x + dx, y + dy), bytes);
						}
					}
					if (inside) {
						out = Vec2I(x, y);
						return true;
					}
				}
			}
			return false;
		}

		static bool FindGrayTexel(const Vector<u8>& pixels, Vec2I size, f32 value, Vec2I& out) {
			return FindColorTexel(pixels, size, Vec3(value, value, value), out);
		}

		static Vec3 ColorBytes(const Color& linear) {
			return Vec3(LinearToByte(linear.r), LinearToByte(linear.g), LinearToByte(linear.b));
		}

		bool AddSkinnedMesh(const Color& color) {
			return PassProbes::AddSkinnedMesh(FindRepoDir("Source/Tests/Fixtures/Graphics"), _root, _scene, color).IsValid();
		}

		static void ExpectScaled(const u8* before, const u8* after, const Vec3& scale) {
			const f32 factors[3] = {scale.x, scale.y, scale.z};
			for (i32 channel = 0; channel < 3; ++channel) {
				const f32 expected = LinearToByte(ByteToLinear(before[channel]) * factors[channel]);
				EXPECT_NEAR(static_cast<f32>(after[channel]), expected, 2.0f) << channel << ' ' << static_cast<i32>(before[channel]);
			}
		}

		static f32 VignetteFactor(Vec2I size, Vec2I texel, f32 intensity, f32 smoothness) {
			const f32 dx = std::abs((static_cast<f32>(texel.x) + 0.5f) / static_cast<f32>(size.x) - 0.5f) * intensity * 3.0f;
			const f32 dy = std::abs((static_cast<f32>(texel.y) + 0.5f) / static_cast<f32>(size.y) - 0.5f) * intensity * 3.0f;
			return std::pow(std::clamp(1.0f - dx * dx - dy * dy, 0.0f, 1.0f), smoothness * 5.0f);
		}

		static std::array<Vec2I, 5> VignetteProbes(Vec2I size) {
			return {Vec2I(0, 0), Vec2I(0, size.y / 2), Vec2I(size.x / 8, size.y / 2), Vec2I(size.x / 4, size.y / 4),
					Vec2I(size.x / 2, size.y / 2)};
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

TEST_F(PostProcessRenderFixture, WhiteBalanceRunsBeforeColorFilter) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));

	PostProcessProfile* profile = MakeProfile("Balance.slpp");
	ASSERT_NE(profile, nullptr);
	profile->whiteBalance = true;
	profile->temperature = -60.0f;
	profile->tint = 20.0f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_EQ(size, referenceSize);

	const u8* before = Texel(reference, referenceSize, size.x / 2, size.y / 2);
	const Vec3 linear(ByteToLinear(before[0]), ByteToLinear(before[1]), ByteToLinear(before[2]));
	const Vec3 balanced = PostProcessOps::WhiteBalanceMatrix(-60.0f, 20.0f) * linear;
	const u8* cooled = Texel(pixels, size, size.x / 2, size.y / 2);
	EXPECT_NEAR(static_cast<f32>(cooled[0]), LinearToByte(balanced.x), 2.0f);
	EXPECT_NEAR(static_cast<f32>(cooled[1]), LinearToByte(balanced.y), 2.0f);
	EXPECT_NEAR(static_cast<f32>(cooled[2]), LinearToByte(balanced.z), 2.0f);
	EXPECT_GT(cooled[2], before[2] + 10);
	EXPECT_LT(cooled[0], before[0]);

	profile->colorGrading = true;
	profile->colorFilter = Color(1.0f, 0.5f, 0.25f);
	Vector<u8> filtered{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(Render(rows, filtered, size));
	const u8* both = Texel(filtered, size, size.x / 2, size.y / 2);
	EXPECT_NEAR(static_cast<f32>(both[0]), LinearToByte(balanced.x * 1.0f), 2.0f);
	EXPECT_NEAR(static_cast<f32>(both[1]), LinearToByte(balanced.y * 0.5f), 2.0f);
	EXPECT_NEAR(static_cast<f32>(both[2]), LinearToByte(balanced.z * 0.25f), 2.0f);
}

TEST_F(PostProcessRenderFixture, TonesFollowLuminance) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(AddBrightQuad(0.6f, Vec3(-1.2f, 0.0f, 0.0f), "Mid"));
	ASSERT_TRUE(AddBrightQuad(0.9f, Vec3(1.2f, 0.0f, 0.0f), "High"));
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));

	Vec2I mid{};
	Vec2I high{};
	ASSERT_TRUE(FindGrayTexel(reference, referenceSize, LinearToByte(0.6f), mid));
	ASSERT_TRUE(FindGrayTexel(reference, referenceSize, LinearToByte(0.9f), high));
	const Vec2I clear(referenceSize.x / 2, referenceSize.y / 2);
	const u8* clearBefore = Texel(reference, referenceSize, clear.x, clear.y);
	const f32 clearLuma = 0.2126f * ByteToLinear(clearBefore[0]) + 0.7152f * ByteToLinear(clearBefore[1]) + 0.0722f * ByteToLinear(clearBefore[2]);
	ASSERT_LT(clearLuma, 0.45f);

	PostProcessProfile* profile = MakeProfile("Tones.slpp");
	ASSERT_NE(profile, nullptr);
	profile->shadowsMidtonesHighlights = true;
	profile->shadows = Color(0.5f, 1.0f, 0.5f);
	profile->shadowsOffset = 0.1f;
	profile->midtonesOffset = -0.25f;
	profile->highlights = Color(1.0f, 1.0f, 0.5f);
	profile->shadowsStart = 0.45f;
	profile->shadowsEnd = 0.5f;
	profile->highlightsStart = 0.7f;
	profile->highlightsEnd = 0.75f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_EQ(size, referenceSize);

	ExpectScaled(clearBefore, Texel(pixels, size, clear.x, clear.y), Vec3(0.9f, 1.4f, 0.9f));
	ExpectScaled(Texel(reference, referenceSize, mid.x, mid.y), Texel(pixels, size, mid.x, mid.y), Vec3(0.75f, 0.75f, 0.75f));
	ExpectScaled(Texel(reference, referenceSize, high.x, high.y), Texel(pixels, size, high.x, high.y), Vec3(1.0f, 1.0f, 0.5f));
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

TEST_F(PostProcessRenderFixture, VignetteFalloffFollowsSmoothness) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));

	PostProcessProfile* profile = MakeProfile("Falloff.slpp");
	ASSERT_NE(profile, nullptr);
	profile->vignette = true;
	profile->vignetteIntensity = 0.4f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	u8 cornerRed[2] = {};
	const f32 smoothnesses[2] = {0.05f, 1.0f};
	for (i32 pass = 0; pass < 2; ++pass) {
		profile->vignetteSmoothness = smoothnesses[pass];
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(Render(rows, pixels, size));
		ASSERT_EQ(size, referenceSize);
		for (const Vec2I& probe : VignetteProbes(size)) {
			const f32 factor = VignetteFactor(size, probe, 0.4f, smoothnesses[pass]);
			SCOPED_TRACE(::testing::Message() << "smoothness " << smoothnesses[pass] << " at " << probe.x << ',' << probe.y);
			ExpectScaled(Texel(reference, referenceSize, probe.x, probe.y), Texel(pixels, size, probe.x, probe.y),
						 Vec3(factor, factor, factor));
		}
		cornerRed[pass] = Texel(pixels, size, 0, 0)[0];
	}
	EXPECT_GT(cornerRed[0], cornerRed[1] + 100);
}

TEST_F(PostProcessRenderFixture, VignetteColorMultipliesFrame) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));
	const Color black(0.0f, 0.0f, 0.0f);
	Vector<u8> dark{GetMemoryResource(MemTag::Graphics)};
	Vec2I darkSize{};
	ASSERT_TRUE(Render({}, dark, darkSize, black));

	PostProcessProfile* profile = MakeProfile("Tinted.slpp");
	ASSERT_NE(profile, nullptr);
	profile->vignette = true;
	profile->vignetteIntensity = 0.5f;
	profile->vignetteSmoothness = 0.2f;
	profile->vignetteColor = Color(1.0f, 0.25f, 0.0f);
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_EQ(size, referenceSize);
	for (const Vec2I& probe : VignetteProbes(size)) {
		const f32 factor = VignetteFactor(size, probe, 0.5f, 0.2f);
		SCOPED_TRACE(::testing::Message() << "at " << probe.x << ',' << probe.y);
		ExpectScaled(Texel(reference, referenceSize, probe.x, probe.y), Texel(pixels, size, probe.x, probe.y),
					 Vec3(1.0f, 0.25f + 0.75f * factor, factor));
	}
	EXPECT_EQ(Texel(pixels, size, 0, 0)[0], Texel(reference, referenceSize, 0, 0)[0]);
	EXPECT_LT(Texel(pixels, size, 0, 0)[1] + 40, Texel(reference, referenceSize, 0, 0)[1]);

	Vector<u8> darkTinted{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(Render(rows, darkTinted, size, black));
	EXPECT_EQ(Texel(dark, darkSize, 0, 0)[0], 0);
	EXPECT_EQ(darkTinted, dark);
}

TEST_F(PostProcessRenderFixture, VignetteRunsBeforeGrade) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));

	PostProcessProfile* profile = MakeProfile("VignetteGraded.slpp");
	ASSERT_NE(profile, nullptr);
	profile->vignette = true;
	profile->vignetteIntensity = 0.4f;
	profile->vignetteSmoothness = 0.2f;
	profile->colorGrading = true;
	profile->contrast = 50.0f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_EQ(size, referenceSize);

	const std::array<Vec2I, 5> probes = VignetteProbes(size);
	for (const Vec2I& probe : {probes[0], probes[2], probes[3], probes[4]}) {
		const f32 factor = VignetteFactor(size, probe, 0.4f, 0.2f);
		const u8* before = Texel(reference, referenceSize, probe.x, probe.y);
		const u8* after = Texel(pixels, size, probe.x, probe.y);
		for (i32 channel = 0; channel < 3; ++channel) {
			const f32 graded = std::max((ByteToLinear(before[channel]) * factor - 0.18f) * 1.5f + 0.18f, 0.0f);
			EXPECT_NEAR(static_cast<f32>(after[channel]), LinearToByte(graded), 2.0f)
				<< probe.x << ',' << probe.y << " channel " << channel;
		}
	}
	EXPECT_EQ(Texel(pixels, size, 0, 0)[2], 0);
	EXPECT_GT(Texel(reference, referenceSize, 0, 0)[2], 40);
}

TEST_F(PostProcessRenderFixture, VignetteOverSkinnedMesh) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const Color green(0.1f, 0.8f, 0.2f);
	ASSERT_TRUE(AddSkinnedMesh(green));

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));
	Vec2I mesh{};
	ASSERT_TRUE(FindColorTexel(reference, referenceSize, ColorBytes(green), mesh));

	PostProcessProfile* profile = MakeProfile("SkinnedVignette.slpp");
	ASSERT_NE(profile, nullptr);
	profile->vignette = true;
	profile->vignetteIntensity = 0.4f;
	profile->vignetteSmoothness = 0.2f;
	AddVolume(profile);

	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_EQ(size, referenceSize);

	const std::array<Vec2I, 5> probes = VignetteProbes(size);
	for (const Vec2I& probe : {probes[0], probes[1], probes[2], probes[3], probes[4], mesh}) {
		const f32 factor = VignetteFactor(size, probe, 0.4f, 0.2f);
		SCOPED_TRACE(::testing::Message() << "at " << probe.x << ',' << probe.y);
		ExpectScaled(Texel(reference, referenceSize, probe.x, probe.y), Texel(pixels, size, probe.x, probe.y),
					 Vec3(factor, factor, factor));
	}
	EXPECT_LT(Texel(pixels, size, 0, 0)[0] + 40, Texel(reference, referenceSize, 0, 0)[0]);
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

TEST_F(PostProcessRenderFixture, BloomSpreadsFromSkinnedMesh) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const Color clear = Color(0.0f, 0.0f, 0.0f);
	ASSERT_TRUE(AddSkinnedMesh(Color(12.0f, 12.0f, 12.0f)));

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize, clear));

	PostProcessProfile* profile = MakeProfile("SkinnedBloom.slpp");
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
	EXPECT_GE(Texel(pixels, size, size.x / 2, edgeY)[0], Texel(reference, referenceSize, size.x / 2, edgeY)[0]);
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

namespace {

	class GammaPostProcessFixture : public PostProcessRenderFixture {
	protected:
		str ProjectSettingsDocument() const override { return "[Graphics]\ncolorSpace = Gamma\n"; }
	};

} // anonymous namespace

TEST_F(GammaPostProcessFixture, GammaThresholdReadsSceneValues) {
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

	u32 spread = 0;
	for (i32 y = 0; y < size.y; ++y) {
		for (i32 x = 0; x < size.x; ++x) {
			const u8 before = Texel(reference, referenceSize, x, y)[0];
			const u8 after = Texel(pixels, size, x, y)[0];
			spread += before < 200 && after > before ? static_cast<u32>(after - before) : 0u;
		}
	}
	EXPECT_GT(spread, 100u);
}

TEST_F(PostProcessRenderFixture, BloomShareIgnoresSourceSize) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const f32 lamp = BloomShare(0.6f);
	const f32 glint = BloomShare(0.12f);
	ASSERT_GT(lamp, 0.0f);
	EXPECT_NEAR(glint, lamp, 0.25f * lamp) << glint << " -> " << lamp;
}

TEST_F(GammaPostProcessFixture, GammaBloomKeepsGlintsSmall) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const f32 lamp = BloomShare(0.6f);
	const f32 glint = BloomShare(0.12f);
	ASSERT_GT(lamp, 0.0f);
	EXPECT_LT(glint, 0.75f * lamp) << glint << " -> " << lamp;
}

TEST_F(GammaPostProcessFixture, GammaBloomAddsInLinearLight) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const Color gray(0.25f, 0.25f, 0.25f);
	ASSERT_TRUE(AddBrightQuad(12.0f));
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize, gray));
	const u8 surround = Texel(reference, referenceSize, 0, 0)[0];
	EXPECT_NEAR(static_cast<f32>(surround), 64.0f, 1.0f);

	PostProcessProfile* profile = MakeProfile("LinearAdd.slpp");
	ASSERT_NE(profile, nullptr);
	profile->bloom = true;
	profile->bloomIntensity = 1.0f;
	profile->bloomThreshold = 0.9f;
	AddVolume(profile);
	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> full{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> half{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, full, size, gray));
	profile->bloomIntensity = 0.5f;
	ASSERT_TRUE(Render(rows, half, size, gray));
	ASSERT_EQ(size, referenceSize);

	u32 checked = 0;
	f32 worst = 0.0f;
	for (i32 y = 0; y < size.y; ++y) {
		for (i32 x = 0; x < size.x; ++x) {
			const u8 lit = Texel(full, size, x, y)[0];
			if (Texel(reference, referenceSize, x, y)[0] != surround || lit < surround + 60 || lit > 230) {
				continue;
			}
			const f32 added = ByteToLinear(lit) - ByteToLinear(surround);
			const f32 expected = LinearToByte(ByteToLinear(surround) + 0.5f * added);
			worst = std::max(worst, std::abs(static_cast<f32>(Texel(half, size, x, y)[0]) - expected));
			++checked;
		}
	}
	EXPECT_GT(checked, 50u);
	EXPECT_LT(worst, 2.5f) << checked;
}

TEST_F(GammaPostProcessFixture, GammaBloomLevelsFilterLinearLight) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const Color clear = Color(0.0f, 0.0f, 0.0f);
	ASSERT_TRUE(AddBrightQuad(0.8f, Vec3(), "Lamp", 1.2f));
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize, clear));

	PostProcessProfile* profile = MakeProfile("LevelEnergy.slpp");
	ASSERT_NE(profile, nullptr);
	profile->bloom = true;
	profile->bloomIntensity = 0.5f;
	profile->bloomThreshold = 0.4f;
	profile->bloomScatter = 0.0f;
	AddVolume(profile);
	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> tight{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> wide{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, tight, size, clear));
	profile->bloomScatter = 1.0f;
	ASSERT_TRUE(Render(rows, wide, size, clear));
	ASSERT_EQ(size, referenceSize);

	f32 source = 0.0f;
	f32 tightLight = 0.0f;
	f32 wideLight = 0.0f;
	u8 peak = 0;
	for (i32 y = 0; y < size.y; ++y) {
		for (i32 x = 0; x < size.x; ++x) {
			const f32 before = ByteToLinear(Texel(reference, referenceSize, x, y)[0]);
			source += Texel(reference, referenceSize, x, y)[0] > 100 ? 1.0f : 0.0f;
			tightLight += ByteToLinear(Texel(tight, size, x, y)[0]) - before;
			wideLight += ByteToLinear(Texel(wide, size, x, y)[0]) - before;
			peak = std::max(peak, Texel(tight, size, x, y)[0]);
		}
	}
	ASSERT_GT(source, 300.0f);
	ASSERT_LT(peak, 250);
	const f32 passed = (0.8f - 0.4f) * 0.5f * source;
	EXPECT_GT(tightLight, 0.5f * passed) << tightLight << " -> " << passed;
	EXPECT_GT(wideLight, 0.4f * tightLight) << wideLight << " -> " << tightLight;
}

TEST_F(GammaPostProcessFixture, GammaVignetteAndGradeUseLinearLight) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	ASSERT_TRUE(Render({}, reference, referenceSize));

	PostProcessProfile* profile = MakeProfile("GammaGraded.slpp");
	ASSERT_NE(profile, nullptr);
	profile->vignette = true;
	profile->vignetteIntensity = 0.4f;
	profile->vignetteSmoothness = 0.2f;
	profile->colorGrading = true;
	profile->postExposure = -1.0f;
	AddVolume(profile);
	PostProcessPass row;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_EQ(size, referenceSize);

	const std::array<Vec2I, 5> probes = VignetteProbes(size);
	for (const Vec2I& probe : {probes[0], probes[2], probes[3], probes[4]}) {
		const f32 scale = 0.5f * VignetteFactor(size, probe, 0.4f, 0.2f);
		SCOPED_TRACE(::testing::Message() << "at " << probe.x << ',' << probe.y);
		ExpectScaled(Texel(reference, referenceSize, probe.x, probe.y), Texel(pixels, size, probe.x, probe.y), Vec3(scale, scale, scale));
	}
	const u8 before = Texel(reference, referenceSize, size.x / 2, size.y / 2)[0];
	const u8 after = Texel(pixels, size, size.x / 2, size.y / 2)[0];
	EXPECT_NEAR(static_cast<f32>(before), 153.0f, 1.0f);
	EXPECT_GT(after, before / 2 + 20);
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
