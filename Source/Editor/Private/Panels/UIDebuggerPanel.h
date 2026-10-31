#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/UIDebugOps.h"

namespace Shalltear {

	class UIDebuggerPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(UIDebuggerPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("UI Debugger")
		SLT_EDITOR_PANEL_CATEGORY("UI")
		SLT_REFLECT_FIELD(_targetKind)
		SLT_REFLECT_FIELD(_targetId)
		SLT_REFLECT_FIELD(_splitRatio)
		SLT_REFLECT_FIELD(_collapsedSections)
		SLT_REFLECT_FIELD(_showAllStyles)
		SLT_REFLECT_FIELD(_textOverlays)
		SLT_REFLECT_FIELD(_atlasOpen)
		SLT_REFLECT_FIELD(_atlasPage)
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		UIDebuggerPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;
		void OnClose() override;

		void ShowTarget(const UIDebugOps::Target& target) { SetTarget(target); }
	private:
		[[nodiscard]] UIDebugOps::Target CurrentTarget() const;
		// never cache
		[[nodiscard]] UI::Context* LiveTarget() const;
		void EnsureTarget();
		void SetTarget(const UIDebugOps::Target& target);
		void OpenTargetPicker(UI::Context& context, UI::ElementHandle anchor);

		void BuildHeader(UI::Context& context);
		void RebuildRows(UI::Context& context);
		void UpdateStatus(UI::Context& context);

		void PublishSelectionFromList();
		void SelectElement(const UI::Context* target, UI::ElementHandle element);
		void PushSelectionToList();
		void RaisePanelWindow();

		void ToggleExpanded(u64 key);

		struct PickTarget {
			UIDebugOps::Target target;
			u32 claim = 0;
			UI::ElementHandle bands[UIDebugOps::HIGHLIGHT_BAND_COUNT];
		};

		[[nodiscard]] PickTarget OpenPickTarget(const UIDebugOps::Target& target, UI::Context& context);
		void ClosePickTarget(UI::Context& context, PickTarget& entry);
		static void CreateHighlightBands(UI::Context& context, UI::ElementHandle (&bands)[UIDebugOps::HIGHLIGHT_BAND_COUNT], i32 zIndex);
		static void DestroyHighlightBands(UI::Context& context, UI::ElementHandle (&bands)[UIDebugOps::HIGHLIGHT_BAND_COUNT]);
		[[nodiscard]] bool IsPicking() const;
		void ArmPick();
		void DisarmPick();
		void ReleasePickTargets();
		void SyncPickTargets();
		void UpdatePick();
		static void DrawHighlight(UI::Context& context, UI::ElementHandle (&bands)[UIDebugOps::HIGHLIGHT_BAND_COUNT],
								  UI::ElementHandle element);

		void SyncSelectionHighlight();
		void DestroySelectionHighlight();
		void CommitPick(const UIDebugOps::Target& target, UI::Context& context, UI::ElementHandle element);

		void SyncTextOverlay();
		void DestroyTextOverlay();
		void DrawTextOverlay(UI::Context& target, UI::PaintScope& paint);
		void OpenTextOverlayMenu(UI::Context& context, UI::ElementHandle anchor);

		void BuildAtlasView(UI::Context& context);
		void OpenAtlasPageMenu(UI::Context& context, UI::ElementHandle anchor);

		enum class Section : u8 {
			Layout = 0,
			Stylesheets,
			MatchingSelectors,
			Attributes,
			Styles,
			Count
		};

		struct InspectorKey {
			UI::ElementHandle element;
			bool resolved = false;
			UIDebugOps::Geometry geometry;
			UI::ElementFlags flags = UI::ElementFlags::None;
			StringHash id;
			StringHash typeTag;
			StringHash classes[UI::MAX_ELEMENT_CLASSES];
			u8 classCount = 0;
			Color tint = Color::White();
			u64 userTag = 0;
			Vec2 scrollOffset;
			Vec2 maxScroll;
			i32 zIndex = 0;
			SystemCursor cursor = SystemCursor::Arrow;
			bool pointerEvents = true;
			u32 collapsedSections = 0;
			bool showAllStyles = false;
			bool atlasOpen = false;
			u8 atlasPage = 0;
			u32 atlasEntries = 0;
			bool operator==(const InspectorKey&) const = default;
		};

		[[nodiscard]] InspectorKey ReadInspectorKey() const;
		void BuildInspector(UI::Context& context);

		void CommitProperty(StringHash name, std::string_view text);
		void RevertOneProperty(StringHash name);
		void BuildAttributeRow(UI::Context& context, UI::ElementHandle parent, const UIDebugOps::AttributeRow& row,
							   UI::ElementHandle element);
		void BuildScrollOffsetRow(UI::Context& context, UI::ElementHandle row, UI::ElementHandle element);
		void AfterEdit(StringHash row);
		UI::ElementHandle BuildSection(UI::Context& context, Section section, std::string_view title);
		void ToggleSection(Section section);
		[[nodiscard]] bool IsSectionCollapsed(Section section) const;
		void FeedProjectNames();

		u8 _targetKind = 0;
		u64 _targetId = 0;

		str _linkStatus;

		u64 _namedTargetId = 0;
		bool _targetWasAlive = true;

		UIDebugOps::NameDictionary _names;
		UIDebugOps::Snapshot _snapshot;
		UIDebugOps::Snapshot _pending;
		Vector<UIDebugOps::TreeRow> _view{GetMemoryResource(MemTag::Editor)};
		Vector<ListRow> _rows{GetMemoryResource(MemTag::Editor)};

		Vector<u64> _collapsed{GetMemoryResource(MemTag::Editor)};

		Vector<u32> _selectedPath{GetMemoryResource(MemTag::Editor)};
		UIDebugOps::ElementIdentity _selectedIdentity;
		bool _hasSelection = false;
		bool _selectionResolved = false;
		bool _selectionDrawn = false;
		bool _revealSelection = false;

		u32 _lastRevision = 0;
		bool _forceRebuild = false;

		Vector<PickTarget> _pickTargets{GetMemoryResource(MemTag::Editor)};
		bool _pickPressed = false;
		bool _pickSpent = false;
		UI::ElementHandle _pickButton;
		bool _headerDirty = false;

		u8 _textOverlays = 0;
		UI::ElementHandle _overlayHost;
		UI::ElementHandle _selectionBands[UIDebugOps::HIGHLIGHT_BAND_COUNT];

		Vector<UIDebugOps::TextBox> _textBoxes{GetMemoryResource(MemTag::Editor)};
		UIDebugOps::TextOverlayStats _overlayStats;

		bool _atlasOpen = false;
		u8 _atlasPage = 0;
		Vector<UIDebugOps::AtlasPageRow> _atlasPages{GetMemoryResource(MemTag::Editor)};

		f32 _splitRatio = 0.5f;
		u32 _collapsedSections = 0;
		bool _showAllStyles = false;
		InspectorKey _inspectorKey;
		bool _inspectorBuilt = false;
		UI::ElementHandle _inspectorHost;
		UIDebugOps::StyleEdits _edits;
		StringHash _focusRow;
		bool _editGesture = false;
		Vector<UIDebugOps::AttributeRow> _attributes{GetMemoryResource(MemTag::Editor)};
		Vector<UIDebugOps::PropertyRow> _properties{GetMemoryResource(MemTag::Editor)};
		Vector<UIDebugOps::Match> _matches{GetMemoryResource(MemTag::Editor)};
		Vector<UIDebugOps::SheetRow> _sheets{GetMemoryResource(MemTag::Editor)};

		bool _searchOpen = false;
		bool _focusSearch = false;
		str _filter;

		UI::ElementHandle _headerHost;
		UI::ElementHandle _targetButton;
		UI::ElementHandle _overlayButton;
		UI::ElementHandle _atlasButton;
		UI::ElementHandle _statusLabel;
		EditorListView _list;
	};

} // namespace Shalltear
