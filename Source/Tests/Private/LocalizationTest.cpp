#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Localization.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UIDocument.h"
#include "Shalltear/Editor/CookOps.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
	}

	str Document(std::string_view body) {
		return str("<ui version=\"1\">") + str(body) + str("</ui>");
	}

	UI::ElementHandle NthChild(UI::Context& ctx, UI::ElementHandle parent, u32 n) {
		const UI::Element* element = ctx.GetElement(parent);
		u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX;
		for (u32 i = 0; child != UI::INVALID_UI_INDEX && i < n; ++i) {
			child = ctx.GetElement(ctx.MakeHandle(child))->nextSibling;
		}
		return child != UI::INVALID_UI_INDEX ? ctx.MakeHandle(child) : UI::ElementHandle{};
	}

	class LocalizationFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_localization_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			Localization::UseSystemLanguage();
			AssetDatabase::Terminate();
			Localization::ReloadTables();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Guid Write(const str& name, std::string_view text) {
			std::ofstream(_root / "Assets" / name, std::ios::binary) << text;
			return AssetDatabase::EnsureAssetIdentity(Path("project://Assets/" + name));
		}

		static const char* MenuTable() {
			return "key,en,ru,pt,zh-Hans\n"
				   "title,Main Menu,\xd0\x93\xd0\xbb\xd0\xb0\xd0\xb2\xd0\xbd\xd0\xbe\xd0\xb5,Menu,\xe4\xb8\xbb\xe8\x8f\x9c\xe5\x8d\x95\n"
				   "play,Play,\xd0\x98\xd0\xb3\xd1\x80\xd0\xb0\xd1\x82\xd1\x8c,Jogar,\xe5\xbc\x80\xe5\xa7\x8b\n"
				   "quit,Quit,,,\n";
		}
	};

} // anonymous namespace

TEST(StringTableTest, ParsesQuotesNewlinesAndLanguageCodes) {
	StringTableAsset table;
	str error;
	ASSERT_TRUE(table.Parse("\xEF\xBB\xBFKey, EN ,pt_br\r\n"
							"menu.play,Play,Jogar\r\n"
							"\r\n"
							"quote,\"Say \"\"hi\"\", then go\",\"Linha 1\nLinha 2\"\n"
							"short,Only\n",
							error))
		<< error;
	ASSERT_EQ(table.languages.size(), 2u);
	EXPECT_EQ(table.languages[0], "en");
	EXPECT_EQ(table.languages[1], "pt-BR");
	ASSERT_EQ(table.keys.size(), 3u);
	EXPECT_EQ(table.Find("menu.play", 1), "Jogar");
	EXPECT_EQ(table.Find("quote", 0), "Say \"hi\", then go");
	EXPECT_EQ(table.Find("quote", 1), "Linha 1\nLinha 2");
	EXPECT_EQ(table.Find("short", 0), "Only");
	EXPECT_TRUE(table.Find("short", 1).empty());
	EXPECT_TRUE(table.Find("missing", 0).empty());
	EXPECT_TRUE(table.Find("short", 2).empty());
	EXPECT_EQ(table.FindLanguage("pt-BR"), 1);
	EXPECT_EQ(table.FindLanguage("pt"), -1);
}

TEST(StringTableTest, RefusesNonTables) {
	struct Case {
		const char* csv;
		const char* named;
	};
	for (const Case& probe : {Case{"id,en\na,b\n", "'id'"}, Case{"key,en,EN\n", "twice"}, Case{"key,en\na,\"open\n", "unclosed"},
							  Case{"", "empty"}, Case{"key,,ru\n", "no language"}, Case{"key,en\na,\"x\"y\n", "after closing quote"}}) {
		StringTableAsset table;
		str error;
		EXPECT_FALSE(table.Parse(probe.csv, error)) << probe.csv;
		EXPECT_NE(error.find(probe.named), str::npos) << probe.named << ": " << error;
	}
}

TEST(StringTableTest, RepeatedKeyKeepsFirstRow) {
	StringTableAsset table;
	str error;
	ASSERT_TRUE(table.Parse("key,en\nplay,First\nplay,Second\n,orphan\nquit,Quit\n", error)) << error;
	ASSERT_EQ(table.keys.size(), 2u);
	EXPECT_EQ(table.Find("play", 0), "First");
	EXPECT_EQ(table.Find("quit", 0), "Quit");
}

TEST(StringTableTest, CookedTableReadsBack) {
	StringTableAsset table;
	str error;
	ASSERT_TRUE(table.Parse("key,en,ru\na,A,\xd0\x90\nb,,\xd0\x91\n", error)) << error;

	PakStringPool strings;
	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Debug)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Debug)};
	CookWriter descriptor(descriptorBytes, strings);
	CookWriter payload(payloadBytes, strings);
	ASSERT_TRUE(table.Cook(descriptor, payload));

	StringTableAsset cooked;
	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size());
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size());
	ASSERT_TRUE(cooked.LoadCooked(descriptorReader, payloadReader));
	EXPECT_EQ(cooked.languages, table.languages);
	EXPECT_EQ(cooked.keys, table.keys);
	EXPECT_EQ(cooked.cells, table.cells);
	EXPECT_EQ(cooked.Find("b", 1), "\xd0\x91");
	EXPECT_TRUE(cooked.Find("b", 0).empty());

	StringTableAsset truncated;
	CookReader shortDescriptor(descriptorBytes.data(), descriptorBytes.size());
	CookReader shortPayload(payloadBytes.data(), payloadBytes.size() - 1);
	EXPECT_FALSE(truncated.LoadCooked(shortDescriptor, shortPayload));
}

TEST(LocalizationTest, LanguageCodesNormalize) {
	EXPECT_EQ(Localization::NormalizeLanguage("en_US.UTF-8"), "en-US");
	EXPECT_EQ(Localization::NormalizeLanguage("zh-hans-cn"), "zh-Hans-CN");
	EXPECT_EQ(Localization::NormalizeLanguage(" PT_br "), "pt-BR");
	EXPECT_EQ(Localization::NormalizeLanguage("de@euro"), "de");
	EXPECT_EQ(Localization::NormalizeLanguage("C"), "");
	const str system = Localization::GetSystemLanguage();
	EXPECT_EQ(system, Localization::NormalizeLanguage(system));
}

TEST_F(LocalizationFixture, LookupWalksLanguageThenDefault) {
	ASSERT_TRUE(Write("Menu.csv", MenuTable()).IsValid());
	Localization::ReloadTables();

	Localization::SetLanguage("pt-BR");
	EXPECT_EQ(Localization::GetLanguage(), "pt-BR");
	EXPECT_EQ(Localization::Translate("play"), "Jogar");
	EXPECT_EQ(Localization::Translate("quit"), "Quit");
	Localization::SetLanguage("zh_CN");
	EXPECT_EQ(Localization::Translate("play"), "\xe5\xbc\x80\xe5\xa7\x8b");
	Localization::SetLanguage("ru-RU");
	EXPECT_EQ(Localization::Translate("play"), "\xd0\x98\xd0\xb3\xd1\x80\xd0\xb0\xd1\x82\xd1\x8c");

	str out = "untouched";
	EXPECT_FALSE(Localization::TryTranslate("nothing", out));
	EXPECT_EQ(out, "untouched");
	EXPECT_EQ(Localization::Translate("nothing"), "nothing");
	EXPECT_EQ(Localization::GetDefaultLanguage(), "en");
}

TEST_F(LocalizationFixture, AddedTableSearchedBeforeProject) {
	ASSERT_TRUE(Write("Menu.csv", MenuTable()).IsValid());
	Localization::ReloadTables();
	Localization::SetLanguage("en");

	StringTableAsset mod;
	str error;
	ASSERT_TRUE(mod.Parse("key,en\nplay,Start\n", error)) << error;
	const u32 before = Localization::GetGeneration();
	Localization::AddTable(Handle<StringTableAsset>(&mod));
	EXPECT_NE(Localization::GetGeneration(), before);
	EXPECT_EQ(Localization::Translate("play"), "Start");
	EXPECT_EQ(Localization::Translate("quit"), "Quit");
	Localization::RemoveTable(Handle<StringTableAsset>(&mod));
	EXPECT_EQ(Localization::Translate("play"), "Play");
}

TEST_F(LocalizationFixture, GenerationMovesWithLanguageAndReimport) {
	ASSERT_TRUE(Write("Menu.csv", MenuTable()).IsValid());
	Localization::ReloadTables();
	Localization::SetLanguage("en");
	const u32 english = Localization::GetGeneration();
	Localization::SetLanguage("en");
	EXPECT_EQ(Localization::GetGeneration(), english);
	Localization::SetLanguage("ru");
	const u32 russian = Localization::GetGeneration();
	EXPECT_NE(russian, english);

	Localization::SetLanguage("en");
	std::ofstream(_root / "Assets" / "Menu.csv", std::ios::binary) << "key,en\nplay,Begin\n";
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Assets/Menu.csv")));
	EXPECT_EQ(Localization::Translate("play"), "Begin");
	str gone;
	EXPECT_FALSE(Localization::TryTranslate("quit", gone));
	EXPECT_NE(Localization::GetGeneration(), english);
}

TEST_F(LocalizationFixture, TablesFoundByTypeAndAlwaysCooked) {
	const Guid second = Write("B.csv", "key,en\nb,B\n");
	const Guid first = Write("A.csv", "key,en\na,A\n");
	ASSERT_TRUE(Write("Notes.txt", "not a table").IsValid());
	Vector<Guid> found{GetMemoryResource(MemTag::Debug)};
	AssetDatabase::FindAssets(StringTableAsset::GetReflectType()->name, found);
	ASSERT_EQ(found.size(), 2u);
	EXPECT_TRUE(std::is_sorted(found.begin(), found.end()));
	EXPECT_NE(std::find(found.begin(), found.end(), first), found.end());
	EXPECT_NE(std::find(found.begin(), found.end(), second), found.end());

	Vector<Guid> roots{GetMemoryResource(MemTag::Debug)};
	roots.push_back(first);
	EXPECT_EQ(CookOps::CollectAlwaysCookedRoots(roots), 1u);
	EXPECT_EQ(roots.size(), 2u);
	EXPECT_EQ(CookOps::CollectAlwaysCookedRoots(roots), 0u);
}

TEST_F(LocalizationFixture, FailedImportNotCooked) {
	const Guid table = Write("Strings.csv", "key,en\na,A\n");
	ASSERT_TRUE(Write("Data.csv", "id,value\n1,2\n").IsValid());
	Vector<Guid> roots{GetMemoryResource(MemTag::Debug)};
	Vector<str> failed{GetMemoryResource(MemTag::Debug)};
	EXPECT_EQ(CookOps::CollectAlwaysCookedRoots(roots, &failed), 1u);
	ASSERT_EQ(roots.size(), 1u);
	EXPECT_EQ(roots[0], table);
	ASSERT_EQ(failed.size(), 1u);
	EXPECT_EQ(failed[0], "project://Assets/Data.csv");
}

TEST_F(LocalizationFixture, KeyedTextFollowsLanguage) {
	ASSERT_TRUE(Write("Menu.csv", MenuTable()).IsValid());
	Localization::ReloadTables();
	Localization::SetLanguage("en");

	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	auto child = [&ctx]() {
		UI::ElementHandle element = ctx.CreateElement();
		ctx.AppendChild(ctx.GetRoot(), element);
		return element;
	};
	UI::ElementHandle title = child();
	UI::ElementHandle lost = child();
	UI::ElementHandle bare = child();
	ctx.SetTextKey(title, "title");
	ctx.SetTextKey(lost, "lost", "Fallback");
	ctx.SetTextKey(bare, "lost");
	EXPECT_EQ(ctx.GetTextString(title), "Main Menu");
	EXPECT_EQ(ctx.GetTextKey(title), "title");
	EXPECT_EQ(ctx.GetTextString(lost), "Fallback");
	EXPECT_EQ(ctx.GetTextString(bare), "lost");

	Localization::SetLanguage("ru");
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(title), "\xd0\x93\xd0\xbb\xd0\xb0\xd0\xb2\xd0\xbd\xd0\xbe\xd0\xb5");

	ctx.SetTextString(title, "Custom");
	EXPECT_TRUE(ctx.GetTextKey(title).empty());
	Localization::SetLanguage("pt");
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(title), "Custom");
}

TEST_F(LocalizationFixture, TextKeysBindPlainAndButtonText) {
	ASSERT_TRUE(Write("Menu.csv", MenuTable()).IsValid());
	Localization::ReloadTables();
	Localization::SetLanguage("en");

	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(Document(R"(<panel><label text-key="title">Menu</label><button text="Go" text-key="play"/>)"
										R"(<label text-key="lost">Lost</label></panel>)"),
							   error))
		<< error;
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle root = document.Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(root));
	UI::ElementHandle title = NthChild(ctx, root, 0);
	UI::ElementHandle label = NthChild(ctx, NthChild(ctx, root, 1), 0);
	UI::ElementHandle lost = NthChild(ctx, root, 2);
	EXPECT_EQ(ctx.GetTextString(title), "Main Menu");
	EXPECT_EQ(ctx.GetTextString(label), "Play");
	EXPECT_EQ(ctx.GetTextString(lost), "Lost");

	Localization::SetLanguage("pt");
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(title), "Menu");
	EXPECT_EQ(ctx.GetTextString(label), "Jogar");

	const str written = document.Write();
	EXPECT_NE(written.find(R"(text-key="play")"), str::npos) << written;
	UIDocumentAsset reread;
	ASSERT_TRUE(reread.Parse(written, error)) << error;
	ASSERT_EQ(reread.root.children.size(), 3u);
	EXPECT_EQ(reread.root.children[1].textKey, "play");

	for (const char* refused : {R"(<checkbox text-key="x"/>)", R"(<label text-key=""/>)"}) {
		UIDocumentAsset bad;
		EXPECT_FALSE(bad.Parse(Document(refused), error)) << refused;
		EXPECT_NE(error.find("text-key"), str::npos) << error;
	}
}

TEST_F(LocalizationFixture, TemplateOverrideSetsTextKey) {
	ASSERT_TRUE(Write("Menu.csv", MenuTable()).IsValid());
	Localization::ReloadTables();
	Localization::SetLanguage("en");
	ASSERT_TRUE(Write("Plate.slxml", Document(R"(<panel><label id="who">Name</label><button id="act" text="Default"/></panel>)")).IsValid());
	ASSERT_TRUE(Write("World.slxml", Document(R"(<template src="project://Assets/Plate.slxml" name="Plate"/><panel>)"
											  R"(<instance template="Plate"><attribute-overrides element-id="who" text-key="title"/>)"
											  R"(<attribute-overrides element-id="act" text-key="quit"/></instance></panel>)"))
					.IsValid());

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://Assets/World.slxml"));
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->GetAssetTypeId(), UIDocumentAsset::GetReflectType()->name);
	const UIDocumentAsset& world = static_cast<const UIDocumentAsset&>(*loaded);
	EXPECT_NE(world.Write().find(R"(element-id="who" text-key="title")"), str::npos) << world.Write();

	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle root = world.Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(root));
	UI::ElementHandle plate = NthChild(ctx, root, 0);
	EXPECT_EQ(ctx.GetTextString(NthChild(ctx, plate, 0)), "Main Menu");
	EXPECT_EQ(ctx.GetTextString(NthChild(ctx, NthChild(ctx, plate, 1), 0)), "Quit");
	AssetDatabase::UnloadAsset(loaded);

	ASSERT_TRUE(Write("Ticks.slxml", Document(R"(<panel><checkbox id="tick"/></panel>)")).IsValid());
	auto instanceOf = [](std::string_view override) {
		return Document(str(R"(<template src="project://Assets/Ticks.slxml" name="Ticks"/><panel><instance template="Ticks">)") +
						R"(<attribute-overrides element-id="tick" )" + str(override) + R"(/></instance></panel>)");
	};
	ASSERT_TRUE(Write("Ticked.slxml", instanceOf(R"(value="true")")).IsValid());
	Asset* ticked = AssetDatabase::LoadAsset(Path("project://Assets/Ticked.slxml"));
	EXPECT_NE(ticked, nullptr);
	if (ticked) {
		AssetDatabase::UnloadAsset(ticked);
	}
	ASSERT_TRUE(Write("Keyed.slxml", instanceOf(R"(text-key="title")")).IsValid());
	EXPECT_EQ(AssetDatabase::LoadAsset(Path("project://Assets/Keyed.slxml")), nullptr);
}

TEST_F(LocalizationFixture, StateReleasedWithAssetDatabase) {
	Localization::SetLanguage("zu");
	ASSERT_EQ(Localization::GetLanguage(), "zu");
	AssetDatabase::Terminate();
	EXPECT_NE(Localization::GetLanguage(), "zu");
	AssetDatabase::Initialize();
}
