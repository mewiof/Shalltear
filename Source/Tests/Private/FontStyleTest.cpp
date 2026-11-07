#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Window.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/FontInspectorOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <format>

using namespace Shalltear;
using namespace UITest;

namespace {

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

	constexpr const char* FIXTURES[] = {"Light", "Regular", "Medium", "Bold", "Italic", "BoldItalic",
										"WeightAxis", "ItalicAxis", "SlantAxis", "OpticalSizeAxis", "Kerned"};

	class FontStyleFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _device = false;

		void SetUp() override {
			const std::filesystem::path fixtures = RepoFile("Source/Tests/Fixtures/Fonts");
			const std::filesystem::path inter = RepoFile("EngineAssets/Fonts/Inter/Inter-Regular.ttf");
			ASSERT_FALSE(fixtures.empty());
			ASSERT_FALSE(inter.empty());

			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_fontstyle_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			for (const char* name : FIXTURES) {
				std::filesystem::copy_file(fixtures / (std::string(name) + ".ttf"), _root / "Assets" / (std::string(name) + ".ttf"));
			}
			std::filesystem::copy_file(inter, _root / "Assets" / "Inter.ttf");
			FileSystem::Mount("project", Path(_root.generic_string()));
			FileSystem::Mount("engine", Path(RepoFile("EngineAssets").generic_string()));
			AssetDatabase::Initialize();
			if (_device) {
				WindowSystem::Initialize();
				RenderSystem::Initialize();
			}
			UISystem::Initialize();
			for (const char* name : FIXTURES) {
				ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(FontPath(name)).IsValid()) << name;
			}
			ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(FontPath("Inter")).IsValid());
		}

		void TearDown() override {
			UISystem::Terminate();
			AssetDatabase::Terminate();
			if (_device) {
				RenderSystem::Terminate();
				WindowSystem::Terminate();
			}
			FileSystem::Unmount("engine");
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static Path FontPath(std::string_view name) { return Path(std::format("project://Assets/{}.ttf", name)); }
		static Guid GuidOf(std::string_view name) { return AssetDatabase::LookupGuid(FontPath(name)); }

		static void SetLists(std::string_view name, std::initializer_list<std::string_view> faces,
							 std::initializer_list<std::string_view> fallbacks) {
			std::deque<str> ids;
			auto ToList = [&ids](std::initializer_list<std::string_view> names) {
				JSONValue list = JSONValue::MakeArray();
				for (std::string_view font : names) {
					list.Push(JSONValue::MakeString(ids.emplace_back(GuidOf(font).ToString())));
				}
				return list;
			};
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("faces", ToList(faces));
			settings.AddMember("fallbacks", ToList(fallbacks));
			ASSERT_TRUE(AssetDatabase::WriteImporterSettings(FontPath(name), settings));
		}

		static void SetFaces(std::string_view name, std::initializer_list<std::string_view> faces) { SetLists(name, faces, {}); }

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

		static UI::TextQuery Query(FontAsset* font, u16 weight, UI::FontStyle style, f32 sizePx, std::string_view text) {
			UI::TextQuery query;
			query.font = Handle<FontAsset>(font);
			query.sizePx = sizePx;
			query.utf8 = text;
			query.fontWeight = weight;
			query.fontStyle = style;
			return query;
		}

		static f32 Width(FontAsset* font, u16 weight, UI::FontStyle style = UI::FontStyle::Normal, f32 sizePx = 100.0f,
						 std::string_view text = "I") {
			return UISystem::GetFontSystem()->MeasureText(Query(font, weight, style, sizePx, text)).x;
		}

		static UI::Rect Ink(FontAsset* font, u16 weight, UI::FontStyle style, f32 sizePx = 100.0f, std::string_view text = "I") {
			UI::ShapedRun run;
			EXPECT_TRUE(UISystem::GetFontSystem()->ShapeText(Query(font, weight, style, sizePx, text), run));
			EXPECT_EQ(run.glyphs.size(), 1u) << text;
			return run.glyphs.size() == 1 ? run.glyphs[0].rect : UI::Rect{};
		}

		static u32 RefsOf(std::string_view name) {
			const Asset* loaded = AssetDatabase::FindLoadedByGuid(GuidOf(name));
			return loaded ? loaded->GetRefCount() : 0u;
		}
	};

	class FontInkFixture : public FontStyleFixture {
	protected:
		FontInkFixture() { _device = true; }

		void SetUp() override {
			FontStyleFixture::SetUp();
			if (!HasFatalFailure() && !RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
		}
	};

	constexpr UI::FontStyle ITALIC = UI::FontStyle::Italic;
	constexpr UI::FontStyle NORMAL = UI::FontStyle::Normal;

} // anonymous namespace

TEST_F(FontStyleFixture, BoldFaceDrawsBoldRuns) {
	SetFaces("Regular", {"Bold"});
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	ASSERT_EQ(regular->faces.size(), 1u);

	EXPECT_EQ(Width(regular, 400), 40.0f);
	EXPECT_EQ(Width(regular, 700), 70.0f);
	EXPECT_EQ(Width(regular, 600), 70.0f);
	EXPECT_EQ(Width(regular, 900), 70.0f);
	EXPECT_EQ(Width(regular, 700, ITALIC), 70.0f);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, ItalicFacesPickedByStyle) {
	SetFaces("Regular", {"Bold", "Italic", "BoldItalic"});
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);

	EXPECT_EQ(Width(regular, 400), 40.0f);
	EXPECT_EQ(Width(regular, 700), 70.0f);
	EXPECT_EQ(Width(regular, 400, ITALIC), 45.0f);
	EXPECT_EQ(Width(regular, 700, ITALIC), 75.0f);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, WeightSearchOrder) {
	SetFaces("Light", {"Regular", "Medium", "Bold"});
	FontAsset* light = Load("Light");
	ASSERT_NE(light, nullptr);
	EXPECT_EQ(Width(light, 300), 30.0f);
	EXPECT_EQ(Width(light, 350), 30.0f);
	EXPECT_EQ(Width(light, 450), 50.0f);
	EXPECT_EQ(Width(light, 500), 50.0f);
	EXPECT_EQ(Width(light, 550), 70.0f);
	EXPECT_EQ(Width(light, 800), 70.0f);
	EXPECT_EQ(Width(light, 100), 30.0f);
	AssetDatabase::UnloadAsset(light);
	AssetDatabase::CollectGarbage();
	ASSERT_EQ(RefsOf("Bold"), 0u);

	SetFaces("Bold", {"Light"});
	FontAsset* bold = Load("Bold");
	ASSERT_NE(bold, nullptr);
	ASSERT_EQ(bold->faces.size(), 1u);
	EXPECT_EQ(Width(bold, 450), 30.0f);
	EXPECT_EQ(Width(bold, 500), 30.0f);
	EXPECT_EQ(Width(bold, 250), 30.0f);
	EXPECT_EQ(Width(bold, 600), 70.0f);
	AssetDatabase::UnloadAsset(bold);
}

TEST_F(FontStyleFixture, MissingBoldSynthesized) {
	FontAsset* regular = Load("Regular");
	FontAsset* medium = Load("Medium");
	ASSERT_NE(regular, nullptr);
	ASSERT_NE(medium, nullptr);

	EXPECT_EQ(Width(regular, 500), 40.0f);
	EXPECT_EQ(Width(regular, 600), 44.0f);
	EXPECT_EQ(Width(regular, 700), 44.0f);
	EXPECT_EQ(Width(medium, 700), 54.0f);
	AssetDatabase::UnloadAsset(medium);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, MissingItalicKeepsAdvance) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	EXPECT_EQ(Width(regular, 400, ITALIC), 40.0f);
	EXPECT_EQ(Width(regular, 700, ITALIC), 44.0f);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, WeightAxisFollowsRequest) {
	FontAsset* font = Load("WeightAxis");
	ASSERT_NE(font, nullptr);
	EXPECT_EQ(Width(font, 400), 40.0f);
	EXPECT_EQ(Width(font, 700), 70.0f);
	EXPECT_EQ(Width(font, 900), 90.0f);
	EXPECT_EQ(Width(font, 1000), 90.0f);
	EXPECT_EQ(Width(font, 50), 10.0f);
	EXPECT_EQ(Width(font, 400, ITALIC), 40.0f);
	AssetDatabase::UnloadAsset(font);
}

TEST_F(FontStyleFixture, InstancesShareFaceWithoutColliding) {
	FontAsset* font = Load("WeightAxis");
	ASSERT_NE(font, nullptr);
	EXPECT_EQ(Width(font, 400), 40.0f);
	EXPECT_EQ(Width(font, 700), 70.0f);
	EXPECT_EQ(Width(font, 400), 40.0f);
	EXPECT_EQ(Width(font, 700, ITALIC), 70.0f);
	EXPECT_EQ(Width(font, 700), 70.0f);
	AssetDatabase::UnloadAsset(font);
}

TEST_F(FontStyleFixture, ItalicAxisPreferredOverShear) {
	FontAsset* font = Load("ItalicAxis");
	ASSERT_NE(font, nullptr);
	EXPECT_EQ(Width(font, 400), 40.0f);
	EXPECT_EQ(Width(font, 400, ITALIC), 50.0f);
	EXPECT_EQ(Width(font, 700, ITALIC), 80.0f);
	EXPECT_EQ(Width(font, 100, ITALIC), 20.0f);
	AssetDatabase::UnloadAsset(font);
}

TEST_F(FontStyleFixture, SlantAxisCoversItalic) {
	FontAsset* font = Load("SlantAxis");
	ASSERT_NE(font, nullptr);
	EXPECT_EQ(Width(font, 400), 40.0f);
	EXPECT_EQ(Width(font, 400, ITALIC), 51.0f);
	AssetDatabase::UnloadAsset(font);
}

TEST_F(FontStyleFixture, OpticalSizeFollowsPixelSize) {
	FontAsset* font = Load("OpticalSizeAxis");
	ASSERT_NE(font, nullptr);
	EXPECT_EQ(Width(font, 400, NORMAL, 50.0f), 15.0f);
	EXPECT_EQ(Width(font, 400, NORMAL, 100.0f), 40.0f);
	EXPECT_EQ(Width(font, 400, NORMAL, 200.0f), 80.0f);
	EXPECT_EQ(Width(font, 400, NORMAL, 50.0f), 15.0f);
	AssetDatabase::UnloadAsset(font);
}

TEST_F(FontStyleFixture, KerningFollowsSize) {
	FontAsset* font = Load("Kerned");
	ASSERT_NE(font, nullptr);
	EXPECT_EQ(Width(font, 400, NORMAL, 100.0f, "II"), 70.0f);
	EXPECT_EQ(Width(font, 400, NORMAL, 20.0f, "II"), 14.0f);
	EXPECT_EQ(Width(font, 400, NORMAL, 100.0f, "II"), 70.0f);
	EXPECT_EQ(Width(font, 400, NORMAL, 20.0f, "I"), 8.0f);
	AssetDatabase::UnloadAsset(font);
}

TEST_F(FontStyleFixture, FacesLoadOnFirstNeed) {
	SetFaces("Regular", {"Bold"});
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	EXPECT_EQ(Width(regular, 400), 40.0f);
	EXPECT_EQ(RefsOf("Bold"), 0u);

	EXPECT_EQ(Width(regular, 700), 70.0f);
	EXPECT_EQ(RefsOf("Bold"), 1u);

	AssetDatabase::UnloadAsset(regular);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(RefsOf("Bold"), 0u);
}

TEST_F(FontStyleFixture, SelfListedFaceIgnored) {
	SetFaces("Regular", {"Regular", "Bold"});
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	ASSERT_EQ(regular->faces.size(), 2u);
	EXPECT_EQ(Width(regular, 700), 70.0f);
	EXPECT_EQ(Width(regular, 400, ITALIC), 40.0f);
	EXPECT_EQ(RefsOf("Regular"), 1u);
	AssetDatabase::UnloadAsset(regular);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(RefsOf("Regular"), 0u);
	EXPECT_EQ(RefsOf("Bold"), 0u);
}

TEST_F(FontStyleFixture, FallbackGlyphsKeepRunStyle) {
	SetLists("Regular", {"Bold"}, {"Inter"});
	SetLists("Bold", {}, {"Inter"});
	FontAsset* inter = Load("Inter");
	FontAsset* regular = Load("Regular");
	FontAsset* bold = Load("Bold");
	ASSERT_NE(inter, nullptr);
	ASSERT_NE(regular, nullptr);
	ASSERT_NE(bold, nullptr);
	const f32 plain = Width(inter, 400, NORMAL, 100.0f, "A");
	ASSERT_GT(plain, 0.0f);

	EXPECT_EQ(Width(regular, 400, NORMAL, 100.0f, "A"), plain);
	EXPECT_EQ(Width(regular, 700, NORMAL, 100.0f, "A"), plain + 4.0f);
	EXPECT_EQ(Width(bold, 400, NORMAL, 100.0f, "A"), plain + 4.0f);
	EXPECT_EQ(Width(regular, 700, NORMAL, 100.0f, "IA"), 70.0f + plain + 4.0f);
	AssetDatabase::UnloadAsset(bold);
	AssetDatabase::UnloadAsset(regular);
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontStyleFixture, DescribeReadsDeclaredStyle) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	auto describe = [fonts](std::string_view name) {
		FontAsset* font = Load(name);
		UI::FontFaceInfo info;
		EXPECT_TRUE(font && fonts->DescribeFont(Handle<FontAsset>(font), info)) << name;
		if (font) {
			AssetDatabase::UnloadAsset(font);
		}
		return info;
	};
	const UI::FontFaceInfo bold = describe("BoldItalic");
	EXPECT_EQ(bold.weightMin, 700);
	EXPECT_EQ(bold.weightMax, 700);
	EXPECT_TRUE(bold.italic);
	EXPECT_FALSE(bold.variableItalic);

	const UI::FontFaceInfo axis = describe("WeightAxis");
	EXPECT_EQ(axis.weightMin, 100);
	EXPECT_EQ(axis.weightMax, 900);
	EXPECT_FALSE(axis.italic);
	EXPECT_FALSE(axis.variableItalic);

	EXPECT_TRUE(describe("ItalicAxis").variableItalic);
	EXPECT_TRUE(describe("SlantAxis").variableItalic);
	EXPECT_EQ(describe("Regular").weightMin, 400);
}

TEST_F(FontStyleFixture, InterSynthesizesBold) {
	FontAsset* inter = Load("Inter");
	ASSERT_NE(inter, nullptr);
	const f32 regular = Width(inter, 400, NORMAL, 16.0f, "Hello");
	EXPECT_EQ(Width(inter, 700, NORMAL, 16.0f, "Hello"), regular + 5.0f);
	EXPECT_EQ(Width(inter, 400, ITALIC, 16.0f, "Hello"), regular);
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontInkFixture, SyntheticBoldThickensStem) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	const UI::Rect plain = Ink(regular, 400, NORMAL);
	const UI::Rect bold = Ink(regular, 700, NORMAL);
	EXPECT_EQ(plain.w, 10.0f);
	EXPECT_EQ(plain.h, 70.0f);
	EXPECT_EQ(bold.w, 15.0f);
	EXPECT_EQ(bold.h, 75.0f);
	EXPECT_EQ(bold.y + bold.h, plain.y + plain.h);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontInkFixture, SyntheticItalicShearsGlyph) {
	FontAsset* regular = Load("Regular");
	FontAsset* axis = Load("WeightAxis");
	ASSERT_NE(regular, nullptr);
	ASSERT_NE(axis, nullptr);
	const UI::Rect upright = Ink(regular, 400, NORMAL);
	for (FontAsset* font : {regular, axis}) {
		const UI::Rect sheared = Ink(font, 400, ITALIC);
		EXPECT_EQ(sheared.w, 28.0f);
		EXPECT_EQ(sheared.h, upright.h);
		EXPECT_EQ(sheared.x, upright.x);
	}
	AssetDatabase::UnloadAsset(axis);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontInkFixture, DesignedItalicsNotSheared) {
	SetFaces("Regular", {"Italic"});
	FontAsset* regular = Load("Regular");
	FontAsset* ital = Load("ItalicAxis");
	FontAsset* slnt = Load("SlantAxis");
	ASSERT_NE(regular, nullptr);
	ASSERT_NE(ital, nullptr);
	ASSERT_NE(slnt, nullptr);
	for (FontAsset* font : {regular, ital, slnt}) {
		EXPECT_EQ(Ink(font, 400, ITALIC).w, 10.0f);
	}
	AssetDatabase::UnloadAsset(slnt);
	AssetDatabase::UnloadAsset(ital);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontInkFixture, OutlineFollowsRunInstance) {
	FontAsset* font = Load("Regular");
	ASSERT_NE(font, nullptr);
	UI::TextQuery outlined = Query(font, 700, NORMAL, 100.0f, "I");
	outlined.outlineWidth = 2.0f;
	UI::ShapedRun run;
	ASSERT_TRUE(UISystem::GetFontSystem()->ShapeText(outlined, run));
	EXPECT_EQ(run.extent.x, 44.0f);
	ASSERT_EQ(run.glyphs.size(), 1u);
	ASSERT_EQ(run.outlineGlyphs.size(), 1u);
	EXPECT_EQ(run.glyphs[0].rect.w, 15.0f);
	EXPECT_EQ(run.outlineGlyphs[0].rect.w, 19.0f);
	AssetDatabase::UnloadAsset(font);
}

TEST_F(FontInkFixture, LineHeightMovesGlyphsByHalfLeading) {
	FontAsset* inter = Load("Inter");
	ASSERT_NE(inter, nullptr);
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	UI::ShapedRun normal;
	ASSERT_TRUE(fonts->ShapeText(Query(inter, 400, NORMAL, 20.0f, "H\nH"), normal));
	ASSERT_EQ(normal.glyphs.size(), 2u);
	EXPECT_EQ(normal.glyphs[1].rect.y - normal.glyphs[0].rect.y, normal.lineHeight);

	for (f32 requested : {normal.lineHeight + 11.0f, normal.lineHeight - 7.0f, 40.0f, 22.6f}) {
		UI::TextQuery query = Query(inter, 400, NORMAL, 20.0f, "H\nH");
		query.lineHeight = requested;
		UI::ShapedRun run;
		ASSERT_TRUE(fonts->ShapeText(query, run));
		ASSERT_EQ(run.glyphs.size(), 2u) << requested;
		EXPECT_EQ(run.lineHeight, std::round(requested)) << requested;
		EXPECT_EQ(run.halfLeading, std::floor((run.lineHeight - normal.lineHeight) * 0.5f)) << requested;
		EXPECT_EQ(run.glyphs[0].rect.y, normal.glyphs[0].rect.y + run.halfLeading) << requested;
		EXPECT_EQ(run.glyphs[1].rect.y - run.glyphs[0].rect.y, run.lineHeight) << requested;
		EXPECT_EQ(run.glyphs[0].rect.x, normal.glyphs[0].rect.x) << requested;
	}
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontInkFixture, FallbackGlyphShearedWithRun) {
	SetLists("Regular", {}, {"Inter"});
	FontAsset* inter = Load("Inter");
	FontAsset* regular = Load("Regular");
	ASSERT_NE(inter, nullptr);
	ASSERT_NE(regular, nullptr);
	const UI::Rect upright = Ink(inter, 400, NORMAL, 100.0f, "l");
	EXPECT_EQ(Ink(regular, 400, NORMAL, 100.0f, "l").w, upright.w);
	EXPECT_GT(Ink(regular, 400, ITALIC, 100.0f, "l").w, upright.w + 15.0f);
	EXPECT_GT(Ink(regular, 700, NORMAL, 100.0f, "l").w, upright.w + 3.0f);
	AssetDatabase::UnloadAsset(regular);
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontInkFixture, InstalledVariableFontFollowsWeight) {
	const std::filesystem::path bahnschrift = "C:/Windows/Fonts/bahnschrift.ttf";
	ASSERT_TRUE(std::filesystem::exists(bahnschrift));
	std::filesystem::copy_file(bahnschrift, _root / "Assets" / "Bahnschrift.ttf");
	ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(FontPath("Bahnschrift")).IsValid());
	FontAsset* font = Load("Bahnschrift");
	ASSERT_NE(font, nullptr);

	UI::FontFaceInfo info;
	ASSERT_TRUE(UISystem::GetFontSystem()->DescribeFont(Handle<FontAsset>(font), info));
	EXPECT_EQ(info.weightMin, 300);
	EXPECT_EQ(info.weightMax, 700);

	const f32 light = Ink(font, 300, NORMAL, 200.0f, "I").w;
	const f32 regular = Ink(font, 400, NORMAL, 200.0f, "I").w;
	const f32 medium = Ink(font, 500, NORMAL, 200.0f, "I").w;
	const f32 bold = Ink(font, 700, NORMAL, 200.0f, "I").w;
	EXPECT_LT(light, regular);
	EXPECT_LT(regular, medium);
	EXPECT_LT(medium, bold);
	EXPECT_EQ(Ink(font, 400, NORMAL, 200.0f, "I").w, regular);
	EXPECT_EQ(Width(font, 300, NORMAL, 200.0f, "I"), Width(font, 700, NORMAL, 200.0f, "I"));
	AssetDatabase::UnloadAsset(font);
}

TEST_F(FontStyleFixture, CookRecordsFaces) {
	SetFaces("Regular", {"Bold", "Italic"});
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);

	PakStringPool strings;
	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Debug)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Debug)};
	CookWriter descriptor(descriptorBytes, strings);
	CookWriter payload(payloadBytes, strings);
	ASSERT_TRUE(regular->Cook(descriptor, payload));
	ASSERT_EQ(descriptor.GetReferences().size(), 2u);
	EXPECT_EQ(descriptor.GetReferences()[0], GuidOf("Bold"));
	EXPECT_EQ(descriptor.GetReferences()[1], GuidOf("Italic"));

	FontAsset* cooked = New<FontAsset>(MemTag::Asset);
	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size());
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size());
	EXPECT_TRUE(cooked->LoadCooked(descriptorReader, payloadReader));
	EXPECT_EQ(cooked->faces, regular->faces);
	EXPECT_TRUE(cooked->fallbacks.empty());
	EXPECT_EQ(cooked->data, regular->data);
	Delete(cooked);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, StyleSheetWeightReachesText) {
	SetFaces("Regular", {"Bold", "Italic"});
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {400, 200};
	desc.contentScale = 1.0f;
	UI::Context ctx(desc, UISystem::GetFontSystem());
	ctx.LoadStyleSheet(".strong { font-weight: bold; } .lean { font-style: italic; }");
	UI::Style lineStyle = FixedBox(400.0f, 150.0f);
	lineStyle.flexDirection = UI::FlexDirection::Row;
	UI::ElementHandle line = MakeChild(ctx, ctx.GetRoot(), lineStyle);
	UI::ElementHandle label = MakeChild(ctx, line, UI::Style{});
	ctx.SetText(label, Handle<FontAsset>(regular), 100.0f, Color::White(), "I");

	auto drawn = [&]() {
		ctx.Update();
		UI::RenderList list;
		ctx.BuildRenderList(list);
		const UI::Context::ShapedTextView view = ctx.GetShapedText(label);
		EXPECT_NE(view.run, nullptr);
		const f32 shaped = view.run ? view.run->extent.x : -1.0f;
		EXPECT_EQ(shaped, ctx.GetLayout(label).content.w);
		return shaped;
	};
	EXPECT_EQ(drawn(), 40.0f);
	ctx.AddClass(label, "strong"_hash);
	EXPECT_EQ(drawn(), 70.0f);
	ctx.RemoveClass(label, "strong"_hash);
	EXPECT_EQ(drawn(), 40.0f);
	ctx.AddClass(label, "lean"_hash);
	EXPECT_EQ(drawn(), 45.0f);
	ctx.RemoveClass(label, "lean"_hash);
	EXPECT_EQ(drawn(), 40.0f);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, ReimportReachesStyledText) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {400, 200};
	desc.contentScale = 1.0f;
	UI::Context ctx(desc, UISystem::GetFontSystem());
	UI::Style lineStyle = FixedBox(400.0f, 150.0f);
	lineStyle.flexDirection = UI::FlexDirection::Row;
	UI::ElementHandle line = MakeChild(ctx, ctx.GetRoot(), lineStyle);
	UI::Style bold;
	bold.fontWeight = 700;
	UI::ElementHandle label = MakeChild(ctx, line, bold);
	ctx.SetText(label, Handle<FontAsset>(regular), 100.0f, Color::White(), "I");
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, 44.0f);

	SetFaces("Regular", {"Bold"});
	ASSERT_TRUE(AssetDatabase::ReimportAsset(FontPath("Regular")));
	ASSERT_EQ(AssetDatabase::FindLoadedByGuid(GuidOf("Regular")), regular);
	ASSERT_EQ(regular->faces.size(), 1u);
	UISystem::Update();
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, 70.0f);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, ApplyWritesBothLists) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	const u32 version = regular->version;
	const Guid faces[] = {GuidOf("Bold"), GuidOf("Regular"), GuidOf("Bold"), Guid{}, GuidOf("Italic")};
	const Guid fallbacks[] = {GuidOf("Inter")};
	ASSERT_TRUE(FontInspectorOps::Apply(FontPath("Regular"), faces, fallbacks));
	EXPECT_EQ(regular->version, version + 1);
	ASSERT_EQ(regular->faces.size(), 2u);
	EXPECT_EQ(regular->faces[0], GuidOf("Bold"));
	EXPECT_EQ(regular->faces[1], GuidOf("Italic"));
	ASSERT_EQ(regular->fallbacks.size(), 1u);
	EXPECT_EQ(regular->fallbacks[0], GuidOf("Inter"));
	AssetDatabase::UnloadAsset(regular);
}

class FontFacesInspectorFixture : public FontStyleFixture {
protected:
	void SetUp() override {
		FontStyleFixture::SetUp();
		if (HasFatalFailure()) {
			return;
		}
		PanelManager::Initialize();
		RegisterBuiltInPanels();
	}

	void TearDown() override {
		PanelManager::Terminate();
		FontStyleFixture::TearDown();
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
};

TEST_F(FontFacesInspectorFixture, AddedFaceShowsDeclaredStyle) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {520, 700};
	desc.contentScale = 1.0f;
	UI::Context context(desc, UISystem::GetFontSystem());
	UI::ElementHandle body = context.CreateElement();
	UI::Style bodyStyle;
	bodyStyle.flexDirection = UI::FlexDirection::Column;
	bodyStyle.width = UI::Length::Percent(1.0f);
	context.SetStyle(body, bodyStyle);
	context.AppendChild(context.GetRoot(), body);
	Build(context, body, "Regular", *regular);
	EXPECT_TRUE(context.IsAlive(FindByText(context, body, "This file: 400")));

	ClickElement(context, FindByText(context, body, "Add Face"));
	UI::ElementHandle offered = FindByTextContaining(context, context.GetRoot(), "BoldItalic.ttf");
	ASSERT_TRUE(context.IsAlive(offered));
	ClickElement(context, offered);
	Build(context, body, "Regular", *regular);
	ASSERT_TRUE(context.IsAlive(FindByText(context, body, "BoldItalic.ttf")));
	EXPECT_TRUE(context.IsAlive(FindByText(context, body, "700 Italic")));

	ClickElement(context, FindByText(context, body, "Add Face"));
	offered = FindByTextContaining(context, context.GetRoot(), "WeightAxis.ttf");
	ASSERT_TRUE(context.IsAlive(offered));
	ClickElement(context, offered);
	Build(context, body, "Regular", *regular);
	EXPECT_TRUE(context.IsAlive(FindByText(context, body, "100-900")));
	EXPECT_TRUE(regular->faces.empty());

	ClickElement(context, FindByText(context, body, "Apply"));
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(GuidOf("Regular")), regular);
	ASSERT_EQ(regular->faces.size(), 2u);
	EXPECT_EQ(regular->faces[0], GuidOf("BoldItalic"));
	EXPECT_EQ(regular->faces[1], GuidOf("WeightAxis"));
	EXPECT_TRUE(regular->fallbacks.empty());
	AssetDatabase::UnloadAsset(regular);
}
