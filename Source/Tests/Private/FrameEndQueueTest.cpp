#include "TestsPCH.h"

#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Scene.h"

#include <atomic>

using namespace Shalltear;

namespace {

	std::vector<str> _feqLog;

	class FeqLifecycleComponent : public Component {
	public:
		i32 tag = 0;

		SLT_REFLECT_COMPONENT(FeqLifecycleComponent, Component)
		SLT_REFLECT_FIELD(tag)
		SLT_END_REFLECT()

		FeqLifecycleComponent() = default;
	protected:
		void OnAttach() override { _feqLog.push_back("attach:" + std::to_string(tag)); }
		void OnDetach() override { _feqLog.push_back("detach:" + std::to_string(tag)); }
		void OnEnable() override { _feqLog.push_back("enable:" + std::to_string(tag)); }
		void OnDisable() override { _feqLog.push_back("disable:" + std::to_string(tag)); }
		void OnDestroy() override { _feqLog.push_back("destroy:" + std::to_string(tag)); }
	};

	class FeqChainDestroyComponent : public Component {
	public:
		Handle<SceneObject> victim;

		SLT_REFLECT_COMPONENT(FeqChainDestroyComponent, Component)
		SLT_REFLECT_FIELD(victim)
		SLT_END_REFLECT()

		FeqChainDestroyComponent() = default;
	protected:
		void OnDestroy() override {
			_feqLog.push_back("chain-destroy");
			SceneSystem::Destroy(victim);
		}
	};

	SLT_REGISTER_TYPE(FeqLifecycleComponent)
	SLT_REGISTER_TYPE(FeqChainDestroyComponent)

	class FrameEndQueueFixture : public ::testing::Test {
	protected:
		size_t _sceneBytesBaseline = 0;
		Handle<Scene> _scene;

		void SetUp() override {
			_feqLog.clear();
			SceneSystem::Initialize();
			SceneSystem::Terminate();
			_sceneBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Scene);
			SceneSystem::Initialize();
			_scene = SceneSystem::CreateScene("FrameEndQueueTest");
		}

		void TearDown() override {
			SceneSystem::Terminate();
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), _sceneBytesBaseline);
		}
	};

	class FeqBusListener : public Object {
	public:
		std::vector<str> events;
		u64 unloadingSceneObjects = 0;

		void OnUnloading(const SceneUnloadingEvent& event) {
			events.push_back("unloading");
			unloadingSceneObjects = event.scene->GetObjectCount();
		}
		void OnUnloaded(const SceneUnloadedEvent&) {
			events.push_back("unloaded");
		}
	};

} // namespace

TEST_F(FrameEndQueueFixture, DestroyDefersUntilEndFrame) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Doomed", _scene);
	u64 id = object.GetInstanceId();

	SceneSystem::Destroy(object);

	EXPECT_TRUE(object.IsValid());
	EXPECT_TRUE(object->IsPendingDestroy());
	EXPECT_EQ(_scene->FindObjectByName("Doomed"), nullptr);
	EXPECT_EQ(_scene->GetObjectCount(), 0u);

	SceneSystem::EndFrame();
	EXPECT_FALSE(object.IsValid());
	EXPECT_EQ(ObjectRegistry::Resolve(id), nullptr);
}

TEST_F(FrameEndQueueFixture, DisableNowDestroyAtEndFrame) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Holder", _scene);
	Handle<FeqLifecycleComponent> component = object->AddComponent<FeqLifecycleComponent>();
	component->tag = 7;

	_feqLog.clear();
	SceneSystem::Destroy(object);
	ASSERT_EQ(_feqLog.size(), 1u);
	EXPECT_EQ(_feqLog[0], "disable:7");

	SceneSystem::EndFrame();
	ASSERT_EQ(_feqLog.size(), 3u);
	EXPECT_EQ(_feqLog[1], "detach:7");
	EXPECT_EQ(_feqLog[2], "destroy:7");
}

TEST_F(FrameEndQueueFixture, CascadeFinalizesBottomUp) {
	Handle<SceneObject> root = SceneSystem::CreateObject("Root", _scene);
	Handle<SceneObject> child = SceneSystem::CreateObject("Child", _scene, root);
	Handle<SceneObject> leaf = SceneSystem::CreateObject("Leaf", _scene, child);
	root->AddComponent<FeqLifecycleComponent>()->tag = 1;
	child->AddComponent<FeqLifecycleComponent>()->tag = 2;
	leaf->AddComponent<FeqLifecycleComponent>()->tag = 3;

	_feqLog.clear();
	SceneSystem::Destroy(root);

	ASSERT_EQ(_feqLog.size(), 3u);
	EXPECT_EQ(_feqLog[0], "disable:1");
	EXPECT_EQ(_feqLog[1], "disable:2");
	EXPECT_EQ(_feqLog[2], "disable:3");

	_feqLog.clear();
	SceneSystem::EndFrame();

	ASSERT_EQ(_feqLog.size(), 6u);
	EXPECT_EQ(_feqLog[0], "detach:3");
	EXPECT_EQ(_feqLog[1], "destroy:3");
	EXPECT_EQ(_feqLog[2], "detach:2");
	EXPECT_EQ(_feqLog[3], "destroy:2");
	EXPECT_EQ(_feqLog[4], "detach:1");
	EXPECT_EQ(_feqLog[5], "destroy:1");

	EXPECT_FALSE(root.IsValid());
	EXPECT_FALSE(child.IsValid());
	EXPECT_FALSE(leaf.IsValid());
}

TEST_F(FrameEndQueueFixture, DoubleDestroyIsIdempotent) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Once", _scene);
	Handle<FeqLifecycleComponent> component = object->AddComponent<FeqLifecycleComponent>();
	component->tag = 4;

	_feqLog.clear();
	SceneSystem::Destroy(object);
	SceneSystem::Destroy(object);
	ASSERT_EQ(_feqLog.size(), 1u);

	SceneSystem::EndFrame();
	ASSERT_EQ(_feqLog.size(), 3u);
}

TEST_F(FrameEndQueueFixture, ComponentOnlyDestroyKeepsOwnerAlive) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Owner", _scene);
	Handle<FeqLifecycleComponent> keep = object->AddComponent<FeqLifecycleComponent>();
	Handle<FeqLifecycleComponent> doomed = object->AddComponent<FeqLifecycleComponent>();
	keep->tag = 1;
	doomed->tag = 2;

	_feqLog.clear();
	SceneSystem::Destroy(Handle<Component>(doomed.Get()));
	ASSERT_EQ(_feqLog.size(), 1u);
	EXPECT_EQ(_feqLog[0], "disable:2");

	EXPECT_TRUE(object->GetComponent<FeqLifecycleComponent>() == keep);
	EXPECT_EQ(object->GetComponentCount(), 2u);

	SceneSystem::EndFrame();
	EXPECT_TRUE(object.IsValid());
	EXPECT_TRUE(keep.IsValid());
	EXPECT_FALSE(doomed.IsValid());
	EXPECT_EQ(object->GetComponentCount(), 1u);
	ASSERT_EQ(_feqLog.size(), 3u);
	EXPECT_EQ(_feqLog[1], "detach:2");
	EXPECT_EQ(_feqLog[2], "destroy:2");
}

TEST_F(FrameEndQueueFixture, DestroyFromOnDestroyDrainsSameFrame) {
	Handle<SceneObject> first = SceneSystem::CreateObject("First", _scene);
	Handle<SceneObject> second = SceneSystem::CreateObject("Second", _scene);
	Handle<FeqChainDestroyComponent> chain = first->AddComponent<FeqChainDestroyComponent>();
	chain->victim = second;
	second->AddComponent<FeqLifecycleComponent>()->tag = 9;

	_feqLog.clear();
	SceneSystem::Destroy(first);
	SceneSystem::EndFrame();

	EXPECT_FALSE(first.IsValid());
	EXPECT_FALSE(second.IsValid());

	auto chainIt = std::find(_feqLog.begin(), _feqLog.end(), "chain-destroy");
	auto destroyIt = std::find(_feqLog.begin(), _feqLog.end(), "destroy:9");
	ASSERT_NE(chainIt, _feqLog.end());
	ASSERT_NE(destroyIt, _feqLog.end());
	EXPECT_LT(chainIt - _feqLog.begin(), destroyIt - _feqLog.begin());
}

TEST_F(FrameEndQueueFixture, SceneUnloadCascadesAndPublishesInOrder) {
	Handle<SceneObject> root = SceneSystem::CreateObject("Root", _scene);
	SceneSystem::CreateObject("Child", _scene, root);
	root->AddComponent<FeqLifecycleComponent>()->tag = 1;

	FeqBusListener listener;
	Handle<FeqBusListener> self(&listener);
	EventBus& bus = EventBus::GetGlobal();
	SceneEvents::Unloading.Subscribe(bus, self, &FeqBusListener::OnUnloading);
	SceneEvents::Unloaded.Subscribe(bus, self, &FeqBusListener::OnUnloaded);

	u64 sceneId = _scene.GetInstanceId();
	_feqLog.clear();
	SceneSystem::UnloadScene(_scene);

	ASSERT_EQ(listener.events.size(), 1u);
	EXPECT_EQ(listener.events[0], "unloading");
	EXPECT_EQ(listener.unloadingSceneObjects, 2u);
	EXPECT_TRUE(_scene.IsValid());
	ASSERT_EQ(_feqLog.size(), 1u);
	EXPECT_EQ(_feqLog[0], "disable:1");

	SceneSystem::EndFrame();
	ASSERT_EQ(listener.events.size(), 2u);
	EXPECT_EQ(listener.events[1], "unloaded");
	EXPECT_FALSE(_scene.IsValid());
	EXPECT_EQ(ObjectRegistry::Resolve(sceneId), nullptr);
	EXPECT_TRUE(SceneSystem::GetLoadedScenes().empty());

	bus.UnsubscribeAll(listener.GetInstanceId());
}

TEST_F(FrameEndQueueFixture, WorkerReadsDoomedObjectSafely) {
	JobSystem::Initialize();

	Handle<SceneObject> object = SceneSystem::CreateObject("SharedWithWorker", _scene);
	object->SetLocalPosition(Vec3(1.0f, 2.0f, 3.0f));

	SceneSystem::Destroy(object);

	std::atomic<bool> resolvedAlive{false};
	std::atomic<f32> readX{0.0f};
	Handle<SceneObject> workerHandle = object;
	JobHandle job = JobSystem::Submit([workerHandle, &resolvedAlive, &readX]() {
		if (SceneObject* ptr = workerHandle.Get()) {
			resolvedAlive.store(true);
			readX.store(ptr->GetLocalPosition().x);
		}
	});

	JobSystem::WaitFor(job);
	EXPECT_TRUE(resolvedAlive.load());
	EXPECT_FLOAT_EQ(readX.load(), 1.0f);

	SceneSystem::EndFrame();
	EXPECT_FALSE(object.IsValid());

	JobSystem::Terminate();
}
