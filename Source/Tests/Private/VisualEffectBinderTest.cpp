#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "EffectAsset.h"
#include "EffectClock.h"
#include "EffectInstance.h"
#include "EffectPass.h"
#include "ParticleSimulation.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#include "Shalltear/VisualEffects/VisualEffect.h"
#include "Shalltear/VisualEffects/VisualEffectBinder.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>

using namespace Shalltear;

SLT_DECLARE_LINKED_PACKAGE(VisualEffects)

namespace {

	using namespace VisualEffectsImpl;

	class TestClockBinder : public VisualEffectBinder {
	public:
		SLT_REFLECT_COMPONENT(TestClockBinder, VisualEffectBinder)
		SLT_END_REFLECT()

		bool valid = true;
		u32 writes = 0;
		f32 handed = 0.0f;
		f32 seen = -1.0f;

		bool IsValid(const VisualEffect&) const override { return valid; }

		void UpdateBinding(VisualEffect& effect, f32 deltaTime) override {
			++writes;
			handed = deltaTime;
			const EffectInstance* instance = EffectComponentAccess::Instance(effect);
			seen = instance ? instance->GetTime() : -1.0f;
		}
	};

	SLT_REGISTER_TYPE(TestClockBinder)

	class VisualEffectBinderFixture : public ::testing::Test {
	protected:
		char _token = 0;
		Handle<Scene> _scene;
		Vector<VisualEffectAsset*> _assets{GetMemoryResource(MemTag::Asset)};

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			SceneSystem::Initialize();
			SLT_CALL_LINKED_PACKAGE(VisualEffects, &_token);
			SetEffectTimes(0.0f, 0.0f);
			_scene = SceneSystem::CreateScene("Binders");
			ASSERT_TRUE(_scene.IsValid());
		}

		static void SetEffectTimes(f32 fixedTimeStep, f32 maxDeltaTime) {
			if (VisualEffectsSettings* settings = Settings::DeclareCategory<VisualEffectsSettings>(EFFECT_SETTINGS_PATH, SettingScope::Project)) {
				settings->fixedTimeStep = fixedTimeStep;
				settings->maxDeltaTime = maxDeltaTime;
			}
		}

		void TearDown() override {
			SetEffectTimes(1.0f / 60.0f, 0.05f);
			SceneSystem::Terminate();
			for (VisualEffectAsset* asset : _assets) {
				Delete(asset);
			}
			_assets.clear();
			ShutdownEffects();
			ShutdownSimulation();
			FramePasses::UnregisterOwnedBy(&_token);
			FrameStages::UnregisterOwnedBy(&_token);
			AssetImporterRegistry::UnregisterOwnedBy(&_token);
		}

		VisualEffectAsset* MakeAsset() {
			EffectDescription description;
			u32 offset = 96;
			const auto add = [&](const char* name, EffectPropertyType type, u32 components) {
				EffectProperty property;
				property.name = name;
				property.reference = name;
				property.type = type;
				property.offset = offset;
				property.components = components;
				description.properties.push_back(property);
				offset += 16;
			};
			add("Position", EffectPropertyType::Vector3, 3);
			add("PreviousPosition", EffectPropertyType::Vector3, 3);
			add("Velocity", EffectPropertyType::Vector3, 3);
			add("Transform_position", EffectPropertyType::Vector3, 3);
			add("Transform_angles", EffectPropertyType::Vector3, 3);
			add("Transform_scale", EffectPropertyType::Vector3, 3);
			add("Enabled", EffectPropertyType::Boolean, 1);
			description.batchesOffset = offset;
			VisualEffectAsset* asset = New<VisualEffectAsset>(MemTag::Asset);
			Vector<ShaderAsset*> shaders{GetMemoryResource(MemTag::Asset)};
			if (!EffectAssetAccess::Adopt(*asset, std::move(description), shaders)) {
				Delete(asset);
				return nullptr;
			}
			_assets.push_back(asset);
			return asset;
		}

		Handle<VisualEffect> AddEffect(VisualEffectAsset* asset, std::string_view name = "Effect") {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			Handle<VisualEffect> effect = object->AddComponent<VisualEffect>();
			effect->asset = Handle<VisualEffectAsset>(asset);
			return effect;
		}

		template <typename T>
		static Handle<T> Bind(Handle<VisualEffect> effect, Handle<SceneObject> target) {
			Handle<T> binder = effect->GetOwner()->AddComponent<T>();
			binder->target = target;
			return binder;
		}

		static void Frame(f32 delta) {
			FrameTime clockState;
			clockState.maximumDeltaTime = 1000.0f;
			Time::FrameScope clock{clockState};
			Time::BeginFrame(delta);
			FrameStages::Run(FrameStage::LateUpdate, {});
		}

		static Vec4 Bound(Handle<VisualEffect> effect, std::string_view property) {
			const f32 none = std::nanf("");
			for (const VisualEffectPropertyOverride& entry : effect->properties) {
				if (entry.name == property && entry.overridden) {
					return entry.value;
				}
			}
			return Vec4(none, none, none, none);
		}

		static Vec3 Bound3(Handle<VisualEffect> effect, std::string_view property) {
			const Vec4 value = Bound(effect, property);
			return Vec3(value.x, value.y, value.z);
		}

		static bool Near(const Vec3& a, const Vec3& b, f32 tolerance = 1e-4f) {
			return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance && std::fabs(a.z - b.z) <= tolerance;
		}
	};

	str Show(const Vec3& v) { return std::format("({}, {}, {})", v.x, v.y, v.z); }

} // anonymous namespace

TEST_F(VisualEffectBinderFixture, BinderStopsWhenTargetDies) {
	VisualEffectAsset* asset = MakeAsset();
	ASSERT_NE(asset, nullptr);
	Handle<SceneObject> leftTarget = SceneSystem::CreateObject("Left", _scene);
	Handle<SceneObject> rightTarget = SceneSystem::CreateObject("Right", _scene);
	Handle<VisualEffect> left = AddEffect(asset, "LeftEffect");
	Handle<VisualEffect> right = AddEffect(asset, "RightEffect");
	Handle<VisualEffectPositionBinder> leftBinder = Bind<VisualEffectPositionBinder>(left, leftTarget);
	Bind<VisualEffectPositionBinder>(right, rightTarget);
	SceneSystem::EndFrame();
	EXPECT_EQ(GetBinderCount(), 2u);

	for (u32 frame = 1; frame <= 3; ++frame) {
		const f32 at = static_cast<f32>(frame);
		leftTarget->SetLocalPosition(Vec3(at, 0.0f, 0.0f));
		rightTarget->SetLocalPosition(Vec3(0.0f, at, 0.0f));
		Frame(0.5f);
		EXPECT_EQ(Bound3(left, "Position"), Vec3(at, 0.0f, 0.0f)) << "frame " << frame;
		EXPECT_EQ(Bound3(right, "Position"), Vec3(0.0f, at, 0.0f)) << "frame " << frame;
	}

	SceneSystem::Destroy(leftTarget);
	SceneSystem::EndFrame();
	ASSERT_EQ(leftTarget.Get(), nullptr);
	EXPECT_FALSE(leftBinder->IsValid(*left.Get()));
	ASSERT_TRUE(left->SetVector3("Position", Vec3(9.0f, 9.0f, 9.0f)));
	rightTarget->SetLocalPosition(Vec3(0.0f, 4.0f, 0.0f));
	Frame(0.5f);
	Frame(0.5f);
	EXPECT_EQ(Bound3(left, "Position"), Vec3(9.0f, 9.0f, 9.0f));
	EXPECT_EQ(Bound3(right, "Position"), Vec3(0.0f, 4.0f, 0.0f));
}

TEST_F(VisualEffectBinderFixture, EachPositionBinderWritesValueInSpace) {
	VisualEffectAsset* asset = MakeAsset();
	ASSERT_NE(asset, nullptr);
	Handle<SceneObject> target = SceneSystem::CreateObject("Target", _scene);
	target->SetLocalPosition(Vec3(14.0f, 2.0f, 0.0f));
	const auto place = [&](Handle<VisualEffect> effect) {
		effect->GetOwner()->SetLocalPosition(Vec3(10.0f, 0.0f, 0.0f));
		effect->GetOwner()->SetLocalScale(Vec3(2.0f, 2.0f, 2.0f));
	};
	Handle<VisualEffect> world = AddEffect(asset);
	Handle<VisualEffect> automatic = AddEffect(asset);
	Handle<VisualEffect> local = AddEffect(asset);
	Handle<VisualEffect> misnamed = AddEffect(asset);
	for (Handle<VisualEffect> effect : {world, automatic, local, misnamed}) {
		place(effect);
	}
	Bind<VisualEffectPositionBinder>(world, target)->space = VisualEffectBinderSpace::World;
	Bind<VisualEffectPositionBinder>(automatic, target);
	Bind<VisualEffectPositionBinder>(local, target)->space = VisualEffectBinderSpace::Local;
	Bind<VisualEffectPositionBinder>(misnamed, target)->property = "Positon";
	Handle<VisualEffectPreviousPositionBinder> previous = Bind<VisualEffectPreviousPositionBinder>(world, target);
	Handle<VisualEffectVelocityBinder> velocity = Bind<VisualEffectVelocityBinder>(world, target);
	SceneSystem::EndFrame();

	Frame(0.5f);
	EXPECT_EQ(Bound3(world, "Position"), Vec3(14.0f, 2.0f, 0.0f));
	EXPECT_EQ(Bound3(automatic, "Position"), Vec3(14.0f, 2.0f, 0.0f));
	EXPECT_TRUE(Near(Bound3(local, "Position"), Vec3(2.0f, 1.0f, 0.0f))) << Show(Bound3(local, "Position"));
	EXPECT_TRUE(misnamed->properties.empty());
	EXPECT_EQ(Bound3(world, "PreviousPosition"), Vec3(14.0f, 2.0f, 0.0f));
	EXPECT_EQ(Bound3(world, "Velocity"), Vec3(0.0f, 0.0f, 0.0f));

	target->SetLocalPosition(Vec3(15.0f, 2.0f, 0.0f));
	Frame(0.5f);
	EXPECT_EQ(Bound3(world, "PreviousPosition"), Vec3(14.0f, 2.0f, 0.0f));
	EXPECT_TRUE(Near(Bound3(world, "Velocity"), Vec3(2.0f, 0.0f, 0.0f))) << Show(Bound3(world, "Velocity"));
	Frame(0.5f);
	EXPECT_EQ(Bound3(world, "Velocity"), Vec3(0.0f, 0.0f, 0.0f));

	velocity->target = {};
	Frame(0.5f);
	velocity->target = target;
	target->SetLocalPosition(Vec3(16.0f, 2.0f, 0.0f));
	Frame(0.5f);
	EXPECT_TRUE(Near(Bound3(world, "Velocity"), Vec3(1.0f, 0.0f, 0.0f)));
	EXPECT_EQ(Bound3(world, "PreviousPosition"), Vec3(15.0f, 2.0f, 0.0f));
	static_cast<void>(previous);
}

TEST_F(VisualEffectBinderFixture, TransformAndEnabledBindersWriteValues) {
	VisualEffectAsset* asset = MakeAsset();
	ASSERT_NE(asset, nullptr);
	Handle<SceneObject> parent = SceneSystem::CreateObject("Parent", _scene);
	Handle<SceneObject> target = SceneSystem::CreateObject("Target", _scene, parent);
	parent->SetLocalPosition(Vec3(10.0f, 0.0f, 0.0f));
	target->SetLocalPosition(Vec3(4.0f, 2.0f, 0.0f));
	const Quat turned = Quat::FromEuler(Vec3(20.0f, 30.0f, 40.0f));
	target->SetLocalRotation(turned);
	target->SetLocalScale(Vec3(1.0f, 2.0f, 3.0f));
	Handle<VisualEffect> world = AddEffect(asset);
	Handle<VisualEffect> local = AddEffect(asset);
	local->GetOwner()->SetLocalPosition(Vec3(10.0f, 0.0f, 0.0f));
	local->GetOwner()->SetLocalScale(Vec3(2.0f, 2.0f, 2.0f));
	Bind<VisualEffectTransformBinder>(world, target);
	Bind<VisualEffectTransformBinder>(local, target)->space = VisualEffectBinderSpace::Local;
	Handle<VisualEffectEnabledBinder> inHierarchy = Bind<VisualEffectEnabledBinder>(world, target);
	Handle<VisualEffectEnabledBinder> self = Bind<VisualEffectEnabledBinder>(local, target);
	self->check = VisualEffectEnabledCheck::ActiveSelf;
	SceneSystem::EndFrame();

	Frame(0.5f);
	EXPECT_TRUE(Near(Bound3(world, "Transform_position"), Vec3(14.0f, 2.0f, 0.0f))) << Show(Bound3(world, "Transform_position"));
	EXPECT_TRUE(Near(Bound3(world, "Transform_scale"), Vec3(1.0f, 2.0f, 3.0f))) << Show(Bound3(world, "Transform_scale"));
	EXPECT_TRUE(Near(Bound3(world, "Transform_angles"), Vec3(20.0f, 30.0f, 40.0f), 1e-3f)) << Show(Bound3(world, "Transform_angles"));
	const Quat back = Quat::FromEuler(Bound3(world, "Transform_angles"));
	EXPECT_NEAR(std::fabs(Quat::Dot(back, target->GetWorldRotation())), 1.0f, 1e-5f);
	EXPECT_TRUE(Near(Bound3(local, "Transform_position"), Vec3(2.0f, 1.0f, 0.0f))) << Show(Bound3(local, "Transform_position"));
	EXPECT_TRUE(Near(Bound3(local, "Transform_scale"), Vec3(0.5f, 1.0f, 1.5f))) << Show(Bound3(local, "Transform_scale"));
	EXPECT_TRUE(Near(Bound3(local, "Transform_angles"), Vec3(20.0f, 30.0f, 40.0f), 1e-3f));

	EXPECT_EQ(Bound(world, "Enabled").x, 1.0f);
	EXPECT_EQ(Bound(local, "Enabled").x, 1.0f);
	parent->SetActive(false);
	Frame(0.5f);
	EXPECT_EQ(Bound(world, "Enabled").x, 0.0f);
	EXPECT_EQ(Bound(local, "Enabled").x, 1.0f);
	static_cast<void>(inHierarchy);
}

TEST_F(VisualEffectBinderFixture, BinderSurvivesSaveAndLoad) {
	const std::filesystem::path root = std::filesystem::temp_directory_path() /
									   std::format("shalltear_binder_{}", std::chrono::steady_clock::now().time_since_epoch().count());
	std::filesystem::create_directories(root);
	FileSystem::Mount("project", Path(root.generic_string()));
	AssetDatabase::Initialize();
	{
		Handle<Scene> scene = SceneSystem::CreateScene("Bound");
		Handle<SceneObject> target = SceneSystem::CreateObject("Target", scene);
		Handle<SceneObject> holder = SceneSystem::CreateObject("Holder", scene);
		Handle<VisualEffectTransformBinder> transform = holder->AddComponent<VisualEffectTransformBinder>();
		transform->target = target;
		transform->property = "Emitter";
		transform->space = VisualEffectBinderSpace::Local;
		Handle<VisualEffectEnabledBinder> enabled = holder->AddComponent<VisualEffectEnabledBinder>();
		enabled->target = target;
		enabled->check = VisualEffectEnabledCheck::ActiveSelf;
		ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://bound.scene")));
		SceneSystem::UnloadScene(scene);
		SceneSystem::EndFrame();
	}
	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://bound.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SceneObject> holder = reloaded->FindObjectByName("Holder");
	Handle<SceneObject> target = reloaded->FindObjectByName("Target");
	ASSERT_TRUE(holder.IsValid());
	ASSERT_TRUE(target.IsValid());
	Handle<VisualEffectTransformBinder> transform = holder->GetComponent<VisualEffectTransformBinder>();
	Handle<VisualEffectEnabledBinder> enabled = holder->GetComponent<VisualEffectEnabledBinder>();
	ASSERT_TRUE(transform.IsValid());
	ASSERT_TRUE(enabled.IsValid());
	EXPECT_EQ(transform->target.Get(), target.Get());
	EXPECT_EQ(transform->property, "Emitter");
	EXPECT_EQ(transform->space, VisualEffectBinderSpace::Local);
	EXPECT_EQ(enabled->target.Get(), target.Get());
	EXPECT_EQ(enabled->check, VisualEffectEnabledCheck::ActiveSelf);
	EXPECT_EQ(GetBinderCount(), 2u);

	SceneSystem::UnloadScene(reloaded);
	SceneSystem::EndFrame();
	AssetDatabase::Terminate();
	FileSystem::Unmount("project");
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
}

TEST_F(VisualEffectBinderFixture, BindersWriteBeforeAdvance) {
	VisualEffectAsset* asset = MakeAsset();
	ASSERT_NE(asset, nullptr);
	Handle<VisualEffect> effect = AddEffect(asset);
	Handle<TestClockBinder> binder = effect->GetOwner()->AddComponent<TestClockBinder>();
	Handle<SceneObject> bare = SceneSystem::CreateObject("NoEffect", _scene);
	Handle<TestClockBinder> stray = bare->AddComponent<TestClockBinder>();
	SceneSystem::EndFrame();
	ASSERT_TRUE(binder.IsValid());
	EXPECT_EQ(GetBinderCount(), 2u);

	Frame(0.5f);
	EXPECT_EQ(binder->writes, 1u);
	EXPECT_EQ(binder->seen, 0.0f);
	EXPECT_EQ(binder->handed, 0.5f);
	Frame(0.5f);
	EXPECT_EQ(binder->seen, 0.5f);
	EXPECT_EQ(stray->writes, 0u);

	binder->valid = false;
	Frame(0.5f);
	EXPECT_EQ(binder->writes, 2u);
	binder->valid = true;
	Frame(0.25f);
	EXPECT_EQ(binder->writes, 3u);
	EXPECT_EQ(binder->handed, 0.75f);

	binder->SetEnabled(false);
	Frame(0.5f);
	EXPECT_EQ(binder->writes, 3u);
	EXPECT_EQ(GetBinderCount(), 1u);
	binder->SetEnabled(true);
	Frame(0.5f);
	EXPECT_EQ(binder->writes, 4u);
	EXPECT_EQ(binder->handed, 0.5f);

	SceneSystem::Destroy(Handle<Component>(binder.Get()));
	SceneSystem::Destroy(bare);
	SceneSystem::EndFrame();
	Frame(0.5f);
	EXPECT_EQ(GetBinderCount(), 0u);
}

#endif
