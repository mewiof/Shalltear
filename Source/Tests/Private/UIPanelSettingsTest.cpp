#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/UIPanelSettings.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	namespace fs = std::filesystem;

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

	class UIPanelSettingsFixture : public ::testing::Test {
	protected:
		fs::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() /
					("shalltear_panelsettings_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root);
			TypeRegistry::InitializePrimitives();
			UIPanelSettings::GetReflectType();
		}

		void TearDown() override {
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		Path PathIn(const char* name) const { return Path((_root / name).generic_string()); }
	};

	class UIPanelSettingsBindingFixture : public UIPanelSettingsFixture {
	protected:
		bool _mountedEngine = false;

		void SetUp() override {
			UIPanelSettingsFixture::SetUp();
			std::error_code ec;
			fs::create_directories(_root / "Assets", ec);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			UIPanelSettingsFixture::TearDown();
		}

		Guid FontGuid() {
			if (!_mountedEngine) {
				return Guid{};
			}
			Asset* font = AssetDatabase::LoadAsset(Path("engine://Fonts/Inter/Inter-Regular.ttf"));
			if (!font) {
				return Guid{};
			}
			Guid guid = font->assetId;
			AssetDatabase::UnloadAsset(font);
			return guid;
		}

		void WriteSettings(const char* name, const Guid& font) {
			str document = str(R"({"scaleMode": "ScaleWithScreenSize", "font": )") +
						   str(R"({"_handle": true, "pointeeType": "FontAsset", "assetId": ")") +
						   font.ToString() + str(R"("}})");
			ASSERT_TRUE(FileSystem::WriteTextFile(Path(str("project://Assets/") + name), document));
		}
	};

} // anonymous namespace

TEST_F(UIPanelSettingsFixture, TheDefaultsAreTheConventionalOnes) {
	UIPanelSettings settings;
	EXPECT_EQ(settings.scaleMode, UIPanelScaleMode::ConstantPhysicalSize);
	EXPECT_FLOAT_EQ(settings.scale, 1.0f);
	EXPECT_FLOAT_EQ(settings.referenceDPI, 96.0f);
	EXPECT_EQ(settings.referenceResolution, (Vec2I{1200, 800}));
	EXPECT_EQ(settings.screenMatchMode, UIPanelScreenMatchMode::MatchWidthOrHeight);
	EXPECT_FLOAT_EQ(settings.match, 0.0f);
	EXPECT_FLOAT_EQ(settings.sortOrder, 0.0f);
	EXPECT_FALSE(settings.font.IsValid());
}

TEST_F(UIPanelSettingsFixture, ConstantPixelSizeIgnoresScreenAndDPI) {
	UIPanelSettings settings;
	settings.scaleMode = UIPanelScaleMode::ConstantPixelSize;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale({1920, 1080}, 1.25f), 1.0f);
	EXPECT_FLOAT_EQ(settings.ResolveContentScale({320, 240}, 2.0f), 1.0f);
	settings.scale = 2.0f;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale({1920, 1080}, 1.25f), 2.0f);
}

TEST_F(UIPanelSettingsFixture, ConstantPhysicalSizeFollowsDPIAgainstReference) {
	UIPanelSettings settings;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale({1920, 1080}, 1.25f), 1.25f);
	settings.referenceDPI = 192.0f;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale({1920, 1080}, 2.0f), 1.0f);
	settings.scale = 3.0f;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale({1920, 1080}, 2.0f), 3.0f);
	EXPECT_FLOAT_EQ(settings.ResolveContentScale({8, 8}, 2.0f), 3.0f);
}

TEST_F(UIPanelSettingsFixture, ScaleIsRatioToReference) {
	UIPanelSettings settings;
	settings.scaleMode = UIPanelScaleMode::ScaleWithScreenSize;
	settings.referenceResolution = {1200, 800};
	EXPECT_FLOAT_EQ(settings.ResolveContentScale({2400, 1600}, 1.0f), 2.0f);
	settings.match = 1.0f;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale({2400, 1600}, 1.0f), 2.0f);

	const Vec2I wide{2400, 800};
	settings.match = 0.0f;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale(wide, 1.0f), 2.0f);
	settings.match = 1.0f;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale(wide, 1.0f), 1.0f);
	settings.match = 0.5f;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale(wide, 1.0f), 1.5f);
	settings.screenMatchMode = UIPanelScreenMatchMode::Expand;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale(wide, 1.0f), 1.0f);
	settings.screenMatchMode = UIPanelScreenMatchMode::Shrink;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale(wide, 1.0f), 2.0f);
	EXPECT_FLOAT_EQ(settings.ResolveContentScale(wide, 2.0f), 2.0f);
	settings.scale = 0.5f;
	EXPECT_FLOAT_EQ(settings.ResolveContentScale(wide, 1.0f), 1.0f);
}

TEST_F(UIPanelSettingsFixture, ZeroScaleStillRenders) {
	UIPanelSettings settings;
	settings.scale = 0.0f;
	EXPECT_GT(settings.ResolveContentScale({1920, 1080}, 1.0f), 0.0f);
	settings.scale = 1.0f;
	settings.scaleMode = UIPanelScaleMode::ScaleWithScreenSize;
	settings.referenceResolution = {0, 0};
	EXPECT_FLOAT_EQ(settings.ResolveContentScale({1920, 1080}, 1.0f), 1.0f);
}

TEST_F(UIPanelSettingsFixture, NullSettingsAreDefaults) {
	UIPanelSettings defaults;
	EXPECT_FLOAT_EQ(UIPanelSettings::ResolveDefaultContentScale({1920, 1080}, 1.25f),
					defaults.ResolveContentScale({1920, 1080}, 1.25f));
	EXPECT_FLOAT_EQ(UIPanelSettings::ResolveDefaultContentScale({640, 360}, 2.0f),
					defaults.ResolveContentScale({640, 360}, 2.0f));
}

TEST_F(UIPanelSettingsFixture, EveryFieldRoundTripsThroughTextForm) {
	UIPanelSettings written;
	written.scaleMode = UIPanelScaleMode::ScaleWithScreenSize;
	written.scale = 1.5f;
	written.referenceDPI = 144.0f;
	written.referenceResolution = {1920, 1080};
	written.screenMatchMode = UIPanelScreenMatchMode::Expand;
	written.match = 0.25f;
	written.sortOrder = -3.0f;
	written.fontSizePx = 18.0f;
	written.textColor = Color(0.1f, 0.2f, 0.3f, 0.4f);

	Path document = PathIn("Round.slpanel");
	ASSERT_TRUE(written.Export(document));

	UIPanelSettings read;
	ASSERT_TRUE(read.Import(document));
	EXPECT_EQ(read.scaleMode, UIPanelScaleMode::ScaleWithScreenSize);
	EXPECT_FLOAT_EQ(read.scale, 1.5f);
	EXPECT_FLOAT_EQ(read.referenceDPI, 144.0f);
	EXPECT_EQ(read.referenceResolution, (Vec2I{1920, 1080}));
	EXPECT_EQ(read.screenMatchMode, UIPanelScreenMatchMode::Expand);
	EXPECT_FLOAT_EQ(read.match, 0.25f);
	EXPECT_FLOAT_EQ(read.sortOrder, -3.0f);
	EXPECT_FLOAT_EQ(read.fontSizePx, 18.0f);
	EXPECT_FLOAT_EQ(read.textColor.r, 0.1f);
	EXPECT_FLOAT_EQ(read.textColor.a, 0.4f);

	str text = FileSystem::ReadTextFile(document, MemTag::Debug);
	EXPECT_NE(text.find("ScaleWithScreenSize"), str::npos) << text;
	EXPECT_NE(text.find("referenceResolution"), str::npos) << text;
}

TEST_F(UIPanelSettingsFixture, ImporterOwnsExtensionAndCanMintOne) {
	UIPanelSettingsImporter importer;
	EXPECT_EQ(importer.GetAssetType(), StringHash("UIPanelSettings"));
	EXPECT_TRUE(importer.CanCreateAsset());
	EXPECT_EQ(importer.GetCreateMenuLabel(), "Panel Settings");

	Asset* minted = importer.CreateAsset();
	ASSERT_NE(minted, nullptr);
	EXPECT_EQ(minted->GetAssetTypeId(), StringHash("UIPanelSettings"));
	Path document = PathIn("Minted.slpanel");
	ASSERT_TRUE(minted->Export(document));
	Delete(minted);

	Asset* reimported = importer.Import(document);
	ASSERT_NE(reimported, nullptr);
	EXPECT_EQ(static_cast<UIPanelSettings*>(reimported)->scaleMode, UIPanelScaleMode::ConstantPhysicalSize);
	Delete(reimported);

	EXPECT_EQ(importer.Import(PathIn("Absent.slpanel")), nullptr);
}

TEST_F(UIPanelSettingsBindingFixture, SettingsBindUnloadedFont) {
	Guid font = FontGuid();
	if (!font.IsValid()) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	ASSERT_GE(AssetDatabase::CollectGarbage(), 1u);

	WriteSettings("Cold.slpanel", font);
	Asset* loaded = AssetDatabase::LoadAsset(Path("project://Assets/Cold.slpanel"));
	ASSERT_NE(loaded, nullptr);
	UIPanelSettings* settings = static_cast<UIPanelSettings*>(loaded);
	EXPECT_EQ(settings->scaleMode, UIPanelScaleMode::ScaleWithScreenSize);
	EXPECT_TRUE(settings->font.IsValid());

	AssetDatabase::CollectGarbage();
	EXPECT_TRUE(settings->font.IsValid());

	Handle<Asset> witness(settings->font.Get());
	ASSERT_TRUE(witness.IsValid());
	AssetDatabase::UnloadAsset(loaded);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(witness.Get(), nullptr);
}

TEST_F(UIPanelSettingsBindingFixture, CookRecordsFontReference) {
	Guid font = FontGuid();
	if (!font.IsValid()) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	WriteSettings("Cooked.slpanel", font);
	Asset* loaded = AssetDatabase::LoadAsset(Path("project://Assets/Cooked.slpanel"));
	ASSERT_NE(loaded, nullptr);

	PakStringPool strings;
	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Debug)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Debug)};
	CookWriter descriptor(descriptorBytes, strings);
	CookWriter payload(payloadBytes, strings);
	ASSERT_TRUE(loaded->Cook(descriptor, payload));

	bool noted = false;
	for (const Guid& reference : descriptor.GetReferences()) {
		noted = noted || reference == font;
	}
	EXPECT_TRUE(noted);
	AssetDatabase::UnloadAsset(loaded);
}
