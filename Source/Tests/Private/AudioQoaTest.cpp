#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioQoa.h"

#include "ThirdParty/qoa.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace Shalltear;
using namespace Shalltear::AudioImpl;

extern "C" void SltAudioQoaFree(void* block);

namespace {

	std::vector<i16> MakeSignal(u32 frames, u32 channels, u32 seed) {
		std::vector<i16> samples(static_cast<size_t>(frames) * channels);
		u32 state = seed * 2654435761u + 1u;
		for (u32 frame = 0; frame < frames; ++frame) {
			for (u32 channel = 0; channel < channels; ++channel) {
				state = state * 1664525u + 1013904223u;
				const f64 noise = static_cast<f64>(static_cast<i32>(state >> 16) - 32768) / 32768.0;
				const f64 tone = std::sin(static_cast<f64>(frame) * 0.013 * static_cast<f64>(channel + 1));
				const f64 value = (0.6 * tone + 0.1 * noise) * 32767.0;
				samples[static_cast<size_t>(frame) * channels + channel] = static_cast<i16>(value);
			}
		}
		return samples;
	}

	struct Encoded {
		std::vector<u8> bytes;
		std::vector<i16> reference;
	};

	Encoded EncodeWithReference(const std::vector<i16>& samples, u32 frames, u32 channels, u32 rate) {
		Encoded encoded;
		qoa_desc description;
		std::memset(&description, 0, sizeof(description));
		description.channels = channels;
		description.samplerate = rate;
		description.samples = frames;
		unsigned int size = 0;
		void* bytes = qoa_encode(samples.data(), &description, &size);
		if (!bytes) {
			return encoded;
		}
		encoded.bytes.assign(static_cast<const u8*>(bytes), static_cast<const u8*>(bytes) + size);
		SltAudioQoaFree(bytes);

		qoa_desc decodedDescription;
		std::memset(&decodedDescription, 0, sizeof(decodedDescription));
		short* decoded = qoa_decode(encoded.bytes.data(), static_cast<int>(encoded.bytes.size()), &decodedDescription);
		if (decoded) {
			encoded.reference.assign(decoded, decoded + static_cast<size_t>(frames) * channels);
			SltAudioQoaFree(decoded);
		}
		return encoded;
	}

	f32 AsFloat(i16 sample) { return static_cast<f32>(sample) * (1.0f / 32768.0f); }

} // namespace

TEST(AudioQoaTest, DecodeMatchesReference) {
	for (u32 channels : {1u, 2u, 5u, 8u}) {
		for (u32 frames : {1u, 19u, 20u, 5119u, 5120u, 5121u, 12345u}) {
			const std::vector<i16> signal = MakeSignal(frames, channels, frames + channels);
			const Encoded encoded = EncodeWithReference(signal, frames, channels, 44100);
			ASSERT_FALSE(encoded.bytes.empty()) << channels << " channels, " << frames << " frames";
			ASSERT_EQ(encoded.reference.size(), static_cast<size_t>(frames) * channels);

			QoaStream stream;
			ASSERT_TRUE(QoaOpen(encoded.bytes.data(), encoded.bytes.size(), stream)) << channels << " channels, " << frames << " frames";
			EXPECT_EQ(stream.channels, channels);
			EXPECT_EQ(stream.sampleRate, 44100u);
			EXPECT_EQ(stream.frames, frames);

			std::vector<f32> ours(static_cast<size_t>(frames) * channels, 99.0f);
			QoaDecodeAll(stream, ours.data());
			u32 mismatches = 0;
			for (size_t index = 0; index < ours.size(); ++index) {
				if (ours[index] != AsFloat(encoded.reference[index])) {
					++mismatches;
				}
			}
			EXPECT_EQ(mismatches, 0u) << channels << " channels, " << frames << " frames";
		}
	}
}

TEST(AudioQoaTest, CursorReadsReferenceWhereverVoiceMovesIt) {
	const u32 channels = 2;
	const u32 frames = 5120 * 3 + 777;
	const std::vector<i16> signal = MakeSignal(frames, channels, 7);
	const Encoded encoded = EncodeWithReference(signal, frames, channels, 48000);
	QoaStream stream;
	ASSERT_TRUE(QoaOpen(encoded.bytes.data(), encoded.bytes.size(), stream));

	std::vector<u64> path;
	for (u64 index = 0; index < 9000; ++index) {
		path.push_back(index);
	}
	for (u64 index = 100; index < 140; ++index) {
		path.push_back(index - 1);
		path.push_back(index);
		path.push_back(index + 1);
		path.push_back(index + 2);
	}
	path.push_back(0);
	path.push_back(5120 * 2 + 1234);
	path.push_back(5120 * 2 + 1233);
	for (u64 index = 5120 * 2 + 400; index > 5120 + 4000; --index) {
		path.push_back(index);
	}
	path.push_back(frames - 1);

	QoaCursor cursor;
	f32 out[2];
	u32 mismatches = 0;
	for (u64 index : path) {
		ASSERT_TRUE(QoaRead(stream, cursor, index, out)) << "index " << index;
		for (u32 channel = 0; channel < channels; ++channel) {
			if (out[channel] != AsFloat(encoded.reference[index * channels + channel])) {
				++mismatches;
			}
		}
	}
	EXPECT_EQ(mismatches, 0u);

	out[0] = out[1] = 5.0f;
	EXPECT_FALSE(QoaRead(stream, cursor, frames, out));
	EXPECT_EQ(out[0], 0.0f);
	EXPECT_EQ(out[1], 0.0f);
}

TEST(AudioQoaTest, OpenRefusesOverreadingStream) {
	const u32 channels = 2;
	const u32 frames = 5120 + 300;
	const std::vector<i16> signal = MakeSignal(frames, channels, 3);
	const Encoded encoded = EncodeWithReference(signal, frames, channels, 22050);
	QoaStream stream;
	ASSERT_TRUE(QoaOpen(encoded.bytes.data(), encoded.bytes.size(), stream));

	std::vector<u8> badMagic = encoded.bytes;
	badMagic[0] = 'x';
	EXPECT_FALSE(QoaOpen(badMagic.data(), badMagic.size(), stream));

	std::vector<u8> truncated(encoded.bytes.begin(), encoded.bytes.end() - 8);
	EXPECT_FALSE(QoaOpen(truncated.data(), truncated.size(), stream));

	std::vector<u8> trailing = encoded.bytes;
	trailing.push_back(0);
	EXPECT_FALSE(QoaOpen(trailing.data(), trailing.size(), stream));

	std::vector<u8> unknownLength = encoded.bytes;
	std::memset(unknownLength.data() + 4, 0, 4);
	EXPECT_FALSE(QoaOpen(unknownLength.data(), unknownLength.size(), stream));

	const size_t secondFrame = 8 + (8 + 16 * channels + 8 * 256 * channels);
	std::vector<u8> liar = encoded.bytes;
	liar[secondFrame] = 1;
	EXPECT_FALSE(QoaOpen(liar.data(), liar.size(), stream));

	EXPECT_FALSE(QoaOpen(encoded.bytes.data(), 15, stream));
	EXPECT_FALSE(QoaOpen(nullptr, 0, stream));
}

TEST(AudioQoaTest, EveryFrameDecodedOnIsReferencesFrame) {
	for (u32 channels : {1u, 2u, 6u}) {
		const u32 frames = 5120 * 3 + 4321;
		const std::vector<i16> signal = MakeSignal(frames, channels, 30 + channels);
		const Encoded encoded = EncodeWithReference(signal, frames, channels, 48000);
		ASSERT_FALSE(encoded.bytes.empty());
		u32 mismatches = 0;
		for (u64 frame = 0; frame * QOA_FRAME_FRAMES < frames; ++frame) {
			const u32 first = static_cast<u32>(frame * QOA_FRAME_FRAMES);
			const u32 count = frames - first < QOA_FRAME_FRAMES ? frames - first : QOA_FRAME_FRAMES;
			const u64 offset = QoaFrameOffset(channels, frame);
			const u64 size = QoaFrameSize(channels, count);
			ASSERT_LE(offset + size, encoded.bytes.size()) << "frame " << frame << " of " << channels << " channels";
			std::vector<f32> ours(static_cast<size_t>(count) * channels, 99.0f);
			ASSERT_TRUE(QoaDecodeFrame(encoded.bytes.data() + offset, size, channels, 48000, count, ours.data())) << "frame " << frame;
			for (size_t index = 0; index < ours.size(); ++index) {
				mismatches += ours[index] != AsFloat(encoded.reference[static_cast<size_t>(first) * channels + index]) ? 1u : 0u;
			}
		}
		EXPECT_EQ(mismatches, 0u) << channels << " channels";
	}
}

TEST(AudioQoaTest, MismatchedFrameRefused) {
	const u32 channels = 2;
	const u32 frames = 5120 * 2;
	const std::vector<i16> signal = MakeSignal(frames, channels, 40);
	const Encoded encoded = EncodeWithReference(signal, frames, channels, 48000);
	const u64 offset = QoaFrameOffset(channels, 1);
	const u64 size = QoaFrameSize(channels, QOA_FRAME_FRAMES);
	std::vector<f32> out(static_cast<size_t>(QOA_FRAME_FRAMES) * channels, 7.0f);
	auto untouched = [&]() {
		for (f32 sample : out) {
			if (sample != 7.0f) {
				return false;
			}
		}
		return true;
	};
	const u8* bytes = encoded.bytes.data() + offset;
	EXPECT_FALSE(QoaDecodeFrame(bytes, size, 1, 48000, QOA_FRAME_FRAMES, out.data()));
	EXPECT_FALSE(QoaDecodeFrame(bytes, size, channels, 44100, QOA_FRAME_FRAMES, out.data()));
	EXPECT_FALSE(QoaDecodeFrame(bytes, size, channels, 48000, QOA_FRAME_FRAMES - 20, out.data()));
	EXPECT_FALSE(QoaDecodeFrame(bytes, size - 8, channels, 48000, QOA_FRAME_FRAMES, out.data()));
	std::vector<u8> lying(bytes, bytes + size);
	lying[7] ^= 0x10;
	EXPECT_FALSE(QoaDecodeFrame(lying.data(), size, channels, 48000, QOA_FRAME_FRAMES, out.data()));
	EXPECT_TRUE(untouched());
	EXPECT_TRUE(QoaDecodeFrame(bytes, size, channels, 48000, QOA_FRAME_FRAMES, out.data()));
	EXPECT_FALSE(untouched());
}

#endif
