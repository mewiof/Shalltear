#pragma once

#include "AudioComposite.h"

#include "Shalltear/Audio/AudioContainer.h"

namespace Shalltear {

	namespace AudioImpl {

		IAudioComposite* CreateRandomPlayback(Handle<AudioResource> resource, const AudioRandomContainer& container,
											  const AudioPlayParams& params);

		IAudioComposite* CreateLayeredPlayback(Handle<AudioResource> resource, const AudioLayeredContainer& container,
											   const AudioPlayParams& params);

		IAudioComposite* CreateInteractivePlayback(Handle<AudioResource> resource, const AudioInteractiveContainer& container,
												   const AudioPlayParams& params);

	} // namespace AudioImpl

} // namespace Shalltear
