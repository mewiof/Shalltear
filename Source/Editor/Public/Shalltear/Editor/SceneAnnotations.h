#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditorEvents.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class Scene;
	class SceneObject;

	enum class LabelColor : u8 {
		None = 0,
		Red,
		Orange,
		Yellow,
		Green,
		Teal,
		Blue,
		Purple,
		Pink,
		Count
	};

	enum class FolderParentKind : u8 {
		SceneRoot = 0,
		Object,
		Folder
	};

	struct FolderParent {
		FolderParentKind kind = FolderParentKind::SceneRoot;
		u64 id = 0;

		bool operator==(const FolderParent& other) const = default;

		static FolderParent Roots() { return {}; }
		static FolderParent Under(u64 objectInstanceId) { return {FolderParentKind::Object, objectInstanceId}; }
		static FolderParent In(u64 folderId) { return {FolderParentKind::Folder, folderId}; }
	};

	struct FolderInfo {
		u64 id = 0;
		str name;
		FolderParent parent;
		u32 slot = 0;
		u32 order = 0;
		LabelColor label = LabelColor::None;
	};

	inline constexpr u32 FOLDER_SLOT_END = 0xFFFFFFFFu;

	class SHALLTEAR_EDITOR_API SceneAnnotations {
	public:
		static void Initialize();
		static void Terminate();

		static LabelColor GetObjectLabel(Handle<SceneObject> object);
		static LabelColor GetFolderLabel(Handle<Scene> scene, u64 folderId);
		static void SetObjectLabels(std::span<const Handle<SceneObject>> objects, LabelColor color);
		static void SetFolderLabel(Handle<Scene> scene, u64 folderId, LabelColor color);
		static std::string_view GetLabelName(LabelColor color);

		static u64 CreateFolder(Handle<Scene> scene, std::string_view name, FolderParent parent,
								std::span<const Handle<SceneObject>> members, u32 slot = FOLDER_SLOT_END);
		static bool RenameFolder(Handle<Scene> scene, u64 folderId, std::string_view name);
		static bool DeleteFolder(Handle<Scene> scene, u64 folderId);
		static bool MoveFolder(Handle<Scene> scene, u64 folderId, FolderParent newParent, u32 slot, u32 newOrder);

		static bool GetFolder(Handle<Scene> scene, u64 folderId, FolderInfo& out);
		static void GetChildFolders(Handle<Scene> scene, FolderParent parent, Vector<FolderInfo>& out);
		static Handle<SceneObject> GetFolderAnchor(Handle<Scene> scene, u64 folderId);

		static u64 GetFolderOf(Handle<SceneObject> object);
		static void FileObjects(Handle<Scene> scene, std::span<const Handle<SceneObject>> objects, u64 folderId);

		static bool CanCreateFolderFrom(std::span<const Handle<SceneObject>> objects,
										Handle<Scene>& outScene, FolderParent& outParent);

		static u64 GetVersion(Handle<Scene> scene);

		static Path GetSidecarPath(Handle<Scene> scene);
		// after the save (SaveScene mints the localIds)
		static bool SaveFor(Handle<Scene> scene);
		static bool LoadFor(Handle<Scene> scene);
		static void FlushDirty();

		static void RemapIds(std::span<const EditorIdRemap> remap);
	};

} // namespace Shalltear
