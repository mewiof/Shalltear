#include "TestsPCH.h"

#ifdef SHALLTEAR_SHARED

#include "UITestUtil.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorTransformGesture.h"
#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr const char* PACKAGE_TARGET = "Physics3D";
	constexpr f32 FIXED_STEP = 1.0f / 60.0f;

	const FieldInfo* FindTypeField(const TypeInfo* type, std::string_view fieldName) {
		const StringHash wanted(fieldName);
		for (const TypeInfo* level = type; level; level = level->parent) {
			for (u32 index = 0; index < level->fieldCount; ++index) {
				if (level->fields[index].name == wanted) {
					return &level->fields[index];
				}
			}
		}
		return nullptr;
	}

	template <typename T>
	bool WriteField(const TypeInfo* type, void* instance, std::string_view fieldName, const T& value) {
		const FieldInfo* field = FindTypeField(type, fieldName);
		if (!field || !instance || !field->addrFn) {
			return false;
		}
		*static_cast<T*>(field->addrFn(instance)) = value;
		return true;
	}

	template <typename T>
	bool ReadField(const TypeInfo* type, const void* instance, std::string_view fieldName, T& out) {
		const FieldInfo* field = FindTypeField(type, fieldName);
		if (!field || !instance || !field->addrFn) {
			return false;
		}
		out = *static_cast<const T*>(field->addrFn(const_cast<void*>(instance)));
		return true;
	}

	bool FindSettingsCategory(std::string_view path, SettingsCategoryInfo& out) {
		Vector<SettingsCategoryInfo> categories;
		Settings::GetCategories(categories);
		for (const SettingsCategoryInfo& category : categories) {
			if (category.path == path) {
				out = category;
				return true;
			}
		}
		return false;
	}

	void CollectRowTexts(UI::Context& context, UI::ElementHandle from, Vector<str>& out) {
		if (!context.IsAlive(from)) {
			return;
		}
		str text = context.GetTextString(from);
		if (!text.empty()) {
			out.push_back(std::move(text));
		}
		const UI::Element* element = context.GetElement(from);
		if (!element) {
			return;
		}
		for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			CollectRowTexts(context, handle, out);
			child = context.GetElement(handle)->nextSibling;
		}
	}

	f32 ExtentAlong(const Vector<DebugLine>& lines, const Vec3& origin, const Vec3& axis) {
		f32 extent = 0.0f;
		for (const DebugLine& line : lines) {
			extent = Math::Max(extent, Math::Abs(Vec3::Dot(line.a - origin, axis)));
			extent = Math::Max(extent, Math::Abs(Vec3::Dot(line.b - origin, axis)));
		}
		return extent;
	}

	Vec3 DrawnCenter(const Vector<DebugLine>& lines) {
		if (lines.empty()) {
			return {};
		}
		Vec3 lower = lines.front().a;
		Vec3 upper = lines.front().a;
		auto fold = [&lower, &upper](const Vec3& point) {
			lower = Vec3(Math::Min(lower.x, point.x), Math::Min(lower.y, point.y), Math::Min(lower.z, point.z));
			upper = Vec3(Math::Max(upper.x, point.x), Math::Max(upper.y, point.y), Math::Max(upper.z, point.z));
		};
		for (const DebugLine& line : lines) {
			fold(line.a);
			fold(line.b);
		}
		return (lower + upper) * 0.5f;
	}

	class Physics3DEditorFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		ModuleHandle _module;
		const void* _moduleBase = nullptr;
		const TypeInfo* _rigidbodyType = nullptr;
		const TypeInfo* _boxType = nullptr;
		const TypeInfo* _sphereType = nullptr;
		const TypeInfo* _capsuleType = nullptr;
		const TypeInfo* _meshType = nullptr;

		Vector<DebugLine> _lines;
		Vector<DebugLine> _overlay;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 unique = counter.fetch_add(1);
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_physics3d_editor_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			EditorSelection::Initialize();
			PropertyDrawers::Initialize();
			EditorGizmos::Initialize();
			EditorSceneOps::Initialize();
			EditorAssetOps::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();

			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			_moduleBase = ModuleSystem::GetNativeHandle(_module);
			ASSERT_NE(_moduleBase, nullptr);

			_rigidbodyType = TypeRegistry::FindType(StringHash("Rigidbody"));
			_boxType = TypeRegistry::FindType(StringHash("BoxCollider"));
			_sphereType = TypeRegistry::FindType(StringHash("SphereCollider"));
			_capsuleType = TypeRegistry::FindType(StringHash("CapsuleCollider"));
			_meshType = TypeRegistry::FindType(StringHash("MeshCollider"));
			ASSERT_NE(_boxType, nullptr);
			ASSERT_NE(_sphereType, nullptr);
			ASSERT_NE(_capsuleType, nullptr);
			ASSERT_NE(_meshType, nullptr);

			_scene = SceneSystem::CreateScene("physics-editor");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			EditorExtensions::Unload(_moduleBase);
			PanelManager::Terminate();
			UndoSystem::Terminate();
			EditorAssetOps::Terminate();
			EditorSceneOps::Terminate();
			EditorGizmos::Terminate();
			PropertyDrawers::Terminate();
			EditorSelection::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool LoadEditorExtensions() { return EditorExtensions::Load(_moduleBase); }

		Handle<SceneObject> MakeObject(const char* name, const Vec3& position) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			object->SetWorldPosition(position);
			return object;
		}

		void Draw() {
			_lines.clear();
			_overlay.clear();
			GizmoDrawContext context;
			context.lines = &_lines;
			context.overlayLines = &_overlay;
			context.cameraPosition = Vec3(0.0f, 5.0f, -10.0f);
			context.viewAspect = 16.0f / 9.0f;
			EditorGizmos::DrawSceneGizmos(context);
			SceneSystem::EndFrame();
		}

		void DrawWithCursor(const Ray& ray, bool inView) {
			_lines.clear();
			_overlay.clear();
			GizmoDrawContext context;
			context.lines = &_lines;
			context.overlayLines = &_overlay;
			context.cameraPosition = ray.origin;
			context.viewAspect = 16.0f / 9.0f;
			context.cursorRay = ray;
			context.cursorInView = inView;
			EditorGizmos::DrawSceneGizmos(context);
			SceneSystem::EndFrame();
		}

		static void RunFixedSteps(u32 count) {
			FrameTime clockState;
			clockState.fixedDeltaTime = FIXED_STEP;
			clockState.maximumDeltaTime = 1000.0f;
			Time::FrameScope clock{clockState};
			Time::BeginFrame(FIXED_STEP);
			FrameStageArgs args;
			for (u32 index = 0; index < count; ++index) {
				FrameStages::Run(FrameStage::FixedUpdate, args);
			}
		}
	};

} // anonymous namespace

TEST_F(Physics3DEditorFixture, GizmoPerColliderType) {
	EXPECT_EQ(EditorGizmos::FindComponentGizmo(StringHash("BoxCollider")), nullptr);

	ASSERT_TRUE(LoadEditorExtensions());

	EXPECT_NE(EditorGizmos::FindComponentGizmo(StringHash("BoxCollider")), nullptr);
	EXPECT_NE(EditorGizmos::FindComponentGizmo(StringHash("SphereCollider")), nullptr);
	EXPECT_NE(EditorGizmos::FindComponentGizmo(StringHash("CapsuleCollider")), nullptr);
	EXPECT_NE(EditorGizmos::FindComponentGizmo(StringHash("MeshCollider")), nullptr);
	EXPECT_EQ(EditorGizmos::FindComponentGizmo(StringHash("Collider")), nullptr);
}

TEST_F(Physics3DEditorFixture, UnloadSweepTakesEveryRegistrationPackageMade) {
	ASSERT_TRUE(LoadEditorExtensions());

	const u32 removed = EditorExtensions::Unload(_moduleBase);
	EXPECT_EQ(removed, 23u);
	EXPECT_EQ(EditorGizmos::FindComponentGizmo(StringHash("BoxCollider")), nullptr);
	EXPECT_EQ(PropertyDrawers::FindFieldDrawerFor(StringHash("Collider"), StringHash("_categories")), nullptr);
	EXPECT_EQ(PropertyDrawers::FindFieldDrawerFor(StringHash("Collider"), StringHash("_collidesWith")), nullptr);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(TypeRegistry::FindType(StringHash("BoxCollider"))),
			  EditorIcons::Mark::Component);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(TypeRegistry::FindType(StringHash("Light"))),
			  EditorIcons::Mark::Light);

	EXPECT_TRUE(ComponentMenu::CategoryFor(TypeRegistry::FindType(StringHash("HingeJoint"))).empty());
	EXPECT_EQ(ComponentMenu::CategoryFor(TypeRegistry::FindType(StringHash("Light"))), "Rendering");

	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("PhysicsMaterial")).icon.glyph,
			  EditorIcons::Glyph::AssetGeneric.glyph);
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("MaterialAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetMaterial.glyph);

	EXPECT_EQ(EditorExtensions::Unload(_moduleBase), 0u);
}

TEST_F(Physics3DEditorFixture, PackageContributesPhysicsDebugPanelType) {
	const TypeInfo* type = TypeRegistry::FindType(StringHash("PhysicsDebugPanel"));
	ASSERT_NE(type, nullptr);
	ASSERT_NE(type->parent, nullptr);
	EXPECT_EQ(type->parent->name, StringHash("EditorPanel"));

	Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
	ASSERT_TRUE(panel.IsValid());
	EXPECT_EQ(panel->GetTitleBase(), "Physics Debug");
}

TEST_F(Physics3DEditorFixture, BoxGizmoDrawsWhenSelected) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> object = MakeObject("box", Vec3{});
	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_size", Vec3{4.0f, 2.0f, 2.0f}));

	Draw();
	EXPECT_TRUE(_lines.empty());

	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();
	EXPECT_EQ(_lines.size(), 12u);
	EXPECT_NEAR(ExtentAlong(_lines, Vec3{}, Vec3::Right()), 2.0f, 1.0e-3f);
	EXPECT_NEAR(ExtentAlong(_lines, Vec3{}, Vec3::Up()), 1.0f, 1.0e-3f);
}

TEST_F(Physics3DEditorFixture, CompoundDrawsAllColliders) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> object = MakeObject("compound", Vec3{});
	Handle<Component> box = object->AddComponent(_boxType);
	Handle<Component> sphere = object->AddComponent(_sphereType);
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_center", Vec3{-2.0f, 0.0f, 0.0f}));
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_size", Vec3{1.0f, 1.0f, 1.0f}));
	ASSERT_TRUE(WriteField(_sphereType, sphere.Get(), "_center", Vec3{2.0f, 0.0f, 0.0f}));
	ASSERT_TRUE(WriteField(_sphereType, sphere.Get(), "_radius", 0.5f));

	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();

	EXPECT_GT(_lines.size(), 12u);

	f32 leftMost = 0.0f;
	f32 rightMost = 0.0f;
	for (const DebugLine& line : _lines) {
		leftMost = Math::Min(leftMost, Math::Min(line.a.x, line.b.x));
		rightMost = Math::Max(rightMost, Math::Max(line.a.x, line.b.x));
	}
	EXPECT_LT(leftMost, -2.0f);
	EXPECT_GT(rightMost, 2.0f);
}

TEST_F(Physics3DEditorFixture, SphereGizmoMatchesShape) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> object = MakeObject("sphere", Vec3{});
	object->SetLocalScale(Vec3{3.0f, 1.0f, 1.0f});
	object->AddComponent(_sphereType);
	EditorSelection::Select(Handle<Object>(object.Get()));

	Draw();
	ASSERT_FALSE(_lines.empty());
	EXPECT_NEAR(ExtentAlong(_lines, Vec3{}, Vec3::Right()), 1.5f, 1.0e-2f);
	EXPECT_NEAR(ExtentAlong(_lines, Vec3{}, Vec3::Up()), 1.5f, 1.0e-2f);
	EXPECT_NEAR(ExtentAlong(_lines, Vec3{}, Vec3::Forward()), 1.5f, 1.0e-2f);
}

TEST_F(Physics3DEditorFixture, CapsuleGizmoMatchesShape) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> object = MakeObject("capsule", Vec3{});
	object->AddComponent(_capsuleType);
	EditorSelection::Select(Handle<Object>(object.Get()));

	Draw();
	ASSERT_FALSE(_lines.empty());
	EXPECT_NEAR(ExtentAlong(_lines, Vec3{}, Vec3::Up()), 1.0f, 1.0e-2f);
	EXPECT_NEAR(ExtentAlong(_lines, Vec3{}, Vec3::Right()), 0.5f, 1.0e-2f);
}

TEST_F(Physics3DEditorFixture, ColliderGizmoFollowsCenterAndObjectsPose) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> object = MakeObject("offset", Vec3{5.0f, 0.0f, 0.0f});
	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_center", Vec3{0.0f, 2.0f, 0.0f}));
	EditorSelection::Select(Handle<Object>(object.Get()));

	Draw();
	ASSERT_FALSE(_lines.empty());
	Vec3 center = DrawnCenter(_lines);
	EXPECT_NEAR(center.x, 5.0f, 1.0e-3f);
	EXPECT_NEAR(center.y, 2.0f, 1.0e-3f);

	object->SetWorldRotation(Quat::FromEuler(Vec3{0.0f, 0.0f, 90.0f}));
	Draw();
	center = DrawnCenter(_lines);
	EXPECT_NEAR(center.x, 3.0f, 1.0e-2f);
	EXPECT_NEAR(center.y, 0.0f, 1.0e-2f);
}

TEST_F(Physics3DEditorFixture, MeshlessColliderDrawsNothing) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> object = MakeObject("mesh", Vec3{});
	object->AddComponent(_meshType);
	EditorSelection::Select(Handle<Object>(object.Get()));

	Draw();
	EXPECT_TRUE(_lines.empty());
}

TEST_F(Physics3DEditorFixture, DebugViewOffByDefault) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> floor = MakeObject("floor", Vec3{});
	Handle<Component> floorBox = floor->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_size", Vec3{40.0f, 1.0f, 40.0f}));
	floorBox->SetEnabled(false);
	floorBox->SetEnabled(true);
	RunFixedSteps(1);

	Draw();
	ASSERT_TRUE(_lines.empty());

	const TypeInfo* type = TypeRegistry::FindType(StringHash("PhysicsDebugPanel"));
	ASSERT_NE(type, nullptr);
	Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 800};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel->BuildContent(context, root);
	context.Update();

	UI::ElementHandle box = FindByClass(context, root, StringHash("slt-checkbox"), 0);
	ASSERT_TRUE(context.IsAlive(box));
	ClickElement(context, box);

	Draw();
#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#else
	EXPECT_FALSE(_lines.empty());
	EXPECT_NEAR(ExtentAlong(_lines, Vec3{}, Vec3::Right()), 20.0f, 0.5f);
#endif
}

TEST_F(Physics3DEditorFixture, JointFramesRowReachesSimulationsJointDrawing) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> anchor = MakeObject("anchor", Vec3{});
	anchor->AddComponent(_boxType);
	Handle<SceneObject> door = MakeObject("door", Vec3{2.0f, 0.0f, 0.0f});
	door->SetActive(false);
	door->AddComponent(_rigidbodyType);
	door->AddComponent(_boxType);
	const TypeInfo* hingeType = TypeRegistry::FindType(StringHash("HingeJoint"));
	ASSERT_NE(hingeType, nullptr);
	door->AddComponent(hingeType);
	door->SetActive(true);
	RunFixedSteps(1);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("PhysicsDebugPanel"));
	ASSERT_NE(type, nullptr);
	Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 800};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel->BuildContent(context, root);
	context.Update();

	UI::ElementHandle master = FindByClass(context, root, StringHash("slt-checkbox"), 0);
	ASSERT_TRUE(context.IsAlive(master));
	ClickElement(context, master);

	Draw();
	const size_t withoutJoints = _lines.size();

	UI::ElementHandle label = FindByText(context, root, "Joint frames");
	ASSERT_TRUE(context.IsAlive(label));
	const UI::Element* labelElement = context.GetElement(label);
	ASSERT_NE(labelElement, nullptr);
	UI::ElementHandle row = context.MakeHandle(labelElement->parent);
	ASSERT_TRUE(context.IsAlive(row));
	UI::ElementHandle jointBox = FindByClass(context, row, StringHash("slt-checkbox"), 0);
	ASSERT_TRUE(context.IsAlive(jointBox));
	ClickElement(context, jointBox);

	Draw();

#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#else
	EXPECT_GT(_lines.size(), withoutJoints);
#endif
}

namespace {

	UI::ElementHandle MakeDrawerRow(UI::Context& context, UI::ElementHandle root) {
		UI::ElementHandle row = context.CreateElement();
		UI::Style style;
		style.width = UI::Length::Pixel(320.0f);
		style.height = UI::Length::Pixel(22.0f);
		context.SetStyle(row, style);
		context.AppendChild(root, row);
		return row;
	}

} // anonymous namespace

TEST_F(Physics3DEditorFixture, CategoryDrawersAnswerForEveryColliderShape) {
	ASSERT_TRUE(LoadEditorExtensions());

	const TypeInfo* shapes[] = {_boxType, _sphereType, _capsuleType, _meshType};
	for (const TypeInfo* shape : shapes) {
		for (const char* fieldName : {"_categories", "_collidesWith"}) {
			const FieldInfo* field = FindTypeField(shape, fieldName);
			ASSERT_NE(field, nullptr) << shape->name.GetValue() << " has no " << fieldName;
			ASSERT_NE(field->ownerType, nullptr);
			EXPECT_EQ(field->ownerType->name, StringHash("Collider"));
			EXPECT_NE(PropertyDrawers::FindFieldDrawerFor(field->ownerType->name, field->name), nullptr) << fieldName << ' ' << shape->name.GetValue();
		}
	}

	const FieldInfo* trigger = FindTypeField(_boxType, "_isTrigger");
	ASSERT_NE(trigger, nullptr);
	EXPECT_EQ(PropertyDrawers::FindFieldDrawerFor(trigger->ownerType->name, trigger->name), nullptr);
}

TEST_F(Physics3DEditorFixture, ChecklistOffersProjectsVocabularyInDeclarationOrder) {
	ASSERT_TRUE(LoadEditorExtensions());

	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory("Physics", physics));
	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Default, Ground, Crate")));

	Handle<SceneObject> object = MakeObject("subject", Vec3{});
	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_categories", str("Default")));

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::ElementHandle row = MakeDrawerRow(context, root);

	const PropertyDrawerFn* drawer = PropertyDrawers::FindFieldDrawerFor(StringHash("Collider"), StringHash("_categories"));
	ASSERT_NE(drawer, nullptr);
	FieldRef ref{StringHash("Collider"), StringHash("_categories")};
	(*drawer)(context, row, ref, EditTarget(Handle<Object>(box.Get())));
	context.Update();

	UI::ElementHandle picker = FindByClass(context, row, StringHash("slt-dropdown"), 0);
	ASSERT_TRUE(context.IsAlive(picker));
	ClickElement(context, picker);
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	UI::ElementHandle content = context.GetOpenPopup(0);

	Vector<str> listed{GetMemoryResource(MemTag::Editor)};
	for (u32 index = 3; index < ChildCountOf(context, content); ++index) {
		Vector<str> texts{GetMemoryResource(MemTag::Editor)};
		CollectRowTexts(context, NthChildOf(context, content, index), texts);
		for (const str& text : texts) {
			if (!text.empty()) {
				listed.push_back(text);
			}
		}
	}
	ASSERT_EQ(listed.size(), 3u);
	EXPECT_EQ(listed[0], "Default");
	EXPECT_EQ(listed[1], "Ground");
	EXPECT_EQ(listed[2], "Crate");
}

TEST_F(Physics3DEditorFixture, UndeclaredNameShown) {
	ASSERT_TRUE(LoadEditorExtensions());

	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory("Physics", physics));
	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Default, Ground")));

	Handle<SceneObject> object = MakeObject("subject", Vec3{});
	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_categories", str("Default, Ghost")));

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::ElementHandle row = MakeDrawerRow(context, root);

	const PropertyDrawerFn* drawer = PropertyDrawers::FindFieldDrawerFor(StringHash("Collider"), StringHash("_categories"));
	ASSERT_NE(drawer, nullptr);
	FieldRef ref{StringHash("Collider"), StringHash("_categories")};
	(*drawer)(context, row, ref, EditTarget(Handle<Object>(box.Get())));
	context.Update();

	UI::ElementHandle picker = FindByClass(context, row, StringHash("slt-dropdown"), 0);
	ASSERT_TRUE(context.IsAlive(picker));
	Vector<str> buttonTexts{GetMemoryResource(MemTag::Editor)};
	CollectRowTexts(context, picker, buttonTexts);
	bool sawGhost = false;
	for (const str& text : buttonTexts) {
		if (text.find("Ghost") != str::npos) {
			sawGhost = true;
		}
	}
	EXPECT_TRUE(sawGhost);

	ClickElement(context, picker);
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	Vector<str> popupTexts{GetMemoryResource(MemTag::Editor)};
	CollectRowTexts(context, context.GetOpenPopup(0), popupTexts);
	bool marked = false;
	for (const str& text : popupTexts) {
		if (text.find("Ghost") != str::npos && text.find("undeclared") != str::npos) {
			marked = true;
		}
	}
	EXPECT_TRUE(marked);
}

TEST_F(Physics3DEditorFixture, PickWritesTypedString) {
	ASSERT_TRUE(LoadEditorExtensions());

	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory("Physics", physics));
	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Default, Ground, Crate")));

	Handle<SceneObject> object = MakeObject("subject", Vec3{});
	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_categories", str("Default")));

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::ElementHandle row = MakeDrawerRow(context, root);

	const PropertyDrawerFn* drawer = PropertyDrawers::FindFieldDrawerFor(StringHash("Collider"), StringHash("_categories"));
	ASSERT_NE(drawer, nullptr);
	FieldRef ref{StringHash("Collider"), StringHash("_categories")};
	(*drawer)(context, row, ref, EditTarget(Handle<Object>(box.Get())));
	context.Update();

	UI::ElementHandle picker = FindByClass(context, row, StringHash("slt-dropdown"), 0);
	ASSERT_TRUE(context.IsAlive(picker));
	ClickElement(context, picker);
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	UI::ElementHandle content = context.GetOpenPopup(0);

	ClickElement(context, NthChildOf(context, content, 5));

	str written;
	ASSERT_TRUE(ReadField(_boxType, box.Get(), "_categories", written));
	EXPECT_EQ(written, "Default, Crate");

	EXPECT_EQ(context.GetOpenPopupCount(), 1u);
	ClickElement(context, NthChildOf(context, content, 4));
	ASSERT_TRUE(ReadField(_boxType, box.Get(), "_categories", written));
	EXPECT_EQ(written, "Default, Ground, Crate");

	UndoSystem::Undo();
	ASSERT_TRUE(ReadField(_boxType, box.Get(), "_categories", written));
	EXPECT_EQ(written, "Default, Crate");
}

TEST_F(Physics3DEditorFixture, PickAfterUndoReadsField) {
	ASSERT_TRUE(LoadEditorExtensions());

	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory("Physics", physics));
	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Default, Ground, Crate")));

	Handle<SceneObject> object = MakeObject("subject", Vec3{});
	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_categories", str("Default")));

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::ElementHandle row = MakeDrawerRow(context, root);

	const PropertyDrawerFn* drawer = PropertyDrawers::FindFieldDrawerFor(StringHash("Collider"), StringHash("_categories"));
	ASSERT_NE(drawer, nullptr);
	FieldRef ref{StringHash("Collider"), StringHash("_categories")};
	(*drawer)(context, row, ref, EditTarget(Handle<Object>(box.Get())));
	context.Update();

	UI::ElementHandle picker = FindByClass(context, row, StringHash("slt-dropdown"), 0);
	ASSERT_TRUE(context.IsAlive(picker));
	ClickElement(context, picker);
	UI::ElementHandle content = context.GetOpenPopup(0);
	ClickElement(context, NthChildOf(context, content, 4));

	str written;
	ASSERT_TRUE(ReadField(_boxType, box.Get(), "_categories", written));
	ASSERT_EQ(written, "Default, Ground");

	UndoSystem::Undo();
	ASSERT_TRUE(ReadField(_boxType, box.Get(), "_categories", written));
	ASSERT_EQ(written, "Default");
	PropertyDrawers::RefreshRows(context);
	context.Update();

	ClickElement(context, NthChildOf(context, content, 5));
	ASSERT_TRUE(ReadField(_boxType, box.Get(), "_categories", written));
	EXPECT_EQ(written, "Default, Crate");
}

TEST_F(Physics3DEditorFixture, PickKeepsLateName) {
	ASSERT_TRUE(LoadEditorExtensions());

	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory("Physics", physics));
	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Default, Ground")));

	Handle<SceneObject> object = MakeObject("subject", Vec3{});
	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_categories", str("Default")));

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::ElementHandle row = MakeDrawerRow(context, root);

	const PropertyDrawerFn* drawer = PropertyDrawers::FindFieldDrawerFor(StringHash("Collider"), StringHash("_categories"));
	ASSERT_NE(drawer, nullptr);
	FieldRef ref{StringHash("Collider"), StringHash("_categories")};
	(*drawer)(context, row, ref, EditTarget(Handle<Object>(box.Get())));
	context.Update();

	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_categories", str("Default, Phantom")));

	UI::ElementHandle picker = FindByClass(context, row, StringHash("slt-dropdown"), 0);
	ClickElement(context, picker);
	UI::ElementHandle content = context.GetOpenPopup(0);
	ClickElement(context, NthChildOf(context, content, 4));

	str written;
	ASSERT_TRUE(ReadField(_boxType, box.Get(), "_categories", written));
	EXPECT_EQ(written, "Default, Ground, Phantom");
}

namespace {

	void CollectText(UI::Context& context, UI::ElementHandle from, str& out) {
		out += context.GetTextString(from);
		out += '\n';
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			CollectText(context, handle, out);
			child = context.GetElement(handle)->nextSibling;
		}
	}

} // anonymous namespace

TEST_F(Physics3DEditorFixture, MouseSelectReportsRayHit) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> target = MakeObject("target", Vec3{0.0f, 0.0f, 10.0f});
	Handle<Component> sphere = target->AddComponent(_sphereType);
	ASSERT_TRUE(sphere.IsValid());
	RunFixedSteps(1);

	const Ray atTarget{Vec3{}, Vec3::Forward()};
	DrawWithCursor(atTarget, true);
	ASSERT_TRUE(_overlay.empty());

	const TypeInfo* type = TypeRegistry::FindType(StringHash("PhysicsDebugPanel"));
	ASSERT_NE(type, nullptr);
	Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 800};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel->BuildContent(context, root);
	context.Update();

	UI::ElementHandle box = FindByClass(context, root, StringHash("slt-checkbox"), 1);
	ASSERT_TRUE(context.IsAlive(box));
	ClickElement(context, box);

	DrawWithCursor(atTarget, true);
	EXPECT_FALSE(_overlay.empty());
	for (const DebugLine& line : _overlay) {
		EXPECT_GT(line.a.z, 8.0f);
	}

	DrawWithCursor(atTarget, false);
	EXPECT_TRUE(_overlay.empty());
}

TEST_F(Physics3DEditorFixture, DebugPanelNamesHovered) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> target = MakeObject("HitMe", Vec3{0.0f, 0.0f, 10.0f});
	ASSERT_TRUE(target->AddComponent(_sphereType).IsValid());
	RunFixedSteps(1);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("PhysicsDebugPanel"));
	ASSERT_NE(type, nullptr);
	Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 800};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel->BuildContent(context, root);
	context.Update();

	str before;
	CollectText(context, root, before);
	EXPECT_NE(before.find("mouse select is off"), str::npos);

	ClickElement(context, FindByClass(context, root, StringHash("slt-checkbox"), 1));
	DrawWithCursor(Ray{Vec3{}, Vec3::Forward()}, true);
	panel->OnTick(1.0f);
	context.Update();

	str after;
	CollectText(context, root, after);
	EXPECT_NE(after.find("HitMe"), str::npos);
	EXPECT_NE(after.find("SphereCollider"), str::npos);
	EXPECT_NE(after.find("static geometry"), str::npos);

	DrawWithCursor(Ray{Vec3{}, Vec3::Forward()}, false);
	panel->OnTick(1.0f);
	context.Update();

	str away;
	CollectText(context, root, away);
	EXPECT_EQ(away.find("HitMe"), str::npos);
	EXPECT_NE(away.find("under cursor: nothing"), str::npos);
}

TEST_F(Physics3DEditorFixture, HoldingTransformStopsSimulationFightingIt) {
	ASSERT_TRUE(LoadEditorExtensions());

	auto pinForASecondThenLetGo = [this](bool withGesture) {
		Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, -0.5f, 0.0f});
		floor->SetLocalScale(Vec3{40.0f, 1.0f, 40.0f});
		floor->AddComponent(_boxType);

		const Vec3 pinned{0.0f, 4.0f, 0.0f};
		Handle<SceneObject> box = MakeObject("held", pinned);
		box->AddComponent(_boxType);
		box->AddComponent(_rigidbodyType);

		if (withGesture) {
			const Handle<SceneObject> grabbed[] = {box};
			EditorTransformGesture::Begin(grabbed);
		}
		for (u32 step = 0; step < 60; ++step) {
			box->SetWorldPosition(pinned);
			RunFixedSteps(1);
		}
		const Vec3 whileHeld = box->GetWorldPosition();
		EditorTransformGesture::End();

		const Vec3 before = box->GetWorldPosition();
		RunFixedSteps(1);
		const f32 released = (box->GetWorldPosition() - before).Length();

		SceneSystem::Destroy(floor);
		SceneSystem::Destroy(box);
		SceneSystem::EndFrame();
		return std::pair<Vec3, f32>{whileHeld, released};
	};

	const auto [unheldPose, unheld] = pinForASecondThenLetGo(false);
	const auto [heldPose, held] = pinForASecondThenLetGo(true);

	EXPECT_GT(unheld, 0.1f);

	EXPECT_LT(held, 0.02f) << held;
	EXPECT_GT(held, 0.0f);

	EXPECT_NEAR(heldPose.y, 4.0f, 1e-3f);
	EXPECT_GT(4.0f - unheldPose.y, 0.05f);
}

TEST_F(Physics3DEditorFixture, GestureOnParentHoldsBodiesBeneathIt) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> root = MakeObject("root", Vec3{0.0f, 4.0f, 0.0f});
	Handle<SceneObject> child = MakeObject("child", Vec3{0.0f, 4.0f, 0.0f});
	child->SetParent(root);
	child->AddComponent(_boxType);
	child->AddComponent(_rigidbodyType);

	const Handle<SceneObject> grabbed[] = {root};
	EditorTransformGesture::Begin(grabbed);
	for (u32 step = 0; step < 60; ++step) {
		root->SetWorldPosition(Vec3{0.0f, 4.0f, 0.0f});
		RunFixedSteps(1);
	}
	EditorTransformGesture::End();

	const Vec3 before = child->GetWorldPosition();
	RunFixedSteps(1);
	EXPECT_LT((child->GetWorldPosition() - before).Length(), 0.02f);
}

TEST_F(Physics3DEditorFixture, DragThroughGroundIsReconciledOnRelease) {
	ASSERT_TRUE(LoadEditorExtensions());

	Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, -0.5f, 0.0f});
	floor->SetLocalScale(Vec3{40.0f, 1.0f, 40.0f});
	floor->AddComponent(_boxType);

	Handle<SceneObject> box = MakeObject("box", Vec3{0.0f, 0.5f, 0.0f});
	box->AddComponent(_boxType);
	Handle<Component> body = box->AddComponent(_rigidbodyType);
	WriteField(_rigidbodyType, body.Get(), "_mass", 12.0f);

	RunFixedSteps(60);
	const f32 resting = box->GetWorldPosition().y;
	ASSERT_NEAR(resting, 0.5f, 0.01f);

	const Handle<SceneObject> grabbed[] = {box};
	EditorTransformGesture::Begin(grabbed);
	for (u32 step = 0; step < 40; ++step) {
		box->SetWorldPosition(Vec3{0.0f, 0.5f - 0.05f * static_cast<f32>(step), 0.0f});
		RunFixedSteps(1);
	}
	EXPECT_LT(box->GetWorldPosition().y, -1.0f);

	EditorTransformGesture::End();

	EXPECT_GT(box->GetWorldPosition().y, 0.0f) << box->GetWorldPosition().y;

	RunFixedSteps(180);
	EXPECT_NEAR(box->GetWorldPosition().y, resting, 0.05f);
}
TEST_F(Physics3DEditorFixture, SteplessGesturePlacesBody) {
	ASSERT_TRUE(LoadEditorExtensions());

	const Vec3 dropped{0.0f, 0.0f, 10.0f};
	const Quat turned = Quat::AngleAxis(40.0f, Vec3::Up());

	Handle<SceneObject> box = MakeObject("box", Vec3{6.0f, 0.0f, 10.0f});
	box->AddComponent(_sphereType);
	box->AddComponent(_rigidbodyType);
	RunFixedSteps(1);

	const Handle<SceneObject> grabbed[] = {box};
	EditorTransformGesture::Begin(grabbed);
	box->SetWorldPosition(dropped);
	box->SetWorldRotation(turned);
	EditorTransformGesture::End();

	EXPECT_LT((box->GetWorldPosition() - dropped).Length(), 1e-3f) << box->GetWorldPosition().x;
	EXPECT_GT(std::abs(Quat::Dot(box->GetWorldRotation(), turned)), 0.9999f);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("PhysicsDebugPanel"));
	ASSERT_NE(type, nullptr);
	Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
	ASSERT_TRUE(panel.IsValid());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 800};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel->BuildContent(context, root);
	context.Update();

	ClickElement(context, FindByClass(context, root, StringHash("slt-checkbox"), 1));
	DrawWithCursor(Ray{Vec3{}, Vec3::Forward()}, true);
	panel->OnTick(1.0f);
	context.Update();

	str readout;
	CollectText(context, root, readout);
	EXPECT_NE(readout.find("box"), str::npos);
}

#endif
