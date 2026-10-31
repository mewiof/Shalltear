#include "TestsPCH.h"

#include "UIStyleVocabulary.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/UIDocument.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
	}

	struct Parsed {
		UIDocumentAsset document;
		str error;
		bool ok = false;

		explicit Parsed(std::string_view source) { ok = document.Parse(source, error); }
	};

	void ExpectRefused(std::string_view source, std::string_view namedInMessage) {
		UIDocumentAsset document;
		str error;
		EXPECT_FALSE(document.Parse(source, error)) << "accepted: " << source;
		EXPECT_NE(error.find(str(namedInMessage)), str::npos) << namedInMessage << "': " << error;
		EXPECT_TRUE(document.root.tag.empty()) << error;
		EXPECT_TRUE(document.sheets.empty()) << error;
	}

	str Document(std::string_view body) {
		return str("<ui version=\"1\">") + str(body) + str("</ui>");
	}

	class UIDocumentSheetFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_uisheet_" + std::to_string(stamp) + "_" + std::to_string(unique));
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

		void WriteFile(const str& virtualPath, std::string_view text) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(resolved, str(text));
		}

		void WriteDocument(const str& virtualPath, const str& body) {
			WriteFile(virtualPath, str("<ui version=\"1\">") + body + str("</ui>"));
		}

		UIDocumentAsset* Load(const str& virtualPath) {
			Asset* loaded = AssetDatabase::LoadAsset(Path(virtualPath));
			return loaded && loaded->GetAssetTypeId() == StringHash("UIDocumentAsset")
					   ? static_cast<UIDocumentAsset*>(loaded)
					   : nullptr;
		}
	};

} // anonymous namespace

TEST(UIDocumentSheetTest, NonSheetRefusedByName) {
	ExpectRefused(Document(R"(<sheet/><panel/>)"), "src");
	ExpectRefused(Document(R"(<sheet src="a.slss" name="Main"/><panel/>)"), "name");
	ExpectRefused(Document(R"(<sheet src="a.slss">x</sheet><panel/>)"), "a.slss");
	ExpectRefused(Document(R"(<sheet src="a.slss"><panel/></sheet><panel/>)"), "a.slss");

	ExpectRefused(Document(R"(<panel><sheet/></panel>)"), "sheet");
	ExpectRefused(Document(R"(<panel><sheet src="a.slss"/></panel>)"), "belongs directly inside");

	ExpectRefused(Document(R"(<sheet src="a.slss"/><sheet src="a.slss"/><panel/>)"), "a.slss");
	Parsed two(Document(R"(<sheet src="a.slss"/><sheet src="b.slss"/><panel/>)"));
	EXPECT_TRUE(two.ok) << two.error;
	EXPECT_EQ(two.document.sheets.size(), 2u);
}

TEST(UIDocumentSheetTest, SheetsRoundTripFirst) {
	Parsed first(R"(<ui version="1">
	<sheet src="project://UI/Main.slss"/>
	<sheet src="project://UI/Hud.slss"/>
	<template src="project://UI/Row.slxml" name="Row"/>
	<panel id="hud"><instance template="Row"/></panel>
</ui>
)");
	ASSERT_TRUE(first.ok) << first.error;

	str written = first.document.Write();
	ASSERT_FALSE(written.empty());

	UIDocumentAsset reread;
	str error;
	ASSERT_TRUE(reread.Parse(written, error)) << error << ' ' << written;
	EXPECT_EQ(reread.Write(), written);
	ASSERT_EQ(reread.sheets.size(), 2u);
	EXPECT_EQ(reread.sheets[0].src, "project://UI/Main.slss");
	EXPECT_EQ(reread.sheets[1].src, "project://UI/Hud.slss");

	Parsed scrambled(R"(<ui version="1"><panel id="hud"><instance template="Row"/></panel><template src="project://UI/Row.slxml" name="Row"/><sheet src="project://UI/Main.slss"/><sheet src="project://UI/Hud.slss"/></ui>)");
	ASSERT_TRUE(scrambled.ok) << scrambled.error;
	EXPECT_EQ(scrambled.document.Write(), written);
}

TEST_F(UIDocumentSheetFixture, SheetResolvedAtImport) {
	WriteFile("project://Assets/Main.slss", ".row { width: 42px; } #title { height: 11px; }");
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<sheet src="project://Assets/Main.slss"/><panel id="title"><panel class="row"/></panel>)");

	UIDocumentAsset* screen = Load("project://Assets/Screen.slxml");
	ASSERT_NE(screen, nullptr);
	ASSERT_EQ(screen->sheets.size(), 1u);
	ASSERT_TRUE(screen->sheets[0].sheet.IsValid());

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = screen->Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(root));

	EXPECT_EQ(ctx.GetComputedStyle(root).height, UI::Length::Pixel(11.0f));
	UI::ElementHandle row = ctx.MakeHandle(ctx.GetElement(root)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(row));
	EXPECT_EQ(ctx.GetComputedStyle(row).width, UI::Length::Pixel(42.0f));
}

TEST_F(UIDocumentSheetFixture, ADocumentsSheetNeverReachesAnotherContext) {
	WriteFile("project://Assets/Loud.slss", ".row { width: 99px; }");
	WriteDocument("project://Assets/Loud.slxml", R"(<sheet src="project://Assets/Loud.slss"/><panel class="row"/>)");

	UIDocumentAsset* loud = Load("project://Assets/Loud.slxml");
	ASSERT_NE(loud, nullptr);

	UI::Context editor(Desc(), nullptr);
	UI::ElementHandle editorRow = editor.CreateElement();
	editor.AppendChild(editor.GetRoot(), editorRow);
	editor.AddClass(editorRow, "row"_hash);
	UI::Length before = editor.GetComputedStyle(editorRow).width;

	UI::Context document(Desc(), nullptr);
	UI::ElementHandle root = loud->Instantiate(document, document.GetRoot());
	ASSERT_TRUE(document.IsAlive(root));
	EXPECT_EQ(document.GetComputedStyle(root).width, UI::Length::Pixel(99.0f));

	EXPECT_EQ(editor.GetComputedStyle(editorRow).width, before);
}

TEST_F(UIDocumentSheetFixture, VocabularyReachesElement) {
	WriteFile("project://Assets/All.slss", str(".everything { ") + StyleVocabulary::EveryDeclaration() + str("; }"));
	WriteDocument("project://Assets/All.slxml", R"(<sheet src="project://Assets/All.slss"/><panel class="everything"/>)");

	UIDocumentAsset* all = Load("project://Assets/All.slxml");
	ASSERT_NE(all, nullptr);

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = all->Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(root));

	EXPECT_EQ(ctx.GetComputedStyle(root), StyleVocabulary::EveryDeclarationAsStyle());
}

TEST_F(UIDocumentSheetFixture, TemplateComposesInWithSheet) {
	WriteFile("project://Assets/Row.slss", ".row { width: 31px; }");
	WriteDocument("project://Assets/Row.slxml", R"(<sheet src="project://Assets/Row.slss"/><panel class="row"/>)");
	WriteDocument("project://Assets/List.slxml",
				  R"(<template src="project://Assets/Row.slxml" name="Row"/><panel><instance template="Row"/></panel>)");

	UIDocumentAsset* list = Load("project://Assets/List.slxml");
	ASSERT_NE(list, nullptr);
	EXPECT_TRUE(list->sheets.empty());

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = list->Instantiate(ctx, ctx.GetRoot());
	UI::ElementHandle row = ctx.MakeHandle(ctx.GetElement(root)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(row));
	EXPECT_EQ(ctx.GetComputedStyle(row).width, UI::Length::Pixel(31.0f));
}

TEST_F(UIDocumentSheetFixture, SheetLoadsOncePerContext) {
	WriteFile("project://Assets/Row.slss", ".row { width: 31px; }");
	WriteDocument("project://Assets/Row.slxml", R"(<sheet src="project://Assets/Row.slss"/><panel class="row"/>)");

	UIDocumentAsset* row = Load("project://Assets/Row.slxml");
	ASSERT_NE(row, nullptr);

	UI::Context fresh(Desc(), nullptr);
	EXPECT_TRUE(fresh.LoadStyleSheetOnce(StringHash("project://Assets/Row.slss"), ".probe {}"));

	UI::Context ctx(Desc(), nullptr);
	for (u32 i = 0; i < 5; ++i) {
		ASSERT_TRUE(ctx.IsAlive(row->Instantiate(ctx, ctx.GetRoot())));
	}
	EXPECT_FALSE(ctx.LoadStyleSheetOnce(StringHash("project://Assets/Row.slss"), ".probe {}"));

	for (u32 index = ctx.GetElement(ctx.GetRoot())->firstChild; index != UI::INVALID_UI_INDEX;
		 index = ctx.GetElement(ctx.MakeHandle(index))->nextSibling) {
		EXPECT_EQ(ctx.GetComputedStyle(ctx.MakeHandle(index)).width, UI::Length::Pixel(31.0f));
	}
}

TEST_F(UIDocumentSheetFixture, NonStyleSheetRefused) {
	WriteDocument("project://Assets/Missing.slxml", R"(<sheet src="project://Assets/Nope.slss"/><panel/>)");
	EXPECT_EQ(Load("project://Assets/Missing.slxml"), nullptr);

	WriteFile("project://Assets/note.txt", "not a stylesheet");
	WriteDocument("project://Assets/WrongType.slxml", R"(<sheet src="project://Assets/note.txt"/><panel/>)");
	EXPECT_EQ(Load("project://Assets/WrongType.slxml"), nullptr);

	WriteFile("project://Assets/Broken.slss", "this is not a stylesheet at all");
	WriteDocument("project://Assets/Broken.slxml", R"(<sheet src="project://Assets/Broken.slss"/><panel/>)");
	EXPECT_EQ(Load("project://Assets/Broken.slxml"), nullptr);
}

TEST_F(UIDocumentSheetFixture, UnloadingDocumentReleasesSheetsItHeld) {
	WriteFile("project://Assets/Main.slss", ".row { width: 42px; }");
	WriteDocument("project://Assets/Screen.slxml", R"(<sheet src="project://Assets/Main.slss"/><panel class="row"/>)");

	UIDocumentAsset* screen = Load("project://Assets/Screen.slxml");
	ASSERT_NE(screen, nullptr);

	Handle<StyleSheetAsset> sheet = screen->sheets[0].sheet;
	ASSERT_NE(sheet.Get(), nullptr);

	AssetDatabase::UnloadAsset(AssetDatabase::LoadAsset(Path("project://Assets/Main.slss")));
	EXPECT_NE(sheet.Get(), nullptr);

	AssetDatabase::UnloadAsset(screen);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(sheet.Get(), nullptr);
}
