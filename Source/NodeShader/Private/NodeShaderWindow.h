#pragma once

#include "NodeShaderAPI.h"

#include "GraphDefinitions.h"
#include "GraphWindowParts.h"
#include "NodeShaderPreviews.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/GraphBlackboard.h"
#include "Shalltear/Editor/GraphCanvas.h"

#include <chrono>
#include <functional>
#include <string_view>

namespace Shalltear {

	struct GraphDocument;

	class SHALLTEAR_NODESHADER_API NodeShaderWindow : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(NodeShaderWindow)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Node Shader")
		SLT_EDITOR_PANEL_CATEGORY("Rendering")
		SLT_EDITOR_PANEL_FLOATING_SIZE(1280.0f, 760.0f)
		SLT_REFLECT_FIELD(graph)
		SLT_END_REFLECT()

		NodeShaderWindow();
		~NodeShaderWindow() override;

		Guid graph{};

		bool OpenGraph(const Guid& asset);
		const Guid& GetGraph() const { return _open; }
		GraphDocument* GetDocument() const;

		str GetTitleBase() const override;
		void OnOpen() override;
		void OnClose() override;
		void OnTick(f32 deltaSeconds) override;
		void OnSubmitView(f32 deltaSeconds) override;
		bool OnCommand(PanelCommand command) override;
		bool HasUnsavedChanges() const override;
		str GetSaveChangesMessage() const override;
		void SaveChanges() override;
		void DiscardChanges() override;

		bool Save();
		Guid SaveAs(const Path& path);
		bool ViewGeneratedShader();

		GraphViewModel& GetModel();
		GraphCanvas& GetCanvas() { return _canvas; }
		NodeShaderPreviews& GetPreviews() { return _previews; }
		u32 GetInspected() const { return _inspected; }
		GraphDefinitions& GetDefinitions() { return _definitions; }
		static GraphNodeId CanvasId(u32 node) { return static_cast<GraphNodeId>(node); }

		template <typename Op>
		bool Edit(std::string_view label, Op&& op) {
			GraphDocument* document = GetDocument();
			if (!document) {
				return false;
			}
			BeginEdit(label);
			const bool changed = op(*document);
			EndEdit();
			return changed;
		}
	protected:
		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
	private:
		class Model;
		friend class Model;

		void BeginEdit(std::string_view label);
		void EndEdit();
		void CloseGraph();
		void OnDocumentChanged();

		void BuildToolbar(UI::Context& context, UI::ElementHandle parent);
		void BuildBlackboard(UI::Context& context, UI::ElementHandle parent);
		void BuildInspector(UI::Context& context, UI::ElementHandle parent);
		void BuildMainPreview(UI::Context& context, UI::ElementHandle parent);
		void RebuildInspector(UI::Context& context);
		void RebuildBlackboard();
		void RefreshStatus(UI::Context& context);
		void BuildNodeSettings(UI::Context& context, UI::ElementHandle parent);
		void BuildGraphSettings(UI::Context& context, UI::ElementHandle parent);
		void BuildPropertySettings(UI::Context& context, UI::ElementHandle parent, u32 property);
		void BuildKeywordEntries(UI::Context& context, UI::ElementHandle parent, UI::ElementHandle row, u32 property);
		void BuildInterpolatorSettings(UI::Context& context, UI::ElementHandle parent, u32 block);
		void BuildCustomFunctionSettings(UI::Context& context, UI::ElementHandle parent, u32 node);
		GraphWindowParts::Editor MakeEditor();
		void OpenMeshMenu(UI::Context& context);
		void OpenSaveAs(UI::Context& context);
		void ApplyMeshSetting();
		void SetMeshSetting(std::string_view value);

		void CopySelection(bool cut);
		void PasteAt(Vec2 graph);
		void DuplicateSelection();
		void DeleteSelection();
		Vector<u32> SelectedDocumentIds() const;
		Vec2 PastePoint() const;

		void SelectItems(std::span<const GraphItem> items);
		void RebuildNodes(UI::Context& context);

		Guid _open{};
		bool _documentOpen = false;
		u64 _seenRevision = 0;
		GraphDefinitions _definitions;
		u64 _seenSaveSerial = 0;
		Model* _model = nullptr;
		GraphCanvas _canvas;
		GraphBlackboard _blackboard;
		GraphBlackboard _functionInputs;
		GraphBlackboard _functionOutputs;
		GraphBlackboard _keywordEntries;
		NodeShaderPreviews _previews;

		UI::ElementHandle _status;
		UI::ElementHandle _inspector;
		UI::ElementHandle _mainPane;
		UI::ElementHandle _mainImage;
		UI::ElementHandle _popup;
		bool _graphSettingsTab = false;
		u32 _inspected = 0;
		u32 _inspectedProperty = 0;

		bool _needsNodeRebuild = false;
		bool _needsInspectorRebuild = false;
		bool _needsBlackboardRebuild = false;
		bool _needsFrame = false;

		std::chrono::steady_clock::time_point _started = std::chrono::steady_clock::now();
		f64 _lastTimedSubmit = 0.0;
		std::chrono::steady_clock::time_point _compileStarted{};
		bool _wasCompiling = false;
		Vec2 _lastPasteAt{};
		u32 _pasteCount = 0;
		bool _dragging = false;
		Vec2 _dragFrom{};
	};

	namespace NodeShaderWindowRegistry {
		SHALLTEAR_NODESHADER_API void RegisterOpener(const void* owner);
		SHALLTEAR_NODESHADER_API NodeShaderWindow* Open(const Guid& asset);
	} // namespace NodeShaderWindowRegistry

} // namespace Shalltear
