#include "AudioBuiltinSpatializer.h"

#include "Shalltear/Core/Memory.h"

#include <cmath>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			using P = SpeakerPosition;

			BuiltinSpatializer _builtin;

			f32 Clamp(f32 value, f32 low, f32 high) [[clang::nonblocking]] {
				return value >= low ? (value <= high ? value : high) : low;
			}

			i32 SideOf(SpeakerPosition position) {
				switch (position) {
				case P::FrontLeft:
				case P::BackLeft:
				case P::SideLeft:
					return -1;
				case P::FrontRight:
				case P::BackRight:
				case P::SideRight:
					return 1;
				default:
					return 0;
				}
			}

			bool TakesDirection(SpeakerPosition position) {
				return position != P::None && position != P::LowFrequency && position != P::Mono;
			}

			struct Bearing {
				f32 degrees;
				f32 x;
				f32 z;
			};

			Bearing BearingOf(SpeakerPosition position, bool hasSides, bool hasBacks) {
				constexpr f32 SIN30 = 0.5f, COS30 = 0.86602540f;
				constexpr f32 SIN110 = 0.93969262f, COS110 = -0.34202014f;
				switch (position) {
				case P::FrontCenter:
					return {0.0f, 0.0f, 1.0f};
				case P::FrontLeft:
					return {-30.0f, -SIN30, COS30};
				case P::FrontRight:
					return {30.0f, SIN30, COS30};
				case P::SideLeft:
					return hasBacks ? Bearing{-90.0f, -1.0f, 0.0f} : Bearing{-110.0f, -SIN110, COS110};
				case P::SideRight:
					return hasBacks ? Bearing{90.0f, 1.0f, 0.0f} : Bearing{110.0f, SIN110, COS110};
				case P::BackLeft:
					return hasSides ? Bearing{-150.0f, -SIN30, -COS30} : Bearing{-110.0f, -SIN110, COS110};
				case P::BackRight:
					return hasSides ? Bearing{150.0f, SIN30, -COS30} : Bearing{110.0f, SIN110, COS110};
				case P::BackCenter:
				default:
					return {180.0f, 0.0f, -1.0f};
				}
			}

			f32 Turn(f32 ax, f32 az, f32 bx, f32 bz) [[clang::nonblocking]] {
				return az * bx - ax * bz;
			}

			struct BuiltinVoiceState {
				PanLayout layout;
				u32 inputChannels = 0;
				f32 downmix[MAX_AUDIO_CHANNELS]{};
				i32 side[MAX_AUDIO_CHANNELS]{};
				f32 pole = 0.0f;

				bool primed = false;
				f32 gains[MAX_AUDIO_CHANNELS * MAX_AUDIO_CHANNELS]{};
				f32 occlusion = 0.0f;
				f32 lowpass[MAX_AUDIO_CHANNELS]{};
				f32 spread = -1.0f;
				f32 spreadCos = 1.0f;
				f32 spreadSin = 0.0f;
			};

			void UpdateSpread(BuiltinVoiceState& state, f32 spread) [[clang::nonblocking]] {
				const f32 clamped = Clamp(spread, 0.0f, 360.0f);
				if (clamped == state.spread) {
					return;
				}
				state.spread = clamped;
				const f32 half = clamped * 0.5f * 0.017453292f;
				state.spreadCos = std::cos(half);
				state.spreadSin = std::sin(half);
			}

		} // anonymous namespace

		PanLayout MakePanLayout(const ChannelLayout& output) {
			PanLayout layout;
			layout.channels = output.count;
			bool hasSides = false;
			bool hasBacks = false;
			u32 directional = 0;
			for (u32 channel = 0; channel < output.count; ++channel) {
				const SpeakerPosition position = output.positions[channel];
				hasSides = hasSides || position == P::SideLeft || position == P::SideRight;
				hasBacks = hasBacks || position == P::BackLeft || position == P::BackRight;
				if (!TakesDirection(position)) {
					continue;
				}
				++directional;
				if (position == P::FrontLeft) {
					layout.left = static_cast<i32>(channel);
				} else if (position == P::FrontRight) {
					layout.right = static_cast<i32>(channel);
				}
			}

			if (directional == 2 && layout.left >= 0 && layout.right >= 0) {
				layout.stereo = true;
				return layout;
			}

			f32 degrees[MAX_AUDIO_CHANNELS]{};
			for (u32 channel = 0; channel < output.count; ++channel) {
				const SpeakerPosition position = output.positions[channel];
				if (!TakesDirection(position)) {
					continue;
				}
				const Bearing bearing = BearingOf(position, hasSides, hasBacks);
				u32 at = layout.ringCount;
				while (at > 0 && degrees[at - 1] > bearing.degrees) {
					degrees[at] = degrees[at - 1];
					layout.ring[at] = layout.ring[at - 1];
					layout.ringX[at] = layout.ringX[at - 1];
					layout.ringZ[at] = layout.ringZ[at - 1];
					--at;
				}
				degrees[at] = bearing.degrees;
				layout.ring[at] = channel;
				layout.ringX[at] = bearing.x;
				layout.ringZ[at] = bearing.z;
				++layout.ringCount;
			}

			if (layout.ringCount > 1) {
				const f32 wrap = degrees[0] + 360.0f - degrees[layout.ringCount - 1];
				layout.frontOnly = wrap >= 180.0f;
			} else {
				layout.frontOnly = true;
			}
			return layout;
		}

		void PanDirection(const PanLayout& layout, const Vec3& direction, f32* gains) {
			for (u32 channel = 0; channel < layout.channels; ++channel) {
				gains[channel] = 0.0f;
			}
			const f32 lengthSquared = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;

			if (layout.stereo) {
				const f32 lean = lengthSquared > 0.0f ? Clamp(direction.x / std::sqrt(lengthSquared), -1.0f, 1.0f) : 0.0f;
				gains[layout.left] = std::sqrt((1.0f - lean) * 0.5f);
				gains[layout.right] = std::sqrt((1.0f + lean) * 0.5f);
				return;
			}

			if (layout.ringCount == 0) {
				const f32 even = layout.channels > 0 ? 1.0f / std::sqrt(static_cast<f32>(layout.channels)) : 0.0f;
				for (u32 channel = 0; channel < layout.channels; ++channel) {
					gains[channel] = even;
				}
				return;
			}
			if (layout.ringCount == 1) {
				gains[layout.ring[0]] = 1.0f;
				return;
			}

			const f32 horizontalSquared = direction.x * direction.x + direction.z * direction.z;
			const f32 horizontal = lengthSquared > 0.0f ? horizontalSquared / lengthSquared : 0.0f;

			f32 pair[MAX_AUDIO_CHANNELS]{};
			if (horizontalSquared > 0.0f) {
				const f32 inverse = 1.0f / std::sqrt(horizontalSquared);
				const f32 px = direction.x * inverse;
				const f32 pz = layout.frontOnly && direction.z < 0.0f ? -direction.z * inverse : direction.z * inverse;

				const u32 pairs = layout.frontOnly ? layout.ringCount - 1 : layout.ringCount;
				bool found = false;
				for (u32 index = 0; index < pairs && !found; ++index) {
					const u32 next = (index + 1) % layout.ringCount;
					const f32 ax = layout.ringX[index], az = layout.ringZ[index];
					const f32 bx = layout.ringX[next], bz = layout.ringZ[next];
					if (Turn(ax, az, px, pz) < 0.0f || Turn(px, pz, bx, bz) < 0.0f) {
						continue;
					}
					const f32 determinant = ax * bz - bx * az;
					if (determinant == 0.0f) {
						continue;
					}
					f32 ga = (px * bz - bx * pz) / determinant;
					f32 gb = (ax * pz - px * az) / determinant;
					ga = ga > 0.0f ? ga : 0.0f;
					gb = gb > 0.0f ? gb : 0.0f;
					const f32 norm = std::sqrt(ga * ga + gb * gb);
					if (norm > 0.0f) {
						pair[index] = ga / norm;
						pair[next] = gb / norm;
						found = true;
					}
				}
				if (!found) {
					const u32 last = layout.ringCount - 1;
					const f32 first = layout.ringX[0] * px + layout.ringZ[0] * pz;
					const f32 end = layout.ringX[last] * px + layout.ringZ[last] * pz;
					pair[first >= end ? 0 : last] = 1.0f;
				}
			}

			const f32 even = (1.0f - horizontal) / static_cast<f32>(layout.ringCount);
			for (u32 index = 0; index < layout.ringCount; ++index) {
				gains[layout.ring[index]] = std::sqrt(horizontal * pair[index] * pair[index] + even);
			}
		}

		BuiltinSpatializer& GetBuiltinSpatializer() { return _builtin; }

		void* BuiltinSpatializer::CreateVoiceState(const AudioSpatializerContext& context) {
			BuiltinVoiceState* state = New<BuiltinVoiceState>(MemTag::Audio);

			ChannelLayout output;
			output.count = context.outputChannels < MAX_AUDIO_CHANNELS ? context.outputChannels : MAX_AUDIO_CHANNELS;
			for (u32 channel = 0; channel < output.count; ++channel) {
				output.positions[channel] = context.outputSpeakers[channel];
			}
			state->layout = MakePanLayout(output);

			ChannelLayout input;
			input.count = context.inputChannels < MAX_AUDIO_CHANNELS ? context.inputChannels : MAX_AUDIO_CHANNELS;
			for (u32 channel = 0; channel < input.count; ++channel) {
				input.positions[channel] = context.inputSpeakers[channel];
				state->side[channel] = SideOf(input.positions[channel]);
			}
			state->inputChannels = input.count;

			ChannelLayout mono;
			mono.count = 1;
			mono.positions[0] = P::Mono;
			f32 folds[MAX_AUDIO_CHANNELS]{};
			ComputeChannelGains(input, mono, 0.0f, folds);
			for (u32 channel = 0; channel < input.count; ++channel) {
				state->downmix[channel] = folds[channel];
			}

			state->pole = context.sampleRate > 0
							  ? std::exp(-2.0f * 3.14159265f * OCCLUSION_CUTOFF_HZ / static_cast<f32>(context.sampleRate))
							  : 0.0f;
			return state;
		}

		void BuiltinSpatializer::DestroyVoiceState(void* state) {
			if (state) {
				Delete(static_cast<BuiltinVoiceState*>(state));
			}
		}

		void BuiltinSpatializer::Process(void* opaque, const AudioSpatialParams& params, const AudioBlock& in, AudioBlock& out) {
			const u32 frames = out.frames < in.frames ? out.frames : in.frames;
			for (u32 channel = 0; channel < out.channels; ++channel) {
				f32* row = out.data + static_cast<size_t>(channel) * out.stride;
				for (u32 frame = 0; frame < out.frames; ++frame) {
					row[frame] = 0.0f;
				}
			}
			if (!opaque || frames == 0) {
				return;
			}
			BuiltinVoiceState& state = *static_cast<BuiltinVoiceState*>(opaque);
			const u32 inputs = in.channels < state.inputChannels ? in.channels : state.inputChannels;
			const u32 outputs = out.channels < state.layout.channels ? out.channels : state.layout.channels;

			UpdateSpread(state, params.spread);
			f32 target[MAX_AUDIO_CHANNELS * MAX_AUDIO_CHANNELS]{};
			f32 column[MAX_AUDIO_CHANNELS];
			for (u32 input = 0; input < inputs; ++input) {
				const f32 share = state.downmix[input];
				if (share == 0.0f) {
					continue;
				}
				Vec3 direction = params.position;
				if (state.side[input] != 0 && state.spreadSin != 0.0f) {
					const f32 s = state.side[input] > 0 ? state.spreadSin : -state.spreadSin;
					const f32 x = direction.x * state.spreadCos + direction.z * s;
					const f32 z = direction.z * state.spreadCos - direction.x * s;
					direction.x = x;
					direction.z = z;
				}
				PanDirection(state.layout, direction, column);
				for (u32 output = 0; output < outputs; ++output) {
					target[output * MAX_AUDIO_CHANNELS + input] = column[output] * share;
				}
			}

			const f32 occlusion = Clamp(params.occlusion, 0.0f, 1.0f);
			if (!state.primed) {
				for (u32 index = 0; index < MAX_AUDIO_CHANNELS * MAX_AUDIO_CHANNELS; ++index) {
					state.gains[index] = target[index];
				}
				state.occlusion = occlusion;
				state.primed = true;
			}

			bool moving = false;
			for (u32 output = 0; output < outputs && !moving; ++output) {
				for (u32 input = 0; input < inputs; ++input) {
					const u32 index = output * MAX_AUDIO_CHANNELS + input;
					if (state.gains[index] != target[index]) {
						moving = true;
						break;
					}
				}
			}

			const f32 through = 1.0f - state.pole;
			f32 occluded[MAX_AUDIO_CHANNELS];
			for (u32 frame = 0; frame < frames; ++frame) {
				const f32 progress = static_cast<f32>(frame + 1) / static_cast<f32>(frames);
				const f32 amount = state.occlusion + (occlusion - state.occlusion) * progress;
				for (u32 input = 0; input < inputs; ++input) {
					const f32 x = in.data[static_cast<size_t>(input) * in.stride + frame];
					state.lowpass[input] += (x - state.lowpass[input]) * through;
					occluded[input] = x + amount * (OCCLUSION_GAIN * state.lowpass[input] - x);
				}
				for (u32 output = 0; output < outputs; ++output) {
					const f32* from = state.gains + output * MAX_AUDIO_CHANNELS;
					const f32* to = target + output * MAX_AUDIO_CHANNELS;
					f32 mixed = 0.0f;
					if (moving) {
						for (u32 input = 0; input < inputs; ++input) {
							mixed += (from[input] + (to[input] - from[input]) * progress) * occluded[input];
						}
					} else {
						for (u32 input = 0; input < inputs; ++input) {
							mixed += to[input] * occluded[input];
						}
					}
					out.data[static_cast<size_t>(output) * out.stride + frame] = mixed;
				}
			}

			for (u32 index = 0; index < MAX_AUDIO_CHANNELS * MAX_AUDIO_CHANNELS; ++index) {
				state.gains[index] = target[index];
			}
			state.occlusion = occlusion;
		}

	} // namespace AudioImpl

} // namespace Shalltear
