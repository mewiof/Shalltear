#include "UI/UIInternal.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Profiler.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/Widgets.h"

#include <algorithm>
#include <cmath>

namespace Shalltear::UI {

	namespace {

		constexpr const char* DOCK_DEFAULT_SKIN = R"css(
.dock-leaf {
	display: flex;
	flex-direction: column;
	width: 100%;
	height: 100%;
	flex-grow: 1;
	background-color: var(--slt-surface);
	border-radius: var(--slt-radius-lg);
	overflow: hidden;
	box-shadow: inset 0 0 0 1 var(--slt-edge-light);
}
.dock-tab-bar {
	width: 100%;
	background-color: var(--slt-well);
	display: flex;
	overflow: scroll;
	flex-shrink: 0;
	border-radius: var(--slt-radius-lg) var(--slt-radius-lg) 0 0;
	box-shadow: inset 0 0 0 1 var(--slt-edge-light), inset 0 -1 0 var(--slt-edge-dark);
}
.dock-tab {
	padding: 6 8 6 8;
	flex-direction: row;
	align-items: center;
	flex-shrink: 0;
	border-radius: var(--slt-radius-tab) var(--slt-radius-tab) 0 0;
	cursor: hand;
}
.dock-tab:hover {
	background-color: var(--slt-tab-hover);
}
.dock-tab:active {
	background-color: var(--slt-tab-pressed);
}
.dock-tab-label {
	text-align-vertical: center;
	text-align: center;
}
.dock-tab-icon {
	margin: 0 var(--slt-tab-icon-gap) 0 0;
	flex-shrink: 0;
}
.dock-active-tab {
	background-color: var(--slt-surface);
	box-shadow:
		inset 1px 0 0 0 var(--slt-edge-light),
		inset 0 1px 0 0 var(--slt-edge-light),
		inset -1px 0 0 0 var(--slt-edge-light);
}
.dock-focused-tab {
	box-shadow:
		inset 1px 0 0 0 var(--slt-edge-light),
		inset 0 1px 0 0 var(--slt-accent),
		inset -1px 0 0 0 var(--slt-edge-light);
}
.dock-splitter {
	background-color: var(--slt-splitter);
}
)css";

		f32 TabLabelSize(const Context& context) {
			return context.GetStyleVarFloat("--slt-tab-font-size"_hash, 14.0f);
		}

		constexpr f32 MIN_DOCK_PANE_PX = 80.0f;
		constexpr f32 DRAG_THRESHOLD_SQ = 25.0f;

		struct TriggerRects {
			Rect center, left, right, top, bottom;
		};

		constexpr f32 TRIGGER_MARGIN = 12.0f;

		TriggerRects CalculateTriggerRects(const Rect& rect, f32 scale) {
			f32 size = (rect.w < 100.0f * scale || rect.h < 100.0f * scale) ? 20.0f * scale : 36.0f * scale;
			f32 middleX = rect.x + rect.w * 0.5f;
			f32 middleY = rect.y + rect.h * 0.5f;
			f32 half = size * 0.5f;
			f32 margin = TRIGGER_MARGIN * scale;

			TriggerRects triggers;
			triggers.center = {middleX - half, middleY - half, size, size};
			triggers.left = {rect.x + margin, middleY - half, size, size};
			triggers.right = {rect.x + rect.w - size - margin, middleY - half, size, size};
			triggers.top = {middleX - half, rect.y + margin, size, size};
			triggers.bottom = {middleX - half, rect.y + rect.h - size - margin, size, size};
			return triggers;
		}

		DockZone ZoneFromTriggers(const Rect& rect, Vec2 point, f32 scale) {
			if (!rect.Contains(point)) {
				return DockZone::None;
			}
			TriggerRects triggers = CalculateTriggerRects(rect, scale);
			if (triggers.center.Contains(point)) {
				return DockZone::Center;
			}
			if (triggers.left.Contains(point)) {
				return DockZone::Left;
			}
			if (triggers.right.Contains(point)) {
				return DockZone::Right;
			}
			if (triggers.top.Contains(point)) {
				return DockZone::Top;
			}
			if (triggers.bottom.Contains(point)) {
				return DockZone::Bottom;
			}
			return DockZone::None;
		}

	} // anonymous namespace

	struct DockPanelEntry {
		StringHash id;
		str title;
		DockBuildFn build;
		Handle<VectorAsset> iconImage;
		f32 iconSizePx = 0.0f;

		ElementHandle contentElement;
	};

	struct DockNode {
		u32 id = INVALID_DOCK_NODE;
		u32 parent = INVALID_DOCK_NODE;
		u32 children[2] = {INVALID_DOCK_NODE, INVALID_DOCK_NODE};
		DockSplit split = DockSplit::None;
		f32 ratio = 0.5f;

		Vector<StringHash> tabs{GetMemoryResource(MemTag::UI)};
		u32 activeTab = 0;

		Rect rect{};
		ElementHandle element;
		ElementHandle contentElement;
		ElementHandle splitterElement;
		Vector<ElementHandle> tabElements{GetMemoryResource(MemTag::UI)};
		ElementHandle hostElement;

		bool dirty = true;
	};

	struct DockManagerImpl {
		Context* context = nullptr;
		Handle<FontAsset> labelFont;

		f32 TriggerScale() const { return std::max(0.001f, context->GetContentScale()); }

		Vector<DockPanelEntry> panels{GetMemoryResource(MemTag::UI)};
		Vector<DockNode> nodes{GetMemoryResource(MemTag::UI)};
		u32 rootId = 0;
		u32 nextNodeId = 1;

		ElementHandle rootElement;

		bool pendingTabDrag = false;
		bool draggingTab = false;
		StringHash draggedPanel{};
		u32 hoveredNodeId = INVALID_DOCK_NODE;
		u32 targetNodeId = INVALID_DOCK_NODE;
		DockZone targetZone = DockZone::None;
		Vec2 dragStartMouse{};
		Vec2 lastMouse{};
		Vec2I lastMouseScreen{};

		ElementHandle dragOverlay;
		ElementHandle dragTabGhost;
		Vector<ElementHandle> triggerVisuals{GetMemoryResource(MemTag::UI)};

		struct DragVisualKey {
			bool draggingTab = false;
			bool externalHovering = false;
			bool containsDragged = false;
			u32 hoveredNodeId = INVALID_DOCK_NODE;
			u32 targetNodeId = INVALID_DOCK_NODE;
			DockZone targetZone = DockZone::None;
			Rect area{};
			Rect rootArea{};
			Vec2 origin{};
			f32 scale = 0.0f;
			bool operator==(const DragVisualKey&) const = default;
		};
		DragVisualKey dragVisualKey;
		bool dragVisualsBuilt = false;

		struct GhostKey {
			StringHash panel{};
			u32 parent = INVALID_UI_INDEX;
			f32 scale = 0.0f;
			str title;
			bool operator==(const GhostKey&) const = default;
		};
		GhostKey ghostKey;
		Vec2 overlayOrigin{};

		bool externalHovering = false;

		struct DropTarget {
			u32 hovered = INVALID_DOCK_NODE;
			u32 node = INVALID_DOCK_NODE;
			DockZone zone = DockZone::None;
		};
		DropTarget externalTarget;

		DockDetachFn detachCallback;
		DockPanelCloseFn closeCallback;

		StringHash tabBarClass;

		StringHash focusedPanel;

		DockNode* GetNode(u32 id) {
			for (DockNode& node : nodes) {
				if (node.id == id) {
					return &node;
				}
			}
			return nullptr;
		}
		const DockNode* GetNode(u32 id) const {
			return const_cast<DockManagerImpl*>(this)->GetNode(id);
		}
		const DockPanelEntry* GetPanel(StringHash id) const {
			for (const DockPanelEntry& panel : panels) {
				if (panel.id == id) {
					return &panel;
				}
			}
			return nullptr;
		}
		DockPanelEntry* GetPanel(StringHash id) {
			for (DockPanelEntry& panel : panels) {
				if (panel.id == id) {
					return &panel;
				}
			}
			return nullptr;
		}
		u32 AllocateNode() {
			for (u32 i = 0; i < nodes.size(); ++i) {
				if (nodes[i].id == INVALID_DOCK_NODE) {
					nodes[i] = DockNode{};
					nodes[i].id = nextNodeId++;
					return i;
				}
			}
			DockNode node;
			node.id = nextNodeId++;
			nodes.push_back(std::move(node));
			return static_cast<u32>(nodes.size() - 1);
		}

		Rect LiveRect(const DockNode& node) const {
			Rect rect = context->IsAlive(node.element) ? context->GetLayout(node.element).content : Rect{};
			return rect.w > 0.0f && rect.h > 0.0f ? rect : node.rect;
		}

		DropTarget DropTargetAt(Vec2 point, StringHash dragged) const {
			DropTarget target;
			const f32 scale = TriggerScale();
			for (const DockNode& node : nodes) {
				if (node.id == INVALID_DOCK_NODE || node.split != DockSplit::None || !context->IsAlive(node.element)) {
					continue;
				}
				const Rect rect = LiveRect(node);
				if (!rect.Contains(point)) {
					continue;
				}
				target.hovered = node.id;
				DockZone zone = ZoneFromTriggers(rect, point, scale);
				if (zone == DockZone::Center && dragged.GetValue() != 0 &&
					std::find(node.tabs.begin(), node.tabs.end(), dragged) != node.tabs.end()) {
					zone = DockZone::None;
				}
				if (zone != DockZone::None) {
					target.node = node.id;
					target.zone = zone;
					return target;
				}
				break;
			}
			const DockNode* root = GetNode(rootId);
			const Rect area = DockAreaRect();
			if (root && root->split != DockSplit::None && area.Contains(point)) {
				const f32 left = point.x - area.x;
				const f32 right = area.x + area.w - point.x;
				const f32 top = point.y - area.y;
				const f32 bottom = area.y + area.h - point.y;
				const f32 nearest = std::min(std::min(left, right), std::min(top, bottom));
				if (nearest < TRIGGER_MARGIN * scale) {
					target.node = rootId;
					target.zone = nearest == left	 ? DockZone::Left
								  : nearest == right ? DockZone::Right
								  : nearest == top	 ? DockZone::Top
													 : DockZone::Bottom;
				}
			}
			return target;
		}

		Rect DockAreaRect() const {
			if (context->IsAlive(rootElement)) {
				Rect area = context->GetLayout(rootElement).content;
				if (area.w > 0.0f && area.h > 0.0f) {
					return area;
				}
			}
			Vec2I size = context->GetSize();
			return {0.0f, 0.0f, static_cast<f32>(size.x), static_cast<f32>(size.y)};
		}

		void DiscardBuild() {
			if (context->IsAlive(rootElement)) {
				context->DestroyElement(rootElement);
			}
			rootElement = {};
			for (DockPanelEntry& panel : panels) {
				panel.contentElement = {};
			}
		}

		static StringHash FocusedTabClass() { return StringHash("dock-focused-tab"); }

		void ApplyFocusedTabClass() {
			Context& ctx = *context;
			const StringHash klass = FocusedTabClass();
			for (DockNode& node : nodes) {
				if (node.id == INVALID_DOCK_NODE) {
					continue;
				}
				const u32 count = std::min(static_cast<u32>(node.tabElements.size()),
										   static_cast<u32>(node.tabs.size()));
				for (u32 i = 0; i < count; ++i) {
					ElementHandle tab = node.tabElements[i];
					if (!ctx.IsAlive(tab)) {
						continue;
					}
					if (focusedPanel.GetValue() != 0 && node.tabs[i] == focusedPanel) {
						ctx.AddClass(tab, klass);
					} else {
						ctx.RemoveClass(tab, klass);
					}
				}
			}
		}

		void Reset();
		void CleanupNodes(u32 specificNodeId);
		void DockPanelAt(StringHash id, u32 targetNodeId, DockZone zone);
		void UndockPanel(StringHash id);
		void OpenTabMenu(StringHash panelId);
		void BuildRecursive(u32 nodeId, ElementHandle parent);
		void RefreshDirtyNodes(u32 nodeId);
		void TryParkPanel(DockPanelEntry& panel);
		void ParkPanelContent(u32 nodeId, bool ancestorDirty);
		void SweepUnadoptedContent();
		void BuildLeaf(DockNode& node, ElementHandle parent);
		void BuildSplit(DockNode& node, ElementHandle parent);
		void Build(ElementHandle parent);
		void UpdateInteraction(Vec2 mousePos, Vec2I screenPos, bool leftDown, bool leftPressed);
		ElementHandle DrawZoneOverlay(ElementHandle parent, const Rect& rect, DockZone zone);
		void DrawTriggerVisuals(ElementHandle parent, const TriggerRects& triggers, DockZone activeZone, bool hideCenter);
	};

	void DockManagerImpl::Reset() {
		nodes.clear();
		DiscardBuild();
		u32 rootIndex = AllocateNode();
		rootId = nodes[rootIndex].id;
	}

	void DockManagerImpl::CleanupNodes(u32 specificNodeId) {
		bool changed = true;
		while (changed) {
			changed = false;
			for (DockNode& node : nodes) {
				if (node.id == INVALID_DOCK_NODE || node.split != DockSplit::None || !node.tabs.empty() || node.id == rootId) {
					continue;
				}
				if (specificNodeId != INVALID_DOCK_NODE && node.id != specificNodeId) {
					continue;
				}
				DockNode* parent = GetNode(node.parent);
				if (!parent || parent->split == DockSplit::None) {
					continue;
				}
				u32 otherId = parent->children[0] == node.id ? parent->children[1] : parent->children[0];
				DockNode* other = GetNode(otherId);
				if (!other) {
					continue;
				}
				parent->split = other->split;
				parent->ratio = other->ratio;
				parent->tabs = other->tabs;
				parent->activeTab = other->activeTab;
				parent->children[0] = other->children[0];
				parent->children[1] = other->children[1];
				if (parent->split != DockSplit::None) {
					if (DockNode* child = GetNode(parent->children[0])) {
						child->parent = parent->id;
					}
					if (DockNode* child = GetNode(parent->children[1])) {
						child->parent = parent->id;
					}
				}
				node.id = INVALID_DOCK_NODE;
				other->id = INVALID_DOCK_NODE;
				parent->dirty = true;
				changed = true;
				break;
			}
			if (specificNodeId != INVALID_DOCK_NODE) {
				break;
			}
		}
	}

	void DockManagerImpl::UndockPanel(StringHash id) {
		for (DockNode& node : nodes) {
			if (node.id == INVALID_DOCK_NODE) {
				continue;
			}
			auto it = std::find(node.tabs.begin(), node.tabs.end(), id);
			if (it != node.tabs.end()) {
				node.tabs.erase(it);
				if (node.activeTab >= node.tabs.size() && !node.tabs.empty()) {
					node.activeTab = static_cast<u32>(node.tabs.size()) - 1;
				}
				node.dirty = true;
				if (node.tabs.empty() && node.id != rootId) {
					CleanupNodes(node.id);
				}
				return;
			}
		}
	}

	void DockManagerImpl::DockPanelAt(StringHash id, u32 targetNodeId, DockZone zone) {
		if (targetNodeId == INVALID_DOCK_NODE) {
			targetNodeId = rootId;
		}

		DockNode* sourceNode = nullptr;
		for (DockNode& node : nodes) {
			if (node.id != INVALID_DOCK_NODE &&
				std::find(node.tabs.begin(), node.tabs.end(), id) != node.tabs.end()) {
				sourceNode = &node;
				break;
			}
		}
		if (sourceNode && sourceNode->id == targetNodeId && zone == DockZone::Center) {
			auto it = std::find(sourceNode->tabs.begin(), sourceNode->tabs.end(), id);
			sourceNode->activeTab = static_cast<u32>(std::distance(sourceNode->tabs.begin(), it));
			sourceNode->dirty = true;
			return;
		}
		if (sourceNode && sourceNode->id == targetNodeId && sourceNode->tabs.size() == 1) {
			return;
		}

		StringHash targetFirstTab{};
		if (DockNode* initialTarget = GetNode(targetNodeId)) {
			if (!initialTarget->tabs.empty()) {
				targetFirstTab = initialTarget->tabs[0];
			}
		}

		UndockPanel(id);

		DockNode* target = GetNode(targetNodeId);
		if (!target && targetFirstTab.GetValue() != 0) {
			for (DockNode& node : nodes) {
				if (node.id != INVALID_DOCK_NODE && node.split == DockSplit::None &&
					std::find(node.tabs.begin(), node.tabs.end(), targetFirstTab) != node.tabs.end()) {
					target = &node;
					break;
				}
			}
		}
		if (!target) {
			target = GetNode(rootId);
		}
		if (!target) {
			return;
		}

		if (target->split == DockSplit::None && target->tabs.empty()) {
			zone = DockZone::Center;
		}
		if (zone == DockZone::Center || zone == DockZone::None) {
			DockNode* leaf = target;
			while (leaf && leaf->split != DockSplit::None) {
				leaf = GetNode(leaf->children[0]);
			}
			if (leaf) {
				leaf->tabs.push_back(id);
				leaf->activeTab = static_cast<u32>(leaf->tabs.size()) - 1;
				leaf->dirty = true;
			}
			return;
		}

		DockNode oldContent = *target;
		u32 parentId = target->id;
		target->tabs.clear();
		target->activeTab = 0;
		target->contentElement = {};
		target->ratio = 0.5f;

		u32 keptIndex = AllocateNode();
		u32 newIndex = AllocateNode();
		target = GetNode(parentId);
		DockNode& kept = nodes[keptIndex];
		DockNode& fresh = nodes[newIndex];

		kept.tabs = oldContent.tabs;
		kept.activeTab = oldContent.activeTab;
		kept.parent = parentId;
		kept.split = oldContent.split;
		kept.ratio = oldContent.ratio;
		kept.children[0] = oldContent.children[0];
		kept.children[1] = oldContent.children[1];
		if (kept.split != DockSplit::None) {
			if (DockNode* child = GetNode(kept.children[0])) {
				child->parent = kept.id;
			}
			if (DockNode* child = GetNode(kept.children[1])) {
				child->parent = kept.id;
			}
		}

		fresh.tabs.push_back(id);
		fresh.activeTab = 0;
		fresh.parent = parentId;

		switch (zone) {
		case DockZone::Left:
			target->split = DockSplit::Horizontal;
			target->children[0] = fresh.id;
			target->children[1] = kept.id;
			break;
		case DockZone::Right:
			target->split = DockSplit::Horizontal;
			target->children[0] = kept.id;
			target->children[1] = fresh.id;
			break;
		case DockZone::Top:
			target->split = DockSplit::Vertical;
			target->children[0] = fresh.id;
			target->children[1] = kept.id;
			break;
		case DockZone::Bottom:
			target->split = DockSplit::Vertical;
			target->children[0] = kept.id;
			target->children[1] = fresh.id;
			break;
		default:
			break;
		}
		target->dirty = true;
	}

	void DockManagerImpl::BuildSplit(DockNode& node, ElementHandle parent) {
		Context& ctx = *context;
		u32 nodeId = node.id;

		Widgets::SplitDesc desc;
		desc.axis = node.split == DockSplit::Horizontal ? FlexDirection::Row : FlexDirection::Column;
		desc.sizing = Widgets::SplitSizing::Ratio;
		desc.position = node.ratio;
		desc.minFirstPx = MIN_DOCK_PANE_PX;
		desc.minSecondPx = MIN_DOCK_PANE_PX;
		desc.onPositionChanged = [this, nodeId](f32 ratio) {
			if (DockNode* live = GetNode(nodeId)) {
				live->ratio = ratio;
			}
		};

		Widgets::SplitParts parts = Widgets::Split(ctx, parent, desc);
		node.element = parts.root;
		node.splitterElement = parts.splitter;
		ctx.AddClass(node.splitterElement, StringHash("dock-splitter"));

		BuildRecursive(node.children[0], parts.first);
		BuildRecursive(node.children[1], parts.second);
	}

	void DockManagerImpl::OpenTabMenu(StringHash panelId) {
		Context& ctx = *context;
		Vector<Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		items.push_back({"Close", "", true, false, [this, panelId]() {
							 if (closeCallback) {
								 closeCallback(panelId);
							 } else {
								 UndockPanel(panelId);
							 }
						 }});
		Widgets::OpenMenu(ctx, ctx.GetPointerPosition(), {items.data(), items.size()}, labelFont,
						  TabLabelSize(ctx));
	}

	void DockManagerImpl::BuildLeaf(DockNode& node, ElementHandle parent) {
		Context& ctx = *context;
		u32 nodeId = node.id;

		node.element = ctx.CreateElement();
		ctx.AddClass(node.element, StringHash("dock-leaf"));
		ctx.AppendChild(parent, node.element);

		ElementHandle tabBar = ctx.CreateElement();
		ctx.AddClass(tabBar, StringHash("dock-tab-bar"));
		if (tabBarClass.GetValue() != 0) {
			ctx.AddClass(tabBar, tabBarClass);
		}
		ctx.AppendChild(node.element, tabBar);

		node.tabElements.clear();
		for (u32 i = 0; i < node.tabs.size(); ++i) {
			ElementHandle tab = ctx.CreateElement();
			ctx.AddClass(tab, StringHash("dock-tab"));
			if (i == node.activeTab) {
				ctx.AddClass(tab, StringHash("dock-active-tab"));
			}
			if (focusedPanel.GetValue() != 0 && node.tabs[i] == focusedPanel) {
				ctx.AddClass(tab, FocusedTabClass());
			}
			const DockPanelEntry* panel = GetPanel(node.tabs[i]);
			node.tabElements.push_back(tab);

			if (panel && panel->iconImage.IsValid()) {
				ElementHandle icon = ctx.CreateElement();
				ctx.AddClass(icon, StringHash("dock-tab-icon"));
				f32 iconSize = panel->iconSizePx > 0.0f ? panel->iconSizePx : TabLabelSize(ctx);
				ctx.SetIcon(icon, panel->iconImage, iconSize,
							IconPalette::Tint(ctx.GetStyleVarColor("--slt-tab-text"_hash, Color(0.9f, 0.9f, 0.9f))));
				ctx.SetHoverable(icon, false);
				ctx.AppendChild(tab, icon);
			}

			ElementHandle label = ctx.CreateElement();
			ctx.AddClass(label, StringHash("dock-tab-label"));
			ctx.SetText(label, labelFont, TabLabelSize(ctx), ctx.GetStyleVarColor("--slt-tab-text"_hash, Color(0.9f, 0.9f, 0.9f)),
						panel ? std::string_view(panel->title) : std::string_view("?"));
			ctx.SetHoverable(label, false);
			ctx.AppendChild(tab, label);

			StringHash panelId = node.tabs[i];
			ctx.Events(tab).onMouseDown = [this, nodeId, i, panelId]() {
				DockNode* liveNode = GetNode(nodeId);
				if (liveNode && liveNode->activeTab != i) {
					liveNode->activeTab = i;
					liveNode->dirty = true;
				}
				if (focusedPanel != panelId) {
					focusedPanel = panelId;
					ApplyFocusedTabClass();
				}
				pendingTabDrag = true;
				draggedPanel = panelId;
				dragStartMouse = lastMouse;
			};

			ctx.Events(tab).onRightClick = [this, panelId]() {
				OpenTabMenu(panelId);
			};

			ctx.AppendChild(tabBar, tab);
		}

		DockPanelEntry* active = node.activeTab < node.tabs.size() ? GetPanel(node.tabs[node.activeTab]) : nullptr;
		if (active && ctx.IsAlive(active->contentElement)) {
			node.contentElement = active->contentElement;
			ctx.AppendChild(node.element, node.contentElement);
			return;
		}

		node.contentElement = ctx.CreateElement();
		Style contentStyle;
		contentStyle.flexDirection = FlexDirection::Column;
		contentStyle.flexGrow = 1.0f;
		contentStyle.width = Length::Percent(1.0f);
		ctx.SetStyle(node.contentElement, contentStyle);
		ctx.AppendChild(node.element, node.contentElement);

		if (active) {
			active->contentElement = node.contentElement;
			if (active->build) {
				active->build(ctx, node.contentElement);
			}
		}
	}

	void DockManagerImpl::BuildRecursive(u32 nodeId, ElementHandle parent) {
		DockNode* node = GetNode(nodeId);
		if (!node) {
			return;
		}
		node->hostElement = parent;
		node->dirty = false;
		if (node->split == DockSplit::None) {
			BuildLeaf(*node, parent);
		} else {
			BuildSplit(*node, parent);
		}
	}

	void DockManagerImpl::TryParkPanel(DockPanelEntry& panel) {
		Context& ctx = *context;
		if (!ctx.IsAlive(panel.contentElement)) {
			return;
		}
		for (const DockNode& node : nodes) {
			if (node.id == INVALID_DOCK_NODE || node.split != DockSplit::None) {
				continue;
			}
			if (node.activeTab < node.tabs.size() && node.tabs[node.activeTab] == panel.id) {
				ctx.AppendChild(ctx.GetRoot(), panel.contentElement);
				return;
			}
		}
	}

	void DockManagerImpl::ParkPanelContent(u32 nodeId, bool ancestorDirty) {
		Context& ctx = *context;
		DockNode* node = GetNode(nodeId);
		if (!node) {
			return;
		}
		const bool dirty = ancestorDirty || node->dirty || !ctx.IsAlive(node->element);
		if (dirty) {
			if (ctx.IsAlive(node->contentElement)) {
				for (DockPanelEntry& panel : panels) {
					if (panel.contentElement == node->contentElement) {
						TryParkPanel(panel);
						break;
					}
				}
			}
			if (node->activeTab < node->tabs.size()) {
				if (DockPanelEntry* active = GetPanel(node->tabs[node->activeTab])) {
					TryParkPanel(*active);
				}
			}
		}
		if (node->split != DockSplit::None) {
			u32 first = node->children[0];
			u32 second = node->children[1];
			ParkPanelContent(first, dirty);
			ParkPanelContent(second, dirty);
		}
	}

	void DockManagerImpl::SweepUnadoptedContent() {
		Context& ctx = *context;
		const u32 rootIndex = ctx.GetRoot().index;
		for (DockPanelEntry& panel : panels) {
			if (!ctx.IsAlive(panel.contentElement)) {
				panel.contentElement = {};
				continue;
			}
			const Element* pod = ctx.GetElement(panel.contentElement);
			if (pod && pod->parent == rootIndex) {
				ctx.DestroyElement(panel.contentElement);
				panel.contentElement = {};
			}
		}
	}

	void DockManagerImpl::RefreshDirtyNodes(u32 nodeId) {
		Context& ctx = *context;
		DockNode* node = GetNode(nodeId);
		if (!node) {
			return;
		}
		if (node->dirty || !ctx.IsAlive(node->element)) {
			ElementHandle host = node->hostElement;
			if (!ctx.IsAlive(host)) {
				return;
			}
			if (ctx.IsAlive(node->element)) {
				ctx.DestroyElement(node->element);
			}
			BuildRecursive(nodeId, host);
			return;
		}
		if (node->split != DockSplit::None) {
			u32 first = node->children[0];
			u32 second = node->children[1];
			RefreshDirtyNodes(first);
			RefreshDirtyNodes(second);
		}
	}

	ElementHandle DockManagerImpl::DrawZoneOverlay(ElementHandle parent, const Rect& rect, DockZone zone) {
		Context& ctx = *context;
		Rect drawRect = rect;
		switch (zone) {
		case DockZone::Center:
			break;
		case DockZone::Left:
			drawRect.w *= 0.5f;
			break;
		case DockZone::Right:
			drawRect.w *= 0.5f;
			drawRect.x += drawRect.w;
			break;
		case DockZone::Top:
			drawRect.h *= 0.5f;
			break;
		case DockZone::Bottom:
			drawRect.h *= 0.5f;
			drawRect.y += drawRect.h;
			break;
		default:
			return {};
		}

		f32 scale = std::max(0.001f, ctx.GetContentScale());
		Style overlay;
		overlay.position = Position::Absolute;
		overlay.background = Brush::SolidColor(ctx.GetStyleVarColor("--slt-zone-fill"_hash, Color(0.0f, 0.47f, 0.84f, 0.3f)));
		overlay.pointerEvents = false;
		overlay.zIndex = 100;
		overlay.borderColor = ctx.GetStyleVarColor("--slt-zone-border"_hash, Color(0.25f, 0.62f, 0.95f, 0.9f));
		overlay.borderWidth = Edges::All(1.0f);
		overlay.left = Length::Pixel((drawRect.x - overlayOrigin.x) / scale);
		overlay.top = Length::Pixel((drawRect.y - overlayOrigin.y) / scale);
		overlay.width = Length::Pixel(drawRect.w / scale);
		overlay.height = Length::Pixel(drawRect.h / scale);

		ElementHandle handle = ctx.CreateElement();
		ctx.SetStyle(handle, overlay);
		ctx.AppendChild(parent, handle);
		return handle;
	}

	void DockManagerImpl::DrawTriggerVisuals(ElementHandle parent, const TriggerRects& triggers, DockZone activeZone, bool hideCenter) {
		Context& ctx = *context;
		f32 scale = std::max(0.001f, ctx.GetContentScale());
		auto drawBox = [&](const Rect& rect, DockZone zone, const char* label) {
			ElementHandle box = ctx.CreateElement();
			triggerVisuals.push_back(box);

			Style style;
			style.position = Position::Absolute;
			style.left = Length::Pixel((rect.x - overlayOrigin.x) / scale);
			style.top = Length::Pixel((rect.y - overlayOrigin.y) / scale);
			style.width = Length::Pixel(rect.w / scale);
			style.height = Length::Pixel(rect.h / scale);
			style.borderRadius = Radii::All(ctx.GetStyleVarFloat("--slt-radius"_hash, 4.0f));
			style.zIndex = 150;
			style.textAlign = Align::Center;
			style.textAlignVertical = Align::Center;
			if (zone == activeZone) {
				style.background = Brush::SolidColor(ctx.GetStyleVarColor("--slt-zone-trigger-active"_hash, Color(0.3f, 0.7f, 1.0f, 0.9f)));
				style.borderColor = Color::White();
				style.borderWidth = Edges::All(1.0f);
			} else {
				style.background = Brush::SolidColor(ctx.GetStyleVarColor("--slt-zone-trigger"_hash, Color(0.15f, 0.15f, 0.17f, 0.8f)));
				style.borderColor = ctx.GetStyleVarColor("--slt-zone-trigger-border"_hash, Color(0.5f, 0.5f, 0.5f, 0.5f));
				style.borderWidth = Edges::All(1.0f);
			}
			ctx.SetStyle(box, style);
			ctx.SetText(box, labelFont, 16.0f, Color::White(), label);
			ctx.AppendChild(parent, box);
		};

		if (!hideCenter) {
			drawBox(triggers.center, DockZone::Center, "+");
		}
		drawBox(triggers.left, DockZone::Left, "\xE2\x86\x90");
		drawBox(triggers.right, DockZone::Right, "\xE2\x86\x92");
		drawBox(triggers.top, DockZone::Top, "\xE2\x86\x91");
		drawBox(triggers.bottom, DockZone::Bottom, "\xE2\x86\x93");
	}

	void DockManagerImpl::Build(ElementHandle parent) {
		Context& ctx = *context;

		overlayOrigin = {ctx.GetLayout(parent).paddingBox.x, ctx.GetLayout(parent).paddingBox.y};

		if (!ctx.IsAlive(rootElement)) {
			rootElement = ctx.CreateElement();
			ctx.AppendChild(parent, rootElement);
			Style rootStyle;
			rootStyle.width = Length::Percent(1.0f);
			rootStyle.height = Length::Percent(1.0f);
			rootStyle.flexGrow = 1.0f;
			ctx.SetStyle(rootElement, rootStyle);
			BuildRecursive(rootId, rootElement);
		} else {
			const Element* rootPod = ctx.GetElement(rootElement);
			if (rootPod && rootPod->parent != parent.index) {
				ctx.AppendChild(parent, rootElement);
			}
			ParkPanelContent(rootId, false);
			RefreshDirtyNodes(rootId);
		}
		SweepUnadoptedContent();

		f32 scale = std::max(0.001f, ctx.GetContentScale());

		DragVisualKey key;
		key.draggingTab = draggingTab;
		key.externalHovering = externalHovering;
		key.origin = overlayOrigin;
		key.scale = scale;
		DropTarget shown;
		if (draggingTab) {
			shown = {hoveredNodeId, targetNodeId, targetZone};
		} else if (externalHovering) {
			shown = externalTarget;
		}
		const DockNode* hovered = (draggingTab || externalHovering) ? GetNode(shown.hovered) : nullptr;
		if (hovered) {
			key.hoveredNodeId = hovered->id;
			key.area = LiveRect(*hovered);
			key.containsDragged = draggingTab &&
								  std::find(hovered->tabs.begin(), hovered->tabs.end(), draggedPanel) != hovered->tabs.end();
		}
		key.targetNodeId = shown.node;
		key.targetZone = shown.zone;
		if (shown.node == rootId) {
			key.rootArea = DockAreaRect();
		}

		bool visualsAlive = ctx.IsAlive(dragOverlay) || !dragOverlay.IsValid();
		for (ElementHandle visual : triggerVisuals) {
			visualsAlive = visualsAlive && ctx.IsAlive(visual);
		}

		if (!dragVisualsBuilt || !(key == dragVisualKey) || !visualsAlive) {
			if (dragOverlay.IsValid()) {
				ctx.DestroyElement(dragOverlay);
				dragOverlay = {};
			}
			for (ElementHandle visual : triggerVisuals) {
				ctx.DestroyElement(visual);
			}
			triggerVisuals.clear();

			if (hovered) {
				DrawTriggerVisuals(parent, CalculateTriggerRects(key.area, TriggerScale()),
								   shown.node == hovered->id ? shown.zone : DockZone::None, key.containsDragged);
			}
			if (shown.zone != DockZone::None) {
				if (shown.node == rootId) {
					dragOverlay = DrawZoneOverlay(parent, key.rootArea, shown.zone);
				} else if (hovered && shown.node == hovered->id) {
					dragOverlay = DrawZoneOverlay(parent, key.area, shown.zone);
				}
			}
			dragVisualKey = key;
			dragVisualsBuilt = true;
		}

		if (draggingTab) {
			const DockPanelEntry* panel = GetPanel(draggedPanel);
			const GhostKey key{draggedPanel, parent.index, scale, panel ? panel->title : str("?")};
			if (!ctx.IsAlive(dragTabGhost) || key != ghostKey) {
				if (ctx.IsAlive(dragTabGhost)) {
					ctx.DestroyElement(dragTabGhost);
				}
				dragTabGhost = ctx.CreateElement();
				ctx.AppendChild(parent, dragTabGhost);
				ctx.AddClass(dragTabGhost, StringHash("dock-tab-ghost"));
				ctx.SetText(dragTabGhost, labelFont, TabLabelSize(ctx), Color::White(), std::string_view(key.title));
				ghostKey = key;
			}

			// ghost must stay parent's last child
			const Element* parentPod = ctx.GetElement(parent);
			if (parentPod && parentPod->lastChild != dragTabGhost.index) {
				ctx.AppendChild(parent, dragTabGhost);
			}

			Style ghost;
			ghost.position = Position::Absolute;
			ghost.background = Brush::SolidColor(ctx.GetStyleVarColor("--slt-tab-ghost"_hash, Color(0.2f, 0.2f, 0.25f, 0.95f)));
			ghost.pointerEvents = false;
			ghost.zIndex = 200;
			ghost.left = Length::Pixel((lastMouse.x + 12.0f - overlayOrigin.x) / scale);
			ghost.top = Length::Pixel((lastMouse.y + 12.0f - overlayOrigin.y) / scale);
			ghost.padding = {12.0f, 6.0f, 12.0f, 6.0f};
			ghost.borderRadius = Radii::All(ctx.GetStyleVarFloat("--slt-radius"_hash, 4.0f));
			ghost.borderColor = ctx.GetStyleVarColor("--slt-ghost-border"_hash, Color(0.4f, 0.4f, 0.45f, 1.0f));
			ghost.borderWidth = Edges::All(1.0f);
			ctx.SetStyle(dragTabGhost, ghost);
		} else if (dragTabGhost.IsValid()) {
			if (ctx.IsAlive(dragTabGhost)) {
				ctx.DestroyElement(dragTabGhost);
			}
			dragTabGhost = {};
			ghostKey = {};
		}
	}

	void DockManagerImpl::UpdateInteraction(Vec2 mousePos, Vec2I screenPos, bool leftDown, bool leftPressed) {
		Context& ctx = *context;
		lastMouse = mousePos;
		lastMouseScreen = screenPos;
		(void)leftPressed;

		for (DockNode& node : nodes) {
			if (node.id != INVALID_DOCK_NODE && ctx.IsAlive(node.element)) {
				node.rect = ctx.GetLayout(node.element).content;
			}
		}

		if (pendingTabDrag) {
			if (!leftDown) {
				pendingTabDrag = false;
			} else {
				f32 dx = mousePos.x - dragStartMouse.x;
				f32 dy = mousePos.y - dragStartMouse.y;
				if (dx * dx + dy * dy > DRAG_THRESHOLD_SQ) {
					pendingTabDrag = false;
					draggingTab = true;
				}
			}
		}

		if (!draggingTab) {
			return;
		}

		if (!leftDown) {
			if (targetNodeId != INVALID_DOCK_NODE && targetZone != DockZone::None) {
				DockPanelAt(draggedPanel, targetNodeId, targetZone);
			} else {
				bool overTabBar = false;
				for (const DockNode& node : nodes) {
					if (node.id == INVALID_DOCK_NODE || node.split != DockSplit::None) {
						continue;
					}
					Rect tabBarRect = {node.rect.x, node.rect.y, node.rect.w, 32.0f};
					if (!node.tabElements.empty() && ctx.IsAlive(node.tabElements[0])) {
						tabBarRect.h = ctx.GetLayout(node.tabElements[0]).borderBox.h;
					}
					if (tabBarRect.Contains(mousePos)) {
						overTabBar = true;
						break;
					}
				}
				Vec2I size = ctx.GetSize();
				bool outsideWindow = mousePos.x < 0.0f || mousePos.y < 0.0f ||
									 mousePos.x > static_cast<f32>(size.x) || mousePos.y > static_cast<f32>(size.y);
				if ((outsideWindow || !overTabBar) && detachCallback) {
					StringHash detached = draggedPanel;
					UndockPanel(detached);
					detachCallback(detached, lastMouseScreen);
				}
			}
			draggingTab = false;
			hoveredNodeId = INVALID_DOCK_NODE;
			targetNodeId = INVALID_DOCK_NODE;
			targetZone = DockZone::None;
			return;
		}

		hoveredNodeId = INVALID_DOCK_NODE;
		targetNodeId = INVALID_DOCK_NODE;
		targetZone = DockZone::None;

		bool reordered = false;
		u32 reorderNodeId = INVALID_DOCK_NODE;
		u32 reorderIndex = 0;
		bool foreignTabBar = false;

		for (const DockNode& node : nodes) {
			if (node.id == INVALID_DOCK_NODE || node.split != DockSplit::None) {
				continue;
			}
			bool hoveredAnyTab = false;
			for (u32 i = 0; i < node.tabElements.size() && i < node.tabs.size(); ++i) {
				if (!ctx.IsAlive(node.tabElements[i])) {
					continue;
				}
				Rect tabRect = ctx.GetLayout(node.tabElements[i]).borderBox;
				if (!tabRect.Contains(mousePos)) {
					continue;
				}
				hoveredAnyTab = true;
				if (node.tabs[i] == draggedPanel) {
					break;
				}
				u32 currentIndex = INVALID_UI_INDEX;
				for (u32 j = 0; j < node.tabs.size(); ++j) {
					if (node.tabs[j] == draggedPanel) {
						currentIndex = j;
						break;
					}
				}
				if (currentIndex != INVALID_UI_INDEX) {
					f32 threshold = tabRect.x + tabRect.w * 0.5f;
					if ((currentIndex < i && mousePos.x > threshold) ||
						(currentIndex > i && mousePos.x < threshold)) {
						reordered = true;
						reorderNodeId = node.id;
						reorderIndex = i;
					}
				} else {
					foreignTabBar = true;
					hoveredNodeId = node.id;
				}
				break;
			}
			if (reordered) {
				break;
			}
			if (!hoveredAnyTab) {
				Rect tabBarRect = {node.rect.x, node.rect.y, node.rect.w, 32.0f};
				if (!node.tabElements.empty() && ctx.IsAlive(node.tabElements[0])) {
					tabBarRect.h = ctx.GetLayout(node.tabElements[0]).borderBox.h;
				}
				if (tabBarRect.Contains(mousePos) &&
					std::find(node.tabs.begin(), node.tabs.end(), draggedPanel) == node.tabs.end()) {
					foreignTabBar = true;
					hoveredNodeId = node.id;
					break;
				}
			}
		}

		if (reordered) {
			for (DockNode& node : nodes) {
				if (node.id == INVALID_DOCK_NODE) {
					continue;
				}
				auto it = std::find(node.tabs.begin(), node.tabs.end(), draggedPanel);
				if (it != node.tabs.end()) {
					node.tabs.erase(it);
					if (node.activeTab >= node.tabs.size() && !node.tabs.empty()) {
						node.activeTab = static_cast<u32>(node.tabs.size()) - 1;
					}
					node.dirty = true;
					break;
				}
			}
			if (DockNode* target = GetNode(reorderNodeId)) {
				reorderIndex = std::min(reorderIndex, static_cast<u32>(target->tabs.size()));
				target->tabs.insert(target->tabs.begin() + reorderIndex, draggedPanel);
				target->activeTab = reorderIndex;
				target->dirty = true;
			}
		} else if (foreignTabBar) {
			targetNodeId = hoveredNodeId;
			targetZone = DockZone::Center;
		} else {
			const DropTarget target = DropTargetAt(mousePos, draggedPanel);
			hoveredNodeId = target.hovered;
			targetNodeId = target.node;
			targetZone = target.zone;
		}
	}

	DockManager::DockManager(Context& context, Handle<FontAsset> labelFont) {
		_impl = New<DockManagerImpl>(MemTag::UI);
		_impl->context = &context;
		_impl->labelFont = labelFont;
		_impl->Reset();
		EnsureSkin(context);
	}

	void DockManager::EnsureSkin(Context& context) {
		context.LoadStyleSheetOnce(StringHash("slt-dock"), DOCK_DEFAULT_SKIN);
	}

	std::string_view DockManager::GetDefaultSkinSource() {
		return DOCK_DEFAULT_SKIN;
	}

	DockManager::~DockManager() {
		Delete(_impl);
		_impl = nullptr;
	}

	void DockManager::RegisterPanel(StringHash id, std::string_view title, DockBuildFn build, const DockTabIcon& icon) {
		for (DockPanelEntry& panel : _impl->panels) {
			if (panel.id == id) {
				panel.title = str(title);
				panel.build = std::move(build);
				panel.iconImage = icon.image;
				panel.iconSizePx = icon.sizePx;
				return;
			}
		}
		_impl->panels.push_back({id, str(title), std::move(build), icon.image, icon.sizePx});
	}

	void DockManager::SetPanelTitle(StringHash id, std::string_view title) {
		DockPanelEntry* entry = nullptr;
		for (DockPanelEntry& panel : _impl->panels) {
			entry = panel.id == id ? &panel : entry;
		}
		if (!entry || entry->title == title) {
			return;
		}
		entry->title = str(title);
		Context& ctx = *_impl->context;
		for (const DockNode& node : _impl->nodes) {
			for (u32 i = 0; i < node.tabs.size() && i < node.tabElements.size(); ++i) {
				if (node.tabs[i] != id || !ctx.IsAlive(node.tabElements[i])) {
					continue;
				}
				const Element* tab = ctx.GetElement(node.tabElements[i]);
				for (u32 child = tab ? tab->firstChild : INVALID_UI_INDEX; child != INVALID_UI_INDEX;) {
					ElementHandle handle = ctx.MakeHandle(child);
					if (ctx.HasClass(handle, StringHash("dock-tab-label"))) {
						ctx.SetText(handle, _impl->labelFont, TabLabelSize(ctx),
									ctx.GetStyleVarColor("--slt-tab-text"_hash, Color(0.9f, 0.9f, 0.9f)), entry->title);
					}
					child = ctx.GetElement(handle)->nextSibling;
				}
			}
		}
	}

	void DockManager::UnregisterPanel(StringHash id) {
		_impl->UndockPanel(id);
		for (size_t i = 0; i < _impl->panels.size(); ++i) {
			if (_impl->panels[i].id == id) {
				_impl->panels.erase(_impl->panels.begin() + static_cast<i64>(i));
				break;
			}
		}
	}

	bool DockManager::HasPanel(StringHash id) const {
		return _impl->GetPanel(id) != nullptr;
	}

	bool DockManager::IsPanelDocked(StringHash id) const {
		for (const DockNode& node : _impl->nodes) {
			if (node.id != INVALID_DOCK_NODE &&
				std::find(node.tabs.begin(), node.tabs.end(), id) != node.tabs.end()) {
				return true;
			}
		}
		return false;
	}

	void DockManager::GetDockedPanels(Vector<StringHash>& out) const {
		for (const DockNode& node : _impl->nodes) {
			if (node.id != INVALID_DOCK_NODE && node.split == DockSplit::None) {
				out.insert(out.end(), node.tabs.begin(), node.tabs.end());
			}
		}
	}

	void DockManager::Reset() {
		_impl->Reset();
	}

	void DockManager::DockPanel(StringHash id, u32 targetNode, DockZone zone) {
		if (!_impl->GetPanel(id)) {
			SLT_LOG_WARN("UI", "DockPanel: unknown panel (hash {:x})", id.GetValue());
			return;
		}
		_impl->DockPanelAt(id, targetNode, zone);
	}

	void DockManager::UndockPanel(StringHash id) {
		_impl->UndockPanel(id);
	}

	u32 DockManager::GetRootNode() const {
		return _impl->rootId;
	}

	bool DockManager::ActivatePanel(StringHash id) {
		for (DockNode& node : _impl->nodes) {
			if (node.id == INVALID_DOCK_NODE) {
				continue;
			}
			auto it = std::find(node.tabs.begin(), node.tabs.end(), id);
			if (it == node.tabs.end()) {
				continue;
			}
			u32 index = static_cast<u32>(it - node.tabs.begin());
			if (node.activeTab != index) {
				node.activeTab = index;
				node.dirty = true;
			}
			return true;
		}
		return false;
	}

	u32 DockManager::FindNodeOfPanel(StringHash id) const {
		for (const DockNode& node : _impl->nodes) {
			if (node.id != INVALID_DOCK_NODE &&
				std::find(node.tabs.begin(), node.tabs.end(), id) != node.tabs.end()) {
				return node.id;
			}
		}
		return INVALID_DOCK_NODE;
	}

	StringHash DockManager::GetActivePanel(u32 node) const {
		if (node == INVALID_DOCK_NODE) {
			return {};
		}
		const DockNode* found = _impl->GetNode(node);
		if (!found || found->activeTab >= found->tabs.size()) {
			return {};
		}
		return found->tabs[found->activeTab];
	}

	void DockManager::SaveLayout(Vector<DockNodeDesc>& out) const {
		out.clear();

		struct Recurse {
			DockManagerImpl& impl;
			Vector<DockNodeDesc>& out;

			i32 Visit(u32 nodeId) {
				DockNode* node = impl.GetNode(nodeId);
				if (!node) {
					return -1;
				}
				size_t slot = out.size();
				out.emplace_back();
				out[slot].split = node->split;
				out[slot].ratio = node->ratio;
				out[slot].tabs = node->tabs;
				out[slot].activeTab = node->activeTab;
				if (node->split != DockSplit::None) {
					i32 childA = Visit(impl.GetNode(nodeId)->children[0]);
					out[slot].childA = childA;
					i32 childB = Visit(impl.GetNode(nodeId)->children[1]);
					out[slot].childB = childB;
				}
				return static_cast<i32>(slot);
			}
		};
		Recurse recurse{*_impl, out};
		recurse.Visit(_impl->rootId);
	}

	bool DockManager::RestoreLayout(std::span<const DockNodeDesc> layout) {
		if (layout.empty()) {
			Reset();
			return true;
		}

		Vector<u8> referenced{GetMemoryResource(MemTag::UI)};
		referenced.resize(layout.size(), 0);
		for (size_t i = 0; i < layout.size(); ++i) {
			const DockNodeDesc& desc = layout[i];
			bool isSplit = desc.split != DockSplit::None;
			i32 kids[2] = {desc.childA, desc.childB};
			for (i32 kid : kids) {
				if (isSplit) {
					if (kid <= static_cast<i32>(i) || kid >= static_cast<i32>(layout.size()) || referenced[static_cast<size_t>(kid)]) {
						SLT_LOG_WARN("UI", "RestoreLayout: malformed child reference at node {}", i);
						return false;
					}
					referenced[static_cast<size_t>(kid)] = 1;
				} else if (kid != -1) {
					SLT_LOG_WARN("UI", "RestoreLayout: leaf node {} has children", i);
					return false;
				}
			}
		}
		for (size_t i = 1; i < layout.size(); ++i) {
			if (!referenced[i]) {
				SLT_LOG_WARN("UI", "RestoreLayout: orphan node {}", i);
				return false;
			}
		}

		_impl->nodes.clear();
		_impl->DiscardBuild();
		_impl->nextNodeId = 1;
		for (size_t i = 0; i < layout.size(); ++i) {
			u32 index = _impl->AllocateNode();
			DockNode& node = _impl->nodes[index];
			const DockNodeDesc& desc = layout[i];
			node.split = desc.split;
			node.ratio = std::clamp(desc.ratio, 0.1f, 0.9f);
			if (desc.split == DockSplit::None) {
				for (StringHash tab : desc.tabs) {
					if (_impl->GetPanel(tab)) {
						node.tabs.push_back(tab);
					} else {
						SLT_LOG_WARN("UI", "RestoreLayout: dropping unknown panel (hash {:x})", tab.GetValue());
					}
				}
				node.activeTab = node.tabs.empty() ? 0 : std::min(desc.activeTab, static_cast<u32>(node.tabs.size()) - 1);
			}
		}
		_impl->rootId = _impl->nodes[0].id;
		for (size_t i = 0; i < layout.size(); ++i) {
			if (layout[i].split == DockSplit::None) {
				continue;
			}
			DockNode& node = _impl->nodes[i];
			node.children[0] = _impl->nodes[static_cast<size_t>(layout[i].childA)].id;
			node.children[1] = _impl->nodes[static_cast<size_t>(layout[i].childB)].id;
			_impl->nodes[static_cast<size_t>(layout[i].childA)].parent = node.id;
			_impl->nodes[static_cast<size_t>(layout[i].childB)].parent = node.id;
		}

		_impl->CleanupNodes(INVALID_DOCK_NODE);
		return true;
	}

	void DockManager::UpdateInteraction(Vec2 mousePos, Vec2I screenPosPx, bool leftDown, bool leftPressed) {
		_impl->UpdateInteraction(mousePos, screenPosPx, leftDown, leftPressed);
	}

	void DockManager::Build(ElementHandle parent) {
		SLT_PROFILE_ZONE("UI.DockBuild");
		_impl->Build(parent);
	}

	DockDropTarget DockManager::GetDropTargetAt(Vec2 mousePos) const {
		const DockManagerImpl::DropTarget target = _impl->DropTargetAt(mousePos, StringHash{});
		return {target.node, target.zone};
	}

	void DockManager::ShowExternalOverlay(Vec2 mousePos) {
		_impl->externalHovering = true;
		_impl->externalTarget = _impl->DropTargetAt(mousePos, StringHash{});
	}

	void DockManager::HideExternalOverlay() {
		_impl->externalHovering = false;
		_impl->externalTarget = {};
	}

	bool DockManager::IsDraggingTab() const {
		return _impl->draggingTab;
	}

	StringHash DockManager::GetDraggedPanel() const {
		return _impl->draggingTab ? _impl->draggedPanel : StringHash{};
	}

	void DockManager::SetDetachCallback(DockDetachFn callback) {
		_impl->detachCallback = std::move(callback);
	}

	void DockManager::SetPanelCloseCallback(DockPanelCloseFn callback) {
		_impl->closeCallback = std::move(callback);
	}

	void DockManager::SetTabBarClass(StringHash extraClass) {
		_impl->tabBarClass = extraClass;
	}

	void DockManager::SetFocusedPanel(StringHash id) {
		if (_impl->focusedPanel == id) {
			return;
		}
		_impl->focusedPanel = id;
		_impl->ApplyFocusedTabClass();
	}

	StringHash DockManager::GetFocusedPanel() const {
		return _impl->focusedPanel;
	}

} // namespace Shalltear::UI
