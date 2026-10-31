#include "Shalltear/Editor/SceneAnnotations.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include "Shalltear/Editor/EditorCommands.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <utility>

namespace Shalltear {

	namespace {

		struct Partition {
			HashMap<u64, LabelColor> objectLabels{GetMemoryResource(MemTag::Editor)};
			HashMap<u64, u64> membership{GetMemoryResource(MemTag::Editor)};
			Vector<FolderInfo> folders{GetMemoryResource(MemTag::Editor)};
			u64 nextFolderId = 1;
			u64 version = 1;
			bool dirty = false;
		};

		struct ScalarRow {
			u64 key = 0;
			u64 before = 0;
			u64 after = 0;
		};

		struct AnnotationDelta {
			Vector<ScalarRow> labels{GetMemoryResource(MemTag::Editor)};
			Vector<ScalarRow> membership{GetMemoryResource(MemTag::Editor)};
			struct FolderRow {
				FolderInfo before;
				FolderInfo after;
				bool hadBefore = false;
				bool hasAfter = false;
			};
			Vector<FolderRow> folders{GetMemoryResource(MemTag::Editor)};
			u64 nextFolderIdBefore = 0;
			u64 nextFolderIdAfter = 0;

			bool Empty() const { return labels.empty() && membership.empty() && folders.empty(); }
		};

		struct AnnotationState : public Object {
			HashMap<u64, Partition> partitions{GetMemoryResource(MemTag::Editor)};

			void OnWorldRemapped(const EditorWorldRemappedEvent& event) { SceneAnnotations::RemapIds(event.ids); }
		};

		AnnotationState* _state = nullptr;

		Partition* Find(u64 sceneId) {
			if (!_state || sceneId == 0) {
				return nullptr;
			}
			auto it = _state->partitions.find(sceneId);
			return it == _state->partitions.end() ? nullptr : &it->second;
		}

		Partition& Obtain(u64 sceneId) {
			return _state->partitions.try_emplace(sceneId).first->second;
		}

		FolderInfo* FindFolder(Partition& partition, u64 folderId) {
			if (folderId == 0) {
				return nullptr;
			}
			for (FolderInfo& folder : partition.folders) {
				if (folder.id == folderId) {
					return &folder;
				}
			}
			return nullptr;
		}

		constexpr u32 MAX_FOLDER_DEPTH = 64;

		bool ResolveAnchor(Partition& partition, u64 folderId, u64& outAnchor) {
			u64 walk = folderId;
			for (u32 step = 0; step < MAX_FOLDER_DEPTH; ++step) {
				FolderInfo* folder = FindFolder(partition, walk);
				if (!folder) {
					return false;
				}
				switch (folder->parent.kind) {
				case FolderParentKind::SceneRoot:
					outAnchor = 0;
					return true;
				case FolderParentKind::Object:
					outAnchor = folder->parent.id;
					return true;
				case FolderParentKind::Folder:
					walk = folder->parent.id;
					break;
				}
			}
			SLT_LOG_WARN("Editor", "annotation folder chain exceeded {} hops (cycle?)", MAX_FOLDER_DEPTH);
			return false;
		}

		bool ResolveParentAnchor(Partition& partition, const FolderParent& parent, u64& outAnchor) {
			switch (parent.kind) {
			case FolderParentKind::SceneRoot:
				outAnchor = 0;
				return true;
			case FolderParentKind::Object:
				outAnchor = parent.id;
				return true;
			case FolderParentKind::Folder:
				return ResolveAnchor(partition, parent.id, outAnchor);
			}
			return false;
		}

		u64 GroupOf(const SceneObject& object) {
			SceneObject* parent = object.GetParent().Get();
			return parent ? parent->GetInstanceId() : 0;
		}

		u64 ValidFolderOf(Partition& partition, SceneObject& object) {
			auto it = partition.membership.find(object.GetInstanceId());
			if (it == partition.membership.end()) {
				return 0;
			}
			u64 anchor = 0;
			if (!ResolveAnchor(partition, it->second, anchor)) {
				return 0;
			}
			return anchor == GroupOf(object) ? it->second : 0;
		}

		bool IsDescendantOf(SceneObject* node, SceneObject* ancestor) {
			for (SceneObject* walk = node; walk; walk = walk->GetParent().Get()) {
				if (walk == ancestor) {
					return true;
				}
			}
			return false;
		}

		void CollectSubtree(Partition& partition, u64 folderId, Vector<u64>& out) {
			out.push_back(folderId);
			for (size_t i = 0; i < out.size(); ++i) {
				u64 current = out[i];
				for (const FolderInfo& folder : partition.folders) {
					if (folder.parent.kind == FolderParentKind::Folder && folder.parent.id == current) {
						out.push_back(folder.id);
					}
				}
				if (out.size() > MAX_FOLDER_DEPTH * MAX_FOLDER_DEPTH) {
					SLT_LOG_WARN("Editor", "annotation folder subtree walk ran away (cycle?)");
					return;
				}
			}
		}

		bool Precedes(const FolderInfo& a, const FolderInfo& b) {
			return a.slot != b.slot ? a.slot < b.slot : a.order < b.order;
		}

		void SortByOrder(Vector<FolderInfo>& folders) {
			std::stable_sort(folders.begin(), folders.end(),
							 [](const FolderInfo& a, const FolderInfo& b) { return Precedes(a, b); });
		}

		void Renumber(Partition& partition, const FolderParent& parent, u32 slot) {
			Vector<FolderInfo*> siblings{GetMemoryResource(MemTag::Editor)};
			for (FolderInfo& folder : partition.folders) {
				if (folder.parent == parent && folder.slot == slot) {
					siblings.push_back(&folder);
				}
			}
			std::stable_sort(siblings.begin(), siblings.end(),
							 [](const FolderInfo* a, const FolderInfo* b) { return a->order < b->order; });
			for (u32 i = 0; i < siblings.size(); ++i) {
				siblings[i]->order = i;
			}
		}

		class AnnotationCommand final : public IEditorCommand {
		public:
			AnnotationCommand(u64 sceneId, str label, AnnotationDelta delta)
				: _sceneId(sceneId), _label(std::move(label)), _delta(std::move(delta)) {}

			void Apply() override { Replay(false); }
			void Revert() override { Replay(true); }
			str GetLabel() const override { return _label; }
			Handle<Scene> GetAffectedScene() const override { return {}; }
		private:
			void Replay(bool backwards) {
				Partition* partition = Find(_sceneId);
				if (!partition) {
					return;
				}
				for (const ScalarRow& row : _delta.labels) {
					LabelColor value = static_cast<LabelColor>(backwards ? row.before : row.after);
					if (value == LabelColor::None) {
						partition->objectLabels.erase(row.key);
					} else {
						partition->objectLabels[row.key] = value;
					}
				}
				for (const ScalarRow& row : _delta.membership) {
					u64 value = backwards ? row.before : row.after;
					if (value == 0) {
						partition->membership.erase(row.key);
					} else {
						partition->membership[row.key] = value;
					}
				}
				for (const AnnotationDelta::FolderRow& row : _delta.folders) {
					const FolderInfo& want = backwards ? row.before : row.after;
					bool present = backwards ? row.hadBefore : row.hasAfter;
					u64 id = row.hadBefore ? row.before.id : row.after.id;
					std::erase_if(partition->folders, [id](const FolderInfo& f) { return f.id == id; });
					if (present) {
						partition->folders.push_back(want);
					}
				}
				partition->nextFolderId = backwards ? _delta.nextFolderIdBefore : _delta.nextFolderIdAfter;
				++partition->version;
				partition->dirty = true;
			}

			u64 _sceneId;
			str _label;
			AnnotationDelta _delta;
		};

		struct Snapshot {
			HashMap<u64, LabelColor> objectLabels{GetMemoryResource(MemTag::Editor)};
			HashMap<u64, u64> membership{GetMemoryResource(MemTag::Editor)};
			Vector<FolderInfo> folders{GetMemoryResource(MemTag::Editor)};
			u64 nextFolderId = 0;
		};

		Snapshot Capture(const Partition& partition) {
			Snapshot out;
			out.objectLabels = partition.objectLabels;
			out.membership = partition.membership;
			out.folders = partition.folders;
			out.nextFolderId = partition.nextFolderId;
			return out;
		}

		const FolderInfo* FindIn(const Vector<FolderInfo>& folders, u64 id) {
			for (const FolderInfo& folder : folders) {
				if (folder.id == id) {
					return &folder;
				}
			}
			return nullptr;
		}

		bool SameFolder(const FolderInfo& a, const FolderInfo& b) {
			return a.id == b.id && a.name == b.name && a.parent == b.parent && a.slot == b.slot &&
				   a.order == b.order && a.label == b.label;
		}

		template <typename MapT>
		void DiffMap(const MapT& before, const MapT& after, Vector<ScalarRow>& out) {
			for (const auto& [key, value] : after) {
				auto it = before.find(key);
				u64 was = it == before.end() ? 0 : static_cast<u64>(it->second);
				if (was != static_cast<u64>(value)) {
					out.push_back(ScalarRow{key, was, static_cast<u64>(value)});
				}
			}
			for (const auto& [key, value] : before) {
				if (!after.contains(key)) {
					out.push_back(ScalarRow{key, static_cast<u64>(value), 0});
				}
			}
		}

		AnnotationDelta Diff(const Snapshot& before, const Partition& after) {
			AnnotationDelta delta;
			DiffMap(before.objectLabels, after.objectLabels, delta.labels);
			DiffMap(before.membership, after.membership, delta.membership);

			for (const FolderInfo& now : after.folders) {
				const FolderInfo* was = FindIn(before.folders, now.id);
				if (!was) {
					delta.folders.push_back(AnnotationDelta::FolderRow{FolderInfo{}, now, false, true});
				} else if (!SameFolder(*was, now)) {
					delta.folders.push_back(AnnotationDelta::FolderRow{*was, now, true, true});
				}
			}
			for (const FolderInfo& was : before.folders) {
				if (!FindIn(after.folders, was.id)) {
					delta.folders.push_back(AnnotationDelta::FolderRow{was, FolderInfo{}, true, false});
				}
			}
			delta.nextFolderIdBefore = before.nextFolderId;
			delta.nextFolderIdAfter = after.nextFolderId;
			return delta;
		}

		void Edit(u64 sceneId, std::string_view label, const std::function<void(Partition&)>& mutate) {
			if (!_state || sceneId == 0) {
				return;
			}
			Partition& partition = Obtain(sceneId);
			Snapshot before = Capture(partition);
			mutate(partition);
			AnnotationDelta delta = Diff(before, partition);
			if (delta.Empty() && delta.nextFolderIdBefore == delta.nextFolderIdAfter) {
				return;
			}
			++partition.version;
			partition.dirty = true;
			UndoSystem::Push(MakeUnique<AnnotationCommand>(sceneId, str(label), std::move(delta)));
		}

		Handle<Scene> SceneOf(Handle<SceneObject> object) {
			SceneObject* resolved = object.Get();
			return resolved ? resolved->GetScene() : Handle<Scene>{};
		}

		struct LocalIdWalk {
			HashMap<u64, u64>* instanceToLocal;
			HashMap<u64, u64>* localToInstance;
			static void Visit(SceneObject& object, void* user) {
				LocalIdWalk& walk = *static_cast<LocalIdWalk*>(user);
				if (object.GetLocalId() == 0) {
					return;
				}
				if (walk.instanceToLocal) {
					(*walk.instanceToLocal)[object.GetInstanceId()] = object.GetLocalId();
				}
				if (walk.localToInstance) {
					(*walk.localToInstance)[object.GetLocalId()] = object.GetInstanceId();
				}
			}
		};

		Path LegacySidecarPath(Handle<Scene> scene) {
			Path scenePath = EditorSceneOps::GetScenePath(scene);
			const str& text = scenePath.ToString();
			constexpr std::string_view MOUNT = "project://";
			if (text.empty() || !text.starts_with(MOUNT)) {
				return {};
			}
			return Path("project://EditorData/Scenes/" + text.substr(MOUNT.size()) + ".json");
		}

		bool ReparentInto(Handle<SceneObject> object, Handle<SceneObject> anchor) {
			SceneObject* resolved = object.Get();
			if (!resolved) {
				return false;
			}
			SceneObject* anchorResolved = anchor.Get();
			if (anchorResolved && IsDescendantOf(anchorResolved, resolved)) {
				return false;
			}
			u64 currentGroup = GroupOf(*resolved);
			u64 wantGroup = anchorResolved ? anchorResolved->GetInstanceId() : 0;
			if (currentGroup == wantGroup) {
				return true;
			}
			u32 index = anchorResolved ? anchorResolved->GetChildCount() : 0;
			if (!anchorResolved) {
				Scene* scene = resolved->GetScene().Get();
				index = scene ? scene->GetRootCount() : 0;
			}
			UndoSystem::Push(MakeUnique<ReparentCommand>(object, anchor, index));
			return true;
		}

	} // anonymous namespace

	void SceneAnnotations::Initialize() {
		if (_state) {
			return;
		}
		_state = New<AnnotationState>(MemTag::Editor);
		Handle<AnnotationState> self(_state);
		EditorEvents::WorldRemapped.Subscribe(EventBus::GetGlobal(), self, &AnnotationState::OnWorldRemapped);
	}

	void SceneAnnotations::Terminate() {
		if (!_state) {
			return;
		}
		FlushDirty();
		EventBus::GetGlobal().UnsubscribeAll(_state->GetInstanceId());
		Delete(_state);
		_state = nullptr;
	}

	LabelColor SceneAnnotations::GetObjectLabel(Handle<SceneObject> object) {
		SceneObject* resolved = object.Get();
		if (!resolved) {
			return LabelColor::None;
		}
		Partition* partition = Find(resolved->GetScene().GetInstanceId());
		if (!partition) {
			return LabelColor::None;
		}
		auto it = partition->objectLabels.find(resolved->GetInstanceId());
		return it == partition->objectLabels.end() ? LabelColor::None : it->second;
	}

	LabelColor SceneAnnotations::GetFolderLabel(Handle<Scene> scene, u64 folderId) {
		Partition* partition = Find(scene.GetInstanceId());
		if (!partition) {
			return LabelColor::None;
		}
		const FolderInfo* folder = FindFolder(*partition, folderId);
		return folder ? folder->label : LabelColor::None;
	}

	void SceneAnnotations::SetObjectLabels(std::span<const Handle<SceneObject>> objects, LabelColor color) {
		if (!_state || objects.empty()) {
			return;
		}
		Vector<u64> scenes{GetMemoryResource(MemTag::Editor)};
		for (Handle<SceneObject> object : objects) {
			u64 sceneId = SceneOf(object).GetInstanceId();
			if (sceneId != 0 && std::find(scenes.begin(), scenes.end(), sceneId) == scenes.end()) {
				scenes.push_back(sceneId);
			}
		}
		str label = color == LabelColor::None ? str("Clear Label") : "Set Label " + str(GetLabelName(color));
		UndoSystem::BeginGroup(label);
		for (u64 sceneId : scenes) {
			Edit(sceneId, label, [&](Partition& partition) {
				for (Handle<SceneObject> object : objects) {
					SceneObject* resolved = object.Get();
					if (!resolved || resolved->GetScene().GetInstanceId() != sceneId) {
						continue;
					}
					if (color == LabelColor::None) {
						partition.objectLabels.erase(resolved->GetInstanceId());
					} else {
						partition.objectLabels[resolved->GetInstanceId()] = color;
					}
				}
			});
		}
		UndoSystem::EndGroup();
	}

	void SceneAnnotations::SetFolderLabel(Handle<Scene> scene, u64 folderId, LabelColor color) {
		Edit(scene.GetInstanceId(), "Set Folder Label", [&](Partition& partition) {
			if (FolderInfo* folder = FindFolder(partition, folderId)) {
				folder->label = color;
			}
		});
	}

	std::string_view SceneAnnotations::GetLabelName(LabelColor color) {
		switch (color) {
		case LabelColor::Red:
			return "Red";
		case LabelColor::Orange:
			return "Orange";
		case LabelColor::Yellow:
			return "Yellow";
		case LabelColor::Green:
			return "Green";
		case LabelColor::Teal:
			return "Teal";
		case LabelColor::Blue:
			return "Blue";
		case LabelColor::Purple:
			return "Purple";
		case LabelColor::Pink:
			return "Pink";
		default:
			return "None";
		}
	}

	u64 SceneAnnotations::CreateFolder(Handle<Scene> scene, std::string_view name, FolderParent parent,
									   std::span<const Handle<SceneObject>> members, u32 slot) {
		if (!_state || !scene.IsValid()) {
			return 0;
		}
		u64 sceneId = scene.GetInstanceId();
		Partition& partition = Obtain(sceneId);
		u64 anchor = 0;
		if (!ResolveParentAnchor(partition, parent, anchor)) {
			return 0;
		}
		for (Handle<SceneObject> member : members) {
			SceneObject* resolved = member.Get();
			if (!resolved || resolved->GetScene().GetInstanceId() != sceneId || GroupOf(*resolved) != anchor) {
				return 0;
			}
		}

		u32 resolvedSlot = slot;
		if (resolvedSlot == FOLDER_SLOT_END) {
			for (Handle<SceneObject> member : members) {
				if (SceneObject* resolved = member.Get()) {
					resolvedSlot = std::min(resolvedSlot, resolved->GetSiblingIndex());
				}
			}
		}
		if (resolvedSlot == FOLDER_SLOT_END) {
			Object* anchorObject = anchor == 0 ? nullptr : ObjectRegistry::Resolve(anchor);
			SceneObject* anchorResolved =
				anchorObject && TypeRegistry::IsDerivedFrom(anchorObject->GetType(), SceneObject::GetReflectType())
					? static_cast<SceneObject*>(anchorObject)
					: nullptr;
			Scene* sceneResolved = scene.Get();
			resolvedSlot = anchorResolved ? anchorResolved->GetChildCount()
										  : (sceneResolved ? sceneResolved->GetRootCount() : 0);
		}

		u64 created = 0;
		Edit(sceneId, "Create Folder", [&](Partition& live) {
			created = live.nextFolderId++;
			u32 order = 0;
			for (const FolderInfo& folder : live.folders) {
				if (folder.parent == parent && folder.slot == resolvedSlot) {
					order = std::max(order, folder.order + 1);
				}
			}
			live.folders.push_back(FolderInfo{created, str(name), parent, resolvedSlot, order, LabelColor::None});
			for (Handle<SceneObject> member : members) {
				if (SceneObject* resolved = member.Get()) {
					live.membership[resolved->GetInstanceId()] = created;
				}
			}
		});
		return created;
	}

	bool SceneAnnotations::RenameFolder(Handle<Scene> scene, u64 folderId, std::string_view name) {
		Partition* partition = Find(scene.GetInstanceId());
		if (!partition || !FindFolder(*partition, folderId) || name.empty()) {
			return false;
		}
		Edit(scene.GetInstanceId(), "Rename Folder", [&](Partition& live) {
			if (FolderInfo* folder = FindFolder(live, folderId)) {
				folder->name = str(name);
			}
		});
		return true;
	}

	bool SceneAnnotations::DeleteFolder(Handle<Scene> scene, u64 folderId) {
		Partition* partition = Find(scene.GetInstanceId());
		if (!partition || !FindFolder(*partition, folderId)) {
			return false;
		}
		Edit(scene.GetInstanceId(), "Delete Folder", [&](Partition& live) {
			FolderInfo* folder = FindFolder(live, folderId);
			if (!folder) {
				return;
			}
			FolderParent inherited = folder->parent;
			u32 inheritedSlot = folder->slot;
			std::erase_if(live.membership, [folderId](const auto& pair) { return pair.second == folderId; });
			for (FolderInfo& other : live.folders) {
				if (other.parent.kind == FolderParentKind::Folder && other.parent.id == folderId) {
					other.parent = inherited;
					other.slot = inheritedSlot;
				}
			}
			std::erase_if(live.folders, [folderId](const FolderInfo& f) { return f.id == folderId; });
			Renumber(live, inherited, inheritedSlot);
		});
		return true;
	}

	bool SceneAnnotations::MoveFolder(Handle<Scene> scene, u64 folderId, FolderParent newParent, u32 slot,
									  u32 newOrder) {
		Partition* partition = Find(scene.GetInstanceId());
		if (!partition) {
			return false;
		}
		FolderInfo* folder = FindFolder(*partition, folderId);
		if (!folder) {
			return false;
		}
		u64 oldAnchor = 0;
		u64 newAnchor = 0;
		if (!ResolveAnchor(*partition, folderId, oldAnchor) || !ResolveParentAnchor(*partition, newParent, newAnchor)) {
			return false;
		}
		Vector<u64> subtree{GetMemoryResource(MemTag::Editor)};
		CollectSubtree(*partition, folderId, subtree);
		if (newParent.kind == FolderParentKind::Folder &&
			std::find(subtree.begin(), subtree.end(), newParent.id) != subtree.end()) {
			return false;
		}

		Vector<Handle<SceneObject>> riders{GetMemoryResource(MemTag::Editor)};
		if (oldAnchor != newAnchor) {
			for (const auto& [objectId, owner] : partition->membership) {
				if (std::find(subtree.begin(), subtree.end(), owner) == subtree.end()) {
					continue;
				}
				Object* raw = ObjectRegistry::Resolve(objectId);
				if (raw && TypeRegistry::IsDerivedFrom(raw->GetType(), SceneObject::GetReflectType())) {
					riders.push_back(Handle<SceneObject>(static_cast<SceneObject*>(raw)));
				}
			}
			Handle<SceneObject> anchorHandle;
			if (newAnchor != 0) {
				Object* raw = ObjectRegistry::Resolve(newAnchor);
				if (!raw) {
					return false;
				}
				anchorHandle = Handle<SceneObject>(static_cast<SceneObject*>(raw));
			}
			for (Handle<SceneObject> rider : riders) {
				SceneObject* resolved = rider.Get();
				if (resolved && anchorHandle.Get() && IsDescendantOf(anchorHandle.Get(), resolved)) {
					return false;
				}
			}

			UndoSystem::BeginGroup("Move Folder");
			for (Handle<SceneObject> rider : riders) {
				ReparentInto(rider, anchorHandle);
			}
		}

		Edit(scene.GetInstanceId(), "Move Folder", [&](Partition& live) {
			FolderInfo* target = FindFolder(live, folderId);
			if (!target) {
				return;
			}
			FolderParent oldParent = target->parent;
			u32 oldSlot = target->slot;
			target->parent = newParent;
			target->slot = slot;

			Vector<FolderInfo*> siblings{GetMemoryResource(MemTag::Editor)};
			for (FolderInfo& folder : live.folders) {
				if (folder.parent == newParent && folder.slot == slot && folder.id != folderId) {
					siblings.push_back(&folder);
				}
			}
			std::stable_sort(siblings.begin(), siblings.end(),
							 [](const FolderInfo* a, const FolderInfo* b) { return a->order < b->order; });
			u32 place = std::min(newOrder, static_cast<u32>(siblings.size()));
			for (u32 i = 0; i < siblings.size(); ++i) {
				siblings[i]->order = i < place ? i : i + 1;
			}
			FindFolder(live, folderId)->order = place;
			if (!(oldParent == newParent) || oldSlot != slot) {
				Renumber(live, oldParent, oldSlot);
			}
		});

		if (oldAnchor != newAnchor) {
			UndoSystem::EndGroup();
		}
		return true;
	}

	bool SceneAnnotations::GetFolder(Handle<Scene> scene, u64 folderId, FolderInfo& out) {
		Partition* partition = Find(scene.GetInstanceId());
		if (!partition) {
			return false;
		}
		const FolderInfo* folder = FindFolder(*partition, folderId);
		if (!folder) {
			return false;
		}
		out = *folder;
		return true;
	}

	void SceneAnnotations::GetChildFolders(Handle<Scene> scene, FolderParent parent, Vector<FolderInfo>& out) {
		Partition* partition = Find(scene.GetInstanceId());
		if (!partition) {
			return;
		}
		for (const FolderInfo& folder : partition->folders) {
			if (folder.parent == parent) {
				out.push_back(folder);
			}
		}
		SortByOrder(out);
	}

	Handle<SceneObject> SceneAnnotations::GetFolderAnchor(Handle<Scene> scene, u64 folderId) {
		Partition* partition = Find(scene.GetInstanceId());
		u64 anchor = 0;
		if (!partition || !ResolveAnchor(*partition, folderId, anchor) || anchor == 0) {
			return {};
		}
		Object* raw = ObjectRegistry::Resolve(anchor);
		if (!raw || !TypeRegistry::IsDerivedFrom(raw->GetType(), SceneObject::GetReflectType())) {
			return {};
		}
		return Handle<SceneObject>(static_cast<SceneObject*>(raw));
	}

	u64 SceneAnnotations::GetFolderOf(Handle<SceneObject> object) {
		SceneObject* resolved = object.Get();
		if (!resolved) {
			return 0;
		}
		Partition* partition = Find(resolved->GetScene().GetInstanceId());
		return partition ? ValidFolderOf(*partition, *resolved) : 0;
	}

	void SceneAnnotations::FileObjects(Handle<Scene> scene, std::span<const Handle<SceneObject>> objects, u64 folderId) {
		if (!_state || !scene.IsValid() || objects.empty()) {
			return;
		}
		u64 sceneId = scene.GetInstanceId();
		Partition& partition = Obtain(sceneId);
		Handle<SceneObject> anchor;
		if (folderId != 0) {
			if (!FindFolder(partition, folderId)) {
				return;
			}
			anchor = GetFolderAnchor(scene, folderId);
		}

		UndoSystem::BeginGroup(folderId == 0 ? "Remove From Folder" : "Move To Folder");
		Vector<Handle<SceneObject>> filed{GetMemoryResource(MemTag::Editor)};
		for (Handle<SceneObject> object : objects) {
			SceneObject* resolved = object.Get();
			if (!resolved || resolved->GetScene().GetInstanceId() != sceneId) {
				continue;
			}
			if (folderId != 0 && !ReparentInto(object, anchor)) {
				continue;
			}
			filed.push_back(object);
		}
		Edit(sceneId, folderId == 0 ? "Remove From Folder" : "Move To Folder", [&](Partition& live) {
			for (Handle<SceneObject> object : filed) {
				SceneObject* resolved = object.Get();
				if (!resolved) {
					continue;
				}
				if (folderId == 0) {
					live.membership.erase(resolved->GetInstanceId());
				} else {
					live.membership[resolved->GetInstanceId()] = folderId;
				}
			}
		});
		UndoSystem::EndGroup();
	}

	bool SceneAnnotations::CanCreateFolderFrom(std::span<const Handle<SceneObject>> objects,
											   Handle<Scene>& outScene, FolderParent& outParent) {
		if (objects.empty()) {
			return false;
		}
		u64 sceneId = 0;
		u64 group = 0;
		bool first = true;
		for (Handle<SceneObject> object : objects) {
			SceneObject* resolved = object.Get();
			if (!resolved || resolved->IsPendingDestroy()) {
				return false;
			}
			u64 objectScene = resolved->GetScene().GetInstanceId();
			u64 objectGroup = GroupOf(*resolved);
			if (first) {
				sceneId = objectScene;
				group = objectGroup;
				first = false;
				continue;
			}
			if (objectScene != sceneId || objectGroup != group) {
				return false;
			}
		}
		Object* raw = ObjectRegistry::Resolve(sceneId);
		if (!raw) {
			return false;
		}
		outScene = Handle<Scene>(static_cast<Scene*>(raw));
		outParent = group == 0 ? FolderParent::Roots() : FolderParent::Under(group);
		return true;
	}

	u64 SceneAnnotations::GetVersion(Handle<Scene> scene) {
		Partition* partition = Find(scene.GetInstanceId());
		return partition ? partition->version : 0;
	}

	Path SceneAnnotations::GetSidecarPath(Handle<Scene> scene) {
		Scene* resolved = scene.Get();
		if (!resolved) {
			return {};
		}
		Guid id = resolved->GetSourceGuid();
		if (!id.IsValid()) {
			return {};
		}
		return Path("project://EditorData/Scenes/" + id.ToString() + ".json");
	}

	bool SceneAnnotations::SaveFor(Handle<Scene> scene) {
		Partition* partition = Find(scene.GetInstanceId());
		Scene* resolved = scene.Get();
		if (!partition || !resolved) {
			return false;
		}
		Path path = GetSidecarPath(scene);
		if (path.ToString().empty()) {
			return false;
		}

		HashMap<u64, u64> instanceToLocal{GetMemoryResource(MemTag::Editor)};
		LocalIdWalk walk{&instanceToLocal, nullptr};
		resolved->ForEachObject(&LocalIdWalk::Visit, &walk);

		auto localOf = [&instanceToLocal](u64 instanceId) -> u64 {
			auto it = instanceToLocal.find(instanceId);
			return it == instanceToLocal.end() ? 0 : it->second;
		};

		JSONValue root = JSONValue::MakeObject();
		root.AddMember("version", JSONValue::MakeNumber(2.0));
		Path breadcrumb = AssetDatabase::LookupPath(resolved->GetSourceGuid());
		root.AddMember("scene", JSONValue::MakeString(breadcrumb.ToString()));
		root.AddMember("nextFolderId", JSONValue::MakeNumber(static_cast<f64>(partition->nextFolderId)));

		JSONValue folders = JSONValue::MakeArray();
		for (const FolderInfo& folder : partition->folders) {
			u64 parentId = folder.parent.id;
			if (folder.parent.kind == FolderParentKind::Object) {
				parentId = localOf(folder.parent.id);
				if (parentId == 0) {
					continue;
				}
			}
			JSONValue value = JSONValue::MakeObject();
			value.AddMember("id", JSONValue::MakeNumber(static_cast<f64>(folder.id)));
			value.AddMember("name", JSONValue::MakeString(folder.name));
			value.AddMember("parentKind", JSONValue::MakeNumber(static_cast<f64>(folder.parent.kind)));
			value.AddMember("parentId", JSONValue::MakeNumber(static_cast<f64>(parentId)));
			value.AddMember("slot", JSONValue::MakeNumber(static_cast<f64>(folder.slot)));
			value.AddMember("order", JSONValue::MakeNumber(static_cast<f64>(folder.order)));
			value.AddMember("label", JSONValue::MakeNumber(static_cast<f64>(folder.label)));
			folders.Push(std::move(value));
		}
		root.AddMember("folders", std::move(folders));

		JSONValue objects = JSONValue::MakeArray();
		HashSet<u64> written{GetMemoryResource(MemTag::Editor)};
		for (const auto& [instanceId, color] : partition->objectLabels) {
			u64 local = localOf(instanceId);
			if (local == 0 || !written.insert(instanceId).second) {
				continue;
			}
			JSONValue value = JSONValue::MakeObject();
			value.AddMember("id", JSONValue::MakeNumber(static_cast<f64>(local)));
			value.AddMember("label", JSONValue::MakeNumber(static_cast<f64>(color)));
			auto member = partition->membership.find(instanceId);
			if (member != partition->membership.end()) {
				value.AddMember("folder", JSONValue::MakeNumber(static_cast<f64>(member->second)));
			}
			objects.Push(std::move(value));
		}
		for (const auto& [instanceId, folderId] : partition->membership) {
			u64 local = localOf(instanceId);
			if (local == 0 || !written.insert(instanceId).second) {
				continue;
			}
			JSONValue value = JSONValue::MakeObject();
			value.AddMember("id", JSONValue::MakeNumber(static_cast<f64>(local)));
			value.AddMember("folder", JSONValue::MakeNumber(static_cast<f64>(folderId)));
			objects.Push(std::move(value));
		}
		root.AddMember("objects", std::move(objects));

		FileSystem::CreateDirectory(Path(path.GetDirectory()));
		if (!FileSystem::WriteTextFile(path, JSONSerialize(root))) {
			SLT_LOG_WARN("Editor", "failed to write scene annotations to '{}'", path.ToString());
			return false;
		}
		partition->dirty = false;
		return true;
	}

	bool SceneAnnotations::LoadFor(Handle<Scene> scene) {
		Scene* resolved = scene.Get();
		if (!_state || !resolved) {
			return false;
		}
		Path path = GetSidecarPath(scene);
		if (path.ToString().empty()) {
			return false;
		}
		Path legacy;
		if (!FileSystem::Exists(path)) {
			legacy = LegacySidecarPath(scene);
			if (legacy.ToString().empty() || !FileSystem::Exists(legacy)) {
				return false;
			}
			path = legacy;
		}
		str text = FileSystem::ReadTextFile(path, MemTag::Editor);
		if (text.empty()) {
			return false;
		}
		LinearAllocator arena(64 * 1024, MemTag::Editor);
		JSONValue root = JSONParse(text, arena);
		if (!root.IsObject()) {
			SLT_LOG_WARN("Editor", "scene annotations at '{}' are unreadable. Ignoring", path.ToString());
			return false;
		}

		HashMap<u64, u64> localToInstance{GetMemoryResource(MemTag::Editor)};
		LocalIdWalk walk{nullptr, &localToInstance};
		resolved->ForEachObject(&LocalIdWalk::Visit, &walk);
		auto instanceOf = [&localToInstance](u64 local) -> u64 {
			auto it = localToInstance.find(local);
			return it == localToInstance.end() ? 0 : it->second;
		};

		Partition& partition = Obtain(scene.GetInstanceId());
		partition.objectLabels.clear();
		partition.membership.clear();
		partition.folders.clear();
		partition.nextFolderId = static_cast<u64>(root["nextFolderId"].AsInt(1));

		const JSONValue& folders = root["folders"];
		for (size_t i = 0; i < folders.Size(); ++i) {
			const JSONValue& value = folders[i];
			FolderInfo folder;
			folder.id = static_cast<u64>(value["id"].AsInt(0));
			folder.name = str(value["name"].AsString("Folder"));
			i64 kind = value["parentKind"].AsInt(0);
			folder.parent.kind = kind >= 0 && kind <= static_cast<i64>(FolderParentKind::Folder)
									 ? static_cast<FolderParentKind>(kind)
									 : FolderParentKind::SceneRoot;
			u64 parentId = static_cast<u64>(value["parentId"].AsInt(0));
			if (folder.parent.kind == FolderParentKind::Object) {
				parentId = instanceOf(parentId);
				if (parentId == 0) {
					continue;
				}
			}
			folder.parent.id = parentId;
			folder.slot = static_cast<u32>(value["slot"].AsInt(0));
			folder.order = static_cast<u32>(value["order"].AsInt(0));
			i64 label = value["label"].AsInt(0);
			folder.label = label > 0 && label < static_cast<i64>(LabelColor::Count) ? static_cast<LabelColor>(label)
																					: LabelColor::None;
			if (folder.id != 0) {
				partition.folders.push_back(std::move(folder));
				partition.nextFolderId = std::max(partition.nextFolderId, partition.folders.back().id + 1);
			}
		}

		const JSONValue& objects = root["objects"];
		for (size_t i = 0; i < objects.Size(); ++i) {
			const JSONValue& value = objects[i];
			u64 instanceId = instanceOf(static_cast<u64>(value["id"].AsInt(0)));
			if (instanceId == 0) {
				continue;
			}
			i64 label = value["label"].AsInt(0);
			if (label > 0 && label < static_cast<i64>(LabelColor::Count)) {
				partition.objectLabels[instanceId] = static_cast<LabelColor>(label);
			}
			u64 folderId = static_cast<u64>(value["folder"].AsInt(0));
			if (folderId != 0 && FindFolder(partition, folderId)) {
				partition.membership[instanceId] = folderId;
			}
		}

		++partition.version;
		partition.dirty = false;
		if (!legacy.ToString().empty()) {
			if (SaveFor(scene)) {
				std::error_code ec;
				std::filesystem::path physical(FileSystem::Resolve(legacy).ToString());
				std::filesystem::remove(physical, ec);
				Path sidecarRootVirtual = FileSystem::Resolve(Path("project://EditorData/Scenes"));
				std::filesystem::path sidecarRoot = std::filesystem::path(sidecarRootVirtual.ToString()).lexically_normal();
				for (std::filesystem::path dir = physical.parent_path().lexically_normal();
					 dir != sidecarRoot && dir.has_relative_path(); dir = dir.parent_path()) {
					std::error_code dirEc;
					if (!std::filesystem::remove(dir, dirEc)) {
						break;
					}
				}
			}
		}
		return true;
	}

	void SceneAnnotations::FlushDirty() {
		if (!_state) {
			return;
		}
		std::erase_if(_state->partitions, [](const auto& pair) {
			return ObjectRegistry::Resolve(pair.first) == nullptr;
		});
		for (auto& [sceneId, partition] : _state->partitions) {
			if (!partition.dirty) {
				continue;
			}
			Object* raw = ObjectRegistry::Resolve(sceneId);
			if (!raw) {
				continue;
			}
			SaveFor(Handle<Scene>(static_cast<Scene*>(raw)));
		}
	}

	void SceneAnnotations::RemapIds(std::span<const EditorIdRemap> remap) {
		if (!_state || remap.empty()) {
			return;
		}
		HashMap<u64, u64> table{GetMemoryResource(MemTag::Editor)};
		for (const EditorIdRemap& row : remap) {
			table[row.oldId] = row.newId;
		}
		auto translate = [&table](u64 id) -> u64 {
			auto it = table.find(id);
			return it == table.end() ? 0 : it->second;
		};

		HashMap<u64, Partition> moved{GetMemoryResource(MemTag::Editor)};
		for (auto& [sceneId, partition] : _state->partitions) {
			u64 newSceneId = translate(sceneId);
			if (newSceneId == 0) {
				continue;
			}
			Partition rekeyed;
			rekeyed.nextFolderId = partition.nextFolderId;
			rekeyed.version = partition.version + 1;
			rekeyed.dirty = partition.dirty;
			for (const auto& [objectId, color] : partition.objectLabels) {
				if (u64 mapped = translate(objectId)) {
					rekeyed.objectLabels[mapped] = color;
				}
			}
			for (const auto& [objectId, folderId] : partition.membership) {
				if (u64 mapped = translate(objectId)) {
					rekeyed.membership[mapped] = folderId;
				}
			}
			for (const FolderInfo& folder : partition.folders) {
				FolderInfo copy = folder;
				if (copy.parent.kind == FolderParentKind::Object) {
					copy.parent.id = translate(copy.parent.id);
					if (copy.parent.id == 0) {
						continue;
					}
				}
				rekeyed.folders.push_back(std::move(copy));
			}
			moved.emplace(newSceneId, std::move(rekeyed));
		}
		_state->partitions = std::move(moved);
	}

} // namespace Shalltear
