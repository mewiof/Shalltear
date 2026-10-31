#include "AudioScriptProbe.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioEffect.h"
#include "Shalltear/Audio/AudioGenerator.h"
#include "Shalltear/Audio/AudioMixer.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace Shalltear;

namespace AudioScriptModuleImpl {

#ifdef SLT_AUDIO_SCRIPT_V2
	constexpr u32 VERSION = 2;
#else
	constexpr u32 VERSION = 1;
#endif

	constexpr const char* GAIN_EFFECT = "AudioScriptModule.Gain";

	AudioScriptProbe* _probe = nullptr;
	AudioScriptProbe::Version* _report = nullptr;

	void FindProbe() {
		const auto find = reinterpret_cast<AudioScriptProbeFn>(::GetProcAddress(::GetModuleHandleW(nullptr), AUDIO_SCRIPT_PROBE_EXPORT));
		_probe = find ? find() : nullptr;
		_report = _probe ? &_probe->versions[VERSION] : nullptr;
	}

	void NoteRelease() {
		if (_probe) {
			_report->lastReleaseAt.store(_probe->sequence.fetch_add(1) + 1);
		}
	}

	class ScriptGain final : public IAudioEffect {
	public:
		ScriptGain(AudioScriptProbe::Version* report, f32 gain) : _report(report), _gain(gain) {}
		~ScriptGain() = default;

		void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
			(void)sidechain;
			for (u32 channel = 0; channel < block.channels; ++channel) {
				f32* row = block.data + static_cast<size_t>(channel) * block.stride;
				for (u32 frame = 0; frame < block.frames; ++frame) {
					row[frame] *= _gain;
				}
			}
			if (_report) {
				_report->effectBlocks.fetch_add(1, std::memory_order_relaxed);
			}
		}

		void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
			if (index == 0) {
				_gain = value;
			}
		}

		void Release() override {
			if (_report) {
				_report->effectsReleased.fetch_add(1);
			}
			NoteRelease();
			Delete(this);
		}
	private:
		AudioScriptProbe::Version* _report;
		f32 _gain;
	};

	class GainDefinition final : public IAudioEffectDefinition {
	public:
		static constexpr AudioParameterDefinition PARAMETERS[] = {{"Gain", "x", 0.0f, 2.0f, 1.0f}};

		std::span<const AudioParameterDefinition> GetParameters() const override { return PARAMETERS; }

		IAudioEffect* CreateEffect(const AudioEffectContext& context) override {
			if (_report) {
				_report->effectsMade.fetch_add(1);
			}
			return New<ScriptGain>(MemTag::Audio, _report, context.parameters.empty() ? 1.0f : context.parameters[0]);
		}
	};

	GainDefinition _gainDefinition;

	class ScriptToneInstance final : public IAudioGeneratorInstance {
	public:
		ScriptToneInstance(AudioScriptProbe::Version* report, f32 level) : _report(report), _level(level) {}
		~ScriptToneInstance() = default;

		u32 Process(AudioBlock& out, u64 fromFrame) [[clang::nonblocking]] override {
			for (u32 channel = 0; channel < out.channels; ++channel) {
				f32* row = out.data + static_cast<size_t>(channel) * out.stride;
				for (u32 frame = 0; frame < out.frames; ++frame) {
					row[frame] = _level;
				}
			}
			if (_report) {
				u64 unset = ~0ull;
				_report->firstFrom.compare_exchange_strong(unset, fromFrame, std::memory_order_relaxed);
				_report->lastEnd.store(fromFrame + out.frames, std::memory_order_relaxed);
				_report->generatorBlocks.fetch_add(1, std::memory_order_relaxed);
			}
			return out.frames;
		}

		void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
			if (index == 0) {
				_level = value;
			}
		}

		void Release() override {
			if (_report) {
				_report->generatorsReleased.fetch_add(1);
			}
			NoteRelease();
			Delete(this);
		}
	private:
		AudioScriptProbe::Version* _report;
		f32 _level;
	};

	class AudioScriptTone : public AudioGenerator {
	public:
		SLT_REFLECT_CLASS(AudioScriptTone)
		SLT_REFLECT_PARENT(AudioGenerator)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(level)
		SLT_END_REFLECT()

		f32 level = 0.25f;

		IAudioGeneratorInstance* CreateInstance(const AudioGeneratorFormat& suggested, AudioGeneratorFormat& chosen) override {
			chosen.sampleRate = suggested.sampleRate;
			chosen.channels = 1;
			if (_report) {
				_report->generatorsMade.fetch_add(1);
			}
			return New<ScriptToneInstance>(MemTag::Audio, _report, level);
		}
	};

	class ToneImporter final : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return AudioScriptTone::GetReflectType()->name; }

		Asset* Import(const Path& sourcePath) override {
			auto* tone = New<AudioScriptTone>(MemTag::Asset);
			if (!tone->Import(sourcePath)) {
				Delete(tone);
				return nullptr;
			}
			return tone;
		}

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return "Audio Script Tone"; }
		std::string_view GetCreateDefaultFileName() const override { return "New Tone.sltone"; }
		Asset* CreateAsset() override { return New<AudioScriptTone>(MemTag::Asset); }

		u32 GetImporterVersion() const override { return 1; }
	};

	ToneImporter _toneImporter;

	void RegisterCoreExtensions(const void* owner) {
		FindProbe();
		AssetImporterRegistry::Register(AudioScriptTone::GetReflectType()->name, &_toneImporter, {".sltone"}, owner);
		AudioEffects::Register(GAIN_EFFECT, &_gainDefinition, owner);
	}

	void Shutdown() {
		if (_probe) {
			_report->shutdownAt.store(_probe->sequence.fetch_add(1) + 1);
		}
	}

} // namespace AudioScriptModuleImpl

using namespace AudioScriptModuleImpl;

extern "C" __declspec(dllexport) bool AudioScriptModulePlay(const char* path) {
	Asset* asset = AssetDatabase::LoadAsset(Path(path));
	if (!asset || !TypeRegistry::IsDerivedFrom(asset->GetType(), AudioResource::GetReflectType())) {
		return false;
	}
	AudioPlayParams params;
	params.loop = true;
	return Audio::Play(Handle<AudioResource>(static_cast<AudioResource*>(asset)), params).IsValid();
}

extern "C" __declspec(dllexport) void* AudioScriptModuleMakeMixer() {
	AudioMixer* mixer = New<AudioMixer>(MemTag::Asset);
	AudioMixer::Group master;
	master.id = "Master";
	master.name = "Master";
	AudioMixer::Unit gain;
	gain.kind = AudioMixerUnitKind::Effect;
	gain.id = "Gain";
	gain.effect = GAIN_EFFECT;
	master.units.push_back(gain);
	master.units.push_back(AudioMixer::Unit{});
	mixer->groups.push_back(std::move(master));
	AudioMixer::Snapshot start;
	start.id = "Start";
	start.name = "Start";
	mixer->snapshots.push_back(std::move(start));
	mixer->startSnapshot = "Start";
	if (!mixer->ApplyChanges()) {
		Delete(mixer);
		return nullptr;
	}
	return mixer;
}

extern "C" __declspec(dllexport) void AudioScriptModuleDestroyMixer(void* mixer) {
	Delete(static_cast<AudioMixer*>(mixer));
}

SLT_REGISTER_TYPE(AudioScriptModuleImpl::AudioScriptTone)
SLT_IMPLEMENT_CORE_EXTENSIONS(&AudioScriptModuleImpl::RegisterCoreExtensions)
SLT_IMPLEMENT_MODULE_WITH_SHUTDOWN(AudioScriptModule, &AudioScriptModuleImpl::Shutdown)
