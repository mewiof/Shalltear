#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"

#include <functional>
#include <span>
#include <string_view>

namespace Shalltear {
	class FontAsset;
	class VectorAsset;
} // namespace Shalltear

namespace Shalltear::UI {

	class Context;
	struct DockManagerImpl;

	enum class DockZone : u8 {
		None = 0,
		Center,
		Left,
		Right,
		Top,
		Bottom
	};

	enum class DockSplit : u8 {
		None = 0,
		Horizontal,
		Vertical
	};

	inline constexpr u32 INVALID_DOCK_NODE = 0xFFFFFFFFu;

	struct DockDropTarget {
		u32 node = INVALID_DOCK_NODE;
		DockZone zone = DockZone::None;
	};

	using DockBuildFn = std::function<void(Context& context, ElementHandle parent)>;

	struct DockTabIcon {
		Handle<VectorAsset> image{};
		f32 sizePx = 0.0f;
	};

	using DockDetachFn = std::function<void(StringHash panelId, Vec2I screenPosPx)>;

	using DockPanelCloseFn = std::function<void(StringHash panelId)>;

	struct DockNodeDesc {
		DockSplit split = DockSplit::None;
		f32 ratio = 0.5f;
		Vector<StringHash> tabs{GetMemoryResource(MemTag::UI)};
		u32 activeTab = 0;
		i32 childA = -1;
		i32 childB = -1;
	};

	class SHALLTEAR_CORE_API DockManager {
	public:
		DockManager(Context& context, Handle<FontAsset> labelFont);
		~DockManager();
		DockManager(const DockManager&) = delete;
		DockManager& operator=(const DockManager&) = delete;

		static void EnsureSkin(Context& context);
		static std::string_view GetDefaultSkinSource();

		void RegisterPanel(StringHash id, std::string_view title, DockBuildFn build, const DockTabIcon& icon = {});
		void SetPanelTitle(StringHash id, std::string_view title);
		// required before a panel instance dies
		void UnregisterPanel(StringHash id);
		bool HasPanel(StringHash id) const;
		bool IsPanelDocked(StringHash id) const;
		void GetDockedPanels(Vector<StringHash>& out) const;

		void Reset();
		void DockPanel(StringHash id, u32 targetNode, DockZone zone);
		void UndockPanel(StringHash id);
		u32 GetRootNode() const;
		u32 FindNodeOfPanel(StringHash id) const;
		bool ActivatePanel(StringHash id);
		StringHash GetActivePanel(u32 node) const;

		void SaveLayout(Vector<DockNodeDesc>& out) const;
		bool RestoreLayout(std::span<const DockNodeDesc> layout);

		void UpdateInteraction(Vec2 mousePos, Vec2I screenPosPx, bool leftDown, bool leftPressed);
		void Build(ElementHandle parent);

		DockDropTarget GetDropTargetAt(Vec2 mousePos) const;
		void ShowExternalOverlay(Vec2 mousePos);
		void HideExternalOverlay();

		bool IsDraggingTab() const;
		StringHash GetDraggedPanel() const;
		void SetDetachCallback(DockDetachFn callback);
		void SetPanelCloseCallback(DockPanelCloseFn callback);

		void SetTabBarClass(StringHash extraClass);

		void SetFocusedPanel(StringHash id);
		StringHash GetFocusedPanel() const;
	private:
		DockManagerImpl* _impl = nullptr;
	};

} // namespace Shalltear::UI
