#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Audio/AudioClip.h"
#include "Shalltear/Audio/AudioConcurrency.h"
#include "Shalltear/Audio/AudioContainer.h"
#include "Shalltear/Audio/AudioFilters.h"
#include "Shalltear/Audio/AudioGenerator.h"
#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioMixer.h"
#include "Shalltear/Audio/AudioReverbZone.h"
#include "Shalltear/Audio/AudioSource.h"

#include "AudioComposite.h"
#include "AudioEffectRegistry.h"
#include "AudioMixerLive.h"
#include "Effects/BuiltinEffects.h"
#include "AudioPackage.h"
#include "AudioScenePass.h"
#include "AudioSettings.h"
#include "AudioSpatializerRegistry.h"
#include "AudioSystem.h"
#include "AudioVoiceManager.h"

#if defined(SLT_WITH_EDITOR)
#include "AudioEditor.h"
#endif

namespace Shalltear {

	namespace AudioImpl {

		constexpr const char* PACKAGE_DISPLAY_NAME = "Audio";

		namespace {

			void PumpStage(const FrameStageArgs&, void* user) {
				AudioSystem& system = *static_cast<AudioSystem*>(user);
				system.Pump();
				UpdateMixers();
				system.EndFrameClock();
			}

			void FixedStepStage(const FrameStageArgs&, void* user) {
				static_cast<AudioSystem*>(user)->NoteGameplay();
				SampleFixedStep();
			}

			void UpdateStage(const FrameStageArgs&, void* user) {
				static_cast<AudioSystem*>(user)->NoteGameplay();
				SampleFrameStart();
			}

		} // anonymous namespace

		void RegisterPackage(const void* owner) {
			AudioSettings* settings = Settings::DeclareCategory<AudioSettings>(SETTINGS_CATEGORY_PATH, SettingScope::Project, true);
			if (!settings) {
				SLT_LOG_ERROR("Audio", "couldn't register the {} settings category", SETTINGS_CATEGORY_PATH);
			}
			SetDeclaredSettings(settings);

			AudioSystem& system = GetAudioSystem();
			system.Up();
			RegisterBuiltinEffects(owner);
			ActivateMixers();

			RegisterScenePass(owner);
			FrameStages::Register(FrameStage::EndFrame, FrameStagePosition::Late, &PumpStage, &system, owner);
			RegisterSourceCues(owner);

			FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Early, &FixedStepStage, &system, owner);
			FrameStages::Register(FrameStage::Update, FrameStagePosition::Early, &UpdateStage, &system, owner);

			// static player drops types without these
			AudioResource::GetReflectType();
			AudioClip::GetReflectType();
			AudioGenerator::GetReflectType();
			AudioMixer::GetReflectType();
			AudioMixerGroup::GetReflectType();
			AudioMixerSnapshot::GetReflectType();
			AudioConcurrency::GetReflectType();
			AudioContainer::GetReflectType();
			AudioRandomContainer::GetReflectType();
			AudioLayeredContainer::GetReflectType();
			AudioInteractiveContainer::GetReflectType();
			AudioSource::GetReflectType();
			AudioListener::GetReflectType();
			AudioReverbZone::GetReflectType();
			AudioFilter::GetReflectType();
			AudioLowPassFilter::GetReflectType();
			AudioHighPassFilter::GetReflectType();
			AudioEchoFilter::GetReflectType();
			AudioDistortionFilter::GetReflectType();
			AudioReverbFilter::GetReflectType();
			AudioChorusFilter::GetReflectType();

			RegisterMixerImporter(owner);
			RegisterConcurrencyImporter(owner);
			RegisterContainerImporters(owner);

#if defined(SLT_WITH_EDITOR)
			const bool imports = AudioEditor::RegisterClipImporter(owner);
#else
			const bool imports = false;
#endif

			SLT_LOG_INFO("Audio", "{} up: {}, {}", PACKAGE_DISPLAY_NAME, GetSettings().obtainedPeriod,
						 imports ? "importing clips" : "no importer half in this build");
		}

		// closing the device joins threads, must run before DLL detach
		void ShutdownPackage() {
#if defined(SLT_WITH_EDITOR)
			// first, its delegate lives in this image
			AudioEditor::ReleasePlaySession();
			AudioEditor::ReleaseEditorState();
#endif
			DeactivateMixers();
			GetAudioSystem().Down();
			SetDeclaredSettings(nullptr);
			ShutdownScenePass();
			ShutdownMixerRegistry();
			ShutdownSpatializerRegistry();
			ShutdownEffectRegistry();
		}

	} // namespace AudioImpl

} // namespace Shalltear

SLT_IMPLEMENT_PACKAGE_WITH_SHUTDOWN(Audio,
									&Shalltear::AudioImpl::RegisterPackage,
									&Shalltear::AudioImpl::ShutdownPackage)
