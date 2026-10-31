#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Widgets.h"

#include <span>

#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorInsertStrips.h"
#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/SceneAnnotations.h"

#include <functional>

namespace Shalltear {

	class SceneObject;
	class Scene;

	class HierarchyPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(HierarchyPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Hierarchy")
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		HierarchyPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;
		void OnOpen() override;
		void OnClose() override;
	private:
		bool BeginRenameRow(u64 instanceId);

		void GetRowTargets(Handle<SceneObject> clicked, Vector<Handle<SceneObject>>& out) const;
		static UI::Widgets::MenuItem BuildColorMenu(const Vector<Handle<SceneObject>>& targets);

		void BuildHeader(UI::Context& context);
		u64 ComputeStructureVersion() const;
		u64 ComputeSelectionVersion() const;
		void PushSelectionToList();
		void PublishSelectionFromList();
		void ClearSelection();
		void RebuildRows(UI::Context& context);
		void BuildStageBreadcrumb(UI::Context& context, Handle<Scene> stage);

		void BuildGroup(UI::Context& context, Handle<Scene> scene, FolderParent parent, u64 owningFolder,
						Handle<SceneObject> anchor, u32 depth);
		void BuildFolderRow(UI::Context& context, Handle<Scene> scene, const FolderInfo& folder,
							Handle<SceneObject> anchor, u32 depth);
		void BuildObjectRow(UI::Context& context, Handle<SceneObject> object, u32 depth);
		void BuildMatchingRows(UI::Context& context, Handle<Scene> scene);
		void PendInsertStrip(UI::Context& context, Handle<Scene> scene, FolderParent parent, u64 owningFolder,
							 Handle<SceneObject> anchor, u32 slot, u32 order);
		void PushRow(ListRow&& row);

		void OpenCreateMenu(UI::Context& context, Handle<Scene> scene, Handle<SceneObject> parent);
		void FocusAfterCreate(Vector<UI::Widgets::MenuItem>& items);
		void OpenRowContextMenu(UI::Context& context, Handle<SceneObject> object);
		void OpenFolderContextMenu(UI::Context& context, Handle<Scene> scene, u64 folderId);
		void OpenSceneContextMenu(UI::Context& context, Handle<Scene> scene);
		void SetSceneExpanded(u64 sceneId, bool expanded);
		bool IsFolderCollapsed(u64 sceneId, u64 folderId) const;
		void SetFolderCollapsed(u64 sceneId, u64 folderId, bool collapsed);
		void Reveal(SceneObject& object);
		void RevealNewlySelected();
		static UI::Widgets::MenuItem BuildCreateFolderItem(const Vector<Handle<SceneObject>>& targets);
		static UI::Widgets::MenuItem BuildCreatePrefabItem(UI::Context& context, Handle<SceneObject> object);
		static UI::Widgets::MenuItem BuildCreatePrefabVariantItem(UI::Context& context, Handle<SceneObject> object);
		static str SeedPrefabPath(const SceneObject& live);

		static u64 FolderKey(u64 sceneId, u64 folderId);

		void OnWorldRemapped(const EditorWorldRemappedEvent& event);

		void OnPingRequested(const EditorPingRequestedEvent& event);

		HashSet<u64> _expanded{GetMemoryResource(MemTag::Editor)};
		HashSet<u64> _collapsedScenes{GetMemoryResource(MemTag::Editor)};
		HashMap<u64, HashSet<u64>> _collapsedFolders{GetMemoryResource(MemTag::Editor)};
		u64 _renameTarget = 0;
		u64 _renameFolderScene = 0;
		u64 _renameFolder = 0;
		bool _renameNeedsFocus = false;
		u64 _lastVersion = 0;
		u64 _lastSelectionVersion = 0;
		HashSet<u64> _lastSelection{GetMemoryResource(MemTag::Editor)};
		bool _forceRebuild = false;

		bool _searchOpen = false;
		bool _focusSearch = false;
		str _filter;

		UI::ElementHandle _headerHost;

		EditorListView _list;
		Vector<ListRow> _rows{GetMemoryResource(MemTag::Editor)};
		EditorInsertStrips _gaps;
	};

} // namespace Shalltear
