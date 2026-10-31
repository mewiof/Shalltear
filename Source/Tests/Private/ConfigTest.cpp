#include "TestsPCH.h"

#include "Shalltear/Core/Config.h"

using namespace Shalltear;

TEST(ConfigTest, ParseBasicSectionsAndKeys) {
	Config config;
	config.Parse(
		"globalKey = hello\n"
		"\n"
		"[Window]\n"
		"width = 1920\n"
		"height = 1080\n"
		"title = Shalltear Editor\n"
		"\n"
		"[Render]\n"
		"vsync = true\n");

	EXPECT_EQ(config.GetString("", "globalKey"), "hello");
	EXPECT_EQ(config.GetI64("Window", "width"), 1920);
	EXPECT_EQ(config.GetI64("Window", "height"), 1080);
	EXPECT_EQ(config.GetString("Window", "title"), "Shalltear Editor");
	EXPECT_TRUE(config.GetBool("Render", "vsync"));

	EXPECT_TRUE(config.HasSection("Window"));
	EXPECT_TRUE(config.Has("Window", "width"));
	EXPECT_FALSE(config.Has("Window", "missing"));
	EXPECT_FALSE(config.HasSection("Missing"));
}

TEST(ConfigTest, ParseCommentsAndWhitespace) {
	Config config;
	config.Parse(
		"; a comment\n"
		"# another comment style\n"
		"   [  Spaced  ]   \r\n"
		"   key   =   value   \r\n"
		"\n"
		"\t\n");
	EXPECT_EQ(config.GetString("Spaced", "key"), "value");
}

TEST(ConfigTest, ParseQuotedValues) {
	Config config;
	config.Parse("key = \"  padded value  \"\n");
	EXPECT_EQ(config.GetString("", "key"), "  padded value  ");
}

TEST(ConfigTest, ParseSkipsMalformedLines) {
	Config config;
	config.Parse(
		"valid = 1\n"
		"this line has no equals sign\n"
		"= value without key\n"
		"[unterminated section\n"
		"stillValid = 2\n");
	EXPECT_EQ(config.GetI64("", "valid"), 1);
	EXPECT_EQ(config.GetI64("", "stillValid"), 2);
}

TEST(ConfigTest, DuplicateKeysLastWriterWins) {
	Config config;
	config.Parse(
		"key = first\n"
		"key = second\n");
	EXPECT_EQ(config.GetString("", "key"), "second");
}

TEST(ConfigTest, TypedGettersAndFallbacks) {
	Config config;
	config.Parse(
		"[Values]\n"
		"integer = -42\n"
		"floating = 3.5\n"
		"flagOn = yes\n"
		"flagOff = OFF\n"
		"notANumber = 12abc\n");

	EXPECT_EQ(config.GetI64("Values", "integer"), -42);
	EXPECT_DOUBLE_EQ(config.GetF64("Values", "floating"), 3.5);
	EXPECT_EQ(config.GetI64("Values", "floating", 7), 7);
	EXPECT_TRUE(config.GetBool("Values", "flagOn"));
	EXPECT_FALSE(config.GetBool("Values", "flagOff", true));

	EXPECT_EQ(config.GetI64("Values", "missing", 99), 99);
	EXPECT_EQ(config.GetI64("Values", "notANumber", 99), 99);
	EXPECT_DOUBLE_EQ(config.GetF64("Missing", "missing", 2.5), 2.5);
	EXPECT_EQ(config.GetString("Missing", "missing", "fallback"), "fallback");
	EXPECT_TRUE(config.GetBool("Values", "notANumber", true));
}

TEST(ConfigTest, BoolSpellings) {
	Config config;
	config.Parse(
		"a = true\nb = TRUE\nc = 1\nd = Yes\ne = on\n"
		"f = false\ng = 0\nh = NO\ni = off\n");
	EXPECT_TRUE(config.GetBool("", "a"));
	EXPECT_TRUE(config.GetBool("", "b"));
	EXPECT_TRUE(config.GetBool("", "c"));
	EXPECT_TRUE(config.GetBool("", "d"));
	EXPECT_TRUE(config.GetBool("", "e"));
	EXPECT_FALSE(config.GetBool("", "f", true));
	EXPECT_FALSE(config.GetBool("", "g", true));
	EXPECT_FALSE(config.GetBool("", "h", true));
	EXPECT_FALSE(config.GetBool("", "i", true));
}

TEST(ConfigTest, SettersCreateAndOverwrite) {
	Config config;
	config.SetString("Window", "title", "Test");
	config.SetI64("Window", "width", 1280);
	config.SetF64("Render", "scale", 1.5);
	config.SetBool("Render", "vsync", false);

	EXPECT_EQ(config.GetString("Window", "title"), "Test");
	EXPECT_EQ(config.GetI64("Window", "width"), 1280);
	EXPECT_DOUBLE_EQ(config.GetF64("Render", "scale"), 1.5);
	EXPECT_FALSE(config.GetBool("Render", "vsync", true));

	config.SetI64("Window", "width", 1920);
	EXPECT_EQ(config.GetI64("Window", "width"), 1920);
}

TEST(ConfigTest, Removal) {
	Config config;
	config.SetI64("Section", "a", 1);
	config.SetI64("Section", "b", 2);

	EXPECT_TRUE(config.Remove("Section", "a"));
	EXPECT_FALSE(config.Has("Section", "a"));
	EXPECT_TRUE(config.Has("Section", "b"));
	EXPECT_FALSE(config.Remove("Section", "a"));

	EXPECT_TRUE(config.RemoveSection("Section"));
	EXPECT_FALSE(config.HasSection("Section"));
	EXPECT_FALSE(config.RemoveSection("Section"));

	config.SetI64("X", "y", 3);
	config.Clear();
	EXPECT_FALSE(config.HasSection("X"));
}

TEST(ConfigTest, SerializeParseRoundTrip) {
	Config original;
	original.SetString("", "topLevel", "yes");
	original.SetString("Window", "title", "Shalltear Editor");
	original.SetI64("Window", "width", 1920);
	original.SetBool("Window", "fullscreen", false);
	original.SetF64("Render", "scale", 0.75);
	original.SetString("Render", "padded", "  keep me  ");
	original.SetI64("Render", "negative", -123);

	Config reloaded;
	reloaded.Parse(original.Serialize());

	EXPECT_EQ(reloaded.GetString("", "topLevel"), "yes");
	EXPECT_EQ(reloaded.GetString("Window", "title"), "Shalltear Editor");
	EXPECT_EQ(reloaded.GetI64("Window", "width"), 1920);
	EXPECT_FALSE(reloaded.GetBool("Window", "fullscreen", true));
	EXPECT_DOUBLE_EQ(reloaded.GetF64("Render", "scale"), 0.75);
	EXPECT_EQ(reloaded.GetString("Render", "padded"), "  keep me  ");
	EXPECT_EQ(reloaded.GetI64("Render", "negative"), -123);

	EXPECT_EQ(original.Serialize(), reloaded.Serialize());
}

TEST(ConfigTest, SaveLoadRoundTrip) {
	namespace fs = std::filesystem;
	Path path((fs::temp_directory_path() / "shalltear_config_test.ini").string());

	Config original;
	original.SetString("Editor", "layout", "default");
	original.SetI64("Editor", "fontSize", 14);
	ASSERT_TRUE(original.Save(path));

	Config loaded;
	ASSERT_TRUE(loaded.Load(path));
	EXPECT_EQ(loaded.GetString("Editor", "layout"), "default");
	EXPECT_EQ(loaded.GetI64("Editor", "fontSize"), 14);

	fs::remove(fs::path(path.ToString()));

	Config untouched;
	untouched.SetI64("Keep", "me", 1);
	EXPECT_FALSE(untouched.Load(Path("Z:/definitely/missing/file.ini")));
	EXPECT_EQ(untouched.GetI64("Keep", "me"), 1);
}
