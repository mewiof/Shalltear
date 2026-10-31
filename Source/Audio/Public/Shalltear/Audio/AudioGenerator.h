#pragma once

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioAPI.h"
#include "Shalltear/Audio/AudioResource.h"
#include "Shalltear/Audio/AudioSpatializer.h"

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	namespace AudioImpl {

		struct AudioGeneratorAccess;

	} // namespace AudioImpl

	struct AudioGeneratorFormat {
		u32 sampleRate = 0;
		u32 channels = 0;
	};

	class IAudioGeneratorInstance {
	public:
		// fewer frames than asked ends the sound
		virtual u32 Process(AudioBlock& out, u64 fromFrame) [[clang::nonblocking]] = 0;

		virtual void SetParameter(u32 index, f32 value) [[clang::nonblocking]] {
			(void)index;
			(void)value;
		}

		// once after the render thread lets go
		virtual void Release() = 0;
	protected:
		~IAudioGeneratorInstance() = default;
	};

	class SHALLTEAR_AUDIO_API AudioGeneratorControl {
	public:
		AudioVoice GetVoice() const { return _voice; }

		void SetParameter(u32 index, f32 value) const;
	private:
		friend struct AudioImpl::AudioGeneratorAccess;

		AudioVoice _voice;
	};

	class SHALLTEAR_AUDIO_API AudioGenerator : public AudioResource {
	public:
		SLT_REFLECT_CLASS(AudioGenerator)
		SLT_REFLECT_PARENT(AudioResource)
		builder.SetFlags(::Shalltear::TypeFlagAbstract);
		SLT_END_REFLECT()

		~AudioGenerator() override = default;

		virtual IAudioGeneratorInstance* CreateInstance(const AudioGeneratorFormat& suggested, AudioGeneratorFormat& chosen) = 0;

		virtual void Update(AudioGeneratorControl& control) { (void)control; }

		StringHash GetAssetTypeId() const override { return GetType()->name; }

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;
	protected:
		AudioGenerator() = default;
	};

	namespace Audio {

		SHALLTEAR_AUDIO_API void SetGeneratorParameter(AudioVoice voice, u32 index, f32 value);

	} // namespace Audio

} // namespace Shalltear
