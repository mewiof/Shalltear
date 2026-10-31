#include "GraphCurves.h"
#include "GraphNodeLibrary.h"
#include "GraphSpaces.h"

#include <algorithm>
#include <array>
#include <iterator>

namespace Shalltear {

	namespace {

		using P = GraphPortType;
		using S = GraphSettingKind;
		using B = GraphProgramBuilder;

		constexpr std::string_view UV0 = "UV0";

		constexpr GraphPortDefinition PROPERTY_OUTPUTS[] = {{"Out", P::Any}};
		constexpr GraphSettingDefinition PROPERTY_SETTINGS[] = {{GRAPH_PROPERTY_SETTING, S::Property}};

		void PropertyBody(GraphBodyContext& c) {
			const GraphSettingValue& property = c.settings[0];
			if (property.value != GRAPH_NO_VALUE) {
				c.outputs[0] = property.value;
				return;
			}
			if (property.type == GraphValueType::None) {
				c.error = "reads keyword '" + str(property.text) + "' (only a Keyword node can read it)";
				return;
			}
			c.outputs[0] = GraphTypes::IsTexture(property.type) ? c.builder.Texture(property.type, property.text, property.expression, property.sampler)
																: c.builder.External(property.type, property.text, property.expression);
		}

		constexpr GraphPortDefinition FLOAT_OUT[] = {{"Out", P::Float}};

		constexpr GraphSettingDefinition BOOLEAN_SETTINGS[] = {{"Value", S::Toggle}};
		constexpr GraphPortDefinition BOOLEAN_OUT[] = {{"Out", P::Boolean}};

		void BooleanBody(GraphBodyContext& c) {
			const f32 value = c.Setting(0) != 0 ? 1.0f : 0.0f;
			c.outputs[0] = c.builder.Constant(GraphValueType::Bool, {&value, 1});
		}

		// documents store colors linear
		constexpr std::string_view COLOR_MODES[] = {"Default", "HDR"};
		constexpr GraphSettingDefinition COLOR_SETTINGS[] = {{"Color", S::Color}, {"Mode", S::Choice, COLOR_MODES}};
		constexpr GraphPortDefinition COLOR_OUT[] = {{"Out", P::Float4}};

		void ColorBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Constant(GraphValueType::Float4, c.settings[0].numbers);
		}

		constexpr std::string_view CONSTANTS[] = {"PI", "TAU", "PHI", "E", "SQRT2"};
		constexpr f32 CONSTANT_VALUES[] = {3.1415926f, 6.28318530f, 1.618034f, 2.718282f, 1.414214f};
		constexpr GraphSettingDefinition CONSTANT_SETTINGS[] = {{"Constant", S::Choice, CONSTANTS}};

		void ConstantBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Scalar(CONSTANT_VALUES[c.Setting(0)]); }

		constexpr GraphSettingDefinition INTEGER_SETTINGS[] = {{"Value", S::Number}};

		void IntegerBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Scalar(static_cast<f32>(static_cast<i32>(c.Number(0)))); }

		constexpr GraphSettingDefinition SLIDER_SETTINGS[] = {
			{"Value", S::Number}, {"Min", S::Number}, {"Max", S::Number, {}, 0, {}, {1.0f, 0.0f, 0.0f, 0.0f}}};

		void SliderBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Scalar(c.Number(0)); }

		constexpr GraphPortDefinition FLOAT_INPUTS[] = {{"X", P::Float}};

		void FloatBody(GraphBodyContext& c) { c.outputs[0] = c.In(0); }

		constexpr GraphPortDefinition VECTOR_INPUTS[] = {{"X", P::Float}, {"Y", P::Float}, {"Z", P::Float}, {"W", P::Float}};
		constexpr GraphPortDefinition VECTOR2_OUT[] = {{"Out", P::Float2}};
		constexpr GraphPortDefinition VECTOR3_OUT[] = {{"Out", P::Float3}};
		constexpr GraphPortDefinition VECTOR4_OUT[] = {{"Out", P::Float4}};

		template <u32 Width>
		void VectorBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Construct(GraphTypes::Vector(Width), c.inputs.first(Width));
		}

		constexpr std::string_view UV_CHANNELS[] = {"UV0", "UV1", "UV2", "UV3", "UV4", "UV5", "UV6", "UV7"};
		constexpr GraphSettingDefinition UV_SETTINGS[] = {{"Channel", S::Choice, UV_CHANNELS}};

		void UVBody(GraphBodyContext& c) {
			if (c.Setting(0) != 0) {
				c.error = "reads channel " + str(UV_CHANNELS[c.Setting(0)]) + " (a mesh here has 1 UV set, UV0)";
				return;
			}
			B& b = c.builder;
			const GraphValueId parts[] = {c.Binding(UV0), b.Scalar(0.0f), b.Scalar(0.0f)};
			c.outputs[0] = b.Construct(GraphValueType::Float4, parts);
		}

		void VertexColorBody(GraphBodyContext& c) { c.outputs[0] = c.Binding("VertexColor"); }

		constexpr GraphPortDefinition TIME_OUTPUTS[] = {
			{"Time", P::Float}, {"Sine Time", P::Float}, {"Cosine Time", P::Float}, {"Delta Time", P::Float}, {"Smooth Delta", P::Float}};

		void TimeBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId time = c.Binding("TimeParameters");
			c.outputs[0] = b.Swizzle(time, "x");
			c.outputs[1] = b.Swizzle(time, "y");
			c.outputs[2] = b.Swizzle(time, "z");
			c.outputs[3] = b.Swizzle(time, "w");
			c.outputs[4] = c.Binding("SmoothDeltaTime");
		}

		constexpr std::string_view DIRECTION_SPACES[] = {"Object", "View", "World", "Tangent"};
		constexpr std::string_view POSITION_SPACES[] = {"Object", "View", "World", "Tangent", "AbsoluteWorld"};
		constexpr GraphSettingDefinition DIRECTION_SPACE_SETTINGS[] = {{"Space", S::Choice, DIRECTION_SPACES, 2}};
		constexpr GraphPortDefinition VECTOR3_OUTPUT[] = {{"Out", P::Float3}};

		template <const char* Quantity>
		void GeometryBody(GraphBodyContext& c) {
			c.outputs[0] = c.Binding(str(POSITION_SPACES[c.Setting(0)]) + "Space" + Quantity);
		}

		constexpr std::string_view POSITION_SOURCES[] = {"Default", "Predisplacement"};
		constexpr GraphSettingDefinition POSITION_SETTINGS[] = {{"Space", S::Choice, POSITION_SPACES, 2}, {"Source", S::Choice, POSITION_SOURCES}};

		void PositionBody(GraphBodyContext& c) {
			str name = str(POSITION_SPACES[c.Setting(0)]) + "SpacePosition";
			c.outputs[0] = c.Binding(c.Setting(1) == 1 ? name + "Predisplacement" : name);
		}

		constexpr char NORMAL[] = "Normal";
		constexpr char TANGENT[] = "Tangent";
		constexpr char BITANGENT[] = "BiTangent";
		constexpr char VIEW_DIRECTION[] = "ViewDirection";

		// not normalized (View Direction is)
		void ViewVectorBody(GraphBodyContext& c) {
			B& b = c.builder;
			const GraphSpace space = static_cast<GraphSpace>(c.Setting(0));
			if (space == GraphSpace::View) {
				GraphValueId position = c.Binding("ViewSpacePosition");
				GraphValueId zero = b.Scalar(0.0f);
				const GraphValueId flat[] = {zero, zero, b.Swizzle(position, "z")};
				GraphValueId orthographic = b.Binary(GraphOp::Equal, b.Swizzle(c.Binding("ProjectionParams"), "z"), b.Scalar(1.0f));
				c.outputs[0] = b.Unary(GraphOp::Negate, b.Select(orthographic, b.Construct(GraphValueType::Float3, flat), position));
				return;
			}
			GraphValueId toCamera = b.Subtract(c.Binding("CameraPosition"), c.Binding("WorldSpacePosition"));
			GraphValueId forward = b.Swizzle(b.MatrixRow(c.Binding("View"), 2), "xyz");
			GraphValueId flattened = b.Multiply(forward, b.Dot(toCamera, forward));
			GraphValueId orthographic = b.Binary(GraphOp::Equal, b.Swizzle(c.Binding("ProjectionParams"), "z"), b.Scalar(1.0f));
			GraphValueId world = b.Select(orthographic, flattened, toCamera);
			switch (space) {
			case GraphSpace::Object:
				c.outputs[0] = GraphSpaces::Transform(c, world, GraphSpace::World, GraphSpace::Object, GraphConversion::Direction, false);
				return;
			case GraphSpace::Tangent: {
				GraphValueId direction = GraphSpaces::Transform(c, world, GraphSpace::World, GraphSpace::Tangent, GraphConversion::Normal, true);
				c.outputs[0] = b.Multiply(direction, b.Unary(GraphOp::Length, world));
				return;
			}
			default:
				c.outputs[0] = world;
				return;
			}
		}

		// normalized screen modes are y down
		constexpr std::string_view SCREEN_MODES[] = {"Default", "Raw", "Center", "Tiled", "Pixel", "Clip"};
		constexpr GraphSettingDefinition SCREEN_POSITION_SETTINGS[] = {{"Mode", S::Choice, SCREEN_MODES}};

		void ScreenPositionBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId zero = b.Scalar(0.0f);
			auto pad = [&](GraphValueId xy) {
				const GraphValueId parts[] = {xy, zero, zero};
				return b.Construct(GraphValueType::Float4, parts);
			};
			switch (c.Setting(0)) {
			case 1:
				c.outputs[0] = c.Binding("ScreenPosition");
				return;
			case 2:
				c.outputs[0] = pad(b.Subtract(b.Multiply(c.Binding("NDCPosition"), b.Scalar(2.0f)), b.Scalar(1.0f)));
				return;
			case 3: {
				GraphValueId ndc = c.Binding("NDCPosition");
				GraphValueId screen = c.Binding("ScreenParams");
				GraphValueId centered = b.Subtract(b.Multiply(ndc, b.Scalar(2.0f)), b.Scalar(1.0f));
				GraphValueId x = b.Divide(b.Multiply(b.Swizzle(centered, "x"), b.Swizzle(screen, "x")), b.Swizzle(screen, "y"));
				const GraphValueId parts[] = {x, b.Swizzle(centered, "y"), zero, zero};
				c.outputs[0] = b.Unary(GraphOp::Fraction, b.Construct(GraphValueType::Float4, parts));
				return;
			}
			case 4:
				c.outputs[0] = pad(c.Binding("PixelPosition"));
				return;
			case 5:
				c.outputs[0] = pad(c.Binding("ClipPosition"));
				return;
			default:
				c.outputs[0] = pad(c.Binding("NDCPosition"));
				return;
			}
		}

		void VertexIDBody(GraphBodyContext& c) { c.outputs[0] = c.Binding("VertexID"); }

		constexpr GraphPortDefinition LIGHT_OUTPUTS[] = {
			{"Direction", P::Float3}, {"Color", P::Float3}, {"Attenuation", P::Float}, {"Shadow", P::Float}};
		constexpr GraphPortDefinition SURFACE_OUTPUTS[] = {{"Base Color", P::Float3}, {"Alpha", P::Float}, {"Metallic", P::Float}, {"Roughness", P::Float}, {"Reflectance", P::Float}, {"Occlusion", P::Float}, {"Emission", P::Float3}, {"Normal", P::Float3}};

		void LightBody(GraphBodyContext& c) {
			c.outputs[0] = c.Binding("LightDirection");
			c.outputs[1] = c.Binding("LightColor");
			c.outputs[2] = c.Binding("LightAttenuation");
			c.outputs[3] = c.Binding("LightShadow");
		}

		void SurfaceBody(GraphBodyContext& c) {
			constexpr std::string_view NAMES[] = {"SurfaceBaseColor", "SurfaceAlpha", "SurfaceMetallic", "SurfaceRoughness",
												  "SurfaceReflectance", "SurfaceOcclusion", "SurfaceEmission", "SurfaceNormal"};
			for (u32 i = 0; i < std::size(NAMES); ++i) {
				c.outputs[i] = c.Binding(NAMES[i]);
			}
		}

		void StandardLightingBody(GraphBodyContext& c) { c.outputs[0] = c.Binding("StandardRadiance"); }

		constexpr GraphPortDefinition SKY_OUTPUTS[] = {{"Direction", P::Float3}, {"Sun Direction", P::Float3}, {"Sun Color", P::Float3}, {"Sun Angular Diameter", P::Float}, {"Sun Enabled", P::Boolean}, {"Capturing", P::Boolean}};

		void SkyBody(GraphBodyContext& c) {
			constexpr std::string_view NAMES[] = {"SkyDirection", "SkySunDirection", "SkySunColor",
												  "SkySunAngularDiameter", "SkySunEnabled", "SkyCapturing"};
			for (u32 i = 0; i < std::size(NAMES); ++i) {
				c.outputs[i] = c.Binding(NAMES[i]);
			}
		}

		void SunDiscBody(GraphBodyContext& c) { c.outputs[0] = c.Binding("SkySunDisc"); }

		constexpr std::string_view INTERPOLATOR_BLOCKS[] = {"Block/CustomInterpolator"};
		constexpr GraphSettingDefinition INTERPOLATOR_NODE_SETTINGS[] = {{"Interpolator", S::Block, INTERPOLATOR_BLOCKS, 0, ""},
																		 {"Type", S::Choice, GRAPH_INTERPOLATOR_TYPES, 3}};
		constexpr GraphPortDefinition INTERPOLATOR_OUT[] = {{"Out", P::DynamicVector}};

		void CustomInterpolatorBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Convert(c.settings[0].value, GraphTypes::Vector(c.Setting(1) + 1));
		}
		void InstanceIDBody(GraphBodyContext& c) { c.outputs[0] = c.Binding("InstanceID"); }

		constexpr GraphPortDefinition BLACKBODY_INPUTS[] = {{"Temperature", P::Float, {512.0f, 512.0f, 512.0f, 512.0f}}};

		void BlackbodyBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId temperature = c.In(0);
			GraphValueId falloff = b.Binary(GraphOp::Power, temperature, b.Scalar(-1.5f));
			GraphValueId logarithm = b.Unary(GraphOp::Logarithm, temperature);
			GraphValueId red = b.Add(b.Multiply(b.Scalar(56100000.0f), falloff), b.Scalar(148.0f));
			GraphValueId greenLow = b.Subtract(b.Multiply(b.Scalar(100.04f), logarithm), b.Scalar(623.6f));
			GraphValueId greenHigh = b.Add(b.Multiply(b.Scalar(35200000.0f), falloff), b.Scalar(184.0f));
			GraphValueId green = b.Select(b.Binary(GraphOp::Less, b.Scalar(6500.0f), temperature), greenHigh, greenLow);
			GraphValueId blue = b.Subtract(b.Multiply(b.Scalar(194.18f), logarithm), b.Scalar(1448.6f));
			const GraphValueId parts[] = {red, green, blue};
			GraphValueId color = b.Divide(b.Clamp(b.Construct(GraphValueType::Float3, parts), b.Scalar(0.0f), b.Scalar(255.0f)), b.Scalar(255.0f));
			GraphValueId dim = b.Multiply(color, b.Divide(temperature, b.Scalar(1000.0f)));
			c.outputs[0] = b.Select(b.Binary(GraphOp::Less, temperature, b.Scalar(1000.0f)), dim, color);
		}

		constexpr GraphSettingDefinition GRADIENT_SETTINGS[] = {{"Gradient", S::Gradient}};
		constexpr GraphPortDefinition GRADIENT_OUTPUT[] = {{"Out", P::Gradient}};

		void GradientBody(GraphBodyContext& c) {
			std::span<const f32> stored = c.settings[0].data;
			if (!stored.empty()) {
				c.outputs[0] = c.builder.Constant(GraphValueType::Gradient, stored);
				return;
			}
			std::array<f32, GRAPH_GRADIENT_COMPONENTS> gradient{};
			gradient[1] = 2.0f;
			gradient[2] = 2.0f;
			const f32 colors[] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
			const f32 alphas[] = {1.0f, 0.0f, 1.0f, 1.0f};
			std::copy(std::begin(colors), std::end(colors), gradient.begin() + GRAPH_GRADIENT_COLORS);
			std::copy(std::begin(alphas), std::end(alphas), gradient.begin() + GRAPH_GRADIENT_ALPHAS);
			c.outputs[0] = c.builder.Constant(GraphValueType::Gradient, gradient);
		}

		constexpr GraphPortDefinition SAMPLE_GRADIENT_INPUTS[] = {{"Gradient", P::Gradient}, {"Time", P::Float}};
		constexpr GraphPortDefinition SAMPLE_GRADIENT_OUTPUT[] = {{"Out", P::Float4}};

		void SampleGradientBody(GraphBodyContext& c) {
			std::span<const f32> gradient = c.builder.ConstantOf(c.In(0));
			if (gradient.size() != GRAPH_GRADIENT_COMPONENTS) {
				c.error = "reads a gradient that isn't a constant";
				return;
			}
			c.outputs[0] = GraphGradients::Sample(c.builder, gradient, c.In(1));
		}

		constexpr GraphSettingDefinition MATRIX_ROWS[] = {{"Row 0", S::Vector}, {"Row 1", S::Vector}, {"Row 2", S::Vector}, {"Row 3", S::Vector}};
		constexpr GraphPortDefinition MATRIX2_OUTPUT[] = {{"Out", P::Float2x2}};
		constexpr GraphPortDefinition MATRIX3_OUTPUT[] = {{"Out", P::Float3x3}};
		constexpr GraphPortDefinition MATRIX4_OUTPUT[] = {{"Out", P::Float4x4}};

		template <u32 Dimension>
		void MatrixBody(GraphBodyContext& c) {
			std::array<f32, 16> components{};
			for (u32 row = 0; row < Dimension; ++row) {
				for (u32 column = 0; column < Dimension; ++column) {
					components[row * Dimension + column] = c.settings[row].numbers[column];
				}
			}
			c.outputs[0] = c.builder.Constant(GraphTypes::Matrix(Dimension), {components.data(), Dimension * Dimension});
		}

		constexpr std::string_view TRANSFORMATION_MATRICES[] = {"Model", "InverseModel", "View", "InverseView",
																"Projection", "InverseProjection", "ViewProjection", "InverseViewProjection"};
		constexpr std::string_view TRANSFORMATION_BINDINGS[] = {"ObjectToWorld", "WorldToObject", "View", "InverseView",
																"Projection", "InverseProjection", "ViewProjection", "InverseViewProjection"};
		constexpr GraphSettingDefinition TRANSFORMATION_SETTINGS[] = {{"Matrix", S::Choice, TRANSFORMATION_MATRICES}};

		void TransformationMatrixBody(GraphBodyContext& c) { c.outputs[0] = c.Binding(TRANSFORMATION_BINDINGS[c.Setting(0)]); }

		constexpr GraphPortDefinition CAMERA_OUTPUTS[] = {{"Position", P::Float3}, {"Direction", P::Float3}, {"Orthographic", P::Float}, {"Near Plane", P::Float}, {"Far Plane", P::Float}, {"Z Buffer Sign", P::Float}, {"Width", P::Float}, {"Height", P::Float}};

		void CameraBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId projection = c.Binding("ProjectionParams");
			GraphValueId screen = c.Binding("ScreenParams");
			c.outputs[0] = c.Binding("CameraPosition");
			c.outputs[1] = b.Swizzle(b.MatrixRow(c.Binding("View"), 2), "xyz");
			c.outputs[2] = b.Swizzle(projection, "z");
			c.outputs[3] = b.Swizzle(projection, "x");
			c.outputs[4] = b.Swizzle(projection, "y");
			c.outputs[5] = b.Scalar(-1.0f);
			c.outputs[6] = b.Swizzle(screen, "z");
			c.outputs[7] = b.Swizzle(screen, "w");
		}

		constexpr GraphPortDefinition OBJECT_OUTPUTS[] = {
			{"Position", P::Float3}, {"Scale", P::Float3}, {"World Bounds Min", P::Float3}, {"World Bounds Max", P::Float3}, {"Bounds Size", P::Float3}};

		void ObjectBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId model = c.Binding("ObjectToWorld");
			const GraphValueId translation[] = {b.Swizzle(b.MatrixRow(model, 0), "w"), b.Swizzle(b.MatrixRow(model, 1), "w"),
												b.Swizzle(b.MatrixRow(model, 2), "w")};
			c.outputs[0] = b.Construct(GraphValueType::Float3, translation);
			GraphValueId columns = b.Unary(GraphOp::Transpose, model);
			const GraphValueId scale[] = {b.Unary(GraphOp::Length, b.Swizzle(b.MatrixRow(columns, 0), "xyz")),
										  b.Unary(GraphOp::Length, b.Swizzle(b.MatrixRow(columns, 1), "xyz")),
										  b.Unary(GraphOp::Length, b.Swizzle(b.MatrixRow(columns, 2), "xyz"))};
			c.outputs[1] = b.Construct(GraphValueType::Float3, scale);
			GraphValueId minimum = c.Binding("RendererBoundsMin");
			GraphValueId maximum = c.Binding("RendererBoundsMax");
			c.outputs[2] = minimum;
			c.outputs[3] = maximum;
			c.outputs[4] = b.Subtract(maximum, minimum);
		}

		constexpr GraphPortDefinition SCREEN_OUTPUTS[] = {{"Width", P::Float}, {"Height", P::Float}};

		void ScreenBody(GraphBodyContext& c) {
			GraphValueId screen = c.Binding("ScreenParams");
			c.outputs[0] = c.builder.Swizzle(screen, "x");
			c.outputs[1] = c.builder.Swizzle(screen, "y");
		}

		constexpr GraphPortDefinition FOG_INPUTS[] = {{"Position", P::Float3, {}, "ObjectSpacePosition"}};
		constexpr GraphPortDefinition FOG_OUTPUTS[] = {{"Color", P::Float4}, {"Density", P::Float}};

		void FogBody(GraphBodyContext& c) {
			const f32 none[] = {0.0f, 0.0f, 0.0f, 0.0f};
			c.outputs[0] = c.builder.Constant(GraphValueType::Float4, none);
			c.outputs[1] = c.builder.Scalar(0.0f);
		}

		void EyeIndexBody(GraphBodyContext& c) { c.outputs[0] = c.Binding("EyeIndex"); }

		constexpr GraphPortDefinition SCENE_UV_INPUT[] = {{"UV", P::Float4, {}, "ScreenPosition"}};

		GraphValueId SampleScene(GraphBodyContext& c, std::string_view image) {
			B& b = c.builder;
			const GraphValueId operands[] = {c.Binding(image), b.Swizzle(c.In(0), "xy"), b.Scalar(0.0f)};
			return b.TextureOp(GraphOp::SampleTextureLevel, operands);
		}

		void SceneColorBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Swizzle(SampleScene(c, "SceneColor"), "xyz"); }

		// raw = reversed z, 1 at the near plane
		constexpr std::string_view SCENE_DEPTH_MODES[] = {"Linear01", "Raw", "Eye"};
		constexpr GraphSettingDefinition SCENE_DEPTH_SETTINGS[] = {{"Sampling Mode", S::Choice, SCENE_DEPTH_MODES}};

		void SceneDepthBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId raw = b.Swizzle(SampleScene(c, "SceneDepth"), "x");
			if (c.Setting(0) == 1) {
				c.outputs[0] = raw;
				return;
			}
			const GraphValueId clip[] = {b.Scalar(0.0f), b.Scalar(0.0f), raw, b.Scalar(1.0f)};
			GraphValueId view = b.Binary(GraphOp::MatrixMultiply, c.Binding("InverseProjection"), b.Construct(GraphValueType::Float4, clip));
			GraphValueId eye = b.Divide(b.Swizzle(view, "z"), b.Swizzle(view, "w"));
			c.outputs[0] = c.Setting(0) == 2 ? eye : b.Divide(eye, b.Swizzle(c.Binding("ProjectionParams"), "y"));
		}

		constexpr std::string_view SAMPLE_BUFFERS[] = {"NormalWorldSpace", "MotionVectors", "BlitSource"};
		constexpr GraphSettingDefinition SAMPLE_BUFFER_SETTINGS[] = {{"Source Buffer", S::Choice, SAMPLE_BUFFERS}};
		constexpr GraphPortDefinition SAMPLE_BUFFER_OUTPUT[] = {{"Output", P::DynamicVector}};

		void SampleBufferBody(GraphBodyContext& c) {
			B& b = c.builder;
			switch (c.Setting(0)) {
			case 1:
				c.outputs[0] = b.Swizzle(SampleScene(c, "MotionVectors"), "xy");
				return;
			case 2:
				c.outputs[0] = SampleScene(c, "SceneColor");
				return;
			default: {
				GraphValueId encoded = b.Swizzle(SampleScene(c, "SceneNormals"), "xyz");
				c.outputs[0] = b.Unary(GraphOp::Normalize, b.Subtract(b.Multiply(encoded, b.Scalar(2.0f)), b.Scalar(1.0f)));
				return;
			}
			}
		}

		constexpr std::string_view DIELECTRICS[] = {"Common", "RustedMetal", "Water", "Ice", "Glass", "Custom"};
		constexpr GraphSettingDefinition DIELECTRIC_SETTINGS[] = {{"Material", S::Choice, DIELECTRICS},
																  {"Range", S::Number, {}, 0, {}, {0.5f, 0.0f, 0.0f, 0.0f}},
																  {"IOR", S::Number, {}, 0, {}, {1.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr f32 DIELECTRIC_VALUES[] = {0.0f, 0.030f, 0.020f, 0.018f, 0.040f};

		void DielectricSpecularBody(GraphBodyContext& c) {
			B& b = c.builder;
			switch (c.Setting(0)) {
			case 0:
				c.outputs[0] = b.Lerp(b.Scalar(0.034f), b.Scalar(0.048f), b.Scalar(c.Number(1)));
				return;
			case 5: {
				GraphValueId ior = b.Scalar(c.Number(2));
				GraphValueId two = b.Scalar(2.0f);
				GraphValueId above = b.Binary(GraphOp::Power, b.Subtract(ior, b.Scalar(1.0f)), two);
				GraphValueId below = b.Binary(GraphOp::Power, b.Add(ior, b.Scalar(1.0f)), two);
				c.outputs[0] = b.Divide(above, below);
				return;
			}
			default:
				c.outputs[0] = b.Scalar(DIELECTRIC_VALUES[c.Setting(0)]);
				return;
			}
		}

		constexpr std::string_view METALS[] = {"Iron", "Silver", "Aluminium", "Gold", "Copper", "Chromium", "Nickel", "Titanium", "Cobalt", "Platinum"};
		constexpr f32 METAL_VALUES[][3] = {{0.560f, 0.570f, 0.580f}, {0.972f, 0.960f, 0.915f}, {0.913f, 0.921f, 0.925f}, {1.000f, 0.766f, 0.336f}, {0.955f, 0.637f, 0.538f}, {0.550f, 0.556f, 0.554f}, {0.660f, 0.609f, 0.526f}, {0.542f, 0.497f, 0.449f}, {0.662f, 0.655f, 0.634f}, {0.672f, 0.637f, 0.585f}};
		constexpr GraphSettingDefinition METAL_SETTINGS[] = {{"Material", S::Choice, METALS}};

		void MetalReflectanceBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Constant(GraphValueType::Float3, METAL_VALUES[c.Setting(0)]); }

		constexpr std::string_view TEXTURE_TYPES[] = {"Default", "Normal"};
		constexpr std::string_view NORMAL_SPACES[] = {"Tangent", "Object"};
		constexpr GraphSettingDefinition SAMPLE_SETTINGS[] = {{"Type", S::Choice, TEXTURE_TYPES}, {"Space", S::Choice, NORMAL_SPACES}};
		constexpr GraphPortDefinition SAMPLE_INPUTS[] = {{"Texture", P::Texture2D}, {"UV", P::Float2, {}, UV0}};
		constexpr std::string_view MIP_SAMPLING_MODES[] = {"Standard", "LOD", "Gradient", "Bias"};
		constexpr u8 MIP_SETTING = 2;
		constexpr GraphSettingDefinition SAMPLE_2D_SETTINGS[] = {
			{"Type", S::Choice, TEXTURE_TYPES}, {"Space", S::Choice, NORMAL_SPACES}, {"Mip Sampling Mode", S::Choice, MIP_SAMPLING_MODES}};
		constexpr GraphPortDefinition SAMPLE_2D_INPUTS[] = {
			{"Texture", P::Texture2D},
			{"UV", P::Float2, {}, UV0},
			{"LOD", P::Float, {}, {}, MIP_SETTING, 1u << 1},
			{"DDX", P::Float2, {}, {}, MIP_SETTING, 1u << 2},
			{"DDY", P::Float2, {}, {}, MIP_SETTING, 1u << 2},
			{"Bias", P::Float, {}, {}, MIP_SETTING, 1u << 3},
		};
		constexpr GraphPortDefinition SAMPLE_LOD_INPUTS[] = {{"Texture", P::Texture2D}, {"UV", P::Float2, {}, UV0}, {"LOD", P::Float}};
		constexpr GraphPortDefinition SAMPLE_OUTPUTS[] = {{"RGBA", P::Float4}, {"R", P::Float}, {"G", P::Float}, {"B", P::Float}, {"A", P::Float}};

		// must match ShalltearUnpackNormalMap
		void SampleOutputs(GraphBodyContext& c, GraphValueId sample) {
			B& b = c.builder;
			GraphValueId rgba = sample;
			if (c.Setting(0) == 1) {
				GraphValueId alpha = b.Swizzle(sample, "w");
				GraphValueId rgb = GRAPH_NO_VALUE;
				if (c.Setting(1) == 0) {
					GraphValueId xy = b.Subtract(b.Multiply(b.Swizzle(sample, "xy"), b.Scalar(2.0f)), b.Scalar(1.0f));
					GraphValueId z = b.Unary(GraphOp::SquareRoot, b.Unary(GraphOp::Saturate, b.Subtract(b.Scalar(1.0f), b.Dot(xy, xy))));
					const GraphValueId normal[] = {xy, z};
					rgb = b.Construct(GraphValueType::Float3, normal);
				} else {
					rgb = b.Subtract(b.Multiply(b.Swizzle(sample, "xyz"), b.Scalar(2.0f)), b.Scalar(1.0f));
				}
				const GraphValueId parts[] = {rgb, alpha};
				rgba = b.Construct(GraphValueType::Float4, parts);
			}
			c.outputs[0] = rgba;
			c.outputs[1] = b.Swizzle(rgba, "x");
			c.outputs[2] = b.Swizzle(rgba, "y");
			c.outputs[3] = b.Swizzle(rgba, "z");
			c.outputs[4] = b.Swizzle(rgba, "w");
		}

		void SampleTexture2DBody(GraphBodyContext& c) {
			switch (c.Setting(MIP_SETTING)) {
			case 1: {
				const GraphValueId operands[] = {c.In(0), c.In(1), c.In(2)};
				SampleOutputs(c, c.builder.TextureOp(GraphOp::SampleTextureLevel, operands));
				return;
			}
			case 2: {
				const GraphValueId operands[] = {c.In(0), c.In(1), c.In(3), c.In(4)};
				SampleOutputs(c, c.builder.TextureOp(GraphOp::SampleTextureGrad, operands));
				return;
			}
			case 3: {
				const GraphValueId operands[] = {c.In(0), c.In(1), c.In(5)};
				SampleOutputs(c, c.builder.TextureOp(GraphOp::SampleTextureBias, operands));
				return;
			}
			default: {
				const GraphValueId operands[] = {c.In(0), c.In(1)};
				SampleOutputs(c, c.builder.TextureOp(GraphOp::SampleTexture, operands));
				return;
			}
			}
		}

		void SampleTexture2DLODBody(GraphBodyContext& c) {
			const GraphValueId operands[] = {c.In(0), c.In(1), c.In(2)};
			SampleOutputs(c, c.builder.TextureOp(GraphOp::SampleTextureLevel, operands));
		}

		void ChannelOutputs(GraphBodyContext& c, GraphValueId sample) {
			B& b = c.builder;
			c.outputs[0] = sample;
			c.outputs[1] = b.Swizzle(sample, "x");
			c.outputs[2] = b.Swizzle(sample, "y");
			c.outputs[3] = b.Swizzle(sample, "z");
			c.outputs[4] = b.Swizzle(sample, "w");
		}

		constexpr u8 ARRAY_MIP_SETTING = 0;
		constexpr GraphSettingDefinition SAMPLE_ARRAY_SETTINGS[] = {{"Mip Sampling Mode", S::Choice, MIP_SAMPLING_MODES}};
		constexpr GraphPortDefinition SAMPLE_ARRAY_INPUTS[] = {
			{"Texture Array", P::Texture2DArray},
			{"Index", P::Float},
			{"UV", P::Float2, {}, UV0},
			{"LOD", P::Float, {}, {}, ARRAY_MIP_SETTING, 1u << 1},
			{"DDX", P::Float2, {}, {}, ARRAY_MIP_SETTING, 1u << 2},
			{"DDY", P::Float2, {}, {}, ARRAY_MIP_SETTING, 1u << 2},
			{"Bias", P::Float, {}, {}, ARRAY_MIP_SETTING, 1u << 3},
		};

		void SampleTexture2DArrayBody(GraphBodyContext& c) {
			B& b = c.builder;
			const GraphValueId place[] = {c.In(2), c.In(1)};
			const GraphValueId coordinate = b.Construct(GraphValueType::Float3, place);
			switch (c.Setting(ARRAY_MIP_SETTING)) {
			case 1: {
				const GraphValueId operands[] = {c.In(0), coordinate, c.In(3)};
				ChannelOutputs(c, b.TextureOp(GraphOp::SampleTextureLevel, operands));
				return;
			}
			case 2: {
				const GraphValueId operands[] = {c.In(0), coordinate, c.In(4), c.In(5)};
				ChannelOutputs(c, b.TextureOp(GraphOp::SampleTextureGrad, operands));
				return;
			}
			case 3: {
				const GraphValueId operands[] = {c.In(0), coordinate, c.In(6)};
				ChannelOutputs(c, b.TextureOp(GraphOp::SampleTextureBias, operands));
				return;
			}
			default: {
				const GraphValueId operands[] = {c.In(0), coordinate};
				ChannelOutputs(c, b.TextureOp(GraphOp::SampleTexture, operands));
				return;
			}
			}
		}

		constexpr std::string_view MIP_3D_MODES[] = {"Standard", "LOD"};
		constexpr GraphSettingDefinition SAMPLE_3D_SETTINGS[] = {{"Mip Sampling Mode", S::Choice, MIP_3D_MODES}};
		constexpr GraphPortDefinition SAMPLE_3D_INPUTS[] = {
			{"Texture", P::Texture3D},
			{"UV", P::Float3},
			{"LOD", P::Float, {}, {}, 0, 1u << 1},
		};

		void SampleTexture3DBody(GraphBodyContext& c) {
			if (c.Setting(0) == 1) {
				const GraphValueId operands[] = {c.In(0), c.In(1), c.In(2)};
				ChannelOutputs(c, c.builder.TextureOp(GraphOp::SampleTextureLevel, operands));
				return;
			}
			const GraphValueId operands[] = {c.In(0), c.In(1)};
			ChannelOutputs(c, c.builder.TextureOp(GraphOp::SampleTexture, operands));
		}

		constexpr GraphPortDefinition SAMPLE_CUBE_INPUTS[] = {{"Cube", P::Cubemap}, {"Dir", P::Float3, {}, "WorldSpaceNormal"}, {"LOD", P::Float}};
		constexpr GraphPortDefinition CUBE_OUTPUT[] = {{"Out", P::Float4}};

		void SampleCubemapBody(GraphBodyContext& c) {
			const GraphValueId operands[] = {c.In(0), c.In(1), c.In(2)};
			c.outputs[0] = c.builder.TextureOp(GraphOp::SampleTextureLevel, operands);
		}

		constexpr GraphPortDefinition SAMPLE_REFLECTED_INPUTS[] = {{"Cube", P::Cubemap},
																   {"ViewDir", P::Float3, {}, "ObjectSpaceViewDirection"},
																   {"Normal", P::Float3, {}, "ObjectSpaceNormal"},
																   {"LOD", P::Float}};

		void SampleReflectedCubemapBody(GraphBodyContext& c) {
			B& b = c.builder;
			const GraphValueId direction = b.Binary(GraphOp::Reflect, b.Unary(GraphOp::Negate, c.In(1)), c.In(2));
			const GraphValueId operands[] = {c.In(0), direction, c.In(3)};
			c.outputs[0] = b.TextureOp(GraphOp::SampleTextureLevel, operands);
		}

		constexpr GraphPortDefinition TEXTURE_INPUT[] = {{"Texture", P::Texture2D}};
		constexpr GraphPortDefinition TEXTURE_SIZE_OUTPUTS[] = {
			{"Width", P::Float}, {"Height", P::Float}, {"Texel Width", P::Float}, {"Texel Height", P::Float}};

		void TextureSizeBody(GraphBodyContext& c) {
			B& b = c.builder;
			const GraphValueId operands[] = {c.In(0)};
			GraphValueId size = b.TextureOp(GraphOp::TextureSize, operands);
			c.outputs[0] = b.Swizzle(size, "x");
			c.outputs[1] = b.Swizzle(size, "y");
			c.outputs[2] = b.Swizzle(size, "z");
			c.outputs[3] = b.Swizzle(size, "w");
		}

		constexpr GraphSettingDefinition LOD_SETTINGS[] = {{"Clamp", S::Toggle, {}, 1}};
		constexpr GraphPortDefinition LOD_OUTPUTS[] = {{"LOD", P::Float}};

		void CalculateLevelOfDetailBody(GraphBodyContext& c) {
			const GraphValueId operands[] = {c.In(0), c.In(1)};
			c.outputs[0] = c.builder.TextureOp(GraphOp::TextureLevelOfDetail, operands, c.Setting(0));
		}

		constexpr GraphNodeDefinition INPUT_DEFINITIONS[] = {
			{GRAPH_PROPERTY_NODE, "Property", "Input", "", {}, PROPERTY_OUTPUTS, PROPERTY_SETTINGS, PropertyBody, GraphPreviewMode::None},

			{"Input/Boolean", "Boolean", "Input/Basic", "toggle switch", {}, BOOLEAN_OUT, BOOLEAN_SETTINGS, BooleanBody, GraphPreviewMode::None},
			{"Input/Color", "Color", "Input/Basic", "rgba", {}, COLOR_OUT, COLOR_SETTINGS, ColorBody, GraphPreviewMode::None},
			{"Input/Constant", "Constant", "Input/Basic", "pi tau phi", {}, FLOAT_OUT, CONSTANT_SETTINGS, ConstantBody, GraphPreviewMode::None},
			{"Input/Integer", "Integer", "Input/Basic", "int whole number", {}, FLOAT_OUT, INTEGER_SETTINGS, IntegerBody, GraphPreviewMode::None},
			{"Input/Float", "Float", "Input/Basic", "Vector 1 scalar value", FLOAT_INPUTS, FLOAT_OUT, {}, FloatBody, GraphPreviewMode::None},
			{"Input/Slider", "Slider", "Input/Basic", "range", {}, FLOAT_OUT, SLIDER_SETTINGS, SliderBody, GraphPreviewMode::None},
			{"Input/Vector2", "Vector 2", "Input/Basic", "float2", std::span(VECTOR_INPUTS).first(2), VECTOR2_OUT, {}, VectorBody<2>, GraphPreviewMode::None},
			{"Input/Vector3", "Vector 3", "Input/Basic", "float3", std::span(VECTOR_INPUTS).first(3), VECTOR3_OUT, {}, VectorBody<3>, GraphPreviewMode::None},
			{"Input/Vector4", "Vector 4", "Input/Basic", "float4", VECTOR_INPUTS, VECTOR4_OUT, {}, VectorBody<4>, GraphPreviewMode::None},

			{"Input/Time", "Time", "Input/Basic", "", {}, TIME_OUTPUTS, {}, TimeBody, GraphPreviewMode::None},

			{"Input/UV", "UV", "Input/Geometry", "texcoords", {}, COLOR_OUT, UV_SETTINGS, UVBody},
			{"Input/VertexColor", "Vertex Color", "Input/Geometry", "", {}, COLOR_OUT, {}, VertexColorBody, GraphPreviewMode::Preview3D},
			{"Input/Position", "Position", "Input/Geometry", "location", {}, VECTOR3_OUTPUT, POSITION_SETTINGS, PositionBody, GraphPreviewMode::Preview3D},
			{"Input/NormalVector", "Normal Vector", "Input/Geometry", "surface direction", {}, VECTOR3_OUTPUT, DIRECTION_SPACE_SETTINGS, GeometryBody<NORMAL>, GraphPreviewMode::Preview3D},
			{"Input/TangentVector", "Tangent Vector", "Input/Geometry", "", {}, VECTOR3_OUTPUT, DIRECTION_SPACE_SETTINGS, GeometryBody<TANGENT>, GraphPreviewMode::Preview3D},
			{"Input/BitangentVector", "Bitangent Vector", "Input/Geometry", "binormal", {}, VECTOR3_OUTPUT, DIRECTION_SPACE_SETTINGS, GeometryBody<BITANGENT>, GraphPreviewMode::Preview3D},
			{"Input/ViewDirection", "View Direction", "Input/Geometry", "eye direction", {}, VECTOR3_OUTPUT, DIRECTION_SPACE_SETTINGS, GeometryBody<VIEW_DIRECTION>, GraphPreviewMode::Preview3D},
			{"Input/ViewVector", "View Vector", "Input/Geometry", "eye vector", {}, VECTOR3_OUTPUT, DIRECTION_SPACE_SETTINGS, ViewVectorBody, GraphPreviewMode::Preview3D},
			{"Input/ScreenPosition", "Screen Position", "Input/Geometry", "", {}, COLOR_OUT, SCREEN_POSITION_SETTINGS, ScreenPositionBody},
			{"Input/VertexID", "Vertex ID", "Input/Geometry", "", {}, FLOAT_OUT, {}, VertexIDBody, GraphPreviewMode::None},
			{"Input/InstanceID", "Instance ID", "Input/Geometry", "", {}, FLOAT_OUT, {}, InstanceIDBody, GraphPreviewMode::None},

			{"Input/Light", "Light", "Input/Lighting", "main light direction color attenuation shadow", {}, LIGHT_OUTPUTS, {}, LightBody, GraphPreviewMode::None},
			{"Input/Surface", "Surface", "Input/Lighting", "base color albedo normal", {}, SURFACE_OUTPUTS, {}, SurfaceBody, GraphPreviewMode::None},
			{"Input/StandardLighting", "Standard Lighting", "Input/Lighting", "brdf pbr lit radiance", {}, VECTOR3_OUTPUT, {}, StandardLightingBody, GraphPreviewMode::Preview3D},

			{"Input/Sky", "Sky", "Input/Sky", "main light direction sun eye ray view direction capturing", {}, SKY_OUTPUTS, {}, SkyBody, GraphPreviewMode::Preview3D},
			{"Input/SunDisc", "Sun Disc", "Input/Sky", "sun disk", {}, VECTOR3_OUTPUT, {}, SunDiscBody, GraphPreviewMode::Preview3D},

			{"CustomInterpolators/Instance", "Custom Interpolator", "Custom Interpolators", "", {}, INTERPOLATOR_OUT, INTERPOLATOR_NODE_SETTINGS, CustomInterpolatorBody},

			{"Input/Blackbody", "Blackbody", "Input/Gradient", "", BLACKBODY_INPUTS, VECTOR3_OUTPUT, {}, BlackbodyBody},
			{"Input/Gradient", "Gradient", "Input/Gradient", "ramp", {}, GRADIENT_OUTPUT, GRADIENT_SETTINGS, GradientBody, GraphPreviewMode::None},
			{"Input/SampleGradient", "Sample Gradient", "Input/Gradient", "", SAMPLE_GRADIENT_INPUTS, SAMPLE_GRADIENT_OUTPUT, {}, SampleGradientBody},

			{"Input/Matrix2x2", "Matrix 2x2", "Input/Matrix", "", {}, MATRIX2_OUTPUT, std::span(MATRIX_ROWS).first(2), MatrixBody<2>, GraphPreviewMode::None},
			{"Input/Matrix3x3", "Matrix 3x3", "Input/Matrix", "", {}, MATRIX3_OUTPUT, std::span(MATRIX_ROWS).first(3), MatrixBody<3>, GraphPreviewMode::None},
			{"Input/Matrix4x4", "Matrix 4x4", "Input/Matrix", "", {}, MATRIX4_OUTPUT, MATRIX_ROWS, MatrixBody<4>, GraphPreviewMode::None},
			{"Input/TransformationMatrix", "Transformation Matrix", "Input/Matrix", "", {}, MATRIX4_OUTPUT, TRANSFORMATION_SETTINGS, TransformationMatrixBody, GraphPreviewMode::None},

			{"Input/Camera", "Camera", "Input/Scene", "position direction orthographic near plane far plane width height", {}, CAMERA_OUTPUTS, {}, CameraBody, GraphPreviewMode::None},
			{"Input/Fog", "Fog", "Input/Scene", "", FOG_INPUTS, FOG_OUTPUTS, {}, FogBody, GraphPreviewMode::None},
			{"Input/Object", "Object", "Input/Scene", "position scale bounds size", {}, OBJECT_OUTPUTS, {}, ObjectBody, GraphPreviewMode::None},
			{"Input/Screen", "Screen", "Input/Scene", "", {}, SCREEN_OUTPUTS, {}, ScreenBody, GraphPreviewMode::None},
			{"Input/EyeIndex", "Eye Index", "Input/Scene", "stereo 3d", {}, FLOAT_OUT, {}, EyeIndexBody, GraphPreviewMode::None},
			{"Input/SceneColor", "Scene Color", "Input/Scene", "screen buffer", SCENE_UV_INPUT, VECTOR3_OUTPUT, {}, SceneColorBody, GraphPreviewMode::None},
			{"Input/SceneDepth", "Scene Depth", "Input/Scene", "zbuffer zdepth", SCENE_UV_INPUT, FLOAT_OUT, SCENE_DEPTH_SETTINGS, SceneDepthBody,
			 GraphPreviewMode::None},
			{"Input/SampleBuffer", "Sample Buffer", "Input/Scene", "sample buffer normal motion vector blit", SCENE_UV_INPUT, SAMPLE_BUFFER_OUTPUT,
			 SAMPLE_BUFFER_SETTINGS, SampleBufferBody, GraphPreviewMode::Preview2D},

			{"Input/DielectricSpecular", "Dielectric Specular", "Input/PBR", "", {}, FLOAT_OUT, DIELECTRIC_SETTINGS, DielectricSpecularBody},
			{"Input/MetalReflectance", "Metal Reflectance", "Input/PBR", "", {}, VECTOR3_OUT, METAL_SETTINGS, MetalReflectanceBody},

			{"Input/SampleTexture2D", "Sample Texture 2D", "Input/Texture", "tex2d", SAMPLE_2D_INPUTS, SAMPLE_OUTPUTS, SAMPLE_2D_SETTINGS, SampleTexture2DBody},
			{"Input/SampleTexture2DLOD", "Sample Texture 2D LOD", "Input/Texture", "tex2dlod", SAMPLE_LOD_INPUTS, SAMPLE_OUTPUTS, SAMPLE_SETTINGS,
			 SampleTexture2DLODBody},
			{"Input/TextureSize", "Texture Size", "Input/Texture", "texel size", TEXTURE_INPUT, TEXTURE_SIZE_OUTPUTS, {}, TextureSizeBody, GraphPreviewMode::None},
			{"Input/CalculateLevelOfDetailTexture2D", "Calculate Level Of Detail Texture 2D", "Input/Texture", "mip lod", SAMPLE_INPUTS,
			 LOD_OUTPUTS, LOD_SETTINGS, CalculateLevelOfDetailBody},
			{"Input/SampleTexture2DArray", "Sample Texture 2D Array", "Input/Texture", "tex2darray texture array", SAMPLE_ARRAY_INPUTS, SAMPLE_OUTPUTS,
			 SAMPLE_ARRAY_SETTINGS, SampleTexture2DArrayBody},
			{"Input/SampleTexture3D", "Sample Texture 3D", "Input/Texture", "tex3d volume", SAMPLE_3D_INPUTS, SAMPLE_OUTPUTS, SAMPLE_3D_SETTINGS,
			 SampleTexture3DBody},
			{"Input/SampleCubemap", "Sample Cubemap", "Input/Texture", "cube texcube", SAMPLE_CUBE_INPUTS, CUBE_OUTPUT, {}, SampleCubemapBody},
			{"Input/SampleReflectedCubemap", "Sample Reflected Cubemap", "Input/Texture", "cube reflection environment", SAMPLE_REFLECTED_INPUTS, CUBE_OUTPUT, {}, SampleReflectedCubemapBody},
		};

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphInputDefinitions() { return INPUT_DEFINITIONS; }

} // namespace Shalltear
