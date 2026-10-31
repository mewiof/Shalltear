#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditTarget.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class Component;
	class Scene;
	class SceneObject;
	struct PrefabInstanceDiff;

	class SHALLTEAR_EDITOR_API EditorSceneOps {
	public:
		static void Initialize();
		static void Terminate();

		static void MarkDirty(Handle<Scene> scene);
		static void MarkClean(Handle<Scene> scene);
		static bool IsDirty(Handle<Scene> scene);
		static bool AnyDirty();

		static Path GetScenePath(Handle<Scene> scene);

		static Handle<Scene> NewScene(std::string_view name);
		static void PopulateNewScene(Handle<Scene> scene);
		static Handle<Scene> OpenScene(const Path& path);
		static Handle<Scene> LoadSceneAdditive(const Path& path);
		static bool SaveScene(Handle<Scene> scene, const Path& path);

		static void GetRecentScenes(Vector<str>& out);
		static void AddRecentScene(std::string_view path);

		static void GetSceneSetup(Vector<str>& out);

		static str ChooseStartupScene(const str& projectPath, std::span<const str> remembered);

		static void GetSelectedObjects(Vector<Handle<SceneObject>>& out);
		static u32 DeleteObjects(std::span<const Handle<SceneObject>> objects);
		static u32 DeleteSelectedObjects();
		static u32 DuplicateObjects(std::span<const Handle<SceneObject>> objects);
		static u32 DuplicateSelectedObjects();

		static void GetOutermostInTreeOrder(std::span<const Handle<SceneObject>> objects,
											Vector<Handle<SceneObject>>& out);
		static bool CanMoveObjects(std::span<const Handle<SceneObject>> objects, Handle<SceneObject> newParent,
								   Handle<Scene> scene);
		static bool MoveObjects(std::span<const Handle<SceneObject>> objects, Handle<SceneObject> newParent,
								Handle<Scene> scene, u32 index);

		static bool CanCreateEmptyParent(std::span<const Handle<SceneObject>> objects);
		static Handle<SceneObject> CreateEmptyParent(std::span<const Handle<SceneObject>> objects,
													 Handle<SceneObject> active);

		static Handle<SceneObject> CreatePrimitiveObject(PrimitiveShape shape, Handle<Scene> scene = {},
														 Handle<SceneObject> parent = {});

		static void BuildCreateMenu(Handle<Scene> scene, Handle<SceneObject> parent,
									Vector<UI::Widgets::MenuItem>& out);
		static UI::Widgets::MenuItem BuildCreateSubmenu(std::string_view label, Handle<Scene> scene,
														Handle<SceneObject> parent);

		static u32 BindDefaultMaterials(Handle<Object> renderer, u64 meshInstanceId);

		static Handle<Scene> OpenPrefab(const Guid& prefabId);
		static Handle<Scene> GetPrefabStage();
		static void GetStageScenes(Vector<Handle<Scene>>& out);
		static Handle<SceneObject> GetDefaultParent(Handle<Scene> scene);
		static bool LeavePrefabStage();
		static Guid GetPrefabTarget(Handle<Scene> scene);
		static bool AcceptsPrefabInstances(Handle<Scene> scene);
		static bool SavePrefabScene(Handle<Scene> scene);

		static Handle<SceneObject> GetPrefabPreviewRoot(const Guid& prefabId);
		static Object* ResolvePrefabRow(const Guid& prefabId, u64 rowKey);
		static void MarkPrefabPreviewEdited(const Guid& prefabId);
		static void FlushPrefabPreview();
		static void ReleasePrefabPreview();

		static Guid CreatePrefabFrom(Handle<SceneObject> root, const Path& path);

		static bool CanCreatePrefabVariantFrom(Handle<SceneObject> root);

		static bool PrefabOverridesFor(Handle<SceneObject> row, PrefabInstanceDiff& out,
									   Handle<SceneObject>* outRoot = nullptr);
		static Guid CreatePrefabVariantFrom(Handle<SceneObject> root, const Path& path);
		static Guid CreatePrefabVariantOf(const Guid& basePrefab, const Path& path);

		static constexpr u32 COMPONENT_INDEX_END = 0xFFFFFFFFu;
		enum class ComponentDropKind : u8 {
			None = 0,
			Reorder,
			Move
		};
		static ComponentDropKind ClassifyComponentDrop(Handle<Component> component, Handle<SceneObject> target);

		static bool RemovePreservedComponent(Handle<Component> component);

		struct PreservedRowSurvey {
			u32 rows = 0;
			Vector<StringHash> types{GetMemoryResource(MemTag::Editor)};
		};
		static PreservedRowSurvey SurveyPreservedComponents(Handle<Scene> scene);
		static bool DropComponentOn(Handle<Component> component, Handle<SceneObject> target, u32 index);

		struct ValueStep {
			i32 element = -1;
			StringHash field{};
		};
		static bool DiffersFromDefault(EditTarget target, StringHash field, std::span<const ValueStep> steps = {});
		static bool ResetToDefault(EditTarget target, StringHash field, std::span<const ValueStep> steps,
								   std::string_view label);
		static u32 ResetFieldsToDefaults(EditTarget target, std::span<const StringHash> fields, std::string_view label);
		static u32 ResetComponentToDefaults(EditTarget component);

		static Vec3 EulerForDisplay(const Quat& rotation);
	};

} // namespace Shalltear
