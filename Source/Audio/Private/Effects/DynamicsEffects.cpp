#include "Effects/BuiltinEffects.h"
#include "Effects/EffectKit.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace Effects {

			namespace {

				constexpr AudioParameterDefinition COMPRESSOR[] = {
					{"Threshold", "dB", -60.0f, 0.0f, 0.0f},
					{"Attack", "ms", 10.0f, 200.0f, 50.0f},
					{"Release", "ms", 20.0f, 1000.0f, 50.0f},
					{"Make up gain", "dB", 0.0f, 30.0f, 0.0f},
				};

				constexpr AudioParameterDefinition DUCK_VOLUME[] = {
					{"Threshold", "dB", -80.0f, 0.0f, -10.0f},
					{"Ratio", "%", 100.0f, 10000.0f, 200.0f},
					{"Attack Time", "ms", 0.0f, 5000.0f, 100.0f},
					{"Release Time", "ms", 0.0f, 5000.0f, 500.0f},
					{"Make-up Gain", "dB", -50.0f, 50.0f, 0.0f},
					{"Knee", "dB", 0.0f, 50.0f, 10.0f},
					{"Sidechain Mix", "%", 0.0f, 100.0f, 100.0f},
				};

				constexpr AudioParameterDefinition NORMALIZE[] = {
					{"Fade in time", "ms", 0.0f, 20000.0f, 5000.0f},
					{"Lowest volume", "", 0.0f, 1.0f, 0.1f},
					{"Maximum amp", "x", 0.0f, 100000.0f, 20.0f},
				};

				constexpr AudioParameterDefinition DISTORTION[] = {
					{"Distortion", "", 0.0f, 1.0f, 0.5f},
				};

				f32 LoudestOfFrame(const AudioBlock& block, u32 channels, u32 frame) [[clang::nonblocking]] {
					f32 loudest = 0.0f;
					for (u32 channel = 0; channel < channels; ++channel) {
						const f32 magnitude = Abs(block.data[static_cast<size_t>(channel) * block.stride + frame]);
						loudest = magnitude > loudest ? magnitude : loudest;
					}
					return loudest;
				}

				class Compressor final : public BuiltinEffect {
				public:
					explicit Compressor(const AudioEffectContext& context) : BuiltinEffect(context) {
						for (u32 index = 0; index < 4; ++index) {
							_parameters[index].Jump(context.parameters[index]);
						}
					}

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						(void)sidechain;
						for (AudioRamp& parameter : _parameters) {
							parameter.Advance(block.frames);
						}
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						const f32 attack = OnePole(_parameters[ATTACK].end * 0.001f, _rate);
						const f32 release = OnePole(_parameters[RELEASE].end * 0.001f, _rate);
						const f32 thresholdStart = DecibelsToGain(_parameters[THRESHOLD].start);
						const f32 thresholdEnd = DecibelsToGain(_parameters[THRESHOLD].end);
						const f32 makeUpStart = DecibelsToGain(_parameters[MAKE_UP].start);
						const f32 makeUpEnd = DecibelsToGain(_parameters[MAKE_UP].end);
						const f32 per = block.frames > 0 ? 1.0f / static_cast<f32>(block.frames) : 0.0f;
						for (u32 frame = 0; frame < block.frames; ++frame) {
							const f32 at = static_cast<f32>(frame + 1) * per;
							const f32 threshold = thresholdStart + (thresholdEnd - thresholdStart) * at;
							const f32 makeUp = makeUpStart + (makeUpEnd - makeUpStart) * at;
							const f32 peak = LoudestOfFrame(block, channels, frame);
							_envelope += (peak - _envelope) * (peak > _envelope ? attack : release);
							const f32 gain = (_envelope > threshold ? threshold / _envelope : 1.0f) * makeUp;
							for (u32 channel = 0; channel < channels; ++channel) {
								block.data[static_cast<size_t>(channel) * block.stride + frame] *= gain;
							}
						}
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index < 4) {
							_parameters[index].Walk(value, _walkFrames);
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override { return _envelope <= DecibelsToGain(_parameters[THRESHOLD].end); }
				private:
					enum : u32 { THRESHOLD = 0,
								 ATTACK,
								 RELEASE,
								 MAKE_UP };
					AudioRamp _parameters[4];
					f32 _envelope = 0.0f;
				};

				class DuckVolume final : public BuiltinEffect {
				public:
					static constexpr u32 CHUNK_FRAMES = 8;
					static constexpr bool READS_SIDECHAIN = true;

					explicit DuckVolume(const AudioEffectContext& context) : BuiltinEffect(context) {
						for (u32 index = 0; index < 7; ++index) {
							_parameters[index].Jump(context.parameters[index]);
						}
					}

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						for (AudioRamp& parameter : _parameters) {
							parameter.Advance(block.frames);
						}
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						const f32 threshold = _parameters[THRESHOLD].end;
						const f32 slope = 1.0f - 100.0f / (_parameters[RATIO].end > 100.0f ? _parameters[RATIO].end : 100.0f);
						const f32 knee = _parameters[KNEE].end;
						const f32 chunkSeconds = static_cast<f32>(CHUNK_FRAMES) / _rate;
						const f32 attack = OnePole(_parameters[ATTACK].end * 0.001f / chunkSeconds, 1.0f);
						const f32 release = OnePole(_parameters[RELEASE].end * 0.001f / chunkSeconds, 1.0f);
						const f32 makeUp = DecibelsToGain(_parameters[MAKE_UP].end);
						const u32 keyed = sidechain ? (sidechain->channels < channels ? sidechain->channels : channels) : 0;

						for (u32 from = 0; from < block.frames; from += CHUNK_FRAMES) {
							const u32 to = from + CHUNK_FRAMES < block.frames ? from + CHUNK_FRAMES : block.frames;
							f32 key = 0.0f;
							for (u32 channel = 0; channel < keyed; ++channel) {
								const f32 loudest = Loudest(sidechain->data + static_cast<size_t>(channel) * sidechain->stride + from, to - from);
								key = loudest > key ? loudest : key;
							}
							const f32 over = GainToDecibels(key) - threshold;
							f32 reduction = 0.0f;
							if (2.0f * over >= knee) {
								reduction = slope * over;
							} else if (knee > 0.0f && 2.0f * over > -knee) {
								const f32 into = over + knee * 0.5f;
								reduction = slope * into * into / (2.0f * knee);
							}
							_reduction += (reduction - _reduction) * (reduction > _reduction ? attack : release);

							const f32 mix = _parameters[MIX].At(to - 1) * 0.01f;
							const f32 target = (1.0f - mix) + mix * DecibelsToGain(-_reduction) * makeUp;
							const f32 step = (target - _gain) / static_cast<f32>(to - from);
							for (u32 frame = from; frame < to; ++frame) {
								_gain += step;
								for (u32 channel = 0; channel < channels; ++channel) {
									block.data[static_cast<size_t>(channel) * block.stride + frame] *= _gain;
								}
							}
							_gain = target;
						}
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index < 7) {
							_parameters[index].Walk(value, _walkFrames);
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override { return _reduction < 1e-3f; }
				private:
					enum : u32 { THRESHOLD = 0,
								 RATIO,
								 ATTACK,
								 RELEASE,
								 MAKE_UP,
								 KNEE,
								 MIX };
					AudioRamp _parameters[7];
					f32 _reduction = 0.0f;
					f32 _gain = 1.0f;
				};

				class Normalize final : public BuiltinEffect {
				public:
					explicit Normalize(const AudioEffectContext& context) : BuiltinEffect(context) {
						for (u32 index = 0; index < 3; ++index) {
							_parameters[index].Jump(context.parameters[index]);
						}
					}

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						(void)sidechain;
						for (AudioRamp& parameter : _parameters) {
							parameter.Advance(block.frames);
						}
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						const f32 fade = _parameters[FADE].end * 0.001f;
						const f32 rise = OnePole(fade * 0.2f, _rate);
						const f32 forget = 1.0f - OnePole(fade, _rate);
						const f32 lowest = _parameters[LOWEST].end;
						const f32 ceiling = _parameters[MAXIMUM].end;
						for (u32 frame = 0; frame < block.frames; ++frame) {
							const f32 peak = LoudestOfFrame(block, channels, frame);
							_peak = peak > _peak * forget ? peak : _peak * forget;
							const f32 floor = _peak > lowest ? _peak : lowest;
							const f32 target = floor > 0.0f ? (1.0f / floor < ceiling ? 1.0f / floor : ceiling) : ceiling;
							const f32 next = _gain + (target - _gain) * rise;
							_gain = target < _gain || next == _gain ? target : next;
							for (u32 channel = 0; channel < channels; ++channel) {
								block.data[static_cast<size_t>(channel) * block.stride + frame] *= _gain;
							}
						}
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index < 3) {
							_parameters[index].Walk(value, _walkFrames);
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override {
						const f32 lowest = _parameters[LOWEST].end;
						const f32 ceiling = _parameters[MAXIMUM].end;
						const f32 rest = lowest > 0.0f ? (1.0f / lowest < ceiling ? 1.0f / lowest : ceiling) : ceiling;
						return _peak <= (lowest > QUIET_LEVEL ? lowest : QUIET_LEVEL) && Abs(_gain - rest) <= rest * 1e-3f;
					}
				private:
					enum : u32 { FADE = 0,
								 LOWEST,
								 MAXIMUM };
					AudioRamp _parameters[3];
					f32 _peak = 0.0f;
					f32 _gain = 1.0f;
				};

				class Distortion final : public BuiltinEffect {
				public:
					explicit Distortion(const AudioEffectContext& context) : BuiltinEffect(context) { _amount.Jump(context.parameters[0]); }

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						(void)sidechain;
						_amount.Advance(block.frames);
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						for (u32 frame = 0; frame < block.frames; ++frame) {
							const f32 amount = Clamp(_amount.At(frame), 0.0f, 0.999f);
							const f32 k = 2.0f * amount / (1.0f - amount);
							for (u32 channel = 0; channel < channels; ++channel) {
								f32& sample = block.data[static_cast<size_t>(channel) * block.stride + frame];
								sample = (1.0f + k) * sample / (1.0f + k * Abs(sample));
							}
						}
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index == 0) {
							_amount.Walk(value, _walkFrames);
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override { return true; }
				private:
					AudioRamp _amount;
				};

				BuiltinDefinition<Compressor> _compressor(COMPRESSOR);
				BuiltinDefinition<DuckVolume> _duckVolume(DUCK_VOLUME);
				BuiltinDefinition<Normalize> _normalize(NORMALIZE);
				BuiltinDefinition<Distortion> _distortion(DISTORTION);

			} // anonymous namespace

			IAudioEffectDefinition& GetCompressor() { return _compressor; }
			IAudioEffectDefinition& GetDuckVolume() { return _duckVolume; }
			IAudioEffectDefinition& GetNormalize() { return _normalize; }
			IAudioEffectDefinition& GetDistortion() { return _distortion; }

		} // namespace Effects

	} // namespace AudioImpl

} // namespace Shalltear
