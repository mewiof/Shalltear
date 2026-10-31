#include "AudioVoice.h"

#include "AudioStream.h"

#include "Shalltear/Audio/AudioEffect.h"
#include "Shalltear/Audio/AudioGenerator.h"

#include <cstring>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			f32 Clamp(f32 value, f32 low, f32 high) [[clang::nonblocking]] {
				return value >= low ? (value <= high ? value : high) : low;
			}

			i64 FloorToInt(f64 value) [[clang::nonblocking]] {
				i64 index = static_cast<i64>(value);
				if (static_cast<f64>(index) > value) {
					--index;
				}
				return index;
			}

			void FillGeneratorWindow(AudioGeneratorStream& stream, u32 channels, i64 index) [[clang::nonblocking]] {
				const i64 windowEnd = stream.start + static_cast<i64>(stream.count);
				if (stream.count == 0 || index >= windowEnd + static_cast<i64>(GENERATOR_WINDOW_FRAMES / 2)) {
					stream.start = index > static_cast<i64>(GENERATOR_HISTORY_FRAMES) ? index - static_cast<i64>(GENERATOR_HISTORY_FRAMES) : 0;
					stream.count = 0;
				} else {
					const u32 keep = stream.count < GENERATOR_HISTORY_FRAMES ? stream.count : GENERATOR_HISTORY_FRAMES;
					for (u32 channel = 0; channel < channels; ++channel) {
						f32* row = stream.window + static_cast<size_t>(channel) * GENERATOR_WINDOW_FRAMES;
						std::memmove(row, row + (stream.count - keep), keep * sizeof(f32));
					}
					stream.start = windowEnd - static_cast<i64>(keep);
					stream.count = keep;
				}
				const u64 from = static_cast<u64>(stream.start) + stream.count;
				if (!stream.instance || from >= stream.end) {
					return;
				}
				const u32 room = GENERATOR_WINDOW_FRAMES - stream.count;
				AudioBlock block{stream.window + stream.count, channels, room, GENERATOR_WINDOW_FRAMES};
				u32 written = stream.instance->Process(block, from);
				written = written < room ? written : room;
				if (written < room) {
					stream.end = from + written;
				}
				stream.count += written;
			}

			[[gnu::noinline]] void ReadGeneratorFrame(const AudioClipData& clip, i64 index, f32* out) [[clang::nonblocking]] {
				AudioGeneratorStream& stream = *clip.stream;
				const u32 channels = clip.channels;
				if (index >= 0 && static_cast<u64>(index) < stream.end) {
					if (stream.count == 0 || index >= stream.start + static_cast<i64>(stream.count)) {
						FillGeneratorWindow(stream, channels, index);
					}
					if (index >= stream.start && index < stream.start + static_cast<i64>(stream.count)) {
						const size_t offset = static_cast<size_t>(index - stream.start);
						for (u32 channel = 0; channel < channels; ++channel) {
							out[channel] = stream.window[static_cast<size_t>(channel) * GENERATOR_WINDOW_FRAMES + offset];
						}
						return;
					}
				}
				for (u32 channel = 0; channel < channels; ++channel) {
					out[channel] = 0.0f;
				}
			}

			void ReadFrame(AudioVoiceState& voice, QoaCursor& cursor, i64 index, f32* out) [[clang::nonblocking]] {
				const AudioClipData& clip = *voice.clip;
				if (clip.encoding == AudioClipEncoding::Generator) {
					ReadGeneratorFrame(clip, index, out);
					return;
				}
				const i64 frames = static_cast<i64>(clip.frames);
				if (voice.loop) {
					index %= frames;
					if (index < 0) {
						index += frames;
					}
				} else if (index < 0 || index >= frames) {
					for (u32 channel = 0; channel < clip.channels; ++channel) {
						out[channel] = 0.0f;
					}
					return;
				}

				if (index < static_cast<i64>(CLIP_HEAD_FRAMES)) {
					const f32* head = clip.head + static_cast<size_t>(index) * clip.channels;
					for (u32 channel = 0; channel < clip.channels; ++channel) {
						out[channel] = head[channel];
					}
					return;
				}
				if (clip.encoding == AudioClipEncoding::Pcm) {
					const f32* frame = clip.pcm + static_cast<size_t>(index) * clip.channels;
					for (u32 channel = 0; channel < clip.channels; ++channel) {
						out[channel] = frame[channel];
					}
					return;
				}
				if (clip.encoding == AudioClipEncoding::Streamed) {
					if (static_cast<u64>(index) < clip.primeFrames) {
						const f32* frame = clip.prime + static_cast<size_t>(index) * clip.channels;
						for (u32 channel = 0; channel < clip.channels; ++channel) {
							out[channel] = frame[channel];
						}
					} else if (voice.stream) {
						ReadStreamFrame(*voice.stream, static_cast<u64>(index), out);
					} else {
						for (u32 channel = 0; channel < clip.channels; ++channel) {
							out[channel] = 0.0f;
						}
					}
					return;
				}
				if (clip.encoding == AudioClipEncoding::Qoa) {
					QoaRead(clip.qoa, cursor, static_cast<u64>(index), out);
					return;
				}
				for (u32 channel = 0; channel < clip.channels; ++channel) {
					out[channel] = 0.0f;
				}
			}

			f64 SumOfRampProduct(f64 a0, f64 a1, f64 b0, f64 b1, f64 c0, f64 c1, f64 n) [[clang::nonblocking]] {
				const f64 s1 = n * (n + 1.0) / 2.0;
				const f64 s2 = n * (n + 1.0) * (2.0 * n + 1.0) / 6.0;
				const f64 s3 = s1 * s1;
				return a0 * b0 * c0 * n + (a1 * b0 * c0 + a0 * b1 * c0 + a0 * b0 * c1) * s1 + (a1 * b1 * c0 + a1 * b0 * c1 + a0 * b1 * c1) * s2 +
					   a1 * b1 * c1 * s3;
			}

			f32 Interpolate(f32 before, f32 at, f32 after, f32 afterNext, f32 t) [[clang::nonblocking]] {
				const f32 a = -0.5f * before + 1.5f * at - 1.5f * after + 0.5f * afterNext;
				const f32 b = before - 2.5f * at + 2.0f * after - 0.5f * afterNext;
				const f32 c = -0.5f * before + 0.5f * after;
				return ((a * t + b) * t + c) * t + at;
			}

			void MixFrame(const f32* gains, const f32* target, bool panMoves, f32 progress, const f32* sample, size_t step, u32 channels, u32 outputs,
						  f32* column, u32 stride, f32 weight) [[clang::nonblocking]] {
				for (u32 out = 0; out < outputs; ++out) {
					f32 mixed = 0.0f;
					const f32* row = gains + out * channels;
					if (panMoves) {
						const f32* toward = target + out * channels;
						for (u32 channel = 0; channel < channels; ++channel) {
							mixed += (row[channel] + (toward[channel] - row[channel]) * progress) * sample[channel * step];
						}
					} else {
						for (u32 channel = 0; channel < channels; ++channel) {
							mixed += row[channel] * sample[channel * step];
						}
					}
					column[out * stride] += mixed * weight;
				}
			}

			void SilenceOutside(const AudioMixScratch& scratch, u32 channels, u32 begin, u32 rendered, u32 frames) [[clang::nonblocking]] {
				for (u32 channel = 0; channel < channels; ++channel) {
					f32* row = scratch.dry + static_cast<size_t>(channel) * scratch.stride;
					for (u32 frame = 0; frame < begin; ++frame) {
						row[frame] = 0.0f;
					}
					for (u32 frame = begin + rendered; frame < frames; ++frame) {
						row[frame] = 0.0f;
					}
				}
			}

			void RunChain(const AudioVoiceChain& chain, AudioBlock& block) [[clang::nonblocking]] {
				for (u32 index = 0; index < chain.count; ++index) {
					if (IAudioEffect* effect = chain.effects[index]) {
						effect->Process(block, nullptr);
					}
				}
			}

		} // anonymous namespace

		void StartVoice(AudioVoiceState& voice, const AudioClipData* clip, u32 generation, u32 deviceRate, u64 startAt, u64 startFrame,
						f32 volume, f32 pitch, f32 stereoPan, bool loop, u32 bus) {
			voice.phase = VoicePhase::Scheduled;
			voice.loop = loop;
			voice.paused = false;
			voice.pausing = false;
			voice.held = false;
			voice.generation = generation;
			voice.bus = bus;
			voice.clip = clip;
			voice.startAt = startAt;
			voice.stopAt = 0;
			voice.position = static_cast<f64>(startFrame < clip->frames ? startFrame : clip->frames);
			voice.rateRatio = deviceRate ? static_cast<f64>(clip->sampleRate) / static_cast<f64>(deviceRate) : 1.0;
			voice.volume = voice.targetVolume = Clamp(volume, 0.0f, MAX_VOICE_GAIN);
			voice.pitch = voice.targetPitch = Clamp(pitch, MIN_PITCH, MAX_PITCH);
			voice.stereoPan = voice.targetStereoPan = Clamp(stereoPan, -1.0f, 1.0f);
			voice.stopFade = 0;
			voice.spatializer = nullptr;
			voice.spatializerState = nullptr;
			voice.spatial = AudioSpatialParams{};
			voice.blend = voice.targetBlend = 0.0f;
			voice.gain3D = voice.targetGain3D = 1.0f;
			voice.doppler = voice.targetDoppler = 1.0f;
			voice.chain = nullptr;
			voice.bypassEffects = false;
			voice.send = 0;
			voice.sendGeneration = 0;
			voice.sendLevel = voice.targetSendLevel = 0.0f;
			voice.stream = nullptr;
		}

		void SeekVoice(AudioVoiceState& voice, u64 frame) {
			if (voice.clip) {
				voice.position = static_cast<f64>(frame < voice.clip->frames ? frame : voice.clip->frames);
				if (AudioGeneratorStream* stream = voice.clip->stream) {
					stream->count = 0;
					stream->end = ~0ull;
				}
			}
		}

		void PlaceVoice(AudioVoiceState& voice, const AudioVoicePlacement& placement, bool start) {
			voice.spatializer = placement.spatializer;
			voice.spatializerState = placement.state;
			voice.spatial = placement.params;
			voice.targetBlend = Clamp(placement.blend, 0.0f, 1.0f);
			voice.targetGain3D = Clamp(placement.gain, 0.0f, 1.0f);
			voice.targetDoppler = Clamp(placement.doppler, 1.0f / MAX_DOPPLER_RATIO, MAX_DOPPLER_RATIO);
			if (start) {
				voice.blend = voice.targetBlend;
				voice.gain3D = voice.targetGain3D;
				voice.doppler = voice.targetDoppler;
			}
		}

		bool MixVoice(AudioVoiceState& voice, AudioVoiceDecoder* decoder, bool fadeOut, const ChannelLayout& output, u64 blockStart, u32 frames,
					  f32* bus, u32 stride, const AudioVoiceClock& clock, const AudioMixScratch& scratch) {
			if (voice.phase == VoicePhase::Free || !voice.clip) {
				return false;
			}

			bool holdingNow = false;
			if (clock.hold && voice.phase != VoicePhase::Stopping) {
				if (voice.phase == VoicePhase::Scheduled || voice.held || voice.paused) {
					voice.startAt += voice.phase == VoicePhase::Scheduled ? frames : 0;
					voice.stopAt += voice.stopAt != 0 ? frames : 0;
					voice.held = voice.phase != VoicePhase::Scheduled;
					return false;
				}
				holdingNow = true;
			} else if (voice.held) {
				voice.held = false;
			}
			if (voice.paused) {
				return false;
			}
			const u64 blockEnd = blockStart + frames;

			u32 begin = 0;
			if (voice.phase == VoicePhase::Scheduled) {
				if (voice.startAt >= blockEnd) {
					return false;
				}
				begin = voice.startAt > blockStart ? static_cast<u32>(voice.startAt - blockStart) : 0;
				voice.phase = VoicePhase::Playing;
			}

			u32 end = frames;
			bool ended = false;
			if (voice.stopAt != 0 && voice.stopAt < blockEnd) {
				end = voice.stopAt > blockStart ? static_cast<u32>(voice.stopAt - blockStart) : 0;
				ended = true;
			}

			const u32 span = end > begin ? end - begin : 0;
			f32 volumeTarget = voice.targetVolume;
			if (voice.phase == VoicePhase::Stopping) {
				if (!ended && decoder && voice.stopFade > span) {
					volumeTarget = voice.volume * (1.0f - static_cast<f32>(span) / static_cast<f32>(voice.stopFade));
					voice.stopFade -= span;
				} else {
					volumeTarget = 0.0f;
					voice.stopFade = 0;
					ended = true;
				}
			} else if (voice.pausing || holdingNow || fadeOut) {
				volumeTarget = 0.0f;
			}
			if (end <= begin) {
				if (!ended) {
					if (voice.pausing) {
						voice.pausing = false;
						voice.paused = true;
						voice.volume = 0.0f;
					}
					if (holdingNow) {
						voice.held = true;
						voice.volume = 0.0f;
					}
				}
				return ended;
			}

			const AudioClipData& clip = *voice.clip;
			const u32 channels = clip.channels;
			const u32 outputs = output.count;

			const f32 volumeStep = (volumeTarget - voice.volume) / static_cast<f32>(span);
			const f32 pitchStep = (voice.targetPitch - voice.pitch) / static_cast<f32>(span);
			const f32 dopplerStep = (voice.targetDoppler - voice.doppler) / static_cast<f32>(span);
			const f32 busRateStep = (clock.rateEnd - clock.rateStart) / static_cast<f32>(frames);
			const bool loop = voice.loop && clip.encoding != AudioClipEncoding::Generator;
			const u64 lastFrame = clip.stream && clip.stream->end < clip.frames ? clip.stream->end : clip.frames;
			const f64 frameCount = static_cast<f64>(lastFrame);

			if (!decoder) {
				if (!loop && (voice.position < 0.0 || voice.position >= frameCount)) {
					ended = true;
				} else {
					const f64 busRate = static_cast<f64>(clock.rateStart) + static_cast<f64>(busRateStep) * static_cast<f64>(begin);
					voice.position += voice.rateRatio * SumOfRampProduct(voice.pitch, pitchStep, voice.doppler, dopplerStep, busRate, busRateStep,
																		 static_cast<f64>(span));
					if (loop && frameCount > 0.0) {
						if (voice.position >= frameCount || voice.position < 0.0) {
							voice.position -= static_cast<f64>(FloorToInt(voice.position / frameCount)) * frameCount;
						}
					} else if (voice.position < 0.0 || voice.position >= frameCount) {
						ended = true;
					}
				}
				voice.volume = volumeTarget;
				voice.pitch = voice.targetPitch;
				voice.doppler = voice.targetDoppler;
				voice.blend = voice.targetBlend;
				voice.gain3D = voice.targetGain3D;
				voice.stereoPan = voice.targetStereoPan;
				if (!ended) {
					if (voice.pausing) {
						voice.pausing = false;
						voice.paused = true;
					}
					if (holdingNow) {
						voice.held = true;
					}
				}
				return ended;
			}

			f32 target[MAX_AUDIO_CHANNELS * MAX_AUDIO_CHANNELS];
			if (!decoder->gainsValid) {
				ComputeChannelGains(clip.layout, output, voice.stereoPan, decoder->gains);
				decoder->gainsValid = true;
			}
			const bool panMoves = voice.targetStereoPan != voice.stereoPan;
			if (panMoves) {
				ComputeChannelGains(clip.layout, output, voice.targetStereoPan, target);
			}

			f32 samples[MAX_AUDIO_CHANNELS];
			f32 before[MAX_AUDIO_CHANNELS];
			f32 after[MAX_AUDIO_CHANNELS];
			f32 afterNext[MAX_AUDIO_CHANNELS];

			const bool spatial = voice.spatializer && voice.spatializerState;

			AudioVoiceChain* const chain = voice.chain && voice.chain->count > 0 && !voice.bypassEffects ? voice.chain : nullptr;
			const bool onOutput = chain && chain->onOutput;
			const bool rows = spatial || (chain && !onOutput);
			f32* const into = onOutput ? scratch.post : bus;
			const u32 intoStride = onOutput ? scratch.stride : stride;
			if (onOutput) {
				for (u32 out = 0; out < outputs; ++out) {
					std::memset(scratch.post + static_cast<size_t>(out) * scratch.stride, 0, sizeof(f32) * frames);
				}
			}

			u32 rendered = 0;
			for (u32 frame = 0; frame < span; ++frame) {
				if (!loop && (voice.position < 0.0 || voice.position >= frameCount)) {
					ended = true;
					break;
				}

				const i64 index = FloorToInt(voice.position);
				const f32 t = static_cast<f32>(voice.position - static_cast<f64>(index));
				if (t == 0.0f) {
					ReadFrame(voice, decoder->qoa, index, samples);
				} else {
					ReadFrame(voice, decoder->qoa, index - 1, before);
					ReadFrame(voice, decoder->qoa, index, samples);
					ReadFrame(voice, decoder->qoa, index + 1, after);
					ReadFrame(voice, decoder->qoa, index + 2, afterNext);
					for (u32 channel = 0; channel < channels; ++channel) {
						samples[channel] = Interpolate(before[channel], samples[channel], after[channel], afterNext[channel], t);
					}
				}
				if (rows) {
					for (u32 channel = 0; channel < channels; ++channel) {
						scratch.dry[static_cast<size_t>(channel) * scratch.stride + begin + frame] = samples[channel];
					}
				} else {
					const f32 progress = static_cast<f32>(frame + 1) / static_cast<f32>(span);
					const f32 volume = voice.volume + volumeStep * static_cast<f32>(frame + 1);
					MixFrame(decoder->gains, target, panMoves, progress, samples, 1, channels, outputs, into + begin + frame, intoStride, volume);
				}
				++rendered;

				const f32 pitch = voice.pitch + pitchStep * static_cast<f32>(frame + 1);
				const f32 doppler = voice.doppler + dopplerStep * static_cast<f32>(frame + 1);
				const f32 busRate = clock.rateStart + busRateStep * static_cast<f32>(begin + frame + 1);
				voice.position += voice.rateRatio * static_cast<f64>(pitch * doppler * busRate);
				if (loop) {
					while (voice.position >= frameCount) {
						voice.position -= frameCount;
					}
					while (voice.position < 0.0) {
						voice.position += frameCount;
					}
				}
			}

			if (chain && !onOutput) {
				SilenceOutside(scratch, channels, begin, rendered, frames);
				AudioBlock block{scratch.dry, channels, frames, scratch.stride};
				RunChain(*chain, block);
			}

			const f32 blendStep = (voice.targetBlend - voice.blend) / static_cast<f32>(span);

			if (rows && (!spatial || voice.blend < 1.0f || voice.targetBlend < 1.0f)) {
				for (u32 frame = 0; frame < rendered; ++frame) {
					const f32 progress = static_cast<f32>(frame + 1) / static_cast<f32>(span);
					const f32 volume = voice.volume + volumeStep * static_cast<f32>(frame + 1);
					const f32 weight = spatial ? volume * (1.0f - (voice.blend + blendStep * static_cast<f32>(frame + 1))) : volume;
					MixFrame(decoder->gains, target, panMoves, progress, scratch.dry + begin + frame, scratch.stride, channels, outputs, into + begin + frame,
							 intoStride, weight);
				}
			}

			if (spatial && (voice.blend > 0.0f || voice.targetBlend > 0.0f)) {
				SilenceOutside(scratch, channels, begin, rendered, frames);
				const AudioBlock in{scratch.dry, channels, frames, scratch.stride};
				AudioBlock out{scratch.wet, outputs, frames, scratch.stride};
				voice.spatializer->Process(voice.spatializerState, voice.spatial, in, out);

				const f32 gainStep = (voice.targetGain3D - voice.gain3D) / static_cast<f32>(span);
				for (u32 frame = 0; frame < rendered; ++frame) {
					const f32 volume = voice.volume + volumeStep * static_cast<f32>(frame + 1);
					const f32 blend = voice.blend + blendStep * static_cast<f32>(frame + 1);
					const f32 gain = voice.gain3D + gainStep * static_cast<f32>(frame + 1);
					const f32 weight = volume * blend * gain;
					for (u32 out = 0; out < outputs; ++out) {
						into[static_cast<size_t>(out) * intoStride + begin + frame] +=
							scratch.wet[static_cast<size_t>(out) * scratch.stride + begin + frame] * weight;
					}
				}
			}

			if (onOutput) {
				AudioBlock block{scratch.post, outputs, frames, scratch.stride};
				RunChain(*chain, block);
				for (u32 out = 0; out < outputs; ++out) {
					const f32* row = scratch.post + static_cast<size_t>(out) * scratch.stride;
					f32* mixed = bus + static_cast<size_t>(out) * stride;
					for (u32 frame = begin; frame < begin + rendered; ++frame) {
						mixed[frame] += row[frame];
					}
				}
			}

			voice.volume = volumeTarget;
			voice.pitch = voice.targetPitch;
			voice.doppler = voice.targetDoppler;
			voice.blend = voice.targetBlend;
			voice.gain3D = voice.targetGain3D;
			if (panMoves) {
				for (u32 index = 0; index < outputs * channels; ++index) {
					decoder->gains[index] = target[index];
				}
				voice.stereoPan = voice.targetStereoPan;
			}
			if (!ended) {
				if (voice.pausing) {
					voice.pausing = false;
					voice.paused = true;
				}
				if (holdingNow) {
					voice.held = true;
				}
			}
			return ended;
		}

	} // namespace AudioImpl

} // namespace Shalltear
