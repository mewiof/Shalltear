#include "Effects/BuiltinEffects.h"
#include "Effects/EffectKit.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace Effects {

			namespace {

				constexpr AudioParameterDefinition RESONANT[] = {
					{"Cutoff freq", "Hz", 10.0f, 22000.0f, 5000.0f},
					{"Resonance", "", 1.0f, 10.0f, 1.0f},
				};

				constexpr AudioParameterDefinition SIMPLE[] = {
					{"Cutoff freq", "Hz", 10.0f, 22000.0f, 5000.0f},
				};

				constexpr AudioParameterDefinition PARAM_EQ[] = {
					{"Center freq", "Hz", 20.0f, 22000.0f, 8000.0f},
					{"Octave Range", "octaves", 0.2f, 5.0f, 1.0f},
					{"Frequency Gain", "x", 0.05f, 3.0f, 1.0f},
				};

				template <u32 N>
				class SectionEffect : public BuiltinEffect {
				public:
					explicit SectionEffect(const AudioEffectContext& context) : BuiltinEffect(context) {
						for (u32 index = 0; index < N; ++index) {
							_parameters[index].Jump(context.parameters[index]);
						}
					}

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						(void)sidechain;
						bool moving = false;
						for (AudioRamp& parameter : _parameters) {
							parameter.Advance(block.frames);
							moving = moving || !parameter.IsFlat();
						}
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						for (u32 from = 0; from < block.frames; from += SEGMENT_FRAMES) {
							const u32 to = from + SEGMENT_FRAMES < block.frames ? from + SEGMENT_FRAMES : block.frames;
							if (moving || !_designed) {
								f32 values[N];
								for (u32 index = 0; index < N; ++index) {
									values[index] = _parameters[index].At(to - 1);
								}
								_section = Design(values);
								_designed = true;
							}
							for (u32 channel = 0; channel < channels; ++channel) {
								f32* row = block.data + static_cast<size_t>(channel) * block.stride;
								BiquadMemory& memory = _memory[channel];
								for (u32 frame = from; frame < to; ++frame) {
									row[frame] = Filter(_section, memory, row[frame]);
								}
							}
						}
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index < N) {
							_parameters[index].Walk(value, _walkFrames);
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override {
						for (u32 channel = 0; channel < _channels; ++channel) {
							if (!_memory[channel].IsQuiet()) {
								return false;
							}
						}
						return true;
					}
				protected:
					virtual Biquad Design(const f32* values) const [[clang::nonblocking]] = 0;
				private:
					AudioRamp _parameters[N];
					Biquad _section;
					bool _designed = false;
					BiquadMemory _memory[AUDIO_MAX_CHANNELS];
				};

				template <Pass PASS>
				class ResonantPass final : public SectionEffect<2> {
				public:
					using SectionEffect<2>::SectionEffect;
				protected:
					Biquad Design(const f32* values) const [[clang::nonblocking]] override { return MakePass(PASS, values[0], values[1], _rate); }
				};

				class ParamEq final : public SectionEffect<3> {
				public:
					using SectionEffect<3>::SectionEffect;
				protected:
					Biquad Design(const f32* values) const [[clang::nonblocking]] override { return MakePeaking(values[0], values[1], values[2], _rate); }
				};

				template <Pass PASS>
				class SimplePass final : public BuiltinEffect {
				public:
					explicit SimplePass(const AudioEffectContext& context) : BuiltinEffect(context) { _cutoff.Jump(context.parameters[0]); }

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						(void)sidechain;
						_cutoff.Advance(block.frames);
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						for (u32 from = 0; from < block.frames; from += SEGMENT_FRAMES) {
							const u32 to = from + SEGMENT_FRAMES < block.frames ? from + SEGMENT_FRAMES : block.frames;
							const f32 warped = std::tan(PI * DesignFrequency(_cutoff.At(to - 1), _rate) / _rate);
							const f32 k = warped * (PASS == Pass::Low ? 1.55377397f : 0.64359425f);
							const f32 feedback = (k - 1.0f) / (k + 1.0f);
							const f32 gain = PASS == Pass::Low ? k / (k + 1.0f) : 1.0f / (k + 1.0f);
							const f32 b1 = PASS == Pass::Low ? gain : -gain;
							for (u32 channel = 0; channel < channels; ++channel) {
								f32* row = block.data + static_cast<size_t>(channel) * block.stride;
								f32 first = _memory[channel][0];
								f32 second = _memory[channel][1];
								for (u32 frame = from; frame < to; ++frame) {
									const f32 middle = gain * row[frame] + first;
									first = b1 * row[frame] - feedback * middle;
									const f32 out = gain * middle + second;
									second = b1 * middle - feedback * out;
									row[frame] = out;
								}
								_memory[channel][0] = first;
								_memory[channel][1] = second;
							}
						}
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index == 0) {
							_cutoff.Walk(value, _walkFrames);
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override {
						for (u32 channel = 0; channel < _channels; ++channel) {
							if (Abs(_memory[channel][0]) >= QUIET_LEVEL || Abs(_memory[channel][1]) >= QUIET_LEVEL) {
								return false;
							}
						}
						return true;
					}
				private:
					AudioRamp _cutoff;
					f32 _memory[AUDIO_MAX_CHANNELS][2]{};
				};

				BuiltinDefinition<ResonantPass<Pass::Low>> _lowPass(RESONANT);
				BuiltinDefinition<ResonantPass<Pass::High>> _highPass(RESONANT);
				BuiltinDefinition<SimplePass<Pass::Low>> _lowPassSimple(SIMPLE);
				BuiltinDefinition<SimplePass<Pass::High>> _highPassSimple(SIMPLE);
				BuiltinDefinition<ParamEq> _paramEq(PARAM_EQ);

			} // anonymous namespace

			IAudioEffectDefinition& GetLowPass() { return _lowPass; }
			IAudioEffectDefinition& GetHighPass() { return _highPass; }
			IAudioEffectDefinition& GetLowPassSimple() { return _lowPassSimple; }
			IAudioEffectDefinition& GetHighPassSimple() { return _highPassSimple; }
			IAudioEffectDefinition& GetParamEq() { return _paramEq; }

		} // namespace Effects

	} // namespace AudioImpl

} // namespace Shalltear
