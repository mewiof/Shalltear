#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioEffectRegistry.h"
#include "AudioLiveFixture.h"
#include "AudioOfflineMixer.h"
#include "Effects/BuiltinEffects.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioEffect.h"
#include "Shalltear/Audio/AudioMixer.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Module/ModuleEvents.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

using namespace AudioTesting;

namespace {

	constexpr u32 RATE = 48000;
	constexpr u32 BLOCK = AudioRenderer::BLOCK_FRAMES;

	constexpr std::string_view BUILTIN_NAMES[] = {"LowPass", "HighPass", "Echo", "Flange", "Distortion", "Normalize", "Param EQ", "Pitch Shifter", "Chorus",
												  "Compressor", "SFX Reverb", "LowPass Simple", "HighPass Simple", "Duck Volume"};

	IAudioEffectDefinition* Builtin(std::string_view name) {
		for (const AudioBuiltinEffect& effect : GetBuiltinEffects()) {
			if (effect.name == name) {
				return effect.definition;
			}
		}
		return nullptr;
	}

	using Overrides = std::vector<std::pair<std::string_view, f32>>;

	class Rig {
	public:
		Rig(std::string_view name, u32 channels, Overrides set = {}) : _channels(channels) {
			_definition = Builtin(name);
			if (!_definition) {
				ADD_FAILURE() << "no built-in named " << name;
				return;
			}
			std::vector<f32> values;
			for (const AudioParameterDefinition& parameter : _definition->GetParameters()) {
				values.push_back(parameter.defaultValue);
			}
			for (const auto& [parameter, value] : set) {
				const u32 index = FindEffectParameter(*_definition, parameter);
				if (index == NO_EFFECT_PARAMETER) {
					ADD_FAILURE() << name << " has no parameter " << parameter;
					continue;
				}
				values[index] = value;
			}
			AudioEffectContext context;
			context.sampleRate = RATE;
			context.blockFrames = BLOCK;
			context.channels = channels;
			context.parameters = {values.data(), values.size()};
			_effect = _definition->CreateEffect(context);
			EXPECT_NE(_effect, nullptr) << name;
			_block.assign(static_cast<size_t>(channels) * BLOCK, 0.0f);
			_side.assign(static_cast<size_t>(channels) * BLOCK, 0.0f);
		}
		~Rig() {
			if (_effect) {
				_effect->Release();
			}
		}
		Rig(const Rig&) = delete;
		Rig& operator=(const Rig&) = delete;

		std::vector<f32> Run(const std::vector<f32>& input, const std::vector<f32>* sidechain = nullptr, u32 channel = 0) {
			std::vector<f32> out;
			out.reserve(input.size());
			for (size_t from = 0; from < input.size(); from += BLOCK) {
				for (u32 row = 0; row < _channels; ++row) {
					for (u32 frame = 0; frame < BLOCK; ++frame) {
						const size_t at = from + frame;
						_block[static_cast<size_t>(row) * BLOCK + frame] = at < input.size() ? input[at] : 0.0f;
						_side[static_cast<size_t>(row) * BLOCK + frame] = sidechain && at < sidechain->size() ? (*sidechain)[at] : 0.0f;
					}
				}
				AudioBlock block{_block.data(), _channels, BLOCK, BLOCK};
				const AudioBlock side{_side.data(), _channels, BLOCK, BLOCK};
				_effect->Process(block, sidechain ? &side : nullptr);
				for (u32 frame = 0; frame < BLOCK && from + frame < input.size(); ++frame) {
					out.push_back(_block[static_cast<size_t>(channel) * BLOCK + frame]);
				}
			}
			return out;
		}

		void Set(std::string_view parameter, f32 value) { _effect->SetParameter(FindEffectParameter(*_definition, parameter), value); }

		f64 Time(const std::vector<f32>& input, u32 blocks) {
			for (u32 row = 0; row < _channels; ++row) {
				std::copy(input.begin(), input.begin() + BLOCK, _side.begin() + static_cast<std::ptrdiff_t>(row) * BLOCK);
			}
			const AudioBlock side{_side.data(), _channels, BLOCK, BLOCK};
			const auto started = std::chrono::steady_clock::now();
			for (u32 pass = 0; pass < blocks; ++pass) {
				std::memcpy(_block.data(), _side.data(), _block.size() * sizeof(f32));
				AudioBlock block{_block.data(), _channels, BLOCK, BLOCK};
				_effect->Process(block, &side);
			}
			return std::chrono::duration<f64>(std::chrono::steady_clock::now() - started).count();
		}

		IAudioEffect& Get() { return *_effect; }
	private:
		IAudioEffectDefinition* _definition = nullptr;
		IAudioEffect* _effect = nullptr;
		u32 _channels = 0;
		std::vector<f32> _block;
		std::vector<f32> _side;
	};

	std::vector<f32> Sine(f32 hertz, f32 seconds, f32 amplitude = 0.5f) {
		std::vector<f32> signal(static_cast<size_t>(seconds * RATE));
		for (size_t frame = 0; frame < signal.size(); ++frame) {
			signal[frame] = amplitude * static_cast<f32>(std::sin(2.0 * 3.14159265358979 * hertz * static_cast<f64>(frame) / RATE));
		}
		return signal;
	}

	std::vector<f32> Constant(f32 value, f32 seconds) { return std::vector<f32>(static_cast<size_t>(seconds * RATE), value); }

	std::vector<f32> Impulse(f32 seconds) {
		std::vector<f32> signal(static_cast<size_t>(seconds * RATE), 0.0f);
		signal[0] = 1.0f;
		return signal;
	}

	std::vector<f32> Noise(size_t frames, f32 amplitude = 0.5f) {
		std::vector<f32> signal(frames);
		u32 state = 0x9E3779B9u;
		for (f32& sample : signal) {
			state = state * 1664525u + 1013904223u;
			sample = amplitude * (static_cast<f32>(state >> 8) / 8388608.0f - 1.0f);
		}
		return signal;
	}

	f32 Amplitude(const std::vector<f32>& signal, size_t from, size_t count, f32 hertz) {
		f64 re = 0.0;
		f64 im = 0.0;
		for (size_t frame = 0; frame < count; ++frame) {
			const f64 phase = 2.0 * 3.14159265358979 * hertz * static_cast<f64>(frame) / RATE;
			re += signal[from + frame] * std::cos(phase);
			im += signal[from + frame] * std::sin(phase);
		}
		return static_cast<f32>(2.0 * std::sqrt(re * re + im * im) / static_cast<f64>(count));
	}

	f32 Decibels(f32 ratio) { return 20.0f * std::log10(ratio); }

	f32 Peak(const std::vector<f32>& signal, size_t from = 0, size_t count = ~size_t{0}) {
		f32 peak = 0.0f;
		for (size_t frame = from; frame < signal.size() && frame - from < count; ++frame) {
			peak = std::fabs(signal[frame]) > peak ? std::fabs(signal[frame]) : peak;
		}
		return peak;
	}

	f32 Response(std::string_view name, Overrides set, f32 hertz) {
		Rig rig(name, 1, set);
		const std::vector<f32> out = rig.Run(Sine(hertz, 0.5f));
		return Decibels(Amplitude(out, RATE / 4, RATE / 4, hertz) / 0.5f);
	}

	f64 Energy(const std::vector<f32>& signal) {
		f64 energy = 0.0;
		for (const f32 sample : signal) {
			energy += static_cast<f64>(sample) * sample;
		}
		return energy;
	}

	Overrides Wet(const Overrides& set) {
		Overrides all = {{"Dry Level", -10000.0f}, {"Room", 0.0f}, {"Room LF", 0.0f}, {"Reflections", -10000.0f}};
		all.insert(all.end(), set.begin(), set.end());
		return all;
	}

	f32 DecayTime(const std::vector<f32>& signal) {
		std::vector<f64> remaining(signal.size() + 1, 0.0);
		for (size_t frame = signal.size(); frame-- > 0;) {
			remaining[frame] = remaining[frame + 1] + static_cast<f64>(signal[frame]) * signal[frame];
		}
		const auto reaches = [&](f64 decibels) {
			const f64 level = remaining[0] * std::pow(10.0, decibels / 10.0);
			size_t frame = 0;
			while (frame < signal.size() && remaining[frame] > level) {
				++frame;
			}
			return static_cast<f32>(frame) / static_cast<f32>(RATE);
		};
		return 2.0f * (reaches(-35.0) - reaches(-5.0));
	}

	std::vector<f32> Band(const std::vector<f32>& signal, f32 hertz) {
		const f64 w0 = 2.0 * 3.14159265358979 * hertz / RATE;
		const f64 alpha = std::sin(w0) / 8.0;
		const f64 b0 = alpha / (1.0 + alpha);
		const f64 a1 = -2.0 * std::cos(w0) / (1.0 + alpha);
		const f64 a2 = (1.0 - alpha) / (1.0 + alpha);
		std::vector<f32> out = signal;
		for (u32 pass = 0; pass < 2; ++pass) {
			f64 x1 = 0.0;
			f64 x2 = 0.0;
			f64 y1 = 0.0;
			f64 y2 = 0.0;
			for (f32& sample : out) {
				const f64 y = b0 * (sample - x2) - a1 * y1 - a2 * y2;
				x2 = x1;
				x1 = sample;
				y2 = y1;
				y1 = y;
				sample = static_cast<f32>(y);
			}
		}
		return out;
	}

} // namespace

TEST(AudioBuiltinEffectTest, LowPassCutsHighTone) {
	EXPECT_NEAR(Response("LowPass", {{"Cutoff freq", 1000.0f}}, 100.0f), 0.0f, 0.3f);
	EXPECT_NEAR(Response("LowPass", {{"Cutoff freq", 1000.0f}}, 1000.0f), 0.0f, 0.5f);
	EXPECT_LT(Response("LowPass", {{"Cutoff freq", 1000.0f}}, 10000.0f), -35.0f);
	EXPECT_NEAR(Response("LowPass", {{"Cutoff freq", 1000.0f}, {"Resonance", 10.0f}}, 1000.0f), 20.0f, 1.0f);
}

TEST(AudioBuiltinEffectTest, HighPassCutsLowTone) {
	EXPECT_NEAR(Response("HighPass", {{"Cutoff freq", 1000.0f}}, 10000.0f), 0.0f, 0.3f);
	EXPECT_LT(Response("HighPass", {{"Cutoff freq", 1000.0f}}, 100.0f), -35.0f);
}

TEST(AudioBuiltinEffectTest, SimpleFilters3DbDownAtCutoff) {
	EXPECT_NEAR(Response("LowPass Simple", {{"Cutoff freq", 1000.0f}}, 1000.0f), -3.0f, 0.5f);
	EXPECT_NEAR(Response("LowPass Simple", {{"Cutoff freq", 1000.0f}}, 100.0f), 0.0f, 0.3f);
	EXPECT_LT(Response("LowPass Simple", {{"Cutoff freq", 1000.0f}}, 10000.0f), -30.0f);
	EXPECT_NEAR(Response("HighPass Simple", {{"Cutoff freq", 1000.0f}}, 1000.0f), -3.0f, 0.5f);
	EXPECT_NEAR(Response("HighPass Simple", {{"Cutoff freq", 1000.0f}}, 10000.0f), 0.0f, 0.3f);
	EXPECT_LT(Response("HighPass Simple", {{"Cutoff freq", 1000.0f}}, 100.0f), -30.0f);
}

TEST(AudioBuiltinEffectTest, ParamEqGainAtCentreOnly) {
	EXPECT_NEAR(Response("Param EQ", {{"Center freq", 1000.0f}, {"Frequency Gain", 2.0f}}, 1000.0f), 6.02f, 0.3f);
	EXPECT_NEAR(Response("Param EQ", {{"Center freq", 1000.0f}, {"Frequency Gain", 0.5f}}, 1000.0f), -6.02f, 0.3f);
	EXPECT_NEAR(Response("Param EQ", {{"Center freq", 1000.0f}, {"Frequency Gain", 2.0f}}, 100.0f), 0.0f, 0.5f);
}

TEST(AudioBuiltinEffectTest, ParameterThatMovesIsWalkedNotStepped) {
	Rig rig("Compressor", 1, {{"Threshold", -12.0f}});
	rig.Run(Constant(0.1f, 0.1f));
	rig.Set("Make up gain", 12.0f);
	const f32 arrived = 0.1f * std::pow(10.0f, 12.0f / 20.0f);
	const std::vector<f32> walking = rig.Run(Constant(0.1f, 0.03f));
	EXPECT_LT(walking[0], 0.101f);
	EXPECT_GT(walking[BLOCK - 1], walking[0]);
	EXPECT_NEAR(walking.back(), arrived, 1e-4f);
}

TEST(AudioBuiltinEffectTest, CompressorHoldsLevelOverThresholdAtThreshold) {
	const f32 threshold = std::pow(10.0f, -12.0f / 20.0f);
	{
		Rig rig("Compressor", 2, {{"Threshold", -12.0f}, {"Attack", 10.0f}, {"Release", 20.0f}});
		const std::vector<f32> out = rig.Run(Constant(0.5f, 0.3f));
		EXPECT_NEAR(out.back(), threshold, threshold * 0.01f);
	}
	{
		Rig rig("Compressor", 2, {{"Threshold", -12.0f}});
		EXPECT_FLOAT_EQ(rig.Run(Constant(0.1f, 0.3f)).back(), 0.1f);
	}
	{
		Rig rig("Compressor", 2, {{"Threshold", -12.0f}, {"Make up gain", 6.0f}});
		EXPECT_NEAR(rig.Run(Constant(0.1f, 0.3f)).back(), 0.1f * std::pow(10.0f, 6.0f / 20.0f), 1e-4f);
	}
}

TEST(AudioBuiltinEffectTest, DuckFollowsSidechain) {
	const Overrides hard = {{"Threshold", -20.0f}, {"Ratio", 200.0f}, {"Knee", 0.0f}, {"Attack Time", 0.0f}, {"Release Time", 0.0f}};
	const std::vector<f32> sound = Constant(0.25f, 0.2f);
	const std::vector<f32> key = Constant(0.5f, 0.2f);
	const f32 over = Decibels(0.5f) + 20.0f;
	{
		Rig rig("Duck Volume", 2, hard);
		EXPECT_NEAR(rig.Run(sound, &key).back(), 0.25f * std::pow(10.0f, -over * 0.5f / 20.0f), 1e-4f);
	}
	{
		Rig rig("Duck Volume", 2, hard);
		const std::vector<f32> quiet = Constant(0.05f, 0.2f);
		EXPECT_FLOAT_EQ(rig.Run(sound, &quiet).back(), 0.25f);
		EXPECT_FLOAT_EQ(rig.Run(sound).back(), 0.25f);
	}
	{
		Rig rig("Duck Volume", 2, {{"Threshold", -20.0f}, {"Ratio", 200.0f}, {"Knee", 0.0f}, {"Attack Time", 0.0f}, {"Release Time", 0.0f}, {"Sidechain Mix", 50.0f}});
		EXPECT_NEAR(rig.Run(sound, &key).back(), 0.25f * (0.5f + 0.5f * std::pow(10.0f, -over * 0.5f / 20.0f)), 1e-4f);
	}
	{
		Rig rig("Duck Volume", 2, {{"Threshold", Decibels(0.5f)}, {"Ratio", 200.0f}, {"Knee", 10.0f}, {"Attack Time", 0.0f}, {"Release Time", 0.0f}});
		EXPECT_NEAR(rig.Run(sound, &key).back(), 0.25f * std::pow(10.0f, -0.625f / 20.0f), 1e-4f);
	}
}

TEST(AudioBuiltinEffectTest, DuckVolumeReleasesOverItsReleaseTime) {
	Rig rig("Duck Volume", 1, {{"Threshold", -20.0f}, {"Knee", 0.0f}, {"Attack Time", 0.0f}, {"Release Time", 200.0f}});
	const std::vector<f32> key = Constant(0.5f, 0.2f);
	const f32 ducked = rig.Run(Constant(0.25f, 0.2f), &key).back();
	const std::vector<f32> silence = Constant(0.0f, 0.02f);
	const std::vector<f32> after = rig.Run(Constant(0.25f, 0.02f), &silence);
	EXPECT_GT(after.back(), ducked);
	EXPECT_LT(after.back(), 0.25f * 0.99f);
	EXPECT_FALSE(rig.Get().IsQuiet());
	const std::vector<f32> long_silence = Constant(0.0f, 3.0f);
	EXPECT_NEAR(rig.Run(Constant(0.25f, 3.0f), &long_silence).back(), 0.25f, 1e-3f);
	EXPECT_TRUE(rig.Get().IsQuiet());
}

TEST(AudioBuiltinEffectTest, NormalizeReachesFullScale) {
	const Overrides fast = {{"Fade in time", 100.0f}};
	{
		Rig rig("Normalize", 2, fast);
		EXPECT_NEAR(rig.Run(Constant(0.25f, 1.0f)).back(), 1.0f, 0.01f);
	}
	{
		Rig rig("Normalize", 2, fast);
		EXPECT_NEAR(rig.Run(Constant(0.01f, 1.0f)).back(), 0.1f, 0.001f);
	}
	{
		Rig rig("Normalize", 2, {{"Fade in time", 100.0f}, {"Maximum amp", 2.0f}});
		EXPECT_NEAR(rig.Run(Constant(0.25f, 1.0f)).back(), 0.5f, 0.005f);
	}
	{
		Rig rig("Normalize", 2, fast);
		rig.Run(Constant(0.1f, 1.0f));
		const std::vector<f32> loud = rig.Run(Constant(0.9f, 0.01f));
		EXPECT_LE(Peak(loud), 1.0f + 1e-5f);
	}
}

TEST(AudioBuiltinEffectTest, DistortionShapesTowardsSquareAsItRises) {
	const std::vector<f32> tone = Sine(1000.0f, 0.1f, 0.8f);
	{
		Rig rig("Distortion", 1, {{"Distortion", 0.0f}});
		EXPECT_EQ(rig.Run(tone), tone);
	}
	f32 third[2]{};
	for (u32 setting = 0; setting < 2; ++setting) {
		Rig rig("Distortion", 1, {{"Distortion", setting == 0 ? 0.2f : 0.9f}});
		const std::vector<f32> out = rig.Run(tone);
		EXPECT_LE(Peak(out), 1.0f);
		third[setting] = Amplitude(out, 0, RATE / 20, 3000.0f);
		EXPECT_LT(Amplitude(out, 0, RATE / 20, 2000.0f), 1e-3f);
	}
	EXPECT_GT(third[1], 2.0f * third[0]);
}

TEST(AudioBuiltinEffectTest, EchoReturnsAtDelayAndDecays) {
	const u32 delay = RATE / 10;
	{
		Rig rig("Echo", 2, {{"Delay", 100.0f}, {"Decay", 50.0f}, {"Drymix", 100.0f}, {"Wetmix", 80.0f}});
		const std::vector<f32> out = rig.Run(Impulse(0.5f));
		EXPECT_FLOAT_EQ(out[0], 1.0f);
		EXPECT_NEAR(out[delay], 0.8f, 1e-5f);
		EXPECT_NEAR(out[2 * delay], 0.8f * 0.5f, 1e-5f);
		EXPECT_NEAR(out[3 * delay], 0.8f * 0.25f, 1e-5f);
		EXPECT_NEAR(out[delay - 1] + out[delay + 1], 0.0f, 1e-5f);
	}
	{
		Rig rig("Echo", 2, {{"Delay", 100.0f}, {"Decay", 0.0f}, {"Drymix", 0.0f}});
		const std::vector<f32> out = rig.Run(Impulse(0.5f));
		EXPECT_EQ(out[0], 0.0f);
		EXPECT_NEAR(out[delay], 1.0f, 1e-5f);
		EXPECT_EQ(out[2 * delay], 0.0f);
	}
}

TEST(AudioBuiltinEffectTest, FlangeIsSoundBesideShortlyDelayedCopy) {
	Rig rig("Flange", 1, {{"Rate", 0.1f}, {"Depth", 0.5f}});
	const std::vector<f32> out = rig.Run(Impulse(0.05f));
	EXPECT_FLOAT_EQ(out[0], 0.45f);
	f32 copy = 0.0f;
	size_t last = 0;
	for (size_t frame = 1; frame < out.size(); ++frame) {
		copy += out[frame];
		last = out[frame] != 0.0f ? frame : last;
	}
	EXPECT_NEAR(copy, 0.55f, 0.01f);
	EXPECT_LT(last, static_cast<size_t>(0.02f * RATE));
}

TEST(AudioBuiltinEffectTest, ChorusTapsDelayAndFeedBack) {
	const u32 delay = RATE / 50;
	Rig rig("Chorus", 1, {{"Delay", 20.0f}, {"Depth", 0.0f}, {"Rate", 0.0f}, {"Feedback", 0.3f}});
	const std::vector<f32> out = rig.Run(Impulse(0.1f));
	EXPECT_FLOAT_EQ(out[0], 0.5f);
	EXPECT_NEAR(out[delay], 3.0f * 0.5f, 1e-5f);
	EXPECT_NEAR(out[2 * delay], 0.3f * 3.0f * 0.5f, 1e-5f);
}

TEST(AudioBuiltinEffectTest, PitchShifterMovesToneToMultiple) {
	const std::vector<f32> tone = Sine(1000.0f, 0.5f);
	const auto measure = [&](f32 pitch, f32 hertz) {
		Rig rig("Pitch Shifter", 1, {{"Pitch", pitch}});
		const std::vector<f32> out = rig.Run(tone);
		return Amplitude(out, RATE / 4, RATE / 4, hertz);
	};
	EXPECT_GT(measure(2.0f, 2000.0f), 10.0f * measure(2.0f, 1000.0f));
	EXPECT_GT(measure(0.5f, 500.0f), 10.0f * measure(0.5f, 1000.0f));
	EXPECT_NEAR(measure(1.0f, 1000.0f), 0.5f, 0.05f);
}

TEST(AudioBuiltinEffectTest, PitchShifterTakesNewFftSize) {
	Rig rig("Pitch Shifter", 1, {{"Pitch", 2.0f}});
	const std::vector<f32> tone = Sine(1000.0f, 0.5f);
	rig.Run(tone);
	rig.Set("FFT Size", 4096.0f);
	rig.Set("Overlap", 8.0f);
	const std::vector<f32> out = rig.Run(tone);
	EXPECT_GT(Amplitude(out, RATE / 4, RATE / 4, 2000.0f), 10.0f * Amplitude(out, RATE / 4, RATE / 4, 1000.0f));
}

TEST(AudioBuiltinEffectTest, SfxReverbAtDefaultsIsDry) {
	Rig rig("SFX Reverb", 2);
	const std::vector<f32> noise = Noise(RATE / 2);
	EXPECT_EQ(rig.Run(noise), noise);
}

TEST(AudioBuiltinEffectTest, SfxReverbDecays60Db) {
	for (const f32 decay : {1.0f, 2.0f}) {
		Rig rig("SFX Reverb", 2, Wet({{"Decay Time", decay}, {"Decay HF Ratio", 1.0f}}));
		EXPECT_NEAR(DecayTime(rig.Run(Impulse(3.0f * decay))), decay, 0.15f * decay) << decay << " s";
	}
}

TEST(AudioBuiltinEffectTest, SfxReverbHighsFollowHfRatio) {
	for (const f32 ratio : {0.25f, 1.0f, 2.0f}) {
		Rig rig("SFX Reverb", 1, Wet({{"Decay Time", 1.0f}, {"Decay HF Ratio", ratio}, {"HFReference", 5000.0f}}));
		const std::vector<f32> response = rig.Run(Impulse(4.0f));
		EXPECT_NEAR(DecayTime(Band(response, 5000.0f)), ratio, 0.25f * ratio) << ratio;
		EXPECT_NEAR(DecayTime(Band(response, 250.0f)), 1.0f, 0.15f) << ratio;
		if (ratio == 1.0f) {
			EXPECT_NEAR(DecayTime(Band(response, 12000.0f)), 1.0f, 0.15f);
		}
	}
}

TEST(AudioBuiltinEffectTest, SfxReverbLateCarriesRoomEnergy) {
	const f32 decays[2] = {0.5f, 3.0f};
	f64 energies[2]{};
	for (u32 index = 0; index < 2; ++index) {
		Rig rig("SFX Reverb", 2, Wet({{"Decay Time", decays[index]}, {"Decay HF Ratio", 1.0f}}));
		energies[index] = Energy(rig.Run(Impulse(3.0f * decays[index])));
		EXPECT_NEAR(10.0 * std::log10(energies[index]), 0.0, 3.0) << decays[index] << " s";
	}
	EXPECT_NEAR(10.0 * std::log10(energies[1] / energies[0]), 0.0, 1.5);
}

TEST(AudioBuiltinEffectTest, SfxReverbReachesStereoPairDecorrelated) {
	const Overrides set = Wet({{"Reflections", 0.0f}});
	Rig first("SFX Reverb", 2, set);
	Rig second("SFX Reverb", 2, set);
	const std::vector<f32> left = first.Run(Impulse(3.0f), nullptr, 0);
	const std::vector<f32> right = second.Run(Impulse(3.0f), nullptr, 1);
	f64 shared = 0.0;
	for (size_t frame = 0; frame < left.size(); ++frame) {
		shared += static_cast<f64>(left[frame]) * right[frame];
	}
	EXPECT_NEAR(10.0 * std::log10(Energy(right) / Energy(left)), 0.0, 1.0);
	EXPECT_LT(std::fabs(shared / std::sqrt(Energy(left) * Energy(right))), 0.2);
}

TEST(AudioBuiltinEffectTest, SfxReverbAudibleWhileTailRings) {
	Rig rig("SFX Reverb", 2, Wet({{"Decay Time", 3.0f}}));
	rig.Run(Noise(RATE / 10));
	rig.Run(Constant(0.0f, 2.0f));
	EXPECT_FALSE(rig.Get().IsQuiet());
	rig.Run(Constant(0.0f, 10.0f));
	EXPECT_TRUE(rig.Get().IsQuiet());
}

TEST(AudioBuiltinEffectTest, SfxReverbRoomHfLfShelve) {
	const auto bands = [](const Overrides& set) {
		Rig rig("SFX Reverb", 1, Wet(set));
		const std::vector<f32> response = rig.Run(Impulse(1.0f));
		return std::pair<f64, f64>{Energy(Band(response, 40.0f)), Energy(Band(response, 16000.0f))};
	};
	const auto change = [](f64 to, f64 from) { return 10.0 * std::log10(to / from); };
	const auto [lowFlat, highFlat] = bands({});
	const auto [lowUnderHf, highUnderHf] = bands({{"Room HF", -2000.0f}});
	const auto [lowUnderLf, highUnderLf] = bands({{"Room LF", -2000.0f}});
	EXPECT_LT(change(highUnderHf, highFlat), -10.0);
	EXPECT_NEAR(change(lowUnderHf, lowFlat), 0.0, 1.0);
	EXPECT_LT(change(lowUnderLf, lowFlat), -10.0);
	EXPECT_NEAR(change(highUnderLf, highFlat), 0.0, 1.0);
}

TEST(AudioBuiltinEffectTest, SfxReverbReflectionAtDelay) {
	Rig rig("SFX Reverb", 2, Wet({{"Reflections", 0.0f}, {"Reverb", -10000.0f}, {"Reflect Delay", 0.1f}}));
	const std::vector<f32> out = rig.Run(Impulse(0.2f));
	const size_t delay = RATE / 10;
	EXPECT_EQ(Peak(out, 0, delay), 0.0f);
	EXPECT_GT(std::fabs(out[delay]), 0.1f);
}

TEST(AudioBuiltinEffectTest, SfxReverbLateFollowsReverbDelay) {
	Rig rig("SFX Reverb", 2, Wet({{"Reflect Delay", 0.05f}, {"Reverb Delay", 0.05f}}));
	const std::vector<f32> out = rig.Run(Impulse(0.3f));
	const size_t start = RATE / 10;
	EXPECT_EQ(Peak(out, 0, start), 0.0f);
	EXPECT_GT(Peak(out, start, RATE / 1000), 0.01f);
}

TEST(AudioBuiltinEffectTest, EveryBuiltinBypassedIsTheIdentity) {
	ClipBlock clip(MakePcm(Noise(8 * BLOCK), 1, RATE));
	const auto render = [&](IAudioEffect* effect, bool unitBypass, bool groupBypass) {
		OfflineMixer mixer(2);
		const OfflineBus bus = mixer.Bus(1);
		if (effect) {
			EXPECT_TRUE(mixer.Push(OfflineMixer::Row(bus, 0, AudioBusRowKind::Effect, {}, 1.0f, effect, unitBypass)));
			EXPECT_TRUE(mixer.Push(OfflineMixer::Row(bus, 1, AudioBusRowKind::Fader)));
			EXPECT_TRUE(mixer.Push(OfflineMixer::RowCount(bus, 2)));
			EXPECT_TRUE(mixer.Push(OfflineMixer::Flag(AudioCommandKind::SetBusBypassEffects, bus, groupBypass)));
		}
		EXPECT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, clip.Get()), bus)));
		return mixer.Render(8 * BLOCK);
	};
	const std::vector<f32> reference = render(nullptr, false, false);
	for (const AudioBuiltinEffect& builtin : GetBuiltinEffects()) {
		std::vector<f32> values;
		for (const AudioParameterDefinition& parameter : builtin.definition->GetParameters()) {
			values.push_back(parameter.defaultValue);
		}
		AudioEffectContext context;
		context.sampleRate = RATE;
		context.blockFrames = BLOCK;
		context.channels = 2;
		context.parameters = {values.data(), values.size()};
		IAudioEffect* effect = builtin.definition->CreateEffect(context);
		ASSERT_NE(effect, nullptr) << builtin.name;
		EXPECT_EQ(render(effect, true, false), reference) << builtin.name;
		EXPECT_EQ(render(effect, false, true), reference) << builtin.name;
		effect->Release();
	}
}

TEST(AudioBuiltinEffectTest, BuiltinsQuietAfterTail) {
	for (const AudioBuiltinEffect& builtin : GetBuiltinEffects()) {
		Rig rig(builtin.name, 2);
		rig.Run(Noise(RATE / 10));
		if (builtin.name == "Echo" || builtin.name == "SFX Reverb") {
			EXPECT_FALSE(rig.Get().IsQuiet()) << builtin.name;
		}
		const std::vector<f32> second = Constant(0.0f, 1.0f);
		bool quiet = false;
		for (u32 seconds = 0; seconds < 30 && !quiet; ++seconds) {
			rig.Run(second);
			quiet = rig.Get().IsQuiet();
		}
		EXPECT_TRUE(quiet) << builtin.name;
	}
}

TEST(AudioBuiltinEffectTest, BlockCostOfEachBuiltin) {
	constexpr u32 BLOCKS = 2000;
	const std::vector<f32> noise = Noise(BLOCK);
	for (const AudioBuiltinEffect& builtin : GetBuiltinEffects()) {
		Rig rig(builtin.name, 2);
		rig.Time(noise, 200);
		const f64 micros = rig.Time(noise, BLOCKS) * 1e6 / BLOCKS;
		std::printf("[ READING  ] %-16.*s %8.2f us a block\n", static_cast<int>(builtin.name.size()), builtin.name.data(), micros);
	}
}

namespace {

	class BuiltinEffectFixture : public LivePackageFixture {};

} // namespace

TEST_F(BuiltinEffectFixture, SetRegisteredByName) {
	EXPECT_EQ(GetBuiltinEffects().size(), std::size(BUILTIN_NAMES));
	for (const std::string_view name : BUILTIN_NAMES) {
		EXPECT_NE(Builtin(name), nullptr) << name;
		EXPECT_EQ(FindEffect(name), Builtin(name)) << name;
		for (const AudioParameterDefinition& parameter : Builtin(name)->GetParameters()) {
			EXPECT_NE(parameter.name, "Max channels") << name;
		}
	}
	char project = 0;
	EXPECT_FALSE(AudioEffects::Register("Echo", Builtin("LowPass"), &project));
}

TEST_F(BuiltinEffectFixture, SetFollowsPackageImage) {
	const u32 registered = GetRegisteredEffectCount();
	char elsewhere = 0;
	ModuleEvents::ImageUnloading.Publish(EventBus::GetGlobal(), ImageUnloadingEvent{&elsewhere});
	EXPECT_NE(FindEffect("Echo"), nullptr);

	ModuleEvents::ImageUnloading.Publish(EventBus::GetGlobal(), ImageUnloadingEvent{&_token});
	for (const std::string_view name : BUILTIN_NAMES) {
		EXPECT_EQ(FindEffect(name), nullptr) << name;
	}
	RegisterBuiltinEffects(&_token);
	RegisterBuiltinEffects(&_token);
	EXPECT_EQ(GetRegisteredEffectCount(), registered);
	EXPECT_EQ(FindEffect("Echo"), Builtin("Echo"));
}

TEST_F(BuiltinEffectFixture, DuckLowersGroupWhileKeyPlays) {
	GetAudioSystem().SetListenerPresent(true);
	AudioMixer mixer;
	const auto group = [](const char* id, const char* parent) {
		AudioMixer::Group made;
		made.id = id;
		made.name = id;
		made.parent = parent;
		return made;
	};
	AudioMixer::Group master = group("Master", "");
	master.units.push_back(AudioMixer::Unit{});
	AudioMixer::Group music = group("Music", "Master");
	AudioMixer::Unit duck;
	duck.kind = AudioMixerUnitKind::Effect;
	duck.id = "Duck";
	duck.effect = "Duck Volume";
	music.units.push_back(duck);
	music.units.push_back(AudioMixer::Unit{});
	AudioMixer::Group sfx = group("SFX", "Master");
	AudioMixer::Unit send;
	send.kind = AudioMixerUnitKind::Send;
	send.target = "Music";
	send.targetUnit = "Duck";
	sfx.units.push_back(send);
	sfx.units.push_back(AudioMixer::Unit{});
	mixer.groups.push_back(std::move(master));
	mixer.groups.push_back(std::move(music));
	mixer.groups.push_back(std::move(sfx));
	AudioMixer::Snapshot start;
	start.id = "Start";
	start.name = "Start";
	const auto value = [](const char* group, AudioMixerParameter parameter, const char* target, const char* effectParameter, f32 set) {
		AudioMixer::Value row;
		row.group = group;
		row.parameter = parameter;
		row.target = target;
		row.effectParameter = effectParameter;
		row.value = set;
		return row;
	};
	start.values.push_back(value("SFX", AudioMixerParameter::Volume, "", "", -80.0f));
	start.values.push_back(value("SFX", AudioMixerParameter::SendLevel, "Music", "", 0.0f));
	start.values.push_back(value("Music", AudioMixerParameter::EffectParameter, "Duck", "Threshold", -20.0f));
	start.values.push_back(value("Music", AudioMixerParameter::EffectParameter, "Duck", "Knee", 0.0f));
	start.values.push_back(value("Music", AudioMixerParameter::EffectParameter, "Duck", "Attack Time", 0.0f));
	start.values.push_back(value("Music", AudioMixerParameter::EffectParameter, "Duck", "Release Time", 0.0f));
	mixer.snapshots.push_back(std::move(start));
	ASSERT_TRUE(mixer.ApplyChanges());

	const auto loop = [&](f32 level, const char* bus) {
		AudioPlayParams params;
		params.loop = true;
		params.output = mixer.GetBus(bus);
		return Audio::Play(ResourceOf(MakeClip(96000, 1, level)), params);
	};
	const auto peakNear = [](f32 expected) { return WaitFor([expected]() { return std::fabs(LastBlockPeak() - expected) < 1e-3f; }); };
	const f32 heard = 0.25f * std::sqrt(0.5f);
	loop(0.25f, "Music");
	EXPECT_TRUE(peakNear(heard)) << LastBlockPeak();

	const AudioVoice effects = loop(0.5f, "SFX");
	const f32 over = Decibels(0.5f * std::sqrt(0.5f)) + 20.0f;
	EXPECT_TRUE(peakNear(heard * std::pow(10.0f, -over * 0.5f / 20.0f))) << LastBlockPeak();

	Audio::Stop(effects);
	EXPECT_TRUE(peakNear(heard)) << LastBlockPeak();
}

#endif
