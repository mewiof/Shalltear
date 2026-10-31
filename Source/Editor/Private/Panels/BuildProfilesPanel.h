#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorPanel.h"

namespace Shalltear {

	class BuildProfilesPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(BuildProfilesPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Build Profiles")
		SLT_EDITOR_PANEL_CATEGORY("Build")
		SLT_EDITOR_PANEL_FLOATING_SIZE(760.0f, 560.0f)
		SLT_REFLECT_FIELD(selectedProfile)
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		BuildProfilesPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;
		void OnOpen() override;

		str selectedProfile;

		static str GetSharedSelection();
	private:
		struct ProfileView {
			str name;
			str targetId;
			str targetDisplay;
			bool targetKnown = true;
			bool available = true;
			str unavailableReason;
			bool includeInBuildAll = true;

			ResolvedProfile resolved;
			Vector<BuildSettingDesc> schema{GetMemoryResource(MemTag::Editor)};
			Vector<BuildIssue> issues{GetMemoryResource(MemTag::Editor)};
			u32 errors = 0;
			u32 warnings = 0;

			str lastResult;
			str lastReason;
			str lastOutput;
			i64 lastTimeUnix = 0;
			f64 lastDurationSeconds = 0.0;
			u64 lastArtifactBytes = 0;
			u32 lastBuildNumber = 0;
		};

		void Refresh();
		const ProfileView* Selected() const;

		void RebuildList(UI::Context& context);
		void RebuildPane(UI::Context& context);
		void RefreshFooter(UI::Context& context);

		void BuildEmptyState(UI::Context& context, UI::ElementHandle parent);
		void BuildListEmptyState(UI::Context& context, UI::ElementHandle host);
		void DecorateRow(UI::Context& context, const ProfileView& view, const UI::Widgets::TreeRowParts& parts);
		void BuildGeneralSection(UI::Context& context, UI::ElementHandle parent, const ProfileView& view);
		void BuildOptionsSection(UI::Context& context, UI::ElementHandle parent, const ProfileView& view);
		void BuildScenesSection(UI::Context& context, UI::ElementHandle parent, const ProfileView& view);
		void BuildStepsSection(UI::Context& context, UI::ElementHandle parent);
		void BuildValidationSection(UI::Context& context, UI::ElementHandle parent, const ProfileView& view);
		void BuildLastResultSection(UI::Context& context, UI::ElementHandle parent, const ProfileView& view);

		UI::ElementHandle BuildFieldRow(UI::Context& context, UI::ElementHandle parent, const ProfileView& view,
										const BuildSettingDesc& desc, const ResolvedSetting& setting);
		void AddExpandedPreview(UI::Context& context, UI::ElementHandle parent, const ResolvedSetting& setting);

		void WriteSetting(std::string_view key, std::string_view value);
		void WriteList(std::string_view key, const Vector<str>& items);
		void ClearSetting(std::string_view key);

		void OpenRowMenu(UI::Context& context, const str& name);
		void RequestBuild(UI::Context& context, bool all, bool run);
		void MoveSelected(i32 delta);

		void DeleteProfileFlow(UI::Context& context, const str& name);
		void DuplicateProfileFlow(UI::Context& context, const str& name);
		void RenameProfileFlow(UI::Context& context, const str& name);

		static u64 RowIdOf(std::string_view name);
		str NameOfRow(u64 id) const;
		void TakeSelectionFromList();
		void PushSelectionToList();

		str _projectPath;
		Vector<ProfileView> _views{GetMemoryResource(MemTag::Editor)};
		bool _dirty = true;
		bool _paneDirty = false;

		EditorListView _list;

		UI::ElementHandle _paneViewport;
		UI::ElementHandle _stepLabel;
		UI::ElementHandle _resultLabel;
		UI::ElementHandle _buildButton;
		UI::ElementHandle _buildAllButton;
		UI::ElementHandle _runButton;

		BuildState _lastDrawnState = BuildState::Idle;
		str _lastDrawnStatus;
	};

} // namespace Shalltear
