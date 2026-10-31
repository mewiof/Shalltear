#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioSpatializer.h"

#include "Shalltear/Core/Containers.h"

#include <string_view>

namespace Shalltear {

	namespace AudioImpl {

		IAudioSpatializer* FindSpatializer(std::string_view name);

		u32 GetRegisteredSpatializerCount();

		void TakeSpatializersOwnedBy(const void* owner, Vector<IAudioSpatializer*>& out);

		void ShutdownSpatializerRegistry();

	} // namespace AudioImpl

} // namespace Shalltear
