#include "TestsPCH.h"

#include "Checkout.h"
#include "UITestUtil.h"

#include "Shalltear/Core/UI.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/SceneAnnotations.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <regex>

using namespace Shalltear;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
	}

	Vector<str> ScrapeVarNames(std::string_view source) {
		Vector<str> names{GetMemoryResource(MemTag::Core)};
		size_t cursor = 0;
		while ((cursor = source.find("var(", cursor)) != std::string_view::npos) {
			size_t close = source.find(')', cursor);
			if (close == std::string_view::npos) {
				break;
			}
			names.push_back(str(source.substr(cursor + 4, close - cursor - 4)));
			cursor = close + 1;
		}
		return names;
	}

	bool ThemeDefines(std::string_view varName) {
		for (u32 i = 0; i < EditorTheme::COLOR_TOKEN_COUNT; ++i) {
			if (EditorTheme::GetVarName(static_cast<EditorTheme::ColorToken>(i)) == varName) {
				return true;
			}
		}
		for (u32 i = 0; i < EditorTheme::METRIC_TOKEN_COUNT; ++i) {
			if (EditorTheme::GetVarName(static_cast<EditorTheme::MetricToken>(i)) == varName) {
				return true;
			}
		}
		return false;
	}

	void CollectSourcesUnder(const std::filesystem::path& root, Vector<std::filesystem::path>& out) {
		if (root.empty()) {
			return;
		}
		std::error_code ec;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
			if (!entry.is_regular_file()) {
				continue;
			}
			str extension = entry.path().extension().string();
			if (extension != ".cpp" && extension != ".h") {
				continue;
			}
			str name = entry.path().filename().string();
			if (name == "EditorTheme.h" || name == "EditorTheme.cpp") {
				continue;
			}
			out.push_back(entry.path());
		}
	}

	Vector<std::filesystem::path> EditorSourceFiles() {
		Vector<std::filesystem::path> out{GetMemoryResource(MemTag::Core)};
		std::filesystem::path source = Checkout::Find("Source");
		if (source.empty()) {
			return out;
		}
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(source, ec)) {
			if (!entry.is_directory()) {
				continue;
			}
			const str name = entry.path().filename().string();
			if (name.size() >= 6 && name.compare(name.size() - 6, 6, "Editor") == 0) {
				CollectSourcesUnder(entry.path(), out);
			}
		}
		return out;
	}

	f32 Luma(Color color) { return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b; }

} // anonymous namespace

TEST(EditorThemeTest, TextRampHas4VisibleSteps) {
	using EditorTheme::ColorToken;
	const f32 strong = Luma(EditorTheme::GetColor(ColorToken::TextStrong));
	const f32 body = Luma(EditorTheme::GetColor(ColorToken::Text));
	const f32 dim = Luma(EditorTheme::GetColor(ColorToken::TextDim));
	const f32 faint = Luma(EditorTheme::GetColor(ColorToken::TextFaint));

	EXPECT_GT(strong - body, 0.1f);
	EXPECT_GT(body - dim, 0.1f);
	EXPECT_GT(dim - faint, 0.1f);
	EXPECT_GT(strong, 0.85f);
}

TEST(EditorThemeTest, NodesSitAboveGridLines) {
	using EditorTheme::ColorToken;
	const f32 background = Luma(EditorTheme::GetColor(ColorToken::GraphBackground));
	const f32 grid = Luma(EditorTheme::GetColor(ColorToken::GraphGrid));
	const f32 major = Luma(EditorTheme::GetColor(ColorToken::GraphGridMajor));
	const f32 muted = Luma(EditorTheme::GetColor(ColorToken::GraphNodeMuted));
	const f32 node = Luma(EditorTheme::GetColor(ColorToken::GraphNode));

	EXPECT_GT(grid, background);
	EXPECT_GT(major, grid);
	EXPECT_GT(muted, major);
	EXPECT_GT(node, major);
	EXPECT_GT(node, muted);

	EXPECT_FLOAT_EQ(EditorTheme::GetColor(ColorToken::GraphNode).a, 1.0f);
	EXPECT_FLOAT_EQ(EditorTheme::GetColor(ColorToken::GraphNodeMuted).a, 1.0f);

	const f32 active = Luma(EditorTheme::GetColor(ColorToken::GraphNodeActive));
	const f32 progress = Luma(EditorTheme::GetColor(ColorToken::GraphNodeProgress));
	EXPECT_GT(progress, node);
	EXPECT_GT(active, progress);
	EXPECT_FLOAT_EQ(EditorTheme::GetColor(ColorToken::GraphNodeActive).a, 1.0f);
	EXPECT_FLOAT_EQ(EditorTheme::GetColor(ColorToken::GraphNodeProgress).a, 1.0f);
}

TEST(EditorThemeTest, PortTokensAreOpaque) {
	using EditorTheme::ColorToken;
	for (ColorToken token : {ColorToken::GraphPortBoolean, ColorToken::GraphPortInteger, ColorToken::GraphPortFloat,
							 ColorToken::GraphPortVector2, ColorToken::GraphPortVector3, ColorToken::GraphPortVector4,
							 ColorToken::GraphPortMatrix, ColorToken::GraphPortTexture, ColorToken::GraphPortOther}) {
		EXPECT_FLOAT_EQ(EditorTheme::GetColor(token).a, 1.0f) << "port token " << static_cast<u32>(token) << " is translucent";
	}
}

TEST(EditorThemeTest, CoreAndEditorTextSameWeight) {
	using EditorTheme::ColorToken;
	EXPECT_EQ(EditorTheme::GetColor(ColorToken::WidgetText), EditorTheme::GetColor(ColorToken::Text));
	EXPECT_EQ(EditorTheme::GetColor(ColorToken::TabText), EditorTheme::GetColor(ColorToken::Text));
	EXPECT_LT(Luma(EditorTheme::GetColor(ColorToken::MenuText)),
			  Luma(EditorTheme::GetColor(ColorToken::Text)) - 0.1f);
}

TEST(EditorThemeTest, DefinesEveryTokenTheShippedSheetsReference) {
	struct Sheet {
		std::string_view source;
		const char* what;
	};
	const Sheet sheets[] = {
		{UI::Widgets::GetDefaultSkinSource(), "the widget skin"},
		{UI::DockManager::GetDefaultSkinSource(), "the dock skin"},
		{EditorTheme::GetSheetSource(), "the editor sheet"},
	};
	for (const Sheet& sheet : sheets) {
		Vector<str> names = ScrapeVarNames(sheet.source);
		EXPECT_FALSE(names.empty()) << sheet.what;
		for (const str& name : names) {
			EXPECT_TRUE(ThemeDefines(name)) << sheet.what << " references " << name;
		}
	}
	EXPECT_FALSE(ThemeDefines("--ed-not-a-real-token"));
}

TEST(EditorThemeTest, EverySwatchClassResolvesToDefinedToken) {
	u32 count = EditorTheme::Class::GetLabelClassCount();
	ASSERT_GT(count, 0u);
	EXPECT_EQ(count, static_cast<u32>(LabelColor::Count) - 1);

	std::string_view sheet = EditorTheme::GetSheetSource();
	for (u8 i = 1; i <= static_cast<u8>(count); ++i) {
		StringHash klass = EditorTheme::Class::ForLabel(i);
		EXPECT_NE(klass, StringHash()) << "swatch " << static_cast<u32>(i);
	}
	EXPECT_EQ(EditorTheme::Class::ForLabel(0), StringHash());
	EXPECT_EQ(EditorTheme::Class::ForLabel(static_cast<u8>(count) + 1), StringHash());

	for (const char* name : {"ed-label-red", "ed-label-orange", "ed-label-yellow", "ed-label-green",
							 "ed-label-teal", "ed-label-blue", "ed-label-purple", "ed-label-pink"}) {
		str base = str(".") + name + " {";
		str hover = str(".slt-tree-row.") + name + ":hover {";
		EXPECT_NE(sheet.find(base), std::string_view::npos) << name;
		EXPECT_NE(sheet.find(hover), std::string_view::npos) << name;
	}
	EXPECT_EQ(sheet.find(".ed-label-chartreuse {"), std::string_view::npos);
}

TEST(EditorThemeTest, LabelledRowPressesWithoutLosingColor) {
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 200};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);
	UI::Widgets::TreeRowDesc row;
	row.label = "Labelled";
	row.onSelect = [](KeyMod) {};
	UI::Widgets::TreeRowParts parts = UI::Widgets::TreeRow(context, context.GetRoot(), row);
	context.AddClass(parts.row, EditorTheme::Class::ForLabel(1));
	context.Update();

	const UI::Rect box = context.GetLayout(parts.row).borderBox;
	const Vec2 on{box.x + box.w * 0.6f, box.y + box.h * 0.5f};
	ASSERT_EQ(context.HitTest(on), parts.row);
	auto send = [&](InputEventType type, Vec2 point) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		context.InjectInput(event);
		context.Update();
	};
	const UI::Brush resting = context.GetComputedStyle(parts.row).background;
	ASSERT_EQ(resting.type, UI::BrushType::LinearGradient);
	send(InputEventType::MouseMove, on);
	const UI::Brush hovered = context.GetComputedStyle(parts.row).background;
	send(InputEventType::MouseDown, on);
	const UI::Brush pressed = context.GetComputedStyle(parts.row).background;
	send(InputEventType::MouseUp, {-5000.0f, -5000.0f});

	EXPECT_FALSE(UITest::SameBrush(pressed, resting));
	EXPECT_FALSE(UITest::SameBrush(pressed, hovered));
	EXPECT_EQ(pressed.type, UI::BrushType::LinearGradient);
	EXPECT_EQ(pressed.gradient0, resting.gradient0);

	UI::Widgets::TreeRowDesc plainDesc = row;
	plainDesc.label = "Plain";
	plainDesc.selected = true;
	UI::Widgets::TreeRowParts plain = UI::Widgets::TreeRow(context, context.GetRoot(), plainDesc);
	context.AddClass(parts.row, StringHash("slt-selected"));
	context.Update();
	auto pressedFill = [&](UI::ElementHandle element) {
		const UI::Rect rect = context.GetLayout(element).borderBox;
		const Vec2 at{rect.x + rect.w * 0.6f, rect.y + rect.h * 0.5f};
		send(InputEventType::MouseMove, at);
		send(InputEventType::MouseDown, at);
		const UI::Brush fill = context.GetComputedStyle(element).background;
		send(InputEventType::MouseUp, {-5000.0f, -5000.0f});
		return fill;
	};
	EXPECT_TRUE(UITest::SameBrush(pressedFill(parts.row), pressedFill(plain.row)));
}

TEST(EditorThemeTest, EverySkinPropertyIsOneParserKnows) {
	std::span<const std::string_view> known = UI::GetStylePropertyNames();
	ASSERT_FALSE(known.empty());

	auto check = [&](std::string_view sheet, const char* which) {
		size_t cursor = 0;
		u32 checked = 0;
		while (true) {
			size_t open = sheet.find('{', cursor);
			if (open == std::string_view::npos) {
				break;
			}
			size_t close = sheet.find('}', open);
			if (close == std::string_view::npos) {
				break;
			}
			std::string_view block = sheet.substr(open + 1, close - open - 1);
			size_t at = 0;
			while (at < block.size()) {
				size_t colon = block.find(':', at);
				if (colon == std::string_view::npos) {
					break;
				}
				std::string_view name = block.substr(at, colon - at);
				while (!name.empty() && (name.front() == ' ' || name.front() == '\n' ||
										 name.front() == '\t' || name.front() == '\r')) {
					name.remove_prefix(1);
				}
				while (!name.empty() && (name.back() == ' ' || name.back() == '\n' ||
										 name.back() == '\t' || name.back() == '\r')) {
					name.remove_suffix(1);
				}
				if (!name.empty() && !name.starts_with("--")) {
					bool found = false;
					for (std::string_view candidate : known) {
						if (candidate == name) {
							found = true;
							break;
						}
					}
					EXPECT_TRUE(found);
					++checked;
				}
				size_t semi = block.find(';', colon);
				if (semi == std::string_view::npos) {
					break;
				}
				at = semi + 1;
			}
			cursor = close + 1;
		}
		EXPECT_GT(checked, 50u);
	};

	check(EditorTheme::GetSheetSource(), "the editor skin");
}

TEST(EditorThemeTest, EditorSheetHoldsNoLiteralColor) {
	std::string_view source = EditorTheme::GetSheetSource();
	EXPECT_EQ(source.find('#'), std::string_view::npos);
}

TEST(EditorThemeTest, NoColorLiteralOutsideTheme) {
	Vector<std::filesystem::path> files = EditorSourceFiles();
	ASSERT_FALSE(files.empty()) << std::filesystem::current_path().string();

	const std::regex literal(R"((\bColor(32)?\b[^;/]*[\(\{]\s*[0-9]|#[0-9a-fA-F]{6}\b))");
	u32 scanned = 0;
	for (const std::filesystem::path& file : files) {
		std::ifstream stream(file);
		str line;
		u32 lineNumber = 0;
		while (std::getline(stream, line)) {
			++lineNumber;
			str code = line.substr(0, line.find("//"));
			if (std::regex_search(code, literal)) {
				ADD_FAILURE();
			}
		}
		++scanned;
	}
	EXPECT_GT(scanned, 10u);

	const char* probes[] = {
		"style.background = UI::Brush::SolidColor(Color(0.1f, 0.2f, 0.3f, 1.0f));",
		"constexpr Color TEXT_DIM{0.62f, 0.62f, 0.66f, 1.0f};",
		"constexpr Color32 AXIS_X{200, 70, 70, 255};",
		"context.LoadStyleSheet(\".x { background-color: #0078d7; }\");",
	};
	for (const char* probe : probes) {
		str text = probe;
		EXPECT_TRUE(std::regex_search(text, literal)) << probe;
	}
	const char* allowed[] = {
		"Color leafColor = Color::White());",
		"return EditorTheme::GetColor(ColorToken::Text);",
		"Color32 color = hovered ? GizmoHighlight() : AxisColor(axis);",
	};
	for (const char* clean : allowed) {
		str text = clean;
		EXPECT_FALSE(std::regex_search(text, literal)) << clean;
	}
}

TEST(EditorThemeTest, NoEditorSourceSizesTextWithLiteral) {
	Vector<std::filesystem::path> files = EditorSourceFiles();
	ASSERT_FALSE(files.empty());

	const std::regex sized(R"((SetText|SetGlyph|SetGlyphOrText)\s*\([^;]*?,\s*[0-9]+\.[0-9]+f\s*,)");
	const std::regex fontSize(R"(fontSizePx\s*=\s*[0-9]+\.[0-9]+f)");
	for (const std::filesystem::path& file : files) {
		std::ifstream stream(file);
		str line;
		u32 lineNumber = 0;
		while (std::getline(stream, line)) {
			++lineNumber;
			str code = line.substr(0, line.find("//"));
			if (std::regex_search(code, sized) || std::regex_search(code, fontSize)) {
				ADD_FAILURE() << file.filename().string() << ":" << lineNumber
							  << " sizes text with a literal. Use a theme metric\n"
							  << "    " << line;
			}
		}
	}
	str probe = "context.SetText(label, font, 12.5f, color, text);";
	EXPECT_TRUE(std::regex_search(probe, sized));
}

TEST(EditorThemeTest, NoInheritedCoreTextSize) {
	Vector<std::filesystem::path> files = EditorSourceFiles();
	ASSERT_FALSE(files.empty());

	const std::regex declaration(
		R"(\b(?:UI::)?(?:Widgets::)?(ButtonDesc|TextFieldDesc|NumberFieldDesc|SliderDesc|VectorFieldDesc|DropdownDesc|MaskDropdownDesc|TreeRowDesc)\s+(\w+)\s*;)");
	const std::regex handOver(R"(Widgets::(Button|TextField|NumberField|Slider|VectorField|Dropdown|MaskDropdown|TreeRow)\s*\()");
	constexpr size_t MAX_SPAN = 80;

	auto scan = [&](const std::vector<str>& lines, const str& what) {
		std::vector<str> found;
		for (size_t i = 0; i < lines.size(); ++i) {
			std::smatch match;
			str code = lines[i].substr(0, lines[i].find("//"));
			if (!std::regex_search(code, match, declaration)) {
				continue;
			}
			const str assigned = match[2].str() + ".fontSizePx";
			bool set = false;
			for (size_t j = i; j < lines.size() && j < i + MAX_SPAN; ++j) {
				if (lines[j].find(assigned) != str::npos) {
					set = true;
					break;
				}
				if (j > i && std::regex_search(lines[j], handOver)) {
					break;
				}
			}
			if (!set) {
				found.push_back(what + ":" + std::to_string(i + 1) + " builds a " + match[1].str() +
								" without setting fontSizePx (inherits Core's default)" +
								". Assign a theme metric\n    " + lines[i]);
			}
		}
		return found;
	};

	for (const std::filesystem::path& file : files) {
		std::ifstream stream(file);
		std::vector<str> lines;
		str line;
		while (std::getline(stream, line)) {
			lines.push_back(line);
		}
		for (const str& report : scan(lines, file.filename().string())) {
			ADD_FAILURE() << report;
		}
	}

	const std::vector<str> bad{"\tUI::Widgets::ButtonDesc ok;", "\tok.label = \"OK\";",
							   "\tUI::Widgets::Button(context, parent, ok);"};
	const std::vector<str> good{"\tUI::Widgets::ButtonDesc ok;", "\tok.fontSizePx = RowFont();",
								"\tUI::Widgets::Button(context, parent, ok);"};
	EXPECT_EQ(scan(bad, "probe").size(), 1u);
	EXPECT_EQ(scan(good, "probe").size(), 0u);
}

TEST(EditorThemeTest, ApplyToContextIsIdempotent) {
	UI::Context ctx(Desc(), nullptr);
	EditorTheme::ApplyToContext(ctx);
	u32 varsAfterFirst = ctx.GetStyleVars().GetCount();
	size_t rulesAfterFirst = ctx.GetStyleVars().GetCount() > 0 ? 1u : 0u;
	(void)rulesAfterFirst;

	UI::ElementHandle card = ctx.CreateElement();
	ctx.AddClass(card, EditorTheme::Class::PopupCard);
	EXPECT_EQ(ctx.GetComputedStyle(card).background.solid, EditorTheme::GetColor(EditorTheme::ColorToken::Surface));
	UI::ElementHandle button = ctx.CreateElement();
	ctx.AddClass(button, StringHash("slt-button"));
	EXPECT_EQ(ctx.GetComputedStyle(button).background.solid, EditorTheme::GetColor(EditorTheme::ColorToken::Control));

	EditorTheme::ApplyToContext(ctx);
	EXPECT_EQ(ctx.GetStyleVars().GetCount(), varsAfterFirst);
	EXPECT_EQ(ctx.GetComputedStyle(card).background.solid, EditorTheme::GetColor(EditorTheme::ColorToken::Surface));
}

TEST(EditorThemeTest, ToolButtonHas4Fills) {
	UI::Context ctx(Desc(), nullptr);
	EditorTheme::ApplyToContext(ctx);
	auto makeButton = [&](bool on) {
		UI::ElementHandle button = ctx.CreateElement();
		ctx.AppendChild(ctx.GetRoot(), button);
		UI::Style style;
		style.width = UI::Length::Pixel(40.0f);
		style.height = UI::Length::Pixel(20.0f);
		ctx.SetStyle(button, style);
		ctx.AddClass(button, EditorTheme::Class::ToolButton);
		if (on) {
			ctx.AddClass(button, EditorTheme::Class::ToolButtonOn);
		}
		return button;
	};
	UI::ElementHandle off = makeButton(false);
	UI::ElementHandle on = makeButton(true);
	ctx.Update();
	auto fill = [&](UI::ElementHandle button) { return ctx.GetComputedStyle(button).background.solid; };
	auto hover = [&](UI::ElementHandle button) {
		const UI::Rect box = ctx.GetLayout(button).borderBox;
		InputEvent move{};
		move.type = InputEventType::MouseMove;
		move.mousePosPx = {static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)};
		ctx.InjectInput(move);
		ctx.Update();
		return ctx.GetHovered() == button;
	};

	const Color offRest = fill(off);
	const Color onRest = fill(on);
	ASSERT_TRUE(hover(off));
	const Color offHover = fill(off);
	ASSERT_TRUE(hover(on));
	const Color onHover = fill(on);

	EXPECT_NE(offHover, onRest);
	EXPECT_NE(onHover, onRest);
	EXPECT_NE(offRest, offHover);
	EXPECT_NE(onHover, offHover);
	EXPECT_NE(offRest, onRest);
}

TEST(EditorThemeTest, SecondThemeSwapsAndRestores) {
	constexpr StringHash HighContrast("TestHighContrast");
	EditorTheme::ThemeDesc desc = EditorTheme::MakeDefaultDesc();
	desc.Set(EditorTheme::ColorToken::Surface, "#101014");
	desc.Set(EditorTheme::ColorToken::Control, "#606068");
	desc.Set(EditorTheme::MetricToken::RadiusLg, "12");
	EditorTheme::Register(HighContrast, desc);

	UI::Context ctx(Desc(), nullptr);
	EditorTheme::ApplyToContext(ctx);
	UI::ElementHandle card = ctx.CreateElement();
	ctx.AddClass(card, EditorTheme::Class::PopupCard);
	const Color darkSurface = EditorTheme::GetColor(EditorTheme::ColorToken::Surface);
	ASSERT_EQ(ctx.GetComputedStyle(card).background.solid, darkSurface);

	ASSERT_TRUE(EditorTheme::SetActive(HighContrast));
	EXPECT_EQ(EditorTheme::GetActive(), HighContrast);
	EXPECT_EQ(EditorTheme::GetColor(EditorTheme::ColorToken::Control), Color::FromHex("#606068"));
	EditorTheme::ApplyToContext(ctx);
	EXPECT_EQ(ctx.GetComputedStyle(card).background.solid, Color::FromHex("#101014"));
	EXPECT_FLOAT_EQ(ctx.GetComputedStyle(card).borderRadius.topLeft, 12.0f);

	EXPECT_FALSE(EditorTheme::SetActive(StringHash("NoSuchTheme")));
	EXPECT_EQ(EditorTheme::GetActive(), HighContrast);

	ASSERT_TRUE(EditorTheme::SetActive(EditorTheme::DarkTheme));
	EditorTheme::ApplyToContext(ctx);
	EXPECT_EQ(ctx.GetComputedStyle(card).background.solid, darkSurface);
}

TEST(EditorThemeTest, PartialThemeInheritsNotResolvingToBlack) {
	const Color inheritedSurface = EditorTheme::GetColor(EditorTheme::ColorToken::Surface);
	const f32 inheritedRadiusLg = EditorTheme::GetMetric(EditorTheme::MetricToken::RadiusLg);

	constexpr StringHash PARTIAL("TestPartial");
	EditorTheme::ThemeDesc desc = EditorTheme::MakeDefaultDesc();
	desc.Set(EditorTheme::ColorToken::Accent, "#ff00ff");
	EditorTheme::Register(PARTIAL, desc);
	ASSERT_TRUE(EditorTheme::SetActive(PARTIAL));

	EXPECT_EQ(EditorTheme::GetColor(EditorTheme::ColorToken::Accent), Color::FromHex("#ff00ff"));
	EXPECT_EQ(EditorTheme::GetColor(EditorTheme::ColorToken::Surface), inheritedSurface);
	EXPECT_FLOAT_EQ(EditorTheme::GetMetric(EditorTheme::MetricToken::RadiusLg), inheritedRadiusLg);
	EXPECT_NE(inheritedSurface, Color::Black());
	EXPECT_GT(inheritedRadiusLg, 0.0f);

	ASSERT_TRUE(EditorTheme::SetActive(EditorTheme::DarkTheme));
}

TEST(EditorThemeTest, TokenNamesUniqueAndPrefixed) {
	Vector<str> seen{GetMemoryResource(MemTag::Core)};
	auto check = [&](std::string_view name) {
		EXPECT_TRUE(name.starts_with("--slt-") || name.starts_with("--ed-")) << name;
		for (const str& other : seen) {
			EXPECT_NE(other, name) << name;
		}
		seen.push_back(str(name));
	};
	for (u32 i = 0; i < EditorTheme::COLOR_TOKEN_COUNT; ++i) {
		check(EditorTheme::GetVarName(static_cast<EditorTheme::ColorToken>(i)));
	}
	for (u32 i = 0; i < EditorTheme::METRIC_TOKEN_COUNT; ++i) {
		check(EditorTheme::GetVarName(static_cast<EditorTheme::MetricToken>(i)));
	}
}

TEST(EditorThemeTest, EveryDefaultValueParses) {
	for (u32 i = 0; i < EditorTheme::COLOR_TOKEN_COUNT; ++i) {
		EditorTheme::ColorToken token = static_cast<EditorTheme::ColorToken>(i);
		EXPECT_NE(EditorTheme::GetColor(token), Color::Magenta()) << EditorTheme::GetVarName(token);
	}
	EditorTheme::ThemeDesc authored = EditorTheme::MakeDefaultDesc();
	for (u32 i = 0; i < EditorTheme::METRIC_TOKEN_COUNT; ++i) {
		EditorTheme::MetricToken token = static_cast<EditorTheme::MetricToken>(i);
		const str& text = authored.metrics[i];
		f32 parsed = 0.0f;
		std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), parsed);
		EXPECT_EQ(result.ec, std::errc()) << EditorTheme::GetVarName(token) << " ('" << text;
		EXPECT_EQ(result.ptr, text.data() + text.size()) << EditorTheme::GetVarName(token) << " ('" << text;
		EXPECT_FLOAT_EQ(EditorTheme::GetMetric(token), parsed) << EditorTheme::GetVarName(token);
	}
}

TEST(EditorThemeTest, CoreAndDarkThemeAgree) {
	UI::StyleVars coreDefaults;
	coreDefaults.Set(UI::GetDefaultStyleVars());
	u32 compared = 0;
	for (u32 i = 0; i < EditorTheme::COLOR_TOKEN_COUNT; ++i) {
		EditorTheme::ColorToken token = static_cast<EditorTheme::ColorToken>(i);
		std::string_view name = EditorTheme::GetVarName(token);
		if (!name.starts_with("--slt-")) {
			continue;
		}
		StringHash key(name);
		ASSERT_TRUE(coreDefaults.Has(key)) << name;
		EXPECT_EQ(coreDefaults.GetColor(key, Color::Magenta()), EditorTheme::GetColor(token)) << name;
		++compared;
	}
	EXPECT_GT(compared, 20u);
}
