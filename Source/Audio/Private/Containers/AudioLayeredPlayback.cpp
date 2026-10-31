#include "AudioContainerKinds.h"
#include "AudioContainerPlayback.h"

#include "Shalltear/Audio/AudioContainer.h"

#include "Shalltear/Core/Memory.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			f32 ClampUnit(f32 value) {
				return value >= 0.0f ? (value <= 1.0f ? value : 1.0f) : 0.0f;
			}

			class AudioLayeredPlayback final : public AudioContainerPlayback {
			public:
				AudioLayeredPlayback(Handle<AudioResource> resource, const AudioPlayParams& params)
					: AudioContainerPlayback(resource, params) {}

				bool Begin(const AudioLayeredContainer& container) {
					const AudioTime earliest = _timebase.EarliestExactStart();
					const AudioTime at = _at > earliest ? _at : earliest;
					for (u32 layer = 0; layer < container.layers.size(); ++layer) {
						const AudioLayeredContainerLayer& authored = container.layers[layer];
						if (authored.resource.GetInstanceId() == 0) {
							continue;
						}
						StartChild(authored.resource, at, _startSample, _loop, ClampUnit(authored.volume), 1.0f, layer);
					}
					return !_children.empty();
				}

				void Update() override { PruneChildren(); }

				void SetLayerVolume(u32 layer, f32 volume) override {
					const f32 clamped = ClampUnit(volume);
					for (AudioContainerChild& child : _children) {
						if (child.tag == layer) {
							child.volumeScale = clamped;
							Audio::SetVolume(child.voice, _volume * clamped);
						}
					}
				}

				u64 GetTimeSamples() override {
					return _children.empty() ? _startSample : Audio::GetTimeSamples(_children.front().voice);
				}

				void SetTimeSamples(u64 sample) override {
					const AudioLayeredContainer* container = ResolveContainer<AudioLayeredContainer>();
					if (!container || _children.empty()) {
						_startSample = sample;
						return;
					}
					const AudioTime at = _timebase.EarliestExactStart();
					const Vector<AudioContainerChild> playing = _children;
					_children.clear();
					for (const AudioContainerChild& child : playing) {
						Audio::SetScheduledEndTime(child.voice, at);
						if (child.tag < container->layers.size()) {
							StartChild(container->layers[child.tag].resource, at, sample, _loop, child.volumeScale, 1.0f, child.tag);
						}
					}
				}
			protected:
				bool HasMoreToStart() const override { return false; }
			};

		} // anonymous namespace

		IAudioComposite* CreateLayeredPlayback(Handle<AudioResource> resource, const AudioLayeredContainer& container,
											   const AudioPlayParams& params) {
			auto* playback = New<AudioLayeredPlayback>(MemTag::Audio, resource, params);
			if (!playback->Begin(container)) {
				Delete(playback);
				return nullptr;
			}
			return playback;
		}

	} // namespace AudioImpl

} // namespace Shalltear
