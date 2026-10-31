#include "GraphNodeLibrary.h"
#include "GraphSpaces.h"

#include <array>

namespace Shalltear {

	namespace {

		using P = GraphPortType;
		using S = GraphSettingKind;
		using B = GraphProgramBuilder;
		using T = GraphValueType;

		GraphValueId Const2(B& b, f32 x, f32 y) {
			const f32 c[] = {x, y};
			return b.Constant(T::Float2, c);
		}

		GraphValueId Const3(B& b, f32 x, f32 y, f32 z) {
			const f32 c[] = {x, y, z};
			return b.Constant(T::Float3, c);
		}

		GraphValueId Const4(B& b, f32 x, f32 y, f32 z, f32 w) {
			const f32 c[] = {x, y, z, w};
			return b.Constant(T::Float4, c);
		}

		GraphValueId Splat3(B& b, GraphValueId scalar) { return b.Swizzle(scalar, "xxx"); }

		template <typename Each>
		GraphValueId PerComponent(B& b, u32 width, Each&& each) {
			std::array<GraphValueId, 4> parts{};
			for (u32 i = 0; i < width; ++i) {
				parts[i] = each(i);
			}
			return width == 1 ? parts[0] : b.Construct(GraphTypes::Vector(width), {parts.data(), width});
		}

		GraphValueId Component(B& b, GraphValueId v, u32 i) {
			constexpr std::string_view NAMES[] = {"x", "y", "z", "w"};
			return GraphTypes::VectorWidth(b.TypeOf(v)) == 1 ? v : b.Swizzle(v, NAMES[i]);
		}

		GraphValueId RgbToHsv(B& b, GraphValueId in, f32 e, bool epsilonV) {
			GraphValueId p = b.Lerp(b.Construct(T::Float4, std::array{b.Swizzle(in, "zy"), Const2(b, -1.0f, 2.0f / 3.0f)}),
									b.Construct(T::Float4, std::array{b.Swizzle(in, "yz"), Const2(b, 0.0f, -1.0f / 3.0f)}),
									b.Binary(GraphOp::Step, b.Swizzle(in, "z"), b.Swizzle(in, "y")));
			GraphValueId r = b.Swizzle(in, "x");
			GraphValueId q = b.Lerp(b.Construct(T::Float4, std::array{b.Swizzle(p, "xyw"), r}),
									b.Construct(T::Float4, std::array{r, b.Swizzle(p, "yzx")}),
									b.Binary(GraphOp::Step, b.Swizzle(p, "x"), r));
			GraphValueId qx = b.Swizzle(q, "x");
			GraphValueId qy = b.Swizzle(q, "y");
			GraphValueId qz = b.Swizzle(q, "z");
			GraphValueId qw = b.Swizzle(q, "w");
			GraphValueId d = b.Subtract(qx, b.Minimum(qw, qy));
			GraphValueId epsilon = b.Scalar(e);
			GraphValueId v = epsilonV ? b.Select(b.Binary(GraphOp::Equal, d, b.Scalar(0.0f)), qx, b.Add(qx, epsilon)) : qx;
			GraphValueId h = b.Unary(GraphOp::Absolute, b.Add(qz, b.Divide(b.Subtract(qw, qy), b.Add(b.Multiply(b.Scalar(6.0f), d), epsilon))));
			GraphValueId s = b.Divide(d, b.Add(qx, epsilon));
			return b.Construct(T::Float3, std::array{h, s, v});
		}

		GraphValueId HsvToRgb(B& b, GraphValueId hsv) {
			GraphValueId k = Const4(b, 1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
			GraphValueId p = b.Unary(GraphOp::Absolute, b.Subtract(b.Multiply(b.Unary(GraphOp::Fraction, b.Add(b.Swizzle(hsv, "xxx"), b.Swizzle(k, "xyz"))), b.Scalar(6.0f)),
																   b.Swizzle(k, "www")));
			GraphValueId kxxx = b.Swizzle(k, "xxx");
			return b.Multiply(b.Swizzle(hsv, "z"), b.Lerp(kxxx, b.Unary(GraphOp::Saturate, b.Subtract(p, kxxx)), b.Swizzle(hsv, "y")));
		}

		GraphValueId SrgbToLinear(B& b, GraphValueId in) {
			GraphValueId lo = b.Divide(in, b.Scalar(12.92f));
			GraphValueId hi = b.Binary(GraphOp::Power, b.Maximum(b.Unary(GraphOp::Absolute, b.Divide(b.Add(in, b.Scalar(0.055f)), b.Scalar(1.055f))), b.Scalar(1.192092896e-07f)),
									   Const3(b, 2.4f, 2.4f, 2.4f));
			return PerComponent(b, 3, [&](u32 i) {
				return b.Select(b.Binary(GraphOp::LessEqual, Component(b, in, i), b.Scalar(0.04045f)), Component(b, lo, i), Component(b, hi, i));
			});
		}

		GraphValueId LinearToSrgb(B& b, GraphValueId in) {
			GraphValueId lo = b.Multiply(in, b.Scalar(12.92f));
			GraphValueId hi = b.Subtract(b.Multiply(b.Binary(GraphOp::Power, b.Maximum(b.Unary(GraphOp::Absolute, in), b.Scalar(1.192092896e-07f)),
															 Const3(b, 1.0f / 2.4f, 1.0f / 2.4f, 1.0f / 2.4f)),
													b.Scalar(1.055f)),
										 b.Scalar(0.055f));
			return PerComponent(b, 3, [&](u32 i) {
				return b.Select(b.Binary(GraphOp::LessEqual, Component(b, in, i), b.Scalar(0.0031308f)), Component(b, lo, i), Component(b, hi, i));
			});
		}

		constexpr GraphPortDefinition IN3[] = {{"In", P::Float3}};
		constexpr GraphPortDefinition OUT3[] = {{"Out", P::Float3}};
		constexpr GraphPortDefinition IN_VECTOR[] = {{"In", P::DynamicVector}};
		constexpr GraphPortDefinition OUT_VECTOR[] = {{"Out", P::DynamicVector}};
		constexpr GraphPortDefinition OUT1[] = {{"Out", P::Float}};

		constexpr GraphPortDefinition CONTRAST_INPUTS[] = {{"In", P::Float3}, {"Contrast", P::Float, {1, 1, 1, 1}}};

		void ContrastBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId midpoint = b.Binary(GraphOp::Power, b.Scalar(0.5f), b.Scalar(2.2f));
			c.outputs[0] = b.Add(b.Multiply(b.Subtract(c.In(0), midpoint), c.In(1)), midpoint);
		}

		constexpr std::string_view HUE_RANGES[] = {"Degrees", "Normalized"};
		constexpr GraphSettingDefinition HUE_SETTINGS[] = {{"Range", S::Choice, HUE_RANGES}};
		constexpr GraphPortDefinition HUE_INPUTS[] = {{"In", P::Float3}, {"Offset", P::Float}};

		void HueBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId hsv = RgbToHsv(b, c.In(0), 1e-4f, true);
			GraphValueId offset = c.Setting(0) == 1 ? c.In(1) : b.Divide(c.In(1), b.Scalar(360.0f));
			GraphValueId hue = b.Add(b.Swizzle(hsv, "x"), offset);
			GraphValueId wrapped = b.Select(b.Binary(GraphOp::Less, hue, b.Scalar(0.0f)), b.Add(hue, b.Scalar(1.0f)),
											b.Select(b.Binary(GraphOp::Less, b.Scalar(1.0f), hue), b.Subtract(hue, b.Scalar(1.0f)), hue));
			c.outputs[0] = HsvToRgb(b, b.Construct(T::Float3, std::array{wrapped, b.Swizzle(hsv, "yz")}));
		}

		constexpr GraphSettingDefinition CHANNEL_TOGGLES[] = {
			{"Red", S::Toggle}, {"Green", S::Toggle}, {"Blue", S::Toggle}, {"Alpha", S::Toggle}};

		void InvertColorsBody(GraphBodyContext& c) {
			B& b = c.builder;
			const u32 width = GraphTypes::VectorWidth(c.TypeOf(0));
			std::array<f32, 4> flags{};
			for (u32 i = 0; i < width; ++i) {
				flags[i] = static_cast<f32>(c.Setting(i));
			}
			GraphValueId invert = b.Constant(GraphTypes::Vector(width), {flags.data(), width});
			c.outputs[0] = b.Unary(GraphOp::Absolute, b.Subtract(invert, c.In(0)));
		}

		constexpr GraphPortDefinition REPLACE_INPUTS[] = {
			{"In", P::Float3}, {"From", P::Float3}, {"To", P::Float3}, {"Range", P::Float}, {"Fuzziness", P::Float}};

		void ReplaceColorBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId distance = b.Binary(GraphOp::Distance, c.In(1), c.In(0));
			GraphValueId t = b.Unary(GraphOp::Saturate, b.Divide(b.Subtract(distance, c.In(3)), b.Maximum(c.In(4), b.Scalar(1e-5f))));
			c.outputs[0] = b.Lerp(c.In(2), c.In(0), t);
		}

		constexpr GraphPortDefinition SATURATION_INPUTS[] = {{"In", P::Float3}, {"Saturation", P::Float, {1, 1, 1, 1}}};

		void SaturationBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId luma = b.Dot(c.In(0), Const3(b, 0.2126729f, 0.7151522f, 0.0721750f));
			GraphValueId lumas = Splat3(b, luma);
			c.outputs[0] = b.Add(lumas, b.Multiply(Splat3(b, c.In(1)), b.Subtract(c.In(0), lumas)));
		}

		constexpr GraphPortDefinition WHITE_BALANCE_INPUTS[] = {{"In", P::Float3}, {"Temperature", P::Float}, {"Tint", P::Float}};

		void WhiteBalanceBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId t1 = b.Divide(b.Multiply(c.In(1), b.Scalar(10.0f)), b.Scalar(6.0f));
			GraphValueId t2 = b.Divide(b.Multiply(c.In(2), b.Scalar(10.0f)), b.Scalar(6.0f));
			GraphValueId x = b.Subtract(b.Scalar(0.31271f), b.Multiply(t1, b.Select(b.Binary(GraphOp::Less, t1, b.Scalar(0.0f)), b.Scalar(0.1f), b.Scalar(0.05f))));
			GraphValueId illuminant = b.Subtract(b.Subtract(b.Multiply(b.Scalar(2.87f), x), b.Multiply(b.Multiply(b.Scalar(3.0f), x), x)), b.Scalar(0.27509507f));
			GraphValueId y = b.Add(illuminant, b.Multiply(t2, b.Scalar(0.05f)));
			GraphValueId one = b.Scalar(1.0f);
			GraphValueId bigX = b.Divide(b.Multiply(one, x), y);
			GraphValueId bigZ = b.Divide(b.Multiply(one, b.Subtract(b.Subtract(one, x), y)), y);
			GraphValueId l = b.Add(b.Add(b.Multiply(b.Scalar(0.7328f), bigX), b.Multiply(b.Scalar(0.4296f), one)), b.Multiply(b.Scalar(-0.1624f), bigZ));
			GraphValueId m = b.Add(b.Add(b.Multiply(b.Scalar(-0.7036f), bigX), b.Multiply(b.Scalar(1.6975f), one)), b.Multiply(b.Scalar(0.0061f), bigZ));
			GraphValueId s = b.Add(b.Add(b.Multiply(b.Scalar(0.0030f), bigX), b.Multiply(b.Scalar(0.0136f), one)), b.Multiply(b.Scalar(0.9834f), bigZ));
			GraphValueId w2 = b.Construct(T::Float3, std::array{l, m, s});
			GraphValueId balance = b.Divide(Const3(b, 0.949237f, 1.03542f, 1.08728f), w2);
			constexpr f32 LIN_2_LMS[] = {3.90405e-1f, 5.49941e-1f, 8.92632e-3f, 7.08416e-2f, 9.63172e-1f, 1.35775e-3f, 2.31082e-2f, 1.28021e-1f, 9.36245e-1f};
			constexpr f32 LMS_2_LIN[] = {2.85847e+0f, -1.62879e+0f, -2.48910e-2f, -2.10182e-1f, 1.15820e+0f, 3.24281e-4f, -4.18120e-2f, -1.18169e-1f, 1.06867e+0f};
			GraphValueId lms = b.Multiply(b.Binary(GraphOp::MatrixMultiply, b.Constant(T::Float3x3, LIN_2_LMS), c.In(0)), balance);
			c.outputs[0] = b.Binary(GraphOp::MatrixMultiply, b.Constant(T::Float3x3, LMS_2_LIN), lms);
		}

		constexpr GraphSettingDefinition CHANNEL_MIXER_SETTINGS[] = {
			{"Red", S::Vector, {}, 0, {}, {1, 0, 0, 0}}, {"Green", S::Vector, {}, 0, {}, {0, 1, 0, 0}}, {"Blue", S::Vector, {}, 0, {}, {0, 0, 1, 0}}};

		void ChannelMixerBody(GraphBodyContext& c) {
			B& b = c.builder;
			std::array<GraphValueId, 3> rows{};
			for (u32 i = 0; i < 3; ++i) {
				const f32* n = c.settings[i].numbers;
				rows[i] = b.Dot(c.In(0), Const3(b, n[0], n[1], n[2]));
			}
			c.outputs[0] = b.Construct(T::Float3, rows);
		}

		constexpr std::string_view BLEND_MODES[] = {"Burn", "Darken", "Difference", "Dodge", "Divide", "Exclusion",
													"HardLight", "HardMix", "Lighten", "LinearBurn", "LinearDodge", "LinearLight",
													"LinearLightAddSub", "Multiply", "Negation", "Overlay", "PinLight", "Screen",
													"SoftLight", "Subtract", "VividLight", "Overwrite"};
		constexpr GraphSettingDefinition BLEND_SETTINGS[] = {{"Mode", S::Choice, BLEND_MODES, 15}};
		constexpr GraphPortDefinition BLEND_INPUTS[] = {{"Base", P::DynamicVector}, {"Blend", P::DynamicVector}, {"Opacity", P::Float, {1, 1, 1, 1}}};

		void BlendBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId base = c.In(0);
			GraphValueId blend = c.In(1);
			const u32 width = GraphTypes::VectorWidth(c.TypeOf(0));
			auto s = [&](f32 v) { return b.Scalar(v); };
			GraphValueId one = s(1.0f);
			GraphValueId out = GRAPH_NO_VALUE;
			switch (c.Setting(0)) {
			case 0:
				out = b.Subtract(one, b.Divide(b.Subtract(one, blend), b.Add(base, s(0.000000000001f))));
				break;
			case 1:
				out = b.Minimum(blend, base);
				break;
			case 2:
				out = b.Unary(GraphOp::Absolute, b.Subtract(blend, base));
				break;
			case 3:
				out = b.Divide(base, b.Subtract(one, b.Clamp(blend, s(0.000001f), s(0.999999f))));
				break;
			case 4:
				out = b.Divide(base, b.Add(blend, s(0.000000000001f)));
				break;
			case 5:
				out = b.Subtract(b.Add(blend, base), b.Multiply(b.Multiply(s(2.0f), blend), base));
				break;
			case 6: {
				GraphValueId result1 = b.Subtract(one, b.Multiply(b.Multiply(s(2.0f), b.Subtract(one, base)), b.Subtract(one, blend)));
				GraphValueId result2 = b.Multiply(b.Multiply(s(2.0f), base), blend);
				GraphValueId zeroOrOne = b.Binary(GraphOp::Step, blend, s(0.5f));
				out = b.Add(b.Multiply(result2, zeroOrOne), b.Multiply(b.Subtract(one, zeroOrOne), result1));
				break;
			}
			case 7:
				out = b.Binary(GraphOp::Step, b.Subtract(one, base), blend);
				break;
			case 8:
				out = b.Maximum(blend, base);
				break;
			case 9:
				out = b.Subtract(b.Add(base, blend), one);
				break;
			case 10:
				out = b.Add(base, blend);
				break;
			case 11:
				out = PerComponent(b, width, [&](u32 i) {
					GraphValueId bi = Component(b, base, i);
					GraphValueId li = Component(b, blend, i);
					return b.Select(b.Binary(GraphOp::Less, li, s(0.5f)), b.Maximum(b.Subtract(b.Add(bi, b.Multiply(s(2.0f), li)), one), s(0.0f)),
									b.Minimum(b.Add(bi, b.Multiply(s(2.0f), b.Subtract(li, s(0.5f)))), one));
				});
				break;
			case 12:
				out = b.Subtract(b.Add(blend, b.Multiply(s(2.0f), base)), one);
				break;
			case 13:
				out = b.Multiply(base, blend);
				break;
			case 14:
				out = b.Subtract(one, b.Unary(GraphOp::Absolute, b.Subtract(b.Subtract(one, blend), base)));
				break;
			case 15: {
				GraphValueId result1 = b.Subtract(one, b.Multiply(b.Multiply(s(2.0f), b.Subtract(one, base)), b.Subtract(one, blend)));
				GraphValueId result2 = b.Multiply(b.Multiply(s(2.0f), base), blend);
				GraphValueId zeroOrOne = b.Binary(GraphOp::Step, base, s(0.5f));
				out = b.Add(b.Multiply(result2, zeroOrOne), b.Multiply(b.Subtract(one, zeroOrOne), result1));
				break;
			}
			case 16: {
				GraphValueId check = b.Binary(GraphOp::Step, s(0.5f), blend);
				GraphValueId result1 = b.Multiply(check, b.Maximum(b.Multiply(s(2.0f), b.Subtract(base, s(0.5f))), blend));
				out = b.Add(result1, b.Multiply(b.Subtract(one, check), b.Minimum(b.Multiply(s(2.0f), base), blend)));
				break;
			}
			case 17:
				out = b.Subtract(one, b.Multiply(b.Subtract(one, blend), b.Subtract(one, base)));
				break;
			case 18: {
				GraphValueId result1 = b.Add(b.Multiply(b.Multiply(s(2.0f), base), blend), b.Multiply(b.Multiply(base, base), b.Subtract(one, b.Multiply(s(2.0f), blend))));
				GraphValueId result2 = b.Add(b.Multiply(b.Unary(GraphOp::SquareRoot, base), b.Subtract(b.Multiply(s(2.0f), blend), one)),
											 b.Multiply(b.Multiply(s(2.0f), base), b.Subtract(one, blend)));
				GraphValueId zeroOrOne = b.Binary(GraphOp::Step, s(0.5f), blend);
				out = b.Add(b.Multiply(result2, zeroOrOne), b.Multiply(b.Subtract(one, zeroOrOne), result1));
				break;
			}
			case 19:
				out = b.Subtract(base, blend);
				break;
			case 20: {
				base = b.Clamp(base, s(0.000001f), s(0.999999f));
				GraphValueId result1 = b.Subtract(one, b.Divide(b.Subtract(one, blend), b.Multiply(s(2.0f), base)));
				GraphValueId result2 = b.Divide(blend, b.Multiply(s(2.0f), b.Subtract(one, base)));
				GraphValueId zeroOrOne = b.Binary(GraphOp::Step, s(0.5f), base);
				out = b.Add(b.Multiply(result2, zeroOrOne), b.Multiply(b.Subtract(one, zeroOrOne), result1));
				break;
			}
			default:
				out = blend;
				break;
			}
			c.outputs[0] = b.Lerp(base, out, c.In(2));
		}

		constexpr GraphPortDefinition DITHER_INPUTS[] = {{"In", P::DynamicVector}, {"Screen Position", P::Float2, {}, "NDCPosition"}};

		void DitherBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId pixel = b.Multiply(c.In(1), b.Swizzle(c.Binding("ScreenParams"), "xy"));
			auto bits = [&](GraphValueId coordinate, GraphValueId& low, GraphValueId& high) {
				GraphValueId wrapped = b.Binary(GraphOp::Modulo, b.Unary(GraphOp::Floor, coordinate), b.Scalar(4.0f));
				low = b.Binary(GraphOp::Modulo, wrapped, b.Scalar(2.0f));
				high = b.Unary(GraphOp::Floor, b.Divide(wrapped, b.Scalar(2.0f)));
			};
			GraphValueId x0 = GRAPH_NO_VALUE;
			GraphValueId x1 = GRAPH_NO_VALUE;
			GraphValueId y0 = GRAPH_NO_VALUE;
			GraphValueId y1 = GRAPH_NO_VALUE;
			bits(b.Swizzle(pixel, "x"), x0, x1);
			bits(b.Swizzle(pixel, "y"), y0, y1);
			GraphValueId rank = b.Add(b.Add(b.Multiply(b.Scalar(8.0f), b.Unary(GraphOp::Absolute, b.Subtract(x0, y0))), b.Multiply(b.Scalar(4.0f), x0)),
									  b.Add(b.Multiply(b.Scalar(2.0f), b.Unary(GraphOp::Absolute, b.Subtract(x1, y1))), x1));
			GraphValueId threshold = b.Divide(b.Add(rank, b.Scalar(1.0f)), b.Scalar(17.0f));
			c.outputs[0] = b.Subtract(c.In(0), threshold);
		}

		constexpr GraphPortDefinition FADE_INPUTS[] = {
			{"NoiseValue", P::Float, {0.5f, 0, 0, 0}}, {"FadeValue", P::Float, {0.5f, 0, 0, 0}}, {"FadeContrast", P::Float, {1, 0, 0, 0}}};
		constexpr GraphPortDefinition FADE_OUTPUTS[] = {{"Fade", P::Float}};

		void FadeTransitionBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId contrast = c.In(2);
			c.outputs[0] = b.Unary(GraphOp::Saturate, b.Add(b.Multiply(c.In(1), b.Add(contrast, b.Scalar(1.0f))),
															b.Multiply(b.Subtract(c.In(0), b.Scalar(1.0f)), contrast)));
		}

		constexpr GraphSettingDefinition CHANNEL_MASK_SETTINGS[] = {
			{"Red", S::Toggle, {}, 1}, {"Green", S::Toggle, {}, 1}, {"Blue", S::Toggle, {}, 1}, {"Alpha", S::Toggle, {}, 1}};

		void ChannelMaskBody(GraphBodyContext& c) {
			B& b = c.builder;
			const u32 width = GraphTypes::VectorWidth(c.TypeOf(0));
			u32 mask = 0;
			for (u32 i = 0; i < 4; ++i) {
				mask |= c.Setting(i) != 0 ? 1u << i : 0u;
			}
			if (mask == 0) {
				const std::array<f32, 4> zero{};
				c.outputs[0] = b.Constant(GraphTypes::Vector(width), {zero.data(), width});
				return;
			}
			if (mask >= (1u << width) || mask == 0xf) {
				c.outputs[0] = c.In(0);
				return;
			}
			if (width == 1) {
				c.outputs[0] = c.In(0);
				return;
			}
			c.outputs[0] = PerComponent(b, width, [&](u32 i) { return (mask & (1u << i)) != 0 ? Component(b, c.In(0), i) : b.Scalar(0.0f); });
		}

		constexpr GraphPortDefinition COLOR_MASK_INPUTS[] = {{"In", P::Float3}, {"Mask Color", P::Float3}, {"Range", P::Float}, {"Fuzziness", P::Float}};

		void ColorMaskBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId distance = b.Binary(GraphOp::Distance, c.In(1), c.In(0));
			c.outputs[0] = b.Unary(GraphOp::Saturate, b.Subtract(b.Scalar(1.0f), b.Divide(b.Subtract(distance, c.In(2)), b.Maximum(c.In(3), b.Scalar(1e-5f)))));
		}

		constexpr std::string_view NORMAL_BLEND_MODES[] = {"Default", "Reoriented"};
		constexpr GraphSettingDefinition NORMAL_BLEND_SETTINGS[] = {{"Mode", S::Choice, NORMAL_BLEND_MODES}};
		constexpr GraphPortDefinition NORMAL_BLEND_INPUTS[] = {{"A", P::Float3, {0, 0, 1, 0}}, {"B", P::Float3, {0, 0, 1, 0}}};

		void NormalBlendBody(GraphBodyContext& c) {
			B& b = c.builder;
			if (c.Setting(0) == 1) {
				GraphValueId t = b.Add(c.In(0), Const3(b, 0.0f, 0.0f, 1.0f));
				GraphValueId u = b.Multiply(c.In(1), Const3(b, -1.0f, -1.0f, 1.0f));
				c.outputs[0] = b.Subtract(b.Multiply(b.Divide(t, b.Swizzle(t, "z")), b.Dot(t, u)), u);
				return;
			}
			GraphValueId xy = b.Add(b.Swizzle(c.In(0), "xy"), b.Swizzle(c.In(1), "xy"));
			GraphValueId z = b.Multiply(b.Swizzle(c.In(0), "z"), b.Swizzle(c.In(1), "z"));
			c.outputs[0] = GraphSpaces::SafeNormalize(b, b.Construct(T::Float3, std::array{xy, z}));
		}

		constexpr std::string_view OUTPUT_SPACES[] = {"Tangent", "World"};
		constexpr GraphSettingDefinition NORMAL_FROM_HEIGHT_SETTINGS[] = {{"Output Space", S::Choice, OUTPUT_SPACES}};
		constexpr GraphPortDefinition NORMAL_FROM_HEIGHT_INPUTS[] = {{"In", P::Float}, {"Strength", P::Float, {0.01f, 0, 0, 0}}};

		// fragment only (takes derivatives)
		void NormalFromHeightBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId position = c.Binding("WorldSpacePosition");
			GraphValueId normal = c.Binding("WorldSpaceNormal");
			if (position == GRAPH_NO_VALUE || normal == GRAPH_NO_VALUE) {
				return;
			}
			GraphValueId dx = b.Unary(GraphOp::DerivativeX, position);
			GraphValueId dy = b.Unary(GraphOp::DerivativeY, position);
			GraphValueId crossX = b.Binary(GraphOp::Cross, normal, dx);
			GraphValueId crossY = b.Binary(GraphOp::Cross, dy, normal);
			GraphValueId d = b.Dot(dx, crossY);
			GraphValueId sign = b.Select(b.Binary(GraphOp::Less, d, b.Scalar(0.0f)), b.Scalar(-1.0f), b.Scalar(1.0f));
			GraphValueId surface = b.Divide(sign, b.Maximum(b.Scalar(0.000000000000001192093f), b.Unary(GraphOp::Absolute, d)));
			GraphValueId dHdx = b.Unary(GraphOp::DerivativeX, c.In(0));
			GraphValueId dHdy = b.Unary(GraphOp::DerivativeY, c.In(0));
			GraphValueId gradient = b.Multiply(surface, b.Add(b.Multiply(dHdx, crossY), b.Multiply(dHdy, crossX)));
			GraphValueId out = GraphSpaces::SafeNormalize(b, b.Subtract(normal, b.Multiply(c.In(1), gradient)));
			c.outputs[0] = c.Setting(0) == 0 ? GraphSpaces::Transform(c, out, GraphSpace::World, GraphSpace::Tangent, GraphConversion::Normal, true) : out;
		}

		constexpr GraphPortDefinition NORMAL_FROM_TEXTURE_INPUTS[] = {
			{"Texture", P::Texture2D}, {"UV", P::Float2, {}, "UV0"}, {"Offset", P::Float, {0.5f, 0, 0, 0}}, {"Strength", P::Float, {8, 0, 0, 0}}};

		void NormalFromTextureBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId uv = c.In(1);
			GraphValueId offset = b.Multiply(b.Binary(GraphOp::Power, c.In(2), b.Scalar(3.0f)), b.Scalar(0.1f));
			GraphValueId zero = b.Scalar(0.0f);
			GraphValueId offsetU = b.Add(uv, b.Construct(T::Float2, std::array{offset, zero}));
			GraphValueId offsetV = b.Subtract(uv, b.Construct(T::Float2, std::array{zero, offset}));
			auto red = [&](GraphValueId at) {
				const GraphValueId operands[] = {c.In(0), at};
				return b.Swizzle(b.TextureOp(GraphOp::SampleTexture, operands), "x");
			};
			GraphValueId here = red(uv);
			GraphValueId u = red(offsetU);
			GraphValueId v = red(offsetV);
			GraphValueId va = b.Construct(T::Float3, std::array{b.Scalar(1.0f), zero, b.Multiply(b.Subtract(u, here), c.In(3))});
			GraphValueId vb = b.Construct(T::Float3, std::array{zero, b.Scalar(1.0f), b.Multiply(b.Subtract(v, here), c.In(3))});
			c.outputs[0] = b.Unary(GraphOp::Normalize, b.Binary(GraphOp::Cross, va, vb));
		}

		constexpr GraphPortDefinition NORMAL_RECONSTRUCT_INPUTS[] = {{"In", P::Float2}};

		void NormalReconstructZBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId z = b.Unary(GraphOp::SquareRoot, b.Subtract(b.Scalar(1.0f), b.Unary(GraphOp::Saturate, b.Dot(c.In(0), c.In(0)))));
			c.outputs[0] = b.Unary(GraphOp::Normalize, b.Construct(T::Float3, std::array{c.In(0), z}));
		}

		constexpr GraphPortDefinition NORMAL_STRENGTH_INPUTS[] = {{"In", P::Float3, {0, 0, 1, 0}}, {"Strength", P::Float, {1, 0, 0, 0}}};

		void NormalStrengthBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId xy = b.Multiply(b.Swizzle(c.In(0), "xy"), c.In(1));
			GraphValueId z = b.Lerp(b.Scalar(1.0f), b.Swizzle(c.In(0), "z"), b.Unary(GraphOp::Saturate, c.In(1)));
			c.outputs[0] = b.Construct(T::Float3, std::array{xy, z});
		}

		constexpr std::string_view NORMAL_MAP_SPACES[] = {"Tangent", "Object"};
		constexpr GraphSettingDefinition NORMAL_UNPACK_SETTINGS[] = {{"Space", S::Choice, NORMAL_MAP_SPACES}};
		constexpr GraphPortDefinition NORMAL_UNPACK_INPUTS[] = {{"In", P::Float4}};

		void NormalUnpackBody(GraphBodyContext& c) {
			B& b = c.builder;
			if (c.Setting(0) == 1) {
				c.outputs[0] = b.Subtract(b.Multiply(b.Swizzle(c.In(0), "xyz"), b.Scalar(2.0f)), b.Scalar(1.0f));
				return;
			}
			GraphValueId xy = b.Subtract(b.Multiply(b.Swizzle(c.In(0), "xy"), b.Scalar(2.0f)), b.Scalar(1.0f));
			GraphValueId z = b.Maximum(b.Scalar(1.0e-16f), b.Unary(GraphOp::SquareRoot, b.Subtract(b.Scalar(1.0f), b.Unary(GraphOp::Saturate, b.Dot(xy, xy)))));
			c.outputs[0] = b.Construct(T::Float3, std::array{xy, z});
		}

		constexpr std::string_view COLORSPACES[] = {"RGB", "Linear", "HSV"};
		constexpr GraphSettingDefinition COLORSPACE_SETTINGS[] = {{"From", S::Choice, COLORSPACES}, {"To", S::Choice, COLORSPACES}};

		void ColorspaceConversionBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId in = c.In(0);
			const u32 from = c.Setting(0);
			const u32 to = c.Setting(1);
			GraphValueId out = in;
			if (from == 0 && to == 1) {
				out = SrgbToLinear(b, in);
			} else if (from == 0 && to == 2) {
				out = RgbToHsv(b, in, 1e-10f, true);
			} else if (from == 1 && to == 0) {
				out = LinearToSrgb(b, in);
			} else if (from == 1 && to == 2) {
				out = RgbToHsv(b, LinearToSrgb(b, in), 1e-10f, false);
			} else if (from == 2 && to == 0) {
				out = HsvToRgb(b, in);
			} else if (from == 2 && to == 1) {
				out = SrgbToLinear(b, HsvToRgb(b, in));
			}
			c.outputs[0] = out;
		}

		constexpr GraphNodeDefinition ARTISTIC_DEFINITIONS[] = {
			{"Artistic/ChannelMixer", "Channel Mixer", "Artistic/Adjustment", "", IN3, OUT3, CHANNEL_MIXER_SETTINGS, ChannelMixerBody},
			{"Artistic/Contrast", "Contrast", "Artistic/Adjustment", "", CONTRAST_INPUTS, OUT3, {}, ContrastBody},
			{"Artistic/Hue", "Hue", "Artistic/Adjustment", "", HUE_INPUTS, OUT3, HUE_SETTINGS, HueBody},
			{"Artistic/InvertColors", "Invert Colors", "Artistic/Adjustment", "", IN_VECTOR, OUT_VECTOR, CHANNEL_TOGGLES, InvertColorsBody},
			{"Artistic/ReplaceColor", "Replace Color", "Artistic/Adjustment", "", REPLACE_INPUTS, OUT3, {}, ReplaceColorBody},
			{"Artistic/Saturation", "Saturation", "Artistic/Adjustment", "", SATURATION_INPUTS, OUT3, {}, SaturationBody},
			{"Artistic/WhiteBalance", "White Balance", "Artistic/Adjustment", "", WHITE_BALANCE_INPUTS, OUT3, {}, WhiteBalanceBody},
			{"Artistic/Blend", "Blend", "Artistic/Blend",
			 "burn darken difference dodge divide exclusion hard light hard mix linear burn linear dodge linear light multiply negate overlay pin "
			 "light screen soft light subtract vivid light overwrite",
			 BLEND_INPUTS, OUT_VECTOR, BLEND_SETTINGS, BlendBody},
			{"Artistic/Dither", "Dither", "Artistic/Filter", "blue noise half tone", DITHER_INPUTS, OUT_VECTOR, {}, DitherBody},
			{"Artistic/FadeTransition", "Fade Transition", "Artistic/Filter", "fade", FADE_INPUTS, FADE_OUTPUTS, {}, FadeTransitionBody},
			{"Artistic/ChannelMask", "Channel Mask", "Artistic/Mask", "component mask", IN_VECTOR, OUT_VECTOR, CHANNEL_MASK_SETTINGS, ChannelMaskBody},
			{"Artistic/ColorMask", "Color Mask", "Artistic/Mask", "", COLOR_MASK_INPUTS, OUT1, {}, ColorMaskBody},
			{"Artistic/NormalBlend", "Normal Blend", "Artistic/Normal", "", NORMAL_BLEND_INPUTS, OUT3, NORMAL_BLEND_SETTINGS, NormalBlendBody},
			{"Artistic/NormalFromHeight", "Normal From Height", "Artistic/Normal", "convert to normal bump map", NORMAL_FROM_HEIGHT_INPUTS, OUT3,
			 NORMAL_FROM_HEIGHT_SETTINGS, NormalFromHeightBody},
			{"Artistic/NormalFromTexture", "Normal From Texture", "Artistic/Normal", "convert to normal bump map", NORMAL_FROM_TEXTURE_INPUTS, OUT3, {}, NormalFromTextureBody},
			{"Artistic/NormalReconstructZ", "Normal Reconstruct Z", "Artistic/Normal", "derive z", NORMAL_RECONSTRUCT_INPUTS, OUT3, {}, NormalReconstructZBody},
			{"Artistic/NormalStrength", "Normal Strength", "Artistic/Normal", "intensity", NORMAL_STRENGTH_INPUTS, OUT3, {}, NormalStrengthBody},
			{"Artistic/NormalUnpack", "Normal Unpack", "Artistic/Normal", "", NORMAL_UNPACK_INPUTS, OUT3, NORMAL_UNPACK_SETTINGS, NormalUnpackBody},
			{"Artistic/ColorspaceConversion", "Colorspace Conversion", "Artistic/Utility", "", IN3, OUT3, COLORSPACE_SETTINGS,
			 ColorspaceConversionBody},
		};

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphArtisticDefinitions() { return ARTISTIC_DEFINITIONS; }

} // namespace Shalltear
