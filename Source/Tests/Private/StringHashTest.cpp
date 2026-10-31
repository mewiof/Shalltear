#include "TestsPCH.h"

using namespace Shalltear;

TEST(StringHashTest, DefaultConstructedIsZero) {
	constexpr StringHash Hash;
	EXPECT_EQ(Hash.GetValue(), 0u);
}

TEST(StringHashTest, ExplicitValueConstructor) {
	constexpr StringHash Hash(0x1234567890ABCDEFull);
	EXPECT_EQ(Hash.GetValue(), 0x1234567890ABCDEFull);
}

TEST(StringHashTest, EmptyStringHashMatchesOffsetBasis) {
	StringHash emptyCStr("");
	StringHash emptyView{std::string_view("")};

	constexpr u64 OFFSET_BASIS = 0xcbf29ce484222325ULL;
	EXPECT_EQ(emptyCStr.GetValue(), OFFSET_BASIS);
	EXPECT_EQ(emptyView.GetValue(), OFFSET_BASIS);
}

TEST(StringHashTest, SameStringProducesSameHash) {
	StringHash a("Transform");
	StringHash b("Transform");
	EXPECT_EQ(a, b);
	EXPECT_EQ(a.GetValue(), b.GetValue());
}

TEST(StringHashTest, DifferentStringsProduceDifferentHashes) {
	StringHash a("Transform");
	StringHash b("Transforn");
	StringHash c("RigidBody");
	EXPECT_NE(a, b);
	EXPECT_NE(a, c);
	EXPECT_NE(b, c);
}

TEST(StringHashTest, CStringMatchesStringView) {
	const char* raw = "WindowResizeEvent";
	StringHash fromCStr(raw);
	StringHash fromView{std::string_view(raw)};
	EXPECT_EQ(fromCStr, fromView);
}

TEST(StringHashTest, EmbeddedNullsAreRespectedByStringView) {
	const char data[] = {'A', 'B', '\0', 'C', 'D'};

	StringHash truncated(data);
	StringHash full{std::string_view(data, sizeof(data))};
	EXPECT_NE(truncated, full);
}

TEST(StringHashTest, UserDefinedLiteralMatchesRuntimeHash) {
	constexpr StringHash CompileTime = "Transform"_hash;
	StringHash runtime("Transform");
	EXPECT_EQ(CompileTime, runtime);
}

TEST(StringHashTest, LiteralIsConstexpr) {
	constexpr StringHash First = "A"_hash;
	constexpr StringHash Second = "B"_hash;
	static_assert(First != Second, "Literal hashing must be constexpr-evaluable");
	SUCCEED();
}

TEST(StringHashTest, EqualityAndInequalityOperators) {
	StringHash a(100);
	StringHash b(100);
	StringHash c(200);

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a == c);
	EXPECT_TRUE(a != c);
	EXPECT_FALSE(a != b);
}

TEST(StringHashTest, LessThanOperatorWorks) {
	StringHash smaller(1);
	StringHash larger(2);
	EXPECT_TRUE(smaller < larger);
	EXPECT_FALSE(larger < smaller);
	EXPECT_FALSE(smaller < smaller);
}

TEST(StringHashTest, StdHashSpecializationReturnsValue) {
	StringHash hash("SomeKey");
	std::hash<StringHash> hasher;
	EXPECT_EQ(hasher(hash), static_cast<std::size_t>(hash.GetValue()));
}

TEST(StringHashTest, UsableAsUnorderedMapKey) {
	std::unordered_map<StringHash, int> lookup;
	lookup["Health"_hash] = 100;
	lookup["Mana"_hash] = 50;

	EXPECT_EQ(lookup["Health"_hash], 100);
	EXPECT_EQ(lookup["Mana"_hash], 50);
	EXPECT_EQ(lookup.size(), 2u);
}
