#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"
#include "AudioOfflineMixer.h"

#include "AudioBuiltinSpatializer.h"
#include "AudioMailbox.h"
#include "AudioPlacement.h"
#include "AudioScenePass.h"
#include "AudioSettings.h"
#include "AudioSpatializerRegistry.h"

#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioSource.h"
#include "Shalltear/Audio/AudioSpatializer.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <thread>
#include <vector>

using namespace AudioTesting;

namespace {

	const f32 CENTRE = std::sqrt(0.5f);

	std::vector<f32> Pan(const ChannelLayout& output, const Vec3& direction) {
		const PanLayout layout = MakePanLayout(output);
		std::vector<f32> gains(output.count, -1.0f);
		PanDirection(layout, direction, gains.data());
		return gains;
	}

	f32 Power(const std::vector<f32>& gains) {
		f32 sum = 0.0f;
		for (f32 gain : gains) {
			sum += gain * gain;
		}
		return sum;
	}

	struct SpatialBlock {
		std::vector<f32> in;
		std::vector<f32> out;
		u32 inputs = 0;
		u32 outputs = 0;

		SpatialBlock(u32 inputChannels, u32 outputChannels)
			: in(static_cast<size_t>(inputChannels) * OfflineMixer::BLOCK, 0.0f), out(static_cast<size_t>(outputChannels) * OfflineMixer::BLOCK, -1.0f),
			  inputs(inputChannels), outputs(outputChannels) {}

		f32& In(u32 channel, u32 frame) { return in[static_cast<size_t>(channel) * OfflineMixer::BLOCK + frame]; }
		f32 Out(u32 channel, u32 frame) const { return out[static_cast<size_t>(channel) * OfflineMixer::BLOCK + frame]; }

		void Run(IAudioSpatializer& spatializer, void* state, const AudioSpatialParams& params) {
			const AudioBlock input{in.data(), inputs, OfflineMixer::BLOCK, OfflineMixer::BLOCK};
			AudioBlock output{out.data(), outputs, OfflineMixer::BLOCK, OfflineMixer::BLOCK};
			spatializer.Process(state, params, input, output);
		}
	};

	AudioSpatializerContext ContextFor(u32 inputs, u32 outputs, u32 rate = 48000) {
		AudioSpatializerContext context;
		context.sampleRate = rate;
		context.blockFrames = OfflineMixer::BLOCK;
		const ChannelLayout in = MakeStandardLayout(inputs);
		const ChannelLayout out = MakeStandardLayout(outputs);
		context.inputChannels = in.count;
		context.outputChannels = out.count;
		for (u32 channel = 0; channel < in.count; ++channel) {
			context.inputSpeakers[channel] = in.positions[channel];
		}
		for (u32 channel = 0; channel < out.count; ++channel) {
			context.outputSpeakers[channel] = out.positions[channel];
		}
		return context;
	}

	class CountingSpatializer final : public IAudioSpatializer {
	public:
		void* CreateVoiceState(const AudioSpatializerContext& context) override {
			++created;
			return _inner.CreateVoiceState(context);
		}
		void DestroyVoiceState(void* state) override {
			++destroyed;
			_inner.DestroyVoiceState(state);
		}
		void Process(void* state, const AudioSpatialParams& params, const AudioBlock& in, AudioBlock& out) [[clang::nonblocking]] override {
			blocks.fetch_add(1, std::memory_order_relaxed);
			_inner.Process(state, params, in, out);
		}

		u32 created = 0;
		u32 destroyed = 0;
		std::atomic<u64> blocks{0};
	private:
		BuiltinSpatializer _inner;
	};

	AudioVoicePlacement Placed(IAudioSpatializer* spatializer, void* state, const Vec3& position, f32 blend = 1.0f, f32 gain = 1.0f, f32 doppler = 1.0f) {
		AudioVoicePlacement placement;
		placement.spatializer = spatializer;
		placement.state = state;
		placement.params.position = position;
		placement.params.distanceAttenuation = gain;
		placement.blend = blend;
		placement.gain = gain;
		placement.doppler = doppler;
		return placement;
	}

} // namespace

TEST(AudioSpatialTest, LogRolloffIsInverseLaw) {
	constexpr AudioRolloffMode LOG = AudioRolloffMode::Logarithmic;
	EXPECT_EQ(ComputeDistanceAttenuation(LOG, 0.5f, 1.0f, 10.0f, 1.0f), 1.0f);
	EXPECT_EQ(ComputeDistanceAttenuation(LOG, 1.0f, 1.0f, 10.0f, 1.0f), 1.0f);
	EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(LOG, 2.0f, 1.0f, 10.0f, 1.0f), 0.5f);
	EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(LOG, 4.0f, 1.0f, 10.0f, 1.0f), 0.25f);
	EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(LOG, 100.0f, 1.0f, 10.0f, 1.0f), 0.01f);
	EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(LOG, 2.0f, 1.0f, 10.0f, 2.0f), 1.0f / 3.0f);
	EXPECT_EQ(ComputeDistanceAttenuation(LOG, 50.0f, 1.0f, 10.0f, 0.0f), 1.0f);
}

TEST(AudioSpatialTest, LinearRolloffSilentAtMax) {
	constexpr AudioRolloffMode LINEAR = AudioRolloffMode::Linear;
	EXPECT_EQ(ComputeDistanceAttenuation(LINEAR, 1.0f, 2.0f, 10.0f, 1.0f), 1.0f);
	EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(LINEAR, 6.0f, 2.0f, 10.0f, 1.0f), 0.5f);
	EXPECT_EQ(ComputeDistanceAttenuation(LINEAR, 10.0f, 2.0f, 10.0f, 1.0f), 0.0f);
	EXPECT_EQ(ComputeDistanceAttenuation(LINEAR, 20.0f, 2.0f, 10.0f, 1.0f), 0.0f);
	EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(LINEAR, 6.0f, 2.0f, 10.0f, 7.0f), 0.5f);
}

TEST(AudioSpatialTest, VoiceUsesNearestListener) {
	std::vector<AudioListenerPose> listeners(2);
	listeners[0].position = Vec3(0.0f, 0.0f, 0.0f);
	listeners[0].rotation = Quat::Identity();
	listeners[1].position = Vec3(20.0f, 0.0f, 0.0f);
	listeners[1].rotation = Quat::LookRotation(Vec3(-1.0f, 0.0f, 0.0f));

	EXPECT_EQ(FindNearestListener(listeners, Vec3(2.0f, 0.0f, 0.0f)), 0);
	EXPECT_EQ(FindNearestListener(listeners, Vec3(18.0f, 0.0f, 0.0f)), 1);
	EXPECT_EQ(FindNearestListener({}, Vec3(18.0f, 0.0f, 0.0f)), -1);

	AudioSpatialSettings settings;
	settings.spatialBlend = 1.0f;
	const AudioVoicePlacement placement = ComputePlacement(settings, Vec3(18.0f, 0.0f, 1.0f), Vec3{}, listeners, 1.0f, 1.0f, false);
	EXPECT_NEAR(placement.params.position.x, 1.0f, 1e-5f);
	EXPECT_NEAR(placement.params.position.y, 0.0f, 1e-5f);
	EXPECT_NEAR(placement.params.position.z, 2.0f, 1e-5f);
	EXPECT_FLOAT_EQ(placement.params.distanceAttenuation, 1.0f / std::sqrt(5.0f));
	EXPECT_FLOAT_EQ(placement.gain, placement.params.distanceAttenuation);

	const AudioVoicePlacement ownGain = ComputePlacement(settings, Vec3(18.0f, 0.0f, 1.0f), Vec3{}, listeners, 1.0f, 1.0f, true);
	EXPECT_EQ(ownGain.gain, 1.0f);
	EXPECT_FLOAT_EQ(ownGain.params.distanceAttenuation, placement.params.distanceAttenuation);
}

TEST(AudioSpatialTest, DopplerFollowsMotionAlongLineBetweenThem) {
	const Vec3 source(0.0f, 0.0f, 10.0f);
	const Vec3 listener(0.0f, 0.0f, 0.0f);
	const f32 v = SPEED_OF_SOUND * 0.1f;
	EXPECT_FLOAT_EQ(ComputeDopplerRatio(source, Vec3(0.0f, 0.0f, -v), listener, Vec3{}, 1.0f, 1.0f), SPEED_OF_SOUND / (SPEED_OF_SOUND - v));
	EXPECT_FLOAT_EQ(ComputeDopplerRatio(source, Vec3(0.0f, 0.0f, v), listener, Vec3{}, 1.0f, 1.0f), SPEED_OF_SOUND / (SPEED_OF_SOUND + v));
	EXPECT_FLOAT_EQ(ComputeDopplerRatio(source, Vec3{}, listener, Vec3(0.0f, 0.0f, v), 1.0f, 1.0f), (SPEED_OF_SOUND + v) / SPEED_OF_SOUND);
	EXPECT_FLOAT_EQ(ComputeDopplerRatio(source, Vec3(v, 0.0f, 0.0f), listener, Vec3{}, 1.0f, 1.0f), 1.0f);
	EXPECT_EQ(ComputeDopplerRatio(source, Vec3(0.0f, 0.0f, -v), listener, Vec3{}, 1.0f, 0.0f), 1.0f);
	EXPECT_EQ(ComputeDopplerRatio(source, Vec3(0.0f, 0.0f, -v), listener, Vec3{}, 0.0f, 1.0f), 1.0f);
	const f32 full = SPEED_OF_SOUND / (SPEED_OF_SOUND - v);
	EXPECT_FLOAT_EQ(ComputeDopplerRatio(source, Vec3(0.0f, 0.0f, -v), listener, Vec3{}, 1.0f, 0.5f), 1.0f + (full - 1.0f) * 0.5f);
	EXPECT_FLOAT_EQ(ComputeDopplerRatio(source, Vec3(0.0f, 0.0f, -v), listener, Vec3{}, 2.0f, 1.0f),
					SPEED_OF_SOUND / (SPEED_OF_SOUND - 2.0f * v));
	EXPECT_EQ(ComputeDopplerRatio(listener, Vec3(0.0f, 0.0f, -v), listener, Vec3{}, 1.0f, 1.0f), 1.0f);
	const f32 extreme = ComputeDopplerRatio(source, Vec3(0.0f, 0.0f, -1.0e6f), listener, Vec3(0.0f, 0.0f, 1.0e6f), 1.0f, 5.0f);
	EXPECT_EQ(extreme, MAX_DOPPLER_RATIO);
	EXPECT_FLOAT_EQ(ComputeDopplerRatio(source, Vec3(0.0f, 0.0f, -1.0e6f), listener, Vec3(0.0f, 0.0f, 1.0e6f), 1.0f, 1.0f), 3.0f);

	AudioSpatialSettings settings;
	settings.spatialBlend = 0.5f;
	const std::vector<AudioListenerPose> one(1);
	const AudioVoicePlacement half = ComputePlacement(settings, source, Vec3(0.0f, 0.0f, -v), one, 1.0f, 1.0f, false);
	EXPECT_FLOAT_EQ(half.doppler, 1.0f + (full - 1.0f) * 0.5f);
}

TEST(AudioSpatialTest, BrokenPosePlacedAtListener) {
	AudioSpatialSettings settings;
	settings.spatialBlend = 1.0f;
	const std::vector<AudioListenerPose> one(1);
	const f32 nan = std::nanf("");
	const AudioVoicePlacement placement = ComputePlacement(settings, Vec3(nan, 1.0f, 2.0f), Vec3(nan, nan, nan), one, 1.0f, 1.0f, false);
	EXPECT_TRUE(std::isfinite(placement.params.position.x) && std::isfinite(placement.params.position.z));
	EXPECT_EQ(placement.params.distanceAttenuation, 1.0f);
	EXPECT_EQ(placement.doppler, 1.0f);
}

TEST(AudioSpatialTest, PanLawAtCentreLeftAndRight) {
	const ChannelLayout stereo = MakeStandardLayout(2);
	const std::vector<f32> ahead = Pan(stereo, Vec3(0.0f, 0.0f, 1.0f));
	EXPECT_FLOAT_EQ(ahead[0], CENTRE);
	EXPECT_FLOAT_EQ(ahead[1], CENTRE);
	const std::vector<f32> right = Pan(stereo, Vec3(3.0f, 0.0f, 0.0f));
	EXPECT_EQ(right[0], 0.0f);
	EXPECT_EQ(right[1], 1.0f);
	const std::vector<f32> left = Pan(stereo, Vec3(-3.0f, 0.0f, 0.0f));
	EXPECT_EQ(left[0], 1.0f);
	EXPECT_EQ(left[1], 0.0f);
	for (const Vec3& centred : {Vec3(0.0f, 0.0f, -2.0f), Vec3(0.0f, 5.0f, 0.0f), Vec3{}}) {
		const std::vector<f32> gains = Pan(stereo, centred);
		EXPECT_FLOAT_EQ(gains[0], CENTRE);
		EXPECT_FLOAT_EQ(gains[1], CENTRE);
	}
	const std::vector<f32> diagonal = Pan(stereo, Vec3(1.0f, 0.0f, 1.0f));
	EXPECT_NEAR(diagonal[0], 0.38268343f, 1e-6f);
	EXPECT_NEAR(diagonal[1], 0.92387953f, 1e-6f);
	EXPECT_NEAR(Power(diagonal), 1.0f, 1e-6f);
}

TEST(AudioSpatialTest, SurroundPansBetweenNeighbours) {
	const ChannelLayout surround = MakeStandardLayout(6);
	const std::vector<f32> ahead = Pan(surround, Vec3(0.0f, 0.0f, 1.0f));
	EXPECT_NEAR(ahead[2], 1.0f, 1e-6f);
	EXPECT_NEAR(Power(ahead), 1.0f, 1e-6f);
	const std::vector<f32> front = Pan(surround, Vec3(0.5f, 0.0f, 0.86602540f));
	EXPECT_NEAR(front[1], 1.0f, 1e-5f);
	const std::vector<f32> between = Pan(surround, Vec3(0.93969262f, 0.0f, 0.34202014f));
	EXPECT_NEAR(between[1], CENTRE, 1e-5f);
	EXPECT_NEAR(between[5], CENTRE, 1e-5f);
	EXPECT_NEAR(between[0] + between[2] + between[4], 0.0f, 1e-6f);
	const std::vector<f32> behind = Pan(surround, Vec3(0.0f, 0.0f, -4.0f));
	EXPECT_NEAR(behind[4], CENTRE, 1e-5f);
	EXPECT_NEAR(behind[5], CENTRE, 1e-5f);
	const std::vector<f32> above = Pan(surround, Vec3(0.0f, 1.0f, 0.0f));
	for (u32 channel : {0u, 1u, 2u, 4u, 5u}) {
		EXPECT_NEAR(above[channel], std::sqrt(0.2f), 1e-6f) << channel;
	}
	for (const std::vector<f32>* gains : {&ahead, &front, &between, &behind, &above}) {
		EXPECT_EQ((*gains)[3], 0.0f);
	}
}

TEST(AudioSpatialTest, FrontOnlyFoldsRearForward) {
	const ChannelLayout front = MakeStandardLayout(3);
	const std::vector<f32> behindRight = Pan(front, Vec3(0.70710678f, 0.0f, -0.70710678f));
	EXPECT_NEAR(behindRight[1], 1.0f, 1e-6f);
	EXPECT_NEAR(behindRight[0] + behindRight[2], 0.0f, 1e-6f);
}

TEST(AudioSpatialTest, SpreadSplitsStereoOnly) {
	BuiltinSpatializer builtin;
	AudioSpatialParams params;
	params.position = Vec3(0.0f, 0.0f, 2.0f);

	const auto leftChannel = [&](f32 spread, u32 frame, u32 output) {
		void* state = builtin.CreateVoiceState(ContextFor(2, 2));
		SpatialBlock block(2, 2);
		for (u32 index = 0; index < OfflineMixer::BLOCK; ++index) {
			block.In(0, index) = 1.0f;
		}
		params.spread = spread;
		block.Run(builtin, state, params);
		builtin.DestroyVoiceState(state);
		return block.Out(output, frame);
	};
	EXPECT_NEAR(leftChannel(0.0f, 255, 0), CENTRE * CENTRE, 1e-6f);
	EXPECT_NEAR(leftChannel(0.0f, 255, 1), CENTRE * CENTRE, 1e-6f);
	EXPECT_NEAR(leftChannel(180.0f, 255, 0), CENTRE, 1e-4f);
	EXPECT_NEAR(leftChannel(180.0f, 255, 1), 0.0f, 1e-4f);
	EXPECT_NEAR(leftChannel(360.0f, 255, 0), CENTRE * CENTRE, 1e-4f);

	void* state = builtin.CreateVoiceState(ContextFor(1, 2));
	SpatialBlock mono(1, 2);
	for (u32 index = 0; index < OfflineMixer::BLOCK; ++index) {
		mono.In(0, index) = 1.0f;
	}
	params.spread = 180.0f;
	mono.Run(builtin, state, params);
	EXPECT_FLOAT_EQ(mono.Out(0, 255), CENTRE);
	EXPECT_FLOAT_EQ(mono.Out(1, 255), CENTRE);
	builtin.DestroyVoiceState(state);
}

TEST(AudioSpatialTest, OcclusionRangesNoneToMuffle) {
	BuiltinSpatializer builtin;
	AudioSpatialParams params;
	params.position = Vec3(0.0f, 0.0f, 1.0f);

	void* open = builtin.CreateVoiceState(ContextFor(1, 2));
	SpatialBlock clear(1, 2);
	for (u32 index = 0; index < OfflineMixer::BLOCK; ++index) {
		clear.In(0, index) = 0.3f * std::sin(0.05f * static_cast<f32>(index));
	}
	clear.Run(builtin, open, params);
	for (u32 index = 0; index < OfflineMixer::BLOCK; ++index) {
		ASSERT_EQ(clear.Out(0, index), CENTRE * clear.In(0, index)) << index;
	}
	builtin.DestroyVoiceState(open);

	params.occlusion = 1.0f;
	const auto settle = [&](bool alternating) {
		void* state = builtin.CreateVoiceState(ContextFor(1, 2));
		SpatialBlock block(1, 2);
		f32 loudest = 0.0f;
		for (u32 pass = 0; pass < 4; ++pass) {
			for (u32 index = 0; index < OfflineMixer::BLOCK; ++index) {
				block.In(0, index) = alternating && (index % 2 == 1) ? -1.0f : 1.0f;
			}
			block.Run(builtin, state, params);
		}
		for (u32 index = OfflineMixer::BLOCK / 2; index < OfflineMixer::BLOCK; ++index) {
			loudest = std::max(loudest, std::fabs(block.Out(0, index)));
		}
		builtin.DestroyVoiceState(state);
		return loudest;
	};
	const f32 steady = settle(false);
	EXPECT_NEAR(steady, OCCLUSION_GAIN * CENTRE, 1e-4f);
	EXPECT_LT(settle(true), steady * 0.1f);
}

TEST(AudioSpatialTest, MovingSourceWalksAcrossBlockNotStepping) {
	BuiltinSpatializer builtin;
	void* state = builtin.CreateVoiceState(ContextFor(1, 2));
	SpatialBlock block(1, 2);
	for (u32 index = 0; index < OfflineMixer::BLOCK; ++index) {
		block.In(0, index) = 1.0f;
	}
	AudioSpatialParams params;
	params.position = Vec3(1.0f, 0.0f, 0.0f);
	block.Run(builtin, state, params);
	EXPECT_EQ(block.Out(0, 0), 0.0f);

	params.position = Vec3(-1.0f, 0.0f, 0.0f);
	block.Run(builtin, state, params);
	EXPECT_LT(block.Out(0, 0), 0.01f);
	EXPECT_EQ(block.Out(0, OfflineMixer::BLOCK - 1), 1.0f);
	for (u32 index = 1; index < OfflineMixer::BLOCK; ++index) {
		ASSERT_GE(block.Out(0, index), block.Out(0, index - 1)) << index;
	}
	builtin.DestroyVoiceState(state);
}

TEST(AudioSpatialTest, ZeroBlendMixesAs2D) {
	std::vector<f32> samples(48000);
	for (size_t index = 0; index < samples.size(); ++index) {
		samples[index] = 0.4f * std::sin(0.013f * static_cast<f32>(index));
	}
	ClipBlock clip(MakePcm(samples, 1, 44100));
	ASSERT_TRUE(clip);

	OfflineMixer flat;
	AudioCommand play = flat.Play(0, clip.Get());
	play.volume = 0.7f;
	play.stereoPan = 0.3f;
	play.pitch = 1.1f;
	ASSERT_TRUE(flat.Push(play));
	const std::vector<f32> expected = flat.Render(OfflineMixer::BLOCK * 8);

	OfflineMixer placed;
	void* state = GetBuiltinSpatializer().CreateVoiceState(placed.ContextFor(clip.Get()));
	AudioCommand same = placed.Play(0, clip.Get());
	same.volume = 0.7f;
	same.stereoPan = 0.3f;
	same.pitch = 1.1f;
	placed.Place(same, Placed(&GetBuiltinSpatializer(), state, Vec3(5.0f, 0.0f, 0.0f), 0.0f));
	ASSERT_TRUE(placed.Push(same));
	const std::vector<f32> actual = placed.Render(OfflineMixer::BLOCK * 8);

	ASSERT_EQ(actual.size(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index) {
		ASSERT_EQ(actual[index], expected[index]) << "sample " << index << " moved";
	}
	GetBuiltinSpatializer().DestroyVoiceState(state);
}

TEST(AudioSpatialTest, Voice3DPlacedFromFirstSample) {
	ClipBlock clip(MakePcm(std::vector<f32>(48000, 0.5f), 1, 48000));
	ASSERT_TRUE(clip);

	OfflineMixer mixer;
	void* state = GetBuiltinSpatializer().CreateVoiceState(mixer.ContextFor(clip.Get()));
	const AudioCommand play = mixer.Play(0, clip.Get());
	mixer.Place(play, Placed(&GetBuiltinSpatializer(), state, Vec3(2.0f, 0.0f, 0.0f)));
	ASSERT_TRUE(mixer.Push(play));
	const std::vector<f32> first = mixer.Render(OfflineMixer::BLOCK);
	EXPECT_EQ(At(first, 2, 0, 0), 0.0f);
	EXPECT_FLOAT_EQ(At(first, 2, 0, 1), 0.5f);
	EXPECT_FLOAT_EQ(At(first, 2, OfflineMixer::BLOCK - 1, 1), 0.5f);

	OfflineMixer control;
	ASSERT_TRUE(control.Push(control.Play(0, clip.Get())));
	const std::vector<f32> centred = control.Render(OfflineMixer::BLOCK);
	EXPECT_FLOAT_EQ(At(centred, 2, 0, 0), 0.5f * CENTRE);
	GetBuiltinSpatializer().DestroyVoiceState(state);
}

TEST(AudioSpatialTest, PackageAppliesDistancesGainAndDopplersRate) {
	ClipBlock clip(MakePcm(std::vector<f32>(480000, 0.5f), 1, 48000));
	ASSERT_TRUE(clip);

	OfflineMixer mixer;
	void* state = GetBuiltinSpatializer().CreateVoiceState(mixer.ContextFor(clip.Get()));
	const AudioCommand play = mixer.Play(0, clip.Get());
	mixer.Place(play, Placed(&GetBuiltinSpatializer(), state, Vec3(2.0f, 0.0f, 0.0f), 1.0f, 0.25f, 1.5f));
	ASSERT_TRUE(mixer.Push(play));
	const std::vector<f32> out = mixer.Render(OfflineMixer::BLOCK * 10);
	EXPECT_FLOAT_EQ(At(out, 2, 100, 1), 0.125f);
	EXPECT_EQ(mixer.Get().GetVoicePosition(0), 3840u);
	GetBuiltinSpatializer().DestroyVoiceState(state);
}

TEST(AudioSpatialTest, BuiltInAsPluginMatches) {
	std::vector<f32> samples(96000 * 2);
	for (size_t frame = 0; frame < samples.size() / 2; ++frame) {
		samples[frame * 2] = 0.3f * std::sin(0.021f * static_cast<f32>(frame));
		samples[frame * 2 + 1] = 0.2f * std::sin(0.047f * static_cast<f32>(frame));
	}
	ClipBlock clip(MakePcm(samples, 2, 48000));
	ASSERT_TRUE(clip);

	CountingSpatializer twin;
	char owner = 0;
	ASSERT_TRUE(AudioSpatializers::Register("Twin", &twin, &owner));
	EXPECT_EQ(FindSpatializer("Twin"), &twin);
	EXPECT_FALSE(AudioSpatializers::Register("Twin", &twin, &owner));

	const auto render = [&](IAudioSpatializer& spatializer) {
		OfflineMixer mixer(6);
		void* state = spatializer.CreateVoiceState(mixer.ContextFor(clip.Get()));
		const AudioCommand play = mixer.Play(0, clip.Get());
		std::vector<f32> all;
		for (u32 block = 0; block < 24; ++block) {
			const f32 angle = 0.4f * static_cast<f32>(block);
			AudioVoicePlacement placement = Placed(&spatializer, state, Vec3(3.0f * std::sin(angle), 0.5f, 3.0f * std::cos(angle)), 0.8f, 0.6f);
			placement.params.spread = static_cast<f32>(block * 15 % 360);
			placement.params.occlusion = static_cast<f32>(block % 5) * 0.25f;
			mixer.Place(play, placement);
			if (block == 0) {
				EXPECT_TRUE(mixer.Push(play));
			}
			const std::vector<f32> out = mixer.Render(OfflineMixer::BLOCK);
			all.insert(all.end(), out.begin(), out.end());
		}
		mixer.Get().Release();
		spatializer.DestroyVoiceState(state);
		return all;
	};
	const std::vector<f32> builtin = render(GetBuiltinSpatializer());
	const std::vector<f32> plugin = render(twin);
	ASSERT_EQ(builtin.size(), plugin.size());
	for (size_t index = 0; index < builtin.size(); ++index) {
		ASSERT_EQ(builtin[index], plugin[index]);
	}
	EXPECT_EQ(twin.blocks.load(), 24u);
	EXPECT_EQ(twin.created, twin.destroyed);

	EXPECT_EQ(AudioSpatializers::UnregisterOwnedBy(&owner), 1u);
	EXPECT_EQ(FindSpatializer("Twin"), nullptr);
}

TEST(AudioSpatialTest, UnregisterKeepsOtherOwners) {
	CountingSpatializer first;
	CountingSpatializer second;
	CountingSpatializer third;
	char kept = 0;
	char gone = 0;
	ASSERT_TRUE(AudioSpatializers::Register("First", &first, &kept));
	ASSERT_TRUE(AudioSpatializers::Register("Second", &second, &gone));
	ASSERT_TRUE(AudioSpatializers::Register("Third", &third, &kept));
	EXPECT_EQ(AudioSpatializers::UnregisterOwnedBy(&gone), 1u);
	EXPECT_EQ(FindSpatializer("First"), &first);
	EXPECT_EQ(FindSpatializer("Second"), nullptr);
	EXPECT_EQ(FindSpatializer("Third"), &third);
	EXPECT_EQ(AudioSpatializers::UnregisterOwnedBy(&kept), 2u);
	EXPECT_EQ(FindSpatializer("First"), nullptr);
}

TEST(AudioSpatialTest, Only3DBlocksSpatialized) {
	ClipBlock clip(MakePcm(std::vector<f32>(96000, 0.5f), 1, 48000));
	ASSERT_TRUE(clip);
	CountingSpatializer counting;
	OfflineMixer mixer;
	void* state = counting.CreateVoiceState(mixer.ContextFor(clip.Get()));
	const AudioCommand play = mixer.Play(0, clip.Get());
	mixer.Place(play, Placed(&counting, state, Vec3(1.0f, 0.0f, 0.0f), 0.0f));
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(OfflineMixer::BLOCK * 4);
	EXPECT_EQ(counting.blocks.load(), 0u);

	mixer.Place(play, Placed(&counting, state, Vec3(1.0f, 0.0f, 0.0f), 1.0f));
	mixer.Render(OfflineMixer::BLOCK * 3);
	EXPECT_EQ(counting.blocks.load(), 3u);
	mixer.Place(play, Placed(&counting, state, Vec3(1.0f, 0.0f, 0.0f), 0.0f));
	mixer.Render(OfflineMixer::BLOCK * 3);
	EXPECT_EQ(counting.blocks.load(), 4u);
	mixer.Get().Release();
	counting.DestroyVoiceState(state);
}

TEST(AudioSpatialTest, MailboxNeverTears) {
	struct Value {
		u64 count = 0;
		u64 thrice = 0;
		u64 fivefold = 0;
		u64 sevenfold = 0;
	};
	const auto make = [](u64 count) { return Value{count, count * 3, count * 5, count * 7}; };

	AudioMailbox<Value> single;
	Value taken;
	EXPECT_FALSE(single.Take(taken));
	single.Post(make(1));
	single.Post(make(2));
	ASSERT_TRUE(single.Take(taken));
	EXPECT_EQ(taken.count, 2u);
	EXPECT_FALSE(single.Take(taken));

	constexpr u64 POSTS = 200000;
	AudioMailbox<Value> shared;
	std::atomic<bool> torn{false};
	std::atomic<bool> backwards{false};
	std::atomic<u64> last{0};
	std::thread reader([&]() {
		u64 previous = 0;
		Value value;
		while (previous < POSTS) {
			if (!shared.Take(value)) {
				continue;
			}
			if (value.thrice != value.count * 3 || value.fivefold != value.count * 5 || value.sevenfold != value.count * 7) {
				torn.store(true);
			}
			if (value.count <= previous) {
				backwards.store(true);
			}
			previous = value.count;
		}
		last.store(previous);
	});
	for (u64 count = 1; count <= POSTS; ++count) {
		shared.Post(make(count));
	}
	reader.join();
	EXPECT_FALSE(torn.load());
	EXPECT_FALSE(backwards.load());
	EXPECT_EQ(last.load(), POSTS);
}

namespace {

	class AudioSpatialFixture : public LivePackageFixture {
	protected:
		Handle<Scene> _scene;
		char _mover = 0;

		void SetUp() override {
			LivePackageFixture::SetUp();
			_scene = SceneSystem::CreateScene("spatial");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			FrameStages::UnregisterOwnedBy(&_mover);
			LivePackageFixture::TearDown();
		}

		Handle<AudioSource> AddSource(AudioClip* clip, const Vec3& position, f32 blend = 1.0f) {
			Handle<SceneObject> object = SceneSystem::CreateObject("source", _scene);
			object->SetWorldPosition(position);
			Handle<AudioSource> source = object->AddComponent<AudioSource>();
			source->SetResource(ResourceOf(clip));
			source->SetLoop(true);
			source->SetSpatialBlend(blend);
			return source;
		}

		Handle<AudioListener> AddListener(const Vec3& position, const Quat& rotation = Quat::Identity()) {
			Handle<SceneObject> object = SceneSystem::CreateObject("listener", _scene);
			object->SetWorldPosition(position);
			object->SetWorldRotation(rotation);
			return object->AddComponent<AudioListener>();
		}

		static SceneObject* ObjectOf(Handle<AudioSource> source) { return source->GetOwner().Get(); }

		static const AudioVoiceRecord* TheVoice() {
			GetAudioSystem().DrainEvents();
			AudioVoiceTable& voices = GetAudioSystem().GetVoices();
			const AudioVoiceRecord* found = nullptr;
			for (u32 slot = 0; slot < voices.GetCapacity(); ++slot) {
				const AudioVoiceRecord* record = voices.Find(slot, voices.GetGeneration(slot));
				if (record && record->claim != VoiceClaim::Free) {
					if (found) {
						return nullptr;
					}
					found = record;
				}
			}
			return found;
		}

		static void SelectSpatializer(std::string_view name) {
			AudioSettings* settings = GetDeclaredSettings();
			ASSERT_NE(settings, nullptr);
			settings->spatializer = str(name);
			Settings::NotifyChanged(SETTINGS_CATEGORY_PATH, "spatializer");
		}
	};

} // namespace

TEST_F(AudioSpatialFixture, VoiceFollowsNearerListener) {
	AddListener(Vec3(0.0f, 0.0f, 0.0f));
	Handle<AudioListener> near = AddListener(Vec3(20.0f, 0.0f, 0.0f));
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	AddSource(clip, Vec3(18.0f, 0.0f, 1.0f));
	RunFrame();

	const AudioVoiceRecord* voice = TheVoice();
	ASSERT_NE(voice, nullptr);
	ASSERT_NE(voice->spatializerState, nullptr);
	EXPECT_EQ(voice->spatializer, &GetBuiltinSpatializer());
	EXPECT_NEAR(voice->placement.params.position.x, -2.0f, 1e-5f);
	EXPECT_NEAR(voice->placement.params.position.z, 1.0f, 1e-5f);

	SceneSystem::Destroy(Handle<Component>(near.Get()));
	RunFrame();
	RunFrame();
	voice = TheVoice();
	ASSERT_NE(voice, nullptr);
	EXPECT_NEAR(voice->placement.params.position.x, 18.0f, 1e-5f);
}

TEST_F(AudioSpatialFixture, MovingSourcePlacedEachFrame) {
	Handle<AudioListener> listener = AddListener(Vec3{});
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip, Vec3(3.0f, 0.0f, 0.0f));
	source->SetRolloffMode(AudioRolloffMode::Linear);
	source->SetMinDistance(1.0f);
	source->SetMaxDistance(5.0f);
	RunFrame();
	const AudioVoiceRecord* voice = TheVoice();
	ASSERT_NE(voice, nullptr);
	EXPECT_NEAR(voice->placement.params.position.x, 3.0f, 1e-5f);
	EXPECT_FLOAT_EQ(voice->placement.gain, 0.5f);

	ObjectOf(source)->SetWorldPosition(Vec3(-3.0f, 0.0f, 0.0f));
	RunFrame();
	EXPECT_NEAR(TheVoice()->placement.params.position.x, -3.0f, 1e-5f);

	ObjectOf(source)->SetWorldPosition(Vec3(0.0f, 0.0f, 4.0f));
	RunFrame();
	EXPECT_NEAR(TheVoice()->placement.params.position.z, 4.0f, 1e-5f);
	EXPECT_FLOAT_EQ(TheVoice()->placement.gain, 0.25f);

	ObjectOf(source)->SetWorldPosition(Vec3(3.0f, 0.0f, 0.0f));
	listener->GetOwner()->SetWorldRotation(Quat::LookRotation(Vec3(1.0f, 0.0f, 0.0f)));
	RunFrame();
	EXPECT_NEAR(TheVoice()->placement.params.position.x, 0.0f, 1e-4f);
	EXPECT_NEAR(TheVoice()->placement.params.position.z, 3.0f, 1e-4f);
}

TEST_F(AudioSpatialFixture, RaisedBlendAdds3DPart) {
	AddListener(Vec3{});
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip, Vec3(3.0f, 0.0f, 0.0f), 0.0f);
	RunFrame();
	ASSERT_NE(TheVoice(), nullptr);
	EXPECT_EQ(TheVoice()->spatializerState, nullptr);

	source->SetSpatialBlend(1.0f);
	RunFrame();
	ASSERT_NE(TheVoice()->spatializerState, nullptr);
	EXPECT_EQ(TheVoice()->placement.blend, 1.0f);

	source->SetSpatialBlend(0.0f);
	RunFrame();
	EXPECT_NE(TheVoice()->spatializerState, nullptr);
	EXPECT_EQ(TheVoice()->placement.blend, 0.0f);
}

namespace {

	struct FramePlan {
		u32 steps;
		f32 seconds;
	};

	constexpr FramePlan PLAN[] = {{0, 1.0f / 144.0f}, {1, 1.0f / 144.0f}, {0, 1.0f / 144.0f}, {0, 1.0f / 144.0f}, {1, 1.0f / 144.0f}, {0, 1.0f / 144.0f}, {0, 1.0f / 144.0f}, {1, 1.0f / 144.0f}, {0, 1.0f / 144.0f}, {1, 1.0f / 144.0f}, {0, 1.0f / 144.0f}, {0, 1.0f / 144.0f}};
	constexpr f32 STEP = 0.02f;
	constexpr f32 SPEED = 10.0f;

	struct Mover {
		SceneObject* object = nullptr;
	};

	void MoveTowardTheListener(const FrameStageArgs&, void* user) {
		SceneObject* object = static_cast<Mover*>(user)->object;
		object->SetWorldPosition(object->GetWorldPosition() + Vec3(0.0f, 0.0f, -SPEED * Time::GetDeltaTime()));
	}

} // namespace

TEST_F(AudioSpatialFixture, FixedStepMotionMeasuredInFixedLoop) {
	AddListener(Vec3{});
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip, Vec3(0.0f, 0.0f, 40.0f));
	Mover mover{ObjectOf(source)};
	FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &MoveTowardTheListener, &mover, &_mover);

	const f32 expected = SPEED_OF_SOUND / (SPEED_OF_SOUND - SPEED);
	std::vector<f32> auto_;
	for (const FramePlan& frame : PLAN) {
		RunFrameWithSteps(frame.steps, STEP, frame.seconds);
		auto_.push_back(TheVoice()->placement.doppler);
	}
	for (size_t index = 2; index < auto_.size(); ++index) {
		EXPECT_NEAR(auto_[index], expected, 1e-4f) << "frame " << index;
	}

	source->SetVelocityUpdateMode(AudioVelocityUpdateMode::Dynamic);
	f32 lowest = 10.0f;
	f32 highest = 0.0f;
	for (const FramePlan& frame : PLAN) {
		RunFrameWithSteps(frame.steps, STEP, frame.seconds);
		lowest = std::min(lowest, TheVoice()->placement.doppler);
		highest = std::max(highest, TheVoice()->placement.doppler);
	}
	EXPECT_NEAR(lowest, 1.0f, 1e-4f);
	EXPECT_GT(highest, expected + 0.02f);
}

TEST_F(AudioSpatialFixture, FrameMotionMeasuredPerFrame) {
	AddListener(Vec3{});
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip, Vec3(0.0f, 0.0f, 40.0f));
	Mover mover{ObjectOf(source)};
	FrameStages::Register(FrameStage::Update, FrameStagePosition::Normal, &MoveTowardTheListener, &mover, &_mover);

	const f32 expected = SPEED_OF_SOUND / (SPEED_OF_SOUND - SPEED);
	for (size_t index = 0; index < std::size(PLAN); ++index) {
		RunFrameWithSteps(PLAN[index].steps, STEP, PLAN[index].seconds);
		if (index >= 1) {
			EXPECT_NEAR(TheVoice()->placement.doppler, expected, 1e-4f) << "frame " << index;
		}
	}
}

TEST_F(AudioSpatialFixture, UnregisterMovesVoicesAndFences) {
	CountingSpatializer counting;
	char owner = 0;
	ASSERT_TRUE(AudioSpatializers::Register("Counter", &counting, &owner));
	SelectSpatializer("Counter");
	AddListener(Vec3{});
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip, Vec3(2.0f, 0.0f, 0.0f));
	source->SetSpatialize(true);
	RunFrame();
	ASSERT_NE(TheVoice(), nullptr);
	EXPECT_EQ(TheVoice()->spatializer, &counting);
	ASSERT_TRUE(WaitFor([&]() { return counting.blocks.load() > 4; }));

	EXPECT_EQ(AudioSpatializers::UnregisterOwnedBy(&owner), 1u);
	EXPECT_EQ(TheVoice()->spatializer, &GetBuiltinSpatializer());
	EXPECT_EQ(counting.created, counting.destroyed);
	const u64 after = counting.blocks.load();
	ASSERT_TRUE(WaitForBlocks(8));
	EXPECT_EQ(counting.blocks.load(), after);
	EXPECT_TRUE(source->IsPlaying());
	SelectSpatializer("");
}

TEST_F(AudioSpatialFixture, SpatializerSwitchMovesAskers) {
	CountingSpatializer first;
	CountingSpatializer second;
	char owner = 0;
	ASSERT_TRUE(AudioSpatializers::Register("First", &first, &owner));
	ASSERT_TRUE(AudioSpatializers::Register("Second", &second, &owner));
	SelectSpatializer("First");
	AddListener(Vec3{});
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> asked = AddSource(clip, Vec3(2.0f, 0.0f, 0.0f));
	asked->SetSpatialize(true);
	AddSource(clip, Vec3(-2.0f, 0.0f, 0.0f));
	RunFrame();
	EXPECT_EQ(first.created, 1u);

	SelectSpatializer("Second");
	EXPECT_EQ(second.created, 1u);
	EXPECT_EQ(first.destroyed, 1u);
	u32 onBuiltin = 0;
	AudioVoiceTable& voices = GetAudioSystem().GetVoices();
	for (u32 slot = 0; slot < voices.GetCapacity(); ++slot) {
		const AudioVoiceRecord* record = voices.Find(slot, voices.GetGeneration(slot));
		if (record && record->claim != VoiceClaim::Free && record->spatializer == &GetBuiltinSpatializer()) {
			++onBuiltin;
		}
	}
	EXPECT_EQ(onBuiltin, 1u);
	EXPECT_EQ(AudioSpatializers::UnregisterOwnedBy(&owner), 2u);
	SelectSpatializer("");
}

TEST_F(AudioSpatialFixture, DeviceReopenRebuildsStates) {
	CountingSpatializer counting;
	char owner = 0;
	ASSERT_TRUE(AudioSpatializers::Register("Counter", &counting, &owner));
	SelectSpatializer("Counter");
	AddListener(Vec3{});
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip, Vec3(2.0f, 0.0f, 0.0f));
	source->SetSpatialize(true);
	RunFrame();
	ASSERT_EQ(counting.created, 1u);

	AudioSettings* settings = GetDeclaredSettings();
	ASSERT_NE(settings, nullptr);
	settings->periodMilliseconds = settings->periodMilliseconds == 20 ? 10 : 20;
	Settings::NotifyChanged(SETTINGS_CATEGORY_PATH, "periodMilliseconds");
	EXPECT_EQ(counting.created, 2u);
	EXPECT_EQ(counting.destroyed, 1u);
	const u64 before = counting.blocks.load();
	EXPECT_TRUE(WaitFor([&]() { return counting.blocks.load() > before + 4; }));
	EXPECT_TRUE(source->IsPlaying());
	EXPECT_EQ(AudioSpatializers::UnregisterOwnedBy(&owner), 1u);
	SelectSpatializer("");
}

TEST_F(AudioSpatialFixture, IsolatedListenerHearsNothing) {
	Handle<Scene> isolated = SceneSystem::CreateIsolatedScene("stage");
	ASSERT_TRUE(isolated.IsValid());
	Handle<SceneObject> staged = SceneSystem::CreateObject("staged", isolated);
	staged->AddComponent<AudioListener>();
	AddListener(Vec3(10.0f, 0.0f, 0.0f));
	const std::span<const AudioListenerPose> listeners = GetAudioSystem().GatherListeners();
	ASSERT_EQ(listeners.size(), 1u);
	EXPECT_EQ(listeners[0].position.x, 10.0f);
}

#endif
