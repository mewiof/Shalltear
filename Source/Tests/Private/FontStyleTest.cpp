#include "TestsPCH.h"

#include "TestPlatform.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Window.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/UIDocument.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/FontInspectorOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <vector>

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
							 std::initializer_list<std::string_view> fallbacks,
							 SyntheticBoldSpacing boldSpacing = SyntheticBoldSpacing::Widened) {
			std::deque<str> ids;
			auto toList = [&ids](std::initializer_list<std::string_view> names) {
				JSONValue list = JSONValue::MakeArray();
				for (std::string_view font : names) {
					list.Push(JSONValue::MakeString(ids.emplace_back(GuidOf(font).ToString())));
				}
				return list;
			};
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("faces", toList(faces));
			settings.AddMember("fallbacks", toList(fallbacks));
			if (boldSpacing == SyntheticBoldSpacing::Regular) {
				settings.AddMember("syntheticBoldSpacing", JSONValue::MakeString("regular"));
			}
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

		template <typename T>
		T* LoadText(std::string_view file, std::string_view text) {
			std::ofstream(_root / "Assets" / std::string(file), std::ios::binary) << text;
			const Path path(std::format("project://Assets/{}", file));
			EXPECT_TRUE(AssetDatabase::EnsureAssetIdentity(path).IsValid());
			Asset* loaded = AssetDatabase::LoadAsset(path);
			if (!loaded || loaded->GetAssetTypeId() != T::GetReflectType()->name) {
				if (loaded) {
					AssetDatabase::UnloadAsset(loaded);
				}
				return nullptr;
			}
			return static_cast<T*>(loaded);
		}

		static f32 TitleWidth(const UIDocumentAsset& document) {
			UI::ContextDesc desc;
			desc.sizePx = {400, 300};
			desc.contentScale = 1.0f;
			UI::Context ctx(desc, UISystem::GetFontSystem());
			Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Debug)};
			document.Instantiate(ctx, ctx.GetRoot(), UIInstantiateDesc{}, mapping);
			ctx.Update();
			for (const UIInstantiateMapping& entry : mapping) {
				if (entry.node && entry.node->id == "title") {
					return ctx.GetLayout(entry.element).content.w;
				}
			}
			return -1.0f;
		}

		static UI::TextRun RunOf(FontAsset* font, u32 begin, u32 end, f32 sizePx = 100.0f) {
			UI::TextRun run;
			run.begin = begin;
			run.end = end;
			run.font = Handle<FontAsset>(font);
			run.sizePx = sizePx;
			return run;
		}

		static UI::ShapedRun ShapeRuns(UI::TextQuery query, std::span<const UI::TextRun> runs) {
			query.runs = runs;
			UI::ShapedRun shaped;
			EXPECT_TRUE(UISystem::GetFontSystem()->ShapeText(query, shaped));
			const Vec2 measured = UISystem::GetFontSystem()->MeasureText(query);
			EXPECT_EQ(measured.x, shaped.extent.x);
			EXPECT_EQ(measured.y, shaped.extent.y);
			return shaped;
		}

		static std::vector<f32> CaretXs(const UI::ShapedRun& run) {
			std::vector<f32> xs;
			for (const UI::CaretStop& stop : run.caretStops) {
				xs.push_back(stop.x);
			}
			return xs;
		}

		static str Decorations(const UI::ShapedRun& run) {
			str out;
			for (const UI::ShapedDecoration& decoration : run.decorations) {
				const char kind = decoration.kind == UI::TextDecoration::Mark ? 'M' : (decoration.kind == UI::TextDecoration::Underline ? 'U' : 'S');
				out += std::format("{}{} {} {} {} {}", out.empty() ? "" : "|", kind, decoration.rect.x, decoration.rect.y, decoration.rect.w,
								   decoration.rect.h);
			}
			return out;
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

TEST_F(FontStyleFixture, SyntheticBoldCanKeepRegularSpacing) {
	SetLists("Regular", {}, {}, SyntheticBoldSpacing::Regular);
	SetLists("Medium", {}, {}, SyntheticBoldSpacing::Regular);
	FontAsset* regular = Load("Regular");
	FontAsset* inter = Load("Inter");
	FontAsset* medium = Load("Medium");
	ASSERT_NE(regular, nullptr);
	ASSERT_NE(inter, nullptr);
	ASSERT_NE(medium, nullptr);
	EXPECT_EQ(regular->syntheticBoldSpacing, SyntheticBoldSpacing::Regular);
	EXPECT_EQ(Width(regular, 400), 40.0f);
	EXPECT_EQ(Width(regular, 700), 40.0f);
	EXPECT_EQ(Width(regular, 700, ITALIC), 40.0f);
	EXPECT_EQ(Width(medium, 500), Width(medium, 700));
	EXPECT_EQ(Width(regular, 700, NORMAL, 16.0f, "IIIIIIIIII"), Width(regular, 400, NORMAL, 16.0f, "IIIIIIIIII"));
	const f32 interRegular = Width(inter, 400, NORMAL, 16.0f, "Hello");
	EXPECT_EQ(Width(inter, 700, NORMAL, 16.0f, "Hello"), interRegular + 5.0f);
	AssetDatabase::UnloadAsset(medium);
	AssetDatabase::UnloadAsset(inter);
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

TEST_F(FontInkFixture, RegularSpacingBoldStillThickensRightward) {
	SetLists("Regular", {}, {}, SyntheticBoldSpacing::Regular);
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	const UI::Rect plain = Ink(regular, 400, NORMAL);
	const UI::Rect bold = Ink(regular, 700, NORMAL);
	EXPECT_EQ(plain.w, 10.0f);
	EXPECT_EQ(bold.w, 15.0f);
	EXPECT_EQ(bold.x, plain.x);
	EXPECT_EQ(bold.y + bold.h, plain.y + plain.h);

	UI::ShapedRun line;
	ASSERT_TRUE(UISystem::GetFontSystem()->ShapeText(Query(regular, 700, NORMAL, 100.0f, "III"), line));
	UI::ShapedRun plainLine;
	ASSERT_TRUE(UISystem::GetFontSystem()->ShapeText(Query(regular, 400, NORMAL, 100.0f, "III"), plainLine));
	ASSERT_EQ(line.glyphs.size(), 3u);
	ASSERT_EQ(plainLine.glyphs.size(), 3u);
	for (size_t i = 0; i < 3; ++i) {
		EXPECT_EQ(line.glyphs[i].rect.x, plainLine.glyphs[i].rect.x) << i;
	}
	EXPECT_EQ(line.extent.x, plainLine.extent.x);
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
	const std::filesystem::path bahnschrift = TestPlatform::FindSystemFont("bahnschrift.ttf");
	if (bahnschrift.empty()) {
		GTEST_SKIP() << "bahnschrift.ttf isn't installed";
	}
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
	SetLists("Regular", {"Bold", "Italic"}, {}, SyntheticBoldSpacing::Regular);
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
	EXPECT_EQ(cooked->syntheticBoldSpacing, SyntheticBoldSpacing::Regular);
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

TEST_F(FontStyleFixture, DocumentFontFamilyReachesText) {
	UIDocumentAsset* document = LoadText<UIDocumentAsset>(
		"Title.slxml", R"xml(<ui version="1"><panel style="font-size: 100px; font-family: url('project://Assets/Bold.ttf')"><panel id="title">I</panel></panel></ui>)xml");
	ASSERT_NE(document, nullptr);
	ASSERT_EQ(document->assets.size(), 1u);
	EXPECT_EQ(document->assets[0].src, "project://Assets/Bold.ttf");
	EXPECT_EQ(document->assets[0].asset.Get(), AssetDatabase::FindLoadedByGuid(GuidOf("Bold")));
	EXPECT_EQ(RefsOf("Bold"), 1u);
	EXPECT_EQ(TitleWidth(*document), 70.0f);
	AssetDatabase::UnloadAsset(document);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(RefsOf("Bold"), 0u);
}

TEST_F(FontStyleFixture, SheetFontFamilyReachesText) {
	StyleSheetAsset* sheet = LoadText<StyleSheetAsset>("Look.slss", ".title { font-size: 100px; font-family: \"project://Assets/Bold.ttf\"; }\n");
	ASSERT_NE(sheet, nullptr);
	ASSERT_EQ(sheet->fonts.size(), 1u);
	EXPECT_EQ(sheet->fonts[0].src, "project://Assets/Bold.ttf");
	EXPECT_EQ(RefsOf("Bold"), 1u);

	UIDocumentAsset* document = LoadText<UIDocumentAsset>(
		"Page.slxml", R"(<ui version="1"><sheet src="project://Assets/Look.slss"/><panel><panel id="title" class="title">I</panel></panel></ui>)");
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(TitleWidth(*document), 70.0f);
	AssetDatabase::UnloadAsset(document);
	AssetDatabase::UnloadAsset(sheet);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(RefsOf("Bold"), 0u);
}

TEST_F(FontStyleFixture, SheetFontsCookByGuid) {
	StyleSheetAsset* sheet = LoadText<StyleSheetAsset>("Cooked.slss", ".a { font-family: url(project://Assets/Bold.ttf); } .b { font-family: url(project://Assets/Italic.ttf); } .c { font-family: url(project://Assets/Bold.ttf); }\n");
	ASSERT_NE(sheet, nullptr);
	ASSERT_EQ(sheet->fonts.size(), 2u);

	PakStringPool strings;
	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Debug)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Debug)};
	CookWriter descriptor(descriptorBytes, strings);
	CookWriter payload(payloadBytes, strings);
	ASSERT_TRUE(sheet->Cook(descriptor, payload));
	ASSERT_EQ(descriptor.GetReferences().size(), 2u);
	EXPECT_EQ(descriptor.GetReferences()[0], GuidOf("Bold"));
	EXPECT_EQ(descriptor.GetReferences()[1], GuidOf("Italic"));
	EXPECT_TRUE(payloadBytes.empty());

	StyleSheetAsset* cooked = New<StyleSheetAsset>(MemTag::Asset);
	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size());
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size());
	ASSERT_TRUE(cooked->LoadCooked(descriptorReader, payloadReader));
	EXPECT_EQ(cooked->source, sheet->source);
	ASSERT_EQ(cooked->fonts.size(), 2u);
	EXPECT_EQ(cooked->fonts[1].src, "project://Assets/Italic.ttf");
	EXPECT_EQ(cooked->fonts[1].font.Get(), AssetDatabase::FindLoadedByGuid(GuidOf("Italic")));
	EXPECT_EQ(RefsOf("Italic"), 2u);
	cooked->NotifyUnloaded();
	Delete(cooked);
	EXPECT_EQ(RefsOf("Italic"), 1u);
	AssetDatabase::UnloadAsset(sheet);
}

TEST_F(FontStyleFixture, MissingSheetFontSkipped) {
	StyleSheetAsset* sheet = LoadText<StyleSheetAsset>("Broken.slss", ".x { font-family: url('project://Assets/Nope.ttf'); } .y { opacity: 0.5; }\n");
	ASSERT_NE(sheet, nullptr);
	EXPECT_TRUE(sheet->fonts.empty());
	EXPECT_NE(sheet->source.find(".y"), str::npos);
	AssetDatabase::UnloadAsset(sheet);
}

TEST_F(FontStyleFixture, StyleFontOutlivedByName) {
	FontAsset* bold = Load("Bold");
	ASSERT_NE(bold, nullptr);
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {400, 200};
	desc.contentScale = 1.0f;
	UI::Context ctx(desc, UISystem::GetFontSystem());
	ctx.SetStyleFont("bold", Handle<FontAsset>(bold));
	ctx.LoadStyleSheet(".t { font-family: bold; font-size: 100px; }");
	UI::ElementHandle line = MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 150.0f));
	UI::ElementHandle label = MakeChild(ctx, line, UI::Style{});
	ctx.AddClass(label, "t"_hash);
	ctx.SetTextString(label, "I");
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, 70.0f);

	AssetDatabase::UnloadAsset(bold);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(GuidOf("Bold")), nullptr);
	EXPECT_EQ(ctx.GetComputedStyle(label).font.value_or(Handle<FontAsset>{}).Get(), nullptr);
	UISystem::Update();
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);

	ctx.SetStyleFont("bold", Handle<FontAsset>(regular));
	ctx.Update();
	EXPECT_EQ(ctx.GetLayout(label).content.w, 40.0f);
	ctx.BuildRenderList(list);
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
	ASSERT_TRUE(FontInspectorOps::Apply(FontPath("Regular"), faces, fallbacks, SyntheticBoldSpacing::Regular));
	EXPECT_EQ(regular->version, version + 1);
	ASSERT_EQ(regular->faces.size(), 2u);
	EXPECT_EQ(regular->faces[0], GuidOf("Bold"));
	EXPECT_EQ(regular->faces[1], GuidOf("Italic"));
	ASSERT_EQ(regular->fallbacks.size(), 1u);
	EXPECT_EQ(regular->fallbacks[0], GuidOf("Inter"));
	EXPECT_EQ(regular->syntheticBoldSpacing, SyntheticBoldSpacing::Regular);

	ASSERT_TRUE(FontInspectorOps::Apply(FontPath("Regular"), faces, fallbacks, SyntheticBoldSpacing::Widened));
	EXPECT_EQ(regular->version, version + 2);
	EXPECT_EQ(regular->syntheticBoldSpacing, SyntheticBoldSpacing::Widened);
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

TEST_F(FontFacesInspectorFixture, BoldSpacingAppliedFromInspector) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {520, 900};
	desc.contentScale = 1.0f;
	UI::Context context(desc, UISystem::GetFontSystem());
	UI::ElementHandle body = context.CreateElement();
	UI::Style bodyStyle;
	bodyStyle.flexDirection = UI::FlexDirection::Column;
	bodyStyle.width = UI::Length::Percent(1.0f);
	context.SetStyle(body, bodyStyle);
	context.AppendChild(context.GetRoot(), body);
	Build(context, body, "Regular", *regular);
	ASSERT_TRUE(context.IsAlive(FindByText(context, body, "Synthetic Bold")));

	ClickElement(context, FindByText(context, body, "Widened"));
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	ClickElement(context, FindByText(context, context.GetOpenPopup(0), "Regular"));
	EXPECT_EQ(regular->syntheticBoldSpacing, SyntheticBoldSpacing::Widened);
	ClickElement(context, FindByText(context, body, "Apply"));
	EXPECT_EQ(regular->syntheticBoldSpacing, SyntheticBoldSpacing::Regular);
	EXPECT_EQ(Width(regular, 700), 40.0f);

	Build(context, body, "Regular", *regular);
	EXPECT_TRUE(context.IsAlive(FindByText(context, body, "Regular")));
	EXPECT_FALSE(context.IsAlive(FindByText(context, body, "Widened")));
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, RichRunsPickFaces) {
	SetFaces("Regular", {"Bold"});
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::TextRun runs[] = {RunOf(regular, 0, 1), RunOf(regular, 1, 2), RunOf(regular, 2, 3)};
	runs[1].fontWeight = 700;
	const UI::ShapedRun shaped = ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "III"), runs);
	EXPECT_EQ(shaped.extent.x, 150.0f);
	EXPECT_EQ(shaped.extent.y, 100.0f);
	EXPECT_EQ(CaretXs(shaped), (std::vector<f32>{0.0f, 40.0f, 110.0f, 150.0f}));
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, RichSizesShareLineBox) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::TextRun runs[] = {RunOf(regular, 0, 1), RunOf(regular, 1, 2, 50.0f)};
	UI::ShapedRun shaped = ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "II"), runs);
	EXPECT_EQ(shaped.extent.x, 60.0f);
	EXPECT_EQ(shaped.extent.y, 100.0f);

	runs[1].lineHeight = 200.0f;
	EXPECT_EQ(ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "II"), runs).extent.y, 200.0f);
	runs[1].lineHeight = 0.0f;
	runs[1].lineHeightScale = 4.0f;
	EXPECT_EQ(ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "II"), runs).extent.y, 200.0f);

	UI::TextRun lines[] = {RunOf(regular, 0, 2), RunOf(regular, 2, 3, 50.0f)};
	shaped = ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "I\nI"), lines);
	EXPECT_EQ(shaped.extent.x, 40.0f);
	EXPECT_EQ(shaped.extent.y, 150.0f);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, RichShiftsKeepLineBox) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::TextRun runs[] = {RunOf(regular, 0, 1), RunOf(regular, 1, 2), RunOf(regular, 2, 3), RunOf(regular, 3, 4)};
	runs[1].script = UI::TextScript::Sub;
	runs[2].script = UI::TextScript::Super;
	runs[3].baselineShift = 30.0f;
	const UI::ShapedRun shaped = ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "IIII"), runs);
	EXPECT_EQ(shaped.extent.x, 40.0f + 26.0f + 26.0f + 40.0f);
	EXPECT_EQ(shaped.extent.y, 100.0f);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, RichScriptsReadFontTables) {
	FontAsset* inter = Load("Inter");
	ASSERT_NE(inter, nullptr);
	UI::TextRun runs[] = {RunOf(inter, 0, 1), RunOf(inter, 1, 2), RunOf(inter, 2, 3)};
	runs[1].script = UI::TextScript::Sub;
	runs[2].script = UI::TextScript::Super;
	const f32 small = Width(inter, 400, NORMAL, 60.0f, "H");
	EXPECT_LT(small, Width(inter, 400, NORMAL, 65.0f, "H"));
	EXPECT_EQ(ShapeRuns(Query(inter, 400, NORMAL, 100.0f, "HHH"), runs).extent.x, Width(inter, 400, NORMAL, 100.0f, "H") + small * 2.0f);
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontStyleFixture, RichDecorationsFromFallbackMetrics) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::TextRun runs[] = {RunOf(regular, 0, 4, 140.0f)};
	runs[0].decoration = UI::TextDecoration::Mark | UI::TextDecoration::Underline | UI::TextDecoration::Strikethrough;
	const UI::ShapedRun shaped = ShapeRuns(Query(regular, 400, NORMAL, 140.0f, "I I "), runs);
	EXPECT_EQ(shaped.extent.x, 182.0f);
	EXPECT_EQ(Decorations(shaped), "M 0 0 147 140|U 0 121 147 10|S 0 65 147 10");
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, RichDecorationsJoinAcrossRuns) {
	SetFaces("Regular", {"Bold"});
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	constexpr UI::TextDecoration U = UI::TextDecoration::Underline;
	constexpr UI::TextDecoration S = UI::TextDecoration::Strikethrough;
	constexpr UI::TextDecoration NONE = UI::TextDecoration::None;
	auto decorate = [&](std::string_view text, std::initializer_list<UI::TextDecoration> kinds, u16 middlePaint = 0) {
		std::vector<UI::TextRun> runs;
		for (UI::TextDecoration kind : kinds) {
			const u32 begin = static_cast<u32>(runs.size());
			runs.push_back(RunOf(regular, begin, begin + 1, 140.0f));
			runs.back().decoration = kind;
		}
		runs[1].fontWeight = 700;
		runs[1].paint = middlePaint;
		return Decorations(ShapeRuns(Query(regular, 400, NORMAL, 140.0f, text), runs));
	};
	EXPECT_EQ(decorate("III", {U, U, U}), "U 0 121 210 10");
	EXPECT_EQ(decorate("III", {U, U, U}, 1), "U 0 121 56 10|U 56 121 98 10|U 154 121 56 10");
	EXPECT_EQ(decorate("III", {U, NONE, U}), "U 0 121 56 10|U 154 121 56 10");
	EXPECT_EQ(decorate("III", {U | S, U, U | S}), "U 0 121 210 10|S 0 65 56 10|S 154 65 56 10");
	EXPECT_EQ(decorate("I I", {U, U, U}), "U 0 121 147 10");
	EXPECT_EQ(decorate("I I", {U, NONE, U}), "U 0 121 56 10|U 91 121 56 10");
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, RichDecorationsFollowAlignment) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::TextRun runs[] = {RunOf(regular, 0, 4, 140.0f)};
	runs[0].decoration = UI::TextDecoration::Underline;
	UI::TextQuery query = Query(regular, 400, NORMAL, 140.0f, "II\nI");
	query.align = UI::Align::Center;
	EXPECT_EQ(Decorations(ShapeRuns(query, runs)), "U 0 121 112 10|U 28 261 56 10");
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, RichDecorationsReadFontTables) {
	FontAsset* inter = Load("Inter");
	ASSERT_NE(inter, nullptr);
	UI::TextRun runs[] = {RunOf(inter, 0, 1)};
	runs[0].decoration = UI::TextDecoration::Underline | UI::TextDecoration::Strikethrough;
	const UI::ShapedRun shaped = ShapeRuns(Query(inter, 400, NORMAL, 100.0f, "H"), runs);
	ASSERT_EQ(shaped.decorations.size(), 2u);
	const UI::Rect underline = shaped.decorations[0].rect;
	const UI::Rect strike = shaped.decorations[1].rect;
	EXPECT_EQ(underline.h, 7.0f);
	EXPECT_EQ(strike.h, 7.0f);
	const f32 upem = 2048.0f;
	const f32 centers = (317.0f + 70.0f + 663.0f - 70.0f) / upem * 100.0f;
	EXPECT_NEAR(underline.y - strike.y, centers, 1.0f);
	EXPECT_EQ(underline.w, Width(inter, 400, NORMAL, 100.0f, "H"));
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontStyleFixture, RichKerningFollowsFace) {
	FontAsset* kerned = Load("Kerned");
	ASSERT_NE(kerned, nullptr);
	auto width = [&](auto&& edit) {
		UI::TextRun runs[] = {RunOf(kerned, 0, 1), RunOf(kerned, 1, 2)};
		edit(runs[0], runs[1]);
		return ShapeRuns(Query(kerned, 400, NORMAL, 100.0f, "II"), runs).extent.x;
	};
	EXPECT_EQ(width([](UI::TextRun&, UI::TextRun&) {}), 70.0f);
	EXPECT_EQ(width([](UI::TextRun&, UI::TextRun& b) { b.paint = 3; }), 70.0f);
	EXPECT_EQ(width([](UI::TextRun& a, UI::TextRun&) { a.letterSpacing = 5.0f; }), 75.0f);
	EXPECT_EQ(width([](UI::TextRun&, UI::TextRun& b) { b.fontWeight = 700; }), 84.0f);
	EXPECT_EQ(width([](UI::TextRun&, UI::TextRun& b) { b.sizePx = 50.0f; }), 60.0f);
	EXPECT_EQ(width([](UI::TextRun&, UI::TextRun& b) { b.advance = 50.0f; }), 90.0f);

	UI::TextRun cells[] = {RunOf(kerned, 0, 2)};
	cells[0].advance = 50.0f;
	EXPECT_EQ(ShapeRuns(Query(kerned, 400, NORMAL, 100.0f, "II"), cells).extent.x, 100.0f);
	AssetDatabase::UnloadAsset(kerned);
}

TEST_F(FontStyleFixture, RichSpacingFollowsEarlierRun) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::TextRun runs[] = {RunOf(regular, 0, 1), RunOf(regular, 1, 2)};
	runs[0].letterSpacing = 10.0f;
	EXPECT_EQ(ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "II"), runs).extent.x, 90.0f);
	runs[0].letterSpacing = 0.0f;
	runs[1].letterSpacing = 10.0f;
	EXPECT_EQ(ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "II"), runs).extent.x, 80.0f);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, RichCellsSetAdvance) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::TextRun runs[] = {RunOf(regular, 0, 3)};
	runs[0].advance = 50.0f;
	const UI::ShapedRun shaped = ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "I?I"), runs);
	EXPECT_EQ(CaretXs(shaped), (std::vector<f32>{0.0f, 50.0f, 100.0f, 150.0f}));
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, RichNoBreakMovesBreak) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::TextQuery query = Query(regular, 400, NORMAL, 100.0f, "I I I");
	query.wrapWidth = 150.0f;
	query.overflow = UI::TextOverflow::Wrap;
	UI::TextRun plain[] = {RunOf(regular, 0, 5)};
	UI::ShapedRun shaped = ShapeRuns(query, plain);
	EXPECT_EQ(shaped.extent.x, 130.0f);
	EXPECT_EQ(shaped.extent.y, 200.0f);

	UI::TextRun held[] = {RunOf(regular, 0, 2), RunOf(regular, 2, 5)};
	held[1].noBreak = true;
	shaped = ShapeRuns(query, held);
	EXPECT_EQ(shaped.extent.x, 105.0f);
	EXPECT_EQ(shaped.extent.y, 200.0f);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontStyleFixture, RichCaseTransforms) {
	FontAsset* inter = Load("Inter");
	ASSERT_NE(inter, nullptr);
	auto width = [&](std::string_view text, UI::TextCase textCase) {
		UI::TextRun runs[] = {RunOf(inter, 0, static_cast<u32>(text.size()), 40.0f)};
		runs[0].textCase = textCase;
		const UI::ShapedRun shaped = ShapeRuns(Query(inter, 400, NORMAL, 40.0f, text), runs);
		EXPECT_EQ(shaped.caretStops.back().byteOffset, text.size());
		return shaped.extent.x;
	};
	EXPECT_EQ(width("Wavy \xC3\xA9t\xC3\xA9", UI::TextCase::Upper), Width(inter, 400, NORMAL, 40.0f, "WAVY \xC3\x89T\xC3\x89"));
	EXPECT_EQ(width("WAVY \xC3\x89T\xC3\x89", UI::TextCase::Lower), Width(inter, 400, NORMAL, 40.0f, "wavy \xC3\xA9t\xC3\xA9"));
	EXPECT_EQ(width("Wavy", UI::TextCase::SmallCaps), Width(inter, 400, NORMAL, 40.0f, "W") + Width(inter, 400, NORMAL, 32.0f, "AVY"));
	EXPECT_EQ(width("WAVY", UI::TextCase::SmallCaps), Width(inter, 400, NORMAL, 40.0f, "WAVY"));
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontStyleFixture, RichSingleRunMatchesPlain) {
	FontAsset* inter = Load("Inter");
	ASSERT_NE(inter, nullptr);
	for (UI::TextOverflow overflow : {UI::TextOverflow::Wrap, UI::TextOverflow::Ellipsis}) {
		for (UI::Align align : {UI::Align::Start, UI::Align::Center}) {
			UI::TextQuery query = Query(inter, 400, NORMAL, 20.0f, "Wavy AVATAR, To-day/Yes\nkeep\n\ntyping Tj");
			query.wrapWidth = 120.0f;
			query.overflow = overflow;
			query.align = align;
			query.letterSpacing = 0.5f;
			query.lineHeight = 30.0f;
			UI::ShapedRun plain;
			ASSERT_TRUE(UISystem::GetFontSystem()->ShapeText(query, plain));

			UI::TextRun runs[] = {RunOf(inter, 0, static_cast<u32>(query.utf8.size()), 20.0f)};
			runs[0].letterSpacing = 0.5f;
			runs[0].lineHeight = 30.0f;
			const UI::ShapedRun styled = ShapeRuns(query, runs);
			EXPECT_EQ(styled.extent.x, plain.extent.x);
			EXPECT_EQ(styled.extent.y, plain.extent.y);
			EXPECT_EQ(styled.lineHeight, plain.lineHeight);
			EXPECT_EQ(styled.halfLeading, plain.halfLeading);
			EXPECT_EQ(styled.multiline, plain.multiline);
			EXPECT_EQ(styled.truncated, plain.truncated);
			ASSERT_EQ(styled.caretStops.size(), plain.caretStops.size());
			for (size_t i = 0; i < plain.caretStops.size(); ++i) {
				EXPECT_EQ(styled.caretStops[i].byteOffset, plain.caretStops[i].byteOffset) << i;
				EXPECT_EQ(styled.caretStops[i].x, plain.caretStops[i].x) << i;
				EXPECT_EQ(styled.caretStops[i].line, plain.caretStops[i].line) << i;
			}
		}
	}
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontStyleFixture, RichFontTagCooksByGuid) {
	UIDocumentAsset* document = LoadText<UIDocumentAsset>(
		"Rich.slxml",
		R"xml(<ui version="1"><panel style="font-size: 100px; font-family: url('project://Assets/Regular.ttf')"><panel id="title">I&lt;font="project://Assets/Bold.ttf"&gt;I&lt;/font&gt;</panel><panel enable-rich-text="false" style="width: 10px">&lt;font="project://Assets/Italic.ttf"&gt;</panel></panel></ui>)xml");
	ASSERT_NE(document, nullptr);
	ASSERT_EQ(document->assets.size(), 2u);
	EXPECT_EQ(document->assets[1].src, "project://Assets/Bold.ttf");
	EXPECT_EQ(RefsOf("Italic"), 0u);
	EXPECT_EQ(TitleWidth(*document), 110.0f);

	PakStringPool strings;
	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Debug)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Debug)};
	CookWriter descriptor(descriptorBytes, strings);
	CookWriter payload(payloadBytes, strings);
	ASSERT_TRUE(document->Cook(descriptor, payload));
	ASSERT_EQ(descriptor.GetReferences().size(), 2u);
	EXPECT_EQ(descriptor.GetReferences()[1], GuidOf("Bold"));

	UIDocumentAsset* cooked = New<UIDocumentAsset>(MemTag::Asset);
	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size());
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size());
	ASSERT_TRUE(cooked->LoadCooked(descriptorReader, payloadReader));
	EXPECT_EQ(TitleWidth(*cooked), 110.0f);
	cooked->NotifyUnloaded();
	Delete(cooked);
	AssetDatabase::UnloadAsset(document);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(RefsOf("Bold"), 0u);
}

TEST_F(FontStyleFixture, RichMissingFontRefusesDocument) {
	EXPECT_EQ(LoadText<UIDocumentAsset>("Typo.slxml", R"(<ui version="1"><panel>&lt;font="project://Assets/Nope.ttf"&gt;x</panel></ui>)"), nullptr);
}

TEST_F(FontInkFixture, RichGlyphsShareBaseline) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::TextRun runs[] = {RunOf(regular, 0, 1, 50.0f), RunOf(regular, 1, 2), RunOf(regular, 2, 3), RunOf(regular, 3, 4), RunOf(regular, 4, 5)};
	runs[2].script = UI::TextScript::Sub;
	runs[3].script = UI::TextScript::Super;
	runs[4].baselineShift = 30.0f;
	const UI::ShapedRun shaped = ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "IIIII"), runs);
	ASSERT_EQ(shaped.glyphs.size(), 5u);
	std::vector<f32> bottoms;
	for (const UI::ShapedGlyph& glyph : shaped.glyphs) {
		bottoms.push_back(glyph.rect.y + glyph.rect.h);
	}
	EXPECT_EQ(bottoms, (std::vector<f32>{80.0f, 80.0f, 95.0f, 45.0f, 50.0f}));
	EXPECT_EQ(shaped.extent.y, 100.0f);
	AssetDatabase::UnloadAsset(regular);
}

TEST_F(FontInkFixture, RichScriptsShiftByFontTables) {
	FontAsset* inter = Load("Inter");
	ASSERT_NE(inter, nullptr);
	UI::TextRun runs[] = {RunOf(inter, 0, 1), RunOf(inter, 1, 2), RunOf(inter, 2, 3)};
	runs[1].script = UI::TextScript::Sub;
	runs[2].script = UI::TextScript::Super;
	const UI::ShapedRun shaped = ShapeRuns(Query(inter, 400, NORMAL, 100.0f, "HHH"), runs);
	ASSERT_EQ(shaped.glyphs.size(), 3u);
	auto bottom = [&](size_t g) { return shaped.glyphs[g].rect.y + shaped.glyphs[g].rect.h; };
	EXPECT_NEAR(bottom(1) - bottom(0), 154.0f / 2048.0f * 100.0f, 0.5f);
	EXPECT_NEAR(bottom(0) - bottom(2), 717.0f / 2048.0f * 100.0f, 0.5f);
	AssetDatabase::UnloadAsset(inter);
}

TEST_F(FontInkFixture, RichCellsCenterGlyphs) {
	FontAsset* regular = Load("Regular");
	ASSERT_NE(regular, nullptr);
	UI::TextRun runs[] = {RunOf(regular, 0, 3)};
	runs[0].advance = 50.0f;
	const UI::ShapedRun shaped = ShapeRuns(Query(regular, 400, NORMAL, 100.0f, "I?I"), runs);
	ASSERT_EQ(shaped.glyphs.size(), 3u);
	EXPECT_EQ(shaped.glyphs[0].rect.x, 10.0f);
	EXPECT_EQ(shaped.glyphs[1].rect.x, 65.0f);
	EXPECT_EQ(shaped.glyphs[2].rect.x, 110.0f);
	AssetDatabase::UnloadAsset(regular);
}
