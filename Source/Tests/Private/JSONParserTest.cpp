#include "TestsPCH.h"

#include "Shalltear/Core/Serialization/JSON.h"

using namespace Shalltear;

namespace {

	LinearAllocator MakeArena() {
		return LinearAllocator(64 * 1024, MemTag::Core);
	}

	bool JSONEquals(const JSONValue& a, const JSONValue& b) {
		if (a.type != b.type) {
			return false;
		}
		switch (a.type) {
		case JSONValue::Type::Null:
			return true;
		case JSONValue::Type::Bool:
			return a.boolValue == b.boolValue;
		case JSONValue::Type::Number:
			return a.numberValue == b.numberValue;
		case JSONValue::Type::String:
			return a.stringValue == b.stringValue;
		case JSONValue::Type::Array: {
			if (a.Size() != b.Size()) {
				return false;
			}
			for (size_t i = 0; i < a.Size(); ++i) {
				if (!JSONEquals(a[i], b[i])) {
					return false;
				}
			}
			return true;
		}
		case JSONValue::Type::Object: {
			if (a.MemberCount() != b.MemberCount()) {
				return false;
			}
			for (size_t i = 0; i < a.MemberCount(); ++i) {
				if (a.KeyAt(i) != b.KeyAt(i)) {
					return false;
				}
				if (!JSONEquals(a.ChildAt(i), b.ChildAt(i))) {
					return false;
				}
			}
			return true;
		}
		}
		return false;
	}

} // namespace

TEST(JSONParserTest, ParsesNull) {
	LinearAllocator arena = MakeArena();
	JSONValue v = JSONParse("null", arena);
	EXPECT_TRUE(v.IsNull());
}

TEST(JSONParserTest, ParsesBooleans) {
	LinearAllocator arena = MakeArena();
	EXPECT_TRUE(JSONParse("true", arena).AsBool());
	EXPECT_FALSE(JSONParse("false", arena).AsBool(true));
}

TEST(JSONParserTest, ParsesIntegers) {
	LinearAllocator arena = MakeArena();
	EXPECT_EQ(JSONParse("42", arena).AsInt(), 42);
	EXPECT_EQ(JSONParse("-17", arena).AsInt(), -17);
	EXPECT_EQ(JSONParse("0", arena).AsInt(), 0);
}

TEST(JSONParserTest, ParsesFloatsAndExponents) {
	LinearAllocator arena = MakeArena();
	EXPECT_DOUBLE_EQ(JSONParse("3.14", arena).AsNumber(), 3.14);
	EXPECT_DOUBLE_EQ(JSONParse("-0.5", arena).AsNumber(), -0.5);
	EXPECT_DOUBLE_EQ(JSONParse("1e3", arena).AsNumber(), 1000.0);
	EXPECT_DOUBLE_EQ(JSONParse("2.5E-2", arena).AsNumber(), 0.025);
}

TEST(JSONParserTest, ParsesSimpleString) {
	LinearAllocator arena = MakeArena();
	JSONValue v = JSONParse("\"hello world\"", arena);
	ASSERT_TRUE(v.IsString());
	EXPECT_EQ(v.AsString(), "hello world");
}

TEST(JSONParserTest, ParsesEmptyString) {
	LinearAllocator arena = MakeArena();
	JSONValue v = JSONParse("\"\"", arena);
	ASSERT_TRUE(v.IsString());
	EXPECT_EQ(v.AsString(), "");
}

TEST(JSONParserTest, UnescapesSupportedSequences) {
	LinearAllocator arena = MakeArena();
	JSONValue v = JSONParse("\"a\\tb\\nc\\\"d\\\\e\\/f\"", arena);
	ASSERT_TRUE(v.IsString());
	EXPECT_EQ(v.AsString(), "a\tb\nc\"d\\e/f");
}

TEST(JSONParserTest, UnescapedStringPointsIntoSourceBuffer) {
	LinearAllocator arena = MakeArena();
	str source = "\"no_escapes_here\"";
	JSONValue v = JSONParse(source, arena);
	ASSERT_TRUE(v.IsString());

	const char* viewBegin = v.AsString().data();
	const char* sourceBegin = source.data();
	EXPECT_GE(viewBegin, sourceBegin);
	EXPECT_LT(viewBegin, sourceBegin + source.size());
}

TEST(JSONParserTest, EscapedStringUsesArena) {
	LinearAllocator arena = MakeArena();
	str source = "\"tab\\there\"";
	JSONValue v = JSONParse(source, arena);
	ASSERT_TRUE(v.IsString());
	EXPECT_EQ(v.AsString(), "tab\there");

	const char* viewBegin = v.AsString().data();
	const char* sourceBegin = source.data();
	bool insideSource = (viewBegin >= sourceBegin) && (viewBegin < sourceBegin + source.size());
	EXPECT_FALSE(insideSource);
}

TEST(JSONParserTest, ParsesEmptyArrayAndObject) {
	LinearAllocator arena = MakeArena();
	EXPECT_TRUE(JSONParse("[]", arena).IsArray());
	EXPECT_EQ(JSONParse("[]", arena).Size(), 0u);
	EXPECT_TRUE(JSONParse("{}", arena).IsObject());
	EXPECT_EQ(JSONParse("{}", arena).MemberCount(), 0u);
}

TEST(JSONParserTest, ParsesNumberArray) {
	LinearAllocator arena = MakeArena();
	JSONValue v = JSONParse("[1, 2, 3]", arena);
	ASSERT_TRUE(v.IsArray());
	ASSERT_EQ(v.Size(), 3u);
	EXPECT_EQ(v[0].AsInt(), 1);
	EXPECT_EQ(v[1].AsInt(), 2);
	EXPECT_EQ(v[2].AsInt(), 3);
}

TEST(JSONParserTest, ParsesObjectWithMixedValues) {
	LinearAllocator arena = MakeArena();
	JSONValue v = JSONParse(R"({"name": "Player", "health": 100.0, "alive": true})", arena);
	ASSERT_TRUE(v.IsObject());
	EXPECT_TRUE(v.HasKey("name"));
	EXPECT_EQ(v["name"].AsString(), "Player");
	EXPECT_DOUBLE_EQ(v["health"].AsNumber(), 100.0);
	EXPECT_TRUE(v["alive"].AsBool());
}

TEST(JSONParserTest, MissingKeyReturnsNullValue) {
	LinearAllocator arena = MakeArena();
	JSONValue v = JSONParse(R"({"a": 1})", arena);
	ASSERT_TRUE(v.IsObject());
	EXPECT_FALSE(v.HasKey("b"));
	EXPECT_TRUE(v["b"].IsNull());
	EXPECT_TRUE(v["b"]["c"].IsNull());
}

TEST(JSONParserTest, ParsesNestedToDepthEight) {
	LinearAllocator arena = MakeArena();
	const char* json = R"({"a":{"b":{"c":{"d":{"e":{"f":{"g":{"h":42}}}}}}}})";
	JSONValue v = JSONParse(json, arena);
	const JSONValue* cursor = &v;
	const char* keys[] = {"a", "b", "c", "d", "e", "f", "g"};
	for (const char* key : keys) {
		ASSERT_TRUE(cursor->IsObject());
		cursor = &(*cursor)[key];
	}
	EXPECT_EQ((*cursor)["h"].AsInt(), 42);
}

TEST(JSONParserTest, RoundTripPreservesDom) {
	LinearAllocator arena1 = MakeArena();
	LinearAllocator arena2 = MakeArena();

	const char* original = R"({"id":7,"tags":["a","b"],"nested":{"flag":false,"ratio":0.25}})";
	JSONValue first = JSONParse(original, arena1);

	str serialized = JSONSerialize(first, false);
	JSONValue second = JSONParse(serialized, arena2);

	EXPECT_TRUE(JSONEquals(first, second));
}

TEST(JSONParserTest, PrettyPrintUsesTabsAndNewlines) {
	LinearAllocator arena = MakeArena();
	JSONValue v = JSONParse(R"({"x":1})", arena);
	str pretty = JSONSerialize(v, true);
	EXPECT_NE(pretty.find("\n\t\"x\": 1"), str::npos);
}

TEST(JSONParserTest, MalformedInputReturnsNull) {
	LinearAllocator arena = MakeArena();
	EXPECT_TRUE(JSONParse("{", arena).IsNull());
	EXPECT_TRUE(JSONParse("[1, 2", arena).IsNull());
	EXPECT_TRUE(JSONParse("{\"a\":}", arena).IsNull());
	EXPECT_TRUE(JSONParse("tru", arena).IsNull());
	EXPECT_TRUE(JSONParse("\"unterminated", arena).IsNull());
	EXPECT_TRUE(JSONParse("", arena).IsNull());
	EXPECT_TRUE(JSONParse("{\"a\":1} trailing", arena).IsNull());
}

TEST(JSONParserTest, ExcessiveDepthReturnsNullWithoutCrashing) {
	LinearAllocator arena = MakeArena();
	str deep;
	for (int i = 0; i < 64; ++i) {
		deep += '[';
	}
	for (int i = 0; i < 64; ++i) {
		deep += ']';
	}
	JSONValue v = JSONParse(deep, arena);
	EXPECT_TRUE(v.IsNull());
}

TEST(JSONParserTest, UnicodeEscapeDecodesToUTF8) {
	LinearAllocator arena = MakeArena();

	EXPECT_EQ(JSONParse("\"\\u0041\"", arena).AsString(), "A");
	EXPECT_EQ(JSONParse("\"\\u00e9\"", arena).AsString(), "\xC3\xA9");
	EXPECT_EQ(JSONParse("\"\\u20AC\"", arena).AsString(), "\xE2\x82\xAC");
	EXPECT_EQ(JSONParse("\"\\u0001\"", arena).AsString(), "\x01");
}

TEST(JSONParserTest, SurrogatePairDecodes) {
	LinearAllocator arena = MakeArena();
	EXPECT_EQ(JSONParse("\"\\ud834\\udd1e\"", arena).AsString(), "\xF0\x9D\x84\x9E");
}

TEST(JSONParserTest, MalformedUnicodeEscapeIsRejectedNotCorrupted) {
	LinearAllocator arena = MakeArena();
	EXPECT_TRUE(JSONParse("\"\\u00\"", arena).IsNull());
	EXPECT_TRUE(JSONParse("\"\\u00gg\"", arena).IsNull());
	EXPECT_TRUE(JSONParse("\"\\ud834\"", arena).IsNull());
	EXPECT_TRUE(JSONParse("\"\\udd1e\"", arena).IsNull());
	EXPECT_TRUE(JSONParse("\"\\ud834\\u0041\"", arena).IsNull());
}

TEST(JSONParserTest, FloatSerializesAtPrecisionItActuallyHas) {
	JSONValue root = JSONValue::MakeObject();
	root.AddMember("roughness", JSONValue::MakeFloat(0.8f));
	root.AddMember("radius", JSONValue::MakeFloat(0.13f));
	root.AddMember("metallic", JSONValue::MakeFloat(0.0f));
	root.AddMember("one", JSONValue::MakeFloat(1.0f));

	str text = JSONSerialize(root, false);
	EXPECT_NE(text.find("\"roughness\":0.8"), str::npos) << text;
	EXPECT_NE(text.find("\"radius\":0.13"), str::npos) << text;
	EXPECT_NE(text.find("\"metallic\":0"), str::npos) << text;
	EXPECT_NE(text.find("\"one\":1"), str::npos) << text;
	EXPECT_EQ(text.find("0.800000011920929"), str::npos);

	LinearAllocator arena = MakeArena();
	JSONValue parsed = JSONParse(text, arena);
	ASSERT_TRUE(parsed.IsObject());
	EXPECT_EQ(static_cast<f32>(parsed["roughness"].AsNumber()), 0.8f);
	EXPECT_EQ(static_cast<f32>(parsed["radius"].AsNumber()), 0.13f);
	EXPECT_EQ(static_cast<f32>(parsed["one"].AsNumber()), 1.0f);
}

TEST(JSONParserTest, DoubleRoundTrips) {
	const f64 cases[] = {0.1, 1.0 / 3.0, 3.14159265358979, 1e300, 2.2250738585072014e-308};
	JSONValue root = JSONValue::MakeObject();
	root.AddMember("a", JSONValue::MakeNumber(cases[0]));
	root.AddMember("b", JSONValue::MakeNumber(cases[1]));
	root.AddMember("c", JSONValue::MakeNumber(cases[2]));
	root.AddMember("d", JSONValue::MakeNumber(cases[3]));
	root.AddMember("e", JSONValue::MakeNumber(cases[4]));

	str text = JSONSerialize(root, false);
	LinearAllocator arena = MakeArena();
	JSONValue parsed = JSONParse(text, arena);
	ASSERT_TRUE(parsed.IsObject());
	const char* keys[] = {"a", "b", "c", "d", "e"};
	for (size_t i = 0; i < 5; ++i) {
		EXPECT_EQ(parsed[keys[i]].AsNumber(), cases[i]) << keys[i];
	}

	JSONValue pair = JSONValue::MakeObject();
	pair.AddMember("f", JSONValue::MakeFloat(0.1f));
	pair.AddMember("d", JSONValue::MakeNumber(0.1));
	str both = JSONSerialize(pair, false);
	EXPECT_NE(both.find("\"f\":0.1"), str::npos) << both;
	EXPECT_NE(both.find("\"d\":0.1"), str::npos) << both;
}
