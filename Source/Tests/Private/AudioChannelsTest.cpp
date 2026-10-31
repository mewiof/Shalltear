#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioChannels.h"

#include <cmath>

using namespace Shalltear;
using namespace Shalltear::AudioImpl;

namespace {

	using P = SpeakerPosition;

	constexpr f32 FOLD = 0.70710678f;

	struct Gains {
		f32 values[MAX_AUDIO_CHANNELS * MAX_AUDIO_CHANNELS]{};
		u32 inputs = 0;

		f32 At(u32 output, u32 input) const { return values[output * inputs + input]; }
	};

	Gains Compute(const ChannelLayout& in, const ChannelLayout& out, f32 pan) {
		Gains gains;
		gains.inputs = in.count;
		ComputeChannelGains(in, out, pan, gains.values);
		return gains;
	}

	ChannelLayout Layout(std::initializer_list<P> positions) {
		ChannelLayout layout;
		for (P position : positions) {
			layout.positions[layout.count++] = position;
		}
		return layout;
	}

} // namespace

TEST(AudioChannelsTest, MonoPannedAtConstantPower) {
	const ChannelLayout mono = MakeStandardLayout(1);
	const ChannelLayout stereo = MakeStandardLayout(2);

	const Gains centre = Compute(mono, stereo, 0.0f);
	EXPECT_NEAR(centre.At(0, 0), FOLD, 1e-6f);
	EXPECT_NEAR(centre.At(1, 0), FOLD, 1e-6f);

	const Gains left = Compute(mono, stereo, -1.0f);
	EXPECT_NEAR(left.At(0, 0), 1.0f, 1e-6f);
	EXPECT_NEAR(left.At(1, 0), 0.0f, 1e-6f);

	const Gains right = Compute(mono, stereo, 1.0f);
	EXPECT_NEAR(right.At(0, 0), 0.0f, 1e-6f);
	EXPECT_NEAR(right.At(1, 0), 1.0f, 1e-6f);

	for (f32 pan = -1.0f; pan <= 1.0f; pan += 0.125f) {
		const Gains swept = Compute(mono, stereo, pan);
		EXPECT_NEAR(swept.At(0, 0) * swept.At(0, 0) + swept.At(1, 0) * swept.At(1, 0), 1.0f, 1e-5f) << "pan " << pan;
	}
}

TEST(AudioChannelsTest, StereoSourceIsBalancedAndNeverBoosted) {
	const ChannelLayout stereo = MakeStandardLayout(2);

	const Gains centred = Compute(stereo, stereo, 0.0f);
	EXPECT_EQ(centred.At(0, 0), 1.0f);
	EXPECT_EQ(centred.At(1, 1), 1.0f);
	EXPECT_EQ(centred.At(0, 1), 0.0f);
	EXPECT_EQ(centred.At(1, 0), 0.0f);

	const Gains halfRight = Compute(stereo, stereo, 0.5f);
	EXPECT_NEAR(halfRight.At(0, 0), 0.5f, 1e-6f);
	EXPECT_EQ(halfRight.At(1, 1), 1.0f);

	const Gains beyond = Compute(stereo, stereo, -7.0f);
	EXPECT_EQ(beyond.At(0, 0), 1.0f);
	EXPECT_EQ(beyond.At(1, 1), 0.0f);
}

TEST(AudioChannelsTest, FivePointOneFoldsToStereo) {
	const Gains gains = Compute(MakeStandardLayout(6), MakeStandardLayout(2), 0.0f);
	EXPECT_EQ(gains.At(0, 0), 1.0f);
	EXPECT_EQ(gains.At(1, 1), 1.0f);
	EXPECT_NEAR(gains.At(0, 2), FOLD, 1e-6f);
	EXPECT_NEAR(gains.At(1, 2), FOLD, 1e-6f);
	EXPECT_EQ(gains.At(0, 3), 0.0f);
	EXPECT_EQ(gains.At(1, 3), 0.0f);
	EXPECT_NEAR(gains.At(0, 4), FOLD, 1e-6f);
	EXPECT_EQ(gains.At(1, 4), 0.0f);
	EXPECT_NEAR(gains.At(1, 5), FOLD, 1e-6f);
	EXPECT_EQ(gains.At(0, 5), 0.0f);
}

TEST(AudioChannelsTest, BackSurroundMovesToSides) {
	const ChannelLayout backs = Layout({P::FrontLeft, P::FrontRight, P::FrontCenter, P::LowFrequency, P::BackLeft, P::BackRight});
	const ChannelLayout sides = MakeStandardLayout(6);
	const Gains gains = Compute(backs, sides, 0.0f);
	EXPECT_EQ(gains.At(4, 4), 1.0f);
	EXPECT_EQ(gains.At(5, 5), 1.0f);
	EXPECT_EQ(gains.At(3, 3), 1.0f);
	EXPECT_EQ(gains.At(0, 4), 0.0f);
}

TEST(AudioChannelsTest, StereoFoldsOntoMono) {
	const Gains gains = Compute(MakeStandardLayout(2), MakeStandardLayout(1), 0.0f);
	EXPECT_NEAR(gains.At(0, 0), FOLD, 1e-6f);
	EXPECT_NEAR(gains.At(0, 1), FOLD, 1e-6f);
}

TEST(AudioChannelsTest, TheStandardLayoutsAreTheWaveDefaults) {
	EXPECT_EQ(MakeStandardLayout(1).positions[0], P::Mono);
	const ChannelLayout four = MakeStandardLayout(4);
	EXPECT_EQ(four.positions[2], P::FrontCenter);
	EXPECT_EQ(four.positions[3], P::BackCenter);
	const ChannelLayout six = MakeStandardLayout(6);
	EXPECT_EQ(six.positions[3], P::LowFrequency);
	EXPECT_EQ(six.positions[4], P::SideLeft);
	EXPECT_EQ(six.positions[5], P::SideRight);
	const ChannelLayout eight = MakeStandardLayout(8);
	EXPECT_EQ(eight.positions[4], P::BackLeft);
	EXPECT_EQ(eight.positions[7], P::SideRight);
	EXPECT_EQ(MakeStandardLayout(0).count, 0u);
	EXPECT_EQ(MakeStandardLayout(9).count, MAX_AUDIO_CHANNELS);
}

#endif
