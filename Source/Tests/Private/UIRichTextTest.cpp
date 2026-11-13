#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/UIDocument.h"
#include "Shalltear/Core/UI/Widgets.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	class RecordingFont final : public UI::IFontSystem {
	public:
		str utf8;
		std::vector<UI::TextRun> runs;
		u32 shapes = 0;

		bool ShapeText(const UI::TextQuery& query, UI::ShapedRun& out) override {
			++shapes;
			Record(query);
			out.glyphs.clear();
			out.outlineGlyphs.clear();
			out.caretStops.clear();
			out.spans.clear();
			out.decorations.clear();
			out.atlas = RHI::TextureHandle{7, 1};
			out.caretStops.push_back({0, 0.0f, 0});
			f32 x = 0.0f;
			for (u32 i = 0; i < query.utf8.size(); ++i) {
				const UI::TextRun* run = RunAt(query, i);
				const u16 paint = run ? run->paint : u16{0};
				if (!query.runs.empty() && (out.spans.empty() || out.spans.back().paint != paint)) {
					out.spans.push_back({static_cast<u32>(out.glyphs.size()), 0, paint});
				}
				const f32 advance = SizeAt(query, i) * 0.5f;
				out.glyphs.push_back({{x, 0.0f, advance, 10.0f}, {0.0f, 0.0f, 0.5f, 0.5f}});
				if (run && HasFlag(run->decoration, UI::TextDecoration::Mark)) {
					out.decorations.push_back({{x, 0.0f, advance, 12.0f}, paint, UI::TextDecoration::Mark});
				}
				if (run && HasFlag(run->decoration, UI::TextDecoration::Underline)) {
					out.decorations.push_back({{x, 11.0f, advance, 1.0f}, paint, UI::TextDecoration::Underline});
				}
				x += advance;
				out.caretStops.push_back({i + 1, x, 0});
			}
			out.extent = {x, 16.0f};
			out.lineHeight = 16.0f;
			return true;
		}

		Vec2 MeasureText(const UI::TextQuery& query) override {
			Record(query);
			f32 x = 0.0f;
			for (u32 i = 0; i < query.utf8.size(); ++i) {
				x += SizeAt(query, i) * 0.5f;
			}
			return {x, 16.0f};
		}

		f32 GetLineHeight(Handle<FontAsset>, f32) override { return 16.0f; }
	private:
		void Record(const UI::TextQuery& query) {
			utf8 = str(query.utf8);
			runs.assign(query.runs.begin(), query.runs.end());
		}

		static const UI::TextRun* RunAt(const UI::TextQuery& query, u32 byte) {
			for (const UI::TextRun& run : query.runs) {
				if (byte >= run.begin && byte < run.end) {
					return &run;
				}
			}
			return nullptr;
		}

		static f32 SizeAt(const UI::TextQuery& query, u32 byte) {
			const UI::TextRun* run = RunAt(query, byte);
			return run ? run->sizePx : query.sizePx;
		}
	};

	UI::ContextDesc Desc(f32 scale = 1.0f) {
		UI::ContextDesc desc;
		desc.sizePx = {800, 600};
		desc.contentScale = scale;
		return desc;
	}

	struct Rig {
		RecordingFont fonts;
		UI::Context ctx;
		UI::ElementHandle label;

		explicit Rig(f32 scale = 1.0f)
			: ctx(Desc(scale), &fonts) {
			UI::Style style;
			style.fontSize = UI::Length::Pixel(20.0f);
			label = MakeChild(ctx, ctx.GetRoot(), style);
		}

		void Show(std::string_view text, bool rich = true) {
			ctx.SetRichText(label, rich);
			ctx.SetTextString(label, text);
			ctx.Update();
			UI::RenderList list;
			ctx.BuildRenderList(list);
		}

		std::vector<Color> GlyphColors() {
			UI::RenderList list;
			ctx.BuildRenderList(list);
			std::vector<Color> colors;
			for (const UI::UIQuad& quad : list.quads) {
				if (quad.flags & UI::QUAD_FLAG_GLYPH) {
					colors.push_back(quad.color0);
				}
			}
			return colors;
		}
	};

	constexpr Color Red = Color(1.0f, 0.0f, 0.0f, 1.0f);
	constexpr Color Green = Color(0.0f, 1.0f, 0.0f, 1.0f);

} // anonymous namespace

TEST(UIRichTextTest, TagsSplitRuns) {
	Rig rig;
	rig.Show("a<b>b</b>c");
	EXPECT_EQ(rig.fonts.utf8, "abc");
	ASSERT_EQ(rig.fonts.runs.size(), 3u);
	EXPECT_EQ(rig.fonts.runs[0].begin, 0u);
	EXPECT_EQ(rig.fonts.runs[0].end, 1u);
	EXPECT_EQ(rig.fonts.runs[0].fontWeight, 400);
	EXPECT_EQ(rig.fonts.runs[1].fontWeight, 700);
	EXPECT_EQ(rig.fonts.runs[2].begin, 2u);
	EXPECT_EQ(rig.fonts.runs[2].end, 3u);
	EXPECT_EQ(rig.fonts.runs[2].fontWeight, 400);
	EXPECT_EQ(rig.ctx.GetTextString(rig.label), "a<b>b</b>c");
}

TEST(UIRichTextTest, PlainTextSendsNoRuns) {
	Rig rig;
	rig.Show("plain text");
	EXPECT_EQ(rig.fonts.utf8, "plain text");
	EXPECT_TRUE(rig.fonts.runs.empty());

	rig.Show("a<br>b<link=x>c</link>");
	EXPECT_EQ(rig.fonts.utf8, "a\nbc");
	EXPECT_TRUE(rig.fonts.runs.empty());
}

TEST(UIRichTextTest, OffUnlessAsked) {
	Rig rig;
	rig.Show("<b>x</b>", false);
	EXPECT_EQ(rig.fonts.utf8, "<b>x</b>");
	EXPECT_TRUE(rig.fonts.runs.empty());

	rig.ctx.SetRichText(rig.label, true);
	rig.ctx.Update();
	EXPECT_EQ(rig.fonts.utf8, "x");
}

TEST(UIRichTextTest, ClosingOrderFree) {
	Rig rig;
	rig.Show("<b><i>x</b>y</i>z");
	EXPECT_EQ(rig.fonts.utf8, "xyz");
	ASSERT_EQ(rig.fonts.runs.size(), 3u);
	EXPECT_EQ(rig.fonts.runs[0].fontWeight, 700);
	EXPECT_EQ(rig.fonts.runs[0].fontStyle, UI::FontStyle::Italic);
	EXPECT_EQ(rig.fonts.runs[1].fontWeight, 400);
	EXPECT_EQ(rig.fonts.runs[1].fontStyle, UI::FontStyle::Italic);
	EXPECT_EQ(rig.fonts.runs[2].fontStyle, UI::FontStyle::Normal);

	rig.Show("<color=red>a<color=#00ff00>b</color>c</color>d</b>");
	EXPECT_EQ(rig.fonts.utf8, "abcd");
	const std::vector<Color> colors = rig.GlyphColors();
	ASSERT_EQ(colors.size(), 4u);
	EXPECT_EQ(colors[0], Red);
	EXPECT_EQ(colors[1], Green);
	EXPECT_EQ(colors[2], Red);
	EXPECT_EQ(colors[3], Color::White());
}

TEST(UIRichTextTest, UnknownAndMalformedStayLiteral) {
	Rig rig;
	for (std::string_view text : {"Vector<u8> and Handle<T>", "a < b > c", "<b", "<color=nope>x", "<size=big>x", "<font-weight=5000>x",
								  "<b=1>x", "<mark>x", "<rotate=45>x"}) {
		rig.Show(text);
		EXPECT_EQ(rig.fonts.utf8, text);
		EXPECT_TRUE(rig.fonts.runs.empty()) << text;
	}
	rig.Show("<B>x</B><COLOR=RED>y");
	EXPECT_EQ(rig.fonts.utf8, "xy");
	ASSERT_EQ(rig.fonts.runs.size(), 2u);
	EXPECT_EQ(rig.fonts.runs[0].fontWeight, 700);
}

TEST(UIRichTextTest, NoParseShowsTags) {
	Rig rig;
	rig.Show("<noparse><b>x</b></noparse><b>y</b>");
	EXPECT_EQ(rig.fonts.utf8, "<b>x</b>y");
	ASSERT_EQ(rig.fonts.runs.size(), 2u);
	EXPECT_EQ(rig.fonts.runs[0].end, 8u);
	EXPECT_EQ(rig.fonts.runs[0].fontWeight, 400);
	EXPECT_EQ(rig.fonts.runs[1].fontWeight, 700);
}

TEST(UIRichTextTest, SizesRelativeToElement) {
	Rig rig(1.25f);
	rig.Show("<size=30>a</size><size=150%>b</size><size=+2>c</size><size=-2>d</size><size=2em>e</size>f");
	ASSERT_EQ(rig.fonts.runs.size(), 6u);
	EXPECT_FLOAT_EQ(rig.fonts.runs[0].sizePx, 37.5f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[1].sizePx, 37.5f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[2].sizePx, 27.5f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[3].sizePx, 22.5f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[4].sizePx, 50.0f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[5].sizePx, 25.0f);
	EXPECT_FLOAT_EQ(rig.ctx.GetLayout(rig.label).content.w, (37.5f + 37.5f + 27.5f + 22.5f + 50.0f + 25.0f) * 0.5f);
}

TEST(UIRichTextTest, SpacingOffsetsAndCells) {
	Rig rig;
	rig.Show("<cspace=2>a</cspace><cspace=0.5em>b</cspace><voffset=1em>c</voffset><line-height=150%>d</line-height>"
			 "<line-height=30>e</line-height><mspace=12>f</mspace>g<space=1em>h");
	EXPECT_EQ(rig.fonts.utf8, "abcdefg h");
	ASSERT_EQ(rig.fonts.runs.size(), 9u);
	EXPECT_FLOAT_EQ(rig.fonts.runs[0].letterSpacing, 2.0f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[1].letterSpacing, 10.0f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[2].baselineShift, 20.0f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[3].lineHeightScale, 1.5f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[4].lineHeight, 30.0f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[5].advance, 12.0f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[6].advance, 0.0f);
	EXPECT_EQ(rig.fonts.runs[7].begin, 7u);
	EXPECT_EQ(rig.fonts.runs[7].end, 8u);
	EXPECT_FLOAT_EQ(rig.fonts.runs[7].advance, 20.0f);
	EXPECT_TRUE(rig.fonts.runs[7].noBreak);
	EXPECT_FLOAT_EQ(rig.fonts.runs[8].advance, 0.0f);
	EXPECT_FALSE(rig.fonts.runs[8].noBreak);

	for (std::string_view text : {"<space=0>x", "<space=-4>x", "<mspace=0>x"}) {
		rig.Show(text);
		EXPECT_EQ(rig.fonts.utf8, text);
	}
}

TEST(UIRichTextTest, FlagsReachRuns) {
	Rig rig;
	rig.Show("<u>a</u><s>b</s><mark=#ffff0080>c</mark><sub>d</sub><sup>e</sup><lowercase>f</lowercase><allcaps>g</allcaps>"
			 "<smallcaps>h</smallcaps><nobr>i</nobr>");
	ASSERT_EQ(rig.fonts.runs.size(), 9u);
	EXPECT_EQ(rig.fonts.runs[0].decoration, UI::TextDecoration::Underline);
	EXPECT_EQ(rig.fonts.runs[1].decoration, UI::TextDecoration::Strikethrough);
	EXPECT_EQ(rig.fonts.runs[2].decoration, UI::TextDecoration::Mark);
	EXPECT_EQ(rig.fonts.runs[3].script, UI::TextScript::Sub);
	EXPECT_EQ(rig.fonts.runs[4].script, UI::TextScript::Super);
	EXPECT_EQ(rig.fonts.runs[5].textCase, UI::TextCase::Lower);
	EXPECT_EQ(rig.fonts.runs[6].textCase, UI::TextCase::Upper);
	EXPECT_EQ(rig.fonts.runs[7].textCase, UI::TextCase::SmallCaps);
	EXPECT_TRUE(rig.fonts.runs[8].noBreak);
	EXPECT_FALSE(rig.fonts.runs[7].noBreak);
}

TEST(UIRichTextTest, PaintsColorGlyphsAndMarks) {
	Rig rig;
	UI::Style style = rig.ctx.GetStyle(rig.label);
	style.color = Color(0.5f, 0.5f, 0.5f, 1.0f);
	rig.ctx.SetStyle(rig.label, style);
	rig.Show("a<alpha=#80>b</alpha><mark=#ffff0080><u>c</u></mark><color=#ff000080>d");
	const std::vector<Color> colors = rig.GlyphColors();
	ASSERT_EQ(colors.size(), 4u);
	EXPECT_EQ(colors[0], Color(0.5f, 0.5f, 0.5f, 1.0f));
	EXPECT_EQ(colors[1], Color(0.5f, 0.5f, 0.5f, 128.0f / 255.0f));
	EXPECT_EQ(colors[2], Color(0.5f, 0.5f, 0.5f, 1.0f));
	EXPECT_EQ(colors[3], Color(1.0f, 0.0f, 0.0f, 128.0f / 255.0f));

	UI::RenderList list;
	rig.ctx.BuildRenderList(list);
	std::vector<u32> order;
	for (u32 i = 0; i < list.quads.size(); ++i) {
		const UI::UIQuad& quad = list.quads[i];
		if (quad.color0 == Color(1.0f, 1.0f, 0.0f, 128.0f / 255.0f)) {
			order.push_back(0);
		} else if (quad.flags & UI::QUAD_FLAG_GLYPH) {
			order.push_back(1);
		} else if (quad.rect.h == 1.0f) {
			order.push_back(2);
			EXPECT_EQ(quad.color0, Color(0.5f, 0.5f, 0.5f, 1.0f));
		}
	}
	EXPECT_EQ(order, (std::vector<u32>{0, 1, 1, 1, 1, 2}));
}

TEST(UIRichTextTest, FontTagsUseStyleFonts) {
	FontAsset title;
	FontAsset body;
	Rig rig;
	rig.ctx.SetStyleFont("body", Handle<FontAsset>(&body));
	UI::Style style = rig.ctx.GetStyle(rig.label);
	style.font = Handle<FontAsset>(&body);
	rig.ctx.SetStyle(rig.label, style);
	rig.Show("<font=\"title\">a</font><font=nowhere>b</font>c");
	EXPECT_EQ(rig.fonts.utf8, "abc");
	EXPECT_TRUE(rig.fonts.runs.empty());

	rig.ctx.SetStyleFont("title", Handle<FontAsset>(&title));
	rig.ctx.Update();
	ASSERT_EQ(rig.fonts.runs.size(), 2u);
	EXPECT_EQ(rig.fonts.runs[0].font, Handle<FontAsset>(&title));
	EXPECT_EQ(rig.fonts.runs[1].font, Handle<FontAsset>(&body));
}

TEST(UIRichTextTest, CodeBuiltButtonShowsTags) {
	RecordingFont fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::Widgets::ButtonDesc plain;
	plain.label = "<b>Go</b>";
	UI::ElementHandle button = UI::Widgets::Button(ctx, ctx.GetRoot(), plain);
	ctx.Update();
	EXPECT_EQ(fonts.utf8, "<b>Go</b>");

	UI::Widgets::ButtonDesc rich = plain;
	rich.richText = true;
	ctx.DestroyElement(button);
	UI::Widgets::Button(ctx, ctx.GetRoot(), rich);
	ctx.Update();
	EXPECT_EQ(fonts.utf8, "Go");
	ASSERT_EQ(fonts.runs.size(), 1u);
	EXPECT_EQ(fonts.runs[0].fontWeight, 700);
}

TEST(UIRichTextTest, TextInputShowsTags) {
	Rig rig;
	rig.ctx.SetTextInput(rig.label, true);
	rig.Show("<b>x</b>");
	EXPECT_EQ(rig.fonts.utf8, "<b>x</b>");
	EXPECT_TRUE(rig.fonts.runs.empty());
}

TEST(UIRichTextTest, ElementStyleReachesRuns) {
	Rig rig;
	UI::Style style = rig.ctx.GetStyle(rig.label);
	style.fontWeight = 300;
	style.letterSpacing = 3.0f;
	style.lineHeight = UI::Length::Percent(2.0f);
	rig.ctx.SetStyle(rig.label, style);
	rig.Show("a<size=40>b</size><i>c</i>");
	ASSERT_EQ(rig.fonts.runs.size(), 3u);
	EXPECT_EQ(rig.fonts.runs[0].fontWeight, 300);
	EXPECT_FLOAT_EQ(rig.fonts.runs[0].letterSpacing, 3.0f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[0].lineHeight, 40.0f);
	EXPECT_FLOAT_EQ(rig.fonts.runs[1].lineHeight, 80.0f);
	EXPECT_EQ(rig.fonts.runs[2].fontWeight, 300);
	EXPECT_EQ(rig.fonts.runs[2].fontStyle, UI::FontStyle::Italic);

	style.fontWeight = 600;
	rig.ctx.SetStyle(rig.label, style);
	rig.ctx.Update();
	EXPECT_EQ(rig.fonts.runs[0].fontWeight, 600);
}

TEST(UIRichTextTest, DocumentTextRichByDefault) {
	RecordingFont fonts;
	UI::Context ctx(Desc(), &fonts);
	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(R"(<ui version="1"><panel><panel>a&lt;b&gt;b&lt;/b&gt;</panel><panel enable-rich-text="false">&lt;b&gt;c</panel><button text="&lt;i&gt;Go&lt;/i&gt;"/></panel></ui>)",
							   error))
		<< error;
	EXPECT_TRUE(document.root.children[0].richText);
	EXPECT_FALSE(document.root.children[1].richText);
	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
	document.Instantiate(ctx, ctx.GetRoot(), UIInstantiateDesc{}, mapping);
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(mapping.size(), 4u);

	auto shapedOf = [&](UI::ElementHandle element) {
		const UI::Context::ShapedTextView view = ctx.GetShapedText(element);
		return view.run ? view.run->caretStops.size() : 0u;
	};
	EXPECT_EQ(shapedOf(mapping[1].element), 3u);
	EXPECT_EQ(shapedOf(mapping[2].element), 5u);
	const UI::Element* button = ctx.GetElement(mapping[3].element);
	ASSERT_NE(button, nullptr);
	EXPECT_EQ(shapedOf(ctx.MakeHandle(button->firstChild)), 3u);

	const str written = document.Write();
	EXPECT_NE(written.find("enable-rich-text=\"false\""), str::npos);
	UIDocumentAsset reread;
	ASSERT_TRUE(reread.Parse(written, error)) << error;
	EXPECT_FALSE(reread.root.children[1].richText);
	EXPECT_EQ(written.find("enable-rich-text=\"true\""), str::npos);
}

TEST(UIRichTextTest, DocumentRefusesBadRichAttribute) {
	UIDocumentAsset document;
	str error;
	EXPECT_FALSE(document.Parse(R"(<ui version="1"><panel enable-rich-text="yes">x</panel></ui>)", error));
	EXPECT_NE(error.find("enable-rich-text"), str::npos);

	error.clear();
	EXPECT_FALSE(document.Parse(R"(<ui version="1"><panel><checkbox enable-rich-text="false"/></panel></ui>)", error));
	EXPECT_NE(error.find("needs text"), str::npos);
	EXPECT_TRUE(document.Parse(R"(<ui version="1"><panel><button text="x" enable-rich-text="false"/></panel></ui>)", error)) << error;

	error.clear();
	EXPECT_FALSE(document.Parse(R"(<ui version="1"><template name="t" src="project://T.slxml"/><instance template="t"><attribute-overrides element-id="a" enable-rich-text="false"/></instance></ui>)",
								error));
	EXPECT_NE(error.find("can't be overridden"), str::npos);
}
