#include "TestsPCH.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

using namespace Shalltear;

namespace {

	constexpr f64 PI = 3.14159265358979323846;

	struct CookedBytes {
		Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
		Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
		PakStringPool strings;
		bool cooked = false;
	};

	void CookInto(const Asset& asset, CookedBytes& out) {
		CookWriter descriptor(out.descriptor, out.strings);
		CookWriter payload(out.payload, out.strings);
		out.cooked = asset.Cook(descriptor, payload);
	}

	bool SameBytes(const CookedBytes& a, const CookedBytes& b) {
		return a.cooked && b.cooked && a.descriptor == b.descriptor && a.payload == b.payload;
	}

	struct Descriptor {
		u8 format = 0;
		u8 loadType = 0;
		u32 rate = 0;
		u8 channels = 0;
		u8 positions[8]{};
		u64 frames = 0;
		u64 bytes = 0;
		bool read = false;
	};

	Descriptor ReadDescriptor(const CookedBytes& cooked) {
		Descriptor out;
		CookReader reader(cooked.descriptor.data(), cooked.descriptor.size());
		reader.Read(out.format);
		reader.Read(out.loadType);
		reader.Read(out.rate);
		reader.Read(out.channels);
		for (u32 channel = 0; channel < out.channels && channel < 8; ++channel) {
			reader.Read(out.positions[channel]);
		}
		reader.Read(out.frames);
		reader.Read(out.bytes);
		out.read = !reader.Failed();
		return out;
	}

	enum : u8 {
		MONO = 1,
		FRONT_LEFT = 2,
		FRONT_RIGHT = 3,
		FRONT_CENTER = 4,
		LOW_FREQUENCY = 5,
		BACK_LEFT = 6,
		BACK_RIGHT = 7,
		BACK_CENTER = 8,
		SIDE_LEFT = 9,
		SIDE_RIGHT = 10,
	};

	std::vector<f32> ChannelPeaks(const CookedBytes& cooked, u32 channels) {
		std::vector<f32> peaks(channels, 0.0f);
		const size_t samples = cooked.payload.size() / sizeof(f32);
		for (size_t index = 0; index < samples; ++index) {
			f32 sample = 0.0f;
			std::memcpy(&sample, cooked.payload.data() + index * sizeof(f32), sizeof(f32));
			f32& peak = peaks[index % channels];
			peak = std::fabs(sample) > peak ? std::fabs(sample) : peak;
		}
		return peaks;
	}

	std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

} // namespace

#ifndef SHALLTEAR_SHARED

#include "AudioClipAccess.h"
#include "AudioClipData.h"
#include "AudioImportOps.h"
#include "AudioQoa.h"
#include "AudioWaveform.h"

#include "Shalltear/Audio/AudioClip.h"

using namespace Shalltear::AudioImpl;

namespace {

	DecodedAudio Tone(u32 channels, u32 rate, u64 frames, f64 frequency, f32 amplitude) {
		DecodedAudio audio;
		audio.layout = MakeStandardLayout(channels);
		audio.sampleRate = rate;
		audio.samples.resize(static_cast<size_t>(frames * channels));
		for (u64 frame = 0; frame < frames; ++frame) {
			for (u32 channel = 0; channel < channels; ++channel) {
				const f64 phase = 2.0 * PI * frequency * static_cast<f64>(channel + 1) * static_cast<f64>(frame) / static_cast<f64>(rate);
				audio.samples[frame * channels + channel] = amplitude / static_cast<f32>(channel + 1) * static_cast<f32>(std::sin(phase));
			}
		}
		return audio;
	}

	class AudioClipBuildFixture : public ::testing::Test {
	protected:
		std::vector<AudioClip*> _clips;

		void TearDown() override {
			for (AudioClip* clip : _clips) {
				Delete(clip);
			}
		}

		AudioClip* Build(DecodedAudio audio, const AudioImportSettings& settings) {
			AudioClip* clip = New<AudioClip>(MemTag::Asset);
			_clips.push_back(clip);
			str error;
			if (!BuildClip(audio, settings, *clip, error)) {
				ADD_FAILURE() << "clip didn't build: " << error;
			}
			return clip;
		}

		AudioClip* Fresh() {
			AudioClip* clip = New<AudioClip>(MemTag::Asset);
			_clips.push_back(clip);
			return clip;
		}

		static AudioImportSettings As(AudioCompressionFormat format, AudioClipLoadType loadType) {
			AudioImportSettings settings;
			settings.compressionFormat = format;
			settings.loadType = loadType;
			return settings;
		}
	};

	u32 CountCrossings(const f32* samples, size_t count, u32 stride) {
		u32 crossings = 0;
		for (size_t index = stride; index < count; index += stride) {
			if ((samples[index - stride] < 0.0f) != (samples[index] < 0.0f)) {
				++crossings;
			}
		}
		return crossings;
	}

	f64 Rms(const Vector<f32>& samples, size_t from, size_t to) {
		f64 sum = 0.0;
		for (size_t index = from; index < to; ++index) {
			sum += static_cast<f64>(samples[index]) * static_cast<f64>(samples[index]);
		}
		return std::sqrt(sum / static_cast<f64>(to - from));
	}

} // namespace

TEST_F(AudioClipBuildFixture, PcmClipKeepsSamples) {
	const DecodedAudio source = Tone(2, 44100, 4410, 441.0, 0.5f);
	AudioClip* clip = Build(source, As(AudioCompressionFormat::PCM, AudioClipLoadType::DecompressOnLoad));
	EXPECT_EQ(clip->GetFrequency(), 44100u);
	EXPECT_EQ(clip->GetChannels(), 2u);
	EXPECT_EQ(clip->GetSamples(), 4410u);
	EXPECT_FLOAT_EQ(clip->GetLength(), 0.1f);
	EXPECT_EQ(clip->GetCompressionFormat(), AudioCompressionFormat::PCM);

	const AudioClipData* data = AudioClipAccess::GetPlayback(*clip);
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(data, AudioClipAccess::GetCooked(*clip));
	EXPECT_EQ(std::memcmp(data->pcm, source.samples.data(), source.samples.size() * sizeof(f32)), 0);
}

TEST_F(AudioClipBuildFixture, QoaPlaysDecodeEitherLoadType) {
	const DecodedAudio source = Tone(2, 48000, 12345, 300.0, 0.6f);
	AudioClip* onLoad = Build(source, As(AudioCompressionFormat::QOA, AudioClipLoadType::DecompressOnLoad));
	const AudioClipData* cooked = AudioClipAccess::GetCooked(*onLoad);
	const AudioClipData* playback = AudioClipAccess::GetPlayback(*onLoad);
	ASSERT_NE(cooked, nullptr);
	ASSERT_NE(playback, nullptr);
	EXPECT_EQ(cooked->encoding, AudioClipEncoding::Qoa);
	EXPECT_EQ(playback->encoding, AudioClipEncoding::Pcm);
	EXPECT_EQ(onLoad->GetCompressionFormat(), AudioCompressionFormat::QOA);

	std::vector<f32> decoded(static_cast<size_t>(cooked->frames) * cooked->channels);
	QoaDecodeAll(cooked->qoa, decoded.data());
	EXPECT_EQ(std::memcmp(playback->pcm, decoded.data(), decoded.size() * sizeof(f32)), 0);

	AudioClip* inMemory = Build(source, As(AudioCompressionFormat::QOA, AudioClipLoadType::CompressedInMemory));
	EXPECT_EQ(AudioClipAccess::GetPlayback(*inMemory), AudioClipAccess::GetCooked(*inMemory));
	EXPECT_EQ(AudioClipAccess::GetPlayback(*inMemory)->encoding, AudioClipEncoding::Qoa);
}

TEST_F(AudioClipBuildFixture, OpusIsAlways48kHz) {
	const DecodedAudio source = Tone(2, 44100, 44100, 300.0, 0.6f);
	AudioClip* onLoad = Build(source, As(AudioCompressionFormat::Opus, AudioClipLoadType::DecompressOnLoad));
	const AudioClipData* cooked = AudioClipAccess::GetCooked(*onLoad);
	const AudioClipData* playback = AudioClipAccess::GetPlayback(*onLoad);
	ASSERT_NE(cooked, nullptr);
	ASSERT_NE(playback, nullptr);
	EXPECT_EQ(onLoad->GetFrequency(), 48000u);
	EXPECT_EQ(onLoad->GetSamples(), 48000u);
	EXPECT_EQ(onLoad->GetCompressionFormat(), AudioCompressionFormat::Opus);
	EXPECT_EQ(cooked->encoding, AudioClipEncoding::Opus);
	EXPECT_EQ(playback->encoding, AudioClipEncoding::Pcm);
	const std::unique_ptr<AudioClipData, void (*)(AudioClipData*)> decoded(DecodeToPcm(*cooked), ReleaseClipData);
	ASSERT_TRUE(decoded);
	EXPECT_EQ(std::memcmp(playback->pcm, decoded->pcm, static_cast<size_t>(playback->frames * 2) * sizeof(f32)), 0);

	for (AudioClipLoadType loadType : {AudioClipLoadType::CompressedInMemory, AudioClipLoadType::Streaming}) {
		AudioClip* streams = Build(source, As(AudioCompressionFormat::Opus, loadType));
		const AudioClipData* block = AudioClipAccess::GetPlayback(*streams);
		ASSERT_NE(block, nullptr);
		EXPECT_EQ(block, AudioClipAccess::GetCooked(*streams));
		EXPECT_EQ(block->encoding, AudioClipEncoding::Streamed) << "load type " << static_cast<u32>(loadType);
		EXPECT_EQ(block->codec, AudioClipEncoding::Opus);
		EXPECT_NE(block->source.memory, nullptr);
		EXPECT_EQ(std::memcmp(block->prime, decoded->pcm, static_cast<size_t>(block->primeFrames * 2) * sizeof(f32)), 0);
		EXPECT_EQ(streams->GetLoadType(), loadType);
	}
}

TEST_F(AudioClipBuildFixture, MonoAveragesNormalizeLiftsPeak) {
	DecodedAudio source;
	source.layout = MakeStandardLayout(2);
	source.sampleRate = 48000;
	for (u32 frame = 0; frame < 1000; ++frame) {
		source.samples.push_back(0.5f);
		source.samples.push_back(0.25f);
	}

	AudioImportSettings averaged = As(AudioCompressionFormat::PCM, AudioClipLoadType::DecompressOnLoad);
	averaged.forceToMono = true;
	averaged.normalize = false;
	AudioClip* mono = Build(source, averaged);
	ASSERT_EQ(mono->GetChannels(), 1u);
	EXPECT_FLOAT_EQ(AudioClipAccess::GetPlayback(*mono)->pcm[500], 0.375f);
	EXPECT_EQ(AudioClipAccess::GetPlayback(*mono)->layout.positions[0], SpeakerPosition::Mono);

	AudioImportSettings normalized = averaged;
	normalized.normalize = true;
	AudioClip* loud = Build(source, normalized);
	EXPECT_FLOAT_EQ(AudioClipAccess::GetPlayback(*loud)->pcm[500], 1.0f);

	AudioImportSettings alone = As(AudioCompressionFormat::PCM, AudioClipLoadType::DecompressOnLoad);
	alone.normalize = true;
	AudioClip* untouched = Build(source, alone);
	EXPECT_EQ(untouched->GetChannels(), 2u);
	EXPECT_FLOAT_EQ(AudioClipAccess::GetPlayback(*untouched)->pcm[0], 0.5f);
}

TEST_F(AudioClipBuildFixture, RateOverrideKeepsPitchAndDuration) {
	AudioImportSettings settings = As(AudioCompressionFormat::PCM, AudioClipLoadType::DecompressOnLoad);
	settings.sampleRateSetting = AudioSampleRateSetting::OverrideSampleRate;
	settings.sampleRateOverride = 22050;

	const DecodedAudio kept = Tone(1, 44100, 22050, 1000.0, 0.5f);
	AudioClip* tone = Build(kept, settings);
	ASSERT_EQ(tone->GetFrequency(), 22050u);
	EXPECT_EQ(tone->GetSamples(), 11025u);
	const AudioClipData* data = AudioClipAccess::GetPlayback(*tone);
	EXPECT_NEAR(static_cast<f64>(CountCrossings(data->pcm, data->frames, 1)), 1000.0, 2.0);
	Vector<f32> resampled{GetMemoryResource(MemTag::Audio)};
	resampled.assign(data->pcm, data->pcm + data->frames);
	EXPECT_NEAR(Rms(resampled, 200, 10800), 0.5 / std::sqrt(2.0), 0.005);

	const DecodedAudio above = Tone(1, 44100, 22050, 15000.0, 0.5f);
	AudioClip* folded = Build(above, settings);
	const AudioClipData* foldedData = AudioClipAccess::GetPlayback(*folded);
	Vector<f32> aliased{GetMemoryResource(MemTag::Audio)};
	aliased.assign(foldedData->pcm, foldedData->pcm + foldedData->frames);
	EXPECT_LT(Rms(aliased, 200, 10800), 0.5 / std::sqrt(2.0) * 1e-3);
}

TEST_F(AudioClipBuildFixture, CookRoundTripIsStable) {
	const DecodedAudio source = Tone(2, 44100, 30000, 523.25, 0.7f);
	for (AudioCompressionFormat format : {AudioCompressionFormat::PCM, AudioCompressionFormat::QOA, AudioCompressionFormat::Opus}) {
		for (AudioClipLoadType loadType : {AudioClipLoadType::DecompressOnLoad, AudioClipLoadType::CompressedInMemory, AudioClipLoadType::Streaming}) {
			AudioClip* built = Build(source, As(format, loadType));
			CookedBytes first;
			CookInto(*built, first);
			ASSERT_TRUE(first.cooked);
			CookedBytes again;
			CookInto(*built, again);
			EXPECT_TRUE(SameBytes(first, again));

			AudioClip* loaded = Fresh();
			CookReader descriptor(first.descriptor.data(), first.descriptor.size());
			CookReader payload(first.payload.data(), first.payload.size());
			ASSERT_TRUE(loaded->LoadCooked(descriptor, payload));
			EXPECT_EQ(loaded->GetFrequency(), built->GetFrequency());
			EXPECT_EQ(loaded->GetChannels(), built->GetChannels());
			EXPECT_EQ(loaded->GetSamples(), built->GetSamples());
			EXPECT_EQ(loaded->GetLoadType(), loadType);
			EXPECT_EQ(loaded->GetCompressionFormat(), format);

			const AudioClipData* was = AudioClipAccess::GetPlayback(*built);
			const AudioClipData* now = AudioClipAccess::GetPlayback(*loaded);
			ASSERT_EQ(was->encoding, now->encoding);
			if (was->encoding == AudioClipEncoding::Pcm) {
				EXPECT_EQ(std::memcmp(was->pcm, now->pcm, static_cast<size_t>(was->frames * was->channels) * sizeof(f32)), 0);
			} else if (was->encoding == AudioClipEncoding::Streamed) {
				EXPECT_EQ(std::memcmp(was->prime, now->prime, static_cast<size_t>(was->primeFrames * was->channels) * sizeof(f32)), 0);
			}

			CookedBytes second;
			CookInto(*loaded, second);
			EXPECT_TRUE(SameBytes(first, second));
		}
	}
}

TEST_F(AudioClipBuildFixture, LoadCookedRefusesRecordThatContradictsItself) {
	AudioClip* pcm = Build(Tone(2, 48000, 1000, 440.0, 0.5f), As(AudioCompressionFormat::PCM, AudioClipLoadType::DecompressOnLoad));
	AudioClip* qoa = Build(Tone(2, 48000, 1000, 440.0, 0.5f), As(AudioCompressionFormat::QOA, AudioClipLoadType::CompressedInMemory));
	CookedBytes pcmBytes;
	CookedBytes qoaBytes;
	CookInto(*pcm, pcmBytes);
	CookInto(*qoa, qoaBytes);

	constexpr size_t FORMAT = 0;
	constexpr size_t LOAD_TYPE = 1;
	constexpr size_t CHANNELS = 6;
	constexpr size_t FIRST_POSITION = 7;
	constexpr size_t FRAMES = 9;

	auto refuses = [this](const CookedBytes& bytes, auto&& corrupt) {
		Vector<u8> descriptor = bytes.descriptor;
		Vector<u8> payload = bytes.payload;
		corrupt(descriptor, payload);
		AudioClip* clip = Fresh();
		CookReader descriptorReader(descriptor.data(), descriptor.size());
		CookReader payloadReader(payload.data(), payload.size());
		return !clip->LoadCooked(descriptorReader, payloadReader) && AudioClipAccess::GetPlayback(*clip) == nullptr;
	};

	EXPECT_FALSE(refuses(pcmBytes, [](Vector<u8>&, Vector<u8>&) {}));
	EXPECT_TRUE(refuses(pcmBytes, [](Vector<u8>& d, Vector<u8>&) { d[FORMAT] = 7; }));
	EXPECT_TRUE(refuses(pcmBytes, [](Vector<u8>& d, Vector<u8>&) { d[LOAD_TYPE] = 9; }));
	EXPECT_TRUE(refuses(pcmBytes, [](Vector<u8>& d, Vector<u8>&) { d[CHANNELS] = 0; }));
	EXPECT_TRUE(refuses(pcmBytes, [](Vector<u8>& d, Vector<u8>&) { d[FIRST_POSITION] = 200; }));
	EXPECT_TRUE(refuses(pcmBytes, [](Vector<u8>& d, Vector<u8>&) { d[FRAMES] ^= 1; }));
	EXPECT_TRUE(refuses(pcmBytes, [](Vector<u8>&, Vector<u8>& p) { p.resize(p.size() - 4); }));
	EXPECT_TRUE(refuses(qoaBytes, [](Vector<u8>& d, Vector<u8>&) { d[FRAMES] ^= 1; }));
	EXPECT_TRUE(refuses(qoaBytes, [](Vector<u8>&, Vector<u8>& p) { p[0] = 'x'; }));

	AudioClip* opus = Build(Tone(2, 48000, 5000, 440.0, 0.5f), As(AudioCompressionFormat::Opus, AudioClipLoadType::DecompressOnLoad));
	CookedBytes opusBytes;
	CookInto(*opus, opusBytes);
	constexpr size_t RATE = 2;
	EXPECT_FALSE(refuses(opusBytes, [](Vector<u8>&, Vector<u8>&) {}));
	EXPECT_TRUE(refuses(opusBytes, [](Vector<u8>& d, Vector<u8>&) { d[RATE] ^= 1; }));
	EXPECT_TRUE(refuses(opusBytes, [](Vector<u8>&, Vector<u8>& p) { p[16] ^= 0x40; }));
	EXPECT_TRUE(refuses(opusBytes, [](Vector<u8>& d, Vector<u8>&) { d[FRAMES + 1] ^= 0x10; }));
}

TEST(AudioImportSettingsTest, ReadByNameElseDefault) {
	using namespace Shalltear::AudioImpl;
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("forceToMono", JSONValue::MakeBool(true));
	settings.AddMember("normalize", JSONValue::MakeBool(false));
	settings.AddMember("loadType", JSONValue::MakeString("CompressedInMemory"));
	settings.AddMember("compressionFormat", JSONValue::MakeString("PCM"));
	settings.AddMember("sampleRateSetting", JSONValue::MakeString("OverrideSampleRate"));
	settings.AddMember("sampleRateOverride", JSONValue::MakeNumber(22050.0));
	settings.AddMember("quality", JSONValue::MakeNumber(0.25));
	AudioImportSettings read;
	ReadImportSettings(settings, read);
	EXPECT_TRUE(read.forceToMono);
	EXPECT_FALSE(read.normalize);
	EXPECT_EQ(read.loadType, AudioClipLoadType::CompressedInMemory);
	EXPECT_EQ(read.compressionFormat, AudioCompressionFormat::PCM);
	EXPECT_EQ(read.sampleRateSetting, AudioSampleRateSetting::OverrideSampleRate);
	EXPECT_EQ(read.sampleRateOverride, 22050u);
	EXPECT_EQ(read.quality, 0.25f);

	JSONValue unknown = JSONValue::MakeObject();
	unknown.AddMember("compressionFormat", JSONValue::MakeString("Vorbis"));
	unknown.AddMember("sampleRateSetting", JSONValue::MakeString("OptimiseSampleRate"));
	unknown.AddMember("sampleRateOverride", JSONValue::MakeNumber(1000000.0));
	unknown.AddMember("quality", JSONValue::MakeNumber(1.5));
	AudioImportSettings kept;
	ReadImportSettings(unknown, kept);
	const AudioImportSettings defaults;
	EXPECT_EQ(kept.compressionFormat, defaults.compressionFormat);
	EXPECT_EQ(kept.sampleRateSetting, defaults.sampleRateSetting);
	EXPECT_EQ(kept.sampleRateOverride, defaults.sampleRateOverride);
	EXPECT_EQ(kept.quality, defaults.quality);
	EXPECT_EQ(defaults.compressionFormat, AudioCompressionFormat::Opus);
	EXPECT_EQ(defaults.quality, 0.7f);

	JSONValue opus = JSONValue::MakeObject();
	opus.AddMember("compressionFormat", JSONValue::MakeString("Opus"));
	AudioImportSettings named;
	named.compressionFormat = AudioCompressionFormat::PCM;
	ReadImportSettings(opus, named);
	EXPECT_EQ(named.compressionFormat, AudioCompressionFormat::Opus);

	JSONValue written = JSONValue::MakeObject();
	WriteImportSettings(read, written);
	AudioImportSettings roundTrip;
	ReadImportSettings(written, roundTrip);
	EXPECT_EQ(roundTrip.forceToMono, read.forceToMono);
	EXPECT_EQ(roundTrip.normalize, read.normalize);
	EXPECT_EQ(roundTrip.loadType, read.loadType);
	EXPECT_EQ(roundTrip.compressionFormat, read.compressionFormat);
	EXPECT_EQ(roundTrip.sampleRateSetting, read.sampleRateSetting);
	EXPECT_EQ(roundTrip.sampleRateOverride, read.sampleRateOverride);
	EXPECT_EQ(roundTrip.quality, read.quality);
}

TEST(AudioImportSettingsTest, LoadTypeChosenByLength) {
	using namespace Shalltear::AudioImpl;
	EXPECT_EQ(DefaultLoadType(0.0), AudioClipLoadType::DecompressOnLoad);
	EXPECT_EQ(DefaultLoadType(9.999), AudioClipLoadType::DecompressOnLoad);
	EXPECT_EQ(DefaultLoadType(10.0), AudioClipLoadType::CompressedInMemory);
	EXPECT_EQ(DefaultLoadType(60.0), AudioClipLoadType::CompressedInMemory);
	EXPECT_EQ(DefaultLoadType(60.001), AudioClipLoadType::Streaming);
	EXPECT_EQ(DefaultLoadType(3600.0), AudioClipLoadType::Streaming);
}

TEST(AudioChannelOrderTest, AWaveSpeakerMaskNamesItsChannels) {
	using namespace Shalltear::AudioImpl;
	const std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Audio");
	ASSERT_FALSE(fixtures.empty()) << std::filesystem::current_path().generic_string();

	Buffer back = FileSystem::ReadFile(Path((fixtures / "back51_48000_pcm16.wav").generic_string()), MemTag::Asset);
	ASSERT_TRUE(back);
	ChannelLayout layout;
	ASSERT_TRUE(ReadWaveChannelLayout(back.data, back.size, 6, layout));
	const SpeakerPosition expected[] = {SpeakerPosition::FrontLeft, SpeakerPosition::FrontRight, SpeakerPosition::FrontCenter,
										SpeakerPosition::LowFrequency, SpeakerPosition::BackLeft, SpeakerPosition::BackRight};
	for (u32 channel = 0; channel < 6; ++channel) {
		EXPECT_EQ(layout.positions[channel], expected[channel]) << "channel " << channel;
	}

	Buffer plain = FileSystem::ReadFile(Path((fixtures / "stereo_44100_pcm16.wav").generic_string()), MemTag::Asset);
	ASSERT_TRUE(plain);
	EXPECT_FALSE(ReadWaveChannelLayout(plain.data, plain.size, 2, layout));
}

TEST(AudioChannelOrderTest, VorbisAndFlacOrdersAreTheirSpecifications) {
	using namespace Shalltear::AudioImpl;
	const ChannelLayout three = MakeVorbisLayout(3);
	EXPECT_EQ(three.positions[0], SpeakerPosition::FrontLeft);
	EXPECT_EQ(three.positions[1], SpeakerPosition::FrontCenter);
	EXPECT_EQ(three.positions[2], SpeakerPosition::FrontRight);
	const ChannelLayout vorbisSix = MakeVorbisLayout(6);
	EXPECT_EQ(vorbisSix.positions[1], SpeakerPosition::FrontCenter);
	EXPECT_EQ(vorbisSix.positions[5], SpeakerPosition::LowFrequency);

	const ChannelLayout quad = MakeFlacLayout(4);
	EXPECT_EQ(quad.positions[2], SpeakerPosition::BackLeft);
	EXPECT_EQ(quad.positions[3], SpeakerPosition::BackRight);
	const ChannelLayout flacSix = MakeFlacLayout(6);
	EXPECT_EQ(flacSix.positions[3], SpeakerPosition::LowFrequency);
	EXPECT_EQ(flacSix.positions[4], SpeakerPosition::BackLeft);
	EXPECT_EQ(flacSix.positions[5], SpeakerPosition::BackRight);
	EXPECT_EQ(MakeFlacLayout(8).positions[7], SpeakerPosition::SideRight);
}

namespace {

	DecodedAudio Pair(const DecodedAudio& left, const DecodedAudio& right) {
		DecodedAudio pair;
		pair.layout = MakeStandardLayout(2);
		pair.sampleRate = left.sampleRate;
		pair.samples.resize(left.samples.size() * 2);
		for (size_t frame = 0; frame < left.samples.size(); ++frame) {
			pair.samples[frame * 2] = left.samples[frame];
			pair.samples[frame * 2 + 1] = right.samples[frame];
		}
		return pair;
	}

	DecodedAudio Sum(DecodedAudio first, const DecodedAudio& second) {
		for (size_t index = 0; index < first.samples.size(); ++index) {
			first.samples[index] += second.samples[index];
		}
		return first;
	}

} // namespace

TEST(AudioImportOptimizeTest, RateIsLowestKeepingContent) {
	using namespace Shalltear::AudioImpl;
	EXPECT_EQ(OptimalSampleRate(Tone(1, 44100, 44100, 1000.0, 0.5f)), 8000u);
	EXPECT_EQ(OptimalSampleRate(Tone(1, 44100, 44100, 3000.0, 0.5f)), 8000u);
	EXPECT_EQ(OptimalSampleRate(Tone(1, 44100, 44100, 4500.0, 0.5f)), 11025u);
	EXPECT_EQ(OptimalSampleRate(Tone(1, 44100, 44100, 9000.0, 0.5f)), 22050u);
	EXPECT_EQ(OptimalSampleRate(Tone(1, 44100, 44100, 5300.0, 0.5f)), 22050u);
	EXPECT_EQ(OptimalSampleRate(Tone(1, 48000, 48000, 15000.0, 0.5f)), 44100u);
}

TEST(AudioImportOptimizeTest, SixtyDecibelsUnderLoudestIsFloor) {
	using namespace Shalltear::AudioImpl;
	const DecodedAudio tone = Tone(1, 44100, 44100, 1000.0, 0.5f);
	const f32 below = 0.5f * std::pow(10.0f, -70.0f / 20.0f);
	const f32 above = 0.5f * std::pow(10.0f, -50.0f / 20.0f);
	EXPECT_EQ(OptimalSampleRate(Sum(tone, Tone(1, 44100, 44100, 12000.0, below))), 8000u);
	EXPECT_EQ(OptimalSampleRate(Sum(tone, Tone(1, 44100, 44100, 12000.0, above))), 44100u);
}

TEST(AudioImportOptimizeTest, NeverRaisesRate) {
	using namespace Shalltear::AudioImpl;
	EXPECT_EQ(OptimalSampleRate(Tone(1, 44100, 44100, 21000.0, 0.5f)), 44100u);
	EXPECT_EQ(OptimalSampleRate(Tone(1, 48000, 48000, 21500.0, 0.5f)), 48000u);
	EXPECT_EQ(OptimalSampleRate(Tone(1, 22050, 22050, 1000.0, 0.0f)), 22050u);
}

TEST(AudioImportOptimizeTest, AllChannelsCountNoPhaseCancel) {
	using namespace Shalltear::AudioImpl;
	const DecodedAudio low = Tone(1, 44100, 44100, 1000.0, 0.5f);
	const DecodedAudio high = Tone(1, 44100, 44100, 9000.0, 0.5f);
	EXPECT_EQ(OptimalSampleRate(Pair(low, high)), 22050u);
	DecodedAudio inverted = high;
	for (f32& sample : inverted.samples) {
		sample = -sample;
	}
	EXPECT_EQ(OptimalSampleRate(Pair(high, inverted)), 22050u);
}

TEST_F(AudioClipBuildFixture, OptimizedClipIsBuiltAtRateChosen) {
	AudioImportSettings settings = As(AudioCompressionFormat::PCM, AudioClipLoadType::DecompressOnLoad);
	settings.sampleRateSetting = AudioSampleRateSetting::OptimizeSampleRate;
	AudioClip* clip = Build(Tone(1, 44100, 44100, 1000.0, 0.5f), settings);
	EXPECT_EQ(clip->GetFrequency(), 8000u);
	EXPECT_EQ(clip->GetSamples(), 8000u);

	JSONValue written = JSONValue::MakeObject();
	WriteImportSettings(settings, written);
	EXPECT_EQ(written["sampleRateSetting"].AsString(), "OptimizeSampleRate");
	AudioImportSettings read;
	ReadImportSettings(written, read);
	EXPECT_EQ(read.sampleRateSetting, AudioSampleRateSetting::OptimizeSampleRate);
}

namespace {

	void ExpectedEnvelope(const DecodedAudio& audio, u32 spans, std::vector<f32>& low, std::vector<f32>& high) {
		const u32 channels = audio.layout.count;
		const u64 frames = audio.GetFrames();
		low.assign(static_cast<size_t>(spans) * channels, 0.0f);
		high.assign(static_cast<size_t>(spans) * channels, 0.0f);
		for (u64 frame = 0; frame < frames; ++frame) {
			const size_t span = static_cast<size_t>(frame * spans / frames);
			for (u32 channel = 0; channel < channels; ++channel) {
				const f32 sample = audio.samples[static_cast<size_t>(frame * channels + channel)];
				low[channel * spans + span] = std::min(low[channel * spans + span], sample);
				high[channel * spans + span] = std::max(high[channel * spans + span], sample);
			}
		}
	}

	bool ReadWhole(const AudioClip& clip, u32 spans, AudioEnvelope& out) {
		AudioEnvelopeBuilder builder;
		if (!builder.Begin(clip, spans)) {
			return false;
		}
		const bool complete = builder.Advance(~0ull);
		out = builder.Get();
		return complete;
	}

} // namespace

TEST_F(AudioClipBuildFixture, EnvelopeIsSpanMinMax) {
	DecodedAudio audio;
	audio.layout = MakeStandardLayout(2);
	audio.sampleRate = 48000;
	const u64 frames = 10007;
	audio.samples.resize(static_cast<size_t>(frames * 2));
	for (u64 frame = 0; frame < frames; ++frame) {
		audio.samples[frame * 2] = static_cast<f32>(frame % 97) / 97.0f - 0.5f;
		audio.samples[frame * 2 + 1] = (frame % 1500 == 3) ? -0.9f : 0.25f;
	}
	const DecodedAudio kept = audio;
	AudioClip* clip = Build(audio, As(AudioCompressionFormat::PCM, AudioClipLoadType::DecompressOnLoad));

	AudioEnvelope envelope;
	ASSERT_TRUE(ReadWhole(*clip, 64, envelope));
	std::vector<f32> low;
	std::vector<f32> high;
	ExpectedEnvelope(kept, 64, low, high);
	ASSERT_EQ(envelope.low.size(), low.size());
	for (size_t at = 0; at < low.size(); ++at) {
		ASSERT_EQ(envelope.low[at], low[at]) << "span " << at % 64 << " of channel " << at / 64;
		ASSERT_EQ(envelope.high[at], high[at]) << "span " << at % 64 << " of channel " << at / 64;
	}
	EXPECT_EQ(envelope.framesRead, frames);
}

TEST_F(AudioClipBuildFixture, EnvelopeAlignsForEveryCodec) {
	constexpr u32 SPANS = 100;
	for (const AudioCompressionFormat format : {AudioCompressionFormat::PCM, AudioCompressionFormat::QOA, AudioCompressionFormat::Opus}) {
		for (const AudioClipLoadType loadType : {AudioClipLoadType::DecompressOnLoad, AudioClipLoadType::CompressedInMemory, AudioClipLoadType::Streaming}) {
			DecodedAudio audio = Tone(1, 48000, 48000, 1000.0, 0.5f);
			std::fill(audio.samples.begin() + 24000, audio.samples.end(), 0.0f);
			AudioClip* clip = Build(audio, As(format, loadType));
			AudioEnvelope envelope;
			ASSERT_TRUE(ReadWhole(*clip, SPANS, envelope)) << "format " << static_cast<int>(format) << ", load type " << static_cast<int>(loadType);
			const f32 tolerance = format == AudioCompressionFormat::PCM ? 1e-6f : 0.08f;
			for (u32 span = 1; span < SPANS / 2 - 1; ++span) {
				ASSERT_NEAR(envelope.high[span], 0.5f, tolerance) << "span " << span << " of format " << static_cast<int>(format);
				ASSERT_NEAR(envelope.low[span], -0.5f, tolerance) << "span " << span << " of format " << static_cast<int>(format);
			}
			for (u32 span = SPANS / 2 + 2; span < SPANS; ++span) {
				ASSERT_LT(envelope.high[span], 0.02f) << "span " << span << " of format " << static_cast<int>(format);
			}
		}
	}
}

TEST_F(AudioClipBuildFixture, SlicedEnvelopeMatchesWhole) {
	for (const AudioCompressionFormat format : {AudioCompressionFormat::PCM, AudioCompressionFormat::QOA, AudioCompressionFormat::Opus}) {
		AudioClip* clip = Build(Tone(2, 48000, 30000, 440.0, 0.5f), As(format, AudioClipLoadType::CompressedInMemory));
		AudioEnvelope whole;
		ASSERT_TRUE(ReadWhole(*clip, 50, whole));
		AudioEnvelopeBuilder builder;
		ASSERT_TRUE(builder.Begin(*clip, 50));
		u32 slices = 0;
		while (!builder.Advance(1)) {
			ASSERT_FALSE(builder.IsFinished()) << slices << " slices";
			++slices;
		}
		EXPECT_GT(slices, 3u);
		EXPECT_EQ(builder.Get().low, whole.low) << "format " << static_cast<int>(format);
		EXPECT_EQ(builder.Get().high, whole.high) << "format " << static_cast<int>(format);
	}
}

TEST_F(AudioClipBuildFixture, EnvelopeCompletesAfterClipIsGone) {
	AudioClip* clip = Build(Tone(1, 48000, 48000, 1000.0, 0.5f), As(AudioCompressionFormat::Opus, AudioClipLoadType::CompressedInMemory));
	AudioEnvelopeBuilder builder;
	ASSERT_TRUE(builder.Begin(*clip, 20));
	ASSERT_FALSE(builder.Advance(1));
	std::erase(_clips, clip);
	Delete(clip);
	EXPECT_TRUE(builder.Advance(~0ull));
	EXPECT_NEAR(builder.Get().high[10], 0.5f, 0.08f);
}

TEST_F(AudioClipBuildFixture, ThreadedEnvelopeReadHoldsBlock) {
	AudioClip* clip = Build(Tone(2, 48000, 30000, 440.0, 0.5f), As(AudioCompressionFormat::Opus, AudioClipLoadType::CompressedInMemory));
	AudioEnvelope whole;
	ASSERT_TRUE(ReadWhole(*clip, 50, whole));
	const AudioClipData* cooked = AudioClipAccess::GetCooked(*clip);
	const u32 unheld = cooked->references;

	AudioEnvelopeBuilder builder;
	ASSERT_TRUE(builder.Begin(*clip, 50));
	ASSERT_EQ(cooked->references, unheld + 1);
	bool complete = false;
	std::thread worker([&builder, &complete]() { complete = builder.Advance(~0ull); });
	worker.join();
	EXPECT_TRUE(complete);
	EXPECT_TRUE(builder.IsFinished());
	EXPECT_EQ(cooked->references, unheld + 1);
	EXPECT_EQ(builder.Get().low, whole.low);
	EXPECT_EQ(builder.Get().high, whole.high);

	builder.Release();
	EXPECT_EQ(cooked->references, unheld);
	EXPECT_EQ(builder.Get().high, whole.high);
}

TEST_F(AudioClipBuildFixture, EmptyClipHasNoEnvelope) {
	AudioEnvelopeBuilder builder;
	EXPECT_FALSE(builder.Begin(*Fresh(), 20));
	AudioClip* clip = Build(Tone(1, 48000, 4800, 1000.0, 0.5f), As(AudioCompressionFormat::PCM, AudioClipLoadType::DecompressOnLoad));
	EXPECT_FALSE(builder.Begin(*clip, 0));
}

TEST(AudioImportSettingsTest, MergeWritesKeysOnce) {
	using namespace Shalltear::AudioImpl;
	JSONValue previous = JSONValue::MakeObject();
	previous.AddMember("compressionFormat", JSONValue::MakeString("PCM"));
	previous.AddMember("loadInBackground", JSONValue::MakeBool(true));
	previous.AddMember("later", JSONValue::MakeNumber(5.0));
	AudioImportSettings settings;
	settings.compressionFormat = AudioCompressionFormat::QOA;

	JSONValue merged = JSONValue::MakeObject();
	MergeImportSettings(previous, settings, merged);
	u32 formats = 0;
	for (size_t index = 0; index < merged.MemberCount(); ++index) {
		formats += merged.KeyAt(index) == "compressionFormat" ? 1 : 0;
	}
	EXPECT_EQ(formats, 1u);
	EXPECT_EQ(merged["compressionFormat"].AsString(), "QOA");
	EXPECT_TRUE(merged["loadInBackground"].AsBool(false));
	EXPECT_EQ(merged["later"].AsNumber(), 5.0);
	AudioImportSettings read;
	ReadImportSettings(merged, read);
	EXPECT_EQ(read.compressionFormat, AudioCompressionFormat::QOA);
}

TEST(AudioWaveformTest, ColumnsFoldOrRepeatSpans) {
	using namespace Shalltear::AudioImpl;
	AudioEnvelope envelope;
	envelope.channels = 1;
	envelope.spans = 4;
	envelope.frames = 400;
	envelope.framesRead = 400;
	envelope.low.assign({-0.1f, -0.4f, -0.2f, -0.3f});
	envelope.high.assign({0.1f, 0.2f, 0.4f, 0.3f});

	f32 low[8];
	f32 high[8];
	GetEnvelopeColumns(envelope, 0, 2, low, high);
	EXPECT_EQ(low[0], -0.4f);
	EXPECT_EQ(high[0], 0.2f);
	EXPECT_EQ(low[1], -0.3f);
	EXPECT_EQ(high[1], 0.4f);

	GetEnvelopeColumns(envelope, 0, 8, low, high);
	for (u32 column = 0; column < 8; ++column) {
		EXPECT_EQ(high[column], envelope.high[column / 2]) << "column " << column;
		EXPECT_EQ(low[column], envelope.low[column / 2]) << "column " << column;
	}

	GetEnvelopeColumns(envelope, 1, 2, low, high);
	EXPECT_EQ(low[0], 0.0f);
	EXPECT_EQ(high[1], 0.0f);
}

#else

#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"

#include "ScopedImportCostThreshold.h"

namespace {

	class AudioImportFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		ModuleHandle _module;
		ScopedImportCostThreshold _cacheAll{ScopedImportCostThreshold::CacheEverything};

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_audio_import_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			FileSystem::WriteTextFile(_paths.project, str("[Audio]\ndevice = None\n"));

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			_module = ModuleSystem::Load(Path("Audio.dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool Stage(const char* name) {
			const std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Audio");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / name, _root / "Assets" / name, std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}

		static Path AssetPath(const char* name) { return Path(str("project://Assets/") + name); }

		template <typename T>
		static T Field(Asset* asset, const char* name) {
			const FieldInfo* field = FindField(asset->GetType(), StringHash(name));
			if (!field || !field->addrFn) {
				ADD_FAILURE() << "clip reflects no field " << name;
				return T{};
			}
			return *static_cast<const T*>(field->addrFn(asset));
		}

		static Asset* ReimportAs(const char* name, JSONValue settings) {
			if (!AssetDatabase::WriteImporterSettings(AssetPath(name), settings) || !AssetDatabase::ReimportAsset(AssetPath(name))) {
				return nullptr;
			}
			return AssetDatabase::LoadAsset(AssetPath(name));
		}

		static JSONValue PcmSettings() {
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("compressionFormat", JSONValue::MakeString("PCM"));
			return settings;
		}
	};

	struct FormatCase {
		const char* file;
		u32 rate;
		u32 channels;
		u64 frames;
		u64 slack;
	};

} // namespace

TEST_F(AudioImportFixture, EveryCommonFormatBecomesClipOfShape) {
	const FormatCase cases[] = {
		{"stereo_44100_pcm16.wav", 44100, 2, 4410, 0},
		{"stereo_44100_pcm16.aiff", 44100, 2, 4410, 0},
		{"stereo_44100_pcm16.flac", 44100, 2, 4410, 0},
		{"stereo_44100.mp3", 44100, 2, 4410, 3 * 1152},
		{"stereo_44100.ogg", 44100, 2, 4410, 1024},
		{"mono_22050_float.wav", 22050, 1, 2205, 0},
		{"quad_48000_pcm16.flac", 48000, 4, 2400, 0},
		{"three_48000.ogg", 48000, 3, 2400, 1024},
		{"back51_48000_pcm16.wav", 48000, 6, 2400, 0},
	};
	for (const FormatCase& format : cases) {
		ASSERT_TRUE(Stage(format.file)) << format.file;
	}
	for (const FormatCase& format : cases) {
		Asset* opus = AssetDatabase::LoadAsset(AssetPath(format.file));
		ASSERT_NE(opus, nullptr) << format.file;
		EXPECT_EQ(Field<u8>(opus, "_compressionFormat"), 2u) << format.file;
		EXPECT_EQ(Field<u32>(opus, "_frequency"), 48000u) << format.file;
		EXPECT_EQ(Field<u32>(opus, "_channels"), format.channels) << format.file;
		const u64 at48 = (format.frames * 48000 + format.rate - 1) / format.rate;
		EXPECT_GE(Field<u64>(opus, "_samples"), at48) << format.file;
		EXPECT_LE(Field<u64>(opus, "_samples"), at48 + (format.slack * 48000 + format.rate - 1) / format.rate + 1) << format.file;
		AssetDatabase::UnloadAsset(opus);

		Asset* clip = ReimportAs(format.file, PcmSettings());
		ASSERT_NE(clip, nullptr) << format.file;
		EXPECT_EQ(clip->GetType()->name, StringHash("AudioClip")) << format.file;
		EXPECT_EQ(Field<u32>(clip, "_frequency"), format.rate) << format.file;
		EXPECT_EQ(Field<u32>(clip, "_channels"), format.channels) << format.file;
		const u64 frames = Field<u64>(clip, "_samples");
		EXPECT_GE(frames, format.frames) << format.file << " lost audio";
		EXPECT_LE(frames, format.frames + format.slack) << format.file;
		AssetDatabase::UnloadAsset(clip);
	}
}

TEST_F(AudioImportFixture, EachContainersSpeakerOrderReachesCookedClip) {
	struct OrderCase {
		const char* file;
		u32 channels;
		u8 positions[6];
		f32 tolerance;
	};
	const OrderCase cases[] = {
		{"quad_48000_pcm16.flac", 4, {FRONT_LEFT, FRONT_RIGHT, BACK_LEFT, BACK_RIGHT}, 0.002f},
		{"three_48000.ogg", 3, {FRONT_LEFT, FRONT_CENTER, FRONT_RIGHT}, 0.03f},
		{"back51_48000_pcm16.wav", 6, {FRONT_LEFT, FRONT_RIGHT, FRONT_CENTER, LOW_FREQUENCY, BACK_LEFT, BACK_RIGHT}, 0.002f},
	};
	for (const OrderCase& order : cases) {
		ASSERT_TRUE(Stage(order.file));
	}
	for (const OrderCase& order : cases) {
		Asset* first = AssetDatabase::LoadAsset(AssetPath(order.file));
		ASSERT_NE(first, nullptr) << order.file;
		AssetDatabase::UnloadAsset(first);
		Asset* clip = ReimportAs(order.file, PcmSettings());
		ASSERT_NE(clip, nullptr) << order.file;

		CookedBytes cooked;
		CookInto(*clip, cooked);
		const Descriptor descriptor = ReadDescriptor(cooked);
		ASSERT_TRUE(descriptor.read) << order.file;
		ASSERT_EQ(descriptor.format, 0u) << order.file;
		ASSERT_EQ(descriptor.channels, order.channels) << order.file;
		const std::vector<f32> peaks = ChannelPeaks(cooked, order.channels);
		for (u32 channel = 0; channel < order.channels; ++channel) {
			EXPECT_EQ(descriptor.positions[channel], order.positions[channel]) << order.file << " channel " << channel;
			EXPECT_NEAR(peaks[channel], 0.1f * static_cast<f32>(channel + 1), order.tolerance) << order.file << " channel " << channel;
		}
		AssetDatabase::UnloadAsset(clip);
	}
}

TEST_F(AudioImportFixture, SidecarEditDrivesNextImport) {
	ASSERT_TRUE(Stage("stereo_44100_pcm16.wav"));
	Asset* clip = AssetDatabase::LoadAsset(AssetPath("stereo_44100_pcm16.wav"));
	ASSERT_NE(clip, nullptr);
	EXPECT_EQ(Field<u8>(clip, "_compressionFormat"), 2u);
	AssetDatabase::UnloadAsset(clip);

	const str sidecar = FileSystem::ReadTextFile(AssetDatabase::GetMetaPath(AssetPath("stereo_44100_pcm16.wav")));
	for (const char* key : {"\"forceToMono\"", "\"normalize\"", "\"loadType\"", "\"compressionFormat\"", "\"sampleRateSetting\"", "\"sampleRateOverride\"",
							"\"quality\""}) {
		EXPECT_NE(sidecar.find(key), str::npos) << key << ":\n"
												<< sidecar;
	}

	JSONValue settings = PcmSettings();
	settings.AddMember("forceToMono", JSONValue::MakeBool(true));
	Asset* mono = ReimportAs("stereo_44100_pcm16.wav", settings);
	ASSERT_NE(mono, nullptr);
	EXPECT_EQ(Field<u32>(mono, "_channels"), 1u);
	EXPECT_EQ(Field<u8>(mono, "_compressionFormat"), 0u);
	CookedBytes cooked;
	CookInto(*mono, cooked);
	EXPECT_NEAR(ChannelPeaks(cooked, 1)[0], 1.0f, 1e-3f);
	AssetDatabase::UnloadAsset(mono);
}

TEST_F(AudioImportFixture, NewSidecarLoadTypeByLength) {
	auto writeWave = [this](const char* name, u32 seconds) {
		const u32 bytes = seconds * 48000 * 2;
		std::vector<u8> file(44 + bytes, 0);
		const auto u32At = [&file](size_t at, u32 value) {
			for (u32 byte = 0; byte < 4; ++byte) {
				file[at + byte] = static_cast<u8>(value >> (8 * byte));
			}
		};
		std::memcpy(file.data(), "RIFF", 4);
		u32At(4, 36 + bytes);
		std::memcpy(file.data() + 8, "WAVEfmt ", 8);
		u32At(16, 16);
		u32At(20, 1 | (1u << 16));
		u32At(24, 48000);
		u32At(28, 48000 * 2);
		u32At(32, 2 | (16u << 16));
		std::memcpy(file.data() + 36, "data", 4);
		u32At(40, bytes);
		std::ofstream(_root / "Assets" / name, std::ios::binary).write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
	};
	writeWave("blip.wav", 1);
	writeWave("line.wav", 12);
	writeWave("theme.wav", 61);
	AssetDatabase::ScanProjectDirectory();

	auto sidecarLoadType = [](const char* name) {
		const str text = FileSystem::ReadTextFile(AssetDatabase::GetMetaPath(AssetPath(name)));
		LinearAllocator arena(text.size() + 64, MemTag::Core);
		const JSONValue root = JSONParse(text, arena);
		return str(root["importer"]["settings"]["loadType"].AsString());
	};
	EXPECT_EQ(sidecarLoadType("blip.wav"), "DecompressOnLoad");
	EXPECT_EQ(sidecarLoadType("line.wav"), "CompressedInMemory");
	EXPECT_EQ(sidecarLoadType("theme.wav"), "Streaming");

	Asset* line = AssetDatabase::LoadAsset(AssetPath("line.wav"));
	ASSERT_NE(line, nullptr);
	EXPECT_EQ(Field<u8>(line, "_loadType"), 1u);
	AssetDatabase::UnloadAsset(line);

	JSONValue streaming = JSONValue::MakeObject();
	streaming.AddMember("loadType", JSONValue::MakeString("Streaming"));
	Asset* edited = ReimportAs("line.wav", streaming);
	ASSERT_NE(edited, nullptr);
	EXPECT_EQ(Field<u8>(edited, "_loadType"), 2u);
	AssetDatabase::UnloadAsset(edited);
	EXPECT_EQ(sidecarLoadType("line.wav"), "Streaming");
}

TEST_F(AudioImportFixture, SecondOpenUsesArtifact) {
	ASSERT_TRUE(Stage("stereo_44100_pcm16.flac"));
	Asset* imported = AssetDatabase::LoadAsset(AssetPath("stereo_44100_pcm16.flac"));
	ASSERT_NE(imported, nullptr);
	CookedBytes first;
	CookInto(*imported, first);
	ASSERT_TRUE(first.cooked);
	AssetDatabase::UnloadAsset(imported);

	std::vector<std::filesystem::path> artifacts;
	std::error_code ec;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(_root / "Library" / "Artifacts", ec)) {
		if (entry.path().extension() == ".slart") {
			artifacts.push_back(entry.path());
		}
	}
	ASSERT_EQ(artifacts.size(), 1u);
	const auto written = std::filesystem::last_write_time(artifacts[0], ec);

	AssetDatabase::Terminate();
	AssetDatabase::Initialize();
	Asset* served = AssetDatabase::LoadAsset(AssetPath("stereo_44100_pcm16.flac"));
	ASSERT_NE(served, nullptr);
	EXPECT_EQ(std::filesystem::last_write_time(artifacts[0], ec), written);
	CookedBytes second;
	CookInto(*served, second);
	EXPECT_TRUE(SameBytes(first, second));
	AssetDatabase::UnloadAsset(served);
}

TEST_F(AudioImportFixture, MislabeledFileDoesNotImport) {
	FileSystem::WriteTextFile(Path((_root / "Assets" / "noise.wav").generic_string()), str("this is not a RIFF file"));
	FileSystem::WriteTextFile(Path((_root / "Assets" / "noise.ogg").generic_string()), str("nor is this an Ogg stream"));
	EXPECT_EQ(AssetDatabase::LoadAsset(AssetPath("noise.wav")), nullptr);
	EXPECT_EQ(AssetDatabase::LoadAsset(AssetPath("noise.ogg")), nullptr);
}

#endif
