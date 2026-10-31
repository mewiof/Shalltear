#include "TestsPCH.h"

using namespace Shalltear;

TEST(AssertTest, CoreAssertPassesForTrueCondition) {
	int witness = 0;

	SLT_CORE_ASSERT(true);
	SLT_CORE_ASSERT(1 + 1 == 2);
	SLT_CORE_ASSERT((witness = 7, witness == 7));

#if defined(SLT_ENABLE_ASSERTS)
	EXPECT_EQ(witness, 7);
#else
	EXPECT_EQ(witness, 0);
#endif
}

TEST(AssertTest, CoreAssertMsgPassesForTrueCondition) {
	SLT_CORE_ASSERT_MSG(true, "message never formatted");
	SLT_CORE_ASSERT_MSG(42 > 0, "number was {}", 42);
	SUCCEED();
}

TEST(AssertTest, AppAssertPassesForTrueCondition) {
	SLT_APP_ASSERT(true);
	SLT_APP_ASSERT(!false);
	SUCCEED();
}

TEST(AssertTest, AppAssertMsgPassesForTrueCondition) {
	SLT_APP_ASSERT_MSG(true, "unused");
	SLT_APP_ASSERT_MSG(1 == 1, "also {}", "unused");
	SUCCEED();
}

TEST(AssertTest, AssertMacrosUsableInsideIfElse) {
	if (true)
		SLT_CORE_ASSERT(true);
	else
		SLT_CORE_ASSERT(false);

	if (true)
		SLT_CORE_ASSERT_MSG(true, "unused");
	else
		SLT_CORE_ASSERT_MSG(false, "unused");

	SUCCEED();
}
