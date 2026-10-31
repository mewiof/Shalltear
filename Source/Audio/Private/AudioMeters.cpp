#include "AudioSystem.h"

#include "Shalltear/Core/Memory.h"

#include <bit>
#include <cmath>
#include <new>

namespace Shalltear::AudioImpl {

	bool AudioRenderer::EnableMetering(bool enabled) {
		if (!_voices) {
			return false;
		}
		if (enabled && !_meters) {
			_meters = static_cast<AudioBusMeter*>(Allocate(sizeof(AudioBusMeter) * MAX_BUSES, MemTag::Audio, 64));
			for (u32 slot = 0; slot < MAX_BUSES; ++slot) {
				new (&_meters[slot]) AudioBusMeter();
			}
		}
		AudioCommand command;
		command.kind = AudioCommandKind::SetMeters;
		command.block = enabled ? _meters : nullptr;
		return _commands.TryPush(command);
	}

	void AudioSystem::AcquireMetering() {
		if (++_metering == 1 && _up) {
			_renderer.EnableMetering(true);
		}
	}

	void AudioSystem::ReleaseMetering() {
		if (_metering == 0) {
			return;
		}
		if (--_metering == 0 && _up) {
			_renderer.EnableMetering(false);
		}
	}

	bool AudioSystem::ReadBusLevel(AudioBus bus, AudioBusLevel& out, AudioMeterPoint point) const {
		out = AudioBusLevel{};
		const AudioBusMeter* meters = _renderer.GetMeters();
		const u32 slot = AudioBusAccess::GetSlot(bus);
		if (_metering == 0 || !meters || !IsBusLive(bus) || slot >= AudioRenderer::MAX_BUSES) {
			return false;
		}
		const AudioMeterTap& tap = point == AudioMeterPoint::Fader ? meters[slot].fader : meters[slot].output;
		out.channels = _renderer.GetChannels();
		for (u32 channel = 0; channel < out.channels; ++channel) {
			const f32 meanSquare = std::bit_cast<f32>(tap.meanSquare[channel].load(std::memory_order_relaxed));
			out.rms[channel] = std::sqrt(meanSquare);
			out.peak[channel] = std::bit_cast<f32>(tap.peak[channel].load(std::memory_order_relaxed));
		}
		return true;
	}

} // namespace Shalltear::AudioImpl
