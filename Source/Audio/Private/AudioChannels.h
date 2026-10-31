#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioSpatializer.h"

#include "Shalltear/Core/Types.h"

namespace Shalltear {

	namespace AudioImpl {

		inline constexpr u32 MAX_AUDIO_CHANNELS = AUDIO_MAX_CHANNELS;

		using SpeakerPosition = AudioSpeaker;

		struct ChannelLayout {
			u32 count = 0;
			SpeakerPosition positions[MAX_AUDIO_CHANNELS]{};
		};

		ChannelLayout MakeStandardLayout(u32 channels);

		void ComputeChannelGains(const ChannelLayout& in, const ChannelLayout& out, f32 stereoPan, f32* gains) [[clang::nonblocking]];

	} // namespace AudioImpl

} // namespace Shalltear
