#include "Shalltear/Core/Scene/Scene.h"

#include "SceneInternal.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/Prefab.h"
#include "Shalltear/Core/Scene/SceneObject.h"

namespace Shalltear {

	using namespace SceneInternal;

	Scene::Scene() = default;

	Scene::~Scene() {
		for (PrefabInstanceRecord* record : _instances) {
			Delete(record);
		}
	}

	u32 Scene::GetRootCount() const {
		return static_cast<u32>(_roots.size());
	}

	Handle<SceneObject> Scene::GetRoot(u32 index) const {
		if (index >= _roots.size()) {
			return Handle<SceneObject>();
		}
		return Handle<SceneObject>(_roots[index]);
	}

	Handle<SceneObject> Scene::FindObjectByName(std::string_view objectName) const {
		for (SceneObject* object : _objects) {
			if (!object->IsPendingDestroy() && object->name == objectName) {
				return Handle<SceneObject>(object);
			}
		}
		return Handle<SceneObject>();
	}

	u32 Scene::GetObjectCount() const {
		u32 count = 0;
		for (const SceneObject* object : _objects) {
			if (!object->IsPendingDestroy()) {
				++count;
			}
		}
		return count;
	}

	void Scene::ForEachObject(void (*visitor)(SceneObject&, void*), void* user) const {
		if (!visitor) {
			return;
		}
		struct Walker {
			void (*visitor)(SceneObject&, void*);
			void* user;
			void Visit(SceneObject& node) const {
				if (node.IsPendingDestroy()) {
					return;
				}
				visitor(node, user);
				for (u32 i = 0; i < node.GetChildCount(); ++i) {
					if (SceneObject* child = node.GetChild(i).Get()) {
						Visit(*child);
					}
				}
			}
		};
		Walker walker{visitor, user};
		for (SceneObject* root : _roots) {
			walker.Visit(*root);
		}
	}

	namespace SceneInternal {

		namespace {

			constexpr u32 TICK_LIST_COUNT = 3;

			BehaviorTick ListBit(u32 listIndex) {
				switch (listIndex) {
				case 0:
					return BehaviorTick::Update;
				case 1:
					return BehaviorTick::FixedUpdate;
				default:
					return BehaviorTick::LateUpdate;
				}
			}

		} // anonymous namespace

		void RegisterTicks(Behavior& behavior) {
			SceneObject* owner = SceneInternalAccess::OwnerOf(behavior);
			Scene* scene = owner ? SceneInternalAccess::SceneOf(*owner) : nullptr;
			if (!scene) {
				return;
			}
			u32(&slots)[3] = SceneInternalAccess::TickSlotsOf(behavior);
			const BehaviorTick ticks = behavior.GetTickMask();
			for (u32 list = 0; list < TICK_LIST_COUNT; ++list) {
				if (!HasFlag(ticks, ListBit(list)) || slots[list] != ~0u) {
					continue;
				}
				Vector<Behavior*>& tickList = SceneInternalAccess::TickListOf(*scene, list);
				slots[list] = static_cast<u32>(tickList.size());
				tickList.push_back(&behavior);
			}
		}

		void UnregisterTicks(Behavior& behavior) {
			SceneObject* owner = SceneInternalAccess::OwnerOf(behavior);
			Scene* scene = owner ? SceneInternalAccess::SceneOf(*owner) : nullptr;
			u32(&slots)[3] = SceneInternalAccess::TickSlotsOf(behavior);
			for (u32 list = 0; list < TICK_LIST_COUNT; ++list) {
				if (slots[list] == ~0u) {
					continue;
				}
				if (scene) {
					Vector<Behavior*>& tickList = SceneInternalAccess::TickListOf(*scene, list);
					if (slots[list] < tickList.size() && tickList[slots[list]] == &behavior) {
						tickList[slots[list]] = nullptr;
					}
				}
				slots[list] = ~0u;
			}
		}

		void CompactTickLists(Scene& scene) {
			for (u32 list = 0; list < TICK_LIST_COUNT; ++list) {
				Vector<Behavior*>& tickList = SceneInternalAccess::TickListOf(scene, list);
				size_t write = 0;
				for (size_t read = 0; read < tickList.size(); ++read) {
					if (Behavior* behavior = tickList[read]) {
						SceneInternalAccess::TickSlotsOf(*behavior)[list] = static_cast<u32>(write);
						tickList[write++] = behavior;
					}
				}
				tickList.resize(write);
			}
		}

	} // namespace SceneInternal

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::Scene)
