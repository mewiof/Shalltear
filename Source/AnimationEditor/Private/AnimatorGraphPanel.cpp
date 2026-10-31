#include "AnimationEditor.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/AssetOpeners.h"
#include "Shalltear/Editor/EditorDocuments.h"
#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/GraphBlackboard.h"
#include "Shalltear/Editor/GraphCanvas.h"
#include "Shalltear/Editor/PanelManager.h"

#include "Shalltear/Animation/AnimatorController.h"

#include "Controller/AnimatorGraphOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <utility>

namespace Shalltear {

	namespace AnimationEditorImpl {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		constexpr f32 NODE_WIDTH = 168.0f;
		constexpr f32 NODE_HEIGHT = 34.0f;
		constexpr f32 PILL_WIDTH = 128.0f;
		constexpr f32 PILL_HEIGHT = 28.0f;

		enum class PickKind : u8 {
			None,
			State,
			Transition,
			AnyTransition,
			Machine,
			Layer,
		};

		inline GraphWireId WireIdOf(PickKind kind, i32 a, i32 b) {
			constexpr u64 MASK = (1ull << 28) - 1;
			return (static_cast<u64>(kind) << 56) | ((static_cast<u64>(static_cast<u32>(a)) & MASK) << 28) |
				   (static_cast<u64>(static_cast<u32>(b)) & MASK);
		}
		inline PickKind WireKindOf(GraphWireId id) { return static_cast<PickKind>(id >> 56); }
		inline i32 WireAOf(GraphWireId id) { return static_cast<i32>((id >> 28) & ((1ull << 28) - 1)); }
		inline i32 WireBOf(GraphWireId id) { return static_cast<i32>(id & ((1ull << 28) - 1)); }

		enum class NodeKind : u8 {
			None = 0,
			State,
			Machine,
			Entry,
			AnyState,
			Up,
		};

		inline GraphNodeId NodeIdOf(NodeKind kind, i32 index) {
			return (static_cast<u64>(kind) << 32) | static_cast<u64>(static_cast<u32>(index));
		}
		inline NodeKind KindOf(GraphNodeId id) { return static_cast<NodeKind>(id >> 32); }
		inline i32 IndexOf(GraphNodeId id) { return static_cast<i32>(static_cast<u32>(id)); }

		enum class WireKind : u8 {
			None,
			FromState,
			FromAnyState,
			FromEntry,
		};

		class AnimatorGraphPanel : public EditorPanel {
		public:
			SLT_REFLECT_CLASS(AnimatorGraphPanel)
			SLT_REFLECT_PARENT(EditorPanel)
			SLT_REFLECT_CREATABLE()
			SLT_EDITOR_PANEL_NAME("Animator")
			SLT_EDITOR_PANEL_CATEGORY("Animation")
			SLT_EDITOR_PANEL_FLOATING_SIZE(1100.0f, 640.0f)
			SLT_END_REFLECT()

			AnimatorGraphPanel() = default;

			void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
			void OnTick(f32 deltaSeconds) override;
			bool OnCommand(PanelCommand command) override;

			void OnClose() override;
		private:
			Guid _guid{};
			Handle<AnimatorController> _controller;
			bool _dirty = false;

			bool _documentOpen = false;
			u64 _seenRevision = 0;
			u32 _seenGraphRevision = 0;
			bool _scrubbing = false;

			i32 _layer = 0;

			i32 _machine = -1;

			Handle<AnimatorStateMachine> _live;
			str _liveObject;
			str _livePath;
			i32 _liveState = -1;
			f32 _liveNormalized = 0.0f;
			u32 _liveTravel = 0;
			str _liveOtherLayers;
			PickKind _activeKind = PickKind::None;
			i32 _activeA = -1;
			u32 _searchCooldown = 0;

			PickKind _pick = PickKind::None;
			i32 _pickA = -1;
			i32 _pickB = -1;
			WireKind _wire = WireKind::None;
			i32 _wireFrom = -1;

			class ControllerModel final : public GraphViewModel {
			public:
				explicit ControllerModel(AnimatorGraphPanel& panel) : _panel(panel) {}

				void GetNodes(Vector<GraphNodeDesc>& out) override;
				bool GetNodePosition(GraphNodeId node, Vec2& out) const override;
				void GetWires(Vector<GraphWireDesc>& out) const override;
				ColorToken GetNodeBorder(GraphNodeId node) const override;
				void PaintNode(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint,
							   GraphNodeId node) override;
				void OnSelectionChanged() override;
				void OnMoveBegin() override;
				void MoveNode(GraphNodeId node, Vec2 graph) override;
				void OnMoveEnd() override;
				void OnNodeClick(GraphNodeId node) override;
				void OnNodeDoubleClick(GraphNodeId node) override;
				void OnNodeContextMenu(UI::Context& context, GraphNodeId node) override;
				bool AcceptsNodeDrop(GraphNodeId node, const UI::DragPayload& payload) const override;
				void OnNodeDrop(GraphNodeId node, const UI::DragPayload& payload) override;
				void OnCanvasContextMenu(UI::Context& context, Vec2 graph) override;
				bool AcceptsDrop(const UI::DragPayload& payload) const override;
				void OnDrop(const UI::DragPayload& payload, Vec2 graph) override;
				bool GetPendingWire(GraphNodeId& from) const override;
				void CancelPendingWire() override;
				void GetSearchEntries(const GraphPortRef* from, Vector<GraphSearchEntry>& out) override;
				void OnSearchPick(const GraphSearchEntry& entry, Vec2 graph, const GraphPortRef* from) override;
			private:
				GraphNodeId ArrivalFor(i32 destination) const;

				AnimatorGraphPanel& _panel;
			};

			ControllerModel _model{*this};
			GraphCanvas _canvas;

			UI::ElementHandle _title;
			UI::ElementHandle _properties;
			UI::ElementHandle _parameters;
			UI::ElementHandle _liveParameters;
			GraphBlackboard _blackboard;
			UI::ElementHandle _layerRow;
			UI::ElementHandle _crumbRow;
			UI::ElementHandle _readout;
			Vector<UI::ElementHandle> _parameterValues{GetMemoryResource(MemTag::Editor)};
			bool _needsNodeRebuild = false;
			bool _needsPropertyRebuild = false;
			bool _needsParameterRebuild = false;
			bool _needsToolbarRebuild = false;
			bool _needsFrame = false;
			bool _needsPlace = false;

			AnimatorController* Graph() const { return _controller.Get(); }
			i32 DrawnMachine() const;
			void EnterMachine(i32 machine);
			void FollowSelection();
			void Save();
			void MarkEdited(bool structural);

			void OpenDocument();
			void CloseDocument();
			void OnDocumentRestored();
			void BeginEdit(std::string_view label);
			void EndEdit();
			void BeginScrub(std::string_view label);
			void EndScrub();
			void ScrubAsOneEdit(UI::Widgets::NumberFieldDesc& desc, std::string_view label);

			template <typename Op>
			bool Edit(std::string_view label, Op&& op) {
				BeginEdit(label);
				const bool changed = op();
				EndEdit();
				return changed;
			}

			void SyncPick();

			bool IsActive(PickKind kind, i32 a) const { return _activeKind != PickKind::None && _activeKind == kind && _activeA == a; }

			void BuildToolbar(UI::Context& context, UI::ElementHandle parent);
			void BuildSide(UI::Context& context, UI::ElementHandle parent);
			void RebuildNodes(UI::Context& context);
			void RebuildProperties(UI::Context& context);
			void RebuildParameters(UI::Context& context);
			void BuildParameterBoard(UI::Context& context);
			static u64 ParameterId(std::string_view name);
			i32 ParameterSlot(u64 id) const;
			void RefreshTitle(UI::Context& context);
			void RebuildCrumbs(UI::Context& context);

			bool RefreshDebug();
			void RefreshReadout(UI::Context& context);
			void RefreshParameterValues(UI::Context& context);

			void CanvasMenu(UI::Context& context, Vec2 graph);

			void CreateState(Vec2 centre);
			void CreateMachine(Vec2 centre);
			void CreateStateFromClip(const Guid& clip, Vec2 at);
			Vector<Guid> _searchClips{GetMemoryResource(MemTag::Editor)};
			void NodeMenu(UI::Context& context, PickKind kind, i32 a);
			void CompleteWire(i32 targetState);
			void DeleteSelection();
			void Select(PickKind kind, i32 a, i32 b);

			i32 ProxyMachineFor(i32 state) const;
		};

		namespace {

			UI::ElementHandle AddText(UI::Context& context, UI::ElementHandle parent, ColorToken color,
									  MetricToken size, std::string_view text) {
				UI::ElementHandle line = context.CreateElement();
				context.AppendChild(parent, line);
				context.SetText(line, UISystem::GetDefaultFont(), EditorTheme::GetMetric(size),
								EditorTheme::GetColor(color), text);
				return line;
			}

			UI::ElementHandle AddRow(UI::Context& context, UI::ElementHandle parent) {
				UI::ElementHandle row = context.CreateElement();
				context.AppendChild(parent, row);
				UI::Style style;
				style.flexDirection = UI::FlexDirection::Row;
				style.alignItems = UI::Align::Center;
				style.width = UI::Length::Percent(1.0f);
				style.margin = {0.0f, 0.0f, 0.0f, 3.0f};
				context.SetStyle(row, style);
				return row;
			}

			UI::ElementHandle AddLabeledRow(UI::Context& context, UI::ElementHandle parent, std::string_view label) {
				UI::ElementHandle row = AddRow(context, parent);
				UI::ElementHandle text = AddText(context, row, ColorToken::WidgetTextDim, MetricToken::FontSmall, label);
				UI::Style style;
				style.width = UI::Length::Pixel(78.0f);
				style.flexShrink = 0.0f;
				context.SetStyle(text, style);
				return row;
			}

			str MotionLabel(const AnimatorController& controller, i32 node) {
				if (node < 0 || static_cast<size_t>(node) >= controller.nodes.size()) {
					return str("None");
				}
				const AnimatorController::MotionNode& entry = controller.nodes[static_cast<size_t>(node)];
				if (entry.kind != AnimatorBlendKind::Clip) {
					return str("Blend Tree");
				}
				const AnimationClip* clip = entry.clip.Get();
				if (!clip) {
					return str("<missing clip>");
				}
				std::string_view path = clip->virtualPath;
				const size_t slash = path.find_last_of('/');
				return str(slash == std::string_view::npos ? path : path.substr(slash + 1));
			}

			const char* ParameterKindName(AnimatorParameterKind kind) {
				switch (kind) {
				case AnimatorParameterKind::Float:
					return "Float";
				case AnimatorParameterKind::Int:
					return "Int";
				case AnimatorParameterKind::Bool:
					return "Bool";
				case AnimatorParameterKind::Trigger:
					return "Trigger";
				}
				return "?";
			}

			constexpr std::string_view ParameterKinds[] = {"Float", "Int", "Bool", "Trigger"};
			constexpr std::string_view ConditionModes[] = {"Greater", "Less", "Equals", "NotEqual", "If", "IfNot"};

			constexpr std::string_view LayerBlendModes[] = {"Override", "Additive"};

			f32 ProgressOf(f32 normalized) {
				if (!(normalized > 0.0f)) {
					return 0.0f;
				}
				const f32 fraction = normalized - std::floor(normalized);
				return fraction > 0.0f ? Math::Clamp(fraction, 0.0f, 1.0f) : 1.0f;
			}

		} // anonymous namespace

		i32 AnimatorGraphPanel::DrawnMachine() const {
			const AnimatorController* controller = Graph();
			if (!controller || controller->layers.empty()) {
				return -1;
			}
			const size_t layer = static_cast<size_t>(Math::Clamp(_layer, 0, static_cast<i32>(controller->layers.size()) - 1));
			const i32 root = controller->layers[layer].machine;

			if (_machine >= 0 && _machine != root && AnimatorGraphOps::MachineContains(*controller, root, _machine)) {
				return _machine;
			}
			return root;
		}

		void AnimatorGraphPanel::EnterMachine(i32 machine) {
			if (machine == DrawnMachine()) {
				return;
			}
			_machine = machine;
			_wire = WireKind::None;
			_wireFrom = -1;
			Select(PickKind::None, -1, -1);

			AnimatorController* controller = Graph();
			const i32 drawn = DrawnMachine();
			if (controller && drawn >= 0 && AnimatorGraphOps::NeedsAutoLayout(*controller, drawn)) {
				AnimatorGraphOps::AutoLayout(*controller, drawn);
				_dirty = true;
			}
			_needsNodeRebuild = true;
			_needsToolbarRebuild = true;
			_needsFrame = true;
		}

		void AnimatorGraphPanel::FollowSelection() {
			const Guid& selected = EditorSelection::GetSelectedAsset();
			if (!selected.IsValid() || selected == _guid) {
				return;
			}
			Asset* resolved = AssetDatabase::LoadAssetByGuid(selected);
			if (!resolved || resolved->GetAssetTypeId() != AnimatorController::GetReflectType()->name) {
				return;
			}
			CloseDocument();
			if (AnimatorController* previous = _controller.Get()) {
				AssetDatabase::UnloadAsset(previous);
			}
			_guid = selected;
			_controller = Handle<AnimatorController>(static_cast<AnimatorController*>(resolved));
			_dirty = false;
			_layer = 0;
			_machine = -1;
			_canvas.ResetView();
			Select(PickKind::None, -1, -1);
			_wire = WireKind::None;

			AnimatorController* controller = _controller.Get();
			const i32 machine = DrawnMachine();
			if (controller && machine >= 0 && AnimatorGraphOps::NeedsAutoLayout(*controller, machine)) {
				AnimatorGraphOps::AutoLayout(*controller, machine);
				_dirty = true;
			}
			OpenDocument();
			_needsNodeRebuild = true;
			_needsParameterRebuild = true;
			_needsToolbarRebuild = true;
			_needsFrame = true;
		}

		void AnimatorGraphPanel::OnClose() {
			CloseDocument();
			if (AnimatorController* held = Graph()) {
				AssetDatabase::UnloadAsset(held);
			}
			_controller = {};
			_guid = {};
		}

		void AnimatorGraphPanel::OpenDocument() {
			AnimatorController* controller = Graph();
			if (_documentOpen || !controller || !_guid.IsValid()) {
				return;
			}
			const Handle<AnimatorController> target = _controller;
			EditorDocumentDesc desc;
			desc.asset = _guid;
			desc.owner = GetType() ? GetType()->ownerModule : nullptr;
			desc.capture = [target](str& out) {
				if (const AnimatorController* held = target.Get()) {
					out = AnimatorGraphOps::CaptureSnapshot(*held);
				}
			};
			desc.restore = [target](std::string_view bytes) {
				AnimatorController* held = target.Get();
				return held && AnimatorGraphOps::RestoreSnapshot(*held, bytes);
			};
			_documentOpen = EditorDocuments::Open(std::move(desc));
			_seenRevision = EditorDocuments::GetRevision(_guid);
			_seenGraphRevision = controller->graphRevision;
		}

		void AnimatorGraphPanel::CloseDocument() {
			EndScrub();
			if (_documentOpen) {
				EditorDocuments::Close(_guid);
				_documentOpen = false;
			}
		}

		void AnimatorGraphPanel::BeginEdit(std::string_view label) {
			if (_documentOpen) {
				EditorDocuments::BeginEdit(_guid, label);
			}
		}

		void AnimatorGraphPanel::EndEdit() {
			if (!_documentOpen) {
				return;
			}
			EditorDocuments::EndEdit(_guid);
			_seenRevision = EditorDocuments::GetRevision(_guid);
			if (const AnimatorController* controller = Graph()) {
				_seenGraphRevision = controller->graphRevision;
			}
		}

		void AnimatorGraphPanel::BeginScrub(std::string_view label) {
			EndScrub();
			_scrubbing = true;
			BeginEdit(label);
		}

		void AnimatorGraphPanel::EndScrub() {
			if (_scrubbing) {
				_scrubbing = false;
				EndEdit();
			}
		}

		void AnimatorGraphPanel::ScrubAsOneEdit(UI::Widgets::NumberFieldDesc& desc, std::string_view label) {
			desc.onScrubBegin = [this, name = str(label)]() { BeginScrub(name); };
			desc.onScrubEnd = [this]() { EndScrub(); };
		}

		void AnimatorGraphPanel::OnDocumentRestored() {
			_seenRevision = EditorDocuments::GetRevision(_guid);
			const AnimatorController* controller = Graph();
			const bool reshaped = !controller || controller->graphRevision != _seenGraphRevision;
			if (controller) {
				_seenGraphRevision = controller->graphRevision;
			}
			_dirty = true;
			if (reshaped) {
				_wire = WireKind::None;
				_wireFrom = -1;
				Select(PickKind::None, -1, -1);
			}
			_needsNodeRebuild = true;
			_needsPropertyRebuild = true;
			_needsParameterRebuild = true;
			_needsToolbarRebuild = true;
		}

		void AnimatorGraphPanel::Save() {
			AnimatorController* controller = Graph();
			if (!controller || !_guid.IsValid()) {
				return;
			}
			Path path = AssetDatabase::LookupPath(_guid);
			if (path.ToString().empty()) {
				SLT_LOG_WARN("Animation", "AnimatorGraphPanel: the controller has no path. Not saved");
				return;
			}
			if (controller->Export(FileSystem::Resolve(path))) {
				_dirty = false;
				SLT_LOG_INFO("Animation", "AnimatorGraphPanel: saved '{}'", path.ToString());
			} else {
				SLT_LOG_WARN("Animation", "AnimatorGraphPanel: failed to write '{}'", path.ToString());
			}
		}

		void AnimatorGraphPanel::MarkEdited(bool structural) {
			_dirty = true;
			if (structural) {
				_needsNodeRebuild = true;
			}
		}

		void AnimatorGraphPanel::Select(PickKind kind, i32 a, i32 b) {
			_pick = kind;
			_pickA = a;
			_pickB = b;
			_needsPropertyRebuild = true;
			GraphItem item{};
			bool drawn = true;
			switch (kind) {
			case PickKind::State:
				item = GraphItem{GraphItemKind::Node, NodeIdOf(NodeKind::State, a)};
				break;
			case PickKind::Machine:
				item = GraphItem{GraphItemKind::Node, NodeIdOf(NodeKind::Machine, a)};
				break;
			case PickKind::Transition:
			case PickKind::AnyTransition:
				item = GraphItem{GraphItemKind::Wire, WireIdOf(kind, a, b)};
				break;
			case PickKind::None:
			case PickKind::Layer:
				drawn = false;
				break;
			}
			_canvas.SetSelection(drawn ? std::span<const GraphItem>(&item, 1) : std::span<const GraphItem>());
		}

		void AnimatorGraphPanel::SyncPick() {
			const std::span<const GraphItem> picked = _canvas.GetSelection();
			PickKind kind = PickKind::None;
			i32 a = -1;
			i32 b = -1;
			if (!picked.empty()) {
				const GraphItem& active = picked.back();
				if (active.kind == GraphItemKind::Wire) {
					kind = WireKindOf(active.id);
					a = WireAOf(active.id);
					b = WireBOf(active.id);
				} else if (KindOf(active.id) == NodeKind::State) {
					kind = PickKind::State;
					a = IndexOf(active.id);
				} else if (KindOf(active.id) == NodeKind::Machine) {
					kind = PickKind::Machine;
					a = IndexOf(active.id);
				}
			}
			if (kind == _pick && a == _pickA && b == _pickB) {
				return;
			}
			_pick = kind;
			_pickA = a;
			_pickB = b;
			_needsPropertyRebuild = true;
		}

		void AnimatorGraphPanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.width = UI::Length::Percent(1.0f);
			rootStyle.height = UI::Length::Percent(1.0f);
			context.SetStyle(root, rootStyle);

			BuildToolbar(context, root);

			UI::ElementHandle body = context.CreateElement();
			context.AppendChild(root, body);
			UI::Style bodyStyle;
			bodyStyle.flexDirection = UI::FlexDirection::Row;
			bodyStyle.width = UI::Length::Percent(1.0f);
			bodyStyle.flexGrow = 1.0f;
			bodyStyle.minHeight = UI::Length::Pixel(0.0f);
			context.SetStyle(body, bodyStyle);

			_canvas.Build(context, body, &_model);
			BuildSide(context, body);

			RebuildNodes(context);
			RebuildProperties(context);
			RebuildParameters(context);
			RefreshTitle(context);
		}

		void AnimatorGraphPanel::BuildToolbar(UI::Context& context, UI::ElementHandle parent) {
			UI::ElementHandle bar = context.CreateElement();
			context.AppendChild(parent, bar);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.alignItems = UI::Align::Center;
			style.width = UI::Length::Percent(1.0f);
			style.flexShrink = 0.0f;
			style.padding = {6.0f, 4.0f, 6.0f, 4.0f};
			style.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::HeaderFill));
			context.SetStyle(bar, style);

			_title = AddText(context, bar, ColorToken::TextStrong, MetricToken::FontBody, "no controller selected");

			_crumbRow = context.CreateElement();
			context.AppendChild(bar, _crumbRow);
			UI::Style crumbStyle;
			crumbStyle.flexDirection = UI::FlexDirection::Row;
			crumbStyle.alignItems = UI::Align::Center;
			crumbStyle.margin = {10.0f, 0.0f, 0.0f, 0.0f};
			context.SetStyle(_crumbRow, crumbStyle);

			_layerRow = context.CreateElement();
			context.AppendChild(bar, _layerRow);
			UI::Style layerStyle;
			layerStyle.flexDirection = UI::FlexDirection::Row;
			layerStyle.alignItems = UI::Align::Center;
			layerStyle.margin = {12.0f, 0.0f, 0.0f, 0.0f};
			context.SetStyle(_layerRow, layerStyle);

			UI::Widgets::Spacer(context, bar);

			UI::Widgets::ButtonDesc frame;
			frame.label = "Frame";
			frame.font = UISystem::GetDefaultFont();
			frame.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			frame.onClick = [this]() { _needsFrame = true; };
			UI::Widgets::Button(context, bar, frame);

			UI::Widgets::ButtonDesc save;
			save.label = "Save";
			save.font = UISystem::GetDefaultFont();
			save.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			save.onClick = [this]() {
				Save();
				if (UI::Context* c = GetPanelContext()) {
					RefreshTitle(*c);
				}
			};
			UI::Widgets::Button(context, bar, save);

			_readout = context.CreateElement();
			context.AppendChild(parent, _readout);
			UI::Style readoutStyle;
			readoutStyle.display = UI::Display::None;
			readoutStyle.width = UI::Length::Percent(1.0f);
			readoutStyle.flexShrink = 0.0f;
			readoutStyle.padding = {8.0f, 3.0f, 8.0f, 3.0f};
			readoutStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
			context.SetStyle(_readout, readoutStyle);
		}

		void AnimatorGraphPanel::RebuildCrumbs(UI::Context& context) {
			if (!context.IsAlive(_crumbRow)) {
				return;
			}
			context.ClearChildren(_crumbRow);
			const AnimatorController* controller = Graph();
			const i32 drawn = DrawnMachine();
			if (!controller || drawn < 0) {
				return;
			}
			constexpr u32 MAX_DEPTH = 32;
			i32 chain[MAX_DEPTH];
			const u32 depth = AnimatorGraphOps::MachineAncestry(*controller, drawn, chain, MAX_DEPTH);
			if (depth < 2) {
				return;
			}
			for (u32 index = 0; index < depth; ++index) {
				const AnimatorController::StateMachine& machine = controller->machines[static_cast<size_t>(chain[index])];
				if (index + 1 == depth) {
					AddText(context, _crumbRow, ColorToken::TextStrong, MetricToken::FontSmall, machine.name);
					break;
				}
				UI::Widgets::ButtonDesc crumb;
				crumb.label = machine.name;
				crumb.font = UISystem::GetDefaultFont();
				crumb.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				const i32 target = chain[index];
				crumb.onClick = [this, target]() { EnterMachine(target); };
				UI::Widgets::Button(context, _crumbRow, crumb);
				AddText(context, _crumbRow, ColorToken::WidgetTextDim, MetricToken::FontSmall, " > ");
			}
		}

		void AnimatorGraphPanel::BuildSide(UI::Context& context, UI::ElementHandle parent) {
			UI::ElementHandle side = context.CreateElement();
			context.AppendChild(parent, side);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Column;
			style.width = UI::Length::Pixel(268.0f);
			style.flexShrink = 0.0f;
			style.height = UI::Length::Percent(1.0f);
			style.overflow = UI::Overflow::Scroll;
			style.padding = {8.0f, 8.0f, 8.0f, 8.0f};
			style.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
			context.SetStyle(side, style);

			_properties = context.CreateElement();
			context.AppendChild(side, _properties);
			UI::Style column;
			column.flexDirection = UI::FlexDirection::Column;
			column.width = UI::Length::Percent(1.0f);
			context.SetStyle(_properties, column);

			_parameters = context.CreateElement();
			context.AppendChild(side, _parameters);
			UI::Style parameterStyle = column;
			parameterStyle.margin = {0.0f, 14.0f, 0.0f, 0.0f};
			context.SetStyle(_parameters, parameterStyle);

			_liveParameters = context.CreateElement();
			context.AppendChild(side, _liveParameters);
			context.SetStyle(_liveParameters, parameterStyle);

			BuildParameterBoard(context);
		}

		i32 AnimatorGraphPanel::ProxyMachineFor(i32 state) const {
			const AnimatorController* controller = Graph();
			const i32 drawn = DrawnMachine();
			if (!controller || drawn < 0 || state < 0 || static_cast<size_t>(state) >= controller->states.size()) {
				return -1;
			}
			i32 machine = controller->states[static_cast<size_t>(state)].machine;
			if (machine == drawn) {
				return -1;
			}
			for (u32 guard = 0; guard < static_cast<u32>(controller->machines.size()) + 1; ++guard) {
				if (machine < 0 || static_cast<size_t>(machine) >= controller->machines.size()) {
					return -1;
				}
				const i32 parent = controller->machines[static_cast<size_t>(machine)].parent;
				if (parent == drawn) {
					return machine;
				}
				machine = parent;
			}
			return -1;
		}

		void AnimatorGraphPanel::RebuildNodes(UI::Context& context) {
			_needsNodeRebuild = false;
			_canvas.RebuildNodes(context);
			if (_pick != PickKind::Layer) {
				SyncPick();
			}
		}

		void AnimatorGraphPanel::ControllerModel::GetNodes(Vector<GraphNodeDesc>& out) {
			AnimatorController* controller = _panel.Graph();
			const i32 machine = _panel.DrawnMachine();
			if (!controller || machine < 0) {
				return;
			}
			const AnimatorController::StateMachine& drawn = controller->machines[static_cast<size_t>(machine)];
			auto add = [&](NodeKind kind, i32 index, Vec2 size, std::string_view label, ColorToken fill, f32 radius) {
				GraphNodeDesc node;
				node.id = NodeIdOf(kind, index);
				node.size = size;
				node.label = str(label);
				node.fill = fill;
				node.radius = radius;
				out.push_back(std::move(node));
			};

			const f32 pillRadius = EditorTheme::GetMetric(MetricToken::RadiusPill);
			add(NodeKind::Entry, 0, Vec2(PILL_WIDTH, PILL_HEIGHT), "Entry", ColorToken::GraphNodeMuted, pillRadius);
			add(NodeKind::AnyState, 0, Vec2(PILL_WIDTH, PILL_HEIGHT), "Any State", ColorToken::GraphNodeMuted, pillRadius);

			if (drawn.parent >= 0 && static_cast<size_t>(drawn.parent) < controller->machines.size()) {
				const str upLabel = str("(Up) ") + controller->machines[static_cast<size_t>(drawn.parent)].name;
				add(NodeKind::Up, 0, Vec2(PILL_WIDTH, PILL_HEIGHT), upLabel, ColorToken::GraphNodeMuted, pillRadius);
			}

			for (size_t index = 0; index < controller->machines.size(); ++index) {
				if (controller->machines[index].parent == machine) {
					add(NodeKind::Machine, static_cast<i32>(index), Vec2(NODE_WIDTH, NODE_HEIGHT),
						controller->machines[index].name, ColorToken::Selected, NODE_HEIGHT * 0.5f);
				}
			}

			const f32 cardRadius = EditorTheme::GetMetric(MetricToken::Radius);
			for (size_t index = 0; index < controller->states.size(); ++index) {
				if (controller->states[index].machine == machine) {
					const bool isDefault = drawn.defaultState == static_cast<i32>(index);
					add(NodeKind::State, static_cast<i32>(index), Vec2(NODE_WIDTH, NODE_HEIGHT), controller->states[index].name,
						isDefault ? ColorToken::Accent : ColorToken::GraphNode, cardRadius);
				}
			}
		}

		bool AnimatorGraphPanel::ControllerModel::GetNodePosition(GraphNodeId node, Vec2& out) const {
			const AnimatorController* controller = _panel.Graph();
			const i32 machine = _panel.DrawnMachine();
			if (!controller || machine < 0) {
				return false;
			}
			const AnimatorController::StateMachine& drawn = controller->machines[static_cast<size_t>(machine)];
			const i32 index = IndexOf(node);
			switch (KindOf(node)) {
			case NodeKind::Entry:
				out = drawn.entryPosition;
				return true;
			case NodeKind::AnyState:
				out = drawn.anyStatePosition;
				return true;
			case NodeKind::Up:
				out = drawn.upPosition;
				return true;
			case NodeKind::State:
				if (index < 0 || static_cast<size_t>(index) >= controller->states.size()) {
					return false;
				}
				out = controller->states[static_cast<size_t>(index)].position;
				return true;
			case NodeKind::Machine:
				if (index < 0 || static_cast<size_t>(index) >= controller->machines.size()) {
					return false;
				}
				out = controller->machines[static_cast<size_t>(index)].position;
				return true;
			case NodeKind::None:
				break;
			}
			return false;
		}

		ColorToken AnimatorGraphPanel::ControllerModel::GetNodeBorder(GraphNodeId node) const {
			const NodeKind kind = KindOf(node);
			const PickKind pick = kind == NodeKind::State ? PickKind::State : kind == NodeKind::Machine ? PickKind::Machine
																										: PickKind::None;
			return _panel.IsActive(pick, IndexOf(node)) ? ColorToken::GraphNodeActive : ColorToken::CardEdge;
		}

		void AnimatorGraphPanel::ControllerModel::PaintNode(UI::Context& context, UI::ElementHandle element,
															UI::PaintScope& paint, GraphNodeId node) {
			const NodeKind kind = KindOf(node);
			const PickKind pick = kind == NodeKind::State ? PickKind::State : kind == NodeKind::Machine ? PickKind::Machine
																										: PickKind::None;
			if (!_panel.IsActive(pick, IndexOf(node))) {
				return;
			}
			const UI::Rect box = context.GetLayout(element).content;
			if (box.w <= 4.0f || box.h <= 4.0f) {
				return;
			}
			const f32 progress = ProgressOf(_panel._liveNormalized);
			const f32 scale = context.GetContentScale();
			const f32 inset = 3.0f * scale;
			const f32 thickness = Math::Max(2.0f, 4.0f * scale);
			const f32 span = Math::Max(0.0f, box.w - inset * 2.0f);
			if (!(span > 0.0f)) {
				return;
			}
			const f32 radius = thickness * 0.5f;
			paint.EmitQuad({box.x + inset, box.y + box.h - inset - thickness, span * progress, thickness},
						   EditorTheme::GetColor(ColorToken::GraphNodeProgress), UI::Radii{radius, radius, radius, radius});
		}

		void AnimatorGraphPanel::ControllerModel::OnSelectionChanged() {
			_panel.SyncPick();
		}

		void AnimatorGraphPanel::ControllerModel::OnMoveBegin() {
			_panel.BeginEdit("Move");
		}

		void AnimatorGraphPanel::ControllerModel::OnMoveEnd() {
			_panel.EndEdit();
		}

		void AnimatorGraphPanel::ControllerModel::MoveNode(GraphNodeId node, Vec2 graph) {
			AnimatorController* controller = _panel.Graph();
			const i32 machine = _panel.DrawnMachine();
			if (!controller || machine < 0) {
				return;
			}
			AnimatorController::StateMachine& drawn = controller->machines[static_cast<size_t>(machine)];
			const i32 index = IndexOf(node);
			switch (KindOf(node)) {
			case NodeKind::Entry:
				drawn.entryPosition = graph;
				break;
			case NodeKind::AnyState:
				drawn.anyStatePosition = graph;
				break;
			case NodeKind::Up:
				drawn.upPosition = graph;
				break;
			case NodeKind::State:
				if (index < 0 || static_cast<size_t>(index) >= controller->states.size()) {
					return;
				}
				controller->states[static_cast<size_t>(index)].position = graph;
				break;
			case NodeKind::Machine:
				if (index < 0 || static_cast<size_t>(index) >= controller->machines.size()) {
					return;
				}
				controller->machines[static_cast<size_t>(index)].position = graph;
				break;
			case NodeKind::None:
				return;
			}
			_panel.MarkEdited(false);
		}

		void AnimatorGraphPanel::ControllerModel::OnNodeClick(GraphNodeId node) {
			const i32 index = IndexOf(node);
			switch (KindOf(node)) {
			case NodeKind::Entry:
				_panel._wire = WireKind::FromEntry;
				_panel._wireFrom = _panel.DrawnMachine();
				break;
			case NodeKind::AnyState:
				_panel._wire = WireKind::FromAnyState;
				_panel._wireFrom = _panel.DrawnMachine();
				break;
			case NodeKind::Up: {
				if (_panel._wire != WireKind::None) {
					CancelPendingWire();
					break;
				}
				const AnimatorController* controller = _panel.Graph();
				const i32 machine = _panel.DrawnMachine();
				if (controller && machine >= 0) {
					_panel.EnterMachine(controller->machines[static_cast<size_t>(machine)].parent);
				}
				break;
			}
			case NodeKind::Machine:
				if (_panel._wire != WireKind::None) {
					CancelPendingWire();
				}
				break;
			case NodeKind::State:
				if (_panel._wire != WireKind::None) {
					_panel.CompleteWire(index);
				}
				break;
			case NodeKind::None:
				break;
			}
		}

		void AnimatorGraphPanel::ControllerModel::OnNodeDoubleClick(GraphNodeId node) {
			if (KindOf(node) == NodeKind::Machine) {
				_panel.EnterMachine(IndexOf(node));
			}
		}

		void AnimatorGraphPanel::ControllerModel::OnNodeContextMenu(UI::Context& context, GraphNodeId node) {
			switch (KindOf(node)) {
			case NodeKind::Entry:
				_panel.NodeMenu(context, PickKind::None, _panel.DrawnMachine());
				break;
			case NodeKind::AnyState:
				_panel.NodeMenu(context, PickKind::AnyTransition, _panel.DrawnMachine());
				break;
			case NodeKind::State:
				_panel.NodeMenu(context, PickKind::State, IndexOf(node));
				break;
			case NodeKind::Machine:
				_panel.NodeMenu(context, PickKind::Machine, IndexOf(node));
				break;
			case NodeKind::Up:
			case NodeKind::None:
				break;
			}
		}

		bool AnimatorGraphPanel::ControllerModel::AcceptsNodeDrop(GraphNodeId node, const UI::DragPayload& payload) const {
			return KindOf(node) == NodeKind::State && payload.type == StringHash("Asset");
		}

		void AnimatorGraphPanel::ControllerModel::OnNodeDrop(GraphNodeId node, const UI::DragPayload& payload) {
			AnimatorController* live = _panel.Graph();
			const AssetDragData* drag = payload.data.TryGet<AssetDragData>();
			const i32 state = IndexOf(node);
			if (!live || !drag || !drag->guid.IsValid()) {
				return;
			}
			Asset* resolved = AssetDatabase::LoadAssetByGuid(drag->guid);
			if (!resolved || resolved->GetAssetTypeId() != AnimationClip::GetReflectType()->name) {
				if (resolved) {
					AssetDatabase::UnloadAsset(resolved);
				}
				return;
			}
			_panel.Edit("Set Motion", [&]() {
				return AnimatorGraphOps::SetStateClip(*live, state, drag->guid,
													  Handle<AnimationClip>(static_cast<AnimationClip*>(resolved))) >= 0;
			});
			_panel.MarkEdited(false);
			_panel.Select(PickKind::State, state, -1);
		}

		void AnimatorGraphPanel::ControllerModel::OnCanvasContextMenu(UI::Context& context, Vec2 graph) {
			_panel.CanvasMenu(context, graph);
		}

		bool AnimatorGraphPanel::ControllerModel::AcceptsDrop(const UI::DragPayload& payload) const {
			return payload.type == StringHash("Asset") && _panel.Graph() != nullptr && _panel.DrawnMachine() >= 0;
		}

		void AnimatorGraphPanel::ControllerModel::OnDrop(const UI::DragPayload& payload, Vec2 graph) {
			if (const AssetDragData* drag = payload.data.TryGet<AssetDragData>()) {
				_panel.CreateStateFromClip(drag->guid, graph);
			}
		}

		namespace {
			enum class SearchKind : u64 {
				State = 1,
				Machine,
				Clip
			};
			inline u64 SearchKey(SearchKind kind, u32 index) { return (static_cast<u64>(kind) << 32) | index; }
		} // anonymous namespace

		void AnimatorGraphPanel::ControllerModel::GetSearchEntries(const GraphPortRef* from, Vector<GraphSearchEntry>& out) {
			if (from || !_panel.Graph() || _panel.DrawnMachine() < 0) {
				return;
			}
			GraphSearchEntry state;
			state.name = "State";
			state.synonyms = "empty node";
			state.key = SearchKey(SearchKind::State, 0);
			out.push_back(std::move(state));
			GraphSearchEntry machine;
			machine.name = "Sub-State Machine";
			machine.synonyms = "nested group";
			machine.key = SearchKey(SearchKind::Machine, 0);
			out.push_back(std::move(machine));

			Vector<AssetBrowserOps::AssignableAsset> clips{GetMemoryResource(MemTag::Editor)};
			AssetBrowserOps::CollectAssignableAssets(AnimationClip::GetReflectType()->name, clips);
			_panel._searchClips.clear();
			for (const AssetBrowserOps::AssignableAsset& clip : clips) {
				std::string_view display = clip.display;
				std::string_view name = display;
				const size_t colon = display.find(" : ");
				if (colon != std::string_view::npos) {
					name = display.substr(colon + 3);
				} else {
					const size_t slash = display.find_last_of('/');
					name = slash == std::string_view::npos ? display : display.substr(slash + 1);
					const size_t dot = name.find_last_of('.');
					if (dot != std::string_view::npos && dot > 0) {
						name = name.substr(0, dot);
					}
				}
				GraphSearchEntry entry;
				entry.name = str(name);
				entry.category = "Animation Clips";
				entry.synonyms = "clip motion";
				entry.detail = str(colon != std::string_view::npos ? display.substr(0, colon) : display);
				entry.key = SearchKey(SearchKind::Clip, static_cast<u32>(_panel._searchClips.size()));
				_panel._searchClips.push_back(clip.guid);
				out.push_back(std::move(entry));
			}
		}

		void AnimatorGraphPanel::ControllerModel::OnSearchPick(const GraphSearchEntry& entry, Vec2 graph, const GraphPortRef* from) {
			(void)from;
			const auto kind = static_cast<SearchKind>(entry.key >> 32);
			const u32 index = static_cast<u32>(entry.key);
			switch (kind) {
			case SearchKind::State:
				_panel.CreateState(graph);
				break;
			case SearchKind::Machine:
				_panel.CreateMachine(graph);
				break;
			case SearchKind::Clip:
				if (index < _panel._searchClips.size()) {
					_panel.CreateStateFromClip(_panel._searchClips[index], graph - Vec2(NODE_WIDTH, NODE_HEIGHT) * 0.5f);
				}
				break;
			}
		}

		bool AnimatorGraphPanel::ControllerModel::GetPendingWire(GraphNodeId& from) const {
			const AnimatorController* controller = _panel.Graph();
			switch (_panel._wire) {
			case WireKind::FromState:
				if (!controller || _panel._wireFrom < 0 || static_cast<size_t>(_panel._wireFrom) >= controller->states.size()) {
					return false;
				}
				from = NodeIdOf(NodeKind::State, _panel._wireFrom);
				return true;
			case WireKind::FromAnyState:
				from = NodeIdOf(NodeKind::AnyState, 0);
				return true;
			case WireKind::FromEntry:
				from = NodeIdOf(NodeKind::Entry, 0);
				return true;
			case WireKind::None:
				break;
			}
			return false;
		}

		void AnimatorGraphPanel::ControllerModel::CancelPendingWire() {
			_panel._wire = WireKind::None;
			_panel._wireFrom = -1;
		}

		GraphNodeId AnimatorGraphPanel::ControllerModel::ArrivalFor(i32 destination) const {
			const AnimatorController* controller = _panel.Graph();
			const i32 machine = _panel.DrawnMachine();
			if (!controller || machine < 0 || destination < 0 || static_cast<size_t>(destination) >= controller->states.size()) {
				return 0;
			}
			const i32 proxy = _panel.ProxyMachineFor(destination);
			if (proxy >= 0) {
				return NodeIdOf(NodeKind::Machine, proxy);
			}
			if (controller->states[static_cast<size_t>(destination)].machine != machine) {
				return controller->machines[static_cast<size_t>(machine)].parent >= 0 ? NodeIdOf(NodeKind::Up, 0) : 0;
			}
			return NodeIdOf(NodeKind::State, destination);
		}

		void AnimatorGraphPanel::ControllerModel::GetWires(Vector<GraphWireDesc>& out) const {
			const AnimatorController* controller = _panel.Graph();
			const i32 machine = _panel.DrawnMachine();
			if (!controller || machine < 0) {
				return;
			}
			const AnimatorController::StateMachine& drawn = controller->machines[static_cast<size_t>(machine)];

			auto push = [&](PickKind kind, i32 a, i32 b, GraphNodeId from, GraphNodeId to, i32 fan) {
				GraphWireDesc wire;
				wire.style = GraphWireStyle::Arrow;
				wire.from.node = from;
				wire.to.node = to;
				wire.fan = fan;
				wire.color = ColorToken::WidgetTextDim;
				if (kind != PickKind::None) {
					wire.id = WireIdOf(kind, a, b);
				}
				out.push_back(wire);
			};

			if (const GraphNodeId arrival = ArrivalFor(drawn.defaultState)) {
				push(PickKind::None, -1, -1, NodeIdOf(NodeKind::Entry, 0), arrival, 0);
			}

			for (size_t index = 0; index < drawn.anyState.size(); ++index) {
				const GraphNodeId arrival = ArrivalFor(drawn.anyState[index].destination);
				if (!arrival) {
					continue;
				}
				i32 fan = 0;
				for (size_t earlier = 0; earlier < index; ++earlier) {
					if (drawn.anyState[earlier].destination == drawn.anyState[index].destination) {
						++fan;
					}
				}
				push(PickKind::AnyTransition, machine, static_cast<i32>(index), NodeIdOf(NodeKind::AnyState, 0), arrival, fan);
			}

			for (size_t source = 0; source < controller->states.size(); ++source) {
				const AnimatorController::State& state = controller->states[source];
				if (state.machine != machine) {
					continue;
				}
				for (size_t index = 0; index < state.transitions.size(); ++index) {
					const GraphNodeId arrival = ArrivalFor(state.transitions[index].destination);
					if (!arrival) {
						continue;
					}
					i32 fan = 0;
					for (size_t earlier = 0; earlier < index; ++earlier) {
						if (state.transitions[earlier].destination == state.transitions[index].destination) {
							++fan;
						}
					}
					push(PickKind::Transition, static_cast<i32>(source), static_cast<i32>(index),
						 NodeIdOf(NodeKind::State, static_cast<i32>(source)), arrival, fan);
				}
			}
		}

		void AnimatorGraphPanel::CompleteWire(i32 targetState) {
			AnimatorController* controller = Graph();
			const WireKind kind = _wire;
			const i32 from = _wireFrom;
			_wire = WireKind::None;
			_wireFrom = -1;
			if (!controller) {
				return;
			}
			switch (kind) {
			case WireKind::FromState: {
				i32 index = -1;
				if (Edit("Make Transition", [&]() {
						index = AnimatorGraphOps::AddTransition(*controller, from, targetState);
						return index >= 0;
					})) {
					MarkEdited(false);
					Select(PickKind::Transition, from, index);
				}
				break;
			}
			case WireKind::FromAnyState: {
				i32 index = -1;
				if (Edit("Make Transition", [&]() {
						index = AnimatorGraphOps::AddAnyStateTransition(*controller, from, targetState);
						return index >= 0;
					})) {
					MarkEdited(false);
					Select(PickKind::AnyTransition, from, index);
				}
				break;
			}
			case WireKind::FromEntry:
				if (Edit("Set Default State", [&]() { return AnimatorGraphOps::SetDefaultState(*controller, from, targetState); })) {
					MarkEdited(true);
				}
				break;
			case WireKind::None:
				break;
			}
		}

		void AnimatorGraphPanel::CanvasMenu(UI::Context& context, Vec2 point) {
			if (!Graph() || DrawnMachine() < 0) {
				return;
			}
			Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
			UI::Widgets::MenuItem create;
			create.label = "Create State";
			create.onSelect = [this, point]() { CreateState(point); };
			items.push_back(std::move(create));

			UI::Widgets::MenuItem nest;
			nest.label = "Create Sub-State Machine";
			nest.onSelect = [this, point]() { CreateMachine(point); };
			items.push_back(std::move(nest));

			UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(),
								  EditorTheme::GetMetric(MetricToken::FontSmall));
		}

		void AnimatorGraphPanel::CreateState(Vec2 centre) {
			AnimatorController* live = Graph();
			const i32 machine = DrawnMachine();
			if (!live || machine < 0) {
				return;
			}
			i32 state = -1;
			Edit("Create State", [&]() {
				state = AnimatorGraphOps::AddState(*live, machine, "New State", centre - Vec2(NODE_WIDTH, NODE_HEIGHT) * 0.5f);
				return state >= 0;
			});
			MarkEdited(true);
			Select(PickKind::State, state, -1);
		}

		void AnimatorGraphPanel::CreateMachine(Vec2 centre) {
			AnimatorController* live = Graph();
			const i32 machine = DrawnMachine();
			if (!live || machine < 0) {
				return;
			}
			i32 child = -1;
			if (Edit("Create Sub-State Machine", [&]() {
					child = AnimatorGraphOps::AddMachine(*live, machine, "New Sub-State Machine",
														 centre - Vec2(NODE_WIDTH, NODE_HEIGHT) * 0.5f);
					return child >= 0;
				})) {
				MarkEdited(true);
				Select(PickKind::Machine, child, -1);
			}
		}

		void AnimatorGraphPanel::CreateStateFromClip(const Guid& clipGuid, Vec2 at) {
			AnimatorController* controller = Graph();
			const i32 machine = DrawnMachine();
			if (!controller || machine < 0 || !clipGuid.IsValid()) {
				return;
			}
			Asset* resolved = AssetDatabase::LoadAssetByGuid(clipGuid);
			if (!resolved || resolved->GetAssetTypeId() != AnimationClip::GetReflectType()->name) {
				if (resolved) {
					AssetDatabase::UnloadAsset(resolved);
				}
				return;
			}
			auto* clip = static_cast<AnimationClip*>(resolved);
			std::string_view path = clip->virtualPath;
			const size_t slash = path.find_last_of('/');
			std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
			const size_t dot = name.find_last_of('.');
			if (dot != std::string_view::npos) {
				name = name.substr(0, dot);
			}
			i32 state = -1;
			Edit("Create State", [&]() {
				state = AnimatorGraphOps::AddState(*controller, machine, name, at);
				AnimatorGraphOps::SetStateClip(*controller, state, clipGuid, Handle<AnimationClip>(clip));
				return state >= 0;
			});
			MarkEdited(true);
			Select(PickKind::State, state, -1);
		}

		void AnimatorGraphPanel::NodeMenu(UI::Context& context, PickKind kind, i32 a) {
			AnimatorController* controller = Graph();
			if (!controller) {
				return;
			}
			Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};

			UI::Widgets::MenuItem make;
			make.label = "Make Transition";
			make.onSelect = [this, kind, a]() {
				_wire = kind == PickKind::State	 ? WireKind::FromState
						: kind == PickKind::None ? WireKind::FromEntry
												 : WireKind::FromAnyState;
				_wireFrom = a;
			};
			items.push_back(std::move(make));

			if (kind == PickKind::Machine) {
				items.clear();

				UI::Widgets::MenuItem open;
				open.label = "Open";
				open.onSelect = [this, a]() { EnterMachine(a); };
				items.push_back(std::move(open));

				UI::Widgets::MenuItem removeMachine;
				removeMachine.label = "Delete";
				removeMachine.shortcut = "Del";
				removeMachine.separatorBefore = true;
				removeMachine.onSelect = [this]() { DeleteSelection(); };
				items.push_back(std::move(removeMachine));
			}

			if (kind == PickKind::State) {
				UI::Widgets::MenuItem asDefault;
				asDefault.label = "Set as Default State";
				asDefault.onSelect = [this, a]() {
					AnimatorController* live = Graph();
					if (live && Edit("Set Default State", [&]() { return AnimatorGraphOps::SetDefaultState(*live, DrawnMachine(), a); })) {
						MarkEdited(true);
					}
				};
				items.push_back(std::move(asDefault));

				UI::Widgets::MenuItem remove;
				remove.label = "Delete";
				remove.shortcut = "Del";
				remove.separatorBefore = true;
				remove.onSelect = [this]() { DeleteSelection(); };
				items.push_back(std::move(remove));
			}

			UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(),
								  EditorTheme::GetMetric(MetricToken::FontSmall));
		}

		// removals shift arrays, order of kinds matters
		void AnimatorGraphPanel::DeleteSelection() {
			AnimatorController* controller = Graph();
			if (!controller) {
				return;
			}
			Vector<i32> states{GetMemoryResource(MemTag::Editor)};
			Vector<i32> machines{GetMemoryResource(MemTag::Editor)};
			Vector<std::pair<i32, i32>> transitions{GetMemoryResource(MemTag::Editor)};
			Vector<std::pair<i32, i32>> anyTransitions{GetMemoryResource(MemTag::Editor)};
			for (const GraphItem& item : _canvas.GetSelection()) {
				if (item.kind == GraphItemKind::Wire) {
					const std::pair<i32, i32> edge{WireAOf(item.id), WireBOf(item.id)};
					(WireKindOf(item.id) == PickKind::AnyTransition ? anyTransitions : transitions).push_back(edge);
				} else if (KindOf(item.id) == NodeKind::State) {
					states.push_back(IndexOf(item.id));
				} else if (KindOf(item.id) == NodeKind::Machine) {
					machines.push_back(IndexOf(item.id));
				}
			}
			if (states.empty() && machines.empty() && transitions.empty() && anyTransitions.empty()) {
				return;
			}
			const auto highestFirst = [](const auto& left, const auto& right) { return left > right; };
			std::sort(transitions.begin(), transitions.end(), highestFirst);
			std::sort(anyTransitions.begin(), anyTransitions.end(), highestFirst);
			std::sort(states.begin(), states.end(), highestFirst);
			std::sort(machines.begin(), machines.end(), highestFirst);

			bool removedMachine = false;
			const bool removed = Edit("Delete", [&]() {
				bool any = false;
				for (const std::pair<i32, i32>& edge : transitions) {
					any = AnimatorGraphOps::RemoveTransition(*controller, edge.first, edge.second) || any;
				}
				for (const std::pair<i32, i32>& edge : anyTransitions) {
					any = AnimatorGraphOps::RemoveAnyStateTransition(*controller, edge.first, edge.second) || any;
				}
				for (const i32 state : states) {
					any = AnimatorGraphOps::RemoveState(*controller, state) || any;
				}
				for (const i32 machine : machines) {
					if (AnimatorGraphOps::RemoveMachine(*controller, machine)) {
						any = true;
						removedMachine = true;
					}
				}
				return any;
			});
			if (!removed) {
				return;
			}
			MarkEdited(true);
			Select(PickKind::None, -1, -1);
			if (removedMachine) {
				_needsToolbarRebuild = true;
				_needsFrame = true;
			}
		}

		bool AnimatorGraphPanel::OnCommand(PanelCommand command) {
			if (_blackboard.HasFocus()) {
				return EditorPanel::OnCommand(command);
			}
			switch (command) {
			case PanelCommand::Delete:
				if (_canvas.GetSelection().empty()) {
					break;
				}
				DeleteSelection();
				return true;
			case PanelCommand::SelectAll:
				if (!Graph()) {
					break;
				}
				_canvas.SelectAll();
				SyncPick();
				return true;
			default:
				break;
			}
			return EditorPanel::OnCommand(command);
		}

		void AnimatorGraphPanel::RefreshTitle(UI::Context& context) {
			if (!context.IsAlive(_title)) {
				return;
			}
			const AnimatorController* controller = Graph();
			str label = str("no controller selected");
			if (controller) {
				std::string_view path = controller->virtualPath;
				const size_t slash = path.find_last_of('/');
				label = str(slash == std::string_view::npos ? path : path.substr(slash + 1));
				if (_dirty) {
					label += " *";
				}
			}
			context.SetText(_title, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontBody),
							EditorTheme::GetColor(ColorToken::TextStrong), label);

			if (!_needsToolbarRebuild) {
				return;
			}
			_needsToolbarRebuild = false;
			RebuildCrumbs(context);
			if (!context.IsAlive(_layerRow)) {
				return;
			}
			context.ClearChildren(_layerRow);
			if (!controller) {
				return;
			}
			Vector<std::string_view> names{GetMemoryResource(MemTag::Editor)};
			for (const AnimatorController::Layer& layer : controller->layers) {
				names.push_back(layer.name);
			}

			if (names.size() >= 2) {
				UI::Widgets::DropdownDesc desc;
				desc.items = std::span<const std::string_view>(names.data(), names.size());
				desc.selected = static_cast<u32>(Math::Clamp(_layer, 0, static_cast<i32>(names.size()) - 1));
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				desc.onSelect = [this](u32 index) {
					_layer = static_cast<i32>(index);
					_machine = -1;
					Select(PickKind::None, -1, -1);
					_needsNodeRebuild = true;
					_needsToolbarRebuild = true;
					_needsFrame = true;
				};
				UI::Widgets::Dropdown(context, _layerRow, desc);
			}

			UI::Widgets::ButtonDesc add;
			add.label = "+";
			add.font = UISystem::GetDefaultFont();
			add.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			add.onClick = [this]() {
				AnimatorController* live = Graph();
				if (!live) {
					return;
				}
				i32 minted = -1;
				if (!Edit("Add Layer", [&]() {
						for (u32 attempt = 0; attempt < 64 && minted < 0; ++attempt) {
							const str candidate = attempt == 0 ? str("New Layer") : std::format("New Layer {}", attempt);
							minted = AnimatorGraphOps::AddLayer(*live, candidate);
						}
						return minted >= 0;
					})) {
					return;
				}
				_layer = minted;
				_machine = -1;
				Select(PickKind::Layer, minted, -1);
				MarkEdited(true);
				_needsNodeRebuild = true;
				_needsToolbarRebuild = true;
				_needsFrame = true;
			};
			UI::Widgets::Button(context, _layerRow, add);

			UI::Widgets::ButtonDesc settings;
			settings.label = "Settings";
			settings.font = UISystem::GetDefaultFont();
			settings.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			settings.onClick = [this]() {
				AnimatorController* live = Graph();
				if (!live || live->layers.empty()) {
					return;
				}
				Select(PickKind::Layer, Math::Clamp(_layer, 0, static_cast<i32>(live->layers.size()) - 1), -1);
				_needsFrame = true;
			};
			UI::Widgets::Button(context, _layerRow, settings);
		}

		void AnimatorGraphPanel::RebuildProperties(UI::Context& context) {
			if (!context.IsAlive(_properties)) {
				return;
			}
			EndScrub();
			context.ClearChildren(_properties);
			AnimatorController* controller = Graph();
			if (!controller) {
				AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontSmall,
						"select an Animator Controller in the Asset Browser");
				return;
			}

			switch (_pick) {
			case PickKind::None:
				AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontSmall,
						"right-click the grid to create a state (or drag a clip in)");
				return;
			case PickKind::Layer: {
				if (_pickA < 0 || static_cast<size_t>(_pickA) >= controller->layers.size()) {
					return;
				}
				const i32 layer = _pickA;
				AnimatorController::Layer& target = controller->layers[static_cast<size_t>(layer)];
				AddText(context, _properties, ColorToken::TextStrong, MetricToken::FontBody, "Layer");

				UI::ElementHandle nameRow = AddLabeledRow(context, _properties, "Name");
				UI::Widgets::TextFieldDesc name;
				name.text = target.name;
				name.font = UISystem::GetDefaultFont();
				name.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				name.onCommit = [this, layer](std::string_view value) {
					AnimatorController* live = Graph();
					if (live && Edit("Rename Layer", [&]() { return AnimatorGraphOps::RenameLayer(*live, layer, value); })) {
						MarkEdited(true);
						_needsToolbarRebuild = true;
					}
				};
				UI::Widgets::TextField(context, nameRow, name);

				UI::ElementHandle weightRow = AddLabeledRow(context, _properties, "Weight");
				UI::Widgets::NumberFieldDesc weight;
				weight.value = target.weight;
				weight.dragScale = 0.01;
				weight.font = UISystem::GetDefaultFont();
				weight.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				weight.onChange = [this, layer](f64 value) {
					AnimatorController* live = Graph();
					if (live && Edit("Layer Weight", [&]() { return AnimatorGraphOps::SetLayerWeight(*live, layer, static_cast<f32>(value)); })) {
						MarkEdited(false);
					}
				};
				ScrubAsOneEdit(weight, "Layer Weight");
				UI::Widgets::NumberField(context, weightRow, weight);

				UI::ElementHandle blendRow = AddLabeledRow(context, _properties, "Blending");
				UI::Widgets::DropdownDesc blend;
				blend.items = std::span<const std::string_view>(LayerBlendModes, std::size(LayerBlendModes));
				blend.selected = static_cast<u32>(target.blend);
				blend.font = UISystem::GetDefaultFont();
				blend.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				blend.onSelect = [this, layer](u32 picked) {
					AnimatorController* live = Graph();
					if (live && Edit("Layer Blending",
									 [&]() { return AnimatorGraphOps::SetLayerBlend(*live, layer, static_cast<AnimationBlendMode>(picked)); })) {
						MarkEdited(false);
					}
				};
				UI::Widgets::Dropdown(context, blendRow, blend);

				AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontSmall, "Mask");
				for (size_t index = 0; index < target.mask.size(); ++index) {
					const i32 slot = static_cast<i32>(index);
					UI::ElementHandle row = AddRow(context, _properties);

					UI::Widgets::TextFieldDesc bone;
					bone.text = target.mask[index].bone;
					bone.font = UISystem::GetDefaultFont();
					bone.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny);
					bone.onCommit = [this, layer, slot](std::string_view value) {
						AnimatorController* live = Graph();
						if (!live || value.empty() || static_cast<size_t>(layer) >= live->layers.size()) {
							return;
						}
						const bool renamed = Edit("Rename Masked Bone", [&]() {
							AnimatorController::Layer& edited = live->layers[static_cast<size_t>(layer)];
							if (static_cast<size_t>(slot) >= edited.mask.size()) {
								return false;
							}
							for (size_t other = 0; other < edited.mask.size(); ++other) {
								if (other != static_cast<size_t>(slot) && edited.mask[other].bone == value) {
									return false;
								}
							}
							edited.mask[static_cast<size_t>(slot)].bone = str(value);
							return true;
						});
						if (renamed) {
							MarkEdited(false);
						}
					};
					UI::Widgets::TextField(context, row, bone);

					UI::Widgets::NumberFieldDesc entryWeight;
					entryWeight.value = target.mask[index].weight;
					entryWeight.dragScale = 0.01;
					entryWeight.font = UISystem::GetDefaultFont();
					entryWeight.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny);
					entryWeight.onChange = [this, layer, slot](f64 value) {
						AnimatorController* live = Graph();
						if (live && Edit("Mask Weight", [&]() {
								return AnimatorGraphOps::SetLayerMaskEntryWeight(*live, layer, slot, static_cast<f32>(value));
							})) {
							MarkEdited(false);
						}
					};
					ScrubAsOneEdit(entryWeight, "Mask Weight");
					UI::Widgets::NumberField(context, row, entryWeight);

					UI::Widgets::ButtonDesc drop;
					drop.label = "x";
					drop.font = UISystem::GetDefaultFont();
					drop.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny);
					drop.onClick = [this, layer, slot]() {
						AnimatorController* live = Graph();
						if (live && Edit("Remove Masked Bone", [&]() { return AnimatorGraphOps::RemoveLayerMaskEntry(*live, layer, slot); })) {
							MarkEdited(false);
							_needsPropertyRebuild = true;
						}
					};
					UI::Widgets::Button(context, row, drop);
				}

				UI::Widgets::ButtonDesc addMask;
				addMask.label = "Add Masked Bone";
				addMask.font = UISystem::GetDefaultFont();
				addMask.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				addMask.onClick = [this, layer]() {
					AnimatorController* live = Graph();
					if (!live) {
						return;
					}
					i32 minted = -1;
					if (Edit("Add Masked Bone", [&]() {
							for (u32 attempt = 0; attempt < 64 && minted < 0; ++attempt) {
								const str candidate = attempt == 0 ? str("Bone") : std::format("Bone {}", attempt);
								minted = AnimatorGraphOps::AddLayerMaskEntry(*live, layer, candidate, 1.0f);
							}
							return minted >= 0;
						})) {
						MarkEdited(false);
						_needsPropertyRebuild = true;
					}
				};
				UI::Widgets::Button(context, _properties, addMask);

				if (layer != 0) {
					UI::Widgets::ButtonDesc remove;
					remove.label = "Remove Layer";
					remove.font = UISystem::GetDefaultFont();
					remove.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
					remove.onClick = [this, layer]() {
						AnimatorController* live = Graph();
						if (!live || !Edit("Remove Layer", [&]() { return AnimatorGraphOps::RemoveLayer(*live, layer); })) {
							return;
						}
						_layer = 0;
						_machine = -1;
						Select(PickKind::None, -1, -1);
						MarkEdited(true);
						_needsNodeRebuild = true;
						_needsToolbarRebuild = true;
						_needsFrame = true;
					};
					UI::Widgets::Button(context, _properties, remove);
				} else {
					AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontTiny,
							"the base layer cannot be removed");
				}
				return;
			}
			case PickKind::Machine: {
				if (static_cast<size_t>(_pickA) >= controller->machines.size()) {
					return;
				}
				const i32 machine = _pickA;
				AddText(context, _properties, ColorToken::TextStrong, MetricToken::FontBody, "State Machine");

				UI::ElementHandle machineName = AddLabeledRow(context, _properties, "Name");
				UI::Widgets::TextFieldDesc rename;
				rename.text = controller->machines[static_cast<size_t>(machine)].name;
				rename.font = UISystem::GetDefaultFont();
				rename.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				rename.onCommit = [this, machine](std::string_view value) {
					AnimatorController* live = Graph();
					if (!live || value.empty() || static_cast<size_t>(machine) >= live->machines.size()) {
						return;
					}
					Edit("Rename State Machine", [&]() {
						live->machines[static_cast<size_t>(machine)].name = str(value);
						return true;
					});
					MarkEdited(true);
					_needsToolbarRebuild = true;
				};
				UI::Widgets::TextField(context, machineName, rename);

				UI::Widgets::ButtonDesc open;
				open.label = "Open";
				open.font = UISystem::GetDefaultFont();
				open.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				open.onClick = [this, machine]() { EnterMachine(machine); };
				UI::Widgets::Button(context, _properties, open);
				return;
			}
			case PickKind::State: {
				if (static_cast<size_t>(_pickA) >= controller->states.size()) {
					return;
				}
				const i32 state = _pickA;
				AnimatorController::State& target = controller->states[static_cast<size_t>(state)];
				AddText(context, _properties, ColorToken::TextStrong, MetricToken::FontBody, "State");

				UI::ElementHandle nameRow = AddLabeledRow(context, _properties, "Name");
				UI::Widgets::TextFieldDesc name;
				name.text = target.name;
				name.font = UISystem::GetDefaultFont();
				name.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				name.onCommit = [this, state](std::string_view value) {
					AnimatorController* live = Graph();
					if (!live || value.empty() || static_cast<size_t>(state) >= live->states.size()) {
						return;
					}
					Edit("Rename State", [&]() {
						live->states[static_cast<size_t>(state)].name = str(value);
						return true;
					});
					MarkEdited(true);
				};
				UI::Widgets::TextField(context, nameRow, name);

				UI::ElementHandle speedRow = AddLabeledRow(context, _properties, "Speed");
				UI::Widgets::NumberFieldDesc speed;
				speed.value = target.speed;
				speed.dragScale = 0.01;
				speed.font = UISystem::GetDefaultFont();
				speed.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				speed.onChange = [this, state](f64 value) {
					AnimatorController* live = Graph();
					if (live && static_cast<size_t>(state) < live->states.size()) {
						Edit("State Speed", [&]() {
							live->states[static_cast<size_t>(state)].speed = static_cast<f32>(value);
							return true;
						});
						MarkEdited(false);
					}
				};
				ScrubAsOneEdit(speed, "State Speed");
				UI::Widgets::NumberField(context, speedRow, speed);

				UI::ElementHandle motionRow = AddLabeledRow(context, _properties, "Motion");
				AddText(context, motionRow, ColorToken::WidgetText, MetricToken::FontSmall,
						MotionLabel(*controller, target.motion));

				if (target.motion >= 0 && static_cast<size_t>(target.motion) < controller->nodes.size()) {
					const AnimatorController::MotionNode& motion = controller->nodes[static_cast<size_t>(target.motion)];
					AnimatorStateMachine* component = _live.Get();
					if (component && motion.kind != AnimatorBlendKind::Clip && !motion.parameterX.empty()) {
						char buffer[96];
						std::snprintf(buffer, sizeof(buffer), "sample  %.3f, %.3f",
									  static_cast<f64>(component->GetFloat(motion.parameterX)),
									  static_cast<f64>(component->GetFloat(motion.parameterY)));
						AddLabeledRow(context, _properties, "Live");
						AddText(context, _properties, ColorToken::GraphNodeActive, MetricToken::FontSmall, buffer);
					}
				}
				AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontTiny,
						"drop an Animation Clip on the node to set it");
				return;
			}
			case PickKind::Transition:
			case PickKind::AnyTransition: {
				AnimatorController::Transition* transition = nullptr;
				if (_pick == PickKind::Transition && static_cast<size_t>(_pickA) < controller->states.size()) {
					Vector<AnimatorController::Transition>& list =
						controller->states[static_cast<size_t>(_pickA)].transitions;
					if (static_cast<size_t>(_pickB) < list.size()) {
						transition = &list[static_cast<size_t>(_pickB)];
					}
				} else if (_pick == PickKind::AnyTransition && static_cast<size_t>(_pickA) < controller->machines.size()) {
					Vector<AnimatorController::Transition>& list =
						controller->machines[static_cast<size_t>(_pickA)].anyState;
					if (static_cast<size_t>(_pickB) < list.size()) {
						transition = &list[static_cast<size_t>(_pickB)];
					}
				}
				if (!transition) {
					return;
				}
				const PickKind kind = _pick;
				const i32 owner = _pickA;
				const i32 slot = _pickB;

				auto live = [this, kind, owner, slot]() -> AnimatorController::Transition* {
					AnimatorController* asset = Graph();
					if (!asset) {
						return nullptr;
					}
					if (kind == PickKind::Transition && static_cast<size_t>(owner) < asset->states.size()) {
						Vector<AnimatorController::Transition>& list = asset->states[static_cast<size_t>(owner)].transitions;
						return static_cast<size_t>(slot) < list.size() ? &list[static_cast<size_t>(slot)] : nullptr;
					}
					if (kind == PickKind::AnyTransition && static_cast<size_t>(owner) < asset->machines.size()) {
						Vector<AnimatorController::Transition>& list = asset->machines[static_cast<size_t>(owner)].anyState;
						return static_cast<size_t>(slot) < list.size() ? &list[static_cast<size_t>(slot)] : nullptr;
					}
					return nullptr;
				};

				AddText(context, _properties, ColorToken::TextStrong, MetricToken::FontBody,
						kind == PickKind::AnyTransition ? "Any State Transition" : "Transition");
				{
					const i32 destination = transition->destination;
					const str to = destination >= 0 && static_cast<size_t>(destination) < controller->states.size()
									   ? controller->states[static_cast<size_t>(destination)].name
									   : str("<none>");
					AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontSmall, str("to ") + to);
				}

				auto numberRow = [&](std::string_view label, f64 value, f64 step, auto&& write) {
					UI::ElementHandle row = AddLabeledRow(context, _properties, label);
					UI::Widgets::NumberFieldDesc desc;
					desc.value = value;
					desc.dragScale = step;
					desc.font = UISystem::GetDefaultFont();
					desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
					desc.onChange = [this, live, write, name = str(label)](f64 changed) {
						Edit(name, [&]() {
							AnimatorController::Transition* target = live();
							if (target) {
								write(*target, changed);
							}
							return target != nullptr;
						});
					};
					ScrubAsOneEdit(desc, label);
					UI::Widgets::NumberField(context, row, desc);
				};

				numberRow("Exit Time", transition->exitTime, 0.01,
						  [this](AnimatorController::Transition& t, f64 v) {
							  t.exitTime = static_cast<f32>(v);
							  MarkEdited(false);
						  });
				AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontTiny,
						"negative = as soon as the conditions hold");
				numberRow("Duration", transition->duration, 0.01,
						  [this](AnimatorController::Transition& t, f64 v) {
							  t.duration = static_cast<f32>(v);
							  MarkEdited(false);
						  });
				numberRow("Offset", transition->offset, 0.01,
						  [this](AnimatorController::Transition& t, f64 v) {
							  t.offset = static_cast<f32>(v);
							  MarkEdited(false);
						  });

				UI::ElementHandle syncRow = AddLabeledRow(context, _properties, "Sync Phase");
				UI::Widgets::Checkbox(context, syncRow, transition->syncPhase, [this, live](bool value) {
					if (Edit("Sync Phase", [&]() {
							AnimatorController::Transition* target = live();
							if (target) {
								target->syncPhase = value;
							}
							return target != nullptr;
						})) {
						MarkEdited(false);
					}
				});

				AddText(context, _properties, ColorToken::WidgetText, MetricToken::FontSmall, "Conditions");
				Vector<std::string_view> parameterNames{GetMemoryResource(MemTag::Editor)};
				for (const AnimatorController::Parameter& parameter : controller->parameters) {
					parameterNames.push_back(parameter.name);
				}

				auto sizedCell = [&context](UI::ElementHandle element, f32 width) {
					UI::Style style = context.GetStyle(element);
					style.width = UI::Length::Pixel(width);
					style.flexShrink = 0.0f;
					context.SetStyle(element, style);
				};

				for (size_t index = 0; index < transition->conditions.size(); ++index) {
					const AnimatorController::Condition& condition = transition->conditions[index];
					const i32 slotIndex = static_cast<i32>(index);
					UI::ElementHandle row = AddRow(context, _properties);

					u32 selected = 0;
					for (size_t p = 0; p < parameterNames.size(); ++p) {
						if (parameterNames[p] == condition.parameter) {
							selected = static_cast<u32>(p);
							break;
						}
					}
					if (!parameterNames.empty()) {
						UI::Widgets::DropdownDesc which;
						which.items = std::span<const std::string_view>(parameterNames.data(), parameterNames.size());
						which.selected = selected;
						which.font = UISystem::GetDefaultFont();
						which.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny);
						which.onSelect = [this, live, slotIndex](u32 picked) {
							const bool changed = Edit("Condition Parameter", [&]() {
								AnimatorController* asset = Graph();
								AnimatorController::Transition* target = live();
								if (!asset || !target || static_cast<size_t>(picked) >= asset->parameters.size() ||
									static_cast<size_t>(slotIndex) >= target->conditions.size()) {
									return false;
								}
								target->conditions[static_cast<size_t>(slotIndex)].parameter =
									asset->parameters[static_cast<size_t>(picked)].name;
								return true;
							});
							if (changed) {
								MarkEdited(false);
							}
						};
						sizedCell(UI::Widgets::Dropdown(context, row, which).root, 82.0f);
					}

					UI::Widgets::DropdownDesc mode;
					mode.items = std::span<const std::string_view>(ConditionModes, std::size(ConditionModes));
					mode.selected = static_cast<u32>(condition.mode);
					mode.font = UISystem::GetDefaultFont();
					mode.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny);
					mode.onSelect = [this, live, slotIndex](u32 picked) {
						const bool changed = Edit("Condition Mode", [&]() {
							AnimatorController::Transition* target = live();
							if (!target || static_cast<size_t>(slotIndex) >= target->conditions.size()) {
								return false;
							}
							target->conditions[static_cast<size_t>(slotIndex)].mode = static_cast<AnimatorConditionMode>(picked);
							return true;
						});
						if (changed) {
							MarkEdited(false);
						}
					};
					sizedCell(UI::Widgets::Dropdown(context, row, mode).root, 82.0f);

					UI::Widgets::NumberFieldDesc threshold;
					threshold.value = condition.threshold;
					threshold.dragScale = 0.05;
					threshold.font = UISystem::GetDefaultFont();
					threshold.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny);
					threshold.onChange = [this, live, slotIndex](f64 value) {
						const bool changed = Edit("Condition Threshold", [&]() {
							AnimatorController::Transition* target = live();
							if (!target || static_cast<size_t>(slotIndex) >= target->conditions.size()) {
								return false;
							}
							target->conditions[static_cast<size_t>(slotIndex)].threshold = static_cast<f32>(value);
							return true;
						});
						if (changed) {
							MarkEdited(false);
						}
					};
					ScrubAsOneEdit(threshold, "Condition Threshold");
					sizedCell(UI::Widgets::NumberField(context, row, threshold).root, 58.0f);

					UI::Widgets::ButtonDesc drop;
					drop.label = "x";
					drop.font = UISystem::GetDefaultFont();
					drop.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny);
					drop.onClick = [this, live, slotIndex]() {
						const bool changed = Edit("Remove Condition", [&]() {
							AnimatorController::Transition* target = live();
							if (!target || static_cast<size_t>(slotIndex) >= target->conditions.size()) {
								return false;
							}
							target->conditions.erase(target->conditions.begin() + static_cast<std::ptrdiff_t>(slotIndex));
							return true;
						});
						if (changed) {
							MarkEdited(false);
							_needsPropertyRebuild = true;
						}
					};
					UI::Widgets::Button(context, row, drop);
				}

				UI::Widgets::ButtonDesc add;
				add.label = "Add Condition";
				add.font = UISystem::GetDefaultFont();
				add.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				add.onClick = [this, live]() {
					const bool changed = Edit("Add Condition", [&]() {
						AnimatorController* asset = Graph();
						AnimatorController::Transition* target = live();
						if (!asset || !target || asset->parameters.empty()) {
							return false;
						}
						AnimatorController::Condition condition;
						condition.parameter = asset->parameters[0].name;
						target->conditions.push_back(std::move(condition));
						return true;
					});
					if (changed) {
						MarkEdited(false);
						_needsPropertyRebuild = true;
					}
				};
				UI::Widgets::Button(context, _properties, add);
				if (controller->parameters.empty()) {
					AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontTiny,
							"declare a parameter below");
				}
				return;
			}
			}
		}

		void AnimatorGraphPanel::RebuildParameters(UI::Context& context) {
			if (!context.IsAlive(_parameters) || !context.IsAlive(_liveParameters)) {
				return;
			}
			EndScrub();
			context.ClearChildren(_liveParameters);
			_parameterValues.clear();
			AnimatorController* controller = Graph();

			const bool live = controller && !_liveObject.empty();
			auto show = [&context](UI::ElementHandle element, bool shown) {
				UI::Style style = context.GetStyle(element);
				style.display = shown ? UI::Display::Flex : UI::Display::None;
				context.SetStyle(element, style);
			};
			show(_parameters, controller && !live);
			show(_liveParameters, live);
			if (live) {
				AddText(context, _liveParameters, ColorToken::TextStrong, MetricToken::FontBody, "Parameters (live)");
				for (const AnimatorController::Parameter& parameter : controller->parameters) {
					UI::ElementHandle row = AddRow(context, _liveParameters);
					UI::ElementHandle name =
						AddText(context, row, ColorToken::WidgetTextDim, MetricToken::FontTiny, parameter.name);
					UI::Style nameStyle;
					nameStyle.width = UI::Length::Pixel(150.0f);
					nameStyle.flexShrink = 0.0f;
					nameStyle.textOverflow = UI::TextOverflow::Ellipsis;
					context.SetStyle(name, nameStyle);
					_parameterValues.push_back(AddText(context, row, ColorToken::TextStrong, MetricToken::FontTiny, ""));
				}
				return;
			}

			Vector<GraphBlackboardItem> items{GetMemoryResource(MemTag::Editor)};
			if (controller) {
				items.reserve(controller->parameters.size());
				for (const AnimatorController::Parameter& parameter : controller->parameters) {
					GraphBlackboardItem item;
					item.id = ParameterId(parameter.name);
					item.name = parameter.name;
					const u64 id = item.id;
					item.onBuildControls = [this, id](UI::Context& context, UI::ElementHandle row) {
						const AnimatorController* drawn = Graph();
						const i32 slot = ParameterSlot(id);
						if (!drawn || slot < 0) {
							return;
						}
						const AnimatorController::Parameter& parameter = drawn->parameters[static_cast<size_t>(slot)];

						auto sized = [&context](UI::ElementHandle element, f32 width) {
							UI::Style style = context.GetStyle(element);
							style.width = UI::Length::Pixel(width);
							style.flexShrink = 0.0f;
							style.flexGrow = 0.0f;
							context.SetStyle(element, style);
						};

						UI::Widgets::DropdownDesc kind;
						kind.items = std::span<const std::string_view>(ParameterKinds, std::size(ParameterKinds));
						kind.selected = static_cast<u32>(parameter.kind);
						kind.font = UISystem::GetDefaultFont();
						kind.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny);
						kind.onSelect = [this, id](u32 picked) {
							AnimatorController* target = Graph();
							const i32 at = ParameterSlot(id);
							if (target && at >= 0) {
								Edit("Parameter Type", [&]() {
									target->parameters[static_cast<size_t>(at)].kind = static_cast<AnimatorParameterKind>(picked);
									return true;
								});
								MarkEdited(false);
							}
						};
						sized(UI::Widgets::Dropdown(context, row, kind).root, 74.0f);

						UI::Widgets::NumberFieldDesc value;
						value.value = parameter.defaultValue;
						value.dragScale = 0.05;
						value.font = UISystem::GetDefaultFont();
						value.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny);
						value.onChange = [this, id](f64 changed) {
							AnimatorController* target = Graph();
							const i32 at = ParameterSlot(id);
							if (target && at >= 0) {
								Edit("Parameter Default", [&]() {
									target->parameters[static_cast<size_t>(at)].defaultValue = static_cast<f32>(changed);
									return true;
								});
								MarkEdited(false);
							}
						};
						ScrubAsOneEdit(value, "Parameter Default");
						sized(UI::Widgets::NumberField(context, row, value).root, 56.0f);
					};
					items.push_back(std::move(item));
				}
			}
			_blackboard.SetItems(items);
		}

		void AnimatorGraphPanel::BuildParameterBoard(UI::Context& context) {
			GraphBlackboardDesc board;
			board.title = "Parameters";
			for (std::string_view kind : ParameterKinds) {
				board.kinds.emplace_back(kind);
			}

			board.onAdd = [this](u32 kind) -> u64 {
				AnimatorController* live = Graph();
				if (!live || kind >= std::size(ParameterKinds)) {
					return 0;
				}
				str added;
				Edit("Add Parameter", [&]() {
					for (u32 suffix = 0; suffix < 1000; ++suffix) {
						str name = str("New ") + str(ParameterKinds[kind]);
						if (suffix > 0) {
							name += " " + std::to_string(suffix);
						}
						if (AnimatorGraphOps::AddParameter(*live, name, static_cast<AnimatorParameterKind>(kind)) >= 0) {
							added = std::move(name);
							return true;
						}
					}
					return false;
				});
				if (added.empty()) {
					return 0;
				}
				MarkEdited(false);
				_needsParameterRebuild = true;
				return ParameterId(added);
			};

			board.onRename = [this](u64 id, std::string_view name) -> u64 {
				AnimatorController* live = Graph();
				const i32 slot = ParameterSlot(id);
				if (!live || slot < 0 ||
					!Edit("Rename Parameter", [&]() { return AnimatorGraphOps::RenameParameter(*live, slot, name); })) {
					return 0;
				}
				MarkEdited(false);
				_needsParameterRebuild = true;
				_needsPropertyRebuild = true;
				return ParameterId(name);
			};

			board.onMove = [this](u64 id, u32 index) -> u64 {
				AnimatorController* live = Graph();
				const i32 slot = ParameterSlot(id);
				if (!live || slot < 0 ||
					!Edit("Move Parameter", [&]() { return AnimatorGraphOps::MoveParameter(*live, slot, static_cast<i32>(index)); })) {
					return 0;
				}
				MarkEdited(false);
				_needsParameterRebuild = true;
				return id;
			};

			board.onDelete = [this](std::span<const u64> ids) {
				AnimatorController* live = Graph();
				if (!live) {
					return;
				}
				Vector<i32> slots{GetMemoryResource(MemTag::Editor)};
				for (u64 id : ids) {
					const i32 slot = ParameterSlot(id);
					if (slot >= 0) {
						slots.push_back(slot);
					}
				}
				if (slots.empty()) {
					return;
				}
				std::sort(slots.begin(), slots.end(), std::greater<i32>());
				const bool removed = Edit(slots.size() == 1 ? "Remove Parameter" : "Remove Parameters", [&]() {
					bool any = false;
					for (i32 slot : slots) {
						any = AnimatorGraphOps::RemoveParameter(*live, slot) || any;
					}
					return any;
				});
				if (removed) {
					MarkEdited(false);
					_needsParameterRebuild = true;
					_needsPropertyRebuild = true;
				}
			};

			_blackboard.Build(this, context, _parameters, std::move(board));
		}

		u64 AnimatorGraphPanel::ParameterId(std::string_view name) {
			const u64 id = StringHash(name).GetValue();
			return id != 0 ? id : 1;
		}

		i32 AnimatorGraphPanel::ParameterSlot(u64 id) const {
			const AnimatorController* controller = Graph();
			if (!controller) {
				return -1;
			}
			for (size_t index = 0; index < controller->parameters.size(); ++index) {
				if (ParameterId(controller->parameters[index].name) == id) {
					return static_cast<i32>(index);
				}
			}
			return -1;
		}

		namespace {

			constexpr u32 SEARCH_INTERVAL = 15;

		} // anonymous namespace

		bool AnimatorGraphPanel::RefreshDebug() {
			const AnimatorController* controller = Graph();
			AnimatorStateMachine* component = _live.Get();

			if (component && (!controller || component->GetController().Get() != controller ||
							  component->GetCurrentState(0) < 0)) {
				component = nullptr;
				_live = {};
			}
			if (!component) {
				if (_searchCooldown > 0) {
					--_searchCooldown;
				} else {
					_searchCooldown = SEARCH_INTERVAL;
					component = AnimationEditor::FindPlayingComponent(controller);
					_live = Handle<AnimatorStateMachine>(component);
				}
			}

			const PickKind wasKind = _activeKind;
			const i32 wasA = _activeA;
			const i32 wasState = _liveState;
			const bool wasPlaying = !_liveObject.empty();

			_activeKind = PickKind::None;
			_activeA = -1;
			_liveState = -1;
			_liveNormalized = 0.0f;
			_liveTravel = 0;
			_liveObject.clear();
			_livePath.clear();
			_liveOtherLayers.clear();

			if (component && controller) {
				const SceneObject* owner = component->GetOwner().Get();
				_liveObject = owner ? owner->name : str("<no object>");
				_liveState = component->GetCurrentState(_layer);
				_liveNormalized = component->GetNormalizedTime(_layer);
				_liveTravel = component->GetTravelRemaining(_layer);
				if (!AnimatorGraphOps::StatePath(*controller, _liveState, _livePath)) {
					_livePath = str("<not running on this layer>");
				}

				const i32 proxy = ProxyMachineFor(_liveState);
				if (proxy >= 0) {
					_activeKind = PickKind::Machine;
					_activeA = proxy;
				} else if (_liveState >= 0 && static_cast<size_t>(_liveState) < controller->states.size() &&
						   controller->states[static_cast<size_t>(_liveState)].machine == DrawnMachine()) {
					_activeKind = PickKind::State;
					_activeA = _liveState;
				}

				for (size_t index = 0; index < controller->layers.size(); ++index) {
					if (static_cast<i32>(index) == _layer) {
						continue;
					}
					const i32 state = component->GetCurrentState(static_cast<i32>(index));
					str path;
					if (state < 0 || !AnimatorGraphOps::StatePath(*controller, state, path)) {
						continue;
					}
					if (!_liveOtherLayers.empty()) {
						_liveOtherLayers += ", ";
					}
					_liveOtherLayers += controller->layers[index].name;
					_liveOtherLayers += ": ";
					_liveOtherLayers += path;
				}
			}

			if (wasPlaying != !_liveObject.empty()) {
				_needsParameterRebuild = true;
			}
			return wasKind != _activeKind || wasA != _activeA || wasState != _liveState;
		}

		void AnimatorGraphPanel::RefreshReadout(UI::Context& context) {
			if (!context.IsAlive(_readout)) {
				return;
			}
			UI::Style style = context.GetStyle(_readout);
			const bool live = !_liveObject.empty();
			const UI::Display wanted = live ? UI::Display::Flex : UI::Display::None;
			if (style.display != wanted) {
				style.display = wanted;
				context.SetStyle(_readout, style);
			}
			if (!live) {
				return;
			}
			str line = _liveObject;
			line += "  ·  ";
			line += _livePath;
			line += "  ·  ";
			line += std::to_string(static_cast<i32>(ProgressOf(_liveNormalized) * 100.0f + 0.5f));
			line += "%";
			if (_liveTravel > 0) {
				line += "  ·  travelling, ";
				line += std::to_string(_liveTravel);
				line += " to go";
			}
			if (!_liveOtherLayers.empty()) {
				line += "  ·  ";
				line += _liveOtherLayers;
			}
			context.SetText(_readout, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
							EditorTheme::GetColor(ColorToken::GraphNodeActive), line);
		}

		void AnimatorGraphPanel::RefreshParameterValues(UI::Context& context) {
			AnimatorStateMachine* component = _live.Get();
			const AnimatorController* controller = Graph();
			if (!component || !controller) {
				return;
			}
			for (size_t index = 0; index < _parameterValues.size() && index < controller->parameters.size(); ++index) {
				if (!context.IsAlive(_parameterValues[index])) {
					continue;
				}
				const AnimatorController::Parameter& parameter = controller->parameters[index];
				const f32 value = component->GetFloat(parameter.name);
				str text;
				switch (parameter.kind) {
				case AnimatorParameterKind::Bool:
				case AnimatorParameterKind::Trigger:
					text = value != 0.0f ? str("true") : str("false");
					break;
				case AnimatorParameterKind::Int:
					text = str(std::to_string(component->GetInt(parameter.name)));
					break;
				case AnimatorParameterKind::Float: {
					char buffer[32];
					std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<f64>(value));
					text = str(buffer);
					break;
				}
				}
				context.SetText(_parameterValues[index], UISystem::GetDefaultFont(),
								EditorTheme::GetMetric(MetricToken::FontTiny),
								EditorTheme::GetColor(ColorToken::TextStrong), text);
			}
		}

		void AnimatorGraphPanel::OnTick(f32 deltaSeconds) {
			const Guid before = _guid;
			FollowSelection();

			if (_documentOpen) {
				if (!EditorDocuments::IsOpen(_guid)) {
					_documentOpen = false;
					_scrubbing = false;
				} else if (EditorDocuments::GetRevision(_guid) != _seenRevision) {
					OnDocumentRestored();
				}
			}

			UI::Context* context = GetPanelContext();
			if (!context) {
				return;
			}
			const bool marksMoved = RefreshDebug();
			if (marksMoved) {
				_needsPlace = true;
			}
			if (_needsNodeRebuild) {
				RebuildNodes(*context);
				_needsPropertyRebuild = true;
			}
			if (_needsPropertyRebuild) {
				_needsPropertyRebuild = false;
				RebuildProperties(*context);
			}
			if (_needsParameterRebuild) {
				_needsParameterRebuild = false;
				RebuildParameters(*context);
			}
			_blackboard.Tick(deltaSeconds);
			if (_needsFrame && _canvas.FrameAll(*context)) {
				_needsFrame = false;
				_needsPlace = false;
			}
			if (_needsPlace) {
				_needsPlace = false;
				_canvas.PlaceNodes(*context);
			}
			RefreshTitle(*context);
			RefreshReadout(*context);
			RefreshParameterValues(*context);
			(void)before;
		}

	} // namespace AnimationEditorImpl

	namespace AnimationEditor {

		namespace {

			struct LiveSearch {
				const AnimatorController* wanted = nullptr;
				AnimatorStateMachine* found = nullptr;
			};

			void VisitForController(SceneObject& object, void* user) {
				auto* search = static_cast<LiveSearch*>(user);
				if (search->found) {
					return;
				}
				AnimatorStateMachine* component = object.GetComponent<AnimatorStateMachine>().Get();
				if (component && component->GetController().Get() == search->wanted && component->GetCurrentState(0) >= 0) {
					search->found = component;
				}
			}

		} // anonymous namespace

		AnimatorStateMachine* FindPlayingComponent(const AnimatorController* controller) {
			if (!controller) {
				return nullptr;
			}

			Object* active = EditorSelection::GetActive().Get();
			if (active && TypeRegistry::IsDerivedFrom(active->GetType(), SceneObject::GetReflectType())) {
				auto* selected = static_cast<SceneObject*>(active);
				AnimatorStateMachine* own = selected->GetComponent<AnimatorStateMachine>().Get();
				if (own && own->GetController().Get() == controller && own->GetCurrentState(0) >= 0) {
					return own;
				}
			}

			LiveSearch search;
			search.wanted = controller;
			for (const Handle<Scene>& scene : SceneSystem::GetLoadedScenes()) {
				if (Scene* live = scene.Get()) {
					live->ForEachObject(&VisitForController, &search);
				}
				if (search.found) {
					break;
				}
			}
			return search.found;
		}

		void RegisterAnimatorOpener(const void* owner) {
			AssetOpeners::Register(
				".controller",
				[](const Guid& asset) {
					EditorSelection::SelectAsset(asset);
					PanelManager::FocusPanelOfType(AnimationEditorImpl::AnimatorGraphPanel::GetReflectType()->name);
				},
				owner);
		}

	} // namespace AnimationEditor

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::AnimationEditorImpl::AnimatorGraphPanel)
