#include "UI/UIInternal.h"

#include "Shalltear/Core/Math/Color.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <span>

namespace Shalltear::UI {

	namespace {

		f32 ParseFloat(std::string_view value) {
			f32 result = 0.0f;
			std::from_chars(value.data(), value.data() + value.size(), result);
			return result;
		}

		Length ParseLength(std::string_view value) {
			if (value == "auto") {
				return Length::Auto();
			}
			if (value.empty()) {
				return Length::Pixel(0.0f);
			}
			if (value.back() == '%') {
				return Length::Percent(ParseFloat(value.substr(0, value.size() - 1)) / 100.0f);
			}
			if (value.size() > 2 && value.substr(value.size() - 2) == "px") {
				value = value.substr(0, value.size() - 2);
			}
			return Length::Pixel(ParseFloat(value));
		}

		Color ParseColor(std::string_view value) {
			Color color = Color(0.0f, 0.0f, 0.0f, 0.0f);
			if (value == "transparent" || value == "none") {
				return color;
			}
			if (!Color::TryParseHex(value, color)) {
				SLT_LOG_WARN("UI", "stylesheet: unparseable color '{}'", str(value));
				return Color::Magenta();
			}
			return color;
		}

		std::string_view Trim(std::string_view text) {
			size_t begin = text.find_first_not_of(" \t\r\n");
			if (begin == std::string_view::npos) {
				return {};
			}
			size_t end = text.find_last_not_of(" \t\r\n");
			return text.substr(begin, end - begin + 1);
		}

		u32 SplitInto(std::string_view text, char delimiter, std::string_view* out, u32 maxOut) {
			u32 count = 0;
			size_t cursor = 0;
			while (cursor <= text.size() && count < maxOut) {
				size_t next = text.find(delimiter, cursor);
				if (next == std::string_view::npos) {
					next = text.size();
				}
				std::string_view token = Trim(text.substr(cursor, next - cursor));
				if (!token.empty()) {
					out[count++] = token;
				}
				cursor = next + 1;
				if (next == text.size()) {
					break;
				}
			}
			return count;
		}

		void ParseEdges(std::string_view value, Edges& out) {
			std::string_view tokens[4];
			u32 count = SplitInto(value, ' ', tokens, 4);
			f32 v[4] = {};
			for (u32 i = 0; i < count; ++i) {
				std::string_view token = tokens[i];
				if (token.size() > 2 && token.substr(token.size() - 2) == "px") {
					token = token.substr(0, token.size() - 2);
				}
				v[i] = ParseFloat(token);
			}
			switch (count) {
			case 1:
				out = Edges::All(v[0]);
				break;
			case 2:
				out = {v[1], v[0], v[1], v[0]};
				break;
			case 3:
				out = {v[1], v[0], v[1], v[2]};
				break;
			case 4:
				out = {v[3], v[0], v[1], v[2]};
				break;
			default:
				break;
			}
		}

		void ParseRadii(std::string_view value, Radii& out) {
			std::string_view tokens[4];
			u32 count = SplitInto(value, ' ', tokens, 4);
			f32 v[4] = {};
			for (u32 i = 0; i < count; ++i) {
				std::string_view token = tokens[i];
				if (token.size() > 2 && token.substr(token.size() - 2) == "px") {
					token = token.substr(0, token.size() - 2);
				}
				v[i] = ParseFloat(token);
			}
			switch (count) {
			case 1:
				out = Radii::All(v[0]);
				break;
			case 2:
				out = {v[0], v[1], v[0], v[1]};
				break;
			case 3:
				out = {v[0], v[1], v[2], v[1]};
				break;
			case 4:
				out = {v[0], v[1], v[2], v[3]};
				break;
			default:
				break;
			}
		}

		bool LooksNumeric(std::string_view token) {
			return !token.empty() && (std::isdigit(static_cast<unsigned char>(token[0])) || token[0] == '-' || token[0] == '.');
		}

		BoxShadow ParseSingleShadow(std::string_view value) {
			BoxShadow shadow;
			std::string_view tokens[8];
			u32 count = SplitInto(value, ' ', tokens, 8);
			u32 cursor = 0;
			if (cursor < count && tokens[cursor] == "inset") {
				shadow.inset = true;
				++cursor;
			}
			f32* slots[4] = {&shadow.x, &shadow.y, &shadow.blur, &shadow.spread};
			u32 slot = 0;
			while (cursor < count && slot < 4 && LooksNumeric(tokens[cursor])) {
				std::string_view token = tokens[cursor];
				if (token.size() > 2 && token.substr(token.size() - 2) == "px") {
					token = token.substr(0, token.size() - 2);
				}
				*slots[slot++] = ParseFloat(token);
				++cursor;
			}
			if (cursor < count && tokens[cursor] != "inset") {
				shadow.color = ParseColor(tokens[cursor]);
			}
			if (count > 0 && tokens[count - 1] == "inset") {
				shadow.inset = true;
			}
			return shadow;
		}

		bool ParseLinearGradient(std::string_view value, Brush& out) {
			constexpr std::string_view PREFIX = "linear-gradient(";
			if (!value.starts_with(PREFIX) || value.back() != ')') {
				return false;
			}
			std::string_view inner = value.substr(PREFIX.size(), value.size() - PREFIX.size() - 1);
			std::string_view parts[3];
			u32 count = SplitInto(inner, ',', parts, 3);
			if (count < 2) {
				return false;
			}
			out.type = BrushType::LinearGradient;
			out.gradient0 = ParseColor(Trim(parts[0]));
			out.gradient1 = ParseColor(Trim(parts[1]));
			out.gradientDirection = count >= 3 && Trim(parts[2]) == "to bottom" ? Vec2{0.0f, 1.0f} : Vec2{1.0f, 0.0f};
			return true;
		}

		void ParseFilter(std::string_view value, DropShadow& out) {
			out = DropShadow{};
			if (value == "none") {
				return;
			}
			constexpr std::string_view PREFIX = "drop-shadow(";
			if (!value.starts_with(PREFIX) || value.back() != ')') {
				SLT_LOG_WARN("UI", "stylesheet: filter '{}' isn't drawn (only drop-shadow() is)", str(value));
				return;
			}
			std::string_view tokens[5];
			u32 count = SplitInto(value.substr(PREFIX.size(), value.size() - PREFIX.size() - 1), ' ', tokens, 5);
			DropShadow parsed;
			parsed.color = Color::Black();
			f32* slots[3] = {&parsed.x, &parsed.y, &parsed.blur};
			u32 slot = 0;
			bool colored = false;
			for (u32 i = 0; i < count; ++i) {
				std::string_view token = tokens[i];
				if (LooksNumeric(token) && slot < 3) {
					if (token.size() > 2 && token.substr(token.size() - 2) == "px") {
						token = token.substr(0, token.size() - 2);
					}
					*slots[slot++] = ParseFloat(token);
				} else if (!colored) {
					parsed.color = ParseColor(token);
					colored = true;
				}
			}
			if (slot < 2) {
				SLT_LOG_WARN("UI", "stylesheet: filter '{}' has no offset (drop-shadow() takes x and y first)", str(value));
				return;
			}
			out = parsed;
		}

		struct Sink {
			char* buffer = nullptr;
			size_t capacity = 0;
			size_t length = 0;

			void Put(std::string_view text) {
				size_t room = capacity > length ? capacity - length : 0;
				size_t take = text.size() < room ? text.size() : room;
				std::memcpy(buffer + length, text.data(), take);
				length += take;
			}

			void Put(f32 value) {
				char scratch[32];
				std::to_chars_result result = std::to_chars(scratch, scratch + sizeof(scratch), value);
				Put(std::string_view(scratch, static_cast<size_t>(result.ptr - scratch)));
			}

			void PutPx(f32 value) {
				Put(value);
				Put("px");
			}

			void Put(const Color& color) {
				Color32 quantized = color.ToColor32();
				constexpr char DIGITS[] = "0123456789abcdef";
				char scratch[9];
				scratch[0] = '#';
				const u8 channels[4] = {quantized.r, quantized.g, quantized.b, quantized.a};
				for (u32 i = 0; i < 4; ++i) {
					scratch[1 + i * 2] = DIGITS[channels[i] >> 4];
					scratch[2 + i * 2] = DIGITS[channels[i] & 0xF];
				}
				Put(std::string_view(scratch, sizeof(scratch)));
			}
		};

		void WriteLength(Sink& out, const Length& length) {
			switch (length.unit) {
			case Length::Unit::Auto:
				out.Put("auto");
				break;
			case Length::Unit::Percent:
				out.Put(length.value * 100.0f);
				out.Put("%");
				break;
			default:
				out.PutPx(length.value);
				break;
			}
		}

		void WriteQuad(Sink& out, f32 a, f32 b, f32 c, f32 d) {
			out.PutPx(a);
			if (a == b && b == c && c == d) {
				return;
			}
			out.Put(" ");
			out.PutPx(b);
			out.Put(" ");
			out.PutPx(c);
			out.Put(" ");
			out.PutPx(d);
		}

		void WriteEdges(Sink& out, const Edges& edges) {
			WriteQuad(out, edges.top, edges.right, edges.bottom, edges.left);
		}

		void WriteRadii(Sink& out, const Radii& radii) {
			WriteQuad(out, radii.topLeft, radii.topRight, radii.bottomRight, radii.bottomLeft);
		}

		std::string_view AlignText(Align align) {
			switch (align) {
			case Align::Auto:
				return "auto";
			case Align::Center:
				return "center";
			case Align::End:
				return "flex-end";
			case Align::Stretch:
				return "stretch";
			case Align::SpaceBetween:
				return "space-between";
			case Align::SpaceAround:
				return "space-around";
			default:
				return "flex-start";
			}
		}

		std::string_view CursorText(SystemCursor cursor) {
			switch (cursor) {
			case SystemCursor::IBeam:
				return "text";
			case SystemCursor::Crosshair:
				return "crosshair";
			case SystemCursor::Hand:
				return "pointer";
			case SystemCursor::ResizeEW:
				return "ew-resize";
			case SystemCursor::ResizeNS:
				return "ns-resize";
			case SystemCursor::ResizeAll:
				return "move";
			case SystemCursor::NotAllowed:
				return "not-allowed";
			default:
				return "default";
			}
		}

		void WriteBrush(Sink& out, const Brush& brush) {
			switch (brush.type) {
			case BrushType::Solid:
				out.Put(brush.solid);
				break;
			case BrushType::LinearGradient:
				out.Put("linear-gradient(");
				out.Put(brush.gradient0);
				out.Put(", ");
				out.Put(brush.gradient1);
				out.Put(std::abs(brush.gradientDirection.y) > std::abs(brush.gradientDirection.x) ? ", to bottom" : ", to right");
				out.Put(")");
				break;
			case BrushType::Image:
				out.Put("image(");
				out.Put(static_cast<f32>(brush.image.index));
				out.Put(":");
				out.Put(static_cast<f32>(brush.image.generation));
				out.Put(")");
				break;
			default:
				out.Put("none");
				break;
			}
		}

		void WriteShadows(Sink& out, const Style& style) {
			if (style.boxShadowCount == 0) {
				out.Put("none");
				return;
			}
			for (u8 i = 0; i < style.boxShadowCount; ++i) {
				const BoxShadow& shadow = style.boxShadows[i];
				if (i > 0) {
					out.Put(", ");
				}
				if (shadow.inset) {
					out.Put("inset ");
				}
				out.PutPx(shadow.x);
				out.Put(" ");
				out.PutPx(shadow.y);
				out.Put(" ");
				out.PutPx(shadow.blur);
				out.Put(" ");
				out.PutPx(shadow.spread);
				out.Put(" ");
				out.Put(shadow.color);
			}
		}

		void WriteFilter(Sink& out, const DropShadow& shadow) {
			if (shadow == DropShadow{}) {
				out.Put("none");
				return;
			}
			out.Put("drop-shadow(");
			out.PutPx(shadow.x);
			out.Put(" ");
			out.PutPx(shadow.y);
			out.Put(" ");
			out.PutPx(shadow.blur);
			out.Put(" ");
			out.Put(shadow.color);
			out.Put(")");
		}

		using PropertySetter = void (*)(Style&, std::string_view);
		using PropertyReader = void (*)(const Style&, Sink&);

		struct PropertyBinding {
			std::string_view name;
			StringHash hash;
			PropertySetter setter;
			PropertyReader reader;

			constexpr PropertyBinding(std::string_view propertyName, PropertySetter propertySetter, PropertyReader propertyReader)
				: name(propertyName), hash(propertyName), setter(propertySetter), reader(propertyReader) {}
		};

		constexpr size_t PROPERTY_COUNT = 42;

		Align ParseAlign(std::string_view value, Align fallback) {
			if (value == "center") {
				return Align::Center;
			}
			if (value == "flex-start" || value == "start" || value == "left" || value == "top") {
				return Align::Start;
			}
			if (value == "flex-end" || value == "end" || value == "right" || value == "bottom") {
				return Align::End;
			}
			if (value == "stretch") {
				return Align::Stretch;
			}
			if (value == "space-between") {
				return Align::SpaceBetween;
			}
			if (value == "space-around") {
				return Align::SpaceAround;
			}
			return fallback;
		}

		std::span<const PropertyBinding> GetBindings() {
			static const PropertyBinding bindings[] = {
				{"width", [](Style& s, std::string_view v) { s.width = ParseLength(v); },
				 [](const Style& s, Sink& o) { WriteLength(o, s.width); }},
				{"height", [](Style& s, std::string_view v) { s.height = ParseLength(v); },
				 [](const Style& s, Sink& o) { WriteLength(o, s.height); }},
				{"min-width", [](Style& s, std::string_view v) { s.minWidth = ParseLength(v); },
				 [](const Style& s, Sink& o) { WriteLength(o, s.minWidth); }},
				{"min-height", [](Style& s, std::string_view v) { s.minHeight = ParseLength(v); },
				 [](const Style& s, Sink& o) { WriteLength(o, s.minHeight); }},
				{"max-width", [](Style& s, std::string_view v) { s.maxWidth = ParseLength(v); },
				 [](const Style& s, Sink& o) { WriteLength(o, s.maxWidth); }},
				{"max-height", [](Style& s, std::string_view v) { s.maxHeight = ParseLength(v); },
				 [](const Style& s, Sink& o) { WriteLength(o, s.maxHeight); }},
				{"display", [](Style& s, std::string_view v) { s.display = (v == "none") ? Display::None : Display::Flex; },
				 [](const Style& s, Sink& o) { o.Put(s.display == Display::None ? "none" : "flex"); }},
				{"position", [](Style& s, std::string_view v) { s.position = (v == "absolute") ? Position::Absolute : Position::Relative; },
				 [](const Style& s, Sink& o) { o.Put(s.position == Position::Absolute ? "absolute" : "relative"); }},
				{"flex-direction", [](Style& s, std::string_view v) { s.flexDirection = (v == "row") ? FlexDirection::Row : FlexDirection::Column; },
				 [](const Style& s, Sink& o) { o.Put(s.flexDirection == FlexDirection::Row ? "row" : "column"); }},
				{"flex-grow", [](Style& s, std::string_view v) { s.flexGrow = ParseFloat(v); },
				 [](const Style& s, Sink& o) { o.Put(s.flexGrow); }},
				{"flex-shrink", [](Style& s, std::string_view v) { s.flexShrink = ParseFloat(v); },
				 [](const Style& s, Sink& o) { o.Put(s.flexShrink); }},
				{"overflow", [](Style& s, std::string_view v) {
					 if (v == "hidden") {
						 s.overflow = Overflow::Hidden;
					 } else if (v == "scroll") {
						 s.overflow = Overflow::Scroll;
					 } else {
						 s.overflow = Overflow::Visible;
					 } },
				 [](const Style& s, Sink& o) {
					 o.Put(s.overflow == Overflow::Hidden ? "hidden" : (s.overflow == Overflow::Scroll ? "scroll" : "visible"));
				 }},
				{"align-items", [](Style& s, std::string_view v) { s.alignItems = ParseAlign(v, s.alignItems); },
				 [](const Style& s, Sink& o) { o.Put(AlignText(s.alignItems)); }},
				{"align-self", [](Style& s, std::string_view v) { s.alignSelf = v == "auto" ? Align::Auto : ParseAlign(v, s.alignSelf); },
				 [](const Style& s, Sink& o) { o.Put(AlignText(s.alignSelf)); }},
				{"justify-content", [](Style& s, std::string_view v) { s.justifyContent = ParseAlign(v, s.justifyContent); },
				 [](const Style& s, Sink& o) { o.Put(AlignText(s.justifyContent)); }},
				{"left", [](Style& s, std::string_view v) { s.left = ParseLength(v); },
				 [](const Style& s, Sink& o) { s.left ? WriteLength(o, *s.left) : o.Put("auto"); }},
				{"top", [](Style& s, std::string_view v) { s.top = ParseLength(v); },
				 [](const Style& s, Sink& o) { s.top ? WriteLength(o, *s.top) : o.Put("auto"); }},
				{"right", [](Style& s, std::string_view v) { s.right = ParseLength(v); },
				 [](const Style& s, Sink& o) { s.right ? WriteLength(o, *s.right) : o.Put("auto"); }},
				{"bottom", [](Style& s, std::string_view v) { s.bottom = ParseLength(v); },
				 [](const Style& s, Sink& o) { s.bottom ? WriteLength(o, *s.bottom) : o.Put("auto"); }},

				{"padding", [](Style& s, std::string_view v) { ParseEdges(v, s.padding); },
				 [](const Style& s, Sink& o) { WriteEdges(o, s.padding); }},
				{"margin", [](Style& s, std::string_view v) { ParseEdges(v, s.margin); },
				 [](const Style& s, Sink& o) { WriteEdges(o, s.margin); }},
				{"margin-left", [](Style& s, std::string_view v) { s.margin.left = ParseFloat(v); },
				 [](const Style& s, Sink& o) { o.PutPx(s.margin.left); }},
				{"margin-top", [](Style& s, std::string_view v) { s.margin.top = ParseFloat(v); },
				 [](const Style& s, Sink& o) { o.PutPx(s.margin.top); }},
				{"margin-right", [](Style& s, std::string_view v) { s.margin.right = ParseFloat(v); },
				 [](const Style& s, Sink& o) { o.PutPx(s.margin.right); }},
				{"margin-bottom", [](Style& s, std::string_view v) { s.margin.bottom = ParseFloat(v); },
				 [](const Style& s, Sink& o) { o.PutPx(s.margin.bottom); }},

				{"background-color", [](Style& s, std::string_view v) {
					 s.background.type = BrushType::Solid;
					 s.background.solid = ParseColor(v); },
				 [](const Style& s, Sink& o) { o.Put(s.background.solid); }},
				{"background", [](Style& s, std::string_view v) {
					 if (ParseLinearGradient(v, s.background)) {
						 return;
					 }
					 s.background.type = BrushType::Solid;
					 s.background.solid = ParseColor(v); },
				 [](const Style& s, Sink& o) { WriteBrush(o, s.background); }},
				{"opacity", [](Style& s, std::string_view v) { s.opacity = ParseFloat(v); },
				 [](const Style& s, Sink& o) { o.Put(s.opacity); }},
				{"z-index", [](Style& s, std::string_view v) { s.zIndex = static_cast<i32>(ParseFloat(v)); },
				 [](const Style& s, Sink& o) { o.Put(static_cast<f32>(s.zIndex)); }},
				{"border-radius", [](Style& s, std::string_view v) { ParseRadii(v, s.borderRadius); },
				 [](const Style& s, Sink& o) { WriteRadii(o, s.borderRadius); }},
				{"border-width", [](Style& s, std::string_view v) { ParseEdges(v, s.borderWidth); },
				 [](const Style& s, Sink& o) { WriteEdges(o, s.borderWidth); }},
				{"border-color", [](Style& s, std::string_view v) { s.borderColor = ParseColor(v); },
				 [](const Style& s, Sink& o) { o.Put(s.borderColor); }},
				{"box-shadow", [](Style& s, std::string_view v) {
					 s.boxShadowCount = 0;
					 if (v == "none") {
						 return;
					 }
					 std::string_view shadows[Style::MAX_BOX_SHADOWS];
					 u32 count = SplitInto(v, ',', shadows, Style::MAX_BOX_SHADOWS);
					 for (u32 i = 0; i < count; ++i) {
						 s.AddBoxShadow(ParseSingleShadow(shadows[i]));
					 } },
				 [](const Style& s, Sink& o) { WriteShadows(o, s); }},
				{"filter", [](Style& s, std::string_view v) { ParseFilter(v, s.dropShadow); },
				 [](const Style& s, Sink& o) { WriteFilter(o, s.dropShadow); }},

				{"text-align", [](Style& s, std::string_view v) { s.textAlign = ParseAlign(v, Align::Start); },
				 [](const Style& s, Sink& o) { o.Put(AlignText(s.textAlign)); }},
				{"text-align-vertical", [](Style& s, std::string_view v) { s.textAlignVertical = ParseAlign(v, Align::Start); },
				 [](const Style& s, Sink& o) { o.Put(AlignText(s.textAlignVertical)); }},
				{"text-overflow", [](Style& s, std::string_view v) {
				 if (v == "ellipsis") {
					 s.textOverflow = TextOverflow::Ellipsis;
				 } else if (v == "clip") {
					 s.textOverflow = TextOverflow::Clip;
				 } else {
					 s.textOverflow = TextOverflow::Wrap;
				 } },
				 [](const Style& s, Sink& o) {
					 o.Put(s.textOverflow == TextOverflow::Ellipsis ? "ellipsis" : (s.textOverflow == TextOverflow::Clip ? "clip" : "wrap"));
				 }},
				{"text-outline-width", [](Style& s, std::string_view v) { s.textOutlineWidth = ParseFloat(v); },
				 [](const Style& s, Sink& o) { o.PutPx(s.textOutlineWidth); }},
				{"letter-spacing", [](Style& s, std::string_view v) { s.letterSpacing = ParseFloat(v); },
				 [](const Style& s, Sink& o) { o.PutPx(s.letterSpacing); }},
				{"text-outline-color", [](Style& s, std::string_view v) { s.textOutlineColor = ParseColor(v); },
				 [](const Style& s, Sink& o) { o.Put(s.textOutlineColor); }},

				{"cursor", [](Style& s, std::string_view v) {
					 if (v == "hand" || v == "pointer") {
						 s.cursor = SystemCursor::Hand;
					 } else if (v == "text") {
						 s.cursor = SystemCursor::IBeam;
					 } else if (v == "move") {
						 s.cursor = SystemCursor::ResizeAll;
					 } else if (v == "ew-resize") {
						 s.cursor = SystemCursor::ResizeEW;
					 } else if (v == "ns-resize") {
						 s.cursor = SystemCursor::ResizeNS;
					 } else if (v == "not-allowed") {
						 s.cursor = SystemCursor::NotAllowed;
					 } else if (v == "crosshair") {
						 s.cursor = SystemCursor::Crosshair;
					 } else {
						 s.cursor = SystemCursor::Arrow;
					 } },
				 [](const Style& s, Sink& o) { o.Put(CursorText(s.cursor)); }},
				{"pointer-events", [](Style& s, std::string_view v) { s.pointerEvents = (v != "none"); },
				 [](const Style& s, Sink& o) { o.Put(s.pointerEvents ? "auto" : "none"); }},
			};
			static_assert(std::size(bindings) == PROPERTY_COUNT,
						  "the property table changed size. Update PROPERTY_COUNT (GetStylePropertyNames spans it)");
			return bindings;
		}

		std::span<const std::string_view> GetPropertyNames() {
			static const std::array<std::string_view, PROPERTY_COUNT> names = [] {
				std::array<std::string_view, PROPERTY_COUNT> out{};
				std::span<const PropertyBinding> bindings = GetBindings();
				for (size_t i = 0; i < PROPERTY_COUNT; ++i) {
					out[i] = bindings[i].name;
				}
				return out;
			}();
			return names;
		}

		constexpr std::string_view VarPrefix = "var(";

		bool ApplyDeclaration(Style& style, StringHash name, std::string_view value, bool hasVar, const StyleVars* vars) {
			for (const PropertyBinding& binding : GetBindings()) {
				if (binding.hash != name) {
					continue;
				}
				char expansion[StyleVars::MAX_EXPANSION];
				if (hasVar && vars) {
					value = vars->Expand(value, expansion, sizeof(expansion));
				}
				binding.setter(style, value);
				return true;
			}
			return false;
		}

		void SkipTrivia(const char*& p, const char* end) {
			while (p < end) {
				if (std::isspace(static_cast<unsigned char>(*p))) {
					++p;
				} else if (*p == '/' && p + 1 < end && p[1] == '*') {
					p += 2;
					while (p < end && !(*p == '*' && p + 1 < end && p[1] == '/')) {
						++p;
					}
					p = std::min(p + 2, end);
				} else {
					break;
				}
			}
		}

		template <typename OnDeclaration>
		void ScanDeclarations(const char*& p, const char* end, const OnDeclaration& onDeclaration) {
			while (p < end) {
				SkipTrivia(p, end);
				if (p >= end || *p == '}') {
					if (p < end) {
						++p;
					}
					return;
				}
				const char* runStart = p;
				while (p < end && *p != ':' && *p != '}' && *p != ';') {
					++p;
				}
				if (p >= end || *p != ':') {
					while (p < end && *p != ';' && *p != '}') {
						++p;
					}
					if (p < end && *p == ';') {
						++p;
					}
					continue;
				}
				std::string_view name = Trim(std::string_view(runStart, static_cast<size_t>(p - runStart)));
				++p;
				const char* valueStart = p;
				while (p < end && *p != ';' && *p != '}') {
					++p;
				}
				std::string_view value = Trim(std::string_view(valueStart, static_cast<size_t>(p - valueStart)));
				if (p < end && *p == ';') {
					++p;
				}
				if (!name.empty()) {
					onDeclaration(name, value, std::string_view(runStart, static_cast<size_t>(p - runStart)));
				}
			}
		}

		// onRule must consume through the matching brace
		template <typename OnRule>
		void ScanSheet(std::string_view source, const OnRule& onRule) {
			const char* p = source.data();
			const char* end = p + source.size();
			while (p < end) {
				SkipTrivia(p, end);
				if (p >= end) {
					return;
				}
				const char* selectorStart = p;
				while (p < end && *p != '{') {
					++p;
				}
				if (p >= end) {
					return;
				}
				std::string_view selector = Trim(std::string_view(selectorStart, static_cast<size_t>(p - selectorStart)));
				++p;
				onRule(selector, p, end);
			}
		}

		StyleRule RuleFromSelector(std::string_view selector) {
			StyleRule rule;
			size_t cursor = 0;
			while (cursor < selector.size()) {
				size_t next = selector.find_first_of(".#:", cursor + 1);
				if (next == std::string_view::npos) {
					next = selector.size();
				}
				std::string_view part = Trim(selector.substr(cursor, next - cursor));
				if (!part.empty()) {
					if (part[0] == '.') {
						rule.targetClass = StringHash(part.substr(1));
						rule.specificity += 10;
					} else if (part[0] == '#') {
						rule.targetId = StringHash(part.substr(1));
						rule.specificity += 100;
					} else if (part[0] == ':') {
						std::string_view pseudo = part.substr(1);
						if (pseudo == "hover") {
							rule.pseudoState = PSEUDO_HOVER;
						} else if (pseudo == "active") {
							rule.pseudoState = PSEUDO_ACTIVE;
						} else if (pseudo == "focus") {
							rule.pseudoState = PSEUDO_FOCUS;
						} else if (pseudo == "drop") {
							rule.pseudoState = PSEUDO_DROP;
						}
						rule.specificity += 10;
					} else if (part == "*") {
					} else {
						rule.targetTag = StringHash(part);
						rule.specificity += 1;
					}
				}
				cursor = next;
			}
			return rule;
		}

	} // anonymous namespace

	void StyleVars::Set(StringHash name, std::string_view value) {
		for (Entry& entry : _entries) {
			if (entry.name == name) {
				entry.value.assign(value);
				return;
			}
		}
		_entries.push_back({name, str(value)});
	}

	void StyleVars::Set(std::span<const StyleVarDesc> descs) {
		for (const StyleVarDesc& desc : descs) {
			Set(StringHash(desc.name), desc.value);
		}
	}

	void StyleVars::Clear() {
		_entries.clear();
	}

	bool StyleVars::Has(StringHash name) const {
		return !Get(name).empty();
	}

	std::string_view StyleVars::Get(StringHash name) const {
		for (const Entry& entry : _entries) {
			if (entry.name == name) {
				return entry.value;
			}
		}
		return {};
	}

	u32 StyleVars::GetCount() const {
		return static_cast<u32>(_entries.size());
	}

	void StyleVars::ExpandInto(std::string_view value, char* buffer, size_t capacity, size_t& length, bool& overflow, u32 depth) const {
		auto append = [&](std::string_view text) {
			if (length + text.size() > capacity) {
				overflow = true;
				return;
			}
			std::memcpy(buffer + length, text.data(), text.size());
			length += text.size();
		};

		size_t cursor = 0;
		while (cursor < value.size()) {
			size_t at = value.find(VarPrefix, cursor);
			if (at == std::string_view::npos) {
				append(value.substr(cursor));
				return;
			}
			append(value.substr(cursor, at - cursor));

			size_t close = value.find(')', at + VarPrefix.size());
			if (close == std::string_view::npos) {
				append(value.substr(at));
				return;
			}
			std::string_view name = Trim(value.substr(at + VarPrefix.size(), close - at - VarPrefix.size()));
			std::string_view resolved = Get(StringHash(name));
			if (resolved.empty()) {
				SLT_LOG_WARN("UI", "stylesheet: undefined variable '{}'", str(name));
				append(value.substr(at, close - at + 1));
			} else if (depth >= MAX_DEPTH) {
				SLT_LOG_WARN("UI", "stylesheet: variable '{}' nests past {} levels (cycle?)", str(name), MAX_DEPTH);
				append(value.substr(at, close - at + 1));
			} else {
				ExpandInto(resolved, buffer, capacity, length, overflow, depth + 1);
			}
			cursor = close + 1;
		}
	}

	std::string_view StyleVars::Expand(std::string_view value, char* buffer, size_t capacity) const {
		if (value.find(VarPrefix) == std::string_view::npos) {
			return value;
		}
		size_t length = 0;
		bool overflow = false;
		ExpandInto(value, buffer, capacity, length, overflow, 0);
		if (overflow) {
			SLT_LOG_WARN("UI", "stylesheet: value expands past {} bytes, left unresolved: '{}'", capacity, str(value));
			return value;
		}
		return std::string_view(buffer, length);
	}

	Color StyleVars::GetColor(StringHash name, Color fallback) const {
		std::string_view value = Get(name);
		if (value.empty()) {
			return fallback;
		}
		char buffer[MAX_EXPANSION];
		return ParseColor(Expand(value, buffer, sizeof(buffer)));
	}

	f32 StyleVars::GetFloat(StringHash name, f32 fallback) const {
		std::string_view value = Get(name);
		if (value.empty()) {
			return fallback;
		}
		char buffer[MAX_EXPANSION];
		return ParseFloat(Expand(value, buffer, sizeof(buffer)));
	}

	void StyleSheet::ApplyRule(Style& style, const StyleRule& rule, const StyleVars* vars) {
		for (const StyleProperty& property : rule.properties) {
			if (!ApplyDeclaration(style, property.name, property.value, property.hasVar, vars)) {
				SLT_LOG_TRACE("UI", "stylesheet: unknown property (hash {:x})", property.name.GetValue());
			}
		}
	}

	void StyleSheet::Clear() {
		_rules.clear();
	}

	void StyleSheet::Parse(std::string_view source) {
		ScanSheet(source, [this](std::string_view selector, const char*& p, const char* end) {
			StyleRule rule = RuleFromSelector(selector);
			ScanDeclarations(p, end, [&rule](std::string_view name, std::string_view value, std::string_view) {
				StyleProperty property;
				property.name = StringHash(name);
				property.value = str(value);
				property.hasVar = value.find(VarPrefix) != std::string_view::npos;
				rule.properties.push_back(std::move(property));
			});
			_rules.push_back(std::move(rule));
		});

		std::stable_sort(_rules.begin(), _rules.end(), [](const StyleRule& a, const StyleRule& b) {
			return a.specificity < b.specificity;
		});
	}

	void StyleSheet::SplitSource(std::string_view source, Vector<StyleRuleSource>& out) {
		out.clear();
		ScanSheet(source, [&out](std::string_view selector, const char*& p, const char* end) {
			out.push_back(StyleRuleSource{selector, {}, Vector<StyleDeclarationSource>{GetMemoryResource(MemTag::UI)}});
			size_t index = out.size() - 1;
			const char* bodyStart = p;
			ScanDeclarations(p, end,
							 [&out, index](std::string_view name, std::string_view value, std::string_view text) {
								 out[index].declarations.push_back(StyleDeclarationSource{name, value, text});
							 });
			const char* bodyEnd = (p > bodyStart && p[-1] == '}') ? p - 1 : p;
			out[index].body = std::string_view(bodyStart, static_cast<size_t>(bodyEnd - bodyStart));
		});
	}

	void StyleSheet::SplitDeclarations(std::string_view declarations, Vector<StyleDeclarationSource>& out) {
		out.clear();
		const char* p = declarations.data();
		const char* end = p + declarations.size();
		ScanDeclarations(p, end, [&out](std::string_view name, std::string_view value, std::string_view text) {
			out.push_back(StyleDeclarationSource{name, value, text});
		});
	}

	std::span<const std::string_view> GetStylePropertyNames() {
		return GetPropertyNames();
	}

	std::string_view ReadStyleProperty(const Style& style, StringHash name, char* buffer, size_t capacity) {
		if (!buffer || capacity == 0) {
			return {};
		}
		for (const PropertyBinding& binding : GetBindings()) {
			if (binding.hash == name) {
				Sink sink{buffer, capacity, 0};
				binding.reader(style, sink);
				return std::string_view(buffer, sink.length);
			}
		}
		return {};
	}

	bool ApplyStyleDeclaration(Style& style, StringHash name, std::string_view value, const StyleVars* vars) {
		return ApplyDeclaration(style, name, value, value.find(VarPrefix) != std::string_view::npos, vars);
	}

} // namespace Shalltear::UI
