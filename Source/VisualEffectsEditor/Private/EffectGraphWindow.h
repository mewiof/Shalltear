#pragma once

#include "GraphCompiler.h"
#include "GraphDefinitions.h"
#include "GraphWindowParts.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/GraphBlackboard.h"
#include "Shalltear/Editor/GraphCanvas.h"

#include <functional>
#include <string_view>

namespace Shalltear {

	struct GraphDocument;
	class VisualEffect;

	class EffectGraphWindow : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(EffectGraphWindow)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Effect Graph")
		SLT_EDITOR_PANEL_CATEGORY("Visual Effects")
		SLT_EDITOR_PANEL_FLOATING_SIZE(1280.0f, 800.0f)
		SLT_REFLECT_FIELD(graph)
		SLT_REFLECT_FIELD(autoCompile)
		SLT_REFLECT_FIELD(attachLocked)
		SLT_REFLECT_FIELD(showControls)
		SLT_END_REFLECT()

		EffectGraphWindow();
		~EffectGraphWindow() override;

		Guid graph{};
		bool autoCompile = true;
		bool attachLocked = false;
		bool showControls = true;

		VisualEffect* GetAttached() const;
		bool AttachToSelection();
		void Detach();

		void AppendRecordedBounds(Vector<DebugLine>& out) const;

		void AppendShape(Vector<DebugLine>& lines, Vector<SceneHandle>& handles);
		bool WriteShape(const SceneHandle& dragged);

		bool OpenGraph(const Guid& asset);
		const Guid& GetGraph() const { return _open; }
		static bool HoldsGraph(const Guid& asset);
		GraphDocument* GetDocument() const;

		str GetTitleBase() const override;
		void OnOpen() override;
		void OnClose() override;
		void OnTick(f32 deltaSeconds) override;
		bool OnCommand(PanelCommand command) override;
		bool HasUnsavedChanges() const override;
		str GetSaveChangesMessage() const override;
		void SaveChanges() override;
		void DiscardChanges() override;

		bool Save();
		bool Compile();

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
		bool ReadChanged() const;
		GraphWindowParts::Editor MakeEditor();

		void BuildToolbar(UI::Context& context, UI::ElementHandle parent);
		void BuildBlackboard(UI::Context& context, UI::ElementHandle parent);
		void BuildInspector(UI::Context& context, UI::ElementHandle parent);
		void RebuildInspector(UI::Context& context);
		void BuildAssetSettings(UI::Context& context, const GraphDocument& document);
		void RebuildBlackboard();
		void RefreshStatus(UI::Context& context);

		void AttachTo(VisualEffect* effect);
		bool PlaysThisGraph(const VisualEffect& effect) const;
		VisualEffect* SelectedEffect() const;
		void UpdateAttach();
		void RebuildBoard(UI::Context& context);
		void RefreshBoard(UI::Context& context);
		u64 BoardKey() const;

		struct RecordedSystem {
			u32 initialize = 0;
			bool world = false;
			str name;
			AABB box = AABB::Empty();
		};
		void SetRecordingBounds(bool on);
		void RecordBounds();
		bool ApplyRecordedBounds();
		void ReadRecordedSystems();
		bool CanRecordBounds() const;
		void BuildRecordingRows(UI::Context& context, const std::function<UI::ElementHandle()>& row);

		void CopySelection(bool cut);
		void PasteAt(Vec2 graph);
		void DuplicateSelection();
		void PasteBlocks(std::string_view label, const str& clip);
		void DeleteSelection();
		Vector<u32> SelectedDocumentIds() const;
		Vec2 PastePoint() const;

		void SelectItems(std::span<const GraphItem> items);
		void RebuildNodes(UI::Context& context);

		Guid _open{};
		bool _documentOpen = false;
		u64 _seenRevision = 0;
		u64 _seenSaveSerial = 0;
		GraphDefinitions _definitions;
		Vector<GraphDiagnostic> _diagnostics{GetMemoryResource(MemTag::Editor)};
		void RefreshWindowWarnings();
		bool _warnedDetached = false;
		Model* _model = nullptr;
		GraphCanvas _canvas;
		GraphBlackboard _blackboard;
		GraphBlackboard _outputs;
		UI::ElementHandle _outputsHost;
		UI::ElementHandle _subtitle;
		u64 MoveProperty(u32 property, u32 index, bool outputs);

		UI::ElementHandle _status;
		UI::ElementHandle _inspector;
		UI::ElementHandle _attachButton;
		UI::ElementHandle _detachButton;
		UI::ElementHandle _board;
		std::function<void(UI::Context&)> _boardRefresh;
		u64 _boardKey = ~0ull;
		Handle<VisualEffect> _attached;
		u32 _shapeSerial = 0;
		u64 _seenActive = 0;
		u32 _inspected = 0;
		u32 _inspectedProperty = 0;

		bool _recording = false;
		Vector<RecordedSystem> _recordedSystems{GetMemoryResource(MemTag::Editor)};
		u64 _recordedRevision = ~0ull;
		u32 _recordedGeneration = 0;

		bool _needsNodeRebuild = false;
		bool _needsInspectorRebuild = false;
		bool _needsBlackboardRebuild = false;
		bool _needsFrame = false;
		u32 _seenAdopted = 0;
		Vec2 _lastPasteAt{};
		u32 _pasteCount = 0;
	};

	namespace EffectGraphWindowRegistry {
		void RegisterOpener(const void* owner);
		EffectGraphWindow* Open(const Guid& asset);
	} // namespace EffectGraphWindowRegistry

} // namespace Shalltear
