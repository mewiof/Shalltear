#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioGenerator.h"
#include "Shalltear/Audio/AudioResource.h"

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

namespace Shalltear {

	namespace AudioImpl {

		struct AudioGeneratorAccess {
			static AudioGenerator* Resolve(Handle<AudioResource> resource) {
				Object* object = ObjectRegistry::Resolve(resource.GetInstanceId());
				if (!object || !TypeRegistry::IsDerivedFrom(object->GetType(), AudioGenerator::GetReflectType())) {
					return nullptr;
				}
				return static_cast<AudioGenerator*>(object);
			}

			static AudioGeneratorControl MakeControl(AudioVoice voice) {
				AudioGeneratorControl control;
				control._voice = voice;
				return control;
			}
		};

	} // namespace AudioImpl

} // namespace Shalltear
