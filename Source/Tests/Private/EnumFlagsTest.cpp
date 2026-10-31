#include "TestsPCH.h"

#include "Shalltear/Core/EnumFlags.h"

using namespace Shalltear;

namespace {

	enum class TestFlags : u32 {
		None = 0,
		A = 1 << 0,
		B = 1 << 1,
		C = 1 << 2,
	};
	SLT_ENUM_CLASS_FLAGS(TestFlags)

} // anonymous namespace

TEST(EnumFlagsTest, OrCombinesFlags) {
	TestFlags combined = TestFlags::A | TestFlags::B;
	EXPECT_EQ(static_cast<u32>(combined), 0b011u);
}

TEST(EnumFlagsTest, AndMasksFlags) {
	TestFlags combined = TestFlags::A | TestFlags::B;
	EXPECT_EQ(combined & TestFlags::A, TestFlags::A);
	EXPECT_EQ(combined & TestFlags::C, TestFlags::None);
}

TEST(EnumFlagsTest, XorTogglesFlags) {
	TestFlags value = TestFlags::A | TestFlags::B;
	value = value ^ TestFlags::B;
	EXPECT_EQ(value, TestFlags::A);
}

TEST(EnumFlagsTest, NotInvertsAndClearsViaMask) {
	TestFlags all = TestFlags::A | TestFlags::B | TestFlags::C;
	TestFlags withoutB = all & ~TestFlags::B;
	EXPECT_EQ(static_cast<u32>(withoutB), 0b101u);
}

TEST(EnumFlagsTest, CompoundAssignmentsMutateInPlace) {
	TestFlags value = TestFlags::None;
	value |= TestFlags::A;
	value |= TestFlags::C;
	EXPECT_EQ(static_cast<u32>(value), 0b101u);

	value &= ~TestFlags::A;
	EXPECT_EQ(value, TestFlags::C);

	value ^= TestFlags::C;
	EXPECT_EQ(value, TestFlags::None);
}

TEST(EnumFlagsTest, HasFlagRequiresAllBits) {
	TestFlags value = TestFlags::A | TestFlags::B;

	EXPECT_TRUE(HasFlag(value, TestFlags::A));
	EXPECT_TRUE(HasFlag(value, TestFlags::A | TestFlags::B));
	EXPECT_FALSE(HasFlag(value, TestFlags::C));
	EXPECT_FALSE(HasFlag(value, TestFlags::A | TestFlags::C));

	EXPECT_FALSE(HasFlag(value, TestFlags::None));
}

TEST(EnumFlagsTest, HasAnyFlagRequiresOneBit) {
	TestFlags value = TestFlags::A | TestFlags::B;

	EXPECT_TRUE(HasAnyFlag(value, TestFlags::A | TestFlags::C));
	EXPECT_FALSE(HasAnyFlag(value, TestFlags::C));
	EXPECT_FALSE(HasAnyFlag(TestFlags::None, TestFlags::A));
}

static_assert(static_cast<u32>(TestFlags::A | TestFlags::B) == 0b011u);
static_assert(HasFlag(TestFlags::A | TestFlags::B, TestFlags::B));
static_assert(!HasAnyFlag(TestFlags::None, TestFlags::A | TestFlags::B | TestFlags::C));
