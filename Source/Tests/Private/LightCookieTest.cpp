#include "TestsPCH.h"

#include "ShadingHarness.h"
#include "TestImages.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <cstring>
#include <vector>

using namespace Shalltear;
using ShadingTest::ArmDiagnosticSink;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::ShaderDiagnosticSink;
using ShadingTest::ShadingStage;

namespace {

	constexpr const char* ATTENUATION_SHADER =
		"#define SHALLTEAR_CUSTOM_LIGHTING\n"
		"#pragma shalltear_template lit\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 baseColor; };\n"
		"float3 ShalltearLightingFunction(ShalltearSurfaceInput input, ShalltearSurface surface, float3 normalWS, ShalltearLightSample light) {\n"
		"\treturn light.color * light.attenuation;\n"
		"}\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = baseColor.rgb;\n"
		"\tsurface.metallic = 0.0;\n"
		"}\n";

	constexpr i32 COOKIE_SIZE = 8;
	constexpr i32 FACE_SIZE = 4;

	Vec3 TexelAt(const Vector<u8>& pixels, Vec2I size, Vec2I at) {
		const size_t index = (static_cast<size_t>(at.y) * size.x + at.x) * 4;
		return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]), static_cast<f32>(pixels[index + 2]));
	}

	Vec2I ProjectToPixel(Vec3 worldPosition, Vec2I size) {
		const ViewDesc view = ShadingTest::QuadView();
		const Vec4 clip = (view.projection * view.view) * Vec4(worldPosition, 1.0f);
		return Vec2I{static_cast<i32>((clip.x / clip.w + 1.0f) * 0.5f * static_cast<f32>(size.x)),
					 static_cast<i32>((1.0f - clip.y / clip.w) * 0.5f * static_cast<f32>(size.y))};
	}

	std::vector<u8> QuadrantCookie() {
		std::vector<u8> rgba(static_cast<size_t>(COOKIE_SIZE) * COOKIE_SIZE * 4, 255);
		for (i32 y = 0; y < COOKIE_SIZE / 2; ++y) {
			for (i32 x = 0; x < COOKIE_SIZE / 2; ++x) {
				u8* texel = &rgba[(static_cast<size_t>(y) * COOKIE_SIZE + x) * 4];
				texel[0] = texel[1] = texel[2] = 0;
			}
		}
		return rgba;
	}

	std::vector<u8> PositiveXDarkStrip() {
		std::vector<u8> rgba(static_cast<size_t>(FACE_SIZE) * 6 * FACE_SIZE * 4, 255);
		for (i32 y = 0; y < FACE_SIZE; ++y) {
			for (i32 x = 0; x < FACE_SIZE; ++x) {
				u8* texel = &rgba[(static_cast<size_t>(y) * FACE_SIZE * 6 + x) * 4];
				texel[0] = texel[1] = texel[2] = 0;
			}
		}
		return rgba;
	}

	class LightCookieFixture : public MaterialReadbackFixture {
	protected:
		MaterialAsset* AttenuationMaterial() {
			WriteProjectFile("Attenuation.hlsl", ATTENUATION_SHADER);
			Asset* shader = AssetDatabase::LoadAsset(Path("project://Attenuation.hlsl"));
			if (!shader) {
				return nullptr;
			}
			return LoadMaterial("Attenuation.mat", shader->assetId, "\t\"colors\": { \"baseColor\": [0, 0, 0, 1] }");
		}

		TextureAsset* Cookie(const char* file, i32 width, i32 height, const std::vector<u8>& rgba, bool cube) {
			TestImages::WriteTga(_root / file, static_cast<u16>(width), static_cast<u16>(height), rgba.data());
			const Path path(str("project://") + file);
			AssetDatabase::EnsureAssetIdentity(path);
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("sRGB", JSONValue::MakeBool(false));
			settings.AddMember("compression", JSONValue::MakeString("none"));
			settings.AddMember("generateMips", JSONValue::MakeBool(false));
			settings.AddMember("filter", JSONValue::MakeString("point"));
			if (cube) {
				settings.AddMember("shape", JSONValue::MakeString("cube"));
				settings.AddMember("convolveIBL", JSONValue::MakeBool(false));
			}
			if (!AssetDatabase::WriteImporterSettings(path, settings) || !AssetDatabase::ReimportAsset(path)) {
				return nullptr;
			}
			return static_cast<TextureAsset*>(AssetDatabase::LoadAsset(path));
		}

		Handle<Light> AddLight(ShadingStage& stage, LightType type, Vec3 position, f32 yawDeg) {
			Handle<SceneObject> object = SceneSystem::CreateObject("Light", stage.scene);
			object->SetLocalPosition(position);
			object->SetLocalRotation(Quat::AngleAxis(yawDeg, Vec3::Up()));
			Handle<Light> light = object->AddComponent<Light>();
			light->SetLightType(type);
			light->SetColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
			light->SetIntensity(1.0f);
			light->SetRange(10.0f);
			light->SetSpotAngleDeg(90.0f);
			light->SetCastShadows(false);
			SceneSystem::EndFrame();
			return light;
		}

		Handle<SceneObject> AddMeshQuad(ShadingStage& stage, MaterialAsset* material, Vec3 position, Vec3 scale, u32 layers) {
			Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
			Handle<SceneObject> object = SceneSystem::CreateObject("Quad", stage.scene);
			object->SetLocalPosition(position);
			object->SetLocalScale(scale);
			Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			renderer->SetRenderingLayerMask(layers);
			return object;
		}

		bool Render(const ShadingStage& stage, Vector<u8>& pixels, Vec2I& size) {
			SceneSystem::EndFrame();
			return RenderStage(stage, pixels, size);
		}

		bool Ready() {
			return RenderSystem::IsRenderingAvailable() && CompilerAvailable();
		}
	};

	struct CookieProbe {
		Vec3 position;
		bool dark;
	};

	void ExpectCookie(const Vector<u8>& cookied, const Vector<u8>& uncookied, const Vector<u8>& unlit, Vec2I size,
					  std::span<const CookieProbe> probes) {
		for (const CookieProbe& probe : probes) {
			const Vec2I at = ProjectToPixel(probe.position, size);
			const Vec3 measured = TexelAt(cookied, size, at);
			const Vec3 expected = TexelAt(probe.dark ? unlit : uncookied, size, at);
			EXPECT_EQ(measured, expected) << probe.position.x << ", " << probe.position.y;
			EXPECT_GT(TexelAt(uncookied, size, at).x, TexelAt(unlit, size, at).x + 8.0f) << probe.position.x << ", " << probe.position.y;
		}
	}

} // namespace

TEST_F(MaterialReadbackFixture, TableIndexReusedAfterFramesInFlight) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	RHI::TextureDesc desc;
	desc.size = {4, 4};
	desc.format = RHI::Format::RGBA8Unorm;
	desc.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;

	const RHI::TextureHandle first = device->CreateTextureResource(desc);
	const u32 index = device->GetTableIndex(first);
	ASSERT_NE(index, RHI::TABLE_NONE);
	EXPECT_EQ(device->GetTableIndex(first), index);

	device->DestroyResource(first);
	EXPECT_EQ(device->GetTableIndex(first), RHI::TABLE_NONE);
	const RHI::TextureHandle during = device->CreateTextureResource(desc);
	EXPECT_NE(device->GetTableIndex(during), index);

	device->BeginFrame();
	device->EndFrame();
	const RHI::TextureHandle oneFrameLater = device->CreateTextureResource(desc);
	EXPECT_NE(device->GetTableIndex(oneFrameLater), index);

	for (u32 frame = 1; frame < RHI::FRAMES_IN_FLIGHT; ++frame) {
		device->BeginFrame();
		device->EndFrame();
	}
	const RHI::TextureHandle retired = device->CreateTextureResource(desc);
	EXPECT_EQ(device->GetTableIndex(retired), index);

	device->DestroyResource(during);
	device->DestroyResource(oneFrameLater);
	device->DestroyResource(retired);
}

TEST_F(MaterialReadbackFixture, TableKindsIndexApart) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	RHI::TextureDesc flat;
	flat.size = {4, 4};
	flat.format = RHI::Format::RGBA8Unorm;
	flat.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
	RHI::TextureDesc cube = flat;
	cube.cube = true;
	cube.arrayLayers = 6;
	RHI::TextureDesc depth = flat;
	depth.format = RHI::Format::D32Float;
	depth.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::DepthStencil;

	const RHI::TextureHandle flatTexture = device->CreateTextureResource(flat);
	const RHI::TextureHandle cubeTexture = device->CreateTextureResource(cube);
	const RHI::TextureHandle depthTexture = device->CreateTextureResource(depth);
	const RHI::SamplerHandle sampler = device->CreateSamplerResource(RHI::SamplerDesc{});

	const u32 flatIndex = device->GetTableIndex(flatTexture);
	const u32 cubeIndex = device->GetTableIndex(cubeTexture);
	const u32 samplerIndex = device->GetTableIndex(sampler);
	EXPECT_NE(flatIndex, RHI::TABLE_NONE);
	EXPECT_EQ(cubeIndex, flatIndex);
	EXPECT_NE(samplerIndex, RHI::TABLE_NONE);
	EXPECT_EQ(device->GetTableIndex(sampler), samplerIndex);
	EXPECT_EQ(device->GetTableIndex(depthTexture), RHI::TABLE_NONE);

	device->DestroyResource(flatTexture);
	device->DestroyResource(cubeTexture);
	device->DestroyResource(depthTexture);
	device->DestroyResource(sampler);
}

TEST_F(LightCookieFixture, DirectionalCookieTilesAtSizeAndOffset) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler" SLT_TEST_DLL_SUFFIX;
	}
	MaterialAsset* material = AttenuationMaterial();
	TextureAsset* cookie = Cookie("Quadrants.tga", COOKIE_SIZE, COOKIE_SIZE, QuadrantCookie(), false);
	ASSERT_NE(material, nullptr);
	ASSERT_NE(cookie, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	stage.sun->SetEnabled(false);
	Handle<Light> light = AddLight(stage, LightType::Directional, Vec3(0.0f, 0.0f, 1.0f), 180.0f);
	light->SetCookieSize(Vec2(2.0f, 2.0f));
	light->SetCookieOffset(Vec2(0.5f, 0.0f));

	Vector<u8> uncookied{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> cookied{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> unlit{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, uncookied, size));
	light->SetCookie(Handle<TextureAsset>(cookie));
	ASSERT_TRUE(Render(stage, cookied, size));
	light->SetEnabled(false);
	ASSERT_TRUE(Render(stage, unlit, size));

	const CookieProbe probes[] = {
		{Vec3(0.0f, 0.5f, 0.0f), true},
		{Vec3(0.0f, -0.5f, 0.0f), false},
		{Vec3(-1.0f, 0.5f, 0.0f), false},
		{Vec3(1.0f, 0.5f, 0.0f), false},
	};
	ExpectCookie(cookied, uncookied, unlit, size, probes);

	TearDownStage(stage);
}

TEST_F(LightCookieFixture, SpotCookieFollowsCone) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler" SLT_TEST_DLL_SUFFIX;
	}
	MaterialAsset* material = AttenuationMaterial();
	TextureAsset* cookie = Cookie("Quadrants.tga", COOKIE_SIZE, COOKIE_SIZE, QuadrantCookie(), false);
	ASSERT_NE(material, nullptr);
	ASSERT_NE(cookie, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	stage.sun->SetEnabled(false);
	Handle<Light> light = AddLight(stage, LightType::Spot, Vec3(0.0f, 0.0f, 2.0f), 180.0f);

	Vector<u8> uncookied{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> cookied{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> unlit{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, uncookied, size));
	light->SetCookie(Handle<TextureAsset>(cookie));
	ASSERT_TRUE(Render(stage, cookied, size));
	light->SetEnabled(false);
	ASSERT_TRUE(Render(stage, unlit, size));

	const CookieProbe probes[] = {
		{Vec3(0.6f, 0.6f, 0.0f), true},
		{Vec3(-0.6f, 0.6f, 0.0f), false},
		{Vec3(0.6f, -0.6f, 0.0f), false},
		{Vec3(-0.6f, -0.6f, 0.0f), false},
	};
	ExpectCookie(cookied, uncookied, unlit, size, probes);

	TearDownStage(stage);
}

TEST_F(LightCookieFixture, PointCookieTurnsWithLight) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler" SLT_TEST_DLL_SUFFIX;
	}
	MaterialAsset* material = AttenuationMaterial();
	TextureAsset* cookie = Cookie("Faces.tga", FACE_SIZE * 6, FACE_SIZE, PositiveXDarkStrip(), true);
	ASSERT_NE(material, nullptr);
	ASSERT_NE(cookie, nullptr);
	ASSERT_TRUE(cookie->isCube);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	stage.sun->SetEnabled(false);
	Handle<Light> light = AddLight(stage, LightType::Point, Vec3(0.0f, 0.0f, 0.5f), 0.0f);

	Vector<u8> uncookied{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> cookied{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> unlit{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, uncookied, size));
	light->SetCookie(Handle<TextureAsset>(cookie));
	ASSERT_TRUE(Render(stage, cookied, size));
	light->SetEnabled(false);
	ASSERT_TRUE(Render(stage, unlit, size));
	const CookieProbe facing[] = {
		{Vec3(1.0f, 0.0f, 0.0f), true},
		{Vec3(-1.0f, 0.0f, 0.0f), false},
		{Vec3(0.0f, 1.0f, 0.0f), false},
	};
	ExpectCookie(cookied, uncookied, unlit, size, facing);

	light->SetEnabled(true);
	light->GetOwner()->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
	ASSERT_TRUE(Render(stage, cookied, size));
	const CookieProbe turned[] = {
		{Vec3(-1.0f, 0.0f, 0.0f), true},
		{Vec3(1.0f, 0.0f, 0.0f), false},
	};
	ExpectCookie(cookied, uncookied, unlit, size, turned);

	TearDownStage(stage);
}

TEST_F(LightCookieFixture, MismatchedCookieShapeIgnored) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler" SLT_TEST_DLL_SUFFIX;
	}
	MaterialAsset* material = AttenuationMaterial();
	TextureAsset* flat = Cookie("Quadrants.tga", COOKIE_SIZE, COOKIE_SIZE, QuadrantCookie(), false);
	ASSERT_NE(material, nullptr);
	ASSERT_NE(flat, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	stage.sun->SetEnabled(false);
	Handle<Light> light = AddLight(stage, LightType::Point, Vec3(0.0f, 0.0f, 0.5f), 0.0f);

	Vector<u8> uncookied{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> cookied{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, uncookied, size));
	light->SetCookie(Handle<TextureAsset>(flat));
	ASSERT_TRUE(Render(stage, cookied, size));
	EXPECT_EQ(std::memcmp(cookied.data(), uncookied.data(), cookied.size()), 0);

	TearDownStage(stage);
}

TEST_F(LightCookieFixture, CookiedFramesReadIdentical) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler" SLT_TEST_DLL_SUFFIX;
	}
	MaterialAsset* material = AttenuationMaterial();
	TextureAsset* cookie = Cookie("Quadrants.tga", COOKIE_SIZE, COOKIE_SIZE, QuadrantCookie(), false);
	ASSERT_NE(material, nullptr);
	ASSERT_NE(cookie, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	stage.sun->SetCookie(Handle<TextureAsset>(cookie));
	Handle<Light> light = AddLight(stage, LightType::Spot, Vec3(0.0f, 0.0f, 2.0f), 180.0f);
	light->SetCookie(Handle<TextureAsset>(cookie));

	Vector<u8> first{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> second{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, first, size));
	ASSERT_TRUE(Render(stage, second, size));
	ASSERT_EQ(first.size(), second.size());
	EXPECT_EQ(std::memcmp(first.data(), second.data(), first.size()), 0);

	TearDownStage(stage);
}

TEST_F(LightCookieFixture, UnloadedCookieLeavesLightPlain) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler" SLT_TEST_DLL_SUFFIX;
	}
	MaterialAsset* material = AttenuationMaterial();
	TextureAsset* cookie = Cookie("Quadrants.tga", COOKIE_SIZE, COOKIE_SIZE, QuadrantCookie(), false);
	ASSERT_NE(material, nullptr);
	ASSERT_NE(cookie, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	stage.sun->SetEnabled(false);
	Handle<Light> light = AddLight(stage, LightType::Spot, Vec3(0.0f, 0.0f, 2.0f), 180.0f);
	Handle<Light> other = AddLight(stage, LightType::Spot, Vec3(0.0f, 0.0f, 2.0f), 180.0f);
	other->SetColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

	Vector<u8> uncookied{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> cookied{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> after{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, uncookied, size));
	light->SetCookie(Handle<TextureAsset>(cookie));
	ASSERT_TRUE(Render(stage, cookied, size));
	EXPECT_NE(std::memcmp(cookied.data(), uncookied.data(), cookied.size()), 0);
	const u32 deadIndex = RenderSystem::GetDevice()->GetTableIndex(cookie->texture);
	ASSERT_NE(deadIndex, RHI::TABLE_NONE);

	AssetDatabase::UnloadAsset(cookie);
	AssetDatabase::CollectGarbage();
	EXPECT_FALSE(light->GetCookie().IsValid());
	ASSERT_TRUE(Render(stage, after, size));
	EXPECT_EQ(std::memcmp(after.data(), uncookied.data(), after.size()), 0);

	TextureAsset* replacement = Cookie("Replacement.tga", COOKIE_SIZE, COOKIE_SIZE, QuadrantCookie(), false);
	ASSERT_NE(replacement, nullptr);
	other->SetCookie(Handle<TextureAsset>(replacement));
	for (u32 frame = 0; frame < RHI::FRAMES_IN_FLIGHT + 1; ++frame) {
		ASSERT_TRUE(Render(stage, after, size));
	}
	EXPECT_EQ(RenderSystem::GetDevice()->GetTableIndex(replacement->texture), deadIndex);
	EXPECT_EQ(std::memcmp(after.data(), uncookied.data(), after.size()), 0);

	TearDownStage(stage);
}

TEST_F(LightCookieFixture, LightSkipsOtherLayerShadowStillCasts) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler" SLT_TEST_DLL_SUFFIX;
	}
	MaterialAsset* material = AttenuationMaterial();
	ASSERT_NE(material, nullptr);
	constexpr u32 LIT_LAYER = 1u << 1;
	constexpr u32 OTHER_LAYER = 1u << 2;

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);
	stage.quad->SetLocalPosition(Vec3(-0.75f, 0.0f, 0.0f));
	stage.quad->SetLocalScale(Vec3(1.4f, 3.0f, 1.0f));
	stage.quad->GetComponent<MeshRenderer>()->SetRenderingLayerMask(LIT_LAYER);
	AddMeshQuad(stage, material, Vec3(0.75f, 0.0f, 0.0f), Vec3(1.4f, 3.0f, 1.0f), OTHER_LAYER);
	Handle<SceneObject> caster = AddMeshQuad(stage, material, Vec3(-0.75f, 0.0f, 0.5f), Vec3(0.8f, 0.8f, 1.0f), OTHER_LAYER);
	caster->GetComponent<MeshRenderer>()->SetCastShadows(ShadowCasting::ShadowsOnly);

	Handle<Light> light = AddLight(stage, LightType::Directional, Vec3(0.0f, 0.0f, 2.0f), 180.0f);
	light->SetCastShadows(true);
	light->SetRenderingLayerMask(LIT_LAYER);

	Vector<u8> layered{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> casterExcluded{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> casterGone{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> everyLayer{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> unlit{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, layered, size));
	light->SetShadowLayerMask(LIT_LAYER);
	ASSERT_TRUE(Render(stage, casterExcluded, size));
	light->SetShadowLayerMask(~0u);
	caster->SetActive(false);
	ASSERT_TRUE(Render(stage, casterGone, size));
	light->SetRenderingLayerMask(~0u);
	ASSERT_TRUE(Render(stage, everyLayer, size));
	light->SetEnabled(false);
	ASSERT_TRUE(Render(stage, unlit, size));

	const Vec2I shadowed = ProjectToPixel(Vec3(-0.75f, 0.0f, 0.0f), size);
	const Vec2I open = ProjectToPixel(Vec3(-0.75f, -1.1f, 0.0f), size);
	const Vec2I otherLayer = ProjectToPixel(Vec3(0.75f, 0.0f, 0.0f), size);

	EXPECT_GT(TexelAt(casterGone, size, open).x, TexelAt(unlit, size, open).x + 8.0f);
	EXPECT_EQ(TexelAt(layered, size, open), TexelAt(casterGone, size, open));
	EXPECT_EQ(TexelAt(layered, size, otherLayer), TexelAt(unlit, size, otherLayer));
	EXPECT_GT(TexelAt(everyLayer, size, otherLayer).x, TexelAt(unlit, size, otherLayer).x + 8.0f);
	EXPECT_EQ(TexelAt(layered, size, shadowed), TexelAt(unlit, size, shadowed));
	EXPECT_EQ(TexelAt(casterExcluded, size, shadowed), TexelAt(casterGone, size, shadowed));
	EXPECT_GT(TexelAt(casterGone, size, shadowed).x, TexelAt(unlit, size, shadowed).x + 8.0f);

	TearDownStage(stage);
}

TEST_F(LightCookieFixture, DefaultLightReachesHighLayers) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler" SLT_TEST_DLL_SUFFIX;
	}
	MaterialAsset* material = AttenuationMaterial();
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	stage.sun->SetEnabled(false);
	AddLight(stage, LightType::Spot, Vec3(0.0f, 0.0f, 2.0f), 180.0f);
	AddLight(stage, LightType::Directional, Vec3(0.0f, 0.0f, 2.0f), 150.0f);

	Vector<u8> layerZero{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> highLayer{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, layerZero, size));
	stage.quad->GetComponent<MeshRenderer>()->SetRenderingLayerMask(1u << 9);
	ASSERT_TRUE(Render(stage, highLayer, size));
	ASSERT_EQ(layerZero.size(), highLayer.size());
	EXPECT_EQ(std::memcmp(layerZero.data(), highLayer.data(), layerZero.size()), 0);

	TearDownStage(stage);
}

TEST_F(LightCookieFixture, RuntimeArraysOnlyInTableSet) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler" SLT_TEST_DLL_SUFFIX;
	}
	const str vertex = "struct VSOutput { float4 positionCS : SV_Position; };\n"
					   "VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
					   "\tVSOutput output; output.positionCS = float4(float(vertexId), 0, 0, 1); return output;\n"
					   "}\n";
	WriteProjectFile("Stray.hlsl", "[[vk::binding(0, 2)]] Texture2D<float4> stray[];\n" + vertex +
									   "float4 PSMain(VSOutput input) : SV_Target { return stray[0].Load(int3(0, 0, 0)); }\n");
	WriteProjectFile("WrongKind.hlsl", "[[vk::binding(1, 3)]] TextureCube<float4> wrong[];\n"
									   "[[vk::binding(0, 1)]] SamplerState pointSampler;\n" +
										   vertex +
										   "float4 PSMain(VSOutput input) : SV_Target { return wrong[0].SampleLevel(pointSampler, float3(0, 0, 1), 0); }\n");
	WriteProjectFile("Table.hlsl", "#include \"TextureTable.hlsli\"\n" + vertex +
									   "float4 PSMain(VSOutput input) : SV_Target {\n"
									   "\treturn ShalltearTableSample2D(0u, float2(0.5, 0.5), float2(0.0, 0.0), float2(0.0, 0.0));\n"
									   "}\n");

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<ShaderDiagnosticSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));

	ShaderAsset stray;
	ShaderAsset wrongKind;
	ShaderAsset table;
	const bool strayImported = stray.Import(FileSystem::Resolve(Path("project://Stray.hlsl")));
	const bool wrongKindImported = wrongKind.Import(FileSystem::Resolve(Path("project://WrongKind.hlsl")));
	const bool tableImported = table.Import(FileSystem::Resolve(Path("project://Table.hlsl")));

	LogManager::Terminate();
	const str captured = sink->Joined();
	sink.reset();
	JobSystem::Terminate();

	EXPECT_FALSE(strayImported);
	EXPECT_FALSE(wrongKindImported);
	EXPECT_TRUE(tableImported);
	EXPECT_NE(captured.find("'stray' is an array of descriptors"), str::npos) << captured;
	EXPECT_NE(captured.find("'wrong' doesn't match texture table binding 1"), str::npos) << captured;
}
