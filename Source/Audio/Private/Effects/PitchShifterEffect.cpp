#include "Effects/BuiltinEffects.h"
#include "Effects/EffectKit.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace Effects {

			namespace {

				constexpr AudioParameterDefinition PITCH_SHIFTER[] = {
					{"Pitch", "x", 0.5f, 2.0f, 1.0f},
					{"FFT Size", "", 256.0f, 4096.0f, 1024.0f},
					{"Overlap", "", 1.0f, 32.0f, 4.0f},
				};

				constexpr u32 MAX_SIZE = 4096;
				constexpr u32 MAX_BINS = MAX_SIZE / 2 + 1;

				u32 SizeOf(f32 value) [[clang::nonblocking]] {
					u32 size = 256;
					while (size < MAX_SIZE && value >= static_cast<f32>(size) * 1.41421356f) {
						size *= 2;
					}
					return size;
				}

				u32 OverlapOf(f32 value, u32 size) [[clang::nonblocking]] {
					const u32 overlap = static_cast<u32>(Clamp(value, 1.0f, 32.0f) + 0.5f);
					return overlap < size ? overlap : size;
				}

				struct VocoderChannel {
					f32 input[MAX_SIZE];
					f32 output[MAX_SIZE];
					f32 sum[2 * MAX_SIZE];
					f32 analysisPhase[MAX_BINS];
					f32 synthesisPhase[MAX_BINS];
				};

				class PitchShifter final : public BuiltinEffect {
				public:
					explicit PitchShifter(const AudioEffectContext& context) : BuiltinEffect(context) {
						_pitch.Jump(context.parameters[0]);
						_size = SizeOf(context.parameters[1]);
						_overlap = OverlapOf(context.parameters[2], _size);
						_requestedSize = context.parameters[1];
						_requestedOverlap = context.parameters[2];
						const size_t bytes = sizeof(VocoderChannel) * _channels;
						_vocoders = static_cast<VocoderChannel*>(Allocate(bytes, MemTag::Audio, 64));
						for (u32 m = 0; m < MAX_SIZE / 2; ++m) {
							_cosine[m] = std::cos(TAU * static_cast<f32>(m) / static_cast<f32>(MAX_SIZE));
							_sine[m] = std::sin(TAU * static_cast<f32>(m) / static_cast<f32>(MAX_SIZE));
						}
						Restart();
					}

					~PitchShifter() override { Free(_vocoders); }

					void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
						(void)sidechain;
						const u32 size = SizeOf(_requestedSize);
						const u32 overlap = OverlapOf(_requestedOverlap, size);
						if (size != _size || overlap != _overlap) {
							_size = size;
							_overlap = overlap;
							Restart();
						}
						_pitch.Advance(block.frames);
						const u32 channels = block.channels < _channels ? block.channels : _channels;
						const u32 hop = _size / _overlap;
						const u32 latency = _size - hop;
						f32 loudest = 0.0f;
						u32 cursor = _cursor;
						for (u32 channel = 0; channel < channels; ++channel) {
							f32* row = block.data + static_cast<size_t>(channel) * block.stride;
							VocoderChannel& vocoder = _vocoders[channel];
							const f32 heard = Loudest(row, block.frames);
							loudest = heard > loudest ? heard : loudest;
							cursor = _cursor;
							for (u32 frame = 0; frame < block.frames; ++frame) {
								vocoder.input[cursor] = row[frame];
								row[frame] = vocoder.output[cursor - latency];
								if (++cursor >= _size) {
									cursor = latency;
									Shift(vocoder, hop, _pitch.At(frame));
								}
							}
						}
						_cursor = cursor;
						_silence.NoteBlock(loudest, block.frames);
					}

					void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
						if (index == 0) {
							_pitch.Walk(value, _walkFrames);
						} else if (index == 1) {
							_requestedSize = value;
						} else if (index == 2) {
							_requestedOverlap = value;
						}
					}

					bool IsQuiet() const [[clang::nonblocking]] override { return _silence.Covers(2 * _size); }
				private:
					void Restart() [[clang::nonblocking]] {
						std::memset(_vocoders, 0, sizeof(VocoderChannel) * _channels);
						_cursor = _size - _size / _overlap;
					}

					f32 TurnCosine(u32 m) const [[clang::nonblocking]] { return m < MAX_SIZE / 2 ? _cosine[m] : -_cosine[m - MAX_SIZE / 2]; }

					f32 Window(u32 k) const [[clang::nonblocking]] { return 0.5f - 0.5f * TurnCosine(k * (MAX_SIZE / _size)); }

					void Transform(f32* data, bool inverse) const [[clang::nonblocking]] {
						const u32 n = _size;
						for (u32 i = 1, j = 0; i < n; ++i) {
							u32 bit = n >> 1;
							for (; j & bit; bit >>= 1) {
								j ^= bit;
							}
							j ^= bit;
							if (i < j) {
								const f32 re = data[2 * i];
								const f32 im = data[2 * i + 1];
								data[2 * i] = data[2 * j];
								data[2 * i + 1] = data[2 * j + 1];
								data[2 * j] = re;
								data[2 * j + 1] = im;
							}
						}
						const f32 sign = inverse ? 1.0f : -1.0f;
						for (u32 length = 2; length <= n; length <<= 1) {
							const u32 half = length / 2;
							const u32 stride = MAX_SIZE / length;
							for (u32 start = 0; start < n; start += length) {
								for (u32 k = 0; k < half; ++k) {
									const f32 wr = _cosine[k * stride];
									const f32 wi = sign * _sine[k * stride];
									f32* u = data + 2 * (start + k);
									f32* v = data + 2 * (start + k + half);
									const f32 vr = v[0] * wr - v[1] * wi;
									const f32 vi = v[0] * wi + v[1] * wr;
									v[0] = u[0] - vr;
									v[1] = u[1] - vi;
									u[0] += vr;
									u[1] += vi;
								}
							}
						}
					}

					void Shift(VocoderChannel& vocoder, u32 hop, f32 pitch) [[clang::nonblocking]] {
						const u32 n = _size;
						const u32 half = n / 2;
						const f32 expected = TAU * static_cast<f32>(hop) / static_cast<f32>(n);
						const f32 overlap = static_cast<f32>(_overlap);

						for (u32 k = 0; k < n; ++k) {
							_work[2 * k] = vocoder.input[k] * Window(k);
							_work[2 * k + 1] = 0.0f;
						}
						Transform(_work, false);

						for (u32 k = 0; k <= half; ++k) {
							const f32 re = _work[2 * k];
							const f32 im = _work[2 * k + 1];
							const f32 phase = std::atan2(im, re);
							f32 delta = phase - vocoder.analysisPhase[k] - static_cast<f32>(k) * expected;
							vocoder.analysisPhase[k] = phase;
							delta -= TAU * std::floor(delta / TAU + 0.5f);
							_magnitude[k] = 2.0f * std::sqrt(re * re + im * im);
							_frequency[k] = static_cast<f32>(k) + delta * overlap / TAU;
						}

						for (u32 k = 0; k <= half; ++k) {
							_shiftedMagnitude[k] = 0.0f;
							_shiftedFrequency[k] = 0.0f;
						}
						for (u32 k = 0; k <= half; ++k) {
							const u32 to = static_cast<u32>(static_cast<f32>(k) * pitch);
							if (to <= half) {
								_shiftedMagnitude[to] += _magnitude[k];
								_shiftedFrequency[to] = _frequency[k] * pitch;
							}
						}

						for (u32 k = 0; k <= half; ++k) {
							const f32 deviation = _shiftedFrequency[k] - static_cast<f32>(k);
							vocoder.synthesisPhase[k] += TAU * deviation / overlap + static_cast<f32>(k) * expected;
							vocoder.synthesisPhase[k] -= TAU * std::floor(vocoder.synthesisPhase[k] / TAU);
							_work[2 * k] = _shiftedMagnitude[k] * std::cos(vocoder.synthesisPhase[k]);
							_work[2 * k + 1] = _shiftedMagnitude[k] * std::sin(vocoder.synthesisPhase[k]);
						}
						for (u32 k = half + 1; k < n; ++k) {
							_work[2 * k] = 0.0f;
							_work[2 * k + 1] = 0.0f;
						}
						Transform(_work, true);

						const f32 scale = 8.0f / (3.0f * static_cast<f32>(n) * overlap);
						for (u32 k = 0; k < n; ++k) {
							vocoder.sum[k] += Window(k) * _work[2 * k] * scale;
						}
						std::memcpy(vocoder.output, vocoder.sum, sizeof(f32) * hop);
						std::memmove(vocoder.sum, vocoder.sum + hop, sizeof(f32) * n);
						std::memset(vocoder.sum + n, 0, sizeof(f32) * hop);
						std::memmove(vocoder.input, vocoder.input + hop, sizeof(f32) * (n - hop));
					}

					AudioRamp _pitch;
					f32 _requestedSize = 1024.0f;
					f32 _requestedOverlap = 4.0f;
					u32 _size = 1024;
					u32 _overlap = 4;
					u32 _cursor = 0;
					VocoderChannel* _vocoders = nullptr;
					SilenceCount _silence;
					f32 _work[2 * MAX_SIZE];
					f32 _magnitude[MAX_BINS];
					f32 _frequency[MAX_BINS];
					f32 _shiftedMagnitude[MAX_BINS];
					f32 _shiftedFrequency[MAX_BINS];
					f32 _cosine[MAX_SIZE / 2];
					f32 _sine[MAX_SIZE / 2];
				};

				BuiltinDefinition<PitchShifter> _pitchShifter(PITCH_SHIFTER);

			} // anonymous namespace

			IAudioEffectDefinition& GetPitchShifter() { return _pitchShifter; }

		} // namespace Effects

	} // namespace AudioImpl

} // namespace Shalltear
