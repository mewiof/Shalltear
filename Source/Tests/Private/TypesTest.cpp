#include "TestsPCH.h"

using namespace Shalltear;

TEST(TypesTest, UnsignedIntegerSizes) {
	EXPECT_EQ(sizeof(u8), 1u);
	EXPECT_EQ(sizeof(u16), 2u);
	EXPECT_EQ(sizeof(u32), 4u);
	EXPECT_EQ(sizeof(u64), 8u);
}

TEST(TypesTest, SignedIntegerSizes) {
	EXPECT_EQ(sizeof(i8), 1u);
	EXPECT_EQ(sizeof(i16), 2u);
	EXPECT_EQ(sizeof(i32), 4u);
	EXPECT_EQ(sizeof(i64), 8u);
}

TEST(TypesTest, FloatingPointSizes) {
	EXPECT_EQ(sizeof(f32), 4u);
	EXPECT_EQ(sizeof(f64), 8u);
}

TEST(TypesTest, SignednessMatchesNames) {
	EXPECT_FALSE(std::is_signed_v<u8>);
	EXPECT_FALSE(std::is_signed_v<u16>);
	EXPECT_FALSE(std::is_signed_v<u32>);
	EXPECT_FALSE(std::is_signed_v<u64>);

	EXPECT_TRUE(std::is_signed_v<i8>);
	EXPECT_TRUE(std::is_signed_v<i16>);
	EXPECT_TRUE(std::is_signed_v<i32>);
	EXPECT_TRUE(std::is_signed_v<i64>);

	EXPECT_TRUE(std::is_floating_point_v<f32>);
	EXPECT_TRUE(std::is_floating_point_v<f64>);
}

TEST(TypesTest, StrIsStdString) {
	constexpr bool SAME = std::is_same_v<str, std::string>;
	EXPECT_TRUE(SAME);
}

TEST(TypesTest, StringLiteralSuffixImported) {
	auto greeting = "hello"s;
	EXPECT_EQ(greeting, str("hello"));
	EXPECT_EQ(greeting.size(), 5u);
}
