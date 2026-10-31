#include "TestsPCH.h"

#include "Shalltear/Core/Serialization/BinarySerializer.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include "Serialization/XML.h"

#include "FuzzHarness.h"

#include <limits>
#include <memory>

using namespace Shalltear;

namespace SerializationFuzzFixture {

	enum class FuzzMode : u8 {
		Idle,
		Running,
		Stopped
	};

} // namespace SerializationFuzzFixture

SLT_REFLECT_ENUM(::SerializationFuzzFixture::FuzzMode)
SLT_ENUM_VALUE(Idle)
SLT_ENUM_VALUE(Running)
SLT_ENUM_VALUE(Stopped)
SLT_END_ENUM()

namespace {

	using SerializationFuzzFixture::FuzzMode;

	using Fuzz::GuardedInput;
	using Fuzz::LentBuffer;
	using Fuzz::Rng;

	constexpr size_t ARENA_BYTES = 64 * 1024;

	const u8* ArenaBase(LinearAllocator& arena) {
		return static_cast<const u8*>(arena.Allocate(1, 1));
	}

	bool ViewIsOwned(std::string_view view, const u8* src, size_t srcLen, const u8* arena, size_t arenaLen) {
		if (view.empty()) {
			return true;
		}
		const u8* p = reinterpret_cast<const u8*>(view.data());
		if (p >= src && p + view.size() <= src + srcLen) {
			return true;
		}
		return p >= arena && p + view.size() <= arena + arenaLen;
	}

	bool ViewsAreOwned(const JSONValue& value, const u8* src, size_t srcLen, const u8* arena, size_t arenaLen) {
		if (value.IsString() && !ViewIsOwned(value.stringValue, src, srcLen, arena, arenaLen)) {
			return false;
		}
		for (size_t i = 0; i < value.MemberCount(); ++i) {
			if (!ViewIsOwned(value.KeyAt(i), src, srcLen, arena, arenaLen)) {
				return false;
			}
		}
		for (size_t i = 0; i < value.Size(); ++i) {
			if (!ViewsAreOwned(value[i], src, srcLen, arena, arenaLen)) {
				return false;
			}
		}
		return true;
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

	const char* const jsonCorpus[] = {
		R"({"guid":"9f2c1a4b-0000-4000-8000-000000000001","importer":{"type":"Texture","srgb":true,"mips":8}})",
		R"({"_type":"MeshRenderer","fields":{"mesh":null,"material":"a-b","layers":[0,1,2,3]}})",
		R"([{"x":1.5,"y":-2.25,"z":3e-4},{"x":0,"y":0,"z":0}])",
		R"({"name":"tab\there\nand \"quotes\" and a \\ backslash","empty":"","escapes":"\b\f\r\/"})",
		R"({"a":{"b":{"c":{"d":{"e":{"f":[1,2,[3,[4,[5]]]]}}}}}})",
		R"({"numbers":[0,-0,1e10,-1E-10,0.000001,123456789012345,3.141592653589793]})",
		R"({"flags":[true,false,null],"nested":{"empty_obj":{},"empty_arr":[]}})",
		R"(  {  "spaced"  :  [  1  ,  2  ]  ,  "more"  :  null  }  )",
	};

	const char interestingBytes[] = {'{', '}', '[', ']', '"', ':', ',', '\\', '/', '-', '+', '.',
									 'e', 'E', 'n', 't', 'f', '0', '9', 'u', ' ', '\t', '\n', '\r',
									 '\0', '\x01', '\x1f', '\x7f', '\xff', '\x80'};

	std::vector<u8> MutateWith(const std::vector<u8>& seed, Rng& rng, size_t capacity, const char* pool, size_t poolSize) {
		std::vector<u8> out = seed;
		auto poolByte = [&] { return static_cast<u8>(pool[rng.Below(static_cast<u32>(poolSize))]); };

		u32 edits = 1 + rng.Below(4);
		for (u32 e = 0; e < edits && !out.empty(); ++e) {
			size_t at = rng.Below(static_cast<u32>(out.size()));
			switch (rng.Below(6)) {
			case 0:
				out[at] ^= static_cast<u8>(1u << rng.Below(8));
				break;
			case 1:
				out[at] = poolByte();
				break;
			case 2:
				out.erase(out.begin() + static_cast<std::ptrdiff_t>(at));
				break;
			case 3:
				if (out.size() < capacity) {
					out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), poolByte());
				}
				break;
			case 4:
				out.resize(at);
				break;
			case 5: {
				size_t runLength = 1 + rng.Below(16);
				runLength = std::min(runLength, out.size() - at);
				if (out.size() + runLength <= capacity) {
					std::vector<u8> run(out.begin() + static_cast<std::ptrdiff_t>(at),
										out.begin() + static_cast<std::ptrdiff_t>(at + runLength));
					out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), run.begin(), run.end());
				}
				break;
			}
			}
		}
		return out;
	}

	std::vector<u8> Mutate(const std::vector<u8>& seed, Rng& rng, size_t capacity) {
		return MutateWith(seed, rng, capacity, interestingBytes, std::size(interestingBytes));
	}

	const char xmlInterestingBytes[] = {'<', '>', '/', '&', ';', '"', '\'', '=', '?', '!', '-',
										'#', 'x', '[', ']', ':', 'a', '0', ' ', '\t', '\n', '\r',
										'\0', '\x01', '\x1f', '\x7f', '\xff', '\xC3'};

	const char* const xmlCorpus[] = {
		R"(<?xml version="1.0" encoding="UTF-8"?><ui version="1"><panel id="root" class="a b"><label>Score</label></panel></ui>)",
		R"(<ui><sheet src="project://UI/Main.slss"/><button id="pause" text="Pause"/></ui>)",
		R"(<a v="&lt;&gt;&amp;&apos;&quot;">&#65;&#x42; and text</a>)",
		R"(<a><!--a comment--><b x='single'/><c y="double"/></a>)",
		R"(<deep><a><b><c><d><e>leaf</e></d></c></b></a></deep>)",
		"<a  spaced = \"1\"   other = '2'  />",
		"<a>line one\nline two\ttabbed</a>",
		R"(<empty></empty>)",
	};

	bool XMLViewIsOwned(std::string_view view, const u8* src, size_t srcLen, const u8* arena, size_t arenaLen) {
		if (view.empty()) {
			return true;
		}
		const u8* p = reinterpret_cast<const u8*>(view.data());
		if (p >= src && p + view.size() <= src + srcLen) {
			return true;
		}
		return p >= arena && p + view.size() <= arena + arenaLen;
	}

	bool XMLViewsAreOwned(const XML::XMLNode& node, const u8* src, size_t srcLen, const u8* arena, size_t arenaLen) {
		if (!XMLViewIsOwned(node.name, src, srcLen, arena, arenaLen) || !XMLViewIsOwned(node.text, src, srcLen, arena, arenaLen)) {
			return false;
		}
		for (const XML::XMLAttribute& attribute : node.attributes) {
			if (!XMLViewIsOwned(attribute.name, src, srcLen, arena, arenaLen) || !XMLViewIsOwned(attribute.value, src, srcLen, arena, arenaLen)) {
				return false;
			}
		}
		for (const XML::XMLNode& child : node.children) {
			if (!XMLViewsAreOwned(child, src, srcLen, arena, arenaLen)) {
				return false;
			}
		}
		return true;
	}

	bool XMLEquals(const XML::XMLNode& a, const XML::XMLNode& b) {
		if (a.name != b.name || a.text != b.text || a.attributes.size() != b.attributes.size() || a.children.size() != b.children.size()) {
			return false;
		}
		for (size_t i = 0; i < a.attributes.size(); ++i) {
			if (a.attributes[i].name != b.attributes[i].name || a.attributes[i].value != b.attributes[i].value) {
				return false;
			}
		}
		for (size_t i = 0; i < a.children.size(); ++i) {
			if (!XMLEquals(a.children[i], b.children[i])) {
				return false;
			}
		}
		return true;
	}

	str DegradeIllegal(std::string_view value) {
		str out;
		for (char c : value) {
			unsigned char byte = static_cast<unsigned char>(c);
			if (byte >= 0x20 || c == '\t' || c == '\n' || c == '\r') {
				out += c;
			} else {
				out += "\xEF\xBF\xBD";
			}
		}
		return out;
	}

	bool XMLEqualsAfterDegrade(const XML::XMLNode& built, const XML::XMLNode& reread) {
		if (built.name != reread.name || DegradeIllegal(built.text) != reread.text || built.attributes.size() != reread.attributes.size() || built.children.size() != reread.children.size()) {
			return false;
		}
		for (size_t i = 0; i < built.attributes.size(); ++i) {
			if (built.attributes[i].name != reread.attributes[i].name || DegradeIllegal(built.attributes[i].value) != reread.attributes[i].value) {
				return false;
			}
		}
		for (size_t i = 0; i < built.children.size(); ++i) {
			if (!XMLEqualsAfterDegrade(built.children[i], reread.children[i])) {
				return false;
			}
		}
		return true;
	}

	class XMLOwnedTree {
	public:
		XML::XMLNode& Root() { return _root; }
		[[nodiscard]] const XML::XMLNode& Root() const { return _root; }

		std::string_view Own(std::string_view text) {
			_storage.push_back(std::make_unique<str>(text));
			return *_storage.back();
		}
	private:
		std::vector<std::unique_ptr<str>> _storage;
		XML::XMLNode _root;
	};

	const char* const xmlNames[] = {"ui", "panel", "label", "button", "sheet", "a", "b", "row_1", "x.y", "z-w", "_u"};
	const char* const xmlAttrNames[] = {"id", "class", "style", "src", "text", "version", "value", "_x"};

	const char* const xmlStringPool[] = {
		"", "plain", "with space", "angle < bracket", "amp & ersand", "quote \" inside",
		"apos ' inside", "tab\there", "newline\nhere", "return\rhere", "]]> sequence",
		"high\xC3\xA9\xE2\x82\xAC", "trailing ", " leading", "\x01", "bell\x07here", "\x1f"};

	void BuildRandomXML(XMLOwnedTree& tree, XML::XMLNode& node, Rng& rng, u32 depth, u32& budget) {
		node.name = xmlNames[rng.Below(static_cast<u32>(std::size(xmlNames)))];

		u32 attributes = rng.Below(4);
		for (u32 i = 0; i < attributes; ++i) {
			std::string_view name = xmlAttrNames[rng.Below(static_cast<u32>(std::size(xmlAttrNames)))];
			if (node.FindAttribute(name)) {
				continue;
			}
			node.attributes.push_back({name, tree.Own(xmlStringPool[rng.Below(static_cast<u32>(std::size(xmlStringPool)))])});
		}

		if (depth < 4 && budget > 0 && rng.OneIn(2)) {
			u32 count = rng.Below(4);
			for (u32 i = 0; i < count && budget > 0; ++i) {
				--budget;
				node.children.emplace_back();
				BuildRandomXML(tree, node.children.back(), rng, depth + 1, budget);
			}
		}
		if (node.children.empty()) {
			node.text = tree.Own(xmlStringPool[rng.Below(static_cast<u32>(std::size(xmlStringPool)))]);
		}
	}

	const char* const stringPool[] = {
		"", "plain", "with space", "quote\" inside", "back\\slash", "slash/inside",
		"tab\there", "newline\nhere", "return\rhere", "\b\f", "high\xC3\xA9\xE2\x82\xAC",
		"\x7f", "\xff\xfe", "{\"looks\":\"like json\"}", "trailing backslash\\",
		"\x01", "bell\x07here", "\x1f", "nul-adjacent\x02\x03"};

	const f64 numberPool[] = {
		0.0, -0.0, 1.0, -1.0, 0.5, -0.125, 3.141592653589793, 1e10, -1e-10,
		1.7976931348623157e308, -1.7976931348623157e308, 5e-324, 123456789012345.0};

	JSONValue RandomDom(Rng& rng, u32 depth) {
		u32 pick = rng.Below(depth >= 6 ? 4u : 6u);
		switch (pick) {
		case 0:
			return JSONValue::MakeNull();
		case 1:
			return JSONValue::MakeBool(rng.OneIn(2));
		case 2:
			return JSONValue::MakeNumber(numberPool[rng.Below(static_cast<u32>(std::size(numberPool)))]);
		case 3:
			return JSONValue::MakeString(stringPool[rng.Below(static_cast<u32>(std::size(stringPool)))]);
		case 4: {
			JSONValue arr = JSONValue::MakeArray();
			u32 count = rng.Below(5);
			for (u32 i = 0; i < count; ++i) {
				arr.Push(RandomDom(rng, depth + 1));
			}
			return arr;
		}
		default: {
			JSONValue obj = JSONValue::MakeObject();
			u32 count = rng.Below(5);
			for (u32 i = 0; i < count; ++i) {
				obj.AddMember(stringPool[rng.Below(static_cast<u32>(std::size(stringPool)))],
							  RandomDom(rng, depth + 1));
			}
			return obj;
		}
		}
	}

	struct FuzzLeaf {
		i32 value = 1;
		f32 weight = 0.5f;

		SLT_REFLECT_VALUE(FuzzLeaf)
		SLT_REFLECT_FIELD(value)
		SLT_REFLECT_FIELD(weight)
		SLT_END_REFLECT()
	};

	struct FuzzMid {
		FuzzLeaf leaf;
		str label = "mid";

		SLT_REFLECT_VALUE(FuzzMid)
		SLT_REFLECT_SUBOBJECT(leaf)
		SLT_REFLECT_FIELD(label)
		SLT_END_REFLECT()
	};

	class FuzzComponent : public Object {
	public:
		f32 health = 100.0f;
		i32 damage = -7;
		bool alive = true;
		str name = "fuzz";
		FuzzMid mid;
		FuzzMode mode = FuzzMode::Idle;
		Guid id;
		Handle<Object> target;

		SLT_REFLECT_CLASS(FuzzComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(health)
		SLT_REFLECT_FIELD(damage)
		SLT_REFLECT_FIELD(alive)
		SLT_REFLECT_FIELD(name)
		SLT_REFLECT_SUBOBJECT(mid)
		SLT_REFLECT_FIELD(mode)
		SLT_REFLECT_FIELD(id)
		SLT_REFLECT_FIELD(target)
		SLT_END_REFLECT()
	};

	std::vector<u8> ValidBlobBytes() {
		FuzzComponent src;
		src.health = 12.5f;
		src.damage = 1234;
		src.alive = false;
		src.name = "a name long enough to have a length worth corrupting";
		src.mid.label = "nested label";
		src.mid.leaf.value = 99;
		src.mode = FuzzMode::Running;
		src.id = Guid(0x0123456789ABCDEFull, 0xFEDCBA9876543210ull);

		Buffer buffer = BinarySerializer::Serialize(&src, FuzzComponent::GetReflectType());
		return std::vector<u8>(buffer.data, buffer.data + buffer.size);
	}

	void ExpectStrCoherent(const str& value, const str& untouched, size_t blobSize) {
		ASSERT_TRUE(value == untouched || value.size() <= blobSize) << "str of " << value.size() << ' ' << blobSize << "-byte blob";

		volatile u8 sink = 0;
		for (char c : value) {
			sink = static_cast<u8>(sink + static_cast<u8>(c));
		}
		(void)sink;
	}

	void ExpectCoherent(const FuzzComponent& dst, size_t blobSize) {
		const FuzzComponent pristine;
		ASSERT_NO_FATAL_FAILURE(ExpectStrCoherent(dst.name, pristine.name, blobSize));
		ASSERT_NO_FATAL_FAILURE(ExpectStrCoherent(dst.mid.label, pristine.mid.label, blobSize));
	}

} // namespace

TEST(SerializationFuzzTest, ParsingRandomBytesStaysInsideTheInput) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC0FFEE01ull);
	std::vector<u8> bytes;

	for (u32 iteration = 0; iteration < 20000; ++iteration) {
		bytes.resize(rng.Below(256));
		for (u8& b : bytes) {
			b = rng.OneIn(3) ? rng.Byte()
							 : static_cast<u8>(interestingBytes[rng.Below(static_cast<u32>(std::size(interestingBytes)))]);
		}

		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		const u8* arenaBase = ArenaBase(arena);
		std::string_view text = input.PlaceText(bytes);

		JSONValue root = JSONParse(text, arena);
		ASSERT_TRUE(ViewsAreOwned(root, reinterpret_cast<const u8*>(text.data()), text.size(), arenaBase, ARENA_BYTES))
			<< "iteration " << iteration;
	}
}

TEST(SerializationFuzzTest, ParsingMutatedDocumentsStaysInsideTheInput) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC0FFEE02ull);

	for (u32 iteration = 0; iteration < 20000; ++iteration) {
		const char* pick = jsonCorpus[rng.Below(static_cast<u32>(std::size(jsonCorpus)))];
		std::vector<u8> seed(pick, pick + std::strlen(pick));
		std::vector<u8> mutant = Mutate(seed, rng, input.Capacity());

		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		const u8* arenaBase = ArenaBase(arena);
		std::string_view text = input.PlaceText(mutant);

		JSONValue root = JSONParse(text, arena);
		ASSERT_TRUE(ViewsAreOwned(root, reinterpret_cast<const u8*>(text.data()), text.size(), arenaBase, ARENA_BYTES))
			<< "iteration " << iteration;
	}
}

TEST(SerializationFuzzTest, AcceptedMutantsSurviveWriteReadRoundTrip) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC0FFEE03ull);
	u32 accepted = 0;

	for (u32 iteration = 0; iteration < 20000; ++iteration) {
		const char* pick = jsonCorpus[rng.Below(static_cast<u32>(std::size(jsonCorpus)))];
		std::vector<u8> seed(pick, pick + std::strlen(pick));
		std::vector<u8> mutant = Mutate(seed, rng, input.Capacity());

		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		std::string_view text = input.PlaceText(mutant);

		JSONValue root = JSONParse(text, arena);
		if (root.IsNull()) {
			continue;
		}
		++accepted;

		for (bool pretty : {false, true}) {
			str written = JSONSerialize(root, pretty);
			LinearAllocator reparseArena(ARENA_BYTES, MemTag::Core);
			JSONValue again = JSONParse(written, reparseArena);
			ASSERT_TRUE(JSONEquals(root, again))
				<< "iteration " << iteration << (pretty ? " (pretty)" : " (compact)") << "\nwrote: " << written;
		}
	}

	EXPECT_GT(accepted, 1000u);
}

TEST(SerializationFuzzTest, DeepNestingIsRejected) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC0FFEE04ull);

	for (u32 iteration = 0; iteration < 2000; ++iteration) {
		u32 depth = SLT_JSON_MAX_DEPTH + 1 + rng.Below(400);
		std::vector<u8> bytes;
		bool asArray = rng.OneIn(2);
		for (u32 i = 0; i < depth; ++i) {
			bytes.push_back(asArray ? static_cast<u8>('[') : static_cast<u8>('{'));
			if (!asArray) {
				const char* key = "\"k\":";
				bytes.insert(bytes.end(), key, key + 4);
			}
		}
		if (!rng.OneIn(3)) {
			for (u32 i = 0; i < depth; ++i) {
				bytes.push_back(asArray ? static_cast<u8>(']') : static_cast<u8>('}'));
			}
		}
		if (bytes.size() > input.Capacity()) {
			continue;
		}

		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		std::string_view text = input.PlaceText(bytes);

		JSONValue root = JSONParse(text, arena);
		ASSERT_TRUE(root.IsNull()) << "iteration " << iteration << " depth " << depth;
	}
}

TEST(SerializationFuzzTest, SerializedDomsReadBackAsThemselves) {
	Rng rng(0xC0FFEE07ull);

	for (u32 iteration = 0; iteration < 5000; ++iteration) {
		JSONValue built = RandomDom(rng, 0);

		for (bool pretty : {false, true}) {
			str written = JSONSerialize(built, pretty);
			LinearAllocator arena(ARENA_BYTES, MemTag::Core);
			JSONValue again = JSONParse(written, arena);
			ASSERT_TRUE(JSONEquals(built, again))
				<< "iteration " << iteration << (pretty ? " (pretty)" : " (compact)") << "\nwrote: " << written;
		}
	}
}

TEST(SerializationFuzzTest, NonFiniteNumbersBecomeNull) {
	for (f64 value : {std::numeric_limits<f64>::quiet_NaN(),
					  std::numeric_limits<f64>::infinity(),
					  -std::numeric_limits<f64>::infinity()}) {
		JSONValue doc = JSONValue::MakeObject();
		doc.AddMember("x", JSONValue::MakeNumber(value));
		doc.AddMember("survivor", JSONValue::MakeNumber(42.0));

		str written = JSONSerialize(doc, false);
		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		JSONValue reread = JSONParse(written, arena);

		ASSERT_TRUE(reread.IsObject()) << "wrote: " << written;
		EXPECT_TRUE(reread["x"].IsNull()) << "wrote: " << written;
		EXPECT_EQ(reread["survivor"].AsNumber(), 42.0) << "wrote: " << written;
	}
}

TEST(SerializationFuzzTest, ControlCharactersRoundTripThroughUnicodeEscape) {
	for (char c : {'\x01', '\x02', '\x07', '\x0b', '\x1f'}) {
		str value(1, c);
		JSONValue doc = JSONValue::MakeObject();
		doc.AddMember("x", JSONValue::MakeString(value));

		str written = JSONSerialize(doc, false);
		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		JSONValue reread = JSONParse(written, arena);

		ASSERT_TRUE(reread.IsObject()) << "wrote: " << written;
		EXPECT_EQ(reread["x"].AsString(), str(1, c)) << "wrote: " << written;
	}
}

TEST(SerializationFuzzTest, RandomXmlStaysInBounds) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC0FFEE10ull);
	std::vector<u8> bytes;

	for (u32 iteration = 0; iteration < 20000; ++iteration) {
		bytes.resize(rng.Below(256));
		for (u8& b : bytes) {
			b = rng.OneIn(3) ? rng.Byte()
							 : static_cast<u8>(xmlInterestingBytes[rng.Below(static_cast<u32>(std::size(xmlInterestingBytes)))]);
		}

		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		const u8* arenaBase = ArenaBase(arena);
		std::string_view text = input.PlaceText(bytes);

		XML::XMLError error;
		XML::XMLNode root = XML::Parse(text, arena, error);
		ASSERT_TRUE(XMLViewsAreOwned(root, reinterpret_cast<const u8*>(text.data()), text.size(), arenaBase, ARENA_BYTES))
			<< "iteration " << iteration;
	}
}

TEST(SerializationFuzzTest, MutatedXmlStaysInBounds) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC0FFEE11ull);

	for (u32 iteration = 0; iteration < 20000; ++iteration) {
		const char* pick = xmlCorpus[rng.Below(static_cast<u32>(std::size(xmlCorpus)))];
		std::vector<u8> seed(pick, pick + std::strlen(pick));
		std::vector<u8> mutant = MutateWith(seed, rng, input.Capacity(), xmlInterestingBytes, std::size(xmlInterestingBytes));

		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		const u8* arenaBase = ArenaBase(arena);
		std::string_view text = input.PlaceText(mutant);

		XML::XMLError error;
		XML::XMLNode root = XML::Parse(text, arena, error);
		ASSERT_TRUE(XMLViewsAreOwned(root, reinterpret_cast<const u8*>(text.data()), text.size(), arenaBase, ARENA_BYTES))
			<< "iteration " << iteration;

		ASSERT_EQ(root.IsValid(), !error.Failed()) << "iteration " << iteration;
	}
}

TEST(SerializationFuzzTest, AcceptedXmlMutantsRoundTrip) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC0FFEE12ull);
	u32 accepted = 0;

	for (u32 iteration = 0; iteration < 20000; ++iteration) {
		const char* pick = xmlCorpus[rng.Below(static_cast<u32>(std::size(xmlCorpus)))];
		std::vector<u8> seed(pick, pick + std::strlen(pick));
		std::vector<u8> mutant = MutateWith(seed, rng, input.Capacity(), xmlInterestingBytes, std::size(xmlInterestingBytes));

		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		std::string_view text = input.PlaceText(mutant);

		XML::XMLError error;
		XML::XMLNode root = XML::Parse(text, arena, error);
		if (!root.IsValid()) {
			continue;
		}
		++accepted;

		for (bool pretty : {false, true}) {
			str written = XML::Write(root, pretty);
			LinearAllocator reparseArena(ARENA_BYTES, MemTag::Core);
			XML::XMLError reparseError;
			XML::XMLNode again = XML::Parse(written, reparseArena, reparseError);
			ASSERT_FALSE(reparseError.Failed()) << "iteration " << iteration << (pretty ? " (pretty)" : " (compact)") << ' ' << reparseError.Describe() << "\nwrote: " << written;
			ASSERT_TRUE(XMLEquals(root, again))
				<< "iteration " << iteration << (pretty ? " (pretty)" : " (compact)") << "\nwrote: " << written;
		}
	}

	EXPECT_GT(accepted, 1000u);
}

TEST(SerializationFuzzTest, DeeplyNestedXMLIsRejectedNotRecursed) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC0FFEE13ull);

	for (u32 iteration = 0; iteration < 2000; ++iteration) {
		u32 depth = XML::SLT_XML_MAX_DEPTH + 1 + rng.Below(200);
		std::vector<u8> bytes;
		for (u32 i = 0; i < depth; ++i) {
			const char* open = "<n>";
			bytes.insert(bytes.end(), open, open + 3);
		}
		if (!rng.OneIn(3)) {
			for (u32 i = 0; i < depth; ++i) {
				const char* close = "</n>";
				bytes.insert(bytes.end(), close, close + 4);
			}
		}
		if (bytes.size() > input.Capacity()) {
			continue;
		}

		LinearAllocator arena(ARENA_BYTES, MemTag::Core);
		std::string_view text = input.PlaceText(bytes);

		XML::XMLError error;
		XML::XMLNode root = XML::Parse(text, arena, error);
		ASSERT_FALSE(root.IsValid()) << "iteration " << iteration << " depth " << depth;
		ASSERT_TRUE(error.Failed()) << "iteration " << iteration << " depth " << depth;
	}
}

TEST(SerializationFuzzTest, SerializedXMLTreesReadBackAsThemselves) {
	Rng rng(0xC0FFEE14ull);

	for (u32 iteration = 0; iteration < 5000; ++iteration) {
		XMLOwnedTree built;
		u32 budget = 48;
		BuildRandomXML(built, built.Root(), rng, 0, budget);

		for (bool pretty : {false, true}) {
			str written = XML::Write(built.Root(), pretty);
			LinearAllocator arena(ARENA_BYTES, MemTag::Core);
			XML::XMLError error;
			XML::XMLNode again = XML::Parse(written, arena, error);
			ASSERT_FALSE(error.Failed())
				<< "iteration " << iteration << (pretty ? " (pretty)" : " (compact)")
				<< "\n"
				<< error.Describe() << "\nwrote: " << written;

			ASSERT_TRUE(XMLEqualsAfterDegrade(built.Root(), again))
				<< "iteration " << iteration << (pretty ? " (pretty)" : " (compact)") << "\nwrote: " << written;
		}
	}
}

TEST(SerializationFuzzTest, BlobPrefixesRefusedOrRead) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	std::vector<u8> valid = ValidBlobBytes();
	ASSERT_FALSE(valid.empty());
	ASSERT_LE(valid.size(), input.Capacity());

	for (size_t length = 0; length <= valid.size(); ++length) {
		const u8* placed = input.Place(valid.data(), length);
		LentBuffer lent(length ? placed : nullptr, length);

		FuzzComponent dst;
		bool ok = BinarySerializer::Deserialize(&dst, FuzzComponent::GetReflectType(), lent.Get());
		if (length < valid.size()) {
			(void)ok;
		} else {
			EXPECT_TRUE(ok);
		}
		ExpectCoherent(dst, length);
	}
}

TEST(SerializationFuzzTest, MutatedBlobsAreRefusedOrReadCoherently) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	std::vector<u8> valid = ValidBlobBytes();
	ASSERT_FALSE(valid.empty());

	Rng rng(0xC0FFEE05ull);

	for (u32 iteration = 0; iteration < 20000; ++iteration) {
		std::vector<u8> blob = valid;

		u32 edits = 1 + rng.Below(6);
		for (u32 e = 0; e < edits && !blob.empty(); ++e) {
			size_t at = rng.Below(static_cast<u32>(blob.size()));
			switch (rng.Below(4)) {
			case 0:
				blob[at] ^= static_cast<u8>(1u << rng.Below(8));
				break;
			case 1:
				blob[at] = rng.Byte();
				break;
			case 2:
				blob[at] = 0xFF;
				break;
			case 3:
				blob.resize(at);
				break;
			}
		}
		if (blob.size() > input.Capacity()) {
			continue;
		}

		const u8* placed = input.Place(blob.data(), blob.size());
		LentBuffer lent(blob.empty() ? nullptr : placed, blob.size());

		FuzzComponent dst;
		BinarySerializer::Deserialize(&dst, FuzzComponent::GetReflectType(), lent.Get());
		ASSERT_NO_FATAL_FAILURE(ExpectCoherent(dst, blob.size())) << "iteration " << iteration;
	}
}

TEST(SerializationFuzzTest, RandomBodiesStayCoherent) {
	GuardedInput input;
	ASSERT_TRUE(input.IsValid());

	std::vector<u8> valid = ValidBlobBytes();
	ASSERT_GT(valid.size(), 24u);

	Rng rng(0xC0FFEE06ull);

	for (u32 iteration = 0; iteration < 20000; ++iteration) {
		std::vector<u8> blob(valid.begin(), valid.begin() + 24);
		size_t bodyLength = rng.Below(192);
		for (size_t i = 0; i < bodyLength; ++i) {
			blob.push_back(rng.OneIn(3) ? 0xFF : rng.Byte());
		}

		const u8* placed = input.Place(blob.data(), blob.size());
		LentBuffer lent(placed, blob.size());

		FuzzComponent dst;
		BinarySerializer::Deserialize(&dst, FuzzComponent::GetReflectType(), lent.Get());
		ASSERT_NO_FATAL_FAILURE(ExpectCoherent(dst, blob.size())) << "iteration " << iteration;
	}
}
