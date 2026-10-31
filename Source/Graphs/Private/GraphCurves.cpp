#include "GraphCurves.h"

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace Shalltear {

	namespace {

		using B = GraphProgramBuilder;

		GraphValueId Wrap(B& b, GraphValueId time, f32 head, f32 span, AnimationCurveWrapMode mode, bool before) {
			const GraphValueId end = b.Scalar(before ? head : head + span);
			if (mode == AnimationCurveWrapMode::Clamp) {
				return end;
			}
			const f32 length = mode == AnimationCurveWrapMode::Loop ? span : span * 2.0f;
			const GraphValueId from = b.Subtract(time, b.Scalar(head));
			const GraphValueId lengthValue = b.Scalar(length);
			const GraphValueId repeated = b.Clamp(b.Subtract(from, b.Multiply(b.Unary(GraphOp::Floor, b.Divide(from, lengthValue)), lengthValue)),
												  b.Scalar(0.0f), lengthValue);
			GraphValueId back = repeated;
			if (mode == AnimationCurveWrapMode::PingPong) {
				back = b.Select(b.Binary(GraphOp::Less, b.Scalar(span), repeated), b.Subtract(lengthValue, repeated), repeated);
			}
			return b.Select(b.Unary(GraphOp::IsInfinite, time), end, b.Add(b.Scalar(head), back));
		}

		AnimationCurveWrapMode WrapOf(f32 stored) {
			const u32 mode = static_cast<u32>(stored);
			return mode <= static_cast<u32>(AnimationCurveWrapMode::PingPong) ? static_cast<AnimationCurveWrapMode>(mode) : AnimationCurveWrapMode::Clamp;
		}

		constexpr std::string_view GRADIENT_MODES[] = {"Blend", "Fixed", "PerceptualBlend"};

		bool ReadKeys(const JSONValue& json, std::string_view key, u32 width, u32 at, std::span<f32> out, u32& count, str& error) {
			const JSONValue* list = json.Find(key);
			if (list == nullptr || !list->IsArray() || list->Size() == 0 || list->Size() > GRAPH_GRADIENT_KEYS) {
				error = "needs one to eight " + str(key);
				return false;
			}
			std::array<std::array<f32, 4>, GRAPH_GRADIENT_KEYS> keys{};
			count = static_cast<u32>(list->Size());
			for (u32 k = 0; k < count; ++k) {
				const JSONValue& item = (*list)[k];
				if (!item.IsArray() || item.Size() != width) {
					error = "has a key in '" + str(key) + "' that isn't " + std::to_string(width) + " numbers";
					return false;
				}
				for (u32 c = 0; c < width; ++c) {
					if (!item[c].IsNumber()) {
						error = "has a key in '" + str(key) + "' that isn't " + std::to_string(width) + " numbers";
						return false;
					}
					keys[k][c] = static_cast<f32>(item[c].AsNumber());
				}
			}
			std::stable_sort(keys.begin(), keys.begin() + count,
							 [width](const std::array<f32, 4>& a, const std::array<f32, 4>& b) { return a[width - 1] < b[width - 1]; });
			for (u32 k = 0; k < count; ++k) {
				for (u32 c = 0; c < width; ++c) {
					out[at + k * width + c] = keys[k][c];
				}
			}
			return true;
		}

		constexpr f32 TO_LMS[] = {0.4122214708f, 0.5363325363f, 0.0514459929f, 0.2119034982f, 0.6806995451f,
								  0.1073969566f, 0.0883024619f, 0.2817188376f, 0.6299787005f};
		constexpr f32 TO_LAB[] = {0.2104542553f, 0.7936177850f, -0.0040720468f, 1.9779984951f, -2.4285922050f,
								  0.4505937099f, 0.0259040371f, 0.7827717662f, -0.8086757660f};
		constexpr f32 FROM_LAB[] = {1.0f, 0.3963377774f, 0.2158037573f, 1.0f, -0.1055613458f, -0.0638541728f, 1.0f, -0.0894841775f, -1.2914855480f};
		constexpr f32 FROM_LMS[] = {4.0767416621f, -3.3077115913f, 0.2309699292f, -1.2684380046f, 2.6097574011f,
									-0.3413193965f, -0.0041960863f, -0.7034186147f, 1.7076147010f};

		GraphValueId LinearToOklab(B& b, GraphValueId rgb) {
			GraphValueId cone = b.Binary(GraphOp::MatrixMultiply, b.Constant(GraphValueType::Float3x3, TO_LMS), rgb);
			GraphValueId root = b.Binary(GraphOp::Power, cone, b.Scalar(0.333333f));
			return b.Binary(GraphOp::MatrixMultiply, b.Constant(GraphValueType::Float3x3, TO_LAB), root);
		}

		GraphValueId OklabToLinear(B& b, GraphValueId lab) {
			GraphValueId root = b.Binary(GraphOp::MatrixMultiply, b.Constant(GraphValueType::Float3x3, FROM_LAB), lab);
			GraphValueId cone = b.Multiply(b.Multiply(root, root), root);
			return b.Binary(GraphOp::MatrixMultiply, b.Constant(GraphValueType::Float3x3, FROM_LMS), cone);
		}

		void Transform(const f32 (&matrix)[9], const f32 in[3], f32 out[3]) {
			for (u32 row = 0; row < 3; ++row) {
				out[row] = matrix[row * 3] * in[0] + matrix[row * 3 + 1] * in[1] + matrix[row * 3 + 2] * in[2];
			}
		}

	} // anonymous namespace

	namespace GraphCurves {

		bool Parse(std::string_view json, AnimationCurve& out, str& error) {
			AnimationCurve read(GetMemoryResource(MemTag::Editor));
			if (!TextSerializer::Deserialize(&read, AnimationCurve::GetReflectType(), json)) {
				error = "isn't a curve";
				return false;
			}
			std::stable_sort(read.keys.begin(), read.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
			out = std::move(read);
			return true;
		}

		str Write(const AnimationCurve& curve) {
			const str pretty = TextSerializer::Serialize(&curve, AnimationCurve::GetReflectType());
			LinearAllocator arena(pretty.size() + 64, MemTag::Editor);
			return JSONSerialize(JSONParse(pretty, arena), false);
		}

		bool Pack(const AnimationCurve& curve, std::span<f32> out, str& error) {
			if (curve.keys.size() > GRAPH_CURVE_KEYS) {
				error = "holds " + std::to_string(curve.keys.size()) + " keys (an effect's curve holds at most " + std::to_string(GRAPH_CURVE_KEYS) + ")";
				return false;
			}
			std::fill(out.begin(), out.end(), 0.0f);
			out[0] = static_cast<f32>(curve.preWrapMode);
			out[1] = static_cast<f32>(curve.postWrapMode);
			out[2] = static_cast<f32>(curve.keys.size());
			const auto finite = [](f32 slope) { return Math::IsFinite(slope) ? slope : 0.0f; };
			for (size_t k = 0; k < curve.keys.size(); ++k) {
				const Keyframe& key = curve.keys[k];
				f32* at = out.data() + GRAPH_CURVE_HEADER + k * GRAPH_CURVE_KEY_FLOATS;
				at[0] = key.time;
				at[1] = key.value;
				at[2] = finite(key.inTangent);
				at[3] = finite(key.outTangent);
				if (k + 1 < curve.keys.size()) {
					const Keyframe& next = curve.keys[k + 1];
					const bool hold = key.outTangentMode == KeyframeTangentMode::Constant || next.inTangentMode == KeyframeTangentMode::Constant ||
									  !Math::IsFinite(key.outTangent) || !Math::IsFinite(next.inTangent);
					at[4] = hold ? 1.0f : 0.0f;
				}
			}
			return true;
		}

		GraphValueId Sample(GraphProgramBuilder& b, std::span<const f32> constant, GraphValueId time) {
			if (constant.size() != GRAPH_CURVE_COMPONENTS) {
				return b.Fail("reads a curve that isn't a constant");
			}
			std::array<f32, GRAPH_CURVE_COMPONENTS> held{};
			std::copy(constant.begin(), constant.end(), held.begin());
			const std::span<const f32> curve = held;
			const u32 count = std::min(static_cast<u32>(curve[2]), GRAPH_CURVE_KEYS);
			if (count == 0) {
				return b.Scalar(0.0f);
			}
			const auto key = [&curve](u32 index, u32 field) { return curve[GRAPH_CURVE_HEADER + index * GRAPH_CURVE_KEY_FLOATS + field]; };
			const f32 head = key(0, 0);
			const f32 tail = key(count - 1, 0);
			const f32 span = tail - head;
			if (count == 1 || !(span > 0.0f)) {
				return b.Scalar(key(0, 1));
			}

			const AnimationCurveWrapMode pre = WrapOf(curve[0]);
			const AnimationCurveWrapMode post = WrapOf(curve[1]);
			GraphValueId at = GRAPH_NO_VALUE;
			if (pre == AnimationCurveWrapMode::Clamp && post == AnimationCurveWrapMode::Clamp) {
				at = b.Clamp(time, b.Scalar(head), b.Scalar(tail));
			} else {
				at = b.Select(b.Binary(GraphOp::Less, time, b.Scalar(head)), Wrap(b, time, head, span, pre, true),
							  b.Select(b.Binary(GraphOp::Less, b.Scalar(tail), time), Wrap(b, time, head, span, post, false), time));
			}

			const auto segment = [&](u32 index, std::array<f32, 4>& cubic, std::array<f32, 2>& place) {
				const f32 value = key(index, 1);
				cubic = {0.0f, 0.0f, 0.0f, value};
				place = {key(index, 0), 0.0f};
				if (index + 1 >= count || key(index, 4) != 0.0f) {
					return;
				}
				const f64 length = static_cast<f64>(key(index + 1, 0)) - static_cast<f64>(key(index, 0));
				if (!(length > 0.0)) {
					return;
				}
				const f64 p0 = value;
				const f64 p1 = key(index + 1, 1);
				const f64 m0 = static_cast<f64>(key(index, 3)) * length;
				const f64 m1 = static_cast<f64>(key(index + 1, 2)) * length;
				cubic = {static_cast<f32>(2.0 * p0 + m0 - 2.0 * p1 + m1), static_cast<f32>(-3.0 * p0 - 2.0 * m0 + 3.0 * p1 - m1), static_cast<f32>(m0),
						 static_cast<f32>(p0)};
				place[1] = static_cast<f32>(1.0 / length);
			};
			std::array<f32, 4> cubic{};
			std::array<f32, 2> place{};
			segment(0, cubic, place);
			GraphValueId coefficients = b.Constant(GraphValueType::Float4, cubic);
			GraphValueId start = b.Constant(GraphValueType::Float2, place);
			for (u32 index = 1; index < count; ++index) {
				segment(index, cubic, place);
				const GraphValueId reached = b.Binary(GraphOp::LessEqual, b.Scalar(key(index, 0)), at);
				coefficients = b.Select(reached, b.Constant(GraphValueType::Float4, cubic), coefficients);
				start = b.Select(reached, b.Constant(GraphValueType::Float2, place), start);
			}
			const GraphValueId u = b.Multiply(b.Subtract(at, b.Swizzle(start, "x")), b.Swizzle(start, "y"));
			GraphValueId value = b.Swizzle(coefficients, "x");
			value = b.Add(b.Multiply(value, u), b.Swizzle(coefficients, "y"));
			value = b.Add(b.Multiply(value, u), b.Swizzle(coefficients, "z"));
			value = b.Add(b.Multiply(value, u), b.Swizzle(coefficients, "w"));
			return b.Select(b.Unary(GraphOp::IsNaN, time), b.Scalar(key(0, 1)), value);
		}

	} // namespace GraphCurves

	namespace GraphGradients {

		void Default(std::span<f32> out) {
			std::fill(out.begin(), out.end(), 0.0f);
			out[1] = 2.0f;
			out[2] = 2.0f;
			const f32 colors[] = {1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
			const f32 alphas[] = {1.0f, 0.0f, 1.0f, 1.0f};
			std::copy(std::begin(colors), std::end(colors), out.begin() + GRAPH_GRADIENT_COLORS);
			std::copy(std::begin(alphas), std::end(alphas), out.begin() + GRAPH_GRADIENT_ALPHAS);
		}

		bool Read(const JSONValue& json, std::span<f32> out, str& error) {
			std::fill(out.begin(), out.end(), 0.0f);
			if (!json.IsObject()) {
				error = "isn't a gradient";
				return false;
			}
			u32 mode = 0;
			if (const JSONValue* stored = json.Find("mode")) {
				mode = static_cast<u32>(std::size(GRADIENT_MODES));
				for (u32 i = 0; i < std::size(GRADIENT_MODES); ++i) {
					mode = stored->AsString() == GRADIENT_MODES[i] ? i : mode;
				}
				if (mode == std::size(GRADIENT_MODES)) {
					error = "has a mode that isn't Blend, Fixed or PerceptualBlend";
					return false;
				}
			}
			u32 colors = 0;
			u32 alphas = 0;
			if (!ReadKeys(json, "colors", 4, GRAPH_GRADIENT_COLORS, out, colors, error) || !ReadKeys(json, "alphas", 2, GRAPH_GRADIENT_ALPHAS, out, alphas, error)) {
				return false;
			}
			out[0] = static_cast<f32>(mode);
			out[1] = static_cast<f32>(colors);
			out[2] = static_cast<f32>(alphas);
			return true;
		}

		bool Read(std::string_view json, std::span<f32> out, str& error) {
			LinearAllocator arena(json.size() + 64, MemTag::Editor);
			return Read(JSONParse(json, arena), out, error);
		}

		GraphValueId Sample(GraphProgramBuilder& b, std::span<const f32> constant, GraphValueId time) {
			if (constant.size() != GRAPH_GRADIENT_COMPONENTS) {
				return b.Fail("reads a gradient that isn't a constant");
			}
			std::array<f32, GRAPH_GRADIENT_COMPONENTS> held{};
			std::copy(constant.begin(), constant.end(), held.begin());
			const std::span<const f32> gradient = held;
			const u32 mode = static_cast<u32>(gradient[0]);
			const u32 colorCount = std::min(static_cast<u32>(gradient[1]), GRAPH_GRADIENT_KEYS);
			const u32 alphaCount = std::min(static_cast<u32>(gradient[2]), GRAPH_GRADIENT_KEYS);
			const bool fixed = mode % 2 == 1;
			const bool perceptual = mode == 2;
			auto blend = [&](GraphValueId position) { return fixed ? b.Binary(GraphOp::Step, b.Scalar(0.01f), position) : position; };
			auto keyColor = [&](u32 key) {
				GraphValueId rgb = b.Constant(GraphValueType::Float3, gradient.subspan(GRAPH_GRADIENT_COLORS + key * 4, 3));
				return perceptual ? LinearToOklab(b, rgb) : rgb;
			};
			auto keyTime = [&](u32 key) { return b.Scalar(gradient[GRAPH_GRADIENT_COLORS + key * 4 + 3]); };
			auto alphaValue = [&](u32 key) { return b.Scalar(gradient[GRAPH_GRADIENT_ALPHAS + key * 2]); };
			auto alphaTime = [&](u32 key) { return b.Scalar(gradient[GRAPH_GRADIENT_ALPHAS + key * 2 + 1]); };

			GraphValueId color = keyColor(0);
			for (u32 key = 1; key < colorCount; ++key) {
				GraphValueId span = b.Subtract(keyTime(key), keyTime(key - 1));
				GraphValueId position = b.Unary(GraphOp::Saturate, b.Divide(b.Subtract(time, keyTime(key - 1)), span));
				color = b.Lerp(color, keyColor(key), blend(position));
			}
			if (perceptual) {
				color = OklabToLinear(b, color);
			}
			GraphValueId alpha = alphaValue(0);
			for (u32 key = 1; key < alphaCount; ++key) {
				GraphValueId span = b.Subtract(alphaTime(key), alphaTime(key - 1));
				GraphValueId position = b.Unary(GraphOp::Saturate, b.Divide(b.Subtract(time, alphaTime(key - 1)), span));
				alpha = b.Lerp(alpha, alphaValue(key), blend(position));
			}
			const GraphValueId parts[] = {color, alpha};
			return b.Construct(GraphValueType::Float4, parts);
		}

		void Evaluate(std::span<const f32> gradient, f32 time, f32 out[4]) {
			out[0] = out[1] = out[2] = out[3] = 1.0f;
			if (gradient.size() != GRAPH_GRADIENT_COMPONENTS) {
				return;
			}
			const u32 mode = static_cast<u32>(gradient[0]);
			const u32 colorCount = std::min(static_cast<u32>(gradient[1]), GRAPH_GRADIENT_KEYS);
			const u32 alphaCount = std::min(static_cast<u32>(gradient[2]), GRAPH_GRADIENT_KEYS);
			const bool fixed = mode % 2 == 1;
			const bool perceptual = mode == 2;
			const auto position = [&](f32 from, f32 to) {
				const f32 span = to - from;
				const f32 at = span > 0.0f ? Math::Clamp((time - from) / span, 0.0f, 1.0f) : (time >= to ? 1.0f : 0.0f);
				return fixed ? (at >= 0.01f ? 1.0f : 0.0f) : at;
			};
			const auto keyColor = [&](u32 key, f32 rgb[3]) {
				const f32* stored = gradient.data() + GRAPH_GRADIENT_COLORS + key * 4;
				if (!perceptual) {
					std::copy(stored, stored + 3, rgb);
					return;
				}
				f32 cone[3];
				Transform(TO_LMS, stored, cone);
				for (f32& c : cone) {
					c = std::pow(c, 0.333333f);
				}
				Transform(TO_LAB, cone, rgb);
			};
			f32 color[3];
			if (colorCount > 0) {
				keyColor(0, color);
				for (u32 key = 1; key < colorCount; ++key) {
					f32 next[3];
					keyColor(key, next);
					const f32 t = position(gradient[GRAPH_GRADIENT_COLORS + (key - 1) * 4 + 3], gradient[GRAPH_GRADIENT_COLORS + key * 4 + 3]);
					for (u32 c = 0; c < 3; ++c) {
						color[c] += (next[c] - color[c]) * t;
					}
				}
				if (perceptual) {
					f32 root[3];
					Transform(FROM_LAB, color, root);
					for (f32& c : root) {
						c = c * c * c;
					}
					Transform(FROM_LMS, root, color);
				}
				std::copy(color, color + 3, out);
			}
			if (alphaCount > 0) {
				f32 alpha = gradient[GRAPH_GRADIENT_ALPHAS];
				for (u32 key = 1; key < alphaCount; ++key) {
					const f32 t = position(gradient[GRAPH_GRADIENT_ALPHAS + (key - 1) * 2 + 1], gradient[GRAPH_GRADIENT_ALPHAS + key * 2 + 1]);
					alpha += (gradient[GRAPH_GRADIENT_ALPHAS + key * 2] - alpha) * t;
				}
				out[3] = alpha;
			}
		}

	} // namespace GraphGradients

} // namespace Shalltear
