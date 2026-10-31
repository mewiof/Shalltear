#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"
#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr const char* V1_DLL = "ShalltearHotReloadModuleV1.dll";
	constexpr const char* V2_DLL = "ShalltearHotReloadModuleV2.dll";
	constexpr const char* GONE_DLL = "ShalltearHotReloadModuleGone.dll";

	const StringHash PanelType{"HotReloadEditorPanel"};

	class EditorExtensionReloadFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		ModuleHandle _module;
		bool _haveModulePanel = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_exreload_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));

			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			EditorSelection::Initialize();
			PropertyDrawers::Initialize();
			PanelManager::Initialize();

			_module = ModuleSystem::Load(Path(V1_DLL));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			_haveModulePanel = TypeRegistry::FindType(PanelType) != nullptr;
		}

		void TearDown() override {
			if (ModuleSystem::IsLoaded(_module)) {
				EditorExtensions::Unload(ModuleBase());
			}
			PanelManager::Terminate();
			PropertyDrawers::Terminate();
			EditorSelection::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		const void* ModuleBase() const { return ModuleSystem::GetNativeHandle(_module); }

		Handle<EditorPanel> FindOpenProbe() const {
			Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
			PanelManager::GetOpenPanels(panels);
			for (Handle<EditorPanel> handle : panels) {
				EditorPanel* panel = handle.Get();
				if (panel && panel->GetType() && panel->GetType()->name == PanelType) {
					return handle;
				}
			}
			return {};
		}

		static i32 PanelMagic(EditorPanel* panel) {
			Variant result;
			EXPECT_TRUE(InvokeMethod(panel, panel->GetType(), StringHash("GetPanelMagic"), nullptr, 0, &result));
			const i32* value = result.TryGet<i32>();
			return value ? *value : -1;
		}

		template <typename T>
		static T GetPanelField(EditorPanel* panel, StringHash field) {
			Variant out;
			EXPECT_TRUE(GetField(panel, panel->GetType(), field, &out));
			const T* value = out.TryGet<T>();
			return value ? *value : T{};
		}

		template <typename T>
		static void SetPanelField(EditorPanel* panel, StringHash field, const T& value) {
			Variant boxed{value};
			EXPECT_TRUE(SetField(panel, panel->GetType(), field, &boxed));
		}

		bool ReloadThrough(const char* binary) {
			EventBus& bus = EventBus::GetGlobal();
			EditorEvents::ModuleReloadBegan.Publish(bus, {ModuleBase()});
			Path path{binary};
			bool succeeded = ModuleSystem::Reload(_module, &path);
			EditorEvents::ModuleReloadEnded.Publish(bus, {succeeded, ModuleBase()});
			return succeeded;
		}
	};

} // anonymous namespace

TEST_F(EditorExtensionReloadFixture, ModulePanelTypeAppearsInWindowMenu) {
	if (!_haveModulePanel) {
		GTEST_SKIP() << "module editor panels are shared link only";
	}
	const TypeInfo* type = TypeRegistry::FindType(PanelType);
	ASSERT_NE(type, nullptr);
	EXPECT_EQ(PanelManager::GetPanelTypeName(type), "Hot Reload Probe");
	EXPECT_EQ(type->ownerModule, ModuleBase());

	Vector<const TypeInfo*> types{GetMemoryResource(MemTag::Editor)};
	PanelManager::GetPanelTypes(types);
	bool found = false;
	for (const TypeInfo* candidate : types) {
		found |= candidate == type;
	}
	EXPECT_TRUE(found);
}

TEST_F(EditorExtensionReloadFixture, OpenModulePanelSurvivesTheReload) {
	if (!_haveModulePanel) {
		GTEST_SKIP() << "module editor panels are shared link only";
	}
	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(PanelType));
	ASSERT_TRUE(panel.IsValid());
	StringHash dockIdBefore = panel.Get()->GetDockId();
	u32 serialBefore = panel.Get()->GetInstanceSerial();
	SetPanelField<str>(panel.Get(), StringHash("note"), "carried across");
	SetPanelField<i32>(panel.Get(), StringHash("tickCount"), 17);
	EXPECT_EQ(PanelMagic(panel.Get()), 1);

	ASSERT_TRUE(ReloadThrough(V2_DLL));

	Handle<EditorPanel> restored = FindOpenProbe();
	ASSERT_TRUE(restored.IsValid());
	EXPECT_FALSE(panel.IsValid());
	EXPECT_EQ(restored.Get()->GetInstanceSerial(), serialBefore);
	EXPECT_EQ(restored.Get()->GetDockId(), dockIdBefore);
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 1u);
	EXPECT_EQ(PanelManager::GetCapturedPanelCount(), 0u);

	EXPECT_EQ(GetPanelField<str>(restored.Get(), StringHash("note")), "carried across");
	EXPECT_EQ(GetPanelField<i32>(restored.Get(), StringHash("tickCount")), 17);
	EXPECT_EQ(PanelMagic(restored.Get()), 2);
	EXPECT_EQ(restored.Get()->GetType()->ownerModule, ModuleBase());

	PanelManager::TickAll(0.016f);
	EXPECT_EQ(GetPanelField<i32>(restored.Get(), StringHash("tickCount")), 18);
}

TEST_F(EditorExtensionReloadFixture, HostPanelsAreUntouchedByTheSweep) {
	RegisterBuiltInPanels();
	Handle<EditorPanel> console = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("ConsolePanel")));
	ASSERT_TRUE(console.IsValid());
	if (_haveModulePanel) {
		PanelManager::OpenPanel(TypeRegistry::FindType(PanelType));
	}
	u32 openBefore = PanelManager::GetOpenPanelCount();

	ASSERT_TRUE(ReloadThrough(V2_DLL));

	EXPECT_TRUE(console.IsValid());
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), openBefore);
}

TEST_F(EditorExtensionReloadFixture, CaptureIgnoresUnrelatedImages) {
	if (!_haveModulePanel) {
		GTEST_SKIP() << "module editor panels are shared link only";
	}
	PanelManager::OpenPanel(TypeRegistry::FindType(PanelType));
	ASSERT_EQ(PanelManager::GetOpenPanelCount(), 1u);

	int elsewhere = 0;
	EXPECT_EQ(PanelManager::CaptureModulePanels(&elsewhere), 0u);
	EXPECT_EQ(PanelManager::CaptureModulePanels(nullptr), 0u);
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 1u);
	EXPECT_EQ(PanelManager::GetCapturedPanelCount(), 0u);
}

TEST_F(EditorExtensionReloadFixture, DrawerRegistrationsAreSweptByOwner) {
	const void* moduleBase = ModuleBase();
	ASSERT_NE(moduleBase, nullptr);
	PropertyDrawers::RegisterFieldDrawer(StringHash("ModuleFieldType"), [](UI::Context&, UI::ElementHandle, FieldRef, EditTarget) {}, moduleBase);
	PropertyDrawers::RegisterComponentInspector(StringHash("ModuleComponent"), [](UI::Context&, UI::ElementHandle, Handle<Component>) {}, moduleBase);
	PropertyDrawers::RegisterFieldDrawer(StringHash("HostFieldType"),
										 [](UI::Context&, UI::ElementHandle, FieldRef, EditTarget) {});
	ASSERT_NE(PropertyDrawers::FindFieldDrawer(StringHash("ModuleFieldType")), nullptr);

	ASSERT_TRUE(ReloadThrough(V2_DLL));

	EXPECT_EQ(PropertyDrawers::FindFieldDrawer(StringHash("ModuleFieldType")), nullptr);
	EXPECT_EQ(PropertyDrawers::FindComponentInspector(StringHash("ModuleComponent")), nullptr);
	EXPECT_NE(PropertyDrawers::FindFieldDrawer(StringHash("HostFieldType")), nullptr);
}

TEST_F(EditorExtensionReloadFixture, VanishedPanelTypesDropWithTheirInstances) {
	if (!_haveModulePanel) {
		GTEST_SKIP() << "module editor panels are shared link only";
	}
	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(PanelType));
	ASSERT_TRUE(panel.IsValid());

	EXPECT_EQ(PanelManager::CaptureModulePanels(ModuleBase()), 1u);
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 0u);
	EXPECT_EQ(PanelManager::GetCapturedPanelCount(), 1u);
	ASSERT_TRUE(ModuleSystem::Unload(_module));

	EXPECT_EQ(PanelManager::RestoreModulePanels(), 0u);
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 0u);
	EXPECT_EQ(PanelManager::GetCapturedPanelCount(), 0u);
}

TEST_F(EditorExtensionReloadFixture, ReloadCyclesLeaveNoResidue) {
	if (!_haveModulePanel) {
		GTEST_SKIP() << "module editor panels are shared link only";
	}
	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(PanelType));
	ASSERT_TRUE(panel.IsValid());
	SetPanelField<str>(panel.Get(), StringHash("note"), "round trip");

	for (int cycle = 0; cycle < 3; ++cycle) {
		ASSERT_TRUE(ReloadThrough(V2_DLL));
		ASSERT_TRUE(ReloadThrough(V1_DLL));
		Handle<EditorPanel> live = FindOpenProbe();
		ASSERT_TRUE(live.IsValid()) << "cycle " << cycle;
		EXPECT_EQ(PanelManager::GetOpenPanelCount(), 1u) << "cycle " << cycle;
		EXPECT_EQ(live.Get()->GetInstanceSerial(), 1u);
		EXPECT_EQ(GetPanelField<str>(live.Get(), StringHash("note")), "round trip");
		EXPECT_EQ(PanelMagic(live.Get()), 1);
	}
}

namespace {

	class EditorExtensionDockFixture : public EditorExtensionReloadFixture {
	protected:
		Handle<Window> _window;

		void SetUp() override {
			EditorExtensionReloadFixture::SetUp();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			PanelManager::Terminate();
			PanelManager::Initialize();
			RegisterBuiltInPanels();

			WindowDesc desc;
			desc.title = "ExtensionReloadDock";
			desc.clientSize = {640, 480};
			desc.flags = WindowFlags::Hidden;
			_window = WindowSystem::Create(desc);
			ASSERT_TRUE(_window.IsValid());
			UISystem::CreateContext(_window);
			ASSERT_NE(UISystem::CreateDockManager(_window), nullptr);
			PanelManager::SetDefaultWindow(_window);
		}

		void TearDown() override {
			PanelManager::Terminate();
			if (_window.IsValid()) {
				WindowSystem::RequestDestroy(_window);
				WindowSystem::PumpEvents();
			}
			UISystem::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			EditorExtensionReloadFixture::TearDown();
		}
	};

} // anonymous namespace

TEST_F(EditorExtensionDockFixture, RestoredPanelReturnsToItsDockLeaf) {
	if (!_haveModulePanel) {
		GTEST_SKIP() << "module editor panels are shared link only";
	}
	UI::DockManager* dock = UISystem::GetDockManager(_window);
	ASSERT_NE(dock, nullptr);

	ASSERT_TRUE(PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("HierarchyPanel")), _window).IsValid());
	Handle<EditorPanel> host = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("ConsolePanel")), _window);
	ASSERT_TRUE(host.IsValid());
	dock->DockPanel(host.Get()->GetDockId(), dock->GetRootNode(), UI::DockZone::Left);
	u32 leaf = dock->FindNodeOfPanel(host.Get()->GetDockId());
	ASSERT_NE(leaf, UI::INVALID_DOCK_NODE);
	ASSERT_NE(leaf, dock->GetRootNode());

	Handle<EditorPanel> probe = PanelManager::OpenPanel(TypeRegistry::FindType(PanelType), _window);
	ASSERT_TRUE(probe.IsValid());
	dock->DockPanel(probe.Get()->GetDockId(), leaf, UI::DockZone::Center);
	ASSERT_EQ(dock->FindNodeOfPanel(probe.Get()->GetDockId()), leaf);
	ASSERT_TRUE(dock->ActivatePanel(host.Get()->GetDockId()));

	ASSERT_TRUE(ReloadThrough(V2_DLL));

	Handle<EditorPanel> restored = FindOpenProbe();
	ASSERT_TRUE(restored.IsValid());
	EXPECT_TRUE(dock->IsPanelDocked(restored.Get()->GetDockId()));
	EXPECT_EQ(dock->FindNodeOfPanel(restored.Get()->GetDockId()),
			  dock->FindNodeOfPanel(host.Get()->GetDockId()));
	EXPECT_EQ(dock->GetActivePanel(dock->FindNodeOfPanel(host.Get()->GetDockId())), host.Get()->GetDockId());
}

TEST_F(EditorExtensionDockFixture, LoneModulePanelReturnsToLeaf) {
	if (!_haveModulePanel) {
		GTEST_SKIP() << "module editor panels are shared link only";
	}
	UI::DockManager* dock = UISystem::GetDockManager(_window);
	ASSERT_NE(dock, nullptr);

	ASSERT_TRUE(PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("HierarchyPanel")), _window).IsValid());
	Handle<EditorPanel> probe = PanelManager::OpenPanel(TypeRegistry::FindType(PanelType), _window);
	ASSERT_TRUE(probe.IsValid());
	dock->DockPanel(probe.Get()->GetDockId(), dock->GetRootNode(), UI::DockZone::Right);
	const u32 leaf = dock->FindNodeOfPanel(probe.Get()->GetDockId());
	ASSERT_NE(leaf, dock->GetRootNode());
	Vector<UI::DockNodeDesc> before{GetMemoryResource(MemTag::Editor)};
	dock->SaveLayout(before);

	ASSERT_TRUE(ReloadThrough(V2_DLL));

	Handle<EditorPanel> restored = FindOpenProbe();
	ASSERT_TRUE(restored.IsValid());
	EXPECT_EQ(dock->FindNodeOfPanel(restored.Get()->GetDockId()), leaf);
	Vector<UI::DockNodeDesc> after{GetMemoryResource(MemTag::Editor)};
	dock->SaveLayout(after);
	ASSERT_EQ(after.size(), before.size());
	for (size_t i = 0; i < after.size(); ++i) {
		EXPECT_EQ(after[i].split, before[i].split) << "node " << i;
		EXPECT_EQ(after[i].tabs.size(), before[i].tabs.size()) << "node " << i;
	}
}

namespace {

	bool AnyTextEquals(UI::Context& context, UI::ElementHandle from, std::string_view wanted) {
		if (context.GetTextString(from) == wanted) {
			return true;
		}
		const UI::Element* element = context.GetElement(from);
		if (!element) {
			return false;
		}
		for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			if (AnyTextEquals(context, handle, wanted)) {
				return true;
			}
			child = context.GetElement(handle)->nextSibling;
		}
		return false;
	}

} // anonymous namespace

TEST_F(EditorExtensionDockFixture, InspectorRowsFollowAReloadedComponentLayout) {
	const TypeInfo* behaviorType = TypeRegistry::FindType(StringHash("HotReloadBehavior"));
	ASSERT_NE(behaviorType, nullptr);

	Handle<Scene> scene = SceneSystem::CreateScene("Reload");
	Handle<SceneObject> object = SceneSystem::CreateObject("Subject", scene);
	ASSERT_TRUE(object->AddComponent(behaviorType).IsValid());
	EditorSelection::Select(Handle<Object>(object.Get()));

	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")), _window);
	ASSERT_TRUE(panel.IsValid());

	UI::Context* context = UISystem::GetContext(_window);
	ASSERT_NE(context, nullptr);
	UISystem::Update();

	UI::ElementHandle root = context->GetRoot();
	ASSERT_TRUE(AnyTextEquals(*context, root, "speed"));
	ASSERT_FALSE(AnyTextEquals(*context, root, "added"));

	ASSERT_TRUE(ReloadThrough(V2_DLL));
	PanelManager::TickAll(0.016f);
	UISystem::Update();

	EXPECT_TRUE(AnyTextEquals(*context, root, "added"));
	EXPECT_FALSE(AnyTextEquals(*context, root, "speed"));
}

TEST_F(EditorExtensionDockFixture, InspectorRebuildsAfterReloadThatChangedNothing) {
	const TypeInfo* behaviorType = TypeRegistry::FindType(StringHash("HotReloadBehavior"));
	ASSERT_NE(behaviorType, nullptr);

	Handle<Scene> scene = SceneSystem::CreateScene("ReloadIdentical");
	Handle<SceneObject> object = SceneSystem::CreateObject("Subject", scene);
	ASSERT_TRUE(object->AddComponent(behaviorType).IsValid());
	EditorSelection::Select(Handle<Object>(object.Get()));

	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")), _window);
	ASSERT_TRUE(panel.IsValid());

	UI::Context* context = UISystem::GetContext(_window);
	ASSERT_NE(context, nullptr);
	UISystem::Update();

	UI::ElementHandle row = FindByText(*context, context->GetRoot(), "speed");
	ASSERT_TRUE(context->IsAlive(row));

	PanelManager::TickAll(0.016f);
	UISystem::Update();
	ASSERT_TRUE(context->IsAlive(row));

	ASSERT_TRUE(ReloadThrough(V1_DLL));
	PanelManager::TickAll(0.016f);
	UISystem::Update();

	EXPECT_FALSE(context->IsAlive(row));
	EXPECT_TRUE(AnyTextEquals(*context, context->GetRoot(), "speed"));
}

TEST_F(EditorExtensionDockFixture, DrawerFieldRefEditsAfterReload) {
	const TypeInfo* behaviorType = TypeRegistry::FindType(StringHash("HotReloadBehavior"));
	ASSERT_NE(behaviorType, nullptr);

	Handle<Scene> scene = SceneSystem::CreateScene("DrawerCapture");
	Handle<SceneObject> object = SceneSystem::CreateObject("Subject", scene);
	Handle<Component> mover = object->AddComponent(behaviorType);
	ASSERT_TRUE(mover.IsValid());
	EditorSelection::Select(Handle<Object>(object.Get()));

	Handle<FontAsset> font = UISystem::GetDefaultFont();

	auto vanished = std::make_shared<u32>(0);

	PropertyDrawers::RegisterFieldDrawer(
		StringHash("i32"),
		[font, vanished](UI::Context& ctx, UI::ElementHandle row, FieldRef field, EditTarget target) {
			UI::ElementHandle button = ctx.CreateElement();
			ctx.AppendChild(row, button);
			UI::Style style;
			style.width = UI::Length::Pixel(80.0f);
			style.height = UI::Length::Pixel(20.0f);
			style.flexShrink = 0.0f;
			ctx.SetStyle(button, style);

			const FieldInfo* resolved = field.Resolve();
			ctx.SetText(button, font, 13.0f, Color::White(),
						str("bump ") + (resolved && resolved->displayName ? resolved->displayName : "?"));

			ctx.Events(button).onClick = [field, target, vanished]() mutable {
				Object* live = target.Resolve();
				const FieldInfo* info = field.Resolve();
				if (!info) {
					++*vanished;
				}
				if (!live || !info || !info->getFn || !info->setFn) {
					return;
				}
				Variant current;
				info->getFn(live, &current);
				Variant next{current.As<i32>() + 1};
				info->setFn(live, &next);
			};
		},
		ModuleBase());

	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")), _window);
	ASSERT_TRUE(panel.IsValid());

	UI::Context* context = UISystem::GetContext(_window);
	ASSERT_NE(context, nullptr);
	UISystem::Update();

	UI::ElementHandle button = FindByText(*context, context->GetRoot(), "bump counter");
	ASSERT_TRUE(context->IsAlive(button));

	ClickElement(*context, button);
	Variant probe;
	ASSERT_TRUE(GetField(mover.Get(), mover.Get()->GetType(), StringHash("counter"), &probe));
	ASSERT_EQ(probe.As<i32>(), 1);

	ASSERT_TRUE(ReloadThrough(V2_DLL));

	ASSERT_TRUE(context->IsAlive(button));
	ClickElement(*context, button);

	Object* live = mover.Get();
	ASSERT_NE(live, nullptr);
	Variant after;
	ASSERT_TRUE(GetField(live, live->GetType(), StringHash("counter"), &after));
	EXPECT_EQ(after.As<i32>(), 2);
	EXPECT_EQ(*vanished, 0u);

	ASSERT_TRUE(ReloadThrough(GONE_DLL));
	ASSERT_TRUE(context->IsAlive(button));
	ClickElement(*context, button);
	EXPECT_EQ(*vanished, 1u);
}
