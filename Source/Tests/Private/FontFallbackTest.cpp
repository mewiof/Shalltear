#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/FontInspectorOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <format>

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr std::string_view INTER_ONLY = "\xEE\x80\xA0";
	constexpr std::string_view HAN = "\xE4\xB8\xAD";
	constexpr std::string_view UNASSIGNED = "\xCD\xB8";

	std::filesystem::path RepoFile(const char* relative) {
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

	class FontFallbackFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			const std::filesystem::path inter = RepoFile("EngineAssets/Fonts/Inter/Inter-Regular.ttf");
			const std::filesystem::path code = RepoFile("EngineAssets/Fonts/SourceCodePro/SourceCodePro-Regular.ttf");
			ASSERT_FALSE(inter.empty());
			ASSERT_FALSE(code.empty());

			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_fontfallback_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			std::filesystem::copy_file(inter, _root / "Assets" / "Inter.ttf");
			std::filesystem::copy_file(code, _root / "Assets" / "Code.ttf");
			std::filesystem::copy_file(code, _root / "Assets" / "Code2.ttf");
			FileSystem::Mount("project", Path(_root.generic_string()));
			FileSystem::Mount("engine", Path(RepoFile("EngineAssets").generic_string()));
			AssetDatabase::Initialize();
			UISystem::Initialize();
			for (std::string_view name : {"Inter", "Code", "Code2"}) {
				ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(FontPath(name)).IsValid());
			}
		}

		void TearDown() override {
			UISystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("engine");
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static Path FontPath(std::string_view name) { return Path(std::format("project://Assets/{}.ttf", name)); }
		static Guid GuidOf(std::string_view name) { return AssetDatabase::LookupGuid(FontPath(name)); }

		static void WriteFallbacks(std::string_view name, std::initializer_list<std::string_view> ids) {
			std::deque<str> pool;
			JSONValue list = JSONValue::MakeArray();
			for (std::string_view id : ids) {
				list.Push(JSONValue::MakeString(pool.emplace_back(id)));
			}
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("fallbacks", list);
			ASSERT_TRUE(AssetDatabase::WriteImporterSettings(FontPath(name), settings));
		}

		static void SetFallbacks(std::string_view name, std::initializer_list<std::string_view> fonts) {
			std::deque<str> ids;
			for (std::string_view font : fonts) {
				ids.push_back(GuidOf(font).ToString());
			}
			JSONValue list = JSONValue::MakeArray();
			for (const str& id : ids) {
				list.Push(JSONValue::MakeString(id));
			}
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("fallbacks", list);
			ASSERT_TRUE(AssetDatabase::WriteImporterSettings(FontPath(name), settings));
		}

		static FontAsset* Load(std::string_view name) {
			Asset* loaded = AssetDatabase::LoadAsset(FontPath(name));
			if (!loaded || loaded->GetAssetTypeId() != FontAsset::GetReflectType()->name) {
				if (loaded) {
					AssetDatabase::UnloadAsset(loaded);
				}
				return nullptr;
			}
			return static_cast<FontAsset*>(loaded);
		}

		static UI::TextQuery Query(FontAsset* font, std::string_view text) {
			UI::TextQuery query;
			query.font = Handle<FontAsset>(font);
			query.sizePx = 16.0f;
			query.utf8 = text;
			return query;
		}

		static f32 Width(FontAsset* font, std::string_view text) { return UISystem::GetFontSystem()->MeasureText(Query(font, text)).x; }

		static u32 RefsOf(std::string_view name) {
			const Asset* loaded = AssetDatabase::FindLoadedByGuid(GuidOf(name));
			return loaded ? loaded->GetRefCount() : 0u;
		}
	};

	class FontInspectorFixture : public FontFallbackFixture {
	protected:
		void SetUp() override {
			FontFallbackFixture::SetUp();
			if (HasFatalFailure()) {
				return;
			}
			PanelManager::Initialize();
			RegisterBuiltInPanels();
			ASSERT_TRUE(UISystem::GetDefaultFont().IsValid());
		}

		void TearDown() override {
			PanelManager::Terminate();
			FontFallbackFixture::TearDown();
		}

		static void Build(UI::Context& context, UI::ElementHandle body, std::string_view name, FontAsset& font) {
			while (const UI::Element* pod = context.GetElement(body)) {
				if (pod->firstChild == UI::INVALID_UI_INDEX) {
					break;
				}
				context.DestroyElement(context.MakeHandle(pod->firstChild));
			}
			const AssetInspectorFn* inspector = PropertyDrawers::FindAssetInspector(FontAsset::GetReflectType()->name);
			ASSERT_NE(inspector, nullptr);
			(*inspector)(context, body, GuidOf(name), font);
			context.Update();
		}

		static UI::ElementHandle ButtonWearing(UI::Context& context, UI::ElementHandle root, EditorIcons::Icon icon) {
			UI::ElementHandle face = FindByIcon(context, root, EditorIcons::GetGlyph(icon));
			const UI::Element* pod = context.GetElement(face);
			return pod ? context.MakeHandle(pod->parent) : UI::ElementHandle{};
		}

		static UI::ElementHandle RowOf(UI::Context& context, UI::ElementHandle element) {
			for (u32 depth = 0; depth < 2 && context.IsAlive(element); ++depth) {
				const UI::Element* pod = context.GetElement(element);
				element = context.MakeHandle(pod->parent);
			}
			return element;
		}
	};

} // anonymous namespace

TEST_F(FontFallbackFixture, MissingGlyphDrawsFromFallback) {
	SetFallbacks("Code", {"Inter"});
	FontAsset* code = Load("Code");
	FontAsset* plain = Load("Code2");
	FontAsset* inter = Load("Inter");
	ASSERT_NE(code, nullptr);
	ASSERT_NE(plain, nullptr);
	ASSERT_NE(inter, nullptr);
	ASSERT_EQ(code->fallbacks.size(), 1u);

	const f32 fromInter = Width(inter, INTER_ONLY);
	EXPECT_NEAR(fromInter, 12.0f, 1.0f);
	EXPECT_EQ(Width(plain, INTER_ONLY), Width(plain, "?"));
	EXPECT_NE(Width(plain, "?"), fromInter);
	EXPECT_EQ(Width(code, INTER_ONLY), fromInter);

	EXPECT_EQ(Width(code, "i"), Width(plain, "i"));
	EXPECT_NE(Width(code, "i"), Width(inter, "i"));

	AssetDatabase::UnloadAsset(inter);
	AssetDatabase::UnloadAsset(plain);
	AssetDatabase::UnloadAsset(code);
}

TEST_F(FontFallbackFixture, FallbacksSearchedDepthFirst) {
	SetFallbacks("Code", {"Code2"});
	SetFallbacks("Code2", {"Inter"});
	FontAsset* code = Load("Code");
	FontAsset* inter = Load("Inter");
	ASSERT_NE(code, nullptr);
	ASSERT_NE(inter, nullptr);

	EXPECT_EQ(Width(code, INTER_ONLY), Width(inter, INTER_ONLY));
	EXPECT_EQ(RefsOf("Code2"), 1u);

	AssetDatabase::UnloadAsset(inter);
	AssetDatabase::UnloadAsset(code);
}

TEST_F(FontFallbackFixture, LaterFallbacksStayUnloaded) {
	SetFallbacks("Code", {"Inter", "Code2"});
	FontAsset* code = Load("Code");
	ASSERT_NE(code, nullptr);
	EXPECT_EQ(RefsOf("Inter"), 0u);

	Width(code, "A");
	EXPECT_EQ(RefsOf("Inter"), 0u);

	Width(code, INTER_ONLY);
	EXPECT_EQ(RefsOf("Inter"), 1u);
	EXPECT_EQ(RefsOf("Code2"), 0u);

	AssetDatabase::UnloadAsset(code);
	EXPECT_EQ(RefsOf("Inter"), 1u);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(RefsOf("Inter"), 0u);
}

TEST_F(FontFallbackFixture, CycleEndsInMissingMark) {
	SetFallbacks("Inter", {"Code"});
	SetFallbacks("Code", {"Inter"});
	FontAsset* inter = Load("Inter");
	FontAsset* code = Load("Code");
	ASSERT_NE(inter, nullptr);
	ASSERT_NE(code, nullptr);

	EXPECT_EQ(Width(inter, UNASSIGNED), Width(inter, "?"));
	EXPECT_EQ(Width(code, UNASSIGNED), Width(code, "?"));
	EXPECT_NE(Width(inter, "?"), Width(code, "?"));

	AssetDatabase::UnloadAsset(code);
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontFallbackFixture, InstalledFontsCoverChainGaps) {
	FontAsset* inter = Load("Inter");
	ASSERT_NE(inter, nullptr);
	EXPECT_NEAR(Width(inter, "?"), 8.0f, 1.0f);
	EXPECT_NEAR(Width(inter, HAN), 16.0f, 1.0f);
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontFallbackFixture, RunKeepsPrimaryLineHeight) {
	SetFallbacks("Code", {"Inter"});
	FontAsset* code = Load("Code");
	ASSERT_NE(code, nullptr);
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	const f32 lineHeight = fonts->GetLineHeight(Handle<FontAsset>(code), 16.0f);
	ASSERT_GT(lineHeight, 0.0f);

	for (std::string_view text : {std::string_view("A"), INTER_ONLY, HAN}) {
		UI::ShapedRun run;
		ASSERT_TRUE(fonts->ShapeText(Query(code, text), run));
		EXPECT_EQ(run.lineHeight, lineHeight);
		EXPECT_EQ(run.extent.y, lineHeight);
	}
	AssetDatabase::UnloadAsset(code);
}

TEST_F(FontFallbackFixture, BadAndRepeatedIdsDropped) {
	const str inter = GuidOf("Inter").ToString();
	WriteFallbacks("Code", {"not-an-id", inter, inter});
	FontAsset* code = Load("Code");
	ASSERT_NE(code, nullptr);
	ASSERT_EQ(code->fallbacks.size(), 1u);
	EXPECT_EQ(code->fallbacks[0], GuidOf("Inter"));
	AssetDatabase::UnloadAsset(code);
}

TEST_F(FontFallbackFixture, CookRecordsFallbacks) {
	SetFallbacks("Code", {"Inter", "Code2"});
	FontAsset* code = Load("Code");
	ASSERT_NE(code, nullptr);

	PakStringPool strings;
	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Debug)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Debug)};
	CookWriter descriptor(descriptorBytes, strings);
	CookWriter payload(payloadBytes, strings);
	ASSERT_TRUE(code->Cook(descriptor, payload));
	ASSERT_EQ(descriptor.GetReferences().size(), 2u);
	EXPECT_EQ(descriptor.GetReferences()[0], GuidOf("Inter"));
	EXPECT_EQ(descriptor.GetReferences()[1], GuidOf("Code2"));

	FontAsset* cooked = New<FontAsset>(MemTag::Asset);
	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size());
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size());
	EXPECT_TRUE(cooked->LoadCooked(descriptorReader, payloadReader));
	EXPECT_EQ(cooked->fallbacks, code->fallbacks);
	EXPECT_EQ(cooked->data, code->data);
	Delete(cooked);
	AssetDatabase::UnloadAsset(code);
}

TEST_F(FontFallbackFixture, ReimportReachesLaidOutText) {
	FontAsset* code = Load("Code");
	FontAsset* inter = Load("Inter");
	ASSERT_NE(code, nullptr);
	ASSERT_NE(inter, nullptr);
	const f32 missing = Width(code, "?");
	const f32 found = Width(inter, INTER_ONLY);
	ASSERT_NE(missing, found);

	UI::ContextDesc desc;
	desc.sizePx = {400, 100};
	desc.contentScale = 1.0f;
	UI::Context ctx(desc, UISystem::GetFontSystem());
	UI::Style lineStyle = FixedBox(400.0f, 50.0f);
	lineStyle.flexDirection = UI::FlexDirection::Row;
	UI::ElementHandle line = MakeChild(ctx, ctx.GetRoot(), lineStyle);
	UI::ElementHandle label = MakeChild(ctx, line, UI::Style{});
	ctx.SetText(label, Handle<FontAsset>(code), 16.0f, Color::White(), INTER_ONLY);
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, missing);

	SetFallbacks("Code", {"Inter"});
	ASSERT_TRUE(AssetDatabase::ReimportAsset(FontPath("Code")));
	ASSERT_EQ(AssetDatabase::FindLoadedByGuid(GuidOf("Code")), code);
	ASSERT_EQ(code->fallbacks.size(), 1u);

	UISystem::Update();
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, found);
	UI::RenderList list;
	ctx.BuildRenderList(list);
	const UI::Context::ShapedTextView view = ctx.GetShapedText(label);
	ASSERT_NE(view.run, nullptr);
	EXPECT_EQ(view.run->extent.x, found);

	AssetDatabase::UnloadAsset(inter);
	AssetDatabase::UnloadAsset(code);
}

TEST_F(FontFallbackFixture, ApplyAdoptsFontInUse) {
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("kept", JSONValue::MakeNumber(7.0));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(FontPath("Code"), settings));
	FontAsset* code = Load("Code");
	ASSERT_NE(code, nullptr);
	const u32 version = code->version;

	const Guid listed[] = {GuidOf("Inter"), GuidOf("Code"), GuidOf("Inter"), Guid{}, GuidOf("Code2")};
	ASSERT_TRUE(FontInspectorOps::Apply(FontPath("Code"), {}, listed));
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(GuidOf("Code")), code);
	EXPECT_EQ(code->version, version + 1);
	ASSERT_EQ(code->fallbacks.size(), 2u);
	EXPECT_EQ(code->fallbacks[0], GuidOf("Inter"));
	EXPECT_EQ(code->fallbacks[1], GuidOf("Code2"));

	const str meta = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(FontPath("Code"))), MemTag::Debug);
	EXPECT_NE(meta.find("\"kept\""), str::npos);
	AssetDatabase::UnloadAsset(code);
}

TEST_F(FontInspectorFixture, ListEditsApplyToFont) {
	SetFallbacks("Code", {"Code2"});
	FontAsset* code = Load("Code");
	ASSERT_NE(code, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {420, 600};
	desc.contentScale = 1.0f;
	UI::Context context(desc, UISystem::GetFontSystem());
	UI::ElementHandle body = context.CreateElement();
	UI::Style bodyStyle;
	bodyStyle.flexDirection = UI::FlexDirection::Column;
	bodyStyle.width = UI::Length::Percent(1.0f);
	context.SetStyle(body, bodyStyle);
	context.AppendChild(context.GetRoot(), body);
	Build(context, body, "Code", *code);
	ASSERT_TRUE(context.IsAlive(FindByText(context, body, "Code2.ttf")));

	ClickElement(context, FindByText(context, body, "Add Fallback"));
	UI::ElementHandle offered = FindByTextContaining(context, context.GetRoot(), "Inter.ttf");
	ASSERT_TRUE(context.IsAlive(offered));
	EXPECT_FALSE(context.IsAlive(FindByTextContaining(context, context.GetRoot(), "Code.ttf")));
	ClickElement(context, offered);
	Build(context, body, "Code", *code);

	UI::ElementHandle interRow = RowOf(context, FindByText(context, body, "Inter.ttf"));
	ASSERT_TRUE(context.IsAlive(interRow));
	UI::ElementHandle up = ButtonWearing(context, interRow, EditorIcons::Glyph::MoveUp);
	ASSERT_TRUE(context.IsAlive(up));
	ClickElement(context, up);
	Build(context, body, "Code", *code);
	interRow = RowOf(context, FindByText(context, body, "Inter.ttf"));
	UI::ElementHandle codeRow = RowOf(context, FindByText(context, body, "Code2.ttf"));
	ASSERT_TRUE(context.IsAlive(interRow));
	ASSERT_TRUE(context.IsAlive(codeRow));
	EXPECT_LT(context.GetLayout(interRow).borderBox.y, context.GetLayout(codeRow).borderBox.y);

	ClickElement(context, FindByText(context, body, "Apply"));
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(GuidOf("Code")), code);
	ASSERT_EQ(code->fallbacks.size(), 2u);
	EXPECT_EQ(code->fallbacks[0], GuidOf("Inter"));
	EXPECT_EQ(code->fallbacks[1], GuidOf("Code2"));

	Build(context, body, "Code", *code);
	codeRow = RowOf(context, FindByText(context, body, "Code2.ttf"));
	UI::ElementHandle remove = FindByText(context, codeRow, "\xC3\x97");
	ASSERT_TRUE(context.IsAlive(remove));
	ClickElement(context, remove);
	Build(context, body, "Code", *code);
	EXPECT_FALSE(context.IsAlive(FindByText(context, body, "Code2.ttf")));
	ClickElement(context, FindByText(context, body, "Apply"));
	ASSERT_EQ(code->fallbacks.size(), 1u);
	EXPECT_EQ(code->fallbacks[0], GuidOf("Inter"));

	AssetDatabase::UnloadAsset(code);
}

TEST_F(FontFallbackFixture, CandidatesSkipSelfAndListed) {
	const Guid listed[] = {GuidOf("Inter")};
	Vector<AssetBrowserOps::AssignableAsset> candidates{GetMemoryResource(MemTag::Editor)};
	FontInspectorOps::CollectCandidates(GuidOf("Code"), listed, candidates);
	auto offered = [&](std::string_view name) {
		return std::any_of(candidates.begin(), candidates.end(), [&](const AssetBrowserOps::AssignableAsset& candidate) {
			return candidate.guid == GuidOf(name);
		});
	};
	EXPECT_TRUE(offered("Code2"));
	EXPECT_FALSE(offered("Code"));
	EXPECT_FALSE(offered("Inter"));
}
