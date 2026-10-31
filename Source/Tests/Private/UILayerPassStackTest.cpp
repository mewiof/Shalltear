#include "TestsPCH.h"

#include "UI/LayerPassStack.h"

using namespace Shalltear;
using namespace Shalltear::UI;

namespace {

	constexpr RHI::TextureHandle WindowTarget{1, 1};
	constexpr RHI::TextureHandle LayerA{10, 1};
	constexpr RHI::TextureHandle LayerB{11, 1};
	constexpr RHI::TextureHandle NoLayer{};

	LayerPassStack Started(Vec2I size = {800, 600}) {
		LayerPassStack stack;
		stack.Reset(WindowTarget, size);
		return stack;
	}

} // namespace

TEST(UILayerPassStackTest, NestedScopesPopInOrder) {
	LayerPassStack stack = Started();
	stack.Push(LayerA, {200, 100}, {10.0f, 20.0f});
	stack.Push(LayerB, {50, 50}, {30.0f, 40.0f});
	EXPECT_EQ(stack.Depth(), 3u);

	LayerPassStack::PopResult inner = stack.Pop();
	EXPECT_TRUE(inner.popped);
	EXPECT_EQ(inner.closed.target, LayerB);
	EXPECT_EQ(inner.parent.target, LayerA);
	EXPECT_EQ(stack.CompositeSource(), LayerB);

	LayerPassStack::PopResult outer = stack.Pop();
	EXPECT_TRUE(outer.popped);
	EXPECT_EQ(outer.closed.target, LayerA);
	EXPECT_EQ(outer.parent.target, WindowTarget);
	EXPECT_EQ(stack.CompositeSource(), LayerA);
	EXPECT_EQ(stack.Depth(), 1u);
}

TEST(UILayerPassStackTest, PopWithNoMatchingPushIsRefused) {
	LayerPassStack stack = Started();
	LayerPassStack::PopResult pop = stack.Pop();
	EXPECT_FALSE(pop.popped);
	EXPECT_EQ(stack.Depth(), 1u);
}

TEST(UILayerPassStackTest, FailedLayerKeepsOuterPop) {
	LayerPassStack stack = Started();
	stack.Push(LayerA, {200, 100}, {10.0f, 20.0f});
	stack.Push(NoLayer, {50, 50}, {30.0f, 40.0f});
	EXPECT_EQ(stack.Depth(), 3u);

	LayerPassStack::PopResult inner = stack.Pop();
	EXPECT_TRUE(inner.popped);
	EXPECT_FALSE(inner.closed.layered);
	EXPECT_EQ(inner.parent.target, LayerA);

	LayerPassStack::PopResult outer = stack.Pop();
	EXPECT_TRUE(outer.popped);
	EXPECT_EQ(outer.closed.target, LayerA);
	EXPECT_EQ(outer.parent.target, WindowTarget);
	EXPECT_EQ(stack.Depth(), 1u);
}

TEST(UILayerPassStackTest, FailedScopeCompositesNothingNotSibling) {
	LayerPassStack stack = Started();
	stack.Push(LayerA, {200, 100}, {10.0f, 20.0f});
	stack.Pop();
	ASSERT_EQ(stack.CompositeSource(), LayerA);

	stack.Push(NoLayer, {50, 50}, {30.0f, 40.0f});
	stack.Pop();
	EXPECT_FALSE(stack.CompositeSource().IsValid());
	EXPECT_NE(stack.CompositeSource(), LayerA);
}

TEST(UILayerPassStackTest, DegradedScopeInheritsTarget) {
	LayerPassStack stack = Started({800, 600});
	const LayerPass& degraded = stack.Push(NoLayer, {50, 50}, {30.0f, 40.0f});
	EXPECT_EQ(degraded.target, WindowTarget);
	EXPECT_EQ(degraded.sizePx, Vec2I(800, 600));
	EXPECT_EQ(degraded.originPx.x, 0.0f);
	EXPECT_EQ(degraded.originPx.y, 0.0f);
	EXPECT_FALSE(degraded.layered);
}

TEST(UILayerPassStackTest, DegradedScopeInsideLayerInheritsThatLayer) {
	LayerPassStack stack = Started();
	stack.Push(LayerA, {200, 100}, {10.0f, 20.0f});
	const LayerPass& degraded = stack.Push(NoLayer, {50, 50}, {30.0f, 40.0f});
	EXPECT_EQ(degraded.target, LayerA);
	EXPECT_EQ(degraded.sizePx, Vec2I(200, 100));
	EXPECT_EQ(degraded.originPx.x, 10.0f);
	EXPECT_EQ(degraded.originPx.y, 20.0f);
}

TEST(UILayerPassStackTest, UnopenableLayerDegrades) {
	LayerPassStack stack = Started();
	stack.Push(LayerA, {200, 100}, {10.0f, 20.0f});
	const LayerPass& degraded = stack.DegradeTop();
	EXPECT_EQ(stack.Depth(), 2u);
	EXPECT_EQ(degraded.target, WindowTarget);
	EXPECT_FALSE(degraded.layered);

	LayerPassStack::PopResult pop = stack.Pop();
	EXPECT_TRUE(pop.popped);
	EXPECT_EQ(pop.parent.target, WindowTarget);
	EXPECT_FALSE(stack.CompositeSource().IsValid());
	EXPECT_EQ(stack.Depth(), 1u);
}

TEST(UILayerPassStackTest, FailedScopesRestoreStack) {
	LayerPassStack stack = Started();
	stack.Push(NoLayer, {50, 50}, {30.0f, 40.0f});
	stack.Push(LayerA, {200, 100}, {10.0f, 20.0f});
	stack.Push(NoLayer, {20, 20}, {5.0f, 5.0f});
	EXPECT_EQ(stack.Depth(), 4u);

	EXPECT_FALSE(stack.Pop().closed.layered);
	EXPECT_FALSE(stack.CompositeSource().IsValid());
	EXPECT_TRUE(stack.Pop().closed.layered);
	EXPECT_EQ(stack.CompositeSource(), LayerA);
	EXPECT_FALSE(stack.Pop().closed.layered);
	EXPECT_FALSE(stack.CompositeSource().IsValid());

	EXPECT_EQ(stack.Depth(), 1u);
	EXPECT_EQ(stack.Top().target, WindowTarget);
}
