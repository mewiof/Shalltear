#include "TestsPCH.h"

#include "Shalltear/Editor/BuildProfile.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace Shalltear;

namespace {

	namespace fs = std::filesystem;

	class BuildProfileFixture : public ::testing::Test {
	protected:
		fs::path _root;
		fs::path _other;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			fs::path base = fs::temp_directory_path() /
							("shalltear_profile_" + std::to_string(stamp) + "_" + std::to_string(unique));
			_root = base / "MyGame";
			_other = base / "TheirGame";
			MakeProject(_root);
			MakeProject(_other);
		}

		void TearDown() override {
			std::error_code ec;
			fs::remove_all(_root.parent_path(), ec);
		}

		static void MakeProject(const fs::path& root) {
			fs::create_directories(root / "Assets" / "Scenes");
			std::ofstream ini(root / "Project.ini", std::ios::binary);
			ini << "[Project]\nname = " << root.filename().generic_string() << "\n";
		}

		str Root() const { return _root.generic_string(); }
		str Other() const { return _other.generic_string(); }

		void WriteRaw(const fs::path& projectRoot, const str& name, const str& text) const {
			fs::create_directories(projectRoot / "BuildProfiles");
			std::ofstream file(projectRoot / "BuildProfiles" / (name + ".buildprofile"), std::ios::binary);
			file << text;
		}

		static str ReadRaw(const str& path) {
			std::ifstream file(path, std::ios::binary);
			return str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		}

		void WriteScene(const fs::path& projectRoot, const str& relative) const {
			fs::path full = projectRoot / relative;
			fs::create_directories(full.parent_path());
			std::ofstream file(full, std::ios::binary);
			file << "{}\n";
		}

		void WriteRoster(const fs::path& projectRoot, const Vector<str>& names) const {
			std::ofstream ini(projectRoot / "Project.ini", std::ios::binary);
			ini << "[Project]\nname = MyGame\n\n[Build]\n";
			for (size_t i = 0; i < names.size(); ++i) {
				ini << "profile." << i << " = " << names[i] << "\n";
			}
		}
	};

	Vector<str> List(std::initializer_list<const char*> values) {
		Vector<str> out;
		for (const char* value : values) {
			out.push_back(value);
		}
		return out;
	}

	Vector<BuildSettingDesc> CoreSchema() {
		Vector<BuildSettingDesc> schema;
		BuildProfileOps::GetCoreSettings(schema);
		return schema;
	}

	const BuildSettingDesc* FindDesc(const Vector<BuildSettingDesc>& schema, std::string_view key) {
		for (const BuildSettingDesc& desc : schema) {
			if (desc.key == key) {
				return &desc;
			}
		}
		return nullptr;
	}

	bool HasIssue(const Vector<BuildIssue>& issues, std::string_view fragment) {
		for (const BuildIssue& issue : issues) {
			if (issue.message.find(fragment) != str::npos) {
				return true;
			}
		}
		return false;
	}

	bool HasIssueOn(const Vector<BuildIssue>& issues, std::string_view key, BuildIssueSeverity severity) {
		for (const BuildIssue& issue : issues) {
			if (issue.key == key && issue.severity == severity) {
				return true;
			}
		}
		return false;
	}

	void SetEnv(const char* name, const char* value) {
		::_putenv_s(name, value);
	}

} // namespace

TEST(BuildSchemaTest, CoreKeysCarryTheirMergePolicy) {
	Vector<BuildSettingDesc> schema = CoreSchema();

	const BuildSettingDesc* defines = FindDesc(schema, "defines");
	ASSERT_NE(defines, nullptr);
	EXPECT_EQ(defines->merge, BuildMergePolicy::Union);
	EXPECT_EQ(defines->type, BuildSettingType::StringList);

	const BuildSettingDesc* scenes = FindDesc(schema, "scenes");
	ASSERT_NE(scenes, nullptr);
	EXPECT_EQ(scenes->merge, BuildMergePolicy::Replace);
	EXPECT_EQ(scenes->type, BuildSettingType::SceneList);

	for (const char* key : {"scenes", "defines", "configuration", "outputPath", "executableName",
							"version", "buildNumber", "buildCommand", "preBuildCommand", "postBuildCommand"}) {
		EXPECT_NE(FindDesc(schema, key), nullptr) << key;
	}

	const BuildSettingDesc* configuration = FindDesc(schema, "configuration");
	ASSERT_NE(configuration, nullptr);
	EXPECT_EQ(configuration->type, BuildSettingType::Enum);
	EXPECT_EQ(configuration->enumValues.size(), 2u);
	EXPECT_EQ(configuration->defaultValue, "Release");

	const BuildSettingDesc* compression = FindDesc(schema, "compressionMethod");
	ASSERT_NE(compression, nullptr);
	EXPECT_EQ(compression->type, BuildSettingType::Enum);
	EXPECT_EQ(compression->merge, BuildMergePolicy::Replace);
	EXPECT_EQ(compression->defaultValue, "Default");
	ASSERT_EQ(compression->enumValues.size(), 3u);
	EXPECT_EQ(compression->enumValues[0], "Default");
	EXPECT_EQ(compression->enumValues[1], "LZ4");
	EXPECT_EQ(compression->enumValues[2], "LZ4HC");
}

TEST(BuildProfileNameTest, RejectsWhatTheFilesystemCannotHold) {
	using BuildProfileOps::IsValidProfileName;

	EXPECT_TRUE(IsValidProfileName("Default"));
	EXPECT_TRUE(IsValidProfileName("Windows Server"));
	EXPECT_TRUE(IsValidProfileName("Windows (Steam Release)"));
	EXPECT_TRUE(IsValidProfileName("v1.0"));

	EXPECT_FALSE(IsValidProfileName(""));
	for (const char* bad : {"a/b", "a\\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b"}) {
		EXPECT_FALSE(IsValidProfileName(bad)) << bad;
	}

	EXPECT_FALSE(IsValidProfileName("trailing."));
	EXPECT_FALSE(IsValidProfileName("trailing "));
	EXPECT_FALSE(IsValidProfileName(".leading"));
	EXPECT_FALSE(IsValidProfileName(" leading"));
}

TEST(BuildProfileNameTest, RejectsTheReservedDeviceNames) {
	using BuildProfileOps::IsValidProfileName;

	for (const char* device : {"CON", "con", "PRN", "AUX", "nul", "COM1", "com9", "LPT1", "lpt9"}) {
		EXPECT_FALSE(IsValidProfileName(device)) << device;
	}

	for (const char* fine : {"Console", "COM", "COM10", "Auxiliary", "nullable"}) {
		EXPECT_TRUE(IsValidProfileName(fine)) << fine;
	}
}

TEST_F(BuildProfileFixture, RoundTripsByteForByte) {
	BuildProfile profile;
	profile.name = "Demo";
	profile.inherits = "Windows";
	profile.target = "WindowsServer";
	profile.SetList("defines", List({"DEMO"}));
	profile.SetString("executableName", "MyGameDemo");

	str path = BuildProfileOps::GetProfilePath(Root(), "Demo");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(path, profile));

	str expected =
		"{\n"
		"\t\"formatVersion\": 1,\n"
		"\t\"inherits\": \"Windows\",\n"
		"\t\"target\": \"WindowsServer\",\n"
		"\t\"settings\": {\n"
		"\t\t\"defines\": [\n"
		"\t\t\t\"DEMO\"\n"
		"\t\t],\n"
		"\t\t\"executableName\": \"MyGameDemo\"\n"
		"\t}\n"
		"}\n";
	EXPECT_EQ(ReadRaw(path), expected);

	BuildProfile reloaded;
	ASSERT_TRUE(BuildProfileOps::LoadProfile(path, reloaded));
	ASSERT_TRUE(BuildProfileOps::SaveProfile(path, reloaded));
	EXPECT_EQ(ReadRaw(path), expected);

	EXPECT_EQ(reloaded.name, "Demo");
	EXPECT_EQ(expected.find("\"name\""), str::npos);
}

TEST_F(BuildProfileFixture, AbsenceIsTheInheritSignal) {
	BuildProfile profile;
	profile.SetString("version", "1.0");

	EXPECT_TRUE(profile.Has("version"));
	EXPECT_FALSE(profile.Has("defines"));
	EXPECT_EQ(profile.SettingCount(), 1u);

	str path = BuildProfileOps::GetProfilePath(Root(), "Sparse");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(path, profile));
	str text = ReadRaw(path);
	EXPECT_NE(text.find("\"version\""), str::npos);
	EXPECT_EQ(text.find("\"defines\""), str::npos);
	EXPECT_EQ(text.find("\"inherits\""), str::npos);

	EXPECT_TRUE(profile.Remove("version"));
	EXPECT_FALSE(profile.Remove("version"));
	EXPECT_EQ(profile.SettingCount(), 0u);
}

TEST_F(BuildProfileFixture, SettingsKeepFileOrderAcrossAnEdit) {
	WriteRaw(_root, "Ordered",
			 "{\n\t\"formatVersion\": 1,\n\t\"settings\": {\n"
			 "\t\t\"version\": \"1.0\",\n\t\t\"executableName\": \"Game\"\n\t}\n}\n");

	BuildProfile profile;
	ASSERT_TRUE(BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(Root(), "Ordered"), profile));
	profile.SetString("version", "2.0");

	ASSERT_EQ(profile.SettingCount(), 2u);
	EXPECT_EQ(profile.SettingKeyAt(0), "version");
	EXPECT_EQ(profile.SettingKeyAt(1), "executableName");
}

TEST_F(BuildProfileFixture, UnknownKeysSurviveALoadSaveCycle) {
	WriteRaw(_root, "Android",
			 "{\n"
			 "\t\"formatVersion\": 1,\n"
			 "\t\"target\": \"Android\",\n"
			 "\t\"settings\": {\n"
			 "\t\t\"defines\": [\n\t\t\t\"MOBILE\"\n\t\t],\n"
			 "\t\t\"keystorePath\": \"keys/release.jks\",\n"
			 "\t\t\"versionCode\": 48,\n"
			 "\t\t\"signing\": {\n\t\t\t\"alias\": \"upload\",\n\t\t\t\"v2\": true\n\t\t}\n"
			 "\t},\n"
			 "\t\"toolExtras\": {\n\t\t\"deploy\": {\n\t\t\t\"device\": \"pixel\"\n\t\t}\n\t}\n"
			 "}\n");

	str path = BuildProfileOps::GetProfilePath(Root(), "Android");
	BuildProfile profile;
	ASSERT_TRUE(BuildProfileOps::LoadProfile(path, profile));

	EXPECT_TRUE(profile.Has("keystorePath"));
	EXPECT_TRUE(profile.Has("versionCode"));
	EXPECT_TRUE(profile.Has("signing"));
	EXPECT_EQ(profile.GetString("keystorePath"), "keys/release.jks");

	profile.SetString("executableName", "MyGame");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(path, profile));

	str saved = ReadRaw(path);
	EXPECT_NE(saved.find("\"keystorePath\": \"keys/release.jks\""), str::npos);
	EXPECT_NE(saved.find("\"versionCode\": 48"), str::npos);
	EXPECT_NE(saved.find("\"alias\": \"upload\""), str::npos);
	EXPECT_NE(saved.find("\"v2\": true"), str::npos);
	EXPECT_NE(saved.find("\"device\": \"pixel\""), str::npos);
	EXPECT_NE(saved.find("\"executableName\": \"MyGame\""), str::npos);
	EXPECT_NE(saved.find("\"target\": \"Android\""), str::npos);

	BuildProfile reloaded;
	ASSERT_TRUE(BuildProfileOps::LoadProfile(path, reloaded));
	ASSERT_TRUE(BuildProfileOps::SaveProfile(path, reloaded));
	EXPECT_EQ(ReadRaw(path), saved);
}

TEST_F(BuildProfileFixture, UnknownKeysAreIgnoredByTheFold) {
	WriteRaw(_root, "Android",
			 "{\n\t\"formatVersion\": 1,\n\t\"settings\": {\n\t\t\"keystorePath\": \"keys/release.jks\"\n\t}\n}\n");

	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Android", CoreSchema(), resolved);

	EXPECT_EQ(resolved.Find("keystorePath"), nullptr);
	EXPECT_EQ(resolved.settings.size(), CoreSchema().size());
}

TEST_F(BuildProfileFixture, ResolvesAGrandparentChainWithProvenance) {
	BuildProfile base;
	base.SetList("scenes", List({"Assets/Scenes/Main.scene", "Assets/Scenes/Boss.scene"}));
	base.SetList("defines", List({"TELEMETRY"}));
	base.SetString("executableName", "MyGame");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Base"), base));

	BuildProfile windows;
	windows.inherits = "Base";
	windows.target = "Windows";
	windows.SetList("defines", List({"STEAM"}));
	windows.SetString("version", "1.0");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Windows"), windows));

	BuildProfile demo;
	demo.inherits = "Windows";
	demo.SetList("scenes", List({"Assets/Scenes/Main.scene"}));
	demo.SetList("defines", List({"DEMO", "TELEMETRY"}));
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Demo"), demo));

	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Demo", CoreSchema(), resolved);

	ASSERT_EQ(resolved.chain.size(), 3u);
	EXPECT_EQ(resolved.chain[0], "Base");
	EXPECT_EQ(resolved.chain[1], "Windows");
	EXPECT_EQ(resolved.chain[2], "Demo");

	EXPECT_EQ(resolved.target, "Windows");
	EXPECT_EQ(resolved.targetSource, "Windows");

	const ResolvedSetting* defines = resolved.Find("defines");
	ASSERT_NE(defines, nullptr);
	ASSERT_EQ(defines->items.size(), 3u);
	EXPECT_EQ(defines->items[0], "TELEMETRY");
	EXPECT_EQ(defines->items[1], "STEAM");
	EXPECT_EQ(defines->items[2], "DEMO");

	const ResolvedSetting* scenes = resolved.Find("scenes");
	ASSERT_NE(scenes, nullptr);
	ASSERT_EQ(scenes->items.size(), 1u);
	EXPECT_EQ(scenes->items[0], "Assets/Scenes/Main.scene");

	EXPECT_EQ(scenes->source, "Demo");
	EXPECT_EQ(resolved.Find("version")->source, "Windows");
	EXPECT_EQ(resolved.Find("executableName")->source, "Base");
	EXPECT_EQ(resolved.Find("buildCommand")->source, "");
	EXPECT_EQ(resolved.Find("configuration")->value, "Release");
}

TEST_F(BuildProfileFixture, CycleIsReported) {
	BuildProfile a;
	a.inherits = "B";
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "A"), a));
	BuildProfile b;
	b.inherits = "A";
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "B"), b));

	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "A", CoreSchema(), resolved);

	EXPECT_TRUE(HasIssue(resolved.issues, "cycle"));
	EXPECT_TRUE(HasIssue(resolved.issues, "'B'"));
	EXPECT_TRUE(HasIssue(resolved.issues, "'A'"));
	EXPECT_EQ(resolved.chain.size(), 2u);
}

TEST_F(BuildProfileFixture, MissingParentFallsBackToProjectRoot) {
	BuildProfile root;
	root.SetList("defines", List({"BASE"}));
	root.SetString("executableName", "TheirGame");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Default"), root));

	BuildProfile imported;
	imported.inherits = "SomebodyElsesWindows";
	imported.SetList("defines", List({"DEMO"}));
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Demo"), imported));
	WriteRoster(_root, List({"Default", "Demo"}));

	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Demo", CoreSchema(), resolved);

	EXPECT_TRUE(HasIssue(resolved.issues, "SomebodyElsesWindows"));
	ASSERT_EQ(resolved.chain.size(), 2u);
	EXPECT_EQ(resolved.chain[0], "Default");

	const ResolvedSetting* defines = resolved.Find("defines");
	ASSERT_EQ(defines->items.size(), 2u);
	EXPECT_EQ(defines->items[0], "BASE");
	EXPECT_EQ(defines->items[1], "DEMO");
	EXPECT_EQ(resolved.Find("executableName")->value, "TheirGame");
}

TEST_F(BuildProfileFixture, MissingProfileIsError) {
	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Nothing", CoreSchema(), resolved);

	EXPECT_TRUE(HasIssue(resolved.issues, "no profile named"));
	EXPECT_TRUE(resolved.chain.empty());
}

TEST_F(BuildProfileFixture, ExpandsEnvironmentVariablesAndKeepsRawForm) {
	SetEnv("SHALLTEAR_TEST_RUN_NUMBER", "48");

	BuildProfile profile;
	profile.SetString("version", "1.0.${ENV:SHALLTEAR_TEST_RUN_NUMBER}");
	profile.SetList("defines", List({"RUN_${ENV:SHALLTEAR_TEST_RUN_NUMBER}"}));
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "CI"), profile));

	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "CI", CoreSchema(), resolved);
	EXPECT_TRUE(resolved.issues.empty());

	const ResolvedSetting* version = resolved.Find("version");
	EXPECT_EQ(version->value, "1.0.48");
	EXPECT_EQ(version->rawValue, "1.0.${ENV:SHALLTEAR_TEST_RUN_NUMBER}");

	const ResolvedSetting* defines = resolved.Find("defines");
	ASSERT_EQ(defines->items.size(), 1u);
	EXPECT_EQ(defines->items[0], "RUN_48");
	EXPECT_EQ(defines->rawItems[0], "RUN_${ENV:SHALLTEAR_TEST_RUN_NUMBER}");

	SetEnv("SHALLTEAR_TEST_RUN_NUMBER", "");
}

TEST_F(BuildProfileFixture, UndefinedVariableIsIssue) {
	SetEnv("SHALLTEAR_TEST_ABSENT", "");

	BuildProfile profile;
	profile.SetString("version", "1.0.${ENV:SHALLTEAR_TEST_ABSENT}");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "CI"), profile));

	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "CI", CoreSchema(), resolved);

	EXPECT_TRUE(HasIssueOn(resolved.issues, "version", BuildIssueSeverity::Error));
	EXPECT_TRUE(HasIssue(resolved.issues, "SHALLTEAR_TEST_ABSENT"));
	EXPECT_EQ(resolved.Find("version")->value, "1.0.${ENV:SHALLTEAR_TEST_ABSENT}");
}

TEST_F(BuildProfileFixture, DoubleDollarIsTheLiteralEscape) {
	BuildProfile profile;
	profile.SetString("postBuildCommand", "echo $${ENV:NOT_A_VARIABLE} > log.txt");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Literal"), profile));

	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Literal", CoreSchema(), resolved);

	EXPECT_TRUE(resolved.issues.empty());
	EXPECT_EQ(resolved.Find("postBuildCommand")->value, "echo ${ENV:NOT_A_VARIABLE} > log.txt");
}

TEST_F(BuildProfileFixture, AutoIsRefusedOutsideBuildNumber) {
	BuildProfile profile;
	profile.SetString("buildNumber", "${AUTO}");
	profile.SetString("version", "1.0.${AUTO}");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Counted"), profile));

	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Counted", CoreSchema(), resolved);

	EXPECT_TRUE(HasIssueOn(resolved.issues, "version", BuildIssueSeverity::Error));
	EXPECT_FALSE(HasIssueOn(resolved.issues, "buildNumber", BuildIssueSeverity::Error));
	EXPECT_EQ(resolved.Find("buildNumber")->value, "${AUTO}");
}

TEST_F(BuildProfileFixture, ReportsWhatIsKnowableBeforeAnythingCompiles) {
	WriteScene(_root, "Assets/Scenes/Main.scene");

	BuildProfile profile;
	profile.SetList("scenes", List({"Assets/Scenes/Main.scene", "Assets/Scenes/Gone.scene"}));
	profile.SetString("configuration", "Retail");
	profile.SetString("outputPath", "Assets/Builds");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Broken"), profile));

	Vector<BuildSettingDesc> schema = CoreSchema();
	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Broken", schema, resolved);
	Vector<BuildIssue> issues;
	BuildProfileOps::ValidateProfile(Root(), resolved, schema, issues);

	EXPECT_TRUE(HasIssue(issues, "Gone.scene"));
	EXPECT_FALSE(HasIssue(issues, "Main.scene"));
	EXPECT_TRUE(HasIssue(issues, "'Retail' is not"));
	EXPECT_TRUE(HasIssue(issues, "content root"));
}

TEST_F(BuildProfileFixture, AbsoluteOutputPathIsWarningNotError) {
	WriteScene(_root, "Assets/Scenes/Main.scene");

	BuildProfile profile;
	profile.SetList("scenes", List({"Assets/Scenes/Main.scene"}));
	profile.SetString("outputPath", "//buildserver/artifacts");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Farm"), profile));

	Vector<BuildSettingDesc> schema = CoreSchema();
	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Farm", schema, resolved);
	Vector<BuildIssue> issues;
	BuildProfileOps::ValidateProfile(Root(), resolved, schema, issues);

	ASSERT_EQ(issues.size(), 1u);
	EXPECT_EQ(issues[0].severity, BuildIssueSeverity::Warning);
	EXPECT_EQ(issues[0].key, "outputPath");
}

TEST_F(BuildProfileFixture, ACleanProfileReportsNothing) {
	WriteScene(_root, "Assets/Scenes/Main.scene");

	BuildProfile profile;
	profile.SetList("scenes", List({"Assets/Scenes/Main.scene"}));
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Default"), profile));

	Vector<BuildSettingDesc> schema = CoreSchema();
	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Default", schema, resolved);
	Vector<BuildIssue> issues;
	BuildProfileOps::ValidateProfile(Root(), resolved, schema, issues);

	EXPECT_TRUE(issues.empty());
}

TEST_F(BuildProfileFixture, TheRosterHealsAgainstDisk) {
	BuildProfile empty;
	for (const char* name : {"Default", "Windows", "Dropped"}) {
		ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), name), empty));
	}
	WriteRoster(_root, List({"Default", "Deleted", "Windows"}));

	Vector<str> names;
	BuildProfileOps::ListProfiles(Root(), names);

	ASSERT_EQ(names.size(), 3u);
	EXPECT_EQ(names[0], "Default");
	EXPECT_EQ(names[1], "Windows");
	EXPECT_EQ(names[2], "Dropped");

	str ini = ReadRaw(Root() + "/Project.ini");
	EXPECT_NE(ini.find("Deleted"), str::npos);

	ASSERT_TRUE(BuildProfileOps::WriteProfileRoster(Root(), names));
	Vector<str> reread;
	BuildProfileOps::ListProfiles(Root(), reread);
	EXPECT_EQ(reread, names);
	EXPECT_EQ(ReadRaw(Root() + "/Project.ini").find("Deleted"), str::npos);
}

TEST_F(BuildProfileFixture, TheRosterShrinksWithoutLeavingATail) {
	BuildProfile empty;
	for (const char* name : {"A", "B", "C"}) {
		ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), name), empty));
	}
	ASSERT_TRUE(BuildProfileOps::WriteProfileRoster(Root(), List({"C", "B", "A"})));

	ASSERT_TRUE(BuildProfileOps::WriteProfileRoster(Root(), List({"B"})));
	str ini = ReadRaw(Root() + "/Project.ini");
	EXPECT_EQ(ini.find("profile.1"), str::npos);
	EXPECT_NE(ini.find("[Project]"), str::npos);

	Vector<str> names;
	BuildProfileOps::ListProfiles(Root(), names);
	ASSERT_EQ(names.size(), 3u);
	EXPECT_EQ(names[0], "B");
}

TEST_F(BuildProfileFixture, RenamingRewritesEveryChildsInherits) {
	BuildProfile base;
	base.SetList("defines", List({"BASE"}));
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Base"), base));

	for (const char* child : {"Demo", "Retail"}) {
		BuildProfile profile;
		profile.inherits = "Base";
		ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), child), profile));
	}
	BuildProfile unrelated;
	unrelated.inherits = "Demo";
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Unrelated"), unrelated));
	WriteRoster(_root, List({"Base", "Demo", "Retail", "Unrelated"}));

	ASSERT_TRUE(BuildProfileOps::RenameProfile(Root(), "Base", "Core"));

	EXPECT_FALSE(fs::exists(BuildProfileOps::GetProfilePath(Root(), "Base")));
	EXPECT_TRUE(fs::exists(BuildProfileOps::GetProfilePath(Root(), "Core")));

	for (const char* child : {"Demo", "Retail"}) {
		BuildProfile profile;
		ASSERT_TRUE(BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(Root(), child), profile));
		EXPECT_EQ(profile.inherits, "Core") << child;
	}
	BuildProfile untouched;
	ASSERT_TRUE(BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(Root(), "Unrelated"), untouched));
	EXPECT_EQ(untouched.inherits, "Demo");

	Vector<str> names;
	BuildProfileOps::ListProfiles(Root(), names);
	ASSERT_EQ(names.size(), 4u);
	EXPECT_EQ(names[0], "Core");

	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Demo", CoreSchema(), resolved);
	EXPECT_TRUE(resolved.issues.empty());
	EXPECT_EQ(resolved.Find("defines")->items.size(), 1u);
}

TEST_F(BuildProfileFixture, RenameRefusesAnUnusableOrTakenName) {
	BuildProfile empty;
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Demo"), empty));
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Retail"), empty));

	EXPECT_FALSE(BuildProfileOps::RenameProfile(Root(), "Demo", "a/b"));
	EXPECT_FALSE(BuildProfileOps::RenameProfile(Root(), "Demo", "NUL"));
	EXPECT_FALSE(BuildProfileOps::RenameProfile(Root(), "Demo", "retail"));
	EXPECT_FALSE(BuildProfileOps::RenameProfile(Root(), "NotHere", "Fine"));
	EXPECT_TRUE(fs::exists(BuildProfileOps::GetProfilePath(Root(), "Demo")));
}

TEST_F(BuildProfileFixture, ExportImportIdempotentCopy) {
	BuildProfile demo;
	demo.inherits = "Windows";
	demo.SetList("defines", List({"DEMO"}));
	demo.SetString("outputPath", "Builds/Demo");
	str source = BuildProfileOps::GetProfilePath(Root(), "Demo");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(source, demo));
	str original = ReadRaw(source);

	str exported = (_root.parent_path() / "Demo.buildprofile").generic_string();
	ASSERT_TRUE(BuildProfileOps::ExportProfile(Root(), "Demo", exported));
	EXPECT_EQ(ReadRaw(exported), original);

	str imported;
	Vector<BuildIssue> issues;
	ASSERT_TRUE(BuildProfileOps::ImportProfile(Other(), exported, imported, issues));
	EXPECT_EQ(imported, "Demo");
	EXPECT_EQ(ReadRaw(BuildProfileOps::GetProfilePath(Other(), "Demo")), original);
	EXPECT_TRUE(HasIssue(issues, "Windows"));

	Vector<str> names;
	BuildProfileOps::ListProfiles(Other(), names);
	ASSERT_EQ(names.size(), 1u);
	EXPECT_EQ(names[0], "Demo");

	str reExported = (_root.parent_path() / "Demo.again.buildprofile").generic_string();
	ASSERT_TRUE(BuildProfileOps::ExportProfile(Other(), "Demo", reExported));
	EXPECT_EQ(ReadRaw(reExported), original);
}

TEST_F(BuildProfileFixture, ImportFlagsPathsRefusesDuplicates) {
	BuildProfile mira;
	mira.SetString("outputPath", "C:/Users/mira/Desktop/Builds");
	str exported = (_root.parent_path() / "Festival.buildprofile").generic_string();
	ASSERT_TRUE(BuildProfileOps::SaveProfile(exported, mira));

	str imported;
	Vector<BuildIssue> issues;
	ASSERT_TRUE(BuildProfileOps::ImportProfile(Root(), exported, imported, issues));
	EXPECT_TRUE(HasIssueOn(issues, "outputPath", BuildIssueSeverity::Warning));

	str clashing = (_root.parent_path() / "festival.buildprofile").generic_string();
	ASSERT_TRUE(BuildProfileOps::SaveProfile(clashing, mira));
	str second;
	Vector<BuildIssue> refusal;
	EXPECT_FALSE(BuildProfileOps::ImportProfile(Root(), clashing, second, refusal));
	EXPECT_TRUE(HasIssue(refusal, "already has a profile"));

	Vector<str> names;
	BuildProfileOps::ListProfiles(Root(), names);
	EXPECT_EQ(names.size(), 1u);
}

TEST_F(BuildProfileFixture, ImportRefusesInvalidStem) {
	str device = (_root.parent_path() / "NUL.buildprofile").generic_string();
	{
		std::ofstream file(device, std::ios::binary);
		file << "{}\n";
	}

	str imported;
	Vector<BuildIssue> issues;
	EXPECT_FALSE(BuildProfileOps::ImportProfile(Root(), device, imported, issues));
	EXPECT_FALSE(issues.empty());
}

TEST_F(BuildProfileFixture, CreateSeedsRootAndAppends) {
	WriteScene(_root, "Assets/Scenes/Main.scene");
	WriteScene(_root, "Assets/Scenes/Boss.scene");

	ASSERT_TRUE(BuildProfileOps::CreateProfile(Root(), "Default"));

	BuildProfile written;
	ASSERT_TRUE(BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(Root(), "Default"), written));
	EXPECT_TRUE(written.inherits.empty());
	Vector<str> scenes;
	ASSERT_TRUE(written.GetList("scenes", scenes));
	ASSERT_EQ(scenes.size(), 2u);
	EXPECT_EQ(scenes[0], "Assets/Scenes/Boss.scene");
	EXPECT_EQ(scenes[1], "Assets/Scenes/Main.scene");

	Vector<str> names;
	BuildProfileOps::ListProfiles(Root(), names);
	ASSERT_EQ(names.size(), 1u);
	EXPECT_EQ(names[0], "Default");

	ASSERT_TRUE(BuildProfileOps::CreateProfile(Root(), "Demo", "Default"));
	BuildProfile demo;
	ASSERT_TRUE(BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(Root(), "Demo"), demo));
	EXPECT_EQ(demo.inherits, "Default");
	EXPECT_FALSE(demo.Has("scenes"));

	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Demo", CoreSchema(), resolved);
	const ResolvedSetting* inherited = resolved.Find("scenes");
	ASSERT_NE(inherited, nullptr);
	EXPECT_EQ(inherited->items.size(), 2u);
	EXPECT_EQ(inherited->source, "Default");
}

TEST_F(BuildProfileFixture, CreateProfileRefusesBadOrTakenName) {
	ASSERT_TRUE(BuildProfileOps::CreateProfile(Root(), "Default"));
	EXPECT_FALSE(BuildProfileOps::CreateProfile(Root(), "Default"));
	EXPECT_FALSE(BuildProfileOps::CreateProfile(Root(), "default"));
	EXPECT_FALSE(BuildProfileOps::CreateProfile(Root(), "NUL"));
	EXPECT_FALSE(BuildProfileOps::CreateProfile(Root(), "bad/name"));

	Vector<str> names;
	BuildProfileOps::ListProfiles(Root(), names);
	EXPECT_EQ(names.size(), 1u);
}

TEST_F(BuildProfileFixture, BuildAllSkipsExcluded) {
	BuildProfile root;
	root.SetString("includeInBuildAll", "false");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Base"), root));
	BuildProfile quiet;
	quiet.inherits = "Base";
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Quiet"), quiet));
	BuildProfile loud;
	loud.inherits = "Base";
	loud.SetString("includeInBuildAll", "true");
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Loud"), loud));
	BuildProfile plain;
	ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), "Plain"), plain));

	Vector<str> included;
	BuildProfileOps::ListBuildAllProfiles(Root(), included);
	ASSERT_EQ(included.size(), 2u);
	EXPECT_TRUE(std::find(included.begin(), included.end(), "Loud") != included.end());
	EXPECT_TRUE(std::find(included.begin(), included.end(), "Plain") != included.end());
	EXPECT_TRUE(std::find(included.begin(), included.end(), "Base") == included.end());
	EXPECT_TRUE(std::find(included.begin(), included.end(), "Quiet") == included.end());

	WriteRoster(_root, List({"Plain", "Loud"}));
	BuildProfileOps::ListBuildAllProfiles(Root(), included);
	ASSERT_EQ(included.size(), 2u);
	EXPECT_EQ(included[0], "Plain");
	EXPECT_EQ(included[1], "Loud");
}

TEST_F(BuildProfileFixture, SceneInventoryIsProjectRelative) {
	WriteScene(_root, "Assets/Scenes/Main.scene");
	WriteScene(_root, "Assets/Levels/Deep/Boss.scene");
	WriteScene(_root, "Assets/NotAScene.txt");
	WriteScene(_root, "Builds/Default/Assets/Stale.scene");

	Vector<str> scenes;
	BuildProfileOps::GetProjectScenes(Root(), scenes);
	ASSERT_EQ(scenes.size(), 2u);
	EXPECT_EQ(scenes[0], "Assets/Levels/Deep/Boss.scene");
	EXPECT_EQ(scenes[1], "Assets/Scenes/Main.scene");
}
