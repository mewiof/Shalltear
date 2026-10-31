#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "FuzzHarness.h"

#include "AudioClipData.h"
#include "AudioImportOps.h"
#include "AudioOfflineMixer.h"
#include "AudioOpus.h"
#include "Realtime.h"

#include <cmath>
#include <vector>

using namespace AudioTesting;

namespace {

	using P = SpeakerPosition;

	ChannelLayout Layout(std::initializer_list<P> positions) {
		ChannelLayout layout;
		for (P position : positions) {
			layout.positions[layout.count++] = position;
		}
		return layout;
	}

	DecodedAudio Tones(u64 frames, u32 channels, ChannelLayout layout = {}) {
		DecodedAudio audio;
		audio.layout = layout.count ? layout : MakeStandardLayout(channels);
		audio.sampleRate = OPUS_RATE;
		audio.samples.resize(static_cast<size_t>(frames * channels));
		for (u64 frame = 0; frame < frames; ++frame) {
			for (u32 channel = 0; channel < channels; ++channel) {
				const f64 hz = 220.0 * static_cast<f64>(channel + 2);
				audio.samples[static_cast<size_t>(frame * channels + channel)] =
					static_cast<f32>(0.5 * std::sin(2.0 * 3.14159265358979 * hz * static_cast<f64>(frame) / OPUS_RATE));
			}
		}
		return audio;
	}

	AudioClipData* EncodeOpus(const DecodedAudio& audio, f32 quality = 0.7f) {
		str error;
		AudioClipData* clip = Encode(audio, AudioCompressionFormat::Opus, quality, error);
		EXPECT_NE(clip, nullptr) << error;
		return clip;
	}

	f64 SnrAtLag(const DecodedAudio& source, const f32* decoded, u64 frames, i64 lag) {
		const u32 channels = source.layout.count;
		f64 signal = 0.0;
		f64 noise = 0.0;
		for (u64 frame = 2 * OPUS_PACKET_FRAMES; frame + 2 * OPUS_PACKET_FRAMES < frames; ++frame) {
			const i64 from = static_cast<i64>(frame) - lag;
			for (u32 channel = 0; channel < channels; ++channel) {
				const f64 want = source.samples[static_cast<size_t>(from) * channels + channel];
				const f64 got = decoded[frame * channels + channel];
				signal += want * want;
				noise += (got - want) * (got - want);
			}
		}
		return 10.0 * std::log10(signal / (noise > 0.0 ? noise : 1e-30));
	}

} // anonymous namespace

TEST(AudioOpusTest, EachLeftSpeakerIsCoupledWithRight) {
	const OpusStreamLayout mono = MakeOpusStreamLayout(Layout({P::Mono}));
	EXPECT_EQ(mono.streams, 1u);
	EXPECT_EQ(mono.coupled, 0u);

	const OpusStreamLayout stereo = MakeOpusStreamLayout(Layout({P::FrontLeft, P::FrontRight}));
	EXPECT_EQ(stereo.streams, 1u);
	EXPECT_EQ(stereo.coupled, 1u);
	EXPECT_EQ(stereo.mapping[0], 0u);
	EXPECT_EQ(stereo.mapping[1], 1u);

	const OpusStreamLayout surround =
		MakeOpusStreamLayout(Layout({P::FrontLeft, P::FrontRight, P::FrontCenter, P::LowFrequency, P::BackLeft, P::BackRight}));
	EXPECT_EQ(surround.streams, 4u);
	EXPECT_EQ(surround.coupled, 2u);
	const u8 expected[] = {0, 1, 4, 5, 2, 3};
	for (u32 channel = 0; channel < 6; ++channel) {
		EXPECT_EQ(surround.mapping[channel], expected[channel]) << "channel " << channel;
	}

	const OpusStreamLayout reversed = MakeOpusStreamLayout(Layout({P::SideRight, P::FrontCenter, P::SideLeft, P::BackLeft}));
	EXPECT_EQ(reversed.coupled, 1u);
	EXPECT_EQ(reversed.streams, 3u);
	EXPECT_EQ(reversed.mapping[2], 0u);
	EXPECT_EQ(reversed.mapping[0], 1u);
	EXPECT_EQ(reversed.mapping[1], 2u);
	EXPECT_EQ(reversed.mapping[3], 3u);
}

TEST(AudioOpusTest, QualityCodesOnTheOwnersLogCurve) {
	EXPECT_EQ(OpusBitrate(0.0f, 1), 6000u);
	EXPECT_EQ(OpusBitrate(1.0f, 1), 128000u);
	EXPECT_EQ(OpusBitrate(1.0f, 2), 256000u);
	EXPECT_NEAR(static_cast<f64>(OpusBitrate(0.7f, 2)), 102200.0, 400.0);
	EXPECT_NEAR(static_cast<f64>(OpusBitrate(0.5f, 1)), std::sqrt(6000.0 * 128000.0), 2.0);
	EXPECT_EQ(OpusBitrate(-1.0f, 1), OpusBitrate(0.0f, 1));
	EXPECT_EQ(OpusBitrate(3.0f, 1), OpusBitrate(1.0f, 1));

	const DecodedAudio audio = Tones(3 * OPUS_RATE, 2);
	for (f32 quality : {0.2f, 0.7f, 1.0f}) {
		ClipBlock clip(EncodeOpus(audio, quality));
		ASSERT_TRUE(clip);
		const f64 seconds = static_cast<f64>(OpusPacketCount(audio.GetFrames()) * OPUS_PACKET_FRAMES) / OPUS_RATE;
		const f64 packetBytes = static_cast<f64>(clip.Get()->source.size - OPUS_HEADER_BYTES - OpusPacketCount(audio.GetFrames()) * 2);
		const f64 bitrate = packetBytes * 8.0 / seconds;
		EXPECT_NEAR(bitrate / static_cast<f64>(OpusBitrate(quality, 2)), 1.0, 0.1) << "at quality " << quality;
	}
}

TEST(AudioOpusTest, DecodeLinesUpWithSourceToFrame) {
	for (u32 channels : {1u, 2u, 6u}) {
		const DecodedAudio audio = Tones(OPUS_RATE, channels);
		ClipBlock cooked(EncodeOpus(audio, 1.0f));
		ASSERT_TRUE(cooked);
		EXPECT_EQ(cooked.Get()->frames, audio.GetFrames());
		EXPECT_EQ(cooked.Get()->sampleRate, OPUS_RATE);
		EXPECT_EQ(cooked.Get()->opus.packets, OpusPacketCount(audio.GetFrames()));
		ClipBlock decoded(DecodeToPcm(*cooked.Get()));
		ASSERT_TRUE(decoded);
		const u64 frames = decoded.Get()->frames;
		const f64 aligned = SnrAtLag(audio, decoded.Get()->pcm, frames, 0);
		EXPECT_GT(aligned, 20.0) << channels;
		for (i64 lag : {-312, -120, -1, 1, 120, 312}) {
			EXPECT_GT(aligned, SnrAtLag(audio, decoded.Get()->pcm, frames, lag) + 3.0) << channels << ' ' << lag << " frames off";
		}
	}
}

TEST(AudioOpusTest, APayloadThatContradictsItselfIsRefused) {
	const DecodedAudio audio = Tones(OPUS_RATE / 4, 2);
	ClipBlock cooked(EncodeOpus(audio));
	ASSERT_TRUE(cooked);
	const AudioClipData& clip = *cooked.Get();
	const std::vector<u8> good(clip.source.memory, clip.source.memory + clip.source.size);
	const u64 frames = clip.frames;

	auto loads = [&](const std::vector<u8>& bytes, u64 withFrames) {
		AudioClipData* data = CreateOpusClipData(bytes.data(), bytes.size(), clip.layout, withFrames);
		const bool made = data != nullptr;
		ReleaseClipData(data);
		return made;
	};
	auto corrupt = [&](auto&& change) {
		std::vector<u8> bytes = good;
		change(bytes);
		return loads(bytes, frames);
	};
	const size_t table = OPUS_HEADER_BYTES;

	EXPECT_TRUE(loads(good, frames));
	EXPECT_FALSE(corrupt([](std::vector<u8>& b) { b[0] = 0; }));
	EXPECT_FALSE(corrupt([](std::vector<u8>& b) { b[1] = 2; }));
	EXPECT_FALSE(corrupt([](std::vector<u8>& b) { b[0] = 2; }));
	EXPECT_FALSE(corrupt([](std::vector<u8>& b) { b[3] = b[2]; }));
	EXPECT_FALSE(corrupt([](std::vector<u8>& b) { b[3] = 7; }));
	EXPECT_FALSE(corrupt([](std::vector<u8>& b) { b[15] = 1; }));
	EXPECT_FALSE(corrupt([&](std::vector<u8>& b) { b[table] = 0, b[table + 1] = 0; }));
	EXPECT_FALSE(corrupt([&](std::vector<u8>& b) { b[table] = 0xFF, b[table + 1] = 0xFF; }));
	EXPECT_FALSE(corrupt([](std::vector<u8>& b) { b.push_back(0); }));
	EXPECT_FALSE(corrupt([](std::vector<u8>& b) { b.pop_back(); }));
	EXPECT_FALSE(corrupt([](std::vector<u8>& b) { b.resize(OPUS_HEADER_BYTES + 3); }));
	EXPECT_FALSE(loads(good, frames + OPUS_PACKET_FRAMES));

	auto oneStereoPacket = [](u32 size) {
		std::vector<u8> bytes(OPUS_HEADER_BYTES + 4, 0);
		bytes[0] = 1;
		bytes[1] = 1;
		bytes[3] = 1;
		bytes[OPUS_HEADER_BYTES] = static_cast<u8>(size & 0xFFu);
		bytes[OPUS_HEADER_BYTES + 1] = static_cast<u8>(size >> 8);
		bytes[OPUS_HEADER_BYTES + 2] = 1;
		bytes.resize(bytes.size() + size + 1, 0x08);
		return bytes;
	};
	EXPECT_TRUE(loads(oneStereoPacket(OpusMaxPacketBytes(1)), OPUS_PACKET_FRAMES));
	EXPECT_FALSE(loads(oneStereoPacket(OpusMaxPacketBytes(1) + 1), OPUS_PACKET_FRAMES));
}

TEST(AudioOpusTest, HostilePacketStaysInFrames) {
	for (u32 channels : {2u, 6u}) {
		const DecodedAudio audio = Tones(OPUS_RATE / 2, channels);
		ClipBlock cooked(EncodeOpus(audio));
		ASSERT_TRUE(cooked);
		const AudioClipData& clip = *cooked.Get();
		const u32 largest = OpusMaxPacketBytes(clip.opus.layout.streams);

		Buffer decoderMemory(static_cast<size_t>(OpusDecoderBytes(clip.opus.layout)), MemTag::Audio);
		OpusMSDecoder* decoder = reinterpret_cast<OpusMSDecoder*>(decoderMemory.data);
		ASSERT_TRUE(InitOpusDecoder(decoder, channels, clip.opus.layout));
		Fuzz::GuardedInput input(2 * largest);
		Fuzz::GuardedInput output(static_cast<size_t>(OPUS_PACKET_FRAMES) * channels * sizeof(f32));
		ASSERT_TRUE(input.IsValid() && output.IsValid());
		const std::vector<u8> zeros(static_cast<size_t>(OPUS_PACKET_FRAMES) * channels * sizeof(f32), 0);

		Fuzz::Rng rng(0x0B05u + channels);
		u32 decoded = 0;
		u32 refused = 0;
		for (u32 round = 0; round < 3000; ++round) {
			const u64 packet = 1 + rng.Below(static_cast<u32>(clip.opus.packets - 1));
			const u8* original = clip.source.memory + clip.opus.offsets[packet];
			std::vector<u8> bytes(original, original + (clip.opus.offsets[packet + 1] - clip.opus.offsets[packet]));
			switch (rng.Below(5)) {
			case 0:
				for (u32 flip = 1 + rng.Below(8); flip > 0; --flip) {
					bytes[rng.Below(static_cast<u32>(bytes.size()))] ^= static_cast<u8>(1u << rng.Below(8));
				}
				break;
			case 1:
				bytes.resize(1 + rng.Below(static_cast<u32>(bytes.size())));
				break;
			case 2:
				bytes[0] = rng.Byte();
				break;
			case 3:
				for (u8& byte : bytes) {
					byte = rng.Byte();
				}
				break;
			default:
				bytes.resize(1 + rng.Below(2 * largest));
				for (u8& byte : bytes) {
					byte = rng.Byte();
				}
				break;
			}
			const u8* placed = input.Place(bytes.data(), bytes.size());
			f32* out = reinterpret_cast<f32*>(const_cast<u8*>(output.Place(zeros.data(), zeros.size(), alignof(f32))));
			const i32 frames = Realtime::OpusDecode(decoder, placed, static_cast<u32>(bytes.size()), out, OPUS_PACKET_FRAMES);
			ASSERT_LE(frames, static_cast<i32>(OPUS_PACKET_FRAMES));
			if (frames == static_cast<i32>(OPUS_PACKET_FRAMES)) {
				++decoded;
				for (u32 sample = 0; sample < OPUS_PACKET_FRAMES * channels; ++sample) {
					ASSERT_TRUE(std::isfinite(out[sample])) << "round " << round;
				}
			} else {
				++refused;
			}
			if (rng.OneIn(50)) {
				ASSERT_TRUE(InitOpusDecoder(decoder, channels, clip.opus.layout));
			}
		}
		EXPECT_GT(decoded, 300u);
		EXPECT_GT(refused, 300u);
	}
}

TEST(AudioOpusTest, HostilePayloadStaysInBytes) {
	const DecodedAudio audio = Tones(OPUS_RATE / 8, 2);
	ClipBlock cooked(EncodeOpus(audio));
	ASSERT_TRUE(cooked);
	const AudioClipData& clip = *cooked.Get();
	const std::vector<u8> good(clip.source.memory, clip.source.memory + clip.source.size);
	std::vector<u32> offsets(static_cast<size_t>(OpusOffsetBytes(clip.frames) / sizeof(u32)));
	std::vector<f32> out(static_cast<size_t>(clip.frames) * 2);
	Fuzz::GuardedInput input(good.size() + 256);
	ASSERT_TRUE(input.IsValid());

	Fuzz::Rng rng(0x0B06u);
	u32 accepted = 0;
	u32 refused = 0;
	for (u32 round = 0; round < 2000; ++round) {
		std::vector<u8> bytes = good;
		for (u32 change = 1 + rng.Below(6); change > 0; --change) {
			const u32 where = rng.Below(8);
			const u32 packetsAt = static_cast<u32>(OPUS_HEADER_BYTES + clip.opus.packets * 2);
			if (where == 0) {
				bytes[rng.Below(OPUS_HEADER_BYTES)] = rng.Byte();
			} else if (where == 1) {
				bytes[OPUS_HEADER_BYTES + rng.Below(static_cast<u32>(clip.opus.packets * 2))] ^= static_cast<u8>(1u << rng.Below(8));
			} else if (where == 2) {
				bytes[rng.Below(static_cast<u32>(bytes.size()))] = rng.Byte();
			} else if (where == 3) {
				bytes.resize(1 + rng.Below(static_cast<u32>(bytes.size() + 64)), rng.Byte());
			} else if (bytes.size() > packetsAt) {
				bytes[packetsAt + rng.Below(static_cast<u32>(bytes.size() - packetsAt))] = rng.Byte();
			}
		}
		AudioStreamSource source;
		source.memory = input.Place(bytes.data(), bytes.size());
		source.size = bytes.size();
		AudioOpusIndex index;
		if (!ReadOpusIndex(source, 2, clip.frames, offsets.data(), index)) {
			++refused;
			continue;
		}
		++accepted;
		DecodeOpusHead(source, index, 2, clip.frames, out.data());
	}
	EXPECT_GT(accepted, 100u);
	EXPECT_GT(refused, 100u);
}

#endif
