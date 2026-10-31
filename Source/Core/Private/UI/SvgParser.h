#pragma once

#include "Serialization/XML.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/VectorAsset.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace Shalltear::Svg {

	inline constexpr f32 SVG_PI = 3.14159265358979323846f;
	inline constexpr f32 SVG_TWO_PI = 6.28318530717958647692f;

	inline constexpr f32 SVG_KAPPA = 0.5522847498307933f;

	struct Affine {
		f32 a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;

		[[nodiscard]] Vec2 Apply(Vec2 p) const {
			return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
		}

		[[nodiscard]] Affine Then(const Affine& child) const {
			Affine out;
			out.a = a * child.a + c * child.b;
			out.b = b * child.a + d * child.b;
			out.c = a * child.c + c * child.d;
			out.d = b * child.c + d * child.d;
			out.e = a * child.e + c * child.f + e;
			out.f = b * child.e + d * child.f + f;
			return out;
		}

		[[nodiscard]] bool operator==(const Affine&) const = default;
	};

	namespace Detail {

		[[nodiscard]] inline bool IsSpace(char c) {
			return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',';
		}

		[[nodiscard]] inline std::string_view Trim(std::string_view text) {
			while (!text.empty() && IsSpace(text.front())) {
				text.remove_prefix(1);
			}
			while (!text.empty() && IsSpace(text.back())) {
				text.remove_suffix(1);
			}
			return text;
		}

		class Scanner {
		public:
			explicit Scanner(std::string_view text) : _text(text) {}

			void SkipSeparators() {
				while (_at < _text.size() && IsSpace(_text[_at])) {
					++_at;
				}
			}

			[[nodiscard]] bool AtEnd() {
				SkipSeparators();
				return _at >= _text.size();
			}

			[[nodiscard]] char Peek() const { return _at < _text.size() ? _text[_at] : '\0'; }
			void Advance() { ++_at; }

			bool Number(f32& out) {
				SkipSeparators();
				const size_t start = _at;
				if (_at < _text.size() && (_text[_at] == '+' || _text[_at] == '-')) {
					++_at;
				}
				bool digits = false;
				while (_at < _text.size() && _text[_at] >= '0' && _text[_at] <= '9') {
					++_at;
					digits = true;
				}
				if (_at < _text.size() && _text[_at] == '.') {
					++_at;
					while (_at < _text.size() && _text[_at] >= '0' && _text[_at] <= '9') {
						++_at;
						digits = true;
					}
				}
				if (!digits) {
					_at = start;
					return false;
				}
				if (_at < _text.size() && (_text[_at] == 'e' || _text[_at] == 'E')) {
					const size_t exponent = _at;
					++_at;
					if (_at < _text.size() && (_text[_at] == '+' || _text[_at] == '-')) {
						++_at;
					}
					bool exponentDigits = false;
					while (_at < _text.size() && _text[_at] >= '0' && _text[_at] <= '9') {
						++_at;
						exponentDigits = true;
					}
					if (!exponentDigits) {
						_at = exponent;
					}
				}

				const size_t length = _at - start;
				if (length >= sizeof(_scratch)) {
					_at = start;
					return false;
				}
				std::memcpy(_scratch, _text.data() + start, length);
				_scratch[length] = '\0';
				out = std::strtof(_scratch, nullptr);
				return true;
			}

			bool Flag(bool& out) {
				SkipSeparators();
				if (_at >= _text.size() || (_text[_at] != '0' && _text[_at] != '1')) {
					return false;
				}
				out = _text[_at] == '1';
				++_at;
				return true;
			}
		private:
			std::string_view _text;
			size_t _at = 0;
			char _scratch[64] = {};
		};

		struct NamedColor {
			std::string_view name;
			u32 rgb;
		};
		inline constexpr NamedColor NamedColors[] = {
			{"black", 0x000000}, {"silver", 0xC0C0C0}, {"gray", 0x808080}, {"grey", 0x808080}, {"white", 0xFFFFFF}, {"maroon", 0x800000}, {"red", 0xFF0000}, {"purple", 0x800080}, {"fuchsia", 0xFF00FF}, {"green", 0x008000}, {"lime", 0x00FF00}, {"olive", 0x808000}, {"yellow", 0xFFFF00}, {"navy", 0x000080}, {"blue", 0x0000FF}, {"teal", 0x008080}, {"aqua", 0x00FFFF}, {"cyan", 0x00FFFF}, {"magenta", 0xFF00FF}, {"orange", 0xFFA500}};

		[[nodiscard]] inline bool ParseColor(std::string_view text, Color& out) {
			if (!text.empty() && text.front() == '#') {
				return Color::TryParseHex(text, out);
			}
			for (const NamedColor& named : NamedColors) {
				if (named.name == text) {
					out = Color(static_cast<f32>((named.rgb >> 16) & 0xFF) / 255.0f,
								static_cast<f32>((named.rgb >> 8) & 0xFF) / 255.0f,
								static_cast<f32>(named.rgb & 0xFF) / 255.0f, 1.0f);
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] inline bool IsRefusedAttribute(std::string_view name) {
			return name == "style" || name == "clip-path" || name == "filter" ||
				   name == "transform-origin" || name == "mask-type" ||
				   name == "paint-order" || name == "vector-effect";
		}

	} // namespace Detail

	struct PaintState {
		Affine transform;
		Color fill = Color(0.0f, 0.0f, 0.0f, 1.0f);
		bool fillNone = false;
		bool fillCurrentColor = false;
		f32 opacity = 1.0f;
		const XML::XMLNode* maskNode = nullptr;
		Affine maskTransform;
	};

	class Reader {
	public:
		bool Build(const XML::XMLNode& root, VectorAsset& out, str& error) {
			_out = &out;
			_error.clear();
			_maskNodes.clear();
			_maskInstances.clear();

			if (root.name != "svg") {
				Fail("root element isn't <svg>", root.name);
				error = _error;
				return false;
			}
			if (!ReadViewBox(root)) {
				error = _error;
				return false;
			}

			CollectMaskNodes(root);

			PaintState initial;
			initial.transform = _viewTransform;
			if (_error.empty() && ApplyNodeState(root, initial)) {
				WalkPaint(root, initial);
			}
			if (!_error.empty()) {
				error = _error;
				ClearOutput();
				return false;
			}
			if (out.shapes.empty()) {
				Fail("nothing paintable", "");
				error = _error;
				ClearOutput();
				return false;
			}
			return true;
		}
	private:
		VectorAsset* _out = nullptr;
		str _error;
		Affine _viewTransform;

		struct MaskNode {
			std::string_view id;
			const XML::XMLNode* node;
		};
		Vector<MaskNode> _maskNodes{GetMemoryResource(MemTag::Asset)};

		struct MaskInstance {
			const XML::XMLNode* node;
			Affine transform;
			u32 index;
		};
		Vector<MaskInstance> _maskInstances{GetMemoryResource(MemTag::Asset)};

		Vector<VectorVerb>* _verbSink = nullptr;
		Vector<Vec2>* _pointSink = nullptr;

		void ClearOutput() {
			_out->viewSize = {};
			_out->verbs.clear();
			_out->points.clear();
			_out->shapes.clear();
			_out->masks.clear();
			_out->maskShapes.clear();
			_out->maskVerbs.clear();
			_out->maskPoints.clear();
			_out->slotDefaults.clear();
		}

		void Fail(std::string_view what, std::string_view detail) {
			if (!_error.empty()) {
				return;
			}
			_error = str(what);
			if (!detail.empty()) {
				_error += str(": '") + str(detail) + str("'");
			}
		}

		bool ReadViewBox(const XML::XMLNode& root) {
			std::string_view box = root.Attribute("viewBox");
			if (!box.empty()) {
				Detail::Scanner scanner(box);
				f32 values[4] = {};
				for (f32& value : values) {
					if (!scanner.Number(value)) {
						Fail("viewBox needs four numbers", box);
						return false;
					}
				}
				if (values[2] <= 0.0f || values[3] <= 0.0f) {
					Fail("viewBox has no area", box);
					return false;
				}
				_out->viewSize = {values[2], values[3]};
				_viewTransform.e = -values[0];
				_viewTransform.f = -values[1];
				return true;
			}

			f32 width = 0.0f;
			f32 height = 0.0f;
			Detail::Scanner widthScanner(root.Attribute("width"));
			Detail::Scanner heightScanner(root.Attribute("height"));
			if (!widthScanner.Number(width) || !heightScanner.Number(height) ||
				!widthScanner.AtEnd() || !heightScanner.AtEnd() || width <= 0.0f || height <= 0.0f) {
				Fail("<svg> needs a viewBox, or a unitless width and height", root.Attribute("width"));
				return false;
			}
			_out->viewSize = {width, height};
			return true;
		}

		void CollectMaskNodes(const XML::XMLNode& node) {
			for (const XML::XMLNode& child : node.children) {
				if (child.name == "mask") {
					std::string_view id = child.Attribute("id");
					if (id.empty()) {
						Fail("<mask> has no id", "");
						return;
					}
					std::string_view units = child.Attribute("maskUnits", "objectBoundingBox");
					if (units != "userSpaceOnUse") {
						Fail("<mask> needs maskUnits=\"userSpaceOnUse\"", units);
						return;
					}
					_maskNodes.push_back({id, &child});
					continue;
				}
				if (child.name == "g" || child.name == "defs") {
					CollectMaskNodes(child);
					if (!_error.empty()) {
						return;
					}
				}
			}
		}

		u32 InstantiateMask(const XML::XMLNode& node, const Affine& transform) {
			for (const MaskInstance& instance : _maskInstances) {
				if (instance.node == &node && instance.transform == transform) {
					return instance.index;
				}
			}

			VectorMask mask;
			mask.firstShape = static_cast<u32>(_out->maskShapes.size());

			PaintState state;
			state.transform = transform;
			if (!ApplyNodeState(node, state)) {
				return INVALID_VECTOR_INDEX;
			}
			_verbSink = &_out->maskVerbs;
			_pointSink = &_out->maskPoints;
			WalkMaskContent(node, state);
			_verbSink = nullptr;
			_pointSink = nullptr;
			if (!_error.empty()) {
				return INVALID_VECTOR_INDEX;
			}

			mask.shapeCount = static_cast<u32>(_out->maskShapes.size()) - mask.firstShape;
			const u32 index = static_cast<u32>(_out->masks.size());
			_out->masks.push_back(mask);
			_maskInstances.push_back({&node, transform, index});
			return index;
		}

		void WalkMaskContent(const XML::XMLNode& node, const PaintState& inherited) {
			for (const XML::XMLNode& child : node.children) {
				if (!_error.empty()) {
					return;
				}
				if (IsIgnorableNode(child.name)) {
					continue;
				}
				PaintState state = inherited;
				if (!ApplyNodeState(child, state)) {
					return;
				}
				if (child.name == "g") {
					WalkMaskContent(child, state);
					continue;
				}
				const u32 firstVerb = static_cast<u32>(_out->maskVerbs.size());
				const u32 firstPoint = static_cast<u32>(_out->maskPoints.size());
				if (!EmitGeometry(child, state)) {
					return;
				}
				if (_out->maskVerbs.size() == firstVerb) {
					continue;
				}
				VectorMaskShape shape;
				shape.firstVerb = firstVerb;
				shape.verbCount = static_cast<u32>(_out->maskVerbs.size()) - firstVerb;
				shape.firstPoint = firstPoint;
				shape.pointCount = static_cast<u32>(_out->maskPoints.size()) - firstPoint;
				shape.color = state.fill;
				shape.color.a *= state.opacity;
				_out->maskShapes.push_back(shape);
			}
		}

		[[nodiscard]] static bool IsIgnorableNode(std::string_view name) {
			return name == "title" || name == "desc" || name == "metadata" || name == "defs" || name == "mask";
		}

		void WalkPaint(const XML::XMLNode& node, const PaintState& inherited) {
			for (const XML::XMLNode& child : node.children) {
				if (!_error.empty()) {
					return;
				}
				if (IsIgnorableNode(child.name)) {
					continue;
				}
				PaintState state = inherited;
				if (!ApplyNodeState(child, state)) {
					return;
				}
				if (child.name == "g") {
					WalkPaint(child, state);
					continue;
				}

				const u32 firstVerb = static_cast<u32>(_out->verbs.size());
				const u32 firstPoint = static_cast<u32>(_out->points.size());
				_verbSink = &_out->verbs;
				_pointSink = &_out->points;
				const bool emitted = EmitGeometry(child, state);
				_verbSink = nullptr;
				_pointSink = nullptr;
				if (!emitted) {
					return;
				}
				if (_out->verbs.size() == firstVerb) {
					continue;
				}

				u32 slot = 0;
				if (!ResolveSlot(state, slot)) {
					return;
				}
				u32 mask = INVALID_VECTOR_INDEX;
				if (state.maskNode) {
					mask = InstantiateMask(*state.maskNode, state.maskTransform);
					if (mask == INVALID_VECTOR_INDEX) {
						return;
					}
				}

				VectorShape shape;
				shape.firstVerb = firstVerb;
				shape.verbCount = static_cast<u32>(_out->verbs.size()) - firstVerb;
				shape.firstPoint = firstPoint;
				shape.pointCount = static_cast<u32>(_out->points.size()) - firstPoint;
				shape.slot = slot;
				shape.opacity = std::clamp(state.fill.a * state.opacity, 0.0f, 1.0f);
				shape.mask = mask;
				_out->shapes.push_back(shape);
			}
		}

		bool ResolveSlot(const PaintState& state, u32& outSlot) {
			const Color key = state.fillCurrentColor
								  ? Color::White()
								  : Color(state.fill.r, state.fill.g, state.fill.b, 1.0f);
			for (size_t i = 0; i < _out->slotDefaults.size(); ++i) {
				const Color& existing = _out->slotDefaults[i];
				if (existing.r == key.r && existing.g == key.g && existing.b == key.b) {
					outSlot = static_cast<u32>(i);
					return true;
				}
			}
			if (_out->slotDefaults.size() >= UI::MAX_ICON_SLOTS) {
				Fail("more distinct fill colors than the palette holds (4)", key.ToHex());
				return false;
			}
			outSlot = static_cast<u32>(_out->slotDefaults.size());
			_out->slotDefaults.push_back(key);
			return true;
		}

		bool ApplyNodeState(const XML::XMLNode& node, PaintState& state) {
			for (const XML::XMLAttribute& attribute : node.attributes) {
				if (Detail::IsRefusedAttribute(attribute.name)) {
					Fail("attribute isn't in the subset", attribute.name);
					return false;
				}
				if (attribute.name.starts_with("stroke") && Detail::Trim(attribute.value) != "none" &&
					!Detail::Trim(attribute.value).empty()) {
					Fail("stroke isn't in the subset (convert strokes to paths)", attribute.name);
					return false;
				}
			}

			std::string_view transform = node.Attribute("transform");
			if (!transform.empty()) {
				Affine local;
				if (!ParseTransform(transform, local)) {
					return false;
				}
				state.transform = state.transform.Then(local);
			}

			std::string_view fill = node.Attribute("fill");
			if (!fill.empty()) {
				fill = Detail::Trim(fill);
				if (fill == "none") {
					state.fillNone = true;
				} else if (fill == "currentColor") {
					state.fillNone = false;
					state.fillCurrentColor = true;
					state.fill = Color::White();
				} else if (fill.starts_with("url(")) {
					Fail("gradients and patterns aren't in the subset", fill);
					return false;
				} else {
					Color color;
					if (!Detail::ParseColor(fill, color)) {
						Fail("unrecognized color", fill);
						return false;
					}
					state.fillNone = false;
					state.fillCurrentColor = false;
					state.fill = color;
				}
			}

			f32 number = 0.0f;
			if (ReadNumberAttribute(node, "fill-opacity", number)) {
				state.opacity *= std::clamp(number, 0.0f, 1.0f);
			}
			if (ReadNumberAttribute(node, "opacity", number)) {
				state.opacity *= std::clamp(number, 0.0f, 1.0f);
			}

			std::string_view fillRule = node.Attribute("fill-rule");
			if (!fillRule.empty() && Detail::Trim(fillRule) != "nonzero") {
				Fail("only the nonzero fill rule is in the subset", fillRule);
				return false;
			}

			std::string_view mask = node.Attribute("mask");
			if (!mask.empty()) {
				std::string_view id = MaskReference(mask);
				if (id.empty()) {
					Fail("mask must be url(#id)", mask);
					return false;
				}
				const XML::XMLNode* found = nullptr;
				for (const MaskNode& entry : _maskNodes) {
					if (entry.id == id) {
						found = entry.node;
						break;
					}
				}
				if (!found) {
					Fail("mask references an id no <mask> defines", id);
					return false;
				}
				state.maskNode = found;
				state.maskTransform = state.transform;
			}
			return true;
		}

		[[nodiscard]] static std::string_view MaskReference(std::string_view value) {
			value = Detail::Trim(value);
			if (!value.starts_with("url(#") || !value.ends_with(")")) {
				return {};
			}
			return value.substr(5, value.size() - 6);
		}

		static bool ReadNumberAttribute(const XML::XMLNode& node, std::string_view name, f32& out) {
			std::string_view text = node.Attribute(name);
			if (text.empty()) {
				return false;
			}
			Detail::Scanner scanner(text);
			return scanner.Number(out);
		}

		bool ParseTransform(std::string_view text, Affine& out) {
			out = Affine{};
			size_t at = 0;
			while (at < text.size()) {
				while (at < text.size() && Detail::IsSpace(text[at])) {
					++at;
				}
				if (at >= text.size()) {
					break;
				}
				const size_t nameStart = at;
				while (at < text.size() && text[at] != '(' && !Detail::IsSpace(text[at])) {
					++at;
				}
				std::string_view name = text.substr(nameStart, at - nameStart);
				while (at < text.size() && Detail::IsSpace(text[at])) {
					++at;
				}
				if (at >= text.size() || text[at] != '(') {
					Fail("malformed transform", text);
					return false;
				}
				const size_t argsStart = ++at;
				const size_t close = text.find(')', at);
				if (close == std::string_view::npos) {
					Fail("unterminated transform", text);
					return false;
				}
				std::string_view args = text.substr(argsStart, close - argsStart);
				at = close + 1;

				f32 values[6] = {};
				u32 count = 0;
				Detail::Scanner scanner(args);
				while (count < 6 && scanner.Number(values[count])) {
					++count;
				}
				if (!scanner.AtEnd()) {
					Fail("transform takes at most 6 numbers", text);
					return false;
				}

				Affine step;
				if (name == "matrix" && count == 6) {
					step = {values[0], values[1], values[2], values[3], values[4], values[5]};
				} else if (name == "translate" && (count == 1 || count == 2)) {
					step.e = values[0];
					step.f = count == 2 ? values[1] : 0.0f;
				} else if (name == "scale" && (count == 1 || count == 2)) {
					step.a = values[0];
					step.d = count == 2 ? values[1] : values[0];
				} else if (name == "rotate" && (count == 1 || count == 3)) {
					const f32 radians = values[0] * SVG_PI / 180.0f;
					const f32 cosine = std::cos(radians);
					const f32 sine = std::sin(radians);
					const Affine rotation{cosine, sine, -sine, cosine, 0.0f, 0.0f};
					if (count == 3) {
						Affine toCenter;
						toCenter.e = values[1];
						toCenter.f = values[2];
						Affine fromCenter;
						fromCenter.e = -values[1];
						fromCenter.f = -values[2];
						step = toCenter.Then(rotation).Then(fromCenter);
					} else {
						step = rotation;
					}
				} else if (name == "skewX" && count == 1) {
					step.c = std::tan(values[0] * SVG_PI / 180.0f);
				} else if (name == "skewY" && count == 1) {
					step.b = std::tan(values[0] * SVG_PI / 180.0f);
				} else {
					Fail("unrecognized transform", name);
					return false;
				}
				out = out.Then(step);
			}
			return true;
		}

		void MoveTo(Vec2 point, const Affine& transform) {
			_verbSink->push_back(VectorVerb::Move);
			_pointSink->push_back(transform.Apply(point));
		}
		void LineTo(Vec2 point, const Affine& transform) {
			_verbSink->push_back(VectorVerb::Line);
			_pointSink->push_back(transform.Apply(point));
		}
		void CubicTo(Vec2 c0, Vec2 c1, Vec2 to, const Affine& transform) {
			_verbSink->push_back(VectorVerb::Cubic);
			_pointSink->push_back(transform.Apply(c0));
			_pointSink->push_back(transform.Apply(c1));
			_pointSink->push_back(transform.Apply(to));
		}
		void ClosePath() { _verbSink->push_back(VectorVerb::Close); }

		bool EmitGeometry(const XML::XMLNode& node, const PaintState& state) {
			if (state.fillNone) {
				return true;
			}
			if (node.name == "path") {
				return EmitPath(node.Attribute("d"), state.transform);
			}
			if (node.name == "rect") {
				return EmitRect(node, state.transform);
			}
			if (node.name == "circle" || node.name == "ellipse") {
				return EmitEllipse(node, state.transform);
			}
			if (node.name == "polygon" || node.name == "polyline") {
				return EmitPolygon(node.Attribute("points"), state.transform);
			}
			if (node.name == "line") {
				Fail("<line> has no fill (stroke isn't in the subset)", node.name);
				return false;
			}
			Fail("element isn't in the subset", node.name);
			return false;
		}

		bool EmitRect(const XML::XMLNode& node, const Affine& transform) {
			f32 x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f, rx = 0.0f, ry = 0.0f;
			ReadNumberAttribute(node, "x", x);
			ReadNumberAttribute(node, "y", y);
			if (!ReadNumberAttribute(node, "width", width) || !ReadNumberAttribute(node, "height", height)) {
				Fail("<rect> needs width and height", node.name);
				return false;
			}
			if (width <= 0.0f || height <= 0.0f) {
				return true;
			}
			const bool hasRx = ReadNumberAttribute(node, "rx", rx);
			const bool hasRy = ReadNumberAttribute(node, "ry", ry);
			if (hasRx && !hasRy) {
				ry = rx;
			} else if (hasRy && !hasRx) {
				rx = ry;
			}
			rx = std::min(std::max(rx, 0.0f), width * 0.5f);
			ry = std::min(std::max(ry, 0.0f), height * 0.5f);

			if (rx <= 0.0f || ry <= 0.0f) {
				MoveTo({x, y}, transform);
				LineTo({x + width, y}, transform);
				LineTo({x + width, y + height}, transform);
				LineTo({x, y + height}, transform);
				ClosePath();
				return true;
			}

			const f32 cx = rx * SVG_KAPPA;
			const f32 cy = ry * SVG_KAPPA;
			const f32 right = x + width;
			const f32 bottom = y + height;
			MoveTo({x + rx, y}, transform);
			LineTo({right - rx, y}, transform);
			CubicTo({right - rx + cx, y}, {right, y + ry - cy}, {right, y + ry}, transform);
			LineTo({right, bottom - ry}, transform);
			CubicTo({right, bottom - ry + cy}, {right - rx + cx, bottom}, {right - rx, bottom}, transform);
			LineTo({x + rx, bottom}, transform);
			CubicTo({x + rx - cx, bottom}, {x, bottom - ry + cy}, {x, bottom - ry}, transform);
			LineTo({x, y + ry}, transform);
			CubicTo({x, y + ry - cy}, {x + rx - cx, y}, {x + rx, y}, transform);
			ClosePath();
			return true;
		}

		bool EmitEllipse(const XML::XMLNode& node, const Affine& transform) {
			f32 cx = 0.0f, cy = 0.0f, rx = 0.0f, ry = 0.0f;
			ReadNumberAttribute(node, "cx", cx);
			ReadNumberAttribute(node, "cy", cy);
			if (node.name == "circle") {
				f32 radius = 0.0f;
				if (!ReadNumberAttribute(node, "r", radius)) {
					Fail("<circle> needs r", node.name);
					return false;
				}
				rx = radius;
				ry = radius;
			} else if (!ReadNumberAttribute(node, "rx", rx) || !ReadNumberAttribute(node, "ry", ry)) {
				Fail("<ellipse> needs rx and ry", node.name);
				return false;
			}
			if (rx <= 0.0f || ry <= 0.0f) {
				return true;
			}
			const f32 ox = rx * SVG_KAPPA;
			const f32 oy = ry * SVG_KAPPA;
			MoveTo({cx + rx, cy}, transform);
			CubicTo({cx + rx, cy + oy}, {cx + ox, cy + ry}, {cx, cy + ry}, transform);
			CubicTo({cx - ox, cy + ry}, {cx - rx, cy + oy}, {cx - rx, cy}, transform);
			CubicTo({cx - rx, cy - oy}, {cx - ox, cy - ry}, {cx, cy - ry}, transform);
			CubicTo({cx + ox, cy - ry}, {cx + rx, cy - oy}, {cx + rx, cy}, transform);
			ClosePath();
			return true;
		}

		bool EmitPolygon(std::string_view text, const Affine& transform) {
			Detail::Scanner scanner(text);
			bool first = true;
			f32 x = 0.0f, y = 0.0f;
			while (scanner.Number(x)) {
				if (!scanner.Number(y)) {
					Fail("points has an odd number of coordinates", text);
					return false;
				}
				if (first) {
					MoveTo({x, y}, transform);
					first = false;
				} else {
					LineTo({x, y}, transform);
				}
			}
			if (!scanner.AtEnd()) {
				Fail("malformed points", text);
				return false;
			}
			if (!first) {
				ClosePath();
			}
			return true;
		}

		bool EmitPath(std::string_view data, const Affine& transform) {
			if (data.empty()) {
				return true;
			}
			Detail::Scanner scanner(data);
			Vec2 pen{};
			Vec2 subpathStart{};
			Vec2 lastControl{};
			char lastCommand = '\0';
			char command = '\0';
			bool open = false;

			auto number = [&](f32& value) {
				if (!scanner.Number(value)) {
					Fail("path data ran out of numbers", data.substr(0, 24));
					return false;
				}
				return true;
			};
			auto ensureOpen = [&] {
				if (!open) {
					MoveTo(pen, transform);
					subpathStart = pen;
					open = true;
				}
			};

			while (!scanner.AtEnd()) {
				const char peek = scanner.Peek();
				if ((peek >= 'A' && peek <= 'Z') || (peek >= 'a' && peek <= 'z')) {
					command = peek;
					scanner.Advance();
				} else if (command == '\0') {
					Fail("path data doesn't start with a command", data.substr(0, 24));
					return false;
				} else if (command == 'M') {
					command = 'L';
				} else if (command == 'm') {
					command = 'l';
				}

				const bool relative = command >= 'a' && command <= 'z';
				const Vec2 origin = relative ? pen : Vec2{0.0f, 0.0f};

				switch (command) {
				case 'M':
				case 'm': {
					f32 x = 0.0f, y = 0.0f;
					if (!number(x) || !number(y)) {
						return false;
					}
					if (open) {
						ClosePath();
					}
					pen = {origin.x + x, origin.y + y};
					subpathStart = pen;
					MoveTo(pen, transform);
					open = true;
					break;
				}
				case 'L':
				case 'l': {
					f32 x = 0.0f, y = 0.0f;
					if (!number(x) || !number(y)) {
						return false;
					}
					ensureOpen();
					pen = {origin.x + x, origin.y + y};
					LineTo(pen, transform);
					break;
				}
				case 'H':
				case 'h': {
					f32 x = 0.0f;
					if (!number(x)) {
						return false;
					}
					ensureOpen();
					pen = {origin.x + x, pen.y};
					LineTo(pen, transform);
					break;
				}
				case 'V':
				case 'v': {
					f32 y = 0.0f;
					if (!number(y)) {
						return false;
					}
					ensureOpen();
					pen = {pen.x, origin.y + y};
					LineTo(pen, transform);
					break;
				}
				case 'C':
				case 'c': {
					f32 values[6] = {};
					for (f32& value : values) {
						if (!number(value)) {
							return false;
						}
					}
					ensureOpen();
					const Vec2 c0{origin.x + values[0], origin.y + values[1]};
					const Vec2 c1{origin.x + values[2], origin.y + values[3]};
					pen = {origin.x + values[4], origin.y + values[5]};
					CubicTo(c0, c1, pen, transform);
					lastControl = c1;
					break;
				}
				case 'S':
				case 's': {
					f32 values[4] = {};
					for (f32& value : values) {
						if (!number(value)) {
							return false;
						}
					}
					ensureOpen();
					const bool smooth = lastCommand == 'C' || lastCommand == 'c' ||
										lastCommand == 'S' || lastCommand == 's';
					const Vec2 c0 = smooth ? Vec2{2.0f * pen.x - lastControl.x, 2.0f * pen.y - lastControl.y} : pen;
					const Vec2 c1{origin.x + values[0], origin.y + values[1]};
					pen = {origin.x + values[2], origin.y + values[3]};
					CubicTo(c0, c1, pen, transform);
					lastControl = c1;
					break;
				}
				case 'Q':
				case 'q': {
					f32 values[4] = {};
					for (f32& value : values) {
						if (!number(value)) {
							return false;
						}
					}
					ensureOpen();
					const Vec2 control{origin.x + values[0], origin.y + values[1]};
					const Vec2 to{origin.x + values[2], origin.y + values[3]};
					EmitQuadratic(pen, control, to, transform);
					lastControl = control;
					pen = to;
					break;
				}
				case 'T':
				case 't': {
					f32 x = 0.0f, y = 0.0f;
					if (!number(x) || !number(y)) {
						return false;
					}
					ensureOpen();
					const bool smooth = lastCommand == 'Q' || lastCommand == 'q' ||
										lastCommand == 'T' || lastCommand == 't';
					const Vec2 control = smooth ? Vec2{2.0f * pen.x - lastControl.x, 2.0f * pen.y - lastControl.y} : pen;
					const Vec2 to{origin.x + x, origin.y + y};
					EmitQuadratic(pen, control, to, transform);
					lastControl = control;
					pen = to;
					break;
				}
				case 'A':
				case 'a': {
					f32 rx = 0.0f, ry = 0.0f, rotation = 0.0f, x = 0.0f, y = 0.0f;
					bool largeArc = false;
					bool sweep = false;
					if (!number(rx) || !number(ry) || !number(rotation)) {
						return false;
					}
					if (!scanner.Flag(largeArc) || !scanner.Flag(sweep)) {
						Fail("arc flags must be 0 or 1", data.substr(0, 24));
						return false;
					}
					if (!number(x) || !number(y)) {
						return false;
					}
					ensureOpen();
					const Vec2 to{origin.x + x, origin.y + y};
					EmitArc(pen, to, rx, ry, rotation, largeArc, sweep, transform);
					pen = to;
					break;
				}
				case 'Z':
				case 'z':
					if (open) {
						ClosePath();
						open = false;
					}
					pen = subpathStart;
					break;
				default:
					Fail("unrecognized path command", data.substr(0, 24));
					return false;
				}
				lastCommand = command;
			}
			if (open) {
				ClosePath();
			}
			return true;
		}

		void EmitQuadratic(Vec2 from, Vec2 control, Vec2 to, const Affine& transform) {
			const Vec2 c0 = from + (control - from) * (2.0f / 3.0f);
			const Vec2 c1 = to + (control - to) * (2.0f / 3.0f);
			CubicTo(c0, c1, to, transform);
		}

		void EmitArc(Vec2 from, Vec2 to, f32 rx, f32 ry, f32 rotationDegrees,
					 bool largeArc, bool sweep, const Affine& transform) {
			if (rx == 0.0f || ry == 0.0f || (from.x == to.x && from.y == to.y)) {
				LineTo(to, transform);
				return;
			}
			rx = std::abs(rx);
			ry = std::abs(ry);
			const f32 phi = rotationDegrees * SVG_PI / 180.0f;
			const f32 cosPhi = std::cos(phi);
			const f32 sinPhi = std::sin(phi);

			const f32 dx2 = (from.x - to.x) * 0.5f;
			const f32 dy2 = (from.y - to.y) * 0.5f;
			const f32 x1 = cosPhi * dx2 + sinPhi * dy2;
			const f32 y1 = -sinPhi * dx2 + cosPhi * dy2;

			const f32 lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
			if (lambda > 1.0f) {
				const f32 scale = std::sqrt(lambda);
				rx *= scale;
				ry *= scale;
			}

			const f32 rxSquared = rx * rx;
			const f32 rySquared = ry * ry;
			f32 numerator = rxSquared * rySquared - rxSquared * y1 * y1 - rySquared * x1 * x1;
			const f32 denominator = rxSquared * y1 * y1 + rySquared * x1 * x1;
			if (numerator < 0.0f) {
				numerator = 0.0f;
			}
			f32 coefficient = denominator > 0.0f ? std::sqrt(numerator / denominator) : 0.0f;
			if (largeArc == sweep) {
				coefficient = -coefficient;
			}
			const f32 cx1 = coefficient * rx * y1 / ry;
			const f32 cy1 = -coefficient * ry * x1 / rx;
			const f32 cx = cosPhi * cx1 - sinPhi * cy1 + (from.x + to.x) * 0.5f;
			const f32 cy = sinPhi * cx1 + cosPhi * cy1 + (from.y + to.y) * 0.5f;

			auto angleBetween = [](f32 ux, f32 uy, f32 vx, f32 vy) {
				const f32 dot = ux * vx + uy * vy;
				const f32 length = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
				f32 value = length > 0.0f ? std::acos(std::clamp(dot / length, -1.0f, 1.0f)) : 0.0f;
				if (ux * vy - uy * vx < 0.0f) {
					value = -value;
				}
				return value;
			};

			const f32 ux = (x1 - cx1) / rx;
			const f32 uy = (y1 - cy1) / ry;
			const f32 vx = (-x1 - cx1) / rx;
			const f32 vy = (-y1 - cy1) / ry;
			const f32 startAngle = angleBetween(1.0f, 0.0f, ux, uy);
			f32 sweepAngle = angleBetween(ux, uy, vx, vy);
			if (!sweep && sweepAngle > 0.0f) {
				sweepAngle -= SVG_TWO_PI;
			} else if (sweep && sweepAngle < 0.0f) {
				sweepAngle += SVG_TWO_PI;
			}

			const i32 segments = std::max(1, static_cast<i32>(std::ceil(std::abs(sweepAngle) / (SVG_PI * 0.5f))));
			const f32 delta = sweepAngle / static_cast<f32>(segments);
			const f32 handle = 4.0f / 3.0f * std::tan(delta * 0.25f);

			auto onEllipse = [&](f32 ex, f32 ey) {
				return Vec2{cosPhi * rx * ex - sinPhi * ry * ey + cx,
							sinPhi * rx * ex + cosPhi * ry * ey + cy};
			};

			f32 theta = startAngle;
			for (i32 i = 0; i < segments; ++i) {
				const f32 cosTheta = std::cos(theta);
				const f32 sinTheta = std::sin(theta);
				const f32 thetaNext = theta + delta;
				const f32 cosNext = std::cos(thetaNext);
				const f32 sinNext = std::sin(thetaNext);
				const Vec2 c0 = onEllipse(cosTheta - handle * sinTheta, sinTheta + handle * cosTheta);
				const Vec2 c1 = onEllipse(cosNext + handle * sinNext, sinNext - handle * cosNext);
				const Vec2 end = onEllipse(cosNext, sinNext);
				CubicTo(c0, c1, end, transform);
				theta = thetaNext;
			}
		}
	};

} // namespace Shalltear::Svg
