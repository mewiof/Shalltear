#include "TestsPCH.h"

#include "UIStyleVocabulary.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/UIDocument.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"

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

		explicit Parsed(std::string_view source) {
			ok = document.Parse(source, error);
		}
	};

	void ExpectRefused(std::string_view source, std::string_view namedInMessage) {
		UIDocumentAsset document;
		str error;
		EXPECT_FALSE(document.Parse(source, error)) << "accepted: " << source;
		EXPECT_NE(error.find(str(namedInMessage)), str::npos) << namedInMessage << "': " << error;
		EXPECT_TRUE(document.root.tag.empty()) << error;
	}

	str Document(std::string_view body) {
		return str("<ui version=\"1\">") + str(body) + str("</ui>");
	}

} // anonymous namespace

TEST(UIDocumentTest, ExampleParsesToTree) {
	Parsed parsed(R"(<?xml version="1.0" encoding="UTF-8"?>
<ui version="1">
	<sheet src="project://UI/Main.slss"/>
	<panel id="hud-root" class="screen hud" style="flex-direction: column; padding: 8px 12px;">
		<label class="score">Score</label>
		<panel id="bar"/>
	</panel>
</ui>
)");
	ASSERT_TRUE(parsed.ok) << parsed.error;

	ASSERT_EQ(parsed.document.sheets.size(), 1u);
	EXPECT_EQ(parsed.document.sheets[0].src, "project://UI/Main.slss");
	EXPECT_FALSE(parsed.document.sheets[0].sheet.IsValid());

	const UIDocumentNode& root = parsed.document.root;
	EXPECT_EQ(root.tag, "panel");
	EXPECT_EQ(root.id, "hud-root");
	ASSERT_EQ(root.classCount, 2u);
	EXPECT_EQ(root.classes[0], "screen");
	EXPECT_EQ(root.classes[1], "hud");
	EXPECT_EQ(root.style, "flex-direction: column; padding: 8px 12px;");

	ASSERT_EQ(root.children.size(), 2u);
	EXPECT_EQ(root.children[0].tag, "label");
	EXPECT_EQ(root.children[0].text, "Score");
	EXPECT_EQ(root.children[1].id, "bar");
	EXPECT_TRUE(root.text.empty());
}

TEST(UIDocumentTest, DocumentNeedsVersionedRoot) {
	ExpectRefused(R"(<layout version="1"><panel/></layout>)", "layout");
	ExpectRefused(R"(<ui><panel/></ui>)", "version");
	ExpectRefused(R"(<ui version="2"><panel/></ui>)", "2");
	ExpectRefused(R"(<ui version="1" theme="dark"><panel/></ui>)", "theme");
	ExpectRefused(R"(<ui version="1"></ui>)", "ui");

	ExpectRefused(R"(<ui version="1"><panel id="a"/><panel id="b"/></ui>)", "b");
}

TEST(UIDocumentTest, ForeignVocabularyRefused) {
	ExpectRefused(Document(R"(<panel calss="row"/>)"), "calss");

	ExpectRefused(Document(R"(<panel text="Pause"/>)"), "text");
	ExpectRefused(Document(R"(<buton text="Pause"/>)"), "text");

	ExpectRefused(Document(R"(<panel><sheet src="a.slss"/></panel>)"), "belongs directly inside");
}

TEST(UIDocumentTest, OverCapListsRefused) {
	ExpectRefused(Document(R"(<panel class="a b c d e"/>)"), "panel");
	Parsed atCap(Document(R"(<panel class="a b c d"/>)"));
	ASSERT_TRUE(atCap.ok) << atCap.error;
	EXPECT_EQ(atCap.document.root.classCount, UI::MAX_ELEMENT_CLASSES);

	constexpr const char* SEVEN =
		"box-shadow: 1px 1px 0 0 #111111, 2px 2px 0 0 #222222, 3px 3px 0 0 #333333, "
		"4px 4px 0 0 #444444, 5px 5px 0 0 #555555, 6px 6px 0 0 #666666, 7px 7px 0 0 #777777";
	ExpectRefused(Document(str("<panel style=\"") + str(SEVEN) + str("\"/>")), "panel");

	constexpr const char* SIX =
		"box-shadow: 1px 1px 0 0 #111111, 2px 2px 0 0 #222222, 3px 3px 0 0 #333333, "
		"4px 4px 0 0 #444444, 5px 5px 0 0 #555555, 6px 6px 0 0 #666666";
	Parsed six(Document(str("<panel style=\"") + str(SIX) + str("\"/>")));
	EXPECT_TRUE(six.ok) << six.error;

	ExpectRefused(Document(str("<panel style=\"width: 10px; ") + str(SEVEN) + str("; opacity: 0.5\"/>")), "panel");
}

TEST(UIDocumentTest, XMLRefusalsReachCallerCarryingName) {
	ExpectRefused(R"(<ui version="1" xmlns="http://x"><panel/></ui>)", "xmlns");
	ExpectRefused(R"(<ui version="1"><engine:Button/></ui>)", "engine:Button");
	ExpectRefused(R"(<ui version="1"><p>Hello <b>x</b></p></ui>)", "p");
}

TEST(UIDocumentTest, EverySpellablePropertyRoundTripsByteIdentically) {
	str fragment = StyleVocabulary::EveryDeclaration();
	ASSERT_FALSE(fragment.empty());

	Parsed first(Document(str("<panel id=\"root\" class=\"a b\" style=\"") + fragment + str("\"><label>Score</label></panel>")));
	ASSERT_TRUE(first.ok) << first.error;
	EXPECT_EQ(first.document.root.style, fragment);

	str written = first.document.Write();
	ASSERT_FALSE(written.empty());

	UIDocumentAsset reread;
	str error;
	ASSERT_TRUE(reread.Parse(written, error)) << error << ' ' << written;

	EXPECT_EQ(reread.Write(), written);
	EXPECT_EQ(reread.root.style, fragment);
}

TEST(UIDocumentTest, WriterSpellingsConverge) {
	Parsed expanded(R"(<ui version="1"><panel></panel></ui>)");
	ASSERT_TRUE(expanded.ok) << expanded.error;
	Parsed collapsed(R"(<ui version="1"><panel/></ui>)");
	ASSERT_TRUE(collapsed.ok) << collapsed.error;
	EXPECT_EQ(expanded.document.Write(), collapsed.document.Write());

	UIDocumentAsset empty;
	EXPECT_TRUE(empty.Write().empty());
}

TEST(UIDocumentTest, EscapedTextSurvivesWriter) {
	Parsed parsed(Document(R"(<label id="a&amp;b" style="width: 10px">5 &lt; 6 &amp; 7 &gt; 2</label>)"));
	ASSERT_TRUE(parsed.ok) << parsed.error;
	EXPECT_EQ(parsed.document.root.id, "a&b");
	EXPECT_EQ(parsed.document.root.text, "5 < 6 & 7 > 2");

	str written = parsed.document.Write();
	UIDocumentAsset reread;
	str error;
	ASSERT_TRUE(reread.Parse(written, error)) << error;
	EXPECT_EQ(reread.root.id, "a&b");
	EXPECT_EQ(reread.root.text, "5 < 6 & 7 > 2");
	EXPECT_EQ(reread.Write(), written);
}

TEST(UIDocumentTest, InstantiateReachesComputedStyleForEveryProperty) {
	Parsed parsed(Document(str("<panel style=\"") + StyleVocabulary::EveryDeclaration() + str("\"/>")));
	ASSERT_TRUE(parsed.ok) << parsed.error;

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle element = parsed.document.Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(element));

	EXPECT_EQ(ctx.GetComputedStyle(element), StyleVocabulary::EveryDeclarationAsStyle());
}

TEST(UIDocumentTest, InstantiateBuildsTreeWithIdentityAndText) {
	Parsed parsed(Document(R"(<panel id="root" class="screen hud"><label id="score">Score</label><panel/></panel>)"));
	ASSERT_TRUE(parsed.ok) << parsed.error;

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = parsed.document.Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(root));

	const UI::Element* element = ctx.GetElement(root);
	ASSERT_NE(element, nullptr);
	EXPECT_EQ(element->id, "root"_hash);
	EXPECT_EQ(element->typeTag, "panel"_hash);
	EXPECT_TRUE(ctx.HasClass(root, "screen"_hash));
	EXPECT_TRUE(ctx.HasClass(root, "hud"_hash));

	ASSERT_NE(element->firstChild, UI::INVALID_UI_INDEX);
	UI::ElementHandle label = ctx.MakeHandle(element->firstChild);
	EXPECT_EQ(ctx.GetTextString(label), "Score");
	EXPECT_EQ(ctx.GetElement(label)->typeTag, "label"_hash);

	u32 children = 0;
	for (u32 i = element->firstChild; i != UI::INVALID_UI_INDEX; i = ctx.GetElement(ctx.MakeHandle(i))->nextSibling) {
		++children;
	}
	EXPECT_EQ(children, 2u);
}

TEST(UIDocumentTest, SheetRulesReachElements) {
	Parsed parsed(Document(R"(<panel id="root" class="card"><label/></panel>)"));
	ASSERT_TRUE(parsed.ok) << parsed.error;

	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(R"css(
		label { opacity: 0.25; }
		.card { width: 40px; }
		#root { height: 20px; }
	)css");

	UI::ElementHandle root = parsed.document.Instantiate(ctx, ctx.GetRoot());
	EXPECT_EQ(ctx.GetComputedStyle(root).width, UI::Length::Pixel(40.0f));
	EXPECT_EQ(ctx.GetComputedStyle(root).height, UI::Length::Pixel(20.0f));

	UI::ElementHandle label = ctx.MakeHandle(ctx.GetElement(root)->firstChild);
	EXPECT_NEAR(ctx.GetComputedStyle(label).opacity, 0.25f, 0.001f);
}

TEST(UIDocumentTest, VarReferencesResolveAtLoad) {
	Parsed parsed(Document(R"xml(<panel style="background-color: var(--probe-fill)"/>)xml"));
	ASSERT_TRUE(parsed.ok) << parsed.error;
	EXPECT_NE(parsed.document.root.style.find("var(--probe-fill)"), str::npos);

	UI::Context ctx(Desc(), nullptr);
	ctx.SetStyleVar("--probe-fill"_hash, "#112233");
	UI::ElementHandle element = parsed.document.Instantiate(ctx, ctx.GetRoot());
	EXPECT_EQ(ctx.GetComputedStyle(element).background.solid, Color::FromHex("#112233"));

	ctx.SetStyleVar("--probe-fill"_hash, "#445566");
	UI::ElementHandle second = parsed.document.Instantiate(ctx, ctx.GetRoot());
	EXPECT_EQ(ctx.GetComputedStyle(second).background.solid, Color::FromHex("#445566"));
}

TEST(UIDocumentTest, DocumentsInstanceAndShareContext) {
	Parsed row(Document(R"(<panel class="row" style="height: 12px"><label>cell</label></panel>)"));
	ASSERT_TRUE(row.ok) << row.error;
	Parsed header(Document(R"(<panel class="header" style="height: 30px"/>)"));
	ASSERT_TRUE(header.ok) << header.error;

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle list = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), list);

	header.document.Instantiate(ctx, list);
	UI::ElementHandle handles[20];
	for (UI::ElementHandle& handle : handles) {
		handle = row.document.Instantiate(ctx, list);
	}

	for (u32 i = 0; i < 20; ++i) {
		ASSERT_TRUE(ctx.IsAlive(handles[i])) << "instance " << i;
		EXPECT_EQ(ctx.GetComputedStyle(handles[i]).height, UI::Length::Pixel(12.0f)) << "instance " << i;
		for (u32 j = 0; j < i; ++j) {
			EXPECT_NE(handles[i], handles[j]) << "instances " << i << " and " << j;
		}
	}
	EXPECT_EQ(ctx.GetLiveCount(), 42u);

	ctx.DestroyElement(handles[0]);
	EXPECT_FALSE(ctx.IsAlive(handles[0]));
	EXPECT_TRUE(ctx.IsAlive(handles[1]));
}

TEST(UIDocumentTest, EscapingInlineStyleIgnored) {
	Parsed parsed(Document(R"(<panel style="width: 1px; } .other { height: 99px"/>)"));
	ASSERT_TRUE(parsed.ok) << parsed.error;

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle victim = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), victim);
	ctx.AddClass(victim, "other"_hash);

	UI::ElementHandle element = parsed.document.Instantiate(ctx, ctx.GetRoot());
	EXPECT_EQ(ctx.GetComputedStyle(element).width, UI::Length::Auto());
	EXPECT_EQ(ctx.GetComputedStyle(victim).height, UI::Length::Auto());
}

TEST(UIDocumentTest, RegisteredTagInstantiatesAsRealWidget) {
	Parsed parsed(Document(R"(<panel><button id="pause" class="wide" text="Pause"/></panel>)"));
	ASSERT_TRUE(parsed.ok) << parsed.error;

	const UIDocumentNode& button = parsed.document.root.children[0];
	ASSERT_EQ(button.attributes.size(), 1u);
	EXPECT_EQ(button.attributes[0].name, "text");
	EXPECT_EQ(button.attributes[0].value, "Pause");

	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle root = parsed.document.Instantiate(ctx, ctx.GetRoot());
	UI::ElementHandle element = ctx.MakeHandle(ctx.GetElement(root)->firstChild);

	EXPECT_TRUE(ctx.HasClass(element, "slt-button"_hash));
	EXPECT_EQ(ctx.GetElement(element)->id, "pause"_hash);
	EXPECT_TRUE(ctx.HasClass(element, "wide"_hash));
	EXPECT_EQ(ctx.GetElement(element)->typeTag, "button"_hash);

	UI::ElementHandle label = ctx.MakeHandle(ctx.GetElement(element)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(label));
	EXPECT_EQ(ctx.GetTextString(label), "Pause");
}

TEST(UIDocumentTest, ContentParentGetsChildren) {
	Parsed parsed(Document(R"(<scroll-view><panel id="content"/></scroll-view>)"));
	ASSERT_TRUE(parsed.ok) << parsed.error;

	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle root = parsed.document.Instantiate(ctx, ctx.GetRoot());
	ASSERT_TRUE(ctx.IsAlive(root));
	EXPECT_TRUE(ctx.HasClass(root, "slt-scroll-root"_hash));

	UI::ElementHandle viewport = ctx.MakeHandle(ctx.GetElement(root)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(viewport));
	EXPECT_TRUE(ctx.HasClass(viewport, "slt-scroll-viewport"_hash));
	UI::ElementHandle content = ctx.MakeHandle(ctx.GetElement(viewport)->firstChild);
	ASSERT_TRUE(ctx.IsAlive(content));
	EXPECT_EQ(ctx.GetElement(content)->id, "content"_hash);
}

TEST(UIDocumentTest, WidgetsAttributeVocabularyIsWhatDescDeclares) {
	Parsed checkbox(Document(R"(<checkbox value="true"/>)"));
	EXPECT_TRUE(checkbox.ok) << checkbox.error;
	Parsed slider(Document(R"(<slider min="0" max="10" value="2.5"/>)"));
	EXPECT_TRUE(slider.ok) << slider.error;

	ExpectRefused(Document(R"(<checkbox text="Pause"/>)"), "text");
	ExpectRefused(Document(R"(<button value="true"/>)"), "value");
	ExpectRefused(Document(R"(<button id="pause" onclick="Fire"/>)"), "pause");

	ExpectRefused(Document(R"(<button text="a" on-click="Fire"/>)"), "on-click");
}

TEST(UIDocumentTest, BadAttributeValueRefused) {
	ExpectRefused(Document(R"(<checkbox id="mute" value="maybe"/>)"), "mute");
	ExpectRefused(Document(R"(<checkbox value="True"/>)"), "True");
	ExpectRefused(Document(R"(<slider min="0" max="ten"/>)"), "ten");
	ExpectRefused(Document(R"(<slider min="12px"/>)"), "12px");

	Parsed blank(Document(R"(<button text=""/>)"));
	EXPECT_TRUE(blank.ok) << blank.error;

	Parsed bare(Document(R"(<slider/>)"));
	EXPECT_TRUE(bare.ok) << bare.error;
}

TEST(UIDocumentTest, WidgetsClassesCountAgainstAuthorsCap) {
	Parsed atCap(Document(R"(<panel class="a b c d"/>)"));
	ASSERT_TRUE(atCap.ok) << atCap.error;

	ExpectRefused(Document(R"(<button id="pause" class="a b c d"/>)"), "pause");
	UIDocumentAsset document;
	str error;
	ASSERT_FALSE(document.Parse(Document(R"(<button class="a b c d"/>)"), error));
	EXPECT_NE(error.find("slt-button"), str::npos) << error;

	Parsed three(Document(R"(<button class="a b c"/>)"));
	ASSERT_TRUE(three.ok) << three.error;
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle element = three.document.Instantiate(ctx, ctx.GetRoot());
	for (const char* klass : {"a", "b", "c"}) {
		EXPECT_TRUE(ctx.HasClass(element, StringHash(klass))) << "class " << klass;
	}
	EXPECT_TRUE(ctx.HasClass(element, "slt-button"_hash));
}

TEST(UIDocumentTest, AWidgetElementCannotCarryTextContent) {
	ExpectRefused(Document(R"(<button id="pause">Pause</button>)"), "pause");
	Parsed label(Document(R"(<label>Score</label>)"));
	EXPECT_TRUE(label.ok) << label.error;
}

TEST(UIDocumentTest, WidgetAttributesRoundTripInDeclaredOrder) {
	Parsed parsed(Document(R"(<panel><slider value="2.5" min="0" max="10"/><button text="Pause"/></panel>)"));
	ASSERT_TRUE(parsed.ok) << parsed.error;

	const UIDocumentNode& slider = parsed.document.root.children[0];
	ASSERT_EQ(slider.attributes.size(), 3u);
	EXPECT_EQ(slider.attributes[0].name, "min");
	EXPECT_EQ(slider.attributes[1].name, "max");
	EXPECT_EQ(slider.attributes[2].name, "value");

	str written = parsed.document.Write();
	EXPECT_NE(written.find(R"(<slider min="0" max="10" value="2.5"/>)"), str::npos) << written;

	UIDocumentAsset reread;
	str error;
	ASSERT_TRUE(reread.Parse(written, error)) << error << ' ' << written;
	EXPECT_EQ(reread.Write(), written);
	ASSERT_EQ(reread.root.children[0].attributes.size(), 3u);
	EXPECT_EQ(reread.root.children[0].attributes[2].value, "2.5");
}

TEST(UIDocumentTest, SkinBeatsInlinePerProperty) {
	Parsed styled(Document(R"(<button style="background-color: #ff0000; width: 33px" text="x"/>)"));
	ASSERT_TRUE(styled.ok) << styled.error;
	Parsed plain(Document(R"(<button text="x"/>)"));
	ASSERT_TRUE(plain.ok) << plain.error;

	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle authored = styled.document.Instantiate(ctx, ctx.GetRoot());
	UI::ElementHandle untouched = plain.document.Instantiate(ctx, ctx.GetRoot());

	EXPECT_EQ(ctx.GetComputedStyle(authored).background.solid, ctx.GetComputedStyle(untouched).background.solid);
	EXPECT_NE(ctx.GetComputedStyle(authored).background.solid, Color::FromHex("#ff0000"));

	EXPECT_EQ(ctx.GetComputedStyle(authored).width, UI::Length::Pixel(33.0f));
	EXPECT_EQ(ctx.GetComputedStyle(untouched).width, UI::Length::Auto());
}

TEST(UIDocumentTest, ImporterIsRegisteredForDocumentExtension) {
	AssetDatabase::Initialize();
	IAssetImporter* byExtension = AssetImporterRegistry::FindByExtension(".slxml");
	ASSERT_NE(byExtension, nullptr);
	EXPECT_EQ(byExtension->GetAssetType(), StringHash("UIDocumentAsset"));

	Asset* created = byExtension->CreateAsset();
	ASSERT_NE(created, nullptr);
	UIDocumentAsset* document = static_cast<UIDocumentAsset*>(created);
	str written = document->Write();
	Delete(created);

	UIDocumentAsset reloaded;
	str error;
	EXPECT_TRUE(reloaded.Parse(written, error)) << error;
}
