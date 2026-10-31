#include "GraphNodeLibrary.h"
#include "GraphSpaces.h"

// uvs run down (like glTF)

namespace Shalltear {

	namespace {

		using P = GraphPortType;
		using S = GraphSettingKind;
		using B = GraphProgramBuilder;

		constexpr std::string_view UV0 = "UV0";
		constexpr f32 FLOAT_EPSILON = 5.960464478e-8f;

		constexpr GraphPortDefinition OUT_UV[] = {{"Out", P::Float2}};

		GraphValueId Vector2(B& b, GraphValueId x, GraphValueId y) {
			const GraphValueId parts[] = {x, y};
			return b.Construct(GraphValueType::Float2, parts);
		}

		GraphValueId Vector3(B& b, GraphValueId x, GraphValueId y, GraphValueId z) {
			const GraphValueId parts[] = {x, y, z};
			return b.Construct(GraphValueType::Float3, parts);
		}

		GraphValueId TextureSpaceViewDirection(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId tangent = c.Binding("TangentSpaceViewDirection");
			return Vector3(b, b.Swizzle(tangent, "x"), b.Unary(GraphOp::Negate, b.Swizzle(tangent, "y")), b.Swizzle(tangent, "z"));
		}

		constexpr std::string_view CHANNEL_SWIZZLES[] = {"x", "y", "z", "w"};
		constexpr std::string_view CHANNELS[] = {"Red", "Green", "Blue", "Alpha"};

		constexpr GraphPortDefinition FLIPBOOK_INPUTS[] = {
			{"UV", P::Float2, {}, UV0}, {"Width", P::Float, {1.0f, 1.0f, 1.0f, 1.0f}}, {"Height", P::Float, {1.0f, 1.0f, 1.0f, 1.0f}}, {"Tile", P::Float}};
		constexpr GraphSettingDefinition FLIPBOOK_SETTINGS[] = {{"Invert X", S::Toggle, {}, 0}, {"Invert Y", S::Toggle, {}, 0}};

		void FlipbookBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId width = c.In(1);
			GraphValueId height = c.In(2);
			GraphValueId one = b.Scalar(1.0f);
			GraphValueId tile = b.Unary(GraphOp::Floor, b.Binary(GraphOp::Modulo, b.Add(c.In(3), b.Scalar(0.00001f)), b.Multiply(width, height)));
			const f32 ones[] = {1.0f, 1.0f};
			GraphValueId tileCount = b.Divide(b.Constant(GraphValueType::Float2, ones), Vector2(b, width, height));
			GraphValueId base = b.Unary(GraphOp::Floor, b.Multiply(b.Add(tile, b.Scalar(0.5f)), b.Swizzle(tileCount, "x")));
			GraphValueId column = b.Subtract(tile, b.Multiply(width, base));
			GraphValueId tileX = c.Setting(0) != 0 ? b.Subtract(width, b.Add(column, one)) : column;
			GraphValueId tileY = c.Setting(1) != 0 ? b.Subtract(height, b.Add(base, one)) : base;
			c.outputs[0] = b.Multiply(b.Add(c.In(0), Vector2(b, tileX, tileY)), tileCount);
		}

		constexpr GraphPortDefinition PARALLAX_INPUTS[] = {
			{"Heightmap", P::Texture2D}, {"Amplitude", P::Float, {1.0f, 1.0f, 1.0f, 1.0f}}, {"UVs", P::Float2, {}, UV0}};
		constexpr GraphPortDefinition PARALLAX_OUTPUTS[] = {{"ParallaxUVs", P::Float2}};
		constexpr GraphSettingDefinition PARALLAX_SETTINGS[] = {{"Heightmap Sample Channel", S::Choice, CHANNELS, 1}};

		void ParallaxMappingBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId uvs = c.In(2);
			const GraphValueId sample[] = {c.In(0), uvs};
			GraphValueId height = b.Swizzle(b.TextureOp(GraphOp::SampleTexture, sample), CHANNEL_SWIZZLES[c.Setting(0)]);
			GraphValueId amplitude = b.Multiply(c.In(1), b.Scalar(0.01f));
			GraphValueId centred = b.Subtract(b.Multiply(height, amplitude), b.Divide(amplitude, b.Scalar(2.0f)));
			GraphValueId view = b.Unary(GraphOp::Normalize, TextureSpaceViewDirection(c));
			GraphValueId lifted = b.Add(b.Swizzle(view, "z"), b.Scalar(0.42f));
			c.outputs[0] = b.Add(uvs, b.Multiply(centred, b.Divide(b.Swizzle(view, "xy"), lifted)));
		}

		constexpr GraphPortDefinition POM_INPUTS[] = {{"Heightmap", P::Texture2D},
													  {"Amplitude", P::Float, {1.0f, 1.0f, 1.0f, 1.0f}},
													  {"Steps", P::Float, {5.0f, 5.0f, 5.0f, 5.0f}},
													  {"UVs", P::Float2, {}, UV0},
													  {"Tiling", P::Float2, {1.0f, 1.0f, 1.0f, 1.0f}},
													  {"Offset", P::Float2},
													  {"PrimitiveSize", P::Float2, {1.0f, 1.0f, 1.0f, 1.0f}},
													  {"LOD", P::Float},
													  {"LODThreshold", P::Float}};
		constexpr GraphPortDefinition POM_OUTPUTS[] = {{"PixelDepthOffset", P::Float}, {"ParallaxUVs", P::Float2}};
		constexpr GraphSettingDefinition POM_SETTINGS[] = {{"Heightmap Sample Channel", S::Choice, CHANNELS, 0}};

		void ParallaxOcclusionMappingBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId tiling = c.In(4);
			GraphValueId worldToObject = c.Binding("WorldToObject");
			GraphValueId scaleX = b.Unary(GraphOp::Length, b.Swizzle(b.MatrixRow(worldToObject, 0), "xyz"));
			GraphValueId scaleZ = b.Unary(GraphOp::Length, b.Swizzle(b.MatrixRow(worldToObject, 2), "xyz"));
			GraphValueId view = b.Multiply(TextureSpaceViewDirection(c), Vector3(b, scaleX, scaleZ, b.Scalar(1.0f)));
			GraphValueId facing = b.Swizzle(view, "z");
			GraphValueId maxHeight = b.Multiply(c.In(1), b.Scalar(0.01f));
			GraphValueId spread = b.Add(b.Unary(GraphOp::Absolute, b.Swizzle(tiling, "x")), b.Unary(GraphOp::Absolute, b.Swizzle(tiling, "y")));
			maxHeight = b.Multiply(maxHeight, b.Divide(b.Scalar(2.0f), spread));
			GraphValueId uvSpaceScale = b.Divide(b.Multiply(maxHeight, tiling), c.In(6));
			const GraphValueId scaled[] = {b.Multiply(b.Swizzle(view, "xy"), uvSpaceScale), facing};
			GraphValueId viewUV = b.Unary(GraphOp::Normalize, b.Construct(GraphValueType::Float3, scaled));
			GraphValueId uvs = b.Add(b.Multiply(c.In(3), tiling), c.In(5));
			GraphValueId steps = b.Maximum(b.Minimum(c.In(2), b.Scalar(256.0f)), b.Scalar(1.0f));
			const GraphValueId operands[] = {c.In(0), uvs, viewUV, Vector3(b, c.In(7), c.In(8), steps)};
			GraphValueId search = b.TextureOp(GraphOp::ParallaxOcclusion, operands, c.Setting(0));
			GraphValueId depth = b.Subtract(maxHeight, b.Multiply(b.Swizzle(search, "z"), maxHeight));
			c.outputs[0] = b.Divide(depth, b.Maximum(facing, b.Scalar(0.0001f)));
			c.outputs[1] = b.Add(uvs, b.Swizzle(search, "xy"));
		}

		constexpr GraphPortDefinition POLAR_INPUTS[] = {{"UV", P::Float2, {}, UV0},
														{"Center", P::Float2, {0.5f, 0.5f, 0.5f, 0.5f}},
														{"RadialScale", P::Float, {1.0f, 1.0f, 1.0f, 1.0f}},
														{"LengthScale", P::Float, {1.0f, 1.0f, 1.0f, 1.0f}}};

		void PolarCoordinatesBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId delta = b.Subtract(c.In(0), c.In(1));
			GraphValueId radius = b.Multiply(b.Multiply(b.Unary(GraphOp::Length, delta), b.Scalar(2.0f)), c.In(2));
			GraphValueId turn = b.Binary(GraphOp::Arctangent2, b.Swizzle(delta, "x"), b.Swizzle(delta, "y"));
			GraphValueId angle = b.Multiply(b.Divide(turn, b.Scalar(6.28f)), c.In(3));
			c.outputs[0] = Vector2(b, radius, angle);
		}

		constexpr GraphPortDefinition SHEAR_INPUTS[] = {{"UV", P::Float2, {}, UV0},
														{"Center", P::Float2, {0.5f, 0.5f, 0.5f, 0.5f}},
														{"Strength", P::Float2, {10.0f, 10.0f, 10.0f, 10.0f}},
														{"Offset", P::Float2}};

		void RadialShearBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId delta = b.Subtract(c.In(0), c.In(1));
			GraphValueId offset = b.Multiply(b.Dot(delta, delta), c.In(2));
			GraphValueId turned = Vector2(b, b.Swizzle(delta, "y"), b.Unary(GraphOp::Negate, b.Swizzle(delta, "x")));
			c.outputs[0] = b.Add(b.Add(c.In(0), b.Multiply(turned, offset)), c.In(3));
		}

		void SpherizeBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId delta = b.Subtract(c.In(0), c.In(1));
			GraphValueId delta2 = b.Dot(delta, delta);
			GraphValueId offset = b.Multiply(b.Multiply(delta2, delta2), c.In(2));
			c.outputs[0] = b.Add(b.Add(c.In(0), b.Multiply(delta, offset)), c.In(3));
		}

		constexpr std::string_view ROTATION_UNITS[] = {"Radians", "Degrees"};
		constexpr GraphSettingDefinition ROTATE_SETTINGS[] = {{"Unit", S::Choice, ROTATION_UNITS}};
		constexpr GraphPortDefinition ROTATE_INPUTS[] = {{"UV", P::Float2, {}, UV0}, {"Center", P::Float2, {0.5f, 0.5f, 0.5f, 0.5f}}, {"Rotation", P::Float}};

		void RotateBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId rotation = c.In(2);
			if (c.Setting(0) == 1) {
				rotation = b.Multiply(rotation, b.Divide(b.Scalar(3.1415926f), b.Scalar(180.0f)));
			}
			GraphValueId uv = b.Subtract(c.In(0), c.In(1));
			GraphValueId s = b.Unary(GraphOp::Sine, rotation);
			GraphValueId cosine = b.Unary(GraphOp::Cosine, rotation);
			GraphValueId y = b.Dot(uv, Vector2(b, b.Unary(GraphOp::Negate, s), cosine));
			GraphValueId x = b.Dot(uv, Vector2(b, cosine, s));
			c.outputs[0] = b.Add(Vector2(b, x, y), c.In(1));
		}

		constexpr GraphPortDefinition TILING_INPUTS[] = {{"UV", P::Float2, {}, UV0}, {"Tiling", P::Float2, {1.0f, 1.0f, 1.0f, 1.0f}}, {"Offset", P::Float2}};

		void TilingAndOffsetBody(GraphBodyContext& c) {
			B& b = c.builder;
			c.outputs[0] = b.Add(b.Multiply(c.In(0), c.In(1)), c.In(2));
		}

		constexpr GraphPortDefinition TWIRL_INPUTS[] = {{"UV", P::Float2, {}, UV0},
														{"Center", P::Float2, {0.5f, 0.5f, 0.5f, 0.5f}},
														{"Strength", P::Float, {10.0f, 0.0f, 0.0f, 0.0f}},
														{"Offset", P::Float2}};

		void TwirlBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId center = c.In(1);
			GraphValueId offset = c.In(3);
			GraphValueId delta = b.Subtract(c.In(0), center);
			GraphValueId angle = b.Multiply(c.In(2), b.Unary(GraphOp::Length, delta));
			GraphValueId cosine = b.Unary(GraphOp::Cosine, angle);
			GraphValueId sine = b.Unary(GraphOp::Sine, angle);
			GraphValueId dx = b.Swizzle(delta, "x");
			GraphValueId dy = b.Swizzle(delta, "y");
			GraphValueId x = b.Subtract(b.Multiply(cosine, dx), b.Multiply(sine, dy));
			GraphValueId y = b.Add(b.Multiply(sine, dx), b.Multiply(cosine, dy));
			c.outputs[0] = Vector2(b, b.Add(b.Add(x, b.Swizzle(center, "x")), b.Swizzle(offset, "x")),
								   b.Add(b.Add(y, b.Swizzle(center, "y")), b.Swizzle(offset, "y")));
		}

		constexpr std::string_view TRIPLANAR_TYPES[] = {"Default", "Normal"};
		constexpr std::string_view TRIPLANAR_SPACES[] = {"Object", "View", "World", "Tangent", "AbsoluteWorld"};
		constexpr GraphSettingDefinition TRIPLANAR_SETTINGS[] = {
			{"Type", S::Choice, TRIPLANAR_TYPES}, {"Input Space", S::Choice, TRIPLANAR_SPACES, 4}, {"Normal Output Space", S::Choice, TRIPLANAR_SPACES, 3}};
		constexpr GraphPortDefinition TRIPLANAR_INPUTS[] = {{"Texture", P::Texture2D},
															{"Position", P::Float3, {}, "AbsoluteWorldSpacePosition"},
															{"Normal", P::Float3, {}, "WorldSpaceNormal"},
															{"Tile", P::Float, {1.0f, 1.0f, 1.0f, 1.0f}},
															{"Blend", P::Float, {1.0f, 1.0f, 1.0f, 1.0f}}};
		constexpr GraphPortDefinition TRIPLANAR_OUTPUTS[] = {{"Out", P::Float4}};

		void TriplanarBody(GraphBodyContext& c) {
			B& b = c.builder;
			const u32 space = c.Setting(1);
			const std::string_view normalSpace = space == 4 ? std::string_view("World") : TRIPLANAR_SPACES[space];
			GraphValueId position = c.IsConnected(1) ? c.In(1) : c.Binding(str(TRIPLANAR_SPACES[space]) + "SpacePosition");
			GraphValueId normal = c.IsConnected(2) ? c.In(2) : c.Binding(str(normalSpace) + "SpaceNormal");
			GraphValueId uv = b.Multiply(position, c.In(3));
			GraphValueId exponent = b.Minimum(c.In(4), b.Scalar(158.0f));
			GraphValueId weights = b.Binary(GraphOp::Power, b.Maximum(b.Unary(GraphOp::Absolute, normal), b.Scalar(FLOAT_EPSILON)), exponent);
			if (c.Setting(0) == 0) {
				const f32 ones[] = {1.0f, 1.0f, 1.0f};
				weights = b.Divide(weights, b.Dot(weights, b.Constant(GraphValueType::Float3, ones)));
			} else {
				weights = b.Divide(weights, b.Add(b.Add(b.Swizzle(weights, "x"), b.Swizzle(weights, "y")), b.Swizzle(weights, "z")));
			}
			auto project = [&](std::string_view axes) {
				GraphValueId pair = b.Swizzle(uv, axes);
				GraphValueId flipped = Vector2(b, b.Swizzle(pair, "x"), b.Unary(GraphOp::Negate, b.Swizzle(pair, "y")));
				const GraphValueId operands[] = {c.In(0), flipped};
				return b.TextureOp(GraphOp::SampleTexture, operands);
			};
			GraphValueId sampleX = project("zy");
			GraphValueId sampleY = project("xz");
			GraphValueId sampleZ = project("xy");
			if (c.Setting(0) == 0) {
				c.outputs[0] = b.Add(b.Add(b.Multiply(sampleX, b.Swizzle(weights, "x")), b.Multiply(sampleY, b.Swizzle(weights, "y"))),
									 b.Multiply(sampleZ, b.Swizzle(weights, "z")));
				return;
			}
			auto unpack = [&](GraphValueId sampled) {
				GraphValueId xy = b.Subtract(b.Multiply(b.Swizzle(sampled, "xy"), b.Scalar(2.0f)), b.Scalar(1.0f));
				GraphValueId z = b.Unary(GraphOp::SquareRoot, b.Unary(GraphOp::Saturate, b.Subtract(b.Scalar(1.0f), b.Dot(xy, xy))));
				const GraphValueId parts[] = {xy, z};
				return b.Construct(GraphValueType::Float3, parts);
			};
			auto whiteout = [&](GraphValueId unpacked, std::string_view axes, std::string_view along) {
				GraphValueId xy = b.Add(b.Swizzle(unpacked, "xy"), b.Swizzle(normal, axes));
				GraphValueId z = b.Multiply(b.Unary(GraphOp::Absolute, b.Swizzle(unpacked, "z")), b.Swizzle(normal, along));
				const GraphValueId parts[] = {xy, z};
				return b.Construct(GraphValueType::Float3, parts);
			};
			GraphValueId normalX = whiteout(unpack(sampleX), "zy", "x");
			GraphValueId normalY = whiteout(unpack(sampleY), "xz", "y");
			GraphValueId normalZ = whiteout(unpack(sampleZ), "xy", "z");
			GraphValueId blended = b.Add(b.Add(b.Multiply(b.Swizzle(normalX, "zyx"), b.Swizzle(weights, "x")),
											   b.Multiply(b.Swizzle(normalY, "xzy"), b.Swizzle(weights, "y"))),
										 b.Multiply(b.Swizzle(normalZ, "xyz"), b.Swizzle(weights, "z")));
			GraphValueId output = GraphSpaces::Transform(c, blended, static_cast<GraphSpace>(space), static_cast<GraphSpace>(c.Setting(2)),
														 GraphConversion::Normal, true);
			const GraphValueId parts[] = {output, b.Scalar(1.0f)};
			c.outputs[0] = b.Construct(GraphValueType::Float4, parts);
		}

		constexpr GraphNodeDefinition UV_DEFINITIONS[] = {
			{"UV/Flipbook", "Flipbook", "UV", "atlas animation", FLIPBOOK_INPUTS, OUT_UV, FLIPBOOK_SETTINGS, FlipbookBody},
			{"UV/ParallaxMapping", "Parallax Mapping", "UV", "offset mapping", PARALLAX_INPUTS, PARALLAX_OUTPUTS, PARALLAX_SETTINGS, ParallaxMappingBody, GraphPreviewMode::None},
			{"UV/ParallaxOcclusionMapping", "Parallax Occlusion Mapping", "UV", "pom", POM_INPUTS, POM_OUTPUTS, POM_SETTINGS,
			 ParallaxOcclusionMappingBody, GraphPreviewMode::None},
			{"UV/PolarCoordinates", "Polar Coordinates", "UV", "", POLAR_INPUTS, OUT_UV, {}, PolarCoordinatesBody},
			{"UV/RadialShear", "Radial Shear", "UV", "", SHEAR_INPUTS, OUT_UV, {}, RadialShearBody},
			{"UV/Rotate", "Rotate", "UV", "", ROTATE_INPUTS, OUT_UV, ROTATE_SETTINGS, RotateBody},
			{"UV/Spherize", "Spherize", "UV", "", SHEAR_INPUTS, OUT_UV, {}, SpherizeBody},
			{"UV/TilingAndOffset", "Tiling And Offset", "UV", "pan scale", TILING_INPUTS, OUT_UV, {}, TilingAndOffsetBody},
			{"UV/Triplanar", "Triplanar", "UV", "project", TRIPLANAR_INPUTS, TRIPLANAR_OUTPUTS, TRIPLANAR_SETTINGS, TriplanarBody, GraphPreviewMode::Preview3D},
			{"UV/Twirl", "Twirl", "UV", "", TWIRL_INPUTS, OUT_UV, {}, TwirlBody},
		};

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphUVDefinitions() { return UV_DEFINITIONS; }

} // namespace Shalltear
