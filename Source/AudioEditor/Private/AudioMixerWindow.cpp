#include "AudioEditor.h"
#include "AudioEffectRegistry.h"
#include "AudioMixerEditOps.h"
#include "AudioMixerLive.h"
#include "AudioSystem.h"

#include "Shalltear/Audio/AudioMixer.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/DragDrop.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorAssetWatch.h"
#include "Shalltear/Editor/EditorDialogs.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorInsertStrips.h"
#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Shalltear::AudioEditorImpl {

	namespace Ops = AudioImpl::AudioMixerEditOps;
	using EditorTheme::ColorToken;
	using EditorTheme::MetricToken;

	namespace {

		constexpr StringHash GROUP_PAYLOAD = StringHash("AudioMixerGroup");

		constexpr StringHash STRIP_CLASS = StringHash("ed-mixer-strip");
		constexpr StringHash FADER_CLASS = StringHash("ed-mixer-fader");
		constexpr StringHash SOLO_CLASS = StringHash("ed-mixer-solo");
		constexpr StringHash MUTE_CLASS = StringHash("ed-mixer-mute");
		constexpr StringHash BYPASS_CLASS = StringHash("ed-mixer-bypass");
		constexpr StringHash SLOT_CLASS = StringHash("ed-mixer-slot");
		constexpr StringHash ADD_SLOT_CLASS = StringHash("ed-mixer-add");
		constexpr StringHash CHAIN_CLASS = StringHash("ed-mixer-chain");
		constexpr StringHash UNIT_CLASS = StringHash("ed-mixer-unit");
		constexpr StringHash UNIT_HEADER_CLASS = StringHash("ed-mixer-unit-header");

		constexpr StringHash UNIT_PAYLOAD = StringHash("AudioMixerUnit");

		constexpr StringHash SNAPSHOT_PAYLOAD = StringHash("AudioMixerSnapshot");

		constexpr StringHash EDIT_IN_PLAY_CLASS = StringHash("ed-mixer-edit-in-play");
		constexpr StringHash SNAPSHOT_ADD_CLASS = StringHash("ed-mixer-snapshot-add");
		constexpr StringHash GROUP_ADD_CLASS = StringHash("ed-mixer-group-add");
		constexpr StringHash EXPOSED_CLASS = StringHash("ed-mixer-exposed");
		constexpr StringHash EXPOSED_ROW_CLASS = StringHash("ed-mixer-exposed-row");
		constexpr StringHash VIEW_ADD_CLASS = StringHash("ed-mixer-view-add");
		constexpr StringHash EYE_CLASS = StringHash("ed-mixer-eye");
		constexpr StringHash COLOR_TAG_CLASS = StringHash("ed-mixer-color");

		constexpr StringHash VIEW_PAYLOAD = StringHash("AudioMixerView");

		constexpr StringHash MIXER_ADD_CLASS = StringHash("ed-mixer-mixer-add");
		constexpr StringHash MIXER_PAYLOAD = StringHash("AudioMixerAsset");

		constexpr std::string_view COLOR_LABELS[] = {"No Color", "Yellow", "Orange", "Red", "Magenta", "Violet", "Blue", "Cyan", "Green"};
		constexpr ColorToken COLOR_TOKENS[] = {ColorToken::AudioGroupYellow, ColorToken::AudioGroupOrange, ColorToken::AudioGroupRed,
											   ColorToken::AudioGroupMagenta, ColorToken::AudioGroupViolet, ColorToken::AudioGroupBlue,
											   ColorToken::AudioGroupCyan, ColorToken::AudioGroupGreen};

		constexpr std::string_view EXPOSED_MARK = " \xE2\x86\x92";

		constexpr std::string_view TRANSITION_LABELS[] = {"Linear Snapshot Transition", "Smoothstep Snapshot Transition",
														  "Squared Snapshot Transition", "SquareRoot Snapshot Transition",
														  "BrickwallStart Snapshot Transition", "BrickwallEnd Snapshot Transition",
														  "Attenuation Snapshot Transition"};

		struct SharedState {
			struct PerMixer {
				Guid mixer;
				str snapshot;
				u32 view = 0;
			};
			Vector<PerMixer> mixers{GetMemoryResource(MemTag::Editor)};
			bool editInPlayMode = false;
		};

		SharedState* _shared = nullptr;

		SharedState& Shared() {
			if (!_shared) {
				_shared = New<SharedState>(MemTag::Editor);
			}
			return *_shared;
		}

		bool EditInPlayMode() { return _shared && _shared->editInPlayMode; }

		const SharedState::PerMixer* Find(const Guid& guid) {
			if (_shared) {
				for (const SharedState::PerMixer& state : _shared->mixers) {
					if (state.mixer == guid) {
						return &state;
					}
				}
			}
			return nullptr;
		}

		SharedState::PerMixer& Own(const Guid& guid) {
			for (SharedState::PerMixer& state : Shared().mixers) {
				if (state.mixer == guid) {
					return state;
				}
			}
			Shared().mixers.push_back(SharedState::PerMixer{guid, {}, 0});
			return Shared().mixers.back();
		}

		u32 TargetOf(const Guid& guid, const AudioMixer& mixer) {
			if (const SharedState::PerMixer* state = Find(guid)) {
				const u32 at = Ops::FindSnapshot(mixer, state->snapshot);
				if (at < mixer.snapshots.size()) {
					return at;
				}
			}
			return AudioImpl::StartSnapshotIndex(mixer);
		}

		void SetTargetOf(const Guid& guid, std::string_view snapshot) { Own(guid).snapshot = str(snapshot); }

		u32 ViewOf(const Guid& guid, const AudioMixer& mixer) {
			const SharedState::PerMixer* state = Find(guid);
			return state ? std::min(state->view, Ops::CountViews(mixer) - 1) : 0u;
		}

		void SetViewOf(const Guid& guid, u32 view) { Own(guid).view = view; }

		constexpr f32 HANDLE_HEIGHT = 14.0f;
		constexpr f32 SCALE_WIDTH = 22.0f;
		constexpr f32 FADER_WIDTH = 26.0f;

		constexpr f32 READOUT_SECONDS = 0.1f;

		constexpr f32 LABEL_WIDTH = 86.0f;
		constexpr f32 UNIT_METER_HEIGHT = 6.0f;

		u64 RowId(std::string_view group) { return StringHash(group).GetValue(); }

		u64 ViewRow(u32 view) { return static_cast<u64>(view) + 1u; }

		u64 MixerRowId(const Guid& guid) {
			const u64 id = StringHash(guid.ToString()).GetValue();
			return id != 0 ? id : 1u;
		}

		str JoinMixers(std::span<const Guid> mixers) {
			str out;
			for (const Guid& guid : mixers) {
				out += guid.ToString() + "|";
			}
			return out;
		}

		Vector<Guid> SplitMixers(const UI::DragPayload& payload) {
			Vector<Guid> out{GetMemoryResource(MemTag::Editor)};
			const str* joined = payload.type == MIXER_PAYLOAD ? payload.data.TryGet<str>() : nullptr;
			for (size_t from = 0; joined && from < joined->size();) {
				const size_t bar = joined->find('|', from);
				if (bar == str::npos) {
					break;
				}
				if (const Guid guid = Guid::FromString(joined->substr(from, bar - from)); guid.IsValid()) {
					out.push_back(guid);
				}
				from = bar + 1;
			}
			return out;
		}

		bool IsPlaying() { return AudioEditor::GetPlayModeState() != PlayModeState::Editing; }

		bool EditingValues() { return !IsPlaying() || EditInPlayMode(); }

		f32 Scale(const UI::Context& context) { return context.GetContentScale() > 0.0f ? context.GetContentScale() : 1.0f; }

		f32 ShareToY(const UI::Rect& box, f32 handle, f32 share) { return box.y + handle * 0.5f + share * (box.h - handle); }

		UI::ElementHandle AddText(UI::Context& context, UI::ElementHandle parent, std::string_view text, ColorToken color,
								  MetricToken size = MetricToken::FontSmall) {
			UI::ElementHandle element = context.CreateElement();
			context.AppendChild(parent, element);
			context.SetText(element, UISystem::GetDefaultFont(), EditorTheme::GetMetric(size), EditorTheme::GetColor(color), text);
			context.SetHoverable(element, false);
			return element;
		}

		UI::ElementHandle AddBox(UI::Context& context, UI::ElementHandle parent, const UI::Style& style) {
			UI::ElementHandle element = context.CreateElement();
			context.AppendChild(parent, element);
			context.SetStyle(element, style);
			return element;
		}

		std::string_view UnitLabel(const AudioMixer::Unit& unit) {
			switch (unit.kind) {
			case AudioMixerUnitKind::Attenuation:
				return "Attenuation";
			case AudioMixerUnitKind::Send:
				return "Send";
			case AudioMixerUnitKind::Receive:
				return "Receive";
			case AudioMixerUnitKind::Effect:
				break;
			}
			return unit.effect;
		}

		str TargetLabel(const AudioMixer& mixer, std::string_view group, std::string_view unit) {
			const i32 at = AudioImpl::FindGroup(mixer, group);
			str name = at >= 0 ? mixer.groups[static_cast<size_t>(at)].name : str(group);
			if (unit.empty()) {
				return name;
			}
			return std::format("{} \xE2\x80\xBA {}", name, unit);
		}

		void MakeInert(UI::Context& context, UI::ElementHandle root) {
			context.AddClass(root, EditorTheme::Class::Disabled);
			Vector<UI::ElementHandle> pending{GetMemoryResource(MemTag::Editor)};
			pending.push_back(root);
			while (!pending.empty()) {
				const UI::ElementHandle element = pending.back();
				pending.pop_back();
				context.SetHoverable(element, false);
				const UI::Element* data = context.GetElement(element);
				for (u32 child = data ? data->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
					const UI::ElementHandle handle = context.MakeHandle(child);
					pending.push_back(handle);
					const UI::Element* next = context.GetElement(handle);
					child = next ? next->nextSibling : UI::INVALID_UI_INDEX;
				}
			}
		}

		u64 ShapeOf(const AudioMixer& mixer, u32 view) {
			u64 hash = StringHash("shape").GetValue();
			auto mix = [&hash](std::string_view text) {
				hash ^= StringHash(text).GetValue() + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
			};
			hash += view * 131u;
			for (const AudioMixer::Group& group : mixer.groups) {
				mix(group.id);
				mix(group.name);
				mix(group.parent);
				hash += static_cast<u64>(group.color) * 7u + (Ops::IsVisible(mixer, view, group.id) ? 3u : 0u);
				for (const AudioMixer::Unit& unit : group.units) {
					mix(UnitLabel(unit));
					mix(unit.id);
					mix(unit.target);
					mix(unit.targetUnit);
					hash += unit.wetMixing ? 1u : 0u;
				}
			}
			return hash;
		}

	} // anonymous namespace

	class AudioMixerWindow : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(AudioMixerWindow)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Audio Mixer")
		SLT_EDITOR_PANEL_CATEGORY("Audio")
		SLT_REFLECT_FIELD(groupsWidth)
		SLT_REFLECT_FIELD(chainWidth)
		SLT_REFLECT_FIELD(snapshotsHeight)
		SLT_REFLECT_FIELD(viewsHeight)
		SLT_REFLECT_FIELD(mixersHeight)
		SLT_END_REFLECT()

		AudioMixerWindow() = default;

		f32 groupsWidth = 0.0f;
		f32 chainWidth = 0.0f;
		f32 snapshotsHeight = 0.0f;
		f32 viewsHeight = 0.0f;
		f32 mixersHeight = 0.0f;

		void OnOpen() override;
		void OnClose() override;
		void OnTick(f32 deltaSeconds) override;
		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
	private:
		struct Strip {
			str id;
			UI::ElementHandle header;
			UI::ElementHandle fader;
			UI::ElementHandle value;
			UI::ElementHandle readout;
			UI::ElementHandle solo;
			UI::ElementHandle mute;
			UI::ElementHandle bypass;
			Vector<UI::ElementHandle> dots{GetMemoryResource(MemTag::Editor)};
		};

		struct Control {
			Ops::ParameterRef ref;
			UI::Widgets::SliderParts slider;
			f32 scale = 1.0f;
		};

		Guid _guid{};
		Handle<AudioMixer> _mixer;
		Guid _followed{};
		bool _holding = false;
		PlayModeState _seenPlay = PlayModeState::Editing;

		Vector<str> _selected{GetMemoryResource(MemTag::Editor)};
		Vector<str> _collapsed{GetMemoryResource(MemTag::Editor)};
		str _renaming;
		bool _renameNeedsFocus = false;
		str _dragGroup;
		f32 _dragStartY = 0.0f;
		f32 _dragStartShare = 0.0f;
		str _slotGroup;
		u32 _slotUnit = 0;
		f32 _slotStartX = 0.0f;
		f32 _slotStartValue = 0.0f;
		bool _metering = false;

		u64 _shape = 0;
		bool _needsRebuild = true;
		f32 _readoutClock = 0.0f;

		UI::ElementHandle _title;
		UI::ElementHandle _editInPlay;
		UI::ElementHandle _exposed;
		UI::ElementHandle _save;
		UI::ElementHandle _exposedPopup;
		i32 _renamingExposed = -1;
		bool _exposedNeedsRebuild = false;
		UI::ElementHandle _add;
		UI::ElementHandle _snapshotAdd;
		EditorListView _snapshots;
		EditorInsertStrips _snapshotGaps;
		Vector<ListRow> _snapshotRows{GetMemoryResource(MemTag::Editor)};
		u64 _snapshotShape = 0;
		str _renamingSnapshot;
		bool _snapshotRenameNeedsFocus = false;
		UI::ElementHandle _viewAdd;
		EditorListView _views;
		EditorInsertStrips _viewGaps;
		Vector<ListRow> _viewRows{GetMemoryResource(MemTag::Editor)};
		u64 _viewShape = 0;
		i32 _renamingView = -1;
		bool _viewRenameNeedsFocus = false;
		struct Listed {
			Guid guid;
			str relPath;
			AudioMixer* read = nullptr;
		};
		UI::ElementHandle _mixerAdd;
		EditorListView _mixers;
		EditorInsertStrips _mixerGaps;
		Vector<ListRow> _mixerRows{GetMemoryResource(MemTag::Editor)};
		Vector<Listed> _listed{GetMemoryResource(MemTag::Editor)};
		bool _mixersNeedScan = true;
		f32 _mixerScanDelay = -1.0f;
		u64 _mixerShape = 0;
		Guid _renamingMixer{};
		bool _creatingMixer = false;
		bool _mixerNameNeedsFocus = false;
		Vector<Guid> _foldedMixers{GetMemoryResource(MemTag::Editor)};
		UI::ElementHandle _confirm;
		UI::ElementHandle _strips;
		Vector<Strip> _parts{GetMemoryResource(MemTag::Editor)};
		EditorListView _groups;
		EditorInsertStrips _gaps;
		Vector<ListRow> _rows{GetMemoryResource(MemTag::Editor)};
		UI::ElementHandle _chain;
		str _chainGroup;
		u64 _chainShape = 0;
		Vector<Control> _controls{GetMemoryResource(MemTag::Editor)};

		AudioMixer* Mixer() const { return _mixer.Get(); }
		void Follow();
		void Adopt(const Guid& guid, AudioMixer* loaded);
		void Release();
		void Edited(bool structural);
		void Save();

		u32 Target() const;
		bool Retarget(u32 snapshot);
		void PlaceMix();
		void SyncHold();
		void RefreshEditInPlay(UI::Context& context);

		u64 SnapshotShapeOf(const AudioMixer* mixer) const;
		void RebuildSnapshots(UI::Context& context);
		void OpenSnapshotMenu(UI::Context& context, const str& snapshot);
		void AddSnapshot();
		void DuplicateSnapshot(const str& snapshot);
		void RemoveSnapshot(const str& snapshot);
		bool BeginSnapshotRename(const str& snapshot);
		str SnapshotOfRow(u64 row) const;

		u32 CurrentView() const;
		void Restyled();
		u64 ViewShapeOf(const AudioMixer* mixer) const;
		void RebuildViews(UI::Context& context);
		void OpenViewMenu(UI::Context& context, u32 view);
		void DuplicateView(u32 view, bool rename);
		void RemoveView(u32 view);
		bool BeginViewRename(u32 view);
		Vector<str> Affected(const str& group) const;
		void ToggleVisible(const str& group);
		void AddColorItems(Vector<UI::Widgets::MenuItem>& items, const str& group, bool separate);
		void OpenEyeMenu(UI::Context& context, const str& group);
		void OpenStripMenu(UI::Context& context, const str& group);

		void ScanMixers();
		void ForgetMixers();
		void ListMixers(Vector<Ops::MixerListing>& out) const;
		u64 MixerShapeOf() const;
		void RebuildMixers(UI::Context& context);
		Vector<Guid> SelectedMixers() const;
		void OpenMixerMenu(UI::Context& context, const Guid& mixer);
		void OpenRouteMenu(UI::Context& context, const Vector<Guid>& dragged, const Guid& onto);
		void Route(std::span<const Guid> dragged, const Guid& onto, std::string_view group);
		void CreateMixer(std::string_view name);
		void RenameMixer(const Guid& mixer, std::string_view name);
		void DeleteMixers(std::span<const Guid> doomed);
		void DuplicateMixers(std::span<const Guid> sources);
		void CloseConfirm();

		void RebuildRows(UI::Context& context);
		void PushSelectionToList();
		void TakeSelectionFromList();
		void OpenGroupMenu(UI::Context& context, const str& group);
		void AddChild(const str& parent);
		void RemoveSelected();
		void DuplicateSelected();
		bool BeginRename(const str& group);
		str GroupOfRow(u64 row) const;

		void RebuildStrips(UI::Context& context);
		void BuildStrip(UI::Context& context, const AudioMixer& mixer, u32 index);
		void RefreshStrips(UI::Context& context);
		void Select(const str& group, bool add);
		void PaintMeters(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint, const str& group) const;
		void PaintScale(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint) const;
		void PaintFader(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint, const str& group) const;
		f32 FaderValue(const AudioMixer& mixer, const str& group) const;
		void DragFader(UI::Context& context, Vec2 point);
		void RefreshTitle(UI::Context& context);
		void PaintSlot(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint, const str& group, u32 unit) const;
		bool SlotLevel(const AudioMixer& mixer, const str& group, u32 unit, Ops::ParameterRef& ref) const;
		void DragSlot(UI::Context& context, Vec2 point);

		u64 ChainShapeOf(const AudioMixer* mixer) const;
		void RebuildChain(UI::Context& context);
		void BuildUnit(UI::Context& context, const AudioMixer& mixer, u32 group, u32 index, bool inert);
		UI::ElementHandle AddValueRow(UI::Context& context, UI::ElementHandle parent, std::string_view label, Ops::ParameterRef ref);
		void RefreshChain(UI::Context& context);
		void PaintUnitMeter(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint, const str& group) const;
		void EditValue(const Ops::ParameterRef& ref, f32 value);
		f32 ShownValue(const AudioMixer& mixer, const Ops::ParameterRef& ref) const;
		void OpenUnitMenu(UI::Context& context, const str& group, u32 index, bool strip);
		void OpenAddMenu(UI::Context& context, const str& group, u32 at);
		void OpenParameterMenu(UI::Context& context, const Ops::ParameterRef& ref);

		void RefreshExposed(UI::Context& context);
		void OpenExposedList(UI::Context& context);
		void BuildExposedRows(UI::Context& context);
		void CloseExposedList();
		void OpenSendMenu(UI::Context& context, const str& group, u32 index);
		Vector<UI::Widgets::MenuItem> AddEffectItems(std::span<const str> groups, u32 at);
		void MoveUnit(const str& group, u32 from, u32 to);
	};

	void AudioMixerWindow::OnOpen() {
		if (!_metering) {
			AudioImpl::GetAudioSystem().AcquireMetering();
			_metering = true;
		}
	}

	void AudioMixerWindow::OnClose() {
		if (_metering) {
			AudioImpl::GetAudioSystem().ReleaseMetering();
			_metering = false;
		}
		if (_exposedPopup.IsValid()) {
			CloseExposedList();
		}
		if (_confirm.IsValid()) {
			CloseConfirm();
		}
		ForgetMixers();
		Release();
	}

	void AudioMixerWindow::Release() {
		if (AudioMixer* held = Mixer()) {
			if (_holding) {
				Ops::ReleaseHold(*held);
			}
			AssetDatabase::UnloadAsset(held);
		}
		_holding = false;
		_mixer = {};
		_guid = {};
	}

	void AudioMixerWindow::Follow() {
		const Guid& selected = EditorSelection::GetSelectedAsset();
		if (!selected.IsValid() || selected == _followed) {
			return;
		}
		_followed = selected;
		const Guid source = AssetDatabase::GetProductSource(selected);
		const Guid mixerGuid = source.IsValid() ? source : selected;
		if (!AssetDatabase::LookupPath(mixerGuid).ToString().ends_with(".mixer")) {
			return;
		}
		str group;
		if (source.IsValid()) {
			if (Asset* product = AssetDatabase::LoadAssetByGuid(selected)) {
				if (product->GetAssetTypeId() == AudioMixerGroup::GetReflectType()->name) {
					group = str(static_cast<AudioMixerGroup*>(product)->GetGroupId());
				}
				AssetDatabase::UnloadAsset(product);
			}
		}
		if (mixerGuid != _guid) {
			Asset* loaded = AssetDatabase::LoadAssetByGuid(mixerGuid);
			if (!loaded || loaded->GetAssetTypeId() != AudioMixer::GetReflectType()->name) {
				if (loaded) {
					AssetDatabase::UnloadAsset(loaded);
				}
				return;
			}
			Adopt(mixerGuid, static_cast<AudioMixer*>(loaded));
		}
		if (!group.empty()) {
			_selected.clear();
			_selected.push_back(group);
			PushSelectionToList();
		}
	}

	void AudioMixerWindow::Adopt(const Guid& guid, AudioMixer* loaded) {
		Release();
		_guid = guid;
		_mixer = Handle<AudioMixer>(loaded);
		_selected.clear();
		_collapsed.clear();
		_renaming.clear();
		_renamingSnapshot.clear();
		_renamingView = -1;
		_dragGroup.clear();
		_needsRebuild = true;
		_snapshotShape = 0;
		_viewShape = 0;
		_mixerShape = 0;
		PlaceMix();
	}

	u32 AudioMixerWindow::Target() const {
		const AudioMixer* mixer = Mixer();
		return mixer ? TargetOf(_guid, *mixer) : 0u;
	}

	bool AudioMixerWindow::Retarget(u32 snapshot) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || snapshot >= mixer->snapshots.size() || !EditingValues()) {
			return false;
		}
		SetTargetOf(_guid, mixer->snapshots[snapshot].id);
		PlaceMix();
		return true;
	}

	void AudioMixerWindow::PlaceMix() {
		AudioMixer* mixer = Mixer();
		if (!mixer) {
			return;
		}
		const u32 target = Target();
		if (!IsPlaying()) {
			if (target < mixer->snapshots.size()) {
				mixer->TransitionToSnapshot(mixer->snapshots[target].id, 0.0f);
			}
		} else if (_holding) {
			Ops::RefreshHold(*mixer, target);
		}
	}

	void AudioMixerWindow::SyncHold() {
		AudioMixer* mixer = Mixer();
		const bool want = mixer && IsPlaying() && EditInPlayMode();
		if (want && !_holding) {
			_holding = Ops::AcquireHold(*mixer, Target());
		} else if (!want && _holding) {
			if (mixer) {
				Ops::ReleaseHold(*mixer);
			}
			_holding = false;
		}
	}

	void AudioMixerWindow::Edited(bool structural) {
		AudioMixer* mixer = Mixer();
		if (!mixer) {
			return;
		}
		mixer->ApplyChanges();
		if (EditingValues()) {
			PlaceMix();
		}
		EditorAssetOps::MarkDirty(Handle<Asset>(mixer));
		if (structural) {
			_needsRebuild = true;
		}
		if (UI::Context* context = GetPanelContext()) {
			RefreshTitle(*context);
		}
	}

	void AudioMixerWindow::Save() {
		if (AudioMixer* mixer = Mixer()) {
			EditorAssetOps::SaveAsset(Handle<Asset>(mixer));
			if (UI::Context* context = GetPanelContext()) {
				RefreshTitle(*context);
			}
		}
	}

	void AudioMixerWindow::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(root, rootStyle);

		UI::Style barStyle;
		barStyle.flexDirection = UI::FlexDirection::Row;
		barStyle.alignItems = UI::Align::Center;
		barStyle.flexShrink = 0.0f;
		barStyle.padding = {6.0f, 4.0f, 6.0f, 4.0f};
		barStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::HeaderFill));
		UI::ElementHandle bar = AddBox(context, root, barStyle);
		_title = AddText(context, bar, "", ColorToken::TextStrong, MetricToken::FontBody);
		UI::Widgets::Spacer(context, bar);
		UI::Style toggleStyle;
		toggleStyle.flexDirection = UI::FlexDirection::Row;
		toggleStyle.alignItems = UI::Align::Center;
		toggleStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		toggleStyle.margin = {0.0f, 0.0f, 6.0f, 0.0f};
		toggleStyle.borderRadius = UI::Radii::All(2.0f);
		toggleStyle.display = UI::Display::None;
		_editInPlay = AddBox(context, bar, toggleStyle);
		context.AddClass(_editInPlay, EDIT_IN_PLAY_CLASS);
		context.AddClass(_editInPlay, EditorTheme::Class::Clickable);
		AddText(context, _editInPlay, "Edit in Play Mode", ColorToken::Text);
		context.SetTooltip(_editInPlay, "Edit in play mode, keeping the changes. Note when editing is disabled, live values are shown.");
		context.Events(_editInPlay).onClick = []() {
			if (IsPlaying()) {
				Shared().editInPlayMode = !Shared().editInPlayMode;
			}
		};
		UI::Style exposedStyle = toggleStyle;
		exposedStyle.display = UI::Display::Flex;
		exposedStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
		_exposed = AddBox(context, bar, exposedStyle);
		context.AddClass(_exposed, EXPOSED_CLASS);
		context.AddClass(_exposed, EditorTheme::Class::Clickable);
		AddText(context, _exposed, "Exposed Parameters (0)", ColorToken::Text);
		context.SetTooltip(_exposed, "Audio Mixer parameters can be exposed to scripting. Select an Audio Mixer Group, right click one of its "
									 "properties in the chain beside the strips and select 'Expose'.");
		context.Events(_exposed).onClick = [this]() {
			if (UI::Context* c = GetPanelContext(); c && Mixer()) {
				OpenExposedList(*c);
			}
		};
		UI::Widgets::ButtonDesc save;
		save.label = "Save";
		save.font = UISystem::GetDefaultFont();
		save.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
		save.onClick = [this]() { Save(); };
		_save = UI::Widgets::Button(context, bar, save);

		UI::Widgets::SplitDesc split;
		split.axis = UI::FlexDirection::Row;
		split.sizing = UI::Widgets::SplitSizing::FirstFixed;
		split.position = groupsWidth > 0.0f ? groupsWidth : EditorTheme::GetMetric(MetricToken::AudioMixerColumn);
		split.minFirstPx = 90.0f;
		split.minSecondPx = EditorTheme::GetMetric(MetricToken::AudioStripWidth);
		split.onPositionChanged = [this](f32 position) { groupsWidth = position; };
		UI::Widgets::SplitParts body = UI::Widgets::Split(context, root, split);
		UI::Style bodyStyle = context.GetStyle(body.root);
		bodyStyle.flexGrow = 1.0f;
		bodyStyle.minHeight = UI::Length::Pixel(0.0f);
		context.SetStyle(body.root, bodyStyle);

		UI::Widgets::SplitDesc mixerSection;
		mixerSection.axis = UI::FlexDirection::Column;
		mixerSection.sizing = UI::Widgets::SplitSizing::FirstFixed;
		mixerSection.position = mixersHeight > 0.0f ? mixersHeight : EditorTheme::GetMetric(MetricToken::AudioMixerMixers);
		mixerSection.minFirstPx = 40.0f;
		mixerSection.minSecondPx = 100.0f;
		mixerSection.onPositionChanged = [this](f32 position) { mixersHeight = position; };
		UI::Widgets::SplitParts top = UI::Widgets::Split(context, body.first, mixerSection);
		UI::Style topStyle = context.GetStyle(top.root);
		topStyle.flexGrow = 1.0f;
		topStyle.minHeight = UI::Length::Pixel(0.0f);
		context.SetStyle(top.root, topStyle);

		UI::Widgets::SplitDesc sections;
		sections.axis = UI::FlexDirection::Column;
		sections.sizing = UI::Widgets::SplitSizing::FirstFixed;
		sections.position = snapshotsHeight > 0.0f ? snapshotsHeight : EditorTheme::GetMetric(MetricToken::AudioMixerSnapshots);
		sections.minFirstPx = 40.0f;
		sections.minSecondPx = 60.0f;
		sections.onPositionChanged = [this](f32 position) { snapshotsHeight = position; };
		UI::Widgets::SplitParts left = UI::Widgets::Split(context, top.second, sections);
		UI::Style leftStyle = context.GetStyle(left.root);
		leftStyle.flexGrow = 1.0f;
		leftStyle.minHeight = UI::Length::Pixel(0.0f);
		context.SetStyle(left.root, leftStyle);

		UI::Style columnStyle;
		columnStyle.flexDirection = UI::FlexDirection::Column;
		columnStyle.flexGrow = 1.0f;
		columnStyle.minHeight = UI::Length::Pixel(0.0f);
		columnStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
		UI::Style headStyle;
		headStyle.flexDirection = UI::FlexDirection::Row;
		headStyle.alignItems = UI::Align::Center;
		headStyle.flexShrink = 0.0f;
		headStyle.padding = {6.0f, 3.0f, 4.0f, 3.0f};
		UI::Style listStyle;
		listStyle.flexDirection = UI::FlexDirection::Column;
		listStyle.flexGrow = 1.0f;
		listStyle.minHeight = UI::Length::Pixel(0.0f);

		UI::ElementHandle mixerColumn = AddBox(context, top.first, columnStyle);
		UI::ElementHandle mixerHead = AddBox(context, mixerColumn, headStyle);
		UI::ElementHandle mixerTitle = AddText(context, mixerHead, "Mixers", ColorToken::TextDim);
		context.SetTooltip(mixerTitle, "All mixers in the project shown here. By default, a mixer outputs to the AudioListener (but mixers can also "
									   "route their output to other mixers). Each mixer shows where it outputs (in parenthesis). To reroute a mixer "
									   "drag the mixer upon another mixer and select a group from the popup.");
		UI::Widgets::Spacer(context, mixerHead);
		UI::Widgets::ButtonDesc addMixer;
		addMixer.label = "+";
		addMixer.font = UISystem::GetDefaultFont();
		addMixer.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
		addMixer.onClick = [this]() {
			_creatingMixer = true;
			_mixerNameNeedsFocus = true;
			_mixerShape = 0;
		};
		_mixerAdd = UI::Widgets::Button(context, mixerHead, addMixer);
		context.AddClass(_mixerAdd, MIXER_ADD_CLASS);
		context.SetTooltip(_mixerAdd, "Add mixer asset. The asset will be saved in the same folder as the current selected mixer (or in the Assets folder, if none "
									  "selected).");
		UI::ElementHandle mixerHost = AddBox(context, mixerColumn, listStyle);
		context.Events(mixerHost).onDragOver = [](const UI::DragPayload& payload) { return payload.type == MIXER_PAYLOAD && !IsPlaying(); };
		context.Events(mixerHost).onDrop = [this](const UI::DragPayload& payload, Vec2) {
			const Vector<Guid> dragged = SplitMixers(payload);
			Route(dragged, Guid{}, {});
		};
		ListViewDesc mixerList;
		mixerList.layout = ListLayout::Rows;
		mixerList.mode = ListSelectionMode::Multiple;
		mixerList.onSelectionChanged = [this]() {
			const Vector<Guid> picked = SelectedMixers();
			if (!picked.empty()) {
				EditorSelection::SelectAssets(picked);
			}
		};
		mixerList.onDelete = [this]() { DeleteMixers(SelectedMixers()); };
		mixerList.onDuplicate = [this]() { DuplicateMixers(SelectedMixers()); };
		mixerList.onRename = [this](u64 row) {
			for (const Listed& listed : _listed) {
				if (MixerRowId(listed.guid) == row) {
					_renamingMixer = listed.guid;
					_mixerNameNeedsFocus = true;
					_mixerShape = 0;
					return true;
				}
			}
			return false;
		};
		mixerList.onBuildTrailing = [this](UI::Context& c, UI::ElementHandle host) { _mixerGaps.DrainInto(c, host); };
		_mixers.Build(this, context, mixerHost, std::move(mixerList));

		UI::ElementHandle snapshotColumn = AddBox(context, left.first, columnStyle);
		UI::ElementHandle snapshotHead = AddBox(context, snapshotColumn, headStyle);
		UI::ElementHandle snapshotTitle = AddText(context, snapshotHead, "Snapshots", ColorToken::TextDim);
		context.SetTooltip(snapshotTitle, "Snapshot is a set of values for all parameters in the mixer. When using the mixer, you modify parameters in the "
										  "selected snapshot. Blend between multiple snapshots at runtime.");
		UI::Widgets::Spacer(context, snapshotHead);
		UI::Widgets::ButtonDesc addSnapshot;
		addSnapshot.label = "+";
		addSnapshot.font = UISystem::GetDefaultFont();
		addSnapshot.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
		addSnapshot.onClick = [this]() { AddSnapshot(); };
		_snapshotAdd = UI::Widgets::Button(context, snapshotHead, addSnapshot);
		context.AddClass(_snapshotAdd, SNAPSHOT_ADD_CLASS);
		context.SetTooltip(_snapshotAdd, "Add a snapshot copied from the selected one");
		UI::ElementHandle snapshotHost = AddBox(context, snapshotColumn, listStyle);
		ListViewDesc snapshotList;
		snapshotList.layout = ListLayout::Rows;
		snapshotList.mode = ListSelectionMode::Single;
		snapshotList.onSelectionChanged = [this]() {
			const std::span<const u64> picked = _snapshots.GetSelected();
			const AudioMixer* mixer = Mixer();
			const u32 at = mixer && !picked.empty() ? Ops::FindSnapshot(*mixer, SnapshotOfRow(picked.back())) : ~0u;
			if (!Retarget(at)) {
				_snapshotShape = 0;
			}
		};
		snapshotList.onDelete = [this]() {
			if (const AudioMixer* mixer = Mixer(); mixer && Target() < mixer->snapshots.size()) {
				RemoveSnapshot(mixer->snapshots[Target()].id);
			}
		};
		snapshotList.onDuplicate = [this]() {
			if (const AudioMixer* mixer = Mixer(); mixer && Target() < mixer->snapshots.size()) {
				DuplicateSnapshot(mixer->snapshots[Target()].id);
			}
		};
		snapshotList.onRename = [this](u64 row) { return BeginSnapshotRename(SnapshotOfRow(row)); };
		snapshotList.onBuildTrailing = [this](UI::Context& c, UI::ElementHandle host) { _snapshotGaps.DrainInto(c, host); };
		_snapshots.Build(this, context, snapshotHost, std::move(snapshotList));

		UI::Widgets::SplitDesc lowerSections;
		lowerSections.axis = UI::FlexDirection::Column;
		lowerSections.sizing = UI::Widgets::SplitSizing::SecondFixed;
		lowerSections.position = viewsHeight > 0.0f ? viewsHeight : EditorTheme::GetMetric(MetricToken::AudioMixerViews);
		lowerSections.minFirstPx = 60.0f;
		lowerSections.minSecondPx = 40.0f;
		lowerSections.onPositionChanged = [this](f32 position) { viewsHeight = position; };
		UI::Widgets::SplitParts lower = UI::Widgets::Split(context, left.second, lowerSections);
		UI::Style lowerStyle = context.GetStyle(lower.root);
		lowerStyle.flexGrow = 1.0f;
		lowerStyle.minHeight = UI::Length::Pixel(0.0f);
		context.SetStyle(lower.root, lowerStyle);

		UI::ElementHandle column = AddBox(context, lower.first, columnStyle);
		UI::ElementHandle head = AddBox(context, column, headStyle);
		AddText(context, head, "Groups", ColorToken::TextDim);
		UI::Widgets::Spacer(context, head);
		UI::Widgets::ButtonDesc add;
		add.label = "+";
		add.font = UISystem::GetDefaultFont();
		add.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
		add.onClick = [this]() {
			const AudioMixer* mixer = Mixer();
			if (!mixer || IsPlaying()) {
				return;
			}
			const i32 top = Ops::FindTop(*mixer);
			if (!_selected.empty()) {
				AddChild(_selected.back());
			} else if (top >= 0) {
				AddChild(mixer->groups[static_cast<size_t>(top)].id);
			}
		};
		_add = UI::Widgets::Button(context, head, add);
		context.AddClass(_add, GROUP_ADD_CLASS);
		context.SetTooltip(_add, "Add a child group to the selected group");

		UI::ElementHandle listHost = AddBox(context, column, listStyle);
		ListViewDesc list;
		list.layout = ListLayout::Rows;
		list.mode = ListSelectionMode::Multiple;
		list.onSelectionChanged = [this]() { TakeSelectionFromList(); };
		list.onDelete = [this]() { RemoveSelected(); };
		list.onDuplicate = [this]() { DuplicateSelected(); };
		list.onRename = [this](u64 row) { return BeginRename(GroupOfRow(row)); };
		list.onBuildTrailing = [this](UI::Context& c, UI::ElementHandle host) { _gaps.DrainInto(c, host); };
		_groups.Build(this, context, listHost, std::move(list));

		UI::ElementHandle viewColumn = AddBox(context, lower.second, columnStyle);
		UI::ElementHandle viewHead = AddBox(context, viewColumn, headStyle);
		UI::ElementHandle viewTitle = AddText(context, viewHead, "Views", ColorToken::TextDim);
		context.SetTooltip(viewTitle, "View is the saved visibility state of the current Mixer Groups. Use views to setup often used combinations of "
									  "Mixer Groups.");
		UI::Widgets::Spacer(context, viewHead);
		UI::Widgets::ButtonDesc addView;
		addView.label = "+";
		addView.font = UISystem::GetDefaultFont();
		addView.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
		addView.onClick = [this]() { DuplicateView(CurrentView(), true); };
		_viewAdd = UI::Widgets::Button(context, viewHead, addView);
		context.AddClass(_viewAdd, VIEW_ADD_CLASS);
		context.SetTooltip(_viewAdd, "Add a view copied from the selected one");
		UI::ElementHandle viewHost = AddBox(context, viewColumn, listStyle);
		ListViewDesc viewList;
		viewList.layout = ListLayout::Rows;
		viewList.mode = ListSelectionMode::Single;
		viewList.onSelectionChanged = [this]() {
			const std::span<const u64> picked = _views.GetSelected();
			const AudioMixer* mixer = Mixer();
			if (mixer && !picked.empty() && picked.back() >= 1 && picked.back() - 1 < Ops::CountViews(*mixer)) {
				SetViewOf(_guid, static_cast<u32>(picked.back() - 1));
			}
			_viewShape = 0;
		};
		viewList.onDelete = [this]() { RemoveView(CurrentView()); };
		viewList.onDuplicate = [this]() { DuplicateView(CurrentView(), false); };
		viewList.onRename = [this](u64 row) { return BeginViewRename(static_cast<u32>(row - 1)); };
		viewList.onBuildTrailing = [this](UI::Context& c, UI::ElementHandle host) { _viewGaps.DrainInto(c, host); };
		_views.Build(this, context, viewHost, std::move(viewList));

		UI::Widgets::SplitDesc inner;
		inner.axis = UI::FlexDirection::Row;
		inner.sizing = UI::Widgets::SplitSizing::SecondFixed;
		inner.position = chainWidth > 0.0f ? chainWidth : EditorTheme::GetMetric(MetricToken::AudioChainColumn);
		inner.minFirstPx = EditorTheme::GetMetric(MetricToken::AudioStripWidth);
		inner.minSecondPx = 160.0f;
		inner.onPositionChanged = [this](f32 position) { chainWidth = position; };
		UI::Widgets::SplitParts right = UI::Widgets::Split(context, body.second, inner);
		UI::Style rightStyle = context.GetStyle(right.root);
		rightStyle.flexGrow = 1.0f;
		rightStyle.minHeight = UI::Length::Pixel(0.0f);
		context.SetStyle(right.root, rightStyle);

		UI::Style stripsStyle;
		stripsStyle.flexDirection = UI::FlexDirection::Row;
		stripsStyle.alignItems = UI::Align::Start;
		stripsStyle.flexGrow = 1.0f;
		stripsStyle.width = UI::Length::Percent(1.0f);
		stripsStyle.height = UI::Length::Percent(1.0f);
		stripsStyle.overflow = UI::Overflow::Scroll;
		stripsStyle.padding = {6.0f, 6.0f, 6.0f, 6.0f};
		_strips = AddBox(context, right.first, stripsStyle);

		UI::Style chainStyle;
		chainStyle.flexDirection = UI::FlexDirection::Column;
		chainStyle.flexGrow = 1.0f;
		chainStyle.width = UI::Length::Percent(1.0f);
		chainStyle.height = UI::Length::Percent(1.0f);
		chainStyle.overflow = UI::Overflow::Scroll;
		chainStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
		_chain = AddBox(context, right.second, chainStyle);
		context.AddClass(_chain, CHAIN_CLASS);

		_needsRebuild = true;
		_chainShape = 0;
		_snapshotShape = 0;
		_viewShape = 0;
		_mixerShape = 0;
		RefreshTitle(context);
	}

	void AudioMixerWindow::RefreshTitle(UI::Context& context) {
		if (!context.IsAlive(_title)) {
			return;
		}
		const AudioMixer* mixer = Mixer();
		str text = "No Audio Mixer selected";
		if (mixer) {
			const str path = AssetDatabase::LookupPath(_guid).ToString();
			const size_t slash = path.find_last_of('/');
			text = path.substr(slash == str::npos ? 0 : slash + 1);
			if (EditorAssetOps::IsDirty(Handle<Asset>(const_cast<AudioMixer*>(mixer)))) {
				text += " *";
			}
		}
		context.SetText(_title, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontBody),
						EditorTheme::GetColor(mixer ? ColorToken::TextStrong : ColorToken::TextDim), text);
	}

	void AudioMixerWindow::OnTick(f32 deltaSeconds) {
		Follow();
		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		if (_guid.IsValid() && (!Mixer() || AssetDatabase::LookupPath(_guid).ToString().empty())) {
			Release();
			_needsRebuild = true;
		}
		const PlayModeState play = AudioEditor::GetPlayModeState();
		SyncHold();
		if (play != _seenPlay) {
			_seenPlay = play;
			if (play == PlayModeState::Editing) {
				PlaceMix();
			}
		}
		RefreshEditInPlay(*context);
		RefreshExposed(*context);
		if (const u64 shape = SnapshotShapeOf(Mixer()); shape != _snapshotShape) {
			_snapshotShape = shape;
			RebuildSnapshots(*context);
		}
		if (const u64 shape = ViewShapeOf(Mixer()); shape != _viewShape) {
			_viewShape = shape;
			RebuildViews(*context);
		}
		if (const AudioMixer* mixer = Mixer(); mixer && ShapeOf(*mixer, CurrentView()) != _shape) {
			_needsRebuild = true;
		}
		if (_needsRebuild) {
			_needsRebuild = false;
			_shape = Mixer() ? ShapeOf(*Mixer(), CurrentView()) : 0;
			RebuildRows(*context);
			RebuildStrips(*context);
			RefreshTitle(*context);
		}
		_readoutClock += deltaSeconds;
		RefreshStrips(*context);
		if (const u64 shape = ChainShapeOf(Mixer()); shape != _chainShape) {
			_chainShape = shape;
			RebuildChain(*context);
		}
		RefreshChain(*context);
		if (context->IsAlive(_add)) {
			if (IsPlaying() || !Mixer()) {
				context->AddClass(_add, EditorTheme::Class::Disabled);
			} else {
				context->RemoveClass(_add, EditorTheme::Class::Disabled);
			}
		}
		_groups.Tick(deltaSeconds);
		_gaps.SyncHits(*context);
		_snapshots.Tick(deltaSeconds);
		_snapshotGaps.SyncHits(*context);
		_views.Tick(deltaSeconds);
		_viewGaps.SyncHits(*context);

		for (const Path& changed : EditorAssetWatch::GetChangedThisFrame()) {
			if (changed.ToString().ends_with(".mixer")) {
				_mixerScanDelay = 0.6f;
			}
		}
		if (EditorAssetWatch::DidOverflowThisFrame()) {
			_mixerScanDelay = 0.6f;
		}
		if (_mixerScanDelay > 0.0f) {
			_mixerScanDelay -= deltaSeconds;
			_mixersNeedScan = _mixersNeedScan || _mixerScanDelay <= 0.0f;
		}
		if (_mixersNeedScan) {
			_mixersNeedScan = false;
			_mixerScanDelay = -1.0f;
			ScanMixers();
			_mixerShape = 0;
		}
		if (const u64 shape = MixerShapeOf(); shape != _mixerShape) {
			_mixerShape = shape;
			RebuildMixers(*context);
		}
		_mixers.Tick(deltaSeconds);
		_mixerGaps.SyncHits(*context);
		if (_confirm.IsValid() && (!context->IsAlive(_confirm) || !context->IsPopupOpen(_confirm))) {
			_confirm = {};
		}
	}

	void AudioMixerWindow::RefreshEditInPlay(UI::Context& context) {
		if (context.IsAlive(_editInPlay)) {
			UI::Style style = context.GetStyle(_editInPlay);
			style.display = IsPlaying() && Mixer() ? UI::Display::Flex : UI::Display::None;
			const bool on = EditInPlayMode();
			style.background = UI::Brush::SolidColor(EditorTheme::GetColor(on ? ColorToken::AudioEditingLive : ColorToken::SectionFill));
			context.SetStyle(_editInPlay, style);
			if (const UI::Element* toggle = context.GetElement(_editInPlay); toggle && toggle->firstChild != UI::INVALID_UI_INDEX) {
				context.SetText(context.MakeHandle(toggle->firstChild), UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
								EditorTheme::GetColor(on ? ColorToken::AudioToggleLitText : ColorToken::Text), "Edit in Play Mode");
			}
		}
		if (context.IsAlive(_snapshotAdd)) {
			if (EditingValues() && Mixer()) {
				context.RemoveClass(_snapshotAdd, EditorTheme::Class::Disabled);
			} else {
				context.AddClass(_snapshotAdd, EditorTheme::Class::Disabled);
			}
		}
		if (context.IsAlive(_viewAdd)) {
			if (Mixer()) {
				context.RemoveClass(_viewAdd, EditorTheme::Class::Disabled);
			} else {
				context.AddClass(_viewAdd, EditorTheme::Class::Disabled);
			}
		}
	}

	str AudioMixerWindow::SnapshotOfRow(u64 row) const {
		if (const AudioMixer* mixer = Mixer()) {
			for (const AudioMixer::Snapshot& snapshot : mixer->snapshots) {
				if (RowId(snapshot.id) == row) {
					return snapshot.id;
				}
			}
		}
		return {};
	}

	u64 AudioMixerWindow::SnapshotShapeOf(const AudioMixer* mixer) const {
		u64 hash = StringHash("snapshots").GetValue();
		auto mix = [&hash](u64 value) { hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2); };
		mix(StringHash(_guid.ToString()).GetValue());
		if (!mixer) {
			return hash;
		}
		for (const AudioMixer::Snapshot& snapshot : mixer->snapshots) {
			mix(StringHash(snapshot.id).GetValue());
			mix(StringHash(snapshot.name).GetValue());
		}
		mix(AudioImpl::StartSnapshotIndex(*mixer));
		mix(Target());
		mix(StringHash(_renamingSnapshot).GetValue());
		mix(EditingValues() ? 7u : 6u);
		return hash;
	}

	void AudioMixerWindow::RebuildSnapshots(UI::Context& context) {
		_snapshotRows.clear();
		_snapshotGaps.BeginBuild(context);
		const AudioMixer* mixer = Mixer();
		if (!mixer) {
			_snapshots.SetRows(_snapshotRows);
			return;
		}
		const u32 start = AudioImpl::StartSnapshotIndex(*mixer);
		const bool editing = EditingValues();
		for (u32 index = 0; index < mixer->snapshots.size(); ++index) {
			const AudioMixer::Snapshot& snapshot = mixer->snapshots[index];
			const str id = snapshot.id;

			EditorInsertStripDesc strip;
			strip.onDragOver = [this](const UI::DragPayload& payload) {
				return payload.type == SNAPSHOT_PAYLOAD && payload.data.TryGet<str>() && Mixer() && EditingValues();
			};
			strip.onDrop = [this, index](const UI::DragPayload& payload, Vec2) {
				const str* dragged = payload.type == SNAPSHOT_PAYLOAD ? payload.data.TryGet<str>() : nullptr;
				AudioMixer* live = Mixer();
				if (dragged && live && EditingValues() && Ops::MoveSnapshot(*live, Ops::FindSnapshot(*live, *dragged), index)) {
					Edited(false);
				}
			};
			_snapshotGaps.PendStrip(std::move(strip));

			if (_renamingSnapshot == id) {
				const str name = snapshot.name;
				_snapshotGaps.PendInFlow([this, id, name](UI::Context& c, UI::ElementHandle host) {
					UI::Style wrapStyle;
					wrapStyle.flexDirection = UI::FlexDirection::Row;
					wrapStyle.padding = {4.0f, 1.0f, 4.0f, 1.0f};
					UI::ElementHandle wrap = AddBox(c, host, wrapStyle);
					UI::Widgets::TextFieldDesc field;
					field.text = name;
					field.font = UISystem::GetDefaultFont();
					field.fontSizePx = EditorTheme::GetMetric(MetricToken::Font);
					field.onCommit = [this, id](std::string_view text) {
						AudioMixer* live = Mixer();
						const u32 at = live ? Ops::FindSnapshot(*live, id) : 0u;
						if (live && EditingValues() && Ops::RenameSnapshot(*live, at, text)) {
							Edited(false);
						}
						_renamingSnapshot.clear();
					};
					UI::ElementHandle input = UI::Widgets::TextField(c, wrap, field);
					if (_snapshotRenameNeedsFocus) {
						_snapshotRenameNeedsFocus = false;
						c.SetFocus(input);
					}
					auto chained = c.Events(input).onFocusLost;
					c.Events(input).onFocusLost = [this, chained]() {
						if (chained) {
							chained();
						}
						_renamingSnapshot.clear();
					};
				});
				continue;
			}

			ListRow row;
			row.id = RowId(id);
			row.label = snapshot.name;
			const bool isStart = index == start;
			row.onDecorate = [isStart, editing](UI::Context& c, const UI::Widgets::TreeRowParts& parts) {
				if (!editing) {
					c.AddClass(parts.row, EditorTheme::Class::Disabled);
				}
				if (isStart) {
					UI::Widgets::Spacer(c, parts.row);
					UI::ElementHandle star = EditorIcons::Add(c, parts.row, EditorIcons::Glyph::Star, EditorTheme::GetColor(ColorToken::TextDim));
					c.SetTooltip(star, "Start snapshot");
				}
			};
			row.onRightClick = [this, id](UI::Context& c) { OpenSnapshotMenu(c, id); };
			row.onDragRow = [this, id](Vec2) {
				if (UISystem::IsDragPayloadActive() || !EditingValues()) {
					return;
				}
				UI::DragPayload payload;
				payload.type = SNAPSHOT_PAYLOAD;
				payload.data = Variant(id);
				const AudioMixer* live = Mixer();
				const u32 at = live ? Ops::FindSnapshot(*live, id) : 0u;
				payload.display = live && at < live->snapshots.size() ? live->snapshots[at].name : id;
				UISystem::BeginDragPayload(payload);
			};
			row.onBuildLeading = _snapshotGaps.Take();
			_snapshotRows.push_back(std::move(row));
		}
		EditorInsertStripDesc end;
		end.onDragOver = [this](const UI::DragPayload& payload) {
			return payload.type == SNAPSHOT_PAYLOAD && payload.data.TryGet<str>() && Mixer() && EditingValues();
		};
		const u32 count = static_cast<u32>(mixer->snapshots.size());
		end.onDrop = [this, count](const UI::DragPayload& payload, Vec2) {
			const str* dragged = payload.type == SNAPSHOT_PAYLOAD ? payload.data.TryGet<str>() : nullptr;
			AudioMixer* live = Mixer();
			if (dragged && live && EditingValues() && Ops::MoveSnapshot(*live, Ops::FindSnapshot(*live, *dragged), count)) {
				Edited(false);
			}
		};
		_snapshotGaps.PendStrip(std::move(end));
		_snapshots.SetRows(_snapshotRows);
		const u64 target = Target() < mixer->snapshots.size() ? RowId(mixer->snapshots[Target()].id) : 0u;
		_snapshots.SetSelection(std::span<const u64>(&target, 1));
	}

	void AudioMixerWindow::OpenSnapshotMenu(UI::Context& context, const str& snapshot) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || !EditingValues() || Ops::FindSnapshot(*mixer, snapshot) >= mixer->snapshots.size()) {
			return;
		}
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		UI::Widgets::MenuItem start;
		start.label = "Set as start Snapshot";
		start.onSelect = [this, snapshot]() {
			AudioMixer* live = Mixer();
			if (live && EditingValues() && Ops::SetStartSnapshot(*live, Ops::FindSnapshot(*live, snapshot))) {
				Edited(false);
			}
		};
		items.push_back(std::move(start));
		UI::Widgets::MenuItem rename;
		rename.label = "Rename";
		rename.separatorBefore = true;
		rename.onSelect = [this, snapshot]() { BeginSnapshotRename(snapshot); };
		items.push_back(std::move(rename));
		UI::Widgets::MenuItem duplicate;
		duplicate.label = "Duplicate";
		duplicate.onSelect = [this, snapshot]() { DuplicateSnapshot(snapshot); };
		items.push_back(std::move(duplicate));
		UI::Widgets::MenuItem remove;
		remove.label = "Delete";
		remove.onSelect = [this, snapshot]() { RemoveSnapshot(snapshot); };
		items.push_back(std::move(remove));
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	void AudioMixerWindow::AddSnapshot() {
		const AudioMixer* mixer = Mixer();
		if (!mixer || !EditingValues() || Target() >= mixer->snapshots.size()) {
			return;
		}
		const str from = mixer->snapshots[Target()].id;
		DuplicateSnapshot(from);
		if (const AudioMixer* live = Mixer(); live && Target() < live->snapshots.size()) {
			BeginSnapshotRename(live->snapshots[Target()].id);
		}
	}

	void AudioMixerWindow::DuplicateSnapshot(const str& snapshot) {
		AudioMixer* mixer = Mixer();
		if (!mixer || !EditingValues()) {
			return;
		}
		const i32 copy = Ops::DuplicateSnapshot(*mixer, Ops::FindSnapshot(*mixer, snapshot));
		if (copy < 0) {
			return;
		}
		SetTargetOf(_guid, mixer->snapshots[static_cast<size_t>(copy)].id);
		Edited(false);
	}

	void AudioMixerWindow::RemoveSnapshot(const str& snapshot) {
		AudioMixer* mixer = Mixer();
		if (!mixer || !EditingValues()) {
			return;
		}
		if (mixer->snapshots.size() < 2) {
			SLT_LOG_INFO("Audio", "You must have at least 1 snapshot in an AudioMixer.");
			return;
		}
		if (Ops::RemoveSnapshot(*mixer, Ops::FindSnapshot(*mixer, snapshot))) {
			Edited(false);
		}
	}

	bool AudioMixerWindow::BeginSnapshotRename(const str& snapshot) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || !EditingValues() || Ops::FindSnapshot(*mixer, snapshot) >= mixer->snapshots.size()) {
			return false;
		}
		_renamingSnapshot = snapshot;
		_snapshotRenameNeedsFocus = true;
		return true;
	}

	u32 AudioMixerWindow::CurrentView() const {
		const AudioMixer* mixer = Mixer();
		return mixer ? ViewOf(_guid, *mixer) : 0u;
	}

	void AudioMixerWindow::Restyled() {
		AudioMixer* mixer = Mixer();
		if (!mixer) {
			return;
		}
		EditorAssetOps::MarkDirty(Handle<Asset>(mixer));
		_needsRebuild = true;
		_viewShape = 0;
		if (UI::Context* context = GetPanelContext()) {
			RefreshTitle(*context);
		}
	}

	u64 AudioMixerWindow::ViewShapeOf(const AudioMixer* mixer) const {
		u64 hash = StringHash("views").GetValue();
		auto mix = [&hash](u64 value) { hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2); };
		mix(StringHash(_guid.ToString()).GetValue());
		if (!mixer) {
			return hash;
		}
		for (u32 view = 0; view < Ops::CountViews(*mixer); ++view) {
			mix(StringHash(Ops::ViewName(*mixer, view)).GetValue());
		}
		mix(CurrentView());
		mix(static_cast<u64>(static_cast<i64>(_renamingView)));
		return hash;
	}

	void AudioMixerWindow::RebuildViews(UI::Context& context) {
		_viewRows.clear();
		_viewGaps.BeginBuild(context);
		const AudioMixer* mixer = Mixer();
		if (!mixer) {
			_views.SetRows(_viewRows);
			return;
		}
		const u32 count = Ops::CountViews(*mixer);
		auto gap = [this](u32 place) {
			EditorInsertStripDesc strip;
			strip.onDragOver = [this](const UI::DragPayload& payload) { return payload.type == VIEW_PAYLOAD && payload.data.TryGet<u32>() && Mixer(); };
			strip.onDrop = [this, place](const UI::DragPayload& payload, Vec2) {
				const u32* dragged = payload.type == VIEW_PAYLOAD ? payload.data.TryGet<u32>() : nullptr;
				AudioMixer* live = Mixer();
				if (!dragged || !live || !Ops::MoveView(*live, *dragged, place)) {
					return;
				}
				SetViewOf(_guid, place > *dragged ? place - 1 : place);
				Restyled();
			};
			_viewGaps.PendStrip(std::move(strip));
		};
		for (u32 view = 0; view < count; ++view) {
			gap(view);
			const str name = str(Ops::ViewName(*mixer, view));
			if (_renamingView == static_cast<i32>(view)) {
				_viewGaps.PendInFlow([this, view, name](UI::Context& c, UI::ElementHandle host) {
					UI::Style wrapStyle;
					wrapStyle.flexDirection = UI::FlexDirection::Row;
					wrapStyle.padding = {4.0f, 1.0f, 4.0f, 1.0f};
					UI::ElementHandle wrap = AddBox(c, host, wrapStyle);
					UI::Widgets::TextFieldDesc field;
					field.text = name;
					field.font = UISystem::GetDefaultFont();
					field.fontSizePx = EditorTheme::GetMetric(MetricToken::Font);
					field.onCommit = [this, view](std::string_view text) {
						if (AudioMixer* live = Mixer(); live && Ops::RenameView(*live, view, text)) {
							Restyled();
						}
						_renamingView = -1;
					};
					UI::ElementHandle input = UI::Widgets::TextField(c, wrap, field);
					if (_viewRenameNeedsFocus) {
						_viewRenameNeedsFocus = false;
						c.SetFocus(input);
					}
					auto chained = c.Events(input).onFocusLost;
					c.Events(input).onFocusLost = [this, chained]() {
						if (chained) {
							chained();
						}
						_renamingView = -1;
					};
				});
				continue;
			}
			ListRow row;
			row.id = ViewRow(view);
			row.label = name;
			row.onRightClick = [this, view](UI::Context& c) { OpenViewMenu(c, view); };
			row.onDragRow = [this, view, name](Vec2) {
				if (UISystem::IsDragPayloadActive()) {
					return;
				}
				UI::DragPayload payload;
				payload.type = VIEW_PAYLOAD;
				payload.data = Variant(view);
				payload.display = name;
				UISystem::BeginDragPayload(payload);
			};
			row.onBuildLeading = _viewGaps.Take();
			_viewRows.push_back(std::move(row));
		}
		gap(count);
		_views.SetRows(_viewRows);
		const u64 shown = ViewRow(CurrentView());
		_views.SetSelection(std::span<const u64>(&shown, 1));
	}

	void AudioMixerWindow::OpenViewMenu(UI::Context& context, u32 view) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || view >= Ops::CountViews(*mixer)) {
			return;
		}
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		UI::Widgets::MenuItem rename;
		rename.label = "Rename";
		rename.onSelect = [this, view]() { BeginViewRename(view); };
		items.push_back(std::move(rename));
		UI::Widgets::MenuItem duplicate;
		duplicate.label = "Duplicate";
		duplicate.onSelect = [this, view]() { DuplicateView(view, false); };
		items.push_back(std::move(duplicate));
		UI::Widgets::MenuItem remove;
		remove.label = "Delete";
		remove.onSelect = [this, view]() { RemoveView(view); };
		items.push_back(std::move(remove));
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	void AudioMixerWindow::DuplicateView(u32 view, bool rename) {
		AudioMixer* mixer = Mixer();
		const i32 copy = mixer ? Ops::DuplicateView(*mixer, view) : -1;
		if (copy < 0) {
			if (mixer && mixer->views.size() >= Ops::MAX_VIEWS) {
				SLT_LOG_INFO("Audio", "An Audio Mixer holds at most {} views", Ops::MAX_VIEWS);
			}
			return;
		}
		SetViewOf(_guid, static_cast<u32>(copy));
		Restyled();
		if (rename) {
			BeginViewRename(static_cast<u32>(copy));
		}
	}

	void AudioMixerWindow::RemoveView(u32 view) {
		AudioMixer* mixer = Mixer();
		if (!mixer) {
			return;
		}
		if (Ops::CountViews(*mixer) < 2) {
			SLT_LOG_INFO("Audio", "Deleting all views isn't allowed");
			return;
		}
		if (Ops::RemoveView(*mixer, view)) {
			Restyled();
		}
	}

	bool AudioMixerWindow::BeginViewRename(u32 view) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || view >= Ops::CountViews(*mixer)) {
			return false;
		}
		_renamingView = static_cast<i32>(view);
		_viewRenameNeedsFocus = true;
		return true;
	}

	Vector<str> AudioMixerWindow::Affected(const str& group) const {
		Vector<str> groups{GetMemoryResource(MemTag::Editor)};
		if (std::find(_selected.begin(), _selected.end(), group) != _selected.end()) {
			groups = _selected;
		} else {
			groups.push_back(group);
		}
		return groups;
	}

	void AudioMixerWindow::ToggleVisible(const str& group) {
		AudioMixer* mixer = Mixer();
		if (!mixer || AudioImpl::FindGroup(*mixer, group) < 0) {
			return;
		}
		const u32 view = CurrentView();
		if (Ops::SetVisible(*mixer, view, Affected(group), !Ops::IsVisible(*mixer, view, group))) {
			Restyled();
		}
	}

	void AudioMixerWindow::AddColorItems(Vector<UI::Widgets::MenuItem>& items, const str& group, bool separate) {
		const AudioMixer* mixer = Mixer();
		const Vector<str> groups = Affected(group);
		const i32 one = mixer && groups.size() == 1 ? AudioImpl::FindGroup(*mixer, groups.front()) : -1;
		for (u32 color = 0; color < std::size(COLOR_LABELS); ++color) {
			UI::Widgets::MenuItem item;
			item.label = str(COLOR_LABELS[color]);
			item.separatorBefore = separate && color == 0;
			item.checked = one >= 0 && static_cast<u32>(mixer->groups[static_cast<size_t>(one)].color) == color;
			item.onSelect = [this, groups, color]() {
				if (AudioMixer* live = Mixer(); live && Ops::SetColor(*live, groups, static_cast<AudioMixerGroupColor>(color))) {
					Restyled();
				}
			};
			items.push_back(std::move(item));
		}
	}

	void AudioMixerWindow::OpenEyeMenu(UI::Context& context, const str& group) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || AudioImpl::FindGroup(*mixer, group) < 0) {
			return;
		}
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		UI::Widgets::MenuItem toggle;
		toggle.label = Ops::IsVisible(*mixer, CurrentView(), group) ? "Hide group" : "Show Group";
		toggle.onSelect = [this, group]() { ToggleVisible(group); };
		items.push_back(std::move(toggle));
		AddColorItems(items, group, true);
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	void AudioMixerWindow::ScanMixers() {
		ForgetMixers();
		Vector<AssetBrowserOps::Entry> entries{GetMemoryResource(MemTag::Editor)};
		AssetBrowserOps::ScanTree(entries);
		for (const AssetBrowserOps::Entry& entry : entries) {
			if (entry.IsDirectory() || !entry.relPath.ends_with(".mixer")) {
				continue;
			}
			const Path path("project://" + entry.relPath);
			const Guid guid = AssetDatabase::LookupGuid(path);
			if (!guid.IsValid()) {
				continue;
			}
			Listed listed;
			listed.guid = guid;
			listed.relPath = entry.relPath;
			if (!AssetDatabase::FindLoadedByGuid(guid)) {
				AudioMixer* read = New<AudioMixer>(MemTag::Editor);
				if (read->Import(FileSystem::Resolve(path))) {
					listed.read = read;
				} else {
					Delete(read);
				}
			}
			_listed.push_back(std::move(listed));
		}
	}

	void AudioMixerWindow::ForgetMixers() {
		for (Listed& listed : _listed) {
			if (listed.read) {
				Delete(listed.read);
			}
		}
		_listed.clear();
	}

	void AudioMixerWindow::ListMixers(Vector<Ops::MixerListing>& out) const {
		out.clear();
		for (const Listed& listed : _listed) {
			Ops::MixerListing entry;
			entry.guid = listed.guid;
			const size_t slash = listed.relPath.find_last_of('/');
			const str file = listed.relPath.substr(slash == str::npos ? 0 : slash + 1);
			entry.name = file.substr(0, file.size() - std::string_view(".mixer").size());
			const Asset* loaded = AssetDatabase::FindLoadedByGuid(listed.guid);
			entry.data = loaded && loaded->GetAssetTypeId() == AudioMixer::GetReflectType()->name ? static_cast<const AudioMixer*>(loaded) : listed.read;
			out.push_back(std::move(entry));
		}
	}

	u64 AudioMixerWindow::MixerShapeOf() const {
		u64 hash = StringHash("mixers").GetValue();
		auto mix = [&hash](u64 value) { hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2); };
		Vector<Ops::MixerListing> listings{GetMemoryResource(MemTag::Editor)};
		ListMixers(listings);
		for (const Ops::MixerListing& listing : listings) {
			mix(StringHash(listing.guid.ToString()).GetValue());
			mix(StringHash(listing.name).GetValue());
			if (listing.data) {
				mix(StringHash(listing.data->outputMixer.ToString()).GetValue());
				mix(StringHash(listing.data->outputGroup).GetValue());
				for (const AudioMixer::Group& group : listing.data->groups) {
					mix(StringHash(group.name).GetValue());
				}
			}
		}
		mix(StringHash(_renamingMixer.ToString()).GetValue());
		mix(_creatingMixer ? 5u : 4u);
		for (const Guid& folded : _foldedMixers) {
			mix(StringHash(folded.ToString()).GetValue());
		}
		for (const Guid& selected : EditorSelection::GetSelectedAssets()) {
			mix(StringHash(selected.ToString()).GetValue());
		}
		return hash;
	}

	void AudioMixerWindow::RebuildMixers(UI::Context& context) {
		_mixerRows.clear();
		_mixerGaps.BeginBuild(context);
		Vector<Ops::MixerListing> listings{GetMemoryResource(MemTag::Editor)};
		ListMixers(listings);
		Vector<Ops::MixerRow> arranged{GetMemoryResource(MemTag::Editor)};
		Ops::ArrangeMixers(listings, arranged);
		auto nameField = [this](std::string_view name, u32 depth, auto commit, auto cancel) {
			_mixerGaps.PendInFlow([this, name = str(name), depth, commit, cancel](UI::Context& c, UI::ElementHandle host) {
				UI::Style wrapStyle;
				wrapStyle.flexDirection = UI::FlexDirection::Row;
				wrapStyle.padding = {4.0f + static_cast<f32>(depth) * 14.0f, 1.0f, 4.0f, 1.0f};
				UI::ElementHandle wrap = AddBox(c, host, wrapStyle);
				UI::Widgets::TextFieldDesc field;
				field.text = name;
				field.font = UISystem::GetDefaultFont();
				field.fontSizePx = EditorTheme::GetMetric(MetricToken::Font);
				field.onCommit = commit;
				UI::ElementHandle input = UI::Widgets::TextField(c, wrap, field);
				if (_mixerNameNeedsFocus) {
					_mixerNameNeedsFocus = false;
					c.SetFocus(input);
				}
				auto chained = c.Events(input).onFocusLost;
				c.Events(input).onFocusLost = [chained, cancel]() {
					if (chained) {
						chained();
					}
					cancel();
				};
			});
		};
		u32 hiddenBelow = ~0u;
		for (const Ops::MixerRow& entry : arranged) {
			if (entry.depth > hiddenBelow) {
				continue;
			}
			hiddenBelow = ~0u;
			const Ops::MixerListing& listing = listings[entry.index];
			const Guid guid = listing.guid;
			const bool folded = std::find(_foldedMixers.begin(), _foldedMixers.end(), guid) != _foldedMixers.end();
			if (folded) {
				hiddenBelow = entry.depth;
			}
			if (_renamingMixer == guid) {
				nameField(
					listing.name, entry.depth, [this, guid](std::string_view text) { RenameMixer(guid, text); },
					[this]() {
						_renamingMixer = {};
						_mixerShape = 0;
					});
				continue;
			}
			ListRow row;
			row.id = MixerRowId(guid);
			row.depth = entry.depth;
			row.hasChildren = entry.hasChildren;
			row.expanded = !folded;
			row.label = listing.name;
			const bool hasChildren = entry.hasChildren;
			const bool expanded = !folded;
			const str info = Ops::DescribeOutput(listings, listing);
			row.onDecorate = [hasChildren, expanded, info](UI::Context& c, const UI::Widgets::TreeRowParts& parts) {
				EditorIcons::SetRowGutter(c, parts, hasChildren, expanded, EditorTheme::GetColor(ColorToken::TextDim), EditorIcons::Icon{}, Color());
				UI::ElementHandle where = AddText(c, parts.row, info, ColorToken::TextDim);
				UI::Style whereStyle = c.GetStyle(where);
				whereStyle.margin = {8.0f, 0.0f, 0.0f, 0.0f};
				whereStyle.flexShrink = 0.0f;
				c.SetStyle(where, whereStyle);
			};
			row.onToggleExpand = [this, guid](bool open) {
				std::erase(_foldedMixers, guid);
				if (!open) {
					_foldedMixers.push_back(guid);
				}
			};
			row.onRightClick = [this, guid](UI::Context& c) { OpenMixerMenu(c, guid); };
			row.onDragRow = [this, guid, name = listing.name](Vec2) {
				if (UISystem::IsDragPayloadActive() || IsPlaying()) {
					return;
				}
				Vector<Guid> moving = SelectedMixers();
				if (std::find(moving.begin(), moving.end(), guid) == moving.end()) {
					moving.clear();
					moving.push_back(guid);
				}
				UI::DragPayload payload;
				payload.type = MIXER_PAYLOAD;
				payload.data = Variant(JoinMixers(moving));
				payload.display = moving.size() > 1 ? std::format("{} AudioMixers", moving.size()) : name;
				UISystem::BeginDragPayload(payload);
			};
			row.onDragOver = [this, guid](const UI::DragPayload& payload) {
				const Vector<Guid> dragged = SplitMixers(payload);
				if (dragged.empty() || IsPlaying()) {
					return false;
				}
				Vector<Ops::MixerListing> now{GetMemoryResource(MemTag::Editor)};
				ListMixers(now);
				return std::all_of(dragged.begin(), dragged.end(), [&](const Guid& moved) { return Ops::CanRouteInto(now, moved, guid); });
			};
			row.onDrop = [this, guid](const UI::DragPayload& payload, Vec2) {
				if (UI::Context* c = GetPanelContext()) {
					OpenRouteMenu(*c, SplitMixers(payload), guid);
				}
			};
			row.onBuildLeading = _mixerGaps.Take();
			_mixerRows.push_back(std::move(row));
		}
		if (_creatingMixer) {
			str suggested = "New Audio Mixer";
			if (IAssetImporter* importer = AssetImporterRegistry::Find(AudioMixer::GetReflectType()->name)) {
				suggested = str(importer->GetCreateDefaultFileName());
				suggested = suggested.substr(0, suggested.find_last_of('.'));
			}
			nameField(
				suggested, 0, [this](std::string_view text) { CreateMixer(text); },
				[this]() {
					_creatingMixer = false;
					_mixerShape = 0;
				});
		} else if (arranged.empty()) {
			_mixerGaps.PendInFlow([](UI::Context& c, UI::ElementHandle host) {
				UI::ElementHandle none = AddText(c, host, "No mixers found", ColorToken::TextDim);
				UI::Style noneStyle = c.GetStyle(none);
				noneStyle.margin = {6.0f, 3.0f, 0.0f, 0.0f};
				c.SetStyle(none, noneStyle);
			});
		}
		_mixers.SetRows(_mixerRows);
		Vector<u64> selected{GetMemoryResource(MemTag::Editor)};
		for (const Guid& asset : EditorSelection::GetSelectedAssets()) {
			for (const Listed& listed : _listed) {
				if (listed.guid == asset) {
					selected.push_back(MixerRowId(asset));
				}
			}
		}
		if (selected.empty() && _guid.IsValid()) {
			selected.push_back(MixerRowId(_guid));
		}
		_mixers.SetSelection(selected);
	}

	Vector<Guid> AudioMixerWindow::SelectedMixers() const {
		Vector<Guid> out{GetMemoryResource(MemTag::Editor)};
		for (const u64 row : _mixers.GetSelected()) {
			for (const Listed& listed : _listed) {
				if (MixerRowId(listed.guid) == row) {
					out.push_back(listed.guid);
				}
			}
		}
		return out;
	}

	void AudioMixerWindow::OpenMixerMenu(UI::Context& context, const Guid& mixer) {
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		UI::Widgets::MenuItem rename;
		rename.label = "Rename";
		rename.onSelect = [this, mixer]() {
			_renamingMixer = mixer;
			_mixerNameNeedsFocus = true;
			_mixerShape = 0;
		};
		items.push_back(std::move(rename));
		UI::Widgets::MenuItem remove;
		remove.label = "Delete AudioMixer";
		remove.separatorBefore = true;
		remove.onSelect = [this, mixer]() { DeleteMixers(std::span<const Guid>(&mixer, 1)); };
		items.push_back(std::move(remove));
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	void AudioMixerWindow::OpenRouteMenu(UI::Context& context, const Vector<Guid>& dragged, const Guid& onto) {
		Vector<Ops::MixerListing> listings{GetMemoryResource(MemTag::Editor)};
		ListMixers(listings);
		const auto target = std::find_if(listings.begin(), listings.end(), [&](const Ops::MixerListing& listing) { return listing.guid == onto; });
		if (IsPlaying() || dragged.empty() || target == listings.end() || !target->data ||
			!std::all_of(dragged.begin(), dragged.end(), [&](const Guid& moved) { return Ops::CanRouteInto(listings, moved, onto); })) {
			return;
		}
		str current;
		if (dragged.size() == 1) {
			for (const Ops::MixerListing& listing : listings) {
				if (listing.guid == dragged.front() && listing.data && listing.data->outputMixer == onto) {
					current = listing.data->outputGroup;
				}
			}
		}
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		UI::Widgets::MenuItem title;
		title.label = "Select Output Audio Mixer Group";
		title.enabled = false;
		items.push_back(std::move(title));
		UI::Widgets::MenuItem none;
		none.label = "None";
		none.separatorBefore = true;
		none.onSelect = [this, dragged]() { Route(dragged, Guid{}, {}); };
		items.push_back(std::move(none));
		Vector<Ops::GroupEntry> entries{GetMemoryResource(MemTag::Editor)};
		Ops::Flatten(*target->data, entries);
		for (const Ops::GroupEntry& entry : entries) {
			const AudioMixer::Group& group = target->data->groups[entry.index];
			UI::Widgets::MenuItem item;
			item.label = group.name;
			item.checked = group.id == current;
			item.onSelect = [this, dragged, onto, id = group.id]() { Route(dragged, onto, id); };
			items.push_back(std::move(item));
		}
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	void AudioMixerWindow::Route(std::span<const Guid> dragged, const Guid& onto, std::string_view group) {
		if (IsPlaying() || dragged.empty()) {
			return;
		}
		for (const Guid& guid : dragged) {
			Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
			if (!loaded) {
				continue;
			}
			if (loaded->GetAssetTypeId() == AudioMixer::GetReflectType()->name) {
				auto* mixer = static_cast<AudioMixer*>(loaded);
				if (mixer->outputMixer != onto || mixer->outputGroup != group) {
					mixer->outputMixer = onto;
					mixer->outputGroup = str(group);
					mixer->ApplyChanges();
					EditorAssetOps::MarkDirty(Handle<Asset>(mixer));
				}
			}
			AssetDatabase::UnloadAsset(loaded);
		}
		EditorSelection::SelectAssets(dragged);
		_mixerShape = 0;
		if (UI::Context* context = GetPanelContext()) {
			RefreshTitle(*context);
		}
	}

	void AudioMixerWindow::CreateMixer(std::string_view name) {
		_creatingMixer = false;
		_mixerShape = 0;
		if (name.empty()) {
			return;
		}
		Path folder = AssetBrowserOps::GetDefaultCreateFolder();
		Guid output{};
		str group;
		if (const AudioMixer* shown = Mixer()) {
			const str path = AssetDatabase::LookupPath(_guid).ToString();
			if (const size_t slash = path.find_last_of('/'); slash != str::npos) {
				folder = Path(path.substr(0, slash));
			}
			output = shown->outputMixer;
			group = shown->outputGroup;
		}
		const Guid made = AssetBrowserOps::CreateAsset(folder, AudioMixer::GetReflectType()->name, str(name) + ".mixer");
		if (!made.IsValid()) {
			return;
		}
		if (output.IsValid()) {
			if (Asset* loaded = AssetDatabase::LoadAssetByGuid(made)) {
				if (loaded->GetAssetTypeId() == AudioMixer::GetReflectType()->name) {
					auto* mixer = static_cast<AudioMixer*>(loaded);
					mixer->outputMixer = output;
					mixer->outputGroup = group;
					mixer->ApplyChanges();
					EditorAssetOps::SaveAsset(Handle<Asset>(mixer));
				}
				AssetDatabase::UnloadAsset(loaded);
			}
		}
		EditorSelection::SelectAsset(made);
		_mixersNeedScan = true;
	}

	void AudioMixerWindow::RenameMixer(const Guid& mixer, std::string_view name) {
		_renamingMixer = {};
		_mixerShape = 0;
		const Path path = AssetDatabase::LookupPath(mixer);
		const str file = str(name) + ".mixer";
		if (name.empty() || path.ToString().empty() || path.GetFilename() == file) {
			return;
		}
		if (AssetBrowserOps::RenameEntry(path, file)) {
			_mixersNeedScan = true;
			if (UI::Context* context = GetPanelContext()) {
				RefreshTitle(*context);
			}
		}
	}

	void AudioMixerWindow::DeleteMixers(std::span<const Guid> doomed) {
		UI::Context* context = GetPanelContext();
		Vector<str> paths{GetMemoryResource(MemTag::Editor)};
		for (const Guid& guid : doomed) {
			if (str path = AssetDatabase::LookupPath(guid).ToString(); !path.empty()) {
				paths.push_back(std::move(path));
			}
		}
		if (!context || paths.empty()) {
			return;
		}
		const str prompt = paths.size() > 1 ? std::format("Delete {} mixers?", paths.size()) : "Delete '" + Path(paths.front()).GetFilename() + "'?";
		CloseConfirm();
		_confirm = EditorDialogs::OpenConfirm(*context, prompt, "Delete", [this, paths]() {
			for (const str& path : paths) {
				AssetBrowserOps::DeleteEntry(Path(path));
			}
			_confirm = {};
			_mixersNeedScan = true;
		});
	}

	void AudioMixerWindow::DuplicateMixers(std::span<const Guid> sources) {
		Vector<Guid> copies{GetMemoryResource(MemTag::Editor)};
		for (const Guid& guid : sources) {
			const Path path = AssetDatabase::LookupPath(guid);
			if (path.ToString().empty()) {
				continue;
			}
			if (const str made = AssetBrowserOps::DuplicateEntry(path); !made.empty()) {
				if (const Guid copy = AssetDatabase::LookupGuid(Path("project://" + made)); copy.IsValid()) {
					copies.push_back(copy);
				}
			}
		}
		if (!copies.empty()) {
			EditorSelection::SelectAssets(copies);
			_mixersNeedScan = true;
		}
	}

	void AudioMixerWindow::CloseConfirm() {
		UI::Context* context = GetPanelContext();
		if (context && context->IsAlive(_confirm) && context->IsPopupOpen(_confirm)) {
			context->ClosePopup(_confirm);
		}
		_confirm = {};
	}

	void AudioMixerWindow::OpenStripMenu(UI::Context& context, const str& group) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || AudioImpl::FindGroup(*mixer, group) < 0) {
			return;
		}
		const Vector<str> groups = Affected(group);
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		UI::Widgets::MenuItem top;
		top.label = "Add effect at top";
		top.children = AddEffectItems(groups, 0);
		items.push_back(std::move(top));
		UI::Widgets::MenuItem bottom;
		bottom.label = "Add effect at bottom";
		bottom.children = AddEffectItems(groups, ~0u);
		items.push_back(std::move(bottom));
		AddColorItems(items, group, true);
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	str AudioMixerWindow::GroupOfRow(u64 row) const {
		if (const AudioMixer* mixer = Mixer()) {
			for (const AudioMixer::Group& group : mixer->groups) {
				if (RowId(group.id) == row) {
					return group.id;
				}
			}
		}
		return {};
	}

	void AudioMixerWindow::RebuildRows(UI::Context& context) {
		_rows.clear();
		_gaps.BeginBuild(context);
		const AudioMixer* mixer = Mixer();
		if (!mixer) {
			_groups.SetRows(_rows);
			return;
		}
		Vector<Ops::GroupEntry> entries{GetMemoryResource(MemTag::Editor)};
		Ops::Flatten(*mixer, entries);
		const u32 view = CurrentView();
		u32 hiddenBelow = ~0u;
		for (const Ops::GroupEntry& entry : entries) {
			if (entry.depth > hiddenBelow) {
				continue;
			}
			hiddenBelow = ~0u;
			const AudioMixer::Group& group = mixer->groups[entry.index];
			const str id = group.id;
			const bool collapsed = std::find(_collapsed.begin(), _collapsed.end(), id) != _collapsed.end();
			if (collapsed) {
				hiddenBelow = entry.depth;
			}

			if (!group.parent.empty()) {
				u32 slot = 0;
				for (u32 other = 0; other < entry.index; ++other) {
					slot += mixer->groups[other].parent == group.parent ? 1u : 0u;
				}
				const str parent = group.parent;
				EditorInsertStripDesc strip;
				strip.onDragOver = [this, parent](const UI::DragPayload& payload) {
					const str* dragged = payload.type == GROUP_PAYLOAD ? payload.data.TryGet<str>() : nullptr;
					const AudioMixer* live = Mixer();
					return dragged && live && !IsPlaying() && Ops::CanMoveGroup(*live, *dragged, parent);
				};
				strip.onDrop = [this, parent, slot](const UI::DragPayload& payload, Vec2) {
					const str* dragged = payload.type == GROUP_PAYLOAD ? payload.data.TryGet<str>() : nullptr;
					AudioMixer* live = Mixer();
					if (!dragged || !live || IsPlaying()) {
						return;
					}
					u32 target = slot;
					const i32 at = AudioImpl::FindGroup(*live, *dragged);
					if (at >= 0 && live->groups[static_cast<size_t>(at)].parent == parent) {
						u32 before = 0;
						for (i32 other = 0; other < at; ++other) {
							before += live->groups[static_cast<size_t>(other)].parent == parent ? 1u : 0u;
						}
						target -= before < slot ? 1u : 0u;
					}
					if (Ops::MoveGroup(*live, *dragged, parent, target)) {
						Edited(true);
					}
				};
				_gaps.PendStrip(std::move(strip));
			}

			if (_renaming == id) {
				const u32 depth = entry.depth;
				const str name = group.name;
				_gaps.PendInFlow([this, id, depth, name](UI::Context& c, UI::ElementHandle host) {
					UI::Style wrapStyle;
					wrapStyle.flexDirection = UI::FlexDirection::Row;
					wrapStyle.padding = {4.0f + static_cast<f32>(depth) * 14.0f, 1.0f, 4.0f, 1.0f};
					UI::ElementHandle wrap = AddBox(c, host, wrapStyle);
					UI::Widgets::TextFieldDesc field;
					field.text = name;
					field.font = UISystem::GetDefaultFont();
					field.fontSizePx = EditorTheme::GetMetric(MetricToken::Font);
					field.onCommit = [this, id](std::string_view text) {
						AudioMixer* live = Mixer();
						const i32 at = live ? AudioImpl::FindGroup(*live, id) : -1;
						if (at >= 0 && !text.empty() && live->groups[static_cast<size_t>(at)].name != text && Ops::RenameGroup(*live, id, text)) {
							Edited(true);
						}
						_renaming.clear();
						_needsRebuild = true;
					};
					UI::ElementHandle input = UI::Widgets::TextField(c, wrap, field);
					if (_renameNeedsFocus) {
						_renameNeedsFocus = false;
						c.SetFocus(input);
					}
					auto chained = c.Events(input).onFocusLost;
					c.Events(input).onFocusLost = [this, chained]() {
						if (chained) {
							chained();
						}
						_renaming.clear();
						_needsRebuild = true;
					};
				});
				continue;
			}

			ListRow row;
			row.id = RowId(id);
			row.depth = entry.depth;
			row.hasChildren = entry.hasChildren;
			row.expanded = !collapsed;
			row.label = group.name;
			const bool hasChildren = entry.hasChildren;
			const bool expanded = !collapsed;
			const bool visible = Ops::IsVisible(*mixer, view, id);
			const u32 color = static_cast<u32>(group.color);
			row.onDecorate = [this, id, hasChildren, expanded, visible, color](UI::Context& c, const UI::Widgets::TreeRowParts& parts) {
				EditorIcons::SetRowGutter(c, parts, hasChildren, expanded, EditorTheme::GetColor(ColorToken::TextDim), EditorIcons::Icon{}, Color());
				Vector<UI::ElementHandle> held{GetMemoryResource(MemTag::Editor)};
				const UI::Element* data = c.GetElement(parts.row);
				for (u32 child = data ? data->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
					const UI::ElementHandle handle = c.MakeHandle(child);
					held.push_back(handle);
					const UI::Element* next = c.GetElement(handle);
					child = next ? next->nextSibling : UI::INVALID_UI_INDEX;
				}
				UI::Style tagStyle;
				tagStyle.width = UI::Length::Pixel(2.0f);
				tagStyle.height = UI::Length::Pixel(EditorIcons::GutterSizePx());
				tagStyle.flexShrink = 0.0f;
				tagStyle.margin = {0.0f, 0.0f, 2.0f, 0.0f};
				if (color > 0 && color <= std::size(COLOR_TOKENS)) {
					tagStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(COLOR_TOKENS[color - 1]));
				}
				UI::ElementHandle tag = AddBox(c, parts.row, tagStyle);
				c.AddClass(tag, COLOR_TAG_CLASS);
				c.SetHoverable(tag, false);
				UI::ElementHandle eye = c.CreateElement();
				c.AppendChild(parts.row, eye);
				UI::Style eyeStyle;
				eyeStyle.flexShrink = 0.0f;
				eyeStyle.margin = {0.0f, 0.0f, 4.0f, 0.0f};
				c.SetStyle(eye, eyeStyle);
				EditorIcons::SetGlyphOrText(c, eye, visible ? EditorIcons::Glyph::Eye : EditorIcons::Glyph::EyeSlash, visible ? "*" : "-",
											EditorTheme::GetColor(ColorToken::TextDim), EditorIcons::GutterSizePx(), EditorTheme::GetMetric(MetricToken::FontRow));
				c.AddClass(eye, EYE_CLASS);
				c.AddClass(eye, EditorTheme::Class::Clickable);
				c.SetTooltip(eye, visible ? "Hide this group in the view. Right-click for its color" : "Show this group in the view. Right-click for its color");
				c.Events(eye).onClick = [this, id]() { ToggleVisible(id); };
				c.Events(eye).onRightClick = [this, id]() {
					if (UI::Context* context = GetPanelContext()) {
						OpenEyeMenu(*context, id);
					}
				};
				for (const UI::ElementHandle handle : held) {
					c.AppendChild(parts.row, handle);
				}
			};
			row.onToggleExpand = [this, id](bool expanded) {
				std::erase(_collapsed, id);
				if (!expanded) {
					_collapsed.push_back(id);
				}
				_needsRebuild = true;
			};
			row.onRightClick = [this, id](UI::Context& c) { OpenGroupMenu(c, id); };
			row.onDragRow = [this, id](Vec2) {
				if (UISystem::IsDragPayloadActive() || IsPlaying()) {
					return;
				}
				UI::DragPayload payload;
				payload.type = GROUP_PAYLOAD;
				payload.data = Variant(id);
				const AudioMixer* live = Mixer();
				const i32 at = live ? AudioImpl::FindGroup(*live, id) : -1;
				payload.display = at >= 0 ? live->groups[static_cast<size_t>(at)].name : id;
				UISystem::BeginDragPayload(payload);
			};
			row.onDragOver = [this, id](const UI::DragPayload& payload) {
				const str* dragged = payload.type == GROUP_PAYLOAD ? payload.data.TryGet<str>() : nullptr;
				const AudioMixer* live = Mixer();
				return dragged && live && !IsPlaying() && Ops::CanMoveGroup(*live, *dragged, id);
			};
			row.onDrop = [this, id](const UI::DragPayload& payload, Vec2) {
				const str* dragged = payload.type == GROUP_PAYLOAD ? payload.data.TryGet<str>() : nullptr;
				AudioMixer* live = Mixer();
				if (dragged && live && !IsPlaying() && Ops::MoveGroup(*live, *dragged, id, ~0u)) {
					Edited(true);
				}
			};
			row.onBuildLeading = _gaps.Take();
			_rows.push_back(std::move(row));
		}
		_groups.SetRows(_rows);
		PushSelectionToList();
	}

	void AudioMixerWindow::PushSelectionToList() {
		Vector<u64> ids{GetMemoryResource(MemTag::Editor)};
		for (const str& group : _selected) {
			ids.push_back(RowId(group));
		}
		_groups.SetSelection(ids);
	}

	void AudioMixerWindow::TakeSelectionFromList() {
		_selected.clear();
		for (const u64 row : _groups.GetSelected()) {
			str group = GroupOfRow(row);
			if (!group.empty()) {
				_selected.push_back(std::move(group));
			}
		}
	}

	void AudioMixerWindow::OpenGroupMenu(UI::Context& context, const str& group) {
		const AudioMixer* mixer = Mixer();
		if (!mixer) {
			return;
		}
		if (std::find(_selected.begin(), _selected.end(), group) == _selected.end()) {
			_selected.clear();
			_selected.push_back(group);
			PushSelectionToList();
		}
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		if (IsPlaying()) {
			UI::Widgets::MenuItem held;
			held.label = "Modifying group topology in play mode is not allowed";
			held.enabled = false;
			items.push_back(std::move(held));
		} else {
			const i32 at = AudioImpl::FindGroup(*mixer, group);
			const bool top = at >= 0 && at == Ops::FindTop(*mixer);
			UI::Widgets::MenuItem child;
			child.label = "Add child group";
			child.onSelect = [this, group]() { AddChild(group); };
			items.push_back(std::move(child));
			if (!top && at >= 0) {
				const str parent = mixer->groups[static_cast<size_t>(at)].parent;
				UI::Widgets::MenuItem sibling;
				sibling.label = "Add sibling group";
				sibling.onSelect = [this, parent]() { AddChild(parent); };
				items.push_back(std::move(sibling));
				UI::Widgets::MenuItem rename;
				rename.label = "Rename";
				rename.separatorBefore = true;
				rename.onSelect = [this, group]() { BeginRename(group); };
				items.push_back(std::move(rename));
				const bool several = _selected.size() > 1;
				UI::Widgets::MenuItem duplicate;
				duplicate.label = several ? "Duplicate groups (and children)" : "Duplicate group (and children)";
				duplicate.onSelect = [this]() { DuplicateSelected(); };
				items.push_back(std::move(duplicate));
				UI::Widgets::MenuItem remove;
				remove.label = several ? "Remove groups (and children)" : "Remove group (and children)";
				remove.onSelect = [this]() { RemoveSelected(); };
				items.push_back(std::move(remove));
			}
		}
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	void AudioMixerWindow::AddChild(const str& parent) {
		AudioMixer* mixer = Mixer();
		if (!mixer || IsPlaying()) {
			return;
		}
		const str added = Ops::AddGroup(*mixer, parent);
		if (added.empty()) {
			return;
		}
		Ops::SetVisible(*mixer, CurrentView(), std::span<const str>(&added, 1), true);
		std::erase(_collapsed, parent);
		_selected.clear();
		_selected.push_back(added);
		Edited(true);
		BeginRename(added);
	}

	void AudioMixerWindow::RemoveSelected() {
		AudioMixer* mixer = Mixer();
		if (!mixer || IsPlaying() || _selected.empty()) {
			return;
		}
		if (Ops::RemoveGroups(*mixer, _selected) > 0) {
			_selected.clear();
			Edited(true);
		}
	}

	void AudioMixerWindow::DuplicateSelected() {
		AudioMixer* mixer = Mixer();
		if (!mixer || IsPlaying() || _selected.empty()) {
			return;
		}
		Vector<str> copies{GetMemoryResource(MemTag::Editor)};
		Ops::DuplicateGroups(*mixer, _selected, CurrentView(), copies);
		if (!copies.empty()) {
			_selected = std::move(copies);
			Edited(true);
		}
	}

	bool AudioMixerWindow::BeginRename(const str& group) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || IsPlaying() || AudioImpl::FindGroup(*mixer, group) < 0) {
			return false;
		}
		_renaming = group;
		_renameNeedsFocus = true;
		_needsRebuild = true;
		return true;
	}

	void AudioMixerWindow::RebuildStrips(UI::Context& context) {
		_parts.clear();
		if (!context.IsAlive(_strips)) {
			return;
		}
		context.ClearChildren(_strips);
		const AudioMixer* mixer = Mixer();
		if (!mixer) {
			UI::ElementHandle hint = AddText(context, _strips, "Select an Audio Mixer in the Asset Browser to edit it here", ColorToken::TextDim, MetricToken::FontBody);
			UI::Style hintStyle = context.GetStyle(hint);
			hintStyle.flexShrink = 0.0f;
			context.SetStyle(hint, hintStyle);
			return;
		}
		Vector<Ops::GroupEntry> entries{GetMemoryResource(MemTag::Editor)};
		Ops::Flatten(*mixer, entries);
		const u32 view = CurrentView();
		for (const Ops::GroupEntry& entry : entries) {
			if (Ops::IsVisible(*mixer, view, mixer->groups[entry.index].id)) {
				BuildStrip(context, *mixer, entry.index);
			}
		}
	}

	void AudioMixerWindow::BuildStrip(UI::Context& context, const AudioMixer& mixer, u32 index) {
		const AudioMixer::Group& group = mixer.groups[index];
		Strip strip;
		strip.id = group.id;
		const str id = group.id;

		UI::Style stripStyle;
		stripStyle.flexDirection = UI::FlexDirection::Column;
		stripStyle.width = UI::Length::Pixel(EditorTheme::GetMetric(MetricToken::AudioStripWidth));
		stripStyle.flexShrink = 0.0f;
		stripStyle.margin = {0.0f, 0.0f, 4.0f, 0.0f};
		stripStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
		UI::ElementHandle root = AddBox(context, _strips, stripStyle);
		context.AddClass(root, STRIP_CLASS);
		auto selects = [this, id, &context](UI::ElementHandle element) {
			context.Events(element).onClick = [this, id]() {
				UI::Context* c = GetPanelContext();
				Select(id, c && (c->GetPressMods() & KeyMod::Ctrl) != KeyMod::None);
			};
			context.Events(element).onRightClick = [this, id]() {
				if (UI::Context* c = GetPanelContext()) {
					OpenStripMenu(*c, id);
				}
			};
		};
		selects(root);

		UI::Style headerStyle;
		headerStyle.flexDirection = UI::FlexDirection::Row;
		headerStyle.justifyContent = UI::Align::Center;
		headerStyle.padding = {4.0f, 3.0f, 4.0f, 3.0f};
		strip.header = AddBox(context, root, headerStyle);
		selects(strip.header);
		UI::ElementHandle title = AddText(context, strip.header, group.name, ColorToken::Text);
		UI::Style titleStyle = context.GetStyle(title);
		titleStyle.textOverflow = UI::TextOverflow::Ellipsis;
		context.SetStyle(title, titleStyle);
		if (group.color != AudioMixerGroupColor::None && static_cast<u32>(group.color) <= std::size(COLOR_TOKENS)) {
			UI::Style tagStyle;
			tagStyle.position = UI::Position::Absolute;
			tagStyle.left = UI::Length::Pixel(0.0f);
			tagStyle.right = UI::Length::Pixel(0.0f);
			tagStyle.bottom = UI::Length::Pixel(0.0f);
			tagStyle.height = UI::Length::Pixel(2.0f);
			tagStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(COLOR_TOKENS[static_cast<u32>(group.color) - 1]));
			UI::ElementHandle tag = AddBox(context, strip.header, tagStyle);
			context.AddClass(tag, COLOR_TAG_CLASS);
			context.SetHoverable(tag, false);
		}

		UI::Style bandStyle;
		bandStyle.flexDirection = UI::FlexDirection::Row;
		bandStyle.height = UI::Length::Pixel(EditorTheme::GetMetric(MetricToken::AudioMeterHeight));
		bandStyle.padding = {2.0f, 2.0f, 2.0f, 2.0f};
		UI::ElementHandle band = AddBox(context, root, bandStyle);
		selects(band);

		UI::Style scaleStyle;
		scaleStyle.width = UI::Length::Pixel(SCALE_WIDTH);
		scaleStyle.flexShrink = 0.0f;
		UI::ElementHandle scale = AddBox(context, band, scaleStyle);
		selects(scale);
		const f32 travel = EditorTheme::GetMetric(MetricToken::AudioMeterHeight) - 4.0f;
		for (i32 level = 20; level >= -80; level -= 20) {
			UI::ElementHandle number = AddText(context, scale, std::format("{}", level), ColorToken::TextFaint);
			UI::Style numberStyle = context.GetStyle(number);
			numberStyle.position = UI::Position::Absolute;
			numberStyle.right = UI::Length::Pixel(6.0f);
			numberStyle.top = UI::Length::Pixel(HANDLE_HEIGHT * 0.5f + Ops::DecibelsToFader(static_cast<f32>(level)) * (travel - HANDLE_HEIGHT) - 6.0f);
			context.SetStyle(number, numberStyle);
		}
		context.Events(scale).customDraw = [this](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) { PaintScale(c, element, paint); };

		UI::Style meterStyle;
		meterStyle.flexGrow = 1.0f;
		meterStyle.margin = {2.0f, 0.0f, 2.0f, 0.0f};
		UI::ElementHandle meters = AddBox(context, band, meterStyle);
		selects(meters);
		context.SetTooltip(meters, "Lvl leaving this group");
		context.Events(meters).customDraw = [this, id](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
			PaintMeters(c, element, paint, id);
		};

		UI::Style faderStyle;
		faderStyle.width = UI::Length::Pixel(FADER_WIDTH);
		faderStyle.flexShrink = 0.0f;
		strip.fader = AddBox(context, band, faderStyle);
		context.AddClass(strip.fader, FADER_CLASS);
		context.SetTooltip(strip.fader, "Attenuation of this group, in decibels");
		context.Events(strip.fader).customDraw = [this, id](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
			PaintFader(c, element, paint, id);
		};
		context.Events(strip.fader).onMouseDown = [this, id]() {
			UI::Context* c = GetPanelContext();
			const AudioMixer* mixer = Mixer();
			if (!c || !mixer || !EditingValues()) {
				return;
			}
			_dragGroup = id;
			_dragStartY = c->GetPointerPosition().y;
			_dragStartShare = Ops::DecibelsToFader(FaderValue(*mixer, id));
		};
		context.Events(strip.fader).onPressDrag = [this](Vec2 point) {
			if (UI::Context* c = GetPanelContext()) {
				DragFader(*c, point);
			}
		};
		context.Events(strip.fader).onPressEnd = [this]() { _dragGroup.clear(); };

		strip.value = AddText(context, strip.fader, "", ColorToken::TextStrong);
		UI::Style valueStyle = context.GetStyle(strip.value);
		valueStyle.position = UI::Position::Absolute;
		valueStyle.left = UI::Length::Pixel(0.0f);
		valueStyle.width = UI::Length::Percent(1.0f);
		valueStyle.height = UI::Length::Pixel(HANDLE_HEIGHT);
		valueStyle.textAlign = UI::Align::Center;
		valueStyle.textAlignVertical = UI::Align::Center;
		context.SetStyle(strip.value, valueStyle);

		UI::Style readoutStyle;
		readoutStyle.flexDirection = UI::FlexDirection::Row;
		readoutStyle.justifyContent = UI::Align::Center;
		readoutStyle.padding = {0.0f, 2.0f, 0.0f, 2.0f};
		UI::ElementHandle readoutRow = AddBox(context, root, readoutStyle);
		selects(readoutRow);
		strip.readout = AddText(context, readoutRow, "", ColorToken::TextDim);

		UI::Style buttonsStyle;
		buttonsStyle.flexDirection = UI::FlexDirection::Row;
		buttonsStyle.padding = {3.0f, 2.0f, 3.0f, 4.0f};
		UI::ElementHandle buttons = AddBox(context, root, buttonsStyle);
		selects(buttons);
		auto toggle = [&](std::string_view letter, std::string_view tooltip) {
			UI::Style style;
			style.flexGrow = 1.0f;
			style.justifyContent = UI::Align::Center;
			style.padding = {0.0f, 1.0f, 0.0f, 1.0f};
			style.margin = {1.0f, 0.0f, 1.0f, 0.0f};
			style.borderRadius = UI::Radii::All(2.0f);
			style.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::HeaderFill));
			UI::ElementHandle element = AddBox(context, buttons, style);
			context.AddClass(element, EditorTheme::Class::Clickable);
			AddText(context, element, letter, ColorToken::Text);
			context.SetTooltip(element, tooltip);
			return element;
		};
		strip.solo = toggle("S", "Adds this group to set of soloed groups");
		strip.mute = toggle("M", "Mutes this group");
		strip.bypass = toggle("B", "Bypasses the effects on this group");
		context.AddClass(strip.solo, SOLO_CLASS);
		context.AddClass(strip.mute, MUTE_CLASS);
		context.AddClass(strip.bypass, BYPASS_CLASS);
		auto flip = [this, id](bool AudioMixer::Group::* field) {
			AudioMixer* mixer = Mixer();
			const i32 at = mixer ? AudioImpl::FindGroup(*mixer, id) : -1;
			if (at < 0) {
				return;
			}
			bool& value = mixer->groups[static_cast<size_t>(at)].*field;
			value = !value;
			Edited(false);
		};
		context.Events(strip.solo).onClick = [flip]() { flip(&AudioMixer::Group::solo); };
		context.Events(strip.mute).onClick = [flip]() { flip(&AudioMixer::Group::mute); };
		context.Events(strip.bypass).onClick = [flip]() { flip(&AudioMixer::Group::bypassEffects); };
		context.Events(strip.solo).onRightClick = [this]() {
			AudioMixer* mixer = Mixer();
			if (!mixer) {
				return;
			}
			bool any = false;
			for (AudioMixer::Group& group : mixer->groups) {
				any = any || group.solo;
				group.solo = false;
			}
			if (any) {
				Edited(false);
			}
		};

		UI::Style unitsStyle;
		unitsStyle.flexDirection = UI::FlexDirection::Column;
		unitsStyle.padding = {3.0f, 0.0f, 3.0f, 4.0f};
		UI::ElementHandle units = AddBox(context, root, unitsStyle);
		selects(units);
		for (u32 unit = 0; unit < group.units.size(); ++unit) {
			const AudioMixer::Unit& row = group.units[unit];
			const bool effect = row.kind == AudioMixerUnitKind::Effect;
			const bool routing = row.kind == AudioMixerUnitKind::Send || row.kind == AudioMixerUnitKind::Receive;
			UI::Style slotStyle;
			slotStyle.flexDirection = UI::FlexDirection::Row;
			slotStyle.alignItems = UI::Align::Center;
			slotStyle.padding = {3.0f, 1.0f, 3.0f, 1.0f};
			slotStyle.margin = {0.0f, 1.0f, 0.0f, 0.0f};
			slotStyle.borderRadius = UI::Radii::All(2.0f);
			slotStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(effect	 ? ColorToken::AudioEffectSlot
																			   : routing ? ColorToken::AudioSendSlot
																						 : ColorToken::HeaderFill));
			UI::ElementHandle slot = AddBox(context, units, slotStyle);
			selects(slot);
			context.AddClass(slot, SLOT_CLASS);
			context.Events(slot).onRightClick = [this, id, unit]() {
				if (UI::Context* c = GetPanelContext()) {
					OpenUnitMenu(*c, id, unit, true);
				}
			};
			context.Events(slot).customDraw = [this, id, unit](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				PaintSlot(c, element, paint, id, unit);
			};
			context.Events(slot).onMouseDown = [this, id, unit]() {
				UI::Context* c = GetPanelContext();
				const AudioMixer* mixer = Mixer();
				Ops::ParameterRef ref;
				if (!c || !mixer || !EditingValues() || !SlotLevel(*mixer, id, unit, ref)) {
					return;
				}
				_slotGroup = id;
				_slotUnit = unit;
				_slotStartX = c->GetPointerPosition().x;
				_slotStartValue = Ops::GetValue(*mixer, Target(), ref);
			};
			context.Events(slot).onPressDrag = [this](Vec2 point) {
				if (UI::Context* c = GetPanelContext()) {
					DragSlot(*c, point);
				}
			};
			context.Events(slot).onPressEnd = [this]() { _slotGroup.clear(); };
			UI::ElementHandle dot;
			if (effect) {
				UI::Style dotStyle;
				dotStyle.width = UI::Length::Pixel(8.0f);
				dotStyle.height = UI::Length::Pixel(8.0f);
				dotStyle.flexShrink = 0.0f;
				dotStyle.margin = {0.0f, 0.0f, 4.0f, 0.0f};
				dotStyle.borderRadius = UI::Radii::All(4.0f);
				dotStyle.borderWidth = UI::Edges::All(1.0f);
				dotStyle.borderColor = EditorTheme::GetColor(ColorToken::TextStrong);
				dot = AddBox(context, slot, dotStyle);
				context.AddClass(dot, EditorTheme::Class::Clickable);
				context.SetTooltip(dot, "Bypass this effect");
				context.Events(dot).onClick = [this, id, unit]() {
					AudioMixer* mixer = Mixer();
					const i32 at = mixer ? AudioImpl::FindGroup(*mixer, id) : -1;
					if (at < 0 || !EditingValues() || unit >= mixer->groups[static_cast<size_t>(at)].units.size()) {
						return;
					}
					bool& bypass = mixer->groups[static_cast<size_t>(at)].units[unit].bypass;
					bypass = !bypass;
					Edited(false);
				};
			}
			strip.dots.push_back(dot);
			UI::ElementHandle label = AddText(context, slot, UnitLabel(row), ColorToken::TextStrong);
			UI::Style labelStyle = context.GetStyle(label);
			labelStyle.textOverflow = UI::TextOverflow::Ellipsis;
			context.SetStyle(label, labelStyle);
		}
		UI::Style addStyle;
		addStyle.flexDirection = UI::FlexDirection::Row;
		addStyle.padding = {3.0f, 1.0f, 3.0f, 1.0f};
		addStyle.margin = {0.0f, 1.0f, 0.0f, 0.0f};
		addStyle.borderRadius = UI::Radii::All(2.0f);
		addStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::HeaderFill));
		UI::ElementHandle add = AddBox(context, units, addStyle);
		context.AddClass(add, ADD_SLOT_CLASS);
		context.AddClass(add, EditorTheme::Class::Clickable);
		AddText(context, add, "Add...", ColorToken::TextDim);
		const u32 end = static_cast<u32>(group.units.size());
		context.Events(add).onClick = [this, id, end]() {
			if (UI::Context* c = GetPanelContext()) {
				OpenAddMenu(*c, id, end);
			}
		};

		_parts.push_back(std::move(strip));
	}

	void AudioMixerWindow::Select(const str& group, bool add) {
		const auto at = std::find(_selected.begin(), _selected.end(), group);
		if (!add) {
			_selected.clear();
			_selected.push_back(group);
		} else if (at != _selected.end()) {
			_selected.erase(at);
		} else {
			_selected.push_back(group);
		}
		PushSelectionToList();
	}

	void AudioMixerWindow::RefreshStrips(UI::Context& context) {
		const AudioMixer* mixer = Mixer();
		if (!mixer) {
			return;
		}
		const bool readout = _readoutClock >= READOUT_SECONDS;
		if (readout) {
			_readoutClock = 0.0f;
		}
		const bool readOnly = !EditingValues();
		const f32 travel = EditorTheme::GetMetric(MetricToken::AudioMeterHeight) - 4.0f;
		for (const Strip& strip : _parts) {
			const i32 at = AudioImpl::FindGroup(*mixer, strip.id);
			if (at < 0) {
				continue;
			}
			const AudioMixer::Group& group = mixer->groups[static_cast<size_t>(at)];
			const bool selected = std::find(_selected.begin(), _selected.end(), strip.id) != _selected.end();
			if (context.IsAlive(strip.header)) {
				UI::Style style = context.GetStyle(strip.header);
				style.background = UI::Brush::SolidColor(EditorTheme::GetColor(selected ? ColorToken::Selected : ColorToken::HeaderFill));
				context.SetStyle(strip.header, style);
			}
			auto lit = [&context](UI::ElementHandle element, bool on, ColorToken color, std::string_view letter) {
				if (!context.IsAlive(element)) {
					return;
				}
				UI::Style style = context.GetStyle(element);
				style.background = UI::Brush::SolidColor(EditorTheme::GetColor(on ? color : ColorToken::HeaderFill));
				context.SetStyle(element, style);
				if (const UI::Element* toggle = context.GetElement(element); toggle && toggle->firstChild != UI::INVALID_UI_INDEX) {
					context.SetText(context.MakeHandle(toggle->firstChild), UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
									EditorTheme::GetColor(on ? ColorToken::AudioToggleLitText : ColorToken::Text), letter);
				}
			};
			lit(strip.solo, group.solo, ColorToken::AudioSolo, "S");
			lit(strip.mute, group.mute, ColorToken::AudioMute, "M");
			lit(strip.bypass, group.bypassEffects, ColorToken::AudioBypass, "B");
			for (u32 unit = 0; unit < strip.dots.size() && unit < group.units.size(); ++unit) {
				if (context.IsAlive(strip.dots[unit])) {
					UI::Style style = context.GetStyle(strip.dots[unit]);
					const bool active = !group.units[unit].bypass && !group.bypassEffects;
					style.background = UI::Brush::SolidColor(EditorTheme::GetColor(active ? ColorToken::TextStrong : ColorToken::AudioEffectSlot));
					context.SetStyle(strip.dots[unit], style);
				}
			}

			const f32 decibels = FaderValue(*mixer, strip.id);
			if (context.IsAlive(strip.value)) {
				context.SetText(strip.value, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
								EditorTheme::GetColor(readOnly ? ColorToken::TextFaint : ColorToken::TextStrong), std::format("{}", static_cast<i32>(std::lround(decibels))));
				UI::Style style = context.GetStyle(strip.value);
				style.top = UI::Length::Pixel(Ops::DecibelsToFader(decibels) * (travel - HANDLE_HEIGHT));
				context.SetStyle(strip.value, style);
			}

			if (readout && context.IsAlive(strip.readout)) {
				AudioImpl::AudioBusLevel level;
				f32 loudest = 0.0f;
				if (AudioImpl::GetAudioSystem().ReadBusLevel(mixer->GetBus(strip.id), level)) {
					for (u32 channel = 0; channel < level.channels; ++channel) {
						loudest = std::max(loudest, level.rms[channel]);
					}
				}
				context.SetText(strip.readout, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall), EditorTheme::GetColor(ColorToken::TextDim),
								std::format("{:.1f} dB", Ops::LevelToDecibels(loudest)));
			}
		}
		RefreshTitle(context);
	}

	void AudioMixerWindow::PaintScale(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint) const {
		const UI::Rect box = context.GetLayout(element).content;
		const f32 scale = Scale(context);
		const Color tick = EditorTheme::GetColor(ColorToken::TextFaint);
		for (i32 level = 20; level >= -80; level -= 10) {
			const f32 y = ShareToY(box, HANDLE_HEIGHT * scale, Ops::DecibelsToFader(static_cast<f32>(level)));
			paint.EmitQuad({box.x + box.w - 4.0f * scale, y, 4.0f * scale, std::max(1.0f, scale)}, tick);
		}
	}

	void AudioMixerWindow::PaintMeters(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint, const str& group) const {
		const UI::Rect box = context.GetLayout(element).content;
		if (box.w <= 1.0f || box.h <= 1.0f) {
			return;
		}
		const f32 scale = Scale(context);
		const f32 handle = HANDLE_HEIGHT * scale;
		const AudioMixer* mixer = Mixer();
		AudioImpl::AudioBusLevel level;
		const bool measured = mixer && AudioImpl::GetAudioSystem().ReadBusLevel(mixer->GetBus(group), level);
		const u32 channels = measured && level.channels > 0 ? level.channels : 2;
		const f32 gap = std::max(1.0f, scale);
		const f32 width = std::max(1.0f, (box.w - gap * static_cast<f32>(channels - 1)) / static_cast<f32>(channels));
		const f32 top = ShareToY(box, handle, 0.0f);
		const f32 bottom = ShareToY(box, handle, 1.0f);
		const f32 hot = ShareToY(box, handle, Ops::DecibelsToFader(-6.0f));
		const f32 full = ShareToY(box, handle, Ops::DecibelsToFader(0.0f));
		for (u32 channel = 0; channel < channels; ++channel) {
			const f32 x = box.x + static_cast<f32>(channel) * (width + gap);
			paint.EmitQuad({x, top, width, bottom - top}, EditorTheme::GetColor(ColorToken::AudioMeterWell));
			if (!measured) {
				continue;
			}
			const f32 decibels = Ops::LevelToDecibels(level.rms[channel]);
			if (decibels > Ops::MIN_DECIBELS) {
				const f32 y = ShareToY(box, handle, Ops::DecibelsToFader(decibels));
				auto band = [&](f32 from, f32 to, ColorToken color) {
					const f32 upper = std::max(y, to);
					if (upper < from) {
						paint.EmitQuad({x, upper, width, from - upper}, EditorTheme::GetColor(color));
					}
				};
				band(bottom, hot, ColorToken::AudioMeterLow);
				band(hot, full, ColorToken::AudioMeterHigh);
				band(full, top, ColorToken::AudioMeterClip);
			}
			const f32 peak = Ops::LevelToDecibels(level.peak[channel]);
			if (peak > Ops::MIN_DECIBELS) {
				const f32 y = ShareToY(box, handle, Ops::DecibelsToFader(peak));
				paint.EmitQuad({x, y - gap * 0.5f, width, gap}, EditorTheme::GetColor(peak > 0.0f ? ColorToken::AudioMeterClip : ColorToken::AudioMeterPeak));
			}
		}
	}

	void AudioMixerWindow::PaintFader(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint, const str& group) const {
		const UI::Rect box = context.GetLayout(element).content;
		const AudioMixer* mixer = Mixer();
		if (!mixer || box.h <= 1.0f) {
			return;
		}
		const f32 scale = Scale(context);
		const f32 handle = HANDLE_HEIGHT * scale;
		const f32 track = std::max(1.0f, 2.0f * scale);
		paint.EmitQuad({box.x + (box.w - track) * 0.5f, ShareToY(box, handle, 0.0f), track, box.h - handle}, EditorTheme::GetColor(ColorToken::AudioFaderTrack));
		const f32 y = ShareToY(box, handle, Ops::DecibelsToFader(FaderValue(*mixer, group)));
		const ColorToken color = !EditingValues() ? ColorToken::TextFaint : ColorToken::AudioFaderHandle;
		paint.EmitQuad({box.x, y - handle * 0.5f, box.w, handle}, EditorTheme::GetColor(color), UI::Radii::All(2.0f * scale));
	}

	f32 AudioMixerWindow::FaderValue(const AudioMixer& mixer, const str& group) const {
		return ShownValue(mixer, {group, AudioMixerParameter::Volume, {}, {}});
	}

	void AudioMixerWindow::DragFader(UI::Context& context, Vec2 point) {
		AudioMixer* mixer = Mixer();
		if (!mixer || _dragGroup.empty() || !EditingValues()) {
			return;
		}
		for (const Strip& strip : _parts) {
			if (strip.id != _dragGroup || !context.IsAlive(strip.fader)) {
				continue;
			}
			const UI::Rect box = context.GetLayout(strip.fader).content;
			const f32 range = box.h - HANDLE_HEIGHT * Scale(context);
			if (range <= 1.0f) {
				return;
			}
			const f32 share = std::clamp(_dragStartShare + (point.y - _dragStartY) / range, 0.0f, 1.0f);
			EditValue({_dragGroup, AudioMixerParameter::Volume, {}, {}}, Ops::FaderToDecibels(share));
			return;
		}
	}

	bool AudioMixerWindow::SlotLevel(const AudioMixer& mixer, const str& group, u32 unit, Ops::ParameterRef& ref) const {
		const i32 at = AudioImpl::FindGroup(mixer, group);
		if (at < 0 || unit >= mixer.groups[static_cast<size_t>(at)].units.size()) {
			return false;
		}
		const AudioMixer::Unit& row = mixer.groups[static_cast<size_t>(at)].units[unit];
		if (row.kind == AudioMixerUnitKind::Send && !row.target.empty()) {
			ref = {group, AudioMixerParameter::SendLevel, row.target, {}};
			return true;
		}
		if (row.kind == AudioMixerUnitKind::Effect && row.wetMixing) {
			ref = {group, AudioMixerParameter::WetLevel, row.id, {}};
			return true;
		}
		return false;
	}

	void AudioMixerWindow::PaintSlot(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint, const str& group, u32 unit) const {
		const AudioMixer* mixer = Mixer();
		Ops::ParameterRef ref;
		Ops::ParameterRange range;
		if (!mixer || !SlotLevel(*mixer, group, unit, ref) || !Ops::GetRange(*mixer, ref, range) || !(range.maximum > range.minimum)) {
			return;
		}
		const UI::Rect box = context.GetLayout(element).paddingBox;
		const f32 share = std::clamp((ShownValue(*mixer, ref) - range.minimum) / (range.maximum - range.minimum), 0.0f, 1.0f);
		if (share < 1.0f) {
			const f32 x = box.x + box.w * share;
			paint.EmitQuad({x, box.y, box.x + box.w - x, box.h}, EditorTheme::GetColor(ColorToken::AudioSlotRest));
		}
	}

	void AudioMixerWindow::DragSlot(UI::Context& context, Vec2 point) {
		AudioMixer* mixer = Mixer();
		Ops::ParameterRef ref;
		Ops::ParameterRange range;
		if (!mixer || _slotGroup.empty() || !EditingValues() || !SlotLevel(*mixer, _slotGroup, _slotUnit, ref) || !Ops::GetRange(*mixer, ref, range)) {
			return;
		}
		const f32 width = std::max(1.0f, EditorTheme::GetMetric(MetricToken::AudioStripWidth) * Scale(context));
		EditValue(ref, _slotStartValue + (point.x - _slotStartX) / width * (range.maximum - range.minimum));
	}

	u64 AudioMixerWindow::ChainShapeOf(const AudioMixer* mixer) const {
		u64 hash = StringHash("chain").GetValue();
		auto mix = [&hash](u64 value) { hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2); };
		mix(StringHash(_guid.ToString()).GetValue());
		if (!mixer) {
			return hash;
		}
		mix(ShapeOf(*mixer, CurrentView()));
		const str group = _selected.empty() ? str() : _selected.back();
		mix(StringHash(group).GetValue());
		if (const i32 at = AudioImpl::FindGroup(*mixer, group); at >= 0) {
			const AudioMixer::Group& shown = mixer->groups[static_cast<size_t>(at)];
			mix(shown.bypassEffects ? 1u : 0u);
			for (const AudioMixer::Unit& unit : shown.units) {
				mix(unit.bypass ? 3u : 2u);
			}
		}
		mix(AudioImpl::GetRegisteredEffectCount());
		mix(!EditingValues() ? 5u : 4u);
		for (const AudioMixer::ExposedParameter& exposed : mixer->exposedParameters) {
			mix(StringHash(exposed.group).GetValue() + static_cast<u64>(exposed.parameter) * 31u);
			mix(StringHash(exposed.target).GetValue() ^ StringHash(exposed.effectParameter).GetValue());
		}
		return hash;
	}

	void AudioMixerWindow::RebuildChain(UI::Context& context) {
		_controls.clear();
		if (!context.IsAlive(_chain)) {
			return;
		}
		context.ClearChildren(_chain);
		const AudioMixer* mixer = Mixer();
		_chainGroup = _selected.empty() ? str() : _selected.back();
		const i32 at = mixer && !_chainGroup.empty() ? AudioImpl::FindGroup(*mixer, _chainGroup) : -1;
		if (at < 0) {
			if (mixer) {
				UI::ElementHandle hint = AddText(context, _chain, "Select a group to edit its effects", ColorToken::TextDim);
				UI::Style hintStyle = context.GetStyle(hint);
				hintStyle.margin = {8.0f, 8.0f, 8.0f, 8.0f};
				context.SetStyle(hint, hintStyle);
			}
			return;
		}
		const AudioMixer::Group& group = mixer->groups[static_cast<size_t>(at)];
		const str id = group.id;
		const bool readOnly = !EditingValues();

		UI::Style headStyle;
		headStyle.flexDirection = UI::FlexDirection::Row;
		headStyle.alignItems = UI::Align::Center;
		headStyle.flexShrink = 0.0f;
		headStyle.padding = {8.0f, 4.0f, 4.0f, 4.0f};
		headStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::HeaderFill));
		UI::ElementHandle head = AddBox(context, _chain, headStyle);
		AddText(context, head, group.name, ColorToken::TextStrong, MetricToken::FontBody);
		UI::Widgets::Spacer(context, head);
		EditorIcons::IconButton(context, head, EditorIcons::Glyph::More, "...", "Copy all effect settings to all snapshots", [this, id]() {
			UI::Context* c = GetPanelContext();
			if (!c || !Mixer() || !EditingValues()) {
				return;
			}
			Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
			UI::Widgets::MenuItem copy;
			copy.label = "Copy all effect settings to all snapshots";
			copy.onSelect = [this, id]() {
				if (AudioMixer* live = Mixer(); live && EditingValues()) {
					Ops::CopyGroupToAllSnapshots(*live, Target(), id);
					Edited(false);
				}
			};
			items.push_back(std::move(copy));
			UI::Widgets::OpenMenu(*c, c->GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
		});

		const UI::ElementHandle pitch = AddValueRow(context, _chain, "Pitch", {id, AudioMixerParameter::Pitch, {}, {}});
		if (readOnly && context.IsAlive(pitch)) {
			MakeInert(context, pitch);
		}

		for (u32 unit = 0; unit < group.units.size(); ++unit) {
			BuildUnit(context, *mixer, static_cast<u32>(at), unit, readOnly);
		}

		UI::Style footStyle;
		footStyle.flexDirection = UI::FlexDirection::Row;
		footStyle.justifyContent = UI::Align::Center;
		footStyle.flexShrink = 0.0f;
		footStyle.padding = {8.0f, 10.0f, 8.0f, 10.0f};
		UI::ElementHandle foot = AddBox(context, _chain, footStyle);
		UI::Widgets::ButtonDesc add;
		add.label = "Add Effect";
		add.font = UISystem::GetDefaultFont();
		add.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
		const u32 end = static_cast<u32>(group.units.size());
		add.onClick = [this, id, end]() {
			if (UI::Context* c = GetPanelContext()) {
				OpenAddMenu(*c, id, end);
			}
		};
		const UI::ElementHandle button = UI::Widgets::Button(context, foot, add);
		if (readOnly) {
			MakeInert(context, button);
		}
	}

	void AudioMixerWindow::BuildUnit(UI::Context& context, const AudioMixer& mixer, u32 groupIndex, u32 index, bool inert) {
		const AudioMixer::Group& group = mixer.groups[groupIndex];
		const AudioMixer::Unit& unit = group.units[index];
		const str id = group.id;

		UI::Style blockStyle;
		blockStyle.flexDirection = UI::FlexDirection::Column;
		blockStyle.flexShrink = 0.0f;
		blockStyle.margin = {0.0f, 1.0f, 0.0f, 1.0f};
		UI::ElementHandle block = AddBox(context, _chain, blockStyle);
		context.AddClass(block, UNIT_CLASS);
		context.Events(block).onDragOver = [this, id](const UI::DragPayload& payload) {
			return payload.type == UNIT_PAYLOAD && payload.data.TryGet<u32>() && _chainGroup == id && EditingValues();
		};
		context.Events(block).onDrop = [this, id, index, block](const UI::DragPayload& payload, Vec2 point) {
			const u32* from = payload.type == UNIT_PAYLOAD ? payload.data.TryGet<u32>() : nullptr;
			UI::Context* c = GetPanelContext();
			if (!from || !c || !c->IsAlive(block)) {
				return;
			}
			const UI::Rect box = c->GetLayout(block).borderBox;
			MoveUnit(id, *from, point.y < box.y + box.h * 0.5f ? index : index + 1);
		};

		const bool effect = unit.kind == AudioMixerUnitKind::Effect;
		const bool routing = unit.kind == AudioMixerUnitKind::Send || unit.kind == AudioMixerUnitKind::Receive;
		UI::Style headerStyle;
		headerStyle.flexDirection = UI::FlexDirection::Row;
		headerStyle.alignItems = UI::Align::Center;
		headerStyle.padding = {8.0f, 2.0f, 4.0f, 2.0f};
		headerStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::HeaderFill));
		UI::ElementHandle header = AddBox(context, block, headerStyle);
		context.AddClass(header, UNIT_HEADER_CLASS);
		UI::Style codeStyle;
		codeStyle.width = UI::Length::Pixel(6.0f);
		codeStyle.height = UI::Length::Pixel(6.0f);
		codeStyle.flexShrink = 0.0f;
		codeStyle.margin = {0.0f, 0.0f, 6.0f, 0.0f};
		codeStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(effect	 ? ColorToken::AudioEffectSlot
																		   : routing ? ColorToken::AudioSendSlot
																					 : ColorToken::TextDim));
		context.SetHoverable(AddBox(context, header, codeStyle), false);
		const str label = str(UnitLabel(unit));
		UI::ElementHandle name = AddText(context, header, label, ColorToken::TextStrong);
		UI::Style nameStyle = context.GetStyle(name);
		nameStyle.flexGrow = 1.0f;
		nameStyle.textOverflow = UI::TextOverflow::Ellipsis;
		context.SetStyle(name, nameStyle);
		EditorIcons::IconButton(context, header, EditorIcons::Glyph::More, "...", "Options for this unit", [this, id, index]() {
			if (UI::Context* c = GetPanelContext()) {
				OpenUnitMenu(*c, id, index, false);
			}
		});
		context.Events(header).onRightClick = [this, id, index]() {
			if (UI::Context* c = GetPanelContext()) {
				OpenUnitMenu(*c, id, index, false);
			}
		};
		context.Events(header).onPressDrag = [this, index, label](Vec2) {
			if (UISystem::IsDragPayloadActive() || !EditingValues()) {
				return;
			}
			UI::DragPayload payload;
			payload.type = UNIT_PAYLOAD;
			payload.data = Variant(index);
			payload.display = label;
			UISystem::BeginDragPayload(payload);
		};

		UI::Style bodyStyle;
		bodyStyle.flexDirection = UI::FlexDirection::Column;
		bodyStyle.padding = {0.0f, 3.0f, 0.0f, 5.0f};
		UI::ElementHandle body = AddBox(context, block, bodyStyle);
		bool aimed = false;
		for (const AudioMixer::Group& sender : mixer.groups) {
			for (const AudioMixer::Unit& send : sender.units) {
				aimed = aimed || (send.kind == AudioMixerUnitKind::Send && send.target == id && !unit.id.empty() && send.targetUnit == unit.id);
			}
		}
		auto note = [&context, body](std::string_view text) {
			UI::ElementHandle line = AddText(context, body, text, ColorToken::TextDim);
			UI::Style lineStyle = context.GetStyle(line);
			lineStyle.margin = {8.0f, 2.0f, 8.0f, 2.0f};
			context.SetStyle(line, lineStyle);
		};
		switch (unit.kind) {
		case AudioMixerUnitKind::Attenuation: {
			AddValueRow(context, body, "Volume", {id, AudioMixerParameter::Volume, {}, {}});
			UI::Style meterStyle;
			meterStyle.height = UI::Length::Pixel(UNIT_METER_HEIGHT * 2.0f + 1.0f);
			meterStyle.flexShrink = 0.0f;
			meterStyle.margin = {8.0f, 3.0f, 8.0f, 2.0f};
			UI::ElementHandle meter = AddBox(context, body, meterStyle);
			context.SetTooltip(meter, "Lvl just past this group's fader");
			context.Events(meter).customDraw = [this, id](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				PaintUnitMeter(c, element, paint, id);
			};
			break;
		}
		case AudioMixerUnitKind::Send: {
			UI::Style rowStyle;
			rowStyle.flexDirection = UI::FlexDirection::Row;
			rowStyle.alignItems = UI::Align::Center;
			rowStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
			UI::ElementHandle row = AddBox(context, body, rowStyle);
			UI::ElementHandle caption = AddText(context, row, "Receive", ColorToken::Text);
			UI::Style captionStyle = context.GetStyle(caption);
			captionStyle.width = UI::Length::Pixel(LABEL_WIDTH);
			captionStyle.flexShrink = 0.0f;
			context.SetStyle(caption, captionStyle);
			const str aimedAt = unit.target.empty() ? str("None") : TargetLabel(mixer, unit.target, unit.targetUnit);
			UI::Widgets::ButtonDesc target;
			target.label = aimedAt;
			target.font = UISystem::GetDefaultFont();
			target.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			target.onClick = [this, id, index]() {
				if (UI::Context* c = GetPanelContext()) {
					OpenSendMenu(*c, id, index);
				}
			};
			UI::Widgets::Button(context, row, target);
			if (!unit.target.empty()) {
				AddValueRow(context, body, "Send level", {id, AudioMixerParameter::SendLevel, unit.target, {}});
			}
			break;
		}
		case AudioMixerUnitKind::Receive:
			if (!aimed) {
				note("No Send sources connected.");
			}
			break;
		case AudioMixerUnitKind::Effect: {
			const IAudioEffectDefinition* definition = AudioImpl::FindEffect(unit.effect);
			if (definition && definition->IsSidechainTarget() && !aimed) {
				note("No Send sources connected.");
			}
			if (unit.wetMixing) {
				AddValueRow(context, body, "Wet", {id, AudioMixerParameter::WetLevel, unit.id, {}});
			}
			if (!definition) {
				note(std::format("'{}' isn't registered (its sound passes through)", unit.effect));
				break;
			}
			for (const AudioParameterDefinition& parameter : definition->GetParameters()) {
				AddValueRow(context, body, parameter.name, {id, AudioMixerParameter::EffectParameter, unit.id, str(parameter.name)});
			}
			break;
		}
		}
		if (inert || (effect && (unit.bypass || group.bypassEffects))) {
			MakeInert(context, body);
		}
	}

	UI::ElementHandle AudioMixerWindow::AddValueRow(UI::Context& context, UI::ElementHandle parent, std::string_view label, Ops::ParameterRef ref) {
		const AudioMixer* mixer = Mixer();
		Ops::ParameterRange range;
		if (!mixer || !Ops::GetRange(*mixer, ref, range)) {
			return {};
		}
		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		rowStyle.alignItems = UI::Align::Center;
		rowStyle.flexShrink = 0.0f;
		rowStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
		UI::ElementHandle row = AddBox(context, parent, rowStyle);
		const bool exposed = Ops::FindExposed(*mixer, ref) >= 0;
		UI::ElementHandle name = AddText(context, row, exposed ? str(label) + str(EXPOSED_MARK) : str(label), ColorToken::Text);
		UI::Style nameStyle = context.GetStyle(name);
		nameStyle.width = UI::Length::Pixel(LABEL_WIDTH);
		nameStyle.flexShrink = 0.0f;
		nameStyle.textOverflow = UI::TextOverflow::Ellipsis;
		context.SetStyle(name, nameStyle);
		context.SetHoverable(name, true);
		context.Events(name).onRightClick = [this, ref]() {
			if (UI::Context* c = GetPanelContext()) {
				OpenParameterMenu(*c, ref);
			}
		};

		const f32 scale = range.displayScale;
		UI::Widgets::SliderDesc desc;
		desc.minValue = range.minimum * scale;
		desc.maxValue = range.maximum * scale;
		desc.value = ShownValue(*mixer, ref) * scale;
		desc.showValueField = true;
		desc.decimals = 2;
		desc.font = UISystem::GetDefaultFont();
		desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
		desc.onChange = [this, ref, scale](f32 shown) { EditValue(ref, shown / scale); };
		Control control;
		control.ref = std::move(ref);
		control.scale = scale;
		control.slider = UI::Widgets::Slider(context, row, desc);
		UI::Style sliderStyle = context.GetStyle(control.slider.root);
		sliderStyle.flexGrow = 1.0f;
		sliderStyle.minWidth = UI::Length::Pixel(0.0f);
		context.SetStyle(control.slider.root, sliderStyle);
		UI::ElementHandle unit = AddText(context, row, range.unit, ColorToken::TextDim);
		UI::Style unitStyle = context.GetStyle(unit);
		unitStyle.width = UI::Length::Pixel(24.0f);
		unitStyle.flexShrink = 0.0f;
		unitStyle.margin = {4.0f, 0.0f, 0.0f, 0.0f};
		context.SetStyle(unit, unitStyle);
		_controls.push_back(std::move(control));
		return row;
	}

	void AudioMixerWindow::RefreshChain(UI::Context& context) {
		const AudioMixer* mixer = Mixer();
		if (!mixer) {
			return;
		}
		const UI::ElementHandle focused = context.GetFocused();
		for (const Control& control : _controls) {
			if (!context.IsAlive(control.slider.root) || focused == control.slider.value.field || focused == control.slider.value.root) {
				continue;
			}
			Ops::ParameterRange range;
			if (Ops::GetRange(*mixer, control.ref, range)) {
				UI::Widgets::SetSliderValue(context, control.slider, range.minimum * control.scale, range.maximum * control.scale,
											ShownValue(*mixer, control.ref) * control.scale);
			}
		}
	}

	void AudioMixerWindow::PaintUnitMeter(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint, const str& group) const {
		const UI::Rect box = context.GetLayout(element).content;
		if (box.w <= 1.0f || box.h <= 1.0f) {
			return;
		}
		const AudioMixer* mixer = Mixer();
		AudioImpl::AudioBusLevel level;
		const bool measured = mixer && AudioImpl::GetAudioSystem().ReadBusLevel(mixer->GetBus(group), level, AudioImpl::AudioMeterPoint::Fader);
		const u32 channels = measured && level.channels > 0 ? level.channels : 2;
		const f32 gap = std::max(1.0f, Scale(context));
		const f32 height = std::max(1.0f, (box.h - gap * static_cast<f32>(channels - 1)) / static_cast<f32>(channels));
		auto across = [&box](f32 decibels) { return box.x + box.w * (1.0f - Ops::DecibelsToFader(decibels)); };
		const f32 hot = across(-6.0f);
		const f32 full = across(0.0f);
		for (u32 channel = 0; channel < channels; ++channel) {
			const f32 y = box.y + static_cast<f32>(channel) * (height + gap);
			paint.EmitQuad({box.x, y, box.w, height}, EditorTheme::GetColor(ColorToken::AudioMeterWell));
			if (!measured) {
				continue;
			}
			const f32 decibels = Ops::LevelToDecibels(level.rms[channel]);
			if (decibels > Ops::MIN_DECIBELS) {
				const f32 reach = across(decibels);
				auto band = [&](f32 from, f32 to, ColorToken color) {
					const f32 end = std::min(reach, to);
					if (end > from) {
						paint.EmitQuad({from, y, end - from, height}, EditorTheme::GetColor(color));
					}
				};
				band(box.x, hot, ColorToken::AudioMeterLow);
				band(hot, full, ColorToken::AudioMeterHigh);
				band(full, box.x + box.w, ColorToken::AudioMeterClip);
			}
			const f32 peak = Ops::LevelToDecibels(level.peak[channel]);
			if (peak > Ops::MIN_DECIBELS) {
				paint.EmitQuad({across(peak) - gap * 0.5f, y, gap, height}, EditorTheme::GetColor(peak > 0.0f ? ColorToken::AudioMeterClip : ColorToken::AudioMeterPeak));
			}
		}
	}

	void AudioMixerWindow::EditValue(const Ops::ParameterRef& ref, f32 value) {
		AudioMixer* mixer = Mixer();
		if (!mixer || !EditingValues() || !Ops::SetValue(*mixer, Target(), ref, value)) {
			return;
		}
		PlaceMix();
		EditorAssetOps::MarkDirty(Handle<Asset>(mixer));
	}

	f32 AudioMixerWindow::ShownValue(const AudioMixer& mixer, const Ops::ParameterRef& ref) const {
		f32 live = 0.0f;
		if (!EditingValues() && Ops::GetLiveValue(mixer, ref, live)) {
			return live;
		}
		return Ops::GetValue(mixer, Target(), ref);
	}

	void AudioMixerWindow::OpenUnitMenu(UI::Context& context, const str& group, u32 index, bool strip) {
		const AudioMixer* mixer = Mixer();
		const i32 at = mixer ? AudioImpl::FindGroup(*mixer, group) : -1;
		if (at < 0 || !EditingValues() || index >= mixer->groups[static_cast<size_t>(at)].units.size()) {
			return;
		}
		const AudioMixer::Unit& unit = mixer->groups[static_cast<size_t>(at)].units[index];
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		bool separate = false;
		if (unit.kind == AudioMixerUnitKind::Effect) {
			UI::Widgets::MenuItem wet;
			wet.label = "Allow Wet Mixing (causes higher memory usage)";
			wet.checked = unit.wetMixing;
			wet.onSelect = [this, group, index, on = !unit.wetMixing]() {
				if (AudioMixer* live = Mixer(); live && EditingValues() && Ops::SetWetMixing(*live, group, index, on)) {
					Edited(true);
				}
			};
			items.push_back(std::move(wet));
			UI::Widgets::MenuItem bypass;
			bypass.label = "Bypass";
			bypass.checked = unit.bypass;
			bypass.onSelect = [this, group, index]() {
				AudioMixer* live = Mixer();
				const i32 in = live ? AudioImpl::FindGroup(*live, group) : -1;
				if (in < 0 || !EditingValues() || index >= live->groups[static_cast<size_t>(in)].units.size()) {
					return;
				}
				bool& value = live->groups[static_cast<size_t>(in)].units[index].bypass;
				value = !value;
				Edited(false);
			};
			items.push_back(std::move(bypass));
			separate = true;
		}
		if (!strip && unit.kind != AudioMixerUnitKind::Receive) {
			auto copy = [&](std::string_view label, bool wet) {
				UI::Widgets::MenuItem item;
				item.label = str(label);
				item.separatorBefore = separate;
				separate = false;
				item.onSelect = [this, group, index, wet]() {
					if (AudioMixer* live = Mixer(); live && EditingValues()) {
						Ops::CopyUnitToAllSnapshots(*live, Target(), group, index, wet);
						Edited(false);
					}
				};
				items.push_back(std::move(item));
			};
			copy("Copy effect settings to all snapshots", false);
			if (unit.kind == AudioMixerUnitKind::Effect && unit.wetMixing) {
				copy("Copy effect settings to all snapshots (including wet level)", true);
			}
			separate = true;
		}
		UI::Widgets::MenuItem before;
		before.label = "Add effect before";
		before.separatorBefore = separate;
		before.children = AddEffectItems(std::span<const str>(&group, 1), index);
		items.push_back(std::move(before));
		UI::Widgets::MenuItem after;
		after.label = "Add effect after";
		after.children = AddEffectItems(std::span<const str>(&group, 1), index + 1);
		items.push_back(std::move(after));
		if (unit.kind != AudioMixerUnitKind::Attenuation) {
			UI::Widgets::MenuItem remove;
			remove.label = strip ? "Remove" : "Remove this effect";
			remove.separatorBefore = true;
			remove.onSelect = [this, group, index]() {
				if (AudioMixer* live = Mixer(); live && EditingValues() && Ops::RemoveUnit(*live, group, index)) {
					Edited(true);
				}
			};
			items.push_back(std::move(remove));
		}
		if (strip && unit.kind == AudioMixerUnitKind::Send) {
			bool first = true;
			if (!unit.target.empty()) {
				UI::Widgets::MenuItem disconnect;
				disconnect.label = std::format("Disconnect from '{}'", TargetLabel(*mixer, unit.target, unit.targetUnit));
				disconnect.separatorBefore = true;
				first = false;
				disconnect.onSelect = [this, group, index]() {
					if (AudioMixer* live = Mixer(); live && EditingValues() && Ops::RetargetSend(*live, group, index, {}, {})) {
						Edited(true);
					}
				};
				items.push_back(std::move(disconnect));
			}
			Vector<Ops::SendTarget> targets{GetMemoryResource(MemTag::Editor)};
			Ops::ListSendTargets(*mixer, group, index, targets);
			for (const Ops::SendTarget& place : targets) {
				if (place.group == unit.target && place.unit == unit.targetUnit) {
					continue;
				}
				const str label = TargetLabel(*mixer, place.group, place.unit);
				UI::Widgets::MenuItem connect;
				connect.label = place.loops ? std::format("A connection to '{}' would result in a feedback loop", label) : std::format("Connect to '{}'", label);
				connect.enabled = !place.loops;
				connect.separatorBefore = first;
				first = false;
				connect.onSelect = [this, group, index, target = place.group, aimed = place.unit]() {
					if (AudioMixer* live = Mixer(); live && EditingValues() && Ops::RetargetSend(*live, group, index, target, aimed)) {
						Edited(true);
					}
				};
				items.push_back(std::move(connect));
			}
		}
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	void AudioMixerWindow::OpenAddMenu(UI::Context& context, const str& group, u32 at) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || !EditingValues() || AudioImpl::FindGroup(*mixer, group) < 0) {
			return;
		}
		const Vector<UI::Widgets::MenuItem> items = AddEffectItems(std::span<const str>(&group, 1), at);
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	void AudioMixerWindow::OpenSendMenu(UI::Context& context, const str& group, u32 index) {
		const AudioMixer* mixer = Mixer();
		const i32 at = mixer ? AudioImpl::FindGroup(*mixer, group) : -1;
		if (at < 0 || !EditingValues() || index >= mixer->groups[static_cast<size_t>(at)].units.size()) {
			return;
		}
		const AudioMixer::Unit& send = mixer->groups[static_cast<size_t>(at)].units[index];
		if (send.kind != AudioMixerUnitKind::Send) {
			return;
		}
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		UI::Widgets::MenuItem none;
		none.label = "None";
		none.checked = send.target.empty();
		none.onSelect = [this, group, index]() {
			if (AudioMixer* live = Mixer(); live && EditingValues() && Ops::RetargetSend(*live, group, index, {}, {})) {
				Edited(true);
			}
		};
		items.push_back(std::move(none));
		Vector<Ops::SendTarget> targets{GetMemoryResource(MemTag::Editor)};
		Ops::ListSendTargets(*mixer, group, index, targets);
		bool first = true;
		for (const Ops::SendTarget& place : targets) {
			const str label = TargetLabel(*mixer, place.group, place.unit);
			UI::Widgets::MenuItem item;
			item.label = place.loops ? std::format("A connection to '{}' would result in a feedback loop", label) : std::format("'{}'", label);
			item.enabled = !place.loops;
			item.checked = send.target == place.group && send.targetUnit == place.unit;
			item.separatorBefore = first;
			first = false;
			item.onSelect = [this, group, index, target = place.group, aimed = place.unit]() {
				if (AudioMixer* live = Mixer(); live && EditingValues() && Ops::RetargetSend(*live, group, index, target, aimed)) {
					Edited(true);
				}
			};
			items.push_back(std::move(item));
		}
		if (targets.empty()) {
			UI::Widgets::MenuItem nothing;
			nothing.label = "No valid Receive targets found";
			nothing.enabled = false;
			nothing.separatorBefore = true;
			items.push_back(std::move(nothing));
		}
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	Vector<UI::Widgets::MenuItem> AudioMixerWindow::AddEffectItems(std::span<const str> groups, u32 at) {
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		const AudioMixer* mixer = Mixer();
		bool full = true;
		for (const str& group : groups) {
			const i32 in = mixer ? AudioImpl::FindGroup(*mixer, group) : -1;
			full = full && (in < 0 || mixer->groups[static_cast<size_t>(in)].units.size() >= AudioImpl::MAX_BUS_ROWS);
		}
		Vector<str> targets{groups.begin(), groups.end(), GetMemoryResource(MemTag::Editor)};
		auto add = [&](std::string_view label, AudioMixerUnitKind kind, std::string_view effect) {
			UI::Widgets::MenuItem item;
			item.label = str(label);
			item.enabled = !full;
			item.onSelect = [this, targets, at, kind, effect = str(effect)]() {
				AudioMixer* live = Mixer();
				if (!live || !EditingValues()) {
					return;
				}
				bool added = false;
				for (const str& group : targets) {
					added = Ops::AddUnit(*live, group, at, kind, effect) >= 0 || added;
				}
				if (added) {
					Edited(true);
				}
			};
			items.push_back(std::move(item));
		};
		add("Send", AudioMixerUnitKind::Send, {});
		add("Receive", AudioMixerUnitKind::Receive, {});
		for (u32 index = 0; index < AudioImpl::GetRegisteredEffectCount(); ++index) {
			const std::string_view name = AudioImpl::GetRegisteredEffectName(index);
			add(name, AudioMixerUnitKind::Effect, name);
		}
		return items;
	}

	void AudioMixerWindow::OpenParameterMenu(UI::Context& context, const Ops::ParameterRef& ref) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || !EditingValues()) {
			return;
		}
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		UI::Widgets::MenuItem expose;
		if (Ops::FindExposed(*mixer, ref) >= 0) {
			expose.label = "Unexpose";
			expose.onSelect = [this, ref]() {
				if (AudioMixer* live = Mixer(); live && EditingValues() && Ops::UnexposeParameter(*live, ref)) {
					Edited(false);
				}
			};
		} else {
			expose.label = std::format("Expose '{}' to script", Ops::DescribeParameter(*mixer, ref));
			expose.onSelect = [this, ref]() {
				if (AudioMixer* live = Mixer(); live && EditingValues() && !Ops::ExposeParameter(*live, ref).empty()) {
					Edited(false);
				}
			};
		}
		items.push_back(std::move(expose));
		const AudioMixerTransitionType current = Ops::GetTransition(*mixer, Target(), ref);
		for (u8 type = 0; type <= static_cast<u8>(AudioMixerTransitionType::Attenuation); ++type) {
			UI::Widgets::MenuItem item;
			item.label = str(TRANSITION_LABELS[type]);
			item.checked = static_cast<u8>(current) == type;
			item.separatorBefore = type == 0;
			item.onSelect = [this, ref, type]() {
				if (AudioMixer* live = Mixer(); live && EditingValues() && Ops::SetTransition(*live, Target(), ref, static_cast<AudioMixerTransitionType>(type))) {
					Edited(false);
				}
			};
			items.push_back(std::move(item));
		}
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
	}

	void AudioMixerWindow::RefreshExposed(UI::Context& context) {
		const AudioMixer* mixer = Mixer();
		if (context.IsAlive(_exposed)) {
			UI::Style style = context.GetStyle(_exposed);
			style.display = mixer ? UI::Display::Flex : UI::Display::None;
			context.SetStyle(_exposed, style);
			if (const UI::Element* button = context.GetElement(_exposed); button && button->firstChild != UI::INVALID_UI_INDEX) {
				context.SetText(context.MakeHandle(button->firstChild), UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
								EditorTheme::GetColor(ColorToken::Text), std::format("Exposed Parameters ({})", mixer ? mixer->exposedParameters.size() : 0u));
			}
		}
		if (_exposedPopup.IsValid() && !(context.IsAlive(_exposedPopup) && context.IsPopupOpen(_exposedPopup))) {
			_exposedPopup = {};
			_renamingExposed = -1;
			_exposedNeedsRebuild = false;
		}
		if (_exposedNeedsRebuild) {
			_exposedNeedsRebuild = false;
			BuildExposedRows(context);
		}
	}

	void AudioMixerWindow::OpenExposedList(UI::Context& context) {
		CloseExposedList();
		UI::Style listStyle;
		listStyle.flexDirection = UI::FlexDirection::Column;
		listStyle.padding = {2.0f, 3.0f, 2.0f, 3.0f};
		listStyle.minWidth = UI::Length::Pixel(125.0f);
		listStyle.minHeight = UI::Length::Pixel(23.0f);
		listStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
		listStyle.borderWidth = UI::Edges::All(1.0f);
		listStyle.borderColor = EditorTheme::GetColor(ColorToken::HeaderFill);
		_exposedPopup = context.CreateElement();
		context.SetStyle(_exposedPopup, listStyle);
		_renamingExposed = -1;
		BuildExposedRows(context);
		const UI::Rect button = context.IsAlive(_exposed) ? context.GetLayout(_exposed).borderBox : UI::Rect{};
		context.OpenPopup(_exposedPopup, Vec2(button.x, button.y + button.h));
	}

	void AudioMixerWindow::CloseExposedList() {
		UI::Context* context = GetPanelContext();
		if (context && context->IsAlive(_exposedPopup) && context->IsPopupOpen(_exposedPopup)) {
			context->ClosePopup(_exposedPopup);
		}
		_exposedPopup = {};
		_renamingExposed = -1;
	}

	void AudioMixerWindow::BuildExposedRows(UI::Context& context) {
		const AudioMixer* mixer = Mixer();
		if (!mixer || !context.IsAlive(_exposedPopup)) {
			return;
		}
		context.ClearChildren(_exposedPopup);
		for (u32 index = 0; index < mixer->exposedParameters.size(); ++index) {
			const AudioMixer::ExposedParameter& exposed = mixer->exposedParameters[index];
			UI::Style rowStyle;
			rowStyle.flexDirection = UI::FlexDirection::Row;
			rowStyle.alignItems = UI::Align::Center;
			rowStyle.padding = {4.0f, 1.0f, 4.0f, 1.0f};
			UI::ElementHandle row = AddBox(context, _exposedPopup, rowStyle);
			context.AddClass(row, EXPOSED_ROW_CLASS);
			if (_renamingExposed == static_cast<i32>(index)) {
				UI::Widgets::TextFieldDesc field;
				field.text = exposed.name;
				field.font = UISystem::GetDefaultFont();
				field.fontSizePx = EditorTheme::GetMetric(MetricToken::Font);
				field.onCommit = [this, index](std::string_view text) {
					if (AudioMixer* live = Mixer(); live && Ops::RenameExposed(*live, index, text)) {
						Edited(false);
					}
					_renamingExposed = -1;
					_exposedNeedsRebuild = true;
				};
				const UI::ElementHandle input = UI::Widgets::TextField(context, row, field);
				context.SetFocus(input);
			} else {
				AddText(context, row, exposed.name, ColorToken::Text);
			}
			UI::Widgets::Spacer(context, row);
			UI::ElementHandle path = AddText(context, row, Ops::DescribeParameter(*mixer, {exposed.group, exposed.parameter, exposed.target, exposed.effectParameter}),
											 ColorToken::TextDim);
			UI::Style pathStyle = context.GetStyle(path);
			pathStyle.margin = {25.0f, 0.0f, 0.0f, 0.0f};
			context.SetStyle(path, pathStyle);
			context.Events(row).onRightClick = [this, index]() {
				UI::Context* c = GetPanelContext();
				if (!c || !Mixer()) {
					return;
				}
				Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
				UI::Widgets::MenuItem unexpose;
				unexpose.label = "Unexpose";
				unexpose.onSelect = [this, index]() {
					AudioMixer* live = Mixer();
					if (live && index < live->exposedParameters.size()) {
						const AudioMixer::ExposedParameter exposed = live->exposedParameters[index];
						if (Ops::UnexposeParameter(*live, {exposed.group, exposed.parameter, exposed.target, exposed.effectParameter})) {
							Edited(false);
						}
					}
					_exposedNeedsRebuild = true;
				};
				items.push_back(std::move(unexpose));
				UI::Widgets::MenuItem rename;
				rename.label = "Rename";
				rename.onSelect = [this, index]() {
					_renamingExposed = static_cast<i32>(index);
					_exposedNeedsRebuild = true;
				};
				items.push_back(std::move(rename));
				UI::Widgets::OpenMenu(*c, c->GetPointerPosition(), items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
			};
		}
	}

	void AudioMixerWindow::MoveUnit(const str& group, u32 from, u32 to) {
		if (AudioMixer* mixer = Mixer(); mixer && EditingValues() && Ops::MoveUnit(*mixer, group, from, to)) {
			Edited(true);
		}
	}

} // namespace Shalltear::AudioEditorImpl

namespace Shalltear::AudioEditor {

	void ReleaseMixerWindows() {
		if (AudioEditorImpl::_shared) {
			Delete(AudioEditorImpl::_shared);
			AudioEditorImpl::_shared = nullptr;
		}
	}

} // namespace Shalltear::AudioEditor

SLT_REGISTER_TYPE(Shalltear::AudioEditorImpl::AudioMixerWindow)
