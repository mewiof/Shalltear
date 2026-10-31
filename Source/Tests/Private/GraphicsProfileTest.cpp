#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/Graphics.h"
#include "Shalltear/Core/Graphics/GraphicsProfile.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/SettingsOps.h"

#include "Graphics/GraphicsSettings.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	namespace fs = std::filesystem;

	bool NoKeyIsAHash(const str& document) {
		for (size_t open = document.find('"'); open != str::npos;) {
			const size_t close = document.find('"', open + 1);
			if (close == str::npos) {
				return true;
			}
			const size_t colon = document.find_first_not_of(" \t", close + 1);
			const bool isKey = colon != str::npos && document[colon] == ':';
			const size_t length = close - open - 1;
			if (isKey && length == 16) {
				bool allHex = true;
				for (size_t index = open + 1; index < close; ++index) {
					allHex = allHex && std::isxdigit(static_cast<unsigned char>(document[index])) != 0;
				}
				if (allHex) {
					return false;
				}
			}
			open = document.find('"', close + 1);
		}
		return true;
	}

	class GraphicsProfileFixture : public ::testing::Test {
	protected:
		fs::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() /
					("shalltear_gfxprofile_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root);
			TypeRegistry::InitializePrimitives();
			GraphicsProfile::GetReflectType();
		}

		void TearDown() override {
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		Path PathIn(const char* name) const { return Path((_root / name).generic_string()); }
	};

} // namespace

TEST_F(GraphicsProfileFixture, EveryProfileFieldIsClassified) {
	const TypeInfo* type = GraphicsProfile::GetReflectType();
	ASSERT_NE(type, nullptr);

	u32 classified = 0;
	for (u32 index = 0; index < type->fieldCount; ++index) {
		const FieldInfo& field = type->fields[index];
		MutabilityClass mutability = MutabilityClass::Hot;
		EXPECT_TRUE(Settings::GetFieldMutability(&field, mutability)) << (field.displayName ? field.displayName : "?");
		EXPECT_NE(mutability, MutabilityClass::Restart);
		++classified;
	}
	EXPECT_GT(classified, 0u);

	EXPECT_EQ(type->fieldCount, 12u);
}

TEST_F(GraphicsProfileFixture, BuiltInDefaultMatchesNoProfile) {
	GraphicsProfile* profile = New<GraphicsProfile>(MemTag::Asset);
	ASSERT_NE(profile, nullptr);
	EXPECT_EQ(profile->msaa, 1);
	EXPECT_FLOAT_EQ(profile->renderScale, 1.0f);
	EXPECT_EQ(profile->tonemap, TonemapCurve::None);
	EXPECT_EQ(profile->lightsPerView, 256);
	EXPECT_EQ(profile->lightsPerCluster, 32);
	EXPECT_EQ(profile->shadowCascades, 4);
	EXPECT_EQ(profile->shadowPcfTaps, 5);
	EXPECT_EQ(profile->shadowAtlasSize, 2048);
	EXPECT_TRUE(profile->transparentReceiveShadows);
	Delete(profile);
}

TEST_F(GraphicsProfileFixture, ProfileRoundTripsThroughTextForm) {
	GraphicsProfile* written = New<GraphicsProfile>(MemTag::Asset);
	written->msaa = 4;
	written->renderScale = 0.75f;
	written->lightsPerView = 64;
	written->lightsPerCluster = 16;
	written->shadowDistance = 42.5f;
	written->shadowCascades = 2;
	written->shadowFirstCascadeDistance = 2.5f;
	written->shadowPcfTaps = 7;
	written->shadowAtlasSize = 1024;
	written->transparentReceiveShadows = false;
	written->tonemap = TonemapCurve::AgX;

	const Path document = PathIn("Tuned.slprofile");
	ASSERT_TRUE(written->Export(document));
	Delete(written);

	const str text = FileSystem::ReadTextFile(document);
	EXPECT_NE(text.find("shadowFirstCascadeDistance"), str::npos) << text;
	EXPECT_TRUE(NoKeyIsAHash(text)) << text;

	GraphicsProfile* read = New<GraphicsProfile>(MemTag::Asset);
	ASSERT_TRUE(read->Import(document));
	EXPECT_EQ(read->msaa, 4);
	EXPECT_FLOAT_EQ(read->renderScale, 0.75f);
	EXPECT_EQ(read->lightsPerView, 64);
	EXPECT_EQ(read->lightsPerCluster, 16);
	EXPECT_FLOAT_EQ(read->shadowDistance, 42.5f);
	EXPECT_EQ(read->shadowCascades, 2);
	EXPECT_FLOAT_EQ(read->shadowFirstCascadeDistance, 2.5f);
	EXPECT_EQ(read->shadowPcfTaps, 7);
	EXPECT_EQ(read->shadowAtlasSize, 1024);
	EXPECT_FALSE(read->transparentReceiveShadows);
	EXPECT_EQ(read->tonemap, TonemapCurve::AgX);
	EXPECT_NE(text.find("AgX"), str::npos) << text;
	Delete(read);
}

TEST_F(GraphicsProfileFixture, MissingKeyKeepsDefault) {
	const Path document = PathIn("Sparse.slprofile");
	ASSERT_TRUE(FileSystem::WriteTextFile(document, str("{\n\t\"msaa\": 8\n}\n")));

	GraphicsProfile* read = New<GraphicsProfile>(MemTag::Asset);
	ASSERT_TRUE(read->Import(document));
	EXPECT_EQ(read->msaa, 8);
	EXPECT_FLOAT_EQ(read->renderScale, 1.0f);
	EXPECT_EQ(read->lightsPerView, 256);
	EXPECT_EQ(read->shadowPcfTaps, 5);
	EXPECT_TRUE(read->transparentReceiveShadows);
	Delete(read);
}

TEST_F(GraphicsProfileFixture, RetiredLambdaKeyStillReads) {
	const Path document = PathIn("SavedBeforeX54.slprofile");
	ASSERT_TRUE(FileSystem::WriteTextFile(
		document, str("{\n\t\"_type\": \"GraphicsProfile\",\n\t\"shadowDistance\": 150,\n\t\"shadowCascades\": 4,\n"
					  "\t\"shadowCascadeLambda\": 0.5,\n\t\"shadowPcfTaps\": 7\n}\n")));

	GraphicsProfile* read = New<GraphicsProfile>(MemTag::Asset);
	ASSERT_TRUE(read->Import(document));
	EXPECT_FLOAT_EQ(read->shadowDistance, 150.0f);
	EXPECT_EQ(read->shadowCascades, 4);
	EXPECT_EQ(read->shadowPcfTaps, 7);

	const GraphicsProfile shipped;
	EXPECT_FLOAT_EQ(read->shadowFirstCascadeDistance, shipped.shadowFirstCascadeDistance);

	ASSERT_TRUE(read->Export(document));
	Delete(read);
	const str text = FileSystem::ReadTextFile(document);
	EXPECT_EQ(text.find("shadowCascadeLambda"), str::npos) << text;
	EXPECT_NE(text.find("shadowFirstCascadeDistance"), str::npos) << text;
}

TEST_F(GraphicsProfileFixture, ImporterOwnsExtensionAndCanMintOne) {
	GraphicsProfileImporter importer;
	EXPECT_EQ(importer.GetAssetType(), StringHash("GraphicsProfile"));
	EXPECT_TRUE(importer.CanCreateAsset());
	EXPECT_EQ(importer.GetCreateDefaultFileName(), "New Graphics Profile.slprofile");

	Asset* minted = importer.CreateAsset();
	ASSERT_NE(minted, nullptr);
	EXPECT_EQ(minted->GetAssetTypeId(), StringHash("GraphicsProfile"));

	const Path document = PathIn("Minted.slprofile");
	ASSERT_TRUE(minted->Export(document));
	Delete(minted);

	Asset* reimported = importer.Import(document);
	ASSERT_NE(reimported, nullptr);
	EXPECT_EQ(static_cast<GraphicsProfile*>(reimported)->msaa, 1);
	Delete(reimported);

	EXPECT_EQ(importer.Import(PathIn("Absent.slprofile")), nullptr);
}

namespace {

	class QualityLevelFixture : public GraphicsProfileFixture {
	protected:
		SettingsPaths _paths;

		void SetUp() override {
			GraphicsProfileFixture::SetUp();
			_paths.project = PathIn("ProjectSettings.ini");
			_paths.userProject = PathIn("PlayerSettings.ini");
			Settings::Initialize(_paths);
		}

		void TearDown() override {
			Settings::Terminate();
			GraphicsProfileFixture::TearDown();
		}

		GraphicsSettings& Declare() {
			GraphicsSettings* settings =
				Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
			SLT_CORE_ASSERT(settings != nullptr);
			return *settings;
		}
	};

} // namespace

TEST_F(QualityLevelFixture, NoProfilesNoLevels) {
	Declare();
	EXPECT_EQ(Graphics::GetQualityLevelCount(), 0u);
	EXPECT_EQ(Graphics::GetQualityLevel(), -1);

	EXPECT_TRUE(Graphics::SetQualityLevel(-1));
	EXPECT_FALSE(Graphics::SetQualityLevel(0));
	EXPECT_EQ(Graphics::GetQualityLevel(), -1);
}

TEST_F(QualityLevelFixture, EmptyEntryIsNotLevel) {
	GraphicsSettings& settings = Declare();
	const Guid first{0x11, 0x22};
	const Guid second{0x33, 0x44};
	settings.qualityProfiles = first.ToString() + ";" + second.ToString() + ";";
	EXPECT_EQ(Graphics::GetQualityLevelCount(), 2u);

	Vector<Guid> ids{GetMemoryResource(MemTag::Graphics)};
	Graphics::GetQualityProfileIds(ids);
	ASSERT_EQ(ids.size(), 2u);
	EXPECT_EQ(ids[0], first);
	EXPECT_EQ(ids[1], second);

	EXPECT_TRUE(Graphics::SetQualityLevel(1));
	EXPECT_EQ(Graphics::GetQualityLevel(), 1);
	EXPECT_FALSE(Graphics::SetQualityLevel(2));
	EXPECT_EQ(Graphics::GetQualityLevel(), 1);
}

TEST_F(QualityLevelFixture, TheChosenLevelSurvivesARelaunch) {
	GraphicsSettings& settings = Declare();
	settings.qualityProfiles = Guid{0x55, 0x66}.ToString() + ";" + Guid{0x77, 0x88}.ToString();
	Settings::NotifyChanged("Graphics", "qualityProfiles");
	Settings::Save();
	ASSERT_TRUE(Graphics::SetQualityLevel(1));

	Settings::Terminate();
	Settings::Initialize(_paths);
	Declare();
	EXPECT_EQ(Graphics::GetQualityLevel(), 1);

	ASSERT_TRUE(Graphics::SetQualityLevel(-1));
	Settings::Save();
	const str document = FileSystem::ReadTextFile(_paths.userProject);
	EXPECT_EQ(document.find("qualityLevel"), str::npos) << document;
}

TEST_F(QualityLevelFixture, ChosenLevelStoredPerPlayer) {
	GraphicsSettings& settings = Declare();
	settings.qualityProfiles = Guid{0x55, 0x66}.ToString() + ";" + Guid{0x77, 0x88}.ToString();
	Settings::NotifyChanged("Graphics", "qualityProfiles");
	Settings::Save();
	const str projectBefore = FileSystem::ReadTextFile(_paths.project);
	ASSERT_NE(projectBefore.find("qualityProfiles"), str::npos);

	ASSERT_TRUE(Graphics::SetQualityLevel(1));
	EXPECT_EQ(FileSystem::ReadTextFile(_paths.project), projectBefore);
	EXPECT_NE(FileSystem::ReadTextFile(_paths.userProject).find("qualityLevel"), str::npos);
}

TEST_F(GraphicsProfileFixture, CategoryClassesMatchRenderer) {
	const TypeInfo* type = GraphicsSettings::GetReflectType();
	ASSERT_NE(type, nullptr);

	auto classOf = [type](std::string_view name) -> MutabilityClass {
		for (u32 index = 0; index < type->fieldCount; ++index) {
			const FieldInfo& field = type->fields[index];
			if (!field.displayName || name != field.displayName) {
				continue;
			}
			MutabilityClass mutability = MutabilityClass::Hot;
			EXPECT_TRUE(Settings::GetFieldMutability(&field, mutability)) << name;
			return mutability;
		}
		ADD_FAILURE() << name << " isn't a GraphicsSettings field";
		return MutabilityClass::Hot;
	};

	EXPECT_EQ(classOf("adapter"), MutabilityClass::Restart);
	EXPECT_EQ(classOf("validation"), MutabilityClass::Restart);
	EXPECT_EQ(classOf("physicalLightUnits"), MutabilityClass::Restart);
	EXPECT_EQ(classOf("vsync"), MutabilityClass::FrameBoundary);
	EXPECT_EQ(classOf("qualityProfiles"), MutabilityClass::FrameBoundary);
	EXPECT_EQ(classOf("qualityLevel"), MutabilityClass::FrameBoundary);
}

namespace {

	class QualityListingFixture : public QualityLevelFixture {
	protected:
		void SetUp() override {
			QualityLevelFixture::SetUp();
			fs::create_directories(fs::path(_root) / "Assets" / "Settings");
			FileSystem::Mount("project", Path(fs::path(_root).generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			QualityLevelFixture::TearDown();
		}

		void WriteProfile(const char* name, i32 msaa) {
			GraphicsProfile profile;
			profile.msaa = msaa;
			ASSERT_TRUE(profile.Export(Path((fs::path(_root) / "Assets" / "Settings" / name).generic_string())));
		}
	};

} // namespace

TEST_F(QualityListingFixture, EmptyListFilledFromProfiles) {
	WriteProfile("1 Low.slprofile", 1);
	WriteProfile("2 Medium.slprofile", 2);
	WriteProfile("3 High.slprofile", 4);
	AssetDatabase::ScanProjectDirectory();
	Declare();

	EXPECT_EQ(SettingsOps::ListProjectQualityProfiles(), 3u);
	ASSERT_EQ(Graphics::GetQualityLevelCount(), 3u);

	Vector<Guid> ids{GetMemoryResource(MemTag::Graphics)};
	Graphics::GetQualityProfileIds(ids);
	ASSERT_EQ(ids.size(), 3u);
	auto msaaOf = [](const Guid& id) {
		Asset* asset = AssetDatabase::LoadAssetByGuid(id);
		return asset ? static_cast<GraphicsProfile*>(asset)->msaa : -1;
	};
	EXPECT_EQ(msaaOf(ids[0]), 1);
	EXPECT_EQ(msaaOf(ids[1]), 2);
	EXPECT_EQ(msaaOf(ids[2]), 4);

	EXPECT_EQ(SettingsOps::ListProjectQualityProfiles(), 0u);
	WriteProfile("4 Ultra.slprofile", 8);
	AssetDatabase::ScanProjectDirectory();
	EXPECT_EQ(SettingsOps::ListProjectQualityProfiles(), 0u);
	EXPECT_EQ(Graphics::GetQualityLevelCount(), 3u);
}

TEST_F(QualityListingFixture, ProjectWithNoProfilesIsLeftAlone) {
	AssetDatabase::ScanProjectDirectory();
	Declare();
	EXPECT_EQ(SettingsOps::ListProjectQualityProfiles(), 0u);
	EXPECT_EQ(Graphics::GetQualityLevelCount(), 0u);
	EXPECT_EQ(Graphics::GetQualityLevel(), -1);
}

namespace {

	struct ProbeAssetRefSettings {
		SLT_REFLECT_VALUE(ProbeAssetRefSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_SETTINGS_CATEGORY("ProbeAssetRefs")
		SLT_REFLECT_FIELD(sheets, SLT_ASSET_REFS)
		SLT_REFLECT_FIELD(label)
		SLT_REFLECT_FIELD(count)
		SLT_END_REFLECT()

		str sheets;
		str label;
		i32 count = 0;
	};

} // namespace

SLT_REGISTER_TYPE(ProbeAssetRefSettings)

namespace {

	class SettingsAssetRootFixture : public QualityLevelFixture {
	protected:
		void SetUp() override {
			QualityLevelFixture::SetUp();
			Settings::Terminate();
			ProbeAssetRefSettings::GetReflectType();
		}

		void OpenProject(const str& document) {
			ASSERT_TRUE(FileSystem::WriteTextFile(_paths.project, document));
			Settings::Initialize(_paths);
		}
	};

} // namespace

TEST_F(SettingsAssetRootFixture, ReaderTakesTaggedFieldsOnly) {
	const Guid sheet{0xabc, 0xdef};
	OpenProject(str("[ProbeAssetRefs]\nsheets = ") + sheet.ToString() + "\nlabel = " +
				Guid{0x1111, 0x2222}.ToString() + "\ncount = 3\n");

	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	EXPECT_EQ(SettingsOps::CollectSettingsAssetRoots(roots), 1u);
	ASSERT_EQ(roots.size(), 1u);
	EXPECT_EQ(roots[0], sheet);
}

TEST_F(SettingsAssetRootFixture, HeadlessBuildReadsProjectWithoutRendererRunning) {
	const Guid named{0x7e, 0x11};
	OpenProject(str("[Graphics]\nqualityProfiles = ") + named.ToString() + "\n");

	Vector<SettingsCategoryInfo> declared{GetMemoryResource(MemTag::Editor)};
	Settings::GetCategories(declared);
	for (const SettingsCategoryInfo& info : declared) {
		ASSERT_NE(info.path, "Graphics");
	}

	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	EXPECT_EQ(SettingsOps::CollectSettingsAssetRoots(roots), 1u);
	ASSERT_EQ(roots.size(), 1u);
	EXPECT_EQ(roots[0], named);
}

TEST_F(SettingsAssetRootFixture, ReaderKeepsCallersRootsAndOrder) {
	const Guid named{0x99, 0x88};
	OpenProject(str("[ProbeAssetRefs]\nsheets = ") + named.ToString() + "\n");

	const Guid firstScene{0xff, 0x01};
	const Guid secondScene{0x02, 0x03};
	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	roots.push_back(firstScene);
	roots.push_back(secondScene);

	EXPECT_EQ(SettingsOps::CollectSettingsAssetRoots(roots), 1u);
	ASSERT_EQ(roots.size(), 3u);
	EXPECT_EQ(roots[0], firstScene);
	EXPECT_EQ(roots[1], secondScene);
	EXPECT_EQ(roots[2], named);

	EXPECT_EQ(SettingsOps::CollectSettingsAssetRoots(roots), 0u);
	EXPECT_EQ(roots.size(), 3u);
}

TEST_F(SettingsAssetRootFixture, TheReaderSurvivesAHandEditedList) {
	const Guid good{0x4a, 0x5b};
	OpenProject(str("[ProbeAssetRefs]\nsheets = ;;") + good.ToString() + ";not-a-guid;\n");

	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	EXPECT_EQ(SettingsOps::CollectSettingsAssetRoots(roots), 1u);
	ASSERT_EQ(roots.size(), 1u);
	EXPECT_EQ(roots[0], good);
}

TEST_F(SettingsAssetRootFixture, AProjectThatNamesNothingSeedsNothing) {
	OpenProject(str("[Graphics]\nadapter = 0\n"));

	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	EXPECT_EQ(SettingsOps::CollectSettingsAssetRoots(roots), 0u);
	EXPECT_TRUE(roots.empty());
}

TEST_F(SettingsAssetRootFixture, TheProjectsQualityProfilesAreCookRoots) {
	const Guid low{0x1a, 0x2b};
	const Guid high{0x3c, 0x4d};
	OpenProject(str("[Graphics]\nqualityProfiles = ") + low.ToString() + ";" + high.ToString() +
				"\nqualityLevel = 0\n");

	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	ASSERT_EQ(SettingsOps::CollectSettingsAssetRoots(roots), 2u);
	EXPECT_NE(std::find(roots.begin(), roots.end(), low), roots.end());
	EXPECT_NE(std::find(roots.begin(), roots.end(), high), roots.end());
}

TEST_F(SettingsAssetRootFixture, CategoryPathMatchesType) {
	OpenProject(str("[Graphics]\nadapter = 0\n"));
	Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);

	Vector<SettingsCategoryInfo> declared{GetMemoryResource(MemTag::Editor)};
	Settings::GetCategories(declared);
	ASSERT_FALSE(declared.empty());

	u32 checked = 0;
	for (const SettingsCategoryInfo& info : declared) {
		const AttributeInfo* attribute = FindTypeAttribute(info.type, StringHash("SettingsCategory"));
		if (!attribute) {
			continue;
		}
		ASSERT_TRUE(attribute->value.Is<str>()) << info.path;
		EXPECT_EQ(attribute->value.As<str>(), str(info.path));
		++checked;
	}
	EXPECT_GT(checked, 0u);
}
