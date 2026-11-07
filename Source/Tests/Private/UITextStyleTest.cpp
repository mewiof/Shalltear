#include "TestsPCH.h"

#include "UIStyleVocabulary.h"
#include "UITestUtil.h"

#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/UIDocument.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	class SizedFont final : public UI::IFontSystem {
	public:
		u32 walks = 0;
		Handle<FontAsset> lastFont;
		f32 lastSize = 0.0f;

		bool ShapeText(const UI::TextQuery& query, UI::ShapedRun& out) override {
			Note(query);
			out.glyphs.clear();
			out.outlineGlyphs.clear();
			out.caretStops.clear();
			out.caretStops.push_back({0, 0.0f, 0});
			out.extent = Extent(query);
			out.lineHeight = query.sizePx;
			return true;
		}

		Vec2 MeasureText(const UI::TextQuery& query) override {
			Note(query);
			return Extent(query);
		}

		f32 GetLineHeight(Handle<FontAsset>, f32 sizePx) override { return sizePx; }
	private:
		void Note(const UI::TextQuery& query) {
			++walks;
			lastFont = query.font;
			lastSize = query.sizePx;
		}

		static Vec2 Extent(const UI::TextQuery& query) {
			return {static_cast<f32>(query.utf8.size()) * query.sizePx * 0.5f, query.sizePx};
		}
	};

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {800, 600};
		desc.contentScale = 1.0f;
		return desc;
	}

	UI::ElementHandle Child(UI::Context& ctx, UI::ElementHandle parent) {
		UI::ElementHandle element = ctx.CreateElement();
		ctx.AppendChild(parent, element);
		return element;
	}

	UI::ElementHandle Classed(UI::Context& ctx, UI::ElementHandle parent, StringHash klass) {
		UI::ElementHandle element = Child(ctx, parent);
		ctx.AddClass(element, klass);
		return element;
	}

	InputEvent MoveTo(f32 x, f32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseMove;
		event.mousePosPx = {static_cast<i32>(x), static_cast<i32>(y)};
		return event;
	}

	constexpr Color RED = Color(1.0f, 0.0f, 0.0f, 1.0f);
	constexpr Color GREEN = Color(0.0f, 1.0f, 0.0f, 1.0f);
	constexpr Color BLUE = Color(0.0f, 0.0f, 1.0f, 1.0f);

} // anonymous namespace

TEST(UITextStyleTest, TextPropertiesInherit) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".loud { color: #ff0000; font-size: 20px; font-weight: bold; font-style: italic; letter-spacing: 2; "
					   "line-height: 1.5; text-outline-width: 1; text-outline-color: #00ff00; }");
	UI::ElementHandle parent = Classed(ctx, ctx.GetRoot(), "loud"_hash);
	UI::ElementHandle child = Child(ctx, parent);
	UI::ElementHandle grandchild = Child(ctx, child);
	for (UI::ElementHandle element : {child, grandchild}) {
		const UI::Style& style = ctx.GetComputedStyle(element);
		EXPECT_EQ(style.color, RED);
		EXPECT_EQ(style.fontSize, UI::Length::Pixel(20.0f));
		EXPECT_EQ(style.fontWeight, 700);
		EXPECT_EQ(style.fontStyle, UI::FontStyle::Italic);
		EXPECT_EQ(style.letterSpacing, 2.0f);
		EXPECT_EQ(style.lineHeight, UI::Length::Percent(1.5f));
		EXPECT_EQ(style.textOutlineWidth, 1.0f);
		EXPECT_EQ(style.textOutlineColor, GREEN);
	}

	const UI::Style& outside = ctx.GetComputedStyle(Child(ctx, ctx.GetRoot()));
	EXPECT_EQ(outside.color, Color::White());
	EXPECT_EQ(outside.fontSize, UI::Length::Pixel(16.0f));
	EXPECT_EQ(outside.fontWeight, 400);
	EXPECT_EQ(outside.fontStyle, UI::FontStyle::Normal);
	EXPECT_EQ(outside.lineHeight, UI::Length::Auto());
	EXPECT_EQ(outside.font, Handle<FontAsset>{});
}

TEST(UITextStyleTest, BoxPropertiesStayOnElement) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".box { text-align: center; text-align-vertical: flex-end; text-overflow: ellipsis; opacity: 0.5; }");
	UI::ElementHandle child = Child(ctx, Classed(ctx, ctx.GetRoot(), "box"_hash));
	const UI::Style& style = ctx.GetComputedStyle(child);
	EXPECT_EQ(style.textAlign, UI::Align::Start);
	EXPECT_EQ(style.textAlignVertical, UI::Align::Start);
	EXPECT_EQ(style.textOverflow, UI::TextOverflow::Wrap);
	EXPECT_EQ(style.opacity, 1.0f);
}

TEST(UITextStyleTest, InheritedFlagMatchesBehavior) {
	u32 inherited = 0;
	for (const StyleVocabulary::Entry& entry : StyleVocabulary::All) {
		UI::Context ctx(Desc(), nullptr);
		UI::Style declared;
		entry.mutate(declared);
		UI::ElementHandle parent = MakeChild(ctx, ctx.GetRoot(), declared);
		UI::ElementHandle child = Child(ctx, parent);
		const std::string_view declaration = entry.declaration;
		const StringHash name{declaration.substr(0, declaration.find(':'))};
		char parentText[UI::MAX_STYLE_VALUE_TEXT];
		char childText[UI::MAX_STYLE_VALUE_TEXT];
		char initialText[UI::MAX_STYLE_VALUE_TEXT];
		const std::string_view parentValue = UI::ReadStyleProperty(ctx.GetComputedStyle(parent), name, parentText, sizeof(parentText));
		const std::string_view childValue = UI::ReadStyleProperty(ctx.GetComputedStyle(child), name, childText, sizeof(childText));
		const std::string_view initialValue = UI::ReadStyleProperty(UI::Style{}, name, initialText, sizeof(initialText));
		if (parentValue == initialValue) {
			continue;
		}
		EXPECT_EQ(childValue == parentValue, UI::IsInheritedStyleProperty(name)) << entry.field;
		inherited += childValue == parentValue ? 1u : 0u;
	}
	EXPECT_EQ(inherited, 8u);
}

TEST(UITextStyleTest, FontSizeRelativeToParent) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".a { font-size: 20px; } .b { font-size: 150%; } .c { font-size: 1.5em; }");
	UI::ElementHandle a = Classed(ctx, ctx.GetRoot(), "a"_hash);
	UI::ElementHandle b = Classed(ctx, a, "b"_hash);
	UI::ElementHandle c = Classed(ctx, b, "c"_hash);
	UI::ElementHandle loose = Classed(ctx, ctx.GetRoot(), "b"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(b).fontSize, UI::Length::Pixel(30.0f));
	EXPECT_EQ(ctx.GetComputedStyle(c).fontSize, UI::Length::Pixel(45.0f));
	EXPECT_EQ(ctx.GetComputedStyle(loose).fontSize, UI::Length::Pixel(24.0f));

	ctx.RemoveClass(a, "a"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(c).fontSize, UI::Length::Pixel(36.0f));
}

TEST(UITextStyleTest, KeywordsPickParentOrInitial) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".red { color: #ff0000; font-size: 30px; } .i { color: initial; font-size: initial; } "
					   ".h { color: inherit; } .u { color: unset; } .h.blue { color: #0000ff; }");
	UI::ElementHandle parent = Classed(ctx, ctx.GetRoot(), "red"_hash);
	UI::ElementHandle initial = Classed(ctx, parent, "i"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(initial).color, Color::White());
	EXPECT_EQ(ctx.GetComputedStyle(initial).fontSize, UI::Length::Pixel(16.0f));
	EXPECT_EQ(ctx.GetComputedStyle(Classed(ctx, parent, "h"_hash)).color, RED);
	EXPECT_EQ(ctx.GetComputedStyle(Classed(ctx, parent, "u"_hash)).color, RED);

	UI::ElementHandle blue = Classed(ctx, parent, "h"_hash);
	ctx.AddClass(blue, "blue"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(blue).color, BLUE);

	UI::Style inlineStyle;
	inlineStyle.color = BLUE;
	EXPECT_TRUE(UI::ApplyStyleDeclaration(inlineStyle, "color"_hash, "inherit", nullptr));
	EXPECT_FALSE(inlineStyle.color.has_value());
}

TEST(UITextStyleTest, NamedColorsParse) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".n { color: Teal; background-color: red; border-color: grey; }");
	const UI::Style& style = ctx.GetComputedStyle(Classed(ctx, ctx.GetRoot(), "n"_hash));
	EXPECT_EQ(style.color, Color(0.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f));
	EXPECT_EQ(style.background.solid, RED);
	EXPECT_EQ(style.borderColor, Color(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f));
}

TEST(UITextStyleTest, SetTextBelowDeclaredAboveInherited) {
	FontAsset fontA;
	FontAsset fontB;
	UI::Context ctx(Desc(), nullptr);
	ctx.SetStyleFont("b", Handle<FontAsset>(&fontB));
	ctx.LoadStyleSheet(".parent { color: #ff0000; font-size: 20px; font-family: b; } .big { font-size: 30px; color: #00ff00; }");
	UI::ElementHandle parent = Classed(ctx, ctx.GetRoot(), "parent"_hash);
	UI::ElementHandle label = Child(ctx, parent);
	ctx.SetText(label, Handle<FontAsset>(&fontA), 12.0f, BLUE, "x");
	EXPECT_EQ(ctx.GetComputedStyle(label).color, BLUE);
	EXPECT_EQ(ctx.GetComputedStyle(label).fontSize, UI::Length::Pixel(12.0f));
	EXPECT_EQ(ctx.GetComputedStyle(label).font, Handle<FontAsset>(&fontA));

	ctx.AddClass(label, "big"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(label).color, GREEN);
	EXPECT_EQ(ctx.GetComputedStyle(label).fontSize, UI::Length::Pixel(30.0f));
	EXPECT_EQ(ctx.GetComputedStyle(label).font, Handle<FontAsset>(&fontA));

	ctx.SetText(label, Handle<FontAsset>(&fontA), 12.0f, RED, "x");
	ctx.RemoveClass(label, "big"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(label).color, RED);
	EXPECT_EQ(ctx.GetComputedStyle(label).fontSize, UI::Length::Pixel(12.0f));

	ctx.ClearText(label);
	EXPECT_EQ(ctx.GetComputedStyle(label).color, RED);
	EXPECT_EQ(ctx.GetComputedStyle(label).fontSize, UI::Length::Pixel(20.0f));
	EXPECT_EQ(ctx.GetComputedStyle(label).font, Handle<FontAsset>(&fontB));
}

TEST(UITextStyleTest, SetTextValuesReachChildren) {
	FontAsset font;
	SizedFont fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle button = Child(ctx, ctx.GetRoot());
	ctx.SetText(button, Handle<FontAsset>(&font), 18.0f, GREEN, "");
	UI::ElementHandle label = Child(ctx, button);
	ctx.SetTextString(label, "abcd");
	ctx.Update();
	EXPECT_EQ(ctx.GetComputedStyle(label).color, GREEN);
	EXPECT_EQ(ctx.GetComputedStyle(label).font, Handle<FontAsset>(&font));
	EXPECT_EQ(ctx.GetComputedStyle(label).fontSize, UI::Length::Pixel(18.0f));
	EXPECT_EQ(ctx.GetTextString(label), "abcd");

	ctx.SetText(button, Handle<FontAsset>(&font), 10.0f, BLUE, "");
	ctx.Update();
	EXPECT_EQ(ctx.GetComputedStyle(label).fontSize, UI::Length::Pixel(10.0f));
	EXPECT_EQ(ctx.GetComputedStyle(label).color, BLUE);
}

TEST(UITextStyleTest, InheritedSizeRemeasuresText) {
	FontAsset body;
	SizedFont fonts;
	UI::Context ctx(Desc(), &fonts);
	ctx.SetStyleFont("body", Handle<FontAsset>(&body));
	ctx.LoadStyleSheet(".small { font-size: 10px; font-family: url(\"body\"); } .large { font-size: 40px; }");
	UI::ElementHandle section = Classed(ctx, ctx.GetRoot(), "small"_hash);
	UI::ElementHandle row = Child(ctx, section);
	UI::ElementHandle label = Child(ctx, row);
	ctx.SetTextString(label, "abcd");
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, 20.0f);
	EXPECT_EQ(fonts.lastFont, Handle<FontAsset>(&body));

	ctx.AddClass(section, "large"_hash);
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, 80.0f);
	EXPECT_EQ(fonts.lastSize, 40.0f);

	ctx.RemoveClass(section, "large"_hash);
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, 20.0f);
}

TEST(UITextStyleTest, ReparentedTextTakesNewParent) {
	SizedFont fonts;
	UI::Context ctx(Desc(), &fonts);
	ctx.LoadStyleSheet(".a { font-size: 10px; color: #ff0000; } .b { font-size: 30px; color: #0000ff; }");
	UI::ElementHandle a = Classed(ctx, ctx.GetRoot(), "a"_hash);
	UI::ElementHandle b = Classed(ctx, ctx.GetRoot(), "b"_hash);
	UI::ElementHandle wrapper = Child(ctx, a);
	UI::ElementHandle label = Child(ctx, wrapper);
	ctx.SetTextString(label, "ab");
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, 10.0f);

	ctx.AppendChild(b, wrapper);
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, 30.0f);
	EXPECT_EQ(ctx.GetComputedStyle(label).color, BLUE);
}

TEST(UITextStyleTest, ColorChangeKeepsShapes) {
	SizedFont fonts;
	UI::Context ctx(Desc(), &fonts);
	ctx.LoadStyleSheet(".tint { color: #ff0000; }");
	UI::ElementHandle section = Child(ctx, ctx.GetRoot());
	UI::ElementHandle label = Child(ctx, section);
	ctx.SetTextString(label, "abc");
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);
	const u32 walks = fonts.walks;

	ctx.AddClass(section, "tint"_hash);
	ctx.Update();
	ctx.BuildRenderList(list);
	EXPECT_EQ(fonts.walks, walks);
	EXPECT_EQ(ctx.GetComputedStyle(label).color, RED);
}

TEST(UITextStyleTest, HoverRecolorsWithoutRecomposingChildren) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".row { width: 200px; height: 40px; } .row:hover { color: #ff0000; }");
	UI::ElementHandle row = Classed(ctx, ctx.GetRoot(), "row"_hash);
	UI::ElementHandle labels[3] = {Child(ctx, row), Child(ctx, row), Child(ctx, row)};
	ctx.Update();
	ctx.ResetStyleStats();

	ctx.InjectInput(MoveTo(10.0f, 10.0f));
	ctx.Update();
	EXPECT_EQ(ctx.GetStyleStats().recalcs, 1u);
	for (UI::ElementHandle label : labels) {
		EXPECT_EQ(ctx.GetComputedStyle(label).color, RED);
	}
	EXPECT_EQ(ctx.GetStyleStats().recalcs, 1u);

	ctx.InjectInput(MoveTo(500.0f, 500.0f));
	ctx.Update();
	EXPECT_EQ(ctx.GetComputedStyle(labels[2]).color, Color::White());
}

TEST(UITextStyleTest, StyleFontNamesResolve) {
	FontAsset fontA;
	FontAsset fontB;
	UI::Context ctx(Desc(), nullptr);
	UI::Context other(Desc(), nullptr);
	ctx.SetStyleFont("title", Handle<FontAsset>(&fontA));
	const char* sheet = ".q { font-family: \"title\"; } .u { font-family: url('title'); } .bare { font-family: title; } "
						".none { font-family: none; } .missing { font-family: nowhere; }";
	ctx.LoadStyleSheet(sheet);
	other.LoadStyleSheet(sheet);
	for (StringHash klass : {"q"_hash, "u"_hash, "bare"_hash}) {
		EXPECT_EQ(ctx.GetComputedStyle(Classed(ctx, ctx.GetRoot(), klass)).font, Handle<FontAsset>(&fontA));
	}
	UI::ElementHandle parent = Classed(ctx, ctx.GetRoot(), "q"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(Classed(ctx, parent, "missing"_hash)).font, Handle<FontAsset>(&fontA));
	EXPECT_EQ(ctx.GetComputedStyle(Classed(ctx, parent, "none"_hash)).font, Handle<FontAsset>{});
	EXPECT_EQ(other.GetComputedStyle(Classed(other, other.GetRoot(), "q"_hash)).font, Handle<FontAsset>{});

	UI::ElementHandle renamed = Classed(ctx, ctx.GetRoot(), "bare"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(renamed).font, Handle<FontAsset>(&fontA));
	ctx.SetStyleFont("title", Handle<FontAsset>(&fontB));
	EXPECT_EQ(ctx.GetComputedStyle(renamed).font, Handle<FontAsset>(&fontB));
	EXPECT_EQ(ctx.GetComputedStyle(Child(ctx, renamed)).font, Handle<FontAsset>(&fontB));
}

TEST(UITextStyleTest, DocumentTextInheritsContainers) {
	FontAsset font;
	SizedFont fonts;
	UI::Context ctx(Desc(), &fonts);
	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(R"(<ui version="1"><panel id="top"><panel id="loud" style="font-size: 30px; color: #ff0000"><panel id="inner">Hi</panel></panel><panel id="plain">Yo</panel></panel></ui>)",
							   error))
		<< error;
	UIInstantiateDesc desc;
	desc.font = Handle<FontAsset>(&font);
	desc.fontSizePx = 14.0f;
	desc.textColor = GREEN;
	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
	document.Instantiate(ctx, ctx.GetRoot(), desc, mapping);
	ASSERT_EQ(mapping.size(), 4u);
	ctx.Update();
	const UI::Style& inner = ctx.GetComputedStyle(mapping[2].element);
	EXPECT_EQ(inner.fontSize, UI::Length::Pixel(30.0f));
	EXPECT_EQ(inner.color, RED);
	EXPECT_EQ(inner.font, Handle<FontAsset>(&font));
	const UI::Style& plain = ctx.GetComputedStyle(mapping[3].element);
	EXPECT_EQ(plain.fontSize, UI::Length::Pixel(14.0f));
	EXPECT_EQ(plain.color, GREEN);
	EXPECT_EQ(ctx.GetLayout(mapping[3].element).content.w, 14.0f);
}

TEST(UITextStyleTest, DocumentRootKeepsDeclaredText) {
	SizedFont fonts;
	UI::Context ctx(Desc(), &fonts);
	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(R"(<ui version="1"><panel style="font-size: 22px">Hi</panel></ui>)", error)) << error;
	UIInstantiateDesc desc;
	desc.fontSizePx = 14.0f;
	desc.textColor = BLUE;
	UI::ElementHandle root = document.Instantiate(ctx, ctx.GetRoot(), desc);
	EXPECT_EQ(ctx.GetComputedStyle(root).fontSize, UI::Length::Pixel(22.0f));
	EXPECT_EQ(ctx.GetComputedStyle(root).color, BLUE);
}

TEST(UITextStyleTest, DocumentButtonLabelInheritsButton) {
	SizedFont fonts;
	UI::Context ctx(Desc(), &fonts);
	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(R"(<ui version="1"><panel><button style="font-size: 24px" text="Play"/><button text="Quit"/></panel></ui>)", error))
		<< error;
	UIInstantiateDesc desc;
	desc.fontSizePx = 14.0f;
	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
	document.Instantiate(ctx, ctx.GetRoot(), desc, mapping);
	ASSERT_EQ(mapping.size(), 3u);
	ctx.Update();
	const UI::Element* big = ctx.GetElement(mapping[1].element);
	const UI::Element* small = ctx.GetElement(mapping[2].element);
	ASSERT_NE(big, nullptr);
	ASSERT_NE(small, nullptr);
	const UI::ElementHandle bigLabel = ctx.MakeHandle(big->firstChild);
	const UI::ElementHandle smallLabel = ctx.MakeHandle(small->firstChild);
	EXPECT_EQ(ctx.GetTextString(bigLabel), "Play");
	EXPECT_EQ(ctx.GetComputedStyle(bigLabel).fontSize, UI::Length::Pixel(24.0f));
	EXPECT_EQ(ctx.GetComputedStyle(smallLabel).fontSize, UI::Length::Pixel(14.0f));
	EXPECT_EQ(ctx.GetComputedStyle(smallLabel).color, ctx.GetStyleVarColor("--slt-text"_hash, Color::Magenta()));
}
