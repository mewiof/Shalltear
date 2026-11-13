#include "Effects/BuiltinEffects.h"
#include "Effects/EffectKit.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace Effects {

			namespace {

				constexpr AudioParameterDefinition EchoParameters[] = {
					{"Delay", "ms", 10.0f, 5000.0f, 500.0f},
					{"Decay", "%", 0.0f, 100.0f, 50.0f},
					{"Drymix", "%", 0.0f, 100.0f, 100.0f},
					{"Wetmix", "%", 0.0f, 100.0f, 100.0f},
				};

				constexpr AudioParameterDefinition FlangeParameters[] = {
					{"Drymix", "%", 0.0f, 100.0f, 45.0f},
					{"Wetmix", "%", 0.0f, 100.0f, 55.0f},
					{"Depth", "", 0.01f, 1.0f, 1.0f},
					{"Rate", "Hz", 0.1f, 20.0f, 10.0f},
				};

				constexpr AudioParameterDefinition ChorusParameters[] = {
					{"Dry mix", "", 0.0f, 1.0f, 0.5f},
					{"Wet mix tap 1", "", 0.0f, 1.0f, 0.5f},
					{"Wet mix tap 2", "", 0.0f, 1.0f, 0.5f},
					{"Wet mix tap 3", "", 0.0f, 1.0f, 0.5f},
					{"Delay", "ms", 0.1f, 100.0f, 40.0f},
					{"Rate", "Hz", 0.0f, 20.0f, 0.8f},
					{"Depth", "", 0.0f, 1.0f, 0.03f},
					{"Feedback", "", 0.0f, 1.0f, 0.0f},
				};

				u32 LineFrames(f32 seconds, f32 rate) { return static_cast<u32>(seconds * rate) + 4; }

				template <u32 N>
				void Start(AudioRamp (&parameters)[N], const AudioEffectContext& context) {
					for (u32 index = 0; index < N; ++index) {
						parameters[index].Jump(context.parameters[index]);
					}
				}

				template <u32 N>
				void AdvanceAll(AudioRamp (&parameters)[N], u32 frames) [[clang::nonblocking]] {
					for (AudioRamp& parameter : parameters) {
						parameter.Advance(frames);
					}
				}

				class Echo final : public BuiltinEffect {
				public:
					explicit Echo(const AudioEffectContext& context) : BuiltinEffect(context) {
						Start(_parameters, context);
						_lines.Allocate(_channels, LineFrames(EchoParameters[Delay].maximum * 0.001f, _rate));
					}

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						(void)sidechain;
						AdvanceAll(_parameters, block.frames);
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						const f32 framesPerMillisecond = _rate / 1000.0f;
						for (u32 frame = 0; frame < block.frames; ++frame) {
							const f32 delay = _parameters[Delay].At(frame) * framesPerMillisecond;
							const f32 decay = _parameters[Decay].At(frame) * 0.01f;
							const f32 dry = _parameters[Dry].At(frame) * 0.01f;
							const f32 wet = _parameters[Wet].At(frame) * 0.01f;
							f32 loudest = 0.0f;
							for (u32 channel = 0; channel < channels; ++channel) {
								f32& sample = block.data[static_cast<size_t>(channel) * block.stride + frame];
								const f32 back = _lines.Read(channel, delay);
								const f32 written = sample + back * decay;
								_lines.Write(channel, written);
								loudest = Abs(written) > loudest ? Abs(written) : loudest;
								sample = sample * dry + back * wet;
							}
							_lines.Advance();
							_silence.Note(loudest);
						}
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index < 4) {
							_parameters[index].Walk(value, _walkFrames);
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override { return _silence.Covers(_lines.GetLength()); }
				private:
					enum : u32 { Delay = 0,
								 Decay,
								 Dry,
								 Wet };
					AudioRamp _parameters[4];
					DelayLines _lines;
					SilenceCount _silence;
				};

				class Flange final : public BuiltinEffect {
				public:
					static constexpr f32 FLANGE_BASE_SECONDS = 0.0001f;
					static constexpr f32 FLANGE_SWEEP_SECONDS = 0.01f;

					explicit Flange(const AudioEffectContext& context) : BuiltinEffect(context) {
						Start(_parameters, context);
						_lines.Allocate(_channels, LineFrames(FLANGE_BASE_SECONDS + FLANGE_SWEEP_SECONDS, _rate));
					}

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						(void)sidechain;
						AdvanceAll(_parameters, block.frames);
						_lfo.SetRate(_parameters[Rate].end, _rate);
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						const f32 base = FLANGE_BASE_SECONDS * _rate;
						const f32 sweep = FLANGE_SWEEP_SECONDS * _rate;
						for (u32 frame = 0; frame < block.frames; ++frame) {
							const f32 delay = base + sweep * _parameters[Depth].At(frame) * (0.5f + 0.5f * _lfo.sine);
							const f32 dry = _parameters[Dry].At(frame) * 0.01f;
							const f32 wet = _parameters[Wet].At(frame) * 0.01f;
							f32 loudest = 0.0f;
							for (u32 channel = 0; channel < channels; ++channel) {
								f32& sample = block.data[static_cast<size_t>(channel) * block.stride + frame];
								_lines.Write(channel, sample);
								loudest = Abs(sample) > loudest ? Abs(sample) : loudest;
								sample = sample * dry + _lines.Read(channel, delay) * wet;
							}
							_lines.Advance();
							_lfo.Turn();
							_silence.Note(loudest);
						}
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index < 4) {
							_parameters[index].Walk(value, _walkFrames);
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override { return _silence.Covers(_lines.GetLength()); }
				private:
					enum : u32 { Dry = 0,
								 Wet,
								 Depth,
								 Rate };
					AudioRamp _parameters[4];
					DelayLines _lines;
					Quadrature _lfo;
					SilenceCount _silence;
				};

				class Chorus final : public BuiltinEffect {
				public:
					explicit Chorus(const AudioEffectContext& context) : BuiltinEffect(context) {
						Start(_parameters, context);
						_lines.Allocate(_channels, LineFrames(2.0f * ChorusParameters[Delay].maximum * 0.001f, _rate));
					}

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						(void)sidechain;
						AdvanceAll(_parameters, block.frames);
						_lfo.SetRate(_parameters[Rate].end, _rate);
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						const f32 framesPerMillisecond = _rate / 1000.0f;
						for (u32 frame = 0; frame < block.frames; ++frame) {
							const f32 delay = _parameters[Delay].At(frame) * framesPerMillisecond;
							const f32 swing = _parameters[Depth].At(frame) * delay;
							const f32 first = delay + swing * _lfo.sine;
							const f32 second = delay + swing * _lfo.cosine;
							const f32 third = delay - swing * _lfo.sine;
							const f32 dry = _parameters[Dry].At(frame);
							const f32 tap1 = _parameters[Tap1].At(frame);
							const f32 tap2 = _parameters[Tap2].At(frame);
							const f32 tap3 = _parameters[Tap3].At(frame);
							const f32 feedback = _parameters[Feedback].At(frame);
							f32 loudest = 0.0f;
							for (u32 channel = 0; channel < channels; ++channel) {
								f32& sample = block.data[static_cast<size_t>(channel) * block.stride + frame];
								const f32 a = _lines.Read(channel, first);
								const f32 b = _lines.Read(channel, second);
								const f32 c = _lines.Read(channel, third);
								const f32 written = sample + feedback * (a + b + c) * (1.0f / 3.0f);
								_lines.Write(channel, written);
								loudest = Abs(written) > loudest ? Abs(written) : loudest;
								sample = sample * dry + a * tap1 + b * tap2 + c * tap3;
							}
							_lines.Advance();
							_lfo.Turn();
							_silence.Note(loudest);
						}
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index < 8) {
							_parameters[index].Walk(value, _walkFrames);
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override { return _silence.Covers(_lines.GetLength()); }
				private:
					enum : u32 { Dry = 0,
								 Tap1,
								 Tap2,
								 Tap3,
								 Delay,
								 Rate,
								 Depth,
								 Feedback };
					AudioRamp _parameters[8];
					DelayLines _lines;
					Quadrature _lfo;
					SilenceCount _silence;
				};

				BuiltinDefinition<Echo> _echo(EchoParameters);
				BuiltinDefinition<Flange> _flange(FlangeParameters);
				BuiltinDefinition<Chorus> _chorus(ChorusParameters);

			} // anonymous namespace

			IAudioEffectDefinition& GetEcho() { return _echo; }
			IAudioEffectDefinition& GetFlange() { return _flange; }
			IAudioEffectDefinition& GetChorus() { return _chorus; }

		} // namespace Effects

	} // namespace AudioImpl

} // namespace Shalltear
