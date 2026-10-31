#include "TestsPCH.h"

#include "Checkout.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/UI/Font.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/VectorAsset.h"
#include "Shalltear/Core/UI/VectorIcon.h"

#include "Shalltear/Editor/EditorIcons.h"

#include "UI/VectorRasterizer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

using namespace Shalltear;

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

	constexpr u32 UNBAKED_PUA = 0xE007;
	const str UNBAKED_UTF8 = [] {
		str out;
		out.push_back(static_cast<char>(0xE0 | (UNBAKED_PUA >> 12)));
		out.push_back(static_cast<char>(0x80 | ((UNBAKED_PUA >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (UNBAKED_PUA & 0x3F)));
		return out;
	}();
	const EditorIcons::Icon UNBAKED_ICON{UNBAKED_UTF8, EditorIcons::Face::Solid};

	class IconTestBehavior : public Behavior {
	public:
		SLT_REFLECT_COMPONENT(IconTestBehavior, Behavior)
		SLT_END_REFLECT()
	};

	class IconTestComponent : public Component {
	public:
		SLT_REFLECT_COMPONENT(IconTestComponent, Component)
		SLT_END_REFLECT()
	};

	struct Coverage {
		Vec2I size{};
		Vector<u8> alpha{GetMemoryResource(MemTag::Debug)};

		[[nodiscard]] u8 At(i32 x, i32 y) const {
			if (x < 0 || y < 0 || x >= size.x || y >= size.y) {
				return 0;
			}
			return alpha[static_cast<size_t>(y) * static_cast<size_t>(size.x) + static_cast<size_t>(x)];
		}
		[[nodiscard]] u32 Lit() const {
			u32 count = 0;
			for (u8 value : alpha) {
				count += value > 8 ? 1u : 0u;
			}
			return count;
		}
	};

	bool RasterizeGlyph(EditorIcons::Icon icon, f32 sizePx, Coverage& out) {
		Handle<VectorAsset> asset = EditorIcons::GetGlyph(icon);
		if (!asset.IsValid()) {
			return false;
		}
		UI::VectorRasterizer rasterizer;
		Vector<u8> rgba{GetMemoryResource(MemTag::Debug)};
		Vec2I size{};
		if (!rasterizer.Rasterize(*asset.Get(), sizePx, UI::IconPalette::Tint(Color::White()), rgba, size)) {
			return false;
		}
		out.size = size;
		out.alpha.clear();
		out.alpha.reserve(static_cast<size_t>(size.x) * static_cast<size_t>(size.y));
		for (size_t i = 3; i < rgba.size(); i += 4) {
			out.alpha.push_back(rgba[i]);
		}
		return true;
	}

	bool RasterizeWithRoom(EditorIcons::Icon icon, f32 sizePx, i32 marginPx, Coverage& out) {
		Handle<VectorAsset> baked = EditorIcons::GetGlyph(icon);
		if (!baked.IsValid()) {
			return false;
		}
		const VectorAsset& source = *baked.Get();
		const f32 longest = std::max(source.viewSize.x, source.viewSize.y);
		const f32 scale = sizePx / longest;
		const f32 marginUnits = static_cast<f32>(marginPx) / scale;

		VectorAsset* grown = New<VectorAsset>(MemTag::Debug);
		grown->viewSize = {source.viewSize.x + marginUnits * 2.0f, source.viewSize.y + marginUnits * 2.0f};
		grown->verbs = source.verbs;
		grown->slotDefaults = source.slotDefaults;
		grown->shapes = source.shapes;
		for (const Vec2& point : source.points) {
			grown->points.push_back(Vec2{point.x + marginUnits, point.y + marginUnits});
		}

		UI::VectorRasterizer rasterizer;
		Vector<u8> rgba{GetMemoryResource(MemTag::Debug)};
		Vec2I size{};
		const bool ok = rasterizer.Rasterize(*grown, sizePx + static_cast<f32>(marginPx) * 2.0f,
											 UI::IconPalette::Tint(Color::White()), rgba, size);
		Delete(grown);
		if (!ok) {
			return false;
		}
		out.size = size;
		out.alpha.clear();
		out.alpha.reserve(static_cast<size_t>(size.x) * static_cast<size_t>(size.y));
		for (size_t i = 3; i < rgba.size(); i += 4) {
			out.alpha.push_back(rgba[i]);
		}
		return true;
	}

	str UTF8(u32 codepoint) {
		str out;
		if (codepoint < 0x800) {
			out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
		} else {
			out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
		}
		out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		return out;
	}

	struct TableEntry {
		u32 codepoint = 0;
		EditorIcons::Face face = EditorIcons::Face::Solid;

		[[nodiscard]] EditorIcons::Icon AsIcon(str& storage) const {
			storage = UTF8(codepoint);
			return EditorIcons::Icon{storage, face};
		}
	};

	Vector<TableEntry> ScrapeHeaderTable() {
		Vector<TableEntry> out{GetMemoryResource(MemTag::Core)};
		std::filesystem::path header = RepoDir("Source/Editor/Public/Shalltear/Editor/EditorIcons.h");
		if (header.empty()) {
			return out;
		}
		std::ifstream file(header);
		str line;
		while (std::getline(file, line)) {
			if (line.find("constexpr Icon") == str::npos) {
				continue;
			}
			size_t at = line.find("\"\\u");
			if (at == str::npos || at + 7 > line.size()) {
				continue;
			}
			TableEntry entry;
			entry.codepoint = static_cast<u32>(std::stoul(line.substr(at + 3, 4), nullptr, 16));
			entry.face = line.find("Face::Regular") != str::npos ? EditorIcons::Face::Regular
																 : EditorIcons::Face::Solid;
			out.push_back(entry);
		}
		return out;
	}

	class EditorIconsFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (engineAssets.empty()) {
				GTEST_SKIP() << "EngineAssets not found";
			}
			FileSystem::Mount("engine", Path(engineAssets.generic_string()));
			AssetDatabase::Initialize();
			UISystem::Initialize();
			EditorIcons::Shutdown();
			ASSERT_TRUE(EditorIcons::IsAvailable(EditorIcons::Face::Solid));
			ASSERT_TRUE(EditorIcons::IsAvailable(EditorIcons::Face::Regular));
		}

		void TearDown() override {
			EditorIcons::Shutdown();
			UISystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("engine");
		}
	};

} // anonymous namespace

TEST_F(EditorIconsFixture, EveryTableEntryIsBaked) {
	Vector<TableEntry> table = ScrapeHeaderTable();
	ASSERT_FALSE(table.empty());

	u32 regularSeen = 0;
	for (const TableEntry& entry : table) {
		regularSeen += entry.face == EditorIcons::Face::Regular ? 1u : 0u;
	}
	ASSERT_GT(regularSeen, 0u);
	ASSERT_LT(regularSeen, table.size());

	ASSERT_FALSE(EditorIcons::GetGlyph(UNBAKED_ICON).IsValid());

	HashSet<u64> seen{GetMemoryResource(MemTag::Core)};
	for (const TableEntry& entry : table) {
		const bool isRegular = entry.face == EditorIcons::Face::Regular;
		SCOPED_TRACE(std::format("U+{:04X} in {}", entry.codepoint, isRegular ? "Regular" : "Solid"));
		EXPECT_GE(entry.codepoint, 0xE000u);
		EXPECT_LE(entry.codepoint, 0xF8FFu);
		u64 key = (static_cast<u64>(entry.face) << 32) | entry.codepoint;
		EXPECT_TRUE(seen.insert(key).second);

		str storage;
		Handle<VectorAsset> glyph = EditorIcons::GetGlyph(entry.AsIcon(storage));
		ASSERT_TRUE(glyph.IsValid());
		const VectorAsset* image = glyph.Get();
		ASSERT_NE(image, nullptr);
		EXPECT_FALSE(image->IsEmpty());
		EXPECT_EQ(image->shapes.size(), 1u);
		EXPECT_EQ(image->slotDefaults.size(), 1u);
		EXPECT_GT(image->viewSize.x, 0.0f);
		EXPECT_GT(image->viewSize.y, 0.0f);
		EXPECT_GT(EditorIcons::GlyphSizeScale(entry.AsIcon(storage)), 1.0f);
	}
}

TEST_F(EditorIconsFixture, NoBakedGlyphLosesInkToBox) {
	Vector<TableEntry> table = ScrapeHeaderTable();
	ASSERT_FALSE(table.empty());

	constexpr f32 SIZE_PX = 64.0f;
	constexpr i32 MARGIN_PX = 6;
	for (const TableEntry& entry : table) {
		SCOPED_TRACE(std::format("U+{:04X} in {}", entry.codepoint,
								 entry.face == EditorIcons::Face::Regular ? "Regular" : "Solid"));
		str storage;
		const EditorIcons::Icon icon = entry.AsIcon(storage);

		Coverage baked;
		ASSERT_TRUE(RasterizeGlyph(icon, SIZE_PX, baked));
		ASSERT_GT(baked.size.x, 0);
		ASSERT_GT(baked.size.y, 0);
		EXPECT_GT(baked.Lit(), 0u);

		Coverage roomy;
		ASSERT_TRUE(RasterizeWithRoom(icon, SIZE_PX, MARGIN_PX, roomy));

		u32 spilled = 0;
		for (i32 y = 0; y < roomy.size.y; ++y) {
			for (i32 x = 0; x < roomy.size.x; ++x) {
				const bool insideBakedFootprint = x >= MARGIN_PX && y >= MARGIN_PX &&
												  x < MARGIN_PX + baked.size.x && y < MARGIN_PX + baked.size.y;
				if (!insideBakedFootprint && roomy.At(x, y) > 8) {
					++spilled;
				}
			}
		}
		EXPECT_EQ(spilled, 0u);
	}
}

TEST_F(EditorIconsFixture, ClipCheckSeesLostInk) {
	Coverage honest;
	ASSERT_TRUE(RasterizeGlyph(EditorIcons::Glyph::AssetMesh, 64.0f, honest));
	ASSERT_GT(honest.Lit(), 0u);

	Coverage cramped;
	ASSERT_TRUE(RasterizeWithRoom(EditorIcons::Glyph::AssetMesh, 64.0f, -8, cramped));
	EXPECT_LT(cramped.size.x, honest.size.x);
	EXPECT_LT(cramped.size.y, honest.size.y);
	EXPECT_LT(cramped.Lit(), honest.Lit());
}

TEST_F(EditorIconsFixture, ABakedCounterIsStillAHole) {
	constexpr f32 SIZE_PX = 64.0f;
	for (EditorIcons::Icon icon : {EditorIcons::Glyph::LevelInfo, EditorIcons::Glyph::LevelError,
								   EditorIcons::Glyph::AssetPrefab, EditorIcons::Glyph::PanelPackages}) {
		Coverage coverage;
		ASSERT_TRUE(RasterizeGlyph(icon, SIZE_PX, coverage));

		i32 x0 = coverage.size.x, y0 = coverage.size.y, x1 = -1, y1 = -1;
		for (i32 y = 0; y < coverage.size.y; ++y) {
			for (i32 x = 0; x < coverage.size.x; ++x) {
				if (coverage.At(x, y) > 8) {
					x0 = std::min(x0, x);
					y0 = std::min(y0, y);
					x1 = std::max(x1, x);
					y1 = std::max(y1, y);
				}
			}
		}
		ASSERT_GE(x1, x0);
		u32 holes = 0;
		for (i32 y = y0 + 1; y < y1; ++y) {
			for (i32 x = x0 + 1; x < x1; ++x) {
				holes += coverage.At(x, y) == 0 ? 1u : 0u;
			}
		}
		EXPECT_GT(holes, 0u);
	}
}

TEST_F(EditorIconsFixture, SolidGlyphHasNoHole) {
	Coverage coverage;
	ASSERT_TRUE(RasterizeGlyph(EditorIcons::Glyph::Remove, 64.0f, coverage));
	i32 x0 = coverage.size.x, y0 = coverage.size.y, x1 = -1, y1 = -1;
	for (i32 y = 0; y < coverage.size.y; ++y) {
		for (i32 x = 0; x < coverage.size.x; ++x) {
			if (coverage.At(x, y) > 8) {
				x0 = std::min(x0, x);
				y0 = std::min(y0, y);
				x1 = std::max(x1, x);
				y1 = std::max(y1, y);
			}
		}
	}
	ASSERT_GT(x1, x0);
	u32 holes = 0;
	for (i32 y = y0 + 1; y < y1; ++y) {
		for (i32 x = x0 + 1; x < x1; ++x) {
			holes += coverage.At(x, y) == 0 ? 1u : 0u;
		}
	}
	EXPECT_EQ(holes, 0u);
}

TEST_F(EditorIconsFixture, CodepointBakesPerFace) {
	ASSERT_EQ(EditorIcons::Glyph::AssetMesh.glyph, EditorIcons::Glyph::SceneObject.glyph);

	Handle<VectorAsset> filled = EditorIcons::GetGlyph(EditorIcons::Glyph::AssetMesh);
	Handle<VectorAsset> outline = EditorIcons::GetGlyph(EditorIcons::Glyph::SceneObject);
	ASSERT_TRUE(filled.IsValid());
	ASSERT_TRUE(outline.IsValid());
	EXPECT_NE(filled.Get(), outline.Get());
	EXPECT_NE(filled.Get()->points.size(), outline.Get()->points.size());

	Coverage solid;
	Coverage regular;
	ASSERT_TRUE(RasterizeGlyph(EditorIcons::Glyph::AssetMesh, 64.0f, solid));
	ASSERT_TRUE(RasterizeGlyph(EditorIcons::Glyph::SceneObject, 64.0f, regular));
	EXPECT_LT(regular.Lit(), solid.Lit());
}

TEST_F(EditorIconsFixture, ScrapedTableMatchesTheCompiledConstants) {
	EXPECT_EQ(EditorIcons::Glyph::Play.glyph, UTF8(0xF04B));
	EXPECT_EQ(EditorIcons::Glyph::Stop.glyph, UTF8(0xF04D));
	EXPECT_EQ(EditorIcons::Glyph::AssetMesh.glyph, UTF8(0xF1B2));
	EXPECT_EQ(EditorIcons::Glyph::LevelWarn.glyph, UTF8(0xF071));

	EXPECT_EQ(EditorIcons::Glyph::SceneObject.glyph, UTF8(0xF1B2));
	EXPECT_EQ(EditorIcons::Glyph::SceneObject.face, EditorIcons::Face::Regular);
	EXPECT_EQ(EditorIcons::Glyph::AssetMesh.face, EditorIcons::Face::Solid);

	Vector<TableEntry> table = ScrapeHeaderTable();
	auto has = [&table](u32 codepoint, EditorIcons::Face face) {
		return std::any_of(table.begin(), table.end(), [&](const TableEntry& entry) {
			return entry.codepoint == codepoint && entry.face == face;
		});
	};
	EXPECT_TRUE(has(0xF04Bu, EditorIcons::Face::Solid));
	EXPECT_TRUE(has(0xF071u, EditorIcons::Face::Solid));
	EXPECT_TRUE(has(0xF1B2u, EditorIcons::Face::Solid));
	EXPECT_TRUE(has(0xF1B2u, EditorIcons::Face::Regular));
}

namespace {

	enum class Provenance : u8 {
		Refused,
		Undeclared,
		FontAwesomeFree,
		Original,
	};

	Provenance ProvenanceOf(std::string_view svg) {
		str notes;
		for (size_t open = svg.find("<!--"); open != std::string_view::npos; open = svg.find("<!--", open + 4)) {
			const size_t close = svg.find("-->", open + 4);
			if (close == std::string_view::npos) {
				break;
			}
			notes.append(svg.substr(open + 4, close - open - 4));
			notes.push_back(' ');
			open = close;
		}
		str folded = notes;
		std::transform(folded.begin(), folded.end(), folded.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
		if (folded.find("font awesome pro") != str::npos || folded.find("commercial license") != str::npos) {
			return Provenance::Refused;
		}
		if (folded.find("font awesome free") != str::npos && folded.find("cc by 4.0") != str::npos) {
			return Provenance::FontAwesomeFree;
		}
		if (notes.find("Original artwork drawn for Shalltear") != str::npos) {
			return Provenance::Original;
		}
		return Provenance::Undeclared;
	}

	Vector<str> ScrapeTableNames(const std::filesystem::path& header) {
		Vector<str> out{GetMemoryResource(MemTag::Core)};
		std::ifstream file(header);
		str line;
		while (std::getline(file, line)) {
			const size_t at = line.find("constexpr Icon ");
			if (at == str::npos) {
				continue;
			}
			const size_t nameStart = at + std::strlen("constexpr Icon ");
			const size_t brace = line.find("{\"", nameStart);
			if (brace == str::npos || brace + 2 >= line.size() || line[brace + 2] == '"') {
				continue;
			}
			out.push_back(line.substr(nameStart, brace - nameStart));
		}
		return out;
	}

} // anonymous namespace

TEST(EditorIconSourcesTest, SourcesAreFreeOrOriginal) {
	EXPECT_EQ(ProvenanceOf("<svg><!--! Font Awesome Pro 7.3.1 by @fontawesome - https://fontawesome.com "
						   "License - https://fontawesome.com/license (Commercial License) --><path d=\"M0 0\"/></svg>"),
			  Provenance::Refused);
	EXPECT_EQ(ProvenanceOf("<svg><path d=\"M0 0\"/></svg>"), Provenance::Undeclared);

	const std::filesystem::path root = Checkout::Root();
	ASSERT_FALSE(root.empty()) << std::filesystem::current_path().generic_string();
	const std::filesystem::path sources = root / "Scripts" / "IconBake" / "Icons";

	const Vector<str> names = ScrapeTableNames(root / "Source" / "Editor" / "Public" / "Shalltear" / "Editor" / "EditorIcons.h");
	ASSERT_GT(names.size(), 50u);

	u32 free = 0;
	u32 original = 0;
	for (const str& name : names) {
		SCOPED_TRACE(name);
		std::ifstream file(sources / (name + ".svg"), std::ios::binary);
		ASSERT_TRUE(file);
		const str text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		switch (ProvenanceOf(text)) {
		case Provenance::FontAwesomeFree:
			++free;
			break;
		case Provenance::Original:
			++original;
			break;
		case Provenance::Refused:
			ADD_FAILURE();
			break;
		case Provenance::Undeclared:
			ADD_FAILURE();
			break;
		}
	}
	EXPECT_GT(free, 0u);
	EXPECT_GT(original, 0u);

	std::error_code ec;
	u32 files = 0;
	for (const auto& entry : std::filesystem::directory_iterator(sources, ec)) {
		if (entry.path().extension() != ".svg") {
			continue;
		}
		++files;
		const str stem = entry.path().stem().generic_string();
		EXPECT_NE(std::find(names.begin(), names.end(), stem), names.end()) << stem;
	}
	ASSERT_FALSE(ec) << sources.generic_string();
	EXPECT_EQ(files, names.size());
}

TEST_F(EditorIconsFixture, ShutdownDestroysGlyphsReaskRebuilds) {
	Handle<VectorAsset> first = EditorIcons::GetGlyph(EditorIcons::Glyph::Play);
	ASSERT_TRUE(first.IsValid());

	EditorIcons::Shutdown();
	EXPECT_FALSE(first.IsValid());

	EditorIcons::Shutdown();
	Handle<VectorAsset> again = EditorIcons::GetGlyph(EditorIcons::Glyph::Play);
	EXPECT_TRUE(again.IsValid());
	EXPECT_NE(again.GetInstanceId(), first.GetInstanceId());
}

TEST_F(EditorIconsFixture, ShutdownDestroysEveryFacesGlyphs) {
	Handle<VectorAsset> solid = EditorIcons::GetGlyph(EditorIcons::Glyph::AssetMesh);
	Handle<VectorAsset> regular = EditorIcons::GetGlyph(EditorIcons::Glyph::SceneObject);
	ASSERT_TRUE(solid.IsValid());
	ASSERT_TRUE(regular.IsValid());

	EditorIcons::Shutdown();
	EXPECT_FALSE(solid.IsValid());
	EXPECT_FALSE(regular.IsValid());
}

TEST_F(EditorIconsFixture, AGlyphNobodyDrawsIsNeverBuilt) {
	EditorIcons::Shutdown();
	EXPECT_FALSE(EditorIcons::GetGlyph(EditorIcons::Glyph::AlignStretch).IsValid() &&
				 EditorIcons::GetGlyph(EditorIcons::Glyph::AlignStretch).GetInstanceId() == 0);
	Handle<VectorAsset> once = EditorIcons::GetGlyph(EditorIcons::Glyph::AlignStretch);
	Handle<VectorAsset> twice = EditorIcons::GetGlyph(EditorIcons::Glyph::AlignStretch);
	ASSERT_TRUE(once.IsValid());
	EXPECT_EQ(once.Get(), twice.Get());
}

TEST_F(EditorIconsFixture, EachFaceIsAnsweredSeparately) {
	EXPECT_TRUE(EditorIcons::IsAvailable(EditorIcons::Face::Solid));
	EXPECT_TRUE(EditorIcons::IsAvailable(EditorIcons::Face::Regular));
	EXPECT_NE(EditorIcons::GetGlyph(EditorIcons::Glyph::AssetMesh).Get(),
			  EditorIcons::GetGlyph(EditorIcons::Glyph::SceneObject).Get());
}

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
	}

	UI::ElementHandle CenteredRow(UI::Context& context) {
		UI::ElementHandle row = context.CreateElement();
		UI::Style style;
		style.flexDirection = UI::FlexDirection::Row;
		style.alignItems = UI::Align::Center;
		context.SetStyle(row, style);
		context.AppendChild(context.GetRoot(), row);
		return row;
	}

} // anonymous namespace

TEST_F(EditorIconsFixture, ElementDrawsFaceIconNamed) {
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};
	ASSERT_EQ(EditorIcons::Glyph::AssetMesh.glyph, EditorIcons::Glyph::SceneObject.glyph);

	UI::ElementHandle filled = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), filled);
	EditorIcons::SetGlyph(ctx, filled, EditorIcons::Glyph::AssetMesh, Color(1, 1, 1, 1));

	UI::ElementHandle outline = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), outline);
	EditorIcons::SetGlyph(ctx, outline, EditorIcons::Glyph::SceneObject, Color(1, 1, 1, 1));

	ASSERT_TRUE(ctx.HasIcon(filled));
	ASSERT_TRUE(ctx.HasIcon(outline));
	ctx.Update();
	EXPECT_NE(ctx.GetLayout(filled).borderBox.w, 0.0f);
	EXPECT_NE(EditorIcons::GetGlyph(EditorIcons::Glyph::AssetMesh).Get(),
			  EditorIcons::GetGlyph(EditorIcons::Glyph::SceneObject).Get());
}

TEST_F(EditorIconsFixture, SettingGlyphTakesElementsTextWithIt) {
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	ctx.SetText(element, UISystem::GetDefaultFont(), 13.0f, Color(1, 1, 1, 1), "v");
	ASSERT_EQ(ctx.GetTextString(element), "v");

	EditorIcons::SetGlyph(ctx, element, EditorIcons::Glyph::CaretDown, Color(1, 1, 1, 1));
	EXPECT_TRUE(ctx.HasIcon(element));
	EXPECT_TRUE(ctx.GetTextString(element).empty());

	EditorIcons::SetGlyph(ctx, element, EditorIcons::Icon{}, Color(1, 1, 1, 1));
	EXPECT_FALSE(ctx.HasIcon(element));
	EXPECT_TRUE(ctx.GetTextString(element).empty());
}

TEST_F(EditorIconsFixture, PrefixPutsIconBeforeWidgetsLabel) {
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};

	UI::Widgets::ButtonDesc desc;
	desc.label = "Compile";
	desc.font = UISystem::GetDefaultFont();
	UI::ElementHandle button = UI::Widgets::Button(ctx, ctx.GetRoot(), desc);
	UI::ElementHandle label = ctx.MakeHandle(ctx.GetElement(button)->firstChild);

	UI::ElementHandle icon = EditorIcons::Prefix(ctx, button, EditorIcons::Glyph::Compile, Color(1, 1, 1, 1));

	EXPECT_EQ(ctx.GetElement(button)->firstChild, icon.index);
	const UI::Element* iconPod = ctx.GetElement(icon);
	ASSERT_NE(iconPod, nullptr);
	EXPECT_EQ(iconPod->nextSibling, label.index);
	EXPECT_EQ(iconPod->flags & UI::ElementFlags::Hoverable, UI::ElementFlags::None);
}

TEST_F(EditorIconsFixture, GutterSeparatesCaretAndPicture) {
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};

	UI::Widgets::TreeRowDesc branchDesc;
	branchDesc.hasChildren = true;
	branchDesc.expanded = true;
	branchDesc.label = "Materials";
	branchDesc.font = UISystem::GetDefaultFont();
	UI::Widgets::TreeRowParts branch = UI::Widgets::TreeRow(ctx, ctx.GetRoot(), branchDesc);
	UI::ElementHandle branchIcon =
		EditorIcons::SetRowGutter(ctx, branch, true, true, Color(1, 1, 1, 1),
								  EditorIcons::Glyph::Folder, Color(1, 1, 1, 1));

	UI::Widgets::TreeRowDesc leafDesc;
	leafDesc.hasChildren = false;
	leafDesc.label = "Rock";
	leafDesc.font = UISystem::GetDefaultFont();
	UI::Widgets::TreeRowParts leaf = UI::Widgets::TreeRow(ctx, ctx.GetRoot(), leafDesc);
	UI::ElementHandle leafIcon =
		EditorIcons::SetRowGutter(ctx, leaf, false, false, Color(1, 1, 1, 1),
								  EditorIcons::Glyph::Folder, Color(1, 1, 1, 1));

	EXPECT_TRUE(ctx.HasIcon(branch.arrow));
	EXPECT_FALSE(ctx.HasIcon(leaf.arrow));
	EXPECT_TRUE(ctx.GetTextString(leaf.arrow).empty());

	ASSERT_TRUE(ctx.IsAlive(branchIcon));
	ASSERT_TRUE(ctx.IsAlive(leafIcon));
	EXPECT_TRUE(ctx.HasIcon(branchIcon));
	EXPECT_TRUE(ctx.HasIcon(leafIcon));

	const auto slots = [&ctx](const UI::Widgets::TreeRowParts& row) {
		Vector<u32> out{GetMemoryResource(MemTag::Debug)};
		const UI::Element* pod = ctx.GetElement(row.row);
		for (u32 child = pod ? pod->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			out.push_back(child);
			child = ctx.GetElement(ctx.MakeHandle(child))->nextSibling;
		}
		return out;
	};
	Vector<u32> branchSlots = slots(branch);
	Vector<u32> leafSlots = slots(leaf);
	ASSERT_EQ(branchSlots.size(), 3u);
	ASSERT_EQ(leafSlots.size(), 3u);
	EXPECT_EQ(branchSlots[0], branch.arrow.index);
	EXPECT_EQ(branchSlots[1], branchIcon.index);
	EXPECT_EQ(branchSlots[2], branch.labelElement.index);
	EXPECT_EQ(leafSlots[0], leaf.arrow.index);
	EXPECT_EQ(leafSlots[1], leafIcon.index);
	EXPECT_EQ(leafSlots[2], leaf.labelElement.index);

	EXPECT_FLOAT_EQ(ctx.GetStyle(branch.arrow).margin.right, EditorIcons::GutterGapPx());
	EXPECT_FLOAT_EQ(ctx.GetStyle(leaf.arrow).margin.right, EditorIcons::GutterGapPx());
	EXPECT_FLOAT_EQ(ctx.GetStyle(branchIcon).margin.right, EditorIcons::GutterGapPx());
	EXPECT_FLOAT_EQ(ctx.GetStyle(leafIcon).margin.right, EditorIcons::GutterGapPx());
}

TEST_F(EditorIconsFixture, PicturelessGutterKeepsCaret) {
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};

	UI::Widgets::TreeRowDesc desc;
	desc.hasChildren = false;
	desc.label = "Rock";
	desc.font = UISystem::GetDefaultFont();
	UI::Widgets::TreeRowParts row = UI::Widgets::TreeRow(ctx, ctx.GetRoot(), desc);

	EXPECT_FALSE(ctx.IsAlive(EditorIcons::SetRowGutter(ctx, row, false, false, Color(1, 1, 1, 1))));
	EXPECT_FLOAT_EQ(ctx.GetStyle(row.arrow).margin.right, EditorIcons::GutterGapPx());
	const UI::Element* pod = ctx.GetElement(row.row);
	ASSERT_NE(pod, nullptr);
	EXPECT_EQ(pod->firstChild, row.arrow.index);
	EXPECT_EQ(ctx.GetElement(row.arrow)->nextSibling, row.labelElement.index);
}

TEST_F(EditorIconsFixture, IconMatchesTextHeight) {
	constexpr f32 TEXT_PX = 13.0f;
	f32 lineBox = EditorIcons::LineBoxHeight(TEXT_PX);
	ASSERT_GT(lineBox, TEXT_PX);

	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};
	UI::ElementHandle icon = EditorIcons::Add(ctx, CenteredRow(ctx), EditorIcons::Glyph::Play,
											  Color(1, 1, 1, 1), TEXT_PX, 0.0f, TEXT_PX);
	ctx.Update();
	EXPECT_FLOAT_EQ(ctx.GetLayout(icon).borderBox.h, lineBox);

	EXPECT_EQ(ctx.GetStyle(icon).textAlignVertical, UI::Align::Center);
}

TEST_F(EditorIconsFixture, MarkTallerThanLineBoxKeepsHeight) {
	constexpr f32 TEXT_PX = 11.5f;
	constexpr f32 MARK_PX = 18.0f;
	const f32 lineBox = EditorIcons::LineBoxHeight(TEXT_PX);
	ASSERT_LT(lineBox, MARK_PX);

	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};
	UI::ElementHandle mark = EditorIcons::AddVector(ctx, CenteredRow(ctx), EditorIcons::Mark::Logo,
													UI::IconPalette{}, MARK_PX, 0.0f, TEXT_PX);
	ctx.Update();
	ASSERT_TRUE(ctx.GetIconAsset(mark).IsValid());
	EXPECT_FLOAT_EQ(ctx.GetLayout(mark).borderBox.h, MARK_PX);
}

TEST_F(EditorIconsFixture, AnIconIsNeverAFlexDonor) {
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};

	UI::ElementHandle host = ctx.CreateElement();
	UI::Style hostStyle;
	hostStyle.width = UI::Length::Pixel(90.0f);
	ctx.SetStyle(host, hostStyle);
	ctx.AppendChild(ctx.GetRoot(), host);

	UI::ElementHandle icon = EditorIcons::Add(ctx, host, EditorIcons::Glyph::AssetMesh,
											  Color(1, 1, 1, 1), 12.0f, 4.0f, 13.0f);
	UI::ElementHandle label = ctx.CreateElement();
	ctx.SetText(label, UISystem::GetDefaultFont(), 13.0f, Color(1, 1, 1, 1),
				"a name far wider than ninety pixels");
	ctx.AppendChild(host, label);
	ctx.Update();

	f32 unpressured = ctx.GetLayout(icon).borderBox.w;
	ASSERT_GT(unpressured, 0.0f);

	UI::ElementHandle solo = EditorIcons::Add(ctx, ctx.GetRoot(), EditorIcons::Glyph::AssetMesh,
											  Color(1, 1, 1, 1), 12.0f, 4.0f, 13.0f);
	ctx.Update();
	EXPECT_FLOAT_EQ(unpressured, ctx.GetLayout(solo).borderBox.w);
}

TEST_F(EditorIconsFixture, IconButtonShowsGlyphTakesClick) {
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};

	u32 clicks = 0;
	EditorIcons::IconButtonParts parts = EditorIcons::IconButton(
		ctx, ctx.GetRoot(), EditorIcons::Glyph::Duplicate, "Duplicate",
		"Copy the selected profile (including its overrides)", [&clicks]() { ++clicks; }, 13.0f);

	ASSERT_TRUE(ctx.IsAlive(parts.button));
	ASSERT_TRUE(ctx.IsAlive(parts.face));
	EXPECT_TRUE(ctx.HasIcon(parts.face));
	EXPECT_TRUE(ctx.GetTextString(parts.face).empty());

	ctx.Update();
	UITest::ClickElement(ctx, parts.button);
	EXPECT_EQ(clicks, 1u);
}

TEST_F(EditorIconsFixture, UnbakedIconButtonFallsBack) {
	ASSERT_FALSE(EditorIcons::GetGlyph(UNBAKED_ICON).IsValid());

	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};

	u32 clicks = 0;
	EditorIcons::IconButtonParts parts = EditorIcons::IconButton(
		ctx, ctx.GetRoot(), UNBAKED_ICON, "Duplicate",
		"Copy the selected profile (including its overrides)", [&clicks]() { ++clicks; }, 13.0f);

	EXPECT_EQ(ctx.GetTextString(parts.face), "Duplicate");
	EXPECT_FALSE(ctx.HasIcon(parts.face));

	ctx.Update();
	UITest::ClickElement(ctx, parts.button);
	EXPECT_EQ(clicks, 1u);
}

TEST_F(EditorIconsFixture, ComponentMarksImportWithinPalette) {
	Vector<str> marks{GetMemoryResource(MemTag::Editor)};
	for (std::string_view named : {EditorIcons::Mark::Logo, EditorIcons::Mark::Transform,
								   EditorIcons::Mark::Camera, EditorIcons::Mark::Light,
								   EditorIcons::Mark::MeshRenderer, EditorIcons::Mark::SkinnedMeshRenderer,
								   EditorIcons::Mark::Skeleton, EditorIcons::Mark::Environment,
								   EditorIcons::Mark::ReflectionProbe, EditorIcons::Mark::UIPanelRenderer,
								   EditorIcons::Mark::ProceduralRenderer, EditorIcons::Mark::Behavior, EditorIcons::Mark::Component,
								   EditorIcons::Mark::MissingComponent}) {
		marks.push_back(str(named));
	}

	std::filesystem::path directory(FileSystem::Resolve(Path("engine://Icons/Components")).ToString());
	std::error_code ec;
	u32 fromDisk = 0;
	for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
		if (entry.path().extension() != ".svg") {
			continue;
		}
		str path = "engine://Icons/Components/" + entry.path().filename().generic_string();
		++fromDisk;
		if (std::find(marks.begin(), marks.end(), path) == marks.end()) {
			marks.push_back(std::move(path));
		}
	}
	ASSERT_FALSE(ec) << directory.generic_string();
	ASSERT_GT(fromDisk, 10u) << "only " << fromDisk << ' ' << directory.generic_string();

	for (const str& path : marks) {
		Handle<VectorAsset> asset = EditorIcons::GetVector(path);
		ASSERT_TRUE(asset.IsValid()) << path;
		const VectorAsset* mark = asset.Get();
		ASSERT_NE(mark, nullptr) << path;
		EXPECT_FALSE(mark->IsEmpty()) << path;
		EXPECT_GT(mark->viewSize.x, 0.0f) << path;
		EXPECT_LE(mark->slotDefaults.size(), UI::MAX_ICON_SLOTS) << path;
		EXPECT_GE(mark->slotDefaults.size(), 1u) << path;
	}
}

TEST_F(EditorIconsFixture, ShippedMarksAreWellFormed) {
	std::filesystem::path directory(FileSystem::Resolve(Path("engine://Icons")).ToString());
	std::error_code ec;
	u32 scanned = 0;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec)) {
		if (entry.path().extension() != ".svg") {
			continue;
		}
		std::ifstream file(entry.path(), std::ios::binary);
		ASSERT_TRUE(file) << entry.path().generic_string();
		const str text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		++scanned;

		for (size_t open = text.find("<!--"); open != str::npos; open = text.find("<!--", open + 4)) {
			const size_t bodyStart = open + 4;
			const size_t close = text.find("-->", bodyStart);
			ASSERT_NE(close, str::npos) << entry.path().filename().generic_string();
			EXPECT_EQ(text.find("--", bodyStart), close);
			open = close;
		}
	}
	ASSERT_FALSE(ec) << directory.generic_string();
	ASSERT_GT(scanned, 10u) << "only " << scanned;
}

TEST_F(EditorIconsFixture, UnregisteredComponentWearsNearestRegisteredAncestor) {
	EditorIcons::ComponentIcons::RegisterBuiltIns();

	EXPECT_EQ(EditorIcons::ComponentIcons::For(Camera::GetReflectType()), EditorIcons::Mark::Camera);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(Light::GetReflectType()), EditorIcons::Mark::Light);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(IconTestBehavior::GetReflectType()), EditorIcons::Mark::Behavior);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(IconTestComponent::GetReflectType()), EditorIcons::Mark::Component);

	EXPECT_TRUE(EditorIcons::ComponentIcons::For(SceneObject::GetReflectType()).empty());
	EXPECT_TRUE(EditorIcons::ComponentIcons::For(nullptr).empty());
}

TEST_F(EditorIconsFixture, ModuleBadgeOverrideSwept) {
	EditorIcons::ComponentIcons::RegisterBuiltIns();
	int moduleA = 1;
	int moduleB = 2;

	EditorIcons::ComponentIcons::Register(Camera::GetReflectType()->name, "engine://Icons/Logo.svg", &moduleA);
	EditorIcons::ComponentIcons::Register(Light::GetReflectType()->name, "engine://Icons/Logo.svg", &moduleB);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(Camera::GetReflectType()), EditorIcons::Mark::Logo);

	EXPECT_EQ(EditorIcons::ComponentIcons::UnregisterOwnedBy(&moduleA), 1u);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(Camera::GetReflectType()), EditorIcons::Mark::Camera);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(Light::GetReflectType()), EditorIcons::Mark::Logo);

	EXPECT_EQ(EditorIcons::ComponentIcons::UnregisterOwnedBy(nullptr), 0u);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(Camera::GetReflectType()), EditorIcons::Mark::Camera);
	EditorIcons::ComponentIcons::UnregisterOwnedBy(&moduleB);
}

TEST_F(EditorIconsFixture, AddIconsComponentsOnly) {
	EditorIcons::ComponentIcons::RegisterBuiltIns();
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};
	UI::ElementHandle row = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), row);

	UI::ElementHandle badge = EditorIcons::ComponentIcons::Add(ctx, row, Camera::GetReflectType());
	ASSERT_TRUE(ctx.IsAlive(badge));
	EXPECT_TRUE(ctx.HasIcon(badge));
	EXPECT_FALSE(HasFlag(ctx.GetElement(badge)->flags, UI::ElementFlags::Hoverable));

	const u32 before = UITest::ChildCountOf(ctx, row);
	EXPECT_FALSE(ctx.IsAlive(EditorIcons::ComponentIcons::Add(ctx, row, SceneObject::GetReflectType())));
	EXPECT_EQ(UITest::ChildCountOf(ctx, row), before);
}

TEST_F(EditorIconsFixture, UnbakedIconFallsBackToWord) {
	ASSERT_FALSE(EditorIcons::GetGlyph(UNBAKED_ICON).IsValid());

	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};

	UI::ElementHandle face = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), face);
	EXPECT_FALSE(EditorIcons::SetGlyphOrText(ctx, face, UNBAKED_ICON, "Play", Color(1, 1, 1, 1)));
	EXPECT_EQ(ctx.GetTextString(face), "Play");
	EXPECT_FALSE(ctx.HasIcon(face));

	UI::Widgets::TreeRowDesc branch;
	branch.hasChildren = true;
	branch.expanded = false;
	branch.label = "Materials";
	branch.font = UISystem::GetDefaultFont();
	UI::Widgets::TreeRowParts row = UI::Widgets::TreeRow(ctx, ctx.GetRoot(), branch);
	EditorIcons::SetRowGutter(ctx, row, true, false, Color(1, 1, 1, 1), UNBAKED_ICON);
	EXPECT_TRUE(ctx.HasIcon(row.arrow));

	const UI::Element* pod = ctx.GetElement(row.row);
	ASSERT_NE(pod, nullptr);
	EXPECT_EQ(pod->firstChild, row.arrow.index);
}

TEST_F(EditorIconsFixture, IconlessRowKeepsWidgetsDisclosureMark) {
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};
	UI::Widgets::TreeRowDesc branch;
	branch.hasChildren = true;
	branch.expanded = false;
	branch.label = "Materials";
	branch.font = UISystem::GetDefaultFont();
	UI::Widgets::TreeRowParts row = UI::Widgets::TreeRow(ctx, ctx.GetRoot(), branch);
	EXPECT_EQ(ctx.GetTextString(row.arrow), ">");

	EditorIcons::SetRowGutter(ctx, row, true, false, Color(1, 1, 1, 1));
	EXPECT_TRUE(ctx.HasIcon(row.arrow));
	EXPECT_TRUE(ctx.GetTextString(row.arrow).empty());
	EXPECT_FLOAT_EQ(ctx.GetStyle(row.arrow).margin.right, EditorIcons::GutterGapPx());
}

TEST_F(EditorIconsFixture, InstalledMarkIsWhatEveryDropdownDraws) {
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};
	const std::string_view items[] = {"One", "Two"};
	UI::Widgets::DropdownDesc desc;
	desc.items = items;
	desc.font = UISystem::GetDefaultFont();
	desc.fontSizePx = 13.0f;

	UI::Widgets::DropdownParts plain = UI::Widgets::Dropdown(ctx, ctx.GetRoot(), desc);
	UI::ElementHandle plainMark = UITest::FindByClass(ctx, plain.root, StringHash("slt-dropdown-mark"), 0);
	ASSERT_TRUE(ctx.IsAlive(plainMark));
	const str fallback = ctx.GetTextString(plainMark);
	ASSERT_FALSE(fallback.empty());
	ASSERT_FALSE(ctx.HasIcon(plainMark));

	EditorIcons::InstallWidgetMarks();
	UI::Widgets::DropdownParts dressed = UI::Widgets::Dropdown(ctx, ctx.GetRoot(), desc);
	UI::ElementHandle mark = UITest::FindByClass(ctx, dressed.root, StringHash("slt-dropdown-mark"), 0);
	ASSERT_TRUE(ctx.IsAlive(mark));
	EXPECT_TRUE(ctx.HasIcon(mark));
	EXPECT_TRUE(ctx.GetTextString(mark).empty());

	UI::Widgets::MaskDropdownDesc maskDesc;
	maskDesc.items = items;
	maskDesc.font = desc.font;
	maskDesc.fontSizePx = desc.fontSizePx;
	UI::Widgets::DropdownParts masked = UI::Widgets::MaskDropdown(ctx, ctx.GetRoot(), maskDesc);
	UI::ElementHandle maskMark = UITest::FindByClass(ctx, masked.root, StringHash("slt-dropdown-mark"), 0);
	ASSERT_TRUE(ctx.IsAlive(maskMark));
	EXPECT_TRUE(ctx.HasIcon(maskMark));

	EditorIcons::Shutdown();
	UI::Widgets::DropdownParts after = UI::Widgets::Dropdown(ctx, ctx.GetRoot(), desc);
	UI::ElementHandle afterMark = UITest::FindByClass(ctx, after.root, StringHash("slt-dropdown-mark"), 0);
	ASSERT_TRUE(ctx.IsAlive(afterMark));
	EXPECT_FALSE(ctx.HasIcon(afterMark));
	EXPECT_EQ(ctx.GetTextString(afterMark), fallback);
}

TEST_F(EditorIconsFixture, UnbakedCaretKeepsCharacter) {
	UI::Context ctx{Desc(), UISystem::GetFontSystem(), UISystem::GetVectorIconSystem()};
	const std::string_view items[] = {"One", "Two"};
	UI::Widgets::DropdownDesc desc;
	desc.items = items;
	desc.font = UISystem::GetDefaultFont();
	desc.fontSizePx = 13.0f;
	UI::Widgets::DropdownParts plain = UI::Widgets::Dropdown(ctx, ctx.GetRoot(), desc);
	const str fallback = ctx.GetTextString(UITest::FindByClass(ctx, plain.root, StringHash("slt-dropdown-mark"), 0));

	UI::Widgets::SetDropdownMark({}, 0.0f);
	UI::Widgets::DropdownParts iconless = UI::Widgets::Dropdown(ctx, ctx.GetRoot(), desc);
	UI::ElementHandle mark = UITest::FindByClass(ctx, iconless.root, StringHash("slt-dropdown-mark"), 0);
	ASSERT_TRUE(ctx.IsAlive(mark));
	EXPECT_FALSE(ctx.HasIcon(mark));
	EXPECT_EQ(ctx.GetTextString(mark), fallback);
}
