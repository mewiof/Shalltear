#include "TestsPCH.h"

#include "UIStyleVocabulary.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/Style.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	struct Parsed {
		StyleSheetAsset sheet;
		str error;
		bool ok = false;

		explicit Parsed(std::string_view text) { ok = sheet.Parse(text, error); }
	};

	void ExpectRefused(std::string_view text, std::string_view namedInMessage) {
		StyleSheetAsset sheet;
		str error;
		EXPECT_FALSE(sheet.Parse(text, error)) << "accepted: " << text;
		EXPECT_NE(error.find(str(namedInMessage)), str::npos) << namedInMessage << "': " << error;
		EXPECT_TRUE(sheet.source.empty()) << error;
	}

	std::filesystem::path RepoDir(const char* relative) {
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

	class StyleSheetFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_uislss_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");

			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void WriteSheet(const str& virtualPath, std::string_view text) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(resolved, str(text));
		}
	};

} // anonymous namespace

TEST(UIStyleSheetTest, SheetKeepsVerbatimText) {
	constexpr const char* AUTHORED =
		"/* the header comment a serializer would eat */\n"
		"\n"
		".row {\n"
		"\tbackground: var(--slt-accent);   /* and the trailing one */\n"
		"\theight:    24px;\n"
		"}\n"
		"\n"
		"#title:hover { color: #ff0000; }\n";

	Parsed parsed(AUTHORED);
	ASSERT_TRUE(parsed.ok) << parsed.error;
	EXPECT_EQ(parsed.sheet.source, AUTHORED);

	Parsed again(parsed.sheet.source);
	ASSERT_TRUE(again.ok) << again.error;
	EXPECT_EQ(again.sheet.source, AUTHORED);
}

TEST(UIStyleSheetTest, RulelessSourceRefused) {
	ExpectRefused("this is not a stylesheet at all", "no rules");
	ExpectRefused("\n\t  \n", "no rules");
	ExpectRefused("/* everything is commented out for now */", "no rules");

	Parsed minimal(".a {}");
	EXPECT_TRUE(minimal.ok) << minimal.error;
}

TEST(UIStyleSheetTest, ShadowCapHoldsInSheet) {
	auto shadows = [](u32 count) {
		str value;
		for (u32 i = 0; i < count; ++i) {
			if (i > 0) {
				value += ", ";
			}
			value += "1px 2px 3px 4px #00000080";
		}
		return str(".a { box-shadow: ") + value + str("; }");
	};

	Parsed atCap(shadows(UI::Style::MAX_BOX_SHADOWS));
	EXPECT_TRUE(atCap.ok) << atCap.error;

	ExpectRefused(shadows(UI::Style::MAX_BOX_SHADOWS + 1), "box-shadow");

	Parsed twoRules(shadows(UI::Style::MAX_BOX_SHADOWS) + str("\n") + shadows(UI::Style::MAX_BOX_SHADOWS));
	EXPECT_TRUE(twoRules.ok) << twoRules.error;
}

TEST(UIStyleSheetTest, CombinatorsStillAcceptedWrongly) {
	Parsed descendant(".a .b { width: 10px; }");
	ASSERT_TRUE(descendant.ok) << descendant.error;

	UI::StyleSheet sheet;
	sheet.Parse(descendant.sheet.source);
	ASSERT_EQ(sheet.GetRules().size(), 1u);
	EXPECT_EQ(sheet.GetRules()[0].targetClass, StringHash("b"));
	EXPECT_EQ(sheet.GetRules()[0].specificity, 20);

	Parsed list(".a, .b { width: 10px; }");
	ASSERT_TRUE(list.ok) << list.error;
	UI::StyleSheet listSheet;
	listSheet.Parse(list.sheet.source);
	ASSERT_EQ(listSheet.GetRules().size(), 1u);
	EXPECT_EQ(listSheet.GetRules()[0].targetClass, StringHash("b"));
}

TEST(UIStyleSheetTest, SyntaxKnowsVocabulary) {
	std::filesystem::path grammarPath = RepoDir("EngineAssets/Schemas/slss.tmLanguage.json");
	if (grammarPath.empty()) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	str text = FileSystem::ReadTextFile(Path(grammarPath.generic_string()), MemTag::Core);
	ASSERT_FALSE(text.empty());

	LinearAllocator arena(text.size() + 64, MemTag::Core);
	JSONValue root = JSONParse(text, arena);
	ASSERT_TRUE(root.IsObject());

	const JSONValue& patterns = root["repository"]["declaration"]["patterns"];
	ASSERT_TRUE(patterns.IsArray());
	ASSERT_GT(patterns.Size(), 0u);
	str known = str(patterns[size_t(0)]["match"].AsString());
	ASSERT_FALSE(known.empty());

	auto listsProperty = [&known](const str& name) {
		auto isNameChar = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'; };
		for (size_t at = known.find(name); at != str::npos; at = known.find(name, at + 1)) {
			bool leftClear = at == 0 || !isNameChar(known[at - 1]);
			size_t after = at + name.size();
			bool rightClear = after >= known.size() || !isNameChar(known[after]);
			if (leftClear && rightClear) {
				return true;
			}
		}
		return false;
	};

	for (std::string_view property : UI::GetStylePropertyNames()) {
		str name{property};
		EXPECT_TRUE(listsProperty(name));
	}

	EXPECT_FALSE(listsProperty("font-family"));
	EXPECT_FALSE(listsProperty("ackground"));
}

TEST_F(StyleSheetFixture, SlssRoundTrips) {
	constexpr const char* AUTHORED = "/* Main.slss */\n.row {\n\tpadding: 8px;\n}\n";
	WriteSheet("project://Assets/Main.slss", AUTHORED);

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://Assets/Main.slss"));
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->GetAssetTypeId(), StringHash("StyleSheetAsset"));
	EXPECT_EQ(static_cast<StyleSheetAsset*>(loaded)->source, AUTHORED);

	AssetDatabase::SaveAsset(loaded);
	str reread = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/Main.slss")), MemTag::Core);
	EXPECT_EQ(reread, AUTHORED);

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(StyleSheetFixture, ARefusedSheetDoesNotLoad) {
	WriteSheet("project://Assets/Broken.slss", "no rules in here, just prose");
	EXPECT_EQ(AssetDatabase::LoadAsset(Path("project://Assets/Broken.slss")), nullptr);
}

TEST_F(StyleSheetFixture, CreateMintsStarterSheetThatImporterAccepts) {
	IAssetImporter* importer = AssetImporterRegistry::FindByExtension(".slss");
	ASSERT_NE(importer, nullptr);
	EXPECT_TRUE(importer->CanCreateAsset());
	EXPECT_EQ(importer->GetCreateMenuLabel(), "Style Sheet");

	Guid created = AssetDatabase::CreateAsset(Path("project://Assets/New Style Sheet.slss"), StringHash("StyleSheetAsset"));
	ASSERT_TRUE(created.IsValid());

	Asset* loaded = AssetDatabase::LoadAssetByGuid(created);
	ASSERT_NE(loaded, nullptr);
	EXPECT_FALSE(static_cast<StyleSheetAsset*>(loaded)->source.empty());
	AssetDatabase::UnloadAsset(loaded);
}
