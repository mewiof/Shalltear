#include "TestsPCH.h"

#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Scene.h"

using namespace Shalltear;

namespace {

	std::vector<str> _soLifecycleLog;

	void SoLog(const str& entry) {
		_soLifecycleLog.push_back(entry);
	}

	class SoMarkComponent : public Component {
	public:
		i32 marker = 0;

		SLT_REFLECT_COMPONENT(SoMarkComponent, Component)
		SLT_REFLECT_FIELD(marker)
		SLT_END_REFLECT()

		SoMarkComponent() = default;
	protected:
		void OnEnable() override { SoLog("enable:" + std::to_string(marker)); }
		void OnDisable() override { SoLog("disable:" + std::to_string(marker)); }
	};

	class SoDerivedMarkComponent : public SoMarkComponent {
	public:
		SLT_REFLECT_COMPONENT(SoDerivedMarkComponent, SoMarkComponent)
		SLT_END_REFLECT()

		SoDerivedMarkComponent() = default;
	};

	class SoTickBehavior : public Behavior {
	public:
		i32 updates = 0;
		i32 fixedUpdates = 0;
		i32 lateUpdates = 0;

		SLT_REFLECT_COMPONENT(SoTickBehavior, Behavior)
		SLT_END_REFLECT()
	protected:
		void OnUpdate() override { ++updates; }
		void OnFixedUpdate() override { ++fixedUpdates; }
		void OnLateUpdate() override { ++lateUpdates; }
	};

	class SoUpdateOnly : public Behavior {
	public:
		i32 updates = 0;

		SLT_REFLECT_COMPONENT(SoUpdateOnly, Behavior)
		SLT_END_REFLECT()
	protected:
		void OnUpdate() override { ++updates; }
	};

	class SoPrivateLate : public Behavior {
	public:
		i32 lateUpdates = 0;

		SLT_REFLECT_COMPONENT(SoPrivateLate, Behavior)
		SLT_END_REFLECT()
	private:
		void OnLateUpdate() override { ++lateUpdates; }
	};

	class SoFixedBase : public Behavior {
	public:
		i32 fixedUpdates = 0;

		SLT_REFLECT_COMPONENT_ABSTRACT(SoFixedBase, Behavior)
		SLT_END_REFLECT()
	protected:
		void OnFixedUpdate() override { ++fixedUpdates; }
	};

	class SoInheritsFixed : public SoFixedBase {
	public:
		SLT_REFLECT_COMPONENT(SoInheritsFixed, SoFixedBase)
		SLT_END_REFLECT()
	};

	class SoOverloaded : public Behavior {
	public:
		i32 updates = 0;

		SLT_REFLECT_COMPONENT(SoOverloaded, Behavior)
		SLT_END_REFLECT()
	protected:
		void OnUpdate() override { ++updates; }
		void OnUpdate(i32 times) { updates += times; }
	};

	class SoNoHooks : public Behavior {
	public:
		SLT_REFLECT_COMPONENT(SoNoHooks, Behavior)
		SLT_END_REFLECT()
	};

	SLT_REGISTER_TYPE(SoMarkComponent)
	SLT_REGISTER_TYPE(SoDerivedMarkComponent)
	SLT_REGISTER_TYPE(SoTickBehavior)
	SLT_REGISTER_TYPE(SoUpdateOnly)
	SLT_REGISTER_TYPE(SoPrivateLate)
	SLT_REGISTER_TYPE(SoInheritsFixed)
	SLT_REGISTER_TYPE(SoOverloaded)
	SLT_REGISTER_TYPE(SoNoHooks)

	class SceneObjectFixture : public ::testing::Test {
	protected:
		size_t _sceneBytesBaseline = 0;
		Handle<Scene> _scene;

		void SetUp() override {
			_soLifecycleLog.clear();
			SceneSystem::Initialize();
			SceneSystem::Terminate();
			_sceneBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Scene);
			SceneSystem::Initialize();
			_scene = SceneSystem::CreateScene("SceneObjectTest");
		}

		void TearDown() override {
			SceneSystem::Terminate();
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), _sceneBytesBaseline);
		}

		Handle<SceneObject> Make(const str& name, Handle<SceneObject> parent = {}) {
			return SceneSystem::CreateObject(name, _scene, parent);
		}
	};

	constexpr f32 EPS = 1e-4f;

	void ExpectVecNear(const Vec3& a, const Vec3& b, f32 eps = EPS) {
		EXPECT_NEAR(a.x, b.x, eps);
		EXPECT_NEAR(a.y, b.y, eps);
		EXPECT_NEAR(a.z, b.z, eps);
	}

} // namespace

TEST_F(SceneObjectFixture, CreateObjectLinksIntoScene) {
	Handle<SceneObject> root = Make("Root");
	ASSERT_TRUE(root.IsValid());
	EXPECT_EQ(root->GetScene().GetInstanceId(), _scene.GetInstanceId());
	EXPECT_EQ(_scene->GetRootCount(), 1u);
	EXPECT_TRUE(root->IsActiveInHierarchy());

	Handle<SceneObject> child = Make("Child", root);
	EXPECT_EQ(child->GetParent(), root);
	EXPECT_EQ(root->GetChildCount(), 1u);
	EXPECT_EQ(_scene->GetRootCount(), 1u);
	EXPECT_EQ(_scene->GetObjectCount(), 2u);
}

TEST_F(SceneObjectFixture, SetParentReparentsAndPreservesSiblingOrder) {
	Handle<SceneObject> a = Make("A");
	Handle<SceneObject> b = Make("B");
	Handle<SceneObject> c = Make("C");
	EXPECT_EQ(_scene->GetRootCount(), 3u);

	EXPECT_EQ(_scene->GetRoot(0)->name, "A");
	EXPECT_EQ(_scene->GetRoot(1)->name, "B");
	EXPECT_EQ(_scene->GetRoot(2)->name, "C");

	b->SetParent(a);
	c->SetParent(a);
	EXPECT_EQ(_scene->GetRootCount(), 1u);
	EXPECT_EQ(a->GetChild(0)->name, "B");
	EXPECT_EQ(a->GetChild(1)->name, "C");
	EXPECT_EQ(b->GetSiblingIndex(), 0u);
	EXPECT_EQ(c->GetSiblingIndex(), 1u);

	c->SetSiblingIndex(0);
	EXPECT_EQ(a->GetChild(0)->name, "C");
	EXPECT_EQ(a->GetChild(1)->name, "B");

	b->SetParent(Handle<SceneObject>());
	EXPECT_EQ(_scene->GetRootCount(), 2u);
	EXPECT_EQ(b->GetParent(), nullptr);
}

TEST_F(SceneObjectFixture, CycleReparentIsRejected) {
	Handle<SceneObject> a = Make("A");
	Handle<SceneObject> b = Make("B", a);
	Handle<SceneObject> c = Make("C", b);

	c->SetParent(a);
	EXPECT_EQ(c->GetParent(), a);
	EXPECT_EQ(a->GetChildCount(), 2u);
}

TEST_F(SceneObjectFixture, FindChildByNameIsDirectOnly) {
	Handle<SceneObject> root = Make("Root");
	Handle<SceneObject> child = Make("Arm", root);
	Make("Hand", child);

	EXPECT_EQ(root->FindChildByName("Arm"), child);
	EXPECT_EQ(root->FindChildByName("Hand"), nullptr);
	EXPECT_EQ(root->FindChildByName("Missing"), nullptr);
	EXPECT_EQ(_scene->FindObjectByName("Hand")->name, "Hand");
}

TEST_F(SceneObjectFixture, WorldMatrixMatchesHandComposedChain) {
	Handle<SceneObject> root = Make("Root");
	Handle<SceneObject> child = Make("Child", root);

	root->SetLocalPosition(Vec3(1.0f, 2.0f, 3.0f));
	root->SetLocalRotation(Quat::FromEuler(0.0f, 90.0f, 0.0f));
	root->SetLocalScale(Vec3(2.0f, 2.0f, 2.0f));
	child->SetLocalPosition(Vec3(0.0f, 0.0f, 1.0f));

	Mat4 expectedRoot = Mat4::TRS(Vec3(1.0f, 2.0f, 3.0f), Quat::FromEuler(0.0f, 90.0f, 0.0f), Vec3(2.0f, 2.0f, 2.0f));
	Mat4 expectedChild = expectedRoot * Mat4::TRS(Vec3(0.0f, 0.0f, 1.0f), Quat::Identity(), Vec3(1.0f, 1.0f, 1.0f));

	EXPECT_TRUE(Mat4::Approximately(root->GetWorldMatrix(), expectedRoot, EPS));
	EXPECT_TRUE(Mat4::Approximately(child->GetWorldMatrix(), expectedChild, EPS));

	ExpectVecNear(child->GetWorldPosition(), Vec3(3.0f, 2.0f, 3.0f));
}

TEST_F(SceneObjectFixture, ParentTRSChangeDirtiesTheWholeSubtree) {
	Handle<SceneObject> root = Make("Root");
	Handle<SceneObject> child = Make("Child", root);
	Handle<SceneObject> leaf = Make("Leaf", child);

	(void)leaf->GetWorldMatrix();
	ExpectVecNear(leaf->GetWorldPosition(), Vec3::Zero());

	root->SetLocalPosition(Vec3(5.0f, 0.0f, 0.0f));
	ExpectVecNear(leaf->GetWorldPosition(), Vec3(5.0f, 0.0f, 0.0f));
	ExpectVecNear(child->GetWorldPosition(), Vec3(5.0f, 0.0f, 0.0f));
}

TEST_F(SceneObjectFixture, WorldSettersRecomputeLocalAgainstParent) {
	Handle<SceneObject> root = Make("Root");
	Handle<SceneObject> child = Make("Child", root);
	root->SetLocalPosition(Vec3(10.0f, 0.0f, 0.0f));

	child->SetWorldPosition(Vec3(10.0f, 5.0f, 0.0f));
	ExpectVecNear(child->GetLocalPosition(), Vec3(0.0f, 5.0f, 0.0f));
	ExpectVecNear(child->GetWorldPosition(), Vec3(10.0f, 5.0f, 0.0f));

	root->SetLocalRotation(Quat::FromEuler(0.0f, 90.0f, 0.0f));
	child->SetWorldRotation(Quat::Identity());
	EXPECT_TRUE(Quat::Approximately(child->GetWorldRotation(), Quat::Identity(), EPS));
}

TEST_F(SceneObjectFixture, ReparentWorldStaysPreservesWorldTransform) {
	Handle<SceneObject> anchorA = Make("AnchorA");
	Handle<SceneObject> anchorB = Make("AnchorB");
	anchorA->SetLocalPosition(Vec3(4.0f, 0.0f, 0.0f));
	anchorB->SetLocalPosition(Vec3(0.0f, 7.0f, 0.0f));
	anchorB->SetLocalRotation(Quat::FromEuler(0.0f, 45.0f, 0.0f));

	Handle<SceneObject> mover = Make("Mover", anchorA);
	mover->SetLocalPosition(Vec3(1.0f, 1.0f, 1.0f));
	Vec3 worldBefore = mover->GetWorldPosition();
	Quat rotationBefore = mover->GetWorldRotation();

	mover->SetParent(anchorB, true);
	ExpectVecNear(mover->GetWorldPosition(), worldBefore);
	EXPECT_TRUE(Quat::Approximately(mover->GetWorldRotation(), rotationBefore, EPS));

	Vec3 localBefore = mover->GetLocalPosition();
	mover->SetParent(anchorA, false);
	ExpectVecNear(mover->GetLocalPosition(), localBefore);
	ExpectVecNear(mover->GetWorldPosition(), anchorA->TransformPoint(localBefore));
}

TEST_F(SceneObjectFixture, LossyScaleIsComponentProductUpChain) {
	Handle<SceneObject> root = Make("Root");
	Handle<SceneObject> child = Make("Child", root);
	root->SetLocalScale(Vec3(2.0f, 3.0f, 1.0f));
	child->SetLocalScale(Vec3(0.5f, 2.0f, -1.0f));

	ExpectVecNear(child->GetLossyScale(), Vec3(1.0f, 6.0f, -1.0f));
}

TEST_F(SceneObjectFixture, TransformHelpersRoundTrip) {
	Handle<SceneObject> node = Make("Node");
	node->SetLocalPosition(Vec3(1.0f, 2.0f, 3.0f));
	node->SetLocalRotation(Quat::FromEuler(0.0f, 90.0f, 0.0f));

	Vec3 world = node->TransformPoint(Vec3(0.0f, 0.0f, 1.0f));
	ExpectVecNear(world, Vec3(2.0f, 2.0f, 3.0f));

	ExpectVecNear(node->InverseTransformPoint(world), Vec3(0.0f, 0.0f, 1.0f));

	ExpectVecNear(node->TransformDirection(Vec3(0.0f, 0.0f, 1.0f)), Vec3(1.0f, 0.0f, 0.0f));
}

TEST_F(SceneObjectFixture, ActiveInHierarchyCascadesDownward) {
	Handle<SceneObject> root = Make("Root");
	Handle<SceneObject> child = Make("Child", root);
	Handle<SceneObject> leaf = Make("Leaf", child);

	EXPECT_TRUE(leaf->IsActiveInHierarchy());

	root->SetActive(false);
	EXPECT_FALSE(root->IsActiveInHierarchy());
	EXPECT_FALSE(child->IsActiveInHierarchy());
	EXPECT_FALSE(leaf->IsActiveInHierarchy());
	EXPECT_TRUE(child->IsActiveSelf());

	root->SetActive(true);
	EXPECT_TRUE(leaf->IsActiveInHierarchy());
}

TEST_F(SceneObjectFixture, EnableDisableDispatchOnlyOnEffectiveFlips) {
	Handle<SceneObject> root = Make("Root");
	Handle<SoMarkComponent> mark = root->AddComponent<SoMarkComponent>();
	ASSERT_TRUE(mark.IsValid());
	mark->marker = 1;

	_soLifecycleLog.clear();

	root->SetActive(false);
	ASSERT_EQ(_soLifecycleLog.size(), 1u);
	EXPECT_EQ(_soLifecycleLog[0], "disable:1");

	mark->SetEnabled(false);
	mark->SetEnabled(true);
	EXPECT_EQ(_soLifecycleLog.size(), 1u);

	root->SetActive(true);
	ASSERT_EQ(_soLifecycleLog.size(), 2u);
	EXPECT_EQ(_soLifecycleLog[1], "enable:1");

	root->SetActive(true);
	EXPECT_EQ(_soLifecycleLog.size(), 2u);
}

TEST_F(SceneObjectFixture, MidChainDeactivationDispatchesParentFirst) {
	Handle<SceneObject> root = Make("Root");
	Handle<SceneObject> child = Make("Child", root);
	root->AddComponent<SoMarkComponent>()->marker = 1;
	child->AddComponent<SoMarkComponent>()->marker = 2;

	_soLifecycleLog.clear();
	root->SetActive(false);
	ASSERT_EQ(_soLifecycleLog.size(), 2u);
	EXPECT_EQ(_soLifecycleLog[0], "disable:1");
	EXPECT_EQ(_soLifecycleLog[1], "disable:2");
}

TEST_F(SceneObjectFixture, GetComponentMatchesThroughBaseClasses) {
	Handle<SceneObject> node = Make("Node");
	Handle<SoDerivedMarkComponent> derived = node->AddComponent<SoDerivedMarkComponent>();
	ASSERT_TRUE(derived.IsValid());

	EXPECT_EQ(node->GetComponent<SoDerivedMarkComponent>(), derived);
	EXPECT_EQ(node->GetComponent<SoMarkComponent>().GetInstanceId(), derived.GetInstanceId());
	EXPECT_EQ(node->GetComponent<Component>().GetInstanceId(), derived.GetInstanceId());
	EXPECT_EQ(node->GetComponent<SoTickBehavior>(), nullptr);
}

TEST_F(SceneObjectFixture, GetComponentsCollectsAllMatches) {
	Handle<SceneObject> node = Make("Node");
	node->AddComponent<SoMarkComponent>();
	node->AddComponent<SoDerivedMarkComponent>();
	node->AddComponent<SoTickBehavior>();

	Vector<Handle<SoMarkComponent>> marks{GetMemoryResource(MemTag::Scene)};
	node->GetComponents(marks);
	EXPECT_EQ(marks.size(), 2u);
	EXPECT_EQ(node->GetComponentCount(), 3u);
}

TEST_F(SceneObjectFixture, GetComponentInChildrenAndParent) {
	Handle<SceneObject> root = Make("Root");
	Handle<SceneObject> child = Make("Child", root);
	Handle<SceneObject> leaf = Make("Leaf", child);
	Handle<SoMarkComponent> onChild = child->AddComponent<SoMarkComponent>();

	EXPECT_EQ(root->GetComponentInChildren<SoMarkComponent>(), onChild);
	EXPECT_EQ(leaf->GetComponentInParent<SoMarkComponent>(), onChild);
	EXPECT_EQ(leaf->GetComponentInChildren<SoMarkComponent>(), nullptr);

	child->SetActive(false);
	EXPECT_EQ(root->GetComponentInChildren<SoMarkComponent>(), nullptr);
	EXPECT_EQ(root->GetComponentInChildren<SoMarkComponent>(true), onChild);
}

TEST_F(SceneObjectFixture, AddComponentRejectsNonComponentTypes) {
	Handle<SceneObject> node = Make("Node");
	EXPECT_EQ(node->AddComponent(nullptr), nullptr);
	EXPECT_EQ(node->AddComponent(SceneObject::GetReflectType()), nullptr);
	EXPECT_EQ(node->AddComponent(Component::GetReflectType()), nullptr);
}

TEST_F(SceneObjectFixture, EveryHookAClassOverridesTicks) {
	Handle<SceneObject> node = Make("Node");
	Handle<SoTickBehavior> ticker = node->AddComponent<SoTickBehavior>();

	SceneSystem::UpdateAll();
	SceneSystem::FixedUpdateAll();
	SceneSystem::LateUpdateAll();

	EXPECT_EQ(ticker->updates, 1);
	EXPECT_EQ(ticker->fixedUpdates, 1);
	EXPECT_EQ(ticker->lateUpdates, 1);
	EXPECT_EQ(ticker->GetTickMask(), BehaviorTick::Update | BehaviorTick::FixedUpdate | BehaviorTick::LateUpdate);
}

TEST_F(SceneObjectFixture, TickMaskIsHooksClassOverrides) {
	Handle<SceneObject> node = Make("Node");
	Handle<SoUpdateOnly> updateOnly = node->AddComponent<SoUpdateOnly>();
	Handle<SoPrivateLate> privateLate = node->AddComponent<SoPrivateLate>();
	Handle<SoInheritsFixed> inherits = node->AddComponent<SoInheritsFixed>();
	Handle<SoOverloaded> overloaded = node->AddComponent<SoOverloaded>();
	Handle<SoNoHooks> noHooks = node->AddComponent<SoNoHooks>();

	EXPECT_EQ(updateOnly->GetTickMask(), BehaviorTick::Update);
	EXPECT_EQ(privateLate->GetTickMask(), BehaviorTick::LateUpdate);
	EXPECT_EQ(inherits->GetTickMask(), BehaviorTick::FixedUpdate);
	EXPECT_EQ(overloaded->GetTickMask(), BehaviorTick::Update);
	EXPECT_EQ(noHooks->GetTickMask(), BehaviorTick::None);

	SceneSystem::UpdateAll();
	SceneSystem::FixedUpdateAll();
	SceneSystem::LateUpdateAll();

	EXPECT_EQ(updateOnly->updates, 1);
	EXPECT_EQ(privateLate->lateUpdates, 1);
	EXPECT_EQ(inherits->fixedUpdates, 1);
	EXPECT_EQ(overloaded->updates, 1);
}

TEST_F(SceneObjectFixture, NonBehaviorRecordsNoTicks) {
	EXPECT_EQ(FindTypeAttribute(SoMarkComponent::GetReflectType(), BehaviorTicksAttribute), nullptr);
	EXPECT_NE(FindTypeAttribute(SoUpdateOnly::GetReflectType(), BehaviorTicksAttribute), nullptr);
}

TEST_F(SceneObjectFixture, DisabledBehaviorStopsTicking) {
	Handle<SceneObject> node = Make("Node");
	Handle<SoTickBehavior> ticker = node->AddComponent<SoTickBehavior>();

	SceneSystem::UpdateAll();
	ticker->SetEnabled(false);
	SceneSystem::UpdateAll();
	EXPECT_EQ(ticker->updates, 1);

	ticker->SetEnabled(true);
	SceneSystem::UpdateAll();
	EXPECT_EQ(ticker->updates, 2);

	node->SetActive(false);
	SceneSystem::UpdateAll();
	EXPECT_EQ(ticker->updates, 2);
}

TEST_F(SceneObjectFixture, TicksAreIsolatedPerScene) {
	Handle<Scene> other = SceneSystem::CreateScene("Other");
	Handle<SceneObject> a = SceneSystem::CreateObject("A", _scene);
	Handle<SceneObject> b = SceneSystem::CreateObject("B", other);
	Handle<SoTickBehavior> tickA = a->AddComponent<SoTickBehavior>();
	Handle<SoTickBehavior> tickB = b->AddComponent<SoTickBehavior>();

	SceneSystem::UnloadScene(other);
	SceneSystem::EndFrame();
	EXPECT_FALSE(tickB.IsValid());

	SceneSystem::UpdateAll();
	EXPECT_EQ(tickA->updates, 1);
}

TEST_F(SceneObjectFixture, CrossSceneReparentMigratesTicks) {
	Handle<Scene> other = SceneSystem::CreateScene("Other");
	Handle<SceneObject> host = SceneSystem::CreateObject("Host", other);
	Handle<SceneObject> mover = Make("Mover");
	Handle<SoTickBehavior> ticker = mover->AddComponent<SoTickBehavior>();

	mover->SetParent(host);
	EXPECT_EQ(mover->GetScene().GetInstanceId(), other.GetInstanceId());

	SceneSystem::UpdateAll();
	EXPECT_EQ(ticker->updates, 1);

	SceneSystem::UnloadScene(_scene);
	SceneSystem::EndFrame();
	EXPECT_TRUE(ticker.IsValid());
	SceneSystem::UpdateAll();
	EXPECT_EQ(ticker->updates, 2);
}

TEST_F(SceneObjectFixture, ForEachIsDepthFirst) {
	Handle<SceneObject> a = Make("A");
	Handle<SceneObject> b = Make("B");
	Make("A1", a);
	Make("A2", a);

	std::vector<str> visited;
	_scene->ForEachObject(
		[](SceneObject& object, void* user) {
			static_cast<std::vector<str>*>(user)->push_back(object.name);
		},
		&visited);

	ASSERT_EQ(visited.size(), 4u);
	EXPECT_EQ(visited[0], "A");
	EXPECT_EQ(visited[1], "A1");
	EXPECT_EQ(visited[2], "A2");
	EXPECT_EQ(visited[3], "B");
	(void)b;
}

TEST_F(SceneObjectFixture, SetComponentIndexMovesAndClamps) {
	Handle<SceneObject> object = Make("Ordered");
	Handle<SoMarkComponent> first = object.Get()->AddComponent<SoMarkComponent>();
	Handle<SoMarkComponent> second = object.Get()->AddComponent<SoMarkComponent>();
	Handle<SoMarkComponent> third = object.Get()->AddComponent<SoMarkComponent>();
	first.Get()->marker = 1;
	second.Get()->marker = 2;
	third.Get()->marker = 3;

	auto order = [&]() {
		str out;
		for (u32 i = 0; i < object.Get()->GetComponentCount(); ++i) {
			out += std::to_string(static_cast<SoMarkComponent*>(object.Get()->GetComponentAt(i).Get())->marker);
		}
		return out;
	};
	ASSERT_EQ(order(), "123");

	object.Get()->SetComponentIndex(Handle<Component>(third.Get()), 0);
	EXPECT_EQ(order(), "312");

	object.Get()->SetComponentIndex(Handle<Component>(third.Get()), 99);
	EXPECT_EQ(order(), "123");

	object.Get()->SetComponentIndex(Handle<Component>(second.Get()), 1);
	EXPECT_EQ(order(), "123");
}

TEST_F(SceneObjectFixture, SetComponentIndexIgnoresAForeignComponent) {
	Handle<SceneObject> owner = Make("Owner");
	Handle<SceneObject> other = Make("Other");
	Handle<SoMarkComponent> a = owner.Get()->AddComponent<SoMarkComponent>();
	Handle<SoMarkComponent> b = owner.Get()->AddComponent<SoMarkComponent>();
	Handle<SoMarkComponent> foreign = other.Get()->AddComponent<SoMarkComponent>();
	a.Get()->marker = 1;
	b.Get()->marker = 2;

	owner.Get()->SetComponentIndex(Handle<Component>(foreign.Get()), 0);
	EXPECT_EQ(owner.Get()->GetComponentCount(), 2u);
	EXPECT_EQ(owner.Get()->GetComponentAt(0), Handle<Component>(a.Get()));
	EXPECT_EQ(owner.Get()->GetComponentAt(1), Handle<Component>(b.Get()));
	EXPECT_EQ(other.Get()->GetComponentCount(), 1u);
	EXPECT_EQ(other.Get()->GetComponentAt(0), Handle<Component>(foreign.Get()));
}

TEST_F(SceneObjectFixture, ComponentOrderDecidesTheFirstMatch) {
	Handle<SceneObject> object = Make("Ordered");
	Handle<SoMarkComponent> base = object.Get()->AddComponent<SoMarkComponent>();
	Handle<SoDerivedMarkComponent> derived = object.Get()->AddComponent<SoDerivedMarkComponent>();
	base.Get()->marker = 1;
	derived.Get()->marker = 2;

	EXPECT_EQ(object.Get()->GetComponent<SoMarkComponent>().GetInstanceId(), base.GetInstanceId());
	object.Get()->SetComponentIndex(Handle<Component>(derived.Get()), 0);
	EXPECT_EQ(object.Get()->GetComponent<SoMarkComponent>().GetInstanceId(), derived.GetInstanceId());
}
