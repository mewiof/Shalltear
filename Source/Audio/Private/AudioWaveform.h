#pragma once

#include "AudioVoiceInternals.h"

#include "AudioClipData.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

struct OpusMSDecoder;

namespace Shalltear {

	class AudioClip;

	namespace AudioImpl {

		struct AudioEnvelope {
			u32 channels = 0;
			u32 spans = 0;
			u64 frames = 0;
			u64 framesRead = 0;
			Vector<f32> low{GetMemoryResource(MemTag::Audio)};
			Vector<f32> high{GetMemoryResource(MemTag::Audio)};

			bool IsComplete() const { return frames != 0 && framesRead >= frames; }
		};

		void GetEnvelopeColumns(const AudioEnvelope& envelope, u32 channel, u32 columns, f32* low, f32* high);

		class AudioEnvelopeBuilder {
		public:
			AudioEnvelopeBuilder() = default;
			~AudioEnvelopeBuilder();
			AudioEnvelopeBuilder(const AudioEnvelopeBuilder&) = delete;
			AudioEnvelopeBuilder& operator=(const AudioEnvelopeBuilder&) = delete;

			bool Begin(const AudioClip& clip, u32 spans);

			bool Advance(u64 frames);

			void Release();

			bool IsFinished() const { return _finished; }
			const AudioEnvelope& Get() const { return _envelope; }
		private:
			void Fold(const f32* samples, u64 first, u64 count);

			AudioEnvelope _envelope;
			AudioClipData* _data = nullptr;
			AudioClipEncoding _codec = AudioClipEncoding::Pcm;
			const AudioStreamSource* _source = nullptr;
			AudioStreamSource _resident;
			bool _finished = false;
			u64 _next = 0;
			Vector<f32> _decoded{GetMemoryResource(MemTag::Audio)};
			Vector<u8> _bytes{GetMemoryResource(MemTag::Audio)};
			OpusMSDecoder* _opus = nullptr;
		};

	} // namespace AudioImpl

} // namespace Shalltear
