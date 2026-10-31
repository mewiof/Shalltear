#include "AudioComposite.h"

#include "AudioSystem.h"

#include "Shalltear/Audio/AudioContainer.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			constexpr u32 COMPOSITE_BIT = 0x80000000u;

			struct CompositeSlot {
				IAudioComposite* composite = nullptr;
				u32 generation = 1;
			};

			struct CompositeTable {
				Vector<CompositeSlot> slots{GetMemoryResource(MemTag::Audio)};
				Vector<u32> free{GetMemoryResource(MemTag::Audio)};
			};

			CompositeTable* _table = nullptr;

			CompositeSlot* Find(AudioVoice voice) {
				const u32 slot = AudioVoiceAccess::GetSlot(voice);
				if (!_table || (slot & COMPOSITE_BIT) == 0) {
					return nullptr;
				}
				const u32 index = slot & ~COMPOSITE_BIT;
				if (index >= _table->slots.size()) {
					return nullptr;
				}
				CompositeSlot& found = _table->slots[index];
				return found.composite && found.generation == AudioVoiceAccess::GetGeneration(voice) ? &found : nullptr;
			}

			void Free(u32 index) {
				CompositeSlot& slot = _table->slots[index];
				Delete(slot.composite);
				slot.composite = nullptr;
				slot.generation = slot.generation + 1 == 0 ? 1 : slot.generation + 1;
				_table->free.push_back(index);
			}

		} // anonymous namespace

		AudioTime GetEarliestExactStart() {
			return AudioPlaybackClock{}.EarliestExactStart();
		}

		AudioTime AudioPlaybackClock::Now() const {
			AudioSystem& system = GetAudioSystem();
			return AudioTime{device ? system.GetDeviceFrames() : system.GetTimeFrames()};
		}

		AudioTime AudioPlaybackClock::EarliestExactStart() const {
			const u32 period = GetAudioSystem().GetDevice().GetFormat().periodFrames;
			return AudioTime{Now().frames + period + AudioRenderer::BLOCK_FRAMES};
		}

		AudioPlaybackClock GetPlaybackClock(AudioBus output) {
			return AudioPlaybackClock{GetAudioSystem().IsDeviceClocked(output)};
		}

		IAudioComposite* FindComposite(AudioVoice voice) {
			CompositeSlot* slot = Find(voice);
			return slot ? slot->composite : nullptr;
		}

		AudioVoice AdoptComposite(IAudioComposite* composite) {
			if (!composite) {
				return {};
			}
			if (!GetAudioSystem().IsUp()) {
				Delete(composite);
				return {};
			}
			if (!_table) {
				_table = New<CompositeTable>(MemTag::Audio);
			}
			u32 index = 0;
			if (!_table->free.empty()) {
				index = _table->free.back();
				_table->free.pop_back();
			} else {
				index = static_cast<u32>(_table->slots.size());
				_table->slots.emplace_back();
			}
			CompositeSlot& slot = _table->slots[index];
			slot.composite = composite;
			return AudioVoiceAccess::Make(index | COMPOSITE_BIT, slot.generation);
		}

		void UpdateComposites() {
			if (!_table) {
				return;
			}
			for (u32 index = 0; index < _table->slots.size(); ++index) {
				if (!_table->slots[index].composite) {
					continue;
				}
				_table->slots[index].composite->Update();
				if (_table->slots[index].composite->IsFinished()) {
					Free(index);
				}
			}
		}

		u32 StopGameComposites() {
			if (!_table) {
				return 0;
			}
			u32 stopped = 0;
			for (u32 index = 0; index < _table->slots.size(); ++index) {
				IAudioComposite* composite = _table->slots[index].composite;
				if (composite && !composite->IsFinished() && !composite->KeepsDeviceTime()) {
					composite->Stop();
					++stopped;
				}
			}
			return stopped;
		}

		void ReleaseComposites() {
			if (!_table) {
				return;
			}
			for (u32 index = 0; index < _table->slots.size(); ++index) {
				if (_table->slots[index].composite) {
					_table->slots[index].composite->Stop();
					Free(index);
				}
			}
			Delete(_table);
			_table = nullptr;
		}

	} // namespace AudioImpl

	namespace Audio {

		using namespace AudioImpl;

		void SwitchToClip(AudioVoice voice, std::string_view clip) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SwitchToClip(clip);
			}
		}

		void SetLayerVolume(AudioVoice voice, u32 layer, f32 volume) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetLayerVolume(layer, volume);
			}
		}

	} // namespace Audio

} // namespace Shalltear
