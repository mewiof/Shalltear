#include "FrameEndQueue.h"

#include "SceneInternal.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/Prefab.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <algorithm>

namespace Shalltear {

	using namespace SceneInternal;

	namespace SceneInternal {

		namespace {

			void FinalizeObject(SceneObject& node, bool dispatchHooks) {
				SystemState& state = GetState();
				Scene* scene = SceneInternalAccess::SceneOf(node);

				// rows leave the record's maps before the memory goes
				PurgeFinalizedFromRecord(node);

				Vector<SceneInternalAccess::Slot>& slots = SceneInternalAccess::ComponentsOf(node);
				for (size_t i = slots.size(); i > 0; --i) {
					Component* component = slots[i - 1].ptr;
					if (TypeRegistry::IsDerivedFrom(slots[i - 1].type, Behavior::GetReflectType())) {
						UnregisterTicks(*static_cast<Behavior*>(component));
					}
					if (dispatchHooks) {
						HookGuard guard;
						SceneInternalAccess::OnDetach(*component);
						SceneInternalAccess::OnDestroy(*component);
					}
					state.ownerById.erase(component->GetInstanceId());
					TypeRegistry::DestroyObjectInstance(component);
				}
				slots.clear();

				if (scene && SceneInternalAccess::PrefabRecordOf(node) != 0) {
					ReleaseInstanceRecord(*scene, SceneInternalAccess::PrefabRecordOf(node));
				}

				if (scene) {
					Vector<SceneObject*>& objects = SceneInternalAccess::ObjectsOf(*scene);
					auto it = std::find(objects.begin(), objects.end(), &node);
					if (it != objects.end()) {
						objects.erase(it);
					}
				}
				state.ownerById.erase(node.GetInstanceId());

				node.~SceneObject();
				Free(&node);
			}

			void FinalizeWalk(SceneObject& node, bool dispatchHooks) {
				Vector<SceneObject*>& children = SceneInternalAccess::ChildrenOf(node);
				while (!children.empty()) {
					SceneObject* child = children.front();
					children.erase(children.begin());
					FinalizeWalk(*child, dispatchHooks);
				}
				FinalizeObject(node, dispatchHooks);
			}

		} // anonymous namespace

		void FinalizeSubtree(SceneObject& root, bool dispatchHooks) {
			// unlinked first (hierarchy never dangles)
			SceneObject* parent = SceneInternalAccess::ParentOf(root);
			Scene* scene = SceneInternalAccess::SceneOf(root);
			Vector<SceneObject*>* siblings = nullptr;
			if (parent) {
				siblings = &SceneInternalAccess::ChildrenOf(*parent);
			} else if (scene) {
				siblings = &SceneInternalAccess::RootsOf(*scene);
			}
			if (siblings) {
				auto it = std::find(siblings->begin(), siblings->end(), &root);
				if (it != siblings->end()) {
					siblings->erase(it);
				}
			}
			FinalizeWalk(root, dispatchHooks);
		}

	} // namespace SceneInternal

	void FrameEndQueue::Drain() {
		SystemState& state = GetState();
		u32 iterations = 0;
		while (!_entries.empty()) {
			++iterations;
			SLT_CORE_ASSERT_MSG(iterations <= 64, "FrameEndQueue: runaway destroy cascade (64 drain iterations), something is fucked");
			if (iterations > 64) {
				_entries.clear();
				return;
			}

			Vector<Entry> batch{std::move(_entries), GetMemoryResource(MemTag::Scene)};
			_entries.clear();

			for (const Entry& entry : batch) {
				switch (entry.kind) {
				case Kind::Object: {
					SceneObject* object = static_cast<SceneObject*>(ObjectRegistry::Resolve(entry.id));
					if (object && object->IsPendingDestroy()) {
						FinalizeSubtree(*object, true);
					}
					break;
				}
				case Kind::Component: {
					Component* component = static_cast<Component*>(ObjectRegistry::Resolve(entry.id));
					if (!component || !component->IsPendingDestroy()) {
						break;
					}
					SceneObject* owner = SceneInternalAccess::OwnerOf(*component);

					if (u64 key = SceneInternalAccess::PrefabKeyOf(*component); key != 0 && owner) {
						for (SceneObject* ancestor = owner; ancestor;
							 ancestor = SceneInternalAccess::ParentOf(*ancestor)) {
							u32 index = SceneInternalAccess::PrefabRecordOf(*ancestor);
							Scene* recordScene = SceneInternalAccess::SceneOf(*ancestor);
							if (index != 0 && recordScene && index <= SceneInternalAccess::InstancesOf(*recordScene).size()) {
								if (PrefabInstanceRecord* record = SceneInternalAccess::InstancesOf(*recordScene)[index - 1]) {
									record->correspondence.erase(key);
									record->keyPaths.erase(key);
									record->keyTypes.erase(key);
								}
								break;
							}
						}
					}
					{
						HookGuard guard;
						SceneInternalAccess::OnDetach(*component);
						SceneInternalAccess::OnDestroy(*component);
					}
					if (owner) {
						Vector<SceneInternalAccess::Slot>& slots = SceneInternalAccess::ComponentsOf(*owner);
						auto it = std::find_if(slots.begin(), slots.end(),
											   [&](const SceneInternalAccess::Slot& s) { return s.ptr == component; });
						if (it != slots.end()) {
							slots.erase(it);
						}
					}
					state.ownerById.erase(component->GetInstanceId());
					TypeRegistry::DestroyObjectInstance(component);
					break;
				}
				case Kind::Scene: {
					Scene* scene = static_cast<Scene*>(ObjectRegistry::Resolve(entry.id));
					if (scene) {
						DestroySceneNow(*scene);
					}
					break;
				}
				}
			}
		}
	}

} // namespace Shalltear
