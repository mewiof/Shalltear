#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/UIDocument.h"

#include "Shalltear/Editor/EditorInsertStrips.h"
#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/UIBuilderOps.h"
#include "Shalltear/Editor/UIDocumentOps.h"

#include <span>

namespace Shalltear {

	class UIBuilderPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(UIBuilderPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("UI Builder")
		SLT_EDITOR_PANEL_CATEGORY("UI")
		SLT_REFLECT_FIELD(_documentPath)
		SLT_REFLECT_FIELD(_canvasWidth)
		SLT_REFLECT_FIELD(_canvasHeight)
		SLT_REFLECT_FIELD(_zoom)
		SLT_REFLECT_FIELD(_fitCanvas)
		SLT_REFLECT_FIELD(_pan)
		SLT_REFLECT_FIELD(_textPanesOpen)
		SLT_REFLECT_FIELD(_leftColumnRatio)
		SLT_REFLECT_FIELD(_centerRatio)
		SLT_REFLECT_FIELD(_textPaneRatio)
		SLT_REFLECT_FIELD(_sheetsRatio)
		SLT_REFLECT_FIELD(_hierarchyRatio)
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		UIBuilderPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;
		void OnClose() override;

		bool OpenDocument(std::string_view virtualPath);

		UISurfaceId GetPresentedSurface() const override { return _surface; }

		bool ShowSheetRule(std::string_view sheetVirtualPath, u32 rule, std::string_view selector);
	private:
		void BuildSheetsPane(UI::Context& context, UI::ElementHandle parent);
		void BuildHierarchyPane(UI::Context& context, UI::ElementHandle parent);
		void BuildLibraryPane(UI::Context& context, UI::ElementHandle parent);
		void BuildViewportPane(UI::Context& context, UI::ElementHandle parent);
		void BuildInspectorPane(UI::Context& context, UI::ElementHandle parent);
		void BuildCanvasToolbar(UI::Context& context, UI::ElementHandle parent);
		void OpenFileMenu();
		void BuildTextPanes(UI::Context& context, UI::ElementHandle parent);
		void RebuildPanes(UI::Context& context);
		void RebuildRows(UI::Context& context);
		void RebuildSheetRows(UI::Context& context);
		void RebuildTextPanes(UI::Context& context);
		void RebuildInspector(UI::Context& context);
		void BuildElementInspector(UI::Context& context, UI::ElementHandle parent, const UIDocumentNode& node);
		void BuildSelectorInspector(UI::Context& context, UI::ElementHandle parent);
		void BuildCanvasInspector(UI::Context& context, UI::ElementHandle parent);
		void BuildPropertySection(UI::Context& context, UI::ElementHandle parent, std::string_view title,
								  std::string_view source, std::string_view region,
								  std::span<const UI::StyleDeclarationSource> declarations);

		enum class EditShape { Fields,
							   Structure };
		bool EditDocument(std::string_view label, EditShape shape,
						  const std::function<bool(UIDocumentAsset&, str&)>& mutate);
		bool EditSheet(Handle<StyleSheetAsset> sheet, std::string_view label, std::string_view source);
		void WriteDeclaration(std::string_view property, std::string_view value);
		void UnsetOnSelection(std::span<const std::string_view> runs, std::string_view label);
		void ExtractInlinedStyles();
		void AddSelector(std::string_view selector);
		void SaveDocument();
		void AddElement(const UIDocumentNode& node, u32 parent, u32 childIndex, std::string_view label);
		void AppendFromLibrary(std::string_view tag);

		void OpenNodeMenu(UI::Context& context, u32 node);
		void BuildInsertStrip(UI::Context& context, u32 parent, u32 childIndex);
		bool AcceptsDrop(const UI::DragPayload& payload, u32 parent) const;
		void DeliverDrop(const UI::DragPayload& payload, u32 parent, u32 childIndex);
		bool IsCollapsed(u32 node) const;
		void PublishHierarchySelectionFromList();
		void PushHierarchySelectionToList();
		void DeleteSelectedNodes();
		void DuplicateSelectedNodes();
		void PublishRuleSelectionFromList();
		void PushRuleSelectionToList();
		// both halves 1-based (0 is the model's reserved id)
		static u64 RuleRowId(u32 sheet, u32 rule);
		static u64 SheetRowId(u32 sheet);
		static u64 NodeRowId(u32 node) { return static_cast<u64>(node) + 1; }
		static u32 NodeOfRow(u64 id) { return static_cast<u32>(id - 1); }

		Handle<StyleSheetAsset> SelectedSheet() const;
		void RepopulatePreview();
		void SelectNode(u32 index);
		void PickAt(UI::Context& context, Vec2 contextPx);
		void RoutePointerToPreview(UI::Context& context, InputEventType type, Vec2 wheel = {});
		void SyncSurfaceSize(UI::Context& context);
		void ReleaseSurface();
		void SetStatus(UI::Context& context, std::string_view text);
		void SyncNameChip(UI::Context& context);
		f32 EffectiveZoom(const UI::Rect& viewport) const;
		UIBuilderOps::CanvasView CurrentView(const UI::Rect& viewport) const;
		UI::Rect CanvasRect(const UI::Rect& viewport) const;
		void LeaveFit(const UI::Rect& viewport);
		void ApplyView(const UIBuilderOps::CanvasView& view);

		str _documentPath;
		str _status;
		Handle<UIDocumentAsset> _document;
		UISurfaceId _surface = INVALID_UI_SURFACE;
		Vec2I _surfaceSize{};
		f32 _surfaceScale = 0.0f;
		Vector<UIInstantiateMapping> _mapping{GetMemoryResource(MemTag::Editor)};
		Vector<UIBuilderOps::Row> _rows{GetMemoryResource(MemTag::Editor)};
		Vector<UIBuilderOps::SheetRow> _sheetRows{GetMemoryResource(MemTag::Editor)};
		Vector<UIBuilderOps::CodeLine> _codeLines{GetMemoryResource(MemTag::Editor)};
		Vector<u32> _collapsed{GetMemoryResource(MemTag::Editor)};
		u32 _selected = UIBuilderOps::INVALID_NODE;
		Vector<u32> _selectedNodes{GetMemoryResource(MemTag::Editor)};
		const UIDocumentAsset* _populated = nullptr;
		u64 _populatedContent = 0;

		i32 _canvasWidth = 1920;
		i32 _canvasHeight = 1080;
		f32 _zoom = 1.0f;
		bool _fitCanvas = true;
		Vec2 _pan{};
		bool _textPanesOpen = false;
		f32 _leftColumnRatio = 0.2f;
		f32 _centerRatio = 0.75f;
		f32 _textPaneRatio = 0.5f;
		f32 _sheetsRatio = 0.334f;
		f32 _hierarchyRatio = 0.5f;
		bool _preview = false;
		bool _previewPointerInside = false;

		UIBuilderOps::Subject _subject = UIBuilderOps::Subject::None;
		u32 _selectedSheet = 0;
		u32 _selectedRule = UIBuilderOps::INVALID_NODE;
		u32 _undoDepth = 0;
		u32 _redoDepth = 0;

		UI::ElementHandle _contentRoot;
		UI::ElementHandle _viewport;
		EditorListView _hierarchyList;
		EditorListView _rulesList;
		EditorInsertStrips _gaps;
		UI::ElementHandle _inspectorParent;
		UI::ElementHandle _statusLabel;
		UI::ElementHandle _nameChip;
		UI::ElementHandle _layoutTextParent;
		UI::ElementHandle _styleTextParent;
	};

} // namespace Shalltear
