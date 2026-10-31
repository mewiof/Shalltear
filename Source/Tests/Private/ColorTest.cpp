#include "TestsPCH.h"

#include "Shalltear/Core/Math/Color.h"

using namespace Shalltear;

TEST(ColorTest, ConstructorsAndConstants) {
	Color c(0.1f, 0.2f, 0.3f);
	EXPECT_FLOAT_EQ(c.a, 1.0f);

	EXPECT_EQ(Color(), Color::Clear());
	EXPECT_EQ(Color::White(), Color(1.0f, 1.0f, 1.0f, 1.0f));
	EXPECT_EQ(Color::Red(), Color(1.0f, 0.0f, 0.0f, 1.0f));

	Color32 c32(10, 20, 30);
	EXPECT_EQ(c32.a, 255);
	EXPECT_EQ(Color32(), Color32::Clear());
}

TEST(ColorTest, Arithmetic) {
	Color a(0.1f, 0.2f, 0.3f, 0.4f);
	Color b(0.5f, 0.5f, 0.5f, 0.5f);
	EXPECT_TRUE(Color::Approximately(a + b, Color(0.6f, 0.7f, 0.8f, 0.9f)));
	EXPECT_TRUE(Color::Approximately(b - a, Color(0.4f, 0.3f, 0.2f, 0.1f)));
	EXPECT_TRUE(Color::Approximately(a * b, Color(0.05f, 0.1f, 0.15f, 0.2f)));
	EXPECT_TRUE(Color::Approximately(a * 2.0f, Color(0.2f, 0.4f, 0.6f, 0.8f)));
	EXPECT_TRUE(Color::Approximately(2.0f * a, a * 2.0f));

	Color compound = a;
	compound += b;
	compound -= b;
	compound *= 2.0f;
	EXPECT_TRUE(Color::Approximately(compound, a * 2.0f));
}

TEST(ColorTest, Lerp) {
	Color mid = Color::Lerp(Color::Black(), Color::White(), 0.5f);
	EXPECT_TRUE(Color::Approximately(mid, Color(0.5f, 0.5f, 0.5f, 1.0f)));
	EXPECT_EQ(Color::Lerp(Color::Black(), Color::White(), 5.0f), Color::White());
	EXPECT_TRUE(Color::Approximately(Color::LerpUnclamped(Color::Black(), Color(0.1f, 0.1f, 0.1f), 2.0f), Color(0.2f, 0.2f, 0.2f, 1.0f)));
}

TEST(ColorTest, Saturated) {
	Color hdr(2.0f, -0.5f, 0.5f, 1.5f);
	EXPECT_EQ(hdr.Saturated(), Color(1.0f, 0.0f, 0.5f, 1.0f));
}

TEST(ColorTest, ToColor32Rounding) {
	EXPECT_EQ(Color(0.5f, 0.0f, 1.0f, 1.0f).ToColor32(), Color32(128, 0, 255, 255));
	EXPECT_EQ(Color(2.0f, -1.0f, 0.0f).ToColor32(), Color32(255, 0, 0, 255));
}

TEST(ColorTest, Color32RoundTrip) {
	Color32 original(12, 34, 56, 78);
	EXPECT_EQ(original.ToColor().ToColor32(), original);
	EXPECT_EQ(Color32(0, 255, 0, 255).ToColor().ToColor32(), Color32(0, 255, 0, 255));
}

TEST(ColorTest, PackedU32) {
	Color32 c(0x11, 0x22, 0x33, 0x44);
	EXPECT_EQ(c.ToU32(), 0x44332211u);
	EXPECT_EQ(Color32::FromU32(0x44332211u), c);
	EXPECT_EQ(Color32::FromU32(c.ToU32()), c);
}

TEST(ColorTest, ParseHexForms) {
	Color out;
	ASSERT_TRUE(Color::TryParseHex("#ff8000", out));
	EXPECT_TRUE(Color::Approximately(out, Color(1.0f, 128.0f / 255.0f, 0.0f, 1.0f)));
	ASSERT_TRUE(Color::TryParseHex("#ff800080", out));
	EXPECT_NEAR(out.a, 128.0f / 255.0f, 1e-5f);
	ASSERT_TRUE(Color::TryParseHex("#f00", out));
	EXPECT_EQ(out, Color::Red());
	ASSERT_TRUE(Color::TryParseHex("#0f08", out));
	EXPECT_TRUE(Color::Approximately(out, Color(0.0f, 1.0f, 0.0f, 8.0f / 15.0f)));
	ASSERT_TRUE(Color::TryParseHex("FF0000", out));
	EXPECT_EQ(out, Color::Red());
}

TEST(ColorTest, ParseHexRejectsMalformedInput) {
	Color out = Color::Blue();
	EXPECT_FALSE(Color::TryParseHex("", out));
	EXPECT_FALSE(Color::TryParseHex("#12345", out));
	EXPECT_FALSE(Color::TryParseHex("#gggggg", out));
	EXPECT_FALSE(Color::TryParseHex("#ff 000", out));
	EXPECT_EQ(out, Color::Blue());
	EXPECT_EQ(Color::FromHex("not-a-color"), Color::Magenta());
}

TEST(ColorTest, HexRoundTrip) {
	Color original = Color::FromHex("#12345678");
	EXPECT_EQ(original.ToHex(), "#12345678");
	EXPECT_EQ(Color::Red().ToHex(), "#ff0000ff");
}

TEST(ColorTest, Equality) {
	EXPECT_TRUE(Color::Red() == Color(1.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_TRUE(Color::Red() != Color(1.0f, 0.0f, 0.0f, 0.5f));
	EXPECT_TRUE(Color32(1, 2, 3, 4) == Color32(1, 2, 3, 4));
	EXPECT_TRUE(Color32(1, 2, 3, 4) != Color32(1, 2, 3, 5));
	EXPECT_TRUE(Color::Approximately(Color::Red(), Color(1.0f - Math::EPSILON * 0.5f, 0.0f, 0.0f, 1.0f)));
}
