#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Graphics/FullScreenPass.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Graphics/TextureCodecs.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Editor/EditorAssetWatch.h"
#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <fstream>
#include <regex>

using namespace Shalltear;
using namespace UITest;

namespace {

	class StubPreviewAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(StubPreviewAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_END_REFLECT()

		StringHash GetAssetTypeId() const override { return StubPreviewAsset::GetReflectType()->name; }
	};

	class SecondStubPreviewAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(SecondStubPreviewAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_END_REFLECT()

		StringHash GetAssetTypeId() const override { return SecondStubPreviewAsset::GetReflectType()->name; }
	};

	class PreviewRegistryTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			StubPreviewAsset::GetReflectType();
			SecondStubPreviewAsset::GetReflectType();
			EditorPreviews::Initialize();
		}
		void TearDown() override { EditorPreviews::Terminate(); }
	};

	std::filesystem::path PreviewRepoDir(const char* relative) {
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

} // anonymous namespace

TEST_F(PreviewRegistryTest, DecliningProducerEqualsNone) {
	StubPreviewAsset asset;
	AssetPreviewImage image;

	EXPECT_FALSE(EditorPreviews::Produce({Guid{}, &asset}, image));

	EditorPreviews::RegisterProducer(StringHash("StubPreviewAsset"),
									 [](const AssetPreviewRequest&, AssetPreviewImage&) { return false; });
	EXPECT_FALSE(EditorPreviews::Produce({Guid{}, &asset}, image));

	EditorPreviews::RegisterProducer(StringHash("StubPreviewAsset"),
									 [](const AssetPreviewRequest&, AssetPreviewImage& out) {
										 out.size = {64, 32};
										 out.detail = "stub";
										 return true;
									 });
	ASSERT_TRUE(EditorPreviews::Produce({Guid{}, &asset}, image));
	EXPECT_EQ(image.size, (Vec2I{64, 32}));
	EXPECT_EQ(image.detail, "stub");
}

TEST_F(PreviewRegistryTest, ADeclineLeavesNoPartialImageBehind) {
	StubPreviewAsset asset;
	EditorPreviews::RegisterProducer(StringHash("StubPreviewAsset"),
									 [](const AssetPreviewRequest&, AssetPreviewImage& out) {
										 out.size = {8, 8};
										 out.detail = "half-written";
										 return false;
									 });
	AssetPreviewImage image;
	image.detail = "stale";
	EXPECT_FALSE(EditorPreviews::Produce({Guid{}, &asset}, image));
	EXPECT_TRUE(image.detail.empty());
	EXPECT_EQ(image.size, (Vec2I{}));
}

TEST_F(PreviewRegistryTest, SweepSparesBuiltInProducers) {
	int builtInOwner = 0;
	(void)builtInOwner;
	EditorPreviews::RegisterProducer(StringHash("StubPreviewAsset"), [](const AssetPreviewRequest&, AssetPreviewImage&) { return true; }, nullptr);

	int moduleImage = 0;
	const void* moduleBase = &moduleImage;

	StubPreviewAsset builtInSubject;
	SecondStubPreviewAsset moduleSubject;
	AssetPreviewImage image;

	for (int cycle = 0; cycle < 2; ++cycle) {
		EditorPreviews::RegisterProducer(StringHash("SecondStubPreviewAsset"), [](const AssetPreviewRequest&, AssetPreviewImage&) { return true; }, moduleBase);
		EXPECT_TRUE(EditorPreviews::Produce({Guid{}, &moduleSubject}, image)) << "cycle " << cycle;

		EXPECT_EQ(EditorPreviews::UnregisterOwnedBy(moduleBase), 1u) << "cycle " << cycle;
		EXPECT_FALSE(EditorPreviews::Produce({Guid{}, &moduleSubject}, image)) << "cycle " << cycle;
		EXPECT_TRUE(EditorPreviews::Produce({Guid{}, &builtInSubject}, image)) << "cycle " << cycle;
	}

	EXPECT_EQ(EditorPreviews::UnregisterOwnedBy(nullptr), 0u);
	EXPECT_TRUE(EditorPreviews::Produce({Guid{}, &builtInSubject}, image));
}

TEST_F(PreviewRegistryTest, TheSweepCountsEveryProducerItRemoved) {
	int moduleImage = 0;
	const void* moduleBase = &moduleImage;
	EditorPreviews::RegisterProducer(StringHash("StubPreviewAsset"), [](const AssetPreviewRequest&, AssetPreviewImage&) { return true; }, moduleBase);
	EditorPreviews::RegisterProducer(StringHash("SecondStubPreviewAsset"), [](const AssetPreviewRequest&, AssetPreviewImage&) { return true; }, moduleBase);
	EXPECT_EQ(EditorPreviews::UnregisterOwnedBy(moduleBase), 2u);
	EXPECT_EQ(EditorPreviews::UnregisterOwnedBy(moduleBase), 0u);
}

TEST(AssetPreviewOpsTest, FitImageLetterboxesWithoutEverDistortingAspect) {
	Vec2 wide = EditorPreviewOps::FitImage({200, 100}, {100.0f, 100.0f});
	EXPECT_FLOAT_EQ(wide.x, 100.0f);
	EXPECT_FLOAT_EQ(wide.y, 50.0f);

	Vec2 tall = EditorPreviewOps::FitImage({100, 200}, {100.0f, 100.0f});
	EXPECT_FLOAT_EQ(tall.x, 50.0f);
	EXPECT_FLOAT_EQ(tall.y, 100.0f);

	Vec2 small = EditorPreviewOps::FitImage({4, 4}, {80.0f, 200.0f});
	EXPECT_FLOAT_EQ(small.x, 80.0f);
	EXPECT_FLOAT_EQ(small.y, 80.0f);

	for (Vec2I size : {Vec2I{200, 100}, Vec2I{100, 200}, Vec2I{4, 4}, Vec2I{1920, 1080}}) {
		Vec2 fitted = EditorPreviewOps::FitImage(size, {137.0f, 91.0f});
		ASSERT_GT(fitted.y, 0.0f);
		EXPECT_NEAR(fitted.x / fitted.y,
					static_cast<f32>(size.x) / static_cast<f32>(size.y), 0.001f);
		EXPECT_LE(fitted.x, 137.0f + 0.001f);
		EXPECT_LE(fitted.y, 91.0f + 0.001f);
	}
}

TEST(AssetPreviewOpsTest, FitImageRefusesDegenerateBox) {
	EXPECT_EQ(EditorPreviewOps::FitImage({64, 64}, {0.0f, 0.0f}), (Vec2{}));
	EXPECT_EQ(EditorPreviewOps::FitImage({64, 64}, {100.0f, 0.0f}), (Vec2{}));
	EXPECT_EQ(EditorPreviewOps::FitImage({0, 0}, {100.0f, 100.0f}), (Vec2{}));
	EXPECT_EQ(EditorPreviewOps::FitImage({-8, 8}, {100.0f, 100.0f}), (Vec2{}));
}

TEST(AssetPreviewOpsTest, DescribeTextureNamesDimensionsAndFormat) {
	EXPECT_EQ(EditorPreviewOps::DescribeTexture({2048, 1024}, RHI::Format::RGBA8SRGB),
			  "2048 x 1024  RGBA8SRGB");
	EXPECT_EQ(EditorPreviewOps::DescribeTexture({8, 8}, RHI::Format::RGBA16Float),
			  "8 x 8  RGBA16Float");
	EXPECT_EQ(EditorPreviewOps::DescribeTexture({1, 1}, RHI::Format::Unknown), "1 x 1  Unknown");
}

TEST_F(PreviewRegistryTest, TextureProducerDeclinesEmpty) {
	EditorPreviews::RegisterBuiltIns();

	TextureAsset texture;
	texture.size = {256, 128};
	texture.format = RHI::Format::RGBA8SRGB;

	texture.pixels.assign(static_cast<size_t>(256 * 128 * 4), u8{255});

	AssetPreviewImage image;
	ASSERT_TRUE(EditorPreviews::Produce({Guid{}, &texture}, image));
	EXPECT_EQ(image.size, (Vec2I{256, 128}));
	EXPECT_EQ(image.detail, "256 x 128  RGBA8SRGB");
	EXPECT_FALSE(image.texture.IsValid());
	EXPECT_EQ(image.pixels.size(), texture.pixels.size());
	EXPECT_EQ(image.pixelsFormat, RHI::Format::RGBA8SRGB);

	TextureAsset empty;
	EXPECT_FALSE(EditorPreviews::Produce({Guid{}, &empty}, image));

	TextureAsset truncated;
	truncated.size = {256, 128};
	truncated.format = RHI::Format::RGBA8SRGB;
	EXPECT_FALSE(EditorPreviews::Produce({Guid{}, &truncated}, image));
}

TEST_F(PreviewRegistryTest, CompressedTexturesPictureIsDecodedTopLevel) {
	EditorPreviews::RegisterBuiltIns();
	auto compressed = [](TextureAsset& texture, RHI::Format format, TexelMeaning meaning, std::initializer_list<u8> rgba) {
		texture.size = {8, 8};
		Vector<u8> source{GetMemoryResource(MemTag::Asset)};
		for (u32 i = 0; i < 64; ++i) {
			source.insert(source.end(), rgba.begin(), rgba.end());
		}
		TextureEncodeInput input;
		input.texels = source;
		input.size = texture.size;
		input.mipLevels = 4;
		input.meaning = meaning;
		const TextureCodec* codec = TextureCodecs::Find(format);
		if (codec && codec->encode(format, input, texture.pixels)) {
			texture.format = format;
			texture.storedMipLevels = 4;
		}
	};

	TextureAsset color;
	compressed(color, RHI::Format::BC7SRGB, TexelMeaning::SRGBColor, {200, 120, 40, 255});
	ASSERT_EQ(color.format, RHI::Format::BC7SRGB);
	AssetPreviewImage image;
	ASSERT_TRUE(EditorPreviews::Produce({Guid{}, &color}, image));
	EXPECT_EQ(image.detail, "8 x 8  BC7SRGB");
	EXPECT_EQ(image.pixelsFormat, RHI::Format::RGBA8SRGB);
	ASSERT_EQ(image.pixels.size(), static_cast<size_t>(8 * 8 * 4));
	EXPECT_EQ(image.pixels.data(), image.decodedPixels.data());
	EXPECT_NEAR(image.pixels[0], 200, 2);
	EXPECT_NEAR(image.pixels[1], 120, 2);
	EXPECT_NEAR(image.pixels[2], 40, 2);

	TextureAsset normal;
	compressed(normal, RHI::Format::BC5Unorm, TexelMeaning::Normal, {204, 89, 222, 255});
	ASSERT_TRUE(EditorPreviews::Produce({Guid{}, &normal}, image));
	EXPECT_EQ(image.pixelsFormat, RHI::Format::RGBA8Unorm);
	EXPECT_NEAR(image.pixels[0], 204, 2);
	EXPECT_NEAR(image.pixels[1], 89, 2);
	EXPECT_NEAR(image.pixels[2], 222, 3);

	TextureAsset hdr;
	hdr.size = {8, 8};
	{
		Vector<u8> source{GetMemoryResource(MemTag::Asset)};
		const u16 texel[4] = {Math::FloatToHalf(3.0f), Math::FloatToHalf(0.5f), Math::FloatToHalf(0.25f), Math::FloatToHalf(1.0f)};
		for (u32 i = 0; i < 64; ++i) {
			source.insert(source.end(), reinterpret_cast<const u8*>(texel), reinterpret_cast<const u8*>(texel) + sizeof(texel));
		}
		TextureEncodeInput input;
		input.texels = source;
		input.texelFormat = RHI::Format::RGBA16Float;
		input.size = hdr.size;
		input.mipLevels = 4;
		input.meaning = TexelMeaning::Linear;
		const TextureCodec* codec = TextureCodecs::Find(RHI::Format::BC6HUFloat);
		ASSERT_NE(codec, nullptr);
		ASSERT_TRUE(codec->encode(RHI::Format::BC6HUFloat, input, hdr.pixels));
		hdr.format = RHI::Format::BC6HUFloat;
		hdr.storedMipLevels = 4;
	}
	ASSERT_TRUE(EditorPreviews::Produce({Guid{}, &hdr}, image));
	EXPECT_EQ(image.detail, "8 x 8  BC6HUFloat");
	EXPECT_EQ(image.pixelsFormat, RHI::Format::RGBA16Float);
	ASSERT_EQ(image.pixels.size(), static_cast<size_t>(8 * 8 * 8));
	u16 red = 0;
	u16 green = 0;
	std::memcpy(&red, image.pixels.data(), sizeof(red));
	std::memcpy(&green, image.pixels.data() + 2, sizeof(green));
	EXPECT_NEAR(Math::HalfToFloat(red), 3.0f, 0.03f);
	EXPECT_NEAR(Math::HalfToFloat(green), 0.5f, 0.01f);

	TextureAsset truncated;
	compressed(truncated, RHI::Format::BC7SRGB, TexelMeaning::SRGBColor, {1, 2, 3, 255});
	truncated.pixels.resize(8);
	EXPECT_FALSE(EditorPreviews::Produce({Guid{}, &truncated}, image));
}

TEST_F(PreviewRegistryTest, TextureProducerCarriesAssetsMagnificationFilter) {
	EditorPreviews::RegisterBuiltIns();

	TextureAsset linear;
	linear.size = {8, 8};
	linear.pixels.assign(8 * 8 * 4, u8{255});
	linear.settings.filter = RHI::FilterMode::Linear;
	AssetPreviewImage image;
	ASSERT_TRUE(EditorPreviews::Produce({Guid{}, &linear}, image));
	EXPECT_EQ(image.sampler, UI::SamplerKind::LinearClamp);

	TextureAsset nearest;
	nearest.size = {8, 8};
	nearest.pixels.assign(8 * 8 * 4, u8{255});
	nearest.settings.filter = RHI::FilterMode::Nearest;
	ASSERT_TRUE(EditorPreviews::Produce({Guid{}, &nearest}, image));
	EXPECT_EQ(image.sampler, UI::SamplerKind::NearestClamp);
	EXPECT_NE(image.sampler, UI::SamplerKind::NearestWrap);
}

TEST(AssetPreviewBadgeTest, EveryTypeGetsBadge) {
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("TextureAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetTexture.glyph);
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("PrefabAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetPrefab.glyph);
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("NoSuchAssetType")).icon.glyph,
			  EditorIcons::Glyph::AssetGeneric.glyph);

	EXPECT_TRUE(EditorPreviews::BadgeForFileName("notes.unknownextension").icon.glyph.empty());
}

TEST(AssetPreviewBadgeTest, ModuleBadgeSweptAlone) {
	EditorPreviews::AssetBadges::RegisterBuiltIns();
	int moduleA = 0;
	int moduleB = 0;

	EditorPreviews::AssetBadges::Register(StringHash("ModuleOwnAsset"), EditorIcons::Glyph::AssetMesh,
										  EditorTheme::ColorToken::AssetMesh, &moduleA);
	EditorPreviews::AssetBadges::Register(StringHash("TextureAsset"), EditorIcons::Glyph::AssetPrefab,
										  EditorTheme::ColorToken::AssetPrefab, &moduleA);
	EditorPreviews::AssetBadges::Register(StringHash("OtherModuleAsset"), EditorIcons::Glyph::AssetFont,
										  EditorTheme::ColorToken::AssetFont, &moduleB);

	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("ModuleOwnAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetMesh.glyph);
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("TextureAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetPrefab.glyph);

	EXPECT_EQ(EditorPreviews::AssetBadges::UnregisterOwnedBy(&moduleA), 2u);
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("ModuleOwnAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetGeneric.glyph);
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("TextureAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetTexture.glyph);
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("OtherModuleAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetFont.glyph);
	EXPECT_EQ(EditorPreviews::AssetBadges::UnregisterOwnedBy(&moduleB), 1u);

	EXPECT_EQ(EditorPreviews::AssetBadges::UnregisterOwnedBy(nullptr), 0u);
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("SceneAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetScene.glyph);
}

TEST(AssetPreviewBadgeTest, BadgeResolvesTintThroughActiveTheme) {
	EditorPreviews::AssetBadges::RegisterBuiltIns();
	const Color dark = EditorPreviews::BadgeForAssetType(StringHash("SceneAsset")).color;

	constexpr StringHash BadgeProbe("TestBadgeTint");
	EditorTheme::ThemeDesc desc = EditorTheme::MakeDefaultDesc();
	desc.Set(EditorTheme::ColorToken::AssetScene, "#101014");
	EditorTheme::Register(BadgeProbe, desc);
	ASSERT_TRUE(EditorTheme::SetActive(BadgeProbe));

	EXPECT_NE(EditorPreviews::BadgeForAssetType(StringHash("SceneAsset")).color, dark);

	ASSERT_TRUE(EditorTheme::SetActive(EditorTheme::DarkTheme));
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("SceneAsset")).color, dark);
}

TEST(AssetPreviewFramingTest, FitCameraRefusesUnframeable) {
	ViewDesc desc;
	ViewDesc untouched;

	EXPECT_FALSE(EditorPreviewOps::FitCameraToBounds(AABB::Empty(), 1.0f, 30.0f, 1.0f, AssetPreviewOrbit{}, desc));
	EXPECT_FALSE(EditorPreviewOps::FitCameraToBounds(AABB(Vec3{}, Vec3{}), 1.0f, 30.0f, 1.0f, AssetPreviewOrbit{}, desc));
	EXPECT_FALSE(EditorPreviewOps::FitCameraToBounds(AABB::FromCenterExtents({}, Vec3(0.5f, 0.5f, 0.5f)),
													 0.0f, 30.0f, 1.0f, AssetPreviewOrbit{}, desc));
	EXPECT_EQ(desc.cameraPosition, untouched.cameraPosition);
}

TEST(AssetPreviewFramingTest, FitCameraBacksOffForBigger) {
	ViewDesc small;
	ViewDesc large;
	AABB unit = AABB::FromCenterExtents({}, Vec3(0.5f, 0.5f, 0.5f));
	AABB tenfold = AABB::FromCenterExtents({}, Vec3(5.0f, 5.0f, 5.0f));
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.0f, 30.0f, 1.0f, AssetPreviewOrbit{}, small));
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(tenfold, 1.0f, 30.0f, 1.0f, AssetPreviewOrbit{}, large));

	EXPECT_LT(large.cameraPosition.z, small.cameraPosition.z);
	EXPECT_NEAR(large.cameraPosition.z / small.cameraPosition.z, 10.0f, 0.01f);
	EXPECT_LT(small.cameraPosition.z, 0.0f);
}

TEST(AssetPreviewFramingTest, FitCameraBacksOffForNarrowFov) {
	AABB unit = AABB::FromCenterExtents({}, Vec3(0.5f, 0.5f, 0.5f));
	ViewDesc square;
	ViewDesc wide;
	ViewDesc tall;
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.0f, 30.0f, 1.0f, AssetPreviewOrbit{}, square));
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 3.0f, 30.0f, 1.0f, AssetPreviewOrbit{}, wide));
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.0f / 3.0f, 30.0f, 1.0f, AssetPreviewOrbit{}, tall));

	EXPECT_NEAR(wide.cameraPosition.z, square.cameraPosition.z, 1e-4f);
	EXPECT_LT(tall.cameraPosition.z, square.cameraPosition.z);
}

TEST(AssetPreviewFramingTest, AMarginLeavesAirAroundTheSubject) {
	AABB unit = AABB::FromCenterExtents({}, Vec3(0.5f, 0.5f, 0.5f));
	ViewDesc tight;
	ViewDesc airy;
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.0f, 30.0f, 1.0f, AssetPreviewOrbit{}, tight));
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.0f, 30.0f, 1.2f, AssetPreviewOrbit{}, airy));
	EXPECT_NEAR(airy.cameraPosition.z / tight.cameraPosition.z, 1.2f, 1e-3f);

	ViewDesc clamped;
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.0f, 30.0f, 0.5f, AssetPreviewOrbit{}, clamped));
	EXPECT_NEAR(clamped.cameraPosition.z, tight.cameraPosition.z, 1e-4f);
}

namespace {

	class MaterialProducerTest : public ::testing::Test {
	protected:
		void SetUp() override {
			std::filesystem::path engineAssets = PreviewRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			EditorPreviews::Initialize();
			EditorPreviews::RegisterBuiltIns();
		}

		void TearDown() override {
			EditorPreviews::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
		}

		bool _mounted = false;
	};

	template <typename T>
	u32 CountRootsCarrying(Handle<Scene> scene) {
		u32 found = 0;
		for (u32 i = 0; i < scene->GetRootCount(); ++i) {
			Handle<SceneObject> root = scene->GetRoot(i);
			if (root.IsValid() && root->GetComponent<T>().IsValid()) {
				++found;
			}
		}
		return found;
	}

} // anonymous namespace

TEST_F(MaterialProducerTest, MaterialProducerNeedsStage) {
	MaterialAsset material;
	AssetPreviewRequest request;
	request.asset = &material;
	AssetPreviewImage image;
	EXPECT_FALSE(EditorPreviews::Produce(request, image));
	EXPECT_EQ(SceneSystem::GetLoadedScenes().size(), 0u);
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);
}

TEST_F(MaterialProducerTest, MaterialProducerBuildsLitSphere) {
	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Stage");
	ASSERT_TRUE(stage.IsValid());

	MaterialAsset material;
	AssetPreviewRequest request;
	request.asset = &material;
	request.stage = stage;
	AssetPreviewImage image;
	EXPECT_FALSE(EditorPreviews::Produce(request, image));
	EXPECT_EQ(image.size, (Vec2I{}));

	Handle<SceneObject> subject = stage->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());
	Handle<MeshRenderer> renderer = subject->GetComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	EXPECT_EQ(renderer->GetMaterial(0).Get(), &material);
	EXPECT_TRUE(renderer->GetMesh().IsValid());

	EXPECT_EQ(CountRootsCarrying<Light>(stage), 2u);

	EXPECT_EQ(SceneSystem::GetLoadedScenes().size(), 0u);
	SceneSystem::UnloadScene(stage);
	SceneSystem::EndFrame();
}

TEST_F(MaterialProducerTest, SkyMaterialPreviewsAsSky) {
	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Stage");
	ASSERT_TRUE(stage.IsValid());

	ShaderAsset shader;
	shader.integrator = ShaderTemplate::Sky;
	MaterialAsset material;
	material.shader = Handle<ShaderAsset>(&shader);
	AssetPreviewRequest request;
	request.asset = &material;
	request.stage = stage;
	AssetPreviewImage image;
	EXPECT_FALSE(EditorPreviews::Produce(request, image));

	EXPECT_FALSE(stage->FindObjectByName("Subject").IsValid());
	Handle<SceneObject> environmentObject = stage->FindObjectByName("Environment");
	ASSERT_TRUE(environmentObject.IsValid());
	Handle<Environment> environment = environmentObject->GetComponent<Environment>();
	ASSERT_TRUE(environment.IsValid());
	EXPECT_EQ(environment->GetSkyMode(), SkyMode::Material);
	EXPECT_EQ(environment->GetSkyMaterial().Get(), &material);
	EXPECT_EQ(CountRootsCarrying<Light>(stage), 2u);

	Handle<Scene> second = SceneSystem::CreateIsolatedScene("Second");
	MaterialAsset surface;
	request.asset = &surface;
	request.stage = second;
	EXPECT_FALSE(EditorPreviews::Produce(request, image));
	Handle<SceneObject> neutral = second->FindObjectByName("Environment");
	ASSERT_TRUE(neutral.IsValid());
	EXPECT_EQ(neutral->GetComponent<Environment>()->GetSkyMode(), SkyMode::None);

	SceneSystem::UnloadScene(stage);
	SceneSystem::UnloadScene(second);
	SceneSystem::EndFrame();
}

TEST_F(MaterialProducerTest, CachedAssetsReloadAfterDatabase) {
	auto produceBoth = [](const char* stageName) {
		Handle<Scene> stage = SceneSystem::CreateIsolatedScene(stageName);
		MaterialAsset material;
		AssetPreviewRequest request;
		request.asset = &material;
		request.stage = stage;
		AssetPreviewImage image;
		EditorPreviews::Produce(request, image);
		Asset* mesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
		AssetPreviewRequest meshRequest;
		meshRequest.asset = mesh;
		meshRequest.stage = SceneSystem::CreateIsolatedScene("MeshStage");
		EditorPreviews::Produce(meshRequest, image);
		AssetDatabase::UnloadAsset(mesh);
		return std::pair{stage, meshRequest.stage};
	};
	auto [firstSphere, firstMesh] = produceBoth("First");
	SceneSystem::UnloadScene(firstSphere);
	SceneSystem::UnloadScene(firstMesh);
	SceneSystem::EndFrame();

	SceneSystem::Terminate();
	AssetDatabase::Terminate();
	AssetDatabase::Initialize();
	SceneSystem::Initialize();

	auto [sphereStage, meshStage] = produceBoth("Second");
	Handle<SceneObject> sphere = sphereStage->FindObjectByName("Subject");
	Handle<SceneObject> mesh = meshStage->FindObjectByName("Subject");
	ASSERT_TRUE(sphere.IsValid() && mesh.IsValid());
	Handle<MeshRenderer> sphereRenderer = sphere->GetComponent<MeshRenderer>();
	Handle<MeshRenderer> meshRenderer = mesh->GetComponent<MeshRenderer>();
	ASSERT_TRUE(sphereRenderer.IsValid() && meshRenderer.IsValid());
	EXPECT_TRUE(sphereRenderer->GetMesh().IsValid());
	EXPECT_TRUE(meshRenderer->GetMaterial(0).IsValid());
	SceneSystem::UnloadScene(sphereStage);
	SceneSystem::UnloadScene(meshStage);
	SceneSystem::EndFrame();
}

namespace {

	class MaterialGoldenTest : public ::testing::Test {
	protected:
		bool _mounted = false;

		void SetUp() override {
			std::filesystem::path engineAssets = PreviewRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			EditorPreviews::Initialize();
			EditorPreviews::RegisterBuiltIns();
			EditorPreviewStore::Initialize();
		}

		void TearDown() override {
			EditorPreviewStore::Terminate();
			EditorPreviews::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
		}
	};

	struct PreviewTexel {
		u8 r = 0, g = 0, b = 0, a = 0;
	};

	PreviewTexel PreviewTexelAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return {pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
	}

} // anonymous namespace

TEST_F(MaterialGoldenTest, PreviewSphereWearsMaterialColor) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Asset* asset = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
	if (!asset) {
		GTEST_SKIP() << "engine://Materials/Default.mat unavailable (no dxcompiler.dll?)";
	}
	MaterialAsset* material = static_cast<MaterialAsset*>(asset);
	Color original = material->GetColor("baseColor");
	material->SetColor("baseColor", Color(0.0f, 1.0f, 0.0f, 1.0f));

	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("GoldenStage");
	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	AssetPreviewRequest request;
	request.asset = material;
	request.stage = stage;
	request.target = target;
	request.targetSize = RenderSystem::GetViewTargetSize(target);
	AssetPreviewImage image;
	ASSERT_TRUE(EditorPreviews::Produce(request, image));
	EXPECT_EQ(image.size, (Vec2I{128, 128}));

	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	EXPECT_GT(RenderSystem::GetStats().drawCalls, 0u);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	ASSERT_EQ(size, (Vec2I{128, 128}));

	PreviewTexel center = PreviewTexelAt(pixels, size, size.x / 2, size.y / 2);
	PreviewTexel corner = PreviewTexelAt(pixels, size, 1, 1);

	EXPECT_GT(center.g, 40u);
	EXPECT_GT(center.g, center.r * 2u) << static_cast<u32>(center.r) << " g=" << static_cast<u32>(center.g) << " b=" << static_cast<u32>(center.b);
	EXPECT_GT(center.g, center.b * 2u);

	EXPECT_LE(corner.g, corner.b);
	EXPECT_LT(corner.g, center.g);

	material->SetColor("baseColor", original);
	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(stage);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(asset);
}

namespace {

	class MeshAndPrefabProducerTest : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		Handle<Scene> _scene;
		bool _mountedEngine = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			_root = std::filesystem::temp_directory_path() /
					("shalltear_m5_" + std::to_string(counter.fetch_add(1)) +
					 "_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = PreviewRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			EditorPreviews::Initialize();
			EditorPreviews::RegisterBuiltIns();
			_scene = SceneSystem::CreateScene("Authoring");
		}

		void TearDown() override {
			EditorPreviews::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static void FillMesh(MeshAsset& mesh, std::initializer_list<u32> slots) {
			mesh.bounds = AABB::FromCenterExtents({}, Vec3(0.5f, 0.5f, 0.5f));
			for (u32 slot : slots) {
				SubMesh submesh;
				submesh.materialSlot = slot;
				mesh.submeshes.push_back(submesh);
			}
		}

		Guid SavePivotPrefab(const str& path, const Vec3& childOffset, MeshAsset* childMesh) {
			Handle<SceneObject> root = SceneSystem::CreateObject("Pivot", _scene);
			Handle<SceneObject> child = SceneSystem::CreateObject("Model", _scene, root);
			child->SetLocalPosition(childOffset);
			if (childMesh) {
				Handle<MeshRenderer> renderer = child->AddComponent<MeshRenderer>();
				renderer->SetMesh(Handle<MeshAsset>(childMesh));
			}
			Guid guid = SceneSystem::SavePrefab(root, Path(path));
			SceneSystem::Destroy(root);
			SceneSystem::EndFrame();
			return guid;
		}
	};

} // anonymous namespace

TEST_F(MeshAndPrefabProducerTest, ProducersNeedStage) {
	MeshAsset mesh;
	FillMesh(mesh, {0});
	AssetPreviewRequest meshRequest;
	meshRequest.asset = &mesh;
	AssetPreviewImage image;
	EXPECT_FALSE(EditorPreviews::Produce(meshRequest, image));

	Guid prefab = SavePivotPrefab("project://pivot.prefab", Vec3(0.0f, 0.0f, 0.0f), &mesh);
	ASSERT_TRUE(prefab.IsValid());
	AssetPreviewRequest prefabRequest;
	prefabRequest.guid = prefab;
	prefabRequest.asset = AssetDatabase::LoadAssetByGuid(prefab);
	ASSERT_NE(prefabRequest.asset, nullptr);
	EXPECT_FALSE(EditorPreviews::Produce(prefabRequest, image));
	AssetDatabase::UnloadAsset(prefabRequest.asset);

	EXPECT_EQ(SceneSystem::GetLoadedScenes().size(), 1u);
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);
}

TEST_F(MeshAndPrefabProducerTest, MeshProducerUsesSharedRig) {
	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Stage");
	ASSERT_TRUE(stage.IsValid());
	MeshAsset mesh;
	FillMesh(mesh, {0});

	AssetPreviewRequest request;
	request.asset = &mesh;
	request.stage = stage;
	AssetPreviewImage image;
	EXPECT_FALSE(EditorPreviews::Produce(request, image));

	Handle<SceneObject> subject = stage->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());
	Handle<MeshRenderer> renderer = subject->GetComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	EXPECT_EQ(renderer->GetMesh().Get(), &mesh);
	EXPECT_EQ(CountRootsCarrying<Light>(stage), 2u);
	EXPECT_EQ(SceneSystem::GetLoadedScenes().size(), 1u);

	SceneSystem::UnloadScene(stage);
	SceneSystem::EndFrame();
}

TEST_F(MeshAndPrefabProducerTest, MeshProducerBindsEverySlotSubmeshesReference) {
	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Stage");
	MeshAsset mesh;
	FillMesh(mesh, {0, 2});

	AssetPreviewRequest request;
	request.asset = &mesh;
	request.stage = stage;
	AssetPreviewImage image;
	EditorPreviews::Produce(request, image);

	Handle<MeshRenderer> renderer = stage->FindObjectByName("Subject")->GetComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	if (!renderer->GetMaterial(0).IsValid()) {
		GTEST_SKIP() << "engine://Materials/Default.mat unavailable (no dxcompiler.dll?)";
	}
	EXPECT_TRUE(renderer->GetMaterial(1).IsValid());
	EXPECT_TRUE(renderer->GetMaterial(2).IsValid());

	SceneSystem::UnloadScene(stage);
	SceneSystem::EndFrame();
}

TEST_F(MeshAndPrefabProducerTest, MeshReproduceStrandsNoReference) {
	MeshAsset mesh;
	FillMesh(mesh, {0});
	auto produceOnce = [&]() {
		Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Stage");
		AssetPreviewRequest request;
		request.asset = &mesh;
		request.stage = stage;
		AssetPreviewImage image;
		EditorPreviews::Produce(request, image);
		SceneSystem::UnloadScene(stage);
		SceneSystem::EndFrame();
	};

	produceOnce();
	Asset* defaultMaterial = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
	if (!defaultMaterial) {
		GTEST_SKIP() << "engine://Materials/Default.mat unavailable (no dxcompiler.dll?)";
	}
	i32 settled = defaultMaterial->GetRefCount();

	for (int i = 0; i < 10; ++i) {
		produceOnce();
	}
	EXPECT_EQ(defaultMaterial->GetRefCount(), settled);
	AssetDatabase::UnloadAsset(defaultMaterial);
}

TEST_F(MeshAndPrefabProducerTest, PrefabProducerComposesWholeGraphIntoStage) {
	MeshAsset mesh;
	FillMesh(mesh, {0});
	Guid prefab = SavePivotPrefab("project://pivot.prefab", Vec3(0.0f, 0.0f, 0.0f), &mesh);
	ASSERT_TRUE(prefab.IsValid());

	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Stage");
	AssetPreviewRequest request;
	request.guid = prefab;
	request.asset = AssetDatabase::LoadAssetByGuid(prefab);
	request.stage = stage;
	AssetPreviewImage image;
	EditorPreviews::Produce(request, image);

	Handle<SceneObject> instance = stage->FindObjectByName("Pivot");
	ASSERT_TRUE(instance.IsValid());
	EXPECT_TRUE(instance->FindChildByName("Model").IsValid());
	EXPECT_EQ(CountRootsCarrying<Light>(stage), 2u);
	EXPECT_EQ(SceneSystem::GetLoadedScenes().size(), 1u);
	EXPECT_FALSE(_scene->FindObjectByName("Pivot").IsValid());

	AssetDatabase::UnloadAsset(request.asset);
	SceneSystem::UnloadScene(stage);
	SceneSystem::EndFrame();
}

TEST_F(MeshAndPrefabProducerTest, GraphBoundsUnionDrawables) {
	MeshAsset mesh;
	FillMesh(mesh, {0});
	Guid prefab = SavePivotPrefab("project://offset.prefab", Vec3(10.0f, 0.0f, 0.0f), &mesh);
	ASSERT_TRUE(prefab.IsValid());

	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Stage");
	Handle<SceneObject> instance = SceneSystem::Instantiate(prefab, stage);
	ASSERT_TRUE(instance.IsValid());

	AABB graph = EditorViewportOps::GetGraphWorldBounds(instance);
	ASSERT_TRUE(graph.IsValid());
	EXPECT_NEAR(graph.Center().x, 10.0f, 0.01f);
	EXPECT_NEAR(EditorViewportOps::GetObjectWorldBounds(instance).Center().x, 0.0f, 0.01f);

	SceneSystem::UnloadScene(stage);
	SceneSystem::EndFrame();
}

TEST_F(MeshAndPrefabProducerTest, PrefabWithNothingDrawableIsStillComposed) {
	Guid prefab = SavePivotPrefab("project://empty.prefab", Vec3(0.0f, 0.0f, 0.0f), nullptr);
	ASSERT_TRUE(prefab.IsValid());

	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Stage");
	AssetPreviewRequest request;
	request.guid = prefab;
	request.asset = AssetDatabase::LoadAssetByGuid(prefab);
	request.stage = stage;
	AssetPreviewImage image;
	EditorPreviews::Produce(request, image);
	EXPECT_TRUE(stage->FindObjectByName("Pivot").IsValid());

	AssetDatabase::UnloadAsset(request.asset);
	SceneSystem::UnloadScene(stage);
	SceneSystem::EndFrame();
}

namespace {

	class M5GoldenTest : public ::testing::Test {
	protected:
		bool _mounted = false;
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			_root = std::filesystem::temp_directory_path() /
					("shalltear_m5g_" + std::to_string(counter.fetch_add(1)) +
					 "_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = PreviewRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			EditorPreviews::Initialize();
			EditorPreviews::RegisterBuiltIns();
		}

		void TearDown() override {
			EditorPreviews::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool RenderOnce(const AssetPreviewRequest& request, Vector<u8>& pixels, Vec2I& size) {
			AssetPreviewImage image;
			if (!EditorPreviews::Produce(request, image)) {
				return false;
			}
			SceneSystem::EndFrame();
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTarget(request.target, pixels, size);
		}
	};

} // anonymous namespace

TEST_F(M5GoldenTest, MeshPreviewShowsMeshFramedAgainstClear) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Asset* asset = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
	if (!asset) {
		GTEST_SKIP() << "shipped cube primitive didn't load (no dxcompiler.dll?)";
	}

	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("GoldenStage");
	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	AssetPreviewRequest request;
	request.asset = asset;
	request.stage = stage;
	request.target = target;
	request.targetSize = RenderSystem::GetViewTargetSize(target);
	AssetPreviewImage image;
	ASSERT_TRUE(EditorPreviews::Produce(request, image));

	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	EXPECT_GT(RenderSystem::GetStats().drawCalls, 0u);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	PreviewTexel center = PreviewTexelAt(pixels, size, size.x / 2, size.y / 2);
	PreviewTexel corner = PreviewTexelAt(pixels, size, 1, 1);

	EXPECT_GT(center.r, corner.r + 30u);
	EXPECT_GT(center.g, corner.g + 30u);
	EXPECT_LT(corner.r, 100u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(stage);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(M5GoldenTest, PrefabPreviewShowsModelHangingOffPivot) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Asset* cube = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
	Asset* materialAsset = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
	if (!cube || !materialAsset) {
		GTEST_SKIP();
	}

	Handle<Scene> authoring = SceneSystem::CreateScene("Authoring");
	Handle<SceneObject> pivot = SceneSystem::CreateObject("Pivot", authoring);
	Handle<SceneObject> model = SceneSystem::CreateObject("Model", authoring, pivot);
	model->SetLocalPosition(Vec3(6.0f, 0.0f, 0.0f));
	Handle<MeshRenderer> renderer = model->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cube)));
	renderer->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(materialAsset)));
	Guid prefab = SceneSystem::SavePrefab(pivot, Path("project://golden.prefab"));
	ASSERT_TRUE(prefab.IsValid());
	SceneSystem::Destroy(pivot);
	SceneSystem::EndFrame();

	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("GoldenStage");
	ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
	AssetPreviewRequest request;
	request.guid = prefab;
	request.asset = AssetDatabase::LoadAssetByGuid(prefab);
	request.stage = stage;
	request.target = target;
	request.targetSize = RenderSystem::GetViewTargetSize(target);
	ASSERT_NE(request.asset, nullptr);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderOnce(request, pixels, size));

	PreviewTexel center = PreviewTexelAt(pixels, size, size.x / 2, size.y / 2);
	PreviewTexel corner = PreviewTexelAt(pixels, size, 1, 1);
	EXPECT_GT(center.r, corner.r + 30u);
	EXPECT_LT(corner.r, 100u);

	AssetDatabase::UnloadAsset(request.asset);
	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(stage);
	SceneSystem::UnloadScene(authoring);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(cube);
}

TEST_F(M5GoldenTest, UndrawablePrefabDeclines) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Asset* cube = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
	if (!cube) {
		GTEST_SKIP() << "shipped cube primitive didn't load (no dxcompiler.dll?)";
	}
	Handle<Scene> authoring = SceneSystem::CreateScene("Authoring");

	auto savePrefab = [&](const char* path, bool drawable) {
		Handle<SceneObject> root = SceneSystem::CreateObject("Root", authoring);
		Handle<SceneObject> child = SceneSystem::CreateObject("Child", authoring, root);
		if (drawable) {
			child->AddComponent<MeshRenderer>()->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cube)));
		}
		Guid guid = SceneSystem::SavePrefab(root, Path(path));
		SceneSystem::Destroy(root);
		SceneSystem::EndFrame();
		return guid;
	};
	Guid drawable = savePrefab("project://drawable.prefab", true);
	Guid hollow = savePrefab("project://hollow.prefab", false);
	ASSERT_TRUE(drawable.IsValid() && hollow.IsValid());

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	auto produce = [&](const Guid& guid) {
		Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Stage");
		AssetPreviewRequest request;
		request.guid = guid;
		request.asset = AssetDatabase::LoadAssetByGuid(guid);
		request.stage = stage;
		request.target = target;
		request.targetSize = RenderSystem::GetViewTargetSize(target);
		AssetPreviewImage image;
		bool produced = EditorPreviews::Produce(request, image);
		AssetDatabase::UnloadAsset(request.asset);
		SceneSystem::UnloadScene(stage);
		SceneSystem::EndFrame();
		return produced;
	};

	EXPECT_TRUE(produce(drawable));
	EXPECT_FALSE(produce(hollow));

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(authoring);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(cube);
}

TEST(AssetPreviewLayeringTest, CorePublicHeadersNeverLearnWordPreview) {
	std::filesystem::path publicDir = PreviewRepoDir("Source/Core/Public");
	ASSERT_FALSE(publicDir.empty()) << std::filesystem::current_path().string();

	const std::regex word(R"([Pp][Rr][Ee][Vv][Ii][Ee][Ww])");
	u32 scanned = 0;
	std::error_code ec;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(publicDir, ec)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".h") {
			continue;
		}
		++scanned;
		std::ifstream stream(entry.path());
		str line;
		u32 lineNumber = 0;
		while (std::getline(stream, line)) {
			++lineNumber;
			str code = line.substr(0, line.find("//"));
			if (std::regex_search(code, word)) {
				ADD_FAILURE() << entry.path().filename().string() << ":" << lineNumber << " editor concept in Core public surface\n " << line;
			}
		}
	}
	EXPECT_GT(scanned, 20u);

	str probe = "	void SetPreviewScene(Handle<Scene> scene);";
	EXPECT_TRUE(std::regex_search(probe, word));
	str allowed = "	// the builder's preview needs a second Context";
	str allowedCode = allowed.substr(0, allowed.find("//"));
	EXPECT_FALSE(std::regex_search(allowedCode, word));
}

TEST(AssetPreviewLayeringTest, ProducerNamedOnlyWhereDefined) {
	std::filesystem::path registrationSite = PreviewRepoDir("Source/Editor/Private/TexturePreview.cpp");
	ASSERT_FALSE(registrationSite.empty()) << std::filesystem::current_path().string();
	std::filesystem::path editorDir = registrationSite.parent_path().parent_path();

	const std::set<str> allowed = {"MaterialPreview.cpp", "MeshPreview.cpp", "PrefabPreview.cpp",
								   "TexturePreview.cpp"};

	const std::regex producer(R"(Produce[A-Za-z0-9_]*Preview)");
	u32 scanned = 0;
	u32 namedInRegistrationSite = 0;
	std::error_code ec;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(editorDir, ec)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		str extension = entry.path().extension().string();
		if (extension != ".cpp" && extension != ".h") {
			continue;
		}
		++scanned;
		str filename = entry.path().filename().string();
		bool isAllowed = allowed.count(filename) > 0;

		std::ifstream stream(entry.path());
		str line;
		u32 lineNumber = 0;
		while (std::getline(stream, line)) {
			++lineNumber;
			str code = line.substr(0, line.find("//"));
			if (!std::regex_search(code, producer)) {
				continue;
			}
			if (filename == "TexturePreview.cpp") {
				++namedInRegistrationSite;
			}
			if (!isAllowed) {
				ADD_FAILURE();
			}
		}
	}
	EXPECT_GT(scanned, 40u) << editorDir.string();

	EXPECT_EQ(namedInRegistrationSite, 6u);

	str probe = "	ProduceMeshPreview(request, image);";
	EXPECT_TRUE(std::regex_search(probe, producer));
	str allowedComment = "	// the mesh producer, ProduceMeshPreview, lives next door";
	str allowedCode = allowedComment.substr(0, allowedComment.find("//"));
	EXPECT_FALSE(std::regex_search(allowedCode, producer));
}

namespace {

	class PreviewPaneTest : public ::testing::Test {
	protected:
		std::filesystem::path _projectRoot;
		Handle<Scene> _scene;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_projectRoot = std::filesystem::temp_directory_path() /
						   ("shalltear_preview_" + std::to_string(stamp) + "_" +
							std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_projectRoot / "Assets");
			FileSystem::Mount("project", Path(_projectRoot.generic_string()));
			OnBeforeDatabase();
			AssetDatabase::Initialize();
			OnDatabaseReady();

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();
			EditorPreviewStore::Initialize();
			_scene = SceneSystem::CreateScene("PreviewFixture");
		}

		void TearDown() override {
			PanelManager::Terminate();
			EditorPreviewStore::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			OnDatabaseGone();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_projectRoot, ec);
		}

		virtual void OnBeforeDatabase() {}
		virtual void OnDatabaseReady() {}
		virtual void OnDatabaseGone() {}

		Guid StageChecker() {
			std::filesystem::path source = PreviewRepoDir("Source/Tests/Fixtures/Graphics/checker.png");
			if (source.empty()) {
				return {};
			}
			std::error_code ec;
			std::filesystem::copy_file(source, _projectRoot / "Assets" / "checker.png",
									   std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				return {};
			}
			AssetDatabase::ScanProjectDirectory();
			return AssetDatabase::LookupGuid(Path("project://Assets/checker.png"));
		}

		void StageCheckerAsNearest(Guid& out) {
			out = StageChecker();
			if (!out.IsValid()) {
				return;
			}
			Asset* first = AssetDatabase::LoadAssetByGuid(out);
			ASSERT_NE(first, nullptr);
			AssetDatabase::UnloadAsset(first);
			Path metaPath = FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://Assets/checker.png")));
			str metaText = FileSystem::ReadTextFile(metaPath, MemTag::Asset);
			constexpr std::string_view DefaultFilter = "\"filter\": \"linear\"";
			size_t at = metaText.find(DefaultFilter);
			ASSERT_NE(at, str::npos) << metaText;
			metaText.replace(at, DefaultFilter.size(), "\"filter\": \"nearest\"");
			ASSERT_TRUE(FileSystem::WriteTextFile(metaPath, metaText));
			AssetDatabase::CollectGarbage();
		}

		Guid StageMaterial() {
			std::filesystem::path source = PreviewRepoDir("EngineAssets/Materials/Default.mat");
			if (source.empty()) {
				return {};
			}
			std::error_code ec;
			std::filesystem::copy_file(source, _projectRoot / "Assets" / "Preview.mat",
									   std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				return {};
			}
			AssetDatabase::ScanProjectDirectory();
			return AssetDatabase::LookupGuid(Path("project://Assets/Preview.mat"));
		}

		Guid StageText() {
			FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/notes.txt")), "hello");
			AssetDatabase::ScanProjectDirectory();
			return AssetDatabase::LookupGuid(Path("project://Assets/notes.txt"));
		}

		Handle<EditorPanel> OpenInspector() {
			return PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")));
		}

		void SettleStore(UI::Context& context, int frames = 4) {
			for (int i = 0; i < frames; ++i) {
				EditorPreviewStore::Drain();
				PanelManager::TickAll(1.0f / 60.0f);
				context.Update();
			}
		}

		static bool AnyTextContains(UI::Context& context, UI::ElementHandle handle, std::string_view needle) {
			if (context.GetTextString(handle).find(needle) != str::npos) {
				return true;
			}
			const UI::Element* element = context.GetElement(handle);
			if (!element) {
				return false;
			}
			for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle next = context.MakeHandle(child);
				if (AnyTextContains(context, next, needle)) {
					return true;
				}
				child = context.GetElement(next)->nextSibling;
			}
			return false;
		}

		struct PaintedPicture {
			UI::Rect rect{};
			RHI::TextureHandle texture;
			UI::SamplerKind sampler = UI::SamplerKind::LinearClamp;
		};
		static Vector<PaintedPicture> PicturesIn(UI::Context& context, UI::ElementHandle well) {
			Vector<PaintedPicture> pictures{GetMemoryResource(MemTag::Editor)};
			if (!context.IsAlive(well)) {
				return pictures;
			}
			UI::RenderList list;
			context.BuildRenderList(list);
			const UI::Rect area = context.GetLayout(well).borderBox;
			for (const UI::UICommand& command : list.commands) {
				if (command.kind != UI::UICommandKind::Quads) {
					continue;
				}
				for (u32 i = command.firstQuad; i < command.firstQuad + command.quadCount; ++i) {
					const UI::UIQuad& quad = list.quads[i];
					const bool whole = quad.uv.x == 0.0f && quad.uv.y == 0.0f && quad.uv.w == 1.0f && quad.uv.h == 1.0f;
					const bool over = quad.rect.x < area.x + area.w && quad.rect.x + quad.rect.w > area.x &&
									  quad.rect.y < area.y + area.h && quad.rect.y + quad.rect.h > area.y;
					if (whole && over) {
						pictures.push_back({quad.rect, command.texture, command.sampler});
					}
				}
			}
			return pictures;
		}

		static void ClickPaneHeader(UI::Context& context, UI::ElementHandle panelRoot) {
			const UI::Element* rootElement = context.GetElement(panelRoot);
			ASSERT_NE(rootElement, nullptr);
			UI::ElementHandle pane = context.MakeHandle(rootElement->lastChild);
			const UI::Element* paneElement = context.GetElement(pane);
			ASSERT_NE(paneElement, nullptr);
			UI::ElementHandle header = context.MakeHandle(paneElement->firstChild);
			const UI::Rect& box = context.GetLayout(header).borderBox;
			ASSERT_GT(box.w, 0.0f);
			Vec2 point{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
			for (InputEventType type : {InputEventType::MouseMove, InputEventType::MouseDown, InputEventType::MouseUp}) {
				InputEvent event{};
				event.type = type;
				event.button = MouseButton::Left;
				event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
				context.InjectInput(event);
			}
			context.Update();
		}

		static UI::ElementHandle FindWell(UI::Context& context, UI::ElementHandle from) {
			if (context.HasClass(from, EditorTheme::Class::ViewportPlaceholder)) {
				return from;
			}
			const UI::Element* element = context.GetElement(from);
			if (!element) {
				return {};
			}
			for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle next = context.MakeHandle(child);
				UI::ElementHandle found = FindWell(context, next);
				if (found.IsValid()) {
					return found;
				}
				child = context.GetElement(next)->nextSibling;
			}
			return {};
		}
	};

} // anonymous namespace

TEST_F(PreviewPaneTest, SelectingTextureDrawsImageAndDimensions) {
	Guid guid = StageChecker();
	if (!guid.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	ASSERT_TRUE(panel.IsValid());
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	EXPECT_TRUE(AnyTextContains(context, root, "checker.png"));
	SettleStore(context);
	EXPECT_TRUE(AnyTextContains(context, root, "8 x 8  BC7SRGB"));

	UI::ElementHandle well = FindWell(context, root);
	ASSERT_TRUE(well.IsValid());
	const Vector<PaintedPicture> pictures = PicturesIn(context, well);
	ASSERT_EQ(pictures.size(), 1u);

	const UI::Rect& drawn = pictures[0].rect;
	const UI::Rect& box = context.GetLayout(well).content;
	EXPECT_GT(drawn.w, 0.0f);
	EXPECT_FLOAT_EQ(drawn.w, drawn.h);
	EXPECT_LE(drawn.w, box.w + 0.001f);
	EXPECT_LE(drawn.h, box.h + 0.001f);
}

TEST_F(PreviewPaneTest, CachedPicturesPaintFirstFrame) {
	Guid first = StageChecker();
	if (!first.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	std::error_code ec;
	std::filesystem::copy_file(_projectRoot / "Assets" / "checker.png", _projectRoot / "Assets" / "second.png",
							   std::filesystem::copy_options::overwrite_existing, ec);
	ASSERT_FALSE(ec) << ec.message();
	AssetDatabase::ScanProjectDirectory();
	Guid second = AssetDatabase::LookupGuid(Path("project://Assets/second.png"));
	ASSERT_TRUE(second.IsValid());

	for (const f32 scale : {1.0f, 1.25f}) {
		SCOPED_TRACE(std::format("content scale {}", scale));
		Handle<EditorPanel> panel = OpenInspector();
		MockFontSystem fonts;
		UI::ContextDesc desc;
		desc.sizePx = {400, 700};
		desc.contentScale = scale;
		UI::Context context(desc, &fonts);
		UI::ElementHandle root = context.CreateElement();
		context.AppendChild(context.GetRoot(), root);
		panel.Get()->BuildContent(context, root);
		context.Update();
		EditorSelection::SelectAsset(first);
		SettleStore(context);
		EditorSelection::SelectAsset(second);
		SettleStore(context);
		ASSERT_EQ(EditorPreviewStore::GetCachedCount(), 2u);

		auto expectPainted = [&](const char* name, const char* when) {
			UI::ElementHandle well = FindWell(context, root);
			const Vector<PaintedPicture> pictures = PicturesIn(context, well);
			ASSERT_EQ(pictures.size(), 1u) << when;
			EXPECT_GT(pictures[0].rect.w, 0.0f) << when;
			EXPECT_NEAR(pictures[0].rect.h, context.GetLayout(well).content.h, 1.0f) << when;
			EXPECT_TRUE(AnyTextContains(context, root, name)) << when;
		};
		for (int round = 0; round < 4; ++round) {
			const bool firstOne = round % 2 == 0;
			EditorSelection::SelectAsset(firstOne ? first : second);
			EditorPreviewStore::Drain();
			PanelManager::TickAll(1.0f / 60.0f);
			context.Update();
			expectPainted(firstOne ? "checker.png" : "second.png", "the first frame of a switch");
		}

		context.ClearChildren(root);
		panel.Get()->BuildContent(context, root);
		context.Update();
		expectPainted("second.png", "the first frame of a rebuilt Inspector");

		PanelManager::ClosePanel(panel);
		PanelManager::TickAll(0.0f);
	}
}

TEST_F(PreviewPaneTest, ImageFitsAtFractionalScale) {
	Guid guid = StageChecker();
	if (!guid.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	ASSERT_TRUE(panel.IsValid());
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	desc.contentScale = 1.25f;
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	SettleStore(context);
	UI::ElementHandle well = FindWell(context, root);
	ASSERT_TRUE(well.IsValid());
	const Vector<PaintedPicture> pictures = PicturesIn(context, well);
	ASSERT_EQ(pictures.size(), 1u);

	const UI::Rect& drawn = pictures[0].rect;
	const UI::Rect& box = context.GetLayout(well).content;

	EXPECT_GT(drawn.w, 0.0f);
	EXPECT_FLOAT_EQ(drawn.w, drawn.h);
	EXPECT_LE(drawn.w, box.w + 0.001f);
	EXPECT_LE(drawn.h, box.h + 0.001f);
	EXPECT_NEAR(drawn.h, box.h, 1.0f);
}

TEST_F(PreviewPaneTest, PaneMagnifiesWithImportFilter) {
	Guid guid;
	ASSERT_NO_FATAL_FAILURE(StageCheckerAsNearest(guid));
	if (!guid.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	ASSERT_TRUE(panel.IsValid());
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	SettleStore(context);
	const Vector<PaintedPicture> pictures = PicturesIn(context, FindWell(context, root));
	ASSERT_EQ(pictures.size(), 1u);
	EXPECT_EQ(pictures[0].sampler, UI::SamplerKind::NearestClamp);
}

TEST_F(PreviewPaneTest, NoProducerShowsBadge) {
	Guid guid = StageText();
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	ASSERT_TRUE(panel.IsValid());
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	EXPECT_TRUE(AnyTextContains(context, root, "notes.txt"));
	EXPECT_TRUE(FindWell(context, root).IsValid());

	SettleStore(context);
	EXPECT_TRUE(PicturesIn(context, FindWell(context, root)).empty());
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);
}

TEST_F(PreviewPaneTest, NoAssetSelectionMeansNoPane) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Subject", _scene);
	EditorSelection::Select(Handle<Object>(object.Get()));

	Handle<EditorPanel> panel = OpenInspector();
	ASSERT_TRUE(panel.IsValid());
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	EXPECT_FALSE(FindWell(context, root).IsValid());
}

TEST_F(PreviewPaneTest, HeaderClickTogglesPane) {
	Guid guid = StageChecker();
	if (!guid.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	ASSERT_TRUE(panel.IsValid());
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	ASSERT_TRUE(FindWell(context, root).IsValid());

	ASSERT_NO_FATAL_FAILURE(ClickPaneHeader(context, root));
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	EXPECT_FALSE(FindWell(context, root).IsValid());
	EXPECT_TRUE(AnyTextContains(context, root, "checker.png"));

	ASSERT_NO_FATAL_FAILURE(ClickPaneHeader(context, root));
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	EXPECT_TRUE(FindWell(context, root).IsValid());
}

TEST_F(PreviewPaneTest, TwoInspectorsKeepSeparatePreviewState) {
	Guid guid = StageChecker();
	if (!guid.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> first = OpenInspector();
	Handle<EditorPanel> second = OpenInspector();
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	ASSERT_NE(first.GetInstanceId(), second.GetInstanceId());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	UI::ElementHandle firstRoot = context.CreateElement();
	UI::ElementHandle secondRoot = context.CreateElement();
	context.AppendChild(context.GetRoot(), firstRoot);
	context.AppendChild(context.GetRoot(), secondRoot);
	first.Get()->BuildContent(context, firstRoot);
	second.Get()->BuildContent(context, secondRoot);
	context.Update();

	ASSERT_TRUE(FindWell(context, firstRoot).IsValid());
	ASSERT_TRUE(FindWell(context, secondRoot).IsValid());

	ASSERT_NO_FATAL_FAILURE(ClickPaneHeader(context, firstRoot));
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();

	EXPECT_FALSE(FindWell(context, firstRoot).IsValid());
	EXPECT_TRUE(FindWell(context, secondRoot).IsValid());
}

TEST_F(PreviewPaneTest, CachedPreviewHoldsNoReference) {
	Guid guid = StageChecker();
	if (!guid.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	ASSERT_TRUE(panel.IsValid());
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();
	SettleStore(context);
	ASSERT_EQ(EditorPreviewStore::GetCachedCount(), 1u);

	Asset* asset = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(asset, nullptr);
	EXPECT_EQ(asset->GetRefCount(), 1);

	PanelManager::ClosePanel(panel);
	EXPECT_EQ(asset->GetRefCount(), 1);
	AssetDatabase::UnloadAsset(asset);
}

namespace {

	void BuildInspectorInto(Handle<EditorPanel> panel, UI::Context& context) {
		UI::ElementHandle root = context.CreateElement();
		context.AppendChild(context.GetRoot(), root);
		panel.Get()->BuildContent(context, root);
		context.Update();
	}

} // anonymous namespace

TEST_F(PreviewPaneTest, ProduceAlwaysReturnsStage) {
	Guid material = StageMaterial();
	Guid texture = StageChecker();
	if (!material.IsValid() || !texture.IsValid()) {
		GTEST_SKIP() << "engine or graphics fixtures not found";
	}
	ASSERT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);

	Handle<EditorPanel> panel = OpenInspector();
	ASSERT_TRUE(panel.IsValid());
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};

	for (int i = 0; i < 10; ++i) {
		EditorSelection::SelectAsset(i % 2 == 0 ? texture : material);
		UI::Context context(desc, &fonts);
		BuildInspectorInto(panel, context);
		SettleStore(context);
		SceneSystem::EndFrame();
		EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);
	}

	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);
}

TEST_F(PreviewPaneTest, CollapsedPaneAsksNothing) {
	Guid texture = StageChecker();
	if (!texture.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	Handle<EditorPanel> panel = OpenInspector();
	ASSERT_TRUE(panel.IsValid());
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};

	EditorSelection::SelectAsset(texture);
	{
		UI::Context context(desc, &fonts);
		BuildInspectorInto(panel, context);
		SettleStore(context);
		ASSERT_EQ(EditorPreviewStore::GetCachedCount(), 1u);

		UI::ElementHandle panelRoot = context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
		ClickPaneHeader(context, panelRoot);
	}
	{
		UI::Context context(desc, &fonts);
		BuildInspectorInto(panel, context);
		EditorPreviewStore::InvalidateAll();
		for (int i = 0; i < 4; ++i) {
			PanelManager::TickAll(1.0f / 60.0f);
			context.Update();
		}
		EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);
	}

	EditorSelection::Clear();
	{
		UI::Context context(desc, &fonts);
		BuildInspectorInto(panel, context);
		for (int i = 0; i < 4; ++i) {
			PanelManager::TickAll(1.0f / 60.0f);
			context.Update();
		}
		EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);
	}

	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
}

TEST_F(PreviewPaneTest, TwoInspectorsShareOneProduce) {
	Guid texture = StageChecker();
	if (!texture.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorSelection::SelectAsset(texture);

	Handle<EditorPanel> first = OpenInspector();
	Handle<EditorPanel> second = OpenInspector();
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	ASSERT_NE(first.Get(), second.Get());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	UI::ElementHandle firstRoot = context.CreateElement();
	UI::ElementHandle secondRoot = context.CreateElement();
	context.AppendChild(context.GetRoot(), firstRoot);
	context.AppendChild(context.GetRoot(), secondRoot);
	first.Get()->BuildContent(context, firstRoot);
	second.Get()->BuildContent(context, secondRoot);
	context.Update();
	SettleStore(context);

	EXPECT_EQ(EditorPreviewStore::GetCachedCount(), 1u);

	const Vector<PaintedPicture> firstPictures = PicturesIn(context, FindWell(context, firstRoot));
	const Vector<PaintedPicture> secondPictures = PicturesIn(context, FindWell(context, secondRoot));
	ASSERT_EQ(firstPictures.size(), 1u);
	ASSERT_EQ(secondPictures.size(), 1u);
	EXPECT_EQ(firstPictures[0].texture, secondPictures[0].texture);

	PanelManager::ClosePanel(second);
	PanelManager::ClosePanel(first);
	SceneSystem::EndFrame();
}

namespace {

	class PreviewStoreTest : public ::testing::Test {
	protected:
		std::filesystem::path _projectRoot;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_projectRoot = std::filesystem::temp_directory_path() /
						   ("shalltear_store_" + std::to_string(stamp) + "_" +
							std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_projectRoot / "Assets");
			FileSystem::Mount("project", Path(_projectRoot.generic_string()));
			AssetDatabase::Initialize();
			TypeRegistry::InitializePrimitives();
			SceneSystem::Initialize();
			EditorPreviews::Initialize();
			EditorPreviews::RegisterBuiltIns();
			EditorPreviewStore::Initialize();
		}

		void TearDown() override {
			EditorPreviewStore::Terminate();
			EditorPreviews::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_projectRoot, ec);
		}

		bool StageTextures(int count, Vector<Guid>& out) {
			std::filesystem::path source = PreviewRepoDir("Source/Tests/Fixtures/Graphics/checker.png");
			if (source.empty()) {
				return false;
			}
			std::error_code ec;
			for (int i = 0; i < count; ++i) {
				std::filesystem::copy_file(source, _projectRoot / "Assets" / ("checker" + std::to_string(i) + ".png"),
										   std::filesystem::copy_options::overwrite_existing, ec);
				if (ec) {
					return false;
				}
			}
			AssetDatabase::ScanProjectDirectory();
			out.clear();
			for (int i = 0; i < count; ++i) {
				Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/checker" + std::to_string(i) + ".png"));
				if (!guid.IsValid()) {
					return false;
				}
				out.push_back(guid);
			}
			return true;
		}

		static bool Settle(const Guid& guid, int frames = 4) {
			AssetPreviewEntry entry;
			for (int i = 0; i < frames; ++i) {
				if (EditorPreviewStore::Request(guid, entry)) {
					return true;
				}
				EditorPreviewStore::Drain();
			}
			return EditorPreviewStore::Request(guid, entry);
		}
	};

} // anonymous namespace

TEST_F(PreviewStoreTest, FirstAskIsAlwaysMissAndEnqueue) {
	Vector<Guid> guids{GetMemoryResource(MemTag::Editor)};
	if (!StageTextures(1, guids)) {
		GTEST_SKIP() << "checker.png fixture not found";
	}

	AssetPreviewEntry entry;
	EXPECT_FALSE(EditorPreviewStore::Request(guids[0], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EXPECT_FALSE(EditorPreviewStore::Request(guids[0], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);

	EditorPreviewStore::Drain();
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);
	EXPECT_TRUE(EditorPreviewStore::Request(guids[0], entry));
	EXPECT_GT(entry.size.x, 0);
}

TEST_F(PreviewStoreTest, ByteTotalIsCountTimesOneEntry) {
	Vector<Guid> guids{GetMemoryResource(MemTag::Editor)};
	if (!StageTextures(4, guids)) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EXPECT_EQ(EditorPreviewStore::GetCachedBytes(), 0u);

	ASSERT_TRUE(Settle(guids[0]));
	ASSERT_EQ(EditorPreviewStore::GetCachedCount(), 1u);
	u64 perEntry = EditorPreviewStore::GetCachedBytes();
	EXPECT_GT(perEntry, 0u);

	for (size_t i = 1; i < guids.size(); ++i) {
		ASSERT_TRUE(Settle(guids[i])) << "entry " << i;
		EXPECT_EQ(EditorPreviewStore::GetCachedCount(), static_cast<u32>(i + 1));
		EXPECT_EQ(EditorPreviewStore::GetCachedBytes(), perEntry * (i + 1));
	}

	EditorPreviewStore::InvalidateAll();
	EXPECT_EQ(EditorPreviewStore::GetCachedCount(), static_cast<u32>(guids.size()));
	EXPECT_EQ(EditorPreviewStore::GetCachedBytes(), perEntry * guids.size());
}

TEST_F(PreviewStoreTest, CapEvictsLeastRecent) {
	Vector<Guid> guids{GetMemoryResource(MemTag::Editor)};
	if (!StageTextures(4, guids)) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorPreviewStore::SetCapacity(2);

	ASSERT_TRUE(Settle(guids[0]));
	ASSERT_TRUE(Settle(guids[1]));
	ASSERT_EQ(EditorPreviewStore::GetCachedCount(), 2u);

	AssetPreviewEntry entry;
	ASSERT_TRUE(EditorPreviewStore::Request(guids[0], entry));

	u64 twoEntries = EditorPreviewStore::GetCachedBytes();
	ASSERT_TRUE(Settle(guids[2]));
	EXPECT_EQ(EditorPreviewStore::GetCachedCount(), 2u);
	EXPECT_TRUE(EditorPreviewStore::Request(guids[0], entry));
	EXPECT_TRUE(EditorPreviewStore::Request(guids[2], entry));
	EXPECT_EQ(EditorPreviewStore::GetCachedBytes(), twoEntries);

	EditorPreviewStore::SetCapacity(1);
	EXPECT_EQ(EditorPreviewStore::GetCachedCount(), 1u);
	EXPECT_EQ(EditorPreviewStore::GetCachedBytes(), twoEntries / 2);
}

TEST_F(PreviewStoreTest, BudgetBetweenOneAndQueue) {
	Vector<Guid> guids{GetMemoryResource(MemTag::Editor)};
	if (!StageTextures(4, guids)) {
		GTEST_SKIP() << "checker.png fixture not found";
	}

	AssetPreviewEntry entry;
	EditorPreviewStore::SetFrameBudgetMs(0.0f);
	for (const Guid& guid : guids) {
		EditorPreviewStore::Request(guid, entry);
	}
	ASSERT_EQ(EditorPreviewStore::GetPendingCount(), 4u);

	EditorPreviewStore::Drain();
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 3u);
	EditorPreviewStore::Drain();
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 2u);

	EditorPreviewStore::SetFrameBudgetMs(60000.0f);
	EditorPreviewStore::Drain();
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);
	EXPECT_EQ(EditorPreviewStore::GetCachedCount(), 4u);
}

TEST_F(PreviewStoreTest, EvictingPaintedTextureIsSafe) {
	Vector<Guid> guids{GetMemoryResource(MemTag::Editor)};
	if (!StageTextures(2, guids)) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorPreviewStore::SetCapacity(1);
	ASSERT_TRUE(Settle(guids[0]));

	AssetPreviewEntry painted;
	ASSERT_TRUE(EditorPreviewStore::Request(guids[0], painted));

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle element = context.CreateElement();
	context.AppendChild(context.GetRoot(), element);
	UI::Style style;
	style.width = UI::Length::Pixel(64.0f);
	style.height = UI::Length::Pixel(64.0f);
	style.background = UI::Brush::ImageBrush(painted.texture, painted.sampler);
	context.SetStyle(element, style);
	context.Update();

	ASSERT_TRUE(Settle(guids[1]));
	ASSERT_EQ(EditorPreviewStore::GetCachedCount(), 1u);

	EXPECT_TRUE(context.IsAlive(element));
	EXPECT_EQ(context.GetStyle(element).background.image, painted.texture);
	context.Update();

	AssetPreviewEntry refreshed;
	EXPECT_FALSE(EditorPreviewStore::Request(guids[0], refreshed));
	ASSERT_TRUE(Settle(guids[0]));
	ASSERT_TRUE(EditorPreviewStore::Request(guids[0], refreshed));
	EXPECT_GT(refreshed.size.x, 0);
}

TEST_F(PreviewStoreTest, SourceChangeReproducesOnlyIt) {
	Vector<Guid> guids{GetMemoryResource(MemTag::Editor)};
	if (!StageTextures(2, guids)) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorAssetWatch::Initialize();
	ASSERT_TRUE(Settle(guids[0]));
	ASSERT_TRUE(Settle(guids[1]));
	ASSERT_EQ(EditorPreviewStore::GetCachedCount(), 2u);

	EditorAssetWatch::PollChanges();
	str physical = (_projectRoot / "Assets" / "checker0.png").string();
	EditorAssetWatch::NotifyChanged(Path(physical));
	EditorAssetWatch::PollChanges();
	ASSERT_FALSE(EditorAssetWatch::GetChangedThisFrame().empty()) << physical;
	EditorPreviewStore::Drain();
	ASSERT_EQ(EditorPreviewStore::GetPendingCount(), 0u);

	AssetPreviewEntry entry;
	EXPECT_TRUE(EditorPreviewStore::Request(guids[0], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EXPECT_TRUE(EditorPreviewStore::Request(guids[1], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EditorPreviewStore::Drain();
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);
	EXPECT_TRUE(EditorPreviewStore::Request(guids[0], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);
	EditorAssetWatch::Terminate();
}

TEST_F(PreviewStoreTest, BulkChangeReproducesAsked) {
	Vector<Guid> guids{GetMemoryResource(MemTag::Editor)};
	if (!StageTextures(2, guids)) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorAssetWatch::Initialize();
	ASSERT_TRUE(Settle(guids[0]));
	ASSERT_TRUE(Settle(guids[1]));
	ASSERT_EQ(EditorPreviewStore::GetCachedCount(), 2u);

	for (int i = 0; i < 4000; ++i) {
		EditorAssetWatch::NotifyChanged(Path(std::format("project://Assets/bulk{}.png", i)));
	}
	EditorAssetWatch::PollChanges();
	ASSERT_TRUE(EditorAssetWatch::DidOverflowThisFrame());
	EditorPreviewStore::Drain();
	ASSERT_EQ(EditorPreviewStore::GetPendingCount(), 0u);

	AssetPreviewEntry entry;
	EXPECT_TRUE(EditorPreviewStore::Request(guids[0], entry));
	EXPECT_TRUE(EditorPreviewStore::Request(guids[1], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 2u);
	EditorAssetWatch::Terminate();
}

TEST_F(PreviewStoreTest, DeclineIsCachedNotRetriedEveryFrame) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/notes.txt")), "hello");
	AssetDatabase::ScanProjectDirectory();
	Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/notes.txt"));
	ASSERT_TRUE(guid.IsValid());

	AssetPreviewEntry entry;
	EXPECT_FALSE(EditorPreviewStore::Request(guid, entry));
	EditorPreviewStore::Drain();
	EXPECT_EQ(EditorPreviewStore::GetCachedCount(), 1u);
	EXPECT_EQ(EditorPreviewStore::GetCachedBytes(), 0u);

	for (int i = 0; i < 5; ++i) {
		EXPECT_FALSE(EditorPreviewStore::Request(guid, entry));
		EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u) << i;
	}
}

namespace {

	class UnpreviewedImporter : public IAssetImporter {
	public:
		int imports = 0;

		StringHash GetAssetType() const override { return StringHash("UnpreviewedAsset"); }
		Asset* Import(const Path& sourcePath) override {
			++imports;
			TextAsset* asset = New<TextAsset>(MemTag::Asset);
			asset->Import(sourcePath);
			return asset;
		}
	};

} // namespace

TEST_F(PreviewStoreTest, UnpreviewableTypeDeclinedUnloaded) {
	UnpreviewedImporter importer;
	AssetImporterRegistry::Register(StringHash("UnpreviewedAsset"), &importer, {".unpreviewed"});
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/quiet.unpreviewed")), "q");
	AssetDatabase::ScanProjectDirectory();
	const Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/quiet.unpreviewed"));
	EXPECT_TRUE(guid.IsValid());

	AssetPreviewEntry entry;
	EXPECT_FALSE(EditorPreviewStore::Request(guid, entry));
	EditorPreviewStore::Drain();
	EXPECT_EQ(EditorPreviewStore::GetCachedCount(), 1u);
	EXPECT_EQ(importer.imports, 0);

	EditorPreviews::RegisterProducer(StringHash("UnpreviewedAsset"), [](const AssetPreviewRequest&, AssetPreviewImage&) { return false; });
	EditorPreviewStore::Invalidate(guid);
	EXPECT_FALSE(EditorPreviewStore::Request(guid, entry));
	EditorPreviewStore::Drain();
	EXPECT_EQ(importer.imports, 1);
	AssetImporterRegistry::Unregister(&importer);
}

TEST_F(PreviewStoreTest, InvalidatedPictureIsShownUntilReplacementLands) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/edited.txt")), "e");
	AssetDatabase::ScanProjectDirectory();
	Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/edited.txt"));
	ASSERT_TRUE(guid.IsValid());
	auto produced = std::make_shared<int>(0);
	EditorPreviews::RegisterProducer(StringHash("TextAsset"), [produced](const AssetPreviewRequest&, AssetPreviewImage& out) {
		static constexpr u8 PIXELS[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
		out.size = {2, 2};
		out.pixelsFormat = RHI::Format::RGBA8Unorm;
		out.pixels = PIXELS;
		out.detail = std::format("produce {}", ++*produced);
		return true;
	});
	ASSERT_TRUE(Settle(guid));
	AssetPreviewEntry entry;
	ASSERT_TRUE(EditorPreviewStore::Request(guid, entry));
	ASSERT_EQ(entry.detail, "produce 1");

	EditorPreviewStore::Invalidate(guid);
	EXPECT_TRUE(EditorPreviewStore::Request(guid, entry));
	EXPECT_EQ(entry.detail, "produce 1");
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EXPECT_TRUE(EditorPreviewStore::Request(guid, entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);

	EditorPreviewStore::Drain();
	ASSERT_TRUE(EditorPreviewStore::Request(guid, entry));
	EXPECT_EQ(entry.detail, "produce 2");
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);
	EXPECT_EQ(EditorPreviewStore::GetCachedCount(), 1u);
}

TEST_F(PreviewStoreTest, PictureStaleAfterAssetChange) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/drawn.txt")), "d");
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/source.txt")), "s");
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/unrelated.txt")), "u");
	AssetDatabase::ScanProjectDirectory();
	Guid drawn = AssetDatabase::LookupGuid(Path("project://Assets/drawn.txt"));
	Guid source = AssetDatabase::LookupGuid(Path("project://Assets/source.txt"));
	Guid unrelated = AssetDatabase::LookupGuid(Path("project://Assets/unrelated.txt"));
	ASSERT_TRUE(drawn.IsValid());
	ASSERT_TRUE(source.IsValid());
	ASSERT_TRUE(unrelated.IsValid());
	auto produced = std::make_shared<int>(0);
	auto depends = std::make_shared<bool>(true);
	EditorPreviews::RegisterProducer(StringHash("TextAsset"), [produced, depends, source](const AssetPreviewRequest&,
																						  AssetPreviewImage& out) {
		static constexpr u8 PIXELS[4] = {255, 255, 255, 255};
		out.size = {1, 1};
		out.pixelsFormat = RHI::Format::RGBA8Unorm;
		out.pixels = PIXELS;
		out.detail = std::format("produce {}", ++*produced);
		if (*depends) {
			out.dependencies.push_back(source);
		}
		return true;
	});
	ASSERT_TRUE(Settle(drawn));
	AssetPreviewEntry entry;
	ASSERT_TRUE(EditorPreviewStore::Request(drawn, entry));
	ASSERT_EQ(entry.detail, "produce 1");

	EditorPreviewStore::Invalidate(unrelated);
	EXPECT_TRUE(EditorPreviewStore::Request(drawn, entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);

	EditorPreviewStore::Invalidate(source);
	EXPECT_TRUE(EditorPreviewStore::Request(drawn, entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EditorPreviewStore::Drain();
	ASSERT_TRUE(EditorPreviewStore::Request(drawn, entry));
	EXPECT_EQ(entry.detail, "produce 2");

	*depends = false;
	EditorPreviewStore::Invalidate(drawn);
	ASSERT_TRUE(EditorPreviewStore::Request(drawn, entry));
	EditorPreviewStore::Drain();
	ASSERT_TRUE(EditorPreviewStore::Request(drawn, entry));
	ASSERT_EQ(entry.detail, "produce 3");
	EditorPreviewStore::Invalidate(source);
	EXPECT_TRUE(EditorPreviewStore::Request(drawn, entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);
}

TEST_F(PreviewStoreTest, AnInvalidatedDeclineIsAskedAgain) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/later.txt")), "l");
	AssetDatabase::ScanProjectDirectory();
	Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/later.txt"));
	ASSERT_TRUE(guid.IsValid());
	auto ready = std::make_shared<bool>(false);
	EditorPreviews::RegisterProducer(StringHash("TextAsset"), [ready](const AssetPreviewRequest&, AssetPreviewImage& out) {
		static constexpr u8 PIXELS[4] = {255, 255, 255, 255};
		if (!*ready) {
			return false;
		}
		out.size = {1, 1};
		out.pixelsFormat = RHI::Format::RGBA8Unorm;
		out.pixels = PIXELS;
		return true;
	});
	AssetPreviewEntry entry;
	EXPECT_FALSE(EditorPreviewStore::Request(guid, entry));
	EditorPreviewStore::Drain();
	ASSERT_FALSE(EditorPreviewStore::Request(guid, entry));
	ASSERT_EQ(EditorPreviewStore::GetPendingCount(), 0u);

	*ready = true;
	EditorPreviewStore::Invalidate(guid);
	EXPECT_FALSE(EditorPreviewStore::Request(guid, entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EditorPreviewStore::Drain();
	EXPECT_TRUE(EditorPreviewStore::Request(guid, entry));
}

TEST_F(PreviewStoreTest, ItAnswersSafelyWhileNotInitialized) {
	EditorPreviewStore::Terminate();

	AssetPreviewEntry entry;
	EXPECT_FALSE(EditorPreviewStore::Request(Guid{}, entry));
	EditorPreviewStore::Drain();
	EditorPreviewStore::Invalidate(Guid{});
	EditorPreviewStore::InvalidateAll();
	EXPECT_EQ(EditorPreviewStore::GetCachedCount(), 0u);
	EXPECT_EQ(EditorPreviewStore::GetCachedBytes(), 0u);
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);

	EditorPreviewStore::Initialize();
}

namespace {

	class PreviewStorePipelineTest : public ::testing::Test {
	protected:
		std::filesystem::path _projectRoot;
		bool _engineMounted = false;

		void SetUp() override {
			std::filesystem::path engineAssets = PreviewRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_engineMounted = true;
			}
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_projectRoot = std::filesystem::temp_directory_path() /
						   ("shalltear_pipeline_" + std::to_string(stamp) + "_" +
							std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_projectRoot / "Assets");
			FileSystem::Mount("project", Path(_projectRoot.generic_string()));
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			EditorPreviews::Initialize();
			EditorPreviews::RegisterBuiltIns();
			EditorPreviewStore::Initialize();
		}

		void TearDown() override {
			EditorPreviewStore::Terminate();
			EditorPreviews::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			FileSystem::Unmount("project");
			if (_engineMounted) {
				FileSystem::Unmount("engine");
			}
			std::error_code ec;
			std::filesystem::remove_all(_projectRoot, ec);
		}

		bool StageMaterials(int count, Vector<Guid>& out) {
			std::filesystem::path source = PreviewRepoDir("EngineAssets/Materials/Default.mat");
			Asset* probe = source.empty() ? nullptr : AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
			if (!probe) {
				return false;
			}
			AssetDatabase::UnloadAsset(probe);
			std::error_code ec;
			for (int i = 0; i < count; ++i) {
				std::filesystem::copy_file(source, _projectRoot / "Assets" / ("M" + std::to_string(i) + ".mat"),
										   std::filesystem::copy_options::overwrite_existing, ec);
				if (ec) {
					return false;
				}
			}
			AssetDatabase::ScanProjectDirectory();
			out.clear();
			for (int i = 0; i < count; ++i) {
				Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/M" + std::to_string(i) + ".mat"));
				if (!guid.IsValid()) {
					return false;
				}
				out.push_back(guid);
			}
			return true;
		}

		static bool Frame(const Vector<Guid>& cells, Vector<RHI::TextureHandle>& painted) {
			EditorPreviewStore::Drain();
			bool all = true;
			painted.resize(cells.size());
			for (size_t i = 0; i < cells.size(); ++i) {
				AssetPreviewEntry entry;
				if (EditorPreviewStore::Request(cells[i], entry)) {
					painted[i] = entry.texture;
				} else {
					painted[i] = {};
					all = false;
				}
			}
			SceneSystem::EndFrame();
			RenderSystem::RenderAll();
			return all;
		}
	};

} // anonymous namespace

TEST_F(PreviewStorePipelineTest, PollingProducesEachOnce) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Vector<Guid> cells{GetMemoryResource(MemTag::Editor)};
	if (!StageMaterials(4, cells)) {
		GTEST_SKIP() << "engine://Materials/Default.mat unavailable (no dxcompiler.dll?)";
	}

	Vector<RHI::TextureHandle> painted{GetMemoryResource(MemTag::Editor)};
	bool settled = false;
	for (int frame = 0; frame < 32 && !settled; ++frame) {
		settled = Frame(cells, painted);
	}
	ASSERT_TRUE(settled);
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);

	Vector<RHI::TextureHandle> later{GetMemoryResource(MemTag::Editor)};
	for (int frame = 0; frame < 8; ++frame) {
		ASSERT_TRUE(Frame(cells, later));
		for (size_t i = 0; i < cells.size(); ++i) {
			EXPECT_EQ(later[i], painted[i]) << "cell " << i << ' ' << frame;
		}
	}
}

TEST_F(PreviewStorePipelineTest, InFlightInvalidationRequeues) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Vector<Guid> cells{GetMemoryResource(MemTag::Editor)};
	if (!StageMaterials(1, cells)) {
		GTEST_SKIP() << "engine://Materials/Default.mat unavailable (no dxcompiler.dll?)";
	}

	Vector<RHI::TextureHandle> painted{GetMemoryResource(MemTag::Editor)};
	ASSERT_FALSE(Frame(cells, painted));
	EditorPreviewStore::Drain();
	ASSERT_EQ(EditorPreviewStore::GetPendingCount(), 1u);

	EditorPreviewStore::Invalidate(cells[0]);
	AssetPreviewEntry entry;
	EXPECT_FALSE(EditorPreviewStore::Request(cells[0], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 2u);
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();

	RHI::TextureHandle superseded;
	bool settled = false;
	for (int frame = 0; frame < 16 && !settled; ++frame) {
		Frame(cells, painted);
		if (!superseded.IsValid()) {
			superseded = painted[0];
		}
		settled = painted[0].IsValid() && painted[0] != superseded && EditorPreviewStore::GetPendingCount() == 0;
	}
	EXPECT_TRUE(settled);

	EditorPreviewStore::Invalidate(cells[0]);
	ASSERT_TRUE(Frame(cells, painted));
	EditorPreviewStore::Drain();
	EditorPreviewStore::InvalidateAll();
	EXPECT_TRUE(EditorPreviewStore::Request(cells[0], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 2u);
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
}

TEST_F(PreviewStorePipelineTest, OvertakenProduceLandsStale) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Vector<Guid> cells{GetMemoryResource(MemTag::Editor)};
	if (!StageMaterials(1, cells)) {
		GTEST_SKIP() << "engine://Materials/Default.mat unavailable (no dxcompiler.dll?)";
	}
	AssetPreviewEntry entry;
	ASSERT_FALSE(EditorPreviewStore::Request(cells[0], entry));
	EditorPreviewStore::Drain();
	ASSERT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EditorPreviewStore::Invalidate(cells[0]);
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	EditorPreviewStore::Drain();
	ASSERT_EQ(EditorPreviewStore::GetPendingCount(), 0u);

	EXPECT_TRUE(EditorPreviewStore::Request(cells[0], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EditorPreviewStore::Drain();
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
}

TEST_F(PreviewStorePipelineTest, MaterialsPictureIsRedrawnWhenShaderChanges) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Vector<Guid> cells{GetMemoryResource(MemTag::Editor)};
	if (!StageMaterials(1, cells)) {
		GTEST_SKIP() << "engine://Materials/Default.mat unavailable (no dxcompiler.dll?)";
	}
	Asset* loaded = AssetDatabase::LoadAsset(Path("project://Assets/M0.mat"));
	ASSERT_NE(loaded, nullptr);
	const Guid shader = static_cast<MaterialAsset*>(loaded)->shaderGuid;
	AssetDatabase::UnloadAsset(loaded);
	ASSERT_TRUE(shader.IsValid());

	Vector<RHI::TextureHandle> painted{GetMemoryResource(MemTag::Editor)};
	bool settled = false;
	for (int frame = 0; frame < 16 && !settled; ++frame) {
		settled = Frame(cells, painted) && EditorPreviewStore::GetPendingCount() == 0;
	}
	ASSERT_TRUE(settled);

	EditorPreviewStore::Invalidate(shader);
	AssetPreviewEntry entry;
	EXPECT_TRUE(EditorPreviewStore::Request(cells[0], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);

	EditorPreviewStore::Drain();
	ASSERT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EditorPreviewStore::Invalidate(shader);
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	EditorPreviewStore::Drain();
	ASSERT_EQ(EditorPreviewStore::GetPendingCount(), 0u);
	EXPECT_TRUE(EditorPreviewStore::Request(cells[0], entry));
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EditorPreviewStore::Drain();
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
}

namespace {

	constexpr const char* GREEN_ROW_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tVSOutput output;\n"
		"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
		"\toutput.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return float4(0.0, 1.0, 0.0, 1.0); }\n";

} // anonymous namespace

TEST_F(PreviewStorePipelineTest, MaterialWrittenForRowPreviewsAsOne) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	FileSystem::WriteTextFile(Path("project://Assets/Green.hlsl"), GREEN_ROW_SHADER);
	Asset* shader = AssetDatabase::LoadAsset(Path("project://Assets/Green.hlsl"));
	if (!shader) {
		GTEST_SKIP() << "row's shader didn't import (no dxcompiler.dll?)";
	}
	ASSERT_TRUE(FullScreenPass::Draws(*static_cast<ShaderAsset*>(shader)));
	FileSystem::WriteTextFile(Path("project://Assets/Green.mat"), "{\n\t\"shader\": \"" + shader->assetId.ToString() + "\"\n}\n");
	AssetDatabase::ScanProjectDirectory();
	const Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/Green.mat"));
	ASSERT_TRUE(guid.IsValid());
	Asset* asset = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(asset, nullptr);

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	auto produce = [&](FullScreenPass* row, Handle<Scene> stage, AssetPreviewImage& image) {
		AssetPreviewRequest request;
		request.guid = guid;
		request.asset = asset;
		request.stage = stage;
		request.target = target;
		request.targetSize = RenderSystem::GetViewTargetSize(target);
		request.row = row;
		return EditorPreviews::Produce(request, image);
	};

	Handle<Scene> bare = SceneSystem::CreateIsolatedScene("Bare");
	AssetPreviewImage declined;
	EXPECT_FALSE(produce(nullptr, bare, declined));
	SceneSystem::UnloadScene(bare);

	FullScreenPass row;
	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Lent");
	AssetPreviewImage image;
	ASSERT_TRUE(produce(&row, stage, image));
	EXPECT_EQ(row.material.Get(), asset);
	Handle<SceneObject> subject = stage->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());
	EXPECT_NE(subject->GetComponent<MeshRenderer>()->GetMaterial(0).Get(), asset);
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	for (const Vec2I at : {Vec2I{1, 1}, Vec2I{size.x / 2, size.y / 2}}) {
		const PreviewTexel texel = PreviewTexelAt(pixels, size, at.x, at.y);
		EXPECT_GT(texel.g, 250u) << at.x << ", " << at.y << ")";
		EXPECT_LT(texel.r + texel.b, 6u) << "(" << at.x << ", " << at.y << ")";
	}
	SceneSystem::UnloadScene(stage);
	SceneSystem::EndFrame();
	RenderSystem::DestroyViewTarget(target);
	AssetDatabase::UnloadAsset(asset);
	AssetDatabase::UnloadAsset(shader);

	const Vector<Guid> cells{std::initializer_list<Guid>{guid}, GetMemoryResource(MemTag::Editor)};
	Vector<RHI::TextureHandle> painted{GetMemoryResource(MemTag::Editor)};
	bool settled = false;
	for (int frame = 0; frame < 16 && !settled; ++frame) {
		settled = Frame(cells, painted) && EditorPreviewStore::GetPendingCount() == 0;
	}
	EXPECT_TRUE(settled);
}

TEST(AssetPreviewResampleTest, ReadableFormatsBecomeRGBA8Square) {
	Vector<u8> out{GetMemoryResource(MemTag::Editor)};

	Vector<u8> rgba{GetMemoryResource(MemTag::Editor)};
	Vector<u8> bgra{GetMemoryResource(MemTag::Editor)};
	for (int i = 0; i < 4; ++i) {
		rgba.insert(rgba.end(), {255, 0, 0, 255});
		bgra.insert(bgra.end(), {0, 0, 255, 255});
	}

	EXPECT_EQ(EditorPreviewOps::ResampleToRGBA8(rgba, {2, 2}, RHI::Format::RGBA8SRGB, {4, 4}, out),
			  RHI::Format::RGBA8SRGB);
	ASSERT_EQ(out.size(), 4u * 4u * 4u);
	EXPECT_EQ(out[0], 255);
	EXPECT_EQ(out[1], 0);

	EXPECT_EQ(EditorPreviewOps::ResampleToRGBA8(bgra, {2, 2}, RHI::Format::BGRA8Unorm, {4, 4}, out),
			  RHI::Format::RGBA8Unorm);
	ASSERT_EQ(out.size(), 4u * 4u * 4u);
	EXPECT_EQ(out[0], 255);
	EXPECT_EQ(out[2], 0);
}

TEST(AssetPreviewResampleTest, MagnifyingPointSamplesAndShrinkingAverages) {
	Vector<u8> out{GetMemoryResource(MemTag::Editor)};

	Vector<u8> pair{GetMemoryResource(MemTag::Editor)};
	pair.insert(pair.end(), {0, 0, 0, 255});
	pair.insert(pair.end(), {255, 255, 255, 255});

	ASSERT_EQ(EditorPreviewOps::ResampleToRGBA8(pair, {2, 1}, RHI::Format::RGBA8Unorm, {4, 1}, out),
			  RHI::Format::RGBA8Unorm);
	ASSERT_EQ(out.size(), 16u);
	EXPECT_EQ(out[0], 0);
	EXPECT_EQ(out[4], 0);
	EXPECT_EQ(out[8], 255);
	EXPECT_EQ(out[12], 255);

	ASSERT_EQ(EditorPreviewOps::ResampleToRGBA8(pair, {2, 1}, RHI::Format::RGBA8Unorm, {1, 1}, out),
			  RHI::Format::RGBA8Unorm);
	ASSERT_EQ(out.size(), 4u);
	EXPECT_NEAR(out[0], 128, 1);
}

TEST(AssetPreviewResampleTest, ItRefusesWhatItCannotRead) {
	Vector<u8> out{GetMemoryResource(MemTag::Editor)};
	Vector<u8> tiny{GetMemoryResource(MemTag::Editor)};
	tiny.assign(4, u8{255});

	EXPECT_EQ(EditorPreviewOps::ResampleToRGBA8(tiny, {64, 64}, RHI::Format::RGBA8Unorm, {8, 8}, out),
			  RHI::Format::Unknown);
	EXPECT_TRUE(out.empty());

	EXPECT_EQ(EditorPreviewOps::ResampleToRGBA8(tiny, {1, 1}, RHI::Format::D32Float, {8, 8}, out),
			  RHI::Format::Unknown);
	EXPECT_EQ(EditorPreviewOps::ResampleToRGBA8(tiny, {1, 1}, RHI::Format::RGBA8Unorm, {0, 8}, out),
			  RHI::Format::Unknown);
	EXPECT_EQ(EditorPreviewOps::ResampleToRGBA8(tiny, {0, 0}, RHI::Format::RGBA8Unorm, {8, 8}, out),
			  RHI::Format::Unknown);
}

TEST(AssetPreviewResampleTest, HalfFloatSourceIsDecodedAndClamped) {
	Vector<u8> out{GetMemoryResource(MemTag::Editor)};
	Vector<u8> half{GetMemoryResource(MemTag::Editor)};
	auto push = [&half](u16 bits) {
		half.push_back(static_cast<u8>(bits & 0xFFu));
		half.push_back(static_cast<u8>(bits >> 8));
	};
	push(0x3800);
	push(0x3C00);
	push(0x4400);
	push(0x3C00);

	ASSERT_EQ(EditorPreviewOps::ResampleToRGBA8(half, {1, 1}, RHI::Format::RGBA16Float, {1, 1}, out),
			  RHI::Format::RGBA8Unorm);
	ASSERT_EQ(out.size(), 4u);
	EXPECT_NEAR(out[0], 128, 1);
	EXPECT_EQ(out[1], 255);
	EXPECT_EQ(out[2], 255);
	EXPECT_EQ(out[3], 255);
}

TEST_F(MaterialGoldenTest, DrawnProducerReadNextFrame) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Guid guid = AssetDatabase::LookupGuid(Path("engine://Materials/Default.mat"));
	if (!guid.IsValid()) {
		GTEST_SKIP() << "engine://Materials/Default.mat unavailable (no dxcompiler.dll?)";
	}

	AssetPreviewEntry entry;
	ASSERT_FALSE(EditorPreviewStore::Request(guid, entry));

	EditorPreviewStore::Drain();
	EXPECT_EQ(EditorPreviewStore::GetCachedCount(), 0u);
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);

	SceneSystem::EndFrame();
	RenderSystem::RenderAll();

	EditorPreviewStore::Drain();
	ASSERT_TRUE(EditorPreviewStore::Request(guid, entry));
	EXPECT_TRUE(entry.texture.IsValid());

	EXPECT_EQ(RenderSystem::GetDevice()->GetTextureFormat(entry.texture), RHI::Format::RGBA8Unorm);
	EXPECT_EQ(entry.size.x, entry.size.y);
	EXPECT_GT(EditorPreviewStore::GetCachedBytes(), 0u);
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 0u);

	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);

	RHI::TextureHandle first = entry.texture;
	EditorPreviewStore::Invalidate(guid);
	ASSERT_TRUE(EditorPreviewStore::Request(guid, entry));
	EXPECT_EQ(entry.texture, first);
	EditorPreviewStore::Drain();
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	EditorPreviewStore::Drain();
	ASSERT_TRUE(EditorPreviewStore::Request(guid, entry));
	EXPECT_NE(entry.texture, first);
}

TEST(AssetPreviewOrbitTest, UnturnedOrbitIsThumbnailCamera) {
	AABB unit = AABB::FromCenterExtents(Vec3(3.0f, -2.0f, 1.0f), Vec3(0.5f, 0.5f, 0.5f));
	ViewDesc desc;
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.6f, 30.0f, 1.15f, AssetPreviewOrbit{}, desc));

	EXPECT_NEAR(desc.cameraPosition.x, 3.0f, 1e-4f);
	EXPECT_NEAR(desc.cameraPosition.y, -2.0f, 1e-4f);
	EXPECT_LT(desc.cameraPosition.z, 1.0f);
}

TEST(AssetPreviewOrbitTest, TurningKeepsCameraSameDistanceFromSubject) {
	AABB unit = AABB::FromCenterExtents(Vec3(3.0f, -2.0f, 1.0f), Vec3(0.5f, 0.5f, 0.5f));
	Vec3 center = unit.Center();

	ViewDesc front;
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.0f, 30.0f, 1.15f, AssetPreviewOrbit{}, front));
	AssetPreviewOrbit turned = EditorPreviewOps::OrbitAfterDrag(AssetPreviewOrbit{}, Vec2(120.0f, 40.0f));
	EXPECT_GT(turned.yawDeg, 0.0f);
	EXPECT_GT(turned.pitchDeg, 0.0f);

	ViewDesc side;
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.0f, 30.0f, 1.15f, turned, side));
	EXPECT_GT(Vec3::Distance(front.cameraPosition, side.cameraPosition), 0.1f);
	EXPECT_NEAR(Vec3::Distance(side.cameraPosition, center), Vec3::Distance(front.cameraPosition, center), 1e-3f);
}

TEST(AssetPreviewOrbitTest, ThePitchStopsShortOfBothPoles) {
	AssetPreviewOrbit up = AssetPreviewOrbit{};
	AssetPreviewOrbit down = AssetPreviewOrbit{};
	for (int i = 0; i < 40; ++i) {
		up = EditorPreviewOps::OrbitAfterDrag(up, Vec2(0.0f, 100.0f));
		down = EditorPreviewOps::OrbitAfterDrag(down, Vec2(0.0f, -100.0f));
	}
	EXPECT_LT(up.pitchDeg, 90.0f);
	EXPECT_GT(down.pitchDeg, -90.0f);
	AssetPreviewOrbit spun = AssetPreviewOrbit{};
	for (int i = 0; i < 40; ++i) {
		spun = EditorPreviewOps::OrbitAfterDrag(spun, Vec2(100.0f, 0.0f));
	}
	EXPECT_GT(spun.yawDeg, 360.0f);
}

TEST(AssetPreviewOrbitTest, ZoomClosesInAndStops) {
	AABB unit = AABB::FromCenterExtents({}, Vec3(0.5f, 0.5f, 0.5f));
	AssetPreviewOrbit closer = EditorPreviewOps::OrbitAfterZoom(AssetPreviewOrbit{}, 1.0f);
	EXPECT_LT(closer.zoom, 1.0f);

	ViewDesc framed;
	ViewDesc near;
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.0f, 30.0f, 1.0f, AssetPreviewOrbit{}, framed));
	ASSERT_TRUE(EditorPreviewOps::FitCameraToBounds(unit, 1.0f, 30.0f, 1.0f, closer, near));
	EXPECT_GT(near.cameraPosition.z, framed.cameraPosition.z);

	AssetPreviewOrbit deepIn = AssetPreviewOrbit{};
	AssetPreviewOrbit farOut = AssetPreviewOrbit{};
	for (int i = 0; i < 60; ++i) {
		deepIn = EditorPreviewOps::OrbitAfterZoom(deepIn, 1.0f);
		farOut = EditorPreviewOps::OrbitAfterZoom(farOut, -1.0f);
	}
	EXPECT_GT(deepIn.zoom, 0.0f);
	EXPECT_LT(farOut.zoom, 100.0f);
}

TEST_F(M5GoldenTest, RenderingProducerReportsFrame) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Asset* cube = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
	if (!cube) {
		GTEST_SKIP() << "shipped cube primitive didn't load (no dxcompiler.dll?)";
	}
	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});

	Handle<Scene> stage = SceneSystem::CreateIsolatedScene("Stage");
	AssetPreviewRequest request;
	request.asset = cube;
	request.stage = stage;
	request.target = target;
	request.targetSize = RenderSystem::GetViewTargetSize(target);
	AssetPreviewImage rendered;
	ASSERT_TRUE(EditorPreviews::Produce(request, rendered));
	EXPECT_TRUE(rendered.subjectBounds.IsValid());

	TextureAsset texture;
	texture.size = {4, 4};
	texture.format = RHI::Format::RGBA8Unorm;
	texture.pixels.resize(4 * 4 * 4, 200);
	AssetPreviewRequest textureRequest;
	textureRequest.asset = &texture;
	textureRequest.stage = SceneSystem::CreateIsolatedScene("TextureStage");
	textureRequest.target = target;
	textureRequest.targetSize = RenderSystem::GetViewTargetSize(target);
	AssetPreviewImage flat;
	ASSERT_TRUE(EditorPreviews::Produce(textureRequest, flat));
	EXPECT_FALSE(flat.subjectBounds.IsValid());

	SceneSystem::UnloadScene(textureRequest.stage);
	SceneSystem::UnloadScene(stage);
	SceneSystem::EndFrame();
	RenderSystem::DestroyViewTarget(target);
	AssetDatabase::UnloadAsset(cube);
}

namespace {

	class LivePreviewPaneTest : public PreviewPaneTest {
	protected:
		bool _mountedEngine = false;

		void OnBeforeDatabase() override {
			std::filesystem::path engineAssets = PreviewRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
		}
		void OnDatabaseReady() override {
			WindowSystem::Initialize();
			RenderSystem::Initialize();
		}
		void OnDatabaseGone() override {
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
		}

		Guid StagePrefab(const char* path = "project://live.prefab") {
			Asset* cube = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
			Asset* material = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
			Handle<SceneObject> root = SceneSystem::CreateObject("Root", _scene);
			Handle<SceneObject> child = SceneSystem::CreateObject("Model", _scene, root);
			Handle<MeshRenderer> renderer = child->AddComponent<MeshRenderer>();
			if (cube) {
				renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cube)));
			}
			if (material) {
				renderer->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(material)));
			}
			Guid guid = SceneSystem::SavePrefab(root, Path(path));
			SceneSystem::Destroy(root);
			SceneSystem::EndFrame();
			if (material) {
				AssetDatabase::UnloadAsset(material);
			}
			if (cube) {
				AssetDatabase::UnloadAsset(cube);
			}
			return guid;
		}

		void DragInTheWell(UI::Context& context, UI::ElementHandle panelRoot, i32 dx, bool release = true) {
			UI::ElementHandle well = FindWell(context, panelRoot);
			ASSERT_TRUE(well.IsValid());
			const UI::Rect box = context.GetLayout(well).borderBox;
			ASSERT_GT(box.w, 0.0f);
			i32 x = static_cast<i32>(box.x + box.w * 0.5f);
			i32 y = static_cast<i32>(box.y + box.h * 0.5f);

			auto post = [&](InputEventType type, i32 px) {
				InputEvent event{};
				event.type = type;
				event.button = MouseButton::Left;
				event.mousePosPx = {px, y};
				context.InjectInput(event);
			};
			post(InputEventType::MouseMove, x);
			post(InputEventType::MouseDown, x);
			context.Update();
			post(InputEventType::MouseMove, x + dx / 2);
			context.Update();
			post(InputEventType::MouseMove, x + dx);
			context.Update();
			if (release) {
				post(InputEventType::MouseUp, x + dx);
				context.Update();
			}
		}

		static std::vector<RHI::TextureHandle> WellImages(UI::Context& context, UI::ElementHandle panelRoot) {
			std::vector<RHI::TextureHandle> out;
			UI::ElementHandle well = FindWell(context, panelRoot);
			if (!well.IsValid()) {
				return out;
			}
			const UI::Rect box = context.GetLayout(well).borderBox;
			UI::RenderList list;
			context.BuildRenderList(list);
			for (const UI::UICommand& command : list.commands) {
				if (command.kind != UI::UICommandKind::Quads || !command.texture.IsValid()) {
					continue;
				}
				for (u32 i = 0; i < command.quadCount; ++i) {
					const UI::UIQuad& quad = list.quads[command.firstQuad + i];
					const bool inside = quad.rect.x < box.x + box.w && quad.rect.x + quad.rect.w > box.x &&
										quad.rect.y < box.y + box.h && quad.rect.y + quad.rect.h > box.y;
					if ((quad.flags & UI::QUAD_FLAG_GLYPH) == 0u && inside) {
						out.push_back(command.texture);
					}
				}
			}
			return out;
		}
	};

} // anonymous namespace

TEST_F(LivePreviewPaneTest, DragOpensLastingSession) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Guid prefab = StagePrefab();
	ASSERT_TRUE(prefab.IsValid());
	EditorSelection::SelectAsset(prefab);

	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	SettleStore(context);

	UI::ElementHandle root = context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
	ASSERT_NO_FATAL_FAILURE(DragInTheWell(context, root, 60));
	SceneSystem::EndFrame();

	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);
	for (int i = 0; i < 4; ++i) {
		PanelManager::TickAll(1.0f / 60.0f);
		context.Update();
		SceneSystem::EndFrame();
	}
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);

	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);
}

TEST_F(LivePreviewPaneTest, PassMaterialPaneGoesLive) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	FileSystem::WriteTextFile(Path("project://Assets/Green.hlsl"), GREEN_ROW_SHADER);
	Asset* shader = AssetDatabase::LoadAsset(Path("project://Assets/Green.hlsl"));
	if (!shader) {
		GTEST_SKIP() << "row's shader didn't import (no dxcompiler.dll?)";
	}
	FileSystem::WriteTextFile(Path("project://Assets/Green.mat"), "{\n\t\"shader\": \"" + shader->assetId.ToString() + "\"\n}\n");
	AssetDatabase::ScanProjectDirectory();
	const Guid material = AssetDatabase::LookupGuid(Path("project://Assets/Green.mat"));
	ASSERT_TRUE(material.IsValid());
	EditorSelection::SelectAsset(material);

	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	SettleStore(context);

	UI::ElementHandle root = context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
	ASSERT_NO_FATAL_FAILURE(DragInTheWell(context, root, 60));
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);

	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(shader);
}

TEST_F(LivePreviewPaneTest, DraggingOnATextureNeverGoesLive) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Guid texture = StageChecker();
	if (!texture.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorSelection::SelectAsset(texture);

	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	SettleStore(context);

	UI::ElementHandle root = context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
	const std::vector<RHI::TextureHandle> before = WellImages(context, root);
	ASSERT_EQ(before.size(), 1u);
	for (int i = 0; i < 3; ++i) {
		ASSERT_NO_FATAL_FAILURE(DragInTheWell(context, root, 40));
		SceneSystem::EndFrame();
		EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);
		EXPECT_EQ(WellImages(context, root), before);
	}
	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
}

TEST_F(LivePreviewPaneTest, DragOnUnpreviewableDoesNothing) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UnpreviewedImporter importer;
	AssetImporterRegistry::Register(StringHash("UnpreviewedAsset"), &importer, {".unpreviewed"});
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/quiet.unpreviewed")), "q");
	AssetDatabase::ScanProjectDirectory();
	const Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/quiet.unpreviewed"));
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	SettleStore(context);

	UI::ElementHandle root = context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
	ASSERT_TRUE(FindWell(context, root).IsValid());
	const int importsBefore = importer.imports;
	const std::vector<RHI::TextureHandle> before = WellImages(context, root);
	for (int i = 0; i < 3; ++i) {
		ASSERT_NO_FATAL_FAILURE(DragInTheWell(context, root, 40));
		SceneSystem::EndFrame();
	}
	EXPECT_EQ(importer.imports, importsBefore);
	EXPECT_EQ(WellImages(context, root), before);
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);

	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
	AssetImporterRegistry::Unregister(&importer);
}

namespace {

	class FailingImporter : public IAssetImporter {
	public:
		int imports = 0;

		StringHash GetAssetType() const override { return StringHash("BrokenAsset"); }
		Asset* Import(const Path&) override {
			++imports;
			return nullptr;
		}
	};

} // namespace

TEST_F(LivePreviewPaneTest, FailedImportAskedOncePerDrag) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	FailingImporter importer;
	AssetImporterRegistry::Register(StringHash("BrokenAsset"), &importer, {".broken"});
	EditorPreviews::RegisterProducer(StringHash("BrokenAsset"), [](const AssetPreviewRequest&, AssetPreviewImage&) { return false; }, &importer);
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/mid-edit.broken")), "b");
	AssetDatabase::ScanProjectDirectory();
	const Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/mid-edit.broken"));
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	SettleStore(context);

	UI::ElementHandle root = context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
	const int importsBefore = importer.imports;
	const std::vector<RHI::TextureHandle> before = WellImages(context, root);
	for (int i = 0; i < 3; ++i) {
		ASSERT_NO_FATAL_FAILURE(DragInTheWell(context, root, 40));
		SceneSystem::EndFrame();
	}
	EXPECT_EQ(importer.imports, importsBefore + 1);
	EXPECT_EQ(WellImages(context, root), before);

	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
	EditorPreviews::UnregisterOwnedBy(&importer);
	AssetImporterRegistry::Unregister(&importer);
}

TEST_F(LivePreviewPaneTest, AClickThatNeverMovesOpensNothing) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Guid prefab = StagePrefab();
	EditorSelection::SelectAsset(prefab);
	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	SettleStore(context);

	UI::ElementHandle root = context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
	UI::ElementHandle well = FindWell(context, root);
	ASSERT_TRUE(well.IsValid());
	const UI::Rect box = context.GetLayout(well).borderBox;
	i32 x = static_cast<i32>(box.x + box.w * 0.5f);
	i32 y = static_cast<i32>(box.y + box.h * 0.5f);
	for (InputEventType type : {InputEventType::MouseMove, InputEventType::MouseDown, InputEventType::MouseUp}) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {x, y};
		context.InjectInput(event);
	}
	context.Update();
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);

	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
}

TEST_F(LivePreviewPaneTest, EveryReleasePointClosesTheLiveSession) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Guid first = StagePrefab("project://one.prefab");
	Guid second = StagePrefab("project://two.prefab");
	ASSERT_TRUE(first.IsValid() && second.IsValid());

	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};

	auto openThen = [&](const Guid& guid, auto&& close) {
		EditorSelection::SelectAsset(guid);
		UI::Context context(desc, &fonts);
		BuildInspectorInto(panel, context);
		SettleStore(context);
		UI::ElementHandle root = context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
		ASSERT_NO_FATAL_FAILURE(DragInTheWell(context, root, 60));
		SceneSystem::EndFrame();
		ASSERT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);
		close(context, root);
		SceneSystem::EndFrame();
		EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);
	};

	ASSERT_NO_FATAL_FAILURE(openThen(first, [&](UI::Context& context, UI::ElementHandle) {
		EditorSelection::SelectAsset(second);
		BuildInspectorInto(panel, context);
	}));
	ASSERT_NO_FATAL_FAILURE(openThen(second, [](UI::Context&, UI::ElementHandle) {
		EditorEvents::ModuleReloadBegan.Publish(EventBus::GetGlobal(), EditorModuleReloadBeganEvent{});
	}));
	ASSERT_NO_FATAL_FAILURE(openThen(first, [](UI::Context&, UI::ElementHandle) {
		EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(),
											  EditorPlayModeChangedEvent{PlayModeState::Playing});
	}));
	ASSERT_NO_FATAL_FAILURE(openThen(second, [&](UI::Context& context, UI::ElementHandle root) {
		ClickPaneHeader(context, root);
		PanelManager::TickAll(1.0f / 60.0f);
		context.Update();
	}));

	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);
}

TEST_F(LivePreviewPaneTest, TwoInspectorsOrbitSeparately) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Guid prefab = StagePrefab();
	EditorSelection::SelectAsset(prefab);

	Handle<EditorPanel> first = OpenInspector();
	Handle<EditorPanel> second = OpenInspector();
	ASSERT_TRUE(first.IsValid() && second.IsValid());
	ASSERT_NE(first.Get(), second.Get());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context firstContext(desc, &fonts);
	UI::Context secondContext(desc, &fonts);
	BuildInspectorInto(first, firstContext);
	BuildInspectorInto(second, secondContext);
	SettleStore(firstContext);
	SettleStore(secondContext);

	UI::ElementHandle firstRoot = firstContext.MakeHandle(firstContext.GetElement(firstContext.GetRoot())->firstChild);
	UI::ElementHandle secondRoot =
		secondContext.MakeHandle(secondContext.GetElement(secondContext.GetRoot())->firstChild);
	ASSERT_NO_FATAL_FAILURE(DragInTheWell(firstContext, firstRoot, 60));
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);

	ASSERT_NO_FATAL_FAILURE(DragInTheWell(secondContext, secondRoot, 60));
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 2u);

	PanelManager::ClosePanel(first);
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);
	PanelManager::ClosePanel(second);
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);
}

TEST_F(LivePreviewPaneTest, EditedAssetKeepsOldPicture) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Guid guid = StageChecker();
	if (!guid.IsValid()) {
		GTEST_SKIP() << "checker.png fixture not found";
	}
	EditorSelection::SelectAsset(guid);
	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	SettleStore(context);

	UI::ElementHandle root = context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
	UI::ElementHandle well = FindWell(context, root);
	auto painted = [&]() {
		const Vector<PaintedPicture> pictures = PicturesIn(context, well);
		return pictures.size() == 1 ? pictures[0].texture : RHI::TextureHandle{};
	};
	const RHI::TextureHandle before = painted();
	ASSERT_TRUE(before.IsValid());

	EditorPreviewStore::Invalidate(guid);
	for (int i = 0; i < 3; ++i) {
		PanelManager::TickAll(1.0f / 60.0f);
		context.Update();
		EXPECT_TRUE(painted().IsValid());
	}
	EXPECT_EQ(EditorPreviewStore::GetPendingCount(), 1u);
	SettleStore(context);
	EXPECT_TRUE(painted().IsValid());
	EXPECT_NE(painted(), before);

	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
}

TEST_F(LivePreviewPaneTest, LiveRendersOnTurnOrEditOnly) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Guid prefab = StagePrefab();
	ASSERT_TRUE(prefab.IsValid());
	EditorSelection::SelectAsset(prefab);
	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	SettleStore(context);

	auto frame = [&]() -> u32 {
		EditorPreviewStore::Drain();
		PanelManager::TickAll(1.0f / 60.0f);
		context.Update();
		SceneSystem::EndFrame();
		RenderSystem::RenderAll();
		return RenderSystem::GetStats().drawCalls;
	};
	for (int i = 0; i < 3; ++i) {
		frame();
	}
	ASSERT_EQ(frame(), 0u);

	UI::ElementHandle root = context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
	ASSERT_NO_FATAL_FAILURE(DragInTheWell(context, root, 60));
	ASSERT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);
	EXPECT_GT(frame(), 0u);
	EXPECT_EQ(frame(), 0u);

	EditorPreviewStore::Invalidate(prefab);
	EXPECT_GT(frame(), 0u);
	EXPECT_GT(frame(), 0u);
	EXPECT_EQ(frame(), 0u);
	EXPECT_EQ(frame(), 0u);

	PanelManager::ClosePanel(panel);
	SceneSystem::EndFrame();
}

namespace {

	struct PaneProbe {
		int shown = 0;
		int hidden = 0;
		int ticks = 0;
		int presses = 0;
		Guid lastShown;
		Guid lastHidden;
		UI::ElementHandle control;
		std::vector<UI::ElementHandle> refreshed;
	};

	const int PANE_OWNER = 0;

	struct PaneRegistration {
		~PaneRegistration() { EditorPreviews::UnregisterOwnedBy(&PANE_OWNER); }
	};

	StringHash TextAssetType() {
		IAssetImporter* importer = AssetImporterRegistry::FindByExtension(Path("notes.txt").GetExtension());
		return importer ? importer->GetAssetType() : StringHash{};
	}

	void RegisterProbePane(PaneProbe& probe) {
		AssetPreviewPaneDesc desc;
		desc.onShow = [&probe](const Guid& guid) {
			++probe.shown;
			probe.lastShown = guid;
		};
		desc.onHide = [&probe](const Guid& guid) {
			++probe.hidden;
			probe.lastHidden = guid;
		};
		desc.buildHeader = [&probe](UI::Context& context, UI::ElementHandle header, const Guid&) {
			UI::Widgets::ButtonDesc button;
			button.label = "PaneControl";
			button.font = UISystem::GetDefaultFont();
			button.fontSizePx = EditorTheme::GetMetric(EditorTheme::MetricToken::FontBody);
			button.onClick = [&probe]() { ++probe.presses; };
			const UI::ElementHandle control = UI::Widgets::Button(context, header, button);
			probe.control = control;
			return AssetPreviewPaneRefreshFn([&probe, control](UI::Context&) {
				++probe.ticks;
				probe.refreshed.push_back(control);
			});
		};
		desc.buildBody = [](UI::Context& context, UI::ElementHandle well, const Guid&) {
			UI::ElementHandle line = context.CreateElement();
			context.AppendChild(well, line);
			context.SetText(line, UISystem::GetDefaultFont(), EditorTheme::GetMetric(EditorTheme::MetricToken::FontBody),
							Color::White(), "PaneBody");
			return AssetPreviewPaneRefreshFn{};
		};
		EditorPreviews::RegisterPane(TextAssetType(), std::move(desc), &PANE_OWNER);
	}

	void TickPanels(UI::Context& context, int frames = 1) {
		for (int i = 0; i < frames; ++i) {
			PanelManager::TickAll(1.0f / 60.0f);
			context.Update();
		}
	}

	UI::ElementHandle PanelRootOf(UI::Context& context) {
		return context.MakeHandle(context.GetElement(context.GetRoot())->firstChild);
	}

} // anonymous namespace

TEST_F(PreviewPaneTest, RegisteredPaneReplacesPicture) {
	EditorPreviews::Initialize();
	PaneProbe probe;
	PaneRegistration registration;
	RegisterProbePane(probe);
	Guid guid = StageText();
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	UI::ElementHandle root = PanelRootOf(context);
	SettleStore(context);

	EXPECT_TRUE(AnyTextContains(context, root, "notes.txt"));
	EXPECT_TRUE(AnyTextContains(context, root, "PaneControl"));
	EXPECT_TRUE(AnyTextContains(context, root, "PaneBody"));
	EXPECT_TRUE(PicturesIn(context, FindWell(context, root)).empty());
	EXPECT_EQ(probe.shown, 1);
	EXPECT_EQ(probe.lastShown, guid);
	EXPECT_EQ(probe.hidden, 0);
}

TEST_F(PreviewPaneTest, RegistrationHearsSelectionAndNotRebuild) {
	EditorPreviews::Initialize();
	PaneProbe probe;
	PaneRegistration registration;
	RegisterProbePane(probe);
	Guid first = StageText();
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/other.txt")), "again");
	AssetDatabase::ScanProjectDirectory();
	Guid second = AssetDatabase::LookupGuid(Path("project://Assets/other.txt"));
	ASSERT_TRUE(first.IsValid() && second.IsValid());

	EditorSelection::SelectAsset(first);
	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	UI::ElementHandle root = PanelRootOf(context);
	ASSERT_EQ(probe.shown, 1);

	ASSERT_NO_FATAL_FAILURE(ClickPaneHeader(context, root));
	TickPanels(context);
	EXPECT_FALSE(AnyTextContains(context, root, "PaneBody"));
	EXPECT_TRUE(AnyTextContains(context, root, "PaneControl"));
	ASSERT_NO_FATAL_FAILURE(ClickPaneHeader(context, root));
	TickPanels(context);
	EXPECT_TRUE(AnyTextContains(context, root, "PaneBody"));
	EXPECT_EQ(probe.shown, 1);
	EXPECT_EQ(probe.hidden, 0);

	EditorSelection::SelectAsset(second);
	TickPanels(context);
	EXPECT_EQ(probe.hidden, 1);
	EXPECT_EQ(probe.lastHidden, first);
	EXPECT_EQ(probe.shown, 2);
	EXPECT_EQ(probe.lastShown, second);

	EditorSelection::Clear();
	TickPanels(context);
	EXPECT_EQ(probe.hidden, 2);
	EXPECT_EQ(probe.lastHidden, second);
}

TEST_F(PreviewPaneTest, HeaderRefreshRunsWhileCollapsed) {
	EditorPreviews::Initialize();
	PaneProbe probe;
	PaneRegistration registration;
	RegisterProbePane(probe);
	Guid guid = StageText();
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	UI::ElementHandle root = PanelRootOf(context);

	ASSERT_TRUE(context.IsAlive(probe.control));
	ASSERT_GT(context.GetLayout(probe.control).borderBox.w, 0.0f);
	ClickElement(context, probe.control);
	context.Update();
	TickPanels(context);
	EXPECT_EQ(probe.presses, 1);
	EXPECT_TRUE(AnyTextContains(context, root, "PaneBody"));

	ASSERT_NO_FATAL_FAILURE(ClickPaneHeader(context, root));
	TickPanels(context);
	ASSERT_FALSE(AnyTextContains(context, root, "PaneBody"));
	const int before = probe.ticks;
	TickPanels(context, 3);
	EXPECT_EQ(probe.ticks, before + 3);
	ASSERT_FALSE(probe.refreshed.empty());
	EXPECT_EQ(probe.refreshed.back(), probe.control);
}

TEST_F(PreviewPaneTest, SweepTakesPaneBackAndPictureReturns) {
	EditorPreviews::Initialize();
	PaneProbe probe;
	PaneRegistration registration;
	RegisterProbePane(probe);
	Guid guid = StageText();
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	ASSERT_TRUE(AnyTextContains(context, PanelRootOf(context), "PaneBody"));

	EXPECT_EQ(EditorPreviews::UnregisterOwnedBy(&PANE_OWNER), 1u);
	const int swept = probe.ticks;
	TickPanels(context, 2);
	EXPECT_EQ(probe.ticks, swept);
	UI::Context rebuilt(desc, &fonts);
	BuildInspectorInto(panel, rebuilt);
	UI::ElementHandle rebuiltRoot = PanelRootOf(rebuilt);
	EXPECT_FALSE(AnyTextContains(rebuilt, rebuiltRoot, "PaneBody"));
	EXPECT_FALSE(AnyTextContains(rebuilt, rebuiltRoot, "PaneControl"));
	EXPECT_TRUE(FindWell(rebuilt, rebuiltRoot).IsValid());
}

TEST_F(PreviewPaneTest, ReloadDropsContentBeforeImage) {
	EditorPreviews::Initialize();
	PaneProbe probe;
	PaneRegistration registration;
	RegisterProbePane(probe);
	Guid guid = StageText();
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	Handle<EditorPanel> panel = OpenInspector();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	BuildInspectorInto(panel, context);
	UI::ElementHandle root = PanelRootOf(context);
	ASSERT_TRUE(AnyTextContains(context, root, "PaneControl"));
	ASSERT_EQ(probe.shown, 1);

	EditorEvents::ModuleReloadBegan.Publish(EventBus::GetGlobal(), EditorModuleReloadBeganEvent{});
	EXPECT_EQ(probe.hidden, 1);
	EXPECT_FALSE(AnyTextContains(context, root, "PaneControl"));
	EXPECT_FALSE(AnyTextContains(context, root, "PaneBody"));

	EditorEvents::ModuleReloadEnded.Publish(EventBus::GetGlobal(), EditorModuleReloadEndedEvent{});
	TickPanels(context);
	EXPECT_TRUE(AnyTextContains(context, root, "PaneControl"));
	EXPECT_EQ(probe.shown, 2);
}

TEST_F(PreviewPaneTest, InspectorsShowAndHideSeparately) {
	EditorPreviews::Initialize();
	PaneProbe probe;
	PaneRegistration registration;
	RegisterProbePane(probe);
	Guid guid = StageText();
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 700};
	UI::Context context(desc, &fonts);
	Handle<EditorPanel> first = OpenInspector();
	Handle<EditorPanel> second = OpenInspector();
	BuildInspectorInto(first, context);
	const UI::ElementHandle firstControl = probe.control;
	BuildInspectorInto(second, context);
	const UI::ElementHandle secondControl = probe.control;
	const UI::ElementHandle secondRoot = context.MakeHandle(context.GetElement(context.GetRoot())->lastChild);
	ASSERT_NE(firstControl, secondControl);
	EXPECT_EQ(probe.shown, 2);

	probe.refreshed.clear();
	PanelManager::TickAll(1.0f / 60.0f);
	EXPECT_EQ(std::count(probe.refreshed.begin(), probe.refreshed.end(), firstControl), 1);
	EXPECT_EQ(std::count(probe.refreshed.begin(), probe.refreshed.end(), secondControl), 1);

	PanelManager::ClosePanel(first);
	EXPECT_EQ(probe.hidden, 1);
	EXPECT_TRUE(AnyTextContains(context, secondRoot, "PaneControl"));
}
