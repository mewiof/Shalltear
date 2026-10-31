#include "TestsPCH.h"

#include "Serialization/XML.h"

using namespace Shalltear;

namespace {

	constexpr size_t ARENA_BYTES = 64 * 1024;

	constexpr const char* DOCUMENT = R"(<?xml version="1.0" encoding="UTF-8"?>
<ui version="1">
	<sheet src="project://UI/Main.slss"/>
	<panel id="hud-root" class="screen hud" style="flex-direction: column; padding: 8px 12px;">
		<label class="score">Score</label>
		<button id="pause" text="Pause"/>
	</panel>
</ui>
)";

	struct Parsed {
		LinearAllocator arena{ARENA_BYTES, MemTag::Core};
		XML::XMLError error;
		XML::XMLNode root;

		explicit Parsed(std::string_view text) {
			root = XML::Parse(text, arena, error);
		}
	};

	void ExpectRefused(std::string_view text, XML::XMLError::Code code, std::string_view namedInDetail) {
		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		XML::XMLError error;
		XML::XMLNode root = XML::Parse(text, arena, error);

		EXPECT_FALSE(root.IsValid()) << "accepted: " << text;
		ASSERT_TRUE(error.Failed()) << "accepted: " << text;
		EXPECT_EQ(static_cast<int>(error.code), static_cast<int>(code)) << error.Describe();
		EXPECT_NE(error.detail.find(str(namedInDetail)), str::npos) << namedInDetail << "': " << error.Describe();
	}

} // anonymous namespace

TEST(XMLTest, ParsesTheLayoutDocumentShape) {
	Parsed doc(DOCUMENT);
	ASSERT_FALSE(doc.error.Failed()) << doc.error.Describe();
	ASSERT_TRUE(doc.root.IsValid());

	EXPECT_EQ(doc.root.name, "ui");
	EXPECT_EQ(doc.root.Attribute("version"), "1");
	ASSERT_EQ(doc.root.children.size(), 2u);

	const XML::XMLNode& sheet = doc.root.children[0];
	EXPECT_EQ(sheet.name, "sheet");
	EXPECT_EQ(sheet.Attribute("src"), "project://UI/Main.slss");
	EXPECT_TRUE(sheet.children.empty());

	const XML::XMLNode& panel = doc.root.children[1];
	EXPECT_EQ(panel.name, "panel");
	EXPECT_EQ(panel.Attribute("id"), "hud-root");
	EXPECT_EQ(panel.Attribute("class"), "screen hud");
	EXPECT_EQ(panel.Attribute("style"), "flex-direction: column; padding: 8px 12px;");
	ASSERT_EQ(panel.children.size(), 2u);

	EXPECT_EQ(panel.children[0].text, "Score");
	EXPECT_TRUE(panel.text.empty());
	EXPECT_TRUE(doc.root.text.empty());

	EXPECT_EQ(panel.Attribute("nope", "fallback"), "fallback");
	EXPECT_EQ(panel.FindAttribute("nope"), nullptr);
}

TEST(XMLTest, AcceptsBothQuoteStylesAndAbsentDeclaration) {
	Parsed doc(R"(<a one='single' two="double"/>)");
	ASSERT_FALSE(doc.error.Failed()) << doc.error.Describe();
	EXPECT_EQ(doc.root.Attribute("one"), "single");
	EXPECT_EQ(doc.root.Attribute("two"), "double");
}

TEST(XMLTest, ExpandsPredefinedEntities) {
	Parsed doc(R"(<a v="&lt;&gt;&amp;&apos;&quot;">&#65;&#x42;&#x263A;</a>)");
	ASSERT_FALSE(doc.error.Failed()) << doc.error.Describe();
	EXPECT_EQ(doc.root.Attribute("v"), "<>&'\"");
	EXPECT_EQ(doc.root.text, "AB\xE2\x98\xBA");
}

TEST(XMLTest, CommentsAreSkippedEverywhereSubsetAllowsThem) {
	Parsed doc("<!-- before -->\n<a><!--inside--><b/><!--between--><c/></a>\n<!-- after -->");
	ASSERT_FALSE(doc.error.Failed()) << doc.error.Describe();
	ASSERT_EQ(doc.root.children.size(), 2u);
	EXPECT_EQ(doc.root.children[0].name, "b");
	EXPECT_EQ(doc.root.children[1].name, "c");
}

TEST(XMLTest, CommentSplittingTextRunJoinsNotCorrupts) {
	Parsed doc("<a>foo<!--c-->bar</a>");
	ASSERT_FALSE(doc.error.Failed()) << doc.error.Describe();
	EXPECT_EQ(doc.root.text, "foobar");
}

TEST(XMLTest, EmptyElementAndEmptyPairAgree) {
	Parsed selfClosed("<a/>");
	Parsed pair("<a></a>");
	ASSERT_FALSE(selfClosed.error.Failed()) << selfClosed.error.Describe();
	ASSERT_FALSE(pair.error.Failed()) << pair.error.Describe();
	EXPECT_TRUE(selfClosed.root.text.empty());
	EXPECT_TRUE(pair.root.text.empty());
	EXPECT_TRUE(selfClosed.root.children.empty());
	EXPECT_TRUE(pair.root.children.empty());
}

TEST(XMLTest, ByteOrderMarkDoesNotHideDeclaration) {
	Parsed doc("\xEF\xBB\xBF<?xml version=\"1.0\"?><a/>");
	ASSERT_FALSE(doc.error.Failed()) << doc.error.Describe();
	EXPECT_EQ(doc.root.name, "a");
}

TEST(XMLTest, AttributeWhitespaceNormalizesButReferencesSurvive) {
	Parsed doc("<a literal=\"x\ty\nz\" escaped=\"x&#9;y&#10;z\"/>");
	ASSERT_FALSE(doc.error.Failed()) << doc.error.Describe();
	EXPECT_EQ(doc.root.Attribute("literal"), "x y z");
	EXPECT_EQ(doc.root.Attribute("escaped"), "x\ty\nz");
}

TEST(XMLTest, TextLineEndingsNormalizeToLf) {
	Parsed doc("<a>one\r\ntwo\rthree</a>");
	ASSERT_FALSE(doc.error.Failed()) << doc.error.Describe();
	EXPECT_EQ(doc.root.text, "one\ntwo\nthree");
}

TEST(XMLTest, NestingUpToTheCapIsAccepted) {
	str source;
	for (u32 i = 0; i < XML::SLT_XML_MAX_DEPTH; ++i) {
		source += "<n>";
	}
	source += "leaf";
	for (u32 i = 0; i < XML::SLT_XML_MAX_DEPTH; ++i) {
		source += "</n>";
	}

	Parsed doc(source);
	ASSERT_FALSE(doc.error.Failed()) << doc.error.Describe();
	const XML::XMLNode* node = &doc.root;
	for (u32 i = 1; i < XML::SLT_XML_MAX_DEPTH; ++i) {
		ASSERT_EQ(node->children.size(), 1u) << "at depth " << i;
		node = &node->children[0];
	}
	EXPECT_EQ(node->text, "leaf");
}

TEST(XMLTest, EveryViewPointsIntoSourceOrArena) {
	LinearAllocator arena(ARENA_BYTES, MemTag::Core);
	const u8* arenaBase = static_cast<const u8*>(arena.Allocate(1, 1));
	std::string_view source = R"(<a v="&amp;x"><b>plain</b><c>a&#65;b</c></a>)";

	XML::XMLError error;
	XML::XMLNode root = XML::Parse(source, arena, error);
	ASSERT_FALSE(error.Failed()) << error.Describe();

	const u8* srcBase = reinterpret_cast<const u8*>(source.data());
	auto owned = [&](std::string_view view) {
		if (view.empty()) {
			return true;
		}
		const u8* p = reinterpret_cast<const u8*>(view.data());
		bool inSource = p >= srcBase && p + view.size() <= srcBase + source.size();
		bool inArena = p >= arenaBase && p + view.size() <= arenaBase + ARENA_BYTES;
		return inSource || inArena;
	};

	EXPECT_TRUE(owned(root.name));
	EXPECT_TRUE(owned(root.Attribute("v")));
	for (const XML::XMLNode& child : root.children) {
		EXPECT_TRUE(owned(child.name));
		EXPECT_TRUE(owned(child.text));
	}
}

TEST(XMLTest, PrefixedNamesAreRefusedByName) {
	using Code = XML::XMLError::Code;
	ExpectRefused("<engine:Button/>", Code::Namespace, "engine:Button");
	ExpectRefused("<a b:c=\"1\"/>", Code::Namespace, "b:c");
	ExpectRefused("<a><b:c/></a>", Code::Namespace, "b:c");
	ExpectRefused("<a xlink:href=\"#x\"/>", Code::Namespace, "xlink:href");
}

TEST(XMLTest, NamespaceDeclarationsAreAcceptedAndIgnored) {
	LinearAllocator arena(1024, MemTag::Asset);
	XML::XMLError error;
	XML::XMLNode root = XML::Parse("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:x=\"http://x\" id=\"m\"/>", arena, error);
	ASSERT_FALSE(error.Failed()) << error.Describe();
	EXPECT_EQ(root.name, "svg");
	EXPECT_EQ(root.Attribute("xmlns"), "http://www.w3.org/2000/svg");
	EXPECT_EQ(root.Attribute("xmlns:x"), "http://x");
	EXPECT_EQ(root.Attribute("id"), "m");

	XML::XMLError refused;
	XML::XMLNode refusedRoot = XML::Parse("<svg xmlns=\"http://www.w3.org/2000/svg\"><x:g/></svg>", arena, refused);
	EXPECT_FALSE(refusedRoot.IsValid());
	EXPECT_EQ(refused.code, XML::XMLError::Code::Namespace);
}

TEST(XMLTest, DocTypesRefused) {
	using Code = XML::XMLError::Code;
	ExpectRefused("<!DOCTYPE ui SYSTEM \"ui.dtd\"><a/>", Code::DocType, "DOCTYPE");
	ExpectRefused("<!ENTITY x \"y\"><a/>", Code::DocType, "ENTITY");
	ExpectRefused("<a><!DOCTYPE x></a>", Code::DocType, "DOCTYPE");
}

TEST(XMLTest, ProcessingInstructionsRefused) {
	using Code = XML::XMLError::Code;
	ExpectRefused("<?php echo 1; ?><a/>", Code::ProcessingInstruction, "php");
	ExpectRefused("<a><?target data?></a>", Code::ProcessingInstruction, "target");
	ExpectRefused("<?xmlfoo?><a/>", Code::ProcessingInstruction, "xmlfoo");
}

TEST(XMLTest, CdataSectionsAreRefusedByName) {
	using Code = XML::XMLError::Code;
	ExpectRefused("<a><![CDATA[raw <b> text]]></a>", Code::CdataSection, "CDATA");
	ExpectRefused("<![CDATA[x]]><a/>", Code::CdataSection, "CDATA");
}

TEST(XMLTest, MixedContentRefused) {
	using Code = XML::XMLError::Code;
	ExpectRefused("<p>Hello <b>world</b></p>", Code::MixedContent, "p");
	ExpectRefused("<p><b>world</b> trailing</p>", Code::MixedContent, "p");
	ExpectRefused("<p><b/>x<c/></p>", Code::MixedContent, "p");

	Parsed indented("<p>\n\t<b/>\n\t<c/>\n</p>");
	EXPECT_FALSE(indented.error.Failed()) << indented.error.Describe();
}

TEST(XMLTest, UnterminatedConstructsAreRefusedByName) {
	using Code = XML::XMLError::Code;
	ExpectRefused("<panel id=\"x\"", Code::UnterminatedTag, "panel");
	ExpectRefused("<panel>text", Code::UnterminatedTag, "panel");
	ExpectRefused("<a v=\"unclosed", Code::UnterminatedTag, "v");
	ExpectRefused("<a><!-- never closed </a>", Code::UnterminatedTag, "<!--");
	ExpectRefused("<?xml version=\"1.0\"<a/>", Code::UnterminatedTag, "<?xml");
}

TEST(XMLTest, MismatchedCloseNamed) {
	ExpectRefused("<a></b>", XML::XMLError::Code::MismatchedTag, "b");
}

TEST(XMLTest, DuplicateAttributeRefused) {
	ExpectRefused("<a id=\"one\" id=\"two\"/>", XML::XMLError::Code::DuplicateAttribute, "id");
}

TEST(XMLTest, ReferencesOutsideSubsetAreRefusedByName) {
	using Code = XML::XMLError::Code;
	ExpectRefused("<a>&nbsp;</a>", Code::BadReference, "nbsp");
	ExpectRefused("<a v=\"&copy;\"/>", Code::BadReference, "copy");
	ExpectRefused("<a>rock & roll</a>", Code::BadReference, "&");
	ExpectRefused("<a>&#;</a>", Code::BadReference, "#");
	ExpectRefused("<a>&#xZZ;</a>", Code::BadReference, "#xZZ");
	ExpectRefused("<a>&#x110000;</a>", Code::BadReference, "#x110000");
}

TEST(XMLTest, IllegalCharactersRefused) {
	using Code = XML::XMLError::Code;
	ExpectRefused("<a>bad\x01here</a>", Code::IllegalCharacter, "bad");
	ExpectRefused("<a v=\"bad\x01here\"/>", Code::IllegalCharacter, "bad");
	ExpectRefused("<a>&#1;</a>", Code::IllegalCharacter, "#1");
	ExpectRefused("<a>&#xB;</a>", Code::IllegalCharacter, "#xB");
}

TEST(XMLTest, NestingPastCapIsRefusedNotRecursed) {
	str source;
	for (u32 i = 0; i < XML::SLT_XML_MAX_DEPTH + 8; ++i) {
		source += "<n>";
	}
	LinearAllocator arena(ARENA_BYTES, MemTag::Core);
	XML::XMLError error;
	XML::XMLNode root = XML::Parse(source, arena, error);
	EXPECT_FALSE(root.IsValid());
	ASSERT_TRUE(error.Failed());
	EXPECT_EQ(static_cast<int>(error.code), static_cast<int>(XML::XMLError::Code::TooDeep)) << error.Describe();
}

TEST(XMLTest, SingleRootRequired) {
	using Code = XML::XMLError::Code;
	LinearAllocator arena(ARENA_BYTES, MemTag::Core);
	XML::XMLError error;

	XML::XMLNode empty = XML::Parse("", arena, error);
	EXPECT_FALSE(empty.IsValid());
	EXPECT_EQ(static_cast<int>(error.code), static_cast<int>(Code::NoRoot)) << error.Describe();

	XML::XMLNode commentOnly = XML::Parse("<!-- only a comment -->", arena, error);
	EXPECT_FALSE(commentOnly.IsValid());
	EXPECT_EQ(static_cast<int>(error.code), static_cast<int>(Code::NoRoot)) << error.Describe();

	ExpectRefused("<a/><b/>", Code::TrailingContent, "<b/>");
	ExpectRefused("<a/>junk", Code::TrailingContent, "junk");
}

TEST(XMLTest, HtmlLeniencyIsNotInheritedAnywhere) {
	using Code = XML::XMLError::Code;
	ExpectRefused("<a><b></a>", Code::MismatchedTag, "a");
	ExpectRefused("<a checked/>", Code::Syntax, "checked");
	ExpectRefused("<a v=unquoted/>", Code::Syntax, "v");
	ExpectRefused("<a b=\"1\"c=\"2\"/>", Code::Syntax, "a");
	ExpectRefused("<1bad/>", Code::Name, "1bad");
}

TEST(XMLTest, ErrorSentenceCarriesLineAndName) {
	LinearAllocator arena(ARENA_BYTES, MemTag::Core);
	XML::XMLError error;
	XML::XMLNode refusedRoot = XML::Parse("<ui>\n\t<panel/>\n\t<engine:Button/>\n</ui>", arena, error);

	EXPECT_FALSE(refusedRoot.IsValid());
	ASSERT_TRUE(error.Failed());
	EXPECT_EQ(error.line, 3u) << error.Describe();
	str described = error.Describe();
	EXPECT_NE(described.find("line 3"), str::npos) << described;
	EXPECT_NE(described.find("namespace"), str::npos) << described;
	EXPECT_NE(described.find("engine:Button"), str::npos) << described;
}

TEST(XMLTest, WriteReadWriteStable) {
	Parsed first(DOCUMENT);
	ASSERT_FALSE(first.error.Failed()) << first.error.Describe();

	str once = XML::Write(first.root);
	Parsed second(once);
	ASSERT_FALSE(second.error.Failed()) << second.error.Describe() << "\nwrote: " << once;

	str twice = XML::Write(second.root);
	EXPECT_EQ(once, twice) << "first:\n"
						   << once << "\nsecond:\n"
						   << twice;

	str compact = XML::Write(first.root, false);
	Parsed third(compact);
	ASSERT_FALSE(third.error.Failed()) << third.error.Describe() << "\nwrote: " << compact;
	EXPECT_EQ(compact, XML::Write(third.root, false));
}

TEST(XMLTest, EverythingTheWriterEscapesSurvivesTheReader) {
	Parsed source("<a text=\"&lt;&gt;&amp;&quot;&apos;&#9;&#10;&#13; plain\">&lt;&gt;&amp;&#13;\nplain</a>");
	ASSERT_FALSE(source.error.Failed()) << source.error.Describe();

	str written = XML::Write(source.root);
	Parsed reread(written);
	ASSERT_FALSE(reread.error.Failed()) << reread.error.Describe() << "\nwrote: " << written;

	EXPECT_EQ(reread.root.Attribute("text"), source.root.Attribute("text")) << "wrote: " << written;
	EXPECT_EQ(reread.root.text, source.root.text) << "wrote: " << written;
}

TEST(XMLTest, UnholdableCharacterDegrades) {
	XML::XMLNode root;
	root.name = "a";
	root.attributes.push_back({"keep", "intact"});
	root.text = std::string_view("bad\x01here", 8);

	str written = XML::Write(root);
	Parsed reread(written);
	ASSERT_FALSE(reread.error.Failed()) << reread.error.Describe() << "\nwrote: " << written;

	EXPECT_EQ(reread.root.Attribute("keep"), "intact");
	EXPECT_EQ(reread.root.text, "bad\xEF\xBF\xBD"
								"here");
}

TEST(XMLTest, InvalidNodeWritesNothingNotStubDocument) {
	EXPECT_TRUE(XML::Write(XML::XMLNode{}).empty());
}
