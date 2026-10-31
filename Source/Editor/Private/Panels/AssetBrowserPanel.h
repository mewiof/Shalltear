#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Input/InputAction.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorPanel.h"

#include <optional>

namespace Shalltear {

	struct FilesDroppedEvent;
	struct EditorPingRequestedEvent;
	class SceneObject;

	class AssetBrowserPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(AssetBrowserPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Asset Browser")
		SLT_REFLECT_FIELD(selectedFolder)
		SLT_REFLECT_FIELD(iconSize)
		SLT_REFLECT_FIELD(folderPaneWidth)
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		AssetBrowserPanel();

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;
		void OnOpen() override;
		void OnSelectionChanged() override;

		void OnFilesDropped(const FilesDroppedEvent& event);

		void OnPingRequested(const EditorPingRequestedEvent& event);

		str selectedFolder;

		f32 iconSize = AssetBrowserOps::SLIDER_SMALLEST_GRID;

		f32 folderPaneWidth = 210.0f;
	private:
		struct BuiltEntry {
			u64 id = 0;
			str relPath;
			Guid guid;
			UI::ElementHandle image;
			UI::ElementHandle badge;
			UI::ElementHandle variantMark;
			RHI::TextureHandle painted;
		};

		void EnsureScanned();
		void Rescan();
		void VisibleFileRows(Vector<AssetBrowserOps::Entry>& out);
		bool IsFileSelected(const str& relPath) const;
		bool EntryExists(const str& relPath) const;
		bool FolderExists(const str& relPath) const;
		bool GoBack();
		bool GoForward();
		bool GoUp();
		void RouteNavigation(UI::Context& context);
		bool IsInsidePanel(UI::Context& context, UI::ElementHandle handle) const;
		void BuildCrumbs(UI::Context& context, UI::ElementHandle bar);
		void BuildPathField(UI::Context& context, UI::ElementHandle bar);
		void OpenSubfolderMenu(UI::Context& context, const str& folder, const str& current, Vec2 anchorPx);
		bool GoToTypedPath(UI::Context& context, const str& typed);
		void NavigateToFolder(const str& folder);
		void OpenFolder(const str& relPath);
		bool AcceptsDropInto(const UI::DragPayload& payload, const str& folderRelPath) const;
		void DropInto(UI::Context& context, const UI::DragPayload& payload, const str& folderRelPath);
		str DropFolderAt(UI::Context& context, Vec2 point) const;
		void SelectOnlyFile(const str& relPath);
		void PublishFileSelectionFromList();
		void PushFileSelectionToList();
		void PublishFolderSelectionFromList();
		void PushFolderSelectionToList();
		void PushAssetSelection();
		void LeaveFolderSelection();
		void ClearFileSelection();
		void SyncSelectionFromEditor();
		u64 RowIdForAsset(const Guid& guid);
		str PathOf(u64 id) const;
		const AssetBrowserOps::Entry* RowOf(u64 id) const;
		const Vector<AssetBrowserOps::Entry>& FoldOutRowsFor(const AssetBrowserOps::Entry& entry);
		bool HasFoldOut(const AssetBrowserOps::Entry& entry);
		bool ReportRefusals(const Vector<u64>& ids, AssetBrowserOps::EntryCommand command);
		void ExtractProductRow(const AssetBrowserOps::Entry& entry);
		void SelectedEntryPaths(Vector<str>& out) const;
		void SelectionTargets(const str& clickedRelPath, Vector<str>& out) const;
		void SelectedFolderPaths(Vector<str>& out) const;
		void DeleteFiles(UI::Context& context, const Vector<str>& targets);
		void DuplicateFiles(const Vector<str>& targets);
		void CreatePrefabVariants(const Vector<str>& targets);
		void CreatePrefabsFrom(const Vector<Handle<SceneObject>>& objects, const str& folderRelPath);
		void OfferPrefabVariant(UI::Context& context, Handle<SceneObject> root, const str& folderRelPath);
		void DeleteFolders(UI::Context& context, const Vector<str>& targets);
		bool BeginRenameEntry(UI::Context& context, const str& relPath);
		bool BeginRenameFolder(UI::Context& context, const str& relPath);
		void RemapViewState(const str& oldRelPath, const str& newRelPath);
		void BuildHeader(UI::Context& context);
		void BuildFooter(UI::Context& context);
		void RebuildPanes(UI::Context& context);
		void BuildFolderRows(UI::Context& context);
		void BuildFileChrome(UI::Context& context);
		void BuildFileEntries(UI::Context& context);
		ListViewDesc MakeFileListDesc();
		void EnsureFileList(UI::Context& context);
		struct FileEntryActions {
			std::function<void(UI::Context&)> onDoubleClick;
			std::function<void(UI::Context&)> onRightClick;
			std::function<void(Vec2)> onDrag;
			std::function<bool(const UI::DragPayload&)> onDragOver;
			std::function<void(const UI::DragPayload&, Vec2)> onDrop;
		};
		FileEntryActions MakeFileActions(const AssetBrowserOps::Entry& entry);
		void BuildCellPicture(UI::Context& context, u64 id, UI::ElementHandle well);
		void RequestVisiblePreviews(UI::Context& context);
		void SetBadgeShown(UI::Context& context, const BuiltEntry& built, bool shown);
		void OpenFolderContextMenu(UI::Context& context, const str& relPath, bool isRoot);
		void OpenFileContextMenu(UI::Context& context, const AssetBrowserOps::Entry& entry);
		void OpenFoldOutContextMenu(UI::Context& context, const AssetBrowserOps::Entry& entry);
		void OpenCreateMenu(UI::Context& context, Vec2 anchorPx);
		void BuildCreateItems(UI::Context& context, Vector<UI::Widgets::MenuItem>& out);
		Path SelectedFolderVirtual() const;
		void CreateAssetInSelectedFolder(StringHash assetTypeId, const str& fileName, u32 templateIndex);

		Vector<AssetBrowserOps::Entry> _entries{GetMemoryResource(MemTag::Editor)};
		HashSet<u64> _expandedFolders{GetMemoryResource(MemTag::Editor)};
		EditorListView _folderList;
		EditorListView _fileList;
		HashMap<u64, AssetBrowserOps::Entry> _rowById{GetMemoryResource(MemTag::Editor)};
		HashSet<u64> _expandedAssets{GetMemoryResource(MemTag::Editor)};
		HashMap<u64, Vector<AssetBrowserOps::Entry>> _foldOutRows{GetMemoryResource(MemTag::Editor)};
		struct FoldOutMemo {
			u64 stamp = 0;
			bool has = false;
		};
		HashMap<u64, FoldOutMemo> _hasProducts{GetMemoryResource(MemTag::Editor)};
		Vector<u64> _selectedFiles{GetMemoryResource(MemTag::Editor)};
		Vector<u64> _selectedFolders{GetMemoryResource(MemTag::Editor)};
		str _filter;
		bool _searchOpen = false;
		bool _focusSearch = false;
		bool _scanned = false;
		bool _mounted = false;
		bool _dirty = false;
		bool _publishingSelection = false;
		bool _fileHighlightDirty = false;

		f32 _rescanDelay = -1.0f;

		f32 _mountPollDelay = 1.0f;

		f32 _foldOutPollDelay = 0.0f;

		Vector<BuiltEntry> _builtEntries{GetMemoryResource(MemTag::Editor)};
		HashMap<u64, u32> _entryIndexById{GetMemoryResource(MemTag::Editor)};

		UI::ElementHandle _headerHost;
		UI::ElementHandle _footerHost;
		UI::Widgets::SliderParts _sliderParts;
		UI::ElementHandle _fileChromeHost;
		UI::ElementHandle _fileListHost;
		UI::ElementHandle _filePane;
		bool _fileListBuilt = false;
		ListLayout _fileListLayout = ListLayout::Rows;
		f32 _fileListCellWidthPx = 0.0f;
		f32 _fileListCellHeightPx = 0.0f;
		UI::Context* _buildContext = nullptr;
		UI::ElementHandle _contentRoot;

		AssetBrowserOps::FolderHistory _history;
		InputActionMap _navigationKeys;
		ActionId _backAction = INVALID_ACTION_ID;
		ActionId _forwardAction = INVALID_ACTION_ID;
		ActionId _upAction = INVALID_ACTION_ID;
		u64 _revealAfterBuild = 0;
		bool _editingPath = false;
		bool _focusPathField = false;
		UI::ElementHandle _pathField;
	};

} // namespace Shalltear
