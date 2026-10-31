#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioSpatializer.h"

#include "AudioChannels.h"

namespace Shalltear {

	namespace AudioImpl {

		inline constexpr f32 OCCLUSION_CUTOFF_HZ = 1000.0f;
		inline constexpr f32 OCCLUSION_GAIN = 0.25f;

		struct PanLayout {
			u32 channels = 0;
			bool stereo = false;
			i32 left = -1;
			i32 right = -1;
			u32 ringCount = 0;
			u32 ring[MAX_AUDIO_CHANNELS]{};
			f32 ringX[MAX_AUDIO_CHANNELS]{};
			f32 ringZ[MAX_AUDIO_CHANNELS]{};
			bool frontOnly = false;
		};

		PanLayout MakePanLayout(const ChannelLayout& output);

		void PanDirection(const PanLayout& layout, const Vec3& direction, f32* gains) [[clang::nonblocking]];

		class BuiltinSpatializer final : public IAudioSpatializer {
		public:
			void* CreateVoiceState(const AudioSpatializerContext& context) override;
			void DestroyVoiceState(void* state) override;
			void Process(void* state, const AudioSpatialParams& params, const AudioBlock& in, AudioBlock& out) [[clang::nonblocking]] override;
		};

		BuiltinSpatializer& GetBuiltinSpatializer();

	} // namespace AudioImpl

} // namespace Shalltear
