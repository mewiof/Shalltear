#pragma once

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include <string_view>
#include <vector>

namespace Shalltear::XML {

	constexpr u32 SLT_XML_MAX_DEPTH = 32;

	struct XMLError {
		enum class Code : u8 {
			None = 0,
			Syntax,
			Name,
			Namespace,
			DocType,
			ProcessingInstruction,
			CdataSection,
			MixedContent,
			UnterminatedTag,
			MismatchedTag,
			DuplicateAttribute,
			BadReference,
			IllegalCharacter,
			TooDeep,
			NoRoot,
			TrailingContent
		};

		Code code = Code::None;
		str detail;
		u32 line = 1;

		[[nodiscard]] bool Failed() const { return code != Code::None; }

		[[nodiscard]] str Describe() const {
			const char* what = "malformed XML";
			switch (code) {
			case Code::None:
				return str("ok");
			case Code::Syntax:
				what = "malformed XML";
				break;
			case Code::Name:
				what = "name is outside the ASCII subset";
				break;
			case Code::Namespace:
				what = "namespaces aren't in the subset";
				break;
			case Code::DocType:
				what = "document type declarations aren't in the subset";
				break;
			case Code::ProcessingInstruction:
				what = "processing instructions aren't in the subset";
				break;
			case Code::CdataSection:
				what = "CDATA sections aren't in the subset";
				break;
			case Code::MixedContent:
				what = "mixed content is not in the subset (an element has text and child elements)";
				break;
			case Code::UnterminatedTag:
				what = "unterminated tag, comment or attribute";
				break;
			case Code::MismatchedTag:
				what = "closing tag does not match the open element";
				break;
			case Code::DuplicateAttribute:
				what = "attribute appears twice on one element";
				break;
			case Code::BadReference:
				what = "entity or character reference is not in the subset";
				break;
			case Code::IllegalCharacter:
				what = "character cannot appear in an XML document";
				break;
			case Code::TooDeep:
				what = "nested past the depth limit";
				break;
			case Code::NoRoot:
				what = "no root element";
				break;
			case Code::TrailingContent:
				what = "content after the root element";
				break;
			}
			str message = str("line ") + str(std::to_string(line)) + str(": ") + str(what);
			if (!detail.empty()) {
				message += str(": '") + detail + str("'");
			}
			return message;
		}
	};

	struct XMLAttribute {
		std::string_view name;
		std::string_view value;
	};

	// non owning, source text + arena must outlive the node
	struct XMLNode {
		std::string_view name;
		std::vector<XMLAttribute> attributes;
		std::vector<XMLNode> children;
		std::string_view text;

		[[nodiscard]] bool IsValid() const { return !name.empty(); }

		[[nodiscard]] const XMLAttribute* FindAttribute(std::string_view key) const {
			for (const XMLAttribute& attribute : attributes) {
				if (attribute.name == key) {
					return &attribute;
				}
			}
			return nullptr;
		}

		[[nodiscard]] std::string_view Attribute(std::string_view key, std::string_view fallback = {}) const {
			const XMLAttribute* found = FindAttribute(key);
			return found ? found->value : fallback;
		}
	};

	namespace Detail {

		[[nodiscard]] inline bool IsSpace(char c) {
			return c == ' ' || c == '\t' || c == '\n' || c == '\r';
		}

		[[nodiscard]] inline bool IsNameStart(char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
		}

		[[nodiscard]] inline bool IsNameChar(char c) {
			return IsNameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
		}

		[[nodiscard]] inline bool IsLegalChar(u32 cp) {
			return cp == 0x09 || cp == 0x0A || cp == 0x0D || (cp >= 0x20 && cp <= 0xD7FF) || (cp >= 0xE000 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0x10FFFF);
		}

		inline u32 EncodeUTF8(u32 cp, char* out) {
			if (cp < 0x80) {
				out[0] = static_cast<char>(cp);
				return 1;
			}
			if (cp < 0x800) {
				out[0] = static_cast<char>(0xC0 | (cp >> 6));
				out[1] = static_cast<char>(0x80 | (cp & 0x3F));
				return 2;
			}
			if (cp < 0x10000) {
				out[0] = static_cast<char>(0xE0 | (cp >> 12));
				out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				out[2] = static_cast<char>(0x80 | (cp & 0x3F));
				return 3;
			}
			out[0] = static_cast<char>(0xF0 | (cp >> 18));
			out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out[3] = static_cast<char>(0x80 | (cp & 0x3F));
			return 4;
		}

		class Reader {
		public:
			Reader(std::string_view text, LinearAllocator& arena, XMLError& error)
				: _text(text), _arena(arena), _error(error) {}

			XMLNode Parse() {
				SkipByteOrderMark();
				if (!SkipDeclaration()) {
					return {};
				}

				if (!SkipMisc()) {
					return {};
				}
				if (_pos >= _text.size()) {
					Fail(XMLError::Code::NoRoot, {});
					return {};
				}

				XMLNode root = ParseElement(0);
				if (_error.Failed()) {
					return {};
				}

				if (!SkipMisc()) {
					return {};
				}
				if (_pos < _text.size()) {
					Fail(XMLError::Code::TrailingContent, Snippet());
					return {};
				}
				return root;
			}
		private:
			std::string_view _text;
			LinearAllocator& _arena;
			XMLError& _error;
			size_t _pos = 0;
			u32 _line = 1;

			[[nodiscard]] char Peek(size_t ahead = 0) const {
				return _pos + ahead < _text.size() ? _text[_pos + ahead] : '\0';
			}

			[[nodiscard]] bool Starts(std::string_view literal) const {
				return _text.substr(_pos, literal.size()) == literal;
			}

			void Advance(size_t count = 1) {
				for (size_t i = 0; i < count && _pos < _text.size(); ++i) {
					if (_text[_pos] == '\n') {
						++_line;
					}
					++_pos;
				}
			}

			void Fail(XMLError::Code code, std::string_view detail) {
				FailAt(code, detail, _line);
			}

			void FailAt(XMLError::Code code, std::string_view detail, u32 line) {
				if (_error.Failed()) {
					return;
				}
				_error.code = code;
				_error.detail = str(detail);
				_error.line = line;
			}

			[[nodiscard]] std::string_view Snippet() const {
				return _text.substr(_pos, 24);
			}

			void SkipByteOrderMark() {
				if (Starts("\xEF\xBB\xBF")) {
					_pos += 3;
				}
			}

			bool SkipDeclaration() {
				if (!Starts("<?xml") || !IsSpace(Peek(5))) {
					return true;
				}
				size_t end = _text.find("?>", _pos);
				if (end == std::string_view::npos) {
					Fail(XMLError::Code::UnterminatedTag, "<?xml");
					return false;
				}
				Advance(end + 2 - _pos);
				return true;
			}

			bool SkipMisc() {
				while (_pos < _text.size()) {
					if (IsSpace(Peek())) {
						Advance();
						continue;
					}
					if (Starts("<!--")) {
						if (!SkipComment()) {
							return false;
						}
						continue;
					}
					if (Starts("<!DOCTYPE") || Starts("<!doctype")) {
						Fail(XMLError::Code::DocType, "<!DOCTYPE");
						return false;
					}
					if (Starts("<![CDATA[")) {
						Fail(XMLError::Code::CdataSection, "<![CDATA[");
						return false;
					}
					if (Starts("<?")) {
						Fail(XMLError::Code::ProcessingInstruction, ReadPiTarget());
						return false;
					}
					if (Starts("<!")) {
						Fail(XMLError::Code::DocType, Snippet());
						return false;
					}
					break;
				}
				return true;
			}

			[[nodiscard]] std::string_view ReadPiTarget() const {
				size_t begin = _pos + 2;
				size_t end = begin;
				while (end < _text.size() && !IsSpace(_text[end]) && _text[end] != '?') {
					++end;
				}
				return _text.substr(_pos, end - _pos);
			}

			bool SkipComment() {
				Advance(4);
				size_t end = _text.find("-->", _pos);
				if (end == std::string_view::npos) {
					Fail(XMLError::Code::UnterminatedTag, "<!--");
					return false;
				}
				Advance(end + 3 - _pos);
				return true;
			}

			bool ReadName(std::string_view& out, bool allowNamespaceDeclaration = false) {
				size_t start = _pos;
				if (!IsNameStart(Peek())) {
					size_t end = start;
					while (end < _text.size() && !IsSpace(_text[end]) && _text[end] != '>' && _text[end] != '/' && _text[end] != '=') {
						++end;
					}
					if (end == start) {
						Fail(XMLError::Code::Syntax, Snippet());
					} else {
						std::string_view bad = _text.substr(start, end - start);
						Fail(bad.find(':') != std::string_view::npos ? XMLError::Code::Namespace : XMLError::Code::Name, bad);
					}
					return false;
				}
				while (_pos < _text.size() && IsNameChar(_text[_pos])) {
					++_pos;
				}
				if (Peek() == ':') {
					size_t end = _pos;
					while (end < _text.size() && (IsNameChar(_text[end]) || _text[end] == ':')) {
						++end;
					}
					std::string_view full = _text.substr(start, end - start);
					if (!allowNamespaceDeclaration || !full.starts_with("xmlns:")) {
						Fail(XMLError::Code::Namespace, full);
						return false;
					}
					_pos = end;
					out = full;
					return true;
				}
				out = _text.substr(start, _pos - start);
				if (out == "xmlns" && !allowNamespaceDeclaration) {
					Fail(XMLError::Code::Namespace, out);
					return false;
				}
				return true;
			}

			u32 ExpandReference(std::string_view run, size_t& at, char* out, u32 line) {
				size_t semicolon = run.find(';', at);
				if (semicolon == std::string_view::npos || semicolon - at > 12) {
					FailAt(XMLError::Code::BadReference, run.substr(at, 12), line);
					return 0;
				}
				std::string_view body = run.substr(at + 1, semicolon - at - 1);
				at = semicolon + 1;

				if (body == "lt") {
					out[0] = '<';
					return 1;
				}
				if (body == "gt") {
					out[0] = '>';
					return 1;
				}
				if (body == "amp") {
					out[0] = '&';
					return 1;
				}
				if (body == "apos") {
					out[0] = '\'';
					return 1;
				}
				if (body == "quot") {
					out[0] = '"';
					return 1;
				}
				if (body.size() >= 2 && body[0] == '#') {
					bool hex = body[1] == 'x' || body[1] == 'X';
					std::string_view digits = body.substr(hex ? 2 : 1);
					if (digits.empty() || digits.size() > 8) {
						Fail(XMLError::Code::BadReference, body);
						return 0;
					}
					u32 cp = 0;
					for (char c : digits) {
						u32 value = 0;
						if (c >= '0' && c <= '9') {
							value = static_cast<u32>(c - '0');
						} else if (hex && c >= 'a' && c <= 'f') {
							value = static_cast<u32>(c - 'a') + 10;
						} else if (hex && c >= 'A' && c <= 'F') {
							value = static_cast<u32>(c - 'A') + 10;
						} else {
							FailAt(XMLError::Code::BadReference, body, line);
							return 0;
						}
						cp = cp * (hex ? 16u : 10u) + value;
						if (cp > 0x10FFFF) {
							FailAt(XMLError::Code::BadReference, body, line);
							return 0;
						}
					}
					if (!IsLegalChar(cp)) {
						FailAt(XMLError::Code::IllegalCharacter, body, line);
						return 0;
					}
					return EncodeUTF8(cp, out);
				}

				FailAt(XMLError::Code::BadReference, body, line);
				return 0;
			}

			bool RunIsClean(std::string_view run, bool normalizeSpaces, u32 line, bool& clean) {
				clean = true;
				for (char c : run) {
					if (c == '&' || c == '\r' || (normalizeSpaces && (c == '\t' || c == '\n'))) {
						clean = false;
						continue;
					}
					if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n') {
						FailAt(XMLError::Code::IllegalCharacter, run.substr(0, 12), line);
						return false;
					}
				}
				return true;
			}

			bool StoreSegments(const std::vector<std::string_view>& segments, bool normalizeSpaces, u32 line, std::string_view& out) {
				size_t total = 0;
				for (std::string_view segment : segments) {
					bool clean = false;
					if (!RunIsClean(segment, normalizeSpaces, line, clean)) {
						return false;
					}
					if (segments.size() == 1 && clean) {
						out = segment;
						return true;
					}
					total += segment.size();
				}
				if (segments.empty()) {
					out = {};
					return true;
				}

				char* buffer = static_cast<char*>(_arena.Allocate(total ? total : 1, 1));

				size_t write = 0;
				for (std::string_view run : segments) {
					size_t at = 0;
					while (at < run.size()) {
						char c = run[at];
						if (c == '&') {
							u32 written = ExpandReference(run, at, buffer + write, line);
							if (written == 0) {
								return false;
							}
							write += written;
							continue;
						}
						if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') {
							FailAt(XMLError::Code::IllegalCharacter, run.substr(at, 12), line);
							return false;
						}
						if (normalizeSpaces && (c == '\t' || c == '\n' || c == '\r')) {
							buffer[write++] = ' ';
							++at;
							continue;
						}
						if (c == '\r') {
							buffer[write++] = '\n';
							++at;
							if (at < run.size() && run[at] == '\n') {
								++at;
							}
							continue;
						}
						buffer[write++] = c;
						++at;
					}
				}
				out = std::string_view(buffer, write);
				return true;
			}

			XMLNode ParseElement(u32 depth) {
				if (depth >= SLT_XML_MAX_DEPTH) {
					Fail(XMLError::Code::TooDeep, Snippet());
					return {};
				}

				Advance();
				XMLNode node;
				if (!ReadName(node.name)) {
					return {};
				}

				bool selfClosing = false;
				if (!ParseAttributes(node, selfClosing)) {
					return {};
				}
				if (selfClosing) {
					return node;
				}
				if (!ParseContent(node, depth)) {
					return {};
				}
				return node;
			}

			bool ParseAttributes(XMLNode& node, bool& selfClosing) {
				while (true) {
					bool sawSpace = false;
					while (IsSpace(Peek())) {
						Advance();
						sawSpace = true;
					}
					if (_pos >= _text.size()) {
						Fail(XMLError::Code::UnterminatedTag, node.name);
						return false;
					}
					if (Peek() == '>') {
						Advance();
						return true;
					}
					if (Peek() == '/') {
						Advance();
						if (Peek() != '>') {
							Fail(XMLError::Code::Syntax, node.name);
							return false;
						}
						Advance();
						selfClosing = true;
						return true;
					}
					if (!sawSpace) {
						Fail(XMLError::Code::Syntax, node.name);
						return false;
					}

					XMLAttribute attribute;
					if (!ReadName(attribute.name, true)) {
						return false;
					}
					while (IsSpace(Peek())) {
						Advance();
					}
					if (Peek() != '=') {
						Fail(XMLError::Code::Syntax, attribute.name);
						return false;
					}
					Advance();
					while (IsSpace(Peek())) {
						Advance();
					}
					char quote = Peek();
					if (quote != '"' && quote != '\'') {
						Fail(XMLError::Code::Syntax, attribute.name);
						return false;
					}
					Advance();
					u32 valueLine = _line;
					size_t begin = _pos;
					while (_pos < _text.size() && _text[_pos] != quote) {
						if (_text[_pos] == '<') {
							Fail(XMLError::Code::Syntax, attribute.name);
							return false;
						}
						Advance();
					}
					if (_pos >= _text.size()) {
						Fail(XMLError::Code::UnterminatedTag, attribute.name);
						return false;
					}
					size_t end = _pos;
					Advance();

					std::vector<std::string_view> segments{_text.substr(begin, end - begin)};
					if (!StoreSegments(segments, true, valueLine, attribute.value)) {
						return false;
					}
					if (node.FindAttribute(attribute.name)) {
						Fail(XMLError::Code::DuplicateAttribute, attribute.name);
						return false;
					}
					node.attributes.push_back(attribute);
				}
			}

			bool ParseContent(XMLNode& node, u32 depth) {
				u32 textLine = _line;
				size_t textBegin = _pos;
				size_t textEnd = _pos;
				bool sawText = false;
				std::vector<std::string_view> segments;

				auto closeSegment = [&] {
					if (textEnd > textBegin) {
						segments.push_back(_text.substr(textBegin, textEnd - textBegin));
					}
					textBegin = _pos;
					textEnd = _pos;
				};

				while (true) {
					if (_pos >= _text.size()) {
						Fail(XMLError::Code::UnterminatedTag, node.name);
						return false;
					}
					if (_text[_pos] != '<') {
						if (!IsSpace(_text[_pos])) {
							sawText = true;
						}
						Advance();
						textEnd = _pos;
						continue;
					}

					if (Starts("<!--")) {
						closeSegment();
						if (!SkipComment()) {
							return false;
						}
						textBegin = _pos;
						textEnd = _pos;
						continue;
					}
					if (Starts("<![CDATA[")) {
						Fail(XMLError::Code::CdataSection, "<![CDATA[");
						return false;
					}
					if (Starts("<!DOCTYPE") || Starts("<!doctype")) {
						Fail(XMLError::Code::DocType, "<!DOCTYPE");
						return false;
					}
					if (Starts("<?")) {
						Fail(XMLError::Code::ProcessingInstruction, ReadPiTarget());
						return false;
					}
					if (Starts("<!")) {
						Fail(XMLError::Code::DocType, Snippet());
						return false;
					}
					if (Starts("</")) {
						Advance(2);
						std::string_view closing;
						if (!ReadName(closing)) {
							return false;
						}
						while (IsSpace(Peek())) {
							Advance();
						}
						if (Peek() != '>') {
							Fail(XMLError::Code::Syntax, closing);
							return false;
						}
						Advance();
						if (closing != node.name) {
							Fail(XMLError::Code::MismatchedTag, closing);
							return false;
						}
						break;
					}

					if (sawText) {
						Fail(XMLError::Code::MixedContent, node.name);
						return false;
					}
					XMLNode child = ParseElement(depth + 1);
					if (_error.Failed()) {
						return false;
					}
					node.children.push_back(std::move(child));
					segments.clear();
					textBegin = _pos;
					textEnd = _pos;
					textLine = _line;
				}
				closeSegment();

				if (!node.children.empty()) {
					if (sawText) {
						Fail(XMLError::Code::MixedContent, node.name);
						return false;
					}
					return true;
				}
				if (!sawText) {
					return true;
				}
				return StoreSegments(segments, false, textLine, node.text);
			}
		};

		inline void AppendChar(str& out, unsigned char byte) {
			if (byte >= 0x20 || byte == '\t' || byte == '\n' || byte == '\r') {
				out += static_cast<char>(byte);
				return;
			}
			out += "\xEF\xBF\xBD";
		}

		inline void AppendText(str& out, std::string_view value) {
			for (char c : value) {
				switch (c) {
				case '&':
					out += "&amp;";
					break;
				case '<':
					out += "&lt;";
					break;
				case '>':
					out += "&gt;";
					break;
				case '\r':
					out += "&#13;";
					break;
				default:
					AppendChar(out, static_cast<unsigned char>(c));
					break;
				}
			}
		}

		inline void AppendAttributeValue(str& out, std::string_view value) {
			for (char c : value) {
				switch (c) {
				case '&':
					out += "&amp;";
					break;
				case '<':
					out += "&lt;";
					break;
				case '>':
					out += "&gt;";
					break;
				case '"':
					out += "&quot;";
					break;
				case '\t':
					out += "&#9;";
					break;
				case '\n':
					out += "&#10;";
					break;
				case '\r':
					out += "&#13;";
					break;
				default:
					AppendChar(out, static_cast<unsigned char>(c));
					break;
				}
			}
		}

		inline void WriteNode(str& out, const XMLNode& node, bool pretty, u32 indent) {
			if (pretty) {
				out.append(indent, '\t');
			}
			out += '<';
			out += node.name;
			for (const XMLAttribute& attribute : node.attributes) {
				out += ' ';
				out += attribute.name;
				out += "=\"";
				AppendAttributeValue(out, attribute.value);
				out += '"';
			}

			if (node.children.empty() && node.text.empty()) {
				out += "/>";
				if (pretty) {
					out += '\n';
				}
				return;
			}

			out += '>';
			if (!node.children.empty()) {
				if (pretty) {
					out += '\n';
				}
				for (const XMLNode& child : node.children) {
					WriteNode(out, child, pretty, indent + 1);
				}
				if (pretty) {
					out.append(indent, '\t');
				}
			} else {
				AppendText(out, node.text);
			}
			out += "</";
			out += node.name;
			out += '>';
			if (pretty) {
				out += '\n';
			}
		}

	} // namespace Detail

	// non owning, text + arena must outlive the node
	[[nodiscard]] inline XMLNode Parse(std::string_view text, LinearAllocator& arena, XMLError& error) {
		error = XMLError{};
		Detail::Reader reader(text, arena, error);
		XMLNode root = reader.Parse();
		if (error.Failed()) {
			return {};
		}
		return root;
	}

	[[nodiscard]] inline str Write(const XMLNode& root, bool pretty = true) {
		str out;
		if (!root.IsValid()) {
			return out;
		}
		out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
		out += pretty ? "\n" : "";
		Detail::WriteNode(out, root, pretty, 0);
		return out;
	}

} // namespace Shalltear::XML
