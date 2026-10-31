#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioClipAccess.h"
#include "AudioImportOps.h"
#include "AudioLiveFixture.h"
#include "AudioOfflineMixer.h"
#include "AudioProfilerOps.h"
#include "AudioStream.h"
#include "AudioStreamer.h"
#include "AudioWaveform.h"

#include "Shalltear/Audio/Audio.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Asset/PakFormat.h"

#include "Shalltear/Editor/CookOps.h"

#include "Asset/ImportArtifact.h"
#include "Asset/ImportArtifactStore.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace AudioTesting;

namespace {

	constexpr u32 BLOCK = AudioRenderer::BLOCK_FRAMES;
	constexpr u32 PRIME = 8192;

	DecodedAudio MakeSignal(u64 frames, u32 channels, u32 seed = 1) {
		DecodedAudio audio;
		audio.layout = MakeStandardLayout(channels);
		audio.sampleRate = 48000;
		audio.samples.resize(static_cast<size_t>(frames * channels));
		u32 state = seed * 2654435761u + 1u;
		for (u64 frame = 0; frame < frames; ++frame) {
			for (u32 channel = 0; channel < channels; ++channel) {
				state = state * 1664525u + 1013904223u;
				const f64 noise = static_cast<f64>(static_cast<i32>(state >> 16) - 32768) / 32768.0;
				const f64 tone = std::sin(static_cast<f64>(frame) * 0.011 * static_cast<f64>(channel + 1));
				audio.samples[static_cast<size_t>(frame * channels + channel)] = static_cast<f32>(0.4 * tone + 0.1 * noise);
			}
		}
		return audio;
	}

	AudioClipData* Resident(const DecodedAudio& audio, AudioCompressionFormat format) {
		str error;
		AudioClipData* clip = Encode(audio, format, 0.7f, error);
		EXPECT_NE(clip, nullptr) << error;
		return clip;
	}

	AudioClipData* Streamed(const AudioClipData* resident, u32 primeFrames = PRIME) {
		const u8* bytes = nullptr;
		u64 size = 0;
		GetPayload(*resident, bytes, size);
		AudioClipData* clip =
			CreateStreamedClipData(resident->encoding, bytes, size, resident->layout, resident->sampleRate, resident->frames, primeFrames);
		EXPECT_NE(clip, nullptr);
		return clip;
	}

	class StreamingMixer {
	public:
		explicit StreamingMixer(u32 aheadBlocks = 32) : _ahead(aheadBlocks) { _streamer.Start(64, false); }

		~StreamingMixer() {
			_streamer.Stop();
			for (AudioStream* stream : _streams) {
				DestroyStream(stream);
			}
		}

		StreamingMixer(const StreamingMixer&) = delete;
		StreamingMixer& operator=(const StreamingMixer&) = delete;

		AudioCommand Start(u32 slot, AudioClipData* clip, f32 pitch = 1.0f, bool loop = false, u64 at = 0, u64 frame = 0, bool virtual_ = false) {
			AudioCommand play = _mixer.Play(slot, clip, at, frame);
			play.pitch = pitch;
			play.loop = loop;
			play.flag = virtual_;
			EXPECT_TRUE(_mixer.Push(play));
			if (!virtual_) {
				Attach(play);
			}
			return play;
		}

		void Attach(const AudioCommand& play) {
			AudioClipData* clip = const_cast<AudioClipData*>(play.clip);
			if (clip->encoding != AudioClipEncoding::Streamed || clip->frames <= clip->primeFrames) {
				return;
			}
			AudioStream* stream = CreateStream(clip, _ahead * BLOCK);
			ASSERT_NE(stream, nullptr);
			_streams.push_back(stream);
			ASSERT_TRUE(_streamer.Add(stream));
			AudioCommand attach = OfflineMixer::To(play, AudioCommandKind::SetVoiceStream);
			attach.stream = stream;
			EXPECT_TRUE(_mixer.Push(attach));
		}

		std::vector<f32> Render(u32 blocks, bool pump = true) {
			std::vector<f32> out;
			out.reserve(static_cast<size_t>(blocks) * BLOCK * _mixer.GetChannels());
			for (u32 block = 0; block < blocks; ++block) {
				const std::vector<f32> rendered = _mixer.Render(BLOCK);
				out.insert(out.end(), rendered.begin(), rendered.end());
				if (pump) {
					_streamer.Pump();
				}
			}
			return out;
		}

		bool Push(const AudioCommand& command) { return _mixer.Push(command); }
		OfflineMixer& Mixer() { return _mixer; }
		u64 Underruns() { return _mixer.Get().GetCounters().streamUnderruns.load(); }
		u32 StreamsMade() const { return static_cast<u32>(_streams.size()); }
		const AudioStreamer& Streamer() const { return _streamer; }
		void Pump() { _streamer.Pump(); }
	private:
		AudioStreamer _streamer;
		OfflineMixer _mixer;
		std::vector<AudioStream*> _streams;
		u32 _ahead = 32;
	};

	i64 FirstDifference(const std::vector<f32>& a, const std::vector<f32>& b, size_t from = 0, size_t to = ~size_t{0}) {
		const size_t end = std::min({a.size(), b.size(), to});
		for (size_t index = from; index < end; ++index) {
			if (std::bit_cast<u32>(a[index]) != std::bit_cast<u32>(b[index])) {
				return static_cast<i64>(index);
			}
		}
		return a.size() == b.size() || to != ~size_t{0} ? -1 : static_cast<i64>(end);
	}

	size_t BlockStart(u32 block, u32 channels = 2) { return static_cast<size_t>(block) * BLOCK * channels; }

	bool IsSilent(const std::vector<f32>& samples, size_t from, size_t to) {
		for (size_t index = from; index < to; ++index) {
			if (samples[index] != 0.0f) {
				return false;
			}
		}
		return true;
	}

	bool FadesInFrom(const std::vector<f32>& streamed, const std::vector<f32>& resident, u32 block, u32 channels = 2) {
		const f32 step = 1.0f / static_cast<f32>(BLOCK);
		for (u32 frame = 0; frame < BLOCK; ++frame) {
			const f32 gain = 0.0f + step * static_cast<f32>(frame + 1);
			for (u32 channel = 0; channel < channels; ++channel) {
				const size_t index = BlockStart(block, channels) + static_cast<size_t>(frame) * channels + channel;
				if (std::bit_cast<u32>(streamed[index]) != std::bit_cast<u32>(resident[index] * gain)) {
					return false;
				}
			}
		}
		return true;
	}

} // anonymous namespace

TEST(AudioStreamTest, StreamedPcmMatchesResident) {
	const DecodedAudio audio = MakeSignal(150000, 2);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::PCM));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	EXPECT_EQ(streamed.Get()->primeFrames, 8192u);

	for (f32 pitch : {1.0f, 1.37f, 0.71f}) {
		StreamingMixer heard;
		StreamingMixer held;
		held.Start(0, const_cast<AudioClipData*>(resident.Get()), pitch);
		heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), pitch);
		const std::vector<f32> expected = held.Render(700);
		const std::vector<f32> actual = heard.Render(700);
		EXPECT_EQ(FirstDifference(actual, expected), -1) << "at pitch " << pitch;
		EXPECT_EQ(heard.Underruns(), 0u) << "at pitch " << pitch;
		EXPECT_EQ(heard.StreamsMade(), 1u);
	}
}

TEST(AudioStreamTest, StreamedQoaMatchesResident) {
	const DecodedAudio audio = MakeSignal(120000, 2, 2);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::QOA));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	EXPECT_EQ(streamed.Get()->primeFrames, 2u * QOA_FRAME_FRAMES);

	for (f32 pitch : {1.0f, 1.21f}) {
		StreamingMixer heard;
		StreamingMixer held;
		held.Start(0, const_cast<AudioClipData*>(resident.Get()), pitch);
		heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), pitch);
		const std::vector<f32> expected = held.Render(600);
		const std::vector<f32> actual = heard.Render(600);
		EXPECT_EQ(FirstDifference(actual, expected), -1) << "at pitch " << pitch;
		EXPECT_EQ(heard.Underruns(), 0u);
	}
}

TEST(AudioStreamTest, LoopingStreamWrapsThroughPrime) {
	for (AudioCompressionFormat format : {AudioCompressionFormat::PCM, AudioCompressionFormat::QOA}) {
		const DecodedAudio audio = MakeSignal(40000, 1, 3);
		ClipBlock resident(Resident(audio, format));
		ClipBlock streamed(Streamed(resident.Get()));
		ASSERT_TRUE(resident && streamed);
		StreamingMixer heard;
		StreamingMixer held;
		held.Start(0, const_cast<AudioClipData*>(resident.Get()), 1.5f, true);
		heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), 1.5f, true);
		const std::vector<f32> expected = held.Render(600);
		const std::vector<f32> actual = heard.Render(600);
		EXPECT_EQ(FirstDifference(actual, expected), -1) << "format " << static_cast<u32>(format);
		EXPECT_EQ(heard.Underruns(), 0u);
	}
}

TEST(AudioStreamTest, ReversedStreamMatchesResident) {
	const DecodedAudio audio = MakeSignal(60000, 2, 4);
	for (AudioCompressionFormat format : {AudioCompressionFormat::PCM, AudioCompressionFormat::QOA}) {
		ClipBlock resident(Resident(audio, format));
		ClipBlock streamed(Streamed(resident.Get()));
		ASSERT_TRUE(resident && streamed);
		for (f32 pitch : {-1.0f, -1.3f}) {
			StreamingMixer heard;
			StreamingMixer held;
			const u64 at = held.Mixer().Now() + 2 * BLOCK;
			held.Start(0, const_cast<AudioClipData*>(resident.Get()), pitch, true, at, 40000);
			heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), pitch, true, at, 40000);
			const std::vector<f32> expected = held.Render(500);
			const std::vector<f32> actual = heard.Render(500);
			EXPECT_EQ(FirstDifference(actual, expected), -1) << "format " << static_cast<u32>(format) << " at pitch " << pitch;
			EXPECT_EQ(heard.Underruns(), 0u);
		}
	}
}

TEST(AudioStreamTest, ReversedPitchReplaysHeld) {
	const DecodedAudio audio = MakeSignal(80000, 2, 12);
	for (AudioCompressionFormat format : {AudioCompressionFormat::PCM, AudioCompressionFormat::QOA}) {
		ClipBlock resident(Resident(audio, format));
		ClipBlock streamed(Streamed(resident.Get()));
		ASSERT_TRUE(resident && streamed);
		StreamingMixer heard;
		StreamingMixer held;
		const AudioCommand heldPlay = held.Start(0, const_cast<AudioClipData*>(resident.Get()), 1.0f, true);
		const AudioCommand heardPlay = heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), 1.0f, true);
		std::vector<f32> expected = held.Render(150);
		std::vector<f32> actual = heard.Render(150);
		ASSERT_EQ(FirstDifference(actual, expected), -1);
		held.Push(OfflineMixer::To(heldPlay, AudioCommandKind::SetPitch, -1.4f));
		heard.Push(OfflineMixer::To(heardPlay, AudioCommandKind::SetPitch, -1.4f));
		expected = held.Render(250);
		actual = heard.Render(250);
		EXPECT_EQ(FirstDifference(actual, expected), -1) << "format " << static_cast<u32>(format);
		EXPECT_EQ(heard.Underruns(), 0u);
	}
}

TEST(AudioStreamTest, VoicesStreamSeparately) {
	const DecodedAudio audio = MakeSignal(90000, 2, 5);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::QOA));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	StreamingMixer heard;
	StreamingMixer held;
	const u64 at = held.Mixer().Now() + 2 * BLOCK;
	for (StreamingMixer* mixer : {&held, &heard}) {
		AudioClipData* clip = const_cast<AudioClipData*>(mixer == &held ? resident.Get() : streamed.Get());
		mixer->Start(0, clip, 1.0f, false, at, 0);
		mixer->Start(1, clip, 0.9f, true, at, 50000);
	}
	const std::vector<f32> expected = held.Render(400);
	const std::vector<f32> actual = heard.Render(400);
	EXPECT_EQ(FirstDifference(actual, expected), -1);
	EXPECT_EQ(heard.StreamsMade(), 2u);
	EXPECT_EQ(heard.Underruns(), 0u);
}

TEST(AudioStreamTest, ShortClipNeedsNoStream) {
	const DecodedAudio audio = MakeSignal(6000, 1, 6);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::PCM));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	EXPECT_EQ(streamed.Get()->primeFrames, streamed.Get()->frames);
	StreamingMixer heard;
	StreamingMixer held;
	held.Start(0, const_cast<AudioClipData*>(resident.Get()), 1.1f, true);
	heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), 1.1f, true);
	EXPECT_EQ(FirstDifference(heard.Render(200), held.Render(200)), -1);
	EXPECT_EQ(heard.StreamsMade(), 0u);
}

namespace {

	struct OpusClip {
		ClipBlock cooked;
		ClipBlock decoded;

		explicit OpusClip(const DecodedAudio& audio)
			: cooked(Resident(audio, AudioCompressionFormat::Opus)), decoded(cooked ? DecodeToPcm(*cooked.Get()) : nullptr) {}

		explicit operator bool() const { return cooked && decoded; }
		AudioClipData* Decoded() const { return const_cast<AudioClipData*>(decoded.Get()); }
	};

	f64 Snr(const std::vector<f32>& actual, const std::vector<f32>& expected, size_t from, size_t to) {
		f64 signal = 0.0;
		f64 noise = 0.0;
		for (size_t index = from; index < to; ++index) {
			const f64 want = expected[index];
			const f64 error = static_cast<f64>(actual[index]) - want;
			signal += want * want;
			noise += error * error;
		}
		return noise == 0.0 ? 1000.0 : 10.0 * std::log10(signal / noise);
	}

} // anonymous namespace

TEST(AudioStreamTest, StreamedOpusMatchesDecode) {
	const DecodedAudio audio = MakeSignal(150000, 2, 30);
	OpusClip clip(audio);
	ASSERT_TRUE(clip);
	ClipBlock streamed(Streamed(clip.cooked.Get()));
	ASSERT_TRUE(streamed);
	EXPECT_EQ(streamed.Get()->primeFrames, 2u * OPUS_CHUNK_FRAMES);
	for (f32 pitch : {1.0f, 1.37f, 0.71f}) {
		StreamingMixer heard;
		StreamingMixer held;
		held.Start(0, clip.Decoded(), pitch);
		heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), pitch);
		const std::vector<f32> expected = held.Render(700);
		const std::vector<f32> actual = heard.Render(700);
		EXPECT_EQ(FirstDifference(actual, expected), -1) << "at pitch " << pitch;
		EXPECT_EQ(heard.Underruns(), 0u);
		EXPECT_EQ(heard.Streamer().GetFailures(), 0u);
	}
}

TEST(AudioStreamTest, OpusLoopMatchesDecode) {
	for (u32 channels : {1u, 2u}) {
		const DecodedAudio audio = MakeSignal(40000, channels, 31);
		OpusClip clip(audio);
		ASSERT_TRUE(clip);
		ClipBlock streamed(Streamed(clip.cooked.Get()));
		ASSERT_TRUE(streamed);
		StreamingMixer heard;
		StreamingMixer held;
		held.Start(0, clip.Decoded(), 1.5f, true);
		heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), 1.5f, true);
		EXPECT_EQ(FirstDifference(heard.Render(600), held.Render(600)), -1) << channels << " channels";
		EXPECT_EQ(heard.Underruns(), 0u);
	}
}

TEST(AudioStreamTest, OpusSeekConvergesInPreRoll) {
	const DecodedAudio audio = MakeSignal(100000, 2, 32);
	OpusClip clip(audio);
	ASSERT_TRUE(clip);
	ClipBlock streamed(Streamed(clip.cooked.Get()));
	ASSERT_TRUE(streamed);
	StreamingMixer heard;
	StreamingMixer held;
	const AudioCommand heldPlay = held.Start(0, clip.Decoded());
	const AudioCommand heardPlay = heard.Start(0, const_cast<AudioClipData*>(streamed.Get()));
	ASSERT_EQ(FirstDifference(heard.Render(20), held.Render(20)), -1);

	AudioCommand heldSeek = OfflineMixer::To(heldPlay, AudioCommandKind::Seek);
	heldSeek.frame = 70000;
	AudioCommand heardSeek = OfflineMixer::To(heardPlay, AudioCommandKind::Seek);
	heardSeek.frame = 70000;
	held.Push(heldSeek);
	heard.Push(heardSeek);
	const std::vector<f32> expected = held.Render(40);
	const std::vector<f32> actual = heard.Render(40);
	EXPECT_TRUE(IsSilent(actual, BlockStart(0), BlockStart(1)));
	const size_t chunkEnd = BlockStart(2) + static_cast<size_t>(OPUS_CHUNK_FRAMES) * 2;
	EXPECT_GT(Snr(actual, expected, BlockStart(2), chunkEnd), 60.0);
	EXPECT_GT(Snr(actual, expected, chunkEnd, BlockStart(40)), 90.0);
	EXPECT_EQ(heard.Underruns(), 0u);
}

TEST(AudioStreamTest, ReversedOpusConverges) {
	const DecodedAudio audio = MakeSignal(60000, 2, 33);
	OpusClip clip(audio);
	ASSERT_TRUE(clip);
	ClipBlock streamed(Streamed(clip.cooked.Get()));
	ASSERT_TRUE(streamed);
	StreamingMixer heard;
	StreamingMixer held;
	const u64 at = held.Mixer().Now() + 2 * BLOCK;
	held.Start(0, clip.Decoded(), -1.0f, true, at, 40000);
	heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), -1.0f, true, at, 40000);
	const std::vector<f32> expected = held.Render(400);
	const std::vector<f32> actual = heard.Render(400);
	EXPECT_GT(Snr(actual, expected, BlockStart(6), actual.size()), 45.0);
	EXPECT_EQ(heard.Underruns(), 0u);
}

TEST(AudioStreamTest, SeekPastPrimeHeardBlockLate) {
	const DecodedAudio audio = MakeSignal(100000, 1, 7);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::PCM));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	StreamingMixer heard;
	StreamingMixer held;
	const AudioCommand heldPlay = held.Start(0, const_cast<AudioClipData*>(resident.Get()));
	const AudioCommand heardPlay = heard.Start(0, const_cast<AudioClipData*>(streamed.Get()));
	std::vector<f32> expected = held.Render(20);
	std::vector<f32> actual = heard.Render(20);
	ASSERT_EQ(FirstDifference(actual, expected), -1);

	AudioCommand heldSeek = OfflineMixer::To(heldPlay, AudioCommandKind::Seek);
	heldSeek.frame = 70000;
	AudioCommand heardSeek = OfflineMixer::To(heardPlay, AudioCommandKind::Seek);
	heardSeek.frame = 70000;
	held.Push(heldSeek);
	heard.Push(heardSeek);
	expected = held.Render(12);
	actual = heard.Render(12);
	EXPECT_TRUE(IsSilent(actual, BlockStart(0), BlockStart(1)));
	EXPECT_FALSE(IsSilent(expected, BlockStart(0), BlockStart(1)));
	EXPECT_TRUE(FadesInFrom(actual, expected, 1));
	EXPECT_EQ(FirstDifference(actual, expected, BlockStart(2)), -1);
	EXPECT_EQ(heard.Underruns(), 0u);
}

TEST(AudioStreamTest, SeekIntoPrimeIsExact) {
	const DecodedAudio audio = MakeSignal(90000, 2, 14);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::QOA));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	StreamingMixer heard;
	StreamingMixer held;
	const AudioCommand heldPlay = held.Start(0, const_cast<AudioClipData*>(resident.Get()));
	const AudioCommand heardPlay = heard.Start(0, const_cast<AudioClipData*>(streamed.Get()));
	std::vector<f32> expected = held.Render(120);
	std::vector<f32> actual = heard.Render(120);
	ASSERT_EQ(FirstDifference(actual, expected), -1);
	AudioCommand heldSeek = OfflineMixer::To(heldPlay, AudioCommandKind::Seek);
	heldSeek.frame = 1000;
	AudioCommand heardSeek = OfflineMixer::To(heardPlay, AudioCommandKind::Seek);
	heardSeek.frame = 1000;
	held.Push(heldSeek);
	heard.Push(heardSeek);
	expected = held.Render(150);
	actual = heard.Render(150);
	EXPECT_EQ(FirstDifference(actual, expected), -1);
	EXPECT_EQ(heard.Underruns(), 0u);
}

TEST(AudioStreamTest, HeardVoiceStreamsFromClock) {
	const DecodedAudio audio = MakeSignal(100000, 2, 8);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::QOA));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	StreamingMixer heard;
	StreamingMixer held;
	const AudioCommand heldPlay = held.Start(0, const_cast<AudioClipData*>(resident.Get()), 1.0f, false, 0, 0, true);
	const AudioCommand heardPlay = heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), 1.0f, false, 0, 0, true);
	EXPECT_TRUE(IsSilent(heard.Render(60), 0, BlockStart(60)));
	held.Render(60);
	EXPECT_EQ(heard.StreamsMade(), 0u);

	AudioCommand heldReal = OfflineMixer::To(heldPlay, AudioCommandKind::SetVoiceReal);
	heldReal.flag = true;
	AudioCommand heardReal = OfflineMixer::To(heardPlay, AudioCommandKind::SetVoiceReal);
	heardReal.flag = true;
	held.Push(heldReal);
	heard.Attach(heardPlay);
	heard.Push(heardReal);
	const std::vector<f32> expected = held.Render(20);
	const std::vector<f32> actual = heard.Render(20);
	EXPECT_TRUE(IsSilent(actual, BlockStart(0), BlockStart(1)));
	EXPECT_TRUE(FadesInFrom(actual, expected, 1));
	EXPECT_EQ(FirstDifference(actual, expected, BlockStart(2)), -1);
	EXPECT_EQ(heard.Underruns(), 0u);
}

TEST(AudioStreamTest, LoopSwitchKeepsBuffer) {
	const DecodedAudio audio = MakeSignal(30000, 1, 9);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::PCM));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	StreamingMixer heard;
	StreamingMixer held;
	const AudioCommand heldPlay = held.Start(0, const_cast<AudioClipData*>(resident.Get()));
	const AudioCommand heardPlay = heard.Start(0, const_cast<AudioClipData*>(streamed.Get()));
	std::vector<f32> expected = held.Render(52);
	std::vector<f32> actual = heard.Render(52);
	ASSERT_EQ(FirstDifference(actual, expected), -1);
	held.Push(OfflineMixer::To(heldPlay, AudioCommandKind::SetLoop, 1.0f));
	heard.Push(OfflineMixer::To(heardPlay, AudioCommandKind::SetLoop, 1.0f));
	expected = held.Render(300);
	actual = heard.Render(300);
	EXPECT_EQ(FirstDifference(actual, expected), -1);
	EXPECT_EQ(heard.Underruns(), 0u);
}

TEST(AudioStreamTest, DeviceReopenedMidStreamKeepsPlace) {
	const DecodedAudio audio = MakeSignal(80000, 2, 10);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::PCM));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	StreamingMixer heard;
	StreamingMixer held;
	held.Start(0, const_cast<AudioClipData*>(resident.Get()), 1.25f, true);
	heard.Start(0, const_cast<AudioClipData*>(streamed.Get()), 1.25f, true);
	std::vector<f32> expected = held.Render(70);
	std::vector<f32> actual = heard.Render(70);
	ASSERT_EQ(FirstDifference(actual, expected), -1);
	for (StreamingMixer* mixer : {&held, &heard}) {
		mixer->Mixer().Get().Release();
		ASSERT_TRUE(mixer->Mixer().Get().Prepare(MakeStandardLayout(2), 48000));
	}
	expected = held.Render(200);
	actual = heard.Render(200);
	EXPECT_EQ(FirstDifference(actual, expected), -1);
	EXPECT_EQ(heard.Underruns(), 0u);
}

TEST(AudioStreamTest, StarvedVoiceRecovers) {
	const DecodedAudio audio = MakeSignal(200000, 1, 11);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::PCM));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	StreamingMixer heard(4);
	StreamingMixer held;
	held.Start(0, const_cast<AudioClipData*>(resident.Get()));
	heard.Start(0, const_cast<AudioClipData*>(streamed.Get()));
	std::vector<f32> expected = held.Render(40);
	std::vector<f32> actual = heard.Render(40);
	ASSERT_EQ(FirstDifference(actual, expected), -1);
	ASSERT_EQ(heard.Underruns(), 0u);

	expected = held.Render(200);
	actual = heard.Render(200, false);
	EXPECT_TRUE(IsSilent(actual, BlockStart(199), BlockStart(200)));
	const u64 starved = heard.Underruns();
	EXPECT_GT(starved, 0u);
	EXPECT_LE(starved, 200u);
	EXPECT_EQ(heard.Mixer().Get().GetVoicePosition(0), held.Mixer().Get().GetVoicePosition(0));

	expected = held.Render(40);
	actual = heard.Render(40);
	u32 back = 40;
	for (u32 block = 0; block < 40; ++block) {
		if (!IsSilent(actual, BlockStart(block), BlockStart(block + 1))) {
			back = block;
			break;
		}
	}
	ASSERT_LT(back, 4u);
	EXPECT_TRUE(FadesInFrom(actual, expected, back));
	EXPECT_EQ(FirstDifference(actual, expected, BlockStart(back + 1)), -1);
}

TEST(AudioStreamTest, ProfilerUnderrunsMatchRender) {
	const DecodedAudio audio = MakeSignal(200000, 1, 11);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::PCM));
	ClipBlock streamed(Streamed(resident.Get()));
	ASSERT_TRUE(resident && streamed);
	StreamingMixer heard(4);
	heard.Start(0, const_cast<AudioClipData*>(streamed.Get()));
	heard.Render(40);
	heard.Render(200, false);
	ASSERT_GT(heard.Underruns(), 0u);

	AudioProfilerOps::Counts counts;
	AudioProfilerOps::ReadCounts(heard.Mixer().Get().GetCounters(), heard.Streamer().GetFailures(), AudioDeviceEvents{}, counts);
	EXPECT_EQ(counts.streamUnderruns, heard.Underruns());
	EXPECT_EQ(counts.streamFailures, 0u);
}

namespace {

	std::filesystem::path WritePayloadFile(const char* name, const AudioClipData& clip, u64 offset, bool corrupt = false) {
		const u8* bytes = nullptr;
		u64 size = 0;
		GetPayload(clip, bytes, size);
		Buffer file(static_cast<size_t>(offset + size), MemTag::Audio);
		std::memset(file.data, 0x5A, static_cast<size_t>(offset));
		std::memcpy(file.data + offset, bytes, static_cast<size_t>(size));
		if (corrupt) {
			file.data[offset + QoaFrameOffset(clip.channels, 6)] ^= 0x01;
		}
		const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
		EXPECT_TRUE(FileSystem::WriteFile(Path(path.generic_string()), file));
		return path;
	}

} // anonymous namespace

TEST(AudioStreamTest, FileStreamHoldsOnlyPrime) {
	for (AudioCompressionFormat format : {AudioCompressionFormat::PCM, AudioCompressionFormat::QOA}) {
		const DecodedAudio audio = MakeSignal(100000, 2, 20);
		ClipBlock resident(Resident(audio, format));
		ASSERT_TRUE(resident);
		const std::filesystem::path path = WritePayloadFile("shalltear_stream_located.bin", *resident.Get(), 1234);
		const u8* bytes = nullptr;
		u64 size = 0;
		GetPayload(*resident.Get(), bytes, size);
		PakPayloadLocator locator;
		locator.physicalPath = path.generic_string();
		locator.offset = 1234;
		locator.size = size;
		{
			ClipBlock located(CreateLocatedClipData(resident.Get()->encoding, locator, resident.Get()->layout, 48000, resident.Get()->frames, PRIME));
			ASSERT_TRUE(located);
			const AudioClipData& data = *located.Get();
			EXPECT_TRUE(data.source.file.IsOpen());
			EXPECT_EQ(data.source.memory, nullptr);
			EXPECT_EQ(data.payloadBytes, data.primeFrames * 2 * sizeof(f32));
			Buffer over(16, MemTag::Audio);
			EXPECT_FALSE(FileSystem::WriteFile(Path(locator.physicalPath), over));

			StreamingMixer heard;
			StreamingMixer held;
			held.Start(0, const_cast<AudioClipData*>(resident.Get()), 1.13f, true);
			heard.Start(0, const_cast<AudioClipData*>(located.Get()), 1.13f, true);
			EXPECT_EQ(FirstDifference(heard.Render(600), held.Render(600)), -1) << "format " << static_cast<u32>(format);
			EXPECT_EQ(heard.Underruns(), 0u);
			EXPECT_EQ(heard.Streamer().GetFailures(), 0u);
		}
		std::error_code ec;
		EXPECT_TRUE(std::filesystem::remove(path, ec));
	}
}

TEST(AudioStreamTest, CorruptFramePlaysSilence) {
	const DecodedAudio audio = MakeSignal(60000, 2, 21);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::QOA));
	ASSERT_TRUE(resident);
	const std::filesystem::path path = WritePayloadFile("shalltear_stream_corrupt.bin", *resident.Get(), 0, true);
	const u8* bytes = nullptr;
	u64 size = 0;
	GetPayload(*resident.Get(), bytes, size);
	PakPayloadLocator locator;
	locator.physicalPath = path.generic_string();
	locator.size = size;
	{
		ClipBlock located(CreateLocatedClipData(AudioClipEncoding::Qoa, locator, resident.Get()->layout, 48000, resident.Get()->frames, PRIME));
		ASSERT_TRUE(located);
		StreamingMixer heard;
		StreamingMixer held;
		held.Start(0, const_cast<AudioClipData*>(resident.Get()));
		heard.Start(0, const_cast<AudioClipData*>(located.Get()));
		const std::vector<f32> expected = held.Render(200);
		const std::vector<f32> actual = heard.Render(200);
		const size_t from = static_cast<size_t>(6) * QOA_FRAME_FRAMES * 2;
		const size_t to = static_cast<size_t>(7) * QOA_FRAME_FRAMES * 2;
		EXPECT_EQ(FirstDifference(actual, expected, 0, from), -1);
		EXPECT_TRUE(IsSilent(actual, from, to));
		EXPECT_FALSE(IsSilent(expected, from, to));
		EXPECT_EQ(FirstDifference(actual, expected, to), -1);
		EXPECT_EQ(heard.Streamer().GetFailures(), 1u);
		EXPECT_EQ(heard.Underruns(), 0u);
	}
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST(AudioStreamTest, OpusFileStreamPlaysDecode) {
	const DecodedAudio audio = MakeSignal(100000, 2, 34);
	OpusClip clip(audio);
	ASSERT_TRUE(clip);
	const AudioClipData& cooked = *clip.cooked.Get();
	const std::filesystem::path path = WritePayloadFile("shalltear_stream_opus.bin", cooked, 777);
	PakPayloadLocator locator;
	locator.physicalPath = path.generic_string();
	locator.offset = 777;
	locator.size = cooked.source.size;
	{
		ClipBlock located(CreateLocatedClipData(AudioClipEncoding::Opus, locator, cooked.layout, OPUS_RATE, cooked.frames, PRIME));
		ASSERT_TRUE(located);
		const AudioClipData& data = *located.Get();
		EXPECT_EQ(data.source.memory, nullptr);
		EXPECT_EQ(data.payloadBytes, data.primeFrames * 2 * sizeof(f32) + OpusOffsetBytes(data.frames));

		StreamingMixer heard;
		StreamingMixer held;
		held.Start(0, clip.Decoded(), 1.13f, true);
		heard.Start(0, const_cast<AudioClipData*>(located.Get()), 1.13f, true);
		EXPECT_EQ(FirstDifference(heard.Render(600), held.Render(600)), -1);
		EXPECT_EQ(heard.Underruns(), 0u);
		EXPECT_EQ(heard.Streamer().GetFailures(), 0u);
	}
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST(AudioStreamTest, BadOpusPacketIsSilence) {
	const DecodedAudio audio = MakeSignal(60000, 2, 35);
	OpusClip clip(audio);
	ASSERT_TRUE(clip);
	const AudioClipData& cooked = *clip.cooked.Get();
	const std::filesystem::path path = WritePayloadFile("shalltear_stream_opus_bad.bin", cooked, 0);
	{
		Buffer file = FileSystem::ReadFile(Path(path.generic_string()), MemTag::Audio);
		ASSERT_TRUE(file);
		const u64 bad = 1 + 6 * OPUS_CHUNK_PACKETS;
		file.data[cooked.opus.offsets[bad]] = static_cast<u8>((30u << 3) | (1u << 2));
		ASSERT_TRUE(FileSystem::WriteFile(Path(path.generic_string()), file));
	}
	PakPayloadLocator locator;
	locator.physicalPath = path.generic_string();
	locator.size = cooked.source.size;
	{
		ClipBlock located(CreateLocatedClipData(AudioClipEncoding::Opus, locator, cooked.layout, OPUS_RATE, cooked.frames, PRIME));
		ASSERT_TRUE(located);
		StreamingMixer heard;
		StreamingMixer held;
		held.Start(0, clip.Decoded());
		heard.Start(0, const_cast<AudioClipData*>(located.Get()));
		const std::vector<f32> expected = held.Render(200);
		const std::vector<f32> actual = heard.Render(200);
		const size_t from = static_cast<size_t>(6) * OPUS_CHUNK_FRAMES * 2;
		const size_t to = static_cast<size_t>(7) * OPUS_CHUNK_FRAMES * 2;
		EXPECT_EQ(FirstDifference(actual, expected, 0, from), -1);
		EXPECT_TRUE(IsSilent(actual, from, to));
		EXPECT_FALSE(IsSilent(expected, from, to));
		const size_t after = to + static_cast<size_t>(OPUS_CHUNK_FRAMES) * 2;
		EXPECT_GT(Snr(actual, expected, to, after), 25.0);
		EXPECT_GT(Snr(actual, expected, after, actual.size()), 55.0);
		EXPECT_EQ(heard.Streamer().GetFailures(), 1u);
		EXPECT_EQ(heard.Underruns(), 0u);
	}
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST(AudioStreamTest, MismatchedPayloadRefused) {
	const DecodedAudio audio = MakeSignal(30000, 1, 22);
	ClipBlock resident(Resident(audio, AudioCompressionFormat::QOA));
	ASSERT_TRUE(resident);
	const AudioClipData& clip = *resident.Get();
	const std::filesystem::path path = WritePayloadFile("shalltear_stream_refused.bin", clip, 64);
	const u8* bytes = nullptr;
	u64 size = 0;
	GetPayload(clip, bytes, size);
	auto locate = [&](u64 offset, u64 bytesAt, u64 frames, const std::filesystem::path& where) {
		PakPayloadLocator locator;
		locator.physicalPath = where.generic_string();
		locator.offset = offset;
		locator.size = bytesAt;
		AudioClipData* data = CreateLocatedClipData(AudioClipEncoding::Qoa, locator, clip.layout, 48000, frames, PRIME);
		const bool made = data != nullptr;
		ReleaseClipData(data);
		return made;
	};
	EXPECT_TRUE(locate(64, size, clip.frames, path));
	EXPECT_FALSE(locate(64, size - 1, clip.frames, path));
	ASSERT_EQ(QoaFileSize(1, clip.frames - 1), size);
	EXPECT_FALSE(locate(64, size, clip.frames - 1, path));
	EXPECT_FALSE(locate(65, size, clip.frames, path));
	EXPECT_FALSE(locate(0, size, clip.frames, path));
	EXPECT_FALSE(locate(64, size, clip.frames, std::filesystem::temp_directory_path() / "shalltear_stream_nothing_here.bin"));
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

namespace {

	class StreamLiveFixture : public LivePackageFixture {
	protected:
		str ProjectSettings() const override { return str("[Audio]\ndevice = None\nmaxRealVoices = 1\n"); }

		static u32 Streams() {
			GetAudioSystem().Pump();
			return GetAudioSystem().CountStreams();
		}
	};

} // anonymous namespace

TEST_F(StreamLiveFixture, StreamFollowsHearing) {
	AudioClip* first = MakeClip(48000 * 3, 2, 0.25f, AudioCompressionFormat::QOA, AudioClipLoadType::Streaming);
	AudioClip* second = MakeClip(48000 * 3, 2, 0.5f, AudioCompressionFormat::PCM, AudioClipLoadType::Streaming);
	ASSERT_EQ(first->GetLoadType(), AudioClipLoadType::Streaming);

	AudioPlayParams quiet;
	quiet.priority = 200;
	const AudioVoice a = Audio::Play(ResourceOf(first), quiet);
	ASSERT_TRUE(a.IsValid());
	EXPECT_EQ(GetAudioSystem().CountStreams(), 1u);

	AudioPlayParams loud;
	loud.priority = 10;
	const AudioVoice b = Audio::Play(ResourceOf(second), loud);
	ASSERT_TRUE(b.IsValid());
	EXPECT_TRUE(Audio::IsVirtual(a));
	EXPECT_TRUE(WaitFor([]() { return Streams() == 1u; }));

	Audio::Stop(b);
	EXPECT_TRUE(WaitFor([&]() {
		RunFrame();
		return !Audio::IsVirtual(a) && Streams() == 1u;
	}));
	Audio::Stop(a);
	EXPECT_TRUE(WaitFor([]() {
		RunFrame();
		return Streams() == 0u;
	}));

	const AudioVoice c = Audio::Play(ResourceOf(first));
	ASSERT_TRUE(c.IsValid());
	ASSERT_TRUE(WaitForBlocks(200));
	EXPECT_GT(Audio::GetTimeSamples(c), 8192u);
	EXPECT_EQ(GetAudioSystem().GetRenderer().GetCounters().streamUnderruns.load(), 0u);
	EXPECT_EQ(GetAudioSystem().GetStreamer().GetFailures(), 0u);
}

namespace {

	class StreamedClipImporter : public IAssetImporter {
	public:
		int imports = 0;
		StringHash GetAssetType() const override { return AudioClip::GetReflectType()->name; }
		Asset* Import(const Path& sourcePath) override {
			++imports;
			const str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
			DecodedAudio audio = MakeSignal(90000, 2, static_cast<u32>(text.size()));
			const str extension = sourcePath.GetExtension();
			AudioImportSettings settings;
			settings.loadType = extension == ".opusmemory" ? AudioClipLoadType::CompressedInMemory : AudioClipLoadType::Streaming;
			settings.compressionFormat = extension == ".streamclip" ? AudioCompressionFormat::QOA : AudioCompressionFormat::Opus;
			AudioClip* clip = New<AudioClip>(MemTag::Asset);
			str error;
			if (!BuildClip(audio, settings, *clip, error)) {
				Delete(clip);
				return nullptr;
			}
			return clip;
		}
	};

	class LocatedClipFixture : public LivePackageFixture {
	protected:
		std::filesystem::path _project;
		StreamedClipImporter _importer;

		void SetUp() override {
			LivePackageFixture::SetUp();
			_project = _root / "project";
			std::filesystem::create_directories(_project / "Assets");
			FileSystem::Mount("project", Path(_project.generic_string()));
			AssetImporterRegistry::Register(_importer.GetAssetType(), &_importer, {".streamclip", ".opusclip", ".opusmemory"});
			AssetDatabase::Initialize();
			ImportArtifactStore::SetCostThresholdMicros(0);
		}

		void TearDown() override {
			ImportArtifactStore::SetCostThresholdMicros(IMPORT_ARTIFACT_COST_THRESHOLD_US);
			AssetDatabase::Terminate();
			AssetImporterRegistry::Unregister(&_importer);
			FileSystem::Unmount("project");
			LivePackageFixture::TearDown();
		}

		void Restart() {
			AssetDatabase::Terminate();
			AssetDatabase::Initialize();
		}

		static AudioClip* Load(const char* virtualPath) { return static_cast<AudioClip*>(AssetDatabase::LoadAsset(Path(virtualPath))); }

		static AudioClipData* ResidentOf(const AudioClip& clip) {
			const AudioClipData* cooked = AudioClipAccess::GetCooked(clip);
			const u8* bytes = nullptr;
			u64 size = 0;
			GetPayload(*cooked, bytes, size);
			if (cooked->codec != AudioClipEncoding::Opus) {
				return CreateQoaClipData(bytes, size, cooked->layout);
			}
			AudioClipData* packets = CreateOpusClipData(bytes, size, cooked->layout, cooked->frames);
			AudioClipData* decoded = packets ? DecodeToPcm(*packets) : nullptr;
			ReleaseClipData(packets);
			return decoded;
		}
	};

} // anonymous namespace

TEST_F(LocatedClipFixture, PackagedClipStreamsFromPackage) {
	FileSystem::WriteTextFile(Path((_project / "Assets/Theme.streamclip").generic_string()), "a theme");
	AudioClip* imported = Load("project://Assets/Theme.streamclip");
	ASSERT_NE(imported, nullptr);
	ASSERT_EQ(imported->GetLoadType(), AudioClipLoadType::Streaming);
	const Guid guid = imported->assetId;
	ClipBlock resident(ResidentOf(*imported));
	ASSERT_TRUE(resident);

	PakStringPool strings;
	CookOps::PakAssetSource source;
	str error;
	ASSERT_TRUE(CookOps::CookAsset(*imported, strings, source, error)) << error;
	EXPECT_EQ(source.residency, PakResidency::Streamed);
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	sources.push_back(source);
	CookOps::PakWriteDesc desc;
	desc.compression = CookOps::CookCompression::LZ4;
	desc.strings.assign(strings.Texts().begin(), strings.Texts().end());
	const Path package((_root / "Content.slpak").generic_string());
	ASSERT_TRUE(CookOps::WritePackage(package, sources, desc, error)) << error;

	Restart();
	ASSERT_TRUE(AssetDatabase::MountPackage(package, error)) << error;
	AudioClip* cooked = static_cast<AudioClip*>(AssetDatabase::LoadAssetByGuid(guid));
	ASSERT_NE(cooked, nullptr);
	const AudioClipData* data = AudioClipAccess::GetPlayback(*cooked);
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(data->encoding, AudioClipEncoding::Streamed);
	EXPECT_TRUE(data->source.file.IsOpen());
	EXPECT_EQ(data->payloadBytes, data->primeFrames * 2 * sizeof(f32));
	{
		StreamingMixer heard;
		StreamingMixer held;
		held.Start(0, const_cast<AudioClipData*>(resident.Get()), 0.87f);
		heard.Start(0, const_cast<AudioClipData*>(data), 0.87f);
		EXPECT_EQ(FirstDifference(heard.Render(500), held.Render(500)), -1);
		EXPECT_EQ(heard.Underruns(), 0u);
	}

	PakStringPool again;
	CookOps::PakAssetSource recooked;
	ASSERT_TRUE(CookOps::CookAsset(*cooked, again, recooked, error)) << error;
	EXPECT_TRUE(recooked.descriptor == source.descriptor);
	EXPECT_TRUE(recooked.payload == source.payload);
}

TEST_F(LocatedClipFixture, ArtifactClipStreamsFromArtifact) {
	FileSystem::WriteTextFile(Path((_project / "Assets/Line.streamclip").generic_string()), "a line of dialogue");
	AudioClip* imported = Load("project://Assets/Line.streamclip");
	ASSERT_NE(imported, nullptr);
	const Guid guid = imported->assetId;
	ClipBlock resident(ResidentOf(*imported));
	ASSERT_TRUE(resident);
	const Path artifact = FileSystem::Resolve(ImportArtifactPath(guid));
	ASSERT_TRUE(FileSystem::Exists(artifact));

	Restart();
	std::error_code ec;
	const std::filesystem::path source = _project / "Assets/Line.streamclip";
	std::filesystem::last_write_time(source, std::filesystem::file_time_type::clock::now() + std::chrono::hours(1), ec);
	ASSERT_FALSE(ec);
	const u64 moved = FileSystem::GetLastWriteTime(Path(source.generic_string()));

	AudioClip* served = Load("project://Assets/Line.streamclip");
	ASSERT_NE(served, nullptr);
	EXPECT_EQ(_importer.imports, 1);
	const AudioClipData* data = AudioClipAccess::GetPlayback(*served);
	ASSERT_NE(data, nullptr);
	EXPECT_TRUE(data->source.file.IsOpen());
	EXPECT_EQ(data->source.memory, nullptr);

	Buffer bytes = FileSystem::ReadFile(artifact, MemTag::Asset);
	ImportArtifactView view;
	ASSERT_TRUE(ImportArtifactRead(bytes.data, bytes.size, view));
	EXPECT_EQ(view.header.sourceMtime, moved);
	Buffer over(16, MemTag::Asset);
	EXPECT_FALSE(FileSystem::WriteFile(artifact, over));

	StreamingMixer heard;
	StreamingMixer held;
	held.Start(0, const_cast<AudioClipData*>(resident.Get()), 1.0f, true);
	heard.Start(0, const_cast<AudioClipData*>(data), 1.0f, true);
	EXPECT_EQ(FirstDifference(heard.Render(500), held.Render(500)), -1);
	EXPECT_EQ(heard.Underruns(), 0u);
}

TEST_F(LocatedClipFixture, PackagedOpusStreams) {
	FileSystem::WriteTextFile(Path((_project / "Assets/Score.opusclip").generic_string()), "a score");
	FileSystem::WriteTextFile(Path((_project / "Assets/Bark.opusmemory").generic_string()), "a bark");
	AudioClip* streaming = Load("project://Assets/Score.opusclip");
	AudioClip* inMemory = Load("project://Assets/Bark.opusmemory");
	ASSERT_NE(streaming, nullptr);
	ASSERT_NE(inMemory, nullptr);
	ASSERT_EQ(streaming->GetCompressionFormat(), AudioCompressionFormat::Opus);
	ASSERT_EQ(inMemory->GetLoadType(), AudioClipLoadType::CompressedInMemory);
	const Guid guids[2] = {streaming->assetId, inMemory->assetId};
	ClipBlock residents[2] = {ClipBlock(ResidentOf(*streaming)), ClipBlock(ResidentOf(*inMemory))};
	ASSERT_TRUE(residents[0] && residents[1]);

	PakStringPool strings;
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	for (AudioClip* clip : {streaming, inMemory}) {
		CookOps::PakAssetSource source;
		str error;
		ASSERT_TRUE(CookOps::CookAsset(*clip, strings, source, error)) << error;
		sources.push_back(source);
	}
	EXPECT_EQ(sources[0].residency, PakResidency::Streamed);
	EXPECT_NE(sources[1].residency, PakResidency::Streamed);
	CookOps::PakWriteDesc desc;
	desc.compression = CookOps::CookCompression::LZ4;
	desc.strings.assign(strings.Texts().begin(), strings.Texts().end());
	const Path package((_root / "Opus.slpak").generic_string());
	str error;
	ASSERT_TRUE(CookOps::WritePackage(package, sources, desc, error)) << error;

	Restart();
	ASSERT_TRUE(AssetDatabase::MountPackage(package, error)) << error;
	for (u32 index = 0; index < 2; ++index) {
		AudioClip* cooked = static_cast<AudioClip*>(AssetDatabase::LoadAssetByGuid(guids[index]));
		ASSERT_NE(cooked, nullptr);
		const AudioClipData* data = AudioClipAccess::GetPlayback(*cooked);
		ASSERT_NE(data, nullptr);
		EXPECT_EQ(data->encoding, AudioClipEncoding::Streamed);
		EXPECT_EQ(data->codec, AudioClipEncoding::Opus);
		if (index == 0) {
			EXPECT_TRUE(data->source.file.IsOpen());
			EXPECT_EQ(data->payloadBytes, data->primeFrames * 2 * sizeof(f32) + OpusOffsetBytes(data->frames));
		} else {
			EXPECT_NE(data->source.memory, nullptr);
		}
		StreamingMixer heard;
		StreamingMixer held;
		held.Start(0, const_cast<AudioClipData*>(residents[index].Get()), 1.0f, true);
		heard.Start(0, const_cast<AudioClipData*>(data), 1.0f, true);
		EXPECT_EQ(FirstDifference(heard.Render(500), held.Render(500)), -1) << (index == 0 ? "streaming" : "compressed in memory");
		EXPECT_EQ(heard.Underruns(), 0u);
		EXPECT_EQ(heard.Streamer().GetFailures(), 0u);
	}
}

TEST_F(LocatedClipFixture, StreamedWaveformMatchesImport) {
	for (const char* name : {"project://Assets/Wave.streamclip", "project://Assets/Wave.opusclip"}) {
		const std::filesystem::path file = _project / "Assets" / Path(name).GetFilename();
		FileSystem::WriteTextFile(Path(file.generic_string()), "a waveform");
		AudioClip* imported = Load(name);
		ASSERT_NE(imported, nullptr) << name;
		ASSERT_EQ(imported->GetLoadType(), AudioClipLoadType::Streaming);
		AudioEnvelopeBuilder fromBytes;
		ASSERT_TRUE(fromBytes.Begin(*imported, 40));
		ASSERT_TRUE(fromBytes.Advance(~0ull)) << name;

		Restart();
		AudioClip* served = Load(name);
		ASSERT_NE(served, nullptr);
		ASSERT_TRUE(AudioClipAccess::GetCooked(*served)->source.file.IsOpen());
		AudioEnvelopeBuilder fromFile;
		ASSERT_TRUE(fromFile.Begin(*served, 40));
		ASSERT_TRUE(fromFile.Advance(~0ull)) << name;
		EXPECT_EQ(fromFile.Get().low, fromBytes.Get().low) << name;
		EXPECT_EQ(fromFile.Get().high, fromBytes.Get().high) << name;
		Restart();
	}
}

#endif
