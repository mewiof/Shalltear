#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc(i32 width = 400, i32 height = 300) {
		UI::ContextDesc desc;
		desc.sizePx = {width, height};
		desc.contentScale = 1.0f;
		return desc;
	}

	UI::Style SolidBox(f32 width, f32 height, Color color) {
		UI::Style style = FixedBox(width, height);
		style.background = UI::Brush::SolidColor(color);
		return style;
	}

	u32 CountKind(const UI::RenderList& list, UI::UICommandKind kind) {
		u32 count = 0;
		for (const UI::UICommand& command : list.commands) {
			if (command.kind == kind) {
				++count;
			}
		}
		return count;
	}

} // anonymous namespace

TEST(UIPaintTest, SolidBackgroundEmitsOneLinearQuad) {
	UI::Context ctx(Desc(), nullptr);
	MakeChild(ctx, ctx.GetRoot(), SolidBox(100.0f, 50.0f, Color::White()));
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	ASSERT_EQ(list.commands.size(), 1u);
	EXPECT_EQ(list.commands[0].kind, UI::UICommandKind::Quads);
	EXPECT_EQ(list.commands[0].quadCount, 1u);
	EXPECT_FALSE(list.commands[0].texture.IsValid());

	const UI::UIQuad& quad = list.quads[0];
	EXPECT_EQ(quad.rect, (UI::Rect{0.0f, 0.0f, 100.0f, 50.0f}));
	EXPECT_NEAR(quad.color0.a, 1.0f, 0.001f);
	EXPECT_EQ(quad.clipRect, UI::InfiniteClip);
}

TEST(UIPaintTest, AuthoredSRGBReachesTheGPUUnconverted) {
	UI::Context ctx(Desc(), nullptr);
	MakeChild(ctx, ctx.GetRoot(), SolidBox(10.0f, 10.0f, Color(0.5f, 0.25f, 0.75f, 0.5f)));
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	EXPECT_EQ(list.quads[0].color0.r, 0.5f);
	EXPECT_EQ(list.quads[0].color0.g, 0.25f);
	EXPECT_EQ(list.quads[0].color0.b, 0.75f);
	EXPECT_EQ(list.quads[0].color0.a, 0.5f);
	EXPECT_EQ(list.quads[0].color1.r, list.quads[0].color0.r);
}

TEST(UIPaintTest, AdjacentUntexturedQuadsBatch) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	for (int i = 0; i < 4; ++i) {
		MakeChild(ctx, root, SolidBox(50.0f, 20.0f, Color::White()));
	}
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(list.quads.size(), 4u);
	ASSERT_EQ(list.commands.size(), 1u);
	EXPECT_EQ(list.commands[0].quadCount, 4u);
}

TEST(UIPaintTest, ZIndexReordersCommandsStably) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style highStyle = SolidBox(50.0f, 20.0f, Color(1.0f, 0.0f, 0.0f, 1.0f));
	highStyle.zIndex = 5;
	UI::ElementHandle high = MakeChild(ctx, root, highStyle);
	UI::ElementHandle low = MakeChild(ctx, root, SolidBox(50.0f, 20.0f, Color(0.0f, 1.0f, 0.0f, 1.0f)));
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.commands.size(), 2u);
	EXPECT_EQ(list.commands[0].z, 0);
	EXPECT_EQ(list.commands[1].z, 5);
	(void)high;
	(void)low;
}

TEST(UIPaintTest, OverflowHiddenClipsChildren) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style clipperStyle = SolidBox(100.0f, 100.0f, Color::White());
	clipperStyle.overflow = UI::Overflow::Hidden;
	UI::ElementHandle clipper = MakeChild(ctx, ctx.GetRoot(), clipperStyle);
	MakeChild(ctx, clipper, SolidBox(500.0f, 20.0f, Color::White()));
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 2u);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 0u);
	EXPECT_EQ(list.quads[1].clipRect, (UI::Rect{0.0f, 0.0f, 100.0f, 100.0f}));
}

TEST(UIPaintTest, RoundedClipMasksWholeSubtreeOnce) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style roundedStyle = SolidBox(100.0f, 100.0f, Color::White());
	roundedStyle.overflow = UI::Overflow::Hidden;
	roundedStyle.borderRadius = UI::Radii::All(8.0f);
	UI::ElementHandle rounded = MakeChild(ctx, ctx.GetRoot(), roundedStyle);
	MakeChild(ctx, rounded, SolidBox(500.0f, 20.0f, Color::White()));
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 1u);

	u32 masks = 0;
	for (const UI::UIQuad& quad : list.quads) {
		if ((quad.flags & UI::QUAD_FLAG_PREMULTIPLIED) != 0u) {
			++masks;
			EXPECT_NEAR(quad.radii.topLeft, 8.0f, 0.001f);
		} else {
			EXPECT_TRUE(quad.radii.IsZero());
			EXPECT_TRUE(quad.clipRadii.IsZero());
		}
	}
	EXPECT_EQ(masks, 1u);
}

TEST(UIPaintTest, IdleRoundedClipNoLayer) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style roundedStyle = SolidBox(100.0f, 100.0f, Color::White());
	roundedStyle.overflow = UI::Overflow::Hidden;
	roundedStyle.borderRadius = UI::Radii::All(8.0f);
	MakeChild(ctx, ctx.GetRoot(), roundedStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 0u);
	ASSERT_EQ(list.quads.size(), 1u);
	EXPECT_NEAR(list.quads[0].radii.topLeft, 8.0f, 0.001f);
}

TEST(UIPaintTest, RedundantNestedClipNoLayer) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style leafStyle = SolidBox(200.0f, 200.0f, Color::Black());
	leafStyle.overflow = UI::Overflow::Hidden;
	leafStyle.borderRadius = UI::Radii::All(6.0f);
	leafStyle.flexDirection = UI::FlexDirection::Column;
	UI::ElementHandle leaf = MakeChild(ctx, ctx.GetRoot(), leafStyle);

	UI::Style barStyle = SolidBox(200.0f, 30.0f, Color::White());
	barStyle.overflow = UI::Overflow::Hidden;
	barStyle.borderRadius = UI::Radii{6.0f, 6.0f, 0.0f, 0.0f};
	UI::ElementHandle bar = MakeChild(ctx, leaf, barStyle);
	MakeChild(ctx, bar, SolidBox(50.0f, 20.0f, Color::White()));
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 1u);

	UI::Style movedStyle = barStyle;
	movedStyle.borderRadius = UI::Radii::All(4.0f);
	ctx.SetStyle(bar, movedStyle);
	ctx.Update();
	list.Clear();
	ctx.BuildRenderList(list);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 2u);
}

TEST(UIPaintTest, NestedRoundedClipsCreateOneLayer) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style outerStyle = SolidBox(200.0f, 200.0f, Color::White());
	outerStyle.overflow = UI::Overflow::Hidden;
	outerStyle.borderRadius = UI::Radii::All(10.0f);
	UI::ElementHandle outer = MakeChild(ctx, ctx.GetRoot(), outerStyle);

	UI::Style innerStyle = SolidBox(100.0f, 100.0f, Color::White());
	innerStyle.overflow = UI::Overflow::Hidden;
	innerStyle.borderRadius = UI::Radii::All(6.0f);
	UI::ElementHandle inner = MakeChild(ctx, outer, innerStyle);
	MakeChild(ctx, inner, SolidBox(500.0f, 20.0f, Color::White()));
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 2u);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PopLayer), 2u);

	bool foundComposite = false;
	for (size_t i = 0; i + 1 < list.commands.size(); ++i) {
		if (list.commands[i].kind == UI::UICommandKind::PopLayer) {
			const UI::UICommand& next = list.commands[i + 1];
			ASSERT_EQ(next.kind, UI::UICommandKind::Quads);
			EXPECT_EQ(next.texture, UI::LayerCompositeTexture);
			EXPECT_TRUE((list.quads[next.firstQuad].flags & UI::QUAD_FLAG_PREMULTIPLIED) != 0u);
			foundComposite = true;
		}
	}
	EXPECT_TRUE(foundComposite);
}

TEST(UIPaintTest, GroupOpacityCreatesALayer) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style groupStyle = SolidBox(100.0f, 100.0f, Color::White());
	groupStyle.opacity = 0.5f;
	UI::ElementHandle group = MakeChild(ctx, ctx.GetRoot(), groupStyle);
	MakeChild(ctx, group, SolidBox(50.0f, 20.0f, Color::White()));
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 1u);

	for (const UI::UICommand& command : list.commands) {
		if (command.kind == UI::UICommandKind::Quads && command.texture == UI::LayerCompositeTexture) {
			EXPECT_NEAR(list.quads[command.firstQuad].color0.a, 0.5f, 0.001f);
		}
	}
}

TEST(UIPaintTest, UnclippedLayerBoundedByContext) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style groupStyle = SolidBox(100.0f, 100.0f, Color::White());
	groupStyle.opacity = 0.5f;
	UI::ElementHandle group = MakeChild(ctx, ctx.GetRoot(), groupStyle);
	MakeChild(ctx, group, SolidBox(50.0f, 20.0f, Color::White()));
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 1u);
	for (const UI::UICommand& command : list.commands) {
		if (command.kind != UI::UICommandKind::PushLayer && command.kind != UI::UICommandKind::PopLayer) {
			continue;
		}
		EXPECT_FLOAT_EQ(command.layerBounds.x, 0.0f);
		EXPECT_FLOAT_EQ(command.layerBounds.y, 0.0f);
		EXPECT_FLOAT_EQ(command.layerBounds.w, 400.0f);
		EXPECT_FLOAT_EQ(command.layerBounds.h, 300.0f);
	}

	bool foundComposite = false;
	for (const UI::UICommand& command : list.commands) {
		if (command.kind == UI::UICommandKind::Quads && command.texture == UI::LayerCompositeTexture) {
			EXPECT_FLOAT_EQ(list.quads[command.firstQuad].rect.w, 400.0f);
			EXPECT_FLOAT_EQ(list.quads[command.firstQuad].rect.h, 300.0f);
			foundComposite = true;
		}
	}
	EXPECT_TRUE(foundComposite);
}

TEST(UIPaintTest, ClippingAncestorStillNarrowsLayerBelowContext) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style clipperStyle = SolidBox(120.0f, 90.0f, Color::White());
	clipperStyle.overflow = UI::Overflow::Hidden;
	UI::ElementHandle clipper = MakeChild(ctx, ctx.GetRoot(), clipperStyle);
	UI::Style groupStyle = SolidBox(100.0f, 100.0f, Color::White());
	groupStyle.opacity = 0.5f;
	UI::ElementHandle group = MakeChild(ctx, clipper, groupStyle);
	MakeChild(ctx, group, SolidBox(50.0f, 20.0f, Color::White()));
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 1u);
	for (const UI::UICommand& command : list.commands) {
		if (command.kind == UI::UICommandKind::PushLayer) {
			EXPECT_FLOAT_EQ(command.layerBounds.w, 120.0f);
			EXPECT_FLOAT_EQ(command.layerBounds.h, 90.0f);
		}
	}
}

TEST(UIPaintTest, LeafOpacityNeedsNoLayer) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style leafStyle = SolidBox(100.0f, 100.0f, Color::White());
	leafStyle.opacity = 0.25f;
	MakeChild(ctx, ctx.GetRoot(), leafStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 0u);
	ASSERT_EQ(list.quads.size(), 1u);
	EXPECT_NEAR(list.quads[0].color0.a, 0.25f, 0.001f);
}

TEST(UIPaintTest, BorderEmitsOneRoundedRing) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style borderedStyle = FixedBox(100.0f, 50.0f);
	borderedStyle.borderWidth = UI::Edges::All(2.0f);
	borderedStyle.borderRadius = UI::Radii::All(8.0f);
	borderedStyle.borderColor = Color(1.0f, 0.0f, 0.0f, 1.0f);
	MakeChild(ctx, ctx.GetRoot(), borderedStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	const UI::UIQuad& ring = list.quads[0];
	EXPECT_TRUE((ring.flags & UI::QUAD_FLAG_RING) != 0u);
	EXPECT_FALSE((ring.flags & UI::QUAD_FLAG_SDF_FROM_UV) != 0u);
	EXPECT_EQ(ring.rect, (UI::Rect{0.0f, 0.0f, 100.0f, 50.0f}));
	EXPECT_EQ(ring.uv, (UI::Rect{2.0f, 2.0f, 96.0f, 46.0f}));
	EXPECT_NEAR(ring.radii.topLeft, 8.0f, 0.001f);
}

TEST(UIPaintTest, PerEdgeBorderWidthsDeflateTheHole) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style borderedStyle = FixedBox(100.0f, 50.0f);
	borderedStyle.borderWidth = {1.0f, 2.0f, 3.0f, 4.0f};
	borderedStyle.borderColor = Color(1.0f, 0.0f, 0.0f, 1.0f);
	MakeChild(ctx, ctx.GetRoot(), borderedStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	EXPECT_EQ(list.quads[0].uv, (UI::Rect{1.0f, 2.0f, 96.0f, 44.0f}));
}

TEST(UIPaintTest, BorderThickerThanTheBoxFillsIt) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style borderedStyle = FixedBox(10.0f, 10.0f);
	borderedStyle.borderWidth = UI::Edges::All(20.0f);
	borderedStyle.borderColor = Color(1.0f, 0.0f, 0.0f, 1.0f);
	MakeChild(ctx, ctx.GetRoot(), borderedStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	EXPECT_FALSE((list.quads[0].flags & UI::QUAD_FLAG_RING) != 0u);
}

TEST(UIPaintTest, SubPixelBorderStaysAHairline) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style borderedStyle = FixedBox(100.0f, 50.0f);
	borderedStyle.borderWidth = UI::Edges::All(0.25f);
	borderedStyle.borderColor = Color(1.0f, 0.0f, 0.0f, 1.0f);
	MakeChild(ctx, ctx.GetRoot(), borderedStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	EXPECT_TRUE((list.quads[0].flags & UI::QUAD_FLAG_RING) != 0u);
	EXPECT_EQ(list.quads[0].uv, (UI::Rect{1.0f, 1.0f, 98.0f, 48.0f}));
}

TEST(UIPaintTest, InsetOutlineShadowEmitsARing) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style outlinedStyle = FixedBox(100.0f, 50.0f);
	outlinedStyle.borderRadius = UI::Radii::All(4.0f);
	outlinedStyle.AddBoxShadow({0.0f, 0.0f, 0.0f, 1.0f, Color(1.0f, 1.0f, 1.0f, 0.1f), true});
	MakeChild(ctx, ctx.GetRoot(), outlinedStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	const UI::UIQuad& ring = list.quads[0];
	EXPECT_TRUE((ring.flags & UI::QUAD_FLAG_RING) != 0u);
	EXPECT_EQ(ring.uv, (UI::Rect{1.0f, 1.0f, 98.0f, 48.0f}));
	EXPECT_NEAR(ring.radii.topLeft, 4.0f, 0.001f);
	EXPECT_EQ(ring.clipRect, UI::InfiniteClip);
}

TEST(UIPaintTest, InsetShadowFollowsCorners) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style shadowedStyle = FixedBox(100.0f, 50.0f);
	shadowedStyle.borderRadius = UI::Radii::All(4.0f);
	shadowedStyle.AddBoxShadow({0.0f, 1.0f, 0.0f, 0.0f, Color(0.0f, 0.0f, 0.0f, 0.3f), true});
	MakeChild(ctx, ctx.GetRoot(), shadowedStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	const UI::UIQuad& band = list.quads[0];
	EXPECT_TRUE((band.flags & UI::QUAD_FLAG_RING) != 0u);
	EXPECT_EQ(band.rect, (UI::Rect{0.0f, 0.0f, 100.0f, 50.0f}));
	EXPECT_NEAR(band.radii.topLeft, 4.0f, 0.001f);
	EXPECT_EQ(band.uv, (UI::Rect{0.0f, 1.0f, 100.0f, 50.0f}));
	EXPECT_EQ(band.clipRect, UI::InfiniteClip);
}

TEST(UIPaintTest, NegativeInsetOnOppositeEdge) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style shadowedStyle = FixedBox(100.0f, 50.0f);
	shadowedStyle.AddBoxShadow({-1.0f, 0.0f, 0.0f, 0.0f, Color(1.0f, 1.0f, 1.0f, 0.03f), true});
	MakeChild(ctx, ctx.GetRoot(), shadowedStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	EXPECT_TRUE((list.quads[0].flags & UI::QUAD_FLAG_RING) != 0u);
	EXPECT_EQ(list.quads[0].uv, (UI::Rect{-1.0f, 0.0f, 100.0f, 50.0f}));
}

TEST(UIPaintTest, OffsetAndSpreadInsetCombinePerEdge) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style shadowedStyle = FixedBox(100.0f, 50.0f);
	shadowedStyle.AddBoxShadow({0.0f, 2.0f, 0.0f, 1.0f, Color(0.0f, 0.0f, 0.0f, 0.5f), true});
	MakeChild(ctx, ctx.GetRoot(), shadowedStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	EXPECT_EQ(list.quads[0].uv, (UI::Rect{1.0f, 3.0f, 98.0f, 48.0f}));
}

TEST(UIPaintTest, RingInnerRadiiFollowCssDeflateRule) {
	const UI::Radii inner = UI::DeflateRadii(UI::Radii::All(8.0f), UI::Edges::All(2.0f));
	EXPECT_NEAR(inner.topLeft, 6.0f, 0.001f);
	EXPECT_NEAR(inner.bottomRight, 6.0f, 0.001f);

	const UI::Radii mixed = UI::DeflateRadii(UI::Radii::All(10.0f), UI::Edges{2.0f, 4.0f, 6.0f, 8.0f});
	EXPECT_NEAR(mixed.topLeft, 7.0f, 0.001f);
	EXPECT_NEAR(mixed.bottomRight, 3.0f, 0.001f);

	EXPECT_NEAR(UI::DeflateRadii(UI::Radii::All(1.0f), UI::Edges::All(9.0f)).topLeft, 0.0f, 0.001f);
	EXPECT_EQ(UI::DeflateRect({0.0f, 0.0f, 10.0f, 10.0f}, UI::Edges::All(9.0f)), UI::Rect{});
}

TEST(UIPaintTest, BoxShadowUsesSoftnessAndSdf) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style shadowedStyle = SolidBox(100.0f, 50.0f, Color::White());
	shadowedStyle.AddBoxShadow({0.0f, 4.0f, 10.0f, 0.0f, Color(0.0f, 0.0f, 0.0f, 0.5f), false});
	MakeChild(ctx, ctx.GetRoot(), shadowedStyle);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 2u);
	const UI::UIQuad& shadow = list.quads[0];
	EXPECT_NEAR(shadow.softness, 10.0f, 0.001f);
	EXPECT_TRUE((shadow.flags & UI::QUAD_FLAG_SDF_FROM_UV) != 0u);
	EXPECT_LT(shadow.rect.x, shadow.uv.x);
	EXPECT_GT(shadow.rect.w, shadow.uv.w);
}

TEST(UIPaintTest, TextEmitsGlyphQuadsWithAtlasTexture) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle label = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), label);
	ctx.SetText(label, Handle<FontAsset>{}, 14.0f, Color::White(), "abc");
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 3u);
	for (const UI::UIQuad& quad : list.quads) {
		EXPECT_TRUE((quad.flags & UI::QUAD_FLAG_GLYPH) != 0u);
	}
	ASSERT_EQ(list.commands.size(), 1u);
	EXPECT_EQ(list.commands[0].texture, (RHI::TextureHandle{7, 1}));
	EXPECT_EQ(list.commands[0].sampler, UI::SamplerKind::NearestClamp);
}

TEST(UIPaintTest, CustomDrawEmitsThroughTheScope) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style hostStyle = FixedBox(100.0f, 100.0f);
	hostStyle.opacity = 0.5f;
	UI::ElementHandle host = MakeChild(ctx, ctx.GetRoot(), hostStyle);
	ctx.Events(host).customDraw = [](UI::Context&, UI::ElementHandle, UI::PaintScope& scope) {
		scope.EmitQuad({10.0f, 10.0f, 20.0f, 20.0f}, Color::White());
	};
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	EXPECT_EQ(list.quads[0].rect, (UI::Rect{10.0f, 10.0f, 20.0f, 20.0f}));
	EXPECT_NEAR(list.quads[0].color0.a, 0.5f, 0.001f);
}

TEST(UIPaintTest, TexturedQuadCarriesEncodingCallerAskedFor) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle host = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	const RHI::TextureHandle page{9, 1};
	ctx.Events(host).customDraw = [page](UI::Context&, UI::ElementHandle, UI::PaintScope& scope) {
		const UI::Rect uv{0.0f, 0.0f, 1.0f, 1.0f};
		scope.EmitTexturedQuad({0.0f, 0.0f, 10.0f, 10.0f}, page, uv, Color::White());
		scope.EmitTexturedQuad({10.0f, 0.0f, 10.0f, 10.0f}, page, uv, Color::White(),
							   UI::SamplerKind::NearestClamp, {}, UI::TexelEncoding::Premultiplied);
		scope.EmitTexturedQuad({20.0f, 0.0f, 10.0f, 10.0f}, page, uv, Color::White(),
							   UI::SamplerKind::NearestClamp, {}, UI::TexelEncoding::Coverage);
	};
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 3u);
	EXPECT_EQ(list.quads[0].flags & (UI::QUAD_FLAG_GLYPH | UI::QUAD_FLAG_PREMULTIPLIED), 0u);
	EXPECT_NE(list.quads[1].flags & UI::QUAD_FLAG_PREMULTIPLIED, 0u);
	EXPECT_EQ(list.quads[1].flags & UI::QUAD_FLAG_GLYPH, 0u);
	EXPECT_NE(list.quads[2].flags & UI::QUAD_FLAG_GLYPH, 0u);
	EXPECT_EQ(list.quads[2].flags & UI::QUAD_FLAG_PREMULTIPLIED, 0u);
}

TEST(UIPaintTest, ContentOverInsetRing) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);

	UI::Style fieldStyle = FixedBox(40.0f, 20.0f);
	fieldStyle.background = UI::Brush::SolidColor(Color::Black());
	fieldStyle.overflow = UI::Overflow::Hidden;
	fieldStyle.boxShadowCount = 1;
	fieldStyle.boxShadows[0] = UI::BoxShadow{0.0f, 0.0f, 0.0f, 1.0f, Color::White(), true};
	UI::ElementHandle field = MakeChild(ctx, ctx.GetRoot(), fieldStyle);
	ctx.SetText(field, Handle<FontAsset>{}, 14.0f, Color::White(), "-0.21034");
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);

	i32 ring = -1;
	i32 lastGlyph = -1;
	for (u32 i = 0; i < list.quads.size(); ++i) {
		if ((list.quads[i].flags & UI::QUAD_FLAG_RING) != 0u) {
			ring = static_cast<i32>(i);
		}
		if ((list.quads[i].flags & UI::QUAD_FLAG_GLYPH) != 0u) {
			lastGlyph = static_cast<i32>(i);
		}
	}
	ASSERT_GE(ring, 0);
	ASSERT_GE(lastGlyph, 0);

	EXPECT_LT(ring, lastGlyph);
}

TEST(UIPaintTest, NarrowedClipKeepsArcRadii) {
	UI::Context ctx(Desc(), nullptr);

	UI::Style leafStyle = SolidBox(200.0f, 200.0f, Color::Black());
	leafStyle.overflow = UI::Overflow::Hidden;
	leafStyle.borderRadius = UI::Radii::All(6.0f);
	leafStyle.flexDirection = UI::FlexDirection::Column;
	UI::ElementHandle leaf = MakeChild(ctx, ctx.GetRoot(), leafStyle);

	UI::Style spacerStyle = FixedBox(200.0f, 20.0f);
	MakeChild(ctx, leaf, spacerStyle);
	UI::Style fieldStyle = SolidBox(100.0f, 20.0f, Color::White());
	fieldStyle.overflow = UI::Overflow::Hidden;
	UI::ElementHandle field = MakeChild(ctx, leaf, fieldStyle);

	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);

	const UI::Rect& fieldBox = ctx.GetLayout(field).borderBox;
	const UI::UIQuad* fieldQuad = nullptr;
	for (const UI::UIQuad& quad : list.quads) {
		if (quad.rect.y >= fieldBox.y && quad.rect.h == fieldBox.h && quad.rect.w == fieldBox.w) {
			fieldQuad = &quad;
		}
	}
	ASSERT_NE(fieldQuad, nullptr);
	EXPECT_TRUE(fieldQuad->clipRadii.IsZero());
}

TEST(UIPaintTest, NarrowedClipInCornerStillFollowsArc) {
	UI::Context ctx(Desc(), nullptr);

	UI::Style leafStyle = SolidBox(200.0f, 200.0f, Color::Black());
	leafStyle.overflow = UI::Overflow::Hidden;
	leafStyle.borderRadius = UI::Radii::All(6.0f);
	UI::ElementHandle leaf = MakeChild(ctx, ctx.GetRoot(), leafStyle);

	UI::Style cornerStyle = SolidBox(100.0f, 20.0f, Color::White());
	cornerStyle.overflow = UI::Overflow::Hidden;
	UI::ElementHandle corner = MakeChild(ctx, leaf, cornerStyle);

	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);

	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 1u);
	const UI::Rect leafBox = UI::SnapRectToPixels(ctx.GetLayout(leaf).borderBox);
	const UI::UIQuad* mask = nullptr;
	for (const UI::UIQuad& quad : list.quads) {
		if ((quad.flags & UI::QUAD_FLAG_PREMULTIPLIED) != 0u) {
			mask = &quad;
		}
	}
	ASSERT_NE(mask, nullptr);
	EXPECT_NEAR(mask->radii.topLeft, 6.0f, 0.001f);
	EXPECT_EQ(mask->rect, leafBox);

	const UI::Rect& cornerBox = ctx.GetLayout(corner).borderBox;
	const UI::UIQuad* cornerQuad = nullptr;
	for (const UI::UIQuad& quad : list.quads) {
		if (quad.rect.w == cornerBox.w && quad.rect.h == cornerBox.h) {
			cornerQuad = &quad;
		}
	}
	ASSERT_NE(cornerQuad, nullptr);
	EXPECT_TRUE(cornerQuad->clipRadii.IsZero());
}

TEST(UIPaintTest, SnappingProducesIntegerEdges) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	for (int i = 0; i < 3; ++i) {
		UI::Style thirdStyle;
		thirdStyle.width = UI::Length::Percent(1.0f / 3.0f);
		thirdStyle.height = UI::Length::Pixel(20.0f);
		thirdStyle.background = UI::Brush::SolidColor(Color::White());
		MakeChild(ctx, root, thirdStyle);
	}
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 3u);
	for (const UI::UIQuad& quad : list.quads) {
		EXPECT_EQ(quad.rect.x, std::floor(quad.rect.x));
		EXPECT_EQ(quad.rect.w, std::floor(quad.rect.w));
	}
	EXPECT_NEAR(list.quads[0].rect.x + list.quads[0].rect.w, list.quads[1].rect.x, 0.001f);
}

TEST(UIPaintTest, ScrimPaintsOverAppAndUnderDialog) {
	UI::Context ctx(Desc(), nullptr);
	MakeChild(ctx, ctx.GetRoot(), SolidBox(400.0f, 300.0f, Color(0.1f, 0.1f, 0.1f, 1.0f)));
	UI::ElementHandle dialog = ctx.CreateElement();
	ctx.SetStyle(dialog, SolidBox(100.0f, 50.0f, Color(0.9f, 0.9f, 0.9f, 1.0f)));

	ctx.OpenPopup(dialog, {0.0f, 0.0f}, UI::PopupFlags::Center | UI::PopupFlags::Scrim);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	Color scrim = ctx.GetStyleVarColor("--slt-popup-scrim"_hash, Color(0.0f, 0.0f, 0.0f, 1.0f));
	ASSERT_LT(scrim.a, 1.0f);

	i32 app = -1, backdrop = -1, card = -1;
	i32 backdropQuad = -1;
	for (u32 i = 0; i < list.commands.size(); ++i) {
		const UI::UICommand& command = list.commands[i];
		for (u32 q = command.firstQuad; q < command.firstQuad + command.quadCount; ++q) {
			const UI::UIQuad& quad = list.quads[q];
			if (quad.rect.w == 400.0f && quad.color0.a == scrim.a && quad.color0.r == scrim.r) {
				backdrop = static_cast<i32>(i);
				backdropQuad = static_cast<i32>(q);
			} else if (quad.rect.w == 400.0f) {
				app = static_cast<i32>(i);
			} else if (quad.rect.w == 100.0f) {
				card = static_cast<i32>(i);
			}
		}
	}
	ASSERT_GE(backdrop, 0);
	ASSERT_GE(app, 0);
	ASSERT_GE(card, 0);
	EXPECT_LT(app, backdrop);
	EXPECT_LT(backdrop, card);
	EXPECT_EQ(list.quads[backdropQuad].rect, (UI::Rect{0.0f, 0.0f, 400.0f, 300.0f}));

	ctx.CloseAllPopups();
	ctx.Update();
	ctx.BuildRenderList(list);
	for (const UI::UIQuad& quad : list.quads) {
		EXPECT_FALSE(quad.color0.a == scrim.a && quad.color0.r == scrim.r);
	}
}

TEST(UIPaintTest, LineIsOneTurnedRecord) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle host = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 200.0f));
	ctx.Events(host).customDraw = [](UI::Context&, UI::ElementHandle, UI::PaintScope& scope) {
		scope.EmitLine({20.0f, 30.0f}, {80.0f, 110.0f}, 4.0f, Color::White());
		scope.EmitLine({20.0f, 30.0f}, {80.0f, 110.0f}, 4.0f, Color::White(), UI::LineCap::Round);
	};
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 2u);

	const UI::UIQuad& butt = list.quads[0];
	EXPECT_NE(butt.flags & UI::QUAD_FLAG_ROTATED, 0u);
	EXPECT_NE(butt.flags & UI::QUAD_FLAG_SDF_FROM_UV, 0u);
	EXPECT_NEAR(butt.rotation, std::atan2(80.0f, 60.0f), 1e-5f);
	EXPECT_NEAR(butt.uv.w, 100.0f, 1e-3f);
	EXPECT_NEAR(butt.uv.h, 4.0f, 1e-3f);
	EXPECT_NEAR(butt.rect.w, 102.0f, 1e-3f);
	EXPECT_NEAR(butt.rect.h, 6.0f, 1e-3f);
	EXPECT_NEAR(butt.rect.x + butt.rect.w * 0.5f, butt.uv.x + butt.uv.w * 0.5f, 1e-4f);
	EXPECT_NEAR(butt.rect.y + butt.rect.h * 0.5f, butt.uv.y + butt.uv.h * 0.5f, 1e-4f);
	EXPECT_NEAR(butt.rect.x + butt.rect.w * 0.5f, 50.0f, 1e-3f);
	EXPECT_NEAR(butt.rect.y + butt.rect.h * 0.5f, 70.0f, 1e-3f);
	EXPECT_TRUE(butt.radii.IsZero());

	const UI::UIQuad& round = list.quads[1];
	EXPECT_NEAR(round.uv.w, 104.0f, 1e-3f);
	EXPECT_NEAR(round.radii.topLeft, 2.0f, 1e-3f);
	EXPECT_NEAR(round.radii.bottomRight, 2.0f, 1e-3f);
	EXPECT_NEAR(round.rotation, butt.rotation, 1e-5f);
	EXPECT_NEAR(round.rect.x + round.rect.w * 0.5f, 50.0f, 1e-3f);
	EXPECT_NEAR(round.rect.y + round.rect.h * 0.5f, 70.0f, 1e-3f);
}

TEST(UIPaintTest, ADegenerateLineEmitsNothing) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle host = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 200.0f));
	ctx.Events(host).customDraw = [](UI::Context&, UI::ElementHandle, UI::PaintScope& scope) {
		scope.EmitLine({40.0f, 40.0f}, {40.0f, 40.0f}, 4.0f, Color::White());
		scope.EmitLine({40.0f, 40.0f}, {90.0f, 40.0f}, 0.0f, Color::White());
		scope.EmitLine({40.0f, 40.0f}, {90.0f, 40.0f}, -2.0f, Color::White());
	};
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(list.quads.size(), 0u);
}

TEST(UIPaintTest, TurnedRecordInkMatchesScreen) {
	UI::Context ctx(Desc(400, 400), nullptr);
	UI::Style clipStyle = FixedBox(100.0f, 100.0f);
	clipStyle.position = UI::Position::Absolute;
	clipStyle.left = UI::Length::Pixel(250.0f);
	clipStyle.top = UI::Length::Pixel(250.0f);
	clipStyle.overflow = UI::Overflow::Hidden;
	UI::ElementHandle clip = MakeChild(ctx, ctx.GetRoot(), clipStyle);

	UI::Style hostStyle = FixedBox(10.0f, 10.0f);
	hostStyle.position = UI::Position::Absolute;
	hostStyle.left = UI::Length::Pixel(-250.0f);
	hostStyle.top = UI::Length::Pixel(-250.0f);
	UI::ElementHandle host = MakeChild(ctx, clip, hostStyle);
	ctx.Events(host).customDraw = [](UI::Context&, UI::ElementHandle, UI::PaintScope& scope) {
		scope.EmitLine({100.0f, 100.0f}, {300.0f, 300.0f}, 2.0f, Color::White());
	};
	ctx.Update();

	UI::RenderList first;
	ctx.BuildRenderList(first);
	ASSERT_EQ(first.quads.size(), 1u);

	UI::RenderList second;
	ctx.BuildRenderList(second);
	EXPECT_EQ(second.quads.size(), 1u);
}

namespace {

	const UI::DropShadow SOFT_DROP{4.0f, 6.0f, 10.0f, Color(0.0f, 0.0f, 0.0f, 0.5f)};

	UI::Style CasterAt(f32 x, f32 y, f32 width, f32 height, const UI::DropShadow& drop) {
		UI::Style style = SolidBox(width, height, Color::White());
		style.position = UI::Position::Absolute;
		style.left = UI::Length::Pixel(x);
		style.top = UI::Length::Pixel(y);
		style.dropShadow = drop;
		return style;
	}

	size_t FirstOfKind(const UI::RenderList& list, UI::UICommandKind kind) {
		for (size_t i = 0; i < list.commands.size(); ++i) {
			if (list.commands[i].kind == kind) {
				return i;
			}
		}
		return list.commands.size();
	}

	const UI::UIQuad& ShadowQuadAfter(const UI::RenderList& list, size_t push) {
		return list.quads[list.commands[push + 1].firstQuad];
	}

	bool Overlaps(const RectI& a, const RectI& b) {
		return a.Left() < b.Right() && b.Left() < a.Right() && a.Top() < b.Bottom() && b.Top() < a.Bottom();
	}

} // anonymous namespace

TEST(UIPaintTest, DropShadowUnderCaster) {
	UI::Context ctx(Desc(), nullptr);
	MakeChild(ctx, ctx.GetRoot(), CasterAt(20.0f, 30.0f, 100.0f, 50.0f, SOFT_DROP));
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);

	ASSERT_EQ(list.commands.size(), 4u);
	EXPECT_EQ(list.commands[0].kind, UI::UICommandKind::PushShadow);
	EXPECT_EQ(list.commands[1].texture, UI::ShadowMaskTexture);
	EXPECT_EQ(list.commands[2].kind, UI::UICommandKind::Quads);
	EXPECT_FALSE(list.commands[2].texture.IsValid());
	EXPECT_EQ(list.commands[3].kind, UI::UICommandKind::PopShadow);

	ASSERT_EQ(list.shadowCasters.size(), 1u);
	const UI::UIShadowCaster& caster = list.shadowCasters[0];
	EXPECT_EQ(caster.source, (UI::Rect{20.0f, 30.0f, 100.0f, 50.0f}));
	EXPECT_FLOAT_EQ(caster.blurPx, 10.0f);
	EXPECT_GT(static_cast<f32>(caster.marginPx), caster.blurPx * 0.5f);
	EXPECT_EQ(caster.slot.size, (Vec2I{100 + caster.marginPx * 2, 50 + caster.marginPx * 2}));

	const UI::UIQuad& shadow = ShadowQuadAfter(list, 0);
	const f32 margin = static_cast<f32>(caster.marginPx);
	EXPECT_EQ(shadow.rect, (UI::Rect{20.0f - margin + 4.0f, 30.0f - margin + 6.0f, static_cast<f32>(caster.slot.size.x),
									 static_cast<f32>(caster.slot.size.y)}));
	EXPECT_NE(shadow.flags & UI::QUAD_FLAG_GLYPH, 0u);
	EXPECT_EQ(shadow.color0.a, 0.5f);

	const Vec2I page = list.shadowAtlasSizePx;
	ASSERT_GE(page.x, caster.slot.Right());
	ASSERT_GE(page.y, caster.slot.Bottom());
	EXPECT_FLOAT_EQ(shadow.uv.x * page.x, static_cast<f32>(caster.slot.position.x));
	EXPECT_FLOAT_EQ(shadow.uv.y * page.y, static_cast<f32>(caster.slot.position.y));
	EXPECT_FLOAT_EQ(shadow.uv.w * page.x, static_cast<f32>(caster.slot.size.x));
	EXPECT_FLOAT_EQ(shadow.uv.h * page.y, static_cast<f32>(caster.slot.size.y));
}

TEST(UIPaintTest, ZIndexSortsInsideScope) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle caster = MakeChild(ctx, ctx.GetRoot(), CasterAt(10.0f, 10.0f, 100.0f, 50.0f, SOFT_DROP));
	UI::Style sunk = SolidBox(20.0f, 20.0f, Color(1.0f, 0.0f, 0.0f, 1.0f));
	sunk.zIndex = -1;
	MakeChild(ctx, caster, sunk);
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);

	const size_t push = FirstOfKind(list, UI::UICommandKind::PushShadow);
	ASSERT_LT(push + 3, list.commands.size());
	EXPECT_EQ(list.commands[push + 1].texture, UI::ShadowMaskTexture);
	EXPECT_EQ(list.commands[push + 2].z, -1);
	EXPECT_EQ(list.commands[push + 3].z, 0);
}

TEST(UIPaintTest, CastersGetBlurSizedSlots) {
	UI::Context ctx(Desc(), nullptr);
	MakeChild(ctx, ctx.GetRoot(), CasterAt(10.0f, 10.0f, 40.0f, 40.0f, SOFT_DROP));
	UI::DropShadow hard = SOFT_DROP;
	hard.blur = 0.0f;
	MakeChild(ctx, ctx.GetRoot(), CasterAt(80.0f, 10.0f, 40.0f, 40.0f, hard));
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);

	ASSERT_EQ(list.shadowCasters.size(), 2u);
	const UI::UIShadowCaster& soft = list.shadowCasters[0];
	const UI::UIShadowCaster& sharp = list.shadowCasters[1];
	EXPECT_FALSE(Overlaps(soft.slot, sharp.slot));
	EXPECT_GT(soft.marginPx, sharp.marginPx);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushShadow), 2u);
	EXPECT_EQ(CountKind(list, UI::UICommandKind::PopShadow), 2u);

	const Vec2I page = list.shadowAtlasSizePx;
	for (size_t i = 0; i < list.commands.size(); ++i) {
		if (list.commands[i].kind != UI::UICommandKind::PushShadow) {
			continue;
		}
		const UI::UIShadowCaster& caster = list.shadowCasters[list.commands[i].shadowCaster];
		EXPECT_FLOAT_EQ(ShadowQuadAfter(list, i).uv.x * page.x, static_cast<f32>(caster.slot.position.x));
	}
}

TEST(UIPaintTest, ATransparentDropShadowOpensNoScope) {
	UI::Context ctx(Desc(), nullptr);
	UI::DropShadow clear = SOFT_DROP;
	clear.color.a = 0.0f;
	MakeChild(ctx, ctx.GetRoot(), CasterAt(10.0f, 10.0f, 40.0f, 40.0f, clear));
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);

	EXPECT_EQ(CountKind(list, UI::UICommandKind::PushShadow), 0u);
	EXPECT_TRUE(list.shadowCasters.empty());
	EXPECT_EQ(list.shadowAtlasSizePx, (Vec2I{0, 0}));
	EXPECT_EQ(list.commands.size(), 1u);
}

TEST(UIPaintTest, ShadowTakesUncarriedOpacity) {
	UI::Context ctx(Desc(), nullptr);
	UI::DropShadow opaque = SOFT_DROP;
	opaque.color.a = 1.0f;

	UI::Style leafStyle = CasterAt(10.0f, 10.0f, 40.0f, 40.0f, opaque);
	leafStyle.opacity = 0.5f;
	MakeChild(ctx, ctx.GetRoot(), leafStyle);

	UI::Style groupStyle = CasterAt(80.0f, 10.0f, 40.0f, 40.0f, opaque);
	groupStyle.opacity = 0.5f;
	UI::ElementHandle group = MakeChild(ctx, ctx.GetRoot(), groupStyle);
	MakeChild(ctx, group, SolidBox(10.0f, 10.0f, Color::White()));
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);

	ASSERT_EQ(CountKind(list, UI::UICommandKind::PushLayer), 1u);
	Vector<f32> alphas{GetMemoryResource(MemTag::Debug)};
	for (size_t i = 0; i < list.commands.size(); ++i) {
		if (list.commands[i].kind == UI::UICommandKind::PushShadow) {
			alphas.push_back(ShadowQuadAfter(list, i).color0.a);
		}
	}
	ASSERT_EQ(alphas.size(), 2u);
	EXPECT_FLOAT_EQ(alphas[0], 1.0f);
	EXPECT_FLOAT_EQ(alphas[1], 0.5f);
}

TEST(UIPaintTest, OversizedCasterCastsNothing) {
	UI::Context ctx(Desc(3000, 200), nullptr);
	MakeChild(ctx, ctx.GetRoot(), CasterAt(0.0f, 10.0f, 2100.0f, 40.0f, SOFT_DROP));
	MakeChild(ctx, ctx.GetRoot(), CasterAt(2200.0f, 10.0f, 40.0f, 40.0f, SOFT_DROP));
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);

	ASSERT_EQ(list.shadowCasters.size(), 2u);
	EXPECT_EQ(list.shadowCasters[0].slot.size, (Vec2I{0, 0}));
	EXPECT_GT(list.shadowCasters[1].slot.size.x, 0);
	const size_t first = FirstOfKind(list, UI::UICommandKind::PushShadow);
	EXPECT_EQ(ShadowQuadAfter(list, first).color0.a, 0.0f);
	EXPECT_LE(list.shadowAtlasSizePx.x, 2048);
	EXPECT_GE(list.shadowAtlasSizePx.x, list.shadowCasters[1].slot.Right());
}

TEST(UIPaintTest, DropShadowScalesWithContent) {
	UI::ContextDesc desc = Desc();
	desc.contentScale = 2.0f;
	UI::Context ctx(desc, nullptr);
	MakeChild(ctx, ctx.GetRoot(), CasterAt(10.0f, 10.0f, 20.0f, 20.0f, SOFT_DROP));
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);

	ASSERT_EQ(list.shadowCasters.size(), 1u);
	const UI::UIShadowCaster& caster = list.shadowCasters[0];
	EXPECT_EQ(caster.source, (UI::Rect{20.0f, 20.0f, 40.0f, 40.0f}));
	EXPECT_FLOAT_EQ(caster.blurPx, 20.0f);
	const UI::UIQuad& shadow = ShadowQuadAfter(list, FirstOfKind(list, UI::UICommandKind::PushShadow));
	const f32 margin = static_cast<f32>(caster.marginPx);
	EXPECT_FLOAT_EQ(shadow.rect.x, 20.0f - margin + 8.0f);
	EXPECT_FLOAT_EQ(shadow.rect.y, 20.0f - margin + 12.0f);
}
