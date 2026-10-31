#include "Effects/BuiltinEffects.h"
#include "Effects/EffectKit.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace Effects {

			namespace {

				constexpr AudioParameterDefinition SFX_REVERB[] = {
					{"Dry Level", "mB", -10000.0f, 0.0f, 0.0f},
					{"Room", "mB", -10000.0f, 0.0f, -10000.0f},
					{"Room HF", "mB", -10000.0f, 0.0f, 0.0f},
					{"Decay Time", "s", 0.1f, 20.0f, 1.0f},
					{"Decay HF Ratio", "", 0.1f, 2.0f, 0.5f},
					{"Reflections", "mB", -10000.0f, 1000.0f, -10000.0f},
					{"Reflect Delay", "s", 0.0f, 0.3f, 0.02f},
					{"Reverb", "mB", -10000.0f, 2000.0f, 0.0f},
					{"Reverb Delay", "s", 0.0f, 0.1f, 0.04f},
					{"Diffusion", "%", 0.0f, 100.0f, 100.0f},
					{"Density", "%", 0.0f, 100.0f, 100.0f},
					{"HFReference", "Hz", 20.0f, 20000.0f, 5000.0f},
					{"Room LF", "mB", -10000.0f, 0.0f, -10000.0f},
					{"LFReference", "Hz", 20.0f, 1000.0f, 250.0f},
				};

				enum : u32 {
					DRY = 0,
					ROOM,
					ROOM_HF,
					DECAY_TIME,
					DECAY_HF_RATIO,
					REFLECTIONS,
					REFLECT_DELAY,
					REVERB,
					REVERB_DELAY,
					DIFFUSION,
					DENSITY,
					HF_REFERENCE,
					ROOM_LF,
					LF_REFERENCE,
					PARAMETERS
				};

				constexpr u32 LINES = 16;
				constexpr u32 TAPS = 8;
				constexpr u32 DIFFUSERS = 4;

				constexpr f32 LINE_SECONDS[LINES] = {0.0211f, 0.0239f, 0.0263f, 0.0283f, 0.0307f, 0.0331f, 0.0353f, 0.0379f,
													 0.0401f, 0.0427f, 0.0449f, 0.0479f, 0.0503f, 0.0541f, 0.0577f, 0.0631f};
				constexpr f32 LEAST_DENSITY = 0.3f;
				// offsets after Reflect Delay
				constexpr f32 TAP_SECONDS[TAPS] = {0.0f, 0.0043f, 0.0089f, 0.0131f, 0.0173f, 0.0211f, 0.0257f, 0.0293f};
				constexpr f32 DIFFUSER_SECONDS[DIFFUSERS] = {0.0047f, 0.0036f, 0.0127f, 0.0093f};
				constexpr f32 MOST_DIFFUSION = 0.7f;
				constexpr f32 LINE_SCALE = 0.25f;

				template <u32 N>
				void Hadamard(f32 (&values)[N]) [[clang::nonblocking]] {
					for (u32 span = 1; span < N; span <<= 1) {
						for (u32 start = 0; start < N; start += span << 1) {
							for (u32 index = start; index < start + span; ++index) {
								const f32 first = values[index];
								const f32 second = values[index + span];
								values[index] = first + second;
								values[index + span] = first - second;
							}
						}
					}
				}

				f32 LevelOf(f32 millibels) [[clang::nonblocking]] { return millibels <= -10000.0f ? 0.0f : MillibelsToGain(millibels); }

				f32 WholeFrames(f32 frames) [[clang::nonblocking]] { return static_cast<f32>(static_cast<u32>(frames + 0.5f)); }

				struct LineFilter {
					f32 b0 = 1.0f;
					f32 b1 = 0.0f;
					f32 a1 = 0.0f;
				};

				LineFilter DampingOf(f32 low, f32 high, f32 w) [[clang::nonblocking]] {
					LineFilter filter;
					const f32 ratio = high / low;
					if (ratio <= 1.0f) {
						const f32 p = ratio * ratio;
						const f32 half = std::sin(w * 0.5f);
						const f32 squared = 1.0f - half * half;
						const f32 root = 4.0f * half * std::sqrt(p * (1.0f - p * squared));
						const f32 b = 2.0f - 2.0f * p * (1.0f - 2.0f * half * half);
						const f32 pole = Clamp(2.0f * (1.0f - p) / (b + root), 0.0f, 0.999f);
						filter.b0 = low * (1.0f - pole);
						filter.a1 = -pole;
						return filter;
					}
					const f32 k = std::tan(w * 0.5f) * 0.5f;
					f32 top = std::sqrt(1.25f * ratio * ratio - 0.25f);
					top = top * low < 0.999f ? top : 0.999f / low;
					filter.b0 = low * (top + k) / (1.0f + k);
					filter.b1 = low * (k - top) / (1.0f + k);
					filter.a1 = (k - 1.0f) / (k + 1.0f);
					return filter;
				}

				struct Split {
					f32 gain = 0.0f;
					f32 feedback = 0.0f;
					f32 memory = 0.0f;

					void Tune(f32 frequency, f32 rate) [[clang::nonblocking]] {
						const f32 k = std::tan(PI * DesignFrequency(frequency, rate) / rate);
						gain = k / (k + 1.0f);
						feedback = (k - 1.0f) / (k + 1.0f);
					}

					f32 Low(f32 input) [[clang::nonblocking]] {
						const f32 low = gain * input + memory;
						memory = gain * input - feedback * low;
						return low;
					}
				};

				struct Levels {
					f32 dry = 0.0f;
					f32 early = 0.0f;
					f32 late = 0.0f;
					f32 roomHf = 0.0f;
					f32 roomLf = 0.0f;
				};

				class SfxReverb final : public BuiltinEffect {
				public:
					explicit SfxReverb(const AudioEffectContext& context) : BuiltinEffect(context) {
						for (u32 index = 0; index < PARAMETERS; ++index) {
							_parameters[index].Jump(context.parameters[index]);
						}
						const f32 reach = SFX_REVERB[REVERB_DELAY].maximum > TAP_SECONDS[TAPS - 1] ? SFX_REVERB[REVERB_DELAY].maximum : TAP_SECONDS[TAPS - 1];
						_predelay.Allocate(1, static_cast<u32>((SFX_REVERB[REFLECT_DELAY].maximum + reach) * _rate) + 4);
						_lines.Allocate(LINES, static_cast<u32>(LINE_SECONDS[LINES - 1] * _rate) + 4);
						_diffusers.Allocate(DIFFUSERS, static_cast<u32>(DIFFUSER_SECONDS[2] * _rate) + 4);
						_memory = _predelay.GetLength();
						for (u32 diffuser = 0; diffuser < DIFFUSERS; ++diffuser) {
							_diffuserFrames[diffuser] = static_cast<f32>(static_cast<u32>(DIFFUSER_SECONDS[diffuser] * _rate));
						}
						f32 energy = 0.0f;
						for (u32 tap = 0; tap < TAPS; ++tap) {
							_tapFrames[tap] = WholeFrames(TAP_SECONDS[tap] * _rate);
							_tapGains[tap] = std::exp(-0.105360516f * static_cast<f32>(tap));
							energy += _tapGains[tap] * _tapGains[tap];
						}
						for (f32& gain : _tapGains) {
							gain /= std::sqrt(energy);
						}
						Design();
					}

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						(void)sidechain;
						bool moving = false;
						for (AudioRamp& parameter : _parameters) {
							parameter.Advance(block.frames);
							moving = moving || !parameter.IsFlat();
						}
						for (u32 line = 0; line < LINES; ++line) {
							_lengthStart[line] = _lengthEnd[line];
						}
						const Levels from = _levels;
						if (moving) {
							Design();
						}
						const Levels& to = _levels;
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						const f32 per = block.frames > 0 ? 1.0f / static_cast<f32>(block.frames) : 0.0f;
						const f32 inverseChannels = 1.0f / static_cast<f32>(channels > 0 ? channels : 1);

						f32 loudest = 0.0f;
						for (u32 frame = 0; frame < block.frames; ++frame) {
							const f32 at = static_cast<f32>(frame + 1) * per;

							f32 room = 0.0f;
							for (u32 channel = 0; channel < channels; ++channel) {
								room += block.data[static_cast<size_t>(channel) * block.stride + frame];
							}
							room *= inverseChannels;
							const f32 below = _roomHf.Low(room);
							room = below + (from.roomHf + (to.roomHf - from.roomHf) * at) * (room - below);
							const f32 bass = _roomLf.Low(room);
							room += (from.roomLf + (to.roomLf - from.roomLf) * at - 1.0f) * bass;
							_predelay.Write(0, room);
							loudest = Abs(room) > loudest ? Abs(room) : loudest;

							const f32 reflect = _parameters[REFLECT_DELAY].At(frame) * _rate;
							f32 taps[TAPS];
							for (u32 tap = 0; tap < TAPS; ++tap) {
								taps[tap] = _tapGains[tap] * _predelay.Read(0, reflect + _tapFrames[tap]);
							}

							const f32 shortest = _lengthStart[0] + (_lengthEnd[0] - _lengthStart[0]) * at;
							f32 late = _predelay.Read(0, reflect + _parameters[REVERB_DELAY].At(frame) * _rate - shortest);
							for (u32 diffuser = 0; diffuser < DIFFUSERS; ++diffuser) {
								const f32 delayed = _diffusers.Read(diffuser, _diffuserFrames[diffuser]);
								const f32 written = late - _diffusion * delayed;
								_diffusers.Write(diffuser, written);
								loudest = Abs(written) > loudest ? Abs(written) : loudest;
								late = _diffusion * written + delayed;
							}
							_predelay.Advance();
							_diffusers.Advance();

							f32 outputs[LINES];
							f32 mixed[LINES];
							for (u32 line = 0; line < LINES; ++line) {
								outputs[line] = _lines.Read(line, _lengthStart[line] + (_lengthEnd[line] - _lengthStart[line]) * at);
								const LineFilter& filter = _filters[line];
								mixed[line] = filter.b0 * outputs[line] + _state[line];
								_state[line] = filter.b1 * outputs[line] - filter.a1 * mixed[line];
							}
							Hadamard(mixed);
							for (u32 line = 0; line < LINES; ++line) {
								const f32 written = late + mixed[line] * LINE_SCALE;
								_lines.Write(line, written);
								loudest = Abs(written) > loudest ? Abs(written) : loudest;
							}
							_lines.Advance();

							Hadamard(taps);
							Hadamard(outputs);
							const f32 dry = from.dry + (to.dry - from.dry) * at;
							const f32 early = from.early + (to.early - from.early) * at;
							const f32 reverberation = from.late + (to.late - from.late) * at;
							for (u32 channel = 0; channel < channels; ++channel) {
								f32& sample = block.data[static_cast<size_t>(channel) * block.stride + frame];
								sample = sample * dry + taps[channel] * early + outputs[channel + 1] * reverberation;
							}
						}
						_silence.NoteBlock(loudest, block.frames);
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index < PARAMETERS) {
							_parameters[index].Walk(value, _walkFrames);
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override { return _silence.Covers(_memory); }
				private:
					void Design() [[clang::nonblocking]] {
						const f32 density = LEAST_DENSITY + (1.0f - LEAST_DENSITY) * _parameters[DENSITY].end * 0.01f;
						const f32 decay = _parameters[DECAY_TIME].end;
						const f32 decayHf = decay * _parameters[DECAY_HF_RATIO].end;
						const f32 w = TAU * DesignFrequency(_parameters[HF_REFERENCE].end, _rate) / _rate;
						f32 energy = 0.0f;
						for (u32 line = 0; line < LINES; ++line) {
							_lengthEnd[line] = WholeFrames(LINE_SECONDS[line] * density * _rate);
							const f32 seconds = _lengthEnd[line] / _rate;
							const f32 low = DecibelsToGain(-60.0f * seconds / decay);
							_filters[line] = DampingOf(low, DecibelsToGain(-60.0f * seconds / decayHf), w);
							energy += low * low;
						}
						_roomHf.Tune(_parameters[HF_REFERENCE].end, _rate);
						_roomLf.Tune(_parameters[LF_REFERENCE].end, _rate);
						_diffusion = MOST_DIFFUSION * _parameters[DIFFUSION].end * 0.01f;
						const f32 room = LevelOf(_parameters[ROOM].end);
						_levels.dry = LevelOf(_parameters[DRY].end);
						_levels.early = room * LevelOf(_parameters[REFLECTIONS].end);
						_levels.late = room * LevelOf(_parameters[REVERB].end) * std::sqrt(1.0f - energy / static_cast<f32>(LINES)) * LINE_SCALE;
						_levels.roomHf = LevelOf(_parameters[ROOM_HF].end);
						_levels.roomLf = LevelOf(_parameters[ROOM_LF].end);
					}

					AudioRamp _parameters[PARAMETERS];
					DelayLines _predelay;
					DelayLines _lines;
					DelayLines _diffusers;
					u32 _memory = 0;
					f32 _diffuserFrames[DIFFUSERS]{};
					f32 _tapFrames[TAPS]{};
					f32 _tapGains[TAPS]{};
					f32 _diffusion = 0.0f;
					Split _roomHf;
					Split _roomLf;
					LineFilter _filters[LINES];
					f32 _state[LINES]{};
					f32 _lengthStart[LINES]{};
					f32 _lengthEnd[LINES]{};
					Levels _levels;
					SilenceCount _silence;
				};

				BuiltinDefinition<SfxReverb> _sfxReverb(SFX_REVERB);

			} // anonymous namespace

			IAudioEffectDefinition& GetSfxReverb() { return _sfxReverb; }

		} // namespace Effects

	} // namespace AudioImpl

} // namespace Shalltear
