#pragma once

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioAPI.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	enum class AudioMixerUpdateMode : u8 {
		Normal = 0,
		UnscaledTime = 1,
	};

	enum class AudioMixerGroupClock : u8 {
		Wall = 0,
		Gameplay = 1,
	};

	enum class AudioMixerUnitKind : u8 {
		Attenuation = 0,
		Send = 1,
		Effect = 2,
		Receive = 3,
	};

	enum class AudioMixerParameter : u8 {
		Volume = 0,
		Pitch = 1,
		SendLevel = 2,
		// percent
		WetLevel = 3,
		EffectParameter = 4,
	};

	enum class AudioMixerTransitionType : u8 {
		Lerp = 0,
		Smoothstep = 1,
		Squared = 2,
		SquareRoot = 3,
		BrickwallStart = 4,
		BrickwallEnd = 5,
		Attenuation = 6,
	};

	enum class AudioMixerGroupColor : u8 {
		None = 0,
		Yellow = 1,
		Orange = 2,
		Red = 3,
		Magenta = 4,
		Violet = 5,
		Blue = 6,
		Cyan = 7,
		Green = 8,
	};

	namespace AudioImpl {

		struct AudioMixerLive;
		struct AudioMixerAccess;

	} // namespace AudioImpl

	class AudioMixerGroup;
	class AudioMixerSnapshot;

	class SHALLTEAR_AUDIO_API AudioMixer : public Asset {
	public:
		SLT_REFLECT_CLASS(AudioMixer)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		struct Unit {
			AudioMixerUnitKind kind = AudioMixerUnitKind::Attenuation;
			str target;
			str targetUnit;
			// never changed by an edit
			str id;
			str effect;
			bool bypass = false;
			bool wetMixing = false;
		};

		struct Group {
			// never changed by a rename (references key on it)
			str id;
			str name;
			str parent;
			AudioMixerGroupClock clock = AudioMixerGroupClock::Wall;
			bool followTimeScale = false;
			bool mute = false;
			bool solo = false;
			bool bypassEffects = false;
			AudioMixerGroupColor color = AudioMixerGroupColor::None;
			u64 shownIn = 0;
			Vector<Unit> units{GetMemoryResource(MemTag::Asset)};
		};

		struct Value {
			str group;
			AudioMixerParameter parameter = AudioMixerParameter::Volume;
			str target;
			str effectParameter;
			f32 value = 0.0f;
		};

		struct TransitionOverride {
			str group;
			AudioMixerParameter parameter = AudioMixerParameter::Volume;
			str target;
			str effectParameter;
			AudioMixerTransitionType type = AudioMixerTransitionType::Lerp;
		};

		struct Snapshot {
			// never changed by a rename
			str id;
			str name;
			Vector<Value> values{GetMemoryResource(MemTag::Asset)};
			Vector<TransitionOverride> transitions{GetMemoryResource(MemTag::Asset)};
		};

		struct ExposedParameter {
			str name;
			str group;
			AudioMixerParameter parameter = AudioMixerParameter::Volume;
			str target;
			str effectParameter;
		};

		AudioMixer() = default;
		~AudioMixer() override;

		Vector<Group> groups{GetMemoryResource(MemTag::Asset)};
		Vector<Snapshot> snapshots{GetMemoryResource(MemTag::Asset)};
		Vector<ExposedParameter> exposedParameters{GetMemoryResource(MemTag::Asset)};
		str startSnapshot;
		AudioMixerUpdateMode updateMode = AudioMixerUpdateMode::Normal;
		Guid outputMixer;
		str outputGroup;
		Vector<str> views{GetMemoryResource(MemTag::Asset)};

		static f32 GetDefaultValue(AudioMixerParameter parameter);

		StringHash GetAssetTypeId() const override { return GetReflectType()->name; }

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		Handle<AudioMixerSnapshot> FindSnapshot(std::string_view name);

		u32 FindMatchingGroups(std::string_view subPath, Vector<Handle<AudioMixerGroup>>& out);

		bool TransitionToSnapshots(std::span<const Handle<AudioMixerSnapshot>> snapshots, std::span<const f32> weights, f32 timeToReach);

		bool TransitionToSnapshot(std::string_view snapshotId, f32 timeToReach);
		bool TransitionToSnapshots(std::span<const std::string_view> snapshotIds, std::span<const f32> weights, f32 timeToReach);

		bool SetFloat(std::string_view name, f32 value);

		bool GetFloat(std::string_view name, f32& value) const;

		bool ClearFloat(std::string_view name);

		AudioBus GetBus(std::string_view group) const;

		bool ApplyChanges();
	protected:
		void OnLoad() override;
		void OnUnload() override;
	private:
		friend struct AudioImpl::AudioMixerAccess;

		struct Product {
			str key;
			Guid guid;
		};

		AudioImpl::AudioMixerLive* _live = nullptr;
		Vector<Product> _products{GetMemoryResource(MemTag::Asset)};
	};

	class SHALLTEAR_AUDIO_API AudioMixerGroup : public Asset {
	public:
		SLT_REFLECT_CLASS(AudioMixerGroup)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		AudioMixerGroup() = default;
		~AudioMixerGroup() override;

		StringHash GetAssetTypeId() const override { return GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		Handle<AudioMixer> GetAudioMixer() const;
		std::string_view GetGroupId() const { return _groupId; }

		AudioBus GetBus() const;
	protected:
		void OnUnload() override;
	private:
		friend struct AudioImpl::AudioMixerAccess;

		AudioMixer* ResolveMixer() const;

		Guid _mixer;
		str _groupId;
		mutable Handle<AudioMixer> _audioMixer;
		mutable bool _holdsMixer = false;
		mutable bool _mixerRefused = false;

		mutable AudioBus _bus;
		mutable u32 _busRevision = 0;
	};

	class SHALLTEAR_AUDIO_API AudioMixerSnapshot : public Asset {
	public:
		SLT_REFLECT_CLASS(AudioMixerSnapshot)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		AudioMixerSnapshot() = default;
		~AudioMixerSnapshot() override;

		StringHash GetAssetTypeId() const override { return GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		bool TransitionTo(f32 timeToReach);

		Handle<AudioMixer> GetAudioMixer() const;
		std::string_view GetSnapshotId() const { return _snapshotId; }
	protected:
		void OnUnload() override;
	private:
		friend struct AudioImpl::AudioMixerAccess;

		AudioMixer* ResolveMixer() const;

		Guid _mixer;
		str _snapshotId;
		mutable Handle<AudioMixer> _audioMixer;
		mutable bool _holdsMixer = false;
		mutable bool _mixerRefused = false;
	};

} // namespace Shalltear
