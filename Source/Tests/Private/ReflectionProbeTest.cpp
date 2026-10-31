#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Graphics/EnvironmentIBL.h"
#include "Shalltear/Core/Math/HalfFloat.h"

#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Graphics/ReflectionProbe.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <cmath>
#include <filesystem>

using namespace Shalltear;
using ShadingTest::ArmDiagnosticSink;
using ShadingTest::CenterTexel;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::ShaderDiagnosticSink;
using ShadingTest::ShadingStage;
using ShadingTest::WriteRadianceHDR;
using Shalltear::GraphicsInternal::CUBE_FACE_COUNT;
using Shalltear::GraphicsInternal::CubeTexelDirection;
using Shalltear::GraphicsInternal::RadianceCubeFaceOffset;

namespace {

	constexpr i32 PROBE_FACE_SIZE = 128;

	Vec3 FaceColor(u32 face) {
		switch (face) {
		case 0:
			return Vec3(0.8f, 0.0f, 0.0f);
		case 1:
			return Vec3(0.0f, 0.8f, 0.0f);
		case 2:
			return Vec3(0.0f, 0.0f, 0.8f);
		case 3:
			return Vec3(0.8f, 0.8f, 0.0f);
		case 4:
			return Vec3(0.0f, 0.8f, 0.8f);
		default:
			return Vec3(0.8f, 0.0f, 0.8f);
		}
	}

	u32 FaceForDirection(const Vec3& direction) {
		u32 best = 0;
		f32 bestDot = -2.0f;
		for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
			const f32 d = Vec3::Dot(CubeTexelDirection(face, 0.5f, 0.5f), direction.Normalized());
			if (d > bestDot) {
				bestDot = d;
				best = face;
			}
		}
		return best;
	}

	TextureAsset* LoadRepaintedProbeCube(const std::filesystem::path& root, const char* fileName) {
		WriteRadianceHDR(root / fileName, PROBE_FACE_SIZE * 4, PROBE_FACE_SIZE * 2,
						 [](f32, f32) { return Vec3(0.5f, 0.5f, 0.5f); });
		const Path path(str("project://") + fileName);
		if (!AssetDatabase::EnsureAssetIdentity(path).IsValid()) {
			return nullptr;
		}
		JSONValue settings = JSONValue::MakeObject();
		settings.AddMember("compression", JSONValue::MakeString("none"));
		AssetDatabase::WriteImporterSettings(path, settings);
		Asset* asset = AssetDatabase::LoadAsset(path);
		if (!asset) {
			return nullptr;
		}
		TextureAsset* cube = static_cast<TextureAsset*>(asset);
		if (!cube->isCube || cube->size.x != PROBE_FACE_SIZE) {
			return cube;
		}
		for (u32 mip = 0; mip < cube->storedMipLevels; ++mip) {
			const u32 edge = Math::Max(1u, static_cast<u32>(PROBE_FACE_SIZE) >> mip);
			for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
				const Vec3 color = FaceColor(face);
				const u16 texel[4] = {Math::FloatToHalf(color.x), Math::FloatToHalf(color.y),
									  Math::FloatToHalf(color.z), Math::FloatToHalf(1.0f)};
				u8* out = cube->pixels.data() +
						  RadianceCubeFaceOffset(static_cast<u32>(PROBE_FACE_SIZE), cube->storedMipLevels, mip, face);
				for (u32 texelIndex = 0; texelIndex < edge * edge; ++texelIndex) {
					std::memcpy(out + static_cast<size_t>(texelIndex) * sizeof(texel), texel, sizeof(texel));
				}
			}
		}
		return cube;
	}

	Handle<ReflectionProbe> AddProbe(ShadingStage& stage, TextureAsset* cube, const Vec3& position,
									 const Vec3& boxSize, f32 importance = 1.0f) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Probe", stage.scene);
		object->SetWorldPosition(position);
		Handle<ReflectionProbe> probe = object->AddComponent<ReflectionProbe>();
		probe->SetBakedCubemap(Handle<TextureAsset>(cube));
		probe->SetBoxSize(boxSize);
		probe->SetImportance(importance);
		SceneSystem::EndFrame();
		return probe;
	}

	Guid LitShaderGuid() {
		Asset* shader = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
		return shader ? shader->assetId : Guid{};
	}

	void ExpectFace(const Vec3& measured, u32 face, const char* what) {
		const Vec3 expected = FaceColor(face);
		const bool wantR = expected.x > 0.4f;
		const bool wantG = expected.y > 0.4f;
		const bool wantB = expected.z > 0.4f;
		const f32 lit = Math::Max(measured.x, Math::Max(measured.y, measured.z));
		EXPECT_GT(lit, 40.0f) << what;
		const f32 floorLevel = lit * 0.5f;
		EXPECT_EQ(measured.x > floorLevel, wantR) << what << ": red channel";
		EXPECT_EQ(measured.y > floorLevel, wantG) << what << ": green channel";
		EXPECT_EQ(measured.z > floorLevel, wantB) << what << ": blue channel";
	}

} // anonymous namespace

class ReflectionProbeReadbackFixture : public MaterialReadbackFixture {};

TEST_F(ReflectionProbeReadbackFixture, ProbeInsideBoxReplacesEnvironmentsReflection) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	TextureAsset* cube = LoadRepaintedProbeCube(_root, "Probe.hdr");
	ASSERT_NE(cube, nullptr);
	ASSERT_TRUE(cube->isCube);
	ASSERT_EQ(cube->size.x, PROBE_FACE_SIZE);

	MaterialAsset* material = LoadMaterial("Mirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	Handle<ReflectionProbe> probe = AddProbe(stage, cube, Vec3(0.0f, 0.0f, 0.0f), Vec3(20.0f, 20.0f, 20.0f));
	probe->SetBoxProjection(false);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	ExpectFace(CenterTexel(pixels, size), FaceForDirection(Vec3(0.0f, 0.0f, 1.0f)), "the +Z face");

	TearDownStage(stage);
}

TEST_F(ReflectionProbeReadbackFixture, BoxProjectionHitsWall) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	TextureAsset* cube = LoadRepaintedProbeCube(_root, "Probe.hdr");
	ASSERT_NE(cube, nullptr);
	ASSERT_EQ(cube->size.x, PROBE_FACE_SIZE);

	MaterialAsset* material = LoadMaterial("Mirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	const Vec3 capture(0.0f, 5.0f, 0.0f);
	const Vec3 boxSize(20.0f, 20.0f, 1.0f);
	Handle<ReflectionProbe> probe = AddProbe(stage, cube, capture, boxSize);
	probe->SetBoxOffset(Vec3(0.0f, -5.0f, 0.0f));
	probe->SetBoxProjection(false);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 unprojected = CenterTexel(pixels, size);
	ExpectFace(unprojected, FaceForDirection(Vec3(0.0f, 0.0f, 1.0f)), "box projection off");

	probe->SetBoxProjection(true);
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 projected = CenterTexel(pixels, size);

	const Vec3 hit(0.0f, 0.0f, boxSize.z * 0.5f);
	ExpectFace(projected, FaceForDirection(hit - capture), "box projection on");
	EXPECT_NE(FaceForDirection(hit - capture), FaceForDirection(Vec3(0.0f, 0.0f, 1.0f)));

	TearDownStage(stage);
}

TEST_F(ReflectionProbeReadbackFixture, ImportantProbeWinsOverlap) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	TextureAsset* first = LoadRepaintedProbeCube(_root, "ProbeA.hdr");
	ASSERT_NE(first, nullptr);
	ASSERT_EQ(first->size.x, PROBE_FACE_SIZE);

	MaterialAsset* material = LoadMaterial("Mirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	Handle<ReflectionProbe> unimportant =
		AddProbe(stage, first, Vec3(0.0f, 0.0f, 0.0f), Vec3(20.0f, 20.0f, 20.0f), 1.0f);
	unimportant->SetBoxProjection(false);

	const Vec3 capture(0.0f, 5.0f, 0.0f);
	const Vec3 boxSize(20.0f, 20.0f, 1.0f);
	Handle<ReflectionProbe> important = AddProbe(stage, first, capture, boxSize, 8.0f);
	important->SetBoxOffset(Vec3(0.0f, -5.0f, 0.0f));
	important->SetBoxProjection(true);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	const Vec3 hit(0.0f, 0.0f, boxSize.z * 0.5f);
	ExpectFace(CenterTexel(pixels, size), FaceForDirection(hit - capture), "the important probe");

	TearDownStage(stage);
}

TEST_F(ReflectionProbeReadbackFixture, SurfaceOutsideEveryBoxStillReadsEnvironment) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	TextureAsset* cube = LoadRepaintedProbeCube(_root, "Probe.hdr");
	ASSERT_NE(cube, nullptr);
	ASSERT_EQ(cube->size.x, PROBE_FACE_SIZE);

	MaterialAsset* material = LoadMaterial("Mirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 before = CenterTexel(pixels, size);

	AddProbe(stage, cube, Vec3(0.0f, 100.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
	SceneSystem::EndFrame();

	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 after = CenterTexel(pixels, size);

	EXPECT_NEAR(after.x, before.x, 1.0f);
	EXPECT_NEAR(after.y, before.y, 1.0f);
	EXPECT_NEAR(after.z, before.z, 1.0f);

	TearDownStage(stage);
}

TEST_F(ReflectionProbeReadbackFixture, CubelessProbeFallsBack) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	TextureAsset* cube = LoadRepaintedProbeCube(_root, "Probe.hdr");
	ASSERT_NE(cube, nullptr);
	ASSERT_EQ(cube->size.x, PROBE_FACE_SIZE);

	MaterialAsset* material = LoadMaterial("Mirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	Handle<ReflectionProbe> probe = AddProbe(stage, cube, Vec3(0.0f, 0.0f, 0.0f), Vec3(20.0f, 20.0f, 20.0f));
	probe->SetBoxProjection(false);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ExpectFace(CenterTexel(pixels, size), FaceForDirection(Vec3(0.0f, 0.0f, 1.0f)), "the probe before it lost its cube");

	probe->SetBakedCubemap({});
	SceneSystem::EndFrame();

	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 after = CenterTexel(pixels, size);
	EXPECT_NEAR(after.x, after.y, 24.0f);
	EXPECT_NEAR(after.y, after.z, 24.0f);

	TearDownStage(stage);
}

TEST_F(ReflectionProbeReadbackFixture, CaptureReflectsEnvironment) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	TextureAsset* cube = LoadRepaintedProbeCube(_root, "Probe.hdr");
	ASSERT_NE(cube, nullptr);
	ASSERT_EQ(cube->size.x, PROBE_FACE_SIZE);
	MaterialAsset* material = LoadMaterial("Mirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);
	Handle<ReflectionProbe> probe = AddProbe(stage, cube, Vec3(0.0f, 0.0f, 0.0f), Vec3(20.0f, 20.0f, 20.0f));
	probe->SetBoxProjection(false);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ExpectFace(CenterTexel(pixels, size), 4, "the ordinary view");

	ViewDesc desc = ShadingTest::QuadView();
	desc.radianceCapture = true;
	RenderSystem::SubmitView(stage.target, desc);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTargetRadiance(stage.target, pixels, size));
	const u16* texels = reinterpret_cast<const u16*>(pixels.data());
	const size_t center = (static_cast<size_t>(size.y / 2) * size.x + size.x / 2) * 4;
	const Vec3 kept(Math::HalfToFloat(texels[center]), Math::HalfToFloat(texels[center + 1]), Math::HalfToFloat(texels[center + 2]));
	EXPECT_GT(kept.y, 0.0f);
	EXPECT_NEAR(kept.x, kept.y, kept.y * 0.1f);
	EXPECT_NEAR(kept.y, kept.z, kept.y * 0.1f);

	TearDownStage(stage);
}

TEST_F(ReflectionProbeReadbackFixture, ReimportedCubeReflects) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 cyan = FaceColor(4);
	const Vec3 red = FaceColor(0);
	WriteRadianceHDR(_root / "Probe.hdr", PROBE_FACE_SIZE * 4, PROBE_FACE_SIZE * 2, [&](f32, f32) { return cyan; });
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Probe.hdr"));
	ASSERT_NE(asset, nullptr);
	TextureAsset* cube = static_cast<TextureAsset*>(asset);
	ASSERT_EQ(cube->size.x, PROBE_FACE_SIZE);

	MaterialAsset* material = LoadMaterial("Mirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);
	Handle<ReflectionProbe> probe = AddProbe(stage, cube, Vec3(0.0f, 0.0f, 0.0f), Vec3(20.0f, 20.0f, 20.0f));
	probe->SetBoxProjection(false);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ExpectFace(CenterTexel(pixels, size), 4, "the first file");

	const u64 instance = cube->GetInstanceId();
	WriteRadianceHDR(_root / "Probe.hdr", PROBE_FACE_SIZE * 4, PROBE_FACE_SIZE * 2, [&](f32, f32) { return red; });
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Probe.hdr")));
	ASSERT_EQ(probe->GetBakedCubemap()->GetInstanceId(), instance);

	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ExpectFace(CenterTexel(pixels, size), 0, "the rewritten file");

	TearDownStage(stage);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(ReflectionProbeReadbackFixture, WrongSizeCubeSkipped) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 cyan = FaceColor(4);
	WriteRadianceHDR(_root / "Small.hdr", PROBE_FACE_SIZE * 2, PROBE_FACE_SIZE, [&](f32, f32) { return cyan; });
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Small.hdr"));
	ASSERT_NE(asset, nullptr);
	TextureAsset* cube = static_cast<TextureAsset*>(asset);
	ASSERT_TRUE(cube->isCube);
	ASSERT_EQ(cube->size.x, PROBE_FACE_SIZE / 2);

	MaterialAsset* material = LoadMaterial("Mirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);
	Handle<ReflectionProbe> probe = AddProbe(stage, cube, Vec3(0.0f, 0.0f, 0.0f), Vec3(20.0f, 20.0f, 20.0f));
	probe->SetBoxProjection(false);
	SceneSystem::EndFrame();

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<ShaderDiagnosticSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	bool rendered = true;
	for (int frame = 0; frame < 3; ++frame) {
		rendered = RenderStage(stage, pixels, size) && rendered;
	}

	LogManager::Terminate();
	const str captured = sink->Joined();
	sink.reset();
	JobSystem::Terminate();

	ASSERT_TRUE(rendered);
	u32 warnings = 0;
	for (size_t at = captured.find("Rebake it"); at != str::npos; at = captured.find("Rebake it", at + 1)) {
		++warnings;
	}
	EXPECT_EQ(warnings, 1u);
	const Vec3 measured = CenterTexel(pixels, size);
	const f32 lit = Math::Max(measured.x, Math::Max(measured.y, measured.z));
	EXPECT_GT(lit, 10.0f);
	EXPECT_GT(measured.x, lit * 0.5f);

	TearDownStage(stage);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(ReflectionProbeReadbackFixture, WrongFormatCubeSkipped) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 cyan = FaceColor(4);
	const Vec3 red = FaceColor(0);
	WriteRadianceHDR(_root / "Compressed.hdr", PROBE_FACE_SIZE * 4, PROBE_FACE_SIZE * 2, [&](f32, f32) { return cyan; });
	WriteRadianceHDR(_root / "Raw.hdr", PROBE_FACE_SIZE * 4, PROBE_FACE_SIZE * 2, [&](f32, f32) { return red; });
	const Path rawPath("project://Raw.hdr");
	ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(rawPath).IsValid());
	JSONValue uncompressed = JSONValue::MakeObject();
	uncompressed.AddMember("compression", JSONValue::MakeString("none"));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(rawPath, uncompressed));
	Asset* compressedAsset = AssetDatabase::LoadAsset(Path("project://Compressed.hdr"));
	Asset* rawAsset = AssetDatabase::LoadAsset(rawPath);
	ASSERT_NE(compressedAsset, nullptr);
	ASSERT_NE(rawAsset, nullptr);
	TextureAsset* compressed = static_cast<TextureAsset*>(compressedAsset);
	TextureAsset* raw = static_cast<TextureAsset*>(rawAsset);
	ASSERT_EQ(compressed->format, RHI::Format::BC6HUFloat);
	ASSERT_EQ(raw->format, RHI::Format::RGBA16Float);
	ASSERT_EQ(compressed->size.x, PROBE_FACE_SIZE);
	ASSERT_EQ(raw->size.x, PROBE_FACE_SIZE);

	MaterialAsset* material = LoadMaterial("Mirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);
	Handle<ReflectionProbe> first = AddProbe(stage, compressed, Vec3(0.0f, 0.0f, 0.0f), Vec3(20.0f, 20.0f, 20.0f), 1.0f);
	Handle<ReflectionProbe> second = AddProbe(stage, raw, Vec3(0.0f, 0.0f, 0.0f), Vec3(20.0f, 20.0f, 20.0f), 2.0f);
	first->SetBoxProjection(false);
	second->SetBoxProjection(false);
	SceneSystem::EndFrame();

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<ShaderDiagnosticSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	bool rendered = true;
	for (int frame = 0; frame < 3; ++frame) {
		rendered = RenderStage(stage, pixels, size) && rendered;
	}
	LogManager::Terminate();
	const str captured = sink->Joined();
	sink.reset();
	JobSystem::Terminate();

	ASSERT_TRUE(rendered);
	u32 warnings = 0;
	for (size_t at = captured.find("the same compression"); at != str::npos; at = captured.find("the same compression", at + 1)) {
		++warnings;
	}
	EXPECT_EQ(warnings, 1u);
	ExpectFace(CenterTexel(pixels, size), 4, "the compressed probe, the other skipped");

	JSONValue compressedNow = JSONValue::MakeObject();
	compressedNow.AddMember("compression", JSONValue::MakeString("highQuality"));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(rawPath, compressedNow));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(rawPath));
	ASSERT_EQ(raw->format, RHI::Format::BC6HUFloat);
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ExpectFace(CenterTexel(pixels, size), 0, "the more important probe, once it is the array's format");

	TearDownStage(stage);
	AssetDatabase::UnloadAsset(compressedAsset);
	AssetDatabase::UnloadAsset(rawAsset);
}
