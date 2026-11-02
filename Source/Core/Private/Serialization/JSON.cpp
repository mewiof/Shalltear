#include "Shalltear/Core/Serialization/JSON.h"

#include "Shalltear/Core/Log.h"

#include <charconv>
#include <cmath>

namespace Shalltear {

	const JSONValue* JSONValue::Find(std::string_view key) const {
		for (size_t i = 0; i < _keys.size(); ++i) {
			if (_keys[i] == key) {
				return &_children[i];
			}
		}
		return nullptr;
	}

	const JSONValue& JSONValue::operator[](std::string_view key) const {
		static const JSONValue nullValue;
		const JSONValue* found = Find(key);
		return found ? *found : nullValue;
	}

	const JSONValue& JSONValue::operator[](size_t index) const {
		static const JSONValue nullValue;
		return index < _children.size() ? _children[index] : nullValue;
	}

	const JSONValue& JSONValue::ChildAt(size_t index) const {
		static const JSONValue nullValue;
		return index < _children.size() ? _children[index] : nullValue;
	}

	JSONValue JSONValue::WithType(Type t) {
		JSONValue v;
		v.type = t;
		return v;
	}

	JSONValue JSONValue::MakeBool(bool value) {
		JSONValue v;
		v.type = Type::Bool;
		v.boolValue = value;
		return v;
	}

	JSONValue JSONValue::MakeNumber(f64 value) {
		JSONValue v;
		v.type = Type::Number;
		v.numberValue = value;
		return v;
	}

	JSONValue JSONValue::MakeFloat(f32 value) {
		JSONValue v;
		v.type = Type::Number;
		v.singlePrecision = true;
		v.numberValue = static_cast<f64>(value);
		return v;
	}

	JSONValue JSONValue::MakeString(std::string_view value) {
		JSONValue v;
		v.type = Type::String;
		v.stringValue = value;
		return v;
	}

	void JSONValue::Push(JSONValue value) {
		type = Type::Array;
		_children.push_back(std::move(value));
	}

	void JSONValue::AddMember(std::string_view key, JSONValue value) {
		type = Type::Object;
		_keys.push_back(key);
		_children.push_back(std::move(value));
	}

	class JSONParser {
	public:
		JSONParser(std::string_view text, LinearAllocator& arena)
			: _text(text), _arena(arena) {}

		JSONValue Parse() {
			SkipWhitespace();
			JSONValue root = ParseValue(0);
			if (_error) {
				return JSONValue{};
			}
			SkipWhitespace();

			if (_pos != _text.size()) {
				return JSONValue{};
			}
			return root;
		}
	private:
		std::string_view _text;
		LinearAllocator& _arena;
		size_t _pos = 0;
		bool _error = false;

		char Peek() const { return _pos < _text.size() ? _text[_pos] : '\0'; }
		char Get() { return _pos < _text.size() ? _text[_pos++] : '\0'; }

		void SkipWhitespace() {
			while (_pos < _text.size()) {
				char c = _text[_pos];
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
					_pos++;
				} else {
					break;
				}
			}
		}

		bool MatchLiteral(std::string_view literal) {
			if (_text.substr(_pos, literal.size()) == literal) {
				_pos += literal.size();
				return true;
			}
			return false;
		}

		JSONValue ParseValue(u32 depth) {
			SkipWhitespace();
			if (_error) {
				return JSONValue{};
			}

			char c = Peek();
			switch (c) {
			case '{':
				return ParseObject(depth);
			case '[':
				return ParseArray(depth);
			case '"': {
				JSONValue v;
				v.type = JSONValue::Type::String;
				if (!ParseString(v.stringValue)) {
					return JSONValue{};
				}
				return v;
			}
			case 't':
				if (MatchLiteral("true")) {
					return JSONValue::MakeBool(true);
				}
				_error = true;
				return JSONValue{};
			case 'f':
				if (MatchLiteral("false")) {
					return JSONValue::MakeBool(false);
				}
				_error = true;
				return JSONValue{};
			case 'n':
				if (MatchLiteral("null")) {
					return JSONValue{};
				}
				_error = true;
				return JSONValue{};
			default:
				if (c == '-' || (c >= '0' && c <= '9')) {
					return ParseNumber();
				}
				_error = true;
				return JSONValue{};
			}
		}

		JSONValue ParseObject(u32 depth) {
			if (depth >= SLT_JSON_MAX_DEPTH) {
				_error = true;
				return JSONValue{};
			}

			Get();
			JSONValue obj = JSONValue::MakeObject();

			SkipWhitespace();
			if (Peek() == '}') {
				Get();
				return obj;
			}

			while (true) {
				SkipWhitespace();
				if (Peek() != '"') {
					_error = true;
					return JSONValue{};
				}

				std::string_view key;
				if (!ParseString(key)) {
					return JSONValue{};
				}

				SkipWhitespace();
				if (Get() != ':') {
					_error = true;
					return JSONValue{};
				}

				JSONValue value = ParseValue(depth + 1);
				if (_error) {
					return JSONValue{};
				}
				obj.AddMember(key, std::move(value));

				SkipWhitespace();
				char c = Get();
				if (c == ',') {
					continue;
				}
				if (c == '}') {
					break;
				}
				_error = true;
				return JSONValue{};
			}
			return obj;
		}

		JSONValue ParseArray(u32 depth) {
			if (depth >= SLT_JSON_MAX_DEPTH) {
				_error = true;
				return JSONValue{};
			}

			Get();
			JSONValue arr = JSONValue::MakeArray();

			SkipWhitespace();
			if (Peek() == ']') {
				Get();
				return arr;
			}

			while (true) {
				JSONValue value = ParseValue(depth + 1);
				if (_error) {
					return JSONValue{};
				}
				arr.Push(std::move(value));

				SkipWhitespace();
				char c = Get();
				if (c == ',') {
					continue;
				}
				if (c == ']') {
					break;
				}
				_error = true;
				return JSONValue{};
			}
			return arr;
		}

		bool ParseString(std::string_view& out) {
			if (Get() != '"') {
				_error = true;
				return false;
			}

			size_t start = _pos;
			bool hasEscape = false;
			while (true) {
				if (_pos >= _text.size()) {
					_error = true;
					return false;
				}
				char c = _text[_pos];
				if (c == '"') {
					break;
				}
				if (c == '\\') {
					hasEscape = true;
					_pos += 2;
					if (_pos > _text.size()) {
						_error = true;
						return false;
					}
					continue;
				}
				if (static_cast<unsigned char>(c) < 0x20) {
					_error = true;
					return false;
				}
				_pos++;
			}

			std::string_view raw = _text.substr(start, _pos - start);
			_pos++;

			if (!hasEscape) {
				out = raw;
				return true;
			}
			return Unescape(raw, out);
		}

		static bool ReadHex4(std::string_view raw, size_t& i, u32& out) {
			if (i + 4 > raw.size()) {
				return false;
			}
			u32 value = 0;
			for (u32 n = 0; n < 4; ++n) {
				char c = raw[i + n];
				u32 digit = 0;
				if (c >= '0' && c <= '9') {
					digit = static_cast<u32>(c - '0');
				} else if (c >= 'a' && c <= 'f') {
					digit = static_cast<u32>(c - 'a') + 10;
				} else if (c >= 'A' && c <= 'F') {
					digit = static_cast<u32>(c - 'A') + 10;
				} else {
					return false;
				}
				value = (value << 4) | digit;
			}
			i += 4;
			out = value;
			return true;
		}

		static void AppendUTF8(u32 cp, char* buffer, size_t& write) {
			if (cp < 0x80) {
				buffer[write++] = static_cast<char>(cp);
			} else if (cp < 0x800) {
				buffer[write++] = static_cast<char>(0xC0 | (cp >> 6));
				buffer[write++] = static_cast<char>(0x80 | (cp & 0x3F));
			} else if (cp < 0x10000) {
				buffer[write++] = static_cast<char>(0xE0 | (cp >> 12));
				buffer[write++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				buffer[write++] = static_cast<char>(0x80 | (cp & 0x3F));
			} else {
				buffer[write++] = static_cast<char>(0xF0 | (cp >> 18));
				buffer[write++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
				buffer[write++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				buffer[write++] = static_cast<char>(0x80 | (cp & 0x3F));
			}
		}

		bool Unescape(std::string_view raw, std::string_view& out) {
			char* buffer = static_cast<char*>(_arena.Allocate(raw.empty() ? 1 : raw.size(), 1));

			size_t write = 0;
			for (size_t i = 0; i < raw.size();) {
				char c = raw[i++];
				if (c != '\\') {
					buffer[write++] = c;
					continue;
				}
				if (i >= raw.size()) {
					_error = true;
					return false;
				}
				char esc = raw[i++];
				switch (esc) {
				case '"':
					buffer[write++] = '"';
					break;
				case '\\':
					buffer[write++] = '\\';
					break;
				case '/':
					buffer[write++] = '/';
					break;
				case 'b':
					buffer[write++] = '\b';
					break;
				case 'f':
					buffer[write++] = '\f';
					break;
				case 'n':
					buffer[write++] = '\n';
					break;
				case 'r':
					buffer[write++] = '\r';
					break;
				case 't':
					buffer[write++] = '\t';
					break;
				case 'u': {
					u32 cp = 0;
					if (!ReadHex4(raw, i, cp)) {
						_error = true;
						return false;
					}
					if (cp >= 0xD800 && cp <= 0xDBFF) {
						if (i + 2 > raw.size() || raw[i] != '\\' || raw[i + 1] != 'u') {
							_error = true;
							return false;
						}
						i += 2;
						u32 low = 0;
						if (!ReadHex4(raw, i, low) || low < 0xDC00 || low > 0xDFFF) {
							_error = true;
							return false;
						}
						cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					} else if (cp >= 0xDC00 && cp <= 0xDFFF) {
						_error = true;
						return false;
					}
					AppendUTF8(cp, buffer, write);
					break;
				}
				default:
					_error = true;
					return false;
				}
			}

			out = std::string_view(buffer, write);
			return true;
		}

		JSONValue ParseNumber() {
			size_t start = _pos;
			if (Peek() == '-') {
				_pos++;
			}
			while (true) {
				char c = Peek();
				if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
					_pos++;
				} else {
					break;
				}
			}

			std::string_view token = _text.substr(start, _pos - start);
			f64 number = 0.0;
			auto result = std::from_chars(token.data(), token.data() + token.size(), number);
			if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
				_error = true;
				return JSONValue{};
			}
			return JSONValue::MakeNumber(number);
		}
	};

	JSONValue JSONParse(std::string_view text, LinearAllocator& arena) {
		JSONParser parser(text, arena);
		return parser.Parse();
	}

	namespace {

		void AppendIndent(str& out, u32 levels) {
			for (u32 i = 0; i < levels; ++i) {
				out += '\t';
			}
		}

		void AppendEscaped(std::string_view s, str& out) {
			for (char c : s) {
				switch (c) {
				case '"':
					out += "\\\"";
					break;
				case '\\':
					out += "\\\\";
					break;
				case '\b':
					out += "\\b";
					break;
				case '\f':
					out += "\\f";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) {
						out += std::format("\\u{:04x}", static_cast<unsigned int>(static_cast<unsigned char>(c)));
					} else {
						out += c;
					}
					break;
				}
			}
		}

		void SerializeValue(const JSONValue& value, str& out, bool pretty, u32 indent) {
			if (indent > SLT_JSON_MAX_DEPTH) {
				out += "null";
				return;
			}

			switch (value.type) {
			case JSONValue::Type::Null:
				out += "null";
				break;
			case JSONValue::Type::Bool:
				out += value.boolValue ? "true" : "false";
				break;
			case JSONValue::Type::Number:
				if (!std::isfinite(value.numberValue)) {
					SLT_LOG_WARN("Serialization", "JsonSerialize: non-finite number written as null");
					out += "null";
					break;
				}
				if (value.singlePrecision) {
					out += std::format("{}", static_cast<f32>(value.numberValue));
				} else {
					out += std::format("{}", value.numberValue);
				}
				break;
			case JSONValue::Type::String:
				out += '"';
				AppendEscaped(value.stringValue, out);
				out += '"';
				break;
			case JSONValue::Type::Array: {
				if (value.Size() == 0) {
					out += "[]";
					break;
				}
				out += '[';
				if (pretty) {
					out += '\n';
				}
				for (size_t i = 0; i < value.Size(); ++i) {
					if (pretty) {
						AppendIndent(out, indent + 1);
					}
					SerializeValue(value[i], out, pretty, indent + 1);
					if (i + 1 < value.Size()) {
						out += ',';
					}
					if (pretty) {
						out += '\n';
					}
				}
				if (pretty) {
					AppendIndent(out, indent);
				}
				out += ']';
				break;
			}
			case JSONValue::Type::Object: {
				if (value.MemberCount() == 0) {
					out += "{}";
					break;
				}
				out += '{';
				if (pretty) {
					out += '\n';
				}
				for (size_t i = 0; i < value.MemberCount(); ++i) {
					if (pretty) {
						AppendIndent(out, indent + 1);
					}
					out += '"';
					AppendEscaped(value.KeyAt(i), out);
					out += pretty ? "\": " : "\":";
					SerializeValue(value.ChildAt(i), out, pretty, indent + 1);
					if (i + 1 < value.MemberCount()) {
						out += ',';
					}
					if (pretty) {
						out += '\n';
					}
				}
				if (pretty) {
					AppendIndent(out, indent);
				}
				out += '}';
				break;
			}
			}
		}

	} // anonymous namespace

	str JSONSerialize(const JSONValue& value, bool pretty) {
		str out;
		SerializeValue(value, out, pretty, 0);
		return out;
	}

} // namespace Shalltear
