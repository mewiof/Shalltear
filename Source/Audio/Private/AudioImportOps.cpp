#include "AudioImportOps.h"

#include "AudioClipAccess.h"
#include "AudioClipData.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include "ThirdParty/qoa.h"

#include <opus_multistream.h>

#include <cmath>
#include <cstring>
#include <span>
#include <utility>
#include <string_view>

extern "C" void* SltAudioQoaAllocate(size_t size) {
	try {
		return ::Shalltear::Allocate(size, ::Shalltear::MemTag::Audio);
	} catch (...) {
		return nullptr;
	}
}

extern "C" void SltAudioQoaFree(void* block) {
	if (block) {
		::Shalltear::Free(block);
	}
}

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			using P = SpeakerPosition;

			constexpr std::string_view LOAD_TYPE_NAMES[] = {"DecompressOnLoad", "CompressedInMemory", "Streaming"};
			constexpr std::string_view FORMAT_NAMES[] = {"PCM", "QOA", "Opus"};
			constexpr std::string_view RATE_SETTING_NAMES[] = {"PreserveSampleRate", "OptimizeSampleRate", "OverrideSampleRate"};

			constexpr u32 MIN_OVERRIDE_RATE = 8000;
			constexpr u32 MAX_OVERRIDE_RATE = 192000;
			constexpr u32 STANDARD_RATES[] = {8000, 11025, 22050, 44100, 48000, 96000, 192000};

			template <typename E, size_t N>
			void ReadEnum(const JSONValue& settings, std::string_view key, const std::string_view (&names)[N], E& out) {
				const JSONValue& value = settings[key];
				if (!value.IsString()) {
					return;
				}
				const std::string_view name = value.AsString();
				for (size_t index = 0; index < N; ++index) {
					if (names[index] == name) {
						out = static_cast<E>(index);
						return;
					}
				}
				SLT_LOG_WARN("Audio", "the clip's import setting {} = '{}' isn't one this importer knows (using {})", key, name,
							 names[static_cast<size_t>(out)]);
			}

			f64 BesselI0(f64 x) {
				const f64 half = x * 0.5;
				f64 sum = 1.0;
				f64 term = 1.0;
				for (u32 k = 1; k < 64; ++k) {
					const f64 factor = half / static_cast<f64>(k);
					term *= factor * factor;
					sum += term;
					if (term < sum * 1e-17) {
						break;
					}
				}
				return sum;
			}

			constexpr f64 PI = 3.14159265358979323846;

			constexpr f64 RESAMPLE_ZERO_CROSSINGS = 16.0;
			constexpr f64 KAISER_BETA = 8.0;
			constexpr f64 RESAMPLE_TABLE_STEPS = 512.0;
			constexpr f64 RESAMPLE_PASSBAND = 0.95;

			constexpr u32 SPECTRUM_FRAMES = 4096;
			constexpr f64 CONTENT_FLOOR = 1e-6;

			void Fft(f64* re, f64* im, u32 size) {
				for (u32 i = 1, j = 0; i < size; ++i) {
					u32 bit = size >> 1;
					for (; j & bit; bit >>= 1) {
						j ^= bit;
					}
					j ^= bit;
					if (i < j) {
						std::swap(re[i], re[j]);
						std::swap(im[i], im[j]);
					}
				}
				for (u32 length = 2; length <= size; length <<= 1) {
					const f64 angle = -2.0 * PI / static_cast<f64>(length);
					const f64 stepRe = std::cos(angle);
					const f64 stepIm = std::sin(angle);
					for (u32 start = 0; start < size; start += length) {
						f64 turnRe = 1.0;
						f64 turnIm = 0.0;
						for (u32 k = 0; k < length / 2; ++k) {
							const u32 a = start + k;
							const u32 b = a + length / 2;
							const f64 bRe = re[b] * turnRe - im[b] * turnIm;
							const f64 bIm = re[b] * turnIm + im[b] * turnRe;
							re[b] = re[a] - bRe;
							im[b] = im[a] - bIm;
							re[a] += bRe;
							im[a] += bIm;
							const f64 nextRe = turnRe * stepRe - turnIm * stepIm;
							turnIm = turnRe * stepIm + turnIm * stepRe;
							turnRe = nextRe;
						}
					}
				}
			}

			// per channel
			constexpr f64 OPUS_KBPS_AT_ZERO = 6.0;
			constexpr f64 OPUS_KBPS_AT_ONE = 128.0;

			AudioClipData* EncodeOpus(const DecodedAudio& audio, f32 quality, str& error) {
				const u64 frames = audio.GetFrames();
				const u32 channels = audio.layout.count;
				if (audio.sampleRate != OPUS_RATE) {
					error = "Opus only codes 48 kHz (samples aren't at 48 kHz)";
					return nullptr;
				}
				if (channels > MAX_AUDIO_CHANNELS) {
					error = "a clip holds at most 8 channels";
					return nullptr;
				}
				const OpusStreamLayout layout = MakeOpusStreamLayout(audio.layout);
				const opus_int32 encoderBytes = opus_multistream_encoder_get_size(layout.streams, layout.coupled);
				if (encoderBytes <= 0) {
					error = "libopus has no encoder for the channels' streams";
					return nullptr;
				}
				Buffer encoderMemory(static_cast<size_t>(encoderBytes), MemTag::Audio);
				OpusMSEncoder* encoder = reinterpret_cast<OpusMSEncoder*>(encoderMemory.data);
				if (opus_multistream_encoder_init(encoder, OPUS_RATE, static_cast<int>(channels), layout.streams, layout.coupled, layout.mapping,
												  OPUS_APPLICATION_AUDIO) != OPUS_OK ||
					opus_multistream_encoder_ctl(encoder, OPUS_SET_BITRATE(static_cast<opus_int32>(OpusBitrate(quality, channels)))) != OPUS_OK) {
					error = "libopus rejected the encoder's settings";
					return nullptr;
				}
				opus_int32 lookahead = 0;
				opus_multistream_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&lookahead));
				if (lookahead < 0 || lookahead >= static_cast<opus_int32>(OPUS_PACKET_FRAMES)) {
					error = "libopus reported a lookahead longer than a packet";
					return nullptr;
				}
				const u64 lead = OPUS_PACKET_FRAMES - static_cast<u64>(lookahead);

				const u64 packets = OpusPacketCount(frames);
				const u32 largest = OpusMaxPacketBytes(layout.streams);
				Vector<u8> body{GetMemoryResource(MemTag::Audio)};
				Vector<u16> sizes{GetMemoryResource(MemTag::Audio)};
				sizes.reserve(static_cast<size_t>(packets));
				Vector<f32> input{GetMemoryResource(MemTag::Audio)};
				input.resize(static_cast<size_t>(OPUS_PACKET_FRAMES) * channels);
				Vector<u8> packet{GetMemoryResource(MemTag::Audio)};
				packet.resize(largest);
				for (u64 index = 0; index < packets; ++index) {
					for (u64 frame = 0; frame < OPUS_PACKET_FRAMES; ++frame) {
						const u64 at = index * OPUS_PACKET_FRAMES + frame;
						f32* into = input.data() + frame * channels;
						if (at >= lead && at - lead < frames) {
							std::memcpy(into, audio.samples.data() + (at - lead) * channels, channels * sizeof(f32));
						} else {
							std::memset(into, 0, channels * sizeof(f32));
						}
					}
					const opus_int32 written = opus_multistream_encode_float(encoder, input.data(), static_cast<int>(OPUS_PACKET_FRAMES), packet.data(),
																			 static_cast<opus_int32>(largest));
					if (written <= 0) {
						error = "the Opus encoder failed on a packet";
						return nullptr;
					}
					sizes.push_back(static_cast<u16>(written));
					body.insert(body.end(), packet.begin(), packet.begin() + written);
				}

				Vector<u8> payload{GetMemoryResource(MemTag::Audio)};
				payload.resize(OPUS_HEADER_BYTES + static_cast<size_t>(packets) * 2 + body.size(), 0);
				payload[0] = layout.streams;
				payload[1] = layout.coupled;
				for (u32 channel = 0; channel < channels; ++channel) {
					payload[2 + channel] = layout.mapping[channel];
				}
				for (u64 index = 0; index < packets; ++index) {
					payload[OPUS_HEADER_BYTES + index * 2] = static_cast<u8>(sizes[index] & 0xFFu);
					payload[OPUS_HEADER_BYTES + index * 2 + 1] = static_cast<u8>(sizes[index] >> 8);
				}
				std::memcpy(payload.data() + OPUS_HEADER_BYTES + packets * 2, body.data(), body.size());
				if (payload.size() > 0xFFFFFFFFull) {
					error = "the clip's Opus packets come to more than 4 GiB. Import it in parts";
					return nullptr;
				}
				AudioClipData* data = CreateOpusClipData(payload.data(), payload.size(), audio.layout, frames);
				if (!data) {
					error = "the Opus encoder produced packets its idx wouldn't read";
				}
				return data;
			}

			i16 ToS16(f32 sample) {
				const f64 scaled = std::floor(static_cast<f64>(sample) * 32768.0 + 0.5);
				return static_cast<i16>(scaled < -32768.0 ? -32768.0 : (scaled > 32767.0 ? 32767.0 : scaled));
			}

		} // anonymous namespace

		AudioClipLoadType DefaultLoadType(f64 seconds) {
			constexpr f64 DECOMPRESS_BELOW_SECONDS = 10.0;
			constexpr f64 STREAM_ABOVE_SECONDS = 60.0;
			if (seconds < DECOMPRESS_BELOW_SECONDS) {
				return AudioClipLoadType::DecompressOnLoad;
			}
			return seconds > STREAM_ABOVE_SECONDS ? AudioClipLoadType::Streaming : AudioClipLoadType::CompressedInMemory;
		}

		u32 OpusBitrate(f32 quality, u32 channels) {
			const f64 q = quality < 0.0f ? 0.0 : (quality > 1.0f ? 1.0 : static_cast<f64>(quality));
			const f64 kbps = OPUS_KBPS_AT_ZERO * std::pow(OPUS_KBPS_AT_ONE / OPUS_KBPS_AT_ZERO, q);
			return static_cast<u32>(std::lround(kbps * 1000.0)) * channels;
		}

		void ReadImportSettings(const JSONValue& settings, AudioImportSettings& out) {
			if (!settings.IsObject()) {
				return;
			}
			out.forceToMono = settings["forceToMono"].AsBool(out.forceToMono);
			out.normalize = settings["normalize"].AsBool(out.normalize);
			ReadEnum(settings, "loadType", LOAD_TYPE_NAMES, out.loadType);
			ReadEnum(settings, "compressionFormat", FORMAT_NAMES, out.compressionFormat);
			ReadEnum(settings, "sampleRateSetting", RATE_SETTING_NAMES, out.sampleRateSetting);
			const JSONValue& quality = settings["quality"];
			if (quality.IsNumber()) {
				const f64 asked = quality.AsNumber();
				if (asked >= 0.0 && asked <= 1.0) {
					out.quality = static_cast<f32>(asked);
				} else {
					SLT_LOG_WARN("Audio", "the clip's import setting quality = {} is outside 0-1 (using {})", asked, out.quality);
				}
			}
			const JSONValue& rate = settings["sampleRateOverride"];
			if (rate.IsNumber()) {
				const i64 asked = rate.AsInt();
				if (asked >= MIN_OVERRIDE_RATE && asked <= MAX_OVERRIDE_RATE) {
					out.sampleRateOverride = static_cast<u32>(asked);
				} else {
					SLT_LOG_WARN("Audio", "the clip's import setting sampleRateOverride = {} is outside {}-{} Hz (using {} Hz)", asked,
								 MIN_OVERRIDE_RATE, MAX_OVERRIDE_RATE, out.sampleRateOverride);
				}
			}
		}

		void WriteImportSettings(const AudioImportSettings& settings, JSONValue& out) {
			out.AddMember("forceToMono", JSONValue::MakeBool(settings.forceToMono));
			out.AddMember("normalize", JSONValue::MakeBool(settings.normalize));
			out.AddMember("loadType", JSONValue::MakeString(LOAD_TYPE_NAMES[static_cast<size_t>(settings.loadType)]));
			out.AddMember("compressionFormat", JSONValue::MakeString(FORMAT_NAMES[static_cast<size_t>(settings.compressionFormat)]));
			out.AddMember("sampleRateSetting", JSONValue::MakeString(RATE_SETTING_NAMES[static_cast<size_t>(settings.sampleRateSetting)]));
			out.AddMember("sampleRateOverride", JSONValue::MakeNumber(static_cast<f64>(settings.sampleRateOverride)));
			out.AddMember("quality", JSONValue::MakeNumber(static_cast<f64>(settings.quality)));
		}

		void MergeImportSettings(const JSONValue& previous, const AudioImportSettings& settings, JSONValue& out) {
			WriteImportSettings(settings, out);
			if (!previous.IsObject()) {
				return;
			}
			for (size_t index = 0; index < previous.MemberCount(); ++index) {
				if (!out.Find(previous.KeyAt(index))) {
					out.AddMember(previous.KeyAt(index), previous.ChildAt(index));
				}
			}
		}

		void DownmixToMono(DecodedAudio& audio) {
			const u32 channels = audio.layout.count;
			if (channels <= 1) {
				return;
			}
			const u64 frames = audio.GetFrames();
			const f32 scale = 1.0f / static_cast<f32>(channels);
			for (u64 frame = 0; frame < frames; ++frame) {
				const f32* in = audio.samples.data() + frame * channels;
				f32 sum = 0.0f;
				for (u32 channel = 0; channel < channels; ++channel) {
					sum += in[channel];
				}
				audio.samples[frame] = sum * scale;
			}
			audio.samples.resize(static_cast<size_t>(frames));
			audio.layout = MakeStandardLayout(1);
		}

		void NormalizePeak(DecodedAudio& audio) {
			f32 peak = 0.0f;
			for (f32 sample : audio.samples) {
				const f32 magnitude = sample < 0.0f ? -sample : sample;
				peak = magnitude > peak ? magnitude : peak;
			}
			if (peak <= 0.0f) {
				return;
			}
			const f32 scale = 1.0f / peak;
			for (f32& sample : audio.samples) {
				sample *= scale;
			}
		}

		bool Resample(DecodedAudio& audio, u32 targetRate) {
			const u32 sourceRate = audio.sampleRate;
			if (targetRate == 0 || sourceRate == 0) {
				return false;
			}
			if (targetRate == sourceRate) {
				return true;
			}
			const u32 channels = audio.layout.count;
			const u64 frames = audio.GetFrames();
			const u64 outFrames = (frames * targetRate + sourceRate - 1) / sourceRate;
			const f64 step = static_cast<f64>(sourceRate) / static_cast<f64>(targetRate);

			const f64 cutoff = (targetRate < sourceRate ? static_cast<f64>(targetRate) / static_cast<f64>(sourceRate) : 1.0) * RESAMPLE_PASSBAND;
			const f64 halfWidth = RESAMPLE_ZERO_CROSSINGS / cutoff;

			const u64 tableLength = static_cast<u64>(std::ceil(halfWidth * RESAMPLE_TABLE_STEPS)) + 2;
			Vector<f64> kernel{GetMemoryResource(MemTag::Audio)};
			kernel.resize(static_cast<size_t>(tableLength), 0.0);
			const f64 windowNorm = 1.0 / BesselI0(KAISER_BETA);
			for (u64 entry = 0; entry < tableLength; ++entry) {
				const f64 x = static_cast<f64>(entry) / RESAMPLE_TABLE_STEPS;
				const f64 relative = x / halfWidth;
				if (relative >= 1.0) {
					break;
				}
				const f64 argument = PI * cutoff * x;
				const f64 sinc = entry == 0 ? 1.0 : std::sin(argument) / argument;
				kernel[entry] = cutoff * sinc * BesselI0(KAISER_BETA * std::sqrt(1.0 - relative * relative)) * windowNorm;
			}

			Vector<f32> out{GetMemoryResource(MemTag::Audio)};
			out.resize(static_cast<size_t>(outFrames * channels), 0.0f);
			Vector<f64> accumulator{GetMemoryResource(MemTag::Audio)};
			accumulator.resize(channels, 0.0);

			for (u64 index = 0; index < outFrames; ++index) {
				const f64 centre = static_cast<f64>(index) * step;
				const i64 first = static_cast<i64>(std::ceil(centre - halfWidth));
				const i64 last = static_cast<i64>(std::floor(centre + halfWidth));
				for (u32 channel = 0; channel < channels; ++channel) {
					accumulator[channel] = 0.0;
				}
				for (i64 tap = first; tap <= last; ++tap) {
					if (tap < 0 || tap >= static_cast<i64>(frames)) {
						continue;
					}
					const f64 distance = std::fabs(centre - static_cast<f64>(tap)) * RESAMPLE_TABLE_STEPS;
					const u64 below = static_cast<u64>(distance);
					if (below + 1 >= tableLength) {
						continue;
					}
					const f64 fraction = distance - static_cast<f64>(below);
					const f64 weight = kernel[below] + (kernel[below + 1] - kernel[below]) * fraction;
					const f32* in = audio.samples.data() + static_cast<u64>(tap) * channels;
					for (u32 channel = 0; channel < channels; ++channel) {
						accumulator[channel] += weight * static_cast<f64>(in[channel]);
					}
				}
				f32* written = out.data() + index * channels;
				for (u32 channel = 0; channel < channels; ++channel) {
					written[channel] = static_cast<f32>(accumulator[channel]);
				}
			}
			audio.samples = std::move(out);
			audio.sampleRate = targetRate;
			return true;
		}

		AudioClipData* Encode(const DecodedAudio& audio, AudioCompressionFormat format, f32 quality, str& error) {
			const u64 frames = audio.GetFrames();
			const u32 channels = audio.layout.count;
			if (frames == 0 || channels == 0) {
				error = "the file holds no audio";
				return nullptr;
			}
			if (format == AudioCompressionFormat::PCM) {
				AudioClipData* data = CreatePcmClipData(audio.samples.data(), frames, audio.layout, audio.sampleRate);
				if (!data) {
					error = "the samples aren't a shape a clip can hold";
				}
				return data;
			}
			if (format == AudioCompressionFormat::Opus) {
				return EncodeOpus(audio, quality, error);
			}

			if (channels > QOA_MAX_CHANNELS) {
				error = "QOA holds at most 8 channels";
				return nullptr;
			}
			if (audio.sampleRate > 0xffffff) {
				error = "QOA holds a rate below 16,777,216 Hz";
				return nullptr;
			}
			if (frames > 0xffffffffull) {
				error = "QOA holds fewer than 2^32 samples a channel. Import it as PCM";
				return nullptr;
			}

			Vector<i16> shorts{GetMemoryResource(MemTag::Audio)};
			shorts.resize(audio.samples.size());
			for (size_t index = 0; index < audio.samples.size(); ++index) {
				shorts[index] = ToS16(audio.samples[index]);
			}
			qoa_desc description;
			std::memset(&description, 0, sizeof(description));
			description.channels = channels;
			description.samplerate = audio.sampleRate;
			description.samples = static_cast<unsigned int>(frames);
			unsigned int size = 0;
			void* bytes = qoa_encode(shorts.data(), &description, &size);
			if (!bytes) {
				error = "the QOA encoder failed on the samples";
				return nullptr;
			}
			AudioClipData* data = CreateQoaClipData(static_cast<const u8*>(bytes), size, audio.layout);
			SltAudioQoaFree(bytes);
			if (!data) {
				error = "the QOA encoder produced a stream its decoder wouldn't index";
			}
			return data;
		}

		bool BuildClip(DecodedAudio& audio, const AudioImportSettings& settings, AudioClip& clip, str& error) {
			AudioClipAccess::Clear(clip);
			if (audio.GetFrames() == 0 || audio.layout.count == 0 || audio.sampleRate == 0) {
				error = "the file holds no audio";
				return false;
			}
			if (settings.forceToMono) {
				DownmixToMono(audio);
				if (settings.normalize) {
					NormalizePeak(audio);
				}
			}
			if (settings.sampleRateSetting == AudioSampleRateSetting::OverrideSampleRate && settings.sampleRateOverride != audio.sampleRate) {
				if (!Resample(audio, settings.sampleRateOverride)) {
					error = "the resample to the override rate failed";
					return false;
				}
			}
			if (settings.sampleRateSetting == AudioSampleRateSetting::OptimizeSampleRate) {
				const u32 rate = OptimalSampleRate(audio);
				if (rate != audio.sampleRate && !Resample(audio, rate)) {
					error = "the resample to the optimized rate failed";
					return false;
				}
			}
			if (settings.compressionFormat == AudioCompressionFormat::Opus && audio.sampleRate != OPUS_RATE) {
				if (!Resample(audio, OPUS_RATE)) {
					error = "the resample to Opus's 48 kHz failed";
					return false;
				}
			}
			AudioClipData* cooked = Encode(audio, settings.compressionFormat, settings.quality, error);
			if (!cooked) {
				return false;
			}
			if (!AudioClipAccess::Adopt(clip, cooked, settings.loadType)) {
				error = "the cooked samples couldn't be decoded for playback";
				return false;
			}
			return true;
		}

		u32 OptimalSampleRate(const DecodedAudio& audio) {
			const u32 channels = audio.layout.count;
			const u64 frames = audio.GetFrames();
			if (channels == 0 || frames == 0 || audio.sampleRate == 0) {
				return audio.sampleRate;
			}
			Vector<f64> re{GetMemoryResource(MemTag::Audio)};
			Vector<f64> im{GetMemoryResource(MemTag::Audio)};
			Vector<f64> power{GetMemoryResource(MemTag::Audio)};
			re.resize(SPECTRUM_FRAMES);
			im.resize(SPECTRUM_FRAMES);
			power.assign(SPECTRUM_FRAMES / 2 + 1, 0.0);
			for (u64 start = 0; start < frames; start += SPECTRUM_FRAMES) {
				const u64 count = frames - start < SPECTRUM_FRAMES ? frames - start : SPECTRUM_FRAMES;
				const f64 span = count > 1 ? static_cast<f64>(count - 1) : 1.0;
				for (u32 channel = 0; channel < channels; ++channel) {
					for (u32 index = 0; index < SPECTRUM_FRAMES; ++index) {
						const f64 window = index < count ? 0.5 - 0.5 * std::cos(2.0 * PI * static_cast<f64>(index) / span) : 0.0;
						const f32 sample = index < count ? audio.samples[static_cast<size_t>((start + index) * channels + channel)] : 0.0f;
						re[index] = static_cast<f64>(sample) * window;
						im[index] = 0.0;
					}
					Fft(re.data(), im.data(), SPECTRUM_FRAMES);
					for (u32 bin = 0; bin < power.size(); ++bin) {
						power[bin] += re[bin] * re[bin] + im[bin] * im[bin];
					}
				}
			}
			f64 loudest = 0.0;
			for (const f64 value : power) {
				loudest = value > loudest ? value : loudest;
			}
			if (loudest <= 0.0) {
				return audio.sampleRate;
			}
			u32 highest = 0;
			for (u32 bin = 0; bin < power.size(); ++bin) {
				if (power[bin] >= loudest * CONTENT_FLOOR) {
					highest = bin;
				}
			}
			const f64 content = static_cast<f64>(highest + 1) * static_cast<f64>(audio.sampleRate) / static_cast<f64>(SPECTRUM_FRAMES);
			for (const u32 rate : STANDARD_RATES) {
				if (rate >= audio.sampleRate) {
					break;
				}
				if (static_cast<f64>(rate) * 0.5 * RESAMPLE_PASSBAND >= content) {
					return rate;
				}
			}
			return audio.sampleRate;
		}

		std::span<const u32> GetStandardSampleRates() {
			return STANDARD_RATES;
		}

		bool ReadWaveChannelLayout(const u8* bytes, size_t size, u32 channels, ChannelLayout& out) {
			if (!bytes || size < 12 || channels == 0 || channels > MAX_AUDIO_CHANNELS || std::memcmp(bytes, "RIFF", 4) != 0 ||
				std::memcmp(bytes + 8, "WAVE", 4) != 0) {
				return false;
			}
			const auto u16At = [bytes](size_t at) { return static_cast<u32>(bytes[at]) | (static_cast<u32>(bytes[at + 1]) << 8); };
			const auto u32At = [bytes](size_t at) {
				return static_cast<u32>(bytes[at]) | (static_cast<u32>(bytes[at + 1]) << 8) | (static_cast<u32>(bytes[at + 2]) << 16) |
					   (static_cast<u32>(bytes[at + 3]) << 24);
			};

			static constexpr P MASK_POSITIONS[] = {
				P::FrontLeft,
				P::FrontRight,
				P::FrontCenter,
				P::LowFrequency,
				P::BackLeft,
				P::BackRight,
				P::None,
				P::None,
				P::BackCenter,
				P::SideLeft,
				P::SideRight,
			};

			size_t offset = 12;
			while (offset + 8 <= size) {
				const u32 chunkSize = u32At(offset + 4);
				if (std::memcmp(bytes + offset, "fmt ", 4) == 0) {
					// WAVEFORMATEXTENSIBLE = 40 bytes, tag 0xFFFE, 22 extension bytes
					const size_t format = offset + 8;
					if (chunkSize < 40 || format + 24 > size || u16At(format) != 0xFFFE || u16At(format + 16) < 22) {
						return false;
					}
					const u32 mask = u32At(format + 20);
					if (mask == 0) {
						return false;
					}
					ChannelLayout layout;
					layout.count = channels;
					u32 channel = 0;
					for (u32 bit = 0; bit < 32 && channel < channels; ++bit) {
						if (mask & (1u << bit)) {
							layout.positions[channel++] = bit < sizeof(MASK_POSITIONS) / sizeof(MASK_POSITIONS[0]) ? MASK_POSITIONS[bit] : P::None;
						}
					}
					out = layout;
					return true;
				}
				offset += 8 + static_cast<size_t>(chunkSize) + (chunkSize & 1u);
			}
			return false;
		}

		ChannelLayout MakeVorbisLayout(u32 channels) {
			static constexpr P ORDERS[MAX_AUDIO_CHANNELS][MAX_AUDIO_CHANNELS] = {
				{P::Mono},
				{P::FrontLeft, P::FrontRight},
				{P::FrontLeft, P::FrontCenter, P::FrontRight},
				{P::FrontLeft, P::FrontRight, P::BackLeft, P::BackRight},
				{P::FrontLeft, P::FrontCenter, P::FrontRight, P::BackLeft, P::BackRight},
				{P::FrontLeft, P::FrontCenter, P::FrontRight, P::BackLeft, P::BackRight, P::LowFrequency},
				{P::FrontLeft, P::FrontCenter, P::FrontRight, P::SideLeft, P::SideRight, P::BackCenter, P::LowFrequency},
				{P::FrontLeft, P::FrontCenter, P::FrontRight, P::SideLeft, P::SideRight, P::BackLeft, P::BackRight, P::LowFrequency},
			};
			ChannelLayout layout;
			if (channels == 0 || channels > MAX_AUDIO_CHANNELS) {
				return layout;
			}
			layout.count = channels;
			for (u32 channel = 0; channel < channels; ++channel) {
				layout.positions[channel] = ORDERS[channels - 1][channel];
			}
			return layout;
		}

		ChannelLayout MakeFlacLayout(u32 channels) {
			ChannelLayout layout = MakeStandardLayout(channels);
			if (channels == 4) {
				layout.positions[2] = P::BackLeft;
				layout.positions[3] = P::BackRight;
			} else if (channels == 6) {
				layout.positions[4] = P::BackLeft;
				layout.positions[5] = P::BackRight;
			}
			return layout;
		}

	} // namespace AudioImpl

} // namespace Shalltear
