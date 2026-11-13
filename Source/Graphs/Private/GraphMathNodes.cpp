#include "GraphNodeLibrary.h"
#include "GraphSpaces.h"

#include <array>

namespace Shalltear {

	namespace {

		using P = GraphPortType;
		using S = GraphSettingKind;
		using B = GraphProgramBuilder;

		constexpr GraphPortDefinition InZero[] = {{"In", P::DynamicVector}};
		constexpr GraphPortDefinition InOne[] = {{"In", P::DynamicVector, {1, 1, 1, 1}}};
		constexpr GraphPortDefinition OutVector[] = {{"Out", P::DynamicVector}};
		constexpr GraphPortDefinition OutFloat[] = {{"Out", P::Float}};
		constexpr GraphPortDefinition AZeroBZero[] = {{"A", P::DynamicVector}, {"B", P::DynamicVector}};
		constexpr GraphPortDefinition AZeroBOne[] = {{"A", P::DynamicVector}, {"B", P::DynamicVector, {1, 1, 1, 1}}};
		constexpr GraphPortDefinition AZeroBTwo[] = {{"A", P::DynamicVector}, {"B", P::DynamicVector, {2, 2, 2, 2}}};
		constexpr GraphPortDefinition AZeroBUp[] = {{"A", P::DynamicVector}, {"B", P::DynamicVector, {0, 1, 0, 0}}};

		template <GraphOp Op>
		void UnaryBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Unary(Op, c.In(0));
		}

		template <GraphOp Op>
		void BinaryBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Binary(Op, c.In(0), c.In(1));
		}

		template <GraphOp Op>
		void TernaryBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Ternary(Op, c.In(0), c.In(1), c.In(2));
		}

		constexpr GraphPortDefinition SubtractInputs[] = {{"A", P::DynamicVector, {1, 1, 1, 1}}, {"B", P::DynamicVector, {1, 1, 1, 1}}};
		constexpr GraphPortDefinition MultiplyInputs[] = {{"A", P::Dynamic}, {"B", P::Dynamic, {2, 2, 2, 2}}};
		constexpr GraphPortDefinition MultiplyOutputs[] = {{"Out", P::Dynamic}};
		constexpr GraphPortDefinition PosterizeInputs[] = {{"In", P::DynamicVector}, {"Steps", P::DynamicVector, {4, 4, 4, 4}}};

		constexpr std::string_view ExponentialBases[] = {"BaseE", "Base2"};
		constexpr std::string_view LogBases[] = {"BaseE", "Base2", "Base10"};
		constexpr std::string_view ReciprocalMethods[] = {"Default", "Fast"};
		constexpr GraphSettingDefinition ExponentialSettings[] = {{"Base", GraphSettingKind::Choice, ExponentialBases}};
		constexpr GraphSettingDefinition LogSettings[] = {{"Base", GraphSettingKind::Choice, LogBases}};
		constexpr GraphSettingDefinition ReciprocalSettings[] = {{"Method", GraphSettingKind::Choice, ReciprocalMethods}};

		void MultiplyBody(GraphBodyContext& c) {
			bool matrix = GraphTypes::MatrixDimension(c.TypeOf(0)) != 0 || GraphTypes::MatrixDimension(c.TypeOf(1)) != 0;
			c.outputs[0] = c.builder.Binary(matrix ? GraphOp::MatrixMultiply : GraphOp::Multiply, c.In(0), c.In(1));
		}

		void ExponentialBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Unary(c.Setting(0) == 1 ? GraphOp::Exponential2 : GraphOp::Exponential, c.In(0));
		}

		void LogBody(GraphBodyContext& c) {
			constexpr GraphOp OPS[] = {GraphOp::Logarithm, GraphOp::Logarithm2, GraphOp::Logarithm10};
			c.outputs[0] = c.builder.Unary(OPS[c.Setting(0)], c.In(0));
		}

		void PosterizeBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId step = b.Divide(b.Scalar(1.0f), c.In(1));
			c.outputs[0] = b.Multiply(b.Unary(GraphOp::Floor, b.Divide(c.In(0), step)), step);
		}

		void ReciprocalBody(GraphBodyContext& c) {
			B& b = c.builder;
			c.outputs[0] = c.Setting(0) == 1 ? b.Unary(GraphOp::Reciprocal, c.In(0)) : b.Divide(b.Scalar(1.0f), c.In(0));
		}

		constexpr GraphPortDefinition StepInputs[] = {{"Edge", P::DynamicVector, {1, 1, 1, 1}}, {"In", P::DynamicVector}};
		constexpr GraphPortDefinition LerpInputs[] = {{"A", P::DynamicVector}, {"B", P::DynamicVector, {1, 1, 1, 1}}, {"T", P::DynamicVector}};
		constexpr GraphPortDefinition SmoothstepInputs[] = {
			{"Edge1", P::DynamicVector}, {"Edge2", P::DynamicVector, {1, 1, 1, 1}}, {"In", P::DynamicVector}};
		constexpr GraphPortDefinition ClampInputs[] = {{"In", P::DynamicVector}, {"Min", P::DynamicVector}, {"Max", P::DynamicVector, {1, 1, 1, 1}}};
		constexpr GraphPortDefinition RandomRangeInputs[] = {{"Seed", P::Float2}, {"Min", P::Float}, {"Max", P::Float, {1, 1, 1, 1}}};
		constexpr GraphPortDefinition RemapInputs[] = {
			{"In", P::DynamicVector, {-1, -1, -1, -1}}, {"In Min Max", P::Float2, {-1, 1, 0, 0}}, {"Out Min Max", P::Float2, {0, 1, 0, 0}}};

		void InverseLerpBody(GraphBodyContext& c) {
			B& b = c.builder;
			c.outputs[0] = b.Divide(b.Subtract(c.In(2), c.In(0)), b.Subtract(c.In(1), c.In(0)));
		}

		void OneMinusBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Subtract(c.builder.Scalar(1.0f), c.In(0));
		}

		void RandomRangeBody(GraphBodyContext& c) {
			B& b = c.builder;
			const f32 weights[] = {12.9898f, 78.233f};
			GraphValueId angle = b.Dot(c.In(0), b.Constant(GraphValueType::Float2, weights));
			GraphValueId random = b.Unary(GraphOp::Fraction, b.Multiply(b.Unary(GraphOp::Sine, angle), b.Scalar(43758.5453f)));
			c.outputs[0] = b.Lerp(c.In(1), c.In(2), random);
		}

		void RemapBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId inMin = b.Swizzle(c.In(1), "x");
			GraphValueId inMax = b.Swizzle(c.In(1), "y");
			GraphValueId outMin = b.Swizzle(c.In(2), "x");
			GraphValueId outMax = b.Swizzle(c.In(2), "y");
			GraphValueId scaled = b.Multiply(b.Subtract(c.In(0), inMin), b.Subtract(outMax, outMin));
			c.outputs[0] = b.Add(outMin, b.Divide(scaled, b.Subtract(inMax, inMin)));
		}

		constexpr GraphPortDefinition NoiseSineWaveInputs[] = {{"In", P::DynamicVector}, {"Min Max", P::Float2, {-0.5f, 0.5f, 1, 1}}};

		void DegreesToRadiansBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Multiply(c.In(0), c.builder.Scalar(static_cast<f32>(3.14159265358979323846 / 180.0)));
		}

		void RadiansToDegreesBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Multiply(c.In(0), c.builder.Scalar(static_cast<f32>(180.0 / 3.14159265358979323846)));
		}

		void NoiseSineWaveBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId sinIn = b.Unary(GraphOp::Sine, c.In(0));
			GraphValueId sinInOffset = b.Unary(GraphOp::Sine, b.Add(c.In(0), b.Scalar(1.0f)));
			GraphValueId angle = b.Multiply(b.Subtract(sinIn, sinInOffset), b.Scalar(static_cast<f32>(12.9898 + 78.233)));
			GraphValueId random = b.Unary(GraphOp::Fraction, b.Multiply(b.Unary(GraphOp::Sine, angle), b.Scalar(43758.5453f)));
			GraphValueId noise = b.Lerp(b.Swizzle(c.In(1), "x"), b.Swizzle(c.In(1), "y"), random);
			c.outputs[0] = b.Add(sinIn, noise);
		}

		GraphValueId Sawtooth(B& b, GraphValueId in) {
			return b.Multiply(b.Scalar(2.0f), b.Subtract(in, b.Unary(GraphOp::Floor, b.Add(b.Scalar(0.5f), in))));
		}

		void SawtoothWaveBody(GraphBodyContext& c) { c.outputs[0] = Sawtooth(c.builder, c.In(0)); }

		void SquareWaveBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId rounded = b.Unary(GraphOp::Round, b.Unary(GraphOp::Fraction, c.In(0)));
			c.outputs[0] = b.Subtract(b.Scalar(1.0f), b.Multiply(b.Scalar(2.0f), rounded));
		}

		void TriangleWaveBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId folded = b.Multiply(b.Scalar(2.0f), b.Unary(GraphOp::Absolute, Sawtooth(b, c.In(0))));
			c.outputs[0] = b.Subtract(folded, b.Scalar(1.0f));
		}

		void DdxyBody(GraphBodyContext& c) {
			B& b = c.builder;
			c.outputs[0] = b.Add(b.Unary(GraphOp::Absolute, b.Unary(GraphOp::DerivativeX, c.In(0))),
								 b.Unary(GraphOp::Absolute, b.Unary(GraphOp::DerivativeY, c.In(0))));
		}

		constexpr std::string_view MatrixAxes[] = {"Row", "Column"};
		constexpr GraphSettingDefinition MatrixAxisSettings[] = {{"Axis", GraphSettingKind::Choice, MatrixAxes}};
		constexpr GraphPortDefinition MatrixConstructionInputs[] = {{"M0", P::Float4}, {"M1", P::Float4}, {"M2", P::Float4}, {"M3", P::Float4}};
		constexpr GraphPortDefinition MatrixConstructionOutputs[] = {{"4x4", P::Float4x4}, {"3x3", P::Float3x3}, {"2x2", P::Float2x2}};
		constexpr GraphPortDefinition MatrixIn[] = {{"In", P::DynamicMatrix}};
		constexpr GraphPortDefinition MatrixOut[] = {{"Out", P::DynamicMatrix}};
		constexpr GraphPortDefinition MatrixSplitOutputs[] = {
			{"M0", P::DynamicVector}, {"M1", P::DynamicVector}, {"M2", P::DynamicVector}, {"M3", P::DynamicVector}};

		constexpr std::string_view Components[] = {"x", "y", "z", "w"};

		void MatrixConstructionBody(GraphBodyContext& c) {
			B& b = c.builder;
			bool columns = c.Setting(0) == 1;
			constexpr std::string_view Leading = "xyzw";
			for (u32 dimension = 4; dimension >= 2; --dimension) {
				std::array<GraphValueId, 4> rows{};
				for (u32 row = 0; row < dimension; ++row) {
					if (!columns) {
						rows[row] = b.Swizzle(c.In(row), Leading.substr(0, dimension));
						continue;
					}
					std::array<GraphValueId, 4> parts{};
					for (u32 column = 0; column < dimension; ++column) {
						parts[column] = b.Swizzle(c.In(column), Components[row]);
					}
					rows[row] = b.Construct(GraphTypes::Vector(dimension), {parts.data(), dimension});
				}
				c.outputs[4 - dimension] = b.Construct(GraphTypes::Matrix(dimension), {rows.data(), dimension});
			}
		}

		void MatrixSplitBody(GraphBodyContext& c) {
			B& b = c.builder;
			u32 dimension = GraphTypes::MatrixDimension(c.TypeOf(0));
			bool columns = c.Setting(0) == 1;
			for (u32 output = 0; output < 4; ++output) {
				if (output >= dimension) {
					const f32 zero[4] = {};
					c.outputs[output] = b.Constant(GraphTypes::Vector(dimension), {zero, dimension});
					continue;
				}
				if (!columns) {
					c.outputs[output] = b.MatrixRow(c.In(0), output);
					continue;
				}
				std::array<GraphValueId, 4> parts{};
				for (u32 row = 0; row < dimension; ++row) {
					parts[row] = b.Swizzle(b.MatrixRow(c.In(0), row), Components[output]);
				}
				c.outputs[output] = b.Construct(GraphTypes::Vector(dimension), {parts.data(), dimension});
			}
		}

		constexpr GraphPortDefinition CrossInputs[] = {{"A", P::Float3}, {"B", P::Float3, {0, 1, 0, 0}}};
		constexpr GraphPortDefinition CrossOutputs[] = {{"Out", P::Float3}};
		constexpr GraphPortDefinition FresnelInputs[] = {
			{"Normal", P::Float3, {}, "WorldSpaceNormal"}, {"View Dir", P::Float3, {}, "WorldSpaceViewDirection"}, {"Power", P::Float, {1, 1, 1, 1}}};
		constexpr GraphPortDefinition ReflectionInputs[] = {{"In", P::DynamicVector}, {"Normal", P::DynamicVector, {0, 1, 0, 0}}};
		constexpr GraphPortDefinition RefractInputs[] = {
			{"Incident", P::Float3, {0, 0, 1, 0}}, {"Normal", P::Float3, {0, 0, -1, 0}}, {"IORSource", P::Float, {1, 0, 0, 0}}, {"IORMedium", P::Float, {1.5f, 0, 0, 0}}};
		constexpr GraphPortDefinition RefractOutputs[] = {{"Refracted", P::Float3}, {"Intensity", P::Float}};
		constexpr std::string_view RefractModes[] = {"CriticalAngle", "Safe"};
		constexpr GraphSettingDefinition RefractSettings[] = {{"Mode", GraphSettingKind::Choice, RefractModes, 1}};
		constexpr GraphPortDefinition RotateInputs[] = {{"In", P::Float3}, {"Axis", P::Float3}, {"Rotation", P::Float}};
		constexpr std::string_view RotationUnits[] = {"Radians", "Degrees"};
		constexpr GraphSettingDefinition RotateSettings[] = {{"Unit", GraphSettingKind::Choice, RotationUnits}};
		constexpr GraphPortDefinition SphereMaskInputs[] = {{"Coords", P::DynamicVector},
															{"Center", P::DynamicVector, {0.5f, 0.5f, 0.5f, 0.5f}},
															{"Radius", P::Float, {0.1f, 0.1f, 0.1f, 0.1f}},
															{"Hardness", P::Float, {0.8f, 0.8f, 0.8f, 0.8f}}};

		void FresnelBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId facing = b.Unary(GraphOp::Saturate, b.Dot(b.Unary(GraphOp::Normalize, c.In(0)), c.In(1)));
			c.outputs[0] = b.Binary(GraphOp::Power, b.Subtract(b.Scalar(1.0f), facing), c.In(2));
		}

		GraphValueId Projection(B& b, GraphValueId a, GraphValueId onto) {
			return b.Divide(b.Multiply(onto, b.Dot(a, onto)), b.Dot(onto, onto));
		}

		void ProjectionBody(GraphBodyContext& c) { c.outputs[0] = Projection(c.builder, c.In(0), c.In(1)); }

		void RejectionBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Subtract(c.In(0), Projection(c.builder, c.In(0), c.In(1)));
		}

		GraphValueId Square(B& b, GraphValueId x) { return b.Multiply(x, x); }

		void RefractBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId one = b.Scalar(1.0f);
			GraphValueId zero = b.Scalar(0.0f);
			GraphValueId incident = c.In(0);
			GraphValueId normal = c.In(1);
			GraphValueId source = b.Maximum(c.In(2), one);
			GraphValueId medium = b.Maximum(c.In(3), one);
			GraphValueId eta = b.Divide(source, medium);
			GraphValueId cos0 = b.Dot(incident, normal);
			GraphValueId k = b.Subtract(one, b.Multiply(b.Multiply(eta, eta), b.Subtract(one, b.Multiply(cos0, cos0))));
			GraphValueId refracts = b.Binary(GraphOp::LessEqual, zero, k);
			bool safe = c.Setting(0) == 1;
			GraphValueId root = b.Unary(GraphOp::SquareRoot, safe ? b.Maximum(k, zero) : k);
			GraphValueId refracted = b.Subtract(b.Multiply(eta, incident), b.Multiply(b.Add(b.Multiply(eta, cos0), root), normal));
			c.outputs[0] = safe ? refracted : b.Select(refracts, refracted, b.Binary(GraphOp::Reflect, incident, normal));

			GraphValueId u = b.Unary(GraphOp::Negate, cos0);
			GraphValueId f0 = Square(b, b.Divide(b.Subtract(medium, source), b.Add(medium, source)));
			GraphValueId x = b.Subtract(one, u);
			GraphValueId x5 = b.Multiply(b.Multiply(x, Square(b, x)), Square(b, x));
			GraphValueId transmitted = b.Subtract(b.Subtract(one, b.Multiply(one, x5)), b.Multiply(f0, b.Subtract(one, x5)));
			GraphValueId ior = b.Divide(medium, source);
			GraphValueId g = b.Unary(GraphOp::SquareRoot, b.Subtract(b.Add(Square(b, ior), Square(b, u)), one));
			GraphValueId first = Square(b, b.Divide(b.Subtract(g, u), b.Add(g, u)));
			GraphValueId numerator = b.Subtract(b.Multiply(b.Add(g, u), u), one);
			GraphValueId denominator = b.Add(b.Multiply(b.Subtract(g, u), u), one);
			GraphValueId second = b.Add(one, Square(b, b.Divide(numerator, denominator)));
			GraphValueId dielectric =
				b.Subtract(one, b.Unary(GraphOp::Saturate, b.Subtract(one, b.Multiply(b.Multiply(b.Scalar(0.5f), first), second))));
			GraphValueId reflected = b.Select(refracts, dielectric, safe ? zero : one);
			c.outputs[1] = b.Select(b.Binary(GraphOp::LessEqual, source, medium), b.Unary(GraphOp::Saturate, transmitted), reflected);
		}

		void RotateAboutAxisBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId rotation = c.In(2);
			if (c.Setting(0) == 1) {
				rotation = b.Multiply(rotation, b.Scalar(static_cast<f32>(3.14159265358979323846 / 180.0)));
			}
			GraphValueId sine = b.Unary(GraphOp::Sine, rotation);
			GraphValueId cosine = b.Unary(GraphOp::Cosine, rotation);
			GraphValueId axis = b.Unary(GraphOp::Normalize, c.In(1));
			GraphValueId in = c.In(0);
			GraphValueId sum = b.Add(b.Multiply(in, cosine), b.Multiply(b.Binary(GraphOp::Cross, axis, in), sine));
			GraphValueId along = b.Multiply(b.Multiply(axis, b.Dot(axis, in)), b.Subtract(b.Scalar(1.0f), cosine));
			c.outputs[0] = b.Add(sum, along);
		}

		void SphereMaskBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId inside = b.Divide(b.Subtract(b.Binary(GraphOp::Distance, c.In(0), c.In(1)), c.In(2)), b.Subtract(b.Scalar(1.0f), c.In(3)));
			c.outputs[0] = b.Subtract(b.Scalar(1.0f), b.Unary(GraphOp::Saturate, inside));
		}

		constexpr GraphSettingDefinition TransformSettings[] = {{"From", S::Choice, GraphSpaces::Names, 0},
																{"To", S::Choice, GraphSpaces::Names, 2},
																{"Type", S::Choice, GraphSpaces::Conversions, 0},
																{"Normalize", S::Toggle, {}, 1}};
		constexpr GraphPortDefinition TransformInputs[] = {{"In", P::Float3}};
		constexpr GraphPortDefinition TransformOutput[] = {{"Out", P::Float3}};

		void TransformBody(GraphBodyContext& c) {
			c.outputs[0] = GraphSpaces::Transform(c, c.In(0), static_cast<GraphSpace>(c.Setting(0)), static_cast<GraphSpace>(c.Setting(1)),
												  static_cast<GraphConversion>(c.Setting(2)), c.Setting(3) != 0);
		}

		constexpr GraphNodeDefinition MathDefinitions[] = {
			{"Math/Add", "Add", "Math/Basic", "addition sum plus", AZeroBZero, OutVector, {}, BinaryBody<GraphOp::Add>},
			{"Math/Subtract", "Subtract", "Math/Basic", "subtraction remove minus take away", SubtractInputs, OutVector, {}, BinaryBody<GraphOp::Subtract>},
			{"Math/Multiply", "Multiply", "Math/Basic", "multiplication times x", MultiplyInputs, MultiplyOutputs, {}, MultiplyBody},
			{"Math/Divide", "Divide", "Math/Basic", "division divided by", AZeroBTwo, OutVector, {}, BinaryBody<GraphOp::Divide>},
			{"Math/Power", "Power", "Math/Basic", "", AZeroBTwo, OutVector, {}, BinaryBody<GraphOp::Power>},
			{"Math/SquareRoot", "Square Root", "Math/Basic", "sqrt", InZero, OutVector, {}, UnaryBody<GraphOp::SquareRoot>},

			{"Math/Absolute", "Absolute", "Math/Advanced", "positive", InZero, OutVector, {}, UnaryBody<GraphOp::Absolute>},
			{"Math/Exponential", "Exponential", "Math/Advanced", "", InZero, OutVector, ExponentialSettings, ExponentialBody},
			{"Math/Length", "Length", "Math/Advanced", "measure", InZero, OutFloat, {}, UnaryBody<GraphOp::Length>},
			{"Math/Log", "Log", "Math/Advanced", "", InOne, OutVector, LogSettings, LogBody},
			{"Math/Modulo", "Modulo", "Math/Advanced", "fmod", AZeroBOne, OutVector, {}, BinaryBody<GraphOp::Modulo>},
			{"Math/Negate", "Negate", "Math/Advanced", "invert opposite", InZero, OutVector, {}, UnaryBody<GraphOp::Negate>},
			{"Math/Normalize", "Normalize", "Math/Advanced", "", InZero, OutVector, {}, UnaryBody<GraphOp::Normalize>},
			{"Math/Posterize", "Posterize", "Math/Advanced", "", PosterizeInputs, OutVector, {}, PosterizeBody},
			{"Math/Reciprocal", "Reciprocal", "Math/Advanced", "rcp", InOne, OutVector, ReciprocalSettings, ReciprocalBody},
			{"Math/ReciprocalSquareRoot", "Reciprocal Square Root", "Math/Advanced", "rsqrt inversesqrt", InOne, OutVector, {}, UnaryBody<GraphOp::ReciprocalSquareRoot>},

			{"Math/Ceiling", "Ceiling", "Math/Round", "up", InZero, OutVector, {}, UnaryBody<GraphOp::Ceiling>},
			{"Math/Floor", "Floor", "Math/Round", "down", InZero, OutVector, {}, UnaryBody<GraphOp::Floor>},
			{"Math/Round", "Round", "Math/Round", "", InZero, OutVector, {}, UnaryBody<GraphOp::Round>},
			{"Math/Sign", "Sign", "Math/Round", "", InZero, OutVector, {}, UnaryBody<GraphOp::Sign>},
			{"Math/Step", "Step", "Math/Round", "", StepInputs, OutVector, {}, BinaryBody<GraphOp::Step>},
			{"Math/Truncate", "Truncate", "Math/Round", "", InZero, OutVector, {}, UnaryBody<GraphOp::Truncate>},

			{"Math/InverseLerp", "Inverse Lerp", "Math/Interpolation", "", LerpInputs, OutVector, {}, InverseLerpBody},
			{"Math/Lerp", "Lerp", "Math/Interpolation", "mix blend linear interpolate", LerpInputs, OutVector, {}, TernaryBody<GraphOp::Lerp>},
			{"Math/Smoothstep", "Smoothstep", "Math/Interpolation", "curve", SmoothstepInputs, OutVector, {}, TernaryBody<GraphOp::Smoothstep>},

			{"Math/Clamp", "Clamp", "Math/Range", "limit", ClampInputs, OutVector, {}, TernaryBody<GraphOp::Clamp>},
			{"Math/Fraction", "Fraction", "Math/Range", "remainder", InZero, OutVector, {}, UnaryBody<GraphOp::Fraction>},
			{"Math/Maximum", "Maximum", "Math/Range", "", AZeroBZero, OutVector, {}, BinaryBody<GraphOp::Maximum>},
			{"Math/Minimum", "Minimum", "Math/Range", "", AZeroBOne, OutVector, {}, BinaryBody<GraphOp::Minimum>},
			{"Math/OneMinus", "One Minus", "Math/Range", "complement invert opposite", InOne, OutVector, {}, OneMinusBody},
			{"Math/RandomRange", "Random Range", "Math/Range", "", RandomRangeInputs, OutFloat, {}, RandomRangeBody},
			{"Math/Remap", "Remap", "Math/Range", "", RemapInputs, OutVector, {}, RemapBody},
			{"Math/Saturate", "Saturate", "Math/Range", "clamp", InZero, OutVector, {}, UnaryBody<GraphOp::Saturate>},

			{"Math/Arccosine", "Arccosine", "Math/Trigonometry", "acos", InOne, OutVector, {}, UnaryBody<GraphOp::Arccosine>},
			{"Math/Arcsine", "Arcsine", "Math/Trigonometry", "asin", InZero, OutVector, {}, UnaryBody<GraphOp::Arcsine>},
			{"Math/Arctangent", "Arctangent", "Math/Trigonometry", "atan", InZero, OutVector, {}, UnaryBody<GraphOp::Arctangent>},
			{"Math/Arctangent2", "Arctangent2", "Math/Trigonometry", "atan2", AZeroBZero, OutVector, {}, BinaryBody<GraphOp::Arctangent2>},
			{"Math/Cosine", "Cosine", "Math/Trigonometry", "", InZero, OutVector, {}, UnaryBody<GraphOp::Cosine>},
			{"Math/DegreesToRadians", "Degrees To Radians", "Math/Trigonometry", "degtorad radians convert", InZero, OutVector, {}, DegreesToRadiansBody},
			{"Math/HyperbolicCosine", "Hyperbolic Cosine", "Math/Trigonometry", "cosh", InZero, OutVector, {}, UnaryBody<GraphOp::HyperbolicCosine>},
			{"Math/HyperbolicSine", "Hyperbolic Sine", "Math/Trigonometry", "sinh", InZero, OutVector, {}, UnaryBody<GraphOp::HyperbolicSine>},
			{"Math/HyperbolicTangent", "Hyperbolic Tangent", "Math/Trigonometry", "tanh", InZero, OutVector, {}, UnaryBody<GraphOp::HyperbolicTangent>},
			{"Math/RadiansToDegrees", "Radians To Degrees", "Math/Trigonometry", "radtodeg degrees convert", InZero, OutVector, {}, RadiansToDegreesBody},
			{"Math/Sine", "Sine", "Math/Trigonometry", "", InZero, OutVector, {}, UnaryBody<GraphOp::Sine>},
			{"Math/Tangent", "Tangent", "Math/Trigonometry", "", InZero, OutVector, {}, UnaryBody<GraphOp::Tangent>},

			{"Math/NoiseSineWave", "Noise Sine Wave", "Math/Wave", "", NoiseSineWaveInputs, OutVector, {}, NoiseSineWaveBody},
			{"Math/SawtoothWave", "Sawtooth Wave", "Math/Wave", "triangle wave", InZero, OutVector, {}, SawtoothWaveBody},
			{"Math/SquareWave", "Square Wave", "Math/Wave", "", InZero, OutVector, {}, SquareWaveBody},
			{"Math/TriangleWave", "Triangle Wave", "Math/Wave", "sawtooth wave", InZero, OutVector, {}, TriangleWaveBody},

			{"Math/DDX", "DDX", "Math/Derivative", "derivative", InZero, OutVector, {}, UnaryBody<GraphOp::DerivativeX>},
			{"Math/DDXY", "DDXY", "Math/Derivative", "derivative", InZero, OutVector, {}, DdxyBody},
			{"Math/DDY", "DDY", "Math/Derivative", "derivative", InZero, OutVector, {}, UnaryBody<GraphOp::DerivativeY>},

			{"Math/MatrixConstruction", "Matrix Construction", "Math/Matrix", "", MatrixConstructionInputs, MatrixConstructionOutputs,
			 MatrixAxisSettings, MatrixConstructionBody, GraphPreviewMode::None},
			{"Math/MatrixDeterminant", "Matrix Determinant", "Math/Matrix", "", MatrixIn, OutFloat, {}, UnaryBody<GraphOp::Determinant>, GraphPreviewMode::None},
			{"Math/MatrixSplit", "Matrix Split", "Math/Matrix", "", MatrixIn, MatrixSplitOutputs, MatrixAxisSettings, MatrixSplitBody, GraphPreviewMode::None},
			{"Math/MatrixTranspose", "Matrix Transpose", "Math/Matrix", "", MatrixIn, MatrixOut, {}, UnaryBody<GraphOp::Transpose>, GraphPreviewMode::None},

			{"Math/CrossProduct", "Cross Product", "Math/Vector", "perpendicular", CrossInputs, CrossOutputs, {}, BinaryBody<GraphOp::Cross>},
			{"Math/Distance", "Distance", "Math/Vector", "", AZeroBZero, OutFloat, {}, BinaryBody<GraphOp::Distance>},
			{"Math/DotProduct", "Dot Product", "Math/Vector", "scalar product", AZeroBUp, OutFloat, {}, BinaryBody<GraphOp::Dot>},
			{"Math/FresnelEffect", "Fresnel Effect", "Math/Vector", "", FresnelInputs, OutFloat, {}, FresnelBody, GraphPreviewMode::Preview3D},
			{"Math/Projection", "Projection", "Math/Vector", "", AZeroBUp, OutVector, {}, ProjectionBody},
			{"Math/Reflection", "Reflection", "Math/Vector", "mirror", ReflectionInputs, OutVector, {}, BinaryBody<GraphOp::Reflect>},
			{"Math/Refract", "Refract", "Math/Vector", "refract warp bend distort", RefractInputs, RefractOutputs, RefractSettings, RefractBody},
			{"Math/Rejection", "Rejection", "Math/Vector", "", AZeroBUp, OutVector, {}, RejectionBody},
			{"Math/RotateAboutAxis", "Rotate About Axis", "Math/Vector", "pivot", RotateInputs, CrossOutputs, RotateSettings, RotateAboutAxisBody},
			{"Math/SphereMask", "Sphere Mask", "Math/Vector", "", SphereMaskInputs, OutFloat, {}, SphereMaskBody},
			{"Math/Transform", "Transform", "Math/Vector", "world tangent object view screen convert", TransformInputs, TransformOutput,
			 TransformSettings, TransformBody},
		};

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphMathDefinitions() { return MathDefinitions; }

} // namespace Shalltear
