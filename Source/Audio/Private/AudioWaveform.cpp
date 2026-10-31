#include "AudioWaveform.h"

#include "AudioClipAccess.h"
#include "AudioOpus.h"
#include "AudioQoa.h"

#include "Shalltear/Audio/AudioClip.h"

namespace Shalltear::AudioImpl {

	namespace {

		constexpr u64 PCM_READ_FRAMES = 4096;

	} // anonymous namespace

	void GetEnvelopeColumns(const AudioEnvelope& envelope, u32 channel, u32 columns, f32* low, f32* high) {
		const u64 spans = envelope.spans;
		for (u32 column = 0; column < columns; ++column) {
			low[column] = 0.0f;
			high[column] = 0.0f;
		}
		if (spans == 0 || channel >= envelope.channels || columns == 0) {
			return;
		}
		const f32* lows = envelope.low.data() + static_cast<size_t>(channel) * spans;
		const f32* highs = envelope.high.data() + static_cast<size_t>(channel) * spans;
		for (u32 column = 0; column < columns; ++column) {
			const u64 first = static_cast<u64>(column) * spans / columns;
			u64 last = static_cast<u64>(column + 1) * spans / columns;
			last = last > first ? last : first + 1;
			for (u64 span = first; span < last && span < spans; ++span) {
				low[column] = lows[span] < low[column] ? lows[span] : low[column];
				high[column] = highs[span] > high[column] ? highs[span] : high[column];
			}
		}
	}

	AudioEnvelopeBuilder::~AudioEnvelopeBuilder() {
		Release();
	}

	void AudioEnvelopeBuilder::Release() {
		if (_opus) {
			Free(_opus);
			_opus = nullptr;
		}
		if (_data) {
			ReleaseClipData(_data);
			_data = nullptr;
		}
		_source = nullptr;
		_resident = AudioStreamSource{};
	}

	bool AudioEnvelopeBuilder::Begin(const AudioClip& clip, u32 spans) {
		Release();
		_envelope = AudioEnvelope{};
		_finished = false;
		_next = 0;
		AudioClipData* cooked = AudioClipAccess::GetCooked(clip);
		if (!cooked || spans == 0 || cooked->frames == 0 || cooked->channels == 0) {
			return false;
		}

		const AudioClipEncoding codec = cooked->encoding == AudioClipEncoding::Streamed ? cooked->codec : cooked->encoding;
		switch (cooked->encoding) {
		case AudioClipEncoding::Streamed:
		case AudioClipEncoding::Opus:
			_source = &cooked->source;
			break;
		case AudioClipEncoding::Qoa:
			_resident.memory = cooked->qoa.bytes;
			_resident.size = cooked->qoa.size;
			_source = &_resident;
			break;
		case AudioClipEncoding::Pcm:
			_resident.memory = reinterpret_cast<const u8*>(cooked->pcm);
			_resident.size = cooked->frames * cooked->channels * sizeof(f32);
			_source = &_resident;
			break;
		default:
			return false;
		}

		if (codec == AudioClipEncoding::Opus) {
			const u64 decoderBytes = OpusDecoderBytes(cooked->opus.layout);
			if (decoderBytes == 0) {
				return false;
			}
			_opus = static_cast<OpusMSDecoder*>(Allocate(static_cast<size_t>(decoderBytes), MemTag::Audio, 16));
			_bytes.resize(cooked->opus.largestRun);
			_decoded.resize(static_cast<size_t>(OPUS_CHUNK_FRAMES) * cooked->channels);
			if (!InitOpusDecoder(_opus, cooked->channels, cooked->opus.layout) ||
				!DecodeOpusPackets(_opus, *_source, cooked->opus, cooked->channels, 0, 1, _decoded.data(), false, _bytes.data(), _bytes.size())) {
				Release();
				return false;
			}
			_next = 1;
		} else if (codec == AudioClipEncoding::Qoa) {
			_bytes.resize(static_cast<size_t>(QoaFrameSize(cooked->channels, QOA_FRAME_FRAMES)));
			_decoded.resize(static_cast<size_t>(QOA_FRAME_FRAMES) * cooked->channels);
		} else {
			_decoded.resize(static_cast<size_t>(PCM_READ_FRAMES) * cooked->channels);
		}

		RetainClipData(cooked);
		_data = cooked;
		_codec = codec;
		_envelope.channels = cooked->channels;
		_envelope.spans = spans;
		_envelope.frames = cooked->frames;
		_envelope.low.assign(static_cast<size_t>(spans) * cooked->channels, 0.0f);
		_envelope.high.assign(static_cast<size_t>(spans) * cooked->channels, 0.0f);
		return true;
	}

	bool AudioEnvelopeBuilder::Advance(u64 frames) {
		if (!_data || _finished) {
			return _envelope.IsComplete();
		}
		const u32 channels = _envelope.channels;
		const u64 total = _envelope.frames;
		u64 read = 0;
		bool failed = false;
		while (read < frames && _envelope.framesRead < total && !failed) {
			const u64 first = _envelope.framesRead;
			u64 count = 0;
			if (_codec == AudioClipEncoding::Opus) {
				const u64 to = _next + OPUS_CHUNK_PACKETS < _data->opus.packets ? _next + OPUS_CHUNK_PACKETS : _data->opus.packets;
				failed = to <= _next || !DecodeOpusPackets(_opus, *_source, _data->opus, channels, _next, to, _decoded.data(), true,
														   _bytes.data(), _bytes.size());
				count = (to - _next) * OPUS_PACKET_FRAMES;
				_next = to;
			} else if (_codec == AudioClipEncoding::Qoa) {
				const u64 left = total - first;
				const u32 samples = static_cast<u32>(left < QOA_FRAME_FRAMES ? left : QOA_FRAME_FRAMES);
				const u64 size = QoaFrameSize(channels, samples);
				failed = !ReadStreamSource(*_source, QoaFrameOffset(channels, _next), _bytes.data(), size) ||
						 !QoaDecodeFrame(_bytes.data(), size, channels, _data->sampleRate, samples, _decoded.data());
				count = samples;
				++_next;
			} else {
				const u64 left = total - first;
				count = left < PCM_READ_FRAMES ? left : PCM_READ_FRAMES;
				failed = !ReadStreamSource(*_source, first * channels * sizeof(f32), reinterpret_cast<u8*>(_decoded.data()),
										   count * channels * sizeof(f32));
			}
			if (failed) {
				break;
			}
			count = first + count > total ? total - first : count;
			Fold(_decoded.data(), first, count);
			read += count;
		}
		if (failed || _envelope.framesRead >= total) {
			_finished = true;
		}
		return _envelope.IsComplete();
	}

	void AudioEnvelopeBuilder::Fold(const f32* samples, u64 first, u64 count) {
		const u32 channels = _envelope.channels;
		const u64 spans = _envelope.spans;
		const u64 total = _envelope.frames;
		for (u64 index = 0; index < count; ++index) {
			const size_t span = static_cast<size_t>((first + index) * spans / total);
			for (u32 channel = 0; channel < channels; ++channel) {
				const f32 sample = samples[index * channels + channel];
				const size_t at = channel * static_cast<size_t>(spans) + span;
				_envelope.low[at] = sample < _envelope.low[at] ? sample : _envelope.low[at];
				_envelope.high[at] = sample > _envelope.high[at] ? sample : _envelope.high[at];
			}
		}
		_envelope.framesRead = first + count;
	}

} // namespace Shalltear::AudioImpl
