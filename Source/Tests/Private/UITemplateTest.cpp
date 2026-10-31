#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/UI/Context.h"
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
		EXPECT_TRUE(document.templates.empty()) << error;
	}

	str Document(std::string_view body) {
		return str("<ui version=\"1\">") + str(body) + str("</ui>");
	}

	class UITemplateFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_uitemplates_" + std::to_string(stamp) + "_" + std::to_string(unique));
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

		void WriteDocument(const str& virtualPath, const str& body) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::filesystem::path fsPath(resolved.ToString());
			std::error_code ec;
			std::filesystem::create_directories(fsPath.parent_path(), ec);
			FileSystem::WriteTextFile(resolved, str("<ui version=\"1\">") + body + str("</ui>"));
		}

		UIDocumentAsset* Load(const str& virtualPath) {
			Asset* loaded = AssetDatabase::LoadAsset(Path(virtualPath));
			return loaded && loaded->GetAssetTypeId() == StringHash("UIDocumentAsset")
					   ? static_cast<UIDocumentAsset*>(loaded)
					   : nullptr;
		}
	};

} // anonymous namespace

TEST(UITemplateTest, TemplateVocabularyParsesIntoDeclarationsAndInstance) {
	Parsed parsed(R"(<?xml version="1.0" encoding="UTF-8"?>
<ui version="1">
	<template src="project://UI/Portrait.slxml" name="Portrait"/>
	<panel id="lobby">
		<instance template="Portrait" id="p1" class="mine">
			<attribute-overrides element-id="ready" value="true"/>
			<attribute-overrides element-id="who">Alice</attribute-overrides>
		</instance>
	</panel>
</ui>
)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	ASSERT_EQ(parsed.document.templates.size(), 1u);
	EXPECT_EQ(parsed.document.templates[0].name, "Portrait");
	EXPECT_EQ(parsed.document.templates[0].src, "project://UI/Portrait.slxml");
	EXPECT_FALSE(parsed.document.templates[0].document.IsValid());

	ASSERT_EQ(parsed.document.root.children.size(), 1u);
	const UIDocumentNode& instance = parsed.document.root.children[0];
	EXPECT_EQ(instance.tag, "instance");
	EXPECT_EQ(instance.templateName, "Portrait");
	EXPECT_EQ(instance.id, "p1");
	ASSERT_EQ(instance.classCount, 1u);

	EXPECT_TRUE(instance.children.empty());
	ASSERT_EQ(instance.overrides.size(), 2u);
	EXPECT_EQ(instance.overrides[0].elementId, "ready");
	ASSERT_EQ(instance.overrides[0].attributes.size(), 1u);
	EXPECT_EQ(instance.overrides[0].attributes[0].name, "value");
	EXPECT_EQ(instance.overrides[0].attributes[0].value, "true");
	EXPECT_EQ(instance.overrides[1].elementId, "who");
	EXPECT_EQ(instance.overrides[1].text, "Alice");
	EXPECT_TRUE(instance.overrides[1].attributes.empty());
}

TEST(UITemplateTest, NonTemplateRefusedByName) {
	ExpectRefused(R"(<ui version="1"><template name="Portrait"/><panel/></ui>)", "src");
	ExpectRefused(R"(<ui version="1"><template src="project://a.slxml"/><panel/></ui>)", "name");
	ExpectRefused(R"(<ui version="1"><template src="a.slxml" name="P" type="x"/><panel/></ui>)", "type");
	ExpectRefused(R"(<ui version="1"><template src="a.slxml" name="P">x</template><panel/></ui>)", "P");
	ExpectRefused(R"(<ui version="1"><template src="a.slxml" name="P"><panel/></template><panel/></ui>)", "P");

	ExpectRefused(R"(<ui version="1"><template src="a.slxml" name="P"/><template src="b.slxml" name="P"/><panel/></ui>)", "P");
	Parsed two(R"(<ui version="1"><template src="a.slxml" name="A"/><template src="b.slxml" name="B"/><panel/></ui>)");
	EXPECT_TRUE(two.ok) << two.error;

	ExpectRefused(Document(R"(<panel><template src="a.slxml" name="P"/></panel>)"), "template");
}

TEST(UITemplateTest, InstanceNeedsTemplateThisDocumentDeclares) {
	constexpr const char* DECLARE = R"(<template src="project://a.slxml" name="Portrait"/>)";

	ExpectRefused(Document(str(DECLARE) + str(R"(<instance/>)")), "template");
	ExpectRefused(Document(str(DECLARE) + str(R"(<instance template="Portrat"/>)")), "Portrat");
	ExpectRefused(Document(R"(<instance template="Portrait"/>)"), "Portrait");

	ExpectRefused(Document(str(DECLARE) + str(R"(<instance template="Portrait">Alice</instance>)")), "attribute-overrides");
	ExpectRefused(Document(str(DECLARE) + str(R"(<instance template="Portrait"><panel id="extra"/></instance>)")), "extra");
	Parsed styled(Document(str(DECLARE) + str(R"(<instance template="Portrait" id="a" class="b" style="width: 3px"/>)")));
	EXPECT_TRUE(styled.ok) << styled.error;
}

TEST(UITemplateTest, InvalidOverridesRefused) {
	constexpr const char* HEAD = R"(<template src="project://a.slxml" name="P"/>)";
	auto instance = [&](std::string_view overrides) {
		return Document(str(HEAD) + str("<instance template=\"P\">") + str(overrides) + str("</instance>"));
	};

	ExpectRefused(instance(R"(<attribute-overrides element-id="x" class="big"/>)"), "class");
	ExpectRefused(instance(R"(<attribute-overrides element-id="x" style="width: 3px"/>)"), "style");
	ExpectRefused(instance(R"(<attribute-overrides element-id="x" id="renamed"/>)"), "id");

	ExpectRefused(instance(R"(<attribute-overrides text="Alice"/>)"), "element-id");
	ExpectRefused(instance(R"(<attribute-overrides element-id="x"/>)"), "x");
	ExpectRefused(instance(R"(<attribute-overrides element-id="x"><panel/></attribute-overrides>)"), "x");

	ExpectRefused(instance(R"(<attribute-overrides element-id="x" value="true"/><attribute-overrides element-id="x">hi</attribute-overrides>)"), "x");

	ExpectRefused(Document(R"(<panel><attribute-overrides element-id="x" text="a"/></panel>)"), "instance");
}

TEST(UITemplateTest, TemplatesRoundTrip) {
	Parsed first(R"(<ui version="1">
	<template src="project://UI/Portrait.slxml" name="Portrait"/>
	<template src="project://UI/Row.slxml" name="Row"/>
	<panel id="lobby" class="a b" style="flex-direction: column">
		<instance template="Portrait" id="p1" class="mine" style="width: 40px">
			<attribute-overrides element-id="ready" value="true" min="0"/>
			<attribute-overrides element-id="who">Alice &amp; Bob</attribute-overrides>
		</instance>
		<instance template="Row"/>
	</panel>
</ui>
)");
	ASSERT_TRUE(first.ok) << first.error;

	str written = first.document.Write();
	ASSERT_FALSE(written.empty());

	UIDocumentAsset reread;
	str error;
	ASSERT_TRUE(reread.Parse(written, error)) << error << ' ' << written;
	EXPECT_EQ(reread.Write(), written);

	ASSERT_EQ(reread.root.children.size(), 2u);
	const UIDocumentNode& instance = reread.root.children[0];
	ASSERT_EQ(instance.overrides.size(), 2u);
	ASSERT_EQ(instance.overrides[0].attributes.size(), 2u);
	EXPECT_EQ(instance.overrides[0].attributes[0].name, "value");
	EXPECT_EQ(instance.overrides[0].attributes[1].name, "min");
	EXPECT_EQ(instance.overrides[1].text, "Alice & Bob");

	ASSERT_EQ(reread.templates.size(), 2u);
	EXPECT_EQ(reread.templates[0].src, "project://UI/Portrait.slxml");
}

TEST(UITemplateTest, DeclarationsMoveFirst) {
	Parsed trailing(R"(<ui version="1"><panel/><template src="a.slxml" name="P"/></ui>)");
	ASSERT_TRUE(trailing.ok) << trailing.error;
	Parsed leading(R"(<ui version="1"><template src="a.slxml" name="P"/><panel/></ui>)");
	ASSERT_TRUE(leading.ok) << leading.error;
	EXPECT_EQ(trailing.document.Write(), leading.document.Write());

	ExpectRefused(R"(<ui version="1"><template src="a.slxml" name="P"/><panel id="a"/><panel id="b"/></ui>)", "b");
}

TEST_F(UITemplateFixture, InstanceComposesInPlace) {
	WriteDocument("project://Assets/Portrait.slxml", R"(<panel class="portrait"><label id="who">Name</label></panel>)");
	WriteDocument("project://Assets/Lobby.slxml", R"(<template src="project://Assets/Portrait.slxml" name="P"/><panel id="lobby"><instance template="P"/></panel>)");

	UIDocumentAsset* lobby = Load("project://Assets/Lobby.slxml");
	ASSERT_NE(lobby, nullptr);
	ASSERT_EQ(lobby->templates.size(), 1u);
	EXPECT_TRUE(lobby->templates[0].document.IsValid());

	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle root = lobby->Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(root));

	UI::ElementHandle portrait = ctx.MakeHandle(ctx.GetElement(root)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(portrait));
	EXPECT_TRUE(ctx.HasClass(portrait, "portrait"_hash));
	EXPECT_EQ(ctx.GetElement(portrait)->typeTag, "panel"_hash);

	UI::ElementHandle who = ctx.MakeHandle(ctx.GetElement(portrait)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(who));
	EXPECT_EQ(ctx.GetTextString(who), "Name");
	EXPECT_EQ(ctx.GetLiveCount(), 3u);
}

TEST_F(UITemplateFixture, OverridesReachAttributesAndText) {
	WriteDocument("project://Assets/Plate.slxml",
				  R"(<panel><label id="who">Name</label><button id="act" text="Default"/><checkbox id="ready" value="false"/></panel>)");
	WriteDocument("project://Assets/World.slxml",
				  R"(<template src="project://Assets/Plate.slxml" name="Plate"/><panel>)"
				  R"(<instance template="Plate" id="p1"><attribute-overrides element-id="who">Alice</attribute-overrides>)"
				  R"(<attribute-overrides element-id="act" text="Kick"/><attribute-overrides element-id="ready" value="true"/></instance>)"
				  R"(<instance template="Plate" id="p2"/></panel>)");

	UIDocumentAsset* world = Load("project://Assets/World.slxml");
	ASSERT_NE(world, nullptr);

	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle root = world->Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(root));

	UI::ElementHandle first = ctx.MakeHandle(ctx.GetElement(root)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(first));
	UI::ElementHandle who = ctx.MakeHandle(ctx.GetElement(first)->firstChild);
	EXPECT_EQ(ctx.GetTextString(who), "Alice");

	UI::ElementHandle act = ctx.MakeHandle(ctx.GetElement(who)->nextSibling);
	ASSERT_TRUE(ctx.IsAlive(act));
	EXPECT_EQ(ctx.GetTextString(ctx.MakeHandle(ctx.GetElement(act)->firstChild)), "Kick");

	UI::ElementHandle second = ctx.MakeHandle(ctx.GetElement(first)->nextSibling);
	ASSERT_TRUE(ctx.IsAlive(second));
	UI::ElementHandle untouched = ctx.MakeHandle(ctx.GetElement(second)->firstChild);
	EXPECT_EQ(ctx.GetTextString(untouched), "Name");
	UI::ElementHandle untouchedButton = ctx.MakeHandle(ctx.GetElement(untouched)->nextSibling);
	EXPECT_EQ(ctx.GetTextString(ctx.MakeHandle(ctx.GetElement(untouchedButton)->firstChild)), "Default");
}

TEST_F(UITemplateFixture, InstancesIdentityLandsOnComposedRoot) {
	WriteDocument("project://Assets/Row.slxml", R"(<panel id="inner" class="row" style="width: 10px; height: 7px"/>)");
	WriteDocument("project://Assets/List.slxml",
				  R"(<template src="project://Assets/Row.slxml" name="Row"/>)"
				  R"(<panel><instance template="Row" id="first" class="selected" style="width: 40px"/></panel>)");

	UIDocumentAsset* list = Load("project://Assets/List.slxml");
	ASSERT_NE(list, nullptr);

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = list->Instantiate(ctx, ctx.GetRoot());
	UI::ElementHandle row = ctx.MakeHandle(ctx.GetElement(root)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(row));

	EXPECT_EQ(ctx.GetElement(row)->id, "first"_hash);
	EXPECT_TRUE(ctx.HasClass(row, "row"_hash));
	EXPECT_TRUE(ctx.HasClass(row, "selected"_hash));
	EXPECT_EQ(ctx.GetComputedStyle(row).width, UI::Length::Pixel(40.0f));
	EXPECT_EQ(ctx.GetComputedStyle(row).height, UI::Length::Pixel(7.0f));
}

TEST_F(UITemplateFixture, NestingPropagatesOverridesAndTheShallowestWins) {
	WriteDocument("project://Assets/Leaf.slxml", R"(<panel><label id="who">Leaf</label><button id="act" text="Leaf"/></panel>)");
	WriteDocument("project://Assets/Mid.slxml",
				  R"(<template src="project://Assets/Leaf.slxml" name="Leaf"/>)"
				  R"(<panel><instance template="Leaf"><attribute-overrides element-id="who">Mid</attribute-overrides>)"
				  R"(<attribute-overrides element-id="act" text="Mid"/></instance></panel>)");
	WriteDocument("project://Assets/Top.slxml",
				  R"(<template src="project://Assets/Mid.slxml" name="Mid"/>)"
				  R"(<panel><instance template="Mid"><attribute-overrides element-id="who">Top</attribute-overrides></instance></panel>)");

	UIDocumentAsset* top = Load("project://Assets/Top.slxml");
	ASSERT_NE(top, nullptr);

	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle root = top->Instantiate(ctx, ctx.GetRoot());
	UI::ElementHandle mid = ctx.MakeHandle(ctx.GetElement(root)->firstChild);
	UI::ElementHandle leaf = ctx.MakeHandle(ctx.GetElement(mid)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(leaf));

	UI::ElementHandle who = ctx.MakeHandle(ctx.GetElement(leaf)->firstChild);
	EXPECT_EQ(ctx.GetTextString(who), "Top");
	UI::ElementHandle act = ctx.MakeHandle(ctx.GetElement(who)->nextSibling);
	EXPECT_EQ(ctx.GetTextString(ctx.MakeHandle(ctx.GetElement(act)->firstChild)), "Mid");
}

TEST_F(UITemplateFixture, CycleIsRefusedAtImportNamingLoop) {
	WriteDocument("project://Assets/A.slxml", R"(<template src="project://Assets/B.slxml" name="B"/><panel><instance template="B"/></panel>)");
	WriteDocument("project://Assets/B.slxml", R"(<template src="project://Assets/A.slxml" name="A"/><panel><instance template="A"/></panel>)");

	EXPECT_EQ(Load("project://Assets/A.slxml"), nullptr);

	WriteDocument("project://Assets/Self.slxml", R"(<template src="project://Assets/Self.slxml" name="Me"/><panel><instance template="Me"/></panel>)");
	EXPECT_EQ(Load("project://Assets/Self.slxml"), nullptr);

	WriteDocument("project://Assets/Leaf.slxml", R"(<panel class="leaf"/>)");
	WriteDocument("project://Assets/Left.slxml", R"(<template src="project://Assets/Leaf.slxml" name="L"/><panel><instance template="L"/></panel>)");
	WriteDocument("project://Assets/Diamond.slxml",
				  R"(<template src="project://Assets/Left.slxml" name="Side"/><template src="project://Assets/Leaf.slxml" name="L"/>)"
				  R"(<panel><instance template="Side"/><instance template="L"/></panel>)");
	EXPECT_NE(Load("project://Assets/Diamond.slxml"), nullptr);
}

TEST_F(UITemplateFixture, NonDocumentTemplateRefused) {
	WriteDocument("project://Assets/Missing.slxml", R"(<template src="project://Assets/Nope.slxml" name="N"/><panel><instance template="N"/></panel>)");
	EXPECT_EQ(Load("project://Assets/Missing.slxml"), nullptr);

	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/note.txt")), "not a document");
	WriteDocument("project://Assets/WrongType.slxml", R"(<template src="project://Assets/note.txt" name="N"/><panel><instance template="N"/></panel>)");
	EXPECT_EQ(Load("project://Assets/WrongType.slxml"), nullptr);
}

TEST_F(UITemplateFixture, OverrideCheckedAgainstFinalTarget) {
	WriteDocument("project://Assets/Plate.slxml", R"(<panel><label id="who">Name</label><checkbox id="ready" value="false"/></panel>)");
	constexpr const char* DECLARE = R"(<template src="project://Assets/Plate.slxml" name="P"/>)";

	auto refused = [&](const str& overrides, const char* what) {
		WriteDocument("project://Assets/Case.slxml", str(DECLARE) + str("<panel><instance template=\"P\" id=\"probe\">") + overrides + str("</instance></panel>"));
		EXPECT_EQ(Load("project://Assets/Case.slxml"), nullptr) << "accepted " << what << ": " << overrides;
		AssetDatabase::RebuildDatabase();
	};

	refused(R"(<attribute-overrides element-id="nobody" value="true"/>)", "an id no element carries");
	refused(R"(<attribute-overrides element-id="ready" text="hi"/>)", "an attribute the target cannot carry");
	refused(R"(<attribute-overrides element-id="ready" value="maybe"/>)", "a literal outside the declared type");
	refused(R"(<attribute-overrides element-id="who" text="Alice"/>)", "an attribute on a plain element");
	refused(R"(<attribute-overrides element-id="ready">Alice</attribute-overrides>)", "text content aimed at a widget");

	WriteDocument("project://Assets/Good.slxml",
				  str(DECLARE) + str(R"(<panel><instance template="P"><attribute-overrides element-id="who">Alice</attribute-overrides>)") +
					  str(R"(<attribute-overrides element-id="ready" value="true"/></instance></panel>)"));
	EXPECT_NE(Load("project://Assets/Good.slxml"), nullptr);
}

TEST_F(UITemplateFixture, ClassCapIsCountedAcrossComposedElement) {
	WriteDocument("project://Assets/Two.slxml", R"(<panel class="x y"/>)");
	constexpr const char* DECLARE = R"(<template src="project://Assets/Two.slxml" name="T"/>)";

	WriteDocument("project://Assets/AtCap.slxml", str(DECLARE) + str(R"(<panel><instance template="T" class="a b"/></panel>)"));
	EXPECT_NE(Load("project://Assets/AtCap.slxml"), nullptr);

	WriteDocument("project://Assets/OverCap.slxml", str(DECLARE) + str(R"(<panel><instance template="T" class="a b c"/></panel>)"));
	EXPECT_EQ(Load("project://Assets/OverCap.slxml"), nullptr);

	WriteDocument("project://Assets/Widget.slxml", R"(<button class="x"/>)");
	WriteDocument("project://Assets/OverWidget.slxml",
				  str(R"(<template src="project://Assets/Widget.slxml" name="W"/>)") + str(R"(<panel><instance template="W" class="a b c"/></panel>)"));
	EXPECT_EQ(Load("project://Assets/OverWidget.slxml"), nullptr);
}

TEST_F(UITemplateFixture, UnloadingDocumentReleasesTemplatesItHeld) {
	WriteDocument("project://Assets/Leaf.slxml", R"(<panel class="leaf"/>)");
	WriteDocument("project://Assets/Host.slxml", R"(<template src="project://Assets/Leaf.slxml" name="L"/><panel><instance template="L"/></panel>)");

	UIDocumentAsset* host = Load("project://Assets/Host.slxml");
	ASSERT_NE(host, nullptr);

	Handle<UIDocumentAsset> leaf = host->templates[0].document;
	ASSERT_NE(leaf.Get(), nullptr);

	AssetDatabase::UnloadAsset(Load("project://Assets/Leaf.slxml"));
	EXPECT_NE(leaf.Get(), nullptr);

	AssetDatabase::UnloadAsset(host);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(leaf.Get(), nullptr);
}

TEST(UITemplateTest, InstantiateIsTotalOnHandBuiltCycle) {
	UIDocumentAsset* first = New<UIDocumentAsset>(MemTag::Asset);
	UIDocumentAsset* second = New<UIDocumentAsset>(MemTag::Asset);

	str error;
	ASSERT_TRUE(first->Parse(R"(<ui version="1"><template src="b" name="B"/><panel class="first"><instance template="B"/></panel></ui>)", error)) << error;
	ASSERT_TRUE(second->Parse(R"(<ui version="1"><template src="a" name="A"/><panel class="second"><instance template="A"/></panel></ui>)", error)) << error;
	first->templates[0].document = Handle<UIDocumentAsset>(second);
	second->templates[0].document = Handle<UIDocumentAsset>(first);

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = first->Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(root));
	EXPECT_TRUE(ctx.HasClass(root, "first"_hash));
	UI::ElementHandle inner = ctx.MakeHandle(ctx.GetElement(root)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(inner));
	EXPECT_TRUE(ctx.HasClass(inner, "second"_hash));
	EXPECT_EQ(ctx.GetElement(inner)->firstChild, UI::INVALID_UI_INDEX);

	Delete(first);
	Delete(second);
}
