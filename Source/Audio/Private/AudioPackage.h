#pragma once

#include "AudioVoiceInternals.h"

namespace Shalltear {

	namespace AudioImpl {

		void RegisterPackage(const void* owner);
		void ShutdownPackage();

	} // namespace AudioImpl

} // namespace Shalltear
