#include "GraphCurves.h"
#include "GraphCustomHLSL.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphEffectBlocks.h"
#include "GraphNodeLibrary.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <format>

namespace Shalltear {

	namespace {

		using P = GraphPortType;
		using S = GraphSettingKind;
		using T = GraphValueType;

		constexpr GraphEffectAttributeInfo ATTRIBUTES[] = {
			{"position", T::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, false, GraphPortSpace::Position},
			{"velocity", T::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, false, GraphPortSpace::Vector},
			{"age", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}},
			{"lifetime", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}},
			{"alive", T::Bool, {1.0f, 0.0f, 0.0f, 0.0f}},
			{"size", T::Float, {0.1f, 0.0f, 0.0f, 0.0f}},
			{"mass", T::Float, {1.0f, 0.0f, 0.0f, 0.0f}},
			{"direction", T::Float3, {0.0f, 0.0f, 1.0f, 0.0f}, false, GraphPortSpace::Vector},
			{"angle", T::Float3, {0.0f, 0.0f, 0.0f, 0.0f}},
			{"angularVelocity", T::Float3, {0.0f, 0.0f, 0.0f, 0.0f}},
			{"oldPosition", T::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, false, GraphPortSpace::Position},
			{"targetPosition", T::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, false, GraphPortSpace::Position},
			{"color", T::Float3, {1.0f, 1.0f, 1.0f, 0.0f}},
			{"alpha", T::Float, {1.0f, 0.0f, 0.0f, 0.0f}},
			{"scale", T::Float3, {1.0f, 1.0f, 1.0f, 0.0f}},
			{"pivot", T::Float3, {0.0f, 0.0f, 0.0f, 0.0f}},
			{"texIndex", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}},
			{"texIndexBlend", T::Float, {1.0f, 0.0f, 0.0f, 0.0f}},
			{"axisX", T::Float3, {1.0f, 0.0f, 0.0f, 0.0f}, false, GraphPortSpace::Vector},
			{"axisY", T::Float3, {0.0f, 1.0f, 0.0f, 0.0f}, false, GraphPortSpace::Vector},
			{"axisZ", T::Float3, {0.0f, 0.0f, 1.0f, 0.0f}, false, GraphPortSpace::Vector},
			// uints read as floats (exact below 2^24)
			{"oldVelocity", T::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, true},
			{"particleId", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}, true},
			{"spawnIndex", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}, true},
			{"spawnCount", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}, true},
			{"collisionEventCount", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}, true},
			{"collisionEventNormal", T::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, true, GraphPortSpace::Direction},
			{"collisionEventPosition", T::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, true, GraphPortSpace::Position},
			{"hasCollisionEvent", T::Bool, {0.0f, 0.0f, 0.0f, 0.0f}, true},
			{"stripIndex", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}, true},
			{"particleIndexInStrip", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}, true},
			{"particleCountInStrip", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}, true},
			{"spawnIndexInStrip", T::Float, {0.0f, 0.0f, 0.0f, 0.0f}, true},
		};

		constexpr size_t WRITABLE_COUNT = [] {
			size_t count = 0;
			while (count < std::size(ATTRIBUTES) && !ATTRIBUTES[count].readOnly) {
				++count;
			}
			return count;
		}();

		constexpr std::array<std::string_view, std::size(ATTRIBUTES)> ATTRIBUTE_NAMES = [] {
			std::array<std::string_view, std::size(ATTRIBUTES)> names{};
			for (size_t i = 0; i < std::size(ATTRIBUTES); ++i) {
				names[i] = ATTRIBUTES[i].name;
			}
			return names;
		}();
		constexpr std::span<const std::string_view> WRITABLE_NAMES{ATTRIBUTE_NAMES.data(), WRITABLE_COUNT};

		P PortTypeOf(T type) {
			switch (type) {
			case T::Bool:
				return P::Boolean;
			case T::Float3:
				return P::Float3;
			default:
				return P::Float;
			}
		}

		str Capitalized(std::string_view name) {
			str out(name);
			if (!out.empty()) {
				out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
			}
			return out;
		}

		GraphValueId RandomBetween(GraphBodyContext& c, GraphValueId a, GraphValueId b, bool perComponent, std::string_view stream,
								   GraphValueId seed, u32 salt) {
			GraphProgramBuilder& builder = c.builder;
			const GraphValueId key = c.Binding(stream);
			const GraphValueType type = builder.TypeOf(a);
			const u32 width = GraphTypes::VectorWidth(type);
			std::array<GraphValueId, 4> draws{};
			for (u32 i = 0; i < width; ++i) {
				draws[i] = builder.Random(key, seed, perComponent ? salt + i : salt);
			}
			const GraphValueId t = width <= 1 || !perComponent ? draws[0] : builder.Construct(type, {draws.data(), width});
			return builder.Lerp(a, b, t);
		}

		u32 NodeSalt(const GraphBodyContext& c) { return c.builder.Identity() * 8u; }

		constexpr GraphSettingDefinition SET_ATTRIBUTE_SETTINGS[] = {
			{GRAPH_EFFECT_ATTRIBUTE_SETTING, S::Choice, WRITABLE_NAMES},
			{"Composition", S::Choice, GRAPH_EFFECT_COMPOSITIONS},
			{"Source", S::Choice, GRAPH_EFFECT_SOURCES},
			{"Random", S::Choice, GRAPH_EFFECT_RANDOMS},
		};
		constexpr u32 COMPOSITION_SETTING = 1;
		constexpr u32 SOURCE_SETTING = 2;
		constexpr u32 RANDOM_SETTING = 3;

		void SetAttributeBody(GraphBodyContext& c) {
			GraphProgramBuilder& b = c.builder;
			const std::string_view attribute = c.definition->outputs[0].name;
			const u32 composition = c.Setting(COMPOSITION_SETTING);
			const bool fromSource = c.Setting(SOURCE_SETTING) == 1;
			const u32 random = fromSource ? 0 : c.Setting(RANDOM_SETTING);
			GraphValueId value = GRAPH_NO_VALUE;
			if (fromSource) {
				value = c.Binding(GraphEffectAttributes::SourceBinding(attribute));
			} else if (random != 0) {
				value = RandomBetween(c, c.In(0), c.In(1), random == 1, GRAPH_EFFECT_RANDOM_PARTICLE, b.Scalar(0.0f), NodeSalt(c));
			} else {
				value = c.In(0);
			}
			if (composition == 0) {
				c.outputs[0] = value;
				return;
			}
			const GraphValueId current = c.Binding(GraphEffectAttributes::CurrentBinding(attribute));
			switch (composition) {
			case 1:
				c.outputs[0] = b.Add(current, value);
				break;
			case 2:
				c.outputs[0] = b.Multiply(current, value);
				break;
			default:
				c.outputs[0] = b.Lerp(current, value, c.In(static_cast<u32>(c.inputs.size()) - 1));
				break;
			}
		}

		constexpr GraphPortDefinition SET_ATTRIBUTE_INPUTS[] = {{"Position", P::Float3}};
		constexpr GraphPortDefinition SET_ATTRIBUTE_OUTPUTS[] = {{"position", P::Float3}};

		constexpr GraphSettingDefinition SET_SPAWN_EVENT_SETTINGS[] = {
			{GRAPH_EFFECT_ATTRIBUTE_SETTING, S::Choice, WRITABLE_NAMES},
			{"Random Mode", S::Choice, GRAPH_EFFECT_RANDOMS},
		};

		void SetSpawnEventBody(GraphBodyContext& c) {
			const u32 random = c.Setting(1);
			c.outputs[0] = random == 0 ? c.In(0) : RandomBetween(c, c.In(0), c.In(1), random == 1, GRAPH_EFFECT_RANDOM_EFFECT, c.builder.Scalar(0.0f), NodeSalt(c));
		}

		constexpr GraphPortDefinition SET_SPAWN_EVENT_INPUTS[] = {{"Position", P::Float3}};

		constexpr u32 AttributeOptions(bool (*keep)(const GraphEffectAttributeInfo&)) {
			u32 mask = 0;
			for (size_t i = 0; i < WRITABLE_COUNT; ++i) {
				mask |= keep(ATTRIBUTES[i]) ? 1u << i : 0u;
			}
			return mask;
		}
		constexpr u32 COLOR_OPTION = AttributeOptions([](const GraphEffectAttributeInfo& a) { return a.name == "color"; });
		constexpr u32 VECTOR_OPTIONS = AttributeOptions([](const GraphEffectAttributeInfo& a) { return a.type == T::Float3; });
		static_assert(WRITABLE_COUNT <= 32, "a setting's whenOptions holds a bit per Attribute option");

		constexpr std::string_view COMPUTE_MODES[] = {"Uniform", "PerComponent"};
		constexpr std::string_view COLOR_MODES[] = {"Color", "Alpha", "ColorAndAlpha"};
		constexpr GraphSettingDefinition ATTRIBUTE_FROM_CURVE_SETTINGS[] = {
			{GRAPH_EFFECT_ATTRIBUTE_SETTING, S::Choice, WRITABLE_NAMES},
			{"Composition", S::Choice, GRAPH_EFFECT_COMPOSITIONS},
			{.key = "Alpha Composition", .kind = S::Choice, .options = GRAPH_EFFECT_COMPOSITIONS, .whenSetting = 0, .whenOptions = COLOR_OPTION},
			{.key = "Sample Mode", .kind = S::Choice, .options = GRAPH_EFFECT_SAMPLE_MODES, .inGraph = true},
			{.key = "Mode", .kind = S::Choice, .options = COMPUTE_MODES, .defaultIndex = 1, .whenSetting = 0, .whenOptions = VECTOR_OPTIONS},
			{.key = "Color Mode", .kind = S::Choice, .options = COLOR_MODES, .defaultIndex = 2, .inGraph = true, .whenSetting = 0, .whenOptions = COLOR_OPTION},
		};
		constexpr u32 ALPHA_COMPOSITION_SETTING = 2;
		constexpr u32 SAMPLE_MODE_SETTING = 3;
		constexpr u32 MODE_SETTING = 4;

		constexpr std::string_view BLOCK_GRADIENT = R"({"alphas":[[0,0],[1,0.1],[0.8,0.8],[0,1]],"colors":[[1,1,1,0],[1,1,1,1]],"mode":"Blend"})";

		GraphValueId Compose(GraphProgramBuilder& b, GraphValueId current, GraphValueId value, u32 composition, GraphValueId blend) {
			switch (composition) {
			case 0:
				return value;
			case 1:
				return b.Add(current, value);
			case 2:
				return b.Multiply(current, value);
			default:
				return b.Lerp(current, value, blend);
			}
		}

		void AttributeFromCurveBody(GraphBodyContext& c) {
			GraphProgramBuilder& b = c.builder;
			const GraphEffectAttributeInfo& written = ATTRIBUTES[c.Setting(0)];
			const auto input = [&](std::string_view name) {
				const i32 at = GraphNodeLibrary::FindInput(*c.definition, name);
				return at < 0 ? GRAPH_NO_VALUE : c.In(static_cast<u32>(at));
			};
			const auto current = [&](std::string_view attribute) { return c.Binding(GraphEffectAttributes::CurrentBinding(attribute)); };

			GraphValueId t = GRAPH_NO_VALUE;
			switch (c.Setting(SAMPLE_MODE_SETTING)) {
			case 0:
				t = b.Divide(current("age"), current("lifetime"));
				break;
			case 1: {
				const GraphValueId range = input("Speed Range");
				const GraphValueId low = b.Swizzle(range, "x");
				t = b.Unary(GraphOp::Saturate, b.Divide(b.Subtract(b.Unary(GraphOp::Length, current("velocity")), low), b.Subtract(b.Swizzle(range, "y"), low)));
				break;
			}
			case 2:
				t = b.Random(c.Binding(GRAPH_EFFECT_RANDOM_PARTICLE), b.Scalar(0.0f), NodeSalt(c));
				break;
			case 3:
				t = b.Random(c.Binding(GRAPH_EFFECT_RANDOM_PARTICLE_CONSTANT), input("Seed"), 0);
				break;
			default:
				t = input("Sample Time");
				break;
			}

			const str label = Capitalized(written.name);
			const auto curve = [&](std::string_view port) { return GraphCurves::Sample(b, b.ConstantOf(input(port)), t); };
			const bool color = written.name == "color";
			const bool perComponent = c.Setting(MODE_SETTING) == 1;
			const GraphValueId blend = input("Blend");
			if (color) {
				const GraphValueId value = perComponent ? GraphGradients::Sample(b, b.ConstantOf(input(label)), t) : curve(label);
				const i32 rgb = GraphNodeLibrary::FindOutput(*c.definition, "color");
				const i32 alpha = GraphNodeLibrary::FindOutput(*c.definition, "alpha");
				if (rgb >= 0) {
					c.outputs[static_cast<u32>(rgb)] = Compose(b, current("color"), b.Swizzle(value, perComponent ? "xyz" : "xxx"), c.Setting(1), blend);
				}
				if (alpha >= 0) {
					c.outputs[static_cast<u32>(alpha)] = Compose(b, current("alpha"), perComponent ? b.Swizzle(value, "w") : value, c.Setting(ALPHA_COMPOSITION_SETTING), blend);
				}
				return;
			}
			GraphValueId value = GRAPH_NO_VALUE;
			if (written.type == T::Float) {
				value = curve(label);
			} else if (!perComponent) {
				value = b.Swizzle(curve(label), "xxx");
			} else {
				const GraphValueId parts[] = {curve(label + "_x"), curve(label + "_y"), curve(label + "_z")};
				value = b.Construct(T::Float3, parts);
			}
			c.outputs[0] = Compose(b, current(written.name), value, c.Setting(1), blend);
		}

		constexpr GraphSettingDefinition GET_ATTRIBUTE_SETTINGS[] = {
			{GRAPH_EFFECT_ATTRIBUTE_SETTING, S::Choice, ATTRIBUTE_NAMES},
			{"Location", S::Choice, GRAPH_EFFECT_LOCATIONS, 0, {}, {}, true},
		};

		void GetAttributeBody(GraphBodyContext& c) {
			const std::string_view attribute = c.definition->outputs[0].name;
			c.outputs[0] = c.Binding(c.Setting(1) == 1 ? GraphEffectAttributes::SourceBinding(attribute) : GraphEffectAttributes::CurrentBinding(attribute));
		}

		void AgeOverLifetimeBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Divide(c.Binding(GraphEffectAttributes::CurrentBinding("age")),
											c.Binding(GraphEffectAttributes::CurrentBinding("lifetime")));
		}
		constexpr GraphPortDefinition AGE_OVER_LIFETIME_OUTPUTS[] = {{"t", P::Float}};

		void RatioOverStripBody(GraphBodyContext& c) {
			GraphProgramBuilder& b = c.builder;
			c.outputs[0] = b.Divide(c.Binding(GraphEffectAttributes::CurrentBinding("particleIndexInStrip")),
									b.Subtract(c.Binding(GraphEffectAttributes::CurrentBinding("particleCountInStrip")), b.Scalar(1.0f)));
		}

		constexpr GraphPortDefinition INCREMENT_STRIP_INDEX_INPUTS[] = {{"Strip Max Count", P::Float, {8.0f, 0.0f, 0.0f, 0.0f}}};

		void TotalTimeBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Swizzle(c.Binding("TimeParameters"), "x"); }
		void DeltaTimeBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Swizzle(c.Binding("TimeParameters"), "w"); }
		constexpr GraphPortDefinition TOTAL_TIME_OUTPUTS[] = {{"totalTime", P::Float}};
		constexpr GraphPortDefinition DELTA_TIME_OUTPUTS[] = {{"deltaTime", P::Float}};

		constexpr std::string_view RANDOM_TYPES[] = {"Float", "Vector2", "Vector3", "Vector4"};
		constexpr std::string_view SEED_MODES[] = {"PerParticle", "PerEffectComponent"};
		constexpr GraphSettingDefinition RANDOM_NUMBER_SETTINGS[] = {
			{"Type", S::Choice, RANDOM_TYPES},
			{"Seed", S::Choice, SEED_MODES, 0, {}, {}, true},
			{"Constant", S::Toggle, {}, 1, {}, {}, true},
		};
		constexpr u32 SEED_SETTING = 1;
		constexpr u32 CONSTANT_SETTING = 2;

		void RandomNumberBody(GraphBodyContext& c) {
			const bool perParticle = c.Setting(SEED_SETTING) == 0;
			const bool constant = c.Setting(CONSTANT_SETTING) != 0;
			std::string_view stream;
			if (constant) {
				stream = perParticle ? GRAPH_EFFECT_RANDOM_PARTICLE_CONSTANT : GRAPH_EFFECT_RANDOM_EFFECT_CONSTANT;
			} else {
				stream = perParticle ? GRAPH_EFFECT_RANDOM_PARTICLE : GRAPH_EFFECT_RANDOM_EFFECT;
			}
			const GraphValueId seed = constant ? c.In(2) : c.builder.Scalar(0.0f);
			c.outputs[0] = RandomBetween(c, c.In(0), c.In(1), true, stream, seed, constant ? 0u : NodeSalt(c));
		}

		constexpr GraphPortDefinition RANDOM_NUMBER_INPUTS[] = {
			{"Min", P::Float, {0.0f, 0.0f, 0.0f, 0.0f}},
			{"Max", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}},
			{.name = "Seed", .type = P::Float, .whenSetting = CONSTANT_SETTING, .whenOptions = 1u << 1},
		};
		constexpr GraphPortDefinition RANDOM_NUMBER_OUTPUTS[] = {{"r", P::Float}};

		void SampleCurveBody(GraphBodyContext& c) {
			c.outputs[0] = GraphCurves::Sample(c.builder, c.builder.ConstantOf(c.In(0)), c.In(1));
		}

		constexpr GraphPortDefinition SAMPLE_CURVE_INPUTS[] = {{"Curve", P::Curve}, {"Time", P::Float}};
		constexpr GraphPortDefinition SAMPLE_CURVE_OUTPUTS[] = {{"s", P::Float}};

		GraphValueId FlippedUV(GraphProgramBuilder& b, GraphValueId uv) {
			const GraphValueId parts[] = {b.Swizzle(uv, "x"), b.Subtract(b.Scalar(1.0f), b.Swizzle(uv, "y"))};
			return b.Construct(T::Float2, parts);
		}

		void SampleTexture2DBody(GraphBodyContext& c) {
			const GraphValueId operands[] = {c.In(0), FlippedUV(c.builder, c.In(1)), c.In(2)};
			c.outputs[0] = c.builder.TextureOp(GraphOp::SampleTextureLevel, operands);
		}

		void SampleTexture2DArrayBody(GraphBodyContext& c) {
			GraphProgramBuilder& b = c.builder;
			const GraphValueId place[] = {FlippedUV(b, c.In(1)), c.In(2)};
			const GraphValueId operands[] = {c.In(0), b.Construct(T::Float3, place), c.In(3)};
			c.outputs[0] = b.TextureOp(GraphOp::SampleTextureLevel, operands);
		}

		void SampleVolumeBody(GraphBodyContext& c) {
			const GraphValueId operands[] = {c.In(0), c.In(1), c.In(2)};
			c.outputs[0] = c.builder.TextureOp(GraphOp::SampleTextureLevel, operands);
		}

		constexpr GraphPortDefinition SAMPLE_TEXTURE_2D_INPUTS[] = {{"Texture", P::Texture2D}, {"UV", P::Float2}, {"Mip Level", P::Float}};
		constexpr GraphPortDefinition SAMPLE_TEXTURE_2D_ARRAY_INPUTS[] = {
			{"Texture", P::Texture2DArray}, {"UV", P::Float2}, {"Slice", P::Float}, {"Mip Level", P::Float}};
		constexpr GraphPortDefinition SAMPLE_TEXTURE_3D_INPUTS[] = {{"Texture", P::Texture3D}, {"UVW", P::Float3}, {"Mip Level", P::Float}};
		constexpr GraphPortDefinition SAMPLE_TEXTURE_CUBE_INPUTS[] = {{"Texture", P::Cubemap}, {"UVW", P::Float3}, {"Mip Level", P::Float}};
		constexpr GraphPortDefinition SAMPLE_TEXTURE_OUTPUTS[] = {{"s", P::Float4}};

		constexpr GraphSettingDefinition NOISE_SETTINGS[] = {
			{"Type", S::Choice, GRAPH_EFFECT_NOISE_TYPES, 1, {}, {}, true},
			{"Dimensions", S::Choice, GRAPH_EFFECT_NOISE_DIMENSIONS, 1, {}, {}, true},
		};
		constexpr GraphSettingDefinition CURL_NOISE_SETTINGS[] = {
			{"Type", S::Choice, GRAPH_EFFECT_NOISE_TYPES, 1, {}, {}, true},
			{"Dimensions", S::Choice, GRAPH_EFFECT_CURL_DIMENSIONS, 0, {}, {}, true},
		};

		constexpr GraphPortDefinition NOISE_COMMON_INPUTS[] = {
			{"Frequency", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}},
			{"Octaves", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}},
			{"Roughness", P::Float, {0.5f, 0.0f, 0.0f, 0.0f}},
			{"Lacunarity", P::Float, {2.0f, 0.0f, 0.0f, 0.0f}},
		};
		constexpr GraphPortDefinition NOISE_INPUTS[] = {{"Coordinate", P::Float2}};
		constexpr GraphPortDefinition NOISE_OUTPUTS[] = {{"Noise", P::Float}, {"Derivatives", P::Float2}};
		constexpr GraphPortDefinition CURL_NOISE_OUTPUTS[] = {{"Noise", P::Float2}};

		GraphValueId NoiseParameters(GraphBodyContext& c) {
			const GraphValueId parts[] = {c.In(1), c.In(3), c.In(4)};
			return c.builder.Construct(T::Float3, parts);
		}

		void NoiseBody(GraphBodyContext& c) {
			GraphProgramBuilder& b = c.builder;
			const auto type = static_cast<GraphEffectNoise>(c.Setting(0));
			const GraphValueId result = b.EffectNoise(GraphOp::FractalNoise, type, c.In(0), NoiseParameters(c), c.In(2));
			const GraphValueId range = c.In(5);
			const GraphValueId low = b.Swizzle(range, "x");
			const GraphValueId width = b.Subtract(b.Swizzle(range, "y"), low);
			const f32 floor = type == GraphEffectNoise::Perlin ? -1.0f : 0.0f;
			const GraphValueId t = b.Divide(b.Subtract(b.Swizzle(result, "x"), b.Scalar(floor)), b.Scalar(1.0f - floor));
			c.outputs[0] = b.Add(low, b.Multiply(t, width));
			constexpr std::string_view DERIVATIVES[] = {"y", "yz", "yzw"};
			const u32 dimension = GraphTypes::VectorWidth(b.TypeOf(result)) - 1;
			c.outputs[1] = b.Multiply(b.Swizzle(result, DERIVATIVES[dimension - 1]), width);
		}

		void CurlNoiseBody(GraphBodyContext& c) {
			GraphProgramBuilder& b = c.builder;
			const auto type = static_cast<GraphEffectNoise>(c.Setting(0));
			c.outputs[0] = b.Multiply(b.EffectNoise(GraphOp::CurlNoise, type, c.In(0), NoiseParameters(c), c.In(2)), c.In(5));
		}

		constexpr std::string_view SPAWN_FLOW[] = {GRAPH_EFFECT_START, GRAPH_EFFECT_STOP};
		constexpr std::string_view IN_FLOW[] = {GRAPH_EFFECT_IN};

		constexpr GraphSettingDefinition EVENT_SETTINGS[] = {{"Event Name", S::Text, {}, 0, GRAPH_EFFECT_ON_PLAY, {}, true}};

		constexpr std::string_view LOOP_MODES[] = {"Infinite", "Constant", "Random"};
		constexpr std::string_view DELAY_MODES[] = {"None", "Constant", "Random"};
		constexpr GraphSettingDefinition SPAWN_SETTINGS[] = {
			{"Loop Duration", S::Choice, LOOP_MODES},
			{"Loop Count", S::Choice, LOOP_MODES},
			{"Delay Before Loop", S::Choice, DELAY_MODES},
			{"Delay After Loop", S::Choice, DELAY_MODES},
		};
		constexpr u32 CONSTANT_OPTION = 1u << 1;
		constexpr u32 RANDOM_OPTION = 1u << 2;
		constexpr GraphPortDefinition SPAWN_INPUTS[] = {
			{.name = "Loop Duration", .type = P::Float, .defaultValue = {0.1f}, .whenSetting = 0, .whenOptions = CONSTANT_OPTION},
			{.name = "Loop Duration Range", .type = P::Float2, .defaultValue = {1.0f, 3.0f}, .whenSetting = 0, .whenOptions = RANDOM_OPTION, .label = "Loop Duration"},
			{.name = "Loop Count", .type = P::Float, .defaultValue = {1.0f}, .whenSetting = 1, .whenOptions = CONSTANT_OPTION},
			{.name = "Loop Count Range", .type = P::Float2, .defaultValue = {1.0f, 3.0f}, .whenSetting = 1, .whenOptions = RANDOM_OPTION, .label = "Loop Count"},
			{.name = "Delay Before Loop", .type = P::Float, .defaultValue = {0.1f}, .whenSetting = 2, .whenOptions = CONSTANT_OPTION},
			{.name = "Delay Before Loop Range", .type = P::Float2, .defaultValue = {0.1f, 0.3f}, .whenSetting = 2, .whenOptions = RANDOM_OPTION, .label = "Delay Before Loop"},
			{.name = "Delay After Loop", .type = P::Float, .defaultValue = {0.1f}, .whenSetting = 3, .whenOptions = CONSTANT_OPTION},
			{.name = "Delay After Loop Range", .type = P::Float2, .defaultValue = {0.1f, 0.3f}, .whenSetting = 3, .whenOptions = RANDOM_OPTION, .label = "Delay After Loop"},
		};

		constexpr std::string_view SPACES[] = {"Local", "World"};
		constexpr u32 PARTICLE_DATA = 1u << 0;
		constexpr u32 STRIP_DATA = 1u << 1;
		constexpr GraphSettingDefinition INITIALIZE_SETTINGS[] = {
			{.key = "Capacity", .kind = S::Number, .defaultValue = {128.0f, 0.0f, 0.0f, 0.0f}, .inGraph = true, .whenSetting = 2, .whenOptions = PARTICLE_DATA},
			{"Space", S::Choice, SPACES, 0, {}, {}, true},
			{.key = "Data Type", .kind = S::Choice, .options = GRAPH_EFFECT_DATA_TYPES},
			{.key = "Strip Capacity", .kind = S::Number, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .inGraph = true, .whenSetting = 2, .whenOptions = STRIP_DATA},
			{.key = "Particle Per Strip Count", .kind = S::Number, .defaultValue = {128.0f, 0.0f, 0.0f, 0.0f}, .inGraph = true, .whenSetting = 2, .whenOptions = STRIP_DATA},
			{.key = GRAPH_EFFECT_SIMULATION_TARGET, .kind = S::Choice, .options = GRAPH_EFFECT_SIMULATION_TARGETS},
			{.key = GRAPH_EFFECT_SYSTEM_NAME, .kind = S::Text, .defaultText = GRAPH_EFFECT_DEFAULT_SYSTEM_NAME, .inGraph = true},
			{.key = GRAPH_EFFECT_BOUNDS_SETTING_MODE, .kind = S::Choice, .options = GRAPH_EFFECT_BOUNDS_MODES, .inGraph = true},
		};
		constexpr u32 AUTHORED_BOUNDS = (1u << GRAPH_EFFECT_BOUNDS_RECORDED) | (1u << GRAPH_EFFECT_BOUNDS_MANUAL);
		constexpr u32 PADDED_BOUNDS = (1u << GRAPH_EFFECT_BOUNDS_RECORDED) | (1u << GRAPH_EFFECT_BOUNDS_AUTOMATIC);
		constexpr GraphPortDefinition INITIALIZE_INPUTS[] = {
			{.name = GRAPH_EFFECT_BOUNDS_CENTER, .type = P::Float3, .whenSetting = 7, .whenOptions = AUTHORED_BOUNDS, .space = GraphPortSpace::Position},
			{.name = GRAPH_EFFECT_BOUNDS_SIZE, .type = P::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}, .whenSetting = 7, .whenOptions = AUTHORED_BOUNDS},
			{.name = GRAPH_EFFECT_BOUNDS_PADDING, .type = P::Float3, .whenSetting = 7, .whenOptions = PADDED_BOUNDS},
			{.name = "Strip Index", .type = P::Float, .whenSetting = 2, .whenOptions = STRIP_DATA}};

		constexpr GraphSettingDefinition UPDATE_SETTINGS[] = {
			{"Update Position", S::Toggle, {}, 1}, {"Age Particles", S::Toggle, {}, 1}, {"Reap Particles", S::Toggle, {}, 1}};

		constexpr GraphSettingDefinition BLOCK_SUBGRAPH_SETTINGS[] = {
			{GRAPH_EFFECT_SUITABLE_CONTEXTS_SETTING, S::Choice, GRAPH_EFFECT_SUITABLE_CONTEXTS, GRAPH_EFFECT_DEFAULT_SUITABLE_CONTEXTS, {}, {}, true}};

		constexpr GraphPortDefinition GPU_EVENT_INPUTS[] = {{.name = GRAPH_EFFECT_EVT, .type = P::GPUEvent}};
		constexpr GraphSettingDefinition OUTPUT_EVENT_SETTINGS[] = {
			{.key = "Event Name", .kind = S::Text, .defaultText = GRAPH_EFFECT_DEFAULT_OUTPUT_EVENT_NAME, .inGraph = true}};

		constexpr std::string_view BLEND_MODES[] = {"Additive", "Alpha", "AlphaPremultiplied", "Opaque"};
		constexpr std::string_view BASE_COLOR_MAPS[] = {"None", "Color", "Alpha", "ColorAndAlpha"};
		constexpr std::string_view UV_MODES[] = {"Default", "Flipbook", "ScaleAndBias"};
		constexpr u32 ANY_MAP = (1u << 1) | (1u << 2) | (1u << 3);
		constexpr u32 BLENDED = (1u << 0) | (1u << 1) | (1u << 2);
		constexpr u32 SORT_ON = 1u << 2;
		constexpr u32 SORT_AUTO = 1u << 0;
		constexpr u32 NOT_COMMUTATIVE = (1u << 1) | (1u << 2);
		constexpr GraphSettingDefinition ClippingSetting() { return {.key = "Use Alpha Clipping", .kind = S::Toggle, .inGraph = true}; }
		constexpr GraphSettingDefinition SoftSetting(u8 blend) {
			return {.key = "Use Soft Particle", .kind = S::Toggle, .inGraph = true, .whenSetting = blend, .whenOptions = BLENDED};
		}
		constexpr GraphSettingDefinition SortSetting() { return {.key = "Sort", .kind = S::Choice, .options = GRAPH_EFFECT_SORTS}; }
		constexpr GraphSettingDefinition SortModeSetting(u8 sort, u8 blend) {
			return {.key = "Sort Mode", .kind = S::Choice, .options = GRAPH_EFFECT_SORT_MODES, .whenSetting = sort, .whenOptions = SORT_ON, .orSetting = sort, .orOptions = SORT_AUTO, .orAlsoSetting = blend, .orAlsoOptions = NOT_COMMUTATIVE};
		}
		constexpr GraphSettingDefinition RevertSortingSetting(u8 sort, u8 blend) {
			return {.key = "Revert Sorting", .kind = S::Toggle, .whenSetting = sort, .whenOptions = SORT_ON, .orSetting = sort, .orOptions = SORT_AUTO, .orAlsoSetting = blend, .orAlsoOptions = NOT_COMMUTATIVE};
		}
		constexpr GraphSettingDefinition CastShadowsSetting() { return {.key = "Cast Shadows", .kind = S::Toggle}; }
		constexpr GraphSettingDefinition CullModeSetting() { return {.key = GRAPH_EFFECT_CULL_MODE, .kind = S::Choice, .options = GRAPH_EFFECT_CULL_MODES}; }
		constexpr GraphSettingDefinition ZWriteModeSetting() {
			return {.key = GRAPH_EFFECT_Z_WRITE_MODE, .kind = S::Choice, .options = GRAPH_EFFECT_Z_WRITE_MODES};
		}
		constexpr GraphSettingDefinition ZTestModeSetting() { return {.key = GRAPH_EFFECT_Z_TEST_MODE, .kind = S::Choice, .options = GRAPH_EFFECT_Z_TEST_MODES}; }
		constexpr GraphPortDefinition AlphaThresholdPort(u8 clipping, u8 casts, u8 blend) {
			return {.name = "Alpha Threshold", .type = P::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}, .whenSetting = clipping, .whenOptions = 1u << 1, .orSetting = casts, .orOptions = 1u << 1, .orAlsoSetting = blend, .orAlsoOptions = BLENDED};
		}
		constexpr GraphPortDefinition SoftFadePort(u8 soft) {
			return {.name = "Soft Particle Fade Distance", .type = P::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = soft, .whenOptions = 1u << 1};
		}
		constexpr GraphPortDefinition SortKeyPort(u8 mode) { return {.name = "Sort Key", .type = P::Float, .whenSetting = mode, .whenOptions = 1u << 3}; }

		constexpr GraphSettingDefinition OUTPUT_SETTINGS[] = {
			{"Primitive Type", S::Choice, GRAPH_EFFECT_PRIMITIVES, 1},
			{"Blend Mode", S::Choice, BLEND_MODES, 1, {}, {}, true},
			{.key = "Use Base Color Map", .kind = S::Choice, .options = BASE_COLOR_MAPS, .defaultIndex = 3},
			{.key = "UV Mode", .kind = S::Choice, .options = UV_MODES, .inGraph = true, .whenSetting = 2, .whenOptions = ANY_MAP},
			{.key = "Flipbook Blend Frames", .kind = S::Toggle, .inGraph = true, .whenSetting = 3, .whenOptions = 1u << 1},
			ClippingSetting(),
			SoftSetting(1),
			SortSetting(),
			SortModeSetting(7, 1),
			RevertSortingSetting(7, 1),
			CastShadowsSetting(),
			CullModeSetting(),
			ZWriteModeSetting(),
			ZTestModeSetting(),
		};
		constexpr GraphPortDefinition OUTPUT_INPUTS[] = {
			{.name = "Main Texture", .type = P::Texture2D, .binding = GRAPH_EFFECT_MAIN_TEXTURE_BINDING, .whenSetting = 2, .whenOptions = ANY_MAP},
			{.name = "Flip Book Size", .type = P::Float2, .defaultValue = {4.0f, 4.0f, 0.0f, 0.0f}, .whenSetting = 3, .whenOptions = 1u << 1},
			{.name = "UV Scale", .type = P::Float2, .defaultValue = {1.0f, 1.0f, 0.0f, 0.0f}, .whenSetting = 3, .whenOptions = 1u << 2},
			{.name = "UV Bias", .type = P::Float2, .whenSetting = 3, .whenOptions = 1u << 2},
			{.name = "Crop Factor", .type = P::Float, .defaultValue = {0.293f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = 1u << 2},
			AlphaThresholdPort(5, 10, 1),
			SoftFadePort(6),
			SortKeyPort(8),
		};
		constexpr GraphSettingDefinition OUTPUT_MESH_SETTINGS[] = {
			{"Blend Mode", S::Choice, BLEND_MODES, 1, {}, {}, true},
			{.key = "Use Base Color Map", .kind = S::Choice, .options = BASE_COLOR_MAPS, .defaultIndex = 3},
			{.key = "UV Mode", .kind = S::Choice, .options = UV_MODES, .inGraph = true, .whenSetting = 1, .whenOptions = ANY_MAP},
			{.key = "Flipbook Blend Frames", .kind = S::Toggle, .inGraph = true, .whenSetting = 2, .whenOptions = 1u << 1},
			ClippingSetting(),
			SoftSetting(0),
			SortSetting(),
			SortModeSetting(6, 0),
			RevertSortingSetting(6, 0),
			CastShadowsSetting(),
			CullModeSetting(),
			ZWriteModeSetting(),
			ZTestModeSetting(),
		};
		constexpr GraphPortDefinition OUTPUT_MESH_INPUTS[] = {
			GraphEffectBlocks::MESH_INPUT[0],
			{.name = "Main Texture", .type = P::Texture2D, .binding = GRAPH_EFFECT_MAIN_TEXTURE_BINDING, .whenSetting = 1, .whenOptions = ANY_MAP},
			{.name = "Flip Book Size", .type = P::Float2, .defaultValue = {4.0f, 4.0f, 0.0f, 0.0f}, .whenSetting = 2, .whenOptions = 1u << 1},
			{.name = "UV Scale", .type = P::Float2, .defaultValue = {1.0f, 1.0f, 0.0f, 0.0f}, .whenSetting = 2, .whenOptions = 1u << 2},
			{.name = "UV Bias", .type = P::Float2, .whenSetting = 2, .whenOptions = 1u << 2},
			AlphaThresholdPort(4, 9, 0),
			SoftFadePort(5),
			SortKeyPort(7),
		};
		constexpr GraphSettingDefinition OUTPUT_POINT_SETTINGS[] = {
			{"Blend Mode", S::Choice, BLEND_MODES, 1, {}, {}, true},
			ClippingSetting(),
			SoftSetting(0),
			SortSetting(),
			SortModeSetting(3, 0),
			RevertSortingSetting(3, 0),
			CastShadowsSetting(),
			CullModeSetting(),
			ZWriteModeSetting(),
			ZTestModeSetting(),
		};
		constexpr GraphPortDefinition OUTPUT_POINT_INPUTS[] = {AlphaThresholdPort(1, 6, 0), SoftFadePort(2), SortKeyPort(4)};
		constexpr GraphSettingDefinition OUTPUT_LINE_SETTINGS[] = {
			{"Blend Mode", S::Choice, BLEND_MODES, 1, {}, {}, true},
			{.key = "Use Target Offset", .kind = S::Toggle, .defaultIndex = 1, .inGraph = true},
			{.key = "Use Native Lines", .kind = S::Toggle},
			ClippingSetting(),
			SoftSetting(0),
			SortSetting(),
			SortModeSetting(5, 0),
			RevertSortingSetting(5, 0),
			CastShadowsSetting(),
			CullModeSetting(),
			ZWriteModeSetting(),
			ZTestModeSetting(),
		};
		constexpr GraphPortDefinition OUTPUT_LINE_INPUTS[] = {
			{.name = "Target Offset", .type = P::Float3, .defaultValue = {0.0f, 1.0f, 0.0f, 0.0f}, .whenSetting = 1, .whenOptions = 1u << 1},
			AlphaThresholdPort(3, 8, 0),
			SoftFadePort(4),
			SortKeyPort(6),
		};

		constexpr u32 COLOR_NOT_BASE = (1u << 0) | (1u << 2);
		constexpr u32 COLOR_NOT_EMISSIVE = (1u << 0) | (1u << 1);
		constexpr GraphSettingDefinition OUTPUT_LIT_SETTINGS[] = {
			{"Primitive Type", S::Choice, GRAPH_EFFECT_PRIMITIVES, 1},
			{"Blend Mode", S::Choice, BLEND_MODES, 1, {}, {}, true},
			{.key = "Use Base Color Map", .kind = S::Choice, .options = BASE_COLOR_MAPS, .defaultIndex = 3},
			{.key = "UV Mode", .kind = S::Choice, .options = UV_MODES, .inGraph = true, .whenSetting = 2, .whenOptions = ANY_MAP},
			{.key = "Flipbook Blend Frames", .kind = S::Toggle, .inGraph = true, .whenSetting = 3, .whenOptions = 1u << 1},
			ClippingSetting(),
			SoftSetting(1),
			SortSetting(),
			SortModeSetting(7, 1),
			RevertSortingSetting(7, 1),
			CastShadowsSetting(),
			{.key = "Smoothness Source", .kind = S::Choice, .options = GRAPH_EFFECT_SMOOTHNESS_SOURCES},
			{.key = "Use Occlusion Map", .kind = S::Toggle},
			{.key = "Use Metallic Map", .kind = S::Toggle},
			{.key = "Use Normal Map", .kind = S::Toggle},
			{.key = "Use Emissive Map", .kind = S::Toggle},
			{.key = "Color Mode", .kind = S::Choice, .options = GRAPH_EFFECT_COLOR_MODES, .defaultIndex = 1},
			{.key = "Use Emissive", .kind = S::Toggle, .whenSetting = 16, .whenOptions = COLOR_NOT_EMISSIVE},
			{.key = "Double Sided", .kind = S::Toggle},
			{.key = "Receive Shadows", .kind = S::Toggle, .defaultIndex = 1},
			{.key = "Normal Bending", .kind = S::Toggle},
			CullModeSetting(),
			ZWriteModeSetting(),
			ZTestModeSetting(),
		};
		constexpr GraphPortDefinition LitMapPort(u32 map, u8 when) {
			return {.name = GRAPH_EFFECT_LIT_MAP_PORTS[map], .type = P::Texture2D, .binding = GRAPH_EFFECT_LIT_MAP_BINDINGS[map], .whenSetting = when, .whenOptions = 1u << 1};
		}
		constexpr GraphPortDefinition OUTPUT_LIT_INPUTS[] = {
			{.name = "Base Color Map", .type = P::Texture2D, .binding = GRAPH_EFFECT_MAIN_TEXTURE_BINDING, .whenSetting = 2, .whenOptions = ANY_MAP},
			{.name = "Flip Book Size", .type = P::Float2, .defaultValue = {4.0f, 4.0f, 0.0f, 0.0f}, .whenSetting = 3, .whenOptions = 1u << 1},
			{.name = "UV Scale", .type = P::Float2, .defaultValue = {1.0f, 1.0f, 0.0f, 0.0f}, .whenSetting = 3, .whenOptions = 1u << 2},
			{.name = "UV Bias", .type = P::Float2, .whenSetting = 3, .whenOptions = 1u << 2},
			{.name = "Crop Factor", .type = P::Float, .defaultValue = {0.293f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = 1u << 2},
			{.name = "Smoothness", .type = P::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}},
			{.name = "Metallic", .type = P::Float},
			LitMapPort(0, 12),
			LitMapPort(1, 13),
			LitMapPort(2, 14),
			{.name = "Normal Scale", .type = P::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 14, .whenOptions = 1u << 1},
			LitMapPort(3, 15),
			{.name = "Emissive Scale", .type = P::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 15, .whenOptions = 1u << 1},
			{.name = "Base Color", .type = P::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}, .whenSetting = 16, .whenOptions = COLOR_NOT_BASE},
			{.name = "Emissive Color", .type = P::Float3, .whenSetting = 17, .whenOptions = 1u << 1},
			{.name = "Normal Bending Factor", .type = P::Float, .defaultValue = {0.1f, 0.0f, 0.0f, 0.0f}, .whenSetting = 20, .whenOptions = 1u << 1},
			AlphaThresholdPort(5, 10, 1),
			SoftFadePort(6),
			SortKeyPort(8),
		};
		constexpr GraphSettingDefinition OUTPUT_LIT_MESH_SETTINGS[] = {
			{"Blend Mode", S::Choice, BLEND_MODES, 1, {}, {}, true},
			{.key = "Use Base Color Map", .kind = S::Choice, .options = BASE_COLOR_MAPS, .defaultIndex = 3},
			{.key = "UV Mode", .kind = S::Choice, .options = UV_MODES, .inGraph = true, .whenSetting = 1, .whenOptions = ANY_MAP},
			{.key = "Flipbook Blend Frames", .kind = S::Toggle, .inGraph = true, .whenSetting = 2, .whenOptions = 1u << 1},
			ClippingSetting(),
			SoftSetting(0),
			SortSetting(),
			SortModeSetting(6, 0),
			RevertSortingSetting(6, 0),
			CastShadowsSetting(),
			{.key = "Smoothness Source", .kind = S::Choice, .options = GRAPH_EFFECT_SMOOTHNESS_SOURCES},
			{.key = "Use Occlusion Map", .kind = S::Toggle},
			{.key = "Use Metallic Map", .kind = S::Toggle},
			{.key = "Use Normal Map", .kind = S::Toggle},
			{.key = "Use Emissive Map", .kind = S::Toggle},
			{.key = "Color Mode", .kind = S::Choice, .options = GRAPH_EFFECT_COLOR_MODES, .defaultIndex = 1},
			{.key = "Use Emissive", .kind = S::Toggle, .whenSetting = 15, .whenOptions = COLOR_NOT_EMISSIVE},
			{.key = "Double Sided", .kind = S::Toggle},
			{.key = "Receive Shadows", .kind = S::Toggle, .defaultIndex = 1},
			CullModeSetting(),
			ZWriteModeSetting(),
			ZTestModeSetting(),
		};
		constexpr GraphPortDefinition OUTPUT_LIT_MESH_INPUTS[] = {
			GraphEffectBlocks::MESH_INPUT[0],
			{.name = "Base Color Map", .type = P::Texture2D, .binding = GRAPH_EFFECT_MAIN_TEXTURE_BINDING, .whenSetting = 1, .whenOptions = ANY_MAP},
			{.name = "Flip Book Size", .type = P::Float2, .defaultValue = {4.0f, 4.0f, 0.0f, 0.0f}, .whenSetting = 2, .whenOptions = 1u << 1},
			{.name = "UV Scale", .type = P::Float2, .defaultValue = {1.0f, 1.0f, 0.0f, 0.0f}, .whenSetting = 2, .whenOptions = 1u << 2},
			{.name = "UV Bias", .type = P::Float2, .whenSetting = 2, .whenOptions = 1u << 2},
			{.name = "Smoothness", .type = P::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}},
			{.name = "Metallic", .type = P::Float},
			LitMapPort(0, 11),
			LitMapPort(1, 12),
			LitMapPort(2, 13),
			{.name = "Normal Scale", .type = P::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 13, .whenOptions = 1u << 1},
			LitMapPort(3, 14),
			{.name = "Emissive Scale", .type = P::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 14, .whenOptions = 1u << 1},
			{.name = "Base Color", .type = P::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}, .whenSetting = 15, .whenOptions = COLOR_NOT_BASE},
			{.name = "Emissive Color", .type = P::Float3, .whenSetting = 16, .whenOptions = 1u << 1},
			AlphaThresholdPort(4, 9, 0),
			SoftFadePort(5),
			SortKeyPort(7),
		};

		constexpr std::string_view NODE_SHADER_EXTENSIONS[] = {".slshader"};
		constexpr GraphSettingDefinition NodeShaderSetting() {
			return {.key = GRAPH_EFFECT_NODE_SHADER_SETTING, .kind = S::Asset, .options = NODE_SHADER_EXTENSIONS, .defaultText = "EffectDefault", .inGraph = true};
		}
		constexpr GraphSettingDefinition GraphSortModeSetting(u8 sort) {
			return {.key = "Sort Mode", .kind = S::Choice, .options = GRAPH_EFFECT_SORT_MODES, .whenSetting = sort, .whenOptions = SORT_ON | SORT_AUTO};
		}
		constexpr GraphSettingDefinition GraphRevertSortingSetting(u8 sort) {
			return {.key = "Revert Sorting", .kind = S::Toggle, .whenSetting = sort, .whenOptions = SORT_ON | SORT_AUTO};
		}
		constexpr GraphPortDefinition GraphAlphaThresholdPort(u8 clipping, u8 casts) {
			return {.name = "Alpha Threshold", .type = P::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}, .whenSetting = clipping, .whenOptions = 1u << 1, .orSetting = casts, .orOptions = 1u << 1};
		}
		constexpr GraphSettingDefinition OUTPUT_NODE_SHADER_SETTINGS[] = {
			NodeShaderSetting(),
			{"Primitive Type", S::Choice, GRAPH_EFFECT_PRIMITIVES, 1},
			ClippingSetting(),
			SortSetting(),
			GraphSortModeSetting(3),
			GraphRevertSortingSetting(3),
			CastShadowsSetting(),
		};
		constexpr GraphPortDefinition OUTPUT_NODE_SHADER_INPUTS[] = {
			{.name = "Crop Factor", .type = P::Float, .defaultValue = {0.293f, 0.0f, 0.0f, 0.0f}, .whenSetting = 1, .whenOptions = 1u << 2},
			GraphAlphaThresholdPort(2, 6),
			SortKeyPort(4),
		};
		constexpr GraphSettingDefinition OUTPUT_NODE_SHADER_MESH_SETTINGS[] = {
			NodeShaderSetting(),
			ClippingSetting(),
			SortSetting(),
			GraphSortModeSetting(2),
			GraphRevertSortingSetting(2),
			CastShadowsSetting(),
		};
		constexpr GraphPortDefinition OUTPUT_NODE_SHADER_MESH_INPUTS[] = {
			GraphEffectBlocks::MESH_INPUT[0],
			GraphAlphaThresholdPort(1, 5),
			SortKeyPort(3),
		};

		constexpr u32 CUSTOM_TILING = 1u << 2;
		constexpr GraphSettingDefinition OUTPUT_STRIP_SETTINGS[] = {
			{"Blend Mode", S::Choice, BLEND_MODES, 1, {}, {}, true},
			{.key = "Use Base Color Map", .kind = S::Choice, .options = BASE_COLOR_MAPS, .defaultIndex = 3},
			{.key = "UV Mode", .kind = S::Choice, .options = UV_MODES, .inGraph = true, .whenSetting = 1, .whenOptions = ANY_MAP},
			{.key = "Flipbook Blend Frames", .kind = S::Toggle, .inGraph = true, .whenSetting = 2, .whenOptions = 1u << 1},
			{.key = "Tiling Mode", .kind = S::Choice, .options = GRAPH_EFFECT_STRIP_TILING_MODES, .inGraph = true},
			{.key = "Swap UV", .kind = S::Toggle, .inGraph = true, .whenSetting = 1, .whenOptions = ANY_MAP},
			ClippingSetting(),
			SoftSetting(0),
			SortSetting(),
			SortModeSetting(8, 0),
			RevertSortingSetting(8, 0),
			CastShadowsSetting(),
			CullModeSetting(),
			ZWriteModeSetting(),
			ZTestModeSetting(),
		};
		constexpr GraphPortDefinition TexCoordPort(u8 tiling) {
			return {.name = "Tex Coord", .type = P::Float, .whenSetting = tiling, .whenOptions = CUSTOM_TILING};
		}
		constexpr GraphPortDefinition OUTPUT_STRIP_INPUTS[] = {
			{.name = "Main Texture", .type = P::Texture2D, .binding = GRAPH_EFFECT_MAIN_TEXTURE_BINDING, .whenSetting = 1, .whenOptions = ANY_MAP},
			{.name = "Flip Book Size", .type = P::Float2, .defaultValue = {4.0f, 4.0f, 0.0f, 0.0f}, .whenSetting = 2, .whenOptions = 1u << 1},
			{.name = "UV Scale", .type = P::Float2, .defaultValue = {1.0f, 1.0f, 0.0f, 0.0f}, .whenSetting = 2, .whenOptions = 1u << 2},
			{.name = "UV Bias", .type = P::Float2, .whenSetting = 2, .whenOptions = 1u << 2},
			TexCoordPort(4),
			AlphaThresholdPort(6, 11, 0),
			SoftFadePort(7),
			SortKeyPort(9),
		};
		constexpr GraphSettingDefinition OUTPUT_NODE_SHADER_STRIP_SETTINGS[] = {
			NodeShaderSetting(),
			{.key = "Tiling Mode", .kind = S::Choice, .options = GRAPH_EFFECT_STRIP_TILING_MODES, .inGraph = true},
			{.key = "Swap UV", .kind = S::Toggle, .inGraph = true},
			ClippingSetting(),
			SortSetting(),
			GraphSortModeSetting(4),
			GraphRevertSortingSetting(4),
			CastShadowsSetting(),
		};
		constexpr GraphPortDefinition OUTPUT_NODE_SHADER_STRIP_INPUTS[] = {
			TexCoordPort(1),
			GraphAlphaThresholdPort(3, 7),
			SortKeyPort(5),
		};

		constexpr GraphPortDefinition CONSTANT_RATE_INPUTS[] = {{"Rate", P::Float, {10.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr std::string_view REPEATS[] = {"Single", "Periodic"};
		constexpr std::string_view RANGE_MODES[] = {"Constant", "Random"};
		constexpr GraphSettingDefinition BURST_SETTINGS[] = {
			{"Repeat", S::Choice, REPEATS},
			{"Spawn Mode", S::Choice, RANGE_MODES, 0, {}, {}, true},
			{"Delay Mode", S::Choice, RANGE_MODES, 0, {}, {}, true},
		};
		constexpr GraphPortDefinition BURST_INPUTS[] = {
			{.name = "Count", .type = P::Float, .whenSetting = 1, .whenOptions = 1u << 0},
			{.name = "Count Range", .type = P::Float2, .defaultValue = {0.0f, 10.0f}, .whenSetting = 1, .whenOptions = 1u << 1, .label = "Count"},
			{.name = "Delay", .type = P::Float, .whenSetting = 2, .whenOptions = 1u << 0},
			{.name = "Delay Range", .type = P::Float2, .defaultValue = {0.0f, 1.0f}, .whenSetting = 2, .whenOptions = 1u << 1, .label = "Delay"},
		};

		constexpr GraphPortDefinition POSITION_SHAPE_INPUTS[] = {
			{.name = "Position", .type = P::Float3, .space = GraphPortSpace::Matrix},
			{.name = "Angles", .type = P::Float3},
			{.name = "Scale", .type = P::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}},
			{.name = "Radius", .type = P::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}},
			{.name = "Arc", .type = P::Float, .defaultValue = {360.0f, 0.0f, 0.0f, 0.0f}},
		};
		constexpr GraphPortDefinition COLLISION_SHAPE_INPUTS[] = {
			{.name = "Position", .type = P::Float3, .space = GraphPortSpace::Position},
			{.name = "Normal", .type = P::Float3, .defaultValue = {0.0f, 1.0f, 0.0f, 0.0f}, .space = GraphPortSpace::Direction},
			{.name = "Bounce", .type = P::Float, .defaultValue = {0.1f, 0.0f, 0.0f, 0.0f}},
			{.name = "Friction", .type = P::Float},
			{.name = "Lifetime Loss", .type = P::Float},
		};
		constexpr GraphPortDefinition COLLISION_SHAPE_OUTPUTS[] = {{"position", P::Float3}, {"velocity", P::Float3}, {"age", P::Float}};
		constexpr GraphPortDefinition COLLISION_DEPTH_INPUTS[] = {
			COLLISION_SHAPE_INPUTS[2],
			COLLISION_SHAPE_INPUTS[3],
			COLLISION_SHAPE_INPUTS[4],
		};
		constexpr GraphPortDefinition POSITION_DEPTH_INPUTS[] = {{.name = "Z Multiplier", .type = P::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition POSITION_DEPTH_OUTPUTS[] = {{"position", P::Float3}};

		constexpr GraphPortDefinition SAMPLE_MESH_INPUTS[] = {
			GraphEffectBlocks::MESH_INPUT[0],
			{.name = "Vertex", .type = P::Float},
			GraphEffectBlocks::MESH_TRANSFORM_INPUTS[0],
			GraphEffectBlocks::MESH_TRANSFORM_INPUTS[1],
			GraphEffectBlocks::MESH_TRANSFORM_INPUTS[2],
		};
		constexpr GraphPortDefinition SAMPLE_MESH_OUTPUTS[] = {{"Position", P::Float3}, {"Color", P::Float4}, {"Tex Coord 0", P::Float4}};
		constexpr GraphPortDefinition POSITION_MESH_INPUTS[] = {
			GraphEffectBlocks::MESH_INPUT[0],
			GraphEffectBlocks::MESH_TRANSFORM_INPUTS[0],
			GraphEffectBlocks::MESH_TRANSFORM_INPUTS[1],
			GraphEffectBlocks::MESH_TRANSFORM_INPUTS[2],
		};
		constexpr GraphPortDefinition POSITION_MESH_OUTPUTS[] = {{"position", P::Float3}, {"direction", P::Float3}};
		constexpr GraphPortDefinition POSITION_SEQUENTIAL_INPUTS[] = {
			{.name = "Offset Index", .type = P::Float},
			{.name = "Count", .type = P::Float, .defaultValue = {64.0f, 0.0f, 0.0f, 0.0f}},
			{.name = "Start", .type = P::Float3, .space = GraphPortSpace::Position},
			{.name = "End", .type = P::Float3, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .space = GraphPortSpace::Position},
		};

		constexpr GraphPreviewMode NONE = GraphPreviewMode::None;

		constexpr GraphNodeDefinition EFFECT_DEFINITIONS[] = {
			{GRAPH_EFFECT_EVENT, "Event", "Context", "trigger", {}, {}, EVENT_SETTINGS, nullptr, NONE, false, nullptr, {}, true},
			{GRAPH_EFFECT_SPAWN, "Spawn", "Context", "emit loop", SPAWN_INPUTS, {}, SPAWN_SETTINGS, nullptr, NONE, false, nullptr, SPAWN_FLOW, true},
			{GRAPH_EFFECT_GPU_EVENT, "GPU Event", "Context", "gpu event child spawn trigger sub emitter", GPU_EVENT_INPUTS, {}, {}, nullptr, NONE, false, nullptr, {}, true},
			{GRAPH_EFFECT_OUTPUT_EVENT, "Output Event", "Context", "script callback handler message death collision burst spawn", GPU_EVENT_INPUTS, {}, OUTPUT_EVENT_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_INITIALIZE, "Initialize Particle", "Context", "", INITIALIZE_INPUTS, {}, INITIALIZE_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, true},
			{GRAPH_EFFECT_UPDATE, "Update Particle", "Context", "", {}, {}, UPDATE_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, true},
			{GRAPH_EFFECT_OUTPUT, "Output Particle Primitive", "Context", "quad triangle octagon billboard", OUTPUT_INPUTS, {}, OUTPUT_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_OUTPUT_MESH, "Output Particle Mesh", "Context", "instanced model", OUTPUT_MESH_INPUTS, {}, OUTPUT_MESH_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_OUTPUT_POINT, "Output Particle Point", "Context", "pixel debug", OUTPUT_POINT_INPUTS, {}, OUTPUT_POINT_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_OUTPUT_LINE, "Output Particle Line", "Context", "debug", OUTPUT_LINE_INPUTS, {}, OUTPUT_LINE_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_OUTPUT_LIT, "Output Particle Lit Primitive", "Context", "lit quad triangle octagon billboard shaded", OUTPUT_LIT_INPUTS, {}, OUTPUT_LIT_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_OUTPUT_LIT_MESH, "Output Particle Lit Mesh", "Context", "lit instanced model shaded", OUTPUT_LIT_MESH_INPUTS, {}, OUTPUT_LIT_MESH_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_OUTPUT_NODE_SHADER, "Output Particle NodeShader Primitive", "Context", "node shader quad triangle octagon billboard custom", OUTPUT_NODE_SHADER_INPUTS, {}, OUTPUT_NODE_SHADER_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_OUTPUT_NODE_SHADER_MESH, "Output Particle NodeShader Mesh", "Context", "node shader instanced model custom", OUTPUT_NODE_SHADER_MESH_INPUTS, {}, OUTPUT_NODE_SHADER_MESH_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_OUTPUT_STRIP, "Output ParticleStrip Quad", "Context", "trail ribbon strip", OUTPUT_STRIP_INPUTS, {}, OUTPUT_STRIP_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP, "Output ParticleStrip NodeShader Quad", "Context", "node shader trail ribbon strip custom", OUTPUT_NODE_SHADER_STRIP_INPUTS, {}, OUTPUT_NODE_SHADER_STRIP_SETTINGS, nullptr, NONE, false, nullptr, IN_FLOW, false},
			{GRAPH_EFFECT_BLOCK_SUBGRAPH_CONTEXT, "Block Subgraph", "Context", "", {}, {}, BLOCK_SUBGRAPH_SETTINGS, nullptr, NONE, false, nullptr, {}, false},
			{GRAPH_EFFECT_SUBGRAPH_BLOCK, "Subgraph Block", "Block", "subgraph", {}, {}, {}, nullptr, NONE},
			{GRAPH_EFFECT_SYSTEM_SUBGRAPH, "Subgraph", "Context", "system subgraph", {}, {}, {}, nullptr, NONE, false, nullptr, {}, false},

			{GRAPH_EFFECT_CONSTANT_RATE, "Constant Spawn Rate", "Block", "emission", CONSTANT_RATE_INPUTS, {}, {}, nullptr, NONE},
			{GRAPH_EFFECT_BURST, "Burst", "Block", "single periodic", BURST_INPUTS, {}, BURST_SETTINGS, nullptr, NONE},
			{GRAPH_EFFECT_INCREMENT_STRIP_INDEX, "Increment Strip Index On Start", "Block", "strip trail ribbon index play start", INCREMENT_STRIP_INDEX_INPUTS, {}, {}, nullptr, NONE},
			{GRAPH_EFFECT_TRIGGER_EVENT, "Trigger Event", "Block", "gpu event child spawn sub emitter die collide rate distance",
			 GraphEffectBlocks::TRIGGER_EVENT_INPUTS, GraphEffectBlocks::TRIGGER_EVENT_OUTPUTS, GraphEffectBlocks::TRIGGER_EVENT_SETTINGS,
			 GraphEffectBlocks::TriggerEventBody, NONE},
			{GRAPH_EFFECT_SET_ATTRIBUTE, "Set Attribute", "Block", "write", SET_ATTRIBUTE_INPUTS, SET_ATTRIBUTE_OUTPUTS,
			 SET_ATTRIBUTE_SETTINGS, SetAttributeBody, NONE},
			{GRAPH_EFFECT_SET_SPAWN_EVENT, "Set SpawnEvent", "Block", "spawn event attribute", SET_SPAWN_EVENT_INPUTS, SET_ATTRIBUTE_OUTPUTS,
			 SET_SPAWN_EVENT_SETTINGS, SetSpawnEventBody, NONE},
			{GRAPH_EFFECT_ATTRIBUTE_FROM_CURVE, "Attribute from Curve", "Block", "curve gradient over life", SET_ATTRIBUTE_INPUTS, SET_ATTRIBUTE_OUTPUTS,
			 ATTRIBUTE_FROM_CURVE_SETTINGS, AttributeFromCurveBody, NONE},
			{GRAPH_EFFECT_POSITION_SHAPE, "Set Position Shape Sphere", "Block", "spawn emit sphere box cone cylinder torus circle line arc",
			 POSITION_SHAPE_INPUTS, SET_ATTRIBUTE_OUTPUTS, GraphEffectBlocks::POSITION_SHAPE_SETTINGS, GraphEffectBlocks::PositionShapeBody, NONE},
			{GRAPH_EFFECT_FORCE, "Force", "Block", "push wind", GraphEffectBlocks::FORCE_INPUTS, GraphEffectBlocks::VELOCITY_OUTPUT,
			 GraphEffectBlocks::FORCE_SETTINGS, GraphEffectBlocks::ForceBody, NONE},
			{GRAPH_EFFECT_GRAVITY, "Gravity", "Block", "fall weight", GraphEffectBlocks::GRAVITY_INPUTS, GraphEffectBlocks::VELOCITY_OUTPUT, {}, GraphEffectBlocks::GravityBody, NONE},
			{GRAPH_EFFECT_LINEAR_DRAG, "Linear Drag", "Block", "friction slow air resistance", GraphEffectBlocks::LINEAR_DRAG_INPUTS,
			 GraphEffectBlocks::VELOCITY_OUTPUT, GraphEffectBlocks::LINEAR_DRAG_SETTINGS, GraphEffectBlocks::LinearDragBody, NONE},
			{GRAPH_EFFECT_TURBULENCE, "Turbulence", "Block", "noise curl wind swirl", GraphEffectBlocks::TURBULENCE_INPUTS, GraphEffectBlocks::VELOCITY_OUTPUT,
			 GraphEffectBlocks::TURBULENCE_SETTINGS, GraphEffectBlocks::TurbulenceBody, NONE},
			{GRAPH_EFFECT_CONFORM_TO_SPHERE, "Attractor Shape Sphere", "Block", "conform attract magnet sphere", GraphEffectBlocks::CONFORM_TO_SPHERE_INPUTS, GraphEffectBlocks::VELOCITY_OUTPUT, {}, GraphEffectBlocks::ConformToSphereBody, NONE},
			{GRAPH_EFFECT_COLLISION_SHAPE, "Collision Shape Plane", "Block", "collide bounce collision kill trigger", COLLISION_SHAPE_INPUTS,
			 COLLISION_SHAPE_OUTPUTS, GraphEffectBlocks::COLLISION_SHAPE_SETTINGS, GraphEffectBlocks::CollisionShapeBody, NONE},
			{GRAPH_EFFECT_COLLISION_DEPTH, "Collision Depth Buffer", "Block", "collide bounce collision kill trigger depth buffer camera screen",
			 COLLISION_DEPTH_INPUTS, COLLISION_SHAPE_OUTPUTS, GraphEffectBlocks::COLLISION_DEPTH_SETTINGS, GraphEffectBlocks::CollisionDepthBody, NONE},
			{GRAPH_EFFECT_POSITION_DEPTH, "Set Position (Depth)", "Block", "spawn emit depth buffer camera screen surface", POSITION_DEPTH_INPUTS,
			 POSITION_DEPTH_OUTPUTS, GraphEffectBlocks::POSITION_DEPTH_SETTINGS, GraphEffectBlocks::PositionDepthBody, NONE},
			{GRAPH_EFFECT_ORIENT, "Orient: Face Camera Plane", "Block", "orientation billboard facing rotate look at", {}, GraphEffectBlocks::AXES_OUTPUTS, GraphEffectBlocks::ORIENT_SETTINGS, GraphEffectBlocks::OrientBody, NONE},
			{GRAPH_EFFECT_CAMERA_FADE, "Camera Fade", "Block", "fade near camera plane distance cull overdraw", GraphEffectBlocks::CAMERA_FADE_INPUTS,
			 GraphEffectBlocks::CAMERA_FADE_OUTPUTS, GraphEffectBlocks::CAMERA_FADE_SETTINGS, GraphEffectBlocks::CameraFadeBody, NONE},
			{GRAPH_EFFECT_FLIPBOOK_PLAYER, "Flipbook Player", "Block", "animation atlas frame rate sheet sprite subuv texture",
			 GraphEffectBlocks::FLIPBOOK_PLAYER_INPUTS, GraphEffectBlocks::TEX_INDEX_OUTPUT, GraphEffectBlocks::FLIPBOOK_PLAYER_SETTINGS,
			 GraphEffectBlocks::FlipbookPlayerBody, NONE},
			{GRAPH_EFFECT_VECTOR_FIELD_FORCE, "Vector Field Force", "Block", "vector field force volume wind vf texture3d", GraphEffectBlocks::VECTOR_FIELD_INPUTS,
			 GraphEffectBlocks::VELOCITY_OUTPUT, GraphEffectBlocks::VECTOR_FIELD_SETTINGS, GraphEffectBlocks::VectorFieldForceBody, NONE},
			{GRAPH_EFFECT_CONFORM_TO_SDF, "Attractor Shape Signed Distance Field", "Block", "conform attract magnet signed distance field sdf", GraphEffectBlocks::CONFORM_TO_SDF_INPUTS, GraphEffectBlocks::VELOCITY_OUTPUT, {}, GraphEffectBlocks::ConformToSDFBody, NONE},
			{GRAPH_EFFECT_POSITION_MESH, "Set Position (Mesh)", "Block", "spawn emit mesh model vertex edge surface triangle", POSITION_MESH_INPUTS,
			 POSITION_MESH_OUTPUTS, GraphEffectBlocks::POSITION_MESH_SETTINGS, GraphEffectBlocks::PositionMeshBody, NONE},
			{GRAPH_EFFECT_POSITION_SEQUENTIAL, "Add Position Sequential Line", "Block", "spawn emit sequence sequential order line circle grid ring",
			 POSITION_SEQUENTIAL_INPUTS, POSITION_MESH_OUTPUTS, GraphEffectBlocks::POSITION_SEQUENTIAL_SETTINGS, GraphEffectBlocks::PositionSequentialBody, NONE},
			{GRAPH_EFFECT_CUSTOM_HLSL_BLOCK, "Custom HLSL", "Block", "hlsl code shader custom function script", GraphCustomHLSL::BLOCK_INPUTS,
			 GraphCustomHLSL::BLOCK_OUTPUTS, GraphCustomHLSL::BLOCK_SETTINGS, GraphCustomHLSL::BlockBody, NONE},
			{GRAPH_EFFECT_CUSTOM_HLSL_OPERATOR, "Custom HLSL", "HLSL", "hlsl code shader custom function script", GraphCustomHLSL::OPERATOR_INPUTS,
			 GraphCustomHLSL::OPERATOR_OUTPUTS, GraphCustomHLSL::OPERATOR_SETTINGS, GraphCustomHLSL::OperatorBody, NONE},

			{GRAPH_EFFECT_GET_ATTRIBUTE, "Get Attribute: position", "Attribute", "read attribute", {}, SET_ATTRIBUTE_OUTPUTS, GET_ATTRIBUTE_SETTINGS, GetAttributeBody, NONE},
			{GRAPH_EFFECT_AGE_OVER_LIFETIME, "Age Over Lifetime", "Attribute", "normalized age life ratio", {}, AGE_OVER_LIFETIME_OUTPUTS, {}, AgeOverLifetimeBody, NONE},
			{GRAPH_EFFECT_RATIO_OVER_STRIP, "Get Ratio Over Strip [0..1]", "Attribute", "strip trail ribbon ratio place progress", {}, AGE_OVER_LIFETIME_OUTPUTS, {}, RatioOverStripBody, NONE},
			{GRAPH_EFFECT_RANDOM_NUMBER, "Random Number", "Random", "probability aleatory range", RANDOM_NUMBER_INPUTS, RANDOM_NUMBER_OUTPUTS,
			 RANDOM_NUMBER_SETTINGS, RandomNumberBody, NONE},
			{GRAPH_EFFECT_TOTAL_TIME, "Total Time", "BuiltIn", "clock elapsed", {}, TOTAL_TIME_OUTPUTS, {}, TotalTimeBody, NONE},
			{GRAPH_EFFECT_DELTA_TIME, "Delta Time", "BuiltIn", "frame step", {}, DELTA_TIME_OUTPUTS, {}, DeltaTimeBody, NONE},
			{GRAPH_EFFECT_SAMPLE_CURVE, "Sample Curve", "Sampling", "animation curve evaluate", SAMPLE_CURVE_INPUTS, SAMPLE_CURVE_OUTPUTS, {}, SampleCurveBody, NONE},
			{GRAPH_EFFECT_SAMPLE_TEXTURE_2D, "Sample Texture2D", "Sampling", "texture image read tex2d", SAMPLE_TEXTURE_2D_INPUTS, SAMPLE_TEXTURE_OUTPUTS, {}, SampleTexture2DBody, NONE},
			{GRAPH_EFFECT_SAMPLE_TEXTURE_2D_ARRAY, "Sample Texture2DArray", "Sampling", "texture array slice read", SAMPLE_TEXTURE_2D_ARRAY_INPUTS, SAMPLE_TEXTURE_OUTPUTS, {}, SampleTexture2DArrayBody, NONE},
			{GRAPH_EFFECT_SAMPLE_TEXTURE_3D, "Sample Texture3D", "Sampling", "texture volume read tex3d", SAMPLE_TEXTURE_3D_INPUTS, SAMPLE_TEXTURE_OUTPUTS, {}, SampleVolumeBody, NONE},
			{GRAPH_EFFECT_SAMPLE_TEXTURE_CUBE, "Sample TextureCube", "Sampling", "texture cubemap read", SAMPLE_TEXTURE_CUBE_INPUTS, SAMPLE_TEXTURE_OUTPUTS, {}, SampleVolumeBody, NONE},
			{GRAPH_EFFECT_SAMPLE_SDF, "Sample Signed Distance Field", "Sampling", "sdf distance field surface", GraphEffectBlocks::SAMPLE_SDF_INPUTS, GraphEffectBlocks::SAMPLE_SDF_OUTPUTS, {}, GraphEffectBlocks::SampleSDFBody, NONE},
			{GRAPH_EFFECT_SAMPLE_MESH, "Sample Mesh", "Sampling", "mesh model vertex edge surface triangle position normal tangent color uv", SAMPLE_MESH_INPUTS,
			 SAMPLE_MESH_OUTPUTS, GraphEffectBlocks::SAMPLE_MESH_SETTINGS, GraphEffectBlocks::SampleMeshBody, NONE},
			{GRAPH_EFFECT_SAMPLE_MESH_INDEX, "Sample Mesh Index", "Sampling", "mesh model index buffer triangle", GraphEffectBlocks::SAMPLE_MESH_INDEX_INPUTS, GraphEffectBlocks::SAMPLE_MESH_INDEX_OUTPUTS, {}, GraphEffectBlocks::SampleMeshIndexBody, NONE},
			{GRAPH_EFFECT_MESH_VERTEX_COUNT, "Get Mesh Vertex Count", "Sampling", "mesh model vertices count number", GraphEffectBlocks::MESH_INPUT, GraphEffectBlocks::MESH_COUNT_OUTPUTS, {}, GraphEffectBlocks::MeshVertexCountBody, NONE},
			{GRAPH_EFFECT_MESH_INDEX_COUNT, "Get Mesh Index Count", "Sampling", "mesh model indices count number", GraphEffectBlocks::MESH_INPUT, GraphEffectBlocks::MESH_COUNT_OUTPUTS, {}, GraphEffectBlocks::MeshIndexCountBody, NONE},
			{GRAPH_EFFECT_MESH_TRIANGLE_COUNT, "Get Mesh Triangle Count", "Sampling", "mesh model triangles faces count number", GraphEffectBlocks::MESH_INPUT, GraphEffectBlocks::MESH_COUNT_OUTPUTS, {}, GraphEffectBlocks::MeshTriangleCountBody, NONE},
			{GRAPH_EFFECT_NOISE, "Perlin Noise 2D", "Noise", "value perlin cellular fractal octave", NOISE_INPUTS, NOISE_OUTPUTS, NOISE_SETTINGS,
			 NoiseBody, NONE},
			{GRAPH_EFFECT_CURL_NOISE, "Perlin Curl Noise 2D", "Noise", "value perlin cellular turbulence swirl", NOISE_INPUTS, CURL_NOISE_OUTPUTS,
			 CURL_NOISE_SETTINGS, CurlNoiseBody, NONE},
		};

		u32 ChoiceOf(const GraphNode& node, std::string_view key, std::span<const std::string_view> options, u32 fallback) {
			for (const GraphSetting& setting : node.settings) {
				if (setting.key != key) {
					continue;
				}
				for (u32 i = 0; i < options.size(); ++i) {
					if (setting.value == "\"" + str(options[i]) + "\"") {
						return i;
					}
				}
				return static_cast<u32>(options.size());
			}
			return fallback;
		}

		bool ToggleOf(const GraphNode& node, std::string_view key, bool fallback) {
			for (const GraphSetting& setting : node.settings) {
				if (setting.key == key) {
					return setting.value == "true";
				}
			}
			return fallback;
		}

		f32 NumberOf(const GraphNode& node, std::string_view key, f32 fallback) {
			for (const GraphSetting& setting : node.settings) {
				if (setting.key == key) {
					char* end = nullptr;
					const f32 value = std::strtof(setting.value.c_str(), &end);
					return end != setting.value.c_str() ? value : fallback;
				}
			}
			return fallback;
		}

		GraphPortDefinition AttributePort(std::string_view name, const GraphEffectAttributeInfo& attribute) {
			GraphPortDefinition port;
			port.name = name;
			port.type = PortTypeOf(attribute.type);
			port.space = attribute.space;
			for (u32 i = 0; i < 4; ++i) {
				port.defaultValue[i] = attribute.defaultValue[i];
			}
			return port;
		}

		void AttributeOutput(const GraphEffectAttributeInfo& attribute, GraphBuiltDefinition& out) {
			GraphPortDefinition output;
			output.name = attribute.name;
			output.type = PortTypeOf(attribute.type);
			out.outputs.push_back(output);
		}

		void Finish(std::string_view def, str title, GraphBuiltDefinition& out) {
			GraphNodeDefinition& definition = out.definition;
			definition = *GraphNodeLibrary::Find(def);
			definition.name = out.strings.emplace_back(std::move(title));
			definition.inputs = out.inputs;
			definition.outputs = out.outputs;
		}

		str RandomLabel(u32 random) { return random == 0 ? str() : (random == 1 ? str(" Random (Per Component)") : str(" Random (Uniform)")); }

		void BuildSetAttribute(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 attribute = ChoiceOf(node, GRAPH_EFFECT_ATTRIBUTE_SETTING, WRITABLE_NAMES, 0);
			const u32 composition = ChoiceOf(node, "Composition", GRAPH_EFFECT_COMPOSITIONS, 0);
			const u32 source = ChoiceOf(node, "Source", GRAPH_EFFECT_SOURCES, 0);
			u32 random = ChoiceOf(node, "Random", GRAPH_EFFECT_RANDOMS, 0);
			if (attribute >= WRITABLE_COUNT || composition >= std::size(GRAPH_EFFECT_COMPOSITIONS) || source >= std::size(GRAPH_EFFECT_SOURCES) ||
				random >= std::size(GRAPH_EFFECT_RANDOMS)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const GraphEffectAttributeInfo& written = ATTRIBUTES[attribute];
			if (written.type == T::Bool && composition != 0) {
				out.error = "composes Boolean attribute '" + str(written.name) + "' by " + str(GRAPH_EFFECT_COMPOSITIONS[composition]) +
							" (a Boolean can only be Set)";
				return;
			}
			if (source == 1) {
				random = 0;
			}
			if (written.type == T::Bool && random != 0) {
				out.error = "draws Boolean attribute '" + str(written.name) + "' at random (a Boolean has no range)";
				return;
			}
			if (written.type == T::Float && random == 1) {
				random = 2;
			}
			if (source == 0 && random == 0) {
				out.inputs.push_back(AttributePort(out.strings.emplace_back(Capitalized(written.name)), written));
			} else if (source == 0) {
				out.inputs.push_back(AttributePort("A", written));
				out.inputs.push_back(AttributePort("B", written));
			}
			if (composition == 3) {
				GraphPortDefinition blend;
				blend.name = "Blend";
				blend.type = P::Float;
				blend.defaultValue[0] = 0.5f;
				out.inputs.push_back(blend);
			}
			AttributeOutput(written, out);
			str title = str(GRAPH_EFFECT_COMPOSITIONS[composition]) + " " + Capitalized(written.name);
			title += source == 1 ? str(" from Source") : RandomLabel(random);
			Finish(GRAPH_EFFECT_SET_ATTRIBUTE, std::move(title), out);
		}

		void BuildSetSpawnEvent(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 attribute = ChoiceOf(node, GRAPH_EFFECT_ATTRIBUTE_SETTING, WRITABLE_NAMES, 0);
			u32 random = ChoiceOf(node, "Random Mode", GRAPH_EFFECT_RANDOMS, 0);
			if (attribute >= WRITABLE_COUNT || random >= std::size(GRAPH_EFFECT_RANDOMS)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const GraphEffectAttributeInfo& written = ATTRIBUTES[attribute];
			if (written.type == T::Bool && random != 0) {
				out.error = "draws Boolean attribute '" + str(written.name) + "' at random (a Boolean has no range)";
				return;
			}
			if (written.type == T::Float && random == 1) {
				random = 2;
			}
			if (random == 0) {
				out.inputs.push_back(AttributePort(out.strings.emplace_back(Capitalized(written.name)), written));
			} else {
				out.inputs.push_back(AttributePort("Min", written));
				out.inputs.push_back(AttributePort("Max", written));
			}
			for (GraphPortDefinition& input : out.inputs) {
				input.space = GraphPortSpace::None;
			}
			AttributeOutput(written, out);
			Finish(GRAPH_EFFECT_SET_SPAWN_EVENT, "Set SpawnEvent " + Capitalized(written.name) + RandomLabel(random), out);
		}

		void BuildAttributeFromCurve(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 attribute = ChoiceOf(node, GRAPH_EFFECT_ATTRIBUTE_SETTING, WRITABLE_NAMES, 0);
			const u32 composition = ChoiceOf(node, "Composition", GRAPH_EFFECT_COMPOSITIONS, 0);
			const u32 alphaComposition = ChoiceOf(node, "Alpha Composition", GRAPH_EFFECT_COMPOSITIONS, 0);
			const u32 sampleMode = ChoiceOf(node, "Sample Mode", GRAPH_EFFECT_SAMPLE_MODES, 0);
			const u32 mode = ChoiceOf(node, "Mode", COMPUTE_MODES, 1);
			const u32 colorMode = ChoiceOf(node, "Color Mode", COLOR_MODES, 2);
			if (attribute >= WRITABLE_COUNT || composition >= std::size(GRAPH_EFFECT_COMPOSITIONS) ||
				alphaComposition >= std::size(GRAPH_EFFECT_COMPOSITIONS) || sampleMode >= std::size(GRAPH_EFFECT_SAMPLE_MODES) ||
				mode >= std::size(COMPUTE_MODES) || colorMode >= std::size(COLOR_MODES)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const GraphEffectAttributeInfo& written = ATTRIBUTES[attribute];
			if (written.type == T::Bool) {
				out.error = "reads Boolean attribute '" + str(written.name) + "' from a curve (a Boolean has none)";
				return;
			}
			const bool color = written.name == "color";
			const bool perComponent = mode == 1;
			const str& label = out.strings.emplace_back(Capitalized(written.name));
			GraphPortDefinition sampled;
			sampled.type = color && perComponent ? P::Gradient : P::Curve;
			sampled.defaultJson = color && perComponent ? BLOCK_GRADIENT : std::string_view();
			if (color || written.type == T::Float || !perComponent) {
				sampled.name = label;
				out.inputs.push_back(sampled);
			} else {
				for (std::string_view axis : {std::string_view("_x"), std::string_view("_y"), std::string_view("_z")}) {
					sampled.name = out.strings.emplace_back(label + str(axis));
					out.inputs.push_back(sampled);
				}
			}
			if (sampleMode == 1) {
				out.inputs.push_back({"Speed Range", P::Float2, {0.0f, 1.0f, 0.0f, 0.0f}});
			} else if (sampleMode == 3) {
				out.inputs.push_back({"Seed", P::Float});
			} else if (sampleMode == 4) {
				out.inputs.push_back({"Sample Time", P::Float});
			}
			if (composition == 3 || (color && alphaComposition == 3)) {
				out.inputs.push_back({"Blend", P::Float});
			}
			if (color) {
				if (colorMode != 1) {
					AttributeOutput(written, out);
				}
				if (colorMode != 0) {
					AttributeOutput(*GraphEffectAttributes::Find("alpha"), out);
				}
			} else {
				AttributeOutput(written, out);
			}
			Finish(GRAPH_EFFECT_ATTRIBUTE_FROM_CURVE, str(GRAPH_EFFECT_COMPOSITIONS[composition]) + " " + label + " " + str(GRAPH_EFFECT_SAMPLE_MODE_NAMES[sampleMode]),
				   out);
		}

		void BuildGetAttribute(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 attribute = ChoiceOf(node, GRAPH_EFFECT_ATTRIBUTE_SETTING, ATTRIBUTE_NAMES, 0);
			const u32 location = ChoiceOf(node, "Location", GRAPH_EFFECT_LOCATIONS, 0);
			if (attribute >= std::size(ATTRIBUTES) || location >= std::size(GRAPH_EFFECT_LOCATIONS)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			AttributeOutput(ATTRIBUTES[attribute], out);
			str title = "Get Attribute: " + str(ATTRIBUTES[attribute].name);
			if (location == 1) {
				title += " (Source)";
			}
			Finish(GRAPH_EFFECT_GET_ATTRIBUTE, std::move(title), out);
		}

		void BuildRandomNumber(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 type = ChoiceOf(node, "Type", RANDOM_TYPES, 0);
			if (type >= std::size(RANDOM_TYPES)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			constexpr P TYPES[] = {P::Float, P::Float2, P::Float3, P::Float4};
			for (const GraphPortDefinition& input : RANDOM_NUMBER_INPUTS) {
				GraphPortDefinition port = input;
				if (input.name != "Seed") {
					port.type = TYPES[type];
					for (u32 i = 1; i < 4; ++i) {
						port.defaultValue[i] = port.defaultValue[0];
					}
				}
				out.inputs.push_back(port);
			}
			GraphPortDefinition r = RANDOM_NUMBER_OUTPUTS[0];
			r.type = TYPES[type];
			out.outputs.push_back(r);
			Finish(GRAPH_EFFECT_RANDOM_NUMBER, type == 0 ? str("Random Number") : "Random Number (" + str(RANDOM_TYPES[type]) + ")", out);
		}

		// arc stored in degrees
		void BuildPositionShape(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 shape = ChoiceOf(node, "Shape", B::POSITION_SHAPES, 0);
			const u32 mode = ChoiceOf(node, "Position Mode", B::POSITION_MODES, 0);
			const u32 spawn = ChoiceOf(node, "Spawn Mode", B::SPAWN_MODES, 0);
			const u32 orientation = ChoiceOf(node, "Apply Orientation", B::ORIENTATIONS, 1);
			const u32 compositions[] = {ChoiceOf(node, "Composition Position", GRAPH_EFFECT_COMPOSITIONS, 0),
										ChoiceOf(node, "Composition Direction", GRAPH_EFFECT_COMPOSITIONS, 0),
										ChoiceOf(node, "Composition Axes", GRAPH_EFFECT_COMPOSITIONS, 0)};
			if (shape >= std::size(B::POSITION_SHAPES) || mode >= std::size(B::POSITION_MODES) || spawn >= std::size(B::SPAWN_MODES) ||
				ChoiceOf(node, "Height Mode", B::HEIGHT_MODES, 1) >= std::size(B::HEIGHT_MODES) || orientation >= std::size(B::ORIENTATIONS) ||
				std::any_of(std::begin(compositions), std::end(compositions), [](u32 composition) { return composition >= std::size(GRAPH_EFFECT_COMPOSITIONS); })) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const auto add = [&out](std::string_view name, P type, std::initializer_list<f32> value, GraphPortSpace space = GraphPortSpace::None) {
				GraphPortDefinition port;
				port.name = name;
				port.type = type;
				std::copy(value.begin(), value.end(), port.defaultValue);
				port.space = space;
				out.inputs.push_back(port);
			};
			const auto transform = [&](std::string_view position) {
				add(position, P::Float3, {0.0f, 0.0f, 0.0f}, GraphPortSpace::Matrix);
				add("Angles", P::Float3, {0.0f, 0.0f, 0.0f});
				add(position == "Center" ? "Size" : "Scale", P::Float3, {1.0f, 1.0f, 1.0f});
			};
			const bool custom = spawn == 1 && shape != B::OrientedBox;
			switch (shape) {
			case B::Sphere:
			case B::Circle:
				transform("Position");
				add("Radius", P::Float, {1.0f});
				add("Arc", P::Float, {360.0f});
				break;
			case B::OrientedBox:
				transform("Center");
				break;
			case B::Cone:
				transform("Position");
				add("Base Radius", P::Float, {1.0f});
				add("Top Radius", P::Float, {0.1f});
				add("Height", P::Float, {1.0f});
				add("Arc", P::Float, {360.0f});
				break;
			case B::Torus:
				transform("Position");
				add("Major Radius", P::Float, {1.0f});
				add("Minor Radius", P::Float, {0.1f});
				add("Arc", P::Float, {360.0f});
				break;
			case B::PositionSDF: {
				add("SDF", P::Texture3D, {});
				for (const GraphPortDefinition& port : B::FIELD_TRANSFORM_INPUTS) {
					out.inputs.push_back(port);
				}
				const f32 steps = NumberOf(node, "Projection Steps", 2.0f);
				if (!(steps >= 1.0f && steps <= static_cast<f32>(B::MAX_PROJECTION_STEPS))) {
					out.error = std::format("has a Projection Steps of {} (a field's placement unrolls 1 to {})", steps, B::MAX_PROJECTION_STEPS);
					return;
				}
				break;
			}
			default:
				add("Start", P::Float3, {0.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				add("End", P::Float3, {1.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				break;
			}
			if (custom) {
				if (shape == B::Sphere || shape == B::Cone || shape == B::Torus) {
					add("Height Sequencer", P::Float, {0.0f});
				}
				add(shape == B::Line ? "Line Sequencer" : "Arc Sequencer", P::Float, {0.0f});
			}
			if (shape != B::Line && mode >= 2) {
				add("Thickness", P::Float, {0.1f});
			}
			const bool direction = orientation == 1 || orientation == 3;
			const bool axes = orientation >= 2;
			if (compositions[0] == 3) {
				add("Blend Position", P::Float, {1.0f});
			}
			if (axes && compositions[2] == 3) {
				add("Blend Axes", P::Float, {1.0f});
			}
			if (direction && compositions[1] == 3) {
				add("Blend Direction", P::Float, {1.0f});
			}
			AttributeOutput(*GraphEffectAttributes::Find("position"), out);
			if (direction) {
				AttributeOutput(*GraphEffectAttributes::Find("direction"), out);
			}
			if (axes) {
				for (std::string_view axis : {"axisX", "axisY", "axisZ"}) {
					AttributeOutput(*GraphEffectAttributes::Find(axis), out);
				}
			}
			if (shape == B::PositionSDF && ToggleOf(node, "Kill Outliers", false)) {
				AttributeOutput(*GraphEffectAttributes::Find("alive"), out);
			}
			Finish(GRAPH_EFFECT_POSITION_SHAPE, str(GRAPH_EFFECT_COMPOSITIONS[compositions[0]]) + " Position Shape " + str(B::POSITION_SHAPE_NAMES[shape]), out);
		}

		void AddPort(GraphBuiltDefinition& out, std::string_view name, std::initializer_list<f32> value, GraphPortSpace space = GraphPortSpace::None,
					 P type = P::Float3) {
			GraphPortDefinition port;
			port.name = name;
			port.type = type;
			std::copy(value.begin(), value.end(), port.defaultValue);
			port.space = space;
			out.inputs.push_back(port);
		}

		void CollisionPorts(const GraphNode& node, GraphBuiltDefinition& out, u32 behavior, u32 radius, u32 attributes, bool shapeHasRadius) {
			namespace B = GraphEffectBlocks;
			const bool collides = behavior == B::BehaviorCollision;
			if (collides) {
				AddPort(out, "Bounce", {0.1f}, GraphPortSpace::None, P::Float);
				AddPort(out, "Friction", {0.0f}, GraphPortSpace::None, P::Float);
				if (ToggleOf(node, "Override Bounce Threshold", false)) {
					AddPort(out, "Bounce Speed Threshold", {1.0f}, GraphPortSpace::None, P::Float);
				}
				AddPort(out, "Lifetime Loss", {0.0f}, GraphPortSpace::None, P::Float);
			}
			if (radius == 2) {
				AddPort(out, "Particle Radius", {0.1f}, GraphPortSpace::None, P::Float);
				if (!shapeHasRadius) {
					out.inputs.back().label = "Radius";
				}
			}
			if (ToggleOf(node, "Rough Surface", false)) {
				AddPort(out, "Roughness", {0.0f}, GraphPortSpace::None, P::Float);
			}
			if (collides) {
				for (std::string_view written : {"position", "velocity", "age"}) {
					AttributeOutput(*GraphEffectAttributes::Find(written), out);
				}
			} else if (behavior == B::BehaviorKill) {
				AttributeOutput(*GraphEffectAttributes::Find("alive"), out);
			}
			if (attributes != 0) {
				for (std::string_view written : {"hasCollisionEvent", "collisionEventNormal", "collisionEventPosition", "collisionEventCount"}) {
					AttributeOutput(*GraphEffectAttributes::Find(written), out);
				}
			}
		}

		void BuildCollisionShape(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 shape = ChoiceOf(node, "Shape", B::COLLISION_SHAPES, B::CollidePlane);
			const u32 radius = ChoiceOf(node, "Radius Mode", B::RADIUS_MODES, 0);
			const u32 behavior = ChoiceOf(node, "Behavior", B::BEHAVIORS, B::BehaviorCollision);
			const u32 attributes = ChoiceOf(node, "Collision Attributes", B::COLLISION_ATTRIBUTES, 0);
			if (shape >= std::size(B::COLLISION_SHAPES) || radius >= std::size(B::RADIUS_MODES) ||
				ChoiceOf(node, "Mode", B::COLLISION_MODES, 0) >= std::size(B::COLLISION_MODES) || behavior >= std::size(B::BEHAVIORS) ||
				attributes >= std::size(B::COLLISION_ATTRIBUTES)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const auto add = [&out](std::string_view name, std::initializer_list<f32> value, GraphPortSpace space = GraphPortSpace::None, P type = P::Float3) {
				AddPort(out, name, value, space, type);
			};
			switch (shape) {
			case B::CollideSphere:
				add("Position", {0.0f, 0.0f, 0.0f}, GraphPortSpace::Matrix);
				add("Angles", {0.0f, 0.0f, 0.0f});
				add("Scale", {1.0f, 1.0f, 1.0f});
				add("Radius", {1.0f}, GraphPortSpace::None, P::Float);
				break;
			case B::CollideBox:
				add("Center", {0.0f, 0.0f, 0.0f}, GraphPortSpace::Matrix);
				add("Angles", {0.0f, 0.0f, 0.0f});
				add("Size", {1.0f, 1.0f, 1.0f});
				break;
			case B::CollideCone:
				add("Position", {0.0f, 0.0f, 0.0f}, GraphPortSpace::Matrix);
				add("Angles", {0.0f, 0.0f, 0.0f});
				add("Scale", {1.0f, 1.0f, 1.0f});
				add("Base Radius", {1.0f}, GraphPortSpace::None, P::Float);
				add("Top Radius", {0.1f}, GraphPortSpace::None, P::Float);
				add("Height", {1.0f}, GraphPortSpace::None, P::Float);
				break;
			case B::CollideSDF:
				add("Distance Field", {}, GraphPortSpace::None, P::Texture3D);
				for (const GraphPortDefinition& port : B::FIELD_TRANSFORM_INPUTS) {
					out.inputs.push_back(port);
				}
				break;
			default:
				add("Position", {0.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				add("Normal", {0.0f, 1.0f, 0.0f}, GraphPortSpace::Direction);
				break;
			}
			CollisionPorts(node, out, behavior, radius, attributes, shape == B::CollideSphere);
			Finish(GRAPH_EFFECT_COLLISION_SHAPE, str(B::BEHAVIOR_NAMES[behavior]) + " Shape " + str(B::COLLISION_SHAPE_NAMES[shape]), out);
		}

		void BuildCollisionDepth(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 thickness = ChoiceOf(node, "Surface Thickness", B::SURFACE_THICKNESSES, 0);
			const u32 radius = ChoiceOf(node, "Radius Mode", B::RADIUS_MODES, 0);
			const u32 behavior = ChoiceOf(node, "Behavior", B::BEHAVIORS, B::BehaviorCollision);
			const u32 attributes = ChoiceOf(node, "Collision Attributes", B::COLLISION_ATTRIBUTES, 0);
			if (thickness >= std::size(B::SURFACE_THICKNESSES) || radius >= std::size(B::RADIUS_MODES) || behavior >= std::size(B::BEHAVIORS) ||
				attributes >= std::size(B::COLLISION_ATTRIBUTES)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			CollisionPorts(node, out, behavior, radius, attributes, false);
			if (thickness == 1) {
				AddPort(out, "Surface Thickness", {1.0f}, GraphPortSpace::None, P::Float);
			}
			Finish(GRAPH_EFFECT_COLLISION_DEPTH, str(B::BEHAVIOR_NAMES[behavior]) + " Depth Buffer", out);
		}

		void BuildPositionDepth(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 mode = ChoiceOf(node, "Mode", B::DEPTH_POSITION_MODES, 0);
			const u32 cull = ChoiceOf(node, "Cull Mode", B::DEPTH_CULL_MODES, 0);
			const u32 composition = ChoiceOf(node, "Composition Position", GRAPH_EFFECT_COMPOSITIONS, 0);
			if (mode >= std::size(B::DEPTH_POSITION_MODES) || cull >= std::size(B::DEPTH_CULL_MODES) || composition >= std::size(GRAPH_EFFECT_COMPOSITIONS)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			AddPort(out, "Z Multiplier", {1.0f}, GraphPortSpace::None, P::Float);
			if (mode == B::DepthSequential) {
				AddPort(out, "Grid Step", {1.0f}, GraphPortSpace::None, P::Float);
			} else if (mode == B::DepthCustom) {
				AddPort(out, "UV Spawn", {0.0f, 0.0f}, GraphPortSpace::None, P::Float2);
			}
			if (cull == B::DepthCullRange) {
				AddPort(out, "Depth Range", {0.0f, 1.0f}, GraphPortSpace::None, P::Float2);
			}
			if (composition == 3) {
				AddPort(out, "Blend Position", {0.0f}, GraphPortSpace::None, P::Float);
			}
			AttributeOutput(*GraphEffectAttributes::Find("position"), out);
			if (cull != B::DepthCullNone) {
				AttributeOutput(*GraphEffectAttributes::Find("alive"), out);
			}
			Finish(GRAPH_EFFECT_POSITION_DEPTH, str(GRAPH_EFFECT_COMPOSITIONS[composition]) + " Position (Depth)", out);
		}

		void BuildOrient(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 mode = ChoiceOf(node, "Mode", B::ORIENT_MODES, 0);
			const u32 pair = ChoiceOf(node, "Axes", B::ORIENT_AXES, B::AxesZY);
			if (mode >= std::size(B::ORIENT_MODES) || pair >= std::size(B::ORIENT_AXES)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const auto add = [&out](std::string_view name, std::initializer_list<f32> value, GraphPortSpace space) {
				GraphPortDefinition port;
				port.name = name;
				port.type = P::Float3;
				std::copy(value.begin(), value.end(), port.defaultValue);
				port.space = space;
				out.inputs.push_back(port);
			};
			switch (mode) {
			case B::LookAtPosition:
				add("Position", {0.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				break;
			case B::LookAtLine:
				add("Start", {0.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				add("End", {-1.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				break;
			case B::OrientAdvanced: {
				constexpr std::string_view NAMES[] = {"Axis X", "Axis Y", "Axis Z"};
				constexpr u32 FIRST[] = {0, 1, 2, 1, 2, 0};
				constexpr u32 SECOND[] = {1, 2, 0, 0, 1, 2};
				for (const u32 axis : {FIRST[pair], SECOND[pair]}) {
					add(NAMES[axis], {axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f}, GraphPortSpace::Direction);
				}
				break;
			}
			case B::FixedAxis:
			case B::CustomY:
				add("Up", {0.0f, 1.0f, 0.0f}, GraphPortSpace::Direction);
				break;
			case B::CustomZ:
				add("Front", {0.0f, 0.0f, -1.0f}, GraphPortSpace::Direction);
				break;
			default:
				break;
			}
			out.outputs.assign(std::begin(B::AXES_OUTPUTS), std::end(B::AXES_OUTPUTS));
			Finish(GRAPH_EFFECT_ORIENT, "Orient: " + str(B::ORIENT_MODE_NAMES[mode]), out);
		}

		void BuildFlipbookPlayer(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 mode = ChoiceOf(node, "Mode", B::FLIPBOOK_MODES, 0);
			const u32 rate = ChoiceOf(node, "Frame Rate Mode", B::FRAME_RATE_MODES, 0);
			const u32 cycles = ChoiceOf(node, "Cycles Mode", B::CYCLES_MODES, 0);
			const u32 range = ChoiceOf(node, "Animation Range", B::ANIMATION_RANGES, 0);
			if (mode >= std::size(B::FLIPBOOK_MODES) || rate >= std::size(B::FRAME_RATE_MODES) || cycles >= std::size(B::CYCLES_MODES) ||
				range >= std::size(B::ANIMATION_RANGES)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const auto add = [&out](std::string_view name, P type, std::initializer_list<f32> value, std::string_view label = {}) {
				GraphPortDefinition port;
				port.name = name;
				port.type = type;
				std::copy(value.begin(), value.end(), port.defaultValue);
				port.label = label;
				out.inputs.push_back(port);
			};
			const bool byCycles = mode == B::ByCycles;
			if (!byCycles) {
				switch (rate) {
				case B::RateRandom:
					add("Frame Rate Range", P::Float2, {24.0f, 30.0f}, "Frame Rate");
					add("Seed", P::Float, {0.0f});
					break;
				case B::RateConstant:
					add("Frame Rate", P::Float, {24.0f});
					break;
				default: {
					AnimationCurve constant;
					for (const f32 time : {0.0f, 1.0f}) {
						Keyframe key;
						key.time = time;
						key.value = 24.0f;
						constant.keys.push_back(key);
					}
					add("Frame Rate Curve", P::Curve, {}, "Frame Rate");
					out.inputs.back().defaultJson = out.strings.emplace_back(GraphCurves::Write(constant));
					if (rate == B::RateBySpeed) {
						add("Speed Range", P::Float2, {0.0f, 1.0f});
					} else if (rate == B::RateCustom) {
						add("Sample Time", P::Float, {0.0f});
					}
					break;
				}
				}
			} else {
				if (cycles == B::CyclesConstant) {
					add("Cycles", P::Float, {1.0f});
				} else {
					add("Cycles Range", P::Float2, {1.0f, 3.0f}, "Cycles");
					add("Seed", P::Float, {0.0f});
				}
				add("Start Offset", P::Float, {0.0f});
				if (ToggleOf(node, "Custom Curve", false)) {
					add("Custom Curve", P::Curve, {});
				}
			}
			const bool ranged = byCycles || ToggleOf(node, "Use Custom Range", false);
			if (ranged && (range == B::FlipbookRow || range == B::FlipbookColumn)) {
				add("Index", P::Float, {0.0f});
			} else if (ranged && range == B::StartEndFrames) {
				add("Frame Range", P::Float2, {0.0f, 16.0f});
			}
			AttributeOutput(*GraphEffectAttributes::Find("texIndex"), out);
			if (ranged && range != B::EntireFlipbook) {
				AttributeOutput(*GraphEffectAttributes::Find("texIndexBlend"), out);
			}
			Finish(GRAPH_EFFECT_FLIPBOOK_PLAYER, "Flipbook Player", out);
		}

		void MeshPlacementPorts(GraphBuiltDefinition& out, u32 placement, u32 surface) {
			namespace B = GraphEffectBlocks;
			switch (placement) {
			case B::PlaceVertex:
				AddPort(out, "Vertex", {0.0f}, GraphPortSpace::None, P::Float);
				break;
			case B::PlaceEdge:
				AddPort(out, "Index", {0.0f}, GraphPortSpace::None, P::Float);
				AddPort(out, "Edge", {0.0f}, GraphPortSpace::None, P::Float);
				break;
			default:
				AddPort(out, "Triangle", {0.0f}, GraphPortSpace::None, P::Float);
				AddPort(out, surface == B::SurfaceUniform ? "Square" : "Barycentric", {0.0f, 0.0f}, GraphPortSpace::None, P::Float2);
				break;
			}
		}

		void BuildSampleMesh(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 mode = ChoiceOf(node, "Mode", B::ADDRESSING_MODES, B::AddressClamp);
			const u32 placement = ChoiceOf(node, "Placement Mode", B::MESH_PLACEMENTS, B::PlaceVertex);
			const u32 surface = ChoiceOf(node, "Surface Coordinates", B::SURFACE_COORDINATES, B::SurfaceUniform);
			if (mode >= std::size(B::ADDRESSING_MODES) || placement >= std::size(B::MESH_PLACEMENTS) || surface >= std::size(B::SURFACE_COORDINATES)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			out.inputs.push_back(B::MESH_INPUT[0]);
			MeshPlacementPorts(out, placement, surface);
			out.inputs.insert(out.inputs.end(), std::begin(B::MESH_TRANSFORM_INPUTS), std::end(B::MESH_TRANSFORM_INPUTS));
			constexpr P TYPES[] = {P::Float3, P::Float3, P::Float3, P::Float3, P::Float, P::Float4, P::Float4};
			for (u32 output = 0; output < B::MESH_OUTPUT_COUNT; ++output) {
				if (ToggleOf(node, B::SAMPLE_MESH_SETTINGS[output].key, B::SAMPLE_MESH_SETTINGS[output].defaultIndex != 0)) {
					out.outputs.push_back({B::MESH_OUTPUTS[output], TYPES[output]});
				}
			}
			Finish(GRAPH_EFFECT_SAMPLE_MESH, "Sample Mesh", out);
		}

		void BuildPositionMesh(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 spawn = ChoiceOf(node, "Spawn Mode", B::SPAWN_MODES, 0);
			const u32 placement = ChoiceOf(node, "Placement Mode", B::MESH_PLACEMENTS, B::PlaceVertex);
			const u32 surface = ChoiceOf(node, "Surface Coordinates", B::SURFACE_COORDINATES, B::SurfaceUniform);
			const u32 orientation = ChoiceOf(node, "Apply Orientation", B::ORIENTATIONS, 1);
			const u32 compositions[] = {ChoiceOf(node, "Composition Position", GRAPH_EFFECT_COMPOSITIONS, 0),
										ChoiceOf(node, "Composition Direction", GRAPH_EFFECT_COMPOSITIONS, 0)};
			const u32 axesComposition = ChoiceOf(node, "Composition Axes", B::AXES_COMPOSITIONS, 0);
			if (spawn >= std::size(B::SPAWN_MODES) || placement >= std::size(B::MESH_PLACEMENTS) || surface >= std::size(B::SURFACE_COORDINATES) ||
				ChoiceOf(node, "Mode", B::ADDRESSING_MODES, B::AddressClamp) >= std::size(B::ADDRESSING_MODES) || orientation >= std::size(B::ORIENTATIONS) ||
				axesComposition >= std::size(B::AXES_COMPOSITIONS) ||
				std::any_of(std::begin(compositions), std::end(compositions), [](u32 composition) { return composition >= std::size(GRAPH_EFFECT_COMPOSITIONS); })) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const bool direction = orientation == 1 || orientation == 3;
			const bool axes = orientation >= 2;
			if (compositions[0] == 3) {
				AddPort(out, "Blend Position", {1.0f}, GraphPortSpace::None, P::Float);
			}
			if (axes && axesComposition == 1) {
				AddPort(out, "Blend Axes", {1.0f}, GraphPortSpace::None, P::Float);
			}
			if (direction && compositions[1] == 3) {
				AddPort(out, "Blend Direction", {1.0f}, GraphPortSpace::None, P::Float);
			}
			out.inputs.push_back(B::MESH_INPUT[0]);
			if (spawn == 1) {
				MeshPlacementPorts(out, placement, surface);
			}
			out.inputs.insert(out.inputs.end(), std::begin(B::MESH_TRANSFORM_INPUTS), std::end(B::MESH_TRANSFORM_INPUTS));
			AttributeOutput(*GraphEffectAttributes::Find("position"), out);
			if (direction) {
				AttributeOutput(*GraphEffectAttributes::Find("direction"), out);
			}
			if (axes) {
				for (std::string_view axis : {"axisX", "axisY", "axisZ"}) {
					AttributeOutput(*GraphEffectAttributes::Find(axis), out);
				}
			}
			Finish(GRAPH_EFFECT_POSITION_MESH, str(GRAPH_EFFECT_COMPOSITIONS[compositions[0]]) + " Position (Mesh)", out);
		}

		void BuildPositionSequential(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 compositions[] = {ChoiceOf(node, "Composition Position", GRAPH_EFFECT_COMPOSITIONS, 1),
										ChoiceOf(node, "Composition Direction", GRAPH_EFFECT_COMPOSITIONS, 0),
										ChoiceOf(node, "Composition Target Position", GRAPH_EFFECT_COMPOSITIONS, 1)};
			const u32 shape = ChoiceOf(node, "Shape", B::SEQUENTIAL_SHAPES, B::SequentialLine);
			const u32 index = ChoiceOf(node, "Index", B::SEQUENTIAL_INDEX_MODES, 0);
			if (shape >= std::size(B::SEQUENTIAL_SHAPES) || index >= std::size(B::SEQUENTIAL_INDEX_MODES) ||
				ChoiceOf(node, "Mode", B::ADDRESSING_MODES, B::AddressClamp) >= std::size(B::ADDRESSING_MODES) ||
				std::any_of(std::begin(compositions), std::end(compositions), [](u32 composition) { return composition >= std::size(GRAPH_EFFECT_COMPOSITIONS); })) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const bool position = ToggleOf(node, "Write Position", true);
			const bool target = ToggleOf(node, "Write Target Position", false);
			if (index == 1) {
				AddPort(out, "Index", {0.0f}, GraphPortSpace::None, P::Float);
			}
			if (position) {
				AddPort(out, "Offset Index", {0.0f}, GraphPortSpace::None, P::Float);
				if (compositions[0] == 3) {
					AddPort(out, "Blend Position", {1.0f}, GraphPortSpace::None, P::Float);
				}
				if (compositions[1] == 3) {
					AddPort(out, "Blend Direction", {1.0f}, GraphPortSpace::None, P::Float);
				}
			}
			if (target) {
				AddPort(out, "Offset Target Index", {1.0f}, GraphPortSpace::None, P::Float);
				if (compositions[2] == 3) {
					AddPort(out, "Blend Target Position", {1.0f}, GraphPortSpace::None, P::Float);
				}
			}
			switch (shape) {
			case B::SequentialLine:
				AddPort(out, "Count", {64.0f}, GraphPortSpace::None, P::Float);
				AddPort(out, "Start", {0.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				AddPort(out, "End", {1.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				break;
			case B::SequentialCircle:
				AddPort(out, "Count", {64.0f}, GraphPortSpace::None, P::Float);
				AddPort(out, "Center", {0.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				AddPort(out, "Normal", {0.0f, 0.0f, 1.0f}, GraphPortSpace::Direction);
				AddPort(out, "Up", {0.0f, 1.0f, 0.0f}, GraphPortSpace::Direction);
				AddPort(out, "Radius", {1.0f}, GraphPortSpace::None, P::Float);
				break;
			default:
				for (std::string_view axis : {"Count X", "Count Y", "Count Z"}) {
					AddPort(out, axis, {8.0f}, GraphPortSpace::None, P::Float);
				}
				AddPort(out, "Origin", {0.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				AddPort(out, "Axis X", {1.0f, 0.0f, 0.0f}, GraphPortSpace::Vector);
				AddPort(out, "Axis Y", {0.0f, 1.0f, 0.0f}, GraphPortSpace::Vector);
				AddPort(out, "Axis Z", {0.0f, 0.0f, 1.0f}, GraphPortSpace::Vector);
				break;
			}
			if (position) {
				AttributeOutput(*GraphEffectAttributes::Find("position"), out);
				AttributeOutput(*GraphEffectAttributes::Find("direction"), out);
			}
			if (target) {
				AttributeOutput(*GraphEffectAttributes::Find("targetPosition"), out);
			}
			Finish(GRAPH_EFFECT_POSITION_SEQUENTIAL,
				   str(GRAPH_EFFECT_COMPOSITIONS[compositions[0]]) + " Position Sequential " + str(B::SEQUENTIAL_SHAPES[shape]), out);
		}

		void BuildTriggerEvent(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 mode = ChoiceOf(node, "Mode", GRAPH_EFFECT_TRIGGER_MODES, 0);
			if (mode >= std::size(GRAPH_EFFECT_TRIGGER_MODES)) {
				out.error = "has a Mode that isn't one of its options";
				return;
			}
			out.inputs.assign(std::begin(B::TRIGGER_EVENT_INPUTS), std::end(B::TRIGGER_EVENT_INPUTS));
			out.outputs.assign(std::begin(B::TRIGGER_EVENT_OUTPUTS), std::end(B::TRIGGER_EVENT_OUTPUTS));
			Finish(GRAPH_EFFECT_TRIGGER_EVENT, "Trigger Event " + str(B::TRIGGER_MODE_NAMES[mode]), out);
		}

		void BuildInitialize(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 type = ChoiceOf(node, "Data Type", GRAPH_EFFECT_DATA_TYPES, 0);
			if (type >= std::size(GRAPH_EFFECT_DATA_TYPES)) {
				out.error = "has a Data Type that isn't one of its options";
				return;
			}
			out.inputs.assign(std::begin(INITIALIZE_INPUTS), std::end(INITIALIZE_INPUTS));
			Finish(GRAPH_EFFECT_INITIALIZE, type == 1 ? "Initialize Particle Strip" : "Initialize Particle", out);
		}

		void BuildNoise(const GraphNode& node, bool curl, GraphBuiltDefinition& out) {
			const std::span<const std::string_view> dimensions = curl ? std::span<const std::string_view>(GRAPH_EFFECT_CURL_DIMENSIONS)
																	  : std::span<const std::string_view>(GRAPH_EFFECT_NOISE_DIMENSIONS);
			const u32 type = ChoiceOf(node, "Type", GRAPH_EFFECT_NOISE_TYPES, 1);
			const u32 dimension = ChoiceOf(node, "Dimensions", dimensions, curl ? 0 : 1);
			if (type >= std::size(GRAPH_EFFECT_NOISE_TYPES) || dimension >= dimensions.size()) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const u32 width = curl ? dimension + 2 : dimension + 1;
			constexpr P VECTORS[] = {P::Float, P::Float2, P::Float3};
			out.inputs.push_back({"Coordinate", VECTORS[width - 1]});
			out.inputs.insert(out.inputs.end(), std::begin(NOISE_COMMON_INPUTS), std::end(NOISE_COMMON_INPUTS));
			if (curl) {
				out.inputs.push_back({"Amplitude", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}});
				out.outputs.push_back({"Noise", VECTORS[width - 1]});
			} else {
				out.inputs.push_back({"Range", P::Float2, {-1.0f, 1.0f, 0.0f, 0.0f}});
				out.outputs.push_back({"Noise", P::Float});
				out.outputs.push_back({"Derivatives", VECTORS[width - 1]});
			}
			Finish(curl ? GRAPH_EFFECT_CURL_NOISE : GRAPH_EFFECT_NOISE,
				   std::format("{} {} {}D", GRAPH_EFFECT_NOISE_TYPES[type], curl ? "Curl Noise" : "Noise", width), out);
		}

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphEffectDefinitions() { return EFFECT_DEFINITIONS; }

	namespace GraphEffectAttributes {

		std::span<const GraphEffectAttributeInfo> All() { return ATTRIBUTES; }

		const GraphEffectAttributeInfo* Find(std::string_view name) {
			for (const GraphEffectAttributeInfo& attribute : ATTRIBUTES) {
				if (attribute.name == name) {
					return &attribute;
				}
			}
			return nullptr;
		}

		str CurrentBinding(std::string_view name) { return "Attribute/" + str(name); }

		str SourceBinding(std::string_view name) { return "SourceAttribute/" + str(name); }

		str RateCount(u32 node) { return str(GRAPH_EFFECT_RATE_COUNT) + "_" + std::to_string(node); }

		bool BuildsPerNode(std::string_view def) {
			return def == GRAPH_EFFECT_SET_ATTRIBUTE || def == GRAPH_EFFECT_SET_SPAWN_EVENT || def == GRAPH_EFFECT_GET_ATTRIBUTE ||
				   def == GRAPH_EFFECT_RANDOM_NUMBER || def == GRAPH_EFFECT_ATTRIBUTE_FROM_CURVE || def == GRAPH_EFFECT_NOISE ||
				   def == GRAPH_EFFECT_CURL_NOISE || def == GRAPH_EFFECT_POSITION_SHAPE || def == GRAPH_EFFECT_COLLISION_SHAPE ||
				   def == GRAPH_EFFECT_COLLISION_DEPTH || def == GRAPH_EFFECT_POSITION_DEPTH || def == GRAPH_EFFECT_ORIENT || def == GRAPH_EFFECT_FLIPBOOK_PLAYER || def == GRAPH_EFFECT_SAMPLE_MESH ||
				   def == GRAPH_EFFECT_POSITION_MESH || def == GRAPH_EFFECT_POSITION_SEQUENTIAL || def == GRAPH_EFFECT_INITIALIZE ||
				   def == GRAPH_EFFECT_TRIGGER_EVENT;
		}

		void Build(const GraphNode& node, GraphBuiltDefinition& out) {
			if (node.def == GRAPH_EFFECT_SET_ATTRIBUTE) {
				BuildSetAttribute(node, out);
			} else if (node.def == GRAPH_EFFECT_SET_SPAWN_EVENT) {
				BuildSetSpawnEvent(node, out);
			} else if (node.def == GRAPH_EFFECT_GET_ATTRIBUTE) {
				BuildGetAttribute(node, out);
			} else if (node.def == GRAPH_EFFECT_RANDOM_NUMBER) {
				BuildRandomNumber(node, out);
			} else if (node.def == GRAPH_EFFECT_ATTRIBUTE_FROM_CURVE) {
				BuildAttributeFromCurve(node, out);
			} else if (node.def == GRAPH_EFFECT_NOISE || node.def == GRAPH_EFFECT_CURL_NOISE) {
				BuildNoise(node, node.def == GRAPH_EFFECT_CURL_NOISE, out);
			} else if (node.def == GRAPH_EFFECT_POSITION_SHAPE) {
				BuildPositionShape(node, out);
			} else if (node.def == GRAPH_EFFECT_COLLISION_SHAPE) {
				BuildCollisionShape(node, out);
			} else if (node.def == GRAPH_EFFECT_COLLISION_DEPTH) {
				BuildCollisionDepth(node, out);
			} else if (node.def == GRAPH_EFFECT_POSITION_DEPTH) {
				BuildPositionDepth(node, out);
			} else if (node.def == GRAPH_EFFECT_ORIENT) {
				BuildOrient(node, out);
			} else if (node.def == GRAPH_EFFECT_FLIPBOOK_PLAYER) {
				BuildFlipbookPlayer(node, out);
			} else if (node.def == GRAPH_EFFECT_SAMPLE_MESH) {
				BuildSampleMesh(node, out);
			} else if (node.def == GRAPH_EFFECT_POSITION_MESH) {
				BuildPositionMesh(node, out);
			} else if (node.def == GRAPH_EFFECT_POSITION_SEQUENTIAL) {
				BuildPositionSequential(node, out);
			} else if (node.def == GRAPH_EFFECT_INITIALIZE) {
				BuildInitialize(node, out);
			} else if (node.def == GRAPH_EFFECT_TRIGGER_EVENT) {
				BuildTriggerEvent(node, out);
			} else {
				out.error = "is no node an effect builds";
			}
		}

	} // namespace GraphEffectAttributes

	namespace GraphEffectShading {

		constexpr std::string_view DEFAULT_DOCUMENT = R"({
	"_type": "NodeShader",
	"version": 1,
	"nextId": 15,
	"settings": {"target":"unlit","blend":"alpha","cull":"off","depth":"test_only","supportEffects":true},
	"properties": [
		{"id":1,"name":"BaseMap","reference":"_BaseMap","type":"Texture2D"},
		{"id":2,"name":"Color","reference":"_Color","type":"Color","default":[1,1,1,1]}
	],
	"nodes": [
		{"id":10,"def":"Input/Property","x":-480,"y":0,"settings":{"property":1}},
		{"id":11,"def":"Input/Property","x":-480,"y":-120,"settings":{"property":2}},
		{"id":12,"def":"Input/SampleTexture2D","x":-320,"y":0,"in":{"Texture":{"from":[10,"Out"]}}},
		{"id":13,"def":"Math/Multiply","x":-120,"y":-40,"in":{"A":{"from":[11,"Out"]},"B":{"from":[12,"RGBA"]}}},
		{"id":14,"def":"Channel/Swizzle","x":40,"y":40,"settings":{"Mask":"w"},"in":{"In":{"from":[13,"Out"]}}}
	],
	"contexts": [
		{"id":3,"def":"Context/Vertex","x":300,"y":-200,"blocks":[
			{"id":4,"def":"Block/Position"},
			{"id":5,"def":"Block/Normal"},
			{"id":6,"def":"Block/Tangent"}
		]},
		{"id":7,"def":"Context/Fragment","x":300,"y":0,"blocks":[
			{"id":8,"def":"Block/BaseColor","in":{"Value":{"from":[13,"Out"]}}},
			{"id":9,"def":"Block/Alpha","in":{"Value":{"from":[14,"Out"]}}}
		]}
	],
	"groups": [],
	"notes": []
}
)";

		str DocumentSetting(const GraphDocument& document, std::string_view key) {
			for (const GraphSetting& setting : document.settings) {
				if (setting.key == key) {
					const str value = setting.value;
					return value.size() >= 2 && value.front() == '"' && value.back() == '"' ? value.substr(1, value.size() - 2) : str();
				}
			}
			return {};
		}

		Held HeldNodeShader(const GraphNode& output, Guid& asset) {
			asset = Guid{};
			for (const GraphSetting& setting : output.settings) {
				if (setting.key != GRAPH_EFFECT_NODE_SHADER_SETTING) {
					continue;
				}
				const str& value = setting.value;
				if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
					asset = Guid::FromString(value.substr(1, value.size() - 2));
				}
				return asset.IsValid() ? Held::Asset : Held::None;
			}
			return Held::Default;
		}

		std::string_view DefaultDocumentText() { return DEFAULT_DOCUMENT; }

		bool IsNodeShaderOutput(std::string_view def) {
			return def == GRAPH_EFFECT_OUTPUT_NODE_SHADER || def == GRAPH_EFFECT_OUTPUT_NODE_SHADER_MESH || def == GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP;
		}

		void Build(const GraphNode& node, const GraphDocument* graph, GraphBuiltDefinition& out) {
			const GraphNodeDefinition& table = *GraphNodeLibrary::Find(node.def);
			out.inputs.assign(table.inputs.begin(), table.inputs.end());
			out.settings.assign(table.settings.begin(), table.settings.end());
			const auto named = [](std::span<GraphSettingDefinition> settings, std::string_view key) -> GraphSettingDefinition* {
				for (GraphSettingDefinition& setting : settings) {
					if (setting.key == key) {
						return &setting;
					}
				}
				return nullptr;
			};
			if (graph) {
				const str blend = DocumentSetting(*graph, "blend");
				const bool sorts = blend == "alpha" || blend == "premultiplied";
				const bool blended = !blend.empty() && blend != "opaque";
				for (std::string_view key : {std::string_view("Sort Mode"), std::string_view("Revert Sorting")}) {
					if (GraphSettingDefinition* setting = named(out.settings, key); setting && !sorts) {
						setting->whenOptions = 1u << 2;
					}
				}
				for (GraphPortDefinition& port : out.inputs) {
					if (port.name == std::string_view("Alpha Threshold") && !blended) {
						port.orSetting = GRAPH_PORT_ALWAYS;
					}
				}
				const str target = DocumentSetting(*graph, "target");
				if (target.empty() || target == "lit" || target == "customLighting") {
					out.settings.push_back({.key = "Receive Shadows", .kind = S::Toggle, .defaultIndex = 1});
				}

				Vector<GraphPropertyInfo> properties{GetMemoryResource(MemTag::Editor)};
				Vector<GraphDiagnostic> ignored{GetMemoryResource(MemTag::Editor)};
				GraphProperties::ParseBlackboard(*graph, properties, ignored);
				for (const GraphPropertyInfo& property : properties) {
					if (GraphProperties::IsKeyword(property.kind)) {
						const str& name = property.name.empty() ? property.reference : property.name;
						if (named(out.settings, name) || name == "Receive Shadows") {
							continue;
						}
						GraphSettingDefinition setting;
						setting.key = out.strings.emplace_back(name);
						if (property.kind == GraphPropertyKind::BooleanKeyword) {
							setting.kind = S::Toggle;
							setting.defaultIndex = property.defaultValue[0] != 0.0f ? 1u : 0u;
						} else {
							Vector<std::string_view>& options = out.options.emplace_back();
							for (const GraphKeywordEntry& entry : property.entries) {
								options.push_back(out.strings.emplace_back(entry.name));
							}
							setting.kind = S::Choice;
							setting.options = options;
							setting.defaultIndex = static_cast<u32>(property.defaultValue[0]);
						}
						out.settings.push_back(setting);
						continue;
					}
					if (property.hidden) {
						continue;
					}
					if (std::any_of(out.inputs.begin(), out.inputs.end(), [&](const GraphPortDefinition& port) { return port.name == property.reference; })) {
						continue;
					}
					GraphPortDefinition port;
					port.name = out.strings.emplace_back(property.reference);
					port.type = GraphSubgraphs::InputPortType(property.kind);
					for (u32 i = 0; i < 4; ++i) {
						port.defaultValue[i] = property.defaultValue[i];
					}
					if (GraphProperties::IsTexture(property.kind)) {
						const u32 kind = property.kind == GraphPropertyKind::Texture2D		  ? 0u
										 : property.kind == GraphPropertyKind::Texture2DArray ? 1u
										 : property.kind == GraphPropertyKind::Texture3D	  ? 2u
																							  : 3u;
						port.binding = GRAPH_EFFECT_NODE_SHADER_TEXTURE_BINDINGS[kind];
					}
					out.inputs.push_back(port);
				}
			}
			out.definition = table;
			out.definition.inputs = out.inputs;
			out.definition.settings = out.settings;
		}

	} // namespace GraphEffectShading

} // namespace Shalltear
