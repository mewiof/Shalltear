#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
	}

	UI::DragPayload AssetPayload(i32 id) {
		UI::DragPayload payload;
		payload.type = "Asset"_hash;
		payload.data = Variant(id);
		payload.display = "asset";
		return payload;
	}

} // anonymous namespace

TEST(DragDropTest, FirstHandlerUpTheChainDecides) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style rootStyle;
	rootStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), rootStyle);

	UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 40.0f));
	UI::ElementHandle label = MakeChild(ctx, row, FixedBox(100.0f, 40.0f));
	ctx.Update();

	int queries = 0;
	ctx.Events(row).onDragOver = [&](const UI::DragPayload& payload) {
		++queries;
		return payload.type == "Asset"_hash;
	};

	UI::ElementHandle target = ctx.UpdateDragTarget(AssetPayload(7), {50.0f, 20.0f});
	EXPECT_EQ(target, row);
	EXPECT_EQ(queries, 1);

	ctx.ClearDragTarget();
}

TEST(DragDropTest, DropStylingAppliesWhileTargetedAndReverts) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 40.0f));
	ctx.AddClass(row, StringHash("dz"));
	ctx.LoadStyleSheet(".dz:drop { opacity: 0.5; }");
	ctx.Events(row).onDragOver = [](const UI::DragPayload&) { return true; };
	ctx.Update();

	EXPECT_FLOAT_EQ(ctx.GetComputedStyle(row).opacity, 1.0f);
	ctx.UpdateDragTarget(AssetPayload(1), {100.0f, 20.0f});
	EXPECT_FLOAT_EQ(ctx.GetComputedStyle(row).opacity, 0.5f);
	ctx.ClearDragTarget();
	EXPECT_FLOAT_EQ(ctx.GetComputedStyle(row).opacity, 1.0f);
}

TEST(DragDropTest, RejectingHandlerBlocksAncestors) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle outer = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 100.0f));
	UI::ElementHandle inner = MakeChild(ctx, outer, FixedBox(100.0f, 50.0f));
	ctx.Update();

	bool outerQueried = false;
	ctx.Events(outer).onDragOver = [&](const UI::DragPayload&) {
		outerQueried = true;
		return true;
	};
	ctx.Events(inner).onDragOver = [](const UI::DragPayload& payload) {
		return payload.type == "SceneObject"_hash;
	};

	UI::ElementHandle target = ctx.UpdateDragTarget(AssetPayload(1), {50.0f, 25.0f});
	EXPECT_FALSE(target.IsValid());
	EXPECT_FALSE(outerQueried);
}

TEST(DragDropTest, DeliverDropFiresTheAcceptedTarget) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 40.0f));
	ctx.Update();

	i32 received = 0;
	str receivedDisplay;
	ctx.Events(row).onDragOver = [](const UI::DragPayload&) { return true; };
	ctx.Events(row).onDrop = [&](const UI::DragPayload& payload, Vec2) {
		received = payload.data.As<i32>();
		receivedDisplay = payload.display;
	};

	EXPECT_TRUE(ctx.DeliverDrop(AssetPayload(42), {100.0f, 20.0f}));
	EXPECT_EQ(received, 42);
	EXPECT_EQ(receivedDisplay, "asset");

	EXPECT_EQ(ctx.GetComputedStyle(row).opacity, 1.0f);
}

TEST(DragDropTest, DropPointReachesHandlerNotTrackedPointer) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 40.0f));
	ctx.Update();

	Vec2 seen{-1.0f, -1.0f};
	ctx.Events(row).onDragOver = [](const UI::DragPayload&) { return true; };
	ctx.Events(row).onDrop = [&](const UI::DragPayload&, Vec2 point) { seen = point; };

	InputEvent move{};
	move.type = InputEventType::MouseMove;
	move.mousePosPx = {5, 5};
	ctx.InjectInput(move);
	ctx.Update();

	ASSERT_TRUE(ctx.DeliverDrop(AssetPayload(1), {137.0f, 21.0f}));
	EXPECT_FLOAT_EQ(seen.x, 137.0f);
	EXPECT_FLOAT_EQ(seen.y, 21.0f);
	EXPECT_NE(ctx.GetPointerPosition().x, seen.x);
}

TEST(DragDropTest, DeliverDropWithoutAcceptorReturnsFalse) {
	UI::Context ctx(Desc(), nullptr);
	MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 40.0f));
	ctx.Update();

	EXPECT_FALSE(ctx.DeliverDrop(AssetPayload(1), {100.0f, 20.0f}));
}

TEST(DragDropTest, PayloadVariantDeepCopies) {
	UI::DragPayload copied;
	{
		UI::DragPayload original;
		original.type = "Asset"_hash;
		original.data = Variant(str("payload-string-larger-than-sbo-threshold-000000000000"));
		original.display = "ghost";
		copied = original;
	}
	EXPECT_EQ(copied.type, "Asset"_hash);
	EXPECT_EQ(copied.data.As<str>(), "payload-string-larger-than-sbo-threshold-000000000000");
	EXPECT_EQ(copied.display, "ghost");
}
