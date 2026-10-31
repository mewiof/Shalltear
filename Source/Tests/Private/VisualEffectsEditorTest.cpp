#include "TestsPCH.h"

#ifdef SHALLTEAR_SHARED

#include "ShadingHarness.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Time.h"

#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorAssetWatch.h"
#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/SceneAssetDrops.h"
#include "Shalltear/Editor/SceneViewEffects.h"
#include "Shalltear/Editor/SceneViewOverlays.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "Shalltear/VisualEffects/VisualEffect.h"
#include "Shalltear/VisualEffects/VisualEffectBinder.h"

#include <chrono>
#include <format>
#include <thread>

using namespace Shalltear;
using namespace ShadingTest;
using namespace UITest;

namespace {

	constexpr std::string_view CATEGORY = "Effect Graphs";

	str Spark(std::string_view tint, std::string_view x) {
		return str(R"({"_type":"EffectGraph","version":2,"nextId":20,"settings":{},
"properties":[{"id":1,"name":"Tint","reference":"tint","type":"Color","default":[)") +
			   str(tint) + R"(]}],
"nodes":[{"id":10,"def":"Input/Property","x":0,"y":0,"settings":{"property":1}}],
"contexts":[
{"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[{"id":3,"def":"Block/Burst","in":{"Count":{"value":[1]}}}]},
{"id":4,"def":"Context/Initialize","x":0,"y":200,"settings":{"Capacity":4},"flow":[[2,"In"]],"blocks":[
{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[)" +
			   str(x) + R"(,0,0.5]}}},
{"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[1]}}}]},
{"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[4,"In"]],"blocks":[
{"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[10,"Out"]}}}]}],
"groups":[],"notes":[]}
)";
	}

	constexpr const char* GRAPH_PATH = "project://Spark.sleffect";

	u32 Tinted(const Vector<u8>& pixels, u32 channel) {
		u32 count = 0;
		for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
			bool lit = pixels[i + channel] > 150;
			for (u32 other = 0; other < 3; ++other) {
				lit = lit && (other == channel || pixels[i + other] < 60);
			}
			count += lit ? 1u : 0u;
		}
		return count;
	}

	class VisualEffectsEditorFixture : public MaterialReadbackFixture {
	protected:
		ModuleHandle _module;
		const void* _moduleBase = nullptr;
		Asset* _imported = nullptr;
		MaterialAsset* _anyMaterial = nullptr;
		ShadingStage _stage;
		bool _staged = false;

		void SetUp() override {
			MaterialReadbackFixture::SetUp();
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			SceneAnnotations::Initialize();
			PanelManager::Initialize();
			PropertyDrawers::Initialize();
			EditorAssetWatch::Initialize();
			EditorGizmos::Initialize();
			ModuleSystem::Initialize();
			_module = ModuleSystem::Load(Path("VisualEffects.dll"));
			if (ModuleSystem::IsLoaded(_module)) {
				_moduleBase = ModuleSystem::GetNativeHandle(_module);
				EditorExtensions::Load(_moduleBase);
			}
		}

		void TearDown() override {
			if (_staged) {
				TearDownStage(_stage);
			}
			if (_imported) {
				AssetDatabase::UnloadAsset(_imported);
			}
			if (_anyMaterial) {
				Delete(_anyMaterial);
			}
			if (_moduleBase) {
				EditorExtensions::Unload(_moduleBase);
			}
			ModuleSystem::Terminate();
			EditorAssetWatch::Terminate();
			PropertyDrawers::Terminate();
			SceneViewEffects::Terminate();
			SceneViewOverlays::Terminate();
			SceneAssetDrops::Terminate();
			EditorGizmos::Terminate();
			PanelManager::Terminate();
			SceneAnnotations::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			MaterialReadbackFixture::TearDown();
		}

		bool Import(const str& graph) {
			WriteProjectFile("Spark.sleffect", graph);
			_imported = AssetDatabase::LoadAsset(Path(GRAPH_PATH));
			return _imported != nullptr;
		}

		bool Stage(const str& graph) {
			if (!RenderSystem::IsRenderingAvailable() || !Import(graph)) {
				return false;
			}
			_anyMaterial = New<MaterialAsset>(MemTag::Asset);
			_anyMaterial->shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
			_staged = BuildStage(_anyMaterial, _stage);
			if (_staged) {
				_stage.quad->SetActive(false);
			}
			return _staged;
		}

		VisualEffect* AddEffect(Handle<Scene> scene, Handle<SceneObject>* object = nullptr) {
			Handle<SceneObject> made = SceneSystem::CreateObject("Effect", scene);
			Handle<Component> component = made->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
			if (!component.IsValid()) {
				return nullptr;
			}
			VisualEffect* effect = static_cast<VisualEffect*>(component.Get());
			effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(_imported));
			SceneSystem::EndFrame();
			if (object) {
				*object = made;
			}
			return effect;
		}

		bool Render(Vector<u8>& pixels, Vec2I& size) { return RenderStage(_stage, pixels, size); }
	};

} // anonymous namespace

TEST_F(VisualEffectsEditorFixture, EditorHalfRegistersEachLoad) {
	ASSERT_NE(_moduleBase, nullptr);
	const auto listed = []() {
		Vector<str> labels{GetMemoryResource(MemTag::Editor)};
		SceneViewEffects::GetLabels(labels);
		return std::find(labels.begin(), labels.end(), CATEGORY) != labels.end();
	};
	const auto drawn = []() { return PropertyDrawers::FindFieldDrawerFor(StringHash("VisualEffect"), StringHash("properties")) != nullptr; };
	const TypeInfo* binder = TypeRegistry::FindType(StringHash("VisualEffectPositionBinder"));
	ASSERT_NE(binder, nullptr);
	const auto filed = [binder]() { return ComponentMenu::CategoryFor(binder) == "Visual Effects"; };
	EXPECT_TRUE(listed());
	EXPECT_TRUE(drawn());
	EXPECT_TRUE(filed());
	Vector<StringHash> hidden{GetMemoryResource(MemTag::Editor)};
	SceneViewEffects::GetHiddenRenderers([](std::string_view label) { return label != CATEGORY; }, hidden);
	EXPECT_TRUE(std::find(hidden.begin(), hidden.end(), StringHash("VisualEffect")) != hidden.end());

	EditorExtensions::Unload(_moduleBase);
	EXPECT_FALSE(listed());
	EXPECT_FALSE(drawn());
	EXPECT_TRUE(ComponentMenu::CategoryFor(binder).empty());

	ASSERT_TRUE(EditorExtensions::Load(_moduleBase));
	EXPECT_TRUE(listed());
	EXPECT_TRUE(drawn());
	EXPECT_TRUE(filed());
}

TEST_F(VisualEffectsEditorFixture, EditModePlaybackFollowsView) {
	ASSERT_NE(_moduleBase, nullptr);
	if (!Stage(Spark("0,1,0,1", "0"))) {
		GTEST_SKIP();
	}
	const auto drawsAfterAFrame = [&]() {
		Handle<SceneObject> object;
		if (!AddEffect(_stage.scene, &object)) {
			return false;
		}
		Time::BeginFrame(1.0 / 60.0);
		FrameStages::Run(FrameStage::EndFrame, {});
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		const bool rendered = Render(pixels, size);
		SceneSystem::Destroy(object);
		SceneSystem::EndFrame();
		return rendered && Tinted(pixels, 1) > 0;
	};
	EXPECT_FALSE(drawsAfterAFrame());

	const TypeInfo* sceneView = TypeRegistry::FindType(StringHash("SceneViewPanel"));
	Handle<EditorPanel> first = PanelManager::OpenPanel(sceneView);
	ASSERT_TRUE(first.IsValid());
	EXPECT_TRUE(drawsAfterAFrame());

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	first.Get()->BuildContent(context, root);
	context.Update();
	const auto pick = [&]() {
		ClickElement(context, FindByClass(context, root, EditorTheme::Class::ToolButton, 9));
		context.Update();
		UI::ElementHandle row = FindByText(context, context.GetRoot(), CATEGORY);
		EXPECT_TRUE(row.IsValid());
		if (row.IsValid()) {
			ClickElement(context, row);
		}
		first.Get()->OnTick(0.0f);
		context.Update();
	};
	pick();
	EXPECT_FALSE(drawsAfterAFrame());

	Handle<EditorPanel> second = PanelManager::OpenPanel(sceneView);
	ASSERT_TRUE(second.IsValid());
	EXPECT_TRUE(drawsAfterAFrame());
	Variant off(false);
	ASSERT_TRUE(SetField(second.Get(), second.Get()->GetType(), "showEffects"_hash, &off));
	EXPECT_FALSE(drawsAfterAFrame());

	pick();
	EXPECT_TRUE(drawsAfterAFrame());

	EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(), EditorPlayModeChangedEvent{PlayModeState::Playing});
	ASSERT_EQ(PanelManager::GetPlayModeState(), PlayModeState::Playing);
	EXPECT_FALSE(drawsAfterAFrame());
	EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(), EditorPlayModeChangedEvent{PlayModeState::Paused});
	EXPECT_FALSE(drawsAfterAFrame());
	EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(), EditorPlayModeChangedEvent{PlayModeState::Editing});
	EXPECT_TRUE(drawsAfterAFrame());
}

TEST_F(VisualEffectsEditorFixture, BinderStageFollowsMode) {
	ASSERT_NE(_moduleBase, nullptr);
	str graph = Spark("0,1,0,1", "0");
	constexpr std::string_view Tint = R"("properties":[{"id":1,"name":"Tint")";
	ASSERT_NE(graph.find(Tint), str::npos);
	graph.replace(graph.find(Tint), Tint.size(),
				  R"("properties":[{"id":11,"name":"Position","reference":"position","type":"Vector3","default":[0,0,0]},{"id":1,"name":"Tint")");
	if (!Stage(graph)) {
		GTEST_SKIP();
	}
	Handle<SceneObject> object;
	VisualEffect* effect = AddEffect(_stage.scene, &object);
	ASSERT_NE(effect, nullptr);
	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffectPositionBinder"));
	ASSERT_NE(type, nullptr);
	VisualEffectPositionBinder* binder = static_cast<VisualEffectPositionBinder*>(object->AddComponent(type).Get());
	ASSERT_NE(binder, nullptr);
	Handle<SceneObject> target = SceneSystem::CreateObject("Target", _stage.scene);
	target->SetLocalPosition(Vec3(1.0f, 2.0f, 3.0f));
	binder->target = target;
	SceneSystem::EndFrame();
	const auto bound = [effect]() {
		for (const VisualEffectPropertyOverride& entry : effect->properties) {
			if (entry.name == "Position" && entry.overridden) {
				return Vec3(entry.value.x, entry.value.y, entry.value.z);
			}
		}
		return Vec3(-1.0f, -1.0f, -1.0f);
	};

	ASSERT_EQ(PanelManager::GetPlayModeState(), PlayModeState::Editing);
	Time::BeginFrame(1.0 / 60.0);
	FrameStages::Run(FrameStage::EndFrame, {});
	EXPECT_EQ(bound(), Vec3(1.0f, 2.0f, 3.0f));

	EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(), EditorPlayModeChangedEvent{PlayModeState::Playing});
	target->SetLocalPosition(Vec3(4.0f, 5.0f, 6.0f));
	FrameStages::Run(FrameStage::EndFrame, {});
	EXPECT_EQ(bound(), Vec3(1.0f, 2.0f, 3.0f));
	FrameStages::Run(FrameStage::LateUpdate, {});
	EXPECT_EQ(bound(), Vec3(4.0f, 5.0f, 6.0f));
	EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(), EditorPlayModeChangedEvent{PlayModeState::Editing});
}

TEST_F(VisualEffectsEditorFixture, SavedGraphReachesEffect) {
	ASSERT_NE(_moduleBase, nullptr);
	if (!Stage(Spark("0,1,0,1", "0"))) {
		GTEST_SKIP();
	}
	VisualEffect* effect = AddEffect(_stage.scene);
	ASSERT_NE(effect, nullptr);
	const VisualEffectAsset* held = effect->asset.Get();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	const auto frame = [&]() {
		EditorAssetWatch::PollChanges();
		FrameStages::Run(FrameStage::LateUpdate, {});
		FrameStages::Run(FrameStage::EndFrame, {});
		return Render(pixels, size);
	};
	const auto save = [&](const str& graph) {
		WriteProjectFile("Spark.sleffect", graph);
		EditorAssetWatch::NotifyChanged(Path(GRAPH_PATH));
	};
	const auto until = [&](const std::function<bool()>& drawn) {
		for (u32 attempt = 0; attempt < 100; ++attempt) {
			if (frame() && drawn()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
		}
		return false;
	};

	ASSERT_TRUE(frame());
	ASSERT_GT(CenterTexel(pixels, size).y, 150.0f);

	save(Spark("1,0,0,1", "0"));
	ASSERT_TRUE(frame());
	EXPECT_GT(CenterTexel(pixels, size).y, 150.0f);
	EXPECT_TRUE(until([&]() { return CenterTexel(pixels, size).x > 150.0f && CenterTexel(pixels, size).y < 40.0f; }));
	EXPECT_EQ(effect->asset.Get(), held);

	effect->properties.push_back({str("Tint"), Vec4(0.0f, 0.0f, 1.0f, 1.0f), true});
	save(Spark("1,1,0,1", "0.9"));
	EXPECT_TRUE(until([&]() { return Tinted(pixels, 2) > 0 && CenterTexel(pixels, size).z < 40.0f; }));

	auto sink = std::make_shared<ShaderDiagnosticSink>();
	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));
	save("{ this is not a graph");
	bool attempted = false;
	for (u32 attempt = 0; attempt < 100 && !attempted; ++attempt) {
		ASSERT_TRUE(frame());
		attempted = sink->Joined().find("didn't compile") != str::npos;
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}
	LogManager::Terminate();
	sink.reset();
	JobSystem::Terminate();
	ASSERT_TRUE(attempted);
	ASSERT_TRUE(frame());
	EXPECT_GT(Tinted(pixels, 2), 0u);
}

TEST_F(VisualEffectsEditorFixture, SavedNodeShaderReachesEffect) {
	ASSERT_NE(_moduleBase, nullptr);
	const auto shading = [](std::string_view color) {
		return std::format(R"({{"_type":"NodeShader","version":1,"nextId":10,"settings":{{"target":"unlit","blend":"opaque","cull":"off","supportEffects":true}},
"properties":[],"nodes":[],"contexts":[{{"id":5,"def":"Context/Fragment","blocks":[{{"id":6,"def":"Block/BaseColor","in":{{"Value":{{"value":[{}]}}}}}},{{"id":7,"def":"Block/Alpha"}}]}}],
"groups":[],"notes":[]}})",
						   color);
	};
	constexpr const char* SHADING_PATH = "project://Shade.slshader";
	WriteProjectFile("Shade.slshader", shading("0,1,0"));
	const Guid graph = AssetDatabase::EnsureAssetIdentity(Path(SHADING_PATH));
	ASSERT_TRUE(graph.IsValid());
	str shaded = Spark("1,1,1,1", "0");
	const std::string_view output = R"("def":"Context/OutputParticlePrimitive")";
	shaded.replace(shaded.find(output), output.size(),
				   std::format(R"("def":"Context/OutputParticleNodeShaderPrimitive","settings":{{"Node Shader":"{}"}})", graph.ToString()));
	if (!Stage(shaded)) {
		GTEST_SKIP();
	}
	VisualEffect* effect = AddEffect(_stage.scene);
	ASSERT_NE(effect, nullptr);
	const VisualEffectAsset* held = effect->asset.Get();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	const auto frame = [&]() {
		EditorAssetWatch::PollChanges();
		FrameStages::Run(FrameStage::LateUpdate, {});
		FrameStages::Run(FrameStage::EndFrame, {});
		return Render(pixels, size);
	};
	const auto until = [&](const std::function<bool()>& drawn) {
		for (u32 attempt = 0; attempt < 100; ++attempt) {
			if (frame() && drawn()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
		}
		return false;
	};
	ASSERT_TRUE(frame());
	ASSERT_GT(CenterTexel(pixels, size).y, 150.0f);

	WriteProjectFile("Shade.slshader", shading("1,0,0"));
	EditorAssetWatch::NotifyChanged(Path(SHADING_PATH));
	EXPECT_TRUE(until([&]() { return CenterTexel(pixels, size).x > 150.0f && CenterTexel(pixels, size).y < 40.0f; }));
	EXPECT_EQ(effect->asset.Get(), held);
}

TEST_F(VisualEffectsEditorFixture, SavedSubgraphReachesUsers) {
	ASSERT_NE(_moduleBase, nullptr);
	const auto tint = [](std::string_view color) {
		return std::format(R"({{"_type":"VisualEffectSubgraphOperator","version":1,"nextId":5,
"properties":[{{"id":1,"name":"Color","reference":"Color","type":"Color","default":[{}],"settings":{{"output":true}}}}],
"nodes":[],"contexts":[],"groups":[],"notes":[]}})",
						   color);
	};
	constexpr const char* SUBGRAPH_PATH = "project://Tint.sleffectop";
	WriteProjectFile("Tint.sleffectop", tint("0,1,0,1"));
	const Guid subgraph = AssetDatabase::EnsureAssetIdentity(Path(SUBGRAPH_PATH));
	ASSERT_TRUE(subgraph.IsValid());
	str used = Spark("1,1,1,1", "0");
	const std::string_view property = R"({"id":10,"def":"Input/Property","x":0,"y":0,"settings":{"property":1}})";
	used.replace(used.find(property), property.size(), std::format(R"({{"id":10,"def":"Utility/Subgraph","x":0,"y":0,"settings":{{"asset":"{}"}}}})", subgraph.ToString()));
	const std::string_view wire = R"("from":[10,"Out"])";
	used.replace(used.find(wire), wire.size(), R"("from":[10,"1"])");
	if (!Stage(used)) {
		GTEST_SKIP();
	}
	VisualEffect* effect = AddEffect(_stage.scene);
	ASSERT_NE(effect, nullptr);
	WriteProjectFile("Other.sleffect", Spark("0,0,1,1", "0.9"));
	Asset* other = AssetDatabase::LoadAsset(Path("project://Other.sleffect"));
	ASSERT_NE(other, nullptr);
	Handle<SceneObject> besideObject = SceneSystem::CreateObject("Other", _stage.scene);
	Handle<Component> besideComponent = besideObject->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
	ASSERT_TRUE(besideComponent.IsValid());
	static_cast<VisualEffect*>(besideComponent.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(other));
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	const auto frame = [&]() {
		EditorAssetWatch::PollChanges();
		FrameStages::Run(FrameStage::LateUpdate, {});
		FrameStages::Run(FrameStage::EndFrame, {});
		return Render(pixels, size);
	};
	ASSERT_TRUE(frame());
	ASSERT_GT(CenterTexel(pixels, size).y, 150.0f);

	auto sink = std::make_shared<ShaderDiagnosticSink>();
	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));
	WriteProjectFile("Tint.sleffectop", tint("1,0,0,1"));
	EditorAssetWatch::NotifyChanged(Path(SUBGRAPH_PATH));
	bool reached = false;
	for (u32 attempt = 0; attempt < 100 && !reached; ++attempt) {
		reached = frame() && CenterTexel(pixels, size).x > 150.0f && CenterTexel(pixels, size).y < 40.0f;
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}
	ASSERT_TRUE(frame());
	LogManager::Terminate();
	const str said = sink->Joined();
	sink.reset();
	JobSystem::Terminate();
	EXPECT_TRUE(reached);
	EXPECT_NE(said.find("Spark.sleffect' recompiled"), str::npos) << said;
	EXPECT_EQ(said.find("Other.sleffect' recompiled"), str::npos);
	SceneSystem::Destroy(besideObject);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(other);
}

TEST_F(VisualEffectsEditorFixture, EditedShaderReachesEffect) {
	ASSERT_NE(_moduleBase, nullptr);
	const auto tint = [](std::string_view color) {
		return std::format("void Tint(inout EffectAttributes attributes)\n{{\n  attributes.color = float3({});\n}}\n", color);
	};
	constexpr const char* FILE_PATH = "project://Tint.hlsl";
	WriteProjectFile("Tint.hlsl", tint("0, 1, 0"));
	const Guid file = AssetDatabase::EnsureAssetIdentity(Path(FILE_PATH));
	ASSERT_TRUE(file.IsValid());
	str used = Spark("1,1,1,1", "0");
	const std::string_view color = R"({"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[10,"Out"]}}})";
	used.replace(used.find(color), color.size(), std::format(R"({{"id":7,"def":"Block/CustomHLSL","settings":{{"Shader File":"{}"}}}})", file.ToString()));
	if (!Stage(used)) {
		GTEST_SKIP();
	}
	VisualEffect* effect = AddEffect(_stage.scene);
	ASSERT_NE(effect, nullptr);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	const auto frame = [&]() {
		EditorAssetWatch::PollChanges();
		FrameStages::Run(FrameStage::LateUpdate, {});
		FrameStages::Run(FrameStage::EndFrame, {});
		return Render(pixels, size);
	};
	ASSERT_TRUE(frame());
	ASSERT_GT(CenterTexel(pixels, size).y, 150.0f);
	ASSERT_LT(CenterTexel(pixels, size).x, 40.0f);

	WriteProjectFile("Tint.hlsl", tint("1, 0, 0"));
	EditorAssetWatch::NotifyChanged(Path(FILE_PATH));
	bool reached = false;
	for (u32 attempt = 0; attempt < 100 && !reached; ++attempt) {
		reached = frame() && CenterTexel(pixels, size).x > 150.0f && CenterTexel(pixels, size).y < 40.0f;
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}
	EXPECT_TRUE(reached);
}

TEST_F(VisualEffectsEditorFixture, SheetTickFollowsEdits) {
	ASSERT_NE(_moduleBase, nullptr);
	if (!Import(Spark("0,1,0,1", "0"))) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Handle<Scene> scene = SceneSystem::CreateScene("Sheet");
	VisualEffect* effect = AddEffect(scene);
	ASSERT_NE(effect, nullptr);

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {600, 400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::ElementHandle row = context.CreateElement();
	UI::Style rowStyle;
	rowStyle.width = UI::Length::Pixel(420.0f);
	context.SetStyle(row, rowStyle);
	context.AppendChild(root, row);
	const PropertyDrawerFn* drawer = PropertyDrawers::FindFieldDrawerFor(StringHash("VisualEffect"), StringHash("properties"));
	ASSERT_NE(drawer, nullptr);
	(*drawer)(context, row, FieldRef{StringHash("VisualEffect"), StringHash("properties")}, EditTarget(Handle<Object>(effect)));
	context.Update();

	ASSERT_TRUE(FindByText(context, row, "Tint").IsValid());
	const UI::ElementHandle tick = FindByClass(context, row, StringHash("slt-checkbox"), 0);
	ASSERT_TRUE(context.IsAlive(tick));
	const auto ticked = [&]() {
		const UI::Element* mark = context.GetElement(FindByClass(context, tick, StringHash("slt-check-mark"), 0));
		return mark && HasFlag(mark->flags, UI::ElementFlags::Visible);
	};
	const auto settle = [&]() {
		PropertyDrawers::RefreshRows(context);
		context.Update();
	};
	EXPECT_FALSE(ticked());

	ClickElement(context, tick);
	ASSERT_EQ(effect->properties.size(), 1u);
	EXPECT_EQ(effect->properties[0].name, "Tint");
	EXPECT_TRUE(effect->properties[0].overridden);
	EXPECT_EQ(effect->properties[0].value, Vec4(0.0f, 1.0f, 0.0f, 1.0f));
	ASSERT_TRUE(UndoSystem::Undo());
	settle();
	EXPECT_TRUE(effect->properties.empty());
	EXPECT_FALSE(ticked());

	const UI::ElementHandle red = FindByClass(context, row, StringHash("slt-number-input"), 0);
	ASSERT_TRUE(context.IsAlive(red));
	ClickElement(context, red);
	ASSERT_EQ(context.GetFocused(), red);
	InputEvent selectAll{};
	selectAll.type = InputEventType::KeyDown;
	selectAll.key = Key::A;
	selectAll.mods = KeyMod::Ctrl;
	context.InjectInput(selectAll);
	for (char c : std::string_view("255")) {
		InputEvent typed{};
		typed.type = InputEventType::Text;
		typed.codepoint = static_cast<char32_t>(c);
		context.InjectInput(typed);
	}
	InputEvent commit{};
	commit.type = InputEventType::KeyDown;
	commit.key = Key::Enter;
	context.InjectInput(commit);
	context.Update();
	settle();
	ASSERT_EQ(effect->properties.size(), 1u);
	EXPECT_TRUE(effect->properties[0].overridden);
	EXPECT_EQ(effect->properties[0].value, Vec4(1.0f, 1.0f, 0.0f, 1.0f));
	EXPECT_TRUE(ticked());

	ClickElement(context, tick);
	settle();
	ASSERT_EQ(effect->properties.size(), 1u);
	EXPECT_FALSE(effect->properties[0].overridden);
	EXPECT_EQ(effect->properties[0].value, Vec4(1.0f, 1.0f, 0.0f, 1.0f));
	ASSERT_TRUE(UndoSystem::Undo());
	settle();
	EXPECT_TRUE(effect->properties[0].overridden);
	EXPECT_TRUE(ticked());

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

namespace {

	bool Shows(UI::Context& context, UI::ElementHandle root, std::string_view word) {
		const UI::ElementHandle label = FindByText(context, root, word);
		const UI::Element* element = label.IsValid() ? context.GetElement(label) : nullptr;
		if (!element || element->parent == UI::INVALID_UI_INDEX) {
			return false;
		}
		return context.GetStyle(context.MakeHandle(element->parent)).display != UI::Display::None;
	}

	void SelectOnly(std::initializer_list<Handle<SceneObject>> objects) {
		EditorSelection::Clear();
		for (const Handle<SceneObject>& object : objects) {
			EditorSelection::AddToSelection(Handle<Object>(static_cast<Object*>(object.Get())));
		}
	}

	struct ViewRig {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> view;

		ViewRig() : context(Desc(), &fonts) {
			EditorTheme::ApplyToContext(context);
			view = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("SceneViewPanel")));
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			if (view.IsValid()) {
				view.Get()->BuildContent(context, root);
			}
			context.Update();
		}

		static UI::ContextDesc Desc() {
			UI::ContextDesc desc;
			desc.sizePx = {1000, 700};
			return desc;
		}

		void Tick() {
			view.Get()->OnTick(0.0f);
			context.Update();
		}

		UI::ElementHandle Card() { return FindByClass(context, root, EditorTheme::Class::SceneOverlayCard, 0); }
	};

} // anonymous namespace

TEST_F(VisualEffectsEditorFixture, PlayControlsReachSelection) {
	ASSERT_NE(_moduleBase, nullptr);
	if (!Stage(Spark("0,1,0,1", "0"))) {
		GTEST_SKIP();
	}
	Handle<SceneObject> one;
	Handle<SceneObject> two;
	ASSERT_NE(AddEffect(_stage.scene, &one), nullptr);
	ASSERT_NE(AddEffect(_stage.scene, &two), nullptr);
	ViewRig rig;
	ASSERT_TRUE(rig.view.IsValid());

	rig.Tick();
	EXPECT_FALSE(rig.context.IsAlive(rig.Card()));
	SelectOnly({one, two});
	rig.Tick();
	ASSERT_TRUE(rig.context.IsAlive(rig.Card()));
	EXPECT_TRUE(rig.context.IsAlive(FindByText(rig.context, rig.Card(), "Play Controls")));
	EXPECT_TRUE(Shows(rig.context, rig.Card(), "Pause"));
	EXPECT_FALSE(Shows(rig.context, rig.Card(), "Play"));

	const auto pressAndAsk = [&](std::string_view press) {
		SelectOnly({one, two});
		rig.Tick();
		ClickElement(rig.context, FindByText(rig.context, rig.Card(), press));
		rig.Tick();
	};
	const auto eachShows = [&](std::string_view word) {
		bool all = true;
		for (const Handle<SceneObject>& alone : {one, two}) {
			SelectOnly({alone});
			rig.Tick();
			all = all && Shows(rig.context, rig.Card(), word);
		}
		return all;
	};
	pressAndAsk("Pause");
	EXPECT_TRUE(eachShows("Play"));
	pressAndAsk("Play");
	EXPECT_TRUE(eachShows("Pause"));
	pressAndAsk("Stop");
	EXPECT_TRUE(eachShows("Play"));
	pressAndAsk("Restart");
	EXPECT_TRUE(eachShows("Pause"));

	SelectOnly({one, two});
	rig.Tick();
	ClickElement(rig.context, FindByText(rig.context, rig.Card(), "Set"));
	const UI::ElementHandle half = FindByText(rig.context, rig.context.GetRoot(), "50%");
	ASSERT_TRUE(half.IsValid());
	ClickElement(rig.context, half);
	rig.Tick();
	for (const Handle<SceneObject>& alone : {one, two}) {
		SelectOnly({alone});
		rig.Tick();
		EXPECT_TRUE(rig.context.IsAlive(FindByText(rig.context, rig.Card(), "50")));
	}
	EditorSelection::Clear();
}

TEST_F(VisualEffectsEditorFixture, ShowBoundsDrawsSelected) {
	ASSERT_NE(_moduleBase, nullptr);
	if (!Stage(Spark("0,1,0,1", "0"))) {
		GTEST_SKIP();
	}
	Handle<SceneObject> object;
	VisualEffect* effect = AddEffect(_stage.scene, &object);
	ASSERT_NE(effect, nullptr);
	ViewRig rig;
	Time::BeginFrame(1.0 / 60.0);
	FrameStages::Run(FrameStage::EndFrame, {});

	const ComponentGizmoFn* draw = EditorGizmos::FindComponentGizmo(StringHash("VisualEffect"));
	ASSERT_NE(draw, nullptr);
	const auto drawn = [&](bool selected) {
		Vector<DebugLine> lines{GetMemoryResource(MemTag::Editor)};
		Vector<DebugLine> overlay{GetMemoryResource(MemTag::Editor)};
		GizmoDrawContext gizmo;
		gizmo.lines = &lines;
		gizmo.overlayLines = &overlay;
		gizmo.selected = selected;
		(*EditorGizmos::FindComponentGizmo(StringHash("VisualEffect")))(gizmo, Handle<Component>(effect));
		u32 bounds = 0;
		for (const DebugLine& line : lines) {
			bounds += line.color == EditorTheme::GetColor32(EditorTheme::ColorToken::GizmoEffectBounds) ? 1u : 0u;
		}
		return bounds;
	};
	EXPECT_EQ(drawn(false), 0u);
	EXPECT_EQ(drawn(true), 12u);

	SelectOnly({object});
	rig.Tick();
	const UI::ElementHandle toggle = FindByClass(rig.context, rig.Card(), StringHash("slt-checkbox"), 0);
	ASSERT_TRUE(toggle.IsValid());
	ClickElement(rig.context, toggle);
	EXPECT_EQ(drawn(true), 0u);
	ClickElement(rig.context, toggle);
	EXPECT_EQ(drawn(true), 12u);
	EditorSelection::Clear();
}

namespace {

	str BoxedSpark(bool worldCenter) {
		str graph = Spark("0,1,0,1", "0");
		const std::string_view settings = R"("settings":{"Capacity":4},)";
		const str boxed = str(R"("settings":{"Capacity":4,"Space":"World"},"in":{"Bounds Center":{"value":[0,1,0])") +
						  (worldCenter ? R"(,"space":"World")" : "") + R"(},"Bounds Size":{"value":[2,2,2]}},)";
		graph.replace(graph.find(settings), settings.size(), boxed);
		return graph;
	}

	AABB DrawnBox(VisualEffect* effect) {
		Vector<DebugLine> lines{GetMemoryResource(MemTag::Editor)};
		Vector<DebugLine> overlay{GetMemoryResource(MemTag::Editor)};
		GizmoDrawContext gizmo;
		gizmo.lines = &lines;
		gizmo.overlayLines = &overlay;
		gizmo.selected = true;
		(*EditorGizmos::FindComponentGizmo(StringHash("VisualEffect")))(gizmo, Handle<Component>(effect));
		Vec3 low(1e30f, 1e30f, 1e30f);
		Vec3 high(-1e30f, -1e30f, -1e30f);
		u32 edges = 0;
		for (const DebugLine& line : lines) {
			if (line.color != EditorTheme::GetColor32(EditorTheme::ColorToken::GizmoEffectBounds)) {
				continue;
			}
			++edges;
			for (const Vec3& p : {line.a, line.b}) {
				low = Vec3(std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z));
				high = Vec3(std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z));
			}
		}
		EXPECT_EQ(edges, 12u);
		return AABB{low, high};
	}

	void ExpectBox(const AABB& box, Vec3 center, const char* what) {
		const Vec3 mid = (box.min + box.max) * 0.5f;
		const Vec3 extent = (box.max - box.min) * 0.5f;
		EXPECT_NEAR(mid.x, center.x, 1.0e-4f) << what;
		EXPECT_NEAR(mid.y, center.y, 1.0e-4f) << what;
		EXPECT_NEAR(mid.z, center.z, 1.0e-4f) << what;
		EXPECT_NEAR(extent.x, 1.0f, 1.0e-4f) << what;
		EXPECT_NEAR(extent.y, 1.0f, 1.0e-4f) << what;
		EXPECT_NEAR(extent.z, 1.0f, 1.0e-4f) << what;
	}

} // anonymous namespace

TEST_F(VisualEffectsEditorFixture, WorldSystemsLocalBoundsFollowEffect) {
	ASSERT_NE(_moduleBase, nullptr);
	if (!Stage(BoxedSpark(false))) {
		GTEST_SKIP();
	}
	Handle<SceneObject> object;
	VisualEffect* effect = AddEffect(_stage.scene, &object);
	ASSERT_NE(effect, nullptr);
	ViewRig rig;
	object->SetLocalPosition(Vec3(5.0f, 0.0f, 0.0f));
	object->SetLocalRotation(Quat::AngleAxis(90.0f, Vec3(0.0f, 1.0f, 0.0f)));
	object->SetLocalScale(Vec3(2.0f, 2.0f, 2.0f));
	Time::BeginFrame(1.0 / 60.0);
	FrameStages::Run(FrameStage::EndFrame, {});
	ExpectBox(DrawnBox(effect), Vec3(5.0f, 2.0f, 0.0f), "a World system's Local box did not go through its effect's transform");

	object->SetLocalPosition(Vec3(-3.0f, 0.0f, 0.0f));
	object->SetLocalRotation(Quat::Identity());
	object->SetLocalScale(Vec3(1.0f, 1.0f, 1.0f));
	Time::BeginFrame(1.0 / 60.0);
	FrameStages::Run(FrameStage::EndFrame, {});
	ExpectBox(DrawnBox(effect), Vec3(-3.0f, 1.0f, 0.0f), "a World system's Local box did not follow its moved effect");
}

TEST_F(VisualEffectsEditorFixture, WorldBoundsStayPut) {
	ASSERT_NE(_moduleBase, nullptr);
	if (!Stage(BoxedSpark(true))) {
		GTEST_SKIP();
	}
	Handle<SceneObject> object;
	VisualEffect* effect = AddEffect(_stage.scene, &object);
	ASSERT_NE(effect, nullptr);
	ViewRig rig;
	object->SetLocalPosition(Vec3(5.0f, 0.0f, 0.0f));
	object->SetLocalRotation(Quat::AngleAxis(90.0f, Vec3(0.0f, 1.0f, 0.0f)));
	object->SetLocalScale(Vec3(2.0f, 2.0f, 2.0f));
	Time::BeginFrame(1.0 / 60.0);
	FrameStages::Run(FrameStage::EndFrame, {});
	ExpectBox(DrawnBox(effect), Vec3(0.0f, 1.0f, 0.0f), "a World system's World box moved with its effect");
}

TEST_F(VisualEffectsEditorFixture, DroppedGraphPlaysWhileLoaded) {
	ASSERT_NE(_moduleBase, nullptr);
	if (!Import(Spark("0,1,0,1", "0"))) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	AssetDragData data;
	data.guid = AssetDatabase::LookupGuid(Path(GRAPH_PATH));
	data.assetTypeHash = StringHash("VisualEffectAsset").GetValue();
	UI::DragPayload payload;
	payload.type = "Asset"_hash;
	payload.data = Variant(data);
	Handle<Scene> scene = SceneSystem::CreateScene("Drop");
	ASSERT_TRUE(SceneAssetDrops::Accepts(payload, scene));

	Handle<SceneObject> made = SceneAssetDrops::Drop(payload, scene, {});
	ASSERT_TRUE(made.IsValid());
	EXPECT_EQ(made->name, "Spark");
	const VisualEffect* effect = nullptr;
	for (u32 index = 0; index < made->GetComponentCount() && !effect; ++index) {
		Component* component = made->GetComponentAt(index).Get();
		if (component && TypeRegistry::IsDerivedFrom(component->GetType(), TypeRegistry::FindType(StringHash("VisualEffect")))) {
			effect = static_cast<const VisualEffect*>(component);
		}
	}
	ASSERT_NE(effect, nullptr);
	EXPECT_EQ(effect->asset.Get(), _imported);

	EditorExtensions::Unload(_moduleBase);
	EXPECT_FALSE(SceneAssetDrops::Accepts(payload, scene));
	ASSERT_TRUE(EditorExtensions::Load(_moduleBase));
	EXPECT_TRUE(SceneAssetDrops::Accepts(payload, scene));
	EditorSelection::Clear();
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

#endif
