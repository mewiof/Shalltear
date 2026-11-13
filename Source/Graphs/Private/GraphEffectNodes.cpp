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

		constexpr GraphEffectAttributeInfo Attributes[] = {
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
			while (count < std::size(Attributes) && !Attributes[count].readOnly) {
				++count;
			}
			return count;
		}();

		constexpr std::array<std::string_view, std::size(Attributes)> AttributeNames = [] {
			std::array<std::string_view, std::size(Attributes)> names{};
			for (size_t i = 0; i < std::size(Attributes); ++i) {
				names[i] = Attributes[i].name;
			}
			return names;
		}();
		constexpr std::span<const std::string_view> WritableNames{AttributeNames.data(), WRITABLE_COUNT};

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

		constexpr GraphSettingDefinition SetAttributeSettings[] = {
			{GraphEffectAttributeSetting, S::Choice, WritableNames},
			{"Composition", S::Choice, GraphEffectCompositions},
			{"Source", S::Choice, GraphEffectSources},
			{"Random", S::Choice, GraphEffectRandoms},
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
				value = RandomBetween(c, c.In(0), c.In(1), random == 1, GraphEffectRandomParticle, b.Scalar(0.0f), NodeSalt(c));
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

		constexpr GraphPortDefinition SetAttributeInputs[] = {{"Position", P::Float3}};
		constexpr GraphPortDefinition SetAttributeOutputs[] = {{"position", P::Float3}};

		constexpr GraphSettingDefinition SetSpawnEventSettings[] = {
			{GraphEffectAttributeSetting, S::Choice, WritableNames},
			{"Random Mode", S::Choice, GraphEffectRandoms},
		};

		void SetSpawnEventBody(GraphBodyContext& c) {
			const u32 random = c.Setting(1);
			c.outputs[0] = random == 0 ? c.In(0) : RandomBetween(c, c.In(0), c.In(1), random == 1, GraphEffectRandomEffect, c.builder.Scalar(0.0f), NodeSalt(c));
		}

		constexpr GraphPortDefinition SetSpawnEventInputs[] = {{"Position", P::Float3}};

		constexpr u32 AttributeOptions(bool (*keep)(const GraphEffectAttributeInfo&)) {
			u32 mask = 0;
			for (size_t i = 0; i < WRITABLE_COUNT; ++i) {
				mask |= keep(Attributes[i]) ? 1u << i : 0u;
			}
			return mask;
		}
		constexpr u32 COLOR_OPTION = AttributeOptions([](const GraphEffectAttributeInfo& a) { return a.name == "color"; });
		constexpr u32 VECTOR_OPTIONS = AttributeOptions([](const GraphEffectAttributeInfo& a) { return a.type == T::Float3; });
		static_assert(WRITABLE_COUNT <= 32, "a setting's whenOptions holds a bit per Attribute option");

		constexpr std::string_view ComputeModes[] = {"Uniform", "PerComponent"};
		constexpr std::string_view ColorModes[] = {"Color", "Alpha", "ColorAndAlpha"};
		constexpr GraphSettingDefinition AttributeFromCurveSettings[] = {
			{GraphEffectAttributeSetting, S::Choice, WritableNames},
			{"Composition", S::Choice, GraphEffectCompositions},
			{.key = "Alpha Composition", .kind = S::Choice, .options = GraphEffectCompositions, .whenSetting = 0, .whenOptions = COLOR_OPTION},
			{.key = "Sample Mode", .kind = S::Choice, .options = GraphEffectSampleModes, .inGraph = true},
			{.key = "Mode", .kind = S::Choice, .options = ComputeModes, .defaultIndex = 1, .whenSetting = 0, .whenOptions = VECTOR_OPTIONS},
			{.key = "Color Mode", .kind = S::Choice, .options = ColorModes, .defaultIndex = 2, .inGraph = true, .whenSetting = 0, .whenOptions = COLOR_OPTION},
		};
		constexpr u32 ALPHA_COMPOSITION_SETTING = 2;
		constexpr u32 SAMPLE_MODE_SETTING = 3;
		constexpr u32 MODE_SETTING = 4;

		constexpr std::string_view BlockGradient = R"({"alphas":[[0,0],[1,0.1],[0.8,0.8],[0,1]],"colors":[[1,1,1,0],[1,1,1,1]],"mode":"Blend"})";

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
			const GraphEffectAttributeInfo& written = Attributes[c.Setting(0)];
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
				t = b.Random(c.Binding(GraphEffectRandomParticle), b.Scalar(0.0f), NodeSalt(c));
				break;
			case 3:
				t = b.Random(c.Binding(GraphEffectRandomParticleConstant), input("Seed"), 0);
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

		constexpr GraphSettingDefinition GetAttributeSettings[] = {
			{GraphEffectAttributeSetting, S::Choice, AttributeNames},
			{"Location", S::Choice, GraphEffectLocations, 0, {}, {}, true},
		};

		void GetAttributeBody(GraphBodyContext& c) {
			const std::string_view attribute = c.definition->outputs[0].name;
			c.outputs[0] = c.Binding(c.Setting(1) == 1 ? GraphEffectAttributes::SourceBinding(attribute) : GraphEffectAttributes::CurrentBinding(attribute));
		}

		void AgeOverLifetimeBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Divide(c.Binding(GraphEffectAttributes::CurrentBinding("age")),
											c.Binding(GraphEffectAttributes::CurrentBinding("lifetime")));
		}
		constexpr GraphPortDefinition AgeOverLifetimeOutputs[] = {{"t", P::Float}};

		void RatioOverStripBody(GraphBodyContext& c) {
			GraphProgramBuilder& b = c.builder;
			c.outputs[0] = b.Divide(c.Binding(GraphEffectAttributes::CurrentBinding("particleIndexInStrip")),
									b.Subtract(c.Binding(GraphEffectAttributes::CurrentBinding("particleCountInStrip")), b.Scalar(1.0f)));
		}

		constexpr GraphPortDefinition IncrementStripIndexInputs[] = {{"Strip Max Count", P::Float, {8.0f, 0.0f, 0.0f, 0.0f}}};

		void TotalTimeBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Swizzle(c.Binding("TimeParameters"), "x"); }
		void DeltaTimeBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Swizzle(c.Binding("TimeParameters"), "w"); }
		constexpr GraphPortDefinition TotalTimeOutputs[] = {{"totalTime", P::Float}};
		constexpr GraphPortDefinition DeltaTimeOutputs[] = {{"deltaTime", P::Float}};

		constexpr std::string_view RandomTypes[] = {"Float", "Vector2", "Vector3", "Vector4"};
		constexpr std::string_view SeedModes[] = {"PerParticle", "PerEffectComponent"};
		constexpr GraphSettingDefinition RandomNumberSettings[] = {
			{"Type", S::Choice, RandomTypes},
			{"Seed", S::Choice, SeedModes, 0, {}, {}, true},
			{"Constant", S::Toggle, {}, 1, {}, {}, true},
		};
		constexpr u32 SEED_SETTING = 1;
		constexpr u32 CONSTANT_SETTING = 2;

		void RandomNumberBody(GraphBodyContext& c) {
			const bool perParticle = c.Setting(SEED_SETTING) == 0;
			const bool constant = c.Setting(CONSTANT_SETTING) != 0;
			std::string_view stream;
			if (constant) {
				stream = perParticle ? GraphEffectRandomParticleConstant : GraphEffectRandomEffectConstant;
			} else {
				stream = perParticle ? GraphEffectRandomParticle : GraphEffectRandomEffect;
			}
			const GraphValueId seed = constant ? c.In(2) : c.builder.Scalar(0.0f);
			c.outputs[0] = RandomBetween(c, c.In(0), c.In(1), true, stream, seed, constant ? 0u : NodeSalt(c));
		}

		constexpr GraphPortDefinition RandomNumberInputs[] = {
			{"Min", P::Float, {0.0f, 0.0f, 0.0f, 0.0f}},
			{"Max", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}},
			{.name = "Seed", .type = P::Float, .whenSetting = CONSTANT_SETTING, .whenOptions = 1u << 1},
		};
		constexpr GraphPortDefinition RandomNumberOutputs[] = {{"r", P::Float}};

		void SampleCurveBody(GraphBodyContext& c) {
			c.outputs[0] = GraphCurves::Sample(c.builder, c.builder.ConstantOf(c.In(0)), c.In(1));
		}

		constexpr GraphPortDefinition SampleCurveInputs[] = {{"Curve", P::Curve}, {"Time", P::Float}};
		constexpr GraphPortDefinition SampleCurveOutputs[] = {{"s", P::Float}};

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

		constexpr GraphPortDefinition SampleTexture2DInputs[] = {{"Texture", P::Texture2D}, {"UV", P::Float2}, {"Mip Level", P::Float}};
		constexpr GraphPortDefinition SampleTexture2DArrayInputs[] = {
			{"Texture", P::Texture2DArray}, {"UV", P::Float2}, {"Slice", P::Float}, {"Mip Level", P::Float}};
		constexpr GraphPortDefinition SampleTexture3DInputs[] = {{"Texture", P::Texture3D}, {"UVW", P::Float3}, {"Mip Level", P::Float}};
		constexpr GraphPortDefinition SampleTextureCubeInputs[] = {{"Texture", P::Cubemap}, {"UVW", P::Float3}, {"Mip Level", P::Float}};
		constexpr GraphPortDefinition SampleTextureOutputs[] = {{"s", P::Float4}};

		constexpr GraphSettingDefinition NoiseSettings[] = {
			{"Type", S::Choice, GraphEffectNoiseTypes, 1, {}, {}, true},
			{"Dimensions", S::Choice, GraphEffectNoiseDimensions, 1, {}, {}, true},
		};
		constexpr GraphSettingDefinition CurlNoiseSettings[] = {
			{"Type", S::Choice, GraphEffectNoiseTypes, 1, {}, {}, true},
			{"Dimensions", S::Choice, GraphEffectCurlDimensions, 0, {}, {}, true},
		};

		constexpr GraphPortDefinition NoiseCommonInputs[] = {
			{"Frequency", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}},
			{"Octaves", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}},
			{"Roughness", P::Float, {0.5f, 0.0f, 0.0f, 0.0f}},
			{"Lacunarity", P::Float, {2.0f, 0.0f, 0.0f, 0.0f}},
		};
		constexpr GraphPortDefinition NoiseInputs[] = {{"Coordinate", P::Float2}};
		constexpr GraphPortDefinition NoiseOutputs[] = {{"Noise", P::Float}, {"Derivatives", P::Float2}};
		constexpr GraphPortDefinition CurlNoiseOutputs[] = {{"Noise", P::Float2}};

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
			constexpr std::string_view Derivatives[] = {"y", "yz", "yzw"};
			const u32 dimension = GraphTypes::VectorWidth(b.TypeOf(result)) - 1;
			c.outputs[1] = b.Multiply(b.Swizzle(result, Derivatives[dimension - 1]), width);
		}

		void CurlNoiseBody(GraphBodyContext& c) {
			GraphProgramBuilder& b = c.builder;
			const auto type = static_cast<GraphEffectNoise>(c.Setting(0));
			c.outputs[0] = b.Multiply(b.EffectNoise(GraphOp::CurlNoise, type, c.In(0), NoiseParameters(c), c.In(2)), c.In(5));
		}

		constexpr std::string_view SpawnFlow[] = {GraphEffectStart, GraphEffectStop};
		constexpr std::string_view InFlow[] = {GraphEffectIn};

		constexpr GraphSettingDefinition EventSettings[] = {{"Event Name", S::Text, {}, 0, GraphEffectOnPlay, {}, true}};

		constexpr std::string_view LoopModes[] = {"Infinite", "Constant", "Random"};
		constexpr std::string_view DelayModes[] = {"None", "Constant", "Random"};
		constexpr GraphSettingDefinition SpawnSettings[] = {
			{"Loop Duration", S::Choice, LoopModes},
			{"Loop Count", S::Choice, LoopModes},
			{"Delay Before Loop", S::Choice, DelayModes},
			{"Delay After Loop", S::Choice, DelayModes},
		};
		constexpr u32 CONSTANT_OPTION = 1u << 1;
		constexpr u32 RANDOM_OPTION = 1u << 2;
		constexpr GraphPortDefinition SpawnInputs[] = {
			{.name = "Loop Duration", .type = P::Float, .defaultValue = {0.1f}, .whenSetting = 0, .whenOptions = CONSTANT_OPTION},
			{.name = "Loop Duration Range", .type = P::Float2, .defaultValue = {1.0f, 3.0f}, .whenSetting = 0, .whenOptions = RANDOM_OPTION, .label = "Loop Duration"},
			{.name = "Loop Count", .type = P::Float, .defaultValue = {1.0f}, .whenSetting = 1, .whenOptions = CONSTANT_OPTION},
			{.name = "Loop Count Range", .type = P::Float2, .defaultValue = {1.0f, 3.0f}, .whenSetting = 1, .whenOptions = RANDOM_OPTION, .label = "Loop Count"},
			{.name = "Delay Before Loop", .type = P::Float, .defaultValue = {0.1f}, .whenSetting = 2, .whenOptions = CONSTANT_OPTION},
			{.name = "Delay Before Loop Range", .type = P::Float2, .defaultValue = {0.1f, 0.3f}, .whenSetting = 2, .whenOptions = RANDOM_OPTION, .label = "Delay Before Loop"},
			{.name = "Delay After Loop", .type = P::Float, .defaultValue = {0.1f}, .whenSetting = 3, .whenOptions = CONSTANT_OPTION},
			{.name = "Delay After Loop Range", .type = P::Float2, .defaultValue = {0.1f, 0.3f}, .whenSetting = 3, .whenOptions = RANDOM_OPTION, .label = "Delay After Loop"},
		};

		constexpr std::string_view Spaces[] = {"Local", "World"};
		constexpr u32 PARTICLE_DATA = 1u << 0;
		constexpr u32 STRIP_DATA = 1u << 1;
		constexpr GraphSettingDefinition InitializeSettings[] = {
			{.key = "Capacity", .kind = S::Number, .defaultValue = {128.0f, 0.0f, 0.0f, 0.0f}, .inGraph = true, .whenSetting = 2, .whenOptions = PARTICLE_DATA},
			{"Space", S::Choice, Spaces, 0, {}, {}, true},
			{.key = "Data Type", .kind = S::Choice, .options = GraphEffectDataTypes},
			{.key = "Strip Capacity", .kind = S::Number, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .inGraph = true, .whenSetting = 2, .whenOptions = STRIP_DATA},
			{.key = "Particle Per Strip Count", .kind = S::Number, .defaultValue = {128.0f, 0.0f, 0.0f, 0.0f}, .inGraph = true, .whenSetting = 2, .whenOptions = STRIP_DATA},
			{.key = GraphEffectSimulationTarget, .kind = S::Choice, .options = GraphEffectSimulationTargets},
			{.key = GraphEffectSystemName, .kind = S::Text, .defaultText = GraphEffectDefaultSystemName, .inGraph = true},
			{.key = GraphEffectBoundsSettingMode, .kind = S::Choice, .options = GraphEffectBoundsModes, .inGraph = true},
		};
		constexpr u32 AUTHORED_BOUNDS = (1u << GRAPH_EFFECT_BOUNDS_RECORDED) | (1u << GRAPH_EFFECT_BOUNDS_MANUAL);
		constexpr u32 PADDED_BOUNDS = (1u << GRAPH_EFFECT_BOUNDS_RECORDED) | (1u << GRAPH_EFFECT_BOUNDS_AUTOMATIC);
		constexpr GraphPortDefinition InitializeInputs[] = {
			{.name = GraphEffectBoundsCenter, .type = P::Float3, .whenSetting = 7, .whenOptions = AUTHORED_BOUNDS, .space = GraphPortSpace::Position},
			{.name = GraphEffectBoundsSize, .type = P::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}, .whenSetting = 7, .whenOptions = AUTHORED_BOUNDS},
			{.name = GraphEffectBoundsPadding, .type = P::Float3, .whenSetting = 7, .whenOptions = PADDED_BOUNDS},
			{.name = "Strip Index", .type = P::Float, .whenSetting = 2, .whenOptions = STRIP_DATA}};

		constexpr GraphSettingDefinition UpdateSettings[] = {
			{"Update Position", S::Toggle, {}, 1}, {"Age Particles", S::Toggle, {}, 1}, {"Reap Particles", S::Toggle, {}, 1}};

		constexpr GraphSettingDefinition BlockSubgraphSettings[] = {
			{GraphEffectSuitableContextsSetting, S::Choice, GraphEffectSuitableContexts, GRAPH_EFFECT_DEFAULT_SUITABLE_CONTEXTS, {}, {}, true}};

		constexpr GraphPortDefinition GPUEventInputs[] = {{.name = GraphEffectEvt, .type = P::GPUEvent}};
		constexpr GraphSettingDefinition OutputEventSettings[] = {
			{.key = "Event Name", .kind = S::Text, .defaultText = GraphEffectDefaultOutputEventName, .inGraph = true}};

		constexpr std::string_view BlendModes[] = {"Additive", "Alpha", "AlphaPremultiplied", "Opaque"};
		constexpr std::string_view BaseColorMaps[] = {"None", "Color", "Alpha", "ColorAndAlpha"};
		constexpr std::string_view UVModes[] = {"Default", "Flipbook", "ScaleAndBias"};
		constexpr u32 ANY_MAP = (1u << 1) | (1u << 2) | (1u << 3);
		constexpr u32 BLENDED = (1u << 0) | (1u << 1) | (1u << 2);
		constexpr u32 SORT_ON = 1u << 2;
		constexpr u32 SORT_AUTO = 1u << 0;
		constexpr u32 NOT_COMMUTATIVE = (1u << 1) | (1u << 2);
		constexpr GraphSettingDefinition ClippingSetting() { return {.key = "Use Alpha Clipping", .kind = S::Toggle, .inGraph = true}; }
		constexpr GraphSettingDefinition SoftSetting(u8 blend) {
			return {.key = "Use Soft Particle", .kind = S::Toggle, .inGraph = true, .whenSetting = blend, .whenOptions = BLENDED};
		}
		constexpr GraphSettingDefinition SortSetting() { return {.key = "Sort", .kind = S::Choice, .options = GraphEffectSorts}; }
		constexpr GraphSettingDefinition SortModeSetting(u8 sort, u8 blend) {
			return {.key = "Sort Mode", .kind = S::Choice, .options = GraphEffectSortModes, .whenSetting = sort, .whenOptions = SORT_ON, .orSetting = sort, .orOptions = SORT_AUTO, .orAlsoSetting = blend, .orAlsoOptions = NOT_COMMUTATIVE};
		}
		constexpr GraphSettingDefinition RevertSortingSetting(u8 sort, u8 blend) {
			return {.key = "Revert Sorting", .kind = S::Toggle, .whenSetting = sort, .whenOptions = SORT_ON, .orSetting = sort, .orOptions = SORT_AUTO, .orAlsoSetting = blend, .orAlsoOptions = NOT_COMMUTATIVE};
		}
		constexpr GraphSettingDefinition CastShadowsSetting() { return {.key = "Cast Shadows", .kind = S::Toggle}; }
		constexpr GraphSettingDefinition CullModeSetting() { return {.key = GraphEffectCullMode, .kind = S::Choice, .options = GraphEffectCullModes}; }
		constexpr GraphSettingDefinition ZWriteModeSetting() {
			return {.key = GraphEffectZWriteMode, .kind = S::Choice, .options = GraphEffectZWriteModes};
		}
		constexpr GraphSettingDefinition ZTestModeSetting() { return {.key = GraphEffectZTestMode, .kind = S::Choice, .options = GraphEffectZTestModes}; }
		constexpr GraphPortDefinition AlphaThresholdPort(u8 clipping, u8 casts, u8 blend) {
			return {.name = "Alpha Threshold", .type = P::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}, .whenSetting = clipping, .whenOptions = 1u << 1, .orSetting = casts, .orOptions = 1u << 1, .orAlsoSetting = blend, .orAlsoOptions = BLENDED};
		}
		constexpr GraphPortDefinition SoftFadePort(u8 soft) {
			return {.name = "Soft Particle Fade Distance", .type = P::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = soft, .whenOptions = 1u << 1};
		}
		constexpr GraphPortDefinition SortKeyPort(u8 mode) { return {.name = "Sort Key", .type = P::Float, .whenSetting = mode, .whenOptions = 1u << 3}; }

		constexpr GraphSettingDefinition OutputSettings[] = {
			{"Primitive Type", S::Choice, GraphEffectPrimitives, 1},
			{"Blend Mode", S::Choice, BlendModes, 1, {}, {}, true},
			{.key = "Use Base Color Map", .kind = S::Choice, .options = BaseColorMaps, .defaultIndex = 3},
			{.key = "UV Mode", .kind = S::Choice, .options = UVModes, .inGraph = true, .whenSetting = 2, .whenOptions = ANY_MAP},
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
		constexpr GraphPortDefinition OutputInputs[] = {
			{.name = "Main Texture", .type = P::Texture2D, .binding = GraphEffectMainTextureBinding, .whenSetting = 2, .whenOptions = ANY_MAP},
			{.name = "Flip Book Size", .type = P::Float2, .defaultValue = {4.0f, 4.0f, 0.0f, 0.0f}, .whenSetting = 3, .whenOptions = 1u << 1},
			{.name = "UV Scale", .type = P::Float2, .defaultValue = {1.0f, 1.0f, 0.0f, 0.0f}, .whenSetting = 3, .whenOptions = 1u << 2},
			{.name = "UV Bias", .type = P::Float2, .whenSetting = 3, .whenOptions = 1u << 2},
			{.name = "Crop Factor", .type = P::Float, .defaultValue = {0.293f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = 1u << 2},
			AlphaThresholdPort(5, 10, 1),
			SoftFadePort(6),
			SortKeyPort(8),
		};
		constexpr GraphSettingDefinition OutputMeshSettings[] = {
			{"Blend Mode", S::Choice, BlendModes, 1, {}, {}, true},
			{.key = "Use Base Color Map", .kind = S::Choice, .options = BaseColorMaps, .defaultIndex = 3},
			{.key = "UV Mode", .kind = S::Choice, .options = UVModes, .inGraph = true, .whenSetting = 1, .whenOptions = ANY_MAP},
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
		constexpr GraphPortDefinition OutputMeshInputs[] = {
			GraphEffectBlocks::MeshInput[0],
			{.name = "Main Texture", .type = P::Texture2D, .binding = GraphEffectMainTextureBinding, .whenSetting = 1, .whenOptions = ANY_MAP},
			{.name = "Flip Book Size", .type = P::Float2, .defaultValue = {4.0f, 4.0f, 0.0f, 0.0f}, .whenSetting = 2, .whenOptions = 1u << 1},
			{.name = "UV Scale", .type = P::Float2, .defaultValue = {1.0f, 1.0f, 0.0f, 0.0f}, .whenSetting = 2, .whenOptions = 1u << 2},
			{.name = "UV Bias", .type = P::Float2, .whenSetting = 2, .whenOptions = 1u << 2},
			AlphaThresholdPort(4, 9, 0),
			SoftFadePort(5),
			SortKeyPort(7),
		};
		constexpr GraphSettingDefinition OutputPointSettings[] = {
			{"Blend Mode", S::Choice, BlendModes, 1, {}, {}, true},
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
		constexpr GraphPortDefinition OutputPointInputs[] = {AlphaThresholdPort(1, 6, 0), SoftFadePort(2), SortKeyPort(4)};
		constexpr GraphSettingDefinition OutputLineSettings[] = {
			{"Blend Mode", S::Choice, BlendModes, 1, {}, {}, true},
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
		constexpr GraphPortDefinition OutputLineInputs[] = {
			{.name = "Target Offset", .type = P::Float3, .defaultValue = {0.0f, 1.0f, 0.0f, 0.0f}, .whenSetting = 1, .whenOptions = 1u << 1},
			AlphaThresholdPort(3, 8, 0),
			SoftFadePort(4),
			SortKeyPort(6),
		};

		constexpr u32 COLOR_NOT_BASE = (1u << 0) | (1u << 2);
		constexpr u32 COLOR_NOT_EMISSIVE = (1u << 0) | (1u << 1);
		constexpr GraphSettingDefinition OutputLitSettings[] = {
			{"Primitive Type", S::Choice, GraphEffectPrimitives, 1},
			{"Blend Mode", S::Choice, BlendModes, 1, {}, {}, true},
			{.key = "Use Base Color Map", .kind = S::Choice, .options = BaseColorMaps, .defaultIndex = 3},
			{.key = "UV Mode", .kind = S::Choice, .options = UVModes, .inGraph = true, .whenSetting = 2, .whenOptions = ANY_MAP},
			{.key = "Flipbook Blend Frames", .kind = S::Toggle, .inGraph = true, .whenSetting = 3, .whenOptions = 1u << 1},
			ClippingSetting(),
			SoftSetting(1),
			SortSetting(),
			SortModeSetting(7, 1),
			RevertSortingSetting(7, 1),
			CastShadowsSetting(),
			{.key = "Smoothness Source", .kind = S::Choice, .options = GraphEffectSmoothnessSources},
			{.key = "Use Occlusion Map", .kind = S::Toggle},
			{.key = "Use Metallic Map", .kind = S::Toggle},
			{.key = "Use Normal Map", .kind = S::Toggle},
			{.key = "Use Emissive Map", .kind = S::Toggle},
			{.key = "Color Mode", .kind = S::Choice, .options = GraphEffectColorModes, .defaultIndex = 1},
			{.key = "Use Emissive", .kind = S::Toggle, .whenSetting = 16, .whenOptions = COLOR_NOT_EMISSIVE},
			{.key = "Double Sided", .kind = S::Toggle},
			{.key = "Receive Shadows", .kind = S::Toggle, .defaultIndex = 1},
			{.key = "Normal Bending", .kind = S::Toggle},
			CullModeSetting(),
			ZWriteModeSetting(),
			ZTestModeSetting(),
		};
		constexpr GraphPortDefinition LitMapPort(u32 map, u8 when) {
			return {.name = GraphEffectLitMapPorts[map], .type = P::Texture2D, .binding = GraphEffectLitMapBindings[map], .whenSetting = when, .whenOptions = 1u << 1};
		}
		constexpr GraphPortDefinition OutputLitInputs[] = {
			{.name = "Base Color Map", .type = P::Texture2D, .binding = GraphEffectMainTextureBinding, .whenSetting = 2, .whenOptions = ANY_MAP},
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
		constexpr GraphSettingDefinition OutputLitMeshSettings[] = {
			{"Blend Mode", S::Choice, BlendModes, 1, {}, {}, true},
			{.key = "Use Base Color Map", .kind = S::Choice, .options = BaseColorMaps, .defaultIndex = 3},
			{.key = "UV Mode", .kind = S::Choice, .options = UVModes, .inGraph = true, .whenSetting = 1, .whenOptions = ANY_MAP},
			{.key = "Flipbook Blend Frames", .kind = S::Toggle, .inGraph = true, .whenSetting = 2, .whenOptions = 1u << 1},
			ClippingSetting(),
			SoftSetting(0),
			SortSetting(),
			SortModeSetting(6, 0),
			RevertSortingSetting(6, 0),
			CastShadowsSetting(),
			{.key = "Smoothness Source", .kind = S::Choice, .options = GraphEffectSmoothnessSources},
			{.key = "Use Occlusion Map", .kind = S::Toggle},
			{.key = "Use Metallic Map", .kind = S::Toggle},
			{.key = "Use Normal Map", .kind = S::Toggle},
			{.key = "Use Emissive Map", .kind = S::Toggle},
			{.key = "Color Mode", .kind = S::Choice, .options = GraphEffectColorModes, .defaultIndex = 1},
			{.key = "Use Emissive", .kind = S::Toggle, .whenSetting = 15, .whenOptions = COLOR_NOT_EMISSIVE},
			{.key = "Double Sided", .kind = S::Toggle},
			{.key = "Receive Shadows", .kind = S::Toggle, .defaultIndex = 1},
			CullModeSetting(),
			ZWriteModeSetting(),
			ZTestModeSetting(),
		};
		constexpr GraphPortDefinition OutputLitMeshInputs[] = {
			GraphEffectBlocks::MeshInput[0],
			{.name = "Base Color Map", .type = P::Texture2D, .binding = GraphEffectMainTextureBinding, .whenSetting = 1, .whenOptions = ANY_MAP},
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

		constexpr std::string_view NodeShaderExtensions[] = {".slshader"};
		constexpr GraphSettingDefinition NodeShaderSetting() {
			return {.key = GraphEffectNodeShaderSetting, .kind = S::Asset, .options = NodeShaderExtensions, .defaultText = "EffectDefault", .inGraph = true};
		}
		constexpr GraphSettingDefinition GraphSortModeSetting(u8 sort) {
			return {.key = "Sort Mode", .kind = S::Choice, .options = GraphEffectSortModes, .whenSetting = sort, .whenOptions = SORT_ON | SORT_AUTO};
		}
		constexpr GraphSettingDefinition GraphRevertSortingSetting(u8 sort) {
			return {.key = "Revert Sorting", .kind = S::Toggle, .whenSetting = sort, .whenOptions = SORT_ON | SORT_AUTO};
		}
		constexpr GraphPortDefinition GraphAlphaThresholdPort(u8 clipping, u8 casts) {
			return {.name = "Alpha Threshold", .type = P::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}, .whenSetting = clipping, .whenOptions = 1u << 1, .orSetting = casts, .orOptions = 1u << 1};
		}
		constexpr GraphSettingDefinition OutputNodeShaderSettings[] = {
			NodeShaderSetting(),
			{"Primitive Type", S::Choice, GraphEffectPrimitives, 1},
			ClippingSetting(),
			SortSetting(),
			GraphSortModeSetting(3),
			GraphRevertSortingSetting(3),
			CastShadowsSetting(),
		};
		constexpr GraphPortDefinition OutputNodeShaderInputs[] = {
			{.name = "Crop Factor", .type = P::Float, .defaultValue = {0.293f, 0.0f, 0.0f, 0.0f}, .whenSetting = 1, .whenOptions = 1u << 2},
			GraphAlphaThresholdPort(2, 6),
			SortKeyPort(4),
		};
		constexpr GraphSettingDefinition OutputNodeShaderMeshSettings[] = {
			NodeShaderSetting(),
			ClippingSetting(),
			SortSetting(),
			GraphSortModeSetting(2),
			GraphRevertSortingSetting(2),
			CastShadowsSetting(),
		};
		constexpr GraphPortDefinition OutputNodeShaderMeshInputs[] = {
			GraphEffectBlocks::MeshInput[0],
			GraphAlphaThresholdPort(1, 5),
			SortKeyPort(3),
		};

		constexpr u32 CUSTOM_TILING = 1u << 2;
		constexpr GraphSettingDefinition OutputStripSettings[] = {
			{"Blend Mode", S::Choice, BlendModes, 1, {}, {}, true},
			{.key = "Use Base Color Map", .kind = S::Choice, .options = BaseColorMaps, .defaultIndex = 3},
			{.key = "UV Mode", .kind = S::Choice, .options = UVModes, .inGraph = true, .whenSetting = 1, .whenOptions = ANY_MAP},
			{.key = "Flipbook Blend Frames", .kind = S::Toggle, .inGraph = true, .whenSetting = 2, .whenOptions = 1u << 1},
			{.key = "Tiling Mode", .kind = S::Choice, .options = GraphEffectStripTilingModes, .inGraph = true},
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
		constexpr GraphPortDefinition OutputStripInputs[] = {
			{.name = "Main Texture", .type = P::Texture2D, .binding = GraphEffectMainTextureBinding, .whenSetting = 1, .whenOptions = ANY_MAP},
			{.name = "Flip Book Size", .type = P::Float2, .defaultValue = {4.0f, 4.0f, 0.0f, 0.0f}, .whenSetting = 2, .whenOptions = 1u << 1},
			{.name = "UV Scale", .type = P::Float2, .defaultValue = {1.0f, 1.0f, 0.0f, 0.0f}, .whenSetting = 2, .whenOptions = 1u << 2},
			{.name = "UV Bias", .type = P::Float2, .whenSetting = 2, .whenOptions = 1u << 2},
			TexCoordPort(4),
			AlphaThresholdPort(6, 11, 0),
			SoftFadePort(7),
			SortKeyPort(9),
		};
		constexpr GraphSettingDefinition OutputNodeShaderStripSettings[] = {
			NodeShaderSetting(),
			{.key = "Tiling Mode", .kind = S::Choice, .options = GraphEffectStripTilingModes, .inGraph = true},
			{.key = "Swap UV", .kind = S::Toggle, .inGraph = true},
			ClippingSetting(),
			SortSetting(),
			GraphSortModeSetting(4),
			GraphRevertSortingSetting(4),
			CastShadowsSetting(),
		};
		constexpr GraphPortDefinition OutputNodeShaderStripInputs[] = {
			TexCoordPort(1),
			GraphAlphaThresholdPort(3, 7),
			SortKeyPort(5),
		};

		constexpr GraphPortDefinition ConstantRateInputs[] = {{"Rate", P::Float, {10.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr std::string_view Repeats[] = {"Single", "Periodic"};
		constexpr std::string_view RangeModes[] = {"Constant", "Random"};
		constexpr GraphSettingDefinition BurstSettings[] = {
			{"Repeat", S::Choice, Repeats},
			{"Spawn Mode", S::Choice, RangeModes, 0, {}, {}, true},
			{"Delay Mode", S::Choice, RangeModes, 0, {}, {}, true},
		};
		constexpr GraphPortDefinition BurstInputs[] = {
			{.name = "Count", .type = P::Float, .whenSetting = 1, .whenOptions = 1u << 0},
			{.name = "Count Range", .type = P::Float2, .defaultValue = {0.0f, 10.0f}, .whenSetting = 1, .whenOptions = 1u << 1, .label = "Count"},
			{.name = "Delay", .type = P::Float, .whenSetting = 2, .whenOptions = 1u << 0},
			{.name = "Delay Range", .type = P::Float2, .defaultValue = {0.0f, 1.0f}, .whenSetting = 2, .whenOptions = 1u << 1, .label = "Delay"},
		};

		constexpr GraphPortDefinition PositionShapeInputs[] = {
			{.name = "Position", .type = P::Float3, .space = GraphPortSpace::Matrix},
			{.name = "Angles", .type = P::Float3},
			{.name = "Scale", .type = P::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}},
			{.name = "Radius", .type = P::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}},
			{.name = "Arc", .type = P::Float, .defaultValue = {360.0f, 0.0f, 0.0f, 0.0f}},
		};
		constexpr GraphPortDefinition CollisionShapeInputs[] = {
			{.name = "Position", .type = P::Float3, .space = GraphPortSpace::Position},
			{.name = "Normal", .type = P::Float3, .defaultValue = {0.0f, 1.0f, 0.0f, 0.0f}, .space = GraphPortSpace::Direction},
			{.name = "Bounce", .type = P::Float, .defaultValue = {0.1f, 0.0f, 0.0f, 0.0f}},
			{.name = "Friction", .type = P::Float},
			{.name = "Lifetime Loss", .type = P::Float},
		};
		constexpr GraphPortDefinition CollisionShapeOutputs[] = {{"position", P::Float3}, {"velocity", P::Float3}, {"age", P::Float}};
		constexpr GraphPortDefinition CollisionDepthInputs[] = {
			CollisionShapeInputs[2],
			CollisionShapeInputs[3],
			CollisionShapeInputs[4],
		};
		constexpr GraphPortDefinition PositionDepthInputs[] = {{.name = "Z Multiplier", .type = P::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition PositionDepthOutputs[] = {{"position", P::Float3}};

		constexpr GraphPortDefinition SampleMeshInputs[] = {
			GraphEffectBlocks::MeshInput[0],
			{.name = "Vertex", .type = P::Float},
			GraphEffectBlocks::MeshTransformInputs[0],
			GraphEffectBlocks::MeshTransformInputs[1],
			GraphEffectBlocks::MeshTransformInputs[2],
		};
		constexpr GraphPortDefinition SampleMeshOutputs[] = {{"Position", P::Float3}, {"Color", P::Float4}, {"Tex Coord 0", P::Float4}};
		constexpr GraphPortDefinition PositionMeshInputs[] = {
			GraphEffectBlocks::MeshInput[0],
			GraphEffectBlocks::MeshTransformInputs[0],
			GraphEffectBlocks::MeshTransformInputs[1],
			GraphEffectBlocks::MeshTransformInputs[2],
		};
		constexpr GraphPortDefinition PositionMeshOutputs[] = {{"position", P::Float3}, {"direction", P::Float3}};
		constexpr GraphPortDefinition PositionSequentialInputs[] = {
			{.name = "Offset Index", .type = P::Float},
			{.name = "Count", .type = P::Float, .defaultValue = {64.0f, 0.0f, 0.0f, 0.0f}},
			{.name = "Start", .type = P::Float3, .space = GraphPortSpace::Position},
			{.name = "End", .type = P::Float3, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .space = GraphPortSpace::Position},
		};

		constexpr GraphPreviewMode NONE = GraphPreviewMode::None;

		constexpr GraphNodeDefinition EffectDefinitions[] = {
			{GraphEffectEvent, "Event", "Context", "trigger", {}, {}, EventSettings, nullptr, NONE, false, nullptr, {}, true},
			{GraphEffectSpawn, "Spawn", "Context", "emit loop", SpawnInputs, {}, SpawnSettings, nullptr, NONE, false, nullptr, SpawnFlow, true},
			{GraphEffectGPUEvent, "GPU Event", "Context", "gpu event child spawn trigger sub emitter", GPUEventInputs, {}, {}, nullptr, NONE, false, nullptr, {}, true},
			{GraphEffectOutputEventDefinition, "Output Event", "Context", "script callback handler message death collision burst spawn", GPUEventInputs, {}, OutputEventSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectInitialize, "Initialize Particle", "Context", "", InitializeInputs, {}, InitializeSettings, nullptr, NONE, false, nullptr, InFlow, true},
			{GraphEffectUpdate, "Update Particle", "Context", "", {}, {}, UpdateSettings, nullptr, NONE, false, nullptr, InFlow, true},
			{GraphEffectOutputDefinition, "Output Particle Primitive", "Context", "quad triangle octagon billboard", OutputInputs, {}, OutputSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectOutputMesh, "Output Particle Mesh", "Context", "instanced model", OutputMeshInputs, {}, OutputMeshSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectOutputPoint, "Output Particle Point", "Context", "pixel debug", OutputPointInputs, {}, OutputPointSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectOutputLine, "Output Particle Line", "Context", "debug", OutputLineInputs, {}, OutputLineSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectOutputLit, "Output Particle Lit Primitive", "Context", "lit quad triangle octagon billboard shaded", OutputLitInputs, {}, OutputLitSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectOutputLitMesh, "Output Particle Lit Mesh", "Context", "lit instanced model shaded", OutputLitMeshInputs, {}, OutputLitMeshSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectOutputNodeShader, "Output Particle NodeShader Primitive", "Context", "node shader quad triangle octagon billboard custom", OutputNodeShaderInputs, {}, OutputNodeShaderSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectOutputNodeShaderMesh, "Output Particle NodeShader Mesh", "Context", "node shader instanced model custom", OutputNodeShaderMeshInputs, {}, OutputNodeShaderMeshSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectOutputStrip, "Output ParticleStrip Quad", "Context", "trail ribbon strip", OutputStripInputs, {}, OutputStripSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectOutputNodeShaderStrip, "Output ParticleStrip NodeShader Quad", "Context", "node shader trail ribbon strip custom", OutputNodeShaderStripInputs, {}, OutputNodeShaderStripSettings, nullptr, NONE, false, nullptr, InFlow, false},
			{GraphEffectBlockSubgraphContext, "Block Subgraph", "Context", "", {}, {}, BlockSubgraphSettings, nullptr, NONE, false, nullptr, {}, false},
			{GraphEffectSubgraphBlock, "Subgraph Block", "Block", "subgraph", {}, {}, {}, nullptr, NONE},
			{GraphEffectSystemSubgraph, "Subgraph", "Context", "system subgraph", {}, {}, {}, nullptr, NONE, false, nullptr, {}, false},

			{GraphEffectConstantRate, "Constant Spawn Rate", "Block", "emission", ConstantRateInputs, {}, {}, nullptr, NONE},
			{GraphEffectBurst, "Burst", "Block", "single periodic", BurstInputs, {}, BurstSettings, nullptr, NONE},
			{GraphEffectIncrementStripIndex, "Increment Strip Index On Start", "Block", "strip trail ribbon index play start", IncrementStripIndexInputs, {}, {}, nullptr, NONE},
			{GraphEffectTriggerEvent, "Trigger Event", "Block", "gpu event child spawn sub emitter die collide rate distance",
			 GraphEffectBlocks::TriggerEventInputs, GraphEffectBlocks::TriggerEventOutputs, GraphEffectBlocks::TriggerEventSettings,
			 GraphEffectBlocks::TriggerEventBody, NONE},
			{GraphEffectSetAttribute, "Set Attribute", "Block", "write", SetAttributeInputs, SetAttributeOutputs,
			 SetAttributeSettings, SetAttributeBody, NONE},
			{GraphEffectSetSpawnEvent, "Set SpawnEvent", "Block", "spawn event attribute", SetSpawnEventInputs, SetAttributeOutputs,
			 SetSpawnEventSettings, SetSpawnEventBody, NONE},
			{GraphEffectAttributeFromCurve, "Attribute from Curve", "Block", "curve gradient over life", SetAttributeInputs, SetAttributeOutputs,
			 AttributeFromCurveSettings, AttributeFromCurveBody, NONE},
			{GraphEffectPositionShape, "Set Position Shape Sphere", "Block", "spawn emit sphere box cone cylinder torus circle line arc",
			 PositionShapeInputs, SetAttributeOutputs, GraphEffectBlocks::PositionShapeSettings, GraphEffectBlocks::PositionShapeBody, NONE},
			{GraphEffectForce, "Force", "Block", "push wind", GraphEffectBlocks::ForceInputs, GraphEffectBlocks::VelocityOutput,
			 GraphEffectBlocks::ForceSettings, GraphEffectBlocks::ForceBody, NONE},
			{GraphEffectGravity, "Gravity", "Block", "fall weight", GraphEffectBlocks::GravityInputs, GraphEffectBlocks::VelocityOutput, {}, GraphEffectBlocks::GravityBody, NONE},
			{GraphEffectLinearDrag, "Linear Drag", "Block", "friction slow air resistance", GraphEffectBlocks::LinearDragInputs,
			 GraphEffectBlocks::VelocityOutput, GraphEffectBlocks::LinearDragSettings, GraphEffectBlocks::LinearDragBody, NONE},
			{GraphEffectTurbulence, "Turbulence", "Block", "noise curl wind swirl", GraphEffectBlocks::TurbulenceInputs, GraphEffectBlocks::VelocityOutput,
			 GraphEffectBlocks::TurbulenceSettings, GraphEffectBlocks::TurbulenceBody, NONE},
			{GraphEffectConformToSphere, "Attractor Shape Sphere", "Block", "conform attract magnet sphere", GraphEffectBlocks::ConformToSphereInputs, GraphEffectBlocks::VelocityOutput, {}, GraphEffectBlocks::ConformToSphereBody, NONE},
			{GraphEffectCollisionShape, "Collision Shape Plane", "Block", "collide bounce collision kill trigger", CollisionShapeInputs,
			 CollisionShapeOutputs, GraphEffectBlocks::CollisionShapeSettings, GraphEffectBlocks::CollisionShapeBody, NONE},
			{GraphEffectCollisionDepth, "Collision Depth Buffer", "Block", "collide bounce collision kill trigger depth buffer camera screen",
			 CollisionDepthInputs, CollisionShapeOutputs, GraphEffectBlocks::CollisionDepthSettings, GraphEffectBlocks::CollisionDepthBody, NONE},
			{GraphEffectPositionDepth, "Set Position (Depth)", "Block", "spawn emit depth buffer camera screen surface", PositionDepthInputs,
			 PositionDepthOutputs, GraphEffectBlocks::PositionDepthSettings, GraphEffectBlocks::PositionDepthBody, NONE},
			{GraphEffectOrient, "Orient: Face Camera Plane", "Block", "orientation billboard facing rotate look at", {}, GraphEffectBlocks::AxesOutputs, GraphEffectBlocks::OrientSettings, GraphEffectBlocks::OrientBody, NONE},
			{GraphEffectCameraFade, "Camera Fade", "Block", "fade near camera plane distance cull overdraw", GraphEffectBlocks::CameraFadeInputs,
			 GraphEffectBlocks::CameraFadeOutputs, GraphEffectBlocks::CameraFadeSettings, GraphEffectBlocks::CameraFadeBody, NONE},
			{GraphEffectFlipbookPlayer, "Flipbook Player", "Block", "animation atlas frame rate sheet sprite subuv texture",
			 GraphEffectBlocks::FlipbookPlayerInputs, GraphEffectBlocks::TexIndexOutput, GraphEffectBlocks::FlipbookPlayerSettings,
			 GraphEffectBlocks::FlipbookPlayerBody, NONE},
			{GraphEffectVectorFieldForce, "Vector Field Force", "Block", "vector field force volume wind vf texture3d", GraphEffectBlocks::VectorFieldInputs,
			 GraphEffectBlocks::VelocityOutput, GraphEffectBlocks::VectorFieldSettings, GraphEffectBlocks::VectorFieldForceBody, NONE},
			{GraphEffectConformToSDF, "Attractor Shape Signed Distance Field", "Block", "conform attract magnet signed distance field sdf", GraphEffectBlocks::ConformToSDFInputs, GraphEffectBlocks::VelocityOutput, {}, GraphEffectBlocks::ConformToSDFBody, NONE},
			{GraphEffectPositionMesh, "Set Position (Mesh)", "Block", "spawn emit mesh model vertex edge surface triangle", PositionMeshInputs,
			 PositionMeshOutputs, GraphEffectBlocks::PositionMeshSettings, GraphEffectBlocks::PositionMeshBody, NONE},
			{GraphEffectPositionSequential, "Add Position Sequential Line", "Block", "spawn emit sequence sequential order line circle grid ring",
			 PositionSequentialInputs, PositionMeshOutputs, GraphEffectBlocks::PositionSequentialSettings, GraphEffectBlocks::PositionSequentialBody, NONE},
			{GraphEffectCustomHLSLBlock, "Custom HLSL", "Block", "hlsl code shader custom function script", GraphCustomHLSL::BlockInputs,
			 GraphCustomHLSL::BlockOutputs, GraphCustomHLSL::BlockSettings, GraphCustomHLSL::BlockBody, NONE},
			{GraphEffectCustomHLSLOperator, "Custom HLSL", "HLSL", "hlsl code shader custom function script", GraphCustomHLSL::OperatorInputs,
			 GraphCustomHLSL::OperatorOutputs, GraphCustomHLSL::OperatorSettings, GraphCustomHLSL::OperatorBody, NONE},

			{GraphEffectGetAttribute, "Get Attribute: position", "Attribute", "read attribute", {}, SetAttributeOutputs, GetAttributeSettings, GetAttributeBody, NONE},
			{GraphEffectAgeOverLifetime, "Age Over Lifetime", "Attribute", "normalized age life ratio", {}, AgeOverLifetimeOutputs, {}, AgeOverLifetimeBody, NONE},
			{GraphEffectRatioOverStrip, "Get Ratio Over Strip [0..1]", "Attribute", "strip trail ribbon ratio place progress", {}, AgeOverLifetimeOutputs, {}, RatioOverStripBody, NONE},
			{GraphEffectRandomNumber, "Random Number", "Random", "probability aleatory range", RandomNumberInputs, RandomNumberOutputs,
			 RandomNumberSettings, RandomNumberBody, NONE},
			{GraphEffectTotalTime, "Total Time", "BuiltIn", "clock elapsed", {}, TotalTimeOutputs, {}, TotalTimeBody, NONE},
			{GraphEffectDeltaTime, "Delta Time", "BuiltIn", "frame step", {}, DeltaTimeOutputs, {}, DeltaTimeBody, NONE},
			{GraphEffectSampleCurve, "Sample Curve", "Sampling", "animation curve evaluate", SampleCurveInputs, SampleCurveOutputs, {}, SampleCurveBody, NONE},
			{GraphEffectSampleTexture2D, "Sample Texture2D", "Sampling", "texture image read tex2d", SampleTexture2DInputs, SampleTextureOutputs, {}, SampleTexture2DBody, NONE},
			{GraphEffectSampleTexture2DArray, "Sample Texture2DArray", "Sampling", "texture array slice read", SampleTexture2DArrayInputs, SampleTextureOutputs, {}, SampleTexture2DArrayBody, NONE},
			{GraphEffectSampleTexture3D, "Sample Texture3D", "Sampling", "texture volume read tex3d", SampleTexture3DInputs, SampleTextureOutputs, {}, SampleVolumeBody, NONE},
			{GraphEffectSampleTextureCube, "Sample TextureCube", "Sampling", "texture cubemap read", SampleTextureCubeInputs, SampleTextureOutputs, {}, SampleVolumeBody, NONE},
			{GraphEffectSampleSDF, "Sample Signed Distance Field", "Sampling", "sdf distance field surface", GraphEffectBlocks::SampleSDFInputs, GraphEffectBlocks::SampleSDFOutputs, {}, GraphEffectBlocks::SampleSDFBody, NONE},
			{GraphEffectSampleMesh, "Sample Mesh", "Sampling", "mesh model vertex edge surface triangle position normal tangent color uv", SampleMeshInputs,
			 SampleMeshOutputs, GraphEffectBlocks::SampleMeshSettings, GraphEffectBlocks::SampleMeshBody, NONE},
			{GraphEffectSampleMeshIndex, "Sample Mesh Index", "Sampling", "mesh model index buffer triangle", GraphEffectBlocks::SampleMeshIndexInputs, GraphEffectBlocks::SampleMeshIndexOutputs, {}, GraphEffectBlocks::SampleMeshIndexBody, NONE},
			{GraphEffectMeshVertexCount, "Get Mesh Vertex Count", "Sampling", "mesh model vertices count number", GraphEffectBlocks::MeshInput, GraphEffectBlocks::MeshCountOutputs, {}, GraphEffectBlocks::MeshVertexCountBody, NONE},
			{GraphEffectMeshIndexCount, "Get Mesh Index Count", "Sampling", "mesh model indices count number", GraphEffectBlocks::MeshInput, GraphEffectBlocks::MeshCountOutputs, {}, GraphEffectBlocks::MeshIndexCountBody, NONE},
			{GraphEffectMeshTriangleCount, "Get Mesh Triangle Count", "Sampling", "mesh model triangles faces count number", GraphEffectBlocks::MeshInput, GraphEffectBlocks::MeshCountOutputs, {}, GraphEffectBlocks::MeshTriangleCountBody, NONE},
			{GraphEffectNoiseDefinition, "Perlin Noise 2D", "Noise", "value perlin cellular fractal octave", NoiseInputs, NoiseOutputs, NoiseSettings,
			 NoiseBody, NONE},
			{GraphEffectCurlNoise, "Perlin Curl Noise 2D", "Noise", "value perlin cellular turbulence swirl", NoiseInputs, CurlNoiseOutputs,
			 CurlNoiseSettings, CurlNoiseBody, NONE},
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
			const u32 attribute = ChoiceOf(node, GraphEffectAttributeSetting, WritableNames, 0);
			const u32 composition = ChoiceOf(node, "Composition", GraphEffectCompositions, 0);
			const u32 source = ChoiceOf(node, "Source", GraphEffectSources, 0);
			u32 random = ChoiceOf(node, "Random", GraphEffectRandoms, 0);
			if (attribute >= WRITABLE_COUNT || composition >= std::size(GraphEffectCompositions) || source >= std::size(GraphEffectSources) ||
				random >= std::size(GraphEffectRandoms)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const GraphEffectAttributeInfo& written = Attributes[attribute];
			if (written.type == T::Bool && composition != 0) {
				out.error = "composes Boolean attribute '" + str(written.name) + "' by " + str(GraphEffectCompositions[composition]) +
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
			str title = str(GraphEffectCompositions[composition]) + " " + Capitalized(written.name);
			title += source == 1 ? str(" from Source") : RandomLabel(random);
			Finish(GraphEffectSetAttribute, std::move(title), out);
		}

		void BuildSetSpawnEvent(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 attribute = ChoiceOf(node, GraphEffectAttributeSetting, WritableNames, 0);
			u32 random = ChoiceOf(node, "Random Mode", GraphEffectRandoms, 0);
			if (attribute >= WRITABLE_COUNT || random >= std::size(GraphEffectRandoms)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const GraphEffectAttributeInfo& written = Attributes[attribute];
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
			Finish(GraphEffectSetSpawnEvent, "Set SpawnEvent " + Capitalized(written.name) + RandomLabel(random), out);
		}

		void BuildAttributeFromCurve(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 attribute = ChoiceOf(node, GraphEffectAttributeSetting, WritableNames, 0);
			const u32 composition = ChoiceOf(node, "Composition", GraphEffectCompositions, 0);
			const u32 alphaComposition = ChoiceOf(node, "Alpha Composition", GraphEffectCompositions, 0);
			const u32 sampleMode = ChoiceOf(node, "Sample Mode", GraphEffectSampleModes, 0);
			const u32 mode = ChoiceOf(node, "Mode", ComputeModes, 1);
			const u32 colorMode = ChoiceOf(node, "Color Mode", ColorModes, 2);
			if (attribute >= WRITABLE_COUNT || composition >= std::size(GraphEffectCompositions) ||
				alphaComposition >= std::size(GraphEffectCompositions) || sampleMode >= std::size(GraphEffectSampleModes) ||
				mode >= std::size(ComputeModes) || colorMode >= std::size(ColorModes)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const GraphEffectAttributeInfo& written = Attributes[attribute];
			if (written.type == T::Bool) {
				out.error = "reads Boolean attribute '" + str(written.name) + "' from a curve (a Boolean has none)";
				return;
			}
			const bool color = written.name == "color";
			const bool perComponent = mode == 1;
			const str& label = out.strings.emplace_back(Capitalized(written.name));
			GraphPortDefinition sampled;
			sampled.type = color && perComponent ? P::Gradient : P::Curve;
			sampled.defaultJSON = color && perComponent ? BlockGradient : std::string_view();
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
			Finish(GraphEffectAttributeFromCurve, str(GraphEffectCompositions[composition]) + " " + label + " " + str(GraphEffectSampleModeNames[sampleMode]),
				   out);
		}

		void BuildGetAttribute(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 attribute = ChoiceOf(node, GraphEffectAttributeSetting, AttributeNames, 0);
			const u32 location = ChoiceOf(node, "Location", GraphEffectLocations, 0);
			if (attribute >= std::size(Attributes) || location >= std::size(GraphEffectLocations)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			AttributeOutput(Attributes[attribute], out);
			str title = "Get Attribute: " + str(Attributes[attribute].name);
			if (location == 1) {
				title += " (Source)";
			}
			Finish(GraphEffectGetAttribute, std::move(title), out);
		}

		void BuildRandomNumber(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 type = ChoiceOf(node, "Type", RandomTypes, 0);
			if (type >= std::size(RandomTypes)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			constexpr P TYPES[] = {P::Float, P::Float2, P::Float3, P::Float4};
			for (const GraphPortDefinition& input : RandomNumberInputs) {
				GraphPortDefinition port = input;
				if (input.name != "Seed") {
					port.type = TYPES[type];
					for (u32 i = 1; i < 4; ++i) {
						port.defaultValue[i] = port.defaultValue[0];
					}
				}
				out.inputs.push_back(port);
			}
			GraphPortDefinition r = RandomNumberOutputs[0];
			r.type = TYPES[type];
			out.outputs.push_back(r);
			Finish(GraphEffectRandomNumber, type == 0 ? str("Random Number") : "Random Number (" + str(RandomTypes[type]) + ")", out);
		}

		// arc stored in degrees
		void BuildPositionShape(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 shape = ChoiceOf(node, "Shape", B::PositionShapes, 0);
			const u32 mode = ChoiceOf(node, "Position Mode", B::PositionModes, 0);
			const u32 spawn = ChoiceOf(node, "Spawn Mode", B::SpawnModes, 0);
			const u32 orientation = ChoiceOf(node, "Apply Orientation", B::Orientations, 1);
			const u32 compositions[] = {ChoiceOf(node, "Composition Position", GraphEffectCompositions, 0),
										ChoiceOf(node, "Composition Direction", GraphEffectCompositions, 0),
										ChoiceOf(node, "Composition Axes", GraphEffectCompositions, 0)};
			if (shape >= std::size(B::PositionShapes) || mode >= std::size(B::PositionModes) || spawn >= std::size(B::SpawnModes) ||
				ChoiceOf(node, "Height Mode", B::HeightModes, 1) >= std::size(B::HeightModes) || orientation >= std::size(B::Orientations) ||
				std::any_of(std::begin(compositions), std::end(compositions), [](u32 composition) { return composition >= std::size(GraphEffectCompositions); })) {
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
				for (const GraphPortDefinition& port : B::FieldTransformInputs) {
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
			Finish(GraphEffectPositionShape, str(GraphEffectCompositions[compositions[0]]) + " Position Shape " + str(B::PositionShapeNames[shape]), out);
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
			const u32 shape = ChoiceOf(node, "Shape", B::CollisionShapes, B::CollidePlane);
			const u32 radius = ChoiceOf(node, "Radius Mode", B::RadiusModes, 0);
			const u32 behavior = ChoiceOf(node, "Behavior", B::Behaviors, B::BehaviorCollision);
			const u32 attributes = ChoiceOf(node, "Collision Attributes", B::CollisionAttributes, 0);
			if (shape >= std::size(B::CollisionShapes) || radius >= std::size(B::RadiusModes) ||
				ChoiceOf(node, "Mode", B::CollisionModes, 0) >= std::size(B::CollisionModes) || behavior >= std::size(B::Behaviors) ||
				attributes >= std::size(B::CollisionAttributes)) {
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
				for (const GraphPortDefinition& port : B::FieldTransformInputs) {
					out.inputs.push_back(port);
				}
				break;
			default:
				add("Position", {0.0f, 0.0f, 0.0f}, GraphPortSpace::Position);
				add("Normal", {0.0f, 1.0f, 0.0f}, GraphPortSpace::Direction);
				break;
			}
			CollisionPorts(node, out, behavior, radius, attributes, shape == B::CollideSphere);
			Finish(GraphEffectCollisionShape, str(B::BehaviorNames[behavior]) + " Shape " + str(B::CollisionShapeNames[shape]), out);
		}

		void BuildCollisionDepth(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 thickness = ChoiceOf(node, "Surface Thickness", B::SurfaceThicknesses, 0);
			const u32 radius = ChoiceOf(node, "Radius Mode", B::RadiusModes, 0);
			const u32 behavior = ChoiceOf(node, "Behavior", B::Behaviors, B::BehaviorCollision);
			const u32 attributes = ChoiceOf(node, "Collision Attributes", B::CollisionAttributes, 0);
			if (thickness >= std::size(B::SurfaceThicknesses) || radius >= std::size(B::RadiusModes) || behavior >= std::size(B::Behaviors) ||
				attributes >= std::size(B::CollisionAttributes)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			CollisionPorts(node, out, behavior, radius, attributes, false);
			if (thickness == 1) {
				AddPort(out, "Surface Thickness", {1.0f}, GraphPortSpace::None, P::Float);
			}
			Finish(GraphEffectCollisionDepth, str(B::BehaviorNames[behavior]) + " Depth Buffer", out);
		}

		void BuildPositionDepth(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 mode = ChoiceOf(node, "Mode", B::DepthPositionModes, 0);
			const u32 cull = ChoiceOf(node, "Cull Mode", B::DepthCullModes, 0);
			const u32 composition = ChoiceOf(node, "Composition Position", GraphEffectCompositions, 0);
			if (mode >= std::size(B::DepthPositionModes) || cull >= std::size(B::DepthCullModes) || composition >= std::size(GraphEffectCompositions)) {
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
			Finish(GraphEffectPositionDepth, str(GraphEffectCompositions[composition]) + " Position (Depth)", out);
		}

		void BuildOrient(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 mode = ChoiceOf(node, "Mode", B::OrientModes, 0);
			const u32 pair = ChoiceOf(node, "Axes", B::OrientAxesOptions, B::AxesZY);
			if (mode >= std::size(B::OrientModes) || pair >= std::size(B::OrientAxesOptions)) {
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
				constexpr std::string_view Names[] = {"Axis X", "Axis Y", "Axis Z"};
				constexpr u32 FIRST[] = {0, 1, 2, 1, 2, 0};
				constexpr u32 SECOND[] = {1, 2, 0, 0, 1, 2};
				for (const u32 axis : {FIRST[pair], SECOND[pair]}) {
					add(Names[axis], {axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f}, GraphPortSpace::Direction);
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
			out.outputs.assign(std::begin(B::AxesOutputs), std::end(B::AxesOutputs));
			Finish(GraphEffectOrient, "Orient: " + str(B::OrientModeNames[mode]), out);
		}

		void BuildFlipbookPlayer(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 mode = ChoiceOf(node, "Mode", B::FlipbookModes, 0);
			const u32 rate = ChoiceOf(node, "Frame Rate Mode", B::FrameRateModes, 0);
			const u32 cycles = ChoiceOf(node, "Cycles Mode", B::CyclesModes, 0);
			const u32 range = ChoiceOf(node, "Animation Range", B::AnimationRanges, 0);
			if (mode >= std::size(B::FlipbookModes) || rate >= std::size(B::FrameRateModes) || cycles >= std::size(B::CyclesModes) ||
				range >= std::size(B::AnimationRanges)) {
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
					out.inputs.back().defaultJSON = out.strings.emplace_back(GraphCurves::Write(constant));
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
			Finish(GraphEffectFlipbookPlayer, "Flipbook Player", out);
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
			const u32 mode = ChoiceOf(node, "Mode", B::AddressingModes, B::AddressClamp);
			const u32 placement = ChoiceOf(node, "Placement Mode", B::MeshPlacements, B::PlaceVertex);
			const u32 surface = ChoiceOf(node, "Surface Coordinates", B::SurfaceCoordinatesOptions, B::SurfaceUniform);
			if (mode >= std::size(B::AddressingModes) || placement >= std::size(B::MeshPlacements) || surface >= std::size(B::SurfaceCoordinatesOptions)) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			out.inputs.push_back(B::MeshInput[0]);
			MeshPlacementPorts(out, placement, surface);
			out.inputs.insert(out.inputs.end(), std::begin(B::MeshTransformInputs), std::end(B::MeshTransformInputs));
			constexpr P TYPES[] = {P::Float3, P::Float3, P::Float3, P::Float3, P::Float, P::Float4, P::Float4};
			for (u32 output = 0; output < B::MESH_OUTPUT_COUNT; ++output) {
				if (ToggleOf(node, B::SampleMeshSettings[output].key, B::SampleMeshSettings[output].defaultIndex != 0)) {
					out.outputs.push_back({B::MeshOutputs[output], TYPES[output]});
				}
			}
			Finish(GraphEffectSampleMesh, "Sample Mesh", out);
		}

		void BuildPositionMesh(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 spawn = ChoiceOf(node, "Spawn Mode", B::SpawnModes, 0);
			const u32 placement = ChoiceOf(node, "Placement Mode", B::MeshPlacements, B::PlaceVertex);
			const u32 surface = ChoiceOf(node, "Surface Coordinates", B::SurfaceCoordinatesOptions, B::SurfaceUniform);
			const u32 orientation = ChoiceOf(node, "Apply Orientation", B::Orientations, 1);
			const u32 compositions[] = {ChoiceOf(node, "Composition Position", GraphEffectCompositions, 0),
										ChoiceOf(node, "Composition Direction", GraphEffectCompositions, 0)};
			const u32 axesComposition = ChoiceOf(node, "Composition Axes", B::AxesCompositions, 0);
			if (spawn >= std::size(B::SpawnModes) || placement >= std::size(B::MeshPlacements) || surface >= std::size(B::SurfaceCoordinatesOptions) ||
				ChoiceOf(node, "Mode", B::AddressingModes, B::AddressClamp) >= std::size(B::AddressingModes) || orientation >= std::size(B::Orientations) ||
				axesComposition >= std::size(B::AxesCompositions) ||
				std::any_of(std::begin(compositions), std::end(compositions), [](u32 composition) { return composition >= std::size(GraphEffectCompositions); })) {
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
			out.inputs.push_back(B::MeshInput[0]);
			if (spawn == 1) {
				MeshPlacementPorts(out, placement, surface);
			}
			out.inputs.insert(out.inputs.end(), std::begin(B::MeshTransformInputs), std::end(B::MeshTransformInputs));
			AttributeOutput(*GraphEffectAttributes::Find("position"), out);
			if (direction) {
				AttributeOutput(*GraphEffectAttributes::Find("direction"), out);
			}
			if (axes) {
				for (std::string_view axis : {"axisX", "axisY", "axisZ"}) {
					AttributeOutput(*GraphEffectAttributes::Find(axis), out);
				}
			}
			Finish(GraphEffectPositionMesh, str(GraphEffectCompositions[compositions[0]]) + " Position (Mesh)", out);
		}

		void BuildPositionSequential(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 compositions[] = {ChoiceOf(node, "Composition Position", GraphEffectCompositions, 1),
										ChoiceOf(node, "Composition Direction", GraphEffectCompositions, 0),
										ChoiceOf(node, "Composition Target Position", GraphEffectCompositions, 1)};
			const u32 shape = ChoiceOf(node, "Shape", B::SequentialShapes, B::SequentialLine);
			const u32 index = ChoiceOf(node, "Index", B::SequentialIndexModes, 0);
			if (shape >= std::size(B::SequentialShapes) || index >= std::size(B::SequentialIndexModes) ||
				ChoiceOf(node, "Mode", B::AddressingModes, B::AddressClamp) >= std::size(B::AddressingModes) ||
				std::any_of(std::begin(compositions), std::end(compositions), [](u32 composition) { return composition >= std::size(GraphEffectCompositions); })) {
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
			Finish(GraphEffectPositionSequential,
				   str(GraphEffectCompositions[compositions[0]]) + " Position Sequential " + str(B::SequentialShapes[shape]), out);
		}

		void BuildTriggerEvent(const GraphNode& node, GraphBuiltDefinition& out) {
			namespace B = GraphEffectBlocks;
			const u32 mode = ChoiceOf(node, "Mode", GraphEffectTriggerModes, 0);
			if (mode >= std::size(GraphEffectTriggerModes)) {
				out.error = "has a Mode that isn't one of its options";
				return;
			}
			out.inputs.assign(std::begin(B::TriggerEventInputs), std::end(B::TriggerEventInputs));
			out.outputs.assign(std::begin(B::TriggerEventOutputs), std::end(B::TriggerEventOutputs));
			Finish(GraphEffectTriggerEvent, "Trigger Event " + str(B::TriggerModeNames[mode]), out);
		}

		void BuildInitialize(const GraphNode& node, GraphBuiltDefinition& out) {
			const u32 type = ChoiceOf(node, "Data Type", GraphEffectDataTypes, 0);
			if (type >= std::size(GraphEffectDataTypes)) {
				out.error = "has a Data Type that isn't one of its options";
				return;
			}
			out.inputs.assign(std::begin(InitializeInputs), std::end(InitializeInputs));
			Finish(GraphEffectInitialize, type == 1 ? "Initialize Particle Strip" : "Initialize Particle", out);
		}

		void BuildNoise(const GraphNode& node, bool curl, GraphBuiltDefinition& out) {
			const std::span<const std::string_view> dimensions = curl ? std::span<const std::string_view>(GraphEffectCurlDimensions)
																	  : std::span<const std::string_view>(GraphEffectNoiseDimensions);
			const u32 type = ChoiceOf(node, "Type", GraphEffectNoiseTypes, 1);
			const u32 dimension = ChoiceOf(node, "Dimensions", dimensions, curl ? 0 : 1);
			if (type >= std::size(GraphEffectNoiseTypes) || dimension >= dimensions.size()) {
				out.error = "has a setting that isn't one of its options";
				return;
			}
			const u32 width = curl ? dimension + 2 : dimension + 1;
			constexpr P VECTORS[] = {P::Float, P::Float2, P::Float3};
			out.inputs.push_back({"Coordinate", VECTORS[width - 1]});
			out.inputs.insert(out.inputs.end(), std::begin(NoiseCommonInputs), std::end(NoiseCommonInputs));
			if (curl) {
				out.inputs.push_back({"Amplitude", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}});
				out.outputs.push_back({"Noise", VECTORS[width - 1]});
			} else {
				out.inputs.push_back({"Range", P::Float2, {-1.0f, 1.0f, 0.0f, 0.0f}});
				out.outputs.push_back({"Noise", P::Float});
				out.outputs.push_back({"Derivatives", VECTORS[width - 1]});
			}
			Finish(curl ? GraphEffectCurlNoise : GraphEffectNoiseDefinition,
				   std::format("{} {} {}D", GraphEffectNoiseTypes[type], curl ? "Curl Noise" : "Noise", width), out);
		}

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphEffectDefinitions() { return EffectDefinitions; }

	namespace GraphEffectAttributes {

		std::span<const GraphEffectAttributeInfo> All() { return Attributes; }

		const GraphEffectAttributeInfo* Find(std::string_view name) {
			for (const GraphEffectAttributeInfo& attribute : Attributes) {
				if (attribute.name == name) {
					return &attribute;
				}
			}
			return nullptr;
		}

		str CurrentBinding(std::string_view name) { return "Attribute/" + str(name); }

		str SourceBinding(std::string_view name) { return "SourceAttribute/" + str(name); }

		str RateCount(u32 node) { return str(GraphEffectRateCount) + "_" + std::to_string(node); }

		bool BuildsPerNode(std::string_view def) {
			return def == GraphEffectSetAttribute || def == GraphEffectSetSpawnEvent || def == GraphEffectGetAttribute ||
				   def == GraphEffectRandomNumber || def == GraphEffectAttributeFromCurve || def == GraphEffectNoiseDefinition ||
				   def == GraphEffectCurlNoise || def == GraphEffectPositionShape || def == GraphEffectCollisionShape ||
				   def == GraphEffectCollisionDepth || def == GraphEffectPositionDepth || def == GraphEffectOrient || def == GraphEffectFlipbookPlayer || def == GraphEffectSampleMesh ||
				   def == GraphEffectPositionMesh || def == GraphEffectPositionSequential || def == GraphEffectInitialize ||
				   def == GraphEffectTriggerEvent;
		}

		void Build(const GraphNode& node, GraphBuiltDefinition& out) {
			if (node.def == GraphEffectSetAttribute) {
				BuildSetAttribute(node, out);
			} else if (node.def == GraphEffectSetSpawnEvent) {
				BuildSetSpawnEvent(node, out);
			} else if (node.def == GraphEffectGetAttribute) {
				BuildGetAttribute(node, out);
			} else if (node.def == GraphEffectRandomNumber) {
				BuildRandomNumber(node, out);
			} else if (node.def == GraphEffectAttributeFromCurve) {
				BuildAttributeFromCurve(node, out);
			} else if (node.def == GraphEffectNoiseDefinition || node.def == GraphEffectCurlNoise) {
				BuildNoise(node, node.def == GraphEffectCurlNoise, out);
			} else if (node.def == GraphEffectPositionShape) {
				BuildPositionShape(node, out);
			} else if (node.def == GraphEffectCollisionShape) {
				BuildCollisionShape(node, out);
			} else if (node.def == GraphEffectCollisionDepth) {
				BuildCollisionDepth(node, out);
			} else if (node.def == GraphEffectPositionDepth) {
				BuildPositionDepth(node, out);
			} else if (node.def == GraphEffectOrient) {
				BuildOrient(node, out);
			} else if (node.def == GraphEffectFlipbookPlayer) {
				BuildFlipbookPlayer(node, out);
			} else if (node.def == GraphEffectSampleMesh) {
				BuildSampleMesh(node, out);
			} else if (node.def == GraphEffectPositionMesh) {
				BuildPositionMesh(node, out);
			} else if (node.def == GraphEffectPositionSequential) {
				BuildPositionSequential(node, out);
			} else if (node.def == GraphEffectInitialize) {
				BuildInitialize(node, out);
			} else if (node.def == GraphEffectTriggerEvent) {
				BuildTriggerEvent(node, out);
			} else {
				out.error = "is no node an effect builds";
			}
		}

	} // namespace GraphEffectAttributes

	namespace GraphEffectShading {

		constexpr std::string_view DefaultDocument = R"({
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
				if (setting.key != GraphEffectNodeShaderSetting) {
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

		std::string_view DefaultDocumentText() { return DefaultDocument; }

		bool IsNodeShaderOutput(std::string_view def) {
			return def == GraphEffectOutputNodeShader || def == GraphEffectOutputNodeShaderMesh || def == GraphEffectOutputNodeShaderStrip;
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
						port.binding = GraphEffectNodeShaderTextureBindings[kind];
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
