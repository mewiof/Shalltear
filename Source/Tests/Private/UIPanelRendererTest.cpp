#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/Window.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	std::filesystem::path FindRepoDir(const char* relative) {
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

	constexpr const char* HUD_DOCUMENT = R"(<ui version="1">
	<panel id="hud" style="width: 100%; height: 100%;">
		<label id="score">Score 0</label>
	</panel>
</ui>)";

	constexpr const char* MENU_DOCUMENT = R"(<ui version="1">
	<panel id="menu" style="width: 100%; height: 100%;">
		<label id="title">Paused</label>
	</panel>
</ui>)";

	class UIPanelRendererFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mountedEngine = false;
		Vector<Asset*> _handMade{GetMemoryResource(MemTag::Debug)};

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_panelrenderer_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::error_code ec;
			std::filesystem::create_directories(_root / "Assets" / "UI", ec);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			SceneSystem::Initialize();
			UIScreenDesc screen;
			screen.target = UIScreenDesc::Target::Texture;
			screen.sizePx = {640, 360};
			UISystem::SetScreen(screen);
		}

		void TearDown() override {
			SceneSystem::Terminate();
			UISystem::Terminate();
			for (Asset* asset : _handMade) {
				Delete(asset);
			}
			_handMade.clear();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Handle<UIDocumentAsset> Parse(const char* text, const char* virtualPath) {
			UIDocumentAsset* document = New<UIDocumentAsset>(MemTag::Asset);
			str error;
			EXPECT_TRUE(document->Parse(text, error)) << error;
			document->virtualPath = virtualPath;
			_handMade.push_back(document);
			return Handle<UIDocumentAsset>(document);
		}

		Handle<UIPanelSettings> MakeSettings() {
			UIPanelSettings* settings = New<UIPanelSettings>(MemTag::Asset);
			_handMade.push_back(settings);
			return Handle<UIPanelSettings>(settings);
		}

		static Vector<UISurfaceId> ScreenOrder() {
			Vector<UISurfaceId> order{GetMemoryResource(MemTag::Debug)};
			UISystem::ForEachScreenSurface([&order](UISurfaceId id, UI::Context&) { order.push_back(id); });
			return order;
		}

		static UISurfaceId SurfaceOf(const UIPanelRenderer& renderer) {
			UISurfaceId found = INVALID_UI_SURFACE;
			UISystem::ForEachScreenSurface([&found, &renderer](UISurfaceId id, UI::Context& context) {
				if (&context == renderer.GetContext()) {
					found = id;
				}
			});
			return found;
		}

		static void RunUIUpdate() {
			FrameStageArgs args;
			FrameStages::Run(FrameStage::UIUpdate, args);
		}
	};

} // anonymous namespace

TEST_F(UIPanelRendererFixture, EnableBuildsNamedSurface) {
	Handle<Scene> scene = SceneSystem::CreateScene("Game");
	Handle<SceneObject> object = SceneSystem::CreateObject("HUD", scene);
	Handle<UIPanelRenderer> renderer = object->AddComponent<UIPanelRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	ASSERT_NE(renderer->GetContext(), nullptr);
	EXPECT_EQ(renderer->GetContext()->GetLiveCount(), 0u);
	EXPECT_EQ(UISystem::GetSurfaceName(SurfaceOf(*renderer)), "UIPanelRenderer");

	Handle<UIDocumentAsset> hud = Parse(HUD_DOCUMENT, "project://Assets/UI/Hud.slxml");
	renderer->SetDocument(hud);
	UI::Context* context = renderer->GetContext();
	ASSERT_NE(context, nullptr);
	EXPECT_EQ(UISystem::GetSurfaceName(SurfaceOf(*renderer)), "project://Assets/UI/Hud.slxml");

	UI::ElementHandle panel = renderer->FindElement("hud");
	UI::ElementHandle score = renderer->FindElement("score");
	ASSERT_TRUE(context->IsAlive(panel));
	ASSERT_TRUE(context->IsAlive(score));
	EXPECT_EQ(context->GetTextString(score), "Score 0");
	EXPECT_FALSE(renderer->FindElement("absent").IsValid());

	RunUIUpdate();
	const UI::Rect box = context->GetLayout(panel).borderBox;
	EXPECT_FLOAT_EQ(box.w, 640.0f);
	EXPECT_FLOAT_EQ(box.h, 360.0f);
	EXPECT_EQ(ScreenOrder().size(), 1u);
}

TEST_F(UIPanelRendererFixture, DisablingDestroysSurfaceAndEnablingRebuildsIt) {
	Handle<Scene> scene = SceneSystem::CreateScene("Game");
	Handle<SceneObject> object = SceneSystem::CreateObject("HUD", scene);
	Handle<UIPanelRenderer> renderer = object->AddComponent<UIPanelRenderer>();
	renderer->SetDocument(Parse(HUD_DOCUMENT, "project://Assets/UI/Hud.slxml"));
	UI::Context* context = renderer->GetContext();
	ASSERT_NE(context, nullptr);
	ASSERT_TRUE(UISystem::IsContextLive(context));

	renderer->SetEnabled(false);
	EXPECT_EQ(renderer->GetContext(), nullptr);
	EXPECT_FALSE(UISystem::IsContextLive(context));
	EXPECT_TRUE(ScreenOrder().empty());
	EXPECT_FALSE(renderer->FindElement("hud").IsValid());

	renderer->SetEnabled(true);
	ASSERT_NE(renderer->GetContext(), nullptr);
	EXPECT_TRUE(renderer->GetContext()->IsAlive(renderer->FindElement("hud")));
	EXPECT_EQ(ScreenOrder().size(), 1u);

	object->SetActive(false);
	EXPECT_EQ(renderer->GetContext(), nullptr);
	EXPECT_TRUE(ScreenOrder().empty());
}

TEST_F(UIPanelRendererFixture, RenderersSortAsSurfaces) {
	Handle<Scene> scene = SceneSystem::CreateScene("Game");
	Handle<UIPanelRenderer> hud = SceneSystem::CreateObject("HUD", scene)->AddComponent<UIPanelRenderer>();
	Handle<UIPanelRenderer> menu = SceneSystem::CreateObject("Menu", scene)->AddComponent<UIPanelRenderer>();
	hud->SetDocument(Parse(HUD_DOCUMENT, "project://Assets/UI/Hud.slxml"));
	menu->SetDocument(Parse(MENU_DOCUMENT, "project://Assets/UI/Menu.slxml"));
	hud->SetSortOrder(1.0f);
	menu->SetSortOrder(0.0f);
	{
		Vector<UISurfaceId> order = ScreenOrder();
		ASSERT_EQ(order.size(), 2u);
		EXPECT_EQ(order[0], SurfaceOf(*menu));
		EXPECT_EQ(order[1], SurfaceOf(*hud));
	}
	EXPECT_TRUE(hud->FindElement("hud").IsValid());
	EXPECT_FALSE(hud->FindElement("menu").IsValid());
	EXPECT_TRUE(menu->FindElement("menu").IsValid());

	Handle<UIPanelSettings> overlay = MakeSettings();
	overlay->sortOrder = 10.0f;
	menu->SetPanelSettings(overlay);
	{
		Vector<UISurfaceId> order = ScreenOrder();
		EXPECT_EQ(order[0], SurfaceOf(*hud));
		EXPECT_EQ(order[1], SurfaceOf(*menu));
	}
	RunUIUpdate();
	const f32 before = menu->GetContext()->GetLayout(menu->FindElement("title")).borderBox.w;
	overlay->fontSizePx = 28.0f;
	menu->SetPanelSettings(overlay);
	RunUIUpdate();
	const f32 after = menu->GetContext()->GetLayout(menu->FindElement("title")).borderBox.w;
	if (_mountedEngine) {
		EXPECT_GT(after, before);
	}
}

TEST_F(UIPanelRendererFixture, NamedWriteReachesSurface) {
	Handle<Scene> scene = SceneSystem::CreateScene("Game");
	Handle<UIPanelRenderer> hud = SceneSystem::CreateObject("HUD", scene)->AddComponent<UIPanelRenderer>();
	Handle<UIPanelRenderer> menu = SceneSystem::CreateObject("Menu", scene)->AddComponent<UIPanelRenderer>();
	Handle<UIDocumentAsset> hudDocument = Parse(HUD_DOCUMENT, "project://Assets/UI/Hud.slxml");
	Handle<UIDocumentAsset> menuDocument = Parse(MENU_DOCUMENT, "project://Assets/UI/Menu.slxml");
	hud->SetDocument(hudDocument);
	menu->SetDocument(menuDocument);
	RunUIUpdate();
	ASSERT_EQ(ScreenOrder()[0], SurfaceOf(*hud));

	const TypeInfo* type = UIPanelRenderer::GetReflectType();
	const FieldInfo* sortField = FindField(type, StringHash("_sortOrder"));
	const FieldInfo* documentField = FindField(type, StringHash("_document"));
	const FieldInfo* settingsField = FindField(type, StringHash("_panelSettings"));
	ASSERT_NE(sortField, nullptr);
	ASSERT_NE(documentField, nullptr);
	ASSERT_NE(settingsField, nullptr);

	*static_cast<f32*>(sortField->addrFn(hud.Get())) = 5.0f;
	EXPECT_EQ(ScreenOrder()[0], SurfaceOf(*hud));
	RunUIUpdate();
	EXPECT_EQ(ScreenOrder()[0], SurfaceOf(*menu));
	EXPECT_FLOAT_EQ(hud->GetSortOrder(), 5.0f);

	UI::Context* context = hud->GetContext();
	*static_cast<u64*>(documentField->addrFn(hud.Get())) = menuDocument.GetInstanceId();
	EXPECT_TRUE(hud->FindElement("hud").IsValid());
	RunUIUpdate();
	EXPECT_EQ(hud->GetContext(), context);
	EXPECT_FALSE(hud->FindElement("hud").IsValid());
	EXPECT_TRUE(hud->FindElement("menu").IsValid());
	EXPECT_EQ(UISystem::GetSurfaceName(SurfaceOf(*hud)), "project://Assets/UI/Menu.slxml");

	Handle<UIPanelSettings> pixel = MakeSettings();
	pixel->scaleMode = UIPanelScaleMode::ConstantPixelSize;
	pixel->scale = 2.0f;
	*static_cast<u64*>(settingsField->addrFn(hud.Get())) = pixel.GetInstanceId();
	RunUIUpdate();
	EXPECT_FLOAT_EQ(hud->GetContext()->GetContentScale(), 2.0f);

	UI::ElementHandle title = hud->FindElement("menu");
	RunUIUpdate();
	EXPECT_TRUE(hud->FindElement("menu") == title);
}

TEST_F(UIPanelRendererFixture, LastRendererRemovesPass) {
	Handle<Scene> scene = SceneSystem::CreateScene("Game");
	Handle<UIPanelRenderer> hud = SceneSystem::CreateObject("HUD", scene)->AddComponent<UIPanelRenderer>();
	hud->SetDocument(Parse(HUD_DOCUMENT, "project://Assets/UI/Hud.slxml"));
	hud->SetEnabled(false);
	RunUIUpdate();
	EXPECT_EQ(hud->GetContext(), nullptr);
	hud->SetEnabled(true);
	const TypeInfo* type = UIPanelRenderer::GetReflectType();
	*static_cast<f32*>(FindField(type, StringHash("_sortOrder"))->addrFn(hud.Get())) = 3.0f;
	RunUIUpdate();
	EXPECT_FLOAT_EQ(hud->GetSortOrder(), 3.0f);
	Handle<UIPanelRenderer> second = SceneSystem::CreateObject("Menu", scene)->AddComponent<UIPanelRenderer>();
	second->SetSortOrder(0.0f);
	RunUIUpdate();
	ASSERT_EQ(ScreenOrder().size(), 2u);
	EXPECT_EQ(ScreenOrder()[1], SurfaceOf(*hud));
}

TEST_F(UIPanelRendererFixture, SnapshotReloadRebuildsSurface) {
	ASSERT_TRUE(FileSystem::WriteTextFile(Path("project://Assets/UI/Hud.slxml"), str(HUD_DOCUMENT)));
	AssetDatabase::ScanProjectDirectory();
	Asset* loaded = AssetDatabase::LoadAsset(Path("project://Assets/UI/Hud.slxml"));
	ASSERT_NE(loaded, nullptr);

	Handle<Scene> scene = SceneSystem::CreateScene("Game");
	Handle<UIPanelRenderer> renderer = SceneSystem::CreateObject("HUD", scene)->AddComponent<UIPanelRenderer>();
	renderer->SetDocument(Handle<UIDocumentAsset>(static_cast<UIDocumentAsset*>(loaded)));
	renderer->SetSortOrder(7.0f);
	ASSERT_EQ(ScreenOrder().size(), 1u);

	Buffer snapshot;
	ASSERT_TRUE(SceneSystem::SaveSceneToMemory(scene, snapshot));
	AssetDatabase::UnloadAsset(loaded);

	SceneSystem::UnloadAllScenes();
	SceneSystem::EndFrame();
	EXPECT_TRUE(ScreenOrder().empty());

	Handle<Scene> restored = SceneSystem::LoadSceneFromMemory(snapshot);
	ASSERT_TRUE(restored.IsValid());
	ASSERT_EQ(ScreenOrder().size(), 1u);
	Handle<SceneObject> object = restored->FindObjectByName("HUD");
	ASSERT_TRUE(object.IsValid());
	Handle<UIPanelRenderer> back = object->GetComponent<UIPanelRenderer>();
	ASSERT_TRUE(back.IsValid());
	EXPECT_FLOAT_EQ(back->GetSortOrder(), 7.0f);
	ASSERT_NE(back->GetContext(), nullptr);
	EXPECT_TRUE(back->GetContext()->IsAlive(back->FindElement("score")));
	EXPECT_EQ(UISystem::GetSurfaceName(SurfaceOf(*back)), "project://Assets/UI/Hud.slxml");
}

namespace {

	class UIPanelRendererReloadFixture : public UIPanelRendererFixture {
	protected:
		void Write(const char* relative, const char* text) {
			const std::filesystem::path path = _root / "Assets" / "UI" / relative;
			ASSERT_TRUE(FileSystem::WriteTextFile(Path(path.generic_string()), text));
		}

		static Path UIPath(const char* relative) { return Path(str("project://Assets/UI/") + relative); }

		Handle<UIDocumentAsset> Load(const char* relative) {
			Asset* asset = AssetDatabase::LoadAsset(UIPath(relative));
			EXPECT_NE(asset, nullptr) << relative;
			_loaded.push_back(asset);
			return Handle<UIDocumentAsset>(static_cast<UIDocumentAsset*>(asset));
		}

		Handle<UIPanelRenderer> Show(Handle<UIDocumentAsset> document) {
			Handle<Scene> scene = SceneSystem::CreateScene("Game");
			Handle<UIPanelRenderer> renderer = SceneSystem::CreateObject("HUD", scene)->AddComponent<UIPanelRenderer>();
			renderer->SetDocument(document);
			return renderer;
		}

		void TearDown() override {
			for (Asset* asset : _loaded) {
				AssetDatabase::UnloadAsset(asset);
			}
			_loaded.clear();
			UIPanelRendererFixture::TearDown();
		}

		Vector<Asset*> _loaded{GetMemoryResource(MemTag::Debug)};
	};

	constexpr const char* HUD_EDITED = R"(<ui version="1">
	<panel id="hud" style="width: 100%; height: 100%;">
		<label id="lives">Lives 3</label>
	</panel>
</ui>)";

} // anonymous namespace

TEST_F(UIPanelRendererReloadFixture, DiskEditRebuildsSurface) {
	Write("Hud.slxml", HUD_DOCUMENT);
	Handle<UIDocumentAsset> hud = Load("Hud.slxml");
	ASSERT_TRUE(hud.IsValid());
	Handle<UIPanelRenderer> renderer = Show(hud);
	ASSERT_TRUE(renderer->GetContext()->IsAlive(renderer->FindElement("score")));
	const u64 instance = hud.GetInstanceId();

	Write("Hud.slxml", HUD_EDITED);
	ASSERT_TRUE(AssetDatabase::ReimportAsset(UIPath("Hud.slxml")));
	ASSERT_EQ(hud.Get(), static_cast<UIDocumentAsset*>(ObjectRegistry::Resolve(instance)));
	EXPECT_EQ(hud->version, 1u);
	EXPECT_FALSE(renderer->FindElement("lives").IsValid());

	RunUIUpdate();
	UI::ElementHandle lives = renderer->FindElement("lives");
	ASSERT_TRUE(renderer->GetContext()->IsAlive(lives));
	EXPECT_EQ(renderer->GetContext()->GetTextString(lives), "Lives 3");
	EXPECT_FALSE(renderer->FindElement("score").IsValid());
}

TEST_F(UIPanelRendererReloadFixture, ARefillThatChangesNothingRebuildsNothing) {
	Write("Hud.slxml", HUD_DOCUMENT);
	Handle<UIDocumentAsset> hud = Load("Hud.slxml");
	Handle<UIPanelRenderer> renderer = Show(hud);
	const UI::ElementHandle score = renderer->FindElement("score");
	ASSERT_TRUE(renderer->GetContext()->IsAlive(score));

	Write("Hud.slxml", HUD_DOCUMENT);
	ASSERT_TRUE(AssetDatabase::ReimportAsset(UIPath("Hud.slxml")));
	EXPECT_EQ(hud->version, 0u);
	RunUIUpdate();
	EXPECT_TRUE(renderer->GetContext()->IsAlive(score));
}

TEST_F(UIPanelRendererReloadFixture, BrokenFileKeepsSurface) {
	Write("Hud.slxml", HUD_DOCUMENT);
	Handle<UIDocumentAsset> hud = Load("Hud.slxml");
	Handle<UIPanelRenderer> renderer = Show(hud);

	Write("Hud.slxml", R"(<ui version="1"><panel id="hud"><label id="broken">)");
	EXPECT_FALSE(AssetDatabase::ReimportAsset(UIPath("Hud.slxml")));
	EXPECT_EQ(hud->version, 0u);
	RunUIUpdate();
	EXPECT_TRUE(renderer->GetContext()->IsAlive(renderer->FindElement("score")));
	EXPECT_FALSE(renderer->FindElement("broken").IsValid());
}

TEST_F(UIPanelRendererReloadFixture, DependencyEditRebuildsDocument) {
	Write("Card.slxml", R"(<ui version="1"><panel id="card"><label id="name">Ada</label></panel></ui>)");
	Write("Hud.slss", ".wide { width: 100px; }");
	Write("Hud.slxml", R"(<ui version="1">
	<sheet src="project://Assets/UI/Hud.slss"/>
	<template src="project://Assets/UI/Card.slxml" name="Card"/>
	<panel id="hud" class="wide"><instance template="Card"/></panel>
</ui>)");
	Handle<UIDocumentAsset> hud = Load("Hud.slxml");
	Handle<UIPanelRenderer> renderer = Show(hud);
	UI::Context* context = renderer->GetContext();
	ASSERT_TRUE(context->IsAlive(renderer->FindElement("name")));
	RunUIUpdate();
	ASSERT_FLOAT_EQ(context->GetLayout(renderer->FindElement("hud")).borderBox.w, 100.0f);

	Write("Card.slxml", R"(<ui version="1"><panel id="card"><label id="title">Lovelace</label></panel></ui>)");
	ASSERT_TRUE(AssetDatabase::ReimportAsset(UIPath("Card.slxml")));
	EXPECT_EQ(hud->version, 0u);
	RunUIUpdate();
	EXPECT_TRUE(context->IsAlive(renderer->FindElement("title")));
	EXPECT_FALSE(renderer->FindElement("name").IsValid());

	Write("Hud.slss", ".wide { width: 200px; }");
	ASSERT_TRUE(AssetDatabase::ReimportAsset(UIPath("Hud.slss")));
	RunUIUpdate();
	RunUIUpdate();
	EXPECT_FLOAT_EQ(context->GetLayout(renderer->FindElement("hud")).borderBox.w, 200.0f);
}

namespace {

	class UIPanelRendererHeadlessFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
		}
		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
		}
	};

} // anonymous namespace

TEST_F(UIPanelRendererHeadlessFixture, HeadlessRendererIsNoOp) {
	Handle<Scene> scene = SceneSystem::CreateScene("Server");
	Handle<UIPanelRenderer> renderer = SceneSystem::CreateObject("HUD", scene)->AddComponent<UIPanelRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	EXPECT_EQ(renderer->GetContext(), nullptr);
	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(HUD_DOCUMENT, error)) << error;
	renderer->SetDocument(Handle<UIDocumentAsset>(&document));
	renderer->SetSortOrder(2.0f);
	EXPECT_EQ(renderer->GetContext(), nullptr);
	EXPECT_FALSE(renderer->FindElement("hud").IsValid());
	FrameStageArgs args;
	FrameStages::Run(FrameStage::UIUpdate, args);
	renderer->SetEnabled(false);
	renderer->SetEnabled(true);
	EXPECT_EQ(renderer->GetContext(), nullptr);
}
