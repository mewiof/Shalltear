#include "TestsPCH.h"

#include "Shalltear/Core/Graphics/LODGroup.h"

using namespace Shalltear;

namespace {

	LODGroup MakeGroup(std::initializer_list<f32> heights) {
		LODGroup group;
		for (f32 height : heights) {
			group.AddLevel(height);
		}
		return group;
	}

} // anonymous namespace

TEST(LODGroupTest, ChooseWalksThresholdsThenCulls) {
	LODGroup group = MakeGroup({0.6f, 0.3f, 0.1f});
	EXPECT_EQ(group.Choose(2.0f).level, 0u);
	EXPECT_EQ(group.Choose(0.6f).level, 0u);
	EXPECT_EQ(group.Choose(0.59f).level, 1u);
	EXPECT_EQ(group.Choose(0.3f).level, 1u);
	EXPECT_EQ(group.Choose(0.2f).level, 2u);
	EXPECT_EQ(group.Choose(0.09f).level, LODChoice::NONE);
	for (f32 height : {2.0f, 0.4f, 0.2f}) {
		const LODChoice choice = group.Choose(height);
		EXPECT_EQ(choice.next, LODChoice::NONE) << height;
		EXPECT_EQ(choice.keep, 255u) << height;
	}
	EXPECT_EQ(MakeGroup({}).Choose(1.0f).level, LODChoice::NONE);
}

TEST(LODGroupTest, CrossFadeBandSitsAboveEachThreshold) {
	LODGroup group = MakeGroup({0.6f, 0.3f, 0.1f});
	group.EditLevel(1).fadeTransitionWidth = 0.5f;
	group.EditLevel(2).fadeTransitionWidth = 1.0f;
	EXPECT_EQ(group.Choose(0.35f).next, LODChoice::NONE);

	group.SetFadeMode(LODFadeMode::CrossFade);
	const LODChoice middle = group.Choose(0.375f);
	EXPECT_EQ(middle.level, 1u);
	EXPECT_EQ(middle.next, 2u);
	EXPECT_NEAR(middle.keep, 128, 1);
	const LODChoice edge = group.Choose(0.3f);
	EXPECT_EQ(edge.level, 1u);
	EXPECT_EQ(edge.keep, 0u);
	const LODChoice above = group.Choose(0.46f);
	EXPECT_EQ(above.next, LODChoice::NONE);
	EXPECT_EQ(above.keep, 255u);

	const LODChoice last = group.Choose(0.2f);
	EXPECT_EQ(last.level, 2u);
	EXPECT_EQ(last.next, LODChoice::NONE);
	EXPECT_NEAR(last.keep, 128, 1);
	EXPECT_EQ(group.Choose(0.7f).keep, 255u);
}

TEST(LODGroupTest, AnimatedCrossFadeRunsThenReverses) {
	LODGroup group = MakeGroup({0.6f, 0.3f});
	group.SetFadeMode(LODFadeMode::AnimatedCrossFade);
	group.SetCrossFadeDuration(0.5f);
	LODFadeState state;
	const LODChoice first = group.Choose(1.0f, state, 10.0);
	EXPECT_EQ(first.level, 0u);
	EXPECT_EQ(first.next, LODChoice::NONE);

	const LODChoice begun = group.Choose(0.4f, state, 10.0);
	EXPECT_EQ(begun.level, 0u);
	EXPECT_EQ(begun.next, 1u);
	EXPECT_EQ(begun.keep, 255u);
	const LODChoice half = group.Choose(0.4f, state, 10.25);
	EXPECT_NEAR(half.keep, 128, 1);
	EXPECT_EQ(group.Choose(0.4f, state, 10.25).keep, half.keep);

	const LODChoice back = group.Choose(1.0f, state, 10.375);
	EXPECT_EQ(back.level, 1u);
	EXPECT_EQ(back.next, 0u);
	EXPECT_NEAR(back.keep, 191, 1);
	const LODChoice done = group.Choose(1.0f, state, 10.75);
	EXPECT_EQ(done.level, 0u);
	EXPECT_EQ(done.next, LODChoice::NONE);
	EXPECT_EQ(done.keep, 255u);

	EXPECT_EQ(group.Choose(0.1f, state, 11.0).level, 0u);
	const LODChoice leaving = group.Choose(0.1f, state, 11.25);
	EXPECT_EQ(leaving.level, 0u);
	EXPECT_EQ(leaving.next, LODChoice::NONE);
	EXPECT_NEAR(leaving.keep, 128, 1);
	EXPECT_EQ(group.Choose(0.1f, state, 11.5).level, LODChoice::NONE);

	LODFadeState fresh;
	EXPECT_EQ(group.Choose(0.1f, fresh, 20.0).level, LODChoice::NONE);
	group.SetFadeMode(LODFadeMode::CrossFade);
	LODFadeState unused;
	EXPECT_EQ(group.Choose(0.4f, unused, 20.0).level, 1u);
	EXPECT_FALSE(unused.started);
}
