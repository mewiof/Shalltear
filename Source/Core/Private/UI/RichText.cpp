#include "UI/RichText.h"

#include "UI/UIInternal.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace Shalltear::UI {

	namespace {

		constexpr size_t MAX_TAG = 256;

		enum class TagKind : u8 {
			Bold,
			Italic,
			Underline,
			Strike,
			Mark,
			Color,
			Alpha,
			Size,
			Font,
			Weight,
			Spacing,
			LineHeight,
			Offset,
			Sub,
			Super,
			Lower,
			Upper,
			SmallCaps,
			NoBreak,
			Monospace,
			Space,
		};

		struct NamedTag {
			std::string_view name;
			TagKind kind;
			bool takesValue;
		};

		constexpr NamedTag TAGS[] = {
			{"b", TagKind::Bold, false},
			{"i", TagKind::Italic, false},
			{"u", TagKind::Underline, false},
			{"s", TagKind::Strike, false},
			{"mark", TagKind::Mark, true},
			{"color", TagKind::Color, true},
			{"alpha", TagKind::Alpha, true},
			{"size", TagKind::Size, true},
			{"font", TagKind::Font, true},
			{"font-weight", TagKind::Weight, true},
			{"cspace", TagKind::Spacing, true},
			{"line-height", TagKind::LineHeight, true},
			{"voffset", TagKind::Offset, true},
			{"sub", TagKind::Sub, false},
			{"sup", TagKind::Super, false},
			{"lowercase", TagKind::Lower, false},
			{"uppercase", TagKind::Upper, false},
			{"allcaps", TagKind::Upper, false},
			{"smallcaps", TagKind::SmallCaps, false},
			{"nobr", TagKind::NoBreak, false},
			{"mspace", TagKind::Monospace, true},
		};

		constexpr std::string_view IGNORED[] = {"a", "align", "gradient", "indent", "line-indent", "link", "margin", "margin-left",
												"margin-right", "pos", "sprite", "style", "width"};

		struct TagEntry {
			TagKind kind = TagKind::Bold;
			Color color{};
			RichAmount amount{};
			Handle<FontAsset> font;
			bool fontKnown = false;
			u16 weight = 0;
			f32 alpha = 1.0f;
		};

		struct TagParts {
			std::string_view name;
			std::string_view value;
			bool closing = false;
		};

		std::string_view Trim(std::string_view text) {
			const size_t begin = text.find_first_not_of(" \t\r\n");
			if (begin == std::string_view::npos) {
				return {};
			}
			const size_t end = text.find_last_not_of(" \t\r\n");
			return text.substr(begin, end - begin + 1);
		}

		bool SameName(std::string_view text, std::string_view lower) {
			if (text.size() != lower.size()) {
				return false;
			}
			for (size_t i = 0; i < text.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(text[i])) != lower[i]) {
					return false;
				}
			}
			return true;
		}

		std::string_view Unquote(std::string_view value) {
			if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
				return value.substr(1, value.size() - 2);
			}
			return value;
		}

		bool SplitTag(std::string_view body, TagParts& out) {
			if (body.empty() || std::isspace(static_cast<unsigned char>(body.front()))) {
				return false;
			}
			body = Trim(body);
			if (body.front() == '/') {
				out.closing = true;
				out.name = body.substr(1);
				return !out.name.empty() && out.name.find_first_of(" \t=") == std::string_view::npos;
			}
			if (body.front() == '#') {
				out.name = "color";
				out.value = body;
				return true;
			}
			const size_t split = body.find_first_of("= \t");
			out.name = body.substr(0, split);
			if (split == std::string_view::npos) {
				return true;
			}
			std::string_view rest = Trim(body.substr(split));
			if (!rest.empty() && rest.front() == '=') {
				rest = Trim(rest.substr(1));
			}
			out.value = Unquote(rest);
			return !out.name.empty();
		}

		bool ParseNumber(std::string_view text, f32& out) {
			if (!text.empty() && text.front() == '+') {
				text.remove_prefix(1);
			}
			if (text.empty()) {
				return false;
			}
			const char* end = text.data() + text.size();
			const std::from_chars_result result = std::from_chars(text.data(), end, out);
			return result.ec == std::errc{} && result.ptr == end;
		}

		bool ParseAmount(std::string_view value, bool signIsDelta, RichAmount& out) {
			if (value.empty()) {
				return false;
			}
			RichUnit unit = RichUnit::Pixels;
			std::string_view number = value;
			if (number.ends_with('%')) {
				unit = RichUnit::Percent;
				number.remove_suffix(1);
			} else if (number.ends_with("em")) {
				unit = RichUnit::Em;
				number.remove_suffix(2);
			} else if (number.ends_with("px")) {
				number.remove_suffix(2);
			} else if (signIsDelta && (number.front() == '+' || number.front() == '-')) {
				unit = RichUnit::Delta;
			}
			f32 parsed = 0.0f;
			if (!ParseNumber(number, parsed)) {
				return false;
			}
			out = {parsed, unit};
			return true;
		}

		bool ParseHexByte(std::string_view value, f32& out) {
			if (value.size() != 3 || value.front() != '#') {
				return false;
			}
			u32 parsed = 0;
			const std::from_chars_result result = std::from_chars(value.data() + 1, value.data() + 3, parsed, 16);
			if (result.ec != std::errc{} || result.ptr != value.data() + 3) {
				return false;
			}
			out = static_cast<f32>(parsed) / 255.0f;
			return true;
		}

		bool ParseEntry(TagKind kind, std::string_view value, const StyleVars& vars, TagEntry& out) {
			out.kind = kind;
			switch (kind) {
			case TagKind::Mark:
			case TagKind::Color:
				return ParseStyleColor(value, out.color);
			case TagKind::Alpha:
				return ParseHexByte(value, out.alpha);
			case TagKind::Size:
				return ParseAmount(value, true, out.amount);
			case TagKind::Spacing:
			case TagKind::LineHeight:
			case TagKind::Offset:
				return ParseAmount(value, false, out.amount);
			case TagKind::Monospace:
			case TagKind::Space:
				return ParseAmount(value, false, out.amount) && out.amount.value > 0.0f;
			case TagKind::Weight: {
				f32 weight = 0.0f;
				if (!ParseNumber(value, weight) || weight < 1.0f || weight > 1000.0f) {
					return false;
				}
				out.weight = static_cast<u16>(weight + 0.5f);
				return true;
			}
			case TagKind::Font:
				if (value.empty()) {
					return false;
				}
				out.fontKnown = ResolveFontName(value, &vars, out.font);
				return true;
			default:
				return true;
			}
		}

		bool SameLook(const RichSpan& a, const RichSpan& b) {
			return a.font == b.font && a.size == b.size && a.letterSpacing == b.letterSpacing && a.lineHeight == b.lineHeight &&
				   a.baselineShift == b.baselineShift && a.advance == b.advance && a.fontWeight == b.fontWeight && a.paint == b.paint &&
				   a.italic == b.italic && a.script == b.script && a.textCase == b.textCase && a.decoration == b.decoration &&
				   a.noBreak == b.noBreak;
		}

		void WarnIgnored(size_t index) {
			static u32 warned = 0;
			const u32 bit = 1u << index;
			if (!(warned & bit)) {
				warned |= bit;
				SLT_LOG_WARN("UI", "rich text tag <{}> isn't supported (content shown unstyled)", IGNORED[index]);
			}
		}

		class Parser {
		public:
			Parser(const StyleVars& vars, RichText& out)
				: _vars(vars), _out(out) {}

			void Run(std::string_view source) {
				size_t literal = 0;
				size_t i = 0;
				while (i < source.size()) {
					if (source[i] != '<') {
						++i;
						continue;
					}
					if (_noParse) {
						if (source.size() - i >= 10 && SameName(source.substr(i, 10), "</noparse>")) {
							Append(source.substr(literal, i - literal));
							_noParse = false;
							i += 10;
							literal = i;
						} else {
							++i;
						}
						continue;
					}
					const size_t close = source.find_first_of("<>", i + 1);
					if (close == std::string_view::npos || source[close] != '>' || close - i > MAX_TAG) {
						++i;
						continue;
					}
					TagParts parts;
					if (!SplitTag(source.substr(i + 1, close - i - 1), parts)) {
						++i;
						continue;
					}
					Append(source.substr(literal, i - literal));
					literal = i;
					if (!Apply(parts)) {
						++i;
						continue;
					}
					FlushTagText();
					i = close + 1;
					literal = i;
				}
				Append(source.substr(literal));
			}
		private:
			const StyleVars& _vars;
			RichText& _out;
			Vector<TagEntry> _stack{GetMemoryResource(MemTag::UI)};
			bool _noParse = false;
			bool _dirty = true;
			std::string_view _tagText;
			bool _isolate = false;

			bool Apply(const TagParts& parts) {
				if (parts.closing) {
					if (SameName(parts.name, "noparse") || SameName(parts.name, "br")) {
						return true;
					}
					for (const NamedTag& tag : TAGS) {
						if (SameName(parts.name, tag.name)) {
							Pop(tag.kind);
							return true;
						}
					}
					return IsIgnored(parts.name);
				}
				if (SameName(parts.name, "br")) {
					_tagText = "\n";
					return true;
				}
				if (SameName(parts.name, "noparse")) {
					_noParse = true;
					return true;
				}
				if (SameName(parts.name, "space")) {
					TagEntry entry;
					if (!ParseEntry(TagKind::Space, parts.value, _vars, entry)) {
						return false;
					}
					_stack.push_back(entry);
					_dirty = true;
					_tagText = " ";
					_isolate = true;
					return true;
				}
				for (const NamedTag& tag : TAGS) {
					if (!SameName(parts.name, tag.name)) {
						continue;
					}
					if (tag.takesValue == parts.value.empty()) {
						return false;
					}
					TagEntry entry;
					if (!ParseEntry(tag.kind, parts.value, _vars, entry)) {
						return false;
					}
					_stack.push_back(entry);
					_dirty = true;
					return true;
				}
				return IsIgnored(parts.name);
			}

			bool IsIgnored(std::string_view name) {
				for (size_t i = 0; i < std::size(IGNORED); ++i) {
					if (SameName(name, IGNORED[i])) {
						WarnIgnored(i);
						return true;
					}
				}
				return false;
			}

			void Pop(TagKind kind) {
				for (size_t i = _stack.size(); i-- > 0;) {
					if (_stack[i].kind == kind) {
						_stack.erase(_stack.begin() + static_cast<i64>(i));
						_dirty = true;
						return;
					}
				}
			}

			void FlushTagText() {
				if (_tagText.empty()) {
					return;
				}
				Append(_tagText);
				_tagText = {};
				if (_isolate) {
					_stack.pop_back();
					_dirty = true;
					_isolate = false;
				}
			}

			RichSpan Fold() {
				RichSpan span;
				RichPaint paint;
				for (const TagEntry& entry : _stack) {
					switch (entry.kind) {
					case TagKind::Bold:
						span.fontWeight = 700;
						break;
					case TagKind::Italic:
						span.italic = true;
						break;
					case TagKind::Underline:
						span.decoration |= TextDecoration::Underline;
						break;
					case TagKind::Strike:
						span.decoration |= TextDecoration::Strikethrough;
						break;
					case TagKind::Mark:
						span.decoration |= TextDecoration::Mark;
						paint.mark = entry.color;
						break;
					case TagKind::Color:
						paint.color = entry.color;
						paint.hasColor = true;
						break;
					case TagKind::Alpha:
						paint.alpha = entry.alpha;
						paint.hasAlpha = true;
						break;
					case TagKind::Size:
						span.size = entry.amount;
						break;
					case TagKind::Font:
						if (entry.fontKnown) {
							span.font = entry.font;
						}
						break;
					case TagKind::Weight:
						span.fontWeight = entry.weight;
						break;
					case TagKind::Spacing:
						span.letterSpacing = entry.amount;
						break;
					case TagKind::LineHeight:
						span.lineHeight = entry.amount;
						break;
					case TagKind::Offset:
						span.baselineShift = entry.amount;
						break;
					case TagKind::Sub:
						span.script = TextScript::Sub;
						break;
					case TagKind::Super:
						span.script = TextScript::Super;
						break;
					case TagKind::Lower:
						span.textCase = TextCase::Lower;
						break;
					case TagKind::Upper:
						span.textCase = TextCase::Upper;
						break;
					case TagKind::SmallCaps:
						span.textCase = TextCase::SmallCaps;
						break;
					case TagKind::NoBreak:
						span.noBreak = true;
						break;
					case TagKind::Monospace:
						span.advance = entry.amount;
						break;
					case TagKind::Space:
						span.advance = entry.amount;
						span.noBreak = true;
						break;
					}
				}
				const auto found = std::find(_out.paints.begin(), _out.paints.end(), paint);
				span.paint = static_cast<u16>(found - _out.paints.begin());
				if (found == _out.paints.end()) {
					_out.paints.push_back(paint);
				}
				return span;
			}

			void Append(std::string_view text) {
				if (text.empty()) {
					return;
				}
				const u32 begin = static_cast<u32>(_out.display.size());
				if (_dirty || _out.spans.empty()) {
					RichSpan span = Fold();
					_dirty = false;
					if (_out.spans.empty() || !SameLook(_out.spans.back(), span) || _isolate) {
						span.begin = begin;
						span.end = begin;
						_out.spans.push_back(span);
					}
				}
				_out.display.append(text.begin(), text.end());
				_out.spans.back().end = static_cast<u32>(_out.display.size());
			}
		};

	} // anonymous namespace

	void ParseRichText(std::string_view source, const StyleVars& vars, RichText& out) {
		out.display.clear();
		out.spans.clear();
		out.paints.clear();
		out.paints.push_back(RichPaint{});
		Parser(vars, out).Run(source);
	}

	bool IsStyled(const RichText& rich) {
		const RichSpan plain;
		for (const RichSpan& span : rich.spans) {
			if (!SameLook(span, plain)) {
				return true;
			}
		}
		return false;
	}

	void ResolveRichRuns(const RichText& rich, const TextMetrics& base, f32 scale, Vector<TextRun>& out) {
		out.clear();
		auto relative = [scale](const RichAmount& amount, f32 fallback, f32 em) {
			switch (amount.unit) {
			case RichUnit::Pixels:
				return amount.value * scale;
			case RichUnit::Em:
				return amount.value * em;
			case RichUnit::Percent:
				return amount.value * 0.01f * em;
			case RichUnit::Delta:
				return fallback + amount.value * scale;
			default:
				return fallback;
			}
		};
		for (const RichSpan& span : rich.spans) {
			TextRun run;
			run.begin = span.begin;
			run.end = span.end;
			run.font = span.font.value_or(base.font);
			run.sizePx = std::max(1.0f, relative(span.size, base.sizePhysical, base.sizePhysical));
			run.letterSpacing = relative(span.letterSpacing, base.letterSpacing, run.sizePx);
			if (span.lineHeight.unit == RichUnit::Percent) {
				run.lineHeightScale = span.lineHeight.value * 0.01f;
			} else if (span.lineHeight.unit != RichUnit::None) {
				run.lineHeight = relative(span.lineHeight, 0.0f, run.sizePx);
			} else {
				run.lineHeight = base.lineHeightFactor > 0.0f ? base.lineHeightFactor * run.sizePx : base.lineHeight;
			}
			run.baselineShift = relative(span.baselineShift, 0.0f, run.sizePx);
			run.advance = relative(span.advance, 0.0f, run.sizePx);
			run.fontWeight = span.fontWeight != 0 ? span.fontWeight : base.fontWeight;
			run.fontStyle = span.italic ? FontStyle::Italic : base.fontStyle;
			run.paint = span.paint;
			run.script = span.script;
			run.textCase = span.textCase;
			run.decoration = span.decoration;
			run.noBreak = span.noBreak;
			out.push_back(run);
		}
	}

	Color PaintColor(const RichPaint& paint, Color base) {
		Color color = paint.hasColor ? paint.color : base;
		if (paint.hasAlpha) {
			color.a = paint.alpha;
		}
		return color;
	}

	void CollectRichFonts(std::string_view source, Vector<std::string_view>& out) {
		bool noParse = false;
		for (size_t i = source.find('<'); i != std::string_view::npos; i = source.find('<', i + 1)) {
			const size_t close = source.find_first_of("<>", i + 1);
			if (close == std::string_view::npos || source[close] != '>' || close - i > MAX_TAG) {
				continue;
			}
			TagParts parts;
			if (!SplitTag(source.substr(i + 1, close - i - 1), parts)) {
				continue;
			}
			if (SameName(parts.name, "noparse")) {
				noParse = !parts.closing;
			} else if (!noParse && !parts.closing && SameName(parts.name, "font") && !parts.value.empty()) {
				out.push_back(FontNameOf(parts.value));
			}
		}
	}

} // namespace Shalltear::UI
