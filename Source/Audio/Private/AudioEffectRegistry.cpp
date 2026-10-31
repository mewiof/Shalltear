#include "AudioEffectRegistry.h"

#include "AudioMixerLive.h"
#include "AudioScenePass.h"
#include "AudioSystem.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			struct Entry {
				str name;
				AudioEffectEntry effect;
			};

			Vector<Entry>* _entries = nullptr;

			Vector<Entry>& Entries() {
				if (!_entries) {
					_entries = New<Vector<Entry>>(MemTag::Audio, GetMemoryResource(MemTag::Audio));
				}
				return *_entries;
			}

			bool ValidateParameters(std::string_view effect, std::span<const AudioParameterDefinition> parameters) {
				for (size_t index = 0; index < parameters.size(); ++index) {
					const AudioParameterDefinition& parameter = parameters[index];
					if (parameter.name.empty()) {
						SLT_LOG_WARN("Audio", "the effect '{}' declares parameter {} with no name (not registered)", effect, index);
						return false;
					}
					if (!(parameter.minimum <= parameter.defaultValue && parameter.defaultValue <= parameter.maximum)) {
						SLT_LOG_WARN("Audio", "the effect '{}' declares parameter '{}' with a default outside its range (not registered)", effect, parameter.name);
						return false;
					}
					for (size_t earlier = 0; earlier < index; ++earlier) {
						if (parameters[earlier].name == parameter.name) {
							SLT_LOG_WARN("Audio", "the effect '{}' declares parameter '{}' twice (not registered)", effect, parameter.name);
							return false;
						}
					}
				}
				return true;
			}

		} // anonymous namespace

		IAudioEffectDefinition* FindEffect(std::string_view name) {
			if (!_entries || name.empty()) {
				return nullptr;
			}
			for (const Entry& entry : *_entries) {
				if (entry.name == name) {
					return entry.effect.definition;
				}
			}
			return nullptr;
		}

		const void* FindEffectOwner(const IAudioEffectDefinition* definition) {
			if (!_entries || !definition) {
				return nullptr;
			}
			for (const Entry& entry : *_entries) {
				if (entry.effect.definition == definition) {
					return entry.effect.owner;
				}
			}
			return nullptr;
		}

		u32 FindEffectParameter(const IAudioEffectDefinition& definition, std::string_view name) {
			const std::span<const AudioParameterDefinition> parameters = definition.GetParameters();
			for (size_t index = 0; index < parameters.size(); ++index) {
				if (parameters[index].name == name) {
					return static_cast<u32>(index);
				}
			}
			return NO_EFFECT_PARAMETER;
		}

		void ResolveParameterValues(const IAudioEffectDefinition& definition, Vector<f32>& values) {
			const std::span<const AudioParameterDefinition> table = definition.GetParameters();
			const size_t given = values.size();
			values.resize(table.size());
			for (size_t index = 0; index < table.size(); ++index) {
				values[index] = ClampParameterValue(definition, static_cast<u32>(index), index < given ? values[index] : table[index].defaultValue);
			}
		}

		f32 ClampParameterValue(const IAudioEffectDefinition& definition, u32 index, f32 value) {
			const AudioParameterDefinition& parameter = definition.GetParameters()[index];
			return value >= parameter.minimum ? (value <= parameter.maximum ? value : parameter.maximum) : parameter.minimum;
		}

		u32 GetRegisteredEffectCount() {
			return _entries ? static_cast<u32>(_entries->size()) : 0u;
		}

		std::string_view GetRegisteredEffectName(u32 index) {
			return _entries && index < _entries->size() ? std::string_view((*_entries)[index].name) : std::string_view{};
		}

		void TakeEffectsOwnedBy(const void* owner, Vector<IAudioEffectDefinition*>& out) {
			if (!_entries) {
				return;
			}
			for (const Entry& entry : *_entries) {
				if (entry.effect.owner == owner) {
					out.push_back(entry.effect.definition);
				}
			}
			std::erase_if(*_entries, [owner](const Entry& entry) { return entry.effect.owner == owner; });
		}

		void ShutdownEffectRegistry() {
			if (_entries) {
				Delete(_entries);
				_entries = nullptr;
			}
		}

	} // namespace AudioImpl

	namespace AudioEffects {

		using namespace AudioImpl;

		bool Register(std::string_view name, IAudioEffectDefinition* definition, const void* owner) {
			if (name.empty() || !definition) {
				SLT_LOG_WARN("Audio", "an effect was registered with no name or no definition (ignored)");
				return false;
			}
			if (FindEffect(name)) {
				SLT_LOG_WARN("Audio", "an effect named '{}' is already registered. Ignoring the 2nd", name);
				return false;
			}
			if (!ValidateParameters(name, definition->GetParameters())) {
				return false;
			}
			Entries().push_back(Entry{str(name), AudioEffectEntry{definition, owner}});
			OnEffectRegistered(name);
			GetAudioSystem().OnVoiceEffectRegistered(name);
			OnListenerEffectRegistered(name);
			return true;
		}

		u32 UnregisterOwnedBy(const void* owner) {
			Vector<IAudioEffectDefinition*> gone{GetMemoryResource(MemTag::Audio)};
			TakeEffectsOwnedBy(owner, gone);
			AudioDetachment detachment;
			for (IAudioEffectDefinition* definition : gone) {
				DetachMixerEffects(definition, detachment);
				GetAudioSystem().DetachVoiceEffects(definition, detachment);
				DetachListenerEffects(definition, detachment);
			}
			GetAudioSystem().CompleteDetachment(detachment);
			return static_cast<u32>(gone.size());
		}

	} // namespace AudioEffects

} // namespace Shalltear
