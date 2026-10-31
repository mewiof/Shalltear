#include "TestsPCH.h"

#ifdef SHALLTEAR_SHARED

#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/DragDrop.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/SettingsPresentation.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "ScopedImportCostThreshold.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <thread>

using namespace Shalltear;
using namespace UITest;

namespace {

	std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	class AudioEditorFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		ModuleHandle _module;
		const void* _moduleBase = nullptr;
		Handle<EditorPanel> _inspector;
		ScopedImportCostThreshold _cacheAll{ScopedImportCostThreshold::CacheEverything};
		bool _ownsJobSystem = false;

		void SetUp() override {
			_ownsJobSystem = JobSystem::GetWorkerCount() == 0;
			if (_ownsJobSystem) {
				JobSystem::Initialize();
			}
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_audio_editor_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			FileSystem::WriteTextFile(_paths.project, str("[Audio]\ndevice = None\n"));

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			EditorSelection::Initialize();
			PropertyDrawers::Initialize();
			EditorGizmos::Initialize();
			EditorSceneOps::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();
			EditorPreviews::Initialize();
			EditorPreviewStore::Initialize();

			_module = ModuleSystem::Load(Path("Audio.dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			_moduleBase = ModuleSystem::GetNativeHandle(_module);
			AssetDatabase::Initialize();
			ASSERT_TRUE(EditorExtensions::Load(_moduleBase));
		}

		void TearDown() override {
			PanelManager::Terminate();
			if (_moduleBase) {
				EditorExtensions::Unload(_moduleBase);
			}
			EditorPreviewStore::Terminate();
			UndoSystem::Terminate();
			EditorSceneOps::Terminate();
			EditorGizmos::Terminate();
			PropertyDrawers::Terminate();
			EditorSelection::Terminate();
			AssetDatabase::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
			if (_ownsJobSystem) {
				JobSystem::Terminate();
			}
		}

		Guid Stage(const char* name) {
			const std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Audio");
			if (fixtures.empty()) {
				return {};
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / name, _root / "Assets" / name, std::filesystem::copy_options::overwrite_existing, ec);
			AssetDatabase::ScanProjectDirectory();
			return AssetDatabase::LookupGuid(Path(str("project://Assets/") + name));
		}

		void BuildInspector(UI::Context& context, UI::ElementHandle& root) {
			_inspector = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")));
			ASSERT_TRUE(_inspector.IsValid());
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			_inspector.Get()->BuildContent(context, root);
			context.Update();
		}

		void Rebuild(UI::Context& context, UI::ElementHandle& root) {
			PanelManager::TickAll(1.0f / 60.0f);
			context.ClearChildren(root);
			_inspector.Get()->BuildContent(context, root);
			context.Update();
		}

		static void EndFrame() {
			FrameStageArgs args;
			FrameStages::Run(FrameStage::EndFrame, args);
		}

		static UI::ElementHandle PaneHeader(UI::Context& context, UI::ElementHandle root) {
			const UI::Element* rootElement = context.GetElement(root);
			if (!rootElement) {
				return {};
			}
			const UI::Element* pane = context.GetElement(context.MakeHandle(rootElement->lastChild));
			return pane ? context.MakeHandle(pane->firstChild) : UI::ElementHandle{};
		}

		static Handle<VectorAsset> IconOf(UI::Context& context, UI::ElementHandle element) {
			if (context.HasIcon(element)) {
				return context.GetIconAsset(element);
			}
			const UI::Element* pod = context.GetElement(element);
			for (u32 child = pod ? pod->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				Handle<VectorAsset> found = IconOf(context, handle);
				if (found.IsValid()) {
					return found;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return {};
		}

		static str Sidecar(const Guid& guid) {
			return FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(AssetDatabase::LookupPath(guid))), MemTag::Editor);
		}
	};

} // namespace

TEST_F(AudioEditorFixture, ClipInspectorApplyReimports) {
	const Guid guid = Stage("stereo_44100_pcm16.wav");
	if (!guid.IsValid()) {
		GTEST_SKIP() << "audio fixtures not found";
	}
	EditorSelection::SelectAsset(guid);
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 800};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	for (const char* row : {"Force To Mono", "Load Type", "Compression Format", "Quality", "Sample Rate Setting"}) {
		EXPECT_TRUE(UITest::FindByText(context, root, row).IsValid()) << "no " << row << " row";
	}
	EXPECT_TRUE(UITest::FindByTextContaining(context, root, "Original Size:").IsValid());

	UI::ElementHandle format = UITest::FindByClass(context, root, StringHash("slt-dropdown"), 1);
	ASSERT_TRUE(format.IsValid());
	UITest::ClickElement(context, format);
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	UITest::ClickElement(context, UITest::NthChildOf(context, context.GetOpenPopup(0), 1));
	Rebuild(context, root);
	EXPECT_FALSE(UITest::FindByText(context, root, "Quality").IsValid());

	UI::ElementHandle apply = UITest::FindByText(context, root, "Apply");
	ASSERT_TRUE(apply.IsValid());
	UITest::ClickElement(context, apply);
	Rebuild(context, root);

	EXPECT_NE(Sidecar(guid).find("\"compressionFormat\": \"QOA\""), str::npos) << Sidecar(guid);
	Asset* clip = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(clip, nullptr);
	const FieldInfo* field = FindField(clip->GetType(), StringHash("_compressionFormat"));
	ASSERT_NE(field, nullptr);
	EXPECT_EQ(*static_cast<const u8*>(field->addrFn(clip)), 1u);
	AssetDatabase::UnloadAsset(clip);
}

TEST_F(AudioEditorFixture, RevertApplyGreyedUntilDraftDiffers) {
	const Guid guid = Stage("stereo_44100_pcm16.wav");
	if (!guid.IsValid()) {
		GTEST_SKIP() << "audio fixtures not found";
	}
	EditorSelection::SelectAsset(guid);
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 800};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	const auto button = [&](const char* label) {
		const UI::Element* text = context.GetElement(UITest::FindByText(context, root, label));
		return text ? context.MakeHandle(text->parent) : UI::ElementHandle{};
	};
	const auto greyed = [&](const char* label) { return context.HasClass(button(label), EditorTheme::Class::Disabled); };
	const auto takesAPress = [&](const char* label) {
		const UI::ElementHandle target = button(label);
		const UI::Rect rect = context.GetLayout(target).borderBox;
		UI::ElementHandle hit = context.HitTest(Vec2(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f));
		while (hit.IsValid() && !(hit == target)) {
			const UI::Element* pod = context.GetElement(hit);
			hit = pod && pod->parent != UI::INVALID_UI_INDEX ? context.MakeHandle(pod->parent) : UI::ElementHandle{};
		}
		return hit.IsValid();
	};
	ASSERT_TRUE(button("Revert").IsValid() && button("Apply").IsValid());
	for (const char* label : {"Revert", "Apply"}) {
		EXPECT_TRUE(greyed(label)) << label;
		EXPECT_FALSE(takesAPress(label)) << label;
	}

	UI::ElementHandle knob = UITest::FindByClass(context, root, StringHash("slt-slider-knob"), 0);
	ASSERT_TRUE(knob.IsValid());
	UITest::DragElementBy(context, knob, -60, 0);
	for (const char* label : {"Revert", "Apply"}) {
		EXPECT_FALSE(greyed(label)) << label;
		EXPECT_TRUE(takesAPress(label)) << label;
	}
	const str untouched = Sidecar(guid);
	UITest::ClickElement(context, button("Revert"));
	Rebuild(context, root);
	EXPECT_TRUE(greyed("Revert"));
	EXPECT_EQ(Sidecar(guid), untouched);

	UI::ElementHandle format = UITest::FindByClass(context, root, StringHash("slt-dropdown"), 1);
	ASSERT_TRUE(format.IsValid());
	UITest::ClickElement(context, format);
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	UITest::ClickElement(context, UITest::NthChildOf(context, context.GetOpenPopup(0), 1));
	Rebuild(context, root);
	for (const char* label : {"Revert", "Apply"}) {
		EXPECT_FALSE(greyed(label)) << label;
		EXPECT_TRUE(takesAPress(label)) << label;
	}
	UITest::ClickElement(context, button("Apply"));
	Rebuild(context, root);
	ASSERT_NE(Sidecar(guid).find("\"compressionFormat\": \"QOA\""), str::npos) << Sidecar(guid);
	EXPECT_TRUE(greyed("Apply"));
}

TEST_F(AudioEditorFixture, PanePlaysClipInEditMode) {
	const Guid guid = Stage("stereo_44100_pcm16.wav");
	if (!guid.IsValid()) {
		GTEST_SKIP() << "audio fixtures not found";
	}
	EditorSelection::SelectAsset(guid);
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 800};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	const UI::ElementHandle header = PaneHeader(context, root);
	const UI::ElementHandle play = UITest::FindByClass(context, header, EditorTheme::Class::IconButton, 0);
	ASSERT_TRUE(play.IsValid());
	EXPECT_EQ(IconOf(context, play), EditorIcons::GetGlyph(EditorIcons::Glyph::Play));
	EXPECT_TRUE(UITest::FindByClass(context, header, EditorTheme::Class::IconButton, 2).IsValid());

	UITest::ClickElement(context, play);
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	EXPECT_EQ(IconOf(context, play), EditorIcons::GetGlyph(EditorIcons::Glyph::Stop));

	bool ended = false;
	for (int attempt = 0; attempt < 200 && !ended; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		EndFrame();
		PanelManager::TickAll(1.0f / 60.0f);
		context.Update();
		ended = IconOf(context, play) == EditorIcons::GetGlyph(EditorIcons::Glyph::Play);
	}
	EXPECT_TRUE(ended);
}

TEST_F(AudioEditorFixture, BrowsersThumbnailArrivesOnceEnvelopeIsRead) {
	const Guid guid = Stage("stereo_44100_pcm16.wav");
	if (!guid.IsValid()) {
		GTEST_SKIP() << "audio fixtures not found";
	}
	AssetPreviewEntry entry;
	bool cached = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!cached && std::chrono::steady_clock::now() < deadline) {
		cached = EditorPreviewStore::Request(guid, entry);
		EditorPreviewStore::Drain();
		EndFrame();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	EXPECT_TRUE(cached);
	EXPECT_EQ(EditorPreviews::BadgeForFileName("a.wav").icon.glyph, EditorIcons::Glyph::AssetAudioClip.glyph);
}

TEST_F(AudioEditorFixture, ContainerPaneHasPlayAndSkip) {
	FileSystem::WriteTextFile(Path((_root / "Assets/Steps.randomcontainer").generic_string()), "{}");
	FileSystem::WriteTextFile(Path((_root / "Assets/Stems.layeredcontainer").generic_string()), "{}");
	AssetDatabase::ScanProjectDirectory();
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 800};
	for (const auto& [name, buttons] : {std::pair{"project://Assets/Steps.randomcontainer", 2u}, std::pair{"project://Assets/Stems.layeredcontainer", 1u}}) {
		const Guid guid = AssetDatabase::LookupGuid(Path(name));
		ASSERT_TRUE(guid.IsValid()) << name;
		EditorSelection::SelectAsset(guid);
		UI::Context context(desc, &fonts);
		UI::ElementHandle root;
		BuildInspector(context, root);
		const UI::ElementHandle header = PaneHeader(context, root);
		EXPECT_TRUE(UITest::FindByClass(context, header, EditorTheme::Class::IconButton, buttons - 1).IsValid()) << name;
		EXPECT_FALSE(UITest::FindByClass(context, header, EditorTheme::Class::IconButton, buttons).IsValid()) << name;
		PanelManager::ClosePanel(_inspector);
	}
}

namespace {

	template <typename T>
	T* FieldOf(Object* object, const char* name) {
		const FieldInfo* field = object ? FindField(object->GetType(), StringHash(name)) : nullptr;
		return field && field->addrFn ? static_cast<T*>(field->addrFn(object)) : nullptr;
	}

	void MouseAt(UI::Context& context, InputEventType type, MouseButton button, Vec2 at) {
		InputEvent event{};
		event.type = type;
		event.button = button;
		event.mousePosPx = {static_cast<i32>(at.x), static_cast<i32>(at.y)};
		context.InjectInput(event);
	}

	void ClickAt(UI::Context& context, Vec2 at) {
		MouseAt(context, InputEventType::MouseMove, MouseButton::Left, at);
		MouseAt(context, InputEventType::MouseDown, MouseButton::Left, at);
		MouseAt(context, InputEventType::MouseUp, MouseButton::Left, at);
		context.Update();
	}

	void DragFromTo(UI::Context& context, Vec2 from, Vec2 to) {
		MouseAt(context, InputEventType::MouseMove, MouseButton::Left, from);
		MouseAt(context, InputEventType::MouseDown, MouseButton::Left, from);
		context.Update();
		for (i32 step = 1; step <= 2; ++step) {
			const f32 t = static_cast<f32>(step) / 2.0f;
			MouseAt(context, InputEventType::MouseMove, MouseButton::Left, from + (to - from) * t);
			context.Update();
		}
		MouseAt(context, InputEventType::MouseUp, MouseButton::Left, to);
		context.Update();
	}

	struct GraphPlot {
		UI::Rect rect;
		Vec2 At(f32 share, f32 value) const { return Vec2(rect.x + share * rect.w, rect.y + (1.0f - value / 1.1f) * rect.h); }
	};

	GraphPlot PlotOf(UI::Context& context, UI::ElementHandle root) {
		const UI::ElementHandle graph = UITest::FindByClass(context, root, EditorTheme::Class::Graph, 0);
		return GraphPlot{context.GetLayout(UITest::NthChildOf(context, graph, 0)).borderBox};
	}

	bool Displayed(UI::Context& context, UI::ElementHandle element) {
		return context.IsAlive(element) && context.GetStyle(element).display != UI::Display::None;
	}

	UI::ElementHandle EntryOf(UI::Context& context, UI::ElementHandle root, std::string_view label) {
		const UI::ElementHandle text = UITest::FindByText(context, root, label);
		const UI::Element* pod = context.IsAlive(text) ? context.GetElement(text) : nullptr;
		return pod ? context.MakeHandle(pod->parent) : UI::ElementHandle{};
	}

} // namespace

TEST_F(AudioEditorFixture, SourceGraphOffersLowPass) {
	Handle<Scene> scene = SceneSystem::CreateScene("curves");
	Handle<SceneObject> object = SceneSystem::CreateObject("source", scene);
	ASSERT_TRUE(object->AddComponent(TypeRegistry::FindType(StringHash("AudioSource"))).IsValid());
	EditorSelection::Select(Handle<Object>(object.Get()));
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	ASSERT_TRUE(UITest::FindByClass(context, root, EditorTheme::Class::Graph, 0).IsValid());
	const UI::ElementHandle label = UITest::FindByText(context, root, "customRolloffCurve");
	ASSERT_TRUE(label.IsValid());
	EXPECT_FALSE(Displayed(context, label));
	for (const char* hidden : {"spatialBlendCurve", "spreadCurve", "reverbZoneMixCurve"}) {
		EXPECT_FALSE(UITest::FindByText(context, root, hidden).IsValid()) << hidden;
	}
	EXPECT_TRUE(UITest::FindByText(context, root, "spatialBlend").IsValid());
	for (const char* entry : {"Volume", "Spatial Blend", "Spread", "Reverb Zone Mix"}) {
		EXPECT_TRUE(Displayed(context, EntryOf(context, root, entry))) << "no " << entry;
	}
	EXPECT_FALSE(Displayed(context, EntryOf(context, root, "Low-Pass")));

	Component* filter = object->AddComponent(TypeRegistry::FindType(StringHash("AudioLowPassFilter"))).Get();
	ASSERT_NE(filter, nullptr);
	AnimationCurve* cutoff = FieldOf<AnimationCurve>(filter, "_cutoffCurve");
	ASSERT_NE(cutoff, nullptr);
	cutoff->keys = {Keyframe{.time = 0.0f, .value = 1.0f}, Keyframe{.time = 1.0f, .value = 0.0f}};
	Rebuild(context, root);
	EXPECT_TRUE(Displayed(context, EntryOf(context, root, "Low-Pass")));
	EXPECT_TRUE(UITest::FindByText(context, root, "Controlled by curve").IsValid());
	PanelManager::ClosePanel(_inspector);
}

TEST_F(AudioEditorFixture, VolumeCurveDragIsOneUndo) {
	Handle<Scene> scene = SceneSystem::CreateScene("curves");
	Handle<SceneObject> object = SceneSystem::CreateObject("source", scene);
	Component* source = object->AddComponent(TypeRegistry::FindType(StringHash("AudioSource"))).Get();
	ASSERT_NE(source, nullptr);
	EditorSelection::Select(Handle<Object>(object.Get()));
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	u8* mode = FieldOf<u8>(source, "_rolloffMode");
	AnimationCurve* custom = FieldOf<AnimationCurve>(source, "_customRolloffCurve");
	ASSERT_NE(mode, nullptr);
	ASSERT_NE(custom, nullptr);
	ASSERT_EQ(*mode, 0u);
	const AnimationCurve before = *custom;

	const GraphPlot plot = PlotOf(context, root);
	DragFromTo(context, plot.At(16.0f / 500.0f, 1.0f / 16.0f), plot.At(0.3f, 0.6f));
	EXPECT_EQ(*mode, 2u);
	EXPECT_FALSE(*custom == before);
	u32 between = 0;
	for (const Keyframe& key : custom->keys) {
		between += key.time > 0.1f && key.time < 0.25f ? 1u : 0u;
	}
	EXPECT_EQ(between, 1u);

	ASSERT_TRUE(UndoSystem::Undo());
	EXPECT_EQ(*mode, 0u);
	EXPECT_TRUE(*custom == before);
	PanelManager::ClosePanel(_inspector);
}

TEST_F(AudioEditorFixture, DoubleClickAddsKeyDeleteRestores) {
	Handle<Scene> scene = SceneSystem::CreateScene("curves");
	Handle<SceneObject> object = SceneSystem::CreateObject("source", scene);
	Component* source = object->AddComponent(TypeRegistry::FindType(StringHash("AudioSource"))).Get();
	ASSERT_NE(source, nullptr);
	f32* blend = FieldOf<f32>(source, "_spatialBlend");
	AnimationCurve* curve = FieldOf<AnimationCurve>(source, "_spatialBlendCurve");
	ASSERT_NE(blend, nullptr);
	ASSERT_NE(curve, nullptr);
	*blend = 0.5f;
	EditorSelection::Select(Handle<Object>(object.Get()));
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);
	EXPECT_FALSE(UITest::FindByText(context, root, "Controlled by curve").IsValid());

	const GraphPlot plot = PlotOf(context, root);
	const Vec2 on = plot.At(0.6f, 0.5f);
	ClickAt(context, on);
	ClickAt(context, on);
	ASSERT_EQ(curve->keys.size(), 2u);
	EXPECT_NEAR(curve->keys[1].time, 0.6f, 0.01f);
	EXPECT_NEAR(curve->keys[1].value, 0.5f, 1e-4f);
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	EXPECT_TRUE(UITest::FindByText(context, root, "Controlled by curve").IsValid());

	UITest::RightClickAt(context, static_cast<i32>(on.x), static_cast<i32>(on.y));
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	const UI::ElementHandle remove = UITest::FindByText(context, context.GetOpenPopup(0), "Delete Key");
	ASSERT_TRUE(remove.IsValid());
	UITest::ClickElement(context, remove);
	EXPECT_TRUE(curve->keys.empty());
	EXPECT_EQ(*blend, 0.5f);
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	EXPECT_FALSE(UITest::FindByText(context, root, "Controlled by curve").IsValid());
	PanelManager::ClosePanel(_inspector);
}

TEST_F(AudioEditorFixture, TheKeyMenuTicksTheKeysMode) {
	Handle<Scene> scene = SceneSystem::CreateScene("curves");
	Handle<SceneObject> object = SceneSystem::CreateObject("source", scene);
	Component* source = object->AddComponent(TypeRegistry::FindType(StringHash("AudioSource"))).Get();
	ASSERT_NE(source, nullptr);
	*FieldOf<f32>(source, "_spatialBlend") = 0.5f;
	AnimationCurve* curve = FieldOf<AnimationCurve>(source, "_spatialBlendCurve");
	ASSERT_NE(curve, nullptr);
	EditorSelection::Select(Handle<Object>(object.Get()));
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	const Vec2 on = PlotOf(context, root).At(0.6f, 0.5f);
	ClickAt(context, on);
	ClickAt(context, on);
	ASSERT_EQ(curve->keys.size(), 2u);
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();

	const auto ticked = [&](const char* label) {
		const UI::Element* text = context.GetElement(UITest::FindByText(context, context.GetOpenPopup(0), label));
		const UI::ElementHandle row = text ? context.MakeHandle(text->parent) : UI::ElementHandle{};
		return row.IsValid() && UITest::FindByClass(context, row, StringHash("slt-check-mark"), 0).IsValid();
	};
	const Vec2 key = PlotOf(context, root).At(0.6f, 0.5f);
	UITest::RightClickAt(context, static_cast<i32>(key.x), static_cast<i32>(key.y));
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	EXPECT_TRUE(ticked("Clamped Auto"));
	EXPECT_FALSE(ticked("Auto"));
	EXPECT_FALSE(ticked("Broken"));

	UITest::ClickElement(context, UITest::FindByText(context, context.GetOpenPopup(0), "Broken"));
	ASSERT_EQ(curve->keys.size(), 2u);
	EXPECT_TRUE(curve->keys[1].broken);
	EXPECT_EQ(curve->keys[1].inTangentMode, KeyframeTangentMode::Free);
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	UITest::RightClickAt(context, static_cast<i32>(key.x), static_cast<i32>(key.y));
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	EXPECT_TRUE(ticked("Broken"));
	EXPECT_FALSE(ticked("Clamped Auto"));
	PanelManager::ClosePanel(_inspector);
}

TEST_F(AudioEditorFixture, GraphSurvivesInspectorRebuild) {
	Handle<Scene> scene = SceneSystem::CreateScene("curves");
	Handle<SceneObject> object = SceneSystem::CreateObject("source", scene);
	Component* source = object->AddComponent(TypeRegistry::FindType(StringHash("AudioSource"))).Get();
	ASSERT_NE(source, nullptr);
	f32* blend = FieldOf<f32>(source, "_spatialBlend");
	AnimationCurve* blendCurve = FieldOf<AnimationCurve>(source, "_spatialBlendCurve");
	AnimationCurve* spreadCurve = FieldOf<AnimationCurve>(source, "_spreadCurve");
	ASSERT_NE(blend, nullptr);
	ASSERT_NE(blendCurve, nullptr);
	ASSERT_NE(spreadCurve, nullptr);
	*blend = 0.5f;
	EditorSelection::Select(Handle<Object>(object.Get()));
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	const UI::ElementHandle first = UITest::FindByClass(context, root, EditorTheme::Class::Graph, 0);
	const Vec2 on = PlotOf(context, root).At(0.6f, 0.5f);
	ClickAt(context, on);
	ClickAt(context, on);
	ASSERT_EQ(blendCurve->keys.size(), 2u);
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	ASSERT_FALSE(context.IsAlive(first));
	InputEvent press{};
	press.type = InputEventType::KeyDown;
	press.key = Key::Delete;
	context.InjectInput(press);
	context.Update();
	EXPECT_TRUE(blendCurve->keys.empty());
	EXPECT_EQ(*blend, 0.5f);
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();

	UITest::ClickElement(context, EntryOf(context, root, "Spread"));
	const Vec2 spread = PlotOf(context, root).At(0.3f, 0.0f);
	ClickAt(context, spread);
	ClickAt(context, spread);
	ASSERT_EQ(spreadCurve->keys.size(), 2u);
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	const Vec2 hidden = PlotOf(context, root).At(0.6f, 0.5f);
	ClickAt(context, hidden);
	ClickAt(context, hidden);
	EXPECT_TRUE(blendCurve->keys.empty());
	PanelManager::ClosePanel(_inspector);
}

TEST_F(AudioEditorFixture, RebuiltGraphKeepsKeyboardState) {
	Handle<Scene> scene = SceneSystem::CreateScene("curves");
	Handle<SceneObject> object = SceneSystem::CreateObject("source", scene);
	Component* source = object->AddComponent(TypeRegistry::FindType(StringHash("AudioSource"))).Get();
	ASSERT_NE(source, nullptr);
	AnimationCurve* curve = FieldOf<AnimationCurve>(source, "_spatialBlendCurve");
	ASSERT_NE(curve, nullptr);
	EditorSelection::Select(Handle<Object>(object.Get()));
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	const UI::ElementHandle first = UITest::FindByClass(context, root, EditorTheme::Class::Graph, 0);
	ClickAt(context, PlotOf(context, root).At(0.5f, 1.05f));
	ASSERT_TRUE(context.GetFocused() == first);
	UITest::ClickElement(context, UITest::FindByClass(context, root, StringHash("slt-textfield"), 0));
	ASSERT_FALSE(context.GetFocused() == first);
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	curve->keys.push_back(Keyframe{.time = 0.0f, .value = 0.0f});
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	ASSERT_FALSE(context.IsAlive(first));
	EXPECT_FALSE(context.GetFocused() == UITest::FindByClass(context, root, EditorTheme::Class::Graph, 0));
	PanelManager::ClosePanel(_inspector);
}

TEST_F(AudioEditorFixture, DistanceLabelsSitOverDistances) {
	Handle<Scene> scene = SceneSystem::CreateScene("curves");
	Handle<SceneObject> object = SceneSystem::CreateObject("source", scene);
	Component* source = object->AddComponent(TypeRegistry::FindType(StringHash("AudioSource"))).Get();
	ASSERT_NE(source, nullptr);
	f32* max = FieldOf<f32>(source, "_maxDistance");
	ASSERT_NE(max, nullptr);
	*max = 40.0f;
	Handle<SceneObject> ear = SceneSystem::CreateObject("listener", scene);
	ASSERT_TRUE(ear->AddComponent(TypeRegistry::FindType(StringHash("AudioListener"))).IsValid());
	ear->SetLocalPosition(Vec3(0.0f, 0.0f, -10.0f));
	EditorSelection::Select(Handle<Object>(object.Get()));
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);
	for (u32 tick = 0; tick < 2; ++tick) {
		PanelManager::TickAll(1.0f / 60.0f);
		context.Update();
	}

	const GraphPlot plot = PlotOf(context, root);
	const UI::ElementHandle graphElement = UITest::FindByClass(context, root, EditorTheme::Class::Graph, 0);
	const auto centre = [&](std::string_view text) {
		const UI::Rect box = context.GetLayout(UITest::FindByText(context, graphElement, text)).borderBox;
		return box.x + box.w * 0.5f;
	};
	for (const char* text : {"20", "40", "Listener"}) {
		ASSERT_TRUE(UITest::FindByText(context, graphElement, text).IsValid()) << "no '" << text;
	}
	EXPECT_NEAR(centre("20"), plot.At(0.5f, 0.0f).x, 1.5f);
	EXPECT_NEAR(centre("Listener"), plot.At(0.25f, 0.0f).x, 1.5f);
	const UI::Rect graph = context.GetLayout(graphElement).borderBox;
	const UI::Rect end = context.GetLayout(UITest::FindByText(context, graphElement, "40")).borderBox;
	EXPECT_GT(centre("40"), plot.At(0.75f, 0.0f).x);
	EXPECT_LE(end.x + end.w, graph.x + graph.w + 0.5f);
	PanelManager::ClosePanel(_inspector);
}

TEST_F(AudioEditorFixture, RolloffFlipsMinDistanceNextTick) {
	Handle<Scene> scene = SceneSystem::CreateScene("curves");
	Handle<SceneObject> object = SceneSystem::CreateObject("source", scene);
	Component* source = object->AddComponent(TypeRegistry::FindType(StringHash("AudioSource"))).Get();
	ASSERT_NE(source, nullptr);
	u8* mode = FieldOf<u8>(source, "_rolloffMode");
	ASSERT_NE(mode, nullptr);
	EditorSelection::Select(Handle<Object>(object.Get()));
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {420, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);
	ASSERT_FALSE(UITest::FindByText(context, root, "Controlled by curve").IsValid());

	*mode = 2;
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	EXPECT_TRUE(UITest::FindByText(context, root, "Controlled by curve").IsValid());
	*mode = 1;
	PanelManager::TickAll(1.0f / 60.0f);
	context.Update();
	EXPECT_FALSE(UITest::FindByText(context, root, "Controlled by curve").IsValid());
	PanelManager::ClosePanel(_inspector);
}

namespace {

	struct AudioComponentSweep {
		const void* image = nullptr;
		u32 visited = 0;
		Vector<str> inheriting{GetMemoryResource(MemTag::Editor)};
		Vector<str> notInAudio{GetMemoryResource(MemTag::Editor)};
	};

	void VisitAudioComponent(const TypeInfo* type, void* user) {
		auto& sweep = *static_cast<AudioComponentSweep*>(user);
		bool component = false;
		for (const TypeInfo* walk = type; walk != nullptr; walk = walk->parent) {
			component |= walk == Component::GetReflectType();
		}
		if (!type || type->ownerModule != sweep.image || !component) {
			return;
		}
		++sweep.visited;
		const str name = type->displayName ? str(type->displayName) : str("<unnamed>");
		if (EditorIcons::ComponentIcons::For(type) == EditorIcons::ComponentIcons::For(type->parent)) {
			sweep.inheriting.push_back(name);
		}
		if (ComponentMenu::CategoryFor(type) != "Audio") {
			sweep.notInAudio.push_back(name);
		}
	}

} // namespace

TEST_F(AudioEditorFixture, AudioComponentsHaveBadgesAndIcons) {
	EditorIcons::ComponentIcons::RegisterBuiltIns();
	ComponentMenu::RegisterBuiltIns();
	const TypeInfo* sourceType = TypeRegistry::FindType(StringHash("AudioSource"));
	ASSERT_NE(sourceType, nullptr);
	AudioComponentSweep sweep;
	sweep.image = sourceType->ownerModule;
	TypeRegistry::ForEachType(&VisitAudioComponent, &sweep);
	ASSERT_GE(sweep.visited, 10u) << sweep.visited << " audio components";
	str inheriting;
	for (const str& name : sweep.inheriting) {
		inheriting += name + " ";
	}
	str loose;
	for (const str& name : sweep.notInAudio) {
		loose += name + " ";
	}
	EXPECT_TRUE(sweep.inheriting.empty()) << inheriting;
	EXPECT_TRUE(sweep.notInAudio.empty()) << loose;

	Handle<Scene> scene = SceneSystem::CreateScene("icons");
	const auto make = [&](const char* type) {
		Handle<SceneObject> object = SceneSystem::CreateObject(type, scene);
		EXPECT_TRUE(object->AddComponent(TypeRegistry::FindType(StringHash(type))).IsValid()) << type;
		return object;
	};
	const Handle<SceneObject> lone[] = {make("AudioSource"), make("AudioListener"), make("AudioReverbZone")};
	const Handle<SceneObject> filter = make("AudioLowPassFilter");
	const auto iconsOn = [&](const Handle<SceneObject>& object) {
		Vector<DebugLine> lines{GetMemoryResource(MemTag::Editor)};
		Vector<DebugLine> overlay{GetMemoryResource(MemTag::Editor)};
		Vector<SceneIcon> icons{GetMemoryResource(MemTag::Editor)};
		GizmoDrawContext context;
		context.lines = &lines;
		context.overlayLines = &overlay;
		context.icons = &icons;
		EditorGizmos::DrawSceneGizmos(context);
		u32 count = 0;
		for (const SceneIcon& icon : icons) {
			count += icon.object == object ? 1u : 0u;
		}
		return count;
	};
	for (const Handle<SceneObject>& object : lone) {
		EXPECT_EQ(iconsOn(object), 1u) << object->name;
	}
	EXPECT_EQ(iconsOn(filter), 0u);

	EditorExtensions::Unload(_moduleBase);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(sourceType), EditorIcons::Mark::Component);
	EXPECT_TRUE(ComponentMenu::CategoryFor(sourceType).empty());
	EXPECT_EQ(iconsOn(lone[0]), 0u);
}

TEST_F(AudioEditorFixture, AudioSettingsLabelledInOrder) {
	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
	Settings::GetCategories(categories);
	const TypeInfo* type = nullptr;
	for (const SettingsCategoryInfo& category : categories) {
		type = category.path == "Audio" ? category.type : type;
	}
	ASSERT_NE(type, nullptr);
	ASSERT_GT(type->fieldCount, 8u);

	str unworded;
	Vector<str> labels{GetMemoryResource(MemTag::Editor)};
	for (u32 index = 0; index < type->fieldCount; ++index) {
		const FieldInfo& field = type->fields[index];
		const SettingsFieldPresentation* words = SettingsPresentation::FindField("Audio", field.displayName ? field.displayName : "");
		if (!words || words->label.empty()) {
			unworded += str(field.displayName ? field.displayName : "<unnamed>") + " ";
			continue;
		}
		labels.push_back(words->label);
	}
	EXPECT_TRUE(unworded.empty()) << unworded;

	size_t after = 0;
	for (const char* label : {"Global Volume", "Volume Rolloff Scale", "Doppler Factor", "DSP Buffer Size", "Max Virtual Voices",
							  "Max Real Voices", "Spatializer Plugin", "Virtualize Effect"}) {
		const auto at = std::find(labels.begin(), labels.end(), str(label));
		ASSERT_NE(at, labels.end()) << "no '" << label << "' row";
		EXPECT_GE(static_cast<size_t>(at - labels.begin()), after) << "'" << label;
		after = static_cast<size_t>(at - labels.begin());
	}
	const SettingsCategoryPresentation* page = SettingsPresentation::FindCategory("Audio");
	ASSERT_NE(page, nullptr);
	EXPECT_EQ(page->audience, SettingsAudience::EditorAndPlayer);
	const SettingsCategoryPresentation* preferences = SettingsPresentation::FindCategory("Editor/Audio");
	ASSERT_NE(preferences, nullptr);
	EXPECT_EQ(preferences->audience, SettingsAudience::EditorOnly);
}

TEST_F(AudioEditorFixture, RuntimeViewMuteIsUserPreference) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	UI::Context context(desc, &fonts);
	Handle<EditorPanel> view = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("RuntimeViewPanel")));
	ASSERT_TRUE(view.IsValid());
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	view.Get()->BuildContent(context, root);
	context.Update();
	const auto tick = [&]() {
		PanelManager::TickAll(1.0f / 60.0f);
		context.Update();
	};
	const auto button = [&]() {
		return UITest::ControlWearing(context, root, EditorIcons::GetGlyph(EditorIcons::Glyph::MuteAudio), EditorTheme::Class::ToolButton);
	};
	ASSERT_TRUE(button().IsValid());

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
	Settings::GetCategories(categories);
	bool* muted = nullptr;
	for (const SettingsCategoryInfo& category : categories) {
		if (category.path == "Editor/Audio" && category.type) {
			const FieldInfo* field = FindField(category.type, StringHash("muteAudio"));
			muted = field && field->addrFn ? static_cast<bool*>(field->addrFn(category.live)) : nullptr;
		}
	}
	ASSERT_NE(muted, nullptr);
	EXPECT_FALSE(*muted);

	UITest::ClickElement(context, button());
	EXPECT_TRUE(*muted);
	tick();
	EXPECT_TRUE(context.HasClass(button(), EditorTheme::Class::ToolButtonOn));
	UITest::ClickElement(context, button());
	EXPECT_FALSE(*muted);

	EditorExtensions::Unload(_moduleBase);
	tick();
	EXPECT_FALSE(button().IsValid());
	PanelManager::ClosePanel(view);
}

TEST_F(AudioEditorFixture, ProfilerHasAudioVoiceRow) {
	const Guid guid = Stage("stereo_44100_pcm16.wav");
	if (!guid.IsValid()) {
		GTEST_SKIP() << "audio fixtures not found";
	}
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {1600, 2400};
	UI::Context context(desc, &fonts);
	const auto openProfiler = [&context](Handle<EditorPanel>& panel) {
		panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("ProfilerPanel")));
		UI::ElementHandle root = context.CreateElement();
		context.AppendChild(context.GetRoot(), root);
		if (panel.IsValid()) {
			panel.Get()->BuildContent(context, root);
		}
		context.Update();
		return root;
	};
	Handle<EditorPanel> left;
	Handle<EditorPanel> right;
	const UI::ElementHandle leftRoot = openProfiler(left);
	const UI::ElementHandle rightRoot = openProfiler(right);
	ASSERT_TRUE(left.IsValid() && right.IsValid());
	const auto tick = [&context]() {
		EndFrame();
		PanelManager::TickAll(0.3f);
		context.Update();
	};
	tick();
	for (UI::ElementHandle root : {leftRoot, rightRoot}) {
		EXPECT_TRUE(FindByText(context, root, "Audio").IsValid());
		EXPECT_TRUE(FindByText(context, root, "Reset play count on play").IsValid());
		EXPECT_TRUE(FindByTextContaining(context, root, "Audio Sources: ").IsValid());
		EXPECT_TRUE(FindByTextContaining(context, root, "Underruns: 0").IsValid());
		EXPECT_TRUE(FindByText(context, root, "Audibility").IsValid());
	}

	EditorSelection::SelectAsset(guid);
	UI::ElementHandle inspectorRoot;
	BuildInspector(context, inspectorRoot);
	const UI::ElementHandle header = PaneHeader(context, inspectorRoot);
	ClickElement(context, FindByClass(context, header, EditorTheme::Class::IconButton, 2));
	ClickElement(context, FindByClass(context, header, EditorTheme::Class::IconButton, 0));
	for (int frame = 0; frame < 4; ++frame) {
		tick();
	}
	const auto plays = [&context](UI::ElementHandle root) {
		const UI::ElementHandle asset = FindByText(context, root, "stereo_44100_pcm16");
		if (!asset.IsValid()) {
			return str("no row");
		}
		const UI::Element* row = context.GetElement(context.MakeHandle(context.GetElement(asset)->parent));
		UI::ElementHandle cell = context.MakeHandle(row->firstChild);
		for (int column = 0; column < 6; ++column) {
			cell = context.MakeHandle(context.GetElement(cell)->nextSibling);
		}
		return context.GetTextString(cell);
	};
	EXPECT_EQ(plays(leftRoot), "1");
	EXPECT_EQ(plays(rightRoot), "1");

	EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(), EditorPlayModeChangedEvent{PlayModeState::Playing});
	tick();
	tick();
	EXPECT_EQ(plays(leftRoot), "");
	EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(), EditorPlayModeChangedEvent{PlayModeState::Editing});

	EditorExtensions::Unload(_moduleBase);
	tick();
	EXPECT_FALSE(FindByText(context, leftRoot, "Reset play count on play").IsValid());
	EXPECT_FALSE(FindByText(context, rightRoot, "Reset play count on play").IsValid());
	PanelManager::ClosePanel(left);
	PanelManager::ClosePanel(right);
}

TEST_F(AudioEditorFixture, SourceAndZoneDrawBothSpheres) {
	Handle<Scene> scene = SceneSystem::CreateScene("ranges");
	Handle<SceneObject> sourceObject = SceneSystem::CreateObject("source", scene);
	sourceObject->SetLocalPosition(Vec3(1.0f, 2.0f, 3.0f));
	Component* source = sourceObject->AddComponent(TypeRegistry::FindType(StringHash("AudioSource"))).Get();
	ASSERT_NE(source, nullptr);
	*FieldOf<f32>(source, "_minDistance") = 2.0f;
	*FieldOf<f32>(source, "_maxDistance") = 30.0f;
	ASSERT_EQ(*FieldOf<u8>(source, "_rolloffMode"), 0u);

	const auto offered = [](const Vector<ComponentRadius>& radii, const char* field) -> const RadiusHandle* {
		for (const ComponentRadius& radius : radii) {
			if (radius.handle.field == StringHash(field)) {
				return &radius.handle;
			}
		}
		return nullptr;
	};
	EditorSelection::Select(Handle<Object>(sourceObject.Get()));
	Vector<ComponentRadius> radii{GetMemoryResource(MemTag::Editor)};
	EditorGizmos::GatherRadiusHandles(radii);
	ASSERT_EQ(radii.size(), 2u);
	ASSERT_NE(offered(radii, "_minDistance"), nullptr);
	ASSERT_NE(offered(radii, "_maxDistance"), nullptr);
	EXPECT_EQ(offered(radii, "_minDistance")->radius, 2.0f);
	EXPECT_EQ(offered(radii, "_maxDistance")->radius, 30.0f);
	EXPECT_EQ(offered(radii, "_maxDistance")->center, Vec3(1.0f, 2.0f, 3.0f));

	Vector<DebugLine> lines{GetMemoryResource(MemTag::Editor)};
	Vector<DebugLine> overlay{GetMemoryResource(MemTag::Editor)};
	GizmoDrawContext context;
	context.lines = &lines;
	context.overlayLines = &overlay;
	EditorGizmos::DrawSceneGizmos(context);
	f32 reach = 0.0f;
	for (const DebugLine& line : lines) {
		reach = Math::Max(reach, Math::Max((line.a - Vec3(1.0f, 2.0f, 3.0f)).Length(), (line.b - Vec3(1.0f, 2.0f, 3.0f)).Length()));
	}
	EXPECT_NEAR(reach, 30.0f, 0.01f);

	Handle<SceneObject> zoneObject = SceneSystem::CreateObject("zone", scene);
	ASSERT_TRUE(zoneObject->AddComponent(TypeRegistry::FindType(StringHash("AudioReverbZone"))).IsValid());
	EditorSelection::Select(Handle<Object>(zoneObject.Get()));
	EditorGizmos::GatherRadiusHandles(radii);
	EXPECT_EQ(radii.size(), 2u);
	EXPECT_NE(offered(radii, "_minDistance"), nullptr);
	EXPECT_NE(offered(radii, "_maxDistance"), nullptr);
}

namespace {

	constexpr const char* WINDOW_MIXER = R"({
	"startSnapshot": "Explore",
	"groups": [
		{ "id": "Master" },
		{ "id": "Music", "parent": "Master" },
		{ "id": "SFX", "parent": "Master", "clock": "Gameplay",
		  "units": [ { "kind": "Send", "target": "Reverb" }, { "kind": "Attenuation" } ] },
		{ "id": "Reverb", "parent": "Master" }
	],
	"snapshots": [
		{ "name": "Explore", "values": [ { "group": "Music", "parameter": "Volume", "value": -6 } ] },
		{ "name": "Combat" }
	]
})";

	constexpr StringHash MIXER_STRIP = StringHash("ed-mixer-strip");
	constexpr StringHash MIXER_FADER = StringHash("ed-mixer-fader");
	constexpr StringHash MIXER_SOLO = StringHash("ed-mixer-solo");
	constexpr StringHash MIXER_MUTE = StringHash("ed-mixer-mute");
	constexpr StringHash MIXER_SLOT = StringHash("ed-mixer-slot");
	constexpr StringHash MIXER_ADD = StringHash("ed-mixer-add");
	constexpr StringHash MIXER_CHAIN = StringHash("ed-mixer-chain");
	constexpr StringHash MIXER_UNIT = StringHash("ed-mixer-unit");
	constexpr StringHash MIXER_EDIT_IN_PLAY = StringHash("ed-mixer-edit-in-play");
	constexpr StringHash MIXER_SNAPSHOT_ADD = StringHash("ed-mixer-snapshot-add");
	constexpr StringHash MIXER_GROUP_ADD = StringHash("ed-mixer-group-add");
	constexpr StringHash MIXER_EXPOSED = StringHash("ed-mixer-exposed");
	constexpr StringHash MIXER_EXPOSED_ROW = StringHash("ed-mixer-exposed-row");
	constexpr StringHash MIXER_VIEW_ADD = StringHash("ed-mixer-view-add");
	constexpr StringHash MIXER_EYE = StringHash("ed-mixer-eye");
	constexpr StringHash MIXER_COLOR_TAG = StringHash("ed-mixer-color");
	constexpr StringHash MIXER_MIXER_ADD = StringHash("ed-mixer-mixer-add");
	constexpr const char* EXPOSED_VOLUME = "Volume \xE2\x86\x92";
	constexpr StringHash SLIDER_TRACK = StringHash("slt-slider-track");
	constexpr StringHash SLIDER_KNOB = StringHash("slt-slider-knob");

	class AudioMixerWindowFixture : public AudioEditorFixture {
	protected:
		MockFontSystem _fonts;
		std::unique_ptr<UI::Context> _context;
		Handle<EditorPanel> _window;
		UI::ElementHandle _windowRoot;
		Guid _mixer;
		str _savedText;
		std::unique_ptr<LinearAllocator> _savedArena;
		JSONValue _saved;

		void SetUp() override {
			AudioEditorFixture::SetUp();
			EditorAssetOps::Initialize();
		}

		void TearDown() override {
			_context.reset();
			EditorAssetOps::Terminate();
			AudioEditorFixture::TearDown();
		}

		Guid StageMixer(const char* document = WINDOW_MIXER) {
			FileSystem::WriteTextFile(Path((_root / "Assets" / "Game.mixer").generic_string()), document);
			AssetDatabase::ScanProjectDirectory();
			return AssetDatabase::LookupGuid(Path("project://Assets/Game.mixer"));
		}

		void OpenOn(const Guid& mixer) {
			_mixer = mixer;
			EditorSelection::SelectAsset(mixer);
			UI::ContextDesc desc;
			desc.sizePx = {1400, 700};
			_context = std::make_unique<UI::Context>(desc, &_fonts);
			_window = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("AudioMixerWindow")));
			ASSERT_TRUE(_window.IsValid());
			_windowRoot = _context->CreateElement();
			_context->AppendChild(_context->GetRoot(), _windowRoot);
			_window.Get()->BuildContent(*_context, _windowRoot);
			Tick();
		}

		void Tick() {
			for (u32 pass = 0; pass < 2; ++pass) {
				_window.Get()->OnTick(1.0f / 60.0f);
				_context->Update();
			}
		}

		str StripTitles() {
			str out;
			for (u32 index = 0;; ++index) {
				const UI::ElementHandle strip = UITest::FindByClass(*_context, _windowRoot, MIXER_STRIP, index);
				if (!strip.IsValid()) {
					break;
				}
				const UI::Element* header = _context->GetElement(UITest::NthChildOf(*_context, strip, 0));
				const str title = header ? _context->GetTextString(_context->MakeHandle(header->firstChild)) : str();
				out += (out.empty() ? "" : " ") + title;
			}
			return out;
		}

		UI::ElementHandle Strip(u32 index) { return UITest::FindByClass(*_context, _windowRoot, MIXER_STRIP, index); }
		UI::ElementHandle Fader(u32 strip) { return UITest::FindByClass(*_context, Strip(strip), MIXER_FADER, 0); }

		str FaderText(u32 strip) {
			const UI::Element* fader = _context->GetElement(Fader(strip));
			return fader ? _context->GetTextString(_context->MakeHandle(fader->firstChild)) : str("<no fader>");
		}

		bool Unsaved() { return UITest::FindByTextContaining(*_context, _windowRoot, "Game.mixer *").IsValid(); }

		bool Click(std::string_view text) {
			const UI::ElementHandle target = UITest::FindByText(*_context, _windowRoot, text);
			if (!target.IsValid()) {
				return false;
			}
			UITest::ClickElement(*_context, target);
			Tick();
			return true;
		}

		void Save() {
			EXPECT_TRUE(Click("Save"));
			_savedText = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/Game.mixer")), MemTag::Editor);
			_savedArena = std::make_unique<LinearAllocator>(_savedText.size() + 64, MemTag::Editor);
			_saved = JSONParse(_savedText, *_savedArena);
		}

		const JSONValue* SavedGroup(std::string_view id) {
			const JSONValue& groups = _saved["groups"];
			for (size_t index = 0; groups.IsArray() && index < groups.Size(); ++index) {
				if (groups[index]["id"].AsString() == id) {
					return &groups[index];
				}
			}
			return nullptr;
		}

		f64 SavedVolume(std::string_view group, size_t snapshot = 0) { return SavedValue(group, "Volume", snapshot, 0.0); }

		f64 SavedValue(std::string_view group, std::string_view parameter, size_t snapshot = 0, f64 missing = -1.0e9) {
			const JSONValue& values = _saved["snapshots"][snapshot]["values"];
			for (size_t index = 0; values.IsArray() && index < values.Size(); ++index) {
				if (values[index]["group"].AsString() == group && values[index]["parameter"].AsString() == parameter) {
					return values[index]["value"].AsNumber();
				}
			}
			return missing;
		}

		UI::ElementHandle Chain() { return UITest::FindByClass(*_context, _windowRoot, MIXER_CHAIN, 0); }

		bool ClickIn(UI::ElementHandle root, std::string_view text) {
			const UI::ElementHandle target = UITest::FindByText(*_context, root, text);
			if (!target.IsValid()) {
				return false;
			}
			UITest::ClickElement(*_context, target);
			Tick();
			return true;
		}

		bool ClickMenu(std::string_view text) { return ClickIn(_context->GetRoot(), text); }

		void Type(std::string_view text) {
			for (char letter : text) {
				InputEvent typed{};
				typed.type = InputEventType::Text;
				typed.codepoint = static_cast<char32_t>(letter);
				_context->InjectInput(typed);
			}
			InputEvent commit{};
			commit.type = InputEventType::KeyDown;
			commit.key = Key::Enter;
			_context->InjectInput(commit);
			_context->Update();
			Tick();
		}

		static void SetPlayMode(PlayModeState state) { EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(), {state}); }

		str HeardInPlay() { return FaderText(1); }

		UI::ElementHandle EditInPlay() { return UITest::FindByClass(*_context, _windowRoot, MIXER_EDIT_IN_PLAY, 0); }
		void ClickEditInPlay() {
			UITest::ClickElement(*_context, EditInPlay());
			Tick();
		}

		str SavedSnapshots() {
			str out;
			const JSONValue& snapshots = _saved["snapshots"];
			for (size_t index = 0; snapshots.IsArray() && index < snapshots.Size(); ++index) {
				out += (out.empty() ? "" : " ") + str(snapshots[index]["name"].AsString());
			}
			return out;
		}

		bool Ticked(const char* label) {
			if (_context->GetOpenPopupCount() == 0) {
				return false;
			}
			const UI::Element* text = _context->GetElement(UITest::FindByText(*_context, _context->GetOpenPopup(_context->GetOpenPopupCount() - 1), label));
			const UI::ElementHandle row = text ? _context->MakeHandle(text->parent) : UI::ElementHandle{};
			return row.IsValid() && UITest::FindByClass(*_context, row, StringHash("slt-check-mark"), 0).IsValid();
		}

		void RightClickName(const char* label) {
			const UI::ElementHandle name = UITest::FindByText(*_context, Chain(), label);
			ASSERT_TRUE(name.IsValid()) << label;
			UITest::RightClickElement(*_context, name);
			Tick();
		}

		void CloseTopPopup() {
			InputEvent escape{};
			escape.type = InputEventType::KeyDown;
			escape.key = Key::Escape;
			_context->InjectInput(escape);
			_context->Update();
			Tick();
		}

		bool EditInPlayShown() {
			const UI::ElementHandle toggle = UITest::FindByClass(*_context, _windowRoot, MIXER_EDIT_IN_PLAY, 0);
			return toggle.IsValid() && _context->GetStyle(toggle).display != UI::Display::None;
		}

		UI::ElementHandle Eye(u32 row) { return UITest::FindByClass(*_context, _windowRoot, MIXER_EYE, row); }

		bool ClickTopMenu(std::string_view text) {
			if (_context->GetOpenPopupCount() == 0) {
				return false;
			}
			return ClickIn(_context->GetOpenPopup(_context->GetOpenPopupCount() - 1), text);
		}

		void CtrlClick(UI::ElementHandle element) {
			const UI::Rect rect = _context->GetLayout(element).borderBox;
			const Vec2I at{static_cast<i32>(rect.x + rect.w * 0.5f), static_cast<i32>(rect.y + rect.h * 0.5f)};
			InputEvent move{};
			move.type = InputEventType::MouseMove;
			move.mousePosPx = at;
			_context->InjectInput(move);
			for (const InputEventType type : {InputEventType::MouseDown, InputEventType::MouseUp}) {
				InputEvent press{};
				press.type = type;
				press.button = MouseButton::Left;
				press.mousePosPx = at;
				press.mods = KeyMod::Ctrl;
				_context->InjectInput(press);
			}
			_context->Update();
			Tick();
		}

		Guid StageSubMixer(const Guid& game) {
			const str document = std::format(R"({{ "output": {{ "mixer": "{}", "group": "Music" }},
				"groups": [ {{ "id": "Master" }} ], "snapshots": [ {{ "name": "Snapshot" }} ] }})",
											 game.ToString());
			FileSystem::WriteTextFile(Path((_root / "Assets" / "Sub.mixer").generic_string()), document);
			AssetDatabase::ScanProjectDirectory();
			return AssetDatabase::LookupGuid(Path("project://Assets/Sub.mixer"));
		}

		str MixerRows() {
			str out;
			for (const char* name : {"Game", "Sub", "Ambience", "Ambient"}) {
				const UI::ElementHandle label = UITest::FindByText(*_context, _windowRoot, name);
				if (!label.IsValid()) {
					continue;
				}
				const UI::Element* text = _context->GetElement(label);
				const UI::Element* row = text ? _context->GetElement(_context->MakeHandle(text->parent)) : nullptr;
				str where;
				for (u32 child = row ? row->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
					const UI::ElementHandle handle = _context->MakeHandle(child);
					const str words = _context->GetTextString(handle);
					if (words.starts_with("(")) {
						where = words;
					}
					const UI::Element* next = _context->GetElement(handle);
					child = next ? next->nextSibling : UI::INVALID_UI_INDEX;
				}
				out += (out.empty() ? "" : "; ") + str(name) + " " + where;
			}
			return out;
		}

		bool DropMixer(const Guid& dragged, const char* onto) {
			UI::DragPayload payload;
			payload.type = StringHash("AudioMixerAsset");
			payload.data = Variant(dragged.ToString() + "|");
			Vec2 at;
			if (onto) {
				const UI::ElementHandle target = UITest::FindByText(*_context, _windowRoot, onto);
				if (!target.IsValid()) {
					ADD_FAILURE() << "no " << onto << " row";
					return false;
				}
				const UI::Rect row = _context->GetLayout(target).borderBox;
				at = Vec2(row.x + row.w * 0.5f, row.y + row.h * 0.5f);
			} else {
				const UI::Element* title = _context->GetElement(UITest::FindByText(*_context, _windowRoot, "Mixers"));
				const UI::Element* head = title ? _context->GetElement(_context->MakeHandle(title->parent)) : nullptr;
				if (!head) {
					ADD_FAILURE() << "no Mixers panel";
					return false;
				}
				const UI::Rect column = _context->GetLayout(_context->MakeHandle(head->parent)).borderBox;
				at = Vec2(column.x + column.w * 0.5f, column.y + column.h - 3.0f);
			}
			const bool taken = _context->DeliverDrop(payload, at);
			Tick();
			return taken;
		}

		str SavedViews() {
			str out;
			const JSONValue& views = _saved["views"];
			for (size_t index = 0; views.IsArray() && index < views.Size(); ++index) {
				out += (out.empty() ? "" : "; ") + str(views[index]["name"].AsString()) + ":";
				const JSONValue& groups = views[index]["groups"];
				for (size_t group = 0; groups.IsArray() && group < groups.Size(); ++group) {
					out += " " + str(groups[group].AsString());
				}
			}
			return out;
		}
	};

} // namespace

TEST_F(AudioMixerWindowFixture, WindowDrawsStripsInTreeOrder) {
	const Guid mixer = StageMixer();
	ASSERT_TRUE(mixer.IsValid());
	OpenOn(mixer);
	EXPECT_EQ(StripTitles(), "Master Music SFX Reverb");
	EXPECT_TRUE(UITest::FindByTextContaining(*_context, _windowRoot, "Game.mixer").IsValid());
	EXPECT_EQ(FaderText(1), "-6");

	const Guid clip = Stage("stereo_44100_pcm16.wav");
	if (clip.IsValid()) {
		EditorSelection::SelectAsset(clip);
		Tick();
		EXPECT_EQ(StripTitles(), "Master Music SFX Reverb");
	}
}

TEST_F(AudioMixerWindowFixture, FaderDragWritesStartSnapshot) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Fader(1).IsValid());
	EXPECT_FALSE(Unsaved());

	UITest::DragElementBy(*_context, Fader(1), 0, 40);
	Tick();
	EXPECT_TRUE(Unsaved());
	const i32 shown = std::atoi(FaderText(1).c_str());
	EXPECT_LT(shown, -6);
	Save();
	EXPECT_LT(SavedVolume("Music"), -6.0);
	EXPECT_NEAR(SavedVolume("Music"), static_cast<f64>(shown), 0.5);
	EXPECT_EQ(SavedVolume("Music", 1), 0.0);
	EXPECT_FALSE(Unsaved());

	UITest::DragElementBy(*_context, Fader(1), 0, -40);
	Tick();
	Save();
	EXPECT_NEAR(SavedVolume("Music"), -6.0, 0.5);
}

TEST_F(AudioMixerWindowFixture, SoloAndMuteWriteGroup) {
	OpenOn(StageMixer());
	const UI::ElementHandle solo = UITest::FindByClass(*_context, Strip(2), MIXER_SOLO, 0);
	const UI::ElementHandle mute = UITest::FindByClass(*_context, Strip(1), MIXER_MUTE, 0);
	ASSERT_TRUE(solo.IsValid() && mute.IsValid());
	const UI::Brush off = _context->GetStyle(solo).background;

	UITest::ClickElement(*_context, solo);
	UITest::ClickElement(*_context, mute);
	Tick();
	EXPECT_FALSE(_context->GetStyle(solo).background == off);
	Save();
	ASSERT_NE(SavedGroup("SFX"), nullptr);
	EXPECT_TRUE((*SavedGroup("SFX"))["solo"].AsBool()) << _savedText;
	EXPECT_TRUE((*SavedGroup("Music"))["mute"].AsBool());

	UITest::ClickElement(*_context, solo);
	UITest::ClickElement(*_context, mute);
	Tick();
	EXPECT_TRUE(_context->GetStyle(solo).background == off);
	Save();
	EXPECT_FALSE((*SavedGroup("SFX"))["solo"].AsBool());
	EXPECT_FALSE((*SavedGroup("Music"))["mute"].AsBool());
}

TEST_F(AudioMixerWindowFixture, AddedGroupNamedAndSaved) {
	const Guid mixer = StageMixer();
	OpenOn(mixer);
	ASSERT_TRUE(Click("SFX"));
	const UI::ElementHandle add = UITest::FindByClass(*_context, _windowRoot, MIXER_GROUP_ADD, 0);
	ASSERT_TRUE(add.IsValid());
	UITest::ClickElement(*_context, add);
	Tick();
	Type("Footsteps");
	EXPECT_EQ(StripTitles(), "Master Music SFX Footsteps Reverb");

	Vector<AssetProduct> products{GetMemoryResource(MemTag::Editor)};
	const auto has = [&products](std::string_view key) {
		return std::any_of(products.begin(), products.end(), [key](const AssetProduct& product) { return product.key == key; });
	};
	AssetDatabase::GetProducts(mixer, products);
	EXPECT_FALSE(has("Groups/New Group"));
	Save();
	const JSONValue* added = SavedGroup("New Group");
	ASSERT_NE(added, nullptr) << _savedText;
	EXPECT_EQ((*added)["name"].AsString(), "Footsteps");
	EXPECT_EQ((*added)["parent"].AsString(), "SFX");
	AssetDatabase::GetProducts(mixer, products);
	EXPECT_TRUE(has("Groups/New Group"));
	const auto named = std::find_if(products.begin(), products.end(), [](const AssetProduct& product) { return product.key == "Groups/New Group"; });
	ASSERT_NE(named, products.end());
	EXPECT_EQ(named->name, "Footsteps");
	Vector<AssetBrowserOps::Entry> rows{GetMemoryResource(MemTag::Editor)};
	AssetBrowserOps::ScanProducts("Assets/Game.mixer", 0, rows);
	EXPECT_TRUE(std::any_of(rows.begin(), rows.end(), [](const AssetBrowserOps::Entry& row) { return row.name == "Footsteps"; }));
}

TEST_F(AudioMixerWindowFixture, DuplicateAndDeleteActOnSelectedGroup) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("SFX"));
	ASSERT_TRUE(_window.Get()->OnCommand(PanelCommand::Duplicate));
	Tick();
	EXPECT_EQ(StripTitles(), "Master Music SFX Reverb SFX - Copy");

	ASSERT_TRUE(_window.Get()->OnCommand(PanelCommand::Delete));
	Tick();
	EXPECT_EQ(StripTitles(), "Master Music SFX Reverb");
}

TEST_F(AudioMixerWindowFixture, DroppedRowBecomesChild) {
	OpenOn(StageMixer());
	const auto dropOn = [&](const char* group, const char* onto) {
		UI::DragPayload payload;
		payload.type = StringHash("AudioMixerGroup");
		payload.data = Variant(str(group));
		const UI::ElementHandle target = UITest::FindByText(*_context, _windowRoot, onto);
		if (!target.IsValid()) {
			ADD_FAILURE() << "no " << onto << " row";
			return false;
		}
		const UI::Rect row = _context->GetLayout(target).borderBox;
		const bool taken = _context->DeliverDrop(payload, Vec2(row.x + row.w * 0.5f, row.y + row.h * 0.5f));
		Tick();
		return taken;
	};
	EXPECT_TRUE(dropOn("Reverb", "Music"));
	EXPECT_EQ(StripTitles(), "Master Music Reverb SFX");
	EXPECT_FALSE(dropOn("Music", "Reverb"));
	Save();
	EXPECT_EQ((*SavedGroup("Reverb"))["parent"].AsString(), "Music");
	EXPECT_EQ((*SavedGroup("Music"))["parent"].AsString(), "Master");
}

TEST_F(AudioMixerWindowFixture, WindowsShareMixerEdits) {
	OpenOn(StageMixer());
	Handle<EditorPanel> second = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("AudioMixerWindow")));
	ASSERT_TRUE(second.IsValid());
	UI::ElementHandle secondRoot = _context->CreateElement();
	_context->AppendChild(_context->GetRoot(), secondRoot);
	second.Get()->BuildContent(*_context, secondRoot);
	const auto tickBoth = [&]() {
		for (u32 pass = 0; pass < 2; ++pass) {
			_window.Get()->OnTick(1.0f / 60.0f);
			second.Get()->OnTick(1.0f / 60.0f);
			_context->Update();
		}
	};
	tickBoth();
	const auto strips = [&](UI::ElementHandle root) {
		u32 count = 0;
		while (UITest::FindByClass(*_context, root, MIXER_STRIP, count).IsValid()) {
			++count;
		}
		return count;
	};
	ASSERT_EQ(strips(secondRoot), 4u);

	ASSERT_TRUE(Click("SFX"));
	ASSERT_TRUE(_window.Get()->OnCommand(PanelCommand::Duplicate));
	tickBoth();
	EXPECT_EQ(strips(_windowRoot), 5u);
	EXPECT_EQ(strips(secondRoot), 5u);
	PanelManager::ClosePanel(second);
}

TEST_F(AudioMixerWindowFixture, PlayModeHoldsTopologyAndFader) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("SFX"));
	SetPlayMode(PlayModeState::Playing);
	Tick();

	UITest::ClickElement(*_context, UITest::FindByClass(*_context, _windowRoot, MIXER_GROUP_ADD, 0));
	Tick();
	_window.Get()->OnCommand(PanelCommand::Duplicate);
	_window.Get()->OnCommand(PanelCommand::Delete);
	Tick();
	EXPECT_EQ(StripTitles(), "Master Music SFX Reverb");
	ASSERT_TRUE(Fader(1).IsValid());
	UITest::DragElementBy(*_context, Fader(1), 0, 40);
	Tick();
	EXPECT_EQ(FaderText(1), "-6");
	const UI::ElementHandle solo = UITest::FindByClass(*_context, Strip(2), MIXER_SOLO, 0);
	const UI::Brush off = _context->GetStyle(solo).background;
	UITest::ClickElement(*_context, solo);
	Tick();
	EXPECT_FALSE(_context->GetStyle(solo).background == off);

	SetPlayMode(PlayModeState::Editing);
	Tick();
	UITest::DragElementBy(*_context, Fader(1), 0, 40);
	Tick();
	EXPECT_NE(FaderText(1), "-6");
	Save();
	EXPECT_TRUE((*SavedGroup("SFX"))["solo"].AsBool());
}

TEST_F(AudioMixerWindowFixture, ChainShowsGroupUnitsAddsEffect) {
	OpenOn(StageMixer());
	const UI::ElementHandle chain = Chain();
	ASSERT_TRUE(chain.IsValid());
	EXPECT_EQ(UITest::CountByClass(*_context, chain, MIXER_UNIT), 0u);
	ASSERT_TRUE(Click("SFX"));
	EXPECT_EQ(UITest::CountByClass(*_context, chain, MIXER_UNIT), 2u);
	for (const char* text : {"Pitch", "Send", "Receive", "Reverb", "Send level", "Attenuation", "Volume"}) {
		EXPECT_TRUE(UITest::FindByText(*_context, chain, text).IsValid()) << text;
	}

	ASSERT_TRUE(ClickIn(chain, "Add Effect"));
	ASSERT_TRUE(ClickMenu("Echo"));
	EXPECT_EQ(UITest::CountByClass(*_context, chain, MIXER_UNIT), 3u);
	EXPECT_TRUE(UITest::FindByText(*_context, chain, "Delay").IsValid());
	EXPECT_TRUE(Unsaved());
	Save();
	const JSONValue& units = (*SavedGroup("SFX"))["units"];
	ASSERT_EQ(units.Size(), 3u);
	EXPECT_EQ(units[2]["kind"].AsString(), "Effect");
	EXPECT_EQ(units[2]["effect"].AsString(), "Echo");

	ASSERT_TRUE(Click("Reverb"));
	EXPECT_TRUE(UITest::FindByText(*_context, chain, "Reverb").IsValid());
	ASSERT_TRUE(Click("Music"));
	EXPECT_TRUE(UITest::FindByText(*_context, chain, "Music").IsValid());
	EXPECT_FALSE(UITest::FindByText(*_context, chain, "Reverb").IsValid());
}

TEST_F(AudioMixerWindowFixture, MenuSendFeedsPickedTarget) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("SFX"));
	const UI::ElementHandle chain = Chain();
	ASSERT_TRUE(ClickIn(chain, "Reverb"));
	ASSERT_TRUE(ClickMenu("'Music'"));
	Save();
	EXPECT_EQ((*SavedGroup("SFX"))["units"][0]["target"].AsString(), "Music");

	ASSERT_TRUE(ClickIn(chain, "Music"));
	ASSERT_TRUE(ClickMenu("None"));
	Save();
	EXPECT_EQ((*SavedGroup("SFX"))["units"][0]["target"].AsString(), "");
	EXPECT_FALSE(UITest::FindByText(*_context, chain, "Send level").IsValid());
}

TEST_F(AudioMixerWindowFixture, StripSlotAddsAndRemovesEffect) {
	OpenOn(StageMixer());
	const UI::ElementHandle add = UITest::FindByClass(*_context, Strip(1), MIXER_ADD, 0);
	ASSERT_TRUE(add.IsValid());
	UITest::ClickElement(*_context, add);
	Tick();
	ASSERT_TRUE(ClickMenu("Echo"));
	EXPECT_EQ(UITest::CountByClass(*_context, Strip(1), MIXER_SLOT), 2u);

	UITest::RightClickElement(*_context, UITest::FindByClass(*_context, Strip(1), MIXER_SLOT, 1));
	Tick();
	ASSERT_TRUE(ClickMenu("Remove"));
	EXPECT_EQ(UITest::CountByClass(*_context, Strip(1), MIXER_SLOT), 1u);
	Save();
	EXPECT_EQ((*SavedGroup("Music"))["units"].Size(), 1u);
}

TEST_F(AudioMixerWindowFixture, ChainSliderWritesSnapshot) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("Music"));
	const UI::ElementHandle chain = Chain();
	const UI::ElementHandle pitch = UITest::FindByClass(*_context, chain, SLIDER_TRACK, 0);
	ASSERT_TRUE(pitch.IsValid());
	UITest::ClickElement(*_context, pitch);
	Tick();
	Save();
	const f64 written = SavedValue("Music", "Pitch");
	EXPECT_GT(written, 2.0);
	const auto knobTakesThePointer = [&]() {
		const UI::ElementHandle slider = UITest::FindByClass(*_context, chain, StringHash("slt-slider"), 0);
		const UI::ElementHandle knob = UITest::FindByClass(*_context, chain, SLIDER_KNOB, 0);
		const UI::Rect box = _context->GetLayout(knob).borderBox;
		for (UI::ElementHandle at = _context->HitTest(Vec2(box.x + box.w * 0.5f, box.y + box.h * 0.5f)); at.IsValid();) {
			if (at == slider) {
				return true;
			}
			const UI::Element* element = _context->GetElement(at);
			at = element && element->parent != UI::INVALID_UI_INDEX ? _context->MakeHandle(element->parent) : UI::ElementHandle{};
		}
		return false;
	};
	EXPECT_TRUE(knobTakesThePointer());

	SetPlayMode(PlayModeState::Playing);
	Tick();
	EXPECT_FALSE(knobTakesThePointer());
	UITest::DragElementBy(*_context, UITest::FindByClass(*_context, chain, SLIDER_KNOB, 0), -300, 0);
	Tick();
	SetPlayMode(PlayModeState::Editing);
	Tick();
	Save();
	EXPECT_EQ(SavedValue("Music", "Pitch"), written);
}

TEST_F(AudioMixerWindowFixture, UnitDropOnLowerHalfGoesBelow) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("SFX"));
	const UI::ElementHandle below = UITest::FindByClass(*_context, Chain(), MIXER_UNIT, 1);
	ASSERT_TRUE(below.IsValid());
	UI::DragPayload payload;
	payload.type = StringHash("AudioMixerUnit");
	payload.data = Variant(u32{0});
	const UI::Rect box = _context->GetLayout(below).borderBox;
	EXPECT_TRUE(_context->DeliverDrop(payload, Vec2(box.x + box.w * 0.5f, box.y + box.h * 0.75f)));
	Tick();
	Save();
	const JSONValue& units = (*SavedGroup("SFX"))["units"];
	ASSERT_EQ(units.Size(), 2u);
	EXPECT_EQ(units[0]["kind"].AsString(), "Attenuation");
	EXPECT_EQ(units[1]["kind"].AsString(), "Send");
}

TEST_F(AudioMixerWindowFixture, SelectedSnapshotIsEdited) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("Combat"));
	EXPECT_EQ(FaderText(1), "0");
	UITest::DragElementBy(*_context, Fader(1), 0, 40);
	Tick();
	const str edited = FaderText(1);
	Save();
	EXPECT_LT(SavedVolume("Music", 1), 0.0);
	EXPECT_EQ(SavedVolume("Music", 0), -6.0);
	SetPlayMode(PlayModeState::Playing);
	Tick();
	EXPECT_EQ(HeardInPlay(), edited);
	ASSERT_TRUE(Click("Explore"));
	EXPECT_EQ(HeardInPlay(), edited);
	SetPlayMode(PlayModeState::Editing);
	Tick();
	SetPlayMode(PlayModeState::Playing);
	Tick();
	EXPECT_EQ(HeardInPlay(), edited);
	SetPlayMode(PlayModeState::Editing);
	Tick();

	ASSERT_TRUE(Click("Explore"));
	SetPlayMode(PlayModeState::Playing);
	Tick();
	EXPECT_EQ(HeardInPlay(), "-6");
	SetPlayMode(PlayModeState::Editing);
}

TEST_F(AudioMixerWindowFixture, NewGroupHeardAtSelectedSnapshot) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("Combat"));
	ASSERT_TRUE(Click("Music"));
	ASSERT_TRUE(_window.Get()->OnCommand(PanelCommand::Duplicate));
	Tick();
	ASSERT_EQ(StripTitles(), "Master Music SFX Reverb Music - Copy");
	SetPlayMode(PlayModeState::Playing);
	Tick();
	EXPECT_EQ(FaderText(4), "0");
	SetPlayMode(PlayModeState::Editing);
	Tick();
}

TEST_F(AudioMixerWindowFixture, PlusCopiesSnapshotMenuEdits) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("Combat"));
	UITest::ClickElement(*_context, UITest::FindByClass(*_context, _windowRoot, MIXER_SNAPSHOT_ADD, 0));
	Tick();
	Type("Paused");
	Save();
	EXPECT_EQ(SavedSnapshots(), "Explore Combat Paused");
	EXPECT_EQ(FaderText(1), "0");

	UITest::RightClickElement(*_context, UITest::FindByText(*_context, _windowRoot, "Combat"));
	Tick();
	ASSERT_TRUE(ClickMenu("Set as start Snapshot"));
	Save();
	EXPECT_EQ(_saved["startSnapshot"].AsString(), "Combat");

	for (const char* name : {"Explore", "Paused"}) {
		UITest::RightClickElement(*_context, UITest::FindByText(*_context, _windowRoot, name));
		Tick();
		ASSERT_TRUE(ClickMenu("Delete")) << name;
	}
	UITest::RightClickElement(*_context, UITest::FindByText(*_context, _windowRoot, "Combat"));
	Tick();
	ASSERT_TRUE(ClickMenu("Delete"));
	Save();
	EXPECT_EQ(SavedSnapshots(), "Combat");
}

TEST_F(AudioMixerWindowFixture, WindowsEditOneTargetSnapshot) {
	OpenOn(StageMixer());
	Handle<EditorPanel> second = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("AudioMixerWindow")));
	ASSERT_TRUE(second.IsValid());
	UI::ElementHandle secondRoot = _context->CreateElement();
	_context->AppendChild(_context->GetRoot(), secondRoot);
	second.Get()->BuildContent(*_context, secondRoot);
	const auto tickBoth = [&]() {
		for (u32 pass = 0; pass < 2; ++pass) {
			_window.Get()->OnTick(1.0f / 60.0f);
			second.Get()->OnTick(1.0f / 60.0f);
			_context->Update();
		}
	};
	tickBoth();
	const auto musicFader = [&](UI::ElementHandle root) {
		const UI::ElementHandle fader = UITest::FindByClass(*_context, UITest::FindByClass(*_context, root, MIXER_STRIP, 1), MIXER_FADER, 0);
		const UI::Element* element = _context->GetElement(fader);
		return element ? _context->GetTextString(_context->MakeHandle(element->firstChild)) : str("<no fader>");
	};
	ASSERT_EQ(musicFader(secondRoot), "-6");
	UITest::ClickElement(*_context, UITest::FindByText(*_context, _windowRoot, "Combat"));
	tickBoth();
	EXPECT_EQ(musicFader(secondRoot), "0");
	PanelManager::ClosePanel(second);
}

TEST_F(AudioMixerWindowFixture, PlayModeEditSettlesOnGame) {
	OpenOn(StageMixer());
	EXPECT_FALSE(EditInPlayShown());
	SetPlayMode(PlayModeState::Playing);
	Tick();
	ASSERT_TRUE(EditInPlayShown());
	UITest::DragElementBy(*_context, Fader(1), 0, 40);
	Tick();
	EXPECT_EQ(FaderText(1), "-6");

	const UI::Brush off = _context->GetStyle(EditInPlay()).background;
	ClickEditInPlay();
	EXPECT_FALSE(_context->GetStyle(EditInPlay()).background == off);
	UITest::DragElementBy(*_context, Fader(1), 0, 40);
	Tick();
	const str edited = FaderText(1);
	EXPECT_LT(std::atoi(edited.c_str()), -6);
	ASSERT_TRUE(Click("Combat"));
	EXPECT_EQ(FaderText(1), "0");
	ClickEditInPlay();
	EXPECT_TRUE(_context->GetStyle(EditInPlay()).background == off);
	EXPECT_EQ(FaderText(1), edited);

	SetPlayMode(PlayModeState::Editing);
	Tick();
	EXPECT_FALSE(EditInPlayShown());
	EXPECT_EQ(FaderText(1), "0");
	Save();
	EXPECT_NEAR(SavedVolume("Music", 0), std::atof(edited.c_str()), 0.5);
}

TEST_F(AudioMixerWindowFixture, RightClickExposesNameToolbarEdits) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("Music"));
	EXPECT_TRUE(UITest::FindByText(*_context, _windowRoot, "Exposed Parameters (0)").IsValid());
	RightClickName("Volume");
	ASSERT_TRUE(ClickMenu("Expose 'Volume (of Music)' to script"));
	EXPECT_TRUE(UITest::FindByText(*_context, Chain(), EXPOSED_VOLUME).IsValid());
	EXPECT_TRUE(UITest::FindByText(*_context, _windowRoot, "Exposed Parameters (1)").IsValid());
	Save();
	ASSERT_EQ(_saved["exposedParameters"].Size(), 1u);
	EXPECT_EQ(_saved["exposedParameters"][0]["name"].AsString(), "MyExposedParam");
	EXPECT_EQ(_saved["exposedParameters"][0]["group"].AsString(), "Music");

	UITest::ClickElement(*_context, UITest::FindByClass(*_context, _windowRoot, MIXER_EXPOSED, 0));
	Tick();
	const UI::ElementHandle row = UITest::FindByClass(*_context, _context->GetRoot(), MIXER_EXPOSED_ROW, 0);
	ASSERT_TRUE(row.IsValid());
	EXPECT_TRUE(UITest::FindByText(*_context, row, "Volume (of Music)").IsValid());
	UITest::RightClickElement(*_context, row);
	Tick();
	ASSERT_TRUE(ClickMenu("Rename"));
	Type("MusicVolume");
	EXPECT_TRUE(UITest::FindByText(*_context, UITest::FindByClass(*_context, _context->GetRoot(), MIXER_EXPOSED_ROW, 0), "MusicVolume").IsValid());
	CloseTopPopup();
	ASSERT_EQ(_context->GetOpenPopupCount(), 0u);
	Save();
	EXPECT_EQ(_saved["exposedParameters"][0]["name"].AsString(), "MusicVolume");

	UITest::ClickElement(*_context, UITest::FindByClass(*_context, _windowRoot, MIXER_EXPOSED, 0));
	Tick();
	const UI::ElementHandle again = UITest::FindByClass(*_context, _context->GetRoot(), MIXER_EXPOSED_ROW, 0);
	ASSERT_TRUE(again.IsValid());
	EXPECT_TRUE(UITest::FindByText(*_context, again, "MusicVolume").IsValid());
	UITest::RightClickElement(*_context, again);
	Tick();
	ASSERT_TRUE(ClickMenu("Unexpose"));
	EXPECT_FALSE(UITest::FindByClass(*_context, _context->GetRoot(), MIXER_EXPOSED_ROW, 0).IsValid());
	CloseTopPopup();
	Save();
	EXPECT_EQ(_saved["exposedParameters"].Size(), 0u);
	EXPECT_TRUE(UITest::FindByText(*_context, Chain(), "Volume").IsValid());
}

TEST_F(AudioMixerWindowFixture, TransitionTypeIsPerSnapshot) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("Combat"));
	ASSERT_TRUE(Click("Music"));
	RightClickName("Volume");
	EXPECT_TRUE(Ticked("Linear Snapshot Transition"));
	ASSERT_TRUE(ClickMenu("Smoothstep Snapshot Transition"));
	Save();
	const JSONValue& combat = _saved["snapshots"][1]["transitions"];
	ASSERT_EQ(combat.Size(), 1u);
	EXPECT_EQ(combat[0]["group"].AsString(), "Music");
	EXPECT_EQ(combat[0]["type"].AsString(), "Smoothstep");
	EXPECT_FALSE(_saved["snapshots"][0]["transitions"].IsArray());

	RightClickName("Volume");
	EXPECT_TRUE(Ticked("Smoothstep Snapshot Transition"));
	EXPECT_FALSE(Ticked("Linear Snapshot Transition"));
	ASSERT_TRUE(ClickMenu("Linear Snapshot Transition"));
	Save();
	EXPECT_FALSE(_saved["snapshots"][1]["transitions"].IsArray());
}

TEST_F(AudioMixerWindowFixture, EyeHidesStripPerView) {
	OpenOn(StageMixer());
	ASSERT_EQ(StripTitles(), "Master Music SFX Reverb");
	EXPECT_TRUE(UITest::FindByText(*_context, _windowRoot, "View").IsValid());
	EXPECT_FALSE(Unsaved());
	ASSERT_TRUE(Eye(1).IsValid());

	UITest::ClickElement(*_context, Eye(1));
	Tick();
	EXPECT_EQ(StripTitles(), "Master SFX Reverb");
	EXPECT_TRUE(Unsaved());

	const UI::ElementHandle add = UITest::FindByClass(*_context, _windowRoot, MIXER_VIEW_ADD, 0);
	ASSERT_TRUE(add.IsValid());
	UITest::ClickElement(*_context, add);
	Tick();
	Type("Mix");
	EXPECT_EQ(StripTitles(), "Master SFX Reverb");
	UITest::ClickElement(*_context, Eye(1));
	Tick();
	UITest::ClickElement(*_context, Eye(3));
	Tick();
	EXPECT_EQ(StripTitles(), "Master Music SFX");
	ASSERT_TRUE(Click("SFX"));
	ASSERT_TRUE(_window.Get()->OnCommand(PanelCommand::Duplicate));
	Tick();
	EXPECT_EQ(StripTitles(), "Master Music SFX SFX - Copy");

	ASSERT_TRUE(Click("View"));
	EXPECT_EQ(StripTitles(), "Master SFX Reverb");
	ASSERT_TRUE(Click("SFX"));
	UITest::ClickElement(*_context, UITest::FindByClass(*_context, _windowRoot, MIXER_GROUP_ADD, 0));
	Tick();
	Type("Steps");
	EXPECT_EQ(StripTitles(), "Master SFX Steps Reverb");
	Save();
	EXPECT_EQ(SavedViews(), "View: Master SFX Reverb New Group; Mix: Master Music SFX SFX - Copy") << _savedText;
}

TEST_F(AudioMixerWindowFixture, PickedColorTagsGroup) {
	OpenOn(StageMixer());
	const auto tagged = [&](u32 strip) { return UITest::FindByClass(*_context, Strip(strip), MIXER_COLOR_TAG, 0).IsValid(); };
	EXPECT_FALSE(tagged(1));

	UITest::RightClickElement(*_context, Eye(1));
	Tick();
	EXPECT_TRUE(Ticked("No Color"));
	ASSERT_TRUE(ClickMenu("Red"));
	EXPECT_TRUE(tagged(1));
	UITest::RightClickElement(*_context, Eye(1));
	Tick();
	EXPECT_TRUE(Ticked("Red"));
	CloseTopPopup();

	UITest::RightClickElement(*_context, Strip(2));
	Tick();
	ASSERT_TRUE(ClickMenu("Blue"));
	EXPECT_TRUE(tagged(2));
	Save();
	ASSERT_NE(SavedGroup("SFX"), nullptr);
	EXPECT_EQ((*SavedGroup("Music"))["color"].AsString(), "Red") << _savedText;
	EXPECT_EQ((*SavedGroup("SFX"))["color"].AsString(), "Blue");
	EXPECT_FALSE((*SavedGroup("Master"))["color"].IsString());
}

TEST_F(AudioMixerWindowFixture, StripMenuAddsEffectToSelected) {
	OpenOn(StageMixer());
	ASSERT_TRUE(Click("Music"));
	CtrlClick(UITest::NthChildOf(*_context, Strip(3), 0));
	UITest::RightClickElement(*_context, Strip(1));
	Tick();
	ASSERT_TRUE(ClickMenu("Add effect at top"));
	ASSERT_TRUE(ClickTopMenu("Receive"));
	UITest::RightClickElement(*_context, Strip(2));
	Tick();
	ASSERT_TRUE(ClickMenu("Add effect at bottom"));
	ASSERT_TRUE(ClickTopMenu("Receive"));
	Save();
	const auto kinds = [&](const char* group) {
		str out;
		const JSONValue& units = (*SavedGroup(group))["units"];
		for (size_t index = 0; units.IsArray() && index < units.Size(); ++index) {
			out += (out.empty() ? "" : " ") + str(units[index]["kind"].AsString());
		}
		return out;
	};
	EXPECT_EQ(kinds("Music"), "Receive Attenuation");
	EXPECT_EQ(kinds("Reverb"), "Receive Attenuation");
	EXPECT_EQ(kinds("SFX"), "Send Attenuation Receive");
}

TEST_F(AudioMixerWindowFixture, MixersPanelNestsAndShows) {
	const Guid game = StageMixer();
	ASSERT_TRUE(StageSubMixer(game).IsValid());
	OpenOn(game);
	EXPECT_EQ(MixerRows(), "Game (Audio Listener); Sub (Music of Game)");
	ASSERT_TRUE(Click("Sub"));
	Tick();
	EXPECT_TRUE(UITest::FindByTextContaining(*_context, _windowRoot, "Sub.mixer").IsValid());
	EXPECT_EQ(StripTitles(), "Master");
}

TEST_F(AudioMixerWindowFixture, DroppedMixerRoutesIntoGroup) {
	const Guid game = StageMixer();
	const Guid sub = StageSubMixer(game);
	OpenOn(game);
	EXPECT_FALSE(DropMixer(_mixer, "Sub"));
	EXPECT_TRUE(DropMixer(sub, nullptr));
	EXPECT_EQ(MixerRows(), "Game (Audio Listener); Sub (Audio Listener)");
	EXPECT_TRUE(UITest::FindByTextContaining(*_context, _windowRoot, "Sub.mixer *").IsValid());
	ASSERT_TRUE(DropMixer(sub, "Game"));
	EXPECT_TRUE(ClickTopMenu("Reverb"));
	EXPECT_EQ(MixerRows(), "Game (Audio Listener); Sub (Reverb of Game)");
	ASSERT_TRUE(Click("Save"));
	const str saved = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/Sub.mixer")), MemTag::Editor);
	EXPECT_NE(saved.find(_mixer.ToString()), str::npos) << saved;
	EXPECT_NE(saved.find("\"Reverb\""), str::npos);

	SetPlayMode(PlayModeState::Playing);
	Tick();
	EXPECT_FALSE(DropMixer(sub, nullptr));
	SetPlayMode(PlayModeState::Editing);
}

TEST_F(AudioMixerWindowFixture, PlusMakesMixerMenuEdits) {
	OpenOn(StageMixer());
	const UI::ElementHandle add = UITest::FindByClass(*_context, _windowRoot, MIXER_MIXER_ADD, 0);
	ASSERT_TRUE(add.IsValid());
	UITest::ClickElement(*_context, add);
	Tick();
	Type("Ambience");
	ASSERT_TRUE(FileSystem::Exists(FileSystem::Resolve(Path("project://Assets/Ambience.mixer"))));
	EXPECT_TRUE(UITest::FindByTextContaining(*_context, _windowRoot, "Ambience.mixer").IsValid());

	const UI::ElementHandle ambience = UITest::FindByText(*_context, _windowRoot, "Ambience");
	ASSERT_TRUE(ambience.IsValid());
	UITest::RightClickElement(*_context, ambience);
	Tick();
	ASSERT_TRUE(ClickMenu("Rename"));
	Type("Ambient");
	ASSERT_TRUE(FileSystem::Exists(FileSystem::Resolve(Path("project://Assets/Ambient.mixer"))));
	EXPECT_FALSE(FileSystem::Exists(FileSystem::Resolve(Path("project://Assets/Ambience.mixer"))));

	const UI::ElementHandle ambient = UITest::FindByText(*_context, _windowRoot, "Ambient");
	ASSERT_TRUE(ambient.IsValid());
	UITest::RightClickElement(*_context, ambient);
	Tick();
	ASSERT_TRUE(ClickMenu("Delete AudioMixer"));
	EXPECT_TRUE(FileSystem::Exists(FileSystem::Resolve(Path("project://Assets/Ambient.mixer"))));
	ASSERT_TRUE(UITest::FindByTextContaining(*_context, _context->GetRoot(), "Delete 'Ambient.mixer'?").IsValid());
	ASSERT_TRUE(ClickTopMenu("Delete"));
	EXPECT_FALSE(FileSystem::Exists(FileSystem::Resolve(Path("project://Assets/Ambient.mixer"))));
	EXPECT_EQ(MixerRows(), "Game (Audio Listener)");
}

#endif
