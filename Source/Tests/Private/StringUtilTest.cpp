#include "TestsPCH.h"
#include "Shalltear/Core/StringUtil.h"

using namespace Shalltear;

TEST(StringUtilTest, Split) {
	str input = "apple,banana,orange";
	auto parts = StringUtil::Split(input, ",");

	ASSERT_EQ(parts.size(), 3u);
	EXPECT_EQ(parts[0], "apple");
	EXPECT_EQ(parts[1], "banana");
	EXPECT_EQ(parts[2], "orange");
}

TEST(StringUtilTest, SplitIgnoresEmptyEntries) {
	str input = "one,,two,,,three";
	auto parts = StringUtil::Split(input, ",");

	ASSERT_EQ(parts.size(), 3u);
	EXPECT_EQ(parts[0], "one");
	EXPECT_EQ(parts[1], "two");
	EXPECT_EQ(parts[2], "three");
}

TEST(StringUtilTest, Join) {
	std::vector<str> parts = {"apple", "banana", "orange"};
	str result = StringUtil::Join(parts, "-");

	EXPECT_EQ(result, "apple-banana-orange");
}

TEST(StringUtilTest, JoinEmptyOrSingle) {
	std::vector<str> emptyParts;
	EXPECT_EQ(StringUtil::Join(emptyParts, ","), "");

	std::vector<str> singlePart = {"alone"};
	EXPECT_EQ(StringUtil::Join(singlePart, ","), "alone");
}

TEST(StringUtilTest, Trim) {
	EXPECT_EQ(StringUtil::TrimLeft("   hello world"), "hello world");
	EXPECT_EQ(StringUtil::TrimRight("hello world   "), "hello world");
	EXPECT_EQ(StringUtil::Trim(" \t\n hello \r\v "), "hello");
	EXPECT_EQ(StringUtil::Trim("   "), "");
}

TEST(StringUtilTest, StartsWithAndEndsWith) {
	str test = "ShalltearEngine";
	EXPECT_TRUE(StringUtil::StartsWith(test, "Shall"));
	EXPECT_FALSE(StringUtil::StartsWith(test, "Engine"));

	EXPECT_TRUE(StringUtil::EndsWith(test, "Engine"));
	EXPECT_FALSE(StringUtil::EndsWith(test, "Shall"));
}

TEST(StringUtilTest, CaseConversion) {
	EXPECT_EQ(StringUtil::ToLower("HeLLo!"), "hello!");
	EXPECT_EQ(StringUtil::ToUpper("HeLLo!"), "HELLO!");
}

TEST(StringUtilTest, Replace) {
	str source = "foo bar foo baz";
	EXPECT_EQ(StringUtil::Replace(source, "foo", "qux"), "qux bar qux baz");
	EXPECT_EQ(StringUtil::Replace(source, "missing", "qux"), "foo bar foo baz");
}

TEST(StringUtilTest, UTF8WideRoundtrip) {
	str originalUTF8 = "Shalltear 🦇 引擎";

	std::wstring wide = StringUtil::UTF8ToWide(originalUTF8);
	EXPECT_FALSE(wide.empty());

	str roundtripped = StringUtil::WideToUTF8(wide);
	EXPECT_EQ(roundtripped, originalUTF8);
}
