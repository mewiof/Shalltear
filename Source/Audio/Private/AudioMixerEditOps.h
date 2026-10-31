#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioMixer.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear::AudioImpl::AudioMixerEditOps {

	inline constexpr f32 MIN_DECIBELS = -80.0f;
	inline constexpr f32 MAX_DECIBELS = 20.0f;

	inline constexpr f32 MIN_PITCH = 0.01f;
	inline constexpr f32 MAX_PITCH = 10.0f;
	inline constexpr f32 MAX_SEND_DECIBELS = 0.0f;

	inline constexpr std::string_view NEW_GROUP_NAME = "New Group";

	struct GroupEntry {
		u32 index = 0;
		u32 depth = 0;
		bool hasChildren = false;
	};

	void Flatten(const AudioMixer& mixer, Vector<GroupEntry>& out);

	i32 FindTop(const AudioMixer& mixer);

	bool IsUnder(const AudioMixer& mixer, std::string_view group, std::string_view ancestor);

	str AddGroup(AudioMixer& mixer, std::string_view parent, std::string_view name = NEW_GROUP_NAME);

	void DuplicateGroups(AudioMixer& mixer, std::span<const str> groups, u32 view, Vector<str>& copies);

	u32 RemoveGroups(AudioMixer& mixer, std::span<const str> groups);

	bool CanMoveGroup(const AudioMixer& mixer, std::string_view group, std::string_view parent);

	bool MoveGroup(AudioMixer& mixer, std::string_view group, std::string_view parent, u32 slot);

	bool RenameGroup(AudioMixer& mixer, std::string_view group, std::string_view name);

	inline constexpr f32 FADER_WARP = 1.7f;
	inline constexpr f32 FADER_ZERO = 0.25f;
	f32 DecibelsToFader(f32 decibels);
	f32 FaderToDecibels(f32 share);

	f32 GetGroupVolume(const AudioMixer& mixer, u32 snapshot, std::string_view group);

	bool SetGroupVolume(AudioMixer& mixer, u32 snapshot, std::string_view group, f32 decibels);

	bool GetLiveGroupVolume(const AudioMixer& mixer, std::string_view group, f32& decibels);

	f32 LevelToDecibels(f32 linear);

	struct ParameterRef {
		str group;
		AudioMixerParameter parameter = AudioMixerParameter::Volume;
		str target;
		str effectParameter;
	};

	struct ParameterRange {
		f32 minimum = 0.0f;
		f32 maximum = 1.0f;
		f32 defaultValue = 0.0f;
		f32 displayScale = 1.0f;
		std::string_view unit;
	};

	bool GetRange(const AudioMixer& mixer, const ParameterRef& ref, ParameterRange& out);

	f32 GetValue(const AudioMixer& mixer, u32 snapshot, const ParameterRef& ref);

	bool SetValue(AudioMixer& mixer, u32 snapshot, const ParameterRef& ref, f32 value);

	bool GetLiveValue(const AudioMixer& mixer, const ParameterRef& ref, f32& value);

	i32 AddUnit(AudioMixer& mixer, std::string_view group, u32 index, AudioMixerUnitKind kind, std::string_view effect = {});

	bool RemoveUnit(AudioMixer& mixer, std::string_view group, u32 index);

	bool MoveUnit(AudioMixer& mixer, std::string_view group, u32 from, u32 to);

	bool SetWetMixing(AudioMixer& mixer, std::string_view group, u32 index, bool on);

	struct SendTarget {
		str group;
		str unit;
		bool loops = false;
	};

	void ListSendTargets(const AudioMixer& mixer, std::string_view group, u32 index, Vector<SendTarget>& out);

	bool RetargetSend(AudioMixer& mixer, std::string_view group, u32 index, std::string_view target, std::string_view targetUnit);

	void CopyUnitToAllSnapshots(AudioMixer& mixer, u32 snapshot, std::string_view group, u32 index, bool wet);

	void CopyGroupToAllSnapshots(AudioMixer& mixer, u32 snapshot, std::string_view group);

	inline constexpr std::string_view COPY_SUFFIX = " - Copy";

	u32 FindSnapshot(const AudioMixer& mixer, std::string_view id);

	i32 DuplicateSnapshot(AudioMixer& mixer, u32 from);

	bool RenameSnapshot(AudioMixer& mixer, u32 index, std::string_view name);

	bool RemoveSnapshot(AudioMixer& mixer, u32 index);

	bool MoveSnapshot(AudioMixer& mixer, u32 from, u32 to);

	bool SetStartSnapshot(AudioMixer& mixer, u32 index);

	inline constexpr std::string_view DEFAULT_VIEW_NAME = "View";

	inline constexpr u32 MAX_VIEWS = 64;

	u32 CountViews(const AudioMixer& mixer);
	std::string_view ViewName(const AudioMixer& mixer, u32 view);

	bool IsVisible(const AudioMixer& mixer, u32 view, std::string_view group);

	i32 DuplicateView(AudioMixer& mixer, u32 from);

	bool RenameView(AudioMixer& mixer, u32 view, std::string_view name);

	bool RemoveView(AudioMixer& mixer, u32 view);

	bool MoveView(AudioMixer& mixer, u32 from, u32 to);

	bool SetVisible(AudioMixer& mixer, u32 view, std::span<const str> groups, bool visible);

	bool SetColor(AudioMixer& mixer, std::span<const str> groups, AudioMixerGroupColor color);

	struct MixerListing {
		Guid guid;
		str name;
		const AudioMixer* data = nullptr;
	};

	struct MixerRow {
		u32 index = 0;
		u32 depth = 0;
		bool hasChildren = false;
	};

	void ArrangeMixers(std::span<const MixerListing> mixers, Vector<MixerRow>& out);

	str DescribeOutput(std::span<const MixerListing> mixers, const MixerListing& mixer);

	bool CanRouteInto(std::span<const MixerListing> mixers, const Guid& dragged, const Guid& onto);

	inline constexpr std::string_view NEW_EXPOSED_NAME = "MyExposedParam";
	inline constexpr size_t MAX_EXPOSED_NAME = 64;

	str DescribeParameter(const AudioMixer& mixer, const ParameterRef& ref, bool baseOnly = false);

	i32 FindExposed(const AudioMixer& mixer, const ParameterRef& ref);

	str ExposeParameter(AudioMixer& mixer, const ParameterRef& ref);

	bool UnexposeParameter(AudioMixer& mixer, const ParameterRef& ref);

	bool RenameExposed(AudioMixer& mixer, u32 index, std::string_view name);

	AudioMixerTransitionType GetTransition(const AudioMixer& mixer, u32 snapshot, const ParameterRef& ref);

	bool SetTransition(AudioMixer& mixer, u32 snapshot, const ParameterRef& ref, AudioMixerTransitionType type);

	bool AcquireHold(AudioMixer& mixer, u32 snapshot);
	void ReleaseHold(AudioMixer& mixer);

	void RefreshHold(AudioMixer& mixer, u32 snapshot);

	u32 CountHolds(const AudioMixer& mixer);

} // namespace Shalltear::AudioImpl::AudioMixerEditOps
