#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioEffect.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include "AudioBus.h"

#include <cmath>
#include <cstring>
#include <span>

namespace Shalltear {

	namespace AudioImpl {

		namespace Effects {

			inline constexpr f32 PI = 3.14159265358979f;
			inline constexpr f32 TAU = 6.28318530717959f;

			inline constexpr f32 QUIET_LEVEL = 1e-7f;

			inline constexpr f32 WALK_SECONDS = 0.02f;

			inline constexpr u32 SEGMENT_FRAMES = 32;

			inline f32 Clamp(f32 value, f32 low, f32 high) [[clang::nonblocking]] {
				return value >= low ? (value <= high ? value : high) : low;
			}

			inline f32 Abs(f32 value) [[clang::nonblocking]] { return value < 0.0f ? -value : value; }

			inline f32 DecibelsToGain(f32 decibels) [[clang::nonblocking]] { return std::exp(decibels * 0.115129255f); }

			inline f32 GainToDecibels(f32 gain) [[clang::nonblocking]] { return gain > 1e-10f ? std::log(gain) * 8.68588964f : -200.0f; }

			inline f32 MillibelsToGain(f32 millibels) [[clang::nonblocking]] { return DecibelsToGain(millibels * 0.01f); }

			inline f32 OnePole(f32 seconds, f32 rate) [[clang::nonblocking]] {
				return seconds > 0.0f ? 1.0f - std::exp(-1.0f / (seconds * rate)) : 1.0f;
			}

			inline f32 Loudest(const f32* samples, u32 count) [[clang::nonblocking]] {
				f32 loudest = 0.0f;
				for (u32 index = 0; index < count; ++index) {
					const f32 magnitude = Abs(samples[index]);
					loudest = magnitude > loudest ? magnitude : loudest;
				}
				return loudest;
			}

			class BuiltinEffect : public IAudioEffect {
			public:
				explicit BuiltinEffect(const AudioEffectContext& context)
					: _channels(context.channels), _rate(static_cast<f32>(context.sampleRate)),
					  _walkFrames(static_cast<u32>(WALK_SECONDS * static_cast<f32>(context.sampleRate)) + 1) {}
				virtual ~BuiltinEffect() = default;

				void Release() override { Delete(this); }

				static constexpr bool READS_SIDECHAIN = false;
			protected:
				u32 _channels;
				f32 _rate;
				u32 _walkFrames;
			};

			template <typename Instance>
			class BuiltinDefinition final : public IAudioEffectDefinition {
			public:
				explicit BuiltinDefinition(std::span<const AudioParameterDefinition> parameters) : _parameters(parameters) {}

				std::span<const AudioParameterDefinition> GetParameters() const override { return _parameters; }

				bool IsSidechainTarget() const override { return Instance::READS_SIDECHAIN; }

				IAudioEffect* CreateEffect(const AudioEffectContext& context) override {
					if (context.channels == 0 || context.channels > AUDIO_MAX_CHANNELS || context.sampleRate == 0 || context.blockFrames == 0 ||
						context.parameters.size() != _parameters.size()) {
						return nullptr;
					}
					return New<Instance>(MemTag::Audio, context);
				}
			private:
				std::span<const AudioParameterDefinition> _parameters;
			};

			// Audio EQ Cookbook designs, normalized by a0
			struct Biquad {
				f32 b0 = 1.0f;
				f32 b1 = 0.0f;
				f32 b2 = 0.0f;
				f32 a1 = 0.0f;
				f32 a2 = 0.0f;
			};

			struct BiquadMemory {
				f32 z1 = 0.0f;
				f32 z2 = 0.0f;

				bool IsQuiet() const [[clang::nonblocking]] { return Abs(z1) < QUIET_LEVEL && Abs(z2) < QUIET_LEVEL; }
			};

			inline f32 Filter(const Biquad& section, BiquadMemory& memory, f32 input) [[clang::nonblocking]] {
				const f32 output = section.b0 * input + memory.z1;
				memory.z1 = section.b1 * input - section.a1 * output + memory.z2;
				memory.z2 = section.b2 * input - section.a2 * output;
				return output;
			}

			inline f32 DesignFrequency(f32 frequency, f32 rate) [[clang::nonblocking]] { return Clamp(frequency, 1.0f, rate * 0.49f); }

			enum class Pass : u8 { Low,
								   High };

			inline Biquad MakePass(Pass pass, f32 frequency, f32 q, f32 rate) [[clang::nonblocking]] {
				const f32 w0 = TAU * DesignFrequency(frequency, rate) / rate;
				const f32 cosine = std::cos(w0);
				const f32 alpha = std::sin(w0) / (2.0f * (q > 0.1f ? q : 0.1f));
				const f32 inverse = 1.0f / (1.0f + alpha);
				Biquad section;
				if (pass == Pass::Low) {
					section.b1 = (1.0f - cosine) * inverse;
					section.b0 = section.b2 = section.b1 * 0.5f;
				} else {
					section.b1 = -(1.0f + cosine) * inverse;
					section.b0 = section.b2 = -section.b1 * 0.5f;
				}
				section.a1 = -2.0f * cosine * inverse;
				section.a2 = (1.0f - alpha) * inverse;
				return section;
			}

			inline Biquad MakePeaking(f32 frequency, f32 octaves, f32 gain, f32 rate) [[clang::nonblocking]] {
				const f32 w0 = TAU * DesignFrequency(frequency, rate) / rate;
				const f32 sine = std::sin(w0);
				const f32 cosine = std::cos(w0);
				const f32 x = 0.34657359f * octaves * w0 / (sine > 1e-6f ? sine : 1e-6f);
				const f32 alpha = sine * 0.5f * (std::exp(x) - std::exp(-x));
				const f32 amplitude = std::sqrt(gain > 1e-6f ? gain : 1e-6f);
				const f32 inverse = 1.0f / (1.0f + alpha / amplitude);
				Biquad section;
				section.b0 = (1.0f + alpha * amplitude) * inverse;
				section.b1 = -2.0f * cosine * inverse;
				section.b2 = (1.0f - alpha * amplitude) * inverse;
				section.a1 = section.b1;
				section.a2 = (1.0f - alpha / amplitude) * inverse;
				return section;
			}

			class DelayLines {
			public:
				DelayLines() = default;
				DelayLines(const DelayLines&) = delete;
				DelayLines& operator=(const DelayLines&) = delete;
				~DelayLines() { Free(_samples); }

				void Allocate(u32 channels, u32 length) {
					_length = length > 2 ? length : 2;
					_channels = channels;
					const size_t bytes = sizeof(f32) * static_cast<size_t>(_length) * channels;
					_samples = static_cast<f32*>(Shalltear::Allocate(bytes, MemTag::Audio, 64));
					std::memset(_samples, 0, bytes);
				}

				u32 GetLength() const [[clang::nonblocking]] { return _length; }

				f32 Read(u32 channel, f32 delay) const [[clang::nonblocking]] {
					const f32 clamped = Clamp(delay, 1.0f, static_cast<f32>(_length - 2));
					const u32 whole = static_cast<u32>(clamped);
					const f32 fraction = clamped - static_cast<f32>(whole);
					const f32* line = _samples + static_cast<size_t>(channel) * _length;
					const u32 first = (_write + _length - whole) % _length;
					const u32 second = (first + _length - 1) % _length;
					return line[first] + (line[second] - line[first]) * fraction;
				}

				void Write(u32 channel, f32 value) [[clang::nonblocking]] { _samples[static_cast<size_t>(channel) * _length + _write] = value; }

				void Advance() [[clang::nonblocking]] { _write = _write + 1 == _length ? 0 : _write + 1; }
			private:
				f32* _samples = nullptr;
				u32 _length = 0;
				u32 _channels = 0;
				u32 _write = 0;
			};

			struct SilenceCount {
				u32 frames = 0;

				void Note(f32 loudest) [[clang::nonblocking]] { frames = loudest < QUIET_LEVEL ? (frames < 0x7FFFFFFFu ? frames + 1 : frames) : 0; }

				void NoteBlock(f32 loudest, u32 count) [[clang::nonblocking]] {
					frames = loudest < QUIET_LEVEL ? (frames < 0x7FFFFFFFu - count ? frames + count : frames) : 0;
				}
				bool Covers(u32 length) const [[clang::nonblocking]] { return frames >= length; }
			};

			struct Quadrature {
				f32 sine = 0.0f;
				f32 cosine = 1.0f;
				f32 stepSine = 0.0f;
				f32 stepCosine = 1.0f;

				void SetRate(f32 hertz, f32 rate) [[clang::nonblocking]] {
					const f32 step = TAU * hertz / rate;
					stepSine = std::sin(step);
					stepCosine = std::cos(step);
					const f32 length = std::sqrt(sine * sine + cosine * cosine);
					if (length > 0.0f) {
						sine /= length;
						cosine /= length;
					}
				}

				void Turn() [[clang::nonblocking]] {
					const f32 nextSine = sine * stepCosine + cosine * stepSine;
					cosine = cosine * stepCosine - sine * stepSine;
					sine = nextSine;
				}
			};

		} // namespace Effects

	} // namespace AudioImpl

} // namespace Shalltear
